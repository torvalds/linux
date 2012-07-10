/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef REG_H
#define REG_H

#include "../reg.h"

#define AR_CR                0x0008
#define AR_CR_RXE            (AR_SREV_9300_20_OR_LATER(ah) ? 0x0000000c : 0x00000004)
#define AR_CR_RXD            0x00000020
#define AR_CR_SWI            0x00000040

#define AR_RXDP              0x000C

#define AR_CFG               0x0014
#define AR_CFG_SWTD          0x00000001
#define AR_CFG_SWTB          0x00000002
#define AR_CFG_SWRD          0x00000004
#define AR_CFG_SWRB          0x00000008
#define AR_CFG_SWRG          0x00000010
#define AR_CFG_AP_ADHOC_INDICATION 0x00000020
#define AR_CFG_PHOK          0x00000100
#define AR_CFG_CLK_GATE_DIS  0x00000400
#define AR_CFG_EEBS          0x00000200
#define AR_CFG_PCI_MASTER_REQ_Q_THRESH         0x00060000
#define AR_CFG_PCI_MASTER_REQ_Q_THRESH_S       17

#define AR_RXBP_THRESH       0x0018
#define AR_RXBP_THRESH_HP    0x0000000f
#define AR_RXBP_THRESH_HP_S  0
#define AR_RXBP_THRESH_LP    0x00003f00
#define AR_RXBP_THRESH_LP_S  8

#define AR_MIRT              0x0020
#define AR_MIRT_VAL          0x0000ffff
#define AR_MIRT_VAL_S        16

#define AR_IER               0x0024
#define AR_IER_ENABLE        0x00000001
#define AR_IER_DISABLE       0x00000000

#define AR_TIMT              0x0028
#define AR_TIMT_LAST         0x0000ffff
#define AR_TIMT_LAST_S       0
#define AR_TIMT_FIRST        0xffff0000
#define AR_TIMT_FIRST_S      16

#define AR_RIMT              0x002C
#define AR_RIMT_LAST         0x0000ffff
#define AR_RIMT_LAST_S       0
#define AR_RIMT_FIRST        0xffff0000
#define AR_RIMT_FIRST_S      16

#define AR_DMASIZE_4B        0x00000000
#define AR_DMASIZE_8B        0x00000001
#define AR_DMASIZE_16B       0x00000002
#define AR_DMASIZE_32B       0x00000003
#define AR_DMASIZE_64B       0x00000004
#define AR_DMASIZE_128B      0x00000005
#define AR_DMASIZE_256B      0x00000006
#define AR_DMASIZE_512B      0x00000007

#define AR_TXCFG             0x0030
#define AR_TXCFG_DMASZ_MASK  0x00000007
#define AR_TXCFG_DMASZ_4B    0
#define AR_TXCFG_DMASZ_8B    1
#define AR_TXCFG_DMASZ_16B   2
#define AR_TXCFG_DMASZ_32B   3
#define AR_TXCFG_DMASZ_64B   4
#define AR_TXCFG_DMASZ_128B  5
#define AR_TXCFG_DMASZ_256B  6
#define AR_TXCFG_DMASZ_512B  7
#define AR_FTRIG             0x000003F0
#define AR_FTRIG_S           4
#define AR_FTRIG_IMMED       0x00000000
#define AR_FTRIG_64B         0x00000010
#define AR_FTRIG_128B        0x00000020
#define AR_FTRIG_192B        0x00000030
#define AR_FTRIG_256B        0x00000040
#define AR_FTRIG_512B        0x00000080
#define AR_TXCFG_ADHOC_BEACON_ATIM_TX_POLICY 0x00000800

#define AR_RXCFG             0x0034
#define AR_RXCFG_CHIRP       0x00000008
#define AR_RXCFG_ZLFDMA      0x00000010
#define AR_RXCFG_DMASZ_MASK  0x00000007
#define AR_RXCFG_DMASZ_4B    0
#define AR_RXCFG_DMASZ_8B    1
#define AR_RXCFG_DMASZ_16B   2
#define AR_RXCFG_DMASZ_32B   3
#define AR_RXCFG_DMASZ_64B   4
#define AR_RXCFG_DMASZ_128B  5
#define AR_RXCFG_DMASZ_256B  6
#define AR_RXCFG_DMASZ_512B  7

#define AR_TOPS              0x0044
#define AR_TOPS_MASK         0x0000FFFF

#define AR_RXNPTO            0x0048
#define AR_RXNPTO_MASK       0x000003FF

#define AR_TXNPTO            0x004C
#define AR_TXNPTO_MASK       0x000003FF
#define AR_TXNPTO_QCU_MASK   0x000FFC00

#define AR_RPGTO             0x0050
#define AR_RPGTO_MASK        0x000003FF

#define AR_RPCNT             0x0054
#define AR_RPCNT_MASK        0x0000001F

#define AR_MACMISC           0x0058
#define AR_MACMISC_PCI_EXT_FORCE        0x00000010
#define AR_MACMISC_DMA_OBS              0x000001E0
#define AR_MACMISC_DMA_OBS_S            5
#define AR_MACMISC_DMA_OBS_LINE_0       0
#define AR_MACMISC_DMA_OBS_LINE_1       1
#define AR_MACMISC_DMA_OBS_LINE_2       2
#define AR_MACMISC_DMA_OBS_LINE_3       3
#define AR_MACMISC_DMA_OBS_LINE_4       4
#define AR_MACMISC_DMA_OBS_LINE_5       5
#define AR_MACMISC_DMA_OBS_LINE_6       6
#define AR_MACMISC_DMA_OBS_LINE_7       7
#define AR_MACMISC_DMA_OBS_LINE_8       8
#define AR_MACMISC_MISC_OBS             0x00000E00
#define AR_MACMISC_MISC_OBS_S           9
#define AR_MACMISC_MISC_OBS_BUS_LSB     0x00007000
#define AR_MACMISC_MISC_OBS_BUS_LSB_S   12
#define AR_MACMISC_MISC_OBS_BUS_MSB     0x00038000
#define AR_MACMISC_MISC_OBS_BUS_MSB_S   15
#define AR_MACMISC_MISC_OBS_BUS_1       1

#define AR_DATABUF_SIZE		0x0060
#define AR_DATABUF_SIZE_MASK	0x00000FFF

#define AR_GTXTO    0x0064
#define AR_GTXTO_TIMEOUT_COUNTER    0x0000FFFF
#define AR_GTXTO_TIMEOUT_LIMIT      0xFFFF0000
#define AR_GTXTO_TIMEOUT_LIMIT_S    16

#define AR_GTTM     0x0068
#define AR_GTTM_USEC          0x00000001
#define AR_GTTM_IGNORE_IDLE   0x00000002
#define AR_GTTM_RESET_IDLE    0x00000004
#define AR_GTTM_CST_USEC      0x00000008

#define AR_CST         0x006C
#define AR_CST_TIMEOUT_COUNTER    0x0000FFFF
#define AR_CST_TIMEOUT_LIMIT      0xFFFF0000
#define AR_CST_TIMEOUT_LIMIT_S    16

#define AR_HP_RXDP 0x0074
#define AR_LP_RXDP 0x0078

#define AR_ISR               0x0080
#define AR_ISR_RXOK          0x00000001
#define AR_ISR_RXDESC        0x00000002
#define AR_ISR_HP_RXOK	     0x00000001
#define AR_ISR_LP_RXOK	     0x00000002
#define AR_ISR_RXERR         0x00000004
#define AR_ISR_RXNOPKT       0x00000008
#define AR_ISR_RXEOL         0x00000010
#define AR_ISR_RXORN         0x00000020
#define AR_ISR_TXOK          0x00000040
#define AR_ISR_TXDESC        0x00000080
#define AR_ISR_TXERR         0x00000100
#define AR_ISR_TXNOPKT       0x00000200
#define AR_ISR_TXEOL         0x00000400
#define AR_ISR_TXURN         0x00000800
#define AR_ISR_MIB           0x00001000
#define AR_ISR_SWI           0x00002000
#define AR_ISR_RXPHY         0x00004000
#define AR_ISR_RXKCM         0x00008000
#define AR_ISR_SWBA          0x00010000
#define AR_ISR_BRSSI         0x00020000
#define AR_ISR_BMISS         0x00040000
#define AR_ISR_BNR           0x00100000
#define AR_ISR_RXCHIRP       0x00200000
#define AR_ISR_BCNMISC       0x00800000
#define AR_ISR_TIM           0x00800000
#define AR_ISR_QCBROVF       0x02000000
#define AR_ISR_QCBRURN       0x04000000
#define AR_ISR_QTRIG         0x08000000
#define AR_ISR_GENTMR        0x10000000

#define AR_ISR_TXMINTR       0x00080000
#define AR_ISR_RXMINTR       0x01000000
#define AR_ISR_TXINTM        0x40000000
#define AR_ISR_RXINTM        0x80000000

#define AR_ISR_S0               0x0084
#define AR_ISR_S0_QCU_TXOK      0x000003FF
#define AR_ISR_S0_QCU_TXOK_S    0
#define AR_ISR_S0_QCU_TXDESC    0x03FF0000
#define AR_ISR_S0_QCU_TXDESC_S  16

#define AR_ISR_S1              0x0088
#define AR_ISR_S1_QCU_TXERR    0x000003FF
#define AR_ISR_S1_QCU_TXERR_S  0
#define AR_ISR_S1_QCU_TXEOL    0x03FF0000
#define AR_ISR_S1_QCU_TXEOL_S  16

#define AR_ISR_S2              0x008c
#define AR_ISR_S2_QCU_TXURN    0x000003FF
#define AR_ISR_S2_BB_WATCHDOG  0x00010000
#define AR_ISR_S2_CST          0x00400000
#define AR_ISR_S2_GTT          0x00800000
#define AR_ISR_S2_TIM          0x01000000
#define AR_ISR_S2_CABEND       0x02000000
#define AR_ISR_S2_DTIMSYNC     0x04000000
#define AR_ISR_S2_BCNTO        0x08000000
#define AR_ISR_S2_CABTO        0x10000000
#define AR_ISR_S2_DTIM         0x20000000
#define AR_ISR_S2_TSFOOR       0x40000000
#define AR_ISR_S2_TBTT_TIME    0x80000000

#define AR_ISR_S3             0x0090
#define AR_ISR_S3_QCU_QCBROVF    0x000003FF
#define AR_ISR_S3_QCU_QCBRURN    0x03FF0000

#define AR_ISR_S4              0x0094
#define AR_ISR_S4_QCU_QTRIG    0x000003FF
#define AR_ISR_S4_RESV0        0xFFFFFC00

#define AR_ISR_S5                   0x0098
#define AR_ISR_S5_TIMER_TRIG        0x000000FF
#define AR_ISR_S5_TIMER_THRESH      0x0007FE00
#define AR_ISR_S5_TIM_TIMER         0x00000010
#define AR_ISR_S5_DTIM_TIMER        0x00000020
#define AR_IMR_S5                   0x00b8
#define AR_IMR_S5_TIM_TIMER         0x00000010
#define AR_IMR_S5_DTIM_TIMER        0x00000020
#define AR_ISR_S5_GENTIMER_TRIG     0x0000FF80
#define AR_ISR_S5_GENTIMER_TRIG_S   0
#define AR_ISR_S5_GENTIMER_THRESH   0xFF800000
#define AR_ISR_S5_GENTIMER_THRESH_S 16
#define AR_IMR_S5_GENTIMER_TRIG     0x0000FF80
#define AR_IMR_S5_GENTIMER_TRIG_S   0
#define AR_IMR_S5_GENTIMER_THRESH   0xFF800000
#define AR_IMR_S5_GENTIMER_THRESH_S 16

#define AR_IMR               0x00a0
#define AR_IMR_RXOK          0x00000001
#define AR_IMR_RXDESC        0x00000002
#define AR_IMR_RXOK_HP	     0x00000001
#define AR_IMR_RXOK_LP	     0x00000002
#define AR_IMR_RXERR         0x00000004
#define AR_IMR_RXNOPKT       0x00000008
#define AR_IMR_RXEOL         0x00000010
#define AR_IMR_RXORN         0x00000020
#define AR_IMR_TXOK          0x00000040
#define AR_IMR_TXDESC        0x00000080
#define AR_IMR_TXERR         0x00000100
#define AR_IMR_TXNOPKT       0x00000200
#define AR_IMR_TXEOL         0x00000400
#define AR_IMR_TXURN         0x00000800
#define AR_IMR_MIB           0x00001000
#define AR_IMR_SWI           0x00002000
#define AR_IMR_RXPHY         0x00004000
#define AR_IMR_RXKCM         0x00008000
#define AR_IMR_SWBA          0x00010000
#define AR_IMR_BRSSI         0x00020000
#define AR_IMR_BMISS         0x00040000
#define AR_IMR_BNR           0x00100000
#define AR_IMR_RXCHIRP       0x00200000
#define AR_IMR_BCNMISC       0x00800000
#define AR_IMR_TIM           0x00800000
#define AR_IMR_QCBROVF       0x02000000
#define AR_IMR_QCBRURN       0x04000000
#define AR_IMR_QTRIG         0x08000000
#define AR_IMR_GENTMR        0x10000000

#define AR_IMR_TXMINTR       0x00080000
#define AR_IMR_RXMINTR       0x01000000
#define AR_IMR_TXINTM        0x40000000
#define AR_IMR_RXINTM        0x80000000

#define AR_IMR_S0               0x00a4
#define AR_IMR_S0_QCU_TXOK      0x000003FF
#define AR_IMR_S0_QCU_TXOK_S    0
#define AR_IMR_S0_QCU_TXDESC    0x03FF0000
#define AR_IMR_S0_QCU_TXDESC_S  16

#define AR_IMR_S1              0x00a8
#define AR_IMR_S1_QCU_TXERR    0x000003FF
#define AR_IMR_S1_QCU_TXERR_S  0
#define AR_IMR_S1_QCU_TXEOL    0x03FF0000
#define AR_IMR_S1_QCU_TXEOL_S  16

#define AR_IMR_S2              0x00ac
#define AR_IMR_S2_QCU_TXURN    0x000003FF
#define AR_IMR_S2_QCU_TXURN_S  0
#define AR_IMR_S2_CST          0x00400000
#define AR_IMR_S2_GTT          0x00800000
#define AR_IMR_S2_TIM          0x01000000
#define AR_IMR_S2_CABEND       0x02000000
#define AR_IMR_S2_DTIMSYNC     0x04000000
#define AR_IMR_S2_BCNTO        0x08000000
#define AR_IMR_S2_CABTO        0x10000000
#define AR_IMR_S2_DTIM         0x20000000
#define AR_IMR_S2_TSFOOR       0x40000000

#define AR_IMR_S3                0x00b0
#define AR_IMR_S3_QCU_QCBROVF    0x000003FF
#define AR_IMR_S3_QCU_QCBRURN    0x03FF0000
#define AR_IMR_S3_QCU_QCBRURN_S  16

#define AR_IMR_S4              0x00b4
#define AR_IMR_S4_QCU_QTRIG    0x000003FF
#define AR_IMR_S4_RESV0        0xFFFFFC00

#define AR_IMR_S5              0x00b8
#define AR_IMR_S5_TIMER_TRIG        0x000000FF
#define AR_IMR_S5_TIMER_THRESH      0x0000FF00


#define AR_ISR_RAC            0x00c0
#define AR_ISR_S0_S           0x00c4
#define AR_ISR_S0_QCU_TXOK      0x000003FF
#define AR_ISR_S0_QCU_TXOK_S    0
#define AR_ISR_S0_QCU_TXDESC    0x03FF0000
#define AR_ISR_S0_QCU_TXDESC_S  16

#define AR_ISR_S1_S           0x00c8
#define AR_ISR_S1_QCU_TXERR    0x000003FF
#define AR_ISR_S1_QCU_TXERR_S  0
#define AR_ISR_S1_QCU_TXEOL    0x03FF0000
#define AR_ISR_S1_QCU_TXEOL_S  16

