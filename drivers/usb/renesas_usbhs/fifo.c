/*
 * Renesas USB driver
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/scatterlist.h>
#include "./common.h"
#include "./pipe.h"

#define usbhsf_get_cfifo(p)	(&((p)->fifo_info.cfifo))
#define usbhsf_get_d0fifo(p)	(&((p)->fifo_info.d0fifo))
#define usbhsf_get_d1fifo(p)	(&((p)->fifo_info.d1fifo))

#define usbhsf_fifo_is_busy(f)	((f)->pipe) /* see usbhs_pipe_select_fifo */

/*
 *		packet initialize
 */
void usbhs_pkt_init(struct usbhs_pkt *pkt)
{
	pkt->dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD(&pkt->node);
}

/*
 *		packet control function
 */
static int usbhsf_null_handle(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pkt->pipe);
	struct device *dev = usbhs_priv_to_dev(priv);

	dev_err(dev, "null handler\n");

	return -EINVAL;
}

static struct usbhs_pkt_handle usbhsf_null_handler = {
	.prepare = usbhsf_null_handle,
	.try_run = usbhsf_null_handle,
};

void usbhs_pkt_push(struct usbhs_pipe *pipe, struct usbhs_pkt *pkt,
		    void (*done)(struct usbhs_priv *priv,
				 struct usbhs_pkt *pkt),
		    void *buf, int len, int zero, int sequence)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct device *dev = usbhs_priv_to_dev(priv);
	unsigned long flags;

	if (!done) {
		dev_err(dev, "no done function\n");
		return;
	}

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	if (!pipe->handler) {
		dev_err(dev, "no handler function\n");
		pipe->handler = &usbhsf_null_handler;
	}

	list_del_init(&pkt->node);
	list_add_tail(&pkt->node, &pipe->list);

	/*
	 * each pkt must hold own handler.
	 * because handler might be changed by its situation.
	 * dma handler -> pio handler.
	 */
	pkt->pipe	= pipe;
	pkt->buf	= buf;
	pkt->handler	= pipe->handler;
	pkt->length	= len;
	pkt->zero	= zero;
	pkt->actual	= 0;
	pkt->done	= done;
	pkt->sequence	= sequence;

	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/
}

static void __usbhsf_pkt_del(struct usbhs_pkt *pkt)
{
	list_del_init(&pkt->node);
}

static struct usbhs_pkt *__usbhsf_pkt_get(struct usbhs_pipe *pipe)
{
	if (list_empty(&pipe->list))
		return NULL;

	return list_entry(pipe->list.next, struct usbhs_pkt, node);
}

struct usbhs_pkt *usbhs_pkt_pop(struct usbhs_pipe *pipe, struct usbhs_pkt *pkt)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	unsigned long flags;

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	if (!pkt)
		pkt = __usbhsf_pkt_get(pipe);

	if (pkt)
		__usbhsf_pkt_del(pkt);

	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/

	return pkt;
}

enum {
	USBHSF_PKT_PREPARE,
	USBHSF_PKT_TRY_RUN,
	USBHSF_PKT_DMA_DONE,
};

static int usbhsf_pkt_handler(struct usbhs_pipe *pipe, int type)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_pkt *pkt;
	struct device *dev = usbhs_priv_to_dev(priv);
	int (*func)(struct usbhs_pkt *pkt, int *is_done);
	unsigned long flags;
	int ret = 0;
	int is_done = 0;

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	pkt = __usbhsf_pkt_get(pipe);
	if (!pkt)
		goto __usbhs_pkt_handler_end;

	switch (type) {
	case USBHSF_PKT_PREPARE:
		func = pkt->handler->prepare;
		break;
	case USBHSF_PKT_TRY_RUN:
		func = pkt->handler->try_run;
		break;
	case USBHSF_PKT_DMA_DONE:
		func = pkt->handler->dma_done;
		break;
	default:
		dev_err(dev, "unknown pkt hander\n");
		goto __usbhs_pkt_handler_end;
	}

	ret = func(pkt, &is_done);

	if (is_done)
		__usbhsf_pkt_del(pkt);

__usbhs_pkt_handler_end:
	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/

	if (is_done) {
		pkt->done(priv, pkt);
		usbhs_pkt_start(pipe);
	}

	return ret;
}

void usbhs_pkt_start(struct usbhs_pipe *pipe)
{
	usbhsf_pkt_handler(pipe, USBHSF_PKT_PREPARE);
}

