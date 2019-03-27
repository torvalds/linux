/*
 * Copyright (c) 2012 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <rdma/rdma_cma.h>
#include <rdma/rsocket.h>
#include <util/compiler.h>
#include "common.h"

static int test_size[] = {
	(1 <<  6),
	(1 <<  7), ((1 <<  7) + (1 << 6)),
	(1 <<  8), ((1 <<  8) + (1 << 7)),
	(1 <<  9), ((1 <<  9) + (1 << 8)),
	(1 << 10), ((1 << 10) + (1 << 9)),
};
#define TEST_CNT (sizeof test_size / sizeof test_size[0])

enum {
	msg_op_login,
	msg_op_start,
	msg_op_data,
	msg_op_echo,
	msg_op_end
};

struct message {
	uint8_t op;
	uint8_t id;
	uint8_t seqno;
	uint8_t reserved;
	__be32 data;
	uint8_t  buf[2048];
};

#define CTRL_MSG_SIZE 16

struct client {
	uint64_t recvcnt;
};

static struct client clients[256];
static uint8_t id;

static int rs;
static int use_async;
static int flags = MSG_DONTWAIT;
static int poll_timeout;
static int custom;
static int echo;
static int transfer_size = 1000;
static int transfer_count = 1000;
static int buffer_size;
static char test_name[10] = "custom";
static const char *port = "7174";
static char *dst_addr;
static char *src_addr;
static union socket_addr g_addr;
static socklen_t g_addrlen;
static struct timeval start, end;
static struct message g_msg;

static void show_perf(void)
{
	char str[32];
	float usec;
	long long bytes;
	int transfers;

	usec = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
	transfers = echo ? transfer_count * 2 : be32toh(g_msg.data);
	bytes = (long long) transfers * transfer_size;

	/* name size transfers bytes seconds Gb/sec usec/xfer */
	printf("%-10s", test_name);
	size_str(str, sizeof str, transfer_size);
	printf("%-8s", str);
	cnt_str(str, sizeof str, transfers);
	printf("%-8s", str);
	size_str(str, sizeof str, bytes);
	printf("%-8s", str);
	printf("%8.2fs%10.2f%11.2f\n",
		usec / 1000000., (bytes * 8) / (1000. * usec),
		(usec / transfers));
}

static void init_latency_test(int size)
{
	char sstr[5];

	size_str(sstr, sizeof sstr, size);
	snprintf(test_name, sizeof test_name, "%s_lat", sstr);
	transfer_size = size;
	transfer_count = size_to_count(transfer_size) / 10;
	echo = 1;
}

static void init_bandwidth_test(int size)
{
	char sstr[5];

	size_str(sstr, sizeof sstr, size);
	snprintf(test_name, sizeof test_name, "%s_bw", sstr);
	transfer_size = size;
	transfer_count = size_to_count(transfer_size);
	echo = 0;
}

static void set_options(int fd)
{
	int val;

	if (buffer_size) {
		rs_setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *) &buffer_size,
			      sizeof buffer_size);
		rs_setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *) &buffer_size,
			      sizeof buffer_size);
	} else {
		val = 1 << 19;
		rs_setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *) &val, sizeof val);
		rs_setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *) &val, sizeof val);
	}

	if (flags & MSG_DONTWAIT)
		rs_fcntl(fd, F_SETFL, O_NONBLOCK);
}

static ssize_t svr_send(struct message *msg, size_t size,
			union socket_addr *addr, socklen_t addrlen)
{
	struct pollfd fds;
	ssize_t ret;

	if (use_async) {
		fds.fd = rs;
		fds.events = POLLOUT;
	}

	do {
		if (use_async) {
			ret = do_poll(&fds, poll_timeout);
			if (ret)
				return ret;
		}

		ret = rs_sendto(rs, msg, size, flags, &addr->sa, addrlen);
	} while (ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN));

	if (ret < 0)
		perror("rsend");

	return ret;
}

static ssize_t svr_recv(struct message *msg, size_t size,
			union socket_addr *addr, socklen_t *addrlen)
{
	struct pollfd fds;
	ssize_t ret;

	if (use_async) {
		fds.fd = rs;
		fds.events = POLLIN;
	}

