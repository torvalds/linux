/*
 *  linux/drivers/net/ehea/ehea_phyp.h
 *
 *  eHEA ethernet device driver for IBM eServer System p
 *
 *  (C) Copyright IBM Corp. 2006
 *
 *  Authors:
 *       Christoph Raisch <raisch@de.ibm.com>
 *       Jan-Bernd Themann <themann@de.ibm.com>
 *       Thomas Klein <tklein@de.ibm.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __EHEA_PHYP_H__
#define __EHEA_PHYP_H__

#include <linux/delay.h>
#include <asm/hvcall.h>
#include "ehea.h"
#include "ehea_hw.h"
#include "ehea_hcall.h"

/* Some abbreviations used here:
 *
 * hcp_*  - structures, variables and functions releated to Hypervisor Calls
 */

static inline u32 get_longbusy_msecs(int long_busy_ret_code)
{
	switch (long_busy_ret_code) {
	case H_LONG_BUSY_ORDER_1_MSEC:
		return 1;
	case H_LONG_BUSY_ORDER_10_MSEC:
		return 10;
	case H_LONG_BUSY_ORDER_100_MSEC:
		return 100;
	case H_LONG_BUSY_ORDER_1_SEC:
		return 1000;
	case H_LONG_BUSY_ORDER_10_SEC:
		return 10000;
	case H_LONG_BUSY_ORDER_100_SEC:
		return 100000;
	default:
		return 1;
	}
}

/* Number of pages which can be registered at once by H_REGISTER_HEA_RPAGES */
#define EHEA_MAX_RPAGE 512

/* Notification Event Queue (NEQ) Entry bit masks */
#define NEQE_EVENT_CODE		EHEA_BMASK_IBM(2, 7)
#define NEQE_PORTNUM  		EHEA_BMASK_IBM(32, 47)
#define NEQE_PORT_UP		EHEA_BMASK_IBM(16, 16)
#define NEQE_EXTSWITCH_PORT_UP	EHEA_BMASK_IBM(17, 17)
#define NEQE_EXTSWITCH_PRIMARY	EHEA_BMASK_IBM(18, 18)
#define NEQE_PLID		EHEA_BMASK_IBM(16, 47)

/* Notification Event Codes */
#define EHEA_EC_PORTSTATE_CHG	0x30
#define EHEA_EC_ADAPTER_MALFUNC	0x32
#define EHEA_EC_PORT_MALFUNC	0x33

/* Notification Event Log Register (NELR) bit masks */
#define NELR_PORT_MALFUNC	EHEA_BMASK_IBM(61, 61)
#define NELR_ADAPTER_MALFUNC	EHEA_BMASK_IBM(62, 62)
#define NELR_PORTSTATE_CHG	EHEA_BMASK_IBM(63, 63)

static inline void hcp_epas_ctor(struct h_epas *epas, u64 paddr_kernel,
				 u64 paddr_user)
{
	/* To support 64k pages we must round to 64k page boundary */
	epas->kernel.addr = ioremap((paddr_kernel & PAGE_MASK), PAGE_SIZE) +
			    (paddr_kernel & ~PAGE_MASK);
	epas->user.addr = paddr_user;
}

static inline void hcp_epas_dtor(struct h_epas *epas)
{
	if (epas->kernel.addr)
		iounmap((void __iomem *)((u64)epas->kernel.addr & PAGE_MASK));

	epas->user.addr = 0;
	epas->kernel.addr = 0;
}

struct hcp_modify_qp_cb0 {
	u64 qp_ctl_reg;		/* 00 */
	u32 max_swqe;		/* 02 */
	u32 max_rwqe;		/* 03 */
	u32 port_nb;		/* 04 */
	u32 reserved0;		/* 05 */
	u64 qp_aer;		/* 06 */
	u64 qp_tenure;		/* 08 */
};

/* Hcall Query/Modify Queue Pair Control Block 0 Selection Mask Bits */
#define H_QPCB0_ALL             EHEA_BMASK_IBM(0, 5)
#define H_QPCB0_QP_CTL_REG      EHEA_BMASK_IBM(0, 0)
#define H_QPCB0_MAX_SWQE        EHEA_BMASK_IBM(1, 1)
#define H_QPCB0_MAX_RWQE        EHEA_BMASK_IBM(2, 2)
#define H_QPCB0_PORT_NB         EHEA_BMASK_IBM(3, 3)
#define H_QPCB0_QP_AER          EHEA_BMASK_IBM(4, 4)
#define H_QPCB0_QP_TENURE       EHEA_BMASK_IBM(5, 5)

