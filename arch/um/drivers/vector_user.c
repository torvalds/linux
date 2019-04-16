/*
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/ether.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <linux/virtio_net.h>
#include <netdb.h>
#include <stdlib.h>
#include <os.h>
#include <um_malloc.h>
#include "vector_user.h"

#define ID_GRE 0
#define ID_L2TPV3 1
#define ID_MAX 1

#define TOKEN_IFNAME "ifname"

#define TRANS_RAW "raw"
#define TRANS_RAW_LEN strlen(TRANS_RAW)

#define VNET_HDR_FAIL "could not enable vnet headers on fd %d"
#define TUN_GET_F_FAIL "tapraw: TUNGETFEATURES failed: %s"
#define L2TPV3_BIND_FAIL "l2tpv3_open : could not bind socket err=%i"
#define BPF_ATTACH_FAIL "Failed to attach filter size %d to %d, err %d\n"

/* This is very ugly and brute force lookup, but it is done
 * only once at initialization so not worth doing hashes or
 * anything more intelligent
 */

char *uml_vector_fetch_arg(struct arglist *ifspec, char *token)
{
	int i;

	for (i = 0; i < ifspec->numargs; i++) {
		if (strcmp(ifspec->tokens[i], token) == 0)
			return ifspec->values[i];
	}
	return NULL;

}

struct arglist *uml_parse_vector_ifspec(char *arg)
{
	struct arglist *result;
	int pos, len;
	bool parsing_token = true, next_starts = true;

	if (arg == NULL)
		return NULL;
	result = uml_kmalloc(sizeof(struct arglist), UM_GFP_KERNEL);
	if (result == NULL)
		return NULL;
	result->numargs = 0;
	len = strlen(arg);
	for (pos = 0; pos < len; pos++) {
		if (next_starts) {
			if (parsing_token) {
				result->tokens[result->numargs] = arg + pos;
			} else {
				result->values[result->numargs] = arg + pos;
				result->numargs++;
			}
			next_starts = false;
		}
		if (*(arg + pos) == '=') {
			if (parsing_token)
				parsing_token = false;
			else
				goto cleanup;
			next_starts = true;
			(*(arg + pos)) = '\0';
		}
		if (*(arg + pos) == ',') {
			parsing_token = true;
			next_starts = true;
			(*(arg + pos)) = '\0';
		}
	}
	return result;
cleanup:
	printk(UM_KERN_ERR "vector_setup - Couldn't parse '%s'\n", arg);
	kfree(result);
	return NULL;
}

/*
 * Socket/FD configuration functions. These return an structure
 * of rx and tx descriptors to cover cases where these are not
 * the same (f.e. read via raw socket and write via tap).
 */

#define PATH_NET_TUN "/dev/net/tun"

static struct vector_fds *user_init_tap_fds(struct arglist *ifspec)
{
	struct ifreq ifr;
	int fd = -1;
	struct sockaddr_ll sock;
	int err = -ENOMEM, offload;
	char *iface;
	struct vector_fds *result = NULL;

	iface = uml_vector_fetch_arg(ifspec, TOKEN_IFNAME);
	if (iface == NULL) {
		printk(UM_KERN_ERR "uml_tap: failed to parse interface spec\n");
		goto tap_cleanup;
	}

	result = uml_kmalloc(sizeof(struct vector_fds), UM_GFP_KERNEL);
	if (result == NULL) {
		printk(UM_KERN_ERR "uml_tap: failed to allocate file descriptors\n");
		goto tap_cleanup;
	}
	result->rx_fd = -1;
	result->tx_fd = -1;
	result->remote_addr = NULL;
	result->remote_addr_size = 0;

	/* TAP */

	fd = open(PATH_NET_TUN, O_RDWR);
	if (fd < 0) {
		printk(UM_KERN_ERR "uml_tap: failed to open tun device\n");
		goto tap_cleanup;
	}
	result->tx_fd = fd;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_VNET_HDR;
	strncpy((char *)&ifr.ifr_name, iface, sizeof(ifr.ifr_name) - 1);

	err = ioctl(fd, TUNSETIFF, (void *) &ifr);
	if (err != 0) {
		printk(UM_KERN_ERR "uml_tap: failed to select tap interface\n");
		goto tap_cleanup;
	}

	offload = TUN_F_CSUM | TUN_F_TSO4 | TUN_F_TSO6;
	ioctl(fd, TUNSETOFFLOAD, offload);

	/* RAW */

	fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (fd == -1) {
		printk(UM_KERN_ERR
			"uml_tap: failed to create socket: %i\n", -errno);
		goto tap_cleanup;
	}
	result->rx_fd = fd;
	memset(&ifr, 0, sizeof(ifr));
	strncpy((char *)&ifr.ifr_name, iface, sizeof(ifr.ifr_name) - 1);
	if (ioctl(fd, SIOCGIFINDEX, (void *) &ifr) < 0) {
		printk(UM_KERN_ERR
			"uml_tap: failed to set interface: %i\n", -errno);
		goto tap_cleanup;
	}

	sock.sll_family = AF_PACKET;
	sock.sll_protocol = htons(ETH_P_ALL);
	sock.sll_ifindex = ifr.ifr_ifindex;

	if (bind(fd,
		(struct sockaddr *) &sock, sizeof(struct sockaddr_ll)) < 0) {
		printk(UM_KERN_ERR
			"user_init_tap: failed to bind raw pair, err %d\n",
				-errno);
		goto tap_cleanup;
	}
	return result;
tap_cleanup:
	printk(UM_KERN_ERR "user_init_tap: init failed, error %d", err);
	if (result != NULL) {
		if (result->rx_fd >= 0)
			os_close_file(result->rx_fd);
		if (result->tx_fd >= 0)
			os_close_file(result->tx_fd);
		kfree(result);
	}
	return NULL;
}


static struct vector_fds *user_init_raw_fds(struct arglist *ifspec)
{
	struct ifreq ifr;
	int rxfd = -1, txfd = -1;
	struct sockaddr_ll sock;
	int err = -ENOMEM;
	char *iface;
	struct vector_fds *result = NULL;

	iface = uml_vector_fetch_arg(ifspec, TOKEN_IFNAME);
	if (iface == NULL)
		goto cleanup;

	rxfd = socket(AF_PACKET, SOCK_RAW, ETH_P_ALL);
	if (rxfd == -1) {
		err = -errno;
		goto cleanup;
	}
	txfd = socket(AF_PACKET, SOCK_RAW, 0); /* Turn off RX on this fd */
	if (txfd == -1) {
		err = -errno;
		goto cleanup;
	}
	memset(&ifr, 0, sizeof(ifr));
	strncpy((char *)&ifr.ifr_name, iface, sizeof(ifr.ifr_name) - 1);
	if (ioctl(rxfd, SIOCGIFINDEX, (void *) &ifr) < 0) {
		err = -errno;
		goto cleanup;
	}

	sock.sll_family = AF_PACKET;
	sock.sll_protocol = htons(ETH_P_ALL);
	sock.sll_ifindex = ifr.ifr_ifindex;

	if (bind(rxfd,
		(struct sockaddr *) &sock, sizeof(struct sockaddr_ll)) < 0) {
		err = -errno;
		goto cleanup;
	}

	sock.sll_family = AF_PACKET;
	sock.sll_protocol = htons(ETH_P_IP);
	sock.sll_ifindex = ifr.ifr_ifindex;

	if (bind(txfd,
		(struct sockaddr *) &sock, sizeof(struct sockaddr_ll)) < 0) {
		err = -errno;
		goto cleanup;
	}

	result = uml_kmalloc(sizeof(struct vector_fds), UM_GFP_KERNEL);
	if (result != NULL) {
		result->rx_fd = rxfd;
		result->tx_fd = txfd;
		result->remote_addr = NULL;
		result->remote_addr_size = 0;
	}
	return result;
cleanup:
	printk(UM_KERN_ERR "user_init_raw: init failed, error %d", err);
	if (rxfd >= 0)
		os_close_file(rxfd);
	if (txfd >= 0)
		os_close_file(txfd);
	kfree(result);
	return NULL;
}


bool uml_raw_enable_qdisc_bypass(int fd)
{
	int optval = 1;

	if (setsockopt(fd,
		SOL_PACKET, PACKET_QDISC_BYPASS,
		&optval, sizeof(optval)) != 0) {
		return false;
	}
	return true;
}

bool uml_raw_enable_vnet_headers(int fd)
{
	int optval = 1;

	if (setsockopt(fd,
		SOL_PACKET, PACKET_VNET_HDR,
		&optval, sizeof(optval)) != 0) {
		printk(UM_KERN_INFO VNET_HDR_FAIL, fd);
		return false;
	}
	return true;
}
bool uml_tap_enable_vnet_headers(int fd)
{
	unsigned int features;
	int len = sizeof(struct virtio_net_hdr);

	if (ioctl(fd, TUNGETFEATURES, &features) == -1) {
		printk(UM_KERN_INFO TUN_GET_F_FAIL, strerror(errno));
		return false;
	}
	if ((features & IFF_VNET_HDR) == 0) {
		printk(UM_KERN_INFO "tapraw: No VNET HEADER support");
		return false;
	}
	ioctl(fd, TUNSETVNETHDRSZ, &len);
	return true;
}

