/*
 * mos7720.c
 *   Controls the Moschip 7720 usb to dual port serial convertor
 *
 * Copyright 2006 Moschip Semiconductor Tech. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Developed by:
 * 	Vijaya Kumar <vijaykumar.gn@gmail.com>
 *	Ajay Kumar <naanuajay@yahoo.com>
 *	Gurudeva <ngurudeva@yahoo.com>
 *
 * Cleaned up from the original by:
 *	Greg Kroah-Hartman <gregkh@suse.de>
 *
 * Originally based on drivers/usb/serial/io_edgeport.c which is:
 *	Copyright (C) 2000 Inside Out Networks, All rights reserved.
 *	Copyright (C) 2001-2002 Greg Kroah-Hartman <greg@kroah.com>
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>
#include <linux/parport.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "2.1"
#define DRIVER_AUTHOR "Aspire Communications pvt Ltd."
#define DRIVER_DESC "Moschip USB Serial Driver"

/* default urb timeout */
#define MOS_WDR_TIMEOUT	5000

#define MOS_MAX_PORT	0x02
#define MOS_WRITE	0x0E
#define MOS_READ	0x0D

/* Interrupt Rotinue Defines	*/
#define SERIAL_IIR_RLS	0x06
#define SERIAL_IIR_RDA	0x04
#define SERIAL_IIR_CTI	0x0c
#define SERIAL_IIR_THR	0x02
#define SERIAL_IIR_MS	0x00

#define NUM_URBS			16	/* URB Count */
#define URB_TRANSFER_BUFFER_SIZE	32	/* URB Size */

/* This structure holds all of the local serial port information */
struct moschip_port {
	__u8	shadowLCR;		/* last LCR value received */
	__u8	shadowMCR;		/* last MCR value received */
	__u8	shadowMSR;		/* last MSR value received */
	char			open;
	struct async_icount	icount;
	struct usb_serial_port	*port;	/* loop back to the owner */
	struct urb		*write_urb_pool[NUM_URBS];
};

static bool debug;

static struct usb_serial_driver moschip7720_2port_driver;

#define USB_VENDOR_ID_MOSCHIP		0x9710
#define MOSCHIP_DEVICE_ID_7720		0x7720
#define MOSCHIP_DEVICE_ID_7715		0x7715

static const struct usb_device_id moschip_port_id_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_MOSCHIP, MOSCHIP_DEVICE_ID_7720) },
	{ USB_DEVICE(USB_VENDOR_ID_MOSCHIP, MOSCHIP_DEVICE_ID_7715) },
	{ } /* terminating entry */
};
MODULE_DEVICE_TABLE(usb, moschip_port_id_table);

#ifdef CONFIG_USB_SERIAL_MOS7715_PARPORT

/* initial values for parport regs */
#define DCR_INIT_VAL       0x0c	/* SLCTIN, nINIT */
#define ECR_INIT_VAL       0x00	/* SPP mode */

struct urbtracker {
	struct mos7715_parport  *mos_parport;
	struct list_head        urblist_entry;
	struct kref             ref_count;
	struct urb              *urb;
};

enum mos7715_pp_modes {
	SPP = 0<<5,
	PS2 = 1<<5,      /* moschip calls this 'NIBBLE' mode */
	PPF = 2<<5,	 /* moschip calls this 'CB-FIFO mode */
};

struct mos7715_parport {
	struct parport          *pp;	       /* back to containing struct */
	struct kref             ref_count;     /* to instance of this struct */
	struct list_head        deferred_urbs; /* list deferred async urbs */
	struct list_head        active_urbs;   /* list async urbs in flight */
	spinlock_t              listlock;      /* protects list access */
	bool                    msg_pending;   /* usb sync call pending */
	struct completion       syncmsg_compl; /* usb sync call completed */
	struct tasklet_struct   urb_tasklet;   /* for sending deferred urbs */
	struct usb_serial       *serial;       /* back to containing struct */
	__u8	                shadowECR;     /* parallel port regs... */
	__u8	                shadowDCR;
	atomic_t                shadowDSR;     /* updated in int-in callback */
};

/* lock guards against dereferencing NULL ptr in parport ops callbacks */
static DEFINE_SPINLOCK(release_lock);

#endif	/* CONFIG_USB_SERIAL_MOS7715_PARPORT */

static const unsigned int dummy; /* for clarity in register access fns */

enum mos_regs {
	THR,	          /* serial port regs */
	RHR,
	IER,
	FCR,
	ISR,
	LCR,
	MCR,
	LSR,
	MSR,
	SPR,
	DLL,
	DLM,
	DPR,              /* parallel port regs */
	DSR,
	DCR,
	ECR,
	SP1_REG,          /* device control regs */
	SP2_REG,          /* serial port 2 (7720 only) */
	PP_REG,
	SP_CONTROL_REG,
};

/*
 * Return the correct value for the Windex field of the setup packet
 * for a control endpoint message.  See the 7715 datasheet.
 */
static inline __u16 get_reg_index(enum mos_regs reg)
{
	static const __u16 mos7715_index_lookup_table[] = {
		0x00,		/* THR */
		0x00,		/* RHR */
		0x01,		/* IER */
		0x02,		/* FCR */
		0x02,		/* ISR */
		0x03,		/* LCR */
		0x04,		/* MCR */
		0x05,		/* LSR */
		0x06,		/* MSR */
		0x07,		/* SPR */
		0x00,		/* DLL */
		0x01,		/* DLM */
		0x00,		/* DPR */
		0x01,		/* DSR */
		0x02,		/* DCR */
		0x0a,		/* ECR */
		0x01,		/* SP1_REG */
		0x02,		/* SP2_REG (7720 only) */
		0x04,		/* PP_REG (7715 only) */
		0x08,		/* SP_CONTROL_REG */
	};
	return mos7715_index_lookup_table[reg];
}

/*
 * Return the correct value for the upper byte of the Wvalue field of
 * the setup packet for a control endpoint message.
 */
static inline __u16 get_reg_value(enum mos_regs reg,
				  unsigned int serial_portnum)
{
	if (reg >= SP1_REG)	      /* control reg */
		return 0x0000;

	else if (reg >= DPR)	      /* parallel port reg (7715 only) */
		return 0x0100;

	else			      /* serial port reg */
		return (serial_portnum + 2) << 8;
}

/*
 * Write data byte to the specified device register.  The data is embedded in
 * the value field of the setup packet. serial_portnum is ignored for registers
 * not specific to a particular serial port.
 */
static int write_mos_reg(struct usb_serial *serial, unsigned int serial_portnum,
			 enum mos_regs reg, __u8 data)
{
	struct usb_device *usbdev = serial->dev;
	unsigned int pipe = usb_sndctrlpipe(usbdev, 0);
	__u8 request = (__u8)0x0e;
	__u8 requesttype = (__u8)0x40;
	__u16 index = get_reg_index(reg);
	__u16 value = get_reg_value(reg, serial_portnum) + data;
	int status = usb_control_msg(usbdev, pipe, request, requesttype, value,
				     index, NULL, 0, MOS_WDR_TIMEOUT);
	if (status < 0)
		dev_err(&usbdev->dev,
			"mos7720: usb_control_msg() failed: %d", status);
	return status;
}

/*
 * Read data byte from the specified device register.  The data returned by the
 * device is embedded in the value field of the setup packet.  serial_portnum is
 * ignored for registers that are not specific to a particular serial port.
 */
static int read_mos_reg(struct usb_serial *serial, unsigned int serial_portnum,
			enum mos_regs reg, __u8 *data)
{
	struct usb_device *usbdev = serial->dev;
	unsigned int pipe = usb_rcvctrlpipe(usbdev, 0);
	__u8 request = (__u8)0x0d;
	__u8 requesttype = (__u8)0xc0;
	__u16 index = get_reg_index(reg);
	__u16 value = get_reg_value(reg, serial_portnum);
	u8 *buf;
	int status;