/*
 *		irq enable/disable function
 */
#define usbhsf_irq_empty_ctrl(p, e) usbhsf_irq_callback_ctrl(p, bempsts, e)
#define usbhsf_irq_ready_ctrl(p, e) usbhsf_irq_callback_ctrl(p, brdysts, e)
#define usbhsf_irq_callback_ctrl(pipe, status, enable)			\
	({								\
		struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);	\
		struct usbhs_mod *mod = usbhs_mod_get_current(priv);	\
		u16 status = (1 << usbhs_pipe_number(pipe));		\
		if (!mod)						\
			return;						\
		if (enable)						\
			mod->irq_##status |= status;			\
		else							\
			mod->irq_##status &= ~status;			\
		usbhs_irq_callback_update(priv, mod);			\
	})

static void usbhsf_tx_irq_ctrl(struct usbhs_pipe *pipe, int enable)
{
	/*
	 * And DCP pipe can NOT use "ready interrupt" for "send"
	 * it should use "empty" interrupt.
	 * see
	 *   "Operation" - "Interrupt Function" - "BRDY Interrupt"
	 *
	 * on the other hand, normal pipe can use "ready interrupt" for "send"
	 * even though it is single/double buffer
	 */
	if (usbhs_pipe_is_dcp(pipe))
		usbhsf_irq_empty_ctrl(pipe, enable);
	else
		usbhsf_irq_ready_ctrl(pipe, enable);
}

static void usbhsf_rx_irq_ctrl(struct usbhs_pipe *pipe, int enable)
{
	usbhsf_irq_ready_ctrl(pipe, enable);
}

/*
 *		FIFO ctrl
 */
static void usbhsf_send_terminator(struct usbhs_pipe *pipe,
				   struct usbhs_fifo *fifo)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);

	usbhs_bset(priv, fifo->ctr, BVAL, BVAL);
}

static int usbhsf_fifo_barrier(struct usbhs_priv *priv,
			       struct usbhs_fifo *fifo)
{
	int timeout = 1024;

	do {
		/* The FIFO port is accessible */
		if (usbhs_read(priv, fifo->ctr) & FRDY)
			return 0;

		udelay(10);
	} while (timeout--);

	return -EBUSY;
}

static void usbhsf_fifo_clear(struct usbhs_pipe *pipe,
			      struct usbhs_fifo *fifo)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);

	if (!usbhs_pipe_is_dcp(pipe))
		usbhsf_fifo_barrier(priv, fifo);

	usbhs_write(priv, fifo->ctr, BCLR);
}

static int usbhsf_fifo_rcv_len(struct usbhs_priv *priv,
			       struct usbhs_fifo *fifo)
{
	return usbhs_read(priv, fifo->ctr) & DTLN_MASK;
}

static void usbhsf_fifo_unselect(struct usbhs_pipe *pipe,
				 struct usbhs_fifo *fifo)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);

	usbhs_pipe_select_fifo(pipe, NULL);
	usbhs_write(priv, fifo->sel, 0);
}

static int usbhsf_fifo_select(struct usbhs_pipe *pipe,
			      struct usbhs_fifo *fifo,
			      int write)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct device *dev = usbhs_priv_to_dev(priv);
	int timeout = 1024;
	u16 mask = ((1 << 5) | 0xF);		/* mask of ISEL | CURPIPE */
	u16 base = usbhs_pipe_number(pipe);	/* CURPIPE */

	if (usbhs_pipe_is_busy(pipe) ||
	    usbhsf_fifo_is_busy(fifo))
		return -EBUSY;

	if (usbhs_pipe_is_dcp(pipe)) {
		base |= (1 == write) << 5;	/* ISEL */

		if (usbhs_mod_is_host(priv))
			usbhs_dcp_dir_for_host(pipe, write);
	}

	/* "base" will be used below  */
	usbhs_write(priv, fifo->sel, base | MBW_32);

	/* check ISEL and CURPIPE value */
	while (timeout--) {
		if (base == (mask & usbhs_read(priv, fifo->sel))) {
			usbhs_pipe_select_fifo(pipe, fifo);
			return 0;
		}
		udelay(10);
	}

	dev_err(dev, "fifo select error\n");

	return -EIO;
}

/*
 *		DCP status stage
 */
