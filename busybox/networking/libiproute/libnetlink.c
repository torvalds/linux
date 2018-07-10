/* vi: set sw=4 ts=4: */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */
#include <sys/socket.h>
#include <sys/uio.h>

#include "libbb.h"
#include "libnetlink.h"

void FAST_FUNC xrtnl_open(struct rtnl_handle *rth/*, unsigned subscriptions*/)
{
	memset(rth, 0, sizeof(*rth));
	rth->fd = xsocket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	rth->local.nl_family = AF_NETLINK;
	/*rth->local.nl_groups = subscriptions;*/

	xbind(rth->fd, (struct sockaddr*)&rth->local, sizeof(rth->local));
	bb_getsockname(rth->fd, (struct sockaddr*)&rth->local, sizeof(rth->local));

/* too much paranoia
	if (getsockname(rth->fd, (struct sockaddr*)&rth->local, &addr_len) < 0)
		bb_perror_msg_and_die("getsockname");
	if (addr_len != sizeof(rth->local))
		bb_error_msg_and_die("wrong address length %d", addr_len);
	if (rth->local.nl_family != AF_NETLINK)
		bb_error_msg_and_die("wrong address family %d", rth->local.nl_family);
*/
	rth->seq = time(NULL);
}

int FAST_FUNC xrtnl_wilddump_request(struct rtnl_handle *rth, int family, int type)
{
	struct {
		struct nlmsghdr nlh;
		struct rtgenmsg g;
	} req;

	req.nlh.nlmsg_len = sizeof(req);
	req.nlh.nlmsg_type = type;
	req.nlh.nlmsg_flags = NLM_F_ROOT|NLM_F_MATCH|NLM_F_REQUEST;
	req.nlh.nlmsg_pid = 0;
	req.nlh.nlmsg_seq = rth->dump = ++rth->seq;
	req.g.rtgen_family = family;

	return rtnl_send(rth, (void*)&req, sizeof(req));
}

//TODO: pass rth->fd instead of full rth?
int FAST_FUNC rtnl_send(struct rtnl_handle *rth, char *buf, int len)
{
	struct sockaddr_nl nladdr;

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;

	return xsendto(rth->fd, buf, len, (struct sockaddr*)&nladdr, sizeof(nladdr));
}

int FAST_FUNC rtnl_dump_request(struct rtnl_handle *rth, int type, void *req, int len)
{
	struct {
		struct nlmsghdr nlh;
		struct msghdr msg;
		struct sockaddr_nl nladdr;
	} s;
	struct iovec iov[2] = { { &s.nlh, sizeof(s.nlh) }, { req, len } };

	memset(&s, 0, sizeof(s));

	s.msg.msg_name = (void*)&s.nladdr;
	s.msg.msg_namelen = sizeof(s.nladdr);
	s.msg.msg_iov = iov;
	s.msg.msg_iovlen = 2;
	/*s.msg.msg_control = NULL; - already is */
	/*s.msg.msg_controllen = 0; - already is */
	/*s.msg.msg_flags = 0; - already is */

	s.nladdr.nl_family = AF_NETLINK;

	s.nlh.nlmsg_len = NLMSG_LENGTH(len);
	s.nlh.nlmsg_type = type;
	s.nlh.nlmsg_flags = NLM_F_ROOT|NLM_F_MATCH|NLM_F_REQUEST;
	/*s.nlh.nlmsg_pid = 0; - already is */
	s.nlh.nlmsg_seq = rth->dump = ++rth->seq;

	return sendmsg(rth->fd, &s.msg, 0);
}

