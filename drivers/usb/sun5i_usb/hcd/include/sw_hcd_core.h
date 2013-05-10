/*
 * drivers/usb/sun5i_usb/hcd/include/sw_hcd_core.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef  __SW_HCD_CORE_H__
#define  __SW_HCD_CORE_H__

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/usb/ch9.h>

#include <plat/dma.h>
#include <linux/dma-mapping.h>

#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include  "sw_hcd_config.h"

//---------------------------------------------------------------
//  预 定义
//---------------------------------------------------------------
struct sw_hcd;
struct sw_hcd_hw_ep;

#include  "sw_hcd_regs_i.h"

#include  "sw_hcd_board.h"
#include  "sw_hcd_host.h"
#include  "sw_hcd_virt_hub.h"
#include  "sw_hcd_dma.h"




//---------------------------------------------------------------
//  宏 定义
//---------------------------------------------------------------

#define is_host_active(m)		((m)->is_host)



//---------------------------------------------------------------
//  数据结构 定义
//---------------------------------------------------------------

#define	is_host_capable()			(1)
#define SW_HCD_C_NUM_EPS      		USBC_MAX_EP_NUM
#define	is_host_enabled(sw_usb)		is_host_capable()

/* host side ep0 states */
enum sw_hcd_h_ep0_state {
	SW_HCD_EP0_IDLE,
	SW_HCD_EP0_START,			/* expect ack of setup */
	SW_HCD_EP0_IN,			/* expect IN DATA */
	SW_HCD_EP0_OUT,			/* expect ack of OUT DATA */
	SW_HCD_EP0_STATUS,		/* expect ack of STATUS */
}__attribute__ ((packed));


/*
 * struct sw_hcd_hw_ep - endpoint hardware (bidirectional)
 *
 * Ordered slightly for better cacheline locality.
 */
typedef struct sw_hcd_hw_ep{
	struct sw_hcd *sw_hcd;              /* ep 的宿主                    */
	void __iomem *fifo;             /* fifo的基址                   */
	void __iomem *regs;             /* USB 控制器基址               */

	u8 epnum;      	                /* index in sw_hcd->endpoints[]   */

	/* hardware configuration, possibly dynamic */
	bool is_shared_fifo;            /* 是否共享 fifo                */
	bool tx_double_buffered;        /* Flag. 是否是双fifo?          */
	bool rx_double_buffered;        /* Flag. 是否是双fifo?          */
	u16 max_packet_sz_tx;           /* 最长包大小                   */
	u16 max_packet_sz_rx;           /* 最长包大小                   */

	void __iomem *target_regs;      /* hub 上的目标设备的地址       */

	/* currently scheduled peripheral endpoint */
	struct sw_hcd_qh *in_qh;          /* 存放 in ep 的调度信息        */
	struct sw_hcd_qh *out_qh;         /* 存放 out ep 的调度信息       */

	u8 rx_reinit;                   /* flag. 是否重新初始化         */
	u8 tx_reinit;                   /* flag. 是否重新初始化         */
}sw_hcd_hw_ep_t;


/*
 * struct sw_hcd - Driver instance data.
 */
