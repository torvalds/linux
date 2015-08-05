/*
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include "common.h"
#include "hardware.h"

#define MU_ATR0_OFFSET	0x0
#define MU_ARR0_OFFSET	0x10
#define MU_ARR1_OFFSET	0x14
#define MU_ASR		0x20
#define MU_ACR		0x24

#define MU_LPM_HANDSHAKE_INDEX		0
#define MU_RPMSG_HANDSHAKE_INDEX	1

struct imx_mu_rpmsg_box {
	const char *name;
	struct blocking_notifier_head notifier;
};

static struct imx_mu_rpmsg_box mu_rpmsg_box = {
	.name	= "m4",
};

static void __iomem *mu_base;
static u32 m4_message;
static struct delayed_work rpmsg_work;

static int imx_mu_send_message(unsigned int index, unsigned int data)
{
	u32 val, ep;
	int i, te_flag = 0;
	unsigned long timeout = jiffies + msecs_to_jiffies(500);

	/* wait for transfer buffer empty, and no event pending */
	do {
		val = readl_relaxed(mu_base + MU_ASR);
		ep = val & BIT(4);
		if (time_after(jiffies, timeout)) {
			pr_err("Waiting MU transmit buffer empty timeout!\n");
			return -EIO;
		}
	} while (((val & (1 << (20 + 3 - index))) == 0) || (ep == BIT(4)));

	writel_relaxed(data, mu_base + index * 0x4 + MU_ATR0_OFFSET);

	/*
	 * make a double check that TEn is not empty after write
	 */
	val = readl_relaxed(mu_base + MU_ASR);
	ep = val & BIT(4);
	if (((val & (1 << (20 + (3 - index)))) == 0) || (ep == BIT(4)))
		return 0;
	else
		te_flag = 1;

	/*
	 * Make sure that TEn flag is changed, after the ATRn is filled up.
	 */
	for (i = 0; i < 100; i++) {
		val = readl_relaxed(mu_base + MU_ASR);
		ep = val & BIT(4);
		if (((val & (1 << (20 + 3 - index))) == 0) || (ep == BIT(4))) {
			/*
			 * BUG here. TEn flag is changes, after the
			 * ATRn is filled with MSG for a while.
			 */
			te_flag = 0;
			break;
		} else if (time_after(jiffies, timeout)) {
			/* Can't see TEn 1->0, maybe already handled! */
			te_flag = 1;
			break;
		}
	}
	if (te_flag == 0)
		pr_info("BUG: TEn is not changed immediately"
				"when ATRn is filled up.\n");

	return 0;
}

int imx_mu_rpmsg_send(unsigned int rpmsg)
{
	return imx_mu_send_message(MU_RPMSG_HANDSHAKE_INDEX, rpmsg);
}

int imx_mu_rpmsg_register_nb(const char *name, struct notifier_block *nb)
{
	if ((name == NULL) || (nb == NULL))
		return -EINVAL;

	if (!strcmp(mu_rpmsg_box.name, name))
		blocking_notifier_chain_register(&(mu_rpmsg_box.notifier), nb);
	else
		return -ENOENT;

	return 0;
}

int imx_mu_rpmsg_unregister_nb(const char *name, struct notifier_block *nb)
{
	if ((name == NULL) || (nb == NULL))
		return -EINVAL;

	if (!strcmp(mu_rpmsg_box.name, name))
		blocking_notifier_chain_unregister(&(mu_rpmsg_box.notifier),
				nb);
	else
		return -ENOENT;

	return 0;
}

static void rpmsg_work_handler(struct work_struct *work)
{

	blocking_notifier_call_chain(&(mu_rpmsg_box.notifier), 4,
						(void *)m4_message);
	m4_message = 0;
}

static irqreturn_t imx_mu_isr(int irq, void *param)
{
	u32 irqs;

	irqs = readl_relaxed(mu_base + MU_ASR);

	/* RPMSG */
	if (irqs & (1 << 26)) {
		/* get message from receive buffer */
		m4_message = readl_relaxed(mu_base + MU_ARR1_OFFSET);
		schedule_delayed_work(&rpmsg_work, 0);
	}

	return IRQ_HANDLED;
}

static int imx_mu_probe(struct platform_device *pdev)
{
	int ret;
	u32 irq;
	struct device_node *np;
	struct clk *clk;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6sx-mu");
	mu_base = of_iomap(np, 0);
	WARN_ON(!mu_base);

	irq = platform_get_irq(pdev, 0);

	ret = request_irq(irq, imx_mu_isr,
		IRQF_EARLY_RESUME, "imx-mu", &mu_rpmsg_box);
	if (ret) {
		pr_err("%s: register interrupt %d failed, rc %d\n",
			__func__, irq, ret);
		return ret;
	}

	ret = of_device_is_compatible(np, "fsl,imx7d-mu");
	if (ret) {
		clk = devm_clk_get(&pdev->dev, "mu");
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev,
				"mu clock source missing or invalid\n");
			return PTR_ERR(clk);
		} else {
			ret = clk_prepare_enable(clk);
			if (ret) {
				dev_err(&pdev->dev,
					"unable to enable mu clock\n");
				return ret;
			}
		}
	}

	INIT_DELAYED_WORK(&rpmsg_work, rpmsg_work_handler);
	/* enable the bit26(RIE1) of MU_ACR */
	writel_relaxed(readl_relaxed(mu_base + MU_ACR) | BIT(26),
			mu_base + MU_ACR);
	BLOCKING_INIT_NOTIFIER_HEAD(&(mu_rpmsg_box.notifier));

	pr_info("MU is ready for cross core communication!\n");

	return 0;
}

static const struct of_device_id imx_mu_ids[] = {
	{ .compatible = "fsl,imx6sx-mu" },
	{ .compatible = "fsl,imx7d-mu" },
	{ }
};

static struct platform_driver imx_mu_driver = {
	.driver = {
		.name   = "imx-mu",
		.owner  = THIS_MODULE,
		.of_match_table = imx_mu_ids,
	},
	.probe = imx_mu_probe,
};

static int __init imx_mu_init(void)
{
	return platform_driver_register(&imx_mu_driver);
}
subsys_initcall(imx_mu_init);
