/*
 * Copyright (c) 1997 - 2001, 2003 Kungliga Tekniska Högskolan
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

#include "kadm5_locl.h"

RCSID("$Id$");

/*
 * Set the keys of `ent' to the string-to-key of `password'
 */

kadm5_ret_t
_kadm5_set_keys(kadm5_server_context *context,
		hdb_entry *ent,
		const char *password)
{
    Key *keys;
    size_t num_keys;
    kadm5_ret_t ret;

    ret = hdb_generate_key_set_password(context->context,
					ent->principal,
					password, &keys, &num_keys);
    if (ret)
	return ret;

    _kadm5_free_keys (context->context, ent->keys.len, ent->keys.val);
    ent->keys.val = keys;
    ent->keys.len = num_keys;

    hdb_entry_set_pw_change_time(context->context, ent, 0);

    if (krb5_config_get_bool_default(context->context, NULL, FALSE,
				     "kadmin", "save-password", NULL))
    {
	ret = hdb_entry_set_password(context->context, context->db,
				     ent, password);
	if (ret)
	    return ret;
    }

    return 0;
}

/*
 * Set the keys of `ent' to (`n_key_data', `key_data')
 */

kadm5_ret_t
_kadm5_set_keys2(kadm5_server_context *context,
		 hdb_entry *ent,
		 int16_t n_key_data,
		 krb5_key_data *key_data)
{
    krb5_error_code ret;
    int i;
    unsigned len;
    Key *keys;

    len  = n_key_data;
    keys = malloc (len * sizeof(*keys));
    if (keys == NULL && len != 0)
	return ENOMEM;

    _kadm5_init_keys (keys, len);

    for(i = 0; i < n_key_data; i++) {
	keys[i].mkvno = NULL;
	keys[i].key.keytype = key_data[i].key_data_type[0];
	ret = krb5_data_copy(&keys[i].key.keyvalue,
			     key_data[i].key_data_contents[0],
			     key_data[i].key_data_length[0]);
	if(ret)
	    goto out;
	if(key_data[i].key_data_ver == 2) {
	    Salt *salt;

	    salt = calloc(1, sizeof(*salt));
	    if(salt == NULL) {
		ret = ENOMEM;
		goto out;
	    }
	    keys[i].salt = salt;
	    salt->type = key_data[i].key_data_type[1];
	    krb5_data_copy(&salt->salt,
			   key_data[i].key_data_contents[1],
			   key_data[i].key_data_length[1]);
	} else
	    keys[i].salt = NULL;
    }
    _kadm5_free_keys (context->context, ent->keys.len, ent->keys.val);
    ent->keys.len = len;
    ent->keys.val = keys;

    hdb_entry_set_pw_change_time(context->context, ent, 0);
    hdb_entry_clear_password(context->context, ent);

    return 0;
 out:
    _kadm5_free_keys (context->context, len, keys);
    return ret;
}

/*
 * Set the keys of `ent' to `n_keys, keys'
 */

kadm5_ret_t
_kadm5_set_keys3(kadm5_server_context *context,
		 hdb_entry *ent,
		 int n_keys,
		 krb5_keyblock *keyblocks)
{
    krb5_error_code ret;
    int i;
    unsigned len;
    Key *keys;

    len  = n_keys;
    keys = malloc (len * sizeof(*keys));
    if (keys == NULL && len != 0)
	return ENOMEM;

    _kadm5_init_keys (keys, len);

    for(i = 0; i < n_keys; i++) {
	keys[i].mkvno = NULL;
	ret = krb5_copy_keyblock_contents (context->context,
					   &keyblocks[i],
					   &keys[i].key);
	if(ret)
	    goto out;
	keys[i].salt = NULL;
    }
    _kadm5_free_keys (context->context, ent->keys.len, ent->keys.val);
    ent->keys.len = len;
    ent->keys.val = keys;

    hdb_entry_set_pw_change_time(context->context, ent, 0);
    hdb_entry_clear_password(context->context, ent);

    return 0;
 out:
    _kadm5_free_keys (context->context, len, keys);
    return ret;
}

/*
 *
 */

static int
is_des_key_p(int keytype)
{
    return keytype == ETYPE_DES_CBC_CRC ||
    	keytype == ETYPE_DES_CBC_MD4 ||
	keytype == ETYPE_DES_CBC_MD5;
}


/*
 * Set the keys of `ent' to random keys and return them in `n_keys'
 * and `new_keys'.
 */

kadm5_ret_t
_kadm5_set_keys_randomly (kadm5_server_context *context,
			  hdb_entry *ent,
			  krb5_keyblock **new_keys,
			  int *n_keys)
{
   krb5_keyblock *kblock = NULL;
   kadm5_ret_t ret = 0;
   int des_keyblock;
   size_t i, num_keys;
   Key *keys;

   ret = hdb_generate_key_set(context->context, ent->principal,
			       &keys, &num_keys, 1);
   if (ret)
	return ret;

   kblock = malloc(num_keys * sizeof(kblock[0]));
   if (kblock == NULL) {
	ret = ENOMEM;
	_kadm5_free_keys (context->context, num_keys, keys);
	return ret;
   }
   memset(kblock, 0, num_keys * sizeof(kblock[0]));

   des_keyblock = -1;
   for (i = 0; i < num_keys; i++) {

	/*
	 * To make sure all des keys are the the same we generate only
	 * the first one and then copy key to all other des keys.
	 */

	if (des_keyblock != -1 && is_des_key_p(keys[i].key.keytype)) {
	    ret = krb5_copy_keyblock_contents (context->context,
					       &kblock[des_keyblock],
					       &kblock[i]);
	    if (ret)
		goto out;
	    kblock[i].keytype = keys[i].key.keytype;
	} else {
	    ret = krb5_generate_random_keyblock (context->context,
						 keys[i].key.keytype,
						 &kblock[i]);
	    if (ret)
		goto out;

	    if (is_des_key_p(keys[i].key.keytype))
		des_keyblock = i;
	}

	ret = krb5_copy_keyblock_contents (context->context,
					   &kblock[i],
					   &keys[i].key);
	if (ret)
	    goto out;
   }

out:
   if(ret) {
	for (i = 0; i < num_keys; ++i)
	    krb5_free_keyblock_contents (context->context, &kblock[i]);
	free(kblock);
	_kadm5_free_keys (context->context, num_keys, keys);
	return ret;
   }

   _kadm5_free_keys (context->context, ent->keys.len, ent->keys.val);
   ent->keys.val = keys;
   ent->keys.len = num_keys;
   *new_keys     = kblock;
   *n_keys       = num_keys;

   hdb_entry_set_pw_change_time(context->context, ent, 0);
   hdb_entry_clear_password(context->context, ent);

   return 0;
}
