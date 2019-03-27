/*
 * Copyright (c) 2000 - 2007 Kungliga Tekniska Högskolan
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

#include "kuser_locl.h"
#include <parse_units.h>

static char *client_principal_str = NULL;
static krb5_principal client_principal;
static char *server_principal_str = NULL;
static krb5_principal server_principal;

static char *ccache_str = NULL;

static char *ticket_flags_str = NULL;
static TicketFlags ticket_flags;
static char *keytab_file = NULL;
static char *enctype_string = NULL;
static int   expiration_time = 3600;
static struct getarg_strings client_addresses;
static int   version_flag = 0;
static int   help_flag = 0;
static int   use_krb5 = 1;

static const char *enc_type = "des-cbc-md5";

/*
 *
 */

static void
encode_ticket (krb5_context context,
	       EncryptionKey *skey,
	       krb5_enctype etype,
	       int skvno,
	       krb5_creds *cred)
{
    size_t len, size;
    char *buf;
    krb5_error_code ret;
    krb5_crypto crypto;
    EncryptedData enc_part;
    EncTicketPart et;
    Ticket ticket;

    memset (&enc_part, 0, sizeof(enc_part));
    memset (&ticket, 0, sizeof(ticket));

    /*
     * Set up `enc_part'
     */

    et.flags = cred->flags.b;
    et.key = cred->session;
    et.crealm = cred->client->realm;
    copy_PrincipalName(&cred->client->name, &et.cname);
    {
	krb5_data empty_string;

	krb5_data_zero(&empty_string);
	et.transited.tr_type = DOMAIN_X500_COMPRESS;
	et.transited.contents = empty_string;
    }
    et.authtime = cred->times.authtime;
    et.starttime = NULL;
    et.endtime = cred->times.endtime;
    et.renew_till = NULL;
    et.caddr = &cred->addresses;
    et.authorization_data = NULL; /* XXX allow random authorization_data */

    /*
     * Encrypt `enc_part' of ticket with service key
     */

    ASN1_MALLOC_ENCODE(EncTicketPart, buf, len, &et, &size, ret);
    if (ret)
	krb5_err(context, 1, ret, "EncTicketPart");

    ret = krb5_crypto_init(context, skey, etype, &crypto);
    if (ret)
	krb5_err(context, 1, ret, "krb5_crypto_init");
    ret = krb5_encrypt_EncryptedData (context,
				      crypto,
				      KRB5_KU_TICKET,
				      buf,
				      len,
				      skvno,
				      &ticket.enc_part);
    if (ret)
	krb5_err(context, 1, ret, "krb5_encrypt_EncryptedData");

    free(buf);
    krb5_crypto_destroy(context, crypto);

    /*
     * Encode ticket
     */

    ticket.tkt_vno = 5;
    ticket.realm = cred->server->realm;
    copy_PrincipalName(&cred->server->name, &ticket.sname);

    ASN1_MALLOC_ENCODE(Ticket, buf, len, &ticket, &size, ret);
    if(ret)
	krb5_err (context, 1, ret, "encode_Ticket");

    krb5_data_copy(&cred->ticket, buf, len);
    free(buf);
}

/*
 *
 */

static int
create_krb5_tickets (krb5_context context, krb5_keytab kt)
{
    krb5_error_code ret;
    krb5_keytab_entry entry;
    krb5_creds cred;
    krb5_enctype etype;
    krb5_ccache ccache;

    memset (&cred, 0, sizeof(cred));

    ret = krb5_string_to_enctype (context, enc_type, &etype);
    if (ret)
	krb5_err (context, 1, ret, "krb5_string_to_enctype");
    ret = krb5_kt_get_entry (context, kt, server_principal,
			     0, etype, &entry);
    if (ret)
	krb5_err (context, 1, ret, "krb5_kt_get_entry");

    /*
     * setup cred
     */


    ret = krb5_copy_principal (context, client_principal, &cred.client);
    if (ret)
	krb5_err (context, 1, ret, "krb5_copy_principal");
    ret = krb5_copy_principal (context, server_principal, &cred.server);
    if (ret)
	krb5_err (context, 1, ret, "krb5_copy_principal");
    krb5_generate_random_keyblock(context, etype, &cred.session);

    cred.times.authtime = time(NULL);
    cred.times.starttime = time(NULL);
    cred.times.endtime = time(NULL) + expiration_time;
    cred.times.renew_till = 0;
    krb5_data_zero(&cred.second_ticket);

    ret = krb5_get_all_client_addrs (context, &cred.addresses);
    if (ret)
	krb5_err (context, 1, ret, "krb5_get_all_client_addrs");
    cred.flags.b = ticket_flags;


    /*
     * Encode encrypted part of ticket
     */

    encode_ticket (context, &entry.keyblock, etype, entry.vno, &cred);

    /*
     * Write to cc
     */

    if (ccache_str) {
	ret = krb5_cc_resolve(context, ccache_str, &ccache);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_cc_resolve");
    } else {
	ret = krb5_cc_default (context, &ccache);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_cc_default");
    }

    ret = krb5_cc_initialize (context, ccache, cred.client);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_initialize");

    ret = krb5_cc_store_cred (context, ccache, &cred);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_store_cred");

    krb5_free_cred_contents (context, &cred);
    krb5_cc_close (context, ccache);

    return 0;
}

