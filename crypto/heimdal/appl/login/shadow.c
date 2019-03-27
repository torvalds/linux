/*
 * Copyright (c) 1997, 1998, 1999 Kungliga Tekniska Högskolan
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

#ifdef HAVE_SHADOW_H

#ifndef _PATH_CHPASS
#define _PATH_CHPASS "/usr/bin/passwd"
#endif

static int
change_passwd(const struct passwd *who)
{
    int status;
    pid_t pid;

    switch (pid = fork()) {
    case -1:
        printf("fork /bin/passwd");
        exit(1);
    case 0:
        execlp(_PATH_CHPASS, "passwd", who->pw_name, (char *) 0);
        exit(1);
    default:
        waitpid(pid, &status, 0);
        return (status);
    }
}

void
check_shadow(const struct passwd *pw, const struct spwd *sp)
{
  long today;

  today = time(0)/(24L * 60 * 60);

  if (sp == NULL)
      return;

  if (sp->sp_expire > 0) {
        if (today >= sp->sp_expire) {
            printf("Your account has expired.\n");
            sleep(1);
            exit(0);
        } else if (sp->sp_expire - today < 14) {
            printf("Your account will expire in %d days.\n",
                   (int)(sp->sp_expire - today));
        }
  }

  if (sp->sp_max > 0) {
        if (today >= (sp->sp_lstchg + sp->sp_max)) {
            printf("Your password has expired. Choose a new one.\n");
            change_passwd(pw);
        } else if (sp->sp_warn > 0
            && (today > (sp->sp_lstchg + sp->sp_max - sp->sp_warn))) {
            printf("Your password will expire in %d days.\n",
                   (int)(sp->sp_lstchg + sp->sp_max - today));
        }
  }
}
#endif /* HAVE_SHADOW_H */
