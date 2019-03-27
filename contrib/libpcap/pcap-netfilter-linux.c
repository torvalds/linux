/*
 * Copyright (c) 2011 Jakub Zawadzki
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pcap-int.h"

#ifdef NEED_STRERROR_H
#include "strerror.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <time.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <linux/types.h>

#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_log.h>
#include <linux/netfilter/nfnetlink_queue.h>

/* NOTE: if your program drops privilages after pcap_activate() it WON'T work with nfqueue.
 *       It took me quite some time to debug ;/
 *
 *       Sending any data to nfnetlink socket requires CAP_NET_ADMIN privilages,
 *       and in nfqueue we need to send verdict reply after recving packet.
 *
 *       In tcpdump you can disable dropping privilages with -Z root
 */

#include "pcap-netfilter-linux.h"

#define HDR_LENGTH (NLMSG_LENGTH(NLMSG_ALIGN(sizeof(struct nfgenmsg))))

#define NFLOG_IFACE "nflog"
#define NFQUEUE_IFACE "nfqueue"

typedef enum { OTHER = -1, NFLOG, NFQUEUE } nftype_t;

/*
 * Private data for capturing on Linux netfilter sockets.
 */
struct pcap_netfilter {
	u_int	packets_read;	/* count of packets read with recvfrom() */
	u_int   packets_nobufs; /* ENOBUFS counter */
};

static int nfqueue_send_verdict(const pcap_t *handle, uint16_t group_id, u_int32_t id, u_int32_t verdict);


