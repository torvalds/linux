/* -*- linux-c -*-
 *
 *  drivers/char/viocons.c
 *
 *  iSeries Virtual Terminal
 *
 *  Authors: Dave Boutcher <boutcher@us.ibm.com>
 *           Ryan Arnold <ryanarn@us.ibm.com>
 *           Colin Devilbiss <devilbis@us.ibm.com>
 *           Stephen Rothwell <sfr@au1.ibm.com>
 *
 * (C) Copyright 2000, 2001, 2002, 2003, 2004 IBM Corporation
 *
 * This program is free software;  you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) anyu later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <asm/ioctls.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/sysrq.h>

#include <asm/iseries/vio.h>

#include <asm/iseries/hv_lp_event.h>
#include <asm/iseries/hv_call_event.h>
#include <asm/iseries/hv_lp_config.h>
#include <asm/iseries/hv_call.h>

#ifdef CONFIG_VT
#error You must turn off CONFIG_VT to use CONFIG_VIOCONS
#endif

#define VIOTTY_MAGIC (0x0DCB)
#define VTTY_PORTS 10

#define VIOCONS_KERN_WARN	KERN_WARNING "viocons: "
#define VIOCONS_KERN_INFO	KERN_INFO "viocons: "

static DEFINE_SPINLOCK(consolelock);
static DEFINE_SPINLOCK(consoleloglock);

#ifdef CONFIG_MAGIC_SYSRQ
static int vio_sysrq_pressed;
extern int sysrq_enabled;
#endif

/*
 * The structure of the events that flow between us and OS/400.  You can't
 * mess with this unless the OS/400 side changes too
 */
struct viocharlpevent {
	struct HvLpEvent event;
	u32 reserved;
	u16 version;
	u16 subtype_result_code;
	u8 virtual_device;
	u8 len;
	u8 data[VIOCHAR_MAX_DATA];
};

#define VIOCHAR_WINDOW		10
#define VIOCHAR_HIGHWATERMARK	3

enum viocharsubtype {
	viocharopen = 0x0001,
	viocharclose = 0x0002,
	viochardata = 0x0003,
	viocharack = 0x0004,
	viocharconfig = 0x0005
};

enum viochar_rc {
	viochar_rc_ebusy = 1
};

#define VIOCHAR_NUM_BUF		16

/*
 * Our port information.  We store a pointer to one entry in the
 * tty_driver_data
 */
static struct port_info {
	int magic;
	struct tty_struct *tty;
	HvLpIndex lp;
	u8 vcons;
	u64 seq;	/* sequence number of last HV send */
	u64 ack;	/* last ack from HV */
/*
 * When we get writes faster than we can send it to the partition,
 * buffer the data here. Note that used is a bit map of used buffers.
 * It had better have enough bits to hold VIOCHAR_NUM_BUF the bitops assume
 * it is a multiple of unsigned long
 */
	unsigned long used;
	u8 *buffer[VIOCHAR_NUM_BUF];
	int bufferBytes[VIOCHAR_NUM_BUF];
	int curbuf;
	int bufferOverflow;
	int overflowMessage;
} port_info[VTTY_PORTS];

#define viochar_is_console(pi)	((pi) == &port_info[0])
#define viochar_port(pi)	((pi) - &port_info[0])

static void initDataEvent(struct viocharlpevent *viochar, HvLpIndex lp);

static struct tty_driver *viotty_driver;

void hvlog(char *fmt, ...)
{
	int i;
	unsigned long flags;
	va_list args;
	static char buf[256];

	spin_lock_irqsave(&consoleloglock, flags);
	va_start(args, fmt);
	i = vscnprintf(buf, sizeof(buf) - 1, fmt, args);
	va_end(args);
	buf[i++] = '\r';
	HvCall_writeLogBuffer(buf, i);
	spin_unlock_irqrestore(&consoleloglock, flags);
}

void hvlogOutput(const char *buf, int count)
{
	unsigned long flags;
	int begin;
	int index;
	static const char cr = '\r';

	begin = 0;
	spin_lock_irqsave(&consoleloglock, flags);
	for (index = 0; index < count; index++) {
		if (buf[index] == '\n') {
			/*
			 * Start right after the last '\n' or at the zeroth
			 * array position and output the number of characters
			 * including the newline.
			 */
			HvCall_writeLogBuffer(&buf[begin], index - begin + 1);
			begin = index + 1;
			HvCall_writeLogBuffer(&cr, 1);
		}
	}
	if ((index - begin) > 0)
		HvCall_writeLogBuffer(&buf[begin], index - begin);
	spin_unlock_irqrestore(&consoleloglock, flags);
}

