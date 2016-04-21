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
#include "common.h"
#include "pipe.h"

#define usbhsf_get_cfifo(p)	(&((p)->fifo_info.cfifo))
#define usbhsf_is_cfifo(p, f)	(usbhsf_get_cfifo(p) == f)

#define usbhsf_fifo_is_busy(f)	((f)->pipe) /* see usbhs_pipe_select_fifo */

/*
 *		packet initialize
 */
void usbhs_pkt_init(struct usbhs_pkt *pkt)
{
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

	list_move_tail(&pkt->node, &pipe->list);

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

	return list_first_entry(&pipe->list, struct usbhs_pkt, node);
}

static void usbhsf_fifo_clear(struct usbhs_pipe *pipe,
			      struct usbhs_fifo *fifo);
static void usbhsf_fifo_unselect(struct usbhs_pipe *pipe,
				 struct usbhs_fifo *fifo);
static struct dma_chan *usbhsf_dma_chan_get(struct usbhs_fifo *fifo,
					    struct usbhs_pkt *pkt);
#define usbhsf_dma_map(p)	__usbhsf_dma_map_ctrl(p, 1)
#define usbhsf_dma_unmap(p)	__usbhsf_dma_map_ctrl(p, 0)
static int __usbhsf_dma_map_ctrl(struct usbhs_pkt *pkt, int map);
struct usbhs_pkt *usbhs_pkt_pop(struct usbhs_pipe *pipe, struct usbhs_pkt *pkt)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_fifo *fifo = usbhs_pipe_to_fifo(pipe);
	unsigned long flags;

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	usbhs_pipe_disable(pipe);

	if (!pkt)
		pkt = __usbhsf_pkt_get(pipe);

	if (pkt) {
		struct dma_chan *chan = NULL;

		if (fifo)
			chan = usbhsf_dma_chan_get(fifo, pkt);
		if (chan) {
			dmaengine_terminate_all(chan);
			usbhsf_fifo_clear(pipe, fifo);
			usbhsf_dma_unmap(pkt);
		}

		__usbhsf_pkt_del(pkt);
	}

	if (fifo)
		usbhsf_fifo_unselect(pipe, fifo);

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
		dev_err(dev, "unknown pkt handler\n");
		goto __usbhs_pkt_handler_end;
	}

	if (likely(func))
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
#define usbhsf_irq_empty_ctrl(p, e) usbhsf_irq_callback_ctrl(p, irq_bempsts, e)
#define usbhsf_irq_ready_ctrl(p, e) usbhsf_irq_callback_ctrl(p, irq_brdysts, e)
#define usbhsf_irq_callback_ctrl(pipe, status, enable)			\
	({								\
		struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);	\
		struct usbhs_mod *mod = usbhs_mod_get_current(priv);	\
		u16 status = (1 << usbhs_pipe_number(pipe));		\
		if (!mod)						\
			return;						\
		if (enable)						\
			mod->status |= status;				\
		else							\
			mod->status &= ~status;				\
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
	if (usbhs_get_dparam(priv, has_sudmac) && !usbhsf_is_cfifo(priv, fifo))
		usbhs_write(priv, fifo->sel, base);
	else
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

	usbhs_pipe_set_trans_count_if_bulk(pipe, pkt->length);

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
	usbhs_pipe_running(pipe, !*is_done);
	usbhs_pipe_enable(pipe);

	dev_dbg(dev, "  send %d (%d/ %d/ %d/ %d)\n",
		usbhs_pipe_number(pipe),
		pkt->length, pkt->actual, *is_done, pkt->zero);

	usbhsf_fifo_unselect(pipe, fifo);

	return 0;

usbhs_fifo_write_busy:
	usbhsf_fifo_unselect(pipe, fifo);

	/*
	 * pipe is busy.
	 * retry in interrupt
	 */
	usbhsf_tx_irq_ctrl(pipe, 1);
	usbhs_pipe_running(pipe, 1);

	return ret;
}

