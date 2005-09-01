/* r128_cce.c -- ATI Rage 128 driver -*- linux-c -*-
 * Created: Wed Apr  5 19:24:19 2000 by kevin@precisioninsight.com
 *
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
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
 *    Gareth Hughes <gareth@valinux.com>
 */

#include "drmP.h"
#include "drm.h"
#include "r128_drm.h"
#include "r128_drv.h"

#define R128_FIFO_DEBUG		0

/* CCE microcode (from ATI) */
static u32 r128_cce_microcode[] = {
	0, 276838400, 0, 268449792, 2, 142, 2, 145, 0, 1076765731, 0,
	1617039951, 0, 774592877, 0, 1987540286, 0, 2307490946U, 0,
	599558925, 0, 589505315, 0, 596487092, 0, 589505315, 1,
	11544576, 1, 206848, 1, 311296, 1, 198656, 2, 912273422, 11,
	262144, 0, 0, 1, 33559837, 1, 7438, 1, 14809, 1, 6615, 12, 28,
	1, 6614, 12, 28, 2, 23, 11, 18874368, 0, 16790922, 1, 409600, 9,
	30, 1, 147854772, 16, 420483072, 3, 8192, 0, 10240, 1, 198656,
	1, 15630, 1, 51200, 10, 34858, 9, 42, 1, 33559823, 2, 10276, 1,
	15717, 1, 15718, 2, 43, 1, 15936948, 1, 570480831, 1, 14715071,
	12, 322123831, 1, 33953125, 12, 55, 1, 33559908, 1, 15718, 2,
	46, 4, 2099258, 1, 526336, 1, 442623, 4, 4194365, 1, 509952, 1,
	459007, 3, 0, 12, 92, 2, 46, 12, 176, 1, 15734, 1, 206848, 1,
	18432, 1, 133120, 1, 100670734, 1, 149504, 1, 165888, 1,
	15975928, 1, 1048576, 6, 3145806, 1, 15715, 16, 2150645232U, 2,
	268449859, 2, 10307, 12, 176, 1, 15734, 1, 15735, 1, 15630, 1,
	15631, 1, 5253120, 6, 3145810, 16, 2150645232U, 1, 15864, 2, 82,
	1, 343310, 1, 1064207, 2, 3145813, 1, 15728, 1, 7817, 1, 15729,
	3, 15730, 12, 92, 2, 98, 1, 16168, 1, 16167, 1, 16002, 1, 16008,
	1, 15974, 1, 15975, 1, 15990, 1, 15976, 1, 15977, 1, 15980, 0,
	15981, 1, 10240, 1, 5253120, 1, 15720, 1, 198656, 6, 110, 1,
	180224, 1, 103824738, 2, 112, 2, 3145839, 0, 536885440, 1,
	114880, 14, 125, 12, 206975, 1, 33559995, 12, 198784, 0,
	33570236, 1, 15803, 0, 15804, 3, 294912, 1, 294912, 3, 442370,
	1, 11544576, 0, 811612160, 1, 12593152, 1, 11536384, 1,
	14024704, 7, 310382726, 0, 10240, 1, 14796, 1, 14797, 1, 14793,
	1, 14794, 0, 14795, 1, 268679168, 1, 9437184, 1, 268449792, 1,
	198656, 1, 9452827, 1, 1075854602, 1, 1075854603, 1, 557056, 1,
	114880, 14, 159, 12, 198784, 1, 1109409213, 12, 198783, 1,
	1107312059, 12, 198784, 1, 1109409212, 2, 162, 1, 1075854781, 1,
	1073757627, 1, 1075854780, 1, 540672, 1, 10485760, 6, 3145894,
	16, 274741248, 9, 168, 3, 4194304, 3, 4209949, 0, 0, 0, 256, 14,
	174, 1, 114857, 1, 33560007, 12, 176, 0, 10240, 1, 114858, 1,
	33560018, 1, 114857, 3, 33560007, 1, 16008, 1, 114874, 1,
	33560360, 1, 114875, 1, 33560154, 0, 15963, 0, 256, 0, 4096, 1,
	409611, 9, 188, 0, 10240, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static int R128_READ_PLL(drm_device_t *dev, int addr)
{
	drm_r128_private_t *dev_priv = dev->dev_private;

	R128_WRITE8(R128_CLOCK_CNTL_INDEX, addr & 0x1f);
	return R128_READ(R128_CLOCK_CNTL_DATA);
}

#if R128_FIFO_DEBUG
static void r128_status( drm_r128_private_t *dev_priv )
{
	printk( "GUI_STAT           = 0x%08x\n",
		(unsigned int)R128_READ( R128_GUI_STAT ) );
	printk( "PM4_STAT           = 0x%08x\n",
		(unsigned int)R128_READ( R128_PM4_STAT ) );
	printk( "PM4_BUFFER_DL_WPTR = 0x%08x\n",
		(unsigned int)R128_READ( R128_PM4_BUFFER_DL_WPTR ) );
	printk( "PM4_BUFFER_DL_RPTR = 0x%08x\n",
		(unsigned int)R128_READ( R128_PM4_BUFFER_DL_RPTR ) );
	printk( "PM4_MICRO_CNTL     = 0x%08x\n",
		(unsigned int)R128_READ( R128_PM4_MICRO_CNTL ) );
	printk( "PM4_BUFFER_CNTL    = 0x%08x\n",
		(unsigned int)R128_READ( R128_PM4_BUFFER_CNTL ) );
}
#endif


/* ================================================================
 * Engine, FIFO control
 */

static int r128_do_pixcache_flush( drm_r128_private_t *dev_priv )
{
	u32 tmp;
	int i;

	tmp = R128_READ( R128_PC_NGUI_CTLSTAT ) | R128_PC_FLUSH_ALL;
	R128_WRITE( R128_PC_NGUI_CTLSTAT, tmp );

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		if ( !(R128_READ( R128_PC_NGUI_CTLSTAT ) & R128_PC_BUSY) ) {
			return 0;
		}
		DRM_UDELAY( 1 );
	}

#if R128_FIFO_DEBUG
	DRM_ERROR( "failed!\n" );
#endif
	return DRM_ERR(EBUSY);
}