	buf = kmalloc(1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	status = usb_control_msg(usbdev, pipe, request, requesttype, value,
				     index, buf, 1, MOS_WDR_TIMEOUT);
	if (status == 1)
		*data = *buf;
	else if (status < 0)
		dev_err(&usbdev->dev,
			"mos7720: usb_control_msg() failed: %d", status);
	kfree(buf);

	return status;
}

#ifdef CONFIG_USB_SERIAL_MOS7715_PARPORT

static inline int mos7715_change_mode(struct mos7715_parport *mos_parport,
				      enum mos7715_pp_modes mode)
{
	mos_parport->shadowECR = mode;
	write_mos_reg(mos_parport->serial, dummy, ECR, mos_parport->shadowECR);
	return 0;
}

static void destroy_mos_parport(struct kref *kref)
{
	struct mos7715_parport *mos_parport =
		container_of(kref, struct mos7715_parport, ref_count);

	dbg("%s called", __func__);
	kfree(mos_parport);
}

static void destroy_urbtracker(struct kref *kref)
{
	struct urbtracker *urbtrack =
		container_of(kref, struct urbtracker, ref_count);
	struct mos7715_parport *mos_parport = urbtrack->mos_parport;
	dbg("%s called", __func__);
	usb_free_urb(urbtrack->urb);
	kfree(urbtrack);
	kref_put(&mos_parport->ref_count, destroy_mos_parport);
}

/*
 * This runs as a tasklet when sending an urb in a non-blocking parallel
 * port callback had to be deferred because the disconnect mutex could not be
 * obtained at the time.
 */
static void send_deferred_urbs(unsigned long _mos_parport)
{
	int ret_val;
	unsigned long flags;
	struct mos7715_parport *mos_parport = (void *)_mos_parport;
	struct urbtracker *urbtrack;
	struct list_head *cursor, *next;

	dbg("%s called", __func__);

	/* if release function ran, game over */
	if (unlikely(mos_parport->serial == NULL))
		return;

	/* try again to get the mutex */
	if (!mutex_trylock(&mos_parport->serial->disc_mutex)) {
		dbg("%s: rescheduling tasklet", __func__);
		tasklet_schedule(&mos_parport->urb_tasklet);
		return;
	}

	/* if device disconnected, game over */
	if (unlikely(mos_parport->serial->disconnected)) {
		mutex_unlock(&mos_parport->serial->disc_mutex);
		return;
	}

	spin_lock_irqsave(&mos_parport->listlock, flags);
	if (list_empty(&mos_parport->deferred_urbs)) {
		spin_unlock_irqrestore(&mos_parport->listlock, flags);
		mutex_unlock(&mos_parport->serial->disc_mutex);
		dbg("%s: deferred_urbs list empty", __func__);
		return;
	}

	/* move contents of deferred_urbs list to active_urbs list and submit */
	list_for_each_safe(cursor, next, &mos_parport->deferred_urbs)
		list_move_tail(cursor, &mos_parport->active_urbs);
	list_for_each_entry(urbtrack, &mos_parport->active_urbs,
			    urblist_entry) {
		ret_val = usb_submit_urb(urbtrack->urb, GFP_ATOMIC);
		dbg("%s: urb submitted", __func__);
		if (ret_val) {
			dev_err(&mos_parport->serial->dev->dev,
				"usb_submit_urb() failed: %d", ret_val);
			list_del(&urbtrack->urblist_entry);
			kref_put(&urbtrack->ref_count, destroy_urbtracker);
		}
	}
	spin_unlock_irqrestore(&mos_parport->listlock, flags);
	mutex_unlock(&mos_parport->serial->disc_mutex);
}

/* callback for parallel port control urbs submitted asynchronously */
static void async_complete(struct urb *urb)
{
	struct urbtracker *urbtrack = urb->context;
	int status = urb->status;
	dbg("%s called", __func__);
	if (unlikely(status))
		dbg("%s - nonzero urb status received: %d", __func__, status);

	/* remove the urbtracker from the active_urbs list */
	spin_lock(&urbtrack->mos_parport->listlock);
	list_del(&urbtrack->urblist_entry);
	spin_unlock(&urbtrack->mos_parport->listlock);
	kref_put(&urbtrack->ref_count, destroy_urbtracker);
}

static int write_parport_reg_nonblock(struct mos7715_parport *mos_parport,
				      enum mos_regs reg, __u8 data)
{
	struct urbtracker *urbtrack;
	int ret_val;
	unsigned long flags;
	struct usb_ctrlrequest setup;
	struct usb_serial *serial = mos_parport->serial;
	struct usb_device *usbdev = serial->dev;
	dbg("%s called", __func__);

	/* create and initialize the control urb and containing urbtracker */
	urbtrack = kmalloc(sizeof(struct urbtracker), GFP_ATOMIC);
	if (urbtrack == NULL) {
		dev_err(&usbdev->dev, "out of memory");
		return -ENOMEM;
	}
	kref_get(&mos_parport->ref_count);
	urbtrack->mos_parport = mos_parport;
	urbtrack->urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (urbtrack->urb == NULL) {
		dev_err(&usbdev->dev, "out of urbs");
		kfree(urbtrack);
		return -ENOMEM;
	}
	setup.bRequestType = (__u8)0x40;
	setup.bRequest = (__u8)0x0e;
	setup.wValue = get_reg_value(reg, dummy);
	setup.wIndex = get_reg_index(reg);
	setup.wLength = 0;
	usb_fill_control_urb(urbtrack->urb, usbdev,
			     usb_sndctrlpipe(usbdev, 0),
			     (unsigned char *)&setup,
			     NULL, 0, async_complete, urbtrack);
	kref_init(&urbtrack->ref_count);
	INIT_LIST_HEAD(&urbtrack->urblist_entry);

	/*
	 * get the disconnect mutex, or add tracker to the deferred_urbs list
	 * and schedule a tasklet to try again later
	 */
	if (!mutex_trylock(&serial->disc_mutex)) {
		spin_lock_irqsave(&mos_parport->listlock, flags);
		list_add_tail(&urbtrack->urblist_entry,
			      &mos_parport->deferred_urbs);
		spin_unlock_irqrestore(&mos_parport->listlock, flags);
		tasklet_schedule(&mos_parport->urb_tasklet);
		dbg("tasklet scheduled");
		return 0;
	}

	/* bail if device disconnected */
	if (serial->disconnected) {
		kref_put(&urbtrack->ref_count, destroy_urbtracker);
		mutex_unlock(&serial->disc_mutex);
		return -ENODEV;
	}