static int usbhsf_pio_prepare_push(struct usbhs_pkt *pkt, int *is_done)
{
	if (usbhs_pipe_is_running(pkt->pipe))
		return 0;

	return usbhsf_pio_try_push(pkt, is_done);
}

struct usbhs_pkt_handle usbhs_fifo_pio_push_handler = {
	.prepare = usbhsf_pio_prepare_push,
	.try_run = usbhsf_pio_try_push,
};

/*
 *		PIO pop handler
 */
static int usbhsf_prepare_pop(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_fifo *fifo = usbhsf_get_cfifo(priv);

	if (usbhs_pipe_is_busy(pipe))
		return 0;

	if (usbhs_pipe_is_running(pipe))
		return 0;

	/*
	 * pipe enable to prepare packet receive
	 */
	usbhs_pipe_data_sequence(pipe, pkt->sequence);
	pkt->sequence = -1; /* -1 sequence will be ignored */

	if (usbhs_pipe_is_dcp(pipe))
		usbhsf_fifo_clear(pipe, fifo);

	usbhs_pipe_set_trans_count_if_bulk(pipe, pkt->length);
	usbhs_pipe_enable(pipe);
	usbhs_pipe_running(pipe, 1);
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
		usbhs_pipe_running(pipe, 0);
		/*
		 * If function mode, since this controller is possible to enter
		 * Control Write status stage at this timing, this driver
		 * should not disable the pipe. If such a case happens, this
		 * controller is not able to complete the status stage.
		 */
		if (!usbhs_mod_is_host(priv) && !usbhs_pipe_is_dcp(pipe))
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
	int i;

	usbhs_for_each_dfifo(priv, fifo, i) {
		if (usbhsf_dma_chan_get(fifo, pkt) &&
		    !usbhsf_fifo_is_busy(fifo))
			return fifo;
	}

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

static int __usbhsf_dma_map_ctrl(struct usbhs_pkt *pkt, int map)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_pipe_info *info = usbhs_priv_to_pipeinfo(priv);

	return info->dma_map_ctrl(pkt, map);
}

