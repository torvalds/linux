/*
 * Copyright (c) 1999 - 2005 Kungliga Tekniska Högskolan
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
#include <getarg.h>
#include <parse_bytes.h>
#include <err.h>

/* verify krb5.conf */

static int dumpconfig_flag = 0;
static int version_flag = 0;
static int help_flag	= 0;
static int warn_mit_syntax_flag = 0;

static struct getargs args[] = {
    {"dumpconfig", 0,      arg_flag,       &dumpconfig_flag,
     "show the parsed config files", NULL },
    {"warn-mit-syntax", 0, arg_flag,       &warn_mit_syntax_flag,
     "show the parsed config files", NULL },
    {"version",	0,	arg_flag,	&version_flag,
     "print version", NULL },
    {"help",	0,	arg_flag,	&help_flag,
     NULL, NULL }
};

static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args)/sizeof(*args),
		    NULL,
		    "[config-file]");
    exit (ret);
}

static int
check_bytes(krb5_context context, const char *path, char *data)
{
    if(parse_bytes(data, NULL) == -1) {
	krb5_warnx(context, "%s: failed to parse \"%s\" as size", path, data);
	return 1;
    }
    return 0;
}

static int
check_time(krb5_context context, const char *path, char *data)
{
    if(parse_time(data, NULL) == -1) {
	krb5_warnx(context, "%s: failed to parse \"%s\" as time", path, data);
	return 1;
    }
    return 0;
}

static int
check_numeric(krb5_context context, const char *path, char *data)
{
    long v;
    char *end;
    v = strtol(data, &end, 0);

    if ((v == LONG_MIN || v == LONG_MAX) && errno != 0) {
	krb5_warnx(context, "%s: over/under flow for \"%s\"",
		   path, data);
	return 1;
    }
    if(*end != '\0') {
	krb5_warnx(context, "%s: failed to parse \"%s\" as a number",
		   path, data);
	return 1;
    }
    return 0;
}

static int
check_boolean(krb5_context context, const char *path, char *data)
{
    long int v;
    char *end;
    if(strcasecmp(data, "yes") == 0 ||
       strcasecmp(data, "true") == 0 ||
       strcasecmp(data, "no") == 0 ||
       strcasecmp(data, "false") == 0)
	return 0;
    v = strtol(data, &end, 0);
    if(*end != '\0') {
	krb5_warnx(context, "%s: failed to parse \"%s\" as a boolean",
		   path, data);
	return 1;
    }
    if(v != 0 && v != 1)
	krb5_warnx(context, "%s: numeric value \"%s\" is treated as \"true\"",
		   path, data);
    return 0;
}

static int
check_524(krb5_context context, const char *path, char *data)
{
    if(strcasecmp(data, "yes") == 0 ||
       strcasecmp(data, "no") == 0 ||
       strcasecmp(data, "2b") == 0 ||
       strcasecmp(data, "local") == 0)
	return 0;

    krb5_warnx(context, "%s: didn't contain a valid option `%s'",
	       path, data);
    return 1;
}

static int
check_host(krb5_context context, const char *path, char *data)
{
    int ret;
    char hostname[128];
    const char *p = data;
    struct addrinfo hints;
    char service[32];
    int defport;
    struct addrinfo *ai;

    hints.ai_flags = 0;
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = 0;
    hints.ai_protocol = 0;

    hints.ai_addrlen = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    /* XXX data could be a list of hosts that this code can't handle */
    /* XXX copied from krbhst.c */
    if(strncmp(p, "http://", 7) == 0){
        p += 7;
	hints.ai_socktype = SOCK_STREAM;
	strlcpy(service, "http", sizeof(service));
	defport = 80;
    } else if(strncmp(p, "http/", 5) == 0) {
        p += 5;
	hints.ai_socktype = SOCK_STREAM;
	strlcpy(service, "http", sizeof(service));
	defport = 80;
    }else if(strncmp(p, "tcp/", 4) == 0){
        p += 4;
	hints.ai_socktype = SOCK_STREAM;
	strlcpy(service, "kerberos", sizeof(service));
	defport = 88;
    } else if(strncmp(p, "udp/", 4) == 0) {
        p += 4;
	hints.ai_socktype = SOCK_DGRAM;
	strlcpy(service, "kerberos", sizeof(service));
	defport = 88;
    } else {
	hints.ai_socktype = SOCK_DGRAM;
	strlcpy(service, "kerberos", sizeof(service));
	defport = 88;
    }
    if(strsep_copy(&p, ":", hostname, sizeof(hostname)) < 0) {
	return 1;
    }
    hostname[strcspn(hostname, "/")] = '\0';
    if(p != NULL) {
	char *end;
	int tmp = strtol(p, &end, 0);
	if(end == p) {
	    krb5_warnx(context, "%s: failed to parse port number in %s",
		       path, data);
	    return 1;
	}
	defport = tmp;
	snprintf(service, sizeof(service), "%u", defport);
    }
    ret = getaddrinfo(hostname, service, &hints, &ai);
    if(ret == EAI_SERVICE && !isdigit((unsigned char)service[0])) {
	snprintf(service, sizeof(service), "%u", defport);
	ret = getaddrinfo(hostname, service, &hints, &ai);
    }
    if(ret != 0) {
	krb5_warnx(context, "%s: %s (%s)", path, gai_strerror(ret), hostname);
	return 1;
    }
    return 0;
}

