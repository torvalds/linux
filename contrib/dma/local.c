/*
 * Copyright (c) 2008-2014, Simon Schubert <2@0x2c.org>.
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Simon Schubert <2@0x2c.org>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "dma.h"

static int
create_mbox(const char *name)
{
	struct sigaction sa, osa;
	pid_t child, waitchild;
	int status;
	int i;
	long maxfd;
	int e;
	int r = -1;

	/*
	 * We need to enable SIGCHLD temporarily so that waitpid works.
	 */
	bzero(&sa, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sa, &osa);

	do_timeout(100, 0);

	child = fork();
	switch (child) {
	case 0:
		/* child */
		maxfd = sysconf(_SC_OPEN_MAX);
		if (maxfd == -1)
			maxfd = 1024;	/* what can we do... */

		for (i = 3; i <= maxfd; ++i)
			close(i);

		execl(LIBEXEC_PATH "/dma-mbox-create", "dma-mbox-create", name, NULL);
		syslog(LOG_ERR, "cannot execute "LIBEXEC_PATH"/dma-mbox-create: %m");
		exit(EX_SOFTWARE);

	default:
		/* parent */
		waitchild = waitpid(child, &status, 0);

		e = errno;

		do_timeout(0, 0);

		if (waitchild == -1 && e == EINTR) {
			syslog(LOG_ERR, "hung child while creating mbox `%s': %m", name);
			break;
		}

		if (waitchild == -1) {
			syslog(LOG_ERR, "child disappeared while creating mbox `%s': %m", name);
			break;
		}

		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			syslog(LOG_ERR, "error creating mbox `%s'", name);
			break;
		}

		/* success */
		r = 0;
		break;

	case -1:
		/* error */
		syslog(LOG_ERR, "error creating mbox");
		break;
	}

	sigaction(SIGCHLD, &osa, NULL);

	return (r);
}

int
deliver_local(struct qitem *it)
{
	char fn[PATH_MAX+1];
	char line[1000];
	const char *sender;
	const char *newline = "\n";
	size_t linelen;
	int tries = 0;
	int mbox;
	int error;
	int hadnl = 0;
	off_t mboxlen;
	time_t now = time(NULL);

	error = snprintf(fn, sizeof(fn), "%s/%s", _PATH_MAILDIR, it->addr);
	if (error < 0 || (size_t)error >= sizeof(fn)) {
		syslog(LOG_NOTICE, "local delivery deferred: %m");
		return (1);
	}

retry:
	/* wait for a maximum of 100s to get the lock to the file */
	do_timeout(100, 0);

	/* don't use O_CREAT here, because we might be running as the wrong user. */
	mbox = open_locked(fn, O_WRONLY|O_APPEND);
	if (mbox < 0) {
		int e = errno;

		do_timeout(0, 0);

		switch (e) {
		case EACCES:
		case ENOENT:
			/*
			 * The file does not exist or we can't access it.
			 * Call dma-mbox-create to create it and fix permissions.
			 */
			if (tries > 0 || create_mbox(it->addr) != 0) {
				syslog(LOG_ERR, "local delivery deferred: can not create `%s'", fn);
				return (1);
			}
			++tries;
			goto retry;

		case EINTR:
			syslog(LOG_NOTICE, "local delivery deferred: can not lock `%s'", fn);
			break;

		default:
			syslog(LOG_NOTICE, "local delivery deferred: can not open `%s': %m", fn);
			break;
		}
		return (1);
	}
	do_timeout(0, 0);

	mboxlen = lseek(mbox, 0, SEEK_END);

	/* New mails start with \nFrom ...., unless we're at the beginning of the mbox */
	if (mboxlen == 0)
		newline = "";

	/* If we're bouncing a message, claim it comes from MAILER-DAEMON */
	sender = it->sender;
	if (strcmp(sender, "") == 0)
		sender = "MAILER-DAEMON";

	if (fseek(it->mailf, 0, SEEK_SET) != 0) {
		syslog(LOG_NOTICE, "local delivery deferred: can not seek: %m");
		goto out;
	}

	error = snprintf(line, sizeof(line), "%sFrom %s %s", newline, sender, ctime(&now));
	if (error < 0 || (size_t)error >= sizeof(line)) {
		syslog(LOG_NOTICE, "local delivery deferred: can not write header: %m");
		goto out;
	}
	if (write(mbox, line, error) != error)
		goto wrerror;

	while (!feof(it->mailf)) {
		if (fgets(line, sizeof(line), it->mailf) == NULL)
			break;
		linelen = strlen(line);
		if (linelen == 0 || line[linelen - 1] != '\n') {
			syslog(LOG_CRIT, "local delivery failed: corrupted queue file");
			snprintf(errmsg, sizeof(errmsg), "corrupted queue file");
			error = -1;
			goto chop;
		}

		/*
		 * mboxro processing:
		 * - escape lines that start with "From " with a > sign.
		 * - be reversable by escaping lines that contain an arbitrary
		 *   number of > signs, followed by "From ", i.e. />*From / in regexp.
		 * - strict mbox processing only requires escaping after empty lines,
		 *   yet most MUAs seem to relax this requirement and will treat any
		 *   line starting with "From " as the beginning of a new mail.
		 */
		if ((!MBOX_STRICT || hadnl) &&
		    strncmp(&line[strspn(line, ">")], "From ", 5) == 0) {
			const char *gt = ">";

			if (write(mbox, gt, 1) != 1)
				goto wrerror;
			hadnl = 0;
		} else if (strcmp(line, "\n") == 0) {
			hadnl = 1;
		} else {
			hadnl = 0;
		}
		if ((size_t)write(mbox, line, linelen) != linelen)
			goto wrerror;
	}
	close(mbox);
	return (0);

wrerror:
	syslog(LOG_ERR, "local delivery failed: write error: %m");
	error = 1;
chop:
	if (ftruncate(mbox, mboxlen) != 0)
		syslog(LOG_WARNING, "error recovering mbox `%s': %m", fn);
out:
	close(mbox);
	return (error);
}
