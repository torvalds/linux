// SPDX-License-Identifier: GPL-2.0
#include <media/drv-intf/saa7146_vv.h>

static int vbi_pixel_to_capture = 720 * 2;

static int vbi_workaround(struct saa7146_dev *dev)
{
	struct saa7146_vv *vv = dev->vv_data;

	u32          *cpu;
	dma_addr_t   dma_addr;

	int count = 0;
	int i;

	DECLARE_WAITQUEUE(wait, current);

	DEB_VBI("dev:%p\n", dev);

	/* once again, a bug in the saa7146: the brs acquisition
	   is buggy and especially the BXO-counter does not work
	   as specified. there is this workaround, but please
	   don't let me explain it. ;-) */

	cpu = dma_alloc_coherent(&dev->pci->dev, 4096, &dma_addr, GFP_KERNEL);
	if (NULL == cpu)
		return -ENOMEM;

	/* setup some basic programming, just for the workaround */
	saa7146_write(dev, BASE_EVEN3,	dma_addr);
	saa7146_write(dev, BASE_ODD3,	dma_addr+vbi_pixel_to_capture);
	saa7146_write(dev, PROT_ADDR3,	dma_addr+4096);
	saa7146_write(dev, PITCH3,	vbi_pixel_to_capture);
	saa7146_write(dev, BASE_PAGE3,	0x0);
	saa7146_write(dev, NUM_LINE_BYTE3, (2<<16)|((vbi_pixel_to_capture)<<0));
	saa7146_write(dev, MC2, MASK_04|MASK_20);

	/* load brs-control register */
	WRITE_RPS1(CMD_WR_REG | (1 << 8) | (BRS_CTRL/4));
	/* BXO = 1h, BRS to outbound */
	WRITE_RPS1(0xc000008c);
	/* wait for vbi_a or vbi_b*/
	if ( 0 != (SAA7146_USE_PORT_B_FOR_VBI & dev->ext_vv_data->flags)) {
		DEB_D("...using port b\n");
		WRITE_RPS1(CMD_PAUSE | CMD_OAN | CMD_SIG1 | CMD_E_FID_B);
		WRITE_RPS1(CMD_PAUSE | CMD_OAN | CMD_SIG1 | CMD_O_FID_B);
/*
		WRITE_RPS1(CMD_PAUSE | MASK_09);
*/
	} else {
		DEB_D("...using port a\n");
		WRITE_RPS1(CMD_PAUSE | MASK_10);
	}
	/* upload brs */
	WRITE_RPS1(CMD_UPLOAD | MASK_08);
	/* load brs-control register */
	WRITE_RPS1(CMD_WR_REG | (1 << 8) | (BRS_CTRL/4));
	/* BYO = 1, BXO = NQBIL (=1728 for PAL, for NTSC this is 858*2) - NumByte3 (=1440) = 288 */
	WRITE_RPS1(((1728-(vbi_pixel_to_capture)) << 7) | MASK_19);
	/* wait for brs_done */
	WRITE_RPS1(CMD_PAUSE | MASK_08);
	/* upload brs */
	WRITE_RPS1(CMD_UPLOAD | MASK_08);
	/* load video-dma3 NumLines3 and NumBytes3 */
	WRITE_RPS1(CMD_WR_REG | (1 << 8) | (NUM_LINE_BYTE3/4));
	/* dev->vbi_count*2 lines, 720 pixel (= 1440 Bytes) */
	WRITE_RPS1((2 << 16) | (vbi_pixel_to_capture));
	/* load brs-control register */
	WRITE_RPS1(CMD_WR_REG | (1 << 8) | (BRS_CTRL/4));
	/* Set BRS right: note: this is an experimental value for BXO (=> PAL!) */
	WRITE_RPS1((540 << 7) | (5 << 19));  // 5 == vbi_start
	/* wait for brs_done */
	WRITE_RPS1(CMD_PAUSE | MASK_08);
	/* upload brs and video-dma3*/
	WRITE_RPS1(CMD_UPLOAD | MASK_08 | MASK_04);
	/* load mc2 register: enable dma3 */
	WRITE_RPS1(CMD_WR_REG | (1 << 8) | (MC1/4));
	WRITE_RPS1(MASK_20 | MASK_04);
	/* generate interrupt */
	WRITE_RPS1(CMD_INTERRUPT);
	/* stop rps1 */
	WRITE_RPS1(CMD_STOP);

	/* we have to do the workaround twice to be sure that
	   everything is ok */
	for(i = 0; i < 2; i++) {

		/* indicate to the irq handler that we do the workaround */
		saa7146_write(dev, MC2, MASK_31|MASK_15);

		saa7146_write(dev, NUM_LINE_BYTE3, (1<<16)|(2<<0));
		saa7146_write(dev, MC2, MASK_04|MASK_20);

		/* enable rps1 irqs */
		SAA7146_IER_ENABLE(dev,MASK_28);

		/* prepare to wait to be woken up by the irq-handler */
		add_wait_queue(&vv->vbi_wq, &wait);
		set_current_state(TASK_INTERRUPTIBLE);

		/* start rps1 to enable workaround */
		saa7146_write(dev, RPS_ADDR1, dev->d_rps1.dma_handle);
		saa7146_write(dev, MC1, (MASK_13 | MASK_29));

		schedule();

		DEB_VBI("brs bug workaround %d/1\n", i);

		remove_wait_queue(&vv->vbi_wq, &wait);
		__set_current_state(TASK_RUNNING);

		/* disable rps1 irqs */
		SAA7146_IER_DISABLE(dev,MASK_28);

		/* stop video-dma3 */
		saa7146_write(dev, MC1, MASK_20);

		if(signal_pending(current)) {

			DEB_VBI("aborted (rps:0x%08x)\n",
				saa7146_read(dev, RPS_ADDR1));

			/* stop rps1 for sure */
			saa7146_write(dev, MC1, MASK_29);

			dma_free_coherent(&dev->pci->dev, 4096, cpu, dma_addr);
			return -EINTR;
		}
	}

	dma_free_coherent(&dev->pci->dev, 4096, cpu, dma_addr);
	return 0;
}

