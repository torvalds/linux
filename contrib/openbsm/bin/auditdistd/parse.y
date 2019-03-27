%{
/*-
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config/config.h>

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif

#include "auditdistd.h"
#include "pjdlog.h"

extern int depth;
extern int lineno;

extern FILE *yyin;
extern char *yytext;

static struct adist_config *lconfig;
static struct adist_host *curhost;
#define	SECTION_GLOBAL		0
#define	SECTION_SENDER		1
#define	SECTION_RECEIVER	2
static int cursection;

/* Sender section. */
static char depth1_source[ADIST_ADDRSIZE];
static int depth1_checksum;
static int depth1_compression;
/* Sender and receiver sections. */
static char depth1_directory[PATH_MAX];

static bool adjust_directory(char *path);
static bool family_supported(int family);

extern void yyrestart(FILE *);
%}

%token CB
%token CERTFILE
%token DIRECTORY
%token FINGERPRINT
%token HOST
%token KEYFILE
%token LISTEN
%token NAME
%token OB
%token PASSWORD
%token PIDFILE
%token RECEIVER REMOTE
%token SENDER SOURCE
%token TIMEOUT

/*
%type <num> checksum_type
%type <num> compression_type
*/

%union
{
	int num;
	char *str;
}

%token <num> NUM
%token <str> STR

%%

statements:
	|
	statements statement
	;

statement:
	name_statement
	|
	pidfile_statement
	|
	timeout_statement
	|
	sender_statement
	|
	receiver_statement
	;

name_statement:	NAME STR
	{
		PJDLOG_RASSERT(depth == 0,
		    "The name variable can only be specificed in the global section.");

		if (lconfig->adc_name[0] != '\0') {
			pjdlog_error("The name variable is specified twice.");
			free($2);
			return (1);
		}
		if (strlcpy(lconfig->adc_name, $2,
		    sizeof(lconfig->adc_name)) >=
		    sizeof(lconfig->adc_name)) {
			pjdlog_error("The name value is too long.");
			free($2);
			return (1);
		}
		free($2);
	}
	;

pidfile_statement:	PIDFILE STR
	{
		PJDLOG_RASSERT(depth == 0,
		    "The pidfile variable can only be specificed in the global section.");

		if (lconfig->adc_pidfile[0] != '\0') {
			pjdlog_error("The pidfile variable is specified twice.");
			free($2);
			return (1);
		}
		if (strcmp($2, "none") != 0 && $2[0] != '/') {
			pjdlog_error("The pidfile variable must be set to absolute pathname or \"none\".");
			free($2);
			return (1);
		}
		if (strlcpy(lconfig->adc_pidfile, $2,
		    sizeof(lconfig->adc_pidfile)) >=
		    sizeof(lconfig->adc_pidfile)) {
			pjdlog_error("The pidfile value is too long.");
			free($2);
			return (1);
		}
		free($2);
	}
	;

timeout_statement:	TIMEOUT NUM
	{
		PJDLOG_ASSERT(depth == 0);

		lconfig->adc_timeout = $2;
	}
	;

sender_statement:	SENDER sender_start sender_entries CB
	{
		PJDLOG_ASSERT(depth == 0);
		PJDLOG_ASSERT(cursection == SECTION_SENDER);

		/* Configure defaults. */
		if (depth1_checksum == -1)
			depth1_checksum = ADIST_CHECKSUM_NONE;
		if (depth1_compression == -1)
			depth1_compression = ADIST_COMPRESSION_NONE;
		if (depth1_directory[0] == '\0') {
			(void)strlcpy(depth1_directory, ADIST_DIRECTORY_SENDER,
			    sizeof(depth1_directory));
		}
		/* Empty depth1_source is ok. */
		TAILQ_FOREACH(curhost, &lconfig->adc_hosts, adh_next) {
			if (curhost->adh_role != ADIST_ROLE_SENDER)
				continue;
			if (curhost->adh_checksum == -1)
				curhost->adh_checksum = depth1_checksum;
			if (curhost->adh_compression == -1)
				curhost->adh_compression = depth1_compression;
			if (curhost->adh_directory[0] == '\0') {
				(void)strlcpy(curhost->adh_directory,
				    depth1_directory,
				    sizeof(curhost->adh_directory));
			}
			if (curhost->adh_localaddr[0] == '\0') {
				(void)strlcpy(curhost->adh_localaddr,
				    depth1_source,
				    sizeof(curhost->adh_localaddr));
			}
		}
		cursection = SECTION_GLOBAL;
	}
	;

