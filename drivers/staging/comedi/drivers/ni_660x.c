/*
  comedi/drivers/ni_660x.c
  Hardware driver for NI 660x devices

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
*/

/*
 * Driver: ni_660x
 * Description: National Instruments 660x counter/timer boards
 * Devices: [National Instruments] PCI-6601 (ni_660x), PCI-6602, PXI-6602,
 *   PXI-6608, PXI-6624
 * Author: J.P. Mellor <jpmellor@rose-hulman.edu>,
 *   Herman.Bruyninckx@mech.kuleuven.ac.be,
 *   Wim.Meeussen@mech.kuleuven.ac.be,
 *   Klaas.Gadeyne@mech.kuleuven.ac.be,
 *   Frank Mori Hess <fmhess@users.sourceforge.net>
 * Updated: Fri, 15 Mar 2013 10:47:56 +0000
 * Status: experimental
 *
 * Encoders work.  PulseGeneration (both single pulse and pulse train)
 * works.  Buffered commands work for input but not output.
 * 
 * References:
 * DAQ 660x Register-Level Programmer Manual  (NI 370505A-01)
 * DAQ 6601/6602 User Manual (NI 322137B-01)
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "comedi_fc.h"
#include "mite.h"
#include "ni_tio.h"

enum ni_660x_constants {
	min_counter_pfi_chan = 8,
	max_dio_pfi_chan = 31,
	counters_per_chip = 4
};

#define NUM_PFI_CHANNELS 40
/* really there are only up to 3 dma channels, but the register layout allows
for 4 */
#define MAX_DMA_CHANNEL 4

/* See Register-Level Programmer Manual page 3.1 */
enum ni_660x_register {
	NI660X_G0_INT_ACK,
	NI660X_G0_STATUS,
	NI660X_G1_INT_ACK,
	NI660X_G1_STATUS,
	NI660X_G01_STATUS,
	NI660X_G0_CMD,
	NI660X_STC_DIO_PARALLEL_INPUT,
	NI660X_G1_CMD,
	NI660X_G0_HW_SAVE,
	NI660X_G1_HW_SAVE,
	NI660X_STC_DIO_OUTPUT,
	NI660X_STC_DIO_CONTROL,
	NI660X_G0_SW_SAVE,
	NI660X_G1_SW_SAVE,
	NI660X_G0_MODE,
	NI660X_G01_STATUS1,
	NI660X_G1_MODE,
	NI660X_STC_DIO_SERIAL_INPUT,
	NI660X_G0_LOADA,
	NI660X_G01_STATUS2,
	NI660X_G0_LOADB,
	NI660X_G1_LOADA,
	NI660X_G1_LOADB,
	NI660X_G0_INPUT_SEL,
	NI660X_G1_INPUT_SEL,
	NI660X_G0_AUTO_INC,
	NI660X_G1_AUTO_INC,
	NI660X_G01_RESET,
	NI660X_G0_INT_ENA,
	NI660X_G1_INT_ENA,
	NI660X_G0_CNT_MODE,
	NI660X_G1_CNT_MODE,
	NI660X_G0_GATE2,
	NI660X_G1_GATE2,
	NI660X_G0_DMA_CFG,
	NI660X_G0_DMA_STATUS,
	NI660X_G1_DMA_CFG,
	NI660X_G1_DMA_STATUS,
	NI660X_G2_INT_ACK,
	NI660X_G2_STATUS,
	NI660X_G3_INT_ACK,
	NI660X_G3_STATUS,
	NI660X_G23_STATUS,
	NI660X_G2_CMD,
	NI660X_G3_CMD,
	NI660X_G2_HW_SAVE,
	NI660X_G3_HW_SAVE,
	NI660X_G2_SW_SAVE,
	NI660X_G3_SW_SAVE,
	NI660X_G2_MODE,
	NI660X_G23_STATUS1,
	NI660X_G3_MODE,
	NI660X_G2_LOADA,
	NI660X_G23_STATUS2,
	NI660X_G2_LOADB,
	NI660X_G3_LOADA,
	NI660X_G3_LOADB,
	NI660X_G2_INPUT_SEL,
	NI660X_G3_INPUT_SEL,
	NI660X_G2_AUTO_INC,
	NI660X_G3_AUTO_INC,
	NI660X_G23_RESET,
	NI660X_G2_INT_ENA,
	NI660X_G3_INT_ENA,
	NI660X_G2_CNT_MODE,
	NI660X_G3_CNT_MODE,
	NI660X_G3_GATE2,
	NI660X_G2_GATE2,
	NI660X_G2_DMA_CFG,
	NI660X_G2_DMA_STATUS,
	NI660X_G3_DMA_CFG,
	NI660X_G3_DMA_STATUS,
	NI660X_DIO32_INPUT,
	NI660X_DIO32_OUTPUT,
	NI660X_CLK_CFG,
	NI660X_GLOBAL_INT_STATUS,
	NI660X_DMA_CFG,
	NI660X_GLOBAL_INT_CFG,
	NI660X_IO_CFG_0_1,
	NI660X_IO_CFG_2_3,
	NI660X_IO_CFG_4_5,
	NI660X_IO_CFG_6_7,
	NI660X_IO_CFG_8_9,
	NI660X_IO_CFG_10_11,
	NI660X_IO_CFG_12_13,
	NI660X_IO_CFG_14_15,
	NI660X_IO_CFG_16_17,
	NI660X_IO_CFG_18_19,
	NI660X_IO_CFG_20_21,
	NI660X_IO_CFG_22_23,
	NI660X_IO_CFG_24_25,
	NI660X_IO_CFG_26_27,
	NI660X_IO_CFG_28_29,
	NI660X_IO_CFG_30_31,
	NI660X_IO_CFG_32_33,
	NI660X_IO_CFG_34_35,
	NI660X_IO_CFG_36_37,
	NI660X_IO_CFG_38_39,
	NI660X_NUM_REGS,
};