/*
 *
 */

static void
setup_env (krb5_context context, krb5_keytab *kt)
{
    krb5_error_code ret;

    if (keytab_file)
	ret = krb5_kt_resolve (context, keytab_file, kt);
    else
	ret = krb5_kt_default (context, kt);
    if (ret)
	krb5_err (context, 1, ret, "resolving keytab");

    if (client_principal_str == NULL)
	krb5_errx (context, 1, "missing client principal");
    ret = krb5_parse_name (context, client_principal_str, &client_principal);
    if (ret)
	krb5_err (context, 1, ret, "resolvning client name");

    if (server_principal_str == NULL)
	krb5_errx (context, 1, "missing server principal");
    ret = krb5_parse_name (context, server_principal_str, &server_principal);
    if (ret)
	krb5_err (context, 1, ret, "resolvning client name");

    if (ticket_flags_str) {
	int ticket_flags_int;

	ticket_flags_int = parse_flags(ticket_flags_str,
				       asn1_TicketFlags_units(), 0);
	if (ticket_flags_int <= 0) {
	    krb5_warnx (context, "bad ticket flags: `%s'", ticket_flags_str);
	    print_flags_table (asn1_TicketFlags_units(), stderr);
	    exit (1);
	}
	if (ticket_flags_int)
	    ticket_flags = int2TicketFlags (ticket_flags_int);
    }
}

/*
 *
 */

struct getargs args[] = {
    { "ccache", 0, arg_string, &ccache_str,
      "name of kerberos 5 credential cache", "cache-name"},
    { "server", 's', arg_string, &server_principal_str,
      "name of server principal", NULL },
    { "client", 'c', arg_string, &client_principal_str,
      "name of client principal", NULL },
    { "keytab", 'k', arg_string, &keytab_file,
      "name of keytab file", NULL },
    { "krb5", '5', arg_flag,	 &use_krb5,
      "create a kerberos 5 ticket", NULL },
    { "expire-time", 'e', arg_integer, &expiration_time,
      "lifetime of ticket in seconds", NULL },
    { "client-addresses", 'a', arg_strings, &client_addresses,
      "addresses of client", NULL },
    { "enc-type", 't', arg_string, 	&enctype_string,
      "encryption type", NULL },
    { "ticket-flags", 'f', arg_string,   &ticket_flags_str,
      "ticket flags for krb5 ticket", NULL },
    { "version", 0,  arg_flag,		&version_flag,	"Print version",
      NULL },
    { "help",	 0,  arg_flag,		&help_flag,	NULL,
      NULL }
};

static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args) / sizeof(args[0]),
		    NULL,
		    "");
    exit (ret);
}

int
main (int argc, char **argv)
{
    int optidx = 0;
    krb5_error_code ret;
    krb5_context context;
    krb5_keytab kt;

    setprogname (argv[0]);

    ret = krb5_init_context (&context);
    if (ret)
	errx(1, "krb5_init_context failed: %u", ret);

    if (getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optidx))
	usage(1);

    if (help_flag)
	usage(0);

    if (version_flag) {
	print_version(NULL);
	return 0;
    }

    if (enctype_string)
	enc_type = enctype_string;

    setup_env(context, &kt);

    if (use_krb5)
	create_krb5_tickets(context, kt);

    krb5_kt_close(context, kt);

    return 0;
}
