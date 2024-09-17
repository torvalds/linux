/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */

#ifndef __FUN_HCI_H
#define __FUN_HCI_H

enum {
	FUN_HCI_ID_INVALID = 0xffffffff,
};

enum fun_admin_op {
	FUN_ADMIN_OP_BIND = 0x1,
	FUN_ADMIN_OP_EPCQ = 0x11,
	FUN_ADMIN_OP_EPSQ = 0x12,
	FUN_ADMIN_OP_PORT = 0x13,
	FUN_ADMIN_OP_ETH = 0x14,
	FUN_ADMIN_OP_VI = 0x15,
	FUN_ADMIN_OP_SWUPGRADE = 0x1f,
	FUN_ADMIN_OP_RSS = 0x21,
	FUN_ADMIN_OP_ADI = 0x25,
	FUN_ADMIN_OP_KTLS = 0x26,
};

enum {
	FUN_REQ_COMMON_FLAG_RSP = 0x1,
	FUN_REQ_COMMON_FLAG_HEAD_WB = 0x2,
	FUN_REQ_COMMON_FLAG_INT = 0x4,
	FUN_REQ_COMMON_FLAG_CQE_IN_RQBUF = 0x8,
};

struct fun_admin_req_common {
	__u8 op;
	__u8 len8;
	__be16 flags;
	__u8 suboff8;
	__u8 rsvd0;
	__be16 cid;
};

#define FUN_ADMIN_REQ_COMMON_INIT(_op, _len8, _flags, _suboff8, _cid)       \
	(struct fun_admin_req_common) {                                     \
		.op = (_op), .len8 = (_len8), .flags = cpu_to_be16(_flags), \
		.suboff8 = (_suboff8), .cid = cpu_to_be16(_cid),            \
	}

#define FUN_ADMIN_REQ_COMMON_INIT2(_op, _len)    \
	(struct fun_admin_req_common) {          \
		.op = (_op), .len8 = (_len) / 8, \
	}

struct fun_admin_rsp_common {
	__u8 op;
	__u8 len8;
	__be16 flags;
	__u8 suboff8;
	__u8 ret;
	__be16 cid;
};

struct fun_admin_write48_req {
	__be64 key_to_data;
};

#define FUN_ADMIN_WRITE48_REQ_KEY_S 56U
#define FUN_ADMIN_WRITE48_REQ_KEY_M 0xff
#define FUN_ADMIN_WRITE48_REQ_KEY_P_NOSWAP(x) \
	(((__u64)x) << FUN_ADMIN_WRITE48_REQ_KEY_S)

#define FUN_ADMIN_WRITE48_REQ_DATA_S 0U
#define FUN_ADMIN_WRITE48_REQ_DATA_M 0xffffffffffff
#define FUN_ADMIN_WRITE48_REQ_DATA_P_NOSWAP(x) \
	(((__u64)x) << FUN_ADMIN_WRITE48_REQ_DATA_S)

#define FUN_ADMIN_WRITE48_REQ_INIT(key, data)                       \
	(struct fun_admin_write48_req) {                            \
		.key_to_data = cpu_to_be64(                         \
			FUN_ADMIN_WRITE48_REQ_KEY_P_NOSWAP(key) |   \
			FUN_ADMIN_WRITE48_REQ_DATA_P_NOSWAP(data)), \
	}

struct fun_admin_write48_rsp {
	__be64 key_to_data;
};

struct fun_admin_read48_req {
	__be64 key_pack;
};

#define FUN_ADMIN_READ48_REQ_KEY_S 56U
#define FUN_ADMIN_READ48_REQ_KEY_M 0xff
#define FUN_ADMIN_READ48_REQ_KEY_P_NOSWAP(x) \
	(((__u64)x) << FUN_ADMIN_READ48_REQ_KEY_S)

#define FUN_ADMIN_READ48_REQ_INIT(key)                                       \
	(struct fun_admin_read48_req) {                                      \
		.key_pack =                                                  \
			cpu_to_be64(FUN_ADMIN_READ48_REQ_KEY_P_NOSWAP(key)), \
	}

struct fun_admin_read48_rsp {
	__be64 key_to_data;
};

#define FUN_ADMIN_READ48_RSP_KEY_S 56U
#define FUN_ADMIN_READ48_RSP_KEY_M 0xff
#define FUN_ADMIN_READ48_RSP_KEY_G(x)                     \
	((be64_to_cpu(x) >> FUN_ADMIN_READ48_RSP_KEY_S) & \
	 FUN_ADMIN_READ48_RSP_KEY_M)

#define FUN_ADMIN_READ48_RSP_RET_S 48U
#define FUN_ADMIN_READ48_RSP_RET_M 0xff
#define FUN_ADMIN_READ48_RSP_RET_G(x)                     \
	((be64_to_cpu(x) >> FUN_ADMIN_READ48_RSP_RET_S) & \
	 FUN_ADMIN_READ48_RSP_RET_M)

#define FUN_ADMIN_READ48_RSP_DATA_S 0U
#define FUN_ADMIN_READ48_RSP_DATA_M 0xffffffffffff
#define FUN_ADMIN_READ48_RSP_DATA_G(x)                     \
	((be64_to_cpu(x) >> FUN_ADMIN_READ48_RSP_DATA_S) & \
	 FUN_ADMIN_READ48_RSP_DATA_M)

enum fun_admin_bind_type {
	FUN_ADMIN_BIND_TYPE_EPCQ = 0x1,
	FUN_ADMIN_BIND_TYPE_EPSQ = 0x2,
	FUN_ADMIN_BIND_TYPE_PORT = 0x3,
	FUN_ADMIN_BIND_TYPE_RSS = 0x4,
	FUN_ADMIN_BIND_TYPE_VI = 0x5,
	FUN_ADMIN_BIND_TYPE_ETH = 0x6,
};

