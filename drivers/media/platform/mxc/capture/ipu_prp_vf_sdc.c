/*
 * Copyright 2004-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 */
/* * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file ipu_prp_vf_sdc.c
 *
 * @brief IPU Use case for PRP-VF
 *
 * @ingroup IPU
 */

#include <linux/dma-mapping.h>
#include <linux/console.h>
#include <linux/ipu.h>
#include <linux/module.h>
#include <linux/mxcfb.h>
#include <mach/hardware.h>
#include <mach/mipi_csi2.h>
#include "mxc_v4l2_capture.h"
#include "ipu_prp_sw.h"

static int buffer_num;
static struct ipu_soc *disp_ipu;

static void get_disp_ipu(cam_data *cam)
{
	if (cam->output > 2)
		disp_ipu = ipu_get_soc(1); /* using DISP4 */
	else
		disp_ipu = ipu_get_soc(0);
}

static irqreturn_t prpvf_rot_eof_callback(int irq, void *dev_id)
{
	cam_data *cam = dev_id;
	pr_debug("buffer_num %d\n",  buffer_num);

	if (cam->vf_rotation >= IPU_ROTATE_VERT_FLIP) {
		ipu_select_buffer(disp_ipu, MEM_FG_SYNC,
				  IPU_INPUT_BUFFER, buffer_num);
		buffer_num = (buffer_num == 0) ? 1 : 0;
		ipu_select_buffer(cam->ipu, MEM_ROT_VF_MEM,
				  IPU_OUTPUT_BUFFER, buffer_num);
	} else {
		ipu_select_buffer(disp_ipu, MEM_FG_SYNC,
				  IPU_INPUT_BUFFER, buffer_num);
		buffer_num = (buffer_num == 0) ? 1 : 0;
		ipu_select_buffer(cam->ipu, CSI_PRP_VF_MEM,
				  IPU_OUTPUT_BUFFER, buffer_num);
	}
	return IRQ_HANDLED;
}
/*
 * Function definitions
 */

