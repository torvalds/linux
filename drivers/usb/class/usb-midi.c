/*
  usb-midi.c  --  USB-MIDI driver

  Copyright (C) 2001 
      NAGANO Daisuke <breeze.nagano@nifty.ne.jp>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

  This driver is based on:
    - 'Universal Serial Bus Device Class Definition for MIDI Device'
    - linux/drivers/sound/es1371.c, linux/drivers/usb/audio.c
    - alsa/lowlevel/pci/cs64xx.c
    - umidi.c for NetBSD
 */

/* ------------------------------------------------------------------------- */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/poll.h>
#include <linux/sound.h>
#include <linux/init.h>
#include <asm/semaphore.h>

#include "usb-midi.h"

/* ------------------------------------------------------------------------- */

/* More verbose on syslog */
#undef MIDI_DEBUG

#define MIDI_IN_BUFSIZ 1024

#define HAVE_SUPPORT_USB_MIDI_CLASS

#undef HAVE_SUPPORT_ALSA

/* ------------------------------------------------------------------------- */

static int singlebyte = 0;
module_param(singlebyte, int, 0);
MODULE_PARM_DESC(singlebyte,"Enable sending MIDI messages with single message packet");

static int maxdevices = 4;
module_param(maxdevices, int, 0);
MODULE_PARM_DESC(maxdevices,"Max number of allocatable MIDI device");

static int uvendor     = -1;
module_param(uvendor, int, 0);
MODULE_PARM_DESC(uvendor, "The USB Vendor ID of a semi-compliant interface");

static int uproduct    = -1;
module_param(uproduct, int, 0);
MODULE_PARM_DESC(uproduct, "The USB Product ID of a semi-compliant interface");

static int uinterface  = -1;
module_param(uinterface, int, 0);
MODULE_PARM_DESC(uinterface, "The Interface number of a semi-compliant interface");

static int ualt        = -1;
module_param(ualt, int, 0);
MODULE_PARM_DESC(ualt, "The optional alternative setting of a semi-compliant interface");

static int umin        = -1;
module_param(umin, int, 0);
MODULE_PARM_DESC(umin, "The input endpoint of a semi-compliant interface");

static int umout       = -1;
module_param(umout, int, 0);
MODULE_PARM_DESC(umout, "The output endpoint of a semi-compliant interface");

static int ucable      = -1;
module_param(ucable, int, 0);
MODULE_PARM_DESC(ucable, "The cable number used for a semi-compliant interface");

/** Note -- the usb_string() returns only Latin-1 characters.
 * (unicode chars <= 255). To support Japanese, a unicode16LE-to-EUC or
 * unicode16LE-to-JIS routine is needed to wrap around usb_get_string().
 **/
static unsigned short ulangid      = 0x0409; /** 0x0411 for Japanese **/
module_param(ulangid, ushort, 0);
MODULE_PARM_DESC(ulangid, "The optional preferred USB Language ID for all devices");

MODULE_AUTHOR("NAGANO Daisuke <breeze.nagano@nifty.ne.jp>");
MODULE_DESCRIPTION("USB-MIDI driver");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------------------- */

/** MIDIStreaming Class-Specific Interface Descriptor Subtypes **/

#define MS_DESCRIPTOR_UNDEFINED	0
#define MS_HEADER		1
#define MIDI_IN_JACK		2
#define MIDI_OUT_JACK		3
/* Spec reads: ELEMENT */
#define ELEMENT_DESCRIPTOR   	4

#define MS_HEADER_LENGTH	7

/** MIDIStreaming Class-Specific Endpoint Descriptor Subtypes **/

#define DESCRIPTOR_UNDEFINED	0
/* Spec reads: MS_GENERAL */
#define MS_GENERAL_ENDPOINT	1

/** MIDIStreaming MIDI IN and OUT Jack Types **/

#define JACK_TYPE_UNDEFINED	0
/* Spec reads: EMBEDDED */
#define EMBEDDED_JACK		1
/* Spec reads: EXTERNAL */
#define EXTERNAL_JACK		2


/* structure summary
  
      usb_midi_state     usb_device
       |         |
      *|        *|       per ep
     in_ep     out_ep
       |         |
      *|        *|       per cable
      min       mout
       |         |       (cable to device pairing magic)
       |         |
       usb_midi_dev      dev_id (major,minor) == file->private_data

*/

/* usb_midi_state: corresponds to a USB-MIDI module */
struct usb_midi_state {
	struct list_head   mididev;
	
	struct usb_device *usbdev;
	
	struct list_head   midiDevList;
	struct list_head   inEndpointList;
	struct list_head   outEndpointList;
	
	spinlock_t         lock;
	
	unsigned int       count; /* usage counter */
};

/* midi_out_endpoint: corresponds to an output endpoint */
struct midi_out_endpoint {
	struct list_head  list;
	
	struct usb_device *usbdev;
	int                endpoint;
	spinlock_t         lock;
	wait_queue_head_t  wait;
	
	unsigned char     *buf;
	int                bufWrPtr;
	int                bufSize;
	
	struct urb       *urb;
};

/* midi_in_endpoint: corresponds to an input endpoint */
struct midi_in_endpoint {
	struct list_head   list;

	struct usb_device *usbdev;
	int                endpoint;
	spinlock_t         lock;
	wait_queue_head_t  wait;

	struct usb_mididev *cables[16];	// cables open for read
	int                 readers;	// number of cables open for read

	struct urb        *urb;
	unsigned char     *recvBuf;
	int                recvBufSize;
	int                urbSubmitted;	//FIXME: == readers > 0
};

/* usb_mididev: corresponds to a logical device */
struct usb_mididev {
	struct list_head       list;

	struct usb_midi_state *midi;
	int                    dev_midi;
	mode_t                 open_mode;

	struct {
		struct midi_in_endpoint *ep;
		int              cableId;
		
// as we are pushing data from usb_bulk_read to usb_midi_read,
// we need a larger, cyclic buffer here.
		unsigned char    buf[MIDI_IN_BUFSIZ];
		int              bufRdPtr;
		int              bufWrPtr;
		int              bufRemains;
	} min;

	struct {
		struct midi_out_endpoint *ep;
		int              cableId;
		
		unsigned char    buf[3];
		int              bufPtr;
		int              bufRemains;
		
		int              isInExclusive;
		unsigned char    lastEvent;
	} mout;

	int singlebyte;
};

/** Map the high nybble of MIDI voice messages to number of Message bytes.
 * High nyble ranges from 0x8 to 0xe
 */

static int remains_80e0[] = {
	3,	/** 0x8X Note Off **/
	3,	/** 0x9X Note On **/
	3,	/** 0xAX Poly-key pressure **/
	3,	/** 0xBX Control Change **/
	2,	/** 0xCX Program Change **/
	2,	/** 0xDX Channel pressure **/
	3 	/** 0xEX PitchBend Change **/
};

/** Map the messages to a number of Message bytes.
 *
 **/
static int remains_f0f6[] = {
	0,	/** 0xF0 **/
	2,	/** 0XF1 **/
	3,	/** 0XF2 **/
	2,	/** 0XF3 **/
	2,	/** 0XF4 (Undefined by MIDI Spec, and subject to change) **/
	2,	/** 0XF5 (Undefined by MIDI Spec, and subject to change) **/
	1	/** 0XF6 **/
};

/** Map the messages to a CIN (Code Index Number).
 *
 **/
static int cin_f0ff[] = {
	4,	/** 0xF0 System Exclusive Message Start (special cases may be 6 or 7) */
	2,	/** 0xF1 **/
	3,	/** 0xF2 **/
	2,	/** 0xF3 **/
	2,	/** 0xF4 **/
	2,	/** 0xF5 **/
	5,	/** 0xF6 **/
	5,	/** 0xF7 End of System Exclusive Message (May be 6 or 7) **/
	5,	/** 0xF8 **/
	5,	/** 0xF9 **/
	5,	/** 0xFA **/
	5,	/** 0xFB **/
	5,	/** 0xFC **/
	5,	/** 0xFD **/
	5,	/** 0xFE **/
	5	/** 0xFF **/
};

/** Map MIDIStreaming Event packet Code Index Number (low nybble of byte 0)
 * to the number of bytes of valid MIDI data.
 *
 * CIN of 0 and 1 are NOT USED in MIDIStreaming 1.0.
 *
 **/
static int cin_to_len[] = {
	0, 0, 2, 3,
	3, 1, 2, 3,
	3, 3, 3, 3,
	2, 2, 3, 1
};


/* ------------------------------------------------------------------------- */

static struct list_head mididevs = LIST_HEAD_INIT(mididevs);

