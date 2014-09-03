/*
 *  tw68-core.c
 *  Core functions for the Techwell 68xx driver
 *
 *  Much of this code is derived from the cx88 and sa7134 drivers, which
 *  were in turn derived from the bt87x driver.  The original work was by
 *  Gerd Knorr; more recently the code was enhanced by Mauro Carvalho Chehab,
 *  Hans Verkuil, Andy Walls and many others.  Their work is gratefully
 *  acknowledged.  Full credit goes to them - any problems within this code
 *  are mine.
 *
 *  Copyright (C) 2009  William M. Brack <wbrack@mmm.com.hk>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/sound.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/dma-mapping.h>
#include <linux/pm.h>

#include <media/v4l2-dev.h>
#include "tw68.h"
#include "tw68-reg.h"

MODULE_DESCRIPTION("v4l2 driver module for tw6800 based video capture cards");
MODULE_AUTHOR("William M. Brack <wbrack@mmm.com.hk>");
MODULE_LICENSE("GPL");

static unsigned int core_debug;
module_param(core_debug, int, 0644);
MODULE_PARM_DESC(core_debug, "enable debug messages [core]");

static unsigned int gpio_tracking;
module_param(gpio_tracking, int, 0644);
MODULE_PARM_DESC(gpio_tracking, "enable debug messages [gpio]");

static unsigned int alsa = 1;
module_param(alsa, int, 0644);
MODULE_PARM_DESC(alsa, "enable/disable ALSA DMA sound [dmasound]");

static unsigned int latency = UNSET;
module_param(latency, int, 0444);
MODULE_PARM_DESC(latency, "pci latency timer");

static unsigned int nocomb;
module_param(nocomb, int, 0644);
MODULE_PARM_DESC(nocomb, "disable comb filter");

static unsigned int video_nr[] = {[0 ... (TW68_MAXBOARDS - 1)] = UNSET };
static unsigned int vbi_nr[]   = {[0 ... (TW68_MAXBOARDS - 1)] = UNSET };
static unsigned int radio_nr[] = {[0 ... (TW68_MAXBOARDS - 1)] = UNSET };
static unsigned int tuner[]    = {[0 ... (TW68_MAXBOARDS - 1)] = UNSET };
static unsigned int card[]     = {[0 ... (TW68_MAXBOARDS - 1)] = UNSET };

module_param_array(video_nr, int, NULL, 0444);
module_param_array(vbi_nr,   int, NULL, 0444);
module_param_array(radio_nr, int, NULL, 0444);
module_param_array(tuner,    int, NULL, 0444);
module_param_array(card,     int, NULL, 0444);

MODULE_PARM_DESC(video_nr, "video device number");
MODULE_PARM_DESC(vbi_nr,   "vbi device number");
MODULE_PARM_DESC(radio_nr, "radio device number");
MODULE_PARM_DESC(tuner,    "tuner type");
MODULE_PARM_DESC(card,     "card type");

LIST_HEAD(tw68_devlist);
EXPORT_SYMBOL(tw68_devlist);
DEFINE_MUTEX(tw68_devlist_lock);
EXPORT_SYMBOL(tw68_devlist_lock);
static LIST_HEAD(mops_list);
static unsigned int tw68_devcount;      /* curr tot num of devices present */

int (*tw68_dmasound_init)(struct tw68_dev *dev);
EXPORT_SYMBOL(tw68_dmasound_init);
int (*tw68_dmasound_exit)(struct tw68_dev *dev);
EXPORT_SYMBOL(tw68_dmasound_exit);

#define dprintk(level, fmt, arg...)      if (core_debug & (level)) \
	printk(KERN_DEBUG "%s: " fmt, dev->name , ## arg)

/* ------------------------------------------------------------------ */

void tw68_dma_free(struct videobuf_queue *q, struct tw68_buf *buf)
{
	struct videobuf_dmabuf *dma = videobuf_to_dma(&buf->vb);
	
	if (core_debug & DBG_FLOW)
		printk(KERN_DEBUG "%s: called\n", __func__);
	BUG_ON(in_interrupt());

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,36)
	videobuf_waiton(&buf->vb, 0, 0);
#else
	videobuf_waiton(q, &buf->vb, 0, 0);
#endif
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,35)	
	videobuf_dma_unmap(q, dma);
#else
	videobuf_dma_unmap(q->dev, dma);
#endif
	videobuf_dma_free(dma);
	/* if no risc area allocated, btcx_riscmem_free just returns */
	btcx_riscmem_free(to_pci_dev(q->dev), &buf->risc);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

/* ------------------------------------------------------------------ */
/* ------------- placeholders for later development ----------------- */

static int tw68_input_init1(struct tw68_dev *dev)
{
	return 0;
}

