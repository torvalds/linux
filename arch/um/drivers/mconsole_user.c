/*
 * Copyright (C) 2001 Lennert Buytenhek (buytenh@gnu.org)
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include "kern_constants.h"
#include "mconsole.h"
#include "user.h"

static struct mconsole_command commands[] = {
	/*
	 * With uts namespaces, uts information becomes process-specific, so
	 * we need a process context.  If we try handling this in interrupt
	 * context, we may hit an exiting process without a valid uts
	 * namespace.
	 */
	{ "version", mconsole_version, MCONSOLE_PROC },
	{ "halt", mconsole_halt, MCONSOLE_PROC },
	{ "reboot", mconsole_reboot, MCONSOLE_PROC },
	{ "config", mconsole_config, MCONSOLE_PROC },
	{ "remove", mconsole_remove, MCONSOLE_PROC },
	{ "sysrq", mconsole_sysrq, MCONSOLE_INTR },
	{ "help", mconsole_help, MCONSOLE_INTR },
	{ "cad", mconsole_cad, MCONSOLE_INTR },
	{ "stop", mconsole_stop, MCONSOLE_PROC },
	{ "go", mconsole_go, MCONSOLE_INTR },
	{ "log", mconsole_log, MCONSOLE_INTR },
	{ "proc", mconsole_proc, MCONSOLE_PROC },
	{ "stack", mconsole_stack, MCONSOLE_INTR },
};

/* Initialized in mconsole_init, which is an initcall */
char mconsole_socket_name[256];

int mconsole_reply_v0(struct mc_request *req, char *reply)
{
	struct iovec iov;
	struct msghdr msg;

	iov.iov_base = reply;
	iov.iov_len = strlen(reply);

	msg.msg_name = &(req->origin);
	msg.msg_namelen = req->originlen;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	return sendmsg(req->originating_fd, &msg, 0);
}

static struct mconsole_command *mconsole_parse(struct mc_request *req)
{
	struct mconsole_command *cmd;
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		cmd = &commands[i];
		if (!strncmp(req->request.data, cmd->command,
			    strlen(cmd->command))) {
			return cmd;
		}
	}
	return NULL;
}

#define MIN(a,b) ((a)<(b) ? (a):(b))

#define STRINGX(x) #x
#define STRING(x) STRINGX(x)

int mconsole_get_request(int fd, struct mc_request *req)
{
	int len;

	req->originlen = sizeof(req->origin);
	req->len = recvfrom(fd, &req->request, sizeof(req->request), 0,
			    (struct sockaddr *) req->origin, &req->originlen);
	if (req->len < 0)
		return 0;

	req->originating_fd = fd;

	if (req->request.magic != MCONSOLE_MAGIC) {
		/* Unversioned request */
		len = MIN(sizeof(req->request.data) - 1,
			  strlen((char *) &req->request));
		memmove(req->request.data, &req->request, len);
		req->request.data[len] = '\0';

		req->request.magic = MCONSOLE_MAGIC;
		req->request.version = 0;
		req->request.len = len;

		mconsole_reply_v0(req, "ERR Version 0 mconsole clients are "
				  "not supported by this driver");
		return 0;
	}

	if (req->request.len >= MCONSOLE_MAX_DATA) {
		mconsole_reply(req, "Request too large", 1, 0);
		return 0;
	}
	if (req->request.version != MCONSOLE_VERSION) {
		mconsole_reply(req, "This driver only supports version "
			       STRING(MCONSOLE_VERSION) " clients", 1, 0);
	}

	req->request.data[req->request.len] = '\0';
	req->cmd = mconsole_parse(req);
	if (req->cmd == NULL) {
		mconsole_reply(req, "Unknown command", 1, 0);
		return 0;
	}

	return 1;
}

int mconsole_reply_len(struct mc_request *req, const char *str, int total,
		       int err, int more)
{
	/*
	 * XXX This is a stack consumption problem.  It'd be nice to
	 * make it global and serialize access to it, but there are a
	 * ton of callers to this function.
	 */
	struct mconsole_reply reply;
	int len, n;

	do {
		reply.err = err;

		/* err can only be true on the first packet */
		err = 0;

		len = MIN(total, MCONSOLE_MAX_DATA - 1);

		if (len == total) reply.more = more;
		else reply.more = 1;

		memcpy(reply.data, str, len);
		reply.data[len] = '\0';
		total -= len;
		str += len;
		reply.len = len + 1;

		len = sizeof(reply) + reply.len - sizeof(reply.data);

		n = sendto(req->originating_fd, &reply, len, 0,
			   (struct sockaddr *) req->origin, req->originlen);

		if (n < 0)
			return -errno;
	} while (total > 0);
	return 0;
}

int mconsole_reply(struct mc_request *req, const char *str, int err, int more)
{
	return mconsole_reply_len(req, str, strlen(str), err, more);
}


int mconsole_unlink_socket(void)
{
	unlink(mconsole_socket_name);
	return 0;
}

static int notify_sock = -1;

int mconsole_notify(char *sock_name, int type, const void *data, int len)
{
	struct sockaddr_un target;
	struct mconsole_notify packet;
	int n, err = 0;

	lock_notify();
	if (notify_sock < 0) {
		notify_sock = socket(PF_UNIX, SOCK_DGRAM, 0);
		if (notify_sock < 0) {
			err = -errno;
			printk(UM_KERN_ERR "mconsole_notify - socket failed, "
			       "errno = %d\n", errno);
		}
	}
	unlock_notify();

	if (err)
		return err;

	target.sun_family = AF_UNIX;
	strcpy(target.sun_path, sock_name);

	packet.magic = MCONSOLE_MAGIC;
	packet.version = MCONSOLE_VERSION;
	packet.type = type;
	len = (len > sizeof(packet.data)) ? sizeof(packet.data) : len;
	packet.len = len;
	memcpy(packet.data, data, len);

	err = 0;
	len = sizeof(packet) + packet.len - sizeof(packet.data);
	n = sendto(notify_sock, &packet, len, 0, (struct sockaddr *) &target,
		   sizeof(target));
	if (n < 0) {
		err = -errno;
		printk(UM_KERN_ERR "mconsole_notify - sendto failed, "
		       "errno = %d\n", errno);
	}
	return err;
}
