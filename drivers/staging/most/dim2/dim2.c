// SPDX-License-Identifier: GPL-2.0
/*
 * dim2.c - MediaLB DIM2 Hardware Dependent Module
 *
 * Copyright (C) 2015-2016, Microchip Technology Germany II GmbH & Co. KG
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/most.h>
#include "hal.h"
#include "errors.h"
#include "sysfs.h"

#define DMA_CHANNELS (32 - 1)  /* channel 0 is a system channel */

#define MAX_BUFFERS_PACKET      32
#define MAX_BUFFERS_STREAMING   32
#define MAX_BUF_SIZE_PACKET     2048
#define MAX_BUF_SIZE_STREAMING  (8 * 1024)

/*
 * The parameter representing the number of frames per sub-buffer for
 * synchronous channels.  Valid values: [0 .. 6].
 *
 * The values 0, 1, 2, 3, 4, 5, 6 represent corresponding number of frames per
 * sub-buffer 1, 2, 4, 8, 16, 32, 64.
 */
static u8 fcnt = 4;  /* (1 << fcnt) frames per subbuffer */
module_param(fcnt, byte, 0000);
MODULE_PARM_DESC(fcnt, "Num of frames per sub-buffer for sync channels as a power of 2");

static DEFINE_SPINLOCK(dim_lock);

static void dim2_tasklet_fn(unsigned long data);
static DECLARE_TASKLET_OLD(dim2_tasklet, dim2_tasklet_fn);

/**
 * struct hdm_channel - private structure to keep channel specific data
 * @is_initialized: identifier to know whether the channel is initialized
 * @ch: HAL specific channel data
 * @pending_list: list to keep MBO's before starting transfer
 * @started_list: list to keep MBO's after starting transfer
 * @direction: channel direction (TX or RX)
 * @data_type: channel data type
 */
struct hdm_channel {
	char name[sizeof "caNNN"];
	bool is_initialized;
	struct dim_channel ch;
	u16 *reset_dbr_size;
	struct list_head pending_list;	/* before dim_enqueue_buffer() */
	struct list_head started_list;	/* after dim_enqueue_buffer() */
	enum most_channel_direction direction;
	enum most_channel_data_type data_type;
};

/**
 * struct dim2_hdm - private structure to keep interface specific data
 * @hch: an array of channel specific data
 * @most_iface: most interface structure
 * @capabilities: an array of channel capability data
 * @io_base: I/O register base address
 * @netinfo_task: thread to deliver network status
 * @netinfo_waitq: waitq for the thread to sleep
 * @deliver_netinfo: to identify whether network status received
 * @mac_addrs: INIC mac address
 * @link_state: network link state
 * @atx_idx: index of async tx channel
 */
struct dim2_hdm {
	struct device dev;
	struct hdm_channel hch[DMA_CHANNELS];
	struct most_channel_capability capabilities[DMA_CHANNELS];
	struct most_interface most_iface;
	char name[16 + sizeof "dim2-"];
	void __iomem *io_base;
	u8 clk_speed;
	struct clk *clk;
	struct clk *clk_pll;
	struct task_struct *netinfo_task;
	wait_queue_head_t netinfo_waitq;
	int deliver_netinfo;
	unsigned char mac_addrs[6];
	unsigned char link_state;
	int atx_idx;
	struct medialb_bus bus;
	void (*on_netinfo)(struct most_interface *most_iface,
			   unsigned char link_state, unsigned char *addrs);
	void (*disable_platform)(struct platform_device *pdev);
};

struct dim2_platform_data {
	int (*enable)(struct platform_device *pdev);
	void (*disable)(struct platform_device *pdev);
};

#define iface_to_hdm(iface) container_of(iface, struct dim2_hdm, most_iface)

/* Macro to identify a network status message */
#define PACKET_IS_NET_INFO(p)  \
	(((p)[1] == 0x18) && ((p)[2] == 0x05) && ((p)[3] == 0x0C) && \
	 ((p)[13] == 0x3C) && ((p)[14] == 0x00) && ((p)[15] == 0x0A))

bool dim2_sysfs_get_state_cb(void)
{
	bool state;
	unsigned long flags;

	spin_lock_irqsave(&dim_lock, flags);
	state = dim_get_lock_state();
	spin_unlock_irqrestore(&dim_lock, flags);

	return state;
}

/**
 * dimcb_on_error - callback from HAL to report miscommunication between
 * HDM and HAL
 * @error_id: Error ID
 * @error_message: Error message. Some text in a free format
 */