static int usbhs_dcp_dir_switch_to_write(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_fifo *fifo = usbhsf_get_cfifo(priv); /* CFIFO */
	struct device *dev = usbhs_priv_to_dev(priv);
	int ret;

	usbhs_pipe_disable(pipe);

	ret = usbhsf_fifo_select(pipe, fifo, 1);
	if (ret < 0) {
		dev_err(dev, "%s() faile\n", __func__);
		return ret;
	}

	usbhs_pipe_sequence_data1(pipe); /* DATA1 */

	usbhsf_fifo_clear(pipe, fifo);
	usbhsf_send_terminator(pipe, fifo);

	usbhsf_fifo_unselect(pipe, fifo);

	usbhsf_tx_irq_ctrl(pipe, 1);
	usbhs_pipe_enable(pipe);

	return ret;
}

static int usbhs_dcp_dir_switch_to_read(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_fifo *fifo = usbhsf_get_cfifo(priv); /* CFIFO */
	struct device *dev = usbhs_priv_to_dev(priv);
	int ret;

	usbhs_pipe_disable(pipe);

	ret = usbhsf_fifo_select(pipe, fifo, 0);
	if (ret < 0) {
		dev_err(dev, "%s() fail\n", __func__);
		return ret;
	}

	usbhs_pipe_sequence_data1(pipe); /* DATA1 */
	usbhsf_fifo_clear(pipe, fifo);

	usbhsf_fifo_unselect(pipe, fifo);

	usbhsf_rx_irq_ctrl(pipe, 1);
	usbhs_pipe_enable(pipe);

	return ret;

}

static int usbhs_dcp_dir_switch_done(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;

	if (pkt->handler == &usbhs_dcp_status_stage_in_handler)
		usbhsf_tx_irq_ctrl(pipe, 0);
	else
		usbhsf_rx_irq_ctrl(pipe, 0);

	pkt->actual = pkt->length;
	*is_done = 1;

	return 0;
}

struct usbhs_pkt_handle usbhs_dcp_status_stage_in_handler = {
	.prepare = usbhs_dcp_dir_switch_to_write,
	.try_run = usbhs_dcp_dir_switch_done,
};

struct usbhs_pkt_handle usbhs_dcp_status_stage_out_handler = {
	.prepare = usbhs_dcp_dir_switch_to_read,
	.try_run = usbhs_dcp_dir_switch_done,
};

/*
 *		DCP data stage (push)
 */
static int usbhsf_dcp_data_stage_try_push(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;

	usbhs_pipe_sequence_data1(pipe); /* DATA1 */

	/*
	 * change handler to PIO push
	 */
	pkt->handler = &usbhs_fifo_pio_push_handler;

	return pkt->handler->prepare(pkt, is_done);
}

struct usbhs_pkt_handle usbhs_dcp_data_stage_out_handler = {
	.prepare = usbhsf_dcp_data_stage_try_push,
};

/*
 *		DCP data stage (pop)
 */
static int usbhsf_dcp_data_stage_prepare_pop(struct usbhs_pkt *pkt,
					     int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_fifo *fifo = usbhsf_get_cfifo(priv);

	if (usbhs_pipe_is_busy(pipe))
		return 0;

	/*
	 * prepare pop for DCP should
	 *  - change DCP direction,
	 *  - clear fifo
	 *  - DATA1
	 */
	usbhs_pipe_disable(pipe);

	usbhs_pipe_sequence_data1(pipe); /* DATA1 */

	usbhsf_fifo_select(pipe, fifo, 0);
	usbhsf_fifo_clear(pipe, fifo);
	usbhsf_fifo_unselect(pipe, fifo);

	/*
	 * change handler to PIO pop
	 */
	pkt->handler = &usbhs_fifo_pio_pop_handler;

	return pkt->handler->prepare(pkt, is_done);
}

struct usbhs_pkt_handle usbhs_dcp_data_stage_in_handler = {
	.prepare = usbhsf_dcp_data_stage_prepare_pop,
};

/*
 *		PIO push handler
 */