static void tw68_input_fini(struct tw68_dev *dev)
{
	return;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
static void tw68_ir_start(struct tw68_dev *dev, struct card_ir *ir)
{
	return;
}

static void tw68_ir_stop(struct tw68_dev *dev)
{
	return;
}
#endif

/* ------------------------------------------------------------------ */
/*
 * Buffer handling routines
 *
 * These routines are "generic", i.e. are intended to be used by more
 * than one module, e.g. the video and the transport stream modules.
 * To accomplish this generality, callbacks are used whenever some
 * module-specific test or action is required.
 */

/* resends a current buffer in queue after resume */
int tw68_buffer_requeue(struct tw68_dev *dev,
				  struct tw68_dmaqueue *q)
{
	struct tw68_buf *buf, *prev;

	dprintk(DBG_FLOW | DBG_TESTING, "%s: called\n", __func__);
	if (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct tw68_buf, vb.queue);
		dprintk(DBG_BUFF, "%s: [%p/%d] restart dma\n", __func__,
			buf, buf->vb.i);
		q->start_dma(dev, q, buf);
		mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
		return 0;
	}

	prev = NULL;
	for (;;) {
		if (list_empty(&q->queued))
			return 0;
		buf = list_entry(q->queued.next, struct tw68_buf, vb.queue);
		/* if nothing precedes this one */
		if (NULL == prev) {
			list_move_tail(&buf->vb.queue, &q->active);
			q->start_dma(dev, q, buf);
			buf->activate(dev, buf, NULL);
			dprintk(DBG_BUFF, "%s: [%p/%d] first active\n",
				__func__, buf, buf->vb.i);

		} else if (q->buf_compat(prev, buf) &&
			   (prev->fmt == buf->fmt)) {
			list_move_tail(&buf->vb.queue, &q->active);
			buf->activate(dev, buf, NULL);
			prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
			dprintk(DBG_BUFF, "%s: [%p/%d] move to active\n",
				__func__, buf, buf->vb.i);
		} else {
			dprintk(DBG_BUFF, "%s: no action taken\n", __func__);
			return 0;
		}
		prev = buf;
	}
}

/* nr of (tw68-)pages for the given buffer size */
static int tw68_buffer_pages(int size)
{
	size  = PAGE_ALIGN(size);
	size += PAGE_SIZE; /* for non-page-aligned buffers */
	size /= 4096;
	return size;
}

/* calc max # of buffers from size (must not exceed the 4MB virtual
 * address space per DMA channel) */
int tw68_buffer_count(unsigned int size, unsigned int count)
{
	unsigned int maxcount;

	maxcount = 1024 / tw68_buffer_pages(size);
	if (count > maxcount)
		count = maxcount;
	return count;
}

/*
 * tw68_wakeup
 *
 * Called when the driver completes filling a buffer, and tasks waiting
 * for the data need to be awakened.
 */
void tw68_wakeup(struct tw68_dmaqueue *q, unsigned int *fc)
{
	struct tw68_dev *dev = q->dev;
	struct tw68_buf *buf;

	dprintk(DBG_FLOW, "%s: called\n", __func__);
	if (list_empty(&q->active)) {
		dprintk(DBG_BUFF | DBG_TESTING, "%s: active list empty",
			__func__);
		del_timer(&q->timeout);
		return;
	}
	buf = list_entry(q->active.next, struct tw68_buf, vb.queue);
	do_gettimeofday(&buf->vb.ts);
	buf->vb.field_count = (*fc)++;
	dprintk(DBG_BUFF | DBG_TESTING, "%s: [%p/%d] field_count=%d\n",
		__func__, buf, buf->vb.i, *fc);
	buf->vb.state = VIDEOBUF_DONE;
	list_del(&buf->vb.queue);
	wake_up(&buf->vb.done);
	mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
}

/*
 * tw68_buffer_queue
 *
 * Add specified buffer to specified queue
 */
void tw68_buffer_queue(struct tw68_dev *dev,
			 struct tw68_dmaqueue *q,
			 struct tw68_buf *buf)
{
	struct tw68_buf    *prev;

	dprintk(DBG_FLOW, "%s: called\n", __func__);
	assert_spin_locked(&dev->slock);
	dprintk(DBG_BUFF, "%s: queuing buffer %p\n", __func__, buf);

	/* append a 'JUMP to stopper' to the buffer risc program */
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_INT_BIT);
	buf->risc.jmp[1] = cpu_to_le32(q->stopper.dma);

	/* if this buffer is not "compatible" (in dimensions and format)
	 * with the currently active chain of buffers, we must change
	 * settings before filling it; if a previous buffer has already
	 * been determined to require changes, this buffer must follow
	 * it.  To do this, we maintain a "queued" chain.  If that
	 * chain exists, append this buffer to it */
	if (!list_empty(&q->queued)) {
		list_add_tail(&buf->vb.queue, &q->queued);
		buf->vb.state = VIDEOBUF_QUEUED;
		dprintk(DBG_BUFF, "%s: [%p/%d] appended to queued\n",
			__func__, buf, buf->vb.i);

	/* else if the 'active' chain doesn't yet exist we create it now */
	} else if (list_empty(&q->active)) {
		dprintk(DBG_BUFF, "%s: [%p/%d] first active\n",
			__func__, buf, buf->vb.i);
		list_add_tail(&buf->vb.queue, &q->active);
		q->start_dma(dev, q, buf);	/* 1st one - start dma */
		/* TODO - why have we removed buf->count and q->count? */
		buf->activate(dev, buf, NULL);

	/* else we would like to put this buffer on the tail of the
	 * active chain, provided it is "compatible". */
	} else {
		/* "compatibility" depends upon the type of buffer */
		prev = list_entry(q->active.prev, struct tw68_buf, vb.queue);
		if (q->buf_compat(prev, buf)) {
			/* If "compatible", append to active chain */
			prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
			/* the param 'prev' is only for debug printing */
			buf->activate(dev, buf, prev);
			list_add_tail(&buf->vb.queue, &q->active);
			dprintk(DBG_BUFF, "%s: [%p/%d] appended to active\n",
				__func__, buf, buf->vb.i);
		} else {
			/* If "incompatible", append to queued chain */
			list_add_tail(&buf->vb.queue, &q->queued);
			buf->vb.state = VIDEOBUF_QUEUED;
			dprintk(DBG_BUFF, "%s: [%p/%d] incompatible - appended "
				"to queued\n", __func__, buf, buf->vb.i);
		}
	}
}

