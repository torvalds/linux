/*
 * Copyright (c) 2011-2012 Intel Corporation.  All rights reserved.
 * Copyright (c) 2014 Mellanox Technologies LTD. All rights reserved.
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

#include <rdma/rdma_cma.h>
#include <rdma/rsocket.h>
#include <util/compiler.h>
#include "common.h"

struct test_size_param {
	int size;
	int option;
};

static struct test_size_param test_size[] = {
	{ 1 <<  6, 0 },
	{ 1 <<  7, 1 }, { (1 <<  7) + (1 <<  6), 1},
	{ 1 <<  8, 1 }, { (1 <<  8) + (1 <<  7), 1},
	{ 1 <<  9, 1 }, { (1 <<  9) + (1 <<  8), 1},
	{ 1 << 10, 1 }, { (1 << 10) + (1 <<  9), 1},
	{ 1 << 11, 1 }, { (1 << 11) + (1 << 10), 1},
	{ 1 << 12, 0 }, { (1 << 12) + (1 << 11), 1},
	{ 1 << 13, 1 }, { (1 << 13) + (1 << 12), 1},
	{ 1 << 14, 1 }, { (1 << 14) + (1 << 13), 1},
	{ 1 << 15, 1 }, { (1 << 15) + (1 << 14), 1},
	{ 1 << 16, 0 }, { (1 << 16) + (1 << 15), 1},
	{ 1 << 17, 1 }, { (1 << 17) + (1 << 16), 1},
	{ 1 << 18, 1 }, { (1 << 18) + (1 << 17), 1},
	{ 1 << 19, 1 }, { (1 << 19) + (1 << 18), 1},
	{ 1 << 20, 0 }, { (1 << 20) + (1 << 19), 1},
	{ 1 << 21, 1 }, { (1 << 21) + (1 << 20), 1},
	{ 1 << 22, 1 }, { (1 << 22) + (1 << 21), 1},
};
#define TEST_CNT (sizeof test_size / sizeof test_size[0])

static int rs, lrs;
static int use_async;
static int use_rgai;
static int verify;
static int flags = MSG_DONTWAIT;
static int poll_timeout = 0;
static int custom;
static enum rs_optimization optimization;
static int size_option;
static int iterations = 1;
static int transfer_size = 1000;
static int transfer_count = 1000;
static int buffer_size, inline_size = 64;
static char test_name[10] = "custom";
static const char *port = "7471";
static char *dst_addr;
static char *src_addr;
static struct timeval start, end;
static void *buf;
static volatile uint8_t *poll_byte;
static struct rdma_addrinfo rai_hints;
static struct addrinfo ai_hints;

static void show_perf(void)
{
	char str[32];
	float usec;
	long long bytes;

	usec = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
	bytes = (long long) iterations * transfer_count * transfer_size * 2;

	/* name size transfers iterations bytes seconds Gb/sec usec/xfer */
	printf("%-10s", test_name);
	size_str(str, sizeof str, transfer_size);
	printf("%-8s", str);
	cnt_str(str, sizeof str, transfer_count);
	printf("%-8s", str);
	cnt_str(str, sizeof str, iterations);
	printf("%-8s", str);
	size_str(str, sizeof str, bytes);
	printf("%-8s", str);
	printf("%8.2fs%10.2f%11.2f\n",
		usec / 1000000., (bytes * 8) / (1000. * usec),
		(usec / iterations) / (transfer_count * 2));
}

static void init_latency_test(int size)
{
	char sstr[5];

	size_str(sstr, sizeof sstr, size);
	snprintf(test_name, sizeof test_name, "%s_lat", sstr);
	transfer_count = 1;
	transfer_size = size;
	iterations = size_to_count(transfer_size);
}

static void init_bandwidth_test(int size)
{
	char sstr[5];

	size_str(sstr, sizeof sstr, size);
	snprintf(test_name, sizeof test_name, "%s_bw", sstr);
	iterations = 1;
	transfer_size = size;
	transfer_count = size_to_count(transfer_size);
}

