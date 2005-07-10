/**
 * \file drm_fops.h 
 * File operations for DRM
 * 
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Daryll Strauss <daryll@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Mon Jan  4 08:58:31 1999 by faith@valinux.com
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include <linux/poll.h>

static int drm_open_helper(struct inode *inode, struct file *filp, drm_device_t *dev);

static int drm_setup( drm_device_t *dev )
{
	int i;
	int ret;

	if (dev->driver->presetup)
	{
		ret=dev->driver->presetup(dev);
		if (ret!=0) 
			return ret;
	}

	atomic_set( &dev->ioctl_count, 0 );
	atomic_set( &dev->vma_count, 0 );
	dev->buf_use = 0;
	atomic_set( &dev->buf_alloc, 0 );

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA))
	{
		i = drm_dma_setup( dev );
		if ( i < 0 )
			return i;
	}

	for ( i = 0 ; i < DRM_ARRAY_SIZE(dev->counts) ; i++ )
		atomic_set( &dev->counts[i], 0 );

	for ( i = 0 ; i < DRM_HASH_SIZE ; i++ ) {
		dev->magiclist[i].head = NULL;
		dev->magiclist[i].tail = NULL;
	}

	dev->ctxlist = drm_alloc(sizeof(*dev->ctxlist),
				  DRM_MEM_CTXLIST);
	if(dev->ctxlist == NULL) return -ENOMEM;
	memset(dev->ctxlist, 0, sizeof(*dev->ctxlist));
	INIT_LIST_HEAD(&dev->ctxlist->head);

	dev->vmalist = NULL;
	dev->sigdata.lock = dev->lock.hw_lock = NULL;
	init_waitqueue_head( &dev->lock.lock_queue );
	dev->queue_count = 0;
	dev->queue_reserved = 0;
	dev->queue_slots = 0;
	dev->queuelist = NULL;
	dev->irq_enabled = 0;
	dev->context_flag = 0;
	dev->interrupt_flag = 0;
	dev->dma_flag = 0;
	dev->last_context = 0;
	dev->last_switch = 0;
	dev->last_checked = 0;
	init_waitqueue_head( &dev->context_wait );
	dev->if_version = 0;

	dev->ctx_start = 0;
	dev->lck_start = 0;

	dev->buf_rp = dev->buf;
	dev->buf_wp = dev->buf;
	dev->buf_end = dev->buf + DRM_BSZ;
	dev->buf_async = NULL;
	init_waitqueue_head( &dev->buf_readers );
	init_waitqueue_head( &dev->buf_writers );

	DRM_DEBUG( "\n" );

	/*
	 * The kernel's context could be created here, but is now created
	 * in drm_dma_enqueue.	This is more resource-efficient for
	 * hardware that does not do DMA, but may mean that
	 * drm_select_queue fails between the time the interrupt is
	 * initialized and the time the queues are initialized.
	 */
	if (dev->driver->postsetup)
		dev->driver->postsetup(dev);

	return 0;
}

/**
 * Open file.
 * 
 * \param inode device inode
 * \param filp file pointer.
 * \return zero on success or a negative number on failure.
 *
 * Searches the DRM device with the same minor number, calls open_helper(), and
 * increments the device open count. If the open count was previous at zero,
 * i.e., it's the first that the device is open, then calls setup().
 */
int drm_open( struct inode *inode, struct file *filp )
{
	drm_device_t *dev = NULL;
	int minor = iminor(inode);
	int retcode = 0;

	if (!((minor >= 0) && (minor < drm_cards_limit)))
		return -ENODEV;
		
	if (!drm_heads[minor])
		return -ENODEV;

	if (!(dev = drm_heads[minor]->dev))
		return -ENODEV;
	
	retcode = drm_open_helper( inode, filp, dev );
	if ( !retcode ) {
		atomic_inc( &dev->counts[_DRM_STAT_OPENS] );
		spin_lock( &dev->count_lock );
		if ( !dev->open_count++ ) {
			spin_unlock( &dev->count_lock );
			return drm_setup( dev );
		}
		spin_unlock( &dev->count_lock );
	}

	return retcode;
}
EXPORT_SYMBOL(drm_open);

