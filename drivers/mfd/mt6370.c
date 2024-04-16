// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Richtek Technology Corp.
 *
 * Author: ChiYuan Huang <cy_huang@richtek.com>
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "mt6370.h"

#define MT6370_REG_DEV_INFO	0x100
#define MT6370_REG_CHG_IRQ1	0x1C0
#define MT6370_REG_CHG_MASK1	0x1E0
#define MT6370_REG_MAXADDR	0x1FF

#define MT6370_VENID_MASK	GENMASK(7, 4)

#define MT6370_NUM_IRQREGS	16
#define MT6370_USBC_I2CADDR	0x4E
#define MT6370_MAX_ADDRLEN	2

#define MT6370_VENID_RT5081	0x8
#define MT6370_VENID_RT5081A	0xA
#define MT6370_VENID_MT6370	0xE
#define MT6370_VENID_MT6371	0xF
#define MT6370_VENID_MT6372P	0x9
#define MT6370_VENID_MT6372CP	0xB

static const struct regmap_irq mt6370_irqs[] = {
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DIRCHGON, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_TREG, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_AICR, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_MIVR, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_PWR_RDY, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FL_CHG_VINOVP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_VSYSUV, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_VSYSOV, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_VBATOV, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_VINOVPCHG, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_TS_BAT_COLD, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_TS_BAT_COOL, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_TS_BAT_WARM, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_TS_BAT_HOT, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_TS_STATC, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_FAULT, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_STATC, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_TMR, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_BATABS, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_ADPBAD, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_RVP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_TSHUTDOWN, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_IINMEAS, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_ICCMEAS, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHGDET_DONE, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_WDTMR, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_SSFINISH, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_RECHG, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_TERM, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHG_IEOC, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_ADC_DONE, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_PUMPX_DONE, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_BST_BATUV, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_BST_MIDOV, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_BST_OLP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_ATTACH, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DETACH, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_HVDCP_STPDONE, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_HVDCP_VBUSDET_DONE, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_HVDCP_DET, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_CHGDET, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DCDT, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DIRCHG_VGOK, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DIRCHG_WDTMR, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DIRCHG_UC, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DIRCHG_OC, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DIRCHG_OV, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_OVPCTRL_SWON, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_OVPCTRL_UVP_D, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_OVPCTRL_UVP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_OVPCTRL_OVP_D, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_OVPCTRL_OVP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FLED_STRBPIN, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FLED_TORPIN, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FLED_TX, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FLED_LVF, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FLED2_SHORT, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FLED1_SHORT, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FLED2_STRB, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FLED1_STRB, 8),
	REGMAP_IRQ_REG_LINE(mT6370_IRQ_FLED2_STRB_TO, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FLED1_STRB_TO, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FLED2_TOR, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_FLED1_TOR, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_OTP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_VDDA_OVP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_VDDA_UV, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_LDO_OC, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_BLED_OCP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_BLED_OVP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DSV_VNEG_OCP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DSV_VPOS_OCP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DSV_BST_OCP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DSV_VNEG_SCP, 8),
	REGMAP_IRQ_REG_LINE(MT6370_IRQ_DSV_VPOS_SCP, 8),
};

static const struct regmap_irq_chip mt6370_irq_chip = {
	.name		= "mt6370-irqs",
	.status_base	= MT6370_REG_CHG_IRQ1,
	.mask_base	= MT6370_REG_CHG_MASK1,
	.num_regs	= MT6370_NUM_IRQREGS,
	.irqs		= mt6370_irqs,
	.num_irqs	= ARRAY_SIZE(mt6370_irqs),
};

static const struct resource mt6370_regulator_irqs[] = {
	DEFINE_RES_IRQ_NAMED(MT6370_IRQ_DSV_VPOS_SCP, "db_vpos_scp"),
	DEFINE_RES_IRQ_NAMED(MT6370_IRQ_DSV_VNEG_SCP, "db_vneg_scp"),
	DEFINE_RES_IRQ_NAMED(MT6370_IRQ_DSV_BST_OCP, "db_vbst_ocp"),
	DEFINE_RES_IRQ_NAMED(MT6370_IRQ_DSV_VPOS_OCP, "db_vpos_ocp"),
	DEFINE_RES_IRQ_NAMED(MT6370_IRQ_DSV_VNEG_OCP, "db_vneg_ocp"),
	DEFINE_RES_IRQ_NAMED(MT6370_IRQ_LDO_OC, "ldo_oc"),
};

static const struct mfd_cell mt6370_devices[] = {
	MFD_CELL_OF("mt6370-adc",
		    NULL, NULL, 0, 0, "mediatek,mt6370-adc"),
	MFD_CELL_OF("mt6370-charger",
		    NULL, NULL, 0, 0, "mediatek,mt6370-charger"),
	MFD_CELL_OF("mt6370-flashlight",
		    NULL, NULL, 0, 0, "mediatek,mt6370-flashlight"),
	MFD_CELL_OF("mt6370-indicator",
		    NULL, NULL, 0, 0, "mediatek,mt6370-indicator"),
	MFD_CELL_OF("mt6370-tcpc",
		    NULL, NULL, 0, 0, "mediatek,mt6370-tcpc"),
	MFD_CELL_RES("mt6370-regulator", mt6370_regulator_irqs),
};

