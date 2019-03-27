/*
 * Copyright (c) 1995 - 1999, 2003 Kungliga Tekniska Högskolan
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

#include "ftpd_locl.h"

RCSID("$Id$");

#if defined(KRB5)

int do_destroy_tickets = 1;
char *k5ccname;

#endif

#ifdef KRB5

static void
dest_cc(void)
{
    krb5_context context;
    krb5_error_code ret;
    krb5_ccache id;

    ret = krb5_init_context(&context);
    if (ret == 0) {
	if (k5ccname)
	    ret = krb5_cc_resolve(context, k5ccname, &id);
	else
	    ret = krb5_cc_default (context, &id);
	if (ret)
	    krb5_free_context(context);
    }
    if (ret == 0) {
	krb5_cc_destroy(context, id);
	krb5_free_context (context);
    }
}
#endif

#if defined(KRB5)

/*
 * Only destroy if we created the tickets
 */

void
cond_kdestroy(void)
{
    if (do_destroy_tickets) {
#if KRB5
	dest_cc();
#endif
	do_destroy_tickets = 0;
    }
    afsunlog();
}

void
kdestroy(void)
{
#if KRB5
    dest_cc();
#endif
    afsunlog();
    reply(200, "Tickets destroyed");
}


void
afslog(const char *cell, int quiet)
{
    if(k_hasafs()) {
#ifdef KRB5
	krb5_context context;
	krb5_error_code ret;
	krb5_ccache id;

	ret = krb5_init_context(&context);
	if (ret == 0) {
	    if (k5ccname)
		ret = krb5_cc_resolve(context, k5ccname, &id);
	    else
		ret = krb5_cc_default(context, &id);
	    if (ret)
		krb5_free_context(context);
	}
	if (ret == 0) {
	    krb5_afslog(context, id, cell, 0);
	    krb5_cc_close (context, id);
	    krb5_free_context (context);
	}
#endif
	if (!quiet)
	    reply(200, "afslog done");
    } else {
	if (!quiet)
	    reply(200, "no AFS present");
    }
}

void
afsunlog(void)
{
    if(k_hasafs())
	k_unlog();
}

#else
int ftpd_afslog_placeholder;
#endif /* KRB5 */
