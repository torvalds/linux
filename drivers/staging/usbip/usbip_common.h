/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#ifndef __VHCI_COMMON_H
#define __VHCI_COMMON_H


#include <linux/version.h>
#include <linux/usb.h>
#include <asm/byteorder.h>
#include <net/sock.h>

/*-------------------------------------------------------------------------*/

/*
 * define macros to print messages
 */

/**
 * usbip_udbg - print debug messages if CONFIG_USB_IP_DEBUG_ENABLE is defined
 * @fmt:
 * @args:
 */

#ifdef CONFIG_USB_IP_DEBUG_ENABLE

#define usbip_udbg(fmt, args...)					\
	do {								\
		printk(KERN_DEBUG "%-10s:(%s,%d) %s: " fmt,		\
			(in_interrupt() ? "interrupt" : (current)->comm),\
			__FILE__, __LINE__, __func__, ##args);		\
	} while (0)

#else  /* CONFIG_USB_IP_DEBUG_ENABLE */

#define usbip_udbg(fmt, args...)		do { } while (0)

#endif /* CONFIG_USB_IP_DEBUG_ENABLE */


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
#define usbip_dbg_flag_vhci_sysfs   (usbip_debug_flag & usbip_debug_vhci_sysfs)

extern unsigned long usbip_debug_flag;
extern struct device_attribute dev_attr_usbip_debug;

#define usbip_dbg_with_flag(flag, fmt, args...)		\
	do {						\
		if (flag & usbip_debug_flag)		\
			usbip_udbg(fmt , ##args);		\
	} while (0)

#define usbip_dbg_sysfs(fmt, args...)		\
	usbip_dbg_with_flag(usbip_debug_sysfs, fmt , ##args)
#define usbip_dbg_xmit(fmt, args...)		\
	usbip_dbg_with_flag(usbip_debug_xmit, fmt , ##args)
#define usbip_dbg_urb(fmt, args...)		\
	usbip_dbg_with_flag(usbip_debug_urb, fmt , ##args)
#define usbip_dbg_eh(fmt, args...)		\
	usbip_dbg_with_flag(usbip_debug_eh, fmt , ##args)

#define usbip_dbg_vhci_rh(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_vhci_rh, fmt , ##args)
#define usbip_dbg_vhci_hc(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_vhci_hc, fmt , ##args)
#define usbip_dbg_vhci_rx(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_vhci_rx, fmt , ##args)
#define usbip_dbg_vhci_tx(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_vhci_tx, fmt , ##args)
#define usbip_dbg_vhci_sysfs(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_vhci_sysfs, fmt , ##args)

#define usbip_dbg_stub_cmp(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_stub_cmp, fmt , ##args)
#define usbip_dbg_stub_rx(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_stub_rx, fmt , ##args)
#define usbip_dbg_stub_tx(fmt, args...)	\
	usbip_dbg_with_flag(usbip_debug_stub_tx, fmt , ##args)


/**
 * usbip_uerr - print error messages
 * @fmt:
 * @args:
 */
#define usbip_uerr(fmt, args...)					\
	do {								\
		printk(KERN_ERR "%-10s: ***ERROR*** (%s,%d) %s: " fmt,	\
			(in_interrupt() ? "interrupt" : (current)->comm),\
			__FILE__, __LINE__, __func__, ##args);	\
	} while (0)

/**
 * usbip_uinfo - print information messages
 * @fmt:
 * @args:
 */
#define usbip_uinfo(fmt, args...)				\
	do {							\
		printk(KERN_INFO "usbip: " fmt , ## args);	\
	} while (0)


/*-------------------------------------------------------------------------*/

/*
 * USB/IP request headers.
 * Currently, we define 4 request types:
 *
 *  - CMD_SUBMIT transfers a USB request, corresponding to usb_submit_urb().
 *    (client to server)
 *  - RET_RETURN transfers the result of CMD_SUBMIT.
 *    (server to client)
 *  - CMD_UNLINK transfers an unlink request of a pending USB request.
 *    (client to server)
 *  - RET_UNLINK transfers the result of CMD_UNLINK.
 *    (server to client)
 *
 * Note: The below request formats are based on the USB subsystem of Linux. Its
 * details will be defined when other implementations come.
 *
 *
 */

/*
 * A basic header followed by other additional headers.
 */
struct usbip_header_basic {
#define USBIP_CMD_SUBMIT	0x0001
#define USBIP_CMD_UNLINK	0x0002
#define USBIP_RET_SUBMIT	0x0003
#define USBIP_RET_UNLINK	0x0004
	__u32 command;

	 /* sequencial number which identifies requests.
	  * incremented per connections */
	__u32 seqnum;

	/* devid is used to specify a remote USB device uniquely instead
	 * of busnum and devnum in Linux. In the case of Linux stub_driver,
	 * this value is ((busnum << 16) | devnum) */
	__u32 devid;

#define USBIP_DIR_OUT	0
#define USBIP_DIR_IN 	1
	__u32 direction;
	__u32 ep;     /* endpoint number */
} __attribute__ ((packed));

/*
 * An additional header for a CMD_SUBMIT packet.
 */
struct usbip_header_cmd_submit {
	/* these values are basically the same as in a URB. */

	/* the same in a URB. */
	__u32 transfer_flags;

	/* set the following data size (out),
	 * or expected reading data size (in) */
	__s32 transfer_buffer_length;

	/* it is difficult for usbip to sync frames (reserved only?) */
	__s32 start_frame;

	/* the number of iso descriptors that follows this header */
	__s32 number_of_packets;

	/* the maximum time within which this request works in a host
	 * controller of a server side */
	__s32 interval;

	/* set setup packet data for a CTRL request */
	unsigned char setup[8];
} __attribute__ ((packed));

/*
 * An additional header for a RET_SUBMIT packet.
 */
struct usbip_header_ret_submit {
	__s32 status;
	__s32 actual_length; /* returned data length */
	__s32 start_frame; /* ISO and INT */
	__s32 number_of_packets;  /* ISO only */
	__s32 error_count; /* ISO only */
} __attribute__ ((packed));

/*
 * An additional header for a CMD_UNLINK packet.
 */
struct usbip_header_cmd_unlink {
	__u32 seqnum; /* URB's seqnum which will be unlinked */
} __attribute__ ((packed));


/*
 * An additional header for a RET_UNLINK packet.
 */
struct usbip_header_ret_unlink {
	__s32 status;
} __attribute__ ((packed));


/* the same as usb_iso_packet_descriptor but packed for pdu */
struct usbip_iso_packet_descriptor {
	__u32 offset;
	__u32 length;            /* expected length */
	__u32 actual_length;
	__u32 status;
} __attribute__ ((packed));


/*
 * All usbip packets use a common header to keep code simple.
 */
struct usbip_header {
	struct usbip_header_basic base;

	union {
		struct usbip_header_cmd_submit	cmd_submit;
		struct usbip_header_ret_submit	ret_submit;
		struct usbip_header_cmd_unlink	cmd_unlink;
		struct usbip_header_ret_unlink	ret_unlink;
	} u;
} __attribute__ ((packed));




/*-------------------------------------------------------------------------*/


int usbip_xmit(int, struct socket *, char *, int, int);
int usbip_sendmsg(struct socket *, struct msghdr *, int);


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

static inline int interface_to_infnum(struct usb_interface *interface)
{
	return interface->cur_altsetting->desc.bInterfaceNumber;
}

#if 0
int setnodelay(struct socket *);
int setquickack(struct socket *);
int setkeepalive(struct socket *socket);
void setreuse(struct socket *);
#endif

struct socket *sockfd_to_socket(unsigned int);
int set_sockaddr(struct socket *socket, struct sockaddr_storage *ss);

void usbip_dump_urb(struct urb *purb);
void usbip_dump_header(struct usbip_header *pdu);


struct usbip_device;

struct usbip_task {
	struct task_struct *thread;
	struct completion thread_done;
	char *name;
	void (*loop_ops)(struct usbip_task *);
};

enum usbip_side {
	USBIP_VHCI,
	USBIP_STUB,
};

enum usbip_status {
	/* sdev is available. */
	SDEV_ST_AVAILABLE = 0x01,
	/* sdev is now used. */
	SDEV_ST_USED,
	/* sdev is unusable because of a fatal error. */
	SDEV_ST_ERROR,

	/* vdev does not connect a remote device. */
	VDEV_ST_NULL,
	/* vdev is used, but the USB address is not assigned yet */
	VDEV_ST_NOTASSIGNED,
	VDEV_ST_USED,
	VDEV_ST_ERROR
};

/* a common structure for stub_device and vhci_device */
struct usbip_device {
	enum usbip_side side;

	enum usbip_status status;

	/* lock for status */
	spinlock_t lock;

	struct socket *tcp_socket;

	struct usbip_task tcp_rx;
	struct usbip_task tcp_tx;

	/* event handler */
#define USBIP_EH_SHUTDOWN	(1 << 0)
#define USBIP_EH_BYE		(1 << 1)
#define USBIP_EH_RESET		(1 << 2)
#define USBIP_EH_UNUSABLE	(1 << 3)

#define SDEV_EVENT_REMOVED   (USBIP_EH_SHUTDOWN | USBIP_EH_RESET | USBIP_EH_BYE)
#define	SDEV_EVENT_DOWN		(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
#define	SDEV_EVENT_ERROR_TCP	(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
#define	SDEV_EVENT_ERROR_SUBMIT	(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
#define	SDEV_EVENT_ERROR_MALLOC	(USBIP_EH_SHUTDOWN | USBIP_EH_UNUSABLE)

#define	VDEV_EVENT_REMOVED	(USBIP_EH_SHUTDOWN | USBIP_EH_BYE)
#define	VDEV_EVENT_DOWN		(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
#define	VDEV_EVENT_ERROR_TCP	(USBIP_EH_SHUTDOWN | USBIP_EH_RESET)
#define	VDEV_EVENT_ERROR_MALLOC	(USBIP_EH_SHUTDOWN | USBIP_EH_UNUSABLE)

	unsigned long event;
	struct usbip_task eh;
	wait_queue_head_t eh_waitq;

	struct eh_ops {
		void (*shutdown)(struct usbip_device *);
		void (*reset)(struct usbip_device *);
		void (*unusable)(struct usbip_device *);
	} eh_ops;
};


void usbip_task_init(struct usbip_task *ut, char *,
				void (*loop_ops)(struct usbip_task *));

int usbip_start_threads(struct usbip_device *ud);
void usbip_stop_threads(struct usbip_device *ud);
int usbip_thread(void *param);

void usbip_pack_pdu(struct usbip_header *pdu, struct urb *urb, int cmd,
								int pack);

void usbip_header_correct_endian(struct usbip_header *pdu, int send);
/* some members of urb must be substituted before. */
int usbip_recv_xbuff(struct usbip_device *ud, struct urb *urb);
/* some members of urb must be substituted before. */
int usbip_recv_iso(struct usbip_device *ud, struct urb *urb);
void *usbip_alloc_iso_desc_pdu(struct urb *urb, ssize_t *bufflen);


/* usbip_event.c */
int usbip_start_eh(struct usbip_device *ud);
void usbip_stop_eh(struct usbip_device *ud);
void usbip_event_add(struct usbip_device *ud, unsigned long event);
int usbip_event_happened(struct usbip_device *ud);


#endif
