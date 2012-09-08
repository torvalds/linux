/*
    me4000.h
    Register descriptions and defines for the ME-4000 board family

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998-9 David A. Schleef <ds@schleef.org>

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

#ifndef _ME4000_H_
#define _ME4000_H_

/*=============================================================================
  ME-4000 base register offsets
  ===========================================================================*/

#define ME4000_AO_00_CTRL_REG			0x00	/*  R/W */
#define ME4000_AO_00_STATUS_REG			0x04	/*  R/_ */
#define ME4000_AO_00_FIFO_REG			0x08	/*  _/W */
#define ME4000_AO_00_SINGLE_REG			0x0C	/*  R/W */
#define ME4000_AO_00_TIMER_REG			0x10	/*  _/W */

#define ME4000_AO_01_CTRL_REG			0x18	/*  R/W */
#define ME4000_AO_01_STATUS_REG			0x1C	/*  R/_ */
#define ME4000_AO_01_FIFO_REG			0x20	/*  _/W */
#define ME4000_AO_01_SINGLE_REG			0x24	/*  R/W */
#define ME4000_AO_01_TIMER_REG			0x28	/*  _/W */

#define ME4000_AO_02_CTRL_REG			0x30	/*  R/W */
#define ME4000_AO_02_STATUS_REG			0x34	/*  R/_ */
#define ME4000_AO_02_FIFO_REG			0x38	/*  _/W */
#define ME4000_AO_02_SINGLE_REG			0x3C	/*  R/W */
#define ME4000_AO_02_TIMER_REG			0x40	/*  _/W */

#define ME4000_AO_03_CTRL_REG			0x48	/*  R/W */
#define ME4000_AO_03_STATUS_REG			0x4C	/*  R/_ */
#define ME4000_AO_03_FIFO_REG			0x50	/*  _/W */
#define ME4000_AO_03_SINGLE_REG			0x54	/*  R/W */
#define ME4000_AO_03_TIMER_REG			0x58	/*  _/W */

#define ME4000_AI_CTRL_REG			0x74	/*  _/W */
#define ME4000_AI_STATUS_REG			0x74	/*  R/_ */
#define ME4000_AI_CHANNEL_LIST_REG		0x78	/*  _/W */
#define ME4000_AI_DATA_REG			0x7C	/*  R/_ */
#define ME4000_AI_CHAN_TIMER_REG		0x80	/*  _/W */
#define ME4000_AI_CHAN_PRE_TIMER_REG		0x84	/*  _/W */
#define ME4000_AI_SCAN_TIMER_LOW_REG		0x88	/*  _/W */
#define ME4000_AI_SCAN_TIMER_HIGH_REG		0x8C	/*  _/W */
#define ME4000_AI_SCAN_PRE_TIMER_LOW_REG	0x90	/*  _/W */
#define ME4000_AI_SCAN_PRE_TIMER_HIGH_REG	0x94	/*  _/W */
#define ME4000_AI_START_REG			0x98	/*  R/_ */

#define ME4000_IRQ_STATUS_REG			0x9C	/*  R/_ */

#define ME4000_DIO_PORT_0_REG			0xA0	/*  R/W */
#define ME4000_DIO_PORT_1_REG			0xA4	/*  R/W */
#define ME4000_DIO_PORT_2_REG			0xA8	/*  R/W */
#define ME4000_DIO_PORT_3_REG			0xAC	/*  R/W */
#define ME4000_DIO_DIR_REG			0xB0	/*  R/W */

#define ME4000_AO_LOADSETREG_XX			0xB4	/*  R/W */

#define ME4000_DIO_CTRL_REG			0xB8	/*  R/W */

#define ME4000_AO_DEMUX_ADJUST_REG		0xBC	/*  -/W */

#define ME4000_AI_SAMPLE_COUNTER_REG		0xC0	/*  _/W */

/*=============================================================================
  Value to adjust Demux
  ===========================================================================*/

#define ME4000_AO_DEMUX_ADJUST_VALUE            0x4C

/*=============================================================================
  Counter base register offsets
  ===========================================================================*/