	/* add the tracker to the active_urbs list and submit */
	spin_lock_irqsave(&mos_parport->listlock, flags);
	list_add_tail(&urbtrack->urblist_entry, &mos_parport->active_urbs);
	spin_unlock_irqrestore(&mos_parport->listlock, flags);
	ret_val = usb_submit_urb(urbtrack->urb, GFP_ATOMIC);
	mutex_unlock(&serial->disc_mutex);
	if (ret_val) {
		dev_err(&usbdev->dev,
			"%s: submit_urb() failed: %d", __func__, ret_val);
		spin_lock_irqsave(&mos_parport->listlock, flags);
		list_del(&urbtrack->urblist_entry);
		spin_unlock_irqrestore(&mos_parport->listlock, flags);
		kref_put(&urbtrack->ref_count, destroy_urbtracker);
		return ret_val;
	}
	return 0;
}

/*
 * This is the the common top part of all parallel port callback operations that
 * send synchronous messages to the device.  This implements convoluted locking
 * that avoids two scenarios: (1) a port operation is called after usbserial
 * has called our release function, at which point struct mos7715_parport has
 * been destroyed, and (2) the device has been disconnected, but usbserial has
 * not called the release function yet because someone has a serial port open.
 * The shared release_lock prevents the first, and the mutex and disconnected
 * flag maintained by usbserial covers the second.  We also use the msg_pending
 * flag to ensure that all synchronous usb messgage calls have completed before
 * our release function can return.
 */
static int parport_prologue(struct parport *pp)
{
	struct mos7715_parport *mos_parport;

	spin_lock(&release_lock);
	mos_parport = pp->private_data;
	if (unlikely(mos_parport == NULL)) {
		/* release fn called, port struct destroyed */
		spin_unlock(&release_lock);
		return -1;
	}
	mos_parport->msg_pending = true;   /* synch usb call pending */
	INIT_COMPLETION(mos_parport->syncmsg_compl);
	spin_unlock(&release_lock);

	mutex_lock(&mos_parport->serial->disc_mutex);
	if (mos_parport->serial->disconnected) {
		/* device disconnected */
		mutex_unlock(&mos_parport->serial->disc_mutex);
		mos_parport->msg_pending = false;
		complete(&mos_parport->syncmsg_compl);
		return -1;
	}

	return 0;
}

/*
 * This is the the common bottom part of all parallel port functions that send
 * synchronous messages to the device.
 */
static inline void parport_epilogue(struct parport *pp)
{
	struct mos7715_parport *mos_parport = pp->private_data;
	mutex_unlock(&mos_parport->serial->disc_mutex);
	mos_parport->msg_pending = false;
	complete(&mos_parport->syncmsg_compl);
}

static void parport_mos7715_write_data(struct parport *pp, unsigned char d)
{
	struct mos7715_parport *mos_parport = pp->private_data;
	dbg("%s called: %2.2x", __func__, d);
	if (parport_prologue(pp) < 0)
		return;
	mos7715_change_mode(mos_parport, SPP);
	write_mos_reg(mos_parport->serial, dummy, DPR, (__u8)d);
	parport_epilogue(pp);
}

static unsigned char parport_mos7715_read_data(struct parport *pp)
{
	struct mos7715_parport *mos_parport = pp->private_data;
	unsigned char d;
	dbg("%s called", __func__);
	if (parport_prologue(pp) < 0)
		return 0;
	read_mos_reg(mos_parport->serial, dummy, DPR, &d);
	parport_epilogue(pp);
	return d;
}

static void parport_mos7715_write_control(struct parport *pp, unsigned char d)
{
	struct mos7715_parport *mos_parport = pp->private_data;
	__u8 data;
	dbg("%s called: %2.2x", __func__, d);
	if (parport_prologue(pp) < 0)
		return;
	data = ((__u8)d & 0x0f) | (mos_parport->shadowDCR & 0xf0);
	write_mos_reg(mos_parport->serial, dummy, DCR, data);
	mos_parport->shadowDCR = data;
	parport_epilogue(pp);
}

static unsigned char parport_mos7715_read_control(struct parport *pp)
{
	struct mos7715_parport *mos_parport = pp->private_data;
	__u8 dcr;
	dbg("%s called", __func__);
	spin_lock(&release_lock);
	mos_parport = pp->private_data;
	if (unlikely(mos_parport == NULL)) {
		spin_unlock(&release_lock);
		return 0;
	}
	dcr = mos_parport->shadowDCR & 0x0f;
	spin_unlock(&release_lock);
	return dcr;
}

static unsigned char parport_mos7715_frob_control(struct parport *pp,
						  unsigned char mask,
						  unsigned char val)
{
	struct mos7715_parport *mos_parport = pp->private_data;
	__u8 dcr;
	dbg("%s called", __func__);
	mask &= 0x0f;
	val &= 0x0f;
	if (parport_prologue(pp) < 0)
		return 0;
	mos_parport->shadowDCR = (mos_parport->shadowDCR & (~mask)) ^ val;
	write_mos_reg(mos_parport->serial, dummy, DCR, mos_parport->shadowDCR);
	dcr = mos_parport->shadowDCR & 0x0f;
	parport_epilogue(pp);
	return dcr;
}

static unsigned char parport_mos7715_read_status(struct parport *pp)
{
	unsigned char status;
	struct mos7715_parport *mos_parport = pp->private_data;
	dbg("%s called", __func__);
	spin_lock(&release_lock);
	mos_parport = pp->private_data;
	if (unlikely(mos_parport == NULL)) {	/* release called */
		spin_unlock(&release_lock);
		return 0;
	}
	status = atomic_read(&mos_parport->shadowDSR) & 0xf8;
	spin_unlock(&release_lock);
	return status;
}

static void parport_mos7715_enable_irq(struct parport *pp)
{
	dbg("%s called", __func__);
}
static void parport_mos7715_disable_irq(struct parport *pp)
{
	dbg("%s called", __func__);
}

static void parport_mos7715_data_forward(struct parport *pp)
{
	struct mos7715_parport *mos_parport = pp->private_data;
	dbg("%s called", __func__);
	if (parport_prologue(pp) < 0)
		return;
	mos7715_change_mode(mos_parport, PS2);
	mos_parport->shadowDCR &=  ~0x20;
	write_mos_reg(mos_parport->serial, dummy, DCR, mos_parport->shadowDCR);
	parport_epilogue(pp);
}

static void parport_mos7715_data_reverse(struct parport *pp)
{
	struct mos7715_parport *mos_parport = pp->private_data;
	dbg("%s called", __func__);
	if (parport_prologue(pp) < 0)
		return;
	mos7715_change_mode(mos_parport, PS2);
	mos_parport->shadowDCR |= 0x20;
	write_mos_reg(mos_parport->serial, dummy, DCR, mos_parport->shadowDCR);
	parport_epilogue(pp);
}

static void parport_mos7715_init_state(struct pardevice *dev,
				       struct parport_state *s)
{
	dbg("%s called", __func__);
	s->u.pc.ctr = DCR_INIT_VAL;
	s->u.pc.ecr = ECR_INIT_VAL;
}

/* N.B. Parport core code requires that this function not block */
static void parport_mos7715_save_state(struct parport *pp,
				       struct parport_state *s)
{
	struct mos7715_parport *mos_parport;
	dbg("%s called", __func__);
	spin_lock(&release_lock);
	mos_parport = pp->private_data;
	if (unlikely(mos_parport == NULL)) {	/* release called */
		spin_unlock(&release_lock);
		return;
	}
	s->u.pc.ctr = mos_parport->shadowDCR;
	s->u.pc.ecr = mos_parport->shadowECR;
	spin_unlock(&release_lock);
}

/* N.B. Parport core code requires that this function not block */
static void parport_mos7715_restore_state(struct parport *pp,
					  struct parport_state *s)
{
	struct mos7715_parport *mos_parport;
	dbg("%s called", __func__);
	spin_lock(&release_lock);
	mos_parport = pp->private_data;
	if (unlikely(mos_parport == NULL)) {	/* release called */
		spin_unlock(&release_lock);
		return;
	}
	write_parport_reg_nonblock(mos_parport, DCR, mos_parport->shadowDCR);
	write_parport_reg_nonblock(mos_parport, ECR, mos_parport->shadowECR);
	spin_unlock(&release_lock);
}

static size_t parport_mos7715_write_compat(struct parport *pp,
					   const void *buffer,
					   size_t len, int flags)
{
	int retval;
	struct mos7715_parport *mos_parport = pp->private_data;
	int actual_len;
	dbg("%s called: %u chars", __func__, (unsigned int)len);
	if (parport_prologue(pp) < 0)
		return 0;
	mos7715_change_mode(mos_parport, PPF);
	retval = usb_bulk_msg(mos_parport->serial->dev,
			      usb_sndbulkpipe(mos_parport->serial->dev, 2),
			      (void *)buffer, len, &actual_len,
			      MOS_WDR_TIMEOUT);
	parport_epilogue(pp);
	if (retval) {
		dev_err(&mos_parport->serial->dev->dev,
			"mos7720: usb_bulk_msg() failed: %d", retval);
		return 0;
	}
	return actual_len;
}

static struct parport_operations parport_mos7715_ops = {
	.owner =		THIS_MODULE,
	.write_data =		parport_mos7715_write_data,
	.read_data =		parport_mos7715_read_data,

	.write_control =	parport_mos7715_write_control,
	.read_control =		parport_mos7715_read_control,
	.frob_control =		parport_mos7715_frob_control,

	.read_status =		parport_mos7715_read_status,

	.enable_irq =		parport_mos7715_enable_irq,
	.disable_irq =		parport_mos7715_disable_irq,

	.data_forward =		parport_mos7715_data_forward,
	.data_reverse =		parport_mos7715_data_reverse,

	.init_state =		parport_mos7715_init_state,
	.save_state =		parport_mos7715_save_state,
	.restore_state =	parport_mos7715_restore_state,

	.compat_write_data =	parport_mos7715_write_compat,