/*
 * Make sure we're pointing to a valid port_info structure.  Shamelessly
 * plagerized from serial.c
 */
static inline int viotty_paranoia_check(struct port_info *pi,
					char *name, const char *routine)
{
	static const char *bad_pi_addr = VIOCONS_KERN_WARN
		"warning: bad address for port_info struct (%s) in %s\n";
	static const char *badmagic = VIOCONS_KERN_WARN
		"warning: bad magic number for port_info struct (%s) in %s\n";

	if ((pi < &port_info[0]) || (viochar_port(pi) > VTTY_PORTS)) {
		printk(bad_pi_addr, name, routine);
		return 1;
	}
	if (pi->magic != VIOTTY_MAGIC) {
		printk(badmagic, name, routine);
		return 1;
	}
	return 0;
}

/*
 * Add data to our pending-send buffers.  
 *
 * NOTE: Don't use printk in here because it gets nastily recursive.
 * hvlog can be used to log to the hypervisor buffer
 */
static int buffer_add(struct port_info *pi, const char *buf, size_t len)
{
	size_t bleft;
	size_t curlen;
	const char *curbuf;
	int nextbuf;

	curbuf = buf;
	bleft = len;
	while (bleft > 0) {
		/*
		 * If there is no space left in the current buffer, we have
		 * filled everything up, so return.  If we filled the previous
		 * buffer we would already have moved to the next one.
		 */
		if (pi->bufferBytes[pi->curbuf] == VIOCHAR_MAX_DATA) {
			hvlog ("\n\rviocons: No overflow buffer available for memcpy().\n");
			pi->bufferOverflow++;
			pi->overflowMessage = 1;
			break;
		}

		/*
		 * Turn on the "used" bit for this buffer.  If it's already on,
		 * that's fine.
		 */
		set_bit(pi->curbuf, &pi->used);

		/*
		 * See if this buffer has been allocated.  If not, allocate it.
		 */
		if (pi->buffer[pi->curbuf] == NULL) {
			pi->buffer[pi->curbuf] =
			    kmalloc(VIOCHAR_MAX_DATA, GFP_ATOMIC);
			if (pi->buffer[pi->curbuf] == NULL) {
				hvlog("\n\rviocons: kmalloc failed allocating spaces for buffer %d.",
					pi->curbuf);
				break;
			}
		}

		/* Figure out how much we can copy into this buffer. */
		if (bleft < (VIOCHAR_MAX_DATA - pi->bufferBytes[pi->curbuf]))
			curlen = bleft;
		else
			curlen = VIOCHAR_MAX_DATA - pi->bufferBytes[pi->curbuf];

		/* Copy the data into the buffer. */
		memcpy(pi->buffer[pi->curbuf] + pi->bufferBytes[pi->curbuf],
				curbuf, curlen);

		pi->bufferBytes[pi->curbuf] += curlen;
		curbuf += curlen;
		bleft -= curlen;

		/*
		 * Now see if we've filled this buffer.  If not then
		 * we'll try to use it again later.  If we've filled it
		 * up then we'll advance the curbuf to the next in the
		 * circular queue.
		 */
		if (pi->bufferBytes[pi->curbuf] == VIOCHAR_MAX_DATA) {
			nextbuf = (pi->curbuf + 1) % VIOCHAR_NUM_BUF;
			/*
			 * Move to the next buffer if it hasn't been used yet
			 */
			if (test_bit(nextbuf, &pi->used) == 0)
				pi->curbuf = nextbuf;
		}
	}
	return len - bleft;
}

/*
 * Send pending data
 *
 * NOTE: Don't use printk in here because it gets nastily recursive.
 * hvlog can be used to log to the hypervisor buffer
 */
