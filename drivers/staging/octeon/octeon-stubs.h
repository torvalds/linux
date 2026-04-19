/* SPDX-License-Identifier: GPL-2.0 */
#define CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE	512

#ifndef XKPHYS_TO_PHYS
# define XKPHYS_TO_PHYS(p)			(p)
#endif

#define OCTEON_IRQ_WORKQ0 0
#define OCTEON_IRQ_RML 0
#define OCTEON_IRQ_TIMER1 0
#define OCTEON_IS_MODEL(x) 0
#define octeon_has_feature(x)	0
#define octeon_get_clock_rate()	0

#define CVMX_SYNCIOBDMA		do { } while (0)

#define CVMX_HELPER_INPUT_TAG_TYPE	0
#define CVMX_HELPER_FIRST_MBUFF_SKIP	7
#define CVMX_FAU_REG_END		(2048)
#define CVMX_FPA_OUTPUT_BUFFER_POOL	    (2)
#define CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE    16
#define CVMX_FPA_PACKET_POOL		    (0)
#define CVMX_FPA_PACKET_POOL_SIZE	    16
#define CVMX_FPA_WQE_POOL		    (1)
#define CVMX_FPA_WQE_POOL_SIZE		    16
#define CVMX_GMXX_RXX_ADR_CAM_EN(a, b)	((a) + (b))
#define CVMX_GMXX_RXX_ADR_CTL(a, b)	((a) + (b))
#define CVMX_GMXX_PRTX_CFG(a, b)	((a) + (b))
#define CVMX_GMXX_RXX_FRM_MAX(a, b)	((a) + (b))
#define CVMX_GMXX_RXX_JABBER(a, b)	((a) + (b))
#define CVMX_IPD_CTL_STATUS		0
#define CVMX_PIP_FRM_LEN_CHKX(a)	(a)
#define CVMX_PIP_NUM_INPUT_PORTS	1
#define CVMX_SCR_SCRATCH		0
#define CVMX_PKO_QUEUES_PER_PORT_INTERFACE0	2
#define CVMX_PKO_QUEUES_PER_PORT_INTERFACE1	2
#define CVMX_IPD_SUB_PORT_FCS		0
#define CVMX_SSO_WQ_IQ_DIS		0
#define CVMX_SSO_WQ_INT			0
#define CVMX_POW_WQ_INT			0
#define CVMX_SSO_WQ_INT_PC		0
#define CVMX_NPI_RSL_INT_BLOCKS		0
#define CVMX_POW_WQ_INT_PC		0

union cvmx_pip_wqe_word2 {
	u64 u64;

	struct {
		u64 bufs         : 8;
		u64 ip_offset    : 8;
		u64 vlan_valid   : 1;
		u64 vlan_stacked : 1;
		u64 unassigned   : 1;
		u64 vlan_cfi     : 1;
		u64 vlan_id      : 12;
		u64 pr           : 4;
		u64 unassigned2  : 8;
		u64 dec_ipcomp   : 1;
		u64 tcp_or_udp   : 1;
		u64 dec_ipsec    : 1;
		u64 is_v6        : 1;
		u64 software     : 1;
		u64 L4_error     : 1;
		u64 is_frag      : 1;
		u64 IP_exc       : 1;
		u64 is_bcast     : 1;
		u64 is_mcast     : 1;
		u64 not_IP       : 1;
		u64 rcv_error    : 1;
		u64 err_code     : 8;
	} s;

	struct {
		u64 bufs         : 8;
		u64 ip_offset    : 8;
		u64 vlan_valid   : 1;
		u64 vlan_stacked : 1;
		u64 unassigned   : 1;
		u64 vlan_cfi     : 1;
		u64 vlan_id      : 12;
		u64 port         : 12;
		u64 dec_ipcomp   : 1;
		u64 tcp_or_udp   : 1;
		u64 dec_ipsec    : 1;
		u64 is_v6        : 1;
		u64 software     : 1;
		u64 L4_error     : 1;
		u64 is_frag      : 1;
		u64 IP_exc       : 1;
		u64 is_bcast     : 1;
		u64 is_mcast     : 1;
		u64 not_IP       : 1;
		u64 rcv_error    : 1;
		u64 err_code     : 8;
	} s_cn68xx;

	struct {
		u64 unused1 : 16;
		u64 vlan    : 16;
		u64 unused2 : 32;
	} svlan;

	struct {
		u64 bufs         : 8;
		u64 unused       : 8;
		u64 vlan_valid   : 1;
		u64 vlan_stacked : 1;
		u64 unassigned   : 1;
		u64 vlan_cfi     : 1;
		u64 vlan_id      : 12;
		u64 pr           : 4;
		u64 unassigned2  : 12;
		u64 software     : 1;
		u64 unassigned3  : 1;
		u64 is_rarp      : 1;
		u64 is_arp       : 1;
		u64 is_bcast     : 1;
		u64 is_mcast     : 1;
		u64 not_IP       : 1;
		u64 rcv_error    : 1;
		u64 err_code     : 8;
	} snoip;
};

union cvmx_pip_wqe_word0 {
	struct {
		u64 next_ptr:40;
		u8 unused;
		__wsum hw_chksum;
	} cn38xx;
	struct {
		u64 pknd:6;        /* 0..5 */
		u64 unused2:2;     /* 6..7 */
		u64 bpid:6;        /* 8..13 */
		u64 unused1:18;    /* 14..31 */
		u64 l2ptr:8;       /* 32..39 */
		u64 l3ptr:8;       /* 40..47 */
		u64 unused0:8;     /* 48..55 */
		u64 l4ptr:8;       /* 56..63 */
	} cn68xx;
};

union cvmx_wqe_word0 {
	u64 u64;
	union cvmx_pip_wqe_word0 pip;
};