typedef struct sw_hcd{
    /* device lock */
	spinlock_t lock;                    /* 互斥锁       */
	irqreturn_t (*isr)(int, void *);    /*  */
	struct work_struct irq_work;        /*  */

	char driver_name[32];
	__u32 usbc_no;

/* this hub status bit is reserved by USB 2.0 and not seen by usbcore */
#define SW_HCD_PORT_STAT_RESUME	(1 << 31)
	u32 port1_status;                   /* 虚拟 hub 的端口状态  */
	unsigned long rh_timer;             /* root hub 的delay时间 */

	enum sw_hcd_h_ep0_state ep0_stage;    /* ep0 的状态           */

	/* bulk traffic normally dedicates endpoint hardware, and each
	 * direction has its own ring of host side endpoints.
	 * we try to progress the transfer at the head of each endpoint's
	 * queue until it completes or NAKs too much; then we try the next
	 * endpoint.
	 */
	struct sw_hcd_hw_ep *bulk_ep;

	struct list_head control;	        /* of sw_hcd_qh           */
	struct list_head in_bulk;	        /* of sw_hcd_qh           */
	struct list_head out_bulk;	        /* of sw_hcd_qh           */

    /* called with IRQs blocked; ON/nonzero implies starting a session,
	 * and waiting at least a_wait_vrise_tmout.
	 */
	void (*board_set_vbus)(struct sw_hcd *, int is_on);

	sw_hcd_dma_t sw_hcd_dma;

	struct device *controller;          /*  */
	void __iomem *ctrl_base;            /* USB 控制器基址       */
	void __iomem *mregs;                /* USB 控制器基址       */

	/* passed down from chip/board specific irq handlers */
	u8 int_usb;                         /* USB 中断             */
	u16 int_rx;                         /* rx 中断              */
	u16 int_tx;                         /* tx 中断              */

	int nIrq;                           /* 中断号               */
	unsigned irq_wake:1;                /* flag. 中断使能标志   */

	struct sw_hcd_hw_ep endpoints[SW_HCD_C_NUM_EPS];    /* sw_hcd 所有 ep 的信息 */
#define control_ep endpoints

#define VBUSERR_RETRY_COUNT	3
	u16 vbuserr_retry;                  /* vbus error 后，host retry的次数  */
	u16 epmask;                         /* ep掩码，bitn = 1, 表示 epn 有效  */
	u8 nr_endpoints;                    /* 有效 ep 的个数                   */

	u8 board_mode;		                /* enum sw_hcd_mode                   */

	int (*board_set_power)(int state);
	int (*set_clock)(struct clk *clk, int is_active);

	u8 min_power;	                    /* vbus for periph, in mA/2         */

	bool is_host;                       /* flag. 是否是 host 传输标志       */
	int a_wait_bcon;	                /* VBUS timeout in msecs            */
	unsigned long idle_timeout;	        /* Next timeout in jiffies          */

	/* active means connected and not suspended */
	unsigned is_active:1;

	unsigned is_multipoint:1;           /* flag. is multiple transaction ep? */
	unsigned ignore_disconnect:1;	    /* during bus resets                */

	unsigned bulk_split:1;
#define	can_bulk_split(sw_usb, type)       (((type) == USB_ENDPOINT_XFER_BULK) && (sw_usb)->bulk_split)

	unsigned bulk_combine:1;
#define	can_bulk_combine(sw_usb, type)     (((type) == USB_ENDPOINT_XFER_BULK) && (sw_usb)->bulk_combine)

	struct sw_hcd_config	*config;        /* sw_hcd 的配置信息                  */

	sw_hcd_io_t	*sw_hcd_io;
	u32 enable;
	u32 suspend;
}sw_hcd_t;

struct sw_hcd_ep_reg{
	__u32 USB_CSR0;
	__u32 USB_TXCSR;
	__u32 USB_RXCSR;
	__u32 USB_COUNT0;
	__u32 USB_RXCOUNT;
	__u32 USB_ATTR0;
	__u32 USB_EPATTR;
	__u32 USB_TXFIFO;
	__u32 USB_RXFIFO;
	__u32 USB_FADDR;
	__u32 USB_TXFADDR;
	__u32 USB_RXFADDR;
};

struct sw_hcd_context_registers {
	/* FIFO Entry for Endpoints */
	__u32 USB_EPFIFO0;
	__u32 USB_EPFIFO1;
	__u32 USB_EPFIFO2;
	__u32 USB_EPFIFO3;
	__u32 USB_EPFIFO4;
	__u32 USB_EPFIFO5;

	/* Common Register */
	__u32 USB_GCS;
	__u32 USB_EPINTF;
	__u32 USB_EPINTE;
	__u32 USB_BUSINTF;
	__u32 USB_BUSINTE;
	__u32 USB_FNUM;
	__u32 USB_TESTC;

	/* Endpoint Index Register */
	struct sw_hcd_ep_reg ep_reg[SW_HCD_C_NUM_EPS];

	/* Configuration Register */
	__u32 USB_CONFIGINFO;
	__u32 USB_LINKTIM;
	__u32 USB_OTGTIM;

