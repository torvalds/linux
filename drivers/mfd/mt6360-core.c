// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 *
 * Author: Gene Chen <gene_chen@richtek.com>
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/version.h>

#include <linux/mfd/mt6360.h>

/* reg 0 -> 0 ~ 7 */
#define MT6360_CHG_TREG_EVT		(4)
#define MT6360_CHG_AICR_EVT		(5)
#define MT6360_CHG_MIVR_EVT		(6)
#define MT6360_PWR_RDY_EVT		(7)
/* REG 1 -> 8 ~ 15 */
#define MT6360_CHG_BATSYSUV_EVT		(9)
#define MT6360_FLED_CHG_VINOVP_EVT	(11)
#define MT6360_CHG_VSYSUV_EVT		(12)
#define MT6360_CHG_VSYSOV_EVT		(13)
#define MT6360_CHG_VBATOV_EVT		(14)
#define MT6360_CHG_VBUSOV_EVT		(15)
/* REG 2 -> 16 ~ 23 */
/* REG 3 -> 24 ~ 31 */
#define MT6360_WD_PMU_DET		(25)
#define MT6360_WD_PMU_DONE		(26)
#define MT6360_CHG_TMRI			(27)
#define MT6360_CHG_ADPBADI		(29)
#define MT6360_CHG_RVPI			(30)
#define MT6360_OTPI			(31)
/* REG 4 -> 32 ~ 39 */
#define MT6360_CHG_AICCMEASL		(32)
#define MT6360_CHGDET_DONEI		(34)
#define MT6360_WDTMRI			(35)
#define MT6360_SSFINISHI		(36)
#define MT6360_CHG_RECHGI		(37)
#define MT6360_CHG_TERMI		(38)
#define MT6360_CHG_IEOCI		(39)
/* REG 5 -> 40 ~ 47 */
#define MT6360_PUMPX_DONEI		(40)
#define MT6360_BAT_OVP_ADC_EVT		(41)
#define MT6360_TYPEC_OTP_EVT		(42)
#define MT6360_ADC_WAKEUP_EVT		(43)
#define MT6360_ADC_DONEI		(44)
#define MT6360_BST_BATUVI		(45)
#define MT6360_BST_VBUSOVI		(46)
#define MT6360_BST_OLPI			(47)
/* REG 6 -> 48 ~ 55 */
#define MT6360_ATTACH_I			(48)
#define MT6360_DETACH_I			(49)
#define MT6360_QC30_STPDONE		(51)
#define MT6360_QC_VBUSDET_DONE		(52)
#define MT6360_HVDCP_DET		(53)
#define MT6360_CHGDETI			(54)
#define MT6360_DCDTI			(55)
/* REG 7 -> 56 ~ 63 */
#define MT6360_FOD_DONE_EVT		(56)
#define MT6360_FOD_OV_EVT		(57)
#define MT6360_CHRDET_UVP_EVT		(58)
#define MT6360_CHRDET_OVP_EVT		(59)
#define MT6360_CHRDET_EXT_EVT		(60)
#define MT6360_FOD_LR_EVT		(61)
#define MT6360_FOD_HR_EVT		(62)
#define MT6360_FOD_DISCHG_FAIL_EVT	(63)
/* REG 8 -> 64 ~ 71 */
#define MT6360_USBID_EVT		(64)
#define MT6360_APWDTRST_EVT		(65)
#define MT6360_EN_EVT			(66)
#define MT6360_QONB_RST_EVT		(67)
#define MT6360_MRSTB_EVT		(68)
#define MT6360_OTP_EVT			(69)
#define MT6360_VDDAOV_EVT		(70)
#define MT6360_SYSUV_EVT		(71)
/* REG 9 -> 72 ~ 79 */
#define MT6360_FLED_STRBPIN_EVT		(72)
#define MT6360_FLED_TORPIN_EVT		(73)
#define MT6360_FLED_TX_EVT		(74)
#define MT6360_FLED_LVF_EVT		(75)
#define MT6360_FLED2_SHORT_EVT		(78)
#define MT6360_FLED1_SHORT_EVT		(79)
/* REG 10 -> 80 ~ 87 */
#define MT6360_FLED2_STRB_EVT		(80)
#define MT6360_FLED1_STRB_EVT		(81)
#define MT6360_FLED2_STRB_TO_EVT	(82)
#define MT6360_FLED1_STRB_TO_EVT	(83)
#define MT6360_FLED2_TOR_EVT		(84)
#define MT6360_FLED1_TOR_EVT		(85)
/* REG 11 -> 88 ~ 95 */
/* REG 12 -> 96 ~ 103 */
#define MT6360_BUCK1_PGB_EVT		(96)
#define MT6360_BUCK1_OC_EVT		(100)
#define MT6360_BUCK1_OV_EVT		(101)
#define MT6360_BUCK1_UV_EVT		(102)
/* REG 13 -> 104 ~ 111 */
#define MT6360_BUCK2_PGB_EVT		(104)
#define MT6360_BUCK2_OC_EVT		(108)
#define MT6360_BUCK2_OV_EVT		(109)
#define MT6360_BUCK2_UV_EVT		(110)
/* REG 14 -> 112 ~ 119 */
#define MT6360_LDO1_OC_EVT		(113)
#define MT6360_LDO2_OC_EVT		(114)
#define MT6360_LDO3_OC_EVT		(115)
#define MT6360_LDO5_OC_EVT		(117)
#define MT6360_LDO6_OC_EVT		(118)
#define MT6360_LDO7_OC_EVT		(119)
/* REG 15 -> 120 ~ 127 */
#define MT6360_LDO1_PGB_EVT		(121)
#define MT6360_LDO2_PGB_EVT		(122)
#define MT6360_LDO3_PGB_EVT		(123)
#define MT6360_LDO5_PGB_EVT		(125)
#define MT6360_LDO6_PGB_EVT		(126)
#define MT6360_LDO7_PGB_EVT		(127)

