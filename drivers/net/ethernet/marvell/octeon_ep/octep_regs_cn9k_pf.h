/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef _OCTEP_REGS_CN9K_PF_H_
#define _OCTEP_REGS_CN9K_PF_H_

/* ############################ RST ######################### */
#define    CN93_RST_BOOT               0x000087E006001600ULL
#define    CN93_RST_CORE_DOMAIN_W1S    0x000087E006001820ULL
#define    CN93_RST_CORE_DOMAIN_W1C    0x000087E006001828ULL

#define     CN93_CONFIG_XPANSION_BAR             0x38
#define     CN93_CONFIG_PCIE_CAP                 0x70
#define     CN93_CONFIG_PCIE_DEVCAP              0x74
#define     CN93_CONFIG_PCIE_DEVCTL              0x78
#define     CN93_CONFIG_PCIE_LINKCAP             0x7C
#define     CN93_CONFIG_PCIE_LINKCTL             0x80
#define     CN93_CONFIG_PCIE_SLOTCAP             0x84
#define     CN93_CONFIG_PCIE_SLOTCTL             0x88

#define     CN93_PCIE_SRIOV_FDL                  0x188      /* 0x98 */
#define     CN93_PCIE_SRIOV_FDL_BIT_POS          0x10
#define     CN93_PCIE_SRIOV_FDL_MASK             0xFF

#define     CN93_CONFIG_PCIE_FLTMSK              0x720

/* ################# Offsets of RING, EPF, MAC ######################### */
#define    CN93_RING_OFFSET                      (0x1ULL << 17)
#define    CN93_EPF_OFFSET                       (0x1ULL << 25)
#define    CN93_MAC_OFFSET                       (0x1ULL << 4)
#define    CN93_BIT_ARRAY_OFFSET                 (0x1ULL << 4)
#define    CN93_EPVF_RING_OFFSET                 (0x1ULL << 4)

/* ################# Scratch Registers ######################### */
#define    CN93_SDP_EPF_SCRATCH                  0x205E0

/* ################# Window Registers ######################### */
#define    CN93_SDP_WIN_WR_ADDR64                0x20000
#define    CN93_SDP_WIN_RD_ADDR64                0x20010
#define    CN93_SDP_WIN_WR_DATA64                0x20020
#define    CN93_SDP_WIN_WR_MASK_REG              0x20030
#define    CN93_SDP_WIN_RD_DATA64                0x20040

#define    CN93_SDP_MAC_NUMBER                   0x2C100

/* ################# Global Previliged registers ######################### */
#define    CN93_SDP_EPF_RINFO                    0x205F0

#define    CN93_SDP_EPF_RINFO_SRN(val)           ((val) & 0xFF)
#define    CN93_SDP_EPF_RINFO_RPVF(val)          (((val) >> 32) & 0xF)
#define    CN93_SDP_EPF_RINFO_NVFS(val)          (((val) >> 48) & 0xFF)

/* SDP Function select */
#define    CN93_SDP_FUNC_SEL_EPF_BIT_POS         8
#define    CN93_SDP_FUNC_SEL_FUNC_BIT_POS        0

/* ##### RING IN (Into device from PCI: Tx Ring) REGISTERS #### */
#define    CN93_SDP_R_IN_CONTROL_START           0x10000
#define    CN93_SDP_R_IN_ENABLE_START            0x10010
#define    CN93_SDP_R_IN_INSTR_BADDR_START       0x10020
#define    CN93_SDP_R_IN_INSTR_RSIZE_START       0x10030
#define    CN93_SDP_R_IN_INSTR_DBELL_START       0x10040
#define    CN93_SDP_R_IN_CNTS_START              0x10050
#define    CN93_SDP_R_IN_INT_LEVELS_START        0x10060
#define    CN93_SDP_R_IN_PKT_CNT_START           0x10080
#define    CN93_SDP_R_IN_BYTE_CNT_START          0x10090

