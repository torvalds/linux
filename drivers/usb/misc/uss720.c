/*****************************************************************************/

/*
 *	uss720.c  --  USS720 USB Parport Cable.
 *
 *	Copyright (C) 1999
 *	    Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Based on parport_pc.c
 *
 *  History:
 *   0.1  04.08.99  Created
 *   0.2  07.08.99  Some fixes mainly suggested by Tim Waugh
 *		    Interrupt handling currently disabled because
 *		    usb_request_irq crashes somewhere within ohci.c
 *		    for no apparent reason (that is for me, anyway)
 *		    ECP currently untested
 *   0.3  10.08.99  fixing merge errors
 *   0.4  13.08.99  Added Vendor/Product ID of Brad Hard's cable
 *   0.5  20.09.99  usb_control_msg wrapper used
 *        Nov01.00  usb_device_table support by Adam J. Richter
 *        08.04.01  Identify version on module load.  gb
 *
 */

/*****************************************************************************/

#include <linux/module.h>
#include <linux/socket.h>
#include <linux/parport.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/delay.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.5"
#define DRIVER_AUTHOR "Thomas M. Sailer, sailer@ife.ee.ethz.ch"
#define DRIVER_DESC "USB Parport Cable driver for Cables using the Lucent Technologies USS720 Chip"

/* --------------------------------------------------------------------- */

struct parport_uss720_private {
	struct usb_device *usbdev;
	void *irqhandle;
	unsigned int irqpipe;
	unsigned char reg[7];  /* USB registers */
};

/* --------------------------------------------------------------------- */

static int get_1284_register(struct parport *pp, unsigned char reg, unsigned char *val)
{
	struct parport_uss720_private *priv = pp->private_data;
	struct usb_device *usbdev = priv->usbdev;
	static const unsigned char regindex[9] = {
		4, 0, 1, 5, 5, 0, 2, 3, 6
	};
	int ret;

	if (!usbdev)
		return -1;
	ret = usb_control_msg(usbdev, usb_rcvctrlpipe(usbdev,0), 3, 0xc0, ((unsigned int)reg) << 8, 0, priv->reg, 7, 1000);
	if (ret != 7) {
		printk(KERN_DEBUG "uss720: get_1284_register(%d) failed, status 0x%x expected 7\n",
		       (unsigned int)reg, ret);
		ret = -1;
	} else {
#if 0
		printk(KERN_DEBUG "uss720: get_1284_register(%d) return %02x %02x %02x %02x %02x %02x %02x\n",
		       (unsigned int)reg, (unsigned int)priv->reg[0], (unsigned int)priv->reg[1],
		       (unsigned int)priv->reg[2], (unsigned int)priv->reg[3], (unsigned int)priv->reg[4],
		       (unsigned int)priv->reg[5], (unsigned int)priv->reg[6]);
#endif
		/* if nAck interrupts are enabled and we have an interrupt, call the interrupt procedure */
		if (priv->reg[2] & priv->reg[1] & 0x10)
			parport_generic_irq(0, pp, NULL);
		ret = 0;
	}
	if (val)
		*val = priv->reg[(reg >= 9) ? 0 : regindex[reg]];
	return ret;
}

static int set_1284_register(struct parport *pp, unsigned char reg, unsigned char val)
{
	struct parport_uss720_private *priv = pp->private_data;
	struct usb_device *usbdev = priv->usbdev;
	int ret;

	if (!usbdev)
		return -1;
	ret = usb_control_msg(usbdev, usb_sndctrlpipe(usbdev,0), 4, 0x40, (((unsigned int)reg) << 8) | val, 0, NULL, 0, 1000);
	if (ret) {
		printk(KERN_DEBUG "uss720: set_1284_register(%u,0x%02x) failed, status 0x%x\n", 
		       (unsigned int)reg, (unsigned int)val, ret);
	} else {
#if 0
		printk(KERN_DEBUG "uss720: set_1284_register(%u,0x%02x)\n", 
		       (unsigned int)reg, (unsigned int)val);
#endif
	}
	return ret;
}

/* --------------------------------------------------------------------- */

/* ECR modes */
#define ECR_SPP 00
#define ECR_PS2 01
#define ECR_PPF 02
#define ECR_ECP 03
#define ECR_EPP 04

