// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx SDFEC
 *
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Description:
 * This driver is developed for SDFEC16 (Soft Decision FEC 16nm)
 * IP. It exposes a char device which supports file operations
 * like  open(), close() and ioctl().
 */

#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include <uapi/misc/xilinx_sdfec.h>

#define DEV_NAME_LEN 12

static struct idr dev_idr;
static struct mutex dev_idr_lock;

/* Xilinx SDFEC Register Map */
/* CODE_WRI_PROTECT Register */
#define XSDFEC_CODE_WR_PROTECT_ADDR (0x4)

/* ACTIVE Register */
#define XSDFEC_ACTIVE_ADDR (0x8)
#define XSDFEC_IS_ACTIVITY_SET (0x1)

/* AXIS_WIDTH Register */
#define XSDFEC_AXIS_WIDTH_ADDR (0xC)
#define XSDFEC_AXIS_DOUT_WORDS_LSB (5)
#define XSDFEC_AXIS_DOUT_WIDTH_LSB (3)
#define XSDFEC_AXIS_DIN_WORDS_LSB (2)
#define XSDFEC_AXIS_DIN_WIDTH_LSB (0)

/* AXIS_ENABLE Register */
#define XSDFEC_AXIS_ENABLE_ADDR (0x10)
#define XSDFEC_AXIS_OUT_ENABLE_MASK (0x38)
#define XSDFEC_AXIS_IN_ENABLE_MASK (0x7)
#define XSDFEC_AXIS_ENABLE_MASK                                                \
	(XSDFEC_AXIS_OUT_ENABLE_MASK | XSDFEC_AXIS_IN_ENABLE_MASK)

/* FEC_CODE Register */
#define XSDFEC_FEC_CODE_ADDR (0x14)

/* ORDER Register Map */
#define XSDFEC_ORDER_ADDR (0x18)

/* Interrupt Status Register */
#define XSDFEC_ISR_ADDR (0x1C)
/* Interrupt Status Register Bit Mask */
#define XSDFEC_ISR_MASK (0x3F)

/* Write Only - Interrupt Enable Register */
#define XSDFEC_IER_ADDR (0x20)
/* Write Only - Interrupt Disable Register */
#define XSDFEC_IDR_ADDR (0x24)
/* Read Only - Interrupt Mask Register */
#define XSDFEC_IMR_ADDR (0x28)

/* ECC Interrupt Status Register */
#define XSDFEC_ECC_ISR_ADDR (0x2C)
/* Single Bit Errors */
#define XSDFEC_ECC_ISR_SBE_MASK (0x7FF)
/* PL Initialize Single Bit Errors */
#define XSDFEC_PL_INIT_ECC_ISR_SBE_MASK (0x3C00000)
/* Multi Bit Errors */
#define XSDFEC_ECC_ISR_MBE_MASK (0x3FF800)
/* PL Initialize Multi Bit Errors */
#define XSDFEC_PL_INIT_ECC_ISR_MBE_MASK (0x3C000000)
/* Multi Bit Error to Event Shift */
#define XSDFEC_ECC_ISR_MBE_TO_EVENT_SHIFT (11)
/* PL Initialize Multi Bit Error to Event Shift */
#define XSDFEC_PL_INIT_ECC_ISR_MBE_TO_EVENT_SHIFT (4)
/* ECC Interrupt Status Bit Mask */
#define XSDFEC_ECC_ISR_MASK (XSDFEC_ECC_ISR_SBE_MASK | XSDFEC_ECC_ISR_MBE_MASK)
/* ECC Interrupt Status PL Initialize Bit Mask */
#define XSDFEC_PL_INIT_ECC_ISR_MASK                                            \
	(XSDFEC_PL_INIT_ECC_ISR_SBE_MASK | XSDFEC_PL_INIT_ECC_ISR_MBE_MASK)
/* ECC Interrupt Status All Bit Mask */
#define XSDFEC_ALL_ECC_ISR_MASK                                                \
	(XSDFEC_ECC_ISR_MASK | XSDFEC_PL_INIT_ECC_ISR_MASK)
/* ECC Interrupt Status Single Bit Errors Mask */
#define XSDFEC_ALL_ECC_ISR_SBE_MASK                                            \
	(XSDFEC_ECC_ISR_SBE_MASK | XSDFEC_PL_INIT_ECC_ISR_SBE_MASK)
/* ECC Interrupt Status Multi Bit Errors Mask */
#define XSDFEC_ALL_ECC_ISR_MBE_MASK                                            \
	(XSDFEC_ECC_ISR_MBE_MASK | XSDFEC_PL_INIT_ECC_ISR_MBE_MASK)