static int r128_do_wait_for_fifo( drm_r128_private_t *dev_priv, int entries )
{
	int i;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		int slots = R128_READ( R128_GUI_STAT ) & R128_GUI_FIFOCNT_MASK;
		if ( slots >= entries ) return 0;
		DRM_UDELAY( 1 );
	}

#if R128_FIFO_DEBUG
	DRM_ERROR( "failed!\n" );
#endif
	return DRM_ERR(EBUSY);
}

static int r128_do_wait_for_idle( drm_r128_private_t *dev_priv )
{
	int i, ret;

	ret = r128_do_wait_for_fifo( dev_priv, 64 );
	if ( ret ) return ret;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		if ( !(R128_READ( R128_GUI_STAT ) & R128_GUI_ACTIVE) ) {
			r128_do_pixcache_flush( dev_priv );
			return 0;
		}
		DRM_UDELAY( 1 );
	}

#if R128_FIFO_DEBUG
	DRM_ERROR( "failed!\n" );
#endif
	return DRM_ERR(EBUSY);
}


/* ================================================================
 * CCE control, initialization
 */

/* Load the microcode for the CCE */
static void r128_cce_load_microcode( drm_r128_private_t *dev_priv )
{
	int i;

	DRM_DEBUG( "\n" );

	r128_do_wait_for_idle( dev_priv );

	R128_WRITE( R128_PM4_MICROCODE_ADDR, 0 );
	for ( i = 0 ; i < 256 ; i++ ) {
		R128_WRITE( R128_PM4_MICROCODE_DATAH,
			    r128_cce_microcode[i * 2] );
		R128_WRITE( R128_PM4_MICROCODE_DATAL,
			    r128_cce_microcode[i * 2 + 1] );
	}
}

/* Flush any pending commands to the CCE.  This should only be used just
 * prior to a wait for idle, as it informs the engine that the command
 * stream is ending.
 */