#define AR_ISR_S2_S           (AR_SREV_9300_20_OR_LATER(ah) ? 0x00d0 : 0x00cc)
#define AR_ISR_S3_S           (AR_SREV_9300_20_OR_LATER(ah) ? 0x00d4 : 0x00d0)
#define AR_ISR_S4_S           (AR_SREV_9300_20_OR_LATER(ah) ? 0x00d8 : 0x00d4)
#define AR_ISR_S5_S           (AR_SREV_9300_20_OR_LATER(ah) ? 0x00dc : 0x00d8)
#define AR_DMADBG_0           0x00e0
#define AR_DMADBG_1           0x00e4
#define AR_DMADBG_2           0x00e8
#define AR_DMADBG_3           0x00ec
#define AR_DMADBG_4           0x00f0
#define AR_DMADBG_5           0x00f4
#define AR_DMADBG_6           0x00f8
#define AR_DMADBG_7           0x00fc

#define AR_NUM_QCU      10
#define AR_QCU_0        0x0001
#define AR_QCU_1        0x0002
#define AR_QCU_2        0x0004
#define AR_QCU_3        0x0008
#define AR_QCU_4        0x0010
#define AR_QCU_5        0x0020
#define AR_QCU_6        0x0040
#define AR_QCU_7        0x0080
#define AR_QCU_8        0x0100
#define AR_QCU_9        0x0200

#define AR_Q0_TXDP           0x0800
#define AR_Q1_TXDP           0x0804
#define AR_Q2_TXDP           0x0808
#define AR_Q3_TXDP           0x080c
#define AR_Q4_TXDP           0x0810
#define AR_Q5_TXDP           0x0814
#define AR_Q6_TXDP           0x0818
#define AR_Q7_TXDP           0x081c
#define AR_Q8_TXDP           0x0820
#define AR_Q9_TXDP           0x0824
#define AR_QTXDP(_i)    (AR_Q0_TXDP + ((_i)<<2))

#define AR_Q_STATUS_RING_START	0x830
#define AR_Q_STATUS_RING_END	0x834

#define AR_Q_TXE             0x0840
#define AR_Q_TXE_M           0x000003FF

#define AR_Q_TXD             0x0880
#define AR_Q_TXD_M           0x000003FF

#define AR_Q0_CBRCFG         0x08c0
#define AR_Q1_CBRCFG         0x08c4
#define AR_Q2_CBRCFG         0x08c8
#define AR_Q3_CBRCFG         0x08cc
#define AR_Q4_CBRCFG         0x08d0
#define AR_Q5_CBRCFG         0x08d4
#define AR_Q6_CBRCFG         0x08d8
#define AR_Q7_CBRCFG         0x08dc
#define AR_Q8_CBRCFG         0x08e0
#define AR_Q9_CBRCFG         0x08e4
#define AR_QCBRCFG(_i)      (AR_Q0_CBRCFG + ((_i)<<2))
#define AR_Q_CBRCFG_INTERVAL     0x00FFFFFF
#define AR_Q_CBRCFG_INTERVAL_S   0
#define AR_Q_CBRCFG_OVF_THRESH   0xFF000000
#define AR_Q_CBRCFG_OVF_THRESH_S 24

#define AR_Q0_RDYTIMECFG         0x0900
#define AR_Q1_RDYTIMECFG         0x0904
#define AR_Q2_RDYTIMECFG         0x0908
#define AR_Q3_RDYTIMECFG         0x090c
#define AR_Q4_RDYTIMECFG         0x0910
#define AR_Q5_RDYTIMECFG         0x0914
#define AR_Q6_RDYTIMECFG         0x0918
#define AR_Q7_RDYTIMECFG         0x091c
#define AR_Q8_RDYTIMECFG         0x0920
#define AR_Q9_RDYTIMECFG         0x0924
#define AR_QRDYTIMECFG(_i)       (AR_Q0_RDYTIMECFG + ((_i)<<2))
#define AR_Q_RDYTIMECFG_DURATION   0x00FFFFFF
#define AR_Q_RDYTIMECFG_DURATION_S 0
#define AR_Q_RDYTIMECFG_EN         0x01000000

#define AR_Q_ONESHOTARM_SC       0x0940
#define AR_Q_ONESHOTARM_SC_M     0x000003FF
#define AR_Q_ONESHOTARM_SC_RESV0 0xFFFFFC00

#define AR_Q_ONESHOTARM_CC       0x0980
#define AR_Q_ONESHOTARM_CC_M     0x000003FF
#define AR_Q_ONESHOTARM_CC_RESV0 0xFFFFFC00

#define AR_Q0_MISC         0x09c0
#define AR_Q1_MISC         0x09c4
#define AR_Q2_MISC         0x09c8
#define AR_Q3_MISC         0x09cc
#define AR_Q4_MISC         0x09d0
#define AR_Q5_MISC         0x09d4
#define AR_Q6_MISC         0x09d8
#define AR_Q7_MISC         0x09dc
#define AR_Q8_MISC         0x09e0
#define AR_Q9_MISC         0x09e4
#define AR_QMISC(_i)       (AR_Q0_MISC + ((_i)<<2))
#define AR_Q_MISC_FSP                     0x0000000F
#define AR_Q_MISC_FSP_ASAP                0
#define AR_Q_MISC_FSP_CBR                 1
#define AR_Q_MISC_FSP_DBA_GATED           2
#define AR_Q_MISC_FSP_TIM_GATED           3
#define AR_Q_MISC_FSP_BEACON_SENT_GATED   4
#define AR_Q_MISC_FSP_BEACON_RCVD_GATED   5
#define AR_Q_MISC_ONE_SHOT_EN             0x00000010
#define AR_Q_MISC_CBR_INCR_DIS1           0x00000020
#define AR_Q_MISC_CBR_INCR_DIS0           0x00000040
#define AR_Q_MISC_BEACON_USE              0x00000080
#define AR_Q_MISC_CBR_EXP_CNTR_LIMIT_EN   0x00000100
#define AR_Q_MISC_RDYTIME_EXP_POLICY      0x00000200
#define AR_Q_MISC_RESET_CBR_EXP_CTR       0x00000400
#define AR_Q_MISC_DCU_EARLY_TERM_REQ      0x00000800
#define AR_Q_MISC_RESV0                   0xFFFFF000

#define AR_Q0_STS         0x0a00
#define AR_Q1_STS         0x0a04
#define AR_Q2_STS         0x0a08
#define AR_Q3_STS         0x0a0c
#define AR_Q4_STS         0x0a10
#define AR_Q5_STS         0x0a14
#define AR_Q6_STS         0x0a18
#define AR_Q7_STS         0x0a1c
#define AR_Q8_STS         0x0a20
#define AR_Q9_STS         0x0a24
#define AR_QSTS(_i)       (AR_Q0_STS + ((_i)<<2))
#define AR_Q_STS_PEND_FR_CNT          0x00000003
#define AR_Q_STS_RESV0                0x000000FC
#define AR_Q_STS_CBR_EXP_CNT          0x0000FF00
#define AR_Q_STS_RESV1                0xFFFF0000

#define AR_Q_RDYTIMESHDN    0x0a40
#define AR_Q_RDYTIMESHDN_M  0x000003FF

/* MAC Descriptor CRC check */
#define AR_Q_DESC_CRCCHK    0xa44
/* Enable CRC check on the descriptor fetched from host */
#define AR_Q_DESC_CRCCHK_EN 1

#define AR_NUM_DCU      10
#define AR_DCU_0        0x0001
#define AR_DCU_1        0x0002
#define AR_DCU_2        0x0004
#define AR_DCU_3        0x0008
#define AR_DCU_4        0x0010
#define AR_DCU_5        0x0020
#define AR_DCU_6        0x0040
#define AR_DCU_7        0x0080
#define AR_DCU_8        0x0100
#define AR_DCU_9        0x0200

#define AR_D0_QCUMASK     0x1000
#define AR_D1_QCUMASK     0x1004
#define AR_D2_QCUMASK     0x1008
#define AR_D3_QCUMASK     0x100c
#define AR_D4_QCUMASK     0x1010
#define AR_D5_QCUMASK     0x1014
#define AR_D6_QCUMASK     0x1018
#define AR_D7_QCUMASK     0x101c
#define AR_D8_QCUMASK     0x1020
#define AR_D9_QCUMASK     0x1024
#define AR_DQCUMASK(_i)   (AR_D0_QCUMASK + ((_i)<<2))
#define AR_D_QCUMASK         0x000003FF
#define AR_D_QCUMASK_RESV0   0xFFFFFC00

#define AR_D_TXBLK_CMD  0x1038
#define AR_D_TXBLK_DATA(i) (AR_D_TXBLK_CMD+(i))

#define AR_D0_LCL_IFS     0x1040
#define AR_D1_LCL_IFS     0x1044
#define AR_D2_LCL_IFS     0x1048
#define AR_D3_LCL_IFS     0x104c
#define AR_D4_LCL_IFS     0x1050
#define AR_D5_LCL_IFS     0x1054
#define AR_D6_LCL_IFS     0x1058
#define AR_D7_LCL_IFS     0x105c
#define AR_D8_LCL_IFS     0x1060
#define AR_D9_LCL_IFS     0x1064
#define AR_DLCL_IFS(_i)   (AR_D0_LCL_IFS + ((_i)<<2))
#define AR_D_LCL_IFS_CWMIN       0x000003FF
#define AR_D_LCL_IFS_CWMIN_S     0
#define AR_D_LCL_IFS_CWMAX       0x000FFC00
#define AR_D_LCL_IFS_CWMAX_S     10
#define AR_D_LCL_IFS_AIFS        0x0FF00000
#define AR_D_LCL_IFS_AIFS_S      20

#define AR_D_LCL_IFS_RESV0    0xF0000000

#define AR_D0_RETRY_LIMIT     0x1080
#define AR_D1_RETRY_LIMIT     0x1084
#define AR_D2_RETRY_LIMIT     0x1088
#define AR_D3_RETRY_LIMIT     0x108c
#define AR_D4_RETRY_LIMIT     0x1090
#define AR_D5_RETRY_LIMIT     0x1094
#define AR_D6_RETRY_LIMIT     0x1098
#define AR_D7_RETRY_LIMIT     0x109c
#define AR_D8_RETRY_LIMIT     0x10a0
#define AR_D9_RETRY_LIMIT     0x10a4
#define AR_DRETRY_LIMIT(_i)   (AR_D0_RETRY_LIMIT + ((_i)<<2))
#define AR_D_RETRY_LIMIT_FR_SH       0x0000000F
#define AR_D_RETRY_LIMIT_FR_SH_S     0
#define AR_D_RETRY_LIMIT_STA_SH      0x00003F00
#define AR_D_RETRY_LIMIT_STA_SH_S    8
#define AR_D_RETRY_LIMIT_STA_LG      0x000FC000
#define AR_D_RETRY_LIMIT_STA_LG_S    14
#define AR_D_RETRY_LIMIT_RESV0       0xFFF00000

#define AR_D0_CHNTIME     0x10c0
#define AR_D1_CHNTIME     0x10c4
#define AR_D2_CHNTIME     0x10c8
#define AR_D3_CHNTIME     0x10cc
#define AR_D4_CHNTIME     0x10d0
#define AR_D5_CHNTIME     0x10d4
#define AR_D6_CHNTIME     0x10d8
#define AR_D7_CHNTIME     0x10dc
#define AR_D8_CHNTIME     0x10e0
#define AR_D9_CHNTIME     0x10e4
#define AR_DCHNTIME(_i)   (AR_D0_CHNTIME + ((_i)<<2))
#define AR_D_CHNTIME_DUR         0x000FFFFF
#define AR_D_CHNTIME_DUR_S       0
#define AR_D_CHNTIME_EN          0x00100000
#define AR_D_CHNTIME_RESV0       0xFFE00000

#define AR_D0_MISC        0x1100
#define AR_D1_MISC        0x1104
#define AR_D2_MISC        0x1108
#define AR_D3_MISC        0x110c
#define AR_D4_MISC        0x1110
#define AR_D5_MISC        0x1114
#define AR_D6_MISC        0x1118
#define AR_D7_MISC        0x111c
#define AR_D8_MISC        0x1120
#define AR_D9_MISC        0x1124
#define AR_DMISC(_i)      (AR_D0_MISC + ((_i)<<2))
#define AR_D_MISC_BKOFF_THRESH        0x0000003F
#define AR_D_MISC_RETRY_CNT_RESET_EN  0x00000040
#define AR_D_MISC_CW_RESET_EN         0x00000080
#define AR_D_MISC_FRAG_WAIT_EN        0x00000100
#define AR_D_MISC_FRAG_BKOFF_EN       0x00000200
#define AR_D_MISC_CW_BKOFF_EN         0x00001000
#define AR_D_MISC_VIR_COL_HANDLING    0x0000C000
#define AR_D_MISC_VIR_COL_HANDLING_S  14
#define AR_D_MISC_VIR_COL_HANDLING_DEFAULT 0
#define AR_D_MISC_VIR_COL_HANDLING_IGNORE  1
#define AR_D_MISC_BEACON_USE          0x00010000
#define AR_D_MISC_ARB_LOCKOUT_CNTRL   0x00060000
#define AR_D_MISC_ARB_LOCKOUT_CNTRL_S 17
#define AR_D_MISC_ARB_LOCKOUT_CNTRL_NONE     0
#define AR_D_MISC_ARB_LOCKOUT_CNTRL_INTRA_FR 1
#define AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL   2
#define AR_D_MISC_ARB_LOCKOUT_IGNORE  0x00080000
#define AR_D_MISC_SEQ_NUM_INCR_DIS    0x00100000
#define AR_D_MISC_POST_FR_BKOFF_DIS   0x00200000
#define AR_D_MISC_VIT_COL_CW_BKOFF_EN 0x00400000
#define AR_D_MISC_BLOWN_IFS_RETRY_EN  0x00800000
#define AR_D_MISC_RESV0               0xFF000000

#define AR_D_SEQNUM      0x1140

#define AR_D_GBL_IFS_SIFS         0x1030
#define AR_D_GBL_IFS_SIFS_M       0x0000FFFF
#define AR_D_GBL_IFS_SIFS_RESV0   0xFFFFFFFF

#define AR_D_TXBLK_BASE            0x1038
#define AR_D_TXBLK_WRITE_BITMASK    0x0000FFFF
#define AR_D_TXBLK_WRITE_BITMASK_S  0
#define AR_D_TXBLK_WRITE_SLICE      0x000F0000
#define AR_D_TXBLK_WRITE_SLICE_S    16
#define AR_D_TXBLK_WRITE_DCU        0x00F00000
#define AR_D_TXBLK_WRITE_DCU_S      20
#define AR_D_TXBLK_WRITE_COMMAND    0x0F000000
#define AR_D_TXBLK_WRITE_COMMAND_S      24

#define AR_D_GBL_IFS_SLOT         0x1070
#define AR_D_GBL_IFS_SLOT_M       0x0000FFFF
#define AR_D_GBL_IFS_SLOT_RESV0   0xFFFF0000

#define AR_D_GBL_IFS_EIFS         0x10b0
#define AR_D_GBL_IFS_EIFS_M       0x0000FFFF
#define AR_D_GBL_IFS_EIFS_RESV0   0xFFFF0000
#define AR_D_GBL_IFS_EIFS_ASYNC_FIFO 363

#define AR_D_GBL_IFS_MISC        0x10f0
#define AR_D_GBL_IFS_MISC_LFSR_SLICE_SEL        0x00000007
#define AR_D_GBL_IFS_MISC_TURBO_MODE            0x00000008
#define AR_D_GBL_IFS_MISC_USEC_DURATION         0x000FFC00
#define AR_D_GBL_IFS_MISC_DCU_ARBITER_DLY       0x00300000
#define AR_D_GBL_IFS_MISC_RANDOM_LFSR_SLICE_DIS 0x01000000
#define AR_D_GBL_IFS_MISC_SLOT_XMIT_WIND_LEN    0x06000000
#define AR_D_GBL_IFS_MISC_FORCE_XMIT_SLOT_BOUND 0x08000000
#define AR_D_GBL_IFS_MISC_IGNORE_BACKOFF        0x10000000

#define AR_D_FPCTL                  0x1230
#define AR_D_FPCTL_DCU              0x0000000F
#define AR_D_FPCTL_DCU_S            0
#define AR_D_FPCTL_PREFETCH_EN      0x00000010
#define AR_D_FPCTL_BURST_PREFETCH   0x00007FE0
#define AR_D_FPCTL_BURST_PREFETCH_S 5

