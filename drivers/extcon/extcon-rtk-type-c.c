// SPDX-License-Identifier: GPL-2.0
/*
 *  * extcon-rtk-type-c.c - Realtek Extcon Type C driver
 *
 * Copyright (C) 2023 Realtek Semiconductor Corporation
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/syscalls.h>
#include <linux/suspend.h>
#include <linux/debugfs.h>
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/sys_soc.h>
#include <linux/nvmem-consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/usb/otg.h>
#include <linux/usb/typec.h>

struct cc_param {
	u32 rp_4p7k_code;
	u32 rp_36k_code;
	u32 rp_12k_code;
	u32 rd_code;
	u32 ra_code;
	u32 vref_2p6v;
	u32 vref_1p23v;
	u32 vref_0p8v;
	u32 vref_0p66v;
	u32 vref_0p4v;
	u32 vref_0p2v;
	u32 vref_1_1p6v;
	u32 vref_0_1p6v;
};

struct type_c_cfg {
	int parameter_ver; /* Parameter version */
	int cc_dfp_mode;
	struct cc_param cc1_param;
	struct cc_param cc2_param;

	u32 debounce_val;
	bool use_defalut_parameter;
};

struct type_c_data {
	void __iomem *reg_base;
	struct device *dev;
	struct extcon_dev *edev;

	u32 irq;

	/* rd control GPIO only for rtd1295 */
	struct gpio_desc *rd_ctrl_gpio_desc;

	/* Parameters */
	struct type_c_cfg *type_c_cfg;
	u32 dfp_mode_rp_en;
	u32 ufp_mode_rd_en;
	u32 cc1_code;
	u32 cc2_code;
	u32 cc1_vref;
	u32 cc2_vref;
	u32 debounce; /* 1b,1us 7f,4.7us */

	/* type_c state */
	int connect_change;
#define CONNECT_CHANGE 1
#define CONNECT_NO_CHANGE 0
	int cc_mode; /* cc is host or device */
#define IN_HOST_MODE 0x10
#define IN_DEVICE_MODE 0x20
	int is_attach;
#define IN_ATTACH 1
#define TO_ATTACH 1
#define IN_DETACH 0
#define TO_DETACH 0
	int at_cc1;
#define AT_CC1 1
#define AT_CC2 0

	u32 int_status;
	u32 cc_status;
	/* protect the data member */
	spinlock_t lock;
	struct delayed_work delayed_work;

	bool rd_en_at_first;

	struct dentry *debug_dir;

	struct typec_port *port;
};

/* Type C register offset */
#define USB_TYPEC_CTRL_CC1_0	0x0
#define USB_TYPEC_CTRL_CC1_1	0x4
#define USB_TYPEC_CTRL_CC2_0	0x8
#define USB_TYPEC_CTRL_CC2_1	0xC
#define USB_TYPEC_STS		0x10
#define USB_TYPEC_CTRL		0x14
#define USB_DBUS_PWR_CTRL	0x18

#define ENABLE_CC1	0x1
#define ENABLE_CC2	0x2
#define DISABLE_CC	0x0

/* Bit mapping USB_TYPEC_CTRL_CC1_0 and USB_TYPEC_CTRL_CC2_0 */
#define PLR_EN		BIT(29)
#define CC_SWITCH_MASK	(BIT(29) | BIT(28) | BIT(27))
#define CC_CODE_MASK	(0xfffff << 7)
#define rp4pk_code(val)	((0x1f & (val)) << 22)
#define code_rp4pk(val)	(((val) >> 22) & 0x1f)
#define rp36k_code(val)	((0x1f & (val)) << 17)
#define code_rp36k(val)	(((val) >> 17) & 0x1f)
#define rp12k_code(val)	((0x1f & (val)) << 12)
#define code_rp12k(val)	(((val) >> 12) & 0x1f)
#define rd_code(val)	((0x1f & (val)) << 7)
#define code_rd(val)	(((val) >> 7) & 0x1f)
#define dfp_mode(val)	((0x3 & (val)) << 5)
#define EN_RP4P7K	BIT(4)
#define EN_RP36K	BIT(3)
#define EN_RP12K	BIT(2)
#define EN_RD		BIT(1)
#define EN_CC_DET	BIT(0)

#define CC_MODE_UFP	0x0
#define CC_MODE_DFP_USB	0x1
#define CC_MODE_DFP_1_5	0x2
#define CC_MODE_DFP_3_0	0x3

/*
 * PARAMETER_V0:
 *  Realtek Kylin    rtd1295
 *  Realtek Hercules rtd1395
 *  Realtek Thor     rtd1619
 *  Realtek Hank     rtd1319
 *  Realtek Groot    rtd1312c
 * PARAMETER_V1:
 *  Realtek Stark    rtd1619b
 *  Realtek Parker   rtd1319d
 *  Realtek Danvers  rtd1315e
 */
enum parameter_version {
	PARAMETER_V0 = 0,
	PARAMETER_V1 = 1,
};

/* Bit mapping USB_TYPEC_CTRL_CC1_1 and USB_TYPEC_CTRL_CC2_1 */
#define V0_vref_2p6v(val)	((0xf & (val)) << 26) /* Bit 29 for groot */
#define V0_vref_1p23v(val)	((0xf & (val)) << 22)
#define V0_vref_0p8v(val)	((0xf & (val)) << 18)
#define V0_vref_0p66v(val)	((0xf & (val)) << 14)
#define V0_vref_0p4v(val)	((0x7 & (val)) << 11)
#define V0_vref_0p2v(val)	((0x7 & (val)) << 8)
#define V0_vref_1_1p6v(val)	((0xf & (val)) << 4)
#define V0_vref_0_1p6v(val)	((0xf & (val)) << 0)

#define V0_decode_2p6v(val)	(((val) >> 26) & 0xf) /* Bit 29 for groot */
#define V0_decode_1p23v(val)	(((val) >> 22) & 0xf)
#define V0_decode_0p8v(val)	(((val) >> 18) & 0xf)
#define V0_decode_0p66v(val)	(((val) >> 14) & 0xf)
#define V0_decode_0p4v(val)	(((val) >> 11) & 0x7)
#define V0_decode_0p2v(val)	(((val) >> 8) & 0x7)
#define V0_decode_1_1p6v(val)	(((val) >> 4) & 0xf)
#define V0_decode_0_1p6v(val)	(((val) >> 0) & 0xf)

/* new Bit mapping USB_TYPEC_CTRL_CC1_1 and USB_TYPEC_CTRL_CC2_1 */
#define V1_vref_2p6v(val)	((0xf & (val)) << 28)
#define V1_vref_1p23v(val)	((0xf & (val)) << 24)
#define V1_vref_0p8v(val)	((0xf & (val)) << 20)
#define V1_vref_0p66v(val)	((0xf & (val)) << 16)
#define V1_vref_0p4v(val)	((0xf & (val)) << 12)
#define V1_vref_0p2v(val)	((0xf & (val)) << 8)
#define V1_vref_1_1p6v(val)	((0xf & (val)) << 4)
#define V1_vref_0_1p6v(val)	((0xf & (val)) << 0)

#define V1_decode_2p6v(val)	(((val) >> 28) & 0xf)
#define V1_decode_1p23v(val)	(((val) >> 24) & 0xf)
#define V1_decode_0p8v(val)	(((val) >> 20) & 0xf)
#define V1_decode_0p66v(val)	(((val) >> 16) & 0xf)
#define V1_decode_0p4v(val)	(((val) >> 12) & 0xf)
#define V1_decode_0p2v(val)	(((val) >> 8) & 0xf)
#define V1_decode_1_1p6v(val)	(((val) >> 4) & 0xf)
#define V1_decode_0_1p6v(val)	(((val) >> 0) & 0xf)

/* Bit mapping USB_TYPEC_STS */
#define DET_STS		0x7
#define CC1_DET_STS	(DET_STS)
#define CC2_DET_STS	(DET_STS << 3)
#define DET_STS_RA	0x1
#define DET_STS_RD	0x3
#define DET_STS_RP	0x1
#define CC1_DET_STS_RA	(DET_STS_RA)
#define CC1_DET_STS_RD	(DET_STS_RD)
#define CC1_DET_STS_RP	(DET_STS_RP)
#define CC2_DET_STS_RA	(DET_STS_RA << 3)
#define CC2_DET_STS_RD	(DET_STS_RD << 3)
#define CC2_DET_STS_RP	(DET_STS_RP << 3)

/* Bit mapping USB_TYPEC_CTRL */
#define CC2_INT_EN		BIT(11)
#define CC1_INT_EN		BIT(10)
#define CC2_INT_STS		BIT(9)
#define CC1_INT_STS		BIT(8)
#define DEBOUNCE_TIME_MASK	0xff
#define DEBOUNCE_EN		BIT(0)
#define ENABLE_TYPE_C_DETECT	(CC1_INT_EN | CC2_INT_EN)
#define ALL_CC_INT_STS		(CC1_INT_STS | CC2_INT_STS)

/* Parameter */
#define DETECT_TIME 50 /* ms */

static const unsigned int usb_type_c_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

enum usb_data_roles {
	DR_NONE,
	DR_HOST,
	DR_DEVICE,
};

static const struct soc_device_attribute rtk_soc_kylin[] = {
	{ .family = "Realtek Kylin", },
	{ /* empty */ }
};

