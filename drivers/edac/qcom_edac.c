// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/soc/qcom/llcc-qcom.h>

#include "edac_mc.h"
#include "edac_device.h"

#define EDAC_LLCC                       "qcom_llcc"

#define LLCC_ERP_PANIC_ON_CE            1

#ifdef CONFIG_EDAC_QCOM_LLCC_PANIC_ON_UE
#define LLCC_ERP_PANIC_ON_UE            1
#else
#define LLCC_ERP_PANIC_ON_UE            0
#endif

#define TRP_SYN_REG_CNT                 6
#define DRP_SYN_REG_CNT                 8

#define LLCC_COMMON_STATUS0_V2          0x0003000c
#define LLCC_COMMON_STATUS0_V21         0x0003400c
#define LLCC_COMMON_STATUS0             edac_regs[LLCC_COMMON_STATUS0_num]
#define LLCC_LB_CNT_MASK                GENMASK(31, 28)
#define LLCC_LB_CNT_SHIFT               28

/* Single & double bit syndrome register offsets */
#define TRP_ECC_SB_ERR_SYN0             0x0002034c
#define TRP_ECC_DB_ERR_SYN0             0x00020370
#define DRP_ECC_SB_ERR_SYN0_V2          0x0004204c
#define DRP_ECC_SB_ERR_SYN0_V21         0x0005204c
#define DRP_ECC_SB_ERR_SYN0             edac_regs[DRP_ECC_SB_ERR_SYN0_num]
#define DRP_ECC_DB_ERR_SYN0_V2          0x00042070
#define DRP_ECC_DB_ERR_SYN0_V21         0x00052070
#define DRP_ECC_DB_ERR_SYN0             edac_regs[DRP_ECC_DB_ERR_SYN0_num]

/* Error register offsets */
#define TRP_ECC_ERROR_STATUS1           0x00020348
#define TRP_ECC_ERROR_STATUS0           0x00020344
#define DRP_ECC_ERROR_STATUS1_V2        0x00042048
#define DRP_ECC_ERROR_STATUS1_V21       0x00052048
#define DRP_ECC_ERROR_STATUS1           edac_regs[DRP_ECC_ERROR_STATUS1_num]
#define DRP_ECC_ERROR_STATUS0_V2        0x00042044
#define DRP_ECC_ERROR_STATUS0_V21       0x00052044
#define DRP_ECC_ERROR_STATUS0           edac_regs[DRP_ECC_ERROR_STATUS0_num]

/* TRP, DRP interrupt register offsets */
#define DRP_INTERRUPT_STATUS_V2         0x00041000
#define DRP_INTERRUPT_STATUS_V21        0x00050020
#define DRP_INTERRUPT_STATUS            edac_regs[DRP_INTERRUPT_STATUS_num]
#define TRP_INTERRUPT_0_STATUS          0x00020480
#define DRP_INTERRUPT_CLEAR_V2          0x00041008
#define DRP_INTERRUPT_CLEAR_V21         0x00050028
#define DRP_INTERRUPT_CLEAR             edac_regs[DRP_INTERRUPT_CLEAR_num]
#define DRP_ECC_ERROR_CNTR_CLEAR_V2     0x00040004
#define DRP_ECC_ERROR_CNTR_CLEAR_V21    0x00050004
#define DRP_ECC_ERROR_CNTR_CLEAR        edac_regs[DRP_ECC_ERROR_CNTR_CLEAR_num]
#define TRP_INTERRUPT_0_CLEAR           0x00020484
#define TRP_ECC_ERROR_CNTR_CLEAR        0x00020440

/* Mask and shift macros */
#define ECC_DB_ERR_COUNT_MASK           GENMASK(4, 0)
#define ECC_DB_ERR_WAYS_MASK            GENMASK(31, 16)
#define ECC_DB_ERR_WAYS_SHIFT           BIT(4)

#define ECC_SB_ERR_COUNT_MASK           GENMASK(23, 16)
#define ECC_SB_ERR_COUNT_SHIFT          BIT(4)
#define ECC_SB_ERR_WAYS_MASK            GENMASK(15, 0)

#define SB_ECC_ERROR                    BIT(0)
#define DB_ECC_ERROR                    BIT(1)

