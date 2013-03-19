/*
 * PMC-Sierra SPCv/ve 8088/8089 SAS/SATA based host adapters driver
 *
 * Copyright (c) 2008-2009 USI Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions, and the following disclaimer,
 *	without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *	substantially similar to the "NO WARRANTY" disclaimer below
 *	("Disclaimer") and any redistribution must be conditioned upon
 *	including a substantially similar Disclaimer requirement for further
 *	binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *	of any contributors may be used to endorse or promote products derived
 *	from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */

#ifndef _PMC8001_REG_H_
#define _PMC8001_REG_H_

#include <linux/types.h>
#include <scsi/libsas.h>

/* for Request Opcode of IOMB */
#define OPC_INB_ECHO				1	/* 0x000 */
#define OPC_INB_PHYSTART			4	/* 0x004 */
#define OPC_INB_PHYSTOP				5	/* 0x005 */
#define OPC_INB_SSPINIIOSTART			6	/* 0x006 */
#define OPC_INB_SSPINITMSTART			7	/* 0x007 */
/* 0x8 RESV IN SPCv */
#define OPC_INB_RSVD				8	/* 0x008 */
#define OPC_INB_DEV_HANDLE_ACCEPT		9	/* 0x009 */
#define OPC_INB_SSPTGTIOSTART			10	/* 0x00A */
#define OPC_INB_SSPTGTRSPSTART			11	/* 0x00B */
/* 0xC, 0xD, 0xE removed in SPCv */
#define OPC_INB_SSP_ABORT			15	/* 0x00F */
#define OPC_INB_DEREG_DEV_HANDLE		16	/* 0x010 */
#define OPC_INB_GET_DEV_HANDLE			17	/* 0x011 */
#define OPC_INB_SMP_REQUEST			18	/* 0x012 */
/* 0x13 SMP_RESPONSE is removed in SPCv */
#define OPC_INB_SMP_ABORT			20	/* 0x014 */
/* 0x16 RESV IN SPCv */
#define OPC_INB_RSVD1				22	/* 0x016 */
#define OPC_INB_SATA_HOST_OPSTART		23	/* 0x017 */
#define OPC_INB_SATA_ABORT			24	/* 0x018 */
#define OPC_INB_LOCAL_PHY_CONTROL		25	/* 0x019 */
/* 0x1A RESV IN SPCv */
#define OPC_INB_RSVD2				26	/* 0x01A */
#define OPC_INB_FW_FLASH_UPDATE			32	/* 0x020 */
#define OPC_INB_GPIO				34	/* 0x022 */
#define OPC_INB_SAS_DIAG_MODE_START_END		35	/* 0x023 */
#define OPC_INB_SAS_DIAG_EXECUTE		36	/* 0x024 */
/* 0x25 RESV IN SPCv */
#define OPC_INB_RSVD3				37	/* 0x025 */
#define OPC_INB_GET_TIME_STAMP			38	/* 0x026 */
#define OPC_INB_PORT_CONTROL			39	/* 0x027 */
#define OPC_INB_GET_NVMD_DATA			40	/* 0x028 */
#define OPC_INB_SET_NVMD_DATA			41	/* 0x029 */
#define OPC_INB_SET_DEVICE_STATE		42	/* 0x02A */
#define OPC_INB_GET_DEVICE_STATE		43	/* 0x02B */
#define OPC_INB_SET_DEV_INFO			44	/* 0x02C */
/* 0x2D RESV IN SPCv */
#define OPC_INB_RSVD4				45	/* 0x02D */
#define OPC_INB_SGPIO_REGISTER			46	/* 0x02E */
#define OPC_INB_PCIE_DIAG_EXEC			47	/* 0x02F */
#define OPC_INB_SET_CONTROLLER_CONFIG		48	/* 0x030 */
#define OPC_INB_GET_CONTROLLER_CONFIG		49	/* 0x031 */
#define OPC_INB_REG_DEV				50	/* 0x032 */
#define OPC_INB_SAS_HW_EVENT_ACK		51	/* 0x033 */
#define OPC_INB_GET_DEVICE_INFO			52	/* 0x034 */
#define OPC_INB_GET_PHY_PROFILE			53	/* 0x035 */
#define OPC_INB_FLASH_OP_EXT			54	/* 0x036 */
#define OPC_INB_SET_PHY_PROFILE			55	/* 0x037 */
#define OPC_INB_KEK_MANAGEMENT			256	/* 0x100 */
#define OPC_INB_DEK_MANAGEMENT			257	/* 0x101 */
#define OPC_INB_SSP_INI_DIF_ENC_IO		258	/* 0x102 */
#define OPC_INB_SATA_DIF_ENC_IO			259	/* 0x103 */

/* for Response Opcode of IOMB */
#define OPC_OUB_ECHO					1	/* 0x001 */
#define OPC_OUB_RSVD					4	/* 0x004 */
#define OPC_OUB_SSP_COMP				5	/* 0x005 */
#define OPC_OUB_SMP_COMP				6	/* 0x006 */
#define OPC_OUB_LOCAL_PHY_CNTRL				7	/* 0x007 */
#define OPC_OUB_RSVD1					10	/* 0x00A */
#define OPC_OUB_DEREG_DEV				11	/* 0x00B */
#define OPC_OUB_GET_DEV_HANDLE				12	/* 0x00C */
#define OPC_OUB_SATA_COMP				13	/* 0x00D */
#define OPC_OUB_SATA_EVENT				14	/* 0x00E */
#define OPC_OUB_SSP_EVENT				15	/* 0x00F */
#define OPC_OUB_RSVD2					16	/* 0x010 */
/* 0x11 - SMP_RECEIVED Notification removed in SPCv*/
#define OPC_OUB_SSP_RECV_EVENT				18	/* 0x012 */
#define OPC_OUB_RSVD3					19	/* 0x013 */
#define OPC_OUB_FW_FLASH_UPDATE				20	/* 0x014 */
#define OPC_OUB_GPIO_RESPONSE				22	/* 0x016 */
#define OPC_OUB_GPIO_EVENT				23	/* 0x017 */
#define OPC_OUB_GENERAL_EVENT				24	/* 0x018 */
#define OPC_OUB_SSP_ABORT_RSP				26	/* 0x01A */
#define OPC_OUB_SATA_ABORT_RSP				27	/* 0x01B */
#define OPC_OUB_SAS_DIAG_MODE_START_END			28	/* 0x01C */
#define OPC_OUB_SAS_DIAG_EXECUTE			29	/* 0x01D */
#define OPC_OUB_GET_TIME_STAMP				30	/* 0x01E */
#define OPC_OUB_RSVD4					31	/* 0x01F */
#define OPC_OUB_PORT_CONTROL				32	/* 0x020 */
#define OPC_OUB_SKIP_ENTRY				33	/* 0x021 */
#define OPC_OUB_SMP_ABORT_RSP				34	/* 0x022 */
#define OPC_OUB_GET_NVMD_DATA				35	/* 0x023 */
#define OPC_OUB_SET_NVMD_DATA				36	/* 0x024 */
#define OPC_OUB_DEVICE_HANDLE_REMOVAL			37	/* 0x025 */
#define OPC_OUB_SET_DEVICE_STATE			38	/* 0x026 */
#define OPC_OUB_GET_DEVICE_STATE			39	/* 0x027 */
#define OPC_OUB_SET_DEV_INFO				40	/* 0x028 */
#define OPC_OUB_RSVD5					41	/* 0x029 */
#define OPC_OUB_HW_EVENT				1792	/* 0x700 */
#define OPC_OUB_DEV_HANDLE_ARRIV			1824	/* 0x720 */
#define OPC_OUB_THERM_HW_EVENT				1840	/* 0x730 */
#define OPC_OUB_SGPIO_RESP				2094	/* 0x82E */
#define OPC_OUB_PCIE_DIAG_EXECUTE			2095	/* 0x82F */
#define OPC_OUB_DEV_REGIST				2098	/* 0x832 */
#define OPC_OUB_SAS_HW_EVENT_ACK			2099	/* 0x833 */
#define OPC_OUB_GET_DEVICE_INFO				2100	/* 0x834 */
/* spcv specific commands */
#define OPC_OUB_PHY_START_RESP				2052	/* 0x804 */
#define OPC_OUB_PHY_STOP_RESP				2053	/* 0x805 */
#define OPC_OUB_SET_CONTROLLER_CONFIG			2096	/* 0x830 */
#define OPC_OUB_GET_CONTROLLER_CONFIG			2097	/* 0x831 */
#define OPC_OUB_GET_PHY_PROFILE				2101	/* 0x835 */
#define OPC_OUB_FLASH_OP_EXT				2102	/* 0x836 */
#define OPC_OUB_SET_PHY_PROFILE				2103	/* 0x837 */
#define OPC_OUB_KEK_MANAGEMENT_RESP			2304	/* 0x900 */
#define OPC_OUB_DEK_MANAGEMENT_RESP			2305	/* 0x901 */
#define OPC_OUB_SSP_COALESCED_COMP_RESP			2306	/* 0x902 */

