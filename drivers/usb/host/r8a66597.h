/*
 * R8A66597 HCD (Host Controller Driver)
 *
 * Copyright (C) 2006-2007 Renesas Solutions Corp.
 * Portions Copyright (C) 2004 Psion Teklogix (for NetBook PRO)
 * Portions Copyright (C) 2004-2005 David Brownell
 * Portions Copyright (C) 1999 Roman Weissgaerber
 *
 * Author : Yoshihiro Shimoda <shimoda.yoshihiro@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
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

#ifndef __R8A66597_H__
#define __R8A66597_H__

#ifdef CONFIG_HAVE_CLK
#include <linux/clk.h>
#endif

#include <linux/usb/r8a66597.h>

#define R8A66597_MAX_NUM_PIPE		10
#define R8A66597_BUF_BSIZE		8
#define R8A66597_MAX_DEVICE		10
#define R8A66597_MAX_ROOT_HUB		2
#define R8A66597_MAX_SAMPLING		5
#define R8A66597_RH_POLL_TIME		10
#define R8A66597_MAX_DMA_CHANNEL	2
#define R8A66597_PIPE_NO_DMA		R8A66597_MAX_DMA_CHANNEL
#define check_bulk_or_isoc(pipenum)	((pipenum >= 1 && pipenum <= 5))
#define check_interrupt(pipenum)	((pipenum >= 6 && pipenum <= 9))
#define make_devsel(addr)		(addr << 12)

struct r8a66597_pipe_info {
	unsigned long timer_interval;
	u16 pipenum;
	u16 address;	/* R8A66597 HCD usb address */
	u16 epnum;
	u16 maxpacket;
	u16 type;
	u16 bufnum;
	u16 buf_bsize;
	u16 interval;
	u16 dir_in;
};

struct r8a66597_pipe {
	struct r8a66597_pipe_info info;

	unsigned long fifoaddr;
	unsigned long fifosel;
	unsigned long fifoctr;
	unsigned long pipectr;
	unsigned long pipetre;
	unsigned long pipetrn;
};

struct r8a66597_td {
	struct r8a66597_pipe *pipe;
	struct urb *urb;
	struct list_head queue;

	u16 type;
	u16 pipenum;
	int iso_cnt;

	u16 address;		/* R8A66597's USB address */
	u16 maxpacket;

	unsigned zero_packet:1;
	unsigned short_packet:1;
	unsigned set_address:1;
};

struct r8a66597_device {
	u16	address;	/* R8A66597's USB address */
	u16	hub_port;
	u16	root_port;

	unsigned short ep_in_toggle;
	unsigned short ep_out_toggle;
	unsigned char pipe_cnt[R8A66597_MAX_NUM_PIPE];
	unsigned char dma_map;

	enum usb_device_state state;

	struct usb_device *udev;
	int usb_address;
	struct list_head device_list;
};

struct r8a66597_root_hub {
	u32 port;
	u16 old_syssts;
	int scount;

	struct r8a66597_device	*dev;
};

struct r8a66597 {
	spinlock_t lock;
	void __iomem *reg;
#ifdef CONFIG_HAVE_CLK
	struct clk *clk;
#endif
	struct r8a66597_platdata	*pdata;
	struct r8a66597_device		device0;
	struct r8a66597_root_hub	root_hub[R8A66597_MAX_ROOT_HUB];
	struct list_head		pipe_queue[R8A66597_MAX_NUM_PIPE];

	struct timer_list rh_timer;
	struct timer_list td_timer[R8A66597_MAX_NUM_PIPE];
	struct timer_list interval_timer[R8A66597_MAX_NUM_PIPE];

	unsigned short address_map;
	unsigned short timeout_map;
	unsigned short interval_map;
	unsigned char pipe_cnt[R8A66597_MAX_NUM_PIPE];
	unsigned char dma_map;
	unsigned int max_root_hub;

	struct list_head child_device;
	unsigned long child_connect_map[4];

	unsigned bus_suspended:1;
	unsigned irq_sense_low:1;
};

static inline struct r8a66597 *hcd_to_r8a66597(struct usb_hcd *hcd)
{
	return (struct r8a66597 *)(hcd->hcd_priv);
}

static inline struct usb_hcd *r8a66597_to_hcd(struct r8a66597 *r8a66597)
{
	return container_of((void *)r8a66597, struct usb_hcd, hcd_priv);
}

static inline struct r8a66597_td *r8a66597_get_td(struct r8a66597 *r8a66597,
						  u16 pipenum)
{
	if (unlikely(list_empty(&r8a66597->pipe_queue[pipenum])))
		return NULL;

	return list_entry(r8a66597->pipe_queue[pipenum].next,
			  struct r8a66597_td, queue);
}

static inline struct urb *r8a66597_get_urb(struct r8a66597 *r8a66597,
					   u16 pipenum)
{
	struct r8a66597_td *td;

	td = r8a66597_get_td(r8a66597, pipenum);
	return (td ? td->urb : NULL);
}

static inline u16 r8a66597_read(struct r8a66597 *r8a66597, unsigned long offset)
{
	return ioread16(r8a66597->reg + offset);
}

static inline void r8a66597_read_fifo(struct r8a66597 *r8a66597,
				      unsigned long offset, u16 *buf,
				      int len)
{
	void __iomem *fifoaddr = r8a66597->reg + offset;
	unsigned long count;

	if (r8a66597->pdata->on_chip) {
		count = len / 4;
		ioread32_rep(fifoaddr, buf, count);

		if (len & 0x00000003) {
			unsigned long tmp = ioread32(fifoaddr);
			memcpy((unsigned char *)buf + count * 4, &tmp,
			       len & 0x03);
		}
	} else {
		len = (len + 1) / 2;
		ioread16_rep(fifoaddr, buf, len);
	}
}

