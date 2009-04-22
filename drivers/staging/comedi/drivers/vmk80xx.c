/*
    comedi/drivers/vmk80xx.c
    Velleman USB Interface Board Kernel-Space Driver

    Copyright (C) 2009 Manuel Gebele <forensixs@gmx.de>, Germany

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
/*
Driver: vmk80xx
Description: Velleman USB Interface Board Kernel-Space Driver
Devices: K8055, K8061 (in development)
Author: Manuel Gebele <forensixs@gmx.de>
Updated: Tue, 21 Apr 2009 19:40:55 +0200
Status: works
*/

#include <linux/kernel.h>
#include <linux/comedidev.h> /* comedi definitions */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/usb.h>
#include <asm/uaccess.h>

/* ------------------------------------------------------------------------ */
#define VMK80XX_MODULE_DESC "Velleman USB Interface Board Kernel-Space Driver"
#define VMK80XX_MODULE_DEVICE "Velleman K8055/K8061 USB Interface Board"
#define VMK80XX_MODULE_AUTHOR "Copyright (C) 2009 Manuel Gebele, Germany"
#define VMK80XX_MODULE_LICENSE "GPL"
#define VMK80XX_MODULE_VERSION "0.7.76"

/* Module device ID's */
static struct usb_device_id vm_id_table[] = {
	/* k8055 */
	{ USB_DEVICE(0x10cf, 0x5500 + 0x00) }, /* @ddr. 0 */
	{ USB_DEVICE(0x10cf, 0x5500 + 0x01) }, /* @ddr. 1 */
	{ USB_DEVICE(0x10cf, 0x5500 + 0x02) }, /* @ddr. 2 */
	{ USB_DEVICE(0x10cf, 0x5500 + 0x03) }, /* @ddr. 3 */
	/* k8061 */
	{ USB_DEVICE(0x10cf, 0x8061 + 0x00) }, /* @ddr. 0 */
	{ USB_DEVICE(0x10cf, 0x8061 + 0x01) }, /* @ddr. 1 */
	{ USB_DEVICE(0x10cf, 0x8061 + 0x02) }, /* @ddr. 2 */
	{ USB_DEVICE(0x10cf, 0x8061 + 0x03) }, /* @ddr. 3 */
	{ USB_DEVICE(0x10cf, 0x8061 + 0x04) }, /* @ddr. 4 */
	{ USB_DEVICE(0x10cf, 0x8061 + 0x05) }, /* @ddr. 5 */
	{ USB_DEVICE(0x10cf, 0x8061 + 0x06) }, /* @ddr. 6 */
	{ USB_DEVICE(0x10cf, 0x8061 + 0x07) }, /* @ddr. 7 */
	{ } /* terminating entry */
};
MODULE_DEVICE_TABLE(usb, vm_id_table);

MODULE_AUTHOR(VMK80XX_MODULE_AUTHOR);
MODULE_DESCRIPTION(VMK80XX_MODULE_DESC);
MODULE_SUPPORTED_DEVICE(VMK80XX_MODULE_DEVICE);
MODULE_VERSION(VMK80XX_MODULE_VERSION);
MODULE_LICENSE(VMK80XX_MODULE_LICENSE);
/* ------------------------------------------------------------------------ */

#define CONFIG_VMK80XX_DEBUG

//#undef CONFIG_COMEDI_DEBUG /* Uncommend this line to disable comedi debug */
#undef CONFIG_VMK80XX_DEBUG  /* Commend this line to enable vmk80xx debug */

#ifdef CONFIG_COMEDI_DEBUG
 static int cm_dbg = 1;
#else   /* !CONFIG_COMEDI_DEBUG */
 static int cm_dbg = 0;
#endif  /* !CONFIG_COMEDI_DEBUG */

#ifdef CONFIG_VMK80XX_DEBUG
 static int vm_dbg = 1;
#else   /* !CONFIG_VMK80XX_DEBUG */
 static int vm_dbg = 0;
#endif  /* !CONFIG_VMK80XX_DEBUG */

/* Define our own debug macros */
#define DBGCM(fmt, arg...) do { if (cm_dbg) printk(fmt, ##arg); } while (0)
#define DBGVM(fmt, arg...) do { if (vm_dbg) printk(fmt, ##arg); } while (0)