/* for phy start*/
#define SSC_DISABLE_15			(0x01 << 16)
#define SSC_DISABLE_30			(0x02 << 16)
#define SSC_DISABLE_60			(0x04 << 16)
#define SAS_ASE				(0x01 << 15)
#define SPINHOLD_DISABLE		(0x00 << 14)
#define SPINHOLD_ENABLE			(0x01 << 14)
#define LINKMODE_SAS			(0x01 << 12)
#define LINKMODE_DSATA			(0x02 << 12)
#define LINKMODE_AUTO			(0x03 << 12)
#define LINKRATE_15			(0x01 << 8)
#define LINKRATE_30			(0x02 << 8)
#define LINKRATE_60			(0x06 << 8)

/* Thermal related */
#define	THERMAL_ENABLE			0x1
#define	THERMAL_LOG_ENABLE		0x1
#define THERMAL_OP_CODE			0x6
#define LTEMPHIL			 70
#define RTEMPHIL			100

/* Encryption info */
#define SCRATCH_PAD3_ENC_DISABLED	0x00000000
#define SCRATCH_PAD3_ENC_DIS_ERR	0x00000001
#define SCRATCH_PAD3_ENC_ENA_ERR	0x00000002
#define SCRATCH_PAD3_ENC_READY		0x00000003
#define SCRATCH_PAD3_ENC_MASK		SCRATCH_PAD3_ENC_READY

#define SCRATCH_PAD3_XTS_ENABLED		(1 << 14)
#define SCRATCH_PAD3_SMA_ENABLED		(1 << 4)
#define SCRATCH_PAD3_SMB_ENABLED		(1 << 5)
#define SCRATCH_PAD3_SMF_ENABLED		0
#define SCRATCH_PAD3_SM_MASK			0x000000F0
#define SCRATCH_PAD3_ERR_CODE			0x00FF0000

#define SEC_MODE_SMF				0x0
#define SEC_MODE_SMA				0x100
#define SEC_MODE_SMB				0x200
#define CIPHER_MODE_ECB				0x00000001
#define CIPHER_MODE_XTS				0x00000002
#define KEK_MGMT_SUBOP_KEYCARDUPDATE		0x4

/* SAS protocol timer configuration page */
#define SAS_PROTOCOL_TIMER_CONFIG_PAGE  0x04
#define STP_MCT_TMO                     32
#define SSP_MCT_TMO                     32
#define SAS_MAX_OPEN_TIME				5
#define SMP_MAX_CONN_TIMER              0xFF
#define STP_FRM_TIMER                   0
#define STP_IDLE_TIME                   5 /* 5 us; controller default */
#define SAS_MFD                         0
#define SAS_OPNRJT_RTRY_INTVL           2
#define SAS_DOPNRJT_RTRY_TMO            128
#define SAS_COPNRJT_RTRY_TMO            128

/*
  Making ORR bigger than IT NEXUS LOSS which is 2000000us = 2 second.
  Assuming a bigger value 3 second, 3000000/128 = 23437.5 where 128
  is DOPNRJT_RTRY_TMO
*/
#define SAS_DOPNRJT_RTRY_THR            23438
#define SAS_COPNRJT_RTRY_THR            23438
#define SAS_MAX_AIP                     0x200000
#define IT_NEXUS_TIMEOUT       0x7D0
#define PORT_RECOVERY_TIMEOUT  ((IT_NEXUS_TIMEOUT/100) + 30)

