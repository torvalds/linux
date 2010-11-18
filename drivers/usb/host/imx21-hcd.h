/*
 * Macros and prototypes for i.MX21
 *
 * Copyright (C) 2006 Loping Dog Embedded Systems
 * Copyright (C) 2009 Martin Fuzzey
 * Originally written by Jay Monkman <jtm@lopingdog.com>
 * Ported to 2.6.30, debugged and enhanced by Martin Fuzzey
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __LINUX_IMX21_HCD_H__
#define __LINUX_IMX21_HCD_H__

#include <mach/mx21-usbhost.h>

#define NUM_ISO_ETDS 	2
#define USB_NUM_ETD	32
#define DMEM_SIZE   	4096

/* Register definitions */
#define USBOTG_HWMODE		0x00
#define USBOTG_HWMODE_ANASDBEN		(1 << 14)
#define USBOTG_HWMODE_OTGXCVR_SHIFT	6
#define USBOTG_HWMODE_OTGXCVR_MASK	(3 << 6)
#define USBOTG_HWMODE_OTGXCVR_TD_RD	(0 << 6)
#define USBOTG_HWMODE_OTGXCVR_TS_RD	(2 << 6)
#define USBOTG_HWMODE_OTGXCVR_TD_RS	(1 << 6)
#define USBOTG_HWMODE_OTGXCVR_TS_RS	(3 << 6)
#define USBOTG_HWMODE_HOSTXCVR_SHIFT	4
#define USBOTG_HWMODE_HOSTXCVR_MASK	(3 << 4)
#define USBOTG_HWMODE_HOSTXCVR_TD_RD	(0 << 4)
#define USBOTG_HWMODE_HOSTXCVR_TS_RD	(2 << 4)
#define USBOTG_HWMODE_HOSTXCVR_TD_RS	(1 << 4)
#define USBOTG_HWMODE_HOSTXCVR_TS_RS	(3 << 4)
#define USBOTG_HWMODE_CRECFG_MASK	(3 << 0)
#define USBOTG_HWMODE_CRECFG_HOST	(1 << 0)
#define USBOTG_HWMODE_CRECFG_FUNC	(2 << 0)
#define USBOTG_HWMODE_CRECFG_HNP	(3 << 0)

#define USBOTG_CINT_STAT	0x04
#define USBOTG_CINT_STEN	0x08
#define USBOTG_ASHNPINT			(1 << 5)
#define USBOTG_ASFCINT			(1 << 4)
#define USBOTG_ASHCINT			(1 << 3)
#define USBOTG_SHNPINT			(1 << 2)
#define USBOTG_FCINT			(1 << 1)
#define USBOTG_HCINT			(1 << 0)

#define USBOTG_CLK_CTRL		0x0c
#define USBOTG_CLK_CTRL_FUNC		(1 << 2)
#define USBOTG_CLK_CTRL_HST		(1 << 1)
#define USBOTG_CLK_CTRL_MAIN		(1 << 0)

#define USBOTG_RST_CTRL		0x10
#define USBOTG_RST_RSTI2C		(1 << 15)
#define USBOTG_RST_RSTCTRL		(1 << 5)
#define USBOTG_RST_RSTFC		(1 << 4)
#define USBOTG_RST_RSTFSKE		(1 << 3)
#define USBOTG_RST_RSTRH		(1 << 2)
#define USBOTG_RST_RSTHSIE		(1 << 1)
#define USBOTG_RST_RSTHC		(1 << 0)

#define USBOTG_FRM_INTVL    	0x14
#define USBOTG_FRM_REMAIN   	0x18
#define USBOTG_HNP_CSR	    	0x1c
#define USBOTG_HNP_ISR	    	0x2c
#define USBOTG_HNP_IEN	    	0x30