#define ME4000_CNT_COUNTER_0_REG		0x00
#define ME4000_CNT_COUNTER_1_REG		0x01
#define ME4000_CNT_COUNTER_2_REG		0x02
#define ME4000_CNT_CTRL_REG			0x03

/*=============================================================================
  PLX base register offsets
  ===========================================================================*/

#define PLX_INTCSR	0x4C	/*  Interrupt control and status register */
#define PLX_ICR		0x50	/*  Initialization control register */

/*=============================================================================
  Bits for the PLX_ICSR register
  ===========================================================================*/

#define PLX_INTCSR_LOCAL_INT1_EN             0x01	/*  If set, local interrupt 1 is enabled (r/w) */
#define PLX_INTCSR_LOCAL_INT1_POL            0x02	/*  If set, local interrupt 1 polarity is active high (r/w) */
#define PLX_INTCSR_LOCAL_INT1_STATE          0x04	/*  If set, local interrupt 1 is active (r/_) */
#define PLX_INTCSR_LOCAL_INT2_EN             0x08	/*  If set, local interrupt 2 is enabled (r/w) */
#define PLX_INTCSR_LOCAL_INT2_POL            0x10	/*  If set, local interrupt 2 polarity is active high (r/w) */
#define PLX_INTCSR_LOCAL_INT2_STATE          0x20	/*  If set, local interrupt 2 is active  (r/_) */
#define PLX_INTCSR_PCI_INT_EN                0x40	/*  If set, PCI interrupt is enabled (r/w) */
#define PLX_INTCSR_SOFT_INT                  0x80	/*  If set, a software interrupt is generated (r/w) */

/*=============================================================================
  Bits for the PLX_ICR register
  ===========================================================================*/

#define PLX_ICR_BIT_EEPROM_CLOCK_SET		0x01000000
#define PLX_ICR_BIT_EEPROM_CHIP_SELECT		0x02000000
#define PLX_ICR_BIT_EEPROM_WRITE		0x04000000
#define PLX_ICR_BIT_EEPROM_READ			0x08000000
#define PLX_ICR_BIT_EEPROM_VALID		0x10000000

#define PLX_ICR_MASK_EEPROM			0x1F000000

#define EEPROM_DELAY				1

/*=============================================================================
  Bits for the ME4000_AO_CTRL_REG register
  ===========================================================================*/

#define ME4000_AO_CTRL_BIT_MODE_0		0x001
#define ME4000_AO_CTRL_BIT_MODE_1		0x002
#define ME4000_AO_CTRL_MASK_MODE		0x003
#define ME4000_AO_CTRL_BIT_STOP			0x004
#define ME4000_AO_CTRL_BIT_ENABLE_FIFO		0x008
#define ME4000_AO_CTRL_BIT_ENABLE_EX_TRIG	0x010
#define ME4000_AO_CTRL_BIT_EX_TRIG_EDGE		0x020
#define ME4000_AO_CTRL_BIT_IMMEDIATE_STOP	0x080
#define ME4000_AO_CTRL_BIT_ENABLE_DO		0x100
#define ME4000_AO_CTRL_BIT_ENABLE_IRQ		0x200
#define ME4000_AO_CTRL_BIT_RESET_IRQ		0x400

/*=============================================================================
  Bits for the ME4000_AO_STATUS_REG register
  ===========================================================================*/

#define ME4000_AO_STATUS_BIT_FSM		0x01
#define ME4000_AO_STATUS_BIT_FF			0x02
#define ME4000_AO_STATUS_BIT_HF			0x04
#define ME4000_AO_STATUS_BIT_EF			0x08

/*=============================================================================
  Bits for the ME4000_AI_CTRL_REG register
  ===========================================================================*/

