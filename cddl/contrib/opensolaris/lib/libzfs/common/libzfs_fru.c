/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <dlfcn.h>
#include <errno.h>
#include <libintl.h>
#include <link.h>
#include <pthread.h>
#include <strings.h>
#include <unistd.h>

#include <libzfs.h>

#include <fm/libtopo.h>
#include <sys/fm/protocol.h>
#include <sys/systeminfo.h>

#include "libzfs_impl.h"

/*
 * This file is responsible for determining the relationship between I/O
 * devices paths and physical locations.  In the world of MPxIO and external
 * enclosures, the device path is not synonymous with the physical location.
 * If you remove a drive and insert it into a different slot, it will end up
 * with the same path under MPxIO.  If you recable storage enclosures, the
 * device paths may change.  All of this makes it difficult to implement the
 * 'autoreplace' property, which is supposed to automatically manage disk
 * replacement based on physical slot.
 *
 * In order to work around these limitations, we have a per-vdev FRU property
 * that is the libtopo path (minus disk-specific authority information) to the
 * physical location of the device on the system.  This is an optional
 * property, and is only needed when using the 'autoreplace' property or when
 * generating FMA faults against vdevs.
 */

/*
 * Because the FMA packages depend on ZFS, we have to dlopen() libtopo in case
 * it is not present.  We only need this once per library instance, so it is
 * not part of the libzfs handle.
 */
static void *_topo_dlhandle;
static topo_hdl_t *(*_topo_open)(int, const char *, int *);
static void (*_topo_close)(topo_hdl_t *);
static char *(*_topo_snap_hold)(topo_hdl_t *, const char *, int *);
static void (*_topo_snap_release)(topo_hdl_t *);
static topo_walk_t *(*_topo_walk_init)(topo_hdl_t *, const char *,
    topo_walk_cb_t, void *, int *);
static int (*_topo_walk_step)(topo_walk_t *, int);
static void (*_topo_walk_fini)(topo_walk_t *);
static void (*_topo_hdl_strfree)(topo_hdl_t *, char *);
static char *(*_topo_node_name)(tnode_t *);
static int (*_topo_prop_get_string)(tnode_t *, const char *, const char *,
    char **, int *);
static int (*_topo_node_fru)(tnode_t *, nvlist_t **, nvlist_t *, int *);
static int (*_topo_fmri_nvl2str)(topo_hdl_t *, nvlist_t *, char **, int *);
static int (*_topo_fmri_strcmp_noauth)(topo_hdl_t *, const char *,
    const char *);

#define	ZFS_FRU_HASH_SIZE	257

static size_t
fru_strhash(const char *key)
{
	ulong_t g, h = 0;
	const char *p;

	for (p = key; *p != '\0'; p++) {
		h = (h << 4) + *p;

		if ((g = (h & 0xf0000000)) != 0) {
			h ^= (g >> 24);
			h ^= g;
		}
	}

	return (h % ZFS_FRU_HASH_SIZE);
}

