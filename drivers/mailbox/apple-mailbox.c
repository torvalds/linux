// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Apple mailbox driver
 *
 * Copyright (C) 2021 The Asahi Linux Contributors
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

#include <linux/apple-mailbox.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define APPLE_ASC_MBOX_CONTROL_FULL  BIT(16)
#define APPLE_ASC_MBOX_CONTROL_EMPTY BIT(17)

#define APPLE_ASC_MBOX_A2I_CONTROL 0x110
#define APPLE_ASC_MBOX_A2I_SEND0   0x800
#define APPLE_ASC_MBOX_A2I_SEND1   0x808
#define APPLE_ASC_MBOX_A2I_RECV0   0x810
#define APPLE_ASC_MBOX_A2I_RECV1   0x818

#define APPLE_ASC_MBOX_I2A_CONTROL 0x114
#define APPLE_ASC_MBOX_I2A_SEND0   0x820
#define APPLE_ASC_MBOX_I2A_SEND1   0x828
#define APPLE_ASC_MBOX_I2A_RECV0   0x830
#define APPLE_ASC_MBOX_I2A_RECV1   0x838

#define APPLE_M3_MBOX_CONTROL_FULL  BIT(16)
#define APPLE_M3_MBOX_CONTROL_EMPTY BIT(17)

#define APPLE_M3_MBOX_A2I_CONTROL 0x50
#define APPLE_M3_MBOX_A2I_SEND0	  0x60
#define APPLE_M3_MBOX_A2I_SEND1	  0x68
#define APPLE_M3_MBOX_A2I_RECV0	  0x70
#define APPLE_M3_MBOX_A2I_RECV1	  0x78

#define APPLE_M3_MBOX_I2A_CONTROL 0x80
#define APPLE_M3_MBOX_I2A_SEND0	  0x90
#define APPLE_M3_MBOX_I2A_SEND1	  0x98
#define APPLE_M3_MBOX_I2A_RECV0	  0xa0
#define APPLE_M3_MBOX_I2A_RECV1	  0xa8

#define APPLE_M3_MBOX_IRQ_ENABLE	0x48
#define APPLE_M3_MBOX_IRQ_ACK		0x4c
#define APPLE_M3_MBOX_IRQ_A2I_EMPTY	BIT(0)
#define APPLE_M3_MBOX_IRQ_A2I_NOT_EMPTY BIT(1)
#define APPLE_M3_MBOX_IRQ_I2A_EMPTY	BIT(2)
#define APPLE_M3_MBOX_IRQ_I2A_NOT_EMPTY BIT(3)

#define APPLE_MBOX_MSG1_OUTCNT GENMASK(56, 52)
#define APPLE_MBOX_MSG1_INCNT  GENMASK(51, 48)
#define APPLE_MBOX_MSG1_OUTPTR GENMASK(47, 44)
#define APPLE_MBOX_MSG1_INPTR  GENMASK(43, 40)
#define APPLE_MBOX_MSG1_MSG    GENMASK(31, 0)

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

struct apple_mbox {
	void __iomem *regs;
	const struct apple_mbox_hw *hw;

	int irq_recv_not_empty;
	int irq_send_empty;

	struct mbox_chan chan;

	struct device *dev;
	struct mbox_controller controller;
};

static const struct of_device_id apple_mbox_of_match[];

static bool apple_mbox_hw_can_send(struct apple_mbox *apple_mbox)
{
	u32 mbox_ctrl =
		readl_relaxed(apple_mbox->regs + apple_mbox->hw->a2i_control);

	return !(mbox_ctrl & apple_mbox->hw->control_full);
}

static int apple_mbox_hw_send(struct apple_mbox *apple_mbox,
			      struct apple_mbox_msg *msg)
{
	if (!apple_mbox_hw_can_send(apple_mbox))
		return -EBUSY;

	dev_dbg(apple_mbox->dev, "> TX %016llx %08x\n", msg->msg0, msg->msg1);

	writeq_relaxed(msg->msg0, apple_mbox->regs + apple_mbox->hw->a2i_send0);
	writeq_relaxed(FIELD_PREP(APPLE_MBOX_MSG1_MSG, msg->msg1),
		       apple_mbox->regs + apple_mbox->hw->a2i_send1);

	return 0;
}

static bool apple_mbox_hw_can_recv(struct apple_mbox *apple_mbox)
{
	u32 mbox_ctrl =
		readl_relaxed(apple_mbox->regs + apple_mbox->hw->i2a_control);

	return !(mbox_ctrl & apple_mbox->hw->control_empty);
}

static int apple_mbox_hw_recv(struct apple_mbox *apple_mbox,
			      struct apple_mbox_msg *msg)
{
	if (!apple_mbox_hw_can_recv(apple_mbox))
		return -ENOMSG;

	msg->msg0 = readq_relaxed(apple_mbox->regs + apple_mbox->hw->i2a_recv0);
	msg->msg1 = FIELD_GET(
		APPLE_MBOX_MSG1_MSG,
		readq_relaxed(apple_mbox->regs + apple_mbox->hw->i2a_recv1));

	dev_dbg(apple_mbox->dev, "< RX %016llx %08x\n", msg->msg0, msg->msg1);

	return 0;
}

static int apple_mbox_chan_send_data(struct mbox_chan *chan, void *data)
{
	struct apple_mbox *apple_mbox = chan->con_priv;
	struct apple_mbox_msg *msg = data;
	int ret;

	ret = apple_mbox_hw_send(apple_mbox, msg);
	if (ret)
		return ret;

	/*
	 * The interrupt is level triggered and will keep firing as long as the
	 * FIFO is empty. It will also keep firing if the FIFO was empty
	 * at any point in the past until it has been acknowledged at the
	 * mailbox level. By acknowledging it here we can ensure that we will
	 * only get the interrupt once the FIFO has been cleared again.
	 * If the FIFO is already empty before the ack it will fire again
	 * immediately after the ack.
	 */
	if (apple_mbox->hw->has_irq_controls) {
		writel_relaxed(apple_mbox->hw->irq_bit_send_empty,
			       apple_mbox->regs + apple_mbox->hw->irq_ack);
	}
	enable_irq(apple_mbox->irq_send_empty);

	return 0;
}

