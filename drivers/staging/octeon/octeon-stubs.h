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

#define CVMX_SYNCIOBDMA		do { } while(0)

#define CVMX_HELPER_INPUT_TAG_TYPE	0
#define CVMX_HELPER_FIRST_MBUFF_SKIP	7
#define CVMX_FAU_REG_END		(2048)
#define CVMX_FPA_OUTPUT_BUFFER_POOL	    (2)
#define CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE    16
#define CVMX_FPA_PACKET_POOL		    (0)
#define CVMX_FPA_PACKET_POOL_SIZE	    16
#define CVMX_FPA_WQE_POOL		    (1)
#define CVMX_FPA_WQE_POOL_SIZE		    16
#define CVMX_GMXX_RXX_ADR_CAM_EN(a, b)	((a)+(b))
#define CVMX_GMXX_RXX_ADR_CTL(a, b)	((a)+(b))
#define CVMX_GMXX_PRTX_CFG(a, b)	((a)+(b))
#define CVMX_GMXX_RXX_FRM_MAX(a, b)	((a)+(b))
#define CVMX_GMXX_RXX_JABBER(a, b)	((a)+(b))
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
	uint64_t u64;
	struct {
		uint64_t bufs:8;
		uint64_t ip_offset:8;
		uint64_t vlan_valid:1;
		uint64_t vlan_stacked:1;
		uint64_t unassigned:1;
		uint64_t vlan_cfi:1;
		uint64_t vlan_id:12;
		uint64_t pr:4;
		uint64_t unassigned2:8;
		uint64_t dec_ipcomp:1;
		uint64_t tcp_or_udp:1;
		uint64_t dec_ipsec:1;
		uint64_t is_v6:1;
		uint64_t software:1;
		uint64_t L4_error:1;
		uint64_t is_frag:1;
		uint64_t IP_exc:1;
		uint64_t is_bcast:1;
		uint64_t is_mcast:1;
		uint64_t not_IP:1;
		uint64_t rcv_error:1;
		uint64_t err_code:8;
	} s;
	struct {
		uint64_t bufs:8;
		uint64_t ip_offset:8;
		uint64_t vlan_valid:1;
		uint64_t vlan_stacked:1;
		uint64_t unassigned:1;
		uint64_t vlan_cfi:1;
		uint64_t vlan_id:12;
		uint64_t port:12;
		uint64_t dec_ipcomp:1;
		uint64_t tcp_or_udp:1;
		uint64_t dec_ipsec:1;
		uint64_t is_v6:1;
		uint64_t software:1;
		uint64_t L4_error:1;
		uint64_t is_frag:1;
		uint64_t IP_exc:1;
		uint64_t is_bcast:1;
		uint64_t is_mcast:1;
		uint64_t not_IP:1;
		uint64_t rcv_error:1;
		uint64_t err_code:8;
	} s_cn68xx;

	struct {
		uint64_t unused1:16;
		uint64_t vlan:16;
		uint64_t unused2:32;
	} svlan;
	struct {
		uint64_t bufs:8;
		uint64_t unused:8;
		uint64_t vlan_valid:1;
		uint64_t vlan_stacked:1;
		uint64_t unassigned:1;
		uint64_t vlan_cfi:1;
		uint64_t vlan_id:12;
		uint64_t pr:4;
		uint64_t unassigned2:12;
		uint64_t software:1;
		uint64_t unassigned3:1;
		uint64_t is_rarp:1;
		uint64_t is_arp:1;
		uint64_t is_bcast:1;
		uint64_t is_mcast:1;
		uint64_t not_IP:1;
		uint64_t rcv_error:1;
		uint64_t err_code:8;
	} snoip;

};

union cvmx_pip_wqe_word0 {
	struct {
		uint64_t next_ptr:40;
		uint8_t unused;
		__wsum hw_chksum;
	} cn38xx;
	struct {
		uint64_t pknd:6;        /* 0..5 */
		uint64_t unused2:2;     /* 6..7 */
		uint64_t bpid:6;        /* 8..13 */
		uint64_t unused1:18;    /* 14..31 */
		uint64_t l2ptr:8;       /* 32..39 */
		uint64_t l3ptr:8;       /* 40..47 */
		uint64_t unused0:8;     /* 48..55 */
		uint64_t l4ptr:8;       /* 56..63 */
	} cn68xx;
};