static inline void r8a66597_write(struct r8a66597 *r8a66597, u16 val,
				  unsigned long offset)
{
	iowrite16(val, r8a66597->reg + offset);
}

static inline void r8a66597_mdfy(struct r8a66597 *r8a66597,
				 u16 val, u16 pat, unsigned long offset)
{
	u16 tmp;
	tmp = r8a66597_read(r8a66597, offset);
	tmp = tmp & (~pat);
	tmp = tmp | val;
	r8a66597_write(r8a66597, tmp, offset);
}

#define r8a66597_bclr(r8a66597, val, offset)	\
			r8a66597_mdfy(r8a66597, 0, val, offset)
#define r8a66597_bset(r8a66597, val, offset)	\
			r8a66597_mdfy(r8a66597, val, 0, offset)

static inline void r8a66597_write_fifo(struct r8a66597 *r8a66597,
				       struct r8a66597_pipe *pipe, u16 *buf,
				       int len)
{
	void __iomem *fifoaddr = r8a66597->reg + pipe->fifoaddr;
	unsigned long count;
	unsigned char *pb;
	int i;

	if (r8a66597->pdata->on_chip) {
		count = len / 4;
		iowrite32_rep(fifoaddr, buf, count);

		if (len & 0x00000003) {
			pb = (unsigned char *)buf + count * 4;
			for (i = 0; i < (len & 0x00000003); i++) {
				if (r8a66597_read(r8a66597, CFIFOSEL) & BIGEND)
					iowrite8(pb[i], fifoaddr + i);
				else
					iowrite8(pb[i], fifoaddr + 3 - i);
			}
		}
	} else {
		int odd = len & 0x0001;

		len = len / 2;
		iowrite16_rep(fifoaddr, buf, len);
		if (unlikely(odd)) {
			buf = &buf[len];
			if (r8a66597->pdata->wr0_shorted_to_wr1)
				r8a66597_bclr(r8a66597, MBW_16, pipe->fifosel);
			iowrite8((unsigned char)*buf, fifoaddr);
			if (r8a66597->pdata->wr0_shorted_to_wr1)
				r8a66597_bset(r8a66597, MBW_16, pipe->fifosel);
		}
	}
}

static inline unsigned long get_syscfg_reg(int port)
{
	return port == 0 ? SYSCFG0 : SYSCFG1;
}

static inline unsigned long get_syssts_reg(int port)
{
	return port == 0 ? SYSSTS0 : SYSSTS1;
}

static inline unsigned long get_dvstctr_reg(int port)
{
	return port == 0 ? DVSTCTR0 : DVSTCTR1;
}

static inline unsigned long get_dmacfg_reg(int port)
{
	return port == 0 ? DMA0CFG : DMA1CFG;
}

static inline unsigned long get_intenb_reg(int port)
{
	return port == 0 ? INTENB1 : INTENB2;
}

static inline unsigned long get_intsts_reg(int port)
{
	return port == 0 ? INTSTS1 : INTSTS2;
}

static inline u16 get_rh_usb_speed(struct r8a66597 *r8a66597, int port)
{
	unsigned long dvstctr_reg = get_dvstctr_reg(port);

	return r8a66597_read(r8a66597, dvstctr_reg) & RHST;
}

static inline void r8a66597_port_power(struct r8a66597 *r8a66597, int port,
				       int power)
{
	unsigned long dvstctr_reg = get_dvstctr_reg(port);

	if (r8a66597->pdata->port_power) {
		r8a66597->pdata->port_power(port, power);
	} else {
		if (power)
			r8a66597_bset(r8a66597, VBOUT, dvstctr_reg);
		else
			r8a66597_bclr(r8a66597, VBOUT, dvstctr_reg);
	}
}

static inline u16 get_xtal_from_pdata(struct r8a66597_platdata *pdata)
{
	u16 clock = 0;

	switch (pdata->xtal) {
	case R8A66597_PLATDATA_XTAL_12MHZ:
		clock = XTAL12;
		break;
	case R8A66597_PLATDATA_XTAL_24MHZ:
		clock = XTAL24;
		break;
	case R8A66597_PLATDATA_XTAL_48MHZ:
		clock = XTAL48;
		break;
	default:
		printk(KERN_ERR "r8a66597: platdata clock is wrong.\n");
		break;
	}

	return clock;
}

#define get_pipectr_addr(pipenum)	(PIPE1CTR + (pipenum - 1) * 2)
#define get_pipetre_addr(pipenum)	(PIPE1TRE + (pipenum - 1) * 4)
#define get_pipetrn_addr(pipenum)	(PIPE1TRN + (pipenum - 1) * 4)
#define get_devadd_addr(address)	(DEVADD0 + address * 2)

#define enable_irq_ready(r8a66597, pipenum)	\
	enable_pipe_irq(r8a66597, pipenum, BRDYENB)
#define disable_irq_ready(r8a66597, pipenum)	\
	disable_pipe_irq(r8a66597, pipenum, BRDYENB)
#define enable_irq_empty(r8a66597, pipenum)	\
	enable_pipe_irq(r8a66597, pipenum, BEMPENB)
#define disable_irq_empty(r8a66597, pipenum)	\
	disable_pipe_irq(r8a66597, pipenum, BEMPENB)
#define enable_irq_nrdy(r8a66597, pipenum)	\
	enable_pipe_irq(r8a66597, pipenum, NRDYENB)
#define disable_irq_nrdy(r8a66597, pipenum)	\
	disable_pipe_irq(r8a66597, pipenum, NRDYENB)

#endif	/* __R8A66597_H__ */

