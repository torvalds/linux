// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Apple mailbox driver
 *
 * Copyright The Asahi Linux Contributors
 *
 * This driver adds support for two mailbox variants (called ASC and M3 by
 * Apple) found in Apple SoCs such as the M1. It consists of two FIFOs used to
 * exchange 64+32 bit messages between the main CPU and a co-processor.
 * Various coprocessors implement different IPC protocols based on these simple
 * messages and shared memory buffers.
 *
 * Both the main CPU and the co-processor see the same set of registers but
 * the first FIFO (A2I) is always used to transfer messages from the application
 * processor (us) to the I/O processor and the second one (I2A) for the
 * other direction.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include "mailbox.h"

#define APPLE_ASC_MBOX_CONTROL_FULL BIT(16)
#define APPLE_ASC_MBOX_CONTROL_EMPTY BIT(17)

#define APPLE_ASC_MBOX_A2I_CONTROL 0x110
#define APPLE_ASC_MBOX_A2I_SEND0 0x800
#define APPLE_ASC_MBOX_A2I_SEND1 0x808
#define APPLE_ASC_MBOX_A2I_RECV0 0x810
#define APPLE_ASC_MBOX_A2I_RECV1 0x818

#define APPLE_ASC_MBOX_I2A_CONTROL 0x114
#define APPLE_ASC_MBOX_I2A_SEND0 0x820
#define APPLE_ASC_MBOX_I2A_SEND1 0x828
#define APPLE_ASC_MBOX_I2A_RECV0 0x830
#define APPLE_ASC_MBOX_I2A_RECV1 0x838

#define APPLE_M3_MBOX_CONTROL_FULL BIT(16)
#define APPLE_M3_MBOX_CONTROL_EMPTY BIT(17)

#define APPLE_M3_MBOX_A2I_CONTROL 0x50
#define APPLE_M3_MBOX_A2I_SEND0 0x60
#define APPLE_M3_MBOX_A2I_SEND1 0x68
#define APPLE_M3_MBOX_A2I_RECV0 0x70
#define APPLE_M3_MBOX_A2I_RECV1 0x78

#define APPLE_M3_MBOX_I2A_CONTROL 0x80
#define APPLE_M3_MBOX_I2A_SEND0 0x90
#define APPLE_M3_MBOX_I2A_SEND1 0x98
#define APPLE_M3_MBOX_I2A_RECV0 0xa0
#define APPLE_M3_MBOX_I2A_RECV1 0xa8

#define APPLE_M3_MBOX_IRQ_ENABLE 0x48
#define APPLE_M3_MBOX_IRQ_ACK 0x4c
#define APPLE_M3_MBOX_IRQ_A2I_EMPTY BIT(0)
#define APPLE_M3_MBOX_IRQ_A2I_NOT_EMPTY BIT(1)
#define APPLE_M3_MBOX_IRQ_I2A_EMPTY BIT(2)
#define APPLE_M3_MBOX_IRQ_I2A_NOT_EMPTY BIT(3)

#define APPLE_MBOX_MSG1_OUTCNT GENMASK(56, 52)
#define APPLE_MBOX_MSG1_INCNT GENMASK(51, 48)
#define APPLE_MBOX_MSG1_OUTPTR GENMASK(47, 44)
#define APPLE_MBOX_MSG1_INPTR GENMASK(43, 40)
#define APPLE_MBOX_MSG1_MSG GENMASK(31, 0)

#define APPLE_MBOX_TX_TIMEOUT 500

struct apple_mbox_hw {
	unsigned int control_full;
	unsigned int control_empty;

	unsigned int a2i_control;
	unsigned int a2i_send0;
	unsigned int a2i_send1;

	unsigned int i2a_control;
	unsigned int i2a_recv0;
	unsigned int i2a_recv1;

	bool has_irq_controls;
	unsigned int irq_enable;
	unsigned int irq_ack;
	unsigned int irq_bit_recv_not_empty;
	unsigned int irq_bit_send_empty;
};