#define DRP_TRP_INT_CLEAR               GENMASK(1, 0)
#define DRP_TRP_CNT_CLEAR               GENMASK(1, 0)

/* Config registers offsets*/
#define DRP_ECC_ERROR_CFG_V2            0x00040000
#define DRP_ECC_ERROR_CFG_V21           0x00050000
#define DRP_ECC_ERROR_CFG               edac_regs[DRP_ECC_ERROR_CFG_num]

/* Tag RAM, Data RAM interrupt register offsets */
#define CMN_INTERRUPT_0_ENABLE_V2       0x0003001c
#define CMN_INTERRUPT_0_ENABLE_V21      0x0003401c
#define CMN_INTERRUPT_0_ENABLE          edac_regs[CMN_INTERRUPT_0_ENABLE_num]
#define CMN_INTERRUPT_2_ENABLE_V2       0x0003003c
#define CMN_INTERRUPT_2_ENABLE_V21      0x0003403c
#define CMN_INTERRUPT_2_ENABLE          edac_regs[CMN_INTERRUPT_2_ENABLE_num]
#define TRP_INTERRUPT_0_ENABLE          0x00020488
#define DRP_INTERRUPT_ENABLE_V2         0x0004100c
#define DRP_INTERRUPT_ENABLE_V21        0x0005002C
#define DRP_INTERRUPT_ENABLE            edac_regs[DRP_INTERRUPT_ENABLE_num]

#define SB_ERROR_THRESHOLD              0x1
#define SB_ERROR_THRESHOLD_SHIFT        24
#define SB_DB_TRP_INTERRUPT_ENABLE      0x3
#define TRP0_INTERRUPT_ENABLE           0x1
#define DRP0_INTERRUPT_ENABLE           BIT(6)
#define SB_DB_DRP_INTERRUPT_ENABLE      0x3

enum {
	LLCC_DRAM_CE = 0,
	LLCC_DRAM_UE,
	LLCC_TRAM_CE,
	LLCC_TRAM_UE,
};

enum {
	LLCC_COMMON_STATUS0_num = 0,
	DRP_ECC_SB_ERR_SYN0_num,
	DRP_ECC_DB_ERR_SYN0_num,
	DRP_ECC_ERROR_STATUS1_num,
	DRP_ECC_ERROR_STATUS0_num,
	DRP_INTERRUPT_STATUS_num,
	DRP_INTERRUPT_CLEAR_num,
	DRP_ECC_ERROR_CNTR_CLEAR_num,
	DRP_ECC_ERROR_CFG_num,
	CMN_INTERRUPT_0_ENABLE_num,
	CMN_INTERRUPT_2_ENABLE_num,
	DRP_INTERRUPT_ENABLE_num,
	EDAC_REGS_MAX,
};

static int poll_msec = 5000;
module_param(poll_msec, int, 0444);

static unsigned int edac_regs_v2[EDAC_REGS_MAX] = {
	LLCC_COMMON_STATUS0_V2,
	DRP_ECC_SB_ERR_SYN0_V2,
	DRP_ECC_DB_ERR_SYN0_V2,
	DRP_ECC_ERROR_STATUS1_V2,
	DRP_ECC_ERROR_STATUS0_V2,
	DRP_INTERRUPT_STATUS_V2,
	DRP_INTERRUPT_CLEAR_V2,
	DRP_ECC_ERROR_CNTR_CLEAR_V2,
	DRP_ECC_ERROR_CFG_V2,
	CMN_INTERRUPT_0_ENABLE_V2,
	CMN_INTERRUPT_2_ENABLE_V2,
	DRP_INTERRUPT_ENABLE_V2,
};

static unsigned int edac_regs_v21[EDAC_REGS_MAX] = {
	LLCC_COMMON_STATUS0_V21,
	DRP_ECC_SB_ERR_SYN0_V21,
	DRP_ECC_DB_ERR_SYN0_V21,
	DRP_ECC_ERROR_STATUS1_V21,
	DRP_ECC_ERROR_STATUS0_V21,
	DRP_INTERRUPT_STATUS_V21,
	DRP_INTERRUPT_CLEAR_V21,
	DRP_ECC_ERROR_CNTR_CLEAR_V21,
	DRP_ECC_ERROR_CFG_V21,
	CMN_INTERRUPT_0_ENABLE_V21,
	CMN_INTERRUPT_2_ENABLE_V21,
	DRP_INTERRUPT_ENABLE_V21,
};

