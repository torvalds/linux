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
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

#include <bsm/audit.h>
#include <bsm/audit_uevents.h>
#include <bsm/auditd_lib.h>
#include <bsm/libbsm.h>

#include <asl.h>
#include <launch.h>
#include <notify.h>
#include <mach/port.h>
#include <mach/mach_error.h>
#include <mach/mach_traps.h>
#include <mach/mach.h>
#include <mach/host_special_ports.h>

#include "auditd.h"

#include "auditd_controlServer.h"
#include "audit_triggersServer.h"

/*
 * Apple System Logger Handles.
 */
static aslmsg 		au_aslmsg = NULL;
static aslclient	au_aslclient = NULL;

static mach_port_t	control_port = MACH_PORT_NULL;
static mach_port_t	signal_port = MACH_PORT_NULL;
static mach_port_t	port_set = MACH_PORT_NULL;

/*
 * Current auditing state (cache).
 */ 
static int		auditing_state = AUD_STATE_INIT;

/*
 * Maximum idle time before auditd terminates under launchd.
 * If it is zero then auditd does not timeout while idle.
 */
static int		max_idletime = 0;

#ifndef	__BSM_INTERNAL_NOTIFY_KEY
#define	__BSM_INTERNAL_NOTIFY_KEY	"com.apple.audit.change"
#endif /* __BSM_INTERNAL_NOTIFY_KEY */

#ifndef	__AUDIT_LAUNCHD_LABEL
#define	__AUDIT_LAUNCHD_LABEL		"com.apple.auditd"
#endif /* __AUDIT_LAUNCHD_LABEL */

#define	MAX_MSG_SIZE	4096

/*
 * Open and set up system logging.
 */
void
auditd_openlog(int debug, gid_t gid)
{
	uint32_t opt = 0;	
	char *cp = NULL;

	if (debug)
		opt = ASL_OPT_STDERR;

	au_aslclient = asl_open("auditd", "com.apple.auditd", opt);
	au_aslmsg = asl_new(ASL_TYPE_MSG); 

#ifdef ASL_KEY_READ_UID
	/*
	 * Make it only so the audit administrator and members of the audit
	 * review group (if used) have access to the auditd system log messages.
	 */
	asl_set(au_aslmsg, ASL_KEY_READ_UID, "0");
	asprintf(&cp, "%u", gid);	
	if (cp != NULL) {
#ifdef ASL_KEY_READ_GID
		asl_set(au_aslmsg, ASL_KEY_READ_GID, cp);
#endif
		free(cp);
	}
#endif

	/*
	 * Set the client-side system log filtering.
	 */
	if (debug)
		asl_set_filter(au_aslclient,
		    ASL_FILTER_MASK_UPTO(ASL_LEVEL_DEBUG));
	else
		asl_set_filter(au_aslclient,
		    ASL_FILTER_MASK_UPTO(ASL_LEVEL_INFO)); 	
}

/*
 * Log messages at different priority levels. 
 */
void
auditd_log_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	asl_vlog(au_aslclient, au_aslmsg, ASL_LEVEL_ERR, fmt, ap);
	va_end(ap);
}

void
auditd_log_notice(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	asl_vlog(au_aslclient, au_aslmsg, ASL_LEVEL_NOTICE, fmt, ap);
	va_end(ap);
}

void
auditd_log_info(const char *fmt, ...)
{
	va_list ap;
	
	va_start(ap, fmt);
	asl_vlog(au_aslclient, au_aslmsg, ASL_LEVEL_INFO, fmt, ap);
	va_end(ap);
}

void
auditd_log_debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	asl_vlog(au_aslclient, au_aslmsg, ASL_LEVEL_DEBUG, fmt, ap);
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
 * Update the cached auditing state.  Let other tasks that may be caching it
 * as well to update their state via notify(3).
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
		notify_post(__BSM_INTERNAL_NOTIFY_KEY);

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

	if (auditing_state == AUD_STATE_INIT) {
		init_audit_state();
		notify_post(__BSM_INTERNAL_NOTIFY_KEY);
	}

	return (auditing_state);
}

/*
 * Lookup the audit mach port in the launchd dictionary.
 */
static mach_port_t
lookup_machport(const char *label)
{
	launch_data_t msg, msd, ld, cdict, to;
	mach_port_t mp = MACH_PORT_NULL;

	msg = launch_data_new_string(LAUNCH_KEY_CHECKIN);

	cdict = launch_msg(msg);
	if (cdict == NULL) {
		auditd_log_err("launch_msg(\"" LAUNCH_KEY_CHECKIN
		    "\") IPC failure: %m");
                return (MACH_PORT_NULL);
        }

	if (launch_data_get_type(cdict) == LAUNCH_DATA_ERRNO) {
		errno = launch_data_get_errno(cdict);
		auditd_log_err("launch_data_get_type() can't get dict: %m");
		return (MACH_PORT_NULL);
	}

	to = launch_data_dict_lookup(cdict, LAUNCH_JOBKEY_TIMEOUT);
	if (to) {
		max_idletime = launch_data_get_integer(to);
		auditd_log_debug("launchd timeout set to %d", max_idletime);
	} else {
		auditd_log_debug("launchd timeout not set, setting to 60");
		max_idletime = 60;
	}

	msd = launch_data_dict_lookup(cdict, LAUNCH_JOBKEY_MACHSERVICES);
	if (msd == NULL) {
		auditd_log_err(
		    "launch_data_dict_lookup() can't get mach services");
		return (MACH_PORT_NULL);
	}

	ld = launch_data_dict_lookup(msd, label);
	if (ld == NULL) {
		auditd_log_err("launch_data_dict_lookup can't find %s", label);
		return (MACH_PORT_NULL);
	}

	mp = launch_data_get_machport(ld);

	return (mp);
}