union cvmx_wqe_word1 {
	u64 u64;
	struct {
		u64 tag:32;
		u64 tag_type:2;
		u64 varies:14;
		u64 len:16;
	};
	struct {
		u64 tag:32;
		u64 tag_type:2;
		u64 zero_2:3;
		u64 grp:6;
		u64 zero_1:1;
		u64 qos:3;
		u64 zero_0:1;
		u64 len:16;
	} cn68xx;
	struct {
		u64 tag:32;
		u64 tag_type:2;
		u64 zero_2:1;
		u64 grp:4;
		u64 qos:3;
		u64 ipprt:6;
		u64 len:16;
	} cn38xx;
};

union cvmx_buf_ptr {
	void *ptr;
	u64 u64;
	struct {
		u64 i:1;
		u64 back:4;
		u64 pool:3;
		u64 size:16;
		u64 addr:40;
	} s;
};

struct cvmx_wqe {
	union cvmx_wqe_word0 word0;
	union cvmx_wqe_word1 word1;
	union cvmx_pip_wqe_word2 word2;
	union cvmx_buf_ptr packet_ptr;
	u8 packet_data[96];
};

union cvmx_helper_link_info {
	u64 u64;
	struct {
		u64 reserved_20_63:44;
		u64 link_up:1;	    /**< Is the physical link up? */
		u64 full_duplex:1;	    /**< 1 if the link is full duplex */
		u64 speed:18;	    /**< Speed of the link in Mbps */
	} s;
};

enum cvmx_fau_reg_32 {
	CVMX_FAU_REG_32_START	= 0,
};

enum cvmx_fau_op_size {
	CVMX_FAU_OP_SIZE_8 = 0,
	CVMX_FAU_OP_SIZE_16 = 1,
	CVMX_FAU_OP_SIZE_32 = 2,
	CVMX_FAU_OP_SIZE_64 = 3
};

typedef enum {
	CVMX_SPI_MODE_UNKNOWN = 0,
	CVMX_SPI_MODE_TX_HALFPLEX = 1,
	CVMX_SPI_MODE_RX_HALFPLEX = 2,
	CVMX_SPI_MODE_DUPLEX = 3
} cvmx_spi_mode_t;

typedef enum {
	CVMX_HELPER_INTERFACE_MODE_DISABLED,
	CVMX_HELPER_INTERFACE_MODE_RGMII,
	CVMX_HELPER_INTERFACE_MODE_GMII,
	CVMX_HELPER_INTERFACE_MODE_SPI,
	CVMX_HELPER_INTERFACE_MODE_PCIE,
	CVMX_HELPER_INTERFACE_MODE_XAUI,
	CVMX_HELPER_INTERFACE_MODE_SGMII,
	CVMX_HELPER_INTERFACE_MODE_PICMG,
	CVMX_HELPER_INTERFACE_MODE_NPI,
	CVMX_HELPER_INTERFACE_MODE_LOOP,
} cvmx_helper_interface_mode_t;

typedef enum {
	CVMX_POW_WAIT = 1,
	CVMX_POW_NO_WAIT = 0,
} cvmx_pow_wait_t;

typedef enum {
	CVMX_PKO_LOCK_NONE = 0,
	CVMX_PKO_LOCK_ATOMIC_TAG = 1,
	CVMX_PKO_LOCK_CMD_QUEUE = 2,
} cvmx_pko_lock_t;

typedef enum {
	CVMX_PKO_SUCCESS,
	CVMX_PKO_INVALID_PORT,
	CVMX_PKO_INVALID_QUEUE,
	CVMX_PKO_INVALID_PRIORITY,
	CVMX_PKO_NO_MEMORY,
	CVMX_PKO_PORT_ALREADY_SETUP,
	CVMX_PKO_CMD_QUEUE_INIT_ERROR
} cvmx_pko_status_t;

enum cvmx_pow_tag_type {
	CVMX_POW_TAG_TYPE_ORDERED   = 0L,
	CVMX_POW_TAG_TYPE_ATOMIC    = 1L,
	CVMX_POW_TAG_TYPE_NULL	    = 2L,
	CVMX_POW_TAG_TYPE_NULL_NULL = 3L
};

union cvmx_ipd_ctl_status {
	u64 u64;
	struct cvmx_ipd_ctl_status_s {
		u64 reserved_18_63:46;
		u64 use_sop:1;
		u64 rst_done:1;
		u64 clken:1;
		u64 no_wptr:1;
		u64 pq_apkt:1;
		u64 pq_nabuf:1;
		u64 ipd_full:1;
		u64 pkt_off:1;
		u64 len_m8:1;
		u64 reset:1;
		u64 addpkt:1;
		u64 naddbuf:1;
		u64 pkt_lend:1;
		u64 wqe_lend:1;
		u64 pbp_en:1;
		u64 opc_mode:2;
		u64 ipd_en:1;
	} s;
	struct cvmx_ipd_ctl_status_cn30xx {
		u64 reserved_10_63:54;
		u64 len_m8:1;
		u64 reset:1;
		u64 addpkt:1;
		u64 naddbuf:1;
		u64 pkt_lend:1;
		u64 wqe_lend:1;
		u64 pbp_en:1;
		u64 opc_mode:2;
		u64 ipd_en:1;
	} cn30xx;
	struct cvmx_ipd_ctl_status_cn38xxp2 {
		u64 reserved_9_63:55;
		u64 reset:1;
		u64 addpkt:1;
		u64 naddbuf:1;
		u64 pkt_lend:1;
		u64 wqe_lend:1;
		u64 pbp_en:1;
		u64 opc_mode:2;
		u64 ipd_en:1;
	} cn38xxp2;
	struct cvmx_ipd_ctl_status_cn50xx {
		u64 reserved_15_63:49;
		u64 no_wptr:1;
		u64 pq_apkt:1;
		u64 pq_nabuf:1;
		u64 ipd_full:1;
		u64 pkt_off:1;
		u64 len_m8:1;
		u64 reset:1;
		u64 addpkt:1;
		u64 naddbuf:1;
		u64 pkt_lend:1;
		u64 wqe_lend:1;
		u64 pbp_en:1;
		u64 opc_mode:2;
		u64 ipd_en:1;
	} cn50xx;
	struct cvmx_ipd_ctl_status_cn58xx {
		u64 reserved_12_63:52;
		u64 ipd_full:1;
		u64 pkt_off:1;
		u64 len_m8:1;
		u64 reset:1;
		u64 addpkt:1;
		u64 naddbuf:1;
		u64 pkt_lend:1;
		u64 wqe_lend:1;
		u64 pbp_en:1;
		u64 opc_mode:2;
		u64 ipd_en:1;
	} cn58xx;
	struct cvmx_ipd_ctl_status_cn63xxp1 {
		u64 reserved_16_63:48;
		u64 clken:1;
		u64 no_wptr:1;
		u64 pq_apkt:1;
		u64 pq_nabuf:1;
		u64 ipd_full:1;
		u64 pkt_off:1;
		u64 len_m8:1;
		u64 reset:1;
		u64 addpkt:1;
		u64 naddbuf:1;
		u64 pkt_lend:1;
		u64 wqe_lend:1;
		u64 pbp_en:1;
		u64 opc_mode:2;
		u64 ipd_en:1;
	} cn63xxp1;
};

