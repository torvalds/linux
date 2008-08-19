/*
 * MUSB OTG driver debug support
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2006 by Texas Instruments
 * Copyright (C) 2006-2007 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>	/* FIXME remove procfs writes */
#include <asm/arch/hardware.h>

#include "musb_core.h"

#include "davinci.h"

#ifdef CONFIG_USB_MUSB_HDRC_HCD

static int dump_qh(struct musb_qh *qh, char *buf, unsigned max)
{
	int				count;
	int				tmp;
	struct usb_host_endpoint	*hep = qh->hep;
	struct urb			*urb;

	count = snprintf(buf, max, "    qh %p dev%d ep%d%s max%d\n",
			qh, qh->dev->devnum, qh->epnum,
			({ char *s; switch (qh->type) {
			case USB_ENDPOINT_XFER_BULK:
				s = "-bulk"; break;
			case USB_ENDPOINT_XFER_INT:
				s = "-int"; break;
			case USB_ENDPOINT_XFER_CONTROL:
				s = ""; break;
			default:
				s = "iso"; break;
			}; s; }),
			qh->maxpacket);
	if (count <= 0)
		return 0;
	buf += count;
	max -= count;

	list_for_each_entry(urb, &hep->urb_list, urb_list) {
		tmp = snprintf(buf, max, "\t%s urb %p %d/%d\n",
				usb_pipein(urb->pipe) ? "in" : "out",
				urb, urb->actual_length,
				urb->transfer_buffer_length);
		if (tmp <= 0)
			break;
		tmp = min(tmp, (int)max);
		count += tmp;
		buf += tmp;
		max -= tmp;
	}
	return count;
}

static int
dump_queue(struct list_head *q, char *buf, unsigned max)
{
	int		count = 0;
	struct musb_qh	*qh;

	list_for_each_entry(qh, q, ring) {
		int	tmp;

		tmp = dump_qh(qh, buf, max);
		if (tmp <= 0)
			break;
		tmp = min(tmp, (int)max);
		count += tmp;
		buf += tmp;
		max -= tmp;
	}
	return count;
}

#endif	/* HCD */

#ifdef CONFIG_USB_GADGET_MUSB_HDRC
static int dump_ep(struct musb_ep *ep, char *buffer, unsigned max)
{
	char		*buf = buffer;
	int		code = 0;
	void __iomem	*regs = ep->hw_ep->regs;
	char		*mode = "1buf";

	if (ep->is_in) {
		if (ep->hw_ep->tx_double_buffered)
			mode = "2buf";
	} else {
		if (ep->hw_ep->rx_double_buffered)
			mode = "2buf";
	}

	do {
		struct usb_request	*req;

		code = snprintf(buf, max,
				"\n%s (hw%d): %s%s, csr %04x maxp %04x\n",
				ep->name, ep->current_epnum,
				mode, ep->dma ? " dma" : "",
				musb_readw(regs,
					(ep->is_in || !ep->current_epnum)
						? MUSB_TXCSR
						: MUSB_RXCSR),
				musb_readw(regs, ep->is_in
						? MUSB_TXMAXP
						: MUSB_RXMAXP)
				);
		if (code <= 0)
			break;
		code = min(code, (int) max);
		buf += code;
		max -= code;

		if (is_cppi_enabled() && ep->current_epnum) {
			unsigned	cppi = ep->current_epnum - 1;
			void __iomem	*base = ep->musb->ctrl_base;
			unsigned	off1 = cppi << 2;
			void __iomem	*ram = base;
			char		tmp[16];

			if (ep->is_in) {
				ram += DAVINCI_TXCPPI_STATERAM_OFFSET(cppi);
				tmp[0] = 0;
			} else {
				ram += DAVINCI_RXCPPI_STATERAM_OFFSET(cppi);
				snprintf(tmp, sizeof tmp, "%d left, ",
					musb_readl(base,
					DAVINCI_RXCPPI_BUFCNT0_REG + off1));
			}

			code = snprintf(buf, max, "%cX DMA%d: %s"
					"%08x %08x, %08x %08x; "
					"%08x %08x %08x .. %08x\n",
				ep->is_in ? 'T' : 'R',
				ep->current_epnum - 1, tmp,
				musb_readl(ram, 0 * 4),
				musb_readl(ram, 1 * 4),
				musb_readl(ram, 2 * 4),
				musb_readl(ram, 3 * 4),
				musb_readl(ram, 4 * 4),
				musb_readl(ram, 5 * 4),
				musb_readl(ram, 6 * 4),
				musb_readl(ram, 7 * 4));
			if (code <= 0)
				break;
			code = min(code, (int) max);
			buf += code;
			max -= code;
		}

		if (list_empty(&ep->req_list)) {
			code = snprintf(buf, max, "\t(queue empty)\n");
			if (code <= 0)
				break;
			code = min(code, (int) max);
			buf += code;
			max -= code;
			break;
		}
		list_for_each_entry(req, &ep->req_list, list) {
			code = snprintf(buf, max, "\treq %p, %s%s%d/%d\n",
					req,
					req->zero ? "zero, " : "",
					req->short_not_ok ? "!short, " : "",
					req->actual, req->length);
			if (code <= 0)
				break;
			code = min(code, (int) max);
			buf += code;
			max -= code;
		}
	} while (0);
	return buf - buffer;
}
#endif