	.nibble_read_data =	parport_ieee1284_read_nibble,
	.byte_read_data =	parport_ieee1284_read_byte,
};

/*
 * Allocate and initialize parallel port control struct, initialize
 * the parallel port hardware device, and register with the parport subsystem.
 */
static int mos7715_parport_init(struct usb_serial *serial)
{
	struct mos7715_parport *mos_parport;

	/* allocate and initialize parallel port control struct */
	mos_parport = kzalloc(sizeof(struct mos7715_parport), GFP_KERNEL);
	if (mos_parport == NULL) {
		dbg("mos7715_parport_init: kzalloc failed");
		return -ENOMEM;
	}
	mos_parport->msg_pending = false;
	kref_init(&mos_parport->ref_count);
	spin_lock_init(&mos_parport->listlock);
	INIT_LIST_HEAD(&mos_parport->active_urbs);
	INIT_LIST_HEAD(&mos_parport->deferred_urbs);
	usb_set_serial_data(serial, mos_parport); /* hijack private pointer */
	mos_parport->serial = serial;
	tasklet_init(&mos_parport->urb_tasklet, send_deferred_urbs,
		     (unsigned long) mos_parport);
	init_completion(&mos_parport->syncmsg_compl);

	/* cycle parallel port reset bit */
	write_mos_reg(mos_parport->serial, dummy, PP_REG, (__u8)0x80);
	write_mos_reg(mos_parport->serial, dummy, PP_REG, (__u8)0x00);

	/* initialize device registers */
	mos_parport->shadowDCR = DCR_INIT_VAL;
	write_mos_reg(mos_parport->serial, dummy, DCR, mos_parport->shadowDCR);
	mos_parport->shadowECR = ECR_INIT_VAL;
	write_mos_reg(mos_parport->serial, dummy, ECR, mos_parport->shadowECR);

	/* register with parport core */
	mos_parport->pp = parport_register_port(0, PARPORT_IRQ_NONE,
						PARPORT_DMA_NONE,
						&parport_mos7715_ops);
	if (mos_parport->pp == NULL) {
		dev_err(&serial->interface->dev,
			"Could not register parport\n");
		kref_put(&mos_parport->ref_count, destroy_mos_parport);
		return -EIO;
	}
	mos_parport->pp->private_data = mos_parport;
	mos_parport->pp->modes = PARPORT_MODE_COMPAT | PARPORT_MODE_PCSPP;
	mos_parport->pp->dev = &serial->interface->dev;
	parport_announce_port(mos_parport->pp);

	return 0;
}
#endif	/* CONFIG_USB_SERIAL_MOS7715_PARPORT */

/*
 * mos7720_interrupt_callback
 *	this is the callback function for when we have received data on the
 *	interrupt endpoint.
 */
static void mos7720_interrupt_callback(struct urb *urb)
{
	int result;
	int length;
	int status = urb->status;
	__u8 *data;
	__u8 sp1;
	__u8 sp2;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __func__,
		    status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __func__,
		    status);
		goto exit;
	}

	length = urb->actual_length;
	data = urb->transfer_buffer;

	/* Moschip get 4 bytes
	 * Byte 1 IIR Port 1 (port.number is 0)
	 * Byte 2 IIR Port 2 (port.number is 1)
	 * Byte 3 --------------
	 * Byte 4 FIFO status for both */

	/* the above description is inverted
	 * 	oneukum 2007-03-14 */

	if (unlikely(length != 4)) {
		dbg("Wrong data !!!");
		return;
	}

	sp1 = data[3];
	sp2 = data[2];

	if ((sp1 | sp2) & 0x01) {
		/* No Interrupt Pending in both the ports */
		dbg("No Interrupt !!!");
	} else {
		switch (sp1 & 0x0f) {
		case SERIAL_IIR_RLS:
			dbg("Serial Port 1: Receiver status error or address "
			    "bit detected in 9-bit mode\n");
			break;
		case SERIAL_IIR_CTI:
			dbg("Serial Port 1: Receiver time out");
			break;
		case SERIAL_IIR_MS:
			/* dbg("Serial Port 1: Modem status change"); */
			break;
		}

		switch (sp2 & 0x0f) {
		case SERIAL_IIR_RLS:
			dbg("Serial Port 2: Receiver status error or address "
			    "bit detected in 9-bit mode");
			break;
		case SERIAL_IIR_CTI:
			dbg("Serial Port 2: Receiver time out");
			break;
		case SERIAL_IIR_MS:
			/* dbg("Serial Port 2: Modem status change"); */
			break;
		}
	}

exit:
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(&urb->dev->dev,
			"%s - Error %d submitting control urb\n",
			__func__, result);
}

/*
 * mos7715_interrupt_callback
 *	this is the 7715's callback function for when we have received data on
 *	the interrupt endpoint.
 */
static void mos7715_interrupt_callback(struct urb *urb)
{
	int result;
	int length;
	int status = urb->status;
	__u8 *data;
	__u8 iir;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -ENODEV:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __func__,
		    status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __func__,
		    status);
		goto exit;
	}

	length = urb->actual_length;
	data = urb->transfer_buffer;

	/* Structure of data from 7715 device:
	 * Byte 1: IIR serial Port
	 * Byte 2: unused
	 * Byte 2: DSR parallel port
	 * Byte 4: FIFO status for both */

	if (unlikely(length != 4)) {
		dbg("Wrong data !!!");
		return;
	}

	iir = data[0];
	if (!(iir & 0x01)) {	/* serial port interrupt pending */
		switch (iir & 0x0f) {
		case SERIAL_IIR_RLS:
			dbg("Serial Port: Receiver status error or address "
			    "bit detected in 9-bit mode\n");
			break;
		case SERIAL_IIR_CTI:
			dbg("Serial Port: Receiver time out");
			break;
		case SERIAL_IIR_MS:
			/* dbg("Serial Port: Modem status change"); */
			break;
		}
	}

#ifdef CONFIG_USB_SERIAL_MOS7715_PARPORT
	{       /* update local copy of DSR reg */
		struct usb_serial_port *port = urb->context;
		struct mos7715_parport *mos_parport = port->serial->private;
		if (unlikely(mos_parport == NULL))
			return;
		atomic_set(&mos_parport->shadowDSR, data[2]);
	}
#endif

exit:
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(&urb->dev->dev,
			"%s - Error %d submitting control urb\n",
			__func__, result);
}

/*
 * mos7720_bulk_in_callback
 *	this is the callback function for when we have received data on the
 *	bulk in endpoint.
 */
static void mos7720_bulk_in_callback(struct urb *urb)
{
	int retval;
	unsigned char *data ;
	struct usb_serial_port *port;
	struct tty_struct *tty;
	int status = urb->status;

	if (status) {
		dbg("nonzero read bulk status received: %d", status);
		return;
	}

	port = urb->context;

	dbg("Entering...%s", __func__);

	data = urb->transfer_buffer;

	tty = tty_port_tty_get(&port->port);
	if (tty && urb->actual_length) {
		tty_insert_flip_string(tty, data, urb->actual_length);
		tty_flip_buffer_push(tty);
	}
	tty_kref_put(tty);

	if (port->read_urb->status != -EINPROGRESS) {
		retval = usb_submit_urb(port->read_urb, GFP_ATOMIC);
		if (retval)
			dbg("usb_submit_urb(read bulk) failed, retval = %d",
			    retval);
	}
}

/*
 * mos7720_bulk_out_data_callback
 *	this is the callback function for when we have finished sending serial
 *	data on the bulk out endpoint.
 */
static void mos7720_bulk_out_data_callback(struct urb *urb)
{
	struct moschip_port *mos7720_port;
	struct tty_struct *tty;
	int status = urb->status;

	if (status) {
		dbg("nonzero write bulk status received:%d", status);
		return;
	}

	mos7720_port = urb->context;
	if (!mos7720_port) {
		dbg("NULL mos7720_port pointer");
		return ;
	}

	tty = tty_port_tty_get(&mos7720_port->port->port);

	if (tty && mos7720_port->open)
		tty_wakeup(tty);
	tty_kref_put(tty);
}

/*
 * mos77xx_probe
 *	this function installs the appropriate read interrupt endpoint callback
 *	depending on whether the device is a 7720 or 7715, thus avoiding costly
 *	run-time checks in the high-frequency callback routine itself.
 */
static int mos77xx_probe(struct usb_serial *serial,
			 const struct usb_device_id *id)
{
	if (id->idProduct == MOSCHIP_DEVICE_ID_7715)
		moschip7720_2port_driver.read_int_callback =
			mos7715_interrupt_callback;
	else
		moschip7720_2port_driver.read_int_callback =
			mos7720_interrupt_callback;

	return 0;
}

static int mos77xx_calc_num_ports(struct usb_serial *serial)
{
	u16 product = le16_to_cpu(serial->dev->descriptor.idProduct);
	if (product == MOSCHIP_DEVICE_ID_7715)
		return 1;

	return 2;
}