static int
netfilter_read_linux(pcap_t *handle, int max_packets, pcap_handler callback, u_char *user)
{
	struct pcap_netfilter *handlep = handle->priv;
	register u_char *bp, *ep;
	int count = 0;
	int len;

	/*
	 * Has "pcap_breakloop()" been called?
	 */
	if (handle->break_loop) {
		/*
		 * Yes - clear the flag that indicates that it
		 * has, and return PCAP_ERROR_BREAK to indicate
		 * that we were told to break out of the loop.
		 */
		handle->break_loop = 0;
		return PCAP_ERROR_BREAK;
	}
	len = handle->cc;
	if (len == 0) {
		/*
		 * The buffer is empty; refill it.
		 *
		 * We ignore EINTR, as that might just be due to a signal
		 * being delivered - if the signal should interrupt the
		 * loop, the signal handler should call pcap_breakloop()
		 * to set handle->break_loop (we ignore it on other
		 * platforms as well).
		 */
		do {
			len = recv(handle->fd, handle->buffer, handle->bufsize, 0);
			if (handle->break_loop) {
				handle->break_loop = 0;
				return PCAP_ERROR_BREAK;
			}
			if (errno == ENOBUFS)
				handlep->packets_nobufs++;
		} while ((len == -1) && (errno == EINTR || errno == ENOBUFS));

		if (len < 0) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "Can't receive packet");
			return PCAP_ERROR;
		}

		bp = (unsigned char *)handle->buffer;
	} else
		bp = handle->bp;
	ep = bp + len;
	while (bp < ep) {
		const struct nlmsghdr *nlh = (const struct nlmsghdr *) bp;
		uint32_t msg_len;
		nftype_t type = OTHER;
		/*
		 * Has "pcap_breakloop()" been called?
		 * If so, return immediately - if we haven't read any
		 * packets, clear the flag and return PCAP_ERROR_BREAK
		 * to indicate that we were told to break out of the loop,
		 * otherwise leave the flag set, so that the *next* call
		 * will break out of the loop without having read any
		 * packets, and return the number of packets we've
		 * processed so far.
		 */
		if (handle->break_loop) {
			handle->bp = bp;
			handle->cc = ep - bp;
			if (count == 0) {
				handle->break_loop = 0;
				return PCAP_ERROR_BREAK;
			} else
				return count;
		}
		if (ep - bp < NLMSG_SPACE(0)) {
			/*
			 * There's less than one netlink message left
			 * in the buffer.  Give up.
			 */
			break;
		}

		if (nlh->nlmsg_len < sizeof(struct nlmsghdr) || (u_int)len < nlh->nlmsg_len) {
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "Message truncated: (got: %d) (nlmsg_len: %u)", len, nlh->nlmsg_len);
			return -1;
		}

		if (NFNL_SUBSYS_ID(nlh->nlmsg_type) == NFNL_SUBSYS_ULOG &&
		    NFNL_MSG_TYPE(nlh->nlmsg_type) == NFULNL_MSG_PACKET)
			type = NFLOG;
		else if (NFNL_SUBSYS_ID(nlh->nlmsg_type) == NFNL_SUBSYS_QUEUE &&
		         NFNL_MSG_TYPE(nlh->nlmsg_type) == NFQNL_MSG_PACKET)
			type = NFQUEUE;

		if (type != OTHER) {
			const unsigned char *payload = NULL;
			struct pcap_pkthdr pkth;

			const struct nfgenmsg *nfg = NULL;
			int id = 0;

			if (handle->linktype != DLT_NFLOG) {
				const struct nfattr *payload_attr = NULL;

				if (nlh->nlmsg_len < HDR_LENGTH) {
					pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "Malformed message: (nlmsg_len: %u)", nlh->nlmsg_len);
					return -1;
				}

				nfg = NLMSG_DATA(nlh);
				if (nlh->nlmsg_len > HDR_LENGTH) {
					struct nfattr *attr = NFM_NFA(nfg);
					int attr_len = nlh->nlmsg_len - NLMSG_ALIGN(HDR_LENGTH);

					while (NFA_OK(attr, attr_len)) {
						if (type == NFQUEUE) {
							switch (NFA_TYPE(attr)) {
								case NFQA_PACKET_HDR:
									{
										const struct nfqnl_msg_packet_hdr *pkt_hdr = (const struct nfqnl_msg_packet_hdr *) NFA_DATA(attr);

										id = ntohl(pkt_hdr->packet_id);
										break;
									}
								case NFQA_PAYLOAD:
									payload_attr = attr;
									break;
							}

						} else if (type == NFLOG) {
							switch (NFA_TYPE(attr)) {
								case NFULA_PAYLOAD:
									payload_attr = attr;
									break;
							}
						}
						attr = NFA_NEXT(attr, attr_len);
					}
				}

				if (payload_attr) {
					payload = NFA_DATA(payload_attr);
					pkth.len = pkth.caplen = NFA_PAYLOAD(payload_attr);
				}

			} else {
				payload = NLMSG_DATA(nlh);
				pkth.caplen = pkth.len = nlh->nlmsg_len-NLMSG_ALIGN(sizeof(struct nlmsghdr));
			}

			if (payload) {
				/* pkth.caplen = min (payload_len, handle->snapshot); */

				gettimeofday(&pkth.ts, NULL);
				if (handle->fcode.bf_insns == NULL ||
						bpf_filter(handle->fcode.bf_insns, payload, pkth.len, pkth.caplen))
				{
					handlep->packets_read++;
					callback(user, &pkth, payload);
					count++;
				}
			}

			if (type == NFQUEUE) {
				/* XXX, possible responses: NF_DROP, NF_ACCEPT, NF_STOLEN, NF_QUEUE, NF_REPEAT, NF_STOP */
				/* if type == NFQUEUE, handle->linktype is always != DLT_NFLOG,
				   so nfg is always initialized to NLMSG_DATA(nlh). */
				if (nfg != NULL)
					nfqueue_send_verdict(handle, ntohs(nfg->res_id), id, NF_ACCEPT);
			}
		}

		msg_len = NLMSG_ALIGN(nlh->nlmsg_len);
		/*
		 * If the message length would run past the end of the
		 * buffer, truncate it to the remaining space in the
		 * buffer.
		 */
		if (msg_len > ep - bp)
			msg_len = ep - bp;

		bp += msg_len;
		if (count >= max_packets && !PACKET_COUNT_IS_UNLIMITED(max_packets)) {
			handle->bp = bp;
			handle->cc = ep - bp;
			if (handle->cc < 0)
				handle->cc = 0;
			return count;
		}
	}

	handle->cc = 0;
	return count;
}

