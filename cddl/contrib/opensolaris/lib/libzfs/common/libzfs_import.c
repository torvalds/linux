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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright 2015 RackTop Systems.
 * Copyright 2016 Nexenta Systems, Inc.
 */

/*
 * Pool import support functions.
 *
 * To import a pool, we rely on reading the configuration information from the
 * ZFS label of each device.  If we successfully read the label, then we
 * organize the configuration information in the following hierarchy:
 *
 *	pool guid -> toplevel vdev guid -> label txg
 *
 * Duplicate entries matching this same tuple will be discarded.  Once we have
 * examined every device, we pick the best label txg config for each toplevel
 * vdev.  We then arrange these toplevel vdevs into a complete pool config, and
 * update any paths that have changed.  Finally, we attempt to import the pool
 * using our derived config, and record the results.
 */

#include <aio.h>
#include <ctype.h>
#include <devid.h>
#include <dirent.h>
#include <errno.h>
#include <libintl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread_pool.h>
#include <libgeom.h>

#include <sys/vdev_impl.h>

#include "libzfs.h"
#include "libzfs_impl.h"

/*
 * Intermediate structures used to gather configuration information.
 */
typedef struct config_entry {
	uint64_t		ce_txg;
	nvlist_t		*ce_config;
	struct config_entry	*ce_next;
} config_entry_t;

typedef struct vdev_entry {
	uint64_t		ve_guid;
	config_entry_t		*ve_configs;
	struct vdev_entry	*ve_next;
} vdev_entry_t;

typedef struct pool_entry {
	uint64_t		pe_guid;
	vdev_entry_t		*pe_vdevs;
	struct pool_entry	*pe_next;
} pool_entry_t;

typedef struct name_entry {
	char			*ne_name;
	uint64_t		ne_guid;
	struct name_entry	*ne_next;
} name_entry_t;

typedef struct pool_list {
	pool_entry_t		*pools;
	name_entry_t		*names;
} pool_list_t;

static char *
get_devid(const char *path)
{
#ifdef have_devid
	int fd;
	ddi_devid_t devid;
	char *minor, *ret;

	if ((fd = open(path, O_RDONLY)) < 0)
		return (NULL);

	minor = NULL;
	ret = NULL;
	if (devid_get(fd, &devid) == 0) {
		if (devid_get_minor_name(fd, &minor) == 0)
			ret = devid_str_encode(devid, minor);
		if (minor != NULL)
			devid_str_free(minor);
		devid_free(devid);
	}
	(void) close(fd);

	return (ret);
#else
	return (NULL);
#endif
}


/*
 * Go through and fix up any path and/or devid information for the given vdev
 * configuration.
 */
static int
fix_paths(nvlist_t *nv, name_entry_t *names)
{
	nvlist_t **child;
	uint_t c, children;
	uint64_t guid;
	name_entry_t *ne, *best;
	char *path, *devid;
	int matched;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if (fix_paths(child[c], names) != 0)
				return (-1);
		return (0);
	}

	/*
	 * This is a leaf (file or disk) vdev.  In either case, go through
	 * the name list and see if we find a matching guid.  If so, replace
	 * the path and see if we can calculate a new devid.
	 *
	 * There may be multiple names associated with a particular guid, in
	 * which case we have overlapping slices or multiple paths to the same
	 * disk.  If this is the case, then we want to pick the path that is
	 * the most similar to the original, where "most similar" is the number
	 * of matching characters starting from the end of the path.  This will
	 * preserve slice numbers even if the disks have been reorganized, and
	 * will also catch preferred disk names if multiple paths exist.
	 */
	verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) == 0);
	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) != 0)
		path = NULL;

	matched = 0;
	best = NULL;
	for (ne = names; ne != NULL; ne = ne->ne_next) {
		if (ne->ne_guid == guid) {
			const char *src, *dst;
			int count;

			if (path == NULL) {
				best = ne;
				break;
			}

			src = ne->ne_name + strlen(ne->ne_name) - 1;
			dst = path + strlen(path) - 1;
			for (count = 0; src >= ne->ne_name && dst >= path;
			    src--, dst--, count++)
				if (*src != *dst)
					break;

			/*
			 * At this point, 'count' is the number of characters
			 * matched from the end.
			 */
			if (count > matched || best == NULL) {
				best = ne;
				matched = count;
			}
		}
	}

	if (best == NULL)
		return (0);

	if (nvlist_add_string(nv, ZPOOL_CONFIG_PATH, best->ne_name) != 0)
		return (-1);

	if ((devid = get_devid(best->ne_name)) == NULL) {
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_DEVID);
	} else {
		if (nvlist_add_string(nv, ZPOOL_CONFIG_DEVID, devid) != 0) {
			devid_str_free(devid);
			return (-1);
		}
		devid_str_free(devid);
	}

	return (0);
}

/*
 * Add the given configuration to the list of known devices.
 */