/* Write Only - ECC Interrupt Enable Register */
#define XSDFEC_ECC_IER_ADDR (0x30)
/* Write Only - ECC Interrupt Disable Register */
#define XSDFEC_ECC_IDR_ADDR (0x34)
/* Read Only - ECC Interrupt Mask Register */
#define XSDFEC_ECC_IMR_ADDR (0x38)

/* BYPASS Register */
#define XSDFEC_BYPASS_ADDR (0x3C)

/**
 * struct xsdfec_clks - For managing SD-FEC clocks
 * @core_clk: Main processing clock for core
 * @axi_clk: AXI4-Lite memory-mapped clock
 * @din_words_clk: DIN Words AXI4-Stream Slave clock
 * @din_clk: DIN AXI4-Stream Slave clock
 * @dout_clk: DOUT Words AXI4-Stream Slave clock
 * @dout_words_clk: DOUT AXI4-Stream Slave clock
 * @ctrl_clk: Control AXI4-Stream Slave clock
 * @status_clk: Status AXI4-Stream Slave clock
 */
struct xsdfec_clks {
	struct clk *core_clk;
	struct clk *axi_clk;
	struct clk *din_words_clk;
	struct clk *din_clk;
	struct clk *dout_clk;
	struct clk *dout_words_clk;
	struct clk *ctrl_clk;
	struct clk *status_clk;
};

/**
 * struct xsdfec_dev - Driver data for SDFEC
 * @miscdev: Misc device handle
 * @clks: Clocks managed by the SDFEC driver
 * @regs: device physical base address
 * @dev: pointer to device struct
 * @config: Configuration of the SDFEC device
 * @dev_name: Device name
 * @state: State of the SDFEC device
 * @error_data_lock: Error counter and states spinlock
 * @dev_id: Device ID
 *
 * This structure contains necessary state for SDFEC driver to operate
 */
struct xsdfec_dev {
	struct miscdevice miscdev;
	struct xsdfec_clks clks;
	void __iomem *regs;
	struct device *dev;
	struct xsdfec_config config;
	char dev_name[DEV_NAME_LEN];
	enum xsdfec_state state;
	/* Spinlock to protect state_updated and stats_updated */
	spinlock_t error_data_lock;
	int dev_id;
};

static inline void xsdfec_regwrite(struct xsdfec_dev *xsdfec, u32 addr,
				   u32 value)
{
	dev_dbg(xsdfec->dev, "Writing 0x%x to offset 0x%x", value, addr);
	iowrite32(value, xsdfec->regs + addr);
}

static inline u32 xsdfec_regread(struct xsdfec_dev *xsdfec, u32 addr)
{
	u32 rval;

	rval = ioread32(xsdfec->regs + addr);
	dev_dbg(xsdfec->dev, "Read value = 0x%x from offset 0x%x", rval, addr);
	return rval;
}

static void update_bool_config_from_reg(struct xsdfec_dev *xsdfec,
					u32 reg_offset, u32 bit_num,
					char *config_value)
{
	u32 reg_val;
	u32 bit_mask = 1 << bit_num;

	reg_val = xsdfec_regread(xsdfec, reg_offset);
	*config_value = (reg_val & bit_mask) > 0;
}

static void update_config_from_hw(struct xsdfec_dev *xsdfec)
{
	u32 reg_value;
	bool sdfec_started;

	/* Update the Order */
	reg_value = xsdfec_regread(xsdfec, XSDFEC_ORDER_ADDR);
	xsdfec->config.order = reg_value;

	update_bool_config_from_reg(xsdfec, XSDFEC_BYPASS_ADDR,
				    0, /* Bit Number, maybe change to mask */
				    &xsdfec->config.bypass);

	update_bool_config_from_reg(xsdfec, XSDFEC_CODE_WR_PROTECT_ADDR,
				    0, /* Bit Number */
				    &xsdfec->config.code_wr_protect);

	reg_value = xsdfec_regread(xsdfec, XSDFEC_IMR_ADDR);
	xsdfec->config.irq.enable_isr = (reg_value & XSDFEC_ISR_MASK) > 0;

	reg_value = xsdfec_regread(xsdfec, XSDFEC_ECC_IMR_ADDR);
	xsdfec->config.irq.enable_ecc_isr =
		(reg_value & XSDFEC_ECC_ISR_MASK) > 0;

	reg_value = xsdfec_regread(xsdfec, XSDFEC_AXIS_ENABLE_ADDR);
	sdfec_started = (reg_value & XSDFEC_AXIS_IN_ENABLE_MASK) > 0;
	if (sdfec_started)
		xsdfec->state = XSDFEC_STARTED;
	else
		xsdfec->state = XSDFEC_STOPPED;
}

