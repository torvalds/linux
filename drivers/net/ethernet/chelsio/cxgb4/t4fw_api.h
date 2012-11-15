/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2009-2010 Chelsio Communications, Inc. All rights reserved.
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

#ifndef _T4FW_INTERFACE_H_
#define _T4FW_INTERFACE_H_

#define FW_T4VF_SGE_BASE_ADDR      0x0000
#define FW_T4VF_MPS_BASE_ADDR      0x0100
#define FW_T4VF_PL_BASE_ADDR       0x0200
#define FW_T4VF_MBDATA_BASE_ADDR   0x0240
#define FW_T4VF_CIM_BASE_ADDR      0x0300

enum fw_wr_opcodes {
	FW_FILTER_WR                   = 0x02,
	FW_ULPTX_WR                    = 0x04,
	FW_TP_WR                       = 0x05,
	FW_ETH_TX_PKT_WR               = 0x08,
	FW_FLOWC_WR                    = 0x0a,
	FW_OFLD_TX_DATA_WR             = 0x0b,
	FW_CMD_WR                      = 0x10,
	FW_ETH_TX_PKT_VM_WR            = 0x11,
	FW_RI_RES_WR                   = 0x0c,
	FW_RI_INIT_WR                  = 0x0d,
	FW_RI_RDMA_WRITE_WR            = 0x14,
	FW_RI_SEND_WR                  = 0x15,
	FW_RI_RDMA_READ_WR             = 0x16,
	FW_RI_RECV_WR                  = 0x17,
	FW_RI_BIND_MW_WR               = 0x18,
	FW_RI_FR_NSMR_WR               = 0x19,
	FW_RI_INV_LSTAG_WR             = 0x1a,
	FW_LASTC2E_WR                  = 0x40
};

struct fw_wr_hdr {
	__be32 hi;
	__be32 lo;
};

#define FW_WR_OP(x)	 ((x) << 24)
#define FW_WR_OP_GET(x)	 (((x) >> 24) & 0xff)
#define FW_WR_ATOMIC(x)	 ((x) << 23)
#define FW_WR_FLUSH(x)   ((x) << 22)
#define FW_WR_COMPL(x)   ((x) << 21)
#define FW_WR_IMMDLEN_MASK 0xff
#define FW_WR_IMMDLEN(x) ((x) << 0)

#define FW_WR_EQUIQ	(1U << 31)
#define FW_WR_EQUEQ	(1U << 30)
#define FW_WR_FLOWID(x)	((x) << 8)
#define FW_WR_LEN16(x)	((x) << 0)

#define HW_TPL_FR_MT_PR_IV_P_FC         0X32B

struct fw_ulptx_wr {
	__be32 op_to_compl;
	__be32 flowid_len16;
	u64 cookie;
};

struct fw_tp_wr {
	__be32 op_to_immdlen;
	__be32 flowid_len16;
	u64 cookie;
};

struct fw_eth_tx_pkt_wr {
	__be32 op_immdlen;
	__be32 equiq_to_len16;
	__be64 r3;
};

enum fw_flowc_mnem {
	FW_FLOWC_MNEM_PFNVFN,		/* PFN [15:8] VFN [7:0] */
	FW_FLOWC_MNEM_CH,
	FW_FLOWC_MNEM_PORT,
	FW_FLOWC_MNEM_IQID,
	FW_FLOWC_MNEM_SNDNXT,
	FW_FLOWC_MNEM_RCVNXT,
	FW_FLOWC_MNEM_SNDBUF,
	FW_FLOWC_MNEM_MSS,
};

struct fw_flowc_mnemval {
	u8 mnemonic;
	u8 r4[3];
	__be32 val;
};

struct fw_flowc_wr {
	__be32 op_to_nparams;
#define FW_FLOWC_WR_NPARAMS(x)	((x) << 0)
	__be32 flowid_len16;
	struct fw_flowc_mnemval mnemval[0];
};

struct fw_ofld_tx_data_wr {
	__be32 op_to_immdlen;
	__be32 flowid_len16;
	__be32 plen;
	__be32 tunnel_to_proxy;
#define FW_OFLD_TX_DATA_WR_TUNNEL(x)	 ((x) << 19)
#define FW_OFLD_TX_DATA_WR_SAVE(x)	 ((x) << 18)
#define FW_OFLD_TX_DATA_WR_FLUSH(x)	 ((x) << 17)
#define FW_OFLD_TX_DATA_WR_URGENT(x)	 ((x) << 16)
#define FW_OFLD_TX_DATA_WR_MORE(x)	 ((x) << 15)
#define FW_OFLD_TX_DATA_WR_SHOVE(x)	 ((x) << 14)
#define FW_OFLD_TX_DATA_WR_ULPMODE(x)	 ((x) << 10)
#define FW_OFLD_TX_DATA_WR_ULPSUBMODE(x) ((x) << 6)
};

struct fw_cmd_wr {
	__be32 op_dma;
#define FW_CMD_WR_DMA (1U << 17)
	__be32 len16_pkd;
	__be64 cookie_daddr;
};

struct fw_eth_tx_pkt_vm_wr {
	__be32 op_immdlen;
	__be32 equiq_to_len16;
	__be32 r3[2];
	u8 ethmacdst[6];
	u8 ethmacsrc[6];
	__be16 ethtype;
	__be16 vlantci;
};

#define FW_CMD_MAX_TIMEOUT 3000

/*
 * If a host driver does a HELLO and discovers that there's already a MASTER
 * selected, we may have to wait for that MASTER to finish issuing RESET,
 * configuration and INITIALIZE commands.  Also, there's a possibility that
 * our own HELLO may get lost if it happens right as the MASTER is issuign a
 * RESET command, so we need to be willing to make a few retries of our HELLO.
 */
#define FW_CMD_HELLO_TIMEOUT	(3 * FW_CMD_MAX_TIMEOUT)
#define FW_CMD_HELLO_RETRIES	3


enum fw_cmd_opcodes {
	FW_LDST_CMD                    = 0x01,
	FW_RESET_CMD                   = 0x03,
	FW_HELLO_CMD                   = 0x04,
	FW_BYE_CMD                     = 0x05,
	FW_INITIALIZE_CMD              = 0x06,
	FW_CAPS_CONFIG_CMD             = 0x07,
	FW_PARAMS_CMD                  = 0x08,
	FW_PFVF_CMD                    = 0x09,
	FW_IQ_CMD                      = 0x10,
	FW_EQ_MNGT_CMD                 = 0x11,
	FW_EQ_ETH_CMD                  = 0x12,
	FW_EQ_CTRL_CMD                 = 0x13,
	FW_EQ_OFLD_CMD                 = 0x21,
	FW_VI_CMD                      = 0x14,
	FW_VI_MAC_CMD                  = 0x15,
	FW_VI_RXMODE_CMD               = 0x16,
	FW_VI_ENABLE_CMD               = 0x17,
	FW_ACL_MAC_CMD                 = 0x18,
	FW_ACL_VLAN_CMD                = 0x19,
	FW_VI_STATS_CMD                = 0x1a,
	FW_PORT_CMD                    = 0x1b,
	FW_PORT_STATS_CMD              = 0x1c,
	FW_PORT_LB_STATS_CMD           = 0x1d,
	FW_PORT_TRACE_CMD              = 0x1e,
	FW_PORT_TRACE_MMAP_CMD         = 0x1f,
	FW_RSS_IND_TBL_CMD             = 0x20,
	FW_RSS_GLB_CONFIG_CMD          = 0x22,
	FW_RSS_VI_CONFIG_CMD           = 0x23,
	FW_LASTC2E_CMD                 = 0x40,
	FW_ERROR_CMD                   = 0x80,
	FW_DEBUG_CMD                   = 0x81,
};

enum fw_cmd_cap {
	FW_CMD_CAP_PF                  = 0x01,
	FW_CMD_CAP_DMAQ                = 0x02,
	FW_CMD_CAP_PORT                = 0x04,
	FW_CMD_CAP_PORTPROMISC         = 0x08,
	FW_CMD_CAP_PORTSTATS           = 0x10,
	FW_CMD_CAP_VF                  = 0x80,
};

/*
 * Generic command header flit0
 */
struct fw_cmd_hdr {
	__be32 hi;
	__be32 lo;
};

#define FW_CMD_OP(x)		((x) << 24)
#define FW_CMD_OP_GET(x)        (((x) >> 24) & 0xff)
#define FW_CMD_REQUEST          (1U << 23)
#define FW_CMD_REQUEST_GET(x)   (((x) >> 23) & 0x1)
#define FW_CMD_READ		(1U << 22)
#define FW_CMD_WRITE		(1U << 21)
#define FW_CMD_EXEC		(1U << 20)
#define FW_CMD_RAMASK(x)	((x) << 20)
#define FW_CMD_RETVAL(x)	((x) << 8)
#define FW_CMD_RETVAL_GET(x)	(((x) >> 8) & 0xff)
#define FW_CMD_LEN16(x)         ((x) << 0)
#define FW_LEN16(fw_struct)	FW_CMD_LEN16(sizeof(fw_struct) / 16)

enum fw_ldst_addrspc {
	FW_LDST_ADDRSPC_FIRMWARE  = 0x0001,
	FW_LDST_ADDRSPC_SGE_EGRC  = 0x0008,
	FW_LDST_ADDRSPC_SGE_INGC  = 0x0009,
	FW_LDST_ADDRSPC_SGE_FLMC  = 0x000a,
	FW_LDST_ADDRSPC_SGE_CONMC = 0x000b,
	FW_LDST_ADDRSPC_TP_PIO    = 0x0010,
	FW_LDST_ADDRSPC_TP_TM_PIO = 0x0011,
	FW_LDST_ADDRSPC_TP_MIB    = 0x0012,
	FW_LDST_ADDRSPC_MDIO      = 0x0018,
	FW_LDST_ADDRSPC_MPS       = 0x0020,
	FW_LDST_ADDRSPC_FUNC      = 0x0028,
	FW_LDST_ADDRSPC_FUNC_PCIE = 0x0029,
};

enum fw_ldst_mps_fid {
	FW_LDST_MPS_ATRB,
	FW_LDST_MPS_RPLC
};

