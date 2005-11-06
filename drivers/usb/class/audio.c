/*****************************************************************************/

/*
 *	audio.c  --  USB Audio Class driver
 *
 *	Copyright (C) 1999, 2000, 2001, 2003, 2004
 *	    Alan Cox (alan@lxorguk.ukuu.org.uk)
 *	    Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * Debugging:
 * 	Use the 'lsusb' utility to dump the descriptors.
 *
 * 1999-09-07:  Alan Cox
 *		Parsing Audio descriptor patch
 * 1999-09-08:  Thomas Sailer
 *		Added OSS compatible data io functions; both parts of the
 *		driver remain to be glued together
 * 1999-09-10:  Thomas Sailer
 *		Beautified the driver. Added sample format conversions.
 *		Still not properly glued with the parsing code.
 *		The parsing code seems to have its problems btw,
 *		Since it parses all available configs but doesn't
 *		store which iface/altsetting belongs to which config.
 * 1999-09-20:  Thomas Sailer
 *		Threw out Alan's parsing code and implemented my own one.
 *		You cannot reasonnably linearly parse audio descriptors,
 *		especially the AudioClass descriptors have to be considered
 *		pointer lists. Mixer parsing untested, due to lack of device.
 *		First stab at synch pipe implementation, the Dallas USB DAC
 *		wants to use an Asynch out pipe. usb_audio_state now basically
 *		only contains lists of mixer and wave devices. We can therefore
 *		now have multiple mixer/wave devices per USB device.
 * 1999-10-28:  Thomas Sailer
 *		Converted to URB API. Fixed a taskstate/wakeup semantics mistake
 *		that made the driver consume all available CPU cycles.
 *		Now runs stable on UHCI-Acher/Fliegl/Sailer.
 * 1999-10-31:  Thomas Sailer
 *		Audio can now be unloaded if it is not in use by any mixer
 *		or dsp client (formerly you had to disconnect the audio devices
 *		from the USB port)
 *		Finally, about three months after ordering, my "Maxxtro SPK222"
 *		speakers arrived, isn't disdata a great mail order company 8-)
 *		Parse class specific endpoint descriptor of the audiostreaming
 *		interfaces and take the endpoint attributes from there.
 *		Unbelievably, the Philips USB DAC has a sampling rate range
 *		of over a decade, yet does not support the sampling rate control!
 *		No wonder it sounds so bad, has very audible sampling rate
 *		conversion distortion. Don't try to listen to it using
 *		decent headphones!
 *		"Let's make things better" -> but please Philips start with your
 *		own stuff!!!!
 * 1999-11-02:  Thomas Sailer
 *		It takes the Philips boxes several seconds to acquire synchronisation
 *		that means they won't play short sounds. Should probably maintain
 *		the ISO datastream even if there's nothing to play.
 *		Fix counting the total_bytes counter, RealPlayer G2 depends on it.
 * 1999-12-20:  Thomas Sailer
 *		Fix bad bug in conversion to per interface probing.
 *		disconnect was called multiple times for the audio device,
 *		leading to a premature freeing of the audio structures
 * 2000-05-13:  Thomas Sailer
 *		I don't remember who changed the find_format routine,
 *              but the change was completely broken for the Dallas
 *              chip. Anyway taking sampling rate into account in find_format
 *              is bad and should not be done unless there are devices with
 *              completely broken audio descriptors. Unless someone shows
 *              me such a descriptor, I will not allow find_format to
 *              take the sampling rate into account.
 *              Also, the former find_format made:
 *              - mpg123 play mono instead of stereo
 *              - sox completely fail for wav's with sample rates < 44.1kHz
 *                  for the Dallas chip.
 *              Also fix a rather long standing problem with applications that
 *              use "small" writes producing no sound at all.
 * 2000-05-15:  Thomas Sailer
 *		My fears came true, the Philips camera indeed has pretty stupid
 *              audio descriptors.
 * 2000-05-17:  Thomas Sailer
 *		Nemsoft spotted my stupid last minute change, thanks
 * 2000-05-19:  Thomas Sailer
 *		Fixed FEATURE_UNIT thinkos found thanks to the KC Technology
 *              Xtend device. Basically the driver treated FEATURE_UNIT's sourced
 *              by mono terminals as stereo.
 * 2000-05-20:  Thomas Sailer
 *		SELECTOR support (and thus selecting record channels from the mixer).
 *              Somewhat peculiar due to OSS interface limitations. Only works
 *              for channels where a "slider" is already in front of it (i.e.
 *              a MIXER unit or a FEATURE unit with volume capability).
 * 2000-11-26:  Thomas Sailer
 *              Workaround for Dallas DS4201. The DS4201 uses PCM8 as format tag for
 *              its 8 bit modes, but expects signed data (and should therefore have used PCM).
 * 2001-03-10:  Thomas Sailer
 *              provide abs function, prevent picking up a bogus kernel macro
 *              for abs. Bug report by Andrew Morton <andrewm@uow.edu.au>
 * 2001-06-16:  Bryce Nesbitt <bryce@obviously.com>
 *              Fix SNDCTL_DSP_STEREO API violation
 * 2003-04-08:	Oliver Neukum (oliver@neukum.name):
 *		Setting a configuration is done by usbcore and must not be overridden
 * 2004-02-27:  Workaround for broken synch descriptors
 * 2004-03-07:	Alan Stern <stern@rowland.harvard.edu>
 *		Add usb_ifnum_to_if() and usb_altnum_to_altsetting() support.
 *		Use the in-memory descriptors instead of reading them from the device.
 * 
 */

/*
 * Strategy:
 *
 * Alan Cox and Thomas Sailer are starting to dig at opposite ends and
 * are hoping to meet in the middle, just like tunnel diggers :)
 * Alan tackles the descriptor parsing, Thomas the actual data IO and the
 * OSS compatible interface.
 *
 * Data IO implementation issues
 *
 * A mmap'able ring buffer per direction is implemented, because
 * almost every OSS app expects it. It is however impractical to
 * transmit/receive USB data directly into and out of the ring buffer,
 * due to alignment and synchronisation issues. Instead, the ring buffer
 * feeds a constant time delay line that handles the USB issues.
 *
 * Now we first try to find an alternate setting that exactly matches
 * the sample format requested by the user. If we find one, we do not
 * need to perform any sample rate conversions. If there is no matching
 * altsetting, we choose the closest one and perform sample format
 * conversions. We never do sample rate conversion; these are too
 * expensive to be performed in the kernel.
 *
 * Current status: no known HCD-specific issues.
 *
 * Generally: Due to the brokenness of the Audio Class spec
 * it seems generally impossible to write a generic Audio Class driver,
 * so a reasonable driver should implement the features that are actually
 * used.
 *
 * Parsing implementation issues
 *
 * One cannot reasonably parse the AudioClass descriptors linearly.
 * Therefore the current implementation features routines to look
 * for a specific descriptor in the descriptor list.
 *
 * How does the parsing work? First, all interfaces are searched
 * for an AudioControl class interface. If found, the config descriptor
 * that belongs to the current configuration is searched and
 * the HEADER descriptor is found. It contains a list of
 * all AudioStreaming and MIDIStreaming devices. This list is then walked,
 * and all AudioStreaming interfaces are classified into input and output
 * interfaces (according to the endpoint0 direction in altsetting1) (MIDIStreaming
 * is currently not supported). The input & output list is then used
 * to group inputs and outputs together and issued pairwise to the
 * AudioStreaming class parser. Finally, all OUTPUT_TERMINAL descriptors
 * are walked and issued to the mixer construction routine.
 *
 * The AudioStreaming parser simply enumerates all altsettings belonging
 * to the specified interface. It looks for AS_GENERAL and FORMAT_TYPE
 * class specific descriptors to extract the sample format/sample rate
 * data. Only sample format types PCM and PCM8 are supported right now, and
 * only FORMAT_TYPE_I is handled. The isochronous data endpoint needs to
 * be the first endpoint of the interface, and the optional synchronisation
 * isochronous endpoint the second one.
 *
 * Mixer construction works as follows: The various TERMINAL and UNIT
 * descriptors span a tree from the root (OUTPUT_TERMINAL) through the
 * intermediate nodes (UNITs) to the leaves (INPUT_TERMINAL). We walk
 * that tree in a depth first manner. FEATURE_UNITs may contribute volume,
 * bass and treble sliders to the mixer, MIXER_UNITs volume sliders.
 * The terminal type encoded in the INPUT_TERMINALs feeds a heuristic
 * to determine "meaningful" OSS slider numbers, however we will see
 * how well this works in practice. Other features are not used at the
 * moment, they seem less often used. Also, it seems difficult at least
 * to construct recording source switches from SELECTOR_UNITs, but
 * since there are not many USB ADC's available, we leave that for later.
 */

/*****************************************************************************/

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/sound.h>
#include <linux/soundcard.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/bitops.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/usb.h>

#include "audio.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.0.0"
#define DRIVER_AUTHOR "Alan Cox <alan@lxorguk.ukuu.org.uk>, Thomas Sailer (sailer@ife.ee.ethz.ch)"
#define DRIVER_DESC "USB Audio Class driver"

#define AUDIO_DEBUG 1

#define SND_DEV_DSP16   5

#define dprintk(x)

/* --------------------------------------------------------------------- */

/*
 * Linked list of all audio devices...
 */
static struct list_head audiodevs = LIST_HEAD_INIT(audiodevs);
static DECLARE_MUTEX(open_sem);

/*
 * wait queue for processes wanting to open an USB audio device
 */
static DECLARE_WAIT_QUEUE_HEAD(open_wait);


#define MAXFORMATS        MAX_ALT
#define DMABUFSHIFT       17  /* 128k worth of DMA buffer */
#define NRSGBUF           (1U<<(DMABUFSHIFT-PAGE_SHIFT))

/*
 * This influences:
 * - Latency
 * - Interrupt rate
 * - Synchronisation behaviour
 * Don't touch this if you don't understand all of the above.
 */
#define DESCFRAMES  5
#define SYNCFRAMES  DESCFRAMES

#define MIXFLG_STEREOIN   1
#define MIXFLG_STEREOOUT  2

struct mixerchannel {
	__u16 value;
	__u16 osschannel;  /* number of the OSS channel */
	__s16 minval, maxval;
	__u16 slctunitid;
	__u8 unitid;
	__u8 selector;
	__u8 chnum;
	__u8 flags;
};

struct audioformat {
	unsigned int format;
	unsigned int sratelo;
	unsigned int sratehi;
	unsigned char altsetting;
	unsigned char attributes;
};

struct dmabuf {
	/* buffer data format */
	unsigned int format;
	unsigned int srate;
	/* physical buffer */
	unsigned char *sgbuf[NRSGBUF];
	unsigned bufsize;
	unsigned numfrag;
	unsigned fragshift;
	unsigned wrptr, rdptr;
	unsigned total_bytes;
	int count;
	unsigned error; /* over/underrun */
	wait_queue_head_t wait;
	/* redundant, but makes calculations easier */
	unsigned fragsize;
	unsigned dmasize;
	/* OSS stuff */
	unsigned mapped:1;
	unsigned ready:1;
	unsigned ossfragshift;
	int ossmaxfrags;
	unsigned subdivision;
};

struct usb_audio_state;

#define FLG_URB0RUNNING   1
#define FLG_URB1RUNNING   2
#define FLG_SYNC0RUNNING  4
#define FLG_SYNC1RUNNING  8
#define FLG_RUNNING      16
#define FLG_CONNECTED    32

struct my_data_urb {
	struct urb *urb;
};

struct my_sync_urb {
	struct urb *urb;
};


struct usb_audiodev {
	struct list_head list;
	struct usb_audio_state *state;
	
	/* soundcore stuff */
	int dev_audio;

	/* wave stuff */
	mode_t open_mode;
	spinlock_t lock;         /* DMA buffer access spinlock */

	struct usbin {
		int interface;           /* Interface number, -1 means not used */
		unsigned int format;     /* USB data format */
		unsigned int datapipe;   /* the data input pipe */
		unsigned int syncpipe;   /* the synchronisation pipe - 0 for anything but adaptive IN mode */
		unsigned int syncinterval;  /* P for adaptive IN mode, 0 otherwise */
		unsigned int freqn;      /* nominal sampling rate in USB format, i.e. fs/1000 in Q10.14 */
		unsigned int freqmax;    /* maximum sampling rate, used for buffer management */
		unsigned int phase;      /* phase accumulator */
		unsigned int flags;      /* see FLG_ defines */
		
		struct my_data_urb durb[2];  /* ISO descriptors for the data endpoint */
		struct my_sync_urb surb[2];  /* ISO sync pipe descriptor if needed */
		
		struct dmabuf dma;
	} usbin;

	struct usbout {
		int interface;           /* Interface number, -1 means not used */
		unsigned int format;     /* USB data format */
		unsigned int datapipe;   /* the data input pipe */
		unsigned int syncpipe;   /* the synchronisation pipe - 0 for anything but asynchronous OUT mode */
		unsigned int syncinterval;  /* P for asynchronous OUT mode, 0 otherwise */
		unsigned int freqn;      /* nominal sampling rate in USB format, i.e. fs/1000 in Q10.14 */
		unsigned int freqm;      /* momentary sampling rate in USB format, i.e. fs/1000 in Q10.14 */
		unsigned int freqmax;    /* maximum sampling rate, used for buffer management */
		unsigned int phase;      /* phase accumulator */
		unsigned int flags;      /* see FLG_ defines */

		struct my_data_urb durb[2];  /* ISO descriptors for the data endpoint */
		struct my_sync_urb surb[2];  /* ISO sync pipe descriptor if needed */
		
		struct dmabuf dma;
	} usbout;


	unsigned int numfmtin, numfmtout;
	struct audioformat fmtin[MAXFORMATS];
	struct audioformat fmtout[MAXFORMATS];
};  

struct usb_mixerdev {
	struct list_head list;
	struct usb_audio_state *state;

	/* soundcore stuff */
	int dev_mixer;

	unsigned char iface;  /* interface number of the AudioControl interface */

	/* USB format descriptions */
	unsigned int numch, modcnt;

	/* mixch is last and gets allocated dynamically */
	struct mixerchannel ch[0];
};

struct usb_audio_state {
	struct list_head audiodev;

	/* USB device */
	struct usb_device *usbdev;

	struct list_head audiolist;
	struct list_head mixerlist;

	unsigned count;  /* usage counter; NOTE: the usb stack is also considered a user */
};

/* private audio format extensions */
#define AFMT_STEREO        0x80000000
#define AFMT_ISSTEREO(x)   ((x) & AFMT_STEREO)
#define AFMT_IS16BIT(x)    ((x) & (AFMT_S16_LE|AFMT_S16_BE|AFMT_U16_LE|AFMT_U16_BE))
#define AFMT_ISUNSIGNED(x) ((x) & (AFMT_U8|AFMT_U16_LE|AFMT_U16_BE))
#define AFMT_BYTESSHIFT(x) ((AFMT_ISSTEREO(x) ? 1 : 0) + (AFMT_IS16BIT(x) ? 1 : 0))
#define AFMT_BYTES(x)      (1<<AFMT_BYTESSHFIT(x))

/* --------------------------------------------------------------------- */

static inline unsigned ld2(unsigned int x)
{
	unsigned r = 0;
	
	if (x >= 0x10000) {
		x >>= 16;
		r += 16;
	}
	if (x >= 0x100) {
		x >>= 8;
		r += 8;
	}
	if (x >= 0x10) {
		x >>= 4;
		r += 4;
	}
	if (x >= 4) {
		x >>= 2;
		r += 2;
	}
	if (x >= 2)
		r++;
	return r;
}

/* --------------------------------------------------------------------- */

/*
 * OSS compatible ring buffer management. The ring buffer may be mmap'ed into
 * an application address space.
 *
 * I first used the rvmalloc stuff copied from bttv. Alan Cox did not like it, so
 * we now use an array of pointers to a single page each. This saves us the
 * kernel page table manipulations, but we have to do a page table alike mechanism
 * (though only one indirection) in software.
 */

static void dmabuf_release(struct dmabuf *db)
{
	unsigned int nr;
	void *p;

	for(nr = 0; nr < NRSGBUF; nr++) {
		if (!(p = db->sgbuf[nr]))
			continue;
		ClearPageReserved(virt_to_page(p));
		free_page((unsigned long)p);
		db->sgbuf[nr] = NULL;
	}
	db->mapped = db->ready = 0;
}

static int dmabuf_init(struct dmabuf *db)
{
	unsigned int nr, bytepersec, bufs;
	void *p;

	/* initialize some fields */
	db->rdptr = db->wrptr = db->total_bytes = db->count = db->error = 0;
	/* calculate required buffer size */
	bytepersec = db->srate << AFMT_BYTESSHIFT(db->format);
	bufs = 1U << DMABUFSHIFT;
	if (db->ossfragshift) {
		if ((1000 << db->ossfragshift) < bytepersec)
			db->fragshift = ld2(bytepersec/1000);
		else
			db->fragshift = db->ossfragshift;
	} else {
		db->fragshift = ld2(bytepersec/100/(db->subdivision ? db->subdivision : 1));
		if (db->fragshift < 3)
			db->fragshift = 3;
	}
	db->numfrag = bufs >> db->fragshift;
	while (db->numfrag < 4 && db->fragshift > 3) {
		db->fragshift--;
		db->numfrag = bufs >> db->fragshift;
	}
	db->fragsize = 1 << db->fragshift;
	if (db->ossmaxfrags >= 4 && db->ossmaxfrags < db->numfrag)
		db->numfrag = db->ossmaxfrags;
	db->dmasize = db->numfrag << db->fragshift;
	for(nr = 0; nr < NRSGBUF; nr++) {
		if (!db->sgbuf[nr]) {
			p = (void *)get_zeroed_page(GFP_KERNEL);
			if (!p)
				return -ENOMEM;
			db->sgbuf[nr] = p;
			SetPageReserved(virt_to_page(p));
		}
		memset(db->sgbuf[nr], AFMT_ISUNSIGNED(db->format) ? 0x80 : 0, PAGE_SIZE);
		if ((nr << PAGE_SHIFT) >= db->dmasize)
			break;
	}
	db->bufsize = nr << PAGE_SHIFT;
	db->ready = 1;
	dprintk((KERN_DEBUG "usbaudio: dmabuf_init bytepersec %d bufs %d ossfragshift %d ossmaxfrags %d "
	         "fragshift %d fragsize %d numfrag %d dmasize %d bufsize %d fmt 0x%x srate %d\n",
	         bytepersec, bufs, db->ossfragshift, db->ossmaxfrags, db->fragshift, db->fragsize,
	         db->numfrag, db->dmasize, db->bufsize, db->format, db->srate));
	return 0;
}