/* Safely change the mode bits in the ECR */
static int change_mode(struct parport *pp, int m)
{
	struct parport_uss720_private *priv = pp->private_data;
	int mode;

	if (get_1284_register(pp, 6, NULL))
		return -EIO;
	/* Bits <7:5> contain the mode. */
	mode = (priv->reg[2] >> 5) & 0x7;
	if (mode == m)
		return 0;
	/* We have to go through mode 000 or 001 */
	if (mode > ECR_PS2 && m > ECR_PS2)
		if (change_mode(pp, ECR_PS2))
			return -EIO;

	if (m <= ECR_PS2 && !(priv->reg[1] & 0x20)) {
		/* This mode resets the FIFO, so we may
		 * have to wait for it to drain first. */
		unsigned long expire = jiffies + pp->physport->cad->timeout;
		switch (mode) {
		case ECR_PPF: /* Parallel Port FIFO mode */
		case ECR_ECP: /* ECP Parallel Port mode */
			/* Poll slowly. */
			for (;;) {
				if (get_1284_register(pp, 6, NULL))
					return -EIO;
				if (priv->reg[2] & 0x01)
					break;
				if (time_after_eq (jiffies, expire))
					/* The FIFO is stuck. */
					return -EBUSY;
				msleep_interruptible(10);
				if (signal_pending (current))
					break;
			}
		}
	}
	/* Set the mode. */
	if (set_1284_register(pp, 6, m << 5))
		return -EIO;
	return 0;
}

/*
 * Clear TIMEOUT BIT in EPP MODE
 */
static int clear_epp_timeout(struct parport *pp)
{
	unsigned char stat;

	if (get_1284_register(pp, 1, &stat))
		return 1;
	return stat & 1;
}

/*
 * Access functions.
 */
#if 0
static int uss720_irq(int usbstatus, void *buffer, int len, void *dev_id)
{
	struct parport *pp = (struct parport *)dev_id;
	struct parport_uss720_private *priv = pp->private_data;	

	if (usbstatus != 0 || len < 4 || !buffer)
		return 1;
	memcpy(priv->reg, buffer, 4);
	/* if nAck interrupts are enabled and we have an interrupt, call the interrupt procedure */
	if (priv->reg[2] & priv->reg[1] & 0x10)
		parport_generic_irq(0, pp, NULL);
	return 1;
}
#endif

static void parport_uss720_write_data(struct parport *pp, unsigned char d)
{
	set_1284_register(pp, 0, d);
}

static unsigned char parport_uss720_read_data(struct parport *pp)
{
	unsigned char ret;

	if (get_1284_register(pp, 0, &ret))
		return 0;
	return ret;
}

static void parport_uss720_write_control(struct parport *pp, unsigned char d)
{
	struct parport_uss720_private *priv = pp->private_data;	

	d = (d & 0xf) | (priv->reg[1] & 0xf0);
	if (set_1284_register(pp, 2, d))
		return;
	priv->reg[1] = d;
}

static unsigned char parport_uss720_read_control(struct parport *pp)
{
	struct parport_uss720_private *priv = pp->private_data;	
	return priv->reg[1] & 0xf; /* Use soft copy */
}

static unsigned char parport_uss720_frob_control(struct parport *pp, unsigned char mask, unsigned char val)
{
	struct parport_uss720_private *priv = pp->private_data;	
	unsigned char d;

	mask &= 0x0f;
	val &= 0x0f;
	d = (priv->reg[1] & (~mask)) ^ val;
	if (set_1284_register(pp, 2, d))
		return 0;
	priv->reg[1] = d;
	return d & 0xf;
}

static unsigned char parport_uss720_read_status(struct parport *pp)
{
	unsigned char ret;

	if (get_1284_register(pp, 1, &ret))
		return 0;
	return ret & 0xf8;
}

static void parport_uss720_disable_irq(struct parport *pp)
{
	struct parport_uss720_private *priv = pp->private_data;	
	unsigned char d;

	d = priv->reg[1] & ~0x10;
	if (set_1284_register(pp, 2, d))
		return;
	priv->reg[1] = d;
}

static void parport_uss720_enable_irq(struct parport *pp)
{
	struct parport_uss720_private *priv = pp->private_data;	
	unsigned char d;

	d = priv->reg[1] | 0x10;
	if (set_1284_register(pp, 2, d))
		return;
	priv->reg[1] = d;
}

