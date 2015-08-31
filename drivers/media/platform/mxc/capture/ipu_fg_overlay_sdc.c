/*
 * Copyright 2004-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */
/* * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file ipu_foreground_sdc.c
 *
 * @brief IPU Use case for PRP-VF
 *
 * @ingroup IPU
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/console.h>
#include <linux/ipu.h>
#include <linux/mxcfb.h>
#include <linux/mipi_csi2.h>

#include "mxc_v4l2_capture.h"
#include "ipu_prp_sw.h"

#ifdef CAMERA_DBG
	#define CAMERA_TRACE(x) (printk)x
#else
	#define CAMERA_TRACE(x)
#endif

static int csi_buffer_num, buffer_num;
static u32 csi_mem_bufsize;
static struct ipu_soc *disp_ipu;
static struct fb_info *fbi;
static struct fb_var_screeninfo fbvar;
static u32 vf_out_format;
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
	task.input.format = IPU_PIX_FMT_NV12;

	if (buffer_num == 0)
		task.output.paddr = fbi->fix.smem_start +
				(fbi->fix.line_length * fbvar.yres);
	else
		task.output.paddr = fbi->fix.smem_start;
	task.output.width = cam->win.w.width;
	task.output.height = cam->win.w.height;
	task.output.format = vf_out_format;
	task.output.rotate = cam->rotation;
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
	ipu_select_buffer(disp_ipu, MEM_FG_SYNC, IPU_INPUT_BUFFER, buffer_num);
	buffer_num = (buffer_num == 0) ? 1 : 0;
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

	if ((cam->crop_current.width != cam->win.w.width) ||
		(cam->crop_current.height != cam->win.w.height) ||
		(vf_out_format != IPU_PIX_FMT_NV12) ||
		(cam->rotation >= IPU_ROTATE_VERT_FLIP))
		schedule_work(&cam->csi_work_struct);
	csi_buffer_num = (csi_buffer_num == 0) ? 1 : 0;
	return IRQ_HANDLED;
}

static int csi_enc_setup(cam_data *cam)
{
	ipu_channel_params_t params;
	int err = 0, sensor_protocol = 0;
	ipu_channel_t chan = (cam->csi == 0) ? CSI_MEM0 : CSI_MEM1;
#ifdef CONFIG_MXC_MIPI_CSI2
	void *mipi_csi2_info;
	int ipu_id;
	int csi_id;
#endif

	CAMERA_TRACE("In csi_enc_setup\n");
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
	csi_mem_bufsize = cam->crop_current.width *
			  cam->crop_current.height * 3/2;
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

	if ((cam->crop_current.width == cam->win.w.width) &&
		(cam->crop_current.height == cam->win.w.height) &&
		(vf_out_format == IPU_PIX_FMT_NV12) &&
		(cam->rotation < IPU_ROTATE_VERT_FLIP)) {
		err = ipu_init_channel_buffer(cam->ipu, chan,
				IPU_OUTPUT_BUFFER, IPU_PIX_FMT_NV12,
				cam->crop_current.width,
				cam->crop_current.height,
				cam->crop_current.width, IPU_ROTATE_NONE,
				fbi->fix.smem_start +
				(fbi->fix.line_length * fbvar.yres),
				fbi->fix.smem_start, 0,
				cam->offset.u_offset, cam->offset.u_offset);
	} else {
		err = ipu_init_channel_buffer(cam->ipu, chan,
				IPU_OUTPUT_BUFFER, IPU_PIX_FMT_NV12,
				cam->crop_current.width,
				cam->crop_current.height,
				cam->crop_current.width, IPU_ROTATE_NONE,
				cam->vf_bufs[0], cam->vf_bufs[1], 0,
				cam->offset.u_offset, cam->offset.u_offset);
	}
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
	CAMERA_TRACE("IPU:In csi_enc_enabling_tasks\n");

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

/*
 * Function definitions
 */