struct mpi_msg_hdr {
	__le32	header;	/* Bits [11:0] - Message operation code */
	/* Bits [15:12] - Message Category */
	/* Bits [21:16] - Outboundqueue ID for the
	operation completion message */
	/* Bits [23:22] - Reserved */
	/* Bits [28:24] - Buffer Count, indicates how
	many buffer are allocated for the massage */
	/* Bits [30:29] - Reserved */
	/* Bits [31] - Message Valid bit */
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of PHY Start Command
 * use to describe enable the phy (128 bytes)
 */
struct phy_start_req {
	__le32	tag;
	__le32	ase_sh_lm_slr_phyid;
	struct sas_identify_frame sas_identify; /* 28 Bytes */
	__le32 spasti;
	u32	reserved[21];
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of PHY Start Command
 * use to disable the phy (128 bytes)
 */
struct phy_stop_req {
	__le32	tag;
	__le32	phy_id;
	u32	reserved[29];
} __attribute__((packed, aligned(4)));

/* set device bits fis - device to host */
struct set_dev_bits_fis {
	u8	fis_type;	/* 0xA1*/
	u8	n_i_pmport;
	/* b7 : n Bit. Notification bit. If set device needs attention. */
	/* b6 : i Bit. Interrupt Bit */
	/* b5-b4: reserved2 */
	/* b3-b0: PM Port */
	u8	status;
	u8	error;
	u32	_r_a;
} __attribute__ ((packed));
/* PIO setup FIS - device to host */
struct pio_setup_fis {
	u8	fis_type;	/* 0x5f */
	u8	i_d_pmPort;
	/* b7 : reserved */
	/* b6 : i bit. Interrupt bit */
	/* b5 : d bit. data transfer direction. set to 1 for device to host
	xfer */
	/* b4 : reserved */
	/* b3-b0: PM Port */
	u8	status;
	u8	error;
	u8	lbal;
	u8	lbam;
	u8	lbah;
	u8	device;
	u8	lbal_exp;
	u8	lbam_exp;
	u8	lbah_exp;
	u8	_r_a;
	u8	sector_count;
	u8	sector_count_exp;
	u8	_r_b;
	u8	e_status;
	u8	_r_c[2];
	u8	transfer_count;
} __attribute__ ((packed));

/*
 * brief the data structure of SATA Completion Response
 * use to describe the sata task response (64 bytes)
 */
struct sata_completion_resp {
	__le32	tag;
	__le32	status;
	__le32	param;
	u32	sata_resp[12];
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of SAS HW Event Notification
 * use to alert the host about the hardware event(64 bytes)
 */
/* updated outbound struct for spcv */

struct hw_event_resp {
	__le32	lr_status_evt_portid;
	__le32	evt_param;
	__le32	phyid_npip_portstate;
	struct sas_identify_frame	sas_identify;
	struct dev_to_host_fis	sata_fis;
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure for thermal event notification
 */

struct thermal_hw_event {
	__le32	thermal_event;
	__le32	rht_lht;
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of REGISTER DEVICE Command
 * use to describe MPI REGISTER DEVICE Command (64 bytes)
 */

struct reg_dev_req {
	__le32	tag;
	__le32	phyid_portid;
	__le32	dtype_dlr_mcn_ir_retry;
	__le32	firstburstsize_ITNexustimeout;
	u8	sas_addr[SAS_ADDR_SIZE];
	__le32	upper_device_id;
	u32	reserved[24];
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of DEREGISTER DEVICE Command
 * use to request spc to remove all internal resources associated
 * with the device id (64 bytes)
 */

struct dereg_dev_req {
	__le32	tag;
	__le32	device_id;
	u32	reserved[29];
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of DEVICE_REGISTRATION Response
 * use to notify the completion of the device registration (64 bytes)
 */
struct dev_reg_resp {
	__le32	tag;
	__le32	status;
	__le32	device_id;
	u32	reserved[12];
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of Local PHY Control Command
 * use to issue PHY CONTROL to local phy (64 bytes)
 */
struct local_phy_ctl_req {
	__le32	tag;
	__le32	phyop_phyid;
	u32	reserved1[29];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure of Local Phy Control Response
 * use to describe MPI Local Phy Control Response (64 bytes)
 */
 struct local_phy_ctl_resp {
	__le32	tag;
	__le32	phyop_phyid;
	__le32	status;
	u32	reserved[12];
} __attribute__((packed, aligned(4)));

#define OP_BITS 0x0000FF00
#define ID_BITS 0x000000FF

/*
 * brief the data structure of PORT Control Command
 * use to control port properties (64 bytes)
 */

struct port_ctl_req {
	__le32	tag;
	__le32	portop_portid;
	__le32	param0;
	__le32	param1;
	u32	reserved1[27];
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of HW Event Ack Command
 * use to acknowledge receive HW event (64 bytes)
 */
struct hw_event_ack_req {
	__le32	tag;
	__le32	phyid_sea_portid;
	__le32	param0;
	__le32	param1;
	u32	reserved1[27];
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of PHY_START Response Command
 * indicates the completion of PHY_START command (64 bytes)
 */
struct phy_start_resp {
	__le32	tag;
	__le32	status;
	__le32	phyid;
	u32	reserved[12];
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of PHY_STOP Response Command
 * indicates the completion of PHY_STOP command (64 bytes)
 */
struct phy_stop_resp {
	__le32	tag;
	__le32	status;
	__le32	phyid;
	u32	reserved[12];
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of SSP Completion Response
 * use to indicate a SSP Completion (n bytes)
 */
struct ssp_completion_resp {
	__le32	tag;
	__le32	status;
	__le32	param;
	__le32	ssptag_rescv_rescpad;
	struct ssp_response_iu ssp_resp_iu;
	__le32	residual_count;
} __attribute__((packed, aligned(4)));

#define SSP_RESCV_BIT	0x00010000

/*
 * brief the data structure of SATA EVNET response
 * use to indicate a SATA Completion (64 bytes)
 */
struct sata_event_resp {
	__le32 tag;
	__le32 event;
	__le32 port_id;
	__le32 device_id;
	u32 reserved;
	__le32 event_param0;
	__le32 event_param1;
	__le32 sata_addr_h32;
	__le32 sata_addr_l32;
	__le32 e_udt1_udt0_crc;
	__le32 e_udt5_udt4_udt3_udt2;
	__le32 a_udt1_udt0_crc;
	__le32 a_udt5_udt4_udt3_udt2;
	__le32 hwdevid_diferr;
	__le32 err_framelen_byteoffset;
	__le32 err_dataframe;
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of SSP EVNET esponse
 * use to indicate a SSP Completion (64 bytes)
 */
struct ssp_event_resp {
	__le32 tag;
	__le32 event;
	__le32 port_id;
	__le32 device_id;
	__le32 ssp_tag;
	__le32 event_param0;
	__le32 event_param1;
	__le32 sas_addr_h32;
	__le32 sas_addr_l32;
	__le32 e_udt1_udt0_crc;
	__le32 e_udt5_udt4_udt3_udt2;
	__le32 a_udt1_udt0_crc;
	__le32 a_udt5_udt4_udt3_udt2;
	__le32 hwdevid_diferr;
	__le32 err_framelen_byteoffset;
	__le32 err_dataframe;
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure of General Event Notification Response
 * use to describe MPI General Event Notification Response (64 bytes)
 */
struct general_event_resp {
	__le32	status;
	__le32	inb_IOMB_payload[14];
} __attribute__((packed, aligned(4)));

#define GENERAL_EVENT_PAYLOAD	14
#define OPCODE_BITS	0x00000fff

/*
 * brief the data structure of SMP Request Command
 * use to describe MPI SMP REQUEST Command (64 bytes)
 */
struct smp_req {
	__le32	tag;
	__le32	device_id;
	__le32	len_ip_ir;
	/* Bits [0] - Indirect response */
	/* Bits [1] - Indirect Payload */
	/* Bits [15:2] - Reserved */
	/* Bits [23:16] - direct payload Len */
	/* Bits [31:24] - Reserved */
	u8	smp_req16[16];
	union {
		u8	smp_req[32];
		struct {
			__le64 long_req_addr;/* sg dma address, LE */
			__le32 long_req_size;/* LE */
			u32	_r_a;
			__le64 long_resp_addr;/* sg dma address, LE */
			__le32 long_resp_size;/* LE */
			u32	_r_b;
			} long_smp_req;/* sequencer extension */
	};
	__le32	rsvd[16];
} __attribute__((packed, aligned(4)));
/*
 * brief the data structure of SMP Completion Response
 * use to describe MPI SMP Completion Response (64 bytes)
 */
struct smp_completion_resp {
	__le32	tag;
	__le32	status;
	__le32	param;
	u8	_r_a[252];
} __attribute__((packed, aligned(4)));

/*
 *brief the data structure of SSP SMP SATA Abort Command
 * use to describe MPI SSP SMP & SATA Abort Command (64 bytes)
 */
struct task_abort_req {
	__le32	tag;
	__le32	device_id;
	__le32	tag_to_abort;
	__le32	abort_all;
	u32	reserved[27];
} __attribute__((packed, aligned(4)));

/* These flags used for SSP SMP & SATA Abort */
#define ABORT_MASK		0x3
#define ABORT_SINGLE		0x0
#define ABORT_ALL		0x1

/**
 * brief the data structure of SSP SATA SMP Abort Response
 * use to describe SSP SMP & SATA Abort Response ( 64 bytes)
 */
struct task_abort_resp {
	__le32	tag;
	__le32	status;
	__le32	scp;
	u32	reserved[12];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure of SAS Diagnostic Start/End Command
 * use to describe MPI SAS Diagnostic Start/End Command (64 bytes)
 */
struct sas_diag_start_end_req {
	__le32	tag;
	__le32	operation_phyid;
	u32	reserved[29];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure of SAS Diagnostic Execute Command
 * use to describe MPI SAS Diagnostic Execute Command (64 bytes)
 */
struct sas_diag_execute_req {
	__le32	tag;
	__le32	cmdtype_cmddesc_phyid;
	__le32	pat1_pat2;
	__le32	threshold;
	__le32	codepat_errmsk;
	__le32	pmon;
	__le32	pERF1CTL;
	u32	reserved[24];
} __attribute__((packed, aligned(4)));

#define SAS_DIAG_PARAM_BYTES 24

/*
 * brief the data structure of Set Device State Command
 * use to describe MPI Set Device State Command (64 bytes)
 */
struct set_dev_state_req {
	__le32	tag;
	__le32	device_id;
	__le32	nds;
	u32	reserved[28];
} __attribute__((packed, aligned(4)));

/*
 * brief the data structure of SATA Start Command
 * use to describe MPI SATA IO Start Command (64 bytes)
 * Note: This structure is common for normal / encryption I/O
 */

struct sata_start_req {
	__le32	tag;
	__le32	device_id;
	__le32	data_len;
	__le32	ncqtag_atap_dir_m_dad;
	struct host_to_dev_fis	sata_fis;
	u32	reserved1;
	u32	reserved2;	/* dword 11. rsvd for normal I/O. */
				/* EPLE Descl for enc I/O */
	u32	addr_low;	/* dword 12. rsvd for enc I/O */
	u32	addr_high;	/* dword 13. reserved for enc I/O */
	__le32	len;		/* dword 14: length for normal I/O. */
				/* EPLE Desch for enc I/O */
	__le32	esgl;		/* dword 15. rsvd for enc I/O */
	__le32	atapi_scsi_cdb[4];	/* dword 16-19. rsvd for enc I/O */
	/* The below fields are reserved for normal I/O */
	__le32	key_index_mode;	/* dword 20 */
	__le32	sector_cnt_enss;/* dword 21 */
	__le32	keytagl;	/* dword 22 */
	__le32	keytagh;	/* dword 23 */
	__le32	twk_val0;	/* dword 24 */
	__le32	twk_val1;	/* dword 25 */
	__le32	twk_val2;	/* dword 26 */
	__le32	twk_val3;	/* dword 27 */
	__le32	enc_addr_low;	/* dword 28. Encryption SGL address high */
	__le32	enc_addr_high;	/* dword 29. Encryption SGL address low */
	__le32	enc_len;	/* dword 30. Encryption length */
	__le32	enc_esgl;	/* dword 31. Encryption esgl bit */
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure of SSP INI TM Start Command
 * use to describe MPI SSP INI TM Start Command (64 bytes)
 */
struct ssp_ini_tm_start_req {
	__le32	tag;
	__le32	device_id;
	__le32	relate_tag;
	__le32	tmf;
	u8	lun[8];
	__le32	ds_ads_m;
	u32	reserved[24];
} __attribute__((packed, aligned(4)));

struct ssp_info_unit {
	u8	lun[8];/* SCSI Logical Unit Number */
	u8	reserved1;/* reserved */
	u8	efb_prio_attr;
	/* B7 : enabledFirstBurst */
	/* B6-3 : taskPriority */
	/* B2-0 : taskAttribute */
	u8	reserved2;	/* reserved */
	u8	additional_cdb_len;
	/* B7-2 : additional_cdb_len */
	/* B1-0 : reserved */
	u8	cdb[16];/* The SCSI CDB up to 16 bytes length */
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure of SSP INI IO Start Command
 * use to describe MPI SSP INI IO Start Command (64 bytes)
 * Note: This structure is common for normal / encryption I/O
 */
struct ssp_ini_io_start_req {
	__le32	tag;
	__le32	device_id;
	__le32	data_len;
	__le32	dad_dir_m_tlr;
	struct ssp_info_unit	ssp_iu;
	__le32	addr_low;	/* dword 12: sgl low for normal I/O. */
				/* epl_descl for encryption I/O */
	__le32	addr_high;	/* dword 13: sgl hi for normal I/O */
				/* dpl_descl for encryption I/O */
	__le32	len;		/* dword 14: len for normal I/O. */
				/* edpl_desch for encryption I/O */
	__le32	esgl;		/* dword 15: ESGL bit for normal I/O. */
				/* user defined tag mask for enc I/O */
	/* The below fields are reserved for normal I/O */
	u8	udt[12];	/* dword 16-18 */
	__le32	sectcnt_ios;	/* dword 19 */
	__le32	key_cmode;	/* dword 20 */
	__le32	ks_enss;	/* dword 21 */
	__le32	keytagl;	/* dword 22 */
	__le32	keytagh;	/* dword 23 */
	__le32	twk_val0;	/* dword 24 */
	__le32	twk_val1;	/* dword 25 */
	__le32	twk_val2;	/* dword 26 */
	__le32	twk_val3;	/* dword 27 */
	__le32	enc_addr_low;	/* dword 28: Encryption sgl addr low */
	__le32	enc_addr_high;	/* dword 29: Encryption sgl addr hi */
	__le32	enc_len;	/* dword 30: Encryption length */
	__le32	enc_esgl;	/* dword 31: ESGL bit for encryption */
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure for SSP_INI_DIF_ENC_IO COMMAND
 * use to initiate SSP I/O operation with optional DIF/ENC
 */
struct ssp_dif_enc_io_req {
	__le32	tag;
	__le32	device_id;
	__le32	data_len;
	__le32	dirMTlr;
	__le32	sspiu0;
	__le32	sspiu1;
	__le32	sspiu2;
	__le32	sspiu3;
	__le32	sspiu4;
	__le32	sspiu5;
	__le32	sspiu6;
	__le32	epl_des;
	__le32	dpl_desl_ndplr;
	__le32	dpl_desh;
	__le32	uum_uuv_bss_difbits;
	u8	udt[12];
	__le32	sectcnt_ios;
	__le32	key_cmode;
	__le32	ks_enss;
	__le32	keytagl;
	__le32	keytagh;
	__le32	twk_val0;
	__le32	twk_val1;
	__le32	twk_val2;
	__le32	twk_val3;
	__le32	addr_low;
	__le32	addr_high;
	__le32	len;
	__le32	esgl;
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure of Firmware download
 * use to describe MPI FW DOWNLOAD Command (64 bytes)
 */
struct fw_flash_Update_req {
	__le32	tag;
	__le32	cur_image_offset;
	__le32	cur_image_len;
	__le32	total_image_len;
	u32	reserved0[7];
	__le32	sgl_addr_lo;
	__le32	sgl_addr_hi;
	__le32	len;
	__le32	ext_reserved;
	u32	reserved1[16];
} __attribute__((packed, aligned(4)));

#define FWFLASH_IOMB_RESERVED_LEN 0x07
/**
 * brief the data structure of FW_FLASH_UPDATE Response
 * use to describe MPI FW_FLASH_UPDATE Response (64 bytes)
 *
 */
 struct fw_flash_Update_resp {
	__le32	tag;
	__le32	status;
	u32	reserved[13];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure of Get NVM Data Command
 * use to get data from NVM in HBA(64 bytes)
 */
struct get_nvm_data_req {
	__le32	tag;
	__le32	len_ir_vpdd;
	__le32	vpd_offset;
	u32	reserved[8];
	__le32	resp_addr_lo;
	__le32	resp_addr_hi;
	__le32	resp_len;
	u32	reserved1[17];
} __attribute__((packed, aligned(4)));

struct set_nvm_data_req {
	__le32	tag;
	__le32	len_ir_vpdd;
	__le32	vpd_offset;
	u32	reserved[8];
	__le32	resp_addr_lo;
	__le32	resp_addr_hi;
	__le32	resp_len;
	u32	reserved1[17];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure for SET CONTROLLER CONFIG COMMAND
 * use to modify controller configuration
 */
struct set_ctrl_cfg_req {
	__le32	tag;
	__le32	cfg_pg[14];
	u32	reserved[16];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure for GET CONTROLLER CONFIG COMMAND
 * use to get controller configuration page
 */
struct get_ctrl_cfg_req {
	__le32	tag;
	__le32	pgcd;
	__le32	int_vec;
	u32	reserved[28];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure for KEK_MANAGEMENT COMMAND
 * use for KEK management
 */
struct kek_mgmt_req {
	__le32	tag;
	__le32	new_curidx_ksop;
	u32	reserved;
	__le32	kblob[12];
	u32	reserved1[16];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure for DEK_MANAGEMENT COMMAND
 * use for DEK management
 */
struct dek_mgmt_req {
	__le32	tag;
	__le32	kidx_dsop;
	__le32	dekidx;
	__le32	addr_l;
	__le32	addr_h;
	__le32	nent;
	__le32	dbf_tblsize;
	u32	reserved[24];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure for SET PHY PROFILE COMMAND
 * use to retrive phy specific information
 */
struct set_phy_profile_req {
	__le32	tag;
	__le32	ppc_phyid;
	u32	reserved[29];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure for GET PHY PROFILE COMMAND
 * use to retrive phy specific information
 */
struct get_phy_profile_req {
	__le32	tag;
	__le32	ppc_phyid;
	__le32	profile[29];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure for EXT FLASH PARTITION
 * use to manage ext flash partition
 */
struct ext_flash_partition_req {
	__le32	tag;
	__le32	cmd;
	__le32	offset;
	__le32	len;
	u32	reserved[7];
	__le32	addr_low;
	__le32	addr_high;
	__le32	len1;
	__le32	ext;
	u32	reserved1[16];
} __attribute__((packed, aligned(4)));

#define TWI_DEVICE	0x0
#define C_SEEPROM	0x1
#define VPD_FLASH	0x4
#define AAP1_RDUMP	0x5
#define IOP_RDUMP	0x6
#define EXPAN_ROM	0x7

#define IPMode		0x80000000
#define NVMD_TYPE	0x0000000F
#define NVMD_STAT	0x0000FFFF
#define NVMD_LEN	0xFF000000
/**
 * brief the data structure of Get NVMD Data Response
 * use to describe MPI Get NVMD Data Response (64 bytes)
 */
struct get_nvm_data_resp {
	__le32		tag;
	__le32		ir_tda_bn_dps_das_nvm;
	__le32		dlen_status;
	__le32		nvm_data[12];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure of SAS Diagnostic Start/End Response
 * use to describe MPI SAS Diagnostic Start/End Response (64 bytes)
 *
 */
struct sas_diag_start_end_resp {
	__le32		tag;
	__le32		status;
	u32		reserved[13];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure of SAS Diagnostic Execute Response
 * use to describe MPI SAS Diagnostic Execute Response (64 bytes)
 *
 */
struct sas_diag_execute_resp {
	__le32		tag;
	__le32		cmdtype_cmddesc_phyid;
	__le32		Status;
	__le32		ReportData;
	u32		reserved[11];
} __attribute__((packed, aligned(4)));

/**
 * brief the data structure of Set Device State Response
 * use to describe MPI Set Device State Response (64 bytes)
 *
 */
struct set_dev_state_resp {
	__le32		tag;
	__le32		status;
	__le32		device_id;
	__le32		pds_nds;
	u32		reserved[11];
} __attribute__((packed, aligned(4)));

/* new outbound structure for spcv - begins */
/**
 * brief the data structure for SET CONTROLLER CONFIG COMMAND
 * use to modify controller configuration
 */
struct set_ctrl_cfg_resp {
	__le32 tag;
	__le32 status;
	__le32 err_qlfr_pgcd;
	u32 reserved[12];
} __attribute__((packed, aligned(4)));

struct get_ctrl_cfg_resp {
	__le32 tag;
	__le32 status;
	__le32 err_qlfr;
	__le32 confg_page[12];
} __attribute__((packed, aligned(4)));

struct kek_mgmt_resp {
	__le32 tag;
	__le32 status;
	__le32 kidx_new_curr_ksop;
	__le32 err_qlfr;
	u32 reserved[11];
} __attribute__((packed, aligned(4)));

struct dek_mgmt_resp {
	__le32 tag;
	__le32 status;
	__le32 kekidx_tbls_dsop;
	__le32 dekidx;
	__le32 err_qlfr;
	u32 reserved[10];
} __attribute__((packed, aligned(4)));

struct get_phy_profile_resp {
	__le32 tag;
	__le32 status;
	__le32 ppc_phyid;
	__le32 ppc_specific_rsp[12];
} __attribute__((packed, aligned(4)));

struct flash_op_ext_resp {
	__le32 tag;
	__le32 cmd;
	__le32 status;
	__le32 epart_size;
	__le32 epart_sect_size;
	u32 reserved[10];
} __attribute__((packed, aligned(4)));

struct set_phy_profile_resp {
	__le32 tag;
	__le32 status;
	__le32 ppc_phyid;
	__le32 ppc_specific_rsp[12];
} __attribute__((packed, aligned(4)));

struct ssp_coalesced_comp_resp {
	__le32 coal_cnt;
	__le32 tag0;
	__le32 ssp_tag0;
	__le32 tag1;
	__le32 ssp_tag1;
	__le32 add_tag_ssp_tag[10];
} __attribute__((packed, aligned(4)));

/* new outbound structure for spcv - ends */

/* brief data structure for SAS protocol timer configuration page.
 *
 */
struct SASProtocolTimerConfig {
	__le32 pageCode;			/* 0 */
	__le32 MST_MSI;				/* 1 */
	__le32 STP_SSP_MCT_TMO;			/* 2 */
	__le32 STP_FRM_TMO;			/* 3 */
	__le32 STP_IDLE_TMO;			/* 4 */
	__le32 OPNRJT_RTRY_INTVL;		/* 5 */
	__le32 Data_Cmd_OPNRJT_RTRY_TMO;	/* 6 */
	__le32 Data_Cmd_OPNRJT_RTRY_THR;	/* 7 */
	__le32 MAX_AIP;				/* 8 */
} __attribute__((packed, aligned(4)));

typedef struct SASProtocolTimerConfig SASProtocolTimerConfig_t;

#define NDS_BITS 0x0F
#define PDS_BITS 0xF0

/*
 * HW Events type
 */

#define HW_EVENT_RESET_START			0x01
#define HW_EVENT_CHIP_RESET_COMPLETE		0x02
#define HW_EVENT_PHY_STOP_STATUS		0x03
#define HW_EVENT_SAS_PHY_UP			0x04
#define HW_EVENT_SATA_PHY_UP			0x05
#define HW_EVENT_SATA_SPINUP_HOLD		0x06
#define HW_EVENT_PHY_DOWN			0x07
#define HW_EVENT_PORT_INVALID			0x08
#define HW_EVENT_BROADCAST_CHANGE		0x09
#define HW_EVENT_PHY_ERROR			0x0A
#define HW_EVENT_BROADCAST_SES			0x0B
#define HW_EVENT_INBOUND_CRC_ERROR		0x0C
#define HW_EVENT_HARD_RESET_RECEIVED		0x0D
#define HW_EVENT_MALFUNCTION			0x0E
#define HW_EVENT_ID_FRAME_TIMEOUT		0x0F
#define HW_EVENT_BROADCAST_EXP			0x10
#define HW_EVENT_PHY_START_STATUS		0x11
#define HW_EVENT_LINK_ERR_INVALID_DWORD		0x12
#define HW_EVENT_LINK_ERR_DISPARITY_ERROR	0x13
#define HW_EVENT_LINK_ERR_CODE_VIOLATION	0x14
#define HW_EVENT_LINK_ERR_LOSS_OF_DWORD_SYNCH	0x15
#define HW_EVENT_LINK_ERR_PHY_RESET_FAILED	0x16
#define HW_EVENT_PORT_RECOVERY_TIMER_TMO	0x17
#define HW_EVENT_PORT_RECOVER			0x18
#define HW_EVENT_PORT_RESET_TIMER_TMO		0x19
#define HW_EVENT_PORT_RESET_COMPLETE		0x20
#define EVENT_BROADCAST_ASYNCH_EVENT		0x21

/* port state */
#define PORT_NOT_ESTABLISHED			0x00
#define PORT_VALID				0x01
#define PORT_LOSTCOMM				0x02
#define PORT_IN_RESET				0x04
#define PORT_3RD_PARTY_RESET			0x07
#define PORT_INVALID				0x08

/*
 * SSP/SMP/SATA IO Completion Status values
 */

#define IO_SUCCESS				0x00
#define IO_ABORTED				0x01
#define IO_OVERFLOW				0x02
#define IO_UNDERFLOW				0x03
#define IO_FAILED				0x04
#define IO_ABORT_RESET				0x05
#define IO_NOT_VALID				0x06
#define IO_NO_DEVICE				0x07
#define IO_ILLEGAL_PARAMETER			0x08
#define IO_LINK_FAILURE				0x09
#define IO_PROG_ERROR				0x0A

#define IO_EDC_IN_ERROR				0x0B
#define IO_EDC_OUT_ERROR			0x0C
#define IO_ERROR_HW_TIMEOUT			0x0D
#define IO_XFER_ERROR_BREAK			0x0E
#define IO_XFER_ERROR_PHY_NOT_READY		0x0F
#define IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED	0x10
#define IO_OPEN_CNX_ERROR_ZONE_VIOLATION		0x11
#define IO_OPEN_CNX_ERROR_BREAK				0x12
#define IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS			0x13
#define IO_OPEN_CNX_ERROR_BAD_DESTINATION		0x14
#define IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED	0x15
#define IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY		0x16
#define IO_OPEN_CNX_ERROR_WRONG_DESTINATION		0x17
/* This error code 0x18 is not used on SPCv */
#define IO_OPEN_CNX_ERROR_UNKNOWN_ERROR			0x18
#define IO_XFER_ERROR_NAK_RECEIVED			0x19
#define IO_XFER_ERROR_ACK_NAK_TIMEOUT			0x1A
#define IO_XFER_ERROR_PEER_ABORTED			0x1B
#define IO_XFER_ERROR_RX_FRAME				0x1C
#define IO_XFER_ERROR_DMA				0x1D
#define IO_XFER_ERROR_CREDIT_TIMEOUT			0x1E
#define IO_XFER_ERROR_SATA_LINK_TIMEOUT			0x1F
#define IO_XFER_ERROR_SATA				0x20

/* This error code 0x22 is not used on SPCv */
#define IO_XFER_ERROR_ABORTED_DUE_TO_SRST		0x22
#define IO_XFER_ERROR_REJECTED_NCQ_MODE			0x21
#define IO_XFER_ERROR_ABORTED_NCQ_MODE			0x23
#define IO_XFER_OPEN_RETRY_TIMEOUT			0x24
/* This error code 0x25 is not used on SPCv */
#define IO_XFER_SMP_RESP_CONNECTION_ERROR		0x25
#define IO_XFER_ERROR_UNEXPECTED_PHASE			0x26
#define IO_XFER_ERROR_XFER_RDY_OVERRUN			0x27
#define IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED		0x28
#define IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT		0x30

/* The following error code 0x31 and 0x32 are not using (obsolete) */
#define IO_XFER_ERROR_CMD_ISSUE_BREAK_BEFORE_ACK_NAK	0x31
#define IO_XFER_ERROR_CMD_ISSUE_PHY_DOWN_BEFORE_ACK_NAK	0x32

#define IO_XFER_ERROR_OFFSET_MISMATCH			0x34
#define IO_XFER_ERROR_XFER_ZERO_DATA_LEN		0x35
#define IO_XFER_CMD_FRAME_ISSUED			0x36
#define IO_ERROR_INTERNAL_SMP_RESOURCE			0x37
#define IO_PORT_IN_RESET				0x38
#define IO_DS_NON_OPERATIONAL				0x39
#define IO_DS_IN_RECOVERY				0x3A
#define IO_TM_TAG_NOT_FOUND				0x3B
#define IO_XFER_PIO_SETUP_ERROR				0x3C
#define IO_SSP_EXT_IU_ZERO_LEN_ERROR			0x3D
#define IO_DS_IN_ERROR					0x3E
#define IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY		0x3F
#define IO_ABORT_IN_PROGRESS				0x40
#define IO_ABORT_DELAYED				0x41
#define IO_INVALID_LENGTH				0x42

/********** additional response event values *****************/

#define IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY_ALT		0x43
#define IO_XFER_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED	0x44
#define IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO	0x45
#define IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST		0x46
#define IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE	0x47
#define IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED	0x48
#define IO_DS_INVALID					0x49
/* WARNING: the value is not contiguous from here */
#define IO_XFER_ERR_LAST_PIO_DATAIN_CRC_ERR	0x52
#define IO_XFER_DMA_ACTIVATE_TIMEOUT		0x53
#define IO_XFER_ERROR_INTERNAL_CRC_ERROR	0x54
#define MPI_IO_RQE_BUSY_FULL			0x55
#define IO_XFER_ERR_EOB_DATA_OVERRUN		0x56
#define IO_XFR_ERROR_INVALID_SSP_RSP_FRAME	0x57
#define IO_OPEN_CNX_ERROR_OPEN_PREEMPTED	0x58

#define MPI_ERR_IO_RESOURCE_UNAVAILABLE		0x1004
#define MPI_ERR_ATAPI_DEVICE_BUSY		0x1024

#define IO_XFR_ERROR_DEK_KEY_CACHE_MISS		0x2040
/*
 * An encryption IO request failed due to DEK Key Tag mismatch.
 * The key tag supplied in the encryption IOMB does not match with
 * the Key Tag in the referenced DEK Entry.
 */
#define IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH	0x2041
#define IO_XFR_ERROR_CIPHER_MODE_INVALID	0x2042
/*
 * An encryption I/O request failed because the initial value (IV)
 * in the unwrapped DEK blob didn't match the IV used to unwrap it.
 */
#define IO_XFR_ERROR_DEK_IV_MISMATCH		0x2043
/* An encryption I/O request failed due to an internal RAM ECC or
 * interface error while unwrapping the DEK. */
#define IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR	0x2044
/* An encryption I/O request failed due to an internal RAM ECC or
 * interface error while unwrapping the DEK. */
#define IO_XFR_ERROR_INTERNAL_RAM		0x2045
/*
 * An encryption I/O request failed
 * because the DEK index specified in the I/O was outside the bounds of
 * the total number of entries in the host DEK table.
 */
#define IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS0x2046

/* define DIF IO response error status code */
#define IO_XFR_ERROR_DIF_MISMATCH			0x3000
#define IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH	0x3001
#define IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH		0x3002
#define IO_XFR_ERROR_DIF_CRC_MISMATCH			0x3003

/* define operator management response status and error qualifier code */
#define OPR_MGMT_OP_NOT_SUPPORTED			0x2060
#define OPR_MGMT_MPI_ENC_ERR_OPR_PARAM_ILLEGAL		0x2061
#define OPR_MGMT_MPI_ENC_ERR_OPR_ID_NOT_FOUND		0x2062
#define OPR_MGMT_MPI_ENC_ERR_OPR_ROLE_NOT_MATCH		0x2063
#define OPR_MGMT_MPI_ENC_ERR_OPR_MAX_NUM_EXCEEDED	0x2064
#define OPR_MGMT_MPI_ENC_ERR_KEK_UNWRAP_FAIL		0x2022
#define OPR_MGMT_MPI_ENC_ERR_NVRAM_OPERATION_FAILURE	0x2023
/***************** additional response event values ***************/

/* WARNING: This error code must always be the last number.
 * If you add error code, modify this code also
 * It is used as an index
 */
#define IO_ERROR_UNKNOWN_GENERIC			0x2023

/* MSGU CONFIGURATION TABLE*/

#define SPCv_MSGU_CFG_TABLE_UPDATE		0x01
#define SPCv_MSGU_CFG_TABLE_RESET		0x02
#define SPCv_MSGU_CFG_TABLE_FREEZE		0x04
#define SPCv_MSGU_CFG_TABLE_UNFREEZE		0x08
#define MSGU_IBDB_SET				0x00
#define MSGU_HOST_INT_STATUS			0x08
#define MSGU_HOST_INT_MASK			0x0C
#define MSGU_IOPIB_INT_STATUS			0x18
#define MSGU_IOPIB_INT_MASK			0x1C
#define MSGU_IBDB_CLEAR				0x20

#define MSGU_MSGU_CONTROL			0x24
#define MSGU_ODR				0x20
#define MSGU_ODCR				0x28

#define MSGU_ODMR				0x30
#define MSGU_ODMR_U				0x34
#define MSGU_ODMR_CLR				0x38
#define MSGU_ODMR_CLR_U				0x3C
#define MSGU_OD_RSVD				0x40

#define MSGU_SCRATCH_PAD_0			0x44
#define MSGU_SCRATCH_PAD_1			0x48
#define MSGU_SCRATCH_PAD_2			0x4C
#define MSGU_SCRATCH_PAD_3			0x50
#define MSGU_HOST_SCRATCH_PAD_0			0x54
#define MSGU_HOST_SCRATCH_PAD_1			0x58
#define MSGU_HOST_SCRATCH_PAD_2			0x5C
#define MSGU_HOST_SCRATCH_PAD_3			0x60
#define MSGU_HOST_SCRATCH_PAD_4			0x64
#define MSGU_HOST_SCRATCH_PAD_5			0x68
#define MSGU_HOST_SCRATCH_PAD_6			0x6C
#define MSGU_HOST_SCRATCH_PAD_7			0x70

/* bit definition for ODMR register */
#define ODMR_MASK_ALL			0xFFFFFFFF/* mask all
					interrupt vector */
#define ODMR_CLEAR_ALL			0	/* clear all
					interrupt vector */
/* bit definition for ODCR register */
#define ODCR_CLEAR_ALL			0xFFFFFFFF /* mask all
					interrupt vector*/
/* MSIX Interupts */
#define MSIX_TABLE_OFFSET		0x2000
#define MSIX_TABLE_ELEMENT_SIZE		0x10
#define MSIX_INTERRUPT_CONTROL_OFFSET	0xC
#define MSIX_TABLE_BASE			(MSIX_TABLE_OFFSET + \
					MSIX_INTERRUPT_CONTROL_OFFSET)
#define MSIX_INTERRUPT_DISABLE		0x1
#define MSIX_INTERRUPT_ENABLE		0x0

/* state definition for Scratch Pad1 register */
#define SCRATCH_PAD_RAAE_READY		0x3
#define SCRATCH_PAD_ILA_READY		0xC
#define SCRATCH_PAD_BOOT_LOAD_SUCCESS	0x0
#define SCRATCH_PAD_IOP0_READY		0xC00
#define SCRATCH_PAD_IOP1_READY		0x3000

/* boot loader state */
#define SCRATCH_PAD1_BOOTSTATE_MASK		0x70	/* Bit 4-6 */
#define SCRATCH_PAD1_BOOTSTATE_SUCESS		0x0	/* Load successful */
#define SCRATCH_PAD1_BOOTSTATE_HDA_SEEPROM	0x10	/* HDA SEEPROM */
#define SCRATCH_PAD1_BOOTSTATE_HDA_BOOTSTRAP	0x20	/* HDA BootStrap Pins */
#define SCRATCH_PAD1_BOOTSTATE_HDA_SOFTRESET	0x30	/* HDA Soft Reset */
#define SCRATCH_PAD1_BOOTSTATE_CRIT_ERROR	0x40	/* HDA critical error */
#define SCRATCH_PAD1_BOOTSTATE_R1		0x50	/* Reserved */
#define SCRATCH_PAD1_BOOTSTATE_R2		0x60	/* Reserved */
#define SCRATCH_PAD1_BOOTSTATE_FATAL		0x70	/* Fatal Error */