/* Queue Pair Control Register Status Bits */
#define H_QP_CR_ENABLED		    0x8000000000000000ULL /* QP enabled */
							  /* QP States: */
#define H_QP_CR_STATE_RESET	    0x0000010000000000ULL /*  Reset */
#define H_QP_CR_STATE_INITIALIZED   0x0000020000000000ULL /*  Initialized */
#define H_QP_CR_STATE_RDY2RCV	    0x0000030000000000ULL /*  Ready to recv */
#define H_QP_CR_STATE_RDY2SND	    0x0000050000000000ULL /*  Ready to send */
#define H_QP_CR_STATE_ERROR	    0x0000800000000000ULL /*  Error */
#define H_QP_CR_RES_STATE 	    0x0000007F00000000ULL /* Resultant state */

struct hcp_modify_qp_cb1 {
	u32 qpn;		/* 00 */
	u32 qp_asyn_ev_eq_nb;	/* 01 */
	u64 sq_cq_handle;	/* 02 */
	u64 rq_cq_handle;	/* 04 */
	/* sgel = scatter gather element */
	u32 sgel_nb_sq;		/* 06 */
	u32 sgel_nb_rq1;	/* 07 */
	u32 sgel_nb_rq2;	/* 08 */
	u32 sgel_nb_rq3;	/* 09 */
};

/* Hcall Query/Modify Queue Pair Control Block 1 Selection Mask Bits */
#define H_QPCB1_ALL             EHEA_BMASK_IBM(0, 7)
#define H_QPCB1_QPN             EHEA_BMASK_IBM(0, 0)
#define H_QPCB1_ASYN_EV_EQ_NB   EHEA_BMASK_IBM(1, 1)
#define H_QPCB1_SQ_CQ_HANDLE    EHEA_BMASK_IBM(2, 2)
#define H_QPCB1_RQ_CQ_HANDLE    EHEA_BMASK_IBM(3, 3)
#define H_QPCB1_SGEL_NB_SQ      EHEA_BMASK_IBM(4, 4)
#define H_QPCB1_SGEL_NB_RQ1     EHEA_BMASK_IBM(5, 5)
#define H_QPCB1_SGEL_NB_RQ2     EHEA_BMASK_IBM(6, 6)
#define H_QPCB1_SGEL_NB_RQ3     EHEA_BMASK_IBM(7, 7)

struct hcp_query_ehea {
	u32 cur_num_qps;		/* 00 */
	u32 cur_num_cqs;		/* 01 */
	u32 cur_num_eqs;		/* 02 */
	u32 cur_num_mrs;		/* 03 */
	u32 auth_level;			/* 04 */
	u32 max_num_qps;		/* 05 */
	u32 max_num_cqs;		/* 06 */
	u32 max_num_eqs;		/* 07 */
	u32 max_num_mrs;		/* 08 */
	u32 reserved0;			/* 09 */
	u32 int_clock_freq;		/* 10 */
	u32 max_num_pds;		/* 11 */
	u32 max_num_addr_handles;	/* 12 */
	u32 max_num_cqes;		/* 13 */
	u32 max_num_wqes;		/* 14 */
	u32 max_num_sgel_rq1wqe;	/* 15 */
	u32 max_num_sgel_rq2wqe;	/* 16 */
	u32 max_num_sgel_rq3wqe;	/* 17 */
	u32 mr_page_size;		/* 18 */
	u32 reserved1;			/* 19 */
	u64 max_mr_size;		/* 20 */
	u64 reserved2;			/* 22 */
	u32 num_ports;			/* 24 */
	u32 reserved3;			/* 25 */
	u32 reserved4;			/* 26 */
	u32 reserved5;			/* 27 */
	u64 max_mc_mac;			/* 28 */
	u64 ehea_cap;			/* 30 */
	u32 max_isn_per_eq;		/* 32 */
	u32 max_num_neq;		/* 33 */
	u64 max_num_vlan_ids;		/* 34 */
	u32 max_num_port_group;		/* 36 */
	u32 max_num_phys_port;		/* 37 */

};