#define AR_D_TXPSE                 0x1270
#define AR_D_TXPSE_CTRL            0x000003FF
#define AR_D_TXPSE_RESV0           0x0000FC00
#define AR_D_TXPSE_STATUS          0x00010000
#define AR_D_TXPSE_RESV1           0xFFFE0000

#define AR_D_TXSLOTMASK            0x12f0
#define AR_D_TXSLOTMASK_NUM        0x0000000F

#define AR_CFG_LED                     0x1f04
#define AR_CFG_SCLK_RATE_IND           0x00000003
#define AR_CFG_SCLK_RATE_IND_S         0
#define AR_CFG_SCLK_32MHZ              0x00000000
#define AR_CFG_SCLK_4MHZ               0x00000001
#define AR_CFG_SCLK_1MHZ               0x00000002
#define AR_CFG_SCLK_32KHZ              0x00000003
#define AR_CFG_LED_BLINK_SLOW          0x00000008
#define AR_CFG_LED_BLINK_THRESH_SEL    0x00000070
#define AR_CFG_LED_MODE_SEL            0x00000380
#define AR_CFG_LED_MODE_SEL_S          7
#define AR_CFG_LED_POWER               0x00000280
#define AR_CFG_LED_POWER_S             7
#define AR_CFG_LED_NETWORK             0x00000300
#define AR_CFG_LED_NETWORK_S           7
#define AR_CFG_LED_MODE_PROP           0x0
#define AR_CFG_LED_MODE_RPROP          0x1
#define AR_CFG_LED_MODE_SPLIT          0x2
#define AR_CFG_LED_MODE_RAND           0x3
#define AR_CFG_LED_MODE_POWER_OFF      0x4
#define AR_CFG_LED_MODE_POWER_ON       0x5
#define AR_CFG_LED_MODE_NETWORK_OFF    0x4
#define AR_CFG_LED_MODE_NETWORK_ON     0x6
#define AR_CFG_LED_ASSOC_CTL           0x00000c00
#define AR_CFG_LED_ASSOC_CTL_S         10
#define AR_CFG_LED_ASSOC_NONE          0x0
#define AR_CFG_LED_ASSOC_ACTIVE        0x1
#define AR_CFG_LED_ASSOC_PENDING       0x2

#define AR_CFG_LED_BLINK_SLOW          0x00000008
#define AR_CFG_LED_BLINK_SLOW_S        3

#define AR_CFG_LED_BLINK_THRESH_SEL    0x00000070
#define AR_CFG_LED_BLINK_THRESH_SEL_S  4

#define AR_MAC_SLEEP                0x1f00
#define AR_MAC_SLEEP_MAC_AWAKE      0x00000000
#define AR_MAC_SLEEP_MAC_ASLEEP     0x00000001

#define AR_RC                0x4000
#define AR_RC_AHB            0x00000001
#define AR_RC_APB            0x00000002
#define AR_RC_HOSTIF         0x00000100

#define AR_WA			(AR_SREV_9340(ah) ? 0x40c4 : 0x4004)
#define AR_WA_BIT6			(1 << 6)
#define AR_WA_BIT7			(1 << 7)
#define AR_WA_BIT23			(1 << 23)
#define AR_WA_D3_L1_DISABLE		(1 << 14)
#define AR_WA_UNTIE_RESET_EN		(1 << 15) /* Enable PCI Reset
						     to POR (power-on-reset) */
#define AR_WA_D3_TO_L1_DISABLE_REAL     (1 << 16)
#define AR_WA_ASPM_TIMER_BASED_DISABLE  (1 << 17)
#define AR_WA_RESET_EN                  (1 << 18) /* Enable PCI-Reset to
						     POR (bit 15) */
#define AR_WA_ANALOG_SHIFT              (1 << 20)
#define AR_WA_POR_SHORT                 (1 << 21) /* PCI-E Phy reset control */
#define AR_WA_BIT22			(1 << 22)
#define AR9285_WA_DEFAULT		0x004a050b
#define AR9280_WA_DEFAULT           	0x0040073b
#define AR_WA_DEFAULT               	0x0000073f


#define AR_PM_STATE                 0x4008
#define AR_PM_STATE_PME_D3COLD_VAUX 0x00100000

#define AR_HOST_TIMEOUT             (AR_SREV_9340(ah) ? 0x4008 : 0x4018)
#define AR_HOST_TIMEOUT_APB_CNTR    0x0000FFFF
#define AR_HOST_TIMEOUT_APB_CNTR_S  0
#define AR_HOST_TIMEOUT_LCL_CNTR    0xFFFF0000
#define AR_HOST_TIMEOUT_LCL_CNTR_S  16

#define AR_EEPROM                0x401c
#define AR_EEPROM_ABSENT         0x00000100
#define AR_EEPROM_CORRUPT        0x00000200
#define AR_EEPROM_PROT_MASK      0x03FFFC00
#define AR_EEPROM_PROT_MASK_S    10

#define EEPROM_PROTECT_RP_0_31        0x0001
#define EEPROM_PROTECT_WP_0_31        0x0002
#define EEPROM_PROTECT_RP_32_63       0x0004
#define EEPROM_PROTECT_WP_32_63       0x0008
#define EEPROM_PROTECT_RP_64_127      0x0010
#define EEPROM_PROTECT_WP_64_127      0x0020
#define EEPROM_PROTECT_RP_128_191     0x0040
#define EEPROM_PROTECT_WP_128_191     0x0080
#define EEPROM_PROTECT_RP_192_255     0x0100
#define EEPROM_PROTECT_WP_192_255     0x0200
#define EEPROM_PROTECT_RP_256_511     0x0400
#define EEPROM_PROTECT_WP_256_511     0x0800
#define EEPROM_PROTECT_RP_512_1023    0x1000
#define EEPROM_PROTECT_WP_512_1023    0x2000
#define EEPROM_PROTECT_RP_1024_2047   0x4000
#define EEPROM_PROTECT_WP_1024_2047   0x8000

#define AR_SREV \
	((AR_SREV_9100(ah)) ? 0x0600 : (AR_SREV_9340(ah) \
					? 0x400c : 0x4020))

#define AR_SREV_ID \
	((AR_SREV_9100(ah)) ? 0x00000FFF : 0x000000FF)
#define AR_SREV_VERSION                       0x000000F0
#define AR_SREV_VERSION_S                     4
#define AR_SREV_REVISION                      0x00000007

#define AR_SREV_ID2                           0xFFFFFFFF
#define AR_SREV_VERSION2        	      0xFFFC0000
#define AR_SREV_VERSION2_S                    18
#define AR_SREV_TYPE2        	      	      0x0003F000
#define AR_SREV_TYPE2_S                       12
#define AR_SREV_TYPE2_CHAIN		      0x00001000
#define AR_SREV_TYPE2_HOST_MODE		      0x00002000
#define AR_SREV_REVISION2        	      0x00000F00
#define AR_SREV_REVISION2_S     	      8

#define AR_SREV_VERSION_5416_PCI	0xD
#define AR_SREV_VERSION_5416_PCIE	0xC
#define AR_SREV_REVISION_5416_10	0
#define AR_SREV_REVISION_5416_20	1
#define AR_SREV_REVISION_5416_22	2
#define AR_SREV_VERSION_9100		0x14
#define AR_SREV_VERSION_9160		0x40
#define AR_SREV_REVISION_9160_10	0
#define AR_SREV_REVISION_9160_11	1
#define AR_SREV_VERSION_9280		0x80
#define AR_SREV_REVISION_9280_10	0
#define AR_SREV_REVISION_9280_20	1
#define AR_SREV_REVISION_9280_21	2
#define AR_SREV_VERSION_9285		0xC0
#define AR_SREV_REVISION_9285_10	0
#define AR_SREV_REVISION_9285_11	1
#define AR_SREV_REVISION_9285_12	2
#define AR_SREV_VERSION_9287		0x180
#define AR_SREV_REVISION_9287_10	0
#define AR_SREV_REVISION_9287_11	1
#define AR_SREV_REVISION_9287_12	2
#define AR_SREV_REVISION_9287_13	3
#define AR_SREV_VERSION_9271		0x140
#define AR_SREV_REVISION_9271_10	0
#define AR_SREV_REVISION_9271_11	1
#define AR_SREV_VERSION_9300		0x1c0
#define AR_SREV_REVISION_9300_20	2 /* 2.0 and 2.1 */
#define AR_SREV_VERSION_9330		0x200
#define AR_SREV_REVISION_9330_10	0
#define AR_SREV_REVISION_9330_11	1
#define AR_SREV_REVISION_9330_12	2
#define AR_SREV_VERSION_9485		0x240
#define AR_SREV_REVISION_9485_10	0
#define AR_SREV_REVISION_9485_11        1
#define AR_SREV_VERSION_9340		0x300
#define AR_SREV_VERSION_9580		0x1C0
#define AR_SREV_REVISION_9580_10	4 /* AR9580 1.0 */
#define AR_SREV_VERSION_9462		0x280
#define AR_SREV_REVISION_9462_20	2
#define AR_SREV_VERSION_9550		0x400

#define AR_SREV_5416(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_5416_PCI) || \
	 ((_ah)->hw_version.macVersion == AR_SREV_VERSION_5416_PCIE))
#define AR_SREV_5416_22_OR_LATER(_ah) \
	(((AR_SREV_5416(_ah)) && \
	 ((_ah)->hw_version.macRev >= AR_SREV_REVISION_5416_22)) || \
	 ((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9100))

#define AR_SREV_9100(ah) \
	((ah->hw_version.macVersion) == AR_SREV_VERSION_9100)
#define AR_SREV_9100_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9100))

#define AR_SREV_9160(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9160))
#define AR_SREV_9160_10_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9160))
#define AR_SREV_9160_11(_ah) \
	(AR_SREV_9160(_ah) && \
	 ((_ah)->hw_version.macRev == AR_SREV_REVISION_9160_11))
#define AR_SREV_9280(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9280))
#define AR_SREV_9280_20_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9280))
#define AR_SREV_9280_20(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9280))

#define AR_SREV_9285(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9285))
#define AR_SREV_9285_12_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9285))

#define AR_SREV_9287(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9287))
#define AR_SREV_9287_11_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9287))
#define AR_SREV_9287_11(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9287) && \
	 ((_ah)->hw_version.macRev == AR_SREV_REVISION_9287_11))
#define AR_SREV_9287_12(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9287) && \
	 ((_ah)->hw_version.macRev == AR_SREV_REVISION_9287_12))
#define AR_SREV_9287_12_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion > AR_SREV_VERSION_9287) || \
	 (((_ah)->hw_version.macVersion == AR_SREV_VERSION_9287) && \
	  ((_ah)->hw_version.macRev >= AR_SREV_REVISION_9287_12)))
#define AR_SREV_9287_13_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion > AR_SREV_VERSION_9287) || \
	 (((_ah)->hw_version.macVersion == AR_SREV_VERSION_9287) && \
	  ((_ah)->hw_version.macRev >= AR_SREV_REVISION_9287_13)))

#define AR_SREV_9271(_ah) \
    (((_ah))->hw_version.macVersion == AR_SREV_VERSION_9271)
#define AR_SREV_9271_10(_ah) \
    (AR_SREV_9271(_ah) && \
     ((_ah)->hw_version.macRev == AR_SREV_REVISION_9271_10))
#define AR_SREV_9271_11(_ah) \
    (AR_SREV_9271(_ah) && \
     ((_ah)->hw_version.macRev == AR_SREV_REVISION_9271_11))

#define AR_SREV_9300(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9300))
#define AR_SREV_9300_20_OR_LATER(_ah) \
	((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9300)

#define AR_SREV_9330(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9330))
#define AR_SREV_9330_10(_ah) \
	(AR_SREV_9330((_ah)) && \
	 ((_ah)->hw_version.macRev == AR_SREV_REVISION_9330_10))
#define AR_SREV_9330_11(_ah) \
	(AR_SREV_9330((_ah)) && \
	 ((_ah)->hw_version.macRev == AR_SREV_REVISION_9330_11))
#define AR_SREV_9330_12(_ah) \
	(AR_SREV_9330((_ah)) && \
	 ((_ah)->hw_version.macRev == AR_SREV_REVISION_9330_12))

#define AR_SREV_9485(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9485))
#define AR_SREV_9485_10(_ah) \
	(AR_SREV_9485(_ah) && \
	 ((_ah)->hw_version.macRev == AR_SREV_REVISION_9485_10))
#define AR_SREV_9485_11(_ah) \
	(AR_SREV_9485(_ah) && \
	 ((_ah)->hw_version.macRev == AR_SREV_REVISION_9485_11))
#define AR_SREV_9485_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9485))

#define AR_SREV_9340(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9340))

#define AR_SREV_9285E_20(_ah) \
    (AR_SREV_9285_12_OR_LATER(_ah) && \
     ((REG_READ(_ah, AR_AN_SYNTH9) & 0x7) == 0x1))

#define AR_SREV_9462(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9462))

#define AR_SREV_9462_20(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9462) && \
	((_ah)->hw_version.macRev == AR_SREV_REVISION_9462_20))

#define AR_SREV_9462_20_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9462) && \
	((_ah)->hw_version.macRev >= AR_SREV_REVISION_9462_20))

#define AR_SREV_9550(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9550))

#define AR_SREV_9580(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9580) && \
	((_ah)->hw_version.macRev >= AR_SREV_REVISION_9580_10))

#define AR_SREV_9580_10(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9580) && \
	((_ah)->hw_version.macRev == AR_SREV_REVISION_9580_10))

/* NOTE: When adding chips newer than Peacock, add chip check here */
#define AR_SREV_9580_10_OR_LATER(_ah) \
	(AR_SREV_9580(_ah))

enum ath_usb_dev {
	AR9280_USB = 1, /* AR7010 + AR9280, UB94 */
	AR9287_USB = 2, /* AR7010 + AR9287, UB95 */
	STORAGE_DEVICE = 3,
};

#define AR_DEVID_7010(_ah) \
	(((_ah)->hw_version.usbdev == AR9280_USB) || \
	 ((_ah)->hw_version.usbdev == AR9287_USB))

#define AR_RADIO_SREV_MAJOR                   0xf0
#define AR_RAD5133_SREV_MAJOR                 0xc0
#define AR_RAD2133_SREV_MAJOR                 0xd0
#define AR_RAD5122_SREV_MAJOR                 0xe0
#define AR_RAD2122_SREV_MAJOR                 0xf0

#define AR_AHB_MODE                           0x4024
#define AR_AHB_EXACT_WR_EN                    0x00000000
#define AR_AHB_BUF_WR_EN                      0x00000001
#define AR_AHB_EXACT_RD_EN                    0x00000000
#define AR_AHB_CACHELINE_RD_EN                0x00000002
#define AR_AHB_PREFETCH_RD_EN                 0x00000004
#define AR_AHB_PAGE_SIZE_1K                   0x00000000
#define AR_AHB_PAGE_SIZE_2K                   0x00000008
#define AR_AHB_PAGE_SIZE_4K                   0x00000010
#define AR_AHB_CUSTOM_BURST_EN                0x000000C0
#define AR_AHB_CUSTOM_BURST_EN_S              6
#define AR_AHB_CUSTOM_BURST_ASYNC_FIFO_VAL    3

#define AR_INTR_RTC_IRQ                       0x00000001
#define AR_INTR_MAC_IRQ                       0x00000002
#define AR_INTR_EEP_PROT_ACCESS               0x00000004
#define AR_INTR_MAC_AWAKE                     0x00020000
#define AR_INTR_MAC_ASLEEP                    0x00040000
#define AR_INTR_SPURIOUS                      0xFFFFFFFF


#define AR_INTR_SYNC_CAUSE                    (AR_SREV_9340(ah) ? 0x4010 : 0x4028)
#define AR_INTR_SYNC_CAUSE_CLR                (AR_SREV_9340(ah) ? 0x4010 : 0x4028)


#define AR_INTR_SYNC_ENABLE                   (AR_SREV_9340(ah) ? 0x4014 : 0x402c)
#define AR_INTR_SYNC_ENABLE_GPIO              0xFFFC0000
#define AR_INTR_SYNC_ENABLE_GPIO_S            18