static int
mach_setup(int launchd_flag)
{
	mach_msg_type_name_t poly;

	/*
	 * Allocate a port set.
	 */
	if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET,
	    &port_set) != KERN_SUCCESS)  {
		auditd_log_err("Allocation of port set failed");
		return (-1);
	}


	/*
	 * Allocate a signal reflection port.
	 */
	if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
	    &signal_port) != KERN_SUCCESS ||
	    mach_port_move_member(mach_task_self(), signal_port, port_set) !=
	    KERN_SUCCESS)  {
		auditd_log_err("Allocation of signal port failed");
		return (-1);
	}

	/*
	 * Allocate a trigger port.
	 */
	if (launchd_flag) {
		/*
		 * If started under launchd, lookup port in launchd dictionary.
		 */
		if ((control_port = lookup_machport(__AUDIT_LAUNCHD_LABEL)) ==
		    MACH_PORT_NULL || mach_port_move_member(mach_task_self(),
		    control_port, port_set) != KERN_SUCCESS) {
			auditd_log_err("Cannot get Mach control port"
                            " via launchd");
			return (-1);
		} else
			auditd_log_debug("Mach control port registered"
			    " via launchd");
	} else {
		/*
		 * If not started under launchd, allocate port and register.
		 */
		if (mach_port_allocate(mach_task_self(),
		    MACH_PORT_RIGHT_RECEIVE, &control_port) != KERN_SUCCESS ||
		    mach_port_move_member(mach_task_self(), control_port,
		    port_set) != KERN_SUCCESS)
			auditd_log_err("Allocation of trigger port failed");

		/*
		 * Create a send right on our trigger port.
		 */
		mach_port_extract_right(mach_task_self(), control_port,
		    MACH_MSG_TYPE_MAKE_SEND, &control_port, &poly);

		/*
		 * Register the trigger port with the kernel.
		 */
		if (host_set_audit_control_port(mach_host_self(),
		    control_port) != KERN_SUCCESS) {
                        auditd_log_err("Cannot set Mach control port");
			return (-1);
		} else
			auditd_log_debug("Mach control port registered");
	}

	return (0);
}

/*
 * Open the trigger messaging mechanism.
 */
int
auditd_open_trigger(int launchd_flag)
{
	
	return (mach_setup(launchd_flag));
}

/*
 * Close the trigger messaging mechanism.
 */
int
auditd_close_trigger(void)
{

	return (0);
}

/*
 * Combined server handler.  Called by the mach message loop when there is
 * a trigger or signal message.
 */
static boolean_t
auditd_combined_server(mach_msg_header_t *InHeadP, mach_msg_header_t *OutHeadP)
{
	mach_port_t local_port = InHeadP->msgh_local_port;

	/* Reset the idle time alarm, if used. */
	if (max_idletime)
		alarm(max_idletime);

	if (local_port == signal_port) {
		int signo = InHeadP->msgh_id;

		switch(signo) {
		case SIGTERM:
		case SIGALRM:
			auditd_terminate();
			/* Not reached. */

		case SIGCHLD:
			auditd_reap_children();
			return (TRUE);

		case SIGHUP:
			auditd_config_controls();
			return (TRUE);

		default:
			auditd_log_info("Received signal %d", signo);
			return (TRUE);
		}
	} else if (local_port == control_port) {
		boolean_t result;

		result = audit_triggers_server(InHeadP, OutHeadP);
		if (!result)
			result = auditd_control_server(InHeadP, OutHeadP);
			return (result);
	}
	auditd_log_info("Recevied msg on bad port 0x%x.", local_port);
	return (FALSE);
}

/*
 * The main event loop.  Wait for trigger messages or signals and handle them.
 * It should not return unless there is a problem.
 */
void
auditd_wait_for_events(void)
{
	kern_return_t   result;

	/*
	 * Call the mach messaging server loop.
 	 */
	result = mach_msg_server(auditd_combined_server, MAX_MSG_SIZE,
	    port_set, MACH_MSG_OPTION_NONE);
}

/*
 * Implementation of the audit_triggers() MIG simpleroutine.  Simply a 
 * wrapper function.  This handles input from the kernel on the host
 * special mach port.
 */
kern_return_t
audit_triggers(mach_port_t __unused audit_port, int trigger)
{

	auditd_handle_trigger(trigger);

	return (KERN_SUCCESS);
}

/*
 * Implementation of the auditd_control() MIG simpleroutine.  Simply a 
 * wrapper function.  This handles input from the audit(1) tool.
 */
kern_return_t
auditd_control(mach_port_t __unused auditd_port, int trigger)
{
	
	auditd_handle_trigger(trigger);
	
	return (KERN_SUCCESS);
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
	mach_msg_empty_send_t msg;

	msg.header.msgh_id = signal;
	msg.header.msgh_remote_port = signal_port;
	msg.header.msgh_local_port = MACH_PORT_NULL;
	msg.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
	mach_msg(&(msg.header), MACH_SEND_MSG|MACH_SEND_TIMEOUT, sizeof(msg),
	    0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
}
