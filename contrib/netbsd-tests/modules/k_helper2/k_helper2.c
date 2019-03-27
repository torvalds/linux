/*	$NetBSD: k_helper2.c,v 1.2 2010/11/03 16:10:23 christos Exp $ */
/*
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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
__KERNEL_RCSID(0, "$NetBSD: k_helper2.c,v 1.2 2010/11/03 16:10:23 christos Exp $");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <prop/proplib.h>

MODULE(MODULE_CLASS_MISC, k_helper2, NULL);

/* --------------------------------------------------------------------- */
/* Sysctl interface to query information about the module.               */
/* --------------------------------------------------------------------- */

/* TODO: Change the integer variables below that represent booleans to
 * bools, once sysctl(8) supports CTLTYPE_BOOL nodes. */

static struct sysctllog *clogp;
static int present = 1;

#define K_HELPER2 0x23456781
#define K_HELPER_PRESENT 0

SYSCTL_SETUP(sysctl_k_helper2_setup, "sysctl k_helper subtree setup")
{

	sysctl_createv(clog, 0, NULL, NULL,
	               CTLFLAG_PERMANENT,
	               CTLTYPE_NODE, "k_helper2", NULL,
	               NULL, 0, NULL, 0,
	               CTL_VENDOR, K_HELPER2, CTL_EOL);

	sysctl_createv(clog, 0, NULL, NULL,
	               CTLFLAG_PERMANENT,
	               CTLTYPE_INT, "present",
		       SYSCTL_DESCR("Whether the module was loaded or not"),
		       NULL, 0, &present, 0,
	               CTL_VENDOR, K_HELPER2, K_HELPER_PRESENT, CTL_EOL);
}

/* --------------------------------------------------------------------- */
/* Module management.                                                    */
/* --------------------------------------------------------------------- */

static
int
k_helper2_init(prop_dictionary_t props)
{
	sysctl_k_helper2_setup(&clogp);

	return 0;
}

static
int
k_helper2_fini(void *arg)
{

	sysctl_teardown(&clogp);

	return 0;
}

static
int
k_helper2_modcmd(modcmd_t cmd, void *arg)
{
	int ret;

	switch (cmd) {
	case MODULE_CMD_INIT:
		ret = k_helper2_init(arg);
		break;

	case MODULE_CMD_FINI:
		ret = k_helper2_fini(arg);
		break;

	case MODULE_CMD_STAT:
	default:
		ret = ENOTTY;
	}

	return ret;
}