static int mos7720_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_serial *serial;
	struct urb *urb;
	struct moschip_port *mos7720_port;
	int response;
	int port_number;
	__u8 data;
	int allocated_urbs = 0;
	int j;

	serial = port->serial;

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL)
		return -ENODEV;

	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);

	/* Initialising the write urb pool */
	for (j = 0; j < NUM_URBS; ++j) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		mos7720_port->write_urb_pool[j] = urb;

		if (urb == NULL) {
			dev_err(&port->dev, "No more urbs???\n");
			continue;
		}

		urb->transfer_buffer = kmalloc(URB_TRANSFER_BUFFER_SIZE,
					       GFP_KERNEL);
		if (!urb->transfer_buffer) {
			dev_err(&port->dev,
				"%s-out of memory for urb buffers.\n",
				__func__);
			usb_free_urb(mos7720_port->write_urb_pool[j]);
			mos7720_port->write_urb_pool[j] = NULL;
			continue;
		}
		allocated_urbs++;
	}

	if (!allocated_urbs)
		return -ENOMEM;

	 /* Initialize MCS7720 -- Write Init values to corresponding Registers
	  *
	  * Register Index
	  * 0 : THR/RHR
	  * 1 : IER
	  * 2 : FCR
	  * 3 : LCR
	  * 4 : MCR
	  * 5 : LSR
	  * 6 : MSR
	  * 7 : SPR
	  *
	  * 0x08 : SP1/2 Control Reg
	  */
	port_number = port->number - port->serial->minor;
	read_mos_reg(serial, port_number, LSR, &data);

	dbg("SS::%p LSR:%x", mos7720_port, data);

	dbg("Check:Sending Command ..........");

	write_mos_reg(serial, dummy, SP1_REG, 0x02);
	write_mos_reg(serial, dummy, SP2_REG, 0x02);

	write_mos_reg(serial, port_number, IER, 0x00);
	write_mos_reg(serial, port_number, FCR, 0x00);

	write_mos_reg(serial, port_number, FCR, 0xcf);
	mos7720_port->shadowLCR = 0x03;
	write_mos_reg(serial, port_number, LCR, mos7720_port->shadowLCR);
	mos7720_port->shadowMCR = 0x0b;
	write_mos_reg(serial, port_number, MCR, mos7720_port->shadowMCR);

	write_mos_reg(serial, port_number, SP_CONTROL_REG, 0x00);
	read_mos_reg(serial, dummy, SP_CONTROL_REG, &data);
	data = data | (port->number - port->serial->minor + 1);
	write_mos_reg(serial, dummy, SP_CONTROL_REG, data);
	mos7720_port->shadowLCR = 0x83;
	write_mos_reg(serial, port_number, LCR, mos7720_port->shadowLCR);
	write_mos_reg(serial, port_number, THR, 0x0c);
	write_mos_reg(serial, port_number, IER, 0x00);
	mos7720_port->shadowLCR = 0x03;
	write_mos_reg(serial, port_number, LCR, mos7720_port->shadowLCR);
	write_mos_reg(serial, port_number, IER, 0x0c);

	response = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (response)
		dev_err(&port->dev, "%s - Error %d submitting read urb\n",
							__func__, response);

	/* initialize our icount structure */
	memset(&(mos7720_port->icount), 0x00, sizeof(mos7720_port->icount));

	/* initialize our port settings */
	mos7720_port->shadowMCR = UART_MCR_OUT2; /* Must set to enable ints! */

	/* send a open port command */
	mos7720_port->open = 1;

	return 0;
}

/*
 * mos7720_chars_in_buffer
 *	this function is called by the tty driver when it wants to know how many
 *	bytes of data we currently have outstanding in the port (data that has
 *	been written, but hasn't made it out the port yet)
 *	If successful, we return the number of bytes left to be written in the
 *	system,
 *	Otherwise we return a negative error number.
 */
static int mos7720_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int i;
	int chars = 0;
	struct moschip_port *mos7720_port;

	dbg("%s:entering ...........", __func__);

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL) {
		dbg("%s:leaving ...........", __func__);
		return 0;
	}

	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7720_port->write_urb_pool[i] &&
		    mos7720_port->write_urb_pool[i]->status == -EINPROGRESS)
			chars += URB_TRANSFER_BUFFER_SIZE;
	}
	dbg("%s - returns %d", __func__, chars);
	return chars;
}

static void mos7720_close(struct usb_serial_port *port)
{
	struct usb_serial *serial;
	struct moschip_port *mos7720_port;
	int j;

	dbg("mos7720_close:entering...");

	serial = port->serial;

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL)
		return;

	for (j = 0; j < NUM_URBS; ++j)
		usb_kill_urb(mos7720_port->write_urb_pool[j]);

	/* Freeing Write URBs */
	for (j = 0; j < NUM_URBS; ++j) {
		if (mos7720_port->write_urb_pool[j]) {
			kfree(mos7720_port->write_urb_pool[j]->transfer_buffer);
			usb_free_urb(mos7720_port->write_urb_pool[j]);
		}
	}

	/* While closing port, shutdown all bulk read, write  *
	 * and interrupt read if they exists, otherwise nop   */
	dbg("Shutdown bulk write");
	usb_kill_urb(port->write_urb);
	dbg("Shutdown bulk read");
	usb_kill_urb(port->read_urb);

	mutex_lock(&serial->disc_mutex);
	/* these commands must not be issued if the device has
	 * been disconnected */
	if (!serial->disconnected) {
		write_mos_reg(serial, port->number - port->serial->minor,
			      MCR, 0x00);
		write_mos_reg(serial, port->number - port->serial->minor,
			      IER, 0x00);
	}
	mutex_unlock(&serial->disc_mutex);
	mos7720_port->open = 0;

	dbg("Leaving %s", __func__);
}

static void mos7720_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned char data;
	struct usb_serial *serial;
	struct moschip_port *mos7720_port;

	dbg("Entering %s", __func__);

	serial = port->serial;

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL)
		return;

	if (break_state == -1)
		data = mos7720_port->shadowLCR | UART_LCR_SBC;
	else
		data = mos7720_port->shadowLCR & ~UART_LCR_SBC;

	mos7720_port->shadowLCR  = data;
	write_mos_reg(serial, port->number - port->serial->minor,
		      LCR, mos7720_port->shadowLCR);
}

/*
 * mos7720_write_room
 *	this function is called by the tty driver when it wants to know how many
 *	bytes of data we can accept for a specific port.
 *	If successful, we return the amount of room that we have for this port
 *	Otherwise we return a negative error number.
 */
static int mos7720_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7720_port;
	int room = 0;
	int i;

	dbg("%s:entering ...........", __func__);

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL) {
		dbg("%s:leaving ...........", __func__);
		return -ENODEV;
	}

	/* FIXME: Locking */
	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7720_port->write_urb_pool[i] &&
		    mos7720_port->write_urb_pool[i]->status != -EINPROGRESS)
			room += URB_TRANSFER_BUFFER_SIZE;
	}

	dbg("%s - returns %d", __func__, room);
	return room;
}

static int mos7720_write(struct tty_struct *tty, struct usb_serial_port *port,
				 const unsigned char *data, int count)
{
	int status;
	int i;
	int bytes_sent = 0;
	int transfer_size;

	struct moschip_port *mos7720_port;
	struct usb_serial *serial;
	struct urb    *urb;
	const unsigned char *current_position = data;

	dbg("%s:entering ...........", __func__);

	serial = port->serial;

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL) {
		dbg("mos7720_port is NULL");
		return -ENODEV;
	}

	/* try to find a free urb in the list */
	urb = NULL;

	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7720_port->write_urb_pool[i] &&
		    mos7720_port->write_urb_pool[i]->status != -EINPROGRESS) {
			urb = mos7720_port->write_urb_pool[i];
			dbg("URB:%d", i);
			break;
		}
	}

	if (urb == NULL) {
		dbg("%s - no more free urbs", __func__);
		goto exit;
	}

	if (urb->transfer_buffer == NULL) {
		urb->transfer_buffer = kmalloc(URB_TRANSFER_BUFFER_SIZE,
					       GFP_KERNEL);
		if (urb->transfer_buffer == NULL) {
			dev_err_console(port, "%s no more kernel memory...\n",
				__func__);
			goto exit;
		}
	}
	transfer_size = min(count, URB_TRANSFER_BUFFER_SIZE);

	memcpy(urb->transfer_buffer, current_position, transfer_size);
	usb_serial_debug_data(debug, &port->dev, __func__, transfer_size,
			      urb->transfer_buffer);

	/* fill urb with data and submit  */
	usb_fill_bulk_urb(urb, serial->dev,
			  usb_sndbulkpipe(serial->dev,
					port->bulk_out_endpointAddress),
			  urb->transfer_buffer, transfer_size,
			  mos7720_bulk_out_data_callback, mos7720_port);

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		dev_err_console(port, "%s - usb_submit_urb(write bulk) failed "
			"with status = %d\n", __func__, status);
		bytes_sent = status;
		goto exit;
	}
	bytes_sent = transfer_size;

exit:
	return bytes_sent;
}

static void mos7720_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7720_port;
	int status;

	dbg("%s- port %d", __func__, port->number);

	mos7720_port = usb_get_serial_port_data(port);

	if (mos7720_port == NULL)
		return;

	if (!mos7720_port->open) {
		dbg("port not opened");
		return;
	}

	dbg("%s: Entering ..........", __func__);

	/* if we are implementing XON/XOFF, send the stop character */
	if (I_IXOFF(tty)) {
		unsigned char stop_char = STOP_CHAR(tty);
		status = mos7720_write(tty, port, &stop_char, 1);
		if (status <= 0)
			return;
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios->c_cflag & CRTSCTS) {
		mos7720_port->shadowMCR &= ~UART_MCR_RTS;
		write_mos_reg(port->serial, port->number - port->serial->minor,
			      MCR, mos7720_port->shadowMCR);
		if (status != 0)
			return;
	}
}

