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
 * @file ipu_prp_enc.c
 *
 * @brief IPU Use case for PRP-ENC
 *
 * @ingroup IPU
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/ipu.h>
#include <linux/mipi_csi2.h>
#include "mxc_v4l2_capture.h"
#include "ipu_prp_sw.h"

#ifdef CAMERA_DBG
	#define CAMERA_TRACE(x) (printk)x
#else
	#define CAMERA_TRACE(x)
#endif

static ipu_rotate_mode_t grotation = IPU_ROTATE_NONE;

/*
 * Function definitions
 */

/*!
 * IPU ENC callback function.
 *
 * @param irq       int irq line
 * @param dev_id    void * device id
 *
 * @return status   IRQ_HANDLED for handled
 */
static irqreturn_t prp_enc_callback(int irq, void *dev_id)
{
	cam_data *cam = (cam_data *) dev_id;

	if (cam->enc_callback == NULL)
		return IRQ_HANDLED;

	cam->enc_callback(irq, dev_id);

	return IRQ_HANDLED;
}

/*!
 * PrpENC enable channel setup function
 *
 * @param cam       struct cam_data * mxc capture instance
 *
 * @return  status
 */
static int prp_enc_setup(cam_data *cam)
{
	ipu_channel_params_t enc;
	int err = 0;
	dma_addr_t dummy = cam->dummy_frame.buffer.m.offset;
#ifdef CONFIG_MXC_MIPI_CSI2
	void *mipi_csi2_info;
	int ipu_id;
	int csi_id;
#endif

	CAMERA_TRACE("In prp_enc_setup\n");
	if (!cam) {
		printk(KERN_ERR "cam private is NULL\n");
		return -ENXIO;
	}
	memset(&enc, 0, sizeof(ipu_channel_params_t));

	ipu_csi_get_window_size(cam->ipu, &enc.csi_prp_enc_mem.in_width,
				&enc.csi_prp_enc_mem.in_height, cam->csi);

	enc.csi_prp_enc_mem.in_pixel_fmt = IPU_PIX_FMT_UYVY;
	enc.csi_prp_enc_mem.out_width = cam->v2f.fmt.pix.width;
	enc.csi_prp_enc_mem.out_height = cam->v2f.fmt.pix.height;
	enc.csi_prp_enc_mem.csi = cam->csi;
	if (cam->rotation >= IPU_ROTATE_90_RIGHT) {
		enc.csi_prp_enc_mem.out_width = cam->v2f.fmt.pix.height;
		enc.csi_prp_enc_mem.out_height = cam->v2f.fmt.pix.width;
	}

	if (cam->v2f.fmt.pix.pixelformat == V4L2_PIX_FMT_YUV420) {
		enc.csi_prp_enc_mem.out_pixel_fmt = IPU_PIX_FMT_YUV420P;
		pr_info("YUV420\n");
	} else if (cam->v2f.fmt.pix.pixelformat == V4L2_PIX_FMT_YVU420) {
		enc.csi_prp_enc_mem.out_pixel_fmt = IPU_PIX_FMT_YVU420P;
		pr_info("YVU420\n");
	} else if (cam->v2f.fmt.pix.pixelformat == V4L2_PIX_FMT_YUV422P) {
		enc.csi_prp_enc_mem.out_pixel_fmt = IPU_PIX_FMT_YUV422P;
		pr_info("YUV422P\n");
	} else if (cam->v2f.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
		enc.csi_prp_enc_mem.out_pixel_fmt = IPU_PIX_FMT_YUYV;
		pr_info("YUYV\n");
	} else if (cam->v2f.fmt.pix.pixelformat == V4L2_PIX_FMT_UYVY) {
		enc.csi_prp_enc_mem.out_pixel_fmt = IPU_PIX_FMT_UYVY;
		pr_info("UYVY\n");
	} else if (cam->v2f.fmt.pix.pixelformat == V4L2_PIX_FMT_NV12) {
		enc.csi_prp_enc_mem.out_pixel_fmt = IPU_PIX_FMT_NV12;
		pr_info("NV12\n");
	} else if (cam->v2f.fmt.pix.pixelformat == V4L2_PIX_FMT_BGR24) {
		enc.csi_prp_enc_mem.out_pixel_fmt = IPU_PIX_FMT_BGR24;
		pr_info("BGR24\n");
	} else if (cam->v2f.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24) {
		enc.csi_prp_enc_mem.out_pixel_fmt = IPU_PIX_FMT_RGB24;
		pr_info("RGB24\n");
	} else if (cam->v2f.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565) {
		enc.csi_prp_enc_mem.out_pixel_fmt = IPU_PIX_FMT_RGB565;
		pr_info("RGB565\n");
	} else if (cam->v2f.fmt.pix.pixelformat == V4L2_PIX_FMT_BGR32) {
		enc.csi_prp_enc_mem.out_pixel_fmt = IPU_PIX_FMT_BGR32;
		pr_info("BGR32\n");
	} else if (cam->v2f.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB32) {
		enc.csi_prp_enc_mem.out_pixel_fmt = IPU_PIX_FMT_RGB32;
		pr_info("RGB32\n");
	} else {
		printk(KERN_ERR "format not supported\n");
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
				enc.csi_prp_enc_mem.mipi_en = true;
				enc.csi_prp_enc_mem.mipi_vc =
				mipi_csi2_get_virtual_channel(mipi_csi2_info);
				enc.csi_prp_enc_mem.mipi_id =
				mipi_csi2_get_datatype(mipi_csi2_info);

				mipi_csi2_pixelclk_enable(mipi_csi2_info);
			} else {
				enc.csi_prp_enc_mem.mipi_en = false;
				enc.csi_prp_enc_mem.mipi_vc = 0;
				enc.csi_prp_enc_mem.mipi_id = 0;
			}
		} else {
			enc.csi_prp_enc_mem.mipi_en = false;
			enc.csi_prp_enc_mem.mipi_vc = 0;
			enc.csi_prp_enc_mem.mipi_id = 0;
		}
	}
