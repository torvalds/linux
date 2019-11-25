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

#include <linux/delay.h>
#include <linux/uaccess.h>

#include <drm/drm.h>
#include <drm/drm_agpsupport.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/via_drm.h>

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
	dev_priv->dma_low += 8;					\
}

#define via_flush_write_combine() mb()

#define VIA_OUT_RING_QW(w1, w2)	do {		\
	*vb++ = (w1);				\
	*vb++ = (w2);				\
	dev_priv->dma_low += 8;			\
} while (0)

static void via_cmdbuf_start(drm_via_private_t *dev_priv);
static void via_cmdbuf_pause(drm_via_private_t *dev_priv);
static void via_cmdbuf_reset(drm_via_private_t *dev_priv);
static void via_cmdbuf_rewind(drm_via_private_t *dev_priv);
static int via_wait_idle(drm_via_private_t *dev_priv);
static void via_pad_cache(drm_via_private_t *dev_priv, int qwords);

/*
 * Free space in command buffer.
 */

static uint32_t via_cmdbuf_space(drm_via_private_t *dev_priv)
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

static uint32_t via_cmdbuf_lag(drm_via_private_t *dev_priv)
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
via_cmdbuf_wait(drm_via_private_t *dev_priv, unsigned int size)
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
		if  ((cur_addr < hw_addr) && (next_addr >= hw_addr))
			msleep(1);
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
	if (via_cmdbuf_wait(dev_priv, size) != 0)
		return NULL;

	return (uint32_t *) (dev_priv->dma_ptr + dev_priv->dma_low);
}

int via_dma_cleanup(struct drm_device *dev)
{
	if (dev->dev_private) {
		drm_via_private_t *dev_priv =
		    (drm_via_private_t *) dev->dev_private;

		if (dev_priv->ring.virtual_start) {
			via_cmdbuf_reset(dev_priv);

			drm_legacy_ioremapfree(&dev_priv->ring.map, dev);
			dev_priv->ring.virtual_start = NULL;
		}

	}

	return 0;
}

