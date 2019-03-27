/*
 * Copyright (c) 1997-2003 Kungliga Tekniska Högskolan
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

#include "headers.h"
#include <getarg.h>

int version5;
int version4;
int afs;
char *principal;
char *cell;
char *password;
const char *keytype_str = "des3-cbc-sha1";
int version;
int help;

struct getargs args[] = {
    { "version5", '5', arg_flag,   &version5, "Output Kerberos v5 string-to-key",
	NULL },
    { "version4", '4', arg_flag,   &version4, "Output Kerberos v4 string-to-key",
	NULL },
    { "afs",      'a', arg_flag,   &afs, "Output AFS string-to-key", NULL },
    { "cell",     'c', arg_string, &cell, "AFS cell to use", "cell" },
    { "password", 'w', arg_string, &password, "Password to use", "password" },
    { "principal",'p', arg_string, &principal, "Kerberos v5 principal to use", "principal" },
    { "keytype",  'k', arg_string, rk_UNCONST(&keytype_str), "Keytype", NULL },
    { "version",    0, arg_flag,   &version, "print version", NULL },
    { "help",       0, arg_flag,   &help, NULL, NULL }
};

int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(int status)
{
    arg_printusage (args, num_args, NULL, "password");
    exit(status);
}

static void
tokey(krb5_context context,
      krb5_enctype enctype,
      const char *pw,
      krb5_salt salt,
      const char *label)
{
    krb5_error_code ret;
    size_t i;
    krb5_keyblock key;
    char *e;

    ret = krb5_string_to_key_salt(context, enctype, pw, salt, &key);
    if (ret)
	krb5_err(context, 1, ret, "krb5_string_to_key_salt");
    ret = krb5_enctype_to_string(context, enctype, &e);
    if (ret)
	krb5_err(context, 1, ret, "krb5_enctype_to_string");
    printf(label, e);
    printf(": ");
    for(i = 0; i < key.keyvalue.length; i++)
	printf("%02x", ((unsigned char*)key.keyvalue.data)[i]);
    printf("\n");
    krb5_free_keyblock_contents(context, &key);
    free(e);
}

int
main(int argc, char **argv)
{
    krb5_context context;
    krb5_principal princ;
    krb5_salt salt;
    int optidx;
    char buf[1024];
    krb5_enctype etype;
    krb5_error_code ret;

    optidx = krb5_program_setup(&context, argc, argv, args, num_args, NULL);

    if(help)
	usage(0);

    if(version){
	print_version (NULL);
	return 0;
    }

    argc -= optidx;
    argv += optidx;

    if (argc > 1)
	usage(1);

    if(!version5 && !version4 && !afs)
	version5 = 1;

    ret = krb5_string_to_enctype(context, keytype_str, &etype);
    if(ret)
	krb5_err(context, 1, ret, "krb5_string_to_enctype");

    if((etype != ETYPE_DES_CBC_CRC &&
	etype != ETYPE_DES_CBC_MD4 &&
	etype != ETYPE_DES_CBC_MD5) &&
       (afs || version4)) {
	if(!version5) {
	    etype = ETYPE_DES_CBC_CRC;
	} else {
	    krb5_errx(context, 1,
		      "DES is the only valid keytype for AFS and Kerberos 4");
	}
    }

    if(version5 && principal == NULL){
	printf("Kerberos v5 principal: ");
	if(fgets(buf, sizeof(buf), stdin) == NULL)
	    return 1;
	buf[strcspn(buf, "\r\n")] = '\0';
	principal = estrdup(buf);
    }
    if(afs && cell == NULL){
	printf("AFS cell: ");
	if(fgets(buf, sizeof(buf), stdin) == NULL)
	    return 1;
	buf[strcspn(buf, "\r\n")] = '\0';
	cell = estrdup(buf);
    }
    if(argv[0])
	password = argv[0];
    if(password == NULL){
	if(UI_UTIL_read_pw_string(buf, sizeof(buf), "Password: ", 0))
	    return 1;
	password = buf;
    }

    if(version5){
	krb5_parse_name(context, principal, &princ);
	krb5_get_pw_salt(context, princ, &salt);
	tokey(context, etype, password, salt, "Kerberos 5 (%s)");
	krb5_free_salt(context, salt);
    }
    if(version4){
	salt.salttype = KRB5_PW_SALT;
	salt.saltvalue.length = 0;
	salt.saltvalue.data = NULL;
	tokey(context, ETYPE_DES_CBC_MD5, password, salt, "Kerberos 4");
    }
    if(afs){
	salt.salttype = KRB5_AFS3_SALT;
	salt.saltvalue.length = strlen(cell);
	salt.saltvalue.data = cell;
	tokey(context, ETYPE_DES_CBC_MD5, password, salt, "AFS");
    }
    return 0;
}
