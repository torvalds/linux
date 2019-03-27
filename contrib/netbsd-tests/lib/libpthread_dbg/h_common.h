/*	$NetBSD: h_common.h,v 1.2 2016/11/19 02:30:54 kamil Exp $	*/

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef H_COMMON_H
#define H_COMMON_H

#include <sys/cdefs.h>
#include <dlfcn.h>
#include <pthread_dbg.h>
#include <string.h>

#include <atf-c.h>

#define PTHREAD_REQUIRE(x) \
    do { \
        int ret = (x); \
        ATF_REQUIRE_MSG(ret == 0, "%s: %s", #x, strerror(ret)); \
    } while (0)

#define PTHREAD_REQUIRE_STATUS(x, v) \
    do { \
        int ret = (x); \
        ATF_REQUIRE_MSG(ret == (v), "%s: %s", #x, strerror(ret)); \
    } while (0)

static int __used
dummy_proc_read(void *arg, caddr_t addr, void *buf, size_t size)
{
	return TD_ERR_ERR;
}

static int __used
dummy_proc_write(void *arg, caddr_t addr, void *buf, size_t size)
{
	return TD_ERR_ERR;
}

static int __used
dummy_proc_lookup(void *arg, const char *sym, caddr_t *addr)
{
	return TD_ERR_ERR;
}

static int __used
dummy_proc_regsize(void *arg, int regset, size_t *size)
{
	return TD_ERR_ERR;
}
 
static int __used
dummy_proc_getregs(void *arg, int regset, int lwp, void *buf)   
{
	return TD_ERR_ERR;
}

static int __used
dummy_proc_setregs(void *arg, int regset, int lwp, void *buf)
{
	return TD_ERR_ERR;
}

/* Minimalistic basic implementation */

static int __used
basic_proc_read(void *arg, caddr_t addr, void *buf, size_t size)
{
	memcpy(buf, addr, size);

	return TD_ERR_OK;
}

static int __used
basic_proc_write(void *arg, caddr_t addr, void *buf, size_t size)
{
	memcpy(addr, buf, size);

	return TD_ERR_OK;
}

static int __used
basic_proc_lookup(void *arg, const char *sym, caddr_t *addr)
{
	void *handle;
	void *symbol;

	ATF_REQUIRE_MSG((handle = dlopen(NULL, RTLD_LOCAL | RTLD_LAZY))
	    != NULL, "dlopen(3) failed: %s", dlerror());

	symbol = dlsym(handle, sym);

	ATF_REQUIRE_MSG(dlclose(handle) == 0, "dlclose(3) failed: %s",
	    dlerror());

	if (!symbol)
		return TD_ERR_NOSYM;

	*addr = (caddr_t)(uintptr_t)symbol;

	return TD_ERR_OK;
}

#endif // H_COMMON_H