enum {
	AR_INTR_SYNC_RTC_IRQ = 0x00000001,
	AR_INTR_SYNC_MAC_IRQ = 0x00000002,
	AR_INTR_SYNC_EEPROM_ILLEGAL_ACCESS = 0x00000004,
	AR_INTR_SYNC_APB_TIMEOUT = 0x00000008,
	AR_INTR_SYNC_PCI_MODE_CONFLICT = 0x00000010,
	AR_INTR_SYNC_HOST1_FATAL = 0x00000020,
	AR_INTR_SYNC_HOST1_PERR = 0x00000040,
	AR_INTR_SYNC_TRCV_FIFO_PERR = 0x00000080,
	AR_INTR_SYNC_RADM_CPL_EP = 0x00000100,
	AR_INTR_SYNC_RADM_CPL_DLLP_ABORT = 0x00000200,
	AR_INTR_SYNC_RADM_CPL_TLP_ABORT = 0x00000400,
	AR_INTR_SYNC_RADM_CPL_ECRC_ERR = 0x00000800,
	AR_INTR_SYNC_RADM_CPL_TIMEOUT = 0x00001000,
	AR_INTR_SYNC_LOCAL_TIMEOUT = 0x00002000,
	AR_INTR_SYNC_PM_ACCESS = 0x00004000,
	AR_INTR_SYNC_MAC_AWAKE = 0x00008000,
	AR_INTR_SYNC_MAC_ASLEEP = 0x00010000,
	AR_INTR_SYNC_MAC_SLEEP_ACCESS = 0x00020000,
	AR_INTR_SYNC_ALL = 0x0003FFFF,


	AR_INTR_SYNC_DEFAULT = (AR_INTR_SYNC_HOST1_FATAL |
				AR_INTR_SYNC_HOST1_PERR |
				AR_INTR_SYNC_RADM_CPL_EP |
				AR_INTR_SYNC_RADM_CPL_DLLP_ABORT |
				AR_INTR_SYNC_RADM_CPL_TLP_ABORT |
				AR_INTR_SYNC_RADM_CPL_ECRC_ERR |
				AR_INTR_SYNC_RADM_CPL_TIMEOUT |
				AR_INTR_SYNC_LOCAL_TIMEOUT |
				AR_INTR_SYNC_MAC_SLEEP_ACCESS),

	AR_INTR_SYNC_SPURIOUS = 0xFFFFFFFF,

};

#define AR_INTR_ASYNC_MASK                       (AR_SREV_9340(ah) ? 0x4018 : 0x4030)
#define AR_INTR_ASYNC_MASK_GPIO                  0xFFFC0000
#define AR_INTR_ASYNC_MASK_GPIO_S                18
#define AR_INTR_ASYNC_MASK_MCI                   0x00000080
#define AR_INTR_ASYNC_MASK_MCI_S                 7

#define AR_INTR_SYNC_MASK                        (AR_SREV_9340(ah) ? 0x401c : 0x4034)
#define AR_INTR_SYNC_MASK_GPIO                   0xFFFC0000
#define AR_INTR_SYNC_MASK_GPIO_S                 18

#define AR_INTR_ASYNC_CAUSE_CLR                  (AR_SREV_9340(ah) ? 0x4020 : 0x4038)
#define AR_INTR_ASYNC_CAUSE                      (AR_SREV_9340(ah) ? 0x4020 : 0x4038)
#define AR_INTR_ASYNC_CAUSE_MCI			 0x00000080
#define AR_INTR_ASYNC_USED			 (AR_INTR_MAC_IRQ | \
						  AR_INTR_ASYNC_CAUSE_MCI)

/* Asynchronous Interrupt Enable Register */
#define AR_INTR_ASYNC_ENABLE_MCI         0x00000080
#define AR_INTR_ASYNC_ENABLE_MCI_S       7


#define AR_INTR_ASYNC_ENABLE                     (AR_SREV_9340(ah) ? 0x4024 : 0x403c)
#define AR_INTR_ASYNC_ENABLE_GPIO                0xFFFC0000
#define AR_INTR_ASYNC_ENABLE_GPIO_S              18

#define AR_PCIE_SERDES                           0x4040
#define AR_PCIE_SERDES2                          0x4044
#define AR_PCIE_PM_CTRL                          (AR_SREV_9340(ah) ? 0x4004 : 0x4014)
#define AR_PCIE_PM_CTRL_ENA                      0x00080000

#define AR_PCIE_PHY_REG3			 0x18c08

#define AR_NUM_GPIO                              14
#define AR928X_NUM_GPIO                          10
#define AR9285_NUM_GPIO                          12
#define AR9287_NUM_GPIO                          11
#define AR9271_NUM_GPIO                          16
#define AR9300_NUM_GPIO                          17
#define AR7010_NUM_GPIO                          16

#define AR_GPIO_IN_OUT                           (AR_SREV_9340(ah) ? 0x4028 : 0x4048)
#define AR_GPIO_IN_VAL                           0x0FFFC000
#define AR_GPIO_IN_VAL_S                         14
#define AR928X_GPIO_IN_VAL                       0x000FFC00
#define AR928X_GPIO_IN_VAL_S                     10
#define AR9285_GPIO_IN_VAL                       0x00FFF000
#define AR9285_GPIO_IN_VAL_S                     12
#define AR9287_GPIO_IN_VAL                       0x003FF800
#define AR9287_GPIO_IN_VAL_S                     11
#define AR9271_GPIO_IN_VAL                       0xFFFF0000
#define AR9271_GPIO_IN_VAL_S                     16
#define AR7010_GPIO_IN_VAL                       0x0000FFFF
#define AR7010_GPIO_IN_VAL_S                     0

#define AR_GPIO_IN				 (AR_SREV_9340(ah) ? 0x402c : 0x404c)
#define AR9300_GPIO_IN_VAL                       0x0001FFFF
#define AR9300_GPIO_IN_VAL_S                     0

#define AR_GPIO_OE_OUT                           (AR_SREV_9340(ah) ? 0x4030 : \
						  (AR_SREV_9300_20_OR_LATER(ah) ? 0x4050 : 0x404c))
#define AR_GPIO_OE_OUT_DRV                       0x3
#define AR_GPIO_OE_OUT_DRV_NO                    0x0
#define AR_GPIO_OE_OUT_DRV_LOW                   0x1
#define AR_GPIO_OE_OUT_DRV_HI                    0x2
#define AR_GPIO_OE_OUT_DRV_ALL                   0x3

#define AR7010_GPIO_OE                           0x52000
#define AR7010_GPIO_OE_MASK                      0x1
#define AR7010_GPIO_OE_AS_OUTPUT                 0x0
#define AR7010_GPIO_OE_AS_INPUT                  0x1
#define AR7010_GPIO_IN                           0x52004
#define AR7010_GPIO_OUT                          0x52008
#define AR7010_GPIO_SET                          0x5200C
#define AR7010_GPIO_CLEAR                        0x52010
#define AR7010_GPIO_INT                          0x52014
#define AR7010_GPIO_INT_TYPE                     0x52018
#define AR7010_GPIO_INT_POLARITY                 0x5201C
#define AR7010_GPIO_PENDING                      0x52020
#define AR7010_GPIO_INT_MASK                     0x52024
#define AR7010_GPIO_FUNCTION                     0x52028

#define AR_GPIO_INTR_POL                         (AR_SREV_9340(ah) ? 0x4038 : \
						  (AR_SREV_9300_20_OR_LATER(ah) ? 0x4058 : 0x4050))
#define AR_GPIO_INTR_POL_VAL                     0x0001FFFF
#define AR_GPIO_INTR_POL_VAL_S                   0

#define AR_GPIO_INPUT_EN_VAL                     (AR_SREV_9340(ah) ? 0x403c : \
						  (AR_SREV_9300_20_OR_LATER(ah) ? 0x405c : 0x4054))
#define AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_DEF     0x00000004
#define AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_S       2
#define AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_DEF    0x00000008
#define AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_S      3
#define AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_DEF       0x00000010
#define AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_S         4
#define AR_GPIO_INPUT_EN_VAL_RFSILENT_DEF        0x00000080
#define AR_GPIO_INPUT_EN_VAL_RFSILENT_DEF_S      7
#define AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_BB      0x00000400
#define AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_BB_S    10
#define AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB        0x00001000
#define AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB_S      12
#define AR_GPIO_INPUT_EN_VAL_RFSILENT_BB         0x00008000
#define AR_GPIO_INPUT_EN_VAL_RFSILENT_BB_S       15
#define AR_GPIO_RTC_RESET_OVERRIDE_ENABLE        0x00010000
#define AR_GPIO_JTAG_DISABLE                     0x00020000

#define AR_GPIO_INPUT_MUX1                       (AR_SREV_9340(ah) ? 0x4040 : \
						  (AR_SREV_9300_20_OR_LATER(ah) ? 0x4060 : 0x4058))
#define AR_GPIO_INPUT_MUX1_BT_ACTIVE             0x000f0000
#define AR_GPIO_INPUT_MUX1_BT_ACTIVE_S           16
#define AR_GPIO_INPUT_MUX1_BT_PRIORITY           0x00000f00
#define AR_GPIO_INPUT_MUX1_BT_PRIORITY_S         8

#define AR_GPIO_INPUT_MUX2                       (AR_SREV_9340(ah) ? 0x4044 : \
						  (AR_SREV_9300_20_OR_LATER(ah) ? 0x4064 : 0x405c))
#define AR_GPIO_INPUT_MUX2_CLK25                 0x0000000f
#define AR_GPIO_INPUT_MUX2_CLK25_S               0
#define AR_GPIO_INPUT_MUX2_RFSILENT              0x000000f0
#define AR_GPIO_INPUT_MUX2_RFSILENT_S            4
#define AR_GPIO_INPUT_MUX2_RTC_RESET             0x00000f00
#define AR_GPIO_INPUT_MUX2_RTC_RESET_S           8

#define AR_GPIO_OUTPUT_MUX1                      (AR_SREV_9340(ah) ? 0x4048 : \
						  (AR_SREV_9300_20_OR_LATER(ah) ? 0x4068 : 0x4060))
#define AR_GPIO_OUTPUT_MUX2                      (AR_SREV_9340(ah) ? 0x404c : \
						  (AR_SREV_9300_20_OR_LATER(ah) ? 0x406c : 0x4064))
#define AR_GPIO_OUTPUT_MUX3                      (AR_SREV_9340(ah) ? 0x4050 : \
						  (AR_SREV_9300_20_OR_LATER(ah) ? 0x4070 : 0x4068))

#define AR_INPUT_STATE                           (AR_SREV_9340(ah) ? 0x4054 : \
						  (AR_SREV_9300_20_OR_LATER(ah) ? 0x4074 : 0x406c))

#define AR_EEPROM_STATUS_DATA                    (AR_SREV_9340(ah) ? 0x40c8 : \
						  (AR_SREV_9300_20_OR_LATER(ah) ? 0x4084 : 0x407c))
#define AR_EEPROM_STATUS_DATA_VAL                0x0000ffff
#define AR_EEPROM_STATUS_DATA_VAL_S              0
#define AR_EEPROM_STATUS_DATA_BUSY               0x00010000
#define AR_EEPROM_STATUS_DATA_BUSY_ACCESS        0x00020000
#define AR_EEPROM_STATUS_DATA_PROT_ACCESS        0x00040000
#define AR_EEPROM_STATUS_DATA_ABSENT_ACCESS      0x00080000

#define AR_OBS                  (AR_SREV_9340(ah) ? 0x405c : \
				 (AR_SREV_9300_20_OR_LATER(ah) ? 0x4088 : 0x4080))

#define AR_GPIO_PDPU                             (AR_SREV_9300_20_OR_LATER(ah) ? 0x4090 : 0x4088)

#define AR_PCIE_MSI                             (AR_SREV_9340(ah) ? 0x40d8 : \
						 (AR_SREV_9300_20_OR_LATER(ah) ? 0x40a4 : 0x4094))
#define AR_PCIE_MSI_ENABLE                       0x00000001

#define AR_INTR_PRIO_SYNC_ENABLE  (AR_SREV_9340(ah) ? 0x4088 : 0x40c4)
#define AR_INTR_PRIO_ASYNC_MASK   (AR_SREV_9340(ah) ? 0x408c : 0x40c8)
#define AR_INTR_PRIO_SYNC_MASK    (AR_SREV_9340(ah) ? 0x4090 : 0x40cc)
#define AR_INTR_PRIO_ASYNC_ENABLE (AR_SREV_9340(ah) ? 0x4094 : 0x40d4)
#define AR_ENT_OTP		  0x40d8
#define AR_ENT_OTP_CHAIN2_DISABLE               0x00020000
#define AR_ENT_OTP_49GHZ_DISABLE		0x00100000
#define AR_ENT_OTP_MIN_PKT_SIZE_DISABLE		0x00800000

#define AR_CH0_BB_DPLL1		 0x16180
#define AR_CH0_BB_DPLL1_REFDIV	 0xF8000000
#define AR_CH0_BB_DPLL1_REFDIV_S 27
#define AR_CH0_BB_DPLL1_NINI	 0x07FC0000
#define AR_CH0_BB_DPLL1_NINI_S	 18
#define AR_CH0_BB_DPLL1_NFRAC	 0x0003FFFF
#define AR_CH0_BB_DPLL1_NFRAC_S	 0

#define AR_CH0_BB_DPLL2		     0x16184
#define AR_CH0_BB_DPLL2_LOCAL_PLL       0x40000000
#define AR_CH0_BB_DPLL2_LOCAL_PLL_S     30
#define AR_CH0_DPLL2_KI              0x3C000000
#define AR_CH0_DPLL2_KI_S            26
#define AR_CH0_DPLL2_KD              0x03F80000
#define AR_CH0_DPLL2_KD_S            19
#define AR_CH0_BB_DPLL2_EN_NEGTRIG   0x00040000
#define AR_CH0_BB_DPLL2_EN_NEGTRIG_S 18
#define AR_CH0_BB_DPLL2_PLL_PWD	     0x00010000
#define AR_CH0_BB_DPLL2_PLL_PWD_S    16
#define AR_CH0_BB_DPLL2_OUTDIV	     0x0000E000
#define AR_CH0_BB_DPLL2_OUTDIV_S     13

#define AR_CH0_BB_DPLL3          0x16188
#define AR_CH0_BB_DPLL3_PHASE_SHIFT	0x3F800000
#define AR_CH0_BB_DPLL3_PHASE_SHIFT_S	23

#define AR_CH0_DDR_DPLL2         0x16244
#define AR_CH0_DDR_DPLL3         0x16248
#define AR_CH0_DPLL3_PHASE_SHIFT     0x3F800000
#define AR_CH0_DPLL3_PHASE_SHIFT_S   23
#define AR_PHY_CCA_NOM_VAL_2GHZ      -118

#define AR_RTC_9300_PLL_DIV          0x000003ff
#define AR_RTC_9300_PLL_DIV_S        0
#define AR_RTC_9300_PLL_REFDIV       0x00003C00
#define AR_RTC_9300_PLL_REFDIV_S     10
#define AR_RTC_9300_PLL_CLKSEL       0x0000C000
#define AR_RTC_9300_PLL_CLKSEL_S     14

#define AR_RTC_9160_PLL_DIV	0x000003ff
#define AR_RTC_9160_PLL_DIV_S   0
#define AR_RTC_9160_PLL_REFDIV  0x00003C00
#define AR_RTC_9160_PLL_REFDIV_S 10
#define AR_RTC_9160_PLL_CLKSEL	0x0000C000
#define AR_RTC_9160_PLL_CLKSEL_S 14

#define AR_RTC_BASE             0x00020000
#define AR_RTC_RC \
	((AR_SREV_9100(ah)) ? (AR_RTC_BASE + 0x0000) : 0x7000)
#define AR_RTC_RC_M		0x00000003
#define AR_RTC_RC_MAC_WARM      0x00000001
#define AR_RTC_RC_MAC_COLD      0x00000002
#define AR_RTC_RC_COLD_RESET    0x00000004
#define AR_RTC_RC_WARM_RESET    0x00000008

