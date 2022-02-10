// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments' Message Manager Driver
 *
 * Copyright (C) 2015-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Nishanth Menon
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/soc/ti/ti-msgmgr.h>

#define Q_DATA_OFFSET(proxy, queue, reg)	\
		     ((0x10000 * (proxy)) + (0x80 * (queue)) + ((reg) * 4))
#define Q_STATE_OFFSET(queue)			((queue) * 0x4)
#define Q_STATE_ENTRY_COUNT_MASK		(0xFFF000)

#define SPROXY_THREAD_OFFSET(tid) (0x1000 * (tid))
#define SPROXY_THREAD_DATA_OFFSET(tid, reg) \
	(SPROXY_THREAD_OFFSET(tid) + ((reg) * 0x4) + 0x4)

#define SPROXY_THREAD_STATUS_OFFSET(tid) (SPROXY_THREAD_OFFSET(tid))

#define SPROXY_THREAD_STATUS_COUNT_MASK (0xFF)

#define SPROXY_THREAD_CTRL_OFFSET(tid) (0x1000 + SPROXY_THREAD_OFFSET(tid))
#define SPROXY_THREAD_CTRL_DIR_MASK (0x1 << 31)

/**
 * struct ti_msgmgr_valid_queue_desc - SoC valid queues meant for this processor
 * @queue_id:	Queue Number for this path
 * @proxy_id:	Proxy ID representing the processor in SoC
 * @is_tx:	Is this a receive path?
 */
struct ti_msgmgr_valid_queue_desc {
	u8 queue_id;
	u8 proxy_id;
	bool is_tx;
};

/**
 * struct ti_msgmgr_desc - Description of message manager integration
 * @queue_count:	Number of Queues
 * @max_message_size:	Message size in bytes
 * @max_messages:	Number of messages
 * @data_first_reg:	First data register for proxy data region
 * @data_last_reg:	Last data register for proxy data region
 * @status_cnt_mask:	Mask for getting the status value
 * @status_err_mask:	Mask for getting the error value, if applicable
 * @tx_polled:		Do I need to use polled mechanism for tx
 * @tx_poll_timeout_ms: Timeout in ms if polled
 * @valid_queues:	List of Valid queues that the processor can access
 * @data_region_name:	Name of the proxy data region
 * @status_region_name:	Name of the proxy status region
 * @ctrl_region_name:	Name of the proxy control region
 * @num_valid_queues:	Number of valid queues
 * @is_sproxy:		Is this an Secure Proxy instance?
 *
 * This structure is used in of match data to describe how integration
 * for a specific compatible SoC is done.
 */
struct ti_msgmgr_desc {
	u8 queue_count;
	u8 max_message_size;
	u8 max_messages;
	u8 data_first_reg;
	u8 data_last_reg;
	u32 status_cnt_mask;
	u32 status_err_mask;
	bool tx_polled;
	int tx_poll_timeout_ms;
	const struct ti_msgmgr_valid_queue_desc *valid_queues;
	const char *data_region_name;
	const char *status_region_name;
	const char *ctrl_region_name;
	int num_valid_queues;
	bool is_sproxy;
};

/**
 * struct ti_queue_inst - Description of a queue instance
 * @name:	Queue Name
 * @queue_id:	Queue Identifier as mapped on SoC
 * @proxy_id:	Proxy Identifier as mapped on SoC
 * @irq:	IRQ for Rx Queue
 * @is_tx:	'true' if transmit queue, else, 'false'
 * @queue_buff_start: First register of Data Buffer
 * @queue_buff_end: Last (or confirmation) register of Data buffer
 * @queue_state: Queue status register
 * @queue_ctrl: Queue Control register
 * @chan:	Mailbox channel
 * @rx_buff:	Receive buffer pointer allocated at probe, max_message_size
 * @polled_rx_mode: Use polling for rx instead of interrupts
 */
struct ti_queue_inst {
	char name[30];
	u8 queue_id;
	u8 proxy_id;
	int irq;
	bool is_tx;
	void __iomem *queue_buff_start;
	void __iomem *queue_buff_end;
	void __iomem *queue_state;
	void __iomem *queue_ctrl;
	struct mbox_chan *chan;
	u32 *rx_buff;
	bool polled_rx_mode;
};