struct fun_admin_bind_entry {
	__u8 type;
	__u8 rsvd0[3];
	__be32 id;
};

#define FUN_ADMIN_BIND_ENTRY_INIT(_type, _id)            \
	(struct fun_admin_bind_entry) {                  \
		.type = (_type), .id = cpu_to_be32(_id), \
	}

struct fun_admin_bind_req {
	struct fun_admin_req_common common;
	struct fun_admin_bind_entry entry[];
};

struct fun_admin_bind_rsp {
	struct fun_admin_rsp_common bind_rsp_common;
};

struct fun_admin_simple_subop {
	__u8 subop;
	__u8 rsvd0;
	__be16 flags;
	__be32 data;
};

#define FUN_ADMIN_SIMPLE_SUBOP_INIT(_subop, _flags, _data)       \
	(struct fun_admin_simple_subop) {                        \
		.subop = (_subop), .flags = cpu_to_be16(_flags), \
		.data = cpu_to_be32(_data),                      \
	}

enum fun_admin_subop {
	FUN_ADMIN_SUBOP_CREATE = 0x10,
	FUN_ADMIN_SUBOP_DESTROY = 0x11,
	FUN_ADMIN_SUBOP_MODIFY = 0x12,
	FUN_ADMIN_SUBOP_RES_COUNT = 0x14,
	FUN_ADMIN_SUBOP_READ = 0x15,
	FUN_ADMIN_SUBOP_WRITE = 0x16,
	FUN_ADMIN_SUBOP_NOTIFY = 0x17,
};

enum {
	FUN_ADMIN_RES_CREATE_FLAG_ALLOCATOR = 0x1,
};

struct fun_admin_generic_destroy_req {
	struct fun_admin_req_common common;
	struct fun_admin_simple_subop destroy;
};

struct fun_admin_generic_create_rsp {
	struct fun_admin_rsp_common common;

	__u8 subop;
	__u8 rsvd0;
	__be16 flags;
	__be32 id;
};

struct fun_admin_res_count_req {
	struct fun_admin_req_common common;
	struct fun_admin_simple_subop count;
};

struct fun_admin_res_count_rsp {
	struct fun_admin_rsp_common common;
	struct fun_admin_simple_subop count;
};

enum {
	FUN_ADMIN_EPCQ_CREATE_FLAG_INT_EPCQ = 0x2,
	FUN_ADMIN_EPCQ_CREATE_FLAG_ENTRY_WR_TPH = 0x4,
	FUN_ADMIN_EPCQ_CREATE_FLAG_SL_WR_TPH = 0x8,
	FUN_ADMIN_EPCQ_CREATE_FLAG_RQ = 0x80,
	FUN_ADMIN_EPCQ_CREATE_FLAG_INT_IQ = 0x100,
	FUN_ADMIN_EPCQ_CREATE_FLAG_INT_NOARM = 0x200,
	FUN_ADMIN_EPCQ_CREATE_FLAG_DROP_ON_OVERFLOW = 0x400,
};

struct fun_admin_epcq_req {
	struct fun_admin_req_common common;
	union epcq_req_subop {
		struct fun_admin_epcq_create_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;

			__be32 epsqid;
			__u8 rsvd1;
			__u8 entry_size_log2;
			__be16 nentries;

			__be64 address;

			__be16 tailroom; /* per packet tailroom in bytes */
			__u8 headroom; /* per packet headroom in 2B units */
			__u8 intcoal_kbytes;
			__u8 intcoal_holdoff_nentries;
			__u8 intcoal_holdoff_usecs;
			__be16 intid;

			__be32 scan_start_id;
			__be32 scan_end_id;

			__be16 tph_cpuid;
			__u8 rsvd3[6];
		} create;

		struct fun_admin_epcq_modify_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;

			__be16 headroom; /* headroom in bytes */
			__u8 rsvd1[6];
		} modify;
	} u;
};

#define FUN_ADMIN_EPCQ_CREATE_REQ_INIT(                                      \
	_subop, _flags, _id, _epsqid, _entry_size_log2, _nentries, _address, \
	_tailroom, _headroom, _intcoal_kbytes, _intcoal_holdoff_nentries,    \
	_intcoal_holdoff_usecs, _intid, _scan_start_id, _scan_end_id,        \
	_tph_cpuid)                                                          \
	(struct fun_admin_epcq_create_req) {                                 \
		.subop = (_subop), .flags = cpu_to_be16(_flags),             \
		.id = cpu_to_be32(_id), .epsqid = cpu_to_be32(_epsqid),      \
		.entry_size_log2 = _entry_size_log2,                         \
		.nentries = cpu_to_be16(_nentries),                          \
		.address = cpu_to_be64(_address),                            \
		.tailroom = cpu_to_be16(_tailroom), .headroom = _headroom,   \
		.intcoal_kbytes = _intcoal_kbytes,                           \
		.intcoal_holdoff_nentries = _intcoal_holdoff_nentries,       \
		.intcoal_holdoff_usecs = _intcoal_holdoff_usecs,             \
		.intid = cpu_to_be16(_intid),                                \
		.scan_start_id = cpu_to_be32(_scan_start_id),                \
		.scan_end_id = cpu_to_be32(_scan_end_id),                    \
		.tph_cpuid = cpu_to_be16(_tph_cpuid),                        \
	}

#define FUN_ADMIN_EPCQ_MODIFY_REQ_INIT(_subop, _flags, _id, _headroom)      \
	(struct fun_admin_epcq_modify_req) {                                \
		.subop = (_subop), .flags = cpu_to_be16(_flags),            \
		.id = cpu_to_be32(_id), .headroom = cpu_to_be16(_headroom), \
	}

