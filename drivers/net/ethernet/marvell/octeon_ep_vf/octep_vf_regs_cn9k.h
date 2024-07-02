/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) VF Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */
#ifndef _OCTEP_VF_REGS_CN9K_H_
#define _OCTEP_VF_REGS_CN9K_H_

/*############################ RST #########################*/
#define     CN93_VF_CONFIG_XPANSION_BAR         0x38
#define     CN93_VF_CONFIG_PCIE_CAP             0x70
#define     CN93_VF_CONFIG_PCIE_DEVCAP          0x74
#define     CN93_VF_CONFIG_PCIE_DEVCTL          0x78
#define     CN93_VF_CONFIG_PCIE_LINKCAP         0x7C
#define     CN93_VF_CONFIG_PCIE_LINKCTL         0x80
#define     CN93_VF_CONFIG_PCIE_SLOTCAP         0x84
#define     CN93_VF_CONFIG_PCIE_SLOTCTL         0x88

#define     CN93_VF_RING_OFFSET                    BIT_ULL(17)

/*###################### RING IN REGISTERS #########################*/
#define    CN93_VF_SDP_R_IN_CONTROL_START          0x10000
#define    CN93_VF_SDP_R_IN_ENABLE_START           0x10010
#define    CN93_VF_SDP_R_IN_INSTR_BADDR_START      0x10020
#define    CN93_VF_SDP_R_IN_INSTR_RSIZE_START      0x10030
#define    CN93_VF_SDP_R_IN_INSTR_DBELL_START      0x10040
#define    CN93_VF_SDP_R_IN_CNTS_START             0x10050
#define    CN93_VF_SDP_R_IN_INT_LEVELS_START       0x10060
#define    CN93_VF_SDP_R_IN_PKT_CNT_START          0x10080
#define    CN93_VF_SDP_R_IN_BYTE_CNT_START         0x10090

