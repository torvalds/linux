/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2012 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

#ifndef __CVMX_SRIOX_DEFS_H__
#define __CVMX_SRIOX_DEFS_H__

#define CVMX_SRIOX_ACC_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000148ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_ASMBLY_ID(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000200ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_ASMBLY_INFO(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000208ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_BELL_RESP_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000310ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_BIST_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000108ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_IMSG_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000508ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_IMSG_INST_HDRX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000510ull) + (((offset) & 1) + ((block_id) & 3) * 0x200000ull) * 8)
#define CVMX_SRIOX_IMSG_QOS_GRPX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000600ull) + (((offset) & 31) + ((block_id) & 3) * 0x200000ull) * 8)
#define CVMX_SRIOX_IMSG_STATUSX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000700ull) + (((offset) & 31) + ((block_id) & 3) * 0x200000ull) * 8)
#define CVMX_SRIOX_IMSG_VPORT_THR(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000500ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_IMSG_VPORT_THR2(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000528ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_INT2_ENABLE(block_id) (CVMX_ADD_IO_SEG(0x00011800C80003E0ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_INT2_REG(block_id) (CVMX_ADD_IO_SEG(0x00011800C80003E8ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_INT_ENABLE(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000110ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_INT_INFO0(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000120ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_INT_INFO1(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000128ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_INT_INFO2(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000130ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_INT_INFO3(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000138ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_INT_REG(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000118ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_IP_FEATURE(block_id) (CVMX_ADD_IO_SEG(0x00011800C80003F8ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_MAC_BUFFERS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000390ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_MAINT_OP(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000158ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_MAINT_RD_DATA(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000160ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_MCE_TX_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000240ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_MEM_OP_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000168ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_OMSG_CTRLX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000488ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#define CVMX_SRIOX_OMSG_DONE_COUNTSX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C80004B0ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#define CVMX_SRIOX_OMSG_FMP_MRX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000498ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#define CVMX_SRIOX_OMSG_NMP_MRX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C80004A0ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#define CVMX_SRIOX_OMSG_PORTX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000480ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#define CVMX_SRIOX_OMSG_SILO_THR(block_id) (CVMX_ADD_IO_SEG(0x00011800C80004F8ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_OMSG_SP_MRX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000490ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#define CVMX_SRIOX_PRIOX_IN_USE(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C80003C0ull) + (((offset) & 3) + ((block_id) & 3) * 0x200000ull) * 8)
#define CVMX_SRIOX_RX_BELL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000308ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_RX_BELL_SEQ(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000300ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_RX_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000380ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_S2M_TYPEX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000180ull) + (((offset) & 15) + ((block_id) & 3) * 0x200000ull) * 8)
#define CVMX_SRIOX_SEQ(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000278ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_STATUS_REG(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000100ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_TAG_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000178ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_TLP_CREDITS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000150ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_TX_BELL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000280ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_TX_BELL_INFO(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000288ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_TX_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000170ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_TX_EMPHASIS(block_id) (CVMX_ADD_IO_SEG(0x00011800C80003F0ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_TX_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000388ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_SRIOX_WR_DONE_COUNTS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000340ull) + ((block_id) & 3) * 0x1000000ull)

union cvmx_sriox_acc_ctrl {
	uint64_t u64;
	struct cvmx_sriox_acc_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t deny_adr2:1;
		uint64_t deny_adr1:1;
		uint64_t deny_adr0:1;
		uint64_t reserved_3_3:1;
		uint64_t deny_bar2:1;
		uint64_t deny_bar1:1;
		uint64_t deny_bar0:1;
#else
		uint64_t deny_bar0:1;
		uint64_t deny_bar1:1;
		uint64_t deny_bar2:1;
		uint64_t reserved_3_3:1;
		uint64_t deny_adr0:1;
		uint64_t deny_adr1:1;
		uint64_t deny_adr2:1;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_sriox_acc_ctrl_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t deny_bar2:1;
		uint64_t deny_bar1:1;
		uint64_t deny_bar0:1;
#else
		uint64_t deny_bar0:1;
		uint64_t deny_bar1:1;
		uint64_t deny_bar2:1;
		uint64_t reserved_3_63:61;
#endif
	} cn63xx;
	struct cvmx_sriox_acc_ctrl_cn63xx cn63xxp1;
	struct cvmx_sriox_acc_ctrl_s cn66xx;
};

union cvmx_sriox_asmbly_id {
	uint64_t u64;
	struct cvmx_sriox_asmbly_id_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t assy_id:16;
		uint64_t assy_ven:16;
#else
		uint64_t assy_ven:16;
		uint64_t assy_id:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sriox_asmbly_id_s cn63xx;
	struct cvmx_sriox_asmbly_id_s cn63xxp1;
	struct cvmx_sriox_asmbly_id_s cn66xx;
};

union cvmx_sriox_asmbly_info {
	uint64_t u64;
	struct cvmx_sriox_asmbly_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t assy_rev:16;
		uint64_t reserved_0_15:16;
#else
		uint64_t reserved_0_15:16;
		uint64_t assy_rev:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sriox_asmbly_info_s cn63xx;
	struct cvmx_sriox_asmbly_info_s cn63xxp1;
	struct cvmx_sriox_asmbly_info_s cn66xx;
};

union cvmx_sriox_bell_resp_ctrl {
	uint64_t u64;
	struct cvmx_sriox_bell_resp_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t rp1_sid:1;
		uint64_t rp0_sid:2;
		uint64_t rp1_pid:1;
		uint64_t rp0_pid:2;
#else
		uint64_t rp0_pid:2;
		uint64_t rp1_pid:1;
		uint64_t rp0_sid:2;
		uint64_t rp1_sid:1;
		uint64_t reserved_6_63:58;
#endif
	} s;
	struct cvmx_sriox_bell_resp_ctrl_s cn63xx;
	struct cvmx_sriox_bell_resp_ctrl_s cn63xxp1;
	struct cvmx_sriox_bell_resp_ctrl_s cn66xx;
};