/* Crystal Control */
#define AR_RTC_XTAL_CONTROL     0x7004

/* Reg Control 0 */
#define AR_RTC_REG_CONTROL0     0x7008

/* Reg Control 1 */
#define AR_RTC_REG_CONTROL1     0x700c
#define AR_RTC_REG_CONTROL1_SWREG_PROGRAM       0x00000001

#define AR_RTC_PLL_CONTROL \
	((AR_SREV_9100(ah)) ? (AR_RTC_BASE + 0x0014) : 0x7014)

#define AR_RTC_PLL_CONTROL2	0x703c

#define AR_RTC_PLL_DIV          0x0000001f
#define AR_RTC_PLL_DIV_S        0
#define AR_RTC_PLL_DIV2         0x00000020
#define AR_RTC_PLL_REFDIV_5     0x000000c0
#define AR_RTC_PLL_CLKSEL       0x00000300
#define AR_RTC_PLL_CLKSEL_S     8
#define AR_RTC_PLL_BYPASS	0x00010000
#define AR_RTC_PLL_NOPWD	0x00040000
#define AR_RTC_PLL_NOPWD_S	18

#define PLL3 0x16188
#define PLL3_DO_MEAS_MASK 0x40000000
#define PLL4 0x1618c
#define PLL4_MEAS_DONE    0x8
#define SQSUM_DVC_MASK 0x007ffff8

#define AR_RTC_RESET \
	((AR_SREV_9100(ah)) ? (AR_RTC_BASE + 0x0040) : 0x7040)
#define AR_RTC_RESET_EN		(0x00000001)

#define AR_RTC_STATUS \
	((AR_SREV_9100(ah)) ? (AR_RTC_BASE + 0x0044) : 0x7044)

#define AR_RTC_STATUS_M \
	((AR_SREV_9100(ah)) ? 0x0000003f : 0x0000000f)

#define AR_RTC_PM_STATUS_M      0x0000000f

#define AR_RTC_STATUS_SHUTDOWN  0x00000001
#define AR_RTC_STATUS_ON        0x00000002
#define AR_RTC_STATUS_SLEEP     0x00000004
#define AR_RTC_STATUS_WAKEUP    0x00000008

#define AR_RTC_SLEEP_CLK \
	((AR_SREV_9100(ah)) ? (AR_RTC_BASE + 0x0048) : 0x7048)
#define AR_RTC_FORCE_DERIVED_CLK    0x2
#define AR_RTC_FORCE_SWREG_PRD      0x00000004

#define AR_RTC_FORCE_WAKE \
	((AR_SREV_9100(ah)) ? (AR_RTC_BASE + 0x004c) : 0x704c)
#define AR_RTC_FORCE_WAKE_EN        0x00000001
#define AR_RTC_FORCE_WAKE_ON_INT    0x00000002


#define AR_RTC_INTR_CAUSE \
	((AR_SREV_9100(ah)) ? (AR_RTC_BASE + 0x0050) : 0x7050)

#define AR_RTC_INTR_ENABLE \
	((AR_SREV_9100(ah)) ? (AR_RTC_BASE + 0x0054) : 0x7054)

#define AR_RTC_INTR_MASK \
	((AR_SREV_9100(ah)) ? (AR_RTC_BASE + 0x0058) : 0x7058)

#define AR_RTC_KEEP_AWAKE	0x7034

/* RTC_DERIVED_* - only for AR9100 */

#define AR_RTC_DERIVED_CLK \
	(AR_SREV_9100(ah) ? (AR_RTC_BASE + 0x0038) : 0x7038)
#define AR_RTC_DERIVED_CLK_PERIOD    0x0000fffe
#define AR_RTC_DERIVED_CLK_PERIOD_S  1

#define	AR_SEQ_MASK	0x8060

#define AR_AN_RF2G1_CH0         0x7810
#define AR_AN_RF2G1_CH0_OB      0x03800000
#define AR_AN_RF2G1_CH0_OB_S    23
#define AR_AN_RF2G1_CH0_DB      0x1C000000
#define AR_AN_RF2G1_CH0_DB_S    26

#define AR_AN_RF5G1_CH0         0x7818
#define AR_AN_RF5G1_CH0_OB5     0x00070000
#define AR_AN_RF5G1_CH0_OB5_S   16
#define AR_AN_RF5G1_CH0_DB5     0x00380000
#define AR_AN_RF5G1_CH0_DB5_S   19

#define AR_AN_RF2G1_CH1         0x7834
#define AR_AN_RF2G1_CH1_OB      0x03800000
#define AR_AN_RF2G1_CH1_OB_S    23
#define AR_AN_RF2G1_CH1_DB      0x1C000000
#define AR_AN_RF2G1_CH1_DB_S    26

#define AR_AN_RF5G1_CH1         0x783C
#define AR_AN_RF5G1_CH1_OB5     0x00070000
#define AR_AN_RF5G1_CH1_OB5_S   16
#define AR_AN_RF5G1_CH1_DB5     0x00380000
#define AR_AN_RF5G1_CH1_DB5_S   19

#define AR_AN_TOP1                  0x7890
#define AR_AN_TOP1_DACIPMODE	    0x00040000
#define AR_AN_TOP1_DACIPMODE_S	    18

#define AR_AN_TOP2                  0x7894
#define AR_AN_TOP2_XPABIAS_LVL      0xC0000000
#define AR_AN_TOP2_XPABIAS_LVL_S    30
#define AR_AN_TOP2_LOCALBIAS        0x00200000
#define AR_AN_TOP2_LOCALBIAS_S      21
#define AR_AN_TOP2_PWDCLKIND        0x00400000
#define AR_AN_TOP2_PWDCLKIND_S      22

#define AR_AN_SYNTH9            0x7868
#define AR_AN_SYNTH9_REFDIVA    0xf8000000
#define AR_AN_SYNTH9_REFDIVA_S  27

#define AR9285_AN_RF2G1              0x7820
#define AR9285_AN_RF2G1_ENPACAL      0x00000800
#define AR9285_AN_RF2G1_ENPACAL_S    11
#define AR9285_AN_RF2G1_PDPADRV1     0x02000000
#define AR9285_AN_RF2G1_PDPADRV1_S   25
#define AR9285_AN_RF2G1_PDPADRV2     0x01000000
#define AR9285_AN_RF2G1_PDPADRV2_S   24
#define AR9285_AN_RF2G1_PDPAOUT      0x00800000
#define AR9285_AN_RF2G1_PDPAOUT_S    23


#define AR9285_AN_RF2G2              0x7824
#define AR9285_AN_RF2G2_OFFCAL       0x00001000
#define AR9285_AN_RF2G2_OFFCAL_S     12

#define AR9285_AN_RF2G3             0x7828
#define AR9285_AN_RF2G3_PDVCCOMP    0x02000000
#define AR9285_AN_RF2G3_PDVCCOMP_S  25
#define AR9285_AN_RF2G3_OB_0    0x00E00000
#define AR9285_AN_RF2G3_OB_0_S    21
#define AR9285_AN_RF2G3_OB_1    0x001C0000
#define AR9285_AN_RF2G3_OB_1_S    18
#define AR9285_AN_RF2G3_OB_2    0x00038000
#define AR9285_AN_RF2G3_OB_2_S    15
#define AR9285_AN_RF2G3_OB_3    0x00007000
#define AR9285_AN_RF2G3_OB_3_S    12
#define AR9285_AN_RF2G3_OB_4    0x00000E00
#define AR9285_AN_RF2G3_OB_4_S    9

#define AR9285_AN_RF2G3_DB1_0    0x000001C0
#define AR9285_AN_RF2G3_DB1_0_S    6
#define AR9285_AN_RF2G3_DB1_1    0x00000038
#define AR9285_AN_RF2G3_DB1_1_S    3
#define AR9285_AN_RF2G3_DB1_2    0x00000007
#define AR9285_AN_RF2G3_DB1_2_S    0
#define AR9285_AN_RF2G4         0x782C
#define AR9285_AN_RF2G4_DB1_3    0xE0000000
#define AR9285_AN_RF2G4_DB1_3_S    29
#define AR9285_AN_RF2G4_DB1_4    0x1C000000
#define AR9285_AN_RF2G4_DB1_4_S    26

#define AR9285_AN_RF2G4_DB2_0    0x03800000
#define AR9285_AN_RF2G4_DB2_0_S    23
#define AR9285_AN_RF2G4_DB2_1    0x00700000
#define AR9285_AN_RF2G4_DB2_1_S    20
#define AR9285_AN_RF2G4_DB2_2    0x000E0000
#define AR9285_AN_RF2G4_DB2_2_S    17
#define AR9285_AN_RF2G4_DB2_3    0x0001C000
#define AR9285_AN_RF2G4_DB2_3_S    14
#define AR9285_AN_RF2G4_DB2_4    0x00003800
#define AR9285_AN_RF2G4_DB2_4_S    11

#define AR9285_RF2G5			0x7830
#define AR9285_RF2G5_IC50TX		0xfffff8ff
#define AR9285_RF2G5_IC50TX_SET		0x00000400
#define AR9285_RF2G5_IC50TX_XE_SET	0x00000500
#define AR9285_RF2G5_IC50TX_CLEAR	0x00000700
#define AR9285_RF2G5_IC50TX_CLEAR_S	8

/* AR9271 : 0x7828, 0x782c different setting from AR9285 */
#define AR9271_AN_RF2G3_OB_cck		0x001C0000
#define AR9271_AN_RF2G3_OB_cck_S	18
#define AR9271_AN_RF2G3_OB_psk		0x00038000
#define AR9271_AN_RF2G3_OB_psk_S	15
#define AR9271_AN_RF2G3_OB_qam		0x00007000
#define AR9271_AN_RF2G3_OB_qam_S	12

#define AR9271_AN_RF2G3_DB_1		0x00E00000
#define AR9271_AN_RF2G3_DB_1_S		21

#define AR9271_AN_RF2G3_CCOMP		0xFFF
#define AR9271_AN_RF2G3_CCOMP_S		0

#define AR9271_AN_RF2G4_DB_2		0xE0000000
#define AR9271_AN_RF2G4_DB_2_S		29

#define AR9285_AN_RF2G6                 0x7834
#define AR9285_AN_RF2G6_CCOMP           0x00007800
#define AR9285_AN_RF2G6_CCOMP_S         11
#define AR9285_AN_RF2G6_OFFS            0x03f00000
#define AR9285_AN_RF2G6_OFFS_S          20

#define AR9271_AN_RF2G6_OFFS            0x07f00000
#define AR9271_AN_RF2G6_OFFS_S            20

#define AR9285_AN_RF2G7                 0x7838
#define AR9285_AN_RF2G7_PWDDB           0x00000002
#define AR9285_AN_RF2G7_PWDDB_S         1
#define AR9285_AN_RF2G7_PADRVGN2TAB0    0xE0000000
#define AR9285_AN_RF2G7_PADRVGN2TAB0_S  29

#define AR9285_AN_RF2G8                  0x783C
#define AR9285_AN_RF2G8_PADRVGN2TAB0     0x0001C000
#define AR9285_AN_RF2G8_PADRVGN2TAB0_S   14


#define AR9285_AN_RF2G9          0x7840
#define AR9285_AN_RXTXBB1              0x7854
#define AR9285_AN_RXTXBB1_PDRXTXBB1    0x00000020
#define AR9285_AN_RXTXBB1_PDRXTXBB1_S  5
#define AR9285_AN_RXTXBB1_PDV2I        0x00000080
#define AR9285_AN_RXTXBB1_PDV2I_S      7
#define AR9285_AN_RXTXBB1_PDDACIF      0x00000100
#define AR9285_AN_RXTXBB1_PDDACIF_S    8
#define AR9285_AN_RXTXBB1_SPARE9       0x00000001
#define AR9285_AN_RXTXBB1_SPARE9_S     0

#define AR9285_AN_TOP2           0x7868

#define AR9285_AN_TOP3                  0x786c
#define AR9285_AN_TOP3_XPABIAS_LVL      0x0000000C
#define AR9285_AN_TOP3_XPABIAS_LVL_S    2
#define AR9285_AN_TOP3_PWDDAC           0x00800000
#define AR9285_AN_TOP3_PWDDAC_S    23

#define AR9285_AN_TOP4           0x7870
#define AR9285_AN_TOP4_DEFAULT   0x10142c00

#define AR9287_AN_RF2G3_CH0             0x7808
#define AR9287_AN_RF2G3_CH1             0x785c
#define AR9287_AN_RF2G3_DB1             0xE0000000
#define AR9287_AN_RF2G3_DB1_S           29
#define AR9287_AN_RF2G3_DB2             0x1C000000
#define AR9287_AN_RF2G3_DB2_S           26
#define AR9287_AN_RF2G3_OB_CCK          0x03800000
#define AR9287_AN_RF2G3_OB_CCK_S        23
#define AR9287_AN_RF2G3_OB_PSK          0x00700000
#define AR9287_AN_RF2G3_OB_PSK_S        20
#define AR9287_AN_RF2G3_OB_QAM          0x000E0000
#define AR9287_AN_RF2G3_OB_QAM_S        17
#define AR9287_AN_RF2G3_OB_PAL_OFF      0x0001C000
#define AR9287_AN_RF2G3_OB_PAL_OFF_S    14

#define AR9287_AN_TXPC0                 0x7898
#define AR9287_AN_TXPC0_TXPCMODE        0x0000C000
#define AR9287_AN_TXPC0_TXPCMODE_S      14
#define AR9287_AN_TXPC0_TXPCMODE_NORMAL    0
#define AR9287_AN_TXPC0_TXPCMODE_TEST      1
#define AR9287_AN_TXPC0_TXPCMODE_TEMPSENSE 2
#define AR9287_AN_TXPC0_TXPCMODE_ATBTEST   3

#define AR9287_AN_TOP2                  0x78b4
#define AR9287_AN_TOP2_XPABIAS_LVL      0xC0000000
#define AR9287_AN_TOP2_XPABIAS_LVL_S    30

/* AR9271 specific stuff */
#define AR9271_RESET_POWER_DOWN_CONTROL		0x50044
#define AR9271_RADIO_RF_RST			0x20
#define AR9271_GATE_MAC_CTL			0x4000

#define AR_STA_ID0                 0x8000
#define AR_STA_ID1                 0x8004
#define AR_STA_ID1_SADH_MASK       0x0000FFFF
#define AR_STA_ID1_STA_AP          0x00010000
#define AR_STA_ID1_ADHOC           0x00020000
#define AR_STA_ID1_PWR_SAV         0x00040000
#define AR_STA_ID1_KSRCHDIS        0x00080000
#define AR_STA_ID1_PCF             0x00100000
#define AR_STA_ID1_USE_DEFANT      0x00200000
#define AR_STA_ID1_DEFANT_UPDATE   0x00400000
#define AR_STA_ID1_AR9100_BA_FIX   0x00400000
#define AR_STA_ID1_RTS_USE_DEF     0x00800000
#define AR_STA_ID1_ACKCTS_6MB      0x01000000
#define AR_STA_ID1_BASE_RATE_11B   0x02000000
#define AR_STA_ID1_SECTOR_SELF_GEN 0x04000000
#define AR_STA_ID1_CRPT_MIC_ENABLE 0x08000000
#define AR_STA_ID1_KSRCH_MODE      0x10000000
#define AR_STA_ID1_PRESERVE_SEQNUM 0x20000000
#define AR_STA_ID1_CBCIV_ENDIAN    0x40000000
#define AR_STA_ID1_MCAST_KSRCH     0x80000000

#define AR_BSS_ID0          0x8008
#define AR_BSS_ID1          0x800C
#define AR_BSS_ID1_U16       0x0000FFFF
#define AR_BSS_ID1_AID       0x07FF0000
#define AR_BSS_ID1_AID_S     16

#define AR_BCN_RSSI_AVE      0x8010
#define AR_BCN_RSSI_AVE_MASK 0x00000FFF

#define AR_TIME_OUT         0x8014
#define AR_TIME_OUT_ACK      0x00003FFF
#define AR_TIME_OUT_ACK_S    0
#define AR_TIME_OUT_CTS      0x3FFF0000
#define AR_TIME_OUT_CTS_S    16

