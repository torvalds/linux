/* mga_dma.c -- DMA support for mga g200/g400 -*- linux-c -*-
 * Created: Mon Dec 13 01:50:01 1999 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
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
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Jeff Hartmann <jhartmann@valinux.com>
 *    Keith Whitwell <keith@tungstengraphics.com>
 *
 * Rewritten by:
 *    Gareth Hughes <gareth@valinux.com>
 */

#include "drmP.h"
#include "drm.h"
#include "mga_drm.h"
#include "mga_drv.h"

#define MGA_DEFAULT_USEC_TIMEOUT	10000
#define MGA_FREELIST_DEBUG		0

static int mga_do_cleanup_dma( drm_device_t *dev );

/* ================================================================
 * Engine control
 */

int mga_do_wait_for_idle( drm_mga_private_t *dev_priv )
{
	u32 status = 0;
	int i;
	DRM_DEBUG( "\n" );

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		status = MGA_READ( MGA_STATUS ) & MGA_ENGINE_IDLE_MASK;
		if ( status == MGA_ENDPRDMASTS ) {
			MGA_WRITE8( MGA_CRTC_INDEX, 0 );
			return 0;
		}
		DRM_UDELAY( 1 );
	}

#if MGA_DMA_DEBUG
	DRM_ERROR( "failed!\n" );
	DRM_INFO( "   status=0x%08x\n", status );
#endif
	return DRM_ERR(EBUSY);
}

static int mga_do_dma_reset( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;

	DRM_DEBUG( "\n" );

	/* The primary DMA stream should look like new right about now.
	 */
	primary->tail = 0;
	primary->space = primary->size;
	primary->last_flush = 0;

	sarea_priv->last_wrap = 0;

	/* FIXME: Reset counters, buffer ages etc...
	 */

	/* FIXME: What else do we need to reinitialize?  WARP stuff?
	 */

	return 0;
}

/* ================================================================
 * Primary DMA stream
 */

void mga_do_dma_flush( drm_mga_private_t *dev_priv )
{
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;
	u32 head, tail;
	u32 status = 0;
	int i;
 	DMA_LOCALS;
	DRM_DEBUG( "\n" );

        /* We need to wait so that we can do an safe flush */
	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		status = MGA_READ( MGA_STATUS ) & MGA_ENGINE_IDLE_MASK;
		if ( status == MGA_ENDPRDMASTS ) break;
		DRM_UDELAY( 1 );
	}

	if ( primary->tail == primary->last_flush ) {
		DRM_DEBUG( "   bailing out...\n" );
		return;
	}

	tail = primary->tail + dev_priv->primary->offset;

	/* We need to pad the stream between flushes, as the card
	 * actually (partially?) reads the first of these commands.
	 * See page 4-16 in the G400 manual, middle of the page or so.
	 */
	BEGIN_DMA( 1 );

	DMA_BLOCK( MGA_DMAPAD,  0x00000000,
		   MGA_DMAPAD,  0x00000000,
		   MGA_DMAPAD,  0x00000000,
		   MGA_DMAPAD,	0x00000000 );

	ADVANCE_DMA();

	primary->last_flush = primary->tail;

	head = MGA_READ( MGA_PRIMADDRESS );

	if ( head <= tail ) {
		primary->space = primary->size - primary->tail;
	} else {
		primary->space = head - tail;
	}

	DRM_DEBUG( "   head = 0x%06lx\n", head - dev_priv->primary->offset );
	DRM_DEBUG( "   tail = 0x%06lx\n", tail - dev_priv->primary->offset );
	DRM_DEBUG( "  space = 0x%06x\n", primary->space );

	mga_flush_write_combine();
	MGA_WRITE( MGA_PRIMEND, tail | MGA_PAGPXFER );

	DRM_DEBUG( "done.\n" );
}

void mga_do_dma_wrap_start( drm_mga_private_t *dev_priv )
{
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;
	u32 head, tail;
	DMA_LOCALS;
	DRM_DEBUG( "\n" );

	BEGIN_DMA_WRAP();

	DMA_BLOCK( MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000 );

	ADVANCE_DMA();

	tail = primary->tail + dev_priv->primary->offset;

	primary->tail = 0;
	primary->last_flush = 0;
	primary->last_wrap++;

	head = MGA_READ( MGA_PRIMADDRESS );

	if ( head == dev_priv->primary->offset ) {
		primary->space = primary->size;
	} else {
		primary->space = head - dev_priv->primary->offset;
	}

	DRM_DEBUG( "   head = 0x%06lx\n",
		  head - dev_priv->primary->offset );
	DRM_DEBUG( "   tail = 0x%06x\n", primary->tail );
	DRM_DEBUG( "   wrap = %d\n", primary->last_wrap );
	DRM_DEBUG( "  space = 0x%06x\n", primary->space );

	mga_flush_write_combine();
	MGA_WRITE( MGA_PRIMEND, tail | MGA_PAGPXFER );

	set_bit( 0, &primary->wrapped );
	DRM_DEBUG( "done.\n" );
}