/*!
 * foreground_start - start the vf task
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 */
static int foreground_start(void *private)
{
	cam_data *cam = (cam_data *) private;
	int err = 0, i = 0, screen_size;
	char *base;

	if (!cam) {
		printk(KERN_ERR "private is NULL\n");
		return -EIO;
	}

	if (cam->overlay_active == true) {
		pr_debug("already started.\n");
		return 0;
	}

	get_disp_ipu(cam);

	for (i = 0; i < num_registered_fb; i++) {
		char *idstr = registered_fb[i]->fix.id;
		if (((strcmp(idstr, "DISP3 FG") == 0) && (cam->output < 3)) ||
		    ((strcmp(idstr, "DISP4 FG") == 0) && (cam->output >= 3))) {
			fbi = registered_fb[i];
			break;
		}
	}

	if (fbi == NULL) {
		printk(KERN_ERR "DISP FG fb not found\n");
		return -EPERM;
	}

	fbvar = fbi->var;

	/* Store the overlay frame buffer's original std */
	cam->fb_origin_std = fbvar.nonstd;

	if (cam->devtype == IMX5_V4L2 || cam->devtype == IMX6_V4L2) {
		/* Use DP to do CSC so that we can get better performance */
		vf_out_format = IPU_PIX_FMT_NV12;
		fbvar.nonstd = vf_out_format;
	} else {
		vf_out_format = IPU_PIX_FMT_RGB565;
		fbvar.nonstd = 0;
	}

	fbvar.bits_per_pixel = 16;
	fbvar.xres = fbvar.xres_virtual = cam->win.w.width;
	fbvar.yres = cam->win.w.height;
	fbvar.yres_virtual = cam->win.w.height * 2;
	fbvar.yoffset = 0;
	fbvar.vmode &= ~FB_VMODE_YWRAP;
	fbvar.accel_flags = FB_ACCEL_DOUBLE_FLAG;
	fbvar.activate |= FB_ACTIVATE_FORCE;
	fb_set_var(fbi, &fbvar);

	ipu_disp_set_window_pos(disp_ipu, MEM_FG_SYNC, cam->win.w.left,
			cam->win.w.top);

	/* Fill black color for framebuffer */
	base = (char *) fbi->screen_base;
	screen_size = fbi->var.xres * fbi->var.yres;
	if (cam->devtype == IMX5_V4L2 || cam->devtype == IMX6_V4L2) {
		memset(base, 0, screen_size);
		base += screen_size;
		for (i = 0; i < screen_size / 2; i++, base++)
			*base = 0x80;
	} else {
		for (i = 0; i < screen_size * 2; i++, base++)
			*base = 0x00;
	}

	console_lock();
	fb_blank(fbi, FB_BLANK_UNBLANK);
	console_unlock();

	/* correct display ch buffer address */
	ipu_update_channel_buffer(disp_ipu, MEM_FG_SYNC, IPU_INPUT_BUFFER,
				0, fbi->fix.smem_start +
				(fbi->fix.line_length * fbvar.yres));
	ipu_update_channel_buffer(disp_ipu, MEM_FG_SYNC, IPU_INPUT_BUFFER,
					1, fbi->fix.smem_start);

	err = csi_enc_enabling_tasks(cam);
	if (err != 0) {
		printk(KERN_ERR "Error csi enc enable fail\n");
		return err;
	}

	cam->overlay_active = true;
	return err;

}

/*!
 * foreground_stop - stop the vf task
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 */
static int foreground_stop(void *private)
{
	cam_data *cam = (cam_data *) private;
	int err = 0, i = 0;
	struct fb_info *fbi = NULL;
	struct fb_var_screeninfo fbvar;
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
	buffer_num = 0;

	for (i = 0; i < num_registered_fb; i++) {
		char *idstr = registered_fb[i]->fix.id;
		if (((strcmp(idstr, "DISP3 FG") == 0) && (cam->output < 3)) ||
		    ((strcmp(idstr, "DISP4 FG") == 0) && (cam->output >= 3))) {
			fbi = registered_fb[i];
			break;
		}
	}

	if (fbi == NULL) {
		printk(KERN_ERR "DISP FG fb not found\n");
		return -EPERM;
	}

	console_lock();
	fb_blank(fbi, FB_BLANK_POWERDOWN);
	console_unlock();

	/* Set the overlay frame buffer std to what it is used to be */
	fbvar = fbi->var;
	fbvar.accel_flags = FB_ACCEL_TRIPLE_FLAG;
	fbvar.nonstd = cam->fb_origin_std;
	fbvar.activate |= FB_ACTIVATE_FORCE;
	fb_set_var(fbi, &fbvar);

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

	cam->overlay_active = false;
	return err;
}

/*!
 * Enable csi
 * @param private       struct cam_data * mxc capture instance
 *
 * @return  status
 */
static int foreground_enable_csi(void *private)
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
static int foreground_disable_csi(void *private)
{
	cam_data *cam = (cam_data *) private;

	/* free csi eof irq firstly.
	 * when disable csi, wait for idmac eof.
	 * it requests eof irq again */
	ipu_free_irq(cam->ipu, IPU_IRQ_CSI0_OUT_EOF + cam->csi, cam);

	return ipu_disable_csi(cam->ipu, cam->csi);
}

/*!
 * function to select foreground as the working path
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 * @return  status
 */
int foreground_sdc_select(void *private)
{
	cam_data *cam;
	int err = 0;
	if (private) {
		cam = (cam_data *) private;
		cam->vf_start_sdc = foreground_start;
		cam->vf_stop_sdc = foreground_stop;
		cam->vf_enable_csi = foreground_enable_csi;
		cam->vf_disable_csi = foreground_disable_csi;
		cam->overlay_active = false;
	} else
		err = -EIO;

	return err;
}
EXPORT_SYMBOL(foreground_sdc_select);

/*!
 * function to de-select foreground as the working path
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 * @return  int
 */
int foreground_sdc_deselect(void *private)
{
	cam_data *cam;

	if (private) {
		cam = (cam_data *) private;
		cam->vf_start_sdc = NULL;
		cam->vf_stop_sdc = NULL;
		cam->vf_enable_csi = NULL;
		cam->vf_disable_csi = NULL;
	}
	return 0;
}
EXPORT_SYMBOL(foreground_sdc_deselect);

/*!
 * Init viewfinder task.
 *
 * @return  Error code indicating success or failure
 */
__init int foreground_sdc_init(void)
{
	return 0;
}

/*!
 * Deinit viewfinder task.
 *
 * @return  Error code indicating success or failure
 */
void __exit foreground_sdc_exit(void)
{
}

module_init(foreground_sdc_init);
module_exit(foreground_sdc_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("IPU PRP VF SDC Driver");
MODULE_LICENSE("GPL");
