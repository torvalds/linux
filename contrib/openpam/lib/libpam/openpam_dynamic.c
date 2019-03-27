/*-
 * Copyright (c) 2002-2003 Networks Associates Technology, Inc.
 * Copyright (c) 2004-2011 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * Network Associates Laboratories, the Security Research Division of
 * Network Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $OpenPAM: openpam_dynamic.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/param.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"
#include "openpam_asprintf.h"
#include "openpam_ctype.h"
#include "openpam_dlfunc.h"

#ifndef RTLD_NOW
#define RTLD_NOW RTLD_LAZY
#endif

/*
 * OpenPAM internal
 *
 * Perform sanity checks and attempt to load a module
 */

#ifdef HAVE_FDLOPEN
static void *
try_dlopen(const char *modfn)
{
	void *dlh;
	int fd;

	openpam_log(PAM_LOG_LIBDEBUG, "dlopen(%s)", modfn);
	if ((fd = open(modfn, O_RDONLY)) < 0) {
		if (errno != ENOENT)
			openpam_log(PAM_LOG_ERROR, "%s: %m", modfn);
		return (NULL);
	}
	if (OPENPAM_FEATURE(VERIFY_MODULE_FILE) &&
	    openpam_check_desc_owner_perms(modfn, fd) != 0) {
		close(fd);
		return (NULL);
	}
	if ((dlh = fdlopen(fd, RTLD_NOW)) == NULL) {
		openpam_log(PAM_LOG_ERROR, "%s: %s", modfn, dlerror());
		close(fd);
		errno = 0;
		return (NULL);
	}
	close(fd);
	return (dlh);
}
#else
static void *
try_dlopen(const char *modfn)
{
	int check_module_file;
	void *dlh;

	openpam_log(PAM_LOG_LIBDEBUG, "dlopen(%s)", modfn);
	openpam_get_feature(OPENPAM_VERIFY_MODULE_FILE,
	    &check_module_file);
	if (check_module_file &&
	    openpam_check_path_owner_perms(modfn) != 0)
		return (NULL);
	if ((dlh = dlopen(modfn, RTLD_NOW)) == NULL) {
		openpam_log(PAM_LOG_ERROR, "%s: %s", modfn, dlerror());
		errno = 0;
		return (NULL);
	}
	return (dlh);
}
#endif

/*
 * Try to load a module from the suggested location.
 */
static pam_module_t *
try_module(const char *modpath)
{
	const pam_module_t *dlmodule;
	pam_module_t *module;
	int i, serrno;

	if ((module = calloc(1, sizeof *module)) == NULL ||
	    (module->path = strdup(modpath)) == NULL ||
	    (module->dlh = try_dlopen(modpath)) == NULL)
		goto err;
	dlmodule = dlsym(module->dlh, "_pam_module");
	for (i = 0; i < PAM_NUM_PRIMITIVES; ++i) {
		if (dlmodule) {
			module->func[i] = dlmodule->func[i];
		} else {
			module->func[i] = (pam_func_t)dlfunc(module->dlh,
			    pam_sm_func_name[i]);
			/*
			 * This openpam_log() call is a major source of
			 * log spam, and the cases that matter are caught
			 * and logged in openpam_dispatch().  This would
			 * be less problematic if dlerror() returned an
			 * error code so we could log an error only when
			 * dlfunc() failed for a reason other than "no
			 * such symbol".
			 */
#if 0
			if (module->func[i] == NULL)
				openpam_log(PAM_LOG_LIBDEBUG, "%s: %s(): %s",
				    modpath, pam_sm_func_name[i], dlerror());
#endif
		}
	}
	return (module);
err:
	serrno = errno;
	if (module != NULL) {
		if (module->dlh != NULL)
			dlclose(module->dlh);
		if (module->path != NULL)
			FREE(module->path);
		FREE(module);
	}
	errno = serrno;
	if (serrno != 0 && serrno != ENOENT)
		openpam_log(PAM_LOG_ERROR, "%s: %m", modpath);
	errno = serrno;
	return (NULL);
}

/*
 * OpenPAM internal
 *
 * Locate a dynamically linked module
 */

pam_module_t *
openpam_dynamic(const char *modname)
{
	pam_module_t *module;
	char modpath[PATH_MAX];
	const char **path, *p;
	int has_so, has_ver;
	int dot, len;

	/*
	 * Simple case: module name contains path separator(s)
	 */
	if (strchr(modname, '/') != NULL) {
		/*
		 * Absolute paths are not allowed if RESTRICT_MODULE_NAME
		 * is in effect (default off).  Relative paths are never
		 * allowed.
		 */
		if (OPENPAM_FEATURE(RESTRICT_MODULE_NAME) ||
		    modname[0] != '/') {
			openpam_log(PAM_LOG_ERROR,
			    "invalid module name: %s", modname);
			return (NULL);
		}
		return (try_module(modname));
	}

	/*
	 * Check for .so and version sufixes
	 */
	p = strchr(modname, '\0');
	has_ver = has_so = 0;
	while (is_digit(*p))
		--p;
	if (*p == '.' && *++p != '\0') {
		/* found a numeric suffix */
		has_ver = 1;
		/* assume that .so is either present or unneeded */
		has_so = 1;
	} else if (*p == '\0' && p >= modname + sizeof PAM_SOEXT &&
	    strcmp(p - sizeof PAM_SOEXT + 1, PAM_SOEXT) == 0) {
		/* found .so suffix */
		has_so = 1;
	}

	/*
	 * Complicated case: search for the module in the usual places.
	 */
	for (path = openpam_module_path; *path != NULL; ++path) {
		/*
		 * Assemble the full path, including the version suffix.  Take
		 * note of where the suffix begins so we can cut it off later.
		 */
		if (has_ver)
			len = snprintf(modpath, sizeof modpath, "%s/%s%n",
			    *path, modname, &dot);
		else if (has_so)
			len = snprintf(modpath, sizeof modpath, "%s/%s%n.%d",
			    *path, modname, &dot, LIB_MAJ);
		else
			len = snprintf(modpath, sizeof modpath, "%s/%s%s%n.%d",
			    *path, modname, PAM_SOEXT, &dot, LIB_MAJ);
		/* check for overflow */
		if (len < 0 || (unsigned int)len >= sizeof modpath) {
			errno = ENOENT;
			continue;
		}
		/* try the versioned path */
		if ((module = try_module(modpath)) != NULL)
			return (module);
		if (errno == ENOENT && modpath[dot] != '\0') {
			/* no luck, try the unversioned path */
			modpath[dot] = '\0';
			if ((module = try_module(modpath)) != NULL)
				return (module);
		}
	}

	/* :( */
	return (NULL);
}

/*
 * NOPARSE
 */