static struct vector_fds *user_init_socket_fds(struct arglist *ifspec, int id)
{
	int err = -ENOMEM;
	int fd = -1, gairet;
	struct addrinfo srchints;
	struct addrinfo dsthints;
	bool v6, udp;
	char *value;
	char *src, *dst, *srcport, *dstport;
	struct addrinfo *gairesult = NULL;
	struct vector_fds *result = NULL;


	value = uml_vector_fetch_arg(ifspec, "v6");
	v6 = false;
	udp = false;
	if (value != NULL) {
		if (strtol((const char *) value, NULL, 10) > 0)
			v6 = true;
	}

	value = uml_vector_fetch_arg(ifspec, "udp");
	if (value != NULL) {
		if (strtol((const char *) value, NULL, 10) > 0)
			udp = true;
	}
	src = uml_vector_fetch_arg(ifspec, "src");
	dst = uml_vector_fetch_arg(ifspec, "dst");
	srcport = uml_vector_fetch_arg(ifspec, "srcport");
	dstport = uml_vector_fetch_arg(ifspec, "dstport");

	memset(&dsthints, 0, sizeof(dsthints));

	if (v6)
		dsthints.ai_family = AF_INET6;
	else
		dsthints.ai_family = AF_INET;

	switch (id) {
	case ID_GRE:
		dsthints.ai_socktype = SOCK_RAW;
		dsthints.ai_protocol = IPPROTO_GRE;
		break;
	case ID_L2TPV3:
		if (udp) {
			dsthints.ai_socktype = SOCK_DGRAM;
			dsthints.ai_protocol = 0;
		} else {
			dsthints.ai_socktype = SOCK_RAW;
			dsthints.ai_protocol = IPPROTO_L2TP;
		}
		break;
	default:
		printk(KERN_ERR "Unsupported socket type\n");
		return NULL;
	}
	memcpy(&srchints, &dsthints, sizeof(struct addrinfo));

	gairet = getaddrinfo(src, srcport, &dsthints, &gairesult);
	if ((gairet != 0) || (gairesult == NULL)) {
		printk(UM_KERN_ERR
			"socket_open : could not resolve src, error = %s",
			gai_strerror(gairet)
		);
		return NULL;
	}
	fd = socket(gairesult->ai_family,
		gairesult->ai_socktype, gairesult->ai_protocol);
	if (fd == -1) {
		printk(UM_KERN_ERR
			"socket_open : could not open socket, error = %d",
			-errno
		);
		goto cleanup;
	}
	if (bind(fd,
		(struct sockaddr *) gairesult->ai_addr,
		gairesult->ai_addrlen)) {
		printk(UM_KERN_ERR L2TPV3_BIND_FAIL, errno);
		goto cleanup;
	}

	if (gairesult != NULL)
		freeaddrinfo(gairesult);

	gairesult = NULL;

	gairet = getaddrinfo(dst, dstport, &dsthints, &gairesult);
	if ((gairet != 0) || (gairesult == NULL)) {
		printk(UM_KERN_ERR
			"socket_open : could not resolve dst, error = %s",
			gai_strerror(gairet)
		);
		return NULL;
	}

	result = uml_kmalloc(sizeof(struct vector_fds), UM_GFP_KERNEL);
	if (result != NULL) {
		result->rx_fd = fd;
		result->tx_fd = fd;
		result->remote_addr = uml_kmalloc(
			gairesult->ai_addrlen, UM_GFP_KERNEL);
		if (result->remote_addr == NULL)
			goto cleanup;
		result->remote_addr_size = gairesult->ai_addrlen;
		memcpy(
			result->remote_addr,
			gairesult->ai_addr,
			gairesult->ai_addrlen
		);
	}
	freeaddrinfo(gairesult);
	return result;
cleanup:
	if (gairesult != NULL)
		freeaddrinfo(gairesult);
	printk(UM_KERN_ERR "user_init_socket: init failed, error %d", err);
	if (fd >= 0)
		os_close_file(fd);
	if (result != NULL) {
		kfree(result->remote_addr);
		kfree(result);
	}
	return NULL;
}