enum {
	FUN_ADMIN_EPSQ_CREATE_FLAG_INT_EPSQ = 0x2,
	FUN_ADMIN_EPSQ_CREATE_FLAG_ENTRY_RD_TPH = 0x4,
	FUN_ADMIN_EPSQ_CREATE_FLAG_GL_RD_TPH = 0x8,
	FUN_ADMIN_EPSQ_CREATE_FLAG_HEAD_WB_ADDRESS = 0x10,
	FUN_ADMIN_EPSQ_CREATE_FLAG_HEAD_WB_ADDRESS_TPH = 0x20,
	FUN_ADMIN_EPSQ_CREATE_FLAG_HEAD_WB_EPCQ = 0x40,
	FUN_ADMIN_EPSQ_CREATE_FLAG_RQ = 0x80,
	FUN_ADMIN_EPSQ_CREATE_FLAG_INT_IQ = 0x100,
	FUN_ADMIN_EPSQ_CREATE_FLAG_NO_CMPL = 0x200,
};

struct fun_admin_epsq_req {
	struct fun_admin_req_common common;

	union epsq_req_subop {
		struct fun_admin_epsq_create_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;

			__be32 epcqid;
			__u8 rsvd1;
			__u8 entry_size_log2;
			__be16 nentries;

			__be64 address; /* DMA address of epsq */

			__u8 rsvd2[3];
			__u8 intcoal_kbytes;
			__u8 intcoal_holdoff_nentries;
			__u8 intcoal_holdoff_usecs;
			__be16 intid;

			__be32 scan_start_id;
			__be32 scan_end_id;

			__u8 rsvd3[4];
			__be16 tph_cpuid;
			__u8 buf_size_log2; /* log2 of RQ buffer size */
			__u8 head_wb_size_log2; /* log2 of head write back size */

			__be64 head_wb_address; /* DMA address for head writeback */
		} create;
	} u;
};

#define FUN_ADMIN_EPSQ_CREATE_REQ_INIT(                                      \
	_subop, _flags, _id, _epcqid, _entry_size_log2, _nentries, _address, \
	_intcoal_kbytes, _intcoal_holdoff_nentries, _intcoal_holdoff_usecs,  \
	_intid, _scan_start_id, _scan_end_id, _tph_cpuid, _buf_size_log2,    \
	_head_wb_size_log2, _head_wb_address)                                \
	(struct fun_admin_epsq_create_req) {                                 \
		.subop = (_subop), .flags = cpu_to_be16(_flags),             \
		.id = cpu_to_be32(_id), .epcqid = cpu_to_be32(_epcqid),      \
		.entry_size_log2 = _entry_size_log2,                         \
		.nentries = cpu_to_be16(_nentries),                          \
		.address = cpu_to_be64(_address),                            \
		.intcoal_kbytes = _intcoal_kbytes,                           \
		.intcoal_holdoff_nentries = _intcoal_holdoff_nentries,       \
		.intcoal_holdoff_usecs = _intcoal_holdoff_usecs,             \
		.intid = cpu_to_be16(_intid),                                \
		.scan_start_id = cpu_to_be32(_scan_start_id),                \
		.scan_end_id = cpu_to_be32(_scan_end_id),                    \
		.tph_cpuid = cpu_to_be16(_tph_cpuid),                        \
		.buf_size_log2 = _buf_size_log2,                             \
		.head_wb_size_log2 = _head_wb_size_log2,                     \
		.head_wb_address = cpu_to_be64(_head_wb_address),            \
	}

enum {
	FUN_PORT_CAP_OFFLOADS = 0x1,
	FUN_PORT_CAP_STATS = 0x2,
	FUN_PORT_CAP_LOOPBACK = 0x4,
	FUN_PORT_CAP_VPORT = 0x8,
	FUN_PORT_CAP_TX_PAUSE = 0x10,
	FUN_PORT_CAP_RX_PAUSE = 0x20,
	FUN_PORT_CAP_AUTONEG = 0x40,
	FUN_PORT_CAP_RSS = 0x80,
	FUN_PORT_CAP_VLAN_OFFLOADS = 0x100,
	FUN_PORT_CAP_ENCAP_OFFLOADS = 0x200,
	FUN_PORT_CAP_1000_X = 0x1000,
	FUN_PORT_CAP_10G_R = 0x2000,
	FUN_PORT_CAP_40G_R4 = 0x4000,
	FUN_PORT_CAP_25G_R = 0x8000,
	FUN_PORT_CAP_50G_R2 = 0x10000,
	FUN_PORT_CAP_50G_R = 0x20000,
	FUN_PORT_CAP_100G_R4 = 0x40000,
	FUN_PORT_CAP_100G_R2 = 0x80000,
	FUN_PORT_CAP_200G_R4 = 0x100000,
	FUN_PORT_CAP_FEC_NONE = 0x10000000,
	FUN_PORT_CAP_FEC_FC = 0x20000000,
	FUN_PORT_CAP_FEC_RS = 0x40000000,
};

enum fun_port_brkout_mode {
	FUN_PORT_BRKMODE_NA = 0x0,
	FUN_PORT_BRKMODE_NONE = 0x1,
	FUN_PORT_BRKMODE_2X = 0x2,
	FUN_PORT_BRKMODE_4X = 0x3,
};