static int
dump_end_info(struct musb *musb, u8 epnum, char *aBuffer, unsigned max)
{
	int			code = 0;
	char			*buf = aBuffer;
	struct musb_hw_ep	*hw_ep = &musb->endpoints[epnum];

	do {
		musb_ep_select(musb->mregs, epnum);
#ifdef CONFIG_USB_MUSB_HDRC_HCD
		if (is_host_active(musb)) {
			int		dump_rx, dump_tx;
			void __iomem	*regs = hw_ep->regs;

			/* TEMPORARY (!) until we have a real periodic
			 * schedule tree ...
			 */
			if (!epnum) {
				/* control is shared, uses RX queue
				 * but (mostly) shadowed tx registers
				 */
				dump_tx = !list_empty(&musb->control);
				dump_rx = 0;
			} else if (hw_ep == musb->bulk_ep) {
				dump_tx = !list_empty(&musb->out_bulk);
				dump_rx = !list_empty(&musb->in_bulk);
			} else if (musb->periodic[epnum]) {
				struct usb_host_endpoint	*hep;

				hep = musb->periodic[epnum]->hep;
				dump_rx = hep->desc.bEndpointAddress
						& USB_ENDPOINT_DIR_MASK;
				dump_tx = !dump_rx;
			} else
				break;
			/* END TEMPORARY */


			if (dump_rx) {
				code = snprintf(buf, max,
					"\nRX%d: %s rxcsr %04x interval %02x "
					"max %04x type %02x; "
					"dev %d hub %d port %d"
					"\n",
					epnum,
					hw_ep->rx_double_buffered
						? "2buf" : "1buf",
					musb_readw(regs, MUSB_RXCSR),
					musb_readb(regs, MUSB_RXINTERVAL),
					musb_readw(regs, MUSB_RXMAXP),
					musb_readb(regs, MUSB_RXTYPE),
					/* FIXME:  assumes multipoint */
					musb_readb(musb->mregs,
						MUSB_BUSCTL_OFFSET(epnum,
						MUSB_RXFUNCADDR)),
					musb_readb(musb->mregs,
						MUSB_BUSCTL_OFFSET(epnum,
						MUSB_RXHUBADDR)),
					musb_readb(musb->mregs,
						MUSB_BUSCTL_OFFSET(epnum,
						MUSB_RXHUBPORT))
					);
				if (code <= 0)
					break;
				code = min(code, (int) max);
				buf += code;
				max -= code;

				if (is_cppi_enabled()
						&& epnum
						&& hw_ep->rx_channel) {
					unsigned	cppi = epnum - 1;
					unsigned	off1 = cppi << 2;
					void __iomem	*base;
					void __iomem	*ram;
					char		tmp[16];

					base = musb->ctrl_base;
					ram = DAVINCI_RXCPPI_STATERAM_OFFSET(
							cppi) + base;
					snprintf(tmp, sizeof tmp, "%d left, ",
						musb_readl(base,
						DAVINCI_RXCPPI_BUFCNT0_REG
								+ off1));

					code = snprintf(buf, max,
						"    rx dma%d: %s"
						"%08x %08x, %08x %08x; "
						"%08x %08x %08x .. %08x\n",
						cppi, tmp,
						musb_readl(ram, 0 * 4),
						musb_readl(ram, 1 * 4),
						musb_readl(ram, 2 * 4),
						musb_readl(ram, 3 * 4),
						musb_readl(ram, 4 * 4),
						musb_readl(ram, 5 * 4),
						musb_readl(ram, 6 * 4),
						musb_readl(ram, 7 * 4));
					if (code <= 0)
						break;
					code = min(code, (int) max);
					buf += code;
					max -= code;
				}

				if (hw_ep == musb->bulk_ep
						&& !list_empty(
							&musb->in_bulk)) {
					code = dump_queue(&musb->in_bulk,
							buf, max);
					if (code <= 0)
						break;
					code = min(code, (int) max);
					buf += code;
					max -= code;
				} else if (musb->periodic[epnum]) {
					code = dump_qh(musb->periodic[epnum],
							buf, max);
					if (code <= 0)
						break;
					code = min(code, (int) max);
					buf += code;
					max -= code;
				}
			}

			if (dump_tx) {
				code = snprintf(buf, max,
					"\nTX%d: %s txcsr %04x interval %02x "
					"max %04x type %02x; "
					"dev %d hub %d port %d"
					"\n",
					epnum,
					hw_ep->tx_double_buffered
						? "2buf" : "1buf",
					musb_readw(regs, MUSB_TXCSR),
					musb_readb(regs, MUSB_TXINTERVAL),
					musb_readw(regs, MUSB_TXMAXP),
					musb_readb(regs, MUSB_TXTYPE),
					/* FIXME:  assumes multipoint */
					musb_readb(musb->mregs,
						MUSB_BUSCTL_OFFSET(epnum,
						MUSB_TXFUNCADDR)),
					musb_readb(musb->mregs,
						MUSB_BUSCTL_OFFSET(epnum,
						MUSB_TXHUBADDR)),
					musb_readb(musb->mregs,
						MUSB_BUSCTL_OFFSET(epnum,
						MUSB_TXHUBPORT))
					);
				if (code <= 0)
					break;
				code = min(code, (int) max);
				buf += code;
				max -= code;

				if (is_cppi_enabled()
						&& epnum
						&& hw_ep->tx_channel) {
					unsigned	cppi = epnum - 1;
					void __iomem	*base;
					void __iomem	*ram;

					base = musb->ctrl_base;
					ram = DAVINCI_RXCPPI_STATERAM_OFFSET(
							cppi) + base;
					code = snprintf(buf, max,
						"    tx dma%d: "
						"%08x %08x, %08x %08x; "
						"%08x %08x %08x .. %08x\n",
						cppi,
						musb_readl(ram, 0 * 4),
						musb_readl(ram, 1 * 4),
						musb_readl(ram, 2 * 4),
						musb_readl(ram, 3 * 4),
						musb_readl(ram, 4 * 4),
						musb_readl(ram, 5 * 4),
						musb_readl(ram, 6 * 4),
						musb_readl(ram, 7 * 4));
					if (code <= 0)
						break;
					code = min(code, (int) max);
					buf += code;
					max -= code;
				}

				if (hw_ep == musb->control_ep
						&& !list_empty(
							&musb->control)) {
					code = dump_queue(&musb->control,
							buf, max);
					if (code <= 0)
						break;
					code = min(code, (int) max);
					buf += code;
					max -= code;
				} else if (hw_ep == musb->bulk_ep
						&& !list_empty(
							&musb->out_bulk)) {
					code = dump_queue(&musb->out_bulk,
							buf, max);
					if (code <= 0)
						break;
					code = min(code, (int) max);
					buf += code;
					max -= code;
				} else if (musb->periodic[epnum]) {
					code = dump_qh(musb->periodic[epnum],
							buf, max);
					if (code <= 0)
						break;
					code = min(code, (int) max);
					buf += code;
					max -= code;
				}
			}
		}
#endif
#ifdef CONFIG_USB_GADGET_MUSB_HDRC
		if (is_peripheral_active(musb)) {
			code = 0;

			if (hw_ep->ep_in.desc || !epnum) {
				code = dump_ep(&hw_ep->ep_in, buf, max);
				if (code <= 0)
					break;
				code = min(code, (int) max);
				buf += code;
				max -= code;
			}
			if (hw_ep->ep_out.desc) {
				code = dump_ep(&hw_ep->ep_out, buf, max);
				if (code <= 0)
					break;
				code = min(code, (int) max);
				buf += code;
				max -= code;
			}
		}
#endif
	} while (0);

	return buf - aBuffer;
}