/* Velleman K8055 specific stuff */
#define VMK8055_DI              0 /* digital input offset */
#define VMK8055_DO              1 /* digital output offset */
#define VMK8055_AO1             2 /* analog output channel 1 offset */
#define VMK8055_AO2             3 /* analog output channel 2 offset */
#define VMK8055_CNT1            4 /* counter 1 offset */
#define VMK8055_CNT2            6 /* counter 2 offset */
#define VMK8055_CMD_RST      0x00 /* reset device registers */
#define VMK8055_CMD_DEB1     0x01 /* debounce time for pulse counter 1 */
#define VMK8055_CMD_DEB2     0x02 /* debounce time for pulse counter 2 */
#define VMK8055_CMD_RST_CNT1 0x03 /* reset pulse counter 1 */
#define VMK8055_CMD_RST_CNT2 0x04 /* reset pulse counter 2 */
#define VMK8055_CMD_AD       0x05 /* write to analog or digital channel */
#define VMK8055_EP_OUT       0x01 /* out endpoint address */
#define VMK8055_EP_IN        0x81 /* in endpoint address */
#define VMK8055_EP_SIZE         8 /* endpoint max packet size */
#define VMK8055_EP_INTERVAL    20 /* general conversion time per command */
#define VMK8055_MAX_BOARDS     16

/* Structure to hold all of our device specific stuff */
struct vmk80xx_usb {
	struct usb_interface	*intf;
	struct semaphore	limit_sem;
	wait_queue_head_t	read_wait;
	wait_queue_head_t	write_wait;
	size_t			irq_out_endpoint_size;
	__u8			irq_out_endpoint;
	int			irq_out_interval;
	unsigned char		*irq_out_buf;
	struct urb		*irq_out_urb;
	int			irq_out_busy;
	size_t			irq_in_endpoint_size;
	__u8			irq_in_endpoint;
	int			irq_in_interval;
	unsigned char		*irq_in_buf;
	struct urb		*irq_in_urb;
	int			irq_in_busy;
	int			irq_in_running;
	int			probed;
	int			attached;
	int			id;
};

static struct vmk80xx_usb vm_boards[VMK8055_MAX_BOARDS];

/* ---------------------------------------------------------------------------
 * Abort active transfers and tidy up allocated resources.
--------------------------------------------------------------------------- */
static void vm_abort_transfers(struct vmk80xx_usb *vm)
{
	DBGVM("comedi#: vmk80xx: %s\n", __func__);

	if (vm->irq_in_running) {
		vm->irq_in_running = 0;
		if (vm->intf)
			usb_kill_urb(vm->irq_in_urb);
	}

	if (vm->irq_out_busy && vm->intf)
		usb_kill_urb(vm->irq_out_urb);
}

static void vm_delete(struct vmk80xx_usb *vm)
{
	DBGVM("comedi#: vmk80xx: %s\n", __func__);

	vm_abort_transfers(vm);

	/* Deallocate usb urbs and kernel buffers */
	if (vm->irq_in_urb)
		usb_free_urb(vm->irq_in_urb);

	if (vm->irq_out_urb);
		usb_free_urb(vm->irq_out_urb);

	if (vm->irq_in_buf)
		kfree(vm->irq_in_buf);

	if (vm->irq_out_buf)
		kfree(vm->irq_out_buf);
}

/* ---------------------------------------------------------------------------
 * Interrupt in and interrupt out callback for usb data transfer.
--------------------------------------------------------------------------- */
static void vm_irq_in_callback(struct urb *urb)
{
	struct vmk80xx_usb *vm = (struct vmk80xx_usb *)urb->context;
	int err;

	DBGVM("comedi#: vmk80xx: %s\n", __func__);

	switch (urb->status) {
	case 0: /* success */
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		break;
	default:
		DBGCM("comedi#: vmk80xx: %s - nonzero urb status (%d)\n",
		      __func__, urb->status);
		goto resubmit; /* maybe we can recover */
	}

	goto exit;
resubmit:
	if (vm->irq_in_running && vm->intf) {
		err = usb_submit_urb(vm->irq_in_urb, GFP_ATOMIC);
		if (!err) goto exit;
		/* FALL THROUGH */
		DBGCM("comedi#: vmk80xx: %s - submit urb failed (err# %d)\n",
		      __func__, err);
	}
exit:
	vm->irq_in_busy = 0;

	/* interrupt-in pipe is available again */
	wake_up_interruptible(&vm->read_wait);
}

static void vm_irq_out_callback(struct urb *urb)
{
	struct vmk80xx_usb *vm;

	DBGVM("comedi#: vmk80xx: %s\n", __func__);

	/* sync/async unlink (hardware going away) faults  aren't errors */
	if (urb->status && !(urb->status == -ENOENT
			||   urb->status == -ECONNRESET
			||   urb->status == -ESHUTDOWN))
		DBGCM("comedi#: vmk80xx: %s - nonzero urb status (%d)\n",
		      __func__, urb->status);

	vm = (struct vmk80xx_usb *)urb->context;
	vm->irq_out_busy = 0;

	/* interrupt-out pipe is available again */
	wake_up_interruptible(&vm->write_wait);
}