/*
 * tw68_buffer_timeout
 *
 * This routine is set as the video_q.timeout.function
 *
 * Log the event, try to reset the h/w.
 * Flag the current buffer as failed, try to start again with next buff
 */
void tw68_buffer_timeout(unsigned long data)
{
	struct tw68_dmaqueue *q = (struct tw68_dmaqueue *)data;
	struct tw68_dev *dev = q->dev;
	struct tw68_buf *buf;
	unsigned long flags;

	dprintk(DBG_FLOW, "%s: called\n", __func__);
	spin_lock_irqsave(&dev->slock, flags);

	/* flag all current active buffers as failed */
	while (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct tw68_buf, vb.queue);
		list_del(&buf->vb.queue);
		buf->vb.state = VIDEOBUF_ERROR;
		wake_up(&buf->vb.done);
		printk(KERN_INFO "%s/0: [%p/%d] timeout - dma=0x%08lx\n",
			dev->name, buf, buf->vb.i,
			(unsigned long)buf->risc.dma);
	}
	tw68_buffer_requeue(dev, q);
	spin_unlock_irqrestore(&dev->slock, flags);
}

/* ------------------------------------------------------------------ */
/* early init (no i2c, no irq) */

/* Called from tw68_hw_init1 and tw68_resume */
static int tw68_hw_enable1(struct tw68_dev *dev)
{
	return 0;
}

/*
 * The device is given a "soft reset". According to the specifications,
 * after this "all register content remain unchanged", so we also write
 * to all specified registers manually as well (mostly to manufacturer's
 * specified reset values)
 */