/*!
 * prpvf_start - start the vf task
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 */
static int prpvf_start(void *private)
{
	struct fb_var_screeninfo fbvar;
	struct fb_info *fbi = NULL;
	cam_data *cam = (cam_data *) private;
	ipu_channel_params_t vf;
	u32 vf_out_format = 0;
	u32 size = 2, temp = 0;
	int err = 0, i = 0;
	short *tmp, color;
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
		vf_out_format = IPU_PIX_FMT_UYVY;
		fbvar.nonstd = vf_out_format;
		color = 0x80;
	} else {
		vf_out_format = IPU_PIX_FMT_RGB565;
		fbvar.nonstd = 0;
		color = 0x0;
	}

	fbvar.bits_per_pixel = 16;
	fbvar.xres = fbvar.xres_virtual = cam->win.w.width;
	fbvar.yres = cam->win.w.height;
	fbvar.yres_virtual = cam->win.w.height * 2;
	fbvar.yoffset = 0;
	fbvar.accel_flags = FB_ACCEL_DOUBLE_FLAG;
	fbvar.activate |= FB_ACTIVATE_FORCE;
	fb_set_var(fbi, &fbvar);

	ipu_disp_set_window_pos(disp_ipu, MEM_FG_SYNC, cam->win.w.left,
			cam->win.w.top);

	/* Fill black color for framebuffer */
	tmp = (short *) fbi->screen_base;
	for (i = 0; i < (fbi->fix.line_length * fbi->var.yres)/2;
			i++, tmp++)
		*tmp = color;

	console_lock();
	fb_blank(fbi, FB_BLANK_UNBLANK);
	console_unlock();

	/* correct display ch buffer address */
	ipu_update_channel_buffer(disp_ipu, MEM_FG_SYNC, IPU_INPUT_BUFFER,
				0, fbi->fix.smem_start +
				(fbi->fix.line_length * fbvar.yres));
	ipu_update_channel_buffer(disp_ipu, MEM_FG_SYNC, IPU_INPUT_BUFFER,
					1, fbi->fix.smem_start);

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
	vf.csi_prp_vf_mem.out_pixel_fmt = vf_out_format;
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
		goto out_5;

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
	cam->vf_bufs_size[0] = PAGE_ALIGN(size);
	cam->vf_bufs_vaddr[0] = (void *)dma_alloc_coherent(0,
							   cam->vf_bufs_size[0],
							   (dma_addr_t *) &
							   cam->vf_bufs[0],
							   GFP_DMA |
							   GFP_KERNEL);
	if (cam->vf_bufs_vaddr[0] == NULL) {
		printk(KERN_ERR "Error to allocate vf buffer\n");
		err = -ENOMEM;
		goto out_4;
	}
	cam->vf_bufs_size[1] = PAGE_ALIGN(size);
	cam->vf_bufs_vaddr[1] = (void *)dma_alloc_coherent(0,
							   cam->vf_bufs_size[1],
							   (dma_addr_t *) &
							   cam->vf_bufs[1],
							   GFP_DMA |
							   GFP_KERNEL);
	if (cam->vf_bufs_vaddr[1] == NULL) {
		printk(KERN_ERR "Error to allocate vf buffer\n");
		err = -ENOMEM;
		goto out_3;
	}
	pr_debug("vf_bufs %x %x\n", cam->vf_bufs[0], cam->vf_bufs[1]);

	if (cam->vf_rotation >= IPU_ROTATE_VERT_FLIP) {
		err = ipu_init_channel_buffer(cam->ipu, CSI_PRP_VF_MEM,
					      IPU_OUTPUT_BUFFER,
					      vf_out_format,
					      vf.csi_prp_vf_mem.out_width,
					      vf.csi_prp_vf_mem.out_height,
					      vf.csi_prp_vf_mem.out_width,
					      IPU_ROTATE_NONE,
					      cam->vf_bufs[0], cam->vf_bufs[1],
					      0, 0, 0);
		if (err != 0)
			goto out_3;

		err = ipu_init_channel(cam->ipu, MEM_ROT_VF_MEM, NULL);
		if (err != 0) {
			printk(KERN_ERR "Error MEM_ROT_VF_MEM channel\n");
			goto out_3;
		}

		err = ipu_init_channel_buffer(cam->ipu, MEM_ROT_VF_MEM,
					      IPU_INPUT_BUFFER,
					      vf_out_format,
					      vf.csi_prp_vf_mem.out_width,
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

		if (cam->vf_rotation < IPU_ROTATE_90_RIGHT) {
			temp = vf.csi_prp_vf_mem.out_width;
			vf.csi_prp_vf_mem.out_width =
						vf.csi_prp_vf_mem.out_height;
			vf.csi_prp_vf_mem.out_height = temp;
		}

		err = ipu_init_channel_buffer(cam->ipu, MEM_ROT_VF_MEM,
					      IPU_OUTPUT_BUFFER,
					      vf_out_format,
					      vf.csi_prp_vf_mem.out_height,
					      vf.csi_prp_vf_mem.out_width,
					      vf.csi_prp_vf_mem.out_height,
					      IPU_ROTATE_NONE,
					      fbi->fix.smem_start +
					      (fbi->fix.line_length *
					       fbi->var.yres),
					      fbi->fix.smem_start, 0, 0, 0);

		if (err != 0) {
			printk(KERN_ERR "Error MEM_ROT_VF_MEM output buffer\n");
			goto out_2;
		}

		ipu_clear_irq(cam->ipu, IPU_IRQ_PRP_VF_ROT_OUT_EOF);
		err = ipu_request_irq(cam->ipu, IPU_IRQ_PRP_VF_ROT_OUT_EOF,
				      prpvf_rot_eof_callback,
			      0, "Mxc Camera", cam);
		if (err < 0) {
			printk(KERN_ERR "Error request irq:IPU_IRQ_PRP_VF_ROT_OUT_EOF\n");
			goto out_2;
		}

		err = ipu_link_channels(cam->ipu,
					CSI_PRP_VF_MEM, MEM_ROT_VF_MEM);
		if (err < 0) {
			printk(KERN_ERR
			       "Error link CSI_PRP_VF_MEM-MEM_ROT_VF_MEM\n");
			goto out_1;
		}

		ipu_enable_channel(cam->ipu, CSI_PRP_VF_MEM);
		ipu_enable_channel(cam->ipu, MEM_ROT_VF_MEM);

		ipu_select_buffer(cam->ipu, CSI_PRP_VF_MEM,
				  IPU_OUTPUT_BUFFER, 0);
		ipu_select_buffer(cam->ipu, CSI_PRP_VF_MEM,
				  IPU_OUTPUT_BUFFER, 1);
		ipu_select_buffer(cam->ipu, MEM_ROT_VF_MEM,
				  IPU_OUTPUT_BUFFER, 0);
	} else {
		err = ipu_init_channel_buffer(cam->ipu, CSI_PRP_VF_MEM,
					      IPU_OUTPUT_BUFFER,
					      vf_out_format, cam->win.w.width,
					      cam->win.w.height,
					      cam->win.w.width,
					      cam->vf_rotation,
					      fbi->fix.smem_start +
					      (fbi->fix.line_length *
					       fbi->var.yres),
					      fbi->fix.smem_start, 0, 0, 0);
		if (err != 0) {
			printk(KERN_ERR "Error initializing CSI_PRP_VF_MEM\n");
			goto out_4;
		}
		ipu_clear_irq(cam->ipu, IPU_IRQ_PRP_VF_OUT_EOF);
		err = ipu_request_irq(cam->ipu, IPU_IRQ_PRP_VF_OUT_EOF,
				      prpvf_rot_eof_callback,
			      0, "Mxc Camera", cam);
		if (err < 0) {
			printk(KERN_ERR "Error request irq:IPU_IRQ_PRP_VF_OUT_EOF\n");
			goto out_4;
		}

		ipu_enable_channel(cam->ipu, CSI_PRP_VF_MEM);

		ipu_select_buffer(cam->ipu, CSI_PRP_VF_MEM,
				  IPU_OUTPUT_BUFFER, 0);
	}

	cam->overlay_active = true;
	return err;

out_1:
	ipu_free_irq(cam->ipu, IPU_IRQ_PRP_VF_OUT_EOF, NULL);
out_2:
	if (cam->vf_rotation >= IPU_ROTATE_VERT_FLIP)
		ipu_uninit_channel(cam->ipu, MEM_ROT_VF_MEM);
out_3:
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
out_4:
	ipu_uninit_channel(cam->ipu, CSI_PRP_VF_MEM);
out_5:
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
	int err = 0, i = 0;
	struct fb_info *fbi = NULL;
	struct fb_var_screeninfo fbvar;
#ifdef CONFIG_MXC_MIPI_CSI2
	void *mipi_csi2_info;
	int ipu_id;
	int csi_id;
#endif

	if (cam->overlay_active == false)
		return 0;

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

	if (cam->vf_rotation >= IPU_ROTATE_VERT_FLIP) {
		ipu_unlink_channels(cam->ipu, CSI_PRP_VF_MEM, MEM_ROT_VF_MEM);
		ipu_free_irq(cam->ipu, IPU_IRQ_PRP_VF_ROT_OUT_EOF, cam);
	}
	buffer_num = 0;

	ipu_disable_channel(cam->ipu, CSI_PRP_VF_MEM, true);

	if (cam->vf_rotation >= IPU_ROTATE_VERT_FLIP) {
		ipu_disable_channel(cam->ipu, MEM_ROT_VF_MEM, true);
		ipu_uninit_channel(cam->ipu, MEM_ROT_VF_MEM);
	}
	ipu_uninit_channel(cam->ipu, CSI_PRP_VF_MEM);

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
	if (cam->vf_rotation < IPU_ROTATE_VERT_FLIP)
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
int prp_vf_sdc_select(void *private)
{
	cam_data *cam;
	int err = 0;
	if (private) {
		cam = (cam_data *) private;
		cam->vf_start_sdc = prpvf_start;
		cam->vf_stop_sdc = prpvf_stop;
		cam->vf_enable_csi = prp_vf_enable_csi;
		cam->vf_disable_csi = prp_vf_disable_csi;
		cam->overlay_active = false;
	} else
		err = -EIO;

	return err;
}
EXPORT_SYMBOL(prp_vf_sdc_select);

/*!
 * function to de-select PRP-VF as the working path
 *
 * @param private    cam_data * mxc v4l2 main structure
 *
 * @return  int
 */
int prp_vf_sdc_deselect(void *private)
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
EXPORT_SYMBOL(prp_vf_sdc_deselect);

/*!
 * Init viewfinder task.
 *
 * @return  Error code indicating success or failure
 */
__init int prp_vf_sdc_init(void)
{
	return 0;
}

/*!
 * Deinit viewfinder task.
 *
 * @return  Error code indicating success or failure
 */
void __exit prp_vf_sdc_exit(void)
{
}

module_init(prp_vf_sdc_init);
module_exit(prp_vf_sdc_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("IPU PRP VF SDC Driver");
MODULE_LICENSE("GPL");