#define AR_RSSI_THR          0x8018
#define AR_RSSI_THR_MASK     0x000000FF
#define AR_RSSI_THR_BM_THR   0x0000FF00
#define AR_RSSI_THR_BM_THR_S 8
#define AR_RSSI_BCN_WEIGHT   0x1F000000
#define AR_RSSI_BCN_WEIGHT_S 24
#define AR_RSSI_BCN_RSSI_RST 0x20000000

#define AR_USEC              0x801c
#define AR_USEC_USEC         0x0000007F
#define AR_USEC_TX_LAT       0x007FC000
#define AR_USEC_TX_LAT_S     14
#define AR_USEC_RX_LAT       0x1F800000
#define AR_USEC_RX_LAT_S     23
#define AR_USEC_ASYNC_FIFO   0x12E00074

#define AR_RESET_TSF        0x8020
#define AR_RESET_TSF_ONCE   0x01000000

#define AR_MAX_CFP_DUR      0x8038
#define AR_CFP_VAL          0x0000FFFF

#define AR_RX_FILTER        0x803C

#define AR_MCAST_FIL0       0x8040
#define AR_MCAST_FIL1       0x8044

/*
 * AR_DIAG_SW - Register which can be used for diagnostics and testing purposes.
 *
 * The force RX abort (AR_DIAG_RX_ABORT, bit 25) can be used in conjunction with
 * RX block (AR_DIAG_RX_DIS, bit 5) to help fast channel change to shut down
 * receive. The force RX abort bit will kill any frame which is currently being
 * transferred between the MAC and baseband. The RX block bit (AR_DIAG_RX_DIS)
 * will prevent any new frames from getting started.
 */
#define AR_DIAG_SW                  0x8048
#define AR_DIAG_CACHE_ACK           0x00000001
#define AR_DIAG_ACK_DIS             0x00000002
#define AR_DIAG_CTS_DIS             0x00000004
#define AR_DIAG_ENCRYPT_DIS         0x00000008
#define AR_DIAG_DECRYPT_DIS         0x00000010
#define AR_DIAG_RX_DIS              0x00000020 /* RX block */
#define AR_DIAG_LOOP_BACK           0x00000040
#define AR_DIAG_CORR_FCS            0x00000080
#define AR_DIAG_CHAN_INFO           0x00000100
#define AR_DIAG_SCRAM_SEED          0x0001FE00
#define AR_DIAG_SCRAM_SEED_S        8
#define AR_DIAG_FRAME_NV0           0x00020000
#define AR_DIAG_OBS_PT_SEL1         0x000C0000
#define AR_DIAG_OBS_PT_SEL1_S       18
#define AR_DIAG_OBS_PT_SEL2         0x08000000
#define AR_DIAG_OBS_PT_SEL2_S       27
#define AR_DIAG_FORCE_RX_CLEAR      0x00100000 /* force rx_clear high */
#define AR_DIAG_IGNORE_VIRT_CS      0x00200000
#define AR_DIAG_FORCE_CH_IDLE_HIGH  0x00400000
#define AR_DIAG_EIFS_CTRL_ENA       0x00800000
#define AR_DIAG_DUAL_CHAIN_INFO     0x01000000
#define AR_DIAG_RX_ABORT            0x02000000 /* Force RX abort */
#define AR_DIAG_SATURATE_CYCLE_CNT  0x04000000
#define AR_DIAG_OBS_PT_SEL2         0x08000000
#define AR_DIAG_RX_CLEAR_CTL_LOW    0x10000000
#define AR_DIAG_RX_CLEAR_EXT_LOW    0x20000000

#define AR_TSF_L32          0x804c
#define AR_TSF_U32          0x8050

#define AR_TST_ADDAC        0x8054
#define AR_DEF_ANTENNA      0x8058

#define AR_AES_MUTE_MASK0       0x805c
#define AR_AES_MUTE_MASK0_FC    0x0000FFFF
#define AR_AES_MUTE_MASK0_QOS   0xFFFF0000
#define AR_AES_MUTE_MASK0_QOS_S 16

#define AR_AES_MUTE_MASK1       0x8060
#define AR_AES_MUTE_MASK1_SEQ   0x0000FFFF
#define AR_AES_MUTE_MASK1_FC_MGMT 0xFFFF0000
#define AR_AES_MUTE_MASK1_FC_MGMT_S 16

#define AR_GATED_CLKS       0x8064
#define AR_GATED_CLKS_TX    0x00000002
#define AR_GATED_CLKS_RX    0x00000004
#define AR_GATED_CLKS_REG   0x00000008

#define AR_OBS_BUS_CTRL     0x8068
#define AR_OBS_BUS_SEL_1    0x00040000
#define AR_OBS_BUS_SEL_2    0x00080000
#define AR_OBS_BUS_SEL_3    0x000C0000
#define AR_OBS_BUS_SEL_4    0x08040000
#define AR_OBS_BUS_SEL_5    0x08080000

#define AR_OBS_BUS_1               0x806c
#define AR_OBS_BUS_1_PCU           0x00000001
#define AR_OBS_BUS_1_RX_END        0x00000002
#define AR_OBS_BUS_1_RX_WEP        0x00000004
#define AR_OBS_BUS_1_RX_BEACON     0x00000008
#define AR_OBS_BUS_1_RX_FILTER     0x00000010
#define AR_OBS_BUS_1_TX_HCF        0x00000020
#define AR_OBS_BUS_1_QUIET_TIME    0x00000040
#define AR_OBS_BUS_1_CHAN_IDLE     0x00000080
#define AR_OBS_BUS_1_TX_HOLD       0x00000100
#define AR_OBS_BUS_1_TX_FRAME      0x00000200
#define AR_OBS_BUS_1_RX_FRAME      0x00000400
#define AR_OBS_BUS_1_RX_CLEAR      0x00000800
#define AR_OBS_BUS_1_WEP_STATE     0x0003F000
#define AR_OBS_BUS_1_WEP_STATE_S   12
#define AR_OBS_BUS_1_RX_STATE      0x01F00000
#define AR_OBS_BUS_1_RX_STATE_S    20
#define AR_OBS_BUS_1_TX_STATE      0x7E000000
#define AR_OBS_BUS_1_TX_STATE_S    25

#define AR_LAST_TSTP        0x8080
#define AR_NAV              0x8084
#define AR_RTS_OK           0x8088
#define AR_RTS_FAIL         0x808c
#define AR_ACK_FAIL         0x8090
#define AR_FCS_FAIL         0x8094
#define AR_BEACON_CNT       0x8098

#define AR_SLEEP1               0x80d4
#define AR_SLEEP1_ASSUME_DTIM   0x00080000
#define AR_SLEEP1_CAB_TIMEOUT   0xFFE00000
#define AR_SLEEP1_CAB_TIMEOUT_S 21

#define AR_SLEEP2                   0x80d8
#define AR_SLEEP2_BEACON_TIMEOUT    0xFFE00000
#define AR_SLEEP2_BEACON_TIMEOUT_S  21

#define AR_TPC                 0x80e8
#define AR_TPC_ACK             0x0000003f
#define AR_TPC_ACK_S           0
#define AR_TPC_CTS             0x00003f00
#define AR_TPC_CTS_S           8
#define AR_TPC_CHIRP           0x003f0000
#define AR_TPC_CHIRP_S         16

#define AR_QUIET1          0x80fc
#define AR_QUIET1_NEXT_QUIET_S         0
#define AR_QUIET1_NEXT_QUIET_M         0x0000ffff
#define AR_QUIET1_QUIET_ENABLE         0x00010000
#define AR_QUIET1_QUIET_ACK_CTS_ENABLE 0x00020000
#define AR_QUIET1_QUIET_ACK_CTS_ENABLE_S 17
#define AR_QUIET2          0x8100
#define AR_QUIET2_QUIET_PERIOD_S       0
#define AR_QUIET2_QUIET_PERIOD_M       0x0000ffff
#define AR_QUIET2_QUIET_DUR_S     16
#define AR_QUIET2_QUIET_DUR       0xffff0000

#define AR_TSF_PARM        0x8104
#define AR_TSF_INCREMENT_M     0x000000ff
#define AR_TSF_INCREMENT_S     0x00

#define AR_QOS_NO_ACK              0x8108
#define AR_QOS_NO_ACK_TWO_BIT      0x0000000f
#define AR_QOS_NO_ACK_TWO_BIT_S    0
#define AR_QOS_NO_ACK_BIT_OFF      0x00000070
#define AR_QOS_NO_ACK_BIT_OFF_S    4
#define AR_QOS_NO_ACK_BYTE_OFF     0x00000180
#define AR_QOS_NO_ACK_BYTE_OFF_S   7

#define AR_PHY_ERR         0x810c

#define AR_PHY_ERR_DCHIRP      0x00000008
#define AR_PHY_ERR_RADAR       0x00000020
#define AR_PHY_ERR_OFDM_TIMING 0x00020000
#define AR_PHY_ERR_CCK_TIMING  0x02000000

#define AR_RXFIFO_CFG          0x8114


#define AR_MIC_QOS_CONTROL 0x8118
#define AR_MIC_QOS_SELECT  0x811c

#define AR_PCU_MISC                0x8120
#define AR_PCU_FORCE_BSSID_MATCH   0x00000001
#define AR_PCU_MIC_NEW_LOC_ENA     0x00000004
#define AR_PCU_TX_ADD_TSF          0x00000008
#define AR_PCU_CCK_SIFS_MODE       0x00000010
#define AR_PCU_RX_ANT_UPDT         0x00000800
#define AR_PCU_TXOP_TBTT_LIMIT_ENA 0x00001000
#define AR_PCU_MISS_BCN_IN_SLEEP   0x00004000
#define AR_PCU_BUG_12306_FIX_ENA   0x00020000
#define AR_PCU_FORCE_QUIET_COLL    0x00040000
#define AR_PCU_TBTT_PROTECT        0x00200000
#define AR_PCU_CLEAR_VMF           0x01000000
#define AR_PCU_CLEAR_BA_VALID      0x04000000
#define AR_PCU_ALWAYS_PERFORM_KEYSEARCH 0x10000000

#define AR_PCU_BT_ANT_PREVENT_RX   0x00100000
#define AR_PCU_BT_ANT_PREVENT_RX_S 20

#define AR_FILT_OFDM           0x8124
#define AR_FILT_OFDM_COUNT     0x00FFFFFF

#define AR_FILT_CCK            0x8128
#define AR_FILT_CCK_COUNT      0x00FFFFFF

#define AR_PHY_ERR_1           0x812c
#define AR_PHY_ERR_1_COUNT     0x00FFFFFF
#define AR_PHY_ERR_MASK_1      0x8130

#define AR_PHY_ERR_2           0x8134
#define AR_PHY_ERR_2_COUNT     0x00FFFFFF
#define AR_PHY_ERR_MASK_2      0x8138

#define AR_PHY_COUNTMAX        (3 << 22)
#define AR_MIBCNT_INTRMASK     (3 << 22)

#define AR_TSFOOR_THRESHOLD       0x813c
#define AR_TSFOOR_THRESHOLD_VAL   0x0000FFFF

#define AR_PHY_ERR_EIFS_MASK   0x8144

#define AR_PHY_ERR_3           0x8168
#define AR_PHY_ERR_3_COUNT     0x00FFFFFF
#define AR_PHY_ERR_MASK_3      0x816c

#define AR_BT_COEX_MODE            0x8170
#define AR_BT_TIME_EXTEND          0x000000ff
#define AR_BT_TIME_EXTEND_S        0
#define AR_BT_TXSTATE_EXTEND       0x00000100
#define AR_BT_TXSTATE_EXTEND_S     8
#define AR_BT_TX_FRAME_EXTEND      0x00000200
#define AR_BT_TX_FRAME_EXTEND_S    9
#define AR_BT_MODE                 0x00000c00
#define AR_BT_MODE_S               10
#define AR_BT_QUIET                0x00001000
#define AR_BT_QUIET_S              12
#define AR_BT_QCU_THRESH           0x0001e000
#define AR_BT_QCU_THRESH_S         13
#define AR_BT_RX_CLEAR_POLARITY    0x00020000
#define AR_BT_RX_CLEAR_POLARITY_S  17
#define AR_BT_PRIORITY_TIME        0x00fc0000
#define AR_BT_PRIORITY_TIME_S      18
#define AR_BT_FIRST_SLOT_TIME      0xff000000
#define AR_BT_FIRST_SLOT_TIME_S    24

#define AR_BT_COEX_WEIGHT          0x8174
#define AR_BT_COEX_WGHT		   0xff55
#define AR_STOMP_ALL_WLAN_WGHT	   0xfcfc
#define AR_STOMP_LOW_WLAN_WGHT	   0xa8a8
#define AR_STOMP_NONE_WLAN_WGHT	   0x0000
#define AR_BTCOEX_BT_WGHT          0x0000ffff
#define AR_BTCOEX_BT_WGHT_S        0
#define AR_BTCOEX_WL_WGHT          0xffff0000
#define AR_BTCOEX_WL_WGHT_S        16

#define AR_BT_COEX_WL_WEIGHTS0     0x8174
#define AR_BT_COEX_WL_WEIGHTS1     0x81c4
#define AR_MCI_COEX_WL_WEIGHTS(_i) (0x18b0 + (_i << 2))
#define AR_BT_COEX_BT_WEIGHTS(_i)  (0x83ac + (_i << 2))

#define AR9300_BT_WGHT             0xcccc4444

#define AR_BT_COEX_MODE2           0x817c
#define AR_BT_BCN_MISS_THRESH      0x000000ff
#define AR_BT_BCN_MISS_THRESH_S    0
#define AR_BT_BCN_MISS_CNT         0x0000ff00
#define AR_BT_BCN_MISS_CNT_S       8
#define AR_BT_HOLD_RX_CLEAR        0x00010000
#define AR_BT_HOLD_RX_CLEAR_S      16
#define AR_BT_DISABLE_BT_ANT       0x00100000
#define AR_BT_DISABLE_BT_ANT_S     20

#define AR_TXSIFS              0x81d0
#define AR_TXSIFS_TIME         0x000000FF
#define AR_TXSIFS_TX_LATENCY   0x00000F00
#define AR_TXSIFS_TX_LATENCY_S 8
#define AR_TXSIFS_ACK_SHIFT    0x00007000
#define AR_TXSIFS_ACK_SHIFT_S  12

#define AR_TXOP_X          0x81ec
#define AR_TXOP_X_VAL      0x000000FF


#define AR_TXOP_0_3    0x81f0
#define AR_TXOP_4_7    0x81f4
#define AR_TXOP_8_11   0x81f8
#define AR_TXOP_12_15  0x81fc

#define AR_NEXT_NDP2_TIMER                  0x8180
#define AR_GEN_TIMER_BANK_1_LEN			8
#define AR_FIRST_NDP_TIMER                  7
#define AR_NDP2_PERIOD                      0x81a0
#define AR_NDP2_TIMER_MODE                  0x81c0

#define AR_GEN_TIMERS(_i)                   (0x8200 + ((_i) << 2))
#define AR_NEXT_TBTT_TIMER                  AR_GEN_TIMERS(0)
#define AR_NEXT_DMA_BEACON_ALERT            AR_GEN_TIMERS(1)
#define AR_NEXT_SWBA                        AR_GEN_TIMERS(2)
#define AR_NEXT_CFP                         AR_GEN_TIMERS(2)
#define AR_NEXT_HCF                         AR_GEN_TIMERS(3)
#define AR_NEXT_TIM                         AR_GEN_TIMERS(4)
#define AR_NEXT_DTIM                        AR_GEN_TIMERS(5)
#define AR_NEXT_QUIET_TIMER                 AR_GEN_TIMERS(6)
#define AR_NEXT_NDP_TIMER                   AR_GEN_TIMERS(7)

#define AR_BEACON_PERIOD                    AR_GEN_TIMERS(8)
#define AR_DMA_BEACON_PERIOD                AR_GEN_TIMERS(9)
#define AR_SWBA_PERIOD                      AR_GEN_TIMERS(10)
#define AR_HCF_PERIOD                       AR_GEN_TIMERS(11)
#define AR_TIM_PERIOD                       AR_GEN_TIMERS(12)
#define AR_DTIM_PERIOD                      AR_GEN_TIMERS(13)
#define AR_QUIET_PERIOD                     AR_GEN_TIMERS(14)
#define AR_NDP_PERIOD                       AR_GEN_TIMERS(15)

