// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2010-2015 Steven Toth <stoth@kernellabs.com>
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <asm/div64.h>

#include "saa7164.h"

MODULE_DESCRIPTION("Driver for NXP SAA7164 based TV cards");
MODULE_AUTHOR("Steven Toth <stoth@kernellabs.com>");
MODULE_LICENSE("GPL");

/*
 *  1 Basic
 *  2
 *  4 i2c
 *  8 api
 * 16 cmd
 * 32 bus
 */

unsigned int saa_debug;
module_param_named(debug, saa_debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages");

static unsigned int fw_debug;
module_param(fw_debug, int, 0644);
MODULE_PARM_DESC(fw_debug, "Firmware debug level def:2");

unsigned int encoder_buffers = SAA7164_MAX_ENCODER_BUFFERS;
module_param(encoder_buffers, int, 0644);
MODULE_PARM_DESC(encoder_buffers, "Total buffers in read queue 16-512 def:64");

unsigned int vbi_buffers = SAA7164_MAX_VBI_BUFFERS;
module_param(vbi_buffers, int, 0644);
MODULE_PARM_DESC(vbi_buffers, "Total buffers in read queue 16-512 def:64");

unsigned int waitsecs = 10;
module_param(waitsecs, int, 0644);
MODULE_PARM_DESC(waitsecs, "timeout on firmware messages");

static unsigned int card[]  = {[0 ... (SAA7164_MAXBOARDS - 1)] = UNSET };
module_param_array(card,  int, NULL, 0444);
MODULE_PARM_DESC(card, "card type");

static unsigned int print_histogram = 64;
module_param(print_histogram, int, 0644);
MODULE_PARM_DESC(print_histogram, "print histogram values once");

unsigned int crc_checking = 1;
module_param(crc_checking, int, 0644);
MODULE_PARM_DESC(crc_checking, "enable crc sanity checking on buffers");

static unsigned int guard_checking = 1;
module_param(guard_checking, int, 0644);
MODULE_PARM_DESC(guard_checking,
	"enable dma sanity checking for buffer overruns");

static bool enable_msi = true;
module_param(enable_msi, bool, 0444);
MODULE_PARM_DESC(enable_msi,
		"enable the use of an msi interrupt if available");

static unsigned int saa7164_devcount;

static DEFINE_MUTEX(devlist);
LIST_HEAD(saa7164_devlist);

#define INT_SIZE 16

static void saa7164_pack_verifier(struct saa7164_buffer *buf)
{
	u8 *p = (u8 *)buf->cpu;
	int i;

	for (i = 0; i < buf->actual_size; i += 2048) {

		if ((*(p + i + 0) != 0x00) || (*(p + i + 1) != 0x00) ||
			(*(p + i + 2) != 0x01) || (*(p + i + 3) != 0xBA)) {
			printk(KERN_ERR "No pack at 0x%x\n", i);
#if 0
			print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 1,
				       p + 1, 32, false);
#endif
		}
	}
}

#define FIXED_VIDEO_PID 0xf1
#define FIXED_AUDIO_PID 0xf2

static void saa7164_ts_verifier(struct saa7164_buffer *buf)
{
	struct saa7164_port *port = buf->port;
	u32 i;
	u8 cc, a;
	u16 pid;
	u8 *bufcpu = (u8 *)buf->cpu;

	port->sync_errors = 0;
	port->v_cc_errors = 0;
	port->a_cc_errors = 0;

	for (i = 0; i < buf->actual_size; i += 188) {
		if (*(bufcpu + i) != 0x47)
			port->sync_errors++;

		/* TODO: Query pid lower 8 bits, ignoring upper bits intensionally */
		pid = ((*(bufcpu + i + 1) & 0x1f) << 8) | *(bufcpu + i + 2);
		cc = *(bufcpu + i + 3) & 0x0f;

		if (pid == FIXED_VIDEO_PID) {
			a = ((port->last_v_cc + 1) & 0x0f);
			if (a != cc) {
				printk(KERN_ERR "video cc last = %x current = %x i = %d\n",
					port->last_v_cc, cc, i);
				port->v_cc_errors++;
			}

			port->last_v_cc = cc;
		} else
		if (pid == FIXED_AUDIO_PID) {
			a = ((port->last_a_cc + 1) & 0x0f);
			if (a != cc) {
				printk(KERN_ERR "audio cc last = %x current = %x i = %d\n",
					port->last_a_cc, cc, i);
				port->a_cc_errors++;
			}

			port->last_a_cc = cc;
		}

	}

	/* Only report errors if we've been through this function at least
	 * once already and the cached cc values are primed. First time through
	 * always generates errors.
	 */
	if (port->v_cc_errors && (port->done_first_interrupt > 1))
		printk(KERN_ERR "video pid cc, %d errors\n", port->v_cc_errors);

	if (port->a_cc_errors && (port->done_first_interrupt > 1))
		printk(KERN_ERR "audio pid cc, %d errors\n", port->a_cc_errors);

	if (port->sync_errors && (port->done_first_interrupt > 1))
		printk(KERN_ERR "sync_errors = %d\n", port->sync_errors);

	if (port->done_first_interrupt == 1)
		port->done_first_interrupt++;
}

static void saa7164_histogram_reset(struct saa7164_histogram *hg, char *name)
{
	int i;

	memset(hg, 0, sizeof(struct saa7164_histogram));
	strscpy(hg->name, name, sizeof(hg->name));

	/* First 30ms x 1ms */
	for (i = 0; i < 30; i++)
		hg->counter1[0 + i].val = i;

	/* 30 - 200ms x 10ms  */
	for (i = 0; i < 18; i++)
		hg->counter1[30 + i].val = 30 + (i * 10);

	/* 200 - 2000ms x 100ms  */
	for (i = 0; i < 15; i++)
		hg->counter1[48 + i].val = 200 + (i * 200);

	/* Catch all massive value (2secs) */
	hg->counter1[55].val = 2000;

	/* Catch all massive value (4secs) */
	hg->counter1[56].val = 4000;

	/* Catch all massive value (8secs) */
	hg->counter1[57].val = 8000;

	/* Catch all massive value (15secs) */
	hg->counter1[58].val = 15000;

	/* Catch all massive value (30secs) */
	hg->counter1[59].val = 30000;

	/* Catch all massive value (60secs) */
	hg->counter1[60].val = 60000;

	/* Catch all massive value (5mins) */
	hg->counter1[61].val = 300000;

	/* Catch all massive value (15mins) */
	hg->counter1[62].val = 900000;

	/* Catch all massive values (1hr) */
	hg->counter1[63].val = 3600000;
}

void saa7164_histogram_update(struct saa7164_histogram *hg, u32 val)
{
	int i;
	for (i = 0; i < 64; i++) {
		if (val <= hg->counter1[i].val) {
			hg->counter1[i].count++;
			hg->counter1[i].update_time = jiffies;
			break;
		}
	}
}