static int
add_config(libzfs_handle_t *hdl, pool_list_t *pl, const char *path,
    nvlist_t *config)
{
	uint64_t pool_guid, vdev_guid, top_guid, txg, state;
	pool_entry_t *pe;
	vdev_entry_t *ve;
	config_entry_t *ce;
	name_entry_t *ne;

	/*
	 * If this is a hot spare not currently in use or level 2 cache
	 * device, add it to the list of names to translate, but don't do
	 * anything else.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &state) == 0 &&
	    (state == POOL_STATE_SPARE || state == POOL_STATE_L2CACHE) &&
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &vdev_guid) == 0) {
		if ((ne = zfs_alloc(hdl, sizeof (name_entry_t))) == NULL)
			return (-1);

		if ((ne->ne_name = zfs_strdup(hdl, path)) == NULL) {
			free(ne);
			return (-1);
		}

		ne->ne_guid = vdev_guid;
		ne->ne_next = pl->names;
		pl->names = ne;

		return (0);
	}

	/*
	 * If we have a valid config but cannot read any of these fields, then
	 * it means we have a half-initialized label.  In vdev_label_init()
	 * we write a label with txg == 0 so that we can identify the device
	 * in case the user refers to the same disk later on.  If we fail to
	 * create the pool, we'll be left with a label in this state
	 * which should not be considered part of a valid pool.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &pool_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID,
	    &vdev_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_TOP_GUID,
	    &top_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG,
	    &txg) != 0 || txg == 0) {
		return (0);
	}

	/*
	 * First, see if we know about this pool.  If not, then add it to the
	 * list of known pools.
	 */
	for (pe = pl->pools; pe != NULL; pe = pe->pe_next) {
		if (pe->pe_guid == pool_guid)
			break;
	}

	if (pe == NULL) {
		if ((pe = zfs_alloc(hdl, sizeof (pool_entry_t))) == NULL) {
			return (-1);
		}
		pe->pe_guid = pool_guid;
		pe->pe_next = pl->pools;
		pl->pools = pe;
	}

	/*
	 * Second, see if we know about this toplevel vdev.  Add it if its
	 * missing.
	 */
	for (ve = pe->pe_vdevs; ve != NULL; ve = ve->ve_next) {
		if (ve->ve_guid == top_guid)
			break;
	}

	if (ve == NULL) {
		if ((ve = zfs_alloc(hdl, sizeof (vdev_entry_t))) == NULL) {
			return (-1);
		}
		ve->ve_guid = top_guid;
		ve->ve_next = pe->pe_vdevs;
		pe->pe_vdevs = ve;
	}

	/*
	 * Third, see if we have a config with a matching transaction group.  If
	 * so, then we do nothing.  Otherwise, add it to the list of known
	 * configs.
	 */
	for (ce = ve->ve_configs; ce != NULL; ce = ce->ce_next) {
		if (ce->ce_txg == txg)
			break;
	}

	if (ce == NULL) {
		if ((ce = zfs_alloc(hdl, sizeof (config_entry_t))) == NULL) {
			return (-1);
		}
		ce->ce_txg = txg;
		ce->ce_config = fnvlist_dup(config);
		ce->ce_next = ve->ve_configs;
		ve->ve_configs = ce;
	}

	/*
	 * At this point we've successfully added our config to the list of
	 * known configs.  The last thing to do is add the vdev guid -> path
	 * mappings so that we can fix up the configuration as necessary before
	 * doing the import.
	 */
	if ((ne = zfs_alloc(hdl, sizeof (name_entry_t))) == NULL)
		return (-1);

	if ((ne->ne_name = zfs_strdup(hdl, path)) == NULL) {
		free(ne);
		return (-1);
	}

	ne->ne_guid = vdev_guid;
	ne->ne_next = pl->names;
	pl->names = ne;

	return (0);
}

/*
 * Returns true if the named pool matches the given GUID.
 */
static int
pool_active(libzfs_handle_t *hdl, const char *name, uint64_t guid,
    boolean_t *isactive)
{
	zpool_handle_t *zhp;
	uint64_t theguid;

	if (zpool_open_silent(hdl, name, &zhp) != 0)
		return (-1);

	if (zhp == NULL) {
		*isactive = B_FALSE;
		return (0);
	}

	verify(nvlist_lookup_uint64(zhp->zpool_config, ZPOOL_CONFIG_POOL_GUID,
	    &theguid) == 0);

	zpool_close(zhp);

	*isactive = (theguid == guid);
	return (0);
}

static nvlist_t *
refresh_config(libzfs_handle_t *hdl, nvlist_t *config)
{
	nvlist_t *nvl;
	zfs_cmd_t zc = { 0 };
	int err, dstbuf_size;

	if (zcmd_write_conf_nvlist(hdl, &zc, config) != 0)
		return (NULL);

	dstbuf_size = MAX(CONFIG_BUF_MINSIZE, zc.zc_nvlist_conf_size * 4);

	if (zcmd_alloc_dst_nvlist(hdl, &zc, dstbuf_size) != 0) {
		zcmd_free_nvlists(&zc);
		return (NULL);
	}

	while ((err = ioctl(hdl->libzfs_fd, ZFS_IOC_POOL_TRYIMPORT,
	    &zc)) != 0 && errno == ENOMEM) {
		if (zcmd_expand_dst_nvlist(hdl, &zc) != 0) {
			zcmd_free_nvlists(&zc);
			return (NULL);
		}
	}

	if (err) {
		zcmd_free_nvlists(&zc);
		return (NULL);
	}

	if (zcmd_read_dst_nvlist(hdl, &zc, &nvl) != 0) {
		zcmd_free_nvlists(&zc);
		return (NULL);
	}

	zcmd_free_nvlists(&zc);
	return (nvl);
}

/*
 * Determine if the vdev id is a hole in the namespace.
 */