struct vector_fds *uml_vector_user_open(
	int unit,
	struct arglist *parsed
)
{
	char *transport;

	if (parsed == NULL) {
		printk(UM_KERN_ERR "no parsed config for unit %d\n", unit);
		return NULL;
	}
	transport = uml_vector_fetch_arg(parsed, "transport");
	if (transport == NULL) {
		printk(UM_KERN_ERR "missing transport for unit %d\n", unit);
		return NULL;
	}
	if (strncmp(transport, TRANS_RAW, TRANS_RAW_LEN) == 0)
		return user_init_raw_fds(parsed);
	if (strncmp(transport, TRANS_TAP, TRANS_TAP_LEN) == 0)
		return user_init_tap_fds(parsed);
	if (strncmp(transport, TRANS_GRE, TRANS_GRE_LEN) == 0)
		return user_init_socket_fds(parsed, ID_GRE);
	if (strncmp(transport, TRANS_L2TPV3, TRANS_L2TPV3_LEN) == 0)
		return user_init_socket_fds(parsed, ID_L2TPV3);
	return NULL;
}


int uml_vector_sendmsg(int fd, void *hdr, int flags)
{
	int n;

	CATCH_EINTR(n = sendmsg(fd, (struct msghdr *) hdr,  flags));
	if ((n < 0) && (errno == EAGAIN))
		return 0;
	if (n >= 0)
		return n;
	else
		return -errno;
}

int uml_vector_recvmsg(int fd, void *hdr, int flags)
{
	int n;

	CATCH_EINTR(n = recvmsg(fd, (struct msghdr *) hdr,  flags));
	if ((n < 0) && (errno == EAGAIN))
		return 0;
	if (n >= 0)
		return n;
	else
		return -errno;
}

int uml_vector_writev(int fd, void *hdr, int iovcount)
{
	int n;

	CATCH_EINTR(n = writev(fd, (struct iovec *) hdr,  iovcount));
	if ((n < 0) && (errno == EAGAIN))
		return 0;
	if (n >= 0)
		return n;
	else
		return -errno;
}

int uml_vector_sendmmsg(
	int fd,
	void *msgvec,
	unsigned int vlen,
	unsigned int flags)
{
	int n;

	CATCH_EINTR(n = sendmmsg(fd, (struct mmsghdr *) msgvec, vlen, flags));
	if ((n < 0) && (errno == EAGAIN))
		return 0;
	if (n >= 0)
		return n;
	else
		return -errno;
}

int uml_vector_recvmmsg(
	int fd,
	void *msgvec,
	unsigned int vlen,
	unsigned int flags)
{
	int n;

	CATCH_EINTR(
		n = recvmmsg(fd, (struct mmsghdr *) msgvec, vlen, flags, 0));
	if ((n < 0) && (errno == EAGAIN))
		return 0;
	if (n >= 0)
		return n;
	else
		return -errno;
}
int uml_vector_attach_bpf(int fd, void *bpf, int bpf_len)
{
	int err = setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, bpf, bpf_len);

	if (err < 0)
		printk(KERN_ERR BPF_ATTACH_FAIL, bpf_len, fd, -errno);
	return err;
}

#define DEFAULT_BPF_LEN 6

void *uml_vector_default_bpf(int fd, void *mac)
{
	struct sock_filter *bpf;
	uint32_t *mac1 = (uint32_t *)(mac + 2);
	uint16_t *mac2 = (uint16_t *) mac;
	struct sock_fprog bpf_prog = {
		.len = 6,
		.filter = NULL,
	};

	bpf = uml_kmalloc(
		sizeof(struct sock_filter) * DEFAULT_BPF_LEN, UM_GFP_KERNEL);
	if (bpf != NULL) {
		bpf_prog.filter = bpf;
		/* ld	[8] */
		bpf[0] = (struct sock_filter){ 0x20, 0, 0, 0x00000008 };
		/* jeq	#0xMAC[2-6] jt 2 jf 5*/
		bpf[1] = (struct sock_filter){ 0x15, 0, 3, ntohl(*mac1)};
		/* ldh	[6] */
		bpf[2] = (struct sock_filter){ 0x28, 0, 0, 0x00000006 };
		/* jeq	#0xMAC[0-1] jt 4 jf 5 */
		bpf[3] = (struct sock_filter){ 0x15, 0, 1, ntohs(*mac2)};
		/* ret	#0 */
		bpf[4] = (struct sock_filter){ 0x6, 0, 0, 0x00000000 };
		/* ret	#0x40000 */
		bpf[5] = (struct sock_filter){ 0x6, 0, 0, 0x00040000 };
		if (uml_vector_attach_bpf(
			fd, &bpf_prog, sizeof(struct sock_fprog)) < 0) {
			kfree(bpf);
			bpf = NULL;
		}
	}
	return bpf;
}