static void mos7720_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7720_port = usb_get_serial_port_data(port);
	int status;

	if (mos7720_port == NULL)
		return;

	if (!mos7720_port->open) {
		dbg("%s - port not opened", __func__);
		return;
	}

	dbg("%s: Entering ..........", __func__);

	/* if we are implementing XON/XOFF, send the start character */
	if (I_IXOFF(tty)) {
		unsigned char start_char = START_CHAR(tty);
		status = mos7720_write(tty, port, &start_char, 1);
		if (status <= 0)
			return;
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios->c_cflag & CRTSCTS) {
		mos7720_port->shadowMCR |= UART_MCR_RTS;
		write_mos_reg(port->serial, port->number - port->serial->minor,
			      MCR, mos7720_port->shadowMCR);
		if (status != 0)
			return;
	}
}

/* FIXME: this function does not work */
static int set_higher_rates(struct moschip_port *mos7720_port,
			    unsigned int baud)
{
	struct usb_serial_port *port;
	struct usb_serial *serial;
	int port_number;
	enum mos_regs sp_reg;
	if (mos7720_port == NULL)
		return -EINVAL;

	port = mos7720_port->port;
	serial = port->serial;

	 /***********************************************
	 *      Init Sequence for higher rates
	 ***********************************************/
	dbg("Sending Setting Commands ..........");
	port_number = port->number - port->serial->minor;

	write_mos_reg(serial, port_number, IER, 0x00);
	write_mos_reg(serial, port_number, FCR, 0x00);
	write_mos_reg(serial, port_number, FCR, 0xcf);
	mos7720_port->shadowMCR = 0x0b;
	write_mos_reg(serial, port_number, MCR, mos7720_port->shadowMCR);
	write_mos_reg(serial, dummy, SP_CONTROL_REG, 0x00);

	/***********************************************
	 *              Set for higher rates           *
	 ***********************************************/
	/* writing baud rate verbatum into uart clock field clearly not right */
	if (port_number == 0)
		sp_reg = SP1_REG;
	else
		sp_reg = SP2_REG;
	write_mos_reg(serial, dummy, sp_reg, baud * 0x10);
	write_mos_reg(serial, dummy, SP_CONTROL_REG, 0x03);
	mos7720_port->shadowMCR = 0x2b;
	write_mos_reg(serial, port_number, MCR, mos7720_port->shadowMCR);

	/***********************************************
	 *              Set DLL/DLM
	 ***********************************************/
	mos7720_port->shadowLCR = mos7720_port->shadowLCR | UART_LCR_DLAB;
	write_mos_reg(serial, port_number, LCR, mos7720_port->shadowLCR);
	write_mos_reg(serial, port_number, DLL, 0x01);
	write_mos_reg(serial, port_number, DLM, 0x00);
	mos7720_port->shadowLCR = mos7720_port->shadowLCR & ~UART_LCR_DLAB;
	write_mos_reg(serial, port_number, LCR, mos7720_port->shadowLCR);

	return 0;
}

/* baud rate information */
struct divisor_table_entry {
	__u32  baudrate;
	__u16  divisor;
};

/* Define table of divisors for moschip 7720 hardware	   *
 * These assume a 3.6864MHz crystal, the standard /16, and *
 * MCR.7 = 0.						   */
static struct divisor_table_entry divisor_table[] = {
	{   50,		2304},
	{   110,	1047},	/* 2094.545455 => 230450   => .0217 % over */
	{   134,	857},	/* 1713.011152 => 230398.5 => .00065% under */
	{   150,	768},
	{   300,	384},
	{   600,	192},
	{   1200,	96},
	{   1800,	64},
	{   2400,	48},
	{   4800,	24},
	{   7200,	16},
	{   9600,	12},
	{   19200,	6},
	{   38400,	3},
	{   57600,	2},
	{   115200,	1},
};

/*****************************************************************************
 * calc_baud_rate_divisor
 *	this function calculates the proper baud rate divisor for the specified
 *	baud rate.
 *****************************************************************************/
static int calc_baud_rate_divisor(int baudrate, int *divisor)
{
	int i;
	__u16 custom;
	__u16 round1;
	__u16 round;


	dbg("%s - %d", __func__, baudrate);

	for (i = 0; i < ARRAY_SIZE(divisor_table); i++) {
		if (divisor_table[i].baudrate == baudrate) {
			*divisor = divisor_table[i].divisor;
			return 0;
		}
	}

	/* After trying for all the standard baud rates    *
	 * Try calculating the divisor for this baud rate  */
	if (baudrate > 75 &&  baudrate < 230400) {
		/* get the divisor */
		custom = (__u16)(230400L  / baudrate);

		/* Check for round off */
		round1 = (__u16)(2304000L / baudrate);
		round = (__u16)(round1 - (custom * 10));
		if (round > 4)
			custom++;
		*divisor = custom;

		dbg("Baud %d = %d", baudrate, custom);
		return 0;
	}

	dbg("Baud calculation Failed...");
	return -EINVAL;
}

/*
 * send_cmd_write_baud_rate
 *	this function sends the proper command to change the baud rate of the
 *	specified port.
 */
static int send_cmd_write_baud_rate(struct moschip_port *mos7720_port,
				    int baudrate)
{
	struct usb_serial_port *port;
	struct usb_serial *serial;
	int divisor;
	int status;
	unsigned char number;

	if (mos7720_port == NULL)
		return -1;

	port = mos7720_port->port;
	serial = port->serial;

	dbg("%s: Entering ..........", __func__);

	number = port->number - port->serial->minor;
	dbg("%s - port = %d, baud = %d", __func__, port->number, baudrate);

	/* Calculate the Divisor */
	status = calc_baud_rate_divisor(baudrate, &divisor);
	if (status) {
		dev_err(&port->dev, "%s - bad baud rate\n", __func__);
		return status;
	}

	/* Enable access to divisor latch */
	mos7720_port->shadowLCR = mos7720_port->shadowLCR | UART_LCR_DLAB;
	write_mos_reg(serial, number, LCR, mos7720_port->shadowLCR);

	/* Write the divisor */
	write_mos_reg(serial, number, DLL, (__u8)(divisor & 0xff));
	write_mos_reg(serial, number, DLM, (__u8)((divisor & 0xff00) >> 8));

	/* Disable access to divisor latch */
	mos7720_port->shadowLCR = mos7720_port->shadowLCR & ~UART_LCR_DLAB;
	write_mos_reg(serial, number, LCR, mos7720_port->shadowLCR);

	return status;
}

/*
 * change_port_settings
 *	This routine is called to set the UART on the device to match
 *      the specified new settings.
 */
static void change_port_settings(struct tty_struct *tty,
				 struct moschip_port *mos7720_port,
				 struct ktermios *old_termios)
{
	struct usb_serial_port *port;
	struct usb_serial *serial;
	int baud;
	unsigned cflag;
	unsigned iflag;
	__u8 mask = 0xff;
	__u8 lData;
	__u8 lParity;
	__u8 lStop;
	int status;
	int port_number;

	if (mos7720_port == NULL)
		return ;

	port = mos7720_port->port;
	serial = port->serial;
	port_number = port->number - port->serial->minor;

	dbg("%s - port %d", __func__, port->number);

	if (!mos7720_port->open) {
		dbg("%s - port not opened", __func__);
		return;
	}

	dbg("%s: Entering ..........", __func__);

	lData = UART_LCR_WLEN8;
	lStop = 0x00;	/* 1 stop bit */
	lParity = 0x00;	/* No parity */

	cflag = tty->termios->c_cflag;
	iflag = tty->termios->c_iflag;

	/* Change the number of bits */
	switch (cflag & CSIZE) {
	case CS5:
		lData = UART_LCR_WLEN5;
		mask = 0x1f;
		break;

	case CS6:
		lData = UART_LCR_WLEN6;
		mask = 0x3f;
		break;

	case CS7:
		lData = UART_LCR_WLEN7;
		mask = 0x7f;
		break;
	default:
	case CS8:
		lData = UART_LCR_WLEN8;
		break;
	}

	/* Change the Parity bit */
	if (cflag & PARENB) {
		if (cflag & PARODD) {
			lParity = UART_LCR_PARITY;
			dbg("%s - parity = odd", __func__);
		} else {
			lParity = (UART_LCR_EPAR | UART_LCR_PARITY);
			dbg("%s - parity = even", __func__);
		}

	} else {
		dbg("%s - parity = none", __func__);
	}

	if (cflag & CMSPAR)
		lParity = lParity | 0x20;

