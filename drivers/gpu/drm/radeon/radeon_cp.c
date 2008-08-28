/* radeon_cp.c -- CP support for Radeon -*- linux-c -*- */
/*
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * Copyright 2007 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Kevin E. Martin <martin@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_drv.h"
#include "r300_reg.h"

#include "radeon_microcode.h"

#define RADEON_FIFO_DEBUG	0

static int radeon_do_cleanup_cp(struct drm_device * dev);
static void radeon_do_cp_start(drm_radeon_private_t * dev_priv);

static u32 R500_READ_MCIND(drm_radeon_private_t *dev_priv, int addr)
{
	u32 ret;
	RADEON_WRITE(R520_MC_IND_INDEX, 0x7f0000 | (addr & 0xff));
	ret = RADEON_READ(R520_MC_IND_DATA);
	RADEON_WRITE(R520_MC_IND_INDEX, 0);
	return ret;
}

static u32 RS480_READ_MCIND(drm_radeon_private_t *dev_priv, int addr)
{
	u32 ret;
	RADEON_WRITE(RS480_NB_MC_INDEX, addr & 0xff);
	ret = RADEON_READ(RS480_NB_MC_DATA);
	RADEON_WRITE(RS480_NB_MC_INDEX, 0xff);
	return ret;
}

static u32 RS690_READ_MCIND(drm_radeon_private_t *dev_priv, int addr)
{
	u32 ret;
	RADEON_WRITE(RS690_MC_INDEX, (addr & RS690_MC_INDEX_MASK));
	ret = RADEON_READ(RS690_MC_DATA);
	RADEON_WRITE(RS690_MC_INDEX, RS690_MC_INDEX_MASK);
	return ret;
}

static u32 IGP_READ_MCIND(drm_radeon_private_t *dev_priv, int addr)
{
	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS690)
		return RS690_READ_MCIND(dev_priv, addr);
	else
		return RS480_READ_MCIND(dev_priv, addr);
}

u32 radeon_read_fb_location(drm_radeon_private_t *dev_priv)
{

	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV515)
		return R500_READ_MCIND(dev_priv, RV515_MC_FB_LOCATION);
	else if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS690)
		return RS690_READ_MCIND(dev_priv, RS690_MC_FB_LOCATION);
	else if ((dev_priv->flags & RADEON_FAMILY_MASK) > CHIP_RV515)
		return R500_READ_MCIND(dev_priv, R520_MC_FB_LOCATION);
	else
		return RADEON_READ(RADEON_MC_FB_LOCATION);
}

static void radeon_write_fb_location(drm_radeon_private_t *dev_priv, u32 fb_loc)
{
	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV515)
		R500_WRITE_MCIND(RV515_MC_FB_LOCATION, fb_loc);
	else if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS690)
		RS690_WRITE_MCIND(RS690_MC_FB_LOCATION, fb_loc);
	else if ((dev_priv->flags & RADEON_FAMILY_MASK) > CHIP_RV515)
		R500_WRITE_MCIND(R520_MC_FB_LOCATION, fb_loc);
	else
		RADEON_WRITE(RADEON_MC_FB_LOCATION, fb_loc);
}

static void radeon_write_agp_location(drm_radeon_private_t *dev_priv, u32 agp_loc)
{
	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV515)
		R500_WRITE_MCIND(RV515_MC_AGP_LOCATION, agp_loc);
	else if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS690)
		RS690_WRITE_MCIND(RS690_MC_AGP_LOCATION, agp_loc);
	else if ((dev_priv->flags & RADEON_FAMILY_MASK) > CHIP_RV515)
		R500_WRITE_MCIND(R520_MC_AGP_LOCATION, agp_loc);
	else
		RADEON_WRITE(RADEON_MC_AGP_LOCATION, agp_loc);
}

static void radeon_write_agp_base(drm_radeon_private_t *dev_priv, u64 agp_base)
{
	u32 agp_base_hi = upper_32_bits(agp_base);
	u32 agp_base_lo = agp_base & 0xffffffff;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV515) {
		R500_WRITE_MCIND(RV515_MC_AGP_BASE, agp_base_lo);
		R500_WRITE_MCIND(RV515_MC_AGP_BASE_2, agp_base_hi);
	} else if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS690) {
		RS690_WRITE_MCIND(RS690_MC_AGP_BASE, agp_base_lo);
		RS690_WRITE_MCIND(RS690_MC_AGP_BASE_2, agp_base_hi);
	} else if ((dev_priv->flags & RADEON_FAMILY_MASK) > CHIP_RV515) {
		R500_WRITE_MCIND(R520_MC_AGP_BASE, agp_base_lo);
		R500_WRITE_MCIND(R520_MC_AGP_BASE_2, agp_base_hi);
	} else if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS480) {
		RADEON_WRITE(RADEON_AGP_BASE, agp_base_lo);
		RADEON_WRITE(RS480_AGP_BASE_2, 0);
	} else {
		RADEON_WRITE(RADEON_AGP_BASE, agp_base_lo);
		if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R200)
			RADEON_WRITE(RADEON_AGP_BASE_2, agp_base_hi);
	}
}

static int RADEON_READ_PLL(struct drm_device * dev, int addr)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	RADEON_WRITE8(RADEON_CLOCK_CNTL_INDEX, addr & 0x1f);
	return RADEON_READ(RADEON_CLOCK_CNTL_DATA);
}

static u32 RADEON_READ_PCIE(drm_radeon_private_t *dev_priv, int addr)
{
	RADEON_WRITE8(RADEON_PCIE_INDEX, addr & 0xff);
	return RADEON_READ(RADEON_PCIE_DATA);
}

#if RADEON_FIFO_DEBUG
static void radeon_status(drm_radeon_private_t * dev_priv)
{
	printk("%s:\n", __func__);
	printk("RBBM_STATUS = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_RBBM_STATUS));
	printk("CP_RB_RTPR = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_CP_RB_RPTR));
	printk("CP_RB_WTPR = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_CP_RB_WPTR));
	printk("AIC_CNTL = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_AIC_CNTL));
	printk("AIC_STAT = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_AIC_STAT));
	printk("AIC_PT_BASE = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_AIC_PT_BASE));
	printk("TLB_ADDR = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_AIC_TLB_ADDR));
	printk("TLB_DATA = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_AIC_TLB_DATA));
}
#endif

/* ================================================================
 * Engine, FIFO control
 */

static int radeon_do_pixcache_flush(drm_radeon_private_t * dev_priv)
{
	u32 tmp;
	int i;

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) <= CHIP_RV280) {
		tmp = RADEON_READ(RADEON_RB3D_DSTCACHE_CTLSTAT);
		tmp |= RADEON_RB3D_DC_FLUSH_ALL;
		RADEON_WRITE(RADEON_RB3D_DSTCACHE_CTLSTAT, tmp);

		for (i = 0; i < dev_priv->usec_timeout; i++) {
			if (!(RADEON_READ(RADEON_RB3D_DSTCACHE_CTLSTAT)
			      & RADEON_RB3D_DC_BUSY)) {
				return 0;
			}
			DRM_UDELAY(1);
		}
	} else {
		/* don't flush or purge cache here or lockup */
		return 0;
	}

#if RADEON_FIFO_DEBUG
	DRM_ERROR("failed!\n");
	radeon_status(dev_priv);
#endif
	return -EBUSY;
}

static int radeon_do_wait_for_fifo(drm_radeon_private_t * dev_priv, int entries)
{
	int i;

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		int slots = (RADEON_READ(RADEON_RBBM_STATUS)
			     & RADEON_RBBM_FIFOCNT_MASK);
		if (slots >= entries)
			return 0;
		DRM_UDELAY(1);
	}
	DRM_INFO("wait for fifo failed status : 0x%08X 0x%08X\n",
		 RADEON_READ(RADEON_RBBM_STATUS),
		 RADEON_READ(R300_VAP_CNTL_STATUS));