union cvmx_sriox_bist_status {
	uint64_t u64;
	struct cvmx_sriox_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_45_63:19;
		uint64_t lram:1;
		uint64_t mram:2;
		uint64_t cram:2;
		uint64_t bell:2;
		uint64_t otag:2;
		uint64_t itag:1;
		uint64_t ofree:1;
		uint64_t rtn:2;
		uint64_t obulk:4;
		uint64_t optrs:4;
		uint64_t oarb2:2;
		uint64_t rxbuf2:2;
		uint64_t oarb:2;
		uint64_t ispf:1;
		uint64_t ospf:1;
		uint64_t txbuf:2;
		uint64_t rxbuf:2;
		uint64_t imsg:5;
		uint64_t omsg:7;
#else
		uint64_t omsg:7;
		uint64_t imsg:5;
		uint64_t rxbuf:2;
		uint64_t txbuf:2;
		uint64_t ospf:1;
		uint64_t ispf:1;
		uint64_t oarb:2;
		uint64_t rxbuf2:2;
		uint64_t oarb2:2;
		uint64_t optrs:4;
		uint64_t obulk:4;
		uint64_t rtn:2;
		uint64_t ofree:1;
		uint64_t itag:1;
		uint64_t otag:2;
		uint64_t bell:2;
		uint64_t cram:2;
		uint64_t mram:2;
		uint64_t lram:1;
		uint64_t reserved_45_63:19;
#endif
	} s;
	struct cvmx_sriox_bist_status_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t mram:2;
		uint64_t cram:2;
		uint64_t bell:2;
		uint64_t otag:2;
		uint64_t itag:1;
		uint64_t ofree:1;
		uint64_t rtn:2;
		uint64_t obulk:4;
		uint64_t optrs:4;
		uint64_t oarb2:2;
		uint64_t rxbuf2:2;
		uint64_t oarb:2;
		uint64_t ispf:1;
		uint64_t ospf:1;
		uint64_t txbuf:2;
		uint64_t rxbuf:2;
		uint64_t imsg:5;
		uint64_t omsg:7;
#else
		uint64_t omsg:7;
		uint64_t imsg:5;
		uint64_t rxbuf:2;
		uint64_t txbuf:2;
		uint64_t ospf:1;
		uint64_t ispf:1;
		uint64_t oarb:2;
		uint64_t rxbuf2:2;
		uint64_t oarb2:2;
		uint64_t optrs:4;
		uint64_t obulk:4;
		uint64_t rtn:2;
		uint64_t ofree:1;
		uint64_t itag:1;
		uint64_t otag:2;
		uint64_t bell:2;
		uint64_t cram:2;
		uint64_t mram:2;
		uint64_t reserved_44_63:20;
#endif
	} cn63xx;
	struct cvmx_sriox_bist_status_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t mram:2;
		uint64_t cram:2;
		uint64_t bell:2;
		uint64_t otag:2;
		uint64_t itag:1;
		uint64_t ofree:1;
		uint64_t rtn:2;
		uint64_t obulk:4;
		uint64_t optrs:4;
		uint64_t reserved_20_23:4;
		uint64_t oarb:2;
		uint64_t ispf:1;
		uint64_t ospf:1;
		uint64_t txbuf:2;
		uint64_t rxbuf:2;
		uint64_t imsg:5;
		uint64_t omsg:7;
#else
		uint64_t omsg:7;
		uint64_t imsg:5;
		uint64_t rxbuf:2;
		uint64_t txbuf:2;
		uint64_t ospf:1;
		uint64_t ispf:1;
		uint64_t oarb:2;
		uint64_t reserved_20_23:4;
		uint64_t optrs:4;
		uint64_t obulk:4;
		uint64_t rtn:2;
		uint64_t ofree:1;
		uint64_t itag:1;
		uint64_t otag:2;
		uint64_t bell:2;
		uint64_t cram:2;
		uint64_t mram:2;
		uint64_t reserved_44_63:20;
#endif
	} cn63xxp1;
	struct cvmx_sriox_bist_status_s cn66xx;
};

union cvmx_sriox_imsg_ctrl {
	uint64_t u64;
	struct cvmx_sriox_imsg_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t to_mode:1;
		uint64_t reserved_30_30:1;
		uint64_t rsp_thr:6;
		uint64_t reserved_22_23:2;
		uint64_t rp1_sid:1;
		uint64_t rp0_sid:2;
		uint64_t rp1_pid:1;
		uint64_t rp0_pid:2;
		uint64_t reserved_15_15:1;
		uint64_t prt_sel:3;
		uint64_t lttr:4;
		uint64_t prio:4;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t prio:4;
		uint64_t lttr:4;
		uint64_t prt_sel:3;
		uint64_t reserved_15_15:1;
		uint64_t rp0_pid:2;
		uint64_t rp1_pid:1;
		uint64_t rp0_sid:2;
		uint64_t rp1_sid:1;
		uint64_t reserved_22_23:2;
		uint64_t rsp_thr:6;
		uint64_t reserved_30_30:1;
		uint64_t to_mode:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sriox_imsg_ctrl_s cn63xx;
	struct cvmx_sriox_imsg_ctrl_s cn63xxp1;
	struct cvmx_sriox_imsg_ctrl_s cn66xx;
};

union cvmx_sriox_imsg_inst_hdrx {
	uint64_t u64;
	struct cvmx_sriox_imsg_inst_hdrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t r:1;
		uint64_t reserved_58_62:5;
		uint64_t pm:2;
		uint64_t reserved_55_55:1;
		uint64_t sl:7;
		uint64_t reserved_46_47:2;
		uint64_t nqos:1;
		uint64_t ngrp:1;
		uint64_t ntt:1;
		uint64_t ntag:1;
		uint64_t reserved_35_41:7;
		uint64_t rs:1;
		uint64_t tt:2;
		uint64_t tag:32;
#else
		uint64_t tag:32;
		uint64_t tt:2;
		uint64_t rs:1;
		uint64_t reserved_35_41:7;
		uint64_t ntag:1;
		uint64_t ntt:1;
		uint64_t ngrp:1;
		uint64_t nqos:1;
		uint64_t reserved_46_47:2;
		uint64_t sl:7;
		uint64_t reserved_55_55:1;
		uint64_t pm:2;
		uint64_t reserved_58_62:5;
		uint64_t r:1;
#endif
	} s;
	struct cvmx_sriox_imsg_inst_hdrx_s cn63xx;
	struct cvmx_sriox_imsg_inst_hdrx_s cn63xxp1;
	struct cvmx_sriox_imsg_inst_hdrx_s cn66xx;
};

