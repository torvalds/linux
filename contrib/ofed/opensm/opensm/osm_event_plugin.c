/*
 * Copyright (c) 2008-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2007 The Regents of the University of California.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/****h* OpenSM Event plugin interface
* DESCRIPTION
*       Database interface to record subnet events
*
*       Implementations of this object _MUST_ be thread safe.
*
* AUTHOR
*	Ira Weiny, LLNL
*
*********/

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <dlfcn.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_EVENT_PLUGIN_C
#include <opensm/osm_event_plugin.h>
#include <opensm/osm_opensm.h>

#if defined(PATH_MAX)
#define OSM_PATH_MAX	(PATH_MAX + 1)
#elif defined (_POSIX_PATH_MAX)
#define OSM_PATH_MAX	(_POSIX_PATH_MAX + 1)
#else
#define OSM_PATH_MAX	256
#endif

/**
 * functions
 */
osm_epi_plugin_t *osm_epi_construct(osm_opensm_t *osm, char *plugin_name)
{
	char lib_name[OSM_PATH_MAX];
	struct old_if { unsigned ver; } *old_impl;
	osm_epi_plugin_t *rc = NULL;

	if (!plugin_name || !*plugin_name)
		return NULL;

	/* find the plugin */
	snprintf(lib_name, sizeof(lib_name), "lib%s.so", plugin_name);

	rc = malloc(sizeof(*rc));
	if (!rc)
		return NULL;

	rc->handle = dlopen(lib_name, RTLD_LAZY);
	if (!rc->handle) {
		OSM_LOG(&osm->log, OSM_LOG_ERROR,
			"Failed to open event plugin \"%s\" : \"%s\"\n",
			lib_name, dlerror());
		goto DLOPENFAIL;
	}

	rc->impl =
	    (osm_event_plugin_t *) dlsym(rc->handle,
					 OSM_EVENT_PLUGIN_IMPL_NAME);
	if (!rc->impl) {
		OSM_LOG(&osm->log, OSM_LOG_ERROR,
			"Failed to find \"%s\" symbol in \"%s\" : \"%s\"\n",
			OSM_EVENT_PLUGIN_IMPL_NAME, lib_name, dlerror());
		goto Exit;
	}

	/* check for old interface */
	old_impl = (struct old_if *) rc->impl;
	if (old_impl->ver == OSM_ORIG_EVENT_PLUGIN_INTERFACE_VER) {
		OSM_LOG(&osm->log, OSM_LOG_ERROR, "Error loading plugin: "
			"\'%s\' contains a depricated interface version %d\n"
			"   Please recompile with the new interface.\n",
			plugin_name, old_impl->ver);
		goto Exit;
	}

	/* Check the version to make sure this module will work with us */
	if (strcmp(rc->impl->osm_version, osm->osm_version)) {
		OSM_LOG(&osm->log, OSM_LOG_ERROR, "Error loading plugin"
			" \'%s\': OpenSM version mismatch - plugin was built"
			" against %s version of OpenSM. Skip loading.\n",
			plugin_name, rc->impl->osm_version);
		goto Exit;
	}

	if (!rc->impl->create) {
		OSM_LOG(&osm->log, OSM_LOG_ERROR,
			"Error loading plugin \'%s\': no create() method.\n",
			plugin_name);
		goto Exit;
	}

	rc->plugin_data = rc->impl->create(osm);

	if (!rc->plugin_data)
		goto Exit;

	rc->plugin_name = strdup(plugin_name);
	return rc;

Exit:
	dlclose(rc->handle);
DLOPENFAIL:
	free(rc);
	return NULL;
}

void osm_epi_destroy(osm_epi_plugin_t * plugin)
{
	if (plugin) {
		if (plugin->impl->delete)
			plugin->impl->delete(plugin->plugin_data);
		dlclose(plugin->handle);
		free(plugin->plugin_name);
		free(plugin);
	}
}
