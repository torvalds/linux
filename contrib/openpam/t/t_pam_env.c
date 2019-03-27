/*-
 * Copyright (c) 2019 Dag-Erling Smørgrav
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
 * $OpenPAM$
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <cryb/test.h>

#include <security/pam_appl.h>
#include <security/openpam.h>

#include "t_pam_err.h"

#define T_ENV_NAME	"MAGIC_WORDS"
#define T_ENV_VALUE	"SQUEAMISH OSSIFRAGE"
#define T_ENV_NAMEVALUE	T_ENV_NAME "=" T_ENV_VALUE

struct pam_conv t_null_pamc;


/***************************************************************************
 * Tests
 */

static int
t_env_empty(char **desc CRYB_UNUSED, void *arg CRYB_UNUSED)
{
	pam_handle_t *pamh;
	char **envlist;
	int pam_err, ret;

	ret = 1;
	pam_err = pam_start("t_pam_env", "test", &t_null_pamc, &pamh);
	t_assert(pam_err == PAM_SUCCESS);
	envlist = pam_getenvlist(pamh);
	ret &= t_is_not_null(envlist);
	if (envlist != NULL) {
		ret &= t_is_null(*envlist);
		openpam_free_envlist(envlist);
	}
	pam_end(pamh, pam_err);
	return (ret);
}

static int
t_putenv_simple(char **desc CRYB_UNUSED, void *arg CRYB_UNUSED)
{
	pam_handle_t *pamh;
	char **envlist;
	int pam_err, ret;

	ret = 1;
	pam_err = pam_start("t_pam_env", "test", &t_null_pamc, &pamh);
	t_assert(pam_err == PAM_SUCCESS);
	pam_err = pam_putenv(pamh, T_ENV_NAMEVALUE);
	ret &= t_compare_pam_err(PAM_SUCCESS, pam_err);
	envlist = pam_getenvlist(pamh);
	ret &= t_is_not_null(envlist);
	if (envlist != NULL) {
		ret &= t_compare_str(T_ENV_NAMEVALUE, envlist[0])
		    & t_is_null(envlist[1]);
		openpam_free_envlist(envlist);
	}
	pam_end(pamh, pam_err);
	return (ret);
}

static int
t_setenv_simple(char **desc CRYB_UNUSED, void *arg CRYB_UNUSED)
{
	pam_handle_t *pamh;
	char **envlist;
	int pam_err, ret;

	ret = 1;
	pam_err = pam_start("t_pam_env", "test", &t_null_pamc, &pamh);
	t_assert(pam_err == PAM_SUCCESS);
	pam_err = pam_setenv(pamh, T_ENV_NAME, T_ENV_VALUE, 0);
	ret &= t_compare_pam_err(PAM_SUCCESS, pam_err);
	envlist = pam_getenvlist(pamh);
	ret &= t_is_not_null(envlist);
	if (envlist != NULL) {
		ret &= t_compare_str(T_ENV_NAMEVALUE, envlist[0])
		    & t_is_null(envlist[1]);
		openpam_free_envlist(envlist);
	}
	pam_end(pamh, pam_err);
	return (ret);
}

static int
t_getenv_empty(char **desc CRYB_UNUSED, void *arg CRYB_UNUSED)
{
	pam_handle_t *pamh;
	const char *value;
	int pam_err, ret;

	ret = 1;
	pam_err = pam_start("t_pam_env", "test", &t_null_pamc, &pamh);
	t_assert(pam_err == PAM_SUCCESS);
	value = pam_getenv(pamh, T_ENV_NAME);
	ret &= t_compare_str(NULL, value);
	pam_end(pamh, pam_err);
	return (ret);
}

static int
t_getenv_simple_miss(char **desc CRYB_UNUSED, void *arg CRYB_UNUSED)
{
	pam_handle_t *pamh;
	const char *value;
	int pam_err, ret;

	ret = 1;
	pam_err = pam_start("t_pam_env", "test", &t_null_pamc, &pamh);
	t_assert(pam_err == PAM_SUCCESS);
	pam_err = pam_setenv(pamh, T_ENV_NAME, T_ENV_VALUE, 0);
	t_assert(pam_err == PAM_SUCCESS);
	value = pam_getenv(pamh, "XYZZY");
	ret &= t_compare_str(NULL, value);
	pam_end(pamh, pam_err);
	return (ret);
}

static int
t_getenv_simple_hit(char **desc CRYB_UNUSED, void *arg CRYB_UNUSED)
{
	pam_handle_t *pamh;
	const char *value;
	int pam_err, ret;

	ret = 1;
	pam_err = pam_start("t_pam_env", "test", &t_null_pamc, &pamh);
	t_assert(pam_err == PAM_SUCCESS);
	pam_err = pam_setenv(pamh, T_ENV_NAME, T_ENV_VALUE, 0);
	t_assert(pam_err == PAM_SUCCESS);
	value = pam_getenv(pamh, T_ENV_NAME);
	ret &= t_compare_str(T_ENV_VALUE, value);
	pam_end(pamh, pam_err);
	return (ret);
}


/***************************************************************************
 * Boilerplate
 */

static int
t_prepare(int argc CRYB_UNUSED, char *argv[] CRYB_UNUSED)
{

	openpam_set_feature(OPENPAM_FALLBACK_TO_OTHER, 0);

	t_add_test(t_env_empty, NULL, "initially empty");
	t_add_test(t_putenv_simple, NULL, "put - simple");
	t_add_test(t_setenv_simple, NULL, "set - simple");
	t_add_test(t_getenv_empty, NULL, "get - empty");
	t_add_test(t_getenv_simple_miss, NULL, "get - simple (miss)");
	t_add_test(t_getenv_simple_hit, NULL, "get - simple (hit)");

	return (0);
}

int
main(int argc, char *argv[])
{

	t_main(t_prepare, NULL, argc, argv);
}