/**
 * struct ti_msgmgr_inst - Description of a Message Manager Instance
 * @dev:	device pointer corresponding to the Message Manager instance
 * @desc:	Description of the SoC integration
 * @queue_proxy_region:	Queue proxy region where queue buffers are located
 * @queue_state_debug_region:	Queue status register regions
 * @queue_ctrl_region:	Queue Control register regions
 * @num_valid_queues:	Number of valid queues defined for the processor
 *		Note: other queues are probably reserved for other processors
 *		in the SoC.
 * @qinsts:	Array of valid Queue Instances for the Processor
 * @mbox:	Mailbox Controller
 * @chans:	Array for channels corresponding to the Queue Instances.
 */
struct ti_msgmgr_inst {
	struct device *dev;
	const struct ti_msgmgr_desc *desc;
	void __iomem *queue_proxy_region;
	void __iomem *queue_state_debug_region;
	void __iomem *queue_ctrl_region;
	u8 num_valid_queues;
	struct ti_queue_inst *qinsts;
	struct mbox_controller mbox;
	struct mbox_chan *chans;
};

/**
 * ti_msgmgr_queue_get_num_messages() - Get the number of pending messages
 * @d:		Description of message manager
 * @qinst:	Queue instance for which we check the number of pending messages
 *
 * Return: number of messages pending in the queue (0 == no pending messages)
 */
static inline int
ti_msgmgr_queue_get_num_messages(const struct ti_msgmgr_desc *d,
				 struct ti_queue_inst *qinst)
{
	u32 val;
	u32 status_cnt_mask = d->status_cnt_mask;

	/*
	 * We cannot use relaxed operation here - update may happen
	 * real-time.
	 */
	val = readl(qinst->queue_state) & status_cnt_mask;
	val >>= __ffs(status_cnt_mask);

	return val;
}

/**
 * ti_msgmgr_queue_is_error() - Check to see if there is queue error
 * @d:		Description of message manager
 * @qinst:	Queue instance for which we check the number of pending messages
 *
 * Return: true if error, else false
 */
static inline bool ti_msgmgr_queue_is_error(const struct ti_msgmgr_desc *d,
					    struct ti_queue_inst *qinst)
{
	u32 val;

	/* Msgmgr has no error detection */
	if (!d->is_sproxy)
		return false;

	/*
	 * We cannot use relaxed operation here - update may happen
	 * real-time.
	 */
	val = readl(qinst->queue_state) & d->status_err_mask;

	return val ? true : false;
}

static int ti_msgmgr_queue_rx_data(struct mbox_chan *chan, struct ti_queue_inst *qinst,
				   const struct ti_msgmgr_desc *desc)
{
	int num_words;
	struct ti_msgmgr_message message;
	void __iomem *data_reg;
	u32 *word_data;

	/*
	 * I have no idea about the protocol being used to communicate with the
	 * remote producer - 0 could be valid data, so I wont make a judgement
	 * of how many bytes I should be reading. Let the client figure this
	 * out.. I just read the full message and pass it on..
	 */
	message.len = desc->max_message_size;
	message.buf = (u8 *)qinst->rx_buff;

	/*
	 * NOTE about register access involved here:
	 * the hardware block is implemented with 32bit access operations and no
	 * support for data splitting.  We don't want the hardware to misbehave
	 * with sub 32bit access - For example: if the last register read is
	 * split into byte wise access, it can result in the queue getting
	 * stuck or indeterminate behavior. An out of order read operation may
	 * result in weird data results as well.
	 * Hence, we do not use memcpy_fromio or __ioread32_copy here, instead
	 * we depend on readl for the purpose.
	 *
	 * Also note that the final register read automatically marks the
	 * queue message as read.
	 */
	for (data_reg = qinst->queue_buff_start, word_data = qinst->rx_buff,
	     num_words = (desc->max_message_size / sizeof(u32));
	     num_words; num_words--, data_reg += sizeof(u32), word_data++)
		*word_data = readl(data_reg);

	/*
	 * Last register read automatically clears the IRQ if only 1 message
	 * is pending - so send the data up the stack..
	 * NOTE: Client is expected to be as optimal as possible, since
	 * we invoke the handler in IRQ context.
	 */
	mbox_chan_received_data(chan, (void *)&message);

	return 0;
}