	do {
		if (use_async) {
			ret = do_poll(&fds, poll_timeout);
			if (ret)
				return ret;
		}

		ret = rs_recvfrom(rs, msg, size, flags, &addr->sa, addrlen);
	} while (ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN));

	if (ret < 0)
		perror("rrecv");

	return ret;
}

static int svr_process(struct message *msg, size_t size,
		       union socket_addr *addr, socklen_t addrlen)
{
	char str[64];
	ssize_t ret;

	switch (msg->op) {
	case msg_op_login:
		if (addr->sa.sa_family == AF_INET) {
			printf("client login from %s\n",
			       inet_ntop(AF_INET, &addr->sin.sin_addr.s_addr,
					 str, sizeof str));
		} else {
			printf("client login from %s\n",
			       inet_ntop(AF_INET6, &addr->sin6.sin6_addr.s6_addr,
					 str, sizeof str));
		}
		msg->id = id++;
		/* fall through */
	case msg_op_start:
		memset(&clients[msg->id], 0, sizeof clients[msg->id]);
		break;
	case msg_op_echo:
		clients[msg->id].recvcnt++;
		break;
	case msg_op_end:
		msg->data = htobe32(clients[msg->id].recvcnt);
		break;
	default:
		clients[msg->id].recvcnt++;
		return 0;
	}

	ret = svr_send(msg, size, addr, addrlen);
	return (ret == size) ? 0 : (int) ret;
}

static int svr_bind(void)
{
	struct addrinfo hints, *res;
	int ret;

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_DGRAM;
 	ret = getaddrinfo(src_addr, port, &hints, &res);
	if (ret) {
		printf("getaddrinfo: %s\n", gai_strerror(ret));
		return ret;
	}

	rs = rs_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (rs < 0) {
		perror("rsocket");
		ret = rs;
		goto out;
	}

	set_options(rs);
	ret = rs_bind(rs, res->ai_addr, res->ai_addrlen);
	if (ret) {
		perror("rbind");
		rs_close(rs);
	}

out:
	free(res);
	return ret;
}

static int svr_run(void)
{
	ssize_t len;
	int ret;

	ret = svr_bind();
	while (!ret) {
		g_addrlen = sizeof g_addr;
		len = svr_recv(&g_msg, sizeof g_msg, &g_addr, &g_addrlen);
		if (len < 0)
			return len;

		ret = svr_process(&g_msg, len, &g_addr, g_addrlen);
	}
	return ret;
}

static ssize_t client_send(struct message *msg, size_t size)
{
	struct pollfd fds;
	int ret;

	if (use_async) {
		fds.fd = rs;
		fds.events = POLLOUT;
	}

	do {
		if (use_async) {
			ret = do_poll(&fds, poll_timeout);
			if (ret)
				return ret;
		}

		ret = rs_send(rs, msg, size, flags);
	} while (ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN));

	if (ret < 0)
		perror("rsend");

	return ret;
}

static ssize_t client_recv(struct message *msg, size_t size, int timeout)
{
	struct pollfd fds;
	int ret;

	if (timeout) {
		fds.fd = rs;
		fds.events = POLLIN;

		ret = rs_poll(&fds, 1, timeout);
		if (ret <= 0)
			return ret;
	}

	ret = rs_recv(rs, msg, size, flags | MSG_DONTWAIT);
	if (ret < 0 && errno != EWOULDBLOCK && errno != EAGAIN)
		perror("rrecv");

	return ret;
}

static int client_send_recv(struct message *msg, size_t size, int timeout)
{
	static uint8_t seqno;
	int ret;

	msg->seqno = seqno;
	do {
		ret = client_send(msg, size);
		if (ret != size)
			return ret;

		ret = client_recv(msg, size, timeout);
	} while (ret <= 0 || msg->seqno != seqno);

	seqno++;
	return ret;
}

static int run_test(void)
{
	int ret, i;

	g_msg.op = msg_op_start;
	ret = client_send_recv(&g_msg, CTRL_MSG_SIZE, 1000);
	if (ret != CTRL_MSG_SIZE)
		goto out;

	g_msg.op = echo ? msg_op_echo : msg_op_data;
	gettimeofday(&start, NULL);
	for (i = 0; i < transfer_count; i++) {
		ret = echo ? client_send_recv(&g_msg, transfer_size, 1) :
			     client_send(&g_msg, transfer_size);
		if (ret != transfer_size)
			goto out;
	}

	g_msg.op = msg_op_end;
	ret = client_send_recv(&g_msg, CTRL_MSG_SIZE, 1);
	if (ret != CTRL_MSG_SIZE)
		goto out;

	gettimeofday(&end, NULL);
	show_perf();
	ret = 0;

out:
	return ret;
}