static int dmabuf_mmap(struct vm_area_struct *vma, struct dmabuf *db, unsigned long start, unsigned long size, pgprot_t prot)
{
	unsigned int nr;

	if (!db->ready || db->mapped || (start | size) & (PAGE_SIZE-1) || size > db->bufsize)
		return -EINVAL;
	size >>= PAGE_SHIFT;
	for(nr = 0; nr < size; nr++)
		if (!db->sgbuf[nr])
			return -EINVAL;
	db->mapped = 1;
	for(nr = 0; nr < size; nr++) {
		unsigned long pfn;

		pfn = virt_to_phys(db->sgbuf[nr]) >> PAGE_SHIFT;
		if (remap_pfn_range(vma, start, pfn, PAGE_SIZE, prot))
			return -EAGAIN;
		start += PAGE_SIZE;
	}
	return 0;
}

static void dmabuf_copyin(struct dmabuf *db, const void *buffer, unsigned int size)
{
	unsigned int pgrem, rem;

	db->total_bytes += size;
	for (;;) {
		if (size <= 0)
			return;
		pgrem = ((~db->wrptr) & (PAGE_SIZE-1)) + 1;
		if (pgrem > size)
			pgrem = size;
		rem = db->dmasize - db->wrptr;
		if (pgrem > rem)
			pgrem = rem;
		memcpy((db->sgbuf[db->wrptr >> PAGE_SHIFT]) + (db->wrptr & (PAGE_SIZE-1)), buffer, pgrem);
		size -= pgrem;
		buffer += pgrem;
		db->wrptr += pgrem;
		if (db->wrptr >= db->dmasize)
			db->wrptr = 0;
	}
}

static void dmabuf_copyout(struct dmabuf *db, void *buffer, unsigned int size)
{
	unsigned int pgrem, rem;

	db->total_bytes += size;
	for (;;) {
		if (size <= 0)
			return;
		pgrem = ((~db->rdptr) & (PAGE_SIZE-1)) + 1;
		if (pgrem > size)
			pgrem = size;
		rem = db->dmasize - db->rdptr;
		if (pgrem > rem)
			pgrem = rem;
		memcpy(buffer, (db->sgbuf[db->rdptr >> PAGE_SHIFT]) + (db->rdptr & (PAGE_SIZE-1)), pgrem);
		size -= pgrem;
		buffer += pgrem;
		db->rdptr += pgrem;
		if (db->rdptr >= db->dmasize)
			db->rdptr = 0;
	}
}

static int dmabuf_copyin_user(struct dmabuf *db, unsigned int ptr, const void __user *buffer, unsigned int size)
{
	unsigned int pgrem, rem;

	if (!db->ready || db->mapped)
		return -EINVAL;
	for (;;) {
		if (size <= 0)
			return 0;
		pgrem = ((~ptr) & (PAGE_SIZE-1)) + 1;
		if (pgrem > size)
			pgrem = size;
		rem = db->dmasize - ptr;
		if (pgrem > rem)
			pgrem = rem;
		if (copy_from_user((db->sgbuf[ptr >> PAGE_SHIFT]) + (ptr & (PAGE_SIZE-1)), buffer, pgrem))
			return -EFAULT;
		size -= pgrem;
		buffer += pgrem;
		ptr += pgrem;
		if (ptr >= db->dmasize)
			ptr = 0;
	}
}

static int dmabuf_copyout_user(struct dmabuf *db, unsigned int ptr, void __user *buffer, unsigned int size)
{
	unsigned int pgrem, rem;

	if (!db->ready || db->mapped)
		return -EINVAL;
	for (;;) {
		if (size <= 0)
			return 0;
		pgrem = ((~ptr) & (PAGE_SIZE-1)) + 1;
		if (pgrem > size)
			pgrem = size;
		rem = db->dmasize - ptr;
		if (pgrem > rem)
			pgrem = rem;
		if (copy_to_user(buffer, (db->sgbuf[ptr >> PAGE_SHIFT]) + (ptr & (PAGE_SIZE-1)), pgrem))
			return -EFAULT;
		size -= pgrem;
		buffer += pgrem;
		ptr += pgrem;
		if (ptr >= db->dmasize)
			ptr = 0;
	}
}

/* --------------------------------------------------------------------- */
/*
 * USB I/O code. We do sample format conversion if necessary
 */

static void usbin_stop(struct usb_audiodev *as)
{
	struct usbin *u = &as->usbin;
	unsigned long flags;
	unsigned int i, notkilled = 1;

	spin_lock_irqsave(&as->lock, flags);
	u->flags &= ~FLG_RUNNING;
	i = u->flags;
	spin_unlock_irqrestore(&as->lock, flags);
	while (i & (FLG_URB0RUNNING|FLG_URB1RUNNING|FLG_SYNC0RUNNING|FLG_SYNC1RUNNING)) {
		if (notkilled)
			schedule_timeout_interruptible(1);
		else
			schedule_timeout_uninterruptible(1);
		spin_lock_irqsave(&as->lock, flags);
		i = u->flags;
		spin_unlock_irqrestore(&as->lock, flags);
		if (notkilled && signal_pending(current)) {
			if (i & FLG_URB0RUNNING)
				usb_kill_urb(u->durb[0].urb);
			if (i & FLG_URB1RUNNING)
				usb_kill_urb(u->durb[1].urb);
			if (i & FLG_SYNC0RUNNING)
				usb_kill_urb(u->surb[0].urb);
			if (i & FLG_SYNC1RUNNING)
				usb_kill_urb(u->surb[1].urb);
			notkilled = 0;
		}
	}
	set_current_state(TASK_RUNNING);
	kfree(u->durb[0].urb->transfer_buffer);
	kfree(u->durb[1].urb->transfer_buffer);
	kfree(u->surb[0].urb->transfer_buffer);
	kfree(u->surb[1].urb->transfer_buffer);
	u->durb[0].urb->transfer_buffer = u->durb[1].urb->transfer_buffer = 
		u->surb[0].urb->transfer_buffer = u->surb[1].urb->transfer_buffer = NULL;
}

static inline void usbin_release(struct usb_audiodev *as)
{
	usbin_stop(as);
}

static void usbin_disc(struct usb_audiodev *as)
{
	struct usbin *u = &as->usbin;

	unsigned long flags;

	spin_lock_irqsave(&as->lock, flags);
	u->flags &= ~(FLG_RUNNING | FLG_CONNECTED);
	spin_unlock_irqrestore(&as->lock, flags);
	usbin_stop(as);
}

static void conversion(const void *ibuf, unsigned int ifmt, void *obuf, unsigned int ofmt, void *tmp, unsigned int scnt)
{
	unsigned int cnt, i;
	__s16 *sp, *sp2, s;
	unsigned char *bp;

	cnt = scnt;
	if (AFMT_ISSTEREO(ifmt))
		cnt <<= 1;
	sp = ((__s16 *)tmp) + cnt;
	switch (ifmt & ~AFMT_STEREO) {
	case AFMT_U8:
		for (bp = ((unsigned char *)ibuf)+cnt, i = 0; i < cnt; i++) {
			bp--;
			sp--;
			*sp = (*bp ^ 0x80) << 8;
		}
		break;
			
	case AFMT_S8:
		for (bp = ((unsigned char *)ibuf)+cnt, i = 0; i < cnt; i++) {
			bp--;
			sp--;
			*sp = *bp << 8;
		}
		break;
		
	case AFMT_U16_LE:
		for (bp = ((unsigned char *)ibuf)+2*cnt, i = 0; i < cnt; i++) {
			bp -= 2;
			sp--;
			*sp = (bp[0] | (bp[1] << 8)) ^ 0x8000;
		}
		break;

	case AFMT_U16_BE:
		for (bp = ((unsigned char *)ibuf)+2*cnt, i = 0; i < cnt; i++) {
			bp -= 2;
			sp--;
			*sp = (bp[1] | (bp[0] << 8)) ^ 0x8000;
		}
		break;

	case AFMT_S16_LE:
		for (bp = ((unsigned char *)ibuf)+2*cnt, i = 0; i < cnt; i++) {
			bp -= 2;
			sp--;
			*sp = bp[0] | (bp[1] << 8);
		}
		break;

	case AFMT_S16_BE:
		for (bp = ((unsigned char *)ibuf)+2*cnt, i = 0; i < cnt; i++) {
			bp -= 2;
			sp--;
			*sp = bp[1] | (bp[0] << 8);
		}
		break;
	}
	if (!AFMT_ISSTEREO(ifmt) && AFMT_ISSTEREO(ofmt)) {
		/* expand from mono to stereo */
		for (sp = ((__s16 *)tmp)+scnt, sp2 = ((__s16 *)tmp)+2*scnt, i = 0; i < scnt; i++) {
			sp--;
			sp2 -= 2;
			sp2[0] = sp2[1] = sp[0];
		}
	}
	if (AFMT_ISSTEREO(ifmt) && !AFMT_ISSTEREO(ofmt)) {
		/* contract from stereo to mono */
		for (sp = sp2 = ((__s16 *)tmp), i = 0; i < scnt; i++, sp++, sp2 += 2)
			sp[0] = (sp2[0] + sp2[1]) >> 1;
	}
	cnt = scnt;
	if (AFMT_ISSTEREO(ofmt))
		cnt <<= 1;
	sp = ((__s16 *)tmp);
	bp = ((unsigned char *)obuf);
	switch (ofmt & ~AFMT_STEREO) {
	case AFMT_U8:
		for (i = 0; i < cnt; i++, sp++, bp++)
			*bp = (*sp >> 8) ^ 0x80;
		break;

	case AFMT_S8:
		for (i = 0; i < cnt; i++, sp++, bp++)
			*bp = *sp >> 8;
		break;

	case AFMT_U16_LE:
		for (i = 0; i < cnt; i++, sp++, bp += 2) {
			s = *sp;
			bp[0] = s;
			bp[1] = (s >> 8) ^ 0x80;
		}
		break;

	case AFMT_U16_BE:
		for (i = 0; i < cnt; i++, sp++, bp += 2) {
			s = *sp;
			bp[1] = s;
			bp[0] = (s >> 8) ^ 0x80;
		}
		break;

	case AFMT_S16_LE:
		for (i = 0; i < cnt; i++, sp++, bp += 2) {
			s = *sp;
			bp[0] = s;
			bp[1] = s >> 8;
		}
		break;

	case AFMT_S16_BE:
		for (i = 0; i < cnt; i++, sp++, bp += 2) {
			s = *sp;
			bp[1] = s;
			bp[0] = s >> 8;
		}
		break;
	}
	
}

static void usbin_convert(struct usbin *u, unsigned char *buffer, unsigned int samples)
{
	union {
		__s16 s[64];
		unsigned char b[0];
	} tmp;
	unsigned int scnt, maxs, ufmtsh, dfmtsh;

	ufmtsh = AFMT_BYTESSHIFT(u->format);
	dfmtsh = AFMT_BYTESSHIFT(u->dma.format);
	maxs = (AFMT_ISSTEREO(u->dma.format | u->format)) ? 32 : 64;
	while (samples > 0) {
		scnt = samples;
		if (scnt > maxs)
			scnt = maxs;
		conversion(buffer, u->format, tmp.b, u->dma.format, tmp.b, scnt);
		dmabuf_copyin(&u->dma, tmp.b, scnt << dfmtsh);
		buffer += scnt << ufmtsh;
		samples -= scnt;
	}
}		

static int usbin_prepare_desc(struct usbin *u, struct urb *urb)
{
	unsigned int i, maxsize, offs;

	maxsize = (u->freqmax + 0x3fff) >> (14 - AFMT_BYTESSHIFT(u->format));
	//printk(KERN_DEBUG "usbin_prepare_desc: maxsize %d freq 0x%x format 0x%x\n", maxsize, u->freqn, u->format);
	for (i = offs = 0; i < DESCFRAMES; i++, offs += maxsize) {
		urb->iso_frame_desc[i].length = maxsize;
		urb->iso_frame_desc[i].offset = offs;
	}
	urb->interval = 1;
	return 0;
}

/*
 * return value: 0 if descriptor should be restarted, -1 otherwise
 * convert sample format on the fly if necessary
 */
static int usbin_retire_desc(struct usbin *u, struct urb *urb)
{
	unsigned int i, ufmtsh, dfmtsh, err = 0, cnt, scnt, dmafree;
	unsigned char *cp;

	ufmtsh = AFMT_BYTESSHIFT(u->format);
	dfmtsh = AFMT_BYTESSHIFT(u->dma.format);
	for (i = 0; i < DESCFRAMES; i++) {
		cp = ((unsigned char *)urb->transfer_buffer) + urb->iso_frame_desc[i].offset;
		if (urb->iso_frame_desc[i].status) {
			dprintk((KERN_DEBUG "usbin_retire_desc: frame %u status %d\n", i, urb->iso_frame_desc[i].status));
			continue;
		}
		scnt = urb->iso_frame_desc[i].actual_length >> ufmtsh;
		if (!scnt)
			continue;
		cnt = scnt << dfmtsh;
		if (!u->dma.mapped) {
			dmafree = u->dma.dmasize - u->dma.count;
			if (cnt > dmafree) {
				scnt = dmafree >> dfmtsh;
				cnt = scnt << dfmtsh;
				err++;
			}
		}
		u->dma.count += cnt;
		if (u->format == u->dma.format) {
			/* we do not need format conversion */
			dprintk((KERN_DEBUG "usbaudio: no sample format conversion\n"));
			dmabuf_copyin(&u->dma, cp, cnt);
		} else {
			/* we need sampling format conversion */
			dprintk((KERN_DEBUG "usbaudio: sample format conversion %x != %x\n", u->format, u->dma.format));
			usbin_convert(u, cp, scnt);
		}
	}
	if (err)
		u->dma.error++;
	if (u->dma.count >= (signed)u->dma.fragsize)
		wake_up(&u->dma.wait);
	return err ? -1 : 0;
}

static void usbin_completed(struct urb *urb, struct pt_regs *regs)
{
	struct usb_audiodev *as = (struct usb_audiodev *)urb->context;
	struct usbin *u = &as->usbin;
	unsigned long flags;
	unsigned int mask;
	int suret = 0;

#if 0
	printk(KERN_DEBUG "usbin_completed: status %d errcnt %d flags 0x%x\n", urb->status, urb->error_count, u->flags);
#endif
	if (urb == u->durb[0].urb)
		mask = FLG_URB0RUNNING;
	else if (urb == u->durb[1].urb)
		mask = FLG_URB1RUNNING;
	else {
		mask = 0;
		printk(KERN_ERR "usbin_completed: panic: unknown URB\n");
	}
	urb->dev = as->state->usbdev;
	spin_lock_irqsave(&as->lock, flags);
	if (!usbin_retire_desc(u, urb) &&
	    u->flags & FLG_RUNNING &&
	    !usbin_prepare_desc(u, urb) && 
	    (suret = usb_submit_urb(urb, GFP_ATOMIC)) == 0) {
		u->flags |= mask;
	} else {
		u->flags &= ~(mask | FLG_RUNNING);
		wake_up(&u->dma.wait);
		printk(KERN_DEBUG "usbin_completed: descriptor not restarted (usb_submit_urb: %d)\n", suret);
	}
	spin_unlock_irqrestore(&as->lock, flags);
}

/*
 * we output sync data
 */
static int usbin_sync_prepare_desc(struct usbin *u, struct urb *urb)
{
	unsigned char *cp = urb->transfer_buffer;
	unsigned int i, offs;
	
	for (i = offs = 0; i < SYNCFRAMES; i++, offs += 3, cp += 3) {
		urb->iso_frame_desc[i].length = 3;
		urb->iso_frame_desc[i].offset = offs;
		cp[0] = u->freqn;
		cp[1] = u->freqn >> 8;
		cp[2] = u->freqn >> 16;
	}
	urb->interval = 1;
	return 0;
}

/*
 * return value: 0 if descriptor should be restarted, -1 otherwise
 */
static int usbin_sync_retire_desc(struct usbin *u, struct urb *urb)
{
	unsigned int i;
	
	for (i = 0; i < SYNCFRAMES; i++)
		if (urb->iso_frame_desc[0].status)
			dprintk((KERN_DEBUG "usbin_sync_retire_desc: frame %u status %d\n", i, urb->iso_frame_desc[i].status));
	return 0;
}