static int send_msg(int size)
{
	struct pollfd fds;
	int offset, ret;

	if (use_async) {
		fds.fd = rs;
		fds.events = POLLOUT;
	}

	for (offset = 0; offset < size; ) {
		if (use_async) {
			ret = do_poll(&fds, poll_timeout);
			if (ret)
				return ret;
		}

		ret = rsend(rs, buf + offset, size - offset, flags);
		if (ret > 0) {
			offset += ret;
		} else if (errno != EWOULDBLOCK && errno != EAGAIN) {
			perror("rsend");
			return ret;
		}
	}

	return 0;
}

static int send_xfer(int size)
{
	struct pollfd fds;
	int offset, ret;

	if (use_async) {
		fds.fd = rs;
		fds.events = POLLOUT;
	}

	for (offset = 0; offset < size; ) {
		if (use_async) {
			ret = do_poll(&fds, poll_timeout);
			if (ret)
				return ret;
		}

		ret = riowrite(rs, buf + offset, size - offset, offset, flags);
		if (ret > 0) {
			offset += ret;
		} else if (errno != EWOULDBLOCK && errno != EAGAIN) {
			perror("riowrite");
			return ret;
		}
	}

	return 0;
}

static int recv_msg(int size)
{
	struct pollfd fds;
	int offset, ret;

	if (use_async) {
		fds.fd = rs;
		fds.events = POLLIN;
	}

	for (offset = 0; offset < size; ) {
		if (use_async) {
			ret = do_poll(&fds, poll_timeout);
			if (ret)
				return ret;
		}

		ret = rrecv(rs, buf + offset, size - offset, flags);
		if (ret > 0) {
			offset += ret;
		} else if (errno != EWOULDBLOCK && errno != EAGAIN) {
			perror("rrecv");
			return ret;
		}
	}

	return 0;
}

static int recv_xfer(int size, uint8_t marker)
{
	int ret;

	while (*poll_byte != marker)
		;

	if (verify) {
		ret = verify_buf(buf, size - 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int sync_test(void)
{
	int ret;

	ret = dst_addr ? send_msg(16) : recv_msg(16);
	if (ret)
		return ret;

	return dst_addr ? recv_msg(16) : send_msg(16);
}

static int run_test(void)
{
	int ret, i, t;
	off_t offset;
	uint8_t marker = 0;

	poll_byte = buf + transfer_size - 1;
	*poll_byte = -1;
	offset = riomap(rs, buf, transfer_size, PROT_WRITE, 0, 0);
	if (offset ==  -1) {
		perror("riomap");
		ret = -1;
		goto out;
	}
	ret = sync_test();
	if (ret)
		goto out;

	gettimeofday(&start, NULL);
	for (i = 0; i < iterations; i++) {
		if (dst_addr) {
			for (t = 0; t < transfer_count - 1; t++) {
				ret = send_xfer(transfer_size);
				if (ret)
					goto out;
			}
			*poll_byte = (uint8_t) marker++;
			if (verify)
				format_buf(buf, transfer_size - 1);
			ret = send_xfer(transfer_size);
			if (ret)
				goto out;

			ret = recv_xfer(transfer_size, marker++);
		} else {
			ret = recv_xfer(transfer_size, marker++);
			if (ret)
				goto out;

			for (t = 0; t < transfer_count - 1; t++) {
				ret = send_xfer(transfer_size);
				if (ret)
					goto out;
			}
			*poll_byte = (uint8_t) marker++;
			if (verify)
				format_buf(buf, transfer_size - 1);
			ret = send_xfer(transfer_size);
		}
		if (ret)
			goto out;
	}
	gettimeofday(&end, NULL);
	show_perf();
	ret = riounmap(rs, buf, transfer_size);

out:
	return ret;
}

static void set_options(int fd)
{
	int val;

	if (buffer_size) {
		rsetsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *) &buffer_size,
			    sizeof buffer_size);
		rsetsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *) &buffer_size,
			    sizeof buffer_size);
	} else {
		val = 1 << 19;
		rsetsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *) &val, sizeof val);
		rsetsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *) &val, sizeof val);
	}

	val = 1;
	rsetsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *) &val, sizeof(val));
	rsetsockopt(fd, SOL_RDMA, RDMA_IOMAPSIZE, (void *) &val, sizeof val);

	if (flags & MSG_DONTWAIT)
		rfcntl(fd, F_SETFL, O_NONBLOCK);

	/* Inline size based on experimental data */
	if (optimization == opt_latency) {
		rsetsockopt(fd, SOL_RDMA, RDMA_INLINE, &inline_size,
			    sizeof inline_size);
	} else if (optimization == opt_bandwidth) {
		val = 0;
		rsetsockopt(fd, SOL_RDMA, RDMA_INLINE, &val, sizeof val);
	}
}