static int
libzfs_fru_gather(topo_hdl_t *thp, tnode_t *tn, void *arg)
{
	libzfs_handle_t *hdl = arg;
	nvlist_t *fru;
	char *devpath, *frustr;
	int err;
	libzfs_fru_t *frup;
	size_t idx;

	/*
	 * If this is the chassis node, and we don't yet have the system
	 * chassis ID, then fill in this value now.
	 */
	if (hdl->libzfs_chassis_id[0] == '\0' &&
	    strcmp(_topo_node_name(tn), "chassis") == 0) {
		if (_topo_prop_get_string(tn, FM_FMRI_AUTHORITY,
		    FM_FMRI_AUTH_CHASSIS, &devpath, &err) == 0)
			(void) strlcpy(hdl->libzfs_chassis_id, devpath,
			    sizeof (hdl->libzfs_chassis_id));
	}

	/*
	 * Skip non-disk nodes.
	 */
	if (strcmp(_topo_node_name(tn), "disk") != 0)
		return (TOPO_WALK_NEXT);

	/*
	 * Get the devfs path and FRU.
	 */
	if (_topo_prop_get_string(tn, "io", "devfs-path", &devpath, &err) != 0)
		return (TOPO_WALK_NEXT);

	if (libzfs_fru_lookup(hdl, devpath) != NULL) {
		_topo_hdl_strfree(thp, devpath);
		return (TOPO_WALK_NEXT);
	}

	if (_topo_node_fru(tn, &fru, NULL, &err) != 0) {
		_topo_hdl_strfree(thp, devpath);
		return (TOPO_WALK_NEXT);
	}

	/*
	 * Convert the FRU into a string.
	 */
	if (_topo_fmri_nvl2str(thp, fru, &frustr, &err) != 0) {
		nvlist_free(fru);
		_topo_hdl_strfree(thp, devpath);
		return (TOPO_WALK_NEXT);
	}

	nvlist_free(fru);

	/*
	 * Finally, we have a FRU string and device path.  Add it to the hash.
	 */
	if ((frup = calloc(sizeof (libzfs_fru_t), 1)) == NULL) {
		_topo_hdl_strfree(thp, devpath);
		_topo_hdl_strfree(thp, frustr);
		return (TOPO_WALK_NEXT);
	}

	if ((frup->zf_device = strdup(devpath)) == NULL ||
	    (frup->zf_fru = strdup(frustr)) == NULL) {
		free(frup->zf_device);
		free(frup);
		_topo_hdl_strfree(thp, devpath);
		_topo_hdl_strfree(thp, frustr);
		return (TOPO_WALK_NEXT);
	}

	_topo_hdl_strfree(thp, devpath);
	_topo_hdl_strfree(thp, frustr);

	idx = fru_strhash(frup->zf_device);
	frup->zf_chain = hdl->libzfs_fru_hash[idx];
	hdl->libzfs_fru_hash[idx] = frup;
	frup->zf_next = hdl->libzfs_fru_list;
	hdl->libzfs_fru_list = frup;

	return (TOPO_WALK_NEXT);
}

/*
 * Called during initialization to setup the dynamic libtopo connection.
 */
#pragma init(libzfs_init_fru)
static void
libzfs_init_fru(void)
{
	char path[MAXPATHLEN];
	char isa[257];

#if defined(_LP64)
	if (sysinfo(SI_ARCHITECTURE_64, isa, sizeof (isa)) < 0)
		isa[0] = '\0';
#else
	isa[0] = '\0';
#endif
	(void) snprintf(path, sizeof (path),
	    "/usr/lib/fm/%s/libtopo.so", isa);

	if ((_topo_dlhandle = dlopen(path, RTLD_LAZY)) == NULL)
		return;

	_topo_open = (topo_hdl_t *(*)())
	    dlsym(_topo_dlhandle, "topo_open");
	_topo_close = (void (*)())
	    dlsym(_topo_dlhandle, "topo_close");
	_topo_snap_hold = (char *(*)())
	    dlsym(_topo_dlhandle, "topo_snap_hold");
	_topo_snap_release = (void (*)())
	    dlsym(_topo_dlhandle, "topo_snap_release");
	_topo_walk_init = (topo_walk_t *(*)())
	    dlsym(_topo_dlhandle, "topo_walk_init");
	_topo_walk_step = (int (*)())
	    dlsym(_topo_dlhandle, "topo_walk_step");
	_topo_walk_fini = (void (*)())
	    dlsym(_topo_dlhandle, "topo_walk_fini");
	_topo_hdl_strfree = (void (*)())
	    dlsym(_topo_dlhandle, "topo_hdl_strfree");
	_topo_node_name = (char *(*)())
	    dlsym(_topo_dlhandle, "topo_node_name");
	_topo_prop_get_string = (int (*)())
	    dlsym(_topo_dlhandle, "topo_prop_get_string");
	_topo_node_fru = (int (*)())
	    dlsym(_topo_dlhandle, "topo_node_fru");
	_topo_fmri_nvl2str = (int (*)())
	    dlsym(_topo_dlhandle, "topo_fmri_nvl2str");
	_topo_fmri_strcmp_noauth = (int (*)())
	    dlsym(_topo_dlhandle, "topo_fmri_strcmp_noauth");

	if (_topo_open == NULL || _topo_close == NULL ||
	    _topo_snap_hold == NULL || _topo_snap_release == NULL ||
	    _topo_walk_init == NULL || _topo_walk_step == NULL ||
	    _topo_walk_fini == NULL || _topo_hdl_strfree == NULL ||
	    _topo_node_name == NULL || _topo_prop_get_string == NULL ||
	    _topo_node_fru == NULL || _topo_fmri_nvl2str == NULL ||
	    _topo_fmri_strcmp_noauth == NULL) {
		(void) dlclose(_topo_dlhandle);
		_topo_dlhandle = NULL;
	}
}

