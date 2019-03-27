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
#include <sys/stat.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif
#ifndef HAVE_FSTATAT
#include "fstatat.h"
#endif
#ifndef HAVE_OPENAT
#include "openat.h"
#endif
#ifndef HAVE_RENAMEAT
#include "renameat.h"
#endif

#include "auditdistd.h"
#include "pjdlog.h"
#include "proto.h"
#include "sandbox.h"
#include "subr.h"
#include "synch.h"
#include "trail.h"

static struct adist_config *adcfg;
static struct adist_host *adhost;

static TAILQ_HEAD(, adreq) adist_free_list;
static pthread_mutex_t adist_free_list_lock;
static pthread_cond_t adist_free_list_cond;
static TAILQ_HEAD(, adreq) adist_disk_list;
static pthread_mutex_t adist_disk_list_lock;
static pthread_cond_t adist_disk_list_cond;
static TAILQ_HEAD(, adreq) adist_send_list;
static pthread_mutex_t adist_send_list_lock;
static pthread_cond_t adist_send_list_cond;

static void
adreq_clear(struct adreq *adreq)
{

	adreq->adr_error = -1;
	adreq->adr_byteorder = ADIST_BYTEORDER_UNDEFINED;
	adreq->adr_cmd = ADIST_CMD_UNDEFINED;
	adreq->adr_seq = 0;
	adreq->adr_datasize = 0;
}

static void
init_environment(void)
{
	struct adreq *adreq;
	unsigned int ii;

	TAILQ_INIT(&adist_free_list);
	mtx_init(&adist_free_list_lock);
	cv_init(&adist_free_list_cond);
	TAILQ_INIT(&adist_disk_list);
	mtx_init(&adist_disk_list_lock);
	cv_init(&adist_disk_list_cond);
	TAILQ_INIT(&adist_send_list);
	mtx_init(&adist_send_list_lock);
	cv_init(&adist_send_list_cond);

	for (ii = 0; ii < ADIST_QUEUE_SIZE; ii++) {
		adreq = malloc(sizeof(*adreq) + ADIST_BUF_SIZE);
		if (adreq == NULL) {
			pjdlog_exitx(EX_TEMPFAIL,
			    "Unable to allocate %zu bytes of memory for adreq object.",
			    sizeof(*adreq) + ADIST_BUF_SIZE);
		}
		adreq_clear(adreq);
		TAILQ_INSERT_TAIL(&adist_free_list, adreq, adr_next);
	}
}

static void
adreq_decode_and_validate_header(struct adreq *adreq)
{

	/* Byte-swap only if the sender is using different byte order. */
	if (adreq->adr_byteorder != ADIST_BYTEORDER) {
		adreq->adr_byteorder = ADIST_BYTEORDER;
		adreq->adr_seq = bswap64(adreq->adr_seq);
		adreq->adr_datasize = bswap32(adreq->adr_datasize);
	}

	/* Validate packet header. */

	if (adreq->adr_datasize > ADIST_BUF_SIZE) {
		pjdlog_exitx(EX_PROTOCOL, "Invalid datasize received (%ju).",
		    (uintmax_t)adreq->adr_datasize);
	}

	switch (adreq->adr_cmd) {
	case ADIST_CMD_OPEN:
	case ADIST_CMD_APPEND:
	case ADIST_CMD_CLOSE:
		if (adreq->adr_datasize == 0) {
			pjdlog_exitx(EX_PROTOCOL,
			    "Invalid datasize received (%ju).",
			    (uintmax_t)adreq->adr_datasize);
		}
		break;
	case ADIST_CMD_KEEPALIVE:
	case ADIST_CMD_ERROR:
		if (adreq->adr_datasize > 0) {
			pjdlog_exitx(EX_PROTOCOL,
			    "Invalid datasize received (%ju).",
			    (uintmax_t)adreq->adr_datasize);
		}
		break;
	default:
		pjdlog_exitx(EX_PROTOCOL, "Invalid command received (%hhu).",
		    adreq->adr_cmd);
	}
}

static void
adreq_validate_data(const struct adreq *adreq)
{

	/* Validate packet data. */

	switch (adreq->adr_cmd) {
	case ADIST_CMD_OPEN:
	case ADIST_CMD_CLOSE:
		/*
		 * File name must end up with '\0' and there must be no '\0'
		 * in the middle.
		 */
		if (adreq->adr_data[adreq->adr_datasize - 1] != '\0' ||
		    strchr(adreq->adr_data, '\0') !=
		    (const char *)adreq->adr_data + adreq->adr_datasize - 1) {
			pjdlog_exitx(EX_PROTOCOL,
			    "Invalid file name received.");
		}
		break;
	}
}