#endif

	err = ipu_init_channel(cam->ipu, CSI_PRP_ENC_MEM, &enc);
	if (err != 0) {
		printk(KERN_ERR "ipu_init_channel %d\n", err);
		return err;
	}

	grotation = cam->rotation;
	if (cam->rotation >= IPU_ROTATE_90_RIGHT) {
		if (cam->rot_enc_bufs_vaddr[0]) {
			dma_free_coherent(0, cam->rot_enc_buf_size[0],
					  cam->rot_enc_bufs_vaddr[0],
					  cam->rot_enc_bufs[0]);
		}
		if (cam->rot_enc_bufs_vaddr[1]) {
			dma_free_coherent(0, cam->rot_enc_buf_size[1],
					  cam->rot_enc_bufs_vaddr[1],
					  cam->rot_enc_bufs[1]);
		}
		cam->rot_enc_buf_size[0] =
		    PAGE_ALIGN(cam->v2f.fmt.pix.sizeimage);
		cam->rot_enc_bufs_vaddr[0] =
		    (void *)dma_alloc_coherent(0, cam->rot_enc_buf_size[0],
					       &cam->rot_enc_bufs[0],
					       GFP_DMA | GFP_KERNEL);
		if (!cam->rot_enc_bufs_vaddr[0]) {
			printk(KERN_ERR "alloc enc_bufs0\n");
			return -ENOMEM;
		}
		cam->rot_enc_buf_size[1] =
		    PAGE_ALIGN(cam->v2f.fmt.pix.sizeimage);
		cam->rot_enc_bufs_vaddr[1] =
		    (void *)dma_alloc_coherent(0, cam->rot_enc_buf_size[1],
					       &cam->rot_enc_bufs[1],
					       GFP_DMA | GFP_KERNEL);
		if (!cam->rot_enc_bufs_vaddr[1]) {
			dma_free_coherent(0, cam->rot_enc_buf_size[0],
					  cam->rot_enc_bufs_vaddr[0],
					  cam->rot_enc_bufs[0]);
			cam->rot_enc_bufs_vaddr[0] = NULL;
			cam->rot_enc_bufs[0] = 0;
			printk(KERN_ERR "alloc enc_bufs1\n");
			return -ENOMEM;
		}

		err = ipu_init_channel_buffer(cam->ipu, CSI_PRP_ENC_MEM,
					      IPU_OUTPUT_BUFFER,
					      enc.csi_prp_enc_mem.out_pixel_fmt,
					      enc.csi_prp_enc_mem.out_width,
					      enc.csi_prp_enc_mem.out_height,
					      enc.csi_prp_enc_mem.out_width,
					      IPU_ROTATE_NONE,
					      cam->rot_enc_bufs[0],
					      cam->rot_enc_bufs[1], 0, 0, 0);
		if (err != 0) {
			printk(KERN_ERR "CSI_PRP_ENC_MEM err\n");
			return err;
		}

		err = ipu_init_channel(cam->ipu, MEM_ROT_ENC_MEM, NULL);
		if (err != 0) {
			printk(KERN_ERR "MEM_ROT_ENC_MEM channel err\n");
			return err;
		}

		err = ipu_init_channel_buffer(cam->ipu, MEM_ROT_ENC_MEM,
					      IPU_INPUT_BUFFER,
					      enc.csi_prp_enc_mem.out_pixel_fmt,
					      enc.csi_prp_enc_mem.out_width,
					      enc.csi_prp_enc_mem.out_height,
					      enc.csi_prp_enc_mem.out_width,
					      cam->rotation,
					      cam->rot_enc_bufs[0],
					      cam->rot_enc_bufs[1], 0, 0, 0);
		if (err != 0) {
			printk(KERN_ERR "MEM_ROT_ENC_MEM input buffer\n");
			return err;
		}

		err =
		    ipu_init_channel_buffer(cam->ipu, MEM_ROT_ENC_MEM,
					    IPU_OUTPUT_BUFFER,
					    enc.csi_prp_enc_mem.out_pixel_fmt,
					    enc.csi_prp_enc_mem.out_height,
					    enc.csi_prp_enc_mem.out_width,
					    cam->v2f.fmt.pix.bytesperline /
					    bytes_per_pixel(enc.csi_prp_enc_mem.
							    out_pixel_fmt),
					    IPU_ROTATE_NONE,
					    dummy, dummy, 0,
					    cam->offset.u_offset,
					    cam->offset.v_offset);
		if (err != 0) {
			printk(KERN_ERR "MEM_ROT_ENC_MEM output buffer\n");
			return err;
		}

		err = ipu_link_channels(cam->ipu,
					CSI_PRP_ENC_MEM, MEM_ROT_ENC_MEM);
		if (err < 0) {
			printk(KERN_ERR
			       "link CSI_PRP_ENC_MEM-MEM_ROT_ENC_MEM\n");
			return err;
		}

		err = ipu_enable_channel(cam->ipu, CSI_PRP_ENC_MEM);
		if (err < 0) {
			printk(KERN_ERR "ipu_enable_channel CSI_PRP_ENC_MEM\n");
			return err;
		}
		err = ipu_enable_channel(cam->ipu, MEM_ROT_ENC_MEM);
		if (err < 0) {
			printk(KERN_ERR "ipu_enable_channel MEM_ROT_ENC_MEM\n");
			return err;
		}

		ipu_select_buffer(cam->ipu, CSI_PRP_ENC_MEM,
				  IPU_OUTPUT_BUFFER, 0);
		ipu_select_buffer(cam->ipu, CSI_PRP_ENC_MEM,
				  IPU_OUTPUT_BUFFER, 1);
	} else {
		err =
		    ipu_init_channel_buffer(cam->ipu, CSI_PRP_ENC_MEM,
					    IPU_OUTPUT_BUFFER,
					    enc.csi_prp_enc_mem.out_pixel_fmt,
					    enc.csi_prp_enc_mem.out_width,
					    enc.csi_prp_enc_mem.out_height,
					    cam->v2f.fmt.pix.bytesperline /
					    bytes_per_pixel(enc.csi_prp_enc_mem.
							    out_pixel_fmt),
					    cam->rotation,
					    dummy, dummy, 0,
					    cam->offset.u_offset,
					    cam->offset.v_offset);
		if (err != 0) {
			printk(KERN_ERR "CSI_PRP_ENC_MEM output buffer\n");
			return err;
		}
		err = ipu_enable_channel(cam->ipu, CSI_PRP_ENC_MEM);
		if (err < 0) {
			printk(KERN_ERR "ipu_enable_channel CSI_PRP_ENC_MEM\n");
			return err;
		}
	}

	return err;
}

