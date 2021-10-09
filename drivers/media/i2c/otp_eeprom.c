// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Rockchip Electronics Co., Ltd.

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/proc_fs.h>
#include <linux/rk-camera-module.h>
#include <linux/sem.h>
#include <linux/seq_file.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include "otp_eeprom.h"

#define DEVICE_NAME			"otp_eeprom"

static inline struct eeprom_device
	*sd_to_eeprom(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct eeprom_device, sd);
}

/* Read registers up to 4 at a time */
static int read_reg_otp(struct i2c_client *client, u16 reg,
	unsigned int len, u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static u8 get_vendor_flag(struct i2c_client *client)
{
	u8 vendor_flag = 0;

	if (client->addr == SLAVE_ADDRESS)
		vendor_flag |= 0x80;
	return vendor_flag;
}

static int otp_read_data(struct eeprom_device *eeprom_dev)
{
	struct i2c_client *client = eeprom_dev->client;
	int i;
	struct otp_info *otp_ptr;
	struct device *dev = &eeprom_dev->client->dev;
	int ret = 0;
	u32 temp = 0;

	otp_ptr = kzalloc(sizeof(*otp_ptr), GFP_KERNEL);
	if (!otp_ptr)
		return -ENOMEM;
	/* OTP base information*/
	ret = read_reg_otp(client, INFO_FLAG_REG,
		1, &otp_ptr->basic_data.flag);
	if (otp_ptr->basic_data.flag == 0x01) {
		ret |= read_reg_otp(client, INFO_ID_REG,
			1, &otp_ptr->basic_data.id.supplier_id);
		ret |= read_reg_otp(client, INFO_ID_REG + 1,
			1, &otp_ptr->basic_data.id.year);
		ret |= read_reg_otp(client, INFO_ID_REG + 2,
			1, &otp_ptr->basic_data.id.month);
		ret |= read_reg_otp(client, INFO_ID_REG + 3,
			1, &otp_ptr->basic_data.id.day);
		ret |= read_reg_otp(client, INFO_ID_REG + 4,
			1, &otp_ptr->basic_data.id.sensor_id);
		ret |= read_reg_otp(client, INFO_ID_REG + 5,
			1, &otp_ptr->basic_data.id.lens_id);
		ret |= read_reg_otp(client, INFO_ID_REG + 6,
			1, &otp_ptr->basic_data.id.vcm_id);
		ret |= read_reg_otp(client, INFO_ID_REG + 7,
			1, &otp_ptr->basic_data.id.driver_ic_id);
		ret |= read_reg_otp(client, INFO_ID_REG + 8,
			1, &otp_ptr->basic_data.id.color_temperature_id);
		for (i = 0; i < SMARTISAN_PN_SIZE; i++) {
			ret |= read_reg_otp(client, SMARTISAN_PN_REG + i,
				1, &temp);
			otp_ptr->basic_data.smartisan_pn[i] = temp;
		}
		for (i = 0; i < MOUDLE_ID_SIZE; i++) {
			ret |= read_reg_otp(client, MOUDLE_ID_REG + i,
				1, &temp);
				otp_ptr->basic_data.modul_id[i] = temp;
		}
		ret |= read_reg_otp(client, MIRROR_FLIP_REG,
			1, &otp_ptr->basic_data.mirror_flip);
		ret |= read_reg_otp(client, FULL_SIZE_WIGHT_REG,
			2, &temp);
		otp_ptr->basic_data.size.width = temp;
		ret |= read_reg_otp(client, FULL_SIZE_HEIGHT_REG,
			2, &temp);
		otp_ptr->basic_data.size.height = temp;
		ret |= read_reg_otp(client, INFO_CHECKSUM_REG,
			1, &otp_ptr->basic_data.checksum);

		dev_dbg(dev, "fasic info: supplier_id(0x%x) lens(0x%x) time(%d_%d_%d)\n",
			otp_ptr->basic_data.id.supplier_id,
			otp_ptr->basic_data.id.lens_id,
			otp_ptr->basic_data.id.year,
			otp_ptr->basic_data.id.month,
			otp_ptr->basic_data.id.day);
		if (ret)
			goto err;
	}

	/* OTP WB calibration data */
	ret = read_reg_otp(client, AWB_FLAG_REG,
		1, &otp_ptr->awb_data.flag);
	if (otp_ptr->awb_data.flag == 0x01) {
		ret |= read_reg_otp(client, AWB_VERSION_REG,
			1, &otp_ptr->awb_data.version);
		ret |= read_reg_otp(client, CUR_R_REG,
			2, &otp_ptr->awb_data.r_ratio);
		ret |= read_reg_otp(client, CUR_B_REG,
			2, &otp_ptr->awb_data.b_ratio);
		ret |= read_reg_otp(client, CUR_G_REG,
			2, &otp_ptr->awb_data.g_ratio);
		ret |= read_reg_otp(client, GOLDEN_R_REG,
			2, &otp_ptr->awb_data.r_golden);
		ret |= read_reg_otp(client, GOLDEN_B_REG,
			2, &otp_ptr->awb_data.b_golden);
		ret |= read_reg_otp(client, GOLDEN_G_REG,
			2, &otp_ptr->awb_data.g_golden);
		ret |= read_reg_otp(client, AWB_CHECKSUM_REG,
			1, &otp_ptr->awb_data.checksum);

		dev_dbg(dev, "awb version:0x%x\n",
			otp_ptr->awb_data.version);
		if (ret)
			goto err;
	}

	/* OTP LSC calibration data */
	ret = read_reg_otp(client, LSC_FLAG_REG,
		1, &otp_ptr->lsc_data.flag);
	if (otp_ptr->lsc_data.flag == 0x01) {
		ret |= read_reg_otp(client, LSC_VERSION_REG,
			1, &otp_ptr->lsc_data.version);
		ret |= read_reg_otp(client, LSC_TABLE_SIZE_REG,
			2, &temp);
		otp_ptr->lsc_data.table_size = temp;
		for (i = 0; i < LSC_DATA_SIZE; i++) {
			ret |= read_reg_otp(client, LSC_DATA_START_REG + i,
				1, &temp);
			otp_ptr->lsc_data.data[i] = temp;
		}
		ret |= read_reg_otp(client, LSC_CHECKSUM_REG,
			1, &otp_ptr->lsc_data.checksum);
		dev_dbg(dev, "lsc cur:(version 0x%x, table_size 0x%x checksum 0x%x)\n",
			otp_ptr->lsc_data.version,
			otp_ptr->lsc_data.table_size,
			otp_ptr->lsc_data.checksum);
		if (ret)
			goto err;
	}

	/* OTP sfr calibration data */
	ret = read_reg_otp(client, LSC_FLAG_REG,
		1, &otp_ptr->sfr_otp_data.flag);
	if (otp_ptr->sfr_otp_data.flag == 0x01) {
		ret |= read_reg_otp(client, SFR_EQUIQ_NUM_REG,
			1, &otp_ptr->sfr_otp_data.equip_num);
		ret |= read_reg_otp(client, SFR_C_HOR_REG,
			2, &otp_ptr->sfr_otp_data.center_horizontal);
		ret |= read_reg_otp(client, SFR_C_VER_REG,
			2, &otp_ptr->sfr_otp_data.center_vertical);
		for (i = 0; i < 3; i++) {
			ret |= read_reg_otp(client, SFR_TOP_L_HOR_REG + 16 * i,
				2, &otp_ptr->sfr_otp_data.data[i].top_l_horizontal);
			ret |= read_reg_otp(client, SFR_TOP_L_VER_REG + 16 * i,
				2, &otp_ptr->sfr_otp_data.data[i].top_l_vertical);
			ret |= read_reg_otp(client, SFR_TOP_R_HOR_REG + 16 * i,
				2, &otp_ptr->sfr_otp_data.data[i].top_r_horizontal);
			ret |= read_reg_otp(client, SFR_TOP_R_VER_REG + 16 * i,
				2, &otp_ptr->sfr_otp_data.data[i].top_r_vertical);
			ret |= read_reg_otp(client, SFR_BOTTOM_L_HOR_REG + 16 * i,
				2, &otp_ptr->sfr_otp_data.data[i].bottom_l_horizontal);
			ret |= read_reg_otp(client, SFR_BOTTOM_L_VER_REG + 16 * i,
				2, &otp_ptr->sfr_otp_data.data[i].bottom_l_vertical);
			ret |= read_reg_otp(client, SFR_BOTTOM_R_HOR_REG + 16 * i,
				2, &otp_ptr->sfr_otp_data.data[i].bottom_r_horizontal);
			ret |= read_reg_otp(client, SFR_BOTTOM_R_VER_REG + 16 * i,
				2, &otp_ptr->sfr_otp_data.data[i].bottom_r_vertical);
		}

		ret |= read_reg_otp(client, SFR_CHECKSUM_REG,
			1, &otp_ptr->sfr_otp_data.checksum);
		if (ret)
			goto err;
	}

	ret = read_reg_otp(client, TOTAL_CHECKSUM_REG,
		1, &otp_ptr->total_checksum);
	if (ret)
		goto err;

	if (otp_ptr->total_checksum) {
		eeprom_dev->otp = otp_ptr;
	} else {
		eeprom_dev->otp = NULL;
		kfree(otp_ptr);
	}

	return 0;
err:
	eeprom_dev->otp = NULL;
	kfree(otp_ptr);
	return -EINVAL;
}

static int otp_read(struct eeprom_device *eeprom_dev)
{
	u8 vendor_flag = 0;
	struct i2c_client *client = eeprom_dev->client;

	vendor_flag = get_vendor_flag(client);
	if (vendor_flag == 0x80)
		otp_read_data(eeprom_dev);
	return 0;
}

static long eeprom_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	struct eeprom_device *eeprom_dev =
		sd_to_eeprom(sd);
	if (!eeprom_dev->otp)
		otp_read(eeprom_dev);
	if (arg && eeprom_dev->otp)
		memcpy(arg, eeprom_dev->otp,
			sizeof(struct otp_info));
	return 0;
}