static int rtd129x_switch_type_c_plug_config(struct type_c_data *type_c,
					     int dr_mode, int cc)
{
	void __iomem *reg = type_c->reg_base + USB_TYPEC_CTRL_CC1_0;
	int val_cc;

#define TYPE_C_EN_SWITCH	BIT(29)
#define TYPE_C_TXRX_SEL		(BIT(28) | BIT(27))
#define TYPE_C_SWITCH_MASK	(TYPE_C_EN_SWITCH | TYPE_C_TXRX_SEL)
#define TYPE_C_ENABLE_CC1	TYPE_C_EN_SWITCH
#define TYPE_C_ENABLE_CC2	(TYPE_C_EN_SWITCH | TYPE_C_TXRX_SEL)
#define TYPE_C_DISABLE_CC	~TYPE_C_SWITCH_MASK

	val_cc = readl(reg);
	val_cc &= ~TYPE_C_SWITCH_MASK;

	if (cc == DISABLE_CC) {
		val_cc &= TYPE_C_DISABLE_CC;
	} else if (cc == ENABLE_CC1) {
		val_cc |= TYPE_C_ENABLE_CC1;
	} else if (cc == ENABLE_CC2) {
		val_cc |= TYPE_C_ENABLE_CC2;
	} else {
		dev_err(type_c->dev, "%s: Error cc setting cc=0x%x\n", __func__, cc);
		return -EINVAL;
	}
	writel(val_cc, reg);

	/* waiting cc stable for enable/disable */
	mdelay(1);

	dev_dbg(type_c->dev, "%s: cc=0x%x val_cc=0x%x usb_typec_ctrl_cc1_0=0x%x\n",
		__func__, cc, val_cc, readl(reg));

	return 0;
}

static inline void switch_type_c_plug_config(struct type_c_data *type_c,
					     int dr_mode, int cc)
{
	int ret = 0;

	if (soc_device_match(rtk_soc_kylin))
		ret = rtd129x_switch_type_c_plug_config(type_c, dr_mode, cc);

	if (ret < 0)
		dev_err(type_c->dev, "%s: Error set type c plug config\n",
			__func__);
}

static void switch_type_c_dr_mode(struct type_c_data *type_c, int dr_mode, int cc)
{
	bool is_host = false;
	bool is_device = false;
	bool polarity = false;
	bool vbus = false;
	bool ss = true;

	switch_type_c_plug_config(type_c, dr_mode, cc);
	if (cc == ENABLE_CC2)
		polarity = true;

	switch (dr_mode) {
	case USB_DR_MODE_HOST:
		is_host = true;
		break;
	case USB_DR_MODE_PERIPHERAL:
		is_device = true;
		vbus = true;
		break;
	default:
		dev_dbg(type_c->dev, "%s dr_mode=%d ==> no host or device\n",
			__func__, dr_mode);
		break;
	}

	dev_dbg(type_c->dev, "%s is_host=%d is_device=%d vbus=%d polarity=%d\n",
		__func__, is_host, is_device, vbus, polarity);

	/* for EXTCON_USB device mode */
	extcon_set_state(type_c->edev, EXTCON_USB, is_device);
	extcon_set_property(type_c->edev, EXTCON_USB,
			    EXTCON_PROP_USB_VBUS,
			    (union extcon_property_value)(int)vbus);
	extcon_set_property(type_c->edev, EXTCON_USB,
			    EXTCON_PROP_USB_TYPEC_POLARITY,
			    (union extcon_property_value)(int)polarity);
	extcon_set_property(type_c->edev, EXTCON_USB,
			    EXTCON_PROP_USB_SS,
			    (union extcon_property_value)(int)ss);

	/* for EXTCON_USB_HOST host mode */
	extcon_set_state(type_c->edev, EXTCON_USB_HOST, is_host);
	extcon_set_property(type_c->edev, EXTCON_USB_HOST,
			    EXTCON_PROP_USB_VBUS,
			    (union extcon_property_value)(int)vbus);
	extcon_set_property(type_c->edev, EXTCON_USB_HOST,
			    EXTCON_PROP_USB_TYPEC_POLARITY,
			    (union extcon_property_value)(int)polarity);
	extcon_set_property(type_c->edev, EXTCON_USB_HOST,
			    EXTCON_PROP_USB_SS,
			    (union extcon_property_value)(int)ss);

	/* sync EXTCON_USB and EXTCON_USB_HOST */
	extcon_sync(type_c->edev, EXTCON_USB);
	extcon_sync(type_c->edev, EXTCON_USB_HOST);

	if (type_c->port) {
		switch (dr_mode) {
		case USB_DR_MODE_HOST:
			typec_set_data_role(type_c->port, TYPEC_HOST);
			typec_set_pwr_role(type_c->port, TYPEC_SOURCE);
			break;
		case USB_DR_MODE_PERIPHERAL:
			typec_set_data_role(type_c->port, TYPEC_DEVICE);
			typec_set_pwr_role(type_c->port, TYPEC_SINK);
			break;
		default:
			dev_dbg(type_c->dev, "%s unknown dr_mode=%d\n",
				__func__, dr_mode);
			break;
		}
	}
}

/* connector attached/detached */
static int connector_attached(struct type_c_data *type_c, u32 cc, int dr_mode)
{
	void __iomem *reg = type_c->reg_base + USB_TYPEC_CTRL;

	cancel_delayed_work(&type_c->delayed_work);

	switch_type_c_dr_mode(type_c, dr_mode, cc);

	writel(ENABLE_TYPE_C_DETECT | readl(reg), reg);

	return 0;
}

static int connector_detached(struct type_c_data *type_c, u32 cc, int dr_mode)
{
	void __iomem *reg = type_c->reg_base + USB_TYPEC_CTRL;

	writel(~ENABLE_TYPE_C_DETECT & readl(reg), reg);

	switch_type_c_dr_mode(type_c, 0, cc);

	schedule_delayed_work(&type_c->delayed_work, msecs_to_jiffies(DETECT_TIME));

	return 0;
}

/* detect host device switch */
static int __detect_host_device(struct type_c_data *type_c, u32 rp_or_rd_en)
{
	struct device *dev = type_c->dev;
	void __iomem *reg_base = type_c->reg_base;
	u32 cc1_config, cc2_config, default_ctrl;
	u32 cc1_switch = 0;

	default_ctrl = readl(reg_base + USB_TYPEC_CTRL) & DEBOUNCE_TIME_MASK;
	writel(default_ctrl, reg_base + USB_TYPEC_CTRL);

	cc1_config = readl(reg_base + USB_TYPEC_CTRL_CC1_0);
	cc2_config = readl(reg_base + USB_TYPEC_CTRL_CC2_0);

	cc1_config &= ~EN_CC_DET;
	cc2_config &= ~EN_CC_DET;
	writel(cc1_config, reg_base + USB_TYPEC_CTRL_CC1_0);
	writel(cc2_config, reg_base + USB_TYPEC_CTRL_CC2_0);

	if (soc_device_match(rtk_soc_kylin))
		cc1_switch = cc1_config & CC_SWITCH_MASK;

	cc1_config &= CC_CODE_MASK;
	cc1_config |= rp_or_rd_en | cc1_switch;
	cc2_config &= CC_CODE_MASK;
	cc2_config |= rp_or_rd_en;
	writel(cc2_config, reg_base + USB_TYPEC_CTRL_CC2_0);
	writel(cc1_config, reg_base + USB_TYPEC_CTRL_CC1_0);

	/* For kylin to disable external rd control gpio */
	if (soc_device_match(rtk_soc_kylin)) {
		struct gpio_desc *gpio = type_c->rd_ctrl_gpio_desc;

		if (gpio && gpiod_direction_output(gpio, 1))
			dev_err(dev, "%s ERROR set rd_ctrl_gpio_desc fail\n", __func__);
	}

	cc1_config |= EN_CC_DET;
	cc2_config |= EN_CC_DET;
	writel(cc1_config, reg_base + USB_TYPEC_CTRL_CC1_0);
	writel(cc2_config, reg_base + USB_TYPEC_CTRL_CC2_0);

	return 0;
}

static int detect_device(struct type_c_data *type_c)
{
	return __detect_host_device(type_c, type_c->dfp_mode_rp_en);
}

static int detect_host(struct type_c_data *type_c)
{
	return __detect_host_device(type_c, type_c->ufp_mode_rd_en);
}

static int host_device_switch_detection(struct type_c_data *type_c)
{
	if (type_c->cc_mode == IN_HOST_MODE) {
		type_c->cc_mode = IN_DEVICE_MODE;
		detect_host(type_c);
	} else {
		type_c->cc_mode = IN_HOST_MODE;
		detect_device(type_c);
	}

	return 0;
}