/*!
 * function to update physical buffer address for encorder IDMA channel
 *
 * @param private     pointer to cam_data structure
 * @param eba         physical buffer address for encorder IDMA channel
 *
 * @return  status
 */
static int prp_enc_eba_update(void *private, dma_addr_t eba)
{
	int err = 0;
	cam_data *cam = (cam_data *) private;
	struct ipu_soc *ipu = cam->ipu;
	int *buffer_num = &cam->ping_pong_csi;

	pr_debug("eba %x\n", eba);
	if (grotation >= IPU_ROTATE_90_RIGHT) {
		err = ipu_update_channel_buffer(ipu, MEM_ROT_ENC_MEM,
						IPU_OUTPUT_BUFFER, *buffer_num,
						eba);
	} else {
		err = ipu_update_channel_buffer(ipu, CSI_PRP_ENC_MEM,
						IPU_OUTPUT_BUFFER, *buffer_num,
						eba);
	}
	if (err != 0) {
		if (grotation >= IPU_ROTATE_90_RIGHT) {
			ipu_clear_buffer_ready(ipu, MEM_ROT_ENC_MEM,
					       IPU_OUTPUT_BUFFER,
					       *buffer_num);
			err = ipu_update_channel_buffer(ipu, MEM_ROT_ENC_MEM,
							IPU_OUTPUT_BUFFER,
							*buffer_num,
							eba);
		} else {
			ipu_clear_buffer_ready(ipu, CSI_PRP_ENC_MEM,
					       IPU_OUTPUT_BUFFER,
					       *buffer_num);
			err = ipu_update_channel_buffer(ipu, CSI_PRP_ENC_MEM,
							IPU_OUTPUT_BUFFER,
							*buffer_num,
							eba);
		}

		if (err != 0) {
			pr_err("ERROR: v4l2 capture: fail to update "
			       "buf%d\n", *buffer_num);
			return err;
		}
	}

	if (grotation >= IPU_ROTATE_90_RIGHT) {
		ipu_select_buffer(ipu, MEM_ROT_ENC_MEM, IPU_OUTPUT_BUFFER,
				  *buffer_num);
	} else {
		ipu_select_buffer(ipu, CSI_PRP_ENC_MEM, IPU_OUTPUT_BUFFER,
				  *buffer_num);
	}

	*buffer_num = (*buffer_num == 0) ? 1 : 0;
	return 0;
}