static void parport_uss720_data_forward (struct parport *pp)
{
	struct parport_uss720_private *priv = pp->private_data;	
	unsigned char d;

	d = priv->reg[1] & ~0x20;
	if (set_1284_register(pp, 2, d))
		return;
	priv->reg[1] = d;
}

static void parport_uss720_data_reverse (struct parport *pp)
{
	struct parport_uss720_private *priv = pp->private_data;	
	unsigned char d;

	d = priv->reg[1] | 0x20;
	if (set_1284_register(pp, 2, d))
		return;
	priv->reg[1] = d;
}

static void parport_uss720_init_state(struct pardevice *dev, struct parport_state *s)
{
	s->u.pc.ctr = 0xc | (dev->irq_func ? 0x10 : 0x0);
	s->u.pc.ecr = 0x24;
}

static void parport_uss720_save_state(struct parport *pp, struct parport_state *s)
{
	struct parport_uss720_private *priv = pp->private_data;	

	if (get_1284_register(pp, 2, NULL))
		return;
	s->u.pc.ctr = priv->reg[1];
	s->u.pc.ecr = priv->reg[2];
}

static void parport_uss720_restore_state(struct parport *pp, struct parport_state *s)
{
	set_1284_register(pp, 2, s->u.pc.ctr);
	set_1284_register(pp, 6, s->u.pc.ecr);
	get_1284_register(pp, 2, NULL);
}

static size_t parport_uss720_epp_read_data(struct parport *pp, void *buf, size_t length, int flags)
{
	struct parport_uss720_private *priv = pp->private_data;	
	size_t got = 0;

	if (change_mode(pp, ECR_EPP))
		return 0;
	for (; got < length; got++) {
		if (get_1284_register(pp, 4, (char *)buf))
			break;
		buf++;
		if (priv->reg[0] & 0x01) {
			clear_epp_timeout(pp);
			break;
		}
	}
	change_mode(pp, ECR_PS2);
	return got;
}

static size_t parport_uss720_epp_write_data(struct parport *pp, const void *buf, size_t length, int flags)
{
#if 0
	struct parport_uss720_private *priv = pp->private_data;	
	size_t written = 0;

	if (change_mode(pp, ECR_EPP))
		return 0;
	for (; written < length; written++) {
		if (set_1284_register(pp, 4, (char *)buf))
			break;
		((char*)buf)++;
		if (get_1284_register(pp, 1, NULL))
			break;
		if (priv->reg[0] & 0x01) {
			clear_epp_timeout(pp);
			break;
		}
	}
	change_mode(pp, ECR_PS2);
	return written;
#else
	struct parport_uss720_private *priv = pp->private_data;
	struct usb_device *usbdev = priv->usbdev;
	int rlen;
	int i;

	if (!usbdev)
		return 0;
	if (change_mode(pp, ECR_EPP))
		return 0;
	i = usb_bulk_msg(usbdev, usb_sndbulkpipe(usbdev, 1), (void *)buf, length, &rlen, 20000);
	if (i)
		printk(KERN_ERR "uss720: sendbulk ep 1 buf %p len %Zu rlen %u\n", buf, length, rlen);
	change_mode(pp, ECR_PS2);
	return rlen;
#endif
}

static size_t parport_uss720_epp_read_addr(struct parport *pp, void *buf, size_t length, int flags)
{
	struct parport_uss720_private *priv = pp->private_data;	
	size_t got = 0;

	if (change_mode(pp, ECR_EPP))
		return 0;
	for (; got < length; got++) {
		if (get_1284_register(pp, 3, (char *)buf))
			break;
		buf++;
		if (priv->reg[0] & 0x01) {
			clear_epp_timeout(pp);
			break;
		}
	}
	change_mode(pp, ECR_PS2);
	return got;
}

static size_t parport_uss720_epp_write_addr(struct parport *pp, const void *buf, size_t length, int flags)
{
	struct parport_uss720_private *priv = pp->private_data;	
	size_t written = 0;

	if (change_mode(pp, ECR_EPP))
		return 0;
	for (; written < length; written++) {
		if (set_1284_register(pp, 3, *(char *)buf))
			break;
		buf++;
		if (get_1284_register(pp, 1, NULL))
			break;
		if (priv->reg[0] & 0x01) {
			clear_epp_timeout(pp);
			break;
		}
	}
	change_mode(pp, ECR_PS2);
	return written;
}

