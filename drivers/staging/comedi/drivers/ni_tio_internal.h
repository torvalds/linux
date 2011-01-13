/*
    drivers/ni_tio_internal.h
    Header file for NI general purpose counter support code (ni_tio.c and
    ni_tiocmd.c)

    COMEDI - Linux Control and Measurement Device Interface

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef _COMEDI_NI_TIO_INTERNAL_H
#define _COMEDI_NI_TIO_INTERNAL_H

#include "ni_tio.h"

static inline enum ni_gpct_register NITIO_Gi_Autoincrement_Reg(unsigned
							       counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_Autoincrement_Reg;
		break;
	case 1:
		return NITIO_G1_Autoincrement_Reg;
		break;
	case 2:
		return NITIO_G2_Autoincrement_Reg;
		break;
	case 3:
		return NITIO_G3_Autoincrement_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_Command_Reg(unsigned counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_Command_Reg;
		break;
	case 1:
		return NITIO_G1_Command_Reg;
		break;
	case 2:
		return NITIO_G2_Command_Reg;
		break;
	case 3:
		return NITIO_G3_Command_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_Counting_Mode_Reg(unsigned
							       counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_Counting_Mode_Reg;
		break;
	case 1:
		return NITIO_G1_Counting_Mode_Reg;
		break;
	case 2:
		return NITIO_G2_Counting_Mode_Reg;
		break;
	case 3:
		return NITIO_G3_Counting_Mode_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_Input_Select_Reg(unsigned
							      counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_Input_Select_Reg;
		break;
	case 1:
		return NITIO_G1_Input_Select_Reg;
		break;
	case 2:
		return NITIO_G2_Input_Select_Reg;
		break;
	case 3:
		return NITIO_G3_Input_Select_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gxx_Joint_Reset_Reg(unsigned
							      counter_index)
{
	switch (counter_index) {
	case 0:
	case 1:
		return NITIO_G01_Joint_Reset_Reg;
		break;
	case 2:
	case 3:
		return NITIO_G23_Joint_Reset_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gxx_Joint_Status1_Reg(unsigned
								counter_index)
{
	switch (counter_index) {
	case 0:
	case 1:
		return NITIO_G01_Joint_Status1_Reg;
		break;
	case 2:
	case 3:
		return NITIO_G23_Joint_Status1_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gxx_Joint_Status2_Reg(unsigned
								counter_index)
{
	switch (counter_index) {
	case 0:
	case 1:
		return NITIO_G01_Joint_Status2_Reg;
		break;
	case 2:
	case 3:
		return NITIO_G23_Joint_Status2_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gxx_Status_Reg(unsigned counter_index)
{
	switch (counter_index) {
	case 0:
	case 1:
		return NITIO_G01_Status_Reg;
		break;
	case 2:
	case 3:
		return NITIO_G23_Status_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_LoadA_Reg(unsigned counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_LoadA_Reg;
		break;
	case 1:
		return NITIO_G1_LoadA_Reg;
		break;
	case 2:
		return NITIO_G2_LoadA_Reg;
		break;
	case 3:
		return NITIO_G3_LoadA_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_LoadB_Reg(unsigned counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_LoadB_Reg;
		break;
	case 1:
		return NITIO_G1_LoadB_Reg;
		break;
	case 2:
		return NITIO_G2_LoadB_Reg;
		break;
	case 3:
		return NITIO_G3_LoadB_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_Mode_Reg(unsigned counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_Mode_Reg;
		break;
	case 1:
		return NITIO_G1_Mode_Reg;
		break;
	case 2:
		return NITIO_G2_Mode_Reg;
		break;
	case 3:
		return NITIO_G3_Mode_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_SW_Save_Reg(int counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_SW_Save_Reg;
		break;
	case 1:
		return NITIO_G1_SW_Save_Reg;
		break;
	case 2:
		return NITIO_G2_SW_Save_Reg;
		break;
	case 3:
		return NITIO_G3_SW_Save_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_Second_Gate_Reg(int counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_Second_Gate_Reg;
		break;
	case 1:
		return NITIO_G1_Second_Gate_Reg;
		break;
	case 2:
		return NITIO_G2_Second_Gate_Reg;
		break;
	case 3:
		return NITIO_G3_Second_Gate_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_DMA_Config_Reg(int counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_DMA_Config_Reg;
		break;
	case 1:
		return NITIO_G1_DMA_Config_Reg;
		break;
	case 2:
		return NITIO_G2_DMA_Config_Reg;
		break;
	case 3:
		return NITIO_G3_DMA_Config_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_DMA_Status_Reg(int counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_DMA_Status_Reg;
		break;
	case 1:
		return NITIO_G1_DMA_Status_Reg;
		break;
	case 2:
		return NITIO_G2_DMA_Status_Reg;
		break;
	case 3:
		return NITIO_G3_DMA_Status_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_ABZ_Reg(int counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_ABZ_Reg;
		break;
	case 1:
		return NITIO_G1_ABZ_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_Interrupt_Acknowledge_Reg(int
								       counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_Interrupt_Acknowledge_Reg;
		break;
	case 1:
		return NITIO_G1_Interrupt_Acknowledge_Reg;
		break;
	case 2:
		return NITIO_G2_Interrupt_Acknowledge_Reg;
		break;
	case 3:
		return NITIO_G3_Interrupt_Acknowledge_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_Status_Reg(int counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_Status_Reg;
		break;
	case 1:
		return NITIO_G1_Status_Reg;
		break;
	case 2:
		return NITIO_G2_Status_Reg;
		break;
	case 3:
		return NITIO_G3_Status_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline enum ni_gpct_register NITIO_Gi_Interrupt_Enable_Reg(int
								  counter_index)
{
	switch (counter_index) {
	case 0:
		return NITIO_G0_Interrupt_Enable_Reg;
		break;
	case 1:
		return NITIO_G1_Interrupt_Enable_Reg;
		break;
	case 2:
		return NITIO_G2_Interrupt_Enable_Reg;
		break;
	case 3:
		return NITIO_G3_Interrupt_Enable_Reg;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

enum Gi_Auto_Increment_Reg_Bits {
	Gi_Auto_Increment_Mask = 0xff
};

#define Gi_Up_Down_Shift 5
enum Gi_Command_Reg_Bits {
	Gi_Arm_Bit = 0x1,
	Gi_Save_Trace_Bit = 0x2,
	Gi_Load_Bit = 0x4,
	Gi_Disarm_Bit = 0x10,
	Gi_Up_Down_Mask = 0x3 << Gi_Up_Down_Shift,
	Gi_Always_Down_Bits = 0x0 << Gi_Up_Down_Shift,
	Gi_Always_Up_Bits = 0x1 << Gi_Up_Down_Shift,
	Gi_Up_Down_Hardware_IO_Bits = 0x2 << Gi_Up_Down_Shift,
	Gi_Up_Down_Hardware_Gate_Bits = 0x3 << Gi_Up_Down_Shift,
	Gi_Write_Switch_Bit = 0x80,
	Gi_Synchronize_Gate_Bit = 0x100,
	Gi_Little_Big_Endian_Bit = 0x200,
	Gi_Bank_Switch_Start_Bit = 0x400,
	Gi_Bank_Switch_Mode_Bit = 0x800,
	Gi_Bank_Switch_Enable_Bit = 0x1000,
	Gi_Arm_Copy_Bit = 0x2000,
	Gi_Save_Trace_Copy_Bit = 0x4000,
	Gi_Disarm_Copy_Bit = 0x8000
};

#define Gi_Index_Phase_Bitshift 5
#define Gi_HW_Arm_Select_Shift 8
enum Gi_Counting_Mode_Reg_Bits {
	Gi_Counting_Mode_Mask = 0x7,
	Gi_Counting_Mode_Normal_Bits = 0x0,
	Gi_Counting_Mode_QuadratureX1_Bits = 0x1,
	Gi_Counting_Mode_QuadratureX2_Bits = 0x2,
	Gi_Counting_Mode_QuadratureX4_Bits = 0x3,
	Gi_Counting_Mode_Two_Pulse_Bits = 0x4,
	Gi_Counting_Mode_Sync_Source_Bits = 0x6,
	Gi_Index_Mode_Bit = 0x10,
	Gi_Index_Phase_Mask = 0x3 << Gi_Index_Phase_Bitshift,
	Gi_Index_Phase_LowA_LowB = 0x0 << Gi_Index_Phase_Bitshift,
	Gi_Index_Phase_LowA_HighB = 0x1 << Gi_Index_Phase_Bitshift,
	Gi_Index_Phase_HighA_LowB = 0x2 << Gi_Index_Phase_Bitshift,
	Gi_Index_Phase_HighA_HighB = 0x3 << Gi_Index_Phase_Bitshift,
	Gi_HW_Arm_Enable_Bit = 0x80,	/* from m-series example code, not documented in 660x register level manual */
	Gi_660x_HW_Arm_Select_Mask = 0x7 << Gi_HW_Arm_Select_Shift,	/* from m-series example code, not documented in 660x register level manual */
	Gi_660x_Prescale_X8_Bit = 0x1000,
	Gi_M_Series_Prescale_X8_Bit = 0x2000,
	Gi_M_Series_HW_Arm_Select_Mask = 0x1f << Gi_HW_Arm_Select_Shift,
	/* must be set for clocks over 40MHz, which includes synchronous counting and quadrature modes */
	Gi_660x_Alternate_Sync_Bit = 0x2000,
	Gi_M_Series_Alternate_Sync_Bit = 0x4000,
	Gi_660x_Prescale_X2_Bit = 0x4000,	/* from m-series example code, not documented in 660x register level manual */
	Gi_M_Series_Prescale_X2_Bit = 0x8000,
};