#define USBOTG_I2C_TXCVR_REG(x)	(0x100 + (x))
#define USBOTG_I2C_XCVR_DEVAD		0x118
#define USBOTG_I2C_SEQ_OP_REG		0x119
#define USBOTG_I2C_SEQ_RD_STARTAD	0x11a
#define USBOTG_I2C_OP_CTRL_REG	     	0x11b
#define USBOTG_I2C_SCLK_TO_SCK_HPER  	0x11e
#define USBOTG_I2C_MASTER_INT_REG    	0x11f

#define USBH_HOST_CTRL		0x80
#define USBH_HOST_CTRL_HCRESET			(1 << 31)
#define USBH_HOST_CTRL_SCHDOVR(x)		((x) << 16)
#define USBH_HOST_CTRL_RMTWUEN			(1 << 4)
#define USBH_HOST_CTRL_HCUSBSTE_RESET		(0 << 2)
#define USBH_HOST_CTRL_HCUSBSTE_RESUME		(1 << 2)
#define USBH_HOST_CTRL_HCUSBSTE_OPERATIONAL	(2 << 2)
#define USBH_HOST_CTRL_HCUSBSTE_SUSPEND	(3 << 2)
#define USBH_HOST_CTRL_CTLBLKSR_1		(0 << 0)
#define USBH_HOST_CTRL_CTLBLKSR_2		(1 << 0)
#define USBH_HOST_CTRL_CTLBLKSR_3		(2 << 0)
#define USBH_HOST_CTRL_CTLBLKSR_4		(3 << 0)

#define USBH_SYSISR		0x88
#define USBH_SYSISR_PSCINT		(1 << 6)
#define USBH_SYSISR_FMOFINT		(1 << 5)
#define USBH_SYSISR_HERRINT		(1 << 4)
#define USBH_SYSISR_RESDETINT		(1 << 3)
#define USBH_SYSISR_SOFINT		(1 << 2)
#define USBH_SYSISR_DONEINT		(1 << 1)
#define USBH_SYSISR_SORINT		(1 << 0)

#define USBH_SYSIEN	    	0x8c
#define USBH_SYSIEN_PSCINT		(1 << 6)
#define USBH_SYSIEN_FMOFINT		(1 << 5)
#define USBH_SYSIEN_HERRINT		(1 << 4)
#define USBH_SYSIEN_RESDETINT		(1 << 3)
#define USBH_SYSIEN_SOFINT		(1 << 2)
#define USBH_SYSIEN_DONEINT		(1 << 1)
#define USBH_SYSIEN_SORINT		(1 << 0)

#define USBH_XBUFSTAT	    	0x98
#define USBH_YBUFSTAT	    	0x9c
#define USBH_XYINTEN	    	0xa0
#define USBH_XFILLSTAT	    	0xa8
#define USBH_YFILLSTAT	    	0xac
#define USBH_ETDENSET	    	0xc0
#define USBH_ETDENCLR	    	0xc4
#define USBH_IMMEDINT	    	0xcc
#define USBH_ETDDONESTAT    	0xd0
#define USBH_ETDDONEEN	    	0xd4
#define USBH_FRMNUB	    	0xe0
#define USBH_LSTHRESH	    	0xe4

#define USBH_ROOTHUBA	    	0xe8
#define USBH_ROOTHUBA_PWRTOGOOD_MASK	(0xff)
#define USBH_ROOTHUBA_PWRTOGOOD_SHIFT	(24)
#define USBH_ROOTHUBA_NOOVRCURP	(1 << 12)
#define USBH_ROOTHUBA_OVRCURPM		(1 << 11)
#define USBH_ROOTHUBA_DEVTYPE		(1 << 10)
#define USBH_ROOTHUBA_PWRSWTMD		(1 << 9)
#define USBH_ROOTHUBA_NOPWRSWT		(1 << 8)
#define USBH_ROOTHUBA_NDNSTMPRT_MASK	(0xff)

#define USBH_ROOTHUBB		0xec
#define USBH_ROOTHUBB_PRTPWRCM(x)	(1 << ((x) + 16))
#define USBH_ROOTHUBB_DEVREMOVE(x)	(1 << (x))