#define    CN93_VF_SDP_R_IN_CONTROL(ring)          \
	(CN93_VF_SDP_R_IN_CONTROL_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_IN_ENABLE(ring)          \
	(CN93_VF_SDP_R_IN_ENABLE_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_IN_INSTR_BADDR(ring)          \
	(CN93_VF_SDP_R_IN_INSTR_BADDR_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_IN_INSTR_RSIZE(ring)          \
	(CN93_VF_SDP_R_IN_INSTR_RSIZE_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_IN_INSTR_DBELL(ring)          \
	(CN93_VF_SDP_R_IN_INSTR_DBELL_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_IN_CNTS(ring)          \
	(CN93_VF_SDP_R_IN_CNTS_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_IN_INT_LEVELS(ring)          \
	(CN93_VF_SDP_R_IN_INT_LEVELS_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_IN_PKT_CNT(ring)          \
	(CN93_VF_SDP_R_IN_PKT_CNT_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_IN_BYTE_CNT(ring)          \
	(CN93_VF_SDP_R_IN_BYTE_CNT_START + ((ring) * CN93_VF_RING_OFFSET))

/*------------------ R_IN Masks ----------------*/

/** Rings per Virtual Function **/
#define    CN93_VF_R_IN_CTL_RPVF_MASK    (0xF)
#define	   CN93_VF_R_IN_CTL_RPVF_POS     (48)

/* Number of instructions to be read in one MAC read request.
 * setting to Max value(4)
 **/
#define    CN93_VF_R_IN_CTL_IDLE                  BIT_ULL(28)
#define    CN93_VF_R_IN_CTL_RDSIZE                (0x3ULL << 25)
#define    CN93_VF_R_IN_CTL_IS_64B                BIT_ULL(24)
#define    CN93_VF_R_IN_CTL_D_NSR                 BIT_ULL(8)
#define    CN93_VF_R_IN_CTL_D_ESR                 BIT_ULL(6)
#define    CN93_VF_R_IN_CTL_D_ROR                 BIT_ULL(5)
#define    CN93_VF_R_IN_CTL_NSR                   BIT_ULL(3)
#define    CN93_VF_R_IN_CTL_ESR                   BIT_ULL(1)
#define    CN93_VF_R_IN_CTL_ROR                   BIT_ULL(0)

#define    CN93_VF_R_IN_CTL_MASK     (CN93_VF_R_IN_CTL_RDSIZE | CN93_VF_R_IN_CTL_IS_64B)

/*###################### RING OUT REGISTERS #########################*/
#define    CN93_VF_SDP_R_OUT_CNTS_START            0x10100
#define    CN93_VF_SDP_R_OUT_INT_LEVELS_START      0x10110
#define    CN93_VF_SDP_R_OUT_SLIST_BADDR_START     0x10120
#define    CN93_VF_SDP_R_OUT_SLIST_RSIZE_START     0x10130
#define    CN93_VF_SDP_R_OUT_SLIST_DBELL_START     0x10140
#define    CN93_VF_SDP_R_OUT_CONTROL_START         0x10150
#define    CN93_VF_SDP_R_OUT_ENABLE_START          0x10160
#define    CN93_VF_SDP_R_OUT_PKT_CNT_START         0x10180
#define    CN93_VF_SDP_R_OUT_BYTE_CNT_START        0x10190

#define    CN93_VF_SDP_R_OUT_CONTROL(ring)          \
	(CN93_VF_SDP_R_OUT_CONTROL_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_OUT_ENABLE(ring)          \
	(CN93_VF_SDP_R_OUT_ENABLE_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_OUT_SLIST_BADDR(ring)          \
	(CN93_VF_SDP_R_OUT_SLIST_BADDR_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_OUT_SLIST_RSIZE(ring)          \
	(CN93_VF_SDP_R_OUT_SLIST_RSIZE_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_OUT_SLIST_DBELL(ring)          \
	(CN93_VF_SDP_R_OUT_SLIST_DBELL_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_OUT_CNTS(ring)          \
	(CN93_VF_SDP_R_OUT_CNTS_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_OUT_INT_LEVELS(ring)          \
	(CN93_VF_SDP_R_OUT_INT_LEVELS_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_OUT_PKT_CNT(ring)          \
	(CN93_VF_SDP_R_OUT_PKT_CNT_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_OUT_BYTE_CNT(ring)          \
	(CN93_VF_SDP_R_OUT_BYTE_CNT_START + ((ring) * CN93_VF_RING_OFFSET))

/*------------------ R_OUT Masks ----------------*/
#define    CN93_VF_R_OUT_INT_LEVELS_BMODE            BIT_ULL(63)
#define    CN93_VF_R_OUT_INT_LEVELS_TIMET            (32)

#define    CN93_VF_R_OUT_CTL_IDLE                    BIT_ULL(40)
#define    CN93_VF_R_OUT_CTL_ES_I                    BIT_ULL(34)
#define    CN93_VF_R_OUT_CTL_NSR_I                   BIT_ULL(33)
#define    CN93_VF_R_OUT_CTL_ROR_I                   BIT_ULL(32)
#define    CN93_VF_R_OUT_CTL_ES_D                    BIT_ULL(30)
#define    CN93_VF_R_OUT_CTL_NSR_D                   BIT_ULL(29)
#define    CN93_VF_R_OUT_CTL_ROR_D                   BIT_ULL(28)
#define    CN93_VF_R_OUT_CTL_ES_P                    BIT_ULL(26)
#define    CN93_VF_R_OUT_CTL_NSR_P                   BIT_ULL(25)
#define    CN93_VF_R_OUT_CTL_ROR_P                   BIT_ULL(24)
#define    CN93_VF_R_OUT_CTL_IMODE                   BIT_ULL(23)

/* ##################### Mail Box Registers ########################## */
/* SDP PF to VF Mailbox Data Register */
#define    CN93_VF_SDP_R_MBOX_PF_VF_DATA_START    0x10210
/* SDP Packet PF to VF Mailbox Interrupt Register */
#define    CN93_VF_SDP_R_MBOX_PF_VF_INT_START     0x10220
/* SDP VF to PF Mailbox Data Register */
#define    CN93_VF_SDP_R_MBOX_VF_PF_DATA_START    0x10230

#define    CN93_VF_SDP_R_MBOX_PF_VF_INT_ENAB         BIT_ULL(1)
#define    CN93_VF_SDP_R_MBOX_PF_VF_INT_STATUS       BIT_ULL(0)

#define    CN93_VF_SDP_R_MBOX_PF_VF_DATA(ring)          \
	(CN93_VF_SDP_R_MBOX_PF_VF_DATA_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_MBOX_PF_VF_INT(ring)          \
	(CN93_VF_SDP_R_MBOX_PF_VF_INT_START + ((ring) * CN93_VF_RING_OFFSET))

#define    CN93_VF_SDP_R_MBOX_VF_PF_DATA(ring)          \
	(CN93_VF_SDP_R_MBOX_VF_PF_DATA_START + ((ring) * CN93_VF_RING_OFFSET))
#endif /* _OCTEP_VF_REGS_CN9K_H_ */