static irqreturn_t apple_mbox_send_empty_irq(int irq, void *data)
{
	struct apple_mbox *apple_mbox = data;

	/*
	 * We don't need to acknowledge the interrupt at the mailbox level
	 * here even if supported by the hardware. It will keep firing but that
	 * doesn't matter since it's disabled at the main interrupt controller.
	 * apple_mbox_chan_send_data will acknowledge it before enabling
	 * it at the main controller again.
	 */
	disable_irq_nosync(apple_mbox->irq_send_empty);
	mbox_chan_txdone(&apple_mbox->chan, 0);
	return IRQ_HANDLED;
}

static irqreturn_t apple_mbox_recv_irq(int irq, void *data)
{
	struct apple_mbox *apple_mbox = data;
	struct apple_mbox_msg msg;

	while (apple_mbox_hw_recv(apple_mbox, &msg) == 0)
		mbox_chan_received_data(&apple_mbox->chan, (void *)&msg);

	/*
	 * The interrupt will keep firing even if there are no more messages
	 * unless we also acknowledge it at the mailbox level here.
	 * There's no race if a message comes in between the check in the while
	 * loop above and the ack below: If a new messages arrives inbetween
	 * those two the interrupt will just fire again immediately after the
	 * ack since it's level triggered.
	 */
	if (apple_mbox->hw->has_irq_controls) {
		writel_relaxed(apple_mbox->hw->irq_bit_recv_not_empty,
			       apple_mbox->regs + apple_mbox->hw->irq_ack);
	}

	return IRQ_HANDLED;
}

static int apple_mbox_chan_startup(struct mbox_chan *chan)
{
	struct apple_mbox *apple_mbox = chan->con_priv;

	/*
	 * Only some variants of this mailbox HW provide interrupt control
	 * at the mailbox level. We therefore need to handle enabling/disabling
	 * interrupts at the main interrupt controller anyway for hardware that
	 * doesn't. Just always keep the interrupts we care about enabled at
	 * the mailbox level so that both hardware revisions behave almost
	 * the same.
	 */
	if (apple_mbox->hw->has_irq_controls) {
		writel_relaxed(apple_mbox->hw->irq_bit_recv_not_empty |
				       apple_mbox->hw->irq_bit_send_empty,
			       apple_mbox->regs + apple_mbox->hw->irq_enable);
	}

	enable_irq(apple_mbox->irq_recv_not_empty);
	return 0;
}

static void apple_mbox_chan_shutdown(struct mbox_chan *chan)
{
	struct apple_mbox *apple_mbox = chan->con_priv;

	disable_irq(apple_mbox->irq_recv_not_empty);
}

static const struct mbox_chan_ops apple_mbox_ops = {
	.send_data = apple_mbox_chan_send_data,
	.startup = apple_mbox_chan_startup,
	.shutdown = apple_mbox_chan_shutdown,
};

static struct mbox_chan *apple_mbox_of_xlate(struct mbox_controller *mbox,
					     const struct of_phandle_args *args)
{
	if (args->args_count != 0)
		return ERR_PTR(-EINVAL);

	return &mbox->chans[0];
}

static int apple_mbox_probe(struct platform_device *pdev)
{
	int ret;
	const struct of_device_id *match;
	char *irqname;
	struct apple_mbox *mbox;
	struct device *dev = &pdev->dev;

	match = of_match_node(apple_mbox_of_match, pdev->dev.of_node);
	if (!match)
		return -EINVAL;
	if (!match->data)
		return -EINVAL;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;
	platform_set_drvdata(pdev, mbox);

	mbox->dev = dev;
	mbox->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mbox->regs))
		return PTR_ERR(mbox->regs);

	mbox->hw = match->data;
	mbox->irq_recv_not_empty =
		platform_get_irq_byname(pdev, "recv-not-empty");
	if (mbox->irq_recv_not_empty < 0)
		return -ENODEV;

	mbox->irq_send_empty = platform_get_irq_byname(pdev, "send-empty");
	if (mbox->irq_send_empty < 0)
		return -ENODEV;

	mbox->controller.dev = mbox->dev;
	mbox->controller.num_chans = 1;
	mbox->controller.chans = &mbox->chan;
	mbox->controller.ops = &apple_mbox_ops;
	mbox->controller.txdone_irq = true;
	mbox->controller.of_xlate = apple_mbox_of_xlate;
	mbox->chan.con_priv = mbox;

	irqname = devm_kasprintf(dev, GFP_KERNEL, "%s-recv", dev_name(dev));
	if (!irqname)
		return -ENOMEM;

	ret = devm_request_threaded_irq(dev, mbox->irq_recv_not_empty, NULL,
					apple_mbox_recv_irq,
					IRQF_NO_AUTOEN | IRQF_ONESHOT, irqname,
					mbox);
	if (ret)
		return ret;

	irqname = devm_kasprintf(dev, GFP_KERNEL, "%s-send", dev_name(dev));
	if (!irqname)
		return -ENOMEM;

	ret = devm_request_irq(dev, mbox->irq_send_empty,
			       apple_mbox_send_empty_irq, IRQF_NO_AUTOEN,
			       irqname, mbox);
	if (ret)
		return ret;

	return devm_mbox_controller_register(dev, &mbox->controller);
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
