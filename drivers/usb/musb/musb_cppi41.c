#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/sizes.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include "musb_core.h"

#define RNDIS_REG(x) (0x80 + ((x - 1) * 4))

#define EP_MODE_AUTOREG_NONE		0
#define EP_MODE_AUTOREG_ALL_NEOP	1
#define EP_MODE_AUTOREG_ALWAYS		3

#define EP_MODE_DMA_TRANSPARENT		0
#define EP_MODE_DMA_RNDIS		1
#define EP_MODE_DMA_GEN_RNDIS		3

#define USB_CTRL_TX_MODE	0x70
#define USB_CTRL_RX_MODE	0x74
#define USB_CTRL_AUTOREQ	0xd0
#define USB_TDOWN		0xd8

struct cppi41_dma_channel {
	struct dma_channel channel;
	struct cppi41_dma_controller *controller;
	struct musb_hw_ep *hw_ep;
	struct dma_chan *dc;
	dma_cookie_t cookie;
	u8 port_num;
	u8 is_tx;
	u8 is_allocated;
	u8 usb_toggle;

	dma_addr_t buf_addr;
	u32 total_len;
	u32 prog_len;
	u32 transferred;
	u32 packet_sz;
	struct list_head tx_check;
	struct work_struct dma_completion;
};

#define MUSB_DMA_NUM_CHANNELS 15

struct cppi41_dma_controller {
	struct dma_controller controller;
	struct cppi41_dma_channel rx_channel[MUSB_DMA_NUM_CHANNELS];
	struct cppi41_dma_channel tx_channel[MUSB_DMA_NUM_CHANNELS];
	struct musb *musb;
	struct hrtimer early_tx;
	struct list_head early_tx_list;
	u32 rx_mode;
	u32 tx_mode;
	u32 auto_req;
};

static void save_rx_toggle(struct cppi41_dma_channel *cppi41_channel)
{
	u16 csr;
	u8 toggle;

	if (cppi41_channel->is_tx)
		return;
	if (!is_host_active(cppi41_channel->controller->musb))
		return;

	csr = musb_readw(cppi41_channel->hw_ep->regs, MUSB_RXCSR);
	toggle = csr & MUSB_RXCSR_H_DATATOGGLE ? 1 : 0;

	cppi41_channel->usb_toggle = toggle;
}

static void update_rx_toggle(struct cppi41_dma_channel *cppi41_channel)
{
	u16 csr;
	u8 toggle;

	if (cppi41_channel->is_tx)
		return;
	if (!is_host_active(cppi41_channel->controller->musb))
		return;

	csr = musb_readw(cppi41_channel->hw_ep->regs, MUSB_RXCSR);
	toggle = csr & MUSB_RXCSR_H_DATATOGGLE ? 1 : 0;

	/*
	 * AM335x Advisory 1.0.13: Due to internal synchronisation error the
	 * data toggle may reset from DATA1 to DATA0 during receiving data from
	 * more than one endpoint.
	 */
	if (!toggle && toggle == cppi41_channel->usb_toggle) {
		csr |= MUSB_RXCSR_H_DATATOGGLE | MUSB_RXCSR_H_WR_DATATOGGLE;
		musb_writew(cppi41_channel->hw_ep->regs, MUSB_RXCSR, csr);
		dev_dbg(cppi41_channel->controller->musb->controller,
				"Restoring DATA1 toggle.\n");
	}

	cppi41_channel->usb_toggle = toggle;
}

static bool musb_is_tx_fifo_empty(struct musb_hw_ep *hw_ep)
{
	u8		epnum = hw_ep->epnum;
	struct musb	*musb = hw_ep->musb;
	void __iomem	*epio = musb->endpoints[epnum].regs;
	u16		csr;

	csr = musb_readw(epio, MUSB_TXCSR);
	if (csr & MUSB_TXCSR_TXPKTRDY)
		return false;
	return true;
}

static bool is_isoc(struct musb_hw_ep *hw_ep, bool in)
{
	if (in && hw_ep->in_qh) {
		if (hw_ep->in_qh->type == USB_ENDPOINT_XFER_ISOC)
			return true;
	} else if (hw_ep->out_qh) {
		if (hw_ep->out_qh->type == USB_ENDPOINT_XFER_ISOC)
			return true;
	}
	return false;
}