static int tw68_hw_init1(struct tw68_dev *dev)
{
	dprintk(DBG_FLOW, "%s: called\n", __func__);
	/* Assure all interrupts are disabled */
	tw_writel(TW68_INTMASK, 0);		/* 020 */
	/* Clear any pending interrupts */
	tw_writel(TW68_INTSTAT, 0xffffffff);	/* 01C */
	/* Stop risc processor, set default buffer level */
	tw_writel(TW68_DMAC, 0x1600);

	tw_writeb(TW68_ACNTL, 0x80);	/* 218	soft reset */
	msleep(100);

	tw_writeb(TW68_INFORM, 0x40);	/* 208	mux0, 27mhz xtal */
	tw_writeb(TW68_OPFORM, 0x04);	/* 20C	analog line-lock */
	tw_writeb(TW68_HSYNC, 0);	/* 210	color-killer high sens */
	tw_writeb(TW68_ACNTL, 0x42);	/* 218	int vref #2, chroma adc off */

	tw_writeb(TW68_CROP_HI, 0x02);	/* 21C	Hactive m.s. bits */
	tw_writeb(TW68_VDELAY_LO, 0x12);/* 220	Mfg specified reset value */
	tw_writeb(TW68_VACTIVE_LO, 0xf0);
	tw_writeb(TW68_HDELAY_LO, 0x0f);
	tw_writeb(TW68_HACTIVE_LO, 0xd0);

	tw_writeb(TW68_CNTRL1, 0xcd);	/* 230	Wide Chroma BPF B/W
					 *	Secam reduction, Adap comb for
					 *	NTSC, Op Mode 1 */

	tw_writeb(TW68_VSCALE_LO, 0);	/* 234 */
	tw_writeb(TW68_SCALE_HI, 0x11);	/* 238 */
	tw_writeb(TW68_HSCALE_LO, 0);	/* 23c */
	tw_writeb(TW68_BRIGHT, 0);	/* 240 */
	tw_writeb(TW68_CONTRAST, 0x5c);	/* 244 */
	tw_writeb(TW68_SHARPNESS, 0x51);/* 248 */
	tw_writeb(TW68_SAT_U, 0x80);	/* 24C */
	tw_writeb(TW68_SAT_V, 0x80);	/* 250 */
	tw_writeb(TW68_HUE, 0x00);	/* 254 */

	/* TODO - Check that none of these are set by control defaults */
	tw_writeb(TW68_SHARP2, 0x53);	/* 258	Mfg specified reset val */
	tw_writeb(TW68_VSHARP, 0x80);	/* 25C	Sharpness Coring val 8 */
	tw_writeb(TW68_CORING, 0x44);	/* 260	CTI and Vert Peak coring */
	tw_writeb(TW68_CNTRL2, 0x00);	/* 268	No power saving enabled */
	tw_writeb(TW68_SDT, 0x07);	/* 270	Enable shadow reg, auto-det */
	tw_writeb(TW68_SDTR, 0x7f);	/* 274	All stds recog, don't start */
	tw_writeb(TW68_CLMPG, 0x50);	/* 280	Clamp end at 40 sys clocks */
	tw_writeb(TW68_IAGC, 0x22);	/* 284	Mfg specified reset val */
	tw_writeb(TW68_AGCGAIN, 0xf0);	/* 288	AGC gain when loop disabled */
	tw_writeb(TW68_PEAKWT, 0xd8);	/* 28C	White peak threshold */
	tw_writeb(TW68_CLMPL, 0x3c);	/* 290	Y channel clamp level */
//	tw_writeb(TW68_SYNCT, 0x38);	/* 294	Sync amplitude */
	tw_writeb(TW68_SYNCT, 0x30);	/* 294	Sync amplitude */
	tw_writeb(TW68_MISSCNT, 0x44);	/* 298	Horiz sync, VCR detect sens */
	tw_writeb(TW68_PCLAMP, 0x28);	/* 29C	Clamp pos from PLL sync */
	/* Bit DETV of VCNTL1 helps sync multi cams/chip board */
	tw_writeb(TW68_VCNTL1, 0x04);	/* 2A0 */
	tw_writeb(TW68_VCNTL2, 0);	/* 2A4 */
	tw_writeb(TW68_CKILL, 0x68);	/* 2A8	Mfg specified reset val */
	tw_writeb(TW68_COMB, 0x44);	/* 2AC	Mfg specified reset val */
	tw_writeb(TW68_LDLY, 0x30);	/* 2B0	Max positive luma delay */
	tw_writeb(TW68_MISC1, 0x14);	/* 2B4	Mfg specified reset val */
	tw_writeb(TW68_LOOP, 0xa5);	/* 2B8	Mfg specified reset val */
	tw_writeb(TW68_MISC2, 0xe0);	/* 2BC	Enable colour killer */
	tw_writeb(TW68_MVSN, 0);	/* 2C0 */
	tw_writeb(TW68_CLMD, 0x05);	/* 2CC	slice level auto, clamp med. */
	tw_writeb(TW68_IDCNTL, 0);	/* 2D0	Writing zero to this register
					 *	selects NTSC ID detection,
					 *	but doesn't change the
					 *	sensitivity (which has a reset
					 *	value of 1E).  Since we are
					 *	not doing auto-detection, it
					 *	has no real effect */
	tw_writeb(TW68_CLCNTL1, 0);	/* 2D4 */
	tw_writel(TW68_VBIC, 0x03);	/* 010 */
	tw_writel(TW68_CAP_CTL, 0x03);	/* 040	Enable both even & odd flds */
	tw_writel(TW68_DMAC, 0x2000);	/* patch set had 0x2080 */
	tw_writel(TW68_TESTREG, 0);	/* 02C */

	/*
	 * Some common boards, especially inexpensive single-chip models,
	 * use the GPIO bits 0-3 to control an on-board video-output mux.
	 * For these boards, we need to set up the GPIO register into
	 * "normal" mode, set bits 0-3 as output, and then set those bits
	 * zero.
	 *
	 * Eventually, it would be nice if we could identify these boards
	 * uniquely, and only do this initialisation if the board has been
	 * identify.  For the moment, however, it shouldn't hurt anything
	 * to do these steps.
	 */
	tw_writel(TW68_GPIOC, 0);	/* Set the GPIO to "normal", no ints */
	tw_writel(TW68_GPOE, 0x0f);	/* Set bits 0-3 to "output" */
	tw_writel(TW68_GPDATA, 0);	/* Set all bits to low state */

	/* Initialize the device control structures */
	mutex_init(&dev->lock);
	spin_lock_init(&dev->slock);

	/* Initialize any subsystems */
	tw68_video_init1(dev);
	tw68_vbi_init1(dev);
	if (card_has_mpeg(dev))
		tw68_ts_init1(dev);
	tw68_input_init1(dev);

	/* Do any other h/w early initialisation at this point */
	tw68_hw_enable1(dev);

	return 0;
}

/* late init (with i2c + irq) */
static int tw68_hw_enable2(struct tw68_dev *dev)
{

	dprintk(DBG_FLOW, "%s: called\n", __func__);
#ifdef	TW68_TESTING
	dev->pci_irqmask |= TW68_I2C_INTS;
#endif
	tw_setl(TW68_INTMASK, dev->pci_irqmask);
	return 0;
}

static int tw68_hw_init2(struct tw68_dev *dev)
{
	dprintk(DBG_FLOW, "%s: called\n", __func__);
	tw68_video_init2(dev);	/* initialise video function first */
	tw68_tvaudio_init2(dev);/* audio next */

	/* all other board-related things, incl. enabling interrupts */
	tw68_hw_enable2(dev);
	return 0;
}

