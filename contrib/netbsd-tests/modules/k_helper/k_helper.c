/*	$NetBSD: k_helper.c,v 1.6 2012/06/03 10:59:44 dsl Exp $	*/
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
__KERNEL_RCSID(0, "$NetBSD: k_helper.c,v 1.6 2012/06/03 10:59:44 dsl Exp $");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <prop/proplib.h>

MODULE(MODULE_CLASS_MISC, k_helper, NULL);

/* --------------------------------------------------------------------- */
/* Sysctl interface to query information about the module.               */
/* --------------------------------------------------------------------- */

/* TODO: Change the integer variables below that represent booleans to
 * bools, once sysctl(8) supports CTLTYPE_BOOL nodes. */

static struct sysctllog *clogp;
static int present = 1;
static int prop_str_ok;
static char prop_str_val[128];
static int prop_int_ok;
static int64_t prop_int_val;
static int prop_int_load;

#define K_HELPER 0x12345678
#define K_HELPER_PRESENT 0
#define K_HELPER_PROP_STR_OK 1
#define K_HELPER_PROP_STR_VAL 2
#define K_HELPER_PROP_INT_OK 3
#define K_HELPER_PROP_INT_VAL 4
#define K_HELPER_PROP_INT_LOAD 5

SYSCTL_SETUP(sysctl_k_helper_setup, "sysctl k_helper subtree setup")
{

	sysctl_createv(clog, 0, NULL, NULL,
	               CTLFLAG_PERMANENT,
	               CTLTYPE_NODE, "k_helper", NULL,
	               NULL, 0, NULL, 0,
	               CTL_VENDOR, K_HELPER, CTL_EOL);

	sysctl_createv(clog, 0, NULL, NULL,
	               CTLFLAG_PERMANENT,
	               CTLTYPE_INT, "present",
		       SYSCTL_DESCR("Whether the module was loaded or not"),
		       NULL, 0, &present, 0,
	               CTL_VENDOR, K_HELPER, K_HELPER_PRESENT, CTL_EOL);

	sysctl_createv(clog, 0, NULL, NULL,
	               CTLFLAG_PERMANENT,
	               CTLTYPE_INT, "prop_str_ok",
		       SYSCTL_DESCR("String property's validity"),
		       NULL, 0, &prop_str_ok, 0,
	               CTL_VENDOR, K_HELPER, K_HELPER_PROP_STR_OK, CTL_EOL);

	sysctl_createv(clog, 0, NULL, NULL,
	               CTLFLAG_PERMANENT,
	               CTLTYPE_STRING, "prop_str_val",
		       SYSCTL_DESCR("String property's value"),
		       NULL, 0, prop_str_val, 0,
	               CTL_VENDOR, K_HELPER, K_HELPER_PROP_STR_VAL, CTL_EOL);

	sysctl_createv(clog, 0, NULL, NULL,
	               CTLFLAG_PERMANENT,
	               CTLTYPE_INT, "prop_int_ok",
		       SYSCTL_DESCR("String property's validity"),
		       NULL, 0, &prop_int_ok, 0,
	               CTL_VENDOR, K_HELPER, K_HELPER_PROP_INT_OK, CTL_EOL);

	sysctl_createv(clog, 0, NULL, NULL,
	               CTLFLAG_PERMANENT,
	               CTLTYPE_QUAD, "prop_int_val",
		       SYSCTL_DESCR("String property's value"),
		       NULL, 0, &prop_int_val, 0,
	               CTL_VENDOR, K_HELPER, K_HELPER_PROP_INT_VAL, CTL_EOL);

	sysctl_createv(clog, 0, NULL, NULL,
	               CTLFLAG_PERMANENT,
	               CTLTYPE_INT, "prop_int_load",
		       SYSCTL_DESCR("Status of recursive modload"),
		       NULL, 0, &prop_int_load, 0,
	               CTL_VENDOR, K_HELPER, K_HELPER_PROP_INT_LOAD, CTL_EOL);
}

/* --------------------------------------------------------------------- */
/* Module management.                                                    */
/* --------------------------------------------------------------------- */

static
int
k_helper_init(prop_dictionary_t props)
{
	prop_object_t p;

	p = prop_dictionary_get(props, "prop_str");
	if (p == NULL)
		prop_str_ok = 0;
	else if (prop_object_type(p) != PROP_TYPE_STRING)
		prop_str_ok = 0;
	else {
		const char *msg = prop_string_cstring_nocopy(p);
		if (msg == NULL)
			prop_str_ok = 0;
		else {
			strlcpy(prop_str_val, msg, sizeof(prop_str_val));
			prop_str_ok = 1;
		}
	}
	if (!prop_str_ok)
		strlcpy(prop_str_val, "", sizeof(prop_str_val));

	p = prop_dictionary_get(props, "prop_int");
	if (p == NULL)
		prop_int_ok = 0;
	else if (prop_object_type(p) != PROP_TYPE_NUMBER)
		prop_int_ok = 0;
	else {
		prop_int_val = prop_number_integer_value(p);
		prop_int_ok = 1;
	}
	if (!prop_int_ok)
		prop_int_val = -1;

	p = prop_dictionary_get(props, "prop_recurse");
	if (p != NULL && prop_object_type(p) == PROP_TYPE_STRING) {
		const char *recurse_name = prop_string_cstring_nocopy(p);
		if (recurse_name != NULL)
			prop_int_load = module_load(recurse_name,
			    MODCTL_NO_PROP, NULL, MODULE_CLASS_ANY);
		else
			prop_int_load = -1;
	} else
		prop_int_load = -2;

	sysctl_k_helper_setup(&clogp);

	return 0;
}

static
int
k_helper_fini(void *arg)
{

	sysctl_teardown(&clogp);

	return 0;
}

static
int
k_helper_modcmd(modcmd_t cmd, void *arg)
{
	int ret;

	switch (cmd) {
	case MODULE_CMD_INIT:
		ret = k_helper_init(arg);
		break;

	case MODULE_CMD_FINI:
		ret = k_helper_fini(arg);
		break;

	case MODULE_CMD_STAT:
	default:
		ret = ENOTTY;
	}

	return ret;
}
