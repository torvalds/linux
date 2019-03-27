/*
 * Copyright 2010 Red Hat, Inc.  All rights reserved.
 * Use is subject to license terms.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Red Hat author: Jan F. Chadima <jchadima@redhat.com>
 */

#include "includes.h"
#if defined(USE_LINUX_AUDIT)
#include <libaudit.h>
#include <unistd.h>
#include <string.h>

#include "log.h"
#include "audit.h"
#include "canohost.h"
#include "packet.h"

const char *audit_username(void);

int
linux_audit_record_event(int uid, const char *username, const char *hostname,
    const char *ip, const char *ttyn, int success)
{
	int audit_fd, rc, saved_errno;

	if ((audit_fd = audit_open()) < 0) {
		if (errno == EINVAL || errno == EPROTONOSUPPORT ||
		    errno == EAFNOSUPPORT)
			return 1; /* No audit support in kernel */
		else
			return 0; /* Must prevent login */
	}
	rc = audit_log_acct_message(audit_fd, AUDIT_USER_LOGIN,
	    NULL, "login", username ? username : "(unknown)",
	    username == NULL ? uid : -1, hostname, ip, ttyn, success);
	saved_errno = errno;
	close(audit_fd);

	/*
	 * Do not report error if the error is EPERM and sshd is run as non
	 * root user.
	 */
	if ((rc == -EPERM) && (geteuid() != 0))
		rc = 0;
	errno = saved_errno;

	return rc >= 0;
}

/* Below is the sshd audit API code */

void
audit_connection_from(const char *host, int port)
{
	/* not implemented */
}

void
audit_run_command(const char *command)
{
	/* not implemented */
}

void
audit_session_open(struct logininfo *li)
{
	if (linux_audit_record_event(li->uid, NULL, li->hostname, NULL,
	    li->line, 1) == 0)
		fatal("linux_audit_write_entry failed: %s", strerror(errno));
}

void
audit_session_close(struct logininfo *li)
{
	/* not implemented */
}

void
audit_event(ssh_audit_event_t event)
{
	struct ssh *ssh = active_state; /* XXX */

	switch(event) {
	case SSH_AUTH_SUCCESS:
	case SSH_CONNECTION_CLOSE:
	case SSH_NOLOGIN:
	case SSH_LOGIN_EXCEED_MAXTRIES:
	case SSH_LOGIN_ROOT_DENIED:
		break;
	case SSH_AUTH_FAIL_NONE:
	case SSH_AUTH_FAIL_PASSWD:
	case SSH_AUTH_FAIL_KBDINT:
	case SSH_AUTH_FAIL_PUBKEY:
	case SSH_AUTH_FAIL_HOSTBASED:
	case SSH_AUTH_FAIL_GSSAPI:
	case SSH_INVALID_USER:
		linux_audit_record_event(-1, audit_username(), NULL,
		    ssh_remote_ipaddr(ssh), "sshd", 0);
		break;
	default:
		debug("%s: unhandled event %d", __func__, event);
		break;
	}
}
#endif /* USE_LINUX_AUDIT */