/*
 * Thread receives requests from the sender.
 */
static void *
recv_thread(void *arg __unused)
{
	struct adreq *adreq;

	for (;;) {
		pjdlog_debug(3, "recv: Taking free request.");
		QUEUE_TAKE(adreq, &adist_free_list, 0);
		pjdlog_debug(3, "recv: (%p) Got request.", adreq);

		if (proto_recv(adhost->adh_remote, &adreq->adr_packet,
		    sizeof(adreq->adr_packet)) == -1) {
			pjdlog_exit(EX_TEMPFAIL,
			    "Unable to receive request header");
		}
		adreq_decode_and_validate_header(adreq);

		switch (adreq->adr_cmd) {
		case ADIST_CMD_KEEPALIVE:
			adreq->adr_error = 0;
			adreq_log(LOG_DEBUG, 2, -1, adreq,
			    "recv: (%p) Got request header: ", adreq);
			pjdlog_debug(3,
			    "recv: (%p) Moving request to the send queue.",
			    adreq);
			QUEUE_INSERT(adreq, &adist_send_list);
			continue;
		case ADIST_CMD_ERROR:
			pjdlog_error("An error occured on the sender while reading \"%s/%s\".",
			    adhost->adh_directory, adhost->adh_trail_name);
			adreq_log(LOG_DEBUG, 2, ADIST_ERROR_READ, adreq,
			    "recv: (%p) Got request header: ", adreq);
			pjdlog_debug(3,
			    "recv: (%p) Moving request to the send queue.",
			    adreq);
			QUEUE_INSERT(adreq, &adist_disk_list);
			continue;
		case ADIST_CMD_OPEN:
		case ADIST_CMD_APPEND:
		case ADIST_CMD_CLOSE:
			if (proto_recv(adhost->adh_remote, adreq->adr_data,
			    adreq->adr_datasize) == -1) {
				pjdlog_exit(EX_TEMPFAIL,
				    "Unable to receive request data");
			}
			adreq_validate_data(adreq);
			adreq_log(LOG_DEBUG, 2, -1, adreq,
			    "recv: (%p) Got request header: ", adreq);
			pjdlog_debug(3,
			    "recv: (%p) Moving request to the disk queue.",
			    adreq);
			QUEUE_INSERT(adreq, &adist_disk_list);
			break;
		default:
			PJDLOG_ABORT("Invalid condition.");
		}
	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Function that opens trail file requested by the sender.
 * If the file already exist, it has to be the most recent file and it can
 * only be open for append.
 * If the file doesn't already exist, it has to be "older" than all existing
 * files.
 */
static int
receiver_open(const char *filename)
{
	int fd;

	/*
	 * Previous file should be closed by now. Sending OPEN request without
	 * sending CLOSE for the previous file is a sender bug.
	 */
	if (adhost->adh_trail_fd != -1) {
		pjdlog_error("Sender requested opening file \"%s\" without first closing \"%s\".",
		    filename, adhost->adh_trail_name);
		return (ADIST_ERROR_WRONG_ORDER);
	}

	if (!trail_validate_name(filename, NULL)) {
		pjdlog_error("Sender wants to open file \"%s\", which has invalid name.",
		    filename);
		return (ADIST_ERROR_INVALID_NAME);
	}

	switch (trail_name_compare(filename, adhost->adh_trail_name)) {
	case TRAIL_RENAMED:
		if (!trail_is_not_terminated(adhost->adh_trail_name)) {
			pjdlog_error("Terminated trail \"%s/%s\" was unterminated on the sender as \"%s/%s\"?",
			    adhost->adh_directory, adhost->adh_trail_name,
			    adhost->adh_directory, filename);
			return (ADIST_ERROR_INVALID_NAME);
		}
		if (renameat(adhost->adh_trail_dirfd, adhost->adh_trail_name,
		    adhost->adh_trail_dirfd, filename) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to rename file \"%s/%s\" to \"%s/%s\"",
			    adhost->adh_directory, adhost->adh_trail_name,
			    adhost->adh_directory, filename);
			PJDLOG_ASSERT(errno > 0);
			return (ADIST_ERROR_RENAME);
		}
		pjdlog_debug(1, "Renamed file \"%s/%s\" to \"%s/%s\".",
		    adhost->adh_directory, adhost->adh_trail_name,
		    adhost->adh_directory, filename);
		/* FALLTHROUGH */
	case TRAIL_IDENTICAL:
		/* Opening existing file. */
		fd = openat(adhost->adh_trail_dirfd, filename,
		    O_WRONLY | O_APPEND | O_NOFOLLOW);
		if (fd == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to open file \"%s/%s\" for append",
			    adhost->adh_directory, filename);
			PJDLOG_ASSERT(errno > 0);
			return (ADIST_ERROR_OPEN);
		}
		pjdlog_debug(1, "Opened file \"%s/%s\".",
		    adhost->adh_directory, filename);
		break;
	case TRAIL_NEWER:
		/* Opening new file. */
		fd = openat(adhost->adh_trail_dirfd, filename,
		    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
		if (fd == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to create file \"%s/%s\"",
			    adhost->adh_directory, filename);
			PJDLOG_ASSERT(errno > 0);
			return (ADIST_ERROR_CREATE);
		}
		pjdlog_debug(1, "Created file \"%s/%s\".",
		    adhost->adh_directory, filename);
		break;
	case TRAIL_OLDER:
		/* Trying to open old file. */
		pjdlog_error("Sender wants to open an old file \"%s\".", filename);
		return (ADIST_ERROR_OPEN_OLD);
	default:
		PJDLOG_ABORT("Unknown return value from trail_name_compare().");
	}
	PJDLOG_VERIFY(strlcpy(adhost->adh_trail_name, filename,
	    sizeof(adhost->adh_trail_name)) < sizeof(adhost->adh_trail_name));
	adhost->adh_trail_fd = fd;
	return (0);
}