sender_start:	OB
	{
		PJDLOG_ASSERT(depth == 1);
		PJDLOG_ASSERT(cursection == SECTION_GLOBAL);

		cursection = SECTION_SENDER;
		depth1_checksum = -1;
		depth1_compression = -1;
		depth1_source[0] = '\0';
		depth1_directory[0] = '\0';

#ifndef HAVE_AUDIT_SYSCALLS
		pjdlog_error("Sender functionality is not available.");
		return (1);
#endif
	}
	;

sender_entries:
	|
	sender_entries sender_entry
	;

sender_entry:
	source_statement
	|
	directory_statement
/*
	|
	checksum_statement
	|
	compression_statement
*/
	|
	sender_host_statement
	;

receiver_statement:	RECEIVER receiver_start receiver_entries CB
	{
		PJDLOG_ASSERT(depth == 0);
		PJDLOG_ASSERT(cursection == SECTION_RECEIVER);

		/*
		 * If not listen addresses were specified,
		 * configure default ones.
		 */
		if (TAILQ_EMPTY(&lconfig->adc_listen)) {
			struct adist_listen *lst;

			if (family_supported(AF_INET)) {
				lst = calloc(1, sizeof(*lst));
				if (lst == NULL) {
					pjdlog_error("Unable to allocate memory for listen address.");
					return (1);
				}
				(void)strlcpy(lst->adl_addr,
				    ADIST_LISTEN_TLS_TCP4,
				    sizeof(lst->adl_addr));
				TAILQ_INSERT_TAIL(&lconfig->adc_listen, lst, adl_next);
			} else {
				pjdlog_debug(1,
				    "No IPv4 support in the kernel, not listening on IPv4 address.");
			}
			if (family_supported(AF_INET6)) {
				lst = calloc(1, sizeof(*lst));
				if (lst == NULL) {
					pjdlog_error("Unable to allocate memory for listen address.");
					return (1);
				}
				(void)strlcpy(lst->adl_addr,
				    ADIST_LISTEN_TLS_TCP6,
				    sizeof(lst->adl_addr));
				TAILQ_INSERT_TAIL(&lconfig->adc_listen, lst, adl_next);
			} else {
				pjdlog_debug(1,
				    "No IPv6 support in the kernel, not listening on IPv6 address.");
			}
			if (TAILQ_EMPTY(&lconfig->adc_listen)) {
				pjdlog_error("No address to listen on.");
				return (1);
			}
		}
		/* Configure defaults. */
		if (depth1_directory[0] == '\0') {
			(void)strlcpy(depth1_directory,
			    ADIST_DIRECTORY_RECEIVER,
			    sizeof(depth1_directory));
		}
		TAILQ_FOREACH(curhost, &lconfig->adc_hosts, adh_next) {
			if (curhost->adh_role != ADIST_ROLE_RECEIVER)
				continue;
			if (curhost->adh_directory[0] == '\0') {
				if (snprintf(curhost->adh_directory,
				    sizeof(curhost->adh_directory), "%s/%s",
				    depth1_directory, curhost->adh_name) >=
				    (ssize_t)sizeof(curhost->adh_directory)) {
					pjdlog_error("Directory value is too long.");
					return (1);
				}
			}
		}
		cursection = SECTION_GLOBAL;
	}
	;

receiver_start:	OB
	{
		PJDLOG_ASSERT(depth == 1);
		PJDLOG_ASSERT(cursection == SECTION_GLOBAL);

		cursection = SECTION_RECEIVER;
		depth1_directory[0] = '\0';
	}
	;

receiver_entries:
	|
	receiver_entries receiver_entry
	;

receiver_entry:
	listen_statement
	|
	directory_statement
	|
	certfile_statement
	|
	keyfile_statement
	|
	receiver_host_statement
	;

/*
checksum_statement:	CHECKSUM checksum_type
	{
		PJDLOG_ASSERT(cursection == SECTION_SENDER);

		switch (depth) {
		case 1:
			depth1_checksum = $2;
			break;
		case 2:
			PJDLOG_ASSERT(curhost != NULL);
			curhost->adh_checksum = $2;
			break;
		default:
			PJDLOG_ABORT("checksum at wrong depth level");
		}
	}
	;

checksum_type:
	NONE		{ $$ = ADIST_CHECKSUM_NONE; }
	|
	CRC32		{ $$ = ADIST_CHECKSUM_CRC32; }
	|
	SHA256		{ $$ = ADIST_CHECKSUM_SHA256; }
	;

compression_statement:	COMPRESSION compression_type
	{
		PJDLOG_ASSERT(cursection == SECTION_SENDER);

		switch (depth) {
		case 1:
			depth1_compression = $2;
			break;
		case 2:
			PJDLOG_ASSERT(curhost != NULL);
			curhost->adh_compression = $2;
			break;
		default:
			PJDLOG_ABORT("compression at wrong depth level");
		}
	}
	;

compression_type:
	NONE		{ $$ = ADIST_COMPRESSION_NONE; }
	|
	LZF		{ $$ = ADIST_COMPRESSION_LZF; }
	;
*/