void dimcb_on_error(u8 error_id, const char *error_message)
{
	pr_err("%s: error_id - %d, error_message - %s\n", __func__, error_id,
	       error_message);
}

/**
 * try_start_dim_transfer - try to transfer a buffer on a channel
 * @hdm_ch: channel specific data
 *
 * Transfer a buffer from pending_list if the channel is ready
 */
static int try_start_dim_transfer(struct hdm_channel *hdm_ch)
{
	u16 buf_size;
	struct list_head *head = &hdm_ch->pending_list;
	struct mbo *mbo;
	unsigned long flags;
	struct dim_ch_state_t st;

	BUG_ON(!hdm_ch);
	BUG_ON(!hdm_ch->is_initialized);

	spin_lock_irqsave(&dim_lock, flags);
	if (list_empty(head)) {
		spin_unlock_irqrestore(&dim_lock, flags);
		return -EAGAIN;
	}

	if (!dim_get_channel_state(&hdm_ch->ch, &st)->ready) {
		spin_unlock_irqrestore(&dim_lock, flags);
		return -EAGAIN;
	}

	mbo = list_first_entry(head, struct mbo, list);
	buf_size = mbo->buffer_length;

	if (dim_dbr_space(&hdm_ch->ch) < buf_size) {
		spin_unlock_irqrestore(&dim_lock, flags);
		return -EAGAIN;
	}

	BUG_ON(mbo->bus_address == 0);
	if (!dim_enqueue_buffer(&hdm_ch->ch, mbo->bus_address, buf_size)) {
		list_del(head->next);
		spin_unlock_irqrestore(&dim_lock, flags);
		mbo->processed_length = 0;
		mbo->status = MBO_E_INVAL;
		mbo->complete(mbo);
		return -EFAULT;
	}

	list_move_tail(head->next, &hdm_ch->started_list);
	spin_unlock_irqrestore(&dim_lock, flags);

	return 0;
}

/**
 * deliver_netinfo_thread - thread to deliver network status to mostcore
 * @data: private data
 *
 * Wait for network status and deliver it to mostcore once it is received
 */
static int deliver_netinfo_thread(void *data)
{
	struct dim2_hdm *dev = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(dev->netinfo_waitq,
					 dev->deliver_netinfo ||
					 kthread_should_stop());

		if (dev->deliver_netinfo) {
			dev->deliver_netinfo--;
			if (dev->on_netinfo) {
				dev->on_netinfo(&dev->most_iface,
						dev->link_state,
						dev->mac_addrs);
			}
		}
	}

	return 0;
}

/**
 * retrieve_netinfo - retrieve network status from received buffer
 * @dev: private data
 * @mbo: received MBO
 *
 * Parse the message in buffer and get node address, link state, MAC address.
 * Wake up a thread to deliver this status to mostcore
 */
static void retrieve_netinfo(struct dim2_hdm *dev, struct mbo *mbo)
{
	u8 *data = mbo->virt_address;

	pr_info("Node Address: 0x%03x\n", (u16)data[16] << 8 | data[17]);
	dev->link_state = data[18];
	pr_info("NIState: %d\n", dev->link_state);
	memcpy(dev->mac_addrs, data + 19, 6);
	dev->deliver_netinfo++;
	wake_up_interruptible(&dev->netinfo_waitq);
}

/**
 * service_done_flag - handle completed buffers
 * @dev: private data
 * @ch_idx: channel index
 *
 * Return back the completed buffers to mostcore, using completion callback
 */
