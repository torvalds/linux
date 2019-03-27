/*-
 * Copyright (c) 2006 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

/*
 * Configuration file parser for auditfilterd.  The configuration file is a
 * very simple format, similar to other BSM configuration files, consisting
 * of configuration entries of one line each.  The configuration function is
 * aware of previous runs, and will update the current configuration as
 * needed.
 *
 * Modules are in one of two states: attached, or detached.  If attach fails,
 * detach is not called because it was not attached.  If a module is attached
 * and a call to its reinit method fails, we will detach it.
 *
 * Modules are passed a (void *) reference to their configuration state so
 * that they may pass this into any common APIs we provide which may rely on
 * that state.  Currently, the only such API is the cookie API, which allows
 * per-instance state to be maintained by a module.  In the future, this will
 * also be used to support per-instance preselection state.
 */

#include <sys/types.h>

#include <config/config.h>
#ifdef HAVE_FULL_QUEUE_H
#include <sys/queue.h>
#else
#include <compat/queue.h>
#endif

#include <bsm/libbsm.h>
#include <bsm/audit_filter.h>

#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "auditfilterd.h"

/*
 * Free an individual auditfilter_module structure.  Will not shut down the
 * module, just frees the memory.  Does so conditional on pointers being
 * non-NULL so that it can be used on partially allocated structures.
 */
static void
auditfilter_module_free(struct auditfilter_module *am)
{

	if (am->am_modulename != NULL)
		free(am->am_modulename);
	if (am->am_arg_buffer != NULL)
		free(am->am_arg_buffer);
	if (am->am_argv != NULL)
		free(am->am_argv);
}

/*
 * Free all memory associated with an auditfilter_module list.  Does not
 * dlclose() or shut down the modules, just free the memory.  Use
 * auditfilter_module_list_detach() for that, if required.
 */
static void
auditfilter_module_list_free(struct auditfilter_module_list *list)
{
	struct auditfilter_module *am;

	while (!(TAILQ_EMPTY(list))) {
		am = TAILQ_FIRST(list);
		TAILQ_REMOVE(list, am, am_list);
		auditfilter_module_free(am);
	}
}

/*
 * Detach an attached module from an auditfilter_module structure.  Does not
 * free the data structure itself.
 */
static void
auditfilter_module_detach(struct auditfilter_module *am)
{

	if (am->am_detach != NULL)
		am->am_detach(am);
	am->am_cookie = NULL;
	(void)dlclose(am->am_dlhandle);
	am->am_dlhandle = NULL;
}

/*
 * Walk an auditfilter_module list, detaching each module.  Intended to be
 * combined with auditfilter_module_list_free().
 */
static void
auditfilter_module_list_detach(struct auditfilter_module_list *list)
{
	struct auditfilter_module *am;

	TAILQ_FOREACH(am, list, am_list)
		auditfilter_module_detach(am);
}

/*
 * Given a filled out auditfilter_module, use dlopen() and dlsym() to attach
 * the module.  If we fail, leave fields in the state we found them.
 *
 * XXXRW: Need a better way to report errors.
 */
static int
auditfilter_module_attach(struct auditfilter_module *am)
{

	am->am_dlhandle = dlopen(am->am_modulename, RTLD_NOW);
	if (am->am_dlhandle == NULL) {
		warnx("auditfilter_module_attach: %s: %s", am->am_modulename,
		    dlerror());
		return (-1);
	}

	/*
	 * Not implementing these is not considered a failure condition,
	 * although we might want to consider warning if obvious stuff is
	 * not implemented, such as am_record.
	 */
	am->am_attach = dlsym(am->am_dlhandle, AUDIT_FILTER_ATTACH_STRING);
	am->am_reinit = dlsym(am->am_dlhandle, AUDIT_FILTER_REINIT_STRING);
	am->am_record = dlsym(am->am_dlhandle, AUDIT_FILTER_RECORD_STRING);
	am->am_rawrecord = dlsym(am->am_dlhandle,
	    AUDIT_FILTER_RAWRECORD_STRING);
	am->am_detach = dlsym(am->am_dlhandle, AUDIT_FILTER_DETACH_STRING);

	if (am->am_attach != NULL) {
		if (am->am_attach(am, am->am_argc, am->am_argv)
		    != AUDIT_FILTER_SUCCESS) {
			warnx("auditfilter_module_attach: %s: failed",
			    am->am_modulename);
			dlclose(am->am_dlhandle);
			am->am_dlhandle = NULL;
			am->am_cookie = NULL;
			am->am_attach = NULL;
			am->am_reinit = NULL;
			am->am_record = NULL;
			am->am_rawrecord = NULL;
			am->am_detach = NULL;
			return (-1);
		}
	}

	return (0);
}

/*
 * When the arguments for a module are changed, we notify the module through
 * a call to its reinit method, if any.  Return 0 on success, or -1 on
 * failure.
 */
