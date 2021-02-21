// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Krzysztof Opasiak <k.opasiak@samsung.com>
 */

#ifndef __USBIP_COMMON_H
#define __USBIP_COMMON_H

#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/net.h>
#include <linux/printk.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/sched/task.h>
#include <uapi/linux/usbip.h>

#undef pr_fmt

#ifdef DEBUG
#define pr_fmt(fmt)     KBUILD_MODNAME ": %s:%d: " fmt, __func__, __LINE__
#else
#define pr_fmt(fmt)     KBUILD_MODNAME ": " fmt
#endif

enum {
	usbip_debug_xmit	= (1 << 0),
	usbip_debug_sysfs	= (1 << 1),
	usbip_debug_urb		= (1 << 2),
	usbip_debug_eh		= (1 << 3),

	usbip_debug_stub_cmp	= (1 << 8),
	usbip_debug_stub_dev	= (1 << 9),
	usbip_debug_stub_rx	= (1 << 10),
	usbip_debug_stub_tx	= (1 << 11),

	usbip_debug_vhci_rh	= (1 << 8),
	usbip_debug_vhci_hc	= (1 << 9),
	usbip_debug_vhci_rx	= (1 << 10),
	usbip_debug_vhci_tx	= (1 << 11),
	usbip_debug_vhci_sysfs  = (1 << 12)
};

#define usbip_dbg_flag_xmit	(usbip_debug_flag & usbip_debug_xmit)
#define usbip_dbg_flag_vhci_rh	(usbip_debug_flag & usbip_debug_vhci_rh)
#define usbip_dbg_flag_vhci_hc	(usbip_debug_flag & usbip_debug_vhci_hc)
#define usbip_dbg_flag_vhci_rx	(usbip_debug_flag & usbip_debug_vhci_rx)
#define usbip_dbg_flag_vhci_tx	(usbip_debug_flag & usbip_debug_vhci_tx)
#define usbip_dbg_flag_stub_rx	(usbip_debug_flag & usbip_debug_stub_rx)
#define usbip_dbg_flag_stub_tx	(usbip_debug_flag & usbip_debug_stub_tx)
#define usbip_dbg_flag_vhci_sysfs  (usbip_debug_flag & usbip_debug_vhci_sysfs)

extern unsigned long usbip_debug_flag;
extern struct device_attribute dev_attr_usbip_debug;