static void saa7164_histogram_print(struct saa7164_port *port,
	struct saa7164_histogram *hg)
{
	u32 entries = 0;
	int i;

	printk(KERN_ERR "Histogram named %s (ms, count, last_update_jiffy)\n", hg->name);
	for (i = 0; i < 64; i++) {
		if (hg->counter1[i].count == 0)
			continue;

		printk(KERN_ERR " %4d %12d %Ld\n",
			hg->counter1[i].val,
			hg->counter1[i].count,
			hg->counter1[i].update_time);

		entries++;
	}
	printk(KERN_ERR "Total: %d\n", entries);
}

static void saa7164_work_enchandler_helper(struct saa7164_port *port, int bufnr)
{
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf = NULL;
	struct saa7164_user_buffer *ubuf = NULL;
	struct list_head *c, *n;
	int i = 0;
	u8 *p;

	mutex_lock(&port->dmaqueue_lock);
	list_for_each_safe(c, n, &port->dmaqueue.list) {

		buf = list_entry(c, struct saa7164_buffer, list);
		if (i++ > port->hwcfg.buffercount) {
			printk(KERN_ERR "%s() illegal i count %d\n",
				__func__, i);
			break;
		}

		if (buf->idx == bufnr) {

			/* Found the buffer, deal with it */
			dprintk(DBGLVL_IRQ, "%s() bufnr: %d\n", __func__, bufnr);

			if (crc_checking) {
				/* Throw a new checksum on the dma buffer */
				buf->crc = crc32(0, buf->cpu, buf->actual_size);
			}

			if (guard_checking) {
				p = (u8 *)buf->cpu;
				if ((*(p + buf->actual_size + 0) != 0xff) ||
					(*(p + buf->actual_size + 1) != 0xff) ||
					(*(p + buf->actual_size + 2) != 0xff) ||
					(*(p + buf->actual_size + 3) != 0xff) ||
					(*(p + buf->actual_size + 0x10) != 0xff) ||
					(*(p + buf->actual_size + 0x11) != 0xff) ||
					(*(p + buf->actual_size + 0x12) != 0xff) ||
					(*(p + buf->actual_size + 0x13) != 0xff)) {
						printk(KERN_ERR "%s() buf %p guard buffer breach\n",
							__func__, buf);
#if 0
			print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 1,
				       p + buf->actual_size - 32, 64, false);
#endif
				}
			}

			if ((port->nr != SAA7164_PORT_VBI1) && (port->nr != SAA7164_PORT_VBI2)) {
				/* Validate the incoming buffer content */
				if (port->encoder_params.stream_type == V4L2_MPEG_STREAM_TYPE_MPEG2_TS)
					saa7164_ts_verifier(buf);
				else if (port->encoder_params.stream_type == V4L2_MPEG_STREAM_TYPE_MPEG2_PS)
					saa7164_pack_verifier(buf);
			}

			/* find a free user buffer and clone to it */
			if (!list_empty(&port->list_buf_free.list)) {

				/* Pull the first buffer from the used list */
				ubuf = list_first_entry(&port->list_buf_free.list,
					struct saa7164_user_buffer, list);

				if (buf->actual_size <= ubuf->actual_size) {

					memcpy(ubuf->data, buf->cpu, ubuf->actual_size);

					if (crc_checking) {
						/* Throw a new checksum on the read buffer */
						ubuf->crc = crc32(0, ubuf->data, ubuf->actual_size);
					}

					/* Requeue the buffer on the free list */
					ubuf->pos = 0;

					list_move_tail(&ubuf->list,
						&port->list_buf_used.list);

					/* Flag any userland waiters */
					wake_up_interruptible(&port->wait_read);

				} else {
					printk(KERN_ERR "buf %p bufsize fails match\n", buf);
				}

			} else
				printk(KERN_ERR "encirq no free buffers, increase param encoder_buffers\n");

			/* Ensure offset into buffer remains 0, fill buffer
			 * with known bad data. We check for this data at a later point
			 * in time. */
			saa7164_buffer_zero_offsets(port, bufnr);
			memset(buf->cpu, 0xff, buf->pci_size);
			if (crc_checking) {
				/* Throw yet aanother new checksum on the dma buffer */
				buf->crc = crc32(0, buf->cpu, buf->actual_size);
			}

			break;
		}
	}
	mutex_unlock(&port->dmaqueue_lock);
}

static void saa7164_work_enchandler(struct work_struct *w)
{
	struct saa7164_port *port =
		container_of(w, struct saa7164_port, workenc);
	struct saa7164_dev *dev = port->dev;

	u32 wp, mcb, rp, cnt = 0;

	port->last_svc_msecs_diff = port->last_svc_msecs;
	port->last_svc_msecs = jiffies_to_msecs(jiffies);

	port->last_svc_msecs_diff = port->last_svc_msecs -
		port->last_svc_msecs_diff;

	saa7164_histogram_update(&port->svc_interval,
		port->last_svc_msecs_diff);

	port->last_irq_svc_msecs_diff = port->last_svc_msecs -
		port->last_irq_msecs;

	saa7164_histogram_update(&port->irq_svc_interval,
		port->last_irq_svc_msecs_diff);

	dprintk(DBGLVL_IRQ,
		"%s() %Ldms elapsed irq->deferred %Ldms wp: %d rp: %d\n",
		__func__,
		port->last_svc_msecs_diff,
		port->last_irq_svc_msecs_diff,
		port->last_svc_wp,
		port->last_svc_rp
		);

	/* Current write position */
	wp = saa7164_readl(port->bufcounter);
	if (wp > (port->hwcfg.buffercount - 1)) {
		printk(KERN_ERR "%s() illegal buf count %d\n", __func__, wp);
		return;
	}

	/* Most current complete buffer */
	if (wp == 0)
		mcb = (port->hwcfg.buffercount - 1);
	else
		mcb = wp - 1;

	while (1) {
		if (port->done_first_interrupt == 0) {
			port->done_first_interrupt++;
			rp = mcb;
		} else
			rp = (port->last_svc_rp + 1) % 8;

		if (rp > (port->hwcfg.buffercount - 1)) {
			printk(KERN_ERR "%s() illegal rp count %d\n", __func__, rp);
			break;
		}

		saa7164_work_enchandler_helper(port, rp);
		port->last_svc_rp = rp;
		cnt++;

		if (rp == mcb)
			break;
	}

	/* TODO: Convert this into a /proc/saa7164 style readable file */
	if (print_histogram == port->nr) {
		saa7164_histogram_print(port, &port->irq_interval);
		saa7164_histogram_print(port, &port->svc_interval);
		saa7164_histogram_print(port, &port->irq_svc_interval);
		saa7164_histogram_print(port, &port->read_interval);
		saa7164_histogram_print(port, &port->poll_interval);
		/* TODO: fix this to preserve any previous state */
		print_histogram = 64 + port->nr;
	}
}