union cvmx_sriox_imsg_qos_grpx {
	uint64_t u64;
	struct cvmx_sriox_imsg_qos_grpx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_63_63:1;
		uint64_t qos7:3;
		uint64_t grp7:4;
		uint64_t reserved_55_55:1;
		uint64_t qos6:3;
		uint64_t grp6:4;
		uint64_t reserved_47_47:1;
		uint64_t qos5:3;
		uint64_t grp5:4;
		uint64_t reserved_39_39:1;
		uint64_t qos4:3;
		uint64_t grp4:4;
		uint64_t reserved_31_31:1;
		uint64_t qos3:3;
		uint64_t grp3:4;
		uint64_t reserved_23_23:1;
		uint64_t qos2:3;
		uint64_t grp2:4;
		uint64_t reserved_15_15:1;
		uint64_t qos1:3;
		uint64_t grp1:4;
		uint64_t reserved_7_7:1;
		uint64_t qos0:3;
		uint64_t grp0:4;
#else
		uint64_t grp0:4;
		uint64_t qos0:3;
		uint64_t reserved_7_7:1;
		uint64_t grp1:4;
		uint64_t qos1:3;
		uint64_t reserved_15_15:1;
		uint64_t grp2:4;
		uint64_t qos2:3;
		uint64_t reserved_23_23:1;
		uint64_t grp3:4;
		uint64_t qos3:3;
		uint64_t reserved_31_31:1;
		uint64_t grp4:4;
		uint64_t qos4:3;
		uint64_t reserved_39_39:1;
		uint64_t grp5:4;
		uint64_t qos5:3;
		uint64_t reserved_47_47:1;
		uint64_t grp6:4;
		uint64_t qos6:3;
		uint64_t reserved_55_55:1;
		uint64_t grp7:4;
		uint64_t qos7:3;
		uint64_t reserved_63_63:1;
#endif
	} s;
	struct cvmx_sriox_imsg_qos_grpx_s cn63xx;
	struct cvmx_sriox_imsg_qos_grpx_s cn63xxp1;
	struct cvmx_sriox_imsg_qos_grpx_s cn66xx;
};

union cvmx_sriox_imsg_statusx {
	uint64_t u64;
	struct cvmx_sriox_imsg_statusx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t val1:1;
		uint64_t err1:1;
		uint64_t toe1:1;
		uint64_t toc1:1;
		uint64_t prt1:1;
		uint64_t reserved_58_58:1;
		uint64_t tt1:1;
		uint64_t dis1:1;
		uint64_t seg1:4;
		uint64_t mbox1:2;
		uint64_t lttr1:2;
		uint64_t sid1:16;
		uint64_t val0:1;
		uint64_t err0:1;
		uint64_t toe0:1;
		uint64_t toc0:1;
		uint64_t prt0:1;
		uint64_t reserved_26_26:1;
		uint64_t tt0:1;
		uint64_t dis0:1;
		uint64_t seg0:4;
		uint64_t mbox0:2;
		uint64_t lttr0:2;
		uint64_t sid0:16;
#else
		uint64_t sid0:16;
		uint64_t lttr0:2;
		uint64_t mbox0:2;
		uint64_t seg0:4;
		uint64_t dis0:1;
		uint64_t tt0:1;
		uint64_t reserved_26_26:1;
		uint64_t prt0:1;
		uint64_t toc0:1;
		uint64_t toe0:1;
		uint64_t err0:1;
		uint64_t val0:1;
		uint64_t sid1:16;
		uint64_t lttr1:2;
		uint64_t mbox1:2;
		uint64_t seg1:4;
		uint64_t dis1:1;
		uint64_t tt1:1;
		uint64_t reserved_58_58:1;
		uint64_t prt1:1;
		uint64_t toc1:1;
		uint64_t toe1:1;
		uint64_t err1:1;
		uint64_t val1:1;
#endif
	} s;
	struct cvmx_sriox_imsg_statusx_s cn63xx;
	struct cvmx_sriox_imsg_statusx_s cn63xxp1;
	struct cvmx_sriox_imsg_statusx_s cn66xx;
};

union cvmx_sriox_imsg_vport_thr {
	uint64_t u64;
	struct cvmx_sriox_imsg_vport_thr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t max_tot:6;
		uint64_t reserved_46_47:2;
		uint64_t max_s1:6;
		uint64_t reserved_38_39:2;
		uint64_t max_s0:6;
		uint64_t sp_vport:1;
		uint64_t reserved_20_30:11;
		uint64_t buf_thr:4;
		uint64_t reserved_14_15:2;
		uint64_t max_p1:6;
		uint64_t reserved_6_7:2;
		uint64_t max_p0:6;
#else
		uint64_t max_p0:6;
		uint64_t reserved_6_7:2;
		uint64_t max_p1:6;
		uint64_t reserved_14_15:2;
		uint64_t buf_thr:4;
		uint64_t reserved_20_30:11;
		uint64_t sp_vport:1;
		uint64_t max_s0:6;
		uint64_t reserved_38_39:2;
		uint64_t max_s1:6;
		uint64_t reserved_46_47:2;
		uint64_t max_tot:6;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_sriox_imsg_vport_thr_s cn63xx;
	struct cvmx_sriox_imsg_vport_thr_s cn63xxp1;
	struct cvmx_sriox_imsg_vport_thr_s cn66xx;
};

union cvmx_sriox_imsg_vport_thr2 {
	uint64_t u64;
	struct cvmx_sriox_imsg_vport_thr2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_46_63:18;
		uint64_t max_s3:6;
		uint64_t reserved_38_39:2;
		uint64_t max_s2:6;
		uint64_t reserved_0_31:32;
#else
		uint64_t reserved_0_31:32;
		uint64_t max_s2:6;
		uint64_t reserved_38_39:2;
		uint64_t max_s3:6;
		uint64_t reserved_46_63:18;
#endif
	} s;
	struct cvmx_sriox_imsg_vport_thr2_s cn66xx;
};

union cvmx_sriox_int2_enable {
	uint64_t u64;
	struct cvmx_sriox_int2_enable_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t pko_rst:1;
#else
		uint64_t pko_rst:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_sriox_int2_enable_s cn63xx;
	struct cvmx_sriox_int2_enable_s cn66xx;
};

union cvmx_sriox_int2_reg {
	uint64_t u64;
	struct cvmx_sriox_int2_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t int_sum:1;
		uint64_t reserved_1_30:30;
		uint64_t pko_rst:1;
#else
		uint64_t pko_rst:1;
		uint64_t reserved_1_30:30;
		uint64_t int_sum:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sriox_int2_reg_s cn63xx;
	struct cvmx_sriox_int2_reg_s cn66xx;
};

