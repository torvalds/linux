// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Inter Processor Interrupt(IPI) Mailbox Driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 */

#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox/zynqmp-ipi-message.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

/* IPI agent ID any */
#define IPI_ID_ANY 0xFFUL

/* indicate if ZynqMP IPI mailbox driver uses SMC calls or HVC calls */
#define USE_SMC 0
#define USE_HVC 1

/* Default IPI SMC function IDs */
#define SMC_IPI_MAILBOX_OPEN		0x82001000U
#define SMC_IPI_MAILBOX_RELEASE		0x82001001U
#define SMC_IPI_MAILBOX_STATUS_ENQUIRY	0x82001002U
#define SMC_IPI_MAILBOX_NOTIFY		0x82001003U
#define SMC_IPI_MAILBOX_ACK		0x82001004U
#define SMC_IPI_MAILBOX_ENABLE_IRQ	0x82001005U
#define SMC_IPI_MAILBOX_DISABLE_IRQ	0x82001006U

/* IPI SMC Macros */
#define IPI_SMC_ENQUIRY_DIRQ_MASK	0x00000001UL /* Flag to indicate if
						      * notification interrupt
						      * to be disabled.
						      */
#define IPI_SMC_ACK_EIRQ_MASK		0x00000001UL /* Flag to indicate if
						      * notification interrupt
						      * to be enabled.
						      */

/* IPI mailbox status */
#define IPI_MB_STATUS_IDLE		0
#define IPI_MB_STATUS_SEND_PENDING	1
#define IPI_MB_STATUS_RECV_PENDING	2

#define IPI_MB_CHNL_TX	0 /* IPI mailbox TX channel */
#define IPI_MB_CHNL_RX	1 /* IPI mailbox RX channel */

/**
 * struct zynqmp_ipi_mchan - Description of a Xilinx ZynqMP IPI mailbox channel
 * @is_opened: indicate if the IPI channel is opened
 * @req_buf: local to remote request buffer start address
 * @resp_buf: local to remote response buffer start address
 * @req_buf_size: request buffer size
 * @resp_buf_size: response buffer size
 * @rx_buf: receive buffer to pass received message to client
 * @chan_type: channel type
 */
struct zynqmp_ipi_mchan {
	int is_opened;
	void __iomem *req_buf;
	void __iomem *resp_buf;
	void *rx_buf;
	size_t req_buf_size;
	size_t resp_buf_size;
	unsigned int chan_type;
};

/**
 * struct zynqmp_ipi_mbox - Description of a ZynqMP IPI mailbox
 *                          platform data.
 * @pdata:		  pointer to the IPI private data
 * @dev:                  device pointer corresponding to the Xilinx ZynqMP
 *                        IPI mailbox
 * @remote_id:            remote IPI agent ID
 * @mbox:                 mailbox Controller
 * @mchans:               array for channels, tx channel and rx channel.
 * @irq:                  IPI agent interrupt ID
 */
struct zynqmp_ipi_mbox {
	struct zynqmp_ipi_pdata *pdata;
	struct device dev;
	u32 remote_id;
	struct mbox_controller mbox;
	struct zynqmp_ipi_mchan mchans[2];
};

/**
 * struct zynqmp_ipi_pdata - Description of z ZynqMP IPI agent platform data.
 *
 * @dev:                  device pointer corresponding to the Xilinx ZynqMP
 *                        IPI agent
 * @irq:                  IPI agent interrupt ID
 * @method:               IPI SMC or HVC is going to be used
 * @local_id:             local IPI agent ID
 * @num_mboxes:           number of mailboxes of this IPI agent
 * @ipi_mboxes:           IPI mailboxes of this IPI agent
 */
struct zynqmp_ipi_pdata {
	struct device *dev;
	int irq;
	unsigned int method;
	u32 local_id;
	int num_mboxes;
	struct zynqmp_ipi_mbox ipi_mboxes[] __counted_by(num_mboxes);
};

static struct device_driver zynqmp_ipi_mbox_driver = {
	.owner = THIS_MODULE,
	.name = "zynqmp-ipi-mbox",
};

static void zynqmp_ipi_fw_call(struct zynqmp_ipi_mbox *ipi_mbox,
			       unsigned long a0, unsigned long a3,
			       struct arm_smccc_res *res)
{
	struct zynqmp_ipi_pdata *pdata = ipi_mbox->pdata;
	unsigned long a1, a2;