static void saa7164_work_vbihandler(struct work_struct *w)
{
	struct saa7164_port *port =
		container_of(w, struct saa7164_port, workenc);
	struct saa7164_dev *dev = port->dev;

	u32 wp, mcb, rp, cnt = 0;

	port->last_svc_msecs_diff = port->last_svc_msecs;
	port->last_svc_msecs = jiffies_to_msecs(jiffies);
	port->last_svc_msecs_diff = port->last_svc_msecs -
		port->last_svc_msecs_diff;

	saa7164_histogram_update(&port->svc_interval,
		port->last_svc_msecs_diff);

	port->last_irq_svc_msecs_diff = port->last_svc_msecs -
		port->last_irq_msecs;

	saa7164_histogram_update(&port->irq_svc_interval,
		port->last_irq_svc_msecs_diff);

	dprintk(DBGLVL_IRQ,
		"%s() %Ldms elapsed irq->deferred %Ldms wp: %d rp: %d\n",
		__func__,
		port->last_svc_msecs_diff,
		port->last_irq_svc_msecs_diff,
		port->last_svc_wp,
		port->last_svc_rp
		);

	/* Current write position */
	wp = saa7164_readl(port->bufcounter);
	if (wp > (port->hwcfg.buffercount - 1)) {
		printk(KERN_ERR "%s() illegal buf count %d\n", __func__, wp);
		return;
	}

	/* Most current complete buffer */
	if (wp == 0)
		mcb = (port->hwcfg.buffercount - 1);
	else
		mcb = wp - 1;

	while (1) {
		if (port->done_first_interrupt == 0) {
			port->done_first_interrupt++;
			rp = mcb;
		} else
			rp = (port->last_svc_rp + 1) % 8;

		if (rp > (port->hwcfg.buffercount - 1)) {
			printk(KERN_ERR "%s() illegal rp count %d\n", __func__, rp);
			break;
		}

		saa7164_work_enchandler_helper(port, rp);
		port->last_svc_rp = rp;
		cnt++;

		if (rp == mcb)
			break;
	}

	/* TODO: Convert this into a /proc/saa7164 style readable file */
	if (print_histogram == port->nr) {
		saa7164_histogram_print(port, &port->irq_interval);
		saa7164_histogram_print(port, &port->svc_interval);
		saa7164_histogram_print(port, &port->irq_svc_interval);
		saa7164_histogram_print(port, &port->read_interval);
		saa7164_histogram_print(port, &port->poll_interval);
		/* TODO: fix this to preserve any previous state */
		print_histogram = 64 + port->nr;
	}
}

static void saa7164_work_cmdhandler(struct work_struct *w)
{
	struct saa7164_dev *dev = container_of(w, struct saa7164_dev, workcmd);

	/* Wake up any complete commands */
	saa7164_irq_dequeue(dev);
}

static void saa7164_buffer_deliver(struct saa7164_buffer *buf)
{
	struct saa7164_port *port = buf->port;

	/* Feed the transport payload into the kernel demux */
	dvb_dmx_swfilter_packets(&port->dvb.demux, (u8 *)buf->cpu,
		SAA7164_TS_NUMBER_OF_LINES);

}

static irqreturn_t saa7164_irq_vbi(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;

	/* Store old time */
	port->last_irq_msecs_diff = port->last_irq_msecs;

	/* Collect new stats */
	port->last_irq_msecs = jiffies_to_msecs(jiffies);

	/* Calculate stats */
	port->last_irq_msecs_diff = port->last_irq_msecs -
		port->last_irq_msecs_diff;

	saa7164_histogram_update(&port->irq_interval,
		port->last_irq_msecs_diff);

	dprintk(DBGLVL_IRQ, "%s() %Ldms elapsed\n", __func__,
		port->last_irq_msecs_diff);

	/* Tis calls the vbi irq handler */
	schedule_work(&port->workenc);
	return 0;
}

static irqreturn_t saa7164_irq_encoder(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;

	/* Store old time */
	port->last_irq_msecs_diff = port->last_irq_msecs;

	/* Collect new stats */
	port->last_irq_msecs = jiffies_to_msecs(jiffies);

	/* Calculate stats */
	port->last_irq_msecs_diff = port->last_irq_msecs -
		port->last_irq_msecs_diff;

	saa7164_histogram_update(&port->irq_interval,
		port->last_irq_msecs_diff);

	dprintk(DBGLVL_IRQ, "%s() %Ldms elapsed\n", __func__,
		port->last_irq_msecs_diff);

	schedule_work(&port->workenc);
	return 0;
}

static irqreturn_t saa7164_irq_ts(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf;
	struct list_head *c, *n;
	int wp, i = 0, rp;

	/* Find the current write point from the hardware */
	wp = saa7164_readl(port->bufcounter);

	BUG_ON(wp > (port->hwcfg.buffercount - 1));

	/* Find the previous buffer to the current write point */
	if (wp == 0)
		rp = (port->hwcfg.buffercount - 1);
	else
		rp = wp - 1;

	/* Lookup the WP in the buffer list */
	/* TODO: turn this into a worker thread */
	list_for_each_safe(c, n, &port->dmaqueue.list) {
		buf = list_entry(c, struct saa7164_buffer, list);
		BUG_ON(i > port->hwcfg.buffercount);
		i++;

		if (buf->idx == rp) {
			/* Found the buffer, deal with it */
			dprintk(DBGLVL_IRQ, "%s() wp: %d processing: %d\n",
				__func__, wp, rp);
			saa7164_buffer_deliver(buf);
			break;
		}

	}
	return 0;
}