static unsigned int *edac_regs = edac_regs_v2;

static struct llcc_edac_reg_data edac_reg_data_v2[] = {
	[LLCC_DRAM_CE] = {
		.name = "DRAM Single-bit",
		.synd_reg = DRP_ECC_SB_ERR_SYN0_V2,
		.count_status_reg = DRP_ECC_ERROR_STATUS1_V2,
		.ways_status_reg = DRP_ECC_ERROR_STATUS0_V2,
		.reg_cnt = DRP_SYN_REG_CNT,
		.count_mask = ECC_SB_ERR_COUNT_MASK,
		.ways_mask = ECC_SB_ERR_WAYS_MASK,
		.count_shift = ECC_SB_ERR_COUNT_SHIFT,
	},
	[LLCC_DRAM_UE] = {
		.name = "DRAM Double-bit",
		.synd_reg = DRP_ECC_DB_ERR_SYN0_V2,
		.count_status_reg = DRP_ECC_ERROR_STATUS1_V2,
		.ways_status_reg = DRP_ECC_ERROR_STATUS0_V2,
		.reg_cnt = DRP_SYN_REG_CNT,
		.count_mask = ECC_DB_ERR_COUNT_MASK,
		.ways_mask = ECC_DB_ERR_WAYS_MASK,
		.ways_shift = ECC_DB_ERR_WAYS_SHIFT,
	},
	[LLCC_TRAM_CE] = {
		.name = "TRAM Single-bit",
		.synd_reg = TRP_ECC_SB_ERR_SYN0,
		.count_status_reg = TRP_ECC_ERROR_STATUS1,
		.ways_status_reg = TRP_ECC_ERROR_STATUS0,
		.reg_cnt = TRP_SYN_REG_CNT,
		.count_mask = ECC_SB_ERR_COUNT_MASK,
		.ways_mask = ECC_SB_ERR_WAYS_MASK,
		.count_shift = ECC_SB_ERR_COUNT_SHIFT,
	},
	[LLCC_TRAM_UE] = {
		.name = "TRAM Double-bit",
		.synd_reg = TRP_ECC_DB_ERR_SYN0,
		.count_status_reg = TRP_ECC_ERROR_STATUS1,
		.ways_status_reg = TRP_ECC_ERROR_STATUS0,
		.reg_cnt = TRP_SYN_REG_CNT,
		.count_mask = ECC_DB_ERR_COUNT_MASK,
		.ways_mask = ECC_DB_ERR_WAYS_MASK,
		.ways_shift = ECC_DB_ERR_WAYS_SHIFT,
	},
};

static struct llcc_edac_reg_data edac_reg_data_v21[] = {
	[LLCC_DRAM_CE] = {
		.name = "DRAM Single-bit",
		.synd_reg = DRP_ECC_SB_ERR_SYN0_V21,
		.count_status_reg = DRP_ECC_ERROR_STATUS1_V21,
		.ways_status_reg = DRP_ECC_ERROR_STATUS0_V21,
		.reg_cnt = DRP_SYN_REG_CNT,
		.count_mask = ECC_SB_ERR_COUNT_MASK,
		.ways_mask = ECC_SB_ERR_WAYS_MASK,
		.count_shift = ECC_SB_ERR_COUNT_SHIFT,
	},
	[LLCC_DRAM_UE] = {
		.name = "DRAM Double-bit",
		.synd_reg = DRP_ECC_DB_ERR_SYN0_V21,
		.count_status_reg = DRP_ECC_ERROR_STATUS1_V21,
		.ways_status_reg = DRP_ECC_ERROR_STATUS0_V21,
		.reg_cnt = DRP_SYN_REG_CNT,
		.count_mask = ECC_DB_ERR_COUNT_MASK,
		.ways_mask = ECC_DB_ERR_WAYS_MASK,
		.ways_shift = ECC_DB_ERR_WAYS_SHIFT,
	},
	[LLCC_TRAM_CE] = {
		.name = "TRAM Single-bit",
		.synd_reg = TRP_ECC_SB_ERR_SYN0,
		.count_status_reg = TRP_ECC_ERROR_STATUS1,
		.ways_status_reg = TRP_ECC_ERROR_STATUS0,
		.reg_cnt = TRP_SYN_REG_CNT,
		.count_mask = ECC_SB_ERR_COUNT_MASK,
		.ways_mask = ECC_SB_ERR_WAYS_MASK,
		.count_shift = ECC_SB_ERR_COUNT_SHIFT,
	},
	[LLCC_TRAM_UE] = {
		.name = "TRAM Double-bit",
		.synd_reg = TRP_ECC_DB_ERR_SYN0,
		.count_status_reg = TRP_ECC_ERROR_STATUS1,
		.ways_status_reg = TRP_ECC_ERROR_STATUS0,
		.reg_cnt = TRP_SYN_REG_CNT,
		.count_mask = ECC_DB_ERR_COUNT_MASK,
		.ways_mask = ECC_DB_ERR_WAYS_MASK,
		.ways_shift = ECC_DB_ERR_WAYS_SHIFT,
	},
};