union cvmx_wqe_word0 {
	uint64_t u64;
	union cvmx_pip_wqe_word0 pip;
};

union cvmx_wqe_word1 {
	uint64_t u64;
	struct {
		uint64_t tag:32;
		uint64_t tag_type:2;
		uint64_t varies:14;
		uint64_t len:16;
	};
	struct {
		uint64_t tag:32;
		uint64_t tag_type:2;
		uint64_t zero_2:3;
		uint64_t grp:6;
		uint64_t zero_1:1;
		uint64_t qos:3;
		uint64_t zero_0:1;
		uint64_t len:16;
	} cn68xx;
	struct {
		uint64_t tag:32;
		uint64_t tag_type:2;
		uint64_t zero_2:1;
		uint64_t grp:4;
		uint64_t qos:3;
		uint64_t ipprt:6;
		uint64_t len:16;
	} cn38xx;
};

union cvmx_buf_ptr {
	void *ptr;
	uint64_t u64;
	struct {
		uint64_t i:1;
		uint64_t back:4;
		uint64_t pool:3;
		uint64_t size:16;
		uint64_t addr:40;
	} s;
};

struct cvmx_wqe {
	union cvmx_wqe_word0 word0;
	union cvmx_wqe_word1 word1;
	union cvmx_pip_wqe_word2 word2;
	union cvmx_buf_ptr packet_ptr;
	uint8_t packet_data[96];
};

