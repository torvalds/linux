/**
 ******************************************************************************
 *
 * @file usb.h
 *
 * @brief usb driver definitions
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef __USB_H
#define __USB_H

#include "ecrnx_defs.h"
#include <linux/usb.h>

#define USB_INFAC_DATA		0
#define USB_INFAC_MSG		1

#define USB_DIR_MASK	0x80
#define USB_NUM_MASK    0x7F

#define USB_DATA_URB_NUM 64
#define USB_MSG_URB_NUM	16

#define USB_DIR_RX		0
#define USB_DIR_TX		1

#define USB_RX_MAX_BUF_SIZE 	4096

struct usb_infac_pipe {
	int dir;
	u32 urb_cnt;
	struct usb_infac_data_t *infac;

	struct usb_anchor urb_submitted;
	unsigned int usb_pipe_handle;
	struct list_head urb_list_head;
	#ifdef CONFIG_ECRNX_WORKQUEUE
	struct work_struct io_complete_work;
	#endif
	#ifdef CONFIG_ECRNX_TASKLET
	struct tasklet_struct tx_tasklet;
	struct tasklet_struct rx_tasklet;
	#endif
	struct sk_buff_head io_comp_queue;
    unsigned int err_count;
    int err_status;
};


struct usb_infac_data_t {
	struct usb_interface *interface;
	struct usb_device *udev;
	u16 max_packet_size;

	struct usb_infac_pipe pipe_rx;
	struct usb_infac_pipe pipe_tx;
};

struct eswin_usb {
	struct eswin * p_eswin;
	struct device * dev;
	struct usb_infac_data_t infac_data;
	struct usb_infac_data_t infac_msg;
	#ifdef CONFIG_ECRNX_KTHREAD
	struct task_struct *kthread_tx_comp;
	struct task_struct *kthread_rx_comp;

	wait_queue_head_t wait_tx_comp;
	wait_queue_head_t wait_rx_comp;
	#endif
	spinlock_t cs_lock;
	u8 usb_enum_already;
};

struct usb_urb_context {
	struct list_head link;
	struct sk_buff *skb;
    struct urb *urb;
	struct usb_infac_pipe * pipe;
};


struct usb_ops {
	int (*start)(struct eswin *tr);
	int (*xmit)(struct eswin *tr, struct sk_buff *skb);
	int (*suspend)(struct eswin *tr);
	int (*resume)(struct eswin *tr);
	int (*write)(struct eswin *tr, const void* data, const u32 len);
	int (*wait_ack)(struct eswin *tr, void* data, const u32 len);
};



extern int ecrnx_usb_register_drv(void);
extern void ecrnx_usb_unregister_drv(void);

#endif /* __SDIO_H */