static struct llcc_edac_reg_data *edac_reg_data = edac_reg_data_v2;

static int qcom_llcc_core_setup(struct regmap *llcc_bcast_regmap)
{
	u32 sb_err_threshold;
	int ret;

	/*
	 * Configure interrupt enable registers such that Tag, Data RAM related
	 * interrupts are propagated to interrupt controller for servicing
	 */
	ret = regmap_update_bits(llcc_bcast_regmap, CMN_INTERRUPT_0_ENABLE,
				 TRP0_INTERRUPT_ENABLE,
				 TRP0_INTERRUPT_ENABLE);
	if (ret)
		return ret;

	ret = regmap_update_bits(llcc_bcast_regmap, TRP_INTERRUPT_0_ENABLE,
				 SB_DB_TRP_INTERRUPT_ENABLE,
				 SB_DB_TRP_INTERRUPT_ENABLE);
	if (ret)
		return ret;

	sb_err_threshold = (SB_ERROR_THRESHOLD << SB_ERROR_THRESHOLD_SHIFT);
	ret = regmap_write(llcc_bcast_regmap, DRP_ECC_ERROR_CFG,
			   sb_err_threshold);
	if (ret)
		return ret;

	ret = regmap_update_bits(llcc_bcast_regmap, CMN_INTERRUPT_0_ENABLE,
				 DRP0_INTERRUPT_ENABLE,
				 DRP0_INTERRUPT_ENABLE);
	if (ret)
		return ret;

	ret = regmap_write(llcc_bcast_regmap, DRP_INTERRUPT_ENABLE,
			   SB_DB_DRP_INTERRUPT_ENABLE);
	return ret;
}

/* Clear the error interrupt and counter registers */
static int
qcom_llcc_clear_error_status(int err_type, struct llcc_drv_data *drv)
{
	int ret = 0;

	switch (err_type) {
	case LLCC_DRAM_CE:
	case LLCC_DRAM_UE:
		ret = regmap_write(drv->bcast_regmap, DRP_INTERRUPT_CLEAR,
				   DRP_TRP_INT_CLEAR);
		if (ret)
			return ret;

		ret = regmap_write(drv->bcast_regmap, DRP_ECC_ERROR_CNTR_CLEAR,
				   DRP_TRP_CNT_CLEAR);
		if (ret)
			return ret;
		break;
	case LLCC_TRAM_CE:
	case LLCC_TRAM_UE:
		ret = regmap_write(drv->bcast_regmap, TRP_INTERRUPT_0_CLEAR,
				   DRP_TRP_INT_CLEAR);
		if (ret)
			return ret;

		ret = regmap_write(drv->bcast_regmap, TRP_ECC_ERROR_CNTR_CLEAR,
				   DRP_TRP_CNT_CLEAR);
		if (ret)
			return ret;
		break;
	default:
		ret = -EINVAL;
		edac_printk(KERN_CRIT, EDAC_LLCC, "Unexpected error type: %d\n",
			    err_type);
	}
	return ret;
}