#ifdef CONFIG_PROC_FS
static int otp_eeprom_show(struct seq_file *p, void *v)
{
	struct eeprom_device *dev = p->private;
	int i = 0;
	int j = 0;

	if (dev) {
		seq_puts(p, "[Header]\n");
		seq_puts(p, "version=1.0;\n\n");

		seq_puts(p, "[RKAWBOTPParam]\n");
		seq_printf(p, "flag=%d;\n", dev->otp->awb_data.flag);
		seq_printf(p, "r_value=%d;\n", dev->otp->awb_data.r_ratio);
		seq_printf(p, "b_value=%d;\n", dev->otp->awb_data.b_ratio);
		seq_printf(p, "gr_value=%d;\n", dev->otp->awb_data.g_ratio);
		seq_puts(p, "gb_value=-1;\n");
		seq_printf(p, "golden_r_value=%d;\n", dev->otp->awb_data.r_golden);
		seq_printf(p, "golden_b_value=%d;\n", dev->otp->awb_data.b_golden);
		seq_printf(p, "golden_gr_value=%d;\n", dev->otp->awb_data.g_golden);
		seq_puts(p, "golden_gb_value=-1;\n\n");

		seq_puts(p, "[RKLSCOTPParam]\n");
		seq_printf(p, "flag=%d;\n", dev->otp->lsc_data.flag);
		seq_printf(p, "width=%d;\n", dev->otp->basic_data.size.width);
		seq_printf(p, "height=%d;\n", dev->otp->basic_data.size.height);
		seq_printf(p, "tablesize=%d;\n\n", dev->otp->lsc_data.table_size);

		seq_puts(p, "lsc_r_table=\n");
		for (i = 0; i < 17; i++) {
			for (j = 0; j < 17; j++) {
				seq_printf(p, "%d", (dev->otp->lsc_data.data[(i * 17 + j) * 2] << 8)
					   | dev->otp->lsc_data.data[(i * 17 + j) * 2 + 1]);
				if (j < 16)
					seq_puts(p, " ");
			}
			if (i < 16)
				seq_puts(p, "\n");
		}
		seq_puts(p, "\n\n");

		seq_puts(p, "lsc_b_table=\n");
		for (i = 0; i < 17; i++) {
			for (j = 0; j < 17; j++) {
				seq_printf(p, "%d", (dev->otp->lsc_data.data[(i * 17 + j) * 2 +
					   1734] << 8) | dev->otp->lsc_data.data[(i * 17 + j) *
					   2 + 1735]);
				if (j < 16)
					seq_puts(p, " ");
			}
			if (i < 16)
				seq_puts(p, "\n");
		}
		seq_puts(p, "\n\n");

		seq_puts(p, "lsc_gr_table=\n");
		for (i = 0; i < 17; i++) {
			for (j = 0; j < 17; j++) {
				seq_printf(p, "%d", (dev->otp->lsc_data.data[(i * 17 + j) * 2 +
					   578] << 8) | dev->otp->lsc_data.data[(i * 17 + j) *
					   2 + 579]);
				if (j < 16)
					seq_puts(p, " ");
			}
			if (i < 16)
				seq_puts(p, "\n");
		}
		seq_puts(p, "\n\n");

		seq_puts(p, "lsc_gb_table=\n");
		for (i = 0; i < 17; i++) {
			for (j = 0; j < 17; j++) {
				seq_printf(p, "%d", (dev->otp->lsc_data.data[(i * 17 + j) * 2 +
					   1156] << 8) | dev->otp->lsc_data.data[(i * 17 + j) *
					   2 + 1157]);
				if (j < 16)
					seq_puts(p, " ");
			}
			if (i < 16)
				seq_puts(p, "\n");
		}
		seq_puts(p, "\n\n");
	}
	return 0;
}