static int
netfilter_set_datalink(pcap_t *handle, int dlt)
{
	handle->linktype = dlt;
	return 0;
}

static int
netfilter_stats_linux(pcap_t *handle, struct pcap_stat *stats)
{
	struct pcap_netfilter *handlep = handle->priv;

	stats->ps_recv = handlep->packets_read;
	stats->ps_drop = handlep->packets_nobufs;
	stats->ps_ifdrop = 0;
	return 0;
}

static int
netfilter_inject_linux(pcap_t *handle, const void *buf _U_, size_t size _U_)
{
	pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "inject not supported on netfilter devices");
	return (-1);
}

struct my_nfattr {
	uint16_t nfa_len;
	uint16_t nfa_type;
	void *data;
};

static int
netfilter_send_config_msg(const pcap_t *handle, uint16_t msg_type, int ack, u_int8_t family, u_int16_t res_id, const struct my_nfattr *mynfa)
{
	char buf[1024] __attribute__ ((aligned));

	struct nlmsghdr *nlh = (struct nlmsghdr *) buf;
	struct nfgenmsg *nfg = (struct nfgenmsg *) (buf + sizeof(struct nlmsghdr));

	struct sockaddr_nl snl;
	static unsigned int seq_id;

	if (!seq_id)
		seq_id = time(NULL);
	++seq_id;

	nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct nfgenmsg));
	nlh->nlmsg_type = msg_type;
	nlh->nlmsg_flags = NLM_F_REQUEST | (ack ? NLM_F_ACK : 0);
	nlh->nlmsg_pid = 0;	/* to kernel */
	nlh->nlmsg_seq = seq_id;

	nfg->nfgen_family = family;
	nfg->version = NFNETLINK_V0;
	nfg->res_id = htons(res_id);

	if (mynfa) {
		struct nfattr *nfa = (struct nfattr *) (buf + NLMSG_ALIGN(nlh->nlmsg_len));

		nfa->nfa_type = mynfa->nfa_type;
		nfa->nfa_len = NFA_LENGTH(mynfa->nfa_len);
		memcpy(NFA_DATA(nfa), mynfa->data, mynfa->nfa_len);
		nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + NFA_ALIGN(nfa->nfa_len);
	}

	memset(&snl, 0, sizeof(snl));
	snl.nl_family = AF_NETLINK;

	if (sendto(handle->fd, nlh, nlh->nlmsg_len, 0, (struct sockaddr *) &snl, sizeof(snl)) == -1)
		return -1;

	if (!ack)
		return 0;

	/* waiting for reply loop */
	do {
		socklen_t addrlen = sizeof(snl);
		int len;

		/* ignore interrupt system call error */
		do {
			len = recvfrom(handle->fd, buf, sizeof(buf), 0, (struct sockaddr *) &snl, &addrlen);
		} while ((len == -1) && (errno == EINTR));

		if (len <= 0)
			return len;

		if (addrlen != sizeof(snl) || snl.nl_family != AF_NETLINK) {
			errno = EINVAL;
			return -1;
		}

		nlh = (struct nlmsghdr *) buf;
		if (snl.nl_pid != 0 || seq_id != nlh->nlmsg_seq)	/* if not from kernel or wrong sequence skip */
			continue;

		while ((u_int)len >= NLMSG_SPACE(0) && NLMSG_OK(nlh, (u_int)len)) {
			if (nlh->nlmsg_type == NLMSG_ERROR || (nlh->nlmsg_type == NLMSG_DONE && nlh->nlmsg_flags & NLM_F_MULTI)) {
				if (nlh->nlmsg_len < NLMSG_ALIGN(sizeof(struct nlmsgerr))) {
					errno = EBADMSG;
					return -1;
				}
				errno = -(*((int *)NLMSG_DATA(nlh)));
				return (errno == 0) ? 0 : -1;
			}
			nlh = NLMSG_NEXT(nlh, len);
		}
	} while (1);

	return -1; /* never here */
}