/* Dump Syndrome registers data for Tag RAM, Data RAM bit errors*/
static int
dump_syn_reg_values(struct llcc_drv_data *drv, u32 bank, int err_type)
{
	struct llcc_edac_reg_data reg_data = edac_reg_data[err_type];
	int err_cnt, err_ways, ret, i;
	u32 synd_reg, synd_val;

	for (i = 0; i < reg_data.reg_cnt; i++) {
		synd_reg = reg_data.synd_reg + (i * 4);
		ret = regmap_read(drv->regmap, drv->offsets[bank] + synd_reg,
				  &synd_val);
		if (ret)
			goto clear;

		edac_printk(KERN_CRIT, EDAC_LLCC, "%s: ECC_SYN%d: 0x%8x\n",
			    reg_data.name, i, synd_val);
	}

	ret = regmap_read(drv->regmap,
			  drv->offsets[bank] + reg_data.count_status_reg,
			  &err_cnt);
	if (ret)
		goto clear;

	err_cnt &= reg_data.count_mask;
	err_cnt >>= reg_data.count_shift;
	edac_printk(KERN_CRIT, EDAC_LLCC, "%s: Error count: 0x%4x\n",
		    reg_data.name, err_cnt);

	ret = regmap_read(drv->regmap,
			  drv->offsets[bank] + reg_data.ways_status_reg,
			  &err_ways);
	if (ret)
		goto clear;

	err_ways &= reg_data.ways_mask;
	err_ways >>= reg_data.ways_shift;

	edac_printk(KERN_CRIT, EDAC_LLCC, "%s: Error ways: 0x%4x\n",
		    reg_data.name, err_ways);

clear:
	return qcom_llcc_clear_error_status(err_type, drv);
}


static void llcc_edac_handle_ce(struct edac_device_ctl_info *edac_dev,
				int inst_nr, int block_nr, const char *msg)
{
	edac_device_handle_ce(edac_dev, inst_nr, block_nr, msg);
#ifdef CONFIG_EDAC_QCOM_LLCC_PANIC_ON_CE
	panic("EDAC %s CE: %s\n", edac_dev->ctl_name, msg);
#endif
}

static int
dump_syn_reg(struct edac_device_ctl_info *edev_ctl, int err_type, u32 bank)
{
	struct llcc_drv_data *drv = edev_ctl->pvt_info;
	int ret;

	ret = dump_syn_reg_values(drv, bank, err_type);
	if (ret)
		return ret;

	switch (err_type) {
	case LLCC_DRAM_CE:
		llcc_edac_handle_ce(edev_ctl, 0, bank,
				      "LLCC Data RAM correctable Error");
		break;
	case LLCC_DRAM_UE:
		edac_device_handle_ue(edev_ctl, 0, bank,
				      "LLCC Data RAM uncorrectable Error");
		break;
	case LLCC_TRAM_CE:
		llcc_edac_handle_ce(edev_ctl, 0, bank,
				      "LLCC Tag RAM correctable Error");
		break;
	case LLCC_TRAM_UE:
		edac_device_handle_ue(edev_ctl, 0, bank,
				      "LLCC Tag RAM uncorrectable Error");
		break;
	default:
		ret = -EINVAL;
		edac_printk(KERN_CRIT, EDAC_LLCC, "Unexpected error type: %d\n",
			    err_type);
	}

	return ret;
}