static int eeprom_open(struct inode *inode, struct file *file)
{
	struct eeprom_device *data = PDE_DATA(inode);

	return single_open(file, otp_eeprom_show, data);
}

static const struct proc_ops ops = {
	.proc_open	= eeprom_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int eeprom_proc_init(struct eeprom_device *dev)
{
	dev->procfs = proc_create_data(dev->name, 0, NULL, &ops, dev);
	if (!dev->procfs)
		return -EINVAL;
	return 0;
}

static void eeprom_proc_cleanup(struct eeprom_device *dev)
{
	if (dev->procfs)
		remove_proc_entry(dev->name, NULL);
	dev->procfs = NULL;
}

#endif

static const struct v4l2_subdev_core_ops eeprom_core_ops = {
	.ioctl = eeprom_ioctl,
};

static const struct v4l2_subdev_ops eeprom_ops = {
	.core = &eeprom_core_ops,
};

static void eeprom_subdev_cleanup(struct eeprom_device *dev)
{
	v4l2_device_unregister_subdev(&dev->sd);
	media_entity_cleanup(&dev->sd.entity);
}

static int eeprom_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct eeprom_device *eeprom_dev;

	dev_info(&client->dev, "probing...\n");
	eeprom_dev = devm_kzalloc(&client->dev,
		sizeof(*eeprom_dev),
		GFP_KERNEL);

	if (eeprom_dev == NULL) {
		dev_err(&client->dev, "Probe failed\n");
		return -ENOMEM;
	}
	v4l2_i2c_subdev_init(&eeprom_dev->sd,
		client, &eeprom_ops);
	eeprom_dev->client = client;
	sprintf(eeprom_dev->name, "%s", DEVICE_NAME);
	eeprom_proc_init(eeprom_dev);
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	dev_info(&client->dev, "probing successful\n");

	return 0;
}

