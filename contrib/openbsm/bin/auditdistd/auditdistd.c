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

#include <sys/param.h>
#if defined(HAVE_SYS_ENDIAN_H) && defined(HAVE_BSWAP)
#include <sys/endian.h>
#else /* !HAVE_SYS_ENDIAN_H || !HAVE_BSWAP */
#ifdef HAVE_MACHINE_ENDIAN_H
#include <machine/endian.h>
#else /* !HAVE_MACHINE_ENDIAN_H */
#ifdef HAVE_ENDIAN_H
#include <endian.h>
#else /* !HAVE_ENDIAN_H */
#error "No supported endian.h"
#endif /* !HAVE_ENDIAN_H */
#endif /* !HAVE_MACHINE_ENDIAN_H */
#include <compat/endian.h>
#endif /* !HAVE_SYS_ENDIAN_H || !HAVE_BSWAP */
#include <sys/queue.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <openssl/hmac.h>

#ifndef HAVE_PIDFILE_OPEN
#include <compat/pidfile.h>
#endif
#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif
#ifndef HAVE_SIGTIMEDWAIT
#include "sigtimedwait.h"
#endif

#include "auditdistd.h"
#include "pjdlog.h"
#include "proto.h"
#include "subr.h"
#include "synch.h"

/* Path to configuration file. */
const char *cfgpath = ADIST_CONFIG;
/* Auditdistd configuration. */
static struct adist_config *adcfg;
/* Was SIGINT or SIGTERM signal received? */
bool sigexit_received = false;
/* PID file handle. */
struct pidfh *pfh;

/* How often check for hooks running for too long. */
#define	SIGNALS_CHECK_INTERVAL	5

static void
usage(void)
{

	errx(EX_USAGE, "[-dFhl] [-c config] [-P pidfile]");
}

void
descriptors_cleanup(struct adist_host *adhost)
{
	struct adist_host *adh;
	struct adist_listen *lst;

	TAILQ_FOREACH(adh, &adcfg->adc_hosts, adh_next) {
		if (adh == adhost)
			continue;
		if (adh->adh_remote != NULL) {
			proto_close(adh->adh_remote);
			adh->adh_remote = NULL;
		}
	}
	TAILQ_FOREACH(lst, &adcfg->adc_listen, adl_next) {
		if (lst->adl_conn != NULL)
			proto_close(lst->adl_conn);
	}
	(void)pidfile_close(pfh);
	pjdlog_fini();
}

static void
child_cleanup(struct adist_host *adhost)
{

	if (adhost->adh_conn != NULL) {
		PJDLOG_ASSERT(adhost->adh_role == ADIST_ROLE_SENDER);
		proto_close(adhost->adh_conn);
		adhost->adh_conn = NULL;
	}
	adhost->adh_worker_pid = 0;
}

static void
child_exit_log(const char *type, unsigned int pid, int status)
{

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		pjdlog_debug(1, "%s process exited gracefully (pid=%u).",
		    type, pid);
	} else if (WIFSIGNALED(status)) {
		pjdlog_error("%s process killed (pid=%u, signal=%d).",
		    type, pid, WTERMSIG(status));
	} else {
		pjdlog_error("%s process exited ungracefully (pid=%u, exitcode=%d).",
		    type, pid, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	}
}

static void
child_exit(void)
{
	struct adist_host *adhost;
	bool restart;
	int status;
	pid_t pid;

	restart = false;
	while ((pid = wait3(&status, WNOHANG, NULL)) > 0) {
		/* Find host related to the process that just exited. */
		TAILQ_FOREACH(adhost, &adcfg->adc_hosts, adh_next) {
			if (pid == adhost->adh_worker_pid)
				break;
		}
		if (adhost == NULL) {
			child_exit_log("Sandbox", pid, status);
		} else {
			if (adhost->adh_role == ADIST_ROLE_SENDER)
				restart = true;
			pjdlog_prefix_set("[%s] (%s) ", adhost->adh_name,
			    role2str(adhost->adh_role));
			child_exit_log("Worker", pid, status);
			child_cleanup(adhost);
			pjdlog_prefix_set("%s", "");
		}
	}
	if (!restart)
		return;
	/* We have some sender processes to restart. */
	sleep(1);
	TAILQ_FOREACH(adhost, &adcfg->adc_hosts, adh_next) {
		if (adhost->adh_role != ADIST_ROLE_SENDER)
			continue;
		if (adhost->adh_worker_pid != 0)
			continue;
		pjdlog_prefix_set("[%s] (%s) ", adhost->adh_name,
		    role2str(adhost->adh_role));
		pjdlog_info("Restarting sender process.");
		adist_sender(adcfg, adhost);
		pjdlog_prefix_set("%s", "");
	}
}