/* ---------------------------------------------------------------------------
 * Interface for digital/analog input/output and counter funcs (see below).
--------------------------------------------------------------------------- */
static int vm_read(struct vmk80xx_usb *vm)
{
	struct usb_device *udev;
	int retval = -ENODEV;

	DBGVM("comedi#: vmk80xx: %s\n", __func__);

	/* Verify that the device wasn't un-plugged */
	if (!vm->intf) {
		DBGCM("comedi#: vmk80xx: %s - No dev or dev un-plugged\n",
		      __func__);
		goto exit;
	}

	if (vm->irq_in_busy) {
		retval = wait_event_interruptible(vm->read_wait,
						 !vm->irq_in_busy);
		if (retval < 0) { /* we were interrupted by a signal */
			retval = -ERESTART;
			goto exit;
		}
	}

	udev = interface_to_usbdev(vm->intf);

	/* Fill the urb and send off */
	usb_fill_int_urb(vm->irq_in_urb,
			 udev,
			 usb_rcvintpipe(udev, vm->irq_in_endpoint),
			 vm->irq_in_buf,
			 vm->irq_in_endpoint_size,
			 vm_irq_in_callback,
			 vm,
			 vm->irq_in_interval);

	vm->irq_in_running = 1;
	vm->irq_in_busy = 1; /* disallow following read request's */

	retval = usb_submit_urb(vm->irq_in_urb, GFP_KERNEL);
	if (!retval) goto exit; /* success */
	/* FALL TROUGH */
	vm->irq_in_running = 0;
	DBGCM("comedi#: vmk80xx: %s - submit urb failed (err# %d)\n",
	      __func__, retval);

exit:
	return retval;
}

static int vm_write(struct vmk80xx_usb *vm, unsigned char cmd)
{
	struct usb_device *udev;
	int retval = -ENODEV;

	DBGVM("comedi#: vmk80xx: %s\n", __func__);

	/* Verify that the device wasn't un-plugged */
	if (!vm->intf) {
		DBGCM("comedi#: vmk80xx: %s - No dev or dev un-plugged\n",
		      __func__);
		goto exit;
	}

	if (vm->irq_out_busy) {
		retval = wait_event_interruptible(vm->write_wait,
						 !vm->irq_out_busy);
		if (retval < 0) { /* we were interrupted by a signal */
			retval = -ERESTART;
			goto exit;
		}
	}

	udev = interface_to_usbdev(vm->intf);

	/* Set the command which should send to the device */
	vm->irq_out_buf[0] = cmd;

	/* Fill the urb and send off */
	usb_fill_int_urb(vm->irq_out_urb,
			 udev,
			 usb_sndintpipe(udev, vm->irq_out_endpoint),
			 vm->irq_out_buf,
			 vm->irq_out_endpoint_size,
			 vm_irq_out_callback,
			 vm,
			 vm->irq_out_interval);

	vm->irq_out_busy = 1; /* disallow following write request's */

	wmb();

	retval = usb_submit_urb(vm->irq_out_urb, GFP_KERNEL);
	if (!retval) goto exit; /* success */
	/* FALL THROUGH */
	vm->irq_out_busy = 0;
	DBGCM("comedi#: vmk80xx: %s - submit urb failed (err# %d)\n",
	      __func__, retval);

exit:
	return retval;
}

/* ---------------------------------------------------------------------------
 * COMEDI-Interface (callback functions for the userspacs apps).
--------------------------------------------------------------------------- */
static int vm_ai_rinsn(comedi_device *dev, comedi_subdevice *s,
		       comedi_insn *insn, unsigned int *data)
{
	struct vmk80xx_usb *vm;
	int minor = dev->minor;
	int ch, ch_offs, i;
	int retval = -EFAULT;

	DBGVM("comedi%d: vmk80xx: %s\n", minor,  __func__);

	if (!(vm = (struct vmk80xx_usb *)dev->private))
		return retval;

	down(&vm->limit_sem);

	/* We have an attached board ? */
	if (!vm->probed) {
		retval = -ENODEV;
		goto error;
	}

	/* interrupt-in pipe busy ? */
	if (vm->irq_in_busy) {
		retval = -EBUSY;
		goto error;
	}

	ch = CR_CHAN(insn->chanspec);
	ch_offs = (!ch) ? VMK8055_AO1 : VMK8055_AO2;

	for (i = 0; i < insn->n; i++) {
		retval = vm_read(vm);
		if (retval)
			goto error;

		/* NOTE:
		 * The input voltage of the selected 8-bit AD channel
		 * is converted to a value which lies between
		 * 0 and 255.
		 */
		data[i] = vm->irq_in_buf[ch_offs];
	}

	up(&vm->limit_sem);

	/* Return the number of samples read */
	return i;
error:
	up(&vm->limit_sem);

	return retval;
}

