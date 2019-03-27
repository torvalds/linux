/*-
 * Copyright (c) 2002-2003 Networks Associates Technology, Inc.
 * Copyright (c) 2004-2017 Dag-Erling Smørgrav
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
 * $OpenPAM: pam_appl.h 938 2017-04-30 21:34:42Z des $
 */

#ifndef SECURITY_PAM_APPL_H_INCLUDED
#define SECURITY_PAM_APPL_H_INCLUDED

#include <security/pam_types.h>
#include <security/pam_constants.h>
#include <security/openpam_attr.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * XSSO 4.2.1, 6
 */

int
pam_acct_mgmt(pam_handle_t *_pamh,
	int _flags)
	OPENPAM_NONNULL((1));

int
pam_authenticate(pam_handle_t *_pamh,
	int _flags)
	OPENPAM_NONNULL((1));

int
pam_chauthtok(pam_handle_t *_pamh,
	int _flags)
	OPENPAM_NONNULL((1));

int
pam_close_session(pam_handle_t *_pamh,
	int _flags)
	OPENPAM_NONNULL((1));

int
pam_end(pam_handle_t *_pamh,
	int _status);

int
pam_get_data(const pam_handle_t *_pamh,
	const char *_module_data_name,
	const void **_data)
	OPENPAM_NONNULL((1,2,3));

int
pam_get_item(const pam_handle_t *_pamh,
	int _item_type,
	const void **_item)
	OPENPAM_NONNULL((1,3));

int
pam_get_user(pam_handle_t *_pamh,
	const char **_user,
	const char *_prompt)
	OPENPAM_NONNULL((1,2));

const char *
pam_getenv(pam_handle_t *_pamh,
	const char *_name)
	OPENPAM_NONNULL((1,2));

char **
pam_getenvlist(pam_handle_t *_pamh)
	OPENPAM_NONNULL((1));

int
pam_open_session(pam_handle_t *_pamh,
	int _flags)
	OPENPAM_NONNULL((1));

int
pam_putenv(pam_handle_t *_pamh,
	const char *_namevalue)
	OPENPAM_NONNULL((1,2));

int
pam_set_data(pam_handle_t *_pamh,
	const char *_module_data_name,
	void *_data,
	void (*_cleanup)(pam_handle_t *_pamh,
		void *_data,
		int _pam_end_status))
	OPENPAM_NONNULL((1,2));

int
pam_set_item(pam_handle_t *_pamh,
	int _item_type,
	const void *_item)
	OPENPAM_NONNULL((1));

int
pam_setcred(pam_handle_t *_pamh,
	int _flags)
	OPENPAM_NONNULL((1));

int
pam_start(const char *_service,
	const char *_user,
	const struct pam_conv *_pam_conv,
	pam_handle_t **_pamh)
	OPENPAM_NONNULL((4));

const char *
pam_strerror(const pam_handle_t *_pamh,
	int _error_number);

/*
 * Single Sign-On extensions
 */
#if 0
int
pam_authenticate_secondary(pam_handle_t *_pamh,
	char *_target_username,
	char *_target_module_type,
	char *_target_authn_domain,
	char *_target_supp_data,
	char *_target_module_authtok,
	int _flags);

int
pam_get_mapped_authtok(pam_handle_t *_pamh,
	const char *_target_module_username,
	const char *_target_module_type,
	const char *_target_authn_domain,
	size_t *_target_authtok_len,
	unsigned char **_target_module_authtok);

int
pam_get_mapped_username(pam_handle_t *_pamh,
	const char *_src_username,
	const char *_src_module_type,
	const char *_src_authn_domain,
	const char *_target_module_type,
	const char *_target_authn_domain,
	char **_target_module_username);

int
pam_set_mapped_authtok(pam_handle_t *_pamh,
	const char *_target_module_username,
	size_t _target_authtok_len,
	unsigned char *_target_module_authtok,
	const char *_target_module_type,
	const char *_target_authn_domain);

int
pam_set_mapped_username(pam_handle_t *_pamh,
	char *_src_username,
	char *_src_module_type,
	char *_src_authn_domain,
	char *_target_module_username,
	char *_target_module_type,
	char *_target_authn_domain);
#endif /* 0 */

#ifdef __cplusplus
}
#endif

#endif /* !SECURITY_PAM_APPL_H_INCLUDED */