static void service_done_flag(struct dim2_hdm *dev, int ch_idx)
{
	struct hdm_channel *hdm_ch = dev->hch + ch_idx;
	struct dim_ch_state_t st;
	struct list_head *head;
	struct mbo *mbo;
	int done_buffers;
	unsigned long flags;
	u8 *data;

	BUG_ON(!hdm_ch);
	BUG_ON(!hdm_ch->is_initialized);

	spin_lock_irqsave(&dim_lock, flags);

	done_buffers = dim_get_channel_state(&hdm_ch->ch, &st)->done_buffers;
	if (!done_buffers) {
		spin_unlock_irqrestore(&dim_lock, flags);
		return;
	}

	if (!dim_detach_buffers(&hdm_ch->ch, done_buffers)) {
		spin_unlock_irqrestore(&dim_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&dim_lock, flags);

	head = &hdm_ch->started_list;

	while (done_buffers) {
		spin_lock_irqsave(&dim_lock, flags);
		if (list_empty(head)) {
			spin_unlock_irqrestore(&dim_lock, flags);
			pr_crit("hard error: started_mbo list is empty whereas DIM2 has sent buffers\n");
			break;
		}

		mbo = list_first_entry(head, struct mbo, list);
		list_del(head->next);
		spin_unlock_irqrestore(&dim_lock, flags);

		data = mbo->virt_address;

		if (hdm_ch->data_type == MOST_CH_ASYNC &&
		    hdm_ch->direction == MOST_CH_RX &&
		    PACKET_IS_NET_INFO(data)) {
			retrieve_netinfo(dev, mbo);

			spin_lock_irqsave(&dim_lock, flags);
			list_add_tail(&mbo->list, &hdm_ch->pending_list);
			spin_unlock_irqrestore(&dim_lock, flags);
		} else {
			if (hdm_ch->data_type == MOST_CH_CONTROL ||
			    hdm_ch->data_type == MOST_CH_ASYNC) {
				u32 const data_size =
					(u32)data[0] * 256 + data[1] + 2;

				mbo->processed_length =
					min_t(u32, data_size,
					      mbo->buffer_length);
			} else {
				mbo->processed_length = mbo->buffer_length;
			}
			mbo->status = MBO_SUCCESS;
			mbo->complete(mbo);
		}

		done_buffers--;
	}
}

static struct dim_channel **get_active_channels(struct dim2_hdm *dev,
						struct dim_channel **buffer)
{
	int idx = 0;
	int ch_idx;

	for (ch_idx = 0; ch_idx < DMA_CHANNELS; ch_idx++) {
		if (dev->hch[ch_idx].is_initialized)
			buffer[idx++] = &dev->hch[ch_idx].ch;
	}
	buffer[idx++] = NULL;

	return buffer;
}

static irqreturn_t dim2_mlb_isr(int irq, void *_dev)
{
	struct dim2_hdm *dev = _dev;
	unsigned long flags;

	spin_lock_irqsave(&dim_lock, flags);
	dim_service_mlb_int_irq();
	spin_unlock_irqrestore(&dim_lock, flags);

	if (dev->atx_idx >= 0 && dev->hch[dev->atx_idx].is_initialized)
		while (!try_start_dim_transfer(dev->hch + dev->atx_idx))
			continue;

	return IRQ_HANDLED;
}

/**
 * dim2_tasklet_fn - tasklet function
 * @data: private data
 *
 * Service each initialized channel, if needed
 */
static void dim2_tasklet_fn(unsigned long data)
{
	struct dim2_hdm *dev = (struct dim2_hdm *)data;
	unsigned long flags;
	int ch_idx;

	for (ch_idx = 0; ch_idx < DMA_CHANNELS; ch_idx++) {
		if (!dev->hch[ch_idx].is_initialized)
			continue;

		spin_lock_irqsave(&dim_lock, flags);
		dim_service_channel(&dev->hch[ch_idx].ch);
		spin_unlock_irqrestore(&dim_lock, flags);

		service_done_flag(dev, ch_idx);
		while (!try_start_dim_transfer(dev->hch + ch_idx))
			continue;
	}
}

/**
 * dim2_ahb_isr - interrupt service routine
 * @irq: irq number
 * @_dev: private data
 *
 * Acknowledge the interrupt and schedule a tasklet to service channels.
 * Return IRQ_HANDLED.
 */
static irqreturn_t dim2_ahb_isr(int irq, void *_dev)
{
	struct dim2_hdm *dev = _dev;
	struct dim_channel *buffer[DMA_CHANNELS + 1];
	unsigned long flags;

	spin_lock_irqsave(&dim_lock, flags);
	dim_service_ahb_int_irq(get_active_channels(dev, buffer));
	spin_unlock_irqrestore(&dim_lock, flags);

	dim2_tasklet.data = (unsigned long)dev;
	tasklet_schedule(&dim2_tasklet);
	return IRQ_HANDLED;
}

/**
 * complete_all_mbos - complete MBO's in a list
 * @head: list head
 *
 * Delete all the entries in list and return back MBO's to mostcore using
 * completion call back.
 */
static void complete_all_mbos(struct list_head *head)
{
	unsigned long flags;
	struct mbo *mbo;

	for (;;) {
		spin_lock_irqsave(&dim_lock, flags);
		if (list_empty(head)) {
			spin_unlock_irqrestore(&dim_lock, flags);
			break;
		}

		mbo = list_first_entry(head, struct mbo, list);
		list_del(head->next);
		spin_unlock_irqrestore(&dim_lock, flags);

		mbo->processed_length = 0;
		mbo->status = MBO_E_CLOSE;
		mbo->complete(mbo);
	}
}

/**
 * configure_channel - initialize a channel
 * @iface: interface the channel belongs to
 * @channel: channel to be configured
 * @channel_config: structure that holds the configuration information
 *
 * Receives configuration information from mostcore and initialize
 * the corresponding channel. Return 0 on success, negative on failure.
 */
static int configure_channel(struct most_interface *most_iface, int ch_idx,
			     struct most_channel_config *ccfg)
{
	struct dim2_hdm *dev = iface_to_hdm(most_iface);
	bool const is_tx = ccfg->direction == MOST_CH_TX;
	u16 const sub_size = ccfg->subbuffer_size;
	u16 const buf_size = ccfg->buffer_size;
	u16 new_size;
	unsigned long flags;
	u8 hal_ret;
	int const ch_addr = ch_idx * 2 + 2;
	struct hdm_channel *const hdm_ch = dev->hch + ch_idx;

	BUG_ON(ch_idx < 0 || ch_idx >= DMA_CHANNELS);

	if (hdm_ch->is_initialized)
		return -EPERM;

	/* do not reset if the property was set by user, see poison_channel */
	hdm_ch->reset_dbr_size = ccfg->dbr_size ? NULL : &ccfg->dbr_size;

	/* zero value is default dbr_size, see dim2 hal */
	hdm_ch->ch.dbr_size = ccfg->dbr_size;

	switch (ccfg->data_type) {
	case MOST_CH_CONTROL:
		new_size = dim_norm_ctrl_async_buffer_size(buf_size);
		if (new_size == 0) {
			pr_err("%s: too small buffer size\n", hdm_ch->name);
			return -EINVAL;
		}
		ccfg->buffer_size = new_size;
		if (new_size != buf_size)
			pr_warn("%s: fixed buffer size (%d -> %d)\n",
				hdm_ch->name, buf_size, new_size);
		spin_lock_irqsave(&dim_lock, flags);
		hal_ret = dim_init_control(&hdm_ch->ch, is_tx, ch_addr,
					   is_tx ? new_size * 2 : new_size);
		break;
	case MOST_CH_ASYNC:
		new_size = dim_norm_ctrl_async_buffer_size(buf_size);
		if (new_size == 0) {
			pr_err("%s: too small buffer size\n", hdm_ch->name);
			return -EINVAL;
		}
		ccfg->buffer_size = new_size;
		if (new_size != buf_size)
			pr_warn("%s: fixed buffer size (%d -> %d)\n",
				hdm_ch->name, buf_size, new_size);
		spin_lock_irqsave(&dim_lock, flags);
		hal_ret = dim_init_async(&hdm_ch->ch, is_tx, ch_addr,
					 is_tx ? new_size * 2 : new_size);
		break;
	case MOST_CH_ISOC:
		new_size = dim_norm_isoc_buffer_size(buf_size, sub_size);
		if (new_size == 0) {
			pr_err("%s: invalid sub-buffer size or too small buffer size\n",
			       hdm_ch->name);
			return -EINVAL;
		}
		ccfg->buffer_size = new_size;
		if (new_size != buf_size)
			pr_warn("%s: fixed buffer size (%d -> %d)\n",
				hdm_ch->name, buf_size, new_size);
		spin_lock_irqsave(&dim_lock, flags);
		hal_ret = dim_init_isoc(&hdm_ch->ch, is_tx, ch_addr, sub_size);
		break;
	case MOST_CH_SYNC:
		new_size = dim_norm_sync_buffer_size(buf_size, sub_size);
		if (new_size == 0) {
			pr_err("%s: invalid sub-buffer size or too small buffer size\n",
			       hdm_ch->name);
			return -EINVAL;
		}
		ccfg->buffer_size = new_size;
		if (new_size != buf_size)
			pr_warn("%s: fixed buffer size (%d -> %d)\n",
				hdm_ch->name, buf_size, new_size);
		spin_lock_irqsave(&dim_lock, flags);
		hal_ret = dim_init_sync(&hdm_ch->ch, is_tx, ch_addr, sub_size);
		break;
	default:
		pr_err("%s: configure failed, bad channel type: %d\n",
		       hdm_ch->name, ccfg->data_type);
		return -EINVAL;
	}

	if (hal_ret != DIM_NO_ERROR) {
		spin_unlock_irqrestore(&dim_lock, flags);
		pr_err("%s: configure failed (%d), type: %d, is_tx: %d\n",
		       hdm_ch->name, hal_ret, ccfg->data_type, (int)is_tx);
		return -ENODEV;
	}

	hdm_ch->data_type = ccfg->data_type;
	hdm_ch->direction = ccfg->direction;
	hdm_ch->is_initialized = true;

	if (hdm_ch->data_type == MOST_CH_ASYNC &&
	    hdm_ch->direction == MOST_CH_TX &&
	    dev->atx_idx < 0)
		dev->atx_idx = ch_idx;

	spin_unlock_irqrestore(&dim_lock, flags);
	ccfg->dbr_size = hdm_ch->ch.dbr_size;

	return 0;
}

/**
 * enqueue - enqueue a buffer for data transfer
 * @iface: intended interface
 * @channel: ID of the channel the buffer is intended for
 * @mbo: pointer to the buffer object
 *
 * Push the buffer into pending_list and try to transfer one buffer from
 * pending_list. Return 0 on success, negative on failure.
 */
static int enqueue(struct most_interface *most_iface, int ch_idx,
		   struct mbo *mbo)
{
	struct dim2_hdm *dev = iface_to_hdm(most_iface);
	struct hdm_channel *hdm_ch = dev->hch + ch_idx;
	unsigned long flags;

	BUG_ON(ch_idx < 0 || ch_idx >= DMA_CHANNELS);

	if (!hdm_ch->is_initialized)
		return -EPERM;

	if (mbo->bus_address == 0)
		return -EFAULT;

	spin_lock_irqsave(&dim_lock, flags);
	list_add_tail(&mbo->list, &hdm_ch->pending_list);
	spin_unlock_irqrestore(&dim_lock, flags);

	(void)try_start_dim_transfer(hdm_ch);

	return 0;
}

/**
 * request_netinfo - triggers retrieving of network info
 * @iface: pointer to the interface
 * @channel_id: corresponding channel ID
 *
 * Send a command to INIC which triggers retrieving of network info by means of
 * "Message exchange over MDP/MEP". Return 0 on success, negative on failure.
 */
static void request_netinfo(struct most_interface *most_iface, int ch_idx,
			    void (*on_netinfo)(struct most_interface *,
					       unsigned char, unsigned char *))
{
	struct dim2_hdm *dev = iface_to_hdm(most_iface);
	struct mbo *mbo;
	u8 *data;

	dev->on_netinfo = on_netinfo;
	if (!on_netinfo)
		return;

	if (dev->atx_idx < 0) {
		pr_err("Async Tx Not initialized\n");
		return;
	}

	mbo = most_get_mbo(&dev->most_iface, dev->atx_idx, NULL);
	if (!mbo)
		return;

	mbo->buffer_length = 5;

	data = mbo->virt_address;

	data[0] = 0x00; /* PML High byte */
	data[1] = 0x03; /* PML Low byte */
	data[2] = 0x02; /* PMHL */
	data[3] = 0x08; /* FPH */
	data[4] = 0x40; /* FMF (FIFO cmd msg - Triggers NAOverMDP) */

	most_submit_mbo(mbo);
}

/**
 * poison_channel - poison buffers of a channel
 * @iface: pointer to the interface the channel to be poisoned belongs to
 * @channel_id: corresponding channel ID
 *
 * Destroy a channel and complete all the buffers in both started_list &
 * pending_list. Return 0 on success, negative on failure.
 */
static int poison_channel(struct most_interface *most_iface, int ch_idx)
{
	struct dim2_hdm *dev = iface_to_hdm(most_iface);
	struct hdm_channel *hdm_ch = dev->hch + ch_idx;
	unsigned long flags;
	u8 hal_ret;
	int ret = 0;

	BUG_ON(ch_idx < 0 || ch_idx >= DMA_CHANNELS);

	if (!hdm_ch->is_initialized)
		return -EPERM;

	tasklet_disable(&dim2_tasklet);
	spin_lock_irqsave(&dim_lock, flags);
	hal_ret = dim_destroy_channel(&hdm_ch->ch);
	hdm_ch->is_initialized = false;
	if (ch_idx == dev->atx_idx)
		dev->atx_idx = -1;
	spin_unlock_irqrestore(&dim_lock, flags);
	tasklet_enable(&dim2_tasklet);
	if (hal_ret != DIM_NO_ERROR) {
		pr_err("HAL Failed to close channel %s\n", hdm_ch->name);
		ret = -EFAULT;
	}

	complete_all_mbos(&hdm_ch->started_list);
	complete_all_mbos(&hdm_ch->pending_list);
	if (hdm_ch->reset_dbr_size)
		*hdm_ch->reset_dbr_size = 0;

	return ret;
}

static void *dma_alloc(struct mbo *mbo, u32 size)
{
	struct device *dev = mbo->ifp->driver_dev;

	return dma_alloc_coherent(dev, size, &mbo->bus_address, GFP_KERNEL);
}

static void dma_free(struct mbo *mbo, u32 size)
{
	struct device *dev = mbo->ifp->driver_dev;

	dma_free_coherent(dev, size, mbo->virt_address, mbo->bus_address);
}

static const struct of_device_id dim2_of_match[];

static struct {
	const char *clock_speed;
	u8 clk_speed;
} clk_mt[] = {
	{ "256fs", CLK_256FS },
	{ "512fs", CLK_512FS },
	{ "1024fs", CLK_1024FS },
	{ "2048fs", CLK_2048FS },
	{ "3072fs", CLK_3072FS },
	{ "4096fs", CLK_4096FS },
	{ "6144fs", CLK_6144FS },
	{ "8192fs", CLK_8192FS },
};

/**
 * get_dim2_clk_speed - converts string to DIM2 clock speed value
 *
 * @clock_speed: string in the format "{NUMBER}fs"
 * @val: pointer to get one of the CLK_{NUMBER}FS values
 *
 * By success stores one of the CLK_{NUMBER}FS in the *val and returns 0,
 * otherwise returns -EINVAL.
 */
static int get_dim2_clk_speed(const char *clock_speed, u8 *val)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(clk_mt); i++) {
		if (!strcmp(clock_speed, clk_mt[i].clock_speed)) {
			*val = clk_mt[i].clk_speed;
			return 0;
		}
	}
	return -EINVAL;
}