static void send_buffers(struct port_info *pi)
{
	HvLpEvent_Rc hvrc;
	int nextbuf;
	struct viocharlpevent *viochar;
	unsigned long flags;

	spin_lock_irqsave(&consolelock, flags);

	viochar = (struct viocharlpevent *)
	    vio_get_event_buffer(viomajorsubtype_chario);

	/* Make sure we got a buffer */
	if (viochar == NULL) {
		hvlog("\n\rviocons: Can't get viochar buffer in sendBuffers().");
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}

	if (pi->used == 0) {
		hvlog("\n\rviocons: in sendbuffers(), but no buffers used.\n");
		vio_free_event_buffer(viomajorsubtype_chario, viochar);
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}

	/*
	 * curbuf points to the buffer we're filling.  We want to
	 * start sending AFTER this one.  
	 */
	nextbuf = (pi->curbuf + 1) % VIOCHAR_NUM_BUF;

	/*
	 * Loop until we find a buffer with the used bit on
	 */
	while (test_bit(nextbuf, &pi->used) == 0)
		nextbuf = (nextbuf + 1) % VIOCHAR_NUM_BUF;

	initDataEvent(viochar, pi->lp);

	/*
	 * While we have buffers with data, and our send window
	 * is open, send them
	 */
	while ((test_bit(nextbuf, &pi->used)) &&
	       ((pi->seq - pi->ack) < VIOCHAR_WINDOW)) {
		viochar->len = pi->bufferBytes[nextbuf];
		viochar->event.xCorrelationToken = pi->seq++;
		viochar->event.xSizeMinus1 =
			offsetof(struct viocharlpevent, data) + viochar->len;

		memcpy(viochar->data, pi->buffer[nextbuf], viochar->len);

		hvrc = HvCallEvent_signalLpEvent(&viochar->event);
		if (hvrc) {
			/*
			 * MUST unlock the spinlock before doing a printk
			 */
			vio_free_event_buffer(viomajorsubtype_chario, viochar);
			spin_unlock_irqrestore(&consolelock, flags);

			printk(VIOCONS_KERN_WARN
			       "error sending event! return code %d\n",
			       (int)hvrc);
			return;
		}

		/*
		 * clear the used bit, zero the number of bytes in
		 * this buffer, and move to the next buffer
		 */
		clear_bit(nextbuf, &pi->used);
		pi->bufferBytes[nextbuf] = 0;
		nextbuf = (nextbuf + 1) % VIOCHAR_NUM_BUF;
	}

	/*
	 * If we have emptied all the buffers, start at 0 again.
	 * this will re-use any allocated buffers
	 */
	if (pi->used == 0) {
		pi->curbuf = 0;

		if (pi->overflowMessage)
			pi->overflowMessage = 0;

		if (pi->tty) {
			tty_wakeup(pi->tty);
		}
	}

	vio_free_event_buffer(viomajorsubtype_chario, viochar);
	spin_unlock_irqrestore(&consolelock, flags);
}

/*
 * Our internal writer.  Gets called both from the console device and
 * the tty device.  the tty pointer will be NULL if called from the console.
 * Return total number of bytes "written".
 *
 * NOTE: Don't use printk in here because it gets nastily recursive.  hvlog
 * can be used to log to the hypervisor buffer
 */
static int internal_write(struct port_info *pi, const char *buf, size_t len)
{
	HvLpEvent_Rc hvrc;
	size_t bleft;
	size_t curlen;
	const char *curbuf;
	unsigned long flags;
	struct viocharlpevent *viochar;

	/*
	 * Write to the hvlog of inbound data are now done prior to
	 * calling internal_write() since internal_write() is only called in
	 * the event that an lp event path is active, which isn't the case for
	 * logging attempts prior to console initialization.
	 *
	 * If there is already data queued for this port, send it prior to
	 * attempting to send any new data.
	 */
	if (pi->used)
		send_buffers(pi);

	spin_lock_irqsave(&consolelock, flags);

	viochar = vio_get_event_buffer(viomajorsubtype_chario);
	if (viochar == NULL) {
		spin_unlock_irqrestore(&consolelock, flags);
		hvlog("\n\rviocons: Can't get vio buffer in internal_write().");
		return -EAGAIN;
	}
	initDataEvent(viochar, pi->lp);

	curbuf = buf;
	bleft = len;

	while ((bleft > 0) && (pi->used == 0) &&
	       ((pi->seq - pi->ack) < VIOCHAR_WINDOW)) {
		if (bleft > VIOCHAR_MAX_DATA)
			curlen = VIOCHAR_MAX_DATA;
		else
			curlen = bleft;

		viochar->event.xCorrelationToken = pi->seq++;
		memcpy(viochar->data, curbuf, curlen);
		viochar->len = curlen;
		viochar->event.xSizeMinus1 =
		    offsetof(struct viocharlpevent, data) + curlen;

		hvrc = HvCallEvent_signalLpEvent(&viochar->event);
		if (hvrc) {
			hvlog("viocons: error sending event! %d\n", (int)hvrc);
			goto out;
		}
		curbuf += curlen;
		bleft -= curlen;
	}

	/* If we didn't send it all, buffer as much of it as we can. */
	if (bleft > 0)
		bleft -= buffer_add(pi, curbuf, bleft);
out:
	vio_free_event_buffer(viomajorsubtype_chario, viochar);
	spin_unlock_irqrestore(&consolelock, flags);
	return len - bleft;
}