boolean_t
vdev_is_hole(uint64_t *hole_array, uint_t holes, uint_t id)
{
	for (int c = 0; c < holes; c++) {

		/* Top-level is a hole */
		if (hole_array[c] == id)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Convert our list of pools into the definitive set of configurations.  We
 * start by picking the best config for each toplevel vdev.  Once that's done,
 * we assemble the toplevel vdevs into a full config for the pool.  We make a
 * pass to fix up any incorrect paths, and then add it to the main list to
 * return to the user.
 */
static nvlist_t *
get_configs(libzfs_handle_t *hdl, pool_list_t *pl, boolean_t active_ok,
    nvlist_t *policy)
{
	pool_entry_t *pe;
	vdev_entry_t *ve;
	config_entry_t *ce;
	nvlist_t *ret = NULL, *config = NULL, *tmp = NULL, *nvtop, *nvroot;
	nvlist_t **spares, **l2cache;
	uint_t i, nspares, nl2cache;
	boolean_t config_seen;
	uint64_t best_txg;
	char *name, *hostname = NULL;
	uint64_t guid;
	uint_t children = 0;
	nvlist_t **child = NULL;
	uint_t holes;
	uint64_t *hole_array, max_id;
	uint_t c;
	boolean_t isactive;
	uint64_t hostid;
	nvlist_t *nvl;
	boolean_t found_one = B_FALSE;
	boolean_t valid_top_config = B_FALSE;

	if (nvlist_alloc(&ret, 0, 0) != 0)
		goto nomem;

	for (pe = pl->pools; pe != NULL; pe = pe->pe_next) {
		uint64_t id, max_txg = 0;

		if (nvlist_alloc(&config, NV_UNIQUE_NAME, 0) != 0)
			goto nomem;
		config_seen = B_FALSE;

		/*
		 * Iterate over all toplevel vdevs.  Grab the pool configuration
		 * from the first one we find, and then go through the rest and
		 * add them as necessary to the 'vdevs' member of the config.
		 */
		for (ve = pe->pe_vdevs; ve != NULL; ve = ve->ve_next) {

			/*
			 * Determine the best configuration for this vdev by
			 * selecting the config with the latest transaction
			 * group.
			 */
			best_txg = 0;
			for (ce = ve->ve_configs; ce != NULL;
			    ce = ce->ce_next) {

				if (ce->ce_txg > best_txg) {
					tmp = ce->ce_config;
					best_txg = ce->ce_txg;
				}
			}

			/*
			 * We rely on the fact that the max txg for the
			 * pool will contain the most up-to-date information
			 * about the valid top-levels in the vdev namespace.
			 */
			if (best_txg > max_txg) {
				(void) nvlist_remove(config,
				    ZPOOL_CONFIG_VDEV_CHILDREN,
				    DATA_TYPE_UINT64);
				(void) nvlist_remove(config,
				    ZPOOL_CONFIG_HOLE_ARRAY,
				    DATA_TYPE_UINT64_ARRAY);

				max_txg = best_txg;
				hole_array = NULL;
				holes = 0;
				max_id = 0;
				valid_top_config = B_FALSE;

				if (nvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_VDEV_CHILDREN, &max_id) == 0) {
					verify(nvlist_add_uint64(config,
					    ZPOOL_CONFIG_VDEV_CHILDREN,
					    max_id) == 0);
					valid_top_config = B_TRUE;
				}

				if (nvlist_lookup_uint64_array(tmp,
				    ZPOOL_CONFIG_HOLE_ARRAY, &hole_array,
				    &holes) == 0) {
					verify(nvlist_add_uint64_array(config,
					    ZPOOL_CONFIG_HOLE_ARRAY,
					    hole_array, holes) == 0);
				}
			}

			if (!config_seen) {
				/*
				 * Copy the relevant pieces of data to the pool
				 * configuration:
				 *
				 *	version
				 *	pool guid
				 *	name
				 *	comment (if available)
				 *	pool state
				 *	hostid (if available)
				 *	hostname (if available)
				 */
				uint64_t state, version;
				char *comment = NULL;

				version = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_VERSION);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_VERSION, version);
				guid = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_POOL_GUID);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_POOL_GUID, guid);
				name = fnvlist_lookup_string(tmp,
				    ZPOOL_CONFIG_POOL_NAME);
				fnvlist_add_string(config,
				    ZPOOL_CONFIG_POOL_NAME, name);

				if (nvlist_lookup_string(tmp,
				    ZPOOL_CONFIG_COMMENT, &comment) == 0)
					fnvlist_add_string(config,
					    ZPOOL_CONFIG_COMMENT, comment);

				state = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_POOL_STATE);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_POOL_STATE, state);

				hostid = 0;
				if (nvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_HOSTID, &hostid) == 0) {
					fnvlist_add_uint64(config,
					    ZPOOL_CONFIG_HOSTID, hostid);
					hostname = fnvlist_lookup_string(tmp,
					    ZPOOL_CONFIG_HOSTNAME);
					fnvlist_add_string(config,
					    ZPOOL_CONFIG_HOSTNAME, hostname);
				}

				config_seen = B_TRUE;
			}

			/*
			 * Add this top-level vdev to the child array.
			 */
			verify(nvlist_lookup_nvlist(tmp,
			    ZPOOL_CONFIG_VDEV_TREE, &nvtop) == 0);
			verify(nvlist_lookup_uint64(nvtop, ZPOOL_CONFIG_ID,
			    &id) == 0);

			if (id >= children) {
				nvlist_t **newchild;

				newchild = zfs_alloc(hdl, (id + 1) *
				    sizeof (nvlist_t *));
				if (newchild == NULL)
					goto nomem;

				for (c = 0; c < children; c++)
					newchild[c] = child[c];

				free(child);
				child = newchild;
				children = id + 1;
			}
			if (nvlist_dup(nvtop, &child[id], 0) != 0)
				goto nomem;

		}

		/*
		 * If we have information about all the top-levels then
		 * clean up the nvlist which we've constructed. This
		 * means removing any extraneous devices that are
		 * beyond the valid range or adding devices to the end
		 * of our array which appear to be missing.
		 */
		if (valid_top_config) {
			if (max_id < children) {
				for (c = max_id; c < children; c++)
					nvlist_free(child[c]);
				children = max_id;
			} else if (max_id > children) {
				nvlist_t **newchild;

				newchild = zfs_alloc(hdl, (max_id) *
				    sizeof (nvlist_t *));
				if (newchild == NULL)
					goto nomem;

				for (c = 0; c < children; c++)
					newchild[c] = child[c];

				free(child);
				child = newchild;
				children = max_id;
			}
		}

		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);

		/*
		 * The vdev namespace may contain holes as a result of
		 * device removal. We must add them back into the vdev
		 * tree before we process any missing devices.
		 */
		if (holes > 0) {
			ASSERT(valid_top_config);

			for (c = 0; c < children; c++) {
				nvlist_t *holey;

				if (child[c] != NULL ||
				    !vdev_is_hole(hole_array, holes, c))
					continue;

				if (nvlist_alloc(&holey, NV_UNIQUE_NAME,
				    0) != 0)
					goto nomem;

				/*
				 * Holes in the namespace are treated as
				 * "hole" top-level vdevs and have a
				 * special flag set on them.
				 */
				if (nvlist_add_string(holey,
				    ZPOOL_CONFIG_TYPE,
				    VDEV_TYPE_HOLE) != 0 ||
				    nvlist_add_uint64(holey,
				    ZPOOL_CONFIG_ID, c) != 0 ||
				    nvlist_add_uint64(holey,
				    ZPOOL_CONFIG_GUID, 0ULL) != 0) {
					nvlist_free(holey);
					goto nomem;
				}
				child[c] = holey;
			}
		}

		/*
		 * Look for any missing top-level vdevs.  If this is the case,
		 * create a faked up 'missing' vdev as a placeholder.  We cannot
		 * simply compress the child array, because the kernel performs
		 * certain checks to make sure the vdev IDs match their location
		 * in the configuration.
		 */
		for (c = 0; c < children; c++) {
			if (child[c] == NULL) {
				nvlist_t *missing;
				if (nvlist_alloc(&missing, NV_UNIQUE_NAME,
				    0) != 0)
					goto nomem;
				if (nvlist_add_string(missing,
				    ZPOOL_CONFIG_TYPE,
				    VDEV_TYPE_MISSING) != 0 ||
				    nvlist_add_uint64(missing,
				    ZPOOL_CONFIG_ID, c) != 0 ||
				    nvlist_add_uint64(missing,
				    ZPOOL_CONFIG_GUID, 0ULL) != 0) {
					nvlist_free(missing);
					goto nomem;
				}
				child[c] = missing;
			}
		}

		/*
		 * Put all of this pool's top-level vdevs into a root vdev.
		 */
		if (nvlist_alloc(&nvroot, NV_UNIQUE_NAME, 0) != 0)
			goto nomem;
		if (nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE,
		    VDEV_TYPE_ROOT) != 0 ||
		    nvlist_add_uint64(nvroot, ZPOOL_CONFIG_ID, 0ULL) != 0 ||
		    nvlist_add_uint64(nvroot, ZPOOL_CONFIG_GUID, guid) != 0 ||
		    nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
		    child, children) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}

		for (c = 0; c < children; c++)
			nvlist_free(child[c]);
		free(child);
		children = 0;
		child = NULL;

		/*
		 * Go through and fix up any paths and/or devids based on our
		 * known list of vdev GUID -> path mappings.
		 */
		if (fix_paths(nvroot, pl->names) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}

		/*
		 * Add the root vdev to this pool's configuration.
		 */
		if (nvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    nvroot) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}
		nvlist_free(nvroot);

		/*
		 * zdb uses this path to report on active pools that were
		 * imported or created using -R.
		 */
		if (active_ok)
			goto add_pool;

		/*
		 * Determine if this pool is currently active, in which case we
		 * can't actually import it.
		 */
		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0);
		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);

		if (pool_active(hdl, name, guid, &isactive) != 0)
			goto error;

		if (isactive) {
			nvlist_free(config);
			config = NULL;
			continue;
		}

		if (policy != NULL) {
			if (nvlist_add_nvlist(config, ZPOOL_LOAD_POLICY,
			    policy) != 0)
				goto nomem;
		}

		if ((nvl = refresh_config(hdl, config)) == NULL) {
			nvlist_free(config);
			config = NULL;
			continue;
		}

		nvlist_free(config);
		config = nvl;

		/*
		 * Go through and update the paths for spares, now that we have
		 * them.
		 */
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &spares, &nspares) == 0) {
			for (i = 0; i < nspares; i++) {
				if (fix_paths(spares[i], pl->names) != 0)
					goto nomem;
			}
		}

		/*
		 * Update the paths for l2cache devices.
		 */
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache) == 0) {
			for (i = 0; i < nl2cache; i++) {
				if (fix_paths(l2cache[i], pl->names) != 0)
					goto nomem;
			}
		}

		/*
		 * Restore the original information read from the actual label.
		 */
		(void) nvlist_remove(config, ZPOOL_CONFIG_HOSTID,
		    DATA_TYPE_UINT64);
		(void) nvlist_remove(config, ZPOOL_CONFIG_HOSTNAME,
		    DATA_TYPE_STRING);
		if (hostid != 0) {
			verify(nvlist_add_uint64(config, ZPOOL_CONFIG_HOSTID,
			    hostid) == 0);
			verify(nvlist_add_string(config, ZPOOL_CONFIG_HOSTNAME,
			    hostname) == 0);
		}