directory_statement:	DIRECTORY STR
	{
		PJDLOG_ASSERT(cursection == SECTION_SENDER ||
		    cursection == SECTION_RECEIVER);

		switch (depth) {
		case 1:
			if (strlcpy(depth1_directory, $2,
			    sizeof(depth1_directory)) >=
			    sizeof(depth1_directory)) {
				pjdlog_error("Directory value is too long.");
				free($2);
				return (1);
			}
			if (!adjust_directory(depth1_directory))
				return (1);
			break;
		case 2:
			if (cursection == SECTION_SENDER || $2[0] == '/') {
				if (strlcpy(curhost->adh_directory, $2,
				    sizeof(curhost->adh_directory)) >=
				    sizeof(curhost->adh_directory)) {
					pjdlog_error("Directory value is too long.");
					free($2);
					return (1);
				}
			} else /* if (cursection == SECTION_RECEIVER) */ {
				if (depth1_directory[0] == '\0') {
					pjdlog_error("Directory path must be absolute.");
					free($2);
					return (1);
				}
				if (snprintf(curhost->adh_directory,
				    sizeof(curhost->adh_directory), "%s/%s",
				    depth1_directory, $2) >=
				    (ssize_t)sizeof(curhost->adh_directory)) {
					pjdlog_error("Directory value is too long.");
					free($2);
					return (1);
				}
			}
			break;
		default:
			PJDLOG_ABORT("directory at wrong depth level");
		}
		free($2);
	}
	;

source_statement:	SOURCE STR
	{
		PJDLOG_RASSERT(cursection == SECTION_SENDER,
		    "The source variable must be in sender section.");

		switch (depth) {
		case 1:
			if (strlcpy(depth1_source, $2,
			    sizeof(depth1_source)) >=
			    sizeof(depth1_source)) {
				pjdlog_error("Source value is too long.");
				free($2);
				return (1);
			}
			break;
		case 2:
			if (strlcpy(curhost->adh_localaddr, $2,
			    sizeof(curhost->adh_localaddr)) >=
			    sizeof(curhost->adh_localaddr)) {
				pjdlog_error("Source value is too long.");
				free($2);
				return (1);
			}
			break;
		}
		free($2);
	}
	;

fingerprint_statement:	FINGERPRINT STR
	{
		PJDLOG_ASSERT(cursection == SECTION_SENDER);
		PJDLOG_ASSERT(depth == 2);

		if (strncasecmp($2, "SHA256=", 7) != 0) {
			pjdlog_error("Invalid fingerprint value.");
			free($2);
			return (1);
		}
		if (strlcpy(curhost->adh_fingerprint, $2,
		    sizeof(curhost->adh_fingerprint)) >=
		    sizeof(curhost->adh_fingerprint)) {
			pjdlog_error("Fingerprint value is too long.");
			free($2);
			return (1);
		}
		free($2);
	}
	;

password_statement:	PASSWORD STR
	{
		PJDLOG_ASSERT(cursection == SECTION_SENDER ||
		    cursection == SECTION_RECEIVER);
		PJDLOG_ASSERT(depth == 2);

		if (strlcpy(curhost->adh_password, $2,
		    sizeof(curhost->adh_password)) >=
		    sizeof(curhost->adh_password)) {
			pjdlog_error("Password value is too long.");
			bzero($2, strlen($2));
			free($2);
			return (1);
		}
		bzero($2, strlen($2));
		free($2);
	}
	;

certfile_statement:	CERTFILE STR
	{
		PJDLOG_ASSERT(cursection == SECTION_RECEIVER);
		PJDLOG_ASSERT(depth == 1);

		if (strlcpy(lconfig->adc_certfile, $2,
		    sizeof(lconfig->adc_certfile)) >=
		    sizeof(lconfig->adc_certfile)) {
			pjdlog_error("Certfile value is too long.");
			free($2);
			return (1);
		}
		free($2);
	}
	;