#define    CN93_SDP_R_IN_CONTROL(ring)		\
	(CN93_SDP_R_IN_CONTROL_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_IN_ENABLE(ring)		\
	(CN93_SDP_R_IN_ENABLE_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_IN_INSTR_BADDR(ring)	\
	(CN93_SDP_R_IN_INSTR_BADDR_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_IN_INSTR_RSIZE(ring)	\
	(CN93_SDP_R_IN_INSTR_RSIZE_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_IN_INSTR_DBELL(ring)	\
	(CN93_SDP_R_IN_INSTR_DBELL_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_IN_CNTS(ring)		\
	(CN93_SDP_R_IN_CNTS_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_IN_INT_LEVELS(ring)	\
	(CN93_SDP_R_IN_INT_LEVELS_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_IN_PKT_CNT(ring)		\
	(CN93_SDP_R_IN_PKT_CNT_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_IN_BYTE_CNT(ring)		\
	(CN93_SDP_R_IN_BYTE_CNT_START + ((ring) * CN93_RING_OFFSET))

/* Rings per Virtual Function */
#define    CN93_R_IN_CTL_RPVF_MASK	(0xF)
#define    CN93_R_IN_CTL_RPVF_POS	(48)

/* Number of instructions to be read in one MAC read request.
 * setting to Max value(4)
 */
#define    CN93_R_IN_CTL_IDLE                    (0x1ULL << 28)
#define    CN93_R_IN_CTL_RDSIZE                  (0x3ULL << 25)
#define    CN93_R_IN_CTL_IS_64B                  (0x1ULL << 24)
#define    CN93_R_IN_CTL_D_NSR                   (0x1ULL << 8)
#define    CN93_R_IN_CTL_D_ESR                   (0x1ULL << 6)
#define    CN93_R_IN_CTL_D_ROR                   (0x1ULL << 5)
#define    CN93_R_IN_CTL_NSR                     (0x1ULL << 3)
#define    CN93_R_IN_CTL_ESR                     (0x1ULL << 1)
#define    CN93_R_IN_CTL_ROR                     (0x1ULL << 0)

#define    CN93_R_IN_CTL_MASK  (CN93_R_IN_CTL_RDSIZE | CN93_R_IN_CTL_IS_64B)

/* ##### RING OUT (out from device to PCI host: Rx Ring) REGISTERS #### */
#define    CN93_SDP_R_OUT_CNTS_START              0x10100
#define    CN93_SDP_R_OUT_INT_LEVELS_START        0x10110
#define    CN93_SDP_R_OUT_SLIST_BADDR_START       0x10120
#define    CN93_SDP_R_OUT_SLIST_RSIZE_START       0x10130
#define    CN93_SDP_R_OUT_SLIST_DBELL_START       0x10140
#define    CN93_SDP_R_OUT_CONTROL_START           0x10150
#define    CN93_SDP_R_OUT_ENABLE_START            0x10160
#define    CN93_SDP_R_OUT_PKT_CNT_START           0x10180
#define    CN93_SDP_R_OUT_BYTE_CNT_START          0x10190

#define    CN93_SDP_R_OUT_CONTROL(ring)          \
	(CN93_SDP_R_OUT_CONTROL_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_ENABLE(ring)          \
	(CN93_SDP_R_OUT_ENABLE_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_SLIST_BADDR(ring)          \
	(CN93_SDP_R_OUT_SLIST_BADDR_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_SLIST_RSIZE(ring)          \
	(CN93_SDP_R_OUT_SLIST_RSIZE_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_SLIST_DBELL(ring)          \
	(CN93_SDP_R_OUT_SLIST_DBELL_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_CNTS(ring)          \
	(CN93_SDP_R_OUT_CNTS_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_INT_LEVELS(ring)          \
	(CN93_SDP_R_OUT_INT_LEVELS_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_PKT_CNT(ring)          \
	(CN93_SDP_R_OUT_PKT_CNT_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_BYTE_CNT(ring)          \
	(CN93_SDP_R_OUT_BYTE_CNT_START + ((ring) * CN93_RING_OFFSET))

/*------------------ R_OUT Masks ----------------*/
#define    CN93_R_OUT_INT_LEVELS_BMODE            BIT_ULL(63)
#define    CN93_R_OUT_INT_LEVELS_TIMET            (32)

#define    CN93_R_OUT_CTL_IDLE                    BIT_ULL(40)
#define    CN93_R_OUT_CTL_ES_I                    BIT_ULL(34)
#define    CN93_R_OUT_CTL_NSR_I                   BIT_ULL(33)
#define    CN93_R_OUT_CTL_ROR_I                   BIT_ULL(32)
#define    CN93_R_OUT_CTL_ES_D                    BIT_ULL(30)
#define    CN93_R_OUT_CTL_NSR_D                   BIT_ULL(29)
#define    CN93_R_OUT_CTL_ROR_D                   BIT_ULL(28)
#define    CN93_R_OUT_CTL_ES_P                    BIT_ULL(26)
#define    CN93_R_OUT_CTL_NSR_P                   BIT_ULL(25)
#define    CN93_R_OUT_CTL_ROR_P                   BIT_ULL(24)
#define    CN93_R_OUT_CTL_IMODE                   BIT_ULL(23)

/* ############### Interrupt Moderation Registers ############### */
#define CN93_SDP_R_IN_INT_MDRT_CTL0_START         0x10280
#define CN93_SDP_R_IN_INT_MDRT_CTL1_START         0x102A0
#define CN93_SDP_R_IN_INT_MDRT_DBG_START          0x102C0

#define CN93_SDP_R_OUT_INT_MDRT_CTL0_START        0x10380
#define CN93_SDP_R_OUT_INT_MDRT_CTL1_START        0x103A0
#define CN93_SDP_R_OUT_INT_MDRT_DBG_START         0x103C0

#define    CN93_SDP_R_IN_INT_MDRT_CTL0(ring)		\
	(CN93_SDP_R_IN_INT_MDRT_CTL0_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_IN_INT_MDRT_CTL1(ring)		\
	(CN93_SDP_R_IN_INT_MDRT_CTL1_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_IN_INT_MDRT_DBG(ring)		\
	(CN93_SDP_R_IN_INT_MDRT_DBG_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_INT_MDRT_CTL0(ring)		\
	(CN93_SDP_R_OUT_INT_MDRT_CTL0_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_INT_MDRT_CTL1(ring)		\
	(CN93_SDP_R_OUT_INT_MDRT_CTL1_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_INT_MDRT_DBG(ring)		\
	(CN93_SDP_R_OUT_INT_MDRT_DBG_START + ((ring) * CN93_RING_OFFSET))

/* ##################### Mail Box Registers ########################## */
/* INT register for VF. when a MBOX write from PF happed to a VF,
 * corresponding bit will be set in this register as well as in
 * PF_VF_INT register.
 *
 * This is a RO register, the int can be cleared by writing 1 to PF_VF_INT
 */
/* Basically first 3 are from PF to VF. The last one is data from VF to PF */
#define    CN93_SDP_R_MBOX_PF_VF_DATA_START       0x10210
#define    CN93_SDP_R_MBOX_PF_VF_INT_START        0x10220
#define    CN93_SDP_R_MBOX_VF_PF_DATA_START       0x10230

#define    CN93_SDP_R_MBOX_PF_VF_DATA(ring)		\
	(CN93_SDP_R_MBOX_PF_VF_DATA_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_MBOX_PF_VF_INT(ring)		\
	(CN93_SDP_R_MBOX_PF_VF_INT_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_MBOX_VF_PF_DATA(ring)		\
	(CN93_SDP_R_MBOX_VF_PF_DATA_START + ((ring) * CN93_RING_OFFSET))

/* ##################### Interrupt Registers ########################## */
#define	   CN93_SDP_R_ERR_TYPE_START	          0x10400

#define    CN93_SDP_R_ERR_TYPE(ring)		\
	(CN93_SDP_R_ERR_TYPE_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_MBOX_ISM_START              0x10500
#define    CN93_SDP_R_OUT_CNTS_ISM_START          0x10510
#define    CN93_SDP_R_IN_CNTS_ISM_START           0x10520

#define    CN93_SDP_R_MBOX_ISM(ring)		\
	(CN93_SDP_R_MBOX_ISM_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_OUT_CNTS_ISM(ring)	\
	(CN93_SDP_R_OUT_CNTS_ISM_START + ((ring) * CN93_RING_OFFSET))

#define    CN93_SDP_R_IN_CNTS_ISM(ring)		\
	(CN93_SDP_R_IN_CNTS_ISM_START + ((ring) * CN93_RING_OFFSET))

#define	   CN93_SDP_EPF_MBOX_RINT_START	          0x20100
#define	   CN93_SDP_EPF_MBOX_RINT_W1S_START	  0x20120
#define	   CN93_SDP_EPF_MBOX_RINT_ENA_W1C_START   0x20140
#define	   CN93_SDP_EPF_MBOX_RINT_ENA_W1S_START   0x20160

#define	   CN93_SDP_EPF_VFIRE_RINT_START          0x20180
#define	   CN93_SDP_EPF_VFIRE_RINT_W1S_START      0x201A0
#define	   CN93_SDP_EPF_VFIRE_RINT_ENA_W1C_START  0x201C0
#define	   CN93_SDP_EPF_VFIRE_RINT_ENA_W1S_START  0x201E0

#define	   CN93_SDP_EPF_IRERR_RINT                0x20200
#define	   CN93_SDP_EPF_IRERR_RINT_W1S            0x20210
#define	   CN93_SDP_EPF_IRERR_RINT_ENA_W1C        0x20220
#define	   CN93_SDP_EPF_IRERR_RINT_ENA_W1S        0x20230

#define	   CN93_SDP_EPF_VFORE_RINT_START          0x20240
#define	   CN93_SDP_EPF_VFORE_RINT_W1S_START      0x20260
#define	   CN93_SDP_EPF_VFORE_RINT_ENA_W1C_START  0x20280
#define	   CN93_SDP_EPF_VFORE_RINT_ENA_W1S_START  0x202A0

#define	   CN93_SDP_EPF_ORERR_RINT                0x20320
#define	   CN93_SDP_EPF_ORERR_RINT_W1S            0x20330
#define	   CN93_SDP_EPF_ORERR_RINT_ENA_W1C        0x20340
#define	   CN93_SDP_EPF_ORERR_RINT_ENA_W1S        0x20350

#define	   CN93_SDP_EPF_OEI_RINT                  0x20360
#define	   CN93_SDP_EPF_OEI_RINT_W1S              0x20370
#define	   CN93_SDP_EPF_OEI_RINT_ENA_W1C          0x20380
#define	   CN93_SDP_EPF_OEI_RINT_ENA_W1S          0x20390

#define	   CN93_SDP_EPF_DMA_RINT                  0x20400
#define	   CN93_SDP_EPF_DMA_RINT_W1S              0x20410
#define	   CN93_SDP_EPF_DMA_RINT_ENA_W1C          0x20420
#define	   CN93_SDP_EPF_DMA_RINT_ENA_W1S          0x20430

#define	   CN93_SDP_EPF_DMA_INT_LEVEL_START	    0x20440
#define	   CN93_SDP_EPF_DMA_CNT_START	            0x20460
#define	   CN93_SDP_EPF_DMA_TIM_START	            0x20480

#define	   CN93_SDP_EPF_MISC_RINT                 0x204A0
#define	   CN93_SDP_EPF_MISC_RINT_W1S	            0x204B0
#define	   CN93_SDP_EPF_MISC_RINT_ENA_W1C         0x204C0
#define	   CN93_SDP_EPF_MISC_RINT_ENA_W1S         0x204D0

#define	   CN93_SDP_EPF_DMA_VF_RINT_START           0x204E0
#define	   CN93_SDP_EPF_DMA_VF_RINT_W1S_START       0x20500
#define	   CN93_SDP_EPF_DMA_VF_RINT_ENA_W1C_START   0x20520
#define	   CN93_SDP_EPF_DMA_VF_RINT_ENA_W1S_START   0x20540

#define	   CN93_SDP_EPF_PP_VF_RINT_START            0x20560
#define	   CN93_SDP_EPF_PP_VF_RINT_W1S_START        0x20580
#define	   CN93_SDP_EPF_PP_VF_RINT_ENA_W1C_START    0x205A0
#define	   CN93_SDP_EPF_PP_VF_RINT_ENA_W1S_START    0x205C0

#define	   CN93_SDP_EPF_MBOX_RINT(index)		\
		(CN93_SDP_EPF_MBOX_RINT_START + ((index) * CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_MBOX_RINT_W1S(index)		\
		(CN93_SDP_EPF_MBOX_RINT_W1S_START + ((index) * CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_MBOX_RINT_ENA_W1C(index)	\
		(CN93_SDP_EPF_MBOX_RINT_ENA_W1C_START + ((index) * CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_MBOX_RINT_ENA_W1S(index)	\
		(CN93_SDP_EPF_MBOX_RINT_ENA_W1S_START + ((index) * CN93_BIT_ARRAY_OFFSET))

#define	   CN93_SDP_EPF_VFIRE_RINT(index)		\
		(CN93_SDP_EPF_VFIRE_RINT_START + ((index) * CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_VFIRE_RINT_W1S(index)		\
		(CN93_SDP_EPF_VFIRE_RINT_W1S_START + ((index) * CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_VFIRE_RINT_ENA_W1C(index)	\
		(CN93_SDP_EPF_VFIRE_RINT_ENA_W1C_START + ((index) * CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_VFIRE_RINT_ENA_W1S(index)	\
		(CN93_SDP_EPF_VFIRE_RINT_ENA_W1S_START + ((index) * CN93_BIT_ARRAY_OFFSET))

#define	   CN93_SDP_EPF_VFORE_RINT(index)		\
		(CN93_SDP_EPF_VFORE_RINT_START + ((index) * CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_VFORE_RINT_W1S(index)		\
		(CN93_SDP_EPF_VFORE_RINT_W1S_START + ((index) * CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_VFORE_RINT_ENA_W1C(index)	\
		(CN93_SDP_EPF_VFORE_RINT_ENA_W1C_START + ((index) * CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_VFORE_RINT_ENA_W1S(index)	\
		(CN93_SDP_EPF_VFORE_RINT_ENA_W1S_START + ((index) * CN93_BIT_ARRAY_OFFSET))

#define	   CN93_SDP_EPF_DMA_VF_RINT(index)		\
		(CN93_SDP_EPF_DMA_VF_RINT_START + ((index) + CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_DMA_VF_RINT_W1S(index)		\
		(CN93_SDP_EPF_DMA_VF_RINT_W1S_START + ((index) + CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_DMA_VF_RINT_ENA_W1C(index)	\
		(CN93_SDP_EPF_DMA_VF_RINT_ENA_W1C_START + ((index) + CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_DMA_VF_RINT_ENA_W1S(index)	\
		(CN93_SDP_EPF_DMA_VF_RINT_ENA_W1S_START + ((index) + CN93_BIT_ARRAY_OFFSET))

#define	   CN93_SDP_EPF_PP_VF_RINT(index)		\
		(CN93_SDP_EPF_PP_VF_RINT_START + ((index) + CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_PP_VF_RINT_W1S(index)		\
		(CN93_SDP_EPF_PP_VF_RINT_W1S_START + ((index) + CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_PP_VF_RINT_ENA_W1C(index)	\
		(CN93_SDP_EPF_PP_VF_RINT_ENA_W1C_START + ((index) + CN93_BIT_ARRAY_OFFSET))
#define	   CN93_SDP_EPF_PP_VF_RINT_ENA_W1S(index)	\
		(CN93_SDP_EPF_PP_VF_RINT_ENA_W1S_START + ((index) + CN93_BIT_ARRAY_OFFSET))

/*------------------ Interrupt Masks ----------------*/
#define	   CN93_INTR_R_SEND_ISM       BIT_ULL(63)
#define	   CN93_INTR_R_OUT_INT        BIT_ULL(62)
#define    CN93_INTR_R_IN_INT         BIT_ULL(61)
#define    CN93_INTR_R_MBOX_INT       BIT_ULL(60)
#define    CN93_INTR_R_RESEND         BIT_ULL(59)
#define    CN93_INTR_R_CLR_TIM        BIT_ULL(58)

/* ####################### Ring Mapping Registers ################################## */
#define    CN93_SDP_EPVF_RING_START          0x26000
#define    CN93_SDP_IN_RING_TB_MAP_START     0x28000
#define    CN93_SDP_IN_RATE_LIMIT_START      0x2A000
#define    CN93_SDP_MAC_PF_RING_CTL_START    0x2C000

#define	   CN93_SDP_EPVF_RING(ring)		\
		(CN93_SDP_EPVF_RING_START + ((ring) * CN93_EPVF_RING_OFFSET))
#define	   CN93_SDP_IN_RING_TB_MAP(ring)	\
		(CN93_SDP_N_RING_TB_MAP_START + ((ring) * CN93_EPVF_RING_OFFSET))
#define	   CN93_SDP_IN_RATE_LIMIT(ring)		\
		(CN93_SDP_IN_RATE_LIMIT_START + ((ring) * CN93_EPVF_RING_OFFSET))
#define	   CN93_SDP_MAC_PF_RING_CTL(mac)	\
		(CN93_SDP_MAC_PF_RING_CTL_START + ((mac) * CN93_MAC_OFFSET))

#define    CN93_SDP_MAC_PF_RING_CTL_NPFS(val)  ((val) & 0xF)
#define    CN93_SDP_MAC_PF_RING_CTL_SRN(val)   (((val) >> 8) & 0xFF)
#define    CN93_SDP_MAC_PF_RING_CTL_RPPF(val)  (((val) >> 16) & 0x3F)

/* Number of non-queue interrupts in CN93xx */
#define    CN93_NUM_NON_IOQ_INTR    16

/* bit 0 for control mbox interrupt */
#define CN93_SDP_EPF_OEI_RINT_DATA_BIT_MBOX	BIT_ULL(0)
/* bit 1 for firmware heartbeat interrupt */
#define CN93_SDP_EPF_OEI_RINT_DATA_BIT_HBEAT	BIT_ULL(1)

#define CN93_PEM_BAR4_INDEX            7
#define CN93_PEM_BAR4_INDEX_SIZE       0x400000ULL
#define CN93_PEM_BAR4_INDEX_OFFSET     (CN93_PEM_BAR4_INDEX * CN93_PEM_BAR4_INDEX_SIZE)

#endif /* _OCTEP_REGS_CN9K_PF_H_ */