static int server_listen(void)
{
	struct rdma_addrinfo *rai = NULL;
	struct addrinfo *ai;
	int val, ret;

	if (use_rgai) {
		rai_hints.ai_flags |= RAI_PASSIVE;
		ret = rdma_getaddrinfo(src_addr, port, &rai_hints, &rai);
	} else {
		ai_hints.ai_flags |= AI_PASSIVE;
		ret = getaddrinfo(src_addr, port, &ai_hints, &ai);
	}
	if (ret) {
		printf("getaddrinfo: %s\n", gai_strerror(ret));
		return ret;
	}

	lrs = rai ? rsocket(rai->ai_family, SOCK_STREAM, 0) :
		    rsocket(ai->ai_family, SOCK_STREAM, 0);
	if (lrs < 0) {
		perror("rsocket");
		ret = lrs;
		goto free;
	}

	val = 1;
	ret = rsetsockopt(lrs, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val);
	if (ret) {
		perror("rsetsockopt SO_REUSEADDR");
		goto close;
	}

	ret = rai ? rbind(lrs, rai->ai_src_addr, rai->ai_src_len) :
		    rbind(lrs, ai->ai_addr, ai->ai_addrlen);
	if (ret) {
		perror("rbind");
		goto close;
	}

	ret = rlisten(lrs, 1);
	if (ret)
		perror("rlisten");

close:
	if (ret)
		rclose(lrs);
free:
	if (rai)
		rdma_freeaddrinfo(rai);
	else
		freeaddrinfo(ai);
	return ret;
}

