/*
    drivers/ni_tio.h
    Header file for NI general purpose counter support code (ni_tio.c)

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

#ifndef _COMEDI_NI_TIO_H
#define _COMEDI_NI_TIO_H

#include "../comedidev.h"

// forward declarations
struct mite_struct;
struct ni_gpct_device;

enum ni_gpct_register {
	NITIO_G0_Autoincrement_Reg,
	NITIO_G1_Autoincrement_Reg,
	NITIO_G2_Autoincrement_Reg,
	NITIO_G3_Autoincrement_Reg,
	NITIO_G0_Command_Reg,
	NITIO_G1_Command_Reg,
	NITIO_G2_Command_Reg,
	NITIO_G3_Command_Reg,
	NITIO_G0_HW_Save_Reg,
	NITIO_G1_HW_Save_Reg,
	NITIO_G2_HW_Save_Reg,
	NITIO_G3_HW_Save_Reg,
	NITIO_G0_SW_Save_Reg,
	NITIO_G1_SW_Save_Reg,
	NITIO_G2_SW_Save_Reg,
	NITIO_G3_SW_Save_Reg,
	NITIO_G0_Mode_Reg,
	NITIO_G1_Mode_Reg,
	NITIO_G2_Mode_Reg,
	NITIO_G3_Mode_Reg,
	NITIO_G0_LoadA_Reg,
	NITIO_G1_LoadA_Reg,
	NITIO_G2_LoadA_Reg,
	NITIO_G3_LoadA_Reg,
	NITIO_G0_LoadB_Reg,
	NITIO_G1_LoadB_Reg,
	NITIO_G2_LoadB_Reg,
	NITIO_G3_LoadB_Reg,
	NITIO_G0_Input_Select_Reg,
	NITIO_G1_Input_Select_Reg,
	NITIO_G2_Input_Select_Reg,
	NITIO_G3_Input_Select_Reg,
	NITIO_G0_Counting_Mode_Reg,
	NITIO_G1_Counting_Mode_Reg,
	NITIO_G2_Counting_Mode_Reg,
	NITIO_G3_Counting_Mode_Reg,
	NITIO_G0_Second_Gate_Reg,
	NITIO_G1_Second_Gate_Reg,
	NITIO_G2_Second_Gate_Reg,
	NITIO_G3_Second_Gate_Reg,
	NITIO_G01_Status_Reg,
	NITIO_G23_Status_Reg,
	NITIO_G01_Joint_Reset_Reg,
	NITIO_G23_Joint_Reset_Reg,
	NITIO_G01_Joint_Status1_Reg,
	NITIO_G23_Joint_Status1_Reg,
	NITIO_G01_Joint_Status2_Reg,
	NITIO_G23_Joint_Status2_Reg,
	NITIO_G0_DMA_Config_Reg,
	NITIO_G1_DMA_Config_Reg,
	NITIO_G2_DMA_Config_Reg,
	NITIO_G3_DMA_Config_Reg,
	NITIO_G0_DMA_Status_Reg,
	NITIO_G1_DMA_Status_Reg,
	NITIO_G2_DMA_Status_Reg,
	NITIO_G3_DMA_Status_Reg,
	NITIO_G0_ABZ_Reg,
	NITIO_G1_ABZ_Reg,
	NITIO_G0_Interrupt_Acknowledge_Reg,
	NITIO_G1_Interrupt_Acknowledge_Reg,
	NITIO_G2_Interrupt_Acknowledge_Reg,
	NITIO_G3_Interrupt_Acknowledge_Reg,
	NITIO_G0_Status_Reg,
	NITIO_G1_Status_Reg,
	NITIO_G2_Status_Reg,
	NITIO_G3_Status_Reg,
	NITIO_G0_Interrupt_Enable_Reg,
	NITIO_G1_Interrupt_Enable_Reg,
	NITIO_G2_Interrupt_Enable_Reg,
	NITIO_G3_Interrupt_Enable_Reg,
	NITIO_Num_Registers,
};

enum ni_gpct_variant {
	ni_gpct_variant_e_series,
	ni_gpct_variant_m_series,
	ni_gpct_variant_660x
};

struct ni_gpct {
	struct ni_gpct_device *counter_dev;
	unsigned counter_index;
	unsigned chip_index;
	uint64_t clock_period_ps;	/* clock period in picoseconds */
	struct mite_channel *mite_chan;
	spinlock_t lock;
};

struct ni_gpct_device {
	struct comedi_device *dev;
	void (*write_register) (struct ni_gpct * counter, unsigned bits,
		enum ni_gpct_register reg);
	unsigned (*read_register) (struct ni_gpct * counter,
		enum ni_gpct_register reg);
	enum ni_gpct_variant variant;
	struct ni_gpct *counters;
	unsigned num_counters;
	unsigned regs[NITIO_Num_Registers];
	spinlock_t regs_lock;
};

extern struct ni_gpct_device *ni_gpct_device_construct(struct comedi_device * dev,
	void (*write_register) (struct ni_gpct * counter, unsigned bits,
		enum ni_gpct_register reg),
	unsigned (*read_register) (struct ni_gpct * counter,
		enum ni_gpct_register reg), enum ni_gpct_variant variant,
	unsigned num_counters);
extern void ni_gpct_device_destroy(struct ni_gpct_device *counter_dev);
extern void ni_tio_init_counter(struct ni_gpct *counter);
extern int ni_tio_rinsn(struct ni_gpct *counter,
	struct comedi_insn * insn, unsigned int * data);
extern int ni_tio_insn_config(struct ni_gpct *counter,
	struct comedi_insn * insn, unsigned int * data);
extern int ni_tio_winsn(struct ni_gpct *counter,
	struct comedi_insn * insn, unsigned int * data);
extern int ni_tio_cmd(struct ni_gpct *counter, struct comedi_async *async);
extern int ni_tio_cmdtest(struct ni_gpct *counter, struct comedi_cmd * cmd);
extern int ni_tio_cancel(struct ni_gpct *counter);
extern void ni_tio_handle_interrupt(struct ni_gpct *counter,
	struct comedi_subdevice * s);
extern void ni_tio_set_mite_channel(struct ni_gpct *counter,
	struct mite_channel *mite_chan);
extern void ni_tio_acknowledge_and_confirm(struct ni_gpct *counter,
	int *gate_error, int *tc_error, int *perm_stale_data, int *stale_data);

static inline struct ni_gpct *subdev_to_counter(struct comedi_subdevice * s)
{
	return s->private;
}

#endif /* _COMEDI_NI_TIO_H */
