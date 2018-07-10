/* vi: set sw=4 ts=4: */
/*
 * Copyright 1989 - 1991, Julianne Frances Haugh <jockgrrl@austin.rr.com>
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
 * 3. Neither the name of Julianne F. Haugh nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JULIE HAUGH AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL JULIE HAUGH OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "libbb.h"

void FAST_FUNC setup_environment(const char *shell, int flags, const struct passwd *pw)
{
	if (!shell || !shell[0])
		shell = DEFAULT_SHELL;

	/* Change the current working directory to be the home directory
	 * of the user */
	if (!(flags & SETUP_ENV_NO_CHDIR)) {
		if (chdir(pw->pw_dir) != 0) {
			bb_error_msg("can't change directory to '%s'", pw->pw_dir);
			xchdir((flags & SETUP_ENV_TO_TMP) ? "/tmp" : "/");
		}
	}

	if (flags & SETUP_ENV_CLEARENV) {
		const char *term;

		/* Leave TERM unchanged. Set HOME, SHELL, USER, LOGNAME, PATH.
		 * Unset all other environment variables.  */
		term = getenv("TERM");
		clearenv();
		if (term)
			xsetenv("TERM", term);
		xsetenv("PATH", (pw->pw_uid ? bb_default_path : bb_default_root_path));
		goto shortcut;
		// No, gcc (4.2.1) is not clever enougn to do it itself.
		//xsetenv("USER",    pw->pw_name);
		//xsetenv("LOGNAME", pw->pw_name);
		//xsetenv("HOME",    pw->pw_dir);
		//xsetenv("SHELL",   shell);
	} else if (flags & SETUP_ENV_CHANGEENV) {
		/* Set HOME, SHELL, and if not becoming a super-user,
		 * USER and LOGNAME.  */
		if (pw->pw_uid) {
 shortcut:
			xsetenv("USER",    pw->pw_name);
			xsetenv("LOGNAME", pw->pw_name);
		}
		xsetenv("HOME",    pw->pw_dir);
		xsetenv("SHELL",   shell);
	}
}
