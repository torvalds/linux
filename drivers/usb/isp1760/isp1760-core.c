// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the NXP ISP1760 chip
 *
 * Copyright 2014 Laurent Pinchart
 * Copyright 2007 Sebastian Siewior
 *
 * Contacts:
 *	Sebastian Siewior <bigeasy@linutronix.de>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "isp1760-core.h"
#include "isp1760-hcd.h"
#include "isp1760-regs.h"
#include "isp1760-udc.h"

static void isp1760_init_core(struct isp1760_device *isp)
{
	struct isp1760_hcd *hcd = &isp->hcd;
	struct isp1760_udc *udc = &isp->udc;

	/* Low-level chip reset */
	if (isp->rst_gpio) {
		gpiod_set_value_cansleep(isp->rst_gpio, 1);
		msleep(50);
		gpiod_set_value_cansleep(isp->rst_gpio, 0);
	}

	/*
	 * Reset the host controller, including the CPU interface
	 * configuration.
	 */
	isp1760_field_set(hcd->fields, SW_RESET_RESET_ALL);
	msleep(100);

	/* Setup HW Mode Control: This assumes a level active-low interrupt */
	if (isp->devflags & ISP1760_FLAG_BUS_WIDTH_16)
		isp1760_field_clear(hcd->fields, HW_DATA_BUS_WIDTH);
	if (isp->devflags & ISP1760_FLAG_ANALOG_OC)
		isp1760_field_set(hcd->fields, HW_ANA_DIGI_OC);
	if (isp->devflags & ISP1760_FLAG_DACK_POL_HIGH)
		isp1760_field_set(hcd->fields, HW_DACK_POL_HIGH);
	if (isp->devflags & ISP1760_FLAG_DREQ_POL_HIGH)
		isp1760_field_set(hcd->fields, HW_DREQ_POL_HIGH);
	if (isp->devflags & ISP1760_FLAG_INTR_POL_HIGH)
		isp1760_field_set(hcd->fields, HW_INTR_HIGH_ACT);
	if (isp->devflags & ISP1760_FLAG_INTR_EDGE_TRIG)
		isp1760_field_set(hcd->fields, HW_INTR_EDGE_TRIG);

	/*
	 * The ISP1761 has a dedicated DC IRQ line but supports sharing the HC
	 * IRQ line for both the host and device controllers. Hardcode IRQ
	 * sharing for now and disable the DC interrupts globally to avoid
	 * spurious interrupts during HCD registration.
	 */
	if (isp->devflags & ISP1760_FLAG_ISP1761) {
		isp1760_reg_write(udc->regs, ISP176x_DC_MODE, 0);
		isp1760_field_set(hcd->fields, HW_COMN_IRQ);
	}

	/*
	 * PORT 1 Control register of the ISP1760 is the OTG control register
	 * on ISP1761.
	 *
	 * TODO: Really support OTG. For now we configure port 1 in device mode
	 */
	if ((isp->devflags & ISP1760_FLAG_ISP1761) &&
	    (isp->devflags & ISP1760_FLAG_PERIPHERAL_EN)) {
		isp1760_field_set(hcd->fields, HW_DM_PULLDOWN);
		isp1760_field_set(hcd->fields, HW_DP_PULLDOWN);
		isp1760_field_set(hcd->fields, HW_OTG_DISABLE);
	} else {
		isp1760_field_set(hcd->fields, HW_SW_SEL_HC_DC);
		isp1760_field_set(hcd->fields, HW_VBUS_DRV);
		isp1760_field_set(hcd->fields, HW_SEL_CP_EXT);
	}

	dev_info(isp->dev, "bus width: %u, oc: %s\n",
		 isp->devflags & ISP1760_FLAG_BUS_WIDTH_16 ? 16 : 32,
		 isp->devflags & ISP1760_FLAG_ANALOG_OC ? "analog" : "digital");
}

void isp1760_set_pullup(struct isp1760_device *isp, bool enable)
{
	struct isp1760_udc *udc = &isp->udc;

	if (enable)
		isp1760_field_set(udc->fields, HW_DP_PULLUP);
	else
		isp1760_field_set(udc->fields, HW_DP_PULLUP_CLEAR);
}

