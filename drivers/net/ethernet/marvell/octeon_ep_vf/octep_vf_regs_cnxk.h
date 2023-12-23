/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) VF Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */
#ifndef _OCTEP_VF_REGS_CNXK_H_
#define _OCTEP_VF_REGS_CNXK_H_

/*############################ RST #########################*/
#define     CNXK_VF_CONFIG_XPANSION_BAR         0x38
#define     CNXK_VF_CONFIG_PCIE_CAP             0x70
#define     CNXK_VF_CONFIG_PCIE_DEVCAP          0x74
#define     CNXK_VF_CONFIG_PCIE_DEVCTL          0x78
#define     CNXK_VF_CONFIG_PCIE_LINKCAP         0x7C
#define     CNXK_VF_CONFIG_PCIE_LINKCTL         0x80
#define     CNXK_VF_CONFIG_PCIE_SLOTCAP         0x84
#define     CNXK_VF_CONFIG_PCIE_SLOTCTL         0x88

#define     CNXK_VF_RING_OFFSET                    (0x1ULL << 17)

/*###################### RING IN REGISTERS #########################*/
#define    CNXK_VF_SDP_R_IN_CONTROL_START          0x10000
#define    CNXK_VF_SDP_R_IN_ENABLE_START           0x10010
#define    CNXK_VF_SDP_R_IN_INSTR_BADDR_START      0x10020
#define    CNXK_VF_SDP_R_IN_INSTR_RSIZE_START      0x10030
#define    CNXK_VF_SDP_R_IN_INSTR_DBELL_START      0x10040
#define    CNXK_VF_SDP_R_IN_CNTS_START             0x10050
#define    CNXK_VF_SDP_R_IN_INT_LEVELS_START       0x10060
#define    CNXK_VF_SDP_R_IN_PKT_CNT_START          0x10080
#define    CNXK_VF_SDP_R_IN_BYTE_CNT_START         0x10090
#define    CNXK_VF_SDP_R_ERR_TYPE_START            0x10400

