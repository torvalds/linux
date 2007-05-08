/* via_dma.c -- DMA support for the VIA Unichrome/Pro
 *
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Copyright 2004 Digeo, Inc., Palo Alto, CA, U.S.A.
 * All Rights Reserved.
 *
 * Copyright 2004 The Unichrome project.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Tungsten Graphics,
 *    Erdi Chen,
 *    Thomas Hellstrom.
 */

#include "drmP.h"
#include "drm.h"
#include "via_drm.h"
#include "via_drv.h"
#include "via_3d_reg.h"

#define CMDBUF_ALIGNMENT_SIZE   (0x100)
#define CMDBUF_ALIGNMENT_MASK   (0x0ff)

/* defines for VIA 3D registers */
#define VIA_REG_STATUS          0x400
#define VIA_REG_TRANSET         0x43C
#define VIA_REG_TRANSPACE       0x440

/* VIA_REG_STATUS(0x400): Engine Status */
#define VIA_CMD_RGTR_BUSY       0x00000080	/* Command Regulator is busy */
#define VIA_2D_ENG_BUSY         0x00000001	/* 2D Engine is busy */
#define VIA_3D_ENG_BUSY         0x00000002	/* 3D Engine is busy */
#define VIA_VR_QUEUE_BUSY       0x00020000	/* Virtual Queue is busy */

#define SetReg2DAGP(nReg, nData) {				\
	*((uint32_t *)(vb)) = ((nReg) >> 2) | HALCYON_HEADER1;	\
	*((uint32_t *)(vb) + 1) = (nData);			\
	vb = ((uint32_t *)vb) + 2;				\
	dev_priv->dma_low +=8;					\
}

#define via_flush_write_combine() DRM_MEMORYBARRIER()

#define VIA_OUT_RING_QW(w1,w2)			\
	*vb++ = (w1);				\
	*vb++ = (w2);				\
	dev_priv->dma_low += 8;

static void via_cmdbuf_start(drm_via_private_t * dev_priv);
static void via_cmdbuf_pause(drm_via_private_t * dev_priv);
static void via_cmdbuf_reset(drm_via_private_t * dev_priv);
static void via_cmdbuf_rewind(drm_via_private_t * dev_priv);
static int via_wait_idle(drm_via_private_t * dev_priv);
static void via_pad_cache(drm_via_private_t * dev_priv, int qwords);

/*
 * Free space in command buffer.
 */

static uint32_t via_cmdbuf_space(drm_via_private_t * dev_priv)
{
	uint32_t agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	uint32_t hw_addr = *(dev_priv->hw_addr_ptr) - agp_base;

	return ((hw_addr <= dev_priv->dma_low) ?
		(dev_priv->dma_high + hw_addr - dev_priv->dma_low) :
		(hw_addr - dev_priv->dma_low));
}

/*
 * How much does the command regulator lag behind?
 */

static uint32_t via_cmdbuf_lag(drm_via_private_t * dev_priv)
{
	uint32_t agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	uint32_t hw_addr = *(dev_priv->hw_addr_ptr) - agp_base;

	return ((hw_addr <= dev_priv->dma_low) ?
		(dev_priv->dma_low - hw_addr) :
		(dev_priv->dma_wrap + dev_priv->dma_low - hw_addr));
}

/*
 * Check that the given size fits in the buffer, otherwise wait.
 */

static inline int
via_cmdbuf_wait(drm_via_private_t * dev_priv, unsigned int size)
{
	uint32_t agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	uint32_t cur_addr, hw_addr, next_addr;
	volatile uint32_t *hw_addr_ptr;
	uint32_t count;
	hw_addr_ptr = dev_priv->hw_addr_ptr;
	cur_addr = dev_priv->dma_low;
	next_addr = cur_addr + size + 512 * 1024;
	count = 1000000;
	do {
		hw_addr = *hw_addr_ptr - agp_base;
		if (count-- == 0) {
			DRM_ERROR
			    ("via_cmdbuf_wait timed out hw %x cur_addr %x next_addr %x\n",
			     hw_addr, cur_addr, next_addr);
			return -1;
		}
	} while ((cur_addr < hw_addr) && (next_addr >= hw_addr));
	return 0;
}

/*
 * Checks whether buffer head has reach the end. Rewind the ring buffer
 * when necessary.
 *
 * Returns virtual pointer to ring buffer.
 */