static DECLARE_MUTEX(open_sem);
static DECLARE_WAIT_QUEUE_HEAD(open_wait);


/* ------------------------------------------------------------------------- */

static void usb_write_callback(struct urb *urb, struct pt_regs *regs)
{
	struct midi_out_endpoint *ep = (struct midi_out_endpoint *)urb->context;

	if ( waitqueue_active( &ep->wait ) )
		wake_up_interruptible( &ep->wait );
}


static int usb_write( struct midi_out_endpoint *ep, unsigned char *buf, int len )
{
	struct usb_device *d;
	int pipe;
	int ret = 0;
	int status;
	int maxretry = 50;
	
	DECLARE_WAITQUEUE(wait,current);
	init_waitqueue_head(&ep->wait);

	d = ep->usbdev;
	pipe = usb_sndbulkpipe(d, ep->endpoint);
	usb_fill_bulk_urb( ep->urb, d, pipe, (unsigned char*)buf, len,
		       usb_write_callback, ep );

	status = usb_submit_urb(ep->urb, GFP_KERNEL);
    
	if (status) {
		printk(KERN_ERR "usbmidi: Cannot submit urb (%d)\n",status);
		ret = -EIO;
		goto error;
	}

	add_wait_queue( &ep->wait, &wait );
	set_current_state( TASK_INTERRUPTIBLE );

	while( ep->urb->status == -EINPROGRESS ) {
		if ( maxretry-- < 0 ) {
			printk(KERN_ERR "usbmidi: usb_bulk_msg timed out\n");
			ret = -ETIME;
			break;
		}
		interruptible_sleep_on_timeout( &ep->wait, 10 );
	}
	set_current_state( TASK_RUNNING );
	remove_wait_queue( &ep->wait, &wait );

error:
	return ret;
}


/** Copy data from URB to In endpoint buf.
 * Discard if CIN == 0 or CIN = 1.
 *
 *
 **/

static void usb_bulk_read(struct urb *urb, struct pt_regs *regs)
{
	struct midi_in_endpoint *ep = (struct midi_in_endpoint *)(urb->context);
	unsigned char *data = urb->transfer_buffer;
	int i, j, wake;

	if ( !ep->urbSubmitted ) {
		return;
	}

	if ( (urb->status == 0) && (urb->actual_length > 0) ) {
		wake = 0;
		spin_lock( &ep->lock );

		for(j = 0; j < urb->actual_length; j += 4) {
			int cin = (data[j]>>0)&0xf;
			int cab = (data[j]>>4)&0xf;
			struct usb_mididev *cable = ep->cables[cab];
			if ( cable ) {
				int len = cin_to_len[cin]; /** length of MIDI data **/
				for (i = 0; i < len; i++) {
					cable->min.buf[cable->min.bufWrPtr] = data[1+i+j];
					cable->min.bufWrPtr = (cable->min.bufWrPtr+1)%MIDI_IN_BUFSIZ;
					if (cable->min.bufRemains < MIDI_IN_BUFSIZ)
						cable->min.bufRemains += 1;
					else /** need to drop data **/
						cable->min.bufRdPtr += (cable->min.bufRdPtr+1)%MIDI_IN_BUFSIZ;
					wake = 1;
				}
			}
		}

		spin_unlock ( &ep->lock );
		if ( wake ) {
			wake_up( &ep->wait );
		}
	}

	/* urb->dev must be reinitialized on 2.4.x kernels */
	urb->dev = ep->usbdev;

	urb->actual_length = 0;
	usb_submit_urb(urb, GFP_ATOMIC);
}



/* ------------------------------------------------------------------------- */

/* This routine must be called with spin_lock */

/** Wrapper around usb_write().
 *  This routine must be called with spin_lock held on ep.
 *  Called by midiWrite(), putOneMidiEvent(), and  usb_midi_write();
 **/
static int flush_midi_buffer( struct midi_out_endpoint *ep )
{
	int ret=0;

	if ( ep->bufWrPtr > 0 ) {
		ret = usb_write( ep, ep->buf, ep->bufWrPtr );
		ep->bufWrPtr = 0;
	}

	return ret;
}


/* ------------------------------------------------------------------------- */


/** Given a MIDI Event, determine size of data to be attached to 
 * USB-MIDI packet.
 * Returns 1, 2 or 3.
 * Called by midiWrite();
 * Uses remains_80e0 and remains_f0f6;
 **/
static int get_remains(int event)
{
	int ret;

	if ( event  < 0x80 ) {
		ret = 1;
	} else if ( event < 0xf0 ) {
		ret = remains_80e0[((event-0x80)>>4)&0x0f];
	} else if ( event < 0xf7 ) {
		ret = remains_f0f6[event-0xf0];
	} else {
		ret = 1;
	}

	return ret;
}

/** Given the output MIDI data in the output buffer, computes a reasonable 
 * CIN.
 * Called by putOneMidiEvent().
 **/
static int get_CIN( struct usb_mididev *m )
{
	int cin;

	if ( m->mout.buf[0] == 0xf7 ) {
		cin = 5;
	}
	else if ( m->mout.buf[1] == 0xf7 ) {
		cin = 6;
	}
	else if ( m->mout.buf[2] == 0xf7 ) {
		cin = 7;
	}
	else {
		if ( m->mout.isInExclusive == 1 ) {
			cin = 4;
		} else if ( m->mout.buf[0] < 0x80 ) {
			/** One byte that we know nothing about. **/
			cin = 0xF; 
		} else if ( m->mout.buf[0] < 0xf0 ) {
			/** MIDI Voice messages 0x8X to 0xEX map to cin 0x8 to 0xE. **/
			cin = (m->mout.buf[0]>>4)&0x0f; 
		}
		else {
			/** Special lookup table exists for real-time events. **/
			cin = cin_f0ff[m->mout.buf[0]-0xf0];
		}
	}

	return cin;
}


/* ------------------------------------------------------------------------- */



/** Move data to USB endpoint buffer.
 *
 **/
static int put_one_midi_event(struct usb_mididev *m)
{
	int cin;
	unsigned long flags;
	struct midi_out_endpoint *ep = m->mout.ep;
	int ret=0;

	cin = get_CIN( m );
	if ( cin > 0x0f || cin < 0 ) {
		return -EINVAL;
	}

	spin_lock_irqsave( &ep->lock, flags );
	ep->buf[ep->bufWrPtr++] = (m->mout.cableId<<4) | cin;
	ep->buf[ep->bufWrPtr++] = m->mout.buf[0];
	ep->buf[ep->bufWrPtr++] = m->mout.buf[1];
	ep->buf[ep->bufWrPtr++] = m->mout.buf[2];
	if ( ep->bufWrPtr >= ep->bufSize ) {
		ret = flush_midi_buffer( ep );
	}
	spin_unlock_irqrestore( &ep->lock, flags);

	m->mout.buf[0] = m->mout.buf[1] = m->mout.buf[2] = 0;
	m->mout.bufPtr = 0;

	return ret;
}

/** Write the MIDI message v on the midi device.
 *  Called by usb_midi_write();
 *  Responsible for packaging a MIDI data stream into USB-MIDI packets.
 **/