/*
 * dim2_probe - dim2 probe handler
 * @pdev: platform device structure
 *
 * Register the dim2 interface with mostcore and initialize it.
 * Return 0 on success, negative on failure.
 */
static int dim2_probe(struct platform_device *pdev)
{
	const struct dim2_platform_data *pdata;
	const struct of_device_id *of_id;
	const char *clock_speed;
	struct dim2_hdm *dev;
	struct resource *res;
	int ret, i;
	u8 hal_ret;
	int irq;

	enum { MLB_INT_IDX, AHB0_INT_IDX };

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->atx_idx = -1;

	platform_set_drvdata(pdev, dev);

	ret = of_property_read_string(pdev->dev.of_node,
				      "microchip,clock-speed", &clock_speed);
	if (ret) {
		dev_err(&pdev->dev, "missing dt property clock-speed\n");
		return ret;
	}

	ret = get_dim2_clk_speed(clock_speed, &dev->clk_speed);
	if (ret) {
		dev_err(&pdev->dev, "bad dt property clock-speed\n");
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->io_base))
		return PTR_ERR(dev->io_base);

	of_id = of_match_node(dim2_of_match, pdev->dev.of_node);
	pdata = of_id->data;
	ret = pdata && pdata->enable ? pdata->enable(pdev) : 0;
	if (ret)
		return ret;

	dev->disable_platform = pdata ? pdata->disable : NULL;

	dev_info(&pdev->dev, "sync: num of frames per sub-buffer: %u\n", fcnt);
	hal_ret = dim_startup(dev->io_base, dev->clk_speed, fcnt);
	if (hal_ret != DIM_NO_ERROR) {
		dev_err(&pdev->dev, "dim_startup failed: %d\n", hal_ret);
		ret = -ENODEV;
		goto err_disable_platform;
	}

	irq = platform_get_irq(pdev, AHB0_INT_IDX);
	if (irq < 0) {
		ret = irq;
		goto err_shutdown_dim;
	}

	ret = devm_request_irq(&pdev->dev, irq, dim2_ahb_isr, 0,
			       "dim2_ahb0_int", dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to request ahb0_int irq %d\n", irq);
		goto err_shutdown_dim;
	}

	irq = platform_get_irq(pdev, MLB_INT_IDX);
	if (irq < 0) {
		ret = irq;
		goto err_shutdown_dim;
	}

	ret = devm_request_irq(&pdev->dev, irq, dim2_mlb_isr, 0,
			       "dim2_mlb_int", dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to request mlb_int irq %d\n", irq);
		goto err_shutdown_dim;
	}

	init_waitqueue_head(&dev->netinfo_waitq);
	dev->deliver_netinfo = 0;
	dev->netinfo_task = kthread_run(&deliver_netinfo_thread, dev,
					"dim2_netinfo");
	if (IS_ERR(dev->netinfo_task)) {
		ret = PTR_ERR(dev->netinfo_task);
		goto err_shutdown_dim;
	}

	for (i = 0; i < DMA_CHANNELS; i++) {
		struct most_channel_capability *cap = dev->capabilities + i;
		struct hdm_channel *hdm_ch = dev->hch + i;

		INIT_LIST_HEAD(&hdm_ch->pending_list);
		INIT_LIST_HEAD(&hdm_ch->started_list);
		hdm_ch->is_initialized = false;
		snprintf(hdm_ch->name, sizeof(hdm_ch->name), "ca%d", i * 2 + 2);

		cap->name_suffix = hdm_ch->name;
		cap->direction = MOST_CH_RX | MOST_CH_TX;
		cap->data_type = MOST_CH_CONTROL | MOST_CH_ASYNC |
				 MOST_CH_ISOC | MOST_CH_SYNC;
		cap->num_buffers_packet = MAX_BUFFERS_PACKET;
		cap->buffer_size_packet = MAX_BUF_SIZE_PACKET;
		cap->num_buffers_streaming = MAX_BUFFERS_STREAMING;
		cap->buffer_size_streaming = MAX_BUF_SIZE_STREAMING;
	}

	{
		const char *fmt;

		if (sizeof(res->start) == sizeof(long long))
			fmt = "dim2-%016llx";
		else if (sizeof(res->start) == sizeof(long))
			fmt = "dim2-%016lx";
		else
			fmt = "dim2-%016x";

		snprintf(dev->name, sizeof(dev->name), fmt, res->start);
	}

	dev->most_iface.interface = ITYPE_MEDIALB_DIM2;
	dev->most_iface.description = dev->name;
	dev->most_iface.num_channels = DMA_CHANNELS;
	dev->most_iface.channel_vector = dev->capabilities;
	dev->most_iface.configure = configure_channel;
	dev->most_iface.enqueue = enqueue;
	dev->most_iface.dma_alloc = dma_alloc;
	dev->most_iface.dma_free = dma_free;
	dev->most_iface.poison_channel = poison_channel;
	dev->most_iface.request_netinfo = request_netinfo;
	dev->most_iface.driver_dev = &pdev->dev;
	dev->most_iface.dev = &dev->dev;
	dev->dev.init_name = "dim2_state";
	dev->dev.parent = &pdev->dev;

	ret = most_register_interface(&dev->most_iface);
	if (ret) {
		dev_err(&pdev->dev, "failed to register MOST interface\n");
		goto err_stop_thread;
	}

	ret = dim2_sysfs_probe(&dev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs attribute\n");
		goto err_unreg_iface;
	}

	return 0;

