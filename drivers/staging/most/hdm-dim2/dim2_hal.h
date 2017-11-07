// SPDX-License-Identifier: GPL-2.0
/*
 * dim2_hal.h - DIM2 HAL interface
 * (MediaLB, Device Interface Macro IP, OS62420)
 *
 * Copyright (C) 2015, Microchip Technology Germany II GmbH & Co. KG
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is licensed under GPLv2.
 */

#ifndef _DIM2_HAL_H
#define _DIM2_HAL_H

#include <linux/types.h>
#include "dim2_reg.h"

/*
 * The values below are specified in the hardware specification.
 * So, they should not be changed until the hardware specification changes.
 */
enum mlb_clk_speed {
	CLK_256FS = 0,
	CLK_512FS = 1,
	CLK_1024FS = 2,
	CLK_2048FS = 3,
	CLK_3072FS = 4,
	CLK_4096FS = 5,
	CLK_6144FS = 6,
	CLK_8192FS = 7,
};

struct dim_ch_state_t {
	bool ready; /* Shows readiness to enqueue next buffer */
	u16 done_buffers; /* Number of completed buffers */
};

struct int_ch_state {
	/* changed only in interrupt context */
	volatile int request_counter;

	/* changed only in task context */
	volatile int service_counter;

	u8 idx1;
	u8 idx2;
	u8 level; /* [0..2], buffering level */
};

struct dim_channel {
	struct int_ch_state state;
	u8 addr;
	u16 dbr_addr;
	u16 dbr_size;
	u16 packet_length; /*< Isochronous packet length in bytes. */
	u16 bytes_per_frame; /*< Synchronous bytes per frame. */
	u16 done_sw_buffers_number; /*< Done software buffers number. */
};

u8 dim_startup(struct dim2_regs __iomem *dim_base_address, u32 mlb_clock,
	       u32 fcnt);

void dim_shutdown(void);

bool dim_get_lock_state(void);

u16 dim_norm_ctrl_async_buffer_size(u16 buf_size);

u16 dim_norm_isoc_buffer_size(u16 buf_size, u16 packet_length);

u16 dim_norm_sync_buffer_size(u16 buf_size, u16 bytes_per_frame);

u8 dim_init_control(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		    u16 max_buffer_size);

u8 dim_init_async(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		  u16 max_buffer_size);

u8 dim_init_isoc(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		 u16 packet_length);

u8 dim_init_sync(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		 u16 bytes_per_frame);

u8 dim_destroy_channel(struct dim_channel *ch);

void dim_service_mlb_int_irq(void);

void dim_service_ahb_int_irq(struct dim_channel *const *channels);

u8 dim_service_channel(struct dim_channel *ch);

struct dim_ch_state_t *dim_get_channel_state(struct dim_channel *ch,
					     struct dim_ch_state_t *state_ptr);

u16 dim_dbr_space(struct dim_channel *ch);

bool dim_enqueue_buffer(struct dim_channel *ch, u32 buffer_addr,
			u16 buffer_size);

bool dim_detach_buffers(struct dim_channel *ch, u16 buffers_number);

u32 dimcb_io_read(u32 __iomem *ptr32);

void dimcb_io_write(u32 __iomem *ptr32, u32 value);

void dimcb_on_error(u8 error_id, const char *error_message);

#endif /* _DIM2_HAL_H */