union cvmx_sriox_int_enable {
	uint64_t u64;
	struct cvmx_sriox_int_enable_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_27_63:37;
		uint64_t zero_pkt:1;
		uint64_t ttl_tout:1;
		uint64_t fail:1;
		uint64_t degrade:1;
		uint64_t mac_buf:1;
		uint64_t f_error:1;
		uint64_t rtry_err:1;
		uint64_t pko_err:1;
		uint64_t omsg_err:1;
		uint64_t omsg1:1;
		uint64_t omsg0:1;
		uint64_t link_up:1;
		uint64_t link_dwn:1;
		uint64_t phy_erb:1;
		uint64_t log_erb:1;
		uint64_t soft_rx:1;
		uint64_t soft_tx:1;
		uint64_t mce_rx:1;
		uint64_t mce_tx:1;
		uint64_t wr_done:1;
		uint64_t sli_err:1;
		uint64_t deny_wr:1;
		uint64_t bar_err:1;
		uint64_t maint_op:1;
		uint64_t rxbell:1;
		uint64_t bell_err:1;
		uint64_t txbell:1;
#else
		uint64_t txbell:1;
		uint64_t bell_err:1;
		uint64_t rxbell:1;
		uint64_t maint_op:1;
		uint64_t bar_err:1;
		uint64_t deny_wr:1;
		uint64_t sli_err:1;
		uint64_t wr_done:1;
		uint64_t mce_tx:1;
		uint64_t mce_rx:1;
		uint64_t soft_tx:1;
		uint64_t soft_rx:1;
		uint64_t log_erb:1;
		uint64_t phy_erb:1;
		uint64_t link_dwn:1;
		uint64_t link_up:1;
		uint64_t omsg0:1;
		uint64_t omsg1:1;
		uint64_t omsg_err:1;
		uint64_t pko_err:1;
		uint64_t rtry_err:1;
		uint64_t f_error:1;
		uint64_t mac_buf:1;
		uint64_t degrade:1;
		uint64_t fail:1;
		uint64_t ttl_tout:1;
		uint64_t zero_pkt:1;
		uint64_t reserved_27_63:37;
#endif
	} s;
	struct cvmx_sriox_int_enable_s cn63xx;
	struct cvmx_sriox_int_enable_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t f_error:1;
		uint64_t rtry_err:1;
		uint64_t pko_err:1;
		uint64_t omsg_err:1;
		uint64_t omsg1:1;
		uint64_t omsg0:1;
		uint64_t link_up:1;
		uint64_t link_dwn:1;
		uint64_t phy_erb:1;
		uint64_t log_erb:1;
		uint64_t soft_rx:1;
		uint64_t soft_tx:1;
		uint64_t mce_rx:1;
		uint64_t mce_tx:1;
		uint64_t wr_done:1;
		uint64_t sli_err:1;
		uint64_t deny_wr:1;
		uint64_t bar_err:1;
		uint64_t maint_op:1;
		uint64_t rxbell:1;
		uint64_t bell_err:1;
		uint64_t txbell:1;
#else
		uint64_t txbell:1;
		uint64_t bell_err:1;
		uint64_t rxbell:1;
		uint64_t maint_op:1;
		uint64_t bar_err:1;
		uint64_t deny_wr:1;
		uint64_t sli_err:1;
		uint64_t wr_done:1;
		uint64_t mce_tx:1;
		uint64_t mce_rx:1;
		uint64_t soft_tx:1;
		uint64_t soft_rx:1;
		uint64_t log_erb:1;
		uint64_t phy_erb:1;
		uint64_t link_dwn:1;
		uint64_t link_up:1;
		uint64_t omsg0:1;
		uint64_t omsg1:1;
		uint64_t omsg_err:1;
		uint64_t pko_err:1;
		uint64_t rtry_err:1;
		uint64_t f_error:1;
		uint64_t reserved_22_63:42;
#endif
	} cn63xxp1;
	struct cvmx_sriox_int_enable_s cn66xx;
};

union cvmx_sriox_int_info0 {
	uint64_t u64;
	struct cvmx_sriox_int_info0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t cmd:4;
		uint64_t type:4;
		uint64_t tag:8;
		uint64_t reserved_42_47:6;
		uint64_t length:10;
		uint64_t status:3;
		uint64_t reserved_16_28:13;
		uint64_t be0:8;
		uint64_t be1:8;
#else
		uint64_t be1:8;
		uint64_t be0:8;
		uint64_t reserved_16_28:13;
		uint64_t status:3;
		uint64_t length:10;
		uint64_t reserved_42_47:6;
		uint64_t tag:8;
		uint64_t type:4;
		uint64_t cmd:4;
#endif
	} s;
	struct cvmx_sriox_int_info0_s cn63xx;
	struct cvmx_sriox_int_info0_s cn63xxp1;
	struct cvmx_sriox_int_info0_s cn66xx;
};

union cvmx_sriox_int_info1 {
	uint64_t u64;
	struct cvmx_sriox_int_info1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t info1:64;
#else
		uint64_t info1:64;
#endif
	} s;
	struct cvmx_sriox_int_info1_s cn63xx;
	struct cvmx_sriox_int_info1_s cn63xxp1;
	struct cvmx_sriox_int_info1_s cn66xx;
};

union cvmx_sriox_int_info2 {
	uint64_t u64;
	struct cvmx_sriox_int_info2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t prio:2;
		uint64_t tt:1;
		uint64_t sis:1;
		uint64_t ssize:4;
		uint64_t did:16;
		uint64_t xmbox:4;
		uint64_t mbox:2;
		uint64_t letter:2;
		uint64_t rsrvd:30;
		uint64_t lns:1;
		uint64_t intr:1;
#else
		uint64_t intr:1;
		uint64_t lns:1;
		uint64_t rsrvd:30;
		uint64_t letter:2;
		uint64_t mbox:2;
		uint64_t xmbox:4;
		uint64_t did:16;
		uint64_t ssize:4;
		uint64_t sis:1;
		uint64_t tt:1;
		uint64_t prio:2;
#endif
	} s;
	struct cvmx_sriox_int_info2_s cn63xx;
	struct cvmx_sriox_int_info2_s cn63xxp1;
	struct cvmx_sriox_int_info2_s cn66xx;
};

union cvmx_sriox_int_info3 {
	uint64_t u64;
	struct cvmx_sriox_int_info3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t prio:2;
		uint64_t tt:2;
		uint64_t type:4;
		uint64_t other:48;
		uint64_t reserved_0_7:8;
#else
		uint64_t reserved_0_7:8;
		uint64_t other:48;
		uint64_t type:4;
		uint64_t tt:2;
		uint64_t prio:2;
#endif
	} s;
	struct cvmx_sriox_int_info3_s cn63xx;
	struct cvmx_sriox_int_info3_s cn63xxp1;
	struct cvmx_sriox_int_info3_s cn66xx;
};