static inline unsigned IOConfigReg(unsigned pfi_channel)
{
	unsigned reg = NI660X_IO_CFG_0_1 + pfi_channel / 2;
	BUG_ON(reg > NI660X_IO_CFG_38_39);
	return reg;
}

enum ni_660x_register_width {
	DATA_1B,
	DATA_2B,
	DATA_4B
};

enum ni_660x_register_direction {
	NI_660x_READ,
	NI_660x_WRITE,
	NI_660x_READ_WRITE
};

enum ni_660x_pfi_output_select {
	pfi_output_select_high_Z = 0,
	pfi_output_select_counter = 1,
	pfi_output_select_do = 2,
	num_pfi_output_selects
};

enum ni_660x_subdevices {
	NI_660X_DIO_SUBDEV = 1,
	NI_660X_GPCT_SUBDEV_0 = 2
};
static inline unsigned NI_660X_GPCT_SUBDEV(unsigned index)
{
	return NI_660X_GPCT_SUBDEV_0 + index;
}

struct NI_660xRegisterData {

	const char *name;	/*  Register Name */
	int offset;		/*  Offset from base address from GPCT chip */
	enum ni_660x_register_direction direction;
	enum ni_660x_register_width size; /* 1 byte, 2 bytes, or 4 bytes */
};