union cvmx_helper_link_info {
	uint64_t u64;
	struct {
		uint64_t reserved_20_63:44;
		uint64_t link_up:1;	    /**< Is the physical link up? */
		uint64_t full_duplex:1;	    /**< 1 if the link is full duplex */
		uint64_t speed:18;	    /**< Speed of the link in Mbps */
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
	uint64_t u64;
	struct cvmx_ipd_ctl_status_s {
		uint64_t reserved_18_63:46;
		uint64_t use_sop:1;
		uint64_t rst_done:1;
		uint64_t clken:1;
		uint64_t no_wptr:1;
		uint64_t pq_apkt:1;
		uint64_t pq_nabuf:1;
		uint64_t ipd_full:1;
		uint64_t pkt_off:1;
		uint64_t len_m8:1;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} s;
	struct cvmx_ipd_ctl_status_cn30xx {
		uint64_t reserved_10_63:54;
		uint64_t len_m8:1;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} cn30xx;
	struct cvmx_ipd_ctl_status_cn38xxp2 {
		uint64_t reserved_9_63:55;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} cn38xxp2;
	struct cvmx_ipd_ctl_status_cn50xx {
		uint64_t reserved_15_63:49;
		uint64_t no_wptr:1;
		uint64_t pq_apkt:1;
		uint64_t pq_nabuf:1;
		uint64_t ipd_full:1;
		uint64_t pkt_off:1;
		uint64_t len_m8:1;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} cn50xx;
	struct cvmx_ipd_ctl_status_cn58xx {
		uint64_t reserved_12_63:52;
		uint64_t ipd_full:1;
		uint64_t pkt_off:1;
		uint64_t len_m8:1;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} cn58xx;
	struct cvmx_ipd_ctl_status_cn63xxp1 {
		uint64_t reserved_16_63:48;
		uint64_t clken:1;
		uint64_t no_wptr:1;
		uint64_t pq_apkt:1;
		uint64_t pq_nabuf:1;
		uint64_t ipd_full:1;
		uint64_t pkt_off:1;
		uint64_t len_m8:1;
		uint64_t reset:1;
		uint64_t addpkt:1;
		uint64_t naddbuf:1;
		uint64_t pkt_lend:1;
		uint64_t wqe_lend:1;
		uint64_t pbp_en:1;
		uint64_t opc_mode:2;
		uint64_t ipd_en:1;
	} cn63xxp1;
};

union cvmx_ipd_sub_port_fcs {
	uint64_t u64;
	struct cvmx_ipd_sub_port_fcs_s {
		uint64_t port_bit:32;
		uint64_t reserved_32_35:4;
		uint64_t port_bit2:4;
		uint64_t reserved_40_63:24;
	} s;
	struct cvmx_ipd_sub_port_fcs_cn30xx {
		uint64_t port_bit:3;
		uint64_t reserved_3_63:61;
	} cn30xx;
	struct cvmx_ipd_sub_port_fcs_cn38xx {
		uint64_t port_bit:32;
		uint64_t reserved_32_63:32;
	} cn38xx;
};

union cvmx_ipd_sub_port_qos_cnt {
	uint64_t u64;
	struct cvmx_ipd_sub_port_qos_cnt_s {
		uint64_t cnt:32;
		uint64_t port_qos:9;
		uint64_t reserved_41_63:23;
	} s;
};
typedef struct {
	uint32_t dropped_octets;
	uint32_t dropped_packets;
	uint32_t pci_raw_packets;
	uint32_t octets;
	uint32_t packets;
	uint32_t multicast_packets;
	uint32_t broadcast_packets;
	uint32_t len_64_packets;
	uint32_t len_65_127_packets;
	uint32_t len_128_255_packets;
	uint32_t len_256_511_packets;
	uint32_t len_512_1023_packets;
	uint32_t len_1024_1518_packets;
	uint32_t len_1519_max_packets;
	uint32_t fcs_align_err_packets;
	uint32_t runt_packets;
	uint32_t runt_crc_packets;
	uint32_t oversize_packets;
	uint32_t oversize_crc_packets;
	uint32_t inb_packets;
	uint64_t inb_octets;
	uint16_t inb_errors;
} cvmx_pip_port_status_t;

typedef struct {
	uint32_t packets;
	uint64_t octets;
	uint64_t doorbell;
} cvmx_pko_port_status_t;

union cvmx_pip_frm_len_chkx {
	uint64_t u64;
	struct cvmx_pip_frm_len_chkx_s {
		uint64_t reserved_32_63:32;
		uint64_t maxlen:16;
		uint64_t minlen:16;
	} s;
};

union cvmx_gmxx_rxx_frm_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_frm_ctl_s {
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t pre_align:1;
		uint64_t null_dis:1;
		uint64_t reserved_11_11:1;
		uint64_t ptp_mode:1;
		uint64_t reserved_13_63:51;
	} s;
	struct cvmx_gmxx_rxx_frm_ctl_cn30xx {
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t reserved_9_63:55;
	} cn30xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn31xx {
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t vlan_len:1;
		uint64_t reserved_8_63:56;
	} cn31xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn50xx {
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t reserved_7_8:2;
		uint64_t pre_align:1;
		uint64_t null_dis:1;
		uint64_t reserved_11_63:53;
	} cn50xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn56xxp1 {
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t reserved_7_8:2;
		uint64_t pre_align:1;
		uint64_t reserved_10_63:54;
	} cn56xxp1;
	struct cvmx_gmxx_rxx_frm_ctl_cn58xx {
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t vlan_len:1;
		uint64_t pad_len:1;
		uint64_t pre_align:1;
		uint64_t null_dis:1;
		uint64_t reserved_11_63:53;
	} cn58xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx {
		uint64_t pre_chk:1;
		uint64_t pre_strp:1;
		uint64_t ctl_drp:1;
		uint64_t ctl_bck:1;
		uint64_t ctl_mcst:1;
		uint64_t ctl_smac:1;
		uint64_t pre_free:1;
		uint64_t reserved_7_8:2;
		uint64_t pre_align:1;
		uint64_t null_dis:1;
		uint64_t reserved_11_11:1;
		uint64_t ptp_mode:1;
		uint64_t reserved_13_63:51;
	} cn61xx;
};