static void usbin_sync_completed(struct urb *urb, struct pt_regs *regs)
{
	struct usb_audiodev *as = (struct usb_audiodev *)urb->context;
	struct usbin *u = &as->usbin;
	unsigned long flags;
	unsigned int mask;
	int suret = 0;

#if 0
	printk(KERN_DEBUG "usbin_sync_completed: status %d errcnt %d flags 0x%x\n", urb->status, urb->error_count, u->flags);
#endif
	if (urb == u->surb[0].urb)
		mask = FLG_SYNC0RUNNING;
	else if (urb == u->surb[1].urb)
		mask = FLG_SYNC1RUNNING;
	else {
		mask = 0;
		printk(KERN_ERR "usbin_sync_completed: panic: unknown URB\n");
	}
	urb->dev = as->state->usbdev;
	spin_lock_irqsave(&as->lock, flags);
	if (!usbin_sync_retire_desc(u, urb) &&
	    u->flags & FLG_RUNNING &&
	    !usbin_sync_prepare_desc(u, urb) && 
	    (suret = usb_submit_urb(urb, GFP_ATOMIC)) == 0) {
		u->flags |= mask;
	} else {
		u->flags &= ~(mask | FLG_RUNNING);
		wake_up(&u->dma.wait);
		dprintk((KERN_DEBUG "usbin_sync_completed: descriptor not restarted (usb_submit_urb: %d)\n", suret));
	}
	spin_unlock_irqrestore(&as->lock, flags);
}

static int usbin_start(struct usb_audiodev *as)
{
	struct usb_device *dev = as->state->usbdev;
	struct usbin *u = &as->usbin;
	struct urb *urb;
	unsigned long flags;
	unsigned int maxsze, bufsz;

#if 0
	printk(KERN_DEBUG "usbin_start: device %d ufmt 0x%08x dfmt 0x%08x srate %d\n",
	       dev->devnum, u->format, u->dma.format, u->dma.srate);
#endif
	/* allocate USB storage if not already done */
	spin_lock_irqsave(&as->lock, flags);
	if (!(u->flags & FLG_CONNECTED)) {
		spin_unlock_irqrestore(&as->lock, flags);
		return -EIO;
	}
	if (!(u->flags & FLG_RUNNING)) {
		spin_unlock_irqrestore(&as->lock, flags);
		u->freqn = ((u->dma.srate << 11) + 62) / 125; /* this will overflow at approx 2MSPS */
		u->freqmax = u->freqn + (u->freqn >> 2);
		u->phase = 0;
		maxsze = (u->freqmax + 0x3fff) >> (14 - AFMT_BYTESSHIFT(u->format));
		bufsz = DESCFRAMES * maxsze;
		kfree(u->durb[0].urb->transfer_buffer);
		u->durb[0].urb->transfer_buffer = kmalloc(bufsz, GFP_KERNEL);
		u->durb[0].urb->transfer_buffer_length = bufsz;
		kfree(u->durb[1].urb->transfer_buffer);
		u->durb[1].urb->transfer_buffer = kmalloc(bufsz, GFP_KERNEL);
		u->durb[1].urb->transfer_buffer_length = bufsz;
		if (u->syncpipe) {
			kfree(u->surb[0].urb->transfer_buffer);
			u->surb[0].urb->transfer_buffer = kmalloc(3*SYNCFRAMES, GFP_KERNEL);
			u->surb[0].urb->transfer_buffer_length = 3*SYNCFRAMES;
			kfree(u->surb[1].urb->transfer_buffer);
			u->surb[1].urb->transfer_buffer = kmalloc(3*SYNCFRAMES, GFP_KERNEL);
			u->surb[1].urb->transfer_buffer_length = 3*SYNCFRAMES;
		}
		if (!u->durb[0].urb->transfer_buffer || !u->durb[1].urb->transfer_buffer || 
		    (u->syncpipe && (!u->surb[0].urb->transfer_buffer || !u->surb[1].urb->transfer_buffer))) {
			printk(KERN_ERR "usbaudio: cannot start playback device %d\n", dev->devnum);
			return 0;
		}
		spin_lock_irqsave(&as->lock, flags);
	}
	if (u->dma.count >= u->dma.dmasize && !u->dma.mapped) {
		spin_unlock_irqrestore(&as->lock, flags);
		return 0;
	}
	u->flags |= FLG_RUNNING;
	if (!(u->flags & FLG_URB0RUNNING)) {
		urb = u->durb[0].urb;
		urb->dev = dev;
		urb->pipe = u->datapipe;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->number_of_packets = DESCFRAMES;
		urb->context = as;
		urb->complete = usbin_completed;
		if (!usbin_prepare_desc(u, urb) && !usb_submit_urb(urb, GFP_KERNEL))
			u->flags |= FLG_URB0RUNNING;
		else
			u->flags &= ~FLG_RUNNING;
	}
	if (u->flags & FLG_RUNNING && !(u->flags & FLG_URB1RUNNING)) {
		urb = u->durb[1].urb;
		urb->dev = dev;
		urb->pipe = u->datapipe;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->number_of_packets = DESCFRAMES;
		urb->context = as;
		urb->complete = usbin_completed;
		if (!usbin_prepare_desc(u, urb) && !usb_submit_urb(urb, GFP_KERNEL))
			u->flags |= FLG_URB1RUNNING;
		else
			u->flags &= ~FLG_RUNNING;
	}
	if (u->syncpipe) {
		if (u->flags & FLG_RUNNING && !(u->flags & FLG_SYNC0RUNNING)) {
			urb = u->surb[0].urb;
			urb->dev = dev;
			urb->pipe = u->syncpipe;
			urb->transfer_flags = URB_ISO_ASAP;
			urb->number_of_packets = SYNCFRAMES;
			urb->context = as;
			urb->complete = usbin_sync_completed;
			/* stride: u->syncinterval */
			if (!usbin_sync_prepare_desc(u, urb) && !usb_submit_urb(urb, GFP_KERNEL))
				u->flags |= FLG_SYNC0RUNNING;
			else
				u->flags &= ~FLG_RUNNING;
		}
		if (u->flags & FLG_RUNNING && !(u->flags & FLG_SYNC1RUNNING)) {
			urb = u->surb[1].urb;
			urb->dev = dev;
			urb->pipe = u->syncpipe;
			urb->transfer_flags = URB_ISO_ASAP;
			urb->number_of_packets = SYNCFRAMES;
			urb->context = as;
			urb->complete = usbin_sync_completed;
			/* stride: u->syncinterval */
			if (!usbin_sync_prepare_desc(u, urb) && !usb_submit_urb(urb, GFP_KERNEL))
				u->flags |= FLG_SYNC1RUNNING;
			else
				u->flags &= ~FLG_RUNNING;
		}
	}
	spin_unlock_irqrestore(&as->lock, flags);
	return 0;
}

static void usbout_stop(struct usb_audiodev *as)
{
	struct usbout *u = &as->usbout;
	unsigned long flags;
	unsigned int i, notkilled = 1;

	spin_lock_irqsave(&as->lock, flags);
	u->flags &= ~FLG_RUNNING;
	i = u->flags;
	spin_unlock_irqrestore(&as->lock, flags);
	while (i & (FLG_URB0RUNNING|FLG_URB1RUNNING|FLG_SYNC0RUNNING|FLG_SYNC1RUNNING)) {
		if (notkilled)
			schedule_timeout_interruptible(1);
		else
			schedule_timeout_uninterruptible(1);
		spin_lock_irqsave(&as->lock, flags);
		i = u->flags;
		spin_unlock_irqrestore(&as->lock, flags);
		if (notkilled && signal_pending(current)) {
			if (i & FLG_URB0RUNNING)
				usb_kill_urb(u->durb[0].urb);
			if (i & FLG_URB1RUNNING)
				usb_kill_urb(u->durb[1].urb);
			if (i & FLG_SYNC0RUNNING)
				usb_kill_urb(u->surb[0].urb);
			if (i & FLG_SYNC1RUNNING)
				usb_kill_urb(u->surb[1].urb);
			notkilled = 0;
		}
	}
	set_current_state(TASK_RUNNING);
	kfree(u->durb[0].urb->transfer_buffer);
	kfree(u->durb[1].urb->transfer_buffer);
	kfree(u->surb[0].urb->transfer_buffer);
	kfree(u->surb[1].urb->transfer_buffer);
	u->durb[0].urb->transfer_buffer = u->durb[1].urb->transfer_buffer = 
		u->surb[0].urb->transfer_buffer = u->surb[1].urb->transfer_buffer = NULL;
}

static inline void usbout_release(struct usb_audiodev *as)
{
	usbout_stop(as);
}

static void usbout_disc(struct usb_audiodev *as)
{
	struct usbout *u = &as->usbout;
	unsigned long flags;

	spin_lock_irqsave(&as->lock, flags);
	u->flags &= ~(FLG_RUNNING | FLG_CONNECTED);
	spin_unlock_irqrestore(&as->lock, flags);
	usbout_stop(as);
}

static void usbout_convert(struct usbout *u, unsigned char *buffer, unsigned int samples)
{
	union {
		__s16 s[64];
		unsigned char b[0];
	} tmp;
	unsigned int scnt, maxs, ufmtsh, dfmtsh;

	ufmtsh = AFMT_BYTESSHIFT(u->format);
	dfmtsh = AFMT_BYTESSHIFT(u->dma.format);
	maxs = (AFMT_ISSTEREO(u->dma.format | u->format)) ? 32 : 64;
	while (samples > 0) {
		scnt = samples;
		if (scnt > maxs)
			scnt = maxs;
		dmabuf_copyout(&u->dma, tmp.b, scnt << dfmtsh);
		conversion(tmp.b, u->dma.format, buffer, u->format, tmp.b, scnt);
		buffer += scnt << ufmtsh;
		samples -= scnt;
	}
}		

static int usbout_prepare_desc(struct usbout *u, struct urb *urb)
{
	unsigned int i, ufmtsh, dfmtsh, err = 0, cnt, scnt, offs;
	unsigned char *cp = urb->transfer_buffer;

	ufmtsh = AFMT_BYTESSHIFT(u->format);
	dfmtsh = AFMT_BYTESSHIFT(u->dma.format);
	for (i = offs = 0; i < DESCFRAMES; i++) {
		urb->iso_frame_desc[i].offset = offs;
		u->phase = (u->phase & 0x3fff) + u->freqm;
		scnt = u->phase >> 14;
		if (!scnt) {
			urb->iso_frame_desc[i].length = 0;
			continue;
		}
		cnt = scnt << dfmtsh;
		if (!u->dma.mapped) {
			if (cnt > u->dma.count) {
				scnt = u->dma.count >> dfmtsh;
				cnt = scnt << dfmtsh;
				err++;
			}
			u->dma.count -= cnt;
		} else
			u->dma.count += cnt;
		if (u->format == u->dma.format) {
			/* we do not need format conversion */
			dmabuf_copyout(&u->dma, cp, cnt);
		} else {
			/* we need sampling format conversion */
			usbout_convert(u, cp, scnt);
		}
		cnt = scnt << ufmtsh;
		urb->iso_frame_desc[i].length = cnt;
		offs += cnt;
		cp += cnt;
	}
	urb->interval = 1;
	if (err)
		u->dma.error++;
	if (u->dma.mapped) {
		if (u->dma.count >= (signed)u->dma.fragsize)
			wake_up(&u->dma.wait);
	} else {
		if ((signed)u->dma.dmasize >= u->dma.count + (signed)u->dma.fragsize)
			wake_up(&u->dma.wait);
	}
	return err ? -1 : 0;
}

/*
 * return value: 0 if descriptor should be restarted, -1 otherwise
 */
static int usbout_retire_desc(struct usbout *u, struct urb *urb)
{
	unsigned int i;

	for (i = 0; i < DESCFRAMES; i++) {
		if (urb->iso_frame_desc[i].status) {
			dprintk((KERN_DEBUG "usbout_retire_desc: frame %u status %d\n", i, urb->iso_frame_desc[i].status));
			continue;
		}
	}
	return 0;
}

static void usbout_completed(struct urb *urb, struct pt_regs *regs)
{
	struct usb_audiodev *as = (struct usb_audiodev *)urb->context;
	struct usbout *u = &as->usbout;
	unsigned long flags;
	unsigned int mask;
	int suret = 0;

#if 0
	printk(KERN_DEBUG "usbout_completed: status %d errcnt %d flags 0x%x\n", urb->status, urb->error_count, u->flags);
#endif
	if (urb == u->durb[0].urb)
		mask = FLG_URB0RUNNING;
	else if (urb == u->durb[1].urb)
		mask = FLG_URB1RUNNING;
	else {
		mask = 0;
		printk(KERN_ERR "usbout_completed: panic: unknown URB\n");
	}
	urb->dev = as->state->usbdev;
	spin_lock_irqsave(&as->lock, flags);
	if (!usbout_retire_desc(u, urb) &&
	    u->flags & FLG_RUNNING &&
	    !usbout_prepare_desc(u, urb) && 
	    (suret = usb_submit_urb(urb, GFP_ATOMIC)) == 0) {
		u->flags |= mask;
	} else {
		u->flags &= ~(mask | FLG_RUNNING);
		wake_up(&u->dma.wait);
		dprintk((KERN_DEBUG "usbout_completed: descriptor not restarted (usb_submit_urb: %d)\n", suret));
	}
	spin_unlock_irqrestore(&as->lock, flags);
}

static int usbout_sync_prepare_desc(struct usbout *u, struct urb *urb)
{
	unsigned int i, offs;

	for (i = offs = 0; i < SYNCFRAMES; i++, offs += 3) {
		urb->iso_frame_desc[i].length = 3;
		urb->iso_frame_desc[i].offset = offs;
	}
	urb->interval = 1;
	return 0;
}

/*
 * return value: 0 if descriptor should be restarted, -1 otherwise
 */
static int usbout_sync_retire_desc(struct usbout *u, struct urb *urb)
{
	unsigned char *cp = urb->transfer_buffer;
	unsigned int f, i;

	for (i = 0; i < SYNCFRAMES; i++, cp += 3) {
		if (urb->iso_frame_desc[i].status) {
			dprintk((KERN_DEBUG "usbout_sync_retire_desc: frame %u status %d\n", i, urb->iso_frame_desc[i].status));
			continue;
		}
		if (urb->iso_frame_desc[i].actual_length < 3) {
			dprintk((KERN_DEBUG "usbout_sync_retire_desc: frame %u length %d\n", i, urb->iso_frame_desc[i].actual_length));
			continue;
		}
		f = cp[0] | (cp[1] << 8) | (cp[2] << 16);
		if (abs(f - u->freqn) > (u->freqn >> 3) || f > u->freqmax) {
			printk(KERN_WARNING "usbout_sync_retire_desc: requested frequency %u (nominal %u) out of range!\n", f, u->freqn);
			continue;
		}
		u->freqm = f;
	}
	return 0;
}

static void usbout_sync_completed(struct urb *urb, struct pt_regs *regs)
{
	struct usb_audiodev *as = (struct usb_audiodev *)urb->context;
	struct usbout *u = &as->usbout;
	unsigned long flags;
	unsigned int mask;
	int suret = 0;

#if 0
	printk(KERN_DEBUG "usbout_sync_completed: status %d errcnt %d flags 0x%x\n", urb->status, urb->error_count, u->flags);
#endif
	if (urb == u->surb[0].urb)
		mask = FLG_SYNC0RUNNING;
	else if (urb == u->surb[1].urb)
		mask = FLG_SYNC1RUNNING;
	else {
		mask = 0;
		printk(KERN_ERR "usbout_sync_completed: panic: unknown URB\n");
	}
	urb->dev = as->state->usbdev;
	spin_lock_irqsave(&as->lock, flags);
	if (!usbout_sync_retire_desc(u, urb) &&
	    u->flags & FLG_RUNNING &&
	    !usbout_sync_prepare_desc(u, urb) && 
	    (suret = usb_submit_urb(urb, GFP_ATOMIC)) == 0) {
		u->flags |= mask;
	} else {
		u->flags &= ~(mask | FLG_RUNNING);
		wake_up(&u->dma.wait);
		dprintk((KERN_DEBUG "usbout_sync_completed: descriptor not restarted (usb_submit_urb: %d)\n", suret));
	}
	spin_unlock_irqrestore(&as->lock, flags);
}