enum fw_ldst_func_access_ctl {
	FW_LDST_FUNC_ACC_CTL_VIID,
	FW_LDST_FUNC_ACC_CTL_FID
};

enum fw_ldst_func_mod_index {
	FW_LDST_FUNC_MPS
};

struct fw_ldst_cmd {
	__be32 op_to_addrspace;
#define FW_LDST_CMD_ADDRSPACE(x) ((x) << 0)
	__be32 cycles_to_len16;
	union fw_ldst {
		struct fw_ldst_addrval {
			__be32 addr;
			__be32 val;
		} addrval;
		struct fw_ldst_idctxt {
			__be32 physid;
			__be32 msg_pkd;
			__be32 ctxt_data7;
			__be32 ctxt_data6;
			__be32 ctxt_data5;
			__be32 ctxt_data4;
			__be32 ctxt_data3;
			__be32 ctxt_data2;
			__be32 ctxt_data1;
			__be32 ctxt_data0;
		} idctxt;
		struct fw_ldst_mdio {
			__be16 paddr_mmd;
			__be16 raddr;
			__be16 vctl;
			__be16 rval;
		} mdio;
		struct fw_ldst_mps {
			__be16 fid_ctl;
			__be16 rplcpf_pkd;
			__be32 rplc127_96;
			__be32 rplc95_64;
			__be32 rplc63_32;
			__be32 rplc31_0;
			__be32 atrb;
			__be16 vlan[16];
		} mps;
		struct fw_ldst_func {
			u8 access_ctl;
			u8 mod_index;
			__be16 ctl_id;
			__be32 offset;
			__be64 data0;
			__be64 data1;
		} func;
		struct fw_ldst_pcie {
			u8 ctrl_to_fn;
			u8 bnum;
			u8 r;
			u8 ext_r;
			u8 select_naccess;
			u8 pcie_fn;
			__be16 nset_pkd;
			__be32 data[12];
		} pcie;
	} u;
};

#define FW_LDST_CMD_MSG(x)	((x) << 31)
#define FW_LDST_CMD_PADDR(x)	((x) << 8)
#define FW_LDST_CMD_MMD(x)	((x) << 0)
#define FW_LDST_CMD_FID(x)	((x) << 15)
#define FW_LDST_CMD_CTL(x)	((x) << 0)
#define FW_LDST_CMD_RPLCPF(x)	((x) << 0)
#define FW_LDST_CMD_LC		(1U << 4)
#define FW_LDST_CMD_NACCESS(x)	((x) << 0)
#define FW_LDST_CMD_FN(x)	((x) << 0)

struct fw_reset_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	__be32 val;
	__be32 halt_pkd;
};

#define FW_RESET_CMD_HALT_SHIFT    31
#define FW_RESET_CMD_HALT_MASK     0x1
#define FW_RESET_CMD_HALT(x)       ((x) << FW_RESET_CMD_HALT_SHIFT)
#define FW_RESET_CMD_HALT_GET(x)  \
	(((x) >> FW_RESET_CMD_HALT_SHIFT) & FW_RESET_CMD_HALT_MASK)

enum fw_hellow_cmd {
	fw_hello_cmd_stage_os		= 0x0
};

struct fw_hello_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	__be32 err_to_clearinit;
#define FW_HELLO_CMD_ERR	    (1U << 31)
#define FW_HELLO_CMD_INIT	    (1U << 30)
#define FW_HELLO_CMD_MASTERDIS(x)   ((x) << 29)
#define FW_HELLO_CMD_MASTERFORCE(x) ((x) << 28)
#define FW_HELLO_CMD_MBMASTER_MASK   0xfU
#define FW_HELLO_CMD_MBMASTER_SHIFT  24
#define FW_HELLO_CMD_MBMASTER(x)     ((x) << FW_HELLO_CMD_MBMASTER_SHIFT)
#define FW_HELLO_CMD_MBMASTER_GET(x) \
	(((x) >> FW_HELLO_CMD_MBMASTER_SHIFT) & FW_HELLO_CMD_MBMASTER_MASK)
#define FW_HELLO_CMD_MBASYNCNOTINT(x)	((x) << 23)
#define FW_HELLO_CMD_MBASYNCNOT(x)  ((x) << 20)
#define FW_HELLO_CMD_STAGE(x)       ((x) << 17)
#define FW_HELLO_CMD_CLEARINIT      (1U << 16)
	__be32 fwrev;
};

struct fw_bye_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	__be64 r3;
};

struct fw_initialize_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	__be64 r3;
};

enum fw_caps_config_hm {
	FW_CAPS_CONFIG_HM_PCIE		= 0x00000001,
	FW_CAPS_CONFIG_HM_PL		= 0x00000002,
	FW_CAPS_CONFIG_HM_SGE		= 0x00000004,
	FW_CAPS_CONFIG_HM_CIM		= 0x00000008,
	FW_CAPS_CONFIG_HM_ULPTX		= 0x00000010,
	FW_CAPS_CONFIG_HM_TP		= 0x00000020,
	FW_CAPS_CONFIG_HM_ULPRX		= 0x00000040,
	FW_CAPS_CONFIG_HM_PMRX		= 0x00000080,
	FW_CAPS_CONFIG_HM_PMTX		= 0x00000100,
	FW_CAPS_CONFIG_HM_MC		= 0x00000200,
	FW_CAPS_CONFIG_HM_LE		= 0x00000400,
	FW_CAPS_CONFIG_HM_MPS		= 0x00000800,
	FW_CAPS_CONFIG_HM_XGMAC		= 0x00001000,
	FW_CAPS_CONFIG_HM_CPLSWITCH	= 0x00002000,
	FW_CAPS_CONFIG_HM_T4DBG		= 0x00004000,
	FW_CAPS_CONFIG_HM_MI		= 0x00008000,
	FW_CAPS_CONFIG_HM_I2CM		= 0x00010000,
	FW_CAPS_CONFIG_HM_NCSI		= 0x00020000,
	FW_CAPS_CONFIG_HM_SMB		= 0x00040000,
	FW_CAPS_CONFIG_HM_MA		= 0x00080000,
	FW_CAPS_CONFIG_HM_EDRAM		= 0x00100000,
	FW_CAPS_CONFIG_HM_PMU		= 0x00200000,
	FW_CAPS_CONFIG_HM_UART		= 0x00400000,
	FW_CAPS_CONFIG_HM_SF		= 0x00800000,
};

enum fw_caps_config_nbm {
	FW_CAPS_CONFIG_NBM_IPMI		= 0x00000001,
	FW_CAPS_CONFIG_NBM_NCSI		= 0x00000002,
};

enum fw_caps_config_link {
	FW_CAPS_CONFIG_LINK_PPP		= 0x00000001,
	FW_CAPS_CONFIG_LINK_QFC		= 0x00000002,
	FW_CAPS_CONFIG_LINK_DCBX	= 0x00000004,
};

enum fw_caps_config_switch {
	FW_CAPS_CONFIG_SWITCH_INGRESS	= 0x00000001,
	FW_CAPS_CONFIG_SWITCH_EGRESS	= 0x00000002,
};

enum fw_caps_config_nic {
	FW_CAPS_CONFIG_NIC		= 0x00000001,
	FW_CAPS_CONFIG_NIC_VM		= 0x00000002,
};

enum fw_caps_config_ofld {
	FW_CAPS_CONFIG_OFLD		= 0x00000001,
};

enum fw_caps_config_rdma {
	FW_CAPS_CONFIG_RDMA_RDDP	= 0x00000001,
	FW_CAPS_CONFIG_RDMA_RDMAC	= 0x00000002,
};

enum fw_caps_config_iscsi {
	FW_CAPS_CONFIG_ISCSI_INITIATOR_PDU = 0x00000001,
	FW_CAPS_CONFIG_ISCSI_TARGET_PDU = 0x00000002,
	FW_CAPS_CONFIG_ISCSI_INITIATOR_CNXOFLD = 0x00000004,
	FW_CAPS_CONFIG_ISCSI_TARGET_CNXOFLD = 0x00000008,
};

enum fw_caps_config_fcoe {
	FW_CAPS_CONFIG_FCOE_INITIATOR	= 0x00000001,
	FW_CAPS_CONFIG_FCOE_TARGET	= 0x00000002,
	FW_CAPS_CONFIG_FCOE_CTRL_OFLD	= 0x00000004,
};

enum fw_memtype_cf {
	FW_MEMTYPE_CF_EDC0		= 0x0,
	FW_MEMTYPE_CF_EDC1		= 0x1,
	FW_MEMTYPE_CF_EXTMEM		= 0x2,
	FW_MEMTYPE_CF_FLASH		= 0x4,
	FW_MEMTYPE_CF_INTERNAL		= 0x5,
};

struct fw_caps_config_cmd {
	__be32 op_to_write;
	__be32 cfvalid_to_len16;
	__be32 r2;
	__be32 hwmbitmap;
	__be16 nbmcaps;
	__be16 linkcaps;
	__be16 switchcaps;
	__be16 r3;
	__be16 niccaps;
	__be16 ofldcaps;
	__be16 rdmacaps;
	__be16 r4;
	__be16 iscsicaps;
	__be16 fcoecaps;
	__be32 cfcsum;
	__be32 finiver;
	__be32 finicsum;
};

#define FW_CAPS_CONFIG_CMD_CFVALID          (1U << 27)
#define FW_CAPS_CONFIG_CMD_MEMTYPE_CF(x)    ((x) << 24)
#define FW_CAPS_CONFIG_CMD_MEMADDR64K_CF(x) ((x) << 16)

/*
 * params command mnemonics
 */
enum fw_params_mnem {
	FW_PARAMS_MNEM_DEV		= 1,	/* device params */
	FW_PARAMS_MNEM_PFVF		= 2,	/* function params */
	FW_PARAMS_MNEM_REG		= 3,	/* limited register access */
	FW_PARAMS_MNEM_DMAQ		= 4,	/* dma queue params */
	FW_PARAMS_MNEM_LAST
};

/*
 * device parameters
 */