/*!
 * Enable encoder task
 * @param private       struct cam_data * mxc capture instance
 *
 * @return  status
 */
static int prp_enc_enabling_tasks(void *private)
{
	cam_data *cam = (cam_data *) private;
	int err = 0;
	CAMERA_TRACE("IPU:In prp_enc_enabling_tasks\n");

	cam->dummy_frame.vaddress = dma_alloc_coherent(0,
			       PAGE_ALIGN(cam->v2f.fmt.pix.sizeimage),
			       &cam->dummy_frame.paddress,
			       GFP_DMA | GFP_KERNEL);
	if (cam->dummy_frame.vaddress == 0) {
		pr_err("ERROR: v4l2 capture: Allocate dummy frame "
		       "failed.\n");
		return -ENOBUFS;
	}
	cam->dummy_frame.buffer.type = V4L2_BUF_TYPE_PRIVATE;
	cam->dummy_frame.buffer.length =
	    PAGE_ALIGN(cam->v2f.fmt.pix.sizeimage);
	cam->dummy_frame.buffer.m.offset = cam->dummy_frame.paddress;

	if (cam->rotation >= IPU_ROTATE_90_RIGHT) {
		err = ipu_request_irq(cam->ipu, IPU_IRQ_PRP_ENC_ROT_OUT_EOF,
				      prp_enc_callback, 0, "Mxc Camera", cam);
	} else {
		err = ipu_request_irq(cam->ipu, IPU_IRQ_PRP_ENC_OUT_EOF,
				      prp_enc_callback, 0, "Mxc Camera", cam);
	}
	if (err != 0) {
		printk(KERN_ERR "Error registering rot irq\n");
		return err;
	}

	err = prp_enc_setup(cam);
	if (err != 0) {
		printk(KERN_ERR "prp_enc_setup %d\n", err);
		return err;
	}

	return err;
}

/*!
 * Disable encoder task
 * @param private       struct cam_data * mxc capture instance
 *
 * @return  int
 */
