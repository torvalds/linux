/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

RCSID("$Id$");

#ifdef	SPX
/*
 * COPYRIGHT (C) 1990 DIGITAL EQUIPMENT CORPORATION
 * ALL RIGHTS RESERVED
 *
 * "Digital Equipment Corporation authorizes the reproduction,
 * distribution and modification of this software subject to the following
 * restrictions:
 *
 * 1.  Any partial or whole copy of this software, or any modification
 * thereof, must include this copyright notice in its entirety.
 *
 * 2.  This software is supplied "as is" with no warranty of any kind,
 * expressed or implied, for any purpose, including any warranty of fitness
 * or merchantibility.  DIGITAL assumes no responsibility for the use or
 * reliability of this software, nor promises to provide any form of
 * support for it on any basis.
 *
 * 3.  Distribution of this software is authorized only if no profit or
 * remuneration of any kind is received in exchange for such distribution.
 *
 * 4.  This software produces public key authentication certificates
 * bearing an expiration date established by DIGITAL and RSA Data
 * Security, Inc.  It may cease to generate certificates after the expiration
 * date.  Any modification of this software that changes or defeats
 * the expiration date or its effect is unauthorized.
 *
 * 5.  Software that will renew or extend the expiration date of
 * authentication certificates produced by this software may be obtained
 * from RSA Data Security, Inc., 10 Twin Dolphin Drive, Redwood City, CA
 * 94065, (415)595-8782, or from DIGITAL"
 *
 */

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_ARPA_TELNET_H
#include <arpa/telnet.h>
#endif
#include <stdio.h>
#include "gssapi_defs.h"
#include <stdlib.h>
#include <string.h>

#include <pwd.h>
#ifdef SOCKS
#include <socks.h>
#endif

#include "encrypt.h"
#include "auth.h"
#include "misc.h"

extern auth_debug_mode;

static unsigned char str_data[1024] = { IAC, SB, TELOPT_AUTHENTICATION, 0,
			  		AUTHTYPE_SPX, };
static unsigned char str_name[1024] = { IAC, SB, TELOPT_AUTHENTICATION,
					TELQUAL_NAME, };

#define	SPX_AUTH	0		/* Authentication data follows */
#define	SPX_REJECT	1		/* Rejected (reason might follow) */
#define SPX_ACCEPT	2		/* Accepted */

static des_key_schedule sched;
static des_cblock	challenge	= { 0 };


/*******************************************************************/

gss_OID_set		actual_mechs;
gss_OID			actual_mech_type, output_name_type;
int			major_status, status, msg_ctx = 0, new_status;
int			req_flags = 0, ret_flags, lifetime_rec;
gss_cred_id_t		gss_cred_handle;
gss_ctx_id_t		actual_ctxhandle, context_handle;
gss_buffer_desc		output_token, input_token, input_name_buffer;
gss_buffer_desc		status_string;
gss_name_t		desired_targname, src_name;
gss_channel_bindings	input_chan_bindings;
char			lhostname[GSS_C_MAX_PRINTABLE_NAME];
char			targ_printable[GSS_C_MAX_PRINTABLE_NAME];
int			to_addr=0, from_addr=0;
char			*address;
gss_buffer_desc		fullname_buffer;
gss_OID			fullname_type;
gss_cred_id_t		gss_delegated_cred_handle;

/*******************************************************************/



	static int
Data(ap, type, d, c)
	Authenticator *ap;
	int type;
	void *d;
	int c;
{
	unsigned char *p = str_data + 4;
	unsigned char *cd = (unsigned char *)d;

	if (c == -1)
		c = strlen((char *)cd);

	if (0) {
		printf("%s:%d: [%d] (%d)",
			str_data[3] == TELQUAL_IS ? ">>>IS" : ">>>REPLY",
			str_data[3],
			type, c);
		printd(d, c);
		printf("\r\n");
	}
	*p++ = ap->type;
	*p++ = ap->way;
	*p++ = type;
	while (c-- > 0) {
		if ((*p++ = *cd++) == IAC)
			*p++ = IAC;
	}
	*p++ = IAC;
	*p++ = SE;
	if (str_data[3] == TELQUAL_IS)
		printsub('>', &str_data[2], p - (&str_data[2]));
	return(telnet_net_write(str_data, p - str_data));
}

	int