static int midi_write( struct usb_mididev *m, int v )
{
	unsigned long flags;
	struct midi_out_endpoint *ep = m->mout.ep;
	int ret=0;
	unsigned char c = (unsigned char)v;
	unsigned char sysrt_buf[4];

	if ( m->singlebyte != 0 ) {
		/** Simple code to handle the single-byte USB-MIDI protocol. */
		spin_lock_irqsave( &ep->lock, flags );
		if ( ep->bufWrPtr+4 > ep->bufSize ) {
			ret = flush_midi_buffer( ep );
			if ( !ret ) {
				spin_unlock_irqrestore( &ep->lock, flags );
				return ret;
			}
		}
		ep->buf[ep->bufWrPtr++] = (m->mout.cableId<<4) |  0x0f; /* single byte */
		ep->buf[ep->bufWrPtr++] = c;
		ep->buf[ep->bufWrPtr++] = 0;
		ep->buf[ep->bufWrPtr++] = 0;
		if ( ep->bufWrPtr >= ep->bufSize ) {
			ret = flush_midi_buffer( ep );
		}
		spin_unlock_irqrestore( &ep->lock, flags );

		return ret;
	}
	/** Normal USB-MIDI protocol begins here. */

	if ( c > 0xf7 ) {	/* system: Realtime messages */
		/** Realtime messages are written IMMEDIATELY. */
		sysrt_buf[0] = (m->mout.cableId<<4) | 0x0f;
		sysrt_buf[1] = c;
		sysrt_buf[2] = 0;
		sysrt_buf[3] = 0;
		spin_lock_irqsave( &ep->lock, flags );
		ret = usb_write( ep, sysrt_buf, 4 );
		spin_unlock_irqrestore( &ep->lock, flags );
		/* m->mout.lastEvent = 0; */

		return ret;
	}

	if ( c >= 0x80 ) {
		if ( c < 0xf0 ) {
			m->mout.lastEvent = c;
			m->mout.isInExclusive = 0;
			m->mout.bufRemains = get_remains(c);
		} else if ( c == 0xf0 ) {
			/* m->mout.lastEvent = 0; */
			m->mout.isInExclusive = 1;
			m->mout.bufRemains = get_remains(c);
		} else if ( c == 0xf7 && m->mout.isInExclusive == 1 ) {
			/* m->mout.lastEvent = 0; */
			m->mout.isInExclusive = 0;
			m->mout.bufRemains = 1;
		} else if ( c > 0xf0 ) {
			/* m->mout.lastEvent = 0; */
			m->mout.isInExclusive = 0;
			m->mout.bufRemains = get_remains(c);
		}
    
	} else if ( m->mout.bufRemains == 0 && m->mout.isInExclusive == 0 ) {
		if ( m->mout.lastEvent == 0 ) {
			return 0; /* discard, waiting for the first event */
		}
		/** track status **/
		m->mout.buf[0] = m->mout.lastEvent;
		m->mout.bufPtr = 1;
		m->mout.bufRemains = get_remains(m->mout.lastEvent)-1;
	}
  
	m->mout.buf[m->mout.bufPtr++] = c;
	m->mout.bufRemains--;
	if ( m->mout.bufRemains == 0 || m->mout.bufPtr >= 3) {
		ret = put_one_midi_event(m);
	}

	return ret;
}


/* ------------------------------------------------------------------------- */

/** Basic operation on /dev/midiXX as registered through struct file_operations.
 *
 *  Basic contract: Used to change the current read/write position in a file.
 *  On success, the non-negative position is reported.
 *  On failure, the negative of an error code is reported.
 *
 *  Because a MIDIStream is not a file, all seek operations are doomed to fail.
 *
 **/
static loff_t usb_midi_llseek(struct file *file, loff_t offset, int origin)
{
	/** Tell user you cannot seek on a PIPE-like device. **/
	return -ESPIPE;
}


/** Basic operation on /dev/midiXX as registered through struct file_operations.
 *
 * Basic contract: Block until count bytes have been read or an error occurs.
 *
 **/

static ssize_t usb_midi_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct usb_mididev *m = (struct usb_mididev *)file->private_data;
	struct midi_in_endpoint *ep = m->min.ep;
	ssize_t ret;
	DECLARE_WAITQUEUE(wait, current);

	if ( !access_ok(VERIFY_READ, buffer, count) ) {
		return -EFAULT;
	}
	if ( count == 0 ) {
		return 0;
	}

	add_wait_queue( &ep->wait, &wait );
	ret = 0;
	while( count > 0 ) {
		int cnt;
		int d = (int)count;

		cnt = m->min.bufRemains;
		if ( cnt > d ) {
			cnt = d;
		}

		if ( cnt <= 0 ) {
			if ( file->f_flags & O_NONBLOCK ) {
				if (!ret) 
					ret = -EAGAIN;
				break;
			}
			__set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			if (signal_pending(current)) {
				if(!ret)
					ret=-ERESTARTSYS;
				break;
			}
			continue;
		}

		{
			int i;
			unsigned long flags; /* used to synchronize access to the endpoint */
			spin_lock_irqsave( &ep->lock, flags );
			for (i = 0; i < cnt; i++) {
				if ( copy_to_user( buffer+i, m->min.buf+m->min.bufRdPtr, 1 ) ) {
					if ( !ret )
						ret = -EFAULT;
					break;
				}
				m->min.bufRdPtr = (m->min.bufRdPtr+1)%MIDI_IN_BUFSIZ;
				m->min.bufRemains -= 1;
			}
			spin_unlock_irqrestore( &ep->lock, flags );
		}

		count-=cnt;
		buffer+=cnt;
		ret+=cnt;

		break;
	}

	remove_wait_queue( &ep->wait, &wait );
	set_current_state(TASK_RUNNING);

	return ret;
}


/** Basic operation on /dev/midiXX as registered through struct file_operations.
 *
 *  Basic Contract: Take MIDI data byte-by-byte and pass it to
 *  writeMidi() which packages MIDI data into USB-MIDI stream.
 *  Then flushMidiData() is called to ensure all bytes have been written
 *  in a timely fashion.
 *
 **/

static ssize_t usb_midi_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct usb_mididev *m = (struct usb_mididev *)file->private_data;
	ssize_t ret;
	unsigned long int flags;

	if ( !access_ok(VERIFY_READ, buffer, count) ) {
		return -EFAULT;
	}
	if ( count == 0 ) {
		return 0;
	}

	ret = 0;
	while( count > 0 ) {
		unsigned char c;

		if (copy_from_user((unsigned char *)&c, buffer, 1)) {
			if ( ret == 0 )
				ret = -EFAULT;
			break;
		}
		if( midi_write(m, (int)c) ) {
			if ( ret == 0 )
				ret = -EFAULT;
			break;
		}
		count--;
		buffer++;
		ret++;
	}

	spin_lock_irqsave( &m->mout.ep->lock, flags );
	if ( flush_midi_buffer(m->mout.ep) < 0 ) {
		ret = -EFAULT;
	}
	spin_unlock_irqrestore( &m->mout.ep->lock, flags );

	return ret;
}

/** Basic operation on /dev/midiXX as registered through struct file_operations.
 *
 * Basic contract:  Wait (spin) until ready to read or write on the file.
 *
 **/
static unsigned int usb_midi_poll(struct file *file, struct poll_table_struct *wait)
{
	struct usb_mididev *m = (struct usb_mididev *)file->private_data;
	struct midi_in_endpoint *iep = m->min.ep;
	struct midi_out_endpoint *oep = m->mout.ep;
	unsigned long flags;
	unsigned int mask = 0;
  
	if ( file->f_mode & FMODE_READ ) {
		poll_wait( file, &iep->wait, wait );
		spin_lock_irqsave( &iep->lock, flags );
		if ( m->min.bufRemains > 0 )
			mask |= POLLIN | POLLRDNORM;
		spin_unlock_irqrestore( &iep->lock, flags );
	}

	if ( file->f_mode & FMODE_WRITE ) {
		poll_wait( file, &oep->wait, wait );
		spin_lock_irqsave( &oep->lock, flags );
		if ( oep->bufWrPtr < oep->bufSize )
			mask |= POLLOUT | POLLWRNORM;
		spin_unlock_irqrestore( &oep->lock, flags );
	}

	return mask;
}


/** Basic operation on /dev/midiXX as registered through struct file_operations.
 *
 * Basic contract: This is always the first operation performed on the
 * device node. If no method is defined, the open succeeds without any
 * notification given to the module.
 *
 **/

static int usb_midi_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	struct usb_midi_state *s;
	struct usb_mididev    *m;
	unsigned long flags;
	int succeed = 0;

#if 0
	printk(KERN_INFO "usb-midi: Open minor= %d.\n", minor);