static void r128_do_cce_flush( drm_r128_private_t *dev_priv )
{
	u32 tmp;

	tmp = R128_READ( R128_PM4_BUFFER_DL_WPTR ) | R128_PM4_BUFFER_DL_DONE;
	R128_WRITE( R128_PM4_BUFFER_DL_WPTR, tmp );
}

/* Wait for the CCE to go idle.
 */
int r128_do_cce_idle( drm_r128_private_t *dev_priv )
{
	int i;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		if ( GET_RING_HEAD( dev_priv ) == dev_priv->ring.tail ) {
			int pm4stat = R128_READ( R128_PM4_STAT );
			if ( ( (pm4stat & R128_PM4_FIFOCNT_MASK) >=
			       dev_priv->cce_fifo_size ) &&
			     !(pm4stat & (R128_PM4_BUSY |
					  R128_PM4_GUI_ACTIVE)) ) {
				return r128_do_pixcache_flush( dev_priv );
			}
		}
		DRM_UDELAY( 1 );
	}

#if R128_FIFO_DEBUG
	DRM_ERROR( "failed!\n" );
	r128_status( dev_priv );
#endif
	return DRM_ERR(EBUSY);
}

/* Start the Concurrent Command Engine.
 */
static void r128_do_cce_start( drm_r128_private_t *dev_priv )
{
	r128_do_wait_for_idle( dev_priv );

	R128_WRITE( R128_PM4_BUFFER_CNTL,
		    dev_priv->cce_mode | dev_priv->ring.size_l2qw
		    | R128_PM4_BUFFER_CNTL_NOUPDATE );
	R128_READ( R128_PM4_BUFFER_ADDR ); /* as per the sample code */
	R128_WRITE( R128_PM4_MICRO_CNTL, R128_PM4_MICRO_FREERUN );

	dev_priv->cce_running = 1;
}

/* Reset the Concurrent Command Engine.  This will not flush any pending
 * commands, so you must wait for the CCE command stream to complete
 * before calling this routine.
 */
static void r128_do_cce_reset( drm_r128_private_t *dev_priv )
{
	R128_WRITE( R128_PM4_BUFFER_DL_WPTR, 0 );
	R128_WRITE( R128_PM4_BUFFER_DL_RPTR, 0 );
	dev_priv->ring.tail = 0;
}

/* Stop the Concurrent Command Engine.  This will not flush any pending
 * commands, so you must flush the command stream and wait for the CCE
 * to go idle before calling this routine.
 */
static void r128_do_cce_stop( drm_r128_private_t *dev_priv )
{
	R128_WRITE( R128_PM4_MICRO_CNTL, 0 );
	R128_WRITE( R128_PM4_BUFFER_CNTL,
		    R128_PM4_NONPM4 | R128_PM4_BUFFER_CNTL_NOUPDATE );

	dev_priv->cce_running = 0;
}

/* Reset the engine.  This will stop the CCE if it is running.
 */
static int r128_do_engine_reset( drm_device_t *dev )
{
	drm_r128_private_t *dev_priv = dev->dev_private;
	u32 clock_cntl_index, mclk_cntl, gen_reset_cntl;

	r128_do_pixcache_flush( dev_priv );

	clock_cntl_index = R128_READ( R128_CLOCK_CNTL_INDEX );
	mclk_cntl = R128_READ_PLL( dev, R128_MCLK_CNTL );

	R128_WRITE_PLL( R128_MCLK_CNTL,
			mclk_cntl | R128_FORCE_GCP | R128_FORCE_PIPE3D_CP );

	gen_reset_cntl = R128_READ( R128_GEN_RESET_CNTL );

	/* Taken from the sample code - do not change */
	R128_WRITE( R128_GEN_RESET_CNTL,
		    gen_reset_cntl | R128_SOFT_RESET_GUI );
	R128_READ( R128_GEN_RESET_CNTL );
	R128_WRITE( R128_GEN_RESET_CNTL,
		    gen_reset_cntl & ~R128_SOFT_RESET_GUI );
	R128_READ( R128_GEN_RESET_CNTL );

	R128_WRITE_PLL( R128_MCLK_CNTL, mclk_cntl );
	R128_WRITE( R128_CLOCK_CNTL_INDEX, clock_cntl_index );
	R128_WRITE( R128_GEN_RESET_CNTL, gen_reset_cntl );

	/* Reset the CCE ring */
	r128_do_cce_reset( dev_priv );

	/* The CCE is no longer running after an engine reset */
	dev_priv->cce_running = 0;

	/* Reset any pending vertex, indirect buffers */
	r128_freelist_reset( dev );

	return 0;
}