#define AR_TIMER_MODE                       0x8240
#define AR_TBTT_TIMER_EN                    0x00000001
#define AR_DBA_TIMER_EN                     0x00000002
#define AR_SWBA_TIMER_EN                    0x00000004
#define AR_HCF_TIMER_EN                     0x00000008
#define AR_TIM_TIMER_EN                     0x00000010
#define AR_DTIM_TIMER_EN                    0x00000020
#define AR_QUIET_TIMER_EN                   0x00000040
#define AR_NDP_TIMER_EN                     0x00000080
#define AR_TIMER_OVERFLOW_INDEX             0x00000700
#define AR_TIMER_OVERFLOW_INDEX_S           8
#define AR_TIMER_THRESH                     0xFFFFF000
#define AR_TIMER_THRESH_S                   12

#define AR_SLP32_MODE                  0x8244
#define AR_SLP32_HALF_CLK_LATENCY      0x000FFFFF
#define AR_SLP32_ENA                   0x00100000
#define AR_SLP32_TSF_WRITE_STATUS      0x00200000

#define AR_SLP32_WAKE              0x8248
#define AR_SLP32_WAKE_XTL_TIME     0x0000FFFF

#define AR_SLP32_INC               0x824c
#define AR_SLP32_TST_INC           0x000FFFFF

#define AR_SLP_CNT         0x8250
#define AR_SLP_CYCLE_CNT   0x8254

#define AR_SLP_MIB_CTRL    0x8258
#define AR_SLP_MIB_CLEAR   0x00000001
#define AR_SLP_MIB_PENDING 0x00000002

#define AR_MAC_PCU_LOGIC_ANALYZER               0x8264
#define AR_MAC_PCU_LOGIC_ANALYZER_DISBUG20768   0x20000000


#define AR_2040_MODE                0x8318
#define AR_2040_JOINED_RX_CLEAR 0x00000001


#define AR_EXTRCCNT         0x8328

#define AR_SELFGEN_MASK         0x832c

#define AR_PCU_TXBUF_CTRL               0x8340
#define AR_PCU_TXBUF_CTRL_SIZE_MASK     0x7FF
#define AR_PCU_TXBUF_CTRL_USABLE_SIZE   0x700
#define AR_9285_PCU_TXBUF_CTRL_USABLE_SIZE   0x380

#define AR_PCU_MISC_MODE2               0x8344
#define AR_PCU_MISC_MODE2_MGMT_CRYPTO_ENABLE           0x00000002
#define AR_PCU_MISC_MODE2_NO_CRYPTO_FOR_NON_DATA_PKT   0x00000004

#define AR_PCU_MISC_MODE2_RESERVED                     0x00000038
#define AR_PCU_MISC_MODE2_ADHOC_MCAST_KEYID_ENABLE     0x00000040
#define AR_PCU_MISC_MODE2_CFP_IGNORE                   0x00000080
#define AR_PCU_MISC_MODE2_MGMT_QOS                     0x0000FF00
#define AR_PCU_MISC_MODE2_MGMT_QOS_S                   8
#define AR_PCU_MISC_MODE2_ENABLE_LOAD_NAV_BEACON_DURATION 0x00010000
#define AR_PCU_MISC_MODE2_ENABLE_AGGWEP                0x00020000
#define AR_PCU_MISC_MODE2_HWWAR1                       0x00100000
#define AR_PCU_MISC_MODE2_HWWAR2                       0x02000000
#define AR_PCU_MISC_MODE2_RESERVED2                    0xFFFE0000

#define AR_PCU_MISC_MODE3			       0x83d0

#define AR_MAC_PCU_ASYNC_FIFO_REG3			0x8358
#define AR_MAC_PCU_ASYNC_FIFO_REG3_DATAPATH_SEL		0x00000400
#define AR_MAC_PCU_ASYNC_FIFO_REG3_SOFT_RESET		0x80000000
#define AR_MAC_PCU_GEN_TIMER_TSF_SEL			0x83d8


#define AR_AES_MUTE_MASK0       0x805c
#define AR_AES_MUTE_MASK0_FC    0x0000FFFF
#define AR_AES_MUTE_MASK0_QOS   0xFFFF0000
#define AR_AES_MUTE_MASK0_QOS_S 16

#define AR_AES_MUTE_MASK1              0x8060
#define AR_AES_MUTE_MASK1_SEQ          0x0000FFFF
#define AR_AES_MUTE_MASK1_SEQ_S        0
#define AR_AES_MUTE_MASK1_FC_MGMT      0xFFFF0000
#define AR_AES_MUTE_MASK1_FC_MGMT_S    16

#define AR_RATE_DURATION_0      0x8700
#define AR_RATE_DURATION_31     0x87CC
#define AR_RATE_DURATION_32     0x8780
#define AR_RATE_DURATION(_n)    (AR_RATE_DURATION_0 + ((_n)<<2))

/* WoW - Wake On Wireless */

#define AR_PMCTRL_AUX_PWR_DET		0x10000000 /* Puts Chip in L2 state */
#define AR_PMCTRL_D3COLD_VAUX		0x00800000
#define AR_PMCTRL_HOST_PME_EN		0x00400000 /* Send OOB WAKE_L on WoW
						      event */
#define AR_PMCTRL_WOW_PME_CLR		0x00200000 /* Clear WoW event */
#define AR_PMCTRL_PWR_STATE_MASK	0x0f000000 /* Power State Mask */
#define AR_PMCTRL_PWR_STATE_D1D3	0x0f000000 /* Activate D1 and D3 */
#define AR_PMCTRL_PWR_STATE_D1D3_REAL	0x0f000000 /* Activate D1 and D3 */
#define AR_PMCTRL_PWR_STATE_D0		0x08000000 /* Activate D0 */
#define AR_PMCTRL_PWR_PM_CTRL_ENA	0x00008000 /* Enable power mgmt */

#define AR_WOW_BEACON_TIMO_MAX		0xffffffff

/*
 * MAC WoW Registers
 */

#define AR_WOW_PATTERN			0x825C
#define AR_WOW_COUNT			0x8260
#define AR_WOW_BCN_EN			0x8270
#define AR_WOW_BCN_TIMO			0x8274
#define AR_WOW_KEEP_ALIVE_TIMO		0x8278
#define AR_WOW_KEEP_ALIVE		0x827c
#define AR_WOW_US_SCALAR		0x8284
#define AR_WOW_KEEP_ALIVE_DELAY		0x8288
#define AR_WOW_PATTERN_MATCH		0x828c
#define AR_WOW_PATTERN_OFF1		0x8290	/* pattern bytes 0 -> 3 */
#define AR_WOW_PATTERN_OFF2		0x8294	/* pattern bytes 4 -> 7 */

/* for AR9285 or later version of chips */
#define AR_WOW_EXACT			0x829c
#define AR_WOW_LENGTH1			0x8360
#define AR_WOW_LENGTH2			0X8364
/* register to enable match for less than 256 bytes packets */
#define AR_WOW_PATTERN_MATCH_LT_256B	0x8368

#define AR_SW_WOW_CONTROL		0x20018
#define AR_SW_WOW_ENABLE		0x1
#define AR_SWITCH_TO_REFCLK		0x2
#define AR_RESET_CONTROL		0x4
#define AR_RESET_VALUE_MASK		0x8
#define AR_HW_WOW_DISABLE		0x10
#define AR_CLR_MAC_INTERRUPT		0x20
#define AR_CLR_KA_INTERRUPT		0x40

/* AR_WOW_PATTERN register values */
#define AR_WOW_BACK_OFF_SHIFT(x)	((x & 0xf) << 28) /* in usecs */
#define AR_WOW_MAC_INTR_EN		0x00040000
#define AR_WOW_MAGIC_EN			0x00010000
#define AR_WOW_PATTERN_EN(x)		(x & 0xff)
#define AR_WOW_PAT_FOUND_SHIFT	8
#define AR_WOW_PATTERN_FOUND(x)		(x & (0xff << AR_WOW_PAT_FOUND_SHIFT))
#define AR_WOW_PATTERN_FOUND_MASK	((0xff) << AR_WOW_PAT_FOUND_SHIFT)
#define AR_WOW_MAGIC_PAT_FOUND		0x00020000
#define AR_WOW_MAC_INTR			0x00080000
#define AR_WOW_KEEP_ALIVE_FAIL		0x00100000
#define AR_WOW_BEACON_FAIL		0x00200000

#define AR_WOW_STATUS(x)		(x & (AR_WOW_PATTERN_FOUND_MASK | \
					      AR_WOW_MAGIC_PAT_FOUND	| \
					      AR_WOW_KEEP_ALIVE_FAIL	| \
					      AR_WOW_BEACON_FAIL))
#define AR_WOW_CLEAR_EVENTS(x)		(x & ~(AR_WOW_PATTERN_EN(0xff) | \
					       AR_WOW_MAGIC_EN | \
					       AR_WOW_MAC_INTR_EN | \
					       AR_WOW_BEACON_FAIL | \
					       AR_WOW_KEEP_ALIVE_FAIL))

/* AR_WOW_COUNT register values */
#define AR_WOW_AIFS_CNT(x)		(x & 0xff)
#define AR_WOW_SLOT_CNT(x)		((x & 0xff) << 8)
#define AR_WOW_KEEP_ALIVE_CNT(x)	((x & 0xff) << 16)

/* AR_WOW_BCN_EN register */
#define AR_WOW_BEACON_FAIL_EN		0x00000001

/* AR_WOW_BCN_TIMO rgister */
#define AR_WOW_BEACON_TIMO		0x40000000 /* valid if BCN_EN is set */

/* AR_WOW_KEEP_ALIVE_TIMO register */
#define AR_WOW_KEEP_ALIVE_TIMO_VALUE
#define AR_WOW_KEEP_ALIVE_NEVER		0xffffffff

/* AR_WOW_KEEP_ALIVE register  */
#define AR_WOW_KEEP_ALIVE_AUTO_DIS	0x00000001
#define AR_WOW_KEEP_ALIVE_FAIL_DIS	0x00000002

/* AR_WOW_KEEP_ALIVE_DELAY register */
#define AR_WOW_KEEP_ALIVE_DELAY_VALUE	0x000003e8 /* 1 msec */


/*
 * keep it long for beacon workaround - ensure no false alarm
 */
#define AR_WOW_BMISSTHRESHOLD		0x20

/* AR_WOW_PATTERN_MATCH register */
#define AR_WOW_PAT_END_OF_PKT(x)	(x & 0xf)
#define AR_WOW_PAT_OFF_MATCH(x)		((x & 0xf) << 8)

/*
 * default values for Wow Configuration for backoff, aifs, slot, keep-alive
 * to be programmed into various registers.
 */
#define AR_WOW_PAT_BACKOFF	0x00000004 /* AR_WOW_PATTERN_REG */
#define AR_WOW_CNT_AIFS_CNT	0x00000022 /* AR_WOW_COUNT_REG */
#define AR_WOW_CNT_SLOT_CNT	0x00000009 /* AR_WOW_COUNT_REG */
/*
 * Keepalive count applicable for AR9280 2.0 and above.
 */
#define AR_WOW_CNT_KA_CNT 0x00000008    /* AR_WOW_COUNT register */

/* WoW - Transmit buffer for keep alive frames */
#define AR_WOW_TRANSMIT_BUFFER	0xe000 /* E000 - EFFC */

#define AR_WOW_TXBUF(i)		(AR_WOW_TRANSMIT_BUFFER + ((i) << 2))

#define AR_WOW_KA_DESC_WORD2	0xe000

#define AR_WOW_KA_DATA_WORD0	0xe030

/* WoW Transmit Buffer for patterns */
#define AR_WOW_TB_PATTERN(i)	(0xe100 + (i << 8))
#define AR_WOW_TB_MASK(i)	(0xec00 + (i << 5))

/* Currently Pattern 0-7 are supported - so bit 0-7 are set */
#define AR_WOW_PATTERN_SUPPORTED	0xff
#define AR_WOW_LENGTH_MAX		0xff
#define AR_WOW_LEN1_SHIFT(_i)	((0x3 - ((_i) & 0x3)) << 0x3)
#define AR_WOW_LENGTH1_MASK(_i)	(AR_WOW_LENGTH_MAX << AR_WOW_LEN1_SHIFT(_i))
#define AR_WOW_LEN2_SHIFT(_i)	((0x7 - ((_i) & 0x7)) << 0x3)
#define AR_WOW_LENGTH2_MASK(_i)	(AR_WOW_LENGTH_MAX << AR_WOW_LEN2_SHIFT(_i))

#define AR9271_CORE_CLOCK	117   /* clock to 117Mhz */
#define AR9271_TARGET_BAUD_RATE	19200 /* 115200 */

#define AR_AGG_WEP_ENABLE_FIX		0x00000008  /* This allows the use of AR_AGG_WEP_ENABLE */
#define AR_ADHOC_MCAST_KEYID_ENABLE     0x00000040  /* This bit enables the Multicast search
						     * based on both MAC Address and Key ID.
						     * If bit is 0, then Multicast search is
						     * based on MAC address only.
						     * For Merlin and above only.
						     */
#define AR_AGG_WEP_ENABLE               0x00020000  /* This field enables AGG_WEP feature,
						     * when it is enable, AGG_WEP would takes
						     * charge of the encryption interface of
						     * pcu_txsm.
						     */

#define AR9300_SM_BASE				0xa200
#define AR9002_PHY_AGC_CONTROL			0x9860
#define AR9003_PHY_AGC_CONTROL			AR9300_SM_BASE + 0xc4
#define AR_PHY_AGC_CONTROL			(AR_SREV_9300_20_OR_LATER(ah) ? AR9003_PHY_AGC_CONTROL : AR9002_PHY_AGC_CONTROL)
#define AR_PHY_AGC_CONTROL_CAL			0x00000001  /* do internal calibration */
#define AR_PHY_AGC_CONTROL_NF			0x00000002  /* do noise-floor calibration */
#define AR_PHY_AGC_CONTROL_OFFSET_CAL		0x00000800  /* allow offset calibration */
#define AR_PHY_AGC_CONTROL_ENABLE_NF		0x00008000  /* enable noise floor calibration to happen */
#define AR_PHY_AGC_CONTROL_FLTR_CAL		0x00010000  /* allow tx filter calibration */
#define AR_PHY_AGC_CONTROL_NO_UPDATE_NF		0x00020000  /* don't update noise floor automatically */
#define AR_PHY_AGC_CONTROL_EXT_NF_PWR_MEAS	0x00040000  /* extend noise floor power measurement */
#define AR_PHY_AGC_CONTROL_CLC_SUCCESS		0x00080000  /* carrier leak calibration done */
#define AR_PHY_AGC_CONTROL_PKDET_CAL		0x00100000
#define AR_PHY_AGC_CONTROL_YCOK_MAX		0x000003c0
#define AR_PHY_AGC_CONTROL_YCOK_MAX_S		6

/* MCI Registers */

#define AR_MCI_COMMAND0				0x1800
#define AR_MCI_COMMAND0_HEADER			0xFF
#define AR_MCI_COMMAND0_HEADER_S		0
#define AR_MCI_COMMAND0_LEN			0x1f00
#define AR_MCI_COMMAND0_LEN_S			8
#define AR_MCI_COMMAND0_DISABLE_TIMESTAMP	0x2000
#define AR_MCI_COMMAND0_DISABLE_TIMESTAMP_S	13

#define AR_MCI_COMMAND1				0x1804

#define AR_MCI_COMMAND2				0x1808
#define AR_MCI_COMMAND2_RESET_TX		0x01
#define AR_MCI_COMMAND2_RESET_TX_S		0
#define AR_MCI_COMMAND2_RESET_RX		0x02
#define AR_MCI_COMMAND2_RESET_RX_S		1
#define AR_MCI_COMMAND2_RESET_RX_NUM_CYCLES     0x3FC
#define AR_MCI_COMMAND2_RESET_RX_NUM_CYCLES_S   2
#define AR_MCI_COMMAND2_RESET_REQ_WAKEUP        0x400
#define AR_MCI_COMMAND2_RESET_REQ_WAKEUP_S      10