int apple_mbox_send(struct apple_mbox *mbox, const struct apple_mbox_msg msg,
		    bool atomic)
{
	unsigned long flags;
	int ret;
	u32 mbox_ctrl;
	long t;

	spin_lock_irqsave(&mbox->tx_lock, flags);
	mbox_ctrl = readl_relaxed(mbox->regs + mbox->hw->a2i_control);

	while (mbox_ctrl & mbox->hw->control_full) {
		if (atomic) {
			ret = readl_poll_timeout_atomic(
				mbox->regs + mbox->hw->a2i_control, mbox_ctrl,
				!(mbox_ctrl & mbox->hw->control_full), 100,
				APPLE_MBOX_TX_TIMEOUT * 1000);

			if (ret) {
				spin_unlock_irqrestore(&mbox->tx_lock, flags);
				return ret;
			}

			break;
		}
		/*
		 * The interrupt is level triggered and will keep firing as long as the
		 * FIFO is empty. It will also keep firing if the FIFO was empty
		 * at any point in the past until it has been acknowledged at the
		 * mailbox level. By acknowledging it here we can ensure that we will
		 * only get the interrupt once the FIFO has been cleared again.
		 * If the FIFO is already empty before the ack it will fire again
		 * immediately after the ack.
		 */
		if (mbox->hw->has_irq_controls) {
			writel_relaxed(mbox->hw->irq_bit_send_empty,
				       mbox->regs + mbox->hw->irq_ack);
		}
		enable_irq(mbox->irq_send_empty);
		reinit_completion(&mbox->tx_empty);
		spin_unlock_irqrestore(&mbox->tx_lock, flags);

		t = wait_for_completion_interruptible_timeout(
			&mbox->tx_empty,
			msecs_to_jiffies(APPLE_MBOX_TX_TIMEOUT));
		if (t < 0)
			return t;
		else if (t == 0)
			return -ETIMEDOUT;

		spin_lock_irqsave(&mbox->tx_lock, flags);
		mbox_ctrl = readl_relaxed(mbox->regs + mbox->hw->a2i_control);
	}

	writeq_relaxed(msg.msg0, mbox->regs + mbox->hw->a2i_send0);
	writeq_relaxed(FIELD_PREP(APPLE_MBOX_MSG1_MSG, msg.msg1),
		       mbox->regs + mbox->hw->a2i_send1);

	spin_unlock_irqrestore(&mbox->tx_lock, flags);

	return 0;
}
EXPORT_SYMBOL(apple_mbox_send);

static irqreturn_t apple_mbox_send_empty_irq(int irq, void *data)
{
	struct apple_mbox *mbox = data;

	/*
	 * We don't need to acknowledge the interrupt at the mailbox level
	 * here even if supported by the hardware. It will keep firing but that
	 * doesn't matter since it's disabled at the main interrupt controller.
	 * apple_mbox_send will acknowledge it before enabling
	 * it at the main controller again.
	 */
	spin_lock(&mbox->tx_lock);
	disable_irq_nosync(mbox->irq_send_empty);
	complete(&mbox->tx_empty);
	spin_unlock(&mbox->tx_lock);

	return IRQ_HANDLED;
}

static int apple_mbox_poll_locked(struct apple_mbox *mbox)
{
	struct apple_mbox_msg msg;
	int ret = 0;

	u32 mbox_ctrl = readl_relaxed(mbox->regs + mbox->hw->i2a_control);

	while (!(mbox_ctrl & mbox->hw->control_empty)) {
		msg.msg0 = readq_relaxed(mbox->regs + mbox->hw->i2a_recv0);
		msg.msg1 = FIELD_GET(
			APPLE_MBOX_MSG1_MSG,
			readq_relaxed(mbox->regs + mbox->hw->i2a_recv1));

		mbox->rx(mbox, msg, mbox->cookie);
		ret++;
		mbox_ctrl = readl_relaxed(mbox->regs + mbox->hw->i2a_control);
	}

	/*
	 * The interrupt will keep firing even if there are no more messages
	 * unless we also acknowledge it at the mailbox level here.
	 * There's no race if a message comes in between the check in the while
	 * loop above and the ack below: If a new messages arrives inbetween
	 * those two the interrupt will just fire again immediately after the
	 * ack since it's level triggered.
	 */
	if (mbox->hw->has_irq_controls) {
		writel_relaxed(mbox->hw->irq_bit_recv_not_empty,
			       mbox->regs + mbox->hw->irq_ack);
	}

	return ret;
}