#if RADEON_FIFO_DEBUG
	DRM_ERROR("failed!\n");
	radeon_status(dev_priv);
#endif
	return -EBUSY;
}

static int radeon_do_wait_for_idle(drm_radeon_private_t * dev_priv)
{
	int i, ret;

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	ret = radeon_do_wait_for_fifo(dev_priv, 64);
	if (ret)
		return ret;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		if (!(RADEON_READ(RADEON_RBBM_STATUS)
		      & RADEON_RBBM_ACTIVE)) {
			radeon_do_pixcache_flush(dev_priv);
			return 0;
		}
		DRM_UDELAY(1);
	}
	DRM_INFO("wait idle failed status : 0x%08X 0x%08X\n",
		 RADEON_READ(RADEON_RBBM_STATUS),
		 RADEON_READ(R300_VAP_CNTL_STATUS));

#if RADEON_FIFO_DEBUG
	DRM_ERROR("failed!\n");
	radeon_status(dev_priv);
#endif
	return -EBUSY;
}

static void radeon_init_pipes(drm_radeon_private_t *dev_priv)
{
	uint32_t gb_tile_config, gb_pipe_sel = 0;

	/* RS4xx/RS6xx/R4xx/R5xx */
	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R420) {
		gb_pipe_sel = RADEON_READ(R400_GB_PIPE_SELECT);
		dev_priv->num_gb_pipes = ((gb_pipe_sel >> 12) & 0x3) + 1;
	} else {
		/* R3xx */
		if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R300) ||
		    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R350)) {
			dev_priv->num_gb_pipes = 2;
		} else {
			/* R3Vxx */
			dev_priv->num_gb_pipes = 1;
		}
	}
	DRM_INFO("Num pipes: %d\n", dev_priv->num_gb_pipes);

	gb_tile_config = (R300_ENABLE_TILING | R300_TILE_SIZE_16 /*| R300_SUBPIXEL_1_16*/);

	switch (dev_priv->num_gb_pipes) {
	case 2: gb_tile_config |= R300_PIPE_COUNT_R300; break;
	case 3: gb_tile_config |= R300_PIPE_COUNT_R420_3P; break;
	case 4: gb_tile_config |= R300_PIPE_COUNT_R420; break;
	default:
	case 1: gb_tile_config |= R300_PIPE_COUNT_RV350; break;
	}

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RV515) {
		RADEON_WRITE_PLL(R500_DYN_SCLK_PWMEM_PIPE, (1 | ((gb_pipe_sel >> 8) & 0xf) << 4));
		RADEON_WRITE(R500_SU_REG_DEST, ((1 << dev_priv->num_gb_pipes) - 1));
	}
	RADEON_WRITE(R300_GB_TILE_CONFIG, gb_tile_config);
	radeon_do_wait_for_idle(dev_priv);
	RADEON_WRITE(R300_DST_PIPE_CONFIG, RADEON_READ(R300_DST_PIPE_CONFIG) | R300_PIPE_AUTO_CONFIG);
	RADEON_WRITE(R300_RB2D_DSTCACHE_MODE, (RADEON_READ(R300_RB2D_DSTCACHE_MODE) |
					       R300_DC_AUTOFLUSH_ENABLE |
					       R300_DC_DC_DISABLE_IGNORE_PE));


}

/* ================================================================
 * CP control, initialization
 */

/* Load the microcode for the CP */
static void radeon_cp_load_microcode(drm_radeon_private_t * dev_priv)
{
	int i;
	DRM_DEBUG("\n");

	radeon_do_wait_for_idle(dev_priv);

	RADEON_WRITE(RADEON_CP_ME_RAM_ADDR, 0);
	if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R100) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV100) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV200) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS100) ||
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS200)) {
		DRM_INFO("Loading R100 Microcode\n");
		for (i = 0; i < 256; i++) {
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAH,
				     R100_cp_microcode[i][1]);
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAL,
				     R100_cp_microcode[i][0]);
		}
	} else if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R200) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV250) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV280) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS300)) {
		DRM_INFO("Loading R200 Microcode\n");
		for (i = 0; i < 256; i++) {
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAH,
				     R200_cp_microcode[i][1]);
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAL,
				     R200_cp_microcode[i][0]);
		}
	} else if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R300) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R350) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV350) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV380) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS480)) {
		DRM_INFO("Loading R300 Microcode\n");
		for (i = 0; i < 256; i++) {
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAH,
				     R300_cp_microcode[i][1]);
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAL,
				     R300_cp_microcode[i][0]);
		}
	} else if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R420) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV410)) {
		DRM_INFO("Loading R400 Microcode\n");
		for (i = 0; i < 256; i++) {
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAH,
				     R420_cp_microcode[i][1]);
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAL,
				     R420_cp_microcode[i][0]);
		}
	} else if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS690) {
		DRM_INFO("Loading RS690 Microcode\n");
		for (i = 0; i < 256; i++) {
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAH,
				     RS690_cp_microcode[i][1]);
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAL,
				     RS690_cp_microcode[i][0]);
		}
	} else if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV515) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R520) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV530) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_R580) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV560) ||
		   ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RV570)) {
		DRM_INFO("Loading R500 Microcode\n");
		for (i = 0; i < 256; i++) {
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAH,
				     R520_cp_microcode[i][1]);
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAL,
				     R520_cp_microcode[i][0]);
		}
	}
}

/* Flush any pending commands to the CP.  This should only be used just
 * prior to a wait for idle, as it informs the engine that the command
 * stream is ending.
 */
static void radeon_do_cp_flush(drm_radeon_private_t * dev_priv)
{
	DRM_DEBUG("\n");
#if 0
	u32 tmp;

	tmp = RADEON_READ(RADEON_CP_RB_WPTR) | (1 << 31);
	RADEON_WRITE(RADEON_CP_RB_WPTR, tmp);
#endif
}

/* Wait for the CP to go idle.
 */
int radeon_do_cp_idle(drm_radeon_private_t * dev_priv)
{
	RING_LOCALS;
	DRM_DEBUG("\n");

	BEGIN_RING(6);

	RADEON_PURGE_CACHE();
	RADEON_PURGE_ZCACHE();
	RADEON_WAIT_UNTIL_IDLE();

	ADVANCE_RING();
	COMMIT_RING();

	return radeon_do_wait_for_idle(dev_priv);
}

/* Start the Command Processor.
 */
static void radeon_do_cp_start(drm_radeon_private_t * dev_priv)
{
	RING_LOCALS;
	DRM_DEBUG("\n");

	radeon_do_wait_for_idle(dev_priv);

	RADEON_WRITE(RADEON_CP_CSQ_CNTL, dev_priv->cp_mode);

	dev_priv->cp_running = 1;

	BEGIN_RING(8);
	/* isync can only be written through cp on r5xx write it here */
	OUT_RING(CP_PACKET0(RADEON_ISYNC_CNTL, 0));
	OUT_RING(RADEON_ISYNC_ANY2D_IDLE3D |
		 RADEON_ISYNC_ANY3D_IDLE2D |
		 RADEON_ISYNC_WAIT_IDLEGUI |
		 RADEON_ISYNC_CPSCRATCH_IDLEGUI);
	RADEON_PURGE_CACHE();
	RADEON_PURGE_ZCACHE();
	RADEON_WAIT_UNTIL_IDLE();
	ADVANCE_RING();
	COMMIT_RING();

	dev_priv->track_flush |= RADEON_FLUSH_EMITED | RADEON_PURGE_EMITED;
}

/* Reset the Command Processor.  This will not flush any pending
 * commands, so you must wait for the CP command stream to complete
 * before calling this routine.
 */