/* Primary IRQ handler and dispatch mechanism */
static irqreturn_t saa7164_irq(int irq, void *dev_id)
{
	struct saa7164_dev *dev = dev_id;
	struct saa7164_port *porta, *portb, *portc, *portd, *porte, *portf;

	u32 intid, intstat[INT_SIZE/4];
	int i, handled = 0, bit;

	if (dev == NULL) {
		printk(KERN_ERR "%s() No device specified\n", __func__);
		handled = 0;
		goto out;
	}

	porta = &dev->ports[SAA7164_PORT_TS1];
	portb = &dev->ports[SAA7164_PORT_TS2];
	portc = &dev->ports[SAA7164_PORT_ENC1];
	portd = &dev->ports[SAA7164_PORT_ENC2];
	porte = &dev->ports[SAA7164_PORT_VBI1];
	portf = &dev->ports[SAA7164_PORT_VBI2];

	/* Check that the hardware is accessible. If the status bytes are
	 * 0xFF then the device is not accessible, the IRQ belongs
	 * to another driver.
	 * 4 x u32 interrupt registers.
	 */
	for (i = 0; i < INT_SIZE/4; i++) {

		/* TODO: Convert into saa7164_readl() */
		/* Read the 4 hardware interrupt registers */
		intstat[i] = saa7164_readl(dev->int_status + (i * 4));

		if (intstat[i])
			handled = 1;
	}
	if (handled == 0)
		goto out;

	/* For each of the HW interrupt registers */
	for (i = 0; i < INT_SIZE/4; i++) {

		if (intstat[i]) {
			/* Each function of the board has it's own interruptid.
			 * Find the function that triggered then call
			 * it's handler.
			 */
			for (bit = 0; bit < 32; bit++) {

				if (((intstat[i] >> bit) & 0x00000001) == 0)
					continue;

				/* Calculate the interrupt id (0x00 to 0x7f) */

				intid = (i * 32) + bit;
				if (intid == dev->intfdesc.bInterruptId) {
					/* A response to an cmd/api call */
					schedule_work(&dev->workcmd);
				} else if (intid == porta->hwcfg.interruptid) {

					/* Transport path 1 */
					saa7164_irq_ts(porta);

				} else if (intid == portb->hwcfg.interruptid) {

					/* Transport path 2 */
					saa7164_irq_ts(portb);

				} else if (intid == portc->hwcfg.interruptid) {

					/* Encoder path 1 */
					saa7164_irq_encoder(portc);

				} else if (intid == portd->hwcfg.interruptid) {

					/* Encoder path 2 */
					saa7164_irq_encoder(portd);

				} else if (intid == porte->hwcfg.interruptid) {

					/* VBI path 1 */
					saa7164_irq_vbi(porte);

				} else if (intid == portf->hwcfg.interruptid) {

					/* VBI path 2 */
					saa7164_irq_vbi(portf);

				} else {
					/* Find the function */
					dprintk(DBGLVL_IRQ,
						"%s() unhandled interrupt reg 0x%x bit 0x%x intid = 0x%x\n",
						__func__, i, bit, intid);
				}
			}

			/* Ack it */
			saa7164_writel(dev->int_ack + (i * 4), intstat[i]);

		}
	}
out:
	return IRQ_RETVAL(handled);
}

void saa7164_getfirmwarestatus(struct saa7164_dev *dev)
{
	struct saa7164_fw_status *s = &dev->fw_status;

	dev->fw_status.status = saa7164_readl(SAA_DEVICE_SYSINIT_STATUS);
	dev->fw_status.mode = saa7164_readl(SAA_DEVICE_SYSINIT_MODE);
	dev->fw_status.spec = saa7164_readl(SAA_DEVICE_SYSINIT_SPEC);
	dev->fw_status.inst = saa7164_readl(SAA_DEVICE_SYSINIT_INST);
	dev->fw_status.cpuload = saa7164_readl(SAA_DEVICE_SYSINIT_CPULOAD);
	dev->fw_status.remainheap =
		saa7164_readl(SAA_DEVICE_SYSINIT_REMAINHEAP);

	dprintk(1, "Firmware status:\n");
	dprintk(1, " .status     = 0x%08x\n", s->status);
	dprintk(1, " .mode       = 0x%08x\n", s->mode);
	dprintk(1, " .spec       = 0x%08x\n", s->spec);
	dprintk(1, " .inst       = 0x%08x\n", s->inst);
	dprintk(1, " .cpuload    = 0x%08x\n", s->cpuload);
	dprintk(1, " .remainheap = 0x%08x\n", s->remainheap);
}

u32 saa7164_getcurrentfirmwareversion(struct saa7164_dev *dev)
{
	u32 reg;

	reg = saa7164_readl(SAA_DEVICE_VERSION);
	dprintk(1, "Device running firmware version %d.%d.%d.%d (0x%x)\n",
		(reg & 0x0000fc00) >> 10,
		(reg & 0x000003e0) >> 5,
		(reg & 0x0000001f),
		(reg & 0xffff0000) >> 16,
		reg);

	return reg;
}

/* TODO: Debugging func, remove */
void saa7164_dumpregs(struct saa7164_dev *dev, u32 addr)
{
	int i;

	dprintk(1, "--------------------> 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n");

	for (i = 0; i < 0x100; i += 16)
		dprintk(1, "region0[0x%08x] = %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			i,
			(u8)saa7164_readb(addr + i + 0),
			(u8)saa7164_readb(addr + i + 1),
			(u8)saa7164_readb(addr + i + 2),
			(u8)saa7164_readb(addr + i + 3),
			(u8)saa7164_readb(addr + i + 4),
			(u8)saa7164_readb(addr + i + 5),
			(u8)saa7164_readb(addr + i + 6),
			(u8)saa7164_readb(addr + i + 7),
			(u8)saa7164_readb(addr + i + 8),
			(u8)saa7164_readb(addr + i + 9),
			(u8)saa7164_readb(addr + i + 10),
			(u8)saa7164_readb(addr + i + 11),
			(u8)saa7164_readb(addr + i + 12),
			(u8)saa7164_readb(addr + i + 13),
			(u8)saa7164_readb(addr + i + 14),
			(u8)saa7164_readb(addr + i + 15)
			);
}

static void saa7164_dump_hwdesc(struct saa7164_dev *dev)
{
	dprintk(1, "@0x%p hwdesc sizeof(struct tmComResHWDescr) = %d bytes\n",
		&dev->hwdesc, (u32)sizeof(struct tmComResHWDescr));

	dprintk(1, " .bLength = 0x%x\n", dev->hwdesc.bLength);
	dprintk(1, " .bDescriptorType = 0x%x\n", dev->hwdesc.bDescriptorType);
	dprintk(1, " .bDescriptorSubtype = 0x%x\n",
		dev->hwdesc.bDescriptorSubtype);

	dprintk(1, " .bcdSpecVersion = 0x%x\n", dev->hwdesc.bcdSpecVersion);
	dprintk(1, " .dwClockFrequency = 0x%x\n", dev->hwdesc.dwClockFrequency);
	dprintk(1, " .dwClockUpdateRes = 0x%x\n", dev->hwdesc.dwClockUpdateRes);
	dprintk(1, " .bCapabilities = 0x%x\n", dev->hwdesc.bCapabilities);
	dprintk(1, " .dwDeviceRegistersLocation = 0x%x\n",
		dev->hwdesc.dwDeviceRegistersLocation);

	dprintk(1, " .dwHostMemoryRegion = 0x%x\n",
		dev->hwdesc.dwHostMemoryRegion);

	dprintk(1, " .dwHostMemoryRegionSize = 0x%x\n",
		dev->hwdesc.dwHostMemoryRegionSize);

	dprintk(1, " .dwHostHibernatMemRegion = 0x%x\n",
		dev->hwdesc.dwHostHibernatMemRegion);

	dprintk(1, " .dwHostHibernatMemRegionSize = 0x%x\n",
		dev->hwdesc.dwHostHibernatMemRegionSize);
}