/* TODO */
static void
adist_reload(void)
{

	pjdlog_info("Reloading configuration is not yet implemented.");
}

static void
terminate_workers(void)
{
	struct adist_host *adhost;

	pjdlog_info("Termination signal received, exiting.");
	TAILQ_FOREACH(adhost, &adcfg->adc_hosts, adh_next) {
		if (adhost->adh_worker_pid == 0)
			continue;
		pjdlog_info("Terminating worker process (adhost=%s, role=%s, pid=%u).",
		    adhost->adh_name, role2str(adhost->adh_role),
		    adhost->adh_worker_pid);
		if (kill(adhost->adh_worker_pid, SIGTERM) == 0)
			continue;
		pjdlog_errno(LOG_WARNING,
		    "Unable to send signal to worker process (adhost=%s, role=%s, pid=%u).",
		    adhost->adh_name, role2str(adhost->adh_role),
		    adhost->adh_worker_pid);
	}
}

static void
listen_accept(struct adist_listen *lst)
{
	unsigned char rnd[32], hash[32], resp[32];
	struct adist_host *adhost;
	struct proto_conn *conn;
	char adname[ADIST_HOSTSIZE];
	char laddr[256], raddr[256];
	char welcome[8];
	int status, version;
	pid_t pid;

	proto_local_address(lst->adl_conn, laddr, sizeof(laddr));
	pjdlog_debug(1, "Accepting connection to %s.", laddr);

	if (proto_accept(lst->adl_conn, &conn) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to accept connection to %s",
		    laddr);
		return;
	}

	proto_local_address(conn, laddr, sizeof(laddr));
	proto_remote_address(conn, raddr, sizeof(raddr));
	pjdlog_info("Connection from %s to %s.", raddr, laddr);

	/* Error in setting timeout is not critical, but why should it fail? */
	if (proto_timeout(conn, ADIST_TIMEOUT) < 0)
		pjdlog_errno(LOG_WARNING, "Unable to set connection timeout");

	/*
	 * Before receiving any data see if remote host is known.
	 */
	TAILQ_FOREACH(adhost, &adcfg->adc_hosts, adh_next) {
		if (adhost->adh_role != ADIST_ROLE_RECEIVER)
			continue;
		if (!proto_address_match(conn, adhost->adh_remoteaddr))
			continue;
		break;
	}
	if (adhost == NULL) {
		pjdlog_error("Client %s is not known.", raddr);
		goto close;
	}
	/* Ok, remote host is known. */

	/* Exchange welcome message, which include version number. */
	bzero(welcome, sizeof(welcome));
	if (proto_recv(conn, welcome, sizeof(welcome)) == -1) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to receive welcome message from %s",
		    adhost->adh_remoteaddr);
		goto close;
	}
	if (strncmp(welcome, "ADIST", 5) != 0 || !isdigit(welcome[5]) ||
	    !isdigit(welcome[6]) || welcome[7] != '\0') {
		pjdlog_warning("Invalid welcome message from %s.",
		    adhost->adh_remoteaddr);
		goto close;
	}

	version = MIN(ADIST_VERSION, atoi(welcome + 5));

	(void)snprintf(welcome, sizeof(welcome), "ADIST%02d", version);
	if (proto_send(conn, welcome, sizeof(welcome)) == -1) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to send welcome message to %s",
		    adhost->adh_remoteaddr);
		goto close;
	}

	if (proto_recv(conn, adname, sizeof(adhost->adh_name)) < 0) {
		pjdlog_errno(LOG_ERR, "Unable to receive hostname from %s",
		    raddr);
		goto close;
	}

	/* Find host now that we have hostname. */
	TAILQ_FOREACH(adhost, &adcfg->adc_hosts, adh_next) {
		if (adhost->adh_role != ADIST_ROLE_RECEIVER)
			continue;
		if (!proto_address_match(conn, adhost->adh_remoteaddr))
			continue;
		if (strcmp(adhost->adh_name, adname) != 0)
			continue;
		break;
	}
	if (adhost == NULL) {
		pjdlog_error("No configuration for host %s from address %s.",
		    adname, raddr);
		goto close;
	}

	adhost->adh_version = version;
	pjdlog_debug(1, "Version %d negotiated with %s.", adhost->adh_version,
	    adhost->adh_remoteaddr);

	/* Now that we know host name setup log prefix. */
	pjdlog_prefix_set("[%s] (%s) ", adhost->adh_name,
	    role2str(adhost->adh_role));

	if (adist_random(rnd, sizeof(rnd)) == -1) {
		pjdlog_error("Unable to generate challenge.");
		goto close;
	}
	pjdlog_debug(1, "Challenge generated.");

	if (proto_send(conn, rnd, sizeof(rnd)) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to send challenge to %s",
		    adhost->adh_remoteaddr);
		goto close;
	}
	pjdlog_debug(1, "Challenge sent.");

	if (proto_recv(conn, resp, sizeof(resp)) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to receive response from %s",
		    adhost->adh_remoteaddr);
		goto close;
	}
	pjdlog_debug(1, "Response received.");

	if (HMAC(EVP_sha256(), adhost->adh_password,
	    (int)strlen(adhost->adh_password), rnd, (int)sizeof(rnd), hash,
	    NULL) == NULL) {
		pjdlog_error("Unable to generate hash.");
		goto close;
	}
	pjdlog_debug(1, "Hash generated.");

	if (memcmp(resp, hash, sizeof(hash)) != 0) {
		pjdlog_error("Invalid response from %s (wrong password?).",
		    adhost->adh_remoteaddr);
		goto close;
	}
	pjdlog_info("Sender authenticated.");

	if (proto_recv(conn, rnd, sizeof(rnd)) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to receive challenge from %s",
		    adhost->adh_remoteaddr);
		goto close;
	}
	pjdlog_debug(1, "Challenge received.");

	if (HMAC(EVP_sha256(), adhost->adh_password,
	    (int)strlen(adhost->adh_password), rnd, (int)sizeof(rnd), hash,
	    NULL) == NULL) {
		pjdlog_error("Unable to generate response.");
		goto close;
	}
	pjdlog_debug(1, "Response generated.");

	if (proto_send(conn, hash, sizeof(hash)) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to send response to %s",
		    adhost->adh_remoteaddr);
		goto close;
	}
	pjdlog_debug(1, "Response sent.");

	if (adhost->adh_worker_pid != 0) {
		pjdlog_debug(1,
		    "Receiver process exists (pid=%u), stopping it.",
		    (unsigned int)adhost->adh_worker_pid);
		/* Stop child process. */
		if (kill(adhost->adh_worker_pid, SIGINT) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to stop worker process (pid=%u)",
			    (unsigned int)adhost->adh_worker_pid);
			/*
			 * Other than logging the problem we
			 * ignore it - nothing smart to do.
			 */
		}
		/* Wait for it to exit. */
		else if ((pid = waitpid(adhost->adh_worker_pid,
		    &status, 0)) != adhost->adh_worker_pid) {
			/* We can only log the problem. */
			pjdlog_errno(LOG_ERR,
			    "Waiting for worker process (pid=%u) failed",
			    (unsigned int)adhost->adh_worker_pid);
		} else {
			child_exit_log("Worker", adhost->adh_worker_pid,
			    status);
		}
		child_cleanup(adhost);
	}

	adhost->adh_remote = conn;
	adist_receiver(adcfg, adhost);

	pjdlog_prefix_set("%s", "");
	return;
