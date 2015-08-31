/*
 * Copyright 2004-2014 Freescale Semiconductor, Inc. All Rights Reserved.
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
 * @file ipu_prp_vf_sdc_bg.c
 *
 * @brief IPU Use case for PRP-VF back-ground
 *
 * @ingroup IPU
 */
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/ipu.h>
#include <linux/module.h>
#include <mach/mipi_csi2.h>
#include "mxc_v4l2_capture.h"
#include "ipu_prp_sw.h"

static int buffer_num;
static int buffer_ready;
static struct ipu_soc *disp_ipu;

static void get_disp_ipu(cam_data *cam)
{
	if (cam->output > 2)
		disp_ipu = ipu_get_soc(1); /* using DISP4 */
	else
		disp_ipu = ipu_get_soc(0);
}

/*
 * Function definitions
 */

/*!
 * SDC V-Sync callback function.
 *
 * @param irq       int irq line
 * @param dev_id    void * device id
 *
 * @return status   IRQ_HANDLED for handled
 */
static irqreturn_t prpvf_sdc_vsync_callback(int irq, void *dev_id)
{
	cam_data *cam = dev_id;
	if (buffer_ready > 0) {
		ipu_select_buffer(cam->ipu, MEM_ROT_VF_MEM,
				  IPU_OUTPUT_BUFFER, 0);
		buffer_ready--;
	}

	return IRQ_HANDLED;
}

/*!
 * VF EOF callback function.
 *
 * @param irq       int irq line
 * @param dev_id    void * device id
 *
 * @return status   IRQ_HANDLED for handled
 */
static irqreturn_t prpvf_vf_eof_callback(int irq, void *dev_id)
{
	cam_data *cam = dev_id;
	pr_debug("buffer_ready %d buffer_num %d\n", buffer_ready, buffer_num);

	ipu_select_buffer(cam->ipu, MEM_ROT_VF_MEM,
			  IPU_INPUT_BUFFER, buffer_num);
	buffer_num = (buffer_num == 0) ? 1 : 0;
	ipu_select_buffer(cam->ipu, CSI_PRP_VF_MEM,
			  IPU_OUTPUT_BUFFER, buffer_num);
	buffer_ready++;
	return IRQ_HANDLED;
}