enum fw_params_param_dev {
	FW_PARAMS_PARAM_DEV_CCLK	= 0x00, /* chip core clock in khz */
	FW_PARAMS_PARAM_DEV_PORTVEC	= 0x01, /* the port vector */
	FW_PARAMS_PARAM_DEV_NTID	= 0x02, /* reads the number of TIDs
						 * allocated by the device's
						 * Lookup Engine
						 */
	FW_PARAMS_PARAM_DEV_FLOWC_BUFFIFO_SZ = 0x03,
	FW_PARAMS_PARAM_DEV_INTVER_NIC	= 0x04,
	FW_PARAMS_PARAM_DEV_INTVER_VNIC = 0x05,
	FW_PARAMS_PARAM_DEV_INTVER_OFLD = 0x06,
	FW_PARAMS_PARAM_DEV_INTVER_RI	= 0x07,
	FW_PARAMS_PARAM_DEV_INTVER_ISCSIPDU = 0x08,
	FW_PARAMS_PARAM_DEV_INTVER_ISCSI = 0x09,
	FW_PARAMS_PARAM_DEV_INTVER_FCOE = 0x0A,
	FW_PARAMS_PARAM_DEV_FWREV = 0x0B,
	FW_PARAMS_PARAM_DEV_TPREV = 0x0C,
	FW_PARAMS_PARAM_DEV_CF = 0x0D,
};

/*
 * physical and virtual function parameters
 */
enum fw_params_param_pfvf {
	FW_PARAMS_PARAM_PFVF_RWXCAPS	= 0x00,
	FW_PARAMS_PARAM_PFVF_ROUTE_START = 0x01,
	FW_PARAMS_PARAM_PFVF_ROUTE_END = 0x02,
	FW_PARAMS_PARAM_PFVF_CLIP_START = 0x03,
	FW_PARAMS_PARAM_PFVF_CLIP_END = 0x04,
	FW_PARAMS_PARAM_PFVF_FILTER_START = 0x05,
	FW_PARAMS_PARAM_PFVF_FILTER_END = 0x06,
	FW_PARAMS_PARAM_PFVF_SERVER_START = 0x07,
	FW_PARAMS_PARAM_PFVF_SERVER_END = 0x08,
	FW_PARAMS_PARAM_PFVF_TDDP_START = 0x09,
	FW_PARAMS_PARAM_PFVF_TDDP_END = 0x0A,
	FW_PARAMS_PARAM_PFVF_ISCSI_START = 0x0B,
	FW_PARAMS_PARAM_PFVF_ISCSI_END = 0x0C,
	FW_PARAMS_PARAM_PFVF_STAG_START = 0x0D,
	FW_PARAMS_PARAM_PFVF_STAG_END = 0x0E,
	FW_PARAMS_PARAM_PFVF_RQ_START = 0x1F,
	FW_PARAMS_PARAM_PFVF_RQ_END	= 0x10,
	FW_PARAMS_PARAM_PFVF_PBL_START = 0x11,
	FW_PARAMS_PARAM_PFVF_PBL_END	= 0x12,
	FW_PARAMS_PARAM_PFVF_L2T_START = 0x13,
	FW_PARAMS_PARAM_PFVF_L2T_END = 0x14,
	FW_PARAMS_PARAM_PFVF_SQRQ_START = 0x15,
	FW_PARAMS_PARAM_PFVF_SQRQ_END	= 0x16,
	FW_PARAMS_PARAM_PFVF_CQ_START	= 0x17,
	FW_PARAMS_PARAM_PFVF_CQ_END	= 0x18,
	FW_PARAMS_PARAM_PFVF_SCHEDCLASS_ETH = 0x20,
	FW_PARAMS_PARAM_PFVF_VIID       = 0x24,
	FW_PARAMS_PARAM_PFVF_CPMASK     = 0x25,
	FW_PARAMS_PARAM_PFVF_OCQ_START  = 0x26,
	FW_PARAMS_PARAM_PFVF_OCQ_END    = 0x27,
	FW_PARAMS_PARAM_PFVF_CONM_MAP   = 0x28,
	FW_PARAMS_PARAM_PFVF_IQFLINT_START = 0x29,
	FW_PARAMS_PARAM_PFVF_IQFLINT_END = 0x2A,
	FW_PARAMS_PARAM_PFVF_EQ_START	= 0x2B,
	FW_PARAMS_PARAM_PFVF_EQ_END	= 0x2C,
	FW_PARAMS_PARAM_PFVF_ACTIVE_FILTER_START = 0x2D,
	FW_PARAMS_PARAM_PFVF_ACTIVE_FILTER_END = 0x2E
};

/*
 * dma queue parameters
 */
enum fw_params_param_dmaq {
	FW_PARAMS_PARAM_DMAQ_IQ_DCAEN_DCACPU = 0x00,
	FW_PARAMS_PARAM_DMAQ_IQ_INTCNTTHRESH = 0x01,
	FW_PARAMS_PARAM_DMAQ_EQ_CMPLIQID_MNGT = 0x10,
	FW_PARAMS_PARAM_DMAQ_EQ_CMPLIQID_CTRL = 0x11,
	FW_PARAMS_PARAM_DMAQ_EQ_SCHEDCLASS_ETH = 0x12,
};

#define FW_PARAMS_MNEM(x)      ((x) << 24)
#define FW_PARAMS_PARAM_X(x)   ((x) << 16)
#define FW_PARAMS_PARAM_Y_SHIFT  8
#define FW_PARAMS_PARAM_Y_MASK   0xffU
#define FW_PARAMS_PARAM_Y(x)     ((x) << FW_PARAMS_PARAM_Y_SHIFT)
#define FW_PARAMS_PARAM_Y_GET(x) (((x) >> FW_PARAMS_PARAM_Y_SHIFT) &\
		FW_PARAMS_PARAM_Y_MASK)
#define FW_PARAMS_PARAM_Z_SHIFT  0
#define FW_PARAMS_PARAM_Z_MASK   0xffu
#define FW_PARAMS_PARAM_Z(x)     ((x) << FW_PARAMS_PARAM_Z_SHIFT)
#define FW_PARAMS_PARAM_Z_GET(x) (((x) >> FW_PARAMS_PARAM_Z_SHIFT) &\
		FW_PARAMS_PARAM_Z_MASK)
#define FW_PARAMS_PARAM_XYZ(x) ((x) << 0)
#define FW_PARAMS_PARAM_YZ(x)  ((x) << 0)

struct fw_params_cmd {
	__be32 op_to_vfn;
	__be32 retval_len16;
	struct fw_params_param {
		__be32 mnem;
		__be32 val;
	} param[7];
};

#define FW_PARAMS_CMD_PFN(x) ((x) << 8)
#define FW_PARAMS_CMD_VFN(x) ((x) << 0)

struct fw_pfvf_cmd {
	__be32 op_to_vfn;
	__be32 retval_len16;
	__be32 niqflint_niq;
	__be32 type_to_neq;
	__be32 tc_to_nexactf;
	__be32 r_caps_to_nethctrl;
	__be16 nricq;
	__be16 nriqp;
	__be32 r4;
};

#define FW_PFVF_CMD_PFN(x) ((x) << 8)
#define FW_PFVF_CMD_VFN(x) ((x) << 0)

#define FW_PFVF_CMD_NIQFLINT(x) ((x) << 20)
#define FW_PFVF_CMD_NIQFLINT_GET(x) (((x) >> 20) & 0xfff)

#define FW_PFVF_CMD_NIQ(x) ((x) << 0)
#define FW_PFVF_CMD_NIQ_GET(x) (((x) >> 0) & 0xfffff)

#define FW_PFVF_CMD_TYPE (1 << 31)
#define FW_PFVF_CMD_TYPE_GET(x) (((x) >> 31) & 0x1)

#define FW_PFVF_CMD_CMASK(x) ((x) << 24)
#define FW_PFVF_CMD_CMASK_MASK 0xf
#define FW_PFVF_CMD_CMASK_GET(x) (((x) >> 24) & FW_PFVF_CMD_CMASK_MASK)

#define FW_PFVF_CMD_PMASK(x) ((x) << 20)
#define FW_PFVF_CMD_PMASK_MASK 0xf
#define FW_PFVF_CMD_PMASK_GET(x) (((x) >> 20) & FW_PFVF_CMD_PMASK_MASK)

#define FW_PFVF_CMD_NEQ(x) ((x) << 0)
#define FW_PFVF_CMD_NEQ_GET(x) (((x) >> 0) & 0xfffff)

#define FW_PFVF_CMD_TC(x) ((x) << 24)
#define FW_PFVF_CMD_TC_GET(x) (((x) >> 24) & 0xff)

#define FW_PFVF_CMD_NVI(x) ((x) << 16)
#define FW_PFVF_CMD_NVI_GET(x) (((x) >> 16) & 0xff)

#define FW_PFVF_CMD_NEXACTF(x) ((x) << 0)
#define FW_PFVF_CMD_NEXACTF_GET(x) (((x) >> 0) & 0xffff)

#define FW_PFVF_CMD_R_CAPS(x) ((x) << 24)
#define FW_PFVF_CMD_R_CAPS_GET(x) (((x) >> 24) & 0xff)

#define FW_PFVF_CMD_WX_CAPS(x) ((x) << 16)
#define FW_PFVF_CMD_WX_CAPS_GET(x) (((x) >> 16) & 0xff)

#define FW_PFVF_CMD_NETHCTRL(x) ((x) << 0)
#define FW_PFVF_CMD_NETHCTRL_GET(x) (((x) >> 0) & 0xffff)

enum fw_iq_type {
	FW_IQ_TYPE_FL_INT_CAP,
	FW_IQ_TYPE_NO_FL_INT_CAP
};

struct fw_iq_cmd {
	__be32 op_to_vfn;
	__be32 alloc_to_len16;
	__be16 physiqid;
	__be16 iqid;
	__be16 fl0id;
	__be16 fl1id;
	__be32 type_to_iqandstindex;
	__be16 iqdroprss_to_iqesize;
	__be16 iqsize;
	__be64 iqaddr;
	__be32 iqns_to_fl0congen;
	__be16 fl0dcaen_to_fl0cidxfthresh;
	__be16 fl0size;
	__be64 fl0addr;
	__be32 fl1cngchmap_to_fl1congen;
	__be16 fl1dcaen_to_fl1cidxfthresh;
	__be16 fl1size;
	__be64 fl1addr;
};