static irqreturn_t apple_mbox_recv_irq(int irq, void *data)
{
	struct apple_mbox *mbox = data;

	spin_lock(&mbox->rx_lock);
	apple_mbox_poll_locked(mbox);
	spin_unlock(&mbox->rx_lock);

	return IRQ_HANDLED;
}

int apple_mbox_poll(struct apple_mbox *mbox)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&mbox->rx_lock, flags);
	ret = apple_mbox_poll_locked(mbox);
	spin_unlock_irqrestore(&mbox->rx_lock, flags);

	return ret;
}
EXPORT_SYMBOL(apple_mbox_poll);

int apple_mbox_start(struct apple_mbox *mbox)
{
	int ret;

	if (mbox->active)
		return 0;

	ret = pm_runtime_resume_and_get(mbox->dev);
	if (ret)
		return ret;

	/*
	 * Only some variants of this mailbox HW provide interrupt control
	 * at the mailbox level. We therefore need to handle enabling/disabling
	 * interrupts at the main interrupt controller anyway for hardware that
	 * doesn't. Just always keep the interrupts we care about enabled at
	 * the mailbox level so that both hardware revisions behave almost
	 * the same.
	 */
	if (mbox->hw->has_irq_controls) {
		writel_relaxed(mbox->hw->irq_bit_recv_not_empty |
				       mbox->hw->irq_bit_send_empty,
			       mbox->regs + mbox->hw->irq_enable);
	}

	enable_irq(mbox->irq_recv_not_empty);
	mbox->active = true;
	return 0;
}
EXPORT_SYMBOL(apple_mbox_start);

void apple_mbox_stop(struct apple_mbox *mbox)
{
	if (!mbox->active)
		return;

	mbox->active = false;
	disable_irq(mbox->irq_recv_not_empty);
	pm_runtime_mark_last_busy(mbox->dev);
	pm_runtime_put_autosuspend(mbox->dev);
}
EXPORT_SYMBOL(apple_mbox_stop);

struct apple_mbox *apple_mbox_get(struct device *dev, int index)
{
	struct of_phandle_args args;
	struct platform_device *pdev;
	struct apple_mbox *mbox;
	int ret;

	ret = of_parse_phandle_with_args(dev->of_node, "mboxes", "#mbox-cells",
					 index, &args);
	if (ret || !args.np)
		return ERR_PTR(ret);

	pdev = of_find_device_by_node(args.np);
	of_node_put(args.np);

	if (!pdev)
		return ERR_PTR(EPROBE_DEFER);

	mbox = platform_get_drvdata(pdev);
	if (!mbox)
		return ERR_PTR(EPROBE_DEFER);

	if (!device_link_add(dev, &pdev->dev, DL_FLAG_AUTOREMOVE_CONSUMER))
		return ERR_PTR(ENODEV);

	return mbox;
}
EXPORT_SYMBOL(apple_mbox_get);

struct apple_mbox *apple_mbox_get_byname(struct device *dev, const char *name)
{
	int index;

	index = of_property_match_string(dev->of_node, "mbox-names", name);
	if (index < 0)
		return ERR_PTR(index);

	return apple_mbox_get(dev, index);
}
EXPORT_SYMBOL(apple_mbox_get_byname);