#define USBH_ROOTSTAT		0xf0
#define USBH_ROOTSTAT_CLRRMTWUE	(1 << 31)
#define USBH_ROOTSTAT_OVRCURCHG	(1 << 17)
#define USBH_ROOTSTAT_DEVCONWUE	(1 << 15)
#define USBH_ROOTSTAT_OVRCURI		(1 << 1)
#define USBH_ROOTSTAT_LOCPWRS		(1 << 0)

#define USBH_PORTSTAT(x)	(0xf4 + ((x) * 4))
#define USBH_PORTSTAT_PRTRSTSC		(1 << 20)
#define USBH_PORTSTAT_OVRCURIC		(1 << 19)
#define USBH_PORTSTAT_PRTSTATSC	(1 << 18)
#define USBH_PORTSTAT_PRTENBLSC	(1 << 17)
#define USBH_PORTSTAT_CONNECTSC	(1 << 16)
#define USBH_PORTSTAT_LSDEVCON		(1 << 9)
#define USBH_PORTSTAT_PRTPWRST		(1 << 8)
#define USBH_PORTSTAT_PRTRSTST		(1 << 4)
#define USBH_PORTSTAT_PRTOVRCURI	(1 << 3)
#define USBH_PORTSTAT_PRTSUSPST	(1 << 2)
#define USBH_PORTSTAT_PRTENABST	(1 << 1)
#define USBH_PORTSTAT_CURCONST		(1 << 0)

#define USB_DMAREV		0x800
#define USB_DMAINTSTAT	    	0x804
#define USB_DMAINTSTAT_EPERR		(1 << 1)
#define USB_DMAINTSTAT_ETDERR		(1 << 0)

#define USB_DMAINTEN	    	0x808
#define USB_DMAINTEN_EPERRINTEN	(1 << 1)
#define USB_DMAINTEN_ETDERRINTEN	(1 << 0)

#define USB_ETDDMAERSTAT    	0x80c
#define USB_EPDMAERSTAT	    	0x810
#define USB_ETDDMAEN	    	0x820
#define USB_EPDMAEN	    	0x824
#define USB_ETDDMAXTEN	    	0x828
#define USB_EPDMAXTEN	    	0x82c
#define USB_ETDDMAENXYT	    	0x830
#define USB_EPDMAENXYT	    	0x834
#define USB_ETDDMABST4EN    	0x838
#define USB_EPDMABST4EN	    	0x83c

#define USB_MISCCONTROL	    	0x840
#define USB_MISCCONTROL_ISOPREVFRM	(1 << 3)
#define USB_MISCCONTROL_SKPRTRY	(1 << 2)
#define USB_MISCCONTROL_ARBMODE	(1 << 1)
#define USB_MISCCONTROL_FILTCC		(1 << 0)

#define USB_ETDDMACHANLCLR  	0x848
#define USB_EPDMACHANLCLR   	0x84c
#define USB_ETDSMSA(x)	    	(0x900 + ((x) * 4))
#define USB_EPSMSA(x)	    	(0x980 + ((x) * 4))
#define USB_ETDDMABUFPTR(x) 	(0xa00 + ((x) * 4))
#define USB_EPDMABUFPTR(x)  	(0xa80 + ((x) * 4))

#define USB_ETD_DWORD(x, w)	(0x200 + ((x) * 16) + ((w) * 4))
#define DW0_ADDRESS	0
#define	DW0_ENDPNT	7
#define	DW0_DIRECT	11
#define	DW0_SPEED	13
#define DW0_FORMAT	14
#define DW0_MAXPKTSIZ	16
#define DW0_HALTED	27
#define	DW0_TOGCRY	28
#define	DW0_SNDNAK	30

#define DW1_XBUFSRTAD	0
#define DW1_YBUFSRTAD	16