static int via_initialize(struct drm_device *dev,
			  drm_via_private_t *dev_priv,
			  drm_via_dma_init_t *init)
{
	if (!dev_priv || !dev_priv->mmio) {
		DRM_ERROR("via_dma_init called before via_map_init\n");
		return -EFAULT;
	}

	if (dev_priv->ring.virtual_start != NULL) {
		DRM_ERROR("called again without calling cleanup\n");
		return -EFAULT;
	}

	if (!dev->agp || !dev->agp->base) {
		DRM_ERROR("called with no agp memory available\n");
		return -EFAULT;
	}

	if (dev_priv->chipset == VIA_DX9_0) {
		DRM_ERROR("AGP DMA is not supported on this chip\n");
		return -EINVAL;
	}

	dev_priv->ring.map.offset = dev->agp->base + init->offset;
	dev_priv->ring.map.size = init->size;
	dev_priv->ring.map.type = 0;
	dev_priv->ring.map.flags = 0;
	dev_priv->ring.map.mtrr = 0;

	drm_legacy_ioremap(&dev_priv->ring.map, dev);

	if (dev_priv->ring.map.handle == NULL) {
		via_dma_cleanup(dev);
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return -ENOMEM;
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

static int via_dma_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	drm_via_dma_init_t *init = data;
	int retcode = 0;

	switch (init->func) {
	case VIA_INIT_DMA:
		if (!capable(CAP_SYS_ADMIN))
			retcode = -EPERM;
		else
			retcode = via_initialize(dev, dev_priv, init);
		break;
	case VIA_CLEANUP_DMA:
		if (!capable(CAP_SYS_ADMIN))
			retcode = -EPERM;
		else
			retcode = via_dma_cleanup(dev);
		break;
	case VIA_DMA_INITIALIZED:
		retcode = (dev_priv->ring.virtual_start != NULL) ?
			0 : -EFAULT;
		break;
	default:
		retcode = -EINVAL;
		break;
	}

	return retcode;
}

static int via_dispatch_cmdbuffer(struct drm_device *dev, drm_via_cmdbuffer_t *cmd)
{
	drm_via_private_t *dev_priv;
	uint32_t *vb;
	int ret;

	dev_priv = (drm_via_private_t *) dev->dev_private;

	if (dev_priv->ring.virtual_start == NULL) {
		DRM_ERROR("called without initializing AGP ring buffer.\n");
		return -EFAULT;
	}

	if (cmd->size > VIA_PCI_BUF_SIZE)
		return -ENOMEM;

	if (copy_from_user(dev_priv->pci_buf, cmd->buf, cmd->size))
		return -EFAULT;

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
	if (vb == NULL)
		return -EAGAIN;

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

int via_driver_dma_quiescent(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = dev->dev_private;

	if (!via_wait_idle(dev_priv))
		return -EBUSY;
	return 0;
}

static int via_flush_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	return via_driver_dma_quiescent(dev);
}

static int via_cmdbuffer(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_cmdbuffer_t *cmdbuf = data;
	int ret;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_DEBUG("buf %p size %lu\n", cmdbuf->buf, cmdbuf->size);

	ret = via_dispatch_cmdbuffer(dev, cmdbuf);
	return ret;
}

static int via_dispatch_pci_cmdbuffer(struct drm_device *dev,
				      drm_via_cmdbuffer_t *cmd)
{
	drm_via_private_t *dev_priv = dev->dev_private;
	int ret;

	if (cmd->size > VIA_PCI_BUF_SIZE)
		return -ENOMEM;
	if (copy_from_user(dev_priv->pci_buf, cmd->buf, cmd->size))
		return -EFAULT;

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

static int via_pci_cmdbuffer(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_cmdbuffer_t *cmdbuf = data;
	int ret;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_DEBUG("buf %p size %lu\n", cmdbuf->buf, cmdbuf->size);

	ret = via_dispatch_pci_cmdbuffer(dev, cmdbuf);
	return ret;
}

static inline uint32_t *via_align_buffer(drm_via_private_t *dev_priv,
					 uint32_t * vb, int qw_count)
{
	for (; qw_count > 0; --qw_count)
		VIA_OUT_RING_QW(HC_DUMMY, HC_DUMMY);
	return vb;
}

/*
 * This function is used internally by ring buffer management code.
 *
 * Returns virtual pointer to ring buffer.
 */
static inline uint32_t *via_get_dma(drm_via_private_t *dev_priv)
{
	return (uint32_t *) (dev_priv->dma_ptr + dev_priv->dma_low);
}

/*
 * Hooks a segment of data into the tail of the ring-buffer by
 * modifying the pause address stored in the buffer itself. If
 * the regulator has already paused, restart it.
 */
static int via_hook_segment(drm_via_private_t *dev_priv,
			    uint32_t pause_addr_hi, uint32_t pause_addr_lo,
			    int no_pci_fire)
{
	int paused, count;
	volatile uint32_t *paused_at = dev_priv->last_pause_ptr;
	uint32_t reader, ptr;
	uint32_t diff;

	paused = 0;
	via_flush_write_combine();
	(void) *(volatile uint32_t *)(via_get_dma(dev_priv) - 1);

	*paused_at = pause_addr_lo;
	via_flush_write_combine();
	(void) *paused_at;

	reader = *(dev_priv->hw_addr_ptr);
	ptr = ((volatile char *)paused_at - dev_priv->dma_ptr) +
		dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr + 4;

	dev_priv->last_pause_ptr = via_get_dma(dev_priv) - 1;

	/*
	 * If there is a possibility that the command reader will
	 * miss the new pause address and pause on the old one,
	 * In that case we need to program the new start address
	 * using PCI.
	 */

	diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
	count = 10000000;
	while (diff == 0 && count--) {
		paused = (via_read(dev_priv, 0x41c) & 0x80000000);
		if (paused)
			break;
		reader = *(dev_priv->hw_addr_ptr);
		diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
	}

	paused = via_read(dev_priv, 0x41c) & 0x80000000;

	if (paused && !no_pci_fire) {
		reader = *(dev_priv->hw_addr_ptr);
		diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
		diff &= (dev_priv->dma_high - 1);
		if (diff != 0 && diff < (dev_priv->dma_high >> 1)) {
			DRM_ERROR("Paused at incorrect address. "
				  "0x%08x, 0x%08x 0x%08x\n",
				  ptr, reader, dev_priv->dma_diff);
		} else if (diff == 0) {
			/*
			 * There is a concern that these writes may stall the PCI bus
			 * if the GPU is not idle. However, idling the GPU first
			 * doesn't make a difference.
			 */

			via_write(dev_priv, VIA_REG_TRANSET, (HC_ParaType_PreCR << 16));
			via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_hi);
			via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_lo);
			via_read(dev_priv, VIA_REG_TRANSPACE);
		}
	}
	return paused;
}