union cvmx_sriox_int_reg {
	uint64_t u64;
	struct cvmx_sriox_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t int2_sum:1;
		uint64_t reserved_27_30:4;
		uint64_t zero_pkt:1;
		uint64_t ttl_tout:1;
		uint64_t fail:1;
		uint64_t degrad:1;
		uint64_t mac_buf:1;
		uint64_t f_error:1;
		uint64_t rtry_err:1;
		uint64_t pko_err:1;
		uint64_t omsg_err:1;
		uint64_t omsg1:1;
		uint64_t omsg0:1;
		uint64_t link_up:1;
		uint64_t link_dwn:1;
		uint64_t phy_erb:1;
		uint64_t log_erb:1;
		uint64_t soft_rx:1;
		uint64_t soft_tx:1;
		uint64_t mce_rx:1;
		uint64_t mce_tx:1;
		uint64_t wr_done:1;
		uint64_t sli_err:1;
		uint64_t deny_wr:1;
		uint64_t bar_err:1;
		uint64_t maint_op:1;
		uint64_t rxbell:1;
		uint64_t bell_err:1;
		uint64_t txbell:1;
#else
		uint64_t txbell:1;
		uint64_t bell_err:1;
		uint64_t rxbell:1;
		uint64_t maint_op:1;
		uint64_t bar_err:1;
		uint64_t deny_wr:1;
		uint64_t sli_err:1;
		uint64_t wr_done:1;
		uint64_t mce_tx:1;
		uint64_t mce_rx:1;
		uint64_t soft_tx:1;
		uint64_t soft_rx:1;
		uint64_t log_erb:1;
		uint64_t phy_erb:1;
		uint64_t link_dwn:1;
		uint64_t link_up:1;
		uint64_t omsg0:1;
		uint64_t omsg1:1;
		uint64_t omsg_err:1;
		uint64_t pko_err:1;
		uint64_t rtry_err:1;
		uint64_t f_error:1;
		uint64_t mac_buf:1;
		uint64_t degrad:1;
		uint64_t fail:1;
		uint64_t ttl_tout:1;
		uint64_t zero_pkt:1;
		uint64_t reserved_27_30:4;
		uint64_t int2_sum:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sriox_int_reg_s cn63xx;
	struct cvmx_sriox_int_reg_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t f_error:1;
		uint64_t rtry_err:1;
		uint64_t pko_err:1;
		uint64_t omsg_err:1;
		uint64_t omsg1:1;
		uint64_t omsg0:1;
		uint64_t link_up:1;
		uint64_t link_dwn:1;
		uint64_t phy_erb:1;
		uint64_t log_erb:1;
		uint64_t soft_rx:1;
		uint64_t soft_tx:1;
		uint64_t mce_rx:1;
		uint64_t mce_tx:1;
		uint64_t wr_done:1;
		uint64_t sli_err:1;
		uint64_t deny_wr:1;
		uint64_t bar_err:1;
		uint64_t maint_op:1;
		uint64_t rxbell:1;
		uint64_t bell_err:1;
		uint64_t txbell:1;
#else
		uint64_t txbell:1;
		uint64_t bell_err:1;
		uint64_t rxbell:1;
		uint64_t maint_op:1;
		uint64_t bar_err:1;
		uint64_t deny_wr:1;
		uint64_t sli_err:1;
		uint64_t wr_done:1;
		uint64_t mce_tx:1;
		uint64_t mce_rx:1;
		uint64_t soft_tx:1;
		uint64_t soft_rx:1;
		uint64_t log_erb:1;
		uint64_t phy_erb:1;
		uint64_t link_dwn:1;
		uint64_t link_up:1;
		uint64_t omsg0:1;
		uint64_t omsg1:1;
		uint64_t omsg_err:1;
		uint64_t pko_err:1;
		uint64_t rtry_err:1;
		uint64_t f_error:1;
		uint64_t reserved_22_63:42;
#endif
	} cn63xxp1;
	struct cvmx_sriox_int_reg_s cn66xx;
};

union cvmx_sriox_ip_feature {
	uint64_t u64;
	struct cvmx_sriox_ip_feature_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t ops:32;
		uint64_t reserved_15_31:17;
		uint64_t no_vmin:1;
		uint64_t a66:1;
		uint64_t a50:1;
		uint64_t reserved_11_11:1;
		uint64_t tx_flow:1;
		uint64_t pt_width:2;
		uint64_t tx_pol:4;
		uint64_t rx_pol:4;
#else
		uint64_t rx_pol:4;
		uint64_t tx_pol:4;
		uint64_t pt_width:2;
		uint64_t tx_flow:1;
		uint64_t reserved_11_11:1;
		uint64_t a50:1;
		uint64_t a66:1;
		uint64_t no_vmin:1;
		uint64_t reserved_15_31:17;
		uint64_t ops:32;
#endif
	} s;
	struct cvmx_sriox_ip_feature_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t ops:32;
		uint64_t reserved_14_31:18;
		uint64_t a66:1;
		uint64_t a50:1;
		uint64_t reserved_11_11:1;
		uint64_t tx_flow:1;
		uint64_t pt_width:2;
		uint64_t tx_pol:4;
		uint64_t rx_pol:4;
#else
		uint64_t rx_pol:4;
		uint64_t tx_pol:4;
		uint64_t pt_width:2;
		uint64_t tx_flow:1;
		uint64_t reserved_11_11:1;
		uint64_t a50:1;
		uint64_t a66:1;
		uint64_t reserved_14_31:18;
		uint64_t ops:32;
#endif
	} cn63xx;
	struct cvmx_sriox_ip_feature_cn63xx cn63xxp1;
	struct cvmx_sriox_ip_feature_s cn66xx;
};

union cvmx_sriox_mac_buffers {
	uint64_t u64;
	struct cvmx_sriox_mac_buffers_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t tx_enb:8;
		uint64_t reserved_44_47:4;
		uint64_t tx_inuse:4;
		uint64_t tx_stat:8;
		uint64_t reserved_24_31:8;
		uint64_t rx_enb:8;
		uint64_t reserved_12_15:4;
		uint64_t rx_inuse:4;
		uint64_t rx_stat:8;
#else
		uint64_t rx_stat:8;
		uint64_t rx_inuse:4;
		uint64_t reserved_12_15:4;
		uint64_t rx_enb:8;
		uint64_t reserved_24_31:8;
		uint64_t tx_stat:8;
		uint64_t tx_inuse:4;
		uint64_t reserved_44_47:4;
		uint64_t tx_enb:8;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_sriox_mac_buffers_s cn63xx;
	struct cvmx_sriox_mac_buffers_s cn66xx;
};

union cvmx_sriox_maint_op {
	uint64_t u64;
	struct cvmx_sriox_maint_op_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t wr_data:32;
		uint64_t reserved_27_31:5;
		uint64_t fail:1;
		uint64_t pending:1;
		uint64_t op:1;
		uint64_t addr:24;
#else
		uint64_t addr:24;
		uint64_t op:1;
		uint64_t pending:1;
		uint64_t fail:1;
		uint64_t reserved_27_31:5;
		uint64_t wr_data:32;
#endif
	} s;
	struct cvmx_sriox_maint_op_s cn63xx;
	struct cvmx_sriox_maint_op_s cn63xxp1;
	struct cvmx_sriox_maint_op_s cn66xx;
};

union cvmx_sriox_maint_rd_data {
	uint64_t u64;
	struct cvmx_sriox_maint_rd_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t valid:1;
		uint64_t rd_data:32;
#else
		uint64_t rd_data:32;
		uint64_t valid:1;
		uint64_t reserved_33_63:31;
#endif
	} s;
	struct cvmx_sriox_maint_rd_data_s cn63xx;
	struct cvmx_sriox_maint_rd_data_s cn63xxp1;
	struct cvmx_sriox_maint_rd_data_s cn66xx;
};

