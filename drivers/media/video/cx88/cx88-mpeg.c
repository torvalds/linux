/*
 *
 *  Support for the mpeg transport stream transfers
 *  PCI function #2 of the cx2388x.
 *
 *    (c) 2004 Jelle Foks <jelle@foks.us>
 *    (c) 2004 Chris Pascoe <c.pascoe@itee.uq.edu.au>
 *    (c) 2004 Gerd Knorr <kraxel@bytesex.org>
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <asm/delay.h>

#include "cx88.h"

/* ------------------------------------------------------------------ */

MODULE_DESCRIPTION("mpeg driver for cx2388x based TV cards");
MODULE_AUTHOR("Jelle Foks <jelle@foks.us>");
MODULE_AUTHOR("Chris Pascoe <c.pascoe@itee.uq.edu.au>");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");
MODULE_VERSION(CX88_VERSION);

static unsigned int debug;
module_param(debug,int,0644);
MODULE_PARM_DESC(debug,"enable debug messages [mpeg]");

#define dprintk(level,fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "%s/2-mpeg: " fmt, dev->core->name, ## arg)

#define mpeg_dbg(level,fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "%s/2-mpeg: " fmt, core->name, ## arg)

#if defined(CONFIG_MODULES) && defined(MODULE)
static void request_module_async(struct work_struct *work)
{
	struct cx8802_dev *dev=container_of(work, struct cx8802_dev, request_module_wk);

	if (dev->core->board.mpeg & CX88_MPEG_DVB)
		request_module("cx88-dvb");
	if (dev->core->board.mpeg & CX88_MPEG_BLACKBIRD)
		request_module("cx88-blackbird");
}

static void request_modules(struct cx8802_dev *dev)
{
	INIT_WORK(&dev->request_module_wk, request_module_async);
	schedule_work(&dev->request_module_wk);
}

static void flush_request_modules(struct cx8802_dev *dev)
{
	flush_work(&dev->request_module_wk);
}
#else
#define request_modules(dev)
#define flush_request_modules(dev)
#endif /* CONFIG_MODULES */


static LIST_HEAD(cx8802_devlist);
static DEFINE_MUTEX(cx8802_mutex);
/* ------------------------------------------------------------------ */

static int cx8802_start_dma(struct cx8802_dev    *dev,
			    struct cx88_dmaqueue *q,
			    struct cx88_buffer   *buf)
{
	struct cx88_core *core = dev->core;

	dprintk(1, "cx8802_start_dma w: %d, h: %d, f: %d\n",
		buf->vb.width, buf->vb.height, buf->vb.field);