	/* Change the Stop bit */
	if (cflag & CSTOPB) {
		lStop = UART_LCR_STOP;
		dbg("%s - stop bits = 2", __func__);
	} else {
		lStop = 0x00;
		dbg("%s - stop bits = 1", __func__);
	}

#define LCR_BITS_MASK		0x03	/* Mask for bits/char field */
#define LCR_STOP_MASK		0x04	/* Mask for stop bits field */
#define LCR_PAR_MASK		0x38	/* Mask for parity field */

	/* Update the LCR with the correct value */
	mos7720_port->shadowLCR &=
		~(LCR_BITS_MASK | LCR_STOP_MASK | LCR_PAR_MASK);
	mos7720_port->shadowLCR |= (lData | lParity | lStop);


	/* Disable Interrupts */
	write_mos_reg(serial, port_number, IER, 0x00);
	write_mos_reg(serial, port_number, FCR, 0x00);
	write_mos_reg(serial, port_number, FCR, 0xcf);

	/* Send the updated LCR value to the mos7720 */
	write_mos_reg(serial, port_number, LCR, mos7720_port->shadowLCR);
	mos7720_port->shadowMCR = 0x0b;
	write_mos_reg(serial, port_number, MCR, mos7720_port->shadowMCR);

	/* set up the MCR register and send it to the mos7720 */
	mos7720_port->shadowMCR = UART_MCR_OUT2;
	if (cflag & CBAUD)
		mos7720_port->shadowMCR |= (UART_MCR_DTR | UART_MCR_RTS);

	if (cflag & CRTSCTS) {
		mos7720_port->shadowMCR |= (UART_MCR_XONANY);
		/* To set hardware flow control to the specified *
		 * serial port, in SP1/2_CONTROL_REG             */
		if (port_number)
			write_mos_reg(serial, dummy, SP_CONTROL_REG, 0x01);
		else
			write_mos_reg(serial, dummy, SP_CONTROL_REG, 0x02);

	} else
		mos7720_port->shadowMCR &= ~(UART_MCR_XONANY);

	write_mos_reg(serial, port_number, MCR, mos7720_port->shadowMCR);

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(tty);
	if (!baud) {
		/* pick a default, any default... */
		dbg("Picked default baud...");
		baud = 9600;
	}

	if (baud >= 230400) {
		set_higher_rates(mos7720_port, baud);
		/* Enable Interrupts */
		write_mos_reg(serial, port_number, IER, 0x0c);
		return;
	}

	dbg("%s - baud rate = %d", __func__, baud);
	status = send_cmd_write_baud_rate(mos7720_port, baud);
	/* FIXME: needs to write actual resulting baud back not just
	   blindly do so */
	if (cflag & CBAUD)
		tty_encode_baud_rate(tty, baud, baud);
	/* Enable Interrupts */
	write_mos_reg(serial, port_number, IER, 0x0c);

	if (port->read_urb->status != -EINPROGRESS) {
		status = usb_submit_urb(port->read_urb, GFP_ATOMIC);
		if (status)
			dbg("usb_submit_urb(read bulk) failed, status = %d",
			    status);
	}
}

/*
 * mos7720_set_termios
 *	this function is called by the tty driver when it wants to change the
 *	termios structure.
 */
static void mos7720_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	int status;
	unsigned int cflag;
	struct usb_serial *serial;
	struct moschip_port *mos7720_port;

	serial = port->serial;

	mos7720_port = usb_get_serial_port_data(port);

	if (mos7720_port == NULL)
		return;

	if (!mos7720_port->open) {
		dbg("%s - port not opened", __func__);
		return;
	}

	dbg("%s\n", "setting termios - ASPIRE");

	cflag = tty->termios->c_cflag;

	dbg("%s - cflag %08x iflag %08x", __func__,
	    tty->termios->c_cflag,
	    RELEVANT_IFLAG(tty->termios->c_iflag));

	dbg("%s - old cflag %08x old iflag %08x", __func__,
	    old_termios->c_cflag,
	    RELEVANT_IFLAG(old_termios->c_iflag));

	dbg("%s - port %d", __func__, port->number);

	/* change the port settings to the new ones specified */
	change_port_settings(tty, mos7720_port, old_termios);

	if (port->read_urb->status != -EINPROGRESS) {
		status = usb_submit_urb(port->read_urb, GFP_ATOMIC);
		if (status)
			dbg("usb_submit_urb(read bulk) failed, status = %d",
			    status);
	}
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space.
 */
static int get_lsr_info(struct tty_struct *tty,
		struct moschip_port *mos7720_port, unsigned int __user *value)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned int result = 0;
	unsigned char data = 0;
	int port_number = port->number - port->serial->minor;
	int count;

	count = mos7720_chars_in_buffer(tty);
	if (count == 0) {
		read_mos_reg(port->serial, port_number, LSR, &data);
		if ((data & (UART_LSR_TEMT | UART_LSR_THRE))
					== (UART_LSR_TEMT | UART_LSR_THRE)) {
			dbg("%s -- Empty", __func__);
			result = TIOCSER_TEMT;
		}
	}
	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

static int mos7720_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7720_port = usb_get_serial_port_data(port);
	unsigned int result = 0;
	unsigned int mcr ;
	unsigned int msr ;

	dbg("%s - port %d", __func__, port->number);

	mcr = mos7720_port->shadowMCR;
	msr = mos7720_port->shadowMSR;

	result = ((mcr & UART_MCR_DTR)  ? TIOCM_DTR : 0)   /* 0x002 */
	  | ((mcr & UART_MCR_RTS)   ? TIOCM_RTS : 0)   /* 0x004 */
	  | ((msr & UART_MSR_CTS)   ? TIOCM_CTS : 0)   /* 0x020 */
	  | ((msr & UART_MSR_DCD)   ? TIOCM_CAR : 0)   /* 0x040 */
	  | ((msr & UART_MSR_RI)    ? TIOCM_RI :  0)   /* 0x080 */
	  | ((msr & UART_MSR_DSR)   ? TIOCM_DSR : 0);  /* 0x100 */

	dbg("%s -- %x", __func__, result);

	return result;
}

static int mos7720_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7720_port = usb_get_serial_port_data(port);
	unsigned int mcr ;
	dbg("%s - port %d", __func__, port->number);
	dbg("he was at tiocmset");

	mcr = mos7720_port->shadowMCR;

	if (set & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	if (set & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	if (set & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;

	if (clear & TIOCM_RTS)
		mcr &= ~UART_MCR_RTS;
	if (clear & TIOCM_DTR)
		mcr &= ~UART_MCR_DTR;
	if (clear & TIOCM_LOOP)
		mcr &= ~UART_MCR_LOOP;

	mos7720_port->shadowMCR = mcr;
	write_mos_reg(port->serial, port->number - port->serial->minor,
		      MCR, mos7720_port->shadowMCR);

	return 0;
}

static int mos7720_get_icount(struct tty_struct *tty,
				struct serial_icounter_struct *icount)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7720_port;
	struct async_icount cnow;

	mos7720_port = usb_get_serial_port_data(port);
	cnow = mos7720_port->icount;

	icount->cts = cnow.cts;
	icount->dsr = cnow.dsr;
	icount->rng = cnow.rng;
	icount->dcd = cnow.dcd;
	icount->rx = cnow.rx;
	icount->tx = cnow.tx;
	icount->frame = cnow.frame;
	icount->overrun = cnow.overrun;
	icount->parity = cnow.parity;
	icount->brk = cnow.brk;
	icount->buf_overrun = cnow.buf_overrun;

	dbg("%s (%d) TIOCGICOUNT RX=%d, TX=%d", __func__,
		port->number, icount->rx, icount->tx);
	return 0;
}

static int set_modem_info(struct moschip_port *mos7720_port, unsigned int cmd,
			  unsigned int __user *value)
{
	unsigned int mcr;
	unsigned int arg;

	struct usb_serial_port *port;

	if (mos7720_port == NULL)
		return -1;

	port = (struct usb_serial_port *)mos7720_port->port;
	mcr = mos7720_port->shadowMCR;

	if (copy_from_user(&arg, value, sizeof(int)))
		return -EFAULT;

	switch (cmd) {
	case TIOCMBIS:
		if (arg & TIOCM_RTS)
			mcr |= UART_MCR_RTS;
		if (arg & TIOCM_DTR)
			mcr |= UART_MCR_RTS;
		if (arg & TIOCM_LOOP)
			mcr |= UART_MCR_LOOP;
		break;

	case TIOCMBIC:
		if (arg & TIOCM_RTS)
			mcr &= ~UART_MCR_RTS;
		if (arg & TIOCM_DTR)
			mcr &= ~UART_MCR_RTS;
		if (arg & TIOCM_LOOP)
			mcr &= ~UART_MCR_LOOP;
		break;

	}

	mos7720_port->shadowMCR = mcr;
	write_mos_reg(port->serial, port->number - port->serial->minor,
		      MCR, mos7720_port->shadowMCR);

	return 0;
}

static int get_serial_info(struct moschip_port *mos7720_port,
			   struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;

	if (!retinfo)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));

	tmp.type		= PORT_16550A;
	tmp.line		= mos7720_port->port->serial->minor;
	tmp.port		= mos7720_port->port->number;
	tmp.irq			= 0;
	tmp.flags		= ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
	tmp.xmit_fifo_size	= NUM_URBS * URB_TRANSFER_BUFFER_SIZE;
	tmp.baud_base		= 9600;
	tmp.close_delay		= 5*HZ;
	tmp.closing_wait	= 30*HZ;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int mos7720_ioctl(struct tty_struct *tty,
			 unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7720_port;
	struct async_icount cnow;
	struct async_icount cprev;

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL)
		return -ENODEV;

	dbg("%s - port %d, cmd = 0x%x", __func__, port->number, cmd);

	switch (cmd) {
	case TIOCSERGETLSR:
		dbg("%s (%d) TIOCSERGETLSR", __func__,  port->number);
		return get_lsr_info(tty, mos7720_port,
					(unsigned int __user *)arg);

	/* FIXME: These should be using the mode methods */
	case TIOCMBIS:
	case TIOCMBIC:
		dbg("%s (%d) TIOCMSET/TIOCMBIC/TIOCMSET",
					__func__, port->number);
		return set_modem_info(mos7720_port, cmd,
				      (unsigned int __user *)arg);

	case TIOCGSERIAL:
		dbg("%s (%d) TIOCGSERIAL", __func__,  port->number);
		return get_serial_info(mos7720_port,
				       (struct serial_struct __user *)arg);

	case TIOCMIWAIT:
		dbg("%s (%d) TIOCMIWAIT", __func__,  port->number);
		cprev = mos7720_port->icount;
		while (1) {
			if (signal_pending(current))
				return -ERESTARTSYS;
			cnow = mos7720_port->icount;
			if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
			    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
				return -EIO; /* no change => error */
			if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
			    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
			    ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
			    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
				return 0;
			}
			cprev = cnow;
		}
		/* NOTREACHED */
		break;
	}

	return -ENOIOCTLCMD;
}