static struct port_info *get_port_data(struct tty_struct *tty)
{
	unsigned long flags;
	struct port_info *pi;

	spin_lock_irqsave(&consolelock, flags);
	if (tty) {
		pi = (struct port_info *)tty->driver_data;
		if (!pi || viotty_paranoia_check(pi, tty->name,
					     "get_port_data")) {
			pi = NULL;
		}
	} else
		/*
		 * If this is the console device, use the lp from
		 * the first port entry
		 */
		pi = &port_info[0];
	spin_unlock_irqrestore(&consolelock, flags);
	return pi;
}

/*
 * Initialize the common fields in a charLpEvent
 */
static void initDataEvent(struct viocharlpevent *viochar, HvLpIndex lp)
{
	memset(viochar, 0, sizeof(struct viocharlpevent));

	viochar->event.xFlags.xValid = 1;
	viochar->event.xFlags.xFunction = HvLpEvent_Function_Int;
	viochar->event.xFlags.xAckInd = HvLpEvent_AckInd_NoAck;
	viochar->event.xFlags.xAckType = HvLpEvent_AckType_DeferredAck;
	viochar->event.xType = HvLpEvent_Type_VirtualIo;
	viochar->event.xSubtype = viomajorsubtype_chario | viochardata;
	viochar->event.xSourceLp = HvLpConfig_getLpIndex();
	viochar->event.xTargetLp = lp;
	viochar->event.xSizeMinus1 = sizeof(struct viocharlpevent);
	viochar->event.xSourceInstanceId = viopath_sourceinst(lp);
	viochar->event.xTargetInstanceId = viopath_targetinst(lp);
}

/*
 * early console device write
 */
static void viocons_write_early(struct console *co, const char *s, unsigned count)
{
	hvlogOutput(s, count);
}

/*
 * console device write
 */
static void viocons_write(struct console *co, const char *s, unsigned count)
{
	int index;
	int begin;
	struct port_info *pi;

	static const char cr = '\r';

	/*
	 * Check port data first because the target LP might be valid but
	 * simply not active, in which case we want to hvlog the output.
	 */
	pi = get_port_data(NULL);
	if (pi == NULL) {
		hvlog("\n\rviocons_write: unable to get port data.");
		return;
	}

	hvlogOutput(s, count);

	if (!viopath_isactive(pi->lp))
		return;

	/* 
	 * Any newline character found will cause a
	 * carriage return character to be emitted as well. 
	 */
	begin = 0;
	for (index = 0; index < count; index++) {
		if (s[index] == '\n') {
			/* 
			 * Newline found. Print everything up to and 
			 * including the newline
			 */
			internal_write(pi, &s[begin], index - begin + 1);
			begin = index + 1;
			/* Emit a carriage return as well */
			internal_write(pi, &cr, 1);
		}
	}

	/* If any characters left to write, write them now */
	if ((index - begin) > 0)
		internal_write(pi, &s[begin], index - begin);
}

/*
 * Work out the device associate with this console
 */
static struct tty_driver *viocons_device(struct console *c, int *index)
{
	*index = c->index;
	return viotty_driver;
}

/*
 * console device I/O methods
 */