static size_t parport_uss720_ecp_write_data(struct parport *pp, const void *buffer, size_t len, int flags)
{
	struct parport_uss720_private *priv = pp->private_data;
	struct usb_device *usbdev = priv->usbdev;
	int rlen;
	int i;

	if (!usbdev)
		return 0;
	if (change_mode(pp, ECR_ECP))
		return 0;
	i = usb_bulk_msg(usbdev, usb_sndbulkpipe(usbdev, 1), (void *)buffer, len, &rlen, 20000);
	if (i)
		printk(KERN_ERR "uss720: sendbulk ep 1 buf %p len %Zu rlen %u\n", buffer, len, rlen);
	change_mode(pp, ECR_PS2);
	return rlen;
}

static size_t parport_uss720_ecp_read_data(struct parport *pp, void *buffer, size_t len, int flags)
{
	struct parport_uss720_private *priv = pp->private_data;
	struct usb_device *usbdev = priv->usbdev;
	int rlen;
	int i;

	if (!usbdev)
		return 0;
	if (change_mode(pp, ECR_ECP))
		return 0;
	i = usb_bulk_msg(usbdev, usb_rcvbulkpipe(usbdev, 2), buffer, len, &rlen, 20000);
	if (i)
		printk(KERN_ERR "uss720: recvbulk ep 2 buf %p len %Zu rlen %u\n", buffer, len, rlen);
	change_mode(pp, ECR_PS2);
	return rlen;
}

static size_t parport_uss720_ecp_write_addr(struct parport *pp, const void *buffer, size_t len, int flags)
{
	size_t written = 0;

	if (change_mode(pp, ECR_ECP))
		return 0;
	for (; written < len; written++) {
		if (set_1284_register(pp, 5, *(char *)buffer))
			break;
		buffer++;
	}
	change_mode(pp, ECR_PS2);
	return written;
}

static size_t parport_uss720_write_compat(struct parport *pp, const void *buffer, size_t len, int flags)
{
	struct parport_uss720_private *priv = pp->private_data;
	struct usb_device *usbdev = priv->usbdev;
	int rlen;
	int i;

	if (!usbdev)
		return 0;
	if (change_mode(pp, ECR_PPF))
		return 0;
	i = usb_bulk_msg(usbdev, usb_sndbulkpipe(usbdev, 1), (void *)buffer, len, &rlen, 20000);
	if (i)
		printk(KERN_ERR "uss720: sendbulk ep 1 buf %p len %Zu rlen %u\n", buffer, len, rlen);
	change_mode(pp, ECR_PS2);
	return rlen;
}

/* --------------------------------------------------------------------- */

static struct parport_operations parport_uss720_ops = 
{
	.owner =		THIS_MODULE,
	.write_data =		parport_uss720_write_data,
	.read_data =		parport_uss720_read_data,

	.write_control =	parport_uss720_write_control,
	.read_control =		parport_uss720_read_control,
	.frob_control =		parport_uss720_frob_control,

	.read_status =		parport_uss720_read_status,

	.enable_irq =		parport_uss720_enable_irq,
	.disable_irq =		parport_uss720_disable_irq,

	.data_forward =		parport_uss720_data_forward,
	.data_reverse =		parport_uss720_data_reverse,

	.init_state =		parport_uss720_init_state,
	.save_state =		parport_uss720_save_state,
	.restore_state =	parport_uss720_restore_state,

	.epp_write_data =	parport_uss720_epp_write_data,
	.epp_read_data =	parport_uss720_epp_read_data,
	.epp_write_addr =	parport_uss720_epp_write_addr,
	.epp_read_addr =	parport_uss720_epp_read_addr,

	.ecp_write_data =	parport_uss720_ecp_write_data,
	.ecp_read_data =	parport_uss720_ecp_read_data,
	.ecp_write_addr =	parport_uss720_ecp_write_addr,

	.compat_write_data =	parport_uss720_write_compat,
	.nibble_read_data =	parport_ieee1284_read_nibble,
	.byte_read_data =	parport_ieee1284_read_byte,
};

/* --------------------------------------------------------------------- */

