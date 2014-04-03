/*
 * Copyright Altera Corporation (C) 2013. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/mailbox_controller.h>

#define MAILBOX_CMD_REG			0x00
#define MAILBOX_PTR_REG			0x04
#define MAILBOX_STS_REG			0x08
#define MAILBOX_INTMASK_REG		0x0C

#define INT_PENDING_MSK			0x1
#define INT_SPACE_MSK			0x2

#define STS_PENDING_MSK			0x1
#define STS_FULL_MSK			0x2
#define STS_FULL_OFT			0x1

#define MBOX_PENDING(status)	(((status) & STS_PENDING_MSK))
#define MBOX_FULL(status)	(((status) & STS_FULL_MSK) >> STS_FULL_OFT)

enum altera_mbox_msg {
	MBOX_CMD = 0,
	MBOX_PTR,
};

#define MBOX_POLLING_MS		1	/* polling interval 1ms */

struct altera_mbox {
	bool is_sender;		/* 1-sender, 0-receiver */
	bool intr_mode;
	int irq;
	int use_count;
	void __iomem *mbox_base;
	struct device *dev;
	struct ipc_link link;
	struct ipc_controller ipc_con;
	struct mutex lock;
	/* If the controller supports only RX polling mode */
	struct timer_list rxpoll_timer;
};

static inline struct altera_mbox *to_altera_mbox(struct ipc_link *link)
{
	if (!link)
		return NULL;

	return container_of(link, struct altera_mbox, link);
}

static inline int altera_mbox_full(struct altera_mbox *mbox)
{
	u32 status;
	status = __raw_readl(mbox->mbox_base + MAILBOX_STS_REG);
	return MBOX_FULL(status);
}

static inline int altera_mbox_pending(struct altera_mbox *mbox)
{
	u32 status;
	status = __raw_readl(mbox->mbox_base + MAILBOX_STS_REG);
	return MBOX_PENDING(status);
}

static void altera_mbox_rx_intmask(struct altera_mbox *mbox, bool enable)
{
	u32 mask;
	mask = __raw_readl(mbox->mbox_base + MAILBOX_INTMASK_REG);
	if (enable)
		mask |= INT_PENDING_MSK;
	else
		mask &= ~INT_PENDING_MSK;
	__raw_writel(mask, mbox->mbox_base + MAILBOX_INTMASK_REG);
}

static void altera_mbox_tx_intmask(struct altera_mbox *mbox, bool enable)
{
	u32 mask;
	mask = __raw_readl(mbox->mbox_base + MAILBOX_INTMASK_REG);
	if (enable)
		mask |= INT_SPACE_MSK;
	else
		mask &= ~INT_SPACE_MSK;
	__raw_writel(mask, mbox->mbox_base + MAILBOX_INTMASK_REG);
}

static bool altera_mbox_is_sender(struct altera_mbox *mbox)
{
	u32 reg;
	/* Write a magic number to PTR register and read back this register.
	 * This register is read-write if it is a sender.
	 */
	#define MBOX_MAGIC	0xA5A5AA55
	__raw_writel(MBOX_MAGIC, mbox->mbox_base + MAILBOX_PTR_REG);
	reg = __raw_readl(mbox->mbox_base + MAILBOX_PTR_REG);
	if (reg == MBOX_MAGIC) {
		/* Clear to 0 */
		__raw_writel(0, mbox->mbox_base + MAILBOX_PTR_REG);
		return true;
	}
	return false;
}

static void altera_mbox_rx_data(struct ipc_link *link)
{
	struct altera_mbox *mbox = to_altera_mbox(link);
	u32 data[2];

	if (altera_mbox_pending(mbox)) {
		data[MBOX_PTR] = __raw_readl(mbox->mbox_base + MAILBOX_PTR_REG);
		data[MBOX_CMD] = __raw_readl(mbox->mbox_base + MAILBOX_CMD_REG);
		ipc_link_received_data(link, (void *)data);
	}

	return;
}

