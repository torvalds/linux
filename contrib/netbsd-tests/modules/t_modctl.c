/*	$NetBSD: t_modctl.c,v 1.12 2012/08/20 08:07:52 martin Exp $	*/
/*
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: t_modctl.c,v 1.12 2012/08/20 08:07:52 martin Exp $");

#include <sys/module.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <prop/proplib.h>

#include <atf-c.h>

enum presence_check { both_checks, stat_check, sysctl_check };

static void	check_permission(void);
static bool	get_modstat_info(const char *, modstat_t *);
static bool	get_sysctl(const char *, void *buf, const size_t);
static bool	k_helper_is_present_stat(void);
static bool	k_helper_is_present_sysctl(void);
static bool	k_helper_is_present(enum presence_check);
static int	load(prop_dictionary_t, bool, const char *, ...);
static int	unload(const char *, bool);
static void	unload_cleanup(const char *);

/* --------------------------------------------------------------------- */
/* Auxiliary functions                                                   */
/* --------------------------------------------------------------------- */

/*
 * A function checking wether we are allowed to load modules currently
 * (either the kernel is not modular, or securelevel may prevent it)
 */
static void
check_permission(void)
{
	int err;

	err = modctl(MODCTL_EXISTS, 0);
	if (err == 0) return;
	if (errno == ENOSYS)
		atf_tc_skip("Kernel does not have 'options MODULAR'.");
	else if (errno == EPERM)
		atf_tc_skip("Module loading administratively forbidden");
	ATF_REQUIRE_EQ_MSG(errno, 0, "unexpected error %d from "
	    "modctl(MODCTL_EXISTS, 0)", errno);
}

static bool
get_modstat_info(const char *name, modstat_t *msdest)
{
	bool found;
	size_t len;
	struct iovec iov;
	modstat_t *ms;

	check_permission();
	for (len = 4096; ;) {
		iov.iov_base = malloc(len);
		iov.iov_len = len;

		errno = 0;

		if (modctl(MODCTL_STAT, &iov) != 0) {
			int err = errno;
			fprintf(stderr, "modctl(MODCTL_STAT) failed: %s\n",
			    strerror(err));
			atf_tc_fail("Failed to query module status");
		}
		if (len >= iov.iov_len)
			break;
		free(iov.iov_base);
		len = iov.iov_len;
	}

	found = false;
	len = iov.iov_len / sizeof(modstat_t);
	for (ms = (modstat_t *)iov.iov_base; len != 0 && !found;
	    ms++, len--) {
		if (strcmp(ms->ms_name, name) == 0) {
			if (msdest != NULL)
				*msdest = *ms;
			found = true;
		}
	}

	free(iov.iov_base);

	return found;
}

/*
 * Queries a sysctl property.
 */
static bool
get_sysctl(const char *name, void *buf, const size_t len)
{
	size_t len2 = len;
	printf("Querying sysctl variable: %s\n", name);
	int ret = sysctlbyname(name, buf, &len2, NULL, 0);
	if (ret == -1 && errno != ENOENT) {
		fprintf(stderr, "sysctlbyname(2) failed: %s\n",
		    strerror(errno));
		atf_tc_fail("Failed to query %s", name);
	}
	return ret != -1;
}

/*
 * Returns a boolean indicating if the k_helper module was loaded
 * successfully.  This implementation uses modctl(2)'s MODCTL_STAT
 * subcommand to do the check.
 */
static bool
k_helper_is_present_stat(void)
{

	return get_modstat_info("k_helper", NULL);
}

/*
 * Returns a boolean indicating if the k_helper module was loaded
 * successfully.  This implementation uses the module's sysctl
 * installed node to do the check.
 */
static bool
k_helper_is_present_sysctl(void)
{
	size_t present;

	return get_sysctl("vendor.k_helper.present", &present,
	    sizeof(present));
}

/*
 * Returns a boolean indicating if the k_helper module was loaded
 * successfully.  The 'how' parameter specifies the implementation to
 * use to do the check.
 */
static bool
k_helper_is_present(enum presence_check how)
{
	bool found;

	switch (how) {
	case both_checks:
		found = k_helper_is_present_stat();
		ATF_CHECK(k_helper_is_present_sysctl() == found);
		break;

	case stat_check:
		found = k_helper_is_present_stat();
		break;

	case sysctl_check:
		found = k_helper_is_present_sysctl();
		break;

	default:
		found = false;
		assert(found);
	}

	return found;
}

/*
 * Loads the specified module from a file.  If fatal is set and an error
 * occurs when loading the module, an error message is printed and the
 * test case is aborted.
 */
