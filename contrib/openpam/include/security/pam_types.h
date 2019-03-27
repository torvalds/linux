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
 * $OpenPAM: pam_types.h 938 2017-04-30 21:34:42Z des $
 */

#ifndef SECURITY_PAM_TYPES_H_INCLUDED
#define SECURITY_PAM_TYPES_H_INCLUDED

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * XSSO 5.1.1
 */
struct pam_message {
	int	 msg_style;
	char	*msg;
};

struct pam_response {
	char	*resp;
	int	 resp_retcode;
};

/*
 * XSSO 5.1.2
 */
struct pam_conv {
	int	(*conv)(int, const struct pam_message **,
	    struct pam_response **, void *);
	void	*appdata_ptr;
};

/*
 * XSSO 5.1.3
 */
struct pam_handle;
typedef struct pam_handle pam_handle_t;

/*
 * Solaris 9
 */
typedef struct pam_repository {
	char	*type;
	void	*scope;
	size_t	 scope_len;
} pam_repository_t;

#ifdef __cplusplus
}
#endif

#endif /* !SECURITY_PAM_TYPES_H_INCLUDED */
