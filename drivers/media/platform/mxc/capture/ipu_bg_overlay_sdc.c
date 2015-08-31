
/*
 * Copyright 2004-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file ipu_bg_overlay_sdc_bg.c
 *
 * @brief IPU Use case for PRP-VF back-ground
 *
 * @ingroup IPU
 */
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/ipu.h>
#include <linux/mipi_csi2.h>
#include "mxc_v4l2_capture.h"
#include "ipu_prp_sw.h"

static int csi_buffer_num;
static u32 bpp, csi_mem_bufsize = 3;
static u32 out_format;
static struct ipu_soc *disp_ipu;
static u32 offset;

static void csi_buf_work_func(struct work_struct *work)
{
	int err = 0;
	cam_data *cam =
		container_of(work, struct _cam_data, csi_work_struct);

	struct ipu_task	task;
	memset(&task, 0, sizeof(task));

	if (csi_buffer_num)
		task.input.paddr = cam->vf_bufs[0];
	else
		task.input.paddr = cam->vf_bufs[1];
	task.input.width = cam->crop_current.width;
	task.input.height = cam->crop_current.height;
	task.input.format = IPU_PIX_FMT_UYVY;

	task.output.paddr = offset;
	task.output.width = cam->overlay_fb->var.xres;
	task.output.height = cam->overlay_fb->var.yres;
	task.output.format = out_format;
	task.output.rotate = cam->rotation;
	task.output.crop.pos.x = cam->win.w.left;
	task.output.crop.pos.y = cam->win.w.top;
	if (cam->win.w.width > 1024 || cam->win.w.height > 1024) {
		task.output.crop.w = cam->overlay_fb->var.xres;
		task.output.crop.h = cam->overlay_fb->var.yres;
	} else {
		task.output.crop.w = cam->win.w.width;
		task.output.crop.h = cam->win.w.height;
	}
again:
	err = ipu_check_task(&task);
	if (err != IPU_CHECK_OK) {
		if (err > IPU_CHECK_ERR_MIN) {
			if (err == IPU_CHECK_ERR_SPLIT_INPUTW_OVER) {
				task.input.crop.w -= 8;
				goto again;
			}
			if (err == IPU_CHECK_ERR_SPLIT_INPUTH_OVER) {
				task.input.crop.h -= 8;
				goto again;
			}
			if (err == IPU_CHECK_ERR_SPLIT_OUTPUTW_OVER) {
					task.output.width -= 8;
					task.output.crop.w = task.output.width;
				goto again;
			}
			if (err == IPU_CHECK_ERR_SPLIT_OUTPUTH_OVER) {
					task.output.height -= 8;
					task.output.crop.h = task.output.height;
				goto again;
			}
			printk(KERN_ERR "check ipu taks fail\n");
			return;
		}
		printk(KERN_ERR "check ipu taks fail\n");
		return;
	}
	err = ipu_queue_task(&task);
	if (err < 0)
		printk(KERN_ERR "queue ipu task error\n");
}

static void get_disp_ipu(cam_data *cam)
{
	if (cam->output > 2)
		disp_ipu = ipu_get_soc(1); /* using DISP4 */
	else
		disp_ipu = ipu_get_soc(0);
}


/*!
 * csi ENC callback function.
 *
 * @param irq       int irq line
 * @param dev_id    void * device id
 *
 * @return status   IRQ_HANDLED for handled
 */
static irqreturn_t csi_enc_callback(int irq, void *dev_id)
{
	cam_data *cam = (cam_data *) dev_id;
	ipu_channel_t chan = (irq == IPU_IRQ_CSI0_OUT_EOF) ?
		CSI_MEM0 : CSI_MEM1;

	ipu_select_buffer(cam->ipu, chan,
			IPU_OUTPUT_BUFFER, csi_buffer_num);
	schedule_work(&cam->csi_work_struct);
	csi_buffer_num = (csi_buffer_num == 0) ? 1 : 0;
	return IRQ_HANDLED;
}