static const struct NI_660xRegisterData registerData[NI660X_NUM_REGS] = {
	{"G0 Interrupt Acknowledge", 0x004, NI_660x_WRITE, DATA_2B},
	{"G0 Status Register", 0x004, NI_660x_READ, DATA_2B},
	{"G1 Interrupt Acknowledge", 0x006, NI_660x_WRITE, DATA_2B},
	{"G1 Status Register", 0x006, NI_660x_READ, DATA_2B},
	{"G01 Status Register ", 0x008, NI_660x_READ, DATA_2B},
	{"G0 Command Register", 0x00C, NI_660x_WRITE, DATA_2B},
	{"STC DIO Parallel Input", 0x00E, NI_660x_READ, DATA_2B},
	{"G1 Command Register", 0x00E, NI_660x_WRITE, DATA_2B},
	{"G0 HW Save Register", 0x010, NI_660x_READ, DATA_4B},
	{"G1 HW Save Register", 0x014, NI_660x_READ, DATA_4B},
	{"STC DIO Output", 0x014, NI_660x_WRITE, DATA_2B},
	{"STC DIO Control", 0x016, NI_660x_WRITE, DATA_2B},
	{"G0 SW Save Register", 0x018, NI_660x_READ, DATA_4B},
	{"G1 SW Save Register", 0x01C, NI_660x_READ, DATA_4B},
	{"G0 Mode Register", 0x034, NI_660x_WRITE, DATA_2B},
	{"G01 Joint Status 1 Register", 0x036, NI_660x_READ, DATA_2B},
	{"G1 Mode Register", 0x036, NI_660x_WRITE, DATA_2B},
	{"STC DIO Serial Input", 0x038, NI_660x_READ, DATA_2B},
	{"G0 Load A Register", 0x038, NI_660x_WRITE, DATA_4B},
	{"G01 Joint Status 2 Register", 0x03A, NI_660x_READ, DATA_2B},
	{"G0 Load B Register", 0x03C, NI_660x_WRITE, DATA_4B},
	{"G1 Load A Register", 0x040, NI_660x_WRITE, DATA_4B},
	{"G1 Load B Register", 0x044, NI_660x_WRITE, DATA_4B},
	{"G0 Input Select Register", 0x048, NI_660x_WRITE, DATA_2B},
	{"G1 Input Select Register", 0x04A, NI_660x_WRITE, DATA_2B},
	{"G0 Autoincrement Register", 0x088, NI_660x_WRITE, DATA_2B},
	{"G1 Autoincrement Register", 0x08A, NI_660x_WRITE, DATA_2B},
	{"G01 Joint Reset Register", 0x090, NI_660x_WRITE, DATA_2B},
	{"G0 Interrupt Enable", 0x092, NI_660x_WRITE, DATA_2B},
	{"G1 Interrupt Enable", 0x096, NI_660x_WRITE, DATA_2B},
	{"G0 Counting Mode Register", 0x0B0, NI_660x_WRITE, DATA_2B},
	{"G1 Counting Mode Register", 0x0B2, NI_660x_WRITE, DATA_2B},
	{"G0 Second Gate Register", 0x0B4, NI_660x_WRITE, DATA_2B},
	{"G1 Second Gate Register", 0x0B6, NI_660x_WRITE, DATA_2B},
	{"G0 DMA Config Register", 0x0B8, NI_660x_WRITE, DATA_2B},
	{"G0 DMA Status Register", 0x0B8, NI_660x_READ, DATA_2B},
	{"G1 DMA Config Register", 0x0BA, NI_660x_WRITE, DATA_2B},
	{"G1 DMA Status Register", 0x0BA, NI_660x_READ, DATA_2B},
	{"G2 Interrupt Acknowledge", 0x104, NI_660x_WRITE, DATA_2B},
	{"G2 Status Register", 0x104, NI_660x_READ, DATA_2B},
	{"G3 Interrupt Acknowledge", 0x106, NI_660x_WRITE, DATA_2B},
	{"G3 Status Register", 0x106, NI_660x_READ, DATA_2B},
	{"G23 Status Register", 0x108, NI_660x_READ, DATA_2B},
	{"G2 Command Register", 0x10C, NI_660x_WRITE, DATA_2B},
	{"G3 Command Register", 0x10E, NI_660x_WRITE, DATA_2B},
	{"G2 HW Save Register", 0x110, NI_660x_READ, DATA_4B},
	{"G3 HW Save Register", 0x114, NI_660x_READ, DATA_4B},
	{"G2 SW Save Register", 0x118, NI_660x_READ, DATA_4B},
	{"G3 SW Save Register", 0x11C, NI_660x_READ, DATA_4B},
	{"G2 Mode Register", 0x134, NI_660x_WRITE, DATA_2B},
	{"G23 Joint Status 1 Register", 0x136, NI_660x_READ, DATA_2B},
	{"G3 Mode Register", 0x136, NI_660x_WRITE, DATA_2B},
	{"G2 Load A Register", 0x138, NI_660x_WRITE, DATA_4B},
	{"G23 Joint Status 2 Register", 0x13A, NI_660x_READ, DATA_2B},
	{"G2 Load B Register", 0x13C, NI_660x_WRITE, DATA_4B},
	{"G3 Load A Register", 0x140, NI_660x_WRITE, DATA_4B},
	{"G3 Load B Register", 0x144, NI_660x_WRITE, DATA_4B},
	{"G2 Input Select Register", 0x148, NI_660x_WRITE, DATA_2B},
	{"G3 Input Select Register", 0x14A, NI_660x_WRITE, DATA_2B},
	{"G2 Autoincrement Register", 0x188, NI_660x_WRITE, DATA_2B},
	{"G3 Autoincrement Register", 0x18A, NI_660x_WRITE, DATA_2B},
	{"G23 Joint Reset Register", 0x190, NI_660x_WRITE, DATA_2B},
	{"G2 Interrupt Enable", 0x192, NI_660x_WRITE, DATA_2B},
	{"G3 Interrupt Enable", 0x196, NI_660x_WRITE, DATA_2B},
	{"G2 Counting Mode Register", 0x1B0, NI_660x_WRITE, DATA_2B},
	{"G3 Counting Mode Register", 0x1B2, NI_660x_WRITE, DATA_2B},
	{"G3 Second Gate Register", 0x1B6, NI_660x_WRITE, DATA_2B},
	{"G2 Second Gate Register", 0x1B4, NI_660x_WRITE, DATA_2B},
	{"G2 DMA Config Register", 0x1B8, NI_660x_WRITE, DATA_2B},
	{"G2 DMA Status Register", 0x1B8, NI_660x_READ, DATA_2B},
	{"G3 DMA Config Register", 0x1BA, NI_660x_WRITE, DATA_2B},
	{"G3 DMA Status Register", 0x1BA, NI_660x_READ, DATA_2B},
	{"32 bit Digital Input", 0x414, NI_660x_READ, DATA_4B},
	{"32 bit Digital Output", 0x510, NI_660x_WRITE, DATA_4B},
	{"Clock Config Register", 0x73C, NI_660x_WRITE, DATA_4B},
	{"Global Interrupt Status Register", 0x754, NI_660x_READ, DATA_4B},
	{"DMA Configuration Register", 0x76C, NI_660x_WRITE, DATA_4B},
	{"Global Interrupt Config Register", 0x770, NI_660x_WRITE, DATA_4B},
	{"IO Config Register 0-1", 0x77C, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 2-3", 0x77E, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 4-5", 0x780, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 6-7", 0x782, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 8-9", 0x784, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 10-11", 0x786, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 12-13", 0x788, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 14-15", 0x78A, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 16-17", 0x78C, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 18-19", 0x78E, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 20-21", 0x790, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 22-23", 0x792, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 24-25", 0x794, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 26-27", 0x796, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 28-29", 0x798, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 30-31", 0x79A, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 32-33", 0x79C, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 34-35", 0x79E, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 36-37", 0x7A0, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 38-39", 0x7A2, NI_660x_READ_WRITE, DATA_2B}
};

/* kind of ENABLE for the second counter */
enum clock_config_register_bits {
	CounterSwap = 0x1 << 21
};

/* ioconfigreg */
static inline unsigned ioconfig_bitshift(unsigned pfi_channel)
{
	if (pfi_channel % 2)
		return 0;
	else
		return 8;
}

static inline unsigned pfi_output_select_mask(unsigned pfi_channel)
{
	return 0x3 << ioconfig_bitshift(pfi_channel);
}

static inline unsigned pfi_output_select_bits(unsigned pfi_channel,
					      unsigned output_select)
{
	return (output_select & 0x3) << ioconfig_bitshift(pfi_channel);
}

static inline unsigned pfi_input_select_mask(unsigned pfi_channel)
{
	return 0x7 << (4 + ioconfig_bitshift(pfi_channel));
}

static inline unsigned pfi_input_select_bits(unsigned pfi_channel,
					     unsigned input_select)
{
	return (input_select & 0x7) << (4 + ioconfig_bitshift(pfi_channel));
}

/* dma configuration register bits */
static inline unsigned dma_select_mask(unsigned dma_channel)
{
	BUG_ON(dma_channel >= MAX_DMA_CHANNEL);
	return 0x1f << (8 * dma_channel);
}

enum dma_selection {
	dma_selection_none = 0x1f,
};

static inline unsigned dma_select_bits(unsigned dma_channel, unsigned selection)
{
	BUG_ON(dma_channel >= MAX_DMA_CHANNEL);
	return (selection << (8 * dma_channel)) & dma_select_mask(dma_channel);
}

static inline unsigned dma_reset_bit(unsigned dma_channel)
{
	BUG_ON(dma_channel >= MAX_DMA_CHANNEL);
	return 0x80 << (8 * dma_channel);
}

enum global_interrupt_status_register_bits {
	Counter_0_Int_Bit = 0x100,
	Counter_1_Int_Bit = 0x200,
	Counter_2_Int_Bit = 0x400,
	Counter_3_Int_Bit = 0x800,
	Cascade_Int_Bit = 0x20000000,
	Global_Int_Bit = 0x80000000
};

enum global_interrupt_config_register_bits {
	Cascade_Int_Enable_Bit = 0x20000000,
	Global_Int_Polarity_Bit = 0x40000000,
	Global_Int_Enable_Bit = 0x80000000
};

/* Offset of the GPCT chips from the base-address of the card */
/* First chip is at base-address + 0x00, etc. */
static const unsigned GPCT_OFFSET[2] = { 0x0, 0x800 };

enum ni_660x_boardid {
	BOARD_PCI6601,
	BOARD_PCI6602,
	BOARD_PXI6602,
	BOARD_PXI6608,
	BOARD_PXI6624
};

struct ni_660x_board {
	const char *name;
	unsigned n_chips;	/* total number of TIO chips */
};

static const struct ni_660x_board ni_660x_boards[] = {
	[BOARD_PCI6601] = {
		.name		= "PCI-6601",
		.n_chips	= 1,
	},
	[BOARD_PCI6602] = {
		.name		= "PCI-6602",
		.n_chips	= 2,
	},
	[BOARD_PXI6602] = {
		.name		= "PXI-6602",
		.n_chips	= 2,
	},
	[BOARD_PXI6608] = {
		.name		= "PXI-6608",
		.n_chips	= 2,
	},
	[BOARD_PXI6624] = {
		.name		= "PXI-6624",
		.n_chips	= 2,
	},
};

#define NI_660X_MAX_NUM_CHIPS 2
#define NI_660X_MAX_NUM_COUNTERS (NI_660X_MAX_NUM_CHIPS * counters_per_chip)

struct ni_660x_private {
	struct mite_struct *mite;
	struct ni_gpct_device *counter_dev;
	uint64_t pfi_direction_bits;
	struct mite_dma_descriptor_ring
	*mite_rings[NI_660X_MAX_NUM_CHIPS][counters_per_chip];
	spinlock_t mite_channel_lock;
	/* interrupt_lock prevents races between interrupt and comedi_poll */
	spinlock_t interrupt_lock;
	unsigned dma_configuration_soft_copies[NI_660X_MAX_NUM_CHIPS];
	spinlock_t soft_reg_copy_lock;
	unsigned short pfi_output_selects[NUM_PFI_CHANNELS];
};

static inline unsigned ni_660x_num_counters(struct comedi_device *dev)
{
	const struct ni_660x_board *board = comedi_board(dev);

	return board->n_chips * counters_per_chip;
}

static enum ni_660x_register ni_gpct_to_660x_register(enum ni_gpct_register reg)
{
	switch (reg) {
	case NITIO_G0_AUTO_INC:
		return NI660X_G0_AUTO_INC;
	case NITIO_G1_AUTO_INC:
		return NI660X_G1_AUTO_INC;
	case NITIO_G2_AUTO_INC:
		return NI660X_G2_AUTO_INC;
	case NITIO_G3_AUTO_INC:
		return NI660X_G3_AUTO_INC;
	case NITIO_G0_CMD:
		return NI660X_G0_CMD;
	case NITIO_G1_CMD:
		return NI660X_G1_CMD;
	case NITIO_G2_CMD:
		return NI660X_G2_CMD;
	case NITIO_G3_CMD:
		return NI660X_G3_CMD;
	case NITIO_G0_HW_SAVE:
		return NI660X_G0_HW_SAVE;
	case NITIO_G1_HW_SAVE:
		return NI660X_G1_HW_SAVE;
	case NITIO_G2_HW_SAVE:
		return NI660X_G2_HW_SAVE;
	case NITIO_G3_HW_SAVE:
		return NI660X_G3_HW_SAVE;
	case NITIO_G0_SW_SAVE:
		return NI660X_G0_SW_SAVE;
	case NITIO_G1_SW_SAVE:
		return NI660X_G1_SW_SAVE;
	case NITIO_G2_SW_SAVE:
		return NI660X_G2_SW_SAVE;
	case NITIO_G3_SW_SAVE:
		return NI660X_G3_SW_SAVE;
	case NITIO_G0_MODE:
		return NI660X_G0_MODE;
	case NITIO_G1_MODE:
		return NI660X_G1_MODE;
	case NITIO_G2_MODE:
		return NI660X_G2_MODE;
	case NITIO_G3_MODE:
		return NI660X_G3_MODE;
	case NITIO_G0_LOADA:
		return NI660X_G0_LOADA;
	case NITIO_G1_LOADA:
		return NI660X_G1_LOADA;
	case NITIO_G2_LOADA:
		return NI660X_G2_LOADA;
	case NITIO_G3_LOADA:
		return NI660X_G3_LOADA;
	case NITIO_G0_LOADB:
		return NI660X_G0_LOADB;
	case NITIO_G1_LOADB:
		return NI660X_G1_LOADB;
	case NITIO_G2_LOADB:
		return NI660X_G2_LOADB;
	case NITIO_G3_LOADB:
		return NI660X_G3_LOADB;
	case NITIO_G0_INPUT_SEL:
		return NI660X_G0_INPUT_SEL;
	case NITIO_G1_INPUT_SEL:
		return NI660X_G1_INPUT_SEL;
	case NITIO_G2_INPUT_SEL:
		return NI660X_G2_INPUT_SEL;
	case NITIO_G3_INPUT_SEL:
		return NI660X_G3_INPUT_SEL;
	case NITIO_G01_STATUS:
		return NI660X_G01_STATUS;
	case NITIO_G23_STATUS:
		return NI660X_G23_STATUS;
	case NITIO_G01_RESET:
		return NI660X_G01_RESET;
	case NITIO_G23_RESET:
		return NI660X_G23_RESET;
	case NITIO_G01_STATUS1:
		return NI660X_G01_STATUS1;
	case NITIO_G23_STATUS1:
		return NI660X_G23_STATUS1;
	case NITIO_G01_STATUS2:
		return NI660X_G01_STATUS2;
	case NITIO_G23_STATUS2:
		return NI660X_G23_STATUS2;
	case NITIO_G0_CNT_MODE:
		return NI660X_G0_CNT_MODE;
	case NITIO_G1_CNT_MODE:
		return NI660X_G1_CNT_MODE;
	case NITIO_G2_CNT_MODE:
		return NI660X_G2_CNT_MODE;
	case NITIO_G3_CNT_MODE:
		return NI660X_G3_CNT_MODE;
	case NITIO_G0_GATE2:
		return NI660X_G0_GATE2;
	case NITIO_G1_GATE2:
		return NI660X_G1_GATE2;
	case NITIO_G2_GATE2:
		return NI660X_G2_GATE2;
	case NITIO_G3_GATE2:
		return NI660X_G3_GATE2;
	case NITIO_G0_DMA_CFG:
		return NI660X_G0_DMA_CFG;
	case NITIO_G0_DMA_STATUS:
		return NI660X_G0_DMA_STATUS;
	case NITIO_G1_DMA_CFG:
		return NI660X_G1_DMA_CFG;
	case NITIO_G1_DMA_STATUS:
		return NI660X_G1_DMA_STATUS;
	case NITIO_G2_DMA_CFG:
		return NI660X_G2_DMA_CFG;
	case NITIO_G2_DMA_STATUS:
		return NI660X_G2_DMA_STATUS;
	case NITIO_G3_DMA_CFG:
		return NI660X_G3_DMA_CFG;
	case NITIO_G3_DMA_STATUS:
		return NI660X_G3_DMA_STATUS;
	case NITIO_G0_INT_ACK:
		return NI660X_G0_INT_ACK;
	case NITIO_G1_INT_ACK:
		return NI660X_G1_INT_ACK;
	case NITIO_G2_INT_ACK:
		return NI660X_G2_INT_ACK;
	case NITIO_G3_INT_ACK:
		return NI660X_G3_INT_ACK;
	case NITIO_G0_STATUS:
		return NI660X_G0_STATUS;
	case NITIO_G1_STATUS:
		return NI660X_G1_STATUS;
	case NITIO_G2_STATUS:
		return NI660X_G2_STATUS;
	case NITIO_G3_STATUS:
		return NI660X_G3_STATUS;
	case NITIO_G0_INT_ENA:
		return NI660X_G0_INT_ENA;
	case NITIO_G1_INT_ENA:
		return NI660X_G1_INT_ENA;
	case NITIO_G2_INT_ENA:
		return NI660X_G2_INT_ENA;
	case NITIO_G3_INT_ENA:
		return NI660X_G3_INT_ENA;
	default:
		BUG();
		return 0;
	}
}

static inline void ni_660x_write_register(struct comedi_device *dev,
					  unsigned chip, unsigned bits,
					  enum ni_660x_register reg)
{
	struct ni_660x_private *devpriv = dev->private;
	void __iomem *write_address =
	    devpriv->mite->daq_io_addr + GPCT_OFFSET[chip] +
	    registerData[reg].offset;

	switch (registerData[reg].size) {
	case DATA_2B:
		writew(bits, write_address);
		break;
	case DATA_4B:
		writel(bits, write_address);
		break;
	default:
		BUG();
		break;
	}
}

static inline unsigned ni_660x_read_register(struct comedi_device *dev,
					     unsigned chip,
					     enum ni_660x_register reg)
{
	struct ni_660x_private *devpriv = dev->private;
	void __iomem *read_address =
	    devpriv->mite->daq_io_addr + GPCT_OFFSET[chip] +
	    registerData[reg].offset;

	switch (registerData[reg].size) {
	case DATA_2B:
		return readw(read_address);
		break;
	case DATA_4B:
		return readl(read_address);
		break;
	default:
		BUG();
		break;
	}
	return 0;
}

static void ni_gpct_write_register(struct ni_gpct *counter, unsigned bits,
				   enum ni_gpct_register reg)
{
	struct comedi_device *dev = counter->counter_dev->dev;
	enum ni_660x_register ni_660x_register = ni_gpct_to_660x_register(reg);
	unsigned chip = counter->chip_index;

	ni_660x_write_register(dev, chip, bits, ni_660x_register);
}

static unsigned ni_gpct_read_register(struct ni_gpct *counter,
				      enum ni_gpct_register reg)
{
	struct comedi_device *dev = counter->counter_dev->dev;
	enum ni_660x_register ni_660x_register = ni_gpct_to_660x_register(reg);
	unsigned chip = counter->chip_index;

	return ni_660x_read_register(dev, chip, ni_660x_register);
}

static inline struct mite_dma_descriptor_ring *mite_ring(struct ni_660x_private
							 *priv,
							 struct ni_gpct
							 *counter)
{
	unsigned chip = counter->chip_index;

	return priv->mite_rings[chip][counter->counter_index];
}

static inline void ni_660x_set_dma_channel(struct comedi_device *dev,
					   unsigned mite_channel,
					   struct ni_gpct *counter)
{
	struct ni_660x_private *devpriv = dev->private;
	unsigned chip = counter->chip_index;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->soft_reg_copy_lock, flags);
	devpriv->dma_configuration_soft_copies[chip] &=
		~dma_select_mask(mite_channel);
	devpriv->dma_configuration_soft_copies[chip] |=
		dma_select_bits(mite_channel, counter->counter_index);
	ni_660x_write_register(dev, chip,
			       devpriv->dma_configuration_soft_copies[chip] |
			       dma_reset_bit(mite_channel), NI660X_DMA_CFG);
	mmiowb();
	spin_unlock_irqrestore(&devpriv->soft_reg_copy_lock, flags);
}