static int server_connect(void)
{
	struct pollfd fds;
	int ret = 0;

	set_options(lrs);
	do {
		if (use_async) {
			fds.fd = lrs;
			fds.events = POLLIN;

			ret = do_poll(&fds, poll_timeout);
			if (ret) {
				perror("rpoll");
				return ret;
			}
		}

		rs = raccept(lrs, NULL, NULL);
	} while (rs < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
	if (rs < 0) {
		perror("raccept");
		return rs;
	}

	set_options(rs);
	return ret;
}

static int client_connect(void)
{
	struct rdma_addrinfo *rai = NULL;
	struct addrinfo *ai;
	struct pollfd fds;
	int ret, err;
	socklen_t len;

	ret = use_rgai ? rdma_getaddrinfo(dst_addr, port, &rai_hints, &rai) :
			 getaddrinfo(dst_addr, port, &ai_hints, &ai);
	if (ret) {
		printf("getaddrinfo: %s\n", gai_strerror(ret));
		return ret;
	}

	rs = rai ? rsocket(rai->ai_family, SOCK_STREAM, 0) :
		   rsocket(ai->ai_family, SOCK_STREAM, 0);
	if (rs < 0) {
		perror("rsocket");
		ret = rs;
		goto free;
	}

	set_options(rs);
	/* TODO: bind client to src_addr */

	ret = rai ? rconnect(rs, rai->ai_dst_addr, rai->ai_dst_len) :
		    rconnect(rs, ai->ai_addr, ai->ai_addrlen);
	if (ret && (errno != EINPROGRESS)) {
		perror("rconnect");
		goto close;
	}

	if (ret && (errno == EINPROGRESS)) {
		fds.fd = rs;
		fds.events = POLLOUT;
		ret = do_poll(&fds, poll_timeout);
		if (ret) {
			perror("rpoll");
			goto close;
		}

		len = sizeof err;
		ret = rgetsockopt(rs, SOL_SOCKET, SO_ERROR, &err, &len);
		if (ret)
			goto close;
		if (err) {
			ret = -1;
			errno = err;
			perror("async rconnect");
		}
	}

close:
	if (ret)
		rclose(rs);
free:
	if (rai)
		rdma_freeaddrinfo(rai);
	else
		freeaddrinfo(ai);
	return ret;
}

static int run(void)
{
	int i, ret = 0;

	buf = malloc(!custom ? test_size[TEST_CNT - 1].size : transfer_size);
	if (!buf) {
		perror("malloc");
		return -1;
	}

	if (!dst_addr) {
		ret = server_listen();
		if (ret)
			goto free;
	}

	printf("%-10s%-8s%-8s%-8s%-8s%8s %10s%13s\n",
	       "name", "bytes", "xfers", "iters", "total", "time", "Gb/sec", "usec/xfer");
	if (!custom) {
		optimization = opt_latency;
		ret = dst_addr ? client_connect() : server_connect();
		if (ret)
			goto free;

		for (i = 0; i < TEST_CNT; i++) {
			if (test_size[i].option > size_option)
				continue;
			init_latency_test(test_size[i].size);
			run_test();
		}
		rshutdown(rs, SHUT_RDWR);
		rclose(rs);

		optimization = opt_bandwidth;
		ret = dst_addr ? client_connect() : server_connect();
		if (ret)
			goto free;
		for (i = 0; i < TEST_CNT; i++) {
			if (test_size[i].option > size_option)
				continue;
			init_bandwidth_test(test_size[i].size);
			run_test();
		}
	} else {
		ret = dst_addr ? client_connect() : server_connect();
		if (ret)
			goto free;

		ret = run_test();
	}

	rshutdown(rs, SHUT_RDWR);
	rclose(rs);
free:
	free(buf);
	return ret;
}

static int set_test_opt(const char *arg)
{
	if (strlen(arg) == 1) {
		switch (arg[0]) {
		case 'a':
			use_async = 1;
			break;
		case 'b':
			flags = (flags & ~MSG_DONTWAIT) | MSG_WAITALL;
			break;
		case 'n':
			flags |= MSG_DONTWAIT;
			break;
		case 'v':
			verify = 1;
			break;
		default:
			return -1;
		}
	} else {
		if (!strncasecmp("async", arg, 5)) {
			use_async = 1;
		} else if (!strncasecmp("block", arg, 5)) {
			flags = (flags & ~MSG_DONTWAIT) | MSG_WAITALL;
		} else if (!strncasecmp("nonblock", arg, 8)) {
			flags |= MSG_DONTWAIT;
		} else if (!strncasecmp("verify", arg, 6)) {
			verify = 1;
		} else {
			return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	int op, ret;

	ai_hints.ai_socktype = SOCK_STREAM;
	rai_hints.ai_port_space = RDMA_PS_TCP;
	while ((op = getopt(argc, argv, "s:b:f:B:i:I:C:S:p:T:")) != -1) {
		switch (op) {
		case 's':
			dst_addr = optarg;
			break;
		case 'b':
			src_addr = optarg;
			break;
		case 'f':
			if (!strncasecmp("ip", optarg, 2)) {
				ai_hints.ai_flags = AI_NUMERICHOST;
			} else if (!strncasecmp("gid", optarg, 3)) {
				rai_hints.ai_flags = RAI_NUMERICHOST | RAI_FAMILY;
				rai_hints.ai_family = AF_IB;
				use_rgai = 1;
			} else {
				fprintf(stderr, "Warning: unknown address format\n");
			}
			break;
		case 'B':
			buffer_size = atoi(optarg);
			break;
		case 'i':
			inline_size = atoi(optarg);
			break;
		case 'I':
			custom = 1;
			iterations = atoi(optarg);
			break;
		case 'C':
			custom = 1;
			transfer_count = atoi(optarg);
			break;
		case 'S':
			if (!strncasecmp("all", optarg, 3)) {
				size_option = 1;
			} else {
				custom = 1;
				transfer_size = atoi(optarg);
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
			printf("\t[-f address_format]\n");
			printf("\t    name, ip, ipv6, or gid\n");
			printf("\t[-B buffer_size]\n");
			printf("\t[-i inline_size]\n");
			printf("\t[-I iterations]\n");
			printf("\t[-C transfer_count]\n");
			printf("\t[-S transfer_size or all]\n");
			printf("\t[-p port_number]\n");
			printf("\t[-T test_option]\n");
			printf("\t    a|async - asynchronous operation (use poll)\n");
			printf("\t    b|blocking - use blocking calls\n");
			printf("\t    n|nonblocking - use nonblocking calls\n");
			printf("\t    v|verify - verify data\n");
			exit(1);
		}
	}

	if (!(flags & MSG_DONTWAIT))
		poll_timeout = -1;

	ret = run();
	return ret;
}
