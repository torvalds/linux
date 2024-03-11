// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OMAP3XXX L3 Interconnect Driver
 *
 * Copyright (C) 2011 Texas Corporation
 *	Felipe Balbi <balbi@ti.com>
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *	Sricharan <r.sricharan@ti.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>

#include "omap_l3_smx.h"

static inline u64 omap3_l3_readll(void __iomem *base, u16 reg)
{
	return __raw_readll(base + reg);
}

static inline void omap3_l3_writell(void __iomem *base, u16 reg, u64 value)
{
	__raw_writell(value, base + reg);
}

static inline enum omap3_l3_code omap3_l3_decode_error_code(u64 error)
{
	return (error & 0x0f000000) >> L3_ERROR_LOG_CODE;
}

static inline u32 omap3_l3_decode_addr(u64 error_addr)
{
	return error_addr & 0xffffffff;
}

static inline unsigned omap3_l3_decode_cmd(u64 error)
{
	return (error & 0x07) >> L3_ERROR_LOG_CMD;
}

static inline enum omap3_l3_initiator_id omap3_l3_decode_initid(u64 error)
{
	return (error & 0xff00) >> L3_ERROR_LOG_INITID;
}

static inline unsigned omap3_l3_decode_req_info(u64 error)
{
	return (error >> 32) & 0xffff;
}

static char *omap3_l3_code_string(u8 code)
{
	switch (code) {
	case OMAP_L3_CODE_NOERROR:
		return "No Error";
	case OMAP_L3_CODE_UNSUP_CMD:
		return "Unsupported Command";
	case OMAP_L3_CODE_ADDR_HOLE:
		return "Address Hole";
	case OMAP_L3_CODE_PROTECT_VIOLATION:
		return "Protection Violation";
	case OMAP_L3_CODE_IN_BAND_ERR:
		return "In-band Error";
	case OMAP_L3_CODE_REQ_TOUT_NOT_ACCEPT:
		return "Request Timeout Not Accepted";
	case OMAP_L3_CODE_REQ_TOUT_NO_RESP:
		return "Request Timeout, no response";
	default:
		return "UNKNOWN error";
	}
}

static char *omap3_l3_initiator_string(u8 initid)
{
	switch (initid) {
	case OMAP_L3_LCD:
		return "LCD";
	case OMAP_L3_SAD2D:
		return "SAD2D";
	case OMAP_L3_IA_MPU_SS_1:
	case OMAP_L3_IA_MPU_SS_2:
	case OMAP_L3_IA_MPU_SS_3:
	case OMAP_L3_IA_MPU_SS_4:
	case OMAP_L3_IA_MPU_SS_5:
		return "MPU";
	case OMAP_L3_IA_IVA_SS_1:
	case OMAP_L3_IA_IVA_SS_2:
	case OMAP_L3_IA_IVA_SS_3:
		return "IVA_SS";
	case OMAP_L3_IA_IVA_SS_DMA_1:
	case OMAP_L3_IA_IVA_SS_DMA_2:
	case OMAP_L3_IA_IVA_SS_DMA_3:
	case OMAP_L3_IA_IVA_SS_DMA_4:
	case OMAP_L3_IA_IVA_SS_DMA_5:
	case OMAP_L3_IA_IVA_SS_DMA_6:
		return "IVA_SS_DMA";
	case OMAP_L3_IA_SGX:
		return "SGX";
	case OMAP_L3_IA_CAM_1:
	case OMAP_L3_IA_CAM_2:
	case OMAP_L3_IA_CAM_3:
		return "CAM";
	case OMAP_L3_IA_DAP:
		return "DAP";
	case OMAP_L3_SDMA_WR_1:
	case OMAP_L3_SDMA_WR_2:
		return "SDMA_WR";
	case OMAP_L3_SDMA_RD_1:
	case OMAP_L3_SDMA_RD_2:
	case OMAP_L3_SDMA_RD_3:
	case OMAP_L3_SDMA_RD_4:
		return "SDMA_RD";
	case OMAP_L3_USBOTG:
		return "USB_OTG";
	case OMAP_L3_USBHOST:
		return "USB_HOST";
	default:
		return "UNKNOWN Initiator";
	}
}

/*
 * omap3_l3_block_irq - handles a register block's irq
 * @l3: struct omap3_l3 *
 * @base: register block base address
 * @error: L3_ERROR_LOG register of our block
 *
 * Called in hard-irq context. Caller should take care of locking
 *
 * OMAP36xx TRM gives, on page 2001, Figure 9-10, the Typical Error
 * Analysis Sequence, we are following that sequence here, please
 * refer to that Figure for more information on the subject.
 */