static int csi_enc_setup(cam_data *cam)
{
	ipu_channel_params_t params;
	u32 pixel_fmt;
	int err = 0, sensor_protocol = 0;
	ipu_channel_t chan = (cam->csi == 0) ? CSI_MEM0 : CSI_MEM1;

#ifdef CONFIG_MXC_MIPI_CSI2
	void *mipi_csi2_info;
	int ipu_id;
	int csi_id;
#endif

	if (!cam) {
		printk(KERN_ERR "cam private is NULL\n");
		return -ENXIO;
	}

	memset(&params, 0, sizeof(ipu_channel_params_t));
	params.csi_mem.csi = cam->csi;

	sensor_protocol = ipu_csi_get_sensor_protocol(cam->ipu, cam->csi);
	switch (sensor_protocol) {
	case IPU_CSI_CLK_MODE_GATED_CLK:
	case IPU_CSI_CLK_MODE_NONGATED_CLK:
	case IPU_CSI_CLK_MODE_CCIR656_PROGRESSIVE:
	case IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_DDR:
	case IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_SDR:
		params.csi_mem.interlaced = false;
		break;
	case IPU_CSI_CLK_MODE_CCIR656_INTERLACED:
	case IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_DDR:
	case IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_SDR:
		params.csi_mem.interlaced = true;
		break;
	default:
		printk(KERN_ERR "sensor protocol unsupported\n");
		return -EINVAL;
	}

#ifdef CONFIG_MXC_MIPI_CSI2
	mipi_csi2_info = mipi_csi2_get_info();

	if (mipi_csi2_info) {
		if (mipi_csi2_get_status(mipi_csi2_info)) {
			ipu_id = mipi_csi2_get_bind_ipu(mipi_csi2_info);
			csi_id = mipi_csi2_get_bind_csi(mipi_csi2_info);

			if (cam->ipu == ipu_get_soc(ipu_id)
				&& cam->csi == csi_id) {
				params.csi_mem.mipi_en = true;
				params.csi_mem.mipi_vc =
				mipi_csi2_get_virtual_channel(mipi_csi2_info);
				params.csi_mem.mipi_id =
				mipi_csi2_get_datatype(mipi_csi2_info);

				mipi_csi2_pixelclk_enable(mipi_csi2_info);
			} else {
				params.csi_mem.mipi_en = false;
				params.csi_mem.mipi_vc = 0;
				params.csi_mem.mipi_id = 0;
			}
		} else {
			params.csi_mem.mipi_en = false;
			params.csi_mem.mipi_vc = 0;
			params.csi_mem.mipi_id = 0;
		}
	}
#endif

	if (cam->vf_bufs_vaddr[0]) {
		dma_free_coherent(0, cam->vf_bufs_size[0],
				  cam->vf_bufs_vaddr[0],
				  (dma_addr_t) cam->vf_bufs[0]);
	}
	if (cam->vf_bufs_vaddr[1]) {
		dma_free_coherent(0, cam->vf_bufs_size[1],
				  cam->vf_bufs_vaddr[1],
				  (dma_addr_t) cam->vf_bufs[1]);
	}
	csi_mem_bufsize =
		cam->crop_current.width * cam->crop_current.height * 2;
	cam->vf_bufs_size[0] = PAGE_ALIGN(csi_mem_bufsize);
	cam->vf_bufs_vaddr[0] = (void *)dma_alloc_coherent(0,
							   cam->vf_bufs_size[0],
							   (dma_addr_t *) &
							   cam->vf_bufs[0],
							   GFP_DMA |
							   GFP_KERNEL);
	if (cam->vf_bufs_vaddr[0] == NULL) {
		printk(KERN_ERR "Error to allocate vf buffer\n");
		err = -ENOMEM;
		goto out_2;
	}
	cam->vf_bufs_size[1] = PAGE_ALIGN(csi_mem_bufsize);
	cam->vf_bufs_vaddr[1] = (void *)dma_alloc_coherent(0,
							   cam->vf_bufs_size[1],
							   (dma_addr_t *) &
							   cam->vf_bufs[1],
							   GFP_DMA |
							   GFP_KERNEL);
	if (cam->vf_bufs_vaddr[1] == NULL) {
		printk(KERN_ERR "Error to allocate vf buffer\n");
		err = -ENOMEM;
		goto out_1;
	}
	pr_debug("vf_bufs %x %x\n", cam->vf_bufs[0], cam->vf_bufs[1]);

	err = ipu_init_channel(cam->ipu, chan, &params);
	if (err != 0) {
		printk(KERN_ERR "ipu_init_channel %d\n", err);
		goto out_1;
	}

	pixel_fmt = IPU_PIX_FMT_UYVY;
	err = ipu_init_channel_buffer(
		cam->ipu, chan, IPU_OUTPUT_BUFFER, pixel_fmt,
		cam->crop_current.width, cam->crop_current.height,
		cam->crop_current.width, IPU_ROTATE_NONE,
		cam->vf_bufs[0], cam->vf_bufs[1], 0,
		cam->offset.u_offset, cam->offset.u_offset);
	if (err != 0) {
		printk(KERN_ERR "CSI_MEM output buffer\n");
		goto out_1;
	}
	err = ipu_enable_channel(cam->ipu, chan);
	if (err < 0) {
		printk(KERN_ERR "ipu_enable_channel CSI_MEM\n");
		goto out_1;
	}

	csi_buffer_num = 0;

	ipu_select_buffer(cam->ipu, chan, IPU_OUTPUT_BUFFER, 0);
	ipu_select_buffer(cam->ipu, chan, IPU_OUTPUT_BUFFER, 1);
	return err;
out_1:
	if (cam->vf_bufs_vaddr[0]) {
		dma_free_coherent(0, cam->vf_bufs_size[0],
				  cam->vf_bufs_vaddr[0],
				  (dma_addr_t) cam->vf_bufs[0]);
		cam->vf_bufs_vaddr[0] = NULL;
		cam->vf_bufs[0] = 0;
	}
	if (cam->vf_bufs_vaddr[1]) {
		dma_free_coherent(0, cam->vf_bufs_size[1],
				  cam->vf_bufs_vaddr[1],
				  (dma_addr_t) cam->vf_bufs[1]);
		cam->vf_bufs_vaddr[1] = NULL;
		cam->vf_bufs[1] = 0;
	}
out_2:
	return err;
}