/* Dump the current status and compile options.
 * @param musb the device driver instance
 * @param buffer where to dump the status; it must be big enough to hold the
 * result otherwise "BAD THINGS HAPPENS(TM)".
 */
static int dump_header_stats(struct musb *musb, char *buffer)
{
	int code, count = 0;
	const void __iomem *mbase = musb->mregs;

	*buffer = 0;
	count = sprintf(buffer, "Status: %sHDRC, Mode=%s "
				"(Power=%02x, DevCtl=%02x)\n",
			(musb->is_multipoint ? "M" : ""), MUSB_MODE(musb),
			musb_readb(mbase, MUSB_POWER),
			musb_readb(mbase, MUSB_DEVCTL));
	if (count <= 0)
		return 0;
	buffer += count;

	code = sprintf(buffer, "OTG state: %s; %sactive\n",
			otg_state_string(musb),
			musb->is_active ? "" : "in");
	if (code <= 0)
		goto done;
	buffer += code;
	count += code;

	code = sprintf(buffer,
			"Options: "
#ifdef CONFIG_MUSB_PIO_ONLY
			"pio"
#elif defined(CONFIG_USB_TI_CPPI_DMA)
			"cppi-dma"
#elif defined(CONFIG_USB_INVENTRA_DMA)
			"musb-dma"
#elif defined(CONFIG_USB_TUSB_OMAP_DMA)
			"tusb-omap-dma"
#else
			"?dma?"
#endif
			", "
#ifdef CONFIG_USB_MUSB_OTG
			"otg (peripheral+host)"
#elif defined(CONFIG_USB_GADGET_MUSB_HDRC)
			"peripheral"
#elif defined(CONFIG_USB_MUSB_HDRC_HCD)
			"host"
#endif
			", debug=%d [eps=%d]\n",
		debug,
		musb->nr_endpoints);
	if (code <= 0)
		goto done;
	count += code;
	buffer += code;

#ifdef	CONFIG_USB_GADGET_MUSB_HDRC
	code = sprintf(buffer, "Peripheral address: %02x\n",
			musb_readb(musb->ctrl_base, MUSB_FADDR));
	if (code <= 0)
		goto done;
	buffer += code;
	count += code;
#endif

#ifdef	CONFIG_USB_MUSB_HDRC_HCD
	code = sprintf(buffer, "Root port status: %08x\n",
			musb->port1_status);
	if (code <= 0)
		goto done;
	buffer += code;
	count += code;
#endif

#ifdef	CONFIG_ARCH_DAVINCI
	code = sprintf(buffer,
			"DaVinci: ctrl=%02x stat=%1x phy=%03x\n"
			"\trndis=%05x auto=%04x intsrc=%08x intmsk=%08x"
			"\n",
			musb_readl(musb->ctrl_base, DAVINCI_USB_CTRL_REG),
			musb_readl(musb->ctrl_base, DAVINCI_USB_STAT_REG),
			__raw_readl((void __force __iomem *)
					IO_ADDRESS(USBPHY_CTL_PADDR)),
			musb_readl(musb->ctrl_base, DAVINCI_RNDIS_REG),
			musb_readl(musb->ctrl_base, DAVINCI_AUTOREQ_REG),
			musb_readl(musb->ctrl_base,
					DAVINCI_USB_INT_SOURCE_REG),
			musb_readl(musb->ctrl_base,
					DAVINCI_USB_INT_MASK_REG));
	if (code <= 0)
		goto done;
	count += code;
	buffer += code;
#endif	/* DAVINCI */

#ifdef CONFIG_USB_TUSB6010
	code = sprintf(buffer,
			"TUSB6010: devconf %08x, phy enable %08x drive %08x"
			"\n\totg %03x timer %08x"
			"\n\tprcm conf %08x mgmt %08x; int src %08x mask %08x"
			"\n",
			musb_readl(musb->ctrl_base, TUSB_DEV_CONF),
			musb_readl(musb->ctrl_base, TUSB_PHY_OTG_CTRL_ENABLE),
			musb_readl(musb->ctrl_base, TUSB_PHY_OTG_CTRL),
			musb_readl(musb->ctrl_base, TUSB_DEV_OTG_STAT),
			musb_readl(musb->ctrl_base, TUSB_DEV_OTG_TIMER),
			musb_readl(musb->ctrl_base, TUSB_PRCM_CONF),
			musb_readl(musb->ctrl_base, TUSB_PRCM_MNGMT),
			musb_readl(musb->ctrl_base, TUSB_INT_SRC),
			musb_readl(musb->ctrl_base, TUSB_INT_MASK));
	if (code <= 0)
		goto done;
	count += code;
	buffer += code;
#endif	/* DAVINCI */

	if (is_cppi_enabled() && musb->dma_controller) {
		code = sprintf(buffer,
				"CPPI: txcr=%d txsrc=%01x txena=%01x; "
				"rxcr=%d rxsrc=%01x rxena=%01x "
				"\n",
				musb_readl(musb->ctrl_base,
						DAVINCI_TXCPPI_CTRL_REG),
				musb_readl(musb->ctrl_base,
						DAVINCI_TXCPPI_RAW_REG),
				musb_readl(musb->ctrl_base,
						DAVINCI_TXCPPI_INTENAB_REG),
				musb_readl(musb->ctrl_base,
						DAVINCI_RXCPPI_CTRL_REG),
				musb_readl(musb->ctrl_base,
						DAVINCI_RXCPPI_RAW_REG),
				musb_readl(musb->ctrl_base,
						DAVINCI_RXCPPI_INTENAB_REG));
		if (code <= 0)
			goto done;
		count += code;
		buffer += code;
	}

#ifdef CONFIG_USB_GADGET_MUSB_HDRC
	if (is_peripheral_enabled(musb)) {
		code = sprintf(buffer, "Gadget driver: %s\n",
				musb->gadget_driver
					? musb->gadget_driver->driver.name
					: "(none)");
		if (code <= 0)
			goto done;
		count += code;
		buffer += code;
	}
#endif

done:
	return count;
}