union cvmx_sriox_mce_tx_ctl {
	uint64_t u64;
	struct cvmx_sriox_mce_tx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t mce:1;
#else
		uint64_t mce:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_sriox_mce_tx_ctl_s cn63xx;
	struct cvmx_sriox_mce_tx_ctl_s cn63xxp1;
	struct cvmx_sriox_mce_tx_ctl_s cn66xx;
};

union cvmx_sriox_mem_op_ctrl {
	uint64_t u64;
	struct cvmx_sriox_mem_op_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t rr_ro:1;
		uint64_t w_ro:1;
		uint64_t reserved_6_7:2;
		uint64_t rp1_sid:1;
		uint64_t rp0_sid:2;
		uint64_t rp1_pid:1;
		uint64_t rp0_pid:2;
#else
		uint64_t rp0_pid:2;
		uint64_t rp1_pid:1;
		uint64_t rp0_sid:2;
		uint64_t rp1_sid:1;
		uint64_t reserved_6_7:2;
		uint64_t w_ro:1;
		uint64_t rr_ro:1;
		uint64_t reserved_10_63:54;
#endif
	} s;
	struct cvmx_sriox_mem_op_ctrl_s cn63xx;
	struct cvmx_sriox_mem_op_ctrl_s cn63xxp1;
	struct cvmx_sriox_mem_op_ctrl_s cn66xx;
};

union cvmx_sriox_omsg_ctrlx {
	uint64_t u64;
	struct cvmx_sriox_omsg_ctrlx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t testmode:1;
		uint64_t reserved_37_62:26;
		uint64_t silo_max:5;
		uint64_t rtry_thr:16;
		uint64_t rtry_en:1;
		uint64_t reserved_11_14:4;
		uint64_t idm_tt:1;
		uint64_t idm_sis:1;
		uint64_t idm_did:1;
		uint64_t lttr_sp:4;
		uint64_t lttr_mp:4;
#else
		uint64_t lttr_mp:4;
		uint64_t lttr_sp:4;
		uint64_t idm_did:1;
		uint64_t idm_sis:1;
		uint64_t idm_tt:1;
		uint64_t reserved_11_14:4;
		uint64_t rtry_en:1;
		uint64_t rtry_thr:16;
		uint64_t silo_max:5;
		uint64_t reserved_37_62:26;
		uint64_t testmode:1;
#endif
	} s;
	struct cvmx_sriox_omsg_ctrlx_s cn63xx;
	struct cvmx_sriox_omsg_ctrlx_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t testmode:1;
		uint64_t reserved_32_62:31;
		uint64_t rtry_thr:16;
		uint64_t rtry_en:1;
		uint64_t reserved_11_14:4;
		uint64_t idm_tt:1;
		uint64_t idm_sis:1;
		uint64_t idm_did:1;
		uint64_t lttr_sp:4;
		uint64_t lttr_mp:4;
#else
		uint64_t lttr_mp:4;
		uint64_t lttr_sp:4;
		uint64_t idm_did:1;
		uint64_t idm_sis:1;
		uint64_t idm_tt:1;
		uint64_t reserved_11_14:4;
		uint64_t rtry_en:1;
		uint64_t rtry_thr:16;
		uint64_t reserved_32_62:31;
		uint64_t testmode:1;
#endif
	} cn63xxp1;
	struct cvmx_sriox_omsg_ctrlx_s cn66xx;
};

union cvmx_sriox_omsg_done_countsx {
	uint64_t u64;
	struct cvmx_sriox_omsg_done_countsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t bad:16;
		uint64_t good:16;
#else
		uint64_t good:16;
		uint64_t bad:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sriox_omsg_done_countsx_s cn63xx;
	struct cvmx_sriox_omsg_done_countsx_s cn66xx;
};

union cvmx_sriox_omsg_fmp_mrx {
	uint64_t u64;
	struct cvmx_sriox_omsg_fmp_mrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t ctlr_sp:1;
		uint64_t ctlr_fmp:1;
		uint64_t ctlr_nmp:1;
		uint64_t id_sp:1;
		uint64_t id_fmp:1;
		uint64_t id_nmp:1;
		uint64_t id_psd:1;
		uint64_t mbox_sp:1;
		uint64_t mbox_fmp:1;
		uint64_t mbox_nmp:1;
		uint64_t mbox_psd:1;
		uint64_t all_sp:1;
		uint64_t all_fmp:1;
		uint64_t all_nmp:1;
		uint64_t all_psd:1;
#else
		uint64_t all_psd:1;
		uint64_t all_nmp:1;
		uint64_t all_fmp:1;
		uint64_t all_sp:1;
		uint64_t mbox_psd:1;
		uint64_t mbox_nmp:1;
		uint64_t mbox_fmp:1;
		uint64_t mbox_sp:1;
		uint64_t id_psd:1;
		uint64_t id_nmp:1;
		uint64_t id_fmp:1;
		uint64_t id_sp:1;
		uint64_t ctlr_nmp:1;
		uint64_t ctlr_fmp:1;
		uint64_t ctlr_sp:1;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_sriox_omsg_fmp_mrx_s cn63xx;
	struct cvmx_sriox_omsg_fmp_mrx_s cn63xxp1;
	struct cvmx_sriox_omsg_fmp_mrx_s cn66xx;
};

union cvmx_sriox_omsg_nmp_mrx {
	uint64_t u64;
	struct cvmx_sriox_omsg_nmp_mrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t ctlr_sp:1;
		uint64_t ctlr_fmp:1;
		uint64_t ctlr_nmp:1;
		uint64_t id_sp:1;
		uint64_t id_fmp:1;
		uint64_t id_nmp:1;
		uint64_t reserved_8_8:1;
		uint64_t mbox_sp:1;
		uint64_t mbox_fmp:1;
		uint64_t mbox_nmp:1;
		uint64_t reserved_4_4:1;
		uint64_t all_sp:1;
		uint64_t all_fmp:1;
		uint64_t all_nmp:1;
		uint64_t reserved_0_0:1;
#else
		uint64_t reserved_0_0:1;
		uint64_t all_nmp:1;
		uint64_t all_fmp:1;
		uint64_t all_sp:1;
		uint64_t reserved_4_4:1;
		uint64_t mbox_nmp:1;
		uint64_t mbox_fmp:1;
		uint64_t mbox_sp:1;
		uint64_t reserved_8_8:1;
		uint64_t id_nmp:1;
		uint64_t id_fmp:1;
		uint64_t id_sp:1;
		uint64_t ctlr_nmp:1;
		uint64_t ctlr_fmp:1;
		uint64_t ctlr_sp:1;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_sriox_omsg_nmp_mrx_s cn63xx;
	struct cvmx_sriox_omsg_nmp_mrx_s cn63xxp1;
	struct cvmx_sriox_omsg_nmp_mrx_s cn66xx;
};