	a1 = pdata->local_id;
	a2 = ipi_mbox->remote_id;
	if (pdata->method == USE_SMC)
		arm_smccc_smc(a0, a1, a2, a3, 0, 0, 0, 0, res);
	else
		arm_smccc_hvc(a0, a1, a2, a3, 0, 0, 0, 0, res);
}

/**
 * zynqmp_ipi_interrupt - Interrupt handler for IPI notification
 *
 * @irq:  Interrupt number
 * @data: ZynqMP IPI mailbox platform data.
 *
 * Return: -EINVAL if there is no instance
 * IRQ_NONE if the interrupt is not ours.
 * IRQ_HANDLED if the rx interrupt was successfully handled.
 */
static irqreturn_t zynqmp_ipi_interrupt(int irq, void *data)
{
	struct zynqmp_ipi_pdata *pdata = data;
	struct mbox_chan *chan;
	struct zynqmp_ipi_mbox *ipi_mbox;
	struct zynqmp_ipi_mchan *mchan;
	struct zynqmp_ipi_message *msg;
	u64 arg0, arg3;
	struct arm_smccc_res res;
	int ret, i, status = IRQ_NONE;

	(void)irq;
	arg0 = SMC_IPI_MAILBOX_STATUS_ENQUIRY;
	arg3 = IPI_SMC_ENQUIRY_DIRQ_MASK;
	for (i = 0; i < pdata->num_mboxes; i++) {
		ipi_mbox = &pdata->ipi_mboxes[i];
		mchan = &ipi_mbox->mchans[IPI_MB_CHNL_RX];
		chan = &ipi_mbox->mbox.chans[IPI_MB_CHNL_RX];
		zynqmp_ipi_fw_call(ipi_mbox, arg0, arg3, &res);
		ret = (int)(res.a0 & 0xFFFFFFFF);
		if (ret > 0 && ret & IPI_MB_STATUS_RECV_PENDING) {
			if (mchan->is_opened) {
				msg = mchan->rx_buf;
				msg->len = mchan->req_buf_size;
				memcpy_fromio(msg->data, mchan->req_buf,
					      msg->len);
				mbox_chan_received_data(chan, (void *)msg);
				status = IRQ_HANDLED;
			}
		}
	}
	return status;
}

/**
 * zynqmp_ipi_peek_data - Peek to see if there are any rx messages.
 *
 * @chan: Channel Pointer
 *
 * Return: 'true' if there is pending rx data, 'false' if there is none.
 */
static bool zynqmp_ipi_peek_data(struct mbox_chan *chan)
{
	struct device *dev = chan->mbox->dev;
	struct zynqmp_ipi_mbox *ipi_mbox = dev_get_drvdata(dev);
	struct zynqmp_ipi_mchan *mchan = chan->con_priv;
	int ret;
	u64 arg0;
	struct arm_smccc_res res;

	if (WARN_ON(!ipi_mbox)) {
		dev_err(dev, "no platform drv data??\n");
		return false;
	}

	arg0 = SMC_IPI_MAILBOX_STATUS_ENQUIRY;
	zynqmp_ipi_fw_call(ipi_mbox, arg0, 0, &res);
	ret = (int)(res.a0 & 0xFFFFFFFF);

	if (mchan->chan_type == IPI_MB_CHNL_TX) {
		/* TX channel, check if the message has been acked
		 * by the remote, if yes, response is available.
		 */
		if (ret < 0 || ret & IPI_MB_STATUS_SEND_PENDING)
			return false;
		else
			return true;
	} else if (ret > 0 && ret & IPI_MB_STATUS_RECV_PENDING) {
		/* RX channel, check if there is message arrived. */
		return true;
	}
	return false;
}

/**
 * zynqmp_ipi_last_tx_done - See if the last tx message is sent
 *
 * @chan: Channel pointer
 *
 * Return: 'true' is no pending tx data, 'false' if there are any.
 */
