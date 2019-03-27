/*-
 * Copyright (c) 2004-2009 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <config/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <bsm/audit.h>
#include <bsm/audit_uevents.h>
#include <bsm/auditd_lib.h>
#include <bsm/libbsm.h>

#include "auditd.h"

/*
 * Current auditing state (cache).
 */
static int	auditing_state = AUD_STATE_INIT;

/*
 * Maximum idle time before auditd terminates under launchd.
 * If it is zero then auditd does not timeout while idle.
 */
static int	max_idletime = 0;

static int	sigchlds, sigchlds_handled;
static int	sighups, sighups_handled;
static int	sigterms, sigterms_handled;
static int	sigalrms, sigalrms_handled;

static int	triggerfd = 0;

/*
 *  Open and set up system logging.
 */
void
auditd_openlog(int debug, gid_t __unused gid)
{
	int logopts = LOG_CONS | LOG_PID;

	if (debug)
		logopts |= LOG_PERROR;

#ifdef LOG_SECURITY
	openlog("auditd", logopts, LOG_SECURITY);
#else
	openlog("auditd", logopts, LOG_AUTH);
#endif
}

/*
 * Log messages at different priority levels. 
 */
void
auditd_log_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);
}

void
auditd_log_notice(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_NOTICE, fmt, ap);
	va_end(ap);
}

void
auditd_log_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_INFO, fmt, ap);
	va_end(ap);
}

void
auditd_log_debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_DEBUG, fmt, ap);
	va_end(ap);
}

/*
 * Get the auditing state from the kernel and cache it.
 */
static void
init_audit_state(void)
{
	int au_cond;

	if (audit_get_cond(&au_cond) < 0) {
		if (errno != ENOSYS) {
			auditd_log_err("Audit status check failed (%s)",
			    strerror(errno));
		}
		auditing_state = AUD_STATE_DISABLED;
	} else
		if (au_cond == AUC_NOAUDIT || au_cond == AUC_DISABLED)
			auditing_state = AUD_STATE_DISABLED;
		else
			auditing_state = AUD_STATE_ENABLED;
}

/*
 * Update the cached auditing state.
 */
void
auditd_set_state(int state)
{
	int old_auditing_state = auditing_state;

	if (state == AUD_STATE_INIT) 
		init_audit_state();
	else
		auditing_state = state;

	if (auditing_state != old_auditing_state) {
		if (auditing_state == AUD_STATE_ENABLED)
			auditd_log_notice("Auditing enabled");
		if (auditing_state == AUD_STATE_DISABLED)
			auditd_log_notice("Auditing disabled");
	}
}

/*
 * Get the cached auditing state.
 */
int
auditd_get_state(void)
{

	if (auditing_state == AUD_STATE_INIT)
		init_audit_state();

	return (auditing_state);
}

/*
 * Open the trigger messaging mechanism.
 */
int
auditd_open_trigger(int __unused launchd_flag)
{

	return ((triggerfd = open(AUDIT_TRIGGER_FILE, O_RDONLY, 0)));
}

/*
 * Close the trigger messaging mechanism.
 */
int
auditd_close_trigger(void)
{
	
	return (close(triggerfd));
}

/* 
 * The main event loop.  Wait for trigger messages or signals and handle them.
 * It should not return unless there is a problem.
 */
void
auditd_wait_for_events(void)
{
	int num;
	unsigned int trigger;

	for (;;) {
		num = read(triggerfd, &trigger, sizeof(trigger));
		if ((num == -1) && (errno != EINTR)) {
			auditd_log_err("%s: error %d", __FUNCTION__, errno);
			return;
		}
		
		/* Reset the idle time alarm, if used. */
		if (max_idletime)
			alarm(max_idletime);

		if (sigterms != sigterms_handled) {
			auditd_log_debug("%s: SIGTERM", __FUNCTION__);
			auditd_terminate();
			/* not reached */ 
		}
		if (sigalrms != sigalrms_handled) {
			auditd_log_debug("%s: SIGALRM", __FUNCTION__);
			auditd_terminate();
			/* not reached */ 
		}
 		if (sigchlds != sigchlds_handled) {
			sigchlds_handled = sigchlds;
			auditd_reap_children();
		}
		if (sighups != sighups_handled) {
			auditd_log_debug("%s: SIGHUP", __FUNCTION__);
			sighups_handled = sighups;
			auditd_config_controls();
		}

		if ((num == -1) && (errno == EINTR))
			continue;
		if (num == 0) {
			auditd_log_err("%s: read EOF", __FUNCTION__);
			return;
		}
		auditd_handle_trigger(trigger);
	}
}

/*
 * When we get a signal, we are often not at a clean point.  So, little can
 * be done in the signal handler itself.  Instead,  we send a message to the
 * main servicing loop to do proper handling from a non-signal-handler
 * context.
 */
void
auditd_relay_signal(int signal)
{
        if (signal == SIGHUP)
                sighups++;
        if (signal == SIGTERM)
                sigterms++;
        if (signal == SIGCHLD)
                sigchlds++;
	if (signal == SIGALRM)
		sigalrms++;
}