static int
mit_entry(krb5_context context, const char *path, char *data)
{
    if (warn_mit_syntax_flag)
	krb5_warnx(context, "%s is only used by MIT Kerberos", path);
    return 0;
}

struct s2i {
    const char *s;
    int val;
};

#define L(X) { #X, LOG_ ## X }

static struct s2i syslogvals[] = {
    /* severity */
    L(EMERG),
    L(ALERT),
    L(CRIT),
    L(ERR),
    L(WARNING),
    L(NOTICE),
    L(INFO),
    L(DEBUG),
    /* facility */
    L(AUTH),
#ifdef LOG_AUTHPRIV
    L(AUTHPRIV),
#endif
#ifdef LOG_CRON
    L(CRON),
#endif
    L(DAEMON),
#ifdef LOG_FTP
    L(FTP),
#endif
    L(KERN),
    L(LPR),
    L(MAIL),
#ifdef LOG_NEWS
    L(NEWS),
#endif
    L(SYSLOG),
    L(USER),
#ifdef LOG_UUCP
    L(UUCP),
#endif
    L(LOCAL0),
    L(LOCAL1),
    L(LOCAL2),
    L(LOCAL3),
    L(LOCAL4),
    L(LOCAL5),
    L(LOCAL6),
    L(LOCAL7),
    { NULL, -1 }
};

static int
find_value(const char *s, struct s2i *table)
{
    while(table->s && strcasecmp(table->s, s))
	table++;
    return table->val;
}

static int
check_log(krb5_context context, const char *path, char *data)
{
    /* XXX sync with log.c */
    int min = 0, max = -1, n;
    char c;
    const char *p = data;

    n = sscanf(p, "%d%c%d/", &min, &c, &max);
    if(n == 2){
	if(c == '/') {
	    if(min < 0){
		max = -min;
		min = 0;
	    }else{
		max = min;
	    }
	}
    }
    if(n){
	p = strchr(p, '/');
	if(p == NULL) {
	    krb5_warnx(context, "%s: failed to parse \"%s\"", path, data);
	    return 1;
	}
	p++;
    }
    if(strcmp(p, "STDERR") == 0 ||
       strcmp(p, "CONSOLE") == 0 ||
       (strncmp(p, "FILE", 4) == 0 && (p[4] == ':' || p[4] == '=')) ||
       (strncmp(p, "DEVICE", 6) == 0 && p[6] == '='))
	return 0;
    if(strncmp(p, "SYSLOG", 6) == 0){
	int ret = 0;
	char severity[128] = "";
	char facility[128] = "";
	p += 6;
	if(*p != '\0')
	    p++;
	if(strsep_copy(&p, ":", severity, sizeof(severity)) != -1)
	    strsep_copy(&p, ":", facility, sizeof(facility));
	if(*severity == '\0')
	    strlcpy(severity, "ERR", sizeof(severity));
 	if(*facility == '\0')
	    strlcpy(facility, "AUTH", sizeof(facility));
	if(find_value(severity, syslogvals) == -1) {
	    krb5_warnx(context, "%s: unknown syslog facility \"%s\"",
		       path, facility);
	    ret++;
	}
	if(find_value(severity, syslogvals) == -1) {
	    krb5_warnx(context, "%s: unknown syslog severity \"%s\"",
		       path, severity);
	    ret++;
	}
	return ret;
    }else{
	krb5_warnx(context, "%s: unknown log type: \"%s\"", path, data);
	return 1;
    }
}

typedef int (*check_func_t)(krb5_context, const char*, char*);
struct entry {
    const char *name;
    int type;
    void *check_data;
    int deprecated;
};

