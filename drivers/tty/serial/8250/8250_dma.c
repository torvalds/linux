// SPDX-License-Identifier: GPL-2.0+
/*
 * 8250_dma.c - DMA Engine API support for 8250.c
 *
 * Copyright (C) 2013 Intel Corporation
 */
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/dma-mapping.h>

#include "8250.h"

#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)
#define MAX_TX_BYTES		64
#define MAX_FIFO_SIZE		64
#define UART_RFL_16550A		0x21
#endif

static void __dma_tx_complete(void *param)
{
	struct uart_8250_port	*p = param;
	struct uart_8250_dma	*dma = p->dma;
	struct circ_buf		*xmit = &p->port.state->xmit;
	unsigned long	flags;
	int		ret;

	dma_sync_single_for_cpu(dma->txchan->device->dev, dma->tx_addr,
				UART_XMIT_SIZE, DMA_TO_DEVICE);

	spin_lock_irqsave(&p->port.lock, flags);

	dma->tx_running = 0;

	xmit->tail += dma->tx_size;
	xmit->tail &= UART_XMIT_SIZE - 1;
	p->port.icount.tx += dma->tx_size;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&p->port);

	ret = serial8250_tx_dma(p);
	if (ret)
		serial8250_set_THRI(p);

	spin_unlock_irqrestore(&p->port.lock, flags);
}

#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)

static void __dma_rx_complete(void *param)
{
	struct uart_8250_port	*p = param;
	struct uart_8250_dma	*dma = p->dma;
	struct tty_port		*tty_port = &p->port.state->port;
	struct dma_tx_state	state;
	unsigned int		count = 0, cur_index = 0;

	dmaengine_tx_status(dma->rxchan, dma->rx_cookie, &state);
	cur_index = dma->rx_size - state.residue;

	if (cur_index == dma->rx_index)
		return;
	else if (cur_index > dma->rx_index)
		count = cur_index - dma->rx_index;
	else
		count = dma->rx_size - dma->rx_index;

	tty_insert_flip_string(tty_port, dma->rx_buf + dma->rx_index, count);

	if (cur_index < dma->rx_index) {
		tty_insert_flip_string(tty_port, dma->rx_buf, cur_index);
		count += cur_index;
	}

	p->port.icount.rx += count;
	dma->rx_index = cur_index;
}

#else

static void __dma_rx_complete(void *param)
{
	struct uart_8250_port	*p = param;
	struct uart_8250_dma	*dma = p->dma;
	struct tty_port		*tty_port = &p->port.state->port;
	struct dma_tx_state	state;
	int			count;

	dma->rx_running = 0;
	dmaengine_tx_status(dma->rxchan, dma->rx_cookie, &state);

	count = dma->rx_size - state.residue;

	tty_insert_flip_string(tty_port, dma->rx_buf, count);
	p->port.icount.rx += count;

	tty_flip_buffer_push(tty_port);
}

#endif

int serial8250_tx_dma(struct uart_8250_port *p)
{
	struct uart_8250_dma		*dma = p->dma;
	struct circ_buf			*xmit = &p->port.state->xmit;
	struct dma_async_tx_descriptor	*desc;
	int ret;

	if (dma->tx_running)
		return 0;

	if (uart_tx_stopped(&p->port) || uart_circ_empty(xmit)) {
		/* We have been called from __dma_tx_complete() */
		serial8250_rpm_put_tx(p);
		return 0;
	}

	dma->tx_size = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);
#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)
	if (dma->tx_size < MAX_TX_BYTES) {
		ret = -EBUSY;
		goto err;
	}
#endif
	desc = dmaengine_prep_slave_single(dma->txchan,
					   dma->tx_addr + xmit->tail,
					   dma->tx_size, DMA_MEM_TO_DEV,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		ret = -EBUSY;
		goto err;
	}

	dma->tx_running = 1;
	desc->callback = __dma_tx_complete;
	desc->callback_param = p;

	dma->tx_cookie = dmaengine_submit(desc);

	dma_sync_single_for_device(dma->txchan->device->dev, dma->tx_addr,
				   UART_XMIT_SIZE, DMA_TO_DEVICE);

	dma_async_issue_pending(dma->txchan);
	if (dma->tx_err) {
		dma->tx_err = 0;
		serial8250_clear_THRI(p);
	}
	return 0;