keyfile_statement:	KEYFILE STR
	{
		PJDLOG_ASSERT(cursection == SECTION_RECEIVER);
		PJDLOG_ASSERT(depth == 1);

		if (strlcpy(lconfig->adc_keyfile, $2,
		    sizeof(lconfig->adc_keyfile)) >=
		    sizeof(lconfig->adc_keyfile)) {
			pjdlog_error("Keyfile value is too long.");
			free($2);
			return (1);
		}
		free($2);
	}
	;

listen_statement:	LISTEN STR
	{
		struct adist_listen *lst;

		PJDLOG_ASSERT(depth == 1);
		PJDLOG_ASSERT(cursection == SECTION_RECEIVER);

		lst = calloc(1, sizeof(*lst));
		if (lst == NULL) {
			pjdlog_error("Unable to allocate memory for listen address.");
			free($2);
			return (1);
		}
		if (strlcpy(lst->adl_addr, $2, sizeof(lst->adl_addr)) >=
		    sizeof(lst->adl_addr)) {
			pjdlog_error("listen argument is too long.");
			free($2);
			free(lst);
			return (1);
		}
		TAILQ_INSERT_TAIL(&lconfig->adc_listen, lst, adl_next);
		free($2);
	}
	;

sender_host_statement:	HOST host_start OB sender_host_entries CB
	{
		/* Put it onto host list. */
		TAILQ_INSERT_TAIL(&lconfig->adc_hosts, curhost, adh_next);
		curhost = NULL;
	}
	;

receiver_host_statement:	HOST host_start OB receiver_host_entries CB
	{
		/* Put it onto host list. */
		TAILQ_INSERT_TAIL(&lconfig->adc_hosts, curhost, adh_next);
		curhost = NULL;
	}
	;

host_start:	STR
	{
		/* Check if there is no duplicate entry. */
		TAILQ_FOREACH(curhost, &lconfig->adc_hosts, adh_next) {
			if (strcmp(curhost->adh_name, $1) != 0)
				continue;
			if (curhost->adh_role == ADIST_ROLE_SENDER &&
			    cursection == SECTION_RECEIVER) {
				continue;
			}
			if (curhost->adh_role == ADIST_ROLE_RECEIVER &&
			    cursection == SECTION_SENDER) {
				continue;
			}
			pjdlog_error("%s host %s is configured more than once.",
			    curhost->adh_role == ADIST_ROLE_SENDER ?
			    "Sender" : "Receiver", curhost->adh_name);
			free($1);
			return (1);
		}

		curhost = calloc(1, sizeof(*curhost));
		if (curhost == NULL) {
			pjdlog_error("Unable to allocate memory for host configuration.");
			free($1);
			return (1);
		}
		if (strlcpy(curhost->adh_name, $1, sizeof(curhost->adh_name)) >=
		    sizeof(curhost->adh_name)) {
			pjdlog_error("Host name is too long.");
			free($1);
			return (1);
		}
		free($1);
		curhost->adh_role = cursection == SECTION_SENDER ?
		    ADIST_ROLE_SENDER : ADIST_ROLE_RECEIVER;
		curhost->adh_version = ADIST_VERSION;
		curhost->adh_localaddr[0] = '\0';
		curhost->adh_remoteaddr[0] = '\0';
		curhost->adh_remote = NULL;
		curhost->adh_directory[0] = '\0';
		curhost->adh_password[0] = '\0';
		curhost->adh_fingerprint[0] = '\0';
		curhost->adh_worker_pid = 0;
		curhost->adh_conn = NULL;
	}
	;

sender_host_entries:
	|
	sender_host_entries sender_host_entry
	;

sender_host_entry:
	source_statement
	|
	remote_statement
	|
	directory_statement
	|
	fingerprint_statement
	|
	password_statement
/*
	|
	checksum_statement
	|
	compression_statement
*/
	;

receiver_host_entries:
	|
	receiver_host_entries receiver_host_entry
	;

receiver_host_entry:
	remote_statement
	|
	directory_statement
	|
	password_statement
	;

remote_statement:	REMOTE STR
	{
		PJDLOG_ASSERT(depth == 2);
		PJDLOG_ASSERT(cursection == SECTION_SENDER ||
		    cursection == SECTION_RECEIVER);

		if (strlcpy(curhost->adh_remoteaddr, $2,
		    sizeof(curhost->adh_remoteaddr)) >=
		    sizeof(curhost->adh_remoteaddr)) {
			pjdlog_error("Remote value is too long.");
			free($2);
			return (1);
		}
		free($2);
	}
	;

%%