static int ti_msgmgr_queue_rx_poll_timeout(struct mbox_chan *chan, int timeout_us)
{
	struct device *dev = chan->mbox->dev;
	struct ti_msgmgr_inst *inst = dev_get_drvdata(dev);
	struct ti_queue_inst *qinst = chan->con_priv;
	const struct ti_msgmgr_desc *desc = inst->desc;
	int msg_count;
	int ret;

	ret = readl_poll_timeout_atomic(qinst->queue_state, msg_count,
					(msg_count & desc->status_cnt_mask),
					10, timeout_us);
	if (ret != 0)
		return ret;

	ti_msgmgr_queue_rx_data(chan, qinst, desc);

	return 0;
}

/**
 * ti_msgmgr_queue_rx_interrupt() - Interrupt handler for receive Queue
 * @irq:	Interrupt number
 * @p:		Channel Pointer
 *
 * Return: -EINVAL if there is no instance
 * IRQ_NONE if the interrupt is not ours.
 * IRQ_HANDLED if the rx interrupt was successfully handled.
 */
static irqreturn_t ti_msgmgr_queue_rx_interrupt(int irq, void *p)
{
	struct mbox_chan *chan = p;
	struct device *dev = chan->mbox->dev;
	struct ti_msgmgr_inst *inst = dev_get_drvdata(dev);
	struct ti_queue_inst *qinst = chan->con_priv;
	const struct ti_msgmgr_desc *desc;
	int msg_count;

	if (WARN_ON(!inst)) {
		dev_err(dev, "no platform drv data??\n");
		return -EINVAL;
	}

	/* Do I have an invalid interrupt source? */
	if (qinst->is_tx) {
		dev_err(dev, "Cannot handle rx interrupt on tx channel %s\n",
			qinst->name);
		return IRQ_NONE;
	}

	desc = inst->desc;
	if (ti_msgmgr_queue_is_error(desc, qinst)) {
		dev_err(dev, "Error on Rx channel %s\n", qinst->name);
		return IRQ_NONE;
	}

	/* Do I actually have messages to read? */
	msg_count = ti_msgmgr_queue_get_num_messages(desc, qinst);
	if (!msg_count) {
		/* Shared IRQ? */
		dev_dbg(dev, "Spurious event - 0 pending data!\n");
		return IRQ_NONE;
	}

	ti_msgmgr_queue_rx_data(chan, qinst, desc);

	return IRQ_HANDLED;
}

/**
 * ti_msgmgr_queue_peek_data() - Peek to see if there are any rx messages.
 * @chan:	Channel Pointer
 *
 * Return: 'true' if there is pending rx data, 'false' if there is none.
 */
static bool ti_msgmgr_queue_peek_data(struct mbox_chan *chan)
{
	struct ti_queue_inst *qinst = chan->con_priv;
	struct device *dev = chan->mbox->dev;
	struct ti_msgmgr_inst *inst = dev_get_drvdata(dev);
	const struct ti_msgmgr_desc *desc = inst->desc;
	int msg_count;

	if (qinst->is_tx)
		return false;

	if (ti_msgmgr_queue_is_error(desc, qinst)) {
		dev_err(dev, "Error on channel %s\n", qinst->name);
		return false;
	}

	msg_count = ti_msgmgr_queue_get_num_messages(desc, qinst);

	return msg_count ? true : false;
}

/**
 * ti_msgmgr_last_tx_done() - See if all the tx messages are sent
 * @chan:	Channel pointer
 *
 * Return: 'true' is no pending tx data, 'false' if there are any.
 */
