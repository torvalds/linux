// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Nuvoton Technology corporation.

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/of_address.h>

/* NPCM7xx GCR registers */
#define NPCM_MDLR_OFFSET	0x7C
#define NPCM_MDLR_USBD0		BIT(9)
#define NPCM_MDLR_USBD1		BIT(8)
#define NPCM_MDLR_USBD2_4	BIT(21)
#define NPCM_MDLR_USBD5_9	BIT(22)

#define NPCM_USB1PHYCTL_OFFSET	0x140
#define NPCM_USB2PHYCTL_OFFSET	0x144
#define NPCM_USBXPHYCTL_RS	BIT(28)

/* NPCM7xx Reset registers */
#define NPCM_SWRSTR		0x14
#define NPCM_SWRST		BIT(2)

#define NPCM_IPSRST1		0x20
#define NPCM_IPSRST1_USBD1	BIT(5)
#define NPCM_IPSRST1_USBD2	BIT(8)
#define NPCM_IPSRST1_USBD3	BIT(25)
#define NPCM_IPSRST1_USBD4	BIT(22)
#define NPCM_IPSRST1_USBD5	BIT(23)
#define NPCM_IPSRST1_USBD6	BIT(24)

#define NPCM_IPSRST2		0x24
#define NPCM_IPSRST2_USB_HOST	BIT(26)

#define NPCM_IPSRST3		0x34
#define NPCM_IPSRST3_USBD0	BIT(4)
#define NPCM_IPSRST3_USBD7	BIT(5)
#define NPCM_IPSRST3_USBD8	BIT(6)
#define NPCM_IPSRST3_USBD9	BIT(7)
#define NPCM_IPSRST3_USBPHY1	BIT(24)
#define NPCM_IPSRST3_USBPHY2	BIT(25)

#define NPCM_RC_RESETS_PER_REG	32
#define NPCM_MASK_RESETS	GENMASK(4, 0)

struct npcm_rc_data {
	struct reset_controller_dev rcdev;
	struct notifier_block restart_nb;
	u32 sw_reset_number;
	void __iomem *base;
	spinlock_t lock;
};

#define to_rc_data(p) container_of(p, struct npcm_rc_data, rcdev)

static int npcm_rc_restart(struct notifier_block *nb, unsigned long mode,
			   void *cmd)
{
	struct npcm_rc_data *rc = container_of(nb, struct npcm_rc_data,
					       restart_nb);

	writel(NPCM_SWRST << rc->sw_reset_number, rc->base + NPCM_SWRSTR);
	mdelay(1000);

	pr_emerg("%s: unable to restart system\n", __func__);

	return NOTIFY_DONE;
}

static int npcm_rc_setclear_reset(struct reset_controller_dev *rcdev,
				  unsigned long id, bool set)
{
	struct npcm_rc_data *rc = to_rc_data(rcdev);
	unsigned int rst_bit = BIT(id & NPCM_MASK_RESETS);
	unsigned int ctrl_offset = id >> 8;
	unsigned long flags;
	u32 stat;

	spin_lock_irqsave(&rc->lock, flags);
	stat = readl(rc->base + ctrl_offset);
	if (set)
		writel(stat | rst_bit, rc->base + ctrl_offset);
	else
		writel(stat & ~rst_bit, rc->base + ctrl_offset);
	spin_unlock_irqrestore(&rc->lock, flags);

	return 0;
}

static int npcm_rc_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	return npcm_rc_setclear_reset(rcdev, id, true);
}

static int npcm_rc_deassert(struct reset_controller_dev *rcdev,
			    unsigned long id)
{
	return npcm_rc_setclear_reset(rcdev, id, false);
}

static int npcm_rc_status(struct reset_controller_dev *rcdev,
			  unsigned long id)
{
	struct npcm_rc_data *rc = to_rc_data(rcdev);
	unsigned int rst_bit = BIT(id & NPCM_MASK_RESETS);
	unsigned int ctrl_offset = id >> 8;

	return (readl(rc->base + ctrl_offset) & rst_bit);
}

static int npcm_reset_xlate(struct reset_controller_dev *rcdev,
			    const struct of_phandle_args *reset_spec)
{
	unsigned int offset, bit;

	offset = reset_spec->args[0];
	if (offset != NPCM_IPSRST1 && offset != NPCM_IPSRST2 &&
	    offset != NPCM_IPSRST3) {
		dev_err(rcdev->dev, "Error reset register (0x%x)\n", offset);
		return -EINVAL;
	}
	bit = reset_spec->args[1];
	if (bit >= NPCM_RC_RESETS_PER_REG) {
		dev_err(rcdev->dev, "Error reset number (%d)\n", bit);
		return -EINVAL;
	}

	return (offset << 8) | bit;
}

static const struct of_device_id npcm_rc_match[] = {
	{ .compatible = "nuvoton,npcm750-reset",
		.data = (void *)"nuvoton,npcm750-gcr" },
	{ }
};

/*
 *  The following procedure should be observed in USB PHY, USB device and
 *  USB host initialization at BMC boot
 */