static int usbout_start(struct usb_audiodev *as)
{
	struct usb_device *dev = as->state->usbdev;
	struct usbout *u = &as->usbout;
	struct urb *urb;
	unsigned long flags;
	unsigned int maxsze, bufsz;

#if 0
	printk(KERN_DEBUG "usbout_start: device %d ufmt 0x%08x dfmt 0x%08x srate %d\n",
	       dev->devnum, u->format, u->dma.format, u->dma.srate);
#endif
	/* allocate USB storage if not already done */
	spin_lock_irqsave(&as->lock, flags);
	if (!(u->flags & FLG_CONNECTED)) {
		spin_unlock_irqrestore(&as->lock, flags);
		return -EIO;
	}
	if (!(u->flags & FLG_RUNNING)) {
		spin_unlock_irqrestore(&as->lock, flags);
		u->freqn = u->freqm = ((u->dma.srate << 11) + 62) / 125; /* this will overflow at approx 2MSPS */
		u->freqmax = u->freqn + (u->freqn >> 2);
		u->phase = 0;
		maxsze = (u->freqmax + 0x3fff) >> (14 - AFMT_BYTESSHIFT(u->format));
		bufsz = DESCFRAMES * maxsze;
		kfree(u->durb[0].urb->transfer_buffer);
		u->durb[0].urb->transfer_buffer = kmalloc(bufsz, GFP_KERNEL);
		u->durb[0].urb->transfer_buffer_length = bufsz;
		kfree(u->durb[1].urb->transfer_buffer);
		u->durb[1].urb->transfer_buffer = kmalloc(bufsz, GFP_KERNEL);
		u->durb[1].urb->transfer_buffer_length = bufsz;
		if (u->syncpipe) {
			kfree(u->surb[0].urb->transfer_buffer);
			u->surb[0].urb->transfer_buffer = kmalloc(3*SYNCFRAMES, GFP_KERNEL);
			u->surb[0].urb->transfer_buffer_length = 3*SYNCFRAMES;
			kfree(u->surb[1].urb->transfer_buffer);
			u->surb[1].urb->transfer_buffer = kmalloc(3*SYNCFRAMES, GFP_KERNEL);
			u->surb[1].urb->transfer_buffer_length = 3*SYNCFRAMES;
		}
		if (!u->durb[0].urb->transfer_buffer || !u->durb[1].urb->transfer_buffer || 
		    (u->syncpipe && (!u->surb[0].urb->transfer_buffer || !u->surb[1].urb->transfer_buffer))) {
			printk(KERN_ERR "usbaudio: cannot start playback device %d\n", dev->devnum);
			return 0;
		}
		spin_lock_irqsave(&as->lock, flags);
	}
	if (u->dma.count <= 0 && !u->dma.mapped) {
		spin_unlock_irqrestore(&as->lock, flags);
		return 0;
	}
       	u->flags |= FLG_RUNNING;
	if (!(u->flags & FLG_URB0RUNNING)) {
		urb = u->durb[0].urb;
		urb->dev = dev;
		urb->pipe = u->datapipe;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->number_of_packets = DESCFRAMES;
		urb->context = as;
		urb->complete = usbout_completed;
		if (!usbout_prepare_desc(u, urb) && !usb_submit_urb(urb, GFP_ATOMIC))
			u->flags |= FLG_URB0RUNNING;
		else
			u->flags &= ~FLG_RUNNING;
	}
	if (u->flags & FLG_RUNNING && !(u->flags & FLG_URB1RUNNING)) {
		urb = u->durb[1].urb;
		urb->dev = dev;
		urb->pipe = u->datapipe;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->number_of_packets = DESCFRAMES;
		urb->context = as;
		urb->complete = usbout_completed;
		if (!usbout_prepare_desc(u, urb) && !usb_submit_urb(urb, GFP_ATOMIC))
			u->flags |= FLG_URB1RUNNING;
		else
			u->flags &= ~FLG_RUNNING;
	}
	if (u->syncpipe) {
		if (u->flags & FLG_RUNNING && !(u->flags & FLG_SYNC0RUNNING)) {
			urb = u->surb[0].urb;
			urb->dev = dev;
			urb->pipe = u->syncpipe;
			urb->transfer_flags = URB_ISO_ASAP;
			urb->number_of_packets = SYNCFRAMES;
			urb->context = as;
			urb->complete = usbout_sync_completed;
			/* stride: u->syncinterval */
			if (!usbout_sync_prepare_desc(u, urb) && !usb_submit_urb(urb, GFP_ATOMIC))
				u->flags |= FLG_SYNC0RUNNING;
			else
				u->flags &= ~FLG_RUNNING;
		}
		if (u->flags & FLG_RUNNING && !(u->flags & FLG_SYNC1RUNNING)) {
			urb = u->surb[1].urb;
			urb->dev = dev;
			urb->pipe = u->syncpipe;
			urb->transfer_flags = URB_ISO_ASAP;
			urb->number_of_packets = SYNCFRAMES;
			urb->context = as;
			urb->complete = usbout_sync_completed;
			/* stride: u->syncinterval */
			if (!usbout_sync_prepare_desc(u, urb) && !usb_submit_urb(urb, GFP_ATOMIC))
				u->flags |= FLG_SYNC1RUNNING;
			else
				u->flags &= ~FLG_RUNNING;
		}
	}
	spin_unlock_irqrestore(&as->lock, flags);
	return 0;
}

/* --------------------------------------------------------------------- */

static unsigned int format_goodness(struct audioformat *afp, unsigned int fmt, unsigned int srate)
{
	unsigned int g = 0;

	if (srate < afp->sratelo)
		g += afp->sratelo - srate;
	if (srate > afp->sratehi)
		g += srate - afp->sratehi;
	if (AFMT_ISSTEREO(afp->format) && !AFMT_ISSTEREO(fmt))
		g += 0x100000;
	if (!AFMT_ISSTEREO(afp->format) && AFMT_ISSTEREO(fmt))
		g += 0x400000;
	if (AFMT_IS16BIT(afp->format) && !AFMT_IS16BIT(fmt))
		g += 0x100000;
	if (!AFMT_IS16BIT(afp->format) && AFMT_IS16BIT(fmt))
		g += 0x400000;
	return g;
}

static int find_format(struct audioformat *afp, unsigned int nr, unsigned int fmt, unsigned int srate)
{
	unsigned int i, g, gb = ~0;
	int j = -1; /* default to failure */

	/* find "best" format (according to format_goodness) */
	for (i = 0; i < nr; i++) {
		g = format_goodness(&afp[i], fmt, srate);
		if (g >= gb) 
			continue;
		j = i;
		gb = g;
	}
       	return j;
}

static int set_format_in(struct usb_audiodev *as)
{
	struct usb_device *dev = as->state->usbdev;
	struct usb_host_interface *alts;
	struct usb_interface *iface;
	struct usbin *u = &as->usbin;
	struct dmabuf *d = &u->dma;
	struct audioformat *fmt;
	unsigned int ep;
	unsigned char data[3];
	int fmtnr, ret;

	iface = usb_ifnum_to_if(dev, u->interface);
	if (!iface)
		return 0;

	fmtnr = find_format(as->fmtin, as->numfmtin, d->format, d->srate);
	if (fmtnr < 0) {
		printk(KERN_ERR "usbaudio: set_format_in(): failed to find desired format/speed combination.\n");
		return -1;
	}

	fmt = as->fmtin + fmtnr;
	alts = usb_altnum_to_altsetting(iface, fmt->altsetting);
	u->format = fmt->format;
	u->datapipe = usb_rcvisocpipe(dev, alts->endpoint[0].desc.bEndpointAddress & 0xf);
	u->syncpipe = u->syncinterval = 0;
	if ((alts->endpoint[0].desc.bmAttributes & 0x0c) == 0x08) {
		if (alts->desc.bNumEndpoints < 2 ||
		    alts->endpoint[1].desc.bmAttributes != 0x01 ||
		    alts->endpoint[1].desc.bSynchAddress != 0 ||
		    alts->endpoint[1].desc.bEndpointAddress != (alts->endpoint[0].desc.bSynchAddress & 0x7f)) {
			printk(KERN_WARNING "usbaudio: device %d interface %d altsetting %d claims adaptive in "
			       "but has invalid synch pipe; treating as asynchronous in\n",
			       dev->devnum, u->interface, fmt->altsetting);
		} else {
			u->syncpipe = usb_sndisocpipe(dev, alts->endpoint[1].desc.bEndpointAddress & 0xf);
			u->syncinterval = alts->endpoint[1].desc.bRefresh;
		}
	}
	if (d->srate < fmt->sratelo)
		d->srate = fmt->sratelo;
	if (d->srate > fmt->sratehi)
		d->srate = fmt->sratehi;
	dprintk((KERN_DEBUG "usbaudio: set_format_in: usb_set_interface %u %u\n",
			u->interface, fmt->altsetting));
	if (usb_set_interface(dev, alts->desc.bInterfaceNumber, fmt->altsetting) < 0) {
		printk(KERN_WARNING "usbaudio: usb_set_interface failed, device %d interface %d altsetting %d\n",
		       dev->devnum, u->interface, fmt->altsetting);
		return -1;
	}
	if (fmt->sratelo == fmt->sratehi)
		return 0;
	ep = usb_pipeendpoint(u->datapipe) | (u->datapipe & USB_DIR_IN);
	/* if endpoint has pitch control, enable it */
	if (fmt->attributes & 0x02) {
		data[0] = 1;
		if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT, 
					   PITCH_CONTROL << 8, ep, data, 1, 1000)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to set output pitch control device %d interface %u endpoint 0x%x to %u\n",
			       ret, dev->devnum, u->interface, ep, d->srate);
			return -1;
		}
	}
	/* if endpoint has sampling rate control, set it */
	if (fmt->attributes & 0x01) {
		data[0] = d->srate;
		data[1] = d->srate >> 8;
		data[2] = d->srate >> 16;
		if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT, 
					   SAMPLING_FREQ_CONTROL << 8, ep, data, 3, 1000)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to set input sampling frequency device %d interface %u endpoint 0x%x to %u\n",
			       ret, dev->devnum, u->interface, ep, d->srate);
			return -1;
		}
		if ((ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_IN,
					   SAMPLING_FREQ_CONTROL << 8, ep, data, 3, 1000)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to get input sampling frequency device %d interface %u endpoint 0x%x\n",
			       ret, dev->devnum, u->interface, ep);
			return -1;
		}
		dprintk((KERN_DEBUG "usbaudio: set_format_in: device %d interface %d altsetting %d srate req: %u real %u\n",
		        dev->devnum, u->interface, fmt->altsetting, d->srate, data[0] | (data[1] << 8) | (data[2] << 16)));
		d->srate = data[0] | (data[1] << 8) | (data[2] << 16);
	}
	dprintk((KERN_DEBUG "usbaudio: set_format_in: USB format 0x%x, DMA format 0x%x srate %u\n", u->format, d->format, d->srate));
	return 0;
}

static int set_format_out(struct usb_audiodev *as)
{
	struct usb_device *dev = as->state->usbdev;
	struct usb_host_interface *alts;
	struct usb_interface *iface;	
	struct usbout *u = &as->usbout;
	struct dmabuf *d = &u->dma;
	struct audioformat *fmt;
	unsigned int ep;
	unsigned char data[3];
	int fmtnr, ret;

	iface = usb_ifnum_to_if(dev, u->interface);
	if (!iface)
		return 0;

	fmtnr = find_format(as->fmtout, as->numfmtout, d->format, d->srate);
	if (fmtnr < 0) {
		printk(KERN_ERR "usbaudio: set_format_out(): failed to find desired format/speed combination.\n");
		return -1;
	}

	fmt = as->fmtout + fmtnr;
	u->format = fmt->format;
	alts = usb_altnum_to_altsetting(iface, fmt->altsetting);
	u->datapipe = usb_sndisocpipe(dev, alts->endpoint[0].desc.bEndpointAddress & 0xf);
	u->syncpipe = u->syncinterval = 0;
	if ((alts->endpoint[0].desc.bmAttributes & 0x0c) == 0x04) {
#if 0
		printk(KERN_DEBUG "bNumEndpoints 0x%02x endpoint[1].bmAttributes 0x%02x\n"
		       KERN_DEBUG "endpoint[1].bSynchAddress 0x%02x endpoint[1].bEndpointAddress 0x%02x\n"
		       KERN_DEBUG "endpoint[0].bSynchAddress 0x%02x\n", alts->bNumEndpoints,
		       alts->endpoint[1].bmAttributes, alts->endpoint[1].bSynchAddress,
		       alts->endpoint[1].bEndpointAddress, alts->endpoint[0].bSynchAddress);
#endif
		if (alts->desc.bNumEndpoints < 2 ||
		    alts->endpoint[1].desc.bmAttributes != 0x01 ||
		    alts->endpoint[1].desc.bSynchAddress != 0 ||
		    alts->endpoint[1].desc.bEndpointAddress != (alts->endpoint[0].desc.bSynchAddress | 0x80)) {
			printk(KERN_WARNING "usbaudio: device %d interface %d altsetting %d claims asynch out "
			       "but has invalid synch pipe; treating as adaptive out\n",
			       dev->devnum, u->interface, fmt->altsetting);
		} else {
			u->syncpipe = usb_rcvisocpipe(dev, alts->endpoint[1].desc.bEndpointAddress & 0xf);
			u->syncinterval = alts->endpoint[1].desc.bRefresh;
		}
	}
	if (d->srate < fmt->sratelo)
		d->srate = fmt->sratelo;
	if (d->srate > fmt->sratehi)
		d->srate = fmt->sratehi;
	dprintk((KERN_DEBUG "usbaudio: set_format_out: usb_set_interface %u %u\n",
			u->interface, fmt->altsetting));
	if (usb_set_interface(dev, u->interface, fmt->altsetting) < 0) {
		printk(KERN_WARNING "usbaudio: usb_set_interface failed, device %d interface %d altsetting %d\n",
		       dev->devnum, u->interface, fmt->altsetting);
		return -1;
	}
	if (fmt->sratelo == fmt->sratehi)
		return 0;
	ep = usb_pipeendpoint(u->datapipe) | (u->datapipe & USB_DIR_IN);
	/* if endpoint has pitch control, enable it */
	if (fmt->attributes & 0x02) {
		data[0] = 1;
		if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT, 
					   PITCH_CONTROL << 8, ep, data, 1, 1000)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to set output pitch control device %d interface %u endpoint 0x%x to %u\n",
			       ret, dev->devnum, u->interface, ep, d->srate);
			return -1;
		}
	}
	/* if endpoint has sampling rate control, set it */
	if (fmt->attributes & 0x01) {
		data[0] = d->srate;
		data[1] = d->srate >> 8;
		data[2] = d->srate >> 16;
		if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT, 
					   SAMPLING_FREQ_CONTROL << 8, ep, data, 3, 1000)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to set output sampling frequency device %d interface %u endpoint 0x%x to %u\n",
			       ret, dev->devnum, u->interface, ep, d->srate);
			return -1;
		}
		if ((ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_IN,
					   SAMPLING_FREQ_CONTROL << 8, ep, data, 3, 1000)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to get output sampling frequency device %d interface %u endpoint 0x%x\n",
			       ret, dev->devnum, u->interface, ep);
			return -1;
		}
		dprintk((KERN_DEBUG "usbaudio: set_format_out: device %d interface %d altsetting %d srate req: %u real %u\n",
		        dev->devnum, u->interface, fmt->altsetting, d->srate, data[0] | (data[1] << 8) | (data[2] << 16)));
		d->srate = data[0] | (data[1] << 8) | (data[2] << 16);
	}
	dprintk((KERN_DEBUG "usbaudio: set_format_out: USB format 0x%x, DMA format 0x%x srate %u\n", u->format, d->format, d->srate));
	return 0;
}

static int set_format(struct usb_audiodev *s, unsigned int fmode, unsigned int fmt, unsigned int srate)
{
	int ret1 = 0, ret2 = 0;

	if (!(fmode & (FMODE_READ|FMODE_WRITE)))
		return -EINVAL;
	if (fmode & FMODE_READ) {
		usbin_stop(s);
		s->usbin.dma.ready = 0;
		if (fmt == AFMT_QUERY)
			fmt = s->usbin.dma.format;
		else
			s->usbin.dma.format = fmt;
		if (!srate)
			srate = s->usbin.dma.srate;
		else
			s->usbin.dma.srate = srate;
	}
	if (fmode & FMODE_WRITE) {
		usbout_stop(s);
		s->usbout.dma.ready = 0;
		if (fmt == AFMT_QUERY)
			fmt = s->usbout.dma.format;
		else
			s->usbout.dma.format = fmt;
		if (!srate)
			srate = s->usbout.dma.srate;
		else
			s->usbout.dma.srate = srate;
	}
	if (fmode & FMODE_READ)
		ret1 = set_format_in(s);
	if (fmode & FMODE_WRITE)
		ret2 = set_format_out(s);
	return ret1 ? ret1 : ret2;
}

/* --------------------------------------------------------------------- */

static int wrmixer(struct usb_mixerdev *ms, unsigned mixch, unsigned value)
{
	struct usb_device *dev = ms->state->usbdev;
	unsigned char data[2];
	struct mixerchannel *ch;
	int v1, v2, v3;

	if (mixch >= ms->numch)
		return -1;
	ch = &ms->ch[mixch];
	v3 = ch->maxval - ch->minval;
	v1 = value & 0xff;
	v2 = (value >> 8) & 0xff;
	if (v1 > 100)
		v1 = 100;
	if (v2 > 100)
		v2 = 100;
	if (!(ch->flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)))
		v2 = v1;
	ch->value = v1 | (v2 << 8);
	v1 = (v1 * v3) / 100 + ch->minval;
	v2 = (v2 * v3) / 100 + ch->minval;
	switch (ch->selector) {
	case 0:  /* mixer unit request */
		data[0] = v1;
		data[1] = v1 >> 8;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    (ch->chnum << 8) | 1, ms->iface | (ch->unitid << 8), data, 2, 1000) < 0)
			goto err;
		if (!(ch->flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)))
			return 0;
		data[0] = v2;
		data[1] = v2 >> 8;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    ((ch->chnum + !!(ch->flags & MIXFLG_STEREOIN)) << 8) | (1 + !!(ch->flags & MIXFLG_STEREOOUT)),
				    ms->iface | (ch->unitid << 8), data, 2, 1000) < 0)
			goto err;
		return 0;

		/* various feature unit controls */
	case VOLUME_CONTROL:
		data[0] = v1;
		data[1] = v1 >> 8;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    (ch->selector << 8) | ch->chnum, ms->iface | (ch->unitid << 8), data, 2, 1000) < 0)
			goto err;
		if (!(ch->flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)))
			return 0;
		data[0] = v2;
		data[1] = v2 >> 8;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    (ch->selector << 8) | (ch->chnum + 1), ms->iface | (ch->unitid << 8), data, 2, 1000) < 0)
			goto err;
		return 0;
                
	case BASS_CONTROL:
	case MID_CONTROL:
	case TREBLE_CONTROL:
		data[0] = v1 >> 8;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    (ch->selector << 8) | ch->chnum, ms->iface | (ch->unitid << 8), data, 1, 1000) < 0)
			goto err;
		if (!(ch->flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)))
			return 0;
		data[0] = v2 >> 8;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    (ch->selector << 8) | (ch->chnum + 1), ms->iface | (ch->unitid << 8), data, 1, 1000) < 0)
			goto err;
		return 0;

	default:
		return -1;
	}
	return 0;

 err:
	printk(KERN_ERR "usbaudio: mixer request device %u if %u unit %u ch %u selector %u failed\n", 
		dev->devnum, ms->iface, ch->unitid, ch->chnum, ch->selector);
	return -1;
}