#define Gi_Source_Select_Shift 2
#define Gi_Gate_Select_Shift 7
enum Gi_Input_Select_Bits {
	Gi_Read_Acknowledges_Irq = 0x1,	/*  not present on 660x */
	Gi_Write_Acknowledges_Irq = 0x2,	/*  not present on 660x */
	Gi_Source_Select_Mask = 0x7c,
	Gi_Gate_Select_Mask = 0x1f << Gi_Gate_Select_Shift,
	Gi_Gate_Select_Load_Source_Bit = 0x1000,
	Gi_Or_Gate_Bit = 0x2000,
	Gi_Output_Polarity_Bit = 0x4000,	/* set to invert */
	Gi_Source_Polarity_Bit = 0x8000	/* set to invert */
};

enum Gi_Mode_Bits {
	Gi_Gating_Mode_Mask = 0x3,
	Gi_Gating_Disabled_Bits = 0x0,
	Gi_Level_Gating_Bits = 0x1,
	Gi_Rising_Edge_Gating_Bits = 0x2,
	Gi_Falling_Edge_Gating_Bits = 0x3,
	Gi_Gate_On_Both_Edges_Bit = 0x4,	/* used in conjunction with rising edge gating mode */
	Gi_Trigger_Mode_for_Edge_Gate_Mask = 0x18,
	Gi_Edge_Gate_Starts_Stops_Bits = 0x0,
	Gi_Edge_Gate_Stops_Starts_Bits = 0x8,
	Gi_Edge_Gate_Starts_Bits = 0x10,
	Gi_Edge_Gate_No_Starts_or_Stops_Bits = 0x18,
	Gi_Stop_Mode_Mask = 0x60,
	Gi_Stop_on_Gate_Bits = 0x00,
	Gi_Stop_on_Gate_or_TC_Bits = 0x20,
	Gi_Stop_on_Gate_or_Second_TC_Bits = 0x40,
	Gi_Load_Source_Select_Bit = 0x80,
	Gi_Output_Mode_Mask = 0x300,
	Gi_Output_TC_Pulse_Bits = 0x100,
	Gi_Output_TC_Toggle_Bits = 0x200,
	Gi_Output_TC_or_Gate_Toggle_Bits = 0x300,
	Gi_Counting_Once_Mask = 0xc00,
	Gi_No_Hardware_Disarm_Bits = 0x000,
	Gi_Disarm_at_TC_Bits = 0x400,
	Gi_Disarm_at_Gate_Bits = 0x800,
	Gi_Disarm_at_TC_or_Gate_Bits = 0xc00,
	Gi_Loading_On_TC_Bit = 0x1000,
	Gi_Gate_Polarity_Bit = 0x2000,
	Gi_Loading_On_Gate_Bit = 0x4000,
	Gi_Reload_Source_Switching_Bit = 0x8000
};

