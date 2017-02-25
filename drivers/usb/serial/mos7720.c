/*
 * mos7720.c
 *   Controls the Moschip 7720 usb to dual port serial converter
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

#define DRIVER_AUTHOR "Aspire Communications pvt Ltd."
#define DRIVER_DESC "Moschip USB Serial Driver"

/* default urb timeout */
#define MOS_WDR_TIMEOUT	5000

#define MOS_MAX_PORT	0x02
#define MOS_WRITE	0x0E
#define MOS_READ	0x0D

/* Interrupt Routines Defines	*/
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
	struct usb_serial_port	*port;	/* loop back to the owner */
	struct urb		*write_urb_pool[NUM_URBS];
};

#define USB_VENDOR_ID_MOSCHIP		0x9710
#define MOSCHIP_DEVICE_ID_7720		0x7720
#define MOSCHIP_DEVICE_ID_7715		0x7715

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_MOSCHIP, MOSCHIP_DEVICE_ID_7720) },
	{ USB_DEVICE(USB_VENDOR_ID_MOSCHIP, MOSCHIP_DEVICE_ID_7715) },
	{ } /* terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

#ifdef CONFIG_USB_SERIAL_MOS7715_PARPORT

/* initial values for parport regs */
#define DCR_INIT_VAL       0x0c	/* SLCTIN, nINIT */
#define ECR_INIT_VAL       0x00	/* SPP mode */

struct urbtracker {
	struct mos7715_parport  *mos_parport;
	struct list_head        urblist_entry;
	struct kref             ref_count;
	struct urb              *urb;
	struct usb_ctrlrequest	*setup;
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
	MOS7720_THR,		  /* serial port regs */
	MOS7720_RHR,
	MOS7720_IER,
	MOS7720_FCR,
	MOS7720_ISR,
	MOS7720_LCR,
	MOS7720_MCR,
	MOS7720_LSR,
	MOS7720_MSR,
	MOS7720_SPR,
	MOS7720_DLL,
	MOS7720_DLM,
	MOS7720_DPR,		  /* parallel port regs */
	MOS7720_DSR,
	MOS7720_DCR,
	MOS7720_ECR,
	MOS7720_SP1_REG,	  /* device control regs */
	MOS7720_SP2_REG,	  /* serial port 2 (7720 only) */
	MOS7720_PP_REG,
	MOS7720_SP_CONTROL_REG,
};

/*
 * Return the correct value for the Windex field of the setup packet
 * for a control endpoint message.  See the 7715 datasheet.
 */
static inline __u16 get_reg_index(enum mos_regs reg)
{
	static const __u16 mos7715_index_lookup_table[] = {
		0x00,		/* MOS7720_THR */
		0x00,		/* MOS7720_RHR */
		0x01,		/* MOS7720_IER */
		0x02,		/* MOS7720_FCR */
		0x02,		/* MOS7720_ISR */
		0x03,		/* MOS7720_LCR */
		0x04,		/* MOS7720_MCR */
		0x05,		/* MOS7720_LSR */
		0x06,		/* MOS7720_MSR */
		0x07,		/* MOS7720_SPR */
		0x00,		/* MOS7720_DLL */
		0x01,		/* MOS7720_DLM */
		0x00,		/* MOS7720_DPR */
		0x01,		/* MOS7720_DSR */
		0x02,		/* MOS7720_DCR */
		0x0a,		/* MOS7720_ECR */
		0x01,		/* MOS7720_SP1_REG */
		0x02,		/* MOS7720_SP2_REG (7720 only) */
		0x04,		/* MOS7720_PP_REG (7715 only) */
		0x08,		/* MOS7720_SP_CONTROL_REG */
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
	if (reg >= MOS7720_SP1_REG)	/* control reg */
		return 0x0000;

	else if (reg >= MOS7720_DPR)	/* parallel port reg (7715 only) */
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
			"mos7720: usb_control_msg() failed: %d\n", status);
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
			"mos7720: usb_control_msg() failed: %d\n", status);
	kfree(buf);

	return status;
}

#ifdef CONFIG_USB_SERIAL_MOS7715_PARPORT

static inline int mos7715_change_mode(struct mos7715_parport *mos_parport,
				      enum mos7715_pp_modes mode)
{
	mos_parport->shadowECR = mode;
	write_mos_reg(mos_parport->serial, dummy, MOS7720_ECR,
		      mos_parport->shadowECR);
	return 0;
}

static void destroy_mos_parport(struct kref *kref)
{
	struct mos7715_parport *mos_parport =
		container_of(kref, struct mos7715_parport, ref_count);

	kfree(mos_parport);
}

