/* $NetBSD: exec_prot.h,v 1.1 2011/07/18 23:16:11 jym Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jean-Yves Migeon.
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

#ifndef _TESTS_EXEC_PROT_H_
#define _TESTS_EXEC_PROT_H_

/*
 * Prototype definitions of external helper functions for executable
 * mapping tests.
 */

/*
 * Trivial MD shellcode that justs returns 1.
 */
int return_one(void);     /* begin marker -- shellcode entry */
int return_one_end(void); /* end marker */

/*
 * MD callback to verify whether host offers executable space protection.
 * Returns execute protection level.
 */
int exec_prot_support(void);

/* execute protection level */
enum {
	NOTIMPL = -1, /* callback not implemented */
	NO_XP,        /* no execute protection */
	PERPAGE_XP,   /* per-page execute protection */
	PARTIAL_XP    /* partial execute protection. Depending on where the
			 page is located in virtual memory, executable space
			 protection may be enforced or not. */
};
#endif