/* shutdown */
static int tw68_hwfini(struct tw68_dev *dev)
{
	dprintk(DBG_FLOW, "%s: called\n", __func__);
	if (card_has_mpeg(dev))
		tw68_ts_fini(dev);
	tw68_input_fini(dev);
	tw68_vbi_fini(dev);
	tw68_tvaudio_fini(dev);
	return 0;
}

static void __devinit must_configure_manually(void)
{
	unsigned int i, p;

	printk(KERN_WARNING
	       "tw68: <rant>\n"
	       "tw68:  Congratulations!  Your TV card vendor saved a few\n"
	       "tw68:  cents for a eeprom, thus your pci board has no\n"
	       "tw68:  subsystem ID and I can't identify it automatically\n"
	       "tw68: </rant>\n"
	       "tw68: I feel better now.  Ok, here is the good news:\n"
	       "tw68: You can use the card=<nr> insmod option to specify\n"
	       "tw68: which board you have.  The list:\n");
	for (i = 0; i < tw68_bcount; i++) {
		printk(KERN_WARNING "tw68:   card=%d -> %-40.40s",
		       i, tw68_boards[i].name);
		for (p = 0; tw68_pci_tbl[p].driver_data; p++) {
			if (tw68_pci_tbl[p].driver_data != i)
				continue;
			printk(" %04x:%04x",
			       tw68_pci_tbl[p].subvendor,
			       tw68_pci_tbl[p].subdevice);
		}
		printk("\n");
	}
}


static irqreturn_t tw68_irq(int irq, void *dev_id)
{
	struct tw68_dev *dev = dev_id;
	u32 status, orig;
	int loop;

	status = orig = tw_readl(TW68_INTSTAT) & dev->pci_irqmask;
	/* Check if anything to do */
	if (0 == status)
		return IRQ_RETVAL(0);	/* Nope - return */
	for (loop = 0; loop < 10; loop++) {
		if (status & dev->board_virqmask)	/* video interrupt */
			tw68_irq_video_done(dev, status);
#ifdef TW68_TESTING
		if (status & TW68_I2C_INTS)
			tw68_irq_i2c(dev, status);
#endif
		status = tw_readl(TW68_INTSTAT) & dev->pci_irqmask;
		if (0 == status)
			goto out;
	}
	dprintk(DBG_UNEXPECTED, "%s: **** INTERRUPT NOT HANDLED - clearing mask"
				" (orig 0x%08x, cur 0x%08x)",
				dev->name, orig, tw_readl(TW68_INTSTAT));
	dprintk(DBG_UNEXPECTED, "%s: pci_irqmask 0x%08x; board_virqmask "
				"0x%08x ****\n", dev->name,
				dev->pci_irqmask, dev->board_virqmask);
	tw_clearl(TW68_INTMASK, dev->pci_irqmask);
out:
	return IRQ_RETVAL(1);
}

int tw68_set_dmabits(struct tw68_dev *dev)
{
	return 0;
}

static struct video_device *vdev_init(struct tw68_dev *dev,
				      struct video_device *template,
				      char *type)
{
	struct video_device *vfd;

	dprintk(DBG_FLOW, "%s: called\n", __func__);
	vfd = video_device_alloc();
	if (NULL == vfd)
		return NULL;
	*vfd = *template;
	vfd->minor   = -1;
	vfd->parent  = &dev->pci->dev;
	vfd->release = video_device_release;
	/* vfd->debug   = tw_video_debug; */
	snprintf(vfd->name, sizeof(vfd->name), "%s %s (%s)",
		 dev->name, type, tw68_boards[dev->board].name);
	return vfd;
}

static void tw68_unregister_video(struct tw68_dev *dev)
{

	dprintk(DBG_FLOW, "%s: called\n", __func__);
	if (dev->video_dev) {
		if (-1 != dev->video_dev->minor)
			video_unregister_device(dev->video_dev);
		else
			video_device_release(dev->video_dev);
		dev->video_dev = NULL;
	}
	if (dev->vbi_dev) {
		if (-1 != dev->vbi_dev->minor)
			video_unregister_device(dev->vbi_dev);
		else
			video_device_release(dev->vbi_dev);
		dev->vbi_dev = NULL;
	}
	if (dev->radio_dev) {
		if (-1 != dev->radio_dev->minor)
			video_unregister_device(dev->radio_dev);
		else
			video_device_release(dev->radio_dev);
		dev->radio_dev = NULL;
	}
}

static void mpeg_ops_attach(struct tw68_mpeg_ops *ops,
			    struct tw68_dev *dev)
{
	int err;

	dprintk(DBG_FLOW, "%s: called\n", __func__);
	if (NULL != dev->mops)
		return;
	if (tw68_boards[dev->board].mpeg != ops->type)
		return;
	err = ops->init(dev);
	if (0 != err)
		return;
	dev->mops = ops;
}

static void mpeg_ops_detach(struct tw68_mpeg_ops *ops,
			    struct tw68_dev *dev)
{

	if (NULL == dev->mops)
		return;
	if (dev->mops != ops)
		return;
	dev->mops->fini(dev);
	dev->mops = NULL;
}