static inline uint32_t *via_check_dma(drm_via_private_t * dev_priv,
				      unsigned int size)
{
	if ((dev_priv->dma_low + size + 4 * CMDBUF_ALIGNMENT_SIZE) >
	    dev_priv->dma_high) {
		via_cmdbuf_rewind(dev_priv);
	}
	if (via_cmdbuf_wait(dev_priv, size) != 0) {
		return NULL;
	}

	return (uint32_t *) (dev_priv->dma_ptr + dev_priv->dma_low);
}

int via_dma_cleanup(drm_device_t * dev)
{
	if (dev->dev_private) {
		drm_via_private_t *dev_priv =
		    (drm_via_private_t *) dev->dev_private;

		if (dev_priv->ring.virtual_start) {
			via_cmdbuf_reset(dev_priv);

			drm_core_ioremapfree(&dev_priv->ring.map, dev);
			dev_priv->ring.virtual_start = NULL;
		}

	}

	return 0;
}

static int via_initialize(drm_device_t * dev,
			  drm_via_private_t * dev_priv,
			  drm_via_dma_init_t * init)
{
	if (!dev_priv || !dev_priv->mmio) {
		DRM_ERROR("via_dma_init called before via_map_init\n");
		return DRM_ERR(EFAULT);
	}

	if (dev_priv->ring.virtual_start != NULL) {
		DRM_ERROR("%s called again without calling cleanup\n",
			  __FUNCTION__);
		return DRM_ERR(EFAULT);
	}

	if (!dev->agp || !dev->agp->base) {
		DRM_ERROR("%s called with no agp memory available\n",
			  __FUNCTION__);
		return DRM_ERR(EFAULT);
	}

	if (dev_priv->chipset == VIA_DX9_0) {
		DRM_ERROR("AGP DMA is not supported on this chip\n");
		return DRM_ERR(EINVAL);
	}

	dev_priv->ring.map.offset = dev->agp->base + init->offset;
	dev_priv->ring.map.size = init->size;
	dev_priv->ring.map.type = 0;
	dev_priv->ring.map.flags = 0;
	dev_priv->ring.map.mtrr = 0;

	drm_core_ioremap(&dev_priv->ring.map, dev);

	if (dev_priv->ring.map.handle == NULL) {
		via_dma_cleanup(dev);
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return DRM_ERR(ENOMEM);
	}

	dev_priv->ring.virtual_start = dev_priv->ring.map.handle;

	dev_priv->dma_ptr = dev_priv->ring.virtual_start;
	dev_priv->dma_low = 0;
	dev_priv->dma_high = init->size;
	dev_priv->dma_wrap = init->size;
	dev_priv->dma_offset = init->offset;
	dev_priv->last_pause_ptr = NULL;
	dev_priv->hw_addr_ptr =
		(volatile uint32_t *)((char *)dev_priv->mmio->handle +
		init->reg_pause_addr);

	via_cmdbuf_start(dev_priv);

	return 0;
}

static int via_dma_init(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	drm_via_dma_init_t init;
	int retcode = 0;

	DRM_COPY_FROM_USER_IOCTL(init, (drm_via_dma_init_t __user *) data,
				 sizeof(init));

	switch (init.func) {
	case VIA_INIT_DMA:
		if (!DRM_SUSER(DRM_CURPROC))
			retcode = DRM_ERR(EPERM);
		else
			retcode = via_initialize(dev, dev_priv, &init);
		break;
	case VIA_CLEANUP_DMA:
		if (!DRM_SUSER(DRM_CURPROC))
			retcode = DRM_ERR(EPERM);
		else
			retcode = via_dma_cleanup(dev);
		break;
	case VIA_DMA_INITIALIZED:
		retcode = (dev_priv->ring.virtual_start != NULL) ?
			0 : DRM_ERR(EFAULT);
		break;
	default:
		retcode = DRM_ERR(EINVAL);
		break;
	}

	return retcode;
}

