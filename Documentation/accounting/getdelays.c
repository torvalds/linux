/* getdelays.c
 *
 * Utility to get per-pid and per-tgid delay accounting statistics
 * Also illustrates usage of the taskstats interface
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2005
 * Copyright (C) Balbir Singh, IBM Corp. 2006
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>

#include <linux/genetlink.h>
#include <linux/taskstats.h>

/*
 * Generic macros for dealing with netlink sockets. Might be duplicated
 * elsewhere. It is recommended that commercial grade applications use
 * libnl or libnetlink and use the interfaces provided by the library
 */
#define GENLMSG_DATA(glh)	((void *)(NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh)	(NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)
#define NLA_DATA(na)		((void *)((char*)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len)	(len - NLA_HDRLEN)

#define err(code, fmt, arg...) do { printf(fmt, ##arg); exit(code); } while (0)
int done = 0;

/*
 * Create a raw netlink socket and bind
 */
static int create_nl_socket(int protocol, int groups)
{
    socklen_t addr_len;
    int fd;
    struct sockaddr_nl local;

    fd = socket(AF_NETLINK, SOCK_RAW, protocol);
    if (fd < 0)
	return -1;

    memset(&local, 0, sizeof(local));
    local.nl_family = AF_NETLINK;
    local.nl_groups = groups;

    if (bind(fd, (struct sockaddr *) &local, sizeof(local)) < 0)
	goto error;

    return fd;
  error:
    close(fd);
    return -1;
}

int sendto_fd(int s, const char *buf, int bufLen)
{
    struct sockaddr_nl nladdr;
    int r;

    memset(&nladdr, 0, sizeof(nladdr));
    nladdr.nl_family = AF_NETLINK;

    while ((r = sendto(s, buf, bufLen, 0, (struct sockaddr *) &nladdr,
		       sizeof(nladdr))) < bufLen) {
	if (r > 0) {
	    buf += r;
	    bufLen -= r;
	} else if (errno != EAGAIN)
	    return -1;
    }
    return 0;
}

/*
 * Probe the controller in genetlink to find the family id
 * for the TASKSTATS family
 */
int get_family_id(int sd)
{
    struct {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char buf[256];
    } family_req;
    struct {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char buf[256];
    } ans;

    int id;
    struct nlattr *na;
    int rep_len;

    /* Get family name */
    family_req.n.nlmsg_type = GENL_ID_CTRL;
    family_req.n.nlmsg_flags = NLM_F_REQUEST;
    family_req.n.nlmsg_seq = 0;
    family_req.n.nlmsg_pid = getpid();
    family_req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    family_req.g.cmd = CTRL_CMD_GETFAMILY;
    family_req.g.version = 0x1;
    na = (struct nlattr *) GENLMSG_DATA(&family_req);
    na->nla_type = CTRL_ATTR_FAMILY_NAME;
    na->nla_len = strlen(TASKSTATS_GENL_NAME) + 1 + NLA_HDRLEN;
    strcpy(NLA_DATA(na), TASKSTATS_GENL_NAME);
    family_req.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

    if (sendto_fd(sd, (char *) &family_req, family_req.n.nlmsg_len) < 0)
	err(1, "error sending message via Netlink\n");

    rep_len = recv(sd, &ans, sizeof(ans), 0);

    if (rep_len < 0)
	err(1, "error receiving reply message via Netlink\n");


    /* Validate response message */
    if (!NLMSG_OK((&ans.n), rep_len))
	err(1, "invalid reply message received via Netlink\n");

    if (ans.n.nlmsg_type == NLMSG_ERROR) {	/* error */
	printf("error received NACK - leaving\n");
	exit(1);
    }


    na = (struct nlattr *) GENLMSG_DATA(&ans);
    na = (struct nlattr *) ((char *) na + NLA_ALIGN(na->nla_len));
    if (na->nla_type == CTRL_ATTR_FAMILY_ID) {
	id = *(__u16 *) NLA_DATA(na);
    }
    return id;
}

void print_taskstats(struct taskstats *t)
{
    printf("\n\nCPU   %15s%15s%15s%15s\n"
	   "      %15llu%15llu%15llu%15llu\n"
	   "IO    %15s%15s\n"
	   "      %15llu%15llu\n"
	   "MEM   %15s%15s\n"
	   "      %15llu%15llu\n\n",
	   "count", "real total", "virtual total", "delay total",
	   t->cpu_count, t->cpu_run_real_total, t->cpu_run_virtual_total,
	   t->cpu_delay_total,
	   "count", "delay total",
	   t->blkio_count, t->blkio_delay_total,
	   "count", "delay total", t->swapin_count, t->swapin_delay_total);
}

void sigchld(int sig)
{
    done = 1;
}

