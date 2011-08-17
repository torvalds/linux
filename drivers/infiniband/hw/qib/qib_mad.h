/*
 * Copyright (c) 2006, 2007, 2008, 2009, 2010 QLogic Corporation.
 * All rights reserved.
 * Copyright (c) 2005, 2006 PathScale, Inc. All rights reserved.
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
 */

#include <rdma/ib_pma.h>

#define IB_SMP_UNSUP_VERSION    cpu_to_be16(0x0004)
#define IB_SMP_UNSUP_METHOD     cpu_to_be16(0x0008)
#define IB_SMP_UNSUP_METH_ATTR  cpu_to_be16(0x000C)
#define IB_SMP_INVALID_FIELD    cpu_to_be16(0x001C)

struct ib_node_info {
	u8 base_version;
	u8 class_version;
	u8 node_type;
	u8 num_ports;
	__be64 sys_guid;
	__be64 node_guid;
	__be64 port_guid;
	__be16 partition_cap;
	__be16 device_id;
	__be32 revision;
	u8 local_port_num;
	u8 vendor_id[3];
} __attribute__ ((packed));

struct ib_mad_notice_attr {
	u8 generic_type;
	u8 prod_type_msb;
	__be16 prod_type_lsb;
	__be16 trap_num;
	__be16 issuer_lid;
	__be16 toggle_count;

	union {
		struct {
			u8	details[54];
		} raw_data;

		struct {
			__be16	reserved;
			__be16	lid;		/* where violation happened */
			u8	port_num;	/* where violation happened */
		} __attribute__ ((packed)) ntc_129_131;

		struct {
			__be16	reserved;
			__be16	lid;		/* LID where change occurred */
			u8	reserved2;
			u8	local_changes;	/* low bit - local changes */
			__be32	new_cap_mask;	/* new capability mask */
			u8	reserved3;
			u8	change_flags;	/* low 3 bits only */
		} __attribute__ ((packed)) ntc_144;

		struct {
			__be16	reserved;
			__be16	lid;		/* lid where sys guid changed */
			__be16	reserved2;
			__be64	new_sys_guid;
		} __attribute__ ((packed)) ntc_145;

		struct {
			__be16	reserved;
			__be16	lid;
			__be16	dr_slid;
			u8	method;
			u8	reserved2;
			__be16	attr_id;
			__be32	attr_mod;
			__be64	mkey;
			u8	reserved3;
			u8	dr_trunc_hop;
			u8	dr_rtn_path[30];
		} __attribute__ ((packed)) ntc_256;

		struct {
			__be16		reserved;
			__be16		lid1;
			__be16		lid2;
			__be32		key;
			__be32		sl_qp1;	/* SL: high 4 bits */
			__be32		qp2;	/* high 8 bits reserved */
			union ib_gid	gid1;
			union ib_gid	gid2;
		} __attribute__ ((packed)) ntc_257_258;

	} details;
};

/*
 * Generic trap/notice types
 */
#define IB_NOTICE_TYPE_FATAL	0x80
#define IB_NOTICE_TYPE_URGENT	0x81
#define IB_NOTICE_TYPE_SECURITY	0x82
#define IB_NOTICE_TYPE_SM	0x83
#define IB_NOTICE_TYPE_INFO	0x84

/*
 * Generic trap/notice producers
 */
#define IB_NOTICE_PROD_CA		cpu_to_be16(1)
#define IB_NOTICE_PROD_SWITCH		cpu_to_be16(2)
#define IB_NOTICE_PROD_ROUTER		cpu_to_be16(3)
#define IB_NOTICE_PROD_CLASS_MGR	cpu_to_be16(4)

/*
 * Generic trap/notice numbers
 */
#define IB_NOTICE_TRAP_LLI_THRESH	cpu_to_be16(129)
#define IB_NOTICE_TRAP_EBO_THRESH	cpu_to_be16(130)
#define IB_NOTICE_TRAP_FLOW_UPDATE	cpu_to_be16(131)
#define IB_NOTICE_TRAP_CAP_MASK_CHG	cpu_to_be16(144)
#define IB_NOTICE_TRAP_SYS_GUID_CHG	cpu_to_be16(145)
#define IB_NOTICE_TRAP_BAD_MKEY		cpu_to_be16(256)
#define IB_NOTICE_TRAP_BAD_PKEY		cpu_to_be16(257)
#define IB_NOTICE_TRAP_BAD_QKEY		cpu_to_be16(258)

