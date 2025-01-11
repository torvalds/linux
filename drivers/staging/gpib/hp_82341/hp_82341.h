/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    copyright            : (C) 2002, 2005 by Frank Mori Hess             *
 ***************************************************************************/

#include "tms9914.h"
#include "gpibP.h"

enum hp_82341_hardware_version {
	HW_VERSION_UNKNOWN,
	HW_VERSION_82341C,
	HW_VERSION_82341D,
};

// struct which defines private_data for board
struct hp_82341_priv {
	struct tms9914_priv tms9914_priv;
	unsigned int irq;
	unsigned short config_control_bits;
	unsigned short mode_control_bits;
	unsigned short event_status_bits;
	struct pnp_dev *pnp_dev;
	unsigned long iobase[4];
	unsigned long io_region_offset;
	enum hp_82341_hardware_version hw_version;
};


// interface functions
int hp_82341_accel_read(gpib_board_t *board, uint8_t *buffer, size_t length, int *end,
			size_t *bytes_read);
int hp_82341_accel_write(gpib_board_t *board, uint8_t *buffer, size_t length, int send_eoi,
			 size_t *bytes_written);
int hp_82341_read(gpib_board_t *board, uint8_t *buffer, size_t length, int *end,
		  size_t *bytes_read);
int hp_82341_write(gpib_board_t *board, uint8_t *buffer, size_t length, int send_eoi,
		   size_t *bytes_written);
int hp_82341_command(gpib_board_t *board, uint8_t *buffer, size_t length, size_t *bytes_written);
int hp_82341_take_control(gpib_board_t *board, int synchronous);
int hp_82341_go_to_standby(gpib_board_t *board);
void hp_82341_request_system_control(gpib_board_t *board, int request_control);
void hp_82341_interface_clear(gpib_board_t *board, int assert);
void hp_82341_remote_enable(gpib_board_t *board, int enable);
int hp_82341_enable_eos(gpib_board_t *board, uint8_t eos_byte, int
			compare_8_bits);
void hp_82341_disable_eos(gpib_board_t *board);
unsigned int hp_82341_update_status(gpib_board_t *board, unsigned int clear_mask);
int hp_82341_primary_address(gpib_board_t *board, unsigned int address);
int hp_82341_secondary_address(gpib_board_t *board, unsigned int address, int
			enable);
int hp_82341_parallel_poll(gpib_board_t *board, uint8_t *result);
void hp_82341_parallel_poll_configure(gpib_board_t *board, uint8_t config);
void hp_82341_parallel_poll_response(gpib_board_t *board, int ist);
void hp_82341_serial_poll_response(gpib_board_t *board, uint8_t status);
void hp_82341_return_to_local(gpib_board_t *board);

// interrupt service routines
irqreturn_t hp_82341_interrupt(int irq, void *arg);

// utility functions
int hp_82341_allocate_private(gpib_board_t *board);
void hp_82341_free_private(gpib_board_t *board);

static const int hp_82341_region_iosize = 0x8;
static const int hp_82341_num_io_regions = 4;
static const int hp_82341_fifo_size = 0xffe;
static const int hp_82341c_firmware_length = 5764;
static const int hp_82341d_firmware_length = 5302;

// hp 82341 register offsets
enum hp_82341_region_0_registers {
	CONFIG_CONTROL_STATUS_REG = 0x0,
	MODE_CONTROL_STATUS_REG = 0x1,
	MONITOR_REG = 0x2,	// after initialization
	XILINX_DATA_REG = 0x2,	// before initialization, write only
	INTERRUPT_ENABLE_REG = 0x3,
	EVENT_STATUS_REG = 0x4,
	EVENT_ENABLE_REG = 0x5,
	STREAM_STATUS_REG = 0x7,
};

enum hp_82341_region_1_registers {
	ID0_REG = 0x2,
	ID1_REG = 0x3,
	TRANSFER_COUNT_LOW_REG = 0x4,
	TRANSFER_COUNT_MID_REG = 0x5,
	TRANSFER_COUNT_HIGH_REG = 0x6,
};

enum hp_82341_region_3_registers {
	BUFFER_PORT_LOW_REG = 0x0,
	BUFFER_PORT_HIGH_REG = 0x1,
	ID2_REG = 0x2,
	ID3_REG = 0x3,
	BUFFER_FLUSH_REG = 0x4,
	BUFFER_CONTROL_REG = 0x7
};

