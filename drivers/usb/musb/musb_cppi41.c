// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/sizes.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include "cppi_dma.h"
#include "musb_core.h"
#include "musb_trace.h"

#define RNDIS_REG(x) (0x80 + ((x - 1) * 4))

#define EP_MODE_AUTOREQ_NONE		0
#define EP_MODE_AUTOREQ_ALL_NEOP	1
#define EP_MODE_AUTOREQ_ALWAYS		3

#define EP_MODE_DMA_TRANSPARENT		0
#define EP_MODE_DMA_RNDIS		1
#define EP_MODE_DMA_GEN_RNDIS		3

#define USB_CTRL_TX_MODE	0x70
#define USB_CTRL_RX_MODE	0x74
#define USB_CTRL_AUTOREQ	0xd0
#define USB_TDOWN		0xd8

#define MUSB_DMA_NUM_CHANNELS 15

#define DA8XX_USB_MODE		0x10
#define DA8XX_USB_AUTOREQ	0x14
#define DA8XX_USB_TEARDOWN	0x1c

#define DA8XX_DMA_NUM_CHANNELS 4

struct cppi41_dma_controller {
	struct dma_controller controller;
	struct cppi41_dma_channel *rx_channel;
	struct cppi41_dma_channel *tx_channel;
	struct hrtimer early_tx;
	struct list_head early_tx_list;
	u32 rx_mode;
	u32 tx_mode;
	u32 auto_req;

	u32 tdown_reg;
	u32 autoreq_reg;

	void (*set_dma_mode)(struct cppi41_dma_channel *cppi41_channel,
			     unsigned int mode);
	u8 num_channels;
};

static void save_rx_toggle(struct cppi41_dma_channel *cppi41_channel)
{
	u16 csr;
	u8 toggle;

	if (cppi41_channel->is_tx)
		return;
	if (!is_host_active(cppi41_channel->controller->controller.musb))
		return;

	csr = musb_readw(cppi41_channel->hw_ep->regs, MUSB_RXCSR);
	toggle = csr & MUSB_RXCSR_H_DATATOGGLE ? 1 : 0;

	cppi41_channel->usb_toggle = toggle;
}

static void update_rx_toggle(struct cppi41_dma_channel *cppi41_channel)
{
	struct musb_hw_ep *hw_ep = cppi41_channel->hw_ep;
	struct musb *musb = hw_ep->musb;
	u16 csr;
	u8 toggle;

	if (cppi41_channel->is_tx)
		return;
	if (!is_host_active(musb))
		return;

	musb_ep_select(musb->mregs, hw_ep->epnum);
	csr = musb_readw(hw_ep->regs, MUSB_RXCSR);
	toggle = csr & MUSB_RXCSR_H_DATATOGGLE ? 1 : 0;

	/*
	 * AM335x Advisory 1.0.13: Due to internal synchronisation error the
	 * data toggle may reset from DATA1 to DATA0 during receiving data from
	 * more than one endpoint.
	 */
	if (!toggle && toggle == cppi41_channel->usb_toggle) {
		csr |= MUSB_RXCSR_H_DATATOGGLE | MUSB_RXCSR_H_WR_DATATOGGLE;
		musb_writew(cppi41_channel->hw_ep->regs, MUSB_RXCSR, csr);
		musb_dbg(musb, "Restoring DATA1 toggle.");
	}

	cppi41_channel->usb_toggle = toggle;
}

static bool musb_is_tx_fifo_empty(struct musb_hw_ep *hw_ep)
{
	u8		epnum = hw_ep->epnum;
	struct musb	*musb = hw_ep->musb;
	void __iomem	*epio = musb->endpoints[epnum].regs;
	u16		csr;

	musb_ep_select(musb->mregs, hw_ep->epnum);
	csr = musb_readw(epio, MUSB_TXCSR);
	if (csr & MUSB_TXCSR_TXPKTRDY)
		return false;
	return true;
}

static void cppi41_dma_callback(void *private_data,
				const struct dmaengine_result *result);