static inline void ni_660x_unset_dma_channel(struct comedi_device *dev,
					     unsigned mite_channel,
					     struct ni_gpct *counter)
{
	struct ni_660x_private *devpriv = dev->private;
	unsigned chip = counter->chip_index;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->soft_reg_copy_lock, flags);
	devpriv->dma_configuration_soft_copies[chip] &=
	    ~dma_select_mask(mite_channel);
	devpriv->dma_configuration_soft_copies[chip] |=
	    dma_select_bits(mite_channel, dma_selection_none);
	ni_660x_write_register(dev, chip,
			       devpriv->dma_configuration_soft_copies[chip],
			       NI660X_DMA_CFG);
	mmiowb();
	spin_unlock_irqrestore(&devpriv->soft_reg_copy_lock, flags);
}

static int ni_660x_request_mite_channel(struct comedi_device *dev,
					struct ni_gpct *counter,
					enum comedi_io_direction direction)
{
	struct ni_660x_private *devpriv = dev->private;
	unsigned long flags;
	struct mite_channel *mite_chan;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	BUG_ON(counter->mite_chan);
	mite_chan = mite_request_channel(devpriv->mite,
					 mite_ring(devpriv, counter));
	if (mite_chan == NULL) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		comedi_error(dev,
			     "failed to reserve mite dma channel for counter.");
		return -EBUSY;
	}
	mite_chan->dir = direction;
	ni_tio_set_mite_channel(counter, mite_chan);
	ni_660x_set_dma_channel(dev, mite_chan->channel, counter);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	return 0;
}