static int via_dispatch_cmdbuffer(drm_device_t * dev, drm_via_cmdbuffer_t * cmd)
{
	drm_via_private_t *dev_priv;
	uint32_t *vb;
	int ret;

	dev_priv = (drm_via_private_t *) dev->dev_private;

	if (dev_priv->ring.virtual_start == NULL) {
		DRM_ERROR("%s called without initializing AGP ring buffer.\n",
			  __FUNCTION__);
		return DRM_ERR(EFAULT);
	}

	if (cmd->size > VIA_PCI_BUF_SIZE) {
		return DRM_ERR(ENOMEM);
	}

	if (DRM_COPY_FROM_USER(dev_priv->pci_buf, cmd->buf, cmd->size))
		return DRM_ERR(EFAULT);

	/*
	 * Running this function on AGP memory is dead slow. Therefore
	 * we run it on a temporary cacheable system memory buffer and
	 * copy it to AGP memory when ready.
	 */

	if ((ret =
	     via_verify_command_stream((uint32_t *) dev_priv->pci_buf,
				       cmd->size, dev, 1))) {
		return ret;
	}

	vb = via_check_dma(dev_priv, (cmd->size < 0x100) ? 0x102 : cmd->size);
	if (vb == NULL) {
		return DRM_ERR(EAGAIN);
	}

	memcpy(vb, dev_priv->pci_buf, cmd->size);

	dev_priv->dma_low += cmd->size;

	/*
	 * Small submissions somehow stalls the CPU. (AGP cache effects?)
	 * pad to greater size.
	 */

	if (cmd->size < 0x100)
		via_pad_cache(dev_priv, (0x100 - cmd->size) >> 3);
	via_cmdbuf_pause(dev_priv);

	return 0;
}

int via_driver_dma_quiescent(drm_device_t * dev)
{
	drm_via_private_t *dev_priv = dev->dev_private;

	if (!via_wait_idle(dev_priv)) {
		return DRM_ERR(EBUSY);
	}
	return 0;
}

static int via_flush_ioctl(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;

	LOCK_TEST_WITH_RETURN(dev, filp);

	return via_driver_dma_quiescent(dev);
}

static int via_cmdbuffer(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_via_cmdbuffer_t cmdbuf;
	int ret;

	LOCK_TEST_WITH_RETURN(dev, filp);

	DRM_COPY_FROM_USER_IOCTL(cmdbuf, (drm_via_cmdbuffer_t __user *) data,
				 sizeof(cmdbuf));

	DRM_DEBUG("via cmdbuffer, buf %p size %lu\n", cmdbuf.buf, cmdbuf.size);

	ret = via_dispatch_cmdbuffer(dev, &cmdbuf);
	if (ret) {
		return ret;
	}

	return 0;
}

static int via_dispatch_pci_cmdbuffer(drm_device_t * dev,
				      drm_via_cmdbuffer_t * cmd)
{
	drm_via_private_t *dev_priv = dev->dev_private;
	int ret;

	if (cmd->size > VIA_PCI_BUF_SIZE) {
		return DRM_ERR(ENOMEM);
	}
	if (DRM_COPY_FROM_USER(dev_priv->pci_buf, cmd->buf, cmd->size))
		return DRM_ERR(EFAULT);

	if ((ret =
	     via_verify_command_stream((uint32_t *) dev_priv->pci_buf,
				       cmd->size, dev, 0))) {
		return ret;
	}

	ret =
	    via_parse_command_stream(dev, (const uint32_t *)dev_priv->pci_buf,
				     cmd->size);
	return ret;
}

static int via_pci_cmdbuffer(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_via_cmdbuffer_t cmdbuf;
	int ret;

	LOCK_TEST_WITH_RETURN(dev, filp);

	DRM_COPY_FROM_USER_IOCTL(cmdbuf, (drm_via_cmdbuffer_t __user *) data,
				 sizeof(cmdbuf));

	DRM_DEBUG("via_pci_cmdbuffer, buf %p size %lu\n", cmdbuf.buf,
		  cmdbuf.size);

	ret = via_dispatch_pci_cmdbuffer(dev, &cmdbuf);
	if (ret) {
		return ret;
	}

	return 0;
}

static inline uint32_t *via_align_buffer(drm_via_private_t * dev_priv,
					 uint32_t * vb, int qw_count)
{
	for (; qw_count > 0; --qw_count) {
		VIA_OUT_RING_QW(HC_DUMMY, HC_DUMMY);
	}
	return vb;
}

/*
 * This function is used internally by ring buffer mangement code.
 *
 * Returns virtual pointer to ring buffer.
 */