static int uss720_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *usbdev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_host_endpoint *endpoint;
	struct parport_uss720_private *priv;
	struct parport *pp;
	int i;

	printk(KERN_DEBUG "uss720: probe: vendor id 0x%x, device id 0x%x\n",
	       le16_to_cpu(usbdev->descriptor.idVendor),
	       le16_to_cpu(usbdev->descriptor.idProduct));

	/* our known interfaces have 3 alternate settings */
	if (intf->num_altsetting != 3)
		return -ENODEV;

	i = usb_set_interface(usbdev, intf->altsetting->desc.bInterfaceNumber, 2);
	printk(KERN_DEBUG "uss720: set inteface result %d\n", i);

	interface = intf->cur_altsetting;

	/*
	 * Allocate parport interface 
	 */
	printk(KERN_INFO "uss720: (C) 1999 by Thomas Sailer, <sailer@ife.ee.ethz.ch>\n");

	if (!(priv = kmalloc(sizeof(struct parport_uss720_private), GFP_KERNEL)))
		return -ENOMEM;
	if (!(pp = parport_register_port(0, PARPORT_IRQ_NONE, PARPORT_DMA_NONE, &parport_uss720_ops))) {
		printk(KERN_WARNING "usb-uss720: could not register parport\n");
		goto probe_abort;
	}

	pp->private_data = priv;
	priv->usbdev = usbdev;
	pp->modes = PARPORT_MODE_PCSPP | PARPORT_MODE_TRISTATE | PARPORT_MODE_EPP | PARPORT_MODE_ECP | PARPORT_MODE_COMPAT;

	/* set the USS720 control register to manual mode, no ECP compression, enable all ints */
	set_1284_register(pp, 7, 0x00);
	set_1284_register(pp, 6, 0x30);  /* PS/2 mode */
	set_1284_register(pp, 2, 0x0c);
	/* debugging */
	get_1284_register(pp, 0, NULL);
	printk("uss720: reg: %02x %02x %02x %02x %02x %02x %02x\n",
	       priv->reg[0], priv->reg[1], priv->reg[2], priv->reg[3], priv->reg[4], priv->reg[5], priv->reg[6]);

	endpoint = &interface->endpoint[2];
	printk(KERN_DEBUG "uss720: epaddr %d interval %d\n", endpoint->desc.bEndpointAddress, endpoint->desc.bInterval);
#if 0
	priv->irqpipe = usb_rcvctrlpipe(usbdev, endpoint->bEndpointAddress);
	i = usb_request_irq(usbdev, priv->irqpipe,
				  uss720_irq, endpoint->bInterval,
				  pp, &priv->irqhandle);
	if (i) {
		printk (KERN_WARNING "usb-uss720: usb_request_irq failed (0x%x)\n", i);
		goto probe_abort_port;
	}
#endif
	parport_announce_port(pp);

	usb_set_intfdata (intf, pp);
	return 0;

#if 0
probe_abort_port:
	parport_put_port(pp);
#endif
probe_abort:
	kfree(priv);
	return -ENODEV;
}

static void uss720_disconnect(struct usb_interface *intf)
{
	struct parport *pp = usb_get_intfdata (intf);
	struct parport_uss720_private *priv;

	usb_set_intfdata (intf, NULL);
	if (pp) {
		priv = pp->private_data;
		parport_remove_port(pp);
#if 0
		usb_release_irq(usbdev, priv->irqhandle, priv->irqpipe);
#endif
		priv->usbdev = NULL;
		parport_put_port(pp);
		kfree(priv);
	}
}

/* table of cables that work through this driver */
static struct usb_device_id uss720_table [] = {
	{ USB_DEVICE(0x047e, 0x1001) },
	{ USB_DEVICE(0x0557, 0x2001) },
	{ USB_DEVICE(0x0729, 0x1284) },
	{ USB_DEVICE(0x1293, 0x0002) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, uss720_table);


static struct usb_driver uss720_driver = {
	.owner =	THIS_MODULE,
	.name =		"uss720",
	.probe =	uss720_probe,
	.disconnect =	uss720_disconnect,
	.id_table =	uss720_table,
};

/* --------------------------------------------------------------------- */

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

static int __init uss720_init(void)
{
	int retval;
	retval = usb_register(&uss720_driver);
	if (retval)
		goto out;

	info(DRIVER_VERSION ":" DRIVER_DESC);
out:
	return retval;
}

static void __exit uss720_cleanup(void)
{
	usb_deregister(&uss720_driver);
}

module_init(uss720_init);
module_exit(uss720_cleanup);

/* --------------------------------------------------------------------- */