union cvmx_ipd_sub_port_fcs {
	u64 u64;
	struct cvmx_ipd_sub_port_fcs_s {
		u64 port_bit:32;
		u64 reserved_32_35:4;
		u64 port_bit2:4;
		u64 reserved_40_63:24;
	} s;
	struct cvmx_ipd_sub_port_fcs_cn30xx {
		u64 port_bit:3;
		u64 reserved_3_63:61;
	} cn30xx;
	struct cvmx_ipd_sub_port_fcs_cn38xx {
		u64 port_bit:32;
		u64 reserved_32_63:32;
	} cn38xx;
};

union cvmx_ipd_sub_port_qos_cnt {
	u64 u64;
	struct cvmx_ipd_sub_port_qos_cnt_s {
		u64 cnt:32;
		u64 port_qos:9;
		u64 reserved_41_63:23;
	} s;
};

typedef struct {
	u32 dropped_octets;
	u32 dropped_packets;
	u32 pci_raw_packets;
	u32 octets;
	u32 packets;
	u32 multicast_packets;
	u32 broadcast_packets;
	u32 len_64_packets;
	u32 len_65_127_packets;
	u32 len_128_255_packets;
	u32 len_256_511_packets;
	u32 len_512_1023_packets;
	u32 len_1024_1518_packets;
	u32 len_1519_max_packets;
	u32 fcs_align_err_packets;
	u32 runt_packets;
	u32 runt_crc_packets;
	u32 oversize_packets;
	u32 oversize_crc_packets;
	u32 inb_packets;
	u64 inb_octets;
	u16 inb_errors;
} cvmx_pip_port_status_t;

typedef struct {
	u32 packets;
	u64 octets;
	u64 doorbell;
} cvmx_pko_port_status_t;

union cvmx_pip_frm_len_chkx {
	u64 u64;
	struct cvmx_pip_frm_len_chkx_s {
		u64 reserved_32_63:32;
		u64 maxlen:16;
		u64 minlen:16;
	} s;
};

union cvmx_gmxx_rxx_frm_ctl {
	u64 u64;
	struct cvmx_gmxx_rxx_frm_ctl_s {
		u64 pre_chk:1;
		u64 pre_strp:1;
		u64 ctl_drp:1;
		u64 ctl_bck:1;
		u64 ctl_mcst:1;
		u64 ctl_smac:1;
		u64 pre_free:1;
		u64 vlan_len:1;
		u64 pad_len:1;
		u64 pre_align:1;
		u64 null_dis:1;
		u64 reserved_11_11:1;
		u64 ptp_mode:1;
		u64 reserved_13_63:51;
	} s;
	struct cvmx_gmxx_rxx_frm_ctl_cn30xx {
		u64 pre_chk:1;
		u64 pre_strp:1;
		u64 ctl_drp:1;
		u64 ctl_bck:1;
		u64 ctl_mcst:1;
		u64 ctl_smac:1;
		u64 pre_free:1;
		u64 vlan_len:1;
		u64 pad_len:1;
		u64 reserved_9_63:55;
	} cn30xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn31xx {
		u64 pre_chk:1;
		u64 pre_strp:1;
		u64 ctl_drp:1;
		u64 ctl_bck:1;
		u64 ctl_mcst:1;
		u64 ctl_smac:1;
		u64 pre_free:1;
		u64 vlan_len:1;
		u64 reserved_8_63:56;
	} cn31xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn50xx {
		u64 pre_chk:1;
		u64 pre_strp:1;
		u64 ctl_drp:1;
		u64 ctl_bck:1;
		u64 ctl_mcst:1;
		u64 ctl_smac:1;
		u64 pre_free:1;
		u64 reserved_7_8:2;
		u64 pre_align:1;
		u64 null_dis:1;
		u64 reserved_11_63:53;
	} cn50xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn56xxp1 {
		u64 pre_chk:1;
		u64 pre_strp:1;
		u64 ctl_drp:1;
		u64 ctl_bck:1;
		u64 ctl_mcst:1;
		u64 ctl_smac:1;
		u64 pre_free:1;
		u64 reserved_7_8:2;
		u64 pre_align:1;
		u64 reserved_10_63:54;
	} cn56xxp1;
	struct cvmx_gmxx_rxx_frm_ctl_cn58xx {
		u64 pre_chk:1;
		u64 pre_strp:1;
		u64 ctl_drp:1;
		u64 ctl_bck:1;
		u64 ctl_mcst:1;
		u64 ctl_smac:1;
		u64 pre_free:1;
		u64 vlan_len:1;
		u64 pad_len:1;
		u64 pre_align:1;
		u64 null_dis:1;
		u64 reserved_11_63:53;
	} cn58xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx {
		u64 pre_chk:1;
		u64 pre_strp:1;
		u64 ctl_drp:1;
		u64 ctl_bck:1;
		u64 ctl_mcst:1;
		u64 ctl_smac:1;
		u64 pre_free:1;
		u64 reserved_7_8:2;
		u64 pre_align:1;
		u64 null_dis:1;
		u64 reserved_11_11:1;
		u64 ptp_mode:1;
		u64 reserved_13_63:51;
	} cn61xx;
};