static int vm_ao_winsn(comedi_device *dev, comedi_subdevice *s,
                       comedi_insn *insn, unsigned int *data)
{
	struct vmk80xx_usb *vm;
	int minor = dev->minor;
	int ch, ch_offs, i;
	int retval = -EFAULT;

	DBGVM("comedi%d: vmk80xx: %s\n", minor,  __func__);

	if (!(vm = (struct vmk80xx_usb *)dev->private))
		return retval;

	down(&vm->limit_sem);

	/* We have an attached board ? */
	if (!vm->probed) {
		retval = -ENODEV;
		goto error;
	}

	/* interrupt-out pipe busy ? */
	if (vm->irq_out_busy) {
		retval = -EBUSY;
		goto error;
	}

	ch = CR_CHAN(insn->chanspec);
	ch_offs = (!ch) ? VMK8055_AO1 : VMK8055_AO2;

	for (i = 0; i < insn->n; i++) {
		/* NOTE:
		 * The indicated 8-bit DA channel is altered according
		 * to the new data. This means that the data corresponds
		 * to a specific voltage. The value 0 corresponds to a
		 * minimum output voltage (+-0 Volt) and the value 255
		 * corresponds to a maximum output voltage (+5 Volt).
		 */
		vm->irq_out_buf[ch_offs] = data[i];

		retval = vm_write(vm, VMK8055_CMD_AD);
		if (retval)
			goto error;
	}

	up(&vm->limit_sem);

	/* Return the number of samples write */
	return i;
error:
	up(&vm->limit_sem);

	return retval;
}

static int vm_di_rinsn(comedi_device *dev, comedi_subdevice *s,
		       comedi_insn *insn, unsigned int *data)
{
	struct vmk80xx_usb *vm;
	int minor = dev->minor;
	int ch, i, inp;
	int retval = -EFAULT;

	DBGVM("comedi%d: vmk80xx: %s\n", minor,  __func__);

	if (!(vm = (struct vmk80xx_usb *)dev->private))
		return retval;

	down(&vm->limit_sem);

	/* We have an attached board ? */
	if (!vm->probed) {
		retval = -ENODEV;
		goto error;
	}

	/* interrupt-in pipe busy ? */
	if (vm->irq_in_busy) {
		retval = -EBUSY;
		goto error;
	}

	for (i = 0, ch = CR_CHAN(insn->chanspec); i < insn->n; i++) {
		retval = vm_read(vm);
		if (retval)
			goto error;

		/* NOTE:
		 * The status of the selected digital input channel is read.
		 */
		inp = (((vm->irq_in_buf[VMK8055_DI] >> 4) & 0x03) |
		       ((vm->irq_in_buf[VMK8055_DI] << 2) & 0x04) |
		       ((vm->irq_in_buf[VMK8055_DI] >> 3) & 0x18));
		data[i] = ((inp & (1 << ch)) > 0);
	}

	up(&vm->limit_sem);

	return i;
error:
	up(&vm->limit_sem);

	return retval;
}

static int vm_do_winsn(comedi_device *dev, comedi_subdevice *s,
		       comedi_insn *insn, unsigned int *data)
{
	struct vmk80xx_usb *vm;
	int minor = dev->minor;
	int ch, i, mask;
	int retval = -EFAULT;

	DBGVM("comedi%d: vmk80xx: %s\n", minor,  __func__);

	if (!(vm = (struct vmk80xx_usb *)dev->private))
		return retval;

	down(&vm->limit_sem);

	/* We have an attached board ? */
	if (!vm->probed) {
		retval = -ENODEV;
		goto error;
	}

	/* interrupt-out pipe busy ? */
	if (vm->irq_out_busy) {
		retval = -EBUSY;
		goto error;
	}

	for (i = 0, ch = CR_CHAN(insn->chanspec); i < insn->n; i++) {
		/* NOTE:
		 * The selected digital output channel is set or cleared.
		 */
		mask = (data[i] == 1)
		     ? vm->irq_out_buf[VMK8055_DO] | (1 << ch)
		     : vm->irq_out_buf[VMK8055_DO] ^ (1 << ch);

		vm->irq_out_buf[VMK8055_DO] = mask;

		retval = vm_write(vm, VMK8055_CMD_AD);
		if (retval)
			goto error;
	}

	up(&vm->limit_sem);

	return i;
error:
	up(&vm->limit_sem);

	return retval;
}