enum {
	FUN_PORT_SPEED_AUTO = 0x0,
	FUN_PORT_SPEED_10M = 0x1,
	FUN_PORT_SPEED_100M = 0x2,
	FUN_PORT_SPEED_1G = 0x4,
	FUN_PORT_SPEED_10G = 0x8,
	FUN_PORT_SPEED_25G = 0x10,
	FUN_PORT_SPEED_40G = 0x20,
	FUN_PORT_SPEED_50G = 0x40,
	FUN_PORT_SPEED_100G = 0x80,
	FUN_PORT_SPEED_200G = 0x100,
};

enum fun_port_duplex_mode {
	FUN_PORT_FULL_DUPLEX = 0x0,
	FUN_PORT_HALF_DUPLEX = 0x1,
};

enum {
	FUN_PORT_FEC_NA = 0x0,
	FUN_PORT_FEC_OFF = 0x1,
	FUN_PORT_FEC_RS = 0x2,
	FUN_PORT_FEC_FC = 0x4,
	FUN_PORT_FEC_AUTO = 0x8,
};

enum fun_port_link_status {
	FUN_PORT_LINK_UP = 0x0,
	FUN_PORT_LINK_UP_WITH_ERR = 0x1,
	FUN_PORT_LINK_DOWN = 0x2,
};

enum fun_port_led_type {
	FUN_PORT_LED_OFF = 0x0,
	FUN_PORT_LED_AMBER = 0x1,
	FUN_PORT_LED_GREEN = 0x2,
	FUN_PORT_LED_BEACON_ON = 0x3,
	FUN_PORT_LED_BEACON_OFF = 0x4,
};

enum {
	FUN_PORT_FLAG_MAC_DOWN = 0x1,
	FUN_PORT_FLAG_MAC_UP = 0x2,
	FUN_PORT_FLAG_NH_DOWN = 0x4,
	FUN_PORT_FLAG_NH_UP = 0x8,
};

enum {
	FUN_PORT_FLAG_ENABLE_NOTIFY = 0x1,
};

enum fun_port_lane_attr {
	FUN_PORT_LANE_1 = 0x1,
	FUN_PORT_LANE_2 = 0x2,
	FUN_PORT_LANE_4 = 0x4,
	FUN_PORT_LANE_SPEED_10G = 0x100,
	FUN_PORT_LANE_SPEED_25G = 0x200,
	FUN_PORT_LANE_SPEED_50G = 0x400,
	FUN_PORT_LANE_SPLIT = 0x8000,
};

enum fun_admin_port_subop {
	FUN_ADMIN_PORT_SUBOP_XCVR_READ = 0x23,
	FUN_ADMIN_PORT_SUBOP_INETADDR_EVENT = 0x24,
};

enum fun_admin_port_key {
	FUN_ADMIN_PORT_KEY_ILLEGAL = 0x0,
	FUN_ADMIN_PORT_KEY_MTU = 0x1,
	FUN_ADMIN_PORT_KEY_FEC = 0x2,
	FUN_ADMIN_PORT_KEY_SPEED = 0x3,
	FUN_ADMIN_PORT_KEY_DEBOUNCE = 0x4,
	FUN_ADMIN_PORT_KEY_DUPLEX = 0x5,
	FUN_ADMIN_PORT_KEY_MACADDR = 0x6,
	FUN_ADMIN_PORT_KEY_LINKMODE = 0x7,
	FUN_ADMIN_PORT_KEY_BREAKOUT = 0x8,
	FUN_ADMIN_PORT_KEY_ENABLE = 0x9,
	FUN_ADMIN_PORT_KEY_DISABLE = 0xa,
	FUN_ADMIN_PORT_KEY_ERR_DISABLE = 0xb,
	FUN_ADMIN_PORT_KEY_CAPABILITIES = 0xc,
	FUN_ADMIN_PORT_KEY_LP_CAPABILITIES = 0xd,
	FUN_ADMIN_PORT_KEY_STATS_DMA_LOW = 0xe,
	FUN_ADMIN_PORT_KEY_STATS_DMA_HIGH = 0xf,
	FUN_ADMIN_PORT_KEY_LANE_ATTRS = 0x10,
	FUN_ADMIN_PORT_KEY_LED = 0x11,
	FUN_ADMIN_PORT_KEY_ADVERT = 0x12,
};

struct fun_subop_imm {
	__u8 subop; /* see fun_data_subop enum */
	__u8 flags;
	__u8 nsgl;
	__u8 rsvd0;
	__be32 len;

	__u8 data[];
};

enum fun_subop_sgl_flags {
	FUN_SUBOP_SGL_USE_OFF8 = 0x1,
	FUN_SUBOP_FLAG_FREE_BUF = 0x2,
	FUN_SUBOP_FLAG_IS_REFBUF = 0x4,
	FUN_SUBOP_SGL_FLAG_LOCAL = 0x8,
};

enum fun_data_op {
	FUN_DATAOP_INVALID = 0x0,
	FUN_DATAOP_SL = 0x1, /* scatter */
	FUN_DATAOP_GL = 0x2, /* gather */
	FUN_DATAOP_SGL = 0x3, /* scatter-gather */
	FUN_DATAOP_IMM = 0x4, /* immediate data */
	FUN_DATAOP_RQBUF = 0x8, /* rq buffer */
};

struct fun_dataop_gl {
	__u8 subop;
	__u8 flags;
	__be16 sgl_off;
	__be32 sgl_len;

	__be64 sgl_data;
};

static inline void fun_dataop_gl_init(struct fun_dataop_gl *s, u8 flags,
				      u16 sgl_off, u32 sgl_len, u64 sgl_data)
{
	s->subop = FUN_DATAOP_GL;
	s->flags = flags;
	s->sgl_off = cpu_to_be16(sgl_off);
	s->sgl_len = cpu_to_be32(sgl_len);
	s->sgl_data = cpu_to_be64(sgl_data);
}