static int __devinit tw68_initdev(struct pci_dev *pci_dev,
				     const struct pci_device_id *pci_id)
{
	struct tw68_dev *dev;
	struct tw68_mpeg_ops *mops;
	int err;

	if (tw68_devcount == TW68_MAXBOARDS)
		return -ENOMEM;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (NULL == dev)
		return -ENOMEM;

	err = v4l2_device_register(&pci_dev->dev, &dev->v4l2_dev);
	if (err)
		goto fail0;

	/* pci init */
	dev->pci = pci_dev;
	if (pci_enable_device(pci_dev)) {
		err = -EIO;
		goto fail1;
	}

	dev->nr = tw68_devcount;
	sprintf(dev->name, "tw%x[%d]", pci_dev->device, dev->nr);

	/* pci quirks */
	if (pci_pci_problems) {
		if (pci_pci_problems & PCIPCI_TRITON)
			printk(KERN_INFO "%s: quirk: PCIPCI_TRITON\n",
				dev->name);
		if (pci_pci_problems & PCIPCI_NATOMA)
			printk(KERN_INFO "%s: quirk: PCIPCI_NATOMA\n",
				dev->name);
		if (pci_pci_problems & PCIPCI_VIAETBF)
			printk(KERN_INFO "%s: quirk: PCIPCI_VIAETBF\n",
				dev->name);
		if (pci_pci_problems & PCIPCI_VSFX)
			printk(KERN_INFO "%s: quirk: PCIPCI_VSFX\n",
				dev->name);
#ifdef PCIPCI_ALIMAGIK
		if (pci_pci_problems & PCIPCI_ALIMAGIK) {
			printk(KERN_INFO "%s: quirk: PCIPCI_ALIMAGIK "
				"-- latency fixup\n", dev->name);
			latency = 0x0A;
		}
#endif
	}
	if (UNSET != latency) {
		printk(KERN_INFO "%s: setting pci latency timer to %d\n",
		       dev->name, latency);
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, latency);
	}

	/* print pci info */
	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &dev->pci_rev);
	pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER,  &dev->pci_lat);
	printk(KERN_INFO "%s: found at %s, rev: %d, irq: %d, "
	       "latency: %d, mmio: 0x%llx\n", dev->name,
	       pci_name(pci_dev), dev->pci_rev, pci_dev->irq, dev->pci_lat,
	       (unsigned long long)pci_resource_start(pci_dev, 0));
	pci_set_master(pci_dev);
	if (!pci_dma_supported(pci_dev, DMA_BIT_MASK(32))) {
		printk("%s: Oops: no 32bit PCI DMA ???\n", dev->name);
		err = -EIO;
		goto fail1;
	}

	switch (pci_id->device) {
	case PCI_DEVICE_ID_6800:	/* TW6800 */
		dev->vdecoder = TW6800;
		dev->board_virqmask = TW68_VID_INTS;
		break;
	case PCI_DEVICE_ID_6801:	/* Video decoder for TW6802 */
		dev->vdecoder = TW6801;
		dev->board_virqmask = TW68_VID_INTS | TW68_VID_INTSX;
		break;
	case PCI_DEVICE_ID_6804:	/* Video decoder for TW6805 */
		dev->vdecoder = TW6804;
		dev->board_virqmask = TW68_VID_INTS | TW68_VID_INTSX;
		break;
	default:
		dev->vdecoder = TWXXXX;	/* To be announced */
		dev->board_virqmask = TW68_VID_INTS | TW68_VID_INTSX;
		break;
	}
	/* board config */
	dev->board = pci_id->driver_data;
	if (card[dev->nr] >= 0 &&
	    card[dev->nr] < tw68_bcount)
		dev->board = card[dev->nr];
	if (TW68_BOARD_NOAUTO == dev->board) {
		must_configure_manually();
		dev->board = TW68_BOARD_UNKNOWN;
	}
	dev->autodetected = card[dev->nr] != dev->board;
	dev->tuner_type = tw68_boards[dev->board].tuner_type;
	dev->tuner_addr = tw68_boards[dev->board].tuner_addr;
	dev->radio_type = tw68_boards[dev->board].radio_type;
	dev->radio_addr = tw68_boards[dev->board].radio_addr;
	dev->tda9887_conf = tw68_boards[dev->board].tda9887_conf;
	if (UNSET != tuner[dev->nr])
		dev->tuner_type = tuner[dev->nr];
	printk(KERN_INFO "%s: subsystem: %04x:%04x, board: %s [card=%d,%s]\n",
		dev->name, pci_dev->subsystem_vendor,
		pci_dev->subsystem_device, tw68_boards[dev->board].name,
		dev->board, dev->autodetected ?
		"autodetected" : "insmod option");

	/* get mmio */
	if (!request_mem_region(pci_resource_start(pci_dev, 0),
				pci_resource_len(pci_dev, 0),
				dev->name)) {
		err = -EBUSY;
		printk(KERN_ERR "%s: can't get MMIO memory @ 0x%llx\n",
			dev->name,
			(unsigned long long)pci_resource_start(pci_dev, 0));
		goto fail1;
	}
	dev->lmmio = ioremap(pci_resource_start(pci_dev, 0),
			     pci_resource_len(pci_dev, 0));
	dev->bmmio = (__u8 __iomem *)dev->lmmio;
	if (NULL == dev->lmmio) {
		err = -EIO;
		printk(KERN_ERR "%s: can't ioremap() MMIO memory\n",
		       dev->name);
		goto fail2;
	}
	/* initialize hardware #1 */
	/* First, take care of anything unique to a particular card */
	tw68_board_init1(dev);
	/* Then do any initialisation wanted before interrupts are on */
	tw68_hw_init1(dev);

	/* get irq */
	err = request_irq(pci_dev->irq, tw68_irq,
			  IRQF_SHARED | IRQF_DISABLED, dev->name, dev);
	if (err < 0) {
		printk(KERN_ERR "%s: can't get IRQ %d\n",
		       dev->name, pci_dev->irq);
		goto fail3;
	}