	/* setup fifo + format */
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH28],
				dev->ts_packet_size, buf->risc.dma);

	/* write TS length to chip */
	cx_write(MO_TS_LNGTH, buf->vb.width);

	/* FIXME: this needs a review.
	 * also: move to cx88-blackbird + cx88-dvb source files? */

	dprintk( 1, "core->active_type_id = 0x%08x\n", core->active_type_id);

	if ( (core->active_type_id == CX88_MPEG_DVB) &&
		(core->board.mpeg & CX88_MPEG_DVB) ) {

		dprintk( 1, "cx8802_start_dma doing .dvb\n");
		/* negedge driven & software reset */
		cx_write(TS_GEN_CNTRL, 0x0040 | dev->ts_gen_cntrl);
		udelay(100);
		cx_write(MO_PINMUX_IO, 0x00);
		cx_write(TS_HW_SOP_CNTRL, 0x47<<16|188<<4|0x01);
		switch (core->boardnr) {
		case CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD_Q:
		case CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD_T:
		case CX88_BOARD_DVICO_FUSIONHDTV_5_GOLD:
		case CX88_BOARD_PCHDTV_HD5500:
			cx_write(TS_SOP_STAT, 1<<13);
			break;
		case CX88_BOARD_SAMSUNG_SMT_7020:
			cx_write(TS_SOP_STAT, 0x00);
			break;
		case CX88_BOARD_HAUPPAUGE_NOVASPLUS_S1:
		case CX88_BOARD_HAUPPAUGE_NOVASE2_S1:
			cx_write(MO_PINMUX_IO, 0x88); /* Enable MPEG parallel IO and video signal pins */
			udelay(100);
			break;
		case CX88_BOARD_HAUPPAUGE_HVR1300:
			/* Enable MPEG parallel IO and video signal pins */
			cx_write(MO_PINMUX_IO, 0x88);
			cx_write(TS_SOP_STAT, 0);
			cx_write(TS_VALERR_CNTRL, 0);
			break;
		case CX88_BOARD_PINNACLE_PCTV_HD_800i:
			/* Enable MPEG parallel IO and video signal pins */
			cx_write(MO_PINMUX_IO, 0x88);
			cx_write(TS_HW_SOP_CNTRL, (0x47 << 16) | (188 << 4));
			dev->ts_gen_cntrl = 5;
			cx_write(TS_SOP_STAT, 0);
			cx_write(TS_VALERR_CNTRL, 0);
			udelay(100);
			break;
		default:
			cx_write(TS_SOP_STAT, 0x00);
			break;
		}
		cx_write(TS_GEN_CNTRL, dev->ts_gen_cntrl);
		udelay(100);
	} else if ( (core->active_type_id == CX88_MPEG_BLACKBIRD) &&
		(core->board.mpeg & CX88_MPEG_BLACKBIRD) ) {
		dprintk( 1, "cx8802_start_dma doing .blackbird\n");
		cx_write(MO_PINMUX_IO, 0x88); /* enable MPEG parallel IO */

		cx_write(TS_GEN_CNTRL, 0x46); /* punctured clock TS & posedge driven & software reset */
		udelay(100);

		cx_write(TS_HW_SOP_CNTRL, 0x408); /* mpeg start byte */
		cx_write(TS_VALERR_CNTRL, 0x2000);

		cx_write(TS_GEN_CNTRL, 0x06); /* punctured clock TS & posedge driven */
		udelay(100);
	} else {
		printk( "%s() Failed. Unsupported value in .mpeg (0x%08x)\n", __func__,
			core->board.mpeg );
		return -EINVAL;
	}

	/* reset counter */
	cx_write(MO_TS_GPCNTRL, GP_COUNT_CONTROL_RESET);
	q->count = 1;

	/* enable irqs */
	dprintk( 1, "setting the interrupt mask\n" );
	cx_set(MO_PCI_INTMSK, core->pci_irqmask | PCI_INT_TSINT);
	cx_set(MO_TS_INTMSK,  0x1f0011);

	/* start dma */
	cx_set(MO_DEV_CNTRL2, (1<<5));
	cx_set(MO_TS_DMACNTRL, 0x11);
	return 0;
}

static int cx8802_stop_dma(struct cx8802_dev *dev)
{
	struct cx88_core *core = dev->core;
	dprintk( 1, "cx8802_stop_dma\n" );

	/* stop dma */
	cx_clear(MO_TS_DMACNTRL, 0x11);

	/* disable irqs */
	cx_clear(MO_PCI_INTMSK, PCI_INT_TSINT);
	cx_clear(MO_TS_INTMSK, 0x1f0011);

	/* Reset the controller */
	cx_write(TS_GEN_CNTRL, 0xcd);
	return 0;
}

static int cx8802_restart_queue(struct cx8802_dev    *dev,
				struct cx88_dmaqueue *q)
{
	struct cx88_buffer *buf;