/* Write to ProcFS
 *
 * C soft-connect
 * c soft-disconnect
 * I enable HS
 * i disable HS
 * s stop session
 * F force session (OTG-unfriendly)
 * E rElinquish bus (OTG)
 * H request host mode
 * h cancel host request
 * T start sending TEST_PACKET
 * D<num> set/query the debug level
 */
static int musb_proc_write(struct file *file, const char __user *buffer,
			unsigned long count, void *data)
{
	char cmd;
	u8 reg;
	struct musb *musb = (struct musb *)data;
	void __iomem *mbase = musb->mregs;

	/* MOD_INC_USE_COUNT; */

	if (unlikely(copy_from_user(&cmd, buffer, 1)))
		return -EFAULT;

	switch (cmd) {
	case 'C':
		if (mbase) {
			reg = musb_readb(mbase, MUSB_POWER)
					| MUSB_POWER_SOFTCONN;
			musb_writeb(mbase, MUSB_POWER, reg);
		}
		break;

	case 'c':
		if (mbase) {
			reg = musb_readb(mbase, MUSB_POWER)
					& ~MUSB_POWER_SOFTCONN;
			musb_writeb(mbase, MUSB_POWER, reg);
		}
		break;

	case 'I':
		if (mbase) {
			reg = musb_readb(mbase, MUSB_POWER)
					| MUSB_POWER_HSENAB;
			musb_writeb(mbase, MUSB_POWER, reg);
		}
		break;

	case 'i':
		if (mbase) {
			reg = musb_readb(mbase, MUSB_POWER)
					& ~MUSB_POWER_HSENAB;
			musb_writeb(mbase, MUSB_POWER, reg);
		}
		break;

	case 'F':
		reg = musb_readb(mbase, MUSB_DEVCTL);
		reg |= MUSB_DEVCTL_SESSION;
		musb_writeb(mbase, MUSB_DEVCTL, reg);
		break;

	case 'H':
		if (mbase) {
			reg = musb_readb(mbase, MUSB_DEVCTL);
			reg |= MUSB_DEVCTL_HR;
			musb_writeb(mbase, MUSB_DEVCTL, reg);
			/* MUSB_HST_MODE( ((struct musb*)data) ); */
			/* WARNING("Host Mode\n"); */
		}
		break;

	case 'h':
		if (mbase) {
			reg = musb_readb(mbase, MUSB_DEVCTL);
			reg &= ~MUSB_DEVCTL_HR;
			musb_writeb(mbase, MUSB_DEVCTL, reg);
		}
		break;

	case 'T':
		if (mbase) {
			musb_load_testpacket(musb);
			musb_writeb(mbase, MUSB_TESTMODE,
					MUSB_TEST_PACKET);
		}
		break;

#if (MUSB_DEBUG > 0)
		/* set/read debug level */
	case 'D':{
			if (count > 1) {
				char digits[8], *p = digits;
				int i = 0, level = 0, sign = 1;
				int len = min(count - 1, (unsigned long)8);

				if (copy_from_user(&digits, &buffer[1], len))
					return -EFAULT;

				/* optional sign */
				if (*p == '-') {
					len -= 1;
					sign = -sign;
					p++;
				}

				/* read it */
				while (i++ < len && *p > '0' && *p < '9') {
					level = level * 10 + (*p - '0');
					p++;
				}

				level *= sign;
				DBG(1, "debug level %d\n", level);
				debug = level;
			}
		}
		break;


	case '?':
		INFO("?: you are seeing it\n");
		INFO("C/c: soft connect enable/disable\n");
		INFO("I/i: hispeed enable/disable\n");
		INFO("F: force session start\n");
		INFO("H: host mode\n");
		INFO("T: start sending TEST_PACKET\n");
		INFO("D: set/read dbug level\n");
		break;
#endif

	default:
		ERR("Command %c not implemented\n", cmd);
		break;
	}

	musb_platform_try_idle(musb, 0);

	return count;
}