static void usbhsf_dma_complete(void *arg);
static void xfer_work(struct work_struct *work)
{
	struct usbhs_pkt *pkt = container_of(work, struct usbhs_pkt, work);
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_fifo *fifo = usbhs_pipe_to_fifo(pipe);
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *chan = usbhsf_dma_chan_get(fifo, pkt);
	struct device *dev = usbhs_priv_to_dev(priv);
	enum dma_transfer_direction dir;

	dir = usbhs_pipe_is_dir_in(pipe) ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV;

	desc = dmaengine_prep_slave_single(chan, pkt->dma + pkt->actual,
					pkt->trans, dir,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		return;

	desc->callback		= usbhsf_dma_complete;
	desc->callback_param	= pipe;

	pkt->cookie = dmaengine_submit(desc);
	if (pkt->cookie < 0) {
		dev_err(dev, "Failed to submit dma descriptor\n");
		return;
	}

	dev_dbg(dev, "  %s %d (%d/ %d)\n",
		fifo->name, usbhs_pipe_number(pipe), pkt->length, pkt->zero);

	usbhs_pipe_running(pipe, 1);
	usbhsf_dma_start(pipe, fifo);
	usbhs_pipe_set_trans_count_if_bulk(pipe, pkt->trans);
	dma_async_issue_pending(chan);
	usbhs_pipe_enable(pipe);
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
	uintptr_t align_mask;

	if (usbhs_pipe_is_busy(pipe))
		return 0;

	/* use PIO if packet is less than pio_dma_border or pipe is DCP */
	if ((len < usbhs_get_dparam(priv, pio_dma_border)) ||
	    usbhs_pipe_is_dcp(pipe))
		goto usbhsf_pio_prepare_push;

	/* check data length if this driver don't use USB-DMAC */
	if (!usbhs_get_dparam(priv, has_usb_dmac) && len & 0x7)
		goto usbhsf_pio_prepare_push;

	/* check buffer alignment */
	align_mask = usbhs_get_dparam(priv, has_usb_dmac) ?
					USBHS_USB_DMAC_XFER_SIZE - 1 : 0x7;
	if ((uintptr_t)(pkt->buf + pkt->actual) & align_mask)
		goto usbhsf_pio_prepare_push;

	/* return at this time if the pipe is running */
	if (usbhs_pipe_is_running(pipe))
		return 0;

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

	usbhsf_tx_irq_ctrl(pipe, 0);
	INIT_WORK(&pkt->work, xfer_work);
	schedule_work(&pkt->work);

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
	int is_short = pkt->trans % usbhs_pipe_get_maxpacket(pipe);

	pkt->actual += pkt->trans;

	if (pkt->actual < pkt->length)
		*is_done = 0;		/* there are remainder data */
	else if (is_short)
		*is_done = 1;		/* short packet */
	else
		*is_done = !pkt->zero;	/* send zero packet? */

	usbhs_pipe_running(pipe, !*is_done);

	usbhsf_dma_stop(pipe, pipe->fifo);
	usbhsf_dma_unmap(pkt);
	usbhsf_fifo_unselect(pipe, pipe->fifo);

	if (!*is_done) {
		/* change handler to PIO */
		pkt->handler = &usbhs_fifo_pio_push_handler;
		return pkt->handler->try_run(pkt, is_done);
	}

	return 0;
}

struct usbhs_pkt_handle usbhs_fifo_dma_push_handler = {
	.prepare	= usbhsf_dma_prepare_push,
	.dma_done	= usbhsf_dma_push_done,
};

/*
 *		DMA pop handler
 */

static int usbhsf_dma_prepare_pop_with_rx_irq(struct usbhs_pkt *pkt,
					      int *is_done)
{
	return usbhsf_prepare_pop(pkt, is_done);
}

static int usbhsf_dma_prepare_pop_with_usb_dmac(struct usbhs_pkt *pkt,
						int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_fifo *fifo;
	int ret;

	if (usbhs_pipe_is_busy(pipe))
		return 0;

	/* use PIO if packet is less than pio_dma_border or pipe is DCP */
	if ((pkt->length < usbhs_get_dparam(priv, pio_dma_border)) ||
	    usbhs_pipe_is_dcp(pipe))
		goto usbhsf_pio_prepare_pop;

	fifo = usbhsf_get_dma_fifo(priv, pkt);
	if (!fifo)
		goto usbhsf_pio_prepare_pop;

	if ((uintptr_t)pkt->buf & (USBHS_USB_DMAC_XFER_SIZE - 1))
		goto usbhsf_pio_prepare_pop;

	usbhs_pipe_config_change_bfre(pipe, 1);

	ret = usbhsf_fifo_select(pipe, fifo, 0);
	if (ret < 0)
		goto usbhsf_pio_prepare_pop;

	if (usbhsf_dma_map(pkt) < 0)
		goto usbhsf_pio_prepare_pop_unselect;

	/* DMA */

	/*
	 * usbhs_fifo_dma_pop_handler :: prepare
	 * enabled irq to come here.
	 * but it is no longer needed for DMA. disable it.
	 */
	usbhsf_rx_irq_ctrl(pipe, 0);

	pkt->trans = pkt->length;

	INIT_WORK(&pkt->work, xfer_work);
	schedule_work(&pkt->work);

	return 0;

usbhsf_pio_prepare_pop_unselect:
	usbhsf_fifo_unselect(pipe, fifo);
usbhsf_pio_prepare_pop:

	/*
	 * change handler to PIO
	 */
	pkt->handler = &usbhs_fifo_pio_pop_handler;
	usbhs_pipe_config_change_bfre(pipe, 0);

	return pkt->handler->prepare(pkt, is_done);
}

static int usbhsf_dma_prepare_pop(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pkt->pipe);

	if (usbhs_get_dparam(priv, has_usb_dmac))
		return usbhsf_dma_prepare_pop_with_usb_dmac(pkt, is_done);
	else
		return usbhsf_dma_prepare_pop_with_rx_irq(pkt, is_done);
}