#define ME4000_AI_CTRL_BIT_MODE_0		0x00000001
#define ME4000_AI_CTRL_BIT_MODE_1		0x00000002
#define ME4000_AI_CTRL_BIT_MODE_2		0x00000004
#define ME4000_AI_CTRL_BIT_SAMPLE_HOLD		0x00000008
#define ME4000_AI_CTRL_BIT_IMMEDIATE_STOP	0x00000010
#define ME4000_AI_CTRL_BIT_STOP			0x00000020
#define ME4000_AI_CTRL_BIT_CHANNEL_FIFO		0x00000040
#define ME4000_AI_CTRL_BIT_DATA_FIFO		0x00000080
#define ME4000_AI_CTRL_BIT_FULLSCALE		0x00000100
#define ME4000_AI_CTRL_BIT_OFFSET		0x00000200
#define ME4000_AI_CTRL_BIT_EX_TRIG_ANALOG	0x00000400
#define ME4000_AI_CTRL_BIT_EX_TRIG		0x00000800
#define ME4000_AI_CTRL_BIT_EX_TRIG_FALLING	0x00001000
#define ME4000_AI_CTRL_BIT_EX_IRQ		0x00002000
#define ME4000_AI_CTRL_BIT_EX_IRQ_RESET		0x00004000
#define ME4000_AI_CTRL_BIT_LE_IRQ		0x00008000
#define ME4000_AI_CTRL_BIT_LE_IRQ_RESET		0x00010000
#define ME4000_AI_CTRL_BIT_HF_IRQ		0x00020000
#define ME4000_AI_CTRL_BIT_HF_IRQ_RESET		0x00040000
#define ME4000_AI_CTRL_BIT_SC_IRQ		0x00080000
#define ME4000_AI_CTRL_BIT_SC_IRQ_RESET		0x00100000
#define ME4000_AI_CTRL_BIT_SC_RELOAD		0x00200000
#define ME4000_AI_CTRL_BIT_EX_TRIG_BOTH		0x80000000

/*=============================================================================
  Bits for the ME4000_AI_STATUS_REG register
  ===========================================================================*/

#define ME4000_AI_STATUS_BIT_EF_CHANNEL		0x00400000
#define ME4000_AI_STATUS_BIT_HF_CHANNEL		0x00800000
#define ME4000_AI_STATUS_BIT_FF_CHANNEL		0x01000000
#define ME4000_AI_STATUS_BIT_EF_DATA		0x02000000
#define ME4000_AI_STATUS_BIT_HF_DATA		0x04000000
#define ME4000_AI_STATUS_BIT_FF_DATA		0x08000000
#define ME4000_AI_STATUS_BIT_LE			0x10000000
#define ME4000_AI_STATUS_BIT_FSM		0x20000000

/*=============================================================================
  Bits for the ME4000_IRQ_STATUS_REG register
  ===========================================================================*/

#define ME4000_IRQ_STATUS_BIT_EX		0x01
#define ME4000_IRQ_STATUS_BIT_LE		0x02
#define ME4000_IRQ_STATUS_BIT_AI_HF		0x04
#define ME4000_IRQ_STATUS_BIT_AO_0_HF		0x08
#define ME4000_IRQ_STATUS_BIT_AO_1_HF		0x10
#define ME4000_IRQ_STATUS_BIT_AO_2_HF		0x20
#define ME4000_IRQ_STATUS_BIT_AO_3_HF		0x40
#define ME4000_IRQ_STATUS_BIT_SC		0x80

/*=============================================================================
  Bits for the ME4000_DIO_CTRL_REG register
  ===========================================================================*/

#define ME4000_DIO_CTRL_BIT_MODE_0		0x0001
#define ME4000_DIO_CTRL_BIT_MODE_1		0x0002
#define ME4000_DIO_CTRL_BIT_MODE_2		0x0004
#define ME4000_DIO_CTRL_BIT_MODE_3		0x0008
#define ME4000_DIO_CTRL_BIT_MODE_4		0x0010
#define ME4000_DIO_CTRL_BIT_MODE_5		0x0020
#define ME4000_DIO_CTRL_BIT_MODE_6		0x0040
#define ME4000_DIO_CTRL_BIT_MODE_7		0x0080

#define ME4000_DIO_CTRL_BIT_FUNCTION_0		0x0100
#define ME4000_DIO_CTRL_BIT_FUNCTION_1		0x0200

#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_0		0x0400
#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_1		0x0800
#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_2		0x1000
#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_3		0x2000

/*=============================================================================
  Global board and subdevice information structures
  ===========================================================================*/