static void destroy_urbtracker(struct kref *kref)
{
	struct urbtracker *urbtrack =
		container_of(kref, struct urbtracker, ref_count);
	struct mos7715_parport *mos_parport = urbtrack->mos_parport;

	usb_free_urb(urbtrack->urb);
	kfree(urbtrack->setup);
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
	struct urbtracker *urbtrack, *tmp;
	struct list_head *cursor, *next;
	struct device *dev;

	/* if release function ran, game over */
	if (unlikely(mos_parport->serial == NULL))
		return;

	dev = &mos_parport->serial->dev->dev;

	/* try again to get the mutex */
	if (!mutex_trylock(&mos_parport->serial->disc_mutex)) {
		dev_dbg(dev, "%s: rescheduling tasklet\n", __func__);
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
		dev_dbg(dev, "%s: deferred_urbs list empty\n", __func__);
		return;
	}

	/* move contents of deferred_urbs list to active_urbs list and submit */
	list_for_each_safe(cursor, next, &mos_parport->deferred_urbs)
		list_move_tail(cursor, &mos_parport->active_urbs);
	list_for_each_entry_safe(urbtrack, tmp, &mos_parport->active_urbs,
			    urblist_entry) {
		ret_val = usb_submit_urb(urbtrack->urb, GFP_ATOMIC);
		dev_dbg(dev, "%s: urb submitted\n", __func__);
		if (ret_val) {
			dev_err(dev, "usb_submit_urb() failed: %d\n", ret_val);
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

	if (unlikely(status))
		dev_dbg(&urb->dev->dev, "%s - nonzero urb status received: %d\n", __func__, status);

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
	struct usb_serial *serial = mos_parport->serial;
	struct usb_device *usbdev = serial->dev;

	/* create and initialize the control urb and containing urbtracker */
	urbtrack = kmalloc(sizeof(struct urbtracker), GFP_ATOMIC);
	if (!urbtrack)
		return -ENOMEM;

	kref_get(&mos_parport->ref_count);
	urbtrack->mos_parport = mos_parport;
	urbtrack->urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urbtrack->urb) {
		kfree(urbtrack);
		return -ENOMEM;
	}
	urbtrack->setup = kmalloc(sizeof(*urbtrack->setup), GFP_ATOMIC);
	if (!urbtrack->setup) {
		usb_free_urb(urbtrack->urb);
		kfree(urbtrack);
		return -ENOMEM;
	}
	urbtrack->setup->bRequestType = (__u8)0x40;
	urbtrack->setup->bRequest = (__u8)0x0e;
	urbtrack->setup->wValue = cpu_to_le16(get_reg_value(reg, dummy));
	urbtrack->setup->wIndex = cpu_to_le16(get_reg_index(reg));
	urbtrack->setup->wLength = 0;
	usb_fill_control_urb(urbtrack->urb, usbdev,
			     usb_sndctrlpipe(usbdev, 0),
			     (unsigned char *)urbtrack->setup,
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
		dev_dbg(&usbdev->dev, "tasklet scheduled\n");
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
			"%s: submit_urb() failed: %d\n", __func__, ret_val);
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
 * flag to ensure that all synchronous usb message calls have completed before
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
	reinit_completion(&mos_parport->syncmsg_compl);
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
 * This is the common bottom part of all parallel port functions that send
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

	if (parport_prologue(pp) < 0)
		return;
	mos7715_change_mode(mos_parport, SPP);
	write_mos_reg(mos_parport->serial, dummy, MOS7720_DPR, (__u8)d);
	parport_epilogue(pp);
}

static unsigned char parport_mos7715_read_data(struct parport *pp)
{
	struct mos7715_parport *mos_parport = pp->private_data;
	unsigned char d;

	if (parport_prologue(pp) < 0)
		return 0;
	read_mos_reg(mos_parport->serial, dummy, MOS7720_DPR, &d);
	parport_epilogue(pp);
	return d;
}

static void parport_mos7715_write_control(struct parport *pp, unsigned char d)
{
	struct mos7715_parport *mos_parport = pp->private_data;
	__u8 data;

	if (parport_prologue(pp) < 0)
		return;
	data = ((__u8)d & 0x0f) | (mos_parport->shadowDCR & 0xf0);
	write_mos_reg(mos_parport->serial, dummy, MOS7720_DCR, data);
	mos_parport->shadowDCR = data;
	parport_epilogue(pp);
}

static unsigned char parport_mos7715_read_control(struct parport *pp)
{
	struct mos7715_parport *mos_parport = pp->private_data;
	__u8 dcr;

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

	mask &= 0x0f;
	val &= 0x0f;
	if (parport_prologue(pp) < 0)
		return 0;
	mos_parport->shadowDCR = (mos_parport->shadowDCR & (~mask)) ^ val;
	write_mos_reg(mos_parport->serial, dummy, MOS7720_DCR,
		      mos_parport->shadowDCR);
	dcr = mos_parport->shadowDCR & 0x0f;
	parport_epilogue(pp);
	return dcr;
}

static unsigned char parport_mos7715_read_status(struct parport *pp)
{
	unsigned char status;
	struct mos7715_parport *mos_parport = pp->private_data;

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
}

static void parport_mos7715_disable_irq(struct parport *pp)
{
}

static void parport_mos7715_data_forward(struct parport *pp)
{
	struct mos7715_parport *mos_parport = pp->private_data;

	if (parport_prologue(pp) < 0)
		return;
	mos7715_change_mode(mos_parport, PS2);
	mos_parport->shadowDCR &=  ~0x20;
	write_mos_reg(mos_parport->serial, dummy, MOS7720_DCR,
		      mos_parport->shadowDCR);
	parport_epilogue(pp);
}

static void parport_mos7715_data_reverse(struct parport *pp)
{
	struct mos7715_parport *mos_parport = pp->private_data;

	if (parport_prologue(pp) < 0)
		return;
	mos7715_change_mode(mos_parport, PS2);
	mos_parport->shadowDCR |= 0x20;
	write_mos_reg(mos_parport->serial, dummy, MOS7720_DCR,
		      mos_parport->shadowDCR);
	parport_epilogue(pp);
}

static void parport_mos7715_init_state(struct pardevice *dev,
				       struct parport_state *s)
{
	s->u.pc.ctr = DCR_INIT_VAL;
	s->u.pc.ecr = ECR_INIT_VAL;
}

/* N.B. Parport core code requires that this function not block */
static void parport_mos7715_save_state(struct parport *pp,
				       struct parport_state *s)
{
	struct mos7715_parport *mos_parport;

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

	spin_lock(&release_lock);
	mos_parport = pp->private_data;
	if (unlikely(mos_parport == NULL)) {	/* release called */
		spin_unlock(&release_lock);
		return;
	}
	write_parport_reg_nonblock(mos_parport, MOS7720_DCR,
				   mos_parport->shadowDCR);
	write_parport_reg_nonblock(mos_parport, MOS7720_ECR,
				   mos_parport->shadowECR);
	spin_unlock(&release_lock);
}

static size_t parport_mos7715_write_compat(struct parport *pp,
					   const void *buffer,
					   size_t len, int flags)
{
	int retval;
	struct mos7715_parport *mos_parport = pp->private_data;
	int actual_len;

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
			"mos7720: usb_bulk_msg() failed: %d\n", retval);
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
	if (!mos_parport)
		return -ENOMEM;

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
	write_mos_reg(mos_parport->serial, dummy, MOS7720_PP_REG, (__u8)0x80);
	write_mos_reg(mos_parport->serial, dummy, MOS7720_PP_REG, (__u8)0x00);

	/* initialize device registers */
	mos_parport->shadowDCR = DCR_INIT_VAL;
	write_mos_reg(mos_parport->serial, dummy, MOS7720_DCR,
		      mos_parport->shadowDCR);
	mos_parport->shadowECR = ECR_INIT_VAL;
	write_mos_reg(mos_parport->serial, dummy, MOS7720_ECR,
		      mos_parport->shadowECR);

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
	struct device *dev = &urb->dev->dev;
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
		dev_dbg(dev, "%s - urb shutting down with status: %d\n", __func__, status);
		return;
	default:
		dev_dbg(dev, "%s - nonzero urb status received: %d\n", __func__, status);
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
		dev_dbg(dev, "Wrong data !!!\n");
		return;
	}

	sp1 = data[3];
	sp2 = data[2];

	if ((sp1 | sp2) & 0x01) {
		/* No Interrupt Pending in both the ports */
		dev_dbg(dev, "No Interrupt !!!\n");
	} else {
		switch (sp1 & 0x0f) {
		case SERIAL_IIR_RLS:
			dev_dbg(dev, "Serial Port 1: Receiver status error or address bit detected in 9-bit mode\n");
			break;
		case SERIAL_IIR_CTI:
			dev_dbg(dev, "Serial Port 1: Receiver time out\n");
			break;
		case SERIAL_IIR_MS:
			/* dev_dbg(dev, "Serial Port 1: Modem status change\n"); */
			break;
		}

		switch (sp2 & 0x0f) {
		case SERIAL_IIR_RLS:
			dev_dbg(dev, "Serial Port 2: Receiver status error or address bit detected in 9-bit mode\n");
			break;
		case SERIAL_IIR_CTI:
			dev_dbg(dev, "Serial Port 2: Receiver time out\n");
			break;
		case SERIAL_IIR_MS:
			/* dev_dbg(dev, "Serial Port 2: Modem status change\n"); */
			break;
		}
	}

