/******************************************************************************
 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * vxge-reg.h: Driver for Neterion Inc's X3100 Series 10GbE PCIe I/O Virtualized
 *             Server Adapter.
 * Copyright(c) 2002-2009 Neterion Inc.
 ******************************************************************************/
#ifndef VXGE_REG_H
#define VXGE_REG_H

/*
 * vxge_mBIT(loc) - set bit at offset
 */
#define vxge_mBIT(loc)		(0x8000000000000000ULL >> (loc))

/*
 * vxge_vBIT(val, loc, sz) - set bits at offset
 */
#define vxge_vBIT(val, loc, sz)	(((u64)(val)) << (64-(loc)-(sz)))
#define vxge_vBIT32(val, loc, sz)	(((u32)(val)) << (32-(loc)-(sz)))

/*
 * vxge_bVALn(bits, loc, n) - Get the value of n bits at location
 */
#define vxge_bVALn(bits, loc, n) \
	((((u64)bits) >> (64-(loc+n))) & ((0x1ULL << n) - 1))

#define	VXGE_HW_TITAN_ASIC_ID_GET_INITIAL_DEVICE_ID(bits) \
							vxge_bVALn(bits, 0, 16)
#define	VXGE_HW_TITAN_ASIC_ID_GET_INITIAL_MAJOR_REVISION(bits) \
							vxge_bVALn(bits, 48, 8)
#define	VXGE_HW_TITAN_ASIC_ID_GET_INITIAL_MINOR_REVISION(bits) \
							vxge_bVALn(bits, 56, 8)

#define	VXGE_HW_VPATH_TO_FUNC_MAP_CFG1_GET_VPATH_TO_FUNC_MAP_CFG1(bits) \
							vxge_bVALn(bits, 3, 5)
#define	VXGE_HW_HOST_TYPE_ASSIGNMENTS_GET_HOST_TYPE_ASSIGNMENTS(bits) \
							vxge_bVALn(bits, 5, 3)
#define VXGE_HW_PF_SW_RESET_COMMAND				0xA5

#define VXGE_HW_TITAN_PCICFGMGMT_REG_SPACES		17
#define VXGE_HW_TITAN_SRPCIM_REG_SPACES			17
#define VXGE_HW_TITAN_VPMGMT_REG_SPACES			17
#define VXGE_HW_TITAN_VPATH_REG_SPACES			17

#define VXGE_HW_ASIC_MODE_RESERVED				0
#define VXGE_HW_ASIC_MODE_NO_IOV				1
#define VXGE_HW_ASIC_MODE_SR_IOV				2
#define VXGE_HW_ASIC_MODE_MR_IOV				3

#define	VXGE_HW_TXMAC_GEN_CFG1_TMAC_PERMA_STOP_EN		vxge_mBIT(3)
#define	VXGE_HW_TXMAC_GEN_CFG1_BLOCK_BCAST_TO_WIRE		vxge_mBIT(19)
#define	VXGE_HW_TXMAC_GEN_CFG1_BLOCK_BCAST_TO_SWITCH	vxge_mBIT(23)
#define	VXGE_HW_TXMAC_GEN_CFG1_HOST_APPEND_FCS			vxge_mBIT(31)

#define	VXGE_HW_VPATH_IS_FIRST_GET_VPATH_IS_FIRST(bits)	vxge_bVALn(bits, 3, 1)

#define	VXGE_HW_TIM_VPATH_ASSIGNMENT_GET_BMAP_ROOT(bits) \
						vxge_bVALn(bits, 0, 32)

#define	VXGE_HW_RXMAC_CFG0_PORT_VPMGMT_CLONE_GET_MAX_PYLD_LEN(bits) \
							vxge_bVALn(bits, 50, 14)

#define	VXGE_HW_XMAC_VSPORT_CHOICES_VP_GET_VSPORT_VECTOR(bits) \
							vxge_bVALn(bits, 0, 17)

#define	VXGE_HW_XMAC_VPATH_TO_VSPORT_VPMGMT_CLONE_GET_VSPORT_NUMBER(bits) \
							vxge_bVALn(bits, 3, 5)

#define	VXGE_HW_KDFC_DRBL_TRIPLET_TOTAL_GET_KDFC_MAX_SIZE(bits) \
							vxge_bVALn(bits, 17, 15)

#define VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_MODE_LEGACY_MODE			0
#define VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_MODE_NON_OFFLOAD_ONLY		1
#define VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_MODE_MULTI_OP_MODE		2

#define VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_MODE_MESSAGES_ONLY		0
#define VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_MODE_MULTI_OP_MODE		1

#define	VXGE_HW_TOC_GET_KDFC_INITIAL_OFFSET(val) \
				(val&~VXGE_HW_TOC_KDFC_INITIAL_BIR(7))
#define	VXGE_HW_TOC_GET_KDFC_INITIAL_BIR(val) \
				vxge_bVALn(val, 61, 3)
#define	VXGE_HW_TOC_GET_USDC_INITIAL_OFFSET(val) \
				(val&~VXGE_HW_TOC_USDC_INITIAL_BIR(7))
#define	VXGE_HW_TOC_GET_USDC_INITIAL_BIR(val) \
				vxge_bVALn(val, 61, 3)

#define	VXGE_HW_TOC_KDFC_VPATH_STRIDE_GET_TOC_KDFC_VPATH_STRIDE(bits)	bits
#define	VXGE_HW_TOC_KDFC_FIFO_STRIDE_GET_TOC_KDFC_FIFO_STRIDE(bits)	bits

#define	VXGE_HW_KDFC_TRPL_FIFO_OFFSET_GET_KDFC_RCTR0(bits) \
						vxge_bVALn(bits, 1, 15)
#define	VXGE_HW_KDFC_TRPL_FIFO_OFFSET_GET_KDFC_RCTR1(bits) \
						vxge_bVALn(bits, 17, 15)
#define	VXGE_HW_KDFC_TRPL_FIFO_OFFSET_GET_KDFC_RCTR2(bits) \
						vxge_bVALn(bits, 33, 15)

#define VXGE_HW_KDFC_TRPL_FIFO_OFFSET_KDFC_VAPTH_NUM(val) vxge_vBIT(val, 42, 5)
#define VXGE_HW_KDFC_TRPL_FIFO_OFFSET_KDFC_FIFO_NUM(val) vxge_vBIT(val, 47, 2)
#define VXGE_HW_KDFC_TRPL_FIFO_OFFSET_KDFC_FIFO_OFFSET(val) \
					vxge_vBIT(val, 49, 15)

#define VXGE_HW_PRC_CFG4_RING_MODE_ONE_BUFFER			0
#define VXGE_HW_PRC_CFG4_RING_MODE_THREE_BUFFER			1
#define VXGE_HW_PRC_CFG4_RING_MODE_FIVE_BUFFER			2

#define VXGE_HW_PRC_CFG7_SCATTER_MODE_A				0
#define VXGE_HW_PRC_CFG7_SCATTER_MODE_B				2
#define VXGE_HW_PRC_CFG7_SCATTER_MODE_C				1

#define VXGE_HW_RTS_MGR_STEER_CTRL_WE_READ                             0
#define VXGE_HW_RTS_MGR_STEER_CTRL_WE_WRITE                            1

#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_DA                  0
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_VID                 1
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_ETYPE               2
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_PN                  3
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_RANGE_PN            4
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_RTH_GEN_CFG         5
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_RTH_SOLO_IT         6
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_RTH_JHASH_CFG       7
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_RTH_MASK            8
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_RTH_KEY             9
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_QOS                 10
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_DS                  11
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_RTH_MULTI_IT        12
#define VXGE_HW_RTS_MGR_STEER_CTRL_DATA_STRUCT_SEL_FW_VERSION          13

#define VXGE_HW_RTS_MGR_STEER_DATA0_GET_DA_MAC_ADDR(bits) \
							vxge_bVALn(bits, 0, 48)
#define VXGE_HW_RTS_MGR_STEER_DATA0_DA_MAC_ADDR(val) vxge_vBIT(val, 0, 48)

#define VXGE_HW_RTS_MGR_STEER_DATA1_GET_DA_MAC_ADDR_MASK(bits) \
							vxge_bVALn(bits, 0, 48)
#define VXGE_HW_RTS_MGR_STEER_DATA1_DA_MAC_ADDR_MASK(val) vxge_vBIT(val, 0, 48)
#define VXGE_HW_RTS_MGR_STEER_DATA1_DA_MAC_ADDR_ADD_PRIVILEGED_MODE \
								vxge_mBIT(54)
#define VXGE_HW_RTS_MGR_STEER_DATA1_GET_DA_MAC_ADDR_ADD_VPATH(bits) \
							vxge_bVALn(bits, 55, 5)
#define VXGE_HW_RTS_MGR_STEER_DATA1_DA_MAC_ADDR_ADD_VPATH(val) \
							vxge_vBIT(val, 55, 5)
#define VXGE_HW_RTS_MGR_STEER_DATA1_GET_DA_MAC_ADDR_ADD_MODE(bits) \
							vxge_bVALn(bits, 62, 2)
#define VXGE_HW_RTS_MGR_STEER_DATA1_DA_MAC_ADDR_MODE(val) vxge_vBIT(val, 62, 2)

#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_ADD_ENTRY			0
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_DELETE_ENTRY		1
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_LIST_FIRST_ENTRY		2
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_LIST_NEXT_ENTRY		3
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_READ_ENTRY			0
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_WRITE_ENTRY		1
#define VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_READ_MEMO_ENTRY		3
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_LED_CONTROL		4
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_ALL_CLEAR			172

#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_DA		0
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_VID		1
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_ETYPE		2
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_PN		3
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_RTH_GEN_CFG	5
#define	VXGE_HW_RTS_ACS_STEER_CTRL_DATA_STRUCT_SEL_RTH_SOLO_IT	6
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_RTH_JHASH_CFG	7
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_RTH_MASK		8
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_RTH_KEY		9
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_QOS		10
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_DS		11
#define	VXGE_HW_RTS_ACS_STEER_CTRL_DATA_STRUCT_SEL_RTH_MULTI_IT	12
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_FW_MEMO		13

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_DA_MAC_ADDR(bits) \
							vxge_bVALn(bits, 0, 48)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_DA_MAC_ADDR(val) vxge_vBIT(val, 0, 48)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_VLAN_ID(bits) vxge_bVALn(bits, 0, 12)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_VLAN_ID(val) vxge_vBIT(val, 0, 12)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_ETYPE(bits)	vxge_bVALn(bits, 0, 11)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_ETYPE(val) vxge_vBIT(val, 0, 16)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_PN_SRC_DEST_SEL(bits) \
							vxge_bVALn(bits, 3, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_PN_SRC_DEST_SEL		vxge_mBIT(3)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_PN_TCP_UDP_SEL(bits) \
							vxge_bVALn(bits, 7, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_PN_TCP_UDP_SEL		vxge_mBIT(7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_PN_PORT_NUM(bits) \
							vxge_bVALn(bits, 8, 16)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_PN_PORT_NUM(val) vxge_vBIT(val, 8, 16)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_RTH_EN(bits) \
							vxge_bVALn(bits, 3, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_EN		vxge_mBIT(3)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_BUCKET_SIZE(bits) \
							vxge_bVALn(bits, 4, 4)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_BUCKET_SIZE(val) \
							vxge_vBIT(val, 4, 4)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_ALG_SEL(bits) \
							vxge_bVALn(bits, 10, 2)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_ALG_SEL(val) \
							vxge_vBIT(val, 10, 2)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_ALG_SEL_JENKINS	0
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_ALG_SEL_MS_RSS	1
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_ALG_SEL_CRC32C	2
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_RTH_TCP_IPV4_EN(bits) \
							vxge_bVALn(bits, 15, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_TCP_IPV4_EN	vxge_mBIT(15)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_RTH_IPV4_EN(bits) \
							vxge_bVALn(bits, 19, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_IPV4_EN	vxge_mBIT(19)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_RTH_TCP_IPV6_EN(bits) \
							vxge_bVALn(bits, 23, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_TCP_IPV6_EN	vxge_mBIT(23)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_RTH_IPV6_EN(bits) \
							vxge_bVALn(bits, 27, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_IPV6_EN	vxge_mBIT(27)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_RTH_TCP_IPV6_EX_EN(bits) \
							vxge_bVALn(bits, 31, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_TCP_IPV6_EX_EN vxge_mBIT(31)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_RTH_IPV6_EX_EN(bits) \
							vxge_bVALn(bits, 35, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_IPV6_EX_EN	vxge_mBIT(35)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_ACTIVE_TABLE(bits) \
							vxge_bVALn(bits, 39, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_ACTIVE_TABLE	vxge_mBIT(39)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_REPL_ENTRY_EN(bits) \
							vxge_bVALn(bits, 43, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_REPL_ENTRY_EN	vxge_mBIT(43)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_SOLO_IT_ENTRY_EN(bits) \
							vxge_bVALn(bits, 3, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_SOLO_IT_ENTRY_EN	vxge_mBIT(3)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_SOLO_IT_BUCKET_DATA(bits) \
							vxge_bVALn(bits, 9, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_SOLO_IT_BUCKET_DATA(val) \
							vxge_vBIT(val, 9, 7)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_ITEM0_BUCKET_NUM(bits) \
							vxge_bVALn(bits, 0, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM0_BUCKET_NUM(val) \
							vxge_vBIT(val, 0, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_ITEM0_ENTRY_EN(bits) \
							vxge_bVALn(bits, 8, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM0_ENTRY_EN	vxge_mBIT(8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_ITEM0_BUCKET_DATA(bits) \
							vxge_bVALn(bits, 9, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM0_BUCKET_DATA(val) \
							vxge_vBIT(val, 9, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_ITEM1_BUCKET_NUM(bits) \
							vxge_bVALn(bits, 16, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM1_BUCKET_NUM(val) \
							vxge_vBIT(val, 16, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_ITEM1_ENTRY_EN(bits) \
							vxge_bVALn(bits, 24, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM1_ENTRY_EN	vxge_mBIT(24)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_ITEM1_BUCKET_DATA(bits) \
							vxge_bVALn(bits, 25, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM1_BUCKET_DATA(val) \
							vxge_vBIT(val, 25, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM0_BUCKET_NUM(bits) \
							vxge_bVALn(bits, 0, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM0_BUCKET_NUM(val) \
							vxge_vBIT(val, 0, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM0_ENTRY_EN(bits) \
							vxge_bVALn(bits, 8, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM0_ENTRY_EN	vxge_mBIT(8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM0_BUCKET_DATA(bits) \
							vxge_bVALn(bits, 9, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM0_BUCKET_DATA(val) \
							vxge_vBIT(val, 9, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM1_BUCKET_NUM(bits) \
							vxge_bVALn(bits, 16, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM1_BUCKET_NUM(val) \
							vxge_vBIT(val, 16, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM1_ENTRY_EN(bits) \
							vxge_bVALn(bits, 24, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM1_ENTRY_EN	vxge_mBIT(24)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM1_BUCKET_DATA(bits) \
							vxge_bVALn(bits, 25, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM1_BUCKET_DATA(val) \
							vxge_vBIT(val, 25, 7)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_JHASH_CFG_GOLDEN_RATIO(bits) \
							vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_JHASH_CFG_GOLDEN_RATIO(val) \
							vxge_vBIT(val, 0, 32)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_JHASH_CFG_INIT_VALUE(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_JHASH_CFG_INIT_VALUE(val) \
							vxge_vBIT(val, 32, 32)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_MASK_IPV6_SA_MASK(bits) \
							vxge_bVALn(bits, 0, 16)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_MASK_IPV6_SA_MASK(val) \
							vxge_vBIT(val, 0, 16)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_MASK_IPV6_DA_MASK(bits) \
							vxge_bVALn(bits, 16, 16)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_MASK_IPV6_DA_MASK(val) \
							vxge_vBIT(val, 16, 16)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_MASK_IPV4_SA_MASK(bits) \
							vxge_bVALn(bits, 32, 4)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_MASK_IPV4_SA_MASK(val) \
							vxge_vBIT(val, 32, 4)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_MASK_IPV4_DA_MASK(bits) \
							vxge_bVALn(bits, 36, 4)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_MASK_IPV4_DA_MASK(val) \
							vxge_vBIT(val, 36, 4)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_MASK_L4SP_MASK(bits) \
							vxge_bVALn(bits, 40, 2)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_MASK_L4SP_MASK(val) \
							vxge_vBIT(val, 40, 2)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_MASK_L4DP_MASK(bits) \
							vxge_bVALn(bits, 42, 2)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_MASK_L4DP_MASK(val) \
							vxge_vBIT(val, 42, 2)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_KEY_KEY(bits) \
							vxge_bVALn(bits, 0, 64)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_KEY_KEY vxge_vBIT(val, 0, 64)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_QOS_ENTRY_EN(bits) \
							vxge_bVALn(bits, 3, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_QOS_ENTRY_EN		vxge_mBIT(3)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_DS_ENTRY_EN(bits) \
							vxge_bVALn(bits, 3, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_DS_ENTRY_EN		vxge_mBIT(3)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_DA_MAC_ADDR_MASK(bits) \
							vxge_bVALn(bits, 0, 48)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_DA_MAC_ADDR_MASK(val) \
							vxge_vBIT(val, 0, 48)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_DA_MAC_ADDR_MODE(val) \
							vxge_vBIT(val, 62, 2)

#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM4_BUCKET_NUM(bits) \
							vxge_bVALn(bits, 0, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM4_BUCKET_NUM(val) \
							vxge_vBIT(val, 0, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM4_ENTRY_EN(bits) \
							vxge_bVALn(bits, 8, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM4_ENTRY_EN	vxge_mBIT(8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM4_BUCKET_DATA(bits) \
							vxge_bVALn(bits, 9, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM4_BUCKET_DATA(val) \
							vxge_vBIT(val, 9, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM5_BUCKET_NUM(bits) \
							vxge_bVALn(bits, 16, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM5_BUCKET_NUM(val) \
							vxge_vBIT(val, 16, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM5_ENTRY_EN(bits) \
							vxge_bVALn(bits, 24, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM5_ENTRY_EN	vxge_mBIT(24)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM5_BUCKET_DATA(bits) \
							vxge_bVALn(bits, 25, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM5_BUCKET_DATA(val) \
							vxge_vBIT(val, 25, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM6_BUCKET_NUM(bits) \
							vxge_bVALn(bits, 32, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM6_BUCKET_NUM(val) \
							vxge_vBIT(val, 32, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM6_ENTRY_EN(bits) \
							vxge_bVALn(bits, 40, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM6_ENTRY_EN	vxge_mBIT(40)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM6_BUCKET_DATA(bits) \
							vxge_bVALn(bits, 41, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM6_BUCKET_DATA(val) \
							vxge_vBIT(val, 41, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM7_BUCKET_NUM(bits) \
							vxge_bVALn(bits, 48, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM7_BUCKET_NUM(val) \
							vxge_vBIT(val, 48, 8)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM7_ENTRY_EN(bits) \
							vxge_bVALn(bits, 56, 1)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM7_ENTRY_EN	vxge_mBIT(56)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_RTH_ITEM7_BUCKET_DATA(bits) \
							vxge_bVALn(bits, 57, 7)
#define	VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM7_BUCKET_DATA(val) \
							vxge_vBIT(val, 57, 7)

#define VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_PART_NUMBER           0
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_SERIAL_NUMBER         1
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_VERSION               2
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_PCI_MODE              3
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_DESC_0                4
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_DESC_1                5
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_DESC_2                6
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_DESC_3                7

#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_LED_CONTROL_ON			1
#define	VXGE_HW_RTS_ACCESS_STEER_DATA0_LED_CONTROL_OFF			0

#define VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_DAY(bits) \
							vxge_bVALn(bits, 0, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_FW_VER_DAY(val) vxge_vBIT(val, 0, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_MONTH(bits) \
							vxge_bVALn(bits, 8, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_FW_VER_MONTH(val) vxge_vBIT(val, 8, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_YEAR(bits) \
						vxge_bVALn(bits, 16, 16)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_FW_VER_YEAR(val) \
							vxge_vBIT(val, 16, 16)

#define VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_MAJOR(bits) \
						vxge_bVALn(bits, 32, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_FW_VER_MAJOR vxge_vBIT(val, 32, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_MINOR(bits) \
						vxge_bVALn(bits, 40, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_FW_VER_MINOR vxge_vBIT(val, 40, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_BUILD(bits) \
						vxge_bVALn(bits, 48, 16)
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_FW_VER_BUILD vxge_vBIT(val, 48, 16)

#define VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_DAY(bits) \
						vxge_bVALn(bits, 0, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_FLASH_VER_DAY(val) vxge_vBIT(val, 0, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_MONTH(bits) \
							vxge_bVALn(bits, 8, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_FLASH_VER_MONTH(val) vxge_vBIT(val, 8, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_YEAR(bits) \
							vxge_bVALn(bits, 16, 16)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_FLASH_VER_YEAR(val) \
							vxge_vBIT(val, 16, 16)

#define VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_MAJOR(bits) \
							vxge_bVALn(bits, 32, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_FLASH_VER_MAJOR vxge_vBIT(val, 32, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_MINOR(bits) \
							vxge_bVALn(bits, 40, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_FLASH_VER_MINOR vxge_vBIT(val, 40, 8)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_BUILD(bits) \
							vxge_bVALn(bits, 48, 16)
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_FLASH_VER_BUILD vxge_vBIT(val, 48, 16)

#define	VXGE_HW_SRPCIM_TO_VPATH_ALARM_REG_GET_PPIF_SRPCIM_TO_VPATH_ALARM(bits)\
							vxge_bVALn(bits, 0, 18)

#define	VXGE_HW_RX_MULTI_CAST_STATS_GET_FRAME_DISCARD(bits) \
							vxge_bVALn(bits, 48, 16)
#define	VXGE_HW_RX_FRM_TRANSFERRED_GET_RX_FRM_TRANSFERRED(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_RXD_RETURNED_GET_RXD_RETURNED(bits)	vxge_bVALn(bits, 48, 16)
#define	VXGE_HW_VPATH_DEBUG_STATS0_GET_INI_NUM_MWR_SENT(bits) \
							vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_VPATH_DEBUG_STATS1_GET_INI_NUM_MRD_SENT(bits) \
							vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_VPATH_DEBUG_STATS2_GET_INI_NUM_CPL_RCVD(bits) \
							vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_VPATH_DEBUG_STATS3_GET_INI_NUM_MWR_BYTE_SENT(bits)	(bits)
#define	VXGE_HW_VPATH_DEBUG_STATS4_GET_INI_NUM_CPL_BYTE_RCVD(bits)	(bits)
#define	VXGE_HW_VPATH_DEBUG_STATS5_GET_WRCRDTARB_XOFF(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_VPATH_DEBUG_STATS6_GET_RDCRDTARB_XOFF(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_VPATH_GENSTATS_COUNT01_GET_PPIF_VPATH_GENSTATS_COUNT1(bits) \
							vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_VPATH_GENSTATS_COUNT01_GET_PPIF_VPATH_GENSTATS_COUNT0(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_VPATH_GENSTATS_COUNT23_GET_PPIF_VPATH_GENSTATS_COUNT3(bits) \
							vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_VPATH_GENSTATS_COUNT23_GET_PPIF_VPATH_GENSTATS_COUNT2(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_VPATH_GENSTATS_COUNT4_GET_PPIF_VPATH_GENSTATS_COUNT4(bits) \
							vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_VPATH_GENSTATS_COUNT5_GET_PPIF_VPATH_GENSTATS_COUNT5(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_TX_VP_RESET_DISCARDED_FRMS_GET_TX_VP_RESET_DISCARDED_FRMS(bits\
) vxge_bVALn(bits, 48, 16)
#define	VXGE_HW_DBG_STATS_GET_RX_MPA_CRC_FAIL_FRMS(bits) vxge_bVALn(bits, 0, 16)
#define	VXGE_HW_DBG_STATS_GET_RX_MPA_MRK_FAIL_FRMS(bits) \
							vxge_bVALn(bits, 16, 16)
#define	VXGE_HW_DBG_STATS_GET_RX_MPA_LEN_FAIL_FRMS(bits) \
							vxge_bVALn(bits, 32, 16)
#define	VXGE_HW_DBG_STATS_GET_RX_FAU_RX_WOL_FRMS(bits)	vxge_bVALn(bits, 0, 16)
#define	VXGE_HW_DBG_STATS_GET_RX_FAU_RX_VP_RESET_DISCARDED_FRMS(bits) \
							vxge_bVALn(bits, 16, 16)
#define	VXGE_HW_DBG_STATS_GET_RX_FAU_RX_PERMITTED_FRMS(bits) \
							vxge_bVALn(bits, 32, 16)

#define	VXGE_HW_MRPCIM_DEBUG_STATS0_GET_INI_WR_DROP(bits) \
							vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_MRPCIM_DEBUG_STATS0_GET_INI_RD_DROP(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_MRPCIM_DEBUG_STATS1_GET_VPLANE_WRCRDTARB_PH_CRDT_DEPLETED(bits\
) vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_MRPCIM_DEBUG_STATS2_GET_VPLANE_WRCRDTARB_PD_CRDT_DEPLETED(bits\
) vxge_bVALn(bits, 32, 32)
#define \
VXGE_HW_MRPCIM_DEBUG_STATS3_GET_VPLANE_RDCRDTARB_NPH_CRDT_DEPLETED(bits) \
	vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_MRPCIM_DEBUG_STATS4_GET_INI_WR_VPIN_DROP(bits) \
							vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_MRPCIM_DEBUG_STATS4_GET_INI_RD_VPIN_DROP(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_GENSTATS_COUNT01_GET_GENSTATS_COUNT1(bits) \
							vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_GENSTATS_COUNT01_GET_GENSTATS_COUNT0(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_GENSTATS_COUNT23_GET_GENSTATS_COUNT3(bits) \
							vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_GENSTATS_COUNT23_GET_GENSTATS_COUNT2(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_GENSTATS_COUNT4_GET_GENSTATS_COUNT4(bits) \
							vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_GENSTATS_COUNT5_GET_GENSTATS_COUNT5(bits) \
							vxge_bVALn(bits, 32, 32)

#define	VXGE_HW_DEBUG_STATS0_GET_RSTDROP_MSG(bits)	vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_DEBUG_STATS0_GET_RSTDROP_CPL(bits)	vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_DEBUG_STATS1_GET_RSTDROP_CLIENT0(bits)	vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_DEBUG_STATS1_GET_RSTDROP_CLIENT1(bits)	vxge_bVALn(bits, 32, 32)
#define	VXGE_HW_DEBUG_STATS2_GET_RSTDROP_CLIENT2(bits)	vxge_bVALn(bits, 0, 32)
#define	VXGE_HW_DEBUG_STATS3_GET_VPLANE_DEPL_PH(bits)	vxge_bVALn(bits, 0, 16)
#define	VXGE_HW_DEBUG_STATS3_GET_VPLANE_DEPL_NPH(bits)	vxge_bVALn(bits, 16, 16)
#define	VXGE_HW_DEBUG_STATS3_GET_VPLANE_DEPL_CPLH(bits)	vxge_bVALn(bits, 32, 16)
#define	VXGE_HW_DEBUG_STATS4_GET_VPLANE_DEPL_PD(bits)	vxge_bVALn(bits, 0, 16)
#define	VXGE_HW_DEBUG_STATS4_GET_VPLANE_DEPL_NPD(bits)	bVAL(bits, 16, 16)
#define	VXGE_HW_DEBUG_STATS4_GET_VPLANE_DEPL_CPLD(bits)	vxge_bVALn(bits, 32, 16)

#define	VXGE_HW_DBG_STATS_TPA_TX_PATH_GET_TX_PERMITTED_FRMS(bits) \
							vxge_bVALn(bits, 32, 32)

#define	VXGE_HW_DBG_STAT_TX_ANY_FRMS_GET_PORT0_TX_ANY_FRMS(bits) \
							vxge_bVALn(bits, 0, 8)
#define	VXGE_HW_DBG_STAT_TX_ANY_FRMS_GET_PORT1_TX_ANY_FRMS(bits) \
							vxge_bVALn(bits, 8, 8)
#define	VXGE_HW_DBG_STAT_TX_ANY_FRMS_GET_PORT2_TX_ANY_FRMS(bits) \
							vxge_bVALn(bits, 16, 8)

#define	VXGE_HW_DBG_STAT_RX_ANY_FRMS_GET_PORT0_RX_ANY_FRMS(bits) \
							vxge_bVALn(bits, 0, 8)
#define	VXGE_HW_DBG_STAT_RX_ANY_FRMS_GET_PORT1_RX_ANY_FRMS(bits) \
							vxge_bVALn(bits, 8, 8)
#define	VXGE_HW_DBG_STAT_RX_ANY_FRMS_GET_PORT2_RX_ANY_FRMS(bits) \
							vxge_bVALn(bits, 16, 8)

#define VXGE_HW_CONFIG_PRIV_H

#define VXGE_HW_SWAPPER_INITIAL_VALUE			0x0123456789abcdefULL
#define VXGE_HW_SWAPPER_BYTE_SWAPPED			0xefcdab8967452301ULL
#define VXGE_HW_SWAPPER_BIT_FLIPPED			0x80c4a2e691d5b3f7ULL
#define VXGE_HW_SWAPPER_BYTE_SWAPPED_BIT_FLIPPED	0xf7b3d591e6a2c480ULL

#define VXGE_HW_SWAPPER_READ_BYTE_SWAP_ENABLE		0xFFFFFFFFFFFFFFFFULL
#define VXGE_HW_SWAPPER_READ_BYTE_SWAP_DISABLE		0x0000000000000000ULL

#define VXGE_HW_SWAPPER_READ_BIT_FLAP_ENABLE		0xFFFFFFFFFFFFFFFFULL
#define VXGE_HW_SWAPPER_READ_BIT_FLAP_DISABLE		0x0000000000000000ULL

#define VXGE_HW_SWAPPER_WRITE_BYTE_SWAP_ENABLE		0xFFFFFFFFFFFFFFFFULL
#define VXGE_HW_SWAPPER_WRITE_BYTE_SWAP_DISABLE		0x0000000000000000ULL

#define VXGE_HW_SWAPPER_WRITE_BIT_FLAP_ENABLE		0xFFFFFFFFFFFFFFFFULL
#define VXGE_HW_SWAPPER_WRITE_BIT_FLAP_DISABLE		0x0000000000000000ULL

/*
 * The registers are memory mapped and are native big-endian byte order. The
 * little-endian hosts are handled by enabling hardware byte-swapping for
 * register and dma operations.
 */
struct vxge_hw_legacy_reg {

	u8	unused00010[0x00010];

/*0x00010*/	u64	toc_swapper_fb;
#define VXGE_HW_TOC_SWAPPER_FB_INITIAL_VAL(val) vxge_vBIT(val, 0, 64)
/*0x00018*/	u64	pifm_rd_swap_en;
#define VXGE_HW_PIFM_RD_SWAP_EN_PIFM_RD_SWAP_EN(val) vxge_vBIT(val, 0, 64)
/*0x00020*/	u64	pifm_rd_flip_en;
#define VXGE_HW_PIFM_RD_FLIP_EN_PIFM_RD_FLIP_EN(val) vxge_vBIT(val, 0, 64)
/*0x00028*/	u64	pifm_wr_swap_en;
#define VXGE_HW_PIFM_WR_SWAP_EN_PIFM_WR_SWAP_EN(val) vxge_vBIT(val, 0, 64)
/*0x00030*/	u64	pifm_wr_flip_en;
#define VXGE_HW_PIFM_WR_FLIP_EN_PIFM_WR_FLIP_EN(val) vxge_vBIT(val, 0, 64)
/*0x00038*/	u64	toc_first_pointer;
#define VXGE_HW_TOC_FIRST_POINTER_INITIAL_VAL(val) vxge_vBIT(val, 0, 64)
/*0x00040*/	u64	host_access_en;
#define VXGE_HW_HOST_ACCESS_EN_HOST_ACCESS_EN(val) vxge_vBIT(val, 0, 64)

} __packed;

struct vxge_hw_toc_reg {

	u8	unused00050[0x00050];

/*0x00050*/	u64	toc_common_pointer;
#define VXGE_HW_TOC_COMMON_POINTER_INITIAL_VAL(val) vxge_vBIT(val, 0, 64)
/*0x00058*/	u64	toc_memrepair_pointer;
#define VXGE_HW_TOC_MEMREPAIR_POINTER_INITIAL_VAL(val) vxge_vBIT(val, 0, 64)
/*0x00060*/	u64	toc_pcicfgmgmt_pointer[17];
#define VXGE_HW_TOC_PCICFGMGMT_POINTER_INITIAL_VAL(val) vxge_vBIT(val, 0, 64)
	u8	unused001e0[0x001e0-0x000e8];

/*0x001e0*/	u64	toc_mrpcim_pointer;
#define VXGE_HW_TOC_MRPCIM_POINTER_INITIAL_VAL(val) vxge_vBIT(val, 0, 64)
/*0x001e8*/	u64	toc_srpcim_pointer[17];
#define VXGE_HW_TOC_SRPCIM_POINTER_INITIAL_VAL(val) vxge_vBIT(val, 0, 64)
	u8	unused00278[0x00278-0x00270];

/*0x00278*/	u64	toc_vpmgmt_pointer[17];
#define VXGE_HW_TOC_VPMGMT_POINTER_INITIAL_VAL(val) vxge_vBIT(val, 0, 64)
	u8	unused00390[0x00390-0x00300];

/*0x00390*/	u64	toc_vpath_pointer[17];
#define VXGE_HW_TOC_VPATH_POINTER_INITIAL_VAL(val) vxge_vBIT(val, 0, 64)
	u8	unused004a0[0x004a0-0x00418];

/*0x004a0*/	u64	toc_kdfc;
#define VXGE_HW_TOC_KDFC_INITIAL_OFFSET(val) vxge_vBIT(val, 0, 61)
#define VXGE_HW_TOC_KDFC_INITIAL_BIR(val) vxge_vBIT(val, 61, 3)
/*0x004a8*/	u64	toc_usdc;
#define VXGE_HW_TOC_USDC_INITIAL_OFFSET(val) vxge_vBIT(val, 0, 61)
#define VXGE_HW_TOC_USDC_INITIAL_BIR(val) vxge_vBIT(val, 61, 3)
/*0x004b0*/	u64	toc_kdfc_vpath_stride;
#define	VXGE_HW_TOC_KDFC_VPATH_STRIDE_INITIAL_TOC_KDFC_VPATH_STRIDE(val) \
							vxge_vBIT(val, 0, 64)
/*0x004b8*/	u64	toc_kdfc_fifo_stride;
#define	VXGE_HW_TOC_KDFC_FIFO_STRIDE_INITIAL_TOC_KDFC_FIFO_STRIDE(val) \
							vxge_vBIT(val, 0, 64)

} __packed;

struct vxge_hw_common_reg {

	u8	unused00a00[0x00a00];

/*0x00a00*/	u64	prc_status1;
#define VXGE_HW_PRC_STATUS1_PRC_VP_QUIESCENT(n)	vxge_mBIT(n)
/*0x00a08*/	u64	rxdcm_reset_in_progress;
#define VXGE_HW_RXDCM_RESET_IN_PROGRESS_PRC_VP(n)	vxge_mBIT(n)
/*0x00a10*/	u64	replicq_flush_in_progress;
#define VXGE_HW_REPLICQ_FLUSH_IN_PROGRESS_NOA_VP(n)	vxge_mBIT(n)
/*0x00a18*/	u64	rxpe_cmds_reset_in_progress;
#define VXGE_HW_RXPE_CMDS_RESET_IN_PROGRESS_NOA_VP(n)	vxge_mBIT(n)
/*0x00a20*/	u64	mxp_cmds_reset_in_progress;
#define VXGE_HW_MXP_CMDS_RESET_IN_PROGRESS_NOA_VP(n)	vxge_mBIT(n)
/*0x00a28*/	u64	noffload_reset_in_progress;
#define VXGE_HW_NOFFLOAD_RESET_IN_PROGRESS_PRC_VP(n)	vxge_mBIT(n)
/*0x00a30*/	u64	rd_req_in_progress;
#define VXGE_HW_RD_REQ_IN_PROGRESS_VP(n)	vxge_mBIT(n)
/*0x00a38*/	u64	rd_req_outstanding;
#define VXGE_HW_RD_REQ_OUTSTANDING_VP(n)	vxge_mBIT(n)
/*0x00a40*/	u64	kdfc_reset_in_progress;
#define VXGE_HW_KDFC_RESET_IN_PROGRESS_NOA_VP(n)	vxge_mBIT(n)
	u8	unused00b00[0x00b00-0x00a48];

/*0x00b00*/	u64	one_cfg_vp;
#define VXGE_HW_ONE_CFG_VP_RDY(n)	vxge_mBIT(n)
/*0x00b08*/	u64	one_common;
#define VXGE_HW_ONE_COMMON_PET_VPATH_RESET_IN_PROGRESS(n)	vxge_mBIT(n)
	u8	unused00b80[0x00b80-0x00b10];

/*0x00b80*/	u64	tim_int_en;
#define VXGE_HW_TIM_INT_EN_TIM_VP(n)	vxge_mBIT(n)
/*0x00b88*/	u64	tim_set_int_en;
#define VXGE_HW_TIM_SET_INT_EN_VP(n)	vxge_mBIT(n)
/*0x00b90*/	u64	tim_clr_int_en;
#define VXGE_HW_TIM_CLR_INT_EN_VP(n)	vxge_mBIT(n)
/*0x00b98*/	u64	tim_mask_int_during_reset;
#define VXGE_HW_TIM_MASK_INT_DURING_RESET_VPATH(n)	vxge_mBIT(n)
/*0x00ba0*/	u64	tim_reset_in_progress;
#define VXGE_HW_TIM_RESET_IN_PROGRESS_TIM_VPATH(n)	vxge_mBIT(n)
/*0x00ba8*/	u64	tim_outstanding_bmap;
#define VXGE_HW_TIM_OUTSTANDING_BMAP_TIM_VPATH(n)	vxge_mBIT(n)
	u8	unused00c00[0x00c00-0x00bb0];

/*0x00c00*/	u64	msg_reset_in_progress;
#define VXGE_HW_MSG_RESET_IN_PROGRESS_MSG_COMPOSITE(val) vxge_vBIT(val, 0, 17)
/*0x00c08*/	u64	msg_mxp_mr_ready;
#define VXGE_HW_MSG_MXP_MR_READY_MP_BOOTED(n)	vxge_mBIT(n)
/*0x00c10*/	u64	msg_uxp_mr_ready;
#define VXGE_HW_MSG_UXP_MR_READY_UP_BOOTED(n)	vxge_mBIT(n)
/*0x00c18*/	u64	msg_dmq_noni_rtl_prefetch;
#define VXGE_HW_MSG_DMQ_NONI_RTL_PREFETCH_BYPASS_ENABLE(n)	vxge_mBIT(n)
/*0x00c20*/	u64	msg_umq_rtl_bwr;
#define VXGE_HW_MSG_UMQ_RTL_BWR_PREFETCH_DISABLE(n)	vxge_mBIT(n)
	u8	unused00d00[0x00d00-0x00c28];

/*0x00d00*/	u64	cmn_rsthdlr_cfg0;
#define VXGE_HW_CMN_RSTHDLR_CFG0_SW_RESET_VPATH(val) vxge_vBIT(val, 0, 17)
/*0x00d08*/	u64	cmn_rsthdlr_cfg1;
#define VXGE_HW_CMN_RSTHDLR_CFG1_CLR_VPATH_RESET(val) vxge_vBIT(val, 0, 17)
/*0x00d10*/	u64	cmn_rsthdlr_cfg2;
#define VXGE_HW_CMN_RSTHDLR_CFG2_SW_RESET_FIFO0(val) vxge_vBIT(val, 0, 17)
/*0x00d18*/	u64	cmn_rsthdlr_cfg3;
#define VXGE_HW_CMN_RSTHDLR_CFG3_SW_RESET_FIFO1(val) vxge_vBIT(val, 0, 17)
/*0x00d20*/	u64	cmn_rsthdlr_cfg4;
#define VXGE_HW_CMN_RSTHDLR_CFG4_SW_RESET_FIFO2(val) vxge_vBIT(val, 0, 17)
	u8	unused00d40[0x00d40-0x00d28];

/*0x00d40*/	u64	cmn_rsthdlr_cfg8;
#define VXGE_HW_CMN_RSTHDLR_CFG8_INCR_VPATH_INST_NUM(val) vxge_vBIT(val, 0, 17)
/*0x00d48*/	u64	stats_cfg0;
#define VXGE_HW_STATS_CFG0_STATS_ENABLE(val) vxge_vBIT(val, 0, 17)
	u8	unused00da8[0x00da8-0x00d50];

/*0x00da8*/	u64	clear_msix_mask_vect[4];
#define VXGE_HW_CLEAR_MSIX_MASK_VECT_CLEAR_MSIX_MASK_VECT(val) \
						vxge_vBIT(val, 0, 17)
/*0x00dc8*/	u64	set_msix_mask_vect[4];
#define VXGE_HW_SET_MSIX_MASK_VECT_SET_MSIX_MASK_VECT(val) vxge_vBIT(val, 0, 17)
/*0x00de8*/	u64	clear_msix_mask_all_vect;
#define	VXGE_HW_CLEAR_MSIX_MASK_ALL_VECT_CLEAR_MSIX_MASK_ALL_VECT(val)	\
							vxge_vBIT(val, 0, 17)
/*0x00df0*/	u64	set_msix_mask_all_vect;
#define	VXGE_HW_SET_MSIX_MASK_ALL_VECT_SET_MSIX_MASK_ALL_VECT(val) \
							vxge_vBIT(val, 0, 17)
/*0x00df8*/	u64	mask_vector[4];
#define VXGE_HW_MASK_VECTOR_MASK_VECTOR(val) vxge_vBIT(val, 0, 17)
/*0x00e18*/	u64	msix_pending_vector[4];
#define VXGE_HW_MSIX_PENDING_VECTOR_MSIX_PENDING_VECTOR(val) \
							vxge_vBIT(val, 0, 17)
/*0x00e38*/	u64	clr_msix_one_shot_vec[4];
#define	VXGE_HW_CLR_MSIX_ONE_SHOT_VEC_CLR_MSIX_ONE_SHOT_VEC(val) \
							vxge_vBIT(val, 0, 17)
/*0x00e58*/	u64	titan_asic_id;
#define VXGE_HW_TITAN_ASIC_ID_INITIAL_DEVICE_ID(val) vxge_vBIT(val, 0, 16)
#define VXGE_HW_TITAN_ASIC_ID_INITIAL_MAJOR_REVISION(val) vxge_vBIT(val, 48, 8)
#define VXGE_HW_TITAN_ASIC_ID_INITIAL_MINOR_REVISION(val) vxge_vBIT(val, 56, 8)
/*0x00e60*/	u64	titan_general_int_status;
#define	VXGE_HW_TITAN_GENERAL_INT_STATUS_MRPCIM_ALARM_INT	vxge_mBIT(0)
#define	VXGE_HW_TITAN_GENERAL_INT_STATUS_SRPCIM_ALARM_INT	vxge_mBIT(1)
#define	VXGE_HW_TITAN_GENERAL_INT_STATUS_VPATH_ALARM_INT	vxge_mBIT(2)
#define	VXGE_HW_TITAN_GENERAL_INT_STATUS_VPATH_TRAFFIC_INT(val) \
							vxge_vBIT(val, 3, 17)
	u8	unused00e70[0x00e70-0x00e68];

/*0x00e70*/	u64	titan_mask_all_int;
#define	VXGE_HW_TITAN_MASK_ALL_INT_ALARM	vxge_mBIT(7)
#define	VXGE_HW_TITAN_MASK_ALL_INT_TRAFFIC	vxge_mBIT(15)
	u8	unused00e80[0x00e80-0x00e78];

/*0x00e80*/	u64	tim_int_status0;
#define VXGE_HW_TIM_INT_STATUS0_TIM_INT_STATUS0(val) vxge_vBIT(val, 0, 64)
/*0x00e88*/	u64	tim_int_mask0;
#define VXGE_HW_TIM_INT_MASK0_TIM_INT_MASK0(val) vxge_vBIT(val, 0, 64)
/*0x00e90*/	u64	tim_int_status1;
#define VXGE_HW_TIM_INT_STATUS1_TIM_INT_STATUS1(val) vxge_vBIT(val, 0, 4)
/*0x00e98*/	u64	tim_int_mask1;
#define VXGE_HW_TIM_INT_MASK1_TIM_INT_MASK1(val) vxge_vBIT(val, 0, 4)
/*0x00ea0*/	u64	rti_int_status;
#define VXGE_HW_RTI_INT_STATUS_RTI_INT_STATUS(val) vxge_vBIT(val, 0, 17)
/*0x00ea8*/	u64	rti_int_mask;
#define VXGE_HW_RTI_INT_MASK_RTI_INT_MASK(val) vxge_vBIT(val, 0, 17)
/*0x00eb0*/	u64	adapter_status;
#define	VXGE_HW_ADAPTER_STATUS_RTDMA_RTDMA_READY	vxge_mBIT(0)
#define	VXGE_HW_ADAPTER_STATUS_WRDMA_WRDMA_READY	vxge_mBIT(1)
#define	VXGE_HW_ADAPTER_STATUS_KDFC_KDFC_READY	vxge_mBIT(2)
#define	VXGE_HW_ADAPTER_STATUS_TPA_TMAC_BUF_EMPTY	vxge_mBIT(3)
#define	VXGE_HW_ADAPTER_STATUS_RDCTL_PIC_QUIESCENT	vxge_mBIT(4)
#define	VXGE_HW_ADAPTER_STATUS_XGMAC_NETWORK_FAULT	vxge_mBIT(5)
#define	VXGE_HW_ADAPTER_STATUS_ROCRC_OFFLOAD_QUIESCENT	vxge_mBIT(6)
#define	VXGE_HW_ADAPTER_STATUS_G3IF_FB_G3IF_FB_GDDR3_READY	vxge_mBIT(7)
#define	VXGE_HW_ADAPTER_STATUS_G3IF_CM_G3IF_CM_GDDR3_READY	vxge_mBIT(8)
#define	VXGE_HW_ADAPTER_STATUS_RIC_RIC_RUNNING	vxge_mBIT(9)
#define	VXGE_HW_ADAPTER_STATUS_CMG_C_PLL_IN_LOCK	vxge_mBIT(10)
#define	VXGE_HW_ADAPTER_STATUS_XGMAC_X_PLL_IN_LOCK	vxge_mBIT(11)
#define	VXGE_HW_ADAPTER_STATUS_FBIF_M_PLL_IN_LOCK	vxge_mBIT(12)
#define VXGE_HW_ADAPTER_STATUS_PCC_PCC_IDLE(val) vxge_vBIT(val, 24, 8)
#define VXGE_HW_ADAPTER_STATUS_ROCRC_RC_PRC_QUIESCENT(val) vxge_vBIT(val, 44, 8)
/*0x00eb8*/	u64	gen_ctrl;
#define	VXGE_HW_GEN_CTRL_SPI_MRPCIM_WR_DIS	vxge_mBIT(0)
#define	VXGE_HW_GEN_CTRL_SPI_MRPCIM_RD_DIS	vxge_mBIT(1)
#define	VXGE_HW_GEN_CTRL_SPI_SRPCIM_WR_DIS	vxge_mBIT(2)
#define	VXGE_HW_GEN_CTRL_SPI_SRPCIM_RD_DIS	vxge_mBIT(3)
#define	VXGE_HW_GEN_CTRL_SPI_DEBUG_DIS	vxge_mBIT(4)
#define	VXGE_HW_GEN_CTRL_SPI_APP_LTSSM_TIMER_DIS	vxge_mBIT(5)
#define VXGE_HW_GEN_CTRL_SPI_NOT_USED(val) vxge_vBIT(val, 6, 4)
	u8	unused00ed0[0x00ed0-0x00ec0];

/*0x00ed0*/	u64	adapter_ready;
#define	VXGE_HW_ADAPTER_READY_ADAPTER_READY	vxge_mBIT(63)
/*0x00ed8*/	u64	outstanding_read;
#define VXGE_HW_OUTSTANDING_READ_OUTSTANDING_READ(val) vxge_vBIT(val, 0, 17)
/*0x00ee0*/	u64	vpath_rst_in_prog;
#define VXGE_HW_VPATH_RST_IN_PROG_VPATH_RST_IN_PROG(val) vxge_vBIT(val, 0, 17)
/*0x00ee8*/	u64	vpath_reg_modified;
#define VXGE_HW_VPATH_REG_MODIFIED_VPATH_REG_MODIFIED(val) vxge_vBIT(val, 0, 17)
	u8	unused00fc0[0x00fc0-0x00ef0];

/*0x00fc0*/	u64	cp_reset_in_progress;
#define VXGE_HW_CP_RESET_IN_PROGRESS_CP_VPATH(n)	vxge_mBIT(n)
	u8	unused01080[0x01080-0x00fc8];

/*0x01080*/	u64	xgmac_ready;
#define VXGE_HW_XGMAC_READY_XMACJ_READY(val) vxge_vBIT(val, 0, 17)
	u8	unused010c0[0x010c0-0x01088];

/*0x010c0*/	u64	fbif_ready;
#define VXGE_HW_FBIF_READY_FAU_READY(val) vxge_vBIT(val, 0, 17)
	u8	unused01100[0x01100-0x010c8];

/*0x01100*/	u64	vplane_assignments;
#define VXGE_HW_VPLANE_ASSIGNMENTS_VPLANE_ASSIGNMENTS(val) vxge_vBIT(val, 3, 5)
/*0x01108*/	u64	vpath_assignments;
#define VXGE_HW_VPATH_ASSIGNMENTS_VPATH_ASSIGNMENTS(val) vxge_vBIT(val, 0, 17)
/*0x01110*/	u64	resource_assignments;
#define VXGE_HW_RESOURCE_ASSIGNMENTS_RESOURCE_ASSIGNMENTS(val) \
						vxge_vBIT(val, 0, 17)
/*0x01118*/	u64	host_type_assignments;
#define	VXGE_HW_HOST_TYPE_ASSIGNMENTS_HOST_TYPE_ASSIGNMENTS(val) \
							vxge_vBIT(val, 5, 3)
	u8	unused01128[0x01128-0x01120];

/*0x01128*/	u64	max_resource_assignments;
#define VXGE_HW_MAX_RESOURCE_ASSIGNMENTS_PCI_MAX_VPLANE(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_MAX_RESOURCE_ASSIGNMENTS_PCI_MAX_VPATHS(val) \
						vxge_vBIT(val, 11, 5)
/*0x01130*/	u64	pf_vpath_assignments;
#define VXGE_HW_PF_VPATH_ASSIGNMENTS_PF_VPATH_ASSIGNMENTS(val) \
						vxge_vBIT(val, 0, 17)
	u8	unused01200[0x01200-0x01138];

/*0x01200*/	u64	rts_access_icmp;
#define VXGE_HW_RTS_ACCESS_ICMP_EN(val) vxge_vBIT(val, 0, 17)
/*0x01208*/	u64	rts_access_tcpsyn;
#define VXGE_HW_RTS_ACCESS_TCPSYN_EN(val) vxge_vBIT(val, 0, 17)
/*0x01210*/	u64	rts_access_zl4pyld;
#define VXGE_HW_RTS_ACCESS_ZL4PYLD_EN(val) vxge_vBIT(val, 0, 17)
/*0x01218*/	u64	rts_access_l4prtcl_tcp;
#define VXGE_HW_RTS_ACCESS_L4PRTCL_TCP_EN(val) vxge_vBIT(val, 0, 17)
/*0x01220*/	u64	rts_access_l4prtcl_udp;
#define VXGE_HW_RTS_ACCESS_L4PRTCL_UDP_EN(val) vxge_vBIT(val, 0, 17)
/*0x01228*/	u64	rts_access_l4prtcl_flex;
#define VXGE_HW_RTS_ACCESS_L4PRTCL_FLEX_EN(val) vxge_vBIT(val, 0, 17)
/*0x01230*/	u64	rts_access_ipfrag;
#define VXGE_HW_RTS_ACCESS_IPFRAG_EN(val) vxge_vBIT(val, 0, 17)

} __packed;

struct vxge_hw_memrepair_reg {
	u64	unused1;
	u64	unused2;
} __packed;

struct vxge_hw_pcicfgmgmt_reg {

/*0x00000*/	u64	resource_no;
#define	VXGE_HW_RESOURCE_NO_PFN_OR_VF	BIT(3)
/*0x00008*/	u64	bargrp_pf_or_vf_bar0_mask;
#define	VXGE_HW_BARGRP_PF_OR_VF_BAR0_MASK_BARGRP_PF_OR_VF_BAR0_MASK(val) \
							vxge_vBIT(val, 2, 6)
/*0x00010*/	u64	bargrp_pf_or_vf_bar1_mask;
#define	VXGE_HW_BARGRP_PF_OR_VF_BAR1_MASK_BARGRP_PF_OR_VF_BAR1_MASK(val) \
							vxge_vBIT(val, 2, 6)
/*0x00018*/	u64	bargrp_pf_or_vf_bar2_mask;
#define	VXGE_HW_BARGRP_PF_OR_VF_BAR2_MASK_BARGRP_PF_OR_VF_BAR2_MASK(val) \
							vxge_vBIT(val, 2, 6)
/*0x00020*/	u64	msixgrp_no;
#define VXGE_HW_MSIXGRP_NO_TABLE_SIZE(val) vxge_vBIT(val, 5, 11)

} __packed;

struct vxge_hw_mrpcim_reg {
/*0x00000*/	u64	g3fbct_int_status;
#define	VXGE_HW_G3FBCT_INT_STATUS_ERR_G3IF_INT	vxge_mBIT(0)
/*0x00008*/	u64	g3fbct_int_mask;
/*0x00010*/	u64	g3fbct_err_reg;
#define	VXGE_HW_G3FBCT_ERR_REG_G3IF_SM_ERR	vxge_mBIT(4)
#define	VXGE_HW_G3FBCT_ERR_REG_G3IF_GDDR3_DECC	vxge_mBIT(5)
#define	VXGE_HW_G3FBCT_ERR_REG_G3IF_GDDR3_U_DECC	vxge_mBIT(6)
#define	VXGE_HW_G3FBCT_ERR_REG_G3IF_CTRL_FIFO_DECC	vxge_mBIT(7)
#define	VXGE_HW_G3FBCT_ERR_REG_G3IF_GDDR3_SECC	vxge_mBIT(29)
#define	VXGE_HW_G3FBCT_ERR_REG_G3IF_GDDR3_U_SECC	vxge_mBIT(30)
#define	VXGE_HW_G3FBCT_ERR_REG_G3IF_CTRL_FIFO_SECC	vxge_mBIT(31)
/*0x00018*/	u64	g3fbct_err_mask;
/*0x00020*/	u64	g3fbct_err_alarm;

	u8	unused00a00[0x00a00-0x00028];

/*0x00a00*/	u64	wrdma_int_status;
#define	VXGE_HW_WRDMA_INT_STATUS_RC_ALARM_RC_INT	vxge_mBIT(0)
#define	VXGE_HW_WRDMA_INT_STATUS_RXDRM_SM_ERR_RXDRM_INT	vxge_mBIT(1)
#define	VXGE_HW_WRDMA_INT_STATUS_RXDCM_SM_ERR_RXDCM_SM_INT	vxge_mBIT(2)
#define	VXGE_HW_WRDMA_INT_STATUS_RXDWM_SM_ERR_RXDWM_INT	vxge_mBIT(3)
#define	VXGE_HW_WRDMA_INT_STATUS_RDA_ERR_RDA_INT	vxge_mBIT(6)
#define	VXGE_HW_WRDMA_INT_STATUS_RDA_ECC_DB_RDA_ECC_DB_INT	vxge_mBIT(8)
#define	VXGE_HW_WRDMA_INT_STATUS_RDA_ECC_SG_RDA_ECC_SG_INT	vxge_mBIT(9)
#define	VXGE_HW_WRDMA_INT_STATUS_FRF_ALARM_FRF_INT	vxge_mBIT(12)
#define	VXGE_HW_WRDMA_INT_STATUS_ROCRC_ALARM_ROCRC_INT	vxge_mBIT(13)
#define	VXGE_HW_WRDMA_INT_STATUS_WDE0_ALARM_WDE0_INT	vxge_mBIT(14)
#define	VXGE_HW_WRDMA_INT_STATUS_WDE1_ALARM_WDE1_INT	vxge_mBIT(15)
#define	VXGE_HW_WRDMA_INT_STATUS_WDE2_ALARM_WDE2_INT	vxge_mBIT(16)
#define	VXGE_HW_WRDMA_INT_STATUS_WDE3_ALARM_WDE3_INT	vxge_mBIT(17)
/*0x00a08*/	u64	wrdma_int_mask;
/*0x00a10*/	u64	rc_alarm_reg;
#define	VXGE_HW_RC_ALARM_REG_FTC_SM_ERR	vxge_mBIT(0)
#define	VXGE_HW_RC_ALARM_REG_FTC_SM_PHASE_ERR	vxge_mBIT(1)
#define	VXGE_HW_RC_ALARM_REG_BTDWM_SM_ERR	vxge_mBIT(2)
#define	VXGE_HW_RC_ALARM_REG_BTC_SM_ERR	vxge_mBIT(3)
#define	VXGE_HW_RC_ALARM_REG_BTDCM_SM_ERR	vxge_mBIT(4)
#define	VXGE_HW_RC_ALARM_REG_BTDRM_SM_ERR	vxge_mBIT(5)
#define	VXGE_HW_RC_ALARM_REG_RMM_RXD_RC_ECC_DB_ERR	vxge_mBIT(6)
#define	VXGE_HW_RC_ALARM_REG_RMM_RXD_RC_ECC_SG_ERR	vxge_mBIT(7)
#define	VXGE_HW_RC_ALARM_REG_RHS_RXD_RHS_ECC_DB_ERR	vxge_mBIT(8)
#define	VXGE_HW_RC_ALARM_REG_RHS_RXD_RHS_ECC_SG_ERR	vxge_mBIT(9)
#define	VXGE_HW_RC_ALARM_REG_RMM_SM_ERR	vxge_mBIT(10)
#define	VXGE_HW_RC_ALARM_REG_BTC_VPATH_MISMATCH_ERR	vxge_mBIT(12)
/*0x00a18*/	u64	rc_alarm_mask;
/*0x00a20*/	u64	rc_alarm_alarm;
/*0x00a28*/	u64	rxdrm_sm_err_reg;
#define VXGE_HW_RXDRM_SM_ERR_REG_PRC_VP(n)	vxge_mBIT(n)
/*0x00a30*/	u64	rxdrm_sm_err_mask;
/*0x00a38*/	u64	rxdrm_sm_err_alarm;
/*0x00a40*/	u64	rxdcm_sm_err_reg;
#define VXGE_HW_RXDCM_SM_ERR_REG_PRC_VP(n)	vxge_mBIT(n)
/*0x00a48*/	u64	rxdcm_sm_err_mask;
/*0x00a50*/	u64	rxdcm_sm_err_alarm;
/*0x00a58*/	u64	rxdwm_sm_err_reg;
#define VXGE_HW_RXDWM_SM_ERR_REG_PRC_VP(n)	vxge_mBIT(n)
/*0x00a60*/	u64	rxdwm_sm_err_mask;
/*0x00a68*/	u64	rxdwm_sm_err_alarm;
/*0x00a70*/	u64	rda_err_reg;
#define	VXGE_HW_RDA_ERR_REG_RDA_SM0_ERR_ALARM	vxge_mBIT(0)
#define	VXGE_HW_RDA_ERR_REG_RDA_MISC_ERR	vxge_mBIT(1)
#define	VXGE_HW_RDA_ERR_REG_RDA_PCIX_ERR	vxge_mBIT(2)
#define	VXGE_HW_RDA_ERR_REG_RDA_RXD_ECC_DB_ERR	vxge_mBIT(3)
#define	VXGE_HW_RDA_ERR_REG_RDA_FRM_ECC_DB_ERR	vxge_mBIT(4)
#define	VXGE_HW_RDA_ERR_REG_RDA_UQM_ECC_DB_ERR	vxge_mBIT(5)
#define	VXGE_HW_RDA_ERR_REG_RDA_IMM_ECC_DB_ERR	vxge_mBIT(6)
#define	VXGE_HW_RDA_ERR_REG_RDA_TIM_ECC_DB_ERR	vxge_mBIT(7)
/*0x00a78*/	u64	rda_err_mask;
/*0x00a80*/	u64	rda_err_alarm;
/*0x00a88*/	u64	rda_ecc_db_reg;
#define VXGE_HW_RDA_ECC_DB_REG_RDA_RXD_ERR(n)	vxge_mBIT(n)
/*0x00a90*/	u64	rda_ecc_db_mask;
/*0x00a98*/	u64	rda_ecc_db_alarm;
/*0x00aa0*/	u64	rda_ecc_sg_reg;
#define VXGE_HW_RDA_ECC_SG_REG_RDA_RXD_ERR(n)	vxge_mBIT(n)
/*0x00aa8*/	u64	rda_ecc_sg_mask;
/*0x00ab0*/	u64	rda_ecc_sg_alarm;
/*0x00ab8*/	u64	rqa_err_reg;
#define	VXGE_HW_RQA_ERR_REG_RQA_SM_ERR_ALARM	vxge_mBIT(0)
/*0x00ac0*/	u64	rqa_err_mask;
/*0x00ac8*/	u64	rqa_err_alarm;
/*0x00ad0*/	u64	frf_alarm_reg;
#define VXGE_HW_FRF_ALARM_REG_PRC_VP_FRF_SM_ERR(n)	vxge_mBIT(n)
/*0x00ad8*/	u64	frf_alarm_mask;
/*0x00ae0*/	u64	frf_alarm_alarm;
/*0x00ae8*/	u64	rocrc_alarm_reg;
#define	VXGE_HW_ROCRC_ALARM_REG_QCQ_QCC_BYP_ECC_DB	vxge_mBIT(0)
#define	VXGE_HW_ROCRC_ALARM_REG_QCQ_QCC_BYP_ECC_SG	vxge_mBIT(1)
#define	VXGE_HW_ROCRC_ALARM_REG_NOA_NMA_SM_ERR	vxge_mBIT(2)
#define	VXGE_HW_ROCRC_ALARM_REG_NOA_IMMM_ECC_DB	vxge_mBIT(3)
#define	VXGE_HW_ROCRC_ALARM_REG_NOA_IMMM_ECC_SG	vxge_mBIT(4)
#define	VXGE_HW_ROCRC_ALARM_REG_UDQ_UMQM_ECC_DB	vxge_mBIT(5)
#define	VXGE_HW_ROCRC_ALARM_REG_UDQ_UMQM_ECC_SG	vxge_mBIT(6)
#define	VXGE_HW_ROCRC_ALARM_REG_NOA_RCBM_ECC_DB	vxge_mBIT(11)
#define	VXGE_HW_ROCRC_ALARM_REG_NOA_RCBM_ECC_SG	vxge_mBIT(12)
#define	VXGE_HW_ROCRC_ALARM_REG_QCQ_MULTI_EGB_RSVD_ERR	vxge_mBIT(13)
#define	VXGE_HW_ROCRC_ALARM_REG_QCQ_MULTI_EGB_OWN_ERR	vxge_mBIT(14)
#define	VXGE_HW_ROCRC_ALARM_REG_QCQ_MULTI_BYP_OWN_ERR	vxge_mBIT(15)
#define	VXGE_HW_ROCRC_ALARM_REG_QCQ_OWN_NOT_ASSIGNED_ERR	vxge_mBIT(16)
#define	VXGE_HW_ROCRC_ALARM_REG_QCQ_OWN_RSVD_SYNC_ERR	vxge_mBIT(17)
#define	VXGE_HW_ROCRC_ALARM_REG_QCQ_LOST_EGB_ERR	vxge_mBIT(18)
#define	VXGE_HW_ROCRC_ALARM_REG_RCQ_BYPQ0_OVERFLOW	vxge_mBIT(19)
#define	VXGE_HW_ROCRC_ALARM_REG_RCQ_BYPQ1_OVERFLOW	vxge_mBIT(20)
#define	VXGE_HW_ROCRC_ALARM_REG_RCQ_BYPQ2_OVERFLOW	vxge_mBIT(21)
#define	VXGE_HW_ROCRC_ALARM_REG_NOA_WCT_CMD_FIFO_ERR	vxge_mBIT(22)
/*0x00af0*/	u64	rocrc_alarm_mask;
/*0x00af8*/	u64	rocrc_alarm_alarm;
/*0x00b00*/	u64	wde0_alarm_reg;
#define	VXGE_HW_WDE0_ALARM_REG_WDE0_DCC_SM_ERR	vxge_mBIT(0)
#define	VXGE_HW_WDE0_ALARM_REG_WDE0_PRM_SM_ERR	vxge_mBIT(1)
#define	VXGE_HW_WDE0_ALARM_REG_WDE0_CP_SM_ERR	vxge_mBIT(2)
#define	VXGE_HW_WDE0_ALARM_REG_WDE0_CP_CMD_ERR	vxge_mBIT(3)
#define	VXGE_HW_WDE0_ALARM_REG_WDE0_PCR_SM_ERR	vxge_mBIT(4)
/*0x00b08*/	u64	wde0_alarm_mask;
/*0x00b10*/	u64	wde0_alarm_alarm;
/*0x00b18*/	u64	wde1_alarm_reg;
#define	VXGE_HW_WDE1_ALARM_REG_WDE1_DCC_SM_ERR	vxge_mBIT(0)
#define	VXGE_HW_WDE1_ALARM_REG_WDE1_PRM_SM_ERR	vxge_mBIT(1)
#define	VXGE_HW_WDE1_ALARM_REG_WDE1_CP_SM_ERR	vxge_mBIT(2)
#define	VXGE_HW_WDE1_ALARM_REG_WDE1_CP_CMD_ERR	vxge_mBIT(3)
#define	VXGE_HW_WDE1_ALARM_REG_WDE1_PCR_SM_ERR	vxge_mBIT(4)
/*0x00b20*/	u64	wde1_alarm_mask;
/*0x00b28*/	u64	wde1_alarm_alarm;
/*0x00b30*/	u64	wde2_alarm_reg;
#define	VXGE_HW_WDE2_ALARM_REG_WDE2_DCC_SM_ERR	vxge_mBIT(0)
#define	VXGE_HW_WDE2_ALARM_REG_WDE2_PRM_SM_ERR	vxge_mBIT(1)
#define	VXGE_HW_WDE2_ALARM_REG_WDE2_CP_SM_ERR	vxge_mBIT(2)
#define	VXGE_HW_WDE2_ALARM_REG_WDE2_CP_CMD_ERR	vxge_mBIT(3)
#define	VXGE_HW_WDE2_ALARM_REG_WDE2_PCR_SM_ERR	vxge_mBIT(4)
/*0x00b38*/	u64	wde2_alarm_mask;
/*0x00b40*/	u64	wde2_alarm_alarm;
/*0x00b48*/	u64	wde3_alarm_reg;
#define	VXGE_HW_WDE3_ALARM_REG_WDE3_DCC_SM_ERR	vxge_mBIT(0)
#define	VXGE_HW_WDE3_ALARM_REG_WDE3_PRM_SM_ERR	vxge_mBIT(1)
#define	VXGE_HW_WDE3_ALARM_REG_WDE3_CP_SM_ERR	vxge_mBIT(2)
#define	VXGE_HW_WDE3_ALARM_REG_WDE3_CP_CMD_ERR	vxge_mBIT(3)
#define	VXGE_HW_WDE3_ALARM_REG_WDE3_PCR_SM_ERR	vxge_mBIT(4)
/*0x00b50*/	u64	wde3_alarm_mask;
/*0x00b58*/	u64	wde3_alarm_alarm;

	u8	unused00be8[0x00be8-0x00b60];

/*0x00be8*/	u64	rx_w_round_robin_0;
#define VXGE_HW_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_0(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_1(val) vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_2(val) vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_3(val) vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_4(val) vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_5(val) vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_6(val) vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_0_RX_W_PRIORITY_SS_7(val) vxge_vBIT(val, 59, 5)
/*0x00bf0*/	u64	rx_w_round_robin_1;
#define VXGE_HW_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_8(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_9(val) vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_10(val) \
						vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_11(val) \
						vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_12(val) \
						vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_13(val) \
						vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_14(val) \
						vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_1_RX_W_PRIORITY_SS_15(val) \
						vxge_vBIT(val, 59, 5)
/*0x00bf8*/	u64	rx_w_round_robin_2;
#define VXGE_HW_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_16(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_17(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_18(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_19(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_20(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_21(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_22(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_2_RX_W_PRIORITY_SS_23(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c00*/	u64	rx_w_round_robin_3;
#define VXGE_HW_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_24(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_25(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_26(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_27(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_28(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_29(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_30(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_3_RX_W_PRIORITY_SS_31(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c08*/	u64	rx_w_round_robin_4;
#define VXGE_HW_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_32(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_33(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_34(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_35(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_36(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_37(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_38(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_4_RX_W_PRIORITY_SS_39(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c10*/	u64	rx_w_round_robin_5;
#define VXGE_HW_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_40(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_41(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_42(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_43(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_44(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_45(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_46(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_5_RX_W_PRIORITY_SS_47(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c18*/	u64	rx_w_round_robin_6;
#define VXGE_HW_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_48(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_49(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_50(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_51(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_52(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_53(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_54(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_6_RX_W_PRIORITY_SS_55(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c20*/	u64	rx_w_round_robin_7;
#define VXGE_HW_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_56(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_57(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_58(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_59(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_60(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_61(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_62(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_7_RX_W_PRIORITY_SS_63(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c28*/	u64	rx_w_round_robin_8;
#define VXGE_HW_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_64(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_65(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_66(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_67(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_68(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_69(val) \
						vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_70(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_8_RX_W_PRIORITY_SS_71(val) \
						vxge_vBIT(val, 59, 5)
/*0x00c30*/	u64	rx_w_round_robin_9;
#define VXGE_HW_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_72(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_73(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_74(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_75(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_76(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_77(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_78(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_9_RX_W_PRIORITY_SS_79(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c38*/	u64	rx_w_round_robin_10;
#define VXGE_HW_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_80(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_81(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_82(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_83(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_84(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_85(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_86(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_10_RX_W_PRIORITY_SS_87(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c40*/	u64	rx_w_round_robin_11;
#define VXGE_HW_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_88(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_89(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_90(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_91(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_92(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_93(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_94(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_11_RX_W_PRIORITY_SS_95(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c48*/	u64	rx_w_round_robin_12;
#define VXGE_HW_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_96(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_97(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_98(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_99(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_100(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_101(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_102(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_12_RX_W_PRIORITY_SS_103(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c50*/	u64	rx_w_round_robin_13;
#define VXGE_HW_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_104(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_105(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_106(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_107(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_108(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_109(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_110(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_13_RX_W_PRIORITY_SS_111(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c58*/	u64	rx_w_round_robin_14;
#define VXGE_HW_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_112(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_113(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_114(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_115(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_116(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_117(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_118(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_14_RX_W_PRIORITY_SS_119(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c60*/	u64	rx_w_round_robin_15;
#define VXGE_HW_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_120(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_121(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_122(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_123(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_124(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_125(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_126(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_15_RX_W_PRIORITY_SS_127(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c68*/	u64	rx_w_round_robin_16;
#define VXGE_HW_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_128(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_129(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_130(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_131(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_132(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_133(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_134(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_16_RX_W_PRIORITY_SS_135(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c70*/	u64	rx_w_round_robin_17;
#define VXGE_HW_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_136(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_137(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_138(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_139(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_140(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_141(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_142(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_17_RX_W_PRIORITY_SS_143(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c78*/	u64	rx_w_round_robin_18;
#define VXGE_HW_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_144(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_145(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_146(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_147(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_148(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_149(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_150(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_18_RX_W_PRIORITY_SS_151(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c80*/	u64	rx_w_round_robin_19;
#define VXGE_HW_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_152(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_153(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_154(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_155(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_156(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_157(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_158(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_19_RX_W_PRIORITY_SS_159(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c88*/	u64	rx_w_round_robin_20;
#define VXGE_HW_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_160(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_161(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_162(val) \
							vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_163(val) \
							vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_164(val) \
							vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_165(val) \
							vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_166(val) \
							vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_20_RX_W_PRIORITY_SS_167(val) \
							vxge_vBIT(val, 59, 5)
/*0x00c90*/	u64	rx_w_round_robin_21;
#define VXGE_HW_RX_W_ROUND_ROBIN_21_RX_W_PRIORITY_SS_168(val) \
							vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_21_RX_W_PRIORITY_SS_169(val) \
							vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_W_ROUND_ROBIN_21_RX_W_PRIORITY_SS_170(val) \
							vxge_vBIT(val, 19, 5)

#define VXGE_HW_WRR_RING_SERVICE_STATES			171
#define VXGE_HW_WRR_RING_COUNT				22

/*0x00c98*/	u64	rx_queue_priority_0;
#define VXGE_HW_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_0(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_1(val) vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_2(val) vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_3(val) vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_4(val) vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_5(val) vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_6(val) vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_0_RX_Q_NUMBER_7(val) vxge_vBIT(val, 59, 5)
/*0x00ca0*/	u64	rx_queue_priority_1;
#define VXGE_HW_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_8(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_9(val) vxge_vBIT(val, 11, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_10(val) vxge_vBIT(val, 19, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_11(val) vxge_vBIT(val, 27, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_12(val) vxge_vBIT(val, 35, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_13(val) vxge_vBIT(val, 43, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_14(val) vxge_vBIT(val, 51, 5)
#define VXGE_HW_RX_QUEUE_PRIORITY_1_RX_Q_NUMBER_15(val) vxge_vBIT(val, 59, 5)
/*0x00ca8*/	u64	rx_queue_priority_2;
#define VXGE_HW_RX_QUEUE_PRIORITY_2_RX_Q_NUMBER_16(val) vxge_vBIT(val, 3, 5)
	u8	unused00cc8[0x00cc8-0x00cb0];

/*0x00cc8*/	u64	replication_queue_priority;
#define	VXGE_HW_REPLICATION_QUEUE_PRIORITY_REPLICATION_QUEUE_PRIORITY(val) \
							vxge_vBIT(val, 59, 5)
/*0x00cd0*/	u64	rx_queue_select;
#define VXGE_HW_RX_QUEUE_SELECT_NUMBER(n)	vxge_mBIT(n)
#define	VXGE_HW_RX_QUEUE_SELECT_ENABLE_CODE	vxge_mBIT(15)
#define	VXGE_HW_RX_QUEUE_SELECT_ENABLE_HIERARCHICAL_PRTY	vxge_mBIT(23)
/*0x00cd8*/	u64	rqa_vpbp_ctrl;
#define	VXGE_HW_RQA_VPBP_CTRL_WR_XON_DIS	vxge_mBIT(15)
#define	VXGE_HW_RQA_VPBP_CTRL_ROCRC_DIS	vxge_mBIT(23)
#define	VXGE_HW_RQA_VPBP_CTRL_TXPE_DIS	vxge_mBIT(31)
/*0x00ce0*/	u64	rx_multi_cast_ctrl;
#define	VXGE_HW_RX_MULTI_CAST_CTRL_TIME_OUT_DIS	vxge_mBIT(0)
#define	VXGE_HW_RX_MULTI_CAST_CTRL_FRM_DROP_DIS	vxge_mBIT(1)
#define VXGE_HW_RX_MULTI_CAST_CTRL_NO_RXD_TIME_OUT_CNT(val) \
							vxge_vBIT(val, 2, 30)
#define VXGE_HW_RX_MULTI_CAST_CTRL_TIME_OUT_CNT(val) vxge_vBIT(val, 32, 32)
/*0x00ce8*/	u64	wde_prm_ctrl;
#define VXGE_HW_WDE_PRM_CTRL_SPAV_THRESHOLD(val) vxge_vBIT(val, 2, 10)
#define VXGE_HW_WDE_PRM_CTRL_SPLIT_THRESHOLD(val) vxge_vBIT(val, 18, 14)
#define	VXGE_HW_WDE_PRM_CTRL_SPLIT_ON_1ST_ROW	vxge_mBIT(32)
#define	VXGE_HW_WDE_PRM_CTRL_SPLIT_ON_ROW_BNDRY	vxge_mBIT(33)
#define VXGE_HW_WDE_PRM_CTRL_FB_ROW_SIZE(val) vxge_vBIT(val, 46, 2)
/*0x00cf0*/	u64	noa_ctrl;
#define VXGE_HW_NOA_CTRL_FRM_PRTY_QUOTA(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_NOA_CTRL_NON_FRM_PRTY_QUOTA(val) vxge_vBIT(val, 11, 5)
#define	VXGE_HW_NOA_CTRL_IGNORE_KDFC_IF_STATUS	vxge_mBIT(16)
#define VXGE_HW_NOA_CTRL_MAX_JOB_CNT_FOR_WDE0(val) vxge_vBIT(val, 37, 4)
#define VXGE_HW_NOA_CTRL_MAX_JOB_CNT_FOR_WDE1(val) vxge_vBIT(val, 45, 4)
#define VXGE_HW_NOA_CTRL_MAX_JOB_CNT_FOR_WDE2(val) vxge_vBIT(val, 53, 4)
#define VXGE_HW_NOA_CTRL_MAX_JOB_CNT_FOR_WDE3(val) vxge_vBIT(val, 60, 4)
/*0x00cf8*/	u64	phase_cfg;
#define	VXGE_HW_PHASE_CFG_QCC_WR_PHASE_EN	vxge_mBIT(0)
#define	VXGE_HW_PHASE_CFG_QCC_RD_PHASE_EN	vxge_mBIT(3)
#define	VXGE_HW_PHASE_CFG_IMMM_WR_PHASE_EN	vxge_mBIT(7)
#define	VXGE_HW_PHASE_CFG_IMMM_RD_PHASE_EN	vxge_mBIT(11)
#define	VXGE_HW_PHASE_CFG_UMQM_WR_PHASE_EN	vxge_mBIT(15)
#define	VXGE_HW_PHASE_CFG_UMQM_RD_PHASE_EN	vxge_mBIT(19)
#define	VXGE_HW_PHASE_CFG_RCBM_WR_PHASE_EN	vxge_mBIT(23)
#define	VXGE_HW_PHASE_CFG_RCBM_RD_PHASE_EN	vxge_mBIT(27)
#define	VXGE_HW_PHASE_CFG_RXD_RC_WR_PHASE_EN	vxge_mBIT(31)
#define	VXGE_HW_PHASE_CFG_RXD_RC_RD_PHASE_EN	vxge_mBIT(35)
#define	VXGE_HW_PHASE_CFG_RXD_RHS_WR_PHASE_EN	vxge_mBIT(39)
#define	VXGE_HW_PHASE_CFG_RXD_RHS_RD_PHASE_EN	vxge_mBIT(43)
/*0x00d00*/	u64	rcq_bypq_cfg;
#define VXGE_HW_RCQ_BYPQ_CFG_OVERFLOW_THRESHOLD(val) vxge_vBIT(val, 10, 22)
#define VXGE_HW_RCQ_BYPQ_CFG_BYP_ON_THRESHOLD(val) vxge_vBIT(val, 39, 9)
#define VXGE_HW_RCQ_BYPQ_CFG_BYP_OFF_THRESHOLD(val) vxge_vBIT(val, 55, 9)
	u8	unused00e00[0x00e00-0x00d08];

/*0x00e00*/	u64	doorbell_int_status;
#define	VXGE_HW_DOORBELL_INT_STATUS_KDFC_ERR_REG_TXDMA_KDFC_INT	vxge_mBIT(7)
#define	VXGE_HW_DOORBELL_INT_STATUS_USDC_ERR_REG_TXDMA_USDC_INT	vxge_mBIT(15)
/*0x00e08*/	u64	doorbell_int_mask;
/*0x00e10*/	u64	kdfc_err_reg;
#define	VXGE_HW_KDFC_ERR_REG_KDFC_KDFC_ECC_SG_ERR	vxge_mBIT(7)
#define	VXGE_HW_KDFC_ERR_REG_KDFC_KDFC_ECC_DB_ERR	vxge_mBIT(15)
#define	VXGE_HW_KDFC_ERR_REG_KDFC_KDFC_SM_ERR_ALARM	vxge_mBIT(23)
#define	VXGE_HW_KDFC_ERR_REG_KDFC_KDFC_MISC_ERR_1	vxge_mBIT(32)
#define	VXGE_HW_KDFC_ERR_REG_KDFC_KDFC_PCIX_ERR	vxge_mBIT(39)
/*0x00e18*/	u64	kdfc_err_mask;
/*0x00e20*/	u64	kdfc_err_reg_alarm;
#define	VXGE_HW_KDFC_ERR_REG_ALARM_KDFC_KDFC_ECC_SG_ERR	vxge_mBIT(7)
#define	VXGE_HW_KDFC_ERR_REG_ALARM_KDFC_KDFC_ECC_DB_ERR	vxge_mBIT(15)
#define	VXGE_HW_KDFC_ERR_REG_ALARM_KDFC_KDFC_SM_ERR_ALARM	vxge_mBIT(23)
#define	VXGE_HW_KDFC_ERR_REG_ALARM_KDFC_KDFC_MISC_ERR_1	vxge_mBIT(32)
#define	VXGE_HW_KDFC_ERR_REG_ALARM_KDFC_KDFC_PCIX_ERR	vxge_mBIT(39)
	u8	unused00e40[0x00e40-0x00e28];
/*0x00e40*/	u64	kdfc_vp_partition_0;
#define	VXGE_HW_KDFC_VP_PARTITION_0_ENABLE	vxge_mBIT(0)
#define VXGE_HW_KDFC_VP_PARTITION_0_NUMBER_0(val) vxge_vBIT(val, 5, 3)
#define VXGE_HW_KDFC_VP_PARTITION_0_LENGTH_0(val) vxge_vBIT(val, 17, 15)
#define VXGE_HW_KDFC_VP_PARTITION_0_NUMBER_1(val) vxge_vBIT(val, 37, 3)
#define VXGE_HW_KDFC_VP_PARTITION_0_LENGTH_1(val) vxge_vBIT(val, 49, 15)
/*0x00e48*/	u64	kdfc_vp_partition_1;
#define VXGE_HW_KDFC_VP_PARTITION_1_NUMBER_2(val) vxge_vBIT(val, 5, 3)
#define VXGE_HW_KDFC_VP_PARTITION_1_LENGTH_2(val) vxge_vBIT(val, 17, 15)
#define VXGE_HW_KDFC_VP_PARTITION_1_NUMBER_3(val) vxge_vBIT(val, 37, 3)
#define VXGE_HW_KDFC_VP_PARTITION_1_LENGTH_3(val) vxge_vBIT(val, 49, 15)
/*0x00e50*/	u64	kdfc_vp_partition_2;
#define VXGE_HW_KDFC_VP_PARTITION_2_NUMBER_4(val) vxge_vBIT(val, 5, 3)
#define VXGE_HW_KDFC_VP_PARTITION_2_LENGTH_4(val) vxge_vBIT(val, 17, 15)
#define VXGE_HW_KDFC_VP_PARTITION_2_NUMBER_5(val) vxge_vBIT(val, 37, 3)
#define VXGE_HW_KDFC_VP_PARTITION_2_LENGTH_5(val) vxge_vBIT(val, 49, 15)
/*0x00e58*/	u64	kdfc_vp_partition_3;
#define VXGE_HW_KDFC_VP_PARTITION_3_NUMBER_6(val) vxge_vBIT(val, 5, 3)
#define VXGE_HW_KDFC_VP_PARTITION_3_LENGTH_6(val) vxge_vBIT(val, 17, 15)
#define VXGE_HW_KDFC_VP_PARTITION_3_NUMBER_7(val) vxge_vBIT(val, 37, 3)
#define VXGE_HW_KDFC_VP_PARTITION_3_LENGTH_7(val) vxge_vBIT(val, 49, 15)
/*0x00e60*/	u64	kdfc_vp_partition_4;
#define VXGE_HW_KDFC_VP_PARTITION_4_LENGTH_8(val) vxge_vBIT(val, 17, 15)
#define VXGE_HW_KDFC_VP_PARTITION_4_LENGTH_9(val) vxge_vBIT(val, 49, 15)
/*0x00e68*/	u64	kdfc_vp_partition_5;
#define VXGE_HW_KDFC_VP_PARTITION_5_LENGTH_10(val) vxge_vBIT(val, 17, 15)
#define VXGE_HW_KDFC_VP_PARTITION_5_LENGTH_11(val) vxge_vBIT(val, 49, 15)
/*0x00e70*/	u64	kdfc_vp_partition_6;
#define VXGE_HW_KDFC_VP_PARTITION_6_LENGTH_12(val) vxge_vBIT(val, 17, 15)
#define VXGE_HW_KDFC_VP_PARTITION_6_LENGTH_13(val) vxge_vBIT(val, 49, 15)
/*0x00e78*/	u64	kdfc_vp_partition_7;
#define VXGE_HW_KDFC_VP_PARTITION_7_LENGTH_14(val) vxge_vBIT(val, 17, 15)
#define VXGE_HW_KDFC_VP_PARTITION_7_LENGTH_15(val) vxge_vBIT(val, 49, 15)
/*0x00e80*/	u64	kdfc_vp_partition_8;
#define VXGE_HW_KDFC_VP_PARTITION_8_LENGTH_16(val) vxge_vBIT(val, 17, 15)
/*0x00e88*/	u64	kdfc_w_round_robin_0;
#define VXGE_HW_KDFC_W_ROUND_ROBIN_0_NUMBER_0(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_0_NUMBER_1(val) vxge_vBIT(val, 11, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_0_NUMBER_2(val) vxge_vBIT(val, 19, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_0_NUMBER_3(val) vxge_vBIT(val, 27, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_0_NUMBER_4(val) vxge_vBIT(val, 35, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_0_NUMBER_5(val) vxge_vBIT(val, 43, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_0_NUMBER_6(val) vxge_vBIT(val, 51, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_0_NUMBER_7(val) vxge_vBIT(val, 59, 5)

	u8	unused0f28[0x0f28-0x0e90];

/*0x00f28*/	u64	kdfc_w_round_robin_20;
#define VXGE_HW_KDFC_W_ROUND_ROBIN_20_NUMBER_0(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_20_NUMBER_1(val) vxge_vBIT(val, 11, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_20_NUMBER_2(val) vxge_vBIT(val, 19, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_20_NUMBER_3(val) vxge_vBIT(val, 27, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_20_NUMBER_4(val) vxge_vBIT(val, 35, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_20_NUMBER_5(val) vxge_vBIT(val, 43, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_20_NUMBER_6(val) vxge_vBIT(val, 51, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_20_NUMBER_7(val) vxge_vBIT(val, 59, 5)

#define VXGE_HW_WRR_FIFO_COUNT				20

	u8	unused0fc8[0x0fc8-0x0f30];

/*0x00fc8*/	u64	kdfc_w_round_robin_40;
#define VXGE_HW_KDFC_W_ROUND_ROBIN_40_NUMBER_0(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_40_NUMBER_1(val) vxge_vBIT(val, 11, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_40_NUMBER_2(val) vxge_vBIT(val, 19, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_40_NUMBER_3(val) vxge_vBIT(val, 27, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_40_NUMBER_4(val) vxge_vBIT(val, 35, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_40_NUMBER_5(val) vxge_vBIT(val, 43, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_40_NUMBER_6(val) vxge_vBIT(val, 51, 5)
#define VXGE_HW_KDFC_W_ROUND_ROBIN_40_NUMBER_7(val) vxge_vBIT(val, 59, 5)

	u8	unused1068[0x01068-0x0fd0];

/*0x01068*/	u64	kdfc_entry_type_sel_0;
#define VXGE_HW_KDFC_ENTRY_TYPE_SEL_0_NUMBER_0(val) vxge_vBIT(val, 6, 2)
#define VXGE_HW_KDFC_ENTRY_TYPE_SEL_0_NUMBER_1(val) vxge_vBIT(val, 14, 2)
#define VXGE_HW_KDFC_ENTRY_TYPE_SEL_0_NUMBER_2(val) vxge_vBIT(val, 22, 2)
#define VXGE_HW_KDFC_ENTRY_TYPE_SEL_0_NUMBER_3(val) vxge_vBIT(val, 30, 2)
#define VXGE_HW_KDFC_ENTRY_TYPE_SEL_0_NUMBER_4(val) vxge_vBIT(val, 38, 2)
#define VXGE_HW_KDFC_ENTRY_TYPE_SEL_0_NUMBER_5(val) vxge_vBIT(val, 46, 2)
#define VXGE_HW_KDFC_ENTRY_TYPE_SEL_0_NUMBER_6(val) vxge_vBIT(val, 54, 2)
#define VXGE_HW_KDFC_ENTRY_TYPE_SEL_0_NUMBER_7(val) vxge_vBIT(val, 62, 2)
/*0x01070*/	u64	kdfc_entry_type_sel_1;
#define VXGE_HW_KDFC_ENTRY_TYPE_SEL_1_NUMBER_8(val) vxge_vBIT(val, 6, 2)
/*0x01078*/	u64	kdfc_fifo_0_ctrl;
#define VXGE_HW_KDFC_FIFO_0_CTRL_WRR_NUMBER(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_WEIGHTED_RR_SERVICE_STATES		176
#define VXGE_HW_WRR_FIFO_SERVICE_STATES			153

	u8	unused1100[0x01100-0x1080];

/*0x01100*/	u64	kdfc_fifo_17_ctrl;
#define VXGE_HW_KDFC_FIFO_17_CTRL_WRR_NUMBER(val) vxge_vBIT(val, 3, 5)

	u8	unused1600[0x01600-0x1108];

/*0x01600*/	u64	rxmac_int_status;
#define	VXGE_HW_RXMAC_INT_STATUS_RXMAC_GEN_ERR_RXMAC_GEN_INT	vxge_mBIT(3)
#define	VXGE_HW_RXMAC_INT_STATUS_RXMAC_ECC_ERR_RXMAC_ECC_INT	vxge_mBIT(7)
#define	VXGE_HW_RXMAC_INT_STATUS_RXMAC_VARIOUS_ERR_RXMAC_VARIOUS_INT \
								vxge_mBIT(11)
/*0x01608*/	u64	rxmac_int_mask;
	u8	unused01618[0x01618-0x01610];

/*0x01618*/	u64	rxmac_gen_err_reg;
/*0x01620*/	u64	rxmac_gen_err_mask;
/*0x01628*/	u64	rxmac_gen_err_alarm;
/*0x01630*/	u64	rxmac_ecc_err_reg;
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RMAC_PORT0_RMAC_RTS_PART_SG_ERR(val) \
							vxge_vBIT(val, 0, 4)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RMAC_PORT0_RMAC_RTS_PART_DB_ERR(val) \
							vxge_vBIT(val, 4, 4)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RMAC_PORT1_RMAC_RTS_PART_SG_ERR(val) \
							vxge_vBIT(val, 8, 4)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RMAC_PORT1_RMAC_RTS_PART_DB_ERR(val) \
							vxge_vBIT(val, 12, 4)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RMAC_PORT2_RMAC_RTS_PART_SG_ERR(val) \
							vxge_vBIT(val, 16, 4)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RMAC_PORT2_RMAC_RTS_PART_DB_ERR(val) \
							vxge_vBIT(val, 20, 4)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DA_LKP_PRT0_SG_ERR(val) \
							vxge_vBIT(val, 24, 2)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DA_LKP_PRT0_DB_ERR(val) \
							vxge_vBIT(val, 26, 2)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DA_LKP_PRT1_SG_ERR(val) \
							vxge_vBIT(val, 28, 2)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DA_LKP_PRT1_DB_ERR(val) \
							vxge_vBIT(val, 30, 2)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_VID_LKP_SG_ERR	vxge_mBIT(32)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_VID_LKP_DB_ERR	vxge_mBIT(33)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT0_SG_ERR	vxge_mBIT(34)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT0_DB_ERR	vxge_mBIT(35)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT1_SG_ERR	vxge_mBIT(36)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT1_DB_ERR	vxge_mBIT(37)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT2_SG_ERR	vxge_mBIT(38)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_PN_LKP_PRT2_DB_ERR	vxge_mBIT(39)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_RTH_MASK_SG_ERR(val) \
							vxge_vBIT(val, 40, 7)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_RTH_MASK_DB_ERR(val) \
							vxge_vBIT(val, 47, 7)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_RTH_LKP_SG_ERR(val) \
							vxge_vBIT(val, 54, 3)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_RTH_LKP_DB_ERR(val) \
							vxge_vBIT(val, 57, 3)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DS_LKP_SG_ERR \
							vxge_mBIT(60)
#define	VXGE_HW_RXMAC_ECC_ERR_REG_RTSJ_RMAC_DS_LKP_DB_ERR \
							vxge_mBIT(61)
/*0x01638*/	u64	rxmac_ecc_err_mask;
/*0x01640*/	u64	rxmac_ecc_err_alarm;
/*0x01648*/	u64	rxmac_various_err_reg;
#define	VXGE_HW_RXMAC_VARIOUS_ERR_REG_RMAC_RMAC_PORT0_FSM_ERR	vxge_mBIT(0)
#define	VXGE_HW_RXMAC_VARIOUS_ERR_REG_RMAC_RMAC_PORT1_FSM_ERR	vxge_mBIT(1)
#define	VXGE_HW_RXMAC_VARIOUS_ERR_REG_RMAC_RMAC_PORT2_FSM_ERR	vxge_mBIT(2)
#define	VXGE_HW_RXMAC_VARIOUS_ERR_REG_RMACJ_RMACJ_FSM_ERR	vxge_mBIT(3)
/*0x01650*/	u64	rxmac_various_err_mask;
/*0x01658*/	u64	rxmac_various_err_alarm;
/*0x01660*/	u64	rxmac_gen_cfg;
#define	VXGE_HW_RXMAC_GEN_CFG_SCALE_RMAC_UTIL	vxge_mBIT(11)
/*0x01668*/	u64	rxmac_authorize_all_addr;
#define VXGE_HW_RXMAC_AUTHORIZE_ALL_ADDR_VP(n)	vxge_mBIT(n)
/*0x01670*/	u64	rxmac_authorize_all_vid;
#define VXGE_HW_RXMAC_AUTHORIZE_ALL_VID_VP(n)	vxge_mBIT(n)
	u8	unused016c0[0x016c0-0x01678];

/*0x016c0*/	u64	rxmac_red_rate_repl_queue;
#define VXGE_HW_RXMAC_RED_RATE_REPL_QUEUE_CRATE_THR0(val) vxge_vBIT(val, 0, 4)
#define VXGE_HW_RXMAC_RED_RATE_REPL_QUEUE_CRATE_THR1(val) vxge_vBIT(val, 4, 4)
#define VXGE_HW_RXMAC_RED_RATE_REPL_QUEUE_CRATE_THR2(val) vxge_vBIT(val, 8, 4)
#define VXGE_HW_RXMAC_RED_RATE_REPL_QUEUE_CRATE_THR3(val) vxge_vBIT(val, 12, 4)
#define VXGE_HW_RXMAC_RED_RATE_REPL_QUEUE_FRATE_THR0(val) vxge_vBIT(val, 16, 4)
#define VXGE_HW_RXMAC_RED_RATE_REPL_QUEUE_FRATE_THR1(val) vxge_vBIT(val, 20, 4)
#define VXGE_HW_RXMAC_RED_RATE_REPL_QUEUE_FRATE_THR2(val) vxge_vBIT(val, 24, 4)
#define VXGE_HW_RXMAC_RED_RATE_REPL_QUEUE_FRATE_THR3(val) vxge_vBIT(val, 28, 4)
#define	VXGE_HW_RXMAC_RED_RATE_REPL_QUEUE_TRICKLE_EN	vxge_mBIT(35)
	u8	unused016e0[0x016e0-0x016c8];

/*0x016e0*/	u64	rxmac_cfg0_port[3];
#define	VXGE_HW_RXMAC_CFG0_PORT_RMAC_EN	vxge_mBIT(3)
#define	VXGE_HW_RXMAC_CFG0_PORT_STRIP_FCS	vxge_mBIT(7)
#define	VXGE_HW_RXMAC_CFG0_PORT_DISCARD_PFRM	vxge_mBIT(11)
#define	VXGE_HW_RXMAC_CFG0_PORT_IGNORE_FCS_ERR	vxge_mBIT(15)
#define	VXGE_HW_RXMAC_CFG0_PORT_IGNORE_LONG_ERR	vxge_mBIT(19)
#define	VXGE_HW_RXMAC_CFG0_PORT_IGNORE_USIZED_ERR	vxge_mBIT(23)
#define	VXGE_HW_RXMAC_CFG0_PORT_IGNORE_LEN_MISMATCH	vxge_mBIT(27)
#define VXGE_HW_RXMAC_CFG0_PORT_MAX_PYLD_LEN(val) vxge_vBIT(val, 50, 14)
	u8	unused01710[0x01710-0x016f8];

/*0x01710*/	u64	rxmac_cfg2_port[3];
#define	VXGE_HW_RXMAC_CFG2_PORT_PROM_EN	vxge_mBIT(3)
/*0x01728*/	u64	rxmac_pause_cfg_port[3];
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_GEN_EN	vxge_mBIT(3)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_RCV_EN	vxge_mBIT(7)
#define VXGE_HW_RXMAC_PAUSE_CFG_PORT_ACCEL_SEND(val) vxge_vBIT(val, 9, 3)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_DUAL_THR	vxge_mBIT(15)
#define VXGE_HW_RXMAC_PAUSE_CFG_PORT_HIGH_PTIME(val) vxge_vBIT(val, 20, 16)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_IGNORE_PF_FCS_ERR	vxge_mBIT(39)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_IGNORE_PF_LEN_ERR	vxge_mBIT(43)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_LIMITER_EN	vxge_mBIT(47)
#define VXGE_HW_RXMAC_PAUSE_CFG_PORT_MAX_LIMIT(val) vxge_vBIT(val, 48, 8)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_PERMIT_RATEMGMT_CTRL	vxge_mBIT(59)
	u8	unused01758[0x01758-0x01740];

/*0x01758*/	u64	rxmac_red_cfg0_port[3];
#define VXGE_HW_RXMAC_RED_CFG0_PORT_RED_EN_VP(n)	vxge_mBIT(n)
/*0x01770*/	u64	rxmac_red_cfg1_port[3];
#define	VXGE_HW_RXMAC_RED_CFG1_PORT_FINE_EN	vxge_mBIT(3)
#define	VXGE_HW_RXMAC_RED_CFG1_PORT_RED_EN_REPL_QUEUE	vxge_mBIT(11)
/*0x01788*/	u64	rxmac_red_cfg2_port[3];
#define VXGE_HW_RXMAC_RED_CFG2_PORT_TRICKLE_EN_VP(n)	vxge_mBIT(n)
/*0x017a0*/	u64	rxmac_link_util_port[3];
#define	VXGE_HW_RXMAC_LINK_UTIL_PORT_RMAC_RMAC_UTILIZATION(val) \
							vxge_vBIT(val, 1, 7)
#define VXGE_HW_RXMAC_LINK_UTIL_PORT_RMAC_UTIL_CFG(val) vxge_vBIT(val, 8, 4)
#define VXGE_HW_RXMAC_LINK_UTIL_PORT_RMAC_RMAC_FRAC_UTIL(val) \
							vxge_vBIT(val, 12, 4)
#define VXGE_HW_RXMAC_LINK_UTIL_PORT_RMAC_PKT_WEIGHT(val) vxge_vBIT(val, 16, 4)
#define	VXGE_HW_RXMAC_LINK_UTIL_PORT_RMAC_RMAC_SCALE_FACTOR	vxge_mBIT(23)
	u8	unused017d0[0x017d0-0x017b8];

/*0x017d0*/	u64	rxmac_status_port[3];
#define	VXGE_HW_RXMAC_STATUS_PORT_RMAC_RX_FRM_RCVD	vxge_mBIT(3)
	u8	unused01800[0x01800-0x017e8];

/*0x01800*/	u64	rxmac_rx_pa_cfg0;
#define	VXGE_HW_RXMAC_RX_PA_CFG0_IGNORE_FRAME_ERR	vxge_mBIT(3)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_SUPPORT_SNAP_AB_N	vxge_mBIT(7)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_SEARCH_FOR_HAO	vxge_mBIT(18)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_SUPPORT_MOBILE_IPV6_HDRS	vxge_mBIT(19)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_IPV6_STOP_SEARCHING	vxge_mBIT(23)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_NO_PS_IF_UNKNOWN	vxge_mBIT(27)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_SEARCH_FOR_ETYPE	vxge_mBIT(35)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_TOSS_ANY_FRM_IF_L3_CSUM_ERR	vxge_mBIT(39)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_TOSS_OFFLD_FRM_IF_L3_CSUM_ERR	vxge_mBIT(43)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_TOSS_ANY_FRM_IF_L4_CSUM_ERR	vxge_mBIT(47)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_TOSS_OFFLD_FRM_IF_L4_CSUM_ERR	vxge_mBIT(51)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_TOSS_ANY_FRM_IF_RPA_ERR	vxge_mBIT(55)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_TOSS_OFFLD_FRM_IF_RPA_ERR	vxge_mBIT(59)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_JUMBO_SNAP_EN	vxge_mBIT(63)
/*0x01808*/	u64	rxmac_rx_pa_cfg1;
#define	VXGE_HW_RXMAC_RX_PA_CFG1_REPL_IPV4_TCP_INCL_PH	vxge_mBIT(3)
#define	VXGE_HW_RXMAC_RX_PA_CFG1_REPL_IPV6_TCP_INCL_PH	vxge_mBIT(7)
#define	VXGE_HW_RXMAC_RX_PA_CFG1_REPL_IPV4_UDP_INCL_PH	vxge_mBIT(11)
#define	VXGE_HW_RXMAC_RX_PA_CFG1_REPL_IPV6_UDP_INCL_PH	vxge_mBIT(15)
#define	VXGE_HW_RXMAC_RX_PA_CFG1_REPL_L4_INCL_CF	vxge_mBIT(19)
#define	VXGE_HW_RXMAC_RX_PA_CFG1_REPL_STRIP_VLAN_TAG	vxge_mBIT(23)
	u8	unused01828[0x01828-0x01810];

/*0x01828*/	u64	rts_mgr_cfg0;
#define	VXGE_HW_RTS_MGR_CFG0_RTS_DP_SP_PRIORITY	vxge_mBIT(3)
#define VXGE_HW_RTS_MGR_CFG0_FLEX_L4PRTCL_VALUE(val) vxge_vBIT(val, 24, 8)
#define	VXGE_HW_RTS_MGR_CFG0_ICMP_TRASH	vxge_mBIT(35)
#define	VXGE_HW_RTS_MGR_CFG0_TCPSYN_TRASH	vxge_mBIT(39)
#define	VXGE_HW_RTS_MGR_CFG0_ZL4PYLD_TRASH	vxge_mBIT(43)
#define	VXGE_HW_RTS_MGR_CFG0_L4PRTCL_TCP_TRASH	vxge_mBIT(47)
#define	VXGE_HW_RTS_MGR_CFG0_L4PRTCL_UDP_TRASH	vxge_mBIT(51)
#define	VXGE_HW_RTS_MGR_CFG0_L4PRTCL_FLEX_TRASH	vxge_mBIT(55)
#define	VXGE_HW_RTS_MGR_CFG0_IPFRAG_TRASH	vxge_mBIT(59)
/*0x01830*/	u64	rts_mgr_cfg1;
#define	VXGE_HW_RTS_MGR_CFG1_DA_ACTIVE_TABLE	vxge_mBIT(3)
#define	VXGE_HW_RTS_MGR_CFG1_PN_ACTIVE_TABLE	vxge_mBIT(7)
/*0x01838*/	u64	rts_mgr_criteria_priority;
#define VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_ETYPE(val) vxge_vBIT(val, 5, 3)
#define VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_ICMP_TCPSYN(val) vxge_vBIT(val, 9, 3)
#define VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_L4PN(val) vxge_vBIT(val, 13, 3)
#define VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_RANGE_L4PN(val) vxge_vBIT(val, 17, 3)
#define VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_RTH_IT(val) vxge_vBIT(val, 21, 3)
#define VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_DS(val) vxge_vBIT(val, 25, 3)
#define VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_QOS(val) vxge_vBIT(val, 29, 3)
#define VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_ZL4PYLD(val) vxge_vBIT(val, 33, 3)
#define VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_L4PRTCL(val) vxge_vBIT(val, 37, 3)
/*0x01840*/	u64	rts_mgr_da_pause_cfg;
#define VXGE_HW_RTS_MGR_DA_PAUSE_CFG_VPATH_VECTOR(val) vxge_vBIT(val, 0, 17)
/*0x01848*/	u64	rts_mgr_da_slow_proto_cfg;
#define VXGE_HW_RTS_MGR_DA_SLOW_PROTO_CFG_VPATH_VECTOR(val) \
							vxge_vBIT(val, 0, 17)
	u8	unused01890[0x01890-0x01850];
/*0x01890*/     u64     rts_mgr_cbasin_cfg;
	u8	unused01968[0x01968-0x01898];

/*0x01968*/	u64	dbg_stat_rx_any_frms;
#define VXGE_HW_DBG_STAT_RX_ANY_FRMS_PORT0_RX_ANY_FRMS(val) vxge_vBIT(val, 0, 8)
#define VXGE_HW_DBG_STAT_RX_ANY_FRMS_PORT1_RX_ANY_FRMS(val) vxge_vBIT(val, 8, 8)
#define VXGE_HW_DBG_STAT_RX_ANY_FRMS_PORT2_RX_ANY_FRMS(val) \
							vxge_vBIT(val, 16, 8)
	u8	unused01a00[0x01a00-0x01970];

/*0x01a00*/	u64	rxmac_red_rate_vp[17];
#define VXGE_HW_RXMAC_RED_RATE_VP_CRATE_THR0(val) vxge_vBIT(val, 0, 4)
#define VXGE_HW_RXMAC_RED_RATE_VP_CRATE_THR1(val) vxge_vBIT(val, 4, 4)
#define VXGE_HW_RXMAC_RED_RATE_VP_CRATE_THR2(val) vxge_vBIT(val, 8, 4)
#define VXGE_HW_RXMAC_RED_RATE_VP_CRATE_THR3(val) vxge_vBIT(val, 12, 4)
#define VXGE_HW_RXMAC_RED_RATE_VP_FRATE_THR0(val) vxge_vBIT(val, 16, 4)
#define VXGE_HW_RXMAC_RED_RATE_VP_FRATE_THR1(val) vxge_vBIT(val, 20, 4)
#define VXGE_HW_RXMAC_RED_RATE_VP_FRATE_THR2(val) vxge_vBIT(val, 24, 4)
#define VXGE_HW_RXMAC_RED_RATE_VP_FRATE_THR3(val) vxge_vBIT(val, 28, 4)
	u8	unused01e00[0x01e00-0x01a88];

/*0x01e00*/	u64	xgmac_int_status;
#define	VXGE_HW_XGMAC_INT_STATUS_XMAC_GEN_ERR_XMAC_GEN_INT	vxge_mBIT(3)
#define	VXGE_HW_XGMAC_INT_STATUS_XMAC_LINK_ERR_PORT0_XMAC_LINK_INT_PORT0 \
								vxge_mBIT(7)
#define	VXGE_HW_XGMAC_INT_STATUS_XMAC_LINK_ERR_PORT1_XMAC_LINK_INT_PORT1 \
								vxge_mBIT(11)
#define	VXGE_HW_XGMAC_INT_STATUS_XGXS_GEN_ERR_XGXS_GEN_INT	vxge_mBIT(15)
#define	VXGE_HW_XGMAC_INT_STATUS_ASIC_NTWK_ERR_ASIC_NTWK_INT	vxge_mBIT(19)
#define	VXGE_HW_XGMAC_INT_STATUS_ASIC_GPIO_ERR_ASIC_GPIO_INT	vxge_mBIT(23)
/*0x01e08*/	u64	xgmac_int_mask;
/*0x01e10*/	u64	xmac_gen_err_reg;
#define	VXGE_HW_XMAC_GEN_ERR_REG_LAGC_LAG_PORT0_ACTOR_CHURN_DETECTED \
								vxge_mBIT(7)
#define	VXGE_HW_XMAC_GEN_ERR_REG_LAGC_LAG_PORT0_PARTNER_CHURN_DETECTED \
								vxge_mBIT(11)
#define	VXGE_HW_XMAC_GEN_ERR_REG_LAGC_LAG_PORT0_RECEIVED_LACPDU	vxge_mBIT(15)
#define	VXGE_HW_XMAC_GEN_ERR_REG_LAGC_LAG_PORT1_ACTOR_CHURN_DETECTED \
								vxge_mBIT(19)
#define	VXGE_HW_XMAC_GEN_ERR_REG_LAGC_LAG_PORT1_PARTNER_CHURN_DETECTED \
								vxge_mBIT(23)
#define	VXGE_HW_XMAC_GEN_ERR_REG_LAGC_LAG_PORT1_RECEIVED_LACPDU	vxge_mBIT(27)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XLCM_LAG_FAILOVER_DETECTED	vxge_mBIT(31)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE0_SG_ERR(val) \
							vxge_vBIT(val, 40, 2)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE0_DB_ERR(val) \
							vxge_vBIT(val, 42, 2)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE1_SG_ERR(val) \
							vxge_vBIT(val, 44, 2)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE1_DB_ERR(val) \
							vxge_vBIT(val, 46, 2)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE2_SG_ERR(val) \
							vxge_vBIT(val, 48, 2)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE2_DB_ERR(val) \
							vxge_vBIT(val, 50, 2)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE3_SG_ERR(val) \
							vxge_vBIT(val, 52, 2)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE3_DB_ERR(val) \
							vxge_vBIT(val, 54, 2)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE4_SG_ERR(val) \
							vxge_vBIT(val, 56, 2)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XSTATS_RMAC_STATS_TILE4_DB_ERR(val) \
							vxge_vBIT(val, 58, 2)
#define	VXGE_HW_XMAC_GEN_ERR_REG_XMACJ_XMAC_FSM_ERR	vxge_mBIT(63)
/*0x01e18*/	u64	xmac_gen_err_mask;
/*0x01e20*/	u64	xmac_gen_err_alarm;
/*0x01e28*/	u64	xmac_link_err_port0_reg;
#define	VXGE_HW_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_DOWN	vxge_mBIT(3)
#define	VXGE_HW_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_UP	vxge_mBIT(7)
#define	VXGE_HW_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_WENT_DOWN	vxge_mBIT(11)
#define	VXGE_HW_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_WENT_UP	vxge_mBIT(15)
#define	VXGE_HW_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_REAFFIRMED_FAULT \
								vxge_mBIT(19)
#define	VXGE_HW_XMAC_LINK_ERR_PORT_REG_XMACJ_PORT_REAFFIRMED_OK	vxge_mBIT(23)
#define	VXGE_HW_XMAC_LINK_ERR_PORT_REG_XMACJ_LINK_DOWN	vxge_mBIT(27)
#define	VXGE_HW_XMAC_LINK_ERR_PORT_REG_XMACJ_LINK_UP	vxge_mBIT(31)
#define	VXGE_HW_XMAC_LINK_ERR_PORT_REG_RATEMGMT_RATE_CHANGE	vxge_mBIT(35)
#define	VXGE_HW_XMAC_LINK_ERR_PORT_REG_RATEMGMT_LASI_INV	vxge_mBIT(39)
#define	VXGE_HW_XMAC_LINK_ERR_PORT_REG_XMDIO_MDIO_MGR_ACCESS_COMPLETE \
								vxge_mBIT(47)
/*0x01e30*/	u64	xmac_link_err_port0_mask;
/*0x01e38*/	u64	xmac_link_err_port0_alarm;
/*0x01e40*/	u64	xmac_link_err_port1_reg;
/*0x01e48*/	u64	xmac_link_err_port1_mask;
/*0x01e50*/	u64	xmac_link_err_port1_alarm;
/*0x01e58*/	u64	xgxs_gen_err_reg;
#define	VXGE_HW_XGXS_GEN_ERR_REG_XGXS_XGXS_FSM_ERR	vxge_mBIT(63)
/*0x01e60*/	u64	xgxs_gen_err_mask;
/*0x01e68*/	u64	xgxs_gen_err_alarm;
/*0x01e70*/	u64	asic_ntwk_err_reg;
#define	VXGE_HW_ASIC_NTWK_ERR_REG_XMACJ_NTWK_DOWN	vxge_mBIT(3)
#define	VXGE_HW_ASIC_NTWK_ERR_REG_XMACJ_NTWK_UP	vxge_mBIT(7)
#define	VXGE_HW_ASIC_NTWK_ERR_REG_XMACJ_NTWK_WENT_DOWN	vxge_mBIT(11)
#define	VXGE_HW_ASIC_NTWK_ERR_REG_XMACJ_NTWK_WENT_UP	vxge_mBIT(15)
#define	VXGE_HW_ASIC_NTWK_ERR_REG_XMACJ_NTWK_REAFFIRMED_FAULT	vxge_mBIT(19)
#define	VXGE_HW_ASIC_NTWK_ERR_REG_XMACJ_NTWK_REAFFIRMED_OK	vxge_mBIT(23)
/*0x01e78*/	u64	asic_ntwk_err_mask;
/*0x01e80*/	u64	asic_ntwk_err_alarm;
/*0x01e88*/	u64	asic_gpio_err_reg;
#define VXGE_HW_ASIC_GPIO_ERR_REG_XMACJ_GPIO_INT(n)	vxge_mBIT(n)
/*0x01e90*/	u64	asic_gpio_err_mask;
/*0x01e98*/	u64	asic_gpio_err_alarm;
/*0x01ea0*/	u64	xgmac_gen_status;
#define	VXGE_HW_XGMAC_GEN_STATUS_XMACJ_NTWK_OK	vxge_mBIT(3)
#define	VXGE_HW_XGMAC_GEN_STATUS_XMACJ_NTWK_DATA_RATE	vxge_mBIT(11)
/*0x01ea8*/	u64	xgmac_gen_fw_memo_status;
#define	VXGE_HW_XGMAC_GEN_FW_MEMO_STATUS_XMACJ_EVENTS_PENDING(val) \
							vxge_vBIT(val, 0, 17)
/*0x01eb0*/	u64	xgmac_gen_fw_memo_mask;
#define VXGE_HW_XGMAC_GEN_FW_MEMO_MASK_MASK(val) vxge_vBIT(val, 0, 64)
/*0x01eb8*/	u64	xgmac_gen_fw_vpath_to_vsport_status;
#define	VXGE_HW_XGMAC_GEN_FW_VPATH_TO_VSPORT_STATUS_XMACJ_EVENTS_PENDING(val) \
						vxge_vBIT(val, 0, 17)
/*0x01ec0*/	u64	xgmac_main_cfg_port[2];
#define	VXGE_HW_XGMAC_MAIN_CFG_PORT_PORT_EN	vxge_mBIT(3)
	u8	unused01f40[0x01f40-0x01ed0];

/*0x01f40*/	u64	xmac_gen_cfg;
#define VXGE_HW_XMAC_GEN_CFG_RATEMGMT_MAC_RATE_SEL(val) vxge_vBIT(val, 2, 2)
#define	VXGE_HW_XMAC_GEN_CFG_TX_HEAD_DROP_WHEN_FAULT	vxge_mBIT(7)
#define	VXGE_HW_XMAC_GEN_CFG_FAULT_BEHAVIOUR	vxge_mBIT(27)
#define VXGE_HW_XMAC_GEN_CFG_PERIOD_NTWK_UP(val) vxge_vBIT(val, 28, 4)
#define VXGE_HW_XMAC_GEN_CFG_PERIOD_NTWK_DOWN(val) vxge_vBIT(val, 32, 4)
/*0x01f48*/	u64	xmac_timestamp;
#define	VXGE_HW_XMAC_TIMESTAMP_EN	vxge_mBIT(3)
#define VXGE_HW_XMAC_TIMESTAMP_USE_LINK_ID(val) vxge_vBIT(val, 6, 2)
#define VXGE_HW_XMAC_TIMESTAMP_INTERVAL(val) vxge_vBIT(val, 12, 4)
#define	VXGE_HW_XMAC_TIMESTAMP_TIMER_RESTART	vxge_mBIT(19)
#define VXGE_HW_XMAC_TIMESTAMP_XMACJ_ROLLOVER_CNT(val) vxge_vBIT(val, 32, 16)
/*0x01f50*/	u64	xmac_stats_gen_cfg;
#define VXGE_HW_XMAC_STATS_GEN_CFG_PRTAGGR_CUM_TIMER(val) vxge_vBIT(val, 4, 4)
#define VXGE_HW_XMAC_STATS_GEN_CFG_VPATH_CUM_TIMER(val) vxge_vBIT(val, 8, 4)
#define	VXGE_HW_XMAC_STATS_GEN_CFG_VLAN_HANDLING	vxge_mBIT(15)
/*0x01f58*/	u64	xmac_stats_sys_cmd;
#define VXGE_HW_XMAC_STATS_SYS_CMD_OP(val) vxge_vBIT(val, 5, 3)
#define	VXGE_HW_XMAC_STATS_SYS_CMD_STROBE	vxge_mBIT(15)
#define VXGE_HW_XMAC_STATS_SYS_CMD_LOC_SEL(val) vxge_vBIT(val, 27, 5)
#define VXGE_HW_XMAC_STATS_SYS_CMD_OFFSET_SEL(val) vxge_vBIT(val, 32, 8)
/*0x01f60*/	u64	xmac_stats_sys_data;
#define VXGE_HW_XMAC_STATS_SYS_DATA_XSMGR_DATA(val) vxge_vBIT(val, 0, 64)
	u8	unused01f80[0x01f80-0x01f68];

/*0x01f80*/	u64	asic_ntwk_ctrl;
#define	VXGE_HW_ASIC_NTWK_CTRL_REQ_TEST_NTWK	vxge_mBIT(3)
#define	VXGE_HW_ASIC_NTWK_CTRL_PORT0_REQ_TEST_PORT	vxge_mBIT(11)
#define	VXGE_HW_ASIC_NTWK_CTRL_PORT1_REQ_TEST_PORT	vxge_mBIT(15)
/*0x01f88*/	u64	asic_ntwk_cfg_show_port_info;
#define VXGE_HW_ASIC_NTWK_CFG_SHOW_PORT_INFO_VP(n)	vxge_mBIT(n)
/*0x01f90*/	u64	asic_ntwk_cfg_port_num;
#define VXGE_HW_ASIC_NTWK_CFG_PORT_NUM_VP(n)	vxge_mBIT(n)
/*0x01f98*/	u64	xmac_cfg_port[3];
#define	VXGE_HW_XMAC_CFG_PORT_XGMII_LOOPBACK	vxge_mBIT(3)
#define	VXGE_HW_XMAC_CFG_PORT_XGMII_REVERSE_LOOPBACK	vxge_mBIT(7)
#define	VXGE_HW_XMAC_CFG_PORT_XGMII_TX_BEHAV	vxge_mBIT(11)
#define	VXGE_HW_XMAC_CFG_PORT_XGMII_RX_BEHAV	vxge_mBIT(15)
/*0x01fb0*/	u64	xmac_station_addr_port[2];
#define VXGE_HW_XMAC_STATION_ADDR_PORT_MAC_ADDR(val) vxge_vBIT(val, 0, 48)
	u8	unused02020[0x02020-0x01fc0];

/*0x02020*/	u64	lag_cfg;
#define	VXGE_HW_LAG_CFG_EN	vxge_mBIT(3)
#define VXGE_HW_LAG_CFG_MODE(val) vxge_vBIT(val, 6, 2)
#define	VXGE_HW_LAG_CFG_TX_DISCARD_BEHAV	vxge_mBIT(11)
#define	VXGE_HW_LAG_CFG_RX_DISCARD_BEHAV	vxge_mBIT(15)
#define	VXGE_HW_LAG_CFG_PREF_INDIV_PORT_NUM	vxge_mBIT(19)
/*0x02028*/	u64	lag_status;
#define	VXGE_HW_LAG_STATUS_XLCM_WAITING_TO_FAILBACK	vxge_mBIT(3)
#define VXGE_HW_LAG_STATUS_XLCM_TIMER_VAL_COLD_FAILOVER(val) \
							vxge_vBIT(val, 8, 8)
/*0x02030*/	u64	lag_active_passive_cfg;
#define	VXGE_HW_LAG_ACTIVE_PASSIVE_CFG_HOT_STANDBY	vxge_mBIT(3)
#define	VXGE_HW_LAG_ACTIVE_PASSIVE_CFG_LACP_DECIDES	vxge_mBIT(7)
#define	VXGE_HW_LAG_ACTIVE_PASSIVE_CFG_PREF_ACTIVE_PORT_NUM	vxge_mBIT(11)
#define	VXGE_HW_LAG_ACTIVE_PASSIVE_CFG_AUTO_FAILBACK	vxge_mBIT(15)
#define	VXGE_HW_LAG_ACTIVE_PASSIVE_CFG_FAILBACK_EN	vxge_mBIT(19)
#define	VXGE_HW_LAG_ACTIVE_PASSIVE_CFG_COLD_FAILOVER_TIMEOUT(val) \
							vxge_vBIT(val, 32, 16)
	u8	unused02040[0x02040-0x02038];

/*0x02040*/	u64	lag_lacp_cfg;
#define	VXGE_HW_LAG_LACP_CFG_EN	vxge_mBIT(3)
#define	VXGE_HW_LAG_LACP_CFG_LACP_BEGIN	vxge_mBIT(7)
#define	VXGE_HW_LAG_LACP_CFG_DISCARD_LACP	vxge_mBIT(11)
#define	VXGE_HW_LAG_LACP_CFG_LIBERAL_LEN_CHK	vxge_mBIT(15)
/*0x02048*/	u64	lag_timer_cfg_1;
#define VXGE_HW_LAG_TIMER_CFG_1_FAST_PER(val) vxge_vBIT(val, 0, 16)
#define VXGE_HW_LAG_TIMER_CFG_1_SLOW_PER(val) vxge_vBIT(val, 16, 16)
#define VXGE_HW_LAG_TIMER_CFG_1_SHORT_TIMEOUT(val) vxge_vBIT(val, 32, 16)
#define VXGE_HW_LAG_TIMER_CFG_1_LONG_TIMEOUT(val) vxge_vBIT(val, 48, 16)
/*0x02050*/	u64	lag_timer_cfg_2;
#define VXGE_HW_LAG_TIMER_CFG_2_CHURN_DET(val) vxge_vBIT(val, 0, 16)
#define VXGE_HW_LAG_TIMER_CFG_2_AGGR_WAIT(val) vxge_vBIT(val, 16, 16)
#define VXGE_HW_LAG_TIMER_CFG_2_SHORT_TIMER_SCALE(val) vxge_vBIT(val, 32, 16)
#define VXGE_HW_LAG_TIMER_CFG_2_LONG_TIMER_SCALE(val)  vxge_vBIT(val, 48, 16)
/*0x02058*/	u64	lag_sys_id;
#define VXGE_HW_LAG_SYS_ID_ADDR(val) vxge_vBIT(val, 0, 48)
#define	VXGE_HW_LAG_SYS_ID_USE_PORT_ADDR	vxge_mBIT(51)
#define	VXGE_HW_LAG_SYS_ID_ADDR_SEL	vxge_mBIT(55)
/*0x02060*/	u64	lag_sys_cfg;
#define VXGE_HW_LAG_SYS_CFG_SYS_PRI(val) vxge_vBIT(val, 0, 16)
	u8	unused02070[0x02070-0x02068];

/*0x02070*/	u64	lag_aggr_addr_cfg[2];
#define VXGE_HW_LAG_AGGR_ADDR_CFG_ADDR(val) vxge_vBIT(val, 0, 48)
#define	VXGE_HW_LAG_AGGR_ADDR_CFG_USE_PORT_ADDR	vxge_mBIT(51)
#define	VXGE_HW_LAG_AGGR_ADDR_CFG_ADDR_SEL	vxge_mBIT(55)
/*0x02080*/	u64	lag_aggr_id_cfg[2];
#define VXGE_HW_LAG_AGGR_ID_CFG_ID(val) vxge_vBIT(val, 0, 16)
/*0x02090*/	u64	lag_aggr_admin_key[2];
#define VXGE_HW_LAG_AGGR_ADMIN_KEY_KEY(val) vxge_vBIT(val, 0, 16)
/*0x020a0*/	u64	lag_aggr_alt_admin_key;
#define VXGE_HW_LAG_AGGR_ALT_ADMIN_KEY_KEY(val) vxge_vBIT(val, 0, 16)
#define	VXGE_HW_LAG_AGGR_ALT_ADMIN_KEY_ALT_AGGR	vxge_mBIT(19)
/*0x020a8*/	u64	lag_aggr_oper_key[2];
#define VXGE_HW_LAG_AGGR_OPER_KEY_LAGC_KEY(val) vxge_vBIT(val, 0, 16)
/*0x020b8*/	u64	lag_aggr_partner_sys_id[2];
#define VXGE_HW_LAG_AGGR_PARTNER_SYS_ID_LAGC_ADDR(val) vxge_vBIT(val, 0, 48)
/*0x020c8*/	u64	lag_aggr_partner_info[2];
#define VXGE_HW_LAG_AGGR_PARTNER_INFO_LAGC_SYS_PRI(val) vxge_vBIT(val, 0, 16)
#define	VXGE_HW_LAG_AGGR_PARTNER_INFO_LAGC_OPER_KEY(val) \
						vxge_vBIT(val, 16, 16)
/*0x020d8*/	u64	lag_aggr_state[2];
#define	VXGE_HW_LAG_AGGR_STATE_LAGC_TX	vxge_mBIT(3)
#define	VXGE_HW_LAG_AGGR_STATE_LAGC_RX	vxge_mBIT(7)
#define	VXGE_HW_LAG_AGGR_STATE_LAGC_READY	vxge_mBIT(11)
#define	VXGE_HW_LAG_AGGR_STATE_LAGC_INDIVIDUAL	vxge_mBIT(15)
	u8	unused020f0[0x020f0-0x020e8];

/*0x020f0*/	u64	lag_port_cfg[2];
#define	VXGE_HW_LAG_PORT_CFG_EN	vxge_mBIT(3)
#define	VXGE_HW_LAG_PORT_CFG_DISCARD_SLOW_PROTO	vxge_mBIT(7)
#define	VXGE_HW_LAG_PORT_CFG_HOST_CHOSEN_AGGR	vxge_mBIT(11)
#define	VXGE_HW_LAG_PORT_CFG_DISCARD_UNKNOWN_SLOW_PROTO	vxge_mBIT(15)
/*0x02100*/	u64	lag_port_actor_admin_cfg[2];
#define VXGE_HW_LAG_PORT_ACTOR_ADMIN_CFG_PORT_NUM(val) vxge_vBIT(val, 0, 16)
#define VXGE_HW_LAG_PORT_ACTOR_ADMIN_CFG_PORT_PRI(val) vxge_vBIT(val, 16, 16)
#define VXGE_HW_LAG_PORT_ACTOR_ADMIN_CFG_KEY_10G(val) vxge_vBIT(val, 32, 16)
#define VXGE_HW_LAG_PORT_ACTOR_ADMIN_CFG_KEY_1G(val) vxge_vBIT(val, 48, 16)
/*0x02110*/	u64	lag_port_actor_admin_state[2];
#define	VXGE_HW_LAG_PORT_ACTOR_ADMIN_STATE_LACP_ACTIVITY	vxge_mBIT(3)
#define	VXGE_HW_LAG_PORT_ACTOR_ADMIN_STATE_LACP_TIMEOUT	vxge_mBIT(7)
#define	VXGE_HW_LAG_PORT_ACTOR_ADMIN_STATE_AGGREGATION	vxge_mBIT(11)
#define	VXGE_HW_LAG_PORT_ACTOR_ADMIN_STATE_SYNCHRONIZATION	vxge_mBIT(15)
#define	VXGE_HW_LAG_PORT_ACTOR_ADMIN_STATE_COLLECTING	vxge_mBIT(19)
#define	VXGE_HW_LAG_PORT_ACTOR_ADMIN_STATE_DISTRIBUTING	vxge_mBIT(23)
#define	VXGE_HW_LAG_PORT_ACTOR_ADMIN_STATE_DEFAULTED	vxge_mBIT(27)
#define	VXGE_HW_LAG_PORT_ACTOR_ADMIN_STATE_EXPIRED	vxge_mBIT(31)
/*0x02120*/	u64	lag_port_partner_admin_sys_id[2];
#define VXGE_HW_LAG_PORT_PARTNER_ADMIN_SYS_ID_ADDR(val) vxge_vBIT(val, 0, 48)
/*0x02130*/	u64	lag_port_partner_admin_cfg[2];
#define VXGE_HW_LAG_PORT_PARTNER_ADMIN_CFG_SYS_PRI(val) vxge_vBIT(val, 0, 16)
#define VXGE_HW_LAG_PORT_PARTNER_ADMIN_CFG_KEY(val) vxge_vBIT(val, 16, 16)
#define	VXGE_HW_LAG_PORT_PARTNER_ADMIN_CFG_PORT_NUM(val) \
							vxge_vBIT(val, 32, 16)
#define	VXGE_HW_LAG_PORT_PARTNER_ADMIN_CFG_PORT_PRI(val) \
							vxge_vBIT(val, 48, 16)
/*0x02140*/	u64	lag_port_partner_admin_state[2];
#define	VXGE_HW_LAG_PORT_PARTNER_ADMIN_STATE_LACP_ACTIVITY	vxge_mBIT(3)
#define	VXGE_HW_LAG_PORT_PARTNER_ADMIN_STATE_LACP_TIMEOUT	vxge_mBIT(7)
#define	VXGE_HW_LAG_PORT_PARTNER_ADMIN_STATE_AGGREGATION	vxge_mBIT(11)
#define	VXGE_HW_LAG_PORT_PARTNER_ADMIN_STATE_SYNCHRONIZATION	vxge_mBIT(15)
#define	VXGE_HW_LAG_PORT_PARTNER_ADMIN_STATE_COLLECTING	vxge_mBIT(19)
#define	VXGE_HW_LAG_PORT_PARTNER_ADMIN_STATE_DISTRIBUTING	vxge_mBIT(23)
#define	VXGE_HW_LAG_PORT_PARTNER_ADMIN_STATE_DEFAULTED	vxge_mBIT(27)
#define	VXGE_HW_LAG_PORT_PARTNER_ADMIN_STATE_EXPIRED	vxge_mBIT(31)
/*0x02150*/	u64	lag_port_to_aggr[2];
#define VXGE_HW_LAG_PORT_TO_AGGR_LAGC_AGGR_ID(val) vxge_vBIT(val, 0, 16)
#define	VXGE_HW_LAG_PORT_TO_AGGR_LAGC_AGGR_VLD_ID	vxge_mBIT(19)
/*0x02160*/	u64	lag_port_actor_oper_key[2];
#define VXGE_HW_LAG_PORT_ACTOR_OPER_KEY_LAGC_KEY(val) vxge_vBIT(val, 0, 16)
/*0x02170*/	u64	lag_port_actor_oper_state[2];
#define	VXGE_HW_LAG_PORT_ACTOR_OPER_STATE_LAGC_LACP_ACTIVITY	vxge_mBIT(3)
#define	VXGE_HW_LAG_PORT_ACTOR_OPER_STATE_LAGC_LACP_TIMEOUT	vxge_mBIT(7)
#define	VXGE_HW_LAG_PORT_ACTOR_OPER_STATE_LAGC_AGGREGATION	vxge_mBIT(11)
#define	VXGE_HW_LAG_PORT_ACTOR_OPER_STATE_LAGC_SYNCHRONIZATION	vxge_mBIT(15)
#define	VXGE_HW_LAG_PORT_ACTOR_OPER_STATE_LAGC_COLLECTING	vxge_mBIT(19)
#define	VXGE_HW_LAG_PORT_ACTOR_OPER_STATE_LAGC_DISTRIBUTING	vxge_mBIT(23)
#define	VXGE_HW_LAG_PORT_ACTOR_OPER_STATE_LAGC_DEFAULTED	vxge_mBIT(27)
#define	VXGE_HW_LAG_PORT_ACTOR_OPER_STATE_LAGC_EXPIRED	vxge_mBIT(31)
/*0x02180*/	u64	lag_port_partner_oper_sys_id[2];
#define VXGE_HW_LAG_PORT_PARTNER_OPER_SYS_ID_LAGC_ADDR(val) \
						vxge_vBIT(val, 0, 48)
/*0x02190*/	u64	lag_port_partner_oper_info[2];
#define VXGE_HW_LAG_PORT_PARTNER_OPER_INFO_LAGC_SYS_PRI(val) \
						vxge_vBIT(val, 0, 16)
#define	VXGE_HW_LAG_PORT_PARTNER_OPER_INFO_LAGC_KEY(val) \
						vxge_vBIT(val, 16, 16)
#define	VXGE_HW_LAG_PORT_PARTNER_OPER_INFO_LAGC_PORT_NUM(val) \
						vxge_vBIT(val, 32, 16)
#define	VXGE_HW_LAG_PORT_PARTNER_OPER_INFO_LAGC_PORT_PRI(val) \
						vxge_vBIT(val, 48, 16)
/*0x021a0*/	u64	lag_port_partner_oper_state[2];
#define	VXGE_HW_LAG_PORT_PARTNER_OPER_STATE_LAGC_LACP_ACTIVITY	vxge_mBIT(3)
#define	VXGE_HW_LAG_PORT_PARTNER_OPER_STATE_LAGC_LACP_TIMEOUT	vxge_mBIT(7)
#define	VXGE_HW_LAG_PORT_PARTNER_OPER_STATE_LAGC_AGGREGATION	vxge_mBIT(11)
#define	VXGE_HW_LAG_PORT_PARTNER_OPER_STATE_LAGC_SYNCHRONIZATION \
								vxge_mBIT(15)
#define	VXGE_HW_LAG_PORT_PARTNER_OPER_STATE_LAGC_COLLECTING	vxge_mBIT(19)
#define	VXGE_HW_LAG_PORT_PARTNER_OPER_STATE_LAGC_DISTRIBUTING	vxge_mBIT(23)
#define	VXGE_HW_LAG_PORT_PARTNER_OPER_STATE_LAGC_DEFAULTED	vxge_mBIT(27)
#define	VXGE_HW_LAG_PORT_PARTNER_OPER_STATE_LAGC_EXPIRED	vxge_mBIT(31)
/*0x021b0*/	u64	lag_port_state_vars[2];
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_READY	vxge_mBIT(3)
#define VXGE_HW_LAG_PORT_STATE_VARS_LAGC_SELECTED(val) vxge_vBIT(val, 6, 2)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_AGGR_NUM	vxge_mBIT(11)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_PORT_MOVED	vxge_mBIT(15)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_PORT_ENABLED	vxge_mBIT(18)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_PORT_DISABLED	vxge_mBIT(19)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_NTT	vxge_mBIT(23)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_ACTOR_CHURN	vxge_mBIT(27)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_PARTNER_CHURN	vxge_mBIT(31)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_ACTOR_INFO_LEN_MISMATCH \
								vxge_mBIT(32)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_PARTNER_INFO_LEN_MISMATCH \
								vxge_mBIT(33)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_COLL_INFO_LEN_MISMATCH	vxge_mBIT(34)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_TERM_INFO_LEN_MISMATCH	vxge_mBIT(35)
#define VXGE_HW_LAG_PORT_STATE_VARS_LAGC_RX_FSM_STATE(val) vxge_vBIT(val, 37, 3)
#define VXGE_HW_LAG_PORT_STATE_VARS_LAGC_MUX_FSM_STATE(val) \
							vxge_vBIT(val, 41, 3)
#define VXGE_HW_LAG_PORT_STATE_VARS_LAGC_MUX_REASON(val) vxge_vBIT(val, 44, 4)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_ACTOR_CHURN_STATE	vxge_mBIT(54)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_PARTNER_CHURN_STATE	vxge_mBIT(55)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_ACTOR_CHURN_COUNT(val) \
							vxge_vBIT(val, 56, 4)
#define	VXGE_HW_LAG_PORT_STATE_VARS_LAGC_PARTNER_CHURN_COUNT(val) \
							vxge_vBIT(val, 60, 4)
/*0x021c0*/	u64	lag_port_timer_cntr[2];
#define VXGE_HW_LAG_PORT_TIMER_CNTR_LAGC_CURRENT_WHILE(val) vxge_vBIT(val, 0, 8)
#define VXGE_HW_LAG_PORT_TIMER_CNTR_LAGC_PERIODIC_WHILE(val) \
							vxge_vBIT(val, 8, 8)
#define VXGE_HW_LAG_PORT_TIMER_CNTR_LAGC_WAIT_WHILE(val) vxge_vBIT(val, 16, 8)
#define VXGE_HW_LAG_PORT_TIMER_CNTR_LAGC_TX_LACP(val) vxge_vBIT(val, 24, 8)
#define	VXGE_HW_LAG_PORT_TIMER_CNTR_LAGC_ACTOR_SYNC_TRANSITION_COUNT(val) \
							vxge_vBIT(val, 32, 8)
#define	VXGE_HW_LAG_PORT_TIMER_CNTR_LAGC_PARTNER_SYNC_TRANSITION_COUNT(val) \
							vxge_vBIT(val, 40, 8)
#define	VXGE_HW_LAG_PORT_TIMER_CNTR_LAGC_ACTOR_CHANGE_COUNT(val) \
							vxge_vBIT(val, 48, 8)
#define	VXGE_HW_LAG_PORT_TIMER_CNTR_LAGC_PARTNER_CHANGE_COUNT(val) \
							vxge_vBIT(val, 56, 8)
	u8	unused02208[0x02700-0x021d0];

/*0x02700*/	u64	rtdma_int_status;
#define	VXGE_HW_RTDMA_INT_STATUS_PDA_ALARM_PDA_INT	vxge_mBIT(1)
#define	VXGE_HW_RTDMA_INT_STATUS_PCC_ERROR_PCC_INT	vxge_mBIT(2)
#define	VXGE_HW_RTDMA_INT_STATUS_LSO_ERROR_LSO_INT	vxge_mBIT(4)
#define	VXGE_HW_RTDMA_INT_STATUS_SM_ERROR_SM_INT	vxge_mBIT(5)
/*0x02708*/	u64	rtdma_int_mask;
/*0x02710*/	u64	pda_alarm_reg;
#define	VXGE_HW_PDA_ALARM_REG_PDA_HSC_FIFO_ERR	vxge_mBIT(0)
#define	VXGE_HW_PDA_ALARM_REG_PDA_SM_ERR	vxge_mBIT(1)
/*0x02718*/	u64	pda_alarm_mask;
/*0x02720*/	u64	pda_alarm_alarm;
/*0x02728*/	u64	pcc_error_reg;
#define VXGE_HW_PCC_ERROR_REG_PCC_PCC_FRM_BUF_SBE(n)	vxge_mBIT(n)
#define VXGE_HW_PCC_ERROR_REG_PCC_PCC_TXDO_SBE(n)	vxge_mBIT(n)
#define VXGE_HW_PCC_ERROR_REG_PCC_PCC_FRM_BUF_DBE(n)	vxge_mBIT(n)
#define VXGE_HW_PCC_ERROR_REG_PCC_PCC_TXDO_DBE(n)	vxge_mBIT(n)
#define VXGE_HW_PCC_ERROR_REG_PCC_PCC_FSM_ERR_ALARM(n)	vxge_mBIT(n)
#define VXGE_HW_PCC_ERROR_REG_PCC_PCC_SERR(n)	vxge_mBIT(n)
/*0x02730*/	u64	pcc_error_mask;
/*0x02738*/	u64	pcc_error_alarm;
/*0x02740*/	u64	lso_error_reg;
#define VXGE_HW_LSO_ERROR_REG_PCC_LSO_ABORT(n)	vxge_mBIT(n)
#define VXGE_HW_LSO_ERROR_REG_PCC_LSO_FSM_ERR_ALARM(n)	vxge_mBIT(n)
/*0x02748*/	u64	lso_error_mask;
/*0x02750*/	u64	lso_error_alarm;
/*0x02758*/	u64	sm_error_reg;
#define	VXGE_HW_SM_ERROR_REG_SM_FSM_ERR_ALARM	vxge_mBIT(15)
/*0x02760*/	u64	sm_error_mask;
/*0x02768*/	u64	sm_error_alarm;

	u8	unused027a8[0x027a8-0x02770];

/*0x027a8*/	u64	txd_ownership_ctrl;
#define	VXGE_HW_TXD_OWNERSHIP_CTRL_KEEP_OWNERSHIP	vxge_mBIT(7)
/*0x027b0*/	u64	pcc_cfg;
#define VXGE_HW_PCC_CFG_PCC_ENABLE(n)	vxge_mBIT(n)
#define VXGE_HW_PCC_CFG_PCC_ECC_ENABLE_N(n)	vxge_mBIT(n)
/*0x027b8*/	u64	pcc_control;
#define VXGE_HW_PCC_CONTROL_FE_ENABLE(val) vxge_vBIT(val, 6, 2)
#define	VXGE_HW_PCC_CONTROL_EARLY_ASSIGN_EN	vxge_mBIT(15)
#define	VXGE_HW_PCC_CONTROL_UNBLOCK_DB_ERR	vxge_mBIT(31)
/*0x027c0*/	u64	pda_status1;
#define VXGE_HW_PDA_STATUS1_PDA_WRAP_0_CTR(val) vxge_vBIT(val, 4, 4)
#define VXGE_HW_PDA_STATUS1_PDA_WRAP_1_CTR(val) vxge_vBIT(val, 12, 4)
#define VXGE_HW_PDA_STATUS1_PDA_WRAP_2_CTR(val) vxge_vBIT(val, 20, 4)
#define VXGE_HW_PDA_STATUS1_PDA_WRAP_3_CTR(val) vxge_vBIT(val, 28, 4)
#define VXGE_HW_PDA_STATUS1_PDA_WRAP_4_CTR(val) vxge_vBIT(val, 36, 4)
#define VXGE_HW_PDA_STATUS1_PDA_WRAP_5_CTR(val) vxge_vBIT(val, 44, 4)
#define VXGE_HW_PDA_STATUS1_PDA_WRAP_6_CTR(val) vxge_vBIT(val, 52, 4)
#define VXGE_HW_PDA_STATUS1_PDA_WRAP_7_CTR(val) vxge_vBIT(val, 60, 4)
/*0x027c8*/	u64	rtdma_bw_timer;
#define VXGE_HW_RTDMA_BW_TIMER_TIMER_CTRL(val) vxge_vBIT(val, 12, 4)

	u8	unused02900[0x02900-0x027d0];
/*0x02900*/	u64	g3cmct_int_status;
#define	VXGE_HW_G3CMCT_INT_STATUS_ERR_G3IF_INT	vxge_mBIT(0)
/*0x02908*/	u64	g3cmct_int_mask;
/*0x02910*/	u64	g3cmct_err_reg;
#define	VXGE_HW_G3CMCT_ERR_REG_G3IF_SM_ERR	vxge_mBIT(4)
#define	VXGE_HW_G3CMCT_ERR_REG_G3IF_GDDR3_DECC	vxge_mBIT(5)
#define	VXGE_HW_G3CMCT_ERR_REG_G3IF_GDDR3_U_DECC	vxge_mBIT(6)
#define	VXGE_HW_G3CMCT_ERR_REG_G3IF_CTRL_FIFO_DECC	vxge_mBIT(7)
#define	VXGE_HW_G3CMCT_ERR_REG_G3IF_GDDR3_SECC	vxge_mBIT(29)
#define	VXGE_HW_G3CMCT_ERR_REG_G3IF_GDDR3_U_SECC	vxge_mBIT(30)
#define	VXGE_HW_G3CMCT_ERR_REG_G3IF_CTRL_FIFO_SECC	vxge_mBIT(31)
/*0x02918*/	u64	g3cmct_err_mask;
/*0x02920*/	u64	g3cmct_err_alarm;
	u8	unused03000[0x03000-0x02928];

/*0x03000*/	u64	mc_int_status;
#define	VXGE_HW_MC_INT_STATUS_MC_ERR_MC_INT	vxge_mBIT(3)
#define	VXGE_HW_MC_INT_STATUS_GROCRC_ALARM_ROCRC_INT	vxge_mBIT(7)
#define	VXGE_HW_MC_INT_STATUS_FAU_GEN_ERR_FAU_GEN_INT	vxge_mBIT(11)
#define	VXGE_HW_MC_INT_STATUS_FAU_ECC_ERR_FAU_ECC_INT	vxge_mBIT(15)
/*0x03008*/	u64	mc_int_mask;
/*0x03010*/	u64	mc_err_reg;
#define	VXGE_HW_MC_ERR_REG_MC_XFMD_MEM_ECC_SG_ERR_A	vxge_mBIT(3)
#define	VXGE_HW_MC_ERR_REG_MC_XFMD_MEM_ECC_SG_ERR_B	vxge_mBIT(4)
#define	VXGE_HW_MC_ERR_REG_MC_G3IF_RD_FIFO_ECC_SG_ERR	vxge_mBIT(5)
#define	VXGE_HW_MC_ERR_REG_MC_MIRI_ECC_SG_ERR_0	vxge_mBIT(6)
#define	VXGE_HW_MC_ERR_REG_MC_MIRI_ECC_SG_ERR_1	vxge_mBIT(7)
#define	VXGE_HW_MC_ERR_REG_MC_XFMD_MEM_ECC_DB_ERR_A	vxge_mBIT(10)
#define	VXGE_HW_MC_ERR_REG_MC_XFMD_MEM_ECC_DB_ERR_B	vxge_mBIT(11)
#define	VXGE_HW_MC_ERR_REG_MC_G3IF_RD_FIFO_ECC_DB_ERR	vxge_mBIT(12)
#define	VXGE_HW_MC_ERR_REG_MC_MIRI_ECC_DB_ERR_0	vxge_mBIT(13)
#define	VXGE_HW_MC_ERR_REG_MC_MIRI_ECC_DB_ERR_1	vxge_mBIT(14)
#define	VXGE_HW_MC_ERR_REG_MC_SM_ERR	vxge_mBIT(15)
/*0x03018*/	u64	mc_err_mask;
/*0x03020*/	u64	mc_err_alarm;
/*0x03028*/	u64	grocrc_alarm_reg;
#define	VXGE_HW_GROCRC_ALARM_REG_XFMD_WR_FIFO_ERR	vxge_mBIT(3)
#define	VXGE_HW_GROCRC_ALARM_REG_WDE2MSR_RD_FIFO_ERR	vxge_mBIT(7)
/*0x03030*/	u64	grocrc_alarm_mask;
/*0x03038*/	u64	grocrc_alarm_alarm;
	u8	unused03100[0x03100-0x03040];

/*0x03100*/	u64	rx_thresh_cfg_repl;
#define VXGE_HW_RX_THRESH_CFG_REPL_PAUSE_LOW_THR(val) vxge_vBIT(val, 0, 8)
#define VXGE_HW_RX_THRESH_CFG_REPL_PAUSE_HIGH_THR(val) vxge_vBIT(val, 8, 8)
#define VXGE_HW_RX_THRESH_CFG_REPL_RED_THR_0(val) vxge_vBIT(val, 16, 8)
#define VXGE_HW_RX_THRESH_CFG_REPL_RED_THR_1(val) vxge_vBIT(val, 24, 8)
#define VXGE_HW_RX_THRESH_CFG_REPL_RED_THR_2(val) vxge_vBIT(val, 32, 8)
#define VXGE_HW_RX_THRESH_CFG_REPL_RED_THR_3(val) vxge_vBIT(val, 40, 8)
#define	VXGE_HW_RX_THRESH_CFG_REPL_GLOBAL_WOL_EN	vxge_mBIT(62)
#define	VXGE_HW_RX_THRESH_CFG_REPL_EXACT_VP_MATCH_REQ	vxge_mBIT(63)
	u8	unused033b8[0x033b8-0x03108];

/*0x033b8*/	u64	fbmc_ecc_cfg;
#define VXGE_HW_FBMC_ECC_CFG_ENABLE(val) vxge_vBIT(val, 3, 5)
	u8	unused03400[0x03400-0x033c0];

/*0x03400*/	u64	pcipif_int_status;
#define	VXGE_HW_PCIPIF_INT_STATUS_DBECC_ERR_DBECC_ERR_INT	vxge_mBIT(3)
#define	VXGE_HW_PCIPIF_INT_STATUS_SBECC_ERR_SBECC_ERR_INT	vxge_mBIT(7)
#define	VXGE_HW_PCIPIF_INT_STATUS_GENERAL_ERR_GENERAL_ERR_INT	vxge_mBIT(11)
#define	VXGE_HW_PCIPIF_INT_STATUS_SRPCIM_MSG_SRPCIM_MSG_INT	vxge_mBIT(15)
#define	VXGE_HW_PCIPIF_INT_STATUS_MRPCIM_SPARE_R1_MRPCIM_SPARE_R1_INT \
								vxge_mBIT(19)
/*0x03408*/	u64	pcipif_int_mask;
/*0x03410*/	u64	dbecc_err_reg;
#define	VXGE_HW_DBECC_ERR_REG_PCI_RETRY_BUF_DB_ERR	vxge_mBIT(3)
#define	VXGE_HW_DBECC_ERR_REG_PCI_RETRY_SOT_DB_ERR	vxge_mBIT(7)
#define	VXGE_HW_DBECC_ERR_REG_PCI_P_HDR_DB_ERR	vxge_mBIT(11)
#define	VXGE_HW_DBECC_ERR_REG_PCI_P_DATA_DB_ERR	vxge_mBIT(15)
#define	VXGE_HW_DBECC_ERR_REG_PCI_NP_HDR_DB_ERR	vxge_mBIT(19)
#define	VXGE_HW_DBECC_ERR_REG_PCI_NP_DATA_DB_ERR	vxge_mBIT(23)
/*0x03418*/	u64	dbecc_err_mask;
/*0x03420*/	u64	dbecc_err_alarm;
/*0x03428*/	u64	sbecc_err_reg;
#define	VXGE_HW_SBECC_ERR_REG_PCI_RETRY_BUF_SG_ERR	vxge_mBIT(3)
#define	VXGE_HW_SBECC_ERR_REG_PCI_RETRY_SOT_SG_ERR	vxge_mBIT(7)
#define	VXGE_HW_SBECC_ERR_REG_PCI_P_HDR_SG_ERR	vxge_mBIT(11)
#define	VXGE_HW_SBECC_ERR_REG_PCI_P_DATA_SG_ERR	vxge_mBIT(15)
#define	VXGE_HW_SBECC_ERR_REG_PCI_NP_HDR_SG_ERR	vxge_mBIT(19)
#define	VXGE_HW_SBECC_ERR_REG_PCI_NP_DATA_SG_ERR	vxge_mBIT(23)
/*0x03430*/	u64	sbecc_err_mask;
/*0x03438*/	u64	sbecc_err_alarm;
/*0x03440*/	u64	general_err_reg;
#define	VXGE_HW_GENERAL_ERR_REG_PCI_DROPPED_ILLEGAL_CFG	vxge_mBIT(3)
#define	VXGE_HW_GENERAL_ERR_REG_PCI_ILLEGAL_MEM_MAP_PROG	vxge_mBIT(7)
#define	VXGE_HW_GENERAL_ERR_REG_PCI_LINK_RST_FSM_ERR	vxge_mBIT(11)
#define	VXGE_HW_GENERAL_ERR_REG_PCI_RX_ILLEGAL_TLP_VPLANE	vxge_mBIT(15)
#define	VXGE_HW_GENERAL_ERR_REG_PCI_TRAINING_RESET_DET	vxge_mBIT(19)
#define	VXGE_HW_GENERAL_ERR_REG_PCI_PCI_LINK_DOWN_DET	vxge_mBIT(23)
#define	VXGE_HW_GENERAL_ERR_REG_PCI_RESET_ACK_DLLP	vxge_mBIT(27)
/*0x03448*/	u64	general_err_mask;
/*0x03450*/	u64	general_err_alarm;
/*0x03458*/	u64	srpcim_msg_reg;
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE0_RMSG_INT \
								vxge_mBIT(0)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE1_RMSG_INT \
								vxge_mBIT(1)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE2_RMSG_INT \
								vxge_mBIT(2)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE3_RMSG_INT \
								vxge_mBIT(3)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE4_RMSG_INT \
								vxge_mBIT(4)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE5_RMSG_INT \
								vxge_mBIT(5)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE6_RMSG_INT \
								vxge_mBIT(6)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE7_RMSG_INT \
								vxge_mBIT(7)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE8_RMSG_INT \
								vxge_mBIT(8)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE9_RMSG_INT \
								vxge_mBIT(9)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE10_RMSG_INT \
								vxge_mBIT(10)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE11_RMSG_INT \
								vxge_mBIT(11)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE12_RMSG_INT \
								vxge_mBIT(12)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE13_RMSG_INT \
								vxge_mBIT(13)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE14_RMSG_INT \
								vxge_mBIT(14)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE15_RMSG_INT \
								vxge_mBIT(15)
#define	VXGE_HW_SRPCIM_MSG_REG_SWIF_SRPCIM_TO_MRPCIM_VPLANE16_RMSG_INT \
								vxge_mBIT(16)
/*0x03460*/	u64	srpcim_msg_mask;
/*0x03468*/	u64	srpcim_msg_alarm;
	u8	unused03600[0x03600-0x03470];

/*0x03600*/	u64	gcmg1_int_status;
#define	VXGE_HW_GCMG1_INT_STATUS_GSSCC_ERR_GSSCC_INT	vxge_mBIT(0)
#define	VXGE_HW_GCMG1_INT_STATUS_GSSC0_ERR0_GSSC0_0_INT	vxge_mBIT(1)
#define	VXGE_HW_GCMG1_INT_STATUS_GSSC0_ERR1_GSSC0_1_INT	vxge_mBIT(2)
#define	VXGE_HW_GCMG1_INT_STATUS_GSSC1_ERR0_GSSC1_0_INT	vxge_mBIT(3)
#define	VXGE_HW_GCMG1_INT_STATUS_GSSC1_ERR1_GSSC1_1_INT	vxge_mBIT(4)
#define	VXGE_HW_GCMG1_INT_STATUS_GSSC2_ERR0_GSSC2_0_INT	vxge_mBIT(5)
#define	VXGE_HW_GCMG1_INT_STATUS_GSSC2_ERR1_GSSC2_1_INT	vxge_mBIT(6)
#define	VXGE_HW_GCMG1_INT_STATUS_UQM_ERR_UQM_INT	vxge_mBIT(7)
#define	VXGE_HW_GCMG1_INT_STATUS_GQCC_ERR_GQCC_INT	vxge_mBIT(8)
/*0x03608*/	u64	gcmg1_int_mask;
	u8	unused03a00[0x03a00-0x03610];

/*0x03a00*/	u64	pcmg1_int_status;
#define	VXGE_HW_PCMG1_INT_STATUS_PSSCC_ERR_PSSCC_INT	vxge_mBIT(0)
#define	VXGE_HW_PCMG1_INT_STATUS_PQCC_ERR_PQCC_INT	vxge_mBIT(1)
#define	VXGE_HW_PCMG1_INT_STATUS_PQCC_CQM_ERR_PQCC_CQM_INT	vxge_mBIT(2)
#define	VXGE_HW_PCMG1_INT_STATUS_PQCC_SQM_ERR_PQCC_SQM_INT	vxge_mBIT(3)
/*0x03a08*/	u64	pcmg1_int_mask;
	u8	unused04000[0x04000-0x03a10];

/*0x04000*/	u64	one_int_status;
#define	VXGE_HW_ONE_INT_STATUS_RXPE_ERR_RXPE_INT	vxge_mBIT(7)
#define	VXGE_HW_ONE_INT_STATUS_TXPE_BCC_MEM_SG_ECC_ERR_TXPE_BCC_MEM_SG_ECC_INT \
							vxge_mBIT(13)
#define	VXGE_HW_ONE_INT_STATUS_TXPE_BCC_MEM_DB_ECC_ERR_TXPE_BCC_MEM_DB_ECC_INT \
							vxge_mBIT(14)
#define	VXGE_HW_ONE_INT_STATUS_TXPE_ERR_TXPE_INT	vxge_mBIT(15)
#define	VXGE_HW_ONE_INT_STATUS_DLM_ERR_DLM_INT	vxge_mBIT(23)
#define	VXGE_HW_ONE_INT_STATUS_PE_ERR_PE_INT	vxge_mBIT(31)
#define	VXGE_HW_ONE_INT_STATUS_RPE_ERR_RPE_INT	vxge_mBIT(39)
#define	VXGE_HW_ONE_INT_STATUS_RPE_FSM_ERR_RPE_FSM_INT	vxge_mBIT(47)
#define	VXGE_HW_ONE_INT_STATUS_OES_ERR_OES_INT	vxge_mBIT(55)
/*0x04008*/	u64	one_int_mask;
	u8	unused04818[0x04818-0x04010];

/*0x04818*/	u64	noa_wct_ctrl;
#define	VXGE_HW_NOA_WCT_CTRL_VP_INT_NUM	vxge_mBIT(0)
/*0x04820*/	u64	rc_cfg2;
#define VXGE_HW_RC_CFG2_BUFF1_SIZE(val) vxge_vBIT(val, 0, 16)
#define VXGE_HW_RC_CFG2_BUFF2_SIZE(val) vxge_vBIT(val, 16, 16)
#define VXGE_HW_RC_CFG2_BUFF3_SIZE(val) vxge_vBIT(val, 32, 16)
#define VXGE_HW_RC_CFG2_BUFF4_SIZE(val) vxge_vBIT(val, 48, 16)
/*0x04828*/	u64	rc_cfg3;
#define VXGE_HW_RC_CFG3_BUFF5_SIZE(val) vxge_vBIT(val, 0, 16)
/*0x04830*/	u64	rx_multi_cast_ctrl1;
#define	VXGE_HW_RX_MULTI_CAST_CTRL1_ENABLE	vxge_mBIT(7)
#define VXGE_HW_RX_MULTI_CAST_CTRL1_DELAY_COUNT(val) vxge_vBIT(val, 11, 5)
/*0x04838*/	u64	rxdm_dbg_rd;
#define VXGE_HW_RXDM_DBG_RD_ADDR(val) vxge_vBIT(val, 0, 12)
#define	VXGE_HW_RXDM_DBG_RD_ENABLE	vxge_mBIT(31)
/*0x04840*/	u64	rxdm_dbg_rd_data;
#define VXGE_HW_RXDM_DBG_RD_DATA_RMC_RXDM_DBG_RD_DATA(val) vxge_vBIT(val, 0, 64)
/*0x04848*/	u64	rqa_top_prty_for_vh[17];
#define VXGE_HW_RQA_TOP_PRTY_FOR_VH_RQA_TOP_PRTY_FOR_VH(val) \
							vxge_vBIT(val, 59, 5)
	u8	unused04900[0x04900-0x048d0];

/*0x04900*/	u64	tim_status;
#define	VXGE_HW_TIM_STATUS_TIM_RESET_IN_PROGRESS	vxge_mBIT(0)
/*0x04908*/	u64	tim_ecc_enable;
#define	VXGE_HW_TIM_ECC_ENABLE_VBLS_N	vxge_mBIT(7)
#define	VXGE_HW_TIM_ECC_ENABLE_BMAP_N	vxge_mBIT(15)
#define	VXGE_HW_TIM_ECC_ENABLE_BMAP_MSG_N	vxge_mBIT(23)
/*0x04910*/	u64	tim_bp_ctrl;
#define	VXGE_HW_TIM_BP_CTRL_RD_XON	vxge_mBIT(7)
#define	VXGE_HW_TIM_BP_CTRL_WR_XON	vxge_mBIT(15)
#define	VXGE_HW_TIM_BP_CTRL_ROCRC_BYP	vxge_mBIT(23)
/*0x04918*/	u64	tim_resource_assignment_vh[17];
#define VXGE_HW_TIM_RESOURCE_ASSIGNMENT_VH_BMAP_ROOT(val) vxge_vBIT(val, 0, 32)
/*0x049a0*/	u64	tim_bmap_mapping_vp_err[17];
#define VXGE_HW_TIM_BMAP_MAPPING_VP_ERR_TIM_DEST_VPATH(val) vxge_vBIT(val, 3, 5)
	u8	unused04b00[0x04b00-0x04a28];

/*0x04b00*/	u64	gcmg2_int_status;
#define	VXGE_HW_GCMG2_INT_STATUS_GXTMC_ERR_GXTMC_INT	vxge_mBIT(7)
#define	VXGE_HW_GCMG2_INT_STATUS_GCP_ERR_GCP_INT	vxge_mBIT(15)
#define	VXGE_HW_GCMG2_INT_STATUS_CMC_ERR_CMC_INT	vxge_mBIT(23)
/*0x04b08*/	u64	gcmg2_int_mask;
/*0x04b10*/	u64	gxtmc_err_reg;
#define VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_MEM_DB_ERR(val) vxge_vBIT(val, 0, 4)
#define VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_MEM_SG_ERR(val) vxge_vBIT(val, 4, 4)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_CMC_RD_DATA_DB_ERR	vxge_mBIT(8)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_REQ_FIFO_ERR	vxge_mBIT(9)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_REQ_DATA_FIFO_ERR	vxge_mBIT(10)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_WR_RSP_FIFO_ERR	vxge_mBIT(11)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_RD_RSP_FIFO_ERR	vxge_mBIT(12)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_CMI_WRP_FIFO_ERR	vxge_mBIT(13)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_CMI_WRP_ERR	vxge_mBIT(14)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_CMI_RRP_FIFO_ERR	vxge_mBIT(15)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_CMI_RRP_ERR	vxge_mBIT(16)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_CMI_DATA_SM_ERR	vxge_mBIT(17)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_CMI_CMC0_IF_ERR	vxge_mBIT(18)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_ARB_SM_ERR	vxge_mBIT(19)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_CFC_SM_ERR	vxge_mBIT(20)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_DFETCH_CREDIT_OVERFLOW \
							vxge_mBIT(21)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_DFETCH_CREDIT_UNDERFLOW \
							vxge_mBIT(22)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_DFETCH_SM_ERR	vxge_mBIT(23)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_RCTRL_CREDIT_OVERFLOW \
							vxge_mBIT(24)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_RCTRL_CREDIT_UNDERFLOW \
							vxge_mBIT(25)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_RCTRL_SM_ERR	vxge_mBIT(26)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_WCOMPL_SM_ERR	vxge_mBIT(27)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_WCOMPL_TAG_ERR	vxge_mBIT(28)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_WREQ_SM_ERR	vxge_mBIT(29)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_BDT_CMI_WREQ_FIFO_ERR	vxge_mBIT(30)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_CP2BDT_RFIFO_POP_ERR	vxge_mBIT(31)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_XTMC_BDT_CMI_OP_ERR	vxge_mBIT(32)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_XTMC_BDT_DFETCH_OP_ERR	vxge_mBIT(33)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_XTMC_BDT_DFIFO_ERR	vxge_mBIT(34)
#define	VXGE_HW_GXTMC_ERR_REG_XTMC_CMI_ARB_SM_ERR	vxge_mBIT(35)
/*0x04b18*/	u64	gxtmc_err_mask;
/*0x04b20*/	u64	gxtmc_err_alarm;
/*0x04b28*/	u64	cmc_err_reg;
#define	VXGE_HW_CMC_ERR_REG_CMC_CMC_SM_ERR	vxge_mBIT(0)
/*0x04b30*/	u64	cmc_err_mask;
/*0x04b38*/	u64	cmc_err_alarm;
/*0x04b40*/	u64	gcp_err_reg;
#define	VXGE_HW_GCP_ERR_REG_CP_H2L2CP_FIFO_ERR	vxge_mBIT(0)
#define	VXGE_HW_GCP_ERR_REG_CP_STC2CP_FIFO_ERR	vxge_mBIT(1)
#define	VXGE_HW_GCP_ERR_REG_CP_STE2CP_FIFO_ERR	vxge_mBIT(2)
#define	VXGE_HW_GCP_ERR_REG_CP_TTE2CP_FIFO_ERR	vxge_mBIT(3)
/*0x04b48*/	u64	gcp_err_mask;
/*0x04b50*/	u64	gcp_err_alarm;
	u8	unused04f00[0x04f00-0x04b58];

/*0x04f00*/	u64	pcmg2_int_status;
#define	VXGE_HW_PCMG2_INT_STATUS_PXTMC_ERR_PXTMC_INT	vxge_mBIT(7)
#define	VXGE_HW_PCMG2_INT_STATUS_CP_EXC_CP_XT_EXC_INT	vxge_mBIT(15)
#define	VXGE_HW_PCMG2_INT_STATUS_CP_ERR_CP_ERR_INT	vxge_mBIT(23)
/*0x04f08*/	u64	pcmg2_int_mask;
/*0x04f10*/	u64	pxtmc_err_reg;
#define VXGE_HW_PXTMC_ERR_REG_XTMC_XT_PIF_SRAM_DB_ERR(val) vxge_vBIT(val, 0, 2)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MPT_REQ_FIFO_ERR	vxge_mBIT(2)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MPT_PRSP_FIFO_ERR	vxge_mBIT(3)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MPT_WRSP_FIFO_ERR	vxge_mBIT(4)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UPT_REQ_FIFO_ERR	vxge_mBIT(5)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UPT_PRSP_FIFO_ERR	vxge_mBIT(6)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UPT_WRSP_FIFO_ERR	vxge_mBIT(7)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CPT_REQ_FIFO_ERR	vxge_mBIT(8)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CPT_PRSP_FIFO_ERR	vxge_mBIT(9)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CPT_WRSP_FIFO_ERR	vxge_mBIT(10)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_REQ_FIFO_ERR	vxge_mBIT(11)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_REQ_DATA_FIFO_ERR	vxge_mBIT(12)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_WR_RSP_FIFO_ERR	vxge_mBIT(13)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_RD_RSP_FIFO_ERR	vxge_mBIT(14)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MPT_REQ_SHADOW_ERR	vxge_mBIT(15)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MPT_RSP_SHADOW_ERR	vxge_mBIT(16)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UPT_REQ_SHADOW_ERR	vxge_mBIT(17)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UPT_RSP_SHADOW_ERR	vxge_mBIT(18)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CPT_REQ_SHADOW_ERR	vxge_mBIT(19)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CPT_RSP_SHADOW_ERR	vxge_mBIT(20)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_XIL_SHADOW_ERR	vxge_mBIT(21)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_ARB_SHADOW_ERR	vxge_mBIT(22)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_RAM_SHADOW_ERR	vxge_mBIT(23)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CMW_SHADOW_ERR	vxge_mBIT(24)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CMR_SHADOW_ERR	vxge_mBIT(25)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MPT_REQ_FSM_ERR	vxge_mBIT(26)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MPT_RSP_FSM_ERR	vxge_mBIT(27)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UPT_REQ_FSM_ERR	vxge_mBIT(28)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UPT_RSP_FSM_ERR	vxge_mBIT(29)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CPT_REQ_FSM_ERR	vxge_mBIT(30)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CPT_RSP_FSM_ERR	vxge_mBIT(31)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_XIL_FSM_ERR	vxge_mBIT(32)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_ARB_FSM_ERR	vxge_mBIT(33)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CMW_FSM_ERR	vxge_mBIT(34)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CMR_FSM_ERR	vxge_mBIT(35)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MXP_RD_PROT_ERR	vxge_mBIT(36)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UXP_RD_PROT_ERR	vxge_mBIT(37)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CXP_RD_PROT_ERR	vxge_mBIT(38)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MXP_WR_PROT_ERR	vxge_mBIT(39)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UXP_WR_PROT_ERR	vxge_mBIT(40)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CXP_WR_PROT_ERR	vxge_mBIT(41)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MXP_INV_ADDR_ERR	vxge_mBIT(42)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UXP_INV_ADDR_ERR	vxge_mBIT(43)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CXP_INV_ADDR_ERR	vxge_mBIT(44)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MXP_RD_PROT_INFO_ERR	vxge_mBIT(45)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UXP_RD_PROT_INFO_ERR	vxge_mBIT(46)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CXP_RD_PROT_INFO_ERR	vxge_mBIT(47)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MXP_WR_PROT_INFO_ERR	vxge_mBIT(48)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UXP_WR_PROT_INFO_ERR	vxge_mBIT(49)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CXP_WR_PROT_INFO_ERR	vxge_mBIT(50)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_MXP_INV_ADDR_INFO_ERR	vxge_mBIT(51)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_UXP_INV_ADDR_INFO_ERR	vxge_mBIT(52)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CXP_INV_ADDR_INFO_ERR	vxge_mBIT(53)
#define VXGE_HW_PXTMC_ERR_REG_XTMC_XT_PIF_SRAM_SG_ERR(val) vxge_vBIT(val, 54, 2)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CP2BDT_DFIFO_PUSH_ERR	vxge_mBIT(56)
#define	VXGE_HW_PXTMC_ERR_REG_XTMC_CP2BDT_RFIFO_PUSH_ERR	vxge_mBIT(57)
/*0x04f18*/	u64	pxtmc_err_mask;
/*0x04f20*/	u64	pxtmc_err_alarm;
/*0x04f28*/	u64	cp_err_reg;
#define VXGE_HW_CP_ERR_REG_CP_CP_DCACHE_SG_ERR(val) vxge_vBIT(val, 0, 8)
#define VXGE_HW_CP_ERR_REG_CP_CP_ICACHE_SG_ERR(val) vxge_vBIT(val, 8, 2)
#define	VXGE_HW_CP_ERR_REG_CP_CP_DTAG_SG_ERR	vxge_mBIT(10)
#define	VXGE_HW_CP_ERR_REG_CP_CP_ITAG_SG_ERR	vxge_mBIT(11)
#define	VXGE_HW_CP_ERR_REG_CP_CP_TRACE_SG_ERR	vxge_mBIT(12)
#define	VXGE_HW_CP_ERR_REG_CP_DMA2CP_SG_ERR	vxge_mBIT(13)
#define	VXGE_HW_CP_ERR_REG_CP_MP2CP_SG_ERR	vxge_mBIT(14)
#define	VXGE_HW_CP_ERR_REG_CP_QCC2CP_SG_ERR	vxge_mBIT(15)
#define VXGE_HW_CP_ERR_REG_CP_STC2CP_SG_ERR(val) vxge_vBIT(val, 16, 2)
#define VXGE_HW_CP_ERR_REG_CP_CP_DCACHE_DB_ERR(val) vxge_vBIT(val, 24, 8)
#define VXGE_HW_CP_ERR_REG_CP_CP_ICACHE_DB_ERR(val) vxge_vBIT(val, 32, 2)
#define	VXGE_HW_CP_ERR_REG_CP_CP_DTAG_DB_ERR	vxge_mBIT(34)
#define	VXGE_HW_CP_ERR_REG_CP_CP_ITAG_DB_ERR	vxge_mBIT(35)
#define	VXGE_HW_CP_ERR_REG_CP_CP_TRACE_DB_ERR	vxge_mBIT(36)
#define	VXGE_HW_CP_ERR_REG_CP_DMA2CP_DB_ERR	vxge_mBIT(37)
#define	VXGE_HW_CP_ERR_REG_CP_MP2CP_DB_ERR	vxge_mBIT(38)
#define	VXGE_HW_CP_ERR_REG_CP_QCC2CP_DB_ERR	vxge_mBIT(39)
#define VXGE_HW_CP_ERR_REG_CP_STC2CP_DB_ERR(val) vxge_vBIT(val, 40, 2)
#define	VXGE_HW_CP_ERR_REG_CP_H2L2CP_FIFO_ERR	vxge_mBIT(48)
#define	VXGE_HW_CP_ERR_REG_CP_STC2CP_FIFO_ERR	vxge_mBIT(49)
#define	VXGE_HW_CP_ERR_REG_CP_STE2CP_FIFO_ERR	vxge_mBIT(50)
#define	VXGE_HW_CP_ERR_REG_CP_TTE2CP_FIFO_ERR	vxge_mBIT(51)
#define	VXGE_HW_CP_ERR_REG_CP_SWIF2CP_FIFO_ERR	vxge_mBIT(52)
#define	VXGE_HW_CP_ERR_REG_CP_CP2DMA_FIFO_ERR	vxge_mBIT(53)
#define	VXGE_HW_CP_ERR_REG_CP_DAM2CP_FIFO_ERR	vxge_mBIT(54)
#define	VXGE_HW_CP_ERR_REG_CP_MP2CP_FIFO_ERR	vxge_mBIT(55)
#define	VXGE_HW_CP_ERR_REG_CP_QCC2CP_FIFO_ERR	vxge_mBIT(56)
#define	VXGE_HW_CP_ERR_REG_CP_DMA2CP_FIFO_ERR	vxge_mBIT(57)
#define	VXGE_HW_CP_ERR_REG_CP_CP_WAKE_FSM_INTEGRITY_ERR	vxge_mBIT(60)
#define	VXGE_HW_CP_ERR_REG_CP_CP_PMON_FSM_INTEGRITY_ERR	vxge_mBIT(61)
#define	VXGE_HW_CP_ERR_REG_CP_DMA_RD_SHADOW_ERR	vxge_mBIT(62)
#define	VXGE_HW_CP_ERR_REG_CP_PIFT_CREDIT_ERR	vxge_mBIT(63)
/*0x04f30*/	u64	cp_err_mask;
/*0x04f38*/	u64	cp_err_alarm;
	u8	unused04fe8[0x04f50-0x04f40];

/*0x04f50*/	u64	cp_exc_reg;
#define	VXGE_HW_CP_EXC_REG_CP_CP_CAUSE_INFO_INT	vxge_mBIT(47)
#define	VXGE_HW_CP_EXC_REG_CP_CP_CAUSE_CRIT_INT	vxge_mBIT(55)
#define	VXGE_HW_CP_EXC_REG_CP_CP_SERR	vxge_mBIT(63)
/*0x04f58*/	u64	cp_exc_mask;
/*0x04f60*/	u64	cp_exc_alarm;
/*0x04f68*/	u64	cp_exc_cause;
#define VXGE_HW_CP_EXC_CAUSE_CP_CP_CAUSE(val) vxge_vBIT(val, 32, 32)
	u8	unused05200[0x05200-0x04f70];

/*0x05200*/	u64	msg_int_status;
#define	VXGE_HW_MSG_INT_STATUS_TIM_ERR_TIM_INT	vxge_mBIT(7)
#define	VXGE_HW_MSG_INT_STATUS_MSG_EXC_MSG_XT_EXC_INT	vxge_mBIT(60)
#define	VXGE_HW_MSG_INT_STATUS_MSG_ERR3_MSG_ERR3_INT	vxge_mBIT(61)
#define	VXGE_HW_MSG_INT_STATUS_MSG_ERR2_MSG_ERR2_INT	vxge_mBIT(62)
#define	VXGE_HW_MSG_INT_STATUS_MSG_ERR_MSG_ERR_INT	vxge_mBIT(63)
/*0x05208*/	u64	msg_int_mask;
/*0x05210*/	u64	tim_err_reg;
#define	VXGE_HW_TIM_ERR_REG_TIM_VBLS_SG_ERR	vxge_mBIT(4)
#define	VXGE_HW_TIM_ERR_REG_TIM_BMAP_PA_SG_ERR	vxge_mBIT(5)
#define	VXGE_HW_TIM_ERR_REG_TIM_BMAP_PB_SG_ERR	vxge_mBIT(6)
#define	VXGE_HW_TIM_ERR_REG_TIM_BMAP_MSG_SG_ERR	vxge_mBIT(7)
#define	VXGE_HW_TIM_ERR_REG_TIM_VBLS_DB_ERR	vxge_mBIT(12)
#define	VXGE_HW_TIM_ERR_REG_TIM_BMAP_PA_DB_ERR	vxge_mBIT(13)
#define	VXGE_HW_TIM_ERR_REG_TIM_BMAP_PB_DB_ERR	vxge_mBIT(14)
#define	VXGE_HW_TIM_ERR_REG_TIM_BMAP_MSG_DB_ERR	vxge_mBIT(15)
#define	VXGE_HW_TIM_ERR_REG_TIM_BMAP_MEM_CNTRL_SM_ERR	vxge_mBIT(18)
#define	VXGE_HW_TIM_ERR_REG_TIM_BMAP_MSG_MEM_CNTRL_SM_ERR	vxge_mBIT(19)
#define	VXGE_HW_TIM_ERR_REG_TIM_MPIF_PCIWR_ERR	vxge_mBIT(20)
#define	VXGE_HW_TIM_ERR_REG_TIM_ROCRC_BMAP_UPDT_FIFO_ERR	vxge_mBIT(22)
#define	VXGE_HW_TIM_ERR_REG_TIM_CREATE_BMAPMSG_FIFO_ERR	vxge_mBIT(23)
#define	VXGE_HW_TIM_ERR_REG_TIM_ROCRCIF_MISMATCH	vxge_mBIT(46)
#define VXGE_HW_TIM_ERR_REG_TIM_BMAP_MAPPING_VP_ERR(n)	vxge_mBIT(n)
/*0x05218*/	u64	tim_err_mask;
/*0x05220*/	u64	tim_err_alarm;
/*0x05228*/	u64	msg_err_reg;
#define	VXGE_HW_MSG_ERR_REG_UP_UXP_WAKE_FSM_INTEGRITY_ERR	vxge_mBIT(0)
#define	VXGE_HW_MSG_ERR_REG_MP_MXP_WAKE_FSM_INTEGRITY_ERR	vxge_mBIT(1)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_DMQ_DMA_READ_CMD_FSM_INTEGRITY_ERR \
								vxge_mBIT(2)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_DMQ_DMA_RESP_FSM_INTEGRITY_ERR \
								vxge_mBIT(3)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_DMQ_OWN_FSM_INTEGRITY_ERR	vxge_mBIT(4)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_PDA_ACC_FSM_INTEGRITY_ERR	vxge_mBIT(5)
#define	VXGE_HW_MSG_ERR_REG_MP_MXP_PMON_FSM_INTEGRITY_ERR	vxge_mBIT(6)
#define	VXGE_HW_MSG_ERR_REG_UP_UXP_PMON_FSM_INTEGRITY_ERR	vxge_mBIT(7)
#define	VXGE_HW_MSG_ERR_REG_UP_UXP_DTAG_SG_ERR	vxge_mBIT(8)
#define	VXGE_HW_MSG_ERR_REG_UP_UXP_ITAG_SG_ERR	vxge_mBIT(10)
#define	VXGE_HW_MSG_ERR_REG_MP_MXP_DTAG_SG_ERR	vxge_mBIT(12)
#define	VXGE_HW_MSG_ERR_REG_MP_MXP_ITAG_SG_ERR	vxge_mBIT(14)
#define	VXGE_HW_MSG_ERR_REG_UP_UXP_TRACE_SG_ERR	vxge_mBIT(16)
#define	VXGE_HW_MSG_ERR_REG_MP_MXP_TRACE_SG_ERR	vxge_mBIT(17)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_CMG2MSG_SG_ERR	vxge_mBIT(18)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_TXPE2MSG_SG_ERR	vxge_mBIT(19)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_RXPE2MSG_SG_ERR	vxge_mBIT(20)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_RPE2MSG_SG_ERR	vxge_mBIT(21)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_UMQ_SG_ERR	vxge_mBIT(26)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_BWR_PF_SG_ERR	vxge_mBIT(27)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_DMQ_ECC_SG_ERR	vxge_mBIT(29)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_DMA_RESP_ECC_SG_ERR	vxge_mBIT(31)
#define	VXGE_HW_MSG_ERR_REG_MSG_XFMDQRY_FSM_INTEGRITY_ERR	vxge_mBIT(33)
#define	VXGE_HW_MSG_ERR_REG_MSG_FRMQRY_FSM_INTEGRITY_ERR	vxge_mBIT(34)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_UMQ_WRITE_FSM_INTEGRITY_ERR	vxge_mBIT(35)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_UMQ_BWR_PF_FSM_INTEGRITY_ERR \
								vxge_mBIT(36)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_REG_RESP_FIFO_ERR	vxge_mBIT(38)
#define	VXGE_HW_MSG_ERR_REG_UP_UXP_DTAG_DB_ERR	vxge_mBIT(39)
#define	VXGE_HW_MSG_ERR_REG_UP_UXP_ITAG_DB_ERR	vxge_mBIT(41)
#define	VXGE_HW_MSG_ERR_REG_MP_MXP_DTAG_DB_ERR	vxge_mBIT(43)
#define	VXGE_HW_MSG_ERR_REG_MP_MXP_ITAG_DB_ERR	vxge_mBIT(45)
#define	VXGE_HW_MSG_ERR_REG_UP_UXP_TRACE_DB_ERR	vxge_mBIT(47)
#define	VXGE_HW_MSG_ERR_REG_MP_MXP_TRACE_DB_ERR	vxge_mBIT(48)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_CMG2MSG_DB_ERR	vxge_mBIT(49)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_TXPE2MSG_DB_ERR	vxge_mBIT(50)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_RXPE2MSG_DB_ERR	vxge_mBIT(51)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_RPE2MSG_DB_ERR	vxge_mBIT(52)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_REG_READ_FIFO_ERR	vxge_mBIT(53)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_MXP2UXP_FIFO_ERR	vxge_mBIT(54)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_KDFC_SIF_FIFO_ERR	vxge_mBIT(55)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_CXP2SWIF_FIFO_ERR	vxge_mBIT(56)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_UMQ_DB_ERR	vxge_mBIT(57)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_BWR_PF_DB_ERR	vxge_mBIT(58)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_BWR_SIF_FIFO_ERR	vxge_mBIT(59)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_DMQ_ECC_DB_ERR	vxge_mBIT(60)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_DMA_READ_FIFO_ERR	vxge_mBIT(61)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_DMA_RESP_ECC_DB_ERR	vxge_mBIT(62)
#define	VXGE_HW_MSG_ERR_REG_MSG_QUE_UXP2MXP_FIFO_ERR	vxge_mBIT(63)
/*0x05230*/	u64	msg_err_mask;
/*0x05238*/	u64	msg_err_alarm;
	u8	unused05340[0x05340-0x05240];

/*0x05340*/	u64	msg_exc_reg;
#define	VXGE_HW_MSG_EXC_REG_MP_MXP_CAUSE_INFO_INT	vxge_mBIT(50)
#define	VXGE_HW_MSG_EXC_REG_MP_MXP_CAUSE_CRIT_INT	vxge_mBIT(51)
#define	VXGE_HW_MSG_EXC_REG_UP_UXP_CAUSE_INFO_INT	vxge_mBIT(54)
#define	VXGE_HW_MSG_EXC_REG_UP_UXP_CAUSE_CRIT_INT	vxge_mBIT(55)
#define	VXGE_HW_MSG_EXC_REG_MP_MXP_SERR	vxge_mBIT(62)
#define	VXGE_HW_MSG_EXC_REG_UP_UXP_SERR	vxge_mBIT(63)
/*0x05348*/	u64	msg_exc_mask;
/*0x05350*/	u64	msg_exc_alarm;
/*0x05358*/	u64	msg_exc_cause;
#define VXGE_HW_MSG_EXC_CAUSE_MP_MXP(val) vxge_vBIT(val, 0, 32)
#define VXGE_HW_MSG_EXC_CAUSE_UP_UXP(val) vxge_vBIT(val, 32, 32)
	u8	unused05368[0x05380-0x05360];

/*0x05380*/	u64	msg_err2_reg;
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_CMG2MSG_DISPATCH_FSM_INTEGRITY_ERR \
							vxge_mBIT(0)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_DMQ_DISPATCH_FSM_INTEGRITY_ERR \
								vxge_mBIT(1)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_SWIF_DISPATCH_FSM_INTEGRITY_ERR \
								vxge_mBIT(2)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_PIC_WRITE_FSM_INTEGRITY_ERR \
								vxge_mBIT(3)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_SWIFREG_FSM_INTEGRITY_ERR	vxge_mBIT(4)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_TIM_WRITE_FSM_INTEGRITY_ERR \
								vxge_mBIT(5)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_UMQ_TA_FSM_INTEGRITY_ERR	vxge_mBIT(6)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_TXPE_TA_FSM_INTEGRITY_ERR	vxge_mBIT(7)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_RXPE_TA_FSM_INTEGRITY_ERR	vxge_mBIT(8)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_SWIF_TA_FSM_INTEGRITY_ERR	vxge_mBIT(9)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_DMA_TA_FSM_INTEGRITY_ERR	vxge_mBIT(10)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_CP_TA_FSM_INTEGRITY_ERR	vxge_mBIT(11)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA16_FSM_INTEGRITY_ERR \
							vxge_mBIT(12)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA15_FSM_INTEGRITY_ERR \
							vxge_mBIT(13)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA14_FSM_INTEGRITY_ERR \
							vxge_mBIT(14)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA13_FSM_INTEGRITY_ERR \
							vxge_mBIT(15)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA12_FSM_INTEGRITY_ERR \
							vxge_mBIT(16)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA11_FSM_INTEGRITY_ERR \
							vxge_mBIT(17)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA10_FSM_INTEGRITY_ERR \
							vxge_mBIT(18)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA9_FSM_INTEGRITY_ERR \
							vxge_mBIT(19)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA8_FSM_INTEGRITY_ERR \
							vxge_mBIT(20)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA7_FSM_INTEGRITY_ERR \
							vxge_mBIT(21)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA6_FSM_INTEGRITY_ERR \
							vxge_mBIT(22)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA5_FSM_INTEGRITY_ERR \
							vxge_mBIT(23)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA4_FSM_INTEGRITY_ERR \
							vxge_mBIT(24)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA3_FSM_INTEGRITY_ERR \
							vxge_mBIT(25)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA2_FSM_INTEGRITY_ERR \
							vxge_mBIT(26)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA1_FSM_INTEGRITY_ERR \
							vxge_mBIT(27)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_LONGTERMUMQ_TA0_FSM_INTEGRITY_ERR \
							vxge_mBIT(28)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_FBMC_OWN_FSM_INTEGRITY_ERR	vxge_mBIT(29)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_TXPE2MSG_DISPATCH_FSM_INTEGRITY_ERR \
							vxge_mBIT(30)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_RXPE2MSG_DISPATCH_FSM_INTEGRITY_ERR \
							vxge_mBIT(31)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_RPE2MSG_DISPATCH_FSM_INTEGRITY_ERR \
							vxge_mBIT(32)
#define	VXGE_HW_MSG_ERR2_REG_MP_MP_PIFT_IF_CREDIT_CNT_ERR	vxge_mBIT(33)
#define	VXGE_HW_MSG_ERR2_REG_UP_UP_PIFT_IF_CREDIT_CNT_ERR	vxge_mBIT(34)
#define	VXGE_HW_MSG_ERR2_REG_MSG_QUE_UMQ2PIC_CMD_FIFO_ERR	vxge_mBIT(62)
#define	VXGE_HW_MSG_ERR2_REG_TIM_TIM2MSG_CMD_FIFO_ERR	vxge_mBIT(63)
/*0x05388*/	u64	msg_err2_mask;
/*0x05390*/	u64	msg_err2_alarm;
/*0x05398*/	u64	msg_err3_reg;
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR0	vxge_mBIT(0)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR1	vxge_mBIT(1)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR2	vxge_mBIT(2)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR3	vxge_mBIT(3)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR4	vxge_mBIT(4)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR5	vxge_mBIT(5)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR6	vxge_mBIT(6)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_SG_ERR7	vxge_mBIT(7)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_ICACHE_SG_ERR0	vxge_mBIT(8)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_ICACHE_SG_ERR1	vxge_mBIT(9)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR0	vxge_mBIT(16)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR1	vxge_mBIT(17)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR2	vxge_mBIT(18)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR3	vxge_mBIT(19)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR4	vxge_mBIT(20)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR5	vxge_mBIT(21)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR6	vxge_mBIT(22)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_SG_ERR7	vxge_mBIT(23)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_ICACHE_SG_ERR0	vxge_mBIT(24)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_ICACHE_SG_ERR1	vxge_mBIT(25)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR0	vxge_mBIT(32)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR1	vxge_mBIT(33)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR2	vxge_mBIT(34)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR3	vxge_mBIT(35)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR4	vxge_mBIT(36)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR5	vxge_mBIT(37)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR6	vxge_mBIT(38)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_DCACHE_DB_ERR7	vxge_mBIT(39)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_ICACHE_DB_ERR0	vxge_mBIT(40)
#define	VXGE_HW_MSG_ERR3_REG_UP_UXP_ICACHE_DB_ERR1	vxge_mBIT(41)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR0	vxge_mBIT(48)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR1	vxge_mBIT(49)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR2	vxge_mBIT(50)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR3	vxge_mBIT(51)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR4	vxge_mBIT(52)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR5	vxge_mBIT(53)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR6	vxge_mBIT(54)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_DCACHE_DB_ERR7	vxge_mBIT(55)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_ICACHE_DB_ERR0	vxge_mBIT(56)
#define	VXGE_HW_MSG_ERR3_REG_MP_MXP_ICACHE_DB_ERR1	vxge_mBIT(57)
/*0x053a0*/	u64	msg_err3_mask;
/*0x053a8*/	u64	msg_err3_alarm;
	u8	unused05600[0x05600-0x053b0];

/*0x05600*/	u64	fau_gen_err_reg;
#define	VXGE_HW_FAU_GEN_ERR_REG_FMPF_PORT0_PERMANENT_STOP	vxge_mBIT(3)
#define	VXGE_HW_FAU_GEN_ERR_REG_FMPF_PORT1_PERMANENT_STOP	vxge_mBIT(7)
#define	VXGE_HW_FAU_GEN_ERR_REG_FMPF_PORT2_PERMANENT_STOP	vxge_mBIT(11)
#define	VXGE_HW_FAU_GEN_ERR_REG_FALR_AUTO_LRO_NOTIFICATION	vxge_mBIT(15)
/*0x05608*/	u64	fau_gen_err_mask;
/*0x05610*/	u64	fau_gen_err_alarm;
/*0x05618*/	u64	fau_ecc_err_reg;
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT0_FAU_MAC2F_N_SG_ERR	vxge_mBIT(0)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT0_FAU_MAC2F_N_DB_ERR	vxge_mBIT(1)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT0_FAU_MAC2F_W_SG_ERR(val) \
							vxge_vBIT(val, 2, 2)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT0_FAU_MAC2F_W_DB_ERR(val) \
							vxge_vBIT(val, 4, 2)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT1_FAU_MAC2F_N_SG_ERR	vxge_mBIT(6)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT1_FAU_MAC2F_N_DB_ERR	vxge_mBIT(7)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT1_FAU_MAC2F_W_SG_ERR(val) \
							vxge_vBIT(val, 8, 2)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT1_FAU_MAC2F_W_DB_ERR(val) \
							vxge_vBIT(val, 10, 2)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT2_FAU_MAC2F_N_SG_ERR	vxge_mBIT(12)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT2_FAU_MAC2F_N_DB_ERR	vxge_mBIT(13)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT2_FAU_MAC2F_W_SG_ERR(val) \
							vxge_vBIT(val, 14, 2)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAU_PORT2_FAU_MAC2F_W_DB_ERR(val) \
							vxge_vBIT(val, 16, 2)
#define VXGE_HW_FAU_ECC_ERR_REG_FAU_FAU_XFMD_INS_SG_ERR(val) \
							vxge_vBIT(val, 18, 2)
#define VXGE_HW_FAU_ECC_ERR_REG_FAU_FAU_XFMD_INS_DB_ERR(val) \
							vxge_vBIT(val, 20, 2)
#define	VXGE_HW_FAU_ECC_ERR_REG_FAUJ_FAU_FSM_ERR	vxge_mBIT(31)
/*0x05620*/	u64	fau_ecc_err_mask;
/*0x05628*/	u64	fau_ecc_err_alarm;
	u8	unused05658[0x05658-0x05630];
/*0x05658*/	u64	fau_pa_cfg;
#define	VXGE_HW_FAU_PA_CFG_REPL_L4_COMP_CSUM	vxge_mBIT(3)
#define	VXGE_HW_FAU_PA_CFG_REPL_L3_INCL_CF	vxge_mBIT(7)
#define	VXGE_HW_FAU_PA_CFG_REPL_L3_COMP_CSUM	vxge_mBIT(11)
	u8	unused05668[0x05668-0x05660];

/*0x05668*/	u64	dbg_stats_fau_rx_path;
#define	VXGE_HW_DBG_STATS_FAU_RX_PATH_RX_PERMITTED_FRMS(val) \
						vxge_vBIT(val, 32, 32)
	u8	unused056c0[0x056c0-0x05670];

/*0x056c0*/	u64	fau_lag_cfg;
#define VXGE_HW_FAU_LAG_CFG_COLL_ALG(val) vxge_vBIT(val, 2, 2)
#define	VXGE_HW_FAU_LAG_CFG_INCR_RX_AGGR_STATS	vxge_mBIT(7)
	u8	unused05800[0x05800-0x056c8];

/*0x05800*/	u64	tpa_int_status;
#define	VXGE_HW_TPA_INT_STATUS_ORP_ERR_ORP_INT	vxge_mBIT(15)
#define	VXGE_HW_TPA_INT_STATUS_PTM_ALARM_PTM_INT	vxge_mBIT(23)
#define	VXGE_HW_TPA_INT_STATUS_TPA_ERROR_TPA_INT	vxge_mBIT(31)
/*0x05808*/	u64	tpa_int_mask;
/*0x05810*/	u64	orp_err_reg;
#define	VXGE_HW_ORP_ERR_REG_ORP_FIFO_SG_ERR	vxge_mBIT(3)
#define	VXGE_HW_ORP_ERR_REG_ORP_FIFO_DB_ERR	vxge_mBIT(7)
#define	VXGE_HW_ORP_ERR_REG_ORP_XFMD_FIFO_UFLOW_ERR	vxge_mBIT(11)
#define	VXGE_HW_ORP_ERR_REG_ORP_FRM_FIFO_UFLOW_ERR	vxge_mBIT(15)
#define	VXGE_HW_ORP_ERR_REG_ORP_XFMD_RCV_FSM_ERR	vxge_mBIT(19)
#define	VXGE_HW_ORP_ERR_REG_ORP_OUTREAD_FSM_ERR	vxge_mBIT(23)
#define	VXGE_HW_ORP_ERR_REG_ORP_OUTQEM_FSM_ERR	vxge_mBIT(27)
#define	VXGE_HW_ORP_ERR_REG_ORP_XFMD_RCV_SHADOW_ERR	vxge_mBIT(31)
#define	VXGE_HW_ORP_ERR_REG_ORP_OUTREAD_SHADOW_ERR	vxge_mBIT(35)
#define	VXGE_HW_ORP_ERR_REG_ORP_OUTQEM_SHADOW_ERR	vxge_mBIT(39)
#define	VXGE_HW_ORP_ERR_REG_ORP_OUTFRM_SHADOW_ERR	vxge_mBIT(43)
#define	VXGE_HW_ORP_ERR_REG_ORP_OPTPRS_SHADOW_ERR	vxge_mBIT(47)
/*0x05818*/	u64	orp_err_mask;
/*0x05820*/	u64	orp_err_alarm;
/*0x05828*/	u64	ptm_alarm_reg;
#define	VXGE_HW_PTM_ALARM_REG_PTM_RDCTRL_SYNC_ERR	vxge_mBIT(3)
#define	VXGE_HW_PTM_ALARM_REG_PTM_RDCTRL_FIFO_ERR	vxge_mBIT(7)
#define	VXGE_HW_PTM_ALARM_REG_XFMD_RD_FIFO_ERR	vxge_mBIT(11)
#define	VXGE_HW_PTM_ALARM_REG_WDE2MSR_WR_FIFO_ERR	vxge_mBIT(15)
#define VXGE_HW_PTM_ALARM_REG_PTM_FRMM_ECC_DB_ERR(val) vxge_vBIT(val, 18, 2)
#define VXGE_HW_PTM_ALARM_REG_PTM_FRMM_ECC_SG_ERR(val) vxge_vBIT(val, 22, 2)
/*0x05830*/	u64	ptm_alarm_mask;
/*0x05838*/	u64	ptm_alarm_alarm;
/*0x05840*/	u64	tpa_error_reg;
#define	VXGE_HW_TPA_ERROR_REG_TPA_FSM_ERR_ALARM	vxge_mBIT(3)
#define	VXGE_HW_TPA_ERROR_REG_TPA_TPA_DA_LKUP_PRT0_DB_ERR	vxge_mBIT(7)
#define	VXGE_HW_TPA_ERROR_REG_TPA_TPA_DA_LKUP_PRT0_SG_ERR	vxge_mBIT(11)
/*0x05848*/	u64	tpa_error_mask;
/*0x05850*/	u64	tpa_error_alarm;
/*0x05858*/	u64	tpa_global_cfg;
#define	VXGE_HW_TPA_GLOBAL_CFG_SUPPORT_SNAP_AB_N	vxge_mBIT(7)
#define	VXGE_HW_TPA_GLOBAL_CFG_ECC_ENABLE_N	vxge_mBIT(35)
	u8	unused05868[0x05870-0x05860];

/*0x05870*/	u64	ptm_ecc_cfg;
#define	VXGE_HW_PTM_ECC_CFG_PTM_FRMM_ECC_EN_N	vxge_mBIT(3)
/*0x05878*/	u64	ptm_phase_cfg;
#define	VXGE_HW_PTM_PHASE_CFG_FRMM_WR_PHASE_EN	vxge_mBIT(3)
#define	VXGE_HW_PTM_PHASE_CFG_FRMM_RD_PHASE_EN	vxge_mBIT(7)
	u8	unused05898[0x05898-0x05880];

/*0x05898*/	u64	dbg_stats_tpa_tx_path;
#define	VXGE_HW_DBG_STATS_TPA_TX_PATH_TX_PERMITTED_FRMS(val) \
							vxge_vBIT(val, 32, 32)
	u8	unused05900[0x05900-0x058a0];

/*0x05900*/	u64	tmac_int_status;
#define	VXGE_HW_TMAC_INT_STATUS_TXMAC_GEN_ERR_TXMAC_GEN_INT	vxge_mBIT(3)
#define	VXGE_HW_TMAC_INT_STATUS_TXMAC_ECC_ERR_TXMAC_ECC_INT	vxge_mBIT(7)
/*0x05908*/	u64	tmac_int_mask;
/*0x05910*/	u64	txmac_gen_err_reg;
#define	VXGE_HW_TXMAC_GEN_ERR_REG_TMACJ_PERMANENT_STOP	vxge_mBIT(3)
#define	VXGE_HW_TXMAC_GEN_ERR_REG_TMACJ_NO_VALID_VSPORT	vxge_mBIT(7)
/*0x05918*/	u64	txmac_gen_err_mask;
/*0x05920*/	u64	txmac_gen_err_alarm;
/*0x05928*/	u64	txmac_ecc_err_reg;
#define	VXGE_HW_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2MAC_SG_ERR	vxge_mBIT(3)
#define	VXGE_HW_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2MAC_DB_ERR	vxge_mBIT(7)
#define	VXGE_HW_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2M_SB_SG_ERR	vxge_mBIT(11)
#define	VXGE_HW_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2M_SB_DB_ERR	vxge_mBIT(15)
#define	VXGE_HW_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2M_DA_SG_ERR	vxge_mBIT(19)
#define	VXGE_HW_TXMAC_ECC_ERR_REG_TMACJ_TMAC_TPA2M_DA_DB_ERR	vxge_mBIT(23)
#define	VXGE_HW_TXMAC_ECC_ERR_REG_TMAC_TMAC_PORT0_FSM_ERR	vxge_mBIT(27)
#define	VXGE_HW_TXMAC_ECC_ERR_REG_TMAC_TMAC_PORT1_FSM_ERR	vxge_mBIT(31)
#define	VXGE_HW_TXMAC_ECC_ERR_REG_TMAC_TMAC_PORT2_FSM_ERR	vxge_mBIT(35)
#define	VXGE_HW_TXMAC_ECC_ERR_REG_TMACJ_TMACJ_FSM_ERR	vxge_mBIT(39)
/*0x05930*/	u64	txmac_ecc_err_mask;
/*0x05938*/	u64	txmac_ecc_err_alarm;
	u8	unused05978[0x05978-0x05940];

/*0x05978*/	u64	dbg_stat_tx_any_frms;
#define VXGE_HW_DBG_STAT_TX_ANY_FRMS_PORT0_TX_ANY_FRMS(val) vxge_vBIT(val, 0, 8)
#define VXGE_HW_DBG_STAT_TX_ANY_FRMS_PORT1_TX_ANY_FRMS(val) vxge_vBIT(val, 8, 8)
#define VXGE_HW_DBG_STAT_TX_ANY_FRMS_PORT2_TX_ANY_FRMS(val) \
							vxge_vBIT(val, 16, 8)
	u8	unused059a0[0x059a0-0x05980];

/*0x059a0*/	u64	txmac_link_util_port[3];
#define	VXGE_HW_TXMAC_LINK_UTIL_PORT_TMAC_TMAC_UTILIZATION(val) \
							vxge_vBIT(val, 1, 7)
#define VXGE_HW_TXMAC_LINK_UTIL_PORT_TMAC_UTIL_CFG(val) vxge_vBIT(val, 8, 4)
#define VXGE_HW_TXMAC_LINK_UTIL_PORT_TMAC_TMAC_FRAC_UTIL(val) \
							vxge_vBIT(val, 12, 4)
#define VXGE_HW_TXMAC_LINK_UTIL_PORT_TMAC_PKT_WEIGHT(val) vxge_vBIT(val, 16, 4)
#define	VXGE_HW_TXMAC_LINK_UTIL_PORT_TMAC_TMAC_SCALE_FACTOR	vxge_mBIT(23)
/*0x059b8*/	u64	txmac_cfg0_port[3];
#define	VXGE_HW_TXMAC_CFG0_PORT_TMAC_EN	vxge_mBIT(3)
#define	VXGE_HW_TXMAC_CFG0_PORT_APPEND_PAD	vxge_mBIT(7)
#define VXGE_HW_TXMAC_CFG0_PORT_PAD_BYTE(val) vxge_vBIT(val, 8, 8)
/*0x059d0*/	u64	txmac_cfg1_port[3];
#define VXGE_HW_TXMAC_CFG1_PORT_AVG_IPG(val) vxge_vBIT(val, 40, 8)
/*0x059e8*/	u64	txmac_status_port[3];
#define	VXGE_HW_TXMAC_STATUS_PORT_TMAC_TX_FRM_SENT	vxge_mBIT(3)
	u8	unused05a20[0x05a20-0x05a00];

/*0x05a20*/	u64	lag_distrib_dest;
#define VXGE_HW_LAG_DISTRIB_DEST_MAP_VPATH(n)	vxge_mBIT(n)
/*0x05a28*/	u64	lag_marker_cfg;
#define	VXGE_HW_LAG_MARKER_CFG_GEN_RCVR_EN	vxge_mBIT(3)
#define	VXGE_HW_LAG_MARKER_CFG_RESP_EN	vxge_mBIT(7)
#define VXGE_HW_LAG_MARKER_CFG_RESP_TIMEOUT(val) vxge_vBIT(val, 16, 16)
#define	VXGE_HW_LAG_MARKER_CFG_SLOW_PROTO_MRKR_MIN_INTERVAL(val) \
							vxge_vBIT(val, 32, 16)
#define	VXGE_HW_LAG_MARKER_CFG_THROTTLE_MRKR_RESP	vxge_mBIT(51)
/*0x05a30*/	u64	lag_tx_cfg;
#define	VXGE_HW_LAG_TX_CFG_INCR_TX_AGGR_STATS	vxge_mBIT(3)
#define VXGE_HW_LAG_TX_CFG_DISTRIB_ALG_SEL(val) vxge_vBIT(val, 6, 2)
#define	VXGE_HW_LAG_TX_CFG_DISTRIB_REMAP_IF_FAIL	vxge_mBIT(11)
#define VXGE_HW_LAG_TX_CFG_COLL_MAX_DELAY(val) vxge_vBIT(val, 16, 16)
/*0x05a38*/	u64	lag_tx_status;
#define VXGE_HW_LAG_TX_STATUS_TLAG_TIMER_VAL_EMPTIED_LINK(val) \
							vxge_vBIT(val, 0, 8)
#define	VXGE_HW_LAG_TX_STATUS_TLAG_TIMER_VAL_SLOW_PROTO_MRKR(val) \
							vxge_vBIT(val, 8, 8)
#define	VXGE_HW_LAG_TX_STATUS_TLAG_TIMER_VAL_SLOW_PROTO_MRKRRESP(val) \
							vxge_vBIT(val, 16, 8)
	u8	unused05d48[0x05d48-0x05a40];

/*0x05d48*/	u64	srpcim_to_mrpcim_vplane_rmsg[17];
#define	\
VXGE_HAL_SRPCIM_TO_MRPCIM_VPLANE_RMSG_SWIF_SRPCIM_TO_MRPCIM_VPLANE_RMSG(val)\
 vxge_vBIT(val, 0, 64)
		u8	unused06420[0x06420-0x05dd0];

/*0x06420*/	u64	mrpcim_to_srpcim_vplane_wmsg[17];
#define	VXGE_HW_MRPCIM_TO_SRPCIM_VPLANE_WMSG_MRPCIM_TO_SRPCIM_VPLANE_WMSG(val) \
							vxge_vBIT(val, 0, 64)
/*0x064a8*/	u64	mrpcim_to_srpcim_vplane_wmsg_trig[17];

/*0x06530*/	u64	debug_stats0;
#define VXGE_HW_DEBUG_STATS0_RSTDROP_MSG(val) vxge_vBIT(val, 0, 32)
#define VXGE_HW_DEBUG_STATS0_RSTDROP_CPL(val) vxge_vBIT(val, 32, 32)
/*0x06538*/	u64	debug_stats1;
#define VXGE_HW_DEBUG_STATS1_RSTDROP_CLIENT0(val) vxge_vBIT(val, 0, 32)
#define VXGE_HW_DEBUG_STATS1_RSTDROP_CLIENT1(val) vxge_vBIT(val, 32, 32)
/*0x06540*/	u64	debug_stats2;
#define VXGE_HW_DEBUG_STATS2_RSTDROP_CLIENT2(val) vxge_vBIT(val, 0, 32)
/*0x06548*/	u64	debug_stats3_vplane[17];
#define VXGE_HW_DEBUG_STATS3_VPLANE_DEPL_PH(val) vxge_vBIT(val, 0, 16)
#define VXGE_HW_DEBUG_STATS3_VPLANE_DEPL_NPH(val) vxge_vBIT(val, 16, 16)
#define VXGE_HW_DEBUG_STATS3_VPLANE_DEPL_CPLH(val) vxge_vBIT(val, 32, 16)
/*0x065d0*/	u64	debug_stats4_vplane[17];
#define VXGE_HW_DEBUG_STATS4_VPLANE_DEPL_PD(val) vxge_vBIT(val, 0, 16)
#define VXGE_HW_DEBUG_STATS4_VPLANE_DEPL_NPD(val) vxge_vBIT(val, 16, 16)
#define VXGE_HW_DEBUG_STATS4_VPLANE_DEPL_CPLD(val) vxge_vBIT(val, 32, 16)

	u8	unused07000[0x07000-0x06658];

/*0x07000*/	u64	mrpcim_general_int_status;
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_PIC_INT	vxge_mBIT(0)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_PCI_INT	vxge_mBIT(1)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_RTDMA_INT	vxge_mBIT(2)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_WRDMA_INT	vxge_mBIT(3)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_G3CMCT_INT	vxge_mBIT(4)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_GCMG1_INT	vxge_mBIT(5)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_GCMG2_INT	vxge_mBIT(6)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_GCMG3_INT	vxge_mBIT(7)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_G3CMIFL_INT	vxge_mBIT(8)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_G3CMIFU_INT	vxge_mBIT(9)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_PCMG1_INT	vxge_mBIT(10)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_PCMG2_INT	vxge_mBIT(11)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_PCMG3_INT	vxge_mBIT(12)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_XMAC_INT	vxge_mBIT(13)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_RXMAC_INT	vxge_mBIT(14)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_TMAC_INT	vxge_mBIT(15)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_G3FBIF_INT	vxge_mBIT(16)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_FBMC_INT	vxge_mBIT(17)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_G3FBCT_INT	vxge_mBIT(18)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_TPA_INT	vxge_mBIT(19)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_DRBELL_INT	vxge_mBIT(20)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_ONE_INT	vxge_mBIT(21)
#define	VXGE_HW_MRPCIM_GENERAL_INT_STATUS_MSG_INT	vxge_mBIT(22)
/*0x07008*/	u64	mrpcim_general_int_mask;
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_PIC_INT	vxge_mBIT(0)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_PCI_INT	vxge_mBIT(1)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_RTDMA_INT	vxge_mBIT(2)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_WRDMA_INT	vxge_mBIT(3)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_G3CMCT_INT	vxge_mBIT(4)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_GCMG1_INT	vxge_mBIT(5)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_GCMG2_INT	vxge_mBIT(6)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_GCMG3_INT	vxge_mBIT(7)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_G3CMIFL_INT	vxge_mBIT(8)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_G3CMIFU_INT	vxge_mBIT(9)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_PCMG1_INT	vxge_mBIT(10)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_PCMG2_INT	vxge_mBIT(11)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_PCMG3_INT	vxge_mBIT(12)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_XMAC_INT	vxge_mBIT(13)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_RXMAC_INT	vxge_mBIT(14)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_TMAC_INT	vxge_mBIT(15)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_G3FBIF_INT	vxge_mBIT(16)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_FBMC_INT	vxge_mBIT(17)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_G3FBCT_INT	vxge_mBIT(18)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_TPA_INT	vxge_mBIT(19)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_DRBELL_INT	vxge_mBIT(20)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_ONE_INT	vxge_mBIT(21)
#define	VXGE_HW_MRPCIM_GENERAL_INT_MASK_MSG_INT	vxge_mBIT(22)
/*0x07010*/	u64	mrpcim_ppif_int_status;
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_INI_ERRORS_INI_INT	vxge_mBIT(3)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_DMA_ERRORS_DMA_INT	vxge_mBIT(7)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_TGT_ERRORS_TGT_INT	vxge_mBIT(11)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CONFIG_ERRORS_CONFIG_INT	vxge_mBIT(15)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_CRDT_INT	vxge_mBIT(19)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_PLL_ERRORS_PLL_INT	vxge_mBIT(27)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE0_CRD_INT_VPLANE0_INT\
							vxge_mBIT(31)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE1_CRD_INT_VPLANE1_INT\
							vxge_mBIT(32)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE2_CRD_INT_VPLANE2_INT\
							vxge_mBIT(33)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE3_CRD_INT_VPLANE3_INT\
							vxge_mBIT(34)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE4_CRD_INT_VPLANE4_INT\
							vxge_mBIT(35)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE5_CRD_INT_VPLANE5_INT\
							vxge_mBIT(36)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE6_CRD_INT_VPLANE6_INT\
							vxge_mBIT(37)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE7_CRD_INT_VPLANE7_INT\
							vxge_mBIT(38)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE8_CRD_INT_VPLANE8_INT\
							vxge_mBIT(39)
#define	VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE9_CRD_INT_VPLANE9_INT\
							vxge_mBIT(40)
#define \
VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE10_CRD_INT_VPLANE10_INT \
							vxge_mBIT(41)
#define \
VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE11_CRD_INT_VPLANE11_INT \
							vxge_mBIT(42)
#define	\
VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE12_CRD_INT_VPLANE12_INT \
							vxge_mBIT(43)
#define	\
VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE13_CRD_INT_VPLANE13_INT \
							vxge_mBIT(44)
#define	\
VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE14_CRD_INT_VPLANE14_INT \
							vxge_mBIT(45)
#define	\
VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE15_CRD_INT_VPLANE15_INT \
							vxge_mBIT(46)
#define	\
VXGE_HW_MRPCIM_PPIF_INT_STATUS_CRDT_ERRORS_VPLANE16_CRD_INT_VPLANE16_INT \
							vxge_mBIT(47)
#define	\
VXGE_HW_MRPCIM_PPIF_INT_STATUS_VPATH_TO_MRPCIM_ALARM_VPATH_TO_MRPCIM_ALARM_INT \
							vxge_mBIT(55)
/*0x07018*/	u64	mrpcim_ppif_int_mask;
	u8	unused07028[0x07028-0x07020];

/*0x07028*/	u64	ini_errors_reg;
#define	VXGE_HW_INI_ERRORS_REG_SCPL_CPL_TIMEOUT_UNUSED_TAG	vxge_mBIT(3)
#define	VXGE_HW_INI_ERRORS_REG_SCPL_CPL_TIMEOUT	vxge_mBIT(7)
#define	VXGE_HW_INI_ERRORS_REG_DCPL_FSM_ERR	vxge_mBIT(11)
#define	VXGE_HW_INI_ERRORS_REG_DCPL_POISON	vxge_mBIT(12)
#define	VXGE_HW_INI_ERRORS_REG_DCPL_UNSUPPORTED	vxge_mBIT(15)
#define	VXGE_HW_INI_ERRORS_REG_DCPL_ABORT	vxge_mBIT(19)
#define	VXGE_HW_INI_ERRORS_REG_INI_TLP_ABORT	vxge_mBIT(23)
#define	VXGE_HW_INI_ERRORS_REG_INI_DLLP_ABORT	vxge_mBIT(27)
#define	VXGE_HW_INI_ERRORS_REG_INI_ECRC_ERR	vxge_mBIT(31)
#define	VXGE_HW_INI_ERRORS_REG_INI_BUF_DB_ERR	vxge_mBIT(35)
#define	VXGE_HW_INI_ERRORS_REG_INI_BUF_SG_ERR	vxge_mBIT(39)
#define	VXGE_HW_INI_ERRORS_REG_INI_DATA_OVERFLOW	vxge_mBIT(43)
#define	VXGE_HW_INI_ERRORS_REG_INI_HDR_OVERFLOW	vxge_mBIT(47)
#define	VXGE_HW_INI_ERRORS_REG_INI_MRD_SYS_DROP	vxge_mBIT(51)
#define	VXGE_HW_INI_ERRORS_REG_INI_MWR_SYS_DROP	vxge_mBIT(55)
#define	VXGE_HW_INI_ERRORS_REG_INI_MRD_CLIENT_DROP	vxge_mBIT(59)
#define	VXGE_HW_INI_ERRORS_REG_INI_MWR_CLIENT_DROP	vxge_mBIT(63)
/*0x07030*/	u64	ini_errors_mask;
/*0x07038*/	u64	ini_errors_alarm;
/*0x07040*/	u64	dma_errors_reg;
#define	VXGE_HW_DMA_ERRORS_REG_RDARB_FSM_ERR	vxge_mBIT(3)
#define	VXGE_HW_DMA_ERRORS_REG_WRARB_FSM_ERR	vxge_mBIT(7)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_WRDMA_WR_HDR_OVERFLOW	vxge_mBIT(8)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_WRDMA_WR_HDR_UNDERFLOW	vxge_mBIT(9)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_WRDMA_WR_DATA_OVERFLOW	vxge_mBIT(10)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_WRDMA_WR_DATA_UNDERFLOW	vxge_mBIT(11)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_MSG_WR_HDR_OVERFLOW	vxge_mBIT(12)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_MSG_WR_HDR_UNDERFLOW	vxge_mBIT(13)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_MSG_WR_DATA_OVERFLOW	vxge_mBIT(14)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_MSG_WR_DATA_UNDERFLOW	vxge_mBIT(15)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_STATS_WR_HDR_OVERFLOW	vxge_mBIT(16)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_STATS_WR_HDR_UNDERFLOW	vxge_mBIT(17)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_STATS_WR_DATA_OVERFLOW	vxge_mBIT(18)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_STATS_WR_DATA_UNDERFLOW	vxge_mBIT(19)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_RTDMA_WR_HDR_OVERFLOW	vxge_mBIT(20)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_RTDMA_WR_HDR_UNDERFLOW	vxge_mBIT(21)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_RTDMA_WR_DATA_OVERFLOW	vxge_mBIT(22)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_RTDMA_WR_DATA_UNDERFLOW	vxge_mBIT(23)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_WRDMA_RD_HDR_OVERFLOW	vxge_mBIT(24)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_WRDMA_RD_HDR_UNDERFLOW	vxge_mBIT(25)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_RTDMA_RD_HDR_OVERFLOW	vxge_mBIT(28)
#define	VXGE_HW_DMA_ERRORS_REG_DMA_RTDMA_RD_HDR_UNDERFLOW	vxge_mBIT(29)
#define	VXGE_HW_DMA_ERRORS_REG_DBLGEN_FSM_ERR	vxge_mBIT(32)
#define	VXGE_HW_DMA_ERRORS_REG_DBLGEN_CREDIT_FSM_ERR	vxge_mBIT(33)
#define	VXGE_HW_DMA_ERRORS_REG_DBLGEN_DMA_WRR_SM_ERR	vxge_mBIT(34)
/*0x07048*/	u64	dma_errors_mask;
/*0x07050*/	u64	dma_errors_alarm;
/*0x07058*/	u64	tgt_errors_reg;
#define	VXGE_HW_TGT_ERRORS_REG_TGT_VENDOR_MSG	vxge_mBIT(0)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_MSG_UNLOCK	vxge_mBIT(1)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_ILLEGAL_TLP_BE	vxge_mBIT(2)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_BOOT_WRITE	vxge_mBIT(3)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_PIF_WR_CROSS_QWRANGE	vxge_mBIT(4)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_PIF_READ_CROSS_QWRANGE	vxge_mBIT(5)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_KDFC_READ	vxge_mBIT(6)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_USDC_READ	vxge_mBIT(7)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_USDC_WR_CROSS_QWRANGE	vxge_mBIT(8)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_MSIX_BEYOND_RANGE	vxge_mBIT(9)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_WR_TO_KDFC_POISON	vxge_mBIT(10)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_WR_TO_USDC_POISON	vxge_mBIT(11)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_WR_TO_PIF_POISON	vxge_mBIT(12)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_WR_TO_MSIX_POISON	vxge_mBIT(13)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_WR_TO_MRIOV_POISON	vxge_mBIT(14)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_NOT_MEM_TLP	vxge_mBIT(15)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_UNKNOWN_MEM_TLP	vxge_mBIT(16)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_REQ_FSM_ERR	vxge_mBIT(17)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_CPL_FSM_ERR	vxge_mBIT(18)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_KDFC_PROT_ERR	vxge_mBIT(19)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_SWIF_PROT_ERR	vxge_mBIT(20)
#define	VXGE_HW_TGT_ERRORS_REG_TGT_MRIOV_MEM_MAP_CFG_ERR	vxge_mBIT(21)
/*0x07060*/	u64	tgt_errors_mask;
/*0x07068*/	u64	tgt_errors_alarm;
/*0x07070*/	u64	config_errors_reg;
#define	VXGE_HW_CONFIG_ERRORS_REG_I2C_ILLEGAL_STOP_COND	vxge_mBIT(3)
#define	VXGE_HW_CONFIG_ERRORS_REG_I2C_ILLEGAL_START_COND	vxge_mBIT(7)
#define	VXGE_HW_CONFIG_ERRORS_REG_I2C_EXP_RD_CNT	vxge_mBIT(11)
#define	VXGE_HW_CONFIG_ERRORS_REG_I2C_EXTRA_CYCLE	vxge_mBIT(15)
#define	VXGE_HW_CONFIG_ERRORS_REG_I2C_MAIN_FSM_ERR	vxge_mBIT(19)
#define	VXGE_HW_CONFIG_ERRORS_REG_I2C_REQ_COLLISION	vxge_mBIT(23)
#define	VXGE_HW_CONFIG_ERRORS_REG_I2C_REG_FSM_ERR	vxge_mBIT(27)
#define	VXGE_HW_CONFIG_ERRORS_REG_CFGM_I2C_TIMEOUT	vxge_mBIT(31)
#define	VXGE_HW_CONFIG_ERRORS_REG_RIC_I2C_TIMEOUT	vxge_mBIT(35)
#define	VXGE_HW_CONFIG_ERRORS_REG_CFGM_FSM_ERR	vxge_mBIT(39)
#define	VXGE_HW_CONFIG_ERRORS_REG_RIC_FSM_ERR	vxge_mBIT(43)
#define	VXGE_HW_CONFIG_ERRORS_REG_PIFM_ILLEGAL_ACCESS	vxge_mBIT(47)
#define	VXGE_HW_CONFIG_ERRORS_REG_PIFM_TIMEOUT	vxge_mBIT(51)
#define	VXGE_HW_CONFIG_ERRORS_REG_PIFM_FSM_ERR	vxge_mBIT(55)
#define	VXGE_HW_CONFIG_ERRORS_REG_PIFM_TO_FSM_ERR	vxge_mBIT(59)
#define	VXGE_HW_CONFIG_ERRORS_REG_RIC_RIC_RD_TIMEOUT	vxge_mBIT(63)
/*0x07078*/	u64	config_errors_mask;
/*0x07080*/	u64	config_errors_alarm;
	u8	unused07090[0x07090-0x07088];

/*0x07090*/	u64	crdt_errors_reg;
#define	VXGE_HW_CRDT_ERRORS_REG_WRCRDTARB_FSM_ERR	vxge_mBIT(11)
#define	VXGE_HW_CRDT_ERRORS_REG_WRCRDTARB_INTCTL_ILLEGAL_CRD_DEAL \
							vxge_mBIT(15)
#define	VXGE_HW_CRDT_ERRORS_REG_WRCRDTARB_PDA_ILLEGAL_CRD_DEAL	vxge_mBIT(19)
#define	VXGE_HW_CRDT_ERRORS_REG_WRCRDTARB_PCI_MSG_ILLEGAL_CRD_DEAL \
							vxge_mBIT(23)
#define	VXGE_HW_CRDT_ERRORS_REG_RDCRDTARB_FSM_ERR	vxge_mBIT(35)
#define	VXGE_HW_CRDT_ERRORS_REG_RDCRDTARB_RDA_ILLEGAL_CRD_DEAL	vxge_mBIT(39)
#define	VXGE_HW_CRDT_ERRORS_REG_RDCRDTARB_PDA_ILLEGAL_CRD_DEAL	vxge_mBIT(43)
#define	VXGE_HW_CRDT_ERRORS_REG_RDCRDTARB_DBLGEN_ILLEGAL_CRD_DEAL \
							vxge_mBIT(47)
/*0x07098*/	u64	crdt_errors_mask;
/*0x070a0*/	u64	crdt_errors_alarm;
	u8	unused070b0[0x070b0-0x070a8];

/*0x070b0*/	u64	mrpcim_general_errors_reg;
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_STATSB_FSM_ERR	vxge_mBIT(3)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_XGEN_FSM_ERR	vxge_mBIT(7)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_XMEM_FSM_ERR	vxge_mBIT(11)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_KDFCCTL_FSM_ERR	vxge_mBIT(15)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_MRIOVCTL_FSM_ERR	vxge_mBIT(19)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_SPI_FLSH_ERR	vxge_mBIT(23)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_SPI_IIC_ACK_ERR	vxge_mBIT(27)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_SPI_IIC_CHKSUM_ERR	vxge_mBIT(31)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_INI_SERR_DET	vxge_mBIT(35)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_INTCTL_MSIX_FSM_ERR	vxge_mBIT(39)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_INTCTL_MSI_OVERFLOW	vxge_mBIT(43)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_PPIF_PCI_NOT_FLUSH_DURING_SW_RESET \
							vxge_mBIT(47)
#define	VXGE_HW_MRPCIM_GENERAL_ERRORS_REG_PPIF_SW_RESET_FSM_ERR	vxge_mBIT(51)
/*0x070b8*/	u64	mrpcim_general_errors_mask;
/*0x070c0*/	u64	mrpcim_general_errors_alarm;
	u8	unused070d0[0x070d0-0x070c8];

/*0x070d0*/	u64	pll_errors_reg;
#define	VXGE_HW_PLL_ERRORS_REG_CORE_CMG_PLL_OOL	vxge_mBIT(3)
#define	VXGE_HW_PLL_ERRORS_REG_CORE_FB_PLL_OOL	vxge_mBIT(7)
#define	VXGE_HW_PLL_ERRORS_REG_CORE_X_PLL_OOL	vxge_mBIT(11)
/*0x070d8*/	u64	pll_errors_mask;
/*0x070e0*/	u64	pll_errors_alarm;
/*0x070e8*/	u64	srpcim_to_mrpcim_alarm_reg;
#define	VXGE_HW_SRPCIM_TO_MRPCIM_ALARM_REG_PPIF_SRPCIM_TO_MRPCIM_ALARM(val) \
							vxge_vBIT(val, 0, 17)
/*0x070f0*/	u64	srpcim_to_mrpcim_alarm_mask;
/*0x070f8*/	u64	srpcim_to_mrpcim_alarm_alarm;
/*0x07100*/	u64	vpath_to_mrpcim_alarm_reg;
#define	VXGE_HW_VPATH_TO_MRPCIM_ALARM_REG_PPIF_VPATH_TO_MRPCIM_ALARM(val) \
							vxge_vBIT(val, 0, 17)
/*0x07108*/	u64	vpath_to_mrpcim_alarm_mask;
/*0x07110*/	u64	vpath_to_mrpcim_alarm_alarm;
	u8	unused07128[0x07128-0x07118];

/*0x07128*/	u64	crdt_errors_vplane_reg[17];
#define	VXGE_HW_CRDT_ERRORS_VPLANE_REG_WRCRDTARB_P_H_CONSUME_CRDT_ERR \
							vxge_mBIT(3)
#define	VXGE_HW_CRDT_ERRORS_VPLANE_REG_WRCRDTARB_P_D_CONSUME_CRDT_ERR \
							vxge_mBIT(7)
#define	VXGE_HW_CRDT_ERRORS_VPLANE_REG_WRCRDTARB_P_H_RETURN_CRDT_ERR \
							vxge_mBIT(11)
#define	VXGE_HW_CRDT_ERRORS_VPLANE_REG_WRCRDTARB_P_D_RETURN_CRDT_ERR \
							vxge_mBIT(15)
#define	VXGE_HW_CRDT_ERRORS_VPLANE_REG_RDCRDTARB_NP_H_CONSUME_CRDT_ERR \
							vxge_mBIT(19)
#define	VXGE_HW_CRDT_ERRORS_VPLANE_REG_RDCRDTARB_NP_H_RETURN_CRDT_ERR \
							vxge_mBIT(23)
#define	VXGE_HW_CRDT_ERRORS_VPLANE_REG_RDCRDTARB_TAG_CONSUME_TAG_ERR \
							vxge_mBIT(27)
#define	VXGE_HW_CRDT_ERRORS_VPLANE_REG_RDCRDTARB_TAG_RETURN_TAG_ERR \
							vxge_mBIT(31)
/*0x07130*/	u64	crdt_errors_vplane_mask[17];
/*0x07138*/	u64	crdt_errors_vplane_alarm[17];
	u8	unused072f0[0x072f0-0x072c0];

/*0x072f0*/	u64	mrpcim_rst_in_prog;
#define	VXGE_HW_MRPCIM_RST_IN_PROG_MRPCIM_RST_IN_PROG	vxge_mBIT(7)
/*0x072f8*/	u64	mrpcim_reg_modified;
#define	VXGE_HW_MRPCIM_REG_MODIFIED_MRPCIM_REG_MODIFIED	vxge_mBIT(7)

	u8	unused07378[0x07378-0x07300];

/*0x07378*/	u64	write_arb_pending;
#define	VXGE_HW_WRITE_ARB_PENDING_WRARB_WRDMA	vxge_mBIT(3)
#define	VXGE_HW_WRITE_ARB_PENDING_WRARB_RTDMA	vxge_mBIT(7)
#define	VXGE_HW_WRITE_ARB_PENDING_WRARB_MSG	vxge_mBIT(11)
#define	VXGE_HW_WRITE_ARB_PENDING_WRARB_STATSB	vxge_mBIT(15)
#define	VXGE_HW_WRITE_ARB_PENDING_WRARB_INTCTL	vxge_mBIT(19)
/*0x07380*/	u64	read_arb_pending;
#define	VXGE_HW_READ_ARB_PENDING_RDARB_WRDMA	vxge_mBIT(3)
#define	VXGE_HW_READ_ARB_PENDING_RDARB_RTDMA	vxge_mBIT(7)
#define	VXGE_HW_READ_ARB_PENDING_RDARB_DBLGEN	vxge_mBIT(11)
/*0x07388*/	u64	dmaif_dmadbl_pending;
#define	VXGE_HW_DMAIF_DMADBL_PENDING_DMAIF_WRDMA_WR	vxge_mBIT(0)
#define	VXGE_HW_DMAIF_DMADBL_PENDING_DMAIF_WRDMA_RD	vxge_mBIT(1)
#define	VXGE_HW_DMAIF_DMADBL_PENDING_DMAIF_RTDMA_WR	vxge_mBIT(2)
#define	VXGE_HW_DMAIF_DMADBL_PENDING_DMAIF_RTDMA_RD	vxge_mBIT(3)
#define	VXGE_HW_DMAIF_DMADBL_PENDING_DMAIF_MSG_WR	vxge_mBIT(4)
#define	VXGE_HW_DMAIF_DMADBL_PENDING_DMAIF_STATS_WR	vxge_mBIT(5)
#define	VXGE_HW_DMAIF_DMADBL_PENDING_DBLGEN_IN_PROG(val) \
							vxge_vBIT(val, 13, 51)
/*0x07390*/	u64	wrcrdtarb_status0_vplane[17];
#define	VXGE_HW_WRCRDTARB_STATUS0_VPLANE_WRCRDTARB_ABS_AVAIL_P_H(val) \
							vxge_vBIT(val, 0, 8)
/*0x07418*/	u64	wrcrdtarb_status1_vplane[17];
#define	VXGE_HW_WRCRDTARB_STATUS1_VPLANE_WRCRDTARB_ABS_AVAIL_P_D(val) \
							vxge_vBIT(val, 4, 12)
	u8	unused07500[0x07500-0x074a0];

/*0x07500*/	u64	mrpcim_general_cfg1;
#define	VXGE_HW_MRPCIM_GENERAL_CFG1_CLEAR_SERR	vxge_mBIT(7)
/*0x07508*/	u64	mrpcim_general_cfg2;
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_INS_TX_WR_TD	vxge_mBIT(3)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_INS_TX_RD_TD	vxge_mBIT(7)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_INS_TX_CPL_TD	vxge_mBIT(11)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_INI_TIMEOUT_EN_MWR	vxge_mBIT(15)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_INI_TIMEOUT_EN_MRD	vxge_mBIT(19)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_IGNORE_VPATH_RST_FOR_MSIX	vxge_mBIT(23)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_FLASH_READ_MSB	vxge_mBIT(27)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_DIS_HOST_PIPELINE_WR	vxge_mBIT(31)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_MRPCIM_STATS_ENABLE	vxge_mBIT(43)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_MRPCIM_STATS_MAP_TO_VPATH(val) \
							vxge_vBIT(val, 47, 5)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_EN_BLOCK_MSIX_DUE_TO_SERR	vxge_mBIT(55)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_FORCE_SENDING_INTA	vxge_mBIT(59)
#define	VXGE_HW_MRPCIM_GENERAL_CFG2_DIS_SWIF_PROT_ON_RDS	vxge_mBIT(63)
/*0x07510*/	u64	mrpcim_general_cfg3;
#define	VXGE_HW_MRPCIM_GENERAL_CFG3_PROTECTION_CA_OR_UNSUPN	vxge_mBIT(0)
#define	VXGE_HW_MRPCIM_GENERAL_CFG3_ILLEGAL_RD_CA_OR_UNSUPN	vxge_mBIT(3)
#define	VXGE_HW_MRPCIM_GENERAL_CFG3_RD_BYTE_SWAPEN	vxge_mBIT(7)
#define	VXGE_HW_MRPCIM_GENERAL_CFG3_RD_BIT_FLIPEN	vxge_mBIT(11)
#define	VXGE_HW_MRPCIM_GENERAL_CFG3_WR_BYTE_SWAPEN	vxge_mBIT(15)
#define	VXGE_HW_MRPCIM_GENERAL_CFG3_WR_BIT_FLIPEN	vxge_mBIT(19)
#define VXGE_HW_MRPCIM_GENERAL_CFG3_MR_MAX_MVFS(val) vxge_vBIT(val, 20, 16)
#define	VXGE_HW_MRPCIM_GENERAL_CFG3_MR_MVF_TBL_SIZE(val) \
							vxge_vBIT(val, 36, 16)
#define	VXGE_HW_MRPCIM_GENERAL_CFG3_PF0_SW_RESET_EN	vxge_mBIT(55)
#define VXGE_HW_MRPCIM_GENERAL_CFG3_REG_MODIFIED_CFG(val) vxge_vBIT(val, 56, 2)
#define	VXGE_HW_MRPCIM_GENERAL_CFG3_CPL_ECC_ENABLE_N	vxge_mBIT(59)
#define	VXGE_HW_MRPCIM_GENERAL_CFG3_BYPASS_DAISY_CHAIN	vxge_mBIT(63)
/*0x07518*/	u64	mrpcim_stats_start_host_addr;
#define	VXGE_HW_MRPCIM_STATS_START_HOST_ADDR_MRPCIM_STATS_START_HOST_ADDR(val)\
							vxge_vBIT(val, 0, 57)

	u8	unused07950[0x07950-0x07520];

/*0x07950*/	u64	rdcrdtarb_cfg0;
#define VXGE_HW_RDCRDTARB_CFG0_RDA_MAX_OUTSTANDING_RDS(val) \
						vxge_vBIT(val, 18, 6)
#define VXGE_HW_RDCRDTARB_CFG0_PDA_MAX_OUTSTANDING_RDS(val) \
						vxge_vBIT(val, 26, 6)
#define VXGE_HW_RDCRDTARB_CFG0_DBLGEN_MAX_OUTSTANDING_RDS(val) \
						vxge_vBIT(val, 34, 6)
#define VXGE_HW_RDCRDTARB_CFG0_WAIT_CNT(val) vxge_vBIT(val, 48, 4)
#define VXGE_HW_RDCRDTARB_CFG0_MAX_OUTSTANDING_RDS(val) vxge_vBIT(val, 54, 6)
#define	VXGE_HW_RDCRDTARB_CFG0_EN_XON	vxge_mBIT(63)
	u8	unused07be8[0x07be8-0x07958];

/*0x07be8*/	u64	bf_sw_reset;
#define VXGE_HW_BF_SW_RESET_BF_SW_RESET(val) vxge_vBIT(val, 0, 8)
/*0x07bf0*/	u64	sw_reset_status;
#define	VXGE_HW_SW_RESET_STATUS_RESET_CMPLT	vxge_mBIT(7)
#define	VXGE_HW_SW_RESET_STATUS_INIT_CMPLT	vxge_mBIT(15)
	u8	unused07d30[0x07d30-0x07bf8];

/*0x07d30*/	u64	mrpcim_debug_stats0;
#define VXGE_HW_MRPCIM_DEBUG_STATS0_INI_WR_DROP(val) vxge_vBIT(val, 0, 32)
#define VXGE_HW_MRPCIM_DEBUG_STATS0_INI_RD_DROP(val) vxge_vBIT(val, 32, 32)
/*0x07d38*/	u64	mrpcim_debug_stats1_vplane[17];
#define	VXGE_HW_MRPCIM_DEBUG_STATS1_VPLANE_WRCRDTARB_PH_CRDT_DEPLETED(val) \
							vxge_vBIT(val, 32, 32)
/*0x07dc0*/	u64	mrpcim_debug_stats2_vplane[17];
#define	VXGE_HW_MRPCIM_DEBUG_STATS2_VPLANE_WRCRDTARB_PD_CRDT_DEPLETED(val) \
							vxge_vBIT(val, 32, 32)
/*0x07e48*/	u64	mrpcim_debug_stats3_vplane[17];
#define	VXGE_HW_MRPCIM_DEBUG_STATS3_VPLANE_RDCRDTARB_NPH_CRDT_DEPLETED(val) \
							vxge_vBIT(val, 32, 32)
/*0x07ed0*/	u64	mrpcim_debug_stats4;
#define VXGE_HW_MRPCIM_DEBUG_STATS4_INI_WR_VPIN_DROP(val) vxge_vBIT(val, 0, 32)
#define	VXGE_HW_MRPCIM_DEBUG_STATS4_INI_RD_VPIN_DROP(val) \
							vxge_vBIT(val, 32, 32)
/*0x07ed8*/	u64	genstats_count01;
#define VXGE_HW_GENSTATS_COUNT01_GENSTATS_COUNT1(val) vxge_vBIT(val, 0, 32)
#define VXGE_HW_GENSTATS_COUNT01_GENSTATS_COUNT0(val) vxge_vBIT(val, 32, 32)
/*0x07ee0*/	u64	genstats_count23;
#define VXGE_HW_GENSTATS_COUNT23_GENSTATS_COUNT3(val) vxge_vBIT(val, 0, 32)
#define VXGE_HW_GENSTATS_COUNT23_GENSTATS_COUNT2(val) vxge_vBIT(val, 32, 32)
/*0x07ee8*/	u64	genstats_count4;
#define VXGE_HW_GENSTATS_COUNT4_GENSTATS_COUNT4(val) vxge_vBIT(val, 32, 32)
/*0x07ef0*/	u64	genstats_count5;
#define VXGE_HW_GENSTATS_COUNT5_GENSTATS_COUNT5(val) vxge_vBIT(val, 32, 32)

	u8	unused07f08[0x07f08-0x07ef8];

/*0x07f08*/	u64	genstats_cfg[6];
#define VXGE_HW_GENSTATS_CFG_DTYPE_SEL(val) vxge_vBIT(val, 3, 5)
#define VXGE_HW_GENSTATS_CFG_CLIENT_NO_SEL(val) vxge_vBIT(val, 9, 3)
#define VXGE_HW_GENSTATS_CFG_WR_RD_CPL_SEL(val) vxge_vBIT(val, 14, 2)
#define VXGE_HW_GENSTATS_CFG_VPATH_SEL(val) vxge_vBIT(val, 31, 17)
/*0x07f38*/	u64	genstat_64bit_cfg;
#define	VXGE_HW_GENSTAT_64BIT_CFG_EN_FOR_GENSTATS0	vxge_mBIT(3)
#define	VXGE_HW_GENSTAT_64BIT_CFG_EN_FOR_GENSTATS2	vxge_mBIT(7)
	u8	unused08000[0x08000-0x07f40];
/*0x08000*/	u64	gcmg3_int_status;
#define	VXGE_HW_GCMG3_INT_STATUS_GSTC_ERR0_GSTC0_INT	vxge_mBIT(0)
#define	VXGE_HW_GCMG3_INT_STATUS_GSTC_ERR1_GSTC1_INT	vxge_mBIT(1)
#define	VXGE_HW_GCMG3_INT_STATUS_GH2L_ERR0_GH2L0_INT	vxge_mBIT(2)
#define	VXGE_HW_GCMG3_INT_STATUS_GHSQ_ERR_GH2L1_INT	vxge_mBIT(3)
#define	VXGE_HW_GCMG3_INT_STATUS_GHSQ_ERR2_GH2L2_INT	vxge_mBIT(4)
#define	VXGE_HW_GCMG3_INT_STATUS_GH2L_SMERR0_GH2L3_INT	vxge_mBIT(5)
#define	VXGE_HW_GCMG3_INT_STATUS_GHSQ_ERR3_GH2L4_INT	vxge_mBIT(6)
/*0x08008*/	u64	gcmg3_int_mask;
	u8	unused09000[0x09000-0x8010];

/*0x09000*/	u64	g3ifcmd_fb_int_status;
#define	VXGE_HW_G3IFCMD_FB_INT_STATUS_ERR_G3IF_INT	vxge_mBIT(0)
/*0x09008*/	u64	g3ifcmd_fb_int_mask;
/*0x09010*/	u64	g3ifcmd_fb_err_reg;
#define	VXGE_HW_G3IFCMD_FB_ERR_REG_G3IF_CK_DLL_LOCK	vxge_mBIT(6)
#define	VXGE_HW_G3IFCMD_FB_ERR_REG_G3IF_SM_ERR	vxge_mBIT(7)
#define VXGE_HW_G3IFCMD_FB_ERR_REG_G3IF_RWDQS_DLL_LOCK(val) \
						vxge_vBIT(val, 24, 8)
#define	VXGE_HW_G3IFCMD_FB_ERR_REG_G3IF_IOCAL_FAULT	vxge_mBIT(55)
/*0x09018*/	u64	g3ifcmd_fb_err_mask;
/*0x09020*/	u64	g3ifcmd_fb_err_alarm;

	u8	unused09400[0x09400-0x09028];

/*0x09400*/	u64	g3ifcmd_cmu_int_status;
#define	VXGE_HW_G3IFCMD_CMU_INT_STATUS_ERR_G3IF_INT	vxge_mBIT(0)
/*0x09408*/	u64	g3ifcmd_cmu_int_mask;
/*0x09410*/	u64	g3ifcmd_cmu_err_reg;
#define	VXGE_HW_G3IFCMD_CMU_ERR_REG_G3IF_CK_DLL_LOCK	vxge_mBIT(6)
#define	VXGE_HW_G3IFCMD_CMU_ERR_REG_G3IF_SM_ERR	vxge_mBIT(7)
#define VXGE_HW_G3IFCMD_CMU_ERR_REG_G3IF_RWDQS_DLL_LOCK(val) \
							vxge_vBIT(val, 24, 8)
#define	VXGE_HW_G3IFCMD_CMU_ERR_REG_G3IF_IOCAL_FAULT	vxge_mBIT(55)
/*0x09418*/	u64	g3ifcmd_cmu_err_mask;
/*0x09420*/	u64	g3ifcmd_cmu_err_alarm;

	u8	unused09800[0x09800-0x09428];

/*0x09800*/	u64	g3ifcmd_cml_int_status;
#define	VXGE_HW_G3IFCMD_CML_INT_STATUS_ERR_G3IF_INT	vxge_mBIT(0)
/*0x09808*/	u64	g3ifcmd_cml_int_mask;
/*0x09810*/	u64	g3ifcmd_cml_err_reg;
#define	VXGE_HW_G3IFCMD_CML_ERR_REG_G3IF_CK_DLL_LOCK	vxge_mBIT(6)
#define	VXGE_HW_G3IFCMD_CML_ERR_REG_G3IF_SM_ERR	vxge_mBIT(7)
#define VXGE_HW_G3IFCMD_CML_ERR_REG_G3IF_RWDQS_DLL_LOCK(val) \
						vxge_vBIT(val, 24, 8)
#define	VXGE_HW_G3IFCMD_CML_ERR_REG_G3IF_IOCAL_FAULT	vxge_mBIT(55)
/*0x09818*/	u64	g3ifcmd_cml_err_mask;
/*0x09820*/	u64	g3ifcmd_cml_err_alarm;
	u8	unused09b00[0x09b00-0x09828];

/*0x09b00*/	u64	vpath_to_vplane_map[17];
#define VXGE_HW_VPATH_TO_VPLANE_MAP_VPATH_TO_VPLANE_MAP(val) \
							vxge_vBIT(val, 3, 5)
	u8	unused09c30[0x09c30-0x09b88];

/*0x09c30*/	u64	xgxs_cfg_port[2];
#define VXGE_HW_XGXS_CFG_PORT_SIG_DETECT_FORCE_LOS(val) vxge_vBIT(val, 16, 4)
#define VXGE_HW_XGXS_CFG_PORT_SIG_DETECT_FORCE_VALID(val) vxge_vBIT(val, 20, 4)
#define	VXGE_HW_XGXS_CFG_PORT_SEL_INFO_0	vxge_mBIT(27)
#define VXGE_HW_XGXS_CFG_PORT_SEL_INFO_1(val) vxge_vBIT(val, 29, 3)
#define VXGE_HW_XGXS_CFG_PORT_TX_LANE0_SKEW(val) vxge_vBIT(val, 32, 4)
#define VXGE_HW_XGXS_CFG_PORT_TX_LANE1_SKEW(val) vxge_vBIT(val, 36, 4)
#define VXGE_HW_XGXS_CFG_PORT_TX_LANE2_SKEW(val) vxge_vBIT(val, 40, 4)
#define VXGE_HW_XGXS_CFG_PORT_TX_LANE3_SKEW(val) vxge_vBIT(val, 44, 4)
/*0x09c40*/	u64	xgxs_rxber_cfg_port[2];
#define VXGE_HW_XGXS_RXBER_CFG_PORT_INTERVAL_DUR(val) vxge_vBIT(val, 0, 4)
#define	VXGE_HW_XGXS_RXBER_CFG_PORT_RXGXS_INTERVAL_CNT(val) \
							vxge_vBIT(val, 16, 48)
/*0x09c50*/	u64	xgxs_rxber_status_port[2];
#define	VXGE_HW_XGXS_RXBER_STATUS_PORT_RXGXS_RXGXS_LANE_A_ERR_CNT(val)	\
							vxge_vBIT(val, 0, 16)
#define	VXGE_HW_XGXS_RXBER_STATUS_PORT_RXGXS_RXGXS_LANE_B_ERR_CNT(val)	\
							vxge_vBIT(val, 16, 16)
#define	VXGE_HW_XGXS_RXBER_STATUS_PORT_RXGXS_RXGXS_LANE_C_ERR_CNT(val)	\
							vxge_vBIT(val, 32, 16)
#define	VXGE_HW_XGXS_RXBER_STATUS_PORT_RXGXS_RXGXS_LANE_D_ERR_CNT(val)	\
							vxge_vBIT(val, 48, 16)
/*0x09c60*/	u64	xgxs_status_port[2];
#define VXGE_HW_XGXS_STATUS_PORT_XMACJ_PCS_TX_ACTIVITY(val) vxge_vBIT(val, 0, 4)
#define VXGE_HW_XGXS_STATUS_PORT_XMACJ_PCS_RX_ACTIVITY(val) vxge_vBIT(val, 4, 4)
#define	VXGE_HW_XGXS_STATUS_PORT_XMACJ_PCS_CTC_FIFO_ERR	BIT(11)
#define VXGE_HW_XGXS_STATUS_PORT_XMACJ_PCS_BYTE_SYNC_LOST(val) \
							vxge_vBIT(val, 12, 4)
#define VXGE_HW_XGXS_STATUS_PORT_XMACJ_PCS_CTC_ERR(val) vxge_vBIT(val, 16, 4)
#define	VXGE_HW_XGXS_STATUS_PORT_XMACJ_PCS_ALIGNMENT_ERR	vxge_mBIT(23)
#define VXGE_HW_XGXS_STATUS_PORT_XMACJ_PCS_DEC_ERR(val) vxge_vBIT(val, 24, 8)
#define VXGE_HW_XGXS_STATUS_PORT_XMACJ_PCS_SKIP_INS_REQ(val) \
							vxge_vBIT(val, 32, 4)
#define VXGE_HW_XGXS_STATUS_PORT_XMACJ_PCS_SKIP_DEL_REQ(val) \
							vxge_vBIT(val, 36, 4)
/*0x09c70*/	u64	xgxs_pma_reset_port[2];
#define VXGE_HW_XGXS_PMA_RESET_PORT_SERDES_RESET(val) vxge_vBIT(val, 0, 8)
	u8	unused09c90[0x09c90-0x09c80];

/*0x09c90*/	u64	xgxs_static_cfg_port[2];
#define	VXGE_HW_XGXS_STATIC_CFG_PORT_FW_CTRL_SERDES	vxge_mBIT(3)
	u8	unused09d40[0x09d40-0x09ca0];

/*0x09d40*/	u64	xgxs_info_port[2];
#define VXGE_HW_XGXS_INFO_PORT_XMACJ_INFO_0(val) vxge_vBIT(val, 0, 32)
#define VXGE_HW_XGXS_INFO_PORT_XMACJ_INFO_1(val) vxge_vBIT(val, 32, 32)
/*0x09d50*/	u64	ratemgmt_cfg_port[2];
#define VXGE_HW_RATEMGMT_CFG_PORT_MODE(val) vxge_vBIT(val, 2, 2)
#define	VXGE_HW_RATEMGMT_CFG_PORT_RATE	vxge_mBIT(7)
#define	VXGE_HW_RATEMGMT_CFG_PORT_FIXED_USE_FSM	vxge_mBIT(11)
#define	VXGE_HW_RATEMGMT_CFG_PORT_ANTP_USE_FSM	vxge_mBIT(15)
#define	VXGE_HW_RATEMGMT_CFG_PORT_ANBE_USE_FSM	vxge_mBIT(19)
/*0x09d60*/	u64	ratemgmt_status_port[2];
#define	VXGE_HW_RATEMGMT_STATUS_PORT_RATEMGMT_COMPLETE	vxge_mBIT(3)
#define	VXGE_HW_RATEMGMT_STATUS_PORT_RATEMGMT_RATE	vxge_mBIT(7)
#define	VXGE_HW_RATEMGMT_STATUS_PORT_RATEMGMT_MAC_MATCHES_PHY	vxge_mBIT(11)
	u8	unused09d80[0x09d80-0x09d70];

/*0x09d80*/	u64	ratemgmt_fixed_cfg_port[2];
#define	VXGE_HW_RATEMGMT_FIXED_CFG_PORT_RESTART	vxge_mBIT(7)
/*0x09d90*/	u64	ratemgmt_antp_cfg_port[2];
#define	VXGE_HW_RATEMGMT_ANTP_CFG_PORT_RESTART	vxge_mBIT(7)
#define	VXGE_HW_RATEMGMT_ANTP_CFG_PORT_USE_PREAMBLE_EXT_PHY	vxge_mBIT(11)
#define	VXGE_HW_RATEMGMT_ANTP_CFG_PORT_USE_ACT_SEL	vxge_mBIT(15)
#define VXGE_HW_RATEMGMT_ANTP_CFG_PORT_T_RETRY_PHY_QUERY(val) \
							vxge_vBIT(val, 16, 4)
#define	VXGE_HW_RATEMGMT_ANTP_CFG_PORT_T_WAIT_MDIO_RESPONSE(val) \
							vxge_vBIT(val, 20, 4)
#define	VXGE_HW_RATEMGMT_ANTP_CFG_PORT_T_LDOWN_REAUTO_RESPONSE(val) \
							vxge_vBIT(val, 24, 4)
#define	VXGE_HW_RATEMGMT_ANTP_CFG_PORT_ADVERTISE_10G	vxge_mBIT(31)
#define	VXGE_HW_RATEMGMT_ANTP_CFG_PORT_ADVERTISE_1G	vxge_mBIT(35)
/*0x09da0*/	u64	ratemgmt_anbe_cfg_port[2];
#define	VXGE_HW_RATEMGMT_ANBE_CFG_PORT_RESTART	vxge_mBIT(7)
#define	VXGE_HW_RATEMGMT_ANBE_CFG_PORT_PARALLEL_DETECT_10G_KX4_ENABLE \
								vxge_mBIT(11)
#define	VXGE_HW_RATEMGMT_ANBE_CFG_PORT_PARALLEL_DETECT_1G_KX_ENABLE \
								vxge_mBIT(15)
#define VXGE_HW_RATEMGMT_ANBE_CFG_PORT_T_SYNC_10G_KX4(val) vxge_vBIT(val, 16, 4)
#define VXGE_HW_RATEMGMT_ANBE_CFG_PORT_T_SYNC_1G_KX(val) vxge_vBIT(val, 20, 4)
#define VXGE_HW_RATEMGMT_ANBE_CFG_PORT_T_DME_EXCHANGE(val) vxge_vBIT(val, 24, 4)
#define	VXGE_HW_RATEMGMT_ANBE_CFG_PORT_ADVERTISE_10G_KX4	vxge_mBIT(31)
#define	VXGE_HW_RATEMGMT_ANBE_CFG_PORT_ADVERTISE_1G_KX	vxge_mBIT(35)
/*0x09db0*/	u64	anbe_cfg_port[2];
#define VXGE_HW_ANBE_CFG_PORT_RESET_CFG_REGS(val) vxge_vBIT(val, 0, 8)
#define VXGE_HW_ANBE_CFG_PORT_ALIGN_10G_KX4_OVERRIDE(val) vxge_vBIT(val, 10, 2)
#define VXGE_HW_ANBE_CFG_PORT_SYNC_1G_KX_OVERRIDE(val) vxge_vBIT(val, 14, 2)
/*0x09dc0*/	u64	anbe_mgr_ctrl_port[2];
#define	VXGE_HW_ANBE_MGR_CTRL_PORT_WE	vxge_mBIT(3)
#define	VXGE_HW_ANBE_MGR_CTRL_PORT_STROBE	vxge_mBIT(7)
#define VXGE_HW_ANBE_MGR_CTRL_PORT_ADDR(val) vxge_vBIT(val, 15, 9)
#define VXGE_HW_ANBE_MGR_CTRL_PORT_DATA(val) vxge_vBIT(val, 32, 32)
	u8	unused09de0[0x09de0-0x09dd0];

/*0x09de0*/	u64	anbe_fw_mstr_port[2];
#define	VXGE_HW_ANBE_FW_MSTR_PORT_CONNECT_BEAN_TO_SERDES	vxge_mBIT(3)
#define	VXGE_HW_ANBE_FW_MSTR_PORT_TX_ZEROES_TO_SERDES	vxge_mBIT(7)
/*0x09df0*/	u64	anbe_hwfsm_gen_status_port[2];
#define	VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_10G_KX4_USING_PD \
							vxge_mBIT(3)
#define	VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_10G_KX4_USING_DME \
							vxge_mBIT(7)
#define	VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_1G_KX_USING_PD \
							vxge_mBIT(11)
#define	VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_1G_KX_USING_DME \
							vxge_mBIT(15)
#define	VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_ANBEFSM_STATE(val)	\
							vxge_vBIT(val, 18, 6)
#define	VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_BEAN_NEXT_PAGE_RECEIVED \
							vxge_mBIT(27)
#define	VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_BEAN_BASE_PAGE_RECEIVED \
							vxge_mBIT(35)
#define	VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_BEAN_AUTONEG_COMPLETE \
							vxge_mBIT(39)
#define	VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_NP_BEFORE_BP \
							vxge_mBIT(43)
#define	\
VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_AN_COMPLETE_BEFORE_BP \
							vxge_mBIT(47)
#define	\
VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_AN_COMPLETE_BEFORE_NP \
vxge_mBIT(51)
#define	\
VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_MODE_WHEN_AN_COMPLETE \
							vxge_mBIT(55)
#define	VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_COUNT_BP(val) \
							vxge_vBIT(val, 56, 4)
#define	VXGE_HW_ANBE_HWFSM_GEN_STATUS_PORT_RATEMGMT_COUNT_NP(val) \
							vxge_vBIT(val, 60, 4)
/*0x09e00*/	u64	anbe_hwfsm_bp_status_port[2];
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_FEC_ENABLE \
							vxge_mBIT(32)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_FEC_ABILITY \
							vxge_mBIT(33)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_10G_KR_CAPABLE \
							vxge_mBIT(40)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_10G_KX4_CAPABLE \
							vxge_mBIT(41)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_1G_KX_CAPABLE \
							vxge_mBIT(42)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_TX_NONCE(val)	\
							vxge_vBIT(val, 43, 5)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_NP	vxge_mBIT(48)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_ACK	vxge_mBIT(49)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_REMOTE_FAULT \
							vxge_mBIT(50)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_ASM_DIR	vxge_mBIT(51)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_PAUSE	vxge_mBIT(53)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_ECHOED_NONCE(val) \
							vxge_vBIT(val, 54, 5)
#define	VXGE_HW_ANBE_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_SELECTOR_FIELD(val) \
							vxge_vBIT(val, 59, 5)
/*0x09e10*/	u64	anbe_hwfsm_np_status_port[2];
#define	VXGE_HW_ANBE_HWFSM_NP_STATUS_PORT_RATEMGMT_NP_BITS_47_TO_32(val) \
							vxge_vBIT(val, 16, 16)
#define	VXGE_HW_ANBE_HWFSM_NP_STATUS_PORT_RATEMGMT_NP_BITS_31_TO_0(val) \
							vxge_vBIT(val, 32, 32)
	u8	unused09e30[0x09e30-0x09e20];

/*0x09e30*/	u64	antp_gen_cfg_port[2];
/*0x09e40*/	u64	antp_hwfsm_gen_status_port[2];
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_10G	vxge_mBIT(3)
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_CHOSE_1G	vxge_mBIT(7)
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_ANTPFSM_STATE(val)	\
							vxge_vBIT(val, 10, 6)
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_AUTONEG_COMPLETE \
								vxge_mBIT(23)
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_NO_LP_XNP \
							vxge_mBIT(27)
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_GOT_LP_XNP	vxge_mBIT(31)
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_MESSAGE_CODE \
							vxge_mBIT(35)
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_NO_HCD \
							vxge_mBIT(43)
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_FOUND_HCD	vxge_mBIT(47)
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_UNEXPECTED_INVALID_RATE \
							vxge_mBIT(51)
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_VALID_RATE	vxge_mBIT(55)
#define	VXGE_HW_ANTP_HWFSM_GEN_STATUS_PORT_RATEMGMT_PERSISTENT_LDOWN \
							vxge_mBIT(59)
/*0x09e50*/	u64	antp_hwfsm_bp_status_port[2];
#define	VXGE_HW_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_NP	vxge_mBIT(0)
#define	VXGE_HW_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_ACK	vxge_mBIT(1)
#define	VXGE_HW_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_RF	vxge_mBIT(2)
#define	VXGE_HW_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_XNP	vxge_mBIT(3)
#define	VXGE_HW_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_ABILITY_FIELD(val) \
							vxge_vBIT(val, 4, 7)
#define	VXGE_HW_ANTP_HWFSM_BP_STATUS_PORT_RATEMGMT_BP_SELECTOR_FIELD(val) \
							vxge_vBIT(val, 11, 5)
/*0x09e60*/	u64	antp_hwfsm_xnp_status_port[2];
#define	VXGE_HW_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_NP	vxge_mBIT(0)
#define	VXGE_HW_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_ACK	vxge_mBIT(1)
#define	VXGE_HW_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_MP	vxge_mBIT(2)
#define	VXGE_HW_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_ACK2	vxge_mBIT(3)
#define	VXGE_HW_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_TOGGLE	vxge_mBIT(4)
#define	VXGE_HW_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_MESSAGE_CODE(val) \
							vxge_vBIT(val, 5, 11)
#define	VXGE_HW_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_UNF_CODE_FIELD1(val) \
							vxge_vBIT(val, 16, 16)
#define	VXGE_HW_ANTP_HWFSM_XNP_STATUS_PORT_RATEMGMT_XNP_UNF_CODE_FIELD2(val) \
							vxge_vBIT(val, 32, 16)
/*0x09e70*/	u64	mdio_mgr_access_port[2];
#define	VXGE_HW_MDIO_MGR_ACCESS_PORT_STROBE_ONE	BIT(3)
#define VXGE_HW_MDIO_MGR_ACCESS_PORT_OP_TYPE(val) vxge_vBIT(val, 5, 3)
#define VXGE_HW_MDIO_MGR_ACCESS_PORT_DEVAD(val) vxge_vBIT(val, 11, 5)
#define VXGE_HW_MDIO_MGR_ACCESS_PORT_ADDR(val) vxge_vBIT(val, 16, 16)
#define VXGE_HW_MDIO_MGR_ACCESS_PORT_DATA(val) vxge_vBIT(val, 32, 16)
#define VXGE_HW_MDIO_MGR_ACCESS_PORT_ST_PATTERN(val) vxge_vBIT(val, 49, 2)
#define	VXGE_HW_MDIO_MGR_ACCESS_PORT_PREAMBLE	vxge_mBIT(51)
#define VXGE_HW_MDIO_MGR_ACCESS_PORT_PRTAD(val) vxge_vBIT(val, 55, 5)
#define	VXGE_HW_MDIO_MGR_ACCESS_PORT_STROBE_TWO	vxge_mBIT(63)
	u8	unused0a200[0x0a200-0x09e80];
/*0x0a200*/	u64	xmac_vsport_choices_vh[17];
#define VXGE_HW_XMAC_VSPORT_CHOICES_VH_VSPORT_VECTOR(val) vxge_vBIT(val, 0, 17)
	u8	unused0a400[0x0a400-0x0a288];

/*0x0a400*/	u64	rx_thresh_cfg_vp[17];
#define VXGE_HW_RX_THRESH_CFG_VP_PAUSE_LOW_THR(val) vxge_vBIT(val, 0, 8)
#define VXGE_HW_RX_THRESH_CFG_VP_PAUSE_HIGH_THR(val) vxge_vBIT(val, 8, 8)
#define VXGE_HW_RX_THRESH_CFG_VP_RED_THR_0(val) vxge_vBIT(val, 16, 8)
#define VXGE_HW_RX_THRESH_CFG_VP_RED_THR_1(val) vxge_vBIT(val, 24, 8)
#define VXGE_HW_RX_THRESH_CFG_VP_RED_THR_2(val) vxge_vBIT(val, 32, 8)
#define VXGE_HW_RX_THRESH_CFG_VP_RED_THR_3(val) vxge_vBIT(val, 40, 8)
	u8	unused0ac90[0x0ac90-0x0a488];
} __packed;

/*VXGE_HW_SRPCIM_REGS_H*/
struct vxge_hw_srpcim_reg {

/*0x00000*/	u64	tim_mr2sr_resource_assignment_vh;
#define	VXGE_HW_TIM_MR2SR_RESOURCE_ASSIGNMENT_VH_BMAP_ROOT(val) \
							vxge_vBIT(val, 0, 32)
	u8	unused00100[0x00100-0x00008];

/*0x00100*/	u64	srpcim_pcipif_int_status;
#define	VXGE_HW_SRPCIM_PCIPIF_INT_STATUS_MRPCIM_MSG_MRPCIM_MSG_INT	BIT(3)
#define	VXGE_HW_SRPCIM_PCIPIF_INT_STATUS_VPATH_MSG_VPATH_MSG_INT	BIT(7)
#define	VXGE_HW_SRPCIM_PCIPIF_INT_STATUS_SRPCIM_SPARE_R1_SRPCIM_SPARE_R1_INT \
									BIT(11)
/*0x00108*/	u64	srpcim_pcipif_int_mask;
/*0x00110*/	u64	mrpcim_msg_reg;
#define	VXGE_HW_MRPCIM_MSG_REG_SWIF_MRPCIM_TO_SRPCIM_RMSG_INT	BIT(3)
/*0x00118*/	u64	mrpcim_msg_mask;
/*0x00120*/	u64	mrpcim_msg_alarm;
/*0x00128*/	u64	vpath_msg_reg;
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH0_TO_SRPCIM_RMSG_INT	BIT(0)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH1_TO_SRPCIM_RMSG_INT	BIT(1)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH2_TO_SRPCIM_RMSG_INT	BIT(2)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH3_TO_SRPCIM_RMSG_INT	BIT(3)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH4_TO_SRPCIM_RMSG_INT	BIT(4)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH5_TO_SRPCIM_RMSG_INT	BIT(5)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH6_TO_SRPCIM_RMSG_INT	BIT(6)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH7_TO_SRPCIM_RMSG_INT	BIT(7)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH8_TO_SRPCIM_RMSG_INT	BIT(8)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH9_TO_SRPCIM_RMSG_INT	BIT(9)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH10_TO_SRPCIM_RMSG_INT	BIT(10)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH11_TO_SRPCIM_RMSG_INT	BIT(11)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH12_TO_SRPCIM_RMSG_INT	BIT(12)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH13_TO_SRPCIM_RMSG_INT	BIT(13)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH14_TO_SRPCIM_RMSG_INT	BIT(14)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH15_TO_SRPCIM_RMSG_INT	BIT(15)
#define	VXGE_HW_VPATH_MSG_REG_SWIF_VPATH16_TO_SRPCIM_RMSG_INT	BIT(16)
/*0x00130*/	u64	vpath_msg_mask;
/*0x00138*/	u64	vpath_msg_alarm;
	u8	unused00160[0x00160-0x00140];

/*0x00160*/	u64	srpcim_to_mrpcim_wmsg;
#define	VXGE_HW_SRPCIM_TO_MRPCIM_WMSG_SRPCIM_TO_MRPCIM_WMSG(val) \
							vxge_vBIT(val, 0, 64)
/*0x00168*/	u64	srpcim_to_mrpcim_wmsg_trig;
#define	VXGE_HW_SRPCIM_TO_MRPCIM_WMSG_TRIG_SRPCIM_TO_MRPCIM_WMSG_TRIG	BIT(0)
/*0x00170*/	u64	mrpcim_to_srpcim_rmsg;
#define	VXGE_HW_MRPCIM_TO_SRPCIM_RMSG_SWIF_MRPCIM_TO_SRPCIM_RMSG(val) \
							vxge_vBIT(val, 0, 64)
/*0x00178*/	u64	vpath_to_srpcim_rmsg_sel;
#define	VXGE_HW_VPATH_TO_SRPCIM_RMSG_SEL_VPATH_TO_SRPCIM_RMSG_SEL(val) \
							vxge_vBIT(val, 0, 5)
/*0x00180*/	u64	vpath_to_srpcim_rmsg;
#define	VXGE_HW_VPATH_TO_SRPCIM_RMSG_SWIF_VPATH_TO_SRPCIM_RMSG(val) \
							vxge_vBIT(val, 0, 64)
	u8	unused00200[0x00200-0x00188];

/*0x00200*/	u64	srpcim_general_int_status;
#define	VXGE_HW_SRPCIM_GENERAL_INT_STATUS_PIC_INT	BIT(0)
#define	VXGE_HW_SRPCIM_GENERAL_INT_STATUS_PCI_INT	BIT(3)
#define	VXGE_HW_SRPCIM_GENERAL_INT_STATUS_XMAC_INT	BIT(7)
	u8	unused00210[0x00210-0x00208];

/*0x00210*/	u64	srpcim_general_int_mask;
#define	VXGE_HW_SRPCIM_GENERAL_INT_MASK_PIC_INT	BIT(0)
#define	VXGE_HW_SRPCIM_GENERAL_INT_MASK_PCI_INT	BIT(3)
#define	VXGE_HW_SRPCIM_GENERAL_INT_MASK_XMAC_INT	BIT(7)
	u8	unused00220[0x00220-0x00218];

/*0x00220*/	u64	srpcim_ppif_int_status;

/*0x00228*/	u64	srpcim_ppif_int_mask;
/*0x00230*/	u64	srpcim_gen_errors_reg;
#define	VXGE_HW_SRPCIM_GEN_ERRORS_REG_PCICONFIG_PF_STATUS_ERR	BIT(3)
#define	VXGE_HW_SRPCIM_GEN_ERRORS_REG_PCICONFIG_PF_UNCOR_ERR	BIT(7)
#define	VXGE_HW_SRPCIM_GEN_ERRORS_REG_PCICONFIG_PF_COR_ERR	BIT(11)
#define	VXGE_HW_SRPCIM_GEN_ERRORS_REG_INTCTRL_SCHED_INT	BIT(15)
#define	VXGE_HW_SRPCIM_GEN_ERRORS_REG_INI_SERR_DET	BIT(19)
#define	VXGE_HW_SRPCIM_GEN_ERRORS_REG_TGT_PF_ILLEGAL_ACCESS	BIT(23)
/*0x00238*/	u64	srpcim_gen_errors_mask;
/*0x00240*/	u64	srpcim_gen_errors_alarm;
/*0x00248*/	u64	mrpcim_to_srpcim_alarm_reg;
#define	VXGE_HW_MRPCIM_TO_SRPCIM_ALARM_REG_PPIF_MRPCIM_TO_SRPCIM_ALARM	BIT(3)
/*0x00250*/	u64	mrpcim_to_srpcim_alarm_mask;
/*0x00258*/	u64	mrpcim_to_srpcim_alarm_alarm;
/*0x00260*/	u64	vpath_to_srpcim_alarm_reg;

/*0x00268*/	u64	vpath_to_srpcim_alarm_mask;
/*0x00270*/	u64	vpath_to_srpcim_alarm_alarm;
	u8	unused00280[0x00280-0x00278];

/*0x00280*/	u64	pf_sw_reset;
#define VXGE_HW_PF_SW_RESET_PF_SW_RESET(val) vxge_vBIT(val, 0, 8)
/*0x00288*/	u64	srpcim_general_cfg1;
#define	VXGE_HW_SRPCIM_GENERAL_CFG1_BOOT_BYTE_SWAPEN	BIT(19)
#define	VXGE_HW_SRPCIM_GENERAL_CFG1_BOOT_BIT_FLIPEN	BIT(23)
#define	VXGE_HW_SRPCIM_GENERAL_CFG1_MSIX_ADDR_SWAPEN	BIT(27)
#define	VXGE_HW_SRPCIM_GENERAL_CFG1_MSIX_ADDR_FLIPEN	BIT(31)
#define	VXGE_HW_SRPCIM_GENERAL_CFG1_MSIX_DATA_SWAPEN	BIT(35)
#define	VXGE_HW_SRPCIM_GENERAL_CFG1_MSIX_DATA_FLIPEN	BIT(39)
/*0x00290*/	u64	srpcim_interrupt_cfg1;
#define VXGE_HW_SRPCIM_INTERRUPT_CFG1_ALARM_MAP_TO_MSG(val) vxge_vBIT(val, 1, 7)
#define VXGE_HW_SRPCIM_INTERRUPT_CFG1_TRAFFIC_CLASS(val) vxge_vBIT(val, 9, 3)
	u8	unused002a8[0x002a8-0x00298];

/*0x002a8*/	u64	srpcim_clear_msix_mask;
#define	VXGE_HW_SRPCIM_CLEAR_MSIX_MASK_SRPCIM_CLEAR_MSIX_MASK	BIT(0)
/*0x002b0*/	u64	srpcim_set_msix_mask;
#define	VXGE_HW_SRPCIM_SET_MSIX_MASK_SRPCIM_SET_MSIX_MASK	BIT(0)
/*0x002b8*/	u64	srpcim_clr_msix_one_shot;
#define	VXGE_HW_SRPCIM_CLR_MSIX_ONE_SHOT_SRPCIM_CLR_MSIX_ONE_SHOT	BIT(0)
/*0x002c0*/	u64	srpcim_rst_in_prog;
#define	VXGE_HW_SRPCIM_RST_IN_PROG_SRPCIM_RST_IN_PROG	BIT(7)
/*0x002c8*/	u64	srpcim_reg_modified;
#define	VXGE_HW_SRPCIM_REG_MODIFIED_SRPCIM_REG_MODIFIED	BIT(7)
/*0x002d0*/	u64	tgt_pf_illegal_access;
#define VXGE_HW_TGT_PF_ILLEGAL_ACCESS_SWIF_REGION(val) vxge_vBIT(val, 1, 7)
/*0x002d8*/	u64	srpcim_msix_status;
#define	VXGE_HW_SRPCIM_MSIX_STATUS_INTCTL_SRPCIM_MSIX_MASK	BIT(3)
#define	VXGE_HW_SRPCIM_MSIX_STATUS_INTCTL_SRPCIM_MSIX_PENDING_VECTOR	BIT(7)
	u8	unused00880[0x00880-0x002e0];

/*0x00880*/	u64	xgmac_sr_int_status;
#define	VXGE_HW_XGMAC_SR_INT_STATUS_ASIC_NTWK_SR_ERR_ASIC_NTWK_SR_INT	BIT(3)
/*0x00888*/	u64	xgmac_sr_int_mask;
/*0x00890*/	u64	asic_ntwk_sr_err_reg;
#define	VXGE_HW_ASIC_NTWK_SR_ERR_REG_XMACJ_NTWK_SUSTAINED_FAULT	BIT(3)
#define	VXGE_HW_ASIC_NTWK_SR_ERR_REG_XMACJ_NTWK_SUSTAINED_OK	BIT(7)
#define	VXGE_HW_ASIC_NTWK_SR_ERR_REG_XMACJ_NTWK_SUSTAINED_FAULT_OCCURRED \
									BIT(11)
#define	VXGE_HW_ASIC_NTWK_SR_ERR_REG_XMACJ_NTWK_SUSTAINED_OK_OCCURRED	BIT(15)
/*0x00898*/	u64	asic_ntwk_sr_err_mask;
/*0x008a0*/	u64	asic_ntwk_sr_err_alarm;
	u8	unused008c0[0x008c0-0x008a8];

/*0x008c0*/	u64	xmac_vsport_choices_sr_clone;
#define	VXGE_HW_XMAC_VSPORT_CHOICES_SR_CLONE_VSPORT_VECTOR(val) \
							vxge_vBIT(val, 0, 17)
	u8	unused00900[0x00900-0x008c8];

/*0x00900*/	u64	mr_rqa_top_prty_for_vh;
#define	VXGE_HW_MR_RQA_TOP_PRTY_FOR_VH_RQA_TOP_PRTY_FOR_VH(val) \
							vxge_vBIT(val, 59, 5)
/*0x00908*/	u64	umq_vh_data_list_empty;
#define	VXGE_HW_UMQ_VH_DATA_LIST_EMPTY_ROCRC_UMQ_VH_DATA_LIST_EMPTY \
							BIT(0)
/*0x00910*/	u64	wde_cfg;
#define	VXGE_HW_WDE_CFG_NS0_FORCE_MWB_START	BIT(0)
#define	VXGE_HW_WDE_CFG_NS0_FORCE_MWB_END	BIT(1)
#define	VXGE_HW_WDE_CFG_NS0_FORCE_QB_START	BIT(2)
#define	VXGE_HW_WDE_CFG_NS0_FORCE_QB_END	BIT(3)
#define	VXGE_HW_WDE_CFG_NS0_FORCE_MPSB_START	BIT(4)
#define	VXGE_HW_WDE_CFG_NS0_FORCE_MPSB_END	BIT(5)
#define	VXGE_HW_WDE_CFG_NS0_MWB_OPT_EN	BIT(6)
#define	VXGE_HW_WDE_CFG_NS0_QB_OPT_EN	BIT(7)
#define	VXGE_HW_WDE_CFG_NS0_MPSB_OPT_EN	BIT(8)
#define	VXGE_HW_WDE_CFG_NS1_FORCE_MWB_START	BIT(9)
#define	VXGE_HW_WDE_CFG_NS1_FORCE_MWB_END	BIT(10)
#define	VXGE_HW_WDE_CFG_NS1_FORCE_QB_START	BIT(11)
#define	VXGE_HW_WDE_CFG_NS1_FORCE_QB_END	BIT(12)
#define	VXGE_HW_WDE_CFG_NS1_FORCE_MPSB_START	BIT(13)
#define	VXGE_HW_WDE_CFG_NS1_FORCE_MPSB_END	BIT(14)
#define	VXGE_HW_WDE_CFG_NS1_MWB_OPT_EN	BIT(15)
#define	VXGE_HW_WDE_CFG_NS1_QB_OPT_EN	BIT(16)
#define	VXGE_HW_WDE_CFG_NS1_MPSB_OPT_EN	BIT(17)
#define	VXGE_HW_WDE_CFG_DISABLE_QPAD_FOR_UNALIGNED_ADDR	BIT(19)
#define VXGE_HW_WDE_CFG_ALIGNMENT_PREFERENCE(val) vxge_vBIT(val, 30, 2)
#define VXGE_HW_WDE_CFG_MEM_WORD_SIZE(val) vxge_vBIT(val, 46, 2)

} __packed;

/*VXGE_HW_VPMGMT_REGS_H*/
struct vxge_hw_vpmgmt_reg {

	u8	unused00040[0x00040-0x00000];

/*0x00040*/	u64	vpath_to_func_map_cfg1;
#define	VXGE_HW_VPATH_TO_FUNC_MAP_CFG1_VPATH_TO_FUNC_MAP_CFG1(val) \
							vxge_vBIT(val, 3, 5)
/*0x00048*/	u64	vpath_is_first;
#define	VXGE_HW_VPATH_IS_FIRST_VPATH_IS_FIRST	vxge_mBIT(3)
/*0x00050*/	u64	srpcim_to_vpath_wmsg;
#define	VXGE_HW_SRPCIM_TO_VPATH_WMSG_SRPCIM_TO_VPATH_WMSG(val) \
							vxge_vBIT(val, 0, 64)
/*0x00058*/	u64	srpcim_to_vpath_wmsg_trig;
#define	VXGE_HW_SRPCIM_TO_VPATH_WMSG_TRIG_SRPCIM_TO_VPATH_WMSG_TRIG \
								vxge_mBIT(0)
	u8	unused00100[0x00100-0x00060];

/*0x00100*/	u64	tim_vpath_assignment;
#define VXGE_HW_TIM_VPATH_ASSIGNMENT_BMAP_ROOT(val) vxge_vBIT(val, 0, 32)
	u8	unused00140[0x00140-0x00108];

/*0x00140*/	u64	rqa_top_prty_for_vp;
#define VXGE_HW_RQA_TOP_PRTY_FOR_VP_RQA_TOP_PRTY_FOR_VP(val) \
							vxge_vBIT(val, 59, 5)
	u8	unused001c0[0x001c0-0x00148];

/*0x001c0*/	u64	rxmac_rx_pa_cfg0_vpmgmt_clone;
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_IGNORE_FRAME_ERR	vxge_mBIT(3)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_SUPPORT_SNAP_AB_N	vxge_mBIT(7)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_SEARCH_FOR_HAO	vxge_mBIT(18)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_SUPPORT_MOBILE_IPV6_HDRS \
								vxge_mBIT(19)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_IPV6_STOP_SEARCHING \
								vxge_mBIT(23)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_NO_PS_IF_UNKNOWN	vxge_mBIT(27)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_SEARCH_FOR_ETYPE	vxge_mBIT(35)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_TOSS_ANY_FRM_IF_L3_CSUM_ERR \
								vxge_mBIT(39)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_TOSS_OFFLD_FRM_IF_L3_CSUM_ERR \
								vxge_mBIT(43)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_TOSS_ANY_FRM_IF_L4_CSUM_ERR \
								vxge_mBIT(47)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_TOSS_OFFLD_FRM_IF_L4_CSUM_ERR \
								vxge_mBIT(51)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_TOSS_ANY_FRM_IF_RPA_ERR \
								vxge_mBIT(55)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_TOSS_OFFLD_FRM_IF_RPA_ERR \
								vxge_mBIT(59)
#define	VXGE_HW_RXMAC_RX_PA_CFG0_VPMGMT_CLONE_JUMBO_SNAP_EN	vxge_mBIT(63)
/*0x001c8*/	u64	rts_mgr_cfg0_vpmgmt_clone;
#define	VXGE_HW_RTS_MGR_CFG0_VPMGMT_CLONE_RTS_DP_SP_PRIORITY	vxge_mBIT(3)
#define	VXGE_HW_RTS_MGR_CFG0_VPMGMT_CLONE_FLEX_L4PRTCL_VALUE(val) \
							vxge_vBIT(val, 24, 8)
#define	VXGE_HW_RTS_MGR_CFG0_VPMGMT_CLONE_ICMP_TRASH	vxge_mBIT(35)
#define	VXGE_HW_RTS_MGR_CFG0_VPMGMT_CLONE_TCPSYN_TRASH	vxge_mBIT(39)
#define	VXGE_HW_RTS_MGR_CFG0_VPMGMT_CLONE_ZL4PYLD_TRASH	vxge_mBIT(43)
#define	VXGE_HW_RTS_MGR_CFG0_VPMGMT_CLONE_L4PRTCL_TCP_TRASH	vxge_mBIT(47)
#define	VXGE_HW_RTS_MGR_CFG0_VPMGMT_CLONE_L4PRTCL_UDP_TRASH	vxge_mBIT(51)
#define	VXGE_HW_RTS_MGR_CFG0_VPMGMT_CLONE_L4PRTCL_FLEX_TRASH	vxge_mBIT(55)
#define	VXGE_HW_RTS_MGR_CFG0_VPMGMT_CLONE_IPFRAG_TRASH	vxge_mBIT(59)
/*0x001d0*/	u64	rts_mgr_criteria_priority_vpmgmt_clone;
#define	VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_VPMGMT_CLONE_ETYPE(val) \
							vxge_vBIT(val, 5, 3)
#define	VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_VPMGMT_CLONE_ICMP_TCPSYN(val) \
							vxge_vBIT(val, 9, 3)
#define	VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_VPMGMT_CLONE_L4PN(val) \
							vxge_vBIT(val, 13, 3)
#define	VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_VPMGMT_CLONE_RANGE_L4PN(val) \
							vxge_vBIT(val, 17, 3)
#define	VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_VPMGMT_CLONE_RTH_IT(val) \
							vxge_vBIT(val, 21, 3)
#define VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_VPMGMT_CLONE_DS(val) \
							vxge_vBIT(val, 25, 3)
#define	VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_VPMGMT_CLONE_QOS(val) \
							vxge_vBIT(val, 29, 3)
#define	VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_VPMGMT_CLONE_ZL4PYLD(val) \
							vxge_vBIT(val, 33, 3)
#define	VXGE_HW_RTS_MGR_CRITERIA_PRIORITY_VPMGMT_CLONE_L4PRTCL(val) \
							vxge_vBIT(val, 37, 3)
/*0x001d8*/	u64	rxmac_cfg0_port_vpmgmt_clone[3];
#define	VXGE_HW_RXMAC_CFG0_PORT_VPMGMT_CLONE_RMAC_EN	vxge_mBIT(3)
#define	VXGE_HW_RXMAC_CFG0_PORT_VPMGMT_CLONE_STRIP_FCS	vxge_mBIT(7)
#define	VXGE_HW_RXMAC_CFG0_PORT_VPMGMT_CLONE_DISCARD_PFRM	vxge_mBIT(11)
#define	VXGE_HW_RXMAC_CFG0_PORT_VPMGMT_CLONE_IGNORE_FCS_ERR	vxge_mBIT(15)
#define	VXGE_HW_RXMAC_CFG0_PORT_VPMGMT_CLONE_IGNORE_LONG_ERR	vxge_mBIT(19)
#define	VXGE_HW_RXMAC_CFG0_PORT_VPMGMT_CLONE_IGNORE_USIZED_ERR	vxge_mBIT(23)
#define	VXGE_HW_RXMAC_CFG0_PORT_VPMGMT_CLONE_IGNORE_LEN_MISMATCH \
								vxge_mBIT(27)
#define	VXGE_HW_RXMAC_CFG0_PORT_VPMGMT_CLONE_MAX_PYLD_LEN(val) \
							vxge_vBIT(val, 50, 14)
/*0x001f0*/	u64	rxmac_pause_cfg_port_vpmgmt_clone[3];
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_VPMGMT_CLONE_GEN_EN	vxge_mBIT(3)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_VPMGMT_CLONE_RCV_EN	vxge_mBIT(7)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_VPMGMT_CLONE_ACCEL_SEND(val) \
							vxge_vBIT(val, 9, 3)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_VPMGMT_CLONE_DUAL_THR	vxge_mBIT(15)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_VPMGMT_CLONE_HIGH_PTIME(val) \
							vxge_vBIT(val, 20, 16)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_VPMGMT_CLONE_IGNORE_PF_FCS_ERR \
								vxge_mBIT(39)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_VPMGMT_CLONE_IGNORE_PF_LEN_ERR \
								vxge_mBIT(43)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_VPMGMT_CLONE_LIMITER_EN	vxge_mBIT(47)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_VPMGMT_CLONE_MAX_LIMIT(val) \
							vxge_vBIT(val, 48, 8)
#define	VXGE_HW_RXMAC_PAUSE_CFG_PORT_VPMGMT_CLONE_PERMIT_RATEMGMT_CTRL \
							vxge_mBIT(59)
	u8	unused00240[0x00240-0x00208];

/*0x00240*/	u64	xmac_vsport_choices_vp;
#define VXGE_HW_XMAC_VSPORT_CHOICES_VP_VSPORT_VECTOR(val) vxge_vBIT(val, 0, 17)
	u8	unused00260[0x00260-0x00248];

/*0x00260*/	u64	xgmac_gen_status_vpmgmt_clone;
#define	VXGE_HW_XGMAC_GEN_STATUS_VPMGMT_CLONE_XMACJ_NTWK_OK	vxge_mBIT(3)
#define	VXGE_HW_XGMAC_GEN_STATUS_VPMGMT_CLONE_XMACJ_NTWK_DATA_RATE \
								vxge_mBIT(11)
/*0x00268*/	u64	xgmac_status_port_vpmgmt_clone[2];
#define	VXGE_HW_XGMAC_STATUS_PORT_VPMGMT_CLONE_RMAC_REMOTE_FAULT \
								vxge_mBIT(3)
#define	VXGE_HW_XGMAC_STATUS_PORT_VPMGMT_CLONE_RMAC_LOCAL_FAULT	vxge_mBIT(7)
#define	VXGE_HW_XGMAC_STATUS_PORT_VPMGMT_CLONE_XMACJ_MAC_PHY_LAYER_AVAIL \
								vxge_mBIT(11)
#define	VXGE_HW_XGMAC_STATUS_PORT_VPMGMT_CLONE_XMACJ_PORT_OK	vxge_mBIT(15)
/*0x00278*/	u64	xmac_gen_cfg_vpmgmt_clone;
#define	VXGE_HW_XMAC_GEN_CFG_VPMGMT_CLONE_RATEMGMT_MAC_RATE_SEL(val) \
							vxge_vBIT(val, 2, 2)
#define	VXGE_HW_XMAC_GEN_CFG_VPMGMT_CLONE_TX_HEAD_DROP_WHEN_FAULT \
							vxge_mBIT(7)
#define	VXGE_HW_XMAC_GEN_CFG_VPMGMT_CLONE_FAULT_BEHAVIOUR	vxge_mBIT(27)
#define VXGE_HW_XMAC_GEN_CFG_VPMGMT_CLONE_PERIOD_NTWK_UP(val) \
							vxge_vBIT(val, 28, 4)
#define	VXGE_HW_XMAC_GEN_CFG_VPMGMT_CLONE_PERIOD_NTWK_DOWN(val) \
							vxge_vBIT(val, 32, 4)
/*0x00280*/	u64	xmac_timestamp_vpmgmt_clone;
#define	VXGE_HW_XMAC_TIMESTAMP_VPMGMT_CLONE_EN	vxge_mBIT(3)
#define VXGE_HW_XMAC_TIMESTAMP_VPMGMT_CLONE_USE_LINK_ID(val) \
							vxge_vBIT(val, 6, 2)
#define VXGE_HW_XMAC_TIMESTAMP_VPMGMT_CLONE_INTERVAL(val) vxge_vBIT(val, 12, 4)
#define	VXGE_HW_XMAC_TIMESTAMP_VPMGMT_CLONE_TIMER_RESTART	vxge_mBIT(19)
#define	VXGE_HW_XMAC_TIMESTAMP_VPMGMT_CLONE_XMACJ_ROLLOVER_CNT(val) \
							vxge_vBIT(val, 32, 16)
/*0x00288*/	u64	xmac_stats_gen_cfg_vpmgmt_clone;
#define	VXGE_HW_XMAC_STATS_GEN_CFG_VPMGMT_CLONE_PRTAGGR_CUM_TIMER(val) \
							vxge_vBIT(val, 4, 4)
#define	VXGE_HW_XMAC_STATS_GEN_CFG_VPMGMT_CLONE_VPATH_CUM_TIMER(val) \
							vxge_vBIT(val, 8, 4)
#define	VXGE_HW_XMAC_STATS_GEN_CFG_VPMGMT_CLONE_VLAN_HANDLING	vxge_mBIT(15)
/*0x00290*/	u64	xmac_cfg_port_vpmgmt_clone[3];
#define	VXGE_HW_XMAC_CFG_PORT_VPMGMT_CLONE_XGMII_LOOPBACK	vxge_mBIT(3)
#define	VXGE_HW_XMAC_CFG_PORT_VPMGMT_CLONE_XGMII_REVERSE_LOOPBACK \
								vxge_mBIT(7)
#define	VXGE_HW_XMAC_CFG_PORT_VPMGMT_CLONE_XGMII_TX_BEHAV	vxge_mBIT(11)
#define	VXGE_HW_XMAC_CFG_PORT_VPMGMT_CLONE_XGMII_RX_BEHAV	vxge_mBIT(15)
	u8	unused002c0[0x002c0-0x002a8];

/*0x002c0*/	u64	txmac_gen_cfg0_vpmgmt_clone;
#define	VXGE_HW_TXMAC_GEN_CFG0_VPMGMT_CLONE_CHOSEN_TX_PORT	vxge_mBIT(7)
/*0x002c8*/	u64	txmac_cfg0_port_vpmgmt_clone[3];
#define	VXGE_HW_TXMAC_CFG0_PORT_VPMGMT_CLONE_TMAC_EN	vxge_mBIT(3)
#define	VXGE_HW_TXMAC_CFG0_PORT_VPMGMT_CLONE_APPEND_PAD	vxge_mBIT(7)
#define VXGE_HW_TXMAC_CFG0_PORT_VPMGMT_CLONE_PAD_BYTE(val) vxge_vBIT(val, 8, 8)
	u8	unused00300[0x00300-0x002e0];

/*0x00300*/	u64	wol_mp_crc;
#define VXGE_HW_WOL_MP_CRC_CRC(val) vxge_vBIT(val, 0, 32)
#define	VXGE_HW_WOL_MP_CRC_RC_EN	vxge_mBIT(63)
/*0x00308*/	u64	wol_mp_mask_a;
#define VXGE_HW_WOL_MP_MASK_A_MASK(val) vxge_vBIT(val, 0, 64)
/*0x00310*/	u64	wol_mp_mask_b;
#define VXGE_HW_WOL_MP_MASK_B_MASK(val) vxge_vBIT(val, 0, 64)
	u8	unused00360[0x00360-0x00318];

/*0x00360*/	u64	fau_pa_cfg_vpmgmt_clone;
#define	VXGE_HW_FAU_PA_CFG_VPMGMT_CLONE_REPL_L4_COMP_CSUM	vxge_mBIT(3)
#define	VXGE_HW_FAU_PA_CFG_VPMGMT_CLONE_REPL_L3_INCL_CF	vxge_mBIT(7)
#define	VXGE_HW_FAU_PA_CFG_VPMGMT_CLONE_REPL_L3_COMP_CSUM	vxge_mBIT(11)
/*0x00368*/	u64	rx_datapath_util_vp_clone;
#define	VXGE_HW_RX_DATAPATH_UTIL_VP_CLONE_FAU_RX_UTILIZATION(val) \
							vxge_vBIT(val, 7, 9)
#define	VXGE_HW_RX_DATAPATH_UTIL_VP_CLONE_RX_UTIL_CFG(val) \
							vxge_vBIT(val, 16, 4)
#define	VXGE_HW_RX_DATAPATH_UTIL_VP_CLONE_FAU_RX_FRAC_UTIL(val) \
							vxge_vBIT(val, 20, 4)
#define	VXGE_HW_RX_DATAPATH_UTIL_VP_CLONE_RX_PKT_WEIGHT(val) \
							vxge_vBIT(val, 24, 4)
	u8	unused00380[0x00380-0x00370];

/*0x00380*/	u64	tx_datapath_util_vp_clone;
#define	VXGE_HW_TX_DATAPATH_UTIL_VP_CLONE_TPA_TX_UTILIZATION(val) \
							vxge_vBIT(val, 7, 9)
#define	VXGE_HW_TX_DATAPATH_UTIL_VP_CLONE_TX_UTIL_CFG(val) \
							vxge_vBIT(val, 16, 4)
#define	VXGE_HW_TX_DATAPATH_UTIL_VP_CLONE_TPA_TX_FRAC_UTIL(val) \
							vxge_vBIT(val, 20, 4)
#define	VXGE_HW_TX_DATAPATH_UTIL_VP_CLONE_TX_PKT_WEIGHT(val) \
							vxge_vBIT(val, 24, 4)

} __packed;

struct vxge_hw_vpath_reg {

	u8	unused00300[0x00300];

/*0x00300*/	u64	usdc_vpath;
#define VXGE_HW_USDC_VPATH_SGRP_ASSIGN(val) vxge_vBIT(val, 0, 32)
	u8	unused00a00[0x00a00-0x00308];

/*0x00a00*/	u64	wrdma_alarm_status;
#define	VXGE_HW_WRDMA_ALARM_STATUS_PRC_ALARM_PRC_INT	vxge_mBIT(1)
/*0x00a08*/	u64	wrdma_alarm_mask;
	u8	unused00a30[0x00a30-0x00a10];

/*0x00a30*/	u64	prc_alarm_reg;
#define	VXGE_HW_PRC_ALARM_REG_PRC_RING_BUMP	vxge_mBIT(0)
#define	VXGE_HW_PRC_ALARM_REG_PRC_RXDCM_SC_ERR	vxge_mBIT(1)
#define	VXGE_HW_PRC_ALARM_REG_PRC_RXDCM_SC_ABORT	vxge_mBIT(2)
#define	VXGE_HW_PRC_ALARM_REG_PRC_QUANTA_SIZE_ERR	vxge_mBIT(3)
/*0x00a38*/	u64	prc_alarm_mask;
/*0x00a40*/	u64	prc_alarm_alarm;
/*0x00a48*/	u64	prc_cfg1;
#define VXGE_HW_PRC_CFG1_RX_TIMER_VAL(val) vxge_vBIT(val, 3, 29)
#define	VXGE_HW_PRC_CFG1_TIM_RING_BUMP_INT_ENABLE	vxge_mBIT(34)
#define	VXGE_HW_PRC_CFG1_RTI_TINT_DISABLE	vxge_mBIT(35)
#define	VXGE_HW_PRC_CFG1_GREEDY_RETURN	vxge_mBIT(36)
#define	VXGE_HW_PRC_CFG1_QUICK_SHOT	vxge_mBIT(37)
#define	VXGE_HW_PRC_CFG1_RX_TIMER_CI	vxge_mBIT(39)
#define VXGE_HW_PRC_CFG1_RESET_TIMER_ON_RXD_RET(val) vxge_vBIT(val, 40, 2)
	u8	unused00a60[0x00a60-0x00a50];

/*0x00a60*/	u64	prc_cfg4;
#define	VXGE_HW_PRC_CFG4_IN_SVC	vxge_mBIT(7)
#define VXGE_HW_PRC_CFG4_RING_MODE(val) vxge_vBIT(val, 14, 2)
#define	VXGE_HW_PRC_CFG4_RXD_NO_SNOOP	vxge_mBIT(22)
#define	VXGE_HW_PRC_CFG4_FRM_NO_SNOOP	vxge_mBIT(23)
#define	VXGE_HW_PRC_CFG4_RTH_DISABLE	vxge_mBIT(31)
#define	VXGE_HW_PRC_CFG4_IGNORE_OWNERSHIP	vxge_mBIT(32)
#define	VXGE_HW_PRC_CFG4_SIGNAL_BENIGN_OVFLW	vxge_mBIT(36)
#define	VXGE_HW_PRC_CFG4_BIMODAL_INTERRUPT	vxge_mBIT(37)
#define VXGE_HW_PRC_CFG4_BACKOFF_INTERVAL(val) vxge_vBIT(val, 40, 24)
/*0x00a68*/	u64	prc_cfg5;
#define VXGE_HW_PRC_CFG5_RXD0_ADD(val) vxge_vBIT(val, 0, 61)
/*0x00a70*/	u64	prc_cfg6;
#define	VXGE_HW_PRC_CFG6_FRM_PAD_EN	vxge_mBIT(0)
#define	VXGE_HW_PRC_CFG6_QSIZE_ALIGNED_RXD	vxge_mBIT(2)
#define	VXGE_HW_PRC_CFG6_DOORBELL_MODE_EN	vxge_mBIT(5)
#define	VXGE_HW_PRC_CFG6_L3_CPC_TRSFR_CODE_EN	vxge_mBIT(8)
#define	VXGE_HW_PRC_CFG6_L4_CPC_TRSFR_CODE_EN	vxge_mBIT(9)
#define VXGE_HW_PRC_CFG6_RXD_CRXDT(val) vxge_vBIT(val, 23, 9)
#define VXGE_HW_PRC_CFG6_RXD_SPAT(val) vxge_vBIT(val, 36, 9)
/*0x00a78*/	u64	prc_cfg7;
#define VXGE_HW_PRC_CFG7_SCATTER_MODE(val) vxge_vBIT(val, 6, 2)
#define	VXGE_HW_PRC_CFG7_SMART_SCAT_EN	vxge_mBIT(11)
#define	VXGE_HW_PRC_CFG7_RXD_NS_CHG_EN	vxge_mBIT(12)
#define	VXGE_HW_PRC_CFG7_NO_HDR_SEPARATION	vxge_mBIT(14)
#define VXGE_HW_PRC_CFG7_RXD_BUFF_SIZE_MASK(val) vxge_vBIT(val, 20, 4)
#define VXGE_HW_PRC_CFG7_BUFF_SIZE0_MASK(val) vxge_vBIT(val, 27, 5)
/*0x00a80*/	u64	tim_dest_addr;
#define VXGE_HW_TIM_DEST_ADDR_TIM_DEST_ADDR(val) vxge_vBIT(val, 0, 64)
/*0x00a88*/	u64	prc_rxd_doorbell;
#define VXGE_HW_PRC_RXD_DOORBELL_NEW_QW_CNT(val) vxge_vBIT(val, 48, 16)
/*0x00a90*/	u64	rqa_prty_for_vp;
#define VXGE_HW_RQA_PRTY_FOR_VP_RQA_PRTY_FOR_VP(val) vxge_vBIT(val, 59, 5)
/*0x00a98*/	u64	rxdmem_size;
#define VXGE_HW_RXDMEM_SIZE_PRC_RXDMEM_SIZE(val) vxge_vBIT(val, 51, 13)
/*0x00aa0*/	u64	frm_in_progress_cnt;
#define	VXGE_HW_FRM_IN_PROGRESS_CNT_PRC_FRM_IN_PROGRESS_CNT(val) \
							vxge_vBIT(val, 59, 5)
/*0x00aa8*/	u64	rx_multi_cast_stats;
#define VXGE_HW_RX_MULTI_CAST_STATS_FRAME_DISCARD(val) vxge_vBIT(val, 48, 16)
/*0x00ab0*/	u64	rx_frm_transferred;
#define	VXGE_HW_RX_FRM_TRANSFERRED_RX_FRM_TRANSFERRED(val) \
							vxge_vBIT(val, 32, 32)
/*0x00ab8*/	u64	rxd_returned;
#define VXGE_HW_RXD_RETURNED_RXD_RETURNED(val) vxge_vBIT(val, 48, 16)
	u8	unused00c00[0x00c00-0x00ac0];

/*0x00c00*/	u64	kdfc_fifo_trpl_partition;
#define VXGE_HW_KDFC_FIFO_TRPL_PARTITION_LENGTH_0(val) vxge_vBIT(val, 17, 15)
#define VXGE_HW_KDFC_FIFO_TRPL_PARTITION_LENGTH_1(val) vxge_vBIT(val, 33, 15)
#define VXGE_HW_KDFC_FIFO_TRPL_PARTITION_LENGTH_2(val) vxge_vBIT(val, 49, 15)
/*0x00c08*/	u64	kdfc_fifo_trpl_ctrl;
#define	VXGE_HW_KDFC_FIFO_TRPL_CTRL_TRIPLET_ENABLE	vxge_mBIT(7)
/*0x00c10*/	u64	kdfc_trpl_fifo_0_ctrl;
#define VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_MODE(val) vxge_vBIT(val, 14, 2)
#define	VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_FLIP_EN	vxge_mBIT(22)
#define	VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_SWAP_EN	vxge_mBIT(23)
#define VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_INT_CTRL(val) vxge_vBIT(val, 26, 2)
#define	VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_CTRL_STRUC	vxge_mBIT(28)
#define	VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_ADD_PAD	vxge_mBIT(29)
#define	VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_NO_SNOOP	vxge_mBIT(30)
#define	VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_RLX_ORD	vxge_mBIT(31)
#define VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_SELECT(val) vxge_vBIT(val, 32, 8)
#define VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_INT_NO(val) vxge_vBIT(val, 41, 7)
#define VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_BIT_MAP(val) vxge_vBIT(val, 48, 16)
/*0x00c18*/	u64	kdfc_trpl_fifo_1_ctrl;
#define VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_MODE(val) vxge_vBIT(val, 14, 2)
#define	VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_FLIP_EN	vxge_mBIT(22)
#define	VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_SWAP_EN	vxge_mBIT(23)
#define VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_INT_CTRL(val) vxge_vBIT(val, 26, 2)
#define	VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_CTRL_STRUC	vxge_mBIT(28)
#define	VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_ADD_PAD	vxge_mBIT(29)
#define	VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_NO_SNOOP	vxge_mBIT(30)
#define	VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_RLX_ORD	vxge_mBIT(31)
#define VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_SELECT(val) vxge_vBIT(val, 32, 8)
#define VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_INT_NO(val) vxge_vBIT(val, 41, 7)
#define VXGE_HW_KDFC_TRPL_FIFO_1_CTRL_BIT_MAP(val) vxge_vBIT(val, 48, 16)
/*0x00c20*/	u64	kdfc_trpl_fifo_2_ctrl;
#define	VXGE_HW_KDFC_TRPL_FIFO_2_CTRL_FLIP_EN	vxge_mBIT(22)
#define	VXGE_HW_KDFC_TRPL_FIFO_2_CTRL_SWAP_EN	vxge_mBIT(23)
#define VXGE_HW_KDFC_TRPL_FIFO_2_CTRL_INT_CTRL(val) vxge_vBIT(val, 26, 2)
#define	VXGE_HW_KDFC_TRPL_FIFO_2_CTRL_CTRL_STRUC	vxge_mBIT(28)
#define	VXGE_HW_KDFC_TRPL_FIFO_2_CTRL_ADD_PAD	vxge_mBIT(29)
#define	VXGE_HW_KDFC_TRPL_FIFO_2_CTRL_NO_SNOOP	vxge_mBIT(30)
#define	VXGE_HW_KDFC_TRPL_FIFO_2_CTRL_RLX_ORD	vxge_mBIT(31)
#define VXGE_HW_KDFC_TRPL_FIFO_2_CTRL_SELECT(val) vxge_vBIT(val, 32, 8)
#define VXGE_HW_KDFC_TRPL_FIFO_2_CTRL_INT_NO(val) vxge_vBIT(val, 41, 7)
#define VXGE_HW_KDFC_TRPL_FIFO_2_CTRL_BIT_MAP(val) vxge_vBIT(val, 48, 16)
/*0x00c28*/	u64	kdfc_trpl_fifo_0_wb_address;
#define VXGE_HW_KDFC_TRPL_FIFO_0_WB_ADDRESS_ADD(val) vxge_vBIT(val, 0, 64)
/*0x00c30*/	u64	kdfc_trpl_fifo_1_wb_address;
#define VXGE_HW_KDFC_TRPL_FIFO_1_WB_ADDRESS_ADD(val) vxge_vBIT(val, 0, 64)
/*0x00c38*/	u64	kdfc_trpl_fifo_2_wb_address;
#define VXGE_HW_KDFC_TRPL_FIFO_2_WB_ADDRESS_ADD(val) vxge_vBIT(val, 0, 64)
/*0x00c40*/	u64	kdfc_trpl_fifo_offset;
#define VXGE_HW_KDFC_TRPL_FIFO_OFFSET_KDFC_RCTR0(val) vxge_vBIT(val, 1, 15)
#define VXGE_HW_KDFC_TRPL_FIFO_OFFSET_KDFC_RCTR1(val) vxge_vBIT(val, 17, 15)
#define VXGE_HW_KDFC_TRPL_FIFO_OFFSET_KDFC_RCTR2(val) vxge_vBIT(val, 33, 15)
/*0x00c48*/	u64	kdfc_drbl_triplet_total;
#define	VXGE_HW_KDFC_DRBL_TRIPLET_TOTAL_KDFC_MAX_SIZE(val) \
							vxge_vBIT(val, 17, 15)
	u8	unused00c60[0x00c60-0x00c50];

/*0x00c60*/	u64	usdc_drbl_ctrl;
#define	VXGE_HW_USDC_DRBL_CTRL_FLIP_EN	vxge_mBIT(22)
#define	VXGE_HW_USDC_DRBL_CTRL_SWAP_EN	vxge_mBIT(23)
/*0x00c68*/	u64	usdc_vp_ready;
#define	VXGE_HW_USDC_VP_READY_USDC_HTN_READY	vxge_mBIT(7)
#define	VXGE_HW_USDC_VP_READY_USDC_SRQ_READY	vxge_mBIT(15)
#define	VXGE_HW_USDC_VP_READY_USDC_CQRQ_READY	vxge_mBIT(23)
/*0x00c70*/	u64	kdfc_status;
#define	VXGE_HW_KDFC_STATUS_KDFC_WRR_0_READY	vxge_mBIT(0)
#define	VXGE_HW_KDFC_STATUS_KDFC_WRR_1_READY	vxge_mBIT(1)
#define	VXGE_HW_KDFC_STATUS_KDFC_WRR_2_READY	vxge_mBIT(2)
	u8	unused00c80[0x00c80-0x00c78];

/*0x00c80*/	u64	xmac_rpa_vcfg;
#define	VXGE_HW_XMAC_RPA_VCFG_IPV4_TCP_INCL_PH	vxge_mBIT(3)
#define	VXGE_HW_XMAC_RPA_VCFG_IPV6_TCP_INCL_PH	vxge_mBIT(7)
#define	VXGE_HW_XMAC_RPA_VCFG_IPV4_UDP_INCL_PH	vxge_mBIT(11)
#define	VXGE_HW_XMAC_RPA_VCFG_IPV6_UDP_INCL_PH	vxge_mBIT(15)
#define	VXGE_HW_XMAC_RPA_VCFG_L4_INCL_CF	vxge_mBIT(19)
#define	VXGE_HW_XMAC_RPA_VCFG_STRIP_VLAN_TAG	vxge_mBIT(23)
/*0x00c88*/	u64	rxmac_vcfg0;
#define VXGE_HW_RXMAC_VCFG0_RTS_MAX_FRM_LEN(val) vxge_vBIT(val, 2, 14)
#define	VXGE_HW_RXMAC_VCFG0_RTS_USE_MIN_LEN	vxge_mBIT(19)
#define VXGE_HW_RXMAC_VCFG0_RTS_MIN_FRM_LEN(val) vxge_vBIT(val, 26, 14)
#define	VXGE_HW_RXMAC_VCFG0_UCAST_ALL_ADDR_EN	vxge_mBIT(43)
#define	VXGE_HW_RXMAC_VCFG0_MCAST_ALL_ADDR_EN	vxge_mBIT(47)
#define	VXGE_HW_RXMAC_VCFG0_BCAST_EN	vxge_mBIT(51)
#define	VXGE_HW_RXMAC_VCFG0_ALL_VID_EN	vxge_mBIT(55)
/*0x00c90*/	u64	rxmac_vcfg1;
#define VXGE_HW_RXMAC_VCFG1_RTS_RTH_MULTI_IT_BD_MODE(val) vxge_vBIT(val, 42, 2)
#define	VXGE_HW_RXMAC_VCFG1_RTS_RTH_MULTI_IT_EN_MODE	vxge_mBIT(47)
#define	VXGE_HW_RXMAC_VCFG1_CONTRIB_L2_FLOW	vxge_mBIT(51)
/*0x00c98*/	u64	rts_access_steer_ctrl;
#define VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION(val) vxge_vBIT(val, 1, 7)
#define VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL(val) vxge_vBIT(val, 8, 4)
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_STROBE	vxge_mBIT(15)
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_BEHAV_TBL_SEL	vxge_mBIT(23)
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_TABLE_SEL	vxge_mBIT(27)
#define	VXGE_HW_RTS_ACCESS_STEER_CTRL_RMACJ_STATUS	vxge_mBIT(0)
#define VXGE_HW_RTS_ACCESS_STEER_CTRL_OFFSET(val) vxge_vBIT(val, 40, 8)
/*0x00ca0*/	u64	rts_access_steer_data0;
#define VXGE_HW_RTS_ACCESS_STEER_DATA0_DATA(val) vxge_vBIT(val, 0, 64)
/*0x00ca8*/	u64	rts_access_steer_data1;
#define VXGE_HW_RTS_ACCESS_STEER_DATA1_DATA(val) vxge_vBIT(val, 0, 64)
	u8	unused00d00[0x00d00-0x00cb0];

/*0x00d00*/	u64	xmac_vsport_choice;
#define VXGE_HW_XMAC_VSPORT_CHOICE_VSPORT_NUMBER(val) vxge_vBIT(val, 3, 5)
/*0x00d08*/	u64	xmac_stats_cfg;
/*0x00d10*/	u64	xmac_stats_access_cmd;
#define VXGE_HW_XMAC_STATS_ACCESS_CMD_OP(val) vxge_vBIT(val, 6, 2)
#define	VXGE_HW_XMAC_STATS_ACCESS_CMD_STROBE	vxge_mBIT(15)
#define VXGE_HW_XMAC_STATS_ACCESS_CMD_OFFSET_SEL(val) vxge_vBIT(val, 32, 8)
/*0x00d18*/	u64	xmac_stats_access_data;
#define VXGE_HW_XMAC_STATS_ACCESS_DATA_XSMGR_DATA(val) vxge_vBIT(val, 0, 64)
/*0x00d20*/	u64	asic_ntwk_vp_ctrl;
#define	VXGE_HW_ASIC_NTWK_VP_CTRL_REQ_TEST_NTWK	vxge_mBIT(3)
#define	VXGE_HW_ASIC_NTWK_VP_CTRL_XMACJ_SHOW_PORT_INFO	vxge_mBIT(55)
#define	VXGE_HW_ASIC_NTWK_VP_CTRL_XMACJ_PORT_NUM	vxge_mBIT(63)
	u8	unused00d30[0x00d30-0x00d28];

/*0x00d30*/	u64	xgmac_vp_int_status;
#define	VXGE_HW_XGMAC_VP_INT_STATUS_ASIC_NTWK_VP_ERR_ASIC_NTWK_VP_INT \
								vxge_mBIT(3)
/*0x00d38*/	u64	xgmac_vp_int_mask;
/*0x00d40*/	u64	asic_ntwk_vp_err_reg;
#define	VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_FLT	vxge_mBIT(3)
#define	VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_OK	vxge_mBIT(7)
#define	VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_FLT_OCCURR \
								vxge_mBIT(11)
#define	VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_OK_OCCURR \
							vxge_mBIT(15)
#define	VXGE_HW_ASIC_NTWK_VP_ERR_REG_XMACJ_NTWK_REAFFIRMED_FAULT \
							vxge_mBIT(19)
#define	VXGE_HW_ASIC_NTWK_VP_ERR_REG_XMACJ_NTWK_REAFFIRMED_OK	vxge_mBIT(23)
/*0x00d48*/	u64	asic_ntwk_vp_err_mask;
/*0x00d50*/	u64	asic_ntwk_vp_err_alarm;
	u8	unused00d80[0x00d80-0x00d58];

/*0x00d80*/	u64	rtdma_bw_ctrl;
#define	VXGE_HW_RTDMA_BW_CTRL_BW_CTRL_EN	vxge_mBIT(39)
#define VXGE_HW_RTDMA_BW_CTRL_DESIRED_BW(val) vxge_vBIT(val, 46, 18)
/*0x00d88*/	u64	rtdma_rd_optimization_ctrl;
#define	VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_GEN_INT_AFTER_ABORT	vxge_mBIT(3)
#define VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_PAD_MODE(val) vxge_vBIT(val, 6, 2)
#define VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_PAD_PATTERN(val) vxge_vBIT(val, 8, 8)
#define	VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_FB_WAIT_FOR_SPACE	vxge_mBIT(19)
#define VXGE_HW_PCI_EXP_DEVCTL_READRQ   0x7000  /* Max_Read_Request_Size */
#define VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_FB_FILL_THRESH(val) \
							vxge_vBIT(val, 21, 3)
#define	VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_TXD_PYLD_WMARK_EN	vxge_mBIT(28)
#define VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_TXD_PYLD_WMARK(val) \
							vxge_vBIT(val, 29, 3)
#define	VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_FB_ADDR_BDRY_EN	vxge_mBIT(35)
#define VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_FB_ADDR_BDRY(val) \
							vxge_vBIT(val, 37, 3)
#define	VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_TXD_WAIT_FOR_SPACE	vxge_mBIT(43)
#define	VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_TXD_FILL_THRESH(val) \
							vxge_vBIT(val, 51, 5)
#define	VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_TXD_ADDR_BDRY_EN	vxge_mBIT(59)
#define VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_TXD_ADDR_BDRY(val) \
							vxge_vBIT(val, 61, 3)
/*0x00d90*/	u64	pda_pcc_job_monitor;
#define	VXGE_HW_PDA_PCC_JOB_MONITOR_PDA_PCC_JOB_STATUS	vxge_mBIT(7)
/*0x00d98*/	u64	tx_protocol_assist_cfg;
#define	VXGE_HW_TX_PROTOCOL_ASSIST_CFG_LSOV2_EN	vxge_mBIT(6)
#define	VXGE_HW_TX_PROTOCOL_ASSIST_CFG_IPV6_KEEP_SEARCHING	vxge_mBIT(7)
	u8	unused01000[0x01000-0x00da0];

/*0x01000*/	u64	tim_cfg1_int_num[4];
#define VXGE_HW_TIM_CFG1_INT_NUM_BTIMER_VAL(val) vxge_vBIT(val, 6, 26)
#define	VXGE_HW_TIM_CFG1_INT_NUM_BITMP_EN	vxge_mBIT(35)
#define	VXGE_HW_TIM_CFG1_INT_NUM_TXFRM_CNT_EN	vxge_mBIT(36)
#define	VXGE_HW_TIM_CFG1_INT_NUM_TXD_CNT_EN	vxge_mBIT(37)
#define	VXGE_HW_TIM_CFG1_INT_NUM_TIMER_AC	vxge_mBIT(38)
#define	VXGE_HW_TIM_CFG1_INT_NUM_TIMER_CI	vxge_mBIT(39)
#define VXGE_HW_TIM_CFG1_INT_NUM_URNG_A(val) vxge_vBIT(val, 41, 7)
#define VXGE_HW_TIM_CFG1_INT_NUM_URNG_B(val) vxge_vBIT(val, 49, 7)
#define VXGE_HW_TIM_CFG1_INT_NUM_URNG_C(val) vxge_vBIT(val, 57, 7)
/*0x01020*/	u64	tim_cfg2_int_num[4];
#define VXGE_HW_TIM_CFG2_INT_NUM_UEC_A(val) vxge_vBIT(val, 0, 16)
#define VXGE_HW_TIM_CFG2_INT_NUM_UEC_B(val) vxge_vBIT(val, 16, 16)
#define VXGE_HW_TIM_CFG2_INT_NUM_UEC_C(val) vxge_vBIT(val, 32, 16)
#define VXGE_HW_TIM_CFG2_INT_NUM_UEC_D(val) vxge_vBIT(val, 48, 16)
/*0x01040*/	u64	tim_cfg3_int_num[4];
#define	VXGE_HW_TIM_CFG3_INT_NUM_TIMER_RI	vxge_mBIT(0)
#define VXGE_HW_TIM_CFG3_INT_NUM_RTIMER_EVENT_SF(val) vxge_vBIT(val, 1, 4)
#define VXGE_HW_TIM_CFG3_INT_NUM_RTIMER_VAL(val) vxge_vBIT(val, 6, 26)
#define VXGE_HW_TIM_CFG3_INT_NUM_UTIL_SEL(val) vxge_vBIT(val, 32, 6)
#define VXGE_HW_TIM_CFG3_INT_NUM_LTIMER_VAL(val) vxge_vBIT(val, 38, 26)
/*0x01060*/	u64	tim_wrkld_clc;
#define VXGE_HW_TIM_WRKLD_CLC_WRKLD_EVAL_PRD(val) vxge_vBIT(val, 0, 32)
#define VXGE_HW_TIM_WRKLD_CLC_WRKLD_EVAL_DIV(val) vxge_vBIT(val, 35, 5)
#define	VXGE_HW_TIM_WRKLD_CLC_CNT_FRM_BYTE	vxge_mBIT(40)
#define VXGE_HW_TIM_WRKLD_CLC_CNT_RX_TX(val) vxge_vBIT(val, 41, 2)
#define	VXGE_HW_TIM_WRKLD_CLC_CNT_LNK_EN	vxge_mBIT(43)
#define VXGE_HW_TIM_WRKLD_CLC_HOST_UTIL(val) vxge_vBIT(val, 57, 7)
/*0x01068*/	u64	tim_bitmap;
#define VXGE_HW_TIM_BITMAP_MASK(val) vxge_vBIT(val, 0, 32)
#define	VXGE_HW_TIM_BITMAP_LLROOT_RXD_EN	vxge_mBIT(32)
#define	VXGE_HW_TIM_BITMAP_LLROOT_TXD_EN	vxge_mBIT(33)
/*0x01070*/	u64	tim_ring_assn;
#define VXGE_HW_TIM_RING_ASSN_INT_NUM(val) vxge_vBIT(val, 6, 2)
/*0x01078*/	u64	tim_remap;
#define	VXGE_HW_TIM_REMAP_TX_EN	vxge_mBIT(5)
#define	VXGE_HW_TIM_REMAP_RX_EN	vxge_mBIT(6)
#define	VXGE_HW_TIM_REMAP_OFFLOAD_EN	vxge_mBIT(7)
#define VXGE_HW_TIM_REMAP_TO_VPATH_NUM(val) vxge_vBIT(val, 11, 5)
/*0x01080*/	u64	tim_vpath_map;
#define VXGE_HW_TIM_VPATH_MAP_BMAP_ROOT(val) vxge_vBIT(val, 0, 32)
/*0x01088*/	u64	tim_pci_cfg;
#define	VXGE_HW_TIM_PCI_CFG_ADD_PAD	vxge_mBIT(7)
#define	VXGE_HW_TIM_PCI_CFG_NO_SNOOP	vxge_mBIT(15)
#define	VXGE_HW_TIM_PCI_CFG_RELAXED	vxge_mBIT(23)
#define	VXGE_HW_TIM_PCI_CFG_CTL_STR	vxge_mBIT(31)
	u8	unused01100[0x01100-0x01090];

/*0x01100*/	u64	sgrp_assign;
#define VXGE_HW_SGRP_ASSIGN_SGRP_ASSIGN(val) vxge_vBIT(val, 0, 64)
/*0x01108*/	u64	sgrp_aoa_and_result;
#define	VXGE_HW_SGRP_AOA_AND_RESULT_PET_SGRP_AOA_AND_RESULT(val) \
							vxge_vBIT(val, 0, 64)
/*0x01110*/	u64	rpe_pci_cfg;
#define	VXGE_HW_RPE_PCI_CFG_PAD_LRO_DATA_ENABLE	vxge_mBIT(7)
#define	VXGE_HW_RPE_PCI_CFG_PAD_LRO_HDR_ENABLE	vxge_mBIT(8)
#define	VXGE_HW_RPE_PCI_CFG_PAD_LRO_CQE_ENABLE	vxge_mBIT(9)
#define	VXGE_HW_RPE_PCI_CFG_PAD_NONLL_CQE_ENABLE	vxge_mBIT(10)
#define	VXGE_HW_RPE_PCI_CFG_PAD_BASE_LL_CQE_ENABLE	vxge_mBIT(11)
#define	VXGE_HW_RPE_PCI_CFG_PAD_LL_CQE_IDATA_ENABLE	vxge_mBIT(12)
#define	VXGE_HW_RPE_PCI_CFG_PAD_CQRQ_IR_ENABLE	vxge_mBIT(13)
#define	VXGE_HW_RPE_PCI_CFG_PAD_CQSQ_IR_ENABLE	vxge_mBIT(14)
#define	VXGE_HW_RPE_PCI_CFG_PAD_CQRR_IR_ENABLE	vxge_mBIT(15)
#define	VXGE_HW_RPE_PCI_CFG_NOSNOOP_DATA	vxge_mBIT(18)
#define	VXGE_HW_RPE_PCI_CFG_NOSNOOP_NONLL_CQE	vxge_mBIT(19)
#define	VXGE_HW_RPE_PCI_CFG_NOSNOOP_LL_CQE	vxge_mBIT(20)
#define	VXGE_HW_RPE_PCI_CFG_NOSNOOP_CQRQ_IR	vxge_mBIT(21)
#define	VXGE_HW_RPE_PCI_CFG_NOSNOOP_CQSQ_IR	vxge_mBIT(22)
#define	VXGE_HW_RPE_PCI_CFG_NOSNOOP_CQRR_IR	vxge_mBIT(23)
#define	VXGE_HW_RPE_PCI_CFG_RELAXED_DATA	vxge_mBIT(26)
#define	VXGE_HW_RPE_PCI_CFG_RELAXED_NONLL_CQE	vxge_mBIT(27)
#define	VXGE_HW_RPE_PCI_CFG_RELAXED_LL_CQE	vxge_mBIT(28)
#define	VXGE_HW_RPE_PCI_CFG_RELAXED_CQRQ_IR	vxge_mBIT(29)
#define	VXGE_HW_RPE_PCI_CFG_RELAXED_CQSQ_IR	vxge_mBIT(30)
#define	VXGE_HW_RPE_PCI_CFG_RELAXED_CQRR_IR	vxge_mBIT(31)
/*0x01118*/	u64	rpe_lro_cfg;
#define	VXGE_HW_RPE_LRO_CFG_SUPPRESS_LRO_ETH_TRLR	vxge_mBIT(7)
#define	VXGE_HW_RPE_LRO_CFG_ALLOW_LRO_SNAP_SNAPJUMBO_MRG	vxge_mBIT(11)
#define	VXGE_HW_RPE_LRO_CFG_ALLOW_LRO_LLC_LLCJUMBO_MRG	vxge_mBIT(15)
#define	VXGE_HW_RPE_LRO_CFG_INCL_ACK_CNT_IN_CQE	vxge_mBIT(23)
/*0x01120*/	u64	pe_mr2vp_ack_blk_limit;
#define VXGE_HW_PE_MR2VP_ACK_BLK_LIMIT_BLK_LIMIT(val) vxge_vBIT(val, 32, 32)
/*0x01128*/	u64	pe_mr2vp_rirr_lirr_blk_limit;
#define	VXGE_HW_PE_MR2VP_RIRR_LIRR_BLK_LIMIT_RIRR_BLK_LIMIT(val) \
							vxge_vBIT(val, 0, 32)
#define	VXGE_HW_PE_MR2VP_RIRR_LIRR_BLK_LIMIT_LIRR_BLK_LIMIT(val) \
							vxge_vBIT(val, 32, 32)
/*0x01130*/	u64	txpe_pci_nce_cfg;
#define VXGE_HW_TXPE_PCI_NCE_CFG_NCE_THRESH(val) vxge_vBIT(val, 0, 32)
#define	VXGE_HW_TXPE_PCI_NCE_CFG_PAD_TOWI_ENABLE	vxge_mBIT(55)
#define	VXGE_HW_TXPE_PCI_NCE_CFG_NOSNOOP_TOWI	vxge_mBIT(63)
	u8	unused01180[0x01180-0x01138];

/*0x01180*/	u64	msg_qpad_en_cfg;
#define	VXGE_HW_MSG_QPAD_EN_CFG_UMQ_BWR_READ	vxge_mBIT(3)
#define	VXGE_HW_MSG_QPAD_EN_CFG_DMQ_BWR_READ	vxge_mBIT(7)
#define	VXGE_HW_MSG_QPAD_EN_CFG_MXP_GENDMA_READ	vxge_mBIT(11)
#define	VXGE_HW_MSG_QPAD_EN_CFG_UXP_GENDMA_READ	vxge_mBIT(15)
#define	VXGE_HW_MSG_QPAD_EN_CFG_UMQ_MSG_WRITE	vxge_mBIT(19)
#define	VXGE_HW_MSG_QPAD_EN_CFG_UMQDMQ_IR_WRITE	vxge_mBIT(23)
#define	VXGE_HW_MSG_QPAD_EN_CFG_MXP_GENDMA_WRITE	vxge_mBIT(27)
#define	VXGE_HW_MSG_QPAD_EN_CFG_UXP_GENDMA_WRITE	vxge_mBIT(31)
/*0x01188*/	u64	msg_pci_cfg;
#define	VXGE_HW_MSG_PCI_CFG_GENDMA_NO_SNOOP	vxge_mBIT(3)
#define	VXGE_HW_MSG_PCI_CFG_UMQDMQ_IR_NO_SNOOP	vxge_mBIT(7)
#define	VXGE_HW_MSG_PCI_CFG_UMQ_NO_SNOOP	vxge_mBIT(11)
#define	VXGE_HW_MSG_PCI_CFG_DMQ_NO_SNOOP	vxge_mBIT(15)
/*0x01190*/	u64	umqdmq_ir_init;
#define VXGE_HW_UMQDMQ_IR_INIT_HOST_WRITE_ADD(val) vxge_vBIT(val, 0, 64)
/*0x01198*/	u64	dmq_ir_int;
#define	VXGE_HW_DMQ_IR_INT_IMMED_ENABLE	vxge_mBIT(6)
#define	VXGE_HW_DMQ_IR_INT_EVENT_ENABLE	vxge_mBIT(7)
#define VXGE_HW_DMQ_IR_INT_NUMBER(val) vxge_vBIT(val, 9, 7)
#define VXGE_HW_DMQ_IR_INT_BITMAP(val) vxge_vBIT(val, 16, 16)
/*0x011a0*/	u64	dmq_bwr_init_add;
#define VXGE_HW_DMQ_BWR_INIT_ADD_HOST(val) vxge_vBIT(val, 0, 64)
/*0x011a8*/	u64	dmq_bwr_init_byte;
#define VXGE_HW_DMQ_BWR_INIT_BYTE_COUNT(val) vxge_vBIT(val, 0, 32)
/*0x011b0*/	u64	dmq_ir;
#define VXGE_HW_DMQ_IR_POLICY(val) vxge_vBIT(val, 0, 8)
/*0x011b8*/	u64	umq_int;
#define	VXGE_HW_UMQ_INT_IMMED_ENABLE	vxge_mBIT(6)
#define	VXGE_HW_UMQ_INT_EVENT_ENABLE	vxge_mBIT(7)
#define VXGE_HW_UMQ_INT_NUMBER(val) vxge_vBIT(val, 9, 7)
#define VXGE_HW_UMQ_INT_BITMAP(val) vxge_vBIT(val, 16, 16)
/*0x011c0*/	u64	umq_mr2vp_bwr_pfch_init;
#define VXGE_HW_UMQ_MR2VP_BWR_PFCH_INIT_NUMBER(val) vxge_vBIT(val, 0, 8)
/*0x011c8*/	u64	umq_bwr_pfch_ctrl;
#define	VXGE_HW_UMQ_BWR_PFCH_CTRL_POLL_EN	vxge_mBIT(3)
/*0x011d0*/	u64	umq_mr2vp_bwr_eol;
#define VXGE_HW_UMQ_MR2VP_BWR_EOL_POLL_LATENCY(val) vxge_vBIT(val, 32, 32)
/*0x011d8*/	u64	umq_bwr_init_add;
#define VXGE_HW_UMQ_BWR_INIT_ADD_HOST(val) vxge_vBIT(val, 0, 64)
/*0x011e0*/	u64	umq_bwr_init_byte;
#define VXGE_HW_UMQ_BWR_INIT_BYTE_COUNT(val) vxge_vBIT(val, 0, 32)
/*0x011e8*/	u64	gendma_int;
/*0x011f0*/	u64	umqdmq_ir_init_notify;
#define	VXGE_HW_UMQDMQ_IR_INIT_NOTIFY_PULSE	vxge_mBIT(3)
/*0x011f8*/	u64	dmq_init_notify;
#define	VXGE_HW_DMQ_INIT_NOTIFY_PULSE	vxge_mBIT(3)
/*0x01200*/	u64	umq_init_notify;
#define	VXGE_HW_UMQ_INIT_NOTIFY_PULSE	vxge_mBIT(3)
	u8	unused01380[0x01380-0x01208];

/*0x01380*/	u64	tpa_cfg;
#define	VXGE_HW_TPA_CFG_IGNORE_FRAME_ERR	vxge_mBIT(3)
#define	VXGE_HW_TPA_CFG_IPV6_STOP_SEARCHING	vxge_mBIT(7)
#define	VXGE_HW_TPA_CFG_L4_PSHDR_PRESENT	vxge_mBIT(11)
#define	VXGE_HW_TPA_CFG_SUPPORT_MOBILE_IPV6_HDRS	vxge_mBIT(15)
	u8	unused01400[0x01400-0x01388];

/*0x01400*/	u64	tx_vp_reset_discarded_frms;
#define	VXGE_HW_TX_VP_RESET_DISCARDED_FRMS_TX_VP_RESET_DISCARDED_FRMS(val) \
							vxge_vBIT(val, 48, 16)
	u8	unused01480[0x01480-0x01408];

/*0x01480*/	u64	fau_rpa_vcfg;
#define	VXGE_HW_FAU_RPA_VCFG_L4_COMP_CSUM	vxge_mBIT(7)
#define	VXGE_HW_FAU_RPA_VCFG_L3_INCL_CF	vxge_mBIT(11)
#define	VXGE_HW_FAU_RPA_VCFG_L3_COMP_CSUM	vxge_mBIT(15)
	u8	unused014d0[0x014d0-0x01488];

/*0x014d0*/	u64	dbg_stats_rx_mpa;
#define VXGE_HW_DBG_STATS_RX_MPA_CRC_FAIL_FRMS(val) vxge_vBIT(val, 0, 16)
#define VXGE_HW_DBG_STATS_RX_MPA_MRK_FAIL_FRMS(val) vxge_vBIT(val, 16, 16)
#define VXGE_HW_DBG_STATS_RX_MPA_LEN_FAIL_FRMS(val) vxge_vBIT(val, 32, 16)
/*0x014d8*/	u64	dbg_stats_rx_fau;
#define VXGE_HW_DBG_STATS_RX_FAU_RX_WOL_FRMS(val) vxge_vBIT(val, 0, 16)
#define	VXGE_HW_DBG_STATS_RX_FAU_RX_VP_RESET_DISCARDED_FRMS(val) \
							vxge_vBIT(val, 16, 16)
#define	VXGE_HW_DBG_STATS_RX_FAU_RX_PERMITTED_FRMS(val) \
							vxge_vBIT(val, 32, 32)
	u8	unused014f0[0x014f0-0x014e0];

/*0x014f0*/	u64	fbmc_vp_rdy;
#define	VXGE_HW_FBMC_VP_RDY_QUEUE_SPAV_FM	vxge_mBIT(0)
	u8	unused01e00[0x01e00-0x014f8];

/*0x01e00*/	u64	vpath_pcipif_int_status;
#define \
VXGE_HW_VPATH_PCIPIF_INT_STATUS_SRPCIM_MSG_TO_VPATH_SRPCIM_MSG_TO_VPATH_INT \
								vxge_mBIT(3)
#define	VXGE_HW_VPATH_PCIPIF_INT_STATUS_VPATH_SPARE_R1_VPATH_SPARE_R1_INT \
								vxge_mBIT(7)
/*0x01e08*/	u64	vpath_pcipif_int_mask;
	u8	unused01e20[0x01e20-0x01e10];

/*0x01e20*/	u64	srpcim_msg_to_vpath_reg;
#define	VXGE_HW_SRPCIM_MSG_TO_VPATH_REG_SWIF_SRPCIM_TO_VPATH_RMSG_INT \
								vxge_mBIT(3)
/*0x01e28*/	u64	srpcim_msg_to_vpath_mask;
/*0x01e30*/	u64	srpcim_msg_to_vpath_alarm;
	u8	unused01ea0[0x01ea0-0x01e38];

/*0x01ea0*/	u64	vpath_to_srpcim_wmsg;
#define VXGE_HW_VPATH_TO_SRPCIM_WMSG_VPATH_TO_SRPCIM_WMSG(val) \
							vxge_vBIT(val, 0, 64)
/*0x01ea8*/	u64	vpath_to_srpcim_wmsg_trig;
#define	VXGE_HW_VPATH_TO_SRPCIM_WMSG_TRIG_VPATH_TO_SRPCIM_WMSG_TRIG \
							vxge_mBIT(0)
	u8	unused02000[0x02000-0x01eb0];

/*0x02000*/	u64	vpath_general_int_status;
#define	VXGE_HW_VPATH_GENERAL_INT_STATUS_PIC_INT	vxge_mBIT(3)
#define	VXGE_HW_VPATH_GENERAL_INT_STATUS_PCI_INT	vxge_mBIT(7)
#define	VXGE_HW_VPATH_GENERAL_INT_STATUS_WRDMA_INT	vxge_mBIT(15)
#define	VXGE_HW_VPATH_GENERAL_INT_STATUS_XMAC_INT	vxge_mBIT(19)
/*0x02008*/	u64	vpath_general_int_mask;
#define	VXGE_HW_VPATH_GENERAL_INT_MASK_PIC_INT	vxge_mBIT(3)
#define	VXGE_HW_VPATH_GENERAL_INT_MASK_PCI_INT	vxge_mBIT(7)
#define	VXGE_HW_VPATH_GENERAL_INT_MASK_WRDMA_INT	vxge_mBIT(15)
#define	VXGE_HW_VPATH_GENERAL_INT_MASK_XMAC_INT	vxge_mBIT(19)
/*0x02010*/	u64	vpath_ppif_int_status;
#define	VXGE_HW_VPATH_PPIF_INT_STATUS_KDFCCTL_ERRORS_KDFCCTL_INT \
							vxge_mBIT(3)
#define	VXGE_HW_VPATH_PPIF_INT_STATUS_GENERAL_ERRORS_GENERAL_INT \
							vxge_mBIT(7)
#define	VXGE_HW_VPATH_PPIF_INT_STATUS_PCI_CONFIG_ERRORS_PCI_CONFIG_INT \
							vxge_mBIT(11)
#define \
VXGE_HW_VPATH_PPIF_INT_STATUS_MRPCIM_TO_VPATH_ALARM_MRPCIM_TO_VPATH_ALARM_INT \
							vxge_mBIT(15)
#define \
VXGE_HW_VPATH_PPIF_INT_STATUS_SRPCIM_TO_VPATH_ALARM_SRPCIM_TO_VPATH_ALARM_INT \
							vxge_mBIT(19)
/*0x02018*/	u64	vpath_ppif_int_mask;
/*0x02020*/	u64	kdfcctl_errors_reg;
#define	VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO0_OVRWR	vxge_mBIT(3)
#define	VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO1_OVRWR	vxge_mBIT(7)
#define	VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO2_OVRWR	vxge_mBIT(11)
#define	VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO0_POISON	vxge_mBIT(15)
#define	VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO1_POISON	vxge_mBIT(19)
#define	VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO2_POISON	vxge_mBIT(23)
#define	VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO0_DMA_ERR	vxge_mBIT(31)
#define	VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO1_DMA_ERR	vxge_mBIT(35)
#define	VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO2_DMA_ERR	vxge_mBIT(39)
/*0x02028*/	u64	kdfcctl_errors_mask;
/*0x02030*/	u64	kdfcctl_errors_alarm;
	u8	unused02040[0x02040-0x02038];

/*0x02040*/	u64	general_errors_reg;
#define	VXGE_HW_GENERAL_ERRORS_REG_DBLGEN_FIFO0_OVRFLOW	vxge_mBIT(3)
#define	VXGE_HW_GENERAL_ERRORS_REG_DBLGEN_FIFO1_OVRFLOW	vxge_mBIT(7)
#define	VXGE_HW_GENERAL_ERRORS_REG_DBLGEN_FIFO2_OVRFLOW	vxge_mBIT(11)
#define	VXGE_HW_GENERAL_ERRORS_REG_STATSB_PIF_CHAIN_ERR	vxge_mBIT(15)
#define	VXGE_HW_GENERAL_ERRORS_REG_STATSB_DROP_TIMEOUT_REQ	vxge_mBIT(19)
#define	VXGE_HW_GENERAL_ERRORS_REG_TGT_ILLEGAL_ACCESS	vxge_mBIT(27)
#define	VXGE_HW_GENERAL_ERRORS_REG_INI_SERR_DET	vxge_mBIT(31)
/*0x02048*/	u64	general_errors_mask;
/*0x02050*/	u64	general_errors_alarm;
/*0x02058*/	u64	pci_config_errors_reg;
#define	VXGE_HW_PCI_CONFIG_ERRORS_REG_PCICONFIG_STATUS_ERR	vxge_mBIT(3)
#define	VXGE_HW_PCI_CONFIG_ERRORS_REG_PCICONFIG_UNCOR_ERR	vxge_mBIT(7)
#define	VXGE_HW_PCI_CONFIG_ERRORS_REG_PCICONFIG_COR_ERR	vxge_mBIT(11)
/*0x02060*/	u64	pci_config_errors_mask;
/*0x02068*/	u64	pci_config_errors_alarm;
/*0x02070*/	u64	mrpcim_to_vpath_alarm_reg;
#define	VXGE_HW_MRPCIM_TO_VPATH_ALARM_REG_PPIF_MRPCIM_TO_VPATH_ALARM \
								vxge_mBIT(3)
/*0x02078*/	u64	mrpcim_to_vpath_alarm_mask;
/*0x02080*/	u64	mrpcim_to_vpath_alarm_alarm;
/*0x02088*/	u64	srpcim_to_vpath_alarm_reg;
#define	VXGE_HW_SRPCIM_TO_VPATH_ALARM_REG_PPIF_SRPCIM_TO_VPATH_ALARM(val) \
							vxge_vBIT(val, 0, 17)
/*0x02090*/	u64	srpcim_to_vpath_alarm_mask;
/*0x02098*/	u64	srpcim_to_vpath_alarm_alarm;
	u8	unused02108[0x02108-0x020a0];

/*0x02108*/	u64	kdfcctl_status;
#define VXGE_HW_KDFCCTL_STATUS_KDFCCTL_FIFO0_PRES(val) vxge_vBIT(val, 0, 8)
#define VXGE_HW_KDFCCTL_STATUS_KDFCCTL_FIFO1_PRES(val) vxge_vBIT(val, 8, 8)
#define VXGE_HW_KDFCCTL_STATUS_KDFCCTL_FIFO2_PRES(val) vxge_vBIT(val, 16, 8)
#define VXGE_HW_KDFCCTL_STATUS_KDFCCTL_FIFO0_OVRWR(val) vxge_vBIT(val, 24, 8)
#define VXGE_HW_KDFCCTL_STATUS_KDFCCTL_FIFO1_OVRWR(val) vxge_vBIT(val, 32, 8)
#define VXGE_HW_KDFCCTL_STATUS_KDFCCTL_FIFO2_OVRWR(val) vxge_vBIT(val, 40, 8)
/*0x02110*/	u64	rsthdlr_status;
#define	VXGE_HW_RSTHDLR_STATUS_RSTHDLR_CURRENT_RESET	vxge_mBIT(3)
#define VXGE_HW_RSTHDLR_STATUS_RSTHDLR_CURRENT_VPIN(val) vxge_vBIT(val, 6, 2)
/*0x02118*/	u64	fifo0_status;
#define VXGE_HW_FIFO0_STATUS_DBLGEN_FIFO0_RDIDX(val) vxge_vBIT(val, 0, 12)
/*0x02120*/	u64	fifo1_status;
#define VXGE_HW_FIFO1_STATUS_DBLGEN_FIFO1_RDIDX(val) vxge_vBIT(val, 0, 12)
/*0x02128*/	u64	fifo2_status;
#define VXGE_HW_FIFO2_STATUS_DBLGEN_FIFO2_RDIDX(val) vxge_vBIT(val, 0, 12)
	u8	unused02158[0x02158-0x02130];

/*0x02158*/	u64	tgt_illegal_access;
#define VXGE_HW_TGT_ILLEGAL_ACCESS_SWIF_REGION(val) vxge_vBIT(val, 1, 7)
	u8	unused02200[0x02200-0x02160];

/*0x02200*/	u64	vpath_general_cfg1;
#define VXGE_HW_VPATH_GENERAL_CFG1_TC_VALUE(val) vxge_vBIT(val, 1, 3)
#define	VXGE_HW_VPATH_GENERAL_CFG1_DATA_BYTE_SWAPEN	vxge_mBIT(7)
#define	VXGE_HW_VPATH_GENERAL_CFG1_DATA_FLIPEN	vxge_mBIT(11)
#define	VXGE_HW_VPATH_GENERAL_CFG1_CTL_BYTE_SWAPEN	vxge_mBIT(15)
#define	VXGE_HW_VPATH_GENERAL_CFG1_CTL_FLIPEN	vxge_mBIT(23)
#define	VXGE_HW_VPATH_GENERAL_CFG1_MSIX_ADDR_SWAPEN	vxge_mBIT(51)
#define	VXGE_HW_VPATH_GENERAL_CFG1_MSIX_ADDR_FLIPEN	vxge_mBIT(55)
#define	VXGE_HW_VPATH_GENERAL_CFG1_MSIX_DATA_SWAPEN	vxge_mBIT(59)
#define	VXGE_HW_VPATH_GENERAL_CFG1_MSIX_DATA_FLIPEN	vxge_mBIT(63)
/*0x02208*/	u64	vpath_general_cfg2;
#define VXGE_HW_VPATH_GENERAL_CFG2_SIZE_QUANTUM(val) vxge_vBIT(val, 1, 3)
/*0x02210*/	u64	vpath_general_cfg3;
#define	VXGE_HW_VPATH_GENERAL_CFG3_IGNORE_VPATH_RST_FOR_INTA	vxge_mBIT(3)
	u8	unused02220[0x02220-0x02218];

/*0x02220*/	u64	kdfcctl_cfg0;
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_SWAPEN_FIFO0	vxge_mBIT(1)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_SWAPEN_FIFO1	vxge_mBIT(2)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_SWAPEN_FIFO2	vxge_mBIT(3)
#define	VXGE_HW_KDFCCTL_CFG0_BIT_FLIPEN_FIFO0	vxge_mBIT(5)
#define	VXGE_HW_KDFCCTL_CFG0_BIT_FLIPEN_FIFO1	vxge_mBIT(6)
#define	VXGE_HW_KDFCCTL_CFG0_BIT_FLIPEN_FIFO2	vxge_mBIT(7)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE0_FIFO0	vxge_mBIT(9)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE0_FIFO1	vxge_mBIT(10)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE0_FIFO2	vxge_mBIT(11)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE1_FIFO0	vxge_mBIT(13)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE1_FIFO1	vxge_mBIT(14)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE1_FIFO2	vxge_mBIT(15)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE2_FIFO0	vxge_mBIT(17)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE2_FIFO1	vxge_mBIT(18)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE2_FIFO2	vxge_mBIT(19)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE3_FIFO0	vxge_mBIT(21)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE3_FIFO1	vxge_mBIT(22)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE3_FIFO2	vxge_mBIT(23)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE4_FIFO0	vxge_mBIT(25)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE4_FIFO1	vxge_mBIT(26)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE4_FIFO2	vxge_mBIT(27)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE5_FIFO0	vxge_mBIT(29)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE5_FIFO1	vxge_mBIT(30)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE5_FIFO2	vxge_mBIT(31)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE6_FIFO0	vxge_mBIT(33)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE6_FIFO1	vxge_mBIT(34)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE6_FIFO2	vxge_mBIT(35)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE7_FIFO0	vxge_mBIT(37)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE7_FIFO1	vxge_mBIT(38)
#define	VXGE_HW_KDFCCTL_CFG0_BYTE_MASK_BYTE7_FIFO2	vxge_mBIT(39)

	u8	unused02268[0x02268-0x02228];

/*0x02268*/	u64	stats_cfg;
#define VXGE_HW_STATS_CFG_START_HOST_ADDR(val) vxge_vBIT(val, 0, 57)
/*0x02270*/	u64	interrupt_cfg0;
#define VXGE_HW_INTERRUPT_CFG0_MSIX_FOR_RXTI(val) vxge_vBIT(val, 1, 7)
#define VXGE_HW_INTERRUPT_CFG0_GROUP0_MSIX_FOR_TXTI(val) vxge_vBIT(val, 9, 7)
#define VXGE_HW_INTERRUPT_CFG0_GROUP1_MSIX_FOR_TXTI(val) vxge_vBIT(val, 17, 7)
#define VXGE_HW_INTERRUPT_CFG0_GROUP2_MSIX_FOR_TXTI(val) vxge_vBIT(val, 25, 7)
#define VXGE_HW_INTERRUPT_CFG0_GROUP3_MSIX_FOR_TXTI(val) vxge_vBIT(val, 33, 7)
	u8	unused02280[0x02280-0x02278];

/*0x02280*/	u64	interrupt_cfg2;
#define VXGE_HW_INTERRUPT_CFG2_ALARM_MAP_TO_MSG(val) vxge_vBIT(val, 1, 7)
/*0x02288*/	u64	one_shot_vect0_en;
#define	VXGE_HW_ONE_SHOT_VECT0_EN_ONE_SHOT_VECT0_EN	vxge_mBIT(3)
/*0x02290*/	u64	one_shot_vect1_en;
#define	VXGE_HW_ONE_SHOT_VECT1_EN_ONE_SHOT_VECT1_EN	vxge_mBIT(3)
/*0x02298*/	u64	one_shot_vect2_en;
#define	VXGE_HW_ONE_SHOT_VECT2_EN_ONE_SHOT_VECT2_EN	vxge_mBIT(3)
/*0x022a0*/	u64	one_shot_vect3_en;
#define	VXGE_HW_ONE_SHOT_VECT3_EN_ONE_SHOT_VECT3_EN	vxge_mBIT(3)
	u8	unused022b0[0x022b0-0x022a8];

/*0x022b0*/	u64	pci_config_access_cfg1;
#define VXGE_HW_PCI_CONFIG_ACCESS_CFG1_ADDRESS(val) vxge_vBIT(val, 0, 12)
#define	VXGE_HW_PCI_CONFIG_ACCESS_CFG1_SEL_FUNC0	vxge_mBIT(15)
/*0x022b8*/	u64	pci_config_access_cfg2;
#define	VXGE_HW_PCI_CONFIG_ACCESS_CFG2_REQ	vxge_mBIT(0)
/*0x022c0*/	u64	pci_config_access_status;
#define	VXGE_HW_PCI_CONFIG_ACCESS_STATUS_ACCESS_ERR	vxge_mBIT(0)
#define VXGE_HW_PCI_CONFIG_ACCESS_STATUS_DATA(val) vxge_vBIT(val, 32, 32)
	u8	unused02300[0x02300-0x022c8];

/*0x02300*/	u64	vpath_debug_stats0;
#define VXGE_HW_VPATH_DEBUG_STATS0_INI_NUM_MWR_SENT(val) vxge_vBIT(val, 0, 32)
/*0x02308*/	u64	vpath_debug_stats1;
#define VXGE_HW_VPATH_DEBUG_STATS1_INI_NUM_MRD_SENT(val) vxge_vBIT(val, 0, 32)
/*0x02310*/	u64	vpath_debug_stats2;
#define VXGE_HW_VPATH_DEBUG_STATS2_INI_NUM_CPL_RCVD(val) vxge_vBIT(val, 0, 32)
/*0x02318*/	u64	vpath_debug_stats3;
#define VXGE_HW_VPATH_DEBUG_STATS3_INI_NUM_MWR_BYTE_SENT(val) \
							vxge_vBIT(val, 0, 64)
/*0x02320*/	u64	vpath_debug_stats4;
#define VXGE_HW_VPATH_DEBUG_STATS4_INI_NUM_CPL_BYTE_RCVD(val) \
							vxge_vBIT(val, 0, 64)
/*0x02328*/	u64	vpath_debug_stats5;
#define VXGE_HW_VPATH_DEBUG_STATS5_WRCRDTARB_XOFF(val) vxge_vBIT(val, 32, 32)
/*0x02330*/	u64	vpath_debug_stats6;
#define VXGE_HW_VPATH_DEBUG_STATS6_RDCRDTARB_XOFF(val) vxge_vBIT(val, 32, 32)
/*0x02338*/	u64	vpath_genstats_count01;
#define	VXGE_HW_VPATH_GENSTATS_COUNT01_PPIF_VPATH_GENSTATS_COUNT1(val) \
							vxge_vBIT(val, 0, 32)
#define	VXGE_HW_VPATH_GENSTATS_COUNT01_PPIF_VPATH_GENSTATS_COUNT0(val) \
							vxge_vBIT(val, 32, 32)
/*0x02340*/	u64	vpath_genstats_count23;
#define	VXGE_HW_VPATH_GENSTATS_COUNT23_PPIF_VPATH_GENSTATS_COUNT3(val) \
							vxge_vBIT(val, 0, 32)
#define	VXGE_HW_VPATH_GENSTATS_COUNT23_PPIF_VPATH_GENSTATS_COUNT2(val) \
							vxge_vBIT(val, 32, 32)
/*0x02348*/	u64	vpath_genstats_count4;
#define	VXGE_HW_VPATH_GENSTATS_COUNT4_PPIF_VPATH_GENSTATS_COUNT4(val) \
							vxge_vBIT(val, 32, 32)
/*0x02350*/	u64	vpath_genstats_count5;
#define	VXGE_HW_VPATH_GENSTATS_COUNT5_PPIF_VPATH_GENSTATS_COUNT5(val) \
							vxge_vBIT(val, 32, 32)
	u8	unused02648[0x02648-0x02358];
} __packed;

#define VXGE_HW_EEPROM_SIZE	(0x01 << 11)

/* Capability lists */
#define  VXGE_HW_PCI_EXP_LNKCAP_LNK_SPEED    0xf  /* Supported Link speeds */
#define  VXGE_HW_PCI_EXP_LNKCAP_LNK_WIDTH    0x3f0 /* Supported Link speeds. */
#define  VXGE_HW_PCI_EXP_LNKCAP_LW_RES       0x0  /* Reserved. */

#endif