static void radeon_do_cp_reset(drm_radeon_private_t * dev_priv)
{
	u32 cur_read_ptr;
	DRM_DEBUG("\n");

	cur_read_ptr = RADEON_READ(RADEON_CP_RB_RPTR);
	RADEON_WRITE(RADEON_CP_RB_WPTR, cur_read_ptr);
	SET_RING_HEAD(dev_priv, cur_read_ptr);
	dev_priv->ring.tail = cur_read_ptr;
}

/* Stop the Command Processor.  This will not flush any pending
 * commands, so you must flush the command stream and wait for the CP
 * to go idle before calling this routine.
 */
static void radeon_do_cp_stop(drm_radeon_private_t * dev_priv)
{
	DRM_DEBUG("\n");

	RADEON_WRITE(RADEON_CP_CSQ_CNTL, RADEON_CSQ_PRIDIS_INDDIS);

	dev_priv->cp_running = 0;
}

/* Reset the engine.  This will stop the CP if it is running.
 */
static int radeon_do_engine_reset(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u32 clock_cntl_index = 0, mclk_cntl = 0, rbbm_soft_reset;
	DRM_DEBUG("\n");

	radeon_do_pixcache_flush(dev_priv);

	if ((dev_priv->flags & RADEON_FAMILY_MASK) <= CHIP_RV410) {
		/* may need something similar for newer chips */
		clock_cntl_index = RADEON_READ(RADEON_CLOCK_CNTL_INDEX);
		mclk_cntl = RADEON_READ_PLL(dev, RADEON_MCLK_CNTL);

		RADEON_WRITE_PLL(RADEON_MCLK_CNTL, (mclk_cntl |
						    RADEON_FORCEON_MCLKA |
						    RADEON_FORCEON_MCLKB |
						    RADEON_FORCEON_YCLKA |
						    RADEON_FORCEON_YCLKB |
						    RADEON_FORCEON_MC |
						    RADEON_FORCEON_AIC));
	}

	rbbm_soft_reset = RADEON_READ(RADEON_RBBM_SOFT_RESET);

	RADEON_WRITE(RADEON_RBBM_SOFT_RESET, (rbbm_soft_reset |
					      RADEON_SOFT_RESET_CP |
					      RADEON_SOFT_RESET_HI |
					      RADEON_SOFT_RESET_SE |
					      RADEON_SOFT_RESET_RE |
					      RADEON_SOFT_RESET_PP |
					      RADEON_SOFT_RESET_E2 |
					      RADEON_SOFT_RESET_RB));
	RADEON_READ(RADEON_RBBM_SOFT_RESET);
	RADEON_WRITE(RADEON_RBBM_SOFT_RESET, (rbbm_soft_reset &
					      ~(RADEON_SOFT_RESET_CP |
						RADEON_SOFT_RESET_HI |
						RADEON_SOFT_RESET_SE |
						RADEON_SOFT_RESET_RE |
						RADEON_SOFT_RESET_PP |
						RADEON_SOFT_RESET_E2 |
						RADEON_SOFT_RESET_RB)));
	RADEON_READ(RADEON_RBBM_SOFT_RESET);

	if ((dev_priv->flags & RADEON_FAMILY_MASK) <= CHIP_RV410) {
		RADEON_WRITE_PLL(RADEON_MCLK_CNTL, mclk_cntl);
		RADEON_WRITE(RADEON_CLOCK_CNTL_INDEX, clock_cntl_index);
		RADEON_WRITE(RADEON_RBBM_SOFT_RESET, rbbm_soft_reset);
	}

	/* setup the raster pipes */
	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R300)
	    radeon_init_pipes(dev_priv);

	/* Reset the CP ring */
	radeon_do_cp_reset(dev_priv);

	/* The CP is no longer running after an engine reset */
	dev_priv->cp_running = 0;

	/* Reset any pending vertex, indirect buffers */
	radeon_freelist_reset(dev);

	return 0;
}

static void radeon_cp_init_ring_buffer(struct drm_device * dev,
				       drm_radeon_private_t * dev_priv)
{
	u32 ring_start, cur_read_ptr;
	u32 tmp;

	/* Initialize the memory controller. With new memory map, the fb location
	 * is not changed, it should have been properly initialized already. Part
	 * of the problem is that the code below is bogus, assuming the GART is
	 * always appended to the fb which is not necessarily the case
	 */
	if (!dev_priv->new_memmap)
		radeon_write_fb_location(dev_priv,
			     ((dev_priv->gart_vm_start - 1) & 0xffff0000)
			     | (dev_priv->fb_location >> 16));

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		radeon_write_agp_base(dev_priv, dev->agp->base);

		radeon_write_agp_location(dev_priv,
			     (((dev_priv->gart_vm_start - 1 +
				dev_priv->gart_size) & 0xffff0000) |
			      (dev_priv->gart_vm_start >> 16)));

		ring_start = (dev_priv->cp_ring->offset
			      - dev->agp->base
			      + dev_priv->gart_vm_start);
	} else
#endif
		ring_start = (dev_priv->cp_ring->offset
			      - (unsigned long)dev->sg->virtual
			      + dev_priv->gart_vm_start);

	RADEON_WRITE(RADEON_CP_RB_BASE, ring_start);

	/* Set the write pointer delay */
	RADEON_WRITE(RADEON_CP_RB_WPTR_DELAY, 0);

	/* Initialize the ring buffer's read and write pointers */
	cur_read_ptr = RADEON_READ(RADEON_CP_RB_RPTR);
	RADEON_WRITE(RADEON_CP_RB_WPTR, cur_read_ptr);
	SET_RING_HEAD(dev_priv, cur_read_ptr);
	dev_priv->ring.tail = cur_read_ptr;

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		RADEON_WRITE(RADEON_CP_RB_RPTR_ADDR,
			     dev_priv->ring_rptr->offset
			     - dev->agp->base + dev_priv->gart_vm_start);
	} else
#endif
	{
		struct drm_sg_mem *entry = dev->sg;
		unsigned long tmp_ofs, page_ofs;

		tmp_ofs = dev_priv->ring_rptr->offset -
				(unsigned long)dev->sg->virtual;
		page_ofs = tmp_ofs >> PAGE_SHIFT;

		RADEON_WRITE(RADEON_CP_RB_RPTR_ADDR, entry->busaddr[page_ofs]);
		DRM_DEBUG("ring rptr: offset=0x%08lx handle=0x%08lx\n",
			  (unsigned long)entry->busaddr[page_ofs],
			  entry->handle + tmp_ofs);
	}

	/* Set ring buffer size */
#ifdef __BIG_ENDIAN
	RADEON_WRITE(RADEON_CP_RB_CNTL,
		     RADEON_BUF_SWAP_32BIT |
		     (dev_priv->ring.fetch_size_l2ow << 18) |
		     (dev_priv->ring.rptr_update_l2qw << 8) |
		     dev_priv->ring.size_l2qw);
#else
	RADEON_WRITE(RADEON_CP_RB_CNTL,
		     (dev_priv->ring.fetch_size_l2ow << 18) |
		     (dev_priv->ring.rptr_update_l2qw << 8) |
		     dev_priv->ring.size_l2qw);