#ifdef TW68_TESTING
	dev->pci_irqmask |= TW68_SBDONE;
	tw_setl(TW68_INTMASK, dev->pci_irqmask);
	printk(KERN_INFO "Calling tw68_i2c_register\n");
	/* Register the i2c bus */
	tw68_i2c_register(dev);
#endif

	/*
	 *  Now do remainder of initialisation, first for
	 *  things unique for this card, then for general board
	 */
	tw68_board_init2(dev);

	tw68_hw_init2(dev);

#if 0
	/* load i2c helpers */
	if (card_is_empress(dev)) {
		struct v4l2_subdev *sd =
			v4l2_i2c_new_subdev(&dev->i2c_adap, "saa6752hs",
				"saa6752hs", 0x20);

		if (sd)
			sd->grp_id = GRP_EMPRESS;
	}

	request_submodules(dev);
#endif

	v4l2_prio_init(&dev->prio);

	mutex_lock(&tw68_devlist_lock);
	list_for_each_entry(mops, &mops_list, next)
		mpeg_ops_attach(mops, dev);
	list_add_tail(&dev->devlist, &tw68_devlist);
	mutex_unlock(&tw68_devlist_lock);

	/* check for signal */
	tw68_irq_video_signalchange(dev);

#if 0
	if (TUNER_ABSENT != dev->tuner_type)
		tw_call_all(dev, core, s_standby, 0);
#endif

	dev->video_dev = vdev_init(dev, &tw68_video_template, "video");
	err = video_register_device(dev->video_dev, VFL_TYPE_GRABBER,
				    video_nr[dev->nr]);
	if (err < 0) {
		printk(KERN_INFO "%s: can't register video device\n",
		       dev->name);
		goto fail4;
	}
	printk(KERN_INFO "%s: registered device video%d [v4l2]\n",
	       dev->name, dev->video_dev->num);

	dev->vbi_dev = vdev_init(dev, &tw68_video_template, "vbi");

	err = video_register_device(dev->vbi_dev, VFL_TYPE_VBI,
				    vbi_nr[dev->nr]);
	if (err < 0) {
		printk(KERN_INFO "%s: can't register vbi device\n",
			dev->name);
		goto fail4;
	}
	printk(KERN_INFO "%s: registered device vbi%d\n",
	       dev->name, dev->vbi_dev->num);

	if (card_has_radio(dev)) {
		dev->radio_dev = vdev_init(dev, &tw68_radio_template,
					   "radio");
		err = video_register_device(dev->radio_dev, VFL_TYPE_RADIO,
					    radio_nr[dev->nr]);
		if (err < 0) {
			/* TODO - need to unregister vbi? */
			printk(KERN_INFO "%s: can't register radio device\n",
				dev->name);
			goto fail4;
		}
		printk(KERN_INFO "%s: registered device radio%d\n",
		       dev->name, dev->radio_dev->num);
	}

	/* everything worked */
	tw68_devcount++;

	if (tw68_dmasound_init && !dev->dmasound.priv_data)
		tw68_dmasound_init(dev);

	return 0;

 fail4:
	tw68_unregister_video(dev);
#ifdef TW68_TESTING
	tw68_i2c_unregister(dev);
#endif
	free_irq(pci_dev->irq, dev);
 fail3:
	tw68_hwfini(dev);
	iounmap(dev->lmmio);
 fail2:
	release_mem_region(pci_resource_start(pci_dev, 0),
			   pci_resource_len(pci_dev, 0));
 fail1:
	v4l2_device_unregister(&dev->v4l2_dev);
 fail0:
	kfree(dev);
	return err;
}