exit:
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(dev, "%s - Error %d submitting control urb\n", __func__, result);
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
	struct device *dev = &urb->dev->dev;
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
		dev_dbg(dev, "%s - urb shutting down with status: %d\n", __func__, status);
		return;
	default:
		dev_dbg(dev, "%s - nonzero urb status received: %d\n", __func__, status);
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
		dev_dbg(dev, "Wrong data !!!\n");
		return;
	}

	iir = data[0];
	if (!(iir & 0x01)) {	/* serial port interrupt pending */
		switch (iir & 0x0f) {
		case SERIAL_IIR_RLS:
			dev_dbg(dev, "Serial Port: Receiver status error or address bit detected in 9-bit mode\n");
			break;
		case SERIAL_IIR_CTI:
			dev_dbg(dev, "Serial Port: Receiver time out\n");
			break;
		case SERIAL_IIR_MS:
			/* dev_dbg(dev, "Serial Port: Modem status change\n"); */
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
		dev_err(dev, "%s - Error %d submitting control urb\n", __func__, result);
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
	int status = urb->status;

	if (status) {
		dev_dbg(&urb->dev->dev, "nonzero read bulk status received: %d\n", status);
		return;
	}

	port = urb->context;

	dev_dbg(&port->dev, "Entering...%s\n", __func__);

	data = urb->transfer_buffer;

	if (urb->actual_length) {
		tty_insert_flip_string(&port->port, data, urb->actual_length);
		tty_flip_buffer_push(&port->port);
	}

	if (port->read_urb->status != -EINPROGRESS) {
		retval = usb_submit_urb(port->read_urb, GFP_ATOMIC);
		if (retval)
			dev_dbg(&port->dev, "usb_submit_urb(read bulk) failed, retval = %d\n", retval);
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
	int status = urb->status;

	if (status) {
		dev_dbg(&urb->dev->dev, "nonzero write bulk status received:%d\n", status);
		return;
	}

	mos7720_port = urb->context;
	if (!mos7720_port) {
		dev_dbg(&urb->dev->dev, "NULL mos7720_port pointer\n");
		return ;
	}

	if (mos7720_port->open)
		tty_port_tty_wakeup(&mos7720_port->port->port);
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
		if (!urb)
			continue;

		urb->transfer_buffer = kmalloc(URB_TRANSFER_BUFFER_SIZE,
					       GFP_KERNEL);
		if (!urb->transfer_buffer) {
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
	  * 0 : MOS7720_THR/MOS7720_RHR
	  * 1 : MOS7720_IER
	  * 2 : MOS7720_FCR
	  * 3 : MOS7720_LCR
	  * 4 : MOS7720_MCR
	  * 5 : MOS7720_LSR
	  * 6 : MOS7720_MSR
	  * 7 : MOS7720_SPR
	  *
	  * 0x08 : SP1/2 Control Reg
	  */
	port_number = port->port_number;
	read_mos_reg(serial, port_number, MOS7720_LSR, &data);

	dev_dbg(&port->dev, "SS::%p LSR:%x\n", mos7720_port, data);

	write_mos_reg(serial, dummy, MOS7720_SP1_REG, 0x02);
	write_mos_reg(serial, dummy, MOS7720_SP2_REG, 0x02);

	write_mos_reg(serial, port_number, MOS7720_IER, 0x00);
	write_mos_reg(serial, port_number, MOS7720_FCR, 0x00);

	write_mos_reg(serial, port_number, MOS7720_FCR, 0xcf);
	mos7720_port->shadowLCR = 0x03;
	write_mos_reg(serial, port_number, MOS7720_LCR,
		      mos7720_port->shadowLCR);
	mos7720_port->shadowMCR = 0x0b;
	write_mos_reg(serial, port_number, MOS7720_MCR,
		      mos7720_port->shadowMCR);

	write_mos_reg(serial, port_number, MOS7720_SP_CONTROL_REG, 0x00);
	read_mos_reg(serial, dummy, MOS7720_SP_CONTROL_REG, &data);
	data = data | (port->port_number + 1);
	write_mos_reg(serial, dummy, MOS7720_SP_CONTROL_REG, data);
	mos7720_port->shadowLCR = 0x83;
	write_mos_reg(serial, port_number, MOS7720_LCR,
		      mos7720_port->shadowLCR);
	write_mos_reg(serial, port_number, MOS7720_THR, 0x0c);
	write_mos_reg(serial, port_number, MOS7720_IER, 0x00);
	mos7720_port->shadowLCR = 0x03;
	write_mos_reg(serial, port_number, MOS7720_LCR,
		      mos7720_port->shadowLCR);
	write_mos_reg(serial, port_number, MOS7720_IER, 0x0c);

	response = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (response)
		dev_err(&port->dev, "%s - Error %d submitting read urb\n",
							__func__, response);

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

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL)
		return 0;

	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7720_port->write_urb_pool[i] &&
		    mos7720_port->write_urb_pool[i]->status == -EINPROGRESS)
			chars += URB_TRANSFER_BUFFER_SIZE;
	}
	dev_dbg(&port->dev, "%s - returns %d\n", __func__, chars);
	return chars;
}

