/*-
 * Copyright (c) 2002-2003 Networks Associates Technology, Inc.
 * Copyright (c) 2004-2014 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * Network Associates Laboratories, the Security Research Division of
 * Network Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $OpenPAM: openpam_ttyconv.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#include <sys/poll.h>
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"
#include "openpam_strlset.h"

int openpam_ttyconv_timeout = 0;

static volatile sig_atomic_t caught_signal;

/*
 * Handle incoming signals during tty conversation
 */
static void
catch_signal(int signo)
{

	switch (signo) {
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		caught_signal = signo;
		break;
	}
}

/*
 * Accept a response from the user on a tty
 */
static int
prompt_tty(int ifd, int ofd, const char *message, char *response, int echo)
{
	struct sigaction action;
	struct sigaction saction_sigint, saction_sigquit, saction_sigterm;
	struct termios tcattr;
	struct timeval now, target, remaining;
	int remaining_ms;
	tcflag_t slflag;
	struct pollfd pfd;
	int serrno;
	int pos, ret;
	char ch;

	/* write prompt */
	if (write(ofd, message, strlen(message)) < 0) {
		openpam_log(PAM_LOG_ERROR, "write(): %m");
		return (-1);
	}

	/* turn echo off if requested */
	slflag = 0; /* prevent bogus uninitialized variable warning */
	if (!echo) {
		if (tcgetattr(ifd, &tcattr) != 0) {
			openpam_log(PAM_LOG_ERROR, "tcgetattr(): %m");
			return (-1);
		}
		slflag = tcattr.c_lflag;
		tcattr.c_lflag &= ~ECHO;
		if (tcsetattr(ifd, TCSAFLUSH, &tcattr) != 0) {
			openpam_log(PAM_LOG_ERROR, "tcsetattr(): %m");
			return (-1);
		}
	}

	/* install signal handlers */
	caught_signal = 0;
	action.sa_handler = &catch_signal;
	action.sa_flags = 0;
	sigfillset(&action.sa_mask);
	sigaction(SIGINT, &action, &saction_sigint);
	sigaction(SIGQUIT, &action, &saction_sigquit);
	sigaction(SIGTERM, &action, &saction_sigterm);

	/* compute timeout */
	if (openpam_ttyconv_timeout > 0) {
		(void)gettimeofday(&now, NULL);
		remaining.tv_sec = openpam_ttyconv_timeout;
		remaining.tv_usec = 0;
		timeradd(&now, &remaining, &target);
	} else {
		/* prevent bogus uninitialized variable warning */
		now.tv_sec = now.tv_usec = 0;
		remaining.tv_sec = remaining.tv_usec = 0;
		target.tv_sec = target.tv_usec = 0;
	}

	/* input loop */
	pos = 0;
	ret = -1;
	serrno = 0;
	while (!caught_signal) {
		pfd.fd = ifd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (openpam_ttyconv_timeout > 0) {
			gettimeofday(&now, NULL);
			if (timercmp(&now, &target, >))
				break;
			timersub(&target, &now, &remaining);
			remaining_ms = remaining.tv_sec * 1000 +
			    remaining.tv_usec / 1000;
		} else {
			remaining_ms = -1;
		}
		if ((ret = poll(&pfd, 1, remaining_ms)) < 0) {
			serrno = errno;
			if (errno == EINTR)
				continue;
			openpam_log(PAM_LOG_ERROR, "poll(): %m");
			break;
		} else if (ret == 0) {
			/* timeout */
			write(ofd, " timed out", 10);
			openpam_log(PAM_LOG_NOTICE, "timed out");
			break;
		}
		if ((ret = read(ifd, &ch, 1)) < 0) {
			serrno = errno;
			openpam_log(PAM_LOG_ERROR, "read(): %m");
			break;
		} else if (ret == 0 || ch == '\n') {
			response[pos] = '\0';
			ret = pos;
			break;
		}
		if (pos + 1 < PAM_MAX_RESP_SIZE)
			response[pos++] = ch;
		/* overflow is discarded */
	}

	/* restore tty state */
	if (!echo) {
		tcattr.c_lflag = slflag;
		if (tcsetattr(ifd, 0, &tcattr) != 0) {
			/* treat as non-fatal, since we have our answer */
			openpam_log(PAM_LOG_NOTICE, "tcsetattr(): %m");
		}
	}

	/* restore signal handlers and re-post caught signal*/
	sigaction(SIGINT, &saction_sigint, NULL);
	sigaction(SIGQUIT, &saction_sigquit, NULL);
	sigaction(SIGTERM, &saction_sigterm, NULL);
	if (caught_signal != 0) {
		openpam_log(PAM_LOG_ERROR, "caught signal %d",
		    (int)caught_signal);
		raise((int)caught_signal);
		/* if raise() had no effect... */
		serrno = EINTR;
		ret = -1;
	}

	/* done */
	write(ofd, "\n", 1);
	errno = serrno;
	return (ret);
}

/*
 * Accept a response from the user on a non-tty stdin.
 */