add_pool:
		/*
		 * Add this pool to the list of configs.
		 */
		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0);
		if (nvlist_add_nvlist(ret, name, config) != 0)
			goto nomem;

		found_one = B_TRUE;
		nvlist_free(config);
		config = NULL;
	}

	if (!found_one) {
		nvlist_free(ret);
		ret = NULL;
	}

	return (ret);

nomem:
	(void) no_memory(hdl);
error:
	nvlist_free(config);
	nvlist_free(ret);
	for (c = 0; c < children; c++)
		nvlist_free(child[c]);
	free(child);

	return (NULL);
}

/*
 * Return the offset of the given label.
 */
static uint64_t
label_offset(uint64_t size, int l)
{
	ASSERT(P2PHASE_TYPED(size, sizeof (vdev_label_t), uint64_t) == 0);
	return (l * sizeof (vdev_label_t) + (l < VDEV_LABELS / 2 ?
	    0 : size - VDEV_LABELS * sizeof (vdev_label_t)));
}

/*
 * Given a file descriptor, read the label information and return an nvlist
 * describing the configuration, if there is one.
 * Return 0 on success, or -1 on failure
 */
int
zpool_read_label(int fd, nvlist_t **config)
{
	struct stat64 statbuf;
	int l;
	vdev_label_t *label;
	uint64_t state, txg, size;

	*config = NULL;

	if (fstat64(fd, &statbuf) == -1)
		return (-1);
	size = P2ALIGN_TYPED(statbuf.st_size, sizeof (vdev_label_t), uint64_t);

	if ((label = malloc(sizeof (vdev_label_t))) == NULL)
		return (-1);

	for (l = 0; l < VDEV_LABELS; l++) {
		if (pread64(fd, label, sizeof (vdev_label_t),
		    label_offset(size, l)) != sizeof (vdev_label_t))
			continue;

		if (nvlist_unpack(label->vl_vdev_phys.vp_nvlist,
		    sizeof (label->vl_vdev_phys.vp_nvlist), config, 0) != 0)
			continue;

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state > POOL_STATE_L2CACHE) {
			nvlist_free(*config);
			continue;
		}

		if (state != POOL_STATE_SPARE && state != POOL_STATE_L2CACHE &&
		    (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_TXG,
		    &txg) != 0 || txg == 0)) {
			nvlist_free(*config);
			continue;
		}

		free(label);
		return (0);
	}

	free(label);
	*config = NULL;
	errno = ENOENT;
	return (-1);
}

/*
 * Given a file descriptor, read the label information and return an nvlist
 * describing the configuration, if there is one.
 * returns the number of valid labels found
 * If a label is found, returns it via config.  The caller is responsible for
 * freeing it.
 */