void mga_do_dma_wrap_end( drm_mga_private_t *dev_priv )
{
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	u32 head = dev_priv->primary->offset;
	DRM_DEBUG( "\n" );

	sarea_priv->last_wrap++;
	DRM_DEBUG( "   wrap = %d\n", sarea_priv->last_wrap );

	mga_flush_write_combine();
	MGA_WRITE( MGA_PRIMADDRESS, head | MGA_DMA_GENERAL );

	clear_bit( 0, &primary->wrapped );
	DRM_DEBUG( "done.\n" );
}


/* ================================================================
 * Freelist management
 */

#define MGA_BUFFER_USED		~0
#define MGA_BUFFER_FREE		0

#if MGA_FREELIST_DEBUG
static void mga_freelist_print( drm_device_t *dev )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_freelist_t *entry;

	DRM_INFO( "\n" );
	DRM_INFO( "current dispatch: last=0x%x done=0x%x\n",
		  dev_priv->sarea_priv->last_dispatch,
		  (unsigned int)(MGA_READ( MGA_PRIMADDRESS ) -
				 dev_priv->primary->offset) );
	DRM_INFO( "current freelist:\n" );

	for ( entry = dev_priv->head->next ; entry ; entry = entry->next ) {
		DRM_INFO( "   %p   idx=%2d  age=0x%x 0x%06lx\n",
			  entry, entry->buf->idx, entry->age.head,
			  entry->age.head - dev_priv->primary->offset );
	}
	DRM_INFO( "\n" );
}
#endif

static int mga_freelist_init( drm_device_t *dev, drm_mga_private_t *dev_priv )
{
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_mga_buf_priv_t *buf_priv;
	drm_mga_freelist_t *entry;
	int i;
	DRM_DEBUG( "count=%d\n", dma->buf_count );

	dev_priv->head = drm_alloc( sizeof(drm_mga_freelist_t),
				     DRM_MEM_DRIVER );
	if ( dev_priv->head == NULL )
		return DRM_ERR(ENOMEM);

	memset( dev_priv->head, 0, sizeof(drm_mga_freelist_t) );
	SET_AGE( &dev_priv->head->age, MGA_BUFFER_USED, 0 );

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		buf = dma->buflist[i];
	        buf_priv = buf->dev_private;

		entry = drm_alloc( sizeof(drm_mga_freelist_t),
				    DRM_MEM_DRIVER );
		if ( entry == NULL )
			return DRM_ERR(ENOMEM);

		memset( entry, 0, sizeof(drm_mga_freelist_t) );

		entry->next = dev_priv->head->next;
		entry->prev = dev_priv->head;
		SET_AGE( &entry->age, MGA_BUFFER_FREE, 0 );
		entry->buf = buf;

		if ( dev_priv->head->next != NULL )
			dev_priv->head->next->prev = entry;
		if ( entry->next == NULL )
			dev_priv->tail = entry;

		buf_priv->list_entry = entry;
		buf_priv->discard = 0;
		buf_priv->dispatched = 0;

		dev_priv->head->next = entry;
	}

	return 0;
}

static void mga_freelist_cleanup( drm_device_t *dev )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_freelist_t *entry;
	drm_mga_freelist_t *next;
	DRM_DEBUG( "\n" );

	entry = dev_priv->head;
	while ( entry ) {
		next = entry->next;
		drm_free( entry, sizeof(drm_mga_freelist_t), DRM_MEM_DRIVER );
		entry = next;
	}

	dev_priv->head = dev_priv->tail = NULL;
}

#if 0
/* FIXME: Still needed?
 */
static void mga_freelist_reset( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_mga_buf_priv_t *buf_priv;
	int i;

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		buf = dma->buflist[i];
	        buf_priv = buf->dev_private;
		SET_AGE( &buf_priv->list_entry->age,
			 MGA_BUFFER_FREE, 0 );
	}
}
#endif

