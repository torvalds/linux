/*
 * Copyright (c) 1997-2004 Kungliga Tekniska Högskolan
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

#include "kadmin_locl.h"
#include "kadmin-commands.h"
#include <kadm5/private.h>

extern int local_flag;

int
dump(struct dump_options *opt, int argc, char **argv)
{
    krb5_error_code ret;
    FILE *f;
    HDB *db = NULL;

    if(!local_flag) {
	krb5_warnx(context, "dump is only available in local (-l) mode");
	return 0;
    }

    db = _kadm5_s_get_db(kadm_handle);

    if(argc == 0)
	f = stdout;
    else
	f = fopen(argv[0], "w");

    if(f == NULL) {
	krb5_warn(context, errno, "open: %s", argv[0]);
	goto out;
    }
    ret = db->hdb_open(context, db, O_RDONLY, 0600);
    if(ret) {
	krb5_warn(context, ret, "hdb_open");
	goto out;
    }

    hdb_foreach(context, db, opt->decrypt_flag ? HDB_F_DECRYPT : 0,
		hdb_print_entry, f);

    db->hdb_close(context, db);
out:
    if(f && f != stdout)
	fclose(f);
    return 0;
}