int
zpool_read_all_labels(int fd, nvlist_t **config)
{
	struct stat64 statbuf;
	struct aiocb aiocbs[VDEV_LABELS];
	struct aiocb *aiocbps[VDEV_LABELS];
	int l;
	vdev_phys_t *labels;
	uint64_t state, txg, size;
	int nlabels = 0;

	*config = NULL;

	if (fstat64(fd, &statbuf) == -1)
		return (0);
	size = P2ALIGN_TYPED(statbuf.st_size, sizeof (vdev_label_t), uint64_t);

	if ((labels = calloc(VDEV_LABELS, sizeof (vdev_phys_t))) == NULL)
		return (0);

	memset(aiocbs, 0, sizeof(aiocbs));
	for (l = 0; l < VDEV_LABELS; l++) {
		aiocbs[l].aio_fildes = fd;
		aiocbs[l].aio_offset = label_offset(size, l) + VDEV_SKIP_SIZE;
		aiocbs[l].aio_buf = &labels[l];
		aiocbs[l].aio_nbytes = sizeof(vdev_phys_t);
		aiocbs[l].aio_lio_opcode = LIO_READ;
		aiocbps[l] = &aiocbs[l];
	}

	if (lio_listio(LIO_WAIT, aiocbps, VDEV_LABELS, NULL) != 0) {
		if (errno == EAGAIN || errno == EINTR || errno == EIO) {
			for (l = 0; l < VDEV_LABELS; l++) {
				errno = 0;
				int r = aio_error(&aiocbs[l]);
				if (r != EINVAL)
					(void)aio_return(&aiocbs[l]);
			}
		}
		free(labels);
		return (0);
	}

	for (l = 0; l < VDEV_LABELS; l++) {
		nvlist_t *temp = NULL;

		if (aio_return(&aiocbs[l]) != sizeof(vdev_phys_t))
			continue;

		if (nvlist_unpack(labels[l].vp_nvlist,
		    sizeof (labels[l].vp_nvlist), &temp, 0) != 0)
			continue;

		if (nvlist_lookup_uint64(temp, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state > POOL_STATE_L2CACHE) {
			nvlist_free(temp);
			temp = NULL;
			continue;
		}

		if (state != POOL_STATE_SPARE && state != POOL_STATE_L2CACHE &&
		    (nvlist_lookup_uint64(temp, ZPOOL_CONFIG_POOL_TXG,
		    &txg) != 0 || txg == 0)) {
			nvlist_free(temp);
			temp = NULL;
			continue;
		}
		if (temp)
			*config = temp;

		nlabels++;
	}

	free(labels);
	return (nlabels);
}

typedef struct rdsk_node {
	char *rn_name;
	int rn_dfd;
	libzfs_handle_t *rn_hdl;
	nvlist_t *rn_config;
	avl_tree_t *rn_avl;
	avl_node_t rn_node;
	boolean_t rn_nozpool;
} rdsk_node_t;

static int
slice_cache_compare(const void *arg1, const void *arg2)
{
	const char  *nm1 = ((rdsk_node_t *)arg1)->rn_name;
	const char  *nm2 = ((rdsk_node_t *)arg2)->rn_name;
	char *nm1slice, *nm2slice;
	int rv;

	/*
	 * slices zero and two are the most likely to provide results,
	 * so put those first
	 */
	nm1slice = strstr(nm1, "s0");
	nm2slice = strstr(nm2, "s0");
	if (nm1slice && !nm2slice) {
		return (-1);
	}
	if (!nm1slice && nm2slice) {
		return (1);
	}
	nm1slice = strstr(nm1, "s2");
	nm2slice = strstr(nm2, "s2");
	if (nm1slice && !nm2slice) {
		return (-1);
	}
	if (!nm1slice && nm2slice) {
		return (1);
	}

	rv = strcmp(nm1, nm2);
	if (rv == 0)
		return (0);
	return (rv > 0 ? 1 : -1);
}

#ifdef illumos
static void
check_one_slice(avl_tree_t *r, char *diskname, uint_t partno,
    diskaddr_t size, uint_t blksz)
{
	rdsk_node_t tmpnode;
	rdsk_node_t *node;
	char sname[MAXNAMELEN];

	tmpnode.rn_name = &sname[0];
	(void) snprintf(tmpnode.rn_name, MAXNAMELEN, "%s%u",
	    diskname, partno);
	/*
	 * protect against division by zero for disk labels that
	 * contain a bogus sector size
	 */
	if (blksz == 0)
		blksz = DEV_BSIZE;
	/* too small to contain a zpool? */
	if ((size < (SPA_MINDEVSIZE / blksz)) &&
	    (node = avl_find(r, &tmpnode, NULL)))
		node->rn_nozpool = B_TRUE;
}
#endif	/* illumos */

static void
nozpool_all_slices(avl_tree_t *r, const char *sname)
{
#ifdef illumos
	char diskname[MAXNAMELEN];
	char *ptr;
	int i;

	(void) strncpy(diskname, sname, MAXNAMELEN);
	if (((ptr = strrchr(diskname, 's')) == NULL) &&
	    ((ptr = strrchr(diskname, 'p')) == NULL))
		return;
	ptr[0] = 's';
	ptr[1] = '\0';
	for (i = 0; i < NDKMAP; i++)
		check_one_slice(r, diskname, i, 0, 1);
	ptr[0] = 'p';
	for (i = 0; i <= FD_NUMPART; i++)
		check_one_slice(r, diskname, i, 0, 1);
#endif	/* illumos */
}

#ifdef illumos
static void
check_slices(avl_tree_t *r, int fd, const char *sname)
{
	struct extvtoc vtoc;
	struct dk_gpt *gpt;
	char diskname[MAXNAMELEN];
	char *ptr;
	int i;

	(void) strncpy(diskname, sname, MAXNAMELEN);
	if ((ptr = strrchr(diskname, 's')) == NULL || !isdigit(ptr[1]))
		return;
	ptr[1] = '\0';

	if (read_extvtoc(fd, &vtoc) >= 0) {
		for (i = 0; i < NDKMAP; i++)
			check_one_slice(r, diskname, i,
			    vtoc.v_part[i].p_size, vtoc.v_sectorsz);
	} else if (efi_alloc_and_read(fd, &gpt) >= 0) {
		/*
		 * on x86 we'll still have leftover links that point
		 * to slices s[9-15], so use NDKMAP instead
		 */
		for (i = 0; i < NDKMAP; i++)
			check_one_slice(r, diskname, i,
			    gpt->efi_parts[i].p_size, gpt->efi_lbasize);
		/* nodes p[1-4] are never used with EFI labels */
		ptr[0] = 'p';
		for (i = 1; i <= FD_NUMPART; i++)
			check_one_slice(r, diskname, i, 0, 1);
		efi_free(gpt);
	}
}
#endif	/* illumos */

static void
zpool_open_func(void *arg)
{
	rdsk_node_t *rn = arg;
	struct stat64 statbuf;
	nvlist_t *config;
	int fd;

	if (rn->rn_nozpool)
		return;
	if ((fd = openat64(rn->rn_dfd, rn->rn_name, O_RDONLY)) < 0) {
		/* symlink to a device that's no longer there */
		if (errno == ENOENT)
			nozpool_all_slices(rn->rn_avl, rn->rn_name);
		return;
	}
	/*
	 * Ignore failed stats.  We only want regular
	 * files, character devs and block devs.
	 */
	if (fstat64(fd, &statbuf) != 0 ||
	    (!S_ISREG(statbuf.st_mode) &&
	    !S_ISCHR(statbuf.st_mode) &&
	    !S_ISBLK(statbuf.st_mode))) {
		(void) close(fd);
		return;
	}
	/* this file is too small to hold a zpool */
#ifdef illumos
	if (S_ISREG(statbuf.st_mode) &&
	    statbuf.st_size < SPA_MINDEVSIZE) {
		(void) close(fd);
		return;
	} else if (!S_ISREG(statbuf.st_mode)) {
		/*
		 * Try to read the disk label first so we don't have to
		 * open a bunch of minor nodes that can't have a zpool.
		 */
		check_slices(rn->rn_avl, fd, rn->rn_name);
	}
#else	/* !illumos */
	if (statbuf.st_size < SPA_MINDEVSIZE) {
		(void) close(fd);
		return;
	}
#endif	/* illumos */

	if ((zpool_read_label(fd, &config)) != 0 && errno == ENOMEM) {
		(void) close(fd);
		(void) no_memory(rn->rn_hdl);
		return;
	}
	(void) close(fd);

	rn->rn_config = config;
}

/*
 * Given a file descriptor, clear (zero) the label information.
 */
int
zpool_clear_label(int fd)
{
	struct stat64 statbuf;
	int l;
	vdev_label_t *label;
	uint64_t size;

	if (fstat64(fd, &statbuf) == -1)
		return (0);
	size = P2ALIGN_TYPED(statbuf.st_size, sizeof (vdev_label_t), uint64_t);

	if ((label = calloc(sizeof (vdev_label_t), 1)) == NULL)
		return (-1);

	for (l = 0; l < VDEV_LABELS; l++) {
		if (pwrite64(fd, label, sizeof (vdev_label_t),
		    label_offset(size, l)) != sizeof (vdev_label_t)) {
			free(label);
			return (-1);
		}
	}

	free(label);
	return (0);
}

/*
 * Given a list of directories to search, find all pools stored on disk.  This
 * includes partial pools which are not available to import.  If no args are
 * given (argc is 0), then the default directory (/dev/dsk) is searched.
 * poolname or guid (but not both) are provided by the caller when trying
 * to import a specific pool.
 */
static nvlist_t *
zpool_find_import_impl(libzfs_handle_t *hdl, importargs_t *iarg)
{
	int i, dirs = iarg->paths;
	struct dirent64 *dp;
	char path[MAXPATHLEN];
	char *end, **dir = iarg->path;
	size_t pathleft;
	nvlist_t *ret = NULL;
	static char *default_dir = "/dev";
	pool_list_t pools = { 0 };
	pool_entry_t *pe, *penext;
	vdev_entry_t *ve, *venext;
	config_entry_t *ce, *cenext;
	name_entry_t *ne, *nenext;
	avl_tree_t slice_cache;
	rdsk_node_t *slice;
	void *cookie;

	if (dirs == 0) {
		dirs = 1;
		dir = &default_dir;
	}

	/*
	 * Go through and read the label configuration information from every
	 * possible device, organizing the information according to pool GUID
	 * and toplevel GUID.
	 */
	for (i = 0; i < dirs; i++) {
		tpool_t *t;
		char rdsk[MAXPATHLEN];
		int dfd;
		boolean_t config_failed = B_FALSE;
		DIR *dirp;

		/* use realpath to normalize the path */
		if (realpath(dir[i], path) == 0) {
			(void) zfs_error_fmt(hdl, EZFS_BADPATH,
			    dgettext(TEXT_DOMAIN, "cannot open '%s'"), dir[i]);
			goto error;
		}
		end = &path[strlen(path)];
		*end++ = '/';
		*end = 0;
		pathleft = &path[sizeof (path)] - end;

#ifdef illumos
		/*
		 * Using raw devices instead of block devices when we're
		 * reading the labels skips a bunch of slow operations during
		 * close(2) processing, so we replace /dev/dsk with /dev/rdsk.
		 */
		if (strcmp(path, ZFS_DISK_ROOTD) == 0)
			(void) strlcpy(rdsk, ZFS_RDISK_ROOTD, sizeof (rdsk));
		else
#endif
			(void) strlcpy(rdsk, path, sizeof (rdsk));

		if ((dfd = open64(rdsk, O_RDONLY)) < 0 ||
		    (dirp = fdopendir(dfd)) == NULL) {
			if (dfd >= 0)
				(void) close(dfd);
			zfs_error_aux(hdl, strerror(errno));
			(void) zfs_error_fmt(hdl, EZFS_BADPATH,
			    dgettext(TEXT_DOMAIN, "cannot open '%s'"),
			    rdsk);
			goto error;
		}

		avl_create(&slice_cache, slice_cache_compare,
		    sizeof (rdsk_node_t), offsetof(rdsk_node_t, rn_node));

		if (strcmp(rdsk, "/dev/") == 0) {
			struct gmesh mesh;
			struct gclass *mp;
			struct ggeom *gp;
			struct gprovider *pp;

			errno = geom_gettree(&mesh);
			if (errno != 0) {
				zfs_error_aux(hdl, strerror(errno));
				(void) zfs_error_fmt(hdl, EZFS_BADPATH,
				    dgettext(TEXT_DOMAIN, "cannot get GEOM tree"));
				goto error;
			}

			LIST_FOREACH(mp, &mesh.lg_class, lg_class) {
		        	LIST_FOREACH(gp, &mp->lg_geom, lg_geom) {
					LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
						slice = zfs_alloc(hdl, sizeof (rdsk_node_t));
						slice->rn_name = zfs_strdup(hdl, pp->lg_name);
						slice->rn_avl = &slice_cache;
						slice->rn_dfd = dfd;
						slice->rn_hdl = hdl;
						slice->rn_nozpool = B_FALSE;
						avl_add(&slice_cache, slice);
					}
				}
			}

			geom_deletetree(&mesh);
			goto skipdir;
		}

		/*
		 * This is not MT-safe, but we have no MT consumers of libzfs
		 */
		while ((dp = readdir64(dirp)) != NULL) {
			const char *name = dp->d_name;
			if (name[0] == '.' &&
			    (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
				continue;

			slice = zfs_alloc(hdl, sizeof (rdsk_node_t));
			slice->rn_name = zfs_strdup(hdl, name);
			slice->rn_avl = &slice_cache;
			slice->rn_dfd = dfd;
			slice->rn_hdl = hdl;
			slice->rn_nozpool = B_FALSE;
			avl_add(&slice_cache, slice);
		}
skipdir:
		/*
		 * create a thread pool to do all of this in parallel;
		 * rn_nozpool is not protected, so this is racy in that
		 * multiple tasks could decide that the same slice can
		 * not hold a zpool, which is benign.  Also choose
		 * double the number of processors; we hold a lot of
		 * locks in the kernel, so going beyond this doesn't
		 * buy us much.
		 */
		t = tpool_create(1, 2 * sysconf(_SC_NPROCESSORS_ONLN),
		    0, NULL);
		for (slice = avl_first(&slice_cache); slice;
		    (slice = avl_walk(&slice_cache, slice,
		    AVL_AFTER)))
			(void) tpool_dispatch(t, zpool_open_func, slice);
		tpool_wait(t);
		tpool_destroy(t);

		cookie = NULL;
		while ((slice = avl_destroy_nodes(&slice_cache,
		    &cookie)) != NULL) {
			if (slice->rn_config != NULL && !config_failed) {
				nvlist_t *config = slice->rn_config;
				boolean_t matched = B_TRUE;

				if (iarg->poolname != NULL) {
					char *pname;

					matched = nvlist_lookup_string(config,
					    ZPOOL_CONFIG_POOL_NAME,
					    &pname) == 0 &&
					    strcmp(iarg->poolname, pname) == 0;
				} else if (iarg->guid != 0) {
					uint64_t this_guid;

					matched = nvlist_lookup_uint64(config,
					    ZPOOL_CONFIG_POOL_GUID,
					    &this_guid) == 0 &&
					    iarg->guid == this_guid;
				}
				if (matched) {
					/*
					 * use the non-raw path for the config
					 */
					(void) strlcpy(end, slice->rn_name,
					    pathleft);
					if (add_config(hdl, &pools, path,
					    config) != 0)
						config_failed = B_TRUE;
				}
				nvlist_free(config);
			}
			free(slice->rn_name);
			free(slice);
		}
		avl_destroy(&slice_cache);

		(void) closedir(dirp);

		if (config_failed)
			goto error;
	}

	ret = get_configs(hdl, &pools, iarg->can_be_active, iarg->policy);

error:
	for (pe = pools.pools; pe != NULL; pe = penext) {
		penext = pe->pe_next;
		for (ve = pe->pe_vdevs; ve != NULL; ve = venext) {
			venext = ve->ve_next;
			for (ce = ve->ve_configs; ce != NULL; ce = cenext) {
				cenext = ce->ce_next;
				nvlist_free(ce->ce_config);
				free(ce);
			}
			free(ve);
		}
		free(pe);
	}

	for (ne = pools.names; ne != NULL; ne = nenext) {
		nenext = ne->ne_next;
		free(ne->ne_name);
		free(ne);
	}

	return (ret);
}

nvlist_t *
zpool_find_import(libzfs_handle_t *hdl, int argc, char **argv)
{
	importargs_t iarg = { 0 };

	iarg.paths = argc;
	iarg.path = argv;

	return (zpool_find_import_impl(hdl, &iarg));
}

/*
 * Given a cache file, return the contents as a list of importable pools.
 * poolname or guid (but not both) are provided by the caller when trying
 * to import a specific pool.
 */
nvlist_t *
zpool_find_import_cached(libzfs_handle_t *hdl, const char *cachefile,
    char *poolname, uint64_t guid)
{
	char *buf;
	int fd;
	struct stat64 statbuf;
	nvlist_t *raw, *src, *dst;
	nvlist_t *pools;
	nvpair_t *elem;
	char *name;
	uint64_t this_guid;
	boolean_t active;

	verify(poolname == NULL || guid == 0);

	if ((fd = open(cachefile, O_RDONLY)) < 0) {
		zfs_error_aux(hdl, "%s", strerror(errno));
		(void) zfs_error(hdl, EZFS_BADCACHE,
		    dgettext(TEXT_DOMAIN, "failed to open cache file"));
		return (NULL);
	}

	if (fstat64(fd, &statbuf) != 0) {
		zfs_error_aux(hdl, "%s", strerror(errno));
		(void) close(fd);
		(void) zfs_error(hdl, EZFS_BADCACHE,
		    dgettext(TEXT_DOMAIN, "failed to get size of cache file"));
		return (NULL);
	}

	if ((buf = zfs_alloc(hdl, statbuf.st_size)) == NULL) {
		(void) close(fd);
		return (NULL);
	}

	if (read(fd, buf, statbuf.st_size) != statbuf.st_size) {
		(void) close(fd);
		free(buf);
		(void) zfs_error(hdl, EZFS_BADCACHE,
		    dgettext(TEXT_DOMAIN,
		    "failed to read cache file contents"));
		return (NULL);
	}

	(void) close(fd);

	if (nvlist_unpack(buf, statbuf.st_size, &raw, 0) != 0) {
		free(buf);
		(void) zfs_error(hdl, EZFS_BADCACHE,
		    dgettext(TEXT_DOMAIN,
		    "invalid or corrupt cache file contents"));
		return (NULL);
	}

	free(buf);

	/*
	 * Go through and get the current state of the pools and refresh their
	 * state.
	 */
	if (nvlist_alloc(&pools, 0, 0) != 0) {
		(void) no_memory(hdl);
		nvlist_free(raw);
		return (NULL);
	}

	elem = NULL;
	while ((elem = nvlist_next_nvpair(raw, elem)) != NULL) {
		src = fnvpair_value_nvlist(elem);

		name = fnvlist_lookup_string(src, ZPOOL_CONFIG_POOL_NAME);
		if (poolname != NULL && strcmp(poolname, name) != 0)
			continue;

		this_guid = fnvlist_lookup_uint64(src, ZPOOL_CONFIG_POOL_GUID);
		if (guid != 0 && guid != this_guid)
			continue;

		if (pool_active(hdl, name, this_guid, &active) != 0) {
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}

		if (active)
			continue;

		if (nvlist_add_string(src, ZPOOL_CONFIG_CACHEFILE,
		    cachefile) != 0) {
			(void) no_memory(hdl);
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}

		if ((dst = refresh_config(hdl, src)) == NULL) {
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}

		if (nvlist_add_nvlist(pools, nvpair_name(elem), dst) != 0) {
			(void) no_memory(hdl);
			nvlist_free(dst);
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}
		nvlist_free(dst);
	}

	nvlist_free(raw);
	return (pools);
}

static int
name_or_guid_exists(zpool_handle_t *zhp, void *data)
{
	importargs_t *import = data;
	int found = 0;

	if (import->poolname != NULL) {
		char *pool_name;

		verify(nvlist_lookup_string(zhp->zpool_config,
		    ZPOOL_CONFIG_POOL_NAME, &pool_name) == 0);
		if (strcmp(pool_name, import->poolname) == 0)
			found = 1;
	} else {
		uint64_t pool_guid;

		verify(nvlist_lookup_uint64(zhp->zpool_config,
		    ZPOOL_CONFIG_POOL_GUID, &pool_guid) == 0);
		if (pool_guid == import->guid)
			found = 1;
	}

	zpool_close(zhp);
	return (found);
}

nvlist_t *
zpool_search_import(libzfs_handle_t *hdl, importargs_t *import)
{
	verify(import->poolname == NULL || import->guid == 0);

	if (import->unique)
		import->exists = zpool_iter(hdl, name_or_guid_exists, import);

	if (import->cachefile != NULL)
		return (zpool_find_import_cached(hdl, import->cachefile,
		    import->poolname, import->guid));

	return (zpool_find_import_impl(hdl, import));
}

boolean_t
find_guid(nvlist_t *nv, uint64_t guid)
{
	uint64_t tmp;
	nvlist_t **child;
	uint_t c, children;

	verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &tmp) == 0);
	if (tmp == guid)
		return (B_TRUE);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if (find_guid(child[c], guid))
				return (B_TRUE);
	}

	return (B_FALSE);
}