static int usbhsf_pio_try_push(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct device *dev = usbhs_priv_to_dev(priv);
	struct usbhs_fifo *fifo = usbhsf_get_cfifo(priv); /* CFIFO */
	void __iomem *addr = priv->base + fifo->port;
	u8 *buf;
	int maxp = usbhs_pipe_get_maxpacket(pipe);
	int total_len;
	int i, ret, len;
	int is_short;

	usbhs_pipe_data_sequence(pipe, pkt->sequence);
	pkt->sequence = -1; /* -1 sequence will be ignored */

	ret = usbhsf_fifo_select(pipe, fifo, 1);
	if (ret < 0)
		return 0;

	ret = usbhs_pipe_is_accessible(pipe);
	if (ret < 0) {
		/* inaccessible pipe is not an error */
		ret = 0;
		goto usbhs_fifo_write_busy;
	}

	ret = usbhsf_fifo_barrier(priv, fifo);
	if (ret < 0)
		goto usbhs_fifo_write_busy;

	buf		= pkt->buf    + pkt->actual;
	len		= pkt->length - pkt->actual;
	len		= min(len, maxp);
	total_len	= len;
	is_short	= total_len < maxp;

	/*
	 * FIXME
	 *
	 * 32-bit access only
	 */
	if (len >= 4 && !((unsigned long)buf & 0x03)) {
		iowrite32_rep(addr, buf, len / 4);
		len %= 4;
		buf += total_len - len;
	}

	/* the rest operation */
	for (i = 0; i < len; i++)
		iowrite8(buf[i], addr + (0x03 - (i & 0x03)));

	/*
	 * variable update
	 */
	pkt->actual += total_len;

	if (pkt->actual < pkt->length)
		*is_done = 0;		/* there are remainder data */
	else if (is_short)
		*is_done = 1;		/* short packet */
	else
		*is_done = !pkt->zero;	/* send zero packet ? */

	/*
	 * pipe/irq handling
	 */
	if (is_short)
		usbhsf_send_terminator(pipe, fifo);

	usbhsf_tx_irq_ctrl(pipe, !*is_done);
	usbhs_pipe_enable(pipe);

	dev_dbg(dev, "  send %d (%d/ %d/ %d/ %d)\n",
		usbhs_pipe_number(pipe),
		pkt->length, pkt->actual, *is_done, pkt->zero);

	/*
	 * Transmission end
	 */
	if (*is_done) {
		if (usbhs_pipe_is_dcp(pipe))
			usbhs_dcp_control_transfer_done(pipe);
	}

	usbhsf_fifo_unselect(pipe, fifo);

	return 0;

usbhs_fifo_write_busy:
	usbhsf_fifo_unselect(pipe, fifo);

	/*
	 * pipe is busy.
	 * retry in interrupt
	 */
	usbhsf_tx_irq_ctrl(pipe, 1);

	return ret;
}

struct usbhs_pkt_handle usbhs_fifo_pio_push_handler = {
	.prepare = usbhsf_pio_try_push,
	.try_run = usbhsf_pio_try_push,
};

/*
 *		PIO pop handler
 */
static int usbhsf_prepare_pop(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;

	if (usbhs_pipe_is_busy(pipe))
		return 0;

	/*
	 * pipe enable to prepare packet receive
	 */
	usbhs_pipe_data_sequence(pipe, pkt->sequence);
	pkt->sequence = -1; /* -1 sequence will be ignored */

	usbhs_pipe_enable(pipe);
	usbhsf_rx_irq_ctrl(pipe, 1);

	return 0;
}

static int usbhsf_pio_try_pop(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct device *dev = usbhs_priv_to_dev(priv);
	struct usbhs_fifo *fifo = usbhsf_get_cfifo(priv); /* CFIFO */
	void __iomem *addr = priv->base + fifo->port;
	u8 *buf;
	u32 data = 0;
	int maxp = usbhs_pipe_get_maxpacket(pipe);
	int rcv_len, len;
	int i, ret;
	int total_len = 0;

	ret = usbhsf_fifo_select(pipe, fifo, 0);
	if (ret < 0)
		return 0;

	ret = usbhsf_fifo_barrier(priv, fifo);
	if (ret < 0)
		goto usbhs_fifo_read_busy;

	rcv_len = usbhsf_fifo_rcv_len(priv, fifo);

	buf		= pkt->buf    + pkt->actual;
	len		= pkt->length - pkt->actual;
	len		= min(len, rcv_len);
	total_len	= len;

	/*
	 * update actual length first here to decide disable pipe.
	 * if this pipe keeps BUF status and all data were popped,
	 * then, next interrupt/token will be issued again
	 */
	pkt->actual += total_len;

	if ((pkt->actual == pkt->length) ||	/* receive all data */
	    (total_len < maxp)) {		/* short packet */
		*is_done = 1;
		usbhsf_rx_irq_ctrl(pipe, 0);
		usbhs_pipe_disable(pipe);	/* disable pipe first */
	}

	/*
	 * Buffer clear if Zero-Length packet
	 *
	 * see
	 * "Operation" - "FIFO Buffer Memory" - "FIFO Port Function"
	 */
	if (0 == rcv_len) {
		pkt->zero = 1;
		usbhsf_fifo_clear(pipe, fifo);
		goto usbhs_fifo_read_end;
	}

	/*
	 * FIXME
	 *
	 * 32-bit access only
	 */
	if (len >= 4 && !((unsigned long)buf & 0x03)) {
		ioread32_rep(addr, buf, len / 4);
		len %= 4;
		buf += total_len - len;
	}

	/* the rest operation */
	for (i = 0; i < len; i++) {
		if (!(i & 0x03))
			data = ioread32(addr);

		buf[i] = (data >> ((i & 0x03) * 8)) & 0xff;
	}

usbhs_fifo_read_end:
	dev_dbg(dev, "  recv %d (%d/ %d/ %d/ %d)\n",
		usbhs_pipe_number(pipe),
		pkt->length, pkt->actual, *is_done, pkt->zero);

usbhs_fifo_read_busy:
	usbhsf_fifo_unselect(pipe, fifo);

	return ret;
}