static int detect_type_c_state(struct type_c_data *type_c)
{
	struct device *dev = type_c->dev;
	void __iomem *reg_base = type_c->reg_base;
	u32 int_status, cc_status, cc_status_check;
	unsigned long flags;

	spin_lock_irqsave(&type_c->lock, flags);

	int_status = readl(reg_base + USB_TYPEC_CTRL);
	cc_status = readl(reg_base + USB_TYPEC_STS);

	type_c->connect_change = CONNECT_NO_CHANGE;

	switch (type_c->cc_mode | type_c->is_attach) {
	case IN_HOST_MODE | IN_ATTACH:
		if (((cc_status & CC1_DET_STS) == CC1_DET_STS) && type_c->at_cc1 == AT_CC1) {
			dev_dbg(dev, "IN host mode and cc1 device detach (cc_status=0x%x)",
				cc_status);
			type_c->is_attach = TO_DETACH;
			type_c->connect_change = CONNECT_CHANGE;
		} else if (((cc_status & CC2_DET_STS) == CC2_DET_STS) &&
			   type_c->at_cc1 == AT_CC2) {
			dev_dbg(dev, "IN host mode and cc2 device detach (cc_status=0x%x)",
				cc_status);
			type_c->is_attach = TO_DETACH;
			type_c->connect_change = CONNECT_CHANGE;
		}
		break;
	case IN_HOST_MODE | IN_DETACH:
		cc_status_check = readl(reg_base + USB_TYPEC_STS);
		if (cc_status_check != (CC1_DET_STS | CC2_DET_STS)) {
			if (in_interrupt()) {
				/* Add delay time to avoid capacitive effect of cable. */
				mdelay(300);
			} else {
				spin_unlock_irqrestore(&type_c->lock, flags);
				/* Add delay time to avoid capacitive effect of cable. */
				msleep(300);
				spin_lock_irqsave(&type_c->lock, flags);
			}
			cc_status_check = readl(reg_base + USB_TYPEC_STS);
		}
		if (cc_status != cc_status_check) {
			dev_warn(dev, "IN_HOST_MODE: cc_status (0x%x) != cc_status_check (0x%x)\n",
				 cc_status, cc_status_check);
			cc_status = readl(reg_base + USB_TYPEC_STS);
		}

		if ((cc_status & CC1_DET_STS) == CC1_DET_STS_RD) {
			dev_dbg(dev, "IN host mode and cc1 device attach (cc_status=0x%x)",
				cc_status);
			type_c->is_attach = TO_ATTACH;
			type_c->at_cc1 = AT_CC1;
			type_c->connect_change = CONNECT_CHANGE;
		} else if ((cc_status & CC2_DET_STS) == CC2_DET_STS_RD) {
			dev_dbg(dev, "In host mode and cc2 device attach (cc_status=0x%x)",
				cc_status);
			type_c->is_attach = TO_ATTACH;
			type_c->at_cc1 = AT_CC2;
			type_c->connect_change = CONNECT_CHANGE;
		}
		break;
	case IN_DEVICE_MODE | IN_ATTACH:
		if ((cc_status & CC1_DET_STS) < CC1_DET_STS_RP ||
		    (cc_status & CC2_DET_STS) < CC2_DET_STS_RP) {
			/* Add a sw debounce to filter cc signal sent from apple pd adapter */
			mdelay(5);
			cc_status_check = readl(reg_base + USB_TYPEC_STS);

			if (cc_status != cc_status_check) {
				dev_dbg(dev, "IN_DEVICE_MODE: cc_status (0x%x) != cc_status_check (0x%x) maybe use a pd adapter\n",
					cc_status, cc_status_check);
				cc_status = cc_status_check;
			}
		}

		if ((cc_status & CC1_DET_STS) < CC1_DET_STS_RP && type_c->at_cc1 == AT_CC1) {
			dev_dbg(dev, "IN device mode and cc1 host disconnect (cc_status=0x%x)",
				cc_status);
			type_c->is_attach = TO_DETACH;
			type_c->connect_change = CONNECT_CHANGE;
		} else if ((cc_status & CC2_DET_STS) < CC2_DET_STS_RP &&
			   type_c->at_cc1 == AT_CC2) {
			dev_dbg(dev, "IN device mode and cc2 host disconnect (cc_status=0x%x)",
				cc_status);
			type_c->is_attach = TO_DETACH;
			type_c->connect_change = CONNECT_CHANGE;
		}
		break;
	case IN_DEVICE_MODE | IN_DETACH:
		cc_status_check = readl(reg_base + USB_TYPEC_STS);
		if (cc_status_check != 0x0) {
			if (in_interrupt()) {
				/* Add delay time to avoid capacitive effect of cable. */
				mdelay(300);
			} else {
				spin_unlock_irqrestore(&type_c->lock, flags);
				/* Add delay time to avoid capacitive effect of cable. */
				msleep(300);
				spin_lock_irqsave(&type_c->lock, flags);
			}
			cc_status_check = readl(reg_base + USB_TYPEC_STS);
		}

		if (cc_status != cc_status_check) {
			dev_warn(dev, "IN_DEVICE_MODE: cc_status (0x%x) != cc_status_check (0x%x)\n",
				 cc_status, cc_status_check);
			cc_status = readl(reg_base + USB_TYPEC_STS);
		}

		if ((cc_status & CC1_DET_STS) >= CC1_DET_STS_RP) {
			dev_dbg(dev, "IN device mode and cc1 host connect (cc_status=0x%x)",
				cc_status);
			type_c->at_cc1 = AT_CC1;
			type_c->is_attach = TO_ATTACH;
			type_c->connect_change = CONNECT_CHANGE;
		} else if ((cc_status & CC2_DET_STS) >= CC2_DET_STS_RP) {
			dev_dbg(dev, "IN device mode and cc2 host connect (cc_status=0x%x)",
				cc_status);
			type_c->at_cc1 = AT_CC2;
			type_c->is_attach = TO_ATTACH;
			type_c->connect_change = CONNECT_CHANGE;
		}
		break;
	default:
		dev_err(dev, "error host or device mode (cc_mode=%d, is_attach=%d) ",
			type_c->cc_mode, type_c->is_attach);
	}

	type_c->int_status = int_status;
	type_c->cc_status = cc_status;

	spin_unlock_irqrestore(&type_c->lock, flags);
	return 0;
}

static void host_device_switch(struct work_struct *work)
{
	struct type_c_data *type_c = container_of(work, struct type_c_data,
						  delayed_work.work);
	struct device *dev = type_c->dev;
	unsigned long flags;
	int connect_change = 0;
	int cc_mode = 0;
	int is_attach = 0;
	int at_cc1 = 0;

	spin_lock_irqsave(&type_c->lock, flags);
	if (type_c->connect_change)
		connect_change = type_c->connect_change;
	spin_unlock_irqrestore(&type_c->lock, flags);

	if (!connect_change)
		detect_type_c_state(type_c);

	spin_lock_irqsave(&type_c->lock, flags);
	if (type_c->connect_change) {
		connect_change = type_c->connect_change;
		cc_mode = type_c->cc_mode;
		is_attach = type_c->is_attach;
		at_cc1 = type_c->at_cc1;
		type_c->connect_change = CONNECT_NO_CHANGE;
	} else {
		host_device_switch_detection(type_c);

		schedule_delayed_work(&type_c->delayed_work, msecs_to_jiffies(DETECT_TIME));
	}
	spin_unlock_irqrestore(&type_c->lock, flags);

	if (!connect_change)
		return;

	dev_dbg(dev, "%s: usb cable connection change\n", __func__);
	if (cc_mode == IN_HOST_MODE) {
		if (is_attach && at_cc1)
			connector_attached(type_c, ENABLE_CC1, USB_DR_MODE_HOST);
		else if (is_attach && !at_cc1)
			connector_attached(type_c, ENABLE_CC2, USB_DR_MODE_HOST);
		else
			connector_detached(type_c, DISABLE_CC, USB_DR_MODE_HOST);
	} else if (cc_mode == IN_DEVICE_MODE) {
		if (is_attach && at_cc1)
			connector_attached(type_c, ENABLE_CC1, USB_DR_MODE_PERIPHERAL);
		else if (is_attach && !at_cc1)
			connector_attached(type_c, ENABLE_CC2, USB_DR_MODE_PERIPHERAL);
		else
			connector_detached(type_c, DISABLE_CC, USB_DR_MODE_PERIPHERAL);
	} else {
		dev_err(dev, "Error: IN unknown mode %d to %s at %s (cc_status=0x%x)\n",
			cc_mode, is_attach ? "attach" : "detach",
			at_cc1 ? "cc1" : "cc2", type_c->cc_status);
	}
	dev_info(dev, "Connection change OK: IN %s mode to %s at %s (cc_status=0x%x)\n",
		 cc_mode == IN_HOST_MODE ? "host" : "device",
		 is_attach ? "attach" : "detach",
		 at_cc1 ? "cc1" : "cc2", type_c->cc_status);
}

static irqreturn_t type_c_detect_irq(int irq, void *__data)
{
	struct type_c_data *type_c = (struct type_c_data *)__data;
	struct device *dev = type_c->dev;
	void __iomem *reg = type_c->reg_base + USB_TYPEC_CTRL;
	unsigned long flags;

	detect_type_c_state(type_c);

	spin_lock_irqsave(&type_c->lock, flags);

	if (type_c->connect_change) {
		dev_dbg(dev, "%s: IN %s mode to %s (at %s interrupt) int_status=0x%x, cc_status=0x%x",
			__func__,
			type_c->cc_mode == IN_HOST_MODE ? "host" : "device",
			type_c->is_attach ? "attach" : "detach",
			type_c->at_cc1 ? "cc1" : "cc2",
			type_c->int_status, type_c->cc_status);

		/* clear interrupt status */
		writel(~ALL_CC_INT_STS & readl(reg), reg);

		cancel_delayed_work(&type_c->delayed_work);
		schedule_delayed_work(&type_c->delayed_work, msecs_to_jiffies(0));
	} else {
		static int local_count;

		/* if no connect_change, we keep the status to avoid status lose */
		if (local_count++ > 10) {
			/* clear interrupt status */
			writel(~ALL_CC_INT_STS & readl(reg), reg);
			local_count = 0;
		}
	}

	spin_unlock_irqrestore(&type_c->lock, flags);

	return IRQ_HANDLED;
}