union cvmx_gmxx_rxx_int_reg {
	uint64_t u64;
	struct cvmx_gmxx_rxx_int_reg_s {
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t hg2fld:1;
		uint64_t hg2cc:1;
		uint64_t reserved_29_63:35;
	} s;
	struct cvmx_gmxx_rxx_int_reg_cn30xx {
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t reserved_19_63:45;
	} cn30xx;
	struct cvmx_gmxx_rxx_int_reg_cn50xx {
		uint64_t reserved_0_0:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t reserved_6_6:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t pause_drp:1;
		uint64_t reserved_20_63:44;
	} cn50xx;
	struct cvmx_gmxx_rxx_int_reg_cn52xx {
		uint64_t reserved_0_0:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t reserved_5_6:2;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_9:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t reserved_16_18:3;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t hg2fld:1;
		uint64_t hg2cc:1;
		uint64_t reserved_29_63:35;
	} cn52xx;
	struct cvmx_gmxx_rxx_int_reg_cn56xxp1 {
		uint64_t reserved_0_0:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t reserved_5_6:2;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_9:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t reserved_16_18:3;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t reserved_27_63:37;
	} cn56xxp1;
	struct cvmx_gmxx_rxx_int_reg_cn58xx {
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t maxerr:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t alnerr:1;
		uint64_t lenerr:1;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t niberr:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t phy_link:1;
		uint64_t phy_spd:1;
		uint64_t phy_dupx:1;
		uint64_t pause_drp:1;
		uint64_t reserved_20_63:44;
	} cn58xx;
	struct cvmx_gmxx_rxx_int_reg_cn61xx {
		uint64_t minerr:1;
		uint64_t carext:1;
		uint64_t reserved_2_2:1;
		uint64_t jabber:1;
		uint64_t fcserr:1;
		uint64_t reserved_5_6:2;
		uint64_t rcverr:1;
		uint64_t skperr:1;
		uint64_t reserved_9_9:1;
		uint64_t ovrerr:1;
		uint64_t pcterr:1;
		uint64_t rsverr:1;
		uint64_t falerr:1;
		uint64_t coldet:1;
		uint64_t ifgerr:1;
		uint64_t reserved_16_18:3;
		uint64_t pause_drp:1;
		uint64_t loc_fault:1;
		uint64_t rem_fault:1;
		uint64_t bad_seq:1;
		uint64_t bad_term:1;
		uint64_t unsop:1;
		uint64_t uneop:1;
		uint64_t undat:1;
		uint64_t hg2fld:1;
		uint64_t hg2cc:1;
		uint64_t reserved_29_63:35;
	} cn61xx;
};

union cvmx_gmxx_prtx_cfg {
	uint64_t u64;
	struct cvmx_gmxx_prtx_cfg_s {
		uint64_t reserved_22_63:42;
		uint64_t pknd:6;
		uint64_t reserved_14_15:2;
		uint64_t tx_idle:1;
		uint64_t rx_idle:1;
		uint64_t reserved_9_11:3;
		uint64_t speed_msb:1;
		uint64_t reserved_4_7:4;
		uint64_t slottime:1;
		uint64_t duplex:1;
		uint64_t speed:1;
		uint64_t en:1;
	} s;
	struct cvmx_gmxx_prtx_cfg_cn30xx {
		uint64_t reserved_4_63:60;
		uint64_t slottime:1;
		uint64_t duplex:1;
		uint64_t speed:1;
		uint64_t en:1;
	} cn30xx;
	struct cvmx_gmxx_prtx_cfg_cn52xx {
		uint64_t reserved_14_63:50;
		uint64_t tx_idle:1;
		uint64_t rx_idle:1;
		uint64_t reserved_9_11:3;
		uint64_t speed_msb:1;
		uint64_t reserved_4_7:4;
		uint64_t slottime:1;
		uint64_t duplex:1;
		uint64_t speed:1;
		uint64_t en:1;
	} cn52xx;
};

union cvmx_gmxx_rxx_adr_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_ctl_s {
		uint64_t reserved_4_63:60;
		uint64_t cam_mode:1;
		uint64_t mcst:2;
		uint64_t bcst:1;
	} s;
};