static struct console viocons_early = {
	.name = "viocons",
	.write = viocons_write_early,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

static struct console viocons = {
	.name = "viocons",
	.write = viocons_write,
	.device = viocons_device,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

/*
 * TTY Open method
 */
static int viotty_open(struct tty_struct *tty, struct file *filp)
{
	int port;
	unsigned long flags;
	struct port_info *pi;

	port = tty->index;

	if ((port < 0) || (port >= VTTY_PORTS))
		return -ENODEV;

	spin_lock_irqsave(&consolelock, flags);

	pi = &port_info[port];
	/* If some other TTY is already connected here, reject the open */
	if ((pi->tty) && (pi->tty != tty)) {
		spin_unlock_irqrestore(&consolelock, flags);
		printk(VIOCONS_KERN_WARN
		       "attempt to open device twice from different ttys\n");
		return -EBUSY;
	}
	tty->driver_data = pi;
	pi->tty = tty;
	spin_unlock_irqrestore(&consolelock, flags);

	return 0;
}

/*
 * TTY Close method
 */
static void viotty_close(struct tty_struct *tty, struct file *filp)
{
	unsigned long flags;
	struct port_info *pi;

	spin_lock_irqsave(&consolelock, flags);
	pi = (struct port_info *)tty->driver_data;

	if (!pi || viotty_paranoia_check(pi, tty->name, "viotty_close")) {
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}
	if (tty->count == 1)
		pi->tty = NULL;
	spin_unlock_irqrestore(&consolelock, flags);
}

/*
 * TTY Write method
 */
static int viotty_write(struct tty_struct *tty, const unsigned char *buf,
		int count)
{
	struct port_info *pi;

	pi = get_port_data(tty);
	if (pi == NULL) {
		hvlog("\n\rviotty_write: no port data.");
		return -ENODEV;
	}

	if (viochar_is_console(pi))
		hvlogOutput(buf, count);

	/*
	 * If the path to this LP is closed, don't bother doing anything more.
	 * just dump the data on the floor and return count.  For some reason
	 * some user level programs will attempt to probe available tty's and
	 * they'll attempt a viotty_write on an invalid port which maps to an
	 * invalid target lp.  If this is the case then ignore the
	 * viotty_write call and, since the viopath isn't active to this
	 * partition, return count.
	 */
	if (!viopath_isactive(pi->lp))
		return count;

	return internal_write(pi, buf, count);
}

/*
 * TTY put_char method
 */
static void viotty_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct port_info *pi;

	pi = get_port_data(tty);
	if (pi == NULL)
		return;

	/* This will append '\r' as well if the char is '\n' */
	if (viochar_is_console(pi))
		hvlogOutput(&ch, 1);

	if (viopath_isactive(pi->lp))
		internal_write(pi, &ch, 1);
}

/*
 * TTY write_room method
 */
static int viotty_write_room(struct tty_struct *tty)
{
	int i;
	int room = 0;
	struct port_info *pi;
	unsigned long flags;

	spin_lock_irqsave(&consolelock, flags);
	pi = (struct port_info *)tty->driver_data;
	if (!pi || viotty_paranoia_check(pi, tty->name, "viotty_write_room")) {
		spin_unlock_irqrestore(&consolelock, flags);
		return 0;
	}

	/* If no buffers are used, return the max size. */
	if (pi->used == 0) {
		spin_unlock_irqrestore(&consolelock, flags);
		return VIOCHAR_MAX_DATA * VIOCHAR_NUM_BUF;
	}

	/*
	 * We retain the spinlock because we want to get an accurate
	 * count and it can change on us between each operation if we
	 * don't hold the spinlock.
	 */
	for (i = 0; ((i < VIOCHAR_NUM_BUF) && (room < VIOCHAR_MAX_DATA)); i++)
		room += (VIOCHAR_MAX_DATA - pi->bufferBytes[i]);
	spin_unlock_irqrestore(&consolelock, flags);

	if (room > VIOCHAR_MAX_DATA)
		room = VIOCHAR_MAX_DATA;
	return room;
}

/*
 * TTY chars_in_buffer method
 */
static int viotty_chars_in_buffer(struct tty_struct *tty)
{
	return 0;
}

static int viotty_ioctl(struct tty_struct *tty, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	/*
	 * the ioctls below read/set the flags usually shown in the leds
	 * don't use them - they will go away without warning
	 */
	case KDGETLED:
	case KDGKBLED:
		return put_user(0, (char *)arg);

	case KDSKBLED:
		return 0;
	}

	return n_tty_ioctl(tty, file, cmd, arg);
}

/*
 * Handle an open charLpEvent.  Could be either interrupt or ack
 */