 /* state definition for Scratch Pad2 register */
#define SCRATCH_PAD2_POR		0x00	/* power on state */
#define SCRATCH_PAD2_SFR		0x01	/* soft reset state */
#define SCRATCH_PAD2_ERR		0x02	/* error state */
#define SCRATCH_PAD2_RDY		0x03	/* ready state */
#define SCRATCH_PAD2_FWRDY_RST		0x04	/* FW rdy for soft reset flag */
#define SCRATCH_PAD2_IOPRDY_RST		0x08	/* IOP ready for soft reset */
#define SCRATCH_PAD2_STATE_MASK		0xFFFFFFF4 /* ScratchPad 2
 Mask, bit1-0 State */
#define SCRATCH_PAD2_RESERVED		0x000003FC/* Scratch Pad1
 Reserved bit 2 to 9 */

#define SCRATCH_PAD_ERROR_MASK		0xFFFFFC00 /* Error mask bits */
#define SCRATCH_PAD_STATE_MASK		0x00000003 /* State Mask bits */

/* main configuration offset - byte offset */
#define MAIN_SIGNATURE_OFFSET		0x00 /* DWORD 0x00 */
#define MAIN_INTERFACE_REVISION		0x04 /* DWORD 0x01 */
#define MAIN_FW_REVISION		0x08 /* DWORD 0x02 */
#define MAIN_MAX_OUTSTANDING_IO_OFFSET	0x0C /* DWORD 0x03 */
#define MAIN_MAX_SGL_OFFSET		0x10 /* DWORD 0x04 */
#define MAIN_CNTRL_CAP_OFFSET		0x14 /* DWORD 0x05 */
#define MAIN_GST_OFFSET			0x18 /* DWORD 0x06 */
#define MAIN_IBQ_OFFSET			0x1C /* DWORD 0x07 */
#define MAIN_OBQ_OFFSET			0x20 /* DWORD 0x08 */
#define MAIN_IQNPPD_HPPD_OFFSET		0x24 /* DWORD 0x09 */

/* 0x28 - 0x4C - RSVD */
#define MAIN_EVENT_CRC_CHECK		0x48 /* DWORD 0x12 */
#define MAIN_EVENT_LOG_ADDR_HI		0x50 /* DWORD 0x14 */
#define MAIN_EVENT_LOG_ADDR_LO		0x54 /* DWORD 0x15 */
#define MAIN_EVENT_LOG_BUFF_SIZE	0x58 /* DWORD 0x16 */
#define MAIN_EVENT_LOG_OPTION		0x5C /* DWORD 0x17 */
#define MAIN_PCS_EVENT_LOG_ADDR_HI	0x60 /* DWORD 0x18 */
#define MAIN_PCS_EVENT_LOG_ADDR_LO	0x64 /* DWORD 0x19 */
#define MAIN_PCS_EVENT_LOG_BUFF_SIZE	0x68 /* DWORD 0x1A */
#define MAIN_PCS_EVENT_LOG_OPTION	0x6C /* DWORD 0x1B */
#define MAIN_FATAL_ERROR_INTERRUPT	0x70 /* DWORD 0x1C */
#define MAIN_FATAL_ERROR_RDUMP0_OFFSET	0x74 /* DWORD 0x1D */
#define MAIN_FATAL_ERROR_RDUMP0_LENGTH	0x78 /* DWORD 0x1E */
#define MAIN_FATAL_ERROR_RDUMP1_OFFSET	0x7C /* DWORD 0x1F */
#define MAIN_FATAL_ERROR_RDUMP1_LENGTH	0x80 /* DWORD 0x20 */
#define MAIN_GPIO_LED_FLAGS_OFFSET	0x84 /* DWORD 0x21 */
#define MAIN_ANALOG_SETUP_OFFSET	0x88 /* DWORD 0x22 */

#define MAIN_INT_VECTOR_TABLE_OFFSET	0x8C /* DWORD 0x23 */
#define MAIN_SAS_PHY_ATTR_TABLE_OFFSET	0x90 /* DWORD 0x24 */
#define MAIN_PORT_RECOVERY_TIMER	0x94 /* DWORD 0x25 */
#define MAIN_INT_REASSERTION_DELAY	0x98 /* DWORD 0x26 */

/* Gereral Status Table offset - byte offset */
#define GST_GSTLEN_MPIS_OFFSET		0x00
#define GST_IQ_FREEZE_STATE0_OFFSET	0x04
#define GST_IQ_FREEZE_STATE1_OFFSET	0x08
#define GST_MSGUTCNT_OFFSET		0x0C
#define GST_IOPTCNT_OFFSET		0x10
/* 0x14 - 0x34 - RSVD */
#define GST_GPIO_INPUT_VAL		0x38
/* 0x3c - 0x40 - RSVD */
#define GST_RERRINFO_OFFSET0		0x44
#define GST_RERRINFO_OFFSET1		0x48
#define GST_RERRINFO_OFFSET2		0x4c
#define GST_RERRINFO_OFFSET3		0x50
#define GST_RERRINFO_OFFSET4		0x54
#define GST_RERRINFO_OFFSET5		0x58
#define GST_RERRINFO_OFFSET6		0x5c
#define GST_RERRINFO_OFFSET7		0x60

/* General Status Table - MPI state */
#define GST_MPI_STATE_UNINIT		0x00
#define GST_MPI_STATE_INIT		0x01
#define GST_MPI_STATE_TERMINATION	0x02
#define GST_MPI_STATE_ERROR		0x03
#define GST_MPI_STATE_MASK		0x07

/* Per SAS PHY Attributes */

#define PSPA_PHYSTATE0_OFFSET		0x00 /* Dword V */
#define PSPA_OB_HW_EVENT_PID0_OFFSET	0x04 /* DWORD V+1 */
#define PSPA_PHYSTATE1_OFFSET		0x08 /* Dword V+2 */
#define PSPA_OB_HW_EVENT_PID1_OFFSET	0x0C /* DWORD V+3 */
#define PSPA_PHYSTATE2_OFFSET		0x10 /* Dword V+4 */
#define PSPA_OB_HW_EVENT_PID2_OFFSET	0x14 /* DWORD V+5 */
#define PSPA_PHYSTATE3_OFFSET		0x18 /* Dword V+6 */
#define PSPA_OB_HW_EVENT_PID3_OFFSET	0x1C /* DWORD V+7 */
#define PSPA_PHYSTATE4_OFFSET		0x20 /* Dword V+8 */
#define PSPA_OB_HW_EVENT_PID4_OFFSET	0x24 /* DWORD V+9 */
#define PSPA_PHYSTATE5_OFFSET		0x28 /* Dword V+10 */
#define PSPA_OB_HW_EVENT_PID5_OFFSET	0x2C /* DWORD V+11 */
#define PSPA_PHYSTATE6_OFFSET		0x30 /* Dword V+12 */
#define PSPA_OB_HW_EVENT_PID6_OFFSET	0x34 /* DWORD V+13 */
#define PSPA_PHYSTATE7_OFFSET		0x38 /* Dword V+14 */
#define PSPA_OB_HW_EVENT_PID7_OFFSET	0x3C /* DWORD V+15 */
#define PSPA_PHYSTATE8_OFFSET		0x40 /* DWORD V+16 */
#define PSPA_OB_HW_EVENT_PID8_OFFSET	0x44 /* DWORD V+17 */
#define PSPA_PHYSTATE9_OFFSET		0x48 /* DWORD V+18 */
#define PSPA_OB_HW_EVENT_PID9_OFFSET	0x4C /* DWORD V+19 */
#define PSPA_PHYSTATE10_OFFSET		0x50 /* DWORD V+20 */
#define PSPA_OB_HW_EVENT_PID10_OFFSET	0x54 /* DWORD V+21 */
#define PSPA_PHYSTATE11_OFFSET		0x58 /* DWORD V+22 */
#define PSPA_OB_HW_EVENT_PID11_OFFSET	0x5C /* DWORD V+23 */
#define PSPA_PHYSTATE12_OFFSET		0x60 /* DWORD V+24 */
#define PSPA_OB_HW_EVENT_PID12_OFFSET	0x64 /* DWORD V+25 */
#define PSPA_PHYSTATE13_OFFSET		0x68 /* DWORD V+26 */
#define PSPA_OB_HW_EVENT_PID13_OFFSET	0x6c /* DWORD V+27 */
#define PSPA_PHYSTATE14_OFFSET		0x70 /* DWORD V+28 */
#define PSPA_OB_HW_EVENT_PID14_OFFSET	0x74 /* DWORD V+29 */
#define PSPA_PHYSTATE15_OFFSET		0x78 /* DWORD V+30 */
#define PSPA_OB_HW_EVENT_PID15_OFFSET	0x7c /* DWORD V+31 */
/* end PSPA */

/* inbound queue configuration offset - byte offset */
#define IB_PROPERITY_OFFSET		0x00
#define IB_BASE_ADDR_HI_OFFSET		0x04
#define IB_BASE_ADDR_LO_OFFSET		0x08
#define IB_CI_BASE_ADDR_HI_OFFSET	0x0C
#define IB_CI_BASE_ADDR_LO_OFFSET	0x10
#define IB_PIPCI_BAR			0x14
#define IB_PIPCI_BAR_OFFSET		0x18
#define IB_RESERVED_OFFSET		0x1C

/* outbound queue configuration offset - byte offset */
#define OB_PROPERITY_OFFSET		0x00
#define OB_BASE_ADDR_HI_OFFSET		0x04
#define OB_BASE_ADDR_LO_OFFSET		0x08
#define OB_PI_BASE_ADDR_HI_OFFSET	0x0C
#define OB_PI_BASE_ADDR_LO_OFFSET	0x10
#define OB_CIPCI_BAR			0x14
#define OB_CIPCI_BAR_OFFSET		0x18
#define OB_INTERRUPT_COALES_OFFSET	0x1C
#define OB_DYNAMIC_COALES_OFFSET	0x20
#define OB_PROPERTY_INT_ENABLE		0x40000000

#define MBIC_NMI_ENABLE_VPE0_IOP	0x000418
#define MBIC_NMI_ENABLE_VPE0_AAP1	0x000418
/* PCIE registers - BAR2(0x18), BAR1(win) 0x010000 */
#define PCIE_EVENT_INTERRUPT_ENABLE	0x003040
#define PCIE_EVENT_INTERRUPT		0x003044
#define PCIE_ERROR_INTERRUPT_ENABLE	0x003048
#define PCIE_ERROR_INTERRUPT		0x00304C

/* SPCV soft reset */
#define SPC_REG_SOFT_RESET 0x00001000
#define SPCv_NORMAL_RESET_VALUE		0x1

#define SPCv_SOFT_RESET_READ_MASK		0xC0
#define SPCv_SOFT_RESET_NO_RESET		0x0
#define SPCv_SOFT_RESET_NORMAL_RESET_OCCURED	0x40
#define SPCv_SOFT_RESET_HDA_MODE_OCCURED	0x80
#define SPCv_SOFT_RESET_CHIP_RESET_OCCURED	0xC0

/* signature definition for host scratch pad0 register */
#define SPC_SOFT_RESET_SIGNATURE	0x252acbcd
/* Signature for Soft Reset */

/* SPC Reset register - BAR4(0x20), BAR2(win) (need dynamic mapping) */
#define SPC_REG_RESET			0x000000/* reset register */

/* bit definition for SPC_RESET register */
#define SPC_REG_RESET_OSSP		0x00000001
#define SPC_REG_RESET_RAAE		0x00000002
#define SPC_REG_RESET_PCS_SPBC		0x00000004
#define SPC_REG_RESET_PCS_IOP_SS	0x00000008
#define SPC_REG_RESET_PCS_AAP1_SS	0x00000010
#define SPC_REG_RESET_PCS_AAP2_SS	0x00000020
#define SPC_REG_RESET_PCS_LM		0x00000040
#define SPC_REG_RESET_PCS		0x00000080
#define SPC_REG_RESET_GSM		0x00000100
#define SPC_REG_RESET_DDR2		0x00010000
#define SPC_REG_RESET_BDMA_CORE		0x00020000
#define SPC_REG_RESET_BDMA_SXCBI	0x00040000
#define SPC_REG_RESET_PCIE_AL_SXCBI	0x00080000
#define SPC_REG_RESET_PCIE_PWR		0x00100000
#define SPC_REG_RESET_PCIE_SFT		0x00200000
#define SPC_REG_RESET_PCS_SXCBI		0x00400000
#define SPC_REG_RESET_LMS_SXCBI		0x00800000
#define SPC_REG_RESET_PMIC_SXCBI	0x01000000
#define SPC_REG_RESET_PMIC_CORE		0x02000000
#define SPC_REG_RESET_PCIE_PC_SXCBI	0x04000000
#define SPC_REG_RESET_DEVICE		0x80000000

/* registers for BAR Shifting - BAR2(0x18), BAR1(win) */
#define SPCV_IBW_AXI_TRANSLATION_LOW	0x001010

#define MBIC_AAP1_ADDR_BASE		0x060000
#define MBIC_IOP_ADDR_BASE		0x070000
#define GSM_ADDR_BASE			0x0700000
/* Dynamic map through Bar4 - 0x00700000 */
#define GSM_CONFIG_RESET		0x00000000
#define RAM_ECC_DB_ERR			0x00000018
#define GSM_READ_ADDR_PARITY_INDIC	0x00000058
#define GSM_WRITE_ADDR_PARITY_INDIC	0x00000060
#define GSM_WRITE_DATA_PARITY_INDIC	0x00000068
#define GSM_READ_ADDR_PARITY_CHECK	0x00000038
#define GSM_WRITE_ADDR_PARITY_CHECK	0x00000040
#define GSM_WRITE_DATA_PARITY_CHECK	0x00000048

#define RB6_ACCESS_REG			0x6A0000
#define HDAC_EXEC_CMD			0x0002
#define HDA_C_PA			0xcb
#define HDA_SEQ_ID_BITS			0x00ff0000
#define HDA_GSM_OFFSET_BITS		0x00FFFFFF
#define HDA_GSM_CMD_OFFSET_BITS		0x42C0
#define HDA_GSM_RSP_OFFSET_BITS		0x42E0

#define MBIC_AAP1_ADDR_BASE		0x060000
#define MBIC_IOP_ADDR_BASE		0x070000
#define GSM_ADDR_BASE			0x0700000
#define SPC_TOP_LEVEL_ADDR_BASE		0x000000
#define GSM_CONFIG_RESET_VALUE		0x00003b00
#define GPIO_ADDR_BASE			0x00090000
#define GPIO_GPIO_0_0UTPUT_CTL_OFFSET	0x0000010c

/* RB6 offset */
#define SPC_RB6_OFFSET			0x80C0
/* Magic number of soft reset for RB6 */
#define RB6_MAGIC_NUMBER_RST		0x1234

/* Device Register status */
#define DEVREG_SUCCESS					0x00
#define DEVREG_FAILURE_OUT_OF_RESOURCE			0x01
#define DEVREG_FAILURE_DEVICE_ALREADY_REGISTERED	0x02
#define DEVREG_FAILURE_INVALID_PHY_ID			0x03
#define DEVREG_FAILURE_PHY_ID_ALREADY_REGISTERED	0x04
#define DEVREG_FAILURE_PORT_ID_OUT_OF_RANGE		0x05
#define DEVREG_FAILURE_PORT_NOT_VALID_STATE		0x06
#define DEVREG_FAILURE_DEVICE_TYPE_NOT_VALID		0x07

#endif