#define Gi_Second_Gate_Select_Shift 7
/*FIXME: m-series has a second gate subselect bit */
/*FIXME: m-series second gate sources are undocumented (by NI)*/
enum Gi_Second_Gate_Bits {
	Gi_Second_Gate_Mode_Bit = 0x1,
	Gi_Second_Gate_Select_Mask = 0x1f << Gi_Second_Gate_Select_Shift,
	Gi_Second_Gate_Polarity_Bit = 0x2000,
	Gi_Second_Gate_Subselect_Bit = 0x4000,	/* m-series only */
	Gi_Source_Subselect_Bit = 0x8000	/* m-series only */
};
static inline unsigned Gi_Second_Gate_Select_Bits(unsigned second_gate_select)
{
	return (second_gate_select << Gi_Second_Gate_Select_Shift) &
	    Gi_Second_Gate_Select_Mask;
}

enum Gxx_Status_Bits {
	G0_Save_Bit = 0x1,
	G1_Save_Bit = 0x2,
	G0_Counting_Bit = 0x4,
	G1_Counting_Bit = 0x8,
	G0_Next_Load_Source_Bit = 0x10,
	G1_Next_Load_Source_Bit = 0x20,
	G0_Stale_Data_Bit = 0x40,
	G1_Stale_Data_Bit = 0x80,
	G0_Armed_Bit = 0x100,
	G1_Armed_Bit = 0x200,
	G0_No_Load_Between_Gates_Bit = 0x400,
	G1_No_Load_Between_Gates_Bit = 0x800,
	G0_TC_Error_Bit = 0x1000,
	G1_TC_Error_Bit = 0x2000,
	G0_Gate_Error_Bit = 0x4000,
	G1_Gate_Error_Bit = 0x8000
};
static inline enum Gxx_Status_Bits Gi_Counting_Bit(unsigned counter_index)
{
	if (counter_index % 2)
		return G1_Counting_Bit;
	return G0_Counting_Bit;
}