#endif

	for(;;) {
		down(&open_sem);
		list_for_each_entry(s, &mididevs, mididev) {
			list_for_each_entry(m, &s->midiDevList, list) {
				if ( !((m->dev_midi ^ minor) & ~0xf) )
					goto device_found;
			}
		}
		up(&open_sem);
		return -ENODEV;

	device_found:
		if ( !s->usbdev ) {
			up(&open_sem);
			return -EIO;
		}
		if ( !(m->open_mode & file->f_mode) ) {
			break;
		}
		if ( file->f_flags & O_NONBLOCK ) {
			up(&open_sem);
			return -EBUSY;
		}
		__set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue( &open_wait, &wait );
		up(&open_sem);
		schedule();
		remove_wait_queue( &open_wait, &wait );
		if ( signal_pending(current) ) {
			return -ERESTARTSYS;
		}
	}

	file->private_data = m;
	spin_lock_irqsave( &s->lock, flags );

	if ( !(m->open_mode & (FMODE_READ | FMODE_WRITE)) ) {
		//FIXME: intented semantics unclear here
		m->min.bufRdPtr       = 0;
		m->min.bufWrPtr       = 0;
		m->min.bufRemains     = 0;
		spin_lock_init(&m->min.ep->lock);

		m->mout.bufPtr        = 0;
		m->mout.bufRemains    = 0;
		m->mout.isInExclusive = 0;
		m->mout.lastEvent     = 0;
		spin_lock_init(&m->mout.ep->lock);
	}

	if ( (file->f_mode & FMODE_READ) && m->min.ep != NULL ) {
		unsigned long int flagsep;
		spin_lock_irqsave( &m->min.ep->lock, flagsep );
		m->min.ep->cables[m->min.cableId] = m;
		m->min.ep->readers += 1;
		m->min.bufRdPtr       = 0;
		m->min.bufWrPtr       = 0;
		m->min.bufRemains     = 0;
		spin_unlock_irqrestore( &m->min.ep->lock, flagsep );

		if ( !(m->min.ep->urbSubmitted)) {

			/* urb->dev must be reinitialized on 2.4.x kernels */
			m->min.ep->urb->dev = m->min.ep->usbdev;

			if ( usb_submit_urb(m->min.ep->urb, GFP_ATOMIC) ) {
				printk(KERN_ERR "usbmidi: Cannot submit urb for MIDI-IN\n");
			}
			m->min.ep->urbSubmitted = 1;
		}
		m->open_mode |= FMODE_READ;
		succeed = 1;
	}

	if ( (file->f_mode & FMODE_WRITE) && m->mout.ep != NULL ) {
		m->mout.bufPtr        = 0;
		m->mout.bufRemains    = 0;
		m->mout.isInExclusive = 0;
		m->mout.lastEvent     = 0;
		m->open_mode |= FMODE_WRITE;
		succeed = 1;
	}

	spin_unlock_irqrestore( &s->lock, flags );

	s->count++;
	up(&open_sem);

	/** Changed to prevent extra increments to USE_COUNT. **/
	if (!succeed) {
		return -EBUSY;
	}

#if 0
	printk(KERN_INFO "usb-midi: Open Succeeded. minor= %d.\n", minor);
#endif

	return nonseekable_open(inode, file); /** Success. **/
}


/** Basic operation on /dev/midiXX as registered through struct file_operations.
 *
 *  Basic contract: Close an opened file and deallocate anything we allocated.
 *  Like open(), this can be missing. If open set file->private_data,
 *  release() must clear it.
 *
 **/

static int usb_midi_release(struct inode *inode, struct file *file)
{
	struct usb_mididev *m = (struct usb_mididev *)file->private_data;
	struct usb_midi_state *s = (struct usb_midi_state *)m->midi;

#if 0
	printk(KERN_INFO "usb-midi: Close.\n");
#endif

	down(&open_sem);

	if ( m->open_mode & FMODE_WRITE ) {
		m->open_mode &= ~FMODE_WRITE;
		usb_kill_urb( m->mout.ep->urb );
	}

	if ( m->open_mode & FMODE_READ ) {
	        unsigned long int flagsep;
	        spin_lock_irqsave( &m->min.ep->lock, flagsep );
                m->min.ep->cables[m->min.cableId] = NULL; // discard cable
                m->min.ep->readers -= 1;
		m->open_mode &= ~FMODE_READ;
		if ( m->min.ep->readers == 0 &&
                     m->min.ep->urbSubmitted ) {
			m->min.ep->urbSubmitted = 0;
			usb_kill_urb(m->min.ep->urb);
		}
	        spin_unlock_irqrestore( &m->min.ep->lock, flagsep );
	}

	s->count--;

	up(&open_sem);
	wake_up(&open_wait);

	file->private_data = NULL;
	return 0;
}

static struct file_operations usb_midi_fops = {
	.owner =	THIS_MODULE,
	.llseek =	usb_midi_llseek,
	.read =		usb_midi_read,
	.write =	usb_midi_write,
	.poll =		usb_midi_poll,
	.open =		usb_midi_open,
	.release =	usb_midi_release,
};

/* ------------------------------------------------------------------------- */

/** Returns filled midi_in_endpoint structure or null on failure.
 *
 * Parameters:
 *	d        - a usb_device
 *	endPoint - An usb endpoint in the range 0 to 15.
 * Called by allocUsbMidiDev();
 *
 **/

static struct midi_in_endpoint *alloc_midi_in_endpoint( struct usb_device *d, int endPoint )
{
	struct midi_in_endpoint *ep;
	int bufSize;
	int pipe;

	endPoint &= 0x0f; /* Silently force endPoint to lie in range 0 to 15. */

	pipe =  usb_rcvbulkpipe( d, endPoint );
	bufSize = usb_maxpacket( d, pipe, 0 );
	/* usb_pipein() = ! usb_pipeout() = true for an in Endpoint */

	ep = (struct midi_in_endpoint *)kmalloc(sizeof(struct midi_in_endpoint), GFP_KERNEL);
	if ( !ep ) {
		printk(KERN_ERR "usbmidi: no memory for midi in-endpoint\n");
		return NULL;
	}
	memset( ep, 0, sizeof(struct midi_in_endpoint) );
//      this sets cables[] and readers to 0, too.
//      for (i=0; i<16; i++) ep->cables[i] = 0; // discard cable
//      ep->readers = 0;

	ep->endpoint = endPoint;

	ep->recvBuf = (unsigned char *)kmalloc(sizeof(unsigned char)*(bufSize), GFP_KERNEL);
	if ( !ep->recvBuf ) {
		printk(KERN_ERR "usbmidi: no memory for midi in-endpoint buffer\n");
		kfree(ep);
		return NULL;
	}

	ep->urb = usb_alloc_urb(0, GFP_KERNEL); /* no ISO */
	if ( !ep->urb ) {
		printk(KERN_ERR "usbmidi: no memory for midi in-endpoint urb\n");
		kfree(ep->recvBuf);
		kfree(ep);
		return NULL;
	}
	usb_fill_bulk_urb( ep->urb, d, 
		       usb_rcvbulkpipe(d, endPoint),
		       (unsigned char *)ep->recvBuf, bufSize,
		       usb_bulk_read, ep );

	/* ep->bufRdPtr     = 0; */
	/* ep->bufWrPtr     = 0; */
	/* ep->bufRemains   = 0; */
	/* ep->urbSubmitted = 0; */
	ep->recvBufSize  = bufSize;

	init_waitqueue_head(&ep->wait);

	return ep;
}

static int remove_midi_in_endpoint( struct midi_in_endpoint *min )
{
	usb_kill_urb( min->urb );
	usb_free_urb( min->urb );
	kfree( min->recvBuf );
	kfree( min );

	return 0;
}

/** Returns filled midi_out_endpoint structure or null on failure.
 *
 * Parameters:
 *	d        - a usb_device
 *	endPoint - An usb endpoint in the range 0 to 15.
 * Called by allocUsbMidiDev();
 *
 **/
static struct midi_out_endpoint *alloc_midi_out_endpoint( struct usb_device *d, int endPoint )
{
	struct midi_out_endpoint *ep = NULL;
	int pipe;
	int bufSize;

	endPoint &= 0x0f;
	pipe =  usb_sndbulkpipe( d, endPoint );
	bufSize = usb_maxpacket( d, pipe, 1 );

	ep = (struct midi_out_endpoint *)kmalloc(sizeof(struct midi_out_endpoint), GFP_KERNEL);
	if ( !ep ) {
		printk(KERN_ERR "usbmidi: no memory for midi out-endpoint\n");
		return NULL;
	}
	memset( ep, 0, sizeof(struct midi_out_endpoint) );

	ep->endpoint = endPoint;
	ep->buf = (unsigned char *)kmalloc(sizeof(unsigned char)*bufSize, GFP_KERNEL);
	if ( !ep->buf ) {
		printk(KERN_ERR "usbmidi: no memory for midi out-endpoint buffer\n");
		kfree(ep);
		return NULL;
	}

	ep->urb = usb_alloc_urb(0, GFP_KERNEL); /* no ISO */
	if ( !ep->urb ) {
		printk(KERN_ERR "usbmidi: no memory for midi out-endpoint urb\n");
		kfree(ep->buf);
		kfree(ep);
		return NULL;
	}

	ep->bufSize       = bufSize;
	/* ep->bufWrPtr      = 0; */

	init_waitqueue_head(&ep->wait);

	return ep;
}


static int remove_midi_out_endpoint( struct midi_out_endpoint *mout )
{
	usb_kill_urb( mout->urb );
	usb_free_urb( mout->urb );
	kfree( mout->buf );
	kfree( mout );

	return 0;
}


/** Returns a filled usb_mididev structure, registered as a Linux MIDI device.
 *
 * Returns null if memory is not available or the device cannot be registered.
 * Called by allocUsbMidiDev();
 *
 **/
