// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Samsung Electronics Co., Ltd.
 * Copyright 2020 Google LLC.
 * Copyright 2024 Linaro Ltd.
 */

#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox/exynos-message.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define EXYNOS_MBOX_INTMR0		0x28	/* Interrupt Mask Register 0 */
#define EXYNOS_MBOX_INTGR1		0x40	/* Interrupt Generation Register 1 */

#define EXYNOS_MBOX_INTMR0_MASK		GENMASK(15, 0)
#define EXYNOS_MBOX_INTGR1_MASK		GENMASK(15, 0)

#define EXYNOS_MBOX_CHAN_COUNT		HWEIGHT32(EXYNOS_MBOX_INTGR1_MASK)

#define EXYNOS850_MBOX_INTGR0		0x8	/* Interrupt Generation Register 0	*/
#define EXYNOS850_MBOX_INTMR1		0x24	/* Interrupt Mask Register 1		*/

#define EXYNOS850_MBOX_INTMR1_MASK	GENMASK(15, 0)
#define EXYNOS850_MBOX_INTGR0_MASK	GENMASK(31, 16)

#define EXYNOS850_MBOX_CHAN_COUNT	HWEIGHT32(EXYNOS850_MBOX_INTGR0_MASK)

/**
 * struct exynos_mbox_driver_data - platform-specific mailbox configuration.
 * @intgr:		offset to the IRQ generation register, doorbell
 *			to APM co-processor.
 * @intgr_shift:	shift to apply to the value written to IRQ generation
 *			register.
 * @intmr:		offset to the IRQ mask register.
 * @intmr_mask:		value to write to the mask register to mask out all
 *			interrupts.
 * @num_chans:		number of channels the mailbox can support (hardware
 *			capability).
 */
struct exynos_mbox_driver_data {
	u32 intgr;
	u32 intgr_shift;
	u32 intmr;
	u32 intmr_mask;
	int num_chans;
};

/**
 * struct exynos_mbox - driver's private data.
 * @regs:	mailbox registers base address.
 * @mbox:	pointer to the mailbox controller.
 * @data:	pointer to driver platform-specific data.
 */
struct exynos_mbox {
	void __iomem *regs;
	struct mbox_controller *mbox;
	const struct exynos_mbox_driver_data *data;
};

static const struct exynos_mbox_driver_data exynos850_mbox_data = {
	.intgr = EXYNOS850_MBOX_INTGR0,
	.intgr_shift = 16,
	.intmr = EXYNOS850_MBOX_INTMR1,
	.intmr_mask = EXYNOS850_MBOX_INTMR1_MASK,
	.num_chans = EXYNOS850_MBOX_CHAN_COUNT,
};

static const struct exynos_mbox_driver_data exynos_gs101_mbox_data = {
	.intgr = EXYNOS_MBOX_INTGR1,
	.intgr_shift = 0,
	.intmr = EXYNOS_MBOX_INTMR0,
	.intmr_mask = EXYNOS_MBOX_INTMR0_MASK,
	.num_chans = EXYNOS_MBOX_CHAN_COUNT,
};

static int exynos_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct device *dev = chan->mbox->dev;
	struct exynos_mbox *exynos_mbox = dev_get_drvdata(dev);
	struct exynos_mbox_msg *msg = data;

	if (msg->chan_id >= exynos_mbox->mbox->num_chans) {
		dev_err(dev, "Invalid channel ID %d\n", msg->chan_id);
		return -EINVAL;
	}

	if (msg->chan_type != EXYNOS_MBOX_CHAN_TYPE_DOORBELL) {
		dev_err(dev, "Unsupported channel type [%d]\n", msg->chan_type);
		return -EINVAL;
	}

	/* Ring the doorbell */
	writel(BIT(msg->chan_id) << exynos_mbox->data->intgr_shift,
	       exynos_mbox->regs + exynos_mbox->data->intgr);

	return 0;
}

static const struct mbox_chan_ops exynos_mbox_chan_ops = {
	.send_data = exynos_mbox_send_data,
};

static struct mbox_chan *exynos_mbox_of_xlate(struct mbox_controller *mbox,
					      const struct of_phandle_args *sp)
{
	int i;

	if (sp->args_count != 0)
		return ERR_PTR(-EINVAL);

	/*
	 * Return the first available channel. When we don't pass the
	 * channel ID from device tree, each channel populated by the driver is
	 * just a software construct or a virtual channel. We use 'void *data'
	 * in send_data() to pass the channel identifiers.
	 */
	for (i = 0; i < mbox->num_chans; i++)
		if (mbox->chans[i].cl == NULL)
			return &mbox->chans[i];
	return ERR_PTR(-EINVAL);
}

static const struct of_device_id exynos_mbox_match[] = {
	{
		.compatible = "google,gs101-mbox",
		.data = &exynos_gs101_mbox_data
	},
	{
		.compatible = "samsung,exynos850-mbox",
		.data = &exynos850_mbox_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_mbox_match);

static int exynos_mbox_probe(struct platform_device *pdev)
{
	const struct exynos_mbox_driver_data *data;
	struct device *dev = &pdev->dev;
	struct exynos_mbox *exynos_mbox;
	struct mbox_controller *mbox;
	struct mbox_chan *chans;
	struct clk *pclk;

	data = device_get_match_data(&pdev->dev);
	if (!data)
		return -ENODEV;

	exynos_mbox = devm_kzalloc(dev, sizeof(*exynos_mbox), GFP_KERNEL);
	if (!exynos_mbox)
		return -ENOMEM;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	chans = devm_kcalloc(dev, data->num_chans, sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return -ENOMEM;

	exynos_mbox->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(exynos_mbox->regs))
		return PTR_ERR(exynos_mbox->regs);

	pclk = devm_clk_get_enabled(dev, "pclk");
	if (IS_ERR(pclk))
		return dev_err_probe(dev, PTR_ERR(pclk),
				     "Failed to enable clock.\n");

	exynos_mbox->data = data;
	mbox->num_chans = data->num_chans;
	mbox->chans = chans;
	mbox->dev = dev;
	mbox->ops = &exynos_mbox_chan_ops;
	mbox->of_xlate = exynos_mbox_of_xlate;

	exynos_mbox->mbox = mbox;

	platform_set_drvdata(pdev, exynos_mbox);

	/* Mask out all interrupts. We support just polling channels for now. */
	writel(data->intmr_mask, exynos_mbox->regs + data->intmr);

	return devm_mbox_controller_register(dev, mbox);
}

static struct platform_driver exynos_mbox_driver = {
	.probe	= exynos_mbox_probe,
	.driver	= {
		.name = "exynos-acpm-mbox",
		.of_match_table	= exynos_mbox_match,
	},
};
module_platform_driver(exynos_mbox_driver);

MODULE_AUTHOR("Tudor Ambarus <tudor.ambarus@linaro.org>");
MODULE_DESCRIPTION("Samsung Exynos mailbox driver");
MODULE_LICENSE("GPL");