union cvmx_pip_prt_tagx {
	uint64_t u64;
	struct cvmx_pip_prt_tagx_s {
		uint64_t reserved_54_63:10;
		uint64_t portadd_en:1;
		uint64_t inc_hwchk:1;
		uint64_t reserved_50_51:2;
		uint64_t grptagbase_msb:2;
		uint64_t reserved_46_47:2;
		uint64_t grptagmask_msb:2;
		uint64_t reserved_42_43:2;
		uint64_t grp_msb:2;
		uint64_t grptagbase:4;
		uint64_t grptagmask:4;
		uint64_t grptag:1;
		uint64_t grptag_mskip:1;
		uint64_t tag_mode:2;
		uint64_t inc_vs:2;
		uint64_t inc_vlan:1;
		uint64_t inc_prt_flag:1;
		uint64_t ip6_dprt_flag:1;
		uint64_t ip4_dprt_flag:1;
		uint64_t ip6_sprt_flag:1;
		uint64_t ip4_sprt_flag:1;
		uint64_t ip6_nxth_flag:1;
		uint64_t ip4_pctl_flag:1;
		uint64_t ip6_dst_flag:1;
		uint64_t ip4_dst_flag:1;
		uint64_t ip6_src_flag:1;
		uint64_t ip4_src_flag:1;
		uint64_t tcp6_tag_type:2;
		uint64_t tcp4_tag_type:2;
		uint64_t ip6_tag_type:2;
		uint64_t ip4_tag_type:2;
		uint64_t non_tag_type:2;
		uint64_t grp:4;
	} s;
	struct cvmx_pip_prt_tagx_cn30xx {
		uint64_t reserved_40_63:24;
		uint64_t grptagbase:4;
		uint64_t grptagmask:4;
		uint64_t grptag:1;
		uint64_t reserved_30_30:1;
		uint64_t tag_mode:2;
		uint64_t inc_vs:2;
		uint64_t inc_vlan:1;
		uint64_t inc_prt_flag:1;
		uint64_t ip6_dprt_flag:1;
		uint64_t ip4_dprt_flag:1;
		uint64_t ip6_sprt_flag:1;
		uint64_t ip4_sprt_flag:1;
		uint64_t ip6_nxth_flag:1;
		uint64_t ip4_pctl_flag:1;
		uint64_t ip6_dst_flag:1;
		uint64_t ip4_dst_flag:1;
		uint64_t ip6_src_flag:1;
		uint64_t ip4_src_flag:1;
		uint64_t tcp6_tag_type:2;
		uint64_t tcp4_tag_type:2;
		uint64_t ip6_tag_type:2;
		uint64_t ip4_tag_type:2;
		uint64_t non_tag_type:2;
		uint64_t grp:4;
	} cn30xx;
	struct cvmx_pip_prt_tagx_cn50xx {
		uint64_t reserved_40_63:24;
		uint64_t grptagbase:4;
		uint64_t grptagmask:4;
		uint64_t grptag:1;
		uint64_t grptag_mskip:1;
		uint64_t tag_mode:2;
		uint64_t inc_vs:2;
		uint64_t inc_vlan:1;
		uint64_t inc_prt_flag:1;
		uint64_t ip6_dprt_flag:1;
		uint64_t ip4_dprt_flag:1;
		uint64_t ip6_sprt_flag:1;
		uint64_t ip4_sprt_flag:1;
		uint64_t ip6_nxth_flag:1;
		uint64_t ip4_pctl_flag:1;
		uint64_t ip6_dst_flag:1;
		uint64_t ip4_dst_flag:1;
		uint64_t ip6_src_flag:1;
		uint64_t ip4_src_flag:1;
		uint64_t tcp6_tag_type:2;
		uint64_t tcp4_tag_type:2;
		uint64_t ip6_tag_type:2;
		uint64_t ip4_tag_type:2;
		uint64_t non_tag_type:2;
		uint64_t grp:4;
	} cn50xx;
};

union cvmx_spxx_int_reg {
	uint64_t u64;
	struct cvmx_spxx_int_reg_s {
		uint64_t reserved_32_63:32;
		uint64_t spf:1;
		uint64_t reserved_12_30:19;
		uint64_t calerr:1;
		uint64_t syncerr:1;
		uint64_t diperr:1;
		uint64_t tpaovr:1;
		uint64_t rsverr:1;
		uint64_t drwnng:1;
		uint64_t clserr:1;
		uint64_t spiovr:1;
		uint64_t reserved_2_3:2;
		uint64_t abnorm:1;
		uint64_t prtnxa:1;
	} s;
};

