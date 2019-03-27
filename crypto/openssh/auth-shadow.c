/*
 * Copyright (c) 2004 Darren Tucker.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"

#if defined(USE_SHADOW) && defined(HAS_SHADOW_EXPIRE)
#include <shadow.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "hostfile.h"
#include "auth.h"
#include "sshbuf.h"
#include "ssherr.h"
#include "log.h"

#ifdef DAY
# undef DAY
#endif
#define DAY	(24L * 60 * 60) /* 1 day in seconds */

extern struct sshbuf *loginmsg;

/*
 * For the account and password expiration functions, we assume the expiry
 * occurs the day after the day specified.
 */

/*
 * Check if specified account is expired.  Returns 1 if account is expired,
 * 0 otherwise.
 */
int
auth_shadow_acctexpired(struct spwd *spw)
{
	time_t today;
	int daysleft;
	int r;

	today = time(NULL) / DAY;
	daysleft = spw->sp_expire - today;
	debug3("%s: today %d sp_expire %d days left %d", __func__, (int)today,
	    (int)spw->sp_expire, daysleft);

	if (spw->sp_expire == -1) {
		debug3("account expiration disabled");
	} else if (daysleft < 0) {
		logit("Account %.100s has expired", spw->sp_namp);
		return 1;
	} else if (daysleft <= spw->sp_warn) {
		debug3("account will expire in %d days", daysleft);
		if ((r = sshbuf_putf(loginmsg, 
		    "Your account will expire in %d day%s.\n", daysleft,
		    daysleft == 1 ? "" : "s")) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
	}

	return 0;
}

/*
 * Checks password expiry for platforms that use shadow passwd files.
 * Returns: 1 = password expired, 0 = password not expired
 */
int
auth_shadow_pwexpired(Authctxt *ctxt)
{
	struct spwd *spw = NULL;
	const char *user = ctxt->pw->pw_name;
	time_t today;
	int r, daysleft, disabled = 0;

	if ((spw = getspnam((char *)user)) == NULL) {
		error("Could not get shadow information for %.100s", user);
		return 0;
	}

	today = time(NULL) / DAY;
	debug3("%s: today %d sp_lstchg %d sp_max %d", __func__, (int)today,
	    (int)spw->sp_lstchg, (int)spw->sp_max);

#if defined(__hpux) && !defined(HAVE_SECUREWARE)
	if (iscomsec()) {
		struct pr_passwd *pr;

		pr = getprpwnam((char *)user);

		/* Test for Trusted Mode expiry disabled */
		if (pr != NULL && pr->ufld.fd_min == 0 &&
		    pr->ufld.fd_lifetime == 0 && pr->ufld.fd_expire == 0 &&
		    pr->ufld.fd_pw_expire_warning == 0 &&
		    pr->ufld.fd_schange != 0)
			disabled = 1;
	}
#endif

	/* TODO: check sp_inact */
	daysleft = spw->sp_lstchg + spw->sp_max - today;
	if (disabled) {
		debug3("password expiration disabled");
	} else if (spw->sp_lstchg == 0) {
		logit("User %.100s password has expired (root forced)", user);
		return 1;
	} else if (spw->sp_max == -1) {
		debug3("password expiration disabled");
	} else if (daysleft < 0) {
		logit("User %.100s password has expired (password aged)", user);
		return 1;
	} else if (daysleft <= spw->sp_warn) {
		debug3("password will expire in %d days", daysleft);
		if ((r = sshbuf_putf(loginmsg, 
		    "Your password will expire in %d day%s.\n", daysleft,
		    daysleft == 1 ? "" : "s")) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
	}

	return 0;
}
#endif	/* USE_SHADOW && HAS_SHADOW_EXPIRE */