/*!
 * prpvf_start - start the vf task
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 */
static int prpvf_start(void *private)
{
	cam_data *cam = (cam_data *) private;
	ipu_channel_params_t vf;
	u32 format;
	u32 offset;
	u32 bpp, size = 3;
	int err = 0;
#ifdef CONFIG_MXC_MIPI_CSI2
	void *mipi_csi2_info;
	int ipu_id;
	int csi_id;
#endif

	if (!cam) {
		printk(KERN_ERR "private is NULL\n");
		return -EIO;
	}

	if (cam->overlay_active == true) {
		pr_debug("already start.\n");
		return 0;
	}

	get_disp_ipu(cam);

	format = cam->v4l2_fb.fmt.pixelformat;
	if (cam->v4l2_fb.fmt.pixelformat == IPU_PIX_FMT_BGR24) {
		bpp = 3, size = 3;
		pr_info("BGR24\n");
	} else if (cam->v4l2_fb.fmt.pixelformat == IPU_PIX_FMT_RGB565) {
		bpp = 2, size = 2;
		pr_info("RGB565\n");
	} else if (cam->v4l2_fb.fmt.pixelformat == IPU_PIX_FMT_BGR32) {
		bpp = 4, size = 4;
		pr_info("BGR32\n");
	} else {
		printk(KERN_ERR
		       "unsupported fix format from the framebuffer.\n");
		return -EINVAL;
	}

	offset = cam->v4l2_fb.fmt.bytesperline * cam->win.w.top +
	    size * cam->win.w.left;

	if (cam->v4l2_fb.base == 0)
		printk(KERN_ERR "invalid frame buffer address.\n");
	else
		offset += (u32) cam->v4l2_fb.base;

	memset(&vf, 0, sizeof(ipu_channel_params_t));
	ipu_csi_get_window_size(cam->ipu, &vf.csi_prp_vf_mem.in_width,
				&vf.csi_prp_vf_mem.in_height, cam->csi);
	vf.csi_prp_vf_mem.in_pixel_fmt = IPU_PIX_FMT_UYVY;
	vf.csi_prp_vf_mem.out_width = cam->win.w.width;
	vf.csi_prp_vf_mem.out_height = cam->win.w.height;
	vf.csi_prp_vf_mem.csi = cam->csi;
	if (cam->vf_rotation >= IPU_ROTATE_90_RIGHT) {
		vf.csi_prp_vf_mem.out_width = cam->win.w.height;
		vf.csi_prp_vf_mem.out_height = cam->win.w.width;
	}
	vf.csi_prp_vf_mem.out_pixel_fmt = format;
	size = cam->win.w.width * cam->win.w.height * size;

#ifdef CONFIG_MXC_MIPI_CSI2
	mipi_csi2_info = mipi_csi2_get_info();

	if (mipi_csi2_info) {
		if (mipi_csi2_get_status(mipi_csi2_info)) {
			ipu_id = mipi_csi2_get_bind_ipu(mipi_csi2_info);
			csi_id = mipi_csi2_get_bind_csi(mipi_csi2_info);

			if (cam->ipu == ipu_get_soc(ipu_id)
				&& cam->csi == csi_id) {
				vf.csi_prp_vf_mem.mipi_en = true;
				vf.csi_prp_vf_mem.mipi_vc =
				mipi_csi2_get_virtual_channel(mipi_csi2_info);
				vf.csi_prp_vf_mem.mipi_id =
				mipi_csi2_get_datatype(mipi_csi2_info);

				mipi_csi2_pixelclk_enable(mipi_csi2_info);
			} else {
				vf.csi_prp_vf_mem.mipi_en = false;
				vf.csi_prp_vf_mem.mipi_vc = 0;
				vf.csi_prp_vf_mem.mipi_id = 0;
			}
		} else {
			vf.csi_prp_vf_mem.mipi_en = false;
			vf.csi_prp_vf_mem.mipi_vc = 0;
			vf.csi_prp_vf_mem.mipi_id = 0;
		}
	}
#endif

	err = ipu_init_channel(cam->ipu, CSI_PRP_VF_MEM, &vf);
	if (err != 0)
		goto out_4;

	if (cam->vf_bufs_vaddr[0]) {
		dma_free_coherent(0, cam->vf_bufs_size[0],
				  cam->vf_bufs_vaddr[0], cam->vf_bufs[0]);
	}
	if (cam->vf_bufs_vaddr[1]) {
		dma_free_coherent(0, cam->vf_bufs_size[1],
				  cam->vf_bufs_vaddr[1], cam->vf_bufs[1]);
	}
	cam->vf_bufs_size[0] = PAGE_ALIGN(size);
	cam->vf_bufs_vaddr[0] = (void *)dma_alloc_coherent(0,
							   cam->vf_bufs_size[0],
							   &cam->vf_bufs[0],
							   GFP_DMA |
							   GFP_KERNEL);
	if (cam->vf_bufs_vaddr[0] == NULL) {
		printk(KERN_ERR "Error to allocate vf buffer\n");
		err = -ENOMEM;
		goto out_3;
	}
	cam->vf_bufs_size[1] = PAGE_ALIGN(size);
	cam->vf_bufs_vaddr[1] = (void *)dma_alloc_coherent(0,
							   cam->vf_bufs_size[1],
							   &cam->vf_bufs[1],
							   GFP_DMA |
							   GFP_KERNEL);
	if (cam->vf_bufs_vaddr[1] == NULL) {
		printk(KERN_ERR "Error to allocate vf buffer\n");
		err = -ENOMEM;
		goto out_3;
	}

	err = ipu_init_channel_buffer(cam->ipu, CSI_PRP_VF_MEM,
				      IPU_OUTPUT_BUFFER,
				      format, vf.csi_prp_vf_mem.out_width,
				      vf.csi_prp_vf_mem.out_height,
				      vf.csi_prp_vf_mem.out_width,
				      IPU_ROTATE_NONE,
				      cam->vf_bufs[0],
				      cam->vf_bufs[1],
				      0, 0, 0);
	if (err != 0) {
		printk(KERN_ERR "Error initializing CSI_PRP_VF_MEM\n");
		goto out_3;
	}
	err = ipu_init_channel(cam->ipu, MEM_ROT_VF_MEM, NULL);
	if (err != 0) {
		printk(KERN_ERR "Error MEM_ROT_VF_MEM channel\n");
		goto out_3;
	}

	err = ipu_init_channel_buffer(cam->ipu, MEM_ROT_VF_MEM,
				      IPU_INPUT_BUFFER,
				      format, vf.csi_prp_vf_mem.out_width,
				      vf.csi_prp_vf_mem.out_height,
				      vf.csi_prp_vf_mem.out_width,
				      cam->vf_rotation,
				      cam->vf_bufs[0],
				      cam->vf_bufs[1],
				      0, 0, 0);
	if (err != 0) {
		printk(KERN_ERR "Error MEM_ROT_VF_MEM input buffer\n");
		goto out_2;
	}

	if (cam->vf_rotation >= IPU_ROTATE_90_RIGHT) {
		err = ipu_init_channel_buffer(cam->ipu, MEM_ROT_VF_MEM,
					      IPU_OUTPUT_BUFFER,
					      format,
					      vf.csi_prp_vf_mem.out_height,
					      vf.csi_prp_vf_mem.out_width,
					      cam->overlay_fb->var.xres * bpp,
					      IPU_ROTATE_NONE,
					      offset, 0, 0, 0, 0);

		if (err != 0) {
			printk(KERN_ERR "Error MEM_ROT_VF_MEM output buffer\n");
			goto out_2;
		}
	} else {
		err = ipu_init_channel_buffer(cam->ipu, MEM_ROT_VF_MEM,
					      IPU_OUTPUT_BUFFER,
					      format,
					      vf.csi_prp_vf_mem.out_width,
					      vf.csi_prp_vf_mem.out_height,
					      cam->overlay_fb->var.xres * bpp,
					      IPU_ROTATE_NONE,
					      offset, 0, 0, 0, 0);
		if (err != 0) {
			printk(KERN_ERR "Error MEM_ROT_VF_MEM output buffer\n");
			goto out_2;
		}
	}

	ipu_clear_irq(cam->ipu, IPU_IRQ_PRP_VF_OUT_EOF);
	err = ipu_request_irq(cam->ipu, IPU_IRQ_PRP_VF_OUT_EOF,
			      prpvf_vf_eof_callback,
			      0, "Mxc Camera", cam);
	if (err != 0) {
		printk(KERN_ERR
		       "Error registering IPU_IRQ_PRP_VF_OUT_EOF irq.\n");
		goto out_2;
	}

	ipu_clear_irq(disp_ipu, IPU_IRQ_BG_SF_END);
	err = ipu_request_irq(disp_ipu, IPU_IRQ_BG_SF_END,
			      prpvf_sdc_vsync_callback,
			      0, "Mxc Camera", cam);
	if (err != 0) {
		printk(KERN_ERR "Error registering IPU_IRQ_BG_SF_END irq.\n");
		goto out_1;
	}

	ipu_enable_channel(cam->ipu, CSI_PRP_VF_MEM);
	ipu_enable_channel(cam->ipu, MEM_ROT_VF_MEM);

	buffer_num = 0;
	buffer_ready = 0;
	ipu_select_buffer(cam->ipu, CSI_PRP_VF_MEM, IPU_OUTPUT_BUFFER, 0);

	cam->overlay_active = true;
	return err;

out_1:
	ipu_free_irq(cam->ipu, IPU_IRQ_PRP_VF_OUT_EOF, NULL);
out_2:
	ipu_uninit_channel(cam->ipu, MEM_ROT_VF_MEM);
out_3:
	ipu_uninit_channel(cam->ipu, CSI_PRP_VF_MEM);
out_4:
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
	return err;
}

