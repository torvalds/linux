/*
 * @File        pvr_sync_ioctl_drm.c
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

#include "pvr_drv.h"
#include "pvr_drm.h"
#include "private_data.h"
#include "env_connection.h"
#include "pvr_sync_api.h"
#include "pvr_sync_ioctl_common.h"
#include "pvr_sync_ioctl_drm.h"

bool pvr_sync_set_private_data(void *connection_data,
			       struct pvr_sync_file_data *fdata)
{
	if (connection_data) {
		ENV_CONNECTION_DATA *env_data;

		env_data = PVRSRVConnectionPrivateData(connection_data);
		if (env_data) {
			env_data->pvPvrSyncPrivateData = fdata;

			return true;
		}
	}

	return false;
}

struct pvr_sync_file_data *
pvr_sync_connection_private_data(void *connection_data)
{
	if (connection_data) {
		ENV_CONNECTION_DATA *env_data;

		env_data = PVRSRVConnectionPrivateData(connection_data);

		if (env_data)
			return env_data->pvPvrSyncPrivateData;
	}

	return NULL;
}

struct pvr_sync_file_data *
pvr_sync_get_private_data(struct file *file)
{
	CONNECTION_DATA *connection_data = LinuxSyncConnectionFromFile(file);

	return pvr_sync_connection_private_data(connection_data);
}

bool pvr_sync_is_timeline(struct file *file)
{
	return file->f_op == &pvr_drm_fops;
}

void *pvr_sync_get_api_priv(struct file *file)
{
	return pvr_sync_get_api_priv_common(file);
}

struct file *pvr_sync_get_file_struct(void *file_handle)
{
	if (file_handle) {
		struct drm_file *file = file_handle;

		return file->filp;
	}

	return NULL;
}

int pvr_sync_open(void *connection_data, struct drm_file *file)
{
	/*
	 * The file structure pointer (file->filp) may not have been
	 * initialised at this point, so pass down a pointer to the
	 * drm_file structure instead.
	 */
	return pvr_sync_open_common(connection_data, file);
}

void pvr_sync_close(void *connection_data)
{
	int iErr = pvr_sync_close_common(connection_data);

	if (iErr < 0)
		pr_err("%s: ERROR (%d) returned by pvr_sync_close_common()\n",
		       __func__, iErr);
}


int pvr_sync_rename_ioctl(struct drm_device __maybe_unused *dev,
			  void *arg, struct drm_file *file)
{
	return pvr_sync_ioctl_common(file->filp,
				     DRM_PVR_SYNC_RENAME_CMD, arg);
}

int pvr_sync_force_sw_only_ioctl(struct drm_device __maybe_unused *dev,
				 void *arg, struct drm_file *file)
{
	return pvr_sync_ioctl_common(file->filp,
				     DRM_PVR_SYNC_FORCE_SW_ONLY_CMD, arg);
}

int pvr_sw_sync_create_fence_ioctl(struct drm_device __maybe_unused *dev,
				   void *arg, struct drm_file *file)
{
	return pvr_sync_ioctl_common(file->filp,
				     DRM_PVR_SW_SYNC_CREATE_FENCE_CMD, arg);
}

int pvr_sw_sync_inc_ioctl(struct drm_device __maybe_unused *dev,
			  void *arg, struct drm_file *file)
{
	return pvr_sync_ioctl_common(file->filp,
				     DRM_PVR_SW_SYNC_INC_CMD, arg);
}

int pvr_sync_ioctl_init(void)
{
	return 0;
}

void pvr_sync_ioctl_deinit(void)
{
}
