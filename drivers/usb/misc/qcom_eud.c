// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/usb/role.h>

#define EUD_REG_INT1_EN_MASK	0x0024
#define EUD_REG_INT_STATUS_1	0x0044
#define EUD_REG_CTL_OUT_1	0x0074
#define EUD_REG_VBUS_INT_CLR	0x0080
#define EUD_REG_CSR_EUD_EN	0x1014
#define EUD_REG_SW_ATTACH_DET	0x1018
#define EUD_REG_EUD_EN2		0x0000

#define EUD_ENABLE		BIT(0)
#define EUD_INT_PET_EUD		BIT(0)
#define EUD_INT_VBUS		BIT(2)
#define EUD_INT_SAFE_MODE	BIT(4)
#define EUD_INT_ALL		(EUD_INT_VBUS | EUD_INT_SAFE_MODE)

struct eud_chip {
	struct device			*dev;
	struct usb_role_switch		*role_sw;
	void __iomem			*base;
	void __iomem			*mode_mgr;
	unsigned int			int_status;
	int				irq;
	bool				enabled;
	bool				usb_attached;
};

static int enable_eud(struct eud_chip *priv)
{
	writel(EUD_ENABLE, priv->base + EUD_REG_CSR_EUD_EN);
	writel(EUD_INT_VBUS | EUD_INT_SAFE_MODE,
			priv->base + EUD_REG_INT1_EN_MASK);
	writel(1, priv->mode_mgr + EUD_REG_EUD_EN2);

	return usb_role_switch_set_role(priv->role_sw, USB_ROLE_DEVICE);
}

static void disable_eud(struct eud_chip *priv)
{
	writel(0, priv->base + EUD_REG_CSR_EUD_EN);
	writel(0, priv->mode_mgr + EUD_REG_EUD_EN2);
}

static ssize_t enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct eud_chip *chip = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", chip->enabled);
}

static ssize_t enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct eud_chip *chip = dev_get_drvdata(dev);
	bool enable;
	int ret;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	if (enable) {
		ret = enable_eud(chip);
		if (!ret)
			chip->enabled = enable;
		else
			disable_eud(chip);
	} else {
		disable_eud(chip);
	}

	return count;
}

static DEVICE_ATTR_RW(enable);

static struct attribute *eud_attrs[] = {
	&dev_attr_enable.attr,
	NULL,
};
ATTRIBUTE_GROUPS(eud);

static void usb_attach_detach(struct eud_chip *chip)
{
	u32 reg;

	/* read ctl_out_1[4] to find USB attach or detach event */
	reg = readl(chip->base + EUD_REG_CTL_OUT_1);
	chip->usb_attached = reg & EUD_INT_SAFE_MODE;
}

static void pet_eud(struct eud_chip *chip)
{
	u32 reg;
	int ret;

	/* When the EUD_INT_PET_EUD in SW_ATTACH_DET is set, the cable has been
	 * disconnected and we need to detach the pet to check if EUD is in safe
	 * mode before attaching again.
	 */
	reg = readl(chip->base + EUD_REG_SW_ATTACH_DET);
	if (reg & EUD_INT_PET_EUD) {
		/* Detach & Attach pet for EUD */
		writel(0, chip->base + EUD_REG_SW_ATTACH_DET);
		/* Delay to make sure detach pet is done before attach pet */
		ret = readl_poll_timeout(chip->base + EUD_REG_SW_ATTACH_DET,
					reg, (reg == 0), 1, 100);
		if (ret) {
			dev_err(chip->dev, "Detach pet failed\n");
			return;
		}
	}
	/* Attach pet for EUD */
	writel(EUD_INT_PET_EUD, chip->base + EUD_REG_SW_ATTACH_DET);
}

static irqreturn_t handle_eud_irq(int irq, void *data)
{
	struct eud_chip *chip = data;
	u32 reg;

	reg = readl(chip->base + EUD_REG_INT_STATUS_1);
	switch (reg & EUD_INT_ALL) {
	case EUD_INT_VBUS:
		usb_attach_detach(chip);
		return IRQ_WAKE_THREAD;
	case EUD_INT_SAFE_MODE:
		pet_eud(chip);
		return IRQ_HANDLED;
	default:
		return IRQ_NONE;
	}
}

static irqreturn_t handle_eud_irq_thread(int irq, void *data)
{
	struct eud_chip *chip = data;
	int ret;

	if (chip->usb_attached)
		ret = usb_role_switch_set_role(chip->role_sw, USB_ROLE_DEVICE);
	else
		ret = usb_role_switch_set_role(chip->role_sw, USB_ROLE_HOST);
	if (ret)
		dev_err(chip->dev, "failed to set role switch\n");

	/* set and clear vbus_int_clr[0] to clear interrupt */
	writel(BIT(0), chip->base + EUD_REG_VBUS_INT_CLR);
	writel(0, chip->base + EUD_REG_VBUS_INT_CLR);

	return IRQ_HANDLED;
}

static void eud_role_switch_release(void *data)
{
	struct eud_chip *chip = data;

	usb_role_switch_put(chip->role_sw);
}

static int eud_probe(struct platform_device *pdev)
{
	struct eud_chip *chip;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;

	chip->role_sw = usb_role_switch_get(&pdev->dev);
	if (IS_ERR(chip->role_sw))
		return dev_err_probe(chip->dev, PTR_ERR(chip->role_sw),
					"failed to get role switch\n");

	ret = devm_add_action_or_reset(chip->dev, eud_role_switch_release, chip);
	if (ret)
		return dev_err_probe(chip->dev, ret,
				"failed to add role switch release action\n");

	chip->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->base))
		return PTR_ERR(chip->base);

	chip->mode_mgr = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(chip->mode_mgr))
		return PTR_ERR(chip->mode_mgr);

	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0)
		return chip->irq;

	ret = devm_request_threaded_irq(&pdev->dev, chip->irq, handle_eud_irq,
			handle_eud_irq_thread, IRQF_ONESHOT, NULL, chip);
	if (ret)
		return dev_err_probe(chip->dev, ret, "failed to allocate irq\n");

	enable_irq_wake(chip->irq);

	platform_set_drvdata(pdev, chip);

	return 0;
}

static void eud_remove(struct platform_device *pdev)
{
	struct eud_chip *chip = platform_get_drvdata(pdev);

	if (chip->enabled)
		disable_eud(chip);

	device_init_wakeup(&pdev->dev, false);
	disable_irq_wake(chip->irq);
}

static const struct of_device_id eud_dt_match[] = {
	{ .compatible = "qcom,sc7280-eud" },
	{ }
};
MODULE_DEVICE_TABLE(of, eud_dt_match);

static struct platform_driver eud_driver = {
	.probe	= eud_probe,
	.remove_new = eud_remove,
	.driver	= {
		.name = "qcom_eud",
		.dev_groups = eud_groups,
		.of_match_table = eud_dt_match,
	},
};
module_platform_driver(eud_driver);

MODULE_DESCRIPTION("QTI EUD driver");
MODULE_LICENSE("GPL v2");