/*
 * Refresh the mappings from device path -> FMRI.  We do this by walking the
 * hc topology looking for disk nodes, and recording the io/devfs-path and FRU.
 * Note that we strip out the disk-specific authority information (serial,
 * part, revision, etc) so that we are left with only the identifying
 * characteristics of the slot (hc path and chassis-id).
 */
void
libzfs_fru_refresh(libzfs_handle_t *hdl)
{
	int err;
	char *uuid;
	topo_hdl_t *thp;
	topo_walk_t *twp;

	if (_topo_dlhandle == NULL)
		return;

	/*
	 * Clear the FRU hash and initialize our basic structures.
	 */
	libzfs_fru_clear(hdl, B_FALSE);

	if ((hdl->libzfs_topo_hdl = _topo_open(TOPO_VERSION,
	    NULL, &err)) == NULL)
		return;

	thp = hdl->libzfs_topo_hdl;

	if ((uuid = _topo_snap_hold(thp, NULL, &err)) == NULL)
		return;

	_topo_hdl_strfree(thp, uuid);

	if (hdl->libzfs_fru_hash == NULL &&
	    (hdl->libzfs_fru_hash =
	    calloc(ZFS_FRU_HASH_SIZE, sizeof (void *))) == NULL)
		return;

	/*
	 * We now have a topo snapshot, so iterate over the hc topology looking
	 * for disks to add to the hash.
	 */
	twp = _topo_walk_init(thp, FM_FMRI_SCHEME_HC,
	    libzfs_fru_gather, hdl, &err);
	if (twp != NULL) {
		(void) _topo_walk_step(twp, TOPO_WALK_CHILD);
		_topo_walk_fini(twp);
	}
}

/*
 * Given a devfs path, return the FRU for the device, if known.  This will
 * automatically call libzfs_fru_refresh() if it hasn't already been called by
 * the consumer.  The string returned is valid until the next call to
 * libzfs_fru_refresh().
 */
const char *
libzfs_fru_lookup(libzfs_handle_t *hdl, const char *devpath)
{
	size_t idx = fru_strhash(devpath);
	libzfs_fru_t *frup;

	if (hdl->libzfs_fru_hash == NULL)
		libzfs_fru_refresh(hdl);

	if (hdl->libzfs_fru_hash == NULL)
		return (NULL);

	for (frup = hdl->libzfs_fru_hash[idx]; frup != NULL;
	    frup = frup->zf_chain) {
		if (strcmp(devpath, frup->zf_device) == 0)
			return (frup->zf_fru);
	}

	return (NULL);
}

/*
 * Given a fru path, return the device path.  This will automatically call
 * libzfs_fru_refresh() if it hasn't already been called by the consumer.  The
 * string returned is valid until the next call to libzfs_fru_refresh().
 */