static void altera_mbox_poll_rx(unsigned long data)
{
	struct ipc_link *link = (struct ipc_link *)data;
	struct altera_mbox *mbox = to_altera_mbox(link);

	altera_mbox_rx_data(link);

	mod_timer(&mbox->rxpoll_timer,
		jiffies + msecs_to_jiffies(MBOX_POLLING_MS));
}

static irqreturn_t altera_mbox_tx_interrupt(int irq, void *p)
{
	struct ipc_link *link = (struct ipc_link *)p;
	struct altera_mbox *mbox = to_altera_mbox(link);

	altera_mbox_tx_intmask(mbox, false);
	ipc_link_txdone(link, XFER_OK);

	return IRQ_HANDLED;
}

static irqreturn_t altera_mbox_rx_interrupt(int irq, void *p)
{
	struct ipc_link *link = (struct ipc_link *)p;
	altera_mbox_rx_data(link);
	return IRQ_HANDLED;
}

static int altera_mbox_startup_sender(struct ipc_link *link)
{
	int ret;
	struct altera_mbox *mbox = to_altera_mbox(link);

	if (mbox->intr_mode) {
		ret = request_irq(mbox->irq, altera_mbox_tx_interrupt, 0,
			mbox->ipc_con.controller_name, link);
		if (ret) {
			dev_err(mbox->dev,
				"failed to register mailbox interrupt:%d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static int altera_mbox_startup_receiver(struct ipc_link *link)
{
	int ret;
	struct altera_mbox *mbox = to_altera_mbox(link);

	if (mbox->intr_mode) {
		ret = request_irq(mbox->irq, altera_mbox_rx_interrupt, 0,
			mbox->ipc_con.controller_name, link);
		if (ret) {
			dev_err(mbox->dev,
				"failed to register mailbox interrupt:%d\n",
				ret);
			return ret;
		}
		altera_mbox_rx_intmask(mbox, true);
	} else {
		/* Setup polling timer */
		setup_timer(&mbox->rxpoll_timer, altera_mbox_poll_rx,
			(unsigned long)link);
		mod_timer(&mbox->rxpoll_timer,
			jiffies + msecs_to_jiffies(MBOX_POLLING_MS));
	}

	return 0;
}

static int altera_mbox_send_data(struct ipc_link *link, void *data)
{
	struct altera_mbox *mbox = to_altera_mbox(link);
	u32 *udata = (u32 *)data;

	if (!mbox || !data)
		return -EINVAL;
	if (!mbox->is_sender) {
		dev_warn(mbox->dev,
				"failed to send. This is receiver mailbox.\n");
		return -EINVAL;
	}

	if (altera_mbox_full(mbox))
		return -EBUSY;

	/* Enable interrupt before send */
	altera_mbox_tx_intmask(mbox, true);

	/* Pointer register must write before command register */
	__raw_writel(udata[MBOX_PTR], mbox->mbox_base + MAILBOX_PTR_REG);
	__raw_writel(udata[MBOX_CMD], mbox->mbox_base + MAILBOX_CMD_REG);

	return 0;
}

static bool altera_mbox_is_ready(struct ipc_link *link)
{
	struct altera_mbox *mbox = to_altera_mbox(link);

	if (WARN_ON(!mbox))
		return false;

	/* Return false if mailbox is full */
	return altera_mbox_full(mbox) ? false : true;
}

static int altera_mbox_startup(struct ipc_link *link, void *ignored)
{
	struct altera_mbox *mbox = to_altera_mbox(link);
	int ret = 0;

	if (!mbox)
		return -EINVAL;

	mutex_lock(&mbox->lock);
	if (!mbox->use_count) {
		if (mbox->is_sender)
			ret = altera_mbox_startup_sender(link);
		else
			ret = altera_mbox_startup_receiver(link);

		if (!ret)
			mbox->use_count++;
	}
	mutex_unlock(&mbox->lock);
	return ret;
}

static void altera_mbox_shutdown(struct ipc_link *link)
{
	struct altera_mbox *mbox = to_altera_mbox(link);

	if (WARN_ON(!mbox))
		return;

	mutex_lock(&mbox->lock);
	if (!--mbox->use_count) {
		if (mbox->intr_mode) {
			/* Unmask all interrupt masks */
			__raw_writel(~0, mbox->mbox_base + MAILBOX_INTMASK_REG);
			free_irq(mbox->irq, link);
		} else if (!mbox->is_sender)
			del_timer_sync(&mbox->rxpoll_timer);
	}
	mutex_unlock(&mbox->lock);
}

static struct ipc_link_ops altera_mbox_ops = {
	.send_data = altera_mbox_send_data,
	.startup = altera_mbox_startup,
	.shutdown = altera_mbox_shutdown,
	.is_ready = altera_mbox_is_ready,
};

static int altera_mbox_probe(struct platform_device *pdev)
{
	struct altera_mbox *mbox;
	struct ipc_link *links[2] = {NULL, NULL};
	struct resource	*regs;
	struct device_node *np = pdev->dev.of_node;
	int ret;
	const char *mbox_name = NULL;

	mbox = devm_kzalloc(&pdev->dev, sizeof(struct altera_mbox),
		GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;

	mbox->mbox_base = devm_request_and_ioremap(&pdev->dev, regs);
	if (!mbox->mbox_base)
		return -EADDRNOTAVAIL;

	mbox->dev = &pdev->dev;
	mutex_init(&mbox->lock);

	/* Check is it a sender or receiver? */
	mbox->is_sender = altera_mbox_is_sender(mbox);

	mbox->irq = platform_get_irq(pdev, 0);
	if (mbox->irq >= 0)
		mbox->intr_mode = true;

	/* Hardware supports only one channel, link_name always set to "0". */
	snprintf(mbox->link.link_name, sizeof(mbox->link.link_name), "0");
	links[0] = &mbox->link;
	mbox->ipc_con.links = links;
	mbox->ipc_con.ops = &altera_mbox_ops;

	if ((strlen(np->name) + 1) > sizeof(mbox->ipc_con.controller_name))
		dev_warn(&pdev->dev, "Length of mailbox controller name is greater than %zu\n",
		sizeof(mbox->ipc_con.controller_name));

	ret = of_property_read_string(np, "linux,mailbox-name", &mbox_name);
	if (ret) {
		dev_err(&pdev->dev, "Missing linux,mailbox-name property in device tree\n");
		goto err;
	}

	snprintf(mbox->ipc_con.controller_name,
		sizeof(mbox->ipc_con.controller_name), "%s", mbox_name);

	dev_info(&pdev->dev, "Mailbox controller name is %s\n",
		mbox->ipc_con.controller_name);

	if (mbox->is_sender) {
		if (mbox->intr_mode)
			mbox->ipc_con.txdone_irq = true;
		else {
			mbox->ipc_con.txdone_poll = true;
			mbox->ipc_con.txpoll_period = MBOX_POLLING_MS;
		}
	}

	ret = ipc_links_register(&mbox->ipc_con);
	if (ret) {
		dev_err(&pdev->dev, "Register mailbox failed\n");
		goto err;
	}

	platform_set_drvdata(pdev, mbox);
	return 0;
err:
	return ret;
}

static int altera_mbox_remove(struct platform_device *pdev)
{
	struct altera_mbox *mbox = platform_get_drvdata(pdev);
	if (!mbox)
		return -EINVAL;

	ipc_links_unregister(&mbox->ipc_con);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id altera_mbox_match[] = {
	{ .compatible = "altr,mailbox-1.0" },
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, altera_mbox_match);

static struct platform_driver altera_mbox_driver = {
	.probe	= altera_mbox_probe,
	.remove	= altera_mbox_remove,
	.driver	= {
		.name	= "altera-mailbox",
		.owner	= THIS_MODULE,
		.of_match_table	= altera_mbox_match,
	},
};

static int altera_mbox_init(void)
{
	return platform_driver_register(&altera_mbox_driver);
}

static void altera_mbox_exit(void)
{
	platform_driver_unregister(&altera_mbox_driver);
}

module_init(altera_mbox_init);
module_exit(altera_mbox_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Altera mailbox specific functions");
MODULE_AUTHOR("Ley Foon Tan <lftan@altera.com>");
MODULE_ALIAS("platform:altera-mailbox");