static irqreturn_t omap3_l3_block_irq(struct omap3_l3 *l3,
					u64 error, int error_addr)
{
	u8 code = omap3_l3_decode_error_code(error);
	u8 initid = omap3_l3_decode_initid(error);
	u8 multi = error & L3_ERROR_LOG_MULTI;
	u32 address = omap3_l3_decode_addr(error_addr);

	pr_err("%s seen by %s %s at address %x\n",
			omap3_l3_code_string(code),
			omap3_l3_initiator_string(initid),
			multi ? "Multiple Errors" : "", address);
	WARN_ON(1);

	return IRQ_HANDLED;
}

static irqreturn_t omap3_l3_app_irq(int irq, void *_l3)
{
	struct omap3_l3 *l3 = _l3;
	u64 status, clear;
	u64 error;
	u64 error_addr;
	u64 err_source = 0;
	void __iomem *base;
	int int_type;
	irqreturn_t ret = IRQ_NONE;

	int_type = irq == l3->app_irq ? L3_APPLICATION_ERROR : L3_DEBUG_ERROR;
	if (!int_type)
		status = omap3_l3_readll(l3->rt, L3_SI_FLAG_STATUS_0);
	else
		status = omap3_l3_readll(l3->rt, L3_SI_FLAG_STATUS_1);

	/* identify the error source */
	err_source = __ffs(status);

	base = l3->rt + omap3_l3_bases[int_type][err_source];
	error = omap3_l3_readll(base, L3_ERROR_LOG);
	if (error) {
		error_addr = omap3_l3_readll(base, L3_ERROR_LOG_ADDR);
		ret |= omap3_l3_block_irq(l3, error, error_addr);
	}

	/*
	 * if we have a timeout error, there's nothing we can
	 * do besides rebooting the board. So let's BUG on any
	 * of such errors and handle the others. timeout error
	 * is severe and not expected to occur.
	 */
	BUG_ON(!int_type && status & L3_STATUS_0_TIMEOUT_MASK);

	/* Clear the status register */
	clear = (L3_AGENT_STATUS_CLEAR_IA << int_type) |
		L3_AGENT_STATUS_CLEAR_TA;
	omap3_l3_writell(base, L3_AGENT_STATUS, clear);

	/* clear the error log register */
	omap3_l3_writell(base, L3_ERROR_LOG, error);

	return ret;
}

#if IS_BUILTIN(CONFIG_OF)
static const struct of_device_id omap3_l3_match[] = {
	{
		.compatible = "ti,omap3-l3-smx",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, omap3_l3_match);
#endif

static int omap3_l3_probe(struct platform_device *pdev)
{
	struct omap3_l3 *l3;
	struct resource *res;
	int ret;

	l3 = kzalloc(sizeof(*l3), GFP_KERNEL);
	if (!l3)
		return -ENOMEM;

	platform_set_drvdata(pdev, l3);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "couldn't find resource\n");
		ret = -ENODEV;
		goto err0;
	}
	l3->rt = ioremap(res->start, resource_size(res));
	if (!l3->rt) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err0;
	}

	l3->debug_irq = platform_get_irq(pdev, 0);
	ret = request_irq(l3->debug_irq, omap3_l3_app_irq, IRQF_TRIGGER_RISING,
			  "l3-debug-irq", l3);
	if (ret) {
		dev_err(&pdev->dev, "couldn't request debug irq\n");
		goto err1;
	}

	l3->app_irq = platform_get_irq(pdev, 1);
	ret = request_irq(l3->app_irq, omap3_l3_app_irq, IRQF_TRIGGER_RISING,
			  "l3-app-irq", l3);
	if (ret) {
		dev_err(&pdev->dev, "couldn't request app irq\n");
		goto err2;
	}

	return 0;

err2:
	free_irq(l3->debug_irq, l3);
err1:
	iounmap(l3->rt);
err0:
	kfree(l3);
	return ret;
}

static void omap3_l3_remove(struct platform_device *pdev)
{
	struct omap3_l3         *l3 = platform_get_drvdata(pdev);

	free_irq(l3->app_irq, l3);
	free_irq(l3->debug_irq, l3);
	iounmap(l3->rt);
	kfree(l3);
}

static struct platform_driver omap3_l3_driver = {
	.probe		= omap3_l3_probe,
	.remove_new     = omap3_l3_remove,
	.driver         = {
		.name   = "omap_l3_smx",
		.of_match_table = of_match_ptr(omap3_l3_match),
	},
};

static int __init omap3_l3_init(void)
{
	return platform_driver_register(&omap3_l3_driver);
}
postcore_initcall_sync(omap3_l3_init);

static void __exit omap3_l3_exit(void)
{
	platform_driver_unregister(&omap3_l3_driver);
}
module_exit(omap3_l3_exit);

MODULE_AUTHOR("Felipe Balbi");
MODULE_AUTHOR("Santosh Shilimkar");
MODULE_AUTHOR("Sricharan R");
MODULE_DESCRIPTION("OMAP3XXX L3 Interconnect Driver");
MODULE_LICENSE("GPL");