static int via_wait_idle(drm_via_private_t *dev_priv)
{
	int count = 10000000;

	while (!(via_read(dev_priv, VIA_REG_STATUS) & VIA_VR_QUEUE_BUSY) && --count)
		;

	while (count && (via_read(dev_priv, VIA_REG_STATUS) &
			   (VIA_CMD_RGTR_BUSY | VIA_2D_ENG_BUSY |
			    VIA_3D_ENG_BUSY)))
		--count;
	return count;
}

static uint32_t *via_align_cmd(drm_via_private_t *dev_priv, uint32_t cmd_type,
			       uint32_t addr, uint32_t *cmd_addr_hi,
			       uint32_t *cmd_addr_lo, int skip_wait)
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

static void via_cmdbuf_start(drm_via_private_t *dev_priv)
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

	via_write(dev_priv, VIA_REG_TRANSET, (HC_ParaType_PreCR << 16));
	via_write(dev_priv, VIA_REG_TRANSPACE, command);
	via_write(dev_priv, VIA_REG_TRANSPACE, start_addr_lo);
	via_write(dev_priv, VIA_REG_TRANSPACE, end_addr_lo);

	via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_hi);
	via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_lo);
	wmb();
	via_write(dev_priv, VIA_REG_TRANSPACE, command | HC_HAGPCMNT_MASK);
	via_read(dev_priv, VIA_REG_TRANSPACE);

	dev_priv->dma_diff = 0;

	count = 10000000;
	while (!(via_read(dev_priv, 0x41c) & 0x80000000) && count--);

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

static void via_pad_cache(drm_via_private_t *dev_priv, int qwords)
{
	uint32_t *vb;

	via_cmdbuf_wait(dev_priv, qwords + 2);
	vb = via_get_dma(dev_priv);
	VIA_OUT_RING_QW(HC_HEADER2, HC_ParaType_NotTex << 16);
	via_align_buffer(dev_priv, vb, qwords);
}

static inline void via_dummy_bitblt(drm_via_private_t *dev_priv)
{
	uint32_t *vb = via_get_dma(dev_priv);
	SetReg2DAGP(0x0C, (0 | (0 << 16)));
	SetReg2DAGP(0x10, 0 | (0 << 16));
	SetReg2DAGP(0x0, 0x1 | 0x2000 | 0xAA000000);
}

static void via_cmdbuf_jump(drm_via_private_t *dev_priv)
{
	uint32_t agp_base;
	uint32_t pause_addr_lo, pause_addr_hi;
	uint32_t jump_addr_lo, jump_addr_hi;
	volatile uint32_t *last_pause_ptr;
	uint32_t dma_low_save1, dma_low_save2;

	agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	via_align_cmd(dev_priv, HC_HAGPBpID_JUMP, 0, &jump_addr_hi,
		      &jump_addr_lo, 0);

	dev_priv->dma_wrap = dev_priv->dma_low;

	/*
	 * Wrap command buffer to the beginning.
	 */

	dev_priv->dma_low = 0;
	if (via_cmdbuf_wait(dev_priv, CMDBUF_ALIGNMENT_SIZE) != 0)
		DRM_ERROR("via_cmdbuf_jump failed\n");

	via_dummy_bitblt(dev_priv);
	via_dummy_bitblt(dev_priv);

	last_pause_ptr =
	    via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
			  &pause_addr_lo, 0) - 1;
	via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
		      &pause_addr_lo, 0);

	*last_pause_ptr = pause_addr_lo;
	dma_low_save1 = dev_priv->dma_low;

	/*
	 * Now, set a trap that will pause the regulator if it tries to rerun the old
	 * command buffer. (Which may happen if via_hook_segment detecs a command regulator pause
	 * and reissues the jump command over PCI, while the regulator has already taken the jump
	 * and actually paused at the current buffer end).
	 * There appears to be no other way to detect this condition, since the hw_addr_pointer
	 * does not seem to get updated immediately when a jump occurs.
	 */

	last_pause_ptr =
		via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
			      &pause_addr_lo, 0) - 1;
	via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
		      &pause_addr_lo, 0);
	*last_pause_ptr = pause_addr_lo;

	dma_low_save2 = dev_priv->dma_low;
	dev_priv->dma_low = dma_low_save1;
	via_hook_segment(dev_priv, jump_addr_hi, jump_addr_lo, 0);
	dev_priv->dma_low = dma_low_save2;
	via_hook_segment(dev_priv, pause_addr_hi, pause_addr_lo, 0);
}


