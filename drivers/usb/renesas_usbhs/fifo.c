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

/*
 *		packet info function
 */
void usbhs_pkt_init(struct usbhs_pkt *pkt)
{
	INIT_LIST_HEAD(&pkt->node);
}

void usbhs_pkt_push(struct usbhs_pipe *pipe, struct usbhs_pkt *pkt,
		    void *buf, int len, int zero)
{
	list_del_init(&pkt->node);
	list_add_tail(&pkt->node, &pipe->list);

	pkt->pipe	= pipe;
	pkt->buf	= buf;
	pkt->length	= len;
	pkt->zero	= zero;
	pkt->actual	= 0;
}

void usbhs_pkt_pop(struct usbhs_pkt *pkt)
{
	list_del_init(&pkt->node);
}

struct usbhs_pkt *usbhs_pkt_get(struct usbhs_pipe *pipe)
{
	if (list_empty(&pipe->list))
		return NULL;

	return list_entry(pipe->list.next, struct usbhs_pkt, node);
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
static void usbhsf_send_terminator(struct usbhs_pipe *pipe)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);

	usbhs_bset(priv, CFIFOCTR, BVAL, BVAL);
}

static int usbhsf_fifo_barrier(struct usbhs_priv *priv)
{
	int timeout = 1024;

	do {
		/* The FIFO port is accessible */
		if (usbhs_read(priv, CFIFOCTR) & FRDY)
			return 0;

		udelay(10);
	} while (timeout--);

	return -EBUSY;
}

static void usbhsf_fifo_clear(struct usbhs_pipe *pipe)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);

	if (!usbhs_pipe_is_dcp(pipe))
		usbhsf_fifo_barrier(priv);

	usbhs_write(priv, CFIFOCTR, BCLR);
}

static int usbhsf_fifo_rcv_len(struct usbhs_priv *priv)
{
	return usbhs_read(priv, CFIFOCTR) & DTLN_MASK;
}

static int usbhsf_fifo_select(struct usbhs_pipe *pipe, int write)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct device *dev = usbhs_priv_to_dev(priv);
	int timeout = 1024;
	u16 mask = ((1 << 5) | 0xF);		/* mask of ISEL | CURPIPE */
	u16 base = usbhs_pipe_number(pipe);	/* CURPIPE */

	if (usbhs_pipe_is_dcp(pipe))
		base |= (1 == write) << 5;	/* ISEL */

	/* "base" will be used below  */
	usbhs_write(priv, CFIFOSEL, base | MBW_32);

	/* check ISEL and CURPIPE value */
	while (timeout--) {
		if (base == (mask & usbhs_read(priv, CFIFOSEL)))
			return 0;
		udelay(10);
	}

	dev_err(dev, "fifo select error\n");

	return -EIO;
}

/*
 *		PIO fifo functions
 */
int usbhs_fifo_prepare_write(struct usbhs_pipe *pipe)
{
	return usbhsf_fifo_select(pipe, 1);
}

int usbhs_fifo_write(struct usbhs_pkt *pkt)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct usbhs_pipe_info *info = usbhs_priv_to_pipeinfo(priv);
	struct device *dev = usbhs_priv_to_dev(priv);
	void __iomem *addr = priv->base + CFIFO;
	u8 *buf;
	int maxp = usbhs_pipe_get_maxpacket(pipe);
	int total_len;
	int i, ret, len;
	int is_short, is_done;

	ret = usbhs_pipe_is_accessible(pipe);
	if (ret < 0)
		goto usbhs_fifo_write_busy;

	ret = usbhsf_fifo_select(pipe, 1);
	if (ret < 0)
		goto usbhs_fifo_write_busy;

	ret = usbhsf_fifo_barrier(priv);
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
		is_done = 0;		/* there are remainder data */
	else if (is_short)
		is_done = 1;		/* short packet */
	else
		is_done = !pkt->zero;	/* send zero packet ? */

	/*
	 * pipe/irq handling
	 */
	if (is_short)
		usbhsf_send_terminator(pipe);

	usbhsf_tx_irq_ctrl(pipe, !is_done);
	usbhs_pipe_enable(pipe);

	dev_dbg(dev, "  send %d (%d/ %d/ %d/ %d)\n",
		usbhs_pipe_number(pipe),
		pkt->length, pkt->actual, is_done, pkt->zero);

	/*
	 * Transmission end
	 */
	if (is_done) {
		if (usbhs_pipe_is_dcp(pipe))
			usbhs_dcp_control_transfer_done(pipe);

		if (info->tx_done)
			info->tx_done(pkt);
	}

	return 0;

usbhs_fifo_write_busy:
	/*
	 * pipe is busy.
	 * retry in interrupt
	 */
	usbhsf_tx_irq_ctrl(pipe, 1);

	return ret;
}

int usbhs_fifo_prepare_read(struct usbhs_pipe *pipe)
{
	int ret;

	/*
	 * select pipe and enable it to prepare packet receive
	 */
	ret = usbhsf_fifo_select(pipe, 0);
	if (ret < 0)
		return ret;

	usbhs_pipe_enable(pipe);
	usbhsf_rx_irq_ctrl(pipe, 1);

	return ret;
}

int usbhs_fifo_read(struct usbhs_pkt *pkt)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	struct device *dev = usbhs_priv_to_dev(priv);
	void __iomem *addr = priv->base + CFIFO;
	u8 *buf;
	u32 data = 0;
	int maxp = usbhs_pipe_get_maxpacket(pipe);
	int rcv_len, len;
	int i, ret;
	int total_len = 0;
	int is_done = 0;

	ret = usbhsf_fifo_select(pipe, 0);
	if (ret < 0)
		return ret;

	ret = usbhsf_fifo_barrier(priv);
	if (ret < 0)
		return ret;

	rcv_len = usbhsf_fifo_rcv_len(priv);

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
		usbhsf_fifo_clear(pipe);
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
	    (total_len < maxp))			/* short packet */
		is_done = 1;

	dev_dbg(dev, "  recv %d (%d/ %d/ %d/ %d)\n",
		usbhs_pipe_number(pipe),
		pkt->length, pkt->actual, is_done, pkt->zero);

	if (is_done) {
		struct usbhs_pipe_info *info = usbhs_priv_to_pipeinfo(priv);

		usbhsf_rx_irq_ctrl(pipe, 0);
		usbhs_pipe_disable(pipe);

		if (info->rx_done)
			info->rx_done(pkt);
	}

	return 0;
}