close:
	proto_close(conn);
	pjdlog_prefix_set("%s", "");
}

static void
connection_migrate(struct adist_host *adhost)
{
	struct proto_conn *conn;
	int16_t val = 0;

	pjdlog_prefix_set("[%s] (%s) ", adhost->adh_name,
	    role2str(adhost->adh_role));

	PJDLOG_ASSERT(adhost->adh_role == ADIST_ROLE_SENDER);

	if (proto_recv(adhost->adh_conn, &val, sizeof(val)) < 0) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to receive connection command");
		return;
	}
	if (proto_set("tls:fingerprint", adhost->adh_fingerprint) == -1) {
		val = errno;
		pjdlog_errno(LOG_WARNING, "Unable to set fingerprint");
		goto out;
	}
	if (proto_connect(adhost->adh_localaddr[0] != '\0' ?
	    adhost->adh_localaddr : NULL,
	    adhost->adh_remoteaddr, -1, &conn) < 0) {
		val = errno;
		pjdlog_errno(LOG_WARNING, "Unable to connect to %s",
		    adhost->adh_remoteaddr);
		goto out;
	}
	val = 0;
out:
	if (proto_send(adhost->adh_conn, &val, sizeof(val)) < 0) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to send reply to connection request");
	}
	if (val == 0 && proto_connection_send(adhost->adh_conn, conn) < 0)
		pjdlog_errno(LOG_WARNING, "Unable to send connection");

	pjdlog_prefix_set("%s", "");
}

