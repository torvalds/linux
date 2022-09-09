// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  TW5864 driver - core functions
 *
 *  Copyright (C) 2016 Bluecherry, LLC <maintainers@bluecherrydvr.com>
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
#include <linux/dma-mapping.h>
#include <linux/pm.h>
#include <linux/pci_ids.h>
#include <linux/jiffies.h>
#include <asm/dma.h>
#include <media/v4l2-dev.h>

#include "tw5864.h"
#include "tw5864-reg.h"

MODULE_DESCRIPTION("V4L2 driver module for tw5864-based multimedia capture & encoding devices");
MODULE_AUTHOR("Bluecherry Maintainers <maintainers@bluecherrydvr.com>");
MODULE_AUTHOR("Andrey Utkin <andrey.utkin@corp.bluecherry.net>");
MODULE_LICENSE("GPL");

/*
 * BEWARE OF KNOWN ISSUES WITH VIDEO QUALITY
 *
 * This driver was developed by Bluecherry LLC by deducing behaviour of
 * original manufacturer's driver, from both source code and execution traces.
 * It is known that there are some artifacts on output video with this driver:
 *  - on all known hardware samples: random pixels of wrong color (mostly
 *    white, red or blue) appearing and disappearing on sequences of P-frames;
 *  - on some hardware samples (known with H.264 core version e006:2800):
 *    total madness on P-frames: blocks of wrong luminance; blocks of wrong
 *    colors "creeping" across the picture.
 * There is a workaround for both issues: avoid P-frames by setting GOP size
 * to 1. To do that, run this command on device files created by this driver:
 *
 * v4l2-ctl --device /dev/videoX --set-ctrl=video_gop_size=1
 *
 * These issues are not decoding errors; all produced H.264 streams are decoded
 * properly. Streams without P-frames don't have these artifacts so it's not
 * analog-to-digital conversion issues nor internal memory errors; we conclude
 * it's internal H.264 encoder issues.
 * We cannot even check the original driver's behaviour because it has never
 * worked properly at all in our development environment. So these issues may
 * be actually related to firmware or hardware. However it may be that there's
 * just some more register settings missing in the driver which would please
 * the hardware.
 * Manufacturer didn't help much on our inquiries, but feel free to disturb
 * again the support of Intersil (owner of former Techwell).
 */

/* take first free /dev/videoX indexes by default */
static unsigned int video_nr[] = {[0 ... (TW5864_INPUTS - 1)] = -1 };

module_param_array(video_nr, int, NULL, 0444);
MODULE_PARM_DESC(video_nr, "video devices numbers array");

/*
 * Please add any new PCI IDs to: https://pci-ids.ucw.cz.  This keeps
 * the PCI ID database up to date.  Note that the entries must be
 * added under vendor 0x1797 (Techwell Inc.) as subsystem IDs.
 */
static const struct pci_device_id tw5864_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_TECHWELL, PCI_DEVICE_ID_TECHWELL_5864)},
	{0,}
};

void tw5864_irqmask_apply(struct tw5864_dev *dev)
{
	tw_writel(TW5864_INTR_ENABLE_L, dev->irqmask & 0xffff);
	tw_writel(TW5864_INTR_ENABLE_H, (dev->irqmask >> 16));
}

static void tw5864_interrupts_disable(struct tw5864_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->slock, flags);
	dev->irqmask = 0;
	tw5864_irqmask_apply(dev);
	spin_unlock_irqrestore(&dev->slock, flags);
}

static void tw5864_timer_isr(struct tw5864_dev *dev);
static void tw5864_h264_isr(struct tw5864_dev *dev);