static void ni_660x_release_mite_channel(struct comedi_device *dev,
					 struct ni_gpct *counter)
{
	struct ni_660x_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (counter->mite_chan) {
		struct mite_channel *mite_chan = counter->mite_chan;

		ni_660x_unset_dma_channel(dev, mite_chan->channel, counter);
		ni_tio_set_mite_channel(counter, NULL);
		mite_release_channel(mite_chan);
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
}

static int ni_660x_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;
	int retval;

	retval = ni_660x_request_mite_channel(dev, counter, COMEDI_INPUT);
	if (retval) {
		comedi_error(dev,
			     "no dma channel available for use by counter");
		return retval;
	}
	ni_tio_acknowledge_and_confirm(counter, NULL, NULL, NULL, NULL);

	return ni_tio_cmd(dev, s);
}

static int ni_660x_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;
	int retval;

	retval = ni_tio_cancel(counter);
	ni_660x_release_mite_channel(dev, counter);
	return retval;
}

static void set_tio_counterswap(struct comedi_device *dev, int chip)
{
	unsigned bits = 0;

	/*
	 * See P. 3.5 of the Register-Level Programming manual.
	 * The CounterSwap bit has to be set on the second chip,
	 * otherwise it will try to use the same pins as the
	 * first chip.
	 */
	if (chip)
		bits = CounterSwap;

	ni_660x_write_register(dev, chip, bits, NI660X_CLK_CFG);
}