/*
 * Function appends data to the trail file that is currently open.
 */
static int
receiver_append(const unsigned char *data, size_t size)
{
	ssize_t done;
	size_t osize;

	/* We should have opened trail file. */
	if (adhost->adh_trail_fd == -1) {
		pjdlog_error("Sender requested append without first opening file.");
		return (ADIST_ERROR_WRONG_ORDER);
	}

	osize = size;
	while (size > 0) {
		done = write(adhost->adh_trail_fd, data, size);
		if (done == -1) {
			if (errno == EINTR)
				continue;
			pjdlog_errno(LOG_ERR, "Write to \"%s/%s\" failed",
			    adhost->adh_directory, adhost->adh_trail_name);
			PJDLOG_ASSERT(errno > 0);
			return (ADIST_ERROR_WRITE);
		}
		pjdlog_debug(3, "Wrote %zd bytes into \"%s/%s\".", done,
		    adhost->adh_directory, adhost->adh_trail_name);
		size -= done;
	}
	pjdlog_debug(2, "Appended %zu bytes to file \"%s/%s\".",
	    osize, adhost->adh_directory, adhost->adh_trail_name);
	return (0);
}

static int
receiver_close(const char *filename)
{

	/* We should have opened trail file. */
	if (adhost->adh_trail_fd == -1) {
		pjdlog_error("Sender requested closing file without first opening it.");
		return (ADIST_ERROR_WRONG_ORDER);
	}

	/* Validate if we can do the rename. */
	if (!trail_validate_name(adhost->adh_trail_name, filename)) {
		pjdlog_error("Sender wants to close file \"%s\" using name \"%s\".",
		    adhost->adh_trail_name, filename);
		return (ADIST_ERROR_INVALID_NAME);
	}

	PJDLOG_VERIFY(close(adhost->adh_trail_fd) == 0);
	adhost->adh_trail_fd = -1;

	pjdlog_debug(1, "Closed file \"%s/%s\".", adhost->adh_directory,
	    adhost->adh_trail_name);

	if (strcmp(adhost->adh_trail_name, filename) == 0) {
		/* File name didn't change, we are done here. */
		return (0);
	}

	if (renameat(adhost->adh_trail_dirfd, adhost->adh_trail_name,
	    adhost->adh_trail_dirfd, filename) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to rename \"%s\" to \"%s\"",
		    adhost->adh_trail_name, filename);
		PJDLOG_ASSERT(errno > 0);
		return (ADIST_ERROR_RENAME);
	}
	pjdlog_debug(1, "Renamed file \"%s/%s\" to \"%s/%s\".",
	    adhost->adh_directory, adhost->adh_trail_name,
	    adhost->adh_directory, filename);
	PJDLOG_VERIFY(strlcpy(adhost->adh_trail_name, filename,
	    sizeof(adhost->adh_trail_name)) < sizeof(adhost->adh_trail_name));

	return (0);
}

static int
receiver_error(void)
{

	/* We should have opened trail file. */
	if (adhost->adh_trail_fd == -1) {
		pjdlog_error("Sender send read error, but file is not open.");
		return (ADIST_ERROR_WRONG_ORDER);
	}

	PJDLOG_VERIFY(close(adhost->adh_trail_fd) == 0);
	adhost->adh_trail_fd = -1;

	pjdlog_debug(1, "Closed file \"%s/%s\".", adhost->adh_directory,
	    adhost->adh_trail_name);

	return (0);
}

