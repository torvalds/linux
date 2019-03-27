/*-
 * Copyright (c) 2015-2017 Dag-Erling Smørgrav
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
 * $OpenPAM: t_openpam_dispatch.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cryb/test.h>

#include <security/pam_appl.h>
#include <security/openpam.h>

#include "openpam_impl.h"
#include "t_pam_conv.h"

#define T_FUNC(n, d)							\
	static const char *t_ ## n ## _desc = d;			\
	static int t_ ## n ## _func(OPENPAM_UNUSED(char **desc),	\
	    OPENPAM_UNUSED(void *arg))

#define T(n)								\
	t_add_test(&t_ ## n ## _func, NULL, "%s", t_ ## n ## _desc)

const char *pam_return_so;

T_FUNC(empty_policy, "empty policy")
{
	struct t_pam_conv_script script;
	struct pam_conv pamc;
	struct t_file *tf;
	pam_handle_t *pamh;
	int pam_err, ret;

	memset(&script, 0, sizeof script);
	pamc.conv = &t_pam_conv;
	pamc.appdata_ptr = &script;
	tf = t_fopen(NULL);
	t_fprintf(tf, "# empty policy\n");
	pam_err = pam_start(tf->name, "test", &pamc, &pamh);
	if (pam_err != PAM_SUCCESS) {
		t_printv("pam_start() returned %d\n", pam_err);
		return (0);
	}
	/*
	 * Note: openpam_dispatch() currently returns PAM_SYSTEM_ERR when
	 * the chain is empty, it should possibly return PAM_SERVICE_ERR
	 * instead.
	 */
	pam_err = pam_authenticate(pamh, 0);
	t_printv("pam_authenticate() returned %d\n", pam_err);
	ret = (pam_err == PAM_SYSTEM_ERR);
	pam_err = pam_setcred(pamh, 0);
	t_printv("pam_setcred() returned %d\n", pam_err);
	ret &= (pam_err == PAM_SYSTEM_ERR);
	pam_err = pam_acct_mgmt(pamh, 0);
	t_printv("pam_acct_mgmt() returned %d\n", pam_err);
	ret &= (pam_err == PAM_SYSTEM_ERR);
	pam_err = pam_chauthtok(pamh, 0);
	t_printv("pam_chauthtok() returned %d\n", pam_err);
	ret &= (pam_err == PAM_SYSTEM_ERR);
	pam_err = pam_open_session(pamh, 0);
	t_printv("pam_open_session() returned %d\n", pam_err);
	ret &= (pam_err == PAM_SYSTEM_ERR);
	pam_err = pam_close_session(pamh, 0);
	t_printv("pam_close_session() returned %d\n", pam_err);
	ret &= (pam_err == PAM_SYSTEM_ERR);
	pam_end(pamh, pam_err);
	t_fclose(tf);
	return (ret);
}

static struct t_pam_return_case {
	int		 facility;
	int		 primitive;
	int		 flags;
	struct {
		int		 ctlflag;
		int		 modret;
	} mod[2];
	int		 result;
} t_pam_return_cases[] = {
	{
		PAM_AUTH, PAM_SM_AUTHENTICATE, 0,
		{
			{ PAM_REQUIRED, PAM_SUCCESS },
			{ PAM_REQUIRED, PAM_SUCCESS },
		},
		PAM_SUCCESS,
	},
};

T_FUNC(mod_return, "module return value")
{
	struct t_pam_return_case *tc;
	struct t_pam_conv_script script;
	struct pam_conv pamc;
	struct t_file *tf;
	pam_handle_t *pamh;
	unsigned int i, j, n;
	int pam_err;

	memset(&script, 0, sizeof script);
	pamc.conv = &t_pam_conv;
	pamc.appdata_ptr = &script;
	n = sizeof t_pam_return_cases / sizeof t_pam_return_cases[0];
	for (i = 0; i < n; ++i) {
		tc = &t_pam_return_cases[i];
		tf = t_fopen(NULL);
		for (j = 0; j < 2; ++j) {
			t_fprintf(tf, "%s %s %s error=%s\n",
			    pam_facility_name[tc->facility],
			    pam_control_flag_name[tc->mod[j].ctlflag],
			    pam_return_so,
			    pam_err_name[tc->mod[j].modret]);
		}
		pam_err = pam_start(tf->name, "test", &pamc, &pamh);
		if (pam_err != PAM_SUCCESS) {
			t_printv("pam_start() returned %d\n", pam_err);
			t_fclose(tf);
			continue;
		}
		switch (tc->primitive) {
		case PAM_SM_AUTHENTICATE:
			pam_err = pam_authenticate(pamh, tc->flags);
			break;
		case PAM_SM_SETCRED:
			pam_err = pam_setcred(pamh, tc->flags);
			break;
		case PAM_SM_ACCT_MGMT:
			pam_err = pam_acct_mgmt(pamh, tc->flags);
			break;
		case PAM_SM_OPEN_SESSION:
			pam_err = pam_open_session(pamh, tc->flags);
			break;
		case PAM_SM_CLOSE_SESSION:
			pam_err = pam_close_session(pamh, tc->flags);
			break;
		case PAM_SM_CHAUTHTOK:
			pam_err = pam_chauthtok(pamh, tc->flags);
			break;
		}
		t_printv("%s returned %d\n",
		    pam_func_name[tc->primitive], pam_err);
		pam_end(pamh, pam_err);
		t_printv("here\n");
		t_fclose(tf);
	}
	return (1);
}


/***************************************************************************
 * Boilerplate
 */

static int
t_prepare(int argc, char *argv[])
{

	(void)argc;
	(void)argv;

	if ((pam_return_so = getenv("PAM_RETURN_SO")) == NULL) {
		t_printv("define PAM_RETURN_SO before running these tests\n");
		return (0);
	}

	openpam_set_feature(OPENPAM_RESTRICT_MODULE_NAME, 0);
	openpam_set_feature(OPENPAM_VERIFY_MODULE_FILE, 0);
	openpam_set_feature(OPENPAM_RESTRICT_SERVICE_NAME, 0);
	openpam_set_feature(OPENPAM_VERIFY_POLICY_FILE, 0);
	openpam_set_feature(OPENPAM_FALLBACK_TO_OTHER, 0);

	T(empty_policy);
	T(mod_return);

	return (0);
}

int
main(int argc, char *argv[])
{

	t_main(t_prepare, NULL, argc, argv);
}