struct usbhs_pkt_handle usbhs_fifo_pio_pop_handler = {
	.prepare = usbhsf_prepare_pop,
	.try_run = usbhsf_pio_try_pop,
};

/*
 *		DCP ctrol statge handler
 */
static int usbhsf_ctrl_stage_end(struct usbhs_pkt *pkt, int *is_done)
{
	usbhs_dcp_control_transfer_done(pkt->pipe);

	*is_done = 1;

	return 0;
}

struct usbhs_pkt_handle usbhs_ctrl_stage_end_handler = {
	.prepare = usbhsf_ctrl_stage_end,
	.try_run = usbhsf_ctrl_stage_end,
};

/*
 *		DMA fifo functions
 */
static struct dma_chan *usbhsf_dma_chan_get(struct usbhs_fifo *fifo,
					    struct usbhs_pkt *pkt)
{
	if (&usbhs_fifo_dma_push_handler == pkt->handler)
		return fifo->tx_chan;

	if (&usbhs_fifo_dma_pop_handler == pkt->handler)
		return fifo->rx_chan;

	return NULL;
}

static struct usbhs_fifo *usbhsf_get_dma_fifo(struct usbhs_priv *priv,
					      struct usbhs_pkt *pkt)
{
	struct usbhs_fifo *fifo;

	/* DMA :: D0FIFO */
	fifo = usbhsf_get_d0fifo(priv);
	if (usbhsf_dma_chan_get(fifo, pkt) &&
	    !usbhsf_fifo_is_busy(fifo))
		return fifo;

	/* DMA :: D1FIFO */
	fifo = usbhsf_get_d1fifo(priv);
	if (usbhsf_dma_chan_get(fifo, pkt) &&
	    !usbhsf_fifo_is_busy(fifo))
		return fifo;

	return NULL;
}

#define usbhsf_dma_start(p, f)	__usbhsf_dma_ctrl(p, f, DREQE)
#define usbhsf_dma_stop(p, f)	__usbhsf_dma_ctrl(p, f, 0)
static void __usbhsf_dma_ctrl(struct usbhs_pipe *pipe,
			      struct usbhs_fifo *fifo,
			      u16 dreqe)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);

	usbhs_bset(priv, fifo->sel, DREQE, dreqe);
}

#define usbhsf_dma_map(p)	__usbhsf_dma_map_ctrl(p, 1)
#define usbhsf_dma_unmap(p)	__usbhsf_dma_map_ctrl(p, 0)
static int __usbhsf_dma_map_ctrl(struct usbhs_pkt *pkt, int map)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_pipe_info *info = usbhs_priv_to_pipeinfo(priv);

	return info->dma_map_ctrl(pkt, map);
}