static inline uint32_t *via_get_dma(drm_via_private_t * dev_priv)
{
	return (uint32_t *) (dev_priv->dma_ptr + dev_priv->dma_low);
}

/*
 * Hooks a segment of data into the tail of the ring-buffer by
 * modifying the pause address stored in the buffer itself. If
 * the regulator has already paused, restart it.
 */
static int via_hook_segment(drm_via_private_t * dev_priv,
			    uint32_t pause_addr_hi, uint32_t pause_addr_lo,
			    int no_pci_fire)
{
	int paused, count;
	volatile uint32_t *paused_at = dev_priv->last_pause_ptr;
	uint32_t reader,ptr;

	paused = 0;
	via_flush_write_combine();
	(void) *(volatile uint32_t *)(via_get_dma(dev_priv) -1);
	*paused_at = pause_addr_lo;
	via_flush_write_combine();
	(void) *paused_at;
	reader = *(dev_priv->hw_addr_ptr);
	ptr = ((volatile char *)paused_at - dev_priv->dma_ptr) +
		dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr + 4;
	dev_priv->last_pause_ptr = via_get_dma(dev_priv) - 1;

	if ((ptr - reader) <= dev_priv->dma_diff ) {
		count = 10000000;
		while (!(paused = (VIA_READ(0x41c) & 0x80000000)) && count--);
	}

	if (paused && !no_pci_fire) {
		reader = *(dev_priv->hw_addr_ptr);
		if ((ptr - reader) == dev_priv->dma_diff) {

			/*
			 * There is a concern that these writes may stall the PCI bus
			 * if the GPU is not idle. However, idling the GPU first
			 * doesn't make a difference.
			 */

			VIA_WRITE(VIA_REG_TRANSET, (HC_ParaType_PreCR << 16));
			VIA_WRITE(VIA_REG_TRANSPACE, pause_addr_hi);
			VIA_WRITE(VIA_REG_TRANSPACE, pause_addr_lo);
			VIA_READ(VIA_REG_TRANSPACE);
		}
	}
	return paused;
}

static int via_wait_idle(drm_via_private_t * dev_priv)
{
	int count = 10000000;

	while (!(VIA_READ(VIA_REG_STATUS) & VIA_VR_QUEUE_BUSY) && count--);

	while (count-- && (VIA_READ(VIA_REG_STATUS) &
			   (VIA_CMD_RGTR_BUSY | VIA_2D_ENG_BUSY |
			    VIA_3D_ENG_BUSY))) ;
	return count;
}

static uint32_t *via_align_cmd(drm_via_private_t * dev_priv, uint32_t cmd_type,
			       uint32_t addr, uint32_t * cmd_addr_hi,
			       uint32_t * cmd_addr_lo, int skip_wait)
{
	uint32_t agp_base;
	uint32_t cmd_addr, addr_lo, addr_hi;
	uint32_t *vb;
	uint32_t qw_pad_count;

	if (!skip_wait)
		via_cmdbuf_wait(dev_priv, 2 * CMDBUF_ALIGNMENT_SIZE);

	vb = via_get_dma(dev_priv);
	VIA_OUT_RING_QW(HC_HEADER2 | ((VIA_REG_TRANSET >> 2) << 12) |
			(VIA_REG_TRANSPACE >> 2), HC_ParaType_PreCR << 16);
	agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	qw_pad_count = (CMDBUF_ALIGNMENT_SIZE >> 3) -
	    ((dev_priv->dma_low & CMDBUF_ALIGNMENT_MASK) >> 3);

	cmd_addr = (addr) ? addr :
	    agp_base + dev_priv->dma_low - 8 + (qw_pad_count << 3);
	addr_lo = ((HC_SubA_HAGPBpL << 24) | (cmd_type & HC_HAGPBpID_MASK) |
		   (cmd_addr & HC_HAGPBpL_MASK));
	addr_hi = ((HC_SubA_HAGPBpH << 24) | (cmd_addr >> 24));

	vb = via_align_buffer(dev_priv, vb, qw_pad_count - 1);
	VIA_OUT_RING_QW(*cmd_addr_hi = addr_hi, *cmd_addr_lo = addr_lo);
	return vb;
}