static void via_cmdbuf_rewind(drm_via_private_t *dev_priv)
{
	via_cmdbuf_jump(dev_priv);
}

static void via_cmdbuf_flush(drm_via_private_t *dev_priv, uint32_t cmd_type)
{
	uint32_t pause_addr_lo, pause_addr_hi;

	via_align_cmd(dev_priv, cmd_type, 0, &pause_addr_hi, &pause_addr_lo, 0);
	via_hook_segment(dev_priv, pause_addr_hi, pause_addr_lo, 0);
}

static void via_cmdbuf_pause(drm_via_private_t *dev_priv)
{
	via_cmdbuf_flush(dev_priv, HC_HAGPBpID_PAUSE);
}

static void via_cmdbuf_reset(drm_via_private_t *dev_priv)
{
	via_cmdbuf_flush(dev_priv, HC_HAGPBpID_STOP);
	via_wait_idle(dev_priv);
}

/*
 * User interface to the space and lag functions.
 */

static int via_cmdbuf_size(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_cmdbuf_size_t *d_siz = data;
	int ret = 0;
	uint32_t tmp_size, count;
	drm_via_private_t *dev_priv;

	DRM_DEBUG("\n");
	LOCK_TEST_WITH_RETURN(dev, file_priv);

	dev_priv = (drm_via_private_t *) dev->dev_private;

	if (dev_priv->ring.virtual_start == NULL) {
		DRM_ERROR("called without initializing AGP ring buffer.\n");
		return -EFAULT;
	}

	count = 1000000;
	tmp_size = d_siz->size;
	switch (d_siz->func) {
	case VIA_CMDBUF_SPACE:
		while (((tmp_size = via_cmdbuf_space(dev_priv)) < d_siz->size)
		       && --count) {
			if (!d_siz->wait)
				break;
		}
		if (!count) {
			DRM_ERROR("VIA_CMDBUF_SPACE timed out.\n");
			ret = -EAGAIN;
		}
		break;
	case VIA_CMDBUF_LAG:
		while (((tmp_size = via_cmdbuf_lag(dev_priv)) > d_siz->size)
		       && --count) {
			if (!d_siz->wait)
				break;
		}
		if (!count) {
			DRM_ERROR("VIA_CMDBUF_LAG timed out.\n");
			ret = -EAGAIN;
		}
		break;
	default:
		ret = -EFAULT;
	}
	d_siz->size = tmp_size;

	return ret;
}

const struct drm_ioctl_desc via_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VIA_ALLOCMEM, via_mem_alloc, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_FREEMEM, via_mem_free, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_AGP_INIT, via_agp_init, DRM_AUTH|DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_FB_INIT, via_fb_init, DRM_AUTH|DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_MAP_INIT, via_map_init, DRM_AUTH|DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_DEC_FUTEX, via_decoder_futex, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_DMA_INIT, via_dma_init, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_CMDBUFFER, via_cmdbuffer, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_FLUSH, via_flush_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_PCICMD, via_pci_cmdbuffer, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_CMDBUF_SIZE, via_cmdbuf_size, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_WAIT_IRQ, via_wait_irq, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_DMA_BLIT, via_dma_blit, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_BLIT_SYNC, via_dma_blit_sync, DRM_AUTH)
};

int via_max_ioctl = ARRAY_SIZE(via_ioctls);