/*!
 * prpvf_stop - stop the vf task
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 */
static int prpvf_stop(void *private)
{
	cam_data *cam = (cam_data *) private;
#ifdef CONFIG_MXC_MIPI_CSI2
	void *mipi_csi2_info;
	int ipu_id;
	int csi_id;
#endif

	if (cam->overlay_active == false)
		return 0;

	ipu_free_irq(disp_ipu, IPU_IRQ_BG_SF_END, cam);

	ipu_disable_channel(cam->ipu, CSI_PRP_VF_MEM, true);
	ipu_disable_channel(cam->ipu, MEM_ROT_VF_MEM, true);
	ipu_uninit_channel(cam->ipu, CSI_PRP_VF_MEM);
	ipu_uninit_channel(cam->ipu, MEM_ROT_VF_MEM);

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

	buffer_num = 0;
	buffer_ready = 0;
	cam->overlay_active = false;
	return 0;
}

/*!
 * Enable csi
 * @param private       struct cam_data * mxc capture instance
 *
 * @return  status
 */
static int prp_vf_enable_csi(void *private)
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
static int prp_vf_disable_csi(void *private)
{
	cam_data *cam = (cam_data *) private;

	/* free csi eof irq firstly.
	 * when disable csi, wait for idmac eof.
	 * it requests eof irq again */
	ipu_free_irq(cam->ipu, IPU_IRQ_PRP_VF_OUT_EOF, cam);

	return ipu_disable_csi(cam->ipu, cam->csi);
}

/*!
 * function to select PRP-VF as the working path
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 * @return  status
 */
int prp_vf_sdc_select_bg(void *private)
{
	cam_data *cam = (cam_data *) private;

	if (cam) {
		cam->vf_start_sdc = prpvf_start;
		cam->vf_stop_sdc = prpvf_stop;
		cam->vf_enable_csi = prp_vf_enable_csi;
		cam->vf_disable_csi = prp_vf_disable_csi;
		cam->overlay_active = false;
	}

	return 0;
}
EXPORT_SYMBOL(prp_vf_sdc_select_bg);

/*!
 * function to de-select PRP-VF as the working path
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 * @return  status
 */
int prp_vf_sdc_deselect_bg(void *private)
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
EXPORT_SYMBOL(prp_vf_sdc_deselect_bg);

/*!
 * Init viewfinder task.
 *
 * @return  Error code indicating success or failure
 */
__init int prp_vf_sdc_init_bg(void)
{
	return 0;
}

/*!
 * Deinit viewfinder task.
 *
 * @return  Error code indicating success or failure
 */
void __exit prp_vf_sdc_exit_bg(void)
{
}

module_init(prp_vf_sdc_init_bg);
module_exit(prp_vf_sdc_exit_bg);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("IPU PRP VF SDC Backgroud Driver");
MODULE_LICENSE("GPL");