	/* PHY and Interface Control and Status Register */
	__u32 USB_ISCR;
	__u32 USB_PHYCTL;
	__u32 USB_PHYBIST;
};

//---------------------------------------------------------------
//
//---------------------------------------------------------------

static inline struct sw_hcd *dev_to_sw_hcd(struct device *dev)
{
	/* usbcore insists dev->driver_data is a "struct hcd *" */
	return hcd_to_sw_hcd(dev_get_drvdata(dev));
}


/* vbus 操作 */
static inline void sw_hcd_set_vbus(struct sw_hcd *sw_hcd, int is_on)
{
	if(sw_hcd->board_set_vbus){
		sw_hcd->board_set_vbus(sw_hcd, is_on);
	}
}

/* 读取 fifo 的大小 */
static inline int sw_hcd_read_fifosize(struct sw_hcd *sw_hcd, struct sw_hcd_hw_ep *hw_ep, u8 epnum)
{
	void *xbase = sw_hcd->mregs;
	u8 reg = 0;

	/* read from core using indexed model */
	reg = USBC_Readb(USBC_REG_TXFIFOSZ(xbase));
	/* 0's returned when no more endpoints */
	if (!reg){
	    return -ENODEV;
	}
	hw_ep->max_packet_sz_tx = 1 << (reg & 0x0f);

	sw_hcd->nr_endpoints++;
	sw_hcd->epmask |= (1 << epnum);

	/* read from core using indexed model */
	reg = USBC_Readb(USBC_REG_RXFIFOSZ(xbase));
	/* 0's returned when no more endpoints */
	if (!reg){
	    return -ENODEV;
	}
	/* shared TX/RX FIFO? */
	if ((reg & 0xf0) == 0xf0) {
		hw_ep->max_packet_sz_rx = hw_ep->max_packet_sz_tx;
		hw_ep->is_shared_fifo = true;
		return 0;
	} else {
		hw_ep->max_packet_sz_rx = 1 << ((reg & 0xf0) >> 4);
		hw_ep->is_shared_fifo = false;
	}

	return 0;
}

/* 配置 ep0 */
static inline void sw_hcd_configure_ep0(struct sw_hcd *sw_hcd)
{
	sw_hcd->endpoints[0].max_packet_sz_tx = USBC_EP0_FIFOSIZE;
	sw_hcd->endpoints[0].max_packet_sz_rx = USBC_EP0_FIFOSIZE;
	sw_hcd->endpoints[0].is_shared_fifo = true;
}

#define  SW_HCD_HST_MODE(sw_hcd) 	{ (sw_hcd)->is_host = true; }
#define  is_direction_in(qh)		(qh->hep->desc.bEndpointAddress & USB_ENDPOINT_DIR_MASK)

//---------------------------------------------------------------
//  函数 定义
//---------------------------------------------------------------
void sw_hcd_write_fifo(struct sw_hcd_hw_ep *hw_ep, u16 len, const u8 *src);
void sw_hcd_read_fifo(struct sw_hcd_hw_ep *hw_ep, u16 len, u8 *dst);
void sw_hcd_load_testpacket(struct sw_hcd *sw_hcd);
void sw_hcd_generic_disable(struct sw_hcd *sw_hcd);

irqreturn_t generic_interrupt(int irq, void *__hci);

void sw_hcd_soft_disconnect(struct sw_hcd *sw_hcd);
void sw_hcd_start(struct sw_hcd *sw_hcd);
void sw_hcd_stop(struct sw_hcd *sw_hcd);


void sw_hcd_platform_try_idle(struct sw_hcd *sw_hcd, unsigned long timeout);
void sw_hcd_platform_enable(struct sw_hcd *sw_hcd);
void sw_hcd_platform_disable(struct sw_hcd *sw_hcd);
int sw_hcd_platform_set_mode(struct sw_hcd *sw_hcd, u8 sw_hcd_mode);
int sw_hcd_platform_init(struct sw_hcd *sw_hcd);
int sw_hcd_platform_exit(struct sw_hcd *sw_hcd);
int sw_hcd_platform_suspend(struct sw_hcd *sw_hcd);
int sw_hcd_platform_resume(struct sw_hcd *sw_hcd);

#endif   //__SW_HCD_CORE_H__