static int get_rec_src(struct usb_mixerdev *ms)
{
	struct usb_device *dev = ms->state->usbdev;
	unsigned int mask = 0, retmask = 0;
	unsigned int i, j;
	unsigned char buf;
	int err = 0;

	for (i = 0; i < ms->numch; i++) {
		if (!ms->ch[i].slctunitid || (mask & (1 << i)))
			continue;
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    0, ms->iface | (ms->ch[i].slctunitid << 8), &buf, 1, 1000) < 0) {
			err = -EIO;
			printk(KERN_ERR "usbaudio: selector read request device %u if %u unit %u failed\n", 
			       dev->devnum, ms->iface, ms->ch[i].slctunitid & 0xff);
			continue;
		}
		for (j = i; j < ms->numch; j++) {
			if ((ms->ch[i].slctunitid ^ ms->ch[j].slctunitid) & 0xff)
				continue;
			mask |= 1 << j;
			if (buf == (ms->ch[j].slctunitid >> 8))
				retmask |= 1 << ms->ch[j].osschannel;
		}
	}
	if (err)
		return -EIO;
	return retmask;
}

static int set_rec_src(struct usb_mixerdev *ms, int srcmask)
{
	struct usb_device *dev = ms->state->usbdev;
	unsigned int mask = 0, smask, bmask;
	unsigned int i, j;
	unsigned char buf;
	int err = 0;

	for (i = 0; i < ms->numch; i++) {
		if (!ms->ch[i].slctunitid || (mask & (1 << i)))
			continue;
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    0, ms->iface | (ms->ch[i].slctunitid << 8), &buf, 1, 1000) < 0) {
			err = -EIO;
			printk(KERN_ERR "usbaudio: selector read request device %u if %u unit %u failed\n", 
			       dev->devnum, ms->iface, ms->ch[i].slctunitid & 0xff);
			continue;
		}
		/* first generate smask */
		smask = bmask = 0;
		for (j = i; j < ms->numch; j++) {
			if ((ms->ch[i].slctunitid ^ ms->ch[j].slctunitid) & 0xff)
				continue;
			smask |= 1 << ms->ch[j].osschannel;
			if (buf == (ms->ch[j].slctunitid >> 8))
				bmask |= 1 << ms->ch[j].osschannel;
			mask |= 1 << j;
		}
		/* check for multiple set sources */
		j = hweight32(srcmask & smask);
		if (j == 0)
			continue;
		if (j > 1)
			srcmask &= ~bmask;
		for (j = i; j < ms->numch; j++) {
			if ((ms->ch[i].slctunitid ^ ms->ch[j].slctunitid) & 0xff)
				continue;
			if (!(srcmask & (1 << ms->ch[j].osschannel)))
				continue;
			buf = ms->ch[j].slctunitid >> 8;
			if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    0, ms->iface | (ms->ch[j].slctunitid << 8), &buf, 1, 1000) < 0) {
				err = -EIO;
				printk(KERN_ERR "usbaudio: selector write request device %u if %u unit %u failed\n", 
				       dev->devnum, ms->iface, ms->ch[j].slctunitid & 0xff);
				continue;
			}
		}
	}
	return err ? -EIO : 0;
}

/* --------------------------------------------------------------------- */

/*
 * should be called with open_sem hold, so that no new processes
 * look at the audio device to be destroyed
 */

static void release(struct usb_audio_state *s)
{
	struct usb_audiodev *as;
	struct usb_mixerdev *ms;

	s->count--;
	if (s->count) {
		up(&open_sem);
		return;
	}
	up(&open_sem);
	wake_up(&open_wait);
	while (!list_empty(&s->audiolist)) {
		as = list_entry(s->audiolist.next, struct usb_audiodev, list);
		list_del(&as->list);
		usbin_release(as);
		usbout_release(as);
		dmabuf_release(&as->usbin.dma);
		dmabuf_release(&as->usbout.dma);
		usb_free_urb(as->usbin.durb[0].urb);
		usb_free_urb(as->usbin.durb[1].urb);
		usb_free_urb(as->usbin.surb[0].urb);
		usb_free_urb(as->usbin.surb[1].urb);
		usb_free_urb(as->usbout.durb[0].urb);
		usb_free_urb(as->usbout.durb[1].urb);
		usb_free_urb(as->usbout.surb[0].urb);
		usb_free_urb(as->usbout.surb[1].urb);
		kfree(as);
	}
	while (!list_empty(&s->mixerlist)) {
		ms = list_entry(s->mixerlist.next, struct usb_mixerdev, list);
		list_del(&ms->list);
		kfree(ms);
	}
	kfree(s);
}

static inline int prog_dmabuf_in(struct usb_audiodev *as)
{
	usbin_stop(as);
	return dmabuf_init(&as->usbin.dma);
}

static inline int prog_dmabuf_out(struct usb_audiodev *as)
{
	usbout_stop(as);
	return dmabuf_init(&as->usbout.dma);
}

/* --------------------------------------------------------------------- */

static int usb_audio_open_mixdev(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct usb_mixerdev *ms;
	struct usb_audio_state *s;

	down(&open_sem);
	list_for_each_entry(s, &audiodevs, audiodev) {
		list_for_each_entry(ms, &s->mixerlist, list) {
			if (ms->dev_mixer == minor)
				goto mixer_found;
		}
	}
	up(&open_sem);
	return -ENODEV;

 mixer_found:
	if (!s->usbdev) {
		up(&open_sem);
		return -EIO;
	}
	file->private_data = ms;
	s->count++;

	up(&open_sem);
	return nonseekable_open(inode, file);
}

static int usb_audio_release_mixdev(struct inode *inode, struct file *file)
{
	struct usb_mixerdev *ms = (struct usb_mixerdev *)file->private_data;
	struct usb_audio_state *s;

	lock_kernel();
	s = ms->state;
	down(&open_sem);
	release(s);
	unlock_kernel();
	return 0;
}

static int usb_audio_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct usb_mixerdev *ms = (struct usb_mixerdev *)file->private_data;
	int i, j, val;
	int __user *user_arg = (int __user *)arg;

	if (!ms->state->usbdev)
		return -ENODEV;
  
	if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;

		memset(&info, 0, sizeof(info));
		strncpy(info.id, "USB_AUDIO", sizeof(info.id));
		strncpy(info.name, "USB Audio Class Driver", sizeof(info.name));
		info.modify_counter = ms->modcnt;
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;

		memset(&info, 0, sizeof(info));
		strncpy(info.id, "USB_AUDIO", sizeof(info.id));
		strncpy(info.name, "USB Audio Class Driver", sizeof(info.name));
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, user_arg);
	if (_IOC_TYPE(cmd) != 'M' || _IOC_SIZE(cmd) != sizeof(int))
		return -EINVAL;
	if (_IOC_DIR(cmd) == _IOC_READ) {
		switch (_IOC_NR(cmd)) {
		case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
			val = get_rec_src(ms);
			if (val < 0)
				return val;
			return put_user(val, user_arg);

		case SOUND_MIXER_DEVMASK: /* Arg contains a bit for each supported device */
			for (val = i = 0; i < ms->numch; i++)
				val |= 1 << ms->ch[i].osschannel;
			return put_user(val, user_arg);

		case SOUND_MIXER_RECMASK: /* Arg contains a bit for each supported recording source */
			for (val = i = 0; i < ms->numch; i++)
				if (ms->ch[i].slctunitid)
					val |= 1 << ms->ch[i].osschannel;
			return put_user(val, user_arg);

		case SOUND_MIXER_STEREODEVS: /* Mixer channels supporting stereo */
			for (val = i = 0; i < ms->numch; i++)
				if (ms->ch[i].flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT))
					val |= 1 << ms->ch[i].osschannel;
			return put_user(val, user_arg);
			
		case SOUND_MIXER_CAPS:
			return put_user(SOUND_CAP_EXCL_INPUT, user_arg);

		default:
			i = _IOC_NR(cmd);
			if (i >= SOUND_MIXER_NRDEVICES)
				return -EINVAL;
			for (j = 0; j < ms->numch; j++) {
				if (ms->ch[j].osschannel == i) {
					return put_user(ms->ch[j].value, user_arg);
				}
			}
			return -EINVAL;
		}
	}
	if (_IOC_DIR(cmd) != (_IOC_READ|_IOC_WRITE)) 
		return -EINVAL;
	ms->modcnt++;
	switch (_IOC_NR(cmd)) {
	case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
		if (get_user(val, user_arg))
			return -EFAULT;
		return set_rec_src(ms, val);

	default:
		i = _IOC_NR(cmd);
		if (i >= SOUND_MIXER_NRDEVICES)
			return -EINVAL;
		for (j = 0; j < ms->numch && ms->ch[j].osschannel != i; j++);
		if (j >= ms->numch)
			return -EINVAL;
		if (get_user(val, user_arg))
			return -EFAULT;
		if (wrmixer(ms, j, val))
			return -EIO;
		return put_user(ms->ch[j].value, user_arg);
	}
}

static /*const*/ struct file_operations usb_mixer_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.ioctl =	usb_audio_ioctl_mixdev,
	.open =		usb_audio_open_mixdev,
	.release =	usb_audio_release_mixdev,
};

/* --------------------------------------------------------------------- */

static int drain_out(struct usb_audiodev *as, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int count, tmo;
	
	if (as->usbout.dma.mapped || !as->usbout.dma.ready)
		return 0;
	usbout_start(as);
	add_wait_queue(&as->usbout.dma.wait, &wait);
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&as->lock, flags);
		count = as->usbout.dma.count;
		spin_unlock_irqrestore(&as->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
			break;
		if (nonblock) {
			remove_wait_queue(&as->usbout.dma.wait, &wait);
			set_current_state(TASK_RUNNING);
			return -EBUSY;
		}
		tmo = 3 * HZ * count / as->usbout.dma.srate;
		tmo >>= AFMT_BYTESSHIFT(as->usbout.dma.format);
		if (!schedule_timeout(tmo + 1)) {
			printk(KERN_DEBUG "usbaudio: dma timed out??\n");
			break;
		}
	}
	remove_wait_queue(&as->usbout.dma.wait, &wait);
	set_current_state(TASK_RUNNING);
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

/* --------------------------------------------------------------------- */

static ssize_t usb_audio_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret = 0;
	unsigned long flags;
	unsigned int ptr;
	int cnt, err;

	if (as->usbin.dma.mapped)
		return -ENXIO;
	if (!as->usbin.dma.ready && (ret = prog_dmabuf_in(as)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	add_wait_queue(&as->usbin.dma.wait, &wait);
	while (count > 0) {
		spin_lock_irqsave(&as->lock, flags);
		ptr = as->usbin.dma.rdptr;
		cnt = as->usbin.dma.count;
		/* set task state early to avoid wakeup races */
		if (cnt <= 0)
			__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&as->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (usbin_start(as)) {
				if (!ret)
					ret = -ENODEV;
				break;
			}
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				break;
			}
			continue;
		}
		if ((err = dmabuf_copyout_user(&as->usbin.dma, ptr, buffer, cnt))) {
			if (!ret)
				ret = err;
			break;
		}
		ptr += cnt;
		if (ptr >= as->usbin.dma.dmasize)
			ptr -= as->usbin.dma.dmasize;
		spin_lock_irqsave(&as->lock, flags);
		as->usbin.dma.rdptr = ptr;
		as->usbin.dma.count -= cnt;
		spin_unlock_irqrestore(&as->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&as->usbin.dma.wait, &wait);
	return ret;
}

static ssize_t usb_audio_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret = 0;
	unsigned long flags;
	unsigned int ptr;
	unsigned int start_thr;
	int cnt, err;

	if (as->usbout.dma.mapped)
		return -ENXIO;
	if (!as->usbout.dma.ready && (ret = prog_dmabuf_out(as)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	start_thr = (as->usbout.dma.srate << AFMT_BYTESSHIFT(as->usbout.dma.format)) / (1000 / (3 * DESCFRAMES));
	add_wait_queue(&as->usbout.dma.wait, &wait);
	while (count > 0) {
#if 0
		printk(KERN_DEBUG "usb_audio_write: count %u dma: count %u rdptr %u wrptr %u dmasize %u fragsize %u flags 0x%02x taskst 0x%lx\n",
		       count, as->usbout.dma.count, as->usbout.dma.rdptr, as->usbout.dma.wrptr, as->usbout.dma.dmasize, as->usbout.dma.fragsize,
		       as->usbout.flags, current->state);
#endif
		spin_lock_irqsave(&as->lock, flags);
		if (as->usbout.dma.count < 0) {
			as->usbout.dma.count = 0;
			as->usbout.dma.rdptr = as->usbout.dma.wrptr;
		}
		ptr = as->usbout.dma.wrptr;
		cnt = as->usbout.dma.dmasize - as->usbout.dma.count;
		/* set task state early to avoid wakeup races */
		if (cnt <= 0)
			__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&as->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (usbout_start(as)) {
				if (!ret)
					ret = -ENODEV;
				break;
			}
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				break;
			}
			continue;
		}
		if ((err = dmabuf_copyin_user(&as->usbout.dma, ptr, buffer, cnt))) {
			if (!ret)
				ret = err;
			break;
		}
		ptr += cnt;
		if (ptr >= as->usbout.dma.dmasize)
			ptr -= as->usbout.dma.dmasize;
		spin_lock_irqsave(&as->lock, flags);
		as->usbout.dma.wrptr = ptr;
		as->usbout.dma.count += cnt;
		spin_unlock_irqrestore(&as->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		if (as->usbout.dma.count >= start_thr && usbout_start(as)) {
			if (!ret)
				ret = -ENODEV;
			break;
		}
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&as->usbout.dma.wait, &wait);
	return ret;
}

/* Called without the kernel lock - fine */
static unsigned int usb_audio_poll(struct file *file, struct poll_table_struct *wait)
{
	struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	if (file->f_mode & FMODE_WRITE) {
		if (!as->usbout.dma.ready)
			prog_dmabuf_out(as);
		poll_wait(file, &as->usbout.dma.wait, wait);
	}
	if (file->f_mode & FMODE_READ) {
		if (!as->usbin.dma.ready)
			prog_dmabuf_in(as);
		poll_wait(file, &as->usbin.dma.wait, wait);
	}
	spin_lock_irqsave(&as->lock, flags);
	if (file->f_mode & FMODE_READ) {
		if (as->usbin.dma.count >= (signed)as->usbin.dma.fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (as->usbout.dma.mapped) {
			if (as->usbout.dma.count >= (signed)as->usbout.dma.fragsize) 
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)as->usbout.dma.dmasize >= as->usbout.dma.count + (signed)as->usbout.dma.fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&as->lock, flags);
	return mask;
}

static int usb_audio_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
	struct dmabuf *db;
	int ret = -EINVAL;

	lock_kernel();
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf_out(as)) != 0)
			goto out;
		db = &as->usbout.dma;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf_in(as)) != 0)
			goto out;
		db = &as->usbin.dma;
	} else
		goto out;

	ret = -EINVAL;
	if (vma->vm_pgoff != 0)
		goto out;

	ret = dmabuf_mmap(vma, db,  vma->vm_start, vma->vm_end - vma->vm_start, vma->vm_page_prot);
out:
	unlock_kernel();
	return ret;
}

static int usb_audio_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
	struct usb_audio_state *s = as->state;
	int __user *user_arg = (int __user *)arg;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	int val = 0;
	int val2, mapped, ret;

	if (!s->usbdev)
		return -EIO;
	mapped = ((file->f_mode & FMODE_WRITE) && as->usbout.dma.mapped) ||
		((file->f_mode & FMODE_READ) && as->usbin.dma.mapped);
#if 0
	if (arg)
		get_user(val, (int *)arg);
	printk(KERN_DEBUG "usbaudio: usb_audio_ioctl cmd=%x arg=%lx *arg=%d\n", cmd, arg, val)