static int rtnl_dump_filter(struct rtnl_handle *rth,
		int (*filter)(const struct sockaddr_nl *, struct nlmsghdr *n, void *) FAST_FUNC,
		void *arg1/*,
		int (*junk)(struct sockaddr_nl *, struct nlmsghdr *n, void *),
		void *arg2*/)
{
	int retval = -1;
	char *buf = xmalloc(8*1024); /* avoid big stack buffer */
	struct sockaddr_nl nladdr;
	struct iovec iov = { buf, 8*1024 };

	while (1) {
		int status;
		struct nlmsghdr *h;
		/* Use designated initializers, struct layout is non-portable */
		struct msghdr msg = {
			.msg_name = (void*)&nladdr,
			.msg_namelen = sizeof(nladdr),
			.msg_iov = &iov,
			.msg_iovlen = 1,
			.msg_control = NULL,
			.msg_controllen = 0,
			.msg_flags = 0
		};

		status = recvmsg(rth->fd, &msg, 0);

		if (status < 0) {
			if (errno == EINTR)
				continue;
			bb_perror_msg("OVERRUN");
			continue;
		}
		if (status == 0) {
			bb_error_msg("EOF on netlink");
			goto ret;
		}
		if (msg.msg_namelen != sizeof(nladdr)) {
			bb_error_msg_and_die("sender address length == %d", msg.msg_namelen);
		}

		h = (struct nlmsghdr*)buf;
		while (NLMSG_OK(h, status)) {
			int err;

			if (nladdr.nl_pid != 0 ||
			    h->nlmsg_pid != rth->local.nl_pid ||
			    h->nlmsg_seq != rth->dump
			) {
//				if (junk) {
//					err = junk(&nladdr, h, arg2);
//					if (err < 0) {
//						retval = err;
//						goto ret;
//					}
//				}
				goto skip_it;
			}

			if (h->nlmsg_type == NLMSG_DONE) {
				goto ret_0;
			}
			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *l_err = (struct nlmsgerr*)NLMSG_DATA(h);
				if (h->nlmsg_len < NLMSG_LENGTH(sizeof(struct nlmsgerr))) {
					bb_error_msg("ERROR truncated");
				} else {
					errno = -l_err->error;
					bb_perror_msg("RTNETLINK answers");
				}
				goto ret;
			}
			err = filter(&nladdr, h, arg1);
			if (err < 0) {
				retval = err;
				goto ret;
			}

 skip_it:
			h = NLMSG_NEXT(h, status);
		}
		if (msg.msg_flags & MSG_TRUNC) {
			bb_error_msg("message truncated");
			continue;
		}
		if (status) {
			bb_error_msg_and_die("remnant of size %d!", status);
		}
	} /* while (1) */
 ret_0:
	retval++; /* = 0 */
 ret:
	free(buf);
	return retval;
}

int FAST_FUNC xrtnl_dump_filter(struct rtnl_handle *rth,
		int (*filter)(const struct sockaddr_nl *, struct nlmsghdr *, void *) FAST_FUNC,
		void *arg1)
{
	int ret = rtnl_dump_filter(rth, filter, arg1/*, NULL, NULL*/);
	if (ret < 0)
		bb_error_msg_and_die("dump terminated");
	return ret;
}

int FAST_FUNC rtnl_talk(struct rtnl_handle *rtnl, struct nlmsghdr *n,
		pid_t peer, unsigned groups,
		struct nlmsghdr *answer,
		int (*junk)(struct sockaddr_nl *, struct nlmsghdr *, void *),
		void *jarg)
{
/* bbox doesn't use parameters no. 3, 4, 6, 7, they are stubbed out */
#define peer   0
#define groups 0
#define junk   NULL
#define jarg   NULL
	int retval = -1;
	int status;
	unsigned seq;
	struct nlmsghdr *h;
	struct sockaddr_nl nladdr;
	struct iovec iov = { (void*)n, n->nlmsg_len };
	char   *buf = xmalloc(8*1024); /* avoid big stack buffer */
	/* Use designated initializers, struct layout is non-portable */
	struct msghdr msg = {
		.msg_name = (void*)&nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0
	};

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
//	nladdr.nl_pid = peer;
//	nladdr.nl_groups = groups;

	n->nlmsg_seq = seq = ++rtnl->seq;
	if (answer == NULL) {
		n->nlmsg_flags |= NLM_F_ACK;
	}
	status = sendmsg(rtnl->fd, &msg, 0);

	if (status < 0) {
		bb_perror_msg("can't talk to rtnetlink");
		goto ret;
	}

	iov.iov_base = buf;

	while (1) {
		iov.iov_len = 8*1024;
		status = recvmsg(rtnl->fd, &msg, 0);

		if (status < 0) {
			if (errno == EINTR) {
				continue;
			}
			bb_perror_msg("OVERRUN");
			continue;
		}
		if (status == 0) {
			bb_error_msg("EOF on netlink");
			goto ret;
		}
		if (msg.msg_namelen != sizeof(nladdr)) {
			bb_error_msg_and_die("sender address length == %d", msg.msg_namelen);
		}
		for (h = (struct nlmsghdr*)buf; status >= (int)sizeof(*h); ) {
//			int l_err;
			int len = h->nlmsg_len;
			int l = len - sizeof(*h);

			if (l < 0 || len > status) {
				if (msg.msg_flags & MSG_TRUNC) {
					bb_error_msg("truncated message");
					goto ret;
				}
				bb_error_msg_and_die("malformed message: len=%d!", len);
			}

			if (nladdr.nl_pid != peer ||
			    h->nlmsg_pid != rtnl->local.nl_pid ||
			    h->nlmsg_seq != seq
			) {
//				if (junk) {
//					l_err = junk(&nladdr, h, jarg);
//					if (l_err < 0) {
//						retval = l_err;
//						goto ret;
//					}
//				}
				continue;
			}

			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *err = (struct nlmsgerr*)NLMSG_DATA(h);
				if (l < (int)sizeof(struct nlmsgerr)) {
					bb_error_msg("ERROR truncated");
				} else {
					errno = - err->error;
					if (errno == 0) {
						if (answer) {
							memcpy(answer, h, h->nlmsg_len);
						}
						goto ret_0;
					}
					bb_perror_msg("RTNETLINK answers");
				}
				goto ret;
			}
			if (answer) {
				memcpy(answer, h, h->nlmsg_len);
				goto ret_0;
			}

			bb_error_msg("unexpected reply!");

			status -= NLMSG_ALIGN(len);
			h = (struct nlmsghdr*)((char*)h + NLMSG_ALIGN(len));
		}
		if (msg.msg_flags & MSG_TRUNC) {
			bb_error_msg("message truncated");
			continue;
		}
		if (status) {
			bb_error_msg_and_die("remnant of size %d!", status);
		}
	} /* while (1) */
 ret_0:
	retval++; /* = 0 */
 ret:
	free(buf);
	return retval;
}