static inline enum Gxx_Status_Bits Gi_Armed_Bit(unsigned counter_index)
{
	if (counter_index % 2)
		return G1_Armed_Bit;
	return G0_Armed_Bit;
}

static inline enum Gxx_Status_Bits Gi_Next_Load_Source_Bit(unsigned
							   counter_index)
{
	if (counter_index % 2)
		return G1_Next_Load_Source_Bit;
	return G0_Next_Load_Source_Bit;
}

static inline enum Gxx_Status_Bits Gi_Stale_Data_Bit(unsigned counter_index)
{
	if (counter_index % 2)
		return G1_Stale_Data_Bit;
	return G0_Stale_Data_Bit;
}

static inline enum Gxx_Status_Bits Gi_TC_Error_Bit(unsigned counter_index)
{
	if (counter_index % 2)
		return G1_TC_Error_Bit;
	return G0_TC_Error_Bit;
}

static inline enum Gxx_Status_Bits Gi_Gate_Error_Bit(unsigned counter_index)
{
	if (counter_index % 2)
		return G1_Gate_Error_Bit;
	return G0_Gate_Error_Bit;
}

/* joint reset register bits */
static inline unsigned Gi_Reset_Bit(unsigned counter_index)
{
	return 0x1 << (2 + (counter_index % 2));
}

enum Gxx_Joint_Status2_Bits {
	G0_Output_Bit = 0x1,
	G1_Output_Bit = 0x2,
	G0_HW_Save_Bit = 0x1000,
	G1_HW_Save_Bit = 0x2000,
	G0_Permanent_Stale_Bit = 0x4000,
	G1_Permanent_Stale_Bit = 0x8000
};
static inline enum Gxx_Joint_Status2_Bits Gi_Permanent_Stale_Bit(unsigned
								 counter_index)
{
	if (counter_index % 2)
		return G1_Permanent_Stale_Bit;
	return G0_Permanent_Stale_Bit;
}

enum Gi_DMA_Config_Reg_Bits {
	Gi_DMA_Enable_Bit = 0x1,
	Gi_DMA_Write_Bit = 0x2,
	Gi_DMA_Int_Bit = 0x4
};

enum Gi_DMA_Status_Reg_Bits {
	Gi_DMA_Readbank_Bit = 0x2000,
	Gi_DRQ_Error_Bit = 0x4000,
	Gi_DRQ_Status_Bit = 0x8000
};

enum G02_Interrupt_Acknowledge_Bits {
	G0_Gate_Error_Confirm_Bit = 0x20,
	G0_TC_Error_Confirm_Bit = 0x40
};
enum G13_Interrupt_Acknowledge_Bits {
	G1_Gate_Error_Confirm_Bit = 0x2,
	G1_TC_Error_Confirm_Bit = 0x4
};
static inline unsigned Gi_Gate_Error_Confirm_Bit(unsigned counter_index)
{
	if (counter_index % 2)
		return G1_Gate_Error_Confirm_Bit;
	return G0_Gate_Error_Confirm_Bit;
}