#define DW2_RTRYDELAY	0
#define DW2_POLINTERV	0
#define DW2_STARTFRM	0
#define DW2_RELPOLPOS	8
#define DW2_DIRPID	16
#define	DW2_BUFROUND	18
#define DW2_DELAYINT	19
#define DW2_DATATOG	22
#define DW2_ERRORCNT	24
#define	DW2_COMPCODE	28

#define DW3_TOTBYECNT	0
#define DW3_PKTLEN0	0
#define DW3_COMPCODE0	12
#define DW3_PKTLEN1	16
#define DW3_BUFSIZE	21
#define DW3_COMPCODE1	28

#define USBCTRL		    	0x600
#define USBCTRL_I2C_WU_INT_STAT	(1 << 27)
#define USBCTRL_OTG_WU_INT_STAT	(1 << 26)
#define USBCTRL_HOST_WU_INT_STAT	(1 << 25)
#define USBCTRL_FNT_WU_INT_STAT	(1 << 24)
#define USBCTRL_I2C_WU_INT_EN		(1 << 19)
#define USBCTRL_OTG_WU_INT_EN		(1 << 18)
#define USBCTRL_HOST_WU_INT_EN		(1 << 17)
#define USBCTRL_FNT_WU_INT_EN		(1 << 16)
#define USBCTRL_OTC_RCV_RXDP		(1 << 13)
#define USBCTRL_HOST1_BYP_TLL		(1 << 12)
#define USBCTRL_OTG_BYP_VAL(x)		((x) << 10)
#define USBCTRL_HOST1_BYP_VAL(x)	((x) << 8)
#define USBCTRL_OTG_PWR_MASK		(1 << 6)
#define USBCTRL_HOST1_PWR_MASK		(1 << 5)
#define USBCTRL_HOST2_PWR_MASK		(1 << 4)
#define USBCTRL_USB_BYP			(1 << 2)
#define USBCTRL_HOST1_TXEN_OE		(1 << 1)

#define USBOTG_DMEM		0x1000

/* Values in TD blocks */
#define TD_DIR_SETUP	    0
#define TD_DIR_OUT	    1
#define TD_DIR_IN	    2
#define TD_FORMAT_CONTROL   0
#define TD_FORMAT_ISO	    1
#define TD_FORMAT_BULK	    2
#define TD_FORMAT_INT	    3
#define TD_TOGGLE_CARRY	    0
#define TD_TOGGLE_DATA0	    2
#define TD_TOGGLE_DATA1	    3

/* control transfer states */
#define US_CTRL_SETUP	2
#define US_CTRL_DATA	1
#define US_CTRL_ACK	0

/* bulk transfer main state and 0-length packet */
#define US_BULK		1
#define US_BULK0	0

/*ETD format description*/
#define IMX_FMT_CTRL   0x0
#define IMX_FMT_ISO    0x1
#define IMX_FMT_BULK   0x2
#define IMX_FMT_INT    0x3

static char fmt_urb_to_etd[4] = {
/*PIPE_ISOCHRONOUS*/ IMX_FMT_ISO,
/*PIPE_INTERRUPT*/ IMX_FMT_INT,
/*PIPE_CONTROL*/ IMX_FMT_CTRL,
/*PIPE_BULK*/ IMX_FMT_BULK
};

/* condition (error) CC codes and mapping (OHCI like) */

#define TD_CC_NOERROR		0x00
#define TD_CC_CRC		0x01
#define TD_CC_BITSTUFFING	0x02
#define TD_CC_DATATOGGLEM	0x03
#define TD_CC_STALL		0x04
#define TD_DEVNOTRESP		0x05
#define TD_PIDCHECKFAIL		0x06
/*#define TD_UNEXPECTEDPID	0x07 - reserved, not active on MX2*/
#define TD_DATAOVERRUN		0x08
#define TD_DATAUNDERRUN		0x09
#define TD_BUFFEROVERRUN	0x0C
#define TD_BUFFERUNDERRUN	0x0D
#define	TD_SCHEDULEOVERRUN	0x0E
#define TD_NOTACCESSED		0x0F