static void r128_cce_init_ring_buffer( drm_device_t *dev,
				       drm_r128_private_t *dev_priv )
{
	u32 ring_start;
	u32 tmp;

	DRM_DEBUG( "\n" );

	/* The manual (p. 2) says this address is in "VM space".  This
	 * means it's an offset from the start of AGP space.
	 */
#if __OS_HAS_AGP
	if ( !dev_priv->is_pci )
		ring_start = dev_priv->cce_ring->offset - dev->agp->base;
	else
#endif
		ring_start = dev_priv->cce_ring->offset - 
				(unsigned long)dev->sg->virtual;

	R128_WRITE( R128_PM4_BUFFER_OFFSET, ring_start | R128_AGP_OFFSET );

	R128_WRITE( R128_PM4_BUFFER_DL_WPTR, 0 );
	R128_WRITE( R128_PM4_BUFFER_DL_RPTR, 0 );

	/* Set watermark control */
	R128_WRITE( R128_PM4_BUFFER_WM_CNTL,
		    ((R128_WATERMARK_L/4) << R128_WMA_SHIFT)
		    | ((R128_WATERMARK_M/4) << R128_WMB_SHIFT)
		    | ((R128_WATERMARK_N/4) << R128_WMC_SHIFT)
		    | ((R128_WATERMARK_K/64) << R128_WB_WM_SHIFT) );

	/* Force read.  Why?  Because it's in the examples... */
	R128_READ( R128_PM4_BUFFER_ADDR );

	/* Turn on bus mastering */
	tmp = R128_READ( R128_BUS_CNTL ) & ~R128_BUS_MASTER_DIS;
	R128_WRITE( R128_BUS_CNTL, tmp );
}