#define AR_MCI_RX_CTRL				0x180c

#define AR_MCI_TX_CTRL				0x1810
/* 0 = no division, 1 = divide by 2, 2 = divide by 4, 3 = divide by 8 */
#define AR_MCI_TX_CTRL_CLK_DIV			0x03
#define AR_MCI_TX_CTRL_CLK_DIV_S		0
#define AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE	0x04
#define AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE_S	2
#define AR_MCI_TX_CTRL_GAIN_UPDATE_FREQ		0xFFFFF8
#define AR_MCI_TX_CTRL_GAIN_UPDATE_FREQ_S	3
#define AR_MCI_TX_CTRL_GAIN_UPDATE_NUM		0xF000000
#define AR_MCI_TX_CTRL_GAIN_UPDATE_NUM_S	24

#define AR_MCI_MSG_ATTRIBUTES_TABLE			0x1814
#define AR_MCI_MSG_ATTRIBUTES_TABLE_CHECKSUM		0xFFFF
#define AR_MCI_MSG_ATTRIBUTES_TABLE_CHECKSUM_S		0
#define AR_MCI_MSG_ATTRIBUTES_TABLE_INVALID_HDR		0xFFFF0000
#define AR_MCI_MSG_ATTRIBUTES_TABLE_INVALID_HDR_S	16

#define AR_MCI_SCHD_TABLE_0				0x1818
#define AR_MCI_SCHD_TABLE_1				0x181c
#define AR_MCI_GPM_0					0x1820
#define AR_MCI_GPM_1					0x1824
#define AR_MCI_GPM_WRITE_PTR				0xFFFF0000
#define AR_MCI_GPM_WRITE_PTR_S				16
#define AR_MCI_GPM_BUF_LEN				0x0000FFFF
#define AR_MCI_GPM_BUF_LEN_S				0

#define AR_MCI_INTERRUPT_RAW				0x1828
#define AR_MCI_INTERRUPT_EN				0x182c
#define AR_MCI_INTERRUPT_SW_MSG_DONE			0x00000001
#define AR_MCI_INTERRUPT_SW_MSG_DONE_S			0
#define AR_MCI_INTERRUPT_CPU_INT_MSG			0x00000002
#define AR_MCI_INTERRUPT_CPU_INT_MSG_S			1
#define AR_MCI_INTERRUPT_RX_CKSUM_FAIL			0x00000004
#define AR_MCI_INTERRUPT_RX_CKSUM_FAIL_S		2
#define AR_MCI_INTERRUPT_RX_INVALID_HDR			0x00000008
#define AR_MCI_INTERRUPT_RX_INVALID_HDR_S		3
#define AR_MCI_INTERRUPT_RX_HW_MSG_FAIL			0x00000010
#define AR_MCI_INTERRUPT_RX_HW_MSG_FAIL_S		4
#define AR_MCI_INTERRUPT_RX_SW_MSG_FAIL			0x00000020
#define AR_MCI_INTERRUPT_RX_SW_MSG_FAIL_S		5
#define AR_MCI_INTERRUPT_TX_HW_MSG_FAIL			0x00000080
#define AR_MCI_INTERRUPT_TX_HW_MSG_FAIL_S		7
#define AR_MCI_INTERRUPT_TX_SW_MSG_FAIL			0x00000100
#define AR_MCI_INTERRUPT_TX_SW_MSG_FAIL_S		8
#define AR_MCI_INTERRUPT_RX_MSG				0x00000200
#define AR_MCI_INTERRUPT_RX_MSG_S			9
#define AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE		0x00000400
#define AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE_S		10
#define AR_MCI_INTERRUPT_BT_PRI				0x07fff800
#define AR_MCI_INTERRUPT_BT_PRI_S			11
#define AR_MCI_INTERRUPT_BT_PRI_THRESH			0x08000000
#define AR_MCI_INTERRUPT_BT_PRI_THRESH_S		27
#define AR_MCI_INTERRUPT_BT_FREQ			0x10000000
#define AR_MCI_INTERRUPT_BT_FREQ_S			28
#define AR_MCI_INTERRUPT_BT_STOMP			0x20000000
#define AR_MCI_INTERRUPT_BT_STOMP_S			29
#define AR_MCI_INTERRUPT_BB_AIC_IRQ			0x40000000
#define AR_MCI_INTERRUPT_BB_AIC_IRQ_S			30
#define AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT		0x80000000
#define AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT_S		31

#define AR_MCI_INTERRUPT_DEFAULT    (AR_MCI_INTERRUPT_SW_MSG_DONE	  | \
				     AR_MCI_INTERRUPT_RX_INVALID_HDR	  | \
				     AR_MCI_INTERRUPT_RX_HW_MSG_FAIL	  | \
				     AR_MCI_INTERRUPT_RX_SW_MSG_FAIL	  | \
				     AR_MCI_INTERRUPT_TX_HW_MSG_FAIL	  | \
				     AR_MCI_INTERRUPT_TX_SW_MSG_FAIL	  | \
				     AR_MCI_INTERRUPT_RX_MSG		  | \
				     AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE | \
				     AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT)

#define AR_MCI_INTERRUPT_MSG_FAIL_MASK (AR_MCI_INTERRUPT_RX_HW_MSG_FAIL | \
					AR_MCI_INTERRUPT_RX_SW_MSG_FAIL | \
					AR_MCI_INTERRUPT_TX_HW_MSG_FAIL | \
					AR_MCI_INTERRUPT_TX_SW_MSG_FAIL)

#define AR_MCI_REMOTE_CPU_INT				0x1830
#define AR_MCI_REMOTE_CPU_INT_EN			0x1834
#define AR_MCI_INTERRUPT_RX_MSG_RAW			0x1838
#define AR_MCI_INTERRUPT_RX_MSG_EN			0x183c
#define AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET		0x00000001
#define AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET_S		0
#define AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL		0x00000002
#define AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL_S		1
#define AR_MCI_INTERRUPT_RX_MSG_CONT_NACK		0x00000004
#define AR_MCI_INTERRUPT_RX_MSG_CONT_NACK_S		2
#define AR_MCI_INTERRUPT_RX_MSG_CONT_INFO		0x00000008
#define AR_MCI_INTERRUPT_RX_MSG_CONT_INFO_S		3
#define AR_MCI_INTERRUPT_RX_MSG_CONT_RST		0x00000010
#define AR_MCI_INTERRUPT_RX_MSG_CONT_RST_S		4
#define AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO		0x00000020
#define AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO_S		5
#define AR_MCI_INTERRUPT_RX_MSG_CPU_INT			0x00000040
#define AR_MCI_INTERRUPT_RX_MSG_CPU_INT_S		6
#define AR_MCI_INTERRUPT_RX_MSG_GPM			0x00000100
#define AR_MCI_INTERRUPT_RX_MSG_GPM_S			8
#define AR_MCI_INTERRUPT_RX_MSG_LNA_INFO		0x00000200
#define AR_MCI_INTERRUPT_RX_MSG_LNA_INFO_S		9
#define AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING		0x00000400
#define AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING_S		10
#define AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING		0x00000800
#define AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING_S		11
#define AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE		0x00001000
#define AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE_S		12
#define AR_MCI_INTERRUPT_RX_HW_MSG_MASK	 (AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO  | \
					  AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL| \
					  AR_MCI_INTERRUPT_RX_MSG_LNA_INFO   | \
					  AR_MCI_INTERRUPT_RX_MSG_CONT_NACK  | \
					  AR_MCI_INTERRUPT_RX_MSG_CONT_INFO  | \
					  AR_MCI_INTERRUPT_RX_MSG_CONT_RST)

#define AR_MCI_INTERRUPT_RX_MSG_DEFAULT (AR_MCI_INTERRUPT_RX_MSG_GPM	 | \
					 AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET| \
					 AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING  | \
					 AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING| \
					 AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE)

#define AR_MCI_CPU_INT					0x1840

#define AR_MCI_RX_STATUS			0x1844
#define AR_MCI_RX_LAST_SCHD_MSG_INDEX		0x00000F00
#define AR_MCI_RX_LAST_SCHD_MSG_INDEX_S		8
#define AR_MCI_RX_REMOTE_SLEEP			0x00001000
#define AR_MCI_RX_REMOTE_SLEEP_S		12
#define AR_MCI_RX_MCI_CLK_REQ			0x00002000
#define AR_MCI_RX_MCI_CLK_REQ_S			13

#define AR_MCI_CONT_STATUS			0x1848
#define AR_MCI_CONT_RSSI_POWER			0x000000FF
#define AR_MCI_CONT_RSSI_POWER_S		0
#define AR_MCI_CONT_PRIORITY			0x0000FF00
#define AR_MCI_CONT_PRIORITY_S			8
#define AR_MCI_CONT_TXRX			0x00010000
#define AR_MCI_CONT_TXRX_S			16

#define AR_MCI_BT_PRI0				0x184c
#define AR_MCI_BT_PRI1				0x1850
#define AR_MCI_BT_PRI2				0x1854
#define AR_MCI_BT_PRI3				0x1858
#define AR_MCI_BT_PRI				0x185c
#define AR_MCI_WL_FREQ0				0x1860
#define AR_MCI_WL_FREQ1				0x1864
#define AR_MCI_WL_FREQ2				0x1868
#define AR_MCI_GAIN				0x186c
#define AR_MCI_WBTIMER1				0x1870
#define AR_MCI_WBTIMER2				0x1874
#define AR_MCI_WBTIMER3				0x1878
#define AR_MCI_WBTIMER4				0x187c
#define AR_MCI_MAXGAIN				0x1880
#define AR_MCI_HW_SCHD_TBL_CTL			0x1884
#define AR_MCI_HW_SCHD_TBL_D0			0x1888
#define AR_MCI_HW_SCHD_TBL_D1			0x188c
#define AR_MCI_HW_SCHD_TBL_D2			0x1890
#define AR_MCI_HW_SCHD_TBL_D3			0x1894
#define AR_MCI_TX_PAYLOAD0			0x1898
#define AR_MCI_TX_PAYLOAD1			0x189c
#define AR_MCI_TX_PAYLOAD2			0x18a0
#define AR_MCI_TX_PAYLOAD3			0x18a4
#define AR_BTCOEX_WBTIMER			0x18a8

#define AR_BTCOEX_CTRL					0x18ac
#define AR_BTCOEX_CTRL_AR9462_MODE			0x00000001
#define AR_BTCOEX_CTRL_AR9462_MODE_S			0
#define AR_BTCOEX_CTRL_WBTIMER_EN			0x00000002
#define AR_BTCOEX_CTRL_WBTIMER_EN_S			1
#define AR_BTCOEX_CTRL_MCI_MODE_EN			0x00000004
#define AR_BTCOEX_CTRL_MCI_MODE_EN_S			2
#define AR_BTCOEX_CTRL_LNA_SHARED			0x00000008
#define AR_BTCOEX_CTRL_LNA_SHARED_S			3
#define AR_BTCOEX_CTRL_PA_SHARED			0x00000010
#define AR_BTCOEX_CTRL_PA_SHARED_S			4
#define AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN		0x00000020
#define AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN_S		5
#define AR_BTCOEX_CTRL_TIME_TO_NEXT_BT_THRESH_EN	0x00000040
#define AR_BTCOEX_CTRL_TIME_TO_NEXT_BT_THRESH_EN_S	6
#define AR_BTCOEX_CTRL_NUM_ANTENNAS			0x00000180
#define AR_BTCOEX_CTRL_NUM_ANTENNAS_S			7
#define AR_BTCOEX_CTRL_RX_CHAIN_MASK			0x00000E00
#define AR_BTCOEX_CTRL_RX_CHAIN_MASK_S			9
#define AR_BTCOEX_CTRL_AGGR_THRESH			0x00007000
#define AR_BTCOEX_CTRL_AGGR_THRESH_S			12
#define AR_BTCOEX_CTRL_1_CHAIN_BCN			0x00080000
#define AR_BTCOEX_CTRL_1_CHAIN_BCN_S			19
#define AR_BTCOEX_CTRL_1_CHAIN_ACK			0x00100000
#define AR_BTCOEX_CTRL_1_CHAIN_ACK_S			20
#define AR_BTCOEX_CTRL_WAIT_BA_MARGIN			0x1FE00000
#define AR_BTCOEX_CTRL_WAIT_BA_MARGIN_S			28
#define AR_BTCOEX_CTRL_REDUCE_TXPWR			0x20000000
#define AR_BTCOEX_CTRL_REDUCE_TXPWR_S			29
#define AR_BTCOEX_CTRL_SPDT_ENABLE_10			0x40000000
#define AR_BTCOEX_CTRL_SPDT_ENABLE_10_S			30
#define AR_BTCOEX_CTRL_SPDT_POLARITY			0x80000000
#define AR_BTCOEX_CTRL_SPDT_POLARITY_S			31

#define AR_BTCOEX_MAX_TXPWR(_x)				(0x18c0 + ((_x) << 2))
#define AR_BTCOEX_WL_LNA				0x1940
#define AR_BTCOEX_RFGAIN_CTRL				0x1944

#define AR_BTCOEX_CTRL2					0x1948
#define AR_BTCOEX_CTRL2_TXPWR_THRESH			0x0007F800
#define AR_BTCOEX_CTRL2_TXPWR_THRESH_S			11
#define AR_BTCOEX_CTRL2_TX_CHAIN_MASK			0x00380000
#define AR_BTCOEX_CTRL2_TX_CHAIN_MASK_S			19
#define AR_BTCOEX_CTRL2_RX_DEWEIGHT			0x00400000
#define AR_BTCOEX_CTRL2_RX_DEWEIGHT_S			22
#define AR_BTCOEX_CTRL2_GPIO_OBS_SEL			0x00800000
#define AR_BTCOEX_CTRL2_GPIO_OBS_SEL_S			23
#define AR_BTCOEX_CTRL2_MAC_BB_OBS_SEL			0x01000000
#define AR_BTCOEX_CTRL2_MAC_BB_OBS_SEL_S		24
#define AR_BTCOEX_CTRL2_DESC_BASED_TXPWR_ENABLE		0x02000000
#define AR_BTCOEX_CTRL2_DESC_BASED_TXPWR_ENABLE_S	25

#define AR_BTCOEX_CTRL_SPDT_ENABLE          0x00000001
#define AR_BTCOEX_CTRL_SPDT_ENABLE_S        0
#define AR_BTCOEX_CTRL_BT_OWN_SPDT_CTRL     0x00000002
#define AR_BTCOEX_CTRL_BT_OWN_SPDT_CTRL_S   1
#define AR_BTCOEX_CTRL_USE_LATCHED_BT_ANT   0x00000004
#define AR_BTCOEX_CTRL_USE_LATCHED_BT_ANT_S 2
#define AR_GLB_WLAN_UART_INTF_EN            0x00020000
#define AR_GLB_WLAN_UART_INTF_EN_S          17
#define AR_GLB_DS_JTAG_DISABLE              0x00040000
#define AR_GLB_DS_JTAG_DISABLE_S            18

#define AR_BTCOEX_RC                    0x194c
#define AR_BTCOEX_MAX_RFGAIN(_x)        (0x1950 + ((_x) << 2))
#define AR_BTCOEX_DBG                   0x1a50
#define AR_MCI_LAST_HW_MSG_HDR          0x1a54
#define AR_MCI_LAST_HW_MSG_BDY          0x1a58

#define AR_MCI_SCHD_TABLE_2             0x1a5c
#define AR_MCI_SCHD_TABLE_2_MEM_BASED   0x00000001
#define AR_MCI_SCHD_TABLE_2_MEM_BASED_S 0
#define AR_MCI_SCHD_TABLE_2_HW_BASED    0x00000002
#define AR_MCI_SCHD_TABLE_2_HW_BASED_S  1

#define AR_BTCOEX_CTRL3               0x1a60
#define AR_BTCOEX_CTRL3_CONT_INFO_TIMEOUT	0x00000fff
#define AR_BTCOEX_CTRL3_CONT_INFO_TIMEOUT_S	0

#define AR_GLB_SWREG_DISCONT_MODE         0x2002c
#define AR_GLB_SWREG_DISCONT_EN_BT_WLAN   0x3

#endif