static void saa7164_dump_intfdesc(struct saa7164_dev *dev)
{
	dprintk(1, "@0x%p intfdesc sizeof(struct tmComResInterfaceDescr) = %d bytes\n",
		&dev->intfdesc, (u32)sizeof(struct tmComResInterfaceDescr));

	dprintk(1, " .bLength = 0x%x\n", dev->intfdesc.bLength);
	dprintk(1, " .bDescriptorType = 0x%x\n", dev->intfdesc.bDescriptorType);
	dprintk(1, " .bDescriptorSubtype = 0x%x\n",
		dev->intfdesc.bDescriptorSubtype);

	dprintk(1, " .bFlags = 0x%x\n", dev->intfdesc.bFlags);
	dprintk(1, " .bInterfaceType = 0x%x\n", dev->intfdesc.bInterfaceType);
	dprintk(1, " .bInterfaceId = 0x%x\n", dev->intfdesc.bInterfaceId);
	dprintk(1, " .bBaseInterface = 0x%x\n", dev->intfdesc.bBaseInterface);
	dprintk(1, " .bInterruptId = 0x%x\n", dev->intfdesc.bInterruptId);
	dprintk(1, " .bDebugInterruptId = 0x%x\n",
		dev->intfdesc.bDebugInterruptId);

	dprintk(1, " .BARLocation = 0x%x\n", dev->intfdesc.BARLocation);
}

static void saa7164_dump_busdesc(struct saa7164_dev *dev)
{
	dprintk(1, "@0x%p busdesc sizeof(struct tmComResBusDescr) = %d bytes\n",
		&dev->busdesc, (u32)sizeof(struct tmComResBusDescr));

	dprintk(1, " .CommandRing   = 0x%016Lx\n", dev->busdesc.CommandRing);
	dprintk(1, " .ResponseRing  = 0x%016Lx\n", dev->busdesc.ResponseRing);
	dprintk(1, " .CommandWrite  = 0x%x\n", dev->busdesc.CommandWrite);
	dprintk(1, " .CommandRead   = 0x%x\n", dev->busdesc.CommandRead);
	dprintk(1, " .ResponseWrite = 0x%x\n", dev->busdesc.ResponseWrite);
	dprintk(1, " .ResponseRead  = 0x%x\n", dev->busdesc.ResponseRead);
}

/* Much of the hardware configuration and PCI registers are configured
 * dynamically depending on firmware. We have to cache some initial
 * structures then use these to locate other important structures
 * from PCI space.
 */
static void saa7164_get_descriptors(struct saa7164_dev *dev)
{
	memcpy_fromio(&dev->hwdesc, dev->bmmio, sizeof(struct tmComResHWDescr));
	memcpy_fromio(&dev->intfdesc, dev->bmmio + sizeof(struct tmComResHWDescr),
		sizeof(struct tmComResInterfaceDescr));
	memcpy_fromio(&dev->busdesc, dev->bmmio + dev->intfdesc.BARLocation,
		sizeof(struct tmComResBusDescr));

	if (dev->hwdesc.bLength != sizeof(struct tmComResHWDescr)) {
		printk(KERN_ERR "Structure struct tmComResHWDescr is mangled\n");
		printk(KERN_ERR "Need %x got %d\n", dev->hwdesc.bLength,
			(u32)sizeof(struct tmComResHWDescr));
	} else
		saa7164_dump_hwdesc(dev);

	if (dev->intfdesc.bLength != sizeof(struct tmComResInterfaceDescr)) {
		printk(KERN_ERR "struct struct tmComResInterfaceDescr is mangled\n");
		printk(KERN_ERR "Need %x got %d\n", dev->intfdesc.bLength,
			(u32)sizeof(struct tmComResInterfaceDescr));
	} else
		saa7164_dump_intfdesc(dev);

	saa7164_dump_busdesc(dev);
}

static int saa7164_pci_quirks(struct saa7164_dev *dev)
{
	return 0;
}

static int get_resources(struct saa7164_dev *dev)
{
	if (request_mem_region(pci_resource_start(dev->pci, 0),
		pci_resource_len(dev->pci, 0), dev->name)) {

		if (request_mem_region(pci_resource_start(dev->pci, 2),
			pci_resource_len(dev->pci, 2), dev->name))
			return 0;
	}

	printk(KERN_ERR "%s: can't get MMIO memory @ 0x%llx or 0x%llx\n",
		dev->name,
		(u64)pci_resource_start(dev->pci, 0),
		(u64)pci_resource_start(dev->pci, 2));

	return -EBUSY;
}

static int saa7164_port_init(struct saa7164_dev *dev, int portnr)
{
	struct saa7164_port *port = NULL;

	BUG_ON((portnr < 0) || (portnr >= SAA7164_MAX_PORTS));

	port = &dev->ports[portnr];

	port->dev = dev;
	port->nr = portnr;

	if ((portnr == SAA7164_PORT_TS1) || (portnr == SAA7164_PORT_TS2))
		port->type = SAA7164_MPEG_DVB;
	else
	if ((portnr == SAA7164_PORT_ENC1) || (portnr == SAA7164_PORT_ENC2)) {
		port->type = SAA7164_MPEG_ENCODER;

		/* We need a deferred interrupt handler for cmd handling */
		INIT_WORK(&port->workenc, saa7164_work_enchandler);
	} else if ((portnr == SAA7164_PORT_VBI1) || (portnr == SAA7164_PORT_VBI2)) {
		port->type = SAA7164_MPEG_VBI;

		/* We need a deferred interrupt handler for cmd handling */
		INIT_WORK(&port->workenc, saa7164_work_vbihandler);
	} else
		BUG();

	/* Init all the critical resources */
	mutex_init(&port->dvb.lock);
	INIT_LIST_HEAD(&port->dmaqueue.list);
	mutex_init(&port->dmaqueue_lock);

	INIT_LIST_HEAD(&port->list_buf_used.list);
	INIT_LIST_HEAD(&port->list_buf_free.list);
	init_waitqueue_head(&port->wait_read);


	saa7164_histogram_reset(&port->irq_interval, "irq intervals");
	saa7164_histogram_reset(&port->svc_interval, "deferred intervals");
	saa7164_histogram_reset(&port->irq_svc_interval,
		"irq to deferred intervals");
	saa7164_histogram_reset(&port->read_interval,
		"encoder/vbi read() intervals");
	saa7164_histogram_reset(&port->poll_interval,
		"encoder/vbi poll() intervals");

	return 0;
}