	dprintk( 1, "cx8802_restart_queue\n" );
	if (list_empty(&q->active))
	{
		struct cx88_buffer *prev;
		prev = NULL;

		dprintk(1, "cx8802_restart_queue: queue is empty\n" );

		for (;;) {
			if (list_empty(&q->queued))
				return 0;
			buf = list_entry(q->queued.next, struct cx88_buffer, vb.queue);
			if (NULL == prev) {
				list_del(&buf->vb.queue);
				list_add_tail(&buf->vb.queue,&q->active);
				cx8802_start_dma(dev, q, buf);
				buf->vb.state = VIDEOBUF_ACTIVE;
				buf->count    = q->count++;
				mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
				dprintk(1,"[%p/%d] restart_queue - first active\n",
					buf,buf->vb.i);

			} else if (prev->vb.width  == buf->vb.width  &&
				   prev->vb.height == buf->vb.height &&
				   prev->fmt       == buf->fmt) {
				list_del(&buf->vb.queue);
				list_add_tail(&buf->vb.queue,&q->active);
				buf->vb.state = VIDEOBUF_ACTIVE;
				buf->count    = q->count++;
				prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
				dprintk(1,"[%p/%d] restart_queue - move to active\n",
					buf,buf->vb.i);
			} else {
				return 0;
			}
			prev = buf;
		}
		return 0;
	}

	buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
	dprintk(2,"restart_queue [%p/%d]: restart dma\n",
		buf, buf->vb.i);
	cx8802_start_dma(dev, q, buf);
	list_for_each_entry(buf, &q->active, vb.queue)
		buf->count = q->count++;
	mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
	return 0;
}

/* ------------------------------------------------------------------ */

int cx8802_buf_prepare(struct videobuf_queue *q, struct cx8802_dev *dev,
			struct cx88_buffer *buf, enum v4l2_field field)
{
	int size = dev->ts_packet_size * dev->ts_packet_count;
	struct videobuf_dmabuf *dma=videobuf_to_dma(&buf->vb);
	int rc;

	dprintk(1, "%s: %p\n", __func__, buf);
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < size)
		return -EINVAL;

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		buf->vb.width  = dev->ts_packet_size;
		buf->vb.height = dev->ts_packet_count;
		buf->vb.size   = size;
		buf->vb.field  = field /*V4L2_FIELD_TOP*/;

		if (0 != (rc = videobuf_iolock(q,&buf->vb,NULL)))
			goto fail;
		cx88_risc_databuffer(dev->pci, &buf->risc,
				     dma->sglist,
				     buf->vb.width, buf->vb.height, 0);
	}
	buf->vb.state = VIDEOBUF_PREPARED;
	return 0;

 fail:
	cx88_free_buffer(q,buf);
	return rc;
}

void cx8802_buf_queue(struct cx8802_dev *dev, struct cx88_buffer *buf)
{
	struct cx88_buffer    *prev;
	struct cx88_dmaqueue  *cx88q = &dev->mpegq;

	dprintk( 1, "cx8802_buf_queue\n" );
	/* add jump to stopper */
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(cx88q->stopper.dma);

	if (list_empty(&cx88q->active)) {
		dprintk( 1, "queue is empty - first active\n" );
		list_add_tail(&buf->vb.queue,&cx88q->active);
		cx8802_start_dma(dev, cx88q, buf);
		buf->vb.state = VIDEOBUF_ACTIVE;
		buf->count    = cx88q->count++;
		mod_timer(&cx88q->timeout, jiffies+BUFFER_TIMEOUT);
		dprintk(1,"[%p/%d] %s - first active\n",
			buf, buf->vb.i, __func__);

	} else {
		dprintk( 1, "queue is not empty - append to active\n" );
		prev = list_entry(cx88q->active.prev, struct cx88_buffer, vb.queue);
		list_add_tail(&buf->vb.queue,&cx88q->active);
		buf->vb.state = VIDEOBUF_ACTIVE;
		buf->count    = cx88q->count++;
		prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
		dprintk( 1, "[%p/%d] %s - append to active\n",
			buf, buf->vb.i, __func__);
	}
}

/* ----------------------------------------------------------- */