union cvmx_spxx_int_msk {
	uint64_t u64;
	struct cvmx_spxx_int_msk_s {
		uint64_t reserved_12_63:52;
		uint64_t calerr:1;
		uint64_t syncerr:1;
		uint64_t diperr:1;
		uint64_t tpaovr:1;
		uint64_t rsverr:1;
		uint64_t drwnng:1;
		uint64_t clserr:1;
		uint64_t spiovr:1;
		uint64_t reserved_2_3:2;
		uint64_t abnorm:1;
		uint64_t prtnxa:1;
	} s;
};

union cvmx_pow_wq_int {
	uint64_t u64;
	struct cvmx_pow_wq_int_s {
		uint64_t wq_int:16;
		uint64_t iq_dis:16;
		uint64_t reserved_32_63:32;
	} s;
};

union cvmx_sso_wq_int_thrx {
	uint64_t u64;
	struct {
		uint64_t iq_thr:12;
		uint64_t reserved_12_13:2;
		uint64_t ds_thr:12;
		uint64_t reserved_26_27:2;
		uint64_t tc_thr:4;
		uint64_t tc_en:1;
		uint64_t reserved_33_63:31;
	} s;
};

union cvmx_stxx_int_reg {
	uint64_t u64;
	struct cvmx_stxx_int_reg_s {
		uint64_t reserved_9_63:55;
		uint64_t syncerr:1;
		uint64_t frmerr:1;
		uint64_t unxfrm:1;
		uint64_t nosync:1;
		uint64_t diperr:1;
		uint64_t datovr:1;
		uint64_t ovrbst:1;
		uint64_t calpar1:1;
		uint64_t calpar0:1;
	} s;
};

union cvmx_stxx_int_msk {
	uint64_t u64;
	struct cvmx_stxx_int_msk_s {
		uint64_t reserved_8_63:56;
		uint64_t frmerr:1;
		uint64_t unxfrm:1;
		uint64_t nosync:1;
		uint64_t diperr:1;
		uint64_t datovr:1;
		uint64_t ovrbst:1;
		uint64_t calpar1:1;
		uint64_t calpar0:1;
	} s;
};

union cvmx_pow_wq_int_pc {
	uint64_t u64;
	struct cvmx_pow_wq_int_pc_s {
		uint64_t reserved_0_7:8;
		uint64_t pc_thr:20;
		uint64_t reserved_28_31:4;
		uint64_t pc:28;
		uint64_t reserved_60_63:4;
	} s;
};

union cvmx_pow_wq_int_thrx {
	uint64_t u64;
	struct cvmx_pow_wq_int_thrx_s {
		uint64_t reserved_29_63:35;
		uint64_t tc_en:1;
		uint64_t tc_thr:4;
		uint64_t reserved_23_23:1;
		uint64_t ds_thr:11;
		uint64_t reserved_11_11:1;
		uint64_t iq_thr:11;
	} s;
	struct cvmx_pow_wq_int_thrx_cn30xx {
		uint64_t reserved_29_63:35;
		uint64_t tc_en:1;
		uint64_t tc_thr:4;
		uint64_t reserved_18_23:6;
		uint64_t ds_thr:6;
		uint64_t reserved_6_11:6;
		uint64_t iq_thr:6;
	} cn30xx;
	struct cvmx_pow_wq_int_thrx_cn31xx {
		uint64_t reserved_29_63:35;
		uint64_t tc_en:1;
		uint64_t tc_thr:4;
		uint64_t reserved_20_23:4;
		uint64_t ds_thr:8;
		uint64_t reserved_8_11:4;
		uint64_t iq_thr:8;
	} cn31xx;
	struct cvmx_pow_wq_int_thrx_cn52xx {
		uint64_t reserved_29_63:35;
		uint64_t tc_en:1;
		uint64_t tc_thr:4;
		uint64_t reserved_21_23:3;
		uint64_t ds_thr:9;
		uint64_t reserved_9_11:3;
		uint64_t iq_thr:9;
	} cn52xx;
	struct cvmx_pow_wq_int_thrx_cn63xx {
		uint64_t reserved_29_63:35;
		uint64_t tc_en:1;
		uint64_t tc_thr:4;
		uint64_t reserved_22_23:2;
		uint64_t ds_thr:10;
		uint64_t reserved_10_11:2;
		uint64_t iq_thr:10;
	} cn63xx;
};