union cvmx_gmxx_rxx_int_reg {
	u64 u64;
	struct cvmx_gmxx_rxx_int_reg_s {
		u64 minerr:1;
		u64 carext:1;
		u64 maxerr:1;
		u64 jabber:1;
		u64 fcserr:1;
		u64 alnerr:1;
		u64 lenerr:1;
		u64 rcverr:1;
		u64 skperr:1;
		u64 niberr:1;
		u64 ovrerr:1;
		u64 pcterr:1;
		u64 rsverr:1;
		u64 falerr:1;
		u64 coldet:1;
		u64 ifgerr:1;
		u64 phy_link:1;
		u64 phy_spd:1;
		u64 phy_dupx:1;
		u64 pause_drp:1;
		u64 loc_fault:1;
		u64 rem_fault:1;
		u64 bad_seq:1;
		u64 bad_term:1;
		u64 unsop:1;
		u64 uneop:1;
		u64 undat:1;
		u64 hg2fld:1;
		u64 hg2cc:1;
		u64 reserved_29_63:35;
	} s;
	struct cvmx_gmxx_rxx_int_reg_cn30xx {
		u64 minerr:1;
		u64 carext:1;
		u64 maxerr:1;
		u64 jabber:1;
		u64 fcserr:1;
		u64 alnerr:1;
		u64 lenerr:1;
		u64 rcverr:1;
		u64 skperr:1;
		u64 niberr:1;
		u64 ovrerr:1;
		u64 pcterr:1;
		u64 rsverr:1;
		u64 falerr:1;
		u64 coldet:1;
		u64 ifgerr:1;
		u64 phy_link:1;
		u64 phy_spd:1;
		u64 phy_dupx:1;
		u64 reserved_19_63:45;
	} cn30xx;
	struct cvmx_gmxx_rxx_int_reg_cn50xx {
		u64 reserved_0_0:1;
		u64 carext:1;
		u64 reserved_2_2:1;
		u64 jabber:1;
		u64 fcserr:1;
		u64 alnerr:1;
		u64 reserved_6_6:1;
		u64 rcverr:1;
		u64 skperr:1;
		u64 niberr:1;
		u64 ovrerr:1;
		u64 pcterr:1;
		u64 rsverr:1;
		u64 falerr:1;
		u64 coldet:1;
		u64 ifgerr:1;
		u64 phy_link:1;
		u64 phy_spd:1;
		u64 phy_dupx:1;
		u64 pause_drp:1;
		u64 reserved_20_63:44;
	} cn50xx;
	struct cvmx_gmxx_rxx_int_reg_cn52xx {
		u64 reserved_0_0:1;
		u64 carext:1;
		u64 reserved_2_2:1;
		u64 jabber:1;
		u64 fcserr:1;
		u64 reserved_5_6:2;
		u64 rcverr:1;
		u64 skperr:1;
		u64 reserved_9_9:1;
		u64 ovrerr:1;
		u64 pcterr:1;
		u64 rsverr:1;
		u64 falerr:1;
		u64 coldet:1;
		u64 ifgerr:1;
		u64 reserved_16_18:3;
		u64 pause_drp:1;
		u64 loc_fault:1;
		u64 rem_fault:1;
		u64 bad_seq:1;
		u64 bad_term:1;
		u64 unsop:1;
		u64 uneop:1;
		u64 undat:1;
		u64 hg2fld:1;
		u64 hg2cc:1;
		u64 reserved_29_63:35;
	} cn52xx;
	struct cvmx_gmxx_rxx_int_reg_cn56xxp1 {
		u64 reserved_0_0:1;
		u64 carext:1;
		u64 reserved_2_2:1;
		u64 jabber:1;
		u64 fcserr:1;
		u64 reserved_5_6:2;
		u64 rcverr:1;
		u64 skperr:1;
		u64 reserved_9_9:1;
		u64 ovrerr:1;
		u64 pcterr:1;
		u64 rsverr:1;
		u64 falerr:1;
		u64 coldet:1;
		u64 ifgerr:1;
		u64 reserved_16_18:3;
		u64 pause_drp:1;
		u64 loc_fault:1;
		u64 rem_fault:1;
		u64 bad_seq:1;
		u64 bad_term:1;
		u64 unsop:1;
		u64 uneop:1;
		u64 undat:1;
		u64 reserved_27_63:37;
	} cn56xxp1;
	struct cvmx_gmxx_rxx_int_reg_cn58xx {
		u64 minerr:1;
		u64 carext:1;
		u64 maxerr:1;
		u64 jabber:1;
		u64 fcserr:1;
		u64 alnerr:1;
		u64 lenerr:1;
		u64 rcverr:1;
		u64 skperr:1;
		u64 niberr:1;
		u64 ovrerr:1;
		u64 pcterr:1;
		u64 rsverr:1;
		u64 falerr:1;
		u64 coldet:1;
		u64 ifgerr:1;
		u64 phy_link:1;
		u64 phy_spd:1;
		u64 phy_dupx:1;
		u64 pause_drp:1;
		u64 reserved_20_63:44;
	} cn58xx;
	struct cvmx_gmxx_rxx_int_reg_cn61xx {
		u64 minerr:1;
		u64 carext:1;
		u64 reserved_2_2:1;
		u64 jabber:1;
		u64 fcserr:1;
		u64 reserved_5_6:2;
		u64 rcverr:1;
		u64 skperr:1;
		u64 reserved_9_9:1;
		u64 ovrerr:1;
		u64 pcterr:1;
		u64 rsverr:1;
		u64 falerr:1;
		u64 coldet:1;
		u64 ifgerr:1;
		u64 reserved_16_18:3;
		u64 pause_drp:1;
		u64 loc_fault:1;
		u64 rem_fault:1;
		u64 bad_seq:1;
		u64 bad_term:1;
		u64 unsop:1;
		u64 uneop:1;
		u64 undat:1;
		u64 hg2fld:1;
		u64 hg2cc:1;
		u64 reserved_29_63:35;
	} cn61xx;
};