static int apple_mbox_probe(struct platform_device *pdev)
{
	int ret;
	char *irqname;
	struct apple_mbox *mbox;
	struct device *dev = &pdev->dev;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	mbox->dev = &pdev->dev;
	mbox->hw = of_device_get_match_data(dev);
	if (!mbox->hw)
		return -EINVAL;

	mbox->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mbox->regs))
		return PTR_ERR(mbox->regs);

	mbox->irq_recv_not_empty =
		platform_get_irq_byname(pdev, "recv-not-empty");
	if (mbox->irq_recv_not_empty < 0)
		return -ENODEV;

	mbox->irq_send_empty = platform_get_irq_byname(pdev, "send-empty");
	if (mbox->irq_send_empty < 0)
		return -ENODEV;

	spin_lock_init(&mbox->rx_lock);
	spin_lock_init(&mbox->tx_lock);
	init_completion(&mbox->tx_empty);

	irqname = devm_kasprintf(dev, GFP_KERNEL, "%s-recv", dev_name(dev));
	if (!irqname)
		return -ENOMEM;

	ret = devm_request_irq(dev, mbox->irq_recv_not_empty,
			       apple_mbox_recv_irq,
			       IRQF_NO_AUTOEN | IRQF_NO_SUSPEND, irqname, mbox);
	if (ret)
		return ret;

	irqname = devm_kasprintf(dev, GFP_KERNEL, "%s-send", dev_name(dev));
	if (!irqname)
		return -ENOMEM;

	ret = devm_request_irq(dev, mbox->irq_send_empty,
			       apple_mbox_send_empty_irq,
			       IRQF_NO_AUTOEN | IRQF_NO_SUSPEND, irqname, mbox);
	if (ret)
		return ret;

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mbox);
	return 0;
}

static const struct apple_mbox_hw apple_mbox_asc_hw = {
	.control_full = APPLE_ASC_MBOX_CONTROL_FULL,
	.control_empty = APPLE_ASC_MBOX_CONTROL_EMPTY,

	.a2i_control = APPLE_ASC_MBOX_A2I_CONTROL,
	.a2i_send0 = APPLE_ASC_MBOX_A2I_SEND0,
	.a2i_send1 = APPLE_ASC_MBOX_A2I_SEND1,

	.i2a_control = APPLE_ASC_MBOX_I2A_CONTROL,
	.i2a_recv0 = APPLE_ASC_MBOX_I2A_RECV0,
	.i2a_recv1 = APPLE_ASC_MBOX_I2A_RECV1,

	.has_irq_controls = false,
};

static const struct apple_mbox_hw apple_mbox_m3_hw = {
	.control_full = APPLE_M3_MBOX_CONTROL_FULL,
	.control_empty = APPLE_M3_MBOX_CONTROL_EMPTY,

	.a2i_control = APPLE_M3_MBOX_A2I_CONTROL,
	.a2i_send0 = APPLE_M3_MBOX_A2I_SEND0,
	.a2i_send1 = APPLE_M3_MBOX_A2I_SEND1,

	.i2a_control = APPLE_M3_MBOX_I2A_CONTROL,
	.i2a_recv0 = APPLE_M3_MBOX_I2A_RECV0,
	.i2a_recv1 = APPLE_M3_MBOX_I2A_RECV1,

	.has_irq_controls = true,
	.irq_enable = APPLE_M3_MBOX_IRQ_ENABLE,
	.irq_ack = APPLE_M3_MBOX_IRQ_ACK,
	.irq_bit_recv_not_empty = APPLE_M3_MBOX_IRQ_I2A_NOT_EMPTY,
	.irq_bit_send_empty = APPLE_M3_MBOX_IRQ_A2I_EMPTY,
};

static const struct of_device_id apple_mbox_of_match[] = {
	{ .compatible = "apple,asc-mailbox-v4", .data = &apple_mbox_asc_hw },
	{ .compatible = "apple,m3-mailbox-v2", .data = &apple_mbox_m3_hw },
	{}
};
MODULE_DEVICE_TABLE(of, apple_mbox_of_match);

static struct platform_driver apple_mbox_driver = {
	.driver = {
		.name = "apple-mailbox",
		.of_match_table = apple_mbox_of_match,
	},
	.probe = apple_mbox_probe,
};
module_platform_driver(apple_mbox_driver);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Sven Peter <sven@svenpeter.dev>");
MODULE_DESCRIPTION("Apple Mailbox driver");