/*
 * 60kb divided in:
 * - 32 blocks @ 256  bytes
 * - 20 blocks @ 1024 bytes
 * -  4 blocks @ 8192 bytes
 */
static const struct isp1760_memory_layout isp176x_memory_conf = {
	.blocks[0]		= 32,
	.blocks_size[0]		= 256,
	.blocks[1]		= 20,
	.blocks_size[1]		= 1024,
	.blocks[2]		= 4,
	.blocks_size[2]		= 8192,

	.ptd_num		= 32,
	.payload_blocks		= 32 + 20 + 4,
	.payload_area_size	= 0xf000,
};

static const struct regmap_range isp176x_hc_volatile_ranges[] = {
	regmap_reg_range(ISP176x_HC_USBCMD, ISP176x_HC_ATL_PTD_LASTPTD),
	regmap_reg_range(ISP176x_HC_BUFFER_STATUS, ISP176x_HC_MEMORY),
	regmap_reg_range(ISP176x_HC_INTERRUPT, ISP176x_HC_ATL_IRQ_MASK_AND),
};

static const struct regmap_access_table isp176x_hc_volatile_table = {
	.yes_ranges	= isp176x_hc_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(isp176x_hc_volatile_ranges),
};

static struct regmap_config isp1760_hc_regmap_conf = {
	.name = "isp1760-hc",
	.reg_bits = 16,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = ISP176x_HC_MEMORY,
	.volatile_table = &isp176x_hc_volatile_table,
};

static const struct reg_field isp1760_hc_reg_fields[] = {
	[HCS_PPC]		= REG_FIELD(ISP176x_HC_HCSPARAMS, 4, 4),
	[HCS_N_PORTS]		= REG_FIELD(ISP176x_HC_HCSPARAMS, 0, 3),
	[HCC_ISOC_CACHE]	= REG_FIELD(ISP176x_HC_HCCPARAMS, 7, 7),
	[HCC_ISOC_THRES]	= REG_FIELD(ISP176x_HC_HCCPARAMS, 4, 6),
	[CMD_LRESET]		= REG_FIELD(ISP176x_HC_USBCMD, 7, 7),
	[CMD_RESET]		= REG_FIELD(ISP176x_HC_USBCMD, 1, 1),
	[CMD_RUN]		= REG_FIELD(ISP176x_HC_USBCMD, 0, 0),
	[STS_PCD]		= REG_FIELD(ISP176x_HC_USBSTS, 2, 2),
	[HC_FRINDEX]		= REG_FIELD(ISP176x_HC_FRINDEX, 0, 13),
	[FLAG_CF]		= REG_FIELD(ISP176x_HC_CONFIGFLAG, 0, 0),
	[PORT_OWNER]		= REG_FIELD(ISP176x_HC_PORTSC1, 13, 13),
	[PORT_POWER]		= REG_FIELD(ISP176x_HC_PORTSC1, 12, 12),
	[PORT_LSTATUS]		= REG_FIELD(ISP176x_HC_PORTSC1, 10, 11),
	[PORT_RESET]		= REG_FIELD(ISP176x_HC_PORTSC1, 8, 8),
	[PORT_SUSPEND]		= REG_FIELD(ISP176x_HC_PORTSC1, 7, 7),
	[PORT_RESUME]		= REG_FIELD(ISP176x_HC_PORTSC1, 6, 6),
	[PORT_PE]		= REG_FIELD(ISP176x_HC_PORTSC1, 2, 2),
	[PORT_CSC]		= REG_FIELD(ISP176x_HC_PORTSC1, 1, 1),
	[PORT_CONNECT]		= REG_FIELD(ISP176x_HC_PORTSC1, 0, 0),
	[ALL_ATX_RESET]		= REG_FIELD(ISP176x_HC_HW_MODE_CTRL, 31, 31),
	[HW_ANA_DIGI_OC]	= REG_FIELD(ISP176x_HC_HW_MODE_CTRL, 15, 15),
	[HW_COMN_IRQ]		= REG_FIELD(ISP176x_HC_HW_MODE_CTRL, 10, 10),
	[HW_DATA_BUS_WIDTH]	= REG_FIELD(ISP176x_HC_HW_MODE_CTRL, 8, 8),
	[HW_DACK_POL_HIGH]	= REG_FIELD(ISP176x_HC_HW_MODE_CTRL, 6, 6),
	[HW_DREQ_POL_HIGH]	= REG_FIELD(ISP176x_HC_HW_MODE_CTRL, 5, 5),
	[HW_INTR_HIGH_ACT]	= REG_FIELD(ISP176x_HC_HW_MODE_CTRL, 2, 2),
	[HW_INTR_EDGE_TRIG]	= REG_FIELD(ISP176x_HC_HW_MODE_CTRL, 1, 1),
	[HW_GLOBAL_INTR_EN]	= REG_FIELD(ISP176x_HC_HW_MODE_CTRL, 0, 0),
	[SW_RESET_RESET_ALL]	= REG_FIELD(ISP176x_HC_RESET, 0, 0),
	[INT_BUF_FILL]		= REG_FIELD(ISP176x_HC_BUFFER_STATUS, 1, 1),
	[ATL_BUF_FILL]		= REG_FIELD(ISP176x_HC_BUFFER_STATUS, 0, 0),
	[MEM_BANK_SEL]		= REG_FIELD(ISP176x_HC_MEMORY, 16, 17),
	[MEM_START_ADDR]	= REG_FIELD(ISP176x_HC_MEMORY, 0, 15),
	[HC_INT_ENABLE]		= REG_FIELD(ISP176x_HC_INTERRUPT_ENABLE, 7, 8),
};