static const struct regmap_irq mt6360_pmu_irqs[] =  {
	REGMAP_IRQ_REG_LINE(MT6360_CHG_TREG_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_AICR_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_MIVR_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_PWR_RDY_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_BATSYSUV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED_CHG_VINOVP_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_VSYSUV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_VSYSOV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_VBATOV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_VBUSOV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_WD_PMU_DET, 8),
	REGMAP_IRQ_REG_LINE(MT6360_WD_PMU_DONE, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_TMRI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_ADPBADI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_RVPI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_OTPI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_AICCMEASL, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHGDET_DONEI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_WDTMRI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_SSFINISHI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_RECHGI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_TERMI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHG_IEOCI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_PUMPX_DONEI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BAT_OVP_ADC_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_TYPEC_OTP_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_ADC_WAKEUP_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_ADC_DONEI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BST_BATUVI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BST_VBUSOVI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BST_OLPI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_ATTACH_I, 8),
	REGMAP_IRQ_REG_LINE(MT6360_DETACH_I, 8),
	REGMAP_IRQ_REG_LINE(MT6360_QC30_STPDONE, 8),
	REGMAP_IRQ_REG_LINE(MT6360_QC_VBUSDET_DONE, 8),
	REGMAP_IRQ_REG_LINE(MT6360_HVDCP_DET, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHGDETI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_DCDTI, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FOD_DONE_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FOD_OV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHRDET_UVP_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHRDET_OVP_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_CHRDET_EXT_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FOD_LR_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FOD_HR_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FOD_DISCHG_FAIL_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_USBID_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_APWDTRST_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_EN_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_QONB_RST_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_MRSTB_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_OTP_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_VDDAOV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_SYSUV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED_STRBPIN_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED_TORPIN_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED_TX_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED_LVF_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED2_SHORT_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED1_SHORT_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED2_STRB_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED1_STRB_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED2_STRB_TO_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED1_STRB_TO_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED2_TOR_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_FLED1_TOR_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BUCK1_PGB_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BUCK1_OC_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BUCK1_OV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BUCK1_UV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BUCK2_PGB_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BUCK2_OC_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BUCK2_OV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_BUCK2_UV_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO1_OC_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO2_OC_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO3_OC_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO5_OC_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO6_OC_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO7_OC_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO1_PGB_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO2_PGB_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO3_PGB_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO5_PGB_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO6_PGB_EVT, 8),
	REGMAP_IRQ_REG_LINE(MT6360_LDO7_PGB_EVT, 8),
};

static int mt6360_pmu_handle_post_irq(void *irq_drv_data)
{
	struct mt6360_pmu_data *mpd = irq_drv_data;

	return regmap_update_bits(mpd->regmap,
		MT6360_PMU_IRQ_SET, MT6360_IRQ_RETRIG, MT6360_IRQ_RETRIG);
}