static const int cc_to_error[16] = {
	/* No  Error  */ 0,
	/* CRC Error  */ -EILSEQ,
	/* Bit Stuff  */ -EPROTO,
	/* Data Togg  */ -EILSEQ,
	/* Stall      */ -EPIPE,
	/* DevNotResp */ -ETIMEDOUT,
	/* PIDCheck   */ -EPROTO,
	/* UnExpPID   */ -EPROTO,
	/* DataOver   */ -EOVERFLOW,
	/* DataUnder  */ -EREMOTEIO,
	/* (for hw)   */ -EIO,
	/* (for hw)   */ -EIO,
	/* BufferOver */ -ECOMM,
	/* BuffUnder  */ -ENOSR,
	/* (for HCD)  */ -ENOSPC,
	/* (for HCD)  */ -EALREADY
};

/* HCD data associated with a usb core URB */
struct urb_priv {
	struct urb *urb;
	struct usb_host_endpoint *ep;
	int active;
	int state;
	struct td *isoc_td;
	int isoc_remaining;
	int isoc_status;
};

/* HCD data associated with a usb core endpoint */
struct ep_priv {
	struct usb_host_endpoint *ep;
	struct list_head td_list;
	struct list_head queue;
	int etd[NUM_ISO_ETDS];
	int waiting_etd;
};

/* isoc packet */
struct td {
	struct list_head list;
	struct urb *urb;
	struct usb_host_endpoint *ep;
	dma_addr_t dma_handle;
	void *cpu_buffer;
	int len;
	int frame;
	int isoc_index;
};

/* HCD data associated with a hardware ETD */
struct etd_priv {
	struct usb_host_endpoint *ep;
	struct urb *urb;
	struct td *td;
	struct list_head queue;
	dma_addr_t dma_handle;
	void *cpu_buffer;
	void *bounce_buffer;
	int alloc;
	int len;
	int dmem_size;
	int dmem_offset;
	int active_count;
#ifdef DEBUG
	int activated_frame;
	int disactivated_frame;
	int last_int_frame;
	int last_req_frame;
	u32 submitted_dwords[4];
#endif
};

/* Hardware data memory info */
struct imx21_dmem_area {
	struct usb_host_endpoint *ep;
	unsigned int offset;
	unsigned int size;
	struct list_head list;
};

#ifdef DEBUG
struct debug_usage_stats {
	unsigned int value;
	unsigned int maximum;
};

struct debug_stats {
	unsigned long submitted;
	unsigned long completed_ok;
	unsigned long completed_failed;
	unsigned long unlinked;
	unsigned long queue_etd;
	unsigned long queue_dmem;
};

struct debug_isoc_trace {
	int schedule_frame;
	int submit_frame;
	int request_len;
	int done_frame;
	int done_len;
	int cc;
	struct td *td;
};
#endif

/* HCD data structure */
struct imx21 {
	spinlock_t lock;
	struct device *dev;
	struct usb_hcd *hcd;
	struct mx21_usbh_platform_data *pdata;
	struct list_head dmem_list;
	struct list_head queue_for_etd; /* eps queued due to etd shortage */
	struct list_head queue_for_dmem; /* etds queued due to dmem shortage */
	struct etd_priv etd[USB_NUM_ETD];
	struct clk *clk;
	void __iomem *regs;
#ifdef DEBUG
	struct dentry *debug_root;
	struct debug_stats nonisoc_stats;
	struct debug_stats isoc_stats;
	struct debug_usage_stats etd_usage;
	struct debug_usage_stats dmem_usage;
	struct debug_isoc_trace isoc_trace[20];
	struct debug_isoc_trace isoc_trace_failed[20];
	unsigned long debug_unblocks;
	int isoc_trace_index;
	int isoc_trace_index_failed;
#endif
};

#endif