/*
 * Repress trap/notice flags
 */
#define IB_NOTICE_REPRESS_LLI_THRESH	(1 << 0)
#define IB_NOTICE_REPRESS_EBO_THRESH	(1 << 1)
#define IB_NOTICE_REPRESS_FLOW_UPDATE	(1 << 2)
#define IB_NOTICE_REPRESS_CAP_MASK_CHG	(1 << 3)
#define IB_NOTICE_REPRESS_SYS_GUID_CHG	(1 << 4)
#define IB_NOTICE_REPRESS_BAD_MKEY	(1 << 5)
#define IB_NOTICE_REPRESS_BAD_PKEY	(1 << 6)
#define IB_NOTICE_REPRESS_BAD_QKEY	(1 << 7)

/*
 * Generic trap/notice other local changes flags (trap 144).
 */
#define IB_NOTICE_TRAP_LSE_CHG		0x04	/* Link Speed Enable changed */
#define IB_NOTICE_TRAP_LWE_CHG		0x02	/* Link Width Enable changed */
#define IB_NOTICE_TRAP_NODE_DESC_CHG	0x01

/*
 * Generic trap/notice M_Key volation flags in dr_trunc_hop (trap 256).
 */
#define IB_NOTICE_TRAP_DR_NOTICE	0x80
#define IB_NOTICE_TRAP_DR_TRUNC		0x40

struct ib_vl_weight_elem {
	u8      vl;     /* Only low 4 bits, upper 4 bits reserved */
	u8      weight;
};

#define IB_VLARB_LOWPRI_0_31    1
#define IB_VLARB_LOWPRI_32_63   2
#define IB_VLARB_HIGHPRI_0_31   3
#define IB_VLARB_HIGHPRI_32_63  4

#define IB_PMA_PORT_COUNTERS_CONG       cpu_to_be16(0xFF00)

struct ib_pma_portcounters_cong {
	u8 reserved;
	u8 reserved1;
	__be16 port_check_rate;
	__be16 symbol_error_counter;
	u8 link_error_recovery_counter;
	u8 link_downed_counter;
	__be16 port_rcv_errors;
	__be16 port_rcv_remphys_errors;
	__be16 port_rcv_switch_relay_errors;
	__be16 port_xmit_discards;
	u8 port_xmit_constraint_errors;
	u8 port_rcv_constraint_errors;
	u8 reserved2;
	u8 link_overrun_errors; /* LocalLink: 7:4, BufferOverrun: 3:0 */
	__be16 reserved3;
	__be16 vl15_dropped;
	__be64 port_xmit_data;
	__be64 port_rcv_data;
	__be64 port_xmit_packets;
	__be64 port_rcv_packets;
	__be64 port_xmit_wait;
	__be64 port_adr_events;
} __attribute__ ((packed));

#define IB_PMA_CONG_HW_CONTROL_TIMER            0x00
#define IB_PMA_CONG_HW_CONTROL_SAMPLE           0x01

#define QIB_XMIT_RATE_UNSUPPORTED               0x0
#define QIB_XMIT_RATE_PICO                      0x7
/* number of 4nsec cycles equaling 2secs */
#define QIB_CONG_TIMER_PSINTERVAL               0x1DCD64EC

#define IB_PMA_SEL_CONG_ALL                     0x01
#define IB_PMA_SEL_CONG_PORT_DATA               0x02
#define IB_PMA_SEL_CONG_XMIT                    0x04
#define IB_PMA_SEL_CONG_ROUTING                 0x08

/*
 * The PortSamplesControl.CounterMasks field is an array of 3 bit fields
 * which specify the N'th counter's capabilities. See ch. 16.1.3.2.
 * We support 5 counters which only count the mandatory quantities.
 */
#define COUNTER_MASK(q, n) (q << ((9 - n) * 3))
#define COUNTER_MASK0_9 \
	cpu_to_be32(COUNTER_MASK(1, 0) | \
		    COUNTER_MASK(1, 1) | \
		    COUNTER_MASK(1, 2) | \
		    COUNTER_MASK(1, 3) | \
		    COUNTER_MASK(1, 4))