/*!
 * Enable encoder task
 * @param private       struct cam_data * mxc capture instance
 *
 * @return  status
 */
static int csi_enc_enabling_tasks(void *private)
{
	cam_data *cam = (cam_data *) private;
	int err = 0;

	ipu_clear_irq(cam->ipu, IPU_IRQ_CSI0_OUT_EOF + cam->csi);
	err = ipu_request_irq(cam->ipu, IPU_IRQ_CSI0_OUT_EOF + cam->csi,
			csi_enc_callback, 0, "Mxc Camera", cam);
	if (err != 0) {
		printk(KERN_ERR "Error registering CSI_OUT_EOF irq\n");
		return err;
	}

	INIT_WORK(&cam->csi_work_struct, csi_buf_work_func);

	err = csi_enc_setup(cam);
	if (err != 0) {
		printk(KERN_ERR "csi_enc_setup %d\n", err);
		goto out1;
	}

	return err;
out1:
	ipu_free_irq(cam->ipu, IPU_IRQ_CSI0_OUT_EOF + cam->csi, cam);
	return err;
}

/*!
 * bg_overlay_start - start the overlay task
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 */
static int bg_overlay_start(void *private)
{
	cam_data *cam = (cam_data *) private;
	int err = 0;

	if (!cam) {
		printk(KERN_ERR "private is NULL\n");
		return -EIO;
	}

	if (cam->overlay_active == true) {
		pr_debug("already start.\n");
		return 0;
	}

	get_disp_ipu(cam);

	out_format = cam->v4l2_fb.fmt.pixelformat;
	if (cam->v4l2_fb.fmt.pixelformat == IPU_PIX_FMT_BGR24) {
		bpp = 3, csi_mem_bufsize = 3;
		pr_info("BGR24\n");
	} else if (cam->v4l2_fb.fmt.pixelformat == IPU_PIX_FMT_RGB565) {
		bpp = 2, csi_mem_bufsize = 2;
		pr_info("RGB565\n");
	} else if (cam->v4l2_fb.fmt.pixelformat == IPU_PIX_FMT_BGR32) {
		bpp = 4, csi_mem_bufsize = 4;
		pr_info("BGR32\n");
	} else {
		printk(KERN_ERR
		       "unsupported fix format from the framebuffer.\n");
		return -EINVAL;
	}

	offset = cam->v4l2_fb.fmt.bytesperline * cam->win.w.top +
	    csi_mem_bufsize * cam->win.w.left;

	if (cam->v4l2_fb.base == 0)
		printk(KERN_ERR "invalid frame buffer address.\n");
	else
		offset += (u32) cam->v4l2_fb.base;

	csi_mem_bufsize = cam->win.w.width * cam->win.w.height
				* csi_mem_bufsize;

	err = csi_enc_enabling_tasks(cam);
	if (err != 0) {
		printk(KERN_ERR "Error csi enc enable fail\n");
		return err;
	}

	cam->overlay_active = true;
	return err;
}

