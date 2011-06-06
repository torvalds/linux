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
#include "./common.h"
#include "./pipe.h"

#define usbhsf_get_cfifo(p)	(&((p)->fifo_info.cfifo))

#define usbhsf_fifo_is_busy(f)	((f)->pipe) /* see usbhs_pipe_select_fifo */

/*
 *		packet info function
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

void usbhs_pkt_init(struct usbhs_pkt *pkt)
{
	INIT_LIST_HEAD(&pkt->node);
}

void usbhs_pkt_push(struct usbhs_pipe *pipe, struct usbhs_pkt *pkt,
		    struct usbhs_pkt_handle *handler,
		    void *buf, int len, int zero)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct device *dev = usbhs_priv_to_dev(priv);
	unsigned long flags;

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	if (!handler) {
		dev_err(dev, "no handler function\n");
		handler = &usbhsf_null_handler;
	}

	list_del_init(&pkt->node);
	list_add_tail(&pkt->node, &pipe->list);

	pkt->pipe	= pipe;
	pkt->buf	= buf;
	pkt->handler	= handler;
	pkt->length	= len;
	pkt->zero	= zero;
	pkt->actual	= 0;

	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/

	usbhs_pkt_start(pipe);
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

int __usbhs_pkt_handler(struct usbhs_pipe *pipe, int type)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_pipe_info *info = usbhs_priv_to_pipeinfo(priv);
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
		info->done(pkt);
		usbhs_pkt_start(pipe);
	}

	return ret;
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

	if (usbhs_pipe_is_dcp(pipe))
		base |= (1 == write) << 5;	/* ISEL */

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
 *		PIO fifo functions
 */
static int usbhsf_try_push(struct usbhs_pkt *pkt, int *is_done)
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

	ret = usbhsf_fifo_select(pipe, fifo, 1);
	if (ret < 0)
		return 0;

	ret = usbhs_pipe_is_accessible(pipe);
	if (ret < 0)
		goto usbhs_fifo_write_busy;

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

struct usbhs_pkt_handle usbhs_fifo_push_handler = {
	.prepare = usbhsf_try_push,
	.try_run = usbhsf_try_push,
};

static int usbhsf_prepare_pop(struct usbhs_pkt *pkt, int *is_done)
{
	struct usbhs_pipe *pipe = pkt->pipe;

	if (usbhs_pipe_is_busy(pipe))
		return 0;

	/*
	 * pipe enable to prepare packet receive
	 */

	usbhs_pipe_enable(pipe);
	usbhsf_rx_irq_ctrl(pipe, 1);

	return 0;
}

static int usbhsf_try_pop(struct usbhs_pkt *pkt, int *is_done)
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
	 * Buffer clear if Zero-Length packet
	 *
	 * see
	 * "Operation" - "FIFO Buffer Memory" - "FIFO Port Function"
	 */
	if (0 == rcv_len) {
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

	pkt->actual += total_len;

usbhs_fifo_read_end:
	if ((pkt->actual == pkt->length) ||	/* receive all data */
	    (total_len < maxp)) {		/* short packet */
		*is_done = 1;
		usbhsf_rx_irq_ctrl(pipe, 0);
		usbhs_pipe_disable(pipe);
	}

	dev_dbg(dev, "  recv %d (%d/ %d/ %d/ %d)\n",
		usbhs_pipe_number(pipe),
		pkt->length, pkt->actual, *is_done, pkt->zero);

usbhs_fifo_read_busy:
	usbhsf_fifo_unselect(pipe, fifo);

	return ret;
}

struct usbhs_pkt_handle usbhs_fifo_pop_handler = {
	.prepare = usbhsf_prepare_pop,
	.try_run = usbhsf_try_pop,
};

/*
 *		handler function
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

		ret = usbhs_pkt_run(pipe);
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

		ret = usbhs_pkt_run(pipe);
		if (ret < 0)
			dev_err(dev, "irq_ready run_error %d : %d\n", i, ret);
	}

	return 0;
}

/*
 *		fifo init
 */
void usbhs_fifo_init(struct usbhs_priv *priv)
{
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	struct usbhs_fifo *cfifo = usbhsf_get_cfifo(priv);

	mod->irq_empty		= usbhsf_irq_empty;
	mod->irq_ready		= usbhsf_irq_ready;
	mod->irq_bempsts	= 0;
	mod->irq_brdysts	= 0;

	cfifo->pipe	= NULL;
}

void usbhs_fifo_quit(struct usbhs_priv *priv)
{
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);

	mod->irq_empty		= NULL;
	mod->irq_ready		= NULL;
	mod->irq_bempsts	= 0;
	mod->irq_brdysts	= 0;
}

int usbhs_fifo_probe(struct usbhs_priv *priv)
{
	struct usbhs_fifo *fifo;

	/* CFIFO */
	fifo = usbhsf_get_cfifo(priv);
	fifo->port	= CFIFO;
	fifo->sel	= CFIFOSEL;
	fifo->ctr	= CFIFOCTR;

	return 0;
}

void usbhs_fifo_remove(struct usbhs_priv *priv)
{
}