static struct usb_mididev *allocMidiDev(
	struct usb_midi_state *s,
	struct midi_in_endpoint *min,
	struct midi_out_endpoint *mout,
	int inCableId,
	int outCableId )
{
	struct usb_mididev *m;

	m = (struct usb_mididev *)kmalloc(sizeof(struct usb_mididev), GFP_KERNEL);
	if (!m) {
		printk(KERN_ERR "usbmidi: no memory for midi device\n");
		return NULL;
	}

	memset(m, 0, sizeof(struct usb_mididev));

	if ((m->dev_midi = register_sound_midi(&usb_midi_fops, -1)) < 0) {
		printk(KERN_ERR "usbmidi: cannot register midi device\n");
		kfree(m);
		return NULL;
	}

	m->midi               = s;
	/* m->open_mode          = 0; */

	if ( min ) {
		m->min.ep             = min;
		m->min.ep->usbdev     = s->usbdev;
		m->min.cableId        = inCableId;
	}
	/* m->min.bufPtr         = 0; */
	/* m->min.bufRemains     = 0; */

	if ( mout ) {
		m->mout.ep            = mout;
		m->mout.ep->usbdev    = s->usbdev;
		m->mout.cableId       = outCableId;
	}
	/* m->mout.bufPtr        = 0; */
	/* m->mout.bufRemains    = 0; */
	/* m->mout.isInExclusive = 0; */
	/* m->mout.lastEvent     = 0; */

	m->singlebyte         = singlebyte;

	return m;
}


static void release_midi_device( struct usb_midi_state *s )
{
	struct usb_mididev *m;
	struct midi_in_endpoint *min;
	struct midi_out_endpoint *mout;

	if ( s->count > 0 ) {
		up(&open_sem);
		return;
	}
	up( &open_sem );
	wake_up( &open_wait );

	while(!list_empty(&s->inEndpointList)) {
		min = list_entry(s->inEndpointList.next, struct midi_in_endpoint, list);
		list_del(&min->list);
		remove_midi_in_endpoint(min);
	}

	while(!list_empty(&s->outEndpointList)) {
		mout = list_entry(s->outEndpointList.next, struct midi_out_endpoint, list);
		list_del(&mout->list);
		remove_midi_out_endpoint(mout);
	}

	while(!list_empty(&s->midiDevList)) {
		m = list_entry(s->midiDevList.next, struct usb_mididev, list);
		list_del(&m->list);
		kfree(m);
	}

	kfree(s);

	return;
}


/* ------------------------------------------------------------------------- */

/** Utility routine to find a descriptor in a dump of many descriptors.
 * Returns start of descriptor or NULL if not found. 
 * descStart pointer to list of interfaces.
 * descLength length (in bytes) of dump
 * after (ignored if NULL) this routine returns only descriptors after "after"
 * dtype (mandatory) The descriptor type.
 * iface (ignored if -1) returns descriptor at/following given interface
 * altSetting (ignored if -1) returns descriptor at/following given altSetting
 *
 *
 *  Called by parseDescriptor(), find_csinterface_descriptor();
 *
 */
static void *find_descriptor( void *descStart, unsigned int descLength, void *after, unsigned char dtype, int iface, int altSetting )
{
	unsigned char *p, *end, *next;
	int interfaceNumber = -1, altSet = -1;

	p = descStart;
	end = p + descLength;
	for( ; p < end; ) {
		if ( p[0] < 2 )
			return NULL;
		next = p + p[0];
		if ( next > end )
			return NULL;
		if ( p[1] == USB_DT_INTERFACE ) {
			if ( p[0] < USB_DT_INTERFACE_SIZE )
				return NULL;
			interfaceNumber = p[2];
			altSet = p[3];
		}
		if ( p[1] == dtype &&
		     ( !after || ( p > (unsigned char *)after) ) &&
		     ( ( iface == -1) || (iface == interfaceNumber) ) &&
		     ( (altSetting == -1) || (altSetting == altSet) )) {
			return p;
		}
		p = next;
	}
	return NULL;
}

/** Utility to find a class-specific interface descriptor.
 *  dsubtype is a descriptor subtype
 *  Called by parseDescriptor();
 **/
static void *find_csinterface_descriptor(void *descStart, unsigned int descLength, void *after, u8 dsubtype, int iface, int altSetting)
{
	unsigned char *p;
  
	p = find_descriptor( descStart, descLength, after, USB_DT_CS_INTERFACE, iface, altSetting );
	while ( p ) {
		if ( p[0] >= 3 && p[2] == dsubtype )
			return p;
		p = find_descriptor( descStart, descLength, p, USB_DT_CS_INTERFACE, 
				     iface, altSetting );
	}
	return NULL;
}


/** The magic of making a new usb_midi_device from config happens here.
 *
 * The caller is responsible for free-ing this return value (if not NULL).
 *
 **/
static struct usb_midi_device *parse_descriptor( struct usb_device *d, unsigned char *buffer, int bufSize, unsigned int ifnum , unsigned int altSetting, int quirks)
{
	struct usb_midi_device *u;
	unsigned char *p1;
	unsigned char *p2;
	unsigned char *next;
	int iep, oep;
	int length;
	unsigned long longBits;
	int pins, nbytes, offset, shift, jack;
#ifdef HAVE_JACK_STRINGS
	/** Jacks can have associated names.  **/
	unsigned char jack2string[256];
#endif

	u = NULL;
	/* find audiocontrol interface */
	p1 = find_csinterface_descriptor( buffer, bufSize, NULL,
					  MS_HEADER, ifnum, altSetting);

	if ( !p1 ) {
		goto error_end;
	}

	if ( p1[0] < MS_HEADER_LENGTH ) {
		goto error_end;
	}

	/* Assume success. Since the device corresponds to USB-MIDI spec, we assume
	   that the rest of the USB 2.0 spec is obeyed. */

	u = (struct usb_midi_device *)kmalloc( sizeof(struct usb_midi_device), GFP_KERNEL );
	if ( !u ) {
		return NULL;
	}
	u->deviceName = NULL;
	u->idVendor = le16_to_cpu(d->descriptor.idVendor);
	u->idProduct = le16_to_cpu(d->descriptor.idProduct);
	u->interface = ifnum;
	u->altSetting = altSetting;
	u->in[0].endpoint = -1;
	u->in[0].cableId = -1;
	u->out[0].endpoint = -1;
	u->out[0].cableId = -1;


	printk(KERN_INFO "usb-midi: Found MIDIStreaming device corresponding to Release %d.%02d of spec.\n",
	       (p1[4] >> 4) * 10 + (p1[4] & 0x0f ),
	       (p1[3] >> 4) * 10 + (p1[3] & 0x0f )
		);

	length = p1[5] | (p1[6] << 8);

#ifdef HAVE_JACK_STRINGS
	memset(jack2string, 0, sizeof(unsigned char) * 256);
#endif

	length -= p1[0];
	for (p2 = p1 + p1[0]; length > 0; p2 = next) {
		next = p2 + p2[0];
		length -= p2[0];

		if (p2[0] < 2 )
			break;
		if (p2[1] != USB_DT_CS_INTERFACE)
			break;
		if (p2[2] == MIDI_IN_JACK && p2[0] >= 6 ) {
			jack = p2[4];
#ifdef HAVE_JACK_STRINGS
			jack2string[jack] = p2[5];
#endif
			printk(KERN_INFO "usb-midi: Found IN Jack 0x%02x %s\n",
			       jack, (p2[3] == EMBEDDED_JACK)?"EMBEDDED":"EXTERNAL" );
		} else if ( p2[2] == MIDI_OUT_JACK && p2[0] >= 6) {
			pins = p2[5];
			if ( p2[0] < (6 + 2 * pins) )
				continue;
			jack = p2[4];
#ifdef HAVE_JACK_STRINGS
			jack2string[jack] = p2[5 + 2 * pins];
#endif
			printk(KERN_INFO "usb-midi: Found OUT Jack 0x%02x %s, %d pins\n",
			       jack, (p2[3] == EMBEDDED_JACK)?"EMBEDDED":"EXTERNAL", pins );
		} else if ( p2[2] == ELEMENT_DESCRIPTOR  && p2[0]  >= 10) {
			pins = p2[4];
			if ( p2[0] < (9 + 2 * pins ) )
				continue;
			nbytes = p2[8 + 2 * pins ];
			if ( p2[0] < (10 + 2 * pins + nbytes) )
				continue;
			longBits = 0L;
			for ( offset = 0, shift = 0; offset < nbytes && offset < 8; offset ++, shift += 8) {
				longBits |= ((long)(p2[9 + 2 * pins + offset])) << shift;
			}
			jack = p2[3];
#ifdef HAVE_JACK_STRINGS
			jack2string[jack] = p2[9 + 2 * pins + nbytes];
#endif
			printk(KERN_INFO "usb-midi: Found ELEMENT 0x%02x, %d/%d pins in/out, bits: 0x%016lx\n",
			       jack, pins, (int)(p2[5 + 2 * pins]), (long)longBits );
		} else {
		}
	}