static bool
family_supported(int family)
{
	int sock;

	sock = socket(family, SOCK_STREAM, 0);
	if (sock == -1 && errno == EPROTONOSUPPORT)
		return (false);
	if (sock >= 0)
		(void)close(sock);
	return (true);
}

static bool
adjust_directory(char *path)
{
	size_t len;

	len = strlen(path);
	for (;;) {
		if (len == 0) {
			pjdlog_error("Directory path is empty.");
			return (false);
		}
		if (path[len - 1] != '/')
			break;
		len--;
		path[len] = '\0';
	}
	if (path[0] != '/') {
		pjdlog_error("Directory path must be absolute.");
		return (false);
	}
	return (true);
}

static int
my_name(char *name, size_t size)
{
	char buf[MAXHOSTNAMELEN];
	char *pos;

	if (gethostname(buf, sizeof(buf)) < 0) {
		pjdlog_errno(LOG_ERR, "gethostname() failed");
		return (-1);
	}

	/* First component of the host name. */
	pos = strchr(buf, '.');
	if (pos == NULL)
		(void)strlcpy(name, buf, size);
	else
		(void)strlcpy(name, buf, MIN((size_t)(pos - buf + 1), size));

	if (name[0] == '\0') {
		pjdlog_error("Empty host name.");
		return (-1);
	}

	return (0);
}

void
yyerror(const char *str)
{

	pjdlog_error("Unable to parse configuration file at line %d near '%s': %s",
	    lineno, yytext, str);
}

struct adist_config *
yy_config_parse(const char *config, bool exitonerror)
{
	int ret;

	curhost = NULL;
	cursection = SECTION_GLOBAL;
	depth = 0;
	lineno = 0;

	lconfig = calloc(1, sizeof(*lconfig));
	if (lconfig == NULL) {
		pjdlog_error("Unable to allocate memory for configuration.");
		if (exitonerror)
			exit(EX_TEMPFAIL);
		return (NULL);
	}
	TAILQ_INIT(&lconfig->adc_hosts);
	TAILQ_INIT(&lconfig->adc_listen);
	lconfig->adc_name[0] = '\0';
	lconfig->adc_timeout = -1;
	lconfig->adc_pidfile[0] = '\0';
	lconfig->adc_certfile[0] = '\0';
	lconfig->adc_keyfile[0] = '\0';

	yyin = fopen(config, "r");
	if (yyin == NULL) {
		pjdlog_errno(LOG_ERR, "Unable to open configuration file %s",
		    config);
		yy_config_free(lconfig);
		if (exitonerror)
			exit(EX_OSFILE);
		return (NULL);
	}
	yyrestart(yyin);
	ret = yyparse();
	fclose(yyin);
	if (ret != 0) {
		yy_config_free(lconfig);
		if (exitonerror)
			exit(EX_CONFIG);
		return (NULL);
	}

	/*
	 * Let's see if everything is set up.
	 */
	if (lconfig->adc_name[0] == '\0' && my_name(lconfig->adc_name,
	    sizeof(lconfig->adc_name)) == -1) {
		yy_config_free(lconfig);
		if (exitonerror)
			exit(EX_CONFIG);
		return (NULL);
	}
	if (lconfig->adc_timeout == -1)
		lconfig->adc_timeout = ADIST_TIMEOUT;
	if (lconfig->adc_pidfile[0] == '\0') {
		(void)strlcpy(lconfig->adc_pidfile, ADIST_PIDFILE,
		    sizeof(lconfig->adc_pidfile));
	}
	if (lconfig->adc_certfile[0] == '\0') {
		(void)strlcpy(lconfig->adc_certfile, ADIST_CERTFILE,
		    sizeof(lconfig->adc_certfile));
	}
	if (lconfig->adc_keyfile[0] == '\0') {
		(void)strlcpy(lconfig->adc_keyfile, ADIST_KEYFILE,
		    sizeof(lconfig->adc_keyfile));
	}

	return (lconfig);
}

void
yy_config_free(struct adist_config *config)
{
	struct adist_host *adhost;
	struct adist_listen *lst;

	while ((lst = TAILQ_FIRST(&config->adc_listen)) != NULL) {
		TAILQ_REMOVE(&config->adc_listen, lst, adl_next);
		free(lst);
	}
	while ((adhost = TAILQ_FIRST(&config->adc_hosts)) != NULL) {
		TAILQ_REMOVE(&config->adc_hosts, adhost, adh_next);
		bzero(adhost, sizeof(*adhost));
		free(adhost);
	}
	free(config);
}