struct fun_dataop_imm {
	__u8 subop;
	__u8 flags;
	__be16 rsvd0;
	__be32 sgl_len;
};

struct fun_subop_sgl {
	__u8 subop;
	__u8 flags;
	__u8 nsgl;
	__u8 rsvd0;
	__be32 sgl_len;

	__be64 sgl_data;
};

#define FUN_SUBOP_SGL_INIT(_subop, _flags, _nsgl, _sgl_len, _sgl_data) \
	(struct fun_subop_sgl) {                                       \
		.subop = (_subop), .flags = (_flags), .nsgl = (_nsgl), \
		.sgl_len = cpu_to_be32(_sgl_len),                      \
		.sgl_data = cpu_to_be64(_sgl_data),                    \
	}

struct fun_dataop_rqbuf {
	__u8 subop;
	__u8 rsvd0;
	__be16 cid;
	__be32 bufoff;
};

struct fun_dataop_hdr {
	__u8 nsgl;
	__u8 flags;
	__u8 ngather;
	__u8 nscatter;
	__be32 total_len;

	struct fun_dataop_imm imm[];
};

#define FUN_DATAOP_HDR_INIT(_nsgl, _flags, _ngather, _nscatter, _total_len)  \
	(struct fun_dataop_hdr) {                                            \
		.nsgl = _nsgl, .flags = _flags, .ngather = _ngather,         \
		.nscatter = _nscatter, .total_len = cpu_to_be32(_total_len), \
	}

enum fun_port_inetaddr_event_type {
	FUN_PORT_INETADDR_ADD = 0x1,
	FUN_PORT_INETADDR_DEL = 0x2,
};

enum fun_port_inetaddr_addr_family {
	FUN_PORT_INETADDR_IPV4 = 0x1,
	FUN_PORT_INETADDR_IPV6 = 0x2,
};

struct fun_admin_port_req {
	struct fun_admin_req_common common;

	union port_req_subop {
		struct fun_admin_port_create_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;
		} create;
		struct fun_admin_port_write_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id; /* portid */

			struct fun_admin_write48_req write48[];
		} write;
		struct fun_admin_port_read_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id; /* portid */

			struct fun_admin_read48_req read48[];
		} read;
		struct fun_admin_port_xcvr_read_req {
			u8 subop;
			u8 rsvd0;
			__be16 flags;
			__be32 id;

			u8 bank;
			u8 page;
			u8 offset;
			u8 length;
			u8 dev_addr;
			u8 rsvd1[3];
		} xcvr_read;
		struct fun_admin_port_inetaddr_event_req {
			__u8 subop;
			__u8 rsvd0;
			__u8 event_type;
			__u8 addr_family;
			__be32 id;

			__u8 addr[];
		} inetaddr_event;
	} u;
};

#define FUN_ADMIN_PORT_CREATE_REQ_INIT(_subop, _flags, _id)      \
	(struct fun_admin_port_create_req) {                     \
		.subop = (_subop), .flags = cpu_to_be16(_flags), \
		.id = cpu_to_be32(_id),                          \
	}

#define FUN_ADMIN_PORT_WRITE_REQ_INIT(_subop, _flags, _id)       \
	(struct fun_admin_port_write_req) {                      \
		.subop = (_subop), .flags = cpu_to_be16(_flags), \
		.id = cpu_to_be32(_id),                          \
	}

#define FUN_ADMIN_PORT_READ_REQ_INIT(_subop, _flags, _id)        \
	(struct fun_admin_port_read_req) {                       \
		.subop = (_subop), .flags = cpu_to_be16(_flags), \
		.id = cpu_to_be32(_id),                          \
	}

#define FUN_ADMIN_PORT_XCVR_READ_REQ_INIT(_flags, _id, _bank, _page,   \
					  _offset, _length, _dev_addr) \
	((struct fun_admin_port_xcvr_read_req) {                       \
		.subop = FUN_ADMIN_PORT_SUBOP_XCVR_READ,               \
		.flags = cpu_to_be16(_flags), .id = cpu_to_be32(_id),  \
		.bank = (_bank), .page = (_page), .offset = (_offset), \
		.length = (_length), .dev_addr = (_dev_addr),          \
	})

struct fun_admin_port_rsp {
	struct fun_admin_rsp_common common;

	union port_rsp_subop {
		struct fun_admin_port_create_rsp {
			__u8 subop;
			__u8 rsvd0[3];
			__be32 id;

			__be16 lport;
			__u8 rsvd1[6];
		} create;
		struct fun_admin_port_write_rsp {
			__u8 subop;
			__u8 rsvd0[3];
			__be32 id; /* portid */

			struct fun_admin_write48_rsp write48[];
		} write;
		struct fun_admin_port_read_rsp {
			__u8 subop;
			__u8 rsvd0[3];
			__be32 id; /* portid */

			struct fun_admin_read48_rsp read48[];
		} read;
		struct fun_admin_port_inetaddr_event_rsp {
			__u8 subop;
			__u8 rsvd0[3];
			__be32 id; /* portid */
		} inetaddr_event;
	} u;
};

struct fun_admin_port_xcvr_read_rsp {
	struct fun_admin_rsp_common common;

	u8 subop;
	u8 rsvd0[3];
	__be32 id;

	u8 bank;
	u8 page;
	u8 offset;
	u8 length;
	u8 dev_addr;
	u8 rsvd1[3];

	u8 data[128];
};

enum fun_xcvr_type {
	FUN_XCVR_BASET = 0x0,
	FUN_XCVR_CU = 0x1,
	FUN_XCVR_SMF = 0x2,
	FUN_XCVR_MMF = 0x3,
	FUN_XCVR_AOC = 0x4,
	FUN_XCVR_SFPP = 0x10, /* SFP+ or later */
	FUN_XCVR_QSFPP = 0x11, /* QSFP+ or later */
	FUN_XCVR_QSFPDD = 0x12, /* QSFP-DD */
};