struct entry all_strings[] = {
    { "", krb5_config_string, NULL },
    { NULL }
};

struct entry all_boolean[] = {
    { "", krb5_config_string, check_boolean },
    { NULL }
};


struct entry v4_name_convert_entries[] = {
    { "host", krb5_config_list, all_strings },
    { "plain", krb5_config_list, all_strings },
    { NULL }
};

struct entry libdefaults_entries[] = {
    { "accept_null_addresses", krb5_config_string, check_boolean },
    { "allow_weak_crypto", krb5_config_string, check_boolean },
    { "capath", krb5_config_list, all_strings, 1 },
    { "check_pac", krb5_config_string, check_boolean },
    { "clockskew", krb5_config_string, check_time },
    { "date_format", krb5_config_string, NULL },
    { "default_cc_name", krb5_config_string, NULL },
    { "default_etypes", krb5_config_string, NULL },
    { "default_etypes_des", krb5_config_string, NULL },
    { "default_keytab_modify_name", krb5_config_string, NULL },
    { "default_keytab_name", krb5_config_string, NULL },
    { "default_realm", krb5_config_string, NULL },
    { "dns_canonize_hostname", krb5_config_string, check_boolean },
    { "dns_proxy", krb5_config_string, NULL },
    { "dns_lookup_kdc", krb5_config_string, check_boolean },
    { "dns_lookup_realm", krb5_config_string, check_boolean },
    { "dns_lookup_realm_labels", krb5_config_string, NULL },
    { "egd_socket", krb5_config_string, NULL },
    { "encrypt", krb5_config_string, check_boolean },
    { "extra_addresses", krb5_config_string, NULL },
    { "fcache_version", krb5_config_string, check_numeric },
    { "fcc-mit-ticketflags", krb5_config_string, check_boolean },
    { "forward", krb5_config_string, check_boolean },
    { "forwardable", krb5_config_string, check_boolean },
    { "http_proxy", krb5_config_string, check_host /* XXX */ },
    { "ignore_addresses", krb5_config_string, NULL },
    { "kdc_timeout", krb5_config_string, check_time },
    { "kdc_timesync", krb5_config_string, check_boolean },
    { "log_utc", krb5_config_string, check_boolean },
    { "maxretries", krb5_config_string, check_numeric },
    { "scan_interfaces", krb5_config_string, check_boolean },
    { "srv_lookup", krb5_config_string, check_boolean },
    { "srv_try_txt", krb5_config_string, check_boolean },
    { "ticket_lifetime", krb5_config_string, check_time },
    { "time_format", krb5_config_string, NULL },
    { "transited_realms_reject", krb5_config_string, NULL },
    { "no-addresses", krb5_config_string, check_boolean },
    { "v4_instance_resolve", krb5_config_string, check_boolean },
    { "v4_name_convert", krb5_config_list, v4_name_convert_entries },
    { "verify_ap_req_nofail", krb5_config_string, check_boolean },
    { "max_retries", krb5_config_string, check_time },
    { "renew_lifetime", krb5_config_string, check_time },
    { "proxiable", krb5_config_string, check_boolean },
    { "warn_pwexpire", krb5_config_string, check_time },
    /* MIT stuff */
    { "permitted_enctypes", krb5_config_string, mit_entry },
    { "default_tgs_enctypes", krb5_config_string, mit_entry },
    { "default_tkt_enctypes", krb5_config_string, mit_entry },
    { NULL }
};

struct entry appdefaults_entries[] = {
    { "afslog", krb5_config_string, check_boolean },
    { "afs-use-524", krb5_config_string, check_524 },
    { "encrypt", krb5_config_string, check_boolean },
    { "forward", krb5_config_string, check_boolean },
    { "forwardable", krb5_config_string, check_boolean },
    { "proxiable", krb5_config_string, check_boolean },
    { "ticket_lifetime", krb5_config_string, check_time },
    { "renew_lifetime", krb5_config_string, check_time },
    { "no-addresses", krb5_config_string, check_boolean },
    { "krb4_get_tickets", krb5_config_string, check_boolean },
    { "pkinit_anchors", krb5_config_string, NULL },
    { "pkinit_win2k", krb5_config_string, NULL },
    { "pkinit_win2k_require_binding", krb5_config_string, NULL },
    { "pkinit_require_eku", krb5_config_string, NULL },
    { "pkinit_require_krbtgt_otherName", krb5_config_string, NULL },
    { "pkinit_require_hostname_match", krb5_config_string, NULL },
#if 0
    { "anonymous", krb5_config_string, check_boolean },
#endif
    { "", krb5_config_list, appdefaults_entries },
    { NULL }
};