err_unreg_iface:
	most_deregister_interface(&dev->most_iface);
err_stop_thread:
	kthread_stop(dev->netinfo_task);
err_shutdown_dim:
	dim_shutdown();
err_disable_platform:
	if (dev->disable_platform)
		dev->disable_platform(pdev);

	return ret;
}

/**
 * dim2_remove - dim2 remove handler
 * @pdev: platform device structure
 *
 * Unregister the interface from mostcore
 */
static int dim2_remove(struct platform_device *pdev)
{
	struct dim2_hdm *dev = platform_get_drvdata(pdev);
	unsigned long flags;

	dim2_sysfs_destroy(&dev->dev);
	most_deregister_interface(&dev->most_iface);
	kthread_stop(dev->netinfo_task);

	spin_lock_irqsave(&dim_lock, flags);
	dim_shutdown();
	spin_unlock_irqrestore(&dim_lock, flags);

	if (dev->disable_platform)
		dev->disable_platform(pdev);

	return 0;
}

/* platform specific functions [[ */

static int fsl_mx6_enable(struct platform_device *pdev)
{
	struct dim2_hdm *dev = platform_get_drvdata(pdev);
	int ret;

	dev->clk = devm_clk_get(&pdev->dev, "mlb");
	if (IS_ERR_OR_NULL(dev->clk)) {
		dev_err(&pdev->dev, "unable to get mlb clock\n");
		return -EFAULT;
	}

	ret = clk_prepare_enable(dev->clk);
	if (ret) {
		dev_err(&pdev->dev, "%s\n", "clk_prepare_enable failed");
		return ret;
	}

	if (dev->clk_speed >= CLK_2048FS) {
		/* enable pll */
		dev->clk_pll = devm_clk_get(&pdev->dev, "pll8_mlb");
		if (IS_ERR_OR_NULL(dev->clk_pll)) {
			dev_err(&pdev->dev, "unable to get mlb pll clock\n");
			clk_disable_unprepare(dev->clk);
			return -EFAULT;
		}

		writel(0x888, dev->io_base + 0x38);
		clk_prepare_enable(dev->clk_pll);
	}

	return 0;
}