static struct regmap_irq_chip mt6360_pmu_irq_chip = {
	.irqs = mt6360_pmu_irqs,
	.num_irqs = ARRAY_SIZE(mt6360_pmu_irqs),
	.num_regs = MT6360_PMU_IRQ_REGNUM,
	.mask_base = MT6360_PMU_CHG_MASK1,
	.status_base = MT6360_PMU_CHG_IRQ1,
	.ack_base = MT6360_PMU_CHG_IRQ1,
	.init_ack_masked = true,
	.use_ack = true,
	.handle_post_irq = mt6360_pmu_handle_post_irq,
};

static const struct regmap_config mt6360_pmu_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MT6360_PMU_MAXREG,
};

static const struct resource mt6360_adc_resources[] = {
	DEFINE_RES_IRQ_NAMED(MT6360_ADC_DONEI, "adc_donei"),
};

static const struct resource mt6360_chg_resources[] = {
	DEFINE_RES_IRQ_NAMED(MT6360_CHG_TREG_EVT, "chg_treg_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_PWR_RDY_EVT, "pwr_rdy_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_CHG_BATSYSUV_EVT, "chg_batsysuv_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_CHG_VSYSUV_EVT, "chg_vsysuv_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_CHG_VSYSOV_EVT, "chg_vsysov_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_CHG_VBATOV_EVT, "chg_vbatov_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_CHG_VBUSOV_EVT, "chg_vbusov_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_CHG_AICCMEASL, "chg_aiccmeasl"),
	DEFINE_RES_IRQ_NAMED(MT6360_WDTMRI, "wdtmri"),
	DEFINE_RES_IRQ_NAMED(MT6360_CHG_RECHGI, "chg_rechgi"),
	DEFINE_RES_IRQ_NAMED(MT6360_CHG_TERMI, "chg_termi"),
	DEFINE_RES_IRQ_NAMED(MT6360_CHG_IEOCI, "chg_ieoci"),
	DEFINE_RES_IRQ_NAMED(MT6360_PUMPX_DONEI, "pumpx_donei"),
	DEFINE_RES_IRQ_NAMED(MT6360_ATTACH_I, "attach_i"),
	DEFINE_RES_IRQ_NAMED(MT6360_CHRDET_EXT_EVT, "chrdet_ext_evt"),
};

static const struct resource mt6360_led_resources[] = {
	DEFINE_RES_IRQ_NAMED(MT6360_FLED_CHG_VINOVP_EVT, "fled_chg_vinovp_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_FLED_LVF_EVT, "fled_lvf_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_FLED2_SHORT_EVT, "fled2_short_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_FLED1_SHORT_EVT, "fled1_short_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_FLED2_STRB_TO_EVT, "fled2_strb_to_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_FLED1_STRB_TO_EVT, "fled1_strb_to_evt"),
};

static const struct resource mt6360_pmic_resources[] = {
	DEFINE_RES_IRQ_NAMED(MT6360_BUCK1_PGB_EVT, "buck1_pgb_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_BUCK1_OC_EVT, "buck1_oc_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_BUCK1_OV_EVT, "buck1_ov_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_BUCK1_UV_EVT, "buck1_uv_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_BUCK2_PGB_EVT, "buck2_pgb_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_BUCK2_OC_EVT, "buck2_oc_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_BUCK2_OV_EVT, "buck2_ov_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_BUCK2_UV_EVT, "buck2_uv_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_LDO6_OC_EVT, "ldo6_oc_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_LDO7_OC_EVT, "ldo7_oc_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_LDO6_PGB_EVT, "ldo6_pgb_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_LDO7_PGB_EVT, "ldo7_pgb_evt"),
};

static const struct resource mt6360_ldo_resources[] = {
	DEFINE_RES_IRQ_NAMED(MT6360_LDO1_OC_EVT, "ldo1_oc_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_LDO2_OC_EVT, "ldo2_oc_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_LDO3_OC_EVT, "ldo3_oc_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_LDO5_OC_EVT, "ldo5_oc_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_LDO1_PGB_EVT, "ldo1_pgb_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_LDO2_PGB_EVT, "ldo2_pgb_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_LDO3_PGB_EVT, "ldo3_pgb_evt"),
	DEFINE_RES_IRQ_NAMED(MT6360_LDO5_PGB_EVT, "ldo5_pgb_evt"),
};

static const struct mfd_cell mt6360_devs[] = {
	OF_MFD_CELL("mt6360_adc", mt6360_adc_resources,
		    NULL, 0, 0, "mediatek,mt6360_adc"),
	OF_MFD_CELL("mt6360_chg", mt6360_chg_resources,
		    NULL, 0, 0, "mediatek,mt6360_chg"),
	OF_MFD_CELL("mt6360_led", mt6360_led_resources,
		    NULL, 0, 0, "mediatek,mt6360_led"),
	OF_MFD_CELL("mt6360_pmic", mt6360_pmic_resources,
		    NULL, 0, 0, "mediatek,mt6360_pmic"),
	OF_MFD_CELL("mt6360_ldo", mt6360_ldo_resources,
		    NULL, 0, 0, "mediatek,mt6360_ldo"),
	OF_MFD_CELL("mt6360_tcpc", NULL,
		    NULL, 0, 0, "mediatek,mt6360_tcpc"),
};

static const unsigned short mt6360_slave_addr[MT6360_SLAVE_MAX] = {
	MT6360_PMU_SLAVEID,
	MT6360_PMIC_SLAVEID,
	MT6360_LDO_SLAVEID,
	MT6360_TCPC_SLAVEID,
};

static int mt6360_pmu_probe(struct i2c_client *client)
{
	struct mt6360_pmu_data *mpd;
	unsigned int reg_data;
	int i, ret;

	mpd = devm_kzalloc(&client->dev, sizeof(*mpd), GFP_KERNEL);
	if (!mpd)
		return -ENOMEM;

	mpd->dev = &client->dev;
	i2c_set_clientdata(client, mpd);

	mpd->regmap = devm_regmap_init_i2c(client, &mt6360_pmu_regmap_config);
	if (IS_ERR(mpd->regmap)) {
		dev_err(&client->dev, "Failed to register regmap\n");
		return PTR_ERR(mpd->regmap);
	}

	ret = regmap_read(mpd->regmap, MT6360_PMU_DEV_INFO, &reg_data);
	if (ret) {
		dev_err(&client->dev, "Device not found\n");
		return ret;
	}

	mpd->chip_rev = reg_data & CHIP_REV_MASK;
	if (mpd->chip_rev != CHIP_VEN_MT6360) {
		dev_err(&client->dev, "Device not supported\n");
		return -ENODEV;
	}

	mt6360_pmu_irq_chip.irq_drv_data = mpd;
	ret = devm_regmap_add_irq_chip(&client->dev, mpd->regmap, client->irq,
				       IRQF_TRIGGER_FALLING, 0,
				       &mt6360_pmu_irq_chip, &mpd->irq_data);
	if (ret) {
		dev_err(&client->dev, "Failed to add Regmap IRQ Chip\n");
		return ret;
	}

	mpd->i2c[0] = client;
	for (i = 1; i < MT6360_SLAVE_MAX; i++) {
		mpd->i2c[i] = devm_i2c_new_dummy_device(&client->dev,
							client->adapter,
							mt6360_slave_addr[i]);
		if (IS_ERR(mpd->i2c[i])) {
			dev_err(&client->dev,
				"Failed to get new dummy I2C device for address 0x%x",
				mt6360_slave_addr[i]);
			return PTR_ERR(mpd->i2c[i]);
		}
		i2c_set_clientdata(mpd->i2c[i], mpd);
	}

	ret = devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_AUTO,
				   mt6360_devs, ARRAY_SIZE(mt6360_devs), NULL,
				   0, regmap_irq_get_domain(mpd->irq_data));
	if (ret) {
		dev_err(&client->dev,
			"Failed to register subordinate devices\n");
		return ret;
	}

	return 0;
}

static int __maybe_unused mt6360_pmu_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(i2c->irq);

	return 0;
}

static int __maybe_unused mt6360_pmu_resume(struct device *dev)
{

	struct i2c_client *i2c = to_i2c_client(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(i2c->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(mt6360_pmu_pm_ops,
			 mt6360_pmu_suspend, mt6360_pmu_resume);

static const struct of_device_id __maybe_unused mt6360_pmu_of_id[] = {
	{ .compatible = "mediatek,mt6360_pmu", },
	{},
};
MODULE_DEVICE_TABLE(of, mt6360_pmu_of_id);

static struct i2c_driver mt6360_pmu_driver = {
	.driver = {
		.pm = &mt6360_pmu_pm_ops,
		.of_match_table = of_match_ptr(mt6360_pmu_of_id),
	},
	.probe_new = mt6360_pmu_probe,
};
module_i2c_driver(mt6360_pmu_driver);

MODULE_AUTHOR("Gene Chen <gene_chen@richtek.com>");
MODULE_DESCRIPTION("MT6360 PMU I2C Driver");
MODULE_LICENSE("GPL v2");
