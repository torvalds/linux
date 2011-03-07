/**
 * \file drm_irq.c
 * IRQ support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Fri Mar 19 14:30:16 1999 by faith@valinux.com
 *
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
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
#include "drm_trace.h"

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/slab.h>

#include <linux/vgaarb.h>

/* Access macro for slots in vblank timestamp ringbuffer. */
#define vblanktimestamp(dev, crtc, count) ( \
	(dev)->_vblank_time[(crtc) * DRM_VBLANKTIME_RBSIZE + \
	((count) % DRM_VBLANKTIME_RBSIZE)])

/* Retry timestamp calculation up to 3 times to satisfy
 * drm_timestamp_precision before giving up.
 */
#define DRM_TIMESTAMP_MAXRETRIES 3

/* Threshold in nanoseconds for detection of redundant
 * vblank irq in drm_handle_vblank(). 1 msec should be ok.
 */
#define DRM_REDUNDANT_VBLIRQ_THRESH_NS 1000000

/**
 * Get interrupt from bus id.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_irq_busid structure.
 * \return zero on success or a negative number on failure.
 *
 * Finds the PCI device with the specified bus id and gets its IRQ number.
 * This IOCTL is deprecated, and will now return EINVAL for any busid not equal
 * to that of the device that this DRM instance attached to.
 */
int drm_irq_by_busid(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct drm_irq_busid *p = data;

	if (drm_core_check_feature(dev, DRIVER_USE_PLATFORM_DEVICE))
		return -EINVAL;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
		return -EINVAL;

	if ((p->busnum >> 8) != drm_get_pci_domain(dev) ||
	    (p->busnum & 0xff) != dev->pdev->bus->number ||
	    p->devnum != PCI_SLOT(dev->pdev->devfn) || p->funcnum != PCI_FUNC(dev->pdev->devfn))
		return -EINVAL;

	p->irq = dev->pdev->irq;

	DRM_DEBUG("%d:%d:%d => IRQ %d\n", p->busnum, p->devnum, p->funcnum,
		  p->irq);

	return 0;
}

/*
 * Clear vblank timestamp buffer for a crtc.
 */
static void clear_vblank_timestamps(struct drm_device *dev, int crtc)
{
	memset(&dev->_vblank_time[crtc * DRM_VBLANKTIME_RBSIZE], 0,
		DRM_VBLANKTIME_RBSIZE * sizeof(struct timeval));
}

/*
 * Disable vblank irq's on crtc, make sure that last vblank count
 * of hardware and corresponding consistent software vblank counter
 * are preserved, even if there are any spurious vblank irq's after
 * disable.
 */
static void vblank_disable_and_save(struct drm_device *dev, int crtc)
{
	unsigned long irqflags;
	u32 vblcount;
	s64 diff_ns;
	int vblrc;
	struct timeval tvblank;

	/* Prevent vblank irq processing while disabling vblank irqs,
	 * so no updates of timestamps or count can happen after we've
	 * disabled. Needed to prevent races in case of delayed irq's.
	 * Disable preemption, so vblank_time_lock is held as short as
	 * possible, even under a kernel with PREEMPT_RT patches.
	 */
	preempt_disable();
	spin_lock_irqsave(&dev->vblank_time_lock, irqflags);

	dev->driver->disable_vblank(dev, crtc);
	dev->vblank_enabled[crtc] = 0;

	/* No further vblank irq's will be processed after
	 * this point. Get current hardware vblank count and
	 * vblank timestamp, repeat until they are consistent.
	 *
	 * FIXME: There is still a race condition here and in
	 * drm_update_vblank_count() which can cause off-by-one
	 * reinitialization of software vblank counter. If gpu
	 * vblank counter doesn't increment exactly at the leading
	 * edge of a vblank interval, then we can lose 1 count if
	 * we happen to execute between start of vblank and the
	 * delayed gpu counter increment.
	 */
	do {
		dev->last_vblank[crtc] = dev->driver->get_vblank_counter(dev, crtc);
		vblrc = drm_get_last_vbltimestamp(dev, crtc, &tvblank, 0);
	} while (dev->last_vblank[crtc] != dev->driver->get_vblank_counter(dev, crtc));

	/* Compute time difference to stored timestamp of last vblank
	 * as updated by last invocation of drm_handle_vblank() in vblank irq.
	 */
	vblcount = atomic_read(&dev->_vblank_count[crtc]);
	diff_ns = timeval_to_ns(&tvblank) -
		  timeval_to_ns(&vblanktimestamp(dev, crtc, vblcount));

	/* If there is at least 1 msec difference between the last stored
	 * timestamp and tvblank, then we are currently executing our
	 * disable inside a new vblank interval, the tvblank timestamp
	 * corresponds to this new vblank interval and the irq handler
	 * for this vblank didn't run yet and won't run due to our disable.
	 * Therefore we need to do the job of drm_handle_vblank() and
	 * increment the vblank counter by one to account for this vblank.
	 *
	 * Skip this step if there isn't any high precision timestamp
	 * available. In that case we can't account for this and just
	 * hope for the best.
	 */
	if ((vblrc > 0) && (abs64(diff_ns) > 1000000)) {
		atomic_inc(&dev->_vblank_count[crtc]);
		smp_mb__after_atomic_inc();
	}

	/* Invalidate all timestamps while vblank irq's are off. */
	clear_vblank_timestamps(dev, crtc);

	spin_unlock_irqrestore(&dev->vblank_time_lock, irqflags);
	preempt_enable();
}