#define FW_IQ_CMD_PFN(x) ((x) << 8)
#define FW_IQ_CMD_VFN(x) ((x) << 0)

#define FW_IQ_CMD_ALLOC (1U << 31)
#define FW_IQ_CMD_FREE (1U << 30)
#define FW_IQ_CMD_MODIFY (1U << 29)
#define FW_IQ_CMD_IQSTART(x) ((x) << 28)
#define FW_IQ_CMD_IQSTOP(x) ((x) << 27)

#define FW_IQ_CMD_TYPE(x) ((x) << 29)
#define FW_IQ_CMD_IQASYNCH(x) ((x) << 28)
#define FW_IQ_CMD_VIID(x) ((x) << 16)
#define FW_IQ_CMD_IQANDST(x) ((x) << 15)
#define FW_IQ_CMD_IQANUS(x) ((x) << 14)
#define FW_IQ_CMD_IQANUD(x) ((x) << 12)
#define FW_IQ_CMD_IQANDSTINDEX(x) ((x) << 0)

#define FW_IQ_CMD_IQDROPRSS (1U << 15)
#define FW_IQ_CMD_IQGTSMODE (1U << 14)
#define FW_IQ_CMD_IQPCIECH(x) ((x) << 12)
#define FW_IQ_CMD_IQDCAEN(x) ((x) << 11)
#define FW_IQ_CMD_IQDCACPU(x) ((x) << 6)
#define FW_IQ_CMD_IQINTCNTTHRESH(x) ((x) << 4)
#define FW_IQ_CMD_IQO (1U << 3)
#define FW_IQ_CMD_IQCPRIO(x) ((x) << 2)
#define FW_IQ_CMD_IQESIZE(x) ((x) << 0)

#define FW_IQ_CMD_IQNS(x) ((x) << 31)
#define FW_IQ_CMD_IQRO(x) ((x) << 30)
#define FW_IQ_CMD_IQFLINTIQHSEN(x) ((x) << 28)
#define FW_IQ_CMD_IQFLINTCONGEN(x) ((x) << 27)
#define FW_IQ_CMD_IQFLINTISCSIC(x) ((x) << 26)
#define FW_IQ_CMD_FL0CNGCHMAP(x) ((x) << 20)
#define FW_IQ_CMD_FL0CACHELOCK(x) ((x) << 15)
#define FW_IQ_CMD_FL0DBP(x) ((x) << 14)
#define FW_IQ_CMD_FL0DATANS(x) ((x) << 13)
#define FW_IQ_CMD_FL0DATARO(x) ((x) << 12)
#define FW_IQ_CMD_FL0CONGCIF(x) ((x) << 11)
#define FW_IQ_CMD_FL0ONCHIP(x) ((x) << 10)
#define FW_IQ_CMD_FL0STATUSPGNS(x) ((x) << 9)
#define FW_IQ_CMD_FL0STATUSPGRO(x) ((x) << 8)
#define FW_IQ_CMD_FL0FETCHNS(x) ((x) << 7)
#define FW_IQ_CMD_FL0FETCHRO(x) ((x) << 6)
#define FW_IQ_CMD_FL0HOSTFCMODE(x) ((x) << 4)
#define FW_IQ_CMD_FL0CPRIO(x) ((x) << 3)
#define FW_IQ_CMD_FL0PADEN(x) ((x) << 2)
#define FW_IQ_CMD_FL0PACKEN(x) ((x) << 1)
#define FW_IQ_CMD_FL0CONGEN (1U << 0)

#define FW_IQ_CMD_FL0DCAEN(x) ((x) << 15)
#define FW_IQ_CMD_FL0DCACPU(x) ((x) << 10)
#define FW_IQ_CMD_FL0FBMIN(x) ((x) << 7)
#define FW_IQ_CMD_FL0FBMAX(x) ((x) << 4)
#define FW_IQ_CMD_FL0CIDXFTHRESHO (1U << 3)
#define FW_IQ_CMD_FL0CIDXFTHRESH(x) ((x) << 0)

#define FW_IQ_CMD_FL1CNGCHMAP(x) ((x) << 20)
#define FW_IQ_CMD_FL1CACHELOCK(x) ((x) << 15)
#define FW_IQ_CMD_FL1DBP(x) ((x) << 14)
#define FW_IQ_CMD_FL1DATANS(x) ((x) << 13)
#define FW_IQ_CMD_FL1DATARO(x) ((x) << 12)
#define FW_IQ_CMD_FL1CONGCIF(x) ((x) << 11)
#define FW_IQ_CMD_FL1ONCHIP(x) ((x) << 10)
#define FW_IQ_CMD_FL1STATUSPGNS(x) ((x) << 9)
#define FW_IQ_CMD_FL1STATUSPGRO(x) ((x) << 8)
#define FW_IQ_CMD_FL1FETCHNS(x) ((x) << 7)
#define FW_IQ_CMD_FL1FETCHRO(x) ((x) << 6)
#define FW_IQ_CMD_FL1HOSTFCMODE(x) ((x) << 4)
#define FW_IQ_CMD_FL1CPRIO(x) ((x) << 3)
#define FW_IQ_CMD_FL1PADEN (1U << 2)
#define FW_IQ_CMD_FL1PACKEN (1U << 1)
#define FW_IQ_CMD_FL1CONGEN (1U << 0)

#define FW_IQ_CMD_FL1DCAEN(x) ((x) << 15)
#define FW_IQ_CMD_FL1DCACPU(x) ((x) << 10)
#define FW_IQ_CMD_FL1FBMIN(x) ((x) << 7)
#define FW_IQ_CMD_FL1FBMAX(x) ((x) << 4)
#define FW_IQ_CMD_FL1CIDXFTHRESHO (1U << 3)
#define FW_IQ_CMD_FL1CIDXFTHRESH(x) ((x) << 0)

struct fw_eq_eth_cmd {
	__be32 op_to_vfn;
	__be32 alloc_to_len16;
	__be32 eqid_pkd;
	__be32 physeqid_pkd;
	__be32 fetchszm_to_iqid;
	__be32 dcaen_to_eqsize;
	__be64 eqaddr;
	__be32 viid_pkd;
	__be32 r8_lo;
	__be64 r9;
};

#define FW_EQ_ETH_CMD_PFN(x) ((x) << 8)
#define FW_EQ_ETH_CMD_VFN(x) ((x) << 0)
#define FW_EQ_ETH_CMD_ALLOC (1U << 31)
#define FW_EQ_ETH_CMD_FREE (1U << 30)
#define FW_EQ_ETH_CMD_MODIFY (1U << 29)
#define FW_EQ_ETH_CMD_EQSTART (1U << 28)
#define FW_EQ_ETH_CMD_EQSTOP (1U << 27)

#define FW_EQ_ETH_CMD_EQID(x) ((x) << 0)
#define FW_EQ_ETH_CMD_EQID_GET(x) (((x) >> 0) & 0xfffff)
#define FW_EQ_ETH_CMD_PHYSEQID(x) ((x) << 0)
#define FW_EQ_ETH_CMD_PHYSEQID_GET(x) (((x) >> 0) & 0xfffff)

#define FW_EQ_ETH_CMD_FETCHSZM(x) ((x) << 26)
#define FW_EQ_ETH_CMD_STATUSPGNS(x) ((x) << 25)
#define FW_EQ_ETH_CMD_STATUSPGRO(x) ((x) << 24)
#define FW_EQ_ETH_CMD_FETCHNS(x) ((x) << 23)
#define FW_EQ_ETH_CMD_FETCHRO(x) ((x) << 22)
#define FW_EQ_ETH_CMD_HOSTFCMODE(x) ((x) << 20)
#define FW_EQ_ETH_CMD_CPRIO(x) ((x) << 19)
#define FW_EQ_ETH_CMD_ONCHIP(x) ((x) << 18)
#define FW_EQ_ETH_CMD_PCIECHN(x) ((x) << 16)
#define FW_EQ_ETH_CMD_IQID(x) ((x) << 0)

#define FW_EQ_ETH_CMD_DCAEN(x) ((x) << 31)
#define FW_EQ_ETH_CMD_DCACPU(x) ((x) << 26)
#define FW_EQ_ETH_CMD_FBMIN(x) ((x) << 23)
#define FW_EQ_ETH_CMD_FBMAX(x) ((x) << 20)
#define FW_EQ_ETH_CMD_CIDXFTHRESHO(x) ((x) << 19)
#define FW_EQ_ETH_CMD_CIDXFTHRESH(x) ((x) << 16)
#define FW_EQ_ETH_CMD_EQSIZE(x) ((x) << 0)

#define FW_EQ_ETH_CMD_VIID(x) ((x) << 16)

struct fw_eq_ctrl_cmd {
	__be32 op_to_vfn;
	__be32 alloc_to_len16;
	__be32 cmpliqid_eqid;
	__be32 physeqid_pkd;
	__be32 fetchszm_to_iqid;
	__be32 dcaen_to_eqsize;
	__be64 eqaddr;
};

#define FW_EQ_CTRL_CMD_PFN(x) ((x) << 8)
#define FW_EQ_CTRL_CMD_VFN(x) ((x) << 0)

#define FW_EQ_CTRL_CMD_ALLOC (1U << 31)
#define FW_EQ_CTRL_CMD_FREE (1U << 30)
#define FW_EQ_CTRL_CMD_MODIFY (1U << 29)
#define FW_EQ_CTRL_CMD_EQSTART (1U << 28)
#define FW_EQ_CTRL_CMD_EQSTOP (1U << 27)

#define FW_EQ_CTRL_CMD_CMPLIQID(x) ((x) << 20)
#define FW_EQ_CTRL_CMD_EQID(x) ((x) << 0)
#define FW_EQ_CTRL_CMD_EQID_GET(x) (((x) >> 0) & 0xfffff)
#define FW_EQ_CTRL_CMD_PHYSEQID_GET(x) (((x) >> 0) & 0xfffff)