/*!
 * bg_overlay_stop - stop the overlay task
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 */
static int bg_overlay_stop(void *private)
{
	int err = 0;
	cam_data *cam = (cam_data *) private;
	ipu_channel_t chan = (cam->csi == 0) ? CSI_MEM0 : CSI_MEM1;
#ifdef CONFIG_MXC_MIPI_CSI2
	void *mipi_csi2_info;
	int ipu_id;
	int csi_id;
#endif

	if (cam->overlay_active == false)
		return 0;

	err = ipu_disable_channel(cam->ipu, chan, true);

	ipu_uninit_channel(cam->ipu, chan);

	csi_buffer_num = 0;

#ifdef CONFIG_MXC_MIPI_CSI2
	mipi_csi2_info = mipi_csi2_get_info();

	if (mipi_csi2_info) {
		if (mipi_csi2_get_status(mipi_csi2_info)) {
			ipu_id = mipi_csi2_get_bind_ipu(mipi_csi2_info);
			csi_id = mipi_csi2_get_bind_csi(mipi_csi2_info);

			if (cam->ipu == ipu_get_soc(ipu_id)
				&& cam->csi == csi_id)
				mipi_csi2_pixelclk_disable(mipi_csi2_info);
		}
	}
#endif

	flush_work(&cam->csi_work_struct);
	cancel_work_sync(&cam->csi_work_struct);

	if (cam->vf_bufs_vaddr[0]) {
		dma_free_coherent(0, cam->vf_bufs_size[0],
				  cam->vf_bufs_vaddr[0], cam->vf_bufs[0]);
		cam->vf_bufs_vaddr[0] = NULL;
		cam->vf_bufs[0] = 0;
	}
	if (cam->vf_bufs_vaddr[1]) {
		dma_free_coherent(0, cam->vf_bufs_size[1],
				  cam->vf_bufs_vaddr[1], cam->vf_bufs[1]);
		cam->vf_bufs_vaddr[1] = NULL;
		cam->vf_bufs[1] = 0;
	}
	if (cam->rot_vf_bufs_vaddr[0]) {
		dma_free_coherent(0, cam->rot_vf_buf_size[0],
				  cam->rot_vf_bufs_vaddr[0],
				  cam->rot_vf_bufs[0]);
		cam->rot_vf_bufs_vaddr[0] = NULL;
		cam->rot_vf_bufs[0] = 0;
	}
	if (cam->rot_vf_bufs_vaddr[1]) {
		dma_free_coherent(0, cam->rot_vf_buf_size[1],
				  cam->rot_vf_bufs_vaddr[1],
				  cam->rot_vf_bufs[1]);
		cam->rot_vf_bufs_vaddr[1] = NULL;
		cam->rot_vf_bufs[1] = 0;
	}

	cam->overlay_active = false;
	return err;
}

/*!
 * Enable csi
 * @param private       struct cam_data * mxc capture instance
 *
 * @return  status
 */
static int bg_overlay_enable_csi(void *private)
{
	cam_data *cam = (cam_data *) private;

	return ipu_enable_csi(cam->ipu, cam->csi);
}

/*!
 * Disable csi
 * @param private       struct cam_data * mxc capture instance
 *
 * @return  status
 */
static int bg_overlay_disable_csi(void *private)
{
	cam_data *cam = (cam_data *) private;

	/* free csi eof irq firstly.
	 * when disable csi, wait for idmac eof.
	 * it requests eof irq again */
	ipu_free_irq(cam->ipu, IPU_IRQ_CSI0_OUT_EOF + cam->csi, cam);

	return ipu_disable_csi(cam->ipu, cam->csi);
}

/*!
 * function to select bg as the working path
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 * @return  status
 */
int bg_overlay_sdc_select(void *private)
{
	cam_data *cam = (cam_data *) private;

	if (cam) {
		cam->vf_start_sdc = bg_overlay_start;
		cam->vf_stop_sdc = bg_overlay_stop;
		cam->vf_enable_csi = bg_overlay_enable_csi;
		cam->vf_disable_csi = bg_overlay_disable_csi;
		cam->overlay_active = false;
	}

	return 0;
}
EXPORT_SYMBOL(bg_overlay_sdc_select);

/*!
 * function to de-select bg as the working path
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 * @return  status
 */
int bg_overlay_sdc_deselect(void *private)
{
	cam_data *cam = (cam_data *) private;

	if (cam) {
		cam->vf_start_sdc = NULL;
		cam->vf_stop_sdc = NULL;
		cam->vf_enable_csi = NULL;
		cam->vf_disable_csi = NULL;
	}
	return 0;
}
EXPORT_SYMBOL(bg_overlay_sdc_deselect);

/*!
 * Init background overlay task.
 *
 * @return  Error code indicating success or failure
 */
__init int bg_overlay_sdc_init(void)
{
	return 0;
}

/*!
 * Deinit background overlay task.
 *
 * @return  Error code indicating success or failure
 */
void __exit bg_overlay_sdc_exit(void)
{
}

module_init(bg_overlay_sdc_init);
module_exit(bg_overlay_sdc_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("IPU PRP VF SDC Backgroud Driver");
MODULE_LICENSE("GPL");