static irqreturn_t
llcc_ecc_irq_handler(int irq, void *edev_ctl)
{
	struct edac_device_ctl_info *edac_dev_ctl = edev_ctl;
	struct llcc_drv_data *drv = edac_dev_ctl->pvt_info;
	irqreturn_t irq_rc = IRQ_NONE;
	u32 drp_error, trp_error, i;
	bool irq_handled = false;
	int ret;

	/* Iterate over the banks and look for Tag RAM or Data RAM errors */
	for (i = 0; i < drv->num_banks; i++) {
		ret = regmap_read(drv->regmap,
				  drv->offsets[i] + DRP_INTERRUPT_STATUS,
				  &drp_error);

		if (!ret && (drp_error & SB_ECC_ERROR)) {
			edac_printk(KERN_CRIT, EDAC_LLCC,
				    "Single Bit Error detected in Data RAM\n");
			ret = dump_syn_reg(edev_ctl, LLCC_DRAM_CE, i);
		} else if (!ret && (drp_error & DB_ECC_ERROR)) {
			edac_printk(KERN_CRIT, EDAC_LLCC,
				    "Double Bit Error detected in Data RAM\n");
			ret = dump_syn_reg(edev_ctl, LLCC_DRAM_UE, i);
		}
		if (!ret)
			irq_handled = true;

		ret = regmap_read(drv->regmap,
				  drv->offsets[i] + TRP_INTERRUPT_0_STATUS,
				  &trp_error);

		if (!ret && (trp_error & SB_ECC_ERROR)) {
			edac_printk(KERN_CRIT, EDAC_LLCC,
				    "Single Bit Error detected in Tag RAM\n");
			ret = dump_syn_reg(edev_ctl, LLCC_TRAM_CE, i);
		} else if (!ret && (trp_error & DB_ECC_ERROR)) {
			edac_printk(KERN_CRIT, EDAC_LLCC,
				    "Double Bit Error detected in Tag RAM\n");
			ret = dump_syn_reg(edev_ctl, LLCC_TRAM_UE, i);
		}
		if (!ret)
			irq_handled = true;
	}

	if (irq_handled)
		irq_rc = IRQ_HANDLED;

	return irq_rc;
}

static void qcom_llcc_poll_cache_errors(struct edac_device_ctl_info *edev_ctl)
{
	llcc_ecc_irq_handler(0, edev_ctl);
}

static int qcom_llcc_edac_probe(struct platform_device *pdev)
{
	struct llcc_drv_data *llcc_driv_data = pdev->dev.platform_data;
	struct edac_device_ctl_info *edev_ctl;
	struct device *dev = &pdev->dev;
	int ecc_irq;
	int rc;

	if (llcc_driv_data->llcc_ver > 20) {
		edac_reg_data = edac_reg_data_v21;
		edac_regs = edac_regs_v21;
	}

	/* Allocate edac control info */
	edev_ctl = edac_device_alloc_ctl_info(0, "qcom-llcc", 1, "bank",
					      llcc_driv_data->num_banks, 1,
					      NULL, 0,
					      edac_device_alloc_index());

	if (!edev_ctl)
		return -ENOMEM;

	edev_ctl->dev = dev;
	edev_ctl->mod_name = dev_name(dev);
	edev_ctl->dev_name = dev_name(dev);
	edev_ctl->ctl_name = "llcc";
	edev_ctl->panic_on_ue = LLCC_ERP_PANIC_ON_UE;
	edev_ctl->pvt_info = llcc_driv_data;

	/* Request for ecc irq */
	ecc_irq = llcc_driv_data->ecc_irq;
	if (ecc_irq < 0) {
		dev_info(dev, "No ECC IRQ; defaulting to polling mode\n");
		edev_ctl->poll_msec = poll_msec;
		edev_ctl->edac_check = qcom_llcc_poll_cache_errors;
	}

	rc = edac_device_add_device(edev_ctl);
	if (rc)
		goto out_mem;

	if (ecc_irq >= 0) {
		rc = qcom_llcc_core_setup(llcc_driv_data->bcast_regmap);
		if (rc)
			goto out_dev;

		rc = devm_request_irq(dev, ecc_irq, llcc_ecc_irq_handler,
			      IRQF_SHARED | IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
			      "llcc_ecc", edev_ctl);
		if (rc)
			goto out_dev;
	}

	platform_set_drvdata(pdev, edev_ctl);
	return rc;

out_dev:
	edac_device_del_device(edev_ctl->dev);
out_mem:
	edac_device_free_ctl_info(edev_ctl);
	return rc;
}

static int qcom_llcc_edac_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edev_ctl = dev_get_drvdata(&pdev->dev);

	edac_device_del_device(edev_ctl->dev);
	edac_device_free_ctl_info(edev_ctl);

	return 0;
}

static struct platform_driver qcom_llcc_edac_driver = {
	.probe = qcom_llcc_edac_probe,
	.remove = qcom_llcc_edac_remove,
	.driver = {
		.name = "qcom_llcc_edac",
	},
};
module_platform_driver(qcom_llcc_edac_driver);

MODULE_DESCRIPTION("QCOM EDAC driver");
MODULE_LICENSE("GPL v2");