static bool zynqmp_ipi_last_tx_done(struct mbox_chan *chan)
{
	struct device *dev = chan->mbox->dev;
	struct zynqmp_ipi_mbox *ipi_mbox = dev_get_drvdata(dev);
	struct zynqmp_ipi_mchan *mchan = chan->con_priv;
	int ret;
	u64 arg0;
	struct arm_smccc_res res;

	if (WARN_ON(!ipi_mbox)) {
		dev_err(dev, "no platform drv data??\n");
		return false;
	}

	if (mchan->chan_type == IPI_MB_CHNL_TX) {
		/* We only need to check if the message been taken
		 * by the remote in the TX channel
		 */
		arg0 = SMC_IPI_MAILBOX_STATUS_ENQUIRY;
		zynqmp_ipi_fw_call(ipi_mbox, arg0, 0, &res);
		/* Check the SMC call status, a0 of the result */
		ret = (int)(res.a0 & 0xFFFFFFFF);
		if (ret < 0 || ret & IPI_MB_STATUS_SEND_PENDING)
			return false;
		return true;
	}
	/* Always true for the response message in RX channel */
	return true;
}

/**
 * zynqmp_ipi_send_data - Send data
 *
 * @chan: Channel Pointer
 * @data: Message Pointer
 *
 * Return: 0 if all goes good, else appropriate error messages.
 */
static int zynqmp_ipi_send_data(struct mbox_chan *chan, void *data)
{
	struct device *dev = chan->mbox->dev;
	struct zynqmp_ipi_mbox *ipi_mbox = dev_get_drvdata(dev);
	struct zynqmp_ipi_mchan *mchan = chan->con_priv;
	struct zynqmp_ipi_message *msg = data;
	u64 arg0;
	struct arm_smccc_res res;

	if (WARN_ON(!ipi_mbox)) {
		dev_err(dev, "no platform drv data??\n");
		return -EINVAL;
	}

	if (mchan->chan_type == IPI_MB_CHNL_TX) {
		/* Send request message */
		if (msg && msg->len > mchan->req_buf_size) {
			dev_err(dev, "channel %d message length %u > max %lu\n",
				mchan->chan_type, (unsigned int)msg->len,
				mchan->req_buf_size);
			return -EINVAL;
		}
		if (msg && msg->len)
			memcpy_toio(mchan->req_buf, msg->data, msg->len);
		/* Kick IPI mailbox to send message */
		arg0 = SMC_IPI_MAILBOX_NOTIFY;
		zynqmp_ipi_fw_call(ipi_mbox, arg0, 0, &res);
	} else {
		/* Send response message */
		if (msg && msg->len > mchan->resp_buf_size) {
			dev_err(dev, "channel %d message length %u > max %lu\n",
				mchan->chan_type, (unsigned int)msg->len,
				mchan->resp_buf_size);
			return -EINVAL;
		}
		if (msg && msg->len)
			memcpy_toio(mchan->resp_buf, msg->data, msg->len);
		arg0 = SMC_IPI_MAILBOX_ACK;
		zynqmp_ipi_fw_call(ipi_mbox, arg0, IPI_SMC_ACK_EIRQ_MASK,
				   &res);
	}
	return 0;
}

/**
 * zynqmp_ipi_startup - Startup the IPI channel
 *
 * @chan: Channel pointer
 *
 * Return: 0 if all goes good, else return corresponding error message
 */
static int zynqmp_ipi_startup(struct mbox_chan *chan)
{
	struct device *dev = chan->mbox->dev;
	struct zynqmp_ipi_mbox *ipi_mbox = dev_get_drvdata(dev);
	struct zynqmp_ipi_mchan *mchan = chan->con_priv;
	u64 arg0;
	struct arm_smccc_res res;
	int ret = 0;
	unsigned int nchan_type;

	if (mchan->is_opened)
		return 0;

	/* If no channel has been opened, open the IPI mailbox */
	nchan_type = (mchan->chan_type + 1) % 2;
	if (!ipi_mbox->mchans[nchan_type].is_opened) {
		arg0 = SMC_IPI_MAILBOX_OPEN;
		zynqmp_ipi_fw_call(ipi_mbox, arg0, 0, &res);
		/* Check the SMC call status, a0 of the result */
		ret = (int)(res.a0 & 0xFFFFFFFF);
		if (ret < 0) {
			dev_err(dev, "SMC to open the IPI channel failed.\n");
			return ret;
		}
		ret = 0;
	}

	/* If it is RX channel, enable the IPI notification interrupt */
	if (mchan->chan_type == IPI_MB_CHNL_RX) {
		arg0 = SMC_IPI_MAILBOX_ENABLE_IRQ;
		zynqmp_ipi_fw_call(ipi_mbox, arg0, 0, &res);
	}
	mchan->is_opened = 1;

	return ret;
}

/**
 * zynqmp_ipi_shutdown - Shutdown the IPI channel
 *
 * @chan: Channel pointer
 */