static int vm_cnt_rinsn(comedi_device *dev, comedi_subdevice *s,
			comedi_insn *insn, unsigned int *data)
{
	struct vmk80xx_usb *vm;
	int minor = dev->minor;
	int cnt, cnt_offs, i;
	int retval = -EFAULT;

	DBGVM("comedi%d: vmk80xx: %s\n", minor,  __func__);

	if (!(vm = (struct vmk80xx_usb *)dev->private))
		return retval;

	down(&vm->limit_sem);

	/* We have an attached board ? */
	if (!vm->probed) {
		retval = -ENODEV;
		goto error;
	}

	/* interrupt-in pipe busy ? */
	if (vm->irq_in_busy) {
		retval = -EBUSY;
		goto error;
	}

	cnt = CR_CHAN(insn->chanspec);
	cnt_offs = (!cnt) ? VMK8055_CNT1 : VMK8055_CNT2;

	for (i = 0; i < insn->n; i++) {
		retval = vm_read(vm);
		if (retval)
			goto error;

		/* NOTE:
		 * The status of the selected 16-bit pulse counter is
		 * read. The counter # 1 counts the pulses fed to the
		 * input Inp1 and the counter # 2 counts the pulses fed
		 * to the input Inp2.
		 */
		data[i] = vm->irq_in_buf[cnt_offs];
	}

	up(&vm->limit_sem);

	return i;
error:
	up(&vm->limit_sem);

	return retval;
}

static int vm_cnt_winsn(comedi_device *dev, comedi_subdevice *s,
			comedi_insn *insn, unsigned int *data)
{
	struct vmk80xx_usb *vm;
	int minor = dev->minor;
	int cnt, cnt_offs, cmd, i;
	int retval = -EFAULT;

	DBGVM("comedi%d: vmk80xx: %s\n", minor,  __func__);

	if (!(vm = (struct vmk80xx_usb *)dev->private))
		return retval;

	down(&vm->limit_sem);

	/* We have an attached board ? */
	if (!vm->probed) {
		retval = -ENODEV;
		goto error;
	}

	/* interrupt-out pipe busy ? */
	if (vm->irq_out_busy) {
		retval = -EBUSY;
		goto error;
	}

	cnt = CR_CHAN(insn->chanspec);
	cnt_offs = (!cnt) ? VMK8055_CNT1 : VMK8055_CNT2;
	cmd = (!cnt) ? VMK8055_CMD_RST_CNT1 : VMK8055_CMD_RST_CNT2;

	for (i = 0; i < insn->n; i++) {
		/* NOTE:
		 * The selected 16-bit pulse counter is reset.
		 */
		vm->irq_out_buf[cnt_offs] = 0x00;

		retval = vm_write(vm, cmd);
		if (retval)
			goto error;
	}

	up(&vm->limit_sem);

	return i;
error:
	up(&vm->limit_sem);

	return retval;
}

static int vm_cnt_cinsn(comedi_device *dev, comedi_subdevice *s,
			comedi_insn *insn, unsigned int *data)
{
	struct vmk80xx_usb *vm;
	int minor = dev->minor;
	int cnt, cmd, i;
	unsigned int debtime, val;
	int retval = -EFAULT;

	DBGVM("comedi%d: vmk80xx: %s\n", minor,  __func__);

	if (!(vm = (struct vmk80xx_usb *)dev->private))
		return retval;

	down(&vm->limit_sem);

	/* We have an attached board ? */
	if (!vm->probed) {
		retval = -ENODEV;
		goto error;
	}

	/* interrupt-out pipe busy ? */
	if (vm->irq_out_busy) {
		retval = -EBUSY;
		goto error;
	}

	cnt = CR_CHAN(insn->chanspec);
	cmd = (!cnt) ? VMK8055_CMD_DEB1 : VMK8055_CMD_DEB2;

	/* NOTE:
	 * The counter inputs are debounced in the software to prevent
	 * false triggering when mechanical switches or relay inputs
	 * are used. The debounce time is equal for both falling and
	 * rising edges. The default debounce time is 2ms. This means
	 * the counter input must be stable for at least 2ms before it
	 * is recognised , giving the maximum count rate of about 200
	 * counts per second. If the debounce time is set to 0, then
	 * the maximum counting rate is about 2000 counts per second.
	 */
	for (i = 0; i < insn->n; i++) {
		debtime = data[i];
		if (debtime == 0)
			debtime = 1;
		/* --------------------------------------------------
		 * From libk8055.c
		 * ---------------
		 * Copyleft (C) 2005 by Sven Lindberg;
		 * Copyright (C) 2007 by Pjetur G. Hjaltason:
		 * By testing and measuring on the other hand I found
		 * the formula dbt=0.115*x^2.........
		 *
		 * I'm using here an adapted formula to avoid floating
		 * point operations inside the kernel. The time set
		 * with this formula is within +-4% +- 1.
		 * ------------------------------------------------ */
		val = int_sqrt(debtime * 1000 / 115);
		if (((val + 1) * val) < debtime * 1000 / 115)
			val += 1;

		vm->irq_out_buf[cnt+6] = val;

		retval = vm_write(vm, cmd);
		if (retval)
			goto error;
	}

	up(&vm->limit_sem);

	return i;
error:
	up(&vm->limit_sem);

	return retval;
}