/* Hcall Query/Modify Port Control Block defines */
#define H_PORT_CB0	 0
#define H_PORT_CB1	 1
#define H_PORT_CB2	 2
#define H_PORT_CB3	 3
#define H_PORT_CB4	 4
#define H_PORT_CB5	 5
#define H_PORT_CB6	 6
#define H_PORT_CB7	 7

struct hcp_ehea_port_cb0 {
	u64 port_mac_addr;
	u64 port_rc;
	u64 reserved0;
	u32 port_op_state;
	u32 port_speed;
	u32 ext_swport_op_state;
	u32 neg_tpf_prpf;
	u32 num_default_qps;
	u32 reserved1;
	u64 default_qpn_arr[16];
};

/* Hcall Query/Modify Port Control Block 0 Selection Mask Bits */
#define H_PORT_CB0_ALL		EHEA_BMASK_IBM(0, 7)    /* Set all bits */
#define H_PORT_CB0_MAC		EHEA_BMASK_IBM(0, 0)    /* MAC address */
#define H_PORT_CB0_PRC		EHEA_BMASK_IBM(1, 1)    /* Port Recv Control */
#define H_PORT_CB0_DEFQPNARRAY	EHEA_BMASK_IBM(7, 7)    /* Default QPN Array */

/*  Hcall Query Port: Returned port speed values */
#define H_SPEED_10M_H	1	/*  10 Mbps, Half Duplex */
#define H_SPEED_10M_F	2	/*  10 Mbps, Full Duplex */
#define H_SPEED_100M_H	3	/* 100 Mbps, Half Duplex */
#define H_SPEED_100M_F	4	/* 100 Mbps, Full Duplex */
#define H_SPEED_1G_F	6	/*   1 Gbps, Full Duplex */
#define H_SPEED_10G_F	8	/*  10 Gbps, Full Duplex */

/* Port Receive Control Status Bits */
#define PXLY_RC_VALID           EHEA_BMASK_IBM(49, 49)
#define PXLY_RC_VLAN_XTRACT     EHEA_BMASK_IBM(50, 50)
#define PXLY_RC_TCP_6_TUPLE     EHEA_BMASK_IBM(51, 51)
#define PXLY_RC_UDP_6_TUPLE     EHEA_BMASK_IBM(52, 52)
#define PXLY_RC_TCP_3_TUPLE     EHEA_BMASK_IBM(53, 53)
#define PXLY_RC_TCP_2_TUPLE     EHEA_BMASK_IBM(54, 54)
#define PXLY_RC_LLC_SNAP        EHEA_BMASK_IBM(55, 55)
#define PXLY_RC_JUMBO_FRAME     EHEA_BMASK_IBM(56, 56)
#define PXLY_RC_FRAG_IP_PKT     EHEA_BMASK_IBM(57, 57)
#define PXLY_RC_TCP_UDP_CHKSUM  EHEA_BMASK_IBM(58, 58)
#define PXLY_RC_IP_CHKSUM       EHEA_BMASK_IBM(59, 59)
#define PXLY_RC_MAC_FILTER      EHEA_BMASK_IBM(60, 60)
#define PXLY_RC_UNTAG_FILTER    EHEA_BMASK_IBM(61, 61)
#define PXLY_RC_VLAN_TAG_FILTER EHEA_BMASK_IBM(62, 63)

#define PXLY_RC_VLAN_FILTER     2
#define PXLY_RC_VLAN_PERM       0


#define H_PORT_CB1_ALL          0x8000000000000000ULL

struct hcp_ehea_port_cb1 {
	u64 vlan_filter[64];
};

#define H_PORT_CB2_ALL          0xFFE0000000000000ULL

struct hcp_ehea_port_cb2 {
	u64 rxo;
	u64 rxucp;
	u64 rxufd;
	u64 rxuerr;
	u64 rxftl;
	u64 rxmcp;
	u64 rxbcp;
	u64 txo;
	u64 txucp;
	u64 txmcp;
	u64 txbcp;
};