static void saa7146_set_vbi_capture(struct saa7146_dev *dev, struct saa7146_buf *buf, struct saa7146_buf *next)
{
	struct saa7146_vv *vv = dev->vv_data;

	struct saa7146_video_dma vdma3;

	int count = 0;
	unsigned long e_wait = vv->current_hps_sync == SAA7146_HPS_SYNC_PORT_A ? CMD_E_FID_A : CMD_E_FID_B;
	unsigned long o_wait = vv->current_hps_sync == SAA7146_HPS_SYNC_PORT_A ? CMD_O_FID_A : CMD_O_FID_B;

/*
	vdma3.base_even	= 0xc8000000+2560*70;
	vdma3.base_odd	= 0xc8000000;
	vdma3.prot_addr	= 0xc8000000+2560*164;
	vdma3.pitch	= 2560;
	vdma3.base_page	= 0;
	vdma3.num_line_byte = (64<<16)|((vbi_pixel_to_capture)<<0); // set above!
*/
	vdma3.base_even	= buf->pt[2].offset;
	vdma3.base_odd	= buf->pt[2].offset + 16 * vbi_pixel_to_capture;
	vdma3.prot_addr	= buf->pt[2].offset + 16 * 2 * vbi_pixel_to_capture;
	vdma3.pitch	= vbi_pixel_to_capture;
	vdma3.base_page	= buf->pt[2].dma | ME1;
	vdma3.num_line_byte = (16 << 16) | vbi_pixel_to_capture;

	saa7146_write_out_dma(dev, 3, &vdma3);

	/* write beginning of rps-program */
	count = 0;

	/* wait for o_fid_a/b / e_fid_a/b toggle only if bit 1 is not set */

	/* we don't wait here for the first field anymore. this is different from the video
	   capture and might cause that the first buffer is only half filled (with only
	   one field). but since this is some sort of streaming data, this is not that negative.
	   but by doing this, we can use the whole engine from videobuf-dma-sg.c... */

/*
	WRITE_RPS1(CMD_PAUSE | CMD_OAN | CMD_SIG1 | e_wait);
	WRITE_RPS1(CMD_PAUSE | CMD_OAN | CMD_SIG1 | o_wait);
*/
	/* set bit 1 */
	WRITE_RPS1(CMD_WR_REG | (1 << 8) | (MC2/4));
	WRITE_RPS1(MASK_28 | MASK_12);

	/* turn on video-dma3 */
	WRITE_RPS1(CMD_WR_REG_MASK | (MC1/4));
	WRITE_RPS1(MASK_04 | MASK_20);			/* => mask */
	WRITE_RPS1(MASK_04 | MASK_20);			/* => values */

	/* wait for o_fid_a/b / e_fid_a/b toggle */
	WRITE_RPS1(CMD_PAUSE | o_wait);
	WRITE_RPS1(CMD_PAUSE | e_wait);

	/* generate interrupt */
	WRITE_RPS1(CMD_INTERRUPT);

	/* stop */
	WRITE_RPS1(CMD_STOP);

	/* enable rps1 irqs */
	SAA7146_IER_ENABLE(dev, MASK_28);

	/* write the address of the rps-program */
	saa7146_write(dev, RPS_ADDR1, dev->d_rps1.dma_handle);

	/* turn on rps */
	saa7146_write(dev, MC1, (MASK_13 | MASK_29));
}