static void via_cmdbuf_start(drm_via_private_t * dev_priv)
{
	uint32_t pause_addr_lo, pause_addr_hi;
	uint32_t start_addr, start_addr_lo;
	uint32_t end_addr, end_addr_lo;
	uint32_t command;
	uint32_t agp_base;
	uint32_t ptr;
	uint32_t reader;
	int count;

	dev_priv->dma_low = 0;

	agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	start_addr = agp_base;
	end_addr = agp_base + dev_priv->dma_high;

	start_addr_lo = ((HC_SubA_HAGPBstL << 24) | (start_addr & 0xFFFFFF));
	end_addr_lo = ((HC_SubA_HAGPBendL << 24) | (end_addr & 0xFFFFFF));
	command = ((HC_SubA_HAGPCMNT << 24) | (start_addr >> 24) |
		   ((end_addr & 0xff000000) >> 16));

	dev_priv->last_pause_ptr =
	    via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0,
			  &pause_addr_hi, &pause_addr_lo, 1) - 1;

	via_flush_write_combine();
	(void) *(volatile uint32_t *)dev_priv->last_pause_ptr;

	VIA_WRITE(VIA_REG_TRANSET, (HC_ParaType_PreCR << 16));
	VIA_WRITE(VIA_REG_TRANSPACE, command);
	VIA_WRITE(VIA_REG_TRANSPACE, start_addr_lo);
	VIA_WRITE(VIA_REG_TRANSPACE, end_addr_lo);

	VIA_WRITE(VIA_REG_TRANSPACE, pause_addr_hi);
	VIA_WRITE(VIA_REG_TRANSPACE, pause_addr_lo);
	DRM_WRITEMEMORYBARRIER();
	VIA_WRITE(VIA_REG_TRANSPACE, command | HC_HAGPCMNT_MASK);
	VIA_READ(VIA_REG_TRANSPACE);

	dev_priv->dma_diff = 0;

	count = 10000000;
	while (!(VIA_READ(0x41c) & 0x80000000) && count--);

	reader = *(dev_priv->hw_addr_ptr);
	ptr = ((volatile char *)dev_priv->last_pause_ptr - dev_priv->dma_ptr) +
	    dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr + 4;

	/*
	 * This is the difference between where we tell the
	 * command reader to pause and where it actually pauses.
	 * This differs between hw implementation so we need to
	 * detect it.
	 */

	dev_priv->dma_diff = ptr - reader;
}

static void via_pad_cache(drm_via_private_t * dev_priv, int qwords)
{
	uint32_t *vb;

	via_cmdbuf_wait(dev_priv, qwords + 2);
	vb = via_get_dma(dev_priv);
	VIA_OUT_RING_QW(HC_HEADER2, HC_ParaType_NotTex << 16);
	via_align_buffer(dev_priv, vb, qwords);
}

static inline void via_dummy_bitblt(drm_via_private_t * dev_priv)
{
	uint32_t *vb = via_get_dma(dev_priv);
	SetReg2DAGP(0x0C, (0 | (0 << 16)));
	SetReg2DAGP(0x10, 0 | (0 << 16));
	SetReg2DAGP(0x0, 0x1 | 0x2000 | 0xAA000000);
}

static void via_cmdbuf_jump(drm_via_private_t * dev_priv)
{
	uint32_t agp_base;
	uint32_t pause_addr_lo, pause_addr_hi;
	uint32_t jump_addr_lo, jump_addr_hi;
	volatile uint32_t *last_pause_ptr;

	agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	via_align_cmd(dev_priv, HC_HAGPBpID_JUMP, 0, &jump_addr_hi,
		      &jump_addr_lo, 0);

	dev_priv->dma_wrap = dev_priv->dma_low;

	/*
	 * Wrap command buffer to the beginning.
	 */

	dev_priv->dma_low = 0;
	if (via_cmdbuf_wait(dev_priv, CMDBUF_ALIGNMENT_SIZE) != 0) {
		DRM_ERROR("via_cmdbuf_jump failed\n");
	}

	via_dummy_bitblt(dev_priv);
	via_dummy_bitblt(dev_priv);

	last_pause_ptr =
	    via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
			  &pause_addr_lo, 0) - 1;
	via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
		      &pause_addr_lo, 0);

	*last_pause_ptr = pause_addr_lo;

	via_hook_segment( dev_priv, jump_addr_hi, jump_addr_lo, 0);
}