static void do_cancel_buffers(struct cx8802_dev *dev, const char *reason, int restart)
{
	struct cx88_dmaqueue *q = &dev->mpegq;
	struct cx88_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&dev->slock,flags);
	while (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
		list_del(&buf->vb.queue);
		buf->vb.state = VIDEOBUF_ERROR;
		wake_up(&buf->vb.done);
		dprintk(1,"[%p/%d] %s - dma=0x%08lx\n",
			buf, buf->vb.i, reason, (unsigned long)buf->risc.dma);
	}
	if (restart)
	{
		dprintk(1, "restarting queue\n" );
		cx8802_restart_queue(dev,q);
	}
	spin_unlock_irqrestore(&dev->slock,flags);
}

void cx8802_cancel_buffers(struct cx8802_dev *dev)
{
	struct cx88_dmaqueue *q = &dev->mpegq;

	dprintk( 1, "cx8802_cancel_buffers" );
	del_timer_sync(&q->timeout);
	cx8802_stop_dma(dev);
	do_cancel_buffers(dev,"cancel",0);
}

static void cx8802_timeout(unsigned long data)
{
	struct cx8802_dev *dev = (struct cx8802_dev*)data;

	dprintk(1, "%s\n",__func__);

	if (debug)
		cx88_sram_channel_dump(dev->core, &cx88_sram_channels[SRAM_CH28]);
	cx8802_stop_dma(dev);
	do_cancel_buffers(dev,"timeout",1);
}

static const char * cx88_mpeg_irqs[32] = {
	"ts_risci1", NULL, NULL, NULL,
	"ts_risci2", NULL, NULL, NULL,
	"ts_oflow",  NULL, NULL, NULL,
	"ts_sync",   NULL, NULL, NULL,
	"opc_err", "par_err", "rip_err", "pci_abort",
	"ts_err?",
};

static void cx8802_mpeg_irq(struct cx8802_dev *dev)
{
	struct cx88_core *core = dev->core;
	u32 status, mask, count;

	dprintk( 1, "cx8802_mpeg_irq\n" );
	status = cx_read(MO_TS_INTSTAT);
	mask   = cx_read(MO_TS_INTMSK);
	if (0 == (status & mask))
		return;

	cx_write(MO_TS_INTSTAT, status);

	if (debug || (status & mask & ~0xff))
		cx88_print_irqbits(core->name, "irq mpeg ",
				   cx88_mpeg_irqs, ARRAY_SIZE(cx88_mpeg_irqs),
				   status, mask);

	/* risc op code error */
	if (status & (1 << 16)) {
		printk(KERN_WARNING "%s: mpeg risc op code error\n",core->name);
		cx_clear(MO_TS_DMACNTRL, 0x11);
		cx88_sram_channel_dump(dev->core, &cx88_sram_channels[SRAM_CH28]);
	}

	/* risc1 y */
	if (status & 0x01) {
		dprintk( 1, "wake up\n" );
		spin_lock(&dev->slock);
		count = cx_read(MO_TS_GPCNT);
		cx88_wakeup(dev->core, &dev->mpegq, count);
		spin_unlock(&dev->slock);
	}

	/* risc2 y */
	if (status & 0x10) {
		spin_lock(&dev->slock);
		cx8802_restart_queue(dev,&dev->mpegq);
		spin_unlock(&dev->slock);
	}

	/* other general errors */
	if (status & 0x1f0100) {
		dprintk( 0, "general errors: 0x%08x\n", status & 0x1f0100 );
		spin_lock(&dev->slock);
		cx8802_stop_dma(dev);
		cx8802_restart_queue(dev,&dev->mpegq);
		spin_unlock(&dev->slock);
	}
}

#define MAX_IRQ_LOOP 10