/**
 * Release file.
 *
 * \param inode device inode
 * \param filp file pointer.
 * \return zero on success or a negative number on failure.
 *
 * If the hardware lock is held then free it, and take it again for the kernel
 * context since it's necessary to reclaim buffers. Unlink the file private
 * data from its list and free it. Decreases the open count and if it reaches
 * zero calls takedown().
 */
int drm_release( struct inode *inode, struct file *filp )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev;
	int retcode = 0;

	lock_kernel();
	dev = priv->head->dev;

	DRM_DEBUG( "open_count = %d\n", dev->open_count );

	if (dev->driver->prerelease)
		dev->driver->prerelease(dev, filp);

	/* ========================================================
	 * Begin inline drm_release
	 */

	DRM_DEBUG( "pid = %d, device = 0x%lx, open_count = %d\n",
		   current->pid, (long)old_encode_dev(priv->head->device), dev->open_count );

	if ( priv->lock_count && dev->lock.hw_lock &&
	     _DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) &&
	     dev->lock.filp == filp ) {
		DRM_DEBUG( "File %p released, freeing lock for context %d\n",
			filp,
			_DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock) );
		
		if (dev->driver->release)
			dev->driver->release(dev, filp);

		drm_lock_free( dev, &dev->lock.hw_lock->lock,
				_DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock) );

				/* FIXME: may require heavy-handed reset of
                                   hardware at this point, possibly
                                   processed via a callback to the X
                                   server. */
	}
	else if ( dev->driver->release && priv->lock_count && dev->lock.hw_lock ) {
		/* The lock is required to reclaim buffers */
		DECLARE_WAITQUEUE( entry, current );

		add_wait_queue( &dev->lock.lock_queue, &entry );
		for (;;) {
			__set_current_state(TASK_INTERRUPTIBLE);
			if ( !dev->lock.hw_lock ) {
				/* Device has been unregistered */
				retcode = -EINTR;
				break;
			}
			if ( drm_lock_take( &dev->lock.hw_lock->lock,
					     DRM_KERNEL_CONTEXT ) ) {
				dev->lock.filp	    = filp;
				dev->lock.lock_time = jiffies;
                                atomic_inc( &dev->counts[_DRM_STAT_LOCKS] );
				break;	/* Got lock */
			}
				/* Contention */
			schedule();
			if ( signal_pending( current ) ) {
				retcode = -ERESTARTSYS;
				break;
			}
		}
		__set_current_state(TASK_RUNNING);
		remove_wait_queue( &dev->lock.lock_queue, &entry );
		if( !retcode ) {
			if (dev->driver->release)
				dev->driver->release(dev, filp);
			drm_lock_free( dev, &dev->lock.hw_lock->lock,
					DRM_KERNEL_CONTEXT );
		}
	}
	
	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA) && !dev->driver->release)
	{
		dev->driver->reclaim_buffers(dev, filp);
	}

	drm_fasync( -1, filp, 0 );

	down( &dev->ctxlist_sem );
	if ( dev->ctxlist && (!list_empty(&dev->ctxlist->head))) {
		drm_ctx_list_t *pos, *n;

		list_for_each_entry_safe( pos, n, &dev->ctxlist->head, head ) {
			if ( pos->tag == priv &&
			     pos->handle != DRM_KERNEL_CONTEXT ) {
				if (dev->driver->context_dtor)
					dev->driver->context_dtor(dev, pos->handle);

				drm_ctxbitmap_free( dev, pos->handle );

				list_del( &pos->head );
				drm_free( pos, sizeof(*pos), DRM_MEM_CTXLIST );
				--dev->ctx_count;
			}
		}
	}
	up( &dev->ctxlist_sem );

	down( &dev->struct_sem );
	if ( priv->remove_auth_on_close == 1 ) {
		drm_file_t *temp = dev->file_first;
		while ( temp ) {
			temp->authenticated = 0;
			temp = temp->next;
		}
	}
	if ( priv->prev ) {
		priv->prev->next = priv->next;
	} else {
		dev->file_first	 = priv->next;
	}
	if ( priv->next ) {
		priv->next->prev = priv->prev;
	} else {
		dev->file_last	 = priv->prev;
	}
	up( &dev->struct_sem );
	
	if (dev->driver->free_filp_priv)
		dev->driver->free_filp_priv(dev, priv);

	drm_free( priv, sizeof(*priv), DRM_MEM_FILES );

	/* ========================================================
	 * End inline drm_release
	 */

	atomic_inc( &dev->counts[_DRM_STAT_CLOSES] );
	spin_lock( &dev->count_lock );
	if ( !--dev->open_count ) {
		if ( atomic_read( &dev->ioctl_count ) || dev->blocked ) {
			DRM_ERROR( "Device busy: %d %d\n",
				   atomic_read( &dev->ioctl_count ),
				   dev->blocked );
			spin_unlock( &dev->count_lock );
			unlock_kernel();
			return -EBUSY;
		}
		spin_unlock( &dev->count_lock );
		unlock_kernel();
		return drm_takedown( dev );
	}
	spin_unlock( &dev->count_lock );

	unlock_kernel();

	return retcode;
}
EXPORT_SYMBOL(drm_release);