static int client_connect(void)
{
	struct addrinfo hints, *res;
	int ret;

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_DGRAM;
 	ret = getaddrinfo(dst_addr, port, &hints, &res);
	if (ret) {
		printf("getaddrinfo: %s\n", gai_strerror(ret));
		return ret;
	}

	rs = rs_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (rs < 0) {
		perror("rsocket");
		ret = rs;
		goto out;
	}

	set_options(rs);
	ret = rs_connect(rs, res->ai_addr, res->ai_addrlen);
	if (ret) {
		perror("rconnect");
		rs_close(rs);
		goto out;
	}

	g_msg.op = msg_op_login;
	ret = client_send_recv(&g_msg, CTRL_MSG_SIZE, 1000);
	if (ret == CTRL_MSG_SIZE)
		ret = 0;

out:
	freeaddrinfo(res);
	return ret;
}

static int client_run(void)
{
	int i, ret;

	printf("%-10s%-8s%-8s%-8s%8s %10s%13s\n",
	       "name", "bytes", "xfers", "total", "time", "Gb/sec", "usec/xfer");

	ret = client_connect();
	if (ret)
		return ret;

	if (!custom) {
		for (i = 0; i < TEST_CNT; i++) {
			init_latency_test(test_size[i]);
			run_test();
		}
		for (i = 0; i < TEST_CNT; i++) {
			init_bandwidth_test(test_size[i]);
			run_test();
		}
	} else {
		run_test();
	}
	rs_close(rs);

	return ret;
}

static int set_test_opt(const char *arg)
{
	if (strlen(arg) == 1) {
		switch (arg[0]) {
		case 's':
			use_rs = 0;
			break;
		case 'a':
			use_async = 1;
			break;
		case 'b':
			flags = 0;
			break;
		case 'n':
			flags = MSG_DONTWAIT;
			break;
		case 'e':
			echo = 1;
			break;
		default:
			return -1;
		}
	} else {
		if (!strncasecmp("socket", arg, 6)) {
			use_rs = 0;
		} else if (!strncasecmp("async", arg, 5)) {
			use_async = 1;
		} else if (!strncasecmp("block", arg, 5)) {
			flags = 0;
		} else if (!strncasecmp("nonblock", arg, 8)) {
			flags = MSG_DONTWAIT;
		} else if (!strncasecmp("echo", arg, 4)) {
			echo = 1;
		} else {
			return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	int op, ret;

	while ((op = getopt(argc, argv, "s:b:B:C:S:p:T:")) != -1) {
		switch (op) {
		case 's':
			dst_addr = optarg;
			break;
		case 'b':
			src_addr = optarg;
			break;
		case 'B':
			buffer_size = atoi(optarg);
			break;
		case 'C':
			custom = 1;
			transfer_count = atoi(optarg);
			break;
		case 'S':
			custom = 1;
			transfer_size = atoi(optarg);
			if (transfer_size < CTRL_MSG_SIZE) {
				printf("size must be at least %d bytes\n",
				       CTRL_MSG_SIZE);
				exit(1);
			}
			break;
		case 'p':
			port = optarg;
			break;
		case 'T':
			if (!set_test_opt(optarg))
				break;
			/* invalid option - fall through */
			SWITCH_FALLTHROUGH;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-s server_address]\n");
			printf("\t[-b bind_address]\n");
			printf("\t[-B buffer_size]\n");
			printf("\t[-C transfer_count]\n");
			printf("\t[-S transfer_size]\n");
			printf("\t[-p port_number]\n");
			printf("\t[-T test_option]\n");
			printf("\t    s|sockets - use standard tcp/ip sockets\n");
			printf("\t    a|async - asynchronous operation (use poll)\n");
			printf("\t    b|blocking - use blocking calls\n");
			printf("\t    n|nonblocking - use nonblocking calls\n");
			printf("\t    e|echo - server echoes all messages\n");
			exit(1);
		}
	}

	if (flags)
		poll_timeout = -1;

	ret = dst_addr ? client_run() : svr_run();
	return ret;
}