static irqreturn_t cx8802_irq(int irq, void *dev_id)
{
	struct cx8802_dev *dev = dev_id;
	struct cx88_core *core = dev->core;
	u32 status;
	int loop, handled = 0;

	for (loop = 0; loop < MAX_IRQ_LOOP; loop++) {
		status = cx_read(MO_PCI_INTSTAT) &
			(core->pci_irqmask | PCI_INT_TSINT);
		if (0 == status)
			goto out;
		dprintk( 1, "cx8802_irq\n" );
		dprintk( 1, "    loop: %d/%d\n", loop, MAX_IRQ_LOOP );
		dprintk( 1, "    status: %d\n", status );
		handled = 1;
		cx_write(MO_PCI_INTSTAT, status);

		if (status & core->pci_irqmask)
			cx88_core_irq(core,status);
		if (status & PCI_INT_TSINT)
			cx8802_mpeg_irq(dev);
	};
	if (MAX_IRQ_LOOP == loop) {
		dprintk( 0, "clearing mask\n" );
		printk(KERN_WARNING "%s/0: irq loop -- clearing mask\n",
		       core->name);
		cx_write(MO_PCI_INTMSK,0);
	}

 out:
	return IRQ_RETVAL(handled);
}

static int cx8802_init_common(struct cx8802_dev *dev)
{
	struct cx88_core *core = dev->core;
	int err;

	/* pci init */
	if (pci_enable_device(dev->pci))
		return -EIO;
	pci_set_master(dev->pci);
	if (!pci_dma_supported(dev->pci,DMA_BIT_MASK(32))) {
		printk("%s/2: Oops: no 32bit PCI DMA ???\n",dev->core->name);
		return -EIO;
	}

	dev->pci_rev = dev->pci->revision;
	pci_read_config_byte(dev->pci, PCI_LATENCY_TIMER,  &dev->pci_lat);
	printk(KERN_INFO "%s/2: found at %s, rev: %d, irq: %d, "
	       "latency: %d, mmio: 0x%llx\n", dev->core->name,
	       pci_name(dev->pci), dev->pci_rev, dev->pci->irq,
	       dev->pci_lat,(unsigned long long)pci_resource_start(dev->pci,0));

	/* initialize driver struct */
	spin_lock_init(&dev->slock);

	/* init dma queue */
	INIT_LIST_HEAD(&dev->mpegq.active);
	INIT_LIST_HEAD(&dev->mpegq.queued);
	dev->mpegq.timeout.function = cx8802_timeout;
	dev->mpegq.timeout.data     = (unsigned long)dev;
	init_timer(&dev->mpegq.timeout);
	cx88_risc_stopper(dev->pci,&dev->mpegq.stopper,
			  MO_TS_DMACNTRL,0x11,0x00);

	/* get irq */
	err = request_irq(dev->pci->irq, cx8802_irq,
			  IRQF_SHARED | IRQF_DISABLED, dev->core->name, dev);
	if (err < 0) {
		printk(KERN_ERR "%s: can't get IRQ %d\n",
		       dev->core->name, dev->pci->irq);
		return err;
	}
	cx_set(MO_PCI_INTMSK, core->pci_irqmask);

	/* everything worked */
	pci_set_drvdata(dev->pci,dev);
	return 0;
}

static void cx8802_fini_common(struct cx8802_dev *dev)
{
	dprintk( 2, "cx8802_fini_common\n" );
	cx8802_stop_dma(dev);
	pci_disable_device(dev->pci);

	/* unregister stuff */
	free_irq(dev->pci->irq, dev);
	pci_set_drvdata(dev->pci, NULL);

	/* free memory */
	btcx_riscmem_free(dev->pci,&dev->mpegq.stopper);
}

/* ----------------------------------------------------------- */

static int cx8802_suspend_common(struct pci_dev *pci_dev, pm_message_t state)
{
	struct cx8802_dev *dev = pci_get_drvdata(pci_dev);
	struct cx88_core *core = dev->core;

	/* stop mpeg dma */
	spin_lock(&dev->slock);
	if (!list_empty(&dev->mpegq.active)) {
		dprintk( 2, "suspend\n" );
		printk("%s: suspend mpeg\n", core->name);
		cx8802_stop_dma(dev);
		del_timer(&dev->mpegq.timeout);
	}
	spin_unlock(&dev->slock);

	/* FIXME -- shutdown device */
	cx88_shutdown(dev->core);

	pci_save_state(pci_dev);
	if (0 != pci_set_power_state(pci_dev, pci_choose_state(pci_dev, state))) {
		pci_disable_device(pci_dev);
		dev->state.disabled = 1;
	}
	return 0;
}