	iep=0;
	oep=0;

	if (quirks==0) {
		/* MIDISTREAM */
		p2 = NULL;
		for (p1 = find_descriptor(buffer, bufSize, NULL, USB_DT_ENDPOINT,
					  ifnum, altSetting ); p1; p1 = next ) {
			next = find_descriptor(buffer, bufSize, p1, USB_DT_ENDPOINT,
					       ifnum, altSetting ); 
			p2 = find_descriptor(buffer, bufSize, p1, USB_DT_CS_ENDPOINT,
					     ifnum, altSetting ); 

			if ( p2 && next && ( p2 > next ) )
				p2 = NULL;

			if ( p1[0] < 9 || !p2 || p2[0] < 4 )
				continue;

			if ( (p1[2] & 0x80) == 0x80 ) {
				if ( iep < 15 ) {
					pins = p2[3]; /* not pins -- actually "cables" */
					if ( pins > 16 )
						pins = 16;
					u->in[iep].endpoint = p1[2];
					u->in[iep].cableId = ( 1 << pins ) - 1;
					if ( u->in[iep].cableId )
						iep ++;
					if ( iep < 15 ) {
						u->in[iep].endpoint = -1;
						u->in[iep].cableId = -1;
					}
				}
			} else {
				if ( oep < 15 ) {
					pins = p2[3]; /* not pins -- actually "cables" */
					if ( pins > 16 )
						pins = 16;
					u->out[oep].endpoint = p1[2];
					u->out[oep].cableId = ( 1 << pins ) - 1;
					if ( u->out[oep].cableId )
						oep ++;
					if ( oep < 15 ) {
						u->out[oep].endpoint = -1;
						u->out[oep].cableId = -1;
					}
				}
			}
	
		}
	} else if (quirks==1) {
		/* YAMAHA quirks */
		for (p1 = find_descriptor(buffer, bufSize, NULL, USB_DT_ENDPOINT,
					  ifnum, altSetting ); p1; p1 = next ) {
			next = find_descriptor(buffer, bufSize, p1, USB_DT_ENDPOINT,
					       ifnum, altSetting ); 
	
			if ( p1[0] < 7 )
				continue;

			if ( (p1[2] & 0x80) == 0x80 ) {
				if ( iep < 15 ) {
					pins = iep+1;
					if ( pins > 16 )
						pins = 16;
					u->in[iep].endpoint = p1[2];
					u->in[iep].cableId = ( 1 << pins ) - 1;
					if ( u->in[iep].cableId )
						iep ++;
					if ( iep < 15 ) {
						u->in[iep].endpoint = -1;
						u->in[iep].cableId = -1;
					}
				}
			} else {
				if ( oep < 15 ) {
					pins = oep+1;
					u->out[oep].endpoint = p1[2];
					u->out[oep].cableId = ( 1 << pins ) - 1;
					if ( u->out[oep].cableId )
						oep ++;
					if ( oep < 15 ) {
						u->out[oep].endpoint = -1;
						u->out[oep].cableId = -1;
					}
				}
			}
	
		}
	}

	if ( !iep && ! oep ) {
		goto error_end;
	}

	return u;

error_end:
	kfree(u);
	return NULL;
}

/* ------------------------------------------------------------------------- */

/** Returns number between 0 and 16.
 *
 **/
static int on_bits( unsigned short v )
{
	int i;
	int ret=0;

	for ( i=0 ; i<16 ; i++ ) {
		if ( v & (1<<i) )
			ret++;
	}

	return ret;
}


/** USB-device will be interrogated for altSetting.
 *
 * Returns negative on error.
 * Called by allocUsbMidiDev();
 *
 **/

