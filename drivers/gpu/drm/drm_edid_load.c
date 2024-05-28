// SPDX-License-Identifier: GPL-2.0-or-later
/*
   drm_edid_load.c: use a built-in EDID data set or load it via the firmware
		    interface

   Copyright (C) 2012 Carsten Emde <C.Emde@osadl.org>

*/

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>

#include "drm_crtc_internal.h"

static char edid_firmware[PATH_MAX];
module_param_string(edid_firmware, edid_firmware, sizeof(edid_firmware), 0644);
MODULE_PARM_DESC(edid_firmware,
		 "Do not probe monitor, use specified EDID blob from /lib/firmware instead.");

static const struct drm_edid *edid_load(struct drm_connector *connector, const char *name)
{
	const struct firmware *fw = NULL;
	const struct drm_edid *drm_edid;
	int err;

	err = request_firmware(&fw, name, connector->dev->dev);
	if (err) {
		drm_err(connector->dev,
			"[CONNECTOR:%d:%s] Requesting EDID firmware \"%s\" failed (err=%d)\n",
			connector->base.id, connector->name,
			name, err);
		return ERR_PTR(err);
	}

	drm_dbg_kms(connector->dev, "[CONNECTOR:%d:%s] Loaded external firmware EDID \"%s\"\n",
		    connector->base.id, connector->name, name);

	drm_edid = drm_edid_alloc(fw->data, fw->size);
	if (!drm_edid_valid(drm_edid)) {
		drm_err(connector->dev, "Invalid firmware EDID \"%s\"\n", name);
		drm_edid_free(drm_edid);
		drm_edid = ERR_PTR(-EINVAL);
	}

	release_firmware(fw);

	return drm_edid;
}

const struct drm_edid *drm_edid_load_firmware(struct drm_connector *connector)
{
	char *edidname, *last, *colon, *fwstr, *edidstr, *fallback = NULL;
	const struct drm_edid *drm_edid;

	if (edid_firmware[0] == '\0')
		return ERR_PTR(-ENOENT);

	/*
	 * If there are multiple edid files specified and separated
	 * by commas, search through the list looking for one that
	 * matches the connector.
	 *
	 * If there's one or more that doesn't specify a connector, keep
	 * the last one found one as a fallback.
	 */
	fwstr = kstrdup(edid_firmware, GFP_KERNEL);
	if (!fwstr)
		return ERR_PTR(-ENOMEM);
	edidstr = fwstr;

	while ((edidname = strsep(&edidstr, ","))) {
		colon = strchr(edidname, ':');
		if (colon != NULL) {
			if (strncmp(connector->name, edidname, colon - edidname))
				continue;
			edidname = colon + 1;
			break;
		}

		if (*edidname != '\0') /* corner case: multiple ',' */
			fallback = edidname;
	}

	if (!edidname) {
		if (!fallback) {
			kfree(fwstr);
			return ERR_PTR(-ENOENT);
		}
		edidname = fallback;
	}

	last = edidname + strlen(edidname) - 1;
	if (*last == '\n')
		*last = '\0';

	drm_edid = edid_load(connector, edidname);

	kfree(fwstr);

	return drm_edid;
}