#endif
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, user_arg);

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_out(as, 0/*file->f_flags & O_NONBLOCK*/);
		return 0;

	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | 
				DSP_CAP_MMAP | DSP_CAP_BATCH, user_arg);

	case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			usbout_stop(as);
			as->usbout.dma.rdptr = as->usbout.dma.wrptr = as->usbout.dma.count = as->usbout.dma.total_bytes = 0;
		}
		if (file->f_mode & FMODE_READ) {
			usbin_stop(as);
			as->usbin.dma.rdptr = as->usbin.dma.wrptr = as->usbin.dma.count = as->usbin.dma.total_bytes = 0;
		}
		return 0;

	case SNDCTL_DSP_SPEED:
		if (get_user(val, user_arg))
			return -EFAULT;
		if (val >= 0) {
			if (val < 4000)
				val = 4000;
			if (val > 100000)
				val = 100000;
			if (set_format(as, file->f_mode, AFMT_QUERY, val))
				return -EIO;
		}
		return put_user((file->f_mode & FMODE_READ) ? 
				as->usbin.dma.srate : as->usbout.dma.srate,
				user_arg);

	case SNDCTL_DSP_STEREO:
		if (get_user(val, user_arg))
			return -EFAULT;
		val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
		if (val)
			val2 |= AFMT_STEREO;
		else
			val2 &= ~AFMT_STEREO;
		if (set_format(as, file->f_mode, val2, 0))
			return -EIO;
		return 0;

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, user_arg))
			return -EFAULT;
		if (val != 0) {
			val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
			if (val == 1)
				val2 &= ~AFMT_STEREO;
			else
				val2 |= AFMT_STEREO;
			if (set_format(as, file->f_mode, val2, 0))
				return -EIO;
		}
		val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
		return put_user(AFMT_ISSTEREO(val2) ? 2 : 1, user_arg);

	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
		return put_user(AFMT_U8 | AFMT_U16_LE | AFMT_U16_BE |
				AFMT_S8 | AFMT_S16_LE | AFMT_S16_BE, user_arg);

	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		if (get_user(val, user_arg))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			if (hweight32(val) != 1)
				return -EINVAL;
			if (!(val & (AFMT_U8 | AFMT_U16_LE | AFMT_U16_BE |
				     AFMT_S8 | AFMT_S16_LE | AFMT_S16_BE)))
				return -EINVAL;
			val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
			val |= val2 & AFMT_STEREO;
			if (set_format(as, file->f_mode, val, 0))
				return -EIO;
		}
		val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
		return put_user(val2 & ~AFMT_STEREO, user_arg);

	case SNDCTL_DSP_POST:
		return 0;

	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if (file->f_mode & FMODE_READ && as->usbin.flags & FLG_RUNNING) 
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & FMODE_WRITE && as->usbout.flags & FLG_RUNNING) 
			val |= PCM_ENABLE_OUTPUT;
		return put_user(val, user_arg);

	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, user_arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!as->usbin.dma.ready && (ret = prog_dmabuf_in(as)))
					return ret;
				if (usbin_start(as))
					return -ENODEV;
			} else
				usbin_stop(as);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!as->usbout.dma.ready && (ret = prog_dmabuf_out(as)))
					return ret;
				if (usbout_start(as))
					return -ENODEV;
			} else
				usbout_stop(as);
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!(as->usbout.flags & FLG_RUNNING) && (val = prog_dmabuf_out(as)) != 0)
			return val;
		spin_lock_irqsave(&as->lock, flags);
		abinfo.fragsize = as->usbout.dma.fragsize;
		abinfo.bytes = as->usbout.dma.dmasize - as->usbout.dma.count;
		abinfo.fragstotal = as->usbout.dma.numfrag;
		abinfo.fragments = abinfo.bytes >> as->usbout.dma.fragshift;      
		spin_unlock_irqrestore(&as->lock, flags);
		return copy_to_user((void __user *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!(as->usbin.flags & FLG_RUNNING) && (val = prog_dmabuf_in(as)) != 0)
			return val;
		spin_lock_irqsave(&as->lock, flags);
		abinfo.fragsize = as->usbin.dma.fragsize;
		abinfo.bytes = as->usbin.dma.count;
		abinfo.fragstotal = as->usbin.dma.numfrag;
		abinfo.fragments = abinfo.bytes >> as->usbin.dma.fragshift;      
		spin_unlock_irqrestore(&as->lock, flags);
		return copy_to_user((void __user *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
		
	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&as->lock, flags);
		val = as->usbout.dma.count;
		spin_unlock_irqrestore(&as->lock, flags);
		return put_user(val, user_arg);

	case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		spin_lock_irqsave(&as->lock, flags);
		cinfo.bytes = as->usbin.dma.total_bytes;
		cinfo.blocks = as->usbin.dma.count >> as->usbin.dma.fragshift;
		cinfo.ptr = as->usbin.dma.wrptr;
		if (as->usbin.dma.mapped)
			as->usbin.dma.count &= as->usbin.dma.fragsize-1;
		spin_unlock_irqrestore(&as->lock, flags);
		if (copy_to_user((void __user *)arg, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;

	case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&as->lock, flags);
		cinfo.bytes = as->usbout.dma.total_bytes;
		cinfo.blocks = as->usbout.dma.count >> as->usbout.dma.fragshift;
		cinfo.ptr = as->usbout.dma.rdptr;
		if (as->usbout.dma.mapped)
			as->usbout.dma.count &= as->usbout.dma.fragsize-1;
		spin_unlock_irqrestore(&as->lock, flags);
		if (copy_to_user((void __user *)arg, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;

       case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf_out(as)))
				return val;
			return put_user(as->usbout.dma.fragsize, user_arg);
		}
		if ((val = prog_dmabuf_in(as)))
			return val;
		return put_user(as->usbin.dma.fragsize, user_arg);

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, user_arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			as->usbin.dma.ossfragshift = val & 0xffff;
			as->usbin.dma.ossmaxfrags = (val >> 16) & 0xffff;
			if (as->usbin.dma.ossfragshift < 4)
				as->usbin.dma.ossfragshift = 4;
			if (as->usbin.dma.ossfragshift > 15)
				as->usbin.dma.ossfragshift = 15;
			if (as->usbin.dma.ossmaxfrags < 4)
				as->usbin.dma.ossmaxfrags = 4;
		}
		if (file->f_mode & FMODE_WRITE) {
			as->usbout.dma.ossfragshift = val & 0xffff;
			as->usbout.dma.ossmaxfrags = (val >> 16) & 0xffff;
			if (as->usbout.dma.ossfragshift < 4)
				as->usbout.dma.ossfragshift = 4;
			if (as->usbout.dma.ossfragshift > 15)
				as->usbout.dma.ossfragshift = 15;
			if (as->usbout.dma.ossmaxfrags < 4)
				as->usbout.dma.ossmaxfrags = 4;
		}
		return 0;

	case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && as->usbin.dma.subdivision) ||
		    (file->f_mode & FMODE_WRITE && as->usbout.dma.subdivision))
			return -EINVAL;
		if (get_user(val, user_arg))
			return -EFAULT;
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		if (file->f_mode & FMODE_READ)
			as->usbin.dma.subdivision = val;
		if (file->f_mode & FMODE_WRITE)
			as->usbout.dma.subdivision = val;
		return 0;

	case SOUND_PCM_READ_RATE:
		return put_user((file->f_mode & FMODE_READ) ? 
				as->usbin.dma.srate : as->usbout.dma.srate,
				user_arg);

	case SOUND_PCM_READ_CHANNELS:
		val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
		return put_user(AFMT_ISSTEREO(val2) ? 2 : 1, user_arg);

	case SOUND_PCM_READ_BITS:
		val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
		return put_user(AFMT_IS16BIT(val2) ? 16 : 8, user_arg);

	case SOUND_PCM_WRITE_FILTER:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;
	}
	dprintk((KERN_DEBUG "usbaudio: usb_audio_ioctl - no command found\n"));
	return -ENOIOCTLCMD;
}

static int usb_audio_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	struct usb_audiodev *as;
	struct usb_audio_state *s;

	for (;;) {
		down(&open_sem);
		list_for_each_entry(s, &audiodevs, audiodev) {
			list_for_each_entry(as, &s->audiolist, list) {
				if (!((as->dev_audio ^ minor) & ~0xf))
					goto device_found;
			}
		}
		up(&open_sem);
		return -ENODEV;

	device_found:
		if (!s->usbdev) {
			up(&open_sem);
			return -EIO;
		}
		/* wait for device to become free */
		if (!(as->open_mode & file->f_mode))
			break;
		if (file->f_flags & O_NONBLOCK) {
			up(&open_sem);
			return -EBUSY;
		}
		__set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&open_wait, &wait);
		up(&open_sem);
		schedule();
		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&open_wait, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}
	if (file->f_mode & FMODE_READ)
		as->usbin.dma.ossfragshift = as->usbin.dma.ossmaxfrags = as->usbin.dma.subdivision = 0;
	if (file->f_mode & FMODE_WRITE)
		as->usbout.dma.ossfragshift = as->usbout.dma.ossmaxfrags = as->usbout.dma.subdivision = 0;
	if (set_format(as, file->f_mode, ((minor & 0xf) == SND_DEV_DSP16) ? AFMT_S16_LE : AFMT_U8 /* AFMT_ULAW */, 8000)) {
		up(&open_sem);
		return -EIO;
	}
	file->private_data = as;
	as->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	s->count++;
	up(&open_sem);
	return nonseekable_open(inode, file);
}

static int usb_audio_release(struct inode *inode, struct file *file)
{
	struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
	struct usb_audio_state *s;
	struct usb_device *dev;

	lock_kernel();
	s = as->state;
	dev = s->usbdev;
	if (file->f_mode & FMODE_WRITE)
		drain_out(as, file->f_flags & O_NONBLOCK);
	down(&open_sem);
	if (file->f_mode & FMODE_WRITE) {
		usbout_stop(as);
		if (dev && as->usbout.interface >= 0)
			usb_set_interface(dev, as->usbout.interface, 0);
		dmabuf_release(&as->usbout.dma);
		usbout_release(as);
	}
	if (file->f_mode & FMODE_READ) {
		usbin_stop(as);
		if (dev && as->usbin.interface >= 0)
			usb_set_interface(dev, as->usbin.interface, 0);
		dmabuf_release(&as->usbin.dma);
		usbin_release(as);
	}
	as->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);
	release(s);
	wake_up(&open_wait);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations usb_audio_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		usb_audio_read,
	.write =	usb_audio_write,
	.poll =		usb_audio_poll,
	.ioctl =	usb_audio_ioctl,
	.mmap =		usb_audio_mmap,
	.open =		usb_audio_open,
	.release =	usb_audio_release,
};

/* --------------------------------------------------------------------- */

static int usb_audio_probe(struct usb_interface *iface,
			   const struct usb_device_id *id);
static void usb_audio_disconnect(struct usb_interface *iface);

static struct usb_device_id usb_audio_ids [] = {
    { .match_flags = (USB_DEVICE_ID_MATCH_INT_CLASS | USB_DEVICE_ID_MATCH_INT_SUBCLASS),
      .bInterfaceClass = USB_CLASS_AUDIO, .bInterfaceSubClass = 1},
    { }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_audio_ids);

static struct usb_driver usb_audio_driver = {
	.owner =	THIS_MODULE,
	.name =		"audio",
	.probe =	usb_audio_probe,
	.disconnect =	usb_audio_disconnect,
	.id_table =	usb_audio_ids,
};

static void *find_descriptor(void *descstart, unsigned int desclen, void *after, 
			     u8 dtype, int iface, int altsetting)
{
	u8 *p, *end, *next;
	int ifc = -1, as = -1;

	p = descstart;
	end = p + desclen;
	for (; p < end;) {
		if (p[0] < 2)
			return NULL;
		next = p + p[0];
		if (next > end)
			return NULL;
		if (p[1] == USB_DT_INTERFACE) {
			/* minimum length of interface descriptor */
			if (p[0] < 9)
				return NULL;
			ifc = p[2];
			as = p[3];
		}
		if (p[1] == dtype && (!after || (void *)p > after) &&
		    (iface == -1 || iface == ifc) && (altsetting == -1 || altsetting == as)) {
			return p;
		}
		p = next;
	}
	return NULL;
}

static void *find_csinterface_descriptor(void *descstart, unsigned int desclen, void *after, u8 dsubtype, int iface, int altsetting)
{
	unsigned char *p;

	p = find_descriptor(descstart, desclen, after, USB_DT_CS_INTERFACE, iface, altsetting);
	while (p) {
		if (p[0] >= 3 && p[2] == dsubtype)
			return p;
		p = find_descriptor(descstart, desclen, p, USB_DT_CS_INTERFACE, iface, altsetting);
	}
	return NULL;
}

static void *find_audiocontrol_unit(void *descstart, unsigned int desclen, void *after, u8 unit, int iface)
{
	unsigned char *p;

	p = find_descriptor(descstart, desclen, after, USB_DT_CS_INTERFACE, iface, -1);
	while (p) {
		if (p[0] >= 4 && p[2] >= INPUT_TERMINAL && p[2] <= EXTENSION_UNIT && p[3] == unit)
			return p;
		p = find_descriptor(descstart, desclen, p, USB_DT_CS_INTERFACE, iface, -1);
	}
	return NULL;
}

static void usb_audio_parsestreaming(struct usb_audio_state *s, unsigned char *buffer, unsigned int buflen, int asifin, int asifout)
{
	struct usb_device *dev = s->usbdev;
	struct usb_audiodev *as;
	struct usb_host_interface *alts;
	struct usb_interface *iface;
	struct audioformat *fp;
	unsigned char *fmt, *csep;
	unsigned int i, j, k, format, idx;

	if (!(as = kmalloc(sizeof(struct usb_audiodev), GFP_KERNEL)))
		return;
	memset(as, 0, sizeof(struct usb_audiodev));
	init_waitqueue_head(&as->usbin.dma.wait);
	init_waitqueue_head(&as->usbout.dma.wait);
	spin_lock_init(&as->lock);
	as->usbin.durb[0].urb = usb_alloc_urb (DESCFRAMES, GFP_KERNEL);
	as->usbin.durb[1].urb = usb_alloc_urb (DESCFRAMES, GFP_KERNEL);
	as->usbin.surb[0].urb = usb_alloc_urb (SYNCFRAMES, GFP_KERNEL);
	as->usbin.surb[1].urb = usb_alloc_urb (SYNCFRAMES, GFP_KERNEL);
	as->usbout.durb[0].urb = usb_alloc_urb (DESCFRAMES, GFP_KERNEL);
	as->usbout.durb[1].urb = usb_alloc_urb (DESCFRAMES, GFP_KERNEL);
	as->usbout.surb[0].urb = usb_alloc_urb (SYNCFRAMES, GFP_KERNEL);
	as->usbout.surb[1].urb = usb_alloc_urb (SYNCFRAMES, GFP_KERNEL);
	if ((!as->usbin.durb[0].urb) ||
	    (!as->usbin.durb[1].urb) ||
	    (!as->usbin.surb[0].urb) ||
	    (!as->usbin.surb[1].urb) ||
	    (!as->usbout.durb[0].urb) ||
	    (!as->usbout.durb[1].urb) ||
	    (!as->usbout.surb[0].urb) ||
	    (!as->usbout.surb[1].urb)) {
		usb_free_urb(as->usbin.durb[0].urb);
		usb_free_urb(as->usbin.durb[1].urb);
		usb_free_urb(as->usbin.surb[0].urb);
		usb_free_urb(as->usbin.surb[1].urb);
		usb_free_urb(as->usbout.durb[0].urb);
		usb_free_urb(as->usbout.durb[1].urb);
		usb_free_urb(as->usbout.surb[0].urb);
		usb_free_urb(as->usbout.surb[1].urb);
		kfree(as);
		return;
	}
	as->state = s;
	as->usbin.interface = asifin;
	as->usbout.interface = asifout;
	/* search for input formats */
	if (asifin >= 0) {
		as->usbin.flags = FLG_CONNECTED;
		iface = usb_ifnum_to_if(dev, asifin);
		for (idx = 0; idx < iface->num_altsetting; idx++) {
			alts = &iface->altsetting[idx];
			i = alts->desc.bAlternateSetting;
			if (alts->desc.bInterfaceClass != USB_CLASS_AUDIO || alts->desc.bInterfaceSubClass != 2)
				continue;
			if (alts->desc.bNumEndpoints < 1) {
				if (i != 0) {  /* altsetting 0 has no endpoints (Section B.3.4.1) */
					printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u does not have an endpoint\n", 
					       dev->devnum, asifin, i);
				}
				continue;
			}
			if ((alts->endpoint[0].desc.bmAttributes & 0x03) != 0x01 ||
			    !(alts->endpoint[0].desc.bEndpointAddress & 0x80)) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u first endpoint not isochronous in\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			fmt = find_csinterface_descriptor(buffer, buflen, NULL, AS_GENERAL, asifin, i);
			if (!fmt) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not found\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			if (fmt[0] < 7 || fmt[6] != 0 || (fmt[5] != 1 && fmt[5] != 2)) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u format not supported\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			format = (fmt[5] == 2) ? (AFMT_U16_LE | AFMT_U8) : (AFMT_S16_LE | AFMT_S8);
			fmt = find_csinterface_descriptor(buffer, buflen, NULL, FORMAT_TYPE, asifin, i);
			if (!fmt) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not found\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			if (fmt[0] < 8+3*(fmt[7] ? fmt[7] : 2) || fmt[3] != 1) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not supported\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			if (fmt[4] < 1 || fmt[4] > 2 || fmt[5] < 1 || fmt[5] > 2) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u unsupported channels %u framesize %u\n", 
				       dev->devnum, asifin, i, fmt[4], fmt[5]);
				continue;
			}
			csep = find_descriptor(buffer, buflen, NULL, USB_DT_CS_ENDPOINT, asifin, i);
			if (!csep || csep[0] < 7 || csep[2] != EP_GENERAL) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u no or invalid class specific endpoint descriptor\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			if (as->numfmtin >= MAXFORMATS)
				continue;
			fp = &as->fmtin[as->numfmtin++];
			if (fmt[5] == 2)
				format &= (AFMT_U16_LE | AFMT_S16_LE);
			else
				format &= (AFMT_U8 | AFMT_S8);
			if (fmt[4] == 2)
				format |= AFMT_STEREO;
			fp->format = format;
			fp->altsetting = i;
			fp->sratelo = fp->sratehi = fmt[8] | (fmt[9] << 8) | (fmt[10] << 16);
			printk(KERN_INFO "usbaudio: valid input sample rate %u\n", fp->sratelo);
			for (j = fmt[7] ? (fmt[7]-1) : 1; j > 0; j--) {
				k = fmt[8+3*j] | (fmt[9+3*j] << 8) | (fmt[10+3*j] << 16);
				printk(KERN_INFO "usbaudio: valid input sample rate %u\n", k);
				if (k > fp->sratehi)
					fp->sratehi = k;
				if (k < fp->sratelo)
					fp->sratelo = k;
			}
			fp->attributes = csep[3];
			printk(KERN_INFO "usbaudio: device %u interface %u altsetting %u: format 0x%08x sratelo %u sratehi %u attributes 0x%02x\n", 
			       dev->devnum, asifin, i, fp->format, fp->sratelo, fp->sratehi, fp->attributes);
		}
	}
	/* search for output formats */
	if (asifout >= 0) {
		as->usbout.flags = FLG_CONNECTED;
		iface = usb_ifnum_to_if(dev, asifout);
		for (idx = 0; idx < iface->num_altsetting; idx++) {
			alts = &iface->altsetting[idx];
			i = alts->desc.bAlternateSetting;
			if (alts->desc.bInterfaceClass != USB_CLASS_AUDIO || alts->desc.bInterfaceSubClass != 2)
				continue;
			if (alts->desc.bNumEndpoints < 1) {
				/* altsetting 0 should never have iso EPs */
				if (i != 0)
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u does not have an endpoint\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			if ((alts->endpoint[0].desc.bmAttributes & 0x03) != 0x01 ||
			    (alts->endpoint[0].desc.bEndpointAddress & 0x80)) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u first endpoint not isochronous out\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			/* See USB audio formats manual, section 2 */
			fmt = find_csinterface_descriptor(buffer, buflen, NULL, AS_GENERAL, asifout, i);
			if (!fmt) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not found\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			if (fmt[0] < 7 || fmt[6] != 0 || (fmt[5] != 1 && fmt[5] != 2)) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u format not supported\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			format = (fmt[5] == 2) ? (AFMT_U16_LE | AFMT_U8) : (AFMT_S16_LE | AFMT_S8);
			/* Dallas DS4201 workaround */
			if (le16_to_cpu(dev->descriptor.idVendor) == 0x04fa && 
			    le16_to_cpu(dev->descriptor.idProduct) == 0x4201)
				format = (AFMT_S16_LE | AFMT_S8);
			fmt = find_csinterface_descriptor(buffer, buflen, NULL, FORMAT_TYPE, asifout, i);
			if (!fmt) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not found\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			if (fmt[0] < 8+3*(fmt[7] ? fmt[7] : 2) || fmt[3] != 1) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not supported\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			if (fmt[4] < 1 || fmt[4] > 2 || fmt[5] < 1 || fmt[5] > 2) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u unsupported channels %u framesize %u\n", 
				       dev->devnum, asifout, i, fmt[4], fmt[5]);
				continue;
			}
			csep = find_descriptor(buffer, buflen, NULL, USB_DT_CS_ENDPOINT, asifout, i);
			if (!csep || csep[0] < 7 || csep[2] != EP_GENERAL) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u no or invalid class specific endpoint descriptor\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			if (as->numfmtout >= MAXFORMATS)
				continue;
			fp = &as->fmtout[as->numfmtout++];
			if (fmt[5] == 2)
				format &= (AFMT_U16_LE | AFMT_S16_LE);
			else
				format &= (AFMT_U8 | AFMT_S8);
			if (fmt[4] == 2)
				format |= AFMT_STEREO;
			fp->format = format;
			fp->altsetting = i;
			fp->sratelo = fp->sratehi = fmt[8] | (fmt[9] << 8) | (fmt[10] << 16);
			printk(KERN_INFO "usbaudio: valid output sample rate %u\n", fp->sratelo);
			for (j = fmt[7] ? (fmt[7]-1) : 1; j > 0; j--) {
				k = fmt[8+3*j] | (fmt[9+3*j] << 8) | (fmt[10+3*j] << 16);
				printk(KERN_INFO "usbaudio: valid output sample rate %u\n", k);
				if (k > fp->sratehi)
					fp->sratehi = k;
				if (k < fp->sratelo)
					fp->sratelo = k;
			}
			fp->attributes = csep[3];
			printk(KERN_INFO "usbaudio: device %u interface %u altsetting %u: format 0x%08x sratelo %u sratehi %u attributes 0x%02x\n", 
			       dev->devnum, asifout, i, fp->format, fp->sratelo, fp->sratehi, fp->attributes);
		}
	}
	if (as->numfmtin == 0 && as->numfmtout == 0) {
		usb_free_urb(as->usbin.durb[0].urb);
		usb_free_urb(as->usbin.durb[1].urb);
		usb_free_urb(as->usbin.surb[0].urb);
		usb_free_urb(as->usbin.surb[1].urb);
		usb_free_urb(as->usbout.durb[0].urb);
		usb_free_urb(as->usbout.durb[1].urb);
		usb_free_urb(as->usbout.surb[0].urb);
		usb_free_urb(as->usbout.surb[1].urb);
		kfree(as);
		return;
	}
	if ((as->dev_audio = register_sound_dsp(&usb_audio_fops, -1)) < 0) {
		printk(KERN_ERR "usbaudio: cannot register dsp\n");
		usb_free_urb(as->usbin.durb[0].urb);
		usb_free_urb(as->usbin.durb[1].urb);
		usb_free_urb(as->usbin.surb[0].urb);
		usb_free_urb(as->usbin.surb[1].urb);
		usb_free_urb(as->usbout.durb[0].urb);
		usb_free_urb(as->usbout.durb[1].urb);
		usb_free_urb(as->usbout.surb[0].urb);
		usb_free_urb(as->usbout.surb[1].urb);
		kfree(as);
		return;
	}
	printk(KERN_INFO "usbaudio: registered dsp 14,%d\n", as->dev_audio);
	/* everything successful */
	list_add_tail(&as->list, &s->audiolist);
}