static irqreturn_t tw5864_isr(int irq, void *dev_id)
{
	struct tw5864_dev *dev = dev_id;
	u32 status;

	status = tw_readl(TW5864_INTR_STATUS_L) |
		tw_readl(TW5864_INTR_STATUS_H) << 16;
	if (!status)
		return IRQ_NONE;

	tw_writel(TW5864_INTR_CLR_L, 0xffff);
	tw_writel(TW5864_INTR_CLR_H, 0xffff);

	if (status & TW5864_INTR_VLC_DONE)
		tw5864_h264_isr(dev);

	if (status & TW5864_INTR_TIMER)
		tw5864_timer_isr(dev);

	if (!(status & (TW5864_INTR_TIMER | TW5864_INTR_VLC_DONE))) {
		dev_dbg(&dev->pci->dev, "Unknown interrupt, status 0x%08X\n",
			status);
	}

	return IRQ_HANDLED;
}

static void tw5864_h264_isr(struct tw5864_dev *dev)
{
	int channel = tw_readl(TW5864_DSP) & TW5864_DSP_ENC_CHN;
	struct tw5864_input *input = &dev->inputs[channel];
	int cur_frame_index, next_frame_index;
	struct tw5864_h264_frame *cur_frame, *next_frame;
	unsigned long flags;

	spin_lock_irqsave(&dev->slock, flags);

	cur_frame_index = dev->h264_buf_w_index;
	next_frame_index = (cur_frame_index + 1) % H264_BUF_CNT;
	cur_frame = &dev->h264_buf[cur_frame_index];
	next_frame = &dev->h264_buf[next_frame_index];

	if (next_frame_index != dev->h264_buf_r_index) {
		cur_frame->vlc_len = tw_readl(TW5864_VLC_LENGTH) << 2;
		cur_frame->checksum = tw_readl(TW5864_VLC_CRC_REG);
		cur_frame->input = input;
		cur_frame->timestamp = ktime_get_ns();
		cur_frame->seqno = input->frame_seqno;
		cur_frame->gop_seqno = input->frame_gop_seqno;

		dev->h264_buf_w_index = next_frame_index;
		tasklet_schedule(&dev->tasklet);

		cur_frame = next_frame;

		spin_lock(&input->slock);
		input->frame_seqno++;
		input->frame_gop_seqno++;
		if (input->frame_gop_seqno >= input->gop)
			input->frame_gop_seqno = 0;
		spin_unlock(&input->slock);
	} else {
		dev_err(&dev->pci->dev,
			"Skipped frame on input %d because all buffers busy\n",
			channel);
	}

	dev->encoder_busy = 0;

	spin_unlock_irqrestore(&dev->slock, flags);

	tw_writel(TW5864_VLC_STREAM_BASE_ADDR, cur_frame->vlc.dma_addr);
	tw_writel(TW5864_MV_STREAM_BASE_ADDR, cur_frame->mv.dma_addr);

	/* Additional ack for this interrupt */
	tw_writel(TW5864_VLC_DSP_INTR, 0x00000001);
	tw_writel(TW5864_PCI_INTR_STATUS, TW5864_VLC_DONE_INTR);
}

static void tw5864_input_deadline_update(struct tw5864_input *input)
{
	input->new_frame_deadline = jiffies + msecs_to_jiffies(1000);
}

static void tw5864_timer_isr(struct tw5864_dev *dev)
{
	unsigned long flags;
	int i;
	int encoder_busy;

	/* Additional ack for this interrupt */
	tw_writel(TW5864_PCI_INTR_STATUS, TW5864_TIMER_INTR);

	spin_lock_irqsave(&dev->slock, flags);
	encoder_busy = dev->encoder_busy;
	spin_unlock_irqrestore(&dev->slock, flags);

	if (encoder_busy)
		return;

	/*
	 * Traversing inputs in round-robin fashion, starting from next to the
	 * last processed one
	 */
	for (i = 0; i < TW5864_INPUTS; i++) {
		int next_input = (i + dev->next_input) % TW5864_INPUTS;
		struct tw5864_input *input = &dev->inputs[next_input];
		int raw_buf_id; /* id of internal buf with last raw frame */

		spin_lock_irqsave(&input->slock, flags);
		if (!input->enabled)
			goto next;

		/* Check if new raw frame is available */
		raw_buf_id = tw_mask_shift_readl(TW5864_SENIF_ORG_FRM_PTR1, 0x3,
						 2 * input->nr);

		if (input->buf_id != raw_buf_id) {
			input->buf_id = raw_buf_id;
			tw5864_input_deadline_update(input);
			spin_unlock_irqrestore(&input->slock, flags);

			spin_lock_irqsave(&dev->slock, flags);
			dev->encoder_busy = 1;
			dev->next_input = (next_input + 1) % TW5864_INPUTS;
			spin_unlock_irqrestore(&dev->slock, flags);

			tw5864_request_encoded_frame(input);
			break;
		}

		/* No new raw frame; check if channel is stuck */
		if (time_is_after_jiffies(input->new_frame_deadline)) {
			/* If stuck, request new raw frames again */
			tw_mask_shift_writel(TW5864_ENC_BUF_PTR_REC1, 0x3,
					     2 * input->nr, input->buf_id + 3);
			tw5864_input_deadline_update(input);
		}
next:
		spin_unlock_irqrestore(&input->slock, flags);
	}
}

