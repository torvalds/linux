/*
 * Copyright (c) 2004 Kungliga Tekniska Högskolan
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

/* $Id$ */

#ifndef KADM5_PWCHECK_H
#define KADM5_PWCHECK_H 1


#define KADM5_PASSWD_VERSION_V0 0
#define KADM5_PASSWD_VERSION_V1 1

typedef const char* (*kadm5_passwd_quality_check_func_v0)(krb5_context,
							  krb5_principal,
							  krb5_data*);

/*
 * The 4th argument, is a tuning parameter for the quality check
 * function, the lib/caller will providing it for the password quality
 * module.
 */

typedef int
(*kadm5_passwd_quality_check_func)(krb5_context context,
				   krb5_principal principal,
				   krb5_data *password,
				   const char *tuning,
				   char *message,
				   size_t length);

struct kadm5_pw_policy_check_func {
    const char *name;
    kadm5_passwd_quality_check_func func;
};

struct kadm5_pw_policy_verifier {
    const char *name;
    int version;
    const char *vendor;
    const struct kadm5_pw_policy_check_func *funcs;
};

#endif /* KADM5_PWCHECK_H */