static int
nflog_send_config_msg(const pcap_t *handle, uint8_t family, u_int16_t group_id, const struct my_nfattr *mynfa)
{
	return netfilter_send_config_msg(handle, (NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_CONFIG, 1, family, group_id, mynfa);
}

static int
nflog_send_config_cmd(const pcap_t *handle, uint16_t group_id, u_int8_t cmd, u_int8_t family)
{
	struct nfulnl_msg_config_cmd msg;
	struct my_nfattr nfa;

	msg.command = cmd;

	nfa.data = &msg;
	nfa.nfa_type = NFULA_CFG_CMD;
	nfa.nfa_len = sizeof(msg);

	return nflog_send_config_msg(handle, family, group_id, &nfa);
}

static int
nflog_send_config_mode(const pcap_t *handle, uint16_t group_id, u_int8_t copy_mode, u_int32_t copy_range)
{
	struct nfulnl_msg_config_mode msg;
	struct my_nfattr nfa;

	msg.copy_range = htonl(copy_range);
	msg.copy_mode = copy_mode;

	nfa.data = &msg;
	nfa.nfa_type = NFULA_CFG_MODE;
	nfa.nfa_len = sizeof(msg);

	return nflog_send_config_msg(handle, AF_UNSPEC, group_id, &nfa);
}

static int
nfqueue_send_verdict(const pcap_t *handle, uint16_t group_id, u_int32_t id, u_int32_t verdict)
{
	struct nfqnl_msg_verdict_hdr msg;
	struct my_nfattr nfa;

	msg.id = htonl(id);
	msg.verdict = htonl(verdict);

	nfa.data = &msg;
	nfa.nfa_type = NFQA_VERDICT_HDR;
	nfa.nfa_len = sizeof(msg);

	return netfilter_send_config_msg(handle, (NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_VERDICT, 0, AF_UNSPEC, group_id, &nfa);
}

static int
nfqueue_send_config_msg(const pcap_t *handle, uint8_t family, u_int16_t group_id, const struct my_nfattr *mynfa)
{
	return netfilter_send_config_msg(handle, (NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_CONFIG, 1, family, group_id, mynfa);
}

static int
nfqueue_send_config_cmd(const pcap_t *handle, uint16_t group_id, u_int8_t cmd, u_int16_t pf)
{
	struct nfqnl_msg_config_cmd msg;
	struct my_nfattr nfa;

	msg.command = cmd;
	msg.pf = htons(pf);

	nfa.data = &msg;
	nfa.nfa_type = NFQA_CFG_CMD;
	nfa.nfa_len = sizeof(msg);

	return nfqueue_send_config_msg(handle, AF_UNSPEC, group_id, &nfa);
}

static int
nfqueue_send_config_mode(const pcap_t *handle, uint16_t group_id, u_int8_t copy_mode, u_int32_t copy_range)
{
	struct nfqnl_msg_config_params msg;
	struct my_nfattr nfa;

	msg.copy_range = htonl(copy_range);
	msg.copy_mode = copy_mode;

	nfa.data = &msg;
	nfa.nfa_type = NFQA_CFG_PARAMS;
	nfa.nfa_len = sizeof(msg);

	return nfqueue_send_config_msg(handle, AF_UNSPEC, group_id, &nfa);
}

static int
netfilter_activate(pcap_t* handle)
{
	const char *dev = handle->opt.device;
	unsigned short groups[32];
	int group_count = 0;
	nftype_t type = OTHER;
	int i;

 	if (strncmp(dev, NFLOG_IFACE, strlen(NFLOG_IFACE)) == 0) {
 		dev += strlen(NFLOG_IFACE);
		type = NFLOG;

	} else if (strncmp(dev, NFQUEUE_IFACE, strlen(NFQUEUE_IFACE)) == 0) {
		dev += strlen(NFQUEUE_IFACE);
		type = NFQUEUE;
	}

	if (type != OTHER && *dev == ':') {
		dev++;
		while (*dev) {
			long int group_id;
			char *end_dev;

			if (group_count == 32) {
				pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
						"Maximum 32 netfilter groups! dev: %s",
						handle->opt.device);
				return PCAP_ERROR;
			}

			group_id = strtol(dev, &end_dev, 0);
			if (end_dev != dev) {
				if (group_id < 0 || group_id > 65535) {
					pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
							"Netfilter group range from 0 to 65535 (got %ld)",
							group_id);
					return PCAP_ERROR;
				}

				groups[group_count++] = (unsigned short) group_id;
				dev = end_dev;
			}
			if (*dev != ',')
				break;
			dev++;
		}
	}

	if (type == OTHER || *dev) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
				"Can't get netfilter group(s) index from %s",
				handle->opt.device);
		return PCAP_ERROR;
	}

	/* if no groups, add default: 0 */
	if (!group_count) {
		groups[0] = 0;
		group_count = 1;
	}

	/*
	 * Turn a negative snapshot value (invalid), a snapshot value of
	 * 0 (unspecified), or a value bigger than the normal maximum
	 * value, into the maximum allowed value.
	 *
	 * If some application really *needs* a bigger snapshot
	 * length, we should just increase MAXIMUM_SNAPLEN.
	 */
	if (handle->snapshot <= 0 || handle->snapshot > MAXIMUM_SNAPLEN)
		handle->snapshot = MAXIMUM_SNAPLEN;

	/* Initialize some components of the pcap structure. */
	handle->bufsize = 128 + handle->snapshot;
	handle->offset = 0;
	handle->read_op = netfilter_read_linux;
	handle->inject_op = netfilter_inject_linux;
	handle->setfilter_op = install_bpf_program; /* no kernel filtering */
	handle->setdirection_op = NULL;
	handle->set_datalink_op = netfilter_set_datalink;
	handle->getnonblock_op = pcap_getnonblock_fd;
	handle->setnonblock_op = pcap_setnonblock_fd;
	handle->stats_op = netfilter_stats_linux;

	/* Create netlink socket */
	handle->fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER);
	if (handle->fd < 0) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "Can't create raw socket");
		return PCAP_ERROR;
	}

	if (type == NFLOG) {
		handle->linktype = DLT_NFLOG;
		handle->dlt_list = (u_int *) malloc(sizeof(u_int) * 2);
		if (handle->dlt_list != NULL) {
			handle->dlt_list[0] = DLT_NFLOG;
			handle->dlt_list[1] = DLT_IPV4;
			handle->dlt_count = 2;
		}

	} else
		handle->linktype = DLT_IPV4;

	handle->buffer = malloc(handle->bufsize);
	if (!handle->buffer) {
		pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "Can't allocate dump buffer");
		goto close_fail;
	}

	if (type == NFLOG) {
		if (nflog_send_config_cmd(handle, 0, NFULNL_CFG_CMD_PF_UNBIND, AF_INET) < 0) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno,
			    "NFULNL_CFG_CMD_PF_UNBIND");
			goto close_fail;
		}

		if (nflog_send_config_cmd(handle, 0, NFULNL_CFG_CMD_PF_BIND, AF_INET) < 0) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "NFULNL_CFG_CMD_PF_BIND");
			goto close_fail;
		}

		/* Bind socket to the nflog groups */
		for (i = 0; i < group_count; i++) {
			if (nflog_send_config_cmd(handle, groups[i], NFULNL_CFG_CMD_BIND, AF_UNSPEC) < 0) {
				pcap_fmt_errmsg_for_errno(handle->errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "Can't listen on group group index");
				goto close_fail;
			}

			if (nflog_send_config_mode(handle, groups[i], NFULNL_COPY_PACKET, handle->snapshot) < 0) {
				pcap_fmt_errmsg_for_errno(handle->errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "NFULNL_COPY_PACKET");
				goto close_fail;
			}
		}

	} else {
		if (nfqueue_send_config_cmd(handle, 0, NFQNL_CFG_CMD_PF_UNBIND, AF_INET) < 0) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "NFQNL_CFG_CMD_PF_UNBIND");
			goto close_fail;
		}

		if (nfqueue_send_config_cmd(handle, 0, NFQNL_CFG_CMD_PF_BIND, AF_INET) < 0) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "NFQNL_CFG_CMD_PF_BIND");
			goto close_fail;
		}

		/* Bind socket to the nfqueue groups */
		for (i = 0; i < group_count; i++) {
			if (nfqueue_send_config_cmd(handle, groups[i], NFQNL_CFG_CMD_BIND, AF_UNSPEC) < 0) {
				pcap_fmt_errmsg_for_errno(handle->errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "Can't listen on group group index");
				goto close_fail;
			}

			if (nfqueue_send_config_mode(handle, groups[i], NFQNL_COPY_PACKET, handle->snapshot) < 0) {
				pcap_fmt_errmsg_for_errno(handle->errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "NFQNL_COPY_PACKET");
				goto close_fail;
			}
		}
	}

	if (handle->opt.rfmon) {
		/*
		 * Monitor mode doesn't apply to netfilter devices.
		 */
		pcap_cleanup_live_common(handle);
		return PCAP_ERROR_RFMON_NOTSUP;
	}

	if (handle->opt.buffer_size != 0) {
		/*
		 * Set the socket buffer size to the specified value.
		 */
		if (setsockopt(handle->fd, SOL_SOCKET, SO_RCVBUF, &handle->opt.buffer_size, sizeof(handle->opt.buffer_size)) == -1) {
			pcap_fmt_errmsg_for_errno(handle->errbuf,
			    PCAP_ERRBUF_SIZE, errno, "SO_RCVBUF");
			goto close_fail;
		}
	}

	handle->selectable_fd = handle->fd;
	return 0;