static void ni_660x_handle_gpct_interrupt(struct comedi_device *dev,
					  struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;

	ni_tio_handle_interrupt(counter, s);
	cfc_handle_events(dev, s);
}

static irqreturn_t ni_660x_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct ni_660x_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	unsigned i;
	unsigned long flags;

	if (!dev->attached)
		return IRQ_NONE;
	/* lock to avoid race with comedi_poll */
	spin_lock_irqsave(&devpriv->interrupt_lock, flags);
	smp_mb();
	for (i = 0; i < ni_660x_num_counters(dev); ++i) {
		s = &dev->subdevices[NI_660X_GPCT_SUBDEV(i)];
		ni_660x_handle_gpct_interrupt(dev, s);
	}
	spin_unlock_irqrestore(&devpriv->interrupt_lock, flags);
	return IRQ_HANDLED;
}

static int ni_660x_input_poll(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct ni_660x_private *devpriv = dev->private;
	struct ni_gpct *counter = s->private;
	unsigned long flags;

	/* lock to avoid race with comedi_poll */
	spin_lock_irqsave(&devpriv->interrupt_lock, flags);
	mite_sync_input_dma(counter->mite_chan, s);
	spin_unlock_irqrestore(&devpriv->interrupt_lock, flags);
	return comedi_buf_read_n_available(s);
}

static int ni_660x_buf_change(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      unsigned long new_size)
{
	struct ni_660x_private *devpriv = dev->private;
	struct ni_gpct *counter = s->private;
	int ret;

	ret = mite_buf_change(mite_ring(devpriv, counter), s);
	if (ret < 0)
		return ret;

	return 0;
}

static int ni_660x_allocate_private(struct comedi_device *dev)
{
	struct ni_660x_private *devpriv;
	unsigned i;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	spin_lock_init(&devpriv->mite_channel_lock);
	spin_lock_init(&devpriv->interrupt_lock);
	spin_lock_init(&devpriv->soft_reg_copy_lock);
	for (i = 0; i < NUM_PFI_CHANNELS; ++i)
		devpriv->pfi_output_selects[i] = pfi_output_select_counter;

	return 0;
}

static int ni_660x_alloc_mite_rings(struct comedi_device *dev)
{
	const struct ni_660x_board *board = comedi_board(dev);
	struct ni_660x_private *devpriv = dev->private;
	unsigned i;
	unsigned j;

	for (i = 0; i < board->n_chips; ++i) {
		for (j = 0; j < counters_per_chip; ++j) {
			devpriv->mite_rings[i][j] =
			    mite_alloc_ring(devpriv->mite);
			if (devpriv->mite_rings[i][j] == NULL)
				return -ENOMEM;
		}
	}
	return 0;
}

static void ni_660x_free_mite_rings(struct comedi_device *dev)
{
	const struct ni_660x_board *board = comedi_board(dev);
	struct ni_660x_private *devpriv = dev->private;
	unsigned i;
	unsigned j;

	for (i = 0; i < board->n_chips; ++i) {
		for (j = 0; j < counters_per_chip; ++j)
			mite_free_ring(devpriv->mite_rings[i][j]);
	}
}