struct hcp_ehea_port_cb3 {
	u64 vlan_bc_filter[64];
	u64 vlan_mc_filter[64];
	u64 vlan_un_filter[64];
	u64 port_mac_hash_array[64];
};

#define H_PORT_CB4_ALL          0xF000000000000000ULL
#define H_PORT_CB4_JUMBO        0x1000000000000000ULL
#define H_PORT_CB4_SPEED        0x8000000000000000ULL

struct hcp_ehea_port_cb4 {
	u32 port_speed;
	u32 pause_frame;
	u32 ens_port_op_state;
	u32 jumbo_frame;
	u32 ens_port_wrap;
};

/* Hcall Query/Modify Port Control Block 5 Selection Mask Bits */
#define H_PORT_CB5_RCU		0x0001000000000000ULL
#define PXS_RCU			EHEA_BMASK_IBM(61, 63)

struct hcp_ehea_port_cb5 {
	u64 prc;	        /* 00 */
	u64 uaa;		/* 01 */
	u64 macvc;		/* 02 */
	u64 xpcsc;		/* 03 */
	u64 xpcsp;		/* 04 */
	u64 pcsid;		/* 05 */
	u64 xpcsst;		/* 06 */
	u64 pthlb;		/* 07 */
	u64 pthrb;		/* 08 */
	u64 pqu;		/* 09 */
	u64 pqd;		/* 10 */
	u64 prt;		/* 11 */
	u64 wsth;		/* 12 */
	u64 rcb;		/* 13 */
	u64 rcm;		/* 14 */
	u64 rcu;		/* 15 */
	u64 macc;		/* 16 */
	u64 pc;			/* 17 */
	u64 pst;		/* 18 */
	u64 ducqpn;		/* 19 */
	u64 mcqpn;		/* 20 */
	u64 mma;		/* 21 */
	u64 pmc0h;		/* 22 */
	u64 pmc0l;		/* 23 */
	u64 lbc;		/* 24 */
};

#define H_PORT_CB6_ALL  0xFFFFFE7FFFFF8000ULL

struct hcp_ehea_port_cb6 {
	u64 rxo;		/* 00 */
	u64 rx64;		/* 01 */
	u64 rx65;		/* 02 */
	u64 rx128;		/* 03 */
	u64 rx256;		/* 04 */
	u64 rx512;		/* 05 */
	u64 rx1024;		/* 06 */
	u64 rxbfcs;		/* 07 */
	u64 rxime;		/* 08 */
	u64 rxrle;		/* 09 */
	u64 rxorle;		/* 10 */
	u64 rxftl;		/* 11 */
	u64 rxjab;		/* 12 */
	u64 rxse;		/* 13 */
	u64 rxce;		/* 14 */
	u64 rxrf;		/* 15 */
	u64 rxfrag;		/* 16 */
	u64 rxuoc;		/* 17 */
	u64 rxcpf;		/* 18 */
	u64 rxsb;		/* 19 */
	u64 rxfd;		/* 20 */
	u64 rxoerr;		/* 21 */
	u64 rxaln;		/* 22 */
	u64 ducqpn;		/* 23 */
	u64 reserved0;		/* 24 */
	u64 rxmcp;		/* 25 */
	u64 rxbcp;		/* 26 */
	u64 txmcp;		/* 27 */
	u64 txbcp;		/* 28 */
	u64 txo;		/* 29 */
	u64 tx64;		/* 30 */
	u64 tx65;		/* 31 */
	u64 tx128;		/* 32 */
	u64 tx256;		/* 33 */
	u64 tx512;		/* 34 */
	u64 tx1024;		/* 35 */
	u64 txbfcs;		/* 36 */
	u64 txcpf;		/* 37 */
	u64 txlf;		/* 38 */
	u64 txrf;		/* 39 */
	u64 txime;		/* 40 */
	u64 txsc;		/* 41 */
	u64 txmc;		/* 42 */
	u64 txsqe;		/* 43 */
	u64 txdef;		/* 44 */
	u64 txlcol;		/* 45 */
	u64 txexcol;		/* 46 */
	u64 txcse;		/* 47 */
	u64 txbor;		/* 48 */
};

#define H_PORT_CB7_DUCQPN 0x8000000000000000ULL

