/*
 * Copyright (c) 2004 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

extern int local_flag;

int
stash(struct stash_options *opt, int argc, char **argv)
{
    char buf[1024];
    krb5_error_code ret;
    krb5_enctype enctype;
    hdb_master_key mkey;

    if(!local_flag) {
	krb5_warnx(context, "stash is only available in local (-l) mode");
	return 0;
    }

    ret = krb5_string_to_enctype(context, opt->enctype_string, &enctype);
    if(ret) {
	krb5_warn(context, ret, "%s", opt->enctype_string);
	return 0;
    }

    if(opt->key_file_string == NULL) {
	asprintf(&opt->key_file_string, "%s/m-key", hdb_db_dir(context));
	if (opt->key_file_string == NULL)
	    errx(1, "out of memory");
    }

    ret = hdb_read_master_key(context, opt->key_file_string, &mkey);
    if(ret && ret != ENOENT) {
	krb5_warn(context, ret, "reading master key from %s",
		  opt->key_file_string);
	return 0;
    }

    if (opt->convert_file_flag) {
	if (ret)
	    krb5_warn(context, ret, "reading master key from %s",
		      opt->key_file_string);
	return 0;
    } else {
	krb5_keyblock key;
	krb5_salt salt;
	salt.salttype = KRB5_PW_SALT;
	/* XXX better value? */
	salt.saltvalue.data = NULL;
	salt.saltvalue.length = 0;
	if(opt->master_key_fd_integer != -1) {
	    ssize_t n;
	    n = read(opt->master_key_fd_integer, buf, sizeof(buf));
	    if(n == 0)
		krb5_warnx(context, "end of file reading passphrase");
	    else if(n < 0) {
		krb5_warn(context, errno, "reading passphrase");
		n = 0;
	    }
	    buf[n] = '\0';
	    buf[strcspn(buf, "\r\n")] = '\0';
	} else if (opt->random_password_flag) {
	    random_password (buf, sizeof(buf));
	    printf("Using random master stash password: %s\n", buf);
	} else {
	    if(UI_UTIL_read_pw_string(buf, sizeof(buf), "Master key: ", 1)) {
		hdb_free_master_key(context, mkey);
		return 0;
	    }
	}
	ret = krb5_string_to_key_salt(context, enctype, buf, salt, &key);
	ret = hdb_add_master_key(context, &key, &mkey);
	krb5_free_keyblock_contents(context, &key);
    }

    {
	char *new, *old;
	asprintf(&old, "%s.old", opt->key_file_string);
	asprintf(&new, "%s.new", opt->key_file_string);
	if(old == NULL || new == NULL) {
	    ret = ENOMEM;
	    goto out;
	}

	if(unlink(new) < 0 && errno != ENOENT) {
	    ret = errno;
	    goto out;
	}
	krb5_warnx(context, "writing key to \"%s\"", opt->key_file_string);
	ret = hdb_write_master_key(context, new, mkey);
	if(ret)
	    unlink(new);
	else {
	    unlink(old);
#ifndef NO_POSIX_LINKS
	    if(link(opt->key_file_string, old) < 0 && errno != ENOENT) {
		ret = errno;
		unlink(new);
	    } else {
#endif
		if(rename(new, opt->key_file_string) < 0) {
		    ret = errno;
		}
#ifndef NO_POSIX_LINKS
	    }
#endif
	}
    out:
	free(old);
	free(new);
	if(ret)
	    krb5_warn(context, errno, "writing master key file");
    }

    hdb_free_master_key(context, mkey);
    return 0;
}