static void init_tio_chip(struct comedi_device *dev, int chipset)
{
	struct ni_660x_private *devpriv = dev->private;
	unsigned i;

	/*  init dma configuration register */
	devpriv->dma_configuration_soft_copies[chipset] = 0;
	for (i = 0; i < MAX_DMA_CHANNEL; ++i) {
		devpriv->dma_configuration_soft_copies[chipset] |=
		    dma_select_bits(i, dma_selection_none) & dma_select_mask(i);
	}
	ni_660x_write_register(dev, chipset,
			       devpriv->dma_configuration_soft_copies[chipset],
			       NI660X_DMA_CFG);
	for (i = 0; i < NUM_PFI_CHANNELS; ++i)
		ni_660x_write_register(dev, chipset, 0, IOConfigReg(i));
}

static int ni_660x_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	unsigned base_bitfield_channel = CR_CHAN(insn->chanspec);

	/*  Check if we have to write some bits */
	if (data[0]) {
		s->state &= ~(data[0] << base_bitfield_channel);
		s->state |= (data[0] & data[1]) << base_bitfield_channel;
		/* Write out the new digital output lines */
		ni_660x_write_register(dev, 0, s->state, NI660X_DIO32_OUTPUT);
	}
	/* on return, data[1] contains the value of the digital
	 * input and output lines. */
	data[1] = (ni_660x_read_register(dev, 0, NI660X_DIO32_INPUT) >>
			base_bitfield_channel);

	return insn->n;
}

static void ni_660x_select_pfi_output(struct comedi_device *dev,
				      unsigned pfi_channel,
				      unsigned output_select)
{
	const struct ni_660x_board *board = comedi_board(dev);
	static const unsigned counter_4_7_first_pfi = 8;
	static const unsigned counter_4_7_last_pfi = 23;
	unsigned active_chipset = 0;
	unsigned idle_chipset = 0;
	unsigned active_bits;
	unsigned idle_bits;

	if (board->n_chips > 1) {
		if (output_select == pfi_output_select_counter &&
		    pfi_channel >= counter_4_7_first_pfi &&
		    pfi_channel <= counter_4_7_last_pfi) {
			active_chipset = 1;
			idle_chipset = 0;
		} else {
			active_chipset = 0;
			idle_chipset = 1;
		}
	}

	if (idle_chipset != active_chipset) {
		idle_bits =
		    ni_660x_read_register(dev, idle_chipset,
					  IOConfigReg(pfi_channel));
		idle_bits &= ~pfi_output_select_mask(pfi_channel);
		idle_bits |=
		    pfi_output_select_bits(pfi_channel,
					   pfi_output_select_high_Z);
		ni_660x_write_register(dev, idle_chipset, idle_bits,
				       IOConfigReg(pfi_channel));
	}

	active_bits =
	    ni_660x_read_register(dev, active_chipset,
				  IOConfigReg(pfi_channel));
	active_bits &= ~pfi_output_select_mask(pfi_channel);
	active_bits |= pfi_output_select_bits(pfi_channel, output_select);
	ni_660x_write_register(dev, active_chipset, active_bits,
			       IOConfigReg(pfi_channel));
}

static int ni_660x_set_pfi_routing(struct comedi_device *dev, unsigned chan,
				   unsigned source)
{
	struct ni_660x_private *devpriv = dev->private;

	if (source > num_pfi_output_selects)
		return -EINVAL;
	if (source == pfi_output_select_high_Z)
		return -EINVAL;
	if (chan < min_counter_pfi_chan) {
		if (source == pfi_output_select_counter)
			return -EINVAL;
	} else if (chan > max_dio_pfi_chan) {
		if (source == pfi_output_select_do)
			return -EINVAL;
	}

	devpriv->pfi_output_selects[chan] = source;
	if (devpriv->pfi_direction_bits & (((uint64_t) 1) << chan))
		ni_660x_select_pfi_output(dev, chan,
					  devpriv->pfi_output_selects[chan]);
	return 0;
}

static int ni_660x_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct ni_660x_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	uint64_t bit = 1ULL << chan;
	unsigned int val;
	int ret;

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		devpriv->pfi_direction_bits |= bit;
		ni_660x_select_pfi_output(dev, chan,
					  devpriv->pfi_output_selects[chan]);
		break;

	case INSN_CONFIG_DIO_INPUT:
		devpriv->pfi_direction_bits &= ~bit;
		ni_660x_select_pfi_output(dev, chan, pfi_output_select_high_Z);
		break;

	case INSN_CONFIG_DIO_QUERY:
		data[1] = (devpriv->pfi_direction_bits & bit) ? COMEDI_OUTPUT
							      : COMEDI_INPUT;
		break;

	case INSN_CONFIG_SET_ROUTING:
		ret = ni_660x_set_pfi_routing(dev, chan, data[1]);
		if (ret)
			return ret;
		break;

	case INSN_CONFIG_GET_ROUTING:
		data[1] = devpriv->pfi_output_selects[chan];
		break;

	case INSN_CONFIG_FILTER:
		val = ni_660x_read_register(dev, 0, IOConfigReg(chan));
		val &= ~pfi_input_select_mask(chan);
		val |= pfi_input_select_bits(chan, data[1]);
		ni_660x_write_register(dev, 0, val, IOConfigReg(chan));
		break;

	default:
		return -EINVAL;
	}

	return insn->n;
}

