/*	$NetBSD: k_uvm.c,v 1.1 2012/02/17 22:36:50 jmmv Exp $	*/
/*
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
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
__KERNEL_RCSID(0, "$NetBSD: k_uvm.c,v 1.1 2012/02/17 22:36:50 jmmv Exp $");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>

MODULE(MODULE_CLASS_MISC, k_uvm, NULL);

/* --------------------------------------------------------------------- */
/* Sysctl interface to query information about the module.               */
/* --------------------------------------------------------------------- */

static struct sysctllog *clogp;
static int page_size;

#define K_UVM 0x12345678
#define K_UVM_VALUE 0

SYSCTL_SETUP(sysctl_k_uvm_setup, "sysctl k_uvm subtree setup")
{

	sysctl_createv(clog, 0, NULL, NULL,
	               CTLFLAG_PERMANENT,
	               CTLTYPE_NODE, "k_uvm", NULL,
	               NULL, 0, NULL, 0,
	               CTL_VENDOR, K_UVM, CTL_EOL);

	sysctl_createv(clog, 0, NULL, NULL,
	               CTLFLAG_PERMANENT,
	               CTLTYPE_INT, "page_size",
		       SYSCTL_DESCR("Value of PAGE_SIZE"),
		       NULL, 0, &page_size, 0,
	               CTL_VENDOR, K_UVM, K_UVM_VALUE, CTL_EOL);
}

/* --------------------------------------------------------------------- */
/* Module management.                                                    */
/* --------------------------------------------------------------------- */

static
int
k_uvm_init(prop_dictionary_t props)
{

	page_size = PAGE_SIZE;

	sysctl_k_uvm_setup(&clogp);

	return 0;
}

static
int
k_uvm_fini(void *arg)
{

	sysctl_teardown(&clogp);

	return 0;
}

static
int
k_uvm_modcmd(modcmd_t cmd, void *arg)
{
	int ret;

	switch (cmd) {
	case MODULE_CMD_INIT:
		ret = k_uvm_init(arg);
		break;

	case MODULE_CMD_FINI:
		ret = k_uvm_fini(arg);
		break;

	case MODULE_CMD_STAT:
	default:
		ret = ENOTTY;
	}

	return ret;
}