static const struct regmap_range isp176x_dc_volatile_ranges[] = {
	regmap_reg_range(ISP176x_DC_EPMAXPKTSZ, ISP176x_DC_EPTYPE),
	regmap_reg_range(ISP176x_DC_BUFLEN, ISP176x_DC_EPINDEX),
	regmap_reg_range(ISP1761_DC_OTG_CTRL_SET, ISP1761_DC_OTG_CTRL_CLEAR),
};

static const struct regmap_access_table isp176x_dc_volatile_table = {
	.yes_ranges	= isp176x_dc_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(isp176x_dc_volatile_ranges),
};

static struct regmap_config isp1761_dc_regmap_conf = {
	.name = "isp1761-dc",
	.reg_bits = 16,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = ISP1761_DC_OTG_CTRL_CLEAR,
	.volatile_table = &isp176x_dc_volatile_table,
};

static const struct reg_field isp1761_dc_reg_fields[] = {
	[DC_DEVEN]		= REG_FIELD(ISP176x_DC_ADDRESS, 7, 7),
	[DC_DEVADDR]		= REG_FIELD(ISP176x_DC_ADDRESS, 0, 6),
	[DC_VBUSSTAT]		= REG_FIELD(ISP176x_DC_MODE, 8, 8),
	[DC_SFRESET]		= REG_FIELD(ISP176x_DC_MODE, 4, 4),
	[DC_GLINTENA]		= REG_FIELD(ISP176x_DC_MODE, 3, 3),
	[DC_CDBGMOD_ACK]	= REG_FIELD(ISP176x_DC_INTCONF, 6, 6),
	[DC_DDBGMODIN_ACK]	= REG_FIELD(ISP176x_DC_INTCONF, 4, 4),
	[DC_DDBGMODOUT_ACK]	= REG_FIELD(ISP176x_DC_INTCONF, 2, 2),
	[DC_INTPOL]		= REG_FIELD(ISP176x_DC_INTCONF, 0, 0),
	[DC_IEPRXTX_7]		= REG_FIELD(ISP176x_DC_INTENABLE, 25, 25),
	[DC_IEPRXTX_6]		= REG_FIELD(ISP176x_DC_INTENABLE, 23, 23),
	[DC_IEPRXTX_5]		= REG_FIELD(ISP176x_DC_INTENABLE, 21, 21),
	[DC_IEPRXTX_4]		= REG_FIELD(ISP176x_DC_INTENABLE, 19, 19),
	[DC_IEPRXTX_3]		= REG_FIELD(ISP176x_DC_INTENABLE, 17, 17),
	[DC_IEPRXTX_2]		= REG_FIELD(ISP176x_DC_INTENABLE, 15, 15),
	[DC_IEPRXTX_1]		= REG_FIELD(ISP176x_DC_INTENABLE, 13, 13),
	[DC_IEPRXTX_0]		= REG_FIELD(ISP176x_DC_INTENABLE, 11, 11),
	[DC_IEP0SETUP]		= REG_FIELD(ISP176x_DC_INTENABLE, 8, 8),
	[DC_IEVBUS]		= REG_FIELD(ISP176x_DC_INTENABLE, 7, 7),
	[DC_IEHS_STA]		= REG_FIELD(ISP176x_DC_INTENABLE, 5, 5),
	[DC_IERESM]		= REG_FIELD(ISP176x_DC_INTENABLE, 4, 4),
	[DC_IESUSP]		= REG_FIELD(ISP176x_DC_INTENABLE, 3, 3),
	[DC_IEBRST]		= REG_FIELD(ISP176x_DC_INTENABLE, 0, 0),
	[DC_EP0SETUP]		= REG_FIELD(ISP176x_DC_EPINDEX, 5, 5),
	[DC_ENDPIDX]		= REG_FIELD(ISP176x_DC_EPINDEX, 1, 4),
	[DC_EPDIR]		= REG_FIELD(ISP176x_DC_EPINDEX, 0, 0),
	[DC_CLBUF]		= REG_FIELD(ISP176x_DC_CTRLFUNC, 4, 4),
	[DC_VENDP]		= REG_FIELD(ISP176x_DC_CTRLFUNC, 3, 3),
	[DC_DSEN]		= REG_FIELD(ISP176x_DC_CTRLFUNC, 2, 2),
	[DC_STATUS]		= REG_FIELD(ISP176x_DC_CTRLFUNC, 1, 1),
	[DC_STALL]		= REG_FIELD(ISP176x_DC_CTRLFUNC, 0, 0),
	[DC_BUFLEN]		= REG_FIELD(ISP176x_DC_BUFLEN, 0, 15),
	[DC_FFOSZ]		= REG_FIELD(ISP176x_DC_EPMAXPKTSZ, 0, 10),
	[DC_EPENABLE]		= REG_FIELD(ISP176x_DC_EPTYPE, 3, 3),
	[DC_ENDPTYP]		= REG_FIELD(ISP176x_DC_EPTYPE, 0, 1),
	[DC_UFRAMENUM]		= REG_FIELD(ISP176x_DC_FRAMENUM, 11, 13),
	[DC_FRAMENUM]		= REG_FIELD(ISP176x_DC_FRAMENUM, 0, 10),
	[HW_OTG_DISABLE]	= REG_FIELD(ISP1761_DC_OTG_CTRL_SET, 10, 10),
	[HW_SW_SEL_HC_DC]	= REG_FIELD(ISP1761_DC_OTG_CTRL_SET, 7, 7),
	[HW_VBUS_DRV]		= REG_FIELD(ISP1761_DC_OTG_CTRL_SET, 4, 4),
	[HW_SEL_CP_EXT]		= REG_FIELD(ISP1761_DC_OTG_CTRL_SET, 3, 3),
	[HW_DM_PULLDOWN]	= REG_FIELD(ISP1761_DC_OTG_CTRL_SET, 2, 2),
	[HW_DP_PULLDOWN]	= REG_FIELD(ISP1761_DC_OTG_CTRL_SET, 1, 1),
	[HW_DP_PULLUP]		= REG_FIELD(ISP1761_DC_OTG_CTRL_SET, 0, 0),
	[HW_OTG_DISABLE_CLEAR]	= REG_FIELD(ISP1761_DC_OTG_CTRL_CLEAR, 10, 10),
	[HW_SW_SEL_HC_DC_CLEAR]	= REG_FIELD(ISP1761_DC_OTG_CTRL_CLEAR, 7, 7),
	[HW_VBUS_DRV_CLEAR]	= REG_FIELD(ISP1761_DC_OTG_CTRL_CLEAR, 4, 4),
	[HW_SEL_CP_EXT_CLEAR]	= REG_FIELD(ISP1761_DC_OTG_CTRL_CLEAR, 3, 3),
	[HW_DM_PULLDOWN_CLEAR]	= REG_FIELD(ISP1761_DC_OTG_CTRL_CLEAR, 2, 2),
	[HW_DP_PULLDOWN_CLEAR]	= REG_FIELD(ISP1761_DC_OTG_CTRL_CLEAR, 1, 1),
	[HW_DP_PULLUP_CLEAR]	= REG_FIELD(ISP1761_DC_OTG_CTRL_CLEAR, 0, 0),
};

