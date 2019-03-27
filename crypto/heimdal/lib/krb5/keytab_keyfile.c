/*
 * Copyright (c) 1997 - 2007 Kungliga Tekniska Högskolan
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

#ifndef HEIMDAL_SMALLER

/* afs keyfile operations --------------------------------------- */

/*
 * Minimum tools to handle the AFS KeyFile.
 *
 * Format of the KeyFile is:
 * <int32_t numkeys> {[<int32_t kvno> <char[8] deskey>] * numkeys}
 *
 * It just adds to the end of the keyfile, deleting isn't implemented.
 * Use your favorite text/hex editor to delete keys.
 *
 */

#define AFS_SERVERTHISCELL "/usr/afs/etc/ThisCell"
#define AFS_SERVERMAGICKRBCONF "/usr/afs/etc/krb.conf"

struct akf_data {
    uint32_t num_entries;
    char *filename;
    char *cell;
    char *realm;
};

/*
 * set `d->cell' and `d->realm'
 */

static int
get_cell_and_realm (krb5_context context, struct akf_data *d)
{
    FILE *f;
    char buf[BUFSIZ], *cp;
    int ret;

    f = fopen (AFS_SERVERTHISCELL, "r");
    if (f == NULL) {
	ret = errno;
	krb5_set_error_message (context, ret,
				N_("Open ThisCell %s: %s", ""),
				AFS_SERVERTHISCELL,
				strerror(ret));
	return ret;
    }
    if (fgets (buf, sizeof(buf), f) == NULL) {
	fclose (f);
	krb5_set_error_message (context, EINVAL,
				N_("No cell in ThisCell file %s", ""),
				AFS_SERVERTHISCELL);
	return EINVAL;
    }
    buf[strcspn(buf, "\n")] = '\0';
    fclose(f);

    d->cell = strdup (buf);
    if (d->cell == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    f = fopen (AFS_SERVERMAGICKRBCONF, "r");
    if (f != NULL) {
	if (fgets (buf, sizeof(buf), f) == NULL) {
	    free (d->cell);
	    d->cell = NULL;
	    fclose (f);
	    krb5_set_error_message (context, EINVAL,
				    N_("No realm in ThisCell file %s", ""),
				    AFS_SERVERMAGICKRBCONF);
	    return EINVAL;
	}
	buf[strcspn(buf, "\n")] = '\0';
	fclose(f);
    }
    /* uppercase */
    for (cp = buf; *cp != '\0'; cp++)
	*cp = toupper((unsigned char)*cp);

    d->realm = strdup (buf);
    if (d->realm == NULL) {
	free (d->cell);
	d->cell = NULL;
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

/*
 * init and get filename
 */

static krb5_error_code KRB5_CALLCONV
akf_resolve(krb5_context context, const char *name, krb5_keytab id)
{
    int ret;
    struct akf_data *d = malloc(sizeof (struct akf_data));

    if (d == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    d->num_entries = 0;
    ret = get_cell_and_realm (context, d);
    if (ret) {
	free (d);
	return ret;
    }
    d->filename = strdup (name);
    if (d->filename == NULL) {
	free (d->cell);
	free (d->realm);
	free (d);
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    id->data = d;

    return 0;
}

/*
 * cleanup
 */

static krb5_error_code KRB5_CALLCONV
akf_close(krb5_context context, krb5_keytab id)
{
    struct akf_data *d = id->data;

    free (d->filename);
    free (d->cell);
    free (d);
    return 0;
}

/*
 * Return filename
 */

static krb5_error_code KRB5_CALLCONV
akf_get_name(krb5_context context,
	     krb5_keytab id,
	     char *name,
	     size_t name_sz)
{
    struct akf_data *d = id->data;

    strlcpy (name, d->filename, name_sz);
    return 0;
}

/*
 * Init
 */

static krb5_error_code KRB5_CALLCONV
akf_start_seq_get(krb5_context context,
		  krb5_keytab id,
		  krb5_kt_cursor *c)
{
    int32_t ret;
    struct akf_data *d = id->data;

    c->fd = open (d->filename, O_RDONLY | O_BINARY | O_CLOEXEC, 0600);
    if (c->fd < 0) {
	ret = errno;
	krb5_set_error_message(context, ret,
			       N_("keytab afs keyfile open %s failed: %s", ""),
			       d->filename, strerror(ret));
	return ret;
    }

    c->data = NULL;
    c->sp = krb5_storage_from_fd(c->fd);
    if (c->sp == NULL) {
	close(c->fd);
	krb5_clear_error_message (context);
	return KRB5_KT_NOTFOUND;
    }
    krb5_storage_set_eof_code(c->sp, KRB5_KT_END);

    ret = krb5_ret_uint32(c->sp, &d->num_entries);
    if(ret || d->num_entries > INT_MAX / 8) {
	krb5_storage_free(c->sp);
	close(c->fd);
	krb5_clear_error_message (context);
	if(ret == KRB5_KT_END)
	    return KRB5_KT_NOTFOUND;
	return ret;
    }

    return 0;
}

static krb5_error_code KRB5_CALLCONV
akf_next_entry(krb5_context context,
	       krb5_keytab id,
	       krb5_keytab_entry *entry,
	       krb5_kt_cursor *cursor)
{
    struct akf_data *d = id->data;
    int32_t kvno;
    off_t pos;
    int ret;

    pos = krb5_storage_seek(cursor->sp, 0, SEEK_CUR);

    if ((pos - 4) / (4 + 8) >= d->num_entries)
	return KRB5_KT_END;

    ret = krb5_make_principal (context, &entry->principal,
			       d->realm, "afs", d->cell, NULL);
    if (ret)
	goto out;

    ret = krb5_ret_int32(cursor->sp, &kvno);
    if (ret) {
	krb5_free_principal (context, entry->principal);
	goto out;
    }

    entry->vno = kvno;

    if (cursor->data)
	entry->keyblock.keytype         = ETYPE_DES_CBC_MD5;
    else
	entry->keyblock.keytype         = ETYPE_DES_CBC_CRC;
    entry->keyblock.keyvalue.length = 8;
    entry->keyblock.keyvalue.data   = malloc (8);
    if (entry->keyblock.keyvalue.data == NULL) {
	krb5_free_principal (context, entry->principal);
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	ret = ENOMEM;
	goto out;
    }

    ret = krb5_storage_read(cursor->sp, entry->keyblock.keyvalue.data, 8);
    if(ret != 8)
	ret = (ret < 0) ? errno : KRB5_KT_END;
    else
	ret = 0;

    entry->timestamp = time(NULL);
    entry->flags = 0;
    entry->aliases = NULL;

 out:
    if (cursor->data) {
	krb5_storage_seek(cursor->sp, pos + 4 + 8, SEEK_SET);
	cursor->data = NULL;
    } else
	cursor->data = cursor;
    return ret;
}

static krb5_error_code KRB5_CALLCONV
akf_end_seq_get(krb5_context context,
		krb5_keytab id,
		krb5_kt_cursor *cursor)
{
    krb5_storage_free(cursor->sp);
    close(cursor->fd);
    cursor->data = NULL;
    return 0;
}

static krb5_error_code KRB5_CALLCONV
akf_add_entry(krb5_context context,
	      krb5_keytab id,
	      krb5_keytab_entry *entry)
{
    struct akf_data *d = id->data;
    int fd, created = 0;
    krb5_error_code ret;
    int32_t len;
    krb5_storage *sp;


    if (entry->keyblock.keyvalue.length != 8)
	return 0;
    switch(entry->keyblock.keytype) {
    case ETYPE_DES_CBC_CRC:
    case ETYPE_DES_CBC_MD4:
    case ETYPE_DES_CBC_MD5:
	break;
    default:
	return 0;
    }

    fd = open (d->filename, O_RDWR | O_BINARY | O_CLOEXEC);
    if (fd < 0) {
	fd = open (d->filename,
		   O_RDWR | O_BINARY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0) {
	    ret = errno;
	    krb5_set_error_message(context, ret,
				   N_("open keyfile(%s): %s", ""),
				   d->filename,
				   strerror(ret));
	    return ret;
	}
	created = 1;
    }

    sp = krb5_storage_from_fd(fd);
    if(sp == NULL) {
	close(fd);
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    if (created)
	len = 0;
    else {
	if(krb5_storage_seek(sp, 0, SEEK_SET) < 0) {
	    ret = errno;
	    krb5_storage_free(sp);
	    close(fd);
	    krb5_set_error_message(context, ret,
				   N_("seeking in keyfile: %s", ""),
				   strerror(ret));
	    return ret;
	}

	ret = krb5_ret_int32(sp, &len);
	if(ret) {
	    krb5_storage_free(sp);
	    close(fd);
	    return ret;
	}
    }

    /*
     * Make sure we don't add the entry twice, assumes the DES
     * encryption types are all the same key.
     */
    if (len > 0) {
	int32_t kvno;
	int i;

	for (i = 0; i < len; i++) {
	    ret = krb5_ret_int32(sp, &kvno);
	    if (ret) {
		krb5_set_error_message (context, ret,
					N_("Failed getting kvno from keyfile", ""));
		goto out;
	    }
	    if(krb5_storage_seek(sp, 8, SEEK_CUR) < 0) {
		ret = errno;
		krb5_set_error_message (context, ret,
					N_("Failed seeing in keyfile: %s", ""),
					strerror(ret));
		goto out;
	    }
	    if (kvno == entry->vno) {
		ret = 0;
		goto out;
	    }
	}
    }

    len++;

    if(krb5_storage_seek(sp, 0, SEEK_SET) < 0) {
	ret = errno;
	krb5_set_error_message (context, ret,
				N_("Failed seeing in keyfile: %s", ""),
				strerror(ret));
	goto out;
    }

    ret = krb5_store_int32(sp, len);
    if(ret) {
	ret = errno;
	krb5_set_error_message (context, ret,
				N_("keytab keyfile failed new length", ""));
	return ret;
    }

    if(krb5_storage_seek(sp, (len - 1) * (8 + 4), SEEK_CUR) < 0) {
	ret = errno;
	krb5_set_error_message (context, ret,
				N_("seek to end: %s", ""), strerror(ret));
	goto out;
    }

    ret = krb5_store_int32(sp, entry->vno);
    if(ret) {
	krb5_set_error_message(context, ret,
			       N_("keytab keyfile failed store kvno", ""));
	goto out;
    }
    ret = krb5_storage_write(sp, entry->keyblock.keyvalue.data,
			     entry->keyblock.keyvalue.length);
    if(ret != entry->keyblock.keyvalue.length) {
	if (ret < 0)
	    ret = errno;
	else
	    ret = ENOTTY;
	krb5_set_error_message(context, ret,
			       N_("keytab keyfile failed to add key", ""));
	goto out;
    }
    ret = 0;
out:
    krb5_storage_free(sp);
    close (fd);
    return ret;
}

const krb5_kt_ops krb5_akf_ops = {
    "AFSKEYFILE",
    akf_resolve,
    akf_get_name,
    akf_close,
    NULL, /* destroy */
    NULL, /* get */
    akf_start_seq_get,
    akf_next_entry,
    akf_end_seq_get,
    akf_add_entry,
    NULL /* remove */
};

#endif /* HEIMDAL_SMALLER */