static drm_buf_t *mga_freelist_get( drm_device_t *dev )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_freelist_t *next;
	drm_mga_freelist_t *prev;
	drm_mga_freelist_t *tail = dev_priv->tail;
	u32 head, wrap;
	DRM_DEBUG( "\n" );

	head = MGA_READ( MGA_PRIMADDRESS );
	wrap = dev_priv->sarea_priv->last_wrap;

	DRM_DEBUG( "   tail=0x%06lx %d\n",
		   tail->age.head ?
		   tail->age.head - dev_priv->primary->offset : 0,
		   tail->age.wrap );
	DRM_DEBUG( "   head=0x%06lx %d\n",
		   head - dev_priv->primary->offset, wrap );

	if ( TEST_AGE( &tail->age, head, wrap ) ) {
		prev = dev_priv->tail->prev;
		next = dev_priv->tail;
		prev->next = NULL;
		next->prev = next->next = NULL;
		dev_priv->tail = prev;
		SET_AGE( &next->age, MGA_BUFFER_USED, 0 );
		return next->buf;
	}

	DRM_DEBUG( "returning NULL!\n" );
	return NULL;
}

int mga_freelist_put( drm_device_t *dev, drm_buf_t *buf )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
	drm_mga_freelist_t *head, *entry, *prev;

	DRM_DEBUG( "age=0x%06lx wrap=%d\n",
		   buf_priv->list_entry->age.head -
		   dev_priv->primary->offset,
		   buf_priv->list_entry->age.wrap );

	entry = buf_priv->list_entry;
	head = dev_priv->head;

	if ( buf_priv->list_entry->age.head == MGA_BUFFER_USED ) {
		SET_AGE( &entry->age, MGA_BUFFER_FREE, 0 );
		prev = dev_priv->tail;
		prev->next = entry;
		entry->prev = prev;
		entry->next = NULL;
	} else {
		prev = head->next;
		head->next = entry;
		prev->prev = entry;
		entry->prev = head;
		entry->next = prev;
	}

	return 0;
}


/* ================================================================
 * DMA initialization, cleanup
 */