static void usbhsf_dma_complete(void *arg);
static void usbhsf_dma_prepare_tasklet(unsigned long data)
{
	struct usbhs_pkt *pkt = (struct usbhs_pkt *)data;
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_fifo *fifo = usbhs_pipe_to_fifo(pipe);
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct scatterlist sg;
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *chan = usbhsf_dma_chan_get(fifo, pkt);
	struct device *dev = usbhs_priv_to_dev(priv);
	enum dma_transfer_direction dir;
	dma_cookie_t cookie;

	dir = usbhs_pipe_is_dir_in(pipe) ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, virt_to_page(pkt->dma),
		    pkt->length, offset_in_page(pkt->dma));
	sg_dma_address(&sg) = pkt->dma + pkt->actual;
	sg_dma_len(&sg) = pkt->trans;

	desc = chan->device->device_prep_slave_sg(chan, &sg, 1, dir,
						  DMA_PREP_INTERRUPT |
						  DMA_CTRL_ACK);
	if (!desc)
		return;

	desc->callback		= usbhsf_dma_complete;
	desc->callback_param	= pipe;

	cookie = desc->tx_submit(desc);
	if (cookie < 0) {
		dev_err(dev, "Failed to submit dma descriptor\n");
		return;
	}

	dev_dbg(dev, "  %s %d (%d/ %d)\n",
		fifo->name, usbhs_pipe_number(pipe), pkt->length, pkt->zero);

	usbhsf_dma_start(pipe, fifo);
	dma_async_issue_pending(chan);
}

/*
 *		DMA push handler
 */
static int usbhsf_dma_prepare_push(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_fifo *fifo;
	int len = pkt->length - pkt->actual;
	int ret;

	if (usbhs_pipe_is_busy(pipe))
		return 0;

	/* use PIO if packet is less than pio_dma_border or pipe is DCP */
	if ((len < usbhs_get_dparam(priv, pio_dma_border)) ||
	    usbhs_pipe_is_dcp(pipe))
		goto usbhsf_pio_prepare_push;

	if (len % 4) /* 32bit alignment */
		goto usbhsf_pio_prepare_push;

	if ((uintptr_t)(pkt->buf + pkt->actual) & 0x7) /* 8byte alignment */
		goto usbhsf_pio_prepare_push;

	/* get enable DMA fifo */
	fifo = usbhsf_get_dma_fifo(priv, pkt);
	if (!fifo)
		goto usbhsf_pio_prepare_push;

	if (usbhsf_dma_map(pkt) < 0)
		goto usbhsf_pio_prepare_push;

	ret = usbhsf_fifo_select(pipe, fifo, 0);
	if (ret < 0)
		goto usbhsf_pio_prepare_push_unmap;

	pkt->trans = len;

	tasklet_init(&fifo->tasklet,
		     usbhsf_dma_prepare_tasklet,
		     (unsigned long)pkt);

	tasklet_schedule(&fifo->tasklet);

	return 0;

usbhsf_pio_prepare_push_unmap:
	usbhsf_dma_unmap(pkt);
usbhsf_pio_prepare_push:
	/*
	 * change handler to PIO
	 */
	pkt->handler = &usbhs_fifo_pio_push_handler;

	return pkt->handler->prepare(pkt, is_done);
}

static int usbhsf_dma_push_done(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;

	pkt->actual = pkt->trans;

	*is_done = !pkt->zero;	/* send zero packet ? */

	usbhsf_dma_stop(pipe, pipe->fifo);
	usbhsf_dma_unmap(pkt);
	usbhsf_fifo_unselect(pipe, pipe->fifo);

	return 0;
}

struct usbhs_pkt_handle usbhs_fifo_dma_push_handler = {
	.prepare	= usbhsf_dma_prepare_push,
	.dma_done	= usbhsf_dma_push_done,
};

/*
 *		DMA pop handler
 */