static int
auditfilter_module_reinit(struct auditfilter_module *am)
{

	if (am->am_reinit == NULL)
		return (0);

	if (am->am_reinit(am, am->am_argc, am->am_argv) !=
	    AUDIT_FILTER_SUCCESS) {
		warnx("auditfilter_module_reinit: %s: failed",
		    am->am_modulename);
		return (-1);
	}

	return (0);
}

/*
 * Given a configuration line, generate an auditfilter_module structure that
 * describes it; caller will not pass comments in, so they are not looked
 * for.  Do not attempt to instantiate it.  Will destroy the contents of
 * 'buffer'.
 *
 * Configuration lines consist of two parts: the module name and arguments
 * separated by a ':', and then a ','-delimited list of arguments.
 *
 * XXXRW: Need to decide where to send the warning output -- stderr for now.
 */
struct auditfilter_module *
auditfilter_module_parse(const char *filename, int linenumber, char *buffer)
{
	char *arguments, *module, **ap;
	struct auditfilter_module *am;

	am = malloc(sizeof(*am));
	if (am == NULL) {
		warn("auditfilter_module_parse: %s:%d", filename, linenumber);
		return (NULL);
	}
	bzero(am, sizeof(*am));

	/*
	 * First, break out the module and arguments strings.  We look for
	 * one extra argument to make sure there are no more :'s in the line.
	 * That way, we prevent modules from using argument strings that, in
	 * the future, may cause problems for adding additional columns.
	 */
	arguments = buffer;
	module = strsep(&arguments, ":");
	if (module == NULL || arguments == NULL) {
		warnx("auditfilter_module_parse: %s:%d: parse error",
		    filename, linenumber);
		return (NULL);
	}

	am->am_modulename = strdup(module);
	if (am->am_modulename == NULL) {
		warn("auditfilter_module_parse: %s:%d", filename, linenumber);
		auditfilter_module_free(am);
		return (NULL);
	}

	am->am_arg_buffer = strdup(buffer);
	if (am->am_arg_buffer == NULL) {
		warn("auditfilter_module_parse: %s:%d", filename, linenumber);
		auditfilter_module_free(am);
		return (NULL);
	}

	/*
	 * Now, break out the arguments string into a series of arguments.
	 * This is a bit more complicated, and requires cleanup if things go
	 * wrong.
	 */
	am->am_argv = malloc(sizeof(char *) * AUDITFILTERD_CONF_MAXARGS);
	if (am->am_argv == NULL) {
		warn("auditfilter_module_parse: %s:%d", filename, linenumber);
		auditfilter_module_free(am);
		return (NULL);
	}
	bzero(am->am_argv, sizeof(char *) * AUDITFILTERD_CONF_MAXARGS);
	am->am_argc = 0;
	for (ap = am->am_argv; (*ap = strsep(&arguments, " \t")) != NULL;) {
		if (**ap != '\0') {
			am->am_argc++;
			if (++ap >= &am->am_argv[AUDITFILTERD_CONF_MAXARGS])
				break;
		}
	}
	if (ap >= &am->am_argv[AUDITFILTERD_CONF_MAXARGS]) {
		warnx("auditfilter_module_parse: %s:%d: too many arguments",
		    filename, linenumber);
		auditfilter_module_free(am);
		return (NULL);
	}

	return (am);
}

/*
 * Read a configuration file, and populate 'list' with the configuration
 * lines.  Does not attempt to instantiate the configuration, just read it
 * into a useful set of data structures.
 */
static int
auditfilterd_conf_read(const char *filename, FILE *fp,
    struct auditfilter_module_list *list)
{
	int error, linenumber, syntaxerror;
	struct auditfilter_module *am;
	char buffer[LINE_MAX];

	syntaxerror = 0;
	linenumber = 0;
	while (!feof(fp) && !ferror(fp)) {
		if (fgets(buffer, LINE_MAX, fp) == NULL)
			break;
		linenumber++;
		if (buffer[0] == '#' || strlen(buffer) < 1)
			continue;
		buffer[strlen(buffer)-1] = '\0';
		am = auditfilter_module_parse(filename, linenumber, buffer);
		if (am == NULL) {
			syntaxerror = 1;
			break;
		}
		TAILQ_INSERT_HEAD(list, am, am_list);
	}

	/*
	 * File I/O error.
	 */
	if (ferror(fp)) {
		error = errno;
		auditfilter_module_list_free(list);
		errno = error;
		return (-1);
	}

	/*
	 * Syntax error.
	 */
	if (syntaxerror) {
		auditfilter_module_list_free(list);
		errno = EINVAL;
		return (-1);
	}
	return (0);
}

/*
 * Apply changes necessary to bring a new configuration into force.  The new
 * configuration data is passed in, and the current configuration is updated
 * to match it.  The contents of 'list' are freed or otherwise disposed of
 * before return.
 *
 * The algorithms here are not very efficient, but this is an infrequent
 * operation on very short lists.
 */