const char *
libzfs_fru_devpath(libzfs_handle_t *hdl, const char *fru)
{
	libzfs_fru_t *frup;
	size_t idx;

	if (hdl->libzfs_fru_hash == NULL)
		libzfs_fru_refresh(hdl);

	if (hdl->libzfs_fru_hash == NULL)
		return (NULL);

	for (idx = 0; idx < ZFS_FRU_HASH_SIZE; idx++) {
		for (frup = hdl->libzfs_fru_hash[idx]; frup != NULL;
		    frup = frup->zf_next) {
			if (_topo_fmri_strcmp_noauth(hdl->libzfs_topo_hdl,
			    fru, frup->zf_fru))
				return (frup->zf_device);
		}
	}

	return (NULL);
}

/*
 * Change the stored FRU for the given vdev.
 */
int
zpool_fru_set(zpool_handle_t *zhp, uint64_t vdev_guid, const char *fru)
{
	zfs_cmd_t zc = { 0 };

	(void) strncpy(zc.zc_name, zhp->zpool_name, sizeof (zc.zc_name));
	(void) strncpy(zc.zc_value, fru, sizeof (zc.zc_value));
	zc.zc_guid = vdev_guid;

	if (zfs_ioctl(zhp->zpool_hdl, ZFS_IOC_VDEV_SETFRU, &zc) != 0)
		return (zpool_standard_error_fmt(zhp->zpool_hdl, errno,
		    dgettext(TEXT_DOMAIN, "cannot set FRU")));

	return (0);
}

/*
 * Compare to two FRUs, ignoring any authority information.
 */
boolean_t
libzfs_fru_compare(libzfs_handle_t *hdl, const char *a, const char *b)
{
	if (hdl->libzfs_fru_hash == NULL)
		libzfs_fru_refresh(hdl);

	if (hdl->libzfs_fru_hash == NULL)
		return (strcmp(a, b) == 0);

	return (_topo_fmri_strcmp_noauth(hdl->libzfs_topo_hdl, a, b));
}

/*
 * This special function checks to see whether the FRU indicates it's supposed
 * to be in the system chassis, but the chassis-id doesn't match.  This can
 * happen in a clustered case, where both head nodes have the same logical
 * disk, but opening the device on the other head node is meaningless.
 */
boolean_t
libzfs_fru_notself(libzfs_handle_t *hdl, const char *fru)
{
	const char *chassisid;
	size_t len;

	if (hdl->libzfs_fru_hash == NULL)
		libzfs_fru_refresh(hdl);

	if (hdl->libzfs_chassis_id[0] == '\0')
		return (B_FALSE);

	if (strstr(fru, "/chassis=0/") == NULL)
		return (B_FALSE);

	if ((chassisid = strstr(fru, ":chassis-id=")) == NULL)
		return (B_FALSE);

	chassisid += 12;
	len = strlen(hdl->libzfs_chassis_id);
	if (strncmp(chassisid, hdl->libzfs_chassis_id, len) == 0 &&
	    (chassisid[len] == '/' || chassisid[len] == ':'))
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Clear memory associated with the FRU hash.
 */
void
libzfs_fru_clear(libzfs_handle_t *hdl, boolean_t final)
{
	libzfs_fru_t *frup;

	while ((frup = hdl->libzfs_fru_list) != NULL) {
		hdl->libzfs_fru_list = frup->zf_next;
		free(frup->zf_device);
		free(frup->zf_fru);
		free(frup);
	}

	hdl->libzfs_fru_list = NULL;

	if (hdl->libzfs_topo_hdl != NULL) {
		_topo_snap_release(hdl->libzfs_topo_hdl);
		_topo_close(hdl->libzfs_topo_hdl);
		hdl->libzfs_topo_hdl = NULL;
	}

	if (final) {
		free(hdl->libzfs_fru_hash);
	} else if (hdl->libzfs_fru_hash != NULL) {
		bzero(hdl->libzfs_fru_hash,
		    ZFS_FRU_HASH_SIZE * sizeof (void *));
	}
}