static int get_alt_setting( struct usb_device *d, int ifnum )
{
	int alts, alt=0;
	struct usb_interface *iface;
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *ep;
	int epin, epout;
	int i;

	iface = usb_ifnum_to_if( d, ifnum );
	alts = iface->num_altsetting;

	for ( alt=0 ; alt<alts ; alt++ ) {
		interface = &iface->altsetting[alt];
		epin = -1;
		epout = -1;

		for ( i=0 ; i<interface->desc.bNumEndpoints ; i++ ) {
			ep = &interface->endpoint[i].desc;
			if ( (ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_BULK ) {
				continue;
			}
			if ( (ep->bEndpointAddress & USB_DIR_IN) && epin < 0 ) {
				epin = i;
			} else if ( epout < 0 ) {
				epout = i;
			}
			if ( epin >= 0 && epout >= 0 ) {
				return interface->desc.bAlternateSetting;
			}
		}
	}

	return -ENODEV;
}


/* ------------------------------------------------------------------------- */


/** Returns 0 if successful in allocating and registering internal structures.
 * Returns negative on failure.
 * Calls allocMidiDev which additionally registers /dev/midiXX devices.
 * Writes messages on success to indicate which /dev/midiXX is which physical
 * endpoint.
 *
 **/
static int alloc_usb_midi_device( struct usb_device *d, struct usb_midi_state *s, struct usb_midi_device *u )
{
	struct usb_mididev **mdevs=NULL;
	struct midi_in_endpoint *mins[15], *min;
	struct midi_out_endpoint *mouts[15], *mout;
	int inDevs=0, outDevs=0;
	int inEndpoints=0, outEndpoints=0;
	int inEndpoint, outEndpoint;
	int inCableId, outCableId;
	int i;
	int devices = 0;
	int alt = 0;

	/* Obtain altSetting or die.. */
	alt = u->altSetting;
	if ( alt < 0 ) {
		alt = get_alt_setting( d, u->interface );
	}
	if ( alt < 0 )
		return -ENXIO;

	/* Configure interface */
	if ( usb_set_interface( d, u->interface, alt ) < 0 ) {
		return -ENXIO;
	}

	for ( i = 0 ; i < 15 ; i++ ) {
		mins[i] = NULL;
		mouts[i] = NULL;
	}

	/* Begin Allocation */
	while( inEndpoints < 15
	       && inDevs < maxdevices
	       && u->in[inEndpoints].cableId >= 0 ) {
		inDevs += on_bits((unsigned short)u->in[inEndpoints].cableId);
		mins[inEndpoints] = alloc_midi_in_endpoint( d, u->in[inEndpoints].endpoint );
		if ( mins[inEndpoints] == NULL )
			goto error_end;
		inEndpoints++;
	}

	while( outEndpoints < 15
	       && outDevs < maxdevices
	       && u->out[outEndpoints].cableId >= 0 ) {
		outDevs += on_bits((unsigned short)u->out[outEndpoints].cableId);
		mouts[outEndpoints] = alloc_midi_out_endpoint( d, u->out[outEndpoints].endpoint );
		if ( mouts[outEndpoints] == NULL )
			goto error_end;
		outEndpoints++;
	}

	devices = inDevs > outDevs ? inDevs : outDevs;
	devices = maxdevices > devices ? devices : maxdevices;

	/* obtain space for device name (iProduct) if not known. */
	if ( ! u->deviceName ) {
		mdevs = (struct usb_mididev **)
			kmalloc(sizeof(struct usb_mididevs *)*devices
				+ sizeof(char) * 256, GFP_KERNEL);
	} else {
		mdevs = (struct usb_mididev **)
			kmalloc(sizeof(struct usb_mididevs *)*devices, GFP_KERNEL);
	}

	if ( !mdevs ) {
		/* devices = 0; */
		/* mdevs = NULL; */
		goto error_end;
	}
	for ( i=0 ; i<devices ; i++ ) {
		mdevs[i] = NULL;
	}

	/* obtain device name (iProduct) if not known. */
	if ( ! u->deviceName ) {
		u->deviceName = (char *) (mdevs + devices);
		if ( ! d->have_langid && d->descriptor.iProduct) {
			alt = usb_get_string(d, 0, 0, u->deviceName, 250);
			if (alt < 0) {
				printk(KERN_INFO "error getting string descriptor 0 (error=%d)\n", alt);
			} else if (u->deviceName[0] < 4) {
				printk(KERN_INFO "string descriptor 0 too short (length = %d)\n", alt);
			} else {
				printk(KERN_INFO "string descriptor 0 found (length = %d)\n", alt);
				for(; alt >= 4; alt -= 2) {
					i = u->deviceName[alt-2] | (u->deviceName[alt-1]<< 8);
					printk(KERN_INFO "usb-midi: langid(%d) 0x%04x\n",
					       (alt-4) >> 1, i);
					if ( ( ( i ^ ulangid ) & 0xff ) == 0 ) {
						d->have_langid = 1;
						d->string_langid = i;
						printk(KERN_INFO "usb-midi: langid(match) 0x%04x\n", i);
						if ( i == ulangid )
							break;
					}
				}
			}
		}
		u->deviceName[0] = (char) 0;
		if (d->descriptor.iProduct) {
			printk(KERN_INFO "usb-midi: fetchString(%d)\n", d->descriptor.iProduct);
			alt = usb_string(d, d->descriptor.iProduct, u->deviceName, 255);
			if( alt < 0 ) {
				u->deviceName[0] = (char) 0;
			}
			printk(KERN_INFO "usb-midi: fetchString = %d\n", alt);
		} 
		/* Failsafe */
		if ( !u->deviceName[0] ) {
			if (le16_to_cpu(d->descriptor.idVendor) == USB_VENDOR_ID_ROLAND ) {
				strcpy(u->deviceName, "Unknown Roland");
			} else if (le16_to_cpu(d->descriptor.idVendor) == USB_VENDOR_ID_STEINBERG  ) {
				strcpy(u->deviceName, "Unknown Steinberg");
			} else if (le16_to_cpu(d->descriptor.idVendor) == USB_VENDOR_ID_YAMAHA ) {
				strcpy(u->deviceName, "Unknown Yamaha");
			} else {
				strcpy(u->deviceName, "Unknown");
			}
		}
	}

	inEndpoint  = 0; inCableId  = -1;
	outEndpoint = 0; outCableId = -1;

	for ( i=0 ; i<devices ; i++ ) {
		for ( inCableId ++ ;
		      inEndpoint <15
			      && mins[inEndpoint] 
			      && !(u->in[inEndpoint].cableId & (1<<inCableId)) ;
		      inCableId++ ) {
			if ( inCableId >= 16 ) {
				inEndpoint  ++;
				inCableId  = 0;
			}
		}
		min  = mins[inEndpoint];
		for ( outCableId ++ ;
		      outEndpoint <15
			      && mouts[outEndpoint] 
			      && !(u->out[outEndpoint].cableId & (1<<outCableId)) ;
		      outCableId++ ) {
			if ( outCableId >= 16 ) {
				outEndpoint  ++;
				outCableId  = 0;
			}
		}
		mout = mouts[outEndpoint];

		mdevs[i] = allocMidiDev( s, min, mout, inCableId, outCableId );
		if ( mdevs[i] == NULL )
			goto error_end;

	}

	/* Success! */
	for ( i=0 ; i<devices ; i++ ) {
		list_add_tail( &mdevs[i]->list, &s->midiDevList );
	}
	for ( i=0 ; i<inEndpoints ; i++ ) {
		list_add_tail( &mins[i]->list, &s->inEndpointList );
	}
	for ( i=0 ; i<outEndpoints ; i++ ) {
		list_add_tail( &mouts[i]->list, &s->outEndpointList );
	}

	printk(KERN_INFO "usbmidi: found [ %s ] (0x%04x:0x%04x), attached:\n", u->deviceName, u->idVendor, u->idProduct );
	for ( i=0 ; i<devices ; i++ ) {
		int dm = (mdevs[i]->dev_midi-2)>>4;
		if ( mdevs[i]->mout.ep != NULL && mdevs[i]->min.ep != NULL ) {
			printk(KERN_INFO "usbmidi: /dev/midi%02d: in (ep:%02x cid:%2d bufsiz:%2d) out (ep:%02x cid:%2d bufsiz:%2d)\n", 
			       dm,
			       mdevs[i]->min.ep->endpoint|USB_DIR_IN, mdevs[i]->min.cableId, mdevs[i]->min.ep->recvBufSize,
			       mdevs[i]->mout.ep->endpoint, mdevs[i]->mout.cableId, mdevs[i]->mout.ep->bufSize);
		} else if ( mdevs[i]->min.ep != NULL ) {
			printk(KERN_INFO "usbmidi: /dev/midi%02d: in (ep:%02x cid:%2d bufsiz:%02d)\n", 
			       dm,
			       mdevs[i]->min.ep->endpoint|USB_DIR_IN, mdevs[i]->min.cableId, mdevs[i]->min.ep->recvBufSize);
		} else if ( mdevs[i]->mout.ep != NULL ) {
			printk(KERN_INFO "usbmidi: /dev/midi%02d: out (ep:%02x cid:%2d bufsiz:%02d)\n", 
			       dm,
			       mdevs[i]->mout.ep->endpoint, mdevs[i]->mout.cableId, mdevs[i]->mout.ep->bufSize);
		}
	}

	kfree(mdevs);
	return 0;

 error_end:
	if ( mdevs != NULL ) {
		for ( i=0 ; i<devices ; i++ ) {
			if ( mdevs[i] != NULL ) {
				unregister_sound_midi( mdevs[i]->dev_midi );
				kfree(mdevs[i]);
			}
		}
		kfree(mdevs);
	}

	for ( i=0 ; i<15 ; i++ ) {
		if ( mins[i] != NULL ) {
			remove_midi_in_endpoint( mins[i] );
		}
		if ( mouts[i] != NULL ) {
			remove_midi_out_endpoint( mouts[i] );
		}
	}

	return -ENOMEM;
}

/* ------------------------------------------------------------------------- */

/** Attempt to scan YAMAHA's device descriptor and detect correct values of
 *  them.
 *  Return 0 on succes, negative on failure.
 *  Called by usb_midi_probe();
 **/

static int detect_yamaha_device( struct usb_device *d,
		struct usb_interface *iface, unsigned int ifnum,
		struct usb_midi_state *s)
{
	struct usb_host_interface *interface;
	struct usb_midi_device *u;
	unsigned char *buffer;
	int bufSize;
	int i;
	int alts=-1;
	int ret;

	if (le16_to_cpu(d->descriptor.idVendor) != USB_VENDOR_ID_YAMAHA) {
		return -EINVAL;
	}

	for ( i=0 ; i < iface->num_altsetting; i++ ) {
		interface = iface->altsetting + i;

		if ( interface->desc.bInterfaceClass != 255 ||
		     interface->desc.bInterfaceSubClass != 0 )
			continue;
		alts = interface->desc.bAlternateSetting;
	}
	if ( alts == -1 ) {
		return -EINVAL;
	}

	printk(KERN_INFO "usb-midi: Found YAMAHA USB-MIDI device on dev %04x:%04x, iface %d\n",
	       le16_to_cpu(d->descriptor.idVendor),
	       le16_to_cpu(d->descriptor.idProduct), ifnum);

	i = d->actconfig - d->config;
	buffer = d->rawdescriptors[i];
	bufSize = le16_to_cpu(d->actconfig->desc.wTotalLength);

	u = parse_descriptor( d, buffer, bufSize, ifnum, alts, 1);
	if ( u == NULL ) {
		return -EINVAL;
	}

	ret = alloc_usb_midi_device( d, s, u );

	kfree(u);

	return ret;
}


/** Scan table of known devices which are only partially compliant with 
 * the MIDIStreaming specification.
 * Called by usb_midi_probe();
 *
 **/

static int detect_vendor_specific_device( struct usb_device *d, unsigned int ifnum, struct usb_midi_state *s )
{
	struct usb_midi_device *u;
	int i;
	int ret = -ENXIO;

	for ( i=0; i<VENDOR_SPECIFIC_USB_MIDI_DEVICES ; i++ ) {
		u=&(usb_midi_devices[i]);
    
		if ( le16_to_cpu(d->descriptor.idVendor) != u->idVendor ||
		     le16_to_cpu(d->descriptor.idProduct) != u->idProduct ||
		     ifnum != u->interface )
			continue;

		ret = alloc_usb_midi_device( d, s, u );
		break;
	}

	return ret;
}


/** Attempt to match any config of an interface to a MIDISTREAMING interface.
 *  Returns 0 on success, negative on failure.
 * Called by usb_midi_probe();
 **/
static int detect_midi_subclass(struct usb_device *d,
		struct usb_interface *iface, unsigned int ifnum,
		struct usb_midi_state *s)
{
	struct usb_host_interface *interface;
	struct usb_midi_device *u;
	unsigned char *buffer;
	int bufSize;
	int i;
	int alts=-1;
	int ret;

	for ( i=0 ; i < iface->num_altsetting; i++ ) {
		interface = iface->altsetting + i;

		if ( interface->desc.bInterfaceClass != USB_CLASS_AUDIO ||
		     interface->desc.bInterfaceSubClass != USB_SUBCLASS_MIDISTREAMING )
			continue;
		alts = interface->desc.bAlternateSetting;
	}
	if ( alts == -1 ) {
		return -EINVAL;
	}

	printk(KERN_INFO "usb-midi: Found MIDISTREAMING on dev %04x:%04x, iface %d\n",
	       le16_to_cpu(d->descriptor.idVendor), 
	       le16_to_cpu(d->descriptor.idProduct), ifnum);


	/* From USB Spec v2.0, Section 9.5.
	   If the class or vendor specific descriptors use the same format
	   as standard descriptors (e.g., start with a length byte and
	   followed by a type byte), they must be returned interleaved with
	   standard descriptors in the configuration information returned by
	   a GetDescriptor(Configuration) request. In this case, the class
	   or vendor-specific descriptors must follow a related standard
	   descriptor they modify or extend.
	*/

	i = d->actconfig - d->config;
	buffer = d->rawdescriptors[i];
	bufSize = le16_to_cpu(d->actconfig->desc.wTotalLength);

	u = parse_descriptor( d, buffer, bufSize, ifnum, alts, 0);
	if ( u == NULL ) {
		return -EINVAL;
	}

	ret = alloc_usb_midi_device( d, s, u );

	kfree(u);

	return ret;
}


/** When user has requested a specific device, match it exactly.
 *
 * Uses uvendor, uproduct, uinterface, ualt, umin, umout and ucable.
 * Called by usb_midi_probe();
 *
 **/
static int detect_by_hand(struct usb_device *d, unsigned int ifnum, struct usb_midi_state *s)
{
	struct usb_midi_device u;

	if ( le16_to_cpu(d->descriptor.idVendor) != uvendor ||
	     le16_to_cpu(d->descriptor.idProduct) != uproduct ||
	     ifnum != uinterface ) {
		return -EINVAL;
	}

	if ( ualt < 0 )
		ualt = -1;

	if ( umin   < 0 || umin   > 15 )
		umin   = 0x01 | USB_DIR_IN;
	if ( umout  < 0 || umout  > 15 )
		umout  = 0x01;
	if ( ucable < 0 || ucable > 15 )
		ucable = 0;

	u.deviceName = NULL; /* A flag for alloc_usb_midi_device to get device
				name from device. */
	u.idVendor   = uvendor;
	u.idProduct  = uproduct;
	u.interface  = uinterface;
	u.altSetting = ualt;

	u.in[0].endpoint    = umin;
	u.in[0].cableId     = (1<<ucable);

	u.out[0].endpoint   = umout;
	u.out[0].cableId    = (1<<ucable);

	return alloc_usb_midi_device( d, s, &u );
}



/* ------------------------------------------------------------------------- */

static int usb_midi_probe(struct usb_interface *intf, 
			  const struct usb_device_id *id)
{
	struct usb_midi_state *s;
	struct usb_device *dev = interface_to_usbdev(intf);
	int ifnum = intf->cur_altsetting->desc.bInterfaceNumber;

	s = (struct usb_midi_state *)kmalloc(sizeof(struct usb_midi_state), GFP_KERNEL);
	if ( !s )
		return -ENOMEM;

	memset( s, 0, sizeof(struct usb_midi_state) );
	INIT_LIST_HEAD(&s->midiDevList);
	INIT_LIST_HEAD(&s->inEndpointList);
	INIT_LIST_HEAD(&s->outEndpointList);
	s->usbdev = dev;
	s->count  = 0;
	spin_lock_init(&s->lock);

	if (
		detect_by_hand( dev, ifnum, s ) &&
		detect_midi_subclass( dev, intf, ifnum, s ) &&
		detect_vendor_specific_device( dev, ifnum, s ) &&
		detect_yamaha_device( dev, intf, ifnum, s) ) {
		kfree(s);
		return -EIO;
	}

	down(&open_sem);
	list_add_tail(&s->mididev, &mididevs);
	up(&open_sem);

	usb_set_intfdata (intf, s);
	return 0;
}


static void usb_midi_disconnect(struct usb_interface *intf)
{
	struct usb_midi_state *s = usb_get_intfdata (intf);
	struct usb_mididev    *m;

	if ( !s )
		return;

	if ( s == (struct usb_midi_state *)-1 ) {
		return;
	}
	if ( !s->usbdev ) {
		return;
	}
	down(&open_sem);
	list_del(&s->mididev);
	INIT_LIST_HEAD(&s->mididev);
	s->usbdev = NULL;
	usb_set_intfdata (intf, NULL);

	list_for_each_entry(m, &s->midiDevList, list) {
		wake_up(&(m->min.ep->wait));
		wake_up(&(m->mout.ep->wait));
		if ( m->dev_midi >= 0 ) {
			unregister_sound_midi(m->dev_midi);
		}
		m->dev_midi = -1;
	}
	release_midi_device(s);
	wake_up(&open_wait);
}

/* we want to look at all devices by hand */
static struct usb_device_id id_table[] = {
	{.driver_info = 42},
	{}
};

static struct usb_driver usb_midi_driver = {
	.owner =	THIS_MODULE,
	.name =		"midi",
	.probe =	usb_midi_probe,
	.disconnect =	usb_midi_disconnect,
	.id_table =	id_table,
};

/* ------------------------------------------------------------------------- */

static int __init usb_midi_init(void)
{
	return usb_register(&usb_midi_driver);
}

static void __exit usb_midi_exit(void)
{
	usb_deregister(&usb_midi_driver);
}

module_init(usb_midi_init) ;
module_exit(usb_midi_exit) ;

#ifdef HAVE_ALSA_SUPPORT
#define SNDRV_MAIN_OBJECT_FILE
#include "../../include/driver.h"
#include "../../include/control.h"
#include "../../include/info.h"
#include "../../include/cs46xx.h"

/* ------------------------------------------------------------------------- */

static int snd_usbmidi_input_close(snd_rawmidi_substream_t * substream)
{
	return 0;
}

static int snd_usbmidi_input_open(snd_rawmidi_substream_t * substream )
{
	return 0;
}

static void snd_usbmidi_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	return 0;
}