#endif

	/* Start with assuming that writeback doesn't work */
	dev_priv->writeback_works = 0;

	/* Initialize the scratch register pointer.  This will cause
	 * the scratch register values to be written out to memory
	 * whenever they are updated.
	 *
	 * We simply put this behind the ring read pointer, this works
	 * with PCI GART as well as (whatever kind of) AGP GART
	 */
	RADEON_WRITE(RADEON_SCRATCH_ADDR, RADEON_READ(RADEON_CP_RB_RPTR_ADDR)
		     + RADEON_SCRATCH_REG_OFFSET);

	dev_priv->scratch = ((__volatile__ u32 *)
			     dev_priv->ring_rptr->handle +
			     (RADEON_SCRATCH_REG_OFFSET / sizeof(u32)));

	RADEON_WRITE(RADEON_SCRATCH_UMSK, 0x7);

	/* Turn on bus mastering */
	tmp = RADEON_READ(RADEON_BUS_CNTL) & ~RADEON_BUS_MASTER_DIS;
	RADEON_WRITE(RADEON_BUS_CNTL, tmp);

	dev_priv->sarea_priv->last_frame = dev_priv->scratch[0] = 0;
	RADEON_WRITE(RADEON_LAST_FRAME_REG, dev_priv->sarea_priv->last_frame);

	dev_priv->sarea_priv->last_dispatch = dev_priv->scratch[1] = 0;
	RADEON_WRITE(RADEON_LAST_DISPATCH_REG,
		     dev_priv->sarea_priv->last_dispatch);

	dev_priv->sarea_priv->last_clear = dev_priv->scratch[2] = 0;
	RADEON_WRITE(RADEON_LAST_CLEAR_REG, dev_priv->sarea_priv->last_clear);

	radeon_do_wait_for_idle(dev_priv);

	/* Sync everything up */
	RADEON_WRITE(RADEON_ISYNC_CNTL,
		     (RADEON_ISYNC_ANY2D_IDLE3D |
		      RADEON_ISYNC_ANY3D_IDLE2D |
		      RADEON_ISYNC_WAIT_IDLEGUI |
		      RADEON_ISYNC_CPSCRATCH_IDLEGUI));

}

static void radeon_test_writeback(drm_radeon_private_t * dev_priv)
{
	u32 tmp;

	/* Writeback doesn't seem to work everywhere, test it here and possibly
	 * enable it if it appears to work
	 */
	DRM_WRITE32(dev_priv->ring_rptr, RADEON_SCRATCHOFF(1), 0);
	RADEON_WRITE(RADEON_SCRATCH_REG1, 0xdeadbeef);

	for (tmp = 0; tmp < dev_priv->usec_timeout; tmp++) {
		if (DRM_READ32(dev_priv->ring_rptr, RADEON_SCRATCHOFF(1)) ==
		    0xdeadbeef)
			break;
		DRM_UDELAY(1);
	}

	if (tmp < dev_priv->usec_timeout) {
		dev_priv->writeback_works = 1;
		DRM_INFO("writeback test succeeded in %d usecs\n", tmp);
	} else {
		dev_priv->writeback_works = 0;
		DRM_INFO("writeback test failed\n");
	}
	if (radeon_no_wb == 1) {
		dev_priv->writeback_works = 0;
		DRM_INFO("writeback forced off\n");
	}

	if (!dev_priv->writeback_works) {
		/* Disable writeback to avoid unnecessary bus master transfer */
		RADEON_WRITE(RADEON_CP_RB_CNTL, RADEON_READ(RADEON_CP_RB_CNTL) |
			     RADEON_RB_NO_UPDATE);
		RADEON_WRITE(RADEON_SCRATCH_UMSK, 0);
	}
}

/* Enable or disable IGP GART on the chip */
static void radeon_set_igpgart(drm_radeon_private_t * dev_priv, int on)
{
	u32 temp;

	if (on) {
		DRM_DEBUG("programming igp gart %08X %08lX %08X\n",
			  dev_priv->gart_vm_start,
			  (long)dev_priv->gart_info.bus_addr,
			  dev_priv->gart_size);

		temp = IGP_READ_MCIND(dev_priv, RS480_MC_MISC_CNTL);
		if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS690)
			IGP_WRITE_MCIND(RS480_MC_MISC_CNTL, (RS480_GART_INDEX_REG_EN |
							     RS690_BLOCK_GFX_D3_EN));
		else
			IGP_WRITE_MCIND(RS480_MC_MISC_CNTL, RS480_GART_INDEX_REG_EN);

		IGP_WRITE_MCIND(RS480_AGP_ADDRESS_SPACE_SIZE, (RS480_GART_EN |
							       RS480_VA_SIZE_32MB));

		temp = IGP_READ_MCIND(dev_priv, RS480_GART_FEATURE_ID);
		IGP_WRITE_MCIND(RS480_GART_FEATURE_ID, (RS480_HANG_EN |
							RS480_TLB_ENABLE |
							RS480_GTW_LAC_EN |
							RS480_1LEVEL_GART));

		temp = dev_priv->gart_info.bus_addr & 0xfffff000;
		temp |= (upper_32_bits(dev_priv->gart_info.bus_addr) & 0xff) << 4;
		IGP_WRITE_MCIND(RS480_GART_BASE, temp);

		temp = IGP_READ_MCIND(dev_priv, RS480_AGP_MODE_CNTL);
		IGP_WRITE_MCIND(RS480_AGP_MODE_CNTL, ((1 << RS480_REQ_TYPE_SNOOP_SHIFT) |
						      RS480_REQ_TYPE_SNOOP_DIS));

		radeon_write_agp_base(dev_priv, dev_priv->gart_vm_start);

		dev_priv->gart_size = 32*1024*1024;
		temp = (((dev_priv->gart_vm_start - 1 + dev_priv->gart_size) &
			 0xffff0000) | (dev_priv->gart_vm_start >> 16));

		radeon_write_agp_location(dev_priv, temp);

		temp = IGP_READ_MCIND(dev_priv, RS480_AGP_ADDRESS_SPACE_SIZE);
		IGP_WRITE_MCIND(RS480_AGP_ADDRESS_SPACE_SIZE, (RS480_GART_EN |
							       RS480_VA_SIZE_32MB));

		do {
			temp = IGP_READ_MCIND(dev_priv, RS480_GART_CACHE_CNTRL);
			if ((temp & RS480_GART_CACHE_INVALIDATE) == 0)
				break;
			DRM_UDELAY(1);
		} while (1);

		IGP_WRITE_MCIND(RS480_GART_CACHE_CNTRL,
				RS480_GART_CACHE_INVALIDATE);

		do {
			temp = IGP_READ_MCIND(dev_priv, RS480_GART_CACHE_CNTRL);
			if ((temp & RS480_GART_CACHE_INVALIDATE) == 0)
				break;
			DRM_UDELAY(1);
		} while (1);

		IGP_WRITE_MCIND(RS480_GART_CACHE_CNTRL, 0);
	} else {
		IGP_WRITE_MCIND(RS480_AGP_ADDRESS_SPACE_SIZE, 0);
	}
}

static void radeon_set_pciegart(drm_radeon_private_t * dev_priv, int on)
{
	u32 tmp = RADEON_READ_PCIE(dev_priv, RADEON_PCIE_TX_GART_CNTL);
	if (on) {

		DRM_DEBUG("programming pcie %08X %08lX %08X\n",
			  dev_priv->gart_vm_start,
			  (long)dev_priv->gart_info.bus_addr,
			  dev_priv->gart_size);
		RADEON_WRITE_PCIE(RADEON_PCIE_TX_DISCARD_RD_ADDR_LO,
				  dev_priv->gart_vm_start);
		RADEON_WRITE_PCIE(RADEON_PCIE_TX_GART_BASE,
				  dev_priv->gart_info.bus_addr);
		RADEON_WRITE_PCIE(RADEON_PCIE_TX_GART_START_LO,
				  dev_priv->gart_vm_start);
		RADEON_WRITE_PCIE(RADEON_PCIE_TX_GART_END_LO,
				  dev_priv->gart_vm_start +
				  dev_priv->gart_size - 1);

		radeon_write_agp_location(dev_priv, 0xffffffc0); /* ?? */

		RADEON_WRITE_PCIE(RADEON_PCIE_TX_GART_CNTL,
				  RADEON_PCIE_TX_GART_EN);
	} else {
		RADEON_WRITE_PCIE(RADEON_PCIE_TX_GART_CNTL,
				  tmp & ~RADEON_PCIE_TX_GART_EN);
	}
}

