/*
 * Copyright (c) 1997 - 1999 Kungliga Tekniska Högskolan
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

#include <config.h>

#include "roken.h"

/*
 * Try to return what should be considered the default username or
 * NULL if we can't guess at all.
 */

ROKEN_LIB_FUNCTION const char * ROKEN_LIB_CALL
get_default_username (void)
{
    const char *user;

    user = getenv ("USER");
    if (user == NULL)
	user = getenv ("LOGNAME");
    if (user == NULL)
	user = getenv ("USERNAME");

#if defined(HAVE_GETLOGIN) && !defined(POSIX_GETLOGIN)
    if (user == NULL) {
	user = (const char *)getlogin ();
	if (user != NULL)
	    return user;
    }
#endif
#ifdef HAVE_PWD_H
    {
	uid_t uid = getuid ();
	struct passwd *pwd;

	if (user != NULL) {
	    pwd = k_getpwnam (user);
	    if (pwd != NULL && pwd->pw_uid == uid)
		return user;
	}
	pwd = k_getpwuid (uid);
	if (pwd != NULL)
	    return pwd->pw_name;
    }
#endif
#ifdef _WIN32
    /* TODO: We can call GetUserNameEx() and figure out a
       username. However, callers do not free the return value of this
       function. */
#endif

    return user;
}