static void
check_signals(void)
{
	struct timespec sigtimeout;
	sigset_t mask;
	int signo;

	sigtimeout.tv_sec = 0;
	sigtimeout.tv_nsec = 0;

	PJDLOG_VERIFY(sigemptyset(&mask) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGHUP) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGINT) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGTERM) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGCHLD) == 0);

	while ((signo = sigtimedwait(&mask, NULL, &sigtimeout)) != -1) {
		switch (signo) {
		case SIGINT:
		case SIGTERM:
			sigexit_received = true;
			terminate_workers();
			exit(EX_OK);
			break;
		case SIGCHLD:
			child_exit();
			break;
		case SIGHUP:
			adist_reload();
			break;
		default:
			PJDLOG_ABORT("Unexpected signal (%d).", signo);
		}
	}
}

static void
main_loop(void)
{
	struct adist_host *adhost;
	struct adist_listen *lst;
	struct timeval seltimeout;
	int fd, maxfd, ret;
	fd_set rfds;

	seltimeout.tv_sec = SIGNALS_CHECK_INTERVAL;
	seltimeout.tv_usec = 0;

	pjdlog_info("Started successfully.");

	for (;;) {
		check_signals();

		/* Setup descriptors for select(2). */
		FD_ZERO(&rfds);
		maxfd = -1;
		TAILQ_FOREACH(lst, &adcfg->adc_listen, adl_next) {
			if (lst->adl_conn == NULL)
				continue;
			fd = proto_descriptor(lst->adl_conn);
			PJDLOG_ASSERT(fd >= 0);
			FD_SET(fd, &rfds);
			maxfd = fd > maxfd ? fd : maxfd;
		}
		TAILQ_FOREACH(adhost, &adcfg->adc_hosts, adh_next) {
			if (adhost->adh_role == ADIST_ROLE_SENDER) {
				/* Only sender workers asks for connections. */
				PJDLOG_ASSERT(adhost->adh_conn != NULL);
				fd = proto_descriptor(adhost->adh_conn);
				PJDLOG_ASSERT(fd >= 0);
				FD_SET(fd, &rfds);
				maxfd = fd > maxfd ? fd : maxfd;
			} else {
				PJDLOG_ASSERT(adhost->adh_conn == NULL);
			}
		}

		PJDLOG_ASSERT(maxfd + 1 <= (int)FD_SETSIZE);
		ret = select(maxfd + 1, &rfds, NULL, NULL, &seltimeout);
		if (ret == 0) {
			/*
			 * select(2) timed out, so there should be no
			 * descriptors to check.
			 */
			continue;
		} else if (ret == -1) {
			if (errno == EINTR)
				continue;
			KEEP_ERRNO((void)pidfile_remove(pfh));
			pjdlog_exit(EX_OSERR, "select() failed");
		}
		PJDLOG_ASSERT(ret > 0);

		/*
		 * Check for signals before we do anything to update our
		 * info about terminated workers in the meantime.
		 */
		check_signals();

		TAILQ_FOREACH(lst, &adcfg->adc_listen, adl_next) {
			if (lst->adl_conn == NULL)
				continue;
			if (FD_ISSET(proto_descriptor(lst->adl_conn), &rfds))
				listen_accept(lst);
		}
		TAILQ_FOREACH(adhost, &adcfg->adc_hosts, adh_next) {
			if (adhost->adh_role == ADIST_ROLE_SENDER) {
				PJDLOG_ASSERT(adhost->adh_conn != NULL);
				if (FD_ISSET(proto_descriptor(adhost->adh_conn),
				    &rfds)) {
					connection_migrate(adhost);
				}
			} else {
				PJDLOG_ASSERT(adhost->adh_conn == NULL);
			}
		}
	}
}