static int buffer_activate(struct saa7146_dev *dev,
			   struct saa7146_buf *buf,
			   struct saa7146_buf *next)
{
	struct saa7146_vv *vv = dev->vv_data;

	DEB_VBI("dev:%p, buf:%p, next:%p\n", dev, buf, next);
	saa7146_set_vbi_capture(dev,buf,next);

	mod_timer(&vv->vbi_dmaq.timeout, jiffies+BUFFER_TIMEOUT);
	return 0;
}

/* ------------------------------------------------------------------ */

static int queue_setup(struct vb2_queue *q,
		       unsigned int *num_buffers, unsigned int *num_planes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	unsigned int size = 16 * 2 * vbi_pixel_to_capture;

	if (*num_planes)
		return sizes[0] < size ? -EINVAL : 0;
	*num_planes = 1;
	sizes[0] = size;

	return 0;
}

static void buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct saa7146_dev *dev = vb2_get_drv_priv(vq);
	struct saa7146_buf *buf = container_of(vbuf, struct saa7146_buf, vb);
	unsigned long flags;

	spin_lock_irqsave(&dev->slock, flags);

	saa7146_buffer_queue(dev, &dev->vv_data->vbi_dmaq, buf);
	spin_unlock_irqrestore(&dev->slock, flags);
}

static int buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct saa7146_buf *buf = container_of(vbuf, struct saa7146_buf, vb);
	struct sg_table *sgt = vb2_dma_sg_plane_desc(&buf->vb.vb2_buf, 0);
	struct scatterlist *list = sgt->sgl;
	int length = sgt->nents;
	struct vb2_queue *vq = vb->vb2_queue;
	struct saa7146_dev *dev = vb2_get_drv_priv(vq);
	int ret;

	buf->activate = buffer_activate;

	saa7146_pgtable_alloc(dev->pci, &buf->pt[2]);

	ret = saa7146_pgtable_build_single(dev->pci, &buf->pt[2],
					   list, length);
	if (ret)
		saa7146_pgtable_free(dev->pci, &buf->pt[2]);
	return ret;
}

static int buf_prepare(struct vb2_buffer *vb)
{
	unsigned int size = 16 * 2 * vbi_pixel_to_capture;

	if (vb2_plane_size(vb, 0) < size)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, size);
	return 0;
}

static void buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct saa7146_buf *buf = container_of(vbuf, struct saa7146_buf, vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct saa7146_dev *dev = vb2_get_drv_priv(vq);

	saa7146_pgtable_free(dev->pci, &buf->pt[2]);
}