/* Comedi subdevice offsets */
#define VMK8055_SUBD_AI_OFFSET	0
#define VMK8055_SUBD_AO_OFFSET	1
#define VMK8055_SUBD_DI_OFFSET	2
#define VMK8055_SUBD_DO_OFFSET	3
#define VMK8055_SUBD_CT_OFFSET	4

static DEFINE_MUTEX(glb_mutex);

/* ---------------------------------------------------------------------------
 * Hook-up (or deallocate) the virtual device file '/dev/comedi[minor]' with
 * the vmk80xx driver (comedi_config/rmmod).
--------------------------------------------------------------------------- */
static int vm_attach(comedi_device *dev, comedi_devconfig *it)
{
	comedi_subdevice *s;
	int minor = dev->minor;
	int idx, i;

	DBGVM("comedi%d: vmk80xx: %s\n", minor,  __func__);

	mutex_lock(&glb_mutex);

	/* Prepare user info... */
	printk("comedi%d: vmk80xx: ", minor);

	idx = -1;

	/* Find the last valid device which has been detected
	 * by the probe function */;
	for (i = 0; i < VMK8055_MAX_BOARDS; i++)
		if (vm_boards[i].probed && !vm_boards[i].attached) {
			idx = i;
			break;
		}

	if (idx == -1) {
		printk("no boards attached\n");
		mutex_unlock(&glb_mutex);
		return -ENODEV;
	}

	down(&vm_boards[idx].limit_sem);

	/* OK, at that time we've an attached board and this is
	 * the first execution of the comedi_config command for
	 * this board */
	printk("board #%d is attached to comedi\n", vm_boards[idx].id);

	dev->board_name = "vmk80xx";
	dev->private = vm_boards + idx; /* will be allocated in vm_probe */

	/* Subdevices section -> set properties */
	if (alloc_subdevices(dev, 5) < 0) {
		printk("comedi%d: vmk80xx: couldn't allocate subdevs\n",
		       minor);
		up(&vm_boards[idx].limit_sem);
		mutex_unlock(&glb_mutex);
		return -ENOMEM;
	}

	s = dev->subdevices + VMK8055_SUBD_AI_OFFSET;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = 2;
	s->maxdata = 0xff; /* +5 Volt */
	s->range_table = &range_unipolar5; /* +-0 Volt - +5 Volt */
	s->insn_read = vm_ai_rinsn;

	s = dev->subdevices + VMK8055_SUBD_AO_OFFSET;
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITEABLE | SDF_GROUND;
	s->n_chan = 2;
	s->maxdata = 0xff;
	s->range_table = &range_unipolar5;
	s->insn_write = vm_ao_winsn;

	s = dev->subdevices + VMK8055_SUBD_DI_OFFSET;
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = 5;
	s->insn_read = vm_di_rinsn;

	s = dev->subdevices + VMK8055_SUBD_DO_OFFSET;
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITEABLE | SDF_GROUND;
	s->n_chan = 8;
	s->maxdata = 1;
	s->insn_write = vm_do_winsn;

	s = dev->subdevices + VMK8055_SUBD_CT_OFFSET;
	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_READABLE | SDF_WRITEABLE;
	s->n_chan = 2;
	s->insn_read = vm_cnt_rinsn;
	s->insn_write = vm_cnt_winsn; /* accept only a channel # as arg */
	s->insn_config = vm_cnt_cinsn;

	/* Register the comedi board connection */
	vm_boards[idx].attached = 1;

	up(&vm_boards[idx].limit_sem);

	mutex_unlock(&glb_mutex);

	return 0;
}