static int type_c_port_dr_set(struct typec_port *port,
			      enum typec_data_role role)
{
	struct type_c_data *type_c = typec_get_drvdata(port);
	u32 enable_cc;
	unsigned long flags;

	spin_lock_irqsave(&type_c->lock, flags);
	enable_cc = type_c->at_cc1 ? ENABLE_CC1 : ENABLE_CC2;
	spin_unlock_irqrestore(&type_c->lock, flags);

	if (role == TYPEC_HOST)
		switch_type_c_dr_mode(type_c, USB_DR_MODE_HOST, enable_cc);
	else if (role == TYPEC_DEVICE)
		switch_type_c_dr_mode(type_c, USB_DR_MODE_PERIPHERAL, enable_cc);
	else
		switch_type_c_dr_mode(type_c, 0, DISABLE_CC);

	return 0;
}

static const struct typec_operations type_c_port_ops = {
	.dr_set = type_c_port_dr_set,
};

#ifdef CONFIG_DEBUG_FS
static int type_c_parameter_show(struct seq_file *s, void *unused)
{
	struct type_c_data *type_c = s->private;
	struct type_c_cfg *type_c_cfg = type_c->type_c_cfg;
	struct cc_param *cc_param;
	unsigned long flags;

	spin_lock_irqsave(&type_c->lock, flags);

	seq_printf(s, "cc_dfp_mode %s\n",
		   ({ char *tmp;
			switch (type_c_cfg->cc_dfp_mode) {
			case CC_MODE_DFP_USB:
				tmp = "CC_MODE_DFP_USB"; break;
			case CC_MODE_DFP_1_5:
				tmp = "CC_MODE_DFP_1_5"; break;
			case CC_MODE_DFP_3_0:
				tmp = "CC_MODE_DFP_3_0"; break;
			default:
				tmp = "?"; break;
		   } tmp; }));

	seq_printf(s, "dfp_mode_rp_en 0x%x\n", type_c->dfp_mode_rp_en);
	seq_printf(s, "ufp_mode_rd_en 0x%x\n", type_c->ufp_mode_rd_en);
	seq_printf(s, "cc1_code 0x%x\n", type_c->cc1_code);
	seq_printf(s, "cc2_code 0x%x\n", type_c->cc2_code);
	seq_printf(s, "cc1_vref 0x%x\n", type_c->cc1_vref);
	seq_printf(s, "cc2_vref 0x%x\n", type_c->cc2_vref);
	seq_printf(s, "debounce 0x%x\n", type_c->debounce);
	seq_puts(s, "\n");

	cc_param = &type_c_cfg->cc1_param;
	seq_puts(s, "cc1_param:\n");
	seq_printf(s, "  rp_4p7k_code 0x%x\n", cc_param->rp_4p7k_code);
	seq_printf(s, "  rp_36k_code  0x%x\n", cc_param->rp_36k_code);
	seq_printf(s, "  rp_12k_code  0x%x\n", cc_param->rp_12k_code);
	seq_printf(s, "  rd_code      0x%x\n", cc_param->rd_code);
	seq_printf(s, "  vref_2p6v    0x%x\n", cc_param->vref_2p6v);
	seq_printf(s, "  vref_1p23v   0x%x\n", cc_param->vref_1p23v);
	seq_printf(s, "  vref_0p8v    0x%x\n", cc_param->vref_0p8v);
	seq_printf(s, "  vref_0p66v   0x%x\n", cc_param->vref_0p66v);
	seq_printf(s, "  vref_0p4v    0x%x\n", cc_param->vref_0p4v);
	seq_printf(s, "  vref_0p2v    0x%x\n", cc_param->vref_0p2v);
	seq_printf(s, "  vref_1_1p6v  0x%x\n", cc_param->vref_1_1p6v);
	seq_printf(s, "  vref_0_1p6v  0x%x\n", cc_param->vref_0_1p6v);

	cc_param = &type_c_cfg->cc2_param;
	seq_puts(s, "cc2_param:\n");
	seq_printf(s, "  rp_4p7k_code 0x%x\n", cc_param->rp_4p7k_code);
	seq_printf(s, "  rp_36k_code  0x%x\n", cc_param->rp_36k_code);
	seq_printf(s, "  rp_12k_code  0x%x\n", cc_param->rp_12k_code);
	seq_printf(s, "  rd_code      0x%x\n", cc_param->rd_code);
	seq_printf(s, "  vref_2p6v    0x%x\n", cc_param->vref_2p6v);
	seq_printf(s, "  vref_1p23v   0x%x\n", cc_param->vref_1p23v);
	seq_printf(s, "  vref_0p8v    0x%x\n", cc_param->vref_0p8v);
	seq_printf(s, "  vref_0p66v   0x%x\n", cc_param->vref_0p66v);
	seq_printf(s, "  vref_0p4v    0x%x\n", cc_param->vref_0p4v);
	seq_printf(s, "  vref_0p2v    0x%x\n", cc_param->vref_0p2v);
	seq_printf(s, "  vref_1_1p6v  0x%x\n", cc_param->vref_1_1p6v);
	seq_printf(s, "  vref_0_1p6v  0x%x\n", cc_param->vref_0_1p6v);

	spin_unlock_irqrestore(&type_c->lock, flags);

	return 0;
}

static int type_c_parameter_open(struct inode *inode, struct file *file)
{
	return single_open(file, type_c_parameter_show, inode->i_private);
}

static const struct file_operations type_c_parameter_fops = {
	.open			= type_c_parameter_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int type_c_status_show(struct seq_file *s, void *unused)
{
	struct type_c_data *type_c = s->private;
	unsigned long flags;

	spin_lock_irqsave(&type_c->lock, flags);

	seq_printf(s, "In %s mode %s at %s (cc_status=0x%x)\n",
		   type_c->cc_mode == IN_HOST_MODE ? "host" : "device",
		   type_c->is_attach ? "attach" : "detach",
		   type_c->at_cc1 ? "cc1" : "cc2", type_c->cc_status);

	seq_printf(s, "Read Register (type_c_ctrl_cc1_0=0x%x)\n",
		   readl(type_c->reg_base + 0x0));
	seq_printf(s, "Read Register (type_c_ctrl_cc1_1=0x%x)\n",
		   readl(type_c->reg_base + 0x4));
	seq_printf(s, "Read Register (type_c_ctrl_cc2_0=0x%x)\n",
		   readl(type_c->reg_base + 0x8));
	seq_printf(s, "Read Register (type_c_ctrl_cc2_1=0x%x)\n",
		   readl(type_c->reg_base + 0xc));
	seq_printf(s, "Read Register (type_c_status=0x%x)\n",
		   readl(type_c->reg_base + 0x10));
	seq_printf(s, "Read Register (type_c_ctrl=0x%x)\n",
		   readl(type_c->reg_base + 0x14));

	spin_unlock_irqrestore(&type_c->lock, flags);

	return 0;
}

static int type_c_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, type_c_status_show, inode->i_private);
}