static int usbhsf_dma_try_pop_with_rx_irq(struct usbhs_pkt *pkt, int *is_done)
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
	if (len & 0x7) /* 8byte alignment */
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

	INIT_WORK(&pkt->work, xfer_work);
	schedule_work(&pkt->work);

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

static int usbhsf_dma_try_pop(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pkt->pipe);

	BUG_ON(usbhs_get_dparam(priv, has_usb_dmac));

	return usbhsf_dma_try_pop_with_rx_irq(pkt, is_done);
}

static int usbhsf_dma_pop_done_with_rx_irq(struct usbhs_pkt *pkt, int *is_done)
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
		usbhs_pipe_running(pipe, 0);
	} else {
		/* re-enable */
		usbhs_pipe_running(pipe, 0);
		usbhsf_prepare_pop(pkt, is_done);
	}

	return 0;
}

static size_t usbhs_dma_calc_received_size(struct usbhs_pkt *pkt,
					   struct dma_chan *chan, int dtln)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct dma_tx_state state;
	size_t received_size;
	int maxp = usbhs_pipe_get_maxpacket(pipe);

	dmaengine_tx_status(chan, pkt->cookie, &state);
	received_size = pkt->length - state.residue;

	if (dtln) {
		received_size -= USBHS_USB_DMAC_XFER_SIZE;
		received_size &= ~(maxp - 1);
		received_size += dtln;
	}

	return received_size;
}

static int usbhsf_dma_pop_done_with_usb_dmac(struct usbhs_pkt *pkt,
					     int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_fifo *fifo = usbhs_pipe_to_fifo(pipe);
	struct dma_chan *chan = usbhsf_dma_chan_get(fifo, pkt);
	int rcv_len;

	/*
	 * Since the driver disables rx_irq in DMA mode, the interrupt handler
	 * cannot the BRDYSTS. So, the function clears it here because the
	 * driver may use PIO mode next time.
	 */
	usbhs_xxxsts_clear(priv, BRDYSTS, usbhs_pipe_number(pipe));

	rcv_len = usbhsf_fifo_rcv_len(priv, fifo);
	usbhsf_fifo_clear(pipe, fifo);
	pkt->actual = usbhs_dma_calc_received_size(pkt, chan, rcv_len);

	usbhsf_dma_stop(pipe, fifo);
	usbhsf_dma_unmap(pkt);
	usbhsf_fifo_unselect(pipe, pipe->fifo);

	/* The driver can assume the rx transaction is always "done" */
	*is_done = 1;

	return 0;
}

static int usbhsf_dma_pop_done(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pkt->pipe);

	if (usbhs_get_dparam(priv, has_usb_dmac))
		return usbhsf_dma_pop_done_with_usb_dmac(pkt, is_done);
	else
		return usbhsf_dma_pop_done_with_rx_irq(pkt, is_done);
}