struct hcp_ehea_port_cb7 {
	u64 def_uc_qpn;
};

u64 ehea_h_query_ehea_qp(const u64 adapter_handle,
			 const u8 qp_category,
			 const u64 qp_handle, const u64 sel_mask,
			 void *cb_addr);

u64 ehea_h_modify_ehea_qp(const u64 adapter_handle,
			  const u8 cat,
			  const u64 qp_handle,
			  const u64 sel_mask,
			  void *cb_addr,
			  u64 *inv_attr_id,
			  u64 *proc_mask, u16 *out_swr, u16 *out_rwr);

u64 ehea_h_alloc_resource_eq(const u64 adapter_handle,
			     struct ehea_eq_attr *eq_attr, u64 *eq_handle);

u64 ehea_h_alloc_resource_cq(const u64 adapter_handle,
			     struct ehea_cq_attr *cq_attr,
			     u64 *cq_handle, struct h_epas *epas);

u64 ehea_h_alloc_resource_qp(const u64 adapter_handle,
			     struct ehea_qp_init_attr *init_attr,
			     const u32 pd,
			     u64 *qp_handle, struct h_epas *h_epas);

#define H_REG_RPAGE_PAGE_SIZE          EHEA_BMASK_IBM(48, 55)
#define H_REG_RPAGE_QT                 EHEA_BMASK_IBM(62, 63)

u64 ehea_h_register_rpage(const u64 adapter_handle,
			  const u8 pagesize,
			  const u8 queue_type,
			  const u64 resource_handle,
			  const u64 log_pageaddr, u64 count);

#define H_DISABLE_GET_EHEA_WQE_P  1
#define H_DISABLE_GET_SQ_WQE_P    2
#define H_DISABLE_GET_RQC         3

u64 ehea_h_disable_and_get_hea(const u64 adapter_handle, const u64 qp_handle);

#define FORCE_FREE 1
#define NORMAL_FREE 0

u64 ehea_h_free_resource(const u64 adapter_handle, const u64 res_handle,
			 u64 force_bit);

u64 ehea_h_alloc_resource_mr(const u64 adapter_handle, const u64 vaddr,
			     const u64 length, const u32 access_ctrl,
			     const u32 pd, u64 *mr_handle, u32 *lkey);

u64 ehea_h_register_rpage_mr(const u64 adapter_handle, const u64 mr_handle,
			     const u8 pagesize, const u8 queue_type,
			     const u64 log_pageaddr, const u64 count);

u64 ehea_h_register_smr(const u64 adapter_handle, const u64 orig_mr_handle,
			const u64 vaddr_in, const u32 access_ctrl, const u32 pd,
			struct ehea_mr *mr);

u64 ehea_h_query_ehea(const u64 adapter_handle, void *cb_addr);

/* output param R5 */
#define H_MEHEAPORT_CAT		EHEA_BMASK_IBM(40, 47)
#define H_MEHEAPORT_PN		EHEA_BMASK_IBM(48, 63)

u64 ehea_h_query_ehea_port(const u64 adapter_handle, const u16 port_num,
			   const u8 cb_cat, const u64 select_mask,
			   void *cb_addr);

u64 ehea_h_modify_ehea_port(const u64 adapter_handle, const u16 port_num,
			    const u8 cb_cat, const u64 select_mask,
			    void *cb_addr);

#define H_REGBCMC_PN            EHEA_BMASK_IBM(48, 63)
#define H_REGBCMC_REGTYPE       EHEA_BMASK_IBM(61, 63)
#define H_REGBCMC_MACADDR       EHEA_BMASK_IBM(16, 63)
#define H_REGBCMC_VLANID        EHEA_BMASK_IBM(52, 63)

u64 ehea_h_reg_dereg_bcmc(const u64 adapter_handle, const u16 port_num,
			  const u8 reg_type, const u64 mc_mac_addr,
			  const u16 vlan_id, const u32 hcall_id);

u64 ehea_h_reset_events(const u64 adapter_handle, const u64 neq_handle,
			const u64 event_mask);

u64 ehea_h_error_data(const u64 adapter_handle, const u64 ressource_handle,
		      void *rblock);

#endif	/* __EHEA_PHYP_H__ */