static int mga_do_init_dma( drm_device_t *dev, drm_mga_init_t *init )
{
	drm_mga_private_t *dev_priv;
	int ret;
	DRM_DEBUG( "\n" );

	dev_priv = drm_alloc( sizeof(drm_mga_private_t), DRM_MEM_DRIVER );
	if ( !dev_priv )
		return DRM_ERR(ENOMEM);

	memset( dev_priv, 0, sizeof(drm_mga_private_t) );

	dev_priv->chipset = init->chipset;

	dev_priv->usec_timeout = MGA_DEFAULT_USEC_TIMEOUT;

	if ( init->sgram ) {
		dev_priv->clear_cmd = MGA_DWGCTL_CLEAR | MGA_ATYPE_BLK;
	} else {
		dev_priv->clear_cmd = MGA_DWGCTL_CLEAR | MGA_ATYPE_RSTR;
	}
	dev_priv->maccess	= init->maccess;

	dev_priv->fb_cpp	= init->fb_cpp;
	dev_priv->front_offset	= init->front_offset;
	dev_priv->front_pitch	= init->front_pitch;
	dev_priv->back_offset	= init->back_offset;
	dev_priv->back_pitch	= init->back_pitch;

	dev_priv->depth_cpp	= init->depth_cpp;
	dev_priv->depth_offset	= init->depth_offset;
	dev_priv->depth_pitch	= init->depth_pitch;

	/* FIXME: Need to support AGP textures...
	 */
	dev_priv->texture_offset = init->texture_offset[0];
	dev_priv->texture_size = init->texture_size[0];

	DRM_GETSAREA();

	if(!dev_priv->sarea) {
		DRM_ERROR( "failed to find sarea!\n" );
		/* Assign dev_private so we can do cleanup. */
		dev->dev_private = (void *)dev_priv;
		mga_do_cleanup_dma( dev );
		return DRM_ERR(EINVAL);
	}

	dev_priv->mmio = drm_core_findmap(dev, init->mmio_offset);
	if(!dev_priv->mmio) {
		DRM_ERROR( "failed to find mmio region!\n" );
		/* Assign dev_private so we can do cleanup. */
		dev->dev_private = (void *)dev_priv;
		mga_do_cleanup_dma( dev );
		return DRM_ERR(EINVAL);
	}
	dev_priv->status = drm_core_findmap(dev, init->status_offset);
	if(!dev_priv->status) {
		DRM_ERROR( "failed to find status page!\n" );
		/* Assign dev_private so we can do cleanup. */
		dev->dev_private = (void *)dev_priv;
		mga_do_cleanup_dma( dev );
		return DRM_ERR(EINVAL);
	}
	dev_priv->warp = drm_core_findmap(dev, init->warp_offset);
	if(!dev_priv->warp) {
		DRM_ERROR( "failed to find warp microcode region!\n" );
		/* Assign dev_private so we can do cleanup. */
		dev->dev_private = (void *)dev_priv;
		mga_do_cleanup_dma( dev );
		return DRM_ERR(EINVAL);
	}
	dev_priv->primary = drm_core_findmap(dev, init->primary_offset);
	if(!dev_priv->primary) {
		DRM_ERROR( "failed to find primary dma region!\n" );
		/* Assign dev_private so we can do cleanup. */
		dev->dev_private = (void *)dev_priv;
		mga_do_cleanup_dma( dev );
		return DRM_ERR(EINVAL);
	}
	dev->agp_buffer_map = drm_core_findmap(dev, init->buffers_offset);
	if(!dev->agp_buffer_map) {
		DRM_ERROR( "failed to find dma buffer region!\n" );
		/* Assign dev_private so we can do cleanup. */
		dev->dev_private = (void *)dev_priv;
		mga_do_cleanup_dma( dev );
		return DRM_ERR(EINVAL);
	}

	dev_priv->sarea_priv =
		(drm_mga_sarea_t *)((u8 *)dev_priv->sarea->handle +
				    init->sarea_priv_offset);

	drm_core_ioremap( dev_priv->warp, dev );
	drm_core_ioremap( dev_priv->primary, dev );
	drm_core_ioremap( dev->agp_buffer_map, dev );

	if(!dev_priv->warp->handle ||
	   !dev_priv->primary->handle ||
	   !dev->agp_buffer_map->handle ) {
		DRM_ERROR( "failed to ioremap agp regions!\n" );
		/* Assign dev_private so we can do cleanup. */
		dev->dev_private = (void *)dev_priv;
		mga_do_cleanup_dma( dev );
		return DRM_ERR(ENOMEM);
	}

	ret = mga_warp_install_microcode( dev_priv );
	if ( ret < 0 ) {
		DRM_ERROR( "failed to install WARP ucode!\n" );
		/* Assign dev_private so we can do cleanup. */
		dev->dev_private = (void *)dev_priv;
		mga_do_cleanup_dma( dev );
		return ret;
	}

	ret = mga_warp_init( dev_priv );
	if ( ret < 0 ) {
		DRM_ERROR( "failed to init WARP engine!\n" );
		/* Assign dev_private so we can do cleanup. */
		dev->dev_private = (void *)dev_priv;
		mga_do_cleanup_dma( dev );
		return ret;
	}

	dev_priv->prim.status = (u32 *)dev_priv->status->handle;

	mga_do_wait_for_idle( dev_priv );

	/* Init the primary DMA registers.
	 */
	MGA_WRITE( MGA_PRIMADDRESS,
		   dev_priv->primary->offset | MGA_DMA_GENERAL );
#if 0
	MGA_WRITE( MGA_PRIMPTR,
		   virt_to_bus((void *)dev_priv->prim.status) |
		   MGA_PRIMPTREN0 |	/* Soft trap, SECEND, SETUPEND */
		   MGA_PRIMPTREN1 );	/* DWGSYNC */
#endif

	dev_priv->prim.start = (u8 *)dev_priv->primary->handle;
	dev_priv->prim.end = ((u8 *)dev_priv->primary->handle
			      + dev_priv->primary->size);
	dev_priv->prim.size = dev_priv->primary->size;

	dev_priv->prim.tail = 0;
	dev_priv->prim.space = dev_priv->prim.size;
	dev_priv->prim.wrapped = 0;

	dev_priv->prim.last_flush = 0;
	dev_priv->prim.last_wrap = 0;

	dev_priv->prim.high_mark = 256 * DMA_BLOCK_SIZE;

	dev_priv->prim.status[0] = dev_priv->primary->offset;
	dev_priv->prim.status[1] = 0;

	dev_priv->sarea_priv->last_wrap = 0;
	dev_priv->sarea_priv->last_frame.head = 0;
	dev_priv->sarea_priv->last_frame.wrap = 0;

	if ( mga_freelist_init( dev, dev_priv ) < 0 ) {
		DRM_ERROR( "could not initialize freelist\n" );
		/* Assign dev_private so we can do cleanup. */
		dev->dev_private = (void *)dev_priv;
		mga_do_cleanup_dma( dev );
		return DRM_ERR(ENOMEM);
	}

	/* Make dev_private visable to others. */
	dev->dev_private = (void *)dev_priv;
	return 0;
}