int FAST_FUNC addattr32(struct nlmsghdr *n, int maxlen, int type, uint32_t data)
{
	int len = RTA_LENGTH(4);
	struct rtattr *rta;

	if ((int)(NLMSG_ALIGN(n->nlmsg_len + len)) > maxlen) {
		return -1;
	}
	rta = (struct rtattr*)(((char*)n) + NLMSG_ALIGN(n->nlmsg_len));
	rta->rta_type = type;
	rta->rta_len = len;
	move_to_unaligned32(RTA_DATA(rta), data);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len + len);
	return 0;
}

int FAST_FUNC addattr_l(struct nlmsghdr *n, int maxlen, int type, void *data, int alen)
{
	int len = RTA_LENGTH(alen);
	struct rtattr *rta;

	if ((int)(NLMSG_ALIGN(n->nlmsg_len + len)) > maxlen) {
		return -1;
	}
	rta = (struct rtattr*)(((char*)n) + NLMSG_ALIGN(n->nlmsg_len));
	rta->rta_type = type;
	rta->rta_len = len;
	memcpy(RTA_DATA(rta), data, alen);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len + len);
	return 0;
}

int FAST_FUNC rta_addattr32(struct rtattr *rta, int maxlen, int type, uint32_t data)
{
	int len = RTA_LENGTH(4);
	struct rtattr *subrta;

	if (RTA_ALIGN(rta->rta_len + len) > maxlen) {
		return -1;
	}
	subrta = (struct rtattr*)(((char*)rta) + RTA_ALIGN(rta->rta_len));
	subrta->rta_type = type;
	subrta->rta_len = len;
	move_to_unaligned32(RTA_DATA(subrta), data);
	rta->rta_len = NLMSG_ALIGN(rta->rta_len + len);
	return 0;
}

int FAST_FUNC rta_addattr_l(struct rtattr *rta, int maxlen, int type, void *data, int alen)
{
	struct rtattr *subrta;
	int len = RTA_LENGTH(alen);

	if (RTA_ALIGN(rta->rta_len + len) > maxlen) {
		return -1;
	}
	subrta = (struct rtattr*)(((char*)rta) + RTA_ALIGN(rta->rta_len));
	subrta->rta_type = type;
	subrta->rta_len = len;
	memcpy(RTA_DATA(subrta), data, alen);
	rta->rta_len = NLMSG_ALIGN(rta->rta_len + len);
	return 0;
}


void FAST_FUNC parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, int len)
{
	memset(tb, 0, (max + 1) * sizeof(tb[0]));

	while (RTA_OK(rta, len)) {
		if (rta->rta_type <= max) {
			tb[rta->rta_type] = rta;
		}
		rta = RTA_NEXT(rta, len);
	}
	if (len) {
		bb_error_msg("deficit %d, rta_len=%d!", len, rta->rta_len);
	}
}