static void cppi41_trans_done(struct cppi41_dma_channel *cppi41_channel)
{
	struct musb_hw_ep *hw_ep = cppi41_channel->hw_ep;
	struct musb *musb = hw_ep->musb;
	void __iomem *epio = hw_ep->regs;
	u16 csr;

	if (!cppi41_channel->prog_len ||
	    (cppi41_channel->channel.status == MUSB_DMA_STATUS_FREE)) {

		/* done, complete */
		cppi41_channel->channel.actual_len =
			cppi41_channel->transferred;
		cppi41_channel->channel.status = MUSB_DMA_STATUS_FREE;
		cppi41_channel->channel.rx_packet_done = true;

		/*
		 * transmit ZLP using PIO mode for transfers which size is
		 * multiple of EP packet size.
		 */
		if (cppi41_channel->tx_zlp && (cppi41_channel->transferred %
					cppi41_channel->packet_sz) == 0) {
			musb_ep_select(musb->mregs, hw_ep->epnum);
			csr = MUSB_TXCSR_MODE | MUSB_TXCSR_TXPKTRDY;
			musb_writew(epio, MUSB_TXCSR, csr);
		}

		trace_musb_cppi41_done(cppi41_channel);
		musb_dma_completion(musb, hw_ep->epnum, cppi41_channel->is_tx);
	} else {
		/* next iteration, reload */
		struct dma_chan *dc = cppi41_channel->dc;
		struct dma_async_tx_descriptor *dma_desc;
		enum dma_transfer_direction direction;
		u32 remain_bytes;

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

		dma_desc->callback_result = cppi41_dma_callback;
		dma_desc->callback_param = &cppi41_channel->channel;
		cppi41_channel->cookie = dma_desc->tx_submit(dma_desc);
		trace_musb_cppi41_cont(cppi41_channel);
		dma_async_issue_pending(dc);

		if (!cppi41_channel->is_tx) {
			musb_ep_select(musb->mregs, hw_ep->epnum);
			csr = musb_readw(epio, MUSB_RXCSR);
			csr |= MUSB_RXCSR_H_REQPKT;
			musb_writew(epio, MUSB_RXCSR, csr);
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
	musb = controller->controller.musb;

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

	if (!list_empty(&controller->early_tx_list) &&
	    !hrtimer_is_queued(&controller->early_tx)) {
		ret = HRTIMER_RESTART;
		hrtimer_forward_now(&controller->early_tx, 20 * NSEC_PER_USEC);
	}

	spin_unlock_irqrestore(&musb->lock, flags);
	return ret;
}

static void cppi41_dma_callback(void *private_data,
				const struct dmaengine_result *result)
{
	struct dma_channel *channel = private_data;
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;
	struct musb_hw_ep *hw_ep = cppi41_channel->hw_ep;
	struct cppi41_dma_controller *controller;
	struct musb *musb = hw_ep->musb;
	unsigned long flags;
	struct dma_tx_state txstate;
	u32 transferred;
	int is_hs = 0;
	bool empty;

	controller = cppi41_channel->controller;
	if (controller->controller.dma_callback)
		controller->controller.dma_callback(&controller->controller);

	if (result->result == DMA_TRANS_ABORTED)
		return;

	spin_lock_irqsave(&musb->lock, flags);

	dmaengine_tx_status(cppi41_channel->dc, cppi41_channel->cookie,
			&txstate);
	transferred = cppi41_channel->prog_len - txstate.residue;
	cppi41_channel->transferred += transferred;

	trace_musb_cppi41_gb(cppi41_channel);
	update_rx_toggle(cppi41_channel);

	if (cppi41_channel->transferred == cppi41_channel->total_len ||
			transferred < cppi41_channel->packet_sz)
		cppi41_channel->prog_len = 0;

	if (cppi41_channel->is_tx) {
		u8 type;

		if (is_host_active(musb))
			type = hw_ep->out_qh->type;
		else
			type = hw_ep->ep_in.type;

		if (type == USB_ENDPOINT_XFER_ISOC)
			/*
			 * Don't use the early-TX-interrupt workaround below
			 * for Isoch transfter. Since Isoch are periodic
			 * transfer, by the time the next transfer is
			 * scheduled, the current one should be done already.
			 *
			 * This avoids audio playback underrun issue.
			 */
			empty = true;
		else
			empty = musb_is_tx_fifo_empty(hw_ep);
	}

	if (!cppi41_channel->is_tx || empty) {
		cppi41_trans_done(cppi41_channel);
		goto out;
	}

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
	if (is_host_active(musb)) {
		if (musb->port1_status & USB_PORT_STAT_HIGH_SPEED)
			is_hs = 1;
	} else {
		if (musb->g.speed == USB_SPEED_HIGH)
			is_hs = 1;
	}
	if (is_hs) {
		unsigned wait = 25;

		do {
			empty = musb_is_tx_fifo_empty(hw_ep);
			if (empty) {
				cppi41_trans_done(cppi41_channel);
				goto out;
			}
			wait--;
			if (!wait)
				break;
			cpu_relax();
		} while (1);
	}
	list_add_tail(&cppi41_channel->tx_check,
			&controller->early_tx_list);
	if (!hrtimer_is_queued(&controller->early_tx)) {
		unsigned long usecs = cppi41_channel->total_len / 10;

		hrtimer_start_range_ns(&controller->early_tx,
				       usecs * NSEC_PER_USEC,
				       20 * NSEC_PER_USEC,
				       HRTIMER_MODE_REL);
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
	struct musb *musb = controller->controller.musb;
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
		musb_writel(musb->ctrl_base, USB_CTRL_TX_MODE, new_mode);
	} else {
		controller->rx_mode = new_mode;
		musb_writel(musb->ctrl_base, USB_CTRL_RX_MODE, new_mode);
	}
}

static void da8xx_set_dma_mode(struct cppi41_dma_channel *cppi41_channel,
		unsigned int mode)
{
	struct cppi41_dma_controller *controller = cppi41_channel->controller;
	struct musb *musb = controller->controller.musb;
	unsigned int shift;
	u32 port;
	u32 new_mode;
	u32 old_mode;

	old_mode = controller->tx_mode;
	port = cppi41_channel->port_num;

	shift = (port - 1) * 4;
	if (!cppi41_channel->is_tx)
		shift += 16;
	new_mode = old_mode & ~(3 << shift);
	new_mode |= mode << shift;

	if (new_mode == old_mode)
		return;
	controller->tx_mode = new_mode;
	musb_writel(musb->ctrl_base, DA8XX_USB_MODE, new_mode);
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
	musb_writel(controller->controller.musb->ctrl_base,
		    controller->autoreq_reg, new_mode);
}

static bool cppi41_configure_channel(struct dma_channel *channel,
				u16 packet_sz, u8 mode,
				dma_addr_t dma_addr, u32 len)
{
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;
	struct cppi41_dma_controller *controller = cppi41_channel->controller;
	struct dma_chan *dc = cppi41_channel->dc;
	struct dma_async_tx_descriptor *dma_desc;
	enum dma_transfer_direction direction;
	struct musb *musb = cppi41_channel->controller->controller.musb;
	unsigned use_gen_rndis = 0;

	cppi41_channel->buf_addr = dma_addr;
	cppi41_channel->total_len = len;
	cppi41_channel->transferred = 0;
	cppi41_channel->packet_sz = packet_sz;
	cppi41_channel->tx_zlp = (cppi41_channel->is_tx && mode) ? 1 : 0;

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
			controller->set_dma_mode(cppi41_channel,
					EP_MODE_DMA_GEN_RNDIS);

			/* auto req */
			cppi41_set_autoreq_mode(cppi41_channel,
					EP_MODE_AUTOREQ_ALL_NEOP);
		} else {
			musb_writel(musb->ctrl_base,
					RNDIS_REG(cppi41_channel->port_num), 0);
			controller->set_dma_mode(cppi41_channel,
					EP_MODE_DMA_TRANSPARENT);
			cppi41_set_autoreq_mode(cppi41_channel,
					EP_MODE_AUTOREQ_NONE);
		}
	} else {
		/* fallback mode */
		controller->set_dma_mode(cppi41_channel,
				EP_MODE_DMA_TRANSPARENT);
		cppi41_set_autoreq_mode(cppi41_channel, EP_MODE_AUTOREQ_NONE);
		len = min_t(u32, packet_sz, len);
	}
	cppi41_channel->prog_len = len;
	direction = cppi41_channel->is_tx ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;
	dma_desc = dmaengine_prep_slave_single(dc, dma_addr, len, direction,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!dma_desc)
		return false;

	dma_desc->callback_result = cppi41_dma_callback;
	dma_desc->callback_param = channel;
	cppi41_channel->cookie = dma_desc->tx_submit(dma_desc);
	cppi41_channel->channel.rx_packet_done = false;

	trace_musb_cppi41_config(cppi41_channel);

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

	if (ch_num >= controller->num_channels)
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

	trace_musb_cppi41_alloc(cppi41_channel);
	return &cppi41_channel->channel;
}

