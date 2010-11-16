/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/nct1008.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define NCT_BIT_CONFIG_RUN_STOP 6

struct nct1008_data {
	struct i2c_client *client;
};

static uint32_t nct1008_debug;
module_param_named(temp_debug, nct1008_debug, uint, 0664);

static int nct1008_run(struct nct1008_data *nct, u8 run)
{
	int err;
	u8 rd_val;
	u8 wr_val;

	rd_val = i2c_smbus_read_byte_data(nct->client, NCT_CONFIG_RD);
	if (rd_val < 0) {
		pr_err("%s: config register read fail: %d\n", __func__, rd_val);
		return rd_val;
	}

	if (nct1008_debug)
		pr_info("%s: Previous config is 0x%02x\n", __func__, rd_val);

	if (run)
		wr_val = rd_val & ~(1 << NCT_BIT_CONFIG_RUN_STOP);
	else
		wr_val = rd_val | (1 << NCT_BIT_CONFIG_RUN_STOP);
	err = i2c_smbus_write_byte_data(nct->client, NCT_CONFIG_WR, wr_val);
	if (err)
		pr_err("%s: setting RUN/STOP failed: %d\n", __func__, err);

	return err;
}

static int nct1008_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct nct1008_data *nct;

	nct = kzalloc(sizeof(struct nct1008_data), GFP_KERNEL);
	if (nct == NULL)
		return -ENOMEM;

	nct->client = client;
	i2c_set_clientdata(client, nct);

	return 0;
}

static int nct1008_remove(struct i2c_client *client)
{
	struct nct1008_data *nct = i2c_get_clientdata(client);

	kfree(nct);
	return 0;
}

static int nct1008_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct nct1008_data *nct = i2c_get_clientdata(client);

	if (nct1008_debug)
		pr_info("%s: Suspending\n", __func__);

	return nct1008_run(nct, 0);
}

static int nct1008_resume(struct i2c_client *client)
{
	struct nct1008_data *nct = i2c_get_clientdata(client);

	if (nct1008_debug)
		pr_info("%s: Resuming\n", __func__);

	return nct1008_run(nct, 1);
}

static const struct i2c_device_id nct1008_id[] = {
	{"nct1008", 0},
	{}
};

static struct i2c_driver nct1008_i2c_driver = {
	.probe = nct1008_probe,
	.remove = nct1008_remove,
	.suspend = nct1008_suspend,
	.resume = nct1008_resume,
	.id_table = nct1008_id,
	.driver = {
		.name = "nct1008",
		.owner = THIS_MODULE,
	},
};

static int __init nct1008_init(void)
{
	return i2c_add_driver(&nct1008_i2c_driver);
}

static void __exit nct1008_exit(void)
{
	i2c_del_driver(&nct1008_i2c_driver);
}

module_init(nct1008_init);
module_exit(nct1008_exit);

MODULE_DESCRIPTION("Temperature sensor driver for OnSemi NCT1008");
MODULE_AUTHOR("Motorola");
MODULE_LICENSE("GPL");