struct fun_admin_port_notif {
	struct fun_admin_rsp_common common;

	__u8 subop;
	__u8 rsvd0;
	__be16 id;
	__be32 speed; /* in 10 Mbps units */

	__u8 link_state;
	__u8 missed_events;
	__u8 link_down_reason;
	__u8 xcvr_type;
	__u8 flow_ctrl;
	__u8 fec;
	__u8 active_lanes;
	__u8 rsvd1;

	__be64 advertising;

	__be64 lp_advertising;
};

enum fun_eth_rss_const {
	FUN_ETH_RSS_MAX_KEY_SIZE = 0x28,
	FUN_ETH_RSS_MAX_INDIR_ENT = 0x40,
};

enum fun_eth_hash_alg {
	FUN_ETH_RSS_ALG_INVALID = 0x0,
	FUN_ETH_RSS_ALG_TOEPLITZ = 0x1,
	FUN_ETH_RSS_ALG_CRC32 = 0x2,
};

struct fun_admin_rss_req {
	struct fun_admin_req_common common;

	union rss_req_subop {
		struct fun_admin_rss_create_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;

			__be32 rsvd1;
			__be32 viid; /* VI flow id */

			__be64 metadata[1];

			__u8 alg;
			__u8 keylen;
			__u8 indir_nent;
			__u8 rsvd2;
			__be16 key_off;
			__be16 indir_off;

			struct fun_dataop_hdr dataop;
		} create;
	} u;
};

#define FUN_ADMIN_RSS_CREATE_REQ_INIT(_subop, _flags, _id, _viid, _alg,    \
				      _keylen, _indir_nent, _key_off,      \
				      _indir_off)                          \
	(struct fun_admin_rss_create_req) {                                \
		.subop = (_subop), .flags = cpu_to_be16(_flags),           \
		.id = cpu_to_be32(_id), .viid = cpu_to_be32(_viid),        \
		.alg = _alg, .keylen = _keylen, .indir_nent = _indir_nent, \
		.key_off = cpu_to_be16(_key_off),                          \
		.indir_off = cpu_to_be16(_indir_off),                      \
	}

struct fun_admin_vi_req {
	struct fun_admin_req_common common;

	union vi_req_subop {
		struct fun_admin_vi_create_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;

			__be32 rsvd1;
			__be32 portid; /* port flow id */
		} create;
	} u;
};

#define FUN_ADMIN_VI_CREATE_REQ_INIT(_subop, _flags, _id, _portid)      \
	(struct fun_admin_vi_create_req) {                              \
		.subop = (_subop), .flags = cpu_to_be16(_flags),        \
		.id = cpu_to_be32(_id), .portid = cpu_to_be32(_portid), \
	}

struct fun_admin_eth_req {
	struct fun_admin_req_common common;

	union eth_req_subop {
		struct fun_admin_eth_create_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;

			__be32 rsvd1;
			__be32 portid; /* port flow id */
		} create;
	} u;
};

#define FUN_ADMIN_ETH_CREATE_REQ_INIT(_subop, _flags, _id, _portid)     \
	(struct fun_admin_eth_create_req) {                             \
		.subop = (_subop), .flags = cpu_to_be16(_flags),        \
		.id = cpu_to_be32(_id), .portid = cpu_to_be32(_portid), \
	}

enum {
	FUN_ADMIN_SWU_UPGRADE_FLAG_INIT = 0x10,
	FUN_ADMIN_SWU_UPGRADE_FLAG_COMPLETE = 0x20,
	FUN_ADMIN_SWU_UPGRADE_FLAG_DOWNGRADE = 0x40,
	FUN_ADMIN_SWU_UPGRADE_FLAG_ACTIVE_IMAGE = 0x80,
	FUN_ADMIN_SWU_UPGRADE_FLAG_ASYNC = 0x1,
};

enum fun_admin_swu_subop {
	FUN_ADMIN_SWU_SUBOP_GET_VERSION = 0x20,
	FUN_ADMIN_SWU_SUBOP_UPGRADE = 0x21,
	FUN_ADMIN_SWU_SUBOP_UPGRADE_DATA = 0x22,
	FUN_ADMIN_SWU_SUBOP_GET_ALL_VERSIONS = 0x23,
};

struct fun_admin_swu_req {
	struct fun_admin_req_common common;

	union swu_req_subop {
		struct fun_admin_swu_create_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;
		} create;
		struct fun_admin_swu_upgrade_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;

			__be32 fourcc;
			__be32 rsvd1;

			__be64 image_size; /* upgrade image length */
		} upgrade;
		struct fun_admin_swu_upgrade_data_req {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;

			__be32 offset; /* offset of data in this command */
			__be32 size; /* total size of data in this command */
		} upgrade_data;
	} u;

	struct fun_subop_sgl sgl[]; /* in, out buffers through sgl */
};

#define FUN_ADMIN_SWU_CREATE_REQ_INIT(_subop, _flags, _id)       \
	(struct fun_admin_swu_create_req) {                      \
		.subop = (_subop), .flags = cpu_to_be16(_flags), \
		.id = cpu_to_be32(_id),                          \
	}

#define FUN_ADMIN_SWU_UPGRADE_REQ_INIT(_subop, _flags, _id, _fourcc,    \
				       _image_size)                     \
	(struct fun_admin_swu_upgrade_req) {                            \
		.subop = (_subop), .flags = cpu_to_be16(_flags),        \
		.id = cpu_to_be32(_id), .fourcc = cpu_to_be32(_fourcc), \
		.image_size = cpu_to_be64(_image_size),                 \
	}