spx_init(ap, server)
	Authenticator *ap;
	int server;
{
	gss_cred_id_t	tmp_cred_handle;

	if (server) {
		str_data[3] = TELQUAL_REPLY;
		gethostname(lhostname, sizeof(lhostname));
		snprintf (targ_printable, sizeof(targ_printable),
			  "SERVICE:rcmd@%s", lhostname);
		input_name_buffer.length = strlen(targ_printable);
		input_name_buffer.value = targ_printable;
		major_status = gss_import_name(&status,
					&input_name_buffer,
					GSS_C_NULL_OID,
					&desired_targname);
		major_status = gss_acquire_cred(&status,
					desired_targname,
					0,
					GSS_C_NULL_OID_SET,
					GSS_C_ACCEPT,
					&tmp_cred_handle,
					&actual_mechs,
					&lifetime_rec);
		if (major_status != GSS_S_COMPLETE) return(0);
	} else {
		str_data[3] = TELQUAL_IS;
	}
	return(1);
}

	int
spx_send(ap)
	Authenticator *ap;
{
	des_cblock enckey;
	int r;

	gss_OID	actual_mech_type, output_name_type;
	int	msg_ctx = 0, new_status, status;
	int	req_flags = 0, ret_flags, lifetime_rec, major_status;
	gss_buffer_desc  output_token, input_token, input_name_buffer;
	gss_buffer_desc  output_name_buffer, status_string;
	gss_name_t    desired_targname;
	gss_channel_bindings  input_chan_bindings;
	char targ_printable[GSS_C_MAX_PRINTABLE_NAME];
	int  from_addr=0, to_addr=0, myhostlen, j;
	int  deleg_flag=1, mutual_flag=0, replay_flag=0, seq_flag=0;
	char *address;

	printf("[ Trying SPX ... ]\r\n");
	snprintf (targ_printable, sizeof(targ_printable),
		  "SERVICE:rcmd@%s", RemoteHostName);

	input_name_buffer.length = strlen(targ_printable);
	input_name_buffer.value = targ_printable;

	if (!UserNameRequested) {
		return(0);
	}

	major_status = gss_import_name(&status,
					&input_name_buffer,
					GSS_C_NULL_OID,
					&desired_targname);


	major_status = gss_display_name(&status,
					desired_targname,
					&output_name_buffer,
					&output_name_type);

	printf("target is '%.*s'\n", (int)output_name_buffer.length,
					(char*)output_name_buffer.value);
	fflush(stdout);

	major_status = gss_release_buffer(&status, &output_name_buffer);

	input_chan_bindings = (gss_channel_bindings)
	  malloc(sizeof(gss_channel_bindings_desc));

	input_chan_bindings->initiator_addrtype = GSS_C_AF_INET;
	input_chan_bindings->initiator_address.length = 4;
	address = (char *) malloc(4);
	input_chan_bindings->initiator_address.value = (char *) address;
	address[0] = ((from_addr & 0xff000000) >> 24);
	address[1] = ((from_addr & 0xff0000) >> 16);
	address[2] = ((from_addr & 0xff00) >> 8);
	address[3] = (from_addr & 0xff);
	input_chan_bindings->acceptor_addrtype = GSS_C_AF_INET;
	input_chan_bindings->acceptor_address.length = 4;
	address = (char *) malloc(4);
	input_chan_bindings->acceptor_address.value = (char *) address;
	address[0] = ((to_addr & 0xff000000) >> 24);
	address[1] = ((to_addr & 0xff0000) >> 16);
	address[2] = ((to_addr & 0xff00) >> 8);
	address[3] = (to_addr & 0xff);
	input_chan_bindings->application_data.length = 0;

	req_flags = 0;
	if (deleg_flag)  req_flags = req_flags | 1;
	if (mutual_flag) req_flags = req_flags | 2;
	if (replay_flag) req_flags = req_flags | 4;
	if (seq_flag)    req_flags = req_flags | 8;

	major_status = gss_init_sec_context(&status,         /* minor status */
					GSS_C_NO_CREDENTIAL, /* cred handle */
					&actual_ctxhandle,   /* ctx handle */
					desired_targname,    /* target name */
					GSS_C_NULL_OID,      /* mech type */
					req_flags,           /* req flags */
					0,                   /* time req */
					input_chan_bindings, /* chan binding */
					GSS_C_NO_BUFFER,     /* input token */
					&actual_mech_type,   /* actual mech */
					&output_token,       /* output token */
					&ret_flags,          /* ret flags */
					&lifetime_rec);      /* time rec */

	if ((major_status != GSS_S_COMPLETE) &&
	    (major_status != GSS_S_CONTINUE_NEEDED)) {
	  gss_display_status(&new_status,
				status,
				GSS_C_MECH_CODE,
				GSS_C_NULL_OID,
				&msg_ctx,
				&status_string);
	  printf("%.*s\n", (int)status_string.length,
				(char*)status_string.value);
	  return(0);
	}

	if (!auth_sendname(UserNameRequested, strlen(UserNameRequested))) {
		return(0);
	}

	if (!Data(ap, SPX_AUTH, output_token.value, output_token.length)) {
		return(0);
	}

	return(1);
}

	void