static void zynqmp_ipi_shutdown(struct mbox_chan *chan)
{
	struct device *dev = chan->mbox->dev;
	struct zynqmp_ipi_mbox *ipi_mbox = dev_get_drvdata(dev);
	struct zynqmp_ipi_mchan *mchan = chan->con_priv;
	u64 arg0;
	struct arm_smccc_res res;
	unsigned int chan_type;

	if (!mchan->is_opened)
		return;

	/* If it is RX channel, disable notification interrupt */
	chan_type = mchan->chan_type;
	if (chan_type == IPI_MB_CHNL_RX) {
		arg0 = SMC_IPI_MAILBOX_DISABLE_IRQ;
		zynqmp_ipi_fw_call(ipi_mbox, arg0, 0, &res);
	}
	/* Release IPI mailbox if no other channel is opened */
	chan_type = (chan_type + 1) % 2;
	if (!ipi_mbox->mchans[chan_type].is_opened) {
		arg0 = SMC_IPI_MAILBOX_RELEASE;
		zynqmp_ipi_fw_call(ipi_mbox, arg0, 0, &res);
	}

	mchan->is_opened = 0;
}

/* ZynqMP IPI mailbox operations */
static const struct mbox_chan_ops zynqmp_ipi_chan_ops = {
	.startup = zynqmp_ipi_startup,
	.shutdown = zynqmp_ipi_shutdown,
	.peek_data = zynqmp_ipi_peek_data,
	.last_tx_done = zynqmp_ipi_last_tx_done,
	.send_data = zynqmp_ipi_send_data,
};

/**
 * zynqmp_ipi_of_xlate - Translate of phandle to IPI mailbox channel
 *
 * @mbox: mailbox controller pointer
 * @p:    phandle pointer
 *
 * Return: Mailbox channel, else return error pointer.
 */
static struct mbox_chan *zynqmp_ipi_of_xlate(struct mbox_controller *mbox,
					     const struct of_phandle_args *p)
{
	struct mbox_chan *chan;
	struct device *dev = mbox->dev;
	unsigned int chan_type;

	/* Only supports TX and RX channels */
	chan_type = p->args[0];
	if (chan_type != IPI_MB_CHNL_TX && chan_type != IPI_MB_CHNL_RX) {
		dev_err(dev, "req chnl failure: invalid chnl type %u.\n",
			chan_type);
		return ERR_PTR(-EINVAL);
	}
	chan = &mbox->chans[chan_type];
	return chan;
}

static const struct of_device_id zynqmp_ipi_of_match[] = {
	{ .compatible = "xlnx,zynqmp-ipi-mailbox" },
	{},
};
MODULE_DEVICE_TABLE(of, zynqmp_ipi_of_match);

/**
 * zynqmp_ipi_mbox_get_buf_res - Get buffer resource from the IPI dev node
 *
 * @node: IPI mbox device child node
 * @name: name of the IPI buffer
 * @res: pointer to where the resource information will be stored.
 *
 * Return: 0 for success, negative value for failure
 */
static int zynqmp_ipi_mbox_get_buf_res(struct device_node *node,
				       const char *name,
				       struct resource *res)
{
	int ret, index;

	index = of_property_match_string(node, "reg-names", name);
	if (index >= 0) {
		ret = of_address_to_resource(node, index, res);
		if (ret < 0)
			return -EINVAL;
		return 0;
	}
	return -ENODEV;
}

/**
 * zynqmp_ipi_mbox_dev_release() - release the existence of a ipi mbox dev
 *
 * @dev: the ipi mailbox device
 *
 * This is to avoid the no device release() function kernel warning.
 *
 */
static void zynqmp_ipi_mbox_dev_release(struct device *dev)
{
	(void)dev;
}

/**
 * zynqmp_ipi_mbox_probe - probe IPI mailbox resource from device node
 *
 * @ipi_mbox: pointer to IPI mailbox private data structure
 * @node: IPI mailbox device node
 *
 * Return: 0 for success, negative value for failure
 */
static int zynqmp_ipi_mbox_probe(struct zynqmp_ipi_mbox *ipi_mbox,
				 struct device_node *node)
{
	struct zynqmp_ipi_mchan *mchan;
	struct mbox_chan *chans;
	struct mbox_controller *mbox;
	struct resource res;
	struct device *dev, *mdev;
	const char *name;
	int ret;