static void cppi41_dma_callback(void *private_data);

static void cppi41_trans_done(struct cppi41_dma_channel *cppi41_channel)
{
	struct musb_hw_ep *hw_ep = cppi41_channel->hw_ep;
	struct musb *musb = hw_ep->musb;

	if (!cppi41_channel->prog_len ||
	    (cppi41_channel->channel.status == MUSB_DMA_STATUS_FREE)) {

		/* done, complete */
		cppi41_channel->channel.actual_len =
			cppi41_channel->transferred;
		cppi41_channel->channel.status = MUSB_DMA_STATUS_FREE;
		cppi41_channel->channel.rx_packet_done = true;
		musb_dma_completion(musb, hw_ep->epnum, cppi41_channel->is_tx);
	} else {
		/* next iteration, reload */
		struct dma_chan *dc = cppi41_channel->dc;
		struct dma_async_tx_descriptor *dma_desc;
		enum dma_transfer_direction direction;
		u16 csr;
		u32 remain_bytes;
		void __iomem *epio = cppi41_channel->hw_ep->regs;

		cppi41_channel->buf_addr += cppi41_channel->packet_sz;

		remain_bytes = cppi41_channel->total_len;
		remain_bytes -= cppi41_channel->transferred;
		remain_bytes = min(remain_bytes, cppi41_channel->packet_sz);
		cppi41_channel->prog_len = remain_bytes;

		direction = cppi41_channel->is_tx ? DMA_MEM_TO_DEV
			: DMA_DEV_TO_MEM;
		dma_desc = dmaengine_prep_slave_single(dc,
				cppi41_channel->buf_addr,
				remain_bytes,
				direction,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (WARN_ON(!dma_desc))
			return;

		dma_desc->callback = cppi41_dma_callback;
		dma_desc->callback_param = &cppi41_channel->channel;
		cppi41_channel->cookie = dma_desc->tx_submit(dma_desc);
		dma_async_issue_pending(dc);

		if (!cppi41_channel->is_tx) {
			csr = musb_readw(epio, MUSB_RXCSR);
			csr |= MUSB_RXCSR_H_REQPKT;
			musb_writew(epio, MUSB_RXCSR, csr);
		}
	}
}

static void cppi_trans_done_work(struct work_struct *work)
{
	unsigned long flags;
	struct cppi41_dma_channel *cppi41_channel =
		container_of(work, struct cppi41_dma_channel, dma_completion);
	struct cppi41_dma_controller *controller = cppi41_channel->controller;
	struct musb *musb = controller->musb;
	struct musb_hw_ep *hw_ep = cppi41_channel->hw_ep;
	bool empty;

	if (!cppi41_channel->is_tx && is_isoc(hw_ep, 1)) {
		spin_lock_irqsave(&musb->lock, flags);
		cppi41_trans_done(cppi41_channel);
		spin_unlock_irqrestore(&musb->lock, flags);
	} else {
		empty = musb_is_tx_fifo_empty(hw_ep);
		if (empty) {
			spin_lock_irqsave(&musb->lock, flags);
			cppi41_trans_done(cppi41_channel);
			spin_unlock_irqrestore(&musb->lock, flags);
		} else {
			schedule_work(&cppi41_channel->dma_completion);
		}
	}
}

static enum hrtimer_restart cppi41_recheck_tx_req(struct hrtimer *timer)
{
	struct cppi41_dma_controller *controller;
	struct cppi41_dma_channel *cppi41_channel, *n;
	struct musb *musb;
	unsigned long flags;
	enum hrtimer_restart ret = HRTIMER_NORESTART;

	controller = container_of(timer, struct cppi41_dma_controller,
			early_tx);
	musb = controller->musb;

	spin_lock_irqsave(&musb->lock, flags);
	list_for_each_entry_safe(cppi41_channel, n, &controller->early_tx_list,
			tx_check) {
		bool empty;
		struct musb_hw_ep *hw_ep = cppi41_channel->hw_ep;

		empty = musb_is_tx_fifo_empty(hw_ep);
		if (empty) {
			list_del_init(&cppi41_channel->tx_check);
			cppi41_trans_done(cppi41_channel);
		}
	}

	if (!list_empty(&controller->early_tx_list)) {
		ret = HRTIMER_RESTART;
		hrtimer_forward_now(&controller->early_tx,
				ktime_set(0, 150 * NSEC_PER_USEC));
	}

	spin_unlock_irqrestore(&musb->lock, flags);
	return ret;
}

static void cppi41_dma_callback(void *private_data)
{
	struct dma_channel *channel = private_data;
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;
	struct musb_hw_ep *hw_ep = cppi41_channel->hw_ep;
	struct musb *musb = hw_ep->musb;
	unsigned long flags;
	struct dma_tx_state txstate;
	u32 transferred;
	bool empty;

	spin_lock_irqsave(&musb->lock, flags);

	dmaengine_tx_status(cppi41_channel->dc, cppi41_channel->cookie,
			&txstate);
	transferred = cppi41_channel->prog_len - txstate.residue;
	cppi41_channel->transferred += transferred;

	dev_dbg(musb->controller, "DMA transfer done on hw_ep=%d bytes=%d/%d\n",
		hw_ep->epnum, cppi41_channel->transferred,
		cppi41_channel->total_len);

	update_rx_toggle(cppi41_channel);

	if (cppi41_channel->transferred == cppi41_channel->total_len ||
			transferred < cppi41_channel->packet_sz)
		cppi41_channel->prog_len = 0;

	if (!cppi41_channel->is_tx) {
		if (is_isoc(hw_ep, 1))
			schedule_work(&cppi41_channel->dma_completion);
		else
			cppi41_trans_done(cppi41_channel);
		goto out;
	}

	empty = musb_is_tx_fifo_empty(hw_ep);
	if (empty) {
		cppi41_trans_done(cppi41_channel);
	} else {
		struct cppi41_dma_controller *controller;
		/*
		 * On AM335x it has been observed that the TX interrupt fires
		 * too early that means the TXFIFO is not yet empty but the DMA
		 * engine says that it is done with the transfer. We don't
		 * receive a FIFO empty interrupt so the only thing we can do is
		 * to poll for the bit. On HS it usually takes 2us, on FS around
		 * 110us - 150us depending on the transfer size.
		 * We spin on HS (no longer than than 25us and setup a timer on
		 * FS to check for the bit and complete the transfer.
		 */
		controller = cppi41_channel->controller;

		if (musb->g.speed == USB_SPEED_HIGH) {
			unsigned wait = 25;

			do {
				empty = musb_is_tx_fifo_empty(hw_ep);
				if (empty)
					break;
				wait--;
				if (!wait)
					break;
				udelay(1);
			} while (1);

			empty = musb_is_tx_fifo_empty(hw_ep);
			if (empty) {
				cppi41_trans_done(cppi41_channel);
				goto out;
			}
		}
		if (is_isoc(hw_ep, 0)) {
			schedule_work(&cppi41_channel->dma_completion);
			goto out;
		}
		list_add_tail(&cppi41_channel->tx_check,
				&controller->early_tx_list);
		if (!hrtimer_active(&controller->early_tx)) {
			hrtimer_start_range_ns(&controller->early_tx,
				ktime_set(0, 140 * NSEC_PER_USEC),
				40 * NSEC_PER_USEC,
				HRTIMER_MODE_REL);
		}
	}
out:
	spin_unlock_irqrestore(&musb->lock, flags);
}

static u32 update_ep_mode(unsigned ep, unsigned mode, u32 old)
{
	unsigned shift;

	shift = (ep - 1) * 2;
	old &= ~(3 << shift);
	old |= mode << shift;
	return old;
}

static void cppi41_set_dma_mode(struct cppi41_dma_channel *cppi41_channel,
		unsigned mode)
{
	struct cppi41_dma_controller *controller = cppi41_channel->controller;
	u32 port;
	u32 new_mode;
	u32 old_mode;

	if (cppi41_channel->is_tx)
		old_mode = controller->tx_mode;
	else
		old_mode = controller->rx_mode;
	port = cppi41_channel->port_num;
	new_mode = update_ep_mode(port, mode, old_mode);

	if (new_mode == old_mode)
		return;
	if (cppi41_channel->is_tx) {
		controller->tx_mode = new_mode;
		musb_writel(controller->musb->ctrl_base, USB_CTRL_TX_MODE,
				new_mode);
	} else {
		controller->rx_mode = new_mode;
		musb_writel(controller->musb->ctrl_base, USB_CTRL_RX_MODE,
				new_mode);
	}
}

static void cppi41_set_autoreq_mode(struct cppi41_dma_channel *cppi41_channel,
		unsigned mode)
{
	struct cppi41_dma_controller *controller = cppi41_channel->controller;
	u32 port;
	u32 new_mode;
	u32 old_mode;

	old_mode = controller->auto_req;
	port = cppi41_channel->port_num;
	new_mode = update_ep_mode(port, mode, old_mode);

	if (new_mode == old_mode)
		return;
	controller->auto_req = new_mode;
	musb_writel(controller->musb->ctrl_base, USB_CTRL_AUTOREQ, new_mode);
}

static bool cppi41_configure_channel(struct dma_channel *channel,
				u16 packet_sz, u8 mode,
				dma_addr_t dma_addr, u32 len)
{
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;
	struct dma_chan *dc = cppi41_channel->dc;
	struct dma_async_tx_descriptor *dma_desc;
	enum dma_transfer_direction direction;
	struct musb *musb = cppi41_channel->controller->musb;
	unsigned use_gen_rndis = 0;

	dev_dbg(musb->controller,
		"configure ep%d/%x packet_sz=%d, mode=%d, dma_addr=0x%llx, len=%d is_tx=%d\n",
		cppi41_channel->port_num, RNDIS_REG(cppi41_channel->port_num),
		packet_sz, mode, (unsigned long long) dma_addr,
		len, cppi41_channel->is_tx);

	cppi41_channel->buf_addr = dma_addr;
	cppi41_channel->total_len = len;
	cppi41_channel->transferred = 0;
	cppi41_channel->packet_sz = packet_sz;

	/*
	 * Due to AM335x' Advisory 1.0.13 we are not allowed to transfer more
	 * than max packet size at a time.
	 */
	if (cppi41_channel->is_tx)
		use_gen_rndis = 1;

	if (use_gen_rndis) {
		/* RNDIS mode */
		if (len > packet_sz) {
			musb_writel(musb->ctrl_base,
				RNDIS_REG(cppi41_channel->port_num), len);
			/* gen rndis */
			cppi41_set_dma_mode(cppi41_channel,
					EP_MODE_DMA_GEN_RNDIS);

			/* auto req */
			cppi41_set_autoreq_mode(cppi41_channel,
					EP_MODE_AUTOREG_ALL_NEOP);
		} else {
			musb_writel(musb->ctrl_base,
					RNDIS_REG(cppi41_channel->port_num), 0);
			cppi41_set_dma_mode(cppi41_channel,
					EP_MODE_DMA_TRANSPARENT);
			cppi41_set_autoreq_mode(cppi41_channel,
					EP_MODE_AUTOREG_NONE);
		}
	} else {
		/* fallback mode */
		cppi41_set_dma_mode(cppi41_channel, EP_MODE_DMA_TRANSPARENT);
		cppi41_set_autoreq_mode(cppi41_channel, EP_MODE_AUTOREG_NONE);
		len = min_t(u32, packet_sz, len);
	}
	cppi41_channel->prog_len = len;
	direction = cppi41_channel->is_tx ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;
	dma_desc = dmaengine_prep_slave_single(dc, dma_addr, len, direction,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!dma_desc)
		return false;

	dma_desc->callback = cppi41_dma_callback;
	dma_desc->callback_param = channel;
	cppi41_channel->cookie = dma_desc->tx_submit(dma_desc);
	cppi41_channel->channel.rx_packet_done = false;

	save_rx_toggle(cppi41_channel);
	dma_async_issue_pending(dc);
	return true;
}

static struct dma_channel *cppi41_dma_channel_allocate(struct dma_controller *c,
				struct musb_hw_ep *hw_ep, u8 is_tx)
{
	struct cppi41_dma_controller *controller = container_of(c,
			struct cppi41_dma_controller, controller);
	struct cppi41_dma_channel *cppi41_channel = NULL;
	u8 ch_num = hw_ep->epnum - 1;

	if (ch_num >= MUSB_DMA_NUM_CHANNELS)
		return NULL;

	if (is_tx)
		cppi41_channel = &controller->tx_channel[ch_num];
	else
		cppi41_channel = &controller->rx_channel[ch_num];

	if (!cppi41_channel->dc)
		return NULL;

	if (cppi41_channel->is_allocated)
		return NULL;

	cppi41_channel->hw_ep = hw_ep;
	cppi41_channel->is_allocated = 1;

	return &cppi41_channel->channel;
}

static void cppi41_dma_channel_release(struct dma_channel *channel)
{
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;

	if (cppi41_channel->is_allocated) {
		cppi41_channel->is_allocated = 0;
		channel->status = MUSB_DMA_STATUS_FREE;
		channel->actual_len = 0;
	}
}

static int cppi41_dma_channel_program(struct dma_channel *channel,
				u16 packet_sz, u8 mode,
				dma_addr_t dma_addr, u32 len)
{
	int ret;
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;
	int hb_mult = 0;

	BUG_ON(channel->status == MUSB_DMA_STATUS_UNKNOWN ||
		channel->status == MUSB_DMA_STATUS_BUSY);

	if (is_host_active(cppi41_channel->controller->musb)) {
		if (cppi41_channel->is_tx)
			hb_mult = cppi41_channel->hw_ep->out_qh->hb_mult;
		else
			hb_mult = cppi41_channel->hw_ep->in_qh->hb_mult;
	}

	channel->status = MUSB_DMA_STATUS_BUSY;
	channel->actual_len = 0;

	if (hb_mult)
		packet_sz = hb_mult * (packet_sz & 0x7FF);

	ret = cppi41_configure_channel(channel, packet_sz, mode, dma_addr, len);
	if (!ret)
		channel->status = MUSB_DMA_STATUS_FREE;

	return ret;
}

static int cppi41_is_compatible(struct dma_channel *channel, u16 maxpacket,
		void *buf, u32 length)
{
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;
	struct cppi41_dma_controller *controller = cppi41_channel->controller;
	struct musb *musb = controller->musb;

	if (is_host_active(musb)) {
		WARN_ON(1);
		return 1;
	}
	if (cppi41_channel->hw_ep->ep_in.type != USB_ENDPOINT_XFER_BULK)
		return 0;
	if (cppi41_channel->is_tx)
		return 1;
	/* AM335x Advisory 1.0.13. No workaround for device RX mode */
	return 0;
}

static int cppi41_dma_channel_abort(struct dma_channel *channel)
{
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;
	struct cppi41_dma_controller *controller = cppi41_channel->controller;
	struct musb *musb = controller->musb;
	void __iomem *epio = cppi41_channel->hw_ep->regs;
	int tdbit;
	int ret;
	unsigned is_tx;
	u16 csr;

	is_tx = cppi41_channel->is_tx;
	dev_dbg(musb->controller, "abort channel=%d, is_tx=%d\n",
			cppi41_channel->port_num, is_tx);

	if (cppi41_channel->channel.status == MUSB_DMA_STATUS_FREE)
		return 0;

	list_del_init(&cppi41_channel->tx_check);
	if (is_tx) {
		csr = musb_readw(epio, MUSB_TXCSR);
		csr &= ~MUSB_TXCSR_DMAENAB;
		musb_writew(epio, MUSB_TXCSR, csr);
	} else {
		csr = musb_readw(epio, MUSB_RXCSR);
		csr &= ~(MUSB_RXCSR_H_REQPKT | MUSB_RXCSR_DMAENAB);
		musb_writew(epio, MUSB_RXCSR, csr);

		csr = musb_readw(epio, MUSB_RXCSR);
		if (csr & MUSB_RXCSR_RXPKTRDY) {
			csr |= MUSB_RXCSR_FLUSHFIFO;
			musb_writew(epio, MUSB_RXCSR, csr);
			musb_writew(epio, MUSB_RXCSR, csr);
		}
	}

	tdbit = 1 << cppi41_channel->port_num;
	if (is_tx)
		tdbit <<= 16;

	do {
		musb_writel(musb->ctrl_base, USB_TDOWN, tdbit);
		ret = dmaengine_terminate_all(cppi41_channel->dc);
	} while (ret == -EAGAIN);

	musb_writel(musb->ctrl_base, USB_TDOWN, tdbit);

	if (is_tx) {
		csr = musb_readw(epio, MUSB_TXCSR);
		if (csr & MUSB_TXCSR_TXPKTRDY) {
			csr |= MUSB_TXCSR_FLUSHFIFO;
			musb_writew(epio, MUSB_TXCSR, csr);
		}
	}

	cppi41_channel->channel.status = MUSB_DMA_STATUS_FREE;
	return 0;
}

static void cppi41_release_all_dma_chans(struct cppi41_dma_controller *ctrl)
{
	struct dma_chan *dc;
	int i;

	for (i = 0; i < MUSB_DMA_NUM_CHANNELS; i++) {
		dc = ctrl->tx_channel[i].dc;
		if (dc)
			dma_release_channel(dc);
		dc = ctrl->rx_channel[i].dc;
		if (dc)
			dma_release_channel(dc);
	}
}

static void cppi41_dma_controller_stop(struct cppi41_dma_controller *controller)
{
	cppi41_release_all_dma_chans(controller);
}

static int cppi41_dma_controller_start(struct cppi41_dma_controller *controller)
{
	struct musb *musb = controller->musb;
	struct device *dev = musb->controller;
	struct device_node *np = dev->of_node;
	struct cppi41_dma_channel *cppi41_channel;
	int count;
	int i;
	int ret;

	count = of_property_count_strings(np, "dma-names");
	if (count < 0)
		return count;

	for (i = 0; i < count; i++) {
		struct dma_chan *dc;
		struct dma_channel *musb_dma;
		const char *str;
		unsigned is_tx;
		unsigned int port;

		ret = of_property_read_string_index(np, "dma-names", i, &str);
		if (ret)
			goto err;
		if (!strncmp(str, "tx", 2))
			is_tx = 1;
		else if (!strncmp(str, "rx", 2))
			is_tx = 0;
		else {
			dev_err(dev, "Wrong dmatype %s\n", str);
			goto err;
		}
		ret = kstrtouint(str + 2, 0, &port);
		if (ret)
			goto err;

		ret = -EINVAL;
		if (port > MUSB_DMA_NUM_CHANNELS || !port)
			goto err;
		if (is_tx)
			cppi41_channel = &controller->tx_channel[port - 1];
		else
			cppi41_channel = &controller->rx_channel[port - 1];

		cppi41_channel->controller = controller;
		cppi41_channel->port_num = port;
		cppi41_channel->is_tx = is_tx;
		INIT_LIST_HEAD(&cppi41_channel->tx_check);
		INIT_WORK(&cppi41_channel->dma_completion,
			  cppi_trans_done_work);

		musb_dma = &cppi41_channel->channel;
		musb_dma->private_data = cppi41_channel;
		musb_dma->status = MUSB_DMA_STATUS_FREE;
		musb_dma->max_len = SZ_4M;

		dc = dma_request_slave_channel(dev, str);
		if (!dc) {
			dev_err(dev, "Failed to request %s.\n", str);
			ret = -EPROBE_DEFER;
			goto err;
		}
		cppi41_channel->dc = dc;
	}
	return 0;
err:
	cppi41_release_all_dma_chans(controller);
	return ret;
}

void dma_controller_destroy(struct dma_controller *c)
{
	struct cppi41_dma_controller *controller = container_of(c,
			struct cppi41_dma_controller, controller);

	hrtimer_cancel(&controller->early_tx);
	cppi41_dma_controller_stop(controller);
	kfree(controller);
}

struct dma_controller *dma_controller_create(struct musb *musb,
					void __iomem *base)
{
	struct cppi41_dma_controller *controller;
	int ret = 0;

	if (!musb->controller->of_node) {
		dev_err(musb->controller, "Need DT for the DMA engine.\n");
		return NULL;
	}

	controller = kzalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		goto kzalloc_fail;

	hrtimer_init(&controller->early_tx, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	controller->early_tx.function = cppi41_recheck_tx_req;
	INIT_LIST_HEAD(&controller->early_tx_list);
	controller->musb = musb;

	controller->controller.channel_alloc = cppi41_dma_channel_allocate;
	controller->controller.channel_release = cppi41_dma_channel_release;
	controller->controller.channel_program = cppi41_dma_channel_program;
	controller->controller.channel_abort = cppi41_dma_channel_abort;
	controller->controller.is_compatible = cppi41_is_compatible;

	ret = cppi41_dma_controller_start(controller);
	if (ret)
		goto plat_get_fail;
	return &controller->controller;

plat_get_fail:
	kfree(controller);
kzalloc_fail:
	if (ret == -EPROBE_DEFER)
		return ERR_PTR(ret);
	return NULL;
}