static int cx8802_resume_common(struct pci_dev *pci_dev)
{
	struct cx8802_dev *dev = pci_get_drvdata(pci_dev);
	struct cx88_core *core = dev->core;
	int err;

	if (dev->state.disabled) {
		err=pci_enable_device(pci_dev);
		if (err) {
			printk(KERN_ERR "%s: can't enable device\n",
					       dev->core->name);
			return err;
		}
		dev->state.disabled = 0;
	}
	err=pci_set_power_state(pci_dev, PCI_D0);
	if (err) {
		printk(KERN_ERR "%s: can't enable device\n",
					       dev->core->name);
		pci_disable_device(pci_dev);
		dev->state.disabled = 1;

		return err;
	}
	pci_restore_state(pci_dev);

	/* FIXME: re-initialize hardware */
	cx88_reset(dev->core);

	/* restart video+vbi capture */
	spin_lock(&dev->slock);
	if (!list_empty(&dev->mpegq.active)) {
		printk("%s: resume mpeg\n", core->name);
		cx8802_restart_queue(dev,&dev->mpegq);
	}
	spin_unlock(&dev->slock);

	return 0;
}

struct cx8802_driver * cx8802_get_driver(struct cx8802_dev *dev, enum cx88_board_type btype)
{
	struct cx8802_driver *d;

	list_for_each_entry(d, &dev->drvlist, drvlist)
		if (d->type_id == btype)
			return d;

	return NULL;
}

/* Driver asked for hardware access. */
static int cx8802_request_acquire(struct cx8802_driver *drv)
{
	struct cx88_core *core = drv->core;
	unsigned int	i;

	/* Fail a request for hardware if the device is busy. */
	if (core->active_type_id != CX88_BOARD_NONE &&
	    core->active_type_id != drv->type_id)
		return -EBUSY;

	if (drv->type_id == CX88_MPEG_DVB) {
		/* When switching to DVB, always set the input to the tuner */
		core->last_analog_input = core->input;
		core->input = 0;
		for (i = 0;
		     i < (sizeof(core->board.input) / sizeof(struct cx88_input));
		     i++) {
			if (core->board.input[i].type == CX88_VMUX_DVB) {
				core->input = i;
				break;
			}
		}
	}

	if (drv->advise_acquire)
	{
		core->active_ref++;
		if (core->active_type_id == CX88_BOARD_NONE) {
			core->active_type_id = drv->type_id;
			drv->advise_acquire(drv);
		}

		mpeg_dbg(1,"%s() Post acquire GPIO=%x\n", __func__, cx_read(MO_GP0_IO));
	}

	return 0;
}

/* Driver asked to release hardware. */
static int cx8802_request_release(struct cx8802_driver *drv)
{
	struct cx88_core *core = drv->core;

	if (drv->advise_release && --core->active_ref == 0)
	{
		if (drv->type_id == CX88_MPEG_DVB) {
			/* If the DVB driver is releasing, reset the input
			   state to the last configured analog input */
			core->input = core->last_analog_input;
		}

		drv->advise_release(drv);
		core->active_type_id = CX88_BOARD_NONE;
		mpeg_dbg(1,"%s() Post release GPIO=%x\n", __func__, cx_read(MO_GP0_IO));
	}

	return 0;
}

static int cx8802_check_driver(struct cx8802_driver *drv)
{
	if (drv == NULL)
		return -ENODEV;

	if ((drv->type_id != CX88_MPEG_DVB) &&
		(drv->type_id != CX88_MPEG_BLACKBIRD))
		return -EINVAL;

	if ((drv->hw_access != CX8802_DRVCTL_SHARED) &&
		(drv->hw_access != CX8802_DRVCTL_EXCLUSIVE))
		return -EINVAL;

	if ((drv->probe == NULL) ||
		(drv->remove == NULL) ||
		(drv->advise_acquire == NULL) ||
		(drv->advise_release == NULL))
		return -EINVAL;

	return 0;
}