static int npcm_usb_reset(struct platform_device *pdev, struct npcm_rc_data *rc)
{
	u32 mdlr, iprst1, iprst2, iprst3;
	struct device *dev = &pdev->dev;
	struct regmap *gcr_regmap;
	u32 ipsrst1_bits = 0;
	u32 ipsrst2_bits = NPCM_IPSRST2_USB_HOST;
	u32 ipsrst3_bits = 0;
	const char *gcr_dt;

	gcr_dt = (const char *)
	of_match_device(dev->driver->of_match_table, dev)->data;

	gcr_regmap = syscon_regmap_lookup_by_compatible(gcr_dt);
	if (IS_ERR(gcr_regmap)) {
		dev_err(&pdev->dev, "Failed to find %s\n", gcr_dt);
		return PTR_ERR(gcr_regmap);
	}

	/* checking which USB device is enabled */
	regmap_read(gcr_regmap, NPCM_MDLR_OFFSET, &mdlr);
	if (!(mdlr & NPCM_MDLR_USBD0))
		ipsrst3_bits |= NPCM_IPSRST3_USBD0;
	if (!(mdlr & NPCM_MDLR_USBD1))
		ipsrst1_bits |= NPCM_IPSRST1_USBD1;
	if (!(mdlr & NPCM_MDLR_USBD2_4))
		ipsrst1_bits |= (NPCM_IPSRST1_USBD2 |
				 NPCM_IPSRST1_USBD3 |
				 NPCM_IPSRST1_USBD4);
	if (!(mdlr & NPCM_MDLR_USBD0)) {
		ipsrst1_bits |= (NPCM_IPSRST1_USBD5 |
				 NPCM_IPSRST1_USBD6);
		ipsrst3_bits |= (NPCM_IPSRST3_USBD7 |
				 NPCM_IPSRST3_USBD8 |
				 NPCM_IPSRST3_USBD9);
	}

	/* assert reset USB PHY and USB devices */
	iprst1 = readl(rc->base + NPCM_IPSRST1);
	iprst2 = readl(rc->base + NPCM_IPSRST2);
	iprst3 = readl(rc->base + NPCM_IPSRST3);

	iprst1 |= ipsrst1_bits;
	iprst2 |= ipsrst2_bits;
	iprst3 |= (ipsrst3_bits | NPCM_IPSRST3_USBPHY1 |
		   NPCM_IPSRST3_USBPHY2);

	writel(iprst1, rc->base + NPCM_IPSRST1);
	writel(iprst2, rc->base + NPCM_IPSRST2);
	writel(iprst3, rc->base + NPCM_IPSRST3);

	/* clear USB PHY RS bit */
	regmap_update_bits(gcr_regmap, NPCM_USB1PHYCTL_OFFSET,
			   NPCM_USBXPHYCTL_RS, 0);
	regmap_update_bits(gcr_regmap, NPCM_USB2PHYCTL_OFFSET,
			   NPCM_USBXPHYCTL_RS, 0);

	/* deassert reset USB PHY */
	iprst3 &= ~(NPCM_IPSRST3_USBPHY1 | NPCM_IPSRST3_USBPHY2);
	writel(iprst3, rc->base + NPCM_IPSRST3);

	udelay(50);

	/* set USB PHY RS bit */
	regmap_update_bits(gcr_regmap, NPCM_USB1PHYCTL_OFFSET,
			   NPCM_USBXPHYCTL_RS, NPCM_USBXPHYCTL_RS);
	regmap_update_bits(gcr_regmap, NPCM_USB2PHYCTL_OFFSET,
			   NPCM_USBXPHYCTL_RS, NPCM_USBXPHYCTL_RS);

	/* deassert reset USB devices*/
	iprst1 &= ~ipsrst1_bits;
	iprst2 &= ~ipsrst2_bits;
	iprst3 &= ~ipsrst3_bits;

	writel(iprst1, rc->base + NPCM_IPSRST1);
	writel(iprst2, rc->base + NPCM_IPSRST2);
	writel(iprst3, rc->base + NPCM_IPSRST3);

	return 0;
}

static const struct reset_control_ops npcm_rc_ops = {
	.assert		= npcm_rc_assert,
	.deassert	= npcm_rc_deassert,
	.status		= npcm_rc_status,
};

static int npcm_rc_probe(struct platform_device *pdev)
{
	struct npcm_rc_data *rc;
	int ret;

	rc = devm_kzalloc(&pdev->dev, sizeof(*rc), GFP_KERNEL);
	if (!rc)
		return -ENOMEM;

	rc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rc->base))
		return PTR_ERR(rc->base);

	spin_lock_init(&rc->lock);

	rc->rcdev.owner = THIS_MODULE;
	rc->rcdev.ops = &npcm_rc_ops;
	rc->rcdev.of_node = pdev->dev.of_node;
	rc->rcdev.of_reset_n_cells = 2;
	rc->rcdev.of_xlate = npcm_reset_xlate;

	platform_set_drvdata(pdev, rc);

	ret = devm_reset_controller_register(&pdev->dev, &rc->rcdev);
	if (ret) {
		dev_err(&pdev->dev, "unable to register device\n");
		return ret;
	}

	if (npcm_usb_reset(pdev, rc))
		dev_warn(&pdev->dev, "NPCM USB reset failed, can cause issues with UDC and USB host\n");

	if (!of_property_read_u32(pdev->dev.of_node, "nuvoton,sw-reset-number",
				  &rc->sw_reset_number)) {
		if (rc->sw_reset_number && rc->sw_reset_number < 5) {
			rc->restart_nb.priority = 192,
			rc->restart_nb.notifier_call = npcm_rc_restart,
			ret = register_restart_handler(&rc->restart_nb);
			if (ret)
				dev_warn(&pdev->dev, "failed to register restart handler\n");
		}
	}

	return ret;
}

static struct platform_driver npcm_rc_driver = {
	.probe	= npcm_rc_probe,
	.driver	= {
		.name			= "npcm-reset",
		.of_match_table		= npcm_rc_match,
		.suppress_bind_attrs	= true,
	},
};
builtin_platform_driver(npcm_rc_driver);
