/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Header file for NI general purpose counter support code (ni_tio.c and
 * ni_tiocmd.c)
 *
 * COMEDI - Linux Control and Measurement Device Interface
 */

#ifndef _COMEDI_NI_TIO_INTERNAL_H
#define _COMEDI_NI_TIO_INTERNAL_H

#include "ni_tio.h"

#define NITIO_AUTO_INC_REG(x)		(NITIO_G0_AUTO_INC + (x))
#define GI_AUTO_INC_MASK		0xff
#define NITIO_CMD_REG(x)		(NITIO_G0_CMD + (x))
#define GI_ARM				BIT(0)
#define GI_SAVE_TRACE			BIT(1)
#define GI_LOAD				BIT(2)
#define GI_DISARM			BIT(4)
#define GI_CNT_DIR(x)			(((x) & 0x3) << 5)
#define GI_CNT_DIR_MASK			GI_CNT_DIR(3)
#define GI_WRITE_SWITCH			BIT(7)
#define GI_SYNC_GATE			BIT(8)
#define GI_LITTLE_BIG_ENDIAN		BIT(9)
#define GI_BANK_SWITCH_START		BIT(10)
#define GI_BANK_SWITCH_MODE		BIT(11)
#define GI_BANK_SWITCH_ENABLE		BIT(12)
#define GI_ARM_COPY			BIT(13)
#define GI_SAVE_TRACE_COPY		BIT(14)
#define GI_DISARM_COPY			BIT(15)
#define NITIO_HW_SAVE_REG(x)		(NITIO_G0_HW_SAVE + (x))
#define NITIO_SW_SAVE_REG(x)		(NITIO_G0_SW_SAVE + (x))
#define NITIO_MODE_REG(x)		(NITIO_G0_MODE + (x))
#define GI_GATING_MODE(x)		(((x) & 0x3) << 0)
#define GI_GATING_DISABLED		GI_GATING_MODE(0)
#define GI_LEVEL_GATING			GI_GATING_MODE(1)
#define GI_RISING_EDGE_GATING		GI_GATING_MODE(2)
#define GI_FALLING_EDGE_GATING		GI_GATING_MODE(3)
#define GI_GATING_MODE_MASK		GI_GATING_MODE(3)
#define GI_GATE_ON_BOTH_EDGES		BIT(2)
#define GI_EDGE_GATE_MODE(x)		(((x) & 0x3) << 3)
#define GI_EDGE_GATE_STARTS_STOPS	GI_EDGE_GATE_MODE(0)
#define GI_EDGE_GATE_STOPS_STARTS	GI_EDGE_GATE_MODE(1)
#define GI_EDGE_GATE_STARTS		GI_EDGE_GATE_MODE(2)
#define GI_EDGE_GATE_NO_STARTS_OR_STOPS	GI_EDGE_GATE_MODE(3)
#define GI_EDGE_GATE_MODE_MASK		GI_EDGE_GATE_MODE(3)
#define GI_STOP_MODE(x)			(((x) & 0x3) << 5)
#define GI_STOP_ON_GATE			GI_STOP_MODE(0)
#define GI_STOP_ON_GATE_OR_TC		GI_STOP_MODE(1)
#define GI_STOP_ON_GATE_OR_SECOND_TC	GI_STOP_MODE(2)
#define GI_STOP_MODE_MASK		GI_STOP_MODE(3)
#define GI_LOAD_SRC_SEL			BIT(7)
#define GI_OUTPUT_MODE(x)		(((x) & 0x3) << 8)
#define GI_OUTPUT_TC_PULSE		GI_OUTPUT_MODE(1)
#define GI_OUTPUT_TC_TOGGLE		GI_OUTPUT_MODE(2)
#define GI_OUTPUT_TC_OR_GATE_TOGGLE	GI_OUTPUT_MODE(3)
#define GI_OUTPUT_MODE_MASK		GI_OUTPUT_MODE(3)
#define GI_COUNTING_ONCE(x)		(((x) & 0x3) << 10)
#define GI_NO_HARDWARE_DISARM		GI_COUNTING_ONCE(0)
#define GI_DISARM_AT_TC			GI_COUNTING_ONCE(1)
#define GI_DISARM_AT_GATE		GI_COUNTING_ONCE(2)
#define GI_DISARM_AT_TC_OR_GATE		GI_COUNTING_ONCE(3)
#define GI_COUNTING_ONCE_MASK		GI_COUNTING_ONCE(3)
#define GI_LOADING_ON_TC		BIT(12)
#define GI_GATE_POL_INVERT		BIT(13)
#define GI_LOADING_ON_GATE		BIT(14)
#define GI_RELOAD_SRC_SWITCHING		BIT(15)
#define NITIO_LOADA_REG(x)		(NITIO_G0_LOADA + (x))
#define NITIO_LOADB_REG(x)		(NITIO_G0_LOADB + (x))
#define NITIO_INPUT_SEL_REG(x)		(NITIO_G0_INPUT_SEL + (x))
#define GI_READ_ACKS_IRQ		BIT(0)
#define GI_WRITE_ACKS_IRQ		BIT(1)
#define GI_BITS_TO_SRC(x)		(((x) >> 2) & 0x1f)
#define GI_SRC_SEL(x)			(((x) & 0x1f) << 2)
#define GI_SRC_SEL_MASK			GI_SRC_SEL(0x1f)
#define GI_BITS_TO_GATE(x)		(((x) >> 7) & 0x1f)
#define GI_GATE_SEL(x)			(((x) & 0x1f) << 7)
#define GI_GATE_SEL_MASK		GI_GATE_SEL(0x1f)
#define GI_GATE_SEL_LOAD_SRC		BIT(12)
#define GI_OR_GATE			BIT(13)
#define GI_OUTPUT_POL_INVERT		BIT(14)
#define GI_SRC_POL_INVERT		BIT(15)
#define NITIO_CNT_MODE_REG(x)		(NITIO_G0_CNT_MODE + (x))
#define GI_CNT_MODE(x)			(((x) & 0x7) << 0)
#define GI_CNT_MODE_NORMAL		GI_CNT_MODE(0)
#define GI_CNT_MODE_QUADX1		GI_CNT_MODE(1)
#define GI_CNT_MODE_QUADX2		GI_CNT_MODE(2)
#define GI_CNT_MODE_QUADX4		GI_CNT_MODE(3)
#define GI_CNT_MODE_TWO_PULSE		GI_CNT_MODE(4)
#define GI_CNT_MODE_SYNC_SRC		GI_CNT_MODE(6)
#define GI_CNT_MODE_MASK		GI_CNT_MODE(7)
#define GI_INDEX_MODE			BIT(4)
#define GI_INDEX_PHASE(x)		(((x) & 0x3) << 5)
#define GI_INDEX_PHASE_MASK		GI_INDEX_PHASE(3)
#define GI_HW_ARM_ENA			BIT(7)
#define GI_HW_ARM_SEL(x)		((x) << 8)
#define GI_660X_HW_ARM_SEL_MASK		GI_HW_ARM_SEL(0x7)
#define GI_M_HW_ARM_SEL_MASK		GI_HW_ARM_SEL(0x1f)
#define GI_660X_PRESCALE_X8		BIT(12)
#define GI_M_PRESCALE_X8		BIT(13)
#define GI_660X_ALT_SYNC		BIT(13)
#define GI_M_ALT_SYNC			BIT(14)
#define GI_660X_PRESCALE_X2		BIT(14)
#define GI_M_PRESCALE_X2		BIT(15)
#define NITIO_GATE2_REG(x)		(NITIO_G0_GATE2 + (x))
#define GI_GATE2_MODE			BIT(0)
#define GI_BITS_TO_GATE2(x)		(((x) >> 7) & 0x1f)
#define GI_GATE2_SEL(x)			(((x) & 0x1f) << 7)
#define GI_GATE2_SEL_MASK		GI_GATE2_SEL(0x1f)
#define GI_GATE2_POL_INVERT		BIT(13)
#define GI_GATE2_SUBSEL			BIT(14)
#define GI_SRC_SUBSEL			BIT(15)
#define NITIO_SHARED_STATUS_REG(x)	(NITIO_G01_STATUS + ((x) / 2))
#define GI_SAVE(x)			(((x) % 2) ? BIT(1) : BIT(0))
#define GI_COUNTING(x)			(((x) % 2) ? BIT(3) : BIT(2))
#define GI_NEXT_LOAD_SRC(x)		(((x) % 2) ? BIT(5) : BIT(4))
#define GI_STALE_DATA(x)		(((x) % 2) ? BIT(7) : BIT(6))
#define GI_ARMED(x)			(((x) % 2) ? BIT(9) : BIT(8))
#define GI_NO_LOAD_BETWEEN_GATES(x)	(((x) % 2) ? BIT(11) : BIT(10))
#define GI_TC_ERROR(x)			(((x) % 2) ? BIT(13) : BIT(12))
#define GI_GATE_ERROR(x)		(((x) % 2) ? BIT(15) : BIT(14))
#define NITIO_RESET_REG(x)		(NITIO_G01_RESET + ((x) / 2))
#define GI_RESET(x)			BIT(2 + ((x) % 2))
#define NITIO_STATUS1_REG(x)		(NITIO_G01_STATUS1 + ((x) / 2))
#define NITIO_STATUS2_REG(x)		(NITIO_G01_STATUS2 + ((x) / 2))
#define GI_OUTPUT(x)			(((x) % 2) ? BIT(1) : BIT(0))
#define GI_HW_SAVE(x)			(((x) % 2) ? BIT(13) : BIT(12))
#define GI_PERMANENT_STALE(x)		(((x) % 2) ? BIT(15) : BIT(14))
#define NITIO_DMA_CFG_REG(x)		(NITIO_G0_DMA_CFG + (x))
#define GI_DMA_ENABLE			BIT(0)
#define GI_DMA_WRITE			BIT(1)
#define GI_DMA_INT_ENA			BIT(2)
#define GI_DMA_RESET			BIT(3)
#define GI_DMA_BANKSW_ERROR		BIT(4)
#define NITIO_DMA_STATUS_REG(x)		(NITIO_G0_DMA_STATUS + (x))
#define GI_DMA_READBANK			BIT(13)
#define GI_DRQ_ERROR			BIT(14)
#define GI_DRQ_STATUS			BIT(15)
#define NITIO_ABZ_REG(x)		(NITIO_G0_ABZ + (x))
#define NITIO_INT_ACK_REG(x)		(NITIO_G0_INT_ACK + (x))
#define GI_GATE_ERROR_CONFIRM(x)	(((x) % 2) ? BIT(1) : BIT(5))
#define GI_TC_ERROR_CONFIRM(x)		(((x) % 2) ? BIT(2) : BIT(6))
#define GI_TC_INTERRUPT_ACK		BIT(14)
#define GI_GATE_INTERRUPT_ACK		BIT(15)
#define NITIO_STATUS_REG(x)		(NITIO_G0_STATUS + (x))
#define GI_GATE_INTERRUPT		BIT(2)
#define GI_TC				BIT(3)
#define GI_INTERRUPT			BIT(15)
#define NITIO_INT_ENA_REG(x)		(NITIO_G0_INT_ENA + (x))
#define GI_TC_INTERRUPT_ENABLE(x)	(((x) % 2) ? BIT(9) : BIT(6))
#define GI_GATE_INTERRUPT_ENABLE(x)	(((x) % 2) ? BIT(10) : BIT(8))

void ni_tio_write(struct ni_gpct *counter, unsigned int value,
		  enum ni_gpct_register);
unsigned int ni_tio_read(struct ni_gpct *counter, enum ni_gpct_register);

static inline bool
ni_tio_counting_mode_registers_present(const struct ni_gpct_device *counter_dev)
{
	/* m series and 660x variants have counting mode registers */
	return counter_dev->variant != ni_gpct_variant_e_series;
}

void ni_tio_set_bits(struct ni_gpct *counter, enum ni_gpct_register reg,
		     unsigned int mask, unsigned int value);
unsigned int ni_tio_get_soft_copy(const struct ni_gpct *counter,
				  enum ni_gpct_register reg);

int ni_tio_arm(struct ni_gpct *counter, bool arm, unsigned int start_trigger);
int ni_tio_set_gate_src(struct ni_gpct *counter, unsigned int gate,
			unsigned int src);
int ni_tio_set_gate_src_raw(struct ni_gpct *counter, unsigned int gate,
			    unsigned int src);

#endif /* _COMEDI_NI_TIO_INTERNAL_H */