static void vioHandleOpenEvent(struct HvLpEvent *event)
{
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	u8 port = cevent->virtual_device;
	struct port_info *pi;
	int reject = 0;

	if (event->xFlags.xFunction == HvLpEvent_Function_Ack) {
		if (port >= VTTY_PORTS)
			return;

		spin_lock_irqsave(&consolelock, flags);
		/* Got the lock, don't cause console output */

		pi = &port_info[port];
		if (event->xRc == HvLpEvent_Rc_Good) {
			pi->seq = pi->ack = 0;
			/*
			 * This line allows connections from the primary
			 * partition but once one is connected from the
			 * primary partition nothing short of a reboot
			 * of linux will allow access from the hosting
			 * partition again without a required iSeries fix.
			 */
			pi->lp = event->xTargetLp;
		}

		spin_unlock_irqrestore(&consolelock, flags);
		if (event->xRc != HvLpEvent_Rc_Good)
			printk(VIOCONS_KERN_WARN
			       "handle_open_event: event->xRc == (%d).\n",
			       event->xRc);

		if (event->xCorrelationToken != 0) {
			atomic_t *aptr= (atomic_t *)event->xCorrelationToken;
			atomic_set(aptr, 1);
		} else
			printk(VIOCONS_KERN_WARN
			       "weird...got open ack without atomic\n");
		return;
	}

	/* This had better require an ack, otherwise complain */
	if (event->xFlags.xAckInd != HvLpEvent_AckInd_DoAck) {
		printk(VIOCONS_KERN_WARN "viocharopen without ack bit!\n");
		return;
	}

	spin_lock_irqsave(&consolelock, flags);
	/* Got the lock, don't cause console output */

	/* Make sure this is a good virtual tty */
	if (port >= VTTY_PORTS) {
		event->xRc = HvLpEvent_Rc_SubtypeError;
		cevent->subtype_result_code = viorc_openRejected;
		/*
		 * Flag state here since we can't printk while holding
		 * a spinlock.
		 */
		reject = 1;
	} else {
		pi = &port_info[port];
		if ((pi->lp != HvLpIndexInvalid) &&
				(pi->lp != event->xSourceLp)) {
			/*
			 * If this is tty is already connected to a different
			 * partition, fail.
			 */
			event->xRc = HvLpEvent_Rc_SubtypeError;
			cevent->subtype_result_code = viorc_openRejected;
			reject = 2;
		} else {
			pi->lp = event->xSourceLp;
			event->xRc = HvLpEvent_Rc_Good;
			cevent->subtype_result_code = viorc_good;
			pi->seq = pi->ack = 0;
			reject = 0;
		}
	}

	spin_unlock_irqrestore(&consolelock, flags);

	if (reject == 1)
		printk(VIOCONS_KERN_WARN "open rejected: bad virtual tty.\n");
	else if (reject == 2)
		printk(VIOCONS_KERN_WARN
			"open rejected: console in exclusive use by another partition.\n");

	/* Return the acknowledgement */
	HvCallEvent_ackLpEvent(event);
}

/*
 * Handle a close charLpEvent.  This should ONLY be an Interrupt because the
 * virtual console should never actually issue a close event to the hypervisor
 * because the virtual console never goes away.  A close event coming from the
 * hypervisor simply means that there are no client consoles connected to the
 * virtual console.
 *
 * Regardless of the number of connections masqueraded on the other side of
 * the hypervisor ONLY ONE close event should be called to accompany the ONE
 * open event that is called.  The close event should ONLY be called when NO
 * MORE connections (masqueraded or not) exist on the other side of the
 * hypervisor.
 */
static void vioHandleCloseEvent(struct HvLpEvent *event)
{
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	u8 port = cevent->virtual_device;

	if (event->xFlags.xFunction == HvLpEvent_Function_Int) {
		if (port >= VTTY_PORTS) {
			printk(VIOCONS_KERN_WARN
					"close message from invalid virtual device.\n");
			return;
		}

		/* For closes, just mark the console partition invalid */
		spin_lock_irqsave(&consolelock, flags);
		/* Got the lock, don't cause console output */

		if (port_info[port].lp == event->xSourceLp)
			port_info[port].lp = HvLpIndexInvalid;

		spin_unlock_irqrestore(&consolelock, flags);
		printk(VIOCONS_KERN_INFO "close from %d\n", event->xSourceLp);
	} else
		printk(VIOCONS_KERN_WARN
				"got unexpected close acknowlegement\n");
}

/*
 * Handle a config charLpEvent.  Could be either interrupt or ack
 */
static void vioHandleConfig(struct HvLpEvent *event)
{
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;

	HvCall_writeLogBuffer(cevent->data, cevent->len);

	if (cevent->data[0] == 0x01)
		printk(VIOCONS_KERN_INFO "window resized to %d: %d: %d: %d\n",
		       cevent->data[1], cevent->data[2],
		       cevent->data[3], cevent->data[4]);
	else
		printk(VIOCONS_KERN_WARN "unknown config event\n");
}

/*
 * Handle a data charLpEvent. 
 */