static void __devexit tw68_finidev(struct pci_dev *pci_dev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
	struct tw68_dev *dev =
		container_of(v4l2_dev, struct tw68_dev, v4l2_dev);
	struct tw68_mpeg_ops *mops;

	dprintk(DBG_FLOW, "%s: called\n", __func__);
	/* Release DMA sound modules if present */
	if (tw68_dmasound_exit && dev->dmasound.priv_data)
		tw68_dmasound_exit(dev);

	/* shutdown subsystems */
	tw68_hwfini(dev);
	tw_clearl(TW68_DMAC, TW68_DMAP_EN | TW68_FIFO_EN);
	tw_writel(TW68_INTMASK, 0);

	/* unregister */
	mutex_lock(&tw68_devlist_lock);
	list_del(&dev->devlist);
	list_for_each_entry(mops, &mops_list, next)
		mpeg_ops_detach(mops, dev);
	mutex_unlock(&tw68_devlist_lock);
	tw68_devcount--;

#ifdef TW68_TESTING
	tw68_i2c_unregister(dev);
#endif
	tw68_unregister_video(dev);


	/* the DMA sound modules should be unloaded before reaching
	   this, but just in case they are still present... */
	if (dev->dmasound.priv_data != NULL) {
		free_irq(pci_dev->irq, &dev->dmasound);
		dev->dmasound.priv_data = NULL;
	}


	/* release resources */
	free_irq(pci_dev->irq, dev);
	iounmap(dev->lmmio);
	release_mem_region(pci_resource_start(pci_dev, 0),
			   pci_resource_len(pci_dev, 0));

	v4l2_device_unregister(&dev->v4l2_dev);

	/* free memory */
	kfree(dev);
}

#ifdef CONFIG_PM

static int tw68_suspend(struct pci_dev *pci_dev , pm_message_t state)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
	struct tw68_dev *dev = container_of(v4l2_dev,
				struct tw68_dev, v4l2_dev);

	dprintk(DBG_FLOW, "%s: called\n", __func__);
	tw_clearl(TW68_DMAC, TW68_DMAP_EN | TW68_FIFO_EN);
	dev->pci_irqmask &= ~TW68_VID_INTS;
	tw_writel(TW68_INTMASK, 0);

	dev->insuspend = 1;
	synchronize_irq(pci_dev->irq);

	/* Disable timeout timers - if we have active buffers, we will
	   fill them on resume*/

	del_timer(&dev->video_q.timeout);
	del_timer(&dev->vbi_q.timeout);
	del_timer(&dev->ts_q.timeout);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
   if (dev->remote)
		tw68_ir_stop(dev);
#endif

	pci_save_state(pci_dev);
	pci_set_power_state(pci_dev, pci_choose_state(pci_dev, state));

	return 0;
}

static int tw68_resume(struct pci_dev *pci_dev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
	struct tw68_dev *dev = container_of(v4l2_dev,
					    struct tw68_dev, v4l2_dev);
	unsigned long flags;

	dprintk(DBG_FLOW, "%s: called\n", __func__);
	pci_set_power_state(pci_dev, PCI_D0);
	pci_restore_state(pci_dev);

	/* Do things that are done in tw68_initdev ,
		except of initializing memory structures.*/

	tw68_board_init1(dev);

	/* tw68_hw_init1 */
	if (tw68_boards[dev->board].video_out)
		tw68_videoport_init(dev);
	if (card_has_mpeg(dev))
		tw68_ts_init_hw(dev);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
	if (dev->remote)
		tw68_ir_start(dev, dev->remote);
#endif
	tw68_hw_enable1(dev);

	msleep(100);

	tw68_board_init2(dev);

	/*tw68_hw_init2*/
	tw68_set_tvnorm_hw(dev);
	tw68_tvaudio_setmute(dev);
/*	tw68_tvaudio_setvolume(dev, dev->ctl_volume); */
	tw68_tvaudio_init(dev);
	tw68_irq_video_signalchange(dev);

	/*resume unfinished buffer(s)*/
	spin_lock_irqsave(&dev->slock, flags);
	tw68_buffer_requeue(dev, &dev->video_q);
	tw68_buffer_requeue(dev, &dev->vbi_q);
	tw68_buffer_requeue(dev, &dev->ts_q);

	/* FIXME: Disable DMA audio sound - temporary till proper support
		  is implemented*/

	dev->dmasound.dma_running = 0;

	/* start DMA now*/
	dev->insuspend = 0;
	smp_wmb();
	tw68_set_dmabits(dev);
	spin_unlock_irqrestore(&dev->slock, flags);

	return 0;
}
#endif

/* ----------------------------------------------------------- */

static struct pci_driver tw68_pci_driver = {
	.name	  = "tw68",
	.id_table = tw68_pci_tbl,
	.probe	  = tw68_initdev,
	.remove	  = __devexit_p(tw68_finidev),
#ifdef CONFIG_PM
	.suspend  = tw68_suspend,
	.resume   = tw68_resume
#endif
};

static int tw68_init(void)
{
	if (core_debug & DBG_FLOW)
		printk(KERN_DEBUG "%s: called\n", __func__);
	INIT_LIST_HEAD(&tw68_devlist);
	printk(KERN_INFO "tw68: v4l2 driver version %d.%d.%d loaded\n",
		(TW68_VERSION_CODE >> 16) & 0xff,
		(TW68_VERSION_CODE >> 8) & 0xff,
		TW68_VERSION_CODE & 0xff);
#if 0
	printk(KERN_INFO "tw68: snapshot date %04d-%02d-%02d\n",
		SNAPSHOT/10000, (SNAPSHOT/100)%100, SNAPSHOT%100);
#endif
	return pci_register_driver(&tw68_pci_driver);
}

static void module_cleanup(void)
{
	if (core_debug & DBG_FLOW)
		printk(KERN_DEBUG "%s: called\n", __func__);
	pci_unregister_driver(&tw68_pci_driver);
}

module_init(tw68_init);
module_exit(module_cleanup);