static int vm_detach(comedi_device *dev)
{
	struct vmk80xx_usb *vm;
	int minor = dev->minor;

	DBGVM("comedi%d: vmk80xx: %s\n", minor,  __func__);

	if (!dev) { /* FIXME: I don't know if i need that here */
		printk("comedi%d: vmk80xx: %s - dev is NULL\n",
		       minor, __func__);
		return -EFAULT;
	}

	if (!(vm = (struct vmk80xx_usb *)dev->private)) {
		printk("comedi%d: vmk80xx: %s - dev->private is NULL\n",
		       minor, __func__);
		return -EFAULT;
	}

	/* NOTE: dev->private and dev->subdevices are deallocated
	 * automatically by the comedi core */

	down(&vm->limit_sem);

	dev->private = NULL;
	vm->attached = 0;

	printk("comedi%d: vmk80xx: board #%d removed from comedi core\n",
	       minor, vm->id);

	up(&vm->limit_sem);

	return 0;
}

/* ---------------------------------------------------------------------------
 * Hook-up or remove the Velleman board from the usb.
--------------------------------------------------------------------------- */
static int vm_probe(struct usb_interface *itf, const struct usb_device_id *id)
{
	struct usb_device *udev;
	int idx, i;
	u16 product_id;
	int retval = -ENOMEM;

	DBGVM("comedi#: vmk80xx: %s\n", __func__);

	mutex_lock(&glb_mutex);

	udev = interface_to_usbdev(itf);

	idx = -1;

	/* TODO: k8061 only theoretically supported yet */
	product_id = le16_to_cpu(udev->descriptor.idProduct);
	if (product_id == 0x8061) {
		printk("comedi#: vmk80xx: Velleman K8061 detected "
		       "(no COMEDI support available yet)\n");
		mutex_unlock(&glb_mutex);
		return -ENODEV;
	}

	/* Look for a free place to put the board into the array */
	for (i = 0; i < VMK8055_MAX_BOARDS; i++) {
		if (!vm_boards[i].probed) {
			idx = i;
			i = VMK8055_MAX_BOARDS;
		}
	}

	if (idx == -1) {
		printk("comedi#: vmk80xx: only FOUR boards supported\n");
		mutex_unlock(&glb_mutex);
		return -EMFILE;
	}

	/* Initialize device states (hard coded) */
	vm_boards[idx].intf = itf;

	/* interrupt-in context */
	vm_boards[idx].irq_in_endpoint = VMK8055_EP_IN;
	vm_boards[idx].irq_in_interval = VMK8055_EP_INTERVAL;
	vm_boards[idx].irq_in_endpoint_size = VMK8055_EP_SIZE;
	vm_boards[idx].irq_in_buf = kmalloc(VMK8055_EP_SIZE, GFP_KERNEL);
	if (!vm_boards[idx].irq_in_buf) {
		err("comedi#: vmk80xx: couldn't alloc irq_in_buf\n");
		goto error;
	}

	/* interrupt-out context */
	vm_boards[idx].irq_out_endpoint = VMK8055_EP_OUT;
	vm_boards[idx].irq_out_interval = VMK8055_EP_INTERVAL;
	vm_boards[idx].irq_out_endpoint_size = VMK8055_EP_SIZE;
	vm_boards[idx].irq_out_buf = kmalloc(VMK8055_EP_SIZE, GFP_KERNEL);
	if (!vm_boards[idx].irq_out_buf) {
		err("comedi#: vmk80xx: couldn't alloc irq_out_buf\n");
		goto error;
	}

	/* Endpoints located ? */
	if (!vm_boards[idx].irq_in_endpoint) {
		err("comedi#: vmk80xx: int-in endpoint not found\n");
		goto error;
	}

	if (!vm_boards[idx].irq_out_endpoint) {
		err("comedi#: vmk80xx: int-out endpoint not found\n");
		goto error;
	}

	/* Try to allocate in/out urbs */
	vm_boards[idx].irq_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!vm_boards[idx].irq_in_urb) {
		err("comedi#: vmk80xx: couldn't alloc irq_in_urb\n");
		goto error;
	}

	vm_boards[idx].irq_out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!vm_boards[idx].irq_out_urb) {
		err("comedi#: vmk80xx: couldn't alloc irq_out_urb\n");
		goto error;
	}

	/* Reset the device */
	vm_boards[idx].irq_out_buf[0] = VMK8055_CMD_RST;
	vm_boards[idx].irq_out_buf[1] = 0x00;
	vm_boards[idx].irq_out_buf[2] = 0x00;
	vm_boards[idx].irq_out_buf[3] = 0x00;
	vm_boards[idx].irq_out_buf[4] = 0x00;
	vm_boards[idx].irq_out_buf[5] = 0x00;
	vm_boards[idx].irq_out_buf[6] = 0x00;
	vm_boards[idx].irq_out_buf[7] = 0x00;

	usb_fill_int_urb(vm_boards[idx].irq_out_urb,
			 udev,
			 usb_sndintpipe(udev,
					vm_boards[idx].irq_out_endpoint),
			 vm_boards[idx].irq_out_buf,
			 vm_boards[idx].irq_out_endpoint_size,
			 vm_irq_out_callback,
			 &vm_boards[idx],
			 vm_boards[idx].irq_out_interval);

	retval = usb_submit_urb(vm_boards[idx].irq_out_urb, GFP_KERNEL);
	if (retval)
		DBGCM("comedi#: vmk80xx: device reset failed (err #%d)\n",
		      retval);
	else
		DBGCM("comedi#: vmk80xx: device reset success\n");


	usb_set_intfdata(itf, &vm_boards[idx]);

	/* Show some debugging messages if required */
	DBGCM("comedi#: vmk80xx: [<-] ep addr 0x%02x size %d interval %d\n",
	      vm_boards[idx].irq_in_endpoint,
	      vm_boards[idx].irq_in_endpoint_size,
	      vm_boards[idx].irq_in_interval);
	DBGCM("comedi#: vmk80xx: [->] ep addr 0x%02x size %d interval %d\n",
	      vm_boards[idx].irq_out_endpoint,
	      vm_boards[idx].irq_out_endpoint_size,
	      vm_boards[idx].irq_out_interval);

	vm_boards[idx].id = idx;

	/* Let the user know that the device is now attached */
	printk("comedi#: vmk80xx: K8055 board #%d now attached\n",
	       vm_boards[idx].id);

	/* We have an attached velleman board */
	vm_boards[idx].probed = 1;

	mutex_unlock(&glb_mutex);

	return retval;
