/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *  Header for ines GPIB boards
 *    copyright            : (C) 2002 by Frank Mori Hess
 ***************************************************************************/

#ifndef _INES_GPIB_H
#define _INES_GPIB_H

#include "nec7210.h"
#include "gpibP.h"
#include "plx9050.h"
#include "amcc5920.h"
#include "quancom_pci.h"
#include <linux/interrupt.h>

enum ines_pci_chip {
	PCI_CHIP_NONE,
	PCI_CHIP_PLX9050,
	PCI_CHIP_AMCC5920,
	PCI_CHIP_QUANCOM,
	PCI_CHIP_QUICKLOGIC5030,
};

struct ines_priv {
	struct nec7210_priv nec7210_priv;
	struct pci_dev *pci_device;
	// base address for plx9052 pci chip
	unsigned long plx_iobase;
	// base address for amcc5920 pci chip
	unsigned long amcc_iobase;
	unsigned int irq;
	enum ines_pci_chip pci_chip_type;
	u8 extend_mode_bits;
};

// interface functions
int ines_read(gpib_board_t *board, uint8_t *buffer, size_t length, int *end, size_t *bytes_read);
int ines_write(gpib_board_t *board, uint8_t *buffer, size_t length,
	       int send_eoi, size_t *bytes_written);
int ines_accel_read(gpib_board_t *board, uint8_t *buffer, size_t length,
		    int *end, size_t *bytes_read);
int ines_accel_write(gpib_board_t *board, uint8_t *buffer, size_t length,
		     int send_eoi, size_t *bytes_written);
int ines_command(gpib_board_t *board, uint8_t *buffer, size_t length, size_t *bytes_written);
int ines_take_control(gpib_board_t *board, int synchronous);
int ines_go_to_standby(gpib_board_t *board);
void ines_request_system_control(gpib_board_t *board, int request_control);
void ines_interface_clear(gpib_board_t *board, int assert);
void ines_remote_enable(gpib_board_t *board, int enable);
int ines_enable_eos(gpib_board_t *board, uint8_t eos_byte, int compare_8_bits);
void ines_disable_eos(gpib_board_t *board);
unsigned int ines_update_status(gpib_board_t *board, unsigned int clear_mask);
int ines_primary_address(gpib_board_t *board, unsigned int address);
int ines_secondary_address(gpib_board_t *board, unsigned int address, int enable);
int ines_parallel_poll(gpib_board_t *board, uint8_t *result);
void ines_parallel_poll_configure(gpib_board_t *board, uint8_t config);
void ines_parallel_poll_response(gpib_board_t *board, int ist);
void ines_serial_poll_response(gpib_board_t *board, uint8_t status);
uint8_t ines_serial_poll_status(gpib_board_t *board);
int ines_line_status(const gpib_board_t *board);
unsigned int ines_t1_delay(gpib_board_t *board, unsigned int nano_sec);
void ines_return_to_local(gpib_board_t *board);

// interrupt service routines
irqreturn_t ines_pci_interrupt(int irq, void *arg);
irqreturn_t ines_interrupt(gpib_board_t *board);

// utility functions
void ines_free_private(gpib_board_t *board);
int ines_generic_attach(gpib_board_t *board);
void ines_online(struct ines_priv *priv, const gpib_board_t *board, int use_accel);
void ines_set_xfer_counter(struct ines_priv *priv, unsigned int count);

/* inb/outb wrappers */
static inline unsigned int ines_inb(struct ines_priv *priv, unsigned int register_number)
{
	return inb(priv->nec7210_priv.iobase +
		   register_number * priv->nec7210_priv.offset);
}

static inline void ines_outb(struct ines_priv *priv, unsigned int value,
			     unsigned int register_number)
{
	outb(value, priv->nec7210_priv.iobase +
	     register_number * priv->nec7210_priv.offset);
}

// pcmcia init/cleanup

int ines_pcmcia_init_module(void);
void ines_pcmcia_cleanup_module(void);

enum ines_regs {
	// read
	FIFO_STATUS = 0x8,
	ISR3 = 0x9,
	ISR4 = 0xa,
	IN_FIFO_COUNT = 0x10,
	OUT_FIFO_COUNT = 0x11,
	EXTEND_STATUS = 0xf,

	// write
	XDMA_CONTROL = 0x8,
	IMR3 = ISR3,
	IMR4 = ISR4,
	IN_FIFO_WATERMARK = IN_FIFO_COUNT,
	OUT_FIFO_WATERMARK = OUT_FIFO_COUNT,
	EXTEND_MODE = 0xf,