static bool ti_msgmgr_last_tx_done(struct mbox_chan *chan)
{
	struct ti_queue_inst *qinst = chan->con_priv;
	struct device *dev = chan->mbox->dev;
	struct ti_msgmgr_inst *inst = dev_get_drvdata(dev);
	const struct ti_msgmgr_desc *desc = inst->desc;
	int msg_count;

	if (!qinst->is_tx)
		return false;

	if (ti_msgmgr_queue_is_error(desc, qinst)) {
		dev_err(dev, "Error on channel %s\n", qinst->name);
		return false;
	}

	msg_count = ti_msgmgr_queue_get_num_messages(desc, qinst);

	if (desc->is_sproxy) {
		/* In secure proxy, msg_count indicates how many we can send */
		return msg_count ? true : false;
	}

	/* if we have any messages pending.. */
	return msg_count ? false : true;
}

static bool ti_msgmgr_chan_has_polled_queue_rx(struct mbox_chan *chan)
{
	struct ti_queue_inst *qinst;

	if (!chan)
		return false;

	qinst = chan->con_priv;
	return qinst->polled_rx_mode;
}

/**
 * ti_msgmgr_send_data() - Send data
 * @chan:	Channel Pointer
 * @data:	ti_msgmgr_message * Message Pointer
 *
 * Return: 0 if all goes good, else appropriate error messages.
 */
static int ti_msgmgr_send_data(struct mbox_chan *chan, void *data)
{
	struct device *dev = chan->mbox->dev;
	struct ti_msgmgr_inst *inst = dev_get_drvdata(dev);
	const struct ti_msgmgr_desc *desc;
	struct ti_queue_inst *qinst = chan->con_priv;
	int num_words, trail_bytes;
	struct ti_msgmgr_message *message = data;
	void __iomem *data_reg;
	u32 *word_data;
	int ret = 0;

	if (WARN_ON(!inst)) {
		dev_err(dev, "no platform drv data??\n");
		return -EINVAL;
	}
	desc = inst->desc;

	if (ti_msgmgr_queue_is_error(desc, qinst)) {
		dev_err(dev, "Error on channel %s\n", qinst->name);
		return false;
	}

	if (desc->max_message_size < message->len) {
		dev_err(dev, "Queue %s message length %zu > max %d\n",
			qinst->name, message->len, desc->max_message_size);
		return -EINVAL;
	}

	/* NOTE: Constraints similar to rx path exists here as well */
	for (data_reg = qinst->queue_buff_start,
	     num_words = message->len / sizeof(u32),
	     word_data = (u32 *)message->buf;
	     num_words; num_words--, data_reg += sizeof(u32), word_data++)
		writel(*word_data, data_reg);

	trail_bytes = message->len % sizeof(u32);
	if (trail_bytes) {
		u32 data_trail = *word_data;

		/* Ensure all unused data is 0 */
		data_trail &= 0xFFFFFFFF >> (8 * (sizeof(u32) - trail_bytes));
		writel(data_trail, data_reg);
		data_reg++;
	}
	/*
	 * 'data_reg' indicates next register to write. If we did not already
	 * write on tx complete reg(last reg), we must do so for transmit
	 */
	if (data_reg <= qinst->queue_buff_end)
		writel(0, qinst->queue_buff_end);

	/* If we are in polled mode, wait for a response before proceeding */
	if (ti_msgmgr_chan_has_polled_queue_rx(message->chan_rx))
		ret = ti_msgmgr_queue_rx_poll_timeout(message->chan_rx,
						      message->timeout_rx_ms * 1000);

	return ret;
}

/**
 *  ti_msgmgr_queue_rx_irq_req() - RX IRQ request
 *  @dev:	device pointer
 *  @d:		descriptor for ti_msgmgr
 *  @qinst:	Queue instance
 *  @chan:	Channel pointer
 */
static int ti_msgmgr_queue_rx_irq_req(struct device *dev,
				      const struct ti_msgmgr_desc *d,
				      struct ti_queue_inst *qinst,
				      struct mbox_chan *chan)
{
	int ret = 0;
	char of_rx_irq_name[7];
	struct device_node *np;

	snprintf(of_rx_irq_name, sizeof(of_rx_irq_name),
		 "rx_%03d", d->is_sproxy ? qinst->proxy_id : qinst->queue_id);