struct consmixstate {
	struct usb_audio_state *s;
	unsigned char *buffer;
	unsigned int buflen;
	unsigned int ctrlif;
	struct mixerchannel mixch[SOUND_MIXER_NRDEVICES];
	unsigned int nrmixch;
	unsigned int mixchmask;
	unsigned long unitbitmap[32/sizeof(unsigned long)];
	/* return values */
	unsigned int nrchannels;
	unsigned int termtype;
	unsigned int chconfig;
};

static struct mixerchannel *getmixchannel(struct consmixstate *state, unsigned int nr)
{
	struct mixerchannel *c;

	if (nr >= SOUND_MIXER_NRDEVICES) {
		printk(KERN_ERR "usbaudio: invalid OSS mixer channel %u\n", nr);
		return NULL;
	}
	if (!(state->mixchmask & (1 << nr))) {
		printk(KERN_WARNING "usbaudio: OSS mixer channel %u already in use\n", nr);
		return NULL;
	}
	c = &state->mixch[state->nrmixch++];
	c->osschannel = nr;
	state->mixchmask &= ~(1 << nr);
	return c;
}

static unsigned int getvolchannel(struct consmixstate *state)
{
	unsigned int u;

	if ((state->termtype & 0xff00) == 0x0000 && (state->mixchmask & SOUND_MASK_VOLUME))
		return SOUND_MIXER_VOLUME;
	if ((state->termtype & 0xff00) == 0x0100) {
		if (state->mixchmask & SOUND_MASK_PCM)
			return SOUND_MIXER_PCM;
		if (state->mixchmask & SOUND_MASK_ALTPCM)
			return SOUND_MIXER_ALTPCM;
	}
	if ((state->termtype & 0xff00) == 0x0200 && (state->mixchmask & SOUND_MASK_MIC))
		return SOUND_MIXER_MIC;
	if ((state->termtype & 0xff00) == 0x0300 && (state->mixchmask & SOUND_MASK_SPEAKER))
		return SOUND_MIXER_SPEAKER;
	if ((state->termtype & 0xff00) == 0x0500) {
		if (state->mixchmask & SOUND_MASK_PHONEIN)
			return SOUND_MIXER_PHONEIN;
		if (state->mixchmask & SOUND_MASK_PHONEOUT)
			return SOUND_MIXER_PHONEOUT;
	}
	if (state->termtype >= 0x710 && state->termtype <= 0x711 && (state->mixchmask & SOUND_MASK_RADIO))
		return SOUND_MIXER_RADIO;
	if (state->termtype >= 0x709 && state->termtype <= 0x70f && (state->mixchmask & SOUND_MASK_VIDEO))
		return SOUND_MIXER_VIDEO;
	u = ffs(state->mixchmask & (SOUND_MASK_LINE | SOUND_MASK_CD | SOUND_MASK_LINE1 | SOUND_MASK_LINE2 | SOUND_MASK_LINE3 |
				    SOUND_MASK_DIGITAL1 | SOUND_MASK_DIGITAL2 | SOUND_MASK_DIGITAL3));
	return u-1;
}

static void prepmixch(struct consmixstate *state)
{
	struct usb_device *dev = state->s->usbdev;
	struct mixerchannel *ch;
	unsigned char *buf;
	__s16 v1;
	unsigned int v2, v3;

	if (!state->nrmixch || state->nrmixch > SOUND_MIXER_NRDEVICES)
		return;
	buf = kmalloc(sizeof(*buf) * 2, GFP_KERNEL);
	if (!buf) {
		printk(KERN_ERR "prepmixch: out of memory\n") ;
		return;
	}

	ch = &state->mixch[state->nrmixch-1];
	switch (ch->selector) {
	case 0:  /* mixer unit request */
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MIN, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->chnum << 8) | 1, state->ctrlif | (ch->unitid << 8), buf, 2, 1000) < 0)
			goto err;
		ch->minval = buf[0] | (buf[1] << 8);
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MAX, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->chnum << 8) | 1, state->ctrlif | (ch->unitid << 8), buf, 2, 1000) < 0)
			goto err;
		ch->maxval = buf[0] | (buf[1] << 8);
		v2 = ch->maxval - ch->minval;
		if (!v2)
			v2 = 1;
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->chnum << 8) | 1, state->ctrlif | (ch->unitid << 8), buf, 2, 1000) < 0)
			goto err;
		v1 = buf[0] | (buf[1] << 8);
		v3 = v1 - ch->minval;
		v3 = 100 * v3 / v2;
		if (v3 > 100)
			v3 = 100;
		ch->value = v3;
		if (ch->flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)) {
			if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
					    ((ch->chnum + !!(ch->flags & MIXFLG_STEREOIN)) << 8) | (1 + !!(ch->flags & MIXFLG_STEREOOUT)),
					    state->ctrlif | (ch->unitid << 8), buf, 2, 1000) < 0)
			goto err;
			v1 = buf[0] | (buf[1] << 8);
			v3 = v1 - ch->minval;
			v3 = 100 * v3 / v2;
			if (v3 > 100)
				v3 = 100;
		}
		ch->value |= v3 << 8;
		break;

		/* various feature unit controls */
	case VOLUME_CONTROL:
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MIN, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 2, 1000) < 0)
			goto err;
		ch->minval = buf[0] | (buf[1] << 8);
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MAX, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 2, 1000) < 0)
			goto err;
		ch->maxval = buf[0] | (buf[1] << 8);
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 2, 1000) < 0)
			goto err;
		v1 = buf[0] | (buf[1] << 8);
		v2 = ch->maxval - ch->minval;
		v3 = v1 - ch->minval;
		if (!v2)
			v2 = 1;
		v3 = 100 * v3 / v2;
		if (v3 > 100)
			v3 = 100;
		ch->value = v3;
		if (ch->flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)) {
			if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
					    (ch->selector << 8) | (ch->chnum + 1), state->ctrlif | (ch->unitid << 8), buf, 2, 1000) < 0)
				goto err;
			v1 = buf[0] | (buf[1] << 8);
			v3 = v1 - ch->minval;
			v3 = 100 * v3 / v2;
			if (v3 > 100)
				v3 = 100;
		}
		ch->value |= v3 << 8;
		break;
		
	case BASS_CONTROL:
	case MID_CONTROL:
	case TREBLE_CONTROL:
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MIN, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 1, 1000) < 0)
			goto err;
		ch->minval = buf[0] << 8;
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MAX, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 1, 1000) < 0)
			goto err;
		ch->maxval = buf[0] << 8;
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 1, 1000) < 0)
			goto err;
		v1 = buf[0] << 8;
		v2 = ch->maxval - ch->minval;
		v3 = v1 - ch->minval;
		if (!v2)
			v2 = 1;
		v3 = 100 * v3 / v2;
		if (v3 > 100)
			v3 = 100;
		ch->value = v3;
		if (ch->flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)) {
			if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
					    (ch->selector << 8) | (ch->chnum + 1), state->ctrlif | (ch->unitid << 8), buf, 1, 1000) < 0)
				goto err;
			v1 = buf[0] << 8;
			v3 = v1 - ch->minval;
			v3 = 100 * v3 / v2;
			if (v3 > 100)
				v3 = 100;
		}
		ch->value |= v3 << 8;
		break;
		
	default:
		goto err;
	}

 freebuf:
	kfree(buf);
	return;
 err:
	printk(KERN_ERR "usbaudio: mixer request device %u if %u unit %u ch %u selector %u failed\n", 
	       dev->devnum, state->ctrlif, ch->unitid, ch->chnum, ch->selector);
	if (state->nrmixch)
		state->nrmixch--;
	goto freebuf;
}


static void usb_audio_recurseunit(struct consmixstate *state, unsigned char unitid);

static inline int checkmixbmap(unsigned char *bmap, unsigned char flg, unsigned int inidx, unsigned int numoch)
{
	unsigned int idx;

	idx = inidx*numoch;
	if (!(bmap[-(idx >> 3)] & (0x80 >> (idx & 7))))
		return 0;
	if (!(flg & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)))
		return 1;
	idx = (inidx+!!(flg & MIXFLG_STEREOIN))*numoch+!!(flg & MIXFLG_STEREOOUT);
	if (!(bmap[-(idx >> 3)] & (0x80 >> (idx & 7))))
		return 0;
	return 1;
}

static void usb_audio_mixerunit(struct consmixstate *state, unsigned char *mixer)
{
	unsigned int nroutch = mixer[5+mixer[4]];
	unsigned int chidx[SOUND_MIXER_NRDEVICES+1];
	unsigned int termt[SOUND_MIXER_NRDEVICES];
	unsigned char flg = (nroutch >= 2) ? MIXFLG_STEREOOUT : 0;
	unsigned char *bmap = &mixer[9+mixer[4]];
	unsigned int bmapsize;
	struct mixerchannel *ch;
	unsigned int i;

	if (!mixer[4]) {
		printk(KERN_ERR "usbaudio: unit %u invalid MIXER_UNIT descriptor\n", mixer[3]);
		return;
	}
	if (mixer[4] > SOUND_MIXER_NRDEVICES) {
		printk(KERN_ERR "usbaudio: mixer unit %u: too many input pins\n", mixer[3]);
		return;
	}
	chidx[0] = 0;
	for (i = 0; i < mixer[4]; i++) {
		usb_audio_recurseunit(state, mixer[5+i]);
		chidx[i+1] = chidx[i] + state->nrchannels;
		termt[i] = state->termtype;
	}
	state->termtype = 0;
	state->chconfig = mixer[6+mixer[4]] | (mixer[7+mixer[4]] << 8);
	bmapsize = (nroutch * chidx[mixer[4]] + 7) >> 3;
	bmap += bmapsize - 1;
	if (mixer[0] < 10+mixer[4]+bmapsize) {
		printk(KERN_ERR "usbaudio: unit %u invalid MIXER_UNIT descriptor (bitmap too small)\n", mixer[3]);
		return;
	}
	for (i = 0; i < mixer[4]; i++) {
		state->termtype = termt[i];
		if (chidx[i+1]-chidx[i] >= 2) {
			flg |= MIXFLG_STEREOIN;
			if (checkmixbmap(bmap, flg, chidx[i], nroutch)) {
				ch = getmixchannel(state, getvolchannel(state));
				if (ch) {
					ch->unitid = mixer[3];
					ch->selector = 0;
					ch->chnum = chidx[i]+1;
					ch->flags = flg;
					prepmixch(state);
				}
				continue;
			}
		}
		flg &= ~MIXFLG_STEREOIN;
		if (checkmixbmap(bmap, flg, chidx[i], nroutch)) {
			ch = getmixchannel(state, getvolchannel(state));
			if (ch) {
				ch->unitid = mixer[3];
				ch->selector = 0;
				ch->chnum = chidx[i]+1;
				ch->flags = flg;
				prepmixch(state);
			}
		}
	}	
	state->termtype = 0;
}

static struct mixerchannel *slctsrc_findunit(struct consmixstate *state, __u8 unitid)
{
	unsigned int i;
	
	for (i = 0; i < state->nrmixch; i++)
		if (state->mixch[i].unitid == unitid)
			return &state->mixch[i];
	return NULL;
}

static void usb_audio_selectorunit(struct consmixstate *state, unsigned char *selector)
{
	unsigned int chnum, i, mixch;
	struct mixerchannel *mch;

	if (!selector[4]) {
		printk(KERN_ERR "usbaudio: unit %u invalid SELECTOR_UNIT descriptor\n", selector[3]);
		return;
	}
	mixch = state->nrmixch;
	usb_audio_recurseunit(state, selector[5]);
	if (state->nrmixch != mixch) {
		mch = &state->mixch[state->nrmixch-1];
		mch->slctunitid = selector[3] | (1 << 8);
	} else if ((mch = slctsrc_findunit(state, selector[5]))) {
		mch->slctunitid = selector[3] | (1 << 8);
	} else {
		printk(KERN_INFO "usbaudio: selector unit %u: ignoring channel 1\n", selector[3]);
	}
	chnum = state->nrchannels;
	for (i = 1; i < selector[4]; i++) {
		mixch = state->nrmixch;
		usb_audio_recurseunit(state, selector[5+i]);
		if (chnum != state->nrchannels) {
			printk(KERN_ERR "usbaudio: selector unit %u: input pins with varying channel numbers\n", selector[3]);
			state->termtype = 0;
			state->chconfig = 0;
			state->nrchannels = 0;
			return;
		}
		if (state->nrmixch != mixch) {
			mch = &state->mixch[state->nrmixch-1];
			mch->slctunitid = selector[3] | ((i + 1) << 8);
		} else if ((mch = slctsrc_findunit(state, selector[5+i]))) {
			mch->slctunitid = selector[3] | ((i + 1) << 8);
		} else {
			printk(KERN_INFO "usbaudio: selector unit %u: ignoring channel %u\n", selector[3], i+1);
		}
	}
	state->termtype = 0;
	state->chconfig = 0;
}

/* in the future we might try to handle 3D etc. effect units */

static void usb_audio_processingunit(struct consmixstate *state, unsigned char *proc)
{
	unsigned int i;

	for (i = 0; i < proc[6]; i++)
		usb_audio_recurseunit(state, proc[7+i]);
	state->nrchannels = proc[7+proc[6]];
	state->termtype = 0;
	state->chconfig = proc[8+proc[6]] | (proc[9+proc[6]] << 8);
}