	dev = ipi_mbox->pdata->dev;
	/* Initialize dev for IPI mailbox */
	ipi_mbox->dev.parent = dev;
	ipi_mbox->dev.release = NULL;
	ipi_mbox->dev.of_node = node;
	dev_set_name(&ipi_mbox->dev, "%s", of_node_full_name(node));
	dev_set_drvdata(&ipi_mbox->dev, ipi_mbox);
	ipi_mbox->dev.release = zynqmp_ipi_mbox_dev_release;
	ipi_mbox->dev.driver = &zynqmp_ipi_mbox_driver;
	ret = device_register(&ipi_mbox->dev);
	if (ret) {
		dev_err(dev, "Failed to register ipi mbox dev.\n");
		put_device(&ipi_mbox->dev);
		return ret;
	}
	mdev = &ipi_mbox->dev;

	mchan = &ipi_mbox->mchans[IPI_MB_CHNL_TX];
	name = "local_request_region";
	ret = zynqmp_ipi_mbox_get_buf_res(node, name, &res);
	if (!ret) {
		mchan->req_buf_size = resource_size(&res);
		mchan->req_buf = devm_ioremap(mdev, res.start,
					      mchan->req_buf_size);
		if (!mchan->req_buf) {
			dev_err(mdev, "Unable to map IPI buffer I/O memory\n");
			return -ENOMEM;
		}
	} else if (ret != -ENODEV) {
		dev_err(mdev, "Unmatched resource %s, %d.\n", name, ret);
		return ret;
	}

	name = "remote_response_region";
	ret = zynqmp_ipi_mbox_get_buf_res(node, name, &res);
	if (!ret) {
		mchan->resp_buf_size = resource_size(&res);
		mchan->resp_buf = devm_ioremap(mdev, res.start,
					       mchan->resp_buf_size);
		if (!mchan->resp_buf) {
			dev_err(mdev, "Unable to map IPI buffer I/O memory\n");
			return -ENOMEM;
		}
	} else if (ret != -ENODEV) {
		dev_err(mdev, "Unmatched resource %s.\n", name);
		return ret;
	}
	mchan->rx_buf = devm_kzalloc(mdev,
				     mchan->resp_buf_size +
				     sizeof(struct zynqmp_ipi_message),
				     GFP_KERNEL);
	if (!mchan->rx_buf)
		return -ENOMEM;

	mchan = &ipi_mbox->mchans[IPI_MB_CHNL_RX];
	name = "remote_request_region";
	ret = zynqmp_ipi_mbox_get_buf_res(node, name, &res);
	if (!ret) {
		mchan->req_buf_size = resource_size(&res);
		mchan->req_buf = devm_ioremap(mdev, res.start,
					      mchan->req_buf_size);
		if (!mchan->req_buf) {
			dev_err(mdev, "Unable to map IPI buffer I/O memory\n");
			return -ENOMEM;
		}
	} else if (ret != -ENODEV) {
		dev_err(mdev, "Unmatched resource %s.\n", name);
		return ret;
	}

	name = "local_response_region";
	ret = zynqmp_ipi_mbox_get_buf_res(node, name, &res);
	if (!ret) {
		mchan->resp_buf_size = resource_size(&res);
		mchan->resp_buf = devm_ioremap(mdev, res.start,
					       mchan->resp_buf_size);
		if (!mchan->resp_buf) {
			dev_err(mdev, "Unable to map IPI buffer I/O memory\n");
			return -ENOMEM;
		}
	} else if (ret != -ENODEV) {
		dev_err(mdev, "Unmatched resource %s.\n", name);
		return ret;
	}
	mchan->rx_buf = devm_kzalloc(mdev,
				     mchan->resp_buf_size +
				     sizeof(struct zynqmp_ipi_message),
				     GFP_KERNEL);
	if (!mchan->rx_buf)
		return -ENOMEM;

	/* Get the IPI remote agent ID */
	ret = of_property_read_u32(node, "xlnx,ipi-id", &ipi_mbox->remote_id);
	if (ret < 0) {
		dev_err(dev, "No IPI remote ID is specified.\n");
		return ret;
	}