static int
prompt_notty(const char *message, char *response)
{
	struct timeval now, target, remaining;
	int remaining_ms;
	struct pollfd pfd;
	int ch, pos, ret;

	/* show prompt */
	fputs(message, stdout);
	fflush(stdout);

	/* compute timeout */
	if (openpam_ttyconv_timeout > 0) {
		(void)gettimeofday(&now, NULL);
		remaining.tv_sec = openpam_ttyconv_timeout;
		remaining.tv_usec = 0;
		timeradd(&now, &remaining, &target);
	} else {
		/* prevent bogus uninitialized variable warning */
		now.tv_sec = now.tv_usec = 0;
		remaining.tv_sec = remaining.tv_usec = 0;
		target.tv_sec = target.tv_usec = 0;
	}

	/* input loop */
	pos = 0;
	for (;;) {
		pfd.fd = STDIN_FILENO;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (openpam_ttyconv_timeout > 0) {
			gettimeofday(&now, NULL);
			if (timercmp(&now, &target, >))
				break;
			timersub(&target, &now, &remaining);
			remaining_ms = remaining.tv_sec * 1000 +
			    remaining.tv_usec / 1000;
		} else {
			remaining_ms = -1;
		}
		if ((ret = poll(&pfd, 1, remaining_ms)) < 0) {
			/* interrupt is ok, everything else -> bail */
			if (errno == EINTR)
				continue;
			perror("\nopenpam_ttyconv");
			return (-1);
		} else if (ret == 0) {
			/* timeout */
			break;
		} else {
			/* input */
			if ((ch = getchar()) == EOF && ferror(stdin)) {
				perror("\nopenpam_ttyconv");
				return (-1);
			}
			if (ch == EOF || ch == '\n') {
				response[pos] = '\0';
				return (pos);
			}
			if (pos + 1 < PAM_MAX_RESP_SIZE)
				response[pos++] = ch;
			/* overflow is discarded */
		}
	}
	fputs("\nopenpam_ttyconv: timeout\n", stderr);
	return (-1);
}

/*
 * Determine whether stdin is a tty; if not, try to open the tty; in
 * either case, call the appropriate method.
 */
static int
prompt(const char *message, char *response, int echo)
{
	int ifd, ofd, ret;

	if (isatty(STDIN_FILENO)) {
		fflush(stdout);
#ifdef HAVE_FPURGE
		fpurge(stdin);
#endif
		ifd = STDIN_FILENO;
		ofd = STDOUT_FILENO;
	} else {
		if ((ifd = open("/dev/tty", O_RDWR)) < 0)
			/* no way to prevent echo */
			return (prompt_notty(message, response));
		ofd = ifd;
	}
	ret = prompt_tty(ifd, ofd, message, response, echo);
	if (ifd != STDIN_FILENO)
		close(ifd);
	return (ret);
}

/*
 * OpenPAM extension
 *
 * Simple tty-based conversation function
 */

int
openpam_ttyconv(int n,
	 const struct pam_message **msg,
	 struct pam_response **resp,
	 void *data)
{
	char respbuf[PAM_MAX_RESP_SIZE];
	struct pam_response *aresp;
	int i;

	ENTER();
	(void)data;
	if (n <= 0 || n > PAM_MAX_NUM_MSG)
		RETURNC(PAM_CONV_ERR);
	if ((aresp = calloc(n, sizeof *aresp)) == NULL)
		RETURNC(PAM_BUF_ERR);
	for (i = 0; i < n; ++i) {
		aresp[i].resp_retcode = 0;
		aresp[i].resp = NULL;
		switch (msg[i]->msg_style) {
		case PAM_PROMPT_ECHO_OFF:
			if (prompt(msg[i]->msg, respbuf, 0) < 0 ||
			    (aresp[i].resp = strdup(respbuf)) == NULL)
				goto fail;
			break;
		case PAM_PROMPT_ECHO_ON:
			if (prompt(msg[i]->msg, respbuf, 1) < 0 ||
			    (aresp[i].resp = strdup(respbuf)) == NULL)
				goto fail;
			break;
		case PAM_ERROR_MSG:
			fputs(msg[i]->msg, stderr);
			if (strlen(msg[i]->msg) > 0 &&
			    msg[i]->msg[strlen(msg[i]->msg) - 1] != '\n')
				fputc('\n', stderr);
			break;
		case PAM_TEXT_INFO:
			fputs(msg[i]->msg, stdout);
			if (strlen(msg[i]->msg) > 0 &&
			    msg[i]->msg[strlen(msg[i]->msg) - 1] != '\n')
				fputc('\n', stdout);
			break;
		default:
			goto fail;
		}
	}
	*resp = aresp;
	memset(respbuf, 0, sizeof respbuf);
	RETURNC(PAM_SUCCESS);
fail:
	for (i = 0; i < n; ++i) {
		if (aresp[i].resp != NULL) {
			strlset(aresp[i].resp, 0, PAM_MAX_RESP_SIZE);
			FREE(aresp[i].resp);
		}
	}
	memset(aresp, 0, n * sizeof *aresp);
	FREE(aresp);
	*resp = NULL;
	memset(respbuf, 0, sizeof respbuf);
	RETURNC(PAM_CONV_ERR);
}

/*
 * Error codes:
 *
 *	PAM_SYSTEM_ERR
 *	PAM_BUF_ERR
 *	PAM_CONV_ERR
 */

/**
 * The =openpam_ttyconv function is a standard conversation function
 * suitable for use on TTY devices.
 * It should be adequate for the needs of most text-based interactive
 * programs.
 *
 * The =openpam_ttyconv function allows the application to specify a
 * timeout for user input by setting the global integer variable
 * :openpam_ttyconv_timeout to the length of the timeout in seconds.
 *
 * >openpam_nullconv
 * >pam_prompt
 * >pam_vprompt
 */
