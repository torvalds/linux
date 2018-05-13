/*
 * Copyright (C) 2002 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __UM_VECTOR_USER_H
#define __UM_VECTOR_USER_H

#define MAXVARGS	20

#define TOKEN_IFNAME "ifname"

#define TRANS_RAW "raw"
#define TRANS_RAW_LEN strlen(TRANS_RAW)

#define TRANS_TAP "tap"
#define TRANS_TAP_LEN strlen(TRANS_TAP)


#define TRANS_GRE "gre"
#define TRANS_GRE_LEN strlen(TRANS_RAW)

#define TRANS_L2TPV3 "l2tpv3"
#define TRANS_L2TPV3_LEN strlen(TRANS_L2TPV3)

#ifndef IPPROTO_GRE
#define IPPROTO_GRE 0x2F
#endif

#define GRE_MODE_CHECKSUM	cpu_to_be16(8 << 12)	/* checksum */
#define GRE_MODE_RESERVED	cpu_to_be16(4 << 12)	/* unused */
#define GRE_MODE_KEY		cpu_to_be16(2 << 12)	/* KEY present */
#define GRE_MODE_SEQUENCE	cpu_to_be16(1 << 12)	/* sequence */

#define GRE_IRB cpu_to_be16(0x6558)

#define L2TPV3_DATA_PACKET 0x30000

/* IANA-assigned IP protocol ID for L2TPv3 */

#ifndef IPPROTO_L2TP
#define IPPROTO_L2TP 0x73
#endif

struct arglist {
	int	numargs;
	char	*tokens[MAXVARGS];
	char	*values[MAXVARGS];
};

/* Separating read and write FDs allows us to have different
 * rx and tx method. Example - read tap via raw socket using
 * recvmmsg, write using legacy tap write calls
 */

struct vector_fds {
	int rx_fd;
	int tx_fd;
	void *remote_addr;
	int remote_addr_size;
};

#define VECTOR_READ	1
#define VECTOR_WRITE	(1 < 1)
#define VECTOR_HEADERS	(1 < 2)

extern struct arglist *uml_parse_vector_ifspec(char *arg);

extern struct vector_fds *uml_vector_user_open(
	int unit,
	struct arglist *parsed
);

extern char *uml_vector_fetch_arg(
	struct arglist *ifspec,
	char *token
);

extern int uml_vector_recvmsg(int fd, void *hdr, int flags);
extern int uml_vector_sendmsg(int fd, void *hdr, int flags);
extern int uml_vector_writev(int fd, void *hdr, int iovcount);
extern int uml_vector_sendmmsg(
	int fd, void *msgvec,
	unsigned int vlen,
	unsigned int flags
);
extern int uml_vector_recvmmsg(
	int fd,
	void *msgvec,
	unsigned int vlen,
	unsigned int flags
);
extern void *uml_vector_default_bpf(int fd, void *mac);
extern int uml_vector_attach_bpf(int fd, void *bpf, int bpf_len);
extern bool uml_raw_enable_qdisc_bypass(int fd);
extern bool uml_raw_enable_vnet_headers(int fd);
extern bool uml_tap_enable_vnet_headers(int fd);


#endif