union cvmx_npi_rsl_int_blocks {
	uint64_t u64;
	struct cvmx_npi_rsl_int_blocks_s {
		uint64_t reserved_32_63:32;
		uint64_t rint_31:1;
		uint64_t iob:1;
		uint64_t reserved_28_29:2;
		uint64_t rint_27:1;
		uint64_t rint_26:1;
		uint64_t rint_25:1;
		uint64_t rint_24:1;
		uint64_t asx1:1;
		uint64_t asx0:1;
		uint64_t rint_21:1;
		uint64_t pip:1;
		uint64_t spx1:1;
		uint64_t spx0:1;
		uint64_t lmc:1;
		uint64_t l2c:1;
		uint64_t rint_15:1;
		uint64_t reserved_13_14:2;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t rint_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t npi:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
	} s;
	struct cvmx_npi_rsl_int_blocks_cn30xx {
		uint64_t reserved_32_63:32;
		uint64_t rint_31:1;
		uint64_t iob:1;
		uint64_t rint_29:1;
		uint64_t rint_28:1;
		uint64_t rint_27:1;
		uint64_t rint_26:1;
		uint64_t rint_25:1;
		uint64_t rint_24:1;
		uint64_t asx1:1;
		uint64_t asx0:1;
		uint64_t rint_21:1;
		uint64_t pip:1;
		uint64_t spx1:1;
		uint64_t spx0:1;
		uint64_t lmc:1;
		uint64_t l2c:1;
		uint64_t rint_15:1;
		uint64_t rint_14:1;
		uint64_t usb:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t rint_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t npi:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
	} cn30xx;
	struct cvmx_npi_rsl_int_blocks_cn38xx {
		uint64_t reserved_32_63:32;
		uint64_t rint_31:1;
		uint64_t iob:1;
		uint64_t rint_29:1;
		uint64_t rint_28:1;
		uint64_t rint_27:1;
		uint64_t rint_26:1;
		uint64_t rint_25:1;
		uint64_t rint_24:1;
		uint64_t asx1:1;
		uint64_t asx0:1;
		uint64_t rint_21:1;
		uint64_t pip:1;
		uint64_t spx1:1;
		uint64_t spx0:1;
		uint64_t lmc:1;
		uint64_t l2c:1;
		uint64_t rint_15:1;
		uint64_t rint_14:1;
		uint64_t rint_13:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t rint_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t npi:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
	} cn38xx;
	struct cvmx_npi_rsl_int_blocks_cn50xx {
		uint64_t reserved_31_63:33;
		uint64_t iob:1;
		uint64_t lmc1:1;
		uint64_t agl:1;
		uint64_t reserved_24_27:4;
		uint64_t asx1:1;
		uint64_t asx0:1;
		uint64_t reserved_21_21:1;
		uint64_t pip:1;
		uint64_t spx1:1;
		uint64_t spx0:1;
		uint64_t lmc:1;
		uint64_t l2c:1;
		uint64_t reserved_15_15:1;
		uint64_t rad:1;
		uint64_t usb:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t reserved_8_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t npi:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
	} cn50xx;
};

union cvmx_pko_command_word0 {
	uint64_t u64;
	struct {
		uint64_t total_bytes:16;
		uint64_t segs:6;
		uint64_t dontfree:1;
		uint64_t ignore_i:1;
		uint64_t ipoffp1:7;
		uint64_t gather:1;
		uint64_t rsp:1;
		uint64_t wqp:1;
		uint64_t n2:1;
		uint64_t le:1;
		uint64_t reg0:11;
		uint64_t subone0:1;
		uint64_t reg1:11;
		uint64_t subone1:1;
		uint64_t size0:2;
		uint64_t size1:2;
	} s;
};

union cvmx_ciu_timx {
	uint64_t u64;
	struct cvmx_ciu_timx_s {
		uint64_t reserved_37_63:27;
		uint64_t one_shot:1;
		uint64_t len:36;
	} s;
};

