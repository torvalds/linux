/*
 * Copyright (c) 1997 - 2001 Kungliga Tekniska Högskolan
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

#include "krb5_locl.h"

KRB5_LIB_FUNCTION int KRB5_CALLCONV
krb5_prompter_posix (krb5_context context,
		     void *data,
		     const char *name,
		     const char *banner,
		     int num_prompts,
		     krb5_prompt prompts[])
{
    int i;

    if (name)
	fprintf (stderr, "%s\n", name);
    if (banner)
	fprintf (stderr, "%s\n", banner);
    if (name || banner)
	fflush(stderr);
    for (i = 0; i < num_prompts; ++i) {
	if (prompts[i].hidden) {
	    if(UI_UTIL_read_pw_string(prompts[i].reply->data,
				  prompts[i].reply->length,
				  prompts[i].prompt,
				  0))
	       return 1;
	} else {
	    char *s = prompts[i].reply->data;

	    fputs (prompts[i].prompt, stdout);
	    fflush (stdout);
	    if(fgets(prompts[i].reply->data,
		     prompts[i].reply->length,
		     stdin) == NULL)
		return 1;
	    s[strcspn(s, "\n")] = '\0';
	}
    }
    return 0;
}
