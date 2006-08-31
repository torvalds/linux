/*
 * 	ucon.c
 *
 * Copyright (c) 2004+ Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <asm/types.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <linux/connector.h>

#define DEBUG
#define NETLINK_CONNECTOR 	11

#ifdef DEBUG
#define ulog(f, a...) fprintf(stdout, f, ##a)
#else
#define ulog(f, a...) do {} while (0)
#endif

static int need_exit;
static __u32 seq;

static int netlink_send(int s, struct cn_msg *msg)
{
	struct nlmsghdr *nlh;
	unsigned int size;
	int err;
	char buf[128];
	struct cn_msg *m;

	size = NLMSG_SPACE(sizeof(struct cn_msg) + msg->len);

	nlh = (struct nlmsghdr *)buf;
	nlh->nlmsg_seq = seq++;
	nlh->nlmsg_pid = getpid();
	nlh->nlmsg_type = NLMSG_DONE;
	nlh->nlmsg_len = NLMSG_LENGTH(size - sizeof(*nlh));
	nlh->nlmsg_flags = 0;

	m = NLMSG_DATA(nlh);
#if 0
	ulog("%s: [%08x.%08x] len=%u, seq=%u, ack=%u.\n",
	       __func__, msg->id.idx, msg->id.val, msg->len, msg->seq, msg->ack);
#endif
	memcpy(m, msg, sizeof(*m) + msg->len);

	err = send(s, nlh, size, 0);
	if (err == -1)
		ulog("Failed to send: %s [%d].\n",
			strerror(errno), errno);

	return err;
}

int main(int argc, char *argv[])
{
	int s;
	char buf[1024];
	int len;
	struct nlmsghdr *reply;
	struct sockaddr_nl l_local;
	struct cn_msg *data;
	FILE *out;
	time_t tm;
	struct pollfd pfd;

	if (argc < 2)
		out = stdout;
	else {
		out = fopen(argv[1], "a+");
		if (!out) {
			ulog("Unable to open %s for writing: %s\n",
				argv[1], strerror(errno));
			out = stdout;
		}
	}

	memset(buf, 0, sizeof(buf));

	s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	if (s == -1) {
		perror("socket");
		return -1;
	}

	l_local.nl_family = AF_NETLINK;
	l_local.nl_groups = 0x123; /* bitmask of requested groups */
	l_local.nl_pid = 0;

	if (bind(s, (struct sockaddr *)&l_local, sizeof(struct sockaddr_nl)) == -1) {
		perror("bind");
		close(s);
		return -1;
	}

#if 0
	{
		int on = 0x57; /* Additional group number */
		setsockopt(s, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &on, sizeof(on));
	}
#endif
	if (0) {
		int i, j;

		memset(buf, 0, sizeof(buf));

		data = (struct cn_msg *)buf;

		data->id.idx = 0x123;
		data->id.val = 0x456;
		data->seq = seq++;
		data->ack = 0;
		data->len = 0;

		for (j=0; j<10; ++j) {
			for (i=0; i<1000; ++i) {
				len = netlink_send(s, data);
			}

			ulog("%d messages have been sent to %08x.%08x.\n", i, data->id.idx, data->id.val);
		}

		return 0;
	}


	pfd.fd = s;

	while (!need_exit) {
		pfd.events = POLLIN;
		pfd.revents = 0;
		switch (poll(&pfd, 1, -1)) {
			case 0:
				need_exit = 1;
				break;
			case -1:
				if (errno != EINTR) {
					need_exit = 1;
					break;
				}
				continue;
		}
		if (need_exit)
			break;

		memset(buf, 0, sizeof(buf));
		len = recv(s, buf, sizeof(buf), 0);
		if (len == -1) {
			perror("recv buf");
			close(s);
			return -1;
		}
		reply = (struct nlmsghdr *)buf;

		switch (reply->nlmsg_type) {
		case NLMSG_ERROR:
			fprintf(out, "Error message received.\n");
			fflush(out);
			break;
		case NLMSG_DONE:
			data = (struct cn_msg *)NLMSG_DATA(reply);

			time(&tm);
			fprintf(out, "%.24s : [%x.%x] [%08u.%08u].\n",
				ctime(&tm), data->id.idx, data->id.val, data->seq, data->ack);
			fflush(out);
			break;
		default:
			break;
		}
	}

	close(s);
	return 0;
}