static void
adist_config_dump(struct adist_config *cfg)
{
	struct adist_host *adhost;
	struct adist_listen *lst;

	pjdlog_debug(2, "Configuration:");
	pjdlog_debug(2, "  Global:");
	pjdlog_debug(2, "    pidfile: %s", cfg->adc_pidfile);
	pjdlog_debug(2, "    timeout: %d", cfg->adc_timeout);
	if (TAILQ_EMPTY(&cfg->adc_listen)) {
		pjdlog_debug(2, "  Sender only, not listening.");
	} else {
		pjdlog_debug(2, "  Listening on:");
		TAILQ_FOREACH(lst, &cfg->adc_listen, adl_next) {
			pjdlog_debug(2, "    listen: %s", lst->adl_addr);
			pjdlog_debug(2, "    conn: %p", lst->adl_conn);
		}
	}
	pjdlog_debug(2, "  Hosts:");
	TAILQ_FOREACH(adhost, &cfg->adc_hosts, adh_next) {
		pjdlog_debug(2, "    name: %s", adhost->adh_name);
		pjdlog_debug(2, "      role: %s", role2str(adhost->adh_role));
		pjdlog_debug(2, "      version: %d", adhost->adh_version);
		pjdlog_debug(2, "      localaddr: %s", adhost->adh_localaddr);
		pjdlog_debug(2, "      remoteaddr: %s", adhost->adh_remoteaddr);
		pjdlog_debug(2, "      remote: %p", adhost->adh_remote);
		pjdlog_debug(2, "      directory: %s", adhost->adh_directory);
		pjdlog_debug(2, "      compression: %d", adhost->adh_compression);
		pjdlog_debug(2, "      checksum: %d", adhost->adh_checksum);
		pjdlog_debug(2, "      pid: %ld", (long)adhost->adh_worker_pid);
		pjdlog_debug(2, "      conn: %p", adhost->adh_conn);
	}
}

static void
dummy_sighandler(int sig __unused)
{
	/* Nothing to do. */
}