static void vioHandleData(struct HvLpEvent *event)
{
	struct tty_struct *tty;
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	struct port_info *pi;
	int index;
	u8 port = cevent->virtual_device;

	if (port >= VTTY_PORTS) {
		printk(VIOCONS_KERN_WARN "data on invalid virtual device %d\n",
				port);
		return;
	}

	/*
	 * Hold the spinlock so that we don't take an interrupt that
	 * changes tty between the time we fetch the port_info
	 * pointer and the time we paranoia check.
	 */
	spin_lock_irqsave(&consolelock, flags);
	pi = &port_info[port];

	/*
	 * Change 05/01/2003 - Ryan Arnold: If a partition other than
	 * the current exclusive partition tries to send us data
	 * events then just drop them on the floor because we don't
	 * want his stinking data.  He isn't authorized to receive
	 * data because he wasn't the first one to get the console,
	 * therefore he shouldn't be allowed to send data either.
	 * This will work without an iSeries fix.
	 */
	if (pi->lp != event->xSourceLp) {
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}

	tty = pi->tty;
	if (tty == NULL) {
		spin_unlock_irqrestore(&consolelock, flags);
		printk(VIOCONS_KERN_WARN "no tty for virtual device %d\n",
				port);
		return;
	}

	if (tty->magic != TTY_MAGIC) {
		spin_unlock_irqrestore(&consolelock, flags);
		printk(VIOCONS_KERN_WARN "tty bad magic\n");
		return;
	}

	/*
	 * Just to be paranoid, make sure the tty points back to this port
	 */
	pi = (struct port_info *)tty->driver_data;
	if (!pi || viotty_paranoia_check(pi, tty->name, "vioHandleData")) {
		spin_unlock_irqrestore(&consolelock, flags);
		return;
	}
	spin_unlock_irqrestore(&consolelock, flags);

	/*
	 * Change 07/21/2003 - Ryan Arnold: functionality added to
	 * support sysrq utilizing ^O as the sysrq key.  The sysrq
	 * functionality will only work if built into the kernel and
	 * then only if sysrq is enabled through the proc filesystem.
	 */
	for (index = 0; index < cevent->len; index++) {
#ifdef CONFIG_MAGIC_SYSRQ
		if (sysrq_enabled) {
			/* 0x0f is the ascii character for ^O */
			if (cevent->data[index] == '\x0f') {
				vio_sysrq_pressed = 1;
				/*
				 * continue because we don't want to add
				 * the sysrq key into the data string.
				 */
				continue;
			} else if (vio_sysrq_pressed) {
				handle_sysrq(cevent->data[index], NULL, tty);
				vio_sysrq_pressed = 0;
				/*
				 * continue because we don't want to add
				 * the sysrq sequence into the data string.
				 */
				continue;
			}
		}
#endif
		/*
		 * The sysrq sequence isn't included in this check if
		 * sysrq is enabled and compiled into the kernel because
		 * the sequence will never get inserted into the buffer.
		 * Don't attempt to copy more data into the buffer than we
		 * have room for because it would fail without indication.
		 */
		if ((tty->flip.count + 1) > TTY_FLIPBUF_SIZE) {
			printk(VIOCONS_KERN_WARN "input buffer overflow!\n");
			break;
		}
		tty_insert_flip_char(tty, cevent->data[index], TTY_NORMAL);
	}

	/* if cevent->len == 0 then no data was added to the buffer and flip.count == 0 */
	if (tty->flip.count)
		/* The next call resets flip.count when the data is flushed. */
		tty_flip_buffer_push(tty);
}

/*
 * Handle an ack charLpEvent. 
 */
static void vioHandleAck(struct HvLpEvent *event)
{
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	unsigned long flags;
	u8 port = cevent->virtual_device;

	if (port >= VTTY_PORTS) {
		printk(VIOCONS_KERN_WARN "data on invalid virtual device\n");
		return;
	}

	spin_lock_irqsave(&consolelock, flags);
	port_info[port].ack = event->xCorrelationToken;
	spin_unlock_irqrestore(&consolelock, flags);

	if (port_info[port].used)
		send_buffers(&port_info[port]);
}

/*
 * Handle charLpEvents and route to the appropriate routine
 */
static void vioHandleCharEvent(struct HvLpEvent *event)
{
	int charminor;

	if (event == NULL)
		return;

	charminor = event->xSubtype & VIOMINOR_SUBTYPE_MASK;
	switch (charminor) {
	case viocharopen:
		vioHandleOpenEvent(event);
		break;
	case viocharclose:
		vioHandleCloseEvent(event);
		break;
	case viochardata:
		vioHandleData(event);
		break;
	case viocharack:
		vioHandleAck(event);
		break;
	case viocharconfig:
		vioHandleConfig(event);
		break;
	default:
		if ((event->xFlags.xFunction == HvLpEvent_Function_Int) &&
		    (event->xFlags.xAckInd == HvLpEvent_AckInd_DoAck)) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}
}

/*
 * Send an open event
 */