union cvmx_gmxx_prtx_cfg {
	u64 u64;
	struct cvmx_gmxx_prtx_cfg_s {
		u64 reserved_22_63:42;
		u64 pknd:6;
		u64 reserved_14_15:2;
		u64 tx_idle:1;
		u64 rx_idle:1;
		u64 reserved_9_11:3;
		u64 speed_msb:1;
		u64 reserved_4_7:4;
		u64 slottime:1;
		u64 duplex:1;
		u64 speed:1;
		u64 en:1;
	} s;
	struct cvmx_gmxx_prtx_cfg_cn30xx {
		u64 reserved_4_63:60;
		u64 slottime:1;
		u64 duplex:1;
		u64 speed:1;
		u64 en:1;
	} cn30xx;
	struct cvmx_gmxx_prtx_cfg_cn52xx {
		u64 reserved_14_63:50;
		u64 tx_idle:1;
		u64 rx_idle:1;
		u64 reserved_9_11:3;
		u64 speed_msb:1;
		u64 reserved_4_7:4;
		u64 slottime:1;
		u64 duplex:1;
		u64 speed:1;
		u64 en:1;
	} cn52xx;
};

union cvmx_gmxx_rxx_adr_ctl {
	u64 u64;
	struct cvmx_gmxx_rxx_adr_ctl_s {
		u64 reserved_4_63:60;
		u64 cam_mode:1;
		u64 mcst:2;
		u64 bcst:1;
	} s;
};

union cvmx_pip_prt_tagx {
	u64 u64;
	struct cvmx_pip_prt_tagx_s {
		u64 reserved_54_63:10;
		u64 portadd_en:1;
		u64 inc_hwchk:1;
		u64 reserved_50_51:2;
		u64 grptagbase_msb:2;
		u64 reserved_46_47:2;
		u64 grptagmask_msb:2;
		u64 reserved_42_43:2;
		u64 grp_msb:2;
		u64 grptagbase:4;
		u64 grptagmask:4;
		u64 grptag:1;
		u64 grptag_mskip:1;
		u64 tag_mode:2;
		u64 inc_vs:2;
		u64 inc_vlan:1;
		u64 inc_prt_flag:1;
		u64 ip6_dprt_flag:1;
		u64 ip4_dprt_flag:1;
		u64 ip6_sprt_flag:1;
		u64 ip4_sprt_flag:1;
		u64 ip6_nxth_flag:1;
		u64 ip4_pctl_flag:1;
		u64 ip6_dst_flag:1;
		u64 ip4_dst_flag:1;
		u64 ip6_src_flag:1;
		u64 ip4_src_flag:1;
		u64 tcp6_tag_type:2;
		u64 tcp4_tag_type:2;
		u64 ip6_tag_type:2;
		u64 ip4_tag_type:2;
		u64 non_tag_type:2;
		u64 grp:4;
	} s;
	struct cvmx_pip_prt_tagx_cn30xx {
		u64 reserved_40_63:24;
		u64 grptagbase:4;
		u64 grptagmask:4;
		u64 grptag:1;
		u64 reserved_30_30:1;
		u64 tag_mode:2;
		u64 inc_vs:2;
		u64 inc_vlan:1;
		u64 inc_prt_flag:1;
		u64 ip6_dprt_flag:1;
		u64 ip4_dprt_flag:1;
		u64 ip6_sprt_flag:1;
		u64 ip4_sprt_flag:1;
		u64 ip6_nxth_flag:1;
		u64 ip4_pctl_flag:1;
		u64 ip6_dst_flag:1;
		u64 ip4_dst_flag:1;
		u64 ip6_src_flag:1;
		u64 ip4_src_flag:1;
		u64 tcp6_tag_type:2;
		u64 tcp4_tag_type:2;
		u64 ip6_tag_type:2;
		u64 ip4_tag_type:2;
		u64 non_tag_type:2;
		u64 grp:4;
	} cn30xx;
	struct cvmx_pip_prt_tagx_cn50xx {
		u64 reserved_40_63:24;
		u64 grptagbase:4;
		u64 grptagmask:4;
		u64 grptag:1;
		u64 grptag_mskip:1;
		u64 tag_mode:2;
		u64 inc_vs:2;
		u64 inc_vlan:1;
		u64 inc_prt_flag:1;
		u64 ip6_dprt_flag:1;
		u64 ip4_dprt_flag:1;
		u64 ip6_sprt_flag:1;
		u64 ip4_sprt_flag:1;
		u64 ip6_nxth_flag:1;
		u64 ip4_pctl_flag:1;
		u64 ip6_dst_flag:1;
		u64 ip4_dst_flag:1;
		u64 ip6_src_flag:1;
		u64 ip4_src_flag:1;
		u64 tcp6_tag_type:2;
		u64 tcp4_tag_type:2;
		u64 ip6_tag_type:2;
		u64 ip4_tag_type:2;
		u64 non_tag_type:2;
		u64 grp:4;
	} cn50xx;
};