int
main(int argc, char *argv[])
{
	struct adist_host *adhost;
	struct adist_listen *lst;
	const char *execpath, *pidfile;
	bool foreground, launchd;
	pid_t otherpid;
	int debuglevel;
	sigset_t mask;

	execpath = argv[0];
	if (execpath[0] != '/') {
		errx(EX_USAGE,
		    "auditdistd requires execution with an absolute path.");
	}

	/*
	 * We are executed from proto to create sandbox.
	 */
	if (argc > 1 && strcmp(argv[1], "proto") == 0) {
		argc -= 2;
		argv += 2;
		if (proto_exec(argc, argv) == -1)
			err(EX_USAGE, "Unable to execute proto");
	}

	foreground = false;
	debuglevel = 0;
	launchd = false;
	pidfile = NULL;

	for (;;) {
		int ch;

		ch = getopt(argc, argv, "c:dFhlP:");
		if (ch == -1)
			break;
		switch (ch) {
		case 'c':
			cfgpath = optarg;
			break;
		case 'd':
			debuglevel++;
			break;
		case 'F':
			foreground = true;
			break;
		case 'l':
			launchd = true;
			break;
		case 'P':
			pidfile = optarg;
			break;
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	pjdlog_init(PJDLOG_MODE_STD);
	pjdlog_debug_set(debuglevel);

	if (proto_set("execpath", execpath) == -1)
		pjdlog_exit(EX_TEMPFAIL, "Unable to set executable name");
	if (proto_set("user", ADIST_USER) == -1)
		pjdlog_exit(EX_TEMPFAIL, "Unable to set proto user");
	if (proto_set("tcp:port", ADIST_TCP_PORT) == -1)
		pjdlog_exit(EX_TEMPFAIL, "Unable to set default TCP port");

	/*
	 * When path to the configuration file is relative, obtain full path,
	 * so we can always find the file, even after daemonizing and changing
	 * working directory to /.
	 */
	if (cfgpath[0] != '/') {
		const char *newcfgpath;

		newcfgpath = realpath(cfgpath, NULL);
		if (newcfgpath == NULL) {
			pjdlog_exit(EX_CONFIG,
			    "Unable to obtain full path of %s", cfgpath);
		}
		cfgpath = newcfgpath;
	}

	adcfg = yy_config_parse(cfgpath, true);
	PJDLOG_ASSERT(adcfg != NULL);
	adist_config_dump(adcfg);

	if (proto_set("tls:certfile", adcfg->adc_certfile) == -1)
		pjdlog_exit(EX_TEMPFAIL, "Unable to set certfile path");
	if (proto_set("tls:keyfile", adcfg->adc_keyfile) == -1)
		pjdlog_exit(EX_TEMPFAIL, "Unable to set keyfile path");

	if (pidfile != NULL) {
		if (strlcpy(adcfg->adc_pidfile, pidfile,
		    sizeof(adcfg->adc_pidfile)) >=
		    sizeof(adcfg->adc_pidfile)) {
			pjdlog_exitx(EX_CONFIG, "Pidfile path is too long.");
		}
	}
	if (foreground && pidfile == NULL) {
		pfh = NULL;
	} else {
		pfh = pidfile_open(adcfg->adc_pidfile, 0600, &otherpid);
		if (pfh == NULL) {
			if (errno == EEXIST) {
				pjdlog_exitx(EX_TEMPFAIL,
				    "Another auditdistd is already running, pid: %jd.",
				    (intmax_t)otherpid);
			}
			/*
			 * If we cannot create pidfile from other reasons,
			 * only warn.
			 */
			pjdlog_errno(LOG_WARNING,
			    "Unable to open or create pidfile %s",
			    adcfg->adc_pidfile);
		}
	}

	/*
	 * Restore default actions for interesting signals in case parent
	 * process (like init(8)) decided to ignore some of them (like SIGHUP).
	 */
	PJDLOG_VERIFY(signal(SIGHUP, SIG_DFL) != SIG_ERR);
	PJDLOG_VERIFY(signal(SIGINT, SIG_DFL) != SIG_ERR);
	PJDLOG_VERIFY(signal(SIGTERM, SIG_DFL) != SIG_ERR);
	/*
	 * Because SIGCHLD is ignored by default, setup dummy handler for it,
	 * so we can mask it.
	 */
	PJDLOG_VERIFY(signal(SIGCHLD, dummy_sighandler) != SIG_ERR);

	PJDLOG_VERIFY(sigemptyset(&mask) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGHUP) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGINT) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGTERM) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGCHLD) == 0);
	PJDLOG_VERIFY(sigprocmask(SIG_SETMASK, &mask, NULL) == 0);

	/* Listen for remote connections. */
	TAILQ_FOREACH(lst, &adcfg->adc_listen, adl_next) {
		if (proto_server(lst->adl_addr, &lst->adl_conn) == -1) {
			KEEP_ERRNO((void)pidfile_remove(pfh));
			pjdlog_exit(EX_OSERR, "Unable to listen on address %s",
			    lst->adl_addr);
		}
	}

	if (!foreground) {
		if (!launchd && daemon(0, 0) == -1) {
			KEEP_ERRNO((void)pidfile_remove(pfh));
			pjdlog_exit(EX_OSERR, "Unable to daemonize");
		}

		/* Start logging to syslog. */
		pjdlog_mode_set(PJDLOG_MODE_SYSLOG);
	}
	if (pfh != NULL) {
		/* Write PID to a file. */
		if (pidfile_write(pfh) < 0) {
			pjdlog_errno(LOG_WARNING,
			    "Unable to write PID to a file");
		}
	}

	TAILQ_FOREACH(adhost, &adcfg->adc_hosts, adh_next) {
		if (adhost->adh_role == ADIST_ROLE_SENDER)
			adist_sender(adcfg, adhost);
	}

	main_loop();

	exit(0);
}