/* Enable or disable PCI GART on the chip */
static void radeon_set_pcigart(drm_radeon_private_t * dev_priv, int on)
{
	u32 tmp;

	if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS690) ||
	    (dev_priv->flags & RADEON_IS_IGPGART)) {
		radeon_set_igpgart(dev_priv, on);
		return;
	}

	if (dev_priv->flags & RADEON_IS_PCIE) {
		radeon_set_pciegart(dev_priv, on);
		return;
	}

	tmp = RADEON_READ(RADEON_AIC_CNTL);

	if (on) {
		RADEON_WRITE(RADEON_AIC_CNTL,
			     tmp | RADEON_PCIGART_TRANSLATE_EN);

		/* set PCI GART page-table base address
		 */
		RADEON_WRITE(RADEON_AIC_PT_BASE, dev_priv->gart_info.bus_addr);

		/* set address range for PCI address translate
		 */
		RADEON_WRITE(RADEON_AIC_LO_ADDR, dev_priv->gart_vm_start);
		RADEON_WRITE(RADEON_AIC_HI_ADDR, dev_priv->gart_vm_start
			     + dev_priv->gart_size - 1);

		/* Turn off AGP aperture -- is this required for PCI GART?
		 */
		radeon_write_agp_location(dev_priv, 0xffffffc0);
		RADEON_WRITE(RADEON_AGP_COMMAND, 0);	/* clear AGP_COMMAND */
	} else {
		RADEON_WRITE(RADEON_AIC_CNTL,
			     tmp & ~RADEON_PCIGART_TRANSLATE_EN);
	}
}