int cx8802_register_driver(struct cx8802_driver *drv)
{
	struct cx8802_dev *dev;
	struct cx8802_driver *driver;
	int err, i = 0;

	printk(KERN_INFO
	       "cx88/2: registering cx8802 driver, type: %s access: %s\n",
	       drv->type_id == CX88_MPEG_DVB ? "dvb" : "blackbird",
	       drv->hw_access == CX8802_DRVCTL_SHARED ? "shared" : "exclusive");

	if ((err = cx8802_check_driver(drv)) != 0) {
		printk(KERN_ERR "cx88/2: cx8802_driver is invalid\n");
		return err;
	}

	mutex_lock(&cx8802_mutex);

	list_for_each_entry(dev, &cx8802_devlist, devlist) {
		printk(KERN_INFO
		       "%s/2: subsystem: %04x:%04x, board: %s [card=%d]\n",
		       dev->core->name, dev->pci->subsystem_vendor,
		       dev->pci->subsystem_device, dev->core->board.name,
		       dev->core->boardnr);

		/* Bring up a new struct for each driver instance */
		driver = kzalloc(sizeof(*drv),GFP_KERNEL);
		if (driver == NULL) {
			err = -ENOMEM;
			goto out;
		}

		/* Snapshot of the driver registration data */
		drv->core = dev->core;
		drv->suspend = cx8802_suspend_common;
		drv->resume = cx8802_resume_common;
		drv->request_acquire = cx8802_request_acquire;
		drv->request_release = cx8802_request_release;
		memcpy(driver, drv, sizeof(*driver));

		mutex_lock(&drv->core->lock);
		err = drv->probe(driver);
		if (err == 0) {
			i++;
			list_add_tail(&driver->drvlist, &dev->drvlist);
		} else {
			printk(KERN_ERR
			       "%s/2: cx8802 probe failed, err = %d\n",
			       dev->core->name, err);
		}
		mutex_unlock(&drv->core->lock);
	}

	err = i ? 0 : -ENODEV;
out:
	mutex_unlock(&cx8802_mutex);
	return err;
}

int cx8802_unregister_driver(struct cx8802_driver *drv)
{
	struct cx8802_dev *dev;
	struct cx8802_driver *d, *dtmp;
	int err = 0;

	printk(KERN_INFO
	       "cx88/2: unregistering cx8802 driver, type: %s access: %s\n",
	       drv->type_id == CX88_MPEG_DVB ? "dvb" : "blackbird",
	       drv->hw_access == CX8802_DRVCTL_SHARED ? "shared" : "exclusive");

	mutex_lock(&cx8802_mutex);

	list_for_each_entry(dev, &cx8802_devlist, devlist) {
		printk(KERN_INFO
		       "%s/2: subsystem: %04x:%04x, board: %s [card=%d]\n",
		       dev->core->name, dev->pci->subsystem_vendor,
		       dev->pci->subsystem_device, dev->core->board.name,
		       dev->core->boardnr);

		mutex_lock(&dev->core->lock);

		list_for_each_entry_safe(d, dtmp, &dev->drvlist, drvlist) {
			/* only unregister the correct driver type */
			if (d->type_id != drv->type_id)
				continue;

			err = d->remove(d);
			if (err == 0) {
				list_del(&d->drvlist);
				kfree(d);
			} else
				printk(KERN_ERR "%s/2: cx8802 driver remove "
				       "failed (%d)\n", dev->core->name, err);
		}

		mutex_unlock(&dev->core->lock);
	}

	mutex_unlock(&cx8802_mutex);

	return err;
}