	/* Get the IRQ if not found */
	if (qinst->irq < 0) {
		np = of_node_get(dev->of_node);
		if (!np)
			return -ENODATA;
		qinst->irq = of_irq_get_byname(np, of_rx_irq_name);
		of_node_put(np);

		if (qinst->irq < 0) {
			dev_err(dev,
				"QID %d PID %d:No IRQ[%s]: %d\n",
				qinst->queue_id, qinst->proxy_id,
				of_rx_irq_name, qinst->irq);
			return qinst->irq;
		}
	}

	/* With the expectation that the IRQ might be shared in SoC */
	ret = request_irq(qinst->irq, ti_msgmgr_queue_rx_interrupt,
			  IRQF_SHARED, qinst->name, chan);
	if (ret) {
		dev_err(dev, "Unable to get IRQ %d on %s(res=%d)\n",
			qinst->irq, qinst->name, ret);
	}

	return ret;
}

/**
 * ti_msgmgr_queue_startup() - Startup queue
 * @chan:	Channel pointer
 *
 * Return: 0 if all goes good, else return corresponding error message
 */
static int ti_msgmgr_queue_startup(struct mbox_chan *chan)
{
	struct device *dev = chan->mbox->dev;
	struct ti_msgmgr_inst *inst = dev_get_drvdata(dev);
	struct ti_queue_inst *qinst = chan->con_priv;
	const struct ti_msgmgr_desc *d = inst->desc;
	int ret;
	int msg_count;

	/*
	 * If sproxy is starting and can send messages, we are a Tx thread,
	 * else Rx
	 */
	if (d->is_sproxy) {
		qinst->is_tx = (readl(qinst->queue_ctrl) &
				SPROXY_THREAD_CTRL_DIR_MASK) ? false : true;

		msg_count = ti_msgmgr_queue_get_num_messages(d, qinst);

		if (!msg_count && qinst->is_tx) {
			dev_err(dev, "%s: Cannot transmit with 0 credits!\n",
				qinst->name);
			return -EINVAL;
		}
	}

	if (!qinst->is_tx) {
		/* Allocate usage buffer for rx */
		qinst->rx_buff = kzalloc(d->max_message_size, GFP_KERNEL);
		if (!qinst->rx_buff)
			return -ENOMEM;
		/* Request IRQ */
		ret = ti_msgmgr_queue_rx_irq_req(dev, d, qinst, chan);
		if (ret) {
			kfree(qinst->rx_buff);
			return ret;
		}
	}

	return 0;
}

/**
 * ti_msgmgr_queue_shutdown() - Shutdown the queue
 * @chan:	Channel pointer
 */
static void ti_msgmgr_queue_shutdown(struct mbox_chan *chan)
{
	struct ti_queue_inst *qinst = chan->con_priv;

	if (!qinst->is_tx) {
		free_irq(qinst->irq, chan);
		kfree(qinst->rx_buff);
	}
}

/**
 * ti_msgmgr_of_xlate() - Translation of phandle to queue
 * @mbox:	Mailbox controller
 * @p:		phandle pointer
 *
 * Return: Mailbox channel corresponding to the queue, else return error
 * pointer.
 */
static struct mbox_chan *ti_msgmgr_of_xlate(struct mbox_controller *mbox,
					    const struct of_phandle_args *p)
{
	struct ti_msgmgr_inst *inst;
	int req_qid, req_pid;
	struct ti_queue_inst *qinst;
	const struct ti_msgmgr_desc *d;
	int i, ncells;

	inst = container_of(mbox, struct ti_msgmgr_inst, mbox);
	if (WARN_ON(!inst))
		return ERR_PTR(-EINVAL);

	d = inst->desc;

	if (d->is_sproxy)
		ncells = 1;
	else
		ncells = 2;
	if (p->args_count != ncells) {
		dev_err(inst->dev, "Invalid arguments in dt[%d]. Must be %d\n",
			p->args_count, ncells);
		return ERR_PTR(-EINVAL);
	}
	if (ncells == 1) {
		req_qid = 0;
		req_pid = p->args[0];
	} else {
		req_qid = p->args[0];
		req_pid = p->args[1];
	}

	if (d->is_sproxy) {
		if (req_pid >= d->num_valid_queues)
			goto err;
		qinst = &inst->qinsts[req_pid];
		return qinst->chan;
	}

	for (qinst = inst->qinsts, i = 0; i < inst->num_valid_queues;
	     i++, qinst++) {
		if (req_qid == qinst->queue_id && req_pid == qinst->proxy_id)
			return qinst->chan;
	}

err:
	dev_err(inst->dev, "Queue ID %d, Proxy ID %d is wrong on %pOFn\n",
		req_qid, req_pid, p->np);
	return ERR_PTR(-ENOENT);
}