static void fsl_mx6_disable(struct platform_device *pdev)
{
	struct dim2_hdm *dev = platform_get_drvdata(pdev);

	if (dev->clk_speed >= CLK_2048FS)
		clk_disable_unprepare(dev->clk_pll);

	clk_disable_unprepare(dev->clk);
}

static int rcar_h2_enable(struct platform_device *pdev)
{
	struct dim2_hdm *dev = platform_get_drvdata(pdev);
	int ret;

	dev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return PTR_ERR(dev->clk);
	}

	ret = clk_prepare_enable(dev->clk);
	if (ret) {
		dev_err(&pdev->dev, "%s\n", "clk_prepare_enable failed");
		return ret;
	}

	if (dev->clk_speed >= CLK_2048FS) {
		/* enable MLP pll and LVDS drivers */
		writel(0x03, dev->io_base + 0x600);
		/* set bias */
		writel(0x888, dev->io_base + 0x38);
	} else {
		/* PLL */
		writel(0x04, dev->io_base + 0x600);
	}


	/* BBCR = 0b11 */
	writel(0x03, dev->io_base + 0x500);
	writel(0x0002FF02, dev->io_base + 0x508);

	return 0;
}

static void rcar_h2_disable(struct platform_device *pdev)
{
	struct dim2_hdm *dev = platform_get_drvdata(pdev);

	clk_disable_unprepare(dev->clk);

	/* disable PLLs and LVDS drivers */
	writel(0x0, dev->io_base + 0x600);
}

