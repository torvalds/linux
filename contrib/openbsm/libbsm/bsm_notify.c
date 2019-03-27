/*-
 * Copyright (c) 2004-2009 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Based on sample code from Marc Majka.
 */
#include <sys/types.h>

#include <config/config.h>
#ifdef HAVE_FULL_QUEUE_H
#include <sys/queue.h>
#else /* !HAVE_FULL_QUEUE_H */
#include <compat/queue.h>
#endif /* !HAVE_FULL_QUEUE_H */

#include <bsm/audit_internal.h>
#include <bsm/libbsm.h>

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>


#ifdef __APPLE__
#include <notify.h>
/* If 1, assumes a kernel that sends the right notification. */
#define	AUDIT_NOTIFICATION_ENABLED	1

#if AUDIT_NOTIFICATION_ENABLED
static int	token = 0;
#endif	/* AUDIT_NOTIFICATION_ENABLED */

static int	au_cond = AUC_UNSET;	/* <bsm/audit.h> */

uint32_t
au_notify_initialize(void)
{
#if AUDIT_NOTIFICATION_ENABLED
	uint32_t status;
	int ignore_first;

	status = notify_register_check(__BSM_INTERNAL_NOTIFY_KEY, &token);
	if (status != NOTIFY_STATUS_OK)
		return (status);
	status = notify_check(token, &ignore_first);
	if (status != NOTIFY_STATUS_OK)
		return (status);
#endif

	if (audit_get_cond(&au_cond) != 0) {
		syslog(LOG_ERR, "Initial audit status check failed (%s)",
		    strerror(errno));
		if (errno == ENOSYS)	/* auditon() unimplemented. */
			return (AU_UNIMPL);
		return (NOTIFY_STATUS_FAILED);	/* Is there a better code? */
	}
	return (NOTIFY_STATUS_OK);
}

int
au_notify_terminate(void)
{

#if AUDIT_NOTIFICATION_ENABLED
	return ((notify_cancel(token) == NOTIFY_STATUS_OK) ? 0 : -1);
#else
	return (0);
#endif
}

/*
 * On error of any notify(3) call, reset 'au_cond' to ensure we re-run
 * au_notify_initialize() next time 'round--but assume auditing is on.  This
 * is a slight performance hit if auditing is off, but at least the system
 * will behave correctly.  The notification calls are unlikely to fail,
 * anyway.
 */
int
au_get_state(void)
{
#if AUDIT_NOTIFICATION_ENABLED
	int did_notify;
#endif
	int status;

	/*
	 * Don't make the client initialize this set of routines, but take the
	 * slight performance hit by checking ourselves every time.
	 */
	if (au_cond == AUC_UNSET) {
		status = au_notify_initialize();
		if (status != NOTIFY_STATUS_OK) {
			if (status == AU_UNIMPL)
				return (AU_UNIMPL);
			return (AUC_AUDITING);
		} else
			return (au_cond);
	}
#if AUDIT_NOTIFICATION_ENABLED
	status = notify_check(token, &did_notify);
	if (status != NOTIFY_STATUS_OK) {
		au_cond = AUC_UNSET;
		return (AUC_AUDITING);
	}

	if (did_notify == 0)
		return (au_cond);
#endif

	if (audit_get_cond(&au_cond) != 0) {
		/* XXX Reset au_cond to AUC_UNSET? */
		syslog(LOG_ERR, "Audit status check failed (%s)",
		    strerror(errno));
		if (errno == ENOSYS)	/* Function unimplemented. */
			return (AU_UNIMPL);
		return (errno);
	}

	switch (au_cond) {
	case AUC_NOAUDIT:	/* Auditing suspended. */
	case AUC_DISABLED:	/* Auditing shut off. */
		return (AUC_NOAUDIT);

	case AUC_UNSET:		/* Uninitialized; shouldn't get here. */
	case AUC_AUDITING:	/* Audit on. */
	default:
		return (AUC_AUDITING);
	}
}
#endif	/* !__APPLE__ */

int
cannot_audit(int val __unused)
{
#ifdef __APPLE__
	return (!(au_get_state() == AUC_AUDITING));
#else
	int cond;

	if (audit_get_cond(&cond) != 0) {
		if (errno != ENOSYS) {
			syslog(LOG_ERR, "Audit status check failed (%s)",
			    strerror(errno));
		}
		return (1);
	}
	if (cond == AUC_NOAUDIT || cond == AUC_DISABLED)
		return (1);
	return (0);
#endif	/* !__APPLE__ */
}
