/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    copyright            : (C) 2002 by Frank Mori Hess
 ***************************************************************************/

#include "nec7210.h"
#include "gpibP.h"
#include "plx9050.h"

struct cec_priv  {
	struct nec7210_priv nec7210_priv;
	struct pci_dev *pci_device;
	// base address for plx9052 pci chip
	unsigned long plx_iobase;
	unsigned int irq;
};

// interface functions
int cec_read(gpib_board_t *board, uint8_t *buffer, size_t length, int *end, size_t *bytes_read);
int cec_write(gpib_board_t *board, uint8_t *buffer, size_t length, int send_eoi,
	      size_t *bytes_written);
int cec_command(gpib_board_t *board, uint8_t *buffer, size_t length, size_t *bytes_written);
int cec_take_control(gpib_board_t *board, int synchronous);
int cec_go_to_standby(gpib_board_t *board);
void cec_request_system_control(gpib_board_t *board, int request_control);
void cec_interface_clear(gpib_board_t *board, int assert);
void cec_remote_enable(gpib_board_t *board, int enable);
int cec_enable_eos(gpib_board_t *board, uint8_t eos_byte, int compare_8_bits);
void cec_disable_eos(gpib_board_t *board);
unsigned int cec_update_status(gpib_board_t *board, unsigned int clear_mask);
int cec_primary_address(gpib_board_t *board, unsigned int address);
int cec_secondary_address(gpib_board_t *board, unsigned int address, int enable);
int cec_parallel_poll(gpib_board_t *board, uint8_t *result);
void cec_parallel_poll_configure(gpib_board_t *board, uint8_t configuration);
void cec_parallel_poll_response(gpib_board_t *board, int ist);
void cec_serial_poll_response(gpib_board_t *board, uint8_t status);
void cec_return_to_local(gpib_board_t *board);

// interrupt service routines
irqreturn_t cec_interrupt(int irq, void *arg);

// utility functions
void cec_free_private(gpib_board_t *board);
int cec_generic_attach(gpib_board_t *board);
void cec_init(struct cec_priv *priv, const gpib_board_t *board);

// offset between consecutive nec7210 registers
static const int cec_reg_offset = 1;