static const struct mfd_cell mt6370_exclusive_devices[] = {
	MFD_CELL_OF("mt6370-backlight",
		    NULL, NULL, 0, 0, "mediatek,mt6370-backlight"),
};

static const struct mfd_cell mt6372_exclusive_devices[] = {
	MFD_CELL_OF("mt6370-backlight",
		    NULL, NULL, 0, 0, "mediatek,mt6372-backlight"),
};

static int mt6370_check_vendor_info(struct device *dev, struct regmap *rmap,
				    int *vid)
{
	unsigned int devinfo;
	int ret;

	ret = regmap_read(rmap, MT6370_REG_DEV_INFO, &devinfo);
	if (ret)
		return ret;

	*vid = FIELD_GET(MT6370_VENID_MASK, devinfo);
	switch (*vid) {
	case MT6370_VENID_RT5081:
	case MT6370_VENID_RT5081A:
	case MT6370_VENID_MT6370:
	case MT6370_VENID_MT6371:
	case MT6370_VENID_MT6372P:
	case MT6370_VENID_MT6372CP:
		return 0;
	default:
		dev_err(dev, "Unknown Vendor ID 0x%02x\n", devinfo);
		return -ENODEV;
	}
}

static int mt6370_regmap_read(void *context, const void *reg_buf,
			      size_t reg_size, void *val_buf, size_t val_size)
{
	struct mt6370_info *info = context;
	const u8 *u8_buf = reg_buf;
	u8 bank_idx, bank_addr;
	int ret;

	bank_idx = u8_buf[0];
	bank_addr = u8_buf[1];

	ret = i2c_smbus_read_i2c_block_data(info->i2c[bank_idx], bank_addr,
					    val_size, val_buf);
	if (ret < 0)
		return ret;

	if (ret != val_size)
		return -EIO;

	return 0;
}

static int mt6370_regmap_write(void *context, const void *data, size_t count)
{
	struct mt6370_info *info = context;
	const u8 *u8_buf = data;
	u8 bank_idx, bank_addr;
	int len = count - MT6370_MAX_ADDRLEN;

	bank_idx = u8_buf[0];
	bank_addr = u8_buf[1];

	return i2c_smbus_write_i2c_block_data(info->i2c[bank_idx], bank_addr,
					      len, data + MT6370_MAX_ADDRLEN);
}

static const struct regmap_bus mt6370_regmap_bus = {
	.read		= mt6370_regmap_read,
	.write		= mt6370_regmap_write,
};

static const struct regmap_config mt6370_regmap_config = {
	.reg_bits		= 16,
	.val_bits		= 8,
	.reg_format_endian	= REGMAP_ENDIAN_BIG,
	.max_register		= MT6370_REG_MAXADDR,
};

static int mt6370_probe(struct i2c_client *i2c)
{
	struct mt6370_info *info;
	struct i2c_client *usbc_i2c;
	struct regmap *regmap;
	struct device *dev = &i2c->dev;
	int ret, vid;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	usbc_i2c = devm_i2c_new_dummy_device(dev, i2c->adapter,
					     MT6370_USBC_I2CADDR);
	if (IS_ERR(usbc_i2c))
		return dev_err_probe(dev, PTR_ERR(usbc_i2c),
				     "Failed to register USBC I2C client\n");

	/* Assign I2C client for PMU and TypeC */
	info->i2c[MT6370_PMU_I2C] = i2c;
	info->i2c[MT6370_USBC_I2C] = usbc_i2c;

	regmap = devm_regmap_init(dev, &mt6370_regmap_bus,
				  info, &mt6370_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to init regmap\n");

	ret = mt6370_check_vendor_info(dev, regmap, &vid);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to check vendor info\n");

	ret = devm_regmap_add_irq_chip(dev, regmap, i2c->irq,
				       IRQF_ONESHOT, -1, &mt6370_irq_chip,
				       &info->irq_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add irq chip\n");

	switch (vid) {
	case MT6370_VENID_MT6372P:
	case MT6370_VENID_MT6372CP:
		ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO,
					   mt6372_exclusive_devices,
					   ARRAY_SIZE(mt6372_exclusive_devices),
					   NULL, 0,
					   regmap_irq_get_domain(info->irq_data));
		break;
	default:
		ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO,
					   mt6370_exclusive_devices,
					   ARRAY_SIZE(mt6370_exclusive_devices),
					   NULL, 0,
					   regmap_irq_get_domain(info->irq_data));
		break;
	}

	if (ret)
		return dev_err_probe(dev, ret, "Failed to add the exclusive devices\n");

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO,
				    mt6370_devices, ARRAY_SIZE(mt6370_devices),
				    NULL, 0,
				    regmap_irq_get_domain(info->irq_data));
}

static const struct of_device_id mt6370_match_table[] = {
	{ .compatible = "mediatek,mt6370" },
	{}
};
MODULE_DEVICE_TABLE(of, mt6370_match_table);

static struct i2c_driver mt6370_driver = {
	.driver = {
		.name = "mt6370",
		.of_match_table = mt6370_match_table,
	},
	.probe_new = mt6370_probe,
};
module_i2c_driver(mt6370_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("MediaTek MT6370 SubPMIC Driver");
MODULE_LICENSE("GPL v2");