static int rcar_m3_enable(struct platform_device *pdev)
{
	struct dim2_hdm *dev = platform_get_drvdata(pdev);
	u32 enable_512fs = dev->clk_speed == CLK_512FS;
	int ret;

	dev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return PTR_ERR(dev->clk);
	}

	ret = clk_prepare_enable(dev->clk);
	if (ret) {
		dev_err(&pdev->dev, "%s\n", "clk_prepare_enable failed");
		return ret;
	}

	/* PLL */
	writel(0x04, dev->io_base + 0x600);

	writel(enable_512fs, dev->io_base + 0x604);

	/* BBCR = 0b11 */
	writel(0x03, dev->io_base + 0x500);
	writel(0x0002FF02, dev->io_base + 0x508);

	return 0;
}

static void rcar_m3_disable(struct platform_device *pdev)
{
	struct dim2_hdm *dev = platform_get_drvdata(pdev);

	clk_disable_unprepare(dev->clk);

	/* disable PLLs and LVDS drivers */
	writel(0x0, dev->io_base + 0x600);
}

/* ]] platform specific functions */

enum dim2_platforms { FSL_MX6, RCAR_H2, RCAR_M3 };

static struct dim2_platform_data plat_data[] = {
	[FSL_MX6] = { .enable = fsl_mx6_enable, .disable = fsl_mx6_disable },
	[RCAR_H2] = { .enable = rcar_h2_enable, .disable = rcar_h2_disable },
	[RCAR_M3] = { .enable = rcar_m3_enable, .disable = rcar_m3_disable },
};

static const struct of_device_id dim2_of_match[] = {
	{
		.compatible = "fsl,imx6q-mlb150",
		.data = plat_data + FSL_MX6
	},
	{
		.compatible = "renesas,mlp",
		.data = plat_data + RCAR_H2
	},
	{
		.compatible = "rcar,medialb-dim2",
		.data = plat_data + RCAR_M3
	},
	{
		.compatible = "xlnx,axi4-os62420_3pin-1.00.a",
	},
	{
		.compatible = "xlnx,axi4-os62420_6pin-1.00.a",
	},
	{},
};

MODULE_DEVICE_TABLE(of, dim2_of_match);

static struct platform_driver dim2_driver = {
	.probe = dim2_probe,
	.remove = dim2_remove,
	.driver = {
		.name = "hdm_dim2",
		.of_match_table = dim2_of_match,
	},
};

module_platform_driver(dim2_driver);

MODULE_AUTHOR("Andrey Shvetsov <andrey.shvetsov@k2l.de>");
MODULE_DESCRIPTION("MediaLB DIM2 Hardware Dependent Module");
MODULE_LICENSE("GPL");