static inline unsigned Gi_TC_Error_Confirm_Bit(unsigned counter_index)
{
	if (counter_index % 2)
		return G1_TC_Error_Confirm_Bit;
	return G0_TC_Error_Confirm_Bit;
}

/* bits that are the same in G0/G2 and G1/G3 interrupt acknowledge registers */
enum Gxx_Interrupt_Acknowledge_Bits {
	Gi_TC_Interrupt_Ack_Bit = 0x4000,
	Gi_Gate_Interrupt_Ack_Bit = 0x8000
};

enum Gi_Status_Bits {
	Gi_Gate_Interrupt_Bit = 0x4,
	Gi_TC_Bit = 0x8,
	Gi_Interrupt_Bit = 0x8000
};

enum G02_Interrupt_Enable_Bits {
	G0_TC_Interrupt_Enable_Bit = 0x40,
	G0_Gate_Interrupt_Enable_Bit = 0x100
};
enum G13_Interrupt_Enable_Bits {
	G1_TC_Interrupt_Enable_Bit = 0x200,
	G1_Gate_Interrupt_Enable_Bit = 0x400
};
static inline unsigned Gi_Gate_Interrupt_Enable_Bit(unsigned counter_index)
{
	unsigned bit;

	if (counter_index % 2) {
		bit = G1_Gate_Interrupt_Enable_Bit;
	} else {
		bit = G0_Gate_Interrupt_Enable_Bit;
	}
	return bit;
}

static inline void write_register(struct ni_gpct *counter, unsigned bits,
				  enum ni_gpct_register reg)
{
	BUG_ON(reg >= NITIO_Num_Registers);
	counter->counter_dev->write_register(counter, bits, reg);
}

static inline unsigned read_register(struct ni_gpct *counter,
				     enum ni_gpct_register reg)
{
	BUG_ON(reg >= NITIO_Num_Registers);
	return counter->counter_dev->read_register(counter, reg);
}

static inline int ni_tio_counting_mode_registers_present(const struct
							 ni_gpct_device
							 *counter_dev)
{
	switch (counter_dev->variant) {
	case ni_gpct_variant_e_series:
		return 0;
		break;
	case ni_gpct_variant_m_series:
	case ni_gpct_variant_660x:
		return 1;
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static inline void ni_tio_set_bits_transient(struct ni_gpct *counter,
					     enum ni_gpct_register
					     register_index, unsigned bit_mask,
					     unsigned bit_values,
					     unsigned transient_bit_values)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned long flags;

	BUG_ON(register_index >= NITIO_Num_Registers);
	spin_lock_irqsave(&counter_dev->regs_lock, flags);
	counter_dev->regs[register_index] &= ~bit_mask;
	counter_dev->regs[register_index] |= (bit_values & bit_mask);
	write_register(counter,
		       counter_dev->regs[register_index] | transient_bit_values,
		       register_index);
	mmiowb();
	spin_unlock_irqrestore(&counter_dev->regs_lock, flags);
}

/* ni_tio_set_bits( ) is for safely writing to registers whose bits may be
twiddled in interrupt context, or whose software copy may be read in interrupt context.
*/
static inline void ni_tio_set_bits(struct ni_gpct *counter,
				   enum ni_gpct_register register_index,
				   unsigned bit_mask, unsigned bit_values)
{
	ni_tio_set_bits_transient(counter, register_index, bit_mask, bit_values,
				  0x0);
}

/* ni_tio_get_soft_copy( ) is for safely reading the software copy of a register
whose bits might be modified in interrupt context, or whose software copy
might need to be read in interrupt context.
*/
static inline unsigned ni_tio_get_soft_copy(const struct ni_gpct *counter,
					    enum ni_gpct_register
					    register_index)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned long flags;
	unsigned value;

	BUG_ON(register_index >= NITIO_Num_Registers);
	spin_lock_irqsave(&counter_dev->regs_lock, flags);
	value = counter_dev->regs[register_index];
	spin_unlock_irqrestore(&counter_dev->regs_lock, flags);
	return value;
}

int ni_tio_arm(struct ni_gpct *counter, int arm, unsigned start_trigger);
int ni_tio_set_gate_src(struct ni_gpct *counter, unsigned gate_index,
			unsigned int gate_source);

#endif /* _COMEDI_NI_TIO_INTERNAL_H */