static void mos7720_close(struct usb_serial_port *port)
{
	struct usb_serial *serial;
	struct moschip_port *mos7720_port;
	int j;

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
	usb_kill_urb(port->write_urb);
	usb_kill_urb(port->read_urb);

	write_mos_reg(serial, port->port_number, MOS7720_MCR, 0x00);
	write_mos_reg(serial, port->port_number, MOS7720_IER, 0x00);

	mos7720_port->open = 0;
}

static void mos7720_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned char data;
	struct usb_serial *serial;
	struct moschip_port *mos7720_port;

	serial = port->serial;

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL)
		return;

	if (break_state == -1)
		data = mos7720_port->shadowLCR | UART_LCR_SBC;
	else
		data = mos7720_port->shadowLCR & ~UART_LCR_SBC;

	mos7720_port->shadowLCR  = data;
	write_mos_reg(serial, port->port_number, MOS7720_LCR,
		      mos7720_port->shadowLCR);
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

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL)
		return -ENODEV;

	/* FIXME: Locking */
	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7720_port->write_urb_pool[i] &&
		    mos7720_port->write_urb_pool[i]->status != -EINPROGRESS)
			room += URB_TRANSFER_BUFFER_SIZE;
	}

	dev_dbg(&port->dev, "%s - returns %d\n", __func__, room);
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

	serial = port->serial;

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL)
		return -ENODEV;

	/* try to find a free urb in the list */
	urb = NULL;

	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7720_port->write_urb_pool[i] &&
		    mos7720_port->write_urb_pool[i]->status != -EINPROGRESS) {
			urb = mos7720_port->write_urb_pool[i];
			dev_dbg(&port->dev, "URB:%d\n", i);
			break;
		}
	}

	if (urb == NULL) {
		dev_dbg(&port->dev, "%s - no more free urbs\n", __func__);
		goto exit;
	}

	if (urb->transfer_buffer == NULL) {
		urb->transfer_buffer = kmalloc(URB_TRANSFER_BUFFER_SIZE,
					       GFP_ATOMIC);
		if (!urb->transfer_buffer)
			goto exit;
	}
	transfer_size = min(count, URB_TRANSFER_BUFFER_SIZE);

	memcpy(urb->transfer_buffer, current_position, transfer_size);
	usb_serial_debug_data(&port->dev, __func__, transfer_size,
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

	mos7720_port = usb_get_serial_port_data(port);

	if (mos7720_port == NULL)
		return;

	if (!mos7720_port->open) {
		dev_dbg(&port->dev, "%s - port not opened\n", __func__);
		return;
	}

	/* if we are implementing XON/XOFF, send the stop character */
	if (I_IXOFF(tty)) {
		unsigned char stop_char = STOP_CHAR(tty);
		status = mos7720_write(tty, port, &stop_char, 1);
		if (status <= 0)
			return;
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (C_CRTSCTS(tty)) {
		mos7720_port->shadowMCR &= ~UART_MCR_RTS;
		write_mos_reg(port->serial, port->port_number, MOS7720_MCR,
			      mos7720_port->shadowMCR);
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
		dev_dbg(&port->dev, "%s - port not opened\n", __func__);
		return;
	}

	/* if we are implementing XON/XOFF, send the start character */
	if (I_IXOFF(tty)) {
		unsigned char start_char = START_CHAR(tty);
		status = mos7720_write(tty, port, &start_char, 1);
		if (status <= 0)
			return;
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (C_CRTSCTS(tty)) {
		mos7720_port->shadowMCR |= UART_MCR_RTS;
		write_mos_reg(port->serial, port->port_number, MOS7720_MCR,
			      mos7720_port->shadowMCR);
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
	dev_dbg(&port->dev, "Sending Setting Commands ..........\n");
	port_number = port->port_number;

	write_mos_reg(serial, port_number, MOS7720_IER, 0x00);
	write_mos_reg(serial, port_number, MOS7720_FCR, 0x00);
	write_mos_reg(serial, port_number, MOS7720_FCR, 0xcf);
	mos7720_port->shadowMCR = 0x0b;
	write_mos_reg(serial, port_number, MOS7720_MCR,
		      mos7720_port->shadowMCR);
	write_mos_reg(serial, dummy, MOS7720_SP_CONTROL_REG, 0x00);

	/***********************************************
	 *              Set for higher rates           *
	 ***********************************************/
	/* writing baud rate verbatum into uart clock field clearly not right */
	if (port_number == 0)
		sp_reg = MOS7720_SP1_REG;
	else
		sp_reg = MOS7720_SP2_REG;
	write_mos_reg(serial, dummy, sp_reg, baud * 0x10);
	write_mos_reg(serial, dummy, MOS7720_SP_CONTROL_REG, 0x03);
	mos7720_port->shadowMCR = 0x2b;
	write_mos_reg(serial, port_number, MOS7720_MCR,
		      mos7720_port->shadowMCR);

	/***********************************************
	 *              Set DLL/DLM
	 ***********************************************/
	mos7720_port->shadowLCR = mos7720_port->shadowLCR | UART_LCR_DLAB;
	write_mos_reg(serial, port_number, MOS7720_LCR,
		      mos7720_port->shadowLCR);
	write_mos_reg(serial, port_number, MOS7720_DLL, 0x01);
	write_mos_reg(serial, port_number, MOS7720_DLM, 0x00);
	mos7720_port->shadowLCR = mos7720_port->shadowLCR & ~UART_LCR_DLAB;
	write_mos_reg(serial, port_number, MOS7720_LCR,
		      mos7720_port->shadowLCR);

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
static int calc_baud_rate_divisor(struct usb_serial_port *port, int baudrate, int *divisor)
{
	int i;
	__u16 custom;
	__u16 round1;
	__u16 round;


	dev_dbg(&port->dev, "%s - %d\n", __func__, baudrate);

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

		dev_dbg(&port->dev, "Baud %d = %d\n", baudrate, custom);
		return 0;
	}

	dev_dbg(&port->dev, "Baud calculation Failed...\n");
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

	number = port->port_number;
	dev_dbg(&port->dev, "%s - baud = %d\n", __func__, baudrate);

	/* Calculate the Divisor */
	status = calc_baud_rate_divisor(port, baudrate, &divisor);
	if (status) {
		dev_err(&port->dev, "%s - bad baud rate\n", __func__);
		return status;
	}

	/* Enable access to divisor latch */
	mos7720_port->shadowLCR = mos7720_port->shadowLCR | UART_LCR_DLAB;
	write_mos_reg(serial, number, MOS7720_LCR, mos7720_port->shadowLCR);

	/* Write the divisor */
	write_mos_reg(serial, number, MOS7720_DLL, (__u8)(divisor & 0xff));
	write_mos_reg(serial, number, MOS7720_DLM,
		      (__u8)((divisor & 0xff00) >> 8));

	/* Disable access to divisor latch */
	mos7720_port->shadowLCR = mos7720_port->shadowLCR & ~UART_LCR_DLAB;
	write_mos_reg(serial, number, MOS7720_LCR, mos7720_port->shadowLCR);

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
	port_number = port->port_number;

	if (!mos7720_port->open) {
		dev_dbg(&port->dev, "%s - port not opened\n", __func__);
		return;
	}

	lData = UART_LCR_WLEN8;
	lStop = 0x00;	/* 1 stop bit */
	lParity = 0x00;	/* No parity */

	cflag = tty->termios.c_cflag;
	iflag = tty->termios.c_iflag;

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
			dev_dbg(&port->dev, "%s - parity = odd\n", __func__);
		} else {
			lParity = (UART_LCR_EPAR | UART_LCR_PARITY);
			dev_dbg(&port->dev, "%s - parity = even\n", __func__);
		}

	} else {
		dev_dbg(&port->dev, "%s - parity = none\n", __func__);
	}

	if (cflag & CMSPAR)
		lParity = lParity | 0x20;

	/* Change the Stop bit */
	if (cflag & CSTOPB) {
		lStop = UART_LCR_STOP;
		dev_dbg(&port->dev, "%s - stop bits = 2\n", __func__);
	} else {
		lStop = 0x00;
		dev_dbg(&port->dev, "%s - stop bits = 1\n", __func__);
	}

#define LCR_BITS_MASK		0x03	/* Mask for bits/char field */
#define LCR_STOP_MASK		0x04	/* Mask for stop bits field */
#define LCR_PAR_MASK		0x38	/* Mask for parity field */

	/* Update the LCR with the correct value */
	mos7720_port->shadowLCR &=
		~(LCR_BITS_MASK | LCR_STOP_MASK | LCR_PAR_MASK);
	mos7720_port->shadowLCR |= (lData | lParity | lStop);


	/* Disable Interrupts */
	write_mos_reg(serial, port_number, MOS7720_IER, 0x00);
	write_mos_reg(serial, port_number, MOS7720_FCR, 0x00);
	write_mos_reg(serial, port_number, MOS7720_FCR, 0xcf);

	/* Send the updated LCR value to the mos7720 */
	write_mos_reg(serial, port_number, MOS7720_LCR,
		      mos7720_port->shadowLCR);
	mos7720_port->shadowMCR = 0x0b;
	write_mos_reg(serial, port_number, MOS7720_MCR,
		      mos7720_port->shadowMCR);

	/* set up the MCR register and send it to the mos7720 */
	mos7720_port->shadowMCR = UART_MCR_OUT2;
	if (cflag & CBAUD)
		mos7720_port->shadowMCR |= (UART_MCR_DTR | UART_MCR_RTS);

	if (cflag & CRTSCTS) {
		mos7720_port->shadowMCR |= (UART_MCR_XONANY);
		/* To set hardware flow control to the specified *
		 * serial port, in SP1/2_CONTROL_REG             */
		if (port_number)
			write_mos_reg(serial, dummy, MOS7720_SP_CONTROL_REG,
				      0x01);
		else
			write_mos_reg(serial, dummy, MOS7720_SP_CONTROL_REG,
				      0x02);

	} else
		mos7720_port->shadowMCR &= ~(UART_MCR_XONANY);

	write_mos_reg(serial, port_number, MOS7720_MCR,
		      mos7720_port->shadowMCR);

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(tty);
	if (!baud) {
		/* pick a default, any default... */
		dev_dbg(&port->dev, "Picked default baud...\n");
		baud = 9600;
	}

	if (baud >= 230400) {
		set_higher_rates(mos7720_port, baud);
		/* Enable Interrupts */
		write_mos_reg(serial, port_number, MOS7720_IER, 0x0c);
		return;
	}

	dev_dbg(&port->dev, "%s - baud rate = %d\n", __func__, baud);
	status = send_cmd_write_baud_rate(mos7720_port, baud);
	/* FIXME: needs to write actual resulting baud back not just
	   blindly do so */
	if (cflag & CBAUD)
		tty_encode_baud_rate(tty, baud, baud);
	/* Enable Interrupts */
	write_mos_reg(serial, port_number, MOS7720_IER, 0x0c);

	if (port->read_urb->status != -EINPROGRESS) {
		status = usb_submit_urb(port->read_urb, GFP_KERNEL);
		if (status)
			dev_dbg(&port->dev, "usb_submit_urb(read bulk) failed, status = %d\n", status);
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
		dev_dbg(&port->dev, "%s - port not opened\n", __func__);
		return;
	}

	dev_dbg(&port->dev, "setting termios - ASPIRE\n");

	cflag = tty->termios.c_cflag;

	dev_dbg(&port->dev, "%s - cflag %08x iflag %08x\n", __func__,
		tty->termios.c_cflag, RELEVANT_IFLAG(tty->termios.c_iflag));

	dev_dbg(&port->dev, "%s - old cflag %08x old iflag %08x\n", __func__,
		old_termios->c_cflag, RELEVANT_IFLAG(old_termios->c_iflag));

	/* change the port settings to the new ones specified */
	change_port_settings(tty, mos7720_port, old_termios);

	if (port->read_urb->status != -EINPROGRESS) {
		status = usb_submit_urb(port->read_urb, GFP_KERNEL);
		if (status)
			dev_dbg(&port->dev, "usb_submit_urb(read bulk) failed, status = %d\n", status);
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
	int port_number = port->port_number;
	int count;

	count = mos7720_chars_in_buffer(tty);
	if (count == 0) {
		read_mos_reg(port->serial, port_number, MOS7720_LSR, &data);
		if ((data & (UART_LSR_TEMT | UART_LSR_THRE))
					== (UART_LSR_TEMT | UART_LSR_THRE)) {
			dev_dbg(&port->dev, "%s -- Empty\n", __func__);
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

	mcr = mos7720_port->shadowMCR;
	msr = mos7720_port->shadowMSR;

	result = ((mcr & UART_MCR_DTR)  ? TIOCM_DTR : 0)   /* 0x002 */
	  | ((mcr & UART_MCR_RTS)   ? TIOCM_RTS : 0)   /* 0x004 */
	  | ((msr & UART_MSR_CTS)   ? TIOCM_CTS : 0)   /* 0x020 */
	  | ((msr & UART_MSR_DCD)   ? TIOCM_CAR : 0)   /* 0x040 */
	  | ((msr & UART_MSR_RI)    ? TIOCM_RI :  0)   /* 0x080 */
	  | ((msr & UART_MSR_DSR)   ? TIOCM_DSR : 0);  /* 0x100 */

	return result;
}

static int mos7720_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7720_port = usb_get_serial_port_data(port);
	unsigned int mcr ;

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
	write_mos_reg(port->serial, port->port_number, MOS7720_MCR,
		      mos7720_port->shadowMCR);

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
	write_mos_reg(port->serial, port->port_number, MOS7720_MCR,
		      mos7720_port->shadowMCR);

	return 0;
}

static int get_serial_info(struct moschip_port *mos7720_port,
			   struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;

	memset(&tmp, 0, sizeof(tmp));

	tmp.type		= PORT_16550A;
	tmp.line		= mos7720_port->port->minor;
	tmp.port		= mos7720_port->port->port_number;
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

	mos7720_port = usb_get_serial_port_data(port);
	if (mos7720_port == NULL)
		return -ENODEV;

	switch (cmd) {
	case TIOCSERGETLSR:
		dev_dbg(&port->dev, "%s TIOCSERGETLSR\n", __func__);
		return get_lsr_info(tty, mos7720_port,
					(unsigned int __user *)arg);

	/* FIXME: These should be using the mode methods */
	case TIOCMBIS:
	case TIOCMBIC:
		dev_dbg(&port->dev, "%s TIOCMSET/TIOCMBIC/TIOCMSET\n", __func__);
		return set_modem_info(mos7720_port, cmd,
				      (unsigned int __user *)arg);

	case TIOCGSERIAL:
		dev_dbg(&port->dev, "%s TIOCGSERIAL\n", __func__);
		return get_serial_info(mos7720_port,
				       (struct serial_struct __user *)arg);
	}

	return -ENOIOCTLCMD;
}

static int mos7720_startup(struct usb_serial *serial)
{
	struct usb_device *dev;
	char data;
	u16 product;
	int ret_val;

	if (serial->num_bulk_in < 2 || serial->num_bulk_out < 2) {
		dev_err(&serial->interface->dev, "missing bulk endpoints\n");
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

		if (serial->port[0]->interrupt_in_urb) {
			struct urb *urb = serial->port[0]->interrupt_in_urb;

			urb->complete = mos7715_interrupt_callback;
		}
	}

	/* setting configuration feature to one */
	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			(__u8)0x03, 0x00, 0x01, 0x00, NULL, 0x00, 5000);

#ifdef CONFIG_USB_SERIAL_MOS7715_PARPORT
	if (product == MOSCHIP_DEVICE_ID_7715) {
		ret_val = mos7715_parport_init(serial);
		if (ret_val < 0)
			return ret_val;
	}
#endif
	/* start the interrupt urb */
	ret_val = usb_submit_urb(serial->port[0]->interrupt_in_urb, GFP_KERNEL);
	if (ret_val) {
		dev_err(&dev->dev, "failed to submit interrupt urb: %d\n",
			ret_val);
	}

	/* LSR For Port 1 */
	read_mos_reg(serial, 0, MOS7720_LSR, &data);
	dev_dbg(&dev->dev, "LSR:%x\n", data);

	return 0;
}

static void mos7720_release(struct usb_serial *serial)
{
	usb_kill_urb(serial->port[0]->interrupt_in_urb);

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
		parport_del_port(mos_parport->pp);

		kref_put(&mos_parport->ref_count, destroy_mos_parport);
	}
#endif
}

static int mos7720_port_probe(struct usb_serial_port *port)
{
	struct moschip_port *mos7720_port;

	mos7720_port = kzalloc(sizeof(*mos7720_port), GFP_KERNEL);
	if (!mos7720_port)
		return -ENOMEM;

	mos7720_port->port = port;

	usb_set_serial_port_data(port, mos7720_port);

	return 0;
}

static int mos7720_port_remove(struct usb_serial_port *port)
{
	struct moschip_port *mos7720_port;

	mos7720_port = usb_get_serial_port_data(port);
	kfree(mos7720_port);

	return 0;
}

static struct usb_serial_driver moschip7720_2port_driver = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"moschip7720",
	},
	.description		= "Moschip 2 port adapter",
	.id_table		= id_table,
	.calc_num_ports		= mos77xx_calc_num_ports,
	.open			= mos7720_open,
	.close			= mos7720_close,
	.throttle		= mos7720_throttle,
	.unthrottle		= mos7720_unthrottle,
	.attach			= mos7720_startup,
	.release		= mos7720_release,
	.port_probe		= mos7720_port_probe,
	.port_remove		= mos7720_port_remove,
	.ioctl			= mos7720_ioctl,
	.tiocmget		= mos7720_tiocmget,
	.tiocmset		= mos7720_tiocmset,
	.set_termios		= mos7720_set_termios,
	.write			= mos7720_write,
	.write_room		= mos7720_write_room,
	.chars_in_buffer	= mos7720_chars_in_buffer,
	.break_ctl		= mos7720_break,
	.read_bulk_callback	= mos7720_bulk_in_callback,
	.read_int_callback	= mos7720_interrupt_callback,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&moschip7720_2port_driver, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
