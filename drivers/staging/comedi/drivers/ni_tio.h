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
*/

#ifndef _COMEDI_NI_TIO_H
#define _COMEDI_NI_TIO_H

#include "../comedidev.h"

/* forward declarations */
struct mite_struct;
struct ni_gpct_device;

enum ni_gpct_register {
	NITIO_G0_AUTO_INC,
	NITIO_G1_AUTO_INC,
	NITIO_G2_AUTO_INC,
	NITIO_G3_AUTO_INC,
	NITIO_G0_CMD,
	NITIO_G1_CMD,
	NITIO_G2_CMD,
	NITIO_G3_CMD,
	NITIO_G0_HW_SAVE,
	NITIO_G1_HW_SAVE,
	NITIO_G2_HW_SAVE,
	NITIO_G3_HW_SAVE,
	NITIO_G0_SW_SAVE,
	NITIO_G1_SW_SAVE,
	NITIO_G2_SW_SAVE,
	NITIO_G3_SW_SAVE,
	NITIO_G0_MODE,
	NITIO_G1_MODE,
	NITIO_G2_MODE,
	NITIO_G3_MODE,
	NITIO_G0_LOADA,
	NITIO_G1_LOADA,
	NITIO_G2_LOADA,
	NITIO_G3_LOADA,
	NITIO_G0_LOADB,
	NITIO_G1_LOADB,
	NITIO_G2_LOADB,
	NITIO_G3_LOADB,
	NITIO_G0_INPUT_SEL,
	NITIO_G1_INPUT_SEL,
	NITIO_G2_INPUT_SEL,
	NITIO_G3_INPUT_SEL,
	NITIO_G0_CNT_MODE,
	NITIO_G1_CNT_MODE,
	NITIO_G2_CNT_MODE,
	NITIO_G3_CNT_MODE,
	NITIO_G0_GATE2,
	NITIO_G1_GATE2,
	NITIO_G2_GATE2,
	NITIO_G3_GATE2,
	NITIO_G01_STATUS,
	NITIO_G23_STATUS,
	NITIO_G01_RESET,
	NITIO_G23_RESET,
	NITIO_G01_STATUS1,
	NITIO_G23_STATUS1,
	NITIO_G01_STATUS2,
	NITIO_G23_STATUS2,
	NITIO_G0_DMA_CFG,
	NITIO_G1_DMA_CFG,
	NITIO_G2_DMA_CFG,
	NITIO_G3_DMA_CFG,
	NITIO_G0_DMA_STATUS,
	NITIO_G1_DMA_STATUS,
	NITIO_G2_DMA_STATUS,
	NITIO_G3_DMA_STATUS,
	NITIO_G0_ABZ,
	NITIO_G1_ABZ,
	NITIO_G0_INT_ACK,
	NITIO_G1_INT_ACK,
	NITIO_G2_INT_ACK,
	NITIO_G3_INT_ACK,
	NITIO_G0_STATUS,
	NITIO_G1_STATUS,
	NITIO_G2_STATUS,
	NITIO_G3_STATUS,
	NITIO_G0_INT_ENA,
	NITIO_G1_INT_ENA,
	NITIO_G2_INT_ENA,
	NITIO_G3_INT_ENA,
	NITIO_NUM_REGS,
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
	void (*write_register)(struct ni_gpct *counter, unsigned bits,
			       enum ni_gpct_register reg);
	unsigned (*read_register)(struct ni_gpct *counter,
				  enum ni_gpct_register reg);
	enum ni_gpct_variant variant;
	struct ni_gpct *counters;
	unsigned num_counters;
	unsigned regs[NITIO_NUM_REGS];
	spinlock_t regs_lock;
};

struct ni_gpct_device *
ni_gpct_device_construct(struct comedi_device *,
			 void (*write_register)(struct ni_gpct *,
						unsigned bits,
						enum ni_gpct_register),
			 unsigned (*read_register)(struct ni_gpct *,
						   enum ni_gpct_register),
			 enum ni_gpct_variant,
			 unsigned num_counters);
void ni_gpct_device_destroy(struct ni_gpct_device *);
void ni_tio_init_counter(struct ni_gpct *);
int ni_tio_insn_read(struct comedi_device *, struct comedi_subdevice *,
		     struct comedi_insn *, unsigned int *data);
int ni_tio_insn_config(struct comedi_device *, struct comedi_subdevice *,
		       struct comedi_insn *, unsigned int *data);
int ni_tio_insn_write(struct comedi_device *, struct comedi_subdevice *,
		      struct comedi_insn *, unsigned int *data);
int ni_tio_cmd(struct comedi_device *, struct comedi_subdevice *);
int ni_tio_cmdtest(struct comedi_device *, struct comedi_subdevice *,
		   struct comedi_cmd *);
int ni_tio_cancel(struct ni_gpct *);
void ni_tio_handle_interrupt(struct ni_gpct *, struct comedi_subdevice *);
void ni_tio_set_mite_channel(struct ni_gpct *, struct mite_channel *);
void ni_tio_acknowledge(struct ni_gpct *);

#endif /* _COMEDI_NI_TIO_H */