#define FW_EQ_CTRL_CMD_FETCHSZM (1U << 26)
#define FW_EQ_CTRL_CMD_STATUSPGNS (1U << 25)
#define FW_EQ_CTRL_CMD_STATUSPGRO (1U << 24)
#define FW_EQ_CTRL_CMD_FETCHNS (1U << 23)
#define FW_EQ_CTRL_CMD_FETCHRO (1U << 22)
#define FW_EQ_CTRL_CMD_HOSTFCMODE(x) ((x) << 20)
#define FW_EQ_CTRL_CMD_CPRIO(x) ((x) << 19)
#define FW_EQ_CTRL_CMD_ONCHIP(x) ((x) << 18)
#define FW_EQ_CTRL_CMD_PCIECHN(x) ((x) << 16)
#define FW_EQ_CTRL_CMD_IQID(x) ((x) << 0)

#define FW_EQ_CTRL_CMD_DCAEN(x) ((x) << 31)
#define FW_EQ_CTRL_CMD_DCACPU(x) ((x) << 26)
#define FW_EQ_CTRL_CMD_FBMIN(x) ((x) << 23)
#define FW_EQ_CTRL_CMD_FBMAX(x) ((x) << 20)
#define FW_EQ_CTRL_CMD_CIDXFTHRESHO(x) ((x) << 19)
#define FW_EQ_CTRL_CMD_CIDXFTHRESH(x) ((x) << 16)
#define FW_EQ_CTRL_CMD_EQSIZE(x) ((x) << 0)

struct fw_eq_ofld_cmd {
	__be32 op_to_vfn;
	__be32 alloc_to_len16;
	__be32 eqid_pkd;
	__be32 physeqid_pkd;
	__be32 fetchszm_to_iqid;
	__be32 dcaen_to_eqsize;
	__be64 eqaddr;
};

#define FW_EQ_OFLD_CMD_PFN(x) ((x) << 8)
#define FW_EQ_OFLD_CMD_VFN(x) ((x) << 0)

#define FW_EQ_OFLD_CMD_ALLOC (1U << 31)
#define FW_EQ_OFLD_CMD_FREE (1U << 30)
#define FW_EQ_OFLD_CMD_MODIFY (1U << 29)
#define FW_EQ_OFLD_CMD_EQSTART (1U << 28)
#define FW_EQ_OFLD_CMD_EQSTOP (1U << 27)

#define FW_EQ_OFLD_CMD_EQID(x) ((x) << 0)
#define FW_EQ_OFLD_CMD_EQID_GET(x) (((x) >> 0) & 0xfffff)
#define FW_EQ_OFLD_CMD_PHYSEQID_GET(x) (((x) >> 0) & 0xfffff)

#define FW_EQ_OFLD_CMD_FETCHSZM(x) ((x) << 26)
#define FW_EQ_OFLD_CMD_STATUSPGNS(x) ((x) << 25)
#define FW_EQ_OFLD_CMD_STATUSPGRO(x) ((x) << 24)
#define FW_EQ_OFLD_CMD_FETCHNS(x) ((x) << 23)
#define FW_EQ_OFLD_CMD_FETCHRO(x) ((x) << 22)
#define FW_EQ_OFLD_CMD_HOSTFCMODE(x) ((x) << 20)
#define FW_EQ_OFLD_CMD_CPRIO(x) ((x) << 19)
#define FW_EQ_OFLD_CMD_ONCHIP(x) ((x) << 18)
#define FW_EQ_OFLD_CMD_PCIECHN(x) ((x) << 16)
#define FW_EQ_OFLD_CMD_IQID(x) ((x) << 0)

#define FW_EQ_OFLD_CMD_DCAEN(x) ((x) << 31)
#define FW_EQ_OFLD_CMD_DCACPU(x) ((x) << 26)
#define FW_EQ_OFLD_CMD_FBMIN(x) ((x) << 23)
#define FW_EQ_OFLD_CMD_FBMAX(x) ((x) << 20)
#define FW_EQ_OFLD_CMD_CIDXFTHRESHO(x) ((x) << 19)
#define FW_EQ_OFLD_CMD_CIDXFTHRESH(x) ((x) << 16)
#define FW_EQ_OFLD_CMD_EQSIZE(x) ((x) << 0)

/*
 * Macros for VIID parsing:
 * VIID - [10:8] PFN, [7] VI Valid, [6:0] VI number
 */
#define FW_VIID_PFN_GET(x) (((x) >> 8) & 0x7)
#define FW_VIID_VIVLD_GET(x) (((x) >> 7) & 0x1)
#define FW_VIID_VIN_GET(x) (((x) >> 0) & 0x7F)

struct fw_vi_cmd {
	__be32 op_to_vfn;
	__be32 alloc_to_len16;
	__be16 type_viid;
	u8 mac[6];
	u8 portid_pkd;
	u8 nmac;
	u8 nmac0[6];
	__be16 rsssize_pkd;
	u8 nmac1[6];
	__be16 idsiiq_pkd;
	u8 nmac2[6];
	__be16 idseiq_pkd;
	u8 nmac3[6];
	__be64 r9;
	__be64 r10;
};

#define FW_VI_CMD_PFN(x) ((x) << 8)
#define FW_VI_CMD_VFN(x) ((x) << 0)
#define FW_VI_CMD_ALLOC (1U << 31)
#define FW_VI_CMD_FREE (1U << 30)
#define FW_VI_CMD_VIID(x) ((x) << 0)
#define FW_VI_CMD_VIID_GET(x) ((x) & 0xfff)
#define FW_VI_CMD_PORTID(x) ((x) << 4)
#define FW_VI_CMD_PORTID_GET(x) (((x) >> 4) & 0xf)
#define FW_VI_CMD_RSSSIZE_GET(x) (((x) >> 0) & 0x7ff)

/* Special VI_MAC command index ids */
#define FW_VI_MAC_ADD_MAC		0x3FF
#define FW_VI_MAC_ADD_PERSIST_MAC	0x3FE
#define FW_VI_MAC_MAC_BASED_FREE	0x3FD
#define FW_CLS_TCAM_NUM_ENTRIES		336

enum fw_vi_mac_smac {
	FW_VI_MAC_MPS_TCAM_ENTRY,
	FW_VI_MAC_MPS_TCAM_ONLY,
	FW_VI_MAC_SMT_ONLY,
	FW_VI_MAC_SMT_AND_MPSTCAM
};

enum fw_vi_mac_result {
	FW_VI_MAC_R_SUCCESS,
	FW_VI_MAC_R_F_NONEXISTENT_NOMEM,
	FW_VI_MAC_R_SMAC_FAIL,
	FW_VI_MAC_R_F_ACL_CHECK
};

struct fw_vi_mac_cmd {
	__be32 op_to_viid;
	__be32 freemacs_to_len16;
	union fw_vi_mac {
		struct fw_vi_mac_exact {
			__be16 valid_to_idx;
			u8 macaddr[6];
		} exact[7];
		struct fw_vi_mac_hash {
			__be64 hashvec;
		} hash;
	} u;
};

#define FW_VI_MAC_CMD_VIID(x) ((x) << 0)
#define FW_VI_MAC_CMD_FREEMACS(x) ((x) << 31)
#define FW_VI_MAC_CMD_HASHVECEN (1U << 23)
#define FW_VI_MAC_CMD_HASHUNIEN(x) ((x) << 22)
#define FW_VI_MAC_CMD_VALID (1U << 15)
#define FW_VI_MAC_CMD_PRIO(x) ((x) << 12)
#define FW_VI_MAC_CMD_SMAC_RESULT(x) ((x) << 10)
#define FW_VI_MAC_CMD_SMAC_RESULT_GET(x) (((x) >> 10) & 0x3)
#define FW_VI_MAC_CMD_IDX(x) ((x) << 0)
#define FW_VI_MAC_CMD_IDX_GET(x) (((x) >> 0) & 0x3ff)

#define FW_RXMODE_MTU_NO_CHG	65535

struct fw_vi_rxmode_cmd {
	__be32 op_to_viid;
	__be32 retval_len16;
	__be32 mtu_to_vlanexen;
	__be32 r4_lo;
};

#define FW_VI_RXMODE_CMD_VIID(x) ((x) << 0)
#define FW_VI_RXMODE_CMD_MTU_MASK 0xffff
#define FW_VI_RXMODE_CMD_MTU(x) ((x) << 16)
#define FW_VI_RXMODE_CMD_PROMISCEN_MASK 0x3
#define FW_VI_RXMODE_CMD_PROMISCEN(x) ((x) << 14)
#define FW_VI_RXMODE_CMD_ALLMULTIEN_MASK 0x3
#define FW_VI_RXMODE_CMD_ALLMULTIEN(x) ((x) << 12)
#define FW_VI_RXMODE_CMD_BROADCASTEN_MASK 0x3
#define FW_VI_RXMODE_CMD_BROADCASTEN(x) ((x) << 10)
#define FW_VI_RXMODE_CMD_VLANEXEN_MASK 0x3
#define FW_VI_RXMODE_CMD_VLANEXEN(x) ((x) << 8)

struct fw_vi_enable_cmd {
	__be32 op_to_viid;
	__be32 ien_to_len16;
	__be16 blinkdur;
	__be16 r3;
	__be32 r4;
};

#define FW_VI_ENABLE_CMD_VIID(x) ((x) << 0)
#define FW_VI_ENABLE_CMD_IEN(x) ((x) << 31)
#define FW_VI_ENABLE_CMD_EEN(x) ((x) << 30)
#define FW_VI_ENABLE_CMD_LED (1U << 29)

/* VI VF stats offset definitions */
#define VI_VF_NUM_STATS	16
enum fw_vi_stats_vf_index {
	FW_VI_VF_STAT_TX_BCAST_BYTES_IX,
	FW_VI_VF_STAT_TX_BCAST_FRAMES_IX,
	FW_VI_VF_STAT_TX_MCAST_BYTES_IX,
	FW_VI_VF_STAT_TX_MCAST_FRAMES_IX,
	FW_VI_VF_STAT_TX_UCAST_BYTES_IX,
	FW_VI_VF_STAT_TX_UCAST_FRAMES_IX,
	FW_VI_VF_STAT_TX_DROP_FRAMES_IX,
	FW_VI_VF_STAT_TX_OFLD_BYTES_IX,
	FW_VI_VF_STAT_TX_OFLD_FRAMES_IX,
	FW_VI_VF_STAT_RX_BCAST_BYTES_IX,
	FW_VI_VF_STAT_RX_BCAST_FRAMES_IX,
	FW_VI_VF_STAT_RX_MCAST_BYTES_IX,
	FW_VI_VF_STAT_RX_MCAST_FRAMES_IX,
	FW_VI_VF_STAT_RX_UCAST_BYTES_IX,
	FW_VI_VF_STAT_RX_UCAST_FRAMES_IX,
	FW_VI_VF_STAT_RX_ERR_FRAMES_IX
};

