//* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    copyright            : (C) 2002 by Frank Mori Hess
 ***************************************************************************/

#ifndef _NEC7210_H
#define _NEC7210_H

#include "gpib_state_machines.h"
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/interrupt.h>

#include "gpib_types.h"
#include "nec7210_registers.h"

/* struct used to provide variables local to a nec7210 chip */
struct nec7210_priv {
#ifdef CONFIG_HAS_IOPORT
	u32 iobase;
#endif
	void __iomem *mmiobase;
	unsigned int offset;	// offset between successive nec7210 io addresses
	unsigned int dma_channel;
	u8 *dma_buffer;
	unsigned int dma_buffer_length;	// length of dma buffer
	dma_addr_t dma_buffer_addr;	// bus address of board->buffer for use with dma
	// software copy of bits written to registers
	u8 reg_bits[8];
	u8 auxa_bits;	// bits written to auxiliary register A
	u8 auxb_bits;	// bits written to auxiliary register B
	// used to keep track of board's state, bit definitions given below
	unsigned long state;
	/* lock for chips that extend the nec7210 registers by paging in alternate regs */
	spinlock_t register_page_lock;
	// wrappers for outb, inb, readb, or writeb
	u8 (*read_byte)(struct nec7210_priv *priv, unsigned int register_number);
	void (*write_byte)(struct nec7210_priv *priv, u8 byte, unsigned int register_number);
	enum nec7210_chipset type;
	enum talker_function_state talker_state;
	enum listener_function_state listener_state;
	void *private;
	unsigned srq_pending : 1;
};

static inline void init_nec7210_private(struct nec7210_priv *priv)
{
	memset(priv, 0, sizeof(struct nec7210_priv));
	spin_lock_init(&priv->register_page_lock);
}

// slightly shorter way to access read_byte and write_byte
static inline u8 read_byte(struct nec7210_priv *priv, unsigned int register_number)
{
	return priv->read_byte(priv, register_number);
}

static inline void write_byte(struct nec7210_priv *priv, u8 byte, unsigned int register_number)
{
	priv->write_byte(priv, byte, register_number);
}

// struct nec7210_priv.state bit numbers
enum {
	PIO_IN_PROGRESS_BN,	// pio transfer in progress
	DMA_READ_IN_PROGRESS_BN,	// dma read transfer in progress
	DMA_WRITE_IN_PROGRESS_BN,	// dma write transfer in progress
	READ_READY_BN,	// board has data byte available to read
	WRITE_READY_BN,	// board is ready to send a data byte
	COMMAND_READY_BN,	// board is ready to send a command byte
	RECEIVED_END_BN,	// received END
	BUS_ERROR_BN,	// output error has occurred
	RFD_HOLDOFF_BN,	// rfd holdoff in effect
	DEV_CLEAR_BN,	// device clear received
	ADR_CHANGE_BN,	// address state change occurred
};

// interface functions
int nec7210_read(struct gpib_board *board, struct nec7210_priv *priv, uint8_t *buffer,
		 size_t length, int *end, size_t *bytes_read);
int nec7210_write(struct gpib_board *board, struct nec7210_priv *priv, uint8_t *buffer,
		  size_t length, int send_eoi, size_t *bytes_written);
int nec7210_command(struct gpib_board *board, struct nec7210_priv *priv, uint8_t *buffer,
		    size_t length, size_t *bytes_written);
int nec7210_take_control(struct gpib_board *board, struct nec7210_priv *priv, int syncronous);
int nec7210_go_to_standby(struct gpib_board *board, struct nec7210_priv *priv);
void nec7210_request_system_control(struct gpib_board *board,
				    struct nec7210_priv *priv, int request_control);
void nec7210_interface_clear(struct gpib_board *board, struct nec7210_priv *priv, int assert);
void nec7210_remote_enable(struct gpib_board *board, struct nec7210_priv *priv, int enable);
int nec7210_enable_eos(struct gpib_board *board, struct nec7210_priv *priv, uint8_t eos_bytes,
		       int compare_8_bits);
void nec7210_disable_eos(struct gpib_board *board, struct nec7210_priv *priv);
unsigned int nec7210_update_status(struct gpib_board *board, struct nec7210_priv *priv,
				   unsigned int clear_mask);
unsigned int nec7210_update_status_nolock(struct gpib_board *board, struct nec7210_priv *priv);
int nec7210_primary_address(const struct gpib_board *board,
			    struct nec7210_priv *priv, unsigned int address);
int nec7210_secondary_address(const struct gpib_board *board, struct nec7210_priv *priv,
			      unsigned int address, int enable);
int nec7210_parallel_poll(struct gpib_board *board, struct nec7210_priv *priv, uint8_t *result);
void nec7210_serial_poll_response(struct gpib_board *board, struct nec7210_priv *priv, uint8_t status);
void nec7210_parallel_poll_configure(struct gpib_board *board,
				     struct nec7210_priv *priv, unsigned int configuration);
void nec7210_parallel_poll_response(struct gpib_board *board,
				    struct nec7210_priv *priv, int ist);
uint8_t nec7210_serial_poll_status(struct gpib_board *board,
				   struct nec7210_priv *priv);
int nec7210_t1_delay(struct gpib_board *board,
		     struct nec7210_priv *priv, unsigned int nano_sec);
void nec7210_return_to_local(const struct gpib_board *board, struct nec7210_priv *priv);

// utility functions
void nec7210_board_reset(struct nec7210_priv *priv, const struct gpib_board *board);
void nec7210_board_online(struct nec7210_priv *priv, const struct gpib_board *board);
unsigned int nec7210_set_reg_bits(struct nec7210_priv *priv, unsigned int reg,
				  unsigned int mask, unsigned int bits);
void nec7210_set_handshake_mode(struct gpib_board *board, struct nec7210_priv *priv, int mode);
void nec7210_release_rfd_holdoff(struct gpib_board *board, struct nec7210_priv *priv);
uint8_t nec7210_read_data_in(struct gpib_board *board, struct nec7210_priv *priv, int *end);

// wrappers for io functions
uint8_t nec7210_ioport_read_byte(struct nec7210_priv *priv, unsigned int register_num);
void nec7210_ioport_write_byte(struct nec7210_priv *priv, uint8_t data, unsigned int register_num);
uint8_t nec7210_iomem_read_byte(struct nec7210_priv *priv, unsigned int register_num);
void nec7210_iomem_write_byte(struct nec7210_priv *priv, uint8_t data, unsigned int register_num);
uint8_t nec7210_locking_ioport_read_byte(struct nec7210_priv *priv, unsigned int register_num);
void nec7210_locking_ioport_write_byte(struct nec7210_priv *priv, uint8_t data,
				       unsigned int register_num);
uint8_t nec7210_locking_iomem_read_byte(struct nec7210_priv *priv, unsigned int register_num);
void nec7210_locking_iomem_write_byte(struct nec7210_priv *priv, uint8_t data,
				      unsigned int register_num);

// interrupt service routine
irqreturn_t nec7210_interrupt(struct gpib_board *board, struct nec7210_priv *priv);
irqreturn_t nec7210_interrupt_have_status(struct gpib_board *board,
					  struct nec7210_priv *priv, int status1, int status2);

#endif	//_NEC7210_H