/**
 * Called whenever a process opens /dev/drm. 
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param dev device.
 * \return zero on success or a negative number on failure.
 * 
 * Creates and initializes a drm_file structure for the file private data in \p
 * filp and add it into the double linked list in \p dev.
 */
static int drm_open_helper(struct inode *inode, struct file *filp, drm_device_t *dev)
{
	int	     minor = iminor(inode);
	drm_file_t   *priv;
	int ret;

	if (filp->f_flags & O_EXCL)   return -EBUSY; /* No exclusive opens */
	if (!drm_cpu_valid())        return -EINVAL;

	DRM_DEBUG("pid = %d, minor = %d\n", current->pid, minor);

	priv		    = drm_alloc(sizeof(*priv), DRM_MEM_FILES);
	if(!priv) return -ENOMEM;

	memset(priv, 0, sizeof(*priv));
	filp->private_data  = priv;
	priv->uid	    = current->euid;
	priv->pid	    = current->pid;
	priv->minor	    = minor;
	priv->head          = drm_heads[minor];
	priv->ioctl_count   = 0;
	priv->authenticated = capable(CAP_SYS_ADMIN);
	priv->lock_count    = 0;

	if (dev->driver->open_helper) {
		ret=dev->driver->open_helper(dev, priv);
		if (ret < 0)
			goto out_free;
	}

	down(&dev->struct_sem);
	if (!dev->file_last) {
		priv->next	= NULL;
		priv->prev	= NULL;
		dev->file_first = priv;
		dev->file_last	= priv;
	} else {
		priv->next	     = NULL;
		priv->prev	     = dev->file_last;
		dev->file_last->next = priv;
		dev->file_last	     = priv;
	}
	up(&dev->struct_sem);

#ifdef __alpha__
	/*
	 * Default the hose
	 */
	if (!dev->hose) {
		struct pci_dev *pci_dev;
		pci_dev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, NULL);
		if (pci_dev) {
			dev->hose = pci_dev->sysdata;
			pci_dev_put(pci_dev);
		}
		if (!dev->hose) {
			struct pci_bus *b = pci_bus_b(pci_root_buses.next);
			if (b) dev->hose = b->sysdata;
		}
	}
#endif

	return 0;
out_free:
	drm_free(priv, sizeof(*priv), DRM_MEM_FILES);
	filp->private_data=NULL;
	return ret;
}

/** No-op. */
int drm_flush(struct file *filp)
{
	drm_file_t    *priv   = filp->private_data;
	drm_device_t  *dev    = priv->head->dev;

	DRM_DEBUG("pid = %d, device = 0x%lx, open_count = %d\n",
		  current->pid, (long)old_encode_dev(priv->head->device), dev->open_count);
	return 0;
}
EXPORT_SYMBOL(drm_flush);

/** No-op. */
int drm_fasync(int fd, struct file *filp, int on)
{
	drm_file_t    *priv   = filp->private_data;
	drm_device_t  *dev    = priv->head->dev;
	int	      retcode;

	DRM_DEBUG("fd = %d, device = 0x%lx\n", fd, (long)old_encode_dev(priv->head->device));
	retcode = fasync_helper(fd, filp, on, &dev->buf_async);
	if (retcode < 0) return retcode;
	return 0;
}
EXPORT_SYMBOL(drm_fasync);

/** No-op. */
unsigned int drm_poll(struct file *filp, struct poll_table_struct *wait)
{
	return 0;
}
EXPORT_SYMBOL(drm_poll);