static void *
disk_thread(void *arg __unused)
{
	struct adreq *adreq;

	for (;;) {
		pjdlog_debug(3, "disk: Taking request.");
		QUEUE_TAKE(adreq, &adist_disk_list, 0);
		adreq_log(LOG_DEBUG, 3, -1, adreq, "disk: (%p) Got request: ",
		    adreq);
		/* Handle the actual request. */
		switch (adreq->adr_cmd) {
		case ADIST_CMD_OPEN:
			adreq->adr_error = receiver_open(adreq->adr_data);
			break;
		case ADIST_CMD_APPEND:
			adreq->adr_error = receiver_append(adreq->adr_data,
			    adreq->adr_datasize);
			break;
		case ADIST_CMD_CLOSE:
			adreq->adr_error = receiver_close(adreq->adr_data);
			break;
		case ADIST_CMD_ERROR:
			adreq->adr_error = receiver_error();
			break;
		default:
			PJDLOG_ABORT("Unexpected command (cmd=%hhu).",
			    adreq->adr_cmd);
		}
		if (adreq->adr_error != 0) {
			adreq_log(LOG_ERR, 0, adreq->adr_error, adreq,
			    "Request failed: ");
		}
		pjdlog_debug(3, "disk: (%p) Moving request to the send queue.",
		    adreq);
		QUEUE_INSERT(adreq, &adist_send_list);
	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Thread sends requests back to primary node.
 */
static void *
send_thread(void *arg __unused)
{
	struct adreq *adreq;
	struct adrep adrep;

	for (;;) {
		pjdlog_debug(3, "send: Taking request.");
		QUEUE_TAKE(adreq, &adist_send_list, 0);
		adreq_log(LOG_DEBUG, 3, -1, adreq, "send: (%p) Got request: ",
		    adreq);
		adrep.adrp_byteorder = ADIST_BYTEORDER;
		adrep.adrp_seq = adreq->adr_seq;
		adrep.adrp_error = adreq->adr_error;
		if (proto_send(adhost->adh_remote, &adrep,
		    sizeof(adrep)) == -1) {
			pjdlog_exit(EX_TEMPFAIL, "Unable to send reply");
		}
		pjdlog_debug(3, "send: (%p) Moving request to the free queue.",
		    adreq);
		adreq_clear(adreq);
		QUEUE_INSERT(adreq, &adist_free_list);
	}
	/* NOTREACHED */
	return (NULL);
}

static void
receiver_directory_create(void)
{
	struct passwd *pw;

	/*
	 * According to getpwnam(3) we have to clear errno before calling the
	 * function to be able to distinguish between an error and missing
	 * entry (with is not treated as error by getpwnam(3)).
	 */
	errno = 0;
	pw = getpwnam(ADIST_USER);
	if (pw == NULL) {
		if (errno != 0) {
			pjdlog_exit(EX_NOUSER,
			    "Unable to find info about '%s' user", ADIST_USER);
		} else {
			pjdlog_exitx(EX_NOUSER, "User '%s' doesn't exist.",
			    ADIST_USER);
		}
	}

	if (mkdir(adhost->adh_directory, 0700) == -1) {
		pjdlog_exit(EX_OSFILE, "Unable to create directory \"%s\"",
		    adhost->adh_directory);
	}
	if (chown(adhost->adh_directory, pw->pw_uid, pw->pw_gid) == -1) {
		pjdlog_errno(LOG_ERR,
		    "Unable to change owner of the directory \"%s\"",
		    adhost->adh_directory);
		(void)rmdir(adhost->adh_directory);
		exit(EX_OSFILE);
	}
}

static void
receiver_directory_open(void)
{

#ifdef HAVE_FDOPENDIR
	adhost->adh_trail_dirfd = open(adhost->adh_directory,
	    O_RDONLY | O_DIRECTORY);
	if (adhost->adh_trail_dirfd == -1) {
		if (errno == ENOENT) {
			receiver_directory_create();
			adhost->adh_trail_dirfd = open(adhost->adh_directory,
			    O_RDONLY | O_DIRECTORY);
		}
		if (adhost->adh_trail_dirfd == -1) {
			pjdlog_exit(EX_CONFIG,
			    "Unable to open directory \"%s\"",
			    adhost->adh_directory);
		}
	}
	adhost->adh_trail_dirfp = fdopendir(adhost->adh_trail_dirfd);
	if (adhost->adh_trail_dirfp == NULL) {
		pjdlog_exit(EX_CONFIG, "Unable to fdopen directory \"%s\"",
		    adhost->adh_directory);
	}
#else
	struct stat sb;

	if (stat(adhost->adh_directory, &sb) == -1) {
		if (errno == ENOENT) {
			receiver_directory_create();
		} else {
			pjdlog_exit(EX_CONFIG,
			    "Unable to stat directory \"%s\"",
			    adhost->adh_directory);
		}
	}
	adhost->adh_trail_dirfp = opendir(adhost->adh_directory);
	if (adhost->adh_trail_dirfp == NULL) {
		pjdlog_exit(EX_CONFIG, "Unable to open directory \"%s\"",
		    adhost->adh_directory);
	}
	adhost->adh_trail_dirfd = dirfd(adhost->adh_trail_dirfp);
#endif
}

static void
receiver_connect(void)
{
	uint64_t trail_size;
	struct stat sb;

	PJDLOG_ASSERT(adhost->adh_trail_dirfp != NULL);

	trail_last(adhost->adh_trail_dirfp, adhost->adh_trail_name,
	    sizeof(adhost->adh_trail_name));

	if (adhost->adh_trail_name[0] == '\0') {
		trail_size = 0;
	} else {
		if (fstatat(adhost->adh_trail_dirfd, adhost->adh_trail_name,
		    &sb, AT_SYMLINK_NOFOLLOW) == -1) {
			pjdlog_exit(EX_CONFIG, "Unable to stat \"%s/%s\"",
			    adhost->adh_directory, adhost->adh_trail_name);
		}
		if (!S_ISREG(sb.st_mode)) {
			pjdlog_exitx(EX_CONFIG,
			    "File \"%s/%s\" is not a regular file.",
			    adhost->adh_directory, adhost->adh_trail_name);
		}
		trail_size = sb.st_size;
	}
	trail_size = htole64(trail_size);
	if (proto_send(adhost->adh_remote, &trail_size,
	    sizeof(trail_size)) == -1) {
		pjdlog_exit(EX_TEMPFAIL,
		    "Unable to send size of the most recent trail file");
	}
	if (proto_send(adhost->adh_remote, adhost->adh_trail_name,
	    sizeof(adhost->adh_trail_name)) == -1) {
		pjdlog_exit(EX_TEMPFAIL,
		    "Unable to send name of the most recent trail file");
	}
}

void
adist_receiver(struct adist_config *config, struct adist_host *adh)
{
	sigset_t mask;
	pthread_t td;
	pid_t pid;
	int error, mode, debuglevel;

	pid = fork();
	if (pid == -1) {
		pjdlog_errno(LOG_ERR, "Unable to fork");
		proto_close(adh->adh_remote);
		adh->adh_remote = NULL;
		return;
	}

	if (pid > 0) {
		/* This is parent. */
		proto_close(adh->adh_remote);
		adh->adh_remote = NULL;
		adh->adh_worker_pid = pid;
		return;
	}

	adcfg = config;
	adhost = adh;
	mode = pjdlog_mode_get();
	debuglevel = pjdlog_debug_get();

	descriptors_cleanup(adhost);

//	descriptors_assert(adhost, mode);

	pjdlog_init(mode);
	pjdlog_debug_set(debuglevel);
	pjdlog_prefix_set("[%s] (%s) ", adhost->adh_name,
	    role2str(adhost->adh_role));
#ifdef HAVE_SETPROCTITLE
	setproctitle("%s (%s)", adhost->adh_name, role2str(adhost->adh_role));
#endif

	PJDLOG_VERIFY(sigemptyset(&mask) == 0);
	PJDLOG_VERIFY(sigprocmask(SIG_SETMASK, &mask, NULL) == 0);

	/* Error in setting timeout is not critical, but why should it fail? */
	if (proto_timeout(adhost->adh_remote, adcfg->adc_timeout) == -1)
		pjdlog_errno(LOG_WARNING, "Unable to set connection timeout");

	init_environment();

	adhost->adh_trail_fd = -1;
	receiver_directory_open();

	if (sandbox(ADIST_USER, true, "auditdistd: %s (%s)",
	    role2str(adhost->adh_role), adhost->adh_name) != 0) {
		exit(EX_CONFIG);
	}
	pjdlog_info("Privileges successfully dropped.");

	receiver_connect();

	error = pthread_create(&td, NULL, recv_thread, adhost);
	PJDLOG_ASSERT(error == 0);
	error = pthread_create(&td, NULL, disk_thread, adhost);
	PJDLOG_ASSERT(error == 0);
	(void)send_thread(adhost);
}