static int saa7164_dev_setup(struct saa7164_dev *dev)
{
	int i;

	mutex_init(&dev->lock);
	atomic_inc(&dev->refcount);
	dev->nr = saa7164_devcount++;

	snprintf(dev->name, sizeof(dev->name), "saa7164[%d]", dev->nr);

	mutex_lock(&devlist);
	list_add_tail(&dev->devlist, &saa7164_devlist);
	mutex_unlock(&devlist);

	/* board config */
	dev->board = UNSET;
	if (card[dev->nr] < saa7164_bcount)
		dev->board = card[dev->nr];

	for (i = 0; UNSET == dev->board  &&  i < saa7164_idcount; i++)
		if (dev->pci->subsystem_vendor == saa7164_subids[i].subvendor &&
			dev->pci->subsystem_device ==
				saa7164_subids[i].subdevice)
				dev->board = saa7164_subids[i].card;

	if (UNSET == dev->board) {
		dev->board = SAA7164_BOARD_UNKNOWN;
		saa7164_card_list(dev);
	}

	dev->pci_bus  = dev->pci->bus->number;
	dev->pci_slot = PCI_SLOT(dev->pci->devfn);

	/* I2C Defaults / setup */
	dev->i2c_bus[0].dev = dev;
	dev->i2c_bus[0].nr = 0;
	dev->i2c_bus[1].dev = dev;
	dev->i2c_bus[1].nr = 1;
	dev->i2c_bus[2].dev = dev;
	dev->i2c_bus[2].nr = 2;

	/* Transport + Encoder ports 1, 2, 3, 4 - Defaults / setup */
	saa7164_port_init(dev, SAA7164_PORT_TS1);
	saa7164_port_init(dev, SAA7164_PORT_TS2);
	saa7164_port_init(dev, SAA7164_PORT_ENC1);
	saa7164_port_init(dev, SAA7164_PORT_ENC2);
	saa7164_port_init(dev, SAA7164_PORT_VBI1);
	saa7164_port_init(dev, SAA7164_PORT_VBI2);

	if (get_resources(dev) < 0) {
		printk(KERN_ERR "CORE %s No more PCIe resources for subsystem: %04x:%04x\n",
		       dev->name, dev->pci->subsystem_vendor,
		       dev->pci->subsystem_device);

		saa7164_devcount--;
		return -ENODEV;
	}

	/* PCI/e allocations */
	dev->lmmio = ioremap(pci_resource_start(dev->pci, 0),
			     pci_resource_len(dev->pci, 0));

	dev->lmmio2 = ioremap(pci_resource_start(dev->pci, 2),
			     pci_resource_len(dev->pci, 2));

	dev->bmmio = (u8 __iomem *)dev->lmmio;
	dev->bmmio2 = (u8 __iomem *)dev->lmmio2;

	/* Interrupt and ack register locations offset of bmmio */
	dev->int_status = 0x183000 + 0xf80;
	dev->int_ack = 0x183000 + 0xf90;

	printk(KERN_INFO
		"CORE %s: subsystem: %04x:%04x, board: %s [card=%d,%s]\n",
	       dev->name, dev->pci->subsystem_vendor,
	       dev->pci->subsystem_device, saa7164_boards[dev->board].name,
	       dev->board, card[dev->nr] == dev->board ?
	       "insmod option" : "autodetected");

	saa7164_pci_quirks(dev);

	return 0;
}

static void saa7164_dev_unregister(struct saa7164_dev *dev)
{
	dprintk(1, "%s()\n", __func__);

	release_mem_region(pci_resource_start(dev->pci, 0),
		pci_resource_len(dev->pci, 0));

	release_mem_region(pci_resource_start(dev->pci, 2),
		pci_resource_len(dev->pci, 2));

	if (!atomic_dec_and_test(&dev->refcount))
		return;

	iounmap(dev->lmmio);
	iounmap(dev->lmmio2);

	return;
}

#ifdef CONFIG_DEBUG_FS
static void *saa7164_seq_start(struct seq_file *s, loff_t *pos)
{
	struct saa7164_dev *dev;
	loff_t index = *pos;

	mutex_lock(&devlist);
	list_for_each_entry(dev, &saa7164_devlist, devlist) {
		if (index-- == 0) {
			mutex_unlock(&devlist);
			return dev;
		}
	}
	mutex_unlock(&devlist);

	return NULL;
}

static void *saa7164_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct saa7164_dev *dev = v;
	void *ret;

	mutex_lock(&devlist);
	if (list_is_last(&dev->devlist, &saa7164_devlist))
		ret = NULL;
	else
		ret = list_next_entry(dev, devlist);
	mutex_unlock(&devlist);

	++*pos;

	return ret;
}

static void saa7164_seq_stop(struct seq_file *s, void *v)
{
}

static int saa7164_seq_show(struct seq_file *m, void *v)
{
	struct saa7164_dev *dev = v;
	struct tmComResBusInfo *b;
	int i, c;

	seq_printf(m, "%s = %p\n", dev->name, dev);

	/* Lock the bus from any other access */
	b = &dev->bus;
	mutex_lock(&b->lock);

	seq_printf(m, " .m_pdwSetWritePos = 0x%x (0x%08x)\n",
		   b->m_dwSetReadPos, saa7164_readl(b->m_dwSetReadPos));

	seq_printf(m, " .m_pdwSetReadPos  = 0x%x (0x%08x)\n",
		   b->m_dwSetWritePos, saa7164_readl(b->m_dwSetWritePos));

	seq_printf(m, " .m_pdwGetWritePos = 0x%x (0x%08x)\n",
		   b->m_dwGetReadPos, saa7164_readl(b->m_dwGetReadPos));

	seq_printf(m, " .m_pdwGetReadPos  = 0x%x (0x%08x)\n",
		   b->m_dwGetWritePos, saa7164_readl(b->m_dwGetWritePos));
	c = 0;
	seq_puts(m, "\n  Set Ring:\n");
	seq_puts(m, "\n addr  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n");
	for (i = 0; i < b->m_dwSizeSetRing; i++) {
		if (c == 0)
			seq_printf(m, " %04x:", i);

		seq_printf(m, " %02x", readb(b->m_pdwSetRing + i));

		if (++c == 16) {
			seq_puts(m, "\n");
			c = 0;
		}
	}

	c = 0;
	seq_puts(m, "\n  Get Ring:\n");
	seq_puts(m, "\n addr  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n");
	for (i = 0; i < b->m_dwSizeGetRing; i++) {
		if (c == 0)
			seq_printf(m, " %04x:", i);

		seq_printf(m, " %02x", readb(b->m_pdwGetRing + i));

		if (++c == 16) {
			seq_puts(m, "\n");
			c = 0;
		}
	}

	mutex_unlock(&b->lock);

	return 0;
}

static const struct seq_operations saa7164_sops = {
	.start = saa7164_seq_start,
	.next = saa7164_seq_next,
	.stop = saa7164_seq_stop,
	.show = saa7164_seq_show,
};

DEFINE_SEQ_ATTRIBUTE(saa7164);

static struct dentry *saa7614_dentry;

static void __init saa7164_debugfs_create(void)
{
	saa7614_dentry = debugfs_create_file("saa7164", 0444, NULL, NULL,
					     &saa7164_fops);
}

static void __exit saa7164_debugfs_remove(void)
{
	debugfs_remove(saa7614_dentry);
}
#else
static void saa7164_debugfs_create(void) { }
static void saa7164_debugfs_remove(void) { }
#endif