enum config_control_status_bits {
	IRQ_SELECT_MASK = 0x7,
	DMA_CONFIG_MASK = 0x18,
	ENABLE_DMA_CONFIG_BIT = 0x20,
	XILINX_READY_BIT = 0x40,	//read only
	DONE_PGL_BIT = 0x80
};

static inline unsigned int IRQ_SELECT_BITS(int irq)
{
	switch (irq) {
	case 3:
		return 0x3;
	case 5:
		return 0x2;
	case 7:
		return 0x1;
	case 9:
		return 0x0;
	case 10:
		return 0x7;
	case 11:
		return 0x6;
	case 12:
		return 0x5;
	case 15:
		return 0x4;
	default:
		return 0x0;
	}
};

enum mode_control_status_bits {
	SLOT8_BIT = 0x1,	// read only
	ACTIVE_CONTROLLER_BIT = 0x2,	// read only
	ENABLE_DMA_BIT = 0x4,
	SYSTEM_CONTROLLER_BIT = 0x8,
	MONITOR_BIT = 0x10,
	ENABLE_IRQ_CONFIG_BIT = 0x20,
	ENABLE_TI_STREAM_BIT = 0x40
};

enum monitor_bits {
	MONITOR_INTERRUPT_PENDING_BIT = 0x1,	// read only
	MONITOR_CLEAR_HOLDOFF_BIT = 0x2,	// write only
	MONITOR_PPOLL_BIT = 0x4,	// write clear
	MONITOR_SRQ_BIT = 0x8,	// write clear
	MONITOR_IFC_BIT = 0x10,	// write clear
	MONITOR_REN_BIT = 0x20,	// write clear
	MONITOR_END_BIT = 0x40,	// write clear
	MONITOR_DAV_BIT = 0x80	// write clear
};

enum interrupt_enable_bits {
	ENABLE_TI_INTERRUPT_BIT = 0x1,
	ENABLE_POINTERS_EQUAL_INTERRUPT_BIT = 0x4,
	ENABLE_BUFFER_END_INTERRUPT_BIT = 0x10,
	ENABLE_TERMINAL_COUNT_INTERRUPT_BIT = 0x20,
	ENABLE_DMA_TERMINAL_COUNT_INTERRUPT_BIT = 0x80,
};

enum event_status_bits {
	TI_INTERRUPT_EVENT_BIT = 0x1,	//write clear
	INTERRUPT_PENDING_EVENT_BIT = 0x2,	// read only
	POINTERS_EQUAL_EVENT_BIT = 0x4,	//write clear
	BUFFER_END_EVENT_BIT = 0x10,	//write clear
	TERMINAL_COUNT_EVENT_BIT = 0x20,	// write clear
	DMA_TERMINAL_COUNT_EVENT_BIT = 0x80,	// write clear
};

enum event_enable_bits {
	ENABLE_TI_INTERRUPT_EVENT_BIT = 0x1,	//write clear
	ENABLE_POINTERS_EQUAL_EVENT_BIT = 0x4,	//write clear
	ENABLE_BUFFER_END_EVENT_BIT = 0x10,	//write clear
	ENABLE_TERMINAL_COUNT_EVENT_BIT = 0x20,	// write clear
	ENABLE_DMA_TERMINAL_COUNT_EVENT_BIT = 0x80,	// write clear
};

enum stream_status_bits {
	HALTED_STATUS_BIT = 0x1,	//read
	RESTART_STREAM_BIT = 0x1	//write
};

enum buffer_control_bits {
	DIRECTION_GPIB_TO_HOST_BIT = 0x20,	// transfer direction (set for gpib to host)
	ENABLE_TI_BUFFER_BIT = 0x40,	//enable fifo
	FAST_WR_EN_BIT = 0x80,	// 350 ns t1 delay?
};

// registers accessible through isapnp chip on 82341d
enum hp_82341d_pnp_registers {
	PIO_DATA_REG = 0x20,	//read/write pio data lines
	PIO_DIRECTION_REG = 0x21,	// set pio data line directions (set for input)
};

enum hp_82341d_pnp_pio_bits {
	HP_82341D_XILINX_READY_BIT = 0x1,
	HP_82341D_XILINX_DONE_BIT = 0x2,
	// use register layout compatible with C and older versions instead of 32 contiguous ioports
	HP_82341D_LEGACY_MODE_BIT = 0x4,
	HP_82341D_NOT_PROG_BIT = 0x8,	// clear to reinitialize xilinx
};

unsigned short read_and_clear_event_status(gpib_board_t *board);
int read_transfer_counter(struct hp_82341_priv *hp_priv);
void set_transfer_counter(struct hp_82341_priv *hp_priv, int count);