static int prp_enc_disabling_tasks(void *private)
{
	cam_data *cam = (cam_data *) private;
	int err = 0;
#ifdef CONFIG_MXC_MIPI_CSI2
	void *mipi_csi2_info;
	int ipu_id;
	int csi_id;
#endif

	if (cam->rotation >= IPU_ROTATE_90_RIGHT) {
		ipu_free_irq(cam->ipu, IPU_IRQ_PRP_ENC_ROT_OUT_EOF, cam);
		ipu_unlink_channels(cam->ipu, CSI_PRP_ENC_MEM, MEM_ROT_ENC_MEM);
	}

	err = ipu_disable_channel(cam->ipu, CSI_PRP_ENC_MEM, true);
	if (cam->rotation >= IPU_ROTATE_90_RIGHT)
		err |= ipu_disable_channel(cam->ipu, MEM_ROT_ENC_MEM, true);

	ipu_uninit_channel(cam->ipu, CSI_PRP_ENC_MEM);
	if (cam->rotation >= IPU_ROTATE_90_RIGHT)
		ipu_uninit_channel(cam->ipu, MEM_ROT_ENC_MEM);

	if (cam->dummy_frame.vaddress != 0) {
		dma_free_coherent(0, cam->dummy_frame.buffer.length,
				  cam->dummy_frame.vaddress,
				  cam->dummy_frame.paddress);
		cam->dummy_frame.vaddress = 0;
	}

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

	return err;
}

/*!
 * Enable csi
 * @param private       struct cam_data * mxc capture instance
 *
 * @return  status
 */
static int prp_enc_enable_csi(void *private)
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
static int prp_enc_disable_csi(void *private)
{
	cam_data *cam = (cam_data *) private;

	/* free csi eof irq firstly.
	 * when disable csi, wait for idmac eof.
	 * it requests eof irq again */
	if (cam->rotation < IPU_ROTATE_90_RIGHT)
		ipu_free_irq(cam->ipu, IPU_IRQ_PRP_ENC_OUT_EOF, cam);

	return ipu_disable_csi(cam->ipu, cam->csi);
}

/*!
 * function to select PRP-ENC as the working path
 *
 * @param private       struct cam_data * mxc capture instance
 *
 * @return  int
 */
int prp_enc_select(void *private)
{
	cam_data *cam = (cam_data *) private;
	int err = 0;

	if (cam) {
		cam->enc_update_eba = prp_enc_eba_update;
		cam->enc_enable = prp_enc_enabling_tasks;
		cam->enc_disable = prp_enc_disabling_tasks;
		cam->enc_enable_csi = prp_enc_enable_csi;
		cam->enc_disable_csi = prp_enc_disable_csi;
	} else {
		err = -EIO;
	}

	return err;
}
EXPORT_SYMBOL(prp_enc_select);

/*!
 * function to de-select PRP-ENC as the working path
 *
 * @param private       struct cam_data * mxc capture instance
 *
 * @return  int
 */
int prp_enc_deselect(void *private)
{
	cam_data *cam = (cam_data *) private;
	int err = 0;

	if (cam) {
		cam->enc_update_eba = NULL;
		cam->enc_enable = NULL;
		cam->enc_disable = NULL;
		cam->enc_enable_csi = NULL;
		cam->enc_disable_csi = NULL;
		if (cam->rot_enc_bufs_vaddr[0]) {
			dma_free_coherent(0, cam->rot_enc_buf_size[0],
					  cam->rot_enc_bufs_vaddr[0],
					  cam->rot_enc_bufs[0]);
			cam->rot_enc_bufs_vaddr[0] = NULL;
			cam->rot_enc_bufs[0] = 0;
		}
		if (cam->rot_enc_bufs_vaddr[1]) {
			dma_free_coherent(0, cam->rot_enc_buf_size[1],
					  cam->rot_enc_bufs_vaddr[1],
					  cam->rot_enc_bufs[1]);
			cam->rot_enc_bufs_vaddr[1] = NULL;
			cam->rot_enc_bufs[1] = 0;
		}
	}

	return err;
}
EXPORT_SYMBOL(prp_enc_deselect);

/*!
 * Init the Encorder channels
 *
 * @return  Error code indicating success or failure
 */
__init int prp_enc_init(void)
{
	return 0;
}

/*!
 * Deinit the Encorder channels
 *
 */
void __exit prp_enc_exit(void)
{
}

module_init(prp_enc_init);
module_exit(prp_enc_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("IPU PRP ENC Driver");
MODULE_LICENSE("GPL");