typedef struct aux_cbdata {
	const char	*cb_type;
	uint64_t	cb_guid;
	zpool_handle_t	*cb_zhp;
} aux_cbdata_t;

static int
find_aux(zpool_handle_t *zhp, void *data)
{
	aux_cbdata_t *cbp = data;
	nvlist_t **list;
	uint_t i, count;
	uint64_t guid;
	nvlist_t *nvroot;

	verify(nvlist_lookup_nvlist(zhp->zpool_config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	if (nvlist_lookup_nvlist_array(nvroot, cbp->cb_type,
	    &list, &count) == 0) {
		for (i = 0; i < count; i++) {
			verify(nvlist_lookup_uint64(list[i],
			    ZPOOL_CONFIG_GUID, &guid) == 0);
			if (guid == cbp->cb_guid) {
				cbp->cb_zhp = zhp;
				return (1);
			}
		}
	}

	zpool_close(zhp);
	return (0);
}

/*
 * Determines if the pool is in use.  If so, it returns true and the state of
 * the pool as well as the name of the pool.  Both strings are allocated and
 * must be freed by the caller.
 */
int
zpool_in_use(libzfs_handle_t *hdl, int fd, pool_state_t *state, char **namestr,
    boolean_t *inuse)
{
	nvlist_t *config;
	char *name;
	boolean_t ret;
	uint64_t guid, vdev_guid;
	zpool_handle_t *zhp;
	nvlist_t *pool_config;
	uint64_t stateval, isspare;
	aux_cbdata_t cb = { 0 };
	boolean_t isactive;

	*inuse = B_FALSE;

	if (zpool_read_label(fd, &config) != 0 && errno == ENOMEM) {
		(void) no_memory(hdl);
		return (-1);
	}

	if (config == NULL)
		return (0);

	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &stateval) == 0);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID,
	    &vdev_guid) == 0);

	if (stateval != POOL_STATE_SPARE && stateval != POOL_STATE_L2CACHE) {
		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0);
		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);
	}

	switch (stateval) {
	case POOL_STATE_EXPORTED:
		/*
		 * A pool with an exported state may in fact be imported
		 * read-only, so check the in-core state to see if it's
		 * active and imported read-only.  If it is, set
		 * its state to active.
		 */
		if (pool_active(hdl, name, guid, &isactive) == 0 && isactive &&
		    (zhp = zpool_open_canfail(hdl, name)) != NULL) {
			if (zpool_get_prop_int(zhp, ZPOOL_PROP_READONLY, NULL))
				stateval = POOL_STATE_ACTIVE;

			/*
			 * All we needed the zpool handle for is the
			 * readonly prop check.
			 */
			zpool_close(zhp);
		}

		ret = B_TRUE;
		break;

	case POOL_STATE_ACTIVE:
		/*
		 * For an active pool, we have to determine if it's really part
		 * of a currently active pool (in which case the pool will exist
		 * and the guid will be the same), or whether it's part of an
		 * active pool that was disconnected without being explicitly
		 * exported.
		 */
		if (pool_active(hdl, name, guid, &isactive) != 0) {
			nvlist_free(config);
			return (-1);
		}

		if (isactive) {
			/*
			 * Because the device may have been removed while
			 * offlined, we only report it as active if the vdev is
			 * still present in the config.  Otherwise, pretend like
			 * it's not in use.
			 */
			if ((zhp = zpool_open_canfail(hdl, name)) != NULL &&
			    (pool_config = zpool_get_config(zhp, NULL))
			    != NULL) {
				nvlist_t *nvroot;

				verify(nvlist_lookup_nvlist(pool_config,
				    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);
				ret = find_guid(nvroot, vdev_guid);
			} else {
				ret = B_FALSE;
			}

			/*
			 * If this is an active spare within another pool, we
			 * treat it like an unused hot spare.  This allows the
			 * user to create a pool with a hot spare that currently
			 * in use within another pool.  Since we return B_TRUE,
			 * libdiskmgt will continue to prevent generic consumers
			 * from using the device.
			 */
			if (ret && nvlist_lookup_uint64(config,
			    ZPOOL_CONFIG_IS_SPARE, &isspare) == 0 && isspare)
				stateval = POOL_STATE_SPARE;

			if (zhp != NULL)
				zpool_close(zhp);
		} else {
			stateval = POOL_STATE_POTENTIALLY_ACTIVE;
			ret = B_TRUE;
		}
		break;

	case POOL_STATE_SPARE:
		/*
		 * For a hot spare, it can be either definitively in use, or
		 * potentially active.  To determine if it's in use, we iterate
		 * over all pools in the system and search for one with a spare
		 * with a matching guid.
		 *
		 * Due to the shared nature of spares, we don't actually report
		 * the potentially active case as in use.  This means the user
		 * can freely create pools on the hot spares of exported pools,
		 * but to do otherwise makes the resulting code complicated, and
		 * we end up having to deal with this case anyway.
		 */
		cb.cb_zhp = NULL;
		cb.cb_guid = vdev_guid;
		cb.cb_type = ZPOOL_CONFIG_SPARES;
		if (zpool_iter(hdl, find_aux, &cb) == 1) {
			name = (char *)zpool_get_name(cb.cb_zhp);
			ret = B_TRUE;
		} else {
			ret = B_FALSE;
		}
		break;

	case POOL_STATE_L2CACHE:

		/*
		 * Check if any pool is currently using this l2cache device.
		 */
		cb.cb_zhp = NULL;
		cb.cb_guid = vdev_guid;
		cb.cb_type = ZPOOL_CONFIG_L2CACHE;
		if (zpool_iter(hdl, find_aux, &cb) == 1) {
			name = (char *)zpool_get_name(cb.cb_zhp);
			ret = B_TRUE;
		} else {
			ret = B_FALSE;
		}
		break;

	default:
		ret = B_FALSE;
	}


	if (ret) {
		if ((*namestr = zfs_strdup(hdl, name)) == NULL) {
			if (cb.cb_zhp)
				zpool_close(cb.cb_zhp);
			nvlist_free(config);
			return (-1);
		}
		*state = (pool_state_t)stateval;
	}

	if (cb.cb_zhp)
		zpool_close(cb.cb_zhp);

	nvlist_free(config);
	*inuse = ret;
	return (0);
}