static int eeprom_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct eeprom_device *eeprom_dev =
		sd_to_eeprom(sd);
	kfree(eeprom_dev->otp);
	pm_runtime_disable(&client->dev);
	eeprom_subdev_cleanup(eeprom_dev);
	eeprom_proc_cleanup(eeprom_dev);

	return 0;
}

static int __maybe_unused eeprom_suspend(struct device *dev)
{
	return 0;
}

static int  __maybe_unused eeprom_resume(struct device *dev)
{
	return 0;
}

static const struct i2c_device_id eeprom_id_table[] = {
	{ DEVICE_NAME, 0 },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(i2c, eeprom_id_table);

static const struct of_device_id eeprom_of_table[] = {
	{ .compatible = "rk,otp_eeprom" },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(of, eeprom_of_table);

static const struct dev_pm_ops eeprom_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(eeprom_suspend, eeprom_resume)
	SET_RUNTIME_PM_OPS(eeprom_suspend, eeprom_resume, NULL)
};

static struct i2c_driver eeprom_i2c_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.pm = &eeprom_pm_ops,
		.of_match_table = eeprom_of_table,
	},
	.probe = &eeprom_probe,
	.remove = &eeprom_remove,
	.id_table = eeprom_id_table,
};

module_i2c_driver(eeprom_i2c_driver);

MODULE_DESCRIPTION("OTP driver");
MODULE_LICENSE("GPL v2");