union cvmx_spxx_int_reg {
	u64 u64;
	struct cvmx_spxx_int_reg_s {
		u64 reserved_32_63:32;
		u64 spf:1;
		u64 reserved_12_30:19;
		u64 calerr:1;
		u64 syncerr:1;
		u64 diperr:1;
		u64 tpaovr:1;
		u64 rsverr:1;
		u64 drwnng:1;
		u64 clserr:1;
		u64 spiovr:1;
		u64 reserved_2_3:2;
		u64 abnorm:1;
		u64 prtnxa:1;
	} s;
};

union cvmx_spxx_int_msk {
	u64 u64;
	struct cvmx_spxx_int_msk_s {
		u64 reserved_12_63:52;
		u64 calerr:1;
		u64 syncerr:1;
		u64 diperr:1;
		u64 tpaovr:1;
		u64 rsverr:1;
		u64 drwnng:1;
		u64 clserr:1;
		u64 spiovr:1;
		u64 reserved_2_3:2;
		u64 abnorm:1;
		u64 prtnxa:1;
	} s;
};

union cvmx_pow_wq_int {
	u64 u64;
	struct cvmx_pow_wq_int_s {
		u64 wq_int:16;
		u64 iq_dis:16;
		u64 reserved_32_63:32;
	} s;
};

union cvmx_sso_wq_int_thrx {
	u64 u64;
	struct {
		u64 iq_thr:12;
		u64 reserved_12_13:2;
		u64 ds_thr:12;
		u64 reserved_26_27:2;
		u64 tc_thr:4;
		u64 tc_en:1;
		u64 reserved_33_63:31;
	} s;
};

union cvmx_stxx_int_reg {
	u64 u64;
	struct cvmx_stxx_int_reg_s {
		u64 reserved_9_63:55;
		u64 syncerr:1;
		u64 frmerr:1;
		u64 unxfrm:1;
		u64 nosync:1;
		u64 diperr:1;
		u64 datovr:1;
		u64 ovrbst:1;
		u64 calpar1:1;
		u64 calpar0:1;
	} s;
};

union cvmx_stxx_int_msk {
	u64 u64;
	struct cvmx_stxx_int_msk_s {
		u64 reserved_8_63:56;
		u64 frmerr:1;
		u64 unxfrm:1;
		u64 nosync:1;
		u64 diperr:1;
		u64 datovr:1;
		u64 ovrbst:1;
		u64 calpar1:1;
		u64 calpar0:1;
	} s;
};

union cvmx_pow_wq_int_pc {
	u64 u64;
	struct cvmx_pow_wq_int_pc_s {
		u64 reserved_0_7:8;
		u64 pc_thr:20;
		u64 reserved_28_31:4;
		u64 pc:28;
		u64 reserved_60_63:4;
	} s;
};

union cvmx_pow_wq_int_thrx {
	u64 u64;
	struct cvmx_pow_wq_int_thrx_s {
		u64 reserved_29_63:35;
		u64 tc_en:1;
		u64 tc_thr:4;
		u64 reserved_23_23:1;
		u64 ds_thr:11;
		u64 reserved_11_11:1;
		u64 iq_thr:11;
	} s;
	struct cvmx_pow_wq_int_thrx_cn30xx {
		u64 reserved_29_63:35;
		u64 tc_en:1;
		u64 tc_thr:4;
		u64 reserved_18_23:6;
		u64 ds_thr:6;
		u64 reserved_6_11:6;
		u64 iq_thr:6;
	} cn30xx;
	struct cvmx_pow_wq_int_thrx_cn31xx {
		u64 reserved_29_63:35;
		u64 tc_en:1;
		u64 tc_thr:4;
		u64 reserved_20_23:4;
		u64 ds_thr:8;
		u64 reserved_8_11:4;
		u64 iq_thr:8;
	} cn31xx;
	struct cvmx_pow_wq_int_thrx_cn52xx {
		u64 reserved_29_63:35;
		u64 tc_en:1;
		u64 tc_thr:4;
		u64 reserved_21_23:3;
		u64 ds_thr:9;
		u64 reserved_9_11:3;
		u64 iq_thr:9;
	} cn52xx;
	struct cvmx_pow_wq_int_thrx_cn63xx {
		u64 reserved_29_63:35;
		u64 tc_en:1;
		u64 tc_thr:4;
		u64 reserved_22_23:2;
		u64 ds_thr:10;
		u64 reserved_10_11:2;
		u64 iq_thr:10;
	} cn63xx;
};