int isp1760_register(struct resource *mem, int irq, unsigned long irqflags,
		     struct device *dev, unsigned int devflags)
{
	struct isp1760_device *isp;
	struct isp1760_hcd *hcd;
	struct isp1760_udc *udc;
	bool udc_disabled = !(devflags & ISP1760_FLAG_ISP1761);
	struct regmap_field *f;
	void __iomem *base;
	int ret;
	int i;

	/*
	 * If neither the HCD not the UDC is enabled return an error, as no
	 * device would be registered.
	 */
	if ((!IS_ENABLED(CONFIG_USB_ISP1760_HCD) || usb_disabled()) &&
	    (!IS_ENABLED(CONFIG_USB_ISP1761_UDC) || udc_disabled))
		return -ENODEV;

	isp = devm_kzalloc(dev, sizeof(*isp), GFP_KERNEL);
	if (!isp)
		return -ENOMEM;

	isp->dev = dev;
	isp->devflags = devflags;
	hcd = &isp->hcd;
	udc = &isp->udc;

	if (devflags & ISP1760_FLAG_BUS_WIDTH_16) {
		isp1760_hc_regmap_conf.val_bits = 16;
		isp1761_dc_regmap_conf.val_bits = 16;
	}

	isp->rst_gpio = devm_gpiod_get_optional(dev, NULL, GPIOD_OUT_HIGH);
	if (IS_ERR(isp->rst_gpio))
		return PTR_ERR(isp->rst_gpio);

