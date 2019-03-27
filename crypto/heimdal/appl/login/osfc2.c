/*
 * Copyright (c) 1998 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "login_locl.h"
RCSID("$Id$");

int
do_osfc2_magic(uid_t uid)
{
#ifdef HAVE_OSFC2
    struct es_passwd *epw;
    char *argv[2];

    /* fake */
    argv[0] = (char*)getprogname();
    argv[1] = NULL;
    set_auth_parameters(1, argv);

    epw = getespwuid(uid);
    if(epw == NULL) {
	syslog(LOG_AUTHPRIV|LOG_NOTICE,
	       "getespwuid failed for %d", uid);
	printf("Sorry.\n");
	return 1;
    }
    /* We don't check for auto-retired, foo-retired,
       bar-retired, or any other kind of retired accounts
       here; neither do we check for time-locked accounts, or
       any other kind of serious C2 mumbo-jumbo. We do,
       however, call setluid, since failing to do so is not
       very good (take my word for it). */

    if(!epw->uflg->fg_uid) {
	syslog(LOG_AUTHPRIV|LOG_NOTICE,
	       "attempted login by %s (has no uid)", epw->ufld->fd_name);
	printf("Sorry.\n");
	return 1;
    }
    setluid(epw->ufld->fd_uid);
    if(getluid() != epw->ufld->fd_uid) {
	syslog(LOG_AUTHPRIV|LOG_NOTICE,
	       "failed to set LUID for %s (%d)",
	       epw->ufld->fd_name, epw->ufld->fd_uid);
	printf("Sorry.\n");
	return 1;
    }
#endif /* HAVE_OSFC2 */
    return 0;
}