union cvmx_sriox_omsg_portx {
	uint64_t u64;
	struct cvmx_sriox_omsg_portx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t enable:1;
		uint64_t reserved_3_30:28;
		uint64_t port:3;
#else
		uint64_t port:3;
		uint64_t reserved_3_30:28;
		uint64_t enable:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sriox_omsg_portx_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t enable:1;
		uint64_t reserved_2_30:29;
		uint64_t port:2;
#else
		uint64_t port:2;
		uint64_t reserved_2_30:29;
		uint64_t enable:1;
		uint64_t reserved_32_63:32;
#endif
	} cn63xx;
	struct cvmx_sriox_omsg_portx_cn63xx cn63xxp1;
	struct cvmx_sriox_omsg_portx_s cn66xx;
};

union cvmx_sriox_omsg_silo_thr {
	uint64_t u64;
	struct cvmx_sriox_omsg_silo_thr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t tot_silo:5;
#else
		uint64_t tot_silo:5;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_sriox_omsg_silo_thr_s cn63xx;
	struct cvmx_sriox_omsg_silo_thr_s cn66xx;
};

union cvmx_sriox_omsg_sp_mrx {
	uint64_t u64;
	struct cvmx_sriox_omsg_sp_mrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t xmbox_sp:1;
		uint64_t ctlr_sp:1;
		uint64_t ctlr_fmp:1;
		uint64_t ctlr_nmp:1;
		uint64_t id_sp:1;
		uint64_t id_fmp:1;
		uint64_t id_nmp:1;
		uint64_t id_psd:1;
		uint64_t mbox_sp:1;
		uint64_t mbox_fmp:1;
		uint64_t mbox_nmp:1;
		uint64_t mbox_psd:1;
		uint64_t all_sp:1;
		uint64_t all_fmp:1;
		uint64_t all_nmp:1;
		uint64_t all_psd:1;
#else
		uint64_t all_psd:1;
		uint64_t all_nmp:1;
		uint64_t all_fmp:1;
		uint64_t all_sp:1;
		uint64_t mbox_psd:1;
		uint64_t mbox_nmp:1;
		uint64_t mbox_fmp:1;
		uint64_t mbox_sp:1;
		uint64_t id_psd:1;
		uint64_t id_nmp:1;
		uint64_t id_fmp:1;
		uint64_t id_sp:1;
		uint64_t ctlr_nmp:1;
		uint64_t ctlr_fmp:1;
		uint64_t ctlr_sp:1;
		uint64_t xmbox_sp:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_sriox_omsg_sp_mrx_s cn63xx;
	struct cvmx_sriox_omsg_sp_mrx_s cn63xxp1;
	struct cvmx_sriox_omsg_sp_mrx_s cn66xx;
};

union cvmx_sriox_priox_in_use {
	uint64_t u64;
	struct cvmx_sriox_priox_in_use_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t end_cnt:16;
		uint64_t start_cnt:16;
#else
		uint64_t start_cnt:16;
		uint64_t end_cnt:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sriox_priox_in_use_s cn63xx;
	struct cvmx_sriox_priox_in_use_s cn66xx;
};

union cvmx_sriox_rx_bell {
	uint64_t u64;
	struct cvmx_sriox_rx_bell_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t data:16;
		uint64_t src_id:16;
		uint64_t count:8;
		uint64_t reserved_5_7:3;
		uint64_t dest_id:1;
		uint64_t id16:1;
		uint64_t reserved_2_2:1;
		uint64_t priority:2;
#else
		uint64_t priority:2;
		uint64_t reserved_2_2:1;
		uint64_t id16:1;
		uint64_t dest_id:1;
		uint64_t reserved_5_7:3;
		uint64_t count:8;
		uint64_t src_id:16;
		uint64_t data:16;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_sriox_rx_bell_s cn63xx;
	struct cvmx_sriox_rx_bell_s cn63xxp1;
	struct cvmx_sriox_rx_bell_s cn66xx;
};

union cvmx_sriox_rx_bell_seq {
	uint64_t u64;
	struct cvmx_sriox_rx_bell_seq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_40_63:24;
		uint64_t count:8;
		uint64_t seq:32;
#else
		uint64_t seq:32;
		uint64_t count:8;
		uint64_t reserved_40_63:24;
#endif
	} s;
	struct cvmx_sriox_rx_bell_seq_s cn63xx;
	struct cvmx_sriox_rx_bell_seq_s cn63xxp1;
	struct cvmx_sriox_rx_bell_seq_s cn66xx;
};

union cvmx_sriox_rx_status {
	uint64_t u64;
	struct cvmx_sriox_rx_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rtn_pr3:8;
		uint64_t rtn_pr2:8;
		uint64_t rtn_pr1:8;
		uint64_t reserved_28_39:12;
		uint64_t mbox:4;
		uint64_t comp:8;
		uint64_t reserved_13_15:3;
		uint64_t n_post:5;
		uint64_t post:8;
#else
		uint64_t post:8;
		uint64_t n_post:5;
		uint64_t reserved_13_15:3;
		uint64_t comp:8;
		uint64_t mbox:4;
		uint64_t reserved_28_39:12;
		uint64_t rtn_pr1:8;
		uint64_t rtn_pr2:8;
		uint64_t rtn_pr3:8;
#endif
	} s;
	struct cvmx_sriox_rx_status_s cn63xx;
	struct cvmx_sriox_rx_status_s cn63xxp1;
	struct cvmx_sriox_rx_status_s cn66xx;
};

union cvmx_sriox_s2m_typex {
	uint64_t u64;
	struct cvmx_sriox_s2m_typex_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t wr_op:3;
		uint64_t reserved_15_15:1;
		uint64_t rd_op:3;
		uint64_t wr_prior:2;
		uint64_t rd_prior:2;
		uint64_t reserved_6_7:2;
		uint64_t src_id:1;
		uint64_t id16:1;
		uint64_t reserved_2_3:2;
		uint64_t iaow_sel:2;
#else
		uint64_t iaow_sel:2;
		uint64_t reserved_2_3:2;
		uint64_t id16:1;
		uint64_t src_id:1;
		uint64_t reserved_6_7:2;
		uint64_t rd_prior:2;
		uint64_t wr_prior:2;
		uint64_t rd_op:3;
		uint64_t reserved_15_15:1;
		uint64_t wr_op:3;
		uint64_t reserved_19_63:45;
#endif
	} s;
	struct cvmx_sriox_s2m_typex_s cn63xx;
	struct cvmx_sriox_s2m_typex_s cn63xxp1;
	struct cvmx_sriox_s2m_typex_s cn66xx;
};