err:
	dma->tx_err = 1;
	return ret;
}

#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)

int serial8250_rx_dma(struct uart_8250_port *p)
{
	unsigned int rfl, i = 0, fcr = 0, cur_index = 0;
	unsigned char buf[MAX_FIFO_SIZE];
	struct uart_port	*port = &p->port;
	struct tty_port		*tty_port = &p->port.state->port;
	struct dma_tx_state	state;
	struct uart_8250_dma	*dma = p->dma;

	fcr = UART_FCR_ENABLE_FIFO | UART_FCR_T_TRIG_10 | UART_FCR_R_TRIG_11;
	serial_port_out(port, UART_FCR, fcr);

	do {
		dmaengine_tx_status(dma->rxchan, dma->rx_cookie, &state);
		cur_index = dma->rx_size - state.residue;
	} while (cur_index % dma->rxconf.src_maxburst);

	rfl = serial_port_in(port, UART_RFL_16550A);
	while (i < rfl)
		buf[i++] = serial_port_in(port, UART_RX);

	__dma_rx_complete(p);

	tty_insert_flip_string(tty_port, buf, i);
	p->port.icount.rx += i;
	tty_flip_buffer_push(tty_port);

	if (fcr)
		serial_port_out(port, UART_FCR, p->fcr);
	return 0;
}

int serial8250_start_rx_dma(struct uart_8250_port *p)
{
	struct uart_8250_dma		*dma = p->dma;
	struct dma_async_tx_descriptor	*desc;

	desc = dmaengine_prep_dma_cyclic(dma->rxchan, dma->rx_addr,
					 dma->rx_size, dma->rx_size,
					 DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT |
					 DMA_CTRL_ACK);
	if (!desc)
		return -EBUSY;

	dma->rx_running = 1;
	desc->callback = NULL;
	desc->callback_param = NULL;

	dma->rx_cookie = dmaengine_submit(desc);
	dma_async_issue_pending(dma->rxchan);
	dma->rx_index = 0;
	return 0;
}

#else