/* ------------------------------------------------------------------------- */

static int snd_usbmidi_output_close(snd_rawmidi_substream_t * substream)
{
	return 0;
}

static int snd_usbmidi_output_open(snd_rawmidi_substream_t * substream)
{
	return 0;
}

static void snd_usb_midi_output_trigger(snd_rawmidi_substream_t * substream,
					int up)
{
	return 0;
}

/* ------------------------------------------------------------------------- */

static snd_rawmidi_ops_t snd_usbmidi_output =
{
        .open =         snd_usbmidi_output_open,
        .close =        snd_usbmidi_output_close,
        .trigger =      snd_usbmidi_output_trigger,
};
static snd_rawmidi_ops_t snd_usbmidi_input =
{
        .open =         snd_usbmidi_input_open,
        .close =        snd_usbmidi_input_close,
        .trigger =      snd_usbmidi_input_trigger,
};

int snd_usbmidi_midi(cs46xx_t *chip, int device, snd_rawmidi_t **rrawmidi)
{
	snd_rawmidi_t *rmidi;
	int err;

	if (rrawmidi)
		*rrawmidi = NULL;
	if ((err = snd_rawmidi_new(chip->card, "USB-MIDI", device, 1, 1, &rmidi)) < 0)
		return err;
	strcpy(rmidi->name, "USB-MIDI");

	snd_rawmidi_set_ops( rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_usbmidi_output );
	snd_rawmidi_set_ops( rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_usbmidi_input );

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT | SNDRV_RAWMIDI_INFO_INPUT | SNDRV_RAWMIDI_INFO_DUPLEX;

	rmidi->private_data = chip;
	chip->rmidi = rmidi;
	if (rrawmidi)
		*rrawmidi = NULL;

	return 0;
}

int snd_usbmidi_create( snd_card_t * card,
			struct pci_dev * pci,
			usbmidi_t ** rchip )
{
	usbmidi_t *chip;
	int err, idx;
	snd_region_t *region;
	static snd_device_opt_t ops = {
		.dev_free = snd_usbmidi_dev_free,
	};

	*rchip = NULL;
	chip = snd_magic_kcalloc( usbmidi_t, 0, GFP_KERNEL );
	if ( chip == NULL )
		return -ENOMEM;
}

EXPORT_SYMBOL(snd_usbmidi_create);
EXPORT_SYMBOL(snd_usbmidi_midi);
#endif /* HAVE_ALSA_SUPPORT */