static int send_open(HvLpIndex remoteLp, void *sem)
{
	return HvCallEvent_signalLpEventFast(remoteLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_chario | viocharopen,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(remoteLp),
			viopath_targetinst(remoteLp),
			(u64)(unsigned long)sem, VIOVERSION << 16,
			0, 0, 0, 0);
}

static struct tty_operations serial_ops = {
	.open = viotty_open,
	.close = viotty_close,
	.write = viotty_write,
	.put_char = viotty_put_char,
	.write_room = viotty_write_room,
	.chars_in_buffer = viotty_chars_in_buffer,
	.ioctl = viotty_ioctl,
};

static int __init viocons_init2(void)
{
	atomic_t wait_flag;
	int rc;

	/* +2 for fudge */
	rc = viopath_open(HvLpConfig_getPrimaryLpIndex(),
			viomajorsubtype_chario, VIOCHAR_WINDOW + 2);
	if (rc)
		printk(VIOCONS_KERN_WARN "error opening to primary %d\n", rc);

	if (viopath_hostLp == HvLpIndexInvalid)
		vio_set_hostlp();

	/*
	 * And if the primary is not the same as the hosting LP, open to the 
	 * hosting lp
	 */
	if ((viopath_hostLp != HvLpIndexInvalid) &&
	    (viopath_hostLp != HvLpConfig_getPrimaryLpIndex())) {
		printk(VIOCONS_KERN_INFO "open path to hosting (%d)\n",
				viopath_hostLp);
		rc = viopath_open(viopath_hostLp, viomajorsubtype_chario,
				VIOCHAR_WINDOW + 2);	/* +2 for fudge */
		if (rc)
			printk(VIOCONS_KERN_WARN
				"error opening to partition %d: %d\n",
				viopath_hostLp, rc);
	}

	if (vio_setHandler(viomajorsubtype_chario, vioHandleCharEvent) < 0)
		printk(VIOCONS_KERN_WARN
				"error seting handler for console events!\n");

	/*
	 * First, try to open the console to the hosting lp.
	 * Wait on a semaphore for the response.
	 */
	atomic_set(&wait_flag, 0);
	if ((viopath_isactive(viopath_hostLp)) &&
	    (send_open(viopath_hostLp, (void *)&wait_flag) == 0)) {
		printk(VIOCONS_KERN_INFO "hosting partition %d\n",
			viopath_hostLp);
		while (atomic_read(&wait_flag) == 0)
			mb();
		atomic_set(&wait_flag, 0);
	}

	/*
	 * If we don't have an active console, try the primary
	 */
	if ((!viopath_isactive(port_info[0].lp)) &&
	    (viopath_isactive(HvLpConfig_getPrimaryLpIndex())) &&
	    (send_open(HvLpConfig_getPrimaryLpIndex(), (void *)&wait_flag)
	     == 0)) {
		printk(VIOCONS_KERN_INFO "opening console to primary partition\n");
		while (atomic_read(&wait_flag) == 0)
			mb();
	}

	/* Initialize the tty_driver structure */
	viotty_driver = alloc_tty_driver(VTTY_PORTS);
	viotty_driver->owner = THIS_MODULE;
	viotty_driver->driver_name = "vioconsole";
	viotty_driver->devfs_name = "vcs/";
	viotty_driver->name = "tty";
	viotty_driver->name_base = 1;
	viotty_driver->major = TTY_MAJOR;
	viotty_driver->minor_start = 1;
	viotty_driver->type = TTY_DRIVER_TYPE_CONSOLE;
	viotty_driver->subtype = 1;
	viotty_driver->init_termios = tty_std_termios;
	viotty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_RESET_TERMIOS;
	tty_set_operations(viotty_driver, &serial_ops);

	if (tty_register_driver(viotty_driver)) {
		printk(VIOCONS_KERN_WARN "couldn't register console driver\n");
		put_tty_driver(viotty_driver);
		viotty_driver = NULL;
	}

	unregister_console(&viocons_early);
	register_console(&viocons);

	return 0;
}

static int __init viocons_init(void)
{
	int i;

	printk(VIOCONS_KERN_INFO "registering console\n");
	for (i = 0; i < VTTY_PORTS; i++) {
		port_info[i].lp = HvLpIndexInvalid;
		port_info[i].magic = VIOTTY_MAGIC;
	}
	HvCall_setLogBufferFormatAndCodepage(HvCall_LogBuffer_ASCII, 437);
	register_console(&viocons_early);
	return 0;
}

console_initcall(viocons_init);
module_init(viocons_init2);
