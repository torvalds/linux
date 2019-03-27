/*-
 * Copyright (c) 2005-2009 Apple Inc.
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
/*
 * Program to trigger the audit daemon with a message that is either:
 *    - Open a new audit log file
 *    - Read the audit control file and take action on it
 *    - Close the audit log file and exit
 *
 */

#include <sys/types.h>
#include <config/config.h>
#ifdef HAVE_FULL_QUEUE_H
#include <sys/queue.h>
#else /* !HAVE_FULL_QUEUE_H */
#include <compat/queue.h>
#endif /* !HAVE_FULL_QUEUE_H */
#include <sys/uio.h>

#include <bsm/libbsm.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


static int send_trigger(int);

#ifdef USE_MACH_IPC
#include <mach/mach.h>
#include <servers/netname.h>
#include <mach/message.h>
#include <mach/port.h>
#include <mach/mach_error.h>
#include <mach/host_special_ports.h>
#include <servers/bootstrap.h>

#include "auditd_control.h"

/* 
 * XXX The following are temporary until these can be added to the kernel
 * audit.h header. 
 */
#ifndef AUDIT_TRIGGER_INITIALIZE
#define	AUDIT_TRIGGER_INITIALIZE	7
#endif
#ifndef AUDIT_TRIGGER_EXPIRE_TRAILS
#define	AUDIT_TRIGGER_EXPIRE_TRAILS	8
#endif

static int
send_trigger(int trigger)
{
	mach_port_t     serverPort;
	kern_return_t	error;

	error = host_get_audit_control_port(mach_host_self(), &serverPort);
	if (error != KERN_SUCCESS) {
		if (geteuid() != 0) {
			errno = EPERM;
			perror("audit requires root privileges"); 
		} else 
			mach_error("Cannot get auditd_control Mach port:",
			    error);
		return (-1);
	}

	error = auditd_control(serverPort, trigger);
	if (error != KERN_SUCCESS) {
		mach_error("Error sending trigger: ", error);
		return (-1);
	}
	
	return (0);
}

#else /* ! USE_MACH_IPC */

static int
send_trigger(int trigger)
{
	int error;

	error = audit_send_trigger(&trigger);
	if (error != 0) {
		if (error == EPERM)
			perror("audit requires root privileges");
		else
			perror("Error sending trigger");
		return (-1);
	}

	return (0);
}
#endif /* ! USE_MACH_IPC */

static void
usage(void)
{

	(void)fprintf(stderr, "Usage: audit -e | -i | -n | -s | -t \n");
	exit(-1);
}

/*
 * Main routine to process command line options.
 */
int
main(int argc, char **argv)
{
	int ch;
	unsigned int trigger = 0;

	if (argc != 2)
		usage();

	while ((ch = getopt(argc, argv, "einst")) != -1) {
		switch(ch) {

		case 'e':
			trigger = AUDIT_TRIGGER_EXPIRE_TRAILS;
			break;

		case 'i':
			trigger = AUDIT_TRIGGER_INITIALIZE;
			break;

		case 'n':
			trigger = AUDIT_TRIGGER_ROTATE_USER;
			break;

		case 's':
			trigger = AUDIT_TRIGGER_READ_FILE;
			break;

		case 't':
			trigger = AUDIT_TRIGGER_CLOSE_AND_DIE;
			break;

		case '?':
		default:
			usage();
			break;
		}
	}
	if (send_trigger(trigger) < 0) 
		exit(-1);

	printf("Trigger sent.\n");
	exit (0);
}
