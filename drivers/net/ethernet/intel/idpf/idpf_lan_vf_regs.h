/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_LAN_VF_REGS_H_
#define _IDPF_LAN_VF_REGS_H_

/* Reset */
#define VFGEN_RSTAT			0x00008800
#define VFGEN_RSTAT_VFR_STATE_S		0
#define VFGEN_RSTAT_VFR_STATE_M		GENMASK(1, 0)

/* Control(VF Mailbox) Queue */
#define VF_BASE				0x00006000

#define VF_ATQBAL			(VF_BASE + 0x1C00)
#define VF_ATQBAH			(VF_BASE + 0x1800)
#define VF_ATQLEN			(VF_BASE + 0x0800)
#define VF_ATQLEN_ATQLEN_S		0
#define VF_ATQLEN_ATQLEN_M		GENMASK(9, 0)
#define VF_ATQLEN_ATQVFE_S		28
#define VF_ATQLEN_ATQVFE_M		BIT(VF_ATQLEN_ATQVFE_S)
#define VF_ATQLEN_ATQOVFL_S		29
#define VF_ATQLEN_ATQOVFL_M		BIT(VF_ATQLEN_ATQOVFL_S)
#define VF_ATQLEN_ATQCRIT_S		30
#define VF_ATQLEN_ATQCRIT_M		BIT(VF_ATQLEN_ATQCRIT_S)
#define VF_ATQLEN_ATQENABLE_S		31
#define VF_ATQLEN_ATQENABLE_M		BIT(VF_ATQLEN_ATQENABLE_S)
#define VF_ATQH				(VF_BASE + 0x0400)
#define VF_ATQH_ATQH_S			0
#define VF_ATQH_ATQH_M			GENMASK(9, 0)
#define VF_ATQT				(VF_BASE + 0x2400)

#define VF_ARQBAL			(VF_BASE + 0x0C00)
#define VF_ARQBAH			(VF_BASE)
#define VF_ARQLEN			(VF_BASE + 0x2000)
#define VF_ARQLEN_ARQLEN_S		0
#define VF_ARQLEN_ARQLEN_M		GENMASK(9, 0)
#define VF_ARQLEN_ARQVFE_S		28
#define VF_ARQLEN_ARQVFE_M		BIT(VF_ARQLEN_ARQVFE_S)
#define VF_ARQLEN_ARQOVFL_S		29
#define VF_ARQLEN_ARQOVFL_M		BIT(VF_ARQLEN_ARQOVFL_S)
#define VF_ARQLEN_ARQCRIT_S		30
#define VF_ARQLEN_ARQCRIT_M		BIT(VF_ARQLEN_ARQCRIT_S)
#define VF_ARQLEN_ARQENABLE_S		31
#define VF_ARQLEN_ARQENABLE_M		BIT(VF_ARQLEN_ARQENABLE_S)
#define VF_ARQH				(VF_BASE + 0x1400)
#define VF_ARQH_ARQH_S			0
#define VF_ARQH_ARQH_M			GENMASK(12, 0)
#define VF_ARQT				(VF_BASE + 0x1000)

/* Transmit queues */
#define VF_QTX_TAIL_BASE		0x00000000
#define VF_QTX_TAIL(_QTX)		(VF_QTX_TAIL_BASE + (_QTX) * 0x4)
#define VF_QTX_TAIL_EXT_BASE		0x00040000
#define VF_QTX_TAIL_EXT(_QTX)		(VF_QTX_TAIL_EXT_BASE + ((_QTX) * 4))

/* Receive queues */
#define VF_QRX_TAIL_BASE		0x00002000
#define VF_QRX_TAIL(_QRX)		(VF_QRX_TAIL_BASE + ((_QRX) * 4))
#define VF_QRX_TAIL_EXT_BASE		0x00050000
#define VF_QRX_TAIL_EXT(_QRX)		(VF_QRX_TAIL_EXT_BASE + ((_QRX) * 4))
#define VF_QRXB_TAIL_BASE		0x00060000
#define VF_QRXB_TAIL(_QRX)		(VF_QRXB_TAIL_BASE + ((_QRX) * 4))