error:
	vm_delete(&vm_boards[idx]);

	mutex_unlock(&glb_mutex);

	return retval;
}

static void vm_disconnect(struct usb_interface *intf)
{
	struct vmk80xx_usb *vm;

	DBGVM("comedi#: vmk80xx: %s\n", __func__);

	vm = (struct vmk80xx_usb *)usb_get_intfdata(intf);
	if (!vm) {
		printk("comedi#: vmk80xx: %s - vm is NULL\n", __func__);
		return; /* -EFAULT */
	}

	mutex_lock(&glb_mutex);
	/* Twill be needed if the driver supports more than one board */
	down(&vm->limit_sem);

	vm->probed = 0; /* we have -1 attached boards */
	usb_set_intfdata(vm->intf, NULL);

	vm_delete(vm); /* tidy up */

	/* Twill be needed if the driver supports more than one board */
	up(&vm->limit_sem);
	mutex_unlock(&glb_mutex);

	printk("comedi#: vmk80xx: Velleman board #%d now detached\n",
	       vm->id);
}

/* ---------------------------------------------------------------------------
 * Register/Deregister this driver with/from the usb subsystem and the comedi.
--------------------------------------------------------------------------- */
static struct usb_driver vm_driver = {
#ifdef COMEDI_HAVE_USB_DRIVER_OWNER
	.owner =	THIS_MODULE,
#endif
	.name =		"vmk80xx",
	.probe =	vm_probe,
	.disconnect =	vm_disconnect,
	.id_table =	vm_id_table,
};

static comedi_driver driver_vm = {
	.module =	THIS_MODULE,
	.driver_name =	"vmk80xx",
	.attach =	vm_attach,
	.detach =	vm_detach,
};

static int __init vm_init(void)
{
	int retval, idx;

	printk("vmk80xx: version " VMK80XX_MODULE_VERSION " -"
				 " Manuel Gebele <forensixs@gmx.de>\n");

	for (idx = 0; idx < VMK8055_MAX_BOARDS; idx++) {
		memset(&vm_boards[idx], 0x00, sizeof(vm_boards[idx]));
		init_MUTEX(&vm_boards[idx].limit_sem);
		init_waitqueue_head(&vm_boards[idx].read_wait);
		init_waitqueue_head(&vm_boards[idx].write_wait);
	}

	/* Register with the usb subsystem */
	retval = usb_register(&vm_driver);
	if (retval) {
		err("vmk80xx: usb subsystem registration failed (err #%d)\n",
		    retval);
		return retval;
	}

	/* Register with the comedi core */
	retval = comedi_driver_register(&driver_vm);
	if (retval) {
		err("vmk80xx: comedi core registration failed (err #%d)\n",
		    retval);
		usb_deregister(&vm_driver);
	}

	return retval;
}

static void __exit vm_exit(void)
{
	comedi_driver_unregister(&driver_vm);
	usb_deregister(&vm_driver);
}
module_init(vm_init);
module_exit(vm_exit);
