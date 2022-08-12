/*
 * @File        pvr_sync_ioctl_common.c
 * @Title       Kernel driver for Android's sync mechanism
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/slab.h>

#include "pvr_drm.h"
#include "pvr_sync_api.h"
#include "pvr_sync_ioctl_common.h"

/*
 * The PVR Sync API is unusual in that some operations configure the
 * timeline for use, and are no longer allowed once the timeline is
 * in use. A locking mechanism, such as a read/write semaphore, would
 * be one method of helping to ensure the API rules are followed, but
 * this would add unnecessary overhead once the timeline has been
 * configured, as read locks would continue to have to be taken after
 * the timeline is in use. To avoid locks, two atomic variables are used,
 * together with memory barriers. The in_setup variable indicates a "rename"
 * or "force software only" ioctl is in progress. At most one of these two
 * configuration ioctls can be in progress at any one time, and they can't
 * overlap with any other Sync ioctl. The in_use variable indicates one
 * of the other Sync ioctls has started. Once set, in_use stays set, and
 * prevents any further configuration ioctls. Non-configuration ioctls
 * are allowed to overlap.
 * It is possible for a configuration and non-configuration ioctl to race,
 * but at most one will be allowed to proceed, and perhaps neither.
 * Given the intended usage of the API in user space, where the timeline
 * is fully configured before being used, the race behaviour won't be
 * an issue.
 */

struct pvr_sync_file_data {
	atomic_t in_setup;
	atomic_t in_use;
	void *api_private;
	bool is_sw;
};

static bool pvr_sync_set_in_use(struct pvr_sync_file_data *fdata)
{
	if (atomic_read(&fdata->in_use) < 2) {
		atomic_set(&fdata->in_use, 1);
		/* Ensure in_use change is visible before in_setup is read */
		smp_mb();
		if (atomic_read(&fdata->in_setup) != 0)
			return false;

		atomic_set(&fdata->in_use, 2);
	} else {
		/* Ensure stale private data isn't read */
		smp_rmb();
	}

	return true;
}

static bool pvr_sync_set_in_setup(struct pvr_sync_file_data *fdata)
{
	int in_setup;

	in_setup = atomic_inc_return(&fdata->in_setup);
	if (in_setup > 1 || atomic_read(&fdata->in_use) != 0) {
		atomic_dec(&fdata->in_setup);
		return false;
	}

	return true;
}

static inline void pvr_sync_reset_in_setup(struct pvr_sync_file_data *fdata)
{
	/*
	 * Ensure setup changes are visible before allowing other
	 * operations to proceed.
	 */
	smp_mb__before_atomic();
	atomic_dec(&fdata->in_setup);
}

void *pvr_sync_get_api_priv_common(struct file *file)
{
	if (file != NULL && pvr_sync_is_timeline(file)) {
		struct pvr_sync_file_data *fdata = pvr_sync_get_private_data(file);

		if (fdata != NULL && pvr_sync_set_in_use(fdata))
			return fdata->api_private;
	}

	return NULL;
}

int pvr_sync_open_common(void *connection_data, void *file_handle)
{
	void *data = NULL;
	struct pvr_sync_file_data *fdata;
	int err;

	fdata = kzalloc(sizeof(*fdata), GFP_KERNEL);
	if (!fdata)
		return -ENOMEM;

	atomic_set(&fdata->in_setup, 0);
	atomic_set(&fdata->in_use, 0);

	if (!pvr_sync_set_private_data(connection_data, fdata)) {
		kfree(fdata);
		return -EINVAL;
	}

	err = pvr_sync_api_init(file_handle, &data);
	if (err)
		kfree(fdata);
	else
		fdata->api_private = data;

	return err;
}

int pvr_sync_close_common(void *connection_data)
{
	struct pvr_sync_file_data *fdata;

	fdata = pvr_sync_connection_private_data(connection_data);
	if (fdata) {
		int err;

		err = pvr_sync_api_deinit(fdata->api_private, fdata->is_sw);

		kfree(fdata);

		return err;
	}

	return 0;
}

static inline int pvr_sync_ioctl_rename(void *api_priv, void *arg)
{
	struct pvr_sync_rename_ioctl_data *data = arg;

	return pvr_sync_api_rename(api_priv, data);
}

static inline int pvr_sync_ioctl_force_sw_only(struct pvr_sync_file_data *fdata)
{
	void *data = fdata->api_private;
	int err;

	err = pvr_sync_api_force_sw_only(fdata->api_private, &data);
	if (!err) {
		if (data != fdata->api_private)
			fdata->api_private = data;

		fdata->is_sw = true;
	}

	return err;
}

static inline int pvr_sync_ioctl_sw_create_fence(void *api_priv, void *arg)
{
	struct pvr_sw_sync_create_fence_data *data = arg;

	return pvr_sync_api_sw_create_fence(api_priv, data);
}

static inline int pvr_sync_ioctl_sw_inc(void *api_priv, void *arg)
{
	struct pvr_sw_timeline_advance_data *data = arg;

	return pvr_sync_api_sw_inc(api_priv, data);
}

int pvr_sync_ioctl_common(struct file *file, unsigned int cmd, void *arg)
{
	int err = -ENOTTY;
	struct pvr_sync_file_data *fdata;
	bool in_setup;

	fdata = pvr_sync_get_private_data(file);
	if (!fdata)
		return -EINVAL;

	switch (cmd) {
	case DRM_PVR_SYNC_RENAME_CMD:
	case DRM_PVR_SYNC_FORCE_SW_ONLY_CMD:
		if (!pvr_sync_set_in_setup(fdata))
			return -EBUSY;

		in_setup = true;
		break;
	default:
		if (!pvr_sync_set_in_use(fdata))
			return -EBUSY;

		in_setup = false;
		break;
	}

	if (in_setup) {
		if (fdata->is_sw)
			err = -ENOTTY;
		else
			switch (cmd) {
			case DRM_PVR_SYNC_RENAME_CMD:
				err = pvr_sync_ioctl_rename(fdata->api_private,
							    arg);
				break;
			case DRM_PVR_SYNC_FORCE_SW_ONLY_CMD:
				err = pvr_sync_ioctl_force_sw_only(fdata);
				break;
			default:
				break;
			}
	} else {
		if (!fdata->is_sw)
			err = -ENOTTY;
		else
			switch (cmd) {
			case DRM_PVR_SW_SYNC_CREATE_FENCE_CMD:
				err = pvr_sync_ioctl_sw_create_fence(fdata->api_private,
								     arg);
				break;
			case DRM_PVR_SW_SYNC_INC_CMD:
				err = pvr_sync_ioctl_sw_inc(fdata->api_private,
							    arg);
				break;
			default:
				break;
			}
	}

	if (in_setup)
		pvr_sync_reset_in_setup(fdata);

	return err;
}