int serial8250_rx_dma(struct uart_8250_port *p)
{
	struct uart_8250_dma		*dma = p->dma;
	struct dma_async_tx_descriptor	*desc;

	if (dma->rx_running)
		return 0;

	desc = dmaengine_prep_slave_single(dma->rxchan, dma->rx_addr,
					   dma->rx_size, DMA_DEV_TO_MEM,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		return -EBUSY;

	dma->rx_running = 1;
	desc->callback = __dma_rx_complete;
	desc->callback_param = p;

	dma->rx_cookie = dmaengine_submit(desc);

	dma_async_issue_pending(dma->rxchan);

	return 0;
}

#endif

void serial8250_rx_dma_flush(struct uart_8250_port *p)
{
	struct uart_8250_dma *dma = p->dma;

	if (dma->rx_running) {
		dmaengine_pause(dma->rxchan);
		__dma_rx_complete(p);
		dmaengine_terminate_async(dma->rxchan);
	}
}
EXPORT_SYMBOL_GPL(serial8250_rx_dma_flush);

int serial8250_request_dma(struct uart_8250_port *p)
{
	struct uart_8250_dma	*dma = p->dma;
	phys_addr_t rx_dma_addr = dma->rx_dma_addr ?
				  dma->rx_dma_addr : p->port.mapbase;
	phys_addr_t tx_dma_addr = dma->tx_dma_addr ?
				  dma->tx_dma_addr : p->port.mapbase;
	dma_cap_mask_t		mask;
	struct dma_slave_caps	caps;
	int			ret;

	/* Default slave configuration parameters */
	dma->rxconf.direction		= DMA_DEV_TO_MEM;
	dma->rxconf.src_addr_width	= DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma->rxconf.src_addr		= rx_dma_addr + UART_RX;
#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)
	if ((p->port.fifosize / 4) < 16)
		dma->rxconf.src_maxburst = p->port.fifosize / 4;
	else
		dma->rxconf.src_maxburst = 16;
#endif

	dma->txconf.direction		= DMA_MEM_TO_DEV;
	dma->txconf.dst_addr_width	= DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma->txconf.dst_addr		= tx_dma_addr + UART_TX;
#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)
	dma->txconf.dst_maxburst	= 16;
#endif
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Get a channel for RX */
	dma->rxchan = dma_request_slave_channel_compat(mask,
						       dma->fn, dma->rx_param,
						       p->port.dev, "rx");
	if (!dma->rxchan)
		return -ENODEV;

	/* 8250 rx dma requires dmaengine driver to support pause/terminate */
	ret = dma_get_slave_caps(dma->rxchan, &caps);
	if (ret)
		goto release_rx;
	if (!caps.cmd_pause || !caps.cmd_terminate ||
	    caps.residue_granularity == DMA_RESIDUE_GRANULARITY_DESCRIPTOR) {
		ret = -EINVAL;
		goto release_rx;
	}

	dmaengine_slave_config(dma->rxchan, &dma->rxconf);

	/* RX buffer */
#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)
	if (!dma->rx_size)
		dma->rx_size = PAGE_SIZE * 2;
#else
	if (!dma->rx_size)
		dma->rx_size = PAGE_SIZE;
#endif

	dma->rx_buf = dma_alloc_coherent(dma->rxchan->device->dev, dma->rx_size,
					&dma->rx_addr, GFP_KERNEL);
	if (!dma->rx_buf) {
		ret = -ENOMEM;
		goto release_rx;
	}

	/* Get a channel for TX */
	dma->txchan = dma_request_slave_channel_compat(mask,
						       dma->fn, dma->tx_param,
						       p->port.dev, "tx");
	if (dma->txchan) {
		dmaengine_slave_config(dma->txchan, &dma->txconf);

		/* TX buffer */
		dma->tx_addr = dma_map_single(dma->txchan->device->dev,
						p->port.state->xmit.buf,
						UART_XMIT_SIZE,
						DMA_TO_DEVICE);
		if (dma_mapping_error(dma->txchan->device->dev, dma->tx_addr)) {
			dma_free_coherent(dma->rxchan->device->dev,
					  dma->rx_size, dma->rx_buf,
					  dma->rx_addr);
			dma_release_channel(dma->txchan);
			dma->txchan = NULL;
		}

		dev_info_ratelimited(p->port.dev, "got rx and tx dma channels\n");
	} else {
		dev_info_ratelimited(p->port.dev, "got rx dma channels only\n");
	}

#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)
	/* start dma for rx*/
	serial8250_start_rx_dma(p);
#endif
	return 0;
release_rx:
	dma_release_channel(dma->rxchan);
	return ret;
}
EXPORT_SYMBOL_GPL(serial8250_request_dma);

void serial8250_release_dma(struct uart_8250_port *p)
{
	struct uart_8250_dma *dma = p->dma;

	if (!dma)
		return;

	/* Release RX resources */
	dmaengine_terminate_sync(dma->rxchan);
	dma_free_coherent(dma->rxchan->device->dev, dma->rx_size, dma->rx_buf,
			  dma->rx_addr);
	dma_release_channel(dma->rxchan);
	dma->rxchan = NULL;
#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)
	dma->rx_running = 0;
#endif
	/* Release TX resources */
	if (dma->txchan) {
		dmaengine_terminate_all(dma->txchan);
		dma_unmap_single(dma->txchan->device->dev, dma->tx_addr,
				 UART_XMIT_SIZE, DMA_TO_DEVICE);
		dma_release_channel(dma->txchan);
		dma->txchan = NULL;
		dma->tx_running = 0;
	}
	dev_dbg_ratelimited(p->port.dev, "dma channels released\n");
}
EXPORT_SYMBOL_GPL(serial8250_release_dma);