static int mga_do_cleanup_dma( drm_device_t *dev )
{
	DRM_DEBUG( "\n" );

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if ( dev->irq_enabled ) drm_irq_uninstall(dev);

	if ( dev->dev_private ) {
		drm_mga_private_t *dev_priv = dev->dev_private;

		if ( dev_priv->warp != NULL )
			drm_core_ioremapfree( dev_priv->warp, dev );
		if ( dev_priv->primary != NULL )
			drm_core_ioremapfree( dev_priv->primary, dev );
		if ( dev->agp_buffer_map != NULL )
			drm_core_ioremapfree( dev->agp_buffer_map, dev );

		if ( dev_priv->head != NULL ) {
			mga_freelist_cleanup( dev );
		}

		drm_free( dev->dev_private, sizeof(drm_mga_private_t),
			   DRM_MEM_DRIVER );
		dev->dev_private = NULL;
	}

	return 0;
}

int mga_dma_init( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_mga_init_t init;

	LOCK_TEST_WITH_RETURN( dev, filp );

	DRM_COPY_FROM_USER_IOCTL( init, (drm_mga_init_t __user *)data, sizeof(init) );

	switch ( init.func ) {
	case MGA_INIT_DMA:
		return mga_do_init_dma( dev, &init );
	case MGA_CLEANUP_DMA:
		return mga_do_cleanup_dma( dev );
	}

	return DRM_ERR(EINVAL);
}


/* ================================================================
 * Primary DMA stream management
 */

int mga_dma_flush( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_lock_t lock;

	LOCK_TEST_WITH_RETURN( dev, filp );

	DRM_COPY_FROM_USER_IOCTL( lock, (drm_lock_t __user *)data, sizeof(lock) );

	DRM_DEBUG( "%s%s%s\n",
		   (lock.flags & _DRM_LOCK_FLUSH) ?	"flush, " : "",
		   (lock.flags & _DRM_LOCK_FLUSH_ALL) ?	"flush all, " : "",
		   (lock.flags & _DRM_LOCK_QUIESCENT) ?	"idle, " : "" );

	WRAP_WAIT_WITH_RETURN( dev_priv );

	if ( lock.flags & (_DRM_LOCK_FLUSH | _DRM_LOCK_FLUSH_ALL) ) {
		mga_do_dma_flush( dev_priv );
	}

	if ( lock.flags & _DRM_LOCK_QUIESCENT ) {
#if MGA_DMA_DEBUG
		int ret = mga_do_wait_for_idle( dev_priv );
		if ( ret < 0 )
			DRM_INFO( "%s: -EBUSY\n", __FUNCTION__ );
		return ret;
#else
		return mga_do_wait_for_idle( dev_priv );
#endif
	} else {
		return 0;
	}
}

int mga_dma_reset( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;

	LOCK_TEST_WITH_RETURN( dev, filp );

	return mga_do_dma_reset( dev_priv );
}


/* ================================================================
 * DMA buffer management
 */

static int mga_dma_get_buffers( DRMFILE filp,
				drm_device_t *dev, drm_dma_t *d )
{
	drm_buf_t *buf;
	int i;

	for ( i = d->granted_count ; i < d->request_count ; i++ ) {
		buf = mga_freelist_get( dev );
		if ( !buf ) return DRM_ERR(EAGAIN);

		buf->filp = filp;

		if ( DRM_COPY_TO_USER( &d->request_indices[i],
				   &buf->idx, sizeof(buf->idx) ) )
			return DRM_ERR(EFAULT);
		if ( DRM_COPY_TO_USER( &d->request_sizes[i],
				   &buf->total, sizeof(buf->total) ) )
			return DRM_ERR(EFAULT);

		d->granted_count++;
	}
	return 0;
}

int mga_dma_buffers( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_device_dma_t *dma = dev->dma;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_dma_t __user *argp = (void __user *)data;
	drm_dma_t d;
	int ret = 0;

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

	WRAP_TEST_WITH_RETURN( dev_priv );

	d.granted_count = 0;

	if ( d.request_count ) {
		ret = mga_dma_get_buffers( filp, dev, &d );
	}

	DRM_COPY_TO_USER_IOCTL( argp, d, sizeof(d) );

	return ret;
}

void mga_driver_pretakedown(drm_device_t *dev)
{
	mga_do_cleanup_dma( dev );
}

int mga_driver_dma_quiescent(drm_device_t *dev)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	return mga_do_wait_for_idle( dev_priv );
}