static void
auditfilterd_conf_apply(struct auditfilter_module_list *list)
{
	struct auditfilter_module *am1, *am2, *am_tmp;
	int argc_tmp, found;
	char **argv_tmp;

	/*
	 * First, remove remove and detach any entries that appear in the
	 * current configuration, but not the new configuration.
	 */
	TAILQ_FOREACH_SAFE(am1, &filter_list, am_list, am_tmp) {
		found = 0;
		TAILQ_FOREACH(am2, list, am_list) {
			if (strcmp(am1->am_modulename, am2->am_modulename)
			    == 0) {
				found = 1;
				break;
			}
		}
		if (found)
			continue;

		/*
		 * am1 appears in filter_list, but not the new list, detach
		 * and free the module.
		 */
		warnx("detaching module %s", am1->am_modulename);
		TAILQ_REMOVE(&filter_list, am1, am_list);
		auditfilter_module_detach(am1);
		auditfilter_module_free(am1);
	}

	/*
	 * Next, update the configuration of any modules that appear in both
	 * lists.  We do this by swapping the two argc and argv values and
	 * freeing the new one, rather than detaching the old one and
	 * attaching the new one.  That way module state is preserved.
	 */
	TAILQ_FOREACH(am1, &filter_list, am_list) {
		found = 0;
		TAILQ_FOREACH(am2, list, am_list) {
			if (strcmp(am1->am_modulename, am2->am_modulename)
			    == 0) {
				found = 1;
				break;
			}
		}
		if (!found)
			continue;

		/*
		 * Swap the arguments.
		 */
		argc_tmp = am1->am_argc;
		argv_tmp = am1->am_argv;
		am1->am_argc = am2->am_argc;
		am1->am_argv = am2->am_argv;
		am2->am_argc = argc_tmp;
		am2->am_argv = argv_tmp;

		/*
		 * The reinit is a bit tricky: if reinit fails, we actually
		 * remove the old entry and detach that, as we don't allow
		 * running modules to be out of sync with the configuration
		 * file.
		 */
		warnx("reiniting module %s", am1->am_modulename);
		if (auditfilter_module_reinit(am1) != 0) {
			warnx("reinit failed for module %s, detaching",
			    am1->am_modulename);
			TAILQ_REMOVE(&filter_list, am1, am_list);
			auditfilter_module_detach(am1);
			auditfilter_module_free(am1);
		}

		/*
		 * Free the entry from the new list, which will discard the
		 * old arguments.  No need to detach, as it was never
		 * attached in the first place.
		 */
		TAILQ_REMOVE(list, am2, am_list);
		auditfilter_module_free(am2);
	}

	/*
	 * Finally, attach any new entries that don't appear in the old
	 * configuration, and if they attach successfully, move them to the
	 * real configuration list.
	 */
	TAILQ_FOREACH(am1, list, am_list) {
		found = 0;
		TAILQ_FOREACH(am2, &filter_list, am_list) {
			if (strcmp(am1->am_modulename, am2->am_modulename)
			    == 0) {
				found = 1;
				break;
			}
		}
		if (found)
			continue;
		/*
		 * Attach the entry.  If it succeeds, add to filter_list,
		 * otherwise, free.  No need to detach if attach failed.
		 */
		warnx("attaching module %s", am1->am_modulename);
		TAILQ_REMOVE(list, am1, am_list);
		if (auditfilter_module_attach(am1) != 0) {
			warnx("attaching module %s failed",
			    am1->am_modulename);
			auditfilter_module_free(am1);
		} else
			TAILQ_INSERT_HEAD(&filter_list, am1, am_list);
	}

	if (TAILQ_FIRST(list) != NULL)
		warnx("auditfilterd_conf_apply: new list not empty\n");
}

/*
 * Read the new configuration file into a local list.  If the configuration
 * file is parsed OK, then apply the changes.
 */
int
auditfilterd_conf(const char *filename, FILE *fp)
{
	struct auditfilter_module_list list;

	TAILQ_INIT(&list);
	if (auditfilterd_conf_read(filename, fp, &list) < 0)
		return (-1);

	auditfilterd_conf_apply(&list);

	return (0);
}

/*
 * Detach and free all active filter modules for daemon shutdown.
 */
void
auditfilterd_conf_shutdown(void)
{

	auditfilter_module_list_detach(&filter_list);
	auditfilter_module_list_free(&filter_list);
}

/*
 * APIs to allow modules to query and set their per-instance cookie.
 */
void
audit_filter_getcookie(void *instance, void **cookie)
{
	struct auditfilter_module *am;

	am = (struct auditfilter_module *)instance;
	*cookie = am->am_cookie;
}

void
audit_filter_setcookie(void *instance, void *cookie)
{
	struct auditfilter_module *am;

	am = (struct auditfilter_module *)instance;
	am->am_cookie = cookie;
}