static int tw5864_initdev(struct pci_dev *pci_dev,
			  const struct pci_device_id *pci_id)
{
	struct tw5864_dev *dev;
	int err;

	dev = devm_kzalloc(&pci_dev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	snprintf(dev->name, sizeof(dev->name), "tw5864:%s", pci_name(pci_dev));

	err = v4l2_device_register(&pci_dev->dev, &dev->v4l2_dev);
	if (err)
		return err;

	/* pci init */
	dev->pci = pci_dev;
	err = pcim_enable_device(pci_dev);
	if (err) {
		dev_err(&dev->pci->dev, "pcim_enable_device() failed\n");
		goto unreg_v4l2;
	}

	pci_set_master(pci_dev);

	err = dma_set_mask(&pci_dev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&dev->pci->dev, "32 bit PCI DMA is not supported\n");
		goto unreg_v4l2;
	}

	/* get mmio */
	err = pcim_iomap_regions(pci_dev, BIT(0), dev->name);
	if (err) {
		dev_err(&dev->pci->dev, "Cannot request regions for MMIO\n");
		goto unreg_v4l2;
	}
	dev->mmio = pcim_iomap_table(pci_dev)[0];

	spin_lock_init(&dev->slock);

	dev_info(&pci_dev->dev, "TW5864 hardware version: %04x\n",
		 tw_readl(TW5864_HW_VERSION));
	dev_info(&pci_dev->dev, "TW5864 H.264 core version: %04x:%04x\n",
		 tw_readl(TW5864_H264REV),
		 tw_readl(TW5864_UNDECLARED_H264REV_PART2));

	err = tw5864_video_init(dev, video_nr);
	if (err)
		goto unreg_v4l2;

	/* get irq */
	err = devm_request_irq(&pci_dev->dev, pci_dev->irq, tw5864_isr,
			       IRQF_SHARED, "tw5864", dev);
	if (err < 0) {
		dev_err(&dev->pci->dev, "can't get IRQ %d\n", pci_dev->irq);
		goto fini_video;
	}

	dev_info(&pci_dev->dev, "Note: there are known video quality issues. For details\n");
	dev_info(&pci_dev->dev, "see the comment in drivers/media/pci/tw5864/tw5864-core.c.\n");

	return 0;

fini_video:
	tw5864_video_fini(dev);
unreg_v4l2:
	v4l2_device_unregister(&dev->v4l2_dev);
	return err;
}

static void tw5864_finidev(struct pci_dev *pci_dev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
	struct tw5864_dev *dev =
		container_of(v4l2_dev, struct tw5864_dev, v4l2_dev);

	/* shutdown subsystems */
	tw5864_interrupts_disable(dev);

	/* unregister */
	tw5864_video_fini(dev);

	v4l2_device_unregister(&dev->v4l2_dev);
}

static struct pci_driver tw5864_pci_driver = {
	.name = "tw5864",
	.id_table = tw5864_pci_tbl,
	.probe = tw5864_initdev,
	.remove = tw5864_finidev,
};

module_pci_driver(tw5864_pci_driver);