	// read-write
	XFER_COUNT_LOWER = 0xb,
	XFER_COUNT_UPPER = 0xc,
	BUS_CONTROL_MONITOR = 0x13,
};

enum isr3_imr3_bits {
	HW_TIMEOUT_BIT = 0x1,
	XFER_COUNT_BIT = 0x2,
	CMD_RECEIVED_BIT = 0x4,
	TCT_RECEIVED_BIT = 0x8,
	IFC_ACTIVE_BIT = 0x10,
	ATN_ACTIVE_BIT = 0x20,
	FIFO_ERROR_BIT = 0x40,
};

enum isr4_imr4_bits {
	IN_FIFO_WATERMARK_BIT = 0x1,
	OUT_FIFO_WATERMARK_BIT = 0x2,
	IN_FIFO_FULL_BIT = 0x4,
	OUT_FIFO_EMPTY_BIT = 0x8,
	IN_FIFO_READY_BIT = 0x10,
	OUT_FIFO_READY_BIT = 0x20,
	IN_FIFO_EXIT_WATERMARK_BIT = 0x40,
	OUT_FIFO_EXIT_WATERMARK_BIT = 0x80,
};

enum extend_mode_bits {
	TR3_TRIG_ENABLE_BIT = 0x1,	// enable generation of trigger pulse T/R3 pin
	// clear message available status bit when chip writes byte with EOI true
	MAV_ENABLE_BIT = 0x2,
	EOS1_ENABLE_BIT = 0x4,	// enable eos register 1
	EOS2_ENABLE_BIT = 0x8,	// enable eos register 2
	EOIDIS_BIT = 0x10,	// disable EOI interrupt when doing rfd holdoff on end?
	XFER_COUNTER_ENABLE_BIT = 0x20,
	XFER_COUNTER_OUTPUT_BIT = 0x40,	// use counter for output, clear for input
	// when xfer counter hits 0, assert EOI on write or RFD holdoff on read
	LAST_BYTE_HANDLING_BIT = 0x80,
};

enum extend_status_bits {
	OUTPUT_MESSAGE_IN_PROGRESS_BIT = 0x1,
	SCSEL_BIT = 0x2,	// statue of SCSEL pin
	LISTEN_DISABLED = 0x4,
	IN_FIFO_EMPTY_BIT = 0x8,
	OUT_FIFO_FULL_BIT = 0x10,
};

// ines adds fifo enable bits to address mode register
enum ines_admr_bits {
	IN_FIFO_ENABLE_BIT = 0x8,
	OUT_FIFO_ENABLE_BIT = 0x4,
};

enum xdma_control_bits {
	DMA_OUTPUT_BIT = 0x1,	// use dma for output, clear for input
	ENABLE_SYNC_DMA_BIT = 0x2,
	DMA_ACCESS_EVERY_CYCLE = 0x4,// dma accesses fifo every cycle, clear for every other cycle
	DMA_16BIT = 0x8,	// clear for 8 bit transfers
};

enum bus_control_monitor_bits {
	BCM_DAV_BIT = 0x1,
	BCM_NRFD_BIT = 0x2,
	BCM_NDAC_BIT = 0x4,
	BCM_IFC_BIT = 0x8,
	BCM_ATN_BIT = 0x10,
	BCM_SRQ_BIT = 0x20,
	BCM_REN_BIT = 0x40,
	BCM_EOI_BIT = 0x80,
};

enum ines_aux_reg_bits {
	INES_AUXD = 0x40,
};

enum ines_aux_cmds {
	INES_RFD_HLD_IMMEDIATE = 0x4,
	INES_AUX_CLR_OUT_FIFO = 0x5,
	INES_AUX_CLR_IN_FIFO = 0x6,
	INES_AUX_XMODE = 0xa,
};

enum ines_auxd_bits {
	INES_FOLLOWING_T1_MASK = 0x3,
	INES_FOLLOWING_T1_500ns = 0x0,
	INES_FOLLOWING_T1_350ns = 0x1,
	INES_FOLLOWING_T1_250ns = 0x2,
	INES_INITIAL_TI_MASK = 0xc,
	INES_INITIAL_T1_2000ns = 0x0,
	INES_INITIAL_T1_1100ns = 0x4,
	INES_INITIAL_T1_700ns = 0x8,
	INES_T6_2us = 0x0,
	INES_T6_50us = 0x10,
};

#endif	// _INES_GPIB_H