static int mos7720_startup(struct usb_serial *serial)
{
	struct moschip_port *mos7720_port;
	struct usb_device *dev;
	int i;
	char data;
	u16 product;
	int ret_val;

	dbg("%s: Entering ..........", __func__);

	if (!serial) {
		dbg("Invalid Handler");
		return -ENODEV;
	}

	product = le16_to_cpu(serial->dev->descriptor.idProduct);
	dev = serial->dev;

	/*
	 * The 7715 uses the first bulk in/out endpoint pair for the parallel
	 * port, and the second for the serial port.  Because the usbserial core
	 * assumes both pairs are serial ports, we must engage in a bit of
	 * subterfuge and swap the pointers for ports 0 and 1 in order to make
	 * port 0 point to the serial port.  However, both moschip devices use a
	 * single interrupt-in endpoint for both ports (as mentioned a little
	 * further down), and this endpoint was assigned to port 0.  So after
	 * the swap, we must copy the interrupt endpoint elements from port 1
	 * (as newly assigned) to port 0, and null out port 1 pointers.
	 */
	if (product == MOSCHIP_DEVICE_ID_7715) {
		struct usb_serial_port *tmp = serial->port[0];
		serial->port[0] = serial->port[1];
		serial->port[1] = tmp;
		serial->port[0]->interrupt_in_urb = tmp->interrupt_in_urb;
		serial->port[0]->interrupt_in_buffer = tmp->interrupt_in_buffer;
		serial->port[0]->interrupt_in_endpointAddress =
			tmp->interrupt_in_endpointAddress;
		serial->port[1]->interrupt_in_urb = NULL;
		serial->port[1]->interrupt_in_buffer = NULL;
	}


	/* set up serial port private structures */
	for (i = 0; i < serial->num_ports; ++i) {
		mos7720_port = kzalloc(sizeof(struct moschip_port), GFP_KERNEL);
		if (mos7720_port == NULL) {
			dev_err(&dev->dev, "%s - Out of memory\n", __func__);
			return -ENOMEM;
		}

		/* Initialize all port interrupt end point to port 0 int
		 * endpoint.  Our device has only one interrupt endpoint
		 * common to all ports */
		serial->port[i]->interrupt_in_endpointAddress =
				serial->port[0]->interrupt_in_endpointAddress;

		mos7720_port->port = serial->port[i];
		usb_set_serial_port_data(serial->port[i], mos7720_port);

		dbg("port number is %d", serial->port[i]->number);
		dbg("serial number is %d", serial->minor);
	}


	/* setting configuration feature to one */
	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			(__u8)0x03, 0x00, 0x01, 0x00, NULL, 0x00, 5000);

	/* start the interrupt urb */
	ret_val = usb_submit_urb(serial->port[0]->interrupt_in_urb, GFP_KERNEL);
	if (ret_val)
		dev_err(&dev->dev,
			"%s - Error %d submitting control urb\n",
			__func__, ret_val);

#ifdef CONFIG_USB_SERIAL_MOS7715_PARPORT
	if (product == MOSCHIP_DEVICE_ID_7715) {
		ret_val = mos7715_parport_init(serial);
		if (ret_val < 0)
			return ret_val;
	}
#endif
	/* LSR For Port 1 */
	read_mos_reg(serial, 0, LSR, &data);
	dbg("LSR:%x", data);

	return 0;
}

static void mos7720_release(struct usb_serial *serial)
{
	int i;

#ifdef CONFIG_USB_SERIAL_MOS7715_PARPORT
	/* close the parallel port */

	if (le16_to_cpu(serial->dev->descriptor.idProduct)
	    == MOSCHIP_DEVICE_ID_7715) {
		struct urbtracker *urbtrack;
		unsigned long flags;
		struct mos7715_parport *mos_parport =
			usb_get_serial_data(serial);

		/* prevent NULL ptr dereference in port callbacks */
		spin_lock(&release_lock);
		mos_parport->pp->private_data = NULL;
		spin_unlock(&release_lock);

		/* wait for synchronous usb calls to return */
		if (mos_parport->msg_pending)
			wait_for_completion_timeout(&mos_parport->syncmsg_compl,
					    msecs_to_jiffies(MOS_WDR_TIMEOUT));

		parport_remove_port(mos_parport->pp);
		usb_set_serial_data(serial, NULL);
		mos_parport->serial = NULL;

		/* if tasklet currently scheduled, wait for it to complete */
		tasklet_kill(&mos_parport->urb_tasklet);

		/* unlink any urbs sent by the tasklet  */
		spin_lock_irqsave(&mos_parport->listlock, flags);
		list_for_each_entry(urbtrack,
				    &mos_parport->active_urbs,
				    urblist_entry)
			usb_unlink_urb(urbtrack->urb);
		spin_unlock_irqrestore(&mos_parport->listlock, flags);

		kref_put(&mos_parport->ref_count, destroy_mos_parport);
	}
#endif
	/* free private structure allocated for serial port */
	for (i = 0; i < serial->num_ports; ++i)
		kfree(usb_get_serial_port_data(serial->port[i]));
}

static struct usb_driver usb_driver = {
	.name =		"moschip7720",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	moschip_port_id_table,
};

static struct usb_serial_driver moschip7720_2port_driver = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"moschip7720",
	},
	.description		= "Moschip 2 port adapter",
	.id_table		= moschip_port_id_table,
	.calc_num_ports		= mos77xx_calc_num_ports,
	.open			= mos7720_open,
	.close			= mos7720_close,
	.throttle		= mos7720_throttle,
	.unthrottle		= mos7720_unthrottle,
	.probe			= mos77xx_probe,
	.attach			= mos7720_startup,
	.release		= mos7720_release,
	.ioctl			= mos7720_ioctl,
	.tiocmget		= mos7720_tiocmget,
	.tiocmset		= mos7720_tiocmset,
	.get_icount		= mos7720_get_icount,
	.set_termios		= mos7720_set_termios,
	.write			= mos7720_write,
	.write_room		= mos7720_write_room,
	.chars_in_buffer	= mos7720_chars_in_buffer,
	.break_ctl		= mos7720_break,
	.read_bulk_callback	= mos7720_bulk_in_callback,
	.read_int_callback	= NULL  /* dynamically assigned in probe() */
};

static struct usb_serial_driver * const serial_drivers[] = {
	&moschip7720_2port_driver, NULL
};

module_usb_serial_driver(usb_driver, serial_drivers);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