close_fail:
	pcap_cleanup_live_common(handle);
	return PCAP_ERROR;
}

pcap_t *
netfilter_create(const char *device, char *ebuf, int *is_ours)
{
	const char *cp;
	pcap_t *p;

	/* Does this look like an netfilter device? */
	cp = strrchr(device, '/');
	if (cp == NULL)
		cp = device;

	/* Does it begin with NFLOG_IFACE or NFQUEUE_IFACE? */
	if (strncmp(cp, NFLOG_IFACE, sizeof NFLOG_IFACE - 1) == 0)
		cp += sizeof NFLOG_IFACE - 1;
	else if (strncmp(cp, NFQUEUE_IFACE, sizeof NFQUEUE_IFACE - 1) == 0)
		cp += sizeof NFQUEUE_IFACE - 1;
	else {
		/* Nope, doesn't begin with NFLOG_IFACE nor NFQUEUE_IFACE */
		*is_ours = 0;
		return NULL;
	}

	/*
	 * Yes - is that either the end of the name, or is it followed
	 * by a colon?
	 */
	if (*cp != ':' && *cp != '\0') {
		/* Nope */
		*is_ours = 0;
		return NULL;
	}

	/* OK, it's probably ours. */
	*is_ours = 1;

	p = pcap_create_common(ebuf, sizeof (struct pcap_netfilter));
	if (p == NULL)
		return (NULL);

	p->activate_op = netfilter_activate;
	return (p);
}

int
netfilter_findalldevs(pcap_if_list_t *devlistp, char *err_str)
{
	int sock;

	sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER);
	if (sock < 0) {
		/* if netlink is not supported this is not fatal */
		if (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT)
			return 0;
		pcap_fmt_errmsg_for_errno(err_str, PCAP_ERRBUF_SIZE,
		    errno, "Can't open netlink socket");
		return -1;
	}
	close(sock);

	/*
	 * The notion of "connected" vs. "disconnected" doesn't apply.
	 * XXX - what about "up" and "running"?
	 */
	if (add_dev(devlistp, NFLOG_IFACE,
	    PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE,
	    "Linux netfilter log (NFLOG) interface", err_str) == NULL)
		return -1;
	if (add_dev(devlistp, NFQUEUE_IFACE,
	    PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE,
	    "Linux netfilter queue (NFQUEUE) interface", err_str) == NULL)
		return -1;
	return 0;
}