static void vblank_disable_fn(unsigned long arg)
{
	struct drm_device *dev = (struct drm_device *)arg;
	unsigned long irqflags;
	int i;

	if (!dev->vblank_disable_allowed)
		return;

	for (i = 0; i < dev->num_crtcs; i++) {
		spin_lock_irqsave(&dev->vbl_lock, irqflags);
		if (atomic_read(&dev->vblank_refcount[i]) == 0 &&
		    dev->vblank_enabled[i]) {
			DRM_DEBUG("disabling vblank on crtc %d\n", i);
			vblank_disable_and_save(dev, i);
		}
		spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
	}
}

void drm_vblank_cleanup(struct drm_device *dev)
{
	/* Bail if the driver didn't call drm_vblank_init() */
	if (dev->num_crtcs == 0)
		return;

	del_timer(&dev->vblank_disable_timer);

	vblank_disable_fn((unsigned long)dev);

	kfree(dev->vbl_queue);
	kfree(dev->_vblank_count);
	kfree(dev->vblank_refcount);
	kfree(dev->vblank_enabled);
	kfree(dev->last_vblank);
	kfree(dev->last_vblank_wait);
	kfree(dev->vblank_inmodeset);
	kfree(dev->_vblank_time);

	dev->num_crtcs = 0;
}
EXPORT_SYMBOL(drm_vblank_cleanup);

int drm_vblank_init(struct drm_device *dev, int num_crtcs)
{
	int i, ret = -ENOMEM;

	setup_timer(&dev->vblank_disable_timer, vblank_disable_fn,
		    (unsigned long)dev);
	spin_lock_init(&dev->vbl_lock);
	spin_lock_init(&dev->vblank_time_lock);

	dev->num_crtcs = num_crtcs;

	dev->vbl_queue = kmalloc(sizeof(wait_queue_head_t) * num_crtcs,
				 GFP_KERNEL);
	if (!dev->vbl_queue)
		goto err;

	dev->_vblank_count = kmalloc(sizeof(atomic_t) * num_crtcs, GFP_KERNEL);
	if (!dev->_vblank_count)
		goto err;

	dev->vblank_refcount = kmalloc(sizeof(atomic_t) * num_crtcs,
				       GFP_KERNEL);
	if (!dev->vblank_refcount)
		goto err;

	dev->vblank_enabled = kcalloc(num_crtcs, sizeof(int), GFP_KERNEL);
	if (!dev->vblank_enabled)
		goto err;

	dev->last_vblank = kcalloc(num_crtcs, sizeof(u32), GFP_KERNEL);
	if (!dev->last_vblank)
		goto err;

	dev->last_vblank_wait = kcalloc(num_crtcs, sizeof(u32), GFP_KERNEL);
	if (!dev->last_vblank_wait)
		goto err;

	dev->vblank_inmodeset = kcalloc(num_crtcs, sizeof(int), GFP_KERNEL);
	if (!dev->vblank_inmodeset)
		goto err;

	dev->_vblank_time = kcalloc(num_crtcs * DRM_VBLANKTIME_RBSIZE,
				    sizeof(struct timeval), GFP_KERNEL);
	if (!dev->_vblank_time)
		goto err;

	DRM_INFO("Supports vblank timestamp caching Rev 1 (10.10.2010).\n");

	/* Driver specific high-precision vblank timestamping supported? */
	if (dev->driver->get_vblank_timestamp)
		DRM_INFO("Driver supports precise vblank timestamp query.\n");
	else
		DRM_INFO("No driver support for vblank timestamp query.\n");

	/* Zero per-crtc vblank stuff */
	for (i = 0; i < num_crtcs; i++) {
		init_waitqueue_head(&dev->vbl_queue[i]);
		atomic_set(&dev->_vblank_count[i], 0);
		atomic_set(&dev->vblank_refcount[i], 0);
	}

	dev->vblank_disable_allowed = 0;
	return 0;

err:
	drm_vblank_cleanup(dev);
	return ret;
}
EXPORT_SYMBOL(drm_vblank_init);

static void drm_irq_vgaarb_nokms(void *cookie, bool state)
{
	struct drm_device *dev = cookie;

	if (dev->driver->vgaarb_irq) {
		dev->driver->vgaarb_irq(dev, state);
		return;
	}

	if (!dev->irq_enabled)
		return;

	if (state)
		dev->driver->irq_uninstall(dev);
	else {
		dev->driver->irq_preinstall(dev);
		dev->driver->irq_postinstall(dev);
	}
}

/**
 * Install IRQ handler.
 *
 * \param dev DRM device.
 *
 * Initializes the IRQ related data. Installs the handler, calling the driver
 * \c drm_driver_irq_preinstall() and \c drm_driver_irq_postinstall() functions
 * before and after the installation.
 */