static void via_cmdbuf_rewind(drm_via_private_t * dev_priv)
{
	via_cmdbuf_jump(dev_priv);
}

static void via_cmdbuf_flush(drm_via_private_t * dev_priv, uint32_t cmd_type)
{
	uint32_t pause_addr_lo, pause_addr_hi;

	via_align_cmd(dev_priv, cmd_type, 0, &pause_addr_hi, &pause_addr_lo, 0);
	via_hook_segment(dev_priv, pause_addr_hi, pause_addr_lo, 0);
}

static void via_cmdbuf_pause(drm_via_private_t * dev_priv)
{
	via_cmdbuf_flush(dev_priv, HC_HAGPBpID_PAUSE);
}

static void via_cmdbuf_reset(drm_via_private_t * dev_priv)
{
	via_cmdbuf_flush(dev_priv, HC_HAGPBpID_STOP);
	via_wait_idle(dev_priv);
}

/*
 * User interface to the space and lag functions.
 */

static int via_cmdbuf_size(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_via_cmdbuf_size_t d_siz;
	int ret = 0;
	uint32_t tmp_size, count;
	drm_via_private_t *dev_priv;

	DRM_DEBUG("via cmdbuf_size\n");
	LOCK_TEST_WITH_RETURN(dev, filp);

	dev_priv = (drm_via_private_t *) dev->dev_private;

	if (dev_priv->ring.virtual_start == NULL) {
		DRM_ERROR("%s called without initializing AGP ring buffer.\n",
			  __FUNCTION__);
		return DRM_ERR(EFAULT);
	}

	DRM_COPY_FROM_USER_IOCTL(d_siz, (drm_via_cmdbuf_size_t __user *) data,
				 sizeof(d_siz));

	count = 1000000;
	tmp_size = d_siz.size;
	switch (d_siz.func) {
	case VIA_CMDBUF_SPACE:
		while (((tmp_size = via_cmdbuf_space(dev_priv)) < d_siz.size)
		       && count--) {
			if (!d_siz.wait) {
				break;
			}
		}
		if (!count) {
			DRM_ERROR("VIA_CMDBUF_SPACE timed out.\n");
			ret = DRM_ERR(EAGAIN);
		}
		break;
	case VIA_CMDBUF_LAG:
		while (((tmp_size = via_cmdbuf_lag(dev_priv)) > d_siz.size)
		       && count--) {
			if (!d_siz.wait) {
				break;
			}
		}
		if (!count) {
			DRM_ERROR("VIA_CMDBUF_LAG timed out.\n");
			ret = DRM_ERR(EAGAIN);
		}
		break;
	default:
		ret = DRM_ERR(EFAULT);
	}
	d_siz.size = tmp_size;

	DRM_COPY_TO_USER_IOCTL((drm_via_cmdbuf_size_t __user *) data, d_siz,
			       sizeof(d_siz));
	return ret;
}

drm_ioctl_desc_t via_ioctls[] = {
	[DRM_IOCTL_NR(DRM_VIA_ALLOCMEM)] = {via_mem_alloc, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_VIA_FREEMEM)] = {via_mem_free, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_VIA_AGP_INIT)] = {via_agp_init, DRM_AUTH|DRM_MASTER},
	[DRM_IOCTL_NR(DRM_VIA_FB_INIT)] = {via_fb_init, DRM_AUTH|DRM_MASTER},
	[DRM_IOCTL_NR(DRM_VIA_MAP_INIT)] = {via_map_init, DRM_AUTH|DRM_MASTER},
	[DRM_IOCTL_NR(DRM_VIA_DEC_FUTEX)] = {via_decoder_futex, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_VIA_DMA_INIT)] = {via_dma_init, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_VIA_CMDBUFFER)] = {via_cmdbuffer, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_VIA_FLUSH)] = {via_flush_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_VIA_PCICMD)] = {via_pci_cmdbuffer, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_VIA_CMDBUF_SIZE)] = {via_cmdbuf_size, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_VIA_WAIT_IRQ)] = {via_wait_irq, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_VIA_DMA_BLIT)] = {via_dma_blit, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_VIA_BLIT_SYNC)] = {via_dma_blit_sync, DRM_AUTH}
};

int via_max_ioctl = DRM_ARRAY_SIZE(via_ioctls);