#define usbip_dbg_with_flag(flag, fmt, args...)		\
	do {						\
		if (flag & usbip_debug_flag)		\
			pr_debug(fmt, ##args);		\
	} while (0)

#define usbip_dbg_sysfs(fmt, args...) \
	usbip_dbg_with_flag(usbip_debug_sysfs, fmt , ##args)
#define usbip_dbg_xmit(fmt, args...) \
	usbip_dbg_with_flag(usbip_debug_xmit, fmt , ##args)
#define usbip_dbg_urb(fmt, args...) \
	usbip_dbg_with_flag(usbip_debug_urb, fmt , ##args)
#define usbip_dbg_eh(fmt, args...) \
	usbip_dbg_with_flag(usbip_debug_eh, fmt , ##args)

#define usbip_dbg_vhci_rh(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_vhci_rh, fmt , ##args)
#define usbip_dbg_vhci_hc(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_vhci_hc, fmt , ##args)
#define usbip_dbg_vhci_rx(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_vhci_rx, fmt , ##args)
#define usbip_dbg_vhci_tx(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_vhci_tx, fmt , ##args)
#define usbip_dbg_vhci_sysfs(fmt, args...) \
	usbip_dbg_with_flag(usbip_debug_vhci_sysfs, fmt , ##args)

#define usbip_dbg_stub_cmp(fmt, args...) \
	usbip_dbg_with_flag(usbip_debug_stub_cmp, fmt , ##args)
#define usbip_dbg_stub_rx(fmt, args...) \
	usbip_dbg_with_flag(usbip_debug_stub_rx, fmt , ##args)
#define usbip_dbg_stub_tx(fmt, args...) \
	usbip_dbg_with_flag(usbip_debug_stub_tx, fmt , ##args)

/*
 * USB/IP request headers
 *
 * Each request is transferred across the network to its counterpart, which
 * facilitates the normal USB communication. The values contained in the headers
 * are basically the same as in a URB. Currently, four request types are
 * defined:
 *
 *  - USBIP_CMD_SUBMIT: a USB request block, corresponds to usb_submit_urb()
 *    (client to server)
 *
 *  - USBIP_RET_SUBMIT: the result of USBIP_CMD_SUBMIT
 *    (server to client)
 *
 *  - USBIP_CMD_UNLINK: an unlink request of a pending USBIP_CMD_SUBMIT,
 *    corresponds to usb_unlink_urb()
 *    (client to server)
 *
 *  - USBIP_RET_UNLINK: the result of USBIP_CMD_UNLINK
 *    (server to client)
 *
 */
#define USBIP_CMD_SUBMIT	0x0001
#define USBIP_CMD_UNLINK	0x0002
#define USBIP_RET_SUBMIT	0x0003
#define USBIP_RET_UNLINK	0x0004

#define USBIP_DIR_OUT	0x00
#define USBIP_DIR_IN	0x01

/*
 * Arbitrary limit for the maximum number of isochronous packets in an URB,
 * compare for example the uhci_submit_isochronous function in
 * drivers/usb/host/uhci-q.c
 */
#define USBIP_MAX_ISO_PACKETS 1024

/**
 * struct usbip_header_basic - data pertinent to every request
 * @command: the usbip request type
 * @seqnum: sequential number that identifies requests; incremented per
 *	    connection
 * @devid: specifies a remote USB device uniquely instead of busnum and devnum;
 *	   in the stub driver, this value is ((busnum << 16) | devnum)
 * @direction: direction of the transfer
 * @ep: endpoint number
 */
struct usbip_header_basic {
	__u32 command;
	__u32 seqnum;
	__u32 devid;
	__u32 direction;
	__u32 ep;
} __packed;

/**
 * struct usbip_header_cmd_submit - USBIP_CMD_SUBMIT packet header
 * @transfer_flags: URB flags
 * @transfer_buffer_length: the data size for (in) or (out) transfer
 * @start_frame: initial frame for isochronous or interrupt transfers
 * @number_of_packets: number of isochronous packets
 * @interval: maximum time for the request on the server-side host controller
 * @setup: setup data for a control request
 */
struct usbip_header_cmd_submit {
	__u32 transfer_flags;
	__s32 transfer_buffer_length;

	/* it is difficult for usbip to sync frames (reserved only?) */
	__s32 start_frame;
	__s32 number_of_packets;
	__s32 interval;

	unsigned char setup[8];
} __packed;

/**
 * struct usbip_header_ret_submit - USBIP_RET_SUBMIT packet header
 * @status: return status of a non-iso request
 * @actual_length: number of bytes transferred
 * @start_frame: initial frame for isochronous or interrupt transfers
 * @number_of_packets: number of isochronous packets
 * @error_count: number of errors for isochronous transfers
 */
struct usbip_header_ret_submit {
	__s32 status;
	__s32 actual_length;
	__s32 start_frame;
	__s32 number_of_packets;
	__s32 error_count;
} __packed;

/**
 * struct usbip_header_cmd_unlink - USBIP_CMD_UNLINK packet header
 * @seqnum: the URB seqnum to unlink
 */
struct usbip_header_cmd_unlink {
	__u32 seqnum;
} __packed;

/**
 * struct usbip_header_ret_unlink - USBIP_RET_UNLINK packet header
 * @status: return status of the request
 */
struct usbip_header_ret_unlink {
	__s32 status;
} __packed;

/**
 * struct usbip_header - common header for all usbip packets
 * @base: the basic header
 * @u: packet type dependent header
 */
struct usbip_header {
	struct usbip_header_basic base;

	union {
		struct usbip_header_cmd_submit	cmd_submit;
		struct usbip_header_ret_submit	ret_submit;
		struct usbip_header_cmd_unlink	cmd_unlink;
		struct usbip_header_ret_unlink	ret_unlink;
	} u;
} __packed;

/*
 * This is the same as usb_iso_packet_descriptor but packed for pdu.
 */
struct usbip_iso_packet_descriptor {
	__u32 offset;
	__u32 length;			/* expected length */
	__u32 actual_length;
	__u32 status;
} __packed;

enum usbip_side {
	USBIP_VHCI,
	USBIP_STUB,
	USBIP_VUDC,
};

/* event handler */
#define USBIP_EH_SHUTDOWN	(1 << 0)
#define USBIP_EH_BYE		(1 << 1)
#define USBIP_EH_RESET		(1 << 2)
#define USBIP_EH_UNUSABLE	(1 << 3)

#define	SDEV_EVENT_REMOVED	(USBIP_EH_SHUTDOWN | USBIP_EH_BYE)
#define	SDEV_EVENT_DOWN		(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
#define	SDEV_EVENT_ERROR_TCP	(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
#define	SDEV_EVENT_ERROR_SUBMIT	(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
#define	SDEV_EVENT_ERROR_MALLOC	(USBIP_EH_SHUTDOWN | USBIP_EH_UNUSABLE)

#define VUDC_EVENT_REMOVED   (USBIP_EH_SHUTDOWN | USBIP_EH_RESET | USBIP_EH_BYE)
#define	VUDC_EVENT_DOWN		(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
#define	VUDC_EVENT_ERROR_TCP	(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
/* catastrophic emulated usb error */
#define	VUDC_EVENT_ERROR_USB	(USBIP_EH_SHUTDOWN | USBIP_EH_UNUSABLE)
#define	VUDC_EVENT_ERROR_MALLOC	(USBIP_EH_SHUTDOWN | USBIP_EH_UNUSABLE)

#define	VDEV_EVENT_REMOVED (USBIP_EH_SHUTDOWN | USBIP_EH_RESET | USBIP_EH_BYE)
#define	VDEV_EVENT_DOWN		(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
#define	VDEV_EVENT_ERROR_TCP	(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
#define	VDEV_EVENT_ERROR_MALLOC	(USBIP_EH_SHUTDOWN | USBIP_EH_UNUSABLE)

/* a common structure for stub_device and vhci_device */
struct usbip_device {
	enum usbip_side side;
	enum usbip_device_status status;

	/* lock for status */
	spinlock_t lock;

	int sockfd;
	struct socket *tcp_socket;

	struct task_struct *tcp_rx;
	struct task_struct *tcp_tx;

	unsigned long event;
	wait_queue_head_t eh_waitq;

	struct eh_ops {
		void (*shutdown)(struct usbip_device *);
		void (*reset)(struct usbip_device *);
		void (*unusable)(struct usbip_device *);
	} eh_ops;

#ifdef CONFIG_KCOV
	u64 kcov_handle;
#endif
};

#define kthread_get_run(threadfn, data, namefmt, ...)			   \
({									   \
	struct task_struct *__k						   \
		= kthread_create(threadfn, data, namefmt, ## __VA_ARGS__); \
	if (!IS_ERR(__k)) {						   \
		get_task_struct(__k);					   \
		wake_up_process(__k);					   \
	}								   \
	__k;								   \
})

#define kthread_stop_put(k)		\
	do {				\
		kthread_stop(k);	\
		put_task_struct(k);	\
	} while (0)

/* usbip_common.c */
void usbip_dump_urb(struct urb *purb);
void usbip_dump_header(struct usbip_header *pdu);

int usbip_recv(struct socket *sock, void *buf, int size);

void usbip_pack_pdu(struct usbip_header *pdu, struct urb *urb, int cmd,
		    int pack);
void usbip_header_correct_endian(struct usbip_header *pdu, int send);

struct usbip_iso_packet_descriptor*
usbip_alloc_iso_desc_pdu(struct urb *urb, ssize_t *bufflen);

/* some members of urb must be substituted before. */
int usbip_recv_iso(struct usbip_device *ud, struct urb *urb);
void usbip_pad_iso(struct usbip_device *ud, struct urb *urb);
int usbip_recv_xbuff(struct usbip_device *ud, struct urb *urb);

/* usbip_event.c */
int usbip_init_eh(void);
void usbip_finish_eh(void);
int usbip_start_eh(struct usbip_device *ud);
void usbip_stop_eh(struct usbip_device *ud);
void usbip_event_add(struct usbip_device *ud, unsigned long event);
int usbip_event_happened(struct usbip_device *ud);
int usbip_in_eh(struct task_struct *task);

static inline int interface_to_busnum(struct usb_interface *interface)
{
	struct usb_device *udev = interface_to_usbdev(interface);

	return udev->bus->busnum;
}

static inline int interface_to_devnum(struct usb_interface *interface)
{
	struct usb_device *udev = interface_to_usbdev(interface);

	return udev->devnum;
}

#ifdef CONFIG_KCOV

static inline void usbip_kcov_handle_init(struct usbip_device *ud)
{
	ud->kcov_handle = kcov_common_handle();
}

static inline void usbip_kcov_remote_start(struct usbip_device *ud)
{
	kcov_remote_start_common(ud->kcov_handle);
}

static inline void usbip_kcov_remote_stop(void)
{
	kcov_remote_stop();
}

#else /* CONFIG_KCOV */

static inline void usbip_kcov_handle_init(struct usbip_device *ud) { }
static inline void usbip_kcov_remote_start(struct usbip_device *ud) { }
static inline void usbip_kcov_remote_stop(void) { }

#endif /* CONFIG_KCOV */

#endif /* __USBIP_COMMON_H */