static int radeon_do_init_cp(struct drm_device * dev, drm_radeon_init_t * init)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("\n");

	/* if we require new memory map but we don't have it fail */
	if ((dev_priv->flags & RADEON_NEW_MEMMAP) && !dev_priv->new_memmap) {
		DRM_ERROR("Cannot initialise DRM on this card\nThis card requires a new X.org DDX for 3D\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	if (init->is_pci && (dev_priv->flags & RADEON_IS_AGP)) {
		DRM_DEBUG("Forcing AGP card to PCI mode\n");
		dev_priv->flags &= ~RADEON_IS_AGP;
	} else if (!(dev_priv->flags & (RADEON_IS_AGP | RADEON_IS_PCI | RADEON_IS_PCIE))
		   && !init->is_pci) {
		DRM_DEBUG("Restoring AGP flag\n");
		dev_priv->flags |= RADEON_IS_AGP;
	}

	if ((!(dev_priv->flags & RADEON_IS_AGP)) && !dev->sg) {
		DRM_ERROR("PCI GART memory not allocated!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	dev_priv->usec_timeout = init->usec_timeout;
	if (dev_priv->usec_timeout < 1 ||
	    dev_priv->usec_timeout > RADEON_MAX_USEC_TIMEOUT) {
		DRM_DEBUG("TIMEOUT problem!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	/* Enable vblank on CRTC1 for older X servers
	 */
	dev_priv->vblank_crtc = DRM_RADEON_VBLANK_CRTC1;

	switch(init->func) {
	case RADEON_INIT_R200_CP:
		dev_priv->microcode_version = UCODE_R200;
		break;
	case RADEON_INIT_R300_CP:
		dev_priv->microcode_version = UCODE_R300;
		break;
	default:
		dev_priv->microcode_version = UCODE_R100;
	}

	dev_priv->do_boxes = 0;
	dev_priv->cp_mode = init->cp_mode;

	/* We don't support anything other than bus-mastering ring mode,
	 * but the ring can be in either AGP or PCI space for the ring
	 * read pointer.
	 */
	if ((init->cp_mode != RADEON_CSQ_PRIBM_INDDIS) &&
	    (init->cp_mode != RADEON_CSQ_PRIBM_INDBM)) {
		DRM_DEBUG("BAD cp_mode (%x)!\n", init->cp_mode);
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	switch (init->fb_bpp) {
	case 16:
		dev_priv->color_fmt = RADEON_COLOR_FORMAT_RGB565;
		break;
	case 32:
	default:
		dev_priv->color_fmt = RADEON_COLOR_FORMAT_ARGB8888;
		break;
	}
	dev_priv->front_offset = init->front_offset;
	dev_priv->front_pitch = init->front_pitch;
	dev_priv->back_offset = init->back_offset;
	dev_priv->back_pitch = init->back_pitch;

	switch (init->depth_bpp) {
	case 16:
		dev_priv->depth_fmt = RADEON_DEPTH_FORMAT_16BIT_INT_Z;
		break;
	case 32:
	default:
		dev_priv->depth_fmt = RADEON_DEPTH_FORMAT_24BIT_INT_Z;
		break;
	}
	dev_priv->depth_offset = init->depth_offset;
	dev_priv->depth_pitch = init->depth_pitch;

	/* Hardware state for depth clears.  Remove this if/when we no
	 * longer clear the depth buffer with a 3D rectangle.  Hard-code
	 * all values to prevent unwanted 3D state from slipping through
	 * and screwing with the clear operation.
	 */
	dev_priv->depth_clear.rb3d_cntl = (RADEON_PLANE_MASK_ENABLE |
					   (dev_priv->color_fmt << 10) |
					   (dev_priv->microcode_version ==
					    UCODE_R100 ? RADEON_ZBLOCK16 : 0));

	dev_priv->depth_clear.rb3d_zstencilcntl =
	    (dev_priv->depth_fmt |
	     RADEON_Z_TEST_ALWAYS |
	     RADEON_STENCIL_TEST_ALWAYS |
	     RADEON_STENCIL_S_FAIL_REPLACE |
	     RADEON_STENCIL_ZPASS_REPLACE |
	     RADEON_STENCIL_ZFAIL_REPLACE | RADEON_Z_WRITE_ENABLE);

	dev_priv->depth_clear.se_cntl = (RADEON_FFACE_CULL_CW |
					 RADEON_BFACE_SOLID |
					 RADEON_FFACE_SOLID |
					 RADEON_FLAT_SHADE_VTX_LAST |
					 RADEON_DIFFUSE_SHADE_FLAT |
					 RADEON_ALPHA_SHADE_FLAT |
					 RADEON_SPECULAR_SHADE_FLAT |
					 RADEON_FOG_SHADE_FLAT |
					 RADEON_VTX_PIX_CENTER_OGL |
					 RADEON_ROUND_MODE_TRUNC |
					 RADEON_ROUND_PREC_8TH_PIX);


	dev_priv->ring_offset = init->ring_offset;
	dev_priv->ring_rptr_offset = init->ring_rptr_offset;
	dev_priv->buffers_offset = init->buffers_offset;
	dev_priv->gart_textures_offset = init->gart_textures_offset;

	dev_priv->sarea = drm_getsarea(dev);
	if (!dev_priv->sarea) {
		DRM_ERROR("could not find sarea!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	dev_priv->cp_ring = drm_core_findmap(dev, init->ring_offset);
	if (!dev_priv->cp_ring) {
		DRM_ERROR("could not find cp ring region!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}
	dev_priv->ring_rptr = drm_core_findmap(dev, init->ring_rptr_offset);
	if (!dev_priv->ring_rptr) {
		DRM_ERROR("could not find ring read pointer!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}
	dev->agp_buffer_token = init->buffers_offset;
	dev->agp_buffer_map = drm_core_findmap(dev, init->buffers_offset);
	if (!dev->agp_buffer_map) {
		DRM_ERROR("could not find dma buffer region!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	if (init->gart_textures_offset) {
		dev_priv->gart_textures =
		    drm_core_findmap(dev, init->gart_textures_offset);
		if (!dev_priv->gart_textures) {
			DRM_ERROR("could not find GART texture region!\n");
			radeon_do_cleanup_cp(dev);
			return -EINVAL;
		}
	}

	dev_priv->sarea_priv =
	    (drm_radeon_sarea_t *) ((u8 *) dev_priv->sarea->handle +
				    init->sarea_priv_offset);

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		drm_core_ioremap(dev_priv->cp_ring, dev);
		drm_core_ioremap(dev_priv->ring_rptr, dev);
		drm_core_ioremap(dev->agp_buffer_map, dev);
		if (!dev_priv->cp_ring->handle ||
		    !dev_priv->ring_rptr->handle ||
		    !dev->agp_buffer_map->handle) {
			DRM_ERROR("could not find ioremap agp regions!\n");
			radeon_do_cleanup_cp(dev);
			return -EINVAL;
		}
	} else
#endif
	{
		dev_priv->cp_ring->handle = (void *)dev_priv->cp_ring->offset;
		dev_priv->ring_rptr->handle =
		    (void *)dev_priv->ring_rptr->offset;
		dev->agp_buffer_map->handle =
		    (void *)dev->agp_buffer_map->offset;

		DRM_DEBUG("dev_priv->cp_ring->handle %p\n",
			  dev_priv->cp_ring->handle);
		DRM_DEBUG("dev_priv->ring_rptr->handle %p\n",
			  dev_priv->ring_rptr->handle);
		DRM_DEBUG("dev->agp_buffer_map->handle %p\n",
			  dev->agp_buffer_map->handle);
	}

	dev_priv->fb_location = (radeon_read_fb_location(dev_priv) & 0xffff) << 16;
	dev_priv->fb_size =
		((radeon_read_fb_location(dev_priv) & 0xffff0000u) + 0x10000)
		- dev_priv->fb_location;

	dev_priv->front_pitch_offset = (((dev_priv->front_pitch / 64) << 22) |
					((dev_priv->front_offset
					  + dev_priv->fb_location) >> 10));

	dev_priv->back_pitch_offset = (((dev_priv->back_pitch / 64) << 22) |
				       ((dev_priv->back_offset
					 + dev_priv->fb_location) >> 10));

	dev_priv->depth_pitch_offset = (((dev_priv->depth_pitch / 64) << 22) |
					((dev_priv->depth_offset
					  + dev_priv->fb_location) >> 10));

	dev_priv->gart_size = init->gart_size;

	/* New let's set the memory map ... */
	if (dev_priv->new_memmap) {
		u32 base = 0;

		DRM_INFO("Setting GART location based on new memory map\n");

		/* If using AGP, try to locate the AGP aperture at the same
		 * location in the card and on the bus, though we have to
		 * align it down.
		 */
#if __OS_HAS_AGP
		if (dev_priv->flags & RADEON_IS_AGP) {
			base = dev->agp->base;
			/* Check if valid */
			if ((base + dev_priv->gart_size - 1) >= dev_priv->fb_location &&
			    base < (dev_priv->fb_location + dev_priv->fb_size - 1)) {
				DRM_INFO("Can't use AGP base @0x%08lx, won't fit\n",
					 dev->agp->base);
				base = 0;
			}
		}
#endif
		/* If not or if AGP is at 0 (Macs), try to put it elsewhere */
		if (base == 0) {
			base = dev_priv->fb_location + dev_priv->fb_size;
			if (base < dev_priv->fb_location ||
			    ((base + dev_priv->gart_size) & 0xfffffffful) < base)
				base = dev_priv->fb_location
					- dev_priv->gart_size;
		}
		dev_priv->gart_vm_start = base & 0xffc00000u;
		if (dev_priv->gart_vm_start != base)
			DRM_INFO("GART aligned down from 0x%08x to 0x%08x\n",
				 base, dev_priv->gart_vm_start);
	} else {
		DRM_INFO("Setting GART location based on old memory map\n");
		dev_priv->gart_vm_start = dev_priv->fb_location +
			RADEON_READ(RADEON_CONFIG_APER_SIZE);
	}

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP)
		dev_priv->gart_buffers_offset = (dev->agp_buffer_map->offset
						 - dev->agp->base
						 + dev_priv->gart_vm_start);
	else
#endif
		dev_priv->gart_buffers_offset = (dev->agp_buffer_map->offset
					- (unsigned long)dev->sg->virtual
					+ dev_priv->gart_vm_start);

	DRM_DEBUG("dev_priv->gart_size %d\n", dev_priv->gart_size);
	DRM_DEBUG("dev_priv->gart_vm_start 0x%x\n", dev_priv->gart_vm_start);
	DRM_DEBUG("dev_priv->gart_buffers_offset 0x%lx\n",
		  dev_priv->gart_buffers_offset);

	dev_priv->ring.start = (u32 *) dev_priv->cp_ring->handle;
	dev_priv->ring.end = ((u32 *) dev_priv->cp_ring->handle
			      + init->ring_size / sizeof(u32));
	dev_priv->ring.size = init->ring_size;
	dev_priv->ring.size_l2qw = drm_order(init->ring_size / 8);

	dev_priv->ring.rptr_update = /* init->rptr_update */ 4096;
	dev_priv->ring.rptr_update_l2qw = drm_order( /* init->rptr_update */ 4096 / 8);

	dev_priv->ring.fetch_size = /* init->fetch_size */ 32;
	dev_priv->ring.fetch_size_l2ow = drm_order( /* init->fetch_size */ 32 / 16);
	dev_priv->ring.tail_mask = (dev_priv->ring.size / sizeof(u32)) - 1;

	dev_priv->ring.high_mark = RADEON_RING_HIGH_MARK;

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		/* Turn off PCI GART */
		radeon_set_pcigart(dev_priv, 0);
	} else
#endif
	{
		dev_priv->gart_info.table_mask = DMA_BIT_MASK(32);
		/* if we have an offset set from userspace */
		if (dev_priv->pcigart_offset_set) {
			dev_priv->gart_info.bus_addr =
			    dev_priv->pcigart_offset + dev_priv->fb_location;
			dev_priv->gart_info.mapping.offset =
			    dev_priv->pcigart_offset + dev_priv->fb_aper_offset;
			dev_priv->gart_info.mapping.size =
			    dev_priv->gart_info.table_size;

			drm_core_ioremap_wc(&dev_priv->gart_info.mapping, dev);
			dev_priv->gart_info.addr =
			    dev_priv->gart_info.mapping.handle;

			if (dev_priv->flags & RADEON_IS_PCIE)
				dev_priv->gart_info.gart_reg_if = DRM_ATI_GART_PCIE;
			else
				dev_priv->gart_info.gart_reg_if = DRM_ATI_GART_PCI;
			dev_priv->gart_info.gart_table_location =
			    DRM_ATI_GART_FB;

			DRM_DEBUG("Setting phys_pci_gart to %p %08lX\n",
				  dev_priv->gart_info.addr,
				  dev_priv->pcigart_offset);
		} else {
			if (dev_priv->flags & RADEON_IS_IGPGART)
				dev_priv->gart_info.gart_reg_if = DRM_ATI_GART_IGP;
			else
				dev_priv->gart_info.gart_reg_if = DRM_ATI_GART_PCI;
			dev_priv->gart_info.gart_table_location =
			    DRM_ATI_GART_MAIN;
			dev_priv->gart_info.addr = NULL;
			dev_priv->gart_info.bus_addr = 0;
			if (dev_priv->flags & RADEON_IS_PCIE) {
				DRM_ERROR
				    ("Cannot use PCI Express without GART in FB memory\n");
				radeon_do_cleanup_cp(dev);
				return -EINVAL;
			}
		}

		if (!drm_ati_pcigart_init(dev, &dev_priv->gart_info)) {
			DRM_ERROR("failed to init PCI GART!\n");
			radeon_do_cleanup_cp(dev);
			return -ENOMEM;
		}

		/* Turn on PCI GART */
		radeon_set_pcigart(dev_priv, 1);
	}

	radeon_cp_load_microcode(dev_priv);
	radeon_cp_init_ring_buffer(dev, dev_priv);

	dev_priv->last_buf = 0;

	radeon_do_engine_reset(dev);
	radeon_test_writeback(dev_priv);

	return 0;
}

static int radeon_do_cleanup_cp(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq_enabled)
		drm_irq_uninstall(dev);

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		if (dev_priv->cp_ring != NULL) {
			drm_core_ioremapfree(dev_priv->cp_ring, dev);
			dev_priv->cp_ring = NULL;
		}
		if (dev_priv->ring_rptr != NULL) {
			drm_core_ioremapfree(dev_priv->ring_rptr, dev);
			dev_priv->ring_rptr = NULL;
		}
		if (dev->agp_buffer_map != NULL) {
			drm_core_ioremapfree(dev->agp_buffer_map, dev);
			dev->agp_buffer_map = NULL;
		}
	} else
#endif
	{

		if (dev_priv->gart_info.bus_addr) {
			/* Turn off PCI GART */
			radeon_set_pcigart(dev_priv, 0);
			if (!drm_ati_pcigart_cleanup(dev, &dev_priv->gart_info))
				DRM_ERROR("failed to cleanup PCI GART!\n");
		}

		if (dev_priv->gart_info.gart_table_location == DRM_ATI_GART_FB)
		{
			drm_core_ioremapfree(&dev_priv->gart_info.mapping, dev);
			dev_priv->gart_info.addr = 0;
		}
	}
	/* only clear to the start of flags */
	memset(dev_priv, 0, offsetof(drm_radeon_private_t, flags));

	return 0;
}

/* This code will reinit the Radeon CP hardware after a resume from disc.
 * AFAIK, it would be very difficult to pickle the state at suspend time, so
 * here we make sure that all Radeon hardware initialisation is re-done without
 * affecting running applications.
 *
 * Charl P. Botha <http://cpbotha.net>
 */
static int radeon_do_resume_cp(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	if (!dev_priv) {
		DRM_ERROR("Called with no initialization\n");
		return -EINVAL;
	}

	DRM_DEBUG("Starting radeon_do_resume_cp()\n");

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		/* Turn off PCI GART */
		radeon_set_pcigart(dev_priv, 0);
	} else
#endif
	{
		/* Turn on PCI GART */
		radeon_set_pcigart(dev_priv, 1);
	}

	radeon_cp_load_microcode(dev_priv);
	radeon_cp_init_ring_buffer(dev, dev_priv);

	radeon_do_engine_reset(dev);
	radeon_enable_interrupt(dev);

	DRM_DEBUG("radeon_do_resume_cp() complete\n");

	return 0;
}

int radeon_cp_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_init_t *init = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (init->func == RADEON_INIT_R300_CP)
		r300_init_reg_flags(dev);

	switch (init->func) {
	case RADEON_INIT_CP:
	case RADEON_INIT_R200_CP:
	case RADEON_INIT_R300_CP:
		return radeon_do_init_cp(dev, init);
	case RADEON_CLEANUP_CP:
		return radeon_do_cleanup_cp(dev);
	}

	return -EINVAL;
}

int radeon_cp_start(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (dev_priv->cp_running) {
		DRM_DEBUG("while CP running\n");
		return 0;
	}
	if (dev_priv->cp_mode == RADEON_CSQ_PRIDIS_INDDIS) {
		DRM_DEBUG("called with bogus CP mode (%d)\n",
			  dev_priv->cp_mode);
		return 0;
	}

	radeon_do_cp_start(dev_priv);

	return 0;
}

/* Stop the CP.  The engine must have been idled before calling this
 * routine.
 */
int radeon_cp_stop(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_cp_stop_t *stop = data;
	int ret;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (!dev_priv->cp_running)
		return 0;

	/* Flush any pending CP commands.  This ensures any outstanding
	 * commands are exectuted by the engine before we turn it off.
	 */
	if (stop->flush) {
		radeon_do_cp_flush(dev_priv);
	}

	/* If we fail to make the engine go idle, we return an error
	 * code so that the DRM ioctl wrapper can try again.
	 */
	if (stop->idle) {
		ret = radeon_do_cp_idle(dev_priv);
		if (ret)
			return ret;
	}

	/* Finally, we can turn off the CP.  If the engine isn't idle,
	 * we will get some dropped triangles as they won't be fully
	 * rendered before the CP is shut down.
	 */
	radeon_do_cp_stop(dev_priv);

	/* Reset the engine */
	radeon_do_engine_reset(dev);

	return 0;
}

void radeon_do_release(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int i, ret;

	if (dev_priv) {
		if (dev_priv->cp_running) {
			/* Stop the cp */
			while ((ret = radeon_do_cp_idle(dev_priv)) != 0) {
				DRM_DEBUG("radeon_do_cp_idle %d\n", ret);
#ifdef __linux__
				schedule();
#else
				tsleep(&ret, PZERO, "rdnrel", 1);
#endif
			}
			radeon_do_cp_stop(dev_priv);
			radeon_do_engine_reset(dev);
		}

		/* Disable *all* interrupts */
		if (dev_priv->mmio)	/* remove this after permanent addmaps */
			RADEON_WRITE(RADEON_GEN_INT_CNTL, 0);

		if (dev_priv->mmio) {	/* remove all surfaces */
			for (i = 0; i < RADEON_MAX_SURFACES; i++) {
				RADEON_WRITE(RADEON_SURFACE0_INFO + 16 * i, 0);
				RADEON_WRITE(RADEON_SURFACE0_LOWER_BOUND +
					     16 * i, 0);
				RADEON_WRITE(RADEON_SURFACE0_UPPER_BOUND +
					     16 * i, 0);
			}
		}

		/* Free memory heap structures */
		radeon_mem_takedown(&(dev_priv->gart_heap));
		radeon_mem_takedown(&(dev_priv->fb_heap));

		/* deallocate kernel resources */
		radeon_do_cleanup_cp(dev);
	}
}

/* Just reset the CP ring.  Called as part of an X Server engine reset.
 */
int radeon_cp_reset(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (!dev_priv) {
		DRM_DEBUG("called before init done\n");
		return -EINVAL;
	}

	radeon_do_cp_reset(dev_priv);

	/* The CP is no longer running after an engine reset */
	dev_priv->cp_running = 0;

	return 0;
}

int radeon_cp_idle(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	return radeon_do_cp_idle(dev_priv);
}

/* Added by Charl P. Botha to call radeon_do_resume_cp().
 */
int radeon_cp_resume(struct drm_device *dev, void *data, struct drm_file *file_priv)
{

	return radeon_do_resume_cp(dev);
}

int radeon_engine_reset(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	return radeon_do_engine_reset(dev);
}

/* ================================================================
 * Fullscreen mode
 */

/* KW: Deprecated to say the least:
 */
int radeon_fullscreen(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return 0;
}

/* ================================================================
 * Freelist management
 */

/* Original comment: FIXME: ROTATE_BUFS is a hack to cycle through
 *   bufs until freelist code is used.  Note this hides a problem with
 *   the scratch register * (used to keep track of last buffer
 *   completed) being written to before * the last buffer has actually
 *   completed rendering.
 *
 * KW:  It's also a good way to find free buffers quickly.
 *
 * KW: Ideally this loop wouldn't exist, and freelist_get wouldn't
 * sleep.  However, bugs in older versions of radeon_accel.c mean that
 * we essentially have to do this, else old clients will break.
 *
 * However, it does leave open a potential deadlock where all the
 * buffers are held by other clients, which can't release them because
 * they can't get the lock.
 */

struct drm_buf *radeon_freelist_get(struct drm_device * dev)
{
	struct drm_device_dma *dma = dev->dma;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv;
	struct drm_buf *buf;
	int i, t;
	int start;

	if (++dev_priv->last_buf >= dma->buf_count)
		dev_priv->last_buf = 0;

	start = dev_priv->last_buf;

	for (t = 0; t < dev_priv->usec_timeout; t++) {
		u32 done_age = GET_SCRATCH(1);
		DRM_DEBUG("done_age = %d\n", done_age);
		for (i = start; i < dma->buf_count; i++) {
			buf = dma->buflist[i];
			buf_priv = buf->dev_private;
			if (buf->file_priv == NULL || (buf->pending &&
						       buf_priv->age <=
						       done_age)) {
				dev_priv->stats.requested_bufs++;
				buf->pending = 0;
				return buf;
			}
			start = 0;
		}

		if (t) {
			DRM_UDELAY(1);
			dev_priv->stats.freelist_loops++;
		}
	}

	DRM_DEBUG("returning NULL!\n");
	return NULL;
}

#if 0
struct drm_buf *radeon_freelist_get(struct drm_device * dev)
{
	struct drm_device_dma *dma = dev->dma;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv;
	struct drm_buf *buf;
	int i, t;
	int start;
	u32 done_age = DRM_READ32(dev_priv->ring_rptr, RADEON_SCRATCHOFF(1));

	if (++dev_priv->last_buf >= dma->buf_count)
		dev_priv->last_buf = 0;

	start = dev_priv->last_buf;
	dev_priv->stats.freelist_loops++;

	for (t = 0; t < 2; t++) {
		for (i = start; i < dma->buf_count; i++) {
			buf = dma->buflist[i];
			buf_priv = buf->dev_private;
			if (buf->file_priv == 0 || (buf->pending &&
						    buf_priv->age <=
						    done_age)) {
				dev_priv->stats.requested_bufs++;
				buf->pending = 0;
				return buf;
			}
		}
		start = 0;
	}

	return NULL;
}
#endif

void radeon_freelist_reset(struct drm_device * dev)
{
	struct drm_device_dma *dma = dev->dma;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int i;

	dev_priv->last_buf = 0;
	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_radeon_buf_priv_t *buf_priv = buf->dev_private;
		buf_priv->age = 0;
	}
}

/* ================================================================
 * CP command submission
 */

int radeon_wait_ring(drm_radeon_private_t * dev_priv, int n)
{
	drm_radeon_ring_buffer_t *ring = &dev_priv->ring;
	int i;
	u32 last_head = GET_RING_HEAD(dev_priv);

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		u32 head = GET_RING_HEAD(dev_priv);

		ring->space = (head - ring->tail) * sizeof(u32);
		if (ring->space <= 0)
			ring->space += ring->size;
		if (ring->space > n)
			return 0;

		dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

		if (head != last_head)
			i = 0;
		last_head = head;

		DRM_UDELAY(1);
	}

	/* FIXME: This return value is ignored in the BEGIN_RING macro! */
#if RADEON_FIFO_DEBUG
	radeon_status(dev_priv);
	DRM_ERROR("failed!\n");
#endif
	return -EBUSY;
}

static int radeon_cp_get_buffers(struct drm_device *dev,
				 struct drm_file *file_priv,
				 struct drm_dma * d)
{
	int i;
	struct drm_buf *buf;

	for (i = d->granted_count; i < d->request_count; i++) {
		buf = radeon_freelist_get(dev);
		if (!buf)
			return -EBUSY;	/* NOTE: broken client */

		buf->file_priv = file_priv;

		if (DRM_COPY_TO_USER(&d->request_indices[i], &buf->idx,
				     sizeof(buf->idx)))
			return -EFAULT;
		if (DRM_COPY_TO_USER(&d->request_sizes[i], &buf->total,
				     sizeof(buf->total)))
			return -EFAULT;

		d->granted_count++;
	}
	return 0;
}

int radeon_cp_buffers(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	int ret = 0;
	struct drm_dma *d = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	/* Please don't send us buffers.
	 */
	if (d->send_count != 0) {
		DRM_ERROR("Process %d trying to send %d buffers via drmDMA\n",
			  DRM_CURRENTPID, d->send_count);
		return -EINVAL;
	}

	/* We'll send you buffers.
	 */
	if (d->request_count < 0 || d->request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  DRM_CURRENTPID, d->request_count, dma->buf_count);
		return -EINVAL;
	}

	d->granted_count = 0;

	if (d->request_count) {
		ret = radeon_cp_get_buffers(dev, file_priv, d);
	}

	return ret;
}

int radeon_driver_load(struct drm_device *dev, unsigned long flags)
{
	drm_radeon_private_t *dev_priv;
	int ret = 0;

	dev_priv = drm_alloc(sizeof(drm_radeon_private_t), DRM_MEM_DRIVER);
	if (dev_priv == NULL)
		return -ENOMEM;

	memset(dev_priv, 0, sizeof(drm_radeon_private_t));
	dev->dev_private = (void *)dev_priv;
	dev_priv->flags = flags;

	switch (flags & RADEON_FAMILY_MASK) {
	case CHIP_R100:
	case CHIP_RV200:
	case CHIP_R200:
	case CHIP_R300:
	case CHIP_R350:
	case CHIP_R420:
	case CHIP_RV410:
	case CHIP_RV515:
	case CHIP_R520:
	case CHIP_RV570:
	case CHIP_R580:
		dev_priv->flags |= RADEON_HAS_HIERZ;
		break;
	default:
		/* all other chips have no hierarchical z buffer */
		break;
	}

	if (drm_device_is_agp(dev))
		dev_priv->flags |= RADEON_IS_AGP;
	else if (drm_device_is_pcie(dev))
		dev_priv->flags |= RADEON_IS_PCIE;
	else
		dev_priv->flags |= RADEON_IS_PCI;

	DRM_DEBUG("%s card detected\n",
		  ((dev_priv->flags & RADEON_IS_AGP) ? "AGP" : (((dev_priv->flags & RADEON_IS_PCIE) ? "PCIE" : "PCI"))));
	return ret;
}

/* Create mappings for registers and framebuffer so userland doesn't necessarily
 * have to find them.
 */
int radeon_driver_firstopen(struct drm_device *dev)
{
	int ret;
	drm_local_map_t *map;
	drm_radeon_private_t *dev_priv = dev->dev_private;

	dev_priv->gart_info.table_size = RADEON_PCIGART_TABLE_SIZE;

	ret = drm_addmap(dev, drm_get_resource_start(dev, 2),
			 drm_get_resource_len(dev, 2), _DRM_REGISTERS,
			 _DRM_READ_ONLY, &dev_priv->mmio);
	if (ret != 0)
		return ret;

	dev_priv->fb_aper_offset = drm_get_resource_start(dev, 0);
	ret = drm_addmap(dev, dev_priv->fb_aper_offset,
			 drm_get_resource_len(dev, 0), _DRM_FRAME_BUFFER,
			 _DRM_WRITE_COMBINING, &map);
	if (ret != 0)
		return ret;

	return 0;
}

int radeon_driver_unload(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("\n");
	drm_free(dev_priv, sizeof(*dev_priv), DRM_MEM_DRIVER);

	dev->dev_private = NULL;
	return 0;
}