struct entry realms_entries[] = {
    { "forwardable", krb5_config_string, check_boolean },
    { "proxiable", krb5_config_string, check_boolean },
    { "ticket_lifetime", krb5_config_string, check_time },
    { "renew_lifetime", krb5_config_string, check_time },
    { "warn_pwexpire", krb5_config_string, check_time },
    { "kdc", krb5_config_string, check_host },
    { "admin_server", krb5_config_string, check_host },
    { "kpasswd_server", krb5_config_string, check_host },
    { "krb524_server", krb5_config_string, check_host },
    { "v4_name_convert", krb5_config_list, v4_name_convert_entries },
    { "v4_instance_convert", krb5_config_list, all_strings },
    { "v4_domains", krb5_config_string, NULL },
    { "default_domain", krb5_config_string, NULL },
    { "win2k_pkinit", krb5_config_string, NULL },
    /* MIT stuff */
    { "admin_keytab", krb5_config_string, mit_entry },
    { "acl_file", krb5_config_string, mit_entry },
    { "dict_file", krb5_config_string, mit_entry },
    { "kadmind_port", krb5_config_string, mit_entry },
    { "kpasswd_port", krb5_config_string, mit_entry },
    { "master_key_name", krb5_config_string, mit_entry },
    { "master_key_type", krb5_config_string, mit_entry },
    { "key_stash_file", krb5_config_string, mit_entry },
    { "max_life", krb5_config_string, mit_entry },
    { "max_renewable_life", krb5_config_string, mit_entry },
    { "default_principal_expiration", krb5_config_string, mit_entry },
    { "default_principal_flags", krb5_config_string, mit_entry },
    { "supported_enctypes", krb5_config_string, mit_entry },
    { "database_name", krb5_config_string, mit_entry },
    { NULL }
};

struct entry realms_foobar[] = {
    { "", krb5_config_list, realms_entries },
    { NULL }
};


struct entry kdc_database_entries[] = {
    { "realm", krb5_config_string, NULL },
    { "dbname", krb5_config_string, NULL },
    { "mkey_file", krb5_config_string, NULL },
    { "acl_file", krb5_config_string, NULL },
    { "log_file", krb5_config_string, NULL },
    { NULL }
};

struct entry kdc_entries[] = {
    { "database", krb5_config_list, kdc_database_entries },
    { "key-file", krb5_config_string, NULL },
    { "logging", krb5_config_string, check_log },
    { "max-request", krb5_config_string, check_bytes },
    { "require-preauth", krb5_config_string, check_boolean },
    { "ports", krb5_config_string, NULL },
    { "addresses", krb5_config_string, NULL },
    { "enable-kerberos4", krb5_config_string, check_boolean },
    { "enable-524", krb5_config_string, check_boolean },
    { "enable-http", krb5_config_string, check_boolean },
    { "check-ticket-addresses", krb5_config_string, check_boolean },
    { "allow-null-ticket-addresses", krb5_config_string, check_boolean },
    { "allow-anonymous", krb5_config_string, check_boolean },
    { "v4_realm", krb5_config_string, NULL },
    { "enable-kaserver", krb5_config_string, check_boolean, 1 },
    { "encode_as_rep_as_tgs_rep", krb5_config_string, check_boolean },
    { "kdc_warn_pwexpire", krb5_config_string, check_time },
    { "use_2b", krb5_config_list, NULL },
    { "enable-pkinit", krb5_config_string, check_boolean },
    { "pkinit_identity", krb5_config_string, NULL },
    { "pkinit_anchors", krb5_config_string, NULL },
    { "pkinit_pool", krb5_config_string, NULL },
    { "pkinit_revoke", krb5_config_string, NULL },
    { "pkinit_kdc_ocsp", krb5_config_string, NULL },
    { "pkinit_principal_in_certificate", krb5_config_string, NULL },
    { "pkinit_dh_min_bits", krb5_config_string, NULL },
    { "pkinit_allow_proxy_certificate", krb5_config_string, NULL },
    { "hdb-ldap-create-base", krb5_config_string, NULL },
    { "v4-realm", krb5_config_string, NULL },
    { NULL }
};

struct entry kadmin_entries[] = {
    { "password_lifetime", krb5_config_string, check_time },
    { "default_keys", krb5_config_string, NULL },
    { "use_v4_salt", krb5_config_string, NULL },
    { "require-preauth", krb5_config_string, check_boolean },
    { NULL }
};
struct entry log_strings[] = {
    { "", krb5_config_string, check_log },
    { NULL }
};