static int usbhsf_dma_try_pop(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_fifo *fifo;
	int len, ret;

	if (usbhs_pipe_is_busy(pipe))
		return 0;

	if (usbhs_pipe_is_dcp(pipe))
		goto usbhsf_pio_prepare_pop;

	/* get enable DMA fifo */
	fifo = usbhsf_get_dma_fifo(priv, pkt);
	if (!fifo)
		goto usbhsf_pio_prepare_pop;

	if ((uintptr_t)(pkt->buf + pkt->actual) & 0x7) /* 8byte alignment */
		goto usbhsf_pio_prepare_pop;

	ret = usbhsf_fifo_select(pipe, fifo, 0);
	if (ret < 0)
		goto usbhsf_pio_prepare_pop;

	/* use PIO if packet is less than pio_dma_border */
	len = usbhsf_fifo_rcv_len(priv, fifo);
	len = min(pkt->length - pkt->actual, len);
	if (len % 4) /* 32bit alignment */
		goto usbhsf_pio_prepare_pop_unselect;

	if (len < usbhs_get_dparam(priv, pio_dma_border))
		goto usbhsf_pio_prepare_pop_unselect;

	ret = usbhsf_fifo_barrier(priv, fifo);
	if (ret < 0)
		goto usbhsf_pio_prepare_pop_unselect;

	if (usbhsf_dma_map(pkt) < 0)
		goto usbhsf_pio_prepare_pop_unselect;

	/* DMA */

	/*
	 * usbhs_fifo_dma_pop_handler :: prepare
	 * enabled irq to come here.
	 * but it is no longer needed for DMA. disable it.
	 */
	usbhsf_rx_irq_ctrl(pipe, 0);

	pkt->trans = len;

	tasklet_init(&fifo->tasklet,
		     usbhsf_dma_prepare_tasklet,
		     (unsigned long)pkt);

	tasklet_schedule(&fifo->tasklet);

	return 0;

usbhsf_pio_prepare_pop_unselect:
	usbhsf_fifo_unselect(pipe, fifo);
usbhsf_pio_prepare_pop:

	/*
	 * change handler to PIO
	 */
	pkt->handler = &usbhs_fifo_pio_pop_handler;

	return pkt->handler->try_run(pkt, is_done);
}

static int usbhsf_dma_pop_done(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	int maxp = usbhs_pipe_get_maxpacket(pipe);

	usbhsf_dma_stop(pipe, pipe->fifo);
	usbhsf_dma_unmap(pkt);
	usbhsf_fifo_unselect(pipe, pipe->fifo);

	pkt->actual += pkt->trans;

	if ((pkt->actual == pkt->length) ||	/* receive all data */
	    (pkt->trans < maxp)) {		/* short packet */
		*is_done = 1;
	} else {
		/* re-enable */
		usbhsf_prepare_pop(pkt, is_done);
	}

	return 0;
}

struct usbhs_pkt_handle usbhs_fifo_dma_pop_handler = {
	.prepare	= usbhsf_prepare_pop,
	.try_run	= usbhsf_dma_try_pop,
	.dma_done	= usbhsf_dma_pop_done
};

/*
 *		DMA setting
 */
static bool usbhsf_dma_filter(struct dma_chan *chan, void *param)
{
	struct sh_dmae_slave *slave = param;

	/*
	 * FIXME
	 *
	 * usbhs doesn't recognize id = 0 as valid DMA
	 */
	if (0 == slave->slave_id)
		return false;

	chan->private = slave;

	return true;
}

static void usbhsf_dma_quit(struct usbhs_priv *priv, struct usbhs_fifo *fifo)
{
	if (fifo->tx_chan)
		dma_release_channel(fifo->tx_chan);
	if (fifo->rx_chan)
		dma_release_channel(fifo->rx_chan);

	fifo->tx_chan = NULL;
	fifo->rx_chan = NULL;
}

static void usbhsf_dma_init(struct usbhs_priv *priv,
			    struct usbhs_fifo *fifo)
{
	struct device *dev = usbhs_priv_to_dev(priv);
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	fifo->tx_chan = dma_request_channel(mask, usbhsf_dma_filter,
					    &fifo->tx_slave);

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	fifo->rx_chan = dma_request_channel(mask, usbhsf_dma_filter,
					    &fifo->rx_slave);

	if (fifo->tx_chan || fifo->rx_chan)
		dev_dbg(dev, "enable DMAEngine (%s%s%s)\n",
			 fifo->name,
			 fifo->tx_chan ? "[TX]" : "    ",
			 fifo->rx_chan ? "[RX]" : "    ");
}

/*
 *		irq functions
 */
static int usbhsf_irq_empty(struct usbhs_priv *priv,
			    struct usbhs_irq_state *irq_state)
{
	struct usbhs_pipe *pipe;
	struct device *dev = usbhs_priv_to_dev(priv);
	int i, ret;

	if (!irq_state->bempsts) {
		dev_err(dev, "debug %s !!\n", __func__);
		return -EIO;
	}

	dev_dbg(dev, "irq empty [0x%04x]\n", irq_state->bempsts);

	/*
	 * search interrupted "pipe"
	 * not "uep".
	 */
	usbhs_for_each_pipe_with_dcp(pipe, priv, i) {
		if (!(irq_state->bempsts & (1 << i)))
			continue;

		ret = usbhsf_pkt_handler(pipe, USBHSF_PKT_TRY_RUN);
		if (ret < 0)
			dev_err(dev, "irq_empty run_error %d : %d\n", i, ret);
	}