struct me4000_ao_context {
	int irq;

	unsigned long mirror;	/*  Store the last written value */

	unsigned long ctrl_reg;
	unsigned long status_reg;
	unsigned long fifo_reg;
	unsigned long single_reg;
	unsigned long timer_reg;
	unsigned long irq_status_reg;
	unsigned long preload_reg;
};

struct me4000_ai_context {
	int irq;

	unsigned long ctrl_reg;
	unsigned long status_reg;
	unsigned long channel_list_reg;
	unsigned long data_reg;
	unsigned long chan_timer_reg;
	unsigned long chan_pre_timer_reg;
	unsigned long scan_timer_low_reg;
	unsigned long scan_timer_high_reg;
	unsigned long scan_pre_timer_low_reg;
	unsigned long scan_pre_timer_high_reg;
	unsigned long start_reg;
	unsigned long irq_status_reg;
	unsigned long sample_counter_reg;
};

struct me4000_dio_context {
	unsigned long dir_reg;
	unsigned long ctrl_reg;
	unsigned long port_0_reg;
	unsigned long port_1_reg;
	unsigned long port_2_reg;
	unsigned long port_3_reg;
};

struct me4000_info {
	unsigned long plx_regbase;	/*  PLX configuration space base address */
	unsigned long timer_regbase;	/*  Base address of the timer circuit */
	unsigned long program_regbase;	/*  Base address to set the program pin for the xilinx */

	unsigned int serial_no;	/*  Serial number of the board */
	unsigned char hw_revision;	/*  Hardware revision of the board */
	unsigned short vendor_id;	/*  Meilhaus vendor id */
	unsigned short device_id;	/*  Device id */

	struct pci_dev *pci_dev_p;	/*  General PCI information */

	unsigned int irq;	/*  IRQ assigned from the PCI BIOS */

	struct me4000_ai_context ai_context;	/*  Analog input  specific context */
	struct me4000_ao_context ao_context[4];	/*  Vector with analog output specific context */
	struct me4000_dio_context dio_context;	/*  Digital I/O specific context */
};

#define info	((struct me4000_info *)dev->private)

/*-----------------------------------------------------------------------------
  Defines for analog input
 ----------------------------------------------------------------------------*/

/* General stuff */
#define ME4000_AI_FIFO_COUNT			2048

#define ME4000_AI_MIN_TICKS			66
#define ME4000_AI_MIN_SAMPLE_TIME		2000	/*  Minimum sample time [ns] */
#define ME4000_AI_BASE_FREQUENCY		(unsigned int) 33E6

/* Channel list defines and masks */
#define ME4000_AI_CHANNEL_LIST_COUNT		1024

#define ME4000_AI_LIST_INPUT_SINGLE_ENDED	0x000
#define ME4000_AI_LIST_INPUT_DIFFERENTIAL	0x020

#define ME4000_AI_LIST_RANGE_BIPOLAR_10		0x000
#define ME4000_AI_LIST_RANGE_BIPOLAR_2_5	0x040
#define ME4000_AI_LIST_RANGE_UNIPOLAR_10	0x080
#define ME4000_AI_LIST_RANGE_UNIPOLAR_2_5	0x0C0

#define ME4000_AI_LIST_LAST_ENTRY		0x100

/*-----------------------------------------------------------------------------
  Defines for counters
 ----------------------------------------------------------------------------*/

#define ME4000_CNT_COUNTER_0  0x00
#define ME4000_CNT_COUNTER_1  0x40
#define ME4000_CNT_COUNTER_2  0x80

#define ME4000_CNT_MODE_0     0x00	/*  Change state if zero crossing */
#define ME4000_CNT_MODE_1     0x02	/*  Retriggerable One-Shot */
#define ME4000_CNT_MODE_2     0x04	/*  Asymmetrical divider */
#define ME4000_CNT_MODE_3     0x06	/*  Symmetrical divider */
#define ME4000_CNT_MODE_4     0x08	/*  Counter start by software trigger */
#define ME4000_CNT_MODE_5     0x0A	/*  Counter start by hardware trigger */

#endif