int drm_irq_install(struct drm_device *dev)
{
	int ret = 0;
	unsigned long sh_flags = 0;
	char *irqname;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
		return -EINVAL;

	if (drm_dev_to_irq(dev) == 0)
		return -EINVAL;

	mutex_lock(&dev->struct_mutex);

	/* Driver must have been initialized */
	if (!dev->dev_private) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	if (dev->irq_enabled) {
		mutex_unlock(&dev->struct_mutex);
		return -EBUSY;
	}
	dev->irq_enabled = 1;
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("irq=%d\n", drm_dev_to_irq(dev));

	/* Before installing handler */
	dev->driver->irq_preinstall(dev);

	/* Install handler */
	if (drm_core_check_feature(dev, DRIVER_IRQ_SHARED))
		sh_flags = IRQF_SHARED;

	if (dev->devname)
		irqname = dev->devname;
	else
		irqname = dev->driver->name;

	ret = request_irq(drm_dev_to_irq(dev), dev->driver->irq_handler,
			  sh_flags, irqname, dev);

	if (ret < 0) {
		mutex_lock(&dev->struct_mutex);
		dev->irq_enabled = 0;
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		vga_client_register(dev->pdev, (void *)dev, drm_irq_vgaarb_nokms, NULL);

	/* After installing handler */
	ret = dev->driver->irq_postinstall(dev);
	if (ret < 0) {
		mutex_lock(&dev->struct_mutex);
		dev->irq_enabled = 0;
		mutex_unlock(&dev->struct_mutex);
	}

	return ret;
}
EXPORT_SYMBOL(drm_irq_install);

/**
 * Uninstall the IRQ handler.
 *
 * \param dev DRM device.
 *
 * Calls the driver's \c drm_driver_irq_uninstall() function, and stops the irq.
 */
int drm_irq_uninstall(struct drm_device *dev)
{
	unsigned long irqflags;
	int irq_enabled, i;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
		return -EINVAL;

	mutex_lock(&dev->struct_mutex);
	irq_enabled = dev->irq_enabled;
	dev->irq_enabled = 0;
	mutex_unlock(&dev->struct_mutex);

	/*
	 * Wake up any waiters so they don't hang.
	 */
	spin_lock_irqsave(&dev->vbl_lock, irqflags);
	for (i = 0; i < dev->num_crtcs; i++) {
		DRM_WAKEUP(&dev->vbl_queue[i]);
		dev->vblank_enabled[i] = 0;
		dev->last_vblank[i] = dev->driver->get_vblank_counter(dev, i);
	}
	spin_unlock_irqrestore(&dev->vbl_lock, irqflags);

	if (!irq_enabled)
		return -EINVAL;

	DRM_DEBUG("irq=%d\n", drm_dev_to_irq(dev));

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		vga_client_register(dev->pdev, NULL, NULL, NULL);

	dev->driver->irq_uninstall(dev);

	free_irq(drm_dev_to_irq(dev), dev);

	return 0;
}
EXPORT_SYMBOL(drm_irq_uninstall);

/**
 * IRQ control ioctl.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_control structure.
 * \return zero on success or a negative number on failure.
 *
 * Calls irq_install() or irq_uninstall() according to \p arg.
 */
int drm_control(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_control *ctl = data;

	/* if we haven't irq we fallback for compatibility reasons -
	 * this used to be a separate function in drm_dma.h
	 */


	switch (ctl->func) {
	case DRM_INST_HANDLER:
		if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
			return 0;
		if (drm_core_check_feature(dev, DRIVER_MODESET))
			return 0;
		if (dev->if_version < DRM_IF_VERSION(1, 2) &&
		    ctl->irq != drm_dev_to_irq(dev))
			return -EINVAL;
		return drm_irq_install(dev);
	case DRM_UNINST_HANDLER:
		if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
			return 0;
		if (drm_core_check_feature(dev, DRIVER_MODESET))
			return 0;
		return drm_irq_uninstall(dev);
	default:
		return -EINVAL;
	}
}

/**
 * drm_calc_timestamping_constants - Calculate and
 * store various constants which are later needed by
 * vblank and swap-completion timestamping, e.g, by
 * drm_calc_vbltimestamp_from_scanoutpos().
 * They are derived from crtc's true scanout timing,
 * so they take things like panel scaling or other
 * adjustments into account.
 *
 * @crtc drm_crtc whose timestamp constants should be updated.
 *
 */
void drm_calc_timestamping_constants(struct drm_crtc *crtc)
{
	s64 linedur_ns = 0, pixeldur_ns = 0, framedur_ns = 0;
	u64 dotclock;

	/* Dot clock in Hz: */
	dotclock = (u64) crtc->hwmode.clock * 1000;

	/* Fields of interlaced scanout modes are only halve a frame duration.
	 * Double the dotclock to get halve the frame-/line-/pixelduration.
	 */
	if (crtc->hwmode.flags & DRM_MODE_FLAG_INTERLACE)
		dotclock *= 2;

	/* Valid dotclock? */
	if (dotclock > 0) {
		/* Convert scanline length in pixels and video dot clock to
		 * line duration, frame duration and pixel duration in
		 * nanoseconds:
		 */
		pixeldur_ns = (s64) div64_u64(1000000000, dotclock);
		linedur_ns  = (s64) div64_u64(((u64) crtc->hwmode.crtc_htotal *
					      1000000000), dotclock);
		framedur_ns = (s64) crtc->hwmode.crtc_vtotal * linedur_ns;
	} else
		DRM_ERROR("crtc %d: Can't calculate constants, dotclock = 0!\n",
			  crtc->base.id);

	crtc->pixeldur_ns = pixeldur_ns;
	crtc->linedur_ns  = linedur_ns;
	crtc->framedur_ns = framedur_ns;

	DRM_DEBUG("crtc %d: hwmode: htotal %d, vtotal %d, vdisplay %d\n",
		  crtc->base.id, crtc->hwmode.crtc_htotal,
		  crtc->hwmode.crtc_vtotal, crtc->hwmode.crtc_vdisplay);
	DRM_DEBUG("crtc %d: clock %d kHz framedur %d linedur %d, pixeldur %d\n",
		  crtc->base.id, (int) dotclock/1000, (int) framedur_ns,
		  (int) linedur_ns, (int) pixeldur_ns);
}
EXPORT_SYMBOL(drm_calc_timestamping_constants);

/**
 * drm_calc_vbltimestamp_from_scanoutpos - helper routine for kms
 * drivers. Implements calculation of exact vblank timestamps from
 * given drm_display_mode timings and current video scanout position
 * of a crtc. This can be called from within get_vblank_timestamp()
 * implementation of a kms driver to implement the actual timestamping.
 *
 * Should return timestamps conforming to the OML_sync_control OpenML
 * extension specification. The timestamp corresponds to the end of
 * the vblank interval, aka start of scanout of topmost-leftmost display
 * pixel in the following video frame.
 *
 * Requires support for optional dev->driver->get_scanout_position()
 * in kms driver, plus a bit of setup code to provide a drm_display_mode
 * that corresponds to the true scanout timing.
 *
 * The current implementation only handles standard video modes. It
 * returns as no operation if a doublescan or interlaced video mode is
 * active. Higher level code is expected to handle this.
 *
 * @dev: DRM device.
 * @crtc: Which crtc's vblank timestamp to retrieve.
 * @max_error: Desired maximum allowable error in timestamps (nanosecs).
 *             On return contains true maximum error of timestamp.
 * @vblank_time: Pointer to struct timeval which should receive the timestamp.
 * @flags: Flags to pass to driver:
 *         0 = Default.
 *         DRM_CALLED_FROM_VBLIRQ = If function is called from vbl irq handler.
 * @refcrtc: drm_crtc* of crtc which defines scanout timing.
 *
 * Returns negative value on error, failure or if not supported in current
 * video mode:
 *
 * -EINVAL   - Invalid crtc.
 * -EAGAIN   - Temporary unavailable, e.g., called before initial modeset.
 * -ENOTSUPP - Function not supported in current display mode.
 * -EIO      - Failed, e.g., due to failed scanout position query.
 *
 * Returns or'ed positive status flags on success:
 *
 * DRM_VBLANKTIME_SCANOUTPOS_METHOD - Signal this method used for timestamping.
 * DRM_VBLANKTIME_INVBL - Timestamp taken while scanout was in vblank interval.
 *
 */
int drm_calc_vbltimestamp_from_scanoutpos(struct drm_device *dev, int crtc,
					  int *max_error,
					  struct timeval *vblank_time,
					  unsigned flags,
					  struct drm_crtc *refcrtc)
{
	struct timeval stime, raw_time;
	struct drm_display_mode *mode;
	int vbl_status, vtotal, vdisplay;
	int vpos, hpos, i;
	s64 framedur_ns, linedur_ns, pixeldur_ns, delta_ns, duration_ns;
	bool invbl;

	if (crtc < 0 || crtc >= dev->num_crtcs) {
		DRM_ERROR("Invalid crtc %d\n", crtc);
		return -EINVAL;
	}

	/* Scanout position query not supported? Should not happen. */
	if (!dev->driver->get_scanout_position) {
		DRM_ERROR("Called from driver w/o get_scanout_position()!?\n");
		return -EIO;
	}

	mode = &refcrtc->hwmode;
	vtotal = mode->crtc_vtotal;
	vdisplay = mode->crtc_vdisplay;

	/* Durations of frames, lines, pixels in nanoseconds. */
	framedur_ns = refcrtc->framedur_ns;
	linedur_ns  = refcrtc->linedur_ns;
	pixeldur_ns = refcrtc->pixeldur_ns;

	/* If mode timing undefined, just return as no-op:
	 * Happens during initial modesetting of a crtc.
	 */
	if (vtotal <= 0 || vdisplay <= 0 || framedur_ns == 0) {
		DRM_DEBUG("crtc %d: Noop due to uninitialized mode.\n", crtc);
		return -EAGAIN;
	}

	/* Get current scanout position with system timestamp.
	 * Repeat query up to DRM_TIMESTAMP_MAXRETRIES times
	 * if single query takes longer than max_error nanoseconds.
	 *
	 * This guarantees a tight bound on maximum error if
	 * code gets preempted or delayed for some reason.
	 */
	for (i = 0; i < DRM_TIMESTAMP_MAXRETRIES; i++) {
		/* Disable preemption to make it very likely to
		 * succeed in the first iteration even on PREEMPT_RT kernel.
		 */
		preempt_disable();

		/* Get system timestamp before query. */
		do_gettimeofday(&stime);

		/* Get vertical and horizontal scanout pos. vpos, hpos. */
		vbl_status = dev->driver->get_scanout_position(dev, crtc, &vpos, &hpos);

		/* Get system timestamp after query. */
		do_gettimeofday(&raw_time);

		preempt_enable();

		/* Return as no-op if scanout query unsupported or failed. */
		if (!(vbl_status & DRM_SCANOUTPOS_VALID)) {
			DRM_DEBUG("crtc %d : scanoutpos query failed [%d].\n",
				  crtc, vbl_status);
			return -EIO;
		}

		duration_ns = timeval_to_ns(&raw_time) - timeval_to_ns(&stime);

		/* Accept result with <  max_error nsecs timing uncertainty. */
		if (duration_ns <= (s64) *max_error)
			break;
	}

	/* Noisy system timing? */
	if (i == DRM_TIMESTAMP_MAXRETRIES) {
		DRM_DEBUG("crtc %d: Noisy timestamp %d us > %d us [%d reps].\n",
			  crtc, (int) duration_ns/1000, *max_error/1000, i);
	}

	/* Return upper bound of timestamp precision error. */
	*max_error = (int) duration_ns;

	/* Check if in vblank area:
	 * vpos is >=0 in video scanout area, but negative
	 * within vblank area, counting down the number of lines until
	 * start of scanout.
	 */
	invbl = vbl_status & DRM_SCANOUTPOS_INVBL;

	/* Convert scanout position into elapsed time at raw_time query
	 * since start of scanout at first display scanline. delta_ns
	 * can be negative if start of scanout hasn't happened yet.
	 */
	delta_ns = (s64) vpos * linedur_ns + (s64) hpos * pixeldur_ns;

	/* Is vpos outside nominal vblank area, but less than
	 * 1/100 of a frame height away from start of vblank?
	 * If so, assume this isn't a massively delayed vblank
	 * interrupt, but a vblank interrupt that fired a few
	 * microseconds before true start of vblank. Compensate
	 * by adding a full frame duration to the final timestamp.
	 * Happens, e.g., on ATI R500, R600.
	 *
	 * We only do this if DRM_CALLED_FROM_VBLIRQ.
	 */
	if ((flags & DRM_CALLED_FROM_VBLIRQ) && !invbl &&
	    ((vdisplay - vpos) < vtotal / 100)) {
		delta_ns = delta_ns - framedur_ns;

		/* Signal this correction as "applied". */
		vbl_status |= 0x8;
	}

	/* Subtract time delta from raw timestamp to get final
	 * vblank_time timestamp for end of vblank.
	 */
	*vblank_time = ns_to_timeval(timeval_to_ns(&raw_time) - delta_ns);

	DRM_DEBUG("crtc %d : v %d p(%d,%d)@ %d.%d -> %d.%d [e %d us, %d rep]\n",
		  crtc, (int) vbl_status, hpos, vpos, raw_time.tv_sec,
		  raw_time.tv_usec, vblank_time->tv_sec, vblank_time->tv_usec,
		  (int) duration_ns/1000, i);

	vbl_status = DRM_VBLANKTIME_SCANOUTPOS_METHOD;
	if (invbl)
		vbl_status |= DRM_VBLANKTIME_INVBL;

	return vbl_status;
}
EXPORT_SYMBOL(drm_calc_vbltimestamp_from_scanoutpos);

/**
 * drm_get_last_vbltimestamp - retrieve raw timestamp for the most recent
 * vblank interval.
 *
 * @dev: DRM device
 * @crtc: which crtc's vblank timestamp to retrieve
 * @tvblank: Pointer to target struct timeval which should receive the timestamp
 * @flags: Flags to pass to driver:
 *         0 = Default.
 *         DRM_CALLED_FROM_VBLIRQ = If function is called from vbl irq handler.
 *
 * Fetches the system timestamp corresponding to the time of the most recent
 * vblank interval on specified crtc. May call into kms-driver to
 * compute the timestamp with a high-precision GPU specific method.
 *
 * Returns zero if timestamp originates from uncorrected do_gettimeofday()
 * call, i.e., it isn't very precisely locked to the true vblank.
 *
 * Returns non-zero if timestamp is considered to be very precise.
 */
u32 drm_get_last_vbltimestamp(struct drm_device *dev, int crtc,
			      struct timeval *tvblank, unsigned flags)
{
	int ret = 0;

	/* Define requested maximum error on timestamps (nanoseconds). */
	int max_error = (int) drm_timestamp_precision * 1000;

	/* Query driver if possible and precision timestamping enabled. */
	if (dev->driver->get_vblank_timestamp && (max_error > 0)) {
		ret = dev->driver->get_vblank_timestamp(dev, crtc, &max_error,
							tvblank, flags);
		if (ret > 0)
			return (u32) ret;
	}

	/* GPU high precision timestamp query unsupported or failed.
	 * Return gettimeofday timestamp as best estimate.
	 */
	do_gettimeofday(tvblank);

	return 0;
}
EXPORT_SYMBOL(drm_get_last_vbltimestamp);

/**
 * drm_vblank_count - retrieve "cooked" vblank counter value
 * @dev: DRM device
 * @crtc: which counter to retrieve
 *
 * Fetches the "cooked" vblank count value that represents the number of
 * vblank events since the system was booted, including lost events due to
 * modesetting activity.
 */
u32 drm_vblank_count(struct drm_device *dev, int crtc)
{
	return atomic_read(&dev->_vblank_count[crtc]);
}
EXPORT_SYMBOL(drm_vblank_count);

/**
 * drm_vblank_count_and_time - retrieve "cooked" vblank counter value
 * and the system timestamp corresponding to that vblank counter value.
 *
 * @dev: DRM device
 * @crtc: which counter to retrieve
 * @vblanktime: Pointer to struct timeval to receive the vblank timestamp.
 *
 * Fetches the "cooked" vblank count value that represents the number of
 * vblank events since the system was booted, including lost events due to
 * modesetting activity. Returns corresponding system timestamp of the time
 * of the vblank interval that corresponds to the current value vblank counter
 * value.
 */
u32 drm_vblank_count_and_time(struct drm_device *dev, int crtc,
			      struct timeval *vblanktime)
{
	u32 cur_vblank;

	/* Read timestamp from slot of _vblank_time ringbuffer
	 * that corresponds to current vblank count. Retry if
	 * count has incremented during readout. This works like
	 * a seqlock.
	 */
	do {
		cur_vblank = atomic_read(&dev->_vblank_count[crtc]);
		*vblanktime = vblanktimestamp(dev, crtc, cur_vblank);
		smp_rmb();
	} while (cur_vblank != atomic_read(&dev->_vblank_count[crtc]));

	return cur_vblank;
}
EXPORT_SYMBOL(drm_vblank_count_and_time);

/**
 * drm_update_vblank_count - update the master vblank counter
 * @dev: DRM device
 * @crtc: counter to update
 *
 * Call back into the driver to update the appropriate vblank counter
 * (specified by @crtc).  Deal with wraparound, if it occurred, and
 * update the last read value so we can deal with wraparound on the next
 * call if necessary.
 *
 * Only necessary when going from off->on, to account for frames we
 * didn't get an interrupt for.
 *
 * Note: caller must hold dev->vbl_lock since this reads & writes
 * device vblank fields.
 */
static void drm_update_vblank_count(struct drm_device *dev, int crtc)
{
	u32 cur_vblank, diff, tslot, rc;
	struct timeval t_vblank;

	/*
	 * Interrupts were disabled prior to this call, so deal with counter
	 * wrap if needed.
	 * NOTE!  It's possible we lost a full dev->max_vblank_count events
	 * here if the register is small or we had vblank interrupts off for
	 * a long time.
	 *
	 * We repeat the hardware vblank counter & timestamp query until
	 * we get consistent results. This to prevent races between gpu
	 * updating its hardware counter while we are retrieving the
	 * corresponding vblank timestamp.
	 */
	do {
		cur_vblank = dev->driver->get_vblank_counter(dev, crtc);
		rc = drm_get_last_vbltimestamp(dev, crtc, &t_vblank, 0);
	} while (cur_vblank != dev->driver->get_vblank_counter(dev, crtc));

	/* Deal with counter wrap */
	diff = cur_vblank - dev->last_vblank[crtc];
	if (cur_vblank < dev->last_vblank[crtc]) {
		diff += dev->max_vblank_count;

		DRM_DEBUG("last_vblank[%d]=0x%x, cur_vblank=0x%x => diff=0x%x\n",
			  crtc, dev->last_vblank[crtc], cur_vblank, diff);
	}

	DRM_DEBUG("enabling vblank interrupts on crtc %d, missed %d\n",
		  crtc, diff);

	/* Reinitialize corresponding vblank timestamp if high-precision query
	 * available. Skip this step if query unsupported or failed. Will
	 * reinitialize delayed at next vblank interrupt in that case.
	 */
	if (rc) {
		tslot = atomic_read(&dev->_vblank_count[crtc]) + diff;
		vblanktimestamp(dev, crtc, tslot) = t_vblank;
	}

	smp_mb__before_atomic_inc();
	atomic_add(diff, &dev->_vblank_count[crtc]);
	smp_mb__after_atomic_inc();
}

/**
 * drm_vblank_get - get a reference count on vblank events
 * @dev: DRM device
 * @crtc: which CRTC to own
 *
 * Acquire a reference count on vblank events to avoid having them disabled
 * while in use.
 *
 * RETURNS
 * Zero on success, nonzero on failure.
 */
int drm_vblank_get(struct drm_device *dev, int crtc)
{
	unsigned long irqflags, irqflags2;
	int ret = 0;

	spin_lock_irqsave(&dev->vbl_lock, irqflags);
	/* Going from 0->1 means we have to enable interrupts again */
	if (atomic_add_return(1, &dev->vblank_refcount[crtc]) == 1) {
		/* Disable preemption while holding vblank_time_lock. Do
		 * it explicitely to guard against PREEMPT_RT kernel.
		 */
		preempt_disable();
		spin_lock_irqsave(&dev->vblank_time_lock, irqflags2);
		if (!dev->vblank_enabled[crtc]) {
			/* Enable vblank irqs under vblank_time_lock protection.
			 * All vblank count & timestamp updates are held off
			 * until we are done reinitializing master counter and
			 * timestamps. Filtercode in drm_handle_vblank() will
			 * prevent double-accounting of same vblank interval.
			 */
			ret = dev->driver->enable_vblank(dev, crtc);
			DRM_DEBUG("enabling vblank on crtc %d, ret: %d\n",
				  crtc, ret);
			if (ret)
				atomic_dec(&dev->vblank_refcount[crtc]);
			else {
				dev->vblank_enabled[crtc] = 1;
				drm_update_vblank_count(dev, crtc);
			}
		}
		spin_unlock_irqrestore(&dev->vblank_time_lock, irqflags2);
		preempt_enable();
	} else {
		if (!dev->vblank_enabled[crtc]) {
			atomic_dec(&dev->vblank_refcount[crtc]);
			ret = -EINVAL;
		}
	}
	spin_unlock_irqrestore(&dev->vbl_lock, irqflags);

	return ret;
}
EXPORT_SYMBOL(drm_vblank_get);

/**
 * drm_vblank_put - give up ownership of vblank events
 * @dev: DRM device
 * @crtc: which counter to give up
 *
 * Release ownership of a given vblank counter, turning off interrupts
 * if possible. Disable interrupts after drm_vblank_offdelay milliseconds.
 */
void drm_vblank_put(struct drm_device *dev, int crtc)
{
	BUG_ON(atomic_read(&dev->vblank_refcount[crtc]) == 0);

	/* Last user schedules interrupt disable */
	if (atomic_dec_and_test(&dev->vblank_refcount[crtc]) &&
	    (drm_vblank_offdelay > 0))
		mod_timer(&dev->vblank_disable_timer,
			  jiffies + ((drm_vblank_offdelay * DRM_HZ)/1000));
}
EXPORT_SYMBOL(drm_vblank_put);

void drm_vblank_off(struct drm_device *dev, int crtc)
{
	unsigned long irqflags;

	spin_lock_irqsave(&dev->vbl_lock, irqflags);
	vblank_disable_and_save(dev, crtc);
	DRM_WAKEUP(&dev->vbl_queue[crtc]);
	spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
}
EXPORT_SYMBOL(drm_vblank_off);

/**
 * drm_vblank_pre_modeset - account for vblanks across mode sets
 * @dev: DRM device
 * @crtc: CRTC in question
 * @post: post or pre mode set?
 *
 * Account for vblank events across mode setting events, which will likely
 * reset the hardware frame counter.
 */
void drm_vblank_pre_modeset(struct drm_device *dev, int crtc)
{
	/* vblank is not initialized (IRQ not installed ?) */
	if (!dev->num_crtcs)
		return;
	/*
	 * To avoid all the problems that might happen if interrupts
	 * were enabled/disabled around or between these calls, we just
	 * have the kernel take a reference on the CRTC (just once though
	 * to avoid corrupting the count if multiple, mismatch calls occur),
	 * so that interrupts remain enabled in the interim.
	 */
	if (!dev->vblank_inmodeset[crtc]) {
		dev->vblank_inmodeset[crtc] = 0x1;
		if (drm_vblank_get(dev, crtc) == 0)
			dev->vblank_inmodeset[crtc] |= 0x2;
	}
}
EXPORT_SYMBOL(drm_vblank_pre_modeset);

void drm_vblank_post_modeset(struct drm_device *dev, int crtc)
{
	unsigned long irqflags;

	if (dev->vblank_inmodeset[crtc]) {
		spin_lock_irqsave(&dev->vbl_lock, irqflags);
		dev->vblank_disable_allowed = 1;
		spin_unlock_irqrestore(&dev->vbl_lock, irqflags);

		if (dev->vblank_inmodeset[crtc] & 0x2)
			drm_vblank_put(dev, crtc);

		dev->vblank_inmodeset[crtc] = 0;
	}
}
EXPORT_SYMBOL(drm_vblank_post_modeset);

/**
 * drm_modeset_ctl - handle vblank event counter changes across mode switch
 * @DRM_IOCTL_ARGS: standard ioctl arguments
 *
 * Applications should call the %_DRM_PRE_MODESET and %_DRM_POST_MODESET
 * ioctls around modesetting so that any lost vblank events are accounted for.
 *
 * Generally the counter will reset across mode sets.  If interrupts are
 * enabled around this call, we don't have to do anything since the counter
 * will have already been incremented.
 */
int drm_modeset_ctl(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct drm_modeset_ctl *modeset = data;
	int ret = 0;
	unsigned int crtc;

	/* If drm_vblank_init() hasn't been called yet, just no-op */
	if (!dev->num_crtcs)
		goto out;

	crtc = modeset->crtc;
	if (crtc >= dev->num_crtcs) {
		ret = -EINVAL;
		goto out;
	}

	switch (modeset->cmd) {
	case _DRM_PRE_MODESET:
		drm_vblank_pre_modeset(dev, crtc);
		break;
	case _DRM_POST_MODESET:
		drm_vblank_post_modeset(dev, crtc);
		break;
	default:
		ret = -EINVAL;
		break;
	}

out:
	return ret;
}

static int drm_queue_vblank_event(struct drm_device *dev, int pipe,
				  union drm_wait_vblank *vblwait,
				  struct drm_file *file_priv)
{
	struct drm_pending_vblank_event *e;
	struct timeval now;
	unsigned long flags;
	unsigned int seq;
	int ret;

	e = kzalloc(sizeof *e, GFP_KERNEL);
	if (e == NULL) {
		ret = -ENOMEM;
		goto err_put;
	}

	e->pipe = pipe;
	e->base.pid = current->pid;
	e->event.base.type = DRM_EVENT_VBLANK;
	e->event.base.length = sizeof e->event;
	e->event.user_data = vblwait->request.signal;
	e->base.event = &e->event.base;
	e->base.file_priv = file_priv;
	e->base.destroy = (void (*) (struct drm_pending_event *)) kfree;

	spin_lock_irqsave(&dev->event_lock, flags);

	if (file_priv->event_space < sizeof e->event) {
		ret = -EBUSY;
		goto err_unlock;
	}

	file_priv->event_space -= sizeof e->event;
	seq = drm_vblank_count_and_time(dev, pipe, &now);

	if ((vblwait->request.type & _DRM_VBLANK_NEXTONMISS) &&
	    (seq - vblwait->request.sequence) <= (1 << 23)) {
		vblwait->request.sequence = seq + 1;
		vblwait->reply.sequence = vblwait->request.sequence;
	}

	DRM_DEBUG("event on vblank count %d, current %d, crtc %d\n",
		  vblwait->request.sequence, seq, pipe);

	trace_drm_vblank_event_queued(current->pid, pipe,
				      vblwait->request.sequence);

	e->event.sequence = vblwait->request.sequence;
	if ((seq - vblwait->request.sequence) <= (1 << 23)) {
		e->event.sequence = seq;
		e->event.tv_sec = now.tv_sec;
		e->event.tv_usec = now.tv_usec;
		drm_vblank_put(dev, pipe);
		list_add_tail(&e->base.link, &e->base.file_priv->event_list);
		wake_up_interruptible(&e->base.file_priv->event_wait);
		vblwait->reply.sequence = seq;
		trace_drm_vblank_event_delivered(current->pid, pipe,
						 vblwait->request.sequence);
	} else {
		list_add_tail(&e->base.link, &dev->vblank_event_list);
		vblwait->reply.sequence = vblwait->request.sequence;
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);

	return 0;

err_unlock:
	spin_unlock_irqrestore(&dev->event_lock, flags);
	kfree(e);
err_put:
	drm_vblank_put(dev, pipe);
	return ret;
}

/**
 * Wait for VBLANK.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param data user argument, pointing to a drm_wait_vblank structure.
 * \return zero on success or a negative number on failure.
 *
 * This function enables the vblank interrupt on the pipe requested, then
 * sleeps waiting for the requested sequence number to occur, and drops
 * the vblank interrupt refcount afterwards. (vblank irq disable follows that
 * after a timeout with no further vblank waits scheduled).
 */
int drm_wait_vblank(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	union drm_wait_vblank *vblwait = data;
	int ret = 0;
	unsigned int flags, seq, crtc;

	if ((!drm_dev_to_irq(dev)) || (!dev->irq_enabled))
		return -EINVAL;

	if (vblwait->request.type & _DRM_VBLANK_SIGNAL)
		return -EINVAL;

	if (vblwait->request.type &
	    ~(_DRM_VBLANK_TYPES_MASK | _DRM_VBLANK_FLAGS_MASK)) {
		DRM_ERROR("Unsupported type value 0x%x, supported mask 0x%x\n",
			  vblwait->request.type,
			  (_DRM_VBLANK_TYPES_MASK | _DRM_VBLANK_FLAGS_MASK));
		return -EINVAL;
	}

	flags = vblwait->request.type & _DRM_VBLANK_FLAGS_MASK;
	crtc = flags & _DRM_VBLANK_SECONDARY ? 1 : 0;

	if (crtc >= dev->num_crtcs)
		return -EINVAL;

	ret = drm_vblank_get(dev, crtc);
	if (ret) {
		DRM_DEBUG("failed to acquire vblank counter, %d\n", ret);
		return ret;
	}
	seq = drm_vblank_count(dev, crtc);

	switch (vblwait->request.type & _DRM_VBLANK_TYPES_MASK) {
	case _DRM_VBLANK_RELATIVE:
		vblwait->request.sequence += seq;
		vblwait->request.type &= ~_DRM_VBLANK_RELATIVE;
	case _DRM_VBLANK_ABSOLUTE:
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	if (flags & _DRM_VBLANK_EVENT)
		return drm_queue_vblank_event(dev, crtc, vblwait, file_priv);

	if ((flags & _DRM_VBLANK_NEXTONMISS) &&
	    (seq - vblwait->request.sequence) <= (1<<23)) {
		vblwait->request.sequence = seq + 1;
	}

	DRM_DEBUG("waiting on vblank count %d, crtc %d\n",
		  vblwait->request.sequence, crtc);
	dev->last_vblank_wait[crtc] = vblwait->request.sequence;
	DRM_WAIT_ON(ret, dev->vbl_queue[crtc], 3 * DRM_HZ,
		    (((drm_vblank_count(dev, crtc) -
		       vblwait->request.sequence) <= (1 << 23)) ||
		     !dev->irq_enabled));

	if (ret != -EINTR) {
		struct timeval now;

		vblwait->reply.sequence = drm_vblank_count_and_time(dev, crtc, &now);
		vblwait->reply.tval_sec = now.tv_sec;
		vblwait->reply.tval_usec = now.tv_usec;

		DRM_DEBUG("returning %d to client\n",
			  vblwait->reply.sequence);
	} else {
		DRM_DEBUG("vblank wait interrupted by signal\n");
	}

done:
	drm_vblank_put(dev, crtc);
	return ret;
}

void drm_handle_vblank_events(struct drm_device *dev, int crtc)
{
	struct drm_pending_vblank_event *e, *t;
	struct timeval now;
	unsigned long flags;
	unsigned int seq;

	seq = drm_vblank_count_and_time(dev, crtc, &now);

	spin_lock_irqsave(&dev->event_lock, flags);

	list_for_each_entry_safe(e, t, &dev->vblank_event_list, base.link) {
		if (e->pipe != crtc)
			continue;
		if ((seq - e->event.sequence) > (1<<23))
			continue;

		DRM_DEBUG("vblank event on %d, current %d\n",
			  e->event.sequence, seq);

		e->event.sequence = seq;
		e->event.tv_sec = now.tv_sec;
		e->event.tv_usec = now.tv_usec;
		drm_vblank_put(dev, e->pipe);
		list_move_tail(&e->base.link, &e->base.file_priv->event_list);
		wake_up_interruptible(&e->base.file_priv->event_wait);
		trace_drm_vblank_event_delivered(e->base.pid, e->pipe,
						 e->event.sequence);
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);

	trace_drm_vblank_event(crtc, seq);
}

/**
 * drm_handle_vblank - handle a vblank event
 * @dev: DRM device
 * @crtc: where this event occurred
 *
 * Drivers should call this routine in their vblank interrupt handlers to
 * update the vblank counter and send any signals that may be pending.
 */
bool drm_handle_vblank(struct drm_device *dev, int crtc)
{
	u32 vblcount;
	s64 diff_ns;
	struct timeval tvblank;
	unsigned long irqflags;

	if (!dev->num_crtcs)
		return false;

	/* Need timestamp lock to prevent concurrent execution with
	 * vblank enable/disable, as this would cause inconsistent
	 * or corrupted timestamps and vblank counts.
	 */
	spin_lock_irqsave(&dev->vblank_time_lock, irqflags);

	/* Vblank irq handling disabled. Nothing to do. */
	if (!dev->vblank_enabled[crtc]) {
		spin_unlock_irqrestore(&dev->vblank_time_lock, irqflags);
		return false;
	}

	/* Fetch corresponding timestamp for this vblank interval from
	 * driver and store it in proper slot of timestamp ringbuffer.
	 */

	/* Get current timestamp and count. */
	vblcount = atomic_read(&dev->_vblank_count[crtc]);
	drm_get_last_vbltimestamp(dev, crtc, &tvblank, DRM_CALLED_FROM_VBLIRQ);

	/* Compute time difference to timestamp of last vblank */
	diff_ns = timeval_to_ns(&tvblank) -
		  timeval_to_ns(&vblanktimestamp(dev, crtc, vblcount));

	/* Update vblank timestamp and count if at least
	 * DRM_REDUNDANT_VBLIRQ_THRESH_NS nanoseconds
	 * difference between last stored timestamp and current
	 * timestamp. A smaller difference means basically
	 * identical timestamps. Happens if this vblank has
	 * been already processed and this is a redundant call,
	 * e.g., due to spurious vblank interrupts. We need to
	 * ignore those for accounting.
	 */
	if (abs64(diff_ns) > DRM_REDUNDANT_VBLIRQ_THRESH_NS) {
		/* Store new timestamp in ringbuffer. */
		vblanktimestamp(dev, crtc, vblcount + 1) = tvblank;

		/* Increment cooked vblank count. This also atomically commits
		 * the timestamp computed above.
		 */
		smp_mb__before_atomic_inc();
		atomic_inc(&dev->_vblank_count[crtc]);
		smp_mb__after_atomic_inc();
	} else {
		DRM_DEBUG("crtc %d: Redundant vblirq ignored. diff_ns = %d\n",
			  crtc, (int) diff_ns);
	}

	DRM_WAKEUP(&dev->vbl_queue[crtc]);
	drm_handle_vblank_events(dev, crtc);

	spin_unlock_irqrestore(&dev->vblank_time_lock, irqflags);
	return true;
}
EXPORT_SYMBOL(drm_handle_vblank);