static int r128_do_init_cce( drm_device_t *dev, drm_r128_init_t *init )
{
	drm_r128_private_t *dev_priv;

	DRM_DEBUG( "\n" );

	dev_priv = drm_alloc( sizeof(drm_r128_private_t), DRM_MEM_DRIVER );
	if ( dev_priv == NULL )
		return DRM_ERR(ENOMEM);

	memset( dev_priv, 0, sizeof(drm_r128_private_t) );

	dev_priv->is_pci = init->is_pci;

	if ( dev_priv->is_pci && !dev->sg ) {
		DRM_ERROR( "PCI GART memory not allocated!\n" );
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce( dev );
		return DRM_ERR(EINVAL);
	}

	dev_priv->usec_timeout = init->usec_timeout;
	if ( dev_priv->usec_timeout < 1 ||
	     dev_priv->usec_timeout > R128_MAX_USEC_TIMEOUT ) {
		DRM_DEBUG( "TIMEOUT problem!\n" );
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce( dev );
		return DRM_ERR(EINVAL);
	}

	dev_priv->cce_mode = init->cce_mode;

	/* GH: Simple idle check.
	 */
	atomic_set( &dev_priv->idle_count, 0 );

	/* We don't support anything other than bus-mastering ring mode,
	 * but the ring can be in either AGP or PCI space for the ring
	 * read pointer.
	 */
	if ( ( init->cce_mode != R128_PM4_192BM ) &&
	     ( init->cce_mode != R128_PM4_128BM_64INDBM ) &&
	     ( init->cce_mode != R128_PM4_64BM_128INDBM ) &&
	     ( init->cce_mode != R128_PM4_64BM_64VCBM_64INDBM ) ) {
		DRM_DEBUG( "Bad cce_mode!\n" );
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce( dev );
		return DRM_ERR(EINVAL);
	}

	switch ( init->cce_mode ) {
	case R128_PM4_NONPM4:
		dev_priv->cce_fifo_size = 0;
		break;
	case R128_PM4_192PIO:
	case R128_PM4_192BM:
		dev_priv->cce_fifo_size = 192;
		break;
	case R128_PM4_128PIO_64INDBM:
	case R128_PM4_128BM_64INDBM:
		dev_priv->cce_fifo_size = 128;
		break;
	case R128_PM4_64PIO_128INDBM:
	case R128_PM4_64BM_128INDBM:
	case R128_PM4_64PIO_64VCBM_64INDBM:
	case R128_PM4_64BM_64VCBM_64INDBM:
	case R128_PM4_64PIO_64VCPIO_64INDPIO:
		dev_priv->cce_fifo_size = 64;
		break;
	}

	switch ( init->fb_bpp ) {
	case 16:
		dev_priv->color_fmt = R128_DATATYPE_RGB565;
		break;
	case 32:
	default:
		dev_priv->color_fmt = R128_DATATYPE_ARGB8888;
		break;
	}
	dev_priv->front_offset	= init->front_offset;
	dev_priv->front_pitch	= init->front_pitch;
	dev_priv->back_offset	= init->back_offset;
	dev_priv->back_pitch	= init->back_pitch;

	switch ( init->depth_bpp ) {
	case 16:
		dev_priv->depth_fmt = R128_DATATYPE_RGB565;
		break;
	case 24:
	case 32:
	default:
		dev_priv->depth_fmt = R128_DATATYPE_ARGB8888;
		break;
	}
	dev_priv->depth_offset	= init->depth_offset;
	dev_priv->depth_pitch	= init->depth_pitch;
	dev_priv->span_offset	= init->span_offset;

	dev_priv->front_pitch_offset_c = (((dev_priv->front_pitch/8) << 21) |
					  (dev_priv->front_offset >> 5));
	dev_priv->back_pitch_offset_c = (((dev_priv->back_pitch/8) << 21) |
					 (dev_priv->back_offset >> 5));
	dev_priv->depth_pitch_offset_c = (((dev_priv->depth_pitch/8) << 21) |
					  (dev_priv->depth_offset >> 5) |
					  R128_DST_TILE);
	dev_priv->span_pitch_offset_c = (((dev_priv->depth_pitch/8) << 21) |
					 (dev_priv->span_offset >> 5));

	DRM_GETSAREA();
	
	if(!dev_priv->sarea) {
		DRM_ERROR("could not find sarea!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce( dev );
		return DRM_ERR(EINVAL);
	}

	dev_priv->mmio = drm_core_findmap(dev, init->mmio_offset);
	if(!dev_priv->mmio) {
		DRM_ERROR("could not find mmio region!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce( dev );
		return DRM_ERR(EINVAL);
	}
	dev_priv->cce_ring = drm_core_findmap(dev, init->ring_offset);
	if(!dev_priv->cce_ring) {
		DRM_ERROR("could not find cce ring region!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce( dev );
		return DRM_ERR(EINVAL);
	}
	dev_priv->ring_rptr = drm_core_findmap(dev, init->ring_rptr_offset);
	if(!dev_priv->ring_rptr) {
		DRM_ERROR("could not find ring read pointer!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce( dev );
		return DRM_ERR(EINVAL);
	}
	dev->agp_buffer_token = init->buffers_offset;
	dev->agp_buffer_map = drm_core_findmap(dev, init->buffers_offset);
	if(!dev->agp_buffer_map) {
		DRM_ERROR("could not find dma buffer region!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce( dev );
		return DRM_ERR(EINVAL);
	}

	if ( !dev_priv->is_pci ) {
		dev_priv->agp_textures = drm_core_findmap(dev, init->agp_textures_offset);
		if(!dev_priv->agp_textures) {
			DRM_ERROR("could not find agp texture region!\n");
			dev->dev_private = (void *)dev_priv;
			r128_do_cleanup_cce( dev );
			return DRM_ERR(EINVAL);
		}
	}

	dev_priv->sarea_priv =
		(drm_r128_sarea_t *)((u8 *)dev_priv->sarea->handle +
				     init->sarea_priv_offset);

#if __OS_HAS_AGP
	if ( !dev_priv->is_pci ) {
		drm_core_ioremap( dev_priv->cce_ring, dev );
		drm_core_ioremap( dev_priv->ring_rptr, dev );
		drm_core_ioremap( dev->agp_buffer_map, dev );
		if(!dev_priv->cce_ring->handle ||
		   !dev_priv->ring_rptr->handle ||
		   !dev->agp_buffer_map->handle) {
			DRM_ERROR("Could not ioremap agp regions!\n");
			dev->dev_private = (void *)dev_priv;
			r128_do_cleanup_cce( dev );
			return DRM_ERR(ENOMEM);
		}
	} else
#endif
	{
		dev_priv->cce_ring->handle =
			(void *)dev_priv->cce_ring->offset;
		dev_priv->ring_rptr->handle =
			(void *)dev_priv->ring_rptr->offset;
		dev->agp_buffer_map->handle = (void *)dev->agp_buffer_map->offset;
	}

#if __OS_HAS_AGP
	if ( !dev_priv->is_pci )
		dev_priv->cce_buffers_offset = dev->agp->base;
	else
#endif
		dev_priv->cce_buffers_offset = (unsigned long)dev->sg->virtual;

	dev_priv->ring.start = (u32 *)dev_priv->cce_ring->handle;
	dev_priv->ring.end = ((u32 *)dev_priv->cce_ring->handle
			      + init->ring_size / sizeof(u32));
	dev_priv->ring.size = init->ring_size;
	dev_priv->ring.size_l2qw = drm_order( init->ring_size / 8 );

	dev_priv->ring.tail_mask =
		(dev_priv->ring.size / sizeof(u32)) - 1;

	dev_priv->ring.high_mark = 128;

	dev_priv->sarea_priv->last_frame = 0;
	R128_WRITE( R128_LAST_FRAME_REG, dev_priv->sarea_priv->last_frame );

	dev_priv->sarea_priv->last_dispatch = 0;
	R128_WRITE( R128_LAST_DISPATCH_REG,
		    dev_priv->sarea_priv->last_dispatch );

#if __OS_HAS_AGP
	if ( dev_priv->is_pci ) {
#endif
		if (!drm_ati_pcigart_init( dev, &dev_priv->phys_pci_gart,
     					    &dev_priv->bus_pci_gart) ) {
			DRM_ERROR( "failed to init PCI GART!\n" );
			dev->dev_private = (void *)dev_priv;
			r128_do_cleanup_cce( dev );
			return DRM_ERR(ENOMEM);
		}
		R128_WRITE( R128_PCI_GART_PAGE, dev_priv->bus_pci_gart );
#if __OS_HAS_AGP
	}
#endif

	r128_cce_init_ring_buffer( dev, dev_priv );
	r128_cce_load_microcode( dev_priv );

	dev->dev_private = (void *)dev_priv;

	r128_do_engine_reset( dev );

	return 0;
}

int r128_do_cleanup_cce( drm_device_t *dev )
{

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if ( dev->irq_enabled ) drm_irq_uninstall(dev);

	if ( dev->dev_private ) {
		drm_r128_private_t *dev_priv = dev->dev_private;

#if __OS_HAS_AGP
		if ( !dev_priv->is_pci ) {
			if ( dev_priv->cce_ring != NULL )
				drm_core_ioremapfree( dev_priv->cce_ring, dev );
			if ( dev_priv->ring_rptr != NULL )
				drm_core_ioremapfree( dev_priv->ring_rptr, dev );
			if ( dev->agp_buffer_map != NULL )
				drm_core_ioremapfree( dev->agp_buffer_map, dev );
		} else
#endif
		{
			if (!drm_ati_pcigart_cleanup( dev,
						dev_priv->phys_pci_gart,
						dev_priv->bus_pci_gart ))
				DRM_ERROR( "failed to cleanup PCI GART!\n" );
		}

		drm_free( dev->dev_private, sizeof(drm_r128_private_t),
			   DRM_MEM_DRIVER );
		dev->dev_private = NULL;
	}

	return 0;
}

int r128_cce_init( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_r128_init_t init;

	DRM_DEBUG( "\n" );

	LOCK_TEST_WITH_RETURN( dev, filp );

	DRM_COPY_FROM_USER_IOCTL( init, (drm_r128_init_t __user *)data, sizeof(init) );

	switch ( init.func ) {
	case R128_INIT_CCE:
		return r128_do_init_cce( dev, &init );
	case R128_CLEANUP_CCE:
		return r128_do_cleanup_cce( dev );
	}

	return DRM_ERR(EINVAL);
}

int r128_cce_start( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_r128_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG( "\n" );

	LOCK_TEST_WITH_RETURN( dev, filp );

	if ( dev_priv->cce_running || dev_priv->cce_mode == R128_PM4_NONPM4 ) {
		DRM_DEBUG( "%s while CCE running\n", __FUNCTION__ );
		return 0;
	}

	r128_do_cce_start( dev_priv );

	return 0;
}

/* Stop the CCE.  The engine must have been idled before calling this
 * routine.
 */
int r128_cce_stop( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_r128_cce_stop_t stop;
	int ret;
	DRM_DEBUG( "\n" );

	LOCK_TEST_WITH_RETURN( dev, filp );

	DRM_COPY_FROM_USER_IOCTL(stop, (drm_r128_cce_stop_t __user *)data, sizeof(stop) );

	/* Flush any pending CCE commands.  This ensures any outstanding
	 * commands are exectuted by the engine before we turn it off.
	 */
	if ( stop.flush ) {
		r128_do_cce_flush( dev_priv );
	}

	/* If we fail to make the engine go idle, we return an error
	 * code so that the DRM ioctl wrapper can try again.
	 */
	if ( stop.idle ) {
		ret = r128_do_cce_idle( dev_priv );
		if ( ret ) return ret;
	}

	/* Finally, we can turn off the CCE.  If the engine isn't idle,
	 * we will get some dropped triangles as they won't be fully
	 * rendered before the CCE is shut down.
	 */
	r128_do_cce_stop( dev_priv );

	/* Reset the engine */
	r128_do_engine_reset( dev );

	return 0;
}

/* Just reset the CCE ring.  Called as part of an X Server engine reset.
 */
int r128_cce_reset( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_r128_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG( "\n" );

	LOCK_TEST_WITH_RETURN( dev, filp );

	if ( !dev_priv ) {
		DRM_DEBUG( "%s called before init done\n", __FUNCTION__ );
		return DRM_ERR(EINVAL);
	}

	r128_do_cce_reset( dev_priv );

	/* The CCE is no longer running after an engine reset */
	dev_priv->cce_running = 0;

	return 0;
}

int r128_cce_idle( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_r128_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG( "\n" );

	LOCK_TEST_WITH_RETURN( dev, filp );

	if ( dev_priv->cce_running ) {
		r128_do_cce_flush( dev_priv );
	}

	return r128_do_cce_idle( dev_priv );
}

int r128_engine_reset( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	DRM_DEBUG( "\n" );

	LOCK_TEST_WITH_RETURN( dev, filp );

	return r128_do_engine_reset( dev );
}

int r128_fullscreen( DRM_IOCTL_ARGS )
{
	return DRM_ERR(EINVAL);
}


/* ================================================================
 * Freelist management
 */
#define R128_BUFFER_USED	0xffffffff
#define R128_BUFFER_FREE	0

#if 0
static int r128_freelist_init( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_buf_t *buf;
	drm_r128_buf_priv_t *buf_priv;
	drm_r128_freelist_t *entry;
	int i;

	dev_priv->head = drm_alloc( sizeof(drm_r128_freelist_t),
				     DRM_MEM_DRIVER );
	if ( dev_priv->head == NULL )
		return DRM_ERR(ENOMEM);

	memset( dev_priv->head, 0, sizeof(drm_r128_freelist_t) );
	dev_priv->head->age = R128_BUFFER_USED;

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		buf = dma->buflist[i];
		buf_priv = buf->dev_private;

		entry = drm_alloc( sizeof(drm_r128_freelist_t),
				    DRM_MEM_DRIVER );
		if ( !entry ) return DRM_ERR(ENOMEM);

		entry->age = R128_BUFFER_FREE;
		entry->buf = buf;
		entry->prev = dev_priv->head;
		entry->next = dev_priv->head->next;
		if ( !entry->next )
			dev_priv->tail = entry;

		buf_priv->discard = 0;
		buf_priv->dispatched = 0;
		buf_priv->list_entry = entry;

		dev_priv->head->next = entry;

		if ( dev_priv->head->next )
			dev_priv->head->next->prev = entry;
	}

	return 0;

}
#endif

static drm_buf_t *r128_freelist_get( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_r128_buf_priv_t *buf_priv;
	drm_buf_t *buf;
	int i, t;

	/* FIXME: Optimize -- use freelist code */

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		buf = dma->buflist[i];
		buf_priv = buf->dev_private;
		if ( buf->filp == 0 )
			return buf;
	}

	for ( t = 0 ; t < dev_priv->usec_timeout ; t++ ) {
		u32 done_age = R128_READ( R128_LAST_DISPATCH_REG );

		for ( i = 0 ; i < dma->buf_count ; i++ ) {
			buf = dma->buflist[i];
			buf_priv = buf->dev_private;
			if ( buf->pending && buf_priv->age <= done_age ) {
				/* The buffer has been processed, so it
				 * can now be used.
				 */
				buf->pending = 0;
				return buf;
			}
		}
		DRM_UDELAY( 1 );
	}

	DRM_DEBUG( "returning NULL!\n" );
	return NULL;
}

void r128_freelist_reset( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	int i;

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		drm_buf_t *buf = dma->buflist[i];
		drm_r128_buf_priv_t *buf_priv = buf->dev_private;
		buf_priv->age = 0;
	}
}


/* ================================================================
 * CCE command submission
 */

int r128_wait_ring( drm_r128_private_t *dev_priv, int n )
{
	drm_r128_ring_buffer_t *ring = &dev_priv->ring;
	int i;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		r128_update_ring_snapshot( dev_priv );
		if ( ring->space >= n )
			return 0;
		DRM_UDELAY( 1 );
	}

	/* FIXME: This is being ignored... */
	DRM_ERROR( "failed!\n" );
	return DRM_ERR(EBUSY);
}

static int r128_cce_get_buffers( DRMFILE filp, drm_device_t *dev, drm_dma_t *d )
{
	int i;
	drm_buf_t *buf;

	for ( i = d->granted_count ; i < d->request_count ; i++ ) {
		buf = r128_freelist_get( dev );
		if ( !buf ) return DRM_ERR(EAGAIN);

		buf->filp = filp;

		if ( DRM_COPY_TO_USER( &d->request_indices[i], &buf->idx,
				   sizeof(buf->idx) ) )
			return DRM_ERR(EFAULT);
		if ( DRM_COPY_TO_USER( &d->request_sizes[i], &buf->total,
				   sizeof(buf->total) ) )
			return DRM_ERR(EFAULT);

		d->granted_count++;
	}
	return 0;
}

int r128_cce_buffers( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_device_dma_t *dma = dev->dma;
	int ret = 0;
	drm_dma_t __user *argp = (void __user *)data;
	drm_dma_t d;

	LOCK_TEST_WITH_RETURN( dev, filp );

	DRM_COPY_FROM_USER_IOCTL( d, argp, sizeof(d) );

	/* Please don't send us buffers.
	 */
	if ( d.send_count != 0 ) {
		DRM_ERROR( "Process %d trying to send %d buffers via drmDMA\n",
			   DRM_CURRENTPID, d.send_count );
		return DRM_ERR(EINVAL);
	}

	/* We'll send you buffers.
	 */
	if ( d.request_count < 0 || d.request_count > dma->buf_count ) {
		DRM_ERROR( "Process %d trying to get %d buffers (of %d max)\n",
			   DRM_CURRENTPID, d.request_count, dma->buf_count );
		return DRM_ERR(EINVAL);
	}

	d.granted_count = 0;

	if ( d.request_count ) {
		ret = r128_cce_get_buffers( filp, dev, &d );
	}

	DRM_COPY_TO_USER_IOCTL(argp, d, sizeof(d) );

	return ret;
}
