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

int usbhs_fifo_write(struct usbhs_pipe *pipe, u8 *buf, int len)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	void __iomem *addr = priv->base + CFIFO;
	int maxp = usbhs_pipe_get_maxpacket(pipe);
	int total_len;
	int i, ret;

	ret = usbhs_pipe_is_accessible(pipe);
	if (ret < 0)
		return ret;

	ret = usbhsf_fifo_select(pipe, 1);
	if (ret < 0)
		return ret;

	ret = usbhsf_fifo_barrier(priv);
	if (ret < 0)
		return ret;

	len = min(len, maxp);
	total_len = len;

	/*
	 * FIXME
	 *
	 * 32-bit access only
	 */
	if (len >= 4 &&
	    !((unsigned long)buf & 0x03)) {
		iowrite32_rep(addr, buf, len / 4);
		len %= 4;
		buf += total_len - len;
	}

	/* the rest operation */
	for (i = 0; i < len; i++)
		iowrite8(buf[i], addr + (0x03 - (i & 0x03)));

	if (total_len < maxp)
		usbhsf_send_terminator(pipe);

	return total_len;
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

	return ret;
}

int usbhs_fifo_read(struct usbhs_pipe *pipe, u8 *buf, int len)
{
	struct usbhs_priv *priv = usbhs_pipe_to_priv(pipe);
	void __iomem *addr = priv->base + CFIFO;
	int rcv_len;
	int i, ret;
	int total_len;
	u32 data = 0;

	ret = usbhsf_fifo_select(pipe, 0);
	if (ret < 0)
		return ret;

	ret = usbhsf_fifo_barrier(priv);
	if (ret < 0)
		return ret;

	rcv_len = usbhsf_fifo_rcv_len(priv);

	/*
	 * Buffer clear if Zero-Length packet
	 *
	 * see
	 * "Operation" - "FIFO Buffer Memory" - "FIFO Port Function"
	 */
	if (0 == rcv_len) {
		usbhsf_fifo_clear(pipe);
		return 0;
	}

	len = min(rcv_len, len);
	total_len = len;

	/*
	 * FIXME
	 *
	 * 32-bit access only
	 */
	if (len >= 4 &&
	    !((unsigned long)buf & 0x03)) {
		ioread32_rep(addr, buf, len / 4);
		len %= 4;
		buf += rcv_len - len;
	}

	/* the rest operation */
	for (i = 0; i < len; i++) {
		if (!(i & 0x03))
			data = ioread32(addr);

		buf[i] = (data >> ((i & 0x03) * 8)) & 0xff;
	}

	return total_len;
}