/* VI PF stats offset definitions */
#define VI_PF_NUM_STATS	17
enum fw_vi_stats_pf_index {
	FW_VI_PF_STAT_TX_BCAST_BYTES_IX,
	FW_VI_PF_STAT_TX_BCAST_FRAMES_IX,
	FW_VI_PF_STAT_TX_MCAST_BYTES_IX,
	FW_VI_PF_STAT_TX_MCAST_FRAMES_IX,
	FW_VI_PF_STAT_TX_UCAST_BYTES_IX,
	FW_VI_PF_STAT_TX_UCAST_FRAMES_IX,
	FW_VI_PF_STAT_TX_OFLD_BYTES_IX,
	FW_VI_PF_STAT_TX_OFLD_FRAMES_IX,
	FW_VI_PF_STAT_RX_BYTES_IX,
	FW_VI_PF_STAT_RX_FRAMES_IX,
	FW_VI_PF_STAT_RX_BCAST_BYTES_IX,
	FW_VI_PF_STAT_RX_BCAST_FRAMES_IX,
	FW_VI_PF_STAT_RX_MCAST_BYTES_IX,
	FW_VI_PF_STAT_RX_MCAST_FRAMES_IX,
	FW_VI_PF_STAT_RX_UCAST_BYTES_IX,
	FW_VI_PF_STAT_RX_UCAST_FRAMES_IX,
	FW_VI_PF_STAT_RX_ERR_FRAMES_IX
};

struct fw_vi_stats_cmd {
	__be32 op_to_viid;
	__be32 retval_len16;
	union fw_vi_stats {
		struct fw_vi_stats_ctl {
			__be16 nstats_ix;
			__be16 r6;
			__be32 r7;
			__be64 stat0;
			__be64 stat1;
			__be64 stat2;
			__be64 stat3;
			__be64 stat4;
			__be64 stat5;
		} ctl;
		struct fw_vi_stats_pf {
			__be64 tx_bcast_bytes;
			__be64 tx_bcast_frames;
			__be64 tx_mcast_bytes;
			__be64 tx_mcast_frames;
			__be64 tx_ucast_bytes;
			__be64 tx_ucast_frames;
			__be64 tx_offload_bytes;
			__be64 tx_offload_frames;
			__be64 rx_pf_bytes;
			__be64 rx_pf_frames;
			__be64 rx_bcast_bytes;
			__be64 rx_bcast_frames;
			__be64 rx_mcast_bytes;
			__be64 rx_mcast_frames;
			__be64 rx_ucast_bytes;
			__be64 rx_ucast_frames;
			__be64 rx_err_frames;
		} pf;
		struct fw_vi_stats_vf {
			__be64 tx_bcast_bytes;
			__be64 tx_bcast_frames;
			__be64 tx_mcast_bytes;
			__be64 tx_mcast_frames;
			__be64 tx_ucast_bytes;
			__be64 tx_ucast_frames;
			__be64 tx_drop_frames;
			__be64 tx_offload_bytes;
			__be64 tx_offload_frames;
			__be64 rx_bcast_bytes;
			__be64 rx_bcast_frames;
			__be64 rx_mcast_bytes;
			__be64 rx_mcast_frames;
			__be64 rx_ucast_bytes;
			__be64 rx_ucast_frames;
			__be64 rx_err_frames;
		} vf;
	} u;
};

#define FW_VI_STATS_CMD_VIID(x) ((x) << 0)
#define FW_VI_STATS_CMD_NSTATS(x) ((x) << 12)
#define FW_VI_STATS_CMD_IX(x) ((x) << 0)

struct fw_acl_mac_cmd {
	__be32 op_to_vfn;
	__be32 en_to_len16;
	u8 nmac;
	u8 r3[7];
	__be16 r4;
	u8 macaddr0[6];
	__be16 r5;
	u8 macaddr1[6];
	__be16 r6;
	u8 macaddr2[6];
	__be16 r7;
	u8 macaddr3[6];
};

#define FW_ACL_MAC_CMD_PFN(x) ((x) << 8)
#define FW_ACL_MAC_CMD_VFN(x) ((x) << 0)
#define FW_ACL_MAC_CMD_EN(x) ((x) << 31)

struct fw_acl_vlan_cmd {
	__be32 op_to_vfn;
	__be32 en_to_len16;
	u8 nvlan;
	u8 dropnovlan_fm;
	u8 r3_lo[6];
	__be16 vlanid[16];
};

#define FW_ACL_VLAN_CMD_PFN(x) ((x) << 8)
#define FW_ACL_VLAN_CMD_VFN(x) ((x) << 0)
#define FW_ACL_VLAN_CMD_EN(x) ((x) << 31)
#define FW_ACL_VLAN_CMD_DROPNOVLAN(x) ((x) << 7)
#define FW_ACL_VLAN_CMD_FM(x) ((x) << 6)

enum fw_port_cap {
	FW_PORT_CAP_SPEED_100M		= 0x0001,
	FW_PORT_CAP_SPEED_1G		= 0x0002,
	FW_PORT_CAP_SPEED_2_5G		= 0x0004,
	FW_PORT_CAP_SPEED_10G		= 0x0008,
	FW_PORT_CAP_SPEED_40G		= 0x0010,
	FW_PORT_CAP_SPEED_100G		= 0x0020,
	FW_PORT_CAP_FC_RX		= 0x0040,
	FW_PORT_CAP_FC_TX		= 0x0080,
	FW_PORT_CAP_ANEG		= 0x0100,
	FW_PORT_CAP_MDI_0		= 0x0200,
	FW_PORT_CAP_MDI_1		= 0x0400,
	FW_PORT_CAP_BEAN		= 0x0800,
	FW_PORT_CAP_PMA_LPBK		= 0x1000,
	FW_PORT_CAP_PCS_LPBK		= 0x2000,
	FW_PORT_CAP_PHYXS_LPBK		= 0x4000,
	FW_PORT_CAP_FAR_END_LPBK	= 0x8000,
};

enum fw_port_mdi {
	FW_PORT_MDI_UNCHANGED,
	FW_PORT_MDI_AUTO,
	FW_PORT_MDI_F_STRAIGHT,
	FW_PORT_MDI_F_CROSSOVER
};

#define FW_PORT_MDI(x) ((x) << 9)

enum fw_port_action {
	FW_PORT_ACTION_L1_CFG		= 0x0001,
	FW_PORT_ACTION_L2_CFG		= 0x0002,
	FW_PORT_ACTION_GET_PORT_INFO	= 0x0003,
	FW_PORT_ACTION_L2_PPP_CFG	= 0x0004,
	FW_PORT_ACTION_L2_DCB_CFG	= 0x0005,
	FW_PORT_ACTION_LOW_PWR_TO_NORMAL = 0x0010,
	FW_PORT_ACTION_L1_LOW_PWR_EN	= 0x0011,
	FW_PORT_ACTION_L2_WOL_MODE_EN	= 0x0012,
	FW_PORT_ACTION_LPBK_TO_NORMAL	= 0x0020,
	FW_PORT_ACTION_L1_LPBK		= 0x0021,
	FW_PORT_ACTION_L1_PMA_LPBK	= 0x0022,
	FW_PORT_ACTION_L1_PCS_LPBK	= 0x0023,
	FW_PORT_ACTION_L1_PHYXS_CSIDE_LPBK = 0x0024,
	FW_PORT_ACTION_L1_PHYXS_ESIDE_LPBK = 0x0025,
	FW_PORT_ACTION_PHY_RESET	= 0x0040,
	FW_PORT_ACTION_PMA_RESET	= 0x0041,
	FW_PORT_ACTION_PCS_RESET	= 0x0042,
	FW_PORT_ACTION_PHYXS_RESET	= 0x0043,
	FW_PORT_ACTION_DTEXS_REEST	= 0x0044,
	FW_PORT_ACTION_AN_RESET		= 0x0045
};

enum fw_port_l2cfg_ctlbf {
	FW_PORT_L2_CTLBF_OVLAN0	= 0x01,
	FW_PORT_L2_CTLBF_OVLAN1	= 0x02,
	FW_PORT_L2_CTLBF_OVLAN2	= 0x04,
	FW_PORT_L2_CTLBF_OVLAN3	= 0x08,
	FW_PORT_L2_CTLBF_IVLAN	= 0x10,
	FW_PORT_L2_CTLBF_TXIPG	= 0x20
};

enum fw_port_dcb_cfg {
	FW_PORT_DCB_CFG_PG	= 0x01,
	FW_PORT_DCB_CFG_PFC	= 0x02,
	FW_PORT_DCB_CFG_APPL	= 0x04
};

enum fw_port_dcb_cfg_rc {
	FW_PORT_DCB_CFG_SUCCESS	= 0x0,
	FW_PORT_DCB_CFG_ERROR	= 0x1
};

enum fw_port_dcb_type {
	FW_PORT_DCB_TYPE_PGID		= 0x00,
	FW_PORT_DCB_TYPE_PGRATE		= 0x01,
	FW_PORT_DCB_TYPE_PRIORATE	= 0x02,
	FW_PORT_DCB_TYPE_PFC		= 0x03,
	FW_PORT_DCB_TYPE_APP_ID		= 0x04,
};