/**
 * ti_msgmgr_queue_setup() - Setup data structures for each queue instance
 * @idx:	index of the queue
 * @dev:	pointer to the message manager device
 * @np:		pointer to the of node
 * @inst:	Queue instance pointer
 * @d:		Message Manager instance description data
 * @qd:		Queue description data
 * @qinst:	Queue instance pointer
 * @chan:	pointer to mailbox channel
 *
 * Return: 0 if all went well, else return corresponding error
 */
static int ti_msgmgr_queue_setup(int idx, struct device *dev,
				 struct device_node *np,
				 struct ti_msgmgr_inst *inst,
				 const struct ti_msgmgr_desc *d,
				 const struct ti_msgmgr_valid_queue_desc *qd,
				 struct ti_queue_inst *qinst,
				 struct mbox_chan *chan)
{
	char *dir;

	qinst->proxy_id = qd->proxy_id;
	qinst->queue_id = qd->queue_id;

	if (qinst->queue_id > d->queue_count) {
		dev_err(dev, "Queue Data [idx=%d] queuid %d > %d\n",
			idx, qinst->queue_id, d->queue_count);
		return -ERANGE;
	}

	if (d->is_sproxy) {
		qinst->queue_buff_start = inst->queue_proxy_region +
		    SPROXY_THREAD_DATA_OFFSET(qinst->proxy_id,
					      d->data_first_reg);
		qinst->queue_buff_end = inst->queue_proxy_region +
		    SPROXY_THREAD_DATA_OFFSET(qinst->proxy_id,
					      d->data_last_reg);
		qinst->queue_state = inst->queue_state_debug_region +
		    SPROXY_THREAD_STATUS_OFFSET(qinst->proxy_id);
		qinst->queue_ctrl = inst->queue_ctrl_region +
		    SPROXY_THREAD_CTRL_OFFSET(qinst->proxy_id);

		/* XXX: DONOT read registers here!.. Some may be unusable */
		dir = "thr";
		snprintf(qinst->name, sizeof(qinst->name), "%s %s_%03d",
			 dev_name(dev), dir, qinst->proxy_id);
	} else {
		qinst->queue_buff_start = inst->queue_proxy_region +
		    Q_DATA_OFFSET(qinst->proxy_id, qinst->queue_id,
				  d->data_first_reg);
		qinst->queue_buff_end = inst->queue_proxy_region +
		    Q_DATA_OFFSET(qinst->proxy_id, qinst->queue_id,
				  d->data_last_reg);
		qinst->queue_state =
		    inst->queue_state_debug_region +
		    Q_STATE_OFFSET(qinst->queue_id);
		qinst->is_tx = qd->is_tx;
		dir = qinst->is_tx ? "tx" : "rx";
		snprintf(qinst->name, sizeof(qinst->name), "%s %s_%03d_%03d",
			 dev_name(dev), dir, qinst->queue_id, qinst->proxy_id);
	}

	qinst->chan = chan;

	/* Setup an error value for IRQ - Lazy allocation */
	qinst->irq = -EINVAL;

	chan->con_priv = qinst;

	dev_dbg(dev, "[%d] qidx=%d pidx=%d irq=%d q_s=%p q_e = %p\n",
		idx, qinst->queue_id, qinst->proxy_id, qinst->irq,
		qinst->queue_buff_start, qinst->queue_buff_end);
	return 0;
}

static int ti_msgmgr_queue_rx_set_polled_mode(struct ti_queue_inst *qinst, bool enable)
{
	if (enable) {
		disable_irq(qinst->irq);
		qinst->polled_rx_mode = true;
	} else {
		enable_irq(qinst->irq);
		qinst->polled_rx_mode = false;
	}

	return 0;
}