	hcd->base = devm_ioremap_resource(dev, mem);
	if (IS_ERR(hcd->base))
		return PTR_ERR(hcd->base);

	hcd->regs = devm_regmap_init_mmio(dev, base, &isp1760_hc_regmap_conf);
	if (IS_ERR(hcd->regs))
		return PTR_ERR(hcd->regs);

	for (i = 0; i < HC_FIELD_MAX; i++) {
		f = devm_regmap_field_alloc(dev, hcd->regs,
					    isp1760_hc_reg_fields[i]);
		if (IS_ERR(f))
			return PTR_ERR(f);

		hcd->fields[i] = f;
	}

	udc->regs = devm_regmap_init_mmio(dev, base, &isp1761_dc_regmap_conf);
	if (IS_ERR(udc->regs))
		return PTR_ERR(udc->regs);

	for (i = 0; i < DC_FIELD_MAX; i++) {
		f = devm_regmap_field_alloc(dev, udc->regs,
					    isp1761_dc_reg_fields[i]);
		if (IS_ERR(f))
			return PTR_ERR(f);

		udc->fields[i] = f;
	}

	hcd->memory_layout = &isp176x_memory_conf;

	isp1760_init_core(isp);

	if (IS_ENABLED(CONFIG_USB_ISP1760_HCD) && !usb_disabled()) {
		ret = isp1760_hcd_register(hcd, mem, irq,
					   irqflags | IRQF_SHARED, dev);
		if (ret < 0)
			return ret;
	}

	if (IS_ENABLED(CONFIG_USB_ISP1761_UDC) && !udc_disabled) {
		ret = isp1760_udc_register(isp, irq, irqflags);
		if (ret < 0) {
			isp1760_hcd_unregister(hcd);
			return ret;
		}
	}

	dev_set_drvdata(dev, isp);

	return 0;
}

void isp1760_unregister(struct device *dev)
{
	struct isp1760_device *isp = dev_get_drvdata(dev);

	isp1760_udc_unregister(isp);
	isp1760_hcd_unregister(&isp->hcd);
}

MODULE_DESCRIPTION("Driver for the ISP1760 USB-controller from NXP");
MODULE_AUTHOR("Sebastian Siewior <bigeasy@linuxtronix.de>");
MODULE_LICENSE("GPL v2");