static __printflike(3, 4) int
load(prop_dictionary_t props, bool fatal, const char *fmt, ...)
{
	int err;
	va_list ap;
	char filename[MAXPATHLEN], *propsstr;
	modctl_load_t ml;

	check_permission();
	if (props == NULL) {
		props = prop_dictionary_create();
		propsstr = prop_dictionary_externalize(props);
		ATF_CHECK(propsstr != NULL);
		prop_object_release(props);
	} else {
		propsstr = prop_dictionary_externalize(props);
		ATF_CHECK(propsstr != NULL);
	}

	va_start(ap, fmt);
	vsnprintf(filename, sizeof(filename), fmt, ap);
	va_end(ap);

	ml.ml_filename = filename;
	ml.ml_flags = 0;
	ml.ml_props = propsstr;
	ml.ml_propslen = strlen(propsstr);

	printf("Loading module %s\n", filename);
	errno = err = 0;

	if (modctl(MODCTL_LOAD, &ml) == -1) {
		err = errno;
		fprintf(stderr, "modctl(MODCTL_LOAD, %s), failed: %s\n",
		    filename, strerror(err));
		if (fatal)
			atf_tc_fail("Module load failed");
	}

	free(propsstr);

	return err;
}

/*
 * Unloads the specified module.  If silent is true, nothing will be
 * printed and no errors will be raised if the unload was unsuccessful.
 */
static int
unload(const char *name, bool fatal)
{
	int err;

	check_permission();
	printf("Unloading module %s\n", name);
	errno = err = 0;

	if (modctl(MODCTL_UNLOAD, __UNCONST(name)) == -1) {
		err = errno;
		fprintf(stderr, "modctl(MODCTL_UNLOAD, %s) failed: %s\n",
		    name, strerror(err));
		if (fatal)
			atf_tc_fail("Module unload failed");
	}
	return err;
}

/*
 * A silent version of unload, to be called as part of the cleanup
 * process only.
 */
static void
unload_cleanup(const char *name)
{

	(void)modctl(MODCTL_UNLOAD, __UNCONST(name));
}

/* --------------------------------------------------------------------- */
/* Test cases                                                            */
/* --------------------------------------------------------------------- */