static void return_buffers(struct vb2_queue *q, int state)
{
	struct saa7146_dev *dev = vb2_get_drv_priv(q);
	struct saa7146_dmaqueue *dq = &dev->vv_data->vbi_dmaq;
	struct saa7146_buf *buf;

	if (dq->curr) {
		buf = dq->curr;
		dq->curr = NULL;
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	while (!list_empty(&dq->queue)) {
		buf = list_entry(dq->queue.next, struct saa7146_buf, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
}

static void vbi_stop(struct saa7146_dev *dev)
{
	struct saa7146_vv *vv = dev->vv_data;
	unsigned long flags;
	DEB_VBI("dev:%p\n", dev);

	spin_lock_irqsave(&dev->slock,flags);

	/* disable rps1  */
	saa7146_write(dev, MC1, MASK_29);

	/* disable rps1 irqs */
	SAA7146_IER_DISABLE(dev, MASK_28);

	/* shut down dma 3 transfers */
	saa7146_write(dev, MC1, MASK_20);

	timer_delete(&vv->vbi_dmaq.timeout);
	timer_delete(&vv->vbi_read_timeout);

	spin_unlock_irqrestore(&dev->slock, flags);
}

static void vbi_read_timeout(struct timer_list *t)
{
	struct saa7146_vv *vv = timer_container_of(vv, t, vbi_read_timeout);
	struct saa7146_dev *dev = vv->vbi_dmaq.dev;

	DEB_VBI("dev:%p\n", dev);

	vbi_stop(dev);
}

static int vbi_begin(struct saa7146_dev *dev)
{
	struct saa7146_vv *vv = dev->vv_data;
	u32 arbtr_ctrl	= saa7146_read(dev, PCI_BT_V1);
	int ret = 0;

	DEB_VBI("dev:%p\n", dev);

	ret = saa7146_res_get(dev, RESOURCE_DMA3_BRS);
	if (0 == ret) {
		DEB_S("cannot get vbi RESOURCE_DMA3_BRS resource\n");
		return -EBUSY;
	}

	/* adjust arbitrition control for video dma 3 */
	arbtr_ctrl &= ~0x1f0000;
	arbtr_ctrl |=  0x1d0000;
	saa7146_write(dev, PCI_BT_V1, arbtr_ctrl);
	saa7146_write(dev, MC2, (MASK_04|MASK_20));

	vv->vbi_read_timeout.function = vbi_read_timeout;

	/* initialize the brs */
	if ( 0 != (SAA7146_USE_PORT_B_FOR_VBI & dev->ext_vv_data->flags)) {
		saa7146_write(dev, BRS_CTRL, MASK_30|MASK_29 | (7 << 19));
	} else {
		saa7146_write(dev, BRS_CTRL, 0x00000001);

		if (0 != (ret = vbi_workaround(dev))) {
			DEB_VBI("vbi workaround failed!\n");
			/* return ret;*/
		}
	}

	/* upload brs register */
	saa7146_write(dev, MC2, (MASK_08|MASK_24));
	return 0;
}

static int start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct saa7146_dev *dev = vb2_get_drv_priv(q);
	int ret;

	if (!vb2_is_streaming(&dev->vv_data->vbi_dmaq.q))
		dev->vv_data->seqnr = 0;
	ret = vbi_begin(dev);
	if (ret)
		return_buffers(q, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void stop_streaming(struct vb2_queue *q)
{
	struct saa7146_dev *dev = vb2_get_drv_priv(q);

	vbi_stop(dev);
	return_buffers(q, VB2_BUF_STATE_ERROR);
	saa7146_res_free(dev, RESOURCE_DMA3_BRS);
}

const struct vb2_ops vbi_qops = {
	.queue_setup	= queue_setup,
	.buf_queue	= buf_queue,
	.buf_init	= buf_init,
	.buf_prepare	= buf_prepare,
	.buf_cleanup	= buf_cleanup,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
};

/* ------------------------------------------------------------------ */

static void vbi_init(struct saa7146_dev *dev, struct saa7146_vv *vv)
{
	DEB_VBI("dev:%p\n", dev);

	INIT_LIST_HEAD(&vv->vbi_dmaq.queue);

	timer_setup(&vv->vbi_dmaq.timeout, saa7146_buffer_timeout, 0);
	vv->vbi_dmaq.dev              = dev;

	init_waitqueue_head(&vv->vbi_wq);
}

static void vbi_irq_done(struct saa7146_dev *dev, unsigned long status)
{
	struct saa7146_vv *vv = dev->vv_data;
	spin_lock(&dev->slock);

	if (vv->vbi_dmaq.curr) {
		DEB_VBI("dev:%p, curr:%p\n", dev, vv->vbi_dmaq.curr);
		saa7146_buffer_finish(dev, &vv->vbi_dmaq, VB2_BUF_STATE_DONE);
	} else {
		DEB_VBI("dev:%p\n", dev);
	}
	saa7146_buffer_next(dev, &vv->vbi_dmaq, 1);

	spin_unlock(&dev->slock);
}

const struct saa7146_use_ops saa7146_vbi_uops = {
	.init		= vbi_init,
	.irq_done	= vbi_irq_done,
};