struct usbhs_pkt_handle usbhs_fifo_dma_pop_handler = {
	.prepare	= usbhsf_dma_prepare_pop,
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
	if (0 == slave->shdma_slave.slave_id)
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

static void usbhsf_dma_init_pdev(struct usbhs_fifo *fifo)
{
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	fifo->tx_chan = dma_request_channel(mask, usbhsf_dma_filter,
					    &fifo->tx_slave);

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	fifo->rx_chan = dma_request_channel(mask, usbhsf_dma_filter,
					    &fifo->rx_slave);
}

static void usbhsf_dma_init_dt(struct device *dev, struct usbhs_fifo *fifo,
			       int channel)
{
	char name[16];

	/*
	 * To avoid complex handing for DnFIFOs, the driver uses each
	 * DnFIFO as TX or RX direction (not bi-direction).
	 * So, the driver uses odd channels for TX, even channels for RX.
	 */
	snprintf(name, sizeof(name), "ch%d", channel);
	if (channel & 1) {
		fifo->tx_chan = dma_request_slave_channel_reason(dev, name);
		if (IS_ERR(fifo->tx_chan))
			fifo->tx_chan = NULL;
	} else {
		fifo->rx_chan = dma_request_slave_channel_reason(dev, name);
		if (IS_ERR(fifo->rx_chan))
			fifo->rx_chan = NULL;
	}
}

static void usbhsf_dma_init(struct usbhs_priv *priv, struct usbhs_fifo *fifo,
			    int channel)
{
	struct device *dev = usbhs_priv_to_dev(priv);

	if (dev->of_node)
		usbhsf_dma_init_dt(dev, fifo, channel);
	else
		usbhsf_dma_init_pdev(fifo);

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

void usbhs_fifo_clear_dcp(struct usbhs_pipe *pipe)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_fifo *fifo = usbhsf_get_cfifo(priv); /* CFIFO */

	/* clear DCP FIFO of transmission */
	if (usbhsf_fifo_select(pipe, fifo, 1) < 0)
		return;
	usbhsf_fifo_clear(pipe, fifo);
	usbhsf_fifo_unselect(pipe, fifo);

	/* clear DCP FIFO of reception */
	if (usbhsf_fifo_select(pipe, fifo, 0) < 0)
		return;
	usbhsf_fifo_clear(pipe, fifo);
	usbhsf_fifo_unselect(pipe, fifo);
}

/*
 *		fifo init
 */
void usbhs_fifo_init(struct usbhs_priv *priv)
{
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	struct usbhs_fifo *cfifo = usbhsf_get_cfifo(priv);
	struct usbhs_fifo *dfifo;
	int i;

	mod->irq_empty		= usbhsf_irq_empty;
	mod->irq_ready		= usbhsf_irq_ready;
	mod->irq_bempsts	= 0;
	mod->irq_brdysts	= 0;

	cfifo->pipe	= NULL;
	usbhs_for_each_dfifo(priv, dfifo, i)
		dfifo->pipe	= NULL;
}

void usbhs_fifo_quit(struct usbhs_priv *priv)
{
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);

	mod->irq_empty		= NULL;
	mod->irq_ready		= NULL;
	mod->irq_bempsts	= 0;
	mod->irq_brdysts	= 0;
}

#define __USBHS_DFIFO_INIT(priv, fifo, channel, fifo_port)		\
do {									\
	fifo = usbhsf_get_dnfifo(priv, channel);			\
	fifo->name	= "D"#channel"FIFO";				\
	fifo->port	= fifo_port;					\
	fifo->sel	= D##channel##FIFOSEL;				\
	fifo->ctr	= D##channel##FIFOCTR;				\
	fifo->tx_slave.shdma_slave.slave_id =				\
			usbhs_get_dparam(priv, d##channel##_tx_id);	\
	fifo->rx_slave.shdma_slave.slave_id =				\
			usbhs_get_dparam(priv, d##channel##_rx_id);	\
	usbhsf_dma_init(priv, fifo, channel);				\
} while (0)

#define USBHS_DFIFO_INIT(priv, fifo, channel)				\
		__USBHS_DFIFO_INIT(priv, fifo, channel, D##channel##FIFO)
#define USBHS_DFIFO_INIT_NO_PORT(priv, fifo, channel)			\
		__USBHS_DFIFO_INIT(priv, fifo, channel, 0)

int usbhs_fifo_probe(struct usbhs_priv *priv)
{
	struct usbhs_fifo *fifo;

	/* CFIFO */
	fifo = usbhsf_get_cfifo(priv);
	fifo->name	= "CFIFO";
	fifo->port	= CFIFO;
	fifo->sel	= CFIFOSEL;
	fifo->ctr	= CFIFOCTR;

	/* DFIFO */
	USBHS_DFIFO_INIT(priv, fifo, 0);
	USBHS_DFIFO_INIT(priv, fifo, 1);
	USBHS_DFIFO_INIT_NO_PORT(priv, fifo, 2);
	USBHS_DFIFO_INIT_NO_PORT(priv, fifo, 3);

	return 0;
}

void usbhs_fifo_remove(struct usbhs_priv *priv)
{
	struct usbhs_fifo *fifo;
	int i;

	usbhs_for_each_dfifo(priv, fifo, i)
		usbhsf_dma_quit(priv, fifo);
}