static int ni_660x_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct ni_660x_board *board = NULL;
	struct ni_660x_private *devpriv;
	struct comedi_subdevice *s;
	int ret;
	unsigned i;
	unsigned global_interrupt_config_bits;

	if (context < ARRAY_SIZE(ni_660x_boards))
		board = &ni_660x_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	ret = ni_660x_allocate_private(dev);
	if (ret < 0)
		return ret;
	devpriv = dev->private;

	devpriv->mite = mite_alloc(pcidev);
	if (!devpriv->mite)
		return -ENOMEM;

	ret = mite_setup2(devpriv->mite, 1);
	if (ret < 0) {
		dev_warn(dev->class_dev, "error setting up mite\n");
		return ret;
	}

	ret = ni_660x_alloc_mite_rings(dev);
	if (ret < 0)
		return ret;

	ret = comedi_alloc_subdevices(dev, 2 + NI_660X_MAX_NUM_COUNTERS);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* Old GENERAL-PURPOSE COUNTER/TIME (GPCT) subdevice, no longer used */
	s->type = COMEDI_SUBD_UNUSED;

	s = &dev->subdevices[NI_660X_DIO_SUBDEV];
	/* DIGITAL I/O SUBDEVICE */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = NUM_PFI_CHANNELS;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = ni_660x_dio_insn_bits;
	s->insn_config = ni_660x_dio_insn_config;
	/*  we use the ioconfig registers to control dio direction, so zero
	output enables in stc dio control reg */
	ni_660x_write_register(dev, 0, 0, NI660X_STC_DIO_CONTROL);

	devpriv->counter_dev = ni_gpct_device_construct(dev,
						     &ni_gpct_write_register,
						     &ni_gpct_read_register,
						     ni_gpct_variant_660x,
						     ni_660x_num_counters
						     (dev));
	if (devpriv->counter_dev == NULL)
		return -ENOMEM;
	for (i = 0; i < NI_660X_MAX_NUM_COUNTERS; ++i) {
		s = &dev->subdevices[NI_660X_GPCT_SUBDEV(i)];
		if (i < ni_660x_num_counters(dev)) {
			s->type = COMEDI_SUBD_COUNTER;
			s->subdev_flags =
			    SDF_READABLE | SDF_WRITABLE | SDF_LSAMPL |
			    SDF_CMD_READ /* | SDF_CMD_WRITE */ ;
			s->n_chan = 3;
			s->maxdata = 0xffffffff;
			s->insn_read = ni_tio_insn_read;
			s->insn_write = ni_tio_insn_write;
			s->insn_config = ni_tio_insn_config;
			s->do_cmd = &ni_660x_cmd;
			s->len_chanlist = 1;
			s->do_cmdtest = ni_tio_cmdtest;
			s->cancel = &ni_660x_cancel;
			s->poll = &ni_660x_input_poll;
			s->async_dma_dir = DMA_BIDIRECTIONAL;
			s->buf_change = &ni_660x_buf_change;
			s->private = &devpriv->counter_dev->counters[i];

			devpriv->counter_dev->counters[i].chip_index =
			    i / counters_per_chip;
			devpriv->counter_dev->counters[i].counter_index =
			    i % counters_per_chip;
		} else {
			s->type = COMEDI_SUBD_UNUSED;
		}
	}
	for (i = 0; i < board->n_chips; ++i)
		init_tio_chip(dev, i);

	for (i = 0; i < ni_660x_num_counters(dev); ++i)
		ni_tio_init_counter(&devpriv->counter_dev->counters[i]);

	for (i = 0; i < NUM_PFI_CHANNELS; ++i) {
		if (i < min_counter_pfi_chan)
			ni_660x_set_pfi_routing(dev, i, pfi_output_select_do);
		else
			ni_660x_set_pfi_routing(dev, i,
						pfi_output_select_counter);
		ni_660x_select_pfi_output(dev, i, pfi_output_select_high_Z);
	}
	/* to be safe, set counterswap bits on tio chips after all the counter
	   outputs have been set to high impedance mode */
	for (i = 0; i < board->n_chips; ++i)
		set_tio_counterswap(dev, i);

	ret = request_irq(pcidev->irq, ni_660x_interrupt,
			  IRQF_SHARED, "ni_660x", dev);
	if (ret < 0) {
		dev_warn(dev->class_dev, " irq not available\n");
		return ret;
	}
	dev->irq = pcidev->irq;
	global_interrupt_config_bits = Global_Int_Enable_Bit;
	if (board->n_chips > 1)
		global_interrupt_config_bits |= Cascade_Int_Enable_Bit;
	ni_660x_write_register(dev, 0, global_interrupt_config_bits,
			       NI660X_GLOBAL_INT_CFG);

	return 0;
}

static void ni_660x_detach(struct comedi_device *dev)
{
	struct ni_660x_private *devpriv = dev->private;

	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv) {
		if (devpriv->counter_dev)
			ni_gpct_device_destroy(devpriv->counter_dev);
		if (devpriv->mite) {
			ni_660x_free_mite_rings(dev);
			mite_unsetup(devpriv->mite);
			mite_free(devpriv->mite);
		}
	}
	comedi_pci_disable(dev);
}

static struct comedi_driver ni_660x_driver = {
	.driver_name	= "ni_660x",
	.module		= THIS_MODULE,
	.auto_attach	= ni_660x_auto_attach,
	.detach		= ni_660x_detach,
};

static int ni_660x_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &ni_660x_driver, id->driver_data);
}

static const struct pci_device_id ni_660x_pci_table[] = {
	{ PCI_VDEVICE(NI, 0x1310), BOARD_PCI6602 },
	{ PCI_VDEVICE(NI, 0x1360), BOARD_PXI6602 },
	{ PCI_VDEVICE(NI, 0x2c60), BOARD_PCI6601 },
	{ PCI_VDEVICE(NI, 0x2cc0), BOARD_PXI6608 },
	{ PCI_VDEVICE(NI, 0x1e40), BOARD_PXI6624 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ni_660x_pci_table);

static struct pci_driver ni_660x_pci_driver = {
	.name		= "ni_660x",
	.id_table	= ni_660x_pci_table,
	.probe		= ni_660x_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(ni_660x_driver, ni_660x_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