/* ----------------------------------------------------------- */
static int __devinit cx8802_probe(struct pci_dev *pci_dev,
			       const struct pci_device_id *pci_id)
{
	struct cx8802_dev *dev;
	struct cx88_core  *core;
	int err;

	/* general setup */
	core = cx88_core_get(pci_dev);
	if (NULL == core)
		return -EINVAL;

	printk("%s/2: cx2388x 8802 Driver Manager\n", core->name);

	err = -ENODEV;
	if (!core->board.mpeg)
		goto fail_core;

	err = -ENOMEM;
	dev = kzalloc(sizeof(*dev),GFP_KERNEL);
	if (NULL == dev)
		goto fail_core;
	dev->pci = pci_dev;
	dev->core = core;

	/* Maintain a reference so cx88-video can query the 8802 device. */
	core->dvbdev = dev;

	err = cx8802_init_common(dev);
	if (err != 0)
		goto fail_free;

	INIT_LIST_HEAD(&dev->drvlist);
	mutex_lock(&cx8802_mutex);
	list_add_tail(&dev->devlist,&cx8802_devlist);
	mutex_unlock(&cx8802_mutex);

	/* now autoload cx88-dvb or cx88-blackbird */
	request_modules(dev);
	return 0;

 fail_free:
	kfree(dev);
 fail_core:
	core->dvbdev = NULL;
	cx88_core_put(core,pci_dev);
	return err;
}

static void __devexit cx8802_remove(struct pci_dev *pci_dev)
{
	struct cx8802_dev *dev;

	dev = pci_get_drvdata(pci_dev);

	dprintk( 1, "%s\n", __func__);

	flush_request_modules(dev);

	mutex_lock(&dev->core->lock);

	if (!list_empty(&dev->drvlist)) {
		struct cx8802_driver *drv, *tmp;
		int err;

		printk(KERN_WARNING "%s/2: Trying to remove cx8802 driver "
		       "while cx8802 sub-drivers still loaded?!\n",
		       dev->core->name);

		list_for_each_entry_safe(drv, tmp, &dev->drvlist, drvlist) {
			err = drv->remove(drv);
			if (err == 0) {
				list_del(&drv->drvlist);
			} else
				printk(KERN_ERR "%s/2: cx8802 driver remove "
				       "failed (%d)\n", dev->core->name, err);
			kfree(drv);
		}
	}

	mutex_unlock(&dev->core->lock);

	/* Destroy any 8802 reference. */
	dev->core->dvbdev = NULL;

	/* common */
	cx8802_fini_common(dev);
	cx88_core_put(dev->core,dev->pci);
	kfree(dev);
}

static const struct pci_device_id cx8802_pci_tbl[] = {
	{
		.vendor       = 0x14f1,
		.device       = 0x8802,
		.subvendor    = PCI_ANY_ID,
		.subdevice    = PCI_ANY_ID,
	},{
		/* --- end of list --- */
	}
};
MODULE_DEVICE_TABLE(pci, cx8802_pci_tbl);

static struct pci_driver cx8802_pci_driver = {
	.name     = "cx88-mpeg driver manager",
	.id_table = cx8802_pci_tbl,
	.probe    = cx8802_probe,
	.remove   = __devexit_p(cx8802_remove),
};

static int __init cx8802_init(void)
{
	printk(KERN_INFO "cx88/2: cx2388x MPEG-TS Driver Manager version %s loaded\n",
	       CX88_VERSION);
	return pci_register_driver(&cx8802_pci_driver);
}

static void __exit cx8802_fini(void)
{
	pci_unregister_driver(&cx8802_pci_driver);
}

module_init(cx8802_init);
module_exit(cx8802_fini);
EXPORT_SYMBOL(cx8802_buf_prepare);
EXPORT_SYMBOL(cx8802_buf_queue);
EXPORT_SYMBOL(cx8802_cancel_buffers);

EXPORT_SYMBOL(cx8802_register_driver);
EXPORT_SYMBOL(cx8802_unregister_driver);
EXPORT_SYMBOL(cx8802_get_driver);
/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 * kate: eol "unix"; indent-width 3; remove-trailing-space on; replace-trailing-space-save on; tab-width 8; replace-tabs off; space-indent off; mixed-indent off
 */
