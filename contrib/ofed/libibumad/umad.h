/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2014 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#ifndef _UMAD_H
#define _UMAD_H

#include <stdint.h>
#include <stdlib.h>
#include <arpa/inet.h>

#ifdef _KERNEL
#include <sys/endian.h>
#include <linux/types.h> /* __be16, __be32 and __be64 */
#else
#include <infiniband/endian.h>
#include <infiniband/types.h>
#endif

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

typedef __be16 __attribute__((deprecated)) be16_t;
typedef __be32 __attribute__((deprecated)) be32_t;
typedef __be64 __attribute__((deprecated)) be64_t;

/*
 * A GID data structure that may be used in definitions of on-the-wire data
 * structures. Do not cast umad_gid pointers to ibv_gid pointers because the
 * alignment of these two data structures is different.
 */
union umad_gid {
	uint8_t		raw[16];
	__be16		raw_be16[8];
	struct {
		__be64	subnet_prefix;
		__be64	interface_id;
	} global;
} __attribute__((aligned(4))) __attribute__((packed));

#define UMAD_MAX_DEVICES 32
#define UMAD_ANY_PORT	0
typedef struct ib_mad_addr {
	__be32 qpn;
	__be32 qkey;
	__be16 lid;
	uint8_t sl;
	uint8_t path_bits;
	uint8_t grh_present;
	uint8_t gid_index;
	uint8_t hop_limit;
	uint8_t traffic_class;
	union {
		uint8_t		gid[16]; /* network-byte order */
		union umad_gid	ib_gid;
	};
	__be32 flow_label;
	uint16_t pkey_index;
	uint8_t reserved[6];
} ib_mad_addr_t;

typedef struct ib_user_mad {
	uint32_t agent_id;
	uint32_t status;
	uint32_t timeout_ms;
	uint32_t retries;
	uint32_t length;
	ib_mad_addr_t addr;
	uint8_t data[0];
} ib_user_mad_t;

#define IB_UMAD_ABI_VERSION	5
#define IB_UMAD_ABI_DIR		"/sys/class/infiniband_mad"
#define IB_UMAD_ABI_FILE	"abi_version"

#define IB_IOCTL_MAGIC		0x1b

#define IB_USER_MAD_REGISTER_AGENT \
	_IOWR(IB_IOCTL_MAGIC, 1, uint8_t [28] /* struct ib_user_mad_reg_req */)
#define IB_USER_MAD_UNREGISTER_AGENT	_IOW(IB_IOCTL_MAGIC, 2, uint32_t)
#define IB_USER_MAD_ENABLE_PKEY		_IO(IB_IOCTL_MAGIC, 3)
#define IB_USER_MAD_REGISTER_AGENT2 \
	_IOWR(IB_IOCTL_MAGIC, 4, uint8_t [40] /* struct ib_user_mad_reg_req2 */)

#define UMAD_CA_NAME_LEN	20
#define UMAD_CA_MAX_PORTS	10	/* 0 - 9 */
#define UMAD_CA_MAX_AGENTS	32

#define SYS_INFINIBAND		"/sys/class/infiniband"

#define SYS_INFINIBAND_MAD	"/sys/class/infiniband_mad"
#define SYS_IB_MAD_PORT		"port"
#define SYS_IB_MAD_DEV		"ibdev"

#define UMAD_MAX_PORTS		64

#define UMAD_DEV_DIR		"/dev"

#define SYS_CA_PORTS_DIR	"ports"

#define SYS_NODE_TYPE		"node_type"
#define SYS_CA_FW_VERS		"fw_ver"
#define SYS_CA_HW_VERS		"hw_rev"
#define SYS_CA_TYPE		"hca_type"
#define SYS_CA_NODE_GUID	"node_guid"
#define SYS_CA_SYS_GUID		"sys_image_guid"

#define SYS_PORT_LMC		"lid_mask_count"
#define SYS_PORT_SMLID		"sm_lid"
#define SYS_PORT_SMSL		"sm_sl"
#define SYS_PORT_LID		"lid"
#define SYS_PORT_STATE		"state"
#define SYS_PORT_PHY_STATE	"phys_state"
#define SYS_PORT_CAPMASK	"cap_mask"
#define SYS_PORT_RATE		"rate"
#define SYS_PORT_GUID		"port_guid"
#define SYS_PORT_GID		"gids/0"
#define SYS_PORT_LINK_LAYER	"link_layer"