spx_is(ap, data, cnt)
	Authenticator *ap;
	unsigned char *data;
	int cnt;
{
	Session_Key skey;
	des_cblock datablock;
	int r;

	if (cnt-- < 1)
		return;
	switch (*data++) {
	case SPX_AUTH:
		input_token.length = cnt;
		input_token.value = (char *) data;

		gethostname(lhostname, sizeof(lhostname));

		snprintf(targ_printable, sizeof(targ_printable),
			 "SERVICE:rcmd@%s", lhostname);

		input_name_buffer.length = strlen(targ_printable);
		input_name_buffer.value = targ_printable;

		major_status = gss_import_name(&status,
					&input_name_buffer,
					GSS_C_NULL_OID,
					&desired_targname);

		major_status = gss_acquire_cred(&status,
					desired_targname,
					0,
					GSS_C_NULL_OID_SET,
					GSS_C_ACCEPT,
					&gss_cred_handle,
					&actual_mechs,
					&lifetime_rec);

		major_status = gss_release_name(&status, desired_targname);

		input_chan_bindings = (gss_channel_bindings)
		  malloc(sizeof(gss_channel_bindings_desc));

		input_chan_bindings->initiator_addrtype = GSS_C_AF_INET;
		input_chan_bindings->initiator_address.length = 4;
		address = (char *) malloc(4);
		input_chan_bindings->initiator_address.value = (char *) address;
		address[0] = ((from_addr & 0xff000000) >> 24);
		address[1] = ((from_addr & 0xff0000) >> 16);
		address[2] = ((from_addr & 0xff00) >> 8);
		address[3] = (from_addr & 0xff);
		input_chan_bindings->acceptor_addrtype = GSS_C_AF_INET;
		input_chan_bindings->acceptor_address.length = 4;
		address = (char *) malloc(4);
		input_chan_bindings->acceptor_address.value = (char *) address;
		address[0] = ((to_addr & 0xff000000) >> 24);
		address[1] = ((to_addr & 0xff0000) >> 16);
		address[2] = ((to_addr & 0xff00) >> 8);
		address[3] = (to_addr & 0xff);
		input_chan_bindings->application_data.length = 0;

		major_status = gss_accept_sec_context(&status,
						&context_handle,
						gss_cred_handle,
						&input_token,
						input_chan_bindings,
						&src_name,
						&actual_mech_type,
						&output_token,
						&ret_flags,
						&lifetime_rec,
						&gss_delegated_cred_handle);


		if (major_status != GSS_S_COMPLETE) {

		  major_status = gss_display_name(&status,
					src_name,
					&fullname_buffer,
					&fullname_type);
			Data(ap, SPX_REJECT, "auth failed", -1);
			auth_finished(ap, AUTH_REJECT);
			return;
		}

		major_status = gss_display_name(&status,
					src_name,
					&fullname_buffer,
					&fullname_type);


		Data(ap, SPX_ACCEPT, output_token.value, output_token.length);
		auth_finished(ap, AUTH_USER);
		break;

	default:
		Data(ap, SPX_REJECT, 0, 0);
		break;
	}
}


	void
