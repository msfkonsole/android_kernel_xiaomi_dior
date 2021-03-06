/*
 * drivers/w1/slaves/w1_bq2022.c
 *
 * Copyright (C) 2015 Xiaomi, Inc.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>

#include "../w1.h"
#include "../w1_int.h"
#include "../w1_family.h"
#include <linux/delay.h>


#define HDQ_CMD_SKIP_ROM (0xCC)
#define HDQ_CMD_READ_FIELD (0xF0)

#define CRYPT_COMMON_HEADER	(0xE54C21ED)

/*PSEUDO_INFO = int(B8, B61, B62, B63*/
#define BQ2022_ID_COSLIGHT		(0xdf0c7a62)
#define BQ2022_ID_AAC			(0xaacaacaa)
#define BQ2022_ID_DELSA			(0x8412e562)

/*PSEUDO_ID*/
/* 0x30000	12kohm */
/* 0x40000	17kohm */
/* 0x50000	22kohm */
/* 0x60000	28kohm */
/* 0x70000	?? */
#define PSEUDO_KOHM_COSLIGHT	(0x30000)		/*batt_id_kohm =12*/
#define PSEUDO_KOHM_AAC	(0x40000)		/*batt_id_kohm =17*/
#define PSEUDO_KOHM_DELSA	(0x50000)		/*batt_id_kohm =22*/

#define GEN_PSEUDO_INFO(ptr) (((*((unsigned int *)&ptr[60]))&0xFFFFFF00)|((unsigned int)ptr[8]))
#define GEN_PSEUDO_HEADER(ptr) (*((unsigned int *)&ptr[0]))



static int F_ID = 0x9;

static char batt_crypt_info[128];
static struct w1_slave *bq2022_slave;

static int w1_bq2022_read(void);

static int bq2022_debug;
static int set_debug_status_param(const char *val, struct kernel_param *kp)
{
        int ret;

        ret = param_set_int(val, kp);
        if (ret) {
                pr_err("error setting value %d\n", ret);
                return ret;
        }

        pr_info("Set debug param to %d\n", bq2022_debug);
        if (bq2022_debug) {
		int i, j;

		w1_bq2022_read();
		for (i = 0; i < 4; i++) {
			printk("Page %d ", i);
			for (j = 0; j < 32; j++) {
				printk("%02x ", *(batt_crypt_info + (i * 32 + j)));
			}
			printk("\n");
		}
	}

	return 0;
}
module_param_call(debug, set_debug_status_param, param_get_uint,
                &bq2022_debug, 0644);

int w1_bq2022_battery_id(void)
{
	unsigned int header, pseduo_info;
	int ret = 0;

	/* The first four bytes */
	header = GEN_PSEUDO_HEADER(batt_crypt_info);

	/*PSEUDO_INFO = int(B8, B61, B62, B63*/
	pseduo_info = GEN_PSEUDO_INFO(batt_crypt_info);

	if (header != CRYPT_COMMON_HEADER)
	{
		pr_err("cannot read batt id through one-wire\n");
		return ret;
	}
	else
	{
		pr_info("pseduo_info:0x%08x\n",pseduo_info);
	}

	switch(pseduo_info) {
		case BQ2022_ID_COSLIGHT:
			ret = PSEUDO_KOHM_COSLIGHT;
			break;
		case BQ2022_ID_AAC:
			ret = PSEUDO_KOHM_AAC;
			break;
		case BQ2022_ID_DELSA:
			ret = PSEUDO_KOHM_DELSA;
			break;
		default:
			ret = 0;
	}
	bq2022_debug = ret;
	return ret;
}

static int w1_bq2022_read(void)
{
	struct w1_slave *sl = bq2022_slave;
	char cmd[4];
	u8 crc, calc_crc;
	int retries = 5;

	if (!sl) {
		pr_err("No w1 device\n");
		return -1;
	}

retry:
	/* Initialization, master's mutex should be hold */
	if (!(retries--)) {
		pr_err("w1_bq2022_read fatal error\n");
		return -1;
	}

	if (w1_reset_bus(sl->master)) {
		pr_warn("reset bus failed, just retry!\n");
		goto retry;
	}

	/* rom comm byte + read comm byte + addr 2 bytes */
	cmd[0] = HDQ_CMD_SKIP_ROM;
	cmd[1] = HDQ_CMD_READ_FIELD;
	cmd[2] = 0x0;
	cmd[3] = 0x0;

	/* send command */
	w1_write_block(sl->master, cmd, 4);

	/* crc verified for read comm byte and addr 2 bytes*/
	crc = w1_read_8(sl->master);
	calc_crc = w1_calc_crc8(&cmd[1], 3);
	if (calc_crc != crc) {
		pr_err("com crc err\n");
		goto retry;
	}

	/* read the whole memory, 1024-bit */
	w1_read_block(sl->master, batt_crypt_info, 128);

	/* crc verified for data */
	crc = w1_read_8(sl->master);
	calc_crc = w1_calc_crc8(batt_crypt_info, 128);
	if (calc_crc != crc) {
		pr_err("w1_bq2022 data crc err\n");
		goto retry;
	}

	return 0;

}

static int w1_bq2022_add_slave(struct w1_slave *sl)
{
	bq2022_slave = sl;
	return w1_bq2022_read();
}

static void w1_bq2022_remove_slave(struct w1_slave *sl)
{
	bq2022_slave = NULL;
}

static struct w1_family_ops w1_bq2022_fops = {
	.add_slave	= w1_bq2022_add_slave,
	.remove_slave	= w1_bq2022_remove_slave,
};

static struct w1_family w1_bq2022_family = {
	.fid = 1,
	.fops = &w1_bq2022_fops,
};

static int __init w1_bq2022_init(void)
{
	if (F_ID)
		w1_bq2022_family.fid = F_ID;

	return w1_register_family(&w1_bq2022_family);
}

static void __exit w1_bq2022_exit(void)
{
	w1_unregister_family(&w1_bq2022_family);
}

module_init(w1_bq2022_init);
module_exit(w1_bq2022_exit);

module_param(F_ID, int, S_IRUSR);
MODULE_PARM_DESC(F_ID, "1-wire slave FID for BQ device");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xiaomi Ltd");
MODULE_DESCRIPTION("HDQ/1-wire slave driver bq2022 battery chip");