	return 0;
}

static int usbhsf_irq_ready(struct usbhs_priv *priv,
			    struct usbhs_irq_state *irq_state)
{
	struct usbhs_pipe *pipe;
	struct device *dev = usbhs_priv_to_dev(priv);
	int i, ret;

	if (!irq_state->brdysts) {
		dev_err(dev, "debug %s !!\n", __func__);
		return -EIO;
	}

	dev_dbg(dev, "irq ready [0x%04x]\n", irq_state->brdysts);

	/*
	 * search interrupted "pipe"
	 * not "uep".
	 */
	usbhs_for_each_pipe_with_dcp(pipe, priv, i) {
		if (!(irq_state->brdysts & (1 << i)))
			continue;

		ret = usbhsf_pkt_handler(pipe, USBHSF_PKT_TRY_RUN);
		if (ret < 0)
			dev_err(dev, "irq_ready run_error %d : %d\n", i, ret);
	}

	return 0;
}

static void usbhsf_dma_complete(void *arg)
{
	struct usbhs_pipe *pipe = arg;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct device *dev = usbhs_priv_to_dev(priv);
	int ret;

	ret = usbhsf_pkt_handler(pipe, USBHSF_PKT_DMA_DONE);
	if (ret < 0)
		dev_err(dev, "dma_complete run_error %d : %d\n",
			usbhs_pipe_number(pipe), ret);
}

/*
 *		fifo init
 */
void usbhs_fifo_init(struct usbhs_priv *priv)
{
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	struct usbhs_fifo *cfifo = usbhsf_get_cfifo(priv);
	struct usbhs_fifo *d0fifo = usbhsf_get_d0fifo(priv);
	struct usbhs_fifo *d1fifo = usbhsf_get_d1fifo(priv);

	mod->irq_empty		= usbhsf_irq_empty;
	mod->irq_ready		= usbhsf_irq_ready;
	mod->irq_bempsts	= 0;
	mod->irq_brdysts	= 0;

	cfifo->pipe	= NULL;
	cfifo->tx_chan	= NULL;
	cfifo->rx_chan	= NULL;

	d0fifo->pipe	= NULL;
	d0fifo->tx_chan	= NULL;
	d0fifo->rx_chan	= NULL;

	d1fifo->pipe	= NULL;
	d1fifo->tx_chan	= NULL;
	d1fifo->rx_chan	= NULL;

	usbhsf_dma_init(priv, usbhsf_get_d0fifo(priv));
	usbhsf_dma_init(priv, usbhsf_get_d1fifo(priv));
}

void usbhs_fifo_quit(struct usbhs_priv *priv)
{
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);

	mod->irq_empty		= NULL;
	mod->irq_ready		= NULL;
	mod->irq_bempsts	= 0;
	mod->irq_brdysts	= 0;

	usbhsf_dma_quit(priv, usbhsf_get_d0fifo(priv));
	usbhsf_dma_quit(priv, usbhsf_get_d1fifo(priv));
}

int usbhs_fifo_probe(struct usbhs_priv *priv)
{
	struct usbhs_fifo *fifo;

	/* CFIFO */
	fifo = usbhsf_get_cfifo(priv);
	fifo->name	= "CFIFO";
	fifo->port	= CFIFO;
	fifo->sel	= CFIFOSEL;
	fifo->ctr	= CFIFOCTR;

	/* D0FIFO */
	fifo = usbhsf_get_d0fifo(priv);
	fifo->name	= "D0FIFO";
	fifo->port	= D0FIFO;
	fifo->sel	= D0FIFOSEL;
	fifo->ctr	= D0FIFOCTR;
	fifo->tx_slave.slave_id	= usbhs_get_dparam(priv, d0_tx_id);
	fifo->rx_slave.slave_id	= usbhs_get_dparam(priv, d0_rx_id);

	/* D1FIFO */
	fifo = usbhsf_get_d1fifo(priv);
	fifo->name	= "D1FIFO";
	fifo->port	= D1FIFO;
	fifo->sel	= D1FIFOSEL;
	fifo->ctr	= D1FIFOCTR;
	fifo->tx_slave.slave_id	= usbhs_get_dparam(priv, d1_tx_id);
	fifo->rx_slave.slave_id	= usbhs_get_dparam(priv, d1_rx_id);

	return 0;
}

void usbhs_fifo_remove(struct usbhs_priv *priv)
{
}
