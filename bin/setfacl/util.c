/*-
 * Copyright (c) 2001 Chris D. Faulhaber
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "setfacl.h"

void *
zmalloc(size_t size)
{
	void *ptr;

	ptr = calloc(1, size);
	if (ptr == NULL)
		err(1, "calloc() failed");
	return (ptr);
}

void *
zrealloc(void *ptr, size_t size)
{
	void *newptr;

	newptr = realloc(ptr, size);
	if (newptr == NULL)
		err(1, "realloc() failed");
	return (newptr);
}

const char *
brand_name(int brand)
{
	switch (brand) {
	case ACL_BRAND_NFS4:
		return "NFSv4";
	case ACL_BRAND_POSIX:
		return "POSIX.1e";
	default:
		return "unknown";
	}
}

int
branding_mismatch(int brand1, int brand2)
{
	if (brand1 == ACL_BRAND_UNKNOWN || brand2 == ACL_BRAND_UNKNOWN)
		return (0);
	if (brand1 != brand2)
		return (1);
	return (0);
}