typedef struct umad_port {
	char ca_name[UMAD_CA_NAME_LEN];
	int portnum;
	unsigned base_lid;
	unsigned lmc;
	unsigned sm_lid;
	unsigned sm_sl;
	unsigned state;
	unsigned phys_state;
	unsigned rate;
	__be32 capmask;
	__be64	 gid_prefix;
	__be64	 port_guid;
	unsigned pkeys_size;
	uint16_t *pkeys;
	char link_layer[UMAD_CA_NAME_LEN];
} umad_port_t;

typedef struct umad_ca {
	char ca_name[UMAD_CA_NAME_LEN];
	unsigned node_type;
	int numports;
	char fw_ver[20];
	char ca_type[40];
	char hw_ver[20];
	__be64 node_guid;
	__be64 system_guid;
	umad_port_t *ports[UMAD_CA_MAX_PORTS];
} umad_ca_t;

int umad_init(void);
int umad_done(void);

int umad_get_cas_names(char cas[][UMAD_CA_NAME_LEN], int max);
int umad_get_ca_portguids(const char *ca_name, __be64 *portguids, int max);

int umad_get_ca(const char *ca_name, umad_ca_t * ca);
int umad_release_ca(umad_ca_t * ca);
int umad_get_port(const char *ca_name, int portnum, umad_port_t * port);
int umad_release_port(umad_port_t * port);

int umad_get_issm_path(const char *ca_name, int portnum, char path[], int max);

int umad_open_port(const char *ca_name, int portnum);
int umad_close_port(int portid);

void *umad_get_mad(void *umad);
size_t umad_size(void);
int umad_status(void *umad);

ib_mad_addr_t *umad_get_mad_addr(void *umad);
int umad_set_grh_net(void *umad, void *mad_addr);
int umad_set_grh(void *umad, void *mad_addr);
int umad_set_addr_net(void *umad, __be16 dlid, __be32 dqp, int sl, __be32 qkey);
int umad_set_addr(void *umad, int dlid, int dqp, int sl, int qkey);
int umad_set_pkey(void *umad, int pkey_index);
int umad_get_pkey(void *umad);

int umad_send(int portid, int agentid, void *umad, int length,
	      int timeout_ms, int retries);
int umad_recv(int portid, void *umad, int *length, int timeout_ms);
int umad_poll(int portid, int timeout_ms);
int umad_get_fd(int portid);

int umad_register(int portid, int mgmt_class, int mgmt_version,
		  uint8_t rmpp_version, long method_mask[16 / sizeof(long)]);
int umad_register_oui(int portid, int mgmt_class, uint8_t rmpp_version,
		      uint8_t oui[3], long method_mask[16 / sizeof(long)]);
int umad_unregister(int portid, int agentid);

enum {
	UMAD_USER_RMPP = (1 << 0)
};

struct umad_reg_attr {
	uint8_t    mgmt_class;
	uint8_t    mgmt_class_version;
	uint32_t   flags;
	uint64_t   method_mask[2];
	uint32_t   oui;
	uint8_t    rmpp_version;
};

int umad_register2(int port_fd, struct umad_reg_attr *attr,
		   uint32_t *agent_id);

int umad_debug(int level);
void umad_addr_dump(ib_mad_addr_t * addr);
void umad_dump(void *umad);

static inline void *umad_alloc(int num, size_t size)
{				/* alloc array of umad buffers */
	return calloc(num, size);
}

static inline void umad_free(void *umad)
{
	free(umad);
}

/* Users should use the glibc functions directly, not these wrappers */
#ifndef ntohll
#undef ntohll
static inline __attribute__((deprecated)) uint64_t ntohll(uint64_t x) { return be64toh(x); }
#define ntohll ntohll
#endif
#ifndef htonll
#undef htonll
static inline __attribute__((deprecated)) uint64_t htonll(uint64_t x) { return htobe64(x); }
#define htonll htonll
#endif

END_C_DECLS
#endif				/* _UMAD_H */