/* See Audio Class Spec, section 4.3.2.5 */
static void usb_audio_featureunit(struct consmixstate *state, unsigned char *ftr)
{
	struct mixerchannel *ch;
	unsigned short chftr, mchftr;
#if 0
	struct usb_device *dev = state->s->usbdev;
	unsigned char data[1];
#endif
	unsigned char nr_logical_channels, i;

	usb_audio_recurseunit(state, ftr[4]);

	if (ftr[5] == 0 ) {
		printk(KERN_ERR "usbaudio: wrong controls size in feature unit %u\n",ftr[3]);
		return;
	}

	if (state->nrchannels == 0) {
		printk(KERN_ERR "usbaudio: feature unit %u source has no channels\n", ftr[3]);
		return;
	}
	if (state->nrchannels > 2)
		printk(KERN_WARNING "usbaudio: feature unit %u: OSS mixer interface does not support more than 2 channels\n", ftr[3]);

	nr_logical_channels=(ftr[0]-7)/ftr[5]-1;

	if (nr_logical_channels != state->nrchannels) {
		printk(KERN_WARNING "usbaudio: warning: found %d of %d logical channels.\n", state->nrchannels,nr_logical_channels);

		if (state->nrchannels == 1 && nr_logical_channels==0) {
			printk(KERN_INFO "usbaudio: assuming the channel found is the master channel (got a Philips camera?). Should be fine.\n");
		} else if (state->nrchannels == 1 && nr_logical_channels==2) {
			printk(KERN_INFO "usbaudio: assuming that a stereo channel connected directly to a mixer is missing in search (got Labtec headset?). Should be fine.\n");
			state->nrchannels=nr_logical_channels;
		} else {
			printk(KERN_WARNING "usbaudio: no idea what's going on..., contact linux-usb-devel@lists.sourceforge.net\n");
		}
	}

	/* There is always a master channel */
	mchftr = ftr[6];
	/* Binary AND over logical channels if they exist */
	if (nr_logical_channels) {
		chftr = ftr[6+ftr[5]];
		for (i = 2; i <= nr_logical_channels; i++)
			chftr &= ftr[6+i*ftr[5]];
	} else {
		chftr = 0;
	}

	/* volume control */
	if (chftr & 2) {
		ch = getmixchannel(state, getvolchannel(state));
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = VOLUME_CONTROL;
			ch->chnum = 1;
			ch->flags = (state->nrchannels > 1) ? (MIXFLG_STEREOIN | MIXFLG_STEREOOUT) : 0;
			prepmixch(state);
		}
	} else if (mchftr & 2) {
		ch = getmixchannel(state, getvolchannel(state));
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = VOLUME_CONTROL;
			ch->chnum = 0;
			ch->flags = 0;
			prepmixch(state);
		}
	}
	/* bass control */
	if (chftr & 4) {
		ch = getmixchannel(state, SOUND_MIXER_BASS);
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = BASS_CONTROL;
			ch->chnum = 1;
			ch->flags = (state->nrchannels > 1) ? (MIXFLG_STEREOIN | MIXFLG_STEREOOUT) : 0;
			prepmixch(state);
		}
	} else if (mchftr & 4) {
		ch = getmixchannel(state, SOUND_MIXER_BASS);
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = BASS_CONTROL;
			ch->chnum = 0;
			ch->flags = 0;
			prepmixch(state);
		}
	}
	/* treble control */
	if (chftr & 16) {
		ch = getmixchannel(state, SOUND_MIXER_TREBLE);
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = TREBLE_CONTROL;
			ch->chnum = 1;
			ch->flags = (state->nrchannels > 1) ? (MIXFLG_STEREOIN | MIXFLG_STEREOOUT) : 0;
			prepmixch(state);
		}
	} else if (mchftr & 16) {
		ch = getmixchannel(state, SOUND_MIXER_TREBLE);
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = TREBLE_CONTROL;
			ch->chnum = 0;
			ch->flags = 0;
			prepmixch(state);
		}
	}
#if 0
	/* if there are mute controls, unmute them */
	/* does not seem to be necessary, and the Dallas chip does not seem to support the "all" channel (255) */
	if ((chftr & 1) || (mchftr & 1)) {
		printk(KERN_DEBUG "usbaudio: unmuting feature unit %u interface %u\n", ftr[3], state->ctrlif);
		data[0] = 0;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    (MUTE_CONTROL << 8) | 0xff, state->ctrlif | (ftr[3] << 8), data, 1, 1000) < 0)
			printk(KERN_WARNING "usbaudio: failure to unmute feature unit %u interface %u\n", ftr[3], state->ctrlif);
 	}
#endif
}

static void usb_audio_recurseunit(struct consmixstate *state, unsigned char unitid)
{
	unsigned char *p1;
	unsigned int i, j;

	if (test_and_set_bit(unitid, state->unitbitmap)) {
		printk(KERN_INFO "usbaudio: mixer path revisits unit %d\n", unitid);
		return;
	}
	p1 = find_audiocontrol_unit(state->buffer, state->buflen, NULL, unitid, state->ctrlif);
	if (!p1) {
		printk(KERN_ERR "usbaudio: unit %d not found!\n", unitid);
		return;
	}
	state->nrchannels = 0;
	state->termtype = 0;
	state->chconfig = 0;
	switch (p1[2]) {
	case INPUT_TERMINAL:
		if (p1[0] < 12) {
			printk(KERN_ERR "usbaudio: unit %u: invalid INPUT_TERMINAL descriptor\n", unitid);
			return;
		}
		state->nrchannels = p1[7];
		state->termtype = p1[4] | (p1[5] << 8);
		state->chconfig = p1[8] | (p1[9] << 8);
		return;

	case MIXER_UNIT:
		if (p1[0] < 10 || p1[0] < 10+p1[4]) {
			printk(KERN_ERR "usbaudio: unit %u: invalid MIXER_UNIT descriptor\n", unitid);
			return;
		}
		usb_audio_mixerunit(state, p1);
		return;

	case SELECTOR_UNIT:
		if (p1[0] < 6 || p1[0] < 6+p1[4]) {
			printk(KERN_ERR "usbaudio: unit %u: invalid SELECTOR_UNIT descriptor\n", unitid);
			return;
		}
		usb_audio_selectorunit(state, p1);
		return;

	case FEATURE_UNIT: /* See USB Audio Class Spec 4.3.2.5 */
		if (p1[0] < 7 || p1[0] < 7+p1[5]) {
			printk(KERN_ERR "usbaudio: unit %u: invalid FEATURE_UNIT descriptor\n", unitid);
			return;
		}
		usb_audio_featureunit(state, p1);
		return;		

	case PROCESSING_UNIT:
		if (p1[0] < 13 || p1[0] < 13+p1[6] || p1[0] < 13+p1[6]+p1[11+p1[6]]) {
			printk(KERN_ERR "usbaudio: unit %u: invalid PROCESSING_UNIT descriptor\n", unitid);
			return;
		}
		usb_audio_processingunit(state, p1);
		return;		

	case EXTENSION_UNIT:
		if (p1[0] < 13 || p1[0] < 13+p1[6] || p1[0] < 13+p1[6]+p1[11+p1[6]]) {
			printk(KERN_ERR "usbaudio: unit %u: invalid EXTENSION_UNIT descriptor\n", unitid);
			return;
		}
		for (j = i = 0; i < p1[6]; i++) {
			usb_audio_recurseunit(state, p1[7+i]);
			if (!i)
				j = state->termtype;
			else if (j != state->termtype)
				j = 0;
		}
		state->nrchannels = p1[7+p1[6]];
		state->chconfig = p1[8+p1[6]] | (p1[9+p1[6]] << 8);
		state->termtype = j;
		return;

	default:
		printk(KERN_ERR "usbaudio: unit %u: unexpected type 0x%02x\n", unitid, p1[2]);
		return;
	}
}

static void usb_audio_constructmixer(struct usb_audio_state *s, unsigned char *buffer, unsigned int buflen, unsigned int ctrlif, unsigned char *oterm)
{
	struct usb_mixerdev *ms;
	struct consmixstate state;

	memset(&state, 0, sizeof(state));
	state.s = s;
	state.nrmixch = 0;
	state.mixchmask = ~0;
	state.buffer = buffer;
	state.buflen = buflen;
	state.ctrlif = ctrlif;
	set_bit(oterm[3], state.unitbitmap);  /* mark terminal ID as visited */
	printk(KERN_DEBUG "usbaudio: constructing mixer for Terminal %u type 0x%04x\n",
	       oterm[3], oterm[4] | (oterm[5] << 8));
	usb_audio_recurseunit(&state, oterm[7]);
	if (!state.nrmixch) {
		printk(KERN_INFO "usbaudio: no mixer controls found for Terminal %u\n", oterm[3]);
		return;
	}
	if (!(ms = kmalloc(sizeof(struct usb_mixerdev)+state.nrmixch*sizeof(struct mixerchannel), GFP_KERNEL)))
		return;
	memset(ms, 0, sizeof(struct usb_mixerdev));
	memcpy(&ms->ch, &state.mixch, state.nrmixch*sizeof(struct mixerchannel));
	ms->state = s;
	ms->iface = ctrlif;
	ms->numch = state.nrmixch;
	if ((ms->dev_mixer = register_sound_mixer(&usb_mixer_fops, -1)) < 0) {
		printk(KERN_ERR "usbaudio: cannot register mixer\n");
		kfree(ms);
		return;
	}
	printk(KERN_INFO "usbaudio: registered mixer 14,%d\n", ms->dev_mixer);
	list_add_tail(&ms->list, &s->mixerlist);
}

/* arbitrary limit, we won't check more interfaces than this */
#define USB_MAXINTERFACES	32

static struct usb_audio_state *usb_audio_parsecontrol(struct usb_device *dev, unsigned char *buffer, unsigned int buflen, unsigned int ctrlif)
{
	struct usb_audio_state *s;
	struct usb_interface *iface;
	struct usb_host_interface *alt;
	unsigned char ifin[USB_MAXINTERFACES], ifout[USB_MAXINTERFACES];
	unsigned char *p1;
	unsigned int i, j, k, numifin = 0, numifout = 0;
	
	if (!(s = kmalloc(sizeof(struct usb_audio_state), GFP_KERNEL)))
		return NULL;
	memset(s, 0, sizeof(struct usb_audio_state));
	INIT_LIST_HEAD(&s->audiolist);
	INIT_LIST_HEAD(&s->mixerlist);
	s->usbdev = dev;
	s->count = 1;

	/* find audiocontrol interface */
	if (!(p1 = find_csinterface_descriptor(buffer, buflen, NULL, HEADER, ctrlif, -1))) {
		printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u no HEADER found\n",
		       dev->devnum, ctrlif);
		goto ret;
	}
	if (p1[0] < 8 + p1[7]) {
		printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u HEADER error\n",
		       dev->devnum, ctrlif);
		goto ret;
	}
	if (!p1[7])
		printk(KERN_INFO "usbaudio: device %d audiocontrol interface %u has no AudioStreaming and MidiStreaming interfaces\n",
		       dev->devnum, ctrlif);
	for (i = 0; i < p1[7]; i++) {
		j = p1[8+i];
		iface = usb_ifnum_to_if(dev, j);
		if (!iface) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u interface %u does not exist\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		if (iface->num_altsetting == 1) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u has only 1 altsetting.\n", dev->devnum, ctrlif);
			continue;
		}
		alt = usb_altnum_to_altsetting(iface, 0);
		if (!alt) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u interface %u has no altsetting 0\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		if (alt->desc.bInterfaceClass != USB_CLASS_AUDIO) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u interface %u is not an AudioClass interface\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		if (alt->desc.bInterfaceSubClass == 3) {
			printk(KERN_INFO "usbaudio: device %d audiocontrol interface %u interface %u MIDIStreaming not supported\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		if (alt->desc.bInterfaceSubClass != 2) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u interface %u invalid AudioClass subtype\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		if (alt->desc.bNumEndpoints > 0) {
			/* Check all endpoints; should they all have a bandwidth of 0 ? */
			for (k = 0; k < alt->desc.bNumEndpoints; k++) {
				if (le16_to_cpu(alt->endpoint[k].desc.wMaxPacketSize) > 0) {
					printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u endpoint %d does not have 0 bandwidth at alt[0]\n", dev->devnum, ctrlif, k);
					break;
				}
			}
			if (k < alt->desc.bNumEndpoints)
				continue;
		}

		alt = usb_altnum_to_altsetting(iface, 1);
		if (!alt) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u interface %u has no altsetting 1\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		if (alt->desc.bNumEndpoints < 1) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u interface %u has no endpoint\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		/* note: this requires the data endpoint to be ep0 and the optional sync
		   ep to be ep1, which seems to be the case */
		if (alt->endpoint[0].desc.bEndpointAddress & USB_DIR_IN) {
			if (numifin < USB_MAXINTERFACES) {
				ifin[numifin++] = j;
				usb_driver_claim_interface(&usb_audio_driver, iface, (void *)-1);
			}
		} else {
			if (numifout < USB_MAXINTERFACES) {
				ifout[numifout++] = j;
				usb_driver_claim_interface(&usb_audio_driver, iface, (void *)-1);
			}
		}
	}
	printk(KERN_INFO "usbaudio: device %d audiocontrol interface %u has %u input and %u output AudioStreaming interfaces\n",
	       dev->devnum, ctrlif, numifin, numifout);
	for (i = 0; i < numifin && i < numifout; i++)
		usb_audio_parsestreaming(s, buffer, buflen, ifin[i], ifout[i]);
	for (j = i; j < numifin; j++)
		usb_audio_parsestreaming(s, buffer, buflen, ifin[i], -1);
	for (j = i; j < numifout; j++)
		usb_audio_parsestreaming(s, buffer, buflen, -1, ifout[i]);
	/* now walk through all OUTPUT_TERMINAL descriptors to search for mixers */
	p1 = find_csinterface_descriptor(buffer, buflen, NULL, OUTPUT_TERMINAL, ctrlif, -1);
	while (p1) {
		if (p1[0] >= 9)
			usb_audio_constructmixer(s, buffer, buflen, ctrlif, p1);
		p1 = find_csinterface_descriptor(buffer, buflen, p1, OUTPUT_TERMINAL, ctrlif, -1);
	}

ret:
	if (list_empty(&s->audiolist) && list_empty(&s->mixerlist)) {
		kfree(s);
		return NULL;
	}
	/* everything successful */
	down(&open_sem);
	list_add_tail(&s->audiodev, &audiodevs);
	up(&open_sem);
	printk(KERN_DEBUG "usb_audio_parsecontrol: usb_audio_state at %p\n", s);
	return s;
}

/* we only care for the currently active configuration */

static int usb_audio_probe(struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev (intf);
	struct usb_audio_state *s;
	unsigned char *buffer;
	unsigned int buflen;

#if 0
	printk(KERN_DEBUG "usbaudio: Probing if %i: IC %x, ISC %x\n", ifnum,
	       config->interface[ifnum].altsetting[0].desc.bInterfaceClass,
	       config->interface[ifnum].altsetting[0].desc.bInterfaceSubClass);
#endif

	/*
	 * audiocontrol interface found
	 * find which configuration number is active
	 */
	buffer = dev->rawdescriptors[dev->actconfig - dev->config];
	buflen = le16_to_cpu(dev->actconfig->desc.wTotalLength);
	s = usb_audio_parsecontrol(dev, buffer, buflen, intf->altsetting->desc.bInterfaceNumber);
	if (s) {
		usb_set_intfdata (intf, s);
		return 0;
	}
	return -ENODEV;
}


/* a revoke facility would make things simpler */

static void usb_audio_disconnect(struct usb_interface *intf)
{
	struct usb_audio_state *s = usb_get_intfdata (intf);
	struct usb_audiodev *as;
	struct usb_mixerdev *ms;

	if (!s)
		return;

	/* we get called with -1 for every audiostreaming interface registered */
	if (s == (struct usb_audio_state *)-1) {
		dprintk((KERN_DEBUG "usbaudio: note, usb_audio_disconnect called with -1\n"));
		return;
	}
	if (!s->usbdev) {
		dprintk((KERN_DEBUG "usbaudio: error,  usb_audio_disconnect already called for %p!\n", s));
		return;
	}
	down(&open_sem);
	list_del_init(&s->audiodev);
	s->usbdev = NULL;
	usb_set_intfdata (intf, NULL);

	/* deregister all audio and mixer devices, so no new processes can open this device */
	list_for_each_entry(as, &s->audiolist, list) {
		usbin_disc(as);
		usbout_disc(as);
		wake_up(&as->usbin.dma.wait);
		wake_up(&as->usbout.dma.wait);
		if (as->dev_audio >= 0) {
			unregister_sound_dsp(as->dev_audio);
			printk(KERN_INFO "usbaudio: unregister dsp 14,%d\n", as->dev_audio);
		}
		as->dev_audio = -1;
	}
	list_for_each_entry(ms, &s->mixerlist, list) {
		if (ms->dev_mixer >= 0) {
			unregister_sound_mixer(ms->dev_mixer);
			printk(KERN_INFO "usbaudio: unregister mixer 14,%d\n", ms->dev_mixer);
		}
		ms->dev_mixer = -1;
	}
	release(s);
	wake_up(&open_wait);
}

static int __init usb_audio_init(void)
{
	int result = usb_register(&usb_audio_driver);
	if (result == 0) 
		info(DRIVER_VERSION ":" DRIVER_DESC);
	return result;
}


static void __exit usb_audio_cleanup(void)
{
	usb_deregister(&usb_audio_driver);
}

module_init(usb_audio_init);
module_exit(usb_audio_cleanup);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