static u32
xsdfec_translate_axis_width_cfg_val(enum xsdfec_axis_width axis_width_cfg)
{
	u32 axis_width_field = 0;

	switch (axis_width_cfg) {
	case XSDFEC_1x128b:
		axis_width_field = 0;
		break;
	case XSDFEC_2x128b:
		axis_width_field = 1;
		break;
	case XSDFEC_4x128b:
		axis_width_field = 2;
		break;
	}

	return axis_width_field;
}

static u32 xsdfec_translate_axis_words_cfg_val(enum xsdfec_axis_word_include
	axis_word_inc_cfg)
{
	u32 axis_words_field = 0;

	if (axis_word_inc_cfg == XSDFEC_FIXED_VALUE ||
	    axis_word_inc_cfg == XSDFEC_IN_BLOCK)
		axis_words_field = 0;
	else if (axis_word_inc_cfg == XSDFEC_PER_AXI_TRANSACTION)
		axis_words_field = 1;

	return axis_words_field;
}

static int xsdfec_cfg_axi_streams(struct xsdfec_dev *xsdfec)
{
	u32 reg_value;
	u32 dout_words_field;
	u32 dout_width_field;
	u32 din_words_field;
	u32 din_width_field;
	struct xsdfec_config *config = &xsdfec->config;

	/* translate config info to register values */
	dout_words_field =
		xsdfec_translate_axis_words_cfg_val(config->dout_word_include);
	dout_width_field =
		xsdfec_translate_axis_width_cfg_val(config->dout_width);
	din_words_field =
		xsdfec_translate_axis_words_cfg_val(config->din_word_include);
	din_width_field =
		xsdfec_translate_axis_width_cfg_val(config->din_width);

	reg_value = dout_words_field << XSDFEC_AXIS_DOUT_WORDS_LSB;
	reg_value |= dout_width_field << XSDFEC_AXIS_DOUT_WIDTH_LSB;
	reg_value |= din_words_field << XSDFEC_AXIS_DIN_WORDS_LSB;
	reg_value |= din_width_field << XSDFEC_AXIS_DIN_WIDTH_LSB;

	xsdfec_regwrite(xsdfec, XSDFEC_AXIS_WIDTH_ADDR, reg_value);

	return 0;
}

static const struct file_operations xsdfec_fops = {
	.owner = THIS_MODULE,
};

static int xsdfec_parse_of(struct xsdfec_dev *xsdfec)
{
	struct device *dev = xsdfec->dev;
	struct device_node *node = dev->of_node;
	int rval;
	const char *fec_code;
	u32 din_width;
	u32 din_word_include;
	u32 dout_width;
	u32 dout_word_include;

	rval = of_property_read_string(node, "xlnx,sdfec-code", &fec_code);
	if (rval < 0)
		return rval;

	if (!strcasecmp(fec_code, "ldpc"))
		xsdfec->config.code = XSDFEC_LDPC_CODE;
	else if (!strcasecmp(fec_code, "turbo"))
		xsdfec->config.code = XSDFEC_TURBO_CODE;
	else
		return -EINVAL;

	rval = of_property_read_u32(node, "xlnx,sdfec-din-words",
				    &din_word_include);
	if (rval < 0)
		return rval;

	if (din_word_include < XSDFEC_AXIS_WORDS_INCLUDE_MAX)
		xsdfec->config.din_word_include = din_word_include;
	else
		return -EINVAL;

	rval = of_property_read_u32(node, "xlnx,sdfec-din-width", &din_width);
	if (rval < 0)
		return rval;

	switch (din_width) {
	/* Fall through and set for valid values */
	case XSDFEC_1x128b:
	case XSDFEC_2x128b:
	case XSDFEC_4x128b:
		xsdfec->config.din_width = din_width;
		break;
	default:
		return -EINVAL;
	}

	rval = of_property_read_u32(node, "xlnx,sdfec-dout-words",
				    &dout_word_include);
	if (rval < 0)
		return rval;

	if (dout_word_include < XSDFEC_AXIS_WORDS_INCLUDE_MAX)
		xsdfec->config.dout_word_include = dout_word_include;
	else
		return -EINVAL;

	rval = of_property_read_u32(node, "xlnx,sdfec-dout-width", &dout_width);
	if (rval < 0)
		return rval;

	switch (dout_width) {
	/* Fall through and set for valid values */
	case XSDFEC_1x128b:
	case XSDFEC_2x128b:
	case XSDFEC_4x128b:
		xsdfec->config.dout_width = dout_width;
		break;
	default:
		return -EINVAL;
	}

	/* Write LDPC to CODE Register */
	xsdfec_regwrite(xsdfec, XSDFEC_FEC_CODE_ADDR, xsdfec->config.code);

	xsdfec_cfg_axi_streams(xsdfec);

	return 0;
}