int main(int argc, char *argv[])
{
    int rc;
    int sk_nl;
    struct nlmsghdr *nlh;
    struct genlmsghdr *genlhdr;
    char *buf;
    struct taskstats_cmd_param *param;
    __u16 id;
    struct nlattr *na;

    /* For receiving */
    struct sockaddr_nl kern_nla, from_nla;
    socklen_t from_nla_len;
    int recv_len;
    struct taskstats_reply *reply;

    struct {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char buf[256];
    } req;

    struct {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char buf[256];
    } ans;

    int nl_sd = -1;
    int rep_len;
    int len = 0;
    int aggr_len, len2;
    struct sockaddr_nl nladdr;
    pid_t tid = 0;
    pid_t rtid = 0;
    int cmd_type = TASKSTATS_TYPE_TGID;
    int c, status;
    int forking = 0;
    struct sigaction act = {
	.sa_handler = SIG_IGN,
	.sa_mask = SA_NOMASK,
    };
    struct sigaction tact ;

    if (argc < 3) {
	printf("usage %s [-t tgid][-p pid][-c cmd]\n", argv[0]);
	exit(-1);
    }

    tact.sa_handler = sigchld;
    sigemptyset(&tact.sa_mask);
    if (sigaction(SIGCHLD, &tact, NULL) < 0)
	err(1, "sigaction failed for SIGCHLD\n");

    while (1) {

	c = getopt(argc, argv, "t:p:c:");
	if (c < 0)
	    break;

	switch (c) {
	case 't':
	    tid = atoi(optarg);
	    if (!tid)
		err(1, "Invalid tgid\n");
	    cmd_type = TASKSTATS_CMD_ATTR_TGID;
	    break;
	case 'p':
	    tid = atoi(optarg);
	    if (!tid)
		err(1, "Invalid pid\n");
	    cmd_type = TASKSTATS_CMD_ATTR_TGID;
	    break;
	case 'c':
	    opterr = 0;
	    tid = fork();
	    if (tid < 0)
		err(1, "fork failed\n");

	    if (tid == 0) {	/* child process */
		if (execvp(argv[optind - 1], &argv[optind - 1]) < 0) {
		    exit(-1);
		}
	    }
	    forking = 1;
	    break;
	default:
	    printf("usage %s [-t tgid][-p pid][-c cmd]\n", argv[0]);
	    exit(-1);
	    break;
	}
	if (c == 'c')
	    break;
    }

    /* Construct Netlink request message */

    /* Send Netlink request message & get reply */

    if ((nl_sd =
	 create_nl_socket(NETLINK_GENERIC, TASKSTATS_LISTEN_GROUP)) < 0)
	err(1, "error creating Netlink socket\n");


    id = get_family_id(nl_sd);

    /* Send command needed */
    req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    req.n.nlmsg_type = id;
    req.n.nlmsg_flags = NLM_F_REQUEST;
    req.n.nlmsg_seq = 0;
    req.n.nlmsg_pid = tid;
    req.g.cmd = TASKSTATS_CMD_GET;
    na = (struct nlattr *) GENLMSG_DATA(&req);
    na->nla_type = cmd_type;
    na->nla_len = sizeof(unsigned int) + NLA_HDRLEN;
    *(__u32 *) NLA_DATA(na) = tid;
    req.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);


    if (!forking && sendto_fd(nl_sd, (char *) &req, req.n.nlmsg_len) < 0)
	err(1, "error sending message via Netlink\n");

    act.sa_handler = SIG_IGN;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGINT, &act, NULL) < 0)
	err(1, "sigaction failed for SIGINT\n");

    do {
	int i;
	struct pollfd pfd;
	int pollres;

	pfd.events = 0xffff & ~POLLOUT;
	pfd.fd = nl_sd;
	pollres = poll(&pfd, 1, 5000);
	if (pollres < 0 || done) {
	    break;
	}

	rep_len = recv(nl_sd, &ans, sizeof(ans), 0);
	nladdr.nl_family = AF_NETLINK;
	nladdr.nl_groups = TASKSTATS_LISTEN_GROUP;

	if (ans.n.nlmsg_type == NLMSG_ERROR) {	/* error */
	    printf("error received NACK - leaving\n");
	    exit(1);
	}

	if (rep_len < 0) {
	    err(1, "error receiving reply message via Netlink\n");
	    break;
	}

	/* Validate response message */
	if (!NLMSG_OK((&ans.n), rep_len))
	    err(1, "invalid reply message received via Netlink\n");

	rep_len = GENLMSG_PAYLOAD(&ans.n);

	na = (struct nlattr *) GENLMSG_DATA(&ans);
	len = 0;
	i = 0;
	while (len < rep_len) {
	    len += NLA_ALIGN(na->nla_len);
	    switch (na->nla_type) {
	    case TASKSTATS_TYPE_AGGR_PID:
		/* Fall through */
	    case TASKSTATS_TYPE_AGGR_TGID:
		aggr_len = NLA_PAYLOAD(na->nla_len);
		len2 = 0;
		/* For nested attributes, na follows */
		na = (struct nlattr *) NLA_DATA(na);
		done = 0;
		while (len2 < aggr_len) {
		    switch (na->nla_type) {
		    case TASKSTATS_TYPE_PID:
			rtid = *(int *) NLA_DATA(na);
			break;
		    case TASKSTATS_TYPE_TGID:
			rtid = *(int *) NLA_DATA(na);
			break;
		    case TASKSTATS_TYPE_STATS:
			if (rtid == tid) {
			    print_taskstats((struct taskstats *)
					    NLA_DATA(na));
			    done = 1;
			}
			break;
		    }
		    len2 += NLA_ALIGN(na->nla_len);
		    na = (struct nlattr *) ((char *) na + len2);
		    if (done)
			break;
		}
	    }
	    na = (struct nlattr *) (GENLMSG_DATA(&ans) + len);
	    if (done)
		break;
	}
	if (done)
	    break;
    }
    while (1);

    close(nl_sd);
    return 0;
}
