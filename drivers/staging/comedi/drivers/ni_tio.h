// SPDX-License-Identifier: GPL-2.0+
/*
 * Header file for NI general purpose counter support code (ni_tio.c)
 *
 * COMEDI - Linux Control and Measurement Device Interface
 */

#ifndef _COMEDI_NI_TIO_H
#define _COMEDI_NI_TIO_H

#include "../comedidev.h"

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
	unsigned int counter_index;
	unsigned int chip_index;
	u64 clock_period_ps;	/* clock period in picoseconds */
	struct mite_channel *mite_chan;
	spinlock_t lock;	/* protects 'mite_chan' */
};

struct ni_gpct_device {
	struct comedi_device *dev;
	void (*write)(struct ni_gpct *counter, unsigned int value,
		      enum ni_gpct_register);
	unsigned int (*read)(struct ni_gpct *counter, enum ni_gpct_register);
	enum ni_gpct_variant variant;
	struct ni_gpct *counters;
	unsigned int num_counters;
	unsigned int counters_per_chip;
	unsigned int regs[NITIO_NUM_REGS];
	spinlock_t regs_lock;		/* protects 'regs' */
	const struct ni_route_tables *routing_tables; /* link to routes */
};

struct ni_gpct_device *
ni_gpct_device_construct(struct comedi_device *dev,
			 void (*write)(struct ni_gpct *counter,
				       unsigned int value,
				       enum ni_gpct_register),
			 unsigned int (*read)(struct ni_gpct *counter,
					      enum ni_gpct_register),
			 enum ni_gpct_variant,
			 unsigned int num_counters,
			 unsigned int counters_per_chip,
			 const struct ni_route_tables *routing_tables);
void ni_gpct_device_destroy(struct ni_gpct_device *counter_dev);
void ni_tio_init_counter(struct ni_gpct *counter);
int ni_tio_insn_read(struct comedi_device *dev, struct comedi_subdevice *s,
		     struct comedi_insn *insn, unsigned int *data);
int ni_tio_insn_config(struct comedi_device *dev, struct comedi_subdevice *s,
		       struct comedi_insn *insn, unsigned int *data);
int ni_tio_insn_write(struct comedi_device *dev, struct comedi_subdevice *s,
		      struct comedi_insn *insn, unsigned int *data);
int ni_tio_cmd(struct comedi_device *dev, struct comedi_subdevice *s);
int ni_tio_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
		   struct comedi_cmd *cmd);
int ni_tio_cancel(struct ni_gpct *counter);
void ni_tio_handle_interrupt(struct ni_gpct *counter,
			     struct comedi_subdevice *s);
void ni_tio_set_mite_channel(struct ni_gpct *counter,
			     struct mite_channel *mite_chan);
void ni_tio_acknowledge(struct ni_gpct *counter);

/*
 * Retrieves the register value of the current source of the output selector for
 * the given destination.
 *
 * If the terminal for the destination is not already configured as an output,
 * this function returns -EINVAL as error.
 *
 * Return: the register value of the destination output selector;
 *         -EINVAL if terminal is not configured for output.
 */
int ni_tio_get_routing(struct ni_gpct_device *counter_dev,
		       unsigned int destination);

/*
 * Sets the register value of the selector MUX for the given destination.
 * @counter_dev:Pointer to general counter device.
 * @destination:Device-global identifier of route destination.
 * @register_value:
 *		The first several bits of this value should store the desired
 *		value to write to the register.  All other bits are for
 *		transmitting information that modify the mode of the particular
 *		destination/gate.  These mode bits might include a bitwise or of
 *		CR_INVERT and CR_EDGE.  Note that the calling function should
 *		have already validated the correctness of this value.
 */
int ni_tio_set_routing(struct ni_gpct_device *counter_dev,
		       unsigned int destination, unsigned int register_value);

/*
 * Sets the given destination MUX to its default value or disable it.
 *
 * Return: 0 if successful; -EINVAL if terminal is unknown.
 */
int ni_tio_unset_routing(struct ni_gpct_device *counter_dev,
			 unsigned int destination);

#endif /* _COMEDI_NI_TIO_H */