struct fw_port_cmd {
	__be32 op_to_portid;
	__be32 action_to_len16;
	union fw_port {
		struct fw_port_l1cfg {
			__be32 rcap;
			__be32 r;
		} l1cfg;
		struct fw_port_l2cfg {
			__be16 ctlbf_to_ivlan0;
			__be16 ivlantype;
			__be32 txipg_pkd;
			__be16 ovlan0mask;
			__be16 ovlan0type;
			__be16 ovlan1mask;
			__be16 ovlan1type;
			__be16 ovlan2mask;
			__be16 ovlan2type;
			__be16 ovlan3mask;
			__be16 ovlan3type;
		} l2cfg;
		struct fw_port_info {
			__be32 lstatus_to_modtype;
			__be16 pcap;
			__be16 acap;
			__be16 mtu;
			__u8   cbllen;
			__u8   r9;
			__be32 r10;
			__be64 r11;
		} info;
		struct fw_port_ppp {
			__be32 pppen_to_ncsich;
			__be32 r11;
		} ppp;
		struct fw_port_dcb {
			__be16 cfg;
			u8 up_map;
			u8 sf_cfgrc;
			__be16 prot_ix;
			u8 pe7_to_pe0;
			u8 numTCPFCs;
			__be32 pgid0_to_pgid7;
			__be32 numTCs_oui;
			u8 pgpc[8];
		} dcb;
	} u;
};

#define FW_PORT_CMD_READ (1U << 22)

#define FW_PORT_CMD_PORTID(x) ((x) << 0)
#define FW_PORT_CMD_PORTID_GET(x) (((x) >> 0) & 0xf)

#define FW_PORT_CMD_ACTION(x) ((x) << 16)
#define FW_PORT_CMD_ACTION_GET(x) (((x) >> 16) & 0xffff)

#define FW_PORT_CMD_CTLBF(x) ((x) << 10)
#define FW_PORT_CMD_OVLAN3(x) ((x) << 7)
#define FW_PORT_CMD_OVLAN2(x) ((x) << 6)
#define FW_PORT_CMD_OVLAN1(x) ((x) << 5)
#define FW_PORT_CMD_OVLAN0(x) ((x) << 4)
#define FW_PORT_CMD_IVLAN0(x) ((x) << 3)

#define FW_PORT_CMD_TXIPG(x) ((x) << 19)

#define FW_PORT_CMD_LSTATUS (1U << 31)
#define FW_PORT_CMD_LSTATUS_GET(x) (((x) >> 31) & 0x1)
#define FW_PORT_CMD_LSPEED(x) ((x) << 24)
#define FW_PORT_CMD_LSPEED_GET(x) (((x) >> 24) & 0x3f)
#define FW_PORT_CMD_TXPAUSE (1U << 23)
#define FW_PORT_CMD_RXPAUSE (1U << 22)
#define FW_PORT_CMD_MDIOCAP (1U << 21)
#define FW_PORT_CMD_MDIOADDR_GET(x) (((x) >> 16) & 0x1f)
#define FW_PORT_CMD_LPTXPAUSE (1U << 15)
#define FW_PORT_CMD_LPRXPAUSE (1U << 14)
#define FW_PORT_CMD_PTYPE_MASK 0x1f
#define FW_PORT_CMD_PTYPE_GET(x) (((x) >> 8) & FW_PORT_CMD_PTYPE_MASK)
#define FW_PORT_CMD_MODTYPE_MASK 0x1f
#define FW_PORT_CMD_MODTYPE_GET(x) (((x) >> 0) & FW_PORT_CMD_MODTYPE_MASK)

#define FW_PORT_CMD_PPPEN(x) ((x) << 31)
#define FW_PORT_CMD_TPSRC(x) ((x) << 28)
#define FW_PORT_CMD_NCSISRC(x) ((x) << 24)

#define FW_PORT_CMD_CH0(x) ((x) << 20)
#define FW_PORT_CMD_CH1(x) ((x) << 16)
#define FW_PORT_CMD_CH2(x) ((x) << 12)
#define FW_PORT_CMD_CH3(x) ((x) << 8)
#define FW_PORT_CMD_NCSICH(x) ((x) << 4)

enum fw_port_type {
	FW_PORT_TYPE_FIBER_XFI,
	FW_PORT_TYPE_FIBER_XAUI,
	FW_PORT_TYPE_BT_SGMII,
	FW_PORT_TYPE_BT_XFI,
	FW_PORT_TYPE_BT_XAUI,
	FW_PORT_TYPE_KX4,
	FW_PORT_TYPE_CX4,
	FW_PORT_TYPE_KX,
	FW_PORT_TYPE_KR,
	FW_PORT_TYPE_SFP,
	FW_PORT_TYPE_BP_AP,
	FW_PORT_TYPE_BP4_AP,

	FW_PORT_TYPE_NONE = FW_PORT_CMD_PTYPE_MASK
};

enum fw_port_module_type {
	FW_PORT_MOD_TYPE_NA,
	FW_PORT_MOD_TYPE_LR,
	FW_PORT_MOD_TYPE_SR,
	FW_PORT_MOD_TYPE_ER,
	FW_PORT_MOD_TYPE_TWINAX_PASSIVE,
	FW_PORT_MOD_TYPE_TWINAX_ACTIVE,
	FW_PORT_MOD_TYPE_LRM,
	FW_PORT_MOD_TYPE_ERROR		= FW_PORT_CMD_MODTYPE_MASK - 3,
	FW_PORT_MOD_TYPE_UNKNOWN	= FW_PORT_CMD_MODTYPE_MASK - 2,
	FW_PORT_MOD_TYPE_NOTSUPPORTED	= FW_PORT_CMD_MODTYPE_MASK - 1,

	FW_PORT_MOD_TYPE_NONE = FW_PORT_CMD_MODTYPE_MASK
};

/* port stats */
#define FW_NUM_PORT_STATS 50
#define FW_NUM_PORT_TX_STATS 23
#define FW_NUM_PORT_RX_STATS 27

enum fw_port_stats_tx_index {
	FW_STAT_TX_PORT_BYTES_IX,
	FW_STAT_TX_PORT_FRAMES_IX,
	FW_STAT_TX_PORT_BCAST_IX,
	FW_STAT_TX_PORT_MCAST_IX,
	FW_STAT_TX_PORT_UCAST_IX,
	FW_STAT_TX_PORT_ERROR_IX,
	FW_STAT_TX_PORT_64B_IX,
	FW_STAT_TX_PORT_65B_127B_IX,
	FW_STAT_TX_PORT_128B_255B_IX,
	FW_STAT_TX_PORT_256B_511B_IX,
	FW_STAT_TX_PORT_512B_1023B_IX,
	FW_STAT_TX_PORT_1024B_1518B_IX,
	FW_STAT_TX_PORT_1519B_MAX_IX,
	FW_STAT_TX_PORT_DROP_IX,
	FW_STAT_TX_PORT_PAUSE_IX,
	FW_STAT_TX_PORT_PPP0_IX,
	FW_STAT_TX_PORT_PPP1_IX,
	FW_STAT_TX_PORT_PPP2_IX,
	FW_STAT_TX_PORT_PPP3_IX,
	FW_STAT_TX_PORT_PPP4_IX,
	FW_STAT_TX_PORT_PPP5_IX,
	FW_STAT_TX_PORT_PPP6_IX,
	FW_STAT_TX_PORT_PPP7_IX
};

enum fw_port_stat_rx_index {
	FW_STAT_RX_PORT_BYTES_IX,
	FW_STAT_RX_PORT_FRAMES_IX,
	FW_STAT_RX_PORT_BCAST_IX,
	FW_STAT_RX_PORT_MCAST_IX,
	FW_STAT_RX_PORT_UCAST_IX,
	FW_STAT_RX_PORT_MTU_ERROR_IX,
	FW_STAT_RX_PORT_MTU_CRC_ERROR_IX,
	FW_STAT_RX_PORT_CRC_ERROR_IX,
	FW_STAT_RX_PORT_LEN_ERROR_IX,
	FW_STAT_RX_PORT_SYM_ERROR_IX,
	FW_STAT_RX_PORT_64B_IX,
	FW_STAT_RX_PORT_65B_127B_IX,
	FW_STAT_RX_PORT_128B_255B_IX,
	FW_STAT_RX_PORT_256B_511B_IX,
	FW_STAT_RX_PORT_512B_1023B_IX,
	FW_STAT_RX_PORT_1024B_1518B_IX,
	FW_STAT_RX_PORT_1519B_MAX_IX,
	FW_STAT_RX_PORT_PAUSE_IX,
	FW_STAT_RX_PORT_PPP0_IX,
	FW_STAT_RX_PORT_PPP1_IX,
	FW_STAT_RX_PORT_PPP2_IX,
	FW_STAT_RX_PORT_PPP3_IX,
	FW_STAT_RX_PORT_PPP4_IX,
	FW_STAT_RX_PORT_PPP5_IX,
	FW_STAT_RX_PORT_PPP6_IX,
	FW_STAT_RX_PORT_PPP7_IX,
	FW_STAT_RX_PORT_LESS_64B_IX
};

struct fw_port_stats_cmd {
	__be32 op_to_portid;
	__be32 retval_len16;
	union fw_port_stats {
		struct fw_port_stats_ctl {
			u8 nstats_bg_bm;
			u8 tx_ix;
			__be16 r6;
			__be32 r7;
			__be64 stat0;
			__be64 stat1;
			__be64 stat2;
			__be64 stat3;
			__be64 stat4;
			__be64 stat5;
		} ctl;
		struct fw_port_stats_all {
			__be64 tx_bytes;
			__be64 tx_frames;
			__be64 tx_bcast;
			__be64 tx_mcast;
			__be64 tx_ucast;
			__be64 tx_error;
			__be64 tx_64b;
			__be64 tx_65b_127b;
			__be64 tx_128b_255b;
			__be64 tx_256b_511b;
			__be64 tx_512b_1023b;
			__be64 tx_1024b_1518b;
			__be64 tx_1519b_max;
			__be64 tx_drop;
			__be64 tx_pause;
			__be64 tx_ppp0;
			__be64 tx_ppp1;
			__be64 tx_ppp2;
			__be64 tx_ppp3;
			__be64 tx_ppp4;
			__be64 tx_ppp5;
			__be64 tx_ppp6;
			__be64 tx_ppp7;
			__be64 rx_bytes;
			__be64 rx_frames;
			__be64 rx_bcast;
			__be64 rx_mcast;
			__be64 rx_ucast;
			__be64 rx_mtu_error;
			__be64 rx_mtu_crc_error;
			__be64 rx_crc_error;
			__be64 rx_len_error;
			__be64 rx_sym_error;
			__be64 rx_64b;
			__be64 rx_65b_127b;
			__be64 rx_128b_255b;
			__be64 rx_256b_511b;
			__be64 rx_512b_1023b;
			__be64 rx_1024b_1518b;
			__be64 rx_1519b_max;
			__be64 rx_pause;
			__be64 rx_ppp0;
			__be64 rx_ppp1;
			__be64 rx_ppp2;
			__be64 rx_ppp3;
			__be64 rx_ppp4;
			__be64 rx_ppp5;
			__be64 rx_ppp6;
			__be64 rx_ppp7;
			__be64 rx_less_64b;
			__be64 rx_bg_drop;
			__be64 rx_bg_trunc;
		} all;
	} u;
};