static void cppi41_dma_channel_release(struct dma_channel *channel)
{
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;

	trace_musb_cppi41_free(cppi41_channel);
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

	if (is_host_active(cppi41_channel->controller->controller.musb)) {
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
	struct musb *musb = controller->controller.musb;

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
	struct musb *musb = controller->controller.musb;
	void __iomem *epio = cppi41_channel->hw_ep->regs;
	int tdbit;
	int ret;
	unsigned is_tx;
	u16 csr;

	is_tx = cppi41_channel->is_tx;
	trace_musb_cppi41_abort(cppi41_channel);

	if (cppi41_channel->channel.status == MUSB_DMA_STATUS_FREE)
		return 0;

	list_del_init(&cppi41_channel->tx_check);
	if (is_tx) {
		csr = musb_readw(epio, MUSB_TXCSR);
		csr &= ~MUSB_TXCSR_DMAENAB;
		musb_writew(epio, MUSB_TXCSR, csr);
	} else {
		cppi41_set_autoreq_mode(cppi41_channel, EP_MODE_AUTOREQ_NONE);

		/* delay to drain to cppi dma pipeline for isoch */
		udelay(250);

		csr = musb_readw(epio, MUSB_RXCSR);
		csr &= ~(MUSB_RXCSR_H_REQPKT | MUSB_RXCSR_DMAENAB);
		musb_writew(epio, MUSB_RXCSR, csr);

		/* wait to drain cppi dma pipe line */
		udelay(50);

		csr = musb_readw(epio, MUSB_RXCSR);
		if (csr & MUSB_RXCSR_RXPKTRDY) {
			csr |= MUSB_RXCSR_FLUSHFIFO;
			musb_writew(epio, MUSB_RXCSR, csr);
			musb_writew(epio, MUSB_RXCSR, csr);
		}
	}

	/* DA8xx Advisory 2.3.27: wait 250 ms before to start the teardown */
	if (musb->ops->quirks & MUSB_DA8XX)
		mdelay(250);

	tdbit = 1 << cppi41_channel->port_num;
	if (is_tx)
		tdbit <<= 16;

	do {
		if (is_tx)
			musb_writel(musb->ctrl_base, controller->tdown_reg,
				    tdbit);
		ret = dmaengine_terminate_all(cppi41_channel->dc);
	} while (ret == -EAGAIN);

	if (is_tx) {
		musb_writel(musb->ctrl_base, controller->tdown_reg, tdbit);

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

	for (i = 0; i < ctrl->num_channels; i++) {
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
	struct musb *musb = controller->controller.musb;
	struct device *dev = musb->controller;
	struct device_node *np = dev->parent->of_node;
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
		if (strstarts(str, "tx"))
			is_tx = 1;
		else if (strstarts(str, "rx"))
			is_tx = 0;
		else {
			dev_err(dev, "Wrong dmatype %s\n", str);
			goto err;
		}
		ret = kstrtouint(str + 2, 0, &port);
		if (ret)
			goto err;

		ret = -EINVAL;
		if (port > controller->num_channels || !port)
			goto err;
		if (is_tx)
			cppi41_channel = &controller->tx_channel[port - 1];
		else
			cppi41_channel = &controller->rx_channel[port - 1];

		cppi41_channel->controller = controller;
		cppi41_channel->port_num = port;
		cppi41_channel->is_tx = is_tx;
		INIT_LIST_HEAD(&cppi41_channel->tx_check);

		musb_dma = &cppi41_channel->channel;
		musb_dma->private_data = cppi41_channel;
		musb_dma->status = MUSB_DMA_STATUS_FREE;
		musb_dma->max_len = SZ_4M;

		dc = dma_request_chan(dev->parent, str);
		if (IS_ERR(dc)) {
			ret = PTR_ERR(dc);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Failed to request %s: %d.\n",
					str, ret);
			goto err;
		}

		cppi41_channel->dc = dc;
	}
	return 0;
err:
	cppi41_release_all_dma_chans(controller);
	return ret;
}

void cppi41_dma_controller_destroy(struct dma_controller *c)
{
	struct cppi41_dma_controller *controller = container_of(c,
			struct cppi41_dma_controller, controller);

	hrtimer_cancel(&controller->early_tx);
	cppi41_dma_controller_stop(controller);
	kfree(controller->rx_channel);
	kfree(controller->tx_channel);
	kfree(controller);
}
EXPORT_SYMBOL_GPL(cppi41_dma_controller_destroy);

struct dma_controller *
cppi41_dma_controller_create(struct musb *musb, void __iomem *base)
{
	struct cppi41_dma_controller *controller;
	int channel_size;
	int ret = 0;

	if (!musb->controller->parent->of_node) {
		dev_err(musb->controller, "Need DT for the DMA engine.\n");
		return NULL;
	}

	controller = kzalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		goto kzalloc_fail;

	hrtimer_init(&controller->early_tx, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	controller->early_tx.function = cppi41_recheck_tx_req;
	INIT_LIST_HEAD(&controller->early_tx_list);

	controller->controller.channel_alloc = cppi41_dma_channel_allocate;
	controller->controller.channel_release = cppi41_dma_channel_release;
	controller->controller.channel_program = cppi41_dma_channel_program;
	controller->controller.channel_abort = cppi41_dma_channel_abort;
	controller->controller.is_compatible = cppi41_is_compatible;
	controller->controller.musb = musb;

	if (musb->ops->quirks & MUSB_DA8XX) {
		controller->tdown_reg = DA8XX_USB_TEARDOWN;
		controller->autoreq_reg = DA8XX_USB_AUTOREQ;
		controller->set_dma_mode = da8xx_set_dma_mode;
		controller->num_channels = DA8XX_DMA_NUM_CHANNELS;
	} else {
		controller->tdown_reg = USB_TDOWN;
		controller->autoreq_reg = USB_CTRL_AUTOREQ;
		controller->set_dma_mode = cppi41_set_dma_mode;
		controller->num_channels = MUSB_DMA_NUM_CHANNELS;
	}

	channel_size = controller->num_channels *
			sizeof(struct cppi41_dma_channel);
	controller->rx_channel = kzalloc(channel_size, GFP_KERNEL);
	if (!controller->rx_channel)
		goto rx_channel_alloc_fail;
	controller->tx_channel = kzalloc(channel_size, GFP_KERNEL);
	if (!controller->tx_channel)
		goto tx_channel_alloc_fail;

	ret = cppi41_dma_controller_start(controller);
	if (ret)
		goto plat_get_fail;
	return &controller->controller;

plat_get_fail:
	kfree(controller->tx_channel);
tx_channel_alloc_fail:
	kfree(controller->rx_channel);
rx_channel_alloc_fail:
	kfree(controller);
kzalloc_fail:
	if (ret == -EPROBE_DEFER)
		return ERR_PTR(ret);
	return NULL;
}
EXPORT_SYMBOL_GPL(cppi41_dma_controller_create);