#define CNXK_VF_SDP_R_ERR_TYPE(ring)                 \
	(CNXK_VF_SDP_R_ERR_TYPE_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_IN_CONTROL(ring)          \
	(CNXK_VF_SDP_R_IN_CONTROL_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_IN_ENABLE(ring)          \
	(CNXK_VF_SDP_R_IN_ENABLE_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_IN_INSTR_BADDR(ring)          \
	(CNXK_VF_SDP_R_IN_INSTR_BADDR_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_IN_INSTR_RSIZE(ring)          \
	(CNXK_VF_SDP_R_IN_INSTR_RSIZE_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_IN_INSTR_DBELL(ring)          \
	(CNXK_VF_SDP_R_IN_INSTR_DBELL_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_IN_CNTS(ring)          \
	(CNXK_VF_SDP_R_IN_CNTS_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_IN_INT_LEVELS(ring)          \
	(CNXK_VF_SDP_R_IN_INT_LEVELS_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_IN_PKT_CNT(ring)          \
	(CNXK_VF_SDP_R_IN_PKT_CNT_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_IN_BYTE_CNT(ring)          \
	(CNXK_VF_SDP_R_IN_BYTE_CNT_START + ((ring) * CNXK_VF_RING_OFFSET))

/*------------------ R_IN Masks ----------------*/

/** Rings per Virtual Function **/
#define    CNXK_VF_R_IN_CTL_RPVF_MASK    (0xF)
#define	   CNXK_VF_R_IN_CTL_RPVF_POS     (48)

/* Number of instructions to be read in one MAC read request.
 * setting to Max value(4)
 **/
#define    CNXK_VF_R_IN_CTL_IDLE                  (0x1ULL << 28)
#define    CNXK_VF_R_IN_CTL_RDSIZE                (0x3ULL << 25)
#define    CNXK_VF_R_IN_CTL_IS_64B                (0x1ULL << 24)
#define    CNXK_VF_R_IN_CTL_D_NSR                 (0x1ULL << 8)
#define    CNXK_VF_R_IN_CTL_D_ESR                 (0x1ULL << 6)
#define    CNXK_VF_R_IN_CTL_D_ROR                 (0x1ULL << 5)
#define    CNXK_VF_R_IN_CTL_NSR                   (0x1ULL << 3)
#define    CNXK_VF_R_IN_CTL_ESR                   (0x1ULL << 1)
#define    CNXK_VF_R_IN_CTL_ROR                   (0x1ULL << 0)

#define    CNXK_VF_R_IN_CTL_MASK     (CNXK_VF_R_IN_CTL_RDSIZE | CNXK_VF_R_IN_CTL_IS_64B)

/*###################### RING OUT REGISTERS #########################*/
#define    CNXK_VF_SDP_R_OUT_CNTS_START            0x10100
#define    CNXK_VF_SDP_R_OUT_INT_LEVELS_START      0x10110
#define    CNXK_VF_SDP_R_OUT_SLIST_BADDR_START     0x10120
#define    CNXK_VF_SDP_R_OUT_SLIST_RSIZE_START     0x10130
#define    CNXK_VF_SDP_R_OUT_SLIST_DBELL_START     0x10140
#define    CNXK_VF_SDP_R_OUT_CONTROL_START         0x10150
#define    CNXK_VF_SDP_R_OUT_WMARK_START           0x10160
#define    CNXK_VF_SDP_R_OUT_ENABLE_START          0x10170
#define    CNXK_VF_SDP_R_OUT_PKT_CNT_START         0x10180
#define    CNXK_VF_SDP_R_OUT_BYTE_CNT_START        0x10190

#define    CNXK_VF_SDP_R_OUT_CONTROL(ring)          \
	(CNXK_VF_SDP_R_OUT_CONTROL_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_OUT_ENABLE(ring)          \
	(CNXK_VF_SDP_R_OUT_ENABLE_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_OUT_SLIST_BADDR(ring)          \
	(CNXK_VF_SDP_R_OUT_SLIST_BADDR_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_OUT_SLIST_RSIZE(ring)          \
	(CNXK_VF_SDP_R_OUT_SLIST_RSIZE_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_OUT_SLIST_DBELL(ring)          \
	(CNXK_VF_SDP_R_OUT_SLIST_DBELL_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_OUT_WMARK(ring)          \
	(CNXK_VF_SDP_R_OUT_WMARK_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_OUT_CNTS(ring)          \
	(CNXK_VF_SDP_R_OUT_CNTS_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_OUT_INT_LEVELS(ring)          \
	(CNXK_VF_SDP_R_OUT_INT_LEVELS_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_OUT_PKT_CNT(ring)          \
	(CNXK_VF_SDP_R_OUT_PKT_CNT_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_OUT_BYTE_CNT(ring)          \
	(CNXK_VF_SDP_R_OUT_BYTE_CNT_START + ((ring) * CNXK_VF_RING_OFFSET))

/*------------------ R_OUT Masks ----------------*/
#define    CNXK_VF_R_OUT_INT_LEVELS_BMODE            BIT_ULL(63)
#define    CNXK_VF_R_OUT_INT_LEVELS_TIMET            (32)

#define    CNXK_VF_R_OUT_CTL_IDLE                    BIT_ULL(40)
#define    CNXK_VF_R_OUT_CTL_ES_I                    BIT_ULL(34)
#define    CNXK_VF_R_OUT_CTL_NSR_I                   BIT_ULL(33)
#define    CNXK_VF_R_OUT_CTL_ROR_I                   BIT_ULL(32)
#define    CNXK_VF_R_OUT_CTL_ES_D                    BIT_ULL(30)
#define    CNXK_VF_R_OUT_CTL_NSR_D                   BIT_ULL(29)
#define    CNXK_VF_R_OUT_CTL_ROR_D                   BIT_ULL(28)
#define    CNXK_VF_R_OUT_CTL_ES_P                    BIT_ULL(26)
#define    CNXK_VF_R_OUT_CTL_NSR_P                   BIT_ULL(25)
#define    CNXK_VF_R_OUT_CTL_ROR_P                   BIT_ULL(24)
#define    CNXK_VF_R_OUT_CTL_IMODE                   BIT_ULL(23)

/* ##################### Mail Box Registers ########################## */
/* SDP PF to VF Mailbox Data Register */
#define    CNXK_VF_SDP_R_MBOX_PF_VF_DATA_START    0x10210
/* SDP Packet PF to VF Mailbox Interrupt Register */
#define    CNXK_VF_SDP_R_MBOX_PF_VF_INT_START     0x10220
/* SDP VF to PF Mailbox Data Register */
#define    CNXK_VF_SDP_R_MBOX_VF_PF_DATA_START    0x10230

#define    CNXK_VF_SDP_R_MBOX_PF_VF_INT_ENAB         BIT_ULL(1)
#define    CNXK_VF_SDP_R_MBOX_PF_VF_INT_STATUS       BIT_ULL(0)

#define    CNXK_VF_SDP_R_MBOX_PF_VF_DATA(ring)          \
	(CNXK_VF_SDP_R_MBOX_PF_VF_DATA_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_MBOX_PF_VF_INT(ring)          \
	(CNXK_VF_SDP_R_MBOX_PF_VF_INT_START + ((ring) * CNXK_VF_RING_OFFSET))

#define    CNXK_VF_SDP_R_MBOX_VF_PF_DATA(ring)          \
	(CNXK_VF_SDP_R_MBOX_VF_PF_DATA_START + ((ring) * CNXK_VF_RING_OFFSET))
#endif /* _OCTEP_VF_REGS_CNXK_H_ */