#define FW_PORT_STATS_CMD_NSTATS(x) ((x) << 4)
#define FW_PORT_STATS_CMD_BG_BM(x) ((x) << 0)
#define FW_PORT_STATS_CMD_TX(x) ((x) << 7)
#define FW_PORT_STATS_CMD_IX(x) ((x) << 0)

/* port loopback stats */
#define FW_NUM_LB_STATS 16
enum fw_port_lb_stats_index {
	FW_STAT_LB_PORT_BYTES_IX,
	FW_STAT_LB_PORT_FRAMES_IX,
	FW_STAT_LB_PORT_BCAST_IX,
	FW_STAT_LB_PORT_MCAST_IX,
	FW_STAT_LB_PORT_UCAST_IX,
	FW_STAT_LB_PORT_ERROR_IX,
	FW_STAT_LB_PORT_64B_IX,
	FW_STAT_LB_PORT_65B_127B_IX,
	FW_STAT_LB_PORT_128B_255B_IX,
	FW_STAT_LB_PORT_256B_511B_IX,
	FW_STAT_LB_PORT_512B_1023B_IX,
	FW_STAT_LB_PORT_1024B_1518B_IX,
	FW_STAT_LB_PORT_1519B_MAX_IX,
	FW_STAT_LB_PORT_DROP_FRAMES_IX
};

struct fw_port_lb_stats_cmd {
	__be32 op_to_lbport;
	__be32 retval_len16;
	union fw_port_lb_stats {
		struct fw_port_lb_stats_ctl {
			u8 nstats_bg_bm;
			u8 ix_pkd;
			__be16 r6;
			__be32 r7;
			__be64 stat0;
			__be64 stat1;
			__be64 stat2;
			__be64 stat3;
			__be64 stat4;
			__be64 stat5;
		} ctl;
		struct fw_port_lb_stats_all {
			__be64 tx_bytes;
			__be64 tx_frames;
			__be64 tx_bcast;
			__be64 tx_mcast;
			__be64 tx_ucast;
			__be64 tx_error;
			__be64 tx_64b;
			__be64 tx_65b_127b;
			__be64 tx_128b_255b;
			__be64 tx_256b_511b;
			__be64 tx_512b_1023b;
			__be64 tx_1024b_1518b;
			__be64 tx_1519b_max;
			__be64 rx_lb_drop;
			__be64 rx_lb_trunc;
		} all;
	} u;
};

#define FW_PORT_LB_STATS_CMD_LBPORT(x) ((x) << 0)
#define FW_PORT_LB_STATS_CMD_NSTATS(x) ((x) << 4)
#define FW_PORT_LB_STATS_CMD_BG_BM(x) ((x) << 0)
#define FW_PORT_LB_STATS_CMD_IX(x) ((x) << 0)

struct fw_rss_ind_tbl_cmd {
	__be32 op_to_viid;
#define FW_RSS_IND_TBL_CMD_VIID(x) ((x) << 0)
	__be32 retval_len16;
	__be16 niqid;
	__be16 startidx;
	__be32 r3;
	__be32 iq0_to_iq2;
#define FW_RSS_IND_TBL_CMD_IQ0(x) ((x) << 20)
#define FW_RSS_IND_TBL_CMD_IQ1(x) ((x) << 10)
#define FW_RSS_IND_TBL_CMD_IQ2(x) ((x) << 0)
	__be32 iq3_to_iq5;
	__be32 iq6_to_iq8;
	__be32 iq9_to_iq11;
	__be32 iq12_to_iq14;
	__be32 iq15_to_iq17;
	__be32 iq18_to_iq20;
	__be32 iq21_to_iq23;
	__be32 iq24_to_iq26;
	__be32 iq27_to_iq29;
	__be32 iq30_iq31;
	__be32 r15_lo;
};

struct fw_rss_glb_config_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	union fw_rss_glb_config {
		struct fw_rss_glb_config_manual {
			__be32 mode_pkd;
			__be32 r3;
			__be64 r4;
			__be64 r5;
		} manual;
		struct fw_rss_glb_config_basicvirtual {
			__be32 mode_pkd;
			__be32 synmapen_to_hashtoeplitz;
#define FW_RSS_GLB_CONFIG_CMD_SYNMAPEN      (1U << 8)
#define FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6 (1U << 7)
#define FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6 (1U << 6)
#define FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4 (1U << 5)
#define FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4 (1U << 4)
#define FW_RSS_GLB_CONFIG_CMD_OFDMAPEN      (1U << 3)
#define FW_RSS_GLB_CONFIG_CMD_TNLMAPEN      (1U << 2)
#define FW_RSS_GLB_CONFIG_CMD_TNLALLLKP     (1U << 1)
#define FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ  (1U << 0)
			__be64 r8;
			__be64 r9;
		} basicvirtual;
	} u;
};

#define FW_RSS_GLB_CONFIG_CMD_MODE(x)	((x) << 28)
#define FW_RSS_GLB_CONFIG_CMD_MODE_GET(x) (((x) >> 28) & 0xf)

#define FW_RSS_GLB_CONFIG_CMD_MODE_MANUAL	0
#define FW_RSS_GLB_CONFIG_CMD_MODE_BASICVIRTUAL	1

struct fw_rss_vi_config_cmd {
	__be32 op_to_viid;
#define FW_RSS_VI_CONFIG_CMD_VIID(x) ((x) << 0)
	__be32 retval_len16;
	union fw_rss_vi_config {
		struct fw_rss_vi_config_manual {
			__be64 r3;
			__be64 r4;
			__be64 r5;
		} manual;
		struct fw_rss_vi_config_basicvirtual {
			__be32 r6;
			__be32 defaultq_to_udpen;
#define FW_RSS_VI_CONFIG_CMD_DEFAULTQ(x)  ((x) << 16)
#define FW_RSS_VI_CONFIG_CMD_DEFAULTQ_GET(x) (((x) >> 16) & 0x3ff)
#define FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN (1U << 4)
#define FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN  (1U << 3)
#define FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN (1U << 2)
#define FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN  (1U << 1)
#define FW_RSS_VI_CONFIG_CMD_UDPEN        (1U << 0)
			__be64 r9;
			__be64 r10;
		} basicvirtual;
	} u;
};

enum fw_error_type {
	FW_ERROR_TYPE_EXCEPTION		= 0x0,
	FW_ERROR_TYPE_HWMODULE		= 0x1,
	FW_ERROR_TYPE_WR		= 0x2,
	FW_ERROR_TYPE_ACL		= 0x3,
};

struct fw_error_cmd {
	__be32 op_to_type;
	__be32 len16_pkd;
	union fw_error {
		struct fw_error_exception {
			__be32 info[6];
		} exception;
		struct fw_error_hwmodule {
			__be32 regaddr;
			__be32 regval;
		} hwmodule;
		struct fw_error_wr {
			__be16 cidx;
			__be16 pfn_vfn;
			__be32 eqid;
			u8 wrhdr[16];
		} wr;
		struct fw_error_acl {
			__be16 cidx;
			__be16 pfn_vfn;
			__be32 eqid;
			__be16 mv_pkd;
			u8 val[6];
			__be64 r4;
		} acl;
	} u;
};

struct fw_debug_cmd {
	__be32 op_type;
#define FW_DEBUG_CMD_TYPE_GET(x) ((x) & 0xff)
	__be32 len16_pkd;
	union fw_debug {
		struct fw_debug_assert {
			__be32 fcid;
			__be32 line;
			__be32 x;
			__be32 y;
			u8 filename_0_7[8];
			u8 filename_8_15[8];
			__be64 r3;
		} assert;
		struct fw_debug_prt {
			__be16 dprtstridx;
			__be16 r3[3];
			__be32 dprtstrparam0;
			__be32 dprtstrparam1;
			__be32 dprtstrparam2;
			__be32 dprtstrparam3;
		} prt;
	} u;
};

#define FW_PCIE_FW_ERR           (1U << 31)
#define FW_PCIE_FW_INIT          (1U << 30)
#define FW_PCIE_FW_HALT          (1U << 29)
#define FW_PCIE_FW_MASTER_VLD    (1U << 15)
#define FW_PCIE_FW_MASTER_MASK   0x7
#define FW_PCIE_FW_MASTER_SHIFT  12
#define FW_PCIE_FW_MASTER(x)     ((x) << FW_PCIE_FW_MASTER_SHIFT)
#define FW_PCIE_FW_MASTER_GET(x) (((x) >> FW_PCIE_FW_MASTER_SHIFT) & \
				 FW_PCIE_FW_MASTER_MASK)

struct fw_hdr {
	u8 ver;
	u8 reserved1;
	__be16	len512;			/* bin length in units of 512-bytes */
	__be32	fw_ver;			/* firmware version */
	__be32	tp_microcode_ver;
	u8 intfver_nic;
	u8 intfver_vnic;
	u8 intfver_ofld;
	u8 intfver_ri;
	u8 intfver_iscsipdu;
	u8 intfver_iscsi;
	u8 intfver_fcoe;
	u8 reserved2;
	__u32   reserved3;
	__u32   reserved4;
	__u32   reserved5;
	__be32  flags;
	__be32  reserved6[23];
};

#define FW_HDR_FW_VER_MAJOR_GET(x) (((x) >> 24) & 0xff)
#define FW_HDR_FW_VER_MINOR_GET(x) (((x) >> 16) & 0xff)
#define FW_HDR_FW_VER_MICRO_GET(x) (((x) >> 8) & 0xff)
#define FW_HDR_FW_VER_BUILD_GET(x) (((x) >> 0) & 0xff)

enum fw_hdr_flags {
	FW_HDR_FLAGS_RESET_HALT = 0x00000001,
};

#endif /* _T4FW_INTERFACE_H_ */