union cvmx_npi_rsl_int_blocks {
	u64 u64;
	struct cvmx_npi_rsl_int_blocks_s {
		u64 reserved_32_63:32;
		u64 rint_31:1;
		u64 iob:1;
		u64 reserved_28_29:2;
		u64 rint_27:1;
		u64 rint_26:1;
		u64 rint_25:1;
		u64 rint_24:1;
		u64 asx1:1;
		u64 asx0:1;
		u64 rint_21:1;
		u64 pip:1;
		u64 spx1:1;
		u64 spx0:1;
		u64 lmc:1;
		u64 l2c:1;
		u64 rint_15:1;
		u64 reserved_13_14:2;
		u64 pow:1;
		u64 tim:1;
		u64 pko:1;
		u64 ipd:1;
		u64 rint_8:1;
		u64 zip:1;
		u64 dfa:1;
		u64 fpa:1;
		u64 key:1;
		u64 npi:1;
		u64 gmx1:1;
		u64 gmx0:1;
		u64 mio:1;
	} s;
	struct cvmx_npi_rsl_int_blocks_cn30xx {
		u64 reserved_32_63:32;
		u64 rint_31:1;
		u64 iob:1;
		u64 rint_29:1;
		u64 rint_28:1;
		u64 rint_27:1;
		u64 rint_26:1;
		u64 rint_25:1;
		u64 rint_24:1;
		u64 asx1:1;
		u64 asx0:1;
		u64 rint_21:1;
		u64 pip:1;
		u64 spx1:1;
		u64 spx0:1;
		u64 lmc:1;
		u64 l2c:1;
		u64 rint_15:1;
		u64 rint_14:1;
		u64 usb:1;
		u64 pow:1;
		u64 tim:1;
		u64 pko:1;
		u64 ipd:1;
		u64 rint_8:1;
		u64 zip:1;
		u64 dfa:1;
		u64 fpa:1;
		u64 key:1;
		u64 npi:1;
		u64 gmx1:1;
		u64 gmx0:1;
		u64 mio:1;
	} cn30xx;
	struct cvmx_npi_rsl_int_blocks_cn38xx {
		u64 reserved_32_63:32;
		u64 rint_31:1;
		u64 iob:1;
		u64 rint_29:1;
		u64 rint_28:1;
		u64 rint_27:1;
		u64 rint_26:1;
		u64 rint_25:1;
		u64 rint_24:1;
		u64 asx1:1;
		u64 asx0:1;
		u64 rint_21:1;
		u64 pip:1;
		u64 spx1:1;
		u64 spx0:1;
		u64 lmc:1;
		u64 l2c:1;
		u64 rint_15:1;
		u64 rint_14:1;
		u64 rint_13:1;
		u64 pow:1;
		u64 tim:1;
		u64 pko:1;
		u64 ipd:1;
		u64 rint_8:1;
		u64 zip:1;
		u64 dfa:1;
		u64 fpa:1;
		u64 key:1;
		u64 npi:1;
		u64 gmx1:1;
		u64 gmx0:1;
		u64 mio:1;
	} cn38xx;
	struct cvmx_npi_rsl_int_blocks_cn50xx {
		u64 reserved_31_63:33;
		u64 iob:1;
		u64 lmc1:1;
		u64 agl:1;
		u64 reserved_24_27:4;
		u64 asx1:1;
		u64 asx0:1;
		u64 reserved_21_21:1;
		u64 pip:1;
		u64 spx1:1;
		u64 spx0:1;
		u64 lmc:1;
		u64 l2c:1;
		u64 reserved_15_15:1;
		u64 rad:1;
		u64 usb:1;
		u64 pow:1;
		u64 tim:1;
		u64 pko:1;
		u64 ipd:1;
		u64 reserved_8_8:1;
		u64 zip:1;
		u64 dfa:1;
		u64 fpa:1;
		u64 key:1;
		u64 npi:1;
		u64 gmx1:1;
		u64 gmx0:1;
		u64 mio:1;
	} cn50xx;
};

union cvmx_pko_command_word0 {
	u64 u64;
	struct {
		u64 total_bytes:16;
		u64 segs:6;
		u64 dontfree:1;
		u64 ignore_i:1;
		u64 ipoffp1:7;
		u64 gather:1;
		u64 rsp:1;
		u64 wqp:1;
		u64 n2:1;
		u64 le:1;
		u64 reg0:11;
		u64 subone0:1;
		u64 reg1:11;
		u64 subone1:1;
		u64 size0:2;
		u64 size1:2;
	} s;
};

union cvmx_ciu_timx {
	u64 u64;
	struct cvmx_ciu_timx_s {
		u64 reserved_37_63:27;
		u64 one_shot:1;
		u64 len:36;
	} s;
};

union cvmx_gmxx_rxx_rx_inbnd {
	u64 u64;
	struct cvmx_gmxx_rxx_rx_inbnd_s {
		u64 status:1;
		u64 speed:2;
		u64 duplex:1;
		u64 reserved_4_63:60;
	} s;
};

static inline int32_t cvmx_fau_fetch_and_add32(enum cvmx_fau_reg_32 reg,
					       int32_t value)
{
	return value;
}

static inline void cvmx_fau_atomic_add32(enum cvmx_fau_reg_32 reg,
					 int32_t value)
{ }

static inline void cvmx_fau_atomic_write32(enum cvmx_fau_reg_32 reg,
					   int32_t value)
{ }

static inline u64 cvmx_scratch_read64(u64 address)
{
	return 0;
}

static inline void cvmx_scratch_write64(u64 address, u64 value)
{ }

static inline int cvmx_wqe_get_grp(struct cvmx_wqe *work)
{
	return 0;
}

static inline void *cvmx_phys_to_ptr(u64 physical_address)
{
	return (void *)(uintptr_t)(physical_address);
}

static inline phys_addr_t cvmx_ptr_to_phys(void *ptr)
{
	return (unsigned long)ptr;
}

static inline int cvmx_helper_get_interface_num(int ipd_port)
{
	return ipd_port;
}

static inline int cvmx_helper_get_interface_index_num(int ipd_port)
{
	return ipd_port;
}

static inline void cvmx_fpa_enable(void)
{ }

static inline u64 cvmx_read_csr(u64 csr_addr)
{
	return 0;
}

static inline void cvmx_write_csr(u64 csr_addr, u64 val)
{ }

static inline int cvmx_helper_setup_red(int pass_thresh, int drop_thresh)
{
	return 0;
}

static inline void *cvmx_fpa_alloc(u64 pool)
{
	return NULL;
}