static int xsdfec_clk_init(struct platform_device *pdev,
			   struct xsdfec_clks *clks)
{
	int err;

	clks->core_clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(clks->core_clk)) {
		dev_err(&pdev->dev, "failed to get core_clk");
		return PTR_ERR(clks->core_clk);
	}

	clks->axi_clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(clks->axi_clk)) {
		dev_err(&pdev->dev, "failed to get axi_clk");
		return PTR_ERR(clks->axi_clk);
	}

	clks->din_words_clk = devm_clk_get(&pdev->dev, "s_axis_din_words_aclk");
	if (IS_ERR(clks->din_words_clk)) {
		if (PTR_ERR(clks->din_words_clk) != -ENOENT) {
			err = PTR_ERR(clks->din_words_clk);
			return err;
		}
		clks->din_words_clk = NULL;
	}

	clks->din_clk = devm_clk_get(&pdev->dev, "s_axis_din_aclk");
	if (IS_ERR(clks->din_clk)) {
		if (PTR_ERR(clks->din_clk) != -ENOENT) {
			err = PTR_ERR(clks->din_clk);
			return err;
		}
		clks->din_clk = NULL;
	}

	clks->dout_clk = devm_clk_get(&pdev->dev, "m_axis_dout_aclk");
	if (IS_ERR(clks->dout_clk)) {
		if (PTR_ERR(clks->dout_clk) != -ENOENT) {
			err = PTR_ERR(clks->dout_clk);
			return err;
		}
		clks->dout_clk = NULL;
	}

	clks->dout_words_clk =
		devm_clk_get(&pdev->dev, "s_axis_dout_words_aclk");
	if (IS_ERR(clks->dout_words_clk)) {
		if (PTR_ERR(clks->dout_words_clk) != -ENOENT) {
			err = PTR_ERR(clks->dout_words_clk);
			return err;
		}
		clks->dout_words_clk = NULL;
	}

	clks->ctrl_clk = devm_clk_get(&pdev->dev, "s_axis_ctrl_aclk");
	if (IS_ERR(clks->ctrl_clk)) {
		if (PTR_ERR(clks->ctrl_clk) != -ENOENT) {
			err = PTR_ERR(clks->ctrl_clk);
			return err;
		}
		clks->ctrl_clk = NULL;
	}

	clks->status_clk = devm_clk_get(&pdev->dev, "m_axis_status_aclk");
	if (IS_ERR(clks->status_clk)) {
		if (PTR_ERR(clks->status_clk) != -ENOENT) {
			err = PTR_ERR(clks->status_clk);
			return err;
		}
		clks->status_clk = NULL;
	}

	err = clk_prepare_enable(clks->core_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable core_clk (%d)", err);
		return err;
	}

	err = clk_prepare_enable(clks->axi_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axi_clk (%d)", err);
		goto err_disable_core_clk;
	}

	err = clk_prepare_enable(clks->din_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable din_clk (%d)", err);
		goto err_disable_axi_clk;
	}

	err = clk_prepare_enable(clks->din_words_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable din_words_clk (%d)", err);
		goto err_disable_din_clk;
	}

	err = clk_prepare_enable(clks->dout_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable dout_clk (%d)", err);
		goto err_disable_din_words_clk;
	}

	err = clk_prepare_enable(clks->dout_words_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable dout_words_clk (%d)",
			err);
		goto err_disable_dout_clk;
	}

	err = clk_prepare_enable(clks->ctrl_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable ctrl_clk (%d)", err);
		goto err_disable_dout_words_clk;
	}

	err = clk_prepare_enable(clks->status_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable status_clk (%d)\n", err);
		goto err_disable_ctrl_clk;
	}

	return err;

err_disable_ctrl_clk:
	clk_disable_unprepare(clks->ctrl_clk);
err_disable_dout_words_clk:
	clk_disable_unprepare(clks->dout_words_clk);
err_disable_dout_clk:
	clk_disable_unprepare(clks->dout_clk);
err_disable_din_words_clk:
	clk_disable_unprepare(clks->din_words_clk);