union cvmx_gmxx_rxx_rx_inbnd {
	uint64_t u64;
	struct cvmx_gmxx_rxx_rx_inbnd_s {
		uint64_t status:1;
		uint64_t speed:2;
		uint64_t duplex:1;
		uint64_t reserved_4_63:60;
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

static inline uint64_t cvmx_scratch_read64(uint64_t address)
{
	return 0;
}

static inline void cvmx_scratch_write64(uint64_t address, uint64_t value)
{ }

static inline int cvmx_wqe_get_grp(struct cvmx_wqe *work)
{
	return 0;
}

static inline void *cvmx_phys_to_ptr(uint64_t physical_address)
{
	return (void *)(uintptr_t)(physical_address);
}

static inline uint64_t cvmx_ptr_to_phys(void *ptr)
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

static inline uint64_t cvmx_read_csr(uint64_t csr_addr)
{
	return 0;
}

static inline void cvmx_write_csr(uint64_t csr_addr, uint64_t val)
{ }

static inline int cvmx_helper_setup_red(int pass_thresh, int drop_thresh)
{
	return 0;
}

static inline void *cvmx_fpa_alloc(uint64_t pool)
{
	return NULL;
}

static inline void cvmx_fpa_free(void *ptr, uint64_t pool,
				 uint64_t num_cache_lines)
{ }

static inline int octeon_is_simulation(void)
{
	return 1;
}

static inline void cvmx_pip_get_port_status(uint64_t port_num, uint64_t clear,
					    cvmx_pip_port_status_t *status)
{ }

static inline void cvmx_pko_get_port_status(uint64_t port_num, uint64_t clear,
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

static inline void cvmx_fau_async_fetch_and_add32(uint64_t scraddr,
						  enum cvmx_fau_reg_32 reg,
						  int32_t value)
{ }

static inline union cvmx_gmxx_rxx_rx_inbnd cvmx_spi4000_check_speed(
	int interface,
	int port)
{
	union cvmx_gmxx_rxx_rx_inbnd r;

	r.u64 = 0;
	return r;
}

static inline void cvmx_pko_send_packet_prepare(uint64_t port, uint64_t queue,
						cvmx_pko_lock_t use_locking)
{ }

static inline cvmx_pko_status_t cvmx_pko_send_packet_finish(uint64_t port,
		uint64_t queue, union cvmx_pko_command_word0 pko_command,
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

static inline void cvmx_pow_work_submit(struct cvmx_wqe *wqp, uint32_t tag,
					enum cvmx_pow_tag_type tag_type,
					uint64_t qos, uint64_t grp)
{ }

#define CVMX_ASXX_RX_CLK_SETX(a, b)	((a)+(b))
#define CVMX_ASXX_TX_CLK_SETX(a, b)	((a)+(b))
#define CVMX_CIU_TIMX(a)		(a)
#define CVMX_GMXX_RXX_ADR_CAM0(a, b)	((a)+(b))
#define CVMX_GMXX_RXX_ADR_CAM1(a, b)	((a)+(b))
#define CVMX_GMXX_RXX_ADR_CAM2(a, b)	((a)+(b))
#define CVMX_GMXX_RXX_ADR_CAM3(a, b)	((a)+(b))
#define CVMX_GMXX_RXX_ADR_CAM4(a, b)	((a)+(b))
#define CVMX_GMXX_RXX_ADR_CAM5(a, b)	((a)+(b))
#define CVMX_GMXX_RXX_FRM_CTL(a, b)	((a)+(b))
#define CVMX_GMXX_RXX_INT_REG(a, b)	((a)+(b))
#define CVMX_GMXX_SMACX(a, b)		((a)+(b))
#define CVMX_PIP_PRT_TAGX(a)		(a)
#define CVMX_POW_PP_GRP_MSKX(a)		(a)
#define CVMX_POW_WQ_INT_THRX(a)		(a)
#define CVMX_SPXX_INT_MSK(a)		(a)
#define CVMX_SPXX_INT_REG(a)		(a)
#define CVMX_SSO_PPX_GRP_MSK(a)		(a)
#define CVMX_SSO_WQ_INT_THRX(a)		(a)
#define CVMX_STXX_INT_MSK(a)		(a)
#define CVMX_STXX_INT_REG(a)		(a)