union cvmx_sriox_seq {
	uint64_t u64;
	struct cvmx_sriox_seq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t seq:32;
#else
		uint64_t seq:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sriox_seq_s cn63xx;
	struct cvmx_sriox_seq_s cn63xxp1;
	struct cvmx_sriox_seq_s cn66xx;
};

union cvmx_sriox_status_reg {
	uint64_t u64;
	struct cvmx_sriox_status_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t access:1;
		uint64_t srio:1;
#else
		uint64_t srio:1;
		uint64_t access:1;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_sriox_status_reg_s cn63xx;
	struct cvmx_sriox_status_reg_s cn63xxp1;
	struct cvmx_sriox_status_reg_s cn66xx;
};

union cvmx_sriox_tag_ctrl {
	uint64_t u64;
	struct cvmx_sriox_tag_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t o_clr:1;
		uint64_t reserved_13_15:3;
		uint64_t otag:5;
		uint64_t reserved_5_7:3;
		uint64_t itag:5;
#else
		uint64_t itag:5;
		uint64_t reserved_5_7:3;
		uint64_t otag:5;
		uint64_t reserved_13_15:3;
		uint64_t o_clr:1;
		uint64_t reserved_17_63:47;
#endif
	} s;
	struct cvmx_sriox_tag_ctrl_s cn63xx;
	struct cvmx_sriox_tag_ctrl_s cn63xxp1;
	struct cvmx_sriox_tag_ctrl_s cn66xx;
};

union cvmx_sriox_tlp_credits {
	uint64_t u64;
	struct cvmx_sriox_tlp_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t mbox:4;
		uint64_t comp:8;
		uint64_t reserved_13_15:3;
		uint64_t n_post:5;
		uint64_t post:8;
#else
		uint64_t post:8;
		uint64_t n_post:5;
		uint64_t reserved_13_15:3;
		uint64_t comp:8;
		uint64_t mbox:4;
		uint64_t reserved_28_63:36;
#endif
	} s;
	struct cvmx_sriox_tlp_credits_s cn63xx;
	struct cvmx_sriox_tlp_credits_s cn63xxp1;
	struct cvmx_sriox_tlp_credits_s cn66xx;
};

union cvmx_sriox_tx_bell {
	uint64_t u64;
	struct cvmx_sriox_tx_bell_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t data:16;
		uint64_t dest_id:16;
		uint64_t reserved_9_15:7;
		uint64_t pending:1;
		uint64_t reserved_5_7:3;
		uint64_t src_id:1;
		uint64_t id16:1;
		uint64_t reserved_2_2:1;
		uint64_t priority:2;
#else
		uint64_t priority:2;
		uint64_t reserved_2_2:1;
		uint64_t id16:1;
		uint64_t src_id:1;
		uint64_t reserved_5_7:3;
		uint64_t pending:1;
		uint64_t reserved_9_15:7;
		uint64_t dest_id:16;
		uint64_t data:16;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_sriox_tx_bell_s cn63xx;
	struct cvmx_sriox_tx_bell_s cn63xxp1;
	struct cvmx_sriox_tx_bell_s cn66xx;
};

union cvmx_sriox_tx_bell_info {
	uint64_t u64;
	struct cvmx_sriox_tx_bell_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t data:16;
		uint64_t dest_id:16;
		uint64_t reserved_8_15:8;
		uint64_t timeout:1;
		uint64_t error:1;
		uint64_t retry:1;
		uint64_t src_id:1;
		uint64_t id16:1;
		uint64_t reserved_2_2:1;
		uint64_t priority:2;
#else
		uint64_t priority:2;
		uint64_t reserved_2_2:1;
		uint64_t id16:1;
		uint64_t src_id:1;
		uint64_t retry:1;
		uint64_t error:1;
		uint64_t timeout:1;
		uint64_t reserved_8_15:8;
		uint64_t dest_id:16;
		uint64_t data:16;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_sriox_tx_bell_info_s cn63xx;
	struct cvmx_sriox_tx_bell_info_s cn63xxp1;
	struct cvmx_sriox_tx_bell_info_s cn66xx;
};

union cvmx_sriox_tx_ctrl {
	uint64_t u64;
	struct cvmx_sriox_tx_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_53_63:11;
		uint64_t tag_th2:5;
		uint64_t reserved_45_47:3;
		uint64_t tag_th1:5;
		uint64_t reserved_37_39:3;
		uint64_t tag_th0:5;
		uint64_t reserved_20_31:12;
		uint64_t tx_th2:4;
		uint64_t reserved_12_15:4;
		uint64_t tx_th1:4;
		uint64_t reserved_4_7:4;
		uint64_t tx_th0:4;
#else
		uint64_t tx_th0:4;
		uint64_t reserved_4_7:4;
		uint64_t tx_th1:4;
		uint64_t reserved_12_15:4;
		uint64_t tx_th2:4;
		uint64_t reserved_20_31:12;
		uint64_t tag_th0:5;
		uint64_t reserved_37_39:3;
		uint64_t tag_th1:5;
		uint64_t reserved_45_47:3;
		uint64_t tag_th2:5;
		uint64_t reserved_53_63:11;
#endif
	} s;
	struct cvmx_sriox_tx_ctrl_s cn63xx;
	struct cvmx_sriox_tx_ctrl_s cn63xxp1;
	struct cvmx_sriox_tx_ctrl_s cn66xx;
};

union cvmx_sriox_tx_emphasis {
	uint64_t u64;
	struct cvmx_sriox_tx_emphasis_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t emph:4;
#else
		uint64_t emph:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_sriox_tx_emphasis_s cn63xx;
	struct cvmx_sriox_tx_emphasis_s cn66xx;
};

union cvmx_sriox_tx_status {
	uint64_t u64;
	struct cvmx_sriox_tx_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t s2m_pr3:8;
		uint64_t s2m_pr2:8;
		uint64_t s2m_pr1:8;
		uint64_t s2m_pr0:8;
#else
		uint64_t s2m_pr0:8;
		uint64_t s2m_pr1:8;
		uint64_t s2m_pr2:8;
		uint64_t s2m_pr3:8;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sriox_tx_status_s cn63xx;
	struct cvmx_sriox_tx_status_s cn63xxp1;
	struct cvmx_sriox_tx_status_s cn66xx;
};

union cvmx_sriox_wr_done_counts {
	uint64_t u64;
	struct cvmx_sriox_wr_done_counts_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t bad:16;
		uint64_t good:16;
#else
		uint64_t good:16;
		uint64_t bad:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_sriox_wr_done_counts_s cn63xx;
	struct cvmx_sriox_wr_done_counts_s cn66xx;
};

#endif