static int ti_msgmgr_suspend(struct device *dev)
{
	struct ti_msgmgr_inst *inst = dev_get_drvdata(dev);
	struct ti_queue_inst *qinst;
	int i;

	/*
	 * We must switch operation to polled mode now as drivers and the genpd
	 * layer may make late TI SCI calls to change clock and device states
	 * from the noirq phase of suspend.
	 */
	for (qinst = inst->qinsts, i = 0; i < inst->num_valid_queues; qinst++, i++) {
		if (!qinst->is_tx)
			ti_msgmgr_queue_rx_set_polled_mode(qinst, true);
	}

	return 0;
}

static int ti_msgmgr_resume(struct device *dev)
{
	struct ti_msgmgr_inst *inst = dev_get_drvdata(dev);
	struct ti_queue_inst *qinst;
	int i;

	for (qinst = inst->qinsts, i = 0; i < inst->num_valid_queues; qinst++, i++) {
		if (!qinst->is_tx)
			ti_msgmgr_queue_rx_set_polled_mode(qinst, false);
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(ti_msgmgr_pm_ops, ti_msgmgr_suspend, ti_msgmgr_resume);

/* Queue operations */
static const struct mbox_chan_ops ti_msgmgr_chan_ops = {
	.startup = ti_msgmgr_queue_startup,
	.shutdown = ti_msgmgr_queue_shutdown,
	.peek_data = ti_msgmgr_queue_peek_data,
	.last_tx_done = ti_msgmgr_last_tx_done,
	.send_data = ti_msgmgr_send_data,
};

/* Keystone K2G SoC integration details */
static const struct ti_msgmgr_valid_queue_desc k2g_valid_queues[] = {
	{.queue_id = 0, .proxy_id = 0, .is_tx = true,},
	{.queue_id = 1, .proxy_id = 0, .is_tx = true,},
	{.queue_id = 2, .proxy_id = 0, .is_tx = true,},
	{.queue_id = 3, .proxy_id = 0, .is_tx = true,},
	{.queue_id = 5, .proxy_id = 2, .is_tx = false,},
	{.queue_id = 56, .proxy_id = 1, .is_tx = true,},
	{.queue_id = 57, .proxy_id = 2, .is_tx = false,},
	{.queue_id = 58, .proxy_id = 3, .is_tx = true,},
	{.queue_id = 59, .proxy_id = 4, .is_tx = true,},
	{.queue_id = 60, .proxy_id = 5, .is_tx = true,},
	{.queue_id = 61, .proxy_id = 6, .is_tx = true,},
};

static const struct ti_msgmgr_desc k2g_desc = {
	.queue_count = 64,
	.max_message_size = 64,
	.max_messages = 128,
	.data_region_name = "queue_proxy_region",
	.status_region_name = "queue_state_debug_region",
	.data_first_reg = 16,
	.data_last_reg = 31,
	.status_cnt_mask = Q_STATE_ENTRY_COUNT_MASK,
	.tx_polled = false,
	.valid_queues = k2g_valid_queues,
	.num_valid_queues = ARRAY_SIZE(k2g_valid_queues),
	.is_sproxy = false,
};

static const struct ti_msgmgr_desc am654_desc = {
	.queue_count = 190,
	.num_valid_queues = 190,
	.max_message_size = 60,
	.data_region_name = "target_data",
	.status_region_name = "rt",
	.ctrl_region_name = "scfg",
	.data_first_reg = 0,
	.data_last_reg = 14,
	.status_cnt_mask = SPROXY_THREAD_STATUS_COUNT_MASK,
	.tx_polled = false,
	.is_sproxy = true,
};

static const struct of_device_id ti_msgmgr_of_match[] = {
	{.compatible = "ti,k2g-message-manager", .data = &k2g_desc},
	{.compatible = "ti,am654-secure-proxy", .data = &am654_desc},
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, ti_msgmgr_of_match);

static int ti_msgmgr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id;
	struct device_node *np;
	struct resource *res;
	const struct ti_msgmgr_desc *desc;
	struct ti_msgmgr_inst *inst;
	struct ti_queue_inst *qinst;
	struct mbox_controller *mbox;
	struct mbox_chan *chans;
	int queue_count;
	int i;
	int ret = -EINVAL;
	const struct ti_msgmgr_valid_queue_desc *queue_desc;

	if (!dev->of_node) {
		dev_err(dev, "no OF information\n");
		return -EINVAL;
	}
	np = dev->of_node;

	of_id = of_match_device(ti_msgmgr_of_match, dev);
	if (!of_id) {
		dev_err(dev, "OF data missing\n");
		return -EINVAL;
	}
	desc = of_id->data;

	inst = devm_kzalloc(dev, sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	inst->dev = dev;
	inst->desc = desc;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   desc->data_region_name);
	inst->queue_proxy_region = devm_ioremap_resource(dev, res);
	if (IS_ERR(inst->queue_proxy_region))
		return PTR_ERR(inst->queue_proxy_region);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   desc->status_region_name);
	inst->queue_state_debug_region = devm_ioremap_resource(dev, res);
	if (IS_ERR(inst->queue_state_debug_region))
		return PTR_ERR(inst->queue_state_debug_region);

	if (desc->is_sproxy) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   desc->ctrl_region_name);
		inst->queue_ctrl_region = devm_ioremap_resource(dev, res);
		if (IS_ERR(inst->queue_ctrl_region))
			return PTR_ERR(inst->queue_ctrl_region);
	}

	dev_dbg(dev, "proxy region=%p, queue_state=%p\n",
		inst->queue_proxy_region, inst->queue_state_debug_region);

	queue_count = desc->num_valid_queues;
	if (!queue_count || queue_count > desc->queue_count) {
		dev_crit(dev, "Invalid Number of queues %d. Max %d\n",
			 queue_count, desc->queue_count);
		return -ERANGE;
	}
	inst->num_valid_queues = queue_count;

	qinst = devm_kcalloc(dev, queue_count, sizeof(*qinst), GFP_KERNEL);
	if (!qinst)
		return -ENOMEM;
	inst->qinsts = qinst;

	chans = devm_kcalloc(dev, queue_count, sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return -ENOMEM;
	inst->chans = chans;

	if (desc->is_sproxy) {
		struct ti_msgmgr_valid_queue_desc sproxy_desc;

		/* All proxies may be valid in Secure Proxy instance */
		for (i = 0; i < queue_count; i++, qinst++, chans++) {
			sproxy_desc.queue_id = 0;
			sproxy_desc.proxy_id = i;
			ret = ti_msgmgr_queue_setup(i, dev, np, inst,
						    desc, &sproxy_desc, qinst,
						    chans);
			if (ret)
				return ret;
		}
	} else {
		/* Only Some proxies are valid in Message Manager */
		for (i = 0, queue_desc = desc->valid_queues;
		     i < queue_count; i++, qinst++, chans++, queue_desc++) {
			ret = ti_msgmgr_queue_setup(i, dev, np, inst,
						    desc, queue_desc, qinst,
						    chans);
			if (ret)
				return ret;
		}
	}

	mbox = &inst->mbox;
	mbox->dev = dev;
	mbox->ops = &ti_msgmgr_chan_ops;
	mbox->chans = inst->chans;
	mbox->num_chans = inst->num_valid_queues;
	mbox->txdone_irq = false;
	mbox->txdone_poll = desc->tx_polled;
	if (desc->tx_polled)
		mbox->txpoll_period = desc->tx_poll_timeout_ms;
	mbox->of_xlate = ti_msgmgr_of_xlate;

	platform_set_drvdata(pdev, inst);
	ret = devm_mbox_controller_register(dev, mbox);
	if (ret)
		dev_err(dev, "Failed to register mbox_controller(%d)\n", ret);

	return ret;
}

static struct platform_driver ti_msgmgr_driver = {
	.probe = ti_msgmgr_probe,
	.driver = {
		   .name = "ti-msgmgr",
		   .of_match_table = of_match_ptr(ti_msgmgr_of_match),
		   .pm = &ti_msgmgr_pm_ops,
	},
};
module_platform_driver(ti_msgmgr_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI message manager driver");
MODULE_AUTHOR("Nishanth Menon");
MODULE_ALIAS("platform:ti-msgmgr");