static int saa7164_thread_function(void *data)
{
	struct saa7164_dev *dev = data;
	struct tmFwInfoStruct fwinfo;
	u64 last_poll_time = 0;

	dprintk(DBGLVL_THR, "thread started\n");

	set_freezable();

	while (1) {
		msleep_interruptible(100);
		if (kthread_should_stop())
			break;
		try_to_freeze();

		dprintk(DBGLVL_THR, "thread running\n");

		/* Dump the firmware debug message to console */
		/* Polling this costs us 1-2% of the arm CPU */
		/* convert this into a respnde to interrupt 0x7a */
		saa7164_api_collect_debug(dev);

		/* Monitor CPU load every 1 second */
		if ((last_poll_time + 1000 /* ms */) < jiffies_to_msecs(jiffies)) {
			saa7164_api_get_load_info(dev, &fwinfo);
			last_poll_time = jiffies_to_msecs(jiffies);
		}

	}

	dprintk(DBGLVL_THR, "thread exiting\n");
	return 0;
}

static bool saa7164_enable_msi(struct pci_dev *pci_dev, struct saa7164_dev *dev)
{
	int err;

	if (!enable_msi) {
		printk(KERN_WARNING "%s() MSI disabled by module parameter 'enable_msi'"
		       , __func__);
		return false;
	}

	err = pci_enable_msi(pci_dev);

	if (err) {
		printk(KERN_ERR "%s() Failed to enable MSI interrupt. Falling back to a shared IRQ\n",
		       __func__);
		return false;
	}

	/* no error - so request an msi interrupt */
	err = request_irq(pci_dev->irq, saa7164_irq, 0,
						dev->name, dev);

	if (err) {
		/* fall back to legacy interrupt */
		printk(KERN_ERR "%s() Failed to get an MSI interrupt. Falling back to a shared IRQ\n",
		       __func__);
		pci_disable_msi(pci_dev);
		return false;
	}

	return true;
}

static int saa7164_initdev(struct pci_dev *pci_dev,
			   const struct pci_device_id *pci_id)
{
	struct saa7164_dev *dev;
	int err, i;
	u32 version;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (NULL == dev)
		return -ENOMEM;

	err = v4l2_device_register(&pci_dev->dev, &dev->v4l2_dev);
	if (err < 0) {
		dev_err(&pci_dev->dev, "v4l2_device_register failed\n");
		goto fail_free;
	}

	/* pci init */
	dev->pci = pci_dev;
	if (pci_enable_device(pci_dev)) {
		err = -EIO;
		goto fail_free;
	}

	if (saa7164_dev_setup(dev) < 0) {
		err = -EINVAL;
		goto fail_dev;
	}

	/* print pci info */
	dev->pci_rev = pci_dev->revision;
	pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER,  &dev->pci_lat);
	printk(KERN_INFO "%s/0: found at %s, rev: %d, irq: %d, latency: %d, mmio: 0x%llx\n",
	       dev->name,
	       pci_name(pci_dev), dev->pci_rev, pci_dev->irq,
	       dev->pci_lat,
		(unsigned long long)pci_resource_start(pci_dev, 0));

	pci_set_master(pci_dev);
	/* TODO */
	err = dma_set_mask(&pci_dev->dev, 0xffffffff);
	if (err) {
		printk("%s/0: Oops: no 32bit PCI DMA ???\n", dev->name);
		goto fail_irq;
	}

	/* irq bit */
	if (saa7164_enable_msi(pci_dev, dev)) {
		dev->msi = true;
	} else {
		/* if we have an error (i.e. we don't have an interrupt)
			 or msi is not enabled - fallback to shared interrupt */

		err = request_irq(pci_dev->irq, saa7164_irq,
				IRQF_SHARED, dev->name, dev);

		if (err < 0) {
			printk(KERN_ERR "%s: can't get IRQ %d\n", dev->name,
			       pci_dev->irq);
			err = -EIO;
			goto fail_irq;
		}
	}

	pci_set_drvdata(pci_dev, dev);

	/* Init the internal command list */
	for (i = 0; i < SAA_CMD_MAX_MSG_UNITS; i++) {
		dev->cmds[i].seqno = i;
		dev->cmds[i].inuse = 0;
		mutex_init(&dev->cmds[i].lock);
		init_waitqueue_head(&dev->cmds[i].wait);
	}

	/* We need a deferred interrupt handler for cmd handling */
	INIT_WORK(&dev->workcmd, saa7164_work_cmdhandler);

	/* Only load the firmware if we know the board */
	if (dev->board != SAA7164_BOARD_UNKNOWN) {

		err = saa7164_downloadfirmware(dev);
		if (err < 0) {
			printk(KERN_ERR
				"Failed to boot firmware, no features registered\n");
			goto fail_fw;
		}

		saa7164_get_descriptors(dev);
		saa7164_dumpregs(dev, 0);
		saa7164_getcurrentfirmwareversion(dev);
		saa7164_getfirmwarestatus(dev);
		err = saa7164_bus_setup(dev);
		if (err < 0)
			printk(KERN_ERR
				"Failed to setup the bus, will continue\n");
		saa7164_bus_dump(dev);

		/* Ping the running firmware via the command bus and get the
		 * firmware version, this checks the bus is running OK.
		 */
		version = 0;
		if (saa7164_api_get_fw_version(dev, &version) == SAA_OK)
			dprintk(1, "Bus is operating correctly using version %d.%d.%d.%d (0x%x)\n",
				(version & 0x0000fc00) >> 10,
				(version & 0x000003e0) >> 5,
				(version & 0x0000001f),
				(version & 0xffff0000) >> 16,
				version);
		else
			printk(KERN_ERR
				"Failed to communicate with the firmware\n");

		/* Bring up the I2C buses */
		saa7164_i2c_register(&dev->i2c_bus[0]);
		saa7164_i2c_register(&dev->i2c_bus[1]);
		saa7164_i2c_register(&dev->i2c_bus[2]);
		saa7164_gpio_setup(dev);
		saa7164_card_setup(dev);

		/* Parse the dynamic device configuration, find various
		 * media endpoints (MPEG, WMV, PS, TS) and cache their
		 * configuration details into the driver, so we can
		 * reference them later during simething_register() func,
		 * interrupt handlers, deferred work handlers etc.
		 */
		saa7164_api_enum_subdevs(dev);

		/* Begin to create the video sub-systems and register funcs */
		if (saa7164_boards[dev->board].porta == SAA7164_MPEG_DVB) {
			if (saa7164_dvb_register(&dev->ports[SAA7164_PORT_TS1]) < 0) {
				printk(KERN_ERR "%s() Failed to register dvb adapters on porta\n",
					__func__);
			}
		}

		if (saa7164_boards[dev->board].portb == SAA7164_MPEG_DVB) {
			if (saa7164_dvb_register(&dev->ports[SAA7164_PORT_TS2]) < 0) {
				printk(KERN_ERR"%s() Failed to register dvb adapters on portb\n",
					__func__);
			}
		}

		if (saa7164_boards[dev->board].portc == SAA7164_MPEG_ENCODER) {
			if (saa7164_encoder_register(&dev->ports[SAA7164_PORT_ENC1]) < 0) {
				printk(KERN_ERR"%s() Failed to register mpeg encoder\n",
				       __func__);
			}
		}

		if (saa7164_boards[dev->board].portd == SAA7164_MPEG_ENCODER) {
			if (saa7164_encoder_register(&dev->ports[SAA7164_PORT_ENC2]) < 0) {
				printk(KERN_ERR"%s() Failed to register mpeg encoder\n",
				       __func__);
			}
		}

		if (saa7164_boards[dev->board].porte == SAA7164_MPEG_VBI) {
			if (saa7164_vbi_register(&dev->ports[SAA7164_PORT_VBI1]) < 0) {
				printk(KERN_ERR"%s() Failed to register vbi device\n",
				       __func__);
			}
		}

		if (saa7164_boards[dev->board].portf == SAA7164_MPEG_VBI) {
			if (saa7164_vbi_register(&dev->ports[SAA7164_PORT_VBI2]) < 0) {
				printk(KERN_ERR"%s() Failed to register vbi device\n",
				       __func__);
			}
		}
		saa7164_api_set_debug(dev, fw_debug);

		if (fw_debug) {
			dev->kthread = kthread_run(saa7164_thread_function, dev,
				"saa7164 debug");
			if (IS_ERR(dev->kthread)) {
				dev->kthread = NULL;
				printk(KERN_ERR "%s() Failed to create debug kernel thread\n",
				       __func__);
			}
		}

	} /* != BOARD_UNKNOWN */
	else
		printk(KERN_ERR "%s() Unsupported board detected, registering without firmware\n",
		       __func__);

	dprintk(1, "%s() parameter debug = %d\n", __func__, saa_debug);
	dprintk(1, "%s() parameter waitsecs = %d\n", __func__, waitsecs);