/* Interrupts */
#define VF_INT_DYN_CTL0			0x00005C00
#define VF_INT_DYN_CTL0_INTENA_S	0
#define VF_INT_DYN_CTL0_INTENA_M	BIT(VF_INT_DYN_CTL0_INTENA_S)
#define VF_INT_DYN_CTL0_ITR_INDX_S	3
#define VF_INT_DYN_CTL0_ITR_INDX_M	GENMASK(4, 3)
#define VF_INT_DYN_CTLN(_INT)		(0x00003800 + ((_INT) * 4))
#define VF_INT_DYN_CTLN_EXT(_INT)	(0x00070000 + ((_INT) * 4))
#define VF_INT_DYN_CTLN_INTENA_S	0
#define VF_INT_DYN_CTLN_INTENA_M	BIT(VF_INT_DYN_CTLN_INTENA_S)
#define VF_INT_DYN_CTLN_CLEARPBA_S	1
#define VF_INT_DYN_CTLN_CLEARPBA_M	BIT(VF_INT_DYN_CTLN_CLEARPBA_S)
#define VF_INT_DYN_CTLN_SWINT_TRIG_S	2
#define VF_INT_DYN_CTLN_SWINT_TRIG_M	BIT(VF_INT_DYN_CTLN_SWINT_TRIG_S)
#define VF_INT_DYN_CTLN_ITR_INDX_S	3
#define VF_INT_DYN_CTLN_ITR_INDX_M	GENMASK(4, 3)
#define VF_INT_DYN_CTLN_INTERVAL_S	5
#define VF_INT_DYN_CTLN_INTERVAL_M	BIT(VF_INT_DYN_CTLN_INTERVAL_S)
#define VF_INT_DYN_CTLN_SW_ITR_INDX_ENA_S 24
#define VF_INT_DYN_CTLN_SW_ITR_INDX_ENA_M BIT(VF_INT_DYN_CTLN_SW_ITR_INDX_ENA_S)
#define VF_INT_DYN_CTLN_SW_ITR_INDX_S	25
#define VF_INT_DYN_CTLN_SW_ITR_INDX_M	BIT(VF_INT_DYN_CTLN_SW_ITR_INDX_S)
#define VF_INT_DYN_CTLN_WB_ON_ITR_S	30
#define VF_INT_DYN_CTLN_WB_ON_ITR_M	BIT(VF_INT_DYN_CTLN_WB_ON_ITR_S)
#define VF_INT_DYN_CTLN_INTENA_MSK_S	31
#define VF_INT_DYN_CTLN_INTENA_MSK_M	BIT(VF_INT_DYN_CTLN_INTENA_MSK_S)
/* _ITR is ITR index, _INT is interrupt index, _itrn_indx_spacing is spacing
 * b/w itrn registers of the same vector
 */
#define VF_INT_ITR0(_ITR)		(0x00004C00 + ((_ITR) * 4))
#define VF_INT_ITRN_ADDR(_ITR, _reg_start, _itrn_indx_spacing)	\
	((_reg_start) + ((_ITR) * (_itrn_indx_spacing)))
/* For VF with 16 vector support, itrn_reg_spacing is 0x4, itrn_indx_spacing
 * is 0x40 and base register offset is 0x00002800
 */
#define VF_INT_ITRN(_INT, _ITR)		\
	(0x00002800 + ((_INT) * 4) + ((_ITR) * 0x40))
/* For VF with 64 vector support, itrn_reg_spacing is 0x4, itrn_indx_spacing
 * is 0x100 and base register offset is 0x00002C00
 */
#define VF_INT_ITRN_64(_INT, _ITR)	\
	(0x00002C00 + ((_INT) * 4) + ((_ITR) * 0x100))
/* For VF with 2k vector support, itrn_reg_spacing is 0x4, itrn_indx_spacing
 * is 0x2000 and base register offset is 0x00072000
 */
#define VF_INT_ITRN_2K(_INT, _ITR)	\
	(0x00072000 + ((_INT) * 4) + ((_ITR) * 0x2000))
#define VF_INT_ITRN_MAX_INDEX		2
#define VF_INT_ITRN_INTERVAL_S		0
#define VF_INT_ITRN_INTERVAL_M		GENMASK(11, 0)
#define VF_INT_PBA_CLEAR		0x00008900

#define VF_INT_ICR0_ENA1		0x00005000
#define VF_INT_ICR0_ENA1_ADMINQ_S	30
#define VF_INT_ICR0_ENA1_ADMINQ_M	BIT(VF_INT_ICR0_ENA1_ADMINQ_S)
#define VF_INT_ICR0_ENA1_RSVD_S		31
#define VF_INT_ICR01			0x00004800
#define VF_QF_HENA(_i)			(0x0000C400 + ((_i) * 4))
#define VF_QF_HENA_MAX_INDX		1
#define VF_QF_HKEY(_i)			(0x0000CC00 + ((_i) * 4))
#define VF_QF_HKEY_MAX_INDX		12
#define VF_QF_HLUT(_i)			(0x0000D000 + ((_i) * 4))
#define VF_QF_HLUT_MAX_INDX		15
#endif