static const struct file_operations type_c_status_fops = {
	.open			= type_c_status_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static inline void create_debug_files(struct type_c_data *type_c)
{
	type_c->debug_dir = debugfs_create_dir("type_c", usb_debug_root);

	debugfs_create_file("parameter", 0444, type_c->debug_dir, type_c,
			    &type_c_parameter_fops);

	debugfs_create_file("status", 0444, type_c->debug_dir, type_c,
			    &type_c_status_fops);
}

static inline void remove_debug_files(struct type_c_data *type_c)
{
	debugfs_remove_recursive(type_c->debug_dir);
}
#else
static inline void create_debug_files(struct type_c_data *type_c) { }
static inline void remove_debug_files(struct type_c_data *type_c) { }
#endif /* CONFIG_DEBUG_FS */

/* Init and probe */

static inline s8 get_value(s8 value)
{
	return (((s8)value & 0x8) ? (-(s8)(0x7 & value)) : ((s8)(value)));
}

static int __updated_type_c_parameter_by_efuse(struct type_c_data *type_c)
{
	struct type_c_cfg *type_c_cfg = type_c->type_c_cfg;
	struct cc_param *cc_param;
	struct nvmem_cell *cell;
	s8 cc1_4p7k = 0;
	s8 cc1_12k = 0;
	s8 cc1_0p2v = 0;
	s8 cc1_0p8v = 0;
	s8 cc1_2p6v = 0;
	s8 cc1_0p66v = 0;
	s8 cc1_1p23v = 0;
	s8 cc2_4p7k = 0;
	s8 cc2_12k = 0;
	s8 cc2_0p2v = 0;
	s8 cc2_0p8v = 0;
	s8 cc2_2p6v = 0;
	s8 cc2_0p66v = 0;
	s8 cc2_1p23v = 0;

	cell = nvmem_cell_get(type_c->dev, "usb-cal");
	if (IS_ERR(cell)) {
		dev_warn(type_c->dev, "%s failed to get usb-cal: %ld\n",
			 __func__, PTR_ERR(cell));
	} else {
		unsigned char *buf;
		size_t buf_size;
		int value_size = 4;
		int value_mask = (BIT(value_size) - 1);

		buf = nvmem_cell_read(cell, &buf_size);
		if (!IS_ERR(buf)) {
			cc1_0p2v = get_value((buf[0] >> value_size * 0) & value_mask);
			cc1_0p8v = get_value((buf[0] >> value_size * 1) & value_mask);
			cc1_2p6v = get_value((buf[1] >> value_size * 0) & value_mask);
			cc1_0p66v = get_value((buf[1] >> value_size * 1) & value_mask);
			cc1_1p23v = get_value((buf[2] >> value_size * 0) & value_mask);

			cc2_0p2v = get_value((buf[3] >> value_size * 0) & value_mask);
			cc2_0p8v = get_value((buf[3] >> value_size * 1) & value_mask);
			cc2_2p6v = get_value((buf[4] >> value_size * 0) & value_mask);
			cc2_0p66v = get_value((buf[4] >> value_size * 1) & value_mask);
			cc2_1p23v = get_value((buf[5] >> value_size * 0) & value_mask);

			cc1_4p7k = get_value((buf[6] >> value_size * 0) & value_mask);
			cc1_12k = get_value((buf[6] >> value_size * 1) & value_mask);
			cc2_4p7k = get_value((buf[7] >> value_size * 0) & value_mask);
			cc2_12k = get_value((buf[7] >> value_size * 1) & value_mask);

			kfree(buf);
		}
		nvmem_cell_put(cell);
	}

	dev_dbg(type_c->dev, "check efuse cc1_4p7k=%d cc1_12k=%d cc2_4p7k=%d cc2_12k=%d\n",
		cc1_4p7k, cc1_12k, cc2_4p7k, cc2_12k);
	dev_dbg(type_c->dev, "check efuse cc1_0p2v=%d cc1_0p8v=%d cc1_2p6v=%d cc1_0p66v=%d cc1_1p23v=%d\n",
		cc1_0p2v, cc1_0p8v, cc1_2p6v, cc1_0p66v, cc1_1p23v);
	dev_dbg(type_c->dev, "check efuse cc2_0p2v=%d cc2_0p8v=%d cc2_2p6v=%d cc2_0p66v=%d cc2_1p23v=%d\n",
		cc2_0p2v, cc2_0p8v, cc2_2p6v, cc2_0p66v, cc2_1p23v);

	cc_param = &type_c_cfg->cc1_param;
	cc_param->rp_4p7k_code = cc_param->rp_4p7k_code + cc1_4p7k;
	cc_param->rp_12k_code = cc_param->rp_12k_code + cc1_12k;

	cc_param->vref_1p23v = cc_param->vref_1p23v + cc1_1p23v;
	cc_param->vref_0p66v = cc_param->vref_0p66v + cc1_0p66v;
	cc_param->vref_2p6v = cc_param->vref_2p6v + cc1_2p6v;
	cc_param->vref_0p8v = cc_param->vref_0p8v + cc1_0p8v;
	cc_param->vref_0p2v = cc_param->vref_0p2v + cc1_0p2v;

	cc_param = &type_c_cfg->cc2_param;
	cc_param->rp_4p7k_code = cc_param->rp_4p7k_code + cc2_4p7k;
	cc_param->rp_12k_code = cc_param->rp_12k_code + cc2_12k;

	cc_param->vref_1p23v = cc_param->vref_1p23v + cc2_1p23v;
	cc_param->vref_0p66v = cc_param->vref_0p66v + cc2_0p66v;
	cc_param->vref_2p6v = cc_param->vref_2p6v + cc2_2p6v;
	cc_param->vref_0p8v = cc_param->vref_0p8v + cc2_0p8v;
	cc_param->vref_0p2v = cc_param->vref_0p2v + cc2_0p2v;

	return 0;
}

static int __updated_type_c_parameter_by_efuse_v2(struct type_c_data *type_c)
{
	struct type_c_cfg *type_c_cfg = type_c->type_c_cfg;
	struct cc_param *cc_param;
	struct nvmem_cell *cell;
	s8 cc1_4p7k = 0;
	s8 cc1_12k = 0;
	s8 cc1_0p2v = 0;
	s8 cc1_0p8v = 0;
	s8 cc1_2p6v = 0;
	s8 cc1_0p66v = 0;
	s8 cc1_1p23v = 0;
	s8 cc2_4p7k = 0;
	s8 cc2_12k = 0;
	s8 cc2_0p2v = 0;
	s8 cc2_0p8v = 0;
	s8 cc2_2p6v = 0;
	s8 cc2_0p66v = 0;
	s8 cc2_1p23v = 0;

	cell = nvmem_cell_get(type_c->dev, "usb-type-c-cal");
	if (IS_ERR(cell)) {
		dev_warn(type_c->dev, "%s failed to get usb-type-c-cal: %ld\n",
			 __func__, PTR_ERR(cell));
	} else {
		unsigned char *buf;
		size_t buf_size;
		int value_size = 0;
		int value_mask = (BIT(value_size) - 1);

		buf = nvmem_cell_read(cell, &buf_size);
		if (!IS_ERR(buf)) {
			value_size = 5;
			value_mask = (BIT(value_size) - 1);
			cc1_4p7k = buf[0] & value_mask;
			cc1_12k = buf[1] & value_mask;
			cc2_4p7k = buf[2] & value_mask;
			cc2_12k = buf[3] & value_mask;

			value_size = 4;
			value_mask = (BIT(value_size) - 1);
			cc1_0p2v = (buf[4] >> value_size * 0) & value_mask;
			cc1_0p66v = (buf[4] >> value_size * 1) & value_mask;
			cc1_0p8v = (buf[5] >> value_size * 0) & value_mask;
			cc1_1p23v = (buf[5] >> value_size * 1) & value_mask;
			cc1_2p6v = (buf[6] >> value_size * 0) & value_mask;

			cc2_0p2v = (buf[6] >> value_size * 1) & value_mask;
			cc2_0p66v = (buf[7] >> value_size * 0) & value_mask;
			cc2_0p8v = (buf[7] >> value_size * 1) & value_mask;
			cc2_1p23v = (buf[8] >> value_size * 0) & value_mask;
			cc2_2p6v = (buf[8] >> value_size * 1) & value_mask;

			kfree(buf);
		}
		nvmem_cell_put(cell);
	}

	dev_dbg(type_c->dev, "check efuse v2 cc1_4p7k=%d cc1_12k=%d cc2_4p7k=%d cc2_12k=%d\n",
		cc1_4p7k, cc1_12k, cc2_4p7k, cc2_12k);
	dev_dbg(type_c->dev, "check efuse v2 cc1_0p2v=%d cc1_0p8v=%d cc1_2p6v=%d cc1_0p66v=%d cc1_1p23v=%d\n",
		cc1_0p2v, cc1_0p8v, cc1_2p6v, cc1_0p66v, cc1_1p23v);
	dev_dbg(type_c->dev, "check efuse v2 cc2_0p2v=%d cc2_0p8v=%d cc2_2p6v=%d cc2_0p66v=%d cc2_1p23v=%d\n",
		cc2_0p2v, cc2_0p8v, cc2_2p6v, cc2_0p66v, cc2_1p23v);

	cc_param = &type_c_cfg->cc1_param;
	if (cc1_4p7k)
		cc_param->rp_4p7k_code = cc1_4p7k;
	if (cc1_12k)
		cc_param->rp_12k_code = cc1_12k;

	if (cc1_1p23v)
		cc_param->vref_1p23v = cc1_1p23v;
	if (cc1_0p66v)
		cc_param->vref_0p66v = cc1_0p66v;
	if (cc1_2p6v)
		cc_param->vref_2p6v = cc1_2p6v;
	if (cc1_0p8v)
		cc_param->vref_0p8v = cc1_0p8v;
	if (cc1_0p2v)
		cc_param->vref_0p2v = cc1_0p2v;

	cc_param = &type_c_cfg->cc2_param;
	if (cc2_4p7k)
		cc_param->rp_4p7k_code = cc2_4p7k;
	if (cc2_12k)
		cc_param->rp_12k_code = cc2_12k;

	if (cc2_1p23v)
		cc_param->vref_1p23v = cc2_1p23v;
	if (cc2_0p66v)
		cc_param->vref_0p66v = cc2_0p66v;
	if (cc2_2p6v)
		cc_param->vref_2p6v = cc2_2p6v;
	if (cc2_0p8v)
		cc_param->vref_0p8v = cc2_0p8v;
	if (cc2_0p2v)
		cc_param->vref_0p2v = cc2_0p2v;

	return 0;
}

static void get_default_type_c_parameter(struct type_c_data *type_c)
{
	void __iomem *reg;
	int val;

	type_c->dfp_mode_rp_en = dfp_mode(CC_MODE_DFP_3_0) | EN_RP4P7K;
	type_c->ufp_mode_rd_en = EN_RD;

	reg = type_c->reg_base + USB_TYPEC_CTRL_CC1_0;
	val = readl(reg);
	type_c->cc1_code = CC_CODE_MASK & val;

	reg = type_c->reg_base + USB_TYPEC_CTRL_CC2_0;
	val = readl(reg);
	type_c->cc2_code = CC_CODE_MASK & val;

	reg = type_c->reg_base + USB_TYPEC_CTRL_CC1_1;
	val = readl(reg);
	type_c->cc1_vref = val;

	reg = type_c->reg_base + USB_TYPEC_CTRL_CC2_1;
	val = readl(reg);
	type_c->cc2_vref = val;

	reg = type_c->reg_base + USB_TYPEC_CTRL;
	val = readl(reg);
	type_c->debounce = DEBOUNCE_TIME_MASK & val;
}

static int setup_type_c_parameter(struct type_c_data *type_c)
{
	struct type_c_cfg *type_c_cfg = type_c->type_c_cfg;
	struct cc_param *cc_param;
	struct soc_device_attribute rtk_soc_efuse_v1[] = {
			{ .family = "Realtek Phoenix",},
			{ .family = "Realtek Kylin",},
			{ .family = "Realtek Hercules",},
			{ .family = "Realtek Thor",},
			{ .family = "Realtek Hank",},
			{ .family = "Realtek Groot",},
			{ .family = "Realtek Stark",},
			{ .family = "Realtek Parker",},
			{ /* empty */ }
		};

	if (type_c_cfg->use_defalut_parameter) {
		get_default_type_c_parameter(type_c);
		return 0;
	}

	if (soc_device_match(rtk_soc_efuse_v1))
		__updated_type_c_parameter_by_efuse(type_c);
	else
		__updated_type_c_parameter_by_efuse_v2(type_c);

	/*
	 * UFP     rd     vref_ufp    : 1p23v,  0p66v, 0p2v
	 * DFP_USB rp36k  vref_dfp_usb: 0_1p6v, 0p2v,  unused
	 * DFP_1.5 rp12k  vref_dfp_1_5: 1_1p6v, 0p4v,  0p2v
	 * DFP_3.0 rp4p7k vref_dfp_3_0: 2p6v,   0p8v,  0p2v
	 */

	switch (type_c_cfg->cc_dfp_mode) {
	case CC_MODE_DFP_USB:
		type_c->dfp_mode_rp_en = dfp_mode(CC_MODE_DFP_USB) | EN_RP36K;
		break;
	case CC_MODE_DFP_1_5:
		type_c->dfp_mode_rp_en = dfp_mode(CC_MODE_DFP_1_5) | EN_RP12K;
		break;
	case CC_MODE_DFP_3_0:
		type_c->dfp_mode_rp_en = dfp_mode(CC_MODE_DFP_3_0) | EN_RP4P7K;
		break;
	default:
		dev_err(type_c->dev, "%s: unknown cc_dfp_mode %d\n",
			__func__, type_c_cfg->cc_dfp_mode);
	}

	type_c->ufp_mode_rd_en = EN_RD;

	cc_param = &type_c_cfg->cc1_param;
	type_c->cc1_code = rp4pk_code(cc_param->rp_4p7k_code) |
			   rp36k_code(cc_param->rp_36k_code) |
			   rp12k_code(cc_param->rp_12k_code) |
			   rd_code(cc_param->rd_code);

	if (type_c_cfg->parameter_ver == PARAMETER_V0)
		type_c->cc1_vref = V0_vref_2p6v(cc_param->vref_2p6v) |
				   V0_vref_1p23v(cc_param->vref_1p23v) |
				   V0_vref_0p8v(cc_param->vref_0p8v) |
				   V0_vref_0p66v(cc_param->vref_0p66v) |
				   V0_vref_0p4v(cc_param->vref_0p4v) |
				   V0_vref_0p2v(cc_param->vref_0p2v) |
				   V0_vref_1_1p6v(cc_param->vref_1_1p6v) |
				   V0_vref_0_1p6v(cc_param->vref_0_1p6v);
	else if (type_c_cfg->parameter_ver == PARAMETER_V1)
		type_c->cc1_vref = V1_vref_2p6v(cc_param->vref_2p6v) |
				   V1_vref_1p23v(cc_param->vref_1p23v) |
				   V1_vref_0p8v(cc_param->vref_0p8v) |
				   V1_vref_0p66v(cc_param->vref_0p66v) |
				   V1_vref_0p4v(cc_param->vref_0p4v) |
				   V1_vref_0p2v(cc_param->vref_0p2v) |
				   V1_vref_1_1p6v(cc_param->vref_1_1p6v) |
				   V1_vref_0_1p6v(cc_param->vref_0_1p6v);
	else
		dev_err(type_c->dev, "%s: unknown parameter_ver %d\n",
			__func__, type_c_cfg->parameter_ver);

	cc_param = &type_c_cfg->cc2_param;
	type_c->cc2_code = rp4pk_code(cc_param->rp_4p7k_code)
			 | rp36k_code(cc_param->rp_36k_code)
			 | rp12k_code(cc_param->rp_12k_code)
			 | rd_code(cc_param->rd_code);

	if (type_c_cfg->parameter_ver == PARAMETER_V0)
		type_c->cc2_vref = V0_vref_2p6v(cc_param->vref_2p6v) |
				   V0_vref_1p23v(cc_param->vref_1p23v) |
				   V0_vref_0p8v(cc_param->vref_0p8v) |
				   V0_vref_0p66v(cc_param->vref_0p66v) |
				   V0_vref_0p4v(cc_param->vref_0p4v) |
				   V0_vref_0p2v(cc_param->vref_0p2v) |
				   V0_vref_1_1p6v(cc_param->vref_1_1p6v) |
				   V0_vref_0_1p6v(cc_param->vref_0_1p6v);
	else if (type_c_cfg->parameter_ver == PARAMETER_V1)
		type_c->cc2_vref = V1_vref_2p6v(cc_param->vref_2p6v) |
				   V1_vref_1p23v(cc_param->vref_1p23v) |
				   V1_vref_0p8v(cc_param->vref_0p8v) |
				   V1_vref_0p66v(cc_param->vref_0p66v) |
				   V1_vref_0p4v(cc_param->vref_0p4v) |
				   V1_vref_0p2v(cc_param->vref_0p2v) |
				   V1_vref_1_1p6v(cc_param->vref_1_1p6v) |
				   V1_vref_0_1p6v(cc_param->vref_0_1p6v);
	else
		dev_err(type_c->dev, "%s: unknown parameter_ver %d\n",
			__func__, type_c_cfg->parameter_ver);

	type_c->debounce = (type_c_cfg->debounce_val << 1) | DEBOUNCE_EN;

	return 0;
}

static int extcon_rtk_type_c_init(struct type_c_data *type_c)
{
	struct device *dev = type_c->dev;
	unsigned long flags;
	void __iomem *reg;
	int val;

	spin_lock_irqsave(&type_c->lock, flags);

	/* set parameter */
	reg = type_c->reg_base + USB_TYPEC_CTRL_CC1_0;
	val = readl(reg);
	val = (~CC_CODE_MASK & val) | (type_c->cc1_code & CC_CODE_MASK);
	writel(val, reg);

	reg = type_c->reg_base + USB_TYPEC_CTRL_CC2_0;
	val = readl(reg);
	val = (~CC_CODE_MASK & val) | (type_c->cc2_code & CC_CODE_MASK);

	reg = type_c->reg_base + USB_TYPEC_CTRL_CC1_1;
	writel(type_c->cc1_vref, reg);

	reg = type_c->reg_base + USB_TYPEC_CTRL_CC2_1;
	writel(type_c->cc2_vref, reg);

	reg = type_c->reg_base + USB_TYPEC_CTRL;
	val = readl(reg);
	val = (~DEBOUNCE_TIME_MASK & val) | (type_c->debounce & DEBOUNCE_TIME_MASK);

	dev_info(dev, "First check USB_DR_MODE_PERIPHERAL");
	type_c->cc_mode = IN_DEVICE_MODE;
	type_c->is_attach = IN_DETACH;
	type_c->connect_change = CONNECT_NO_CHANGE;

	detect_host(type_c);

	spin_unlock_irqrestore(&type_c->lock, flags);

	schedule_delayed_work(&type_c->delayed_work, msecs_to_jiffies(0));

	if (!type_c->port) {
		struct typec_capability typec_cap = { };
		struct fwnode_handle *fwnode;
		const char *buf;
		int ret;

		typec_cap.revision = USB_TYPEC_REV_1_0;
		typec_cap.prefer_role = TYPEC_NO_PREFERRED_ROLE;
		typec_cap.driver_data = type_c;
		typec_cap.ops = &type_c_port_ops;

		fwnode = device_get_named_child_node(dev, "connector");
		if (!fwnode)
			return -EINVAL;

		ret = fwnode_property_read_string(fwnode, "power-role", &buf);
		if (ret) {
			dev_err(dev, "power-role not found: %d\n", ret);
			return ret;
		}

		ret = typec_find_port_power_role(buf);
		if (ret < 0)
			return ret;
		typec_cap.type = ret;

		ret = fwnode_property_read_string(fwnode, "data-role", &buf);
		if (ret) {
			dev_err(dev, "data-role not found: %d\n", ret);
			return ret;
		}

		ret = typec_find_port_data_role(buf);
		if (ret < 0)
			return ret;
		typec_cap.data = ret;

		type_c->port = typec_register_port(type_c->dev, &typec_cap);
		if (IS_ERR(type_c->port))
			return PTR_ERR(type_c->port);
	}

	return 0;
}

static int extcon_rtk_type_c_edev_register(struct type_c_data *type_c)
{
	struct device *dev = type_c->dev;
	int ret = 0;

	type_c->edev = devm_extcon_dev_allocate(dev, usb_type_c_cable);
	if (IS_ERR(type_c->edev)) {
		dev_err(dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	ret = devm_extcon_dev_register(dev, type_c->edev);
	if (ret < 0) {
		dev_err(dev, "failed to register extcon device\n");
		return ret;
	}

	extcon_set_property_capability(type_c->edev, EXTCON_USB,
				       EXTCON_PROP_USB_VBUS);
	extcon_set_property_capability(type_c->edev, EXTCON_USB,
				       EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(type_c->edev, EXTCON_USB,
				       EXTCON_PROP_USB_SS);

	extcon_set_property_capability(type_c->edev, EXTCON_USB_HOST,
				       EXTCON_PROP_USB_VBUS);
	extcon_set_property_capability(type_c->edev, EXTCON_USB_HOST,
				       EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(type_c->edev, EXTCON_USB_HOST,
				       EXTCON_PROP_USB_SS);

	return ret;
}

static int extcon_rtk_type_c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct type_c_data *type_c;
	const struct type_c_cfg *type_c_cfg;
	int ret = 0;

	type_c = devm_kzalloc(dev, sizeof(*type_c), GFP_KERNEL);
	if (!type_c)
		return -ENOMEM;

	type_c->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(type_c->reg_base))
		return PTR_ERR(type_c->reg_base);

	type_c->dev = dev;

	type_c->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (type_c->irq <= 0) {
		dev_err(&pdev->dev, "Type C driver with no IRQ. Check %s setup!\n",
			dev_name(&pdev->dev));
		ret = -ENODEV;
		goto err;
	}

	ret = devm_request_irq(dev, type_c->irq, type_c_detect_irq,
			       IRQF_SHARED, "type_c_detect", type_c);

	spin_lock_init(&type_c->lock);

	type_c->rd_ctrl_gpio_desc = NULL;
	if (soc_device_match(rtk_soc_kylin)) {
		struct gpio_desc *gpio;

		gpio = fwnode_gpiod_get_index(of_fwnode_handle(dev->of_node),
					      "realtek,rd-ctrl-gpios",
					      0, GPIOD_OUT_HIGH, "rd-ctrl-gpio");
		if (IS_ERR(gpio)) {
			dev_err(dev, "Error rd_ctrl-gpios no found (err=%d)\n",
				(int)PTR_ERR(gpio));
		} else {
			type_c->rd_ctrl_gpio_desc = gpio;
			dev_dbg(dev, "%s get rd-ctrl-gpios (id=%d) OK\n",
				__func__, desc_to_gpio(gpio));
		}
	}

	type_c_cfg = of_device_get_match_data(dev);
	if (!type_c_cfg) {
		dev_err(dev, "type_c config are not assigned!\n");
		ret = -EINVAL;
		goto err;
	}

	type_c->type_c_cfg = devm_kzalloc(dev, sizeof(*type_c_cfg), GFP_KERNEL);
	if (!type_c->type_c_cfg)
		return -ENOMEM;

	memcpy(type_c->type_c_cfg, type_c_cfg, sizeof(*type_c_cfg));

	if (setup_type_c_parameter(type_c)) {
		dev_err(dev, "ERROR: %s to setup type c parameter!!", __func__);
		ret = -EINVAL;
		goto err;
	}

	INIT_DELAYED_WORK(&type_c->delayed_work, host_device_switch);

	ret = extcon_rtk_type_c_init(type_c);
	if (ret) {
		dev_err(dev, "%s failed to init type_c\n", __func__);
		goto err;
	}

	platform_set_drvdata(pdev, type_c);

	ret = extcon_rtk_type_c_edev_register(type_c);

	create_debug_files(type_c);

	return 0;

err:
	dev_err(&pdev->dev, "%s: Probe fail, %d\n", __func__, ret);

	return ret;
}

static void extcon_rtk_type_c_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct type_c_data *type_c = dev_get_drvdata(dev);
	u32 default_ctrl;
	unsigned long flags;

	remove_debug_files(type_c);

	if (type_c->port) {
		typec_unregister_port(type_c->port);
		type_c->port = NULL;
	}

	cancel_delayed_work_sync(&type_c->delayed_work);
	flush_delayed_work(&type_c->delayed_work);
	WARN_ON_ONCE(delayed_work_pending(&type_c->delayed_work));

	spin_lock_irqsave(&type_c->lock, flags);
	/* disable interrupt */
	default_ctrl = readl(type_c->reg_base + USB_TYPEC_CTRL) &
		    DEBOUNCE_TIME_MASK;
	writel(default_ctrl, type_c->reg_base + USB_TYPEC_CTRL);

	/* disable cc detect, rp, rd */
	writel(PLR_EN, type_c->reg_base + USB_TYPEC_CTRL_CC1_0);
	writel(0, type_c->reg_base + USB_TYPEC_CTRL_CC2_0);

	spin_unlock_irqrestore(&type_c->lock, flags);

	if (type_c->rd_ctrl_gpio_desc)
		gpiod_put(type_c->rd_ctrl_gpio_desc);
	type_c->rd_ctrl_gpio_desc = NULL;

	free_irq(type_c->irq, type_c);
}

static const struct type_c_cfg rtd1295_type_c_cfg = {
	.parameter_ver = PARAMETER_V0,
	.cc_dfp_mode = CC_MODE_DFP_3_0,
	.cc1_param = { .rp_4p7k_code = 0xb,
		       .rp_36k_code = 0x17,
		       .rp_12k_code = 0x10,
		       .rd_code = 0,
		       .ra_code = 0,
		       .vref_2p6v = 0x0,
		       .vref_1p23v = 0x0,
		       .vref_0p8v = 0x3,
		       .vref_0p66v = 0x0,
		       .vref_0p4v = 0x0,
		       .vref_0p2v = 0x4,
		       .vref_1_1p6v = 0,
		       .vref_0_1p6v = 0 },
	.cc2_param = { .rp_4p7k_code = 0xc,
		       .rp_36k_code = 0x17,
		       .rp_12k_code = 0x12,
		       .rd_code = 0,
		       .ra_code = 0,
		       .vref_2p6v = 0x2,
		       .vref_1p23v = 0x0,
		       .vref_0p8v = 0x3,
		       .vref_0p66v = 0x0,
		       .vref_0p4v = 0x0,
		       .vref_0p2v = 0x5,
		       .vref_1_1p6v = 0,
		       .vref_0_1p6v = 0 },
	.debounce_val = 0x7f, /* 1b,1us 7f,4.7us */
	.use_defalut_parameter = false,
};

static const struct type_c_cfg rtd1395_type_c_cfg = {
	.parameter_ver = PARAMETER_V0,
	.cc_dfp_mode = CC_MODE_DFP_3_0,
	.cc1_param = { .rp_4p7k_code = 0xc,
		       .rp_36k_code = 0xb,
		       .rp_12k_code = 0xe,
		       .rd_code = 0x10,
		       .ra_code = 0x0,
		       .vref_2p6v = 0x0,
		       .vref_1p23v = 0x1,
		       .vref_0p8v = 0x0,
		       .vref_0p66v = 0x0,
		       .vref_0p4v = 0x3,
		       .vref_0p2v = 0x0,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.cc2_param = { .rp_4p7k_code = 0xb,
		       .rp_36k_code = 0x9,
		       .rp_12k_code = 0xe,
		       .rd_code = 0xf,
		       .ra_code = 0x0,
		       .vref_2p6v = 0x1,
		       .vref_1p23v = 0x3,
		       .vref_0p8v = 0x3,
		       .vref_0p66v = 0x2,
		       .vref_0p4v = 0x3,
		       .vref_0p2v = 0x2,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.debounce_val = 0x7f, /* 1b,1us 7f,4.7us */
	.use_defalut_parameter = false,
};

static const struct type_c_cfg rtd1619_type_c_cfg = {
	.parameter_ver = PARAMETER_V0,
	.cc_dfp_mode = CC_MODE_DFP_3_0,
	.cc1_param = { .rp_4p7k_code = 0xc,
		       .rp_36k_code = 0xf,
		       .rp_12k_code = 0xe,
		       .rd_code = 0x11,
		       .ra_code = 0x0,
		       .vref_2p6v = 0x5,
		       .vref_1p23v = 0x7,
		       .vref_0p8v = 0xa,
		       .vref_0p66v = 0xa,
		       .vref_0p4v = 0x3,
		       .vref_0p2v = 0x2,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.cc2_param = { .rp_4p7k_code = 0xc,
		       .rp_36k_code = 0xf,
		       .rp_12k_code = 0xe,
		       .rd_code = 0xf,
		       .ra_code = 0x0,
		       .vref_2p6v = 0x5,
		       .vref_1p23v = 0x8,
		       .vref_0p8v = 0xa,
		       .vref_0p66v = 0xa,
		       .vref_0p4v = 0x3,
		       .vref_0p2v = 0x2,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.debounce_val = 0x7f, /* 1b,1us 7f,4.7us */
	.use_defalut_parameter = false,
};

static const struct type_c_cfg rtd1319_type_c_cfg = {
	.parameter_ver = PARAMETER_V0,
	.cc_dfp_mode = CC_MODE_DFP_1_5,
	.cc1_param = { .rp_4p7k_code = 0x9,
		       .rp_36k_code = 0xe,
		       .rp_12k_code = 0x9,
		       .rd_code = 0x9,
		       .ra_code = 0x7,
		       .vref_2p6v = 0x3,
		       .vref_1p23v = 0x7,
		       .vref_0p8v = 0x7,
		       .vref_0p66v = 0x6,
		       .vref_0p4v = 0x2,
		       .vref_0p2v = 0x3,
		       .vref_1_1p6v = 0x4,
		       .vref_0_1p6v = 0x7 },
	.cc2_param = { .rp_4p7k_code = 0x8,
		       .rp_36k_code = 0xe,
		       .rp_12k_code = 0x9,
		       .rd_code = 0x9,
		       .ra_code = 0x7,
		       .vref_2p6v = 0x3,
		       .vref_1p23v = 0x7,
		       .vref_0p8v = 0x7,
		       .vref_0p66v = 0x6,
		       .vref_0p4v = 0x3,
		       .vref_0p2v = 0x3,
		       .vref_1_1p6v = 0x6,
		       .vref_0_1p6v = 0x7 },
	.debounce_val = 0x7f, /* 1b,1us 7f,4.7us */
	.use_defalut_parameter = false,
};

static const struct type_c_cfg rtd1312c_type_c_cfg = {
	.parameter_ver = PARAMETER_V0,
	.cc_dfp_mode = CC_MODE_DFP_1_5,
	.cc1_param = { .rp_4p7k_code = 0xe,
		       .rp_36k_code = 0xc,
		       .rp_12k_code = 0xc,
		       .rd_code = 0xa,
		       .ra_code = 0x3,
		       .vref_2p6v = 0xa,
		       .vref_1p23v = 0x7,
		       .vref_0p8v = 0x7,
		       .vref_0p66v = 0x7,
		       .vref_0p4v = 0x4,
		       .vref_0p2v = 0x4,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.cc2_param = { .rp_4p7k_code = 0xe,
		       .rp_36k_code = 0xc,
		       .rp_12k_code = 0xc,
		       .rd_code = 0xa,
		       .ra_code = 0x3,
		       .vref_2p6v = 0xa,
		       .vref_1p23v = 0x7,
		       .vref_0p8v = 0x7,
		       .vref_0p66v = 0x7,
		       .vref_0p4v = 0x4,
		       .vref_0p2v = 0x4,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.debounce_val = 0x7f, /* 1b,1us 7f,4.7us */
	.use_defalut_parameter = false,
};

static const struct type_c_cfg rtd1619b_type_c_cfg = {
	.parameter_ver = PARAMETER_V1,
	.cc_dfp_mode = CC_MODE_DFP_1_5,
	.cc1_param = { .rp_4p7k_code = 0xf,
		       .rp_36k_code = 0xf,
		       .rp_12k_code = 0xf,
		       .rd_code = 0xf,
		       .ra_code = 0x7,
		       .vref_2p6v = 0x9,
		       .vref_1p23v = 0x7,
		       .vref_0p8v = 0x9,
		       .vref_0p66v = 0x8,
		       .vref_0p4v = 0x7,
		       .vref_0p2v = 0x9,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.cc2_param = { .rp_4p7k_code = 0xf,
		       .rp_36k_code = 0xf,
		       .rp_12k_code = 0xf,
		       .rd_code = 0xf,
		       .ra_code = 0x7,
		       .vref_1p23v = 0x7,
		       .vref_0p8v = 0x9,
		       .vref_0p66v = 0x8,
		       .vref_0p4v = 0x7,
		       .vref_0p2v = 0x8,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.debounce_val = 0x7f, /* 1b,1us 7f,4.7us */
	.use_defalut_parameter = false,
};

static const struct type_c_cfg rtd1319d_type_c_cfg = {
	.parameter_ver = PARAMETER_V1,
	.cc_dfp_mode = CC_MODE_DFP_1_5,
	.cc1_param = { .rp_4p7k_code = 0xe,
		       .rp_36k_code = 0x3,
		       .rp_12k_code = 0xe,
		       .rd_code = 0xf,
		       .ra_code = 0x6,
		       .vref_2p6v = 0x7,
		       .vref_1p23v = 0x7,
		       .vref_0p8v = 0x8,
		       .vref_0p66v = 0x7,
		       .vref_0p4v = 0x7,
		       .vref_0p2v = 0x7,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.cc2_param = { .rp_4p7k_code = 0xe,
		       .rp_36k_code = 0x3,
		       .rp_12k_code = 0xe,
		       .rd_code = 0xf,
		       .ra_code = 0x6,
		       .vref_2p6v = 0x7,
		       .vref_1p23v = 0x7,
		       .vref_0p8v = 0x8,
		       .vref_0p66v = 0x7,
		       .vref_0p4v = 0x7,
		       .vref_0p2v = 0x8,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.debounce_val = 0x7f, /* 1b,1us 7f,4.7us */
	.use_defalut_parameter = false,
};

static const struct type_c_cfg rtd1315e_type_c_cfg = {
	.parameter_ver = PARAMETER_V1,
	.cc_dfp_mode = CC_MODE_DFP_1_5,
	.cc1_param = { .rp_4p7k_code = 0xe,
		       .rp_36k_code = 0x3,
		       .rp_12k_code = 0xe,
		       .rd_code = 0xf,
		       .ra_code = 0x6,
		       .vref_2p6v = 0x7,
		       .vref_1p23v = 0x7,
		       .vref_0p8v = 0x8,
		       .vref_0p66v = 0x7,
		       .vref_0p4v = 0x7,
		       .vref_0p2v = 0x7,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.cc2_param = { .rp_4p7k_code = 0xe,
		       .rp_36k_code = 0x3,
		       .rp_12k_code = 0xe,
		       .rd_code = 0xf,
		       .ra_code = 0x6,
		       .vref_2p6v = 0x7,
		       .vref_1p23v = 0x7,
		       .vref_0p8v = 0x8,
		       .vref_0p66v = 0x7,
		       .vref_0p4v = 0x7,
		       .vref_0p2v = 0x8,
		       .vref_1_1p6v = 0x7,
		       .vref_0_1p6v = 0x7 },
	.debounce_val = 0x7f, /* 1b,1us 7f,4.7us */
	.use_defalut_parameter = false,
};

static const struct of_device_id extcon_rtk_type_c_match[] = {
	{ .compatible = "realtek,rtd1295-type-c", .data = &rtd1295_type_c_cfg },
	{ .compatible = "realtek,rtd1312c-type-c", .data = &rtd1312c_type_c_cfg },
	{ .compatible = "realtek,rtd1315e-type-c", .data = &rtd1315e_type_c_cfg },
	{ .compatible = "realtek,rtd1319-type-c", .data = &rtd1319_type_c_cfg },
	{ .compatible = "realtek,rtd1319d-type-c", .data = &rtd1319d_type_c_cfg },
	{ .compatible = "realtek,rtd1395-type-c", .data = &rtd1395_type_c_cfg },
	{ .compatible = "realtek,rtd1619-type-c", .data = &rtd1619_type_c_cfg },
	{ .compatible = "realtek,rtd1619b-type-c", .data = &rtd1619b_type_c_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, extcon_rtk_type_c_match);

#ifdef CONFIG_PM_SLEEP
static int extcon_rtk_type_c_prepare(struct device *dev)
{
	struct type_c_data *type_c = dev_get_drvdata(dev);
	u32 default_ctrl;
	unsigned long flags;

	cancel_delayed_work_sync(&type_c->delayed_work);
	flush_delayed_work(&type_c->delayed_work);
	WARN_ON_ONCE(delayed_work_pending(&type_c->delayed_work));

	spin_lock_irqsave(&type_c->lock, flags);
	/* disable interrupt */
	default_ctrl = readl(type_c->reg_base + USB_TYPEC_CTRL) &
		    DEBOUNCE_TIME_MASK;
	writel(default_ctrl, type_c->reg_base + USB_TYPEC_CTRL);

	/* disable cc detect, rp, rd */
	writel(PLR_EN, type_c->reg_base + USB_TYPEC_CTRL_CC1_0);
	writel(0, type_c->reg_base + USB_TYPEC_CTRL_CC2_0);

	spin_unlock_irqrestore(&type_c->lock, flags);

	return 0;
}

static void extcon_rtk_type_c_complete(struct device *dev)
{
	/* nothing */
}

static int extcon_rtk_type_c_suspend(struct device *dev)
{
	/* nothing */

	return 0;
}

static int extcon_rtk_type_c_resume(struct device *dev)
{
	struct type_c_data *type_c = dev_get_drvdata(dev);
	int ret;

	ret = extcon_rtk_type_c_init(type_c);
	if (ret) {
		dev_err(dev, "%s failed to init type_c\n", __func__);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops extcon_rtk_type_c_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(extcon_rtk_type_c_suspend, extcon_rtk_type_c_resume)
	.prepare = extcon_rtk_type_c_prepare,
	.complete = extcon_rtk_type_c_complete,
};

#define DEV_PM_OPS	(&extcon_rtk_type_c_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver extcon_rtk_type_c_driver = {
	.probe		= extcon_rtk_type_c_probe,
	.remove		= extcon_rtk_type_c_remove,
	.driver		= {
		.name	= "extcon-rtk-type_c",
		.of_match_table = extcon_rtk_type_c_match,
		.pm = DEV_PM_OPS,
	},
};

module_platform_driver(extcon_rtk_type_c_driver);

MODULE_DESCRIPTION("Realtek Extcon Type C driver");
MODULE_AUTHOR("Stanley Chang <stanley_chang@realtek.com>");
MODULE_LICENSE("GPL");