/* MIT stuff */
struct entry kdcdefaults_entries[] = {
    { "kdc_ports", krb5_config_string, mit_entry },
    { "v4_mode", krb5_config_string, mit_entry },
    { NULL }
};

struct entry capaths_entries[] = {
    { "", krb5_config_list, all_strings },
    { NULL }
};

struct entry password_quality_entries[] = {
    { "policies", krb5_config_string, NULL },
    { "external_program", krb5_config_string, NULL },
    { "min_classes", krb5_config_string, check_numeric },
    { "min_length", krb5_config_string, check_numeric },
    { "", krb5_config_list, all_strings },
    { NULL }
};

struct entry toplevel_sections[] = {
    { "libdefaults" , krb5_config_list, libdefaults_entries },
    { "realms", krb5_config_list, realms_foobar },
    { "domain_realm", krb5_config_list, all_strings },
    { "logging", krb5_config_list, log_strings },
    { "kdc", krb5_config_list, kdc_entries },
    { "kadmin", krb5_config_list, kadmin_entries },
    { "appdefaults", krb5_config_list, appdefaults_entries },
    { "gssapi", krb5_config_list, NULL },
    { "capaths", krb5_config_list, capaths_entries },
    { "password_quality", krb5_config_list, password_quality_entries },
    /* MIT stuff */
    { "kdcdefaults", krb5_config_list, kdcdefaults_entries },
    { NULL }
};


static int
check_section(krb5_context context, const char *path, krb5_config_section *cf,
	      struct entry *entries)
{
    int error = 0;
    krb5_config_section *p;
    struct entry *e;

    char *local;

    for(p = cf; p != NULL; p = p->next) {
	local = NULL;
	if (asprintf(&local, "%s/%s", path, p->name) < 0 || local == NULL)
	    errx(1, "out of memory");
	for(e = entries; e->name != NULL; e++) {
	    if(*e->name == '\0' || strcmp(e->name, p->name) == 0) {
		if(e->type != p->type) {
		    krb5_warnx(context, "%s: unknown or wrong type", local);
		    error |= 1;
		} else if(p->type == krb5_config_string && e->check_data != NULL) {
		    error |= (*(check_func_t)e->check_data)(context, local, p->u.string);
		} else if(p->type == krb5_config_list && e->check_data != NULL) {
		    error |= check_section(context, local, p->u.list, e->check_data);
		}
		if(e->deprecated) {
		    krb5_warnx(context, "%s: is a deprecated entry", local);
		    error |= 1;
		}
		break;
	    }
	}
	if(e->name == NULL) {
	    krb5_warnx(context, "%s: unknown entry", local);
	    error |= 1;
	}
	free(local);
    }
    return error;
}


static void
dumpconfig(int level, krb5_config_section *top)
{
    krb5_config_section *x;
    for(x = top; x; x = x->next) {
	switch(x->type) {
	case krb5_config_list:
	    if(level == 0) {
		printf("[%s]\n", x->name);
	    } else {
		printf("%*s%s = {\n", 4 * level, " ", x->name);
	    }
	    dumpconfig(level + 1, x->u.list);
	    if(level > 0)
		printf("%*s}\n", 4 * level, " ");
	    break;
	case krb5_config_string:
	    printf("%*s%s = %s\n", 4 * level, " ", x->name, x->u.string);
	    break;
	}
    }
}

int
main(int argc, char **argv)
{
    krb5_context context;
    krb5_error_code ret;
    krb5_config_section *tmp_cf;
    int optidx = 0;

    setprogname (argv[0]);

    ret = krb5_init_context(&context);
    if (ret == KRB5_CONFIG_BADFORMAT)
	errx (1, "krb5_init_context failed to parse configuration file");
    else if (ret)
	errx (1, "krb5_init_context failed with %d", ret);

    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optidx))
	usage(1);

    if (help_flag)
	usage (0);

    if(version_flag){
	print_version(NULL);
	exit(0);
    }

    argc -= optidx;
    argv += optidx;

    tmp_cf = NULL;
    if(argc == 0)
	krb5_get_default_config_files(&argv);

    while(*argv) {
	ret = krb5_config_parse_file_multi(context, *argv, &tmp_cf);
	if (ret != 0)
	    krb5_warn (context, ret, "krb5_config_parse_file");
	argv++;
    }

    if(dumpconfig_flag)
	dumpconfig(0, tmp_cf);

    return check_section(context, "", tmp_cf, toplevel_sections);
}