static int musb_proc_read(char *page, char **start,
			off_t off, int count, int *eof, void *data)
{
	char *buffer = page;
	int code = 0;
	unsigned long	flags;
	struct musb	*musb = data;
	unsigned	epnum;

	count -= off;
	count -= 1;		/* for NUL at end */
	if (count <= 0)
		return -EINVAL;

	spin_lock_irqsave(&musb->lock, flags);

	code = dump_header_stats(musb, buffer);
	if (code > 0) {
		buffer += code;
		count -= code;
	}

	/* generate the report for the end points */
	/* REVISIT ... not unless something's connected! */
	for (epnum = 0; count >= 0 && epnum < musb->nr_endpoints;
			epnum++) {
		code = dump_end_info(musb, epnum, buffer, count);
		if (code > 0) {
			buffer += code;
			count -= code;
		}
	}

	musb_platform_try_idle(musb, 0);

	spin_unlock_irqrestore(&musb->lock, flags);
	*eof = 1;

	return buffer - page;
}

void __devexit musb_debug_delete(char *name, struct musb *musb)
{
	if (musb->proc_entry)
		remove_proc_entry(name, NULL);
}

struct proc_dir_entry *__init
musb_debug_create(char *name, struct musb *data)
{
	struct proc_dir_entry	*pde;

	/* FIXME convert everything to seq_file; then later, debugfs */

	if (!name)
		return NULL;

	pde = create_proc_entry(name, S_IFREG | S_IRUGO | S_IWUSR, NULL);
	data->proc_entry = pde;
	if (pde) {
		pde->data = data;
		/* pde->owner = THIS_MODULE; */

		pde->read_proc = musb_proc_read;
		pde->write_proc = musb_proc_write;

		pde->size = 0;

		pr_debug("Registered /proc/%s\n", name);
	} else {
		pr_debug("Cannot create a valid proc file entry");
	}

	return pde;
}