err_disable_din_clk:
	clk_disable_unprepare(clks->din_clk);
err_disable_axi_clk:
	clk_disable_unprepare(clks->axi_clk);
err_disable_core_clk:
	clk_disable_unprepare(clks->core_clk);

	return err;
}

static void xsdfec_disable_all_clks(struct xsdfec_clks *clks)
{
	clk_disable_unprepare(clks->status_clk);
	clk_disable_unprepare(clks->ctrl_clk);
	clk_disable_unprepare(clks->dout_words_clk);
	clk_disable_unprepare(clks->dout_clk);
	clk_disable_unprepare(clks->din_words_clk);
	clk_disable_unprepare(clks->din_clk);
	clk_disable_unprepare(clks->core_clk);
	clk_disable_unprepare(clks->axi_clk);
}

static void xsdfec_idr_remove(struct xsdfec_dev *xsdfec)
{
	mutex_lock(&dev_idr_lock);
	idr_remove(&dev_idr, xsdfec->dev_id);
	mutex_unlock(&dev_idr_lock);
}

static int xsdfec_probe(struct platform_device *pdev)
{
	struct xsdfec_dev *xsdfec;
	struct device *dev;
	struct resource *res;
	int err;

	xsdfec = devm_kzalloc(&pdev->dev, sizeof(*xsdfec), GFP_KERNEL);
	if (!xsdfec)
		return -ENOMEM;

	xsdfec->dev = &pdev->dev;
	spin_lock_init(&xsdfec->error_data_lock);

	err = xsdfec_clk_init(pdev, &xsdfec->clks);
	if (err)
		return err;

	dev = xsdfec->dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xsdfec->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(xsdfec->regs)) {
		err = PTR_ERR(xsdfec->regs);
		goto err_xsdfec_dev;
	}

	err = xsdfec_parse_of(xsdfec);
	if (err < 0)
		goto err_xsdfec_dev;

	update_config_from_hw(xsdfec);

	/* Save driver private data */
	platform_set_drvdata(pdev, xsdfec);

	mutex_lock(&dev_idr_lock);
	err = idr_alloc(&dev_idr, xsdfec->dev_name, 0, 0, GFP_KERNEL);
	mutex_unlock(&dev_idr_lock);
	if (err < 0)
		goto err_xsdfec_dev;
	xsdfec->dev_id = err;

	snprintf(xsdfec->dev_name, DEV_NAME_LEN, "xsdfec%d", xsdfec->dev_id);
	xsdfec->miscdev.minor = MISC_DYNAMIC_MINOR;
	xsdfec->miscdev.name = xsdfec->dev_name;
	xsdfec->miscdev.fops = &xsdfec_fops;
	xsdfec->miscdev.parent = dev;
	err = misc_register(&xsdfec->miscdev);
	if (err) {
		dev_err(dev, "error:%d. Unable to register device", err);
		goto err_xsdfec_idr;
	}
	return 0;

err_xsdfec_idr:
	xsdfec_idr_remove(xsdfec);
err_xsdfec_dev:
	xsdfec_disable_all_clks(&xsdfec->clks);
	return err;
}

static int xsdfec_remove(struct platform_device *pdev)
{
	struct xsdfec_dev *xsdfec;

	xsdfec = platform_get_drvdata(pdev);
	misc_deregister(&xsdfec->miscdev);
	xsdfec_idr_remove(xsdfec);
	xsdfec_disable_all_clks(&xsdfec->clks);
	return 0;
}

static const struct of_device_id xsdfec_of_match[] = {
	{
		.compatible = "xlnx,sd-fec-1.1",
	},
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, xsdfec_of_match);

static struct platform_driver xsdfec_driver = {
	.driver = {
		.name = "xilinx-sdfec",
		.of_match_table = xsdfec_of_match,
	},
	.probe = xsdfec_probe,
	.remove =  xsdfec_remove,
};

static int __init xsdfec_init(void)
{
	int err;

	mutex_init(&dev_idr_lock);
	idr_init(&dev_idr);
	err = platform_driver_register(&xsdfec_driver);
	if (err < 0) {
		pr_err("%s Unabled to register SDFEC driver", __func__);
		return err;
	}
	return 0;
}

static void __exit xsdfec_exit(void)
{
	platform_driver_unregister(&xsdfec_driver);
	idr_destroy(&dev_idr);
}

module_init(xsdfec_init);
module_exit(xsdfec_exit);

MODULE_AUTHOR("Xilinx, Inc");
MODULE_DESCRIPTION("Xilinx SD-FEC16 Driver");
MODULE_LICENSE("GPL");