	mbox = &ipi_mbox->mbox;
	mbox->dev = mdev;
	mbox->ops = &zynqmp_ipi_chan_ops;
	mbox->num_chans = 2;
	mbox->txdone_irq = false;
	mbox->txdone_poll = true;
	mbox->txpoll_period = 5;
	mbox->of_xlate = zynqmp_ipi_of_xlate;
	chans = devm_kzalloc(mdev, 2 * sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return -ENOMEM;
	mbox->chans = chans;
	chans[IPI_MB_CHNL_TX].con_priv = &ipi_mbox->mchans[IPI_MB_CHNL_TX];
	chans[IPI_MB_CHNL_RX].con_priv = &ipi_mbox->mchans[IPI_MB_CHNL_RX];
	ipi_mbox->mchans[IPI_MB_CHNL_TX].chan_type = IPI_MB_CHNL_TX;
	ipi_mbox->mchans[IPI_MB_CHNL_RX].chan_type = IPI_MB_CHNL_RX;
	ret = devm_mbox_controller_register(mdev, mbox);
	if (ret)
		dev_err(mdev,
			"Failed to register mbox_controller(%d)\n", ret);
	else
		dev_info(mdev,
			 "Registered ZynqMP IPI mbox with TX/RX channels.\n");
	return ret;
}

/**
 * zynqmp_ipi_free_mboxes - Free IPI mailboxes devices
 *
 * @pdata: IPI private data
 */
static void zynqmp_ipi_free_mboxes(struct zynqmp_ipi_pdata *pdata)
{
	struct zynqmp_ipi_mbox *ipi_mbox;
	int i;

	i = pdata->num_mboxes;
	for (; i >= 0; i--) {
		ipi_mbox = &pdata->ipi_mboxes[i];
		if (ipi_mbox->dev.parent) {
			mbox_controller_unregister(&ipi_mbox->mbox);
			if (device_is_registered(&ipi_mbox->dev))
				device_unregister(&ipi_mbox->dev);
		}
	}
}

static int zynqmp_ipi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *nc, *np = pdev->dev.of_node;
	struct zynqmp_ipi_pdata *pdata;
	struct zynqmp_ipi_mbox *mbox;
	int num_mboxes, ret = -EINVAL;

	num_mboxes = of_get_available_child_count(np);
	if (num_mboxes == 0) {
		dev_err(dev, "mailbox nodes not available\n");
		return -EINVAL;
	}

	pdata = devm_kzalloc(dev, struct_size(pdata, ipi_mboxes, num_mboxes),
			     GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	pdata->dev = dev;

	/* Get the IPI local agents ID */
	ret = of_property_read_u32(np, "xlnx,ipi-id", &pdata->local_id);
	if (ret < 0) {
		dev_err(dev, "No IPI local ID is specified.\n");
		return ret;
	}

	pdata->num_mboxes = num_mboxes;

	mbox = pdata->ipi_mboxes;
	for_each_available_child_of_node(np, nc) {
		mbox->pdata = pdata;
		ret = zynqmp_ipi_mbox_probe(mbox, nc);
		if (ret) {
			of_node_put(nc);
			dev_err(dev, "failed to probe subdev.\n");
			ret = -EINVAL;
			goto free_mbox_dev;
		}
		mbox++;
	}

	/* IPI IRQ */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto free_mbox_dev;

	pdata->irq = ret;
	ret = devm_request_irq(dev, pdata->irq, zynqmp_ipi_interrupt,
			       IRQF_SHARED, dev_name(dev), pdata);
	if (ret) {
		dev_err(dev, "IRQ %d is not requested successfully.\n",
			pdata->irq);
		goto free_mbox_dev;
	}

	platform_set_drvdata(pdev, pdata);
	return ret;

free_mbox_dev:
	zynqmp_ipi_free_mboxes(pdata);
	return ret;
}

static int zynqmp_ipi_remove(struct platform_device *pdev)
{
	struct zynqmp_ipi_pdata *pdata;

	pdata = platform_get_drvdata(pdev);
	zynqmp_ipi_free_mboxes(pdata);

	return 0;
}

static struct platform_driver zynqmp_ipi_driver = {
	.probe = zynqmp_ipi_probe,
	.remove = zynqmp_ipi_remove,
	.driver = {
		   .name = "zynqmp-ipi",
		   .of_match_table = of_match_ptr(zynqmp_ipi_of_match),
	},
};

static int __init zynqmp_ipi_init(void)
{
	return platform_driver_register(&zynqmp_ipi_driver);
}
subsys_initcall(zynqmp_ipi_init);

static void __exit zynqmp_ipi_exit(void)
{
	platform_driver_unregister(&zynqmp_ipi_driver);
}
module_exit(zynqmp_ipi_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Xilinx ZynqMP IPI Mailbox driver");
MODULE_AUTHOR("Xilinx Inc.");