static inline void cvmx_fpa_free(void *ptr, u64 pool,
				 u64 num_cache_lines)
{ }

static inline int octeon_is_simulation(void)
{
	return 1;
}

static inline void cvmx_pip_get_port_status(u64 port_num, u64 clear,
					    cvmx_pip_port_status_t *status)
{ }

static inline void cvmx_pko_get_port_status(u64 port_num, u64 clear,
					    cvmx_pko_port_status_t *status)
{ }

static inline cvmx_helper_interface_mode_t cvmx_helper_interface_get_mode(int
								   interface)
{
	return 0;
}

static inline union cvmx_helper_link_info cvmx_helper_link_get(int ipd_port)
{
	union cvmx_helper_link_info ret = { .u64 = 0 };

	return ret;
}

static inline int cvmx_helper_link_set(int ipd_port,
				       union cvmx_helper_link_info link_info)
{
	return 0;
}

static inline int cvmx_helper_initialize_packet_io_global(void)
{
	return 0;
}

static inline int cvmx_helper_get_number_of_interfaces(void)
{
	return 2;
}

static inline int cvmx_helper_ports_on_interface(int interface)
{
	return 1;
}

static inline int cvmx_helper_get_ipd_port(int interface, int port)
{
	return 0;
}

static inline int cvmx_helper_ipd_and_packet_input_enable(void)
{
	return 0;
}

static inline void cvmx_ipd_disable(void)
{ }

static inline void cvmx_ipd_free_ptr(void)
{ }

static inline void cvmx_pko_disable(void)
{ }

static inline void cvmx_pko_shutdown(void)
{ }

static inline int cvmx_pko_get_base_queue_per_core(int port, int core)
{
	return port;
}

static inline int cvmx_pko_get_base_queue(int port)
{
	return port;
}

static inline int cvmx_pko_get_num_queues(int port)
{
	return port;
}

static inline unsigned int cvmx_get_core_num(void)
{
	return 0;
}

static inline void cvmx_pow_work_request_async_nocheck(int scr_addr,
						       cvmx_pow_wait_t wait)
{ }

static inline void cvmx_pow_work_request_async(int scr_addr,
					       cvmx_pow_wait_t wait)
{ }

static inline struct cvmx_wqe *cvmx_pow_work_response_async(int scr_addr)
{
	struct cvmx_wqe *wqe = (void *)(unsigned long)scr_addr;

	return wqe;
}

static inline struct cvmx_wqe *cvmx_pow_work_request_sync(cvmx_pow_wait_t wait)
{
	return (void *)(unsigned long)wait;
}

static inline int cvmx_spi_restart_interface(int interface,
					     cvmx_spi_mode_t mode, int timeout)
{
	return 0;
}

static inline void cvmx_fau_async_fetch_and_add32(u64 scraddr,
						  enum cvmx_fau_reg_32 reg,
						  int32_t value)
{ }

static inline union cvmx_gmxx_rxx_rx_inbnd cvmx_spi4000_check_speed(int interface, int port)
{
	union cvmx_gmxx_rxx_rx_inbnd r;

	r.u64 = 0;
	return r;
}

static inline void cvmx_pko_send_packet_prepare(u64 port, u64 queue,
						cvmx_pko_lock_t use_locking)
{ }

static inline cvmx_pko_status_t cvmx_pko_send_packet_finish(u64 port,
		u64 queue, union cvmx_pko_command_word0 pko_command,
		union cvmx_buf_ptr packet, cvmx_pko_lock_t use_locking)
{
	return 0;
}

static inline void cvmx_wqe_set_port(struct cvmx_wqe *work, int port)
{ }

static inline void cvmx_wqe_set_qos(struct cvmx_wqe *work, int qos)
{ }

static inline int cvmx_wqe_get_qos(struct cvmx_wqe *work)
{
	return 0;
}

static inline void cvmx_wqe_set_grp(struct cvmx_wqe *work, int grp)
{ }

static inline void cvmx_pow_work_submit(struct cvmx_wqe *wqp, u32 tag,
					enum cvmx_pow_tag_type tag_type,
					u64 qos, u64 grp)
{ }

#define CVMX_ASXX_RX_CLK_SETX(a, b)	((a) + (b))
#define CVMX_ASXX_TX_CLK_SETX(a, b)	((a) + (b))
#define CVMX_CIU_TIMX(a)		(a)
#define CVMX_GMXX_RXX_ADR_CAM0(a, b)	((a) + (b))
#define CVMX_GMXX_RXX_ADR_CAM1(a, b)	((a) + (b))
#define CVMX_GMXX_RXX_ADR_CAM2(a, b)	((a) + (b))
#define CVMX_GMXX_RXX_ADR_CAM3(a, b)	((a) + (b))
#define CVMX_GMXX_RXX_ADR_CAM4(a, b)	((a) + (b))
#define CVMX_GMXX_RXX_ADR_CAM5(a, b)	((a) + (b))
#define CVMX_GMXX_RXX_FRM_CTL(a, b)	((a) + (b))
#define CVMX_GMXX_RXX_INT_REG(a, b)	((a) + (b))
#define CVMX_GMXX_SMACX(a, b)		((a) + (b))
#define CVMX_PIP_PRT_TAGX(a)		(a)
#define CVMX_POW_PP_GRP_MSKX(a)		(a)
#define CVMX_POW_WQ_INT_THRX(a)		(a)
#define CVMX_SPXX_INT_MSK(a)		(a)
#define CVMX_SPXX_INT_REG(a)		(a)
#define CVMX_SSO_PPX_GRP_MSK(a)		(a)
#define CVMX_SSO_WQ_INT_THRX(a)		(a)
#define CVMX_STXX_INT_MSK(a)		(a)
#define CVMX_STXX_INT_REG(a)		(a)