#define FUN_ADMIN_SWU_UPGRADE_DATA_REQ_INIT(_subop, _flags, _id, _offset, \
					    _size)                        \
	(struct fun_admin_swu_upgrade_data_req) {                         \
		.subop = (_subop), .flags = cpu_to_be16(_flags),          \
		.id = cpu_to_be32(_id), .offset = cpu_to_be32(_offset),   \
		.size = cpu_to_be32(_size),                               \
	}

struct fun_admin_swu_rsp {
	struct fun_admin_rsp_common common;

	union swu_rsp_subop {
		struct fun_admin_swu_create_rsp {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;
		} create;
		struct fun_admin_swu_upgrade_rsp {
			__u8 subop;
			__u8 rsvd0[3];
			__be32 id;

			__be32 fourcc;
			__be32 status;

			__be32 progress;
			__be32 unused;
		} upgrade;
		struct fun_admin_swu_upgrade_data_rsp {
			__u8 subop;
			__u8 rsvd0;
			__be16 flags;
			__be32 id;

			__be32 offset;
			__be32 size;
		} upgrade_data;
	} u;
};

enum fun_ktls_version {
	FUN_KTLS_TLSV2 = 0x20,
	FUN_KTLS_TLSV3 = 0x30,
};

enum fun_ktls_cipher {
	FUN_KTLS_CIPHER_AES_GCM_128 = 0x33,
	FUN_KTLS_CIPHER_AES_GCM_256 = 0x34,
	FUN_KTLS_CIPHER_AES_CCM_128 = 0x35,
	FUN_KTLS_CIPHER_CHACHA20_POLY1305 = 0x36,
};

enum fun_ktls_modify_flags {
	FUN_KTLS_MODIFY_REMOVE = 0x1,
};

struct fun_admin_ktls_create_req {
	struct fun_admin_req_common common;

	__u8 subop;
	__u8 rsvd0;
	__be16 flags;
	__be32 id;
};

#define FUN_ADMIN_KTLS_CREATE_REQ_INIT(_subop, _flags, _id)      \
	(struct fun_admin_ktls_create_req) {                     \
		.subop = (_subop), .flags = cpu_to_be16(_flags), \
		.id = cpu_to_be32(_id),                          \
	}

struct fun_admin_ktls_create_rsp {
	struct fun_admin_rsp_common common;

	__u8 subop;
	__u8 rsvd0[3];
	__be32 id;
};

struct fun_admin_ktls_modify_req {
	struct fun_admin_req_common common;

	__u8 subop;
	__u8 rsvd0;
	__be16 flags;
	__be32 id;

	__be64 tlsid;

	__be32 tcp_seq;
	__u8 version;
	__u8 cipher;
	__u8 rsvd1[2];

	__u8 record_seq[8];

	__u8 key[32];

	__u8 iv[16];

	__u8 salt[8];
};

#define FUN_ADMIN_KTLS_MODIFY_REQ_INIT(_subop, _flags, _id, _tlsid, _tcp_seq, \
				       _version, _cipher)                     \
	(struct fun_admin_ktls_modify_req) {                                  \
		.subop = (_subop), .flags = cpu_to_be16(_flags),              \
		.id = cpu_to_be32(_id), .tlsid = cpu_to_be64(_tlsid),         \
		.tcp_seq = cpu_to_be32(_tcp_seq), .version = _version,        \
		.cipher = _cipher,                                            \
	}

struct fun_admin_ktls_modify_rsp {
	struct fun_admin_rsp_common common;

	__u8 subop;
	__u8 rsvd0[3];
	__be32 id;

	__be64 tlsid;
};

struct fun_req_common {
	__u8 op;
	__u8 len8;
	__be16 flags;
	__u8 suboff8;
	__u8 rsvd0;
	__be16 cid;
};

struct fun_rsp_common {
	__u8 op;
	__u8 len8;
	__be16 flags;
	__u8 suboff8;
	__u8 ret;
	__be16 cid;
};

struct fun_cqe_info {
	__be16 sqhd;
	__be16 sqid;
	__be16 cid;
	__be16 sf_p;
};

enum fun_eprq_def {
	FUN_EPRQ_PKT_ALIGN = 0x80,
};

struct fun_eprq_rqbuf {
	__be64 bufaddr;
};

#define FUN_EPRQ_RQBUF_INIT(_bufaddr)             \
	(struct fun_eprq_rqbuf) {                 \
		.bufaddr = cpu_to_be64(_bufaddr), \
	}

enum fun_eth_op {
	FUN_ETH_OP_TX = 0x1,
	FUN_ETH_OP_RX = 0x2,
};

enum {
	FUN_ETH_OFFLOAD_EN = 0x8000,
	FUN_ETH_OUTER_EN = 0x4000,
	FUN_ETH_INNER_LSO = 0x2000,
	FUN_ETH_INNER_TSO = 0x1000,
	FUN_ETH_OUTER_IPV6 = 0x800,
	FUN_ETH_OUTER_UDP = 0x400,
	FUN_ETH_INNER_IPV6 = 0x200,
	FUN_ETH_INNER_UDP = 0x100,
	FUN_ETH_UPDATE_OUTER_L3_LEN = 0x80,
	FUN_ETH_UPDATE_OUTER_L3_CKSUM = 0x40,
	FUN_ETH_UPDATE_OUTER_L4_LEN = 0x20,
	FUN_ETH_UPDATE_OUTER_L4_CKSUM = 0x10,
	FUN_ETH_UPDATE_INNER_L3_LEN = 0x8,
	FUN_ETH_UPDATE_INNER_L3_CKSUM = 0x4,
	FUN_ETH_UPDATE_INNER_L4_LEN = 0x2,
	FUN_ETH_UPDATE_INNER_L4_CKSUM = 0x1,
};