ATF_TC_WITH_CLEANUP(cmd_load);
ATF_TC_HEAD(cmd_load, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests for the MODCTL_LOAD command");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(cmd_load, tc)
{
	char longname[MAXPATHLEN];
	size_t i;

	ATF_CHECK(load(NULL, false, " ") == ENOENT);
	ATF_CHECK(load(NULL, false, "non-existent.o") == ENOENT);

	for (i = 0; i < MAXPATHLEN - 1; i++)
		longname[i] = 'a';
	longname[MAXPATHLEN - 1] = '\0';
	ATF_CHECK(load(NULL, false, "%s", longname) == ENAMETOOLONG);

	ATF_CHECK(!k_helper_is_present(stat_check));
	load(NULL, true, "%s/k_helper/k_helper.kmod",
	    atf_tc_get_config_var(tc, "srcdir"));
	printf("Checking if load was successful\n");
	ATF_CHECK(k_helper_is_present(stat_check));
}
ATF_TC_CLEANUP(cmd_load, tc)
{
	unload_cleanup("k_helper");
}

ATF_TC_WITH_CLEANUP(cmd_load_props);
ATF_TC_HEAD(cmd_load_props, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests for the MODCTL_LOAD command, "
	    "providing extra load-time properties");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(cmd_load_props, tc)
{
	prop_dictionary_t props;

	printf("Loading module without properties\n");
	props = prop_dictionary_create();
	load(props, true, "%s/k_helper/k_helper.kmod",
	    atf_tc_get_config_var(tc, "srcdir"));
	prop_object_release(props);
	{
		int ok;
		ATF_CHECK(get_sysctl("vendor.k_helper.prop_str_ok",
		    &ok, sizeof(ok)));
		ATF_CHECK(!ok);
	}
	unload("k_helper", true);

	printf("Loading module with a string property\n");
	props = prop_dictionary_create();
	prop_dictionary_set(props, "prop_str",
	    prop_string_create_cstring("1st string"));
	load(props, true, "%s/k_helper/k_helper.kmod",
	    atf_tc_get_config_var(tc, "srcdir"));
	prop_object_release(props);
	{
		int ok;
		ATF_CHECK(get_sysctl("vendor.k_helper.prop_str_ok",
		    &ok, sizeof(ok)));
		ATF_CHECK(ok);

		char val[128];
		ATF_CHECK(get_sysctl("vendor.k_helper.prop_str_val",
		    &val, sizeof(val)));
		ATF_CHECK(strcmp(val, "1st string") == 0);
	}
	unload("k_helper", true);

	printf("Loading module with a different string property\n");
	props = prop_dictionary_create();
	prop_dictionary_set(props, "prop_str",
	    prop_string_create_cstring("2nd string"));
	load(props, true, "%s/k_helper/k_helper.kmod",
	    atf_tc_get_config_var(tc, "srcdir"));
	prop_object_release(props);
	{
		int ok;
		ATF_CHECK(get_sysctl("vendor.k_helper.prop_str_ok",
		    &ok, sizeof(ok)));
		ATF_CHECK(ok);

		char val[128];
		ATF_CHECK(get_sysctl("vendor.k_helper.prop_str_val",
		    &val, sizeof(val)));
		ATF_CHECK(strcmp(val, "2nd string") == 0);
	}
	unload("k_helper", true);
}
ATF_TC_CLEANUP(cmd_load_props, tc)
{
	unload_cleanup("k_helper");
}

ATF_TC_WITH_CLEANUP(cmd_load_recurse);
ATF_TC_HEAD(cmd_load_recurse, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests for the MODCTL_LOAD command, "
	    "with recursive module_load()");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(cmd_load_recurse, tc)
{
	prop_dictionary_t props;
	char filename[MAXPATHLEN];

	printf("Loading module with request to load another module\n");
	props = prop_dictionary_create();
	snprintf(filename, sizeof(filename), "%s/k_helper2/k_helper2.kmod",
	    atf_tc_get_config_var(tc, "srcdir"));
	prop_dictionary_set(props, "prop_recurse",
	    prop_string_create_cstring(filename));
	load(props, true, "%s/k_helper/k_helper.kmod",
	    atf_tc_get_config_var(tc, "srcdir"));
	{
		int ok;
		ATF_CHECK(get_sysctl("vendor.k_helper.prop_int_load",
		    &ok, sizeof(ok)));
		ATF_CHECK(ok == 0);
		ATF_CHECK(get_sysctl("vendor.k_helper2.present",
		    &ok, sizeof(ok)));
		ATF_CHECK(ok);
	}
	unload("k_helper", true);
	unload("k_helper2", true);
}
ATF_TC_CLEANUP(cmd_load_recurse, tc)
{
	unload_cleanup("k_helper");
	unload_cleanup("k_helper2");
}

ATF_TC_WITH_CLEANUP(cmd_stat);
ATF_TC_HEAD(cmd_stat, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests for the MODCTL_STAT command");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(cmd_stat, tc)
{
	ATF_CHECK(!k_helper_is_present(both_checks));

	load(NULL, true, "%s/k_helper/k_helper.kmod",
	    atf_tc_get_config_var(tc, "srcdir"));
	ATF_CHECK(k_helper_is_present(both_checks));
	{
		modstat_t ms;
		ATF_CHECK(get_modstat_info("k_helper", &ms));

		ATF_CHECK(ms.ms_class == MODULE_CLASS_MISC);
		ATF_CHECK(ms.ms_source == MODULE_SOURCE_FILESYS);
		ATF_CHECK(ms.ms_refcnt == 0);
	}
	unload("k_helper", true);

	ATF_CHECK(!k_helper_is_present(both_checks));
}
ATF_TC_CLEANUP(cmd_stat, tc)
{
	unload_cleanup("k_helper");
}

ATF_TC_WITH_CLEANUP(cmd_unload);
ATF_TC_HEAD(cmd_unload, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests for the MODCTL_UNLOAD command");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(cmd_unload, tc)
{
	load(NULL, true, "%s/k_helper/k_helper.kmod",
	    atf_tc_get_config_var(tc, "srcdir"));

	ATF_CHECK(unload("", false) == ENOENT);
	ATF_CHECK(unload("non-existent.kmod", false) == ENOENT);
	ATF_CHECK(unload("k_helper.kmod", false) == ENOENT);

	ATF_CHECK(k_helper_is_present(stat_check));
	unload("k_helper", true);
	printf("Checking if unload was successful\n");
	ATF_CHECK(!k_helper_is_present(stat_check));
}
ATF_TC_CLEANUP(cmd_unload, tc)
{
	unload_cleanup("k_helper");
}

/* --------------------------------------------------------------------- */
/* Main                                                                  */
/* --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, cmd_load);
	ATF_TP_ADD_TC(tp, cmd_load_props);
	ATF_TP_ADD_TC(tp, cmd_stat);
	ATF_TP_ADD_TC(tp, cmd_load_recurse);
	ATF_TP_ADD_TC(tp, cmd_unload);

	return atf_no_error();
}