spx_reply(ap, data, cnt)
	Authenticator *ap;
	unsigned char *data;
	int cnt;
{
	Session_Key skey;

	if (cnt-- < 1)
		return;
	switch (*data++) {
	case SPX_REJECT:
		if (cnt > 0) {
			printf("[ SPX refuses authentication because %.*s ]\r\n",
				cnt, data);
		} else
			printf("[ SPX refuses authentication ]\r\n");
		auth_send_retry();
		return;
	case SPX_ACCEPT:
		printf("[ SPX accepts you ]\r\n");
		if ((ap->way & AUTH_HOW_MASK) == AUTH_HOW_MUTUAL) {
			/*
			 * Send over the encrypted challenge.
		 	 */
		  input_token.value = (char *) data;
		  input_token.length = cnt;

		  major_status = gss_init_sec_context(&status, /* minor stat */
					GSS_C_NO_CREDENTIAL, /* cred handle */
					&actual_ctxhandle,   /* ctx handle */
					desired_targname,    /* target name */
					GSS_C_NULL_OID,      /* mech type */
					req_flags,           /* req flags */
					0,                   /* time req */
					input_chan_bindings, /* chan binding */
					&input_token,        /* input token */
					&actual_mech_type,   /* actual mech */
					&output_token,       /* output token */
					&ret_flags,          /* ret flags */
					&lifetime_rec);      /* time rec */

		  if (major_status != GSS_S_COMPLETE) {
		    gss_display_status(&new_status,
					status,
					GSS_C_MECH_CODE,
					GSS_C_NULL_OID,
					&msg_ctx,
					&status_string);
		    printf("[ SPX mutual response fails ... '%.*s' ]\r\n",
			 (int)status_string.length,
			 (char*)status_string.value);
		    auth_send_retry();
		    return;
		  }
		}
		auth_finished(ap, AUTH_USER);
		return;

	default:
		return;
	}
}

	int
spx_status(ap, name, name_sz, level)
	Authenticator *ap;
	char *name;
	size_t name_sz;
	int level;
{

	gss_buffer_desc  fullname_buffer, acl_file_buffer;
	gss_OID          fullname_type;
	char acl_file[160], fullname[160];
	int major_status, status = 0;
	struct passwd  *pwd;

	/*
	 * hard code fullname to
	 *   "SPX:/C=US/O=Digital/OU=LKG/OU=Sphinx/OU=Users/CN=Kannan Alagappan"
	 * and acl_file to "~kannan/.sphinx"
	 */

	pwd = k_getpwnam(UserNameRequested);
	if (pwd == NULL) {
	  return(AUTH_USER);   /*  not authenticated  */
	}

	snprintf (acl_file, sizeof(acl_file),
		  "%s/.sphinx", pwd->pw_dir);

	acl_file_buffer.value = acl_file;
	acl_file_buffer.length = strlen(acl_file);

	major_status = gss_display_name(&status,
					src_name,
					&fullname_buffer,
					&fullname_type);

	if (level < AUTH_USER)
		return(level);

	major_status = gss__check_acl(&status, &fullname_buffer,
					&acl_file_buffer);

	if (major_status == GSS_S_COMPLETE) {
	  strlcpy(name, UserNameRequested, name_sz);
	  return(AUTH_VALID);
	} else {
	   return(AUTH_USER);
	}

}

#define	BUMP(buf, len)		while (*(buf)) {++(buf), --(len);}
#define	ADDC(buf, len, c)	if ((len) > 0) {*(buf)++ = (c); --(len);}

	void
spx_printsub(unsigned char *data, size_t cnt,
	     unsigned char *buf, size_t buflen)
{
	size_t i;

	buf[buflen-1] = '\0';		/* make sure it's NULL terminated */
	buflen -= 1;

	switch(data[3]) {
	case SPX_REJECT:		/* Rejected (reason might follow) */
		strlcpy((char *)buf, " REJECT ", buflen);
		goto common;

	case SPX_ACCEPT:		/* Accepted (name might follow) */
		strlcpy((char *)buf, " ACCEPT ", buflen);
	common:
		BUMP(buf, buflen);
		if (cnt <= 4)
			break;
		ADDC(buf, buflen, '"');
		for (i = 4; i < cnt; i++)
			ADDC(buf, buflen, data[i]);
		ADDC(buf, buflen, '"');
		ADDC(buf, buflen, '\0');
		break;

	case SPX_AUTH:			/* Authentication data follows */
		strlcpy((char *)buf, " AUTH", buflen);
		goto common2;

	default:
		snprintf(buf, buflen, " %d (unknown)", data[3]);
	common2:
		BUMP(buf, buflen);
		for (i = 4; i < cnt; i++) {
			snprintf(buf, buflen, " %d", data[i]);
			BUMP(buf, buflen);
		}
		break;
	}
}

#endif

#ifdef notdef

prkey(msg, key)
	char *msg;
	unsigned char *key;
{
	int i;
	printf("%s:", msg);
	for (i = 0; i < 8; i++)
		printf(" %3d", key[i]);
	printf("\r\n");
}
#endif