struct fun_eth_offload {
	__be16 flags; /* combination of above flags */
	__be16 mss; /* TSO max seg size */
	__be16 tcp_doff_flags; /* TCP data offset + flags 16b word */
	__be16 vlan;

	__be16 inner_l3_off; /* Inner L3 header offset */
	__be16 inner_l4_off; /* Inner L4 header offset */
	__be16 outer_l3_off; /* Outer L3 header offset */
	__be16 outer_l4_off; /* Outer L4 header offset */
};

static inline void fun_eth_offload_init(struct fun_eth_offload *s, u16 flags,
					u16 mss, __be16 tcp_doff_flags,
					__be16 vlan, u16 inner_l3_off,
					u16 inner_l4_off, u16 outer_l3_off,
					u16 outer_l4_off)
{
	s->flags = cpu_to_be16(flags);
	s->mss = cpu_to_be16(mss);
	s->tcp_doff_flags = tcp_doff_flags;
	s->vlan = vlan;
	s->inner_l3_off = cpu_to_be16(inner_l3_off);
	s->inner_l4_off = cpu_to_be16(inner_l4_off);
	s->outer_l3_off = cpu_to_be16(outer_l3_off);
	s->outer_l4_off = cpu_to_be16(outer_l4_off);
}

struct fun_eth_tls {
	__be64 tlsid;
};

enum {
	FUN_ETH_TX_TLS = 0x8000,
};

struct fun_eth_tx_req {
	__u8 op;
	__u8 len8;
	__be16 flags;
	__u8 suboff8;
	__u8 repr_idn;
	__be16 encap_proto;

	struct fun_eth_offload offload;

	struct fun_dataop_hdr dataop;
};

struct fun_eth_rx_cv {
	__be16 il4_prot_to_l2_type;
};

#define FUN_ETH_RX_CV_IL4_PROT_S 13U
#define FUN_ETH_RX_CV_IL4_PROT_M 0x3

#define FUN_ETH_RX_CV_IL3_PROT_S 11U
#define FUN_ETH_RX_CV_IL3_PROT_M 0x3

#define FUN_ETH_RX_CV_OL4_PROT_S 8U
#define FUN_ETH_RX_CV_OL4_PROT_M 0x7

#define FUN_ETH_RX_CV_ENCAP_TYPE_S 6U
#define FUN_ETH_RX_CV_ENCAP_TYPE_M 0x3

#define FUN_ETH_RX_CV_OL3_PROT_S 4U
#define FUN_ETH_RX_CV_OL3_PROT_M 0x3

#define FUN_ETH_RX_CV_VLAN_TYPE_S 3U
#define FUN_ETH_RX_CV_VLAN_TYPE_M 0x1

#define FUN_ETH_RX_CV_L2_TYPE_S 2U
#define FUN_ETH_RX_CV_L2_TYPE_M 0x1

enum fun_rx_cv {
	FUN_RX_CV_NONE = 0x0,
	FUN_RX_CV_IP = 0x2,
	FUN_RX_CV_IP6 = 0x3,
	FUN_RX_CV_TCP = 0x2,
	FUN_RX_CV_UDP = 0x3,
	FUN_RX_CV_VXLAN = 0x2,
	FUN_RX_CV_MPLS = 0x3,
};

struct fun_eth_cqe {
	__u8 op;
	__u8 len8;
	__u8 nsgl;
	__u8 repr_idn;
	__be32 pkt_len;

	__be64 timestamp;

	__be16 pkt_cv;
	__be16 rsvd0;
	__be32 hash;

	__be16 encap_proto;
	__be16 vlan;
	__be32 rsvd1;

	__be32 buf_offset;
	__be16 headroom;
	__be16 csum;
};

enum fun_admin_adi_attr {
	FUN_ADMIN_ADI_ATTR_MACADDR = 0x1,
	FUN_ADMIN_ADI_ATTR_VLAN = 0x2,
	FUN_ADMIN_ADI_ATTR_RATE = 0x3,
};

struct fun_adi_param {
	union adi_param {
		struct fun_adi_mac {
			__be64 addr;
		} mac;
		struct fun_adi_vlan {
			__be32 rsvd;
			__be16 eth_type;
			__be16 tci;
		} vlan;
		struct fun_adi_rate {
			__be32 rsvd;
			__be32 tx_mbps;
		} rate;
	} u;
};

#define FUN_ADI_MAC_INIT(_addr)             \
	(struct fun_adi_mac) {              \
		.addr = cpu_to_be64(_addr), \
	}

#define FUN_ADI_VLAN_INIT(_eth_type, _tci)                                    \
	(struct fun_adi_vlan) {                                               \
		.eth_type = cpu_to_be16(_eth_type), .tci = cpu_to_be16(_tci), \
	}

#define FUN_ADI_RATE_INIT(_tx_mbps)               \
	(struct fun_adi_rate) {                   \
		.tx_mbps = cpu_to_be32(_tx_mbps), \
	}

struct fun_admin_adi_req {
	struct fun_admin_req_common common;

	union adi_req_subop {
		struct fun_admin_adi_write_req {
			__u8 subop;
			__u8 attribute;
			__be16 rsvd;
			__be32 id;

			struct fun_adi_param param;
		} write;
	} u;
};

#define FUN_ADMIN_ADI_WRITE_REQ_INIT(_subop, _attribute, _id) \
	(struct fun_admin_adi_write_req) {                    \
		.subop = (_subop), .attribute = (_attribute), \
		.id = cpu_to_be32(_id),                       \
	}

#endif /* __FUN_HCI_H */