fail_fw:
	return 0;

fail_irq:
	saa7164_dev_unregister(dev);
fail_dev:
	pci_disable_device(pci_dev);
fail_free:
	v4l2_device_unregister(&dev->v4l2_dev);
	kfree(dev);
	return err;
}

static void saa7164_shutdown(struct saa7164_dev *dev)
{
	dprintk(1, "%s()\n", __func__);
}

static void saa7164_finidev(struct pci_dev *pci_dev)
{
	struct saa7164_dev *dev = pci_get_drvdata(pci_dev);

	if (dev->board != SAA7164_BOARD_UNKNOWN) {
		if (fw_debug && dev->kthread) {
			kthread_stop(dev->kthread);
			dev->kthread = NULL;
		}
		if (dev->firmwareloaded)
			saa7164_api_set_debug(dev, 0x00);
	}

	saa7164_histogram_print(&dev->ports[SAA7164_PORT_ENC1],
		&dev->ports[SAA7164_PORT_ENC1].irq_interval);
	saa7164_histogram_print(&dev->ports[SAA7164_PORT_ENC1],
		&dev->ports[SAA7164_PORT_ENC1].svc_interval);
	saa7164_histogram_print(&dev->ports[SAA7164_PORT_ENC1],
		&dev->ports[SAA7164_PORT_ENC1].irq_svc_interval);
	saa7164_histogram_print(&dev->ports[SAA7164_PORT_ENC1],
		&dev->ports[SAA7164_PORT_ENC1].read_interval);
	saa7164_histogram_print(&dev->ports[SAA7164_PORT_ENC1],
		&dev->ports[SAA7164_PORT_ENC1].poll_interval);
	saa7164_histogram_print(&dev->ports[SAA7164_PORT_VBI1],
		&dev->ports[SAA7164_PORT_VBI1].read_interval);
	saa7164_histogram_print(&dev->ports[SAA7164_PORT_VBI2],
		&dev->ports[SAA7164_PORT_VBI2].poll_interval);

	saa7164_shutdown(dev);

	if (saa7164_boards[dev->board].porta == SAA7164_MPEG_DVB)
		saa7164_dvb_unregister(&dev->ports[SAA7164_PORT_TS1]);

	if (saa7164_boards[dev->board].portb == SAA7164_MPEG_DVB)
		saa7164_dvb_unregister(&dev->ports[SAA7164_PORT_TS2]);

	if (saa7164_boards[dev->board].portc == SAA7164_MPEG_ENCODER)
		saa7164_encoder_unregister(&dev->ports[SAA7164_PORT_ENC1]);

	if (saa7164_boards[dev->board].portd == SAA7164_MPEG_ENCODER)
		saa7164_encoder_unregister(&dev->ports[SAA7164_PORT_ENC2]);

	if (saa7164_boards[dev->board].porte == SAA7164_MPEG_VBI)
		saa7164_vbi_unregister(&dev->ports[SAA7164_PORT_VBI1]);

	if (saa7164_boards[dev->board].portf == SAA7164_MPEG_VBI)
		saa7164_vbi_unregister(&dev->ports[SAA7164_PORT_VBI2]);

	saa7164_i2c_unregister(&dev->i2c_bus[0]);
	saa7164_i2c_unregister(&dev->i2c_bus[1]);
	saa7164_i2c_unregister(&dev->i2c_bus[2]);

	/* unregister stuff */
	free_irq(pci_dev->irq, dev);

	if (dev->msi) {
		pci_disable_msi(pci_dev);
		dev->msi = false;
	}

	pci_disable_device(pci_dev);

	mutex_lock(&devlist);
	list_del(&dev->devlist);
	mutex_unlock(&devlist);

	saa7164_dev_unregister(dev);
	v4l2_device_unregister(&dev->v4l2_dev);
	kfree(dev);
}

static const struct pci_device_id saa7164_pci_tbl[] = {
	{
		/* SAA7164 */
		.vendor       = 0x1131,
		.device       = 0x7164,
		.subvendor    = PCI_ANY_ID,
		.subdevice    = PCI_ANY_ID,
	}, {
		/* --- end of list --- */
	}
};
MODULE_DEVICE_TABLE(pci, saa7164_pci_tbl);

static struct pci_driver saa7164_pci_driver = {
	.name     = "saa7164",
	.id_table = saa7164_pci_tbl,
	.probe    = saa7164_initdev,
	.remove   = saa7164_finidev,
};

static int __init saa7164_init(void)
{
	int ret = pci_register_driver(&saa7164_pci_driver);

	if (ret)
		return ret;

	saa7164_debugfs_create();

	pr_info("saa7164 driver loaded\n");

	return 0;
}

static void __exit saa7164_fini(void)
{
	saa7164_debugfs_remove();
	pci_unregister_driver(&saa7164_pci_driver);
}

module_init(saa7164_init);
module_exit(saa7164_fini);
