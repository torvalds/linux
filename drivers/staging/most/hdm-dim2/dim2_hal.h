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


#ifdef __cplusplus
extern "C" {
#endif

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

typedef int atomic_counter_t;

struct int_ch_state {
	/* changed only in interrupt context */
	volatile atomic_counter_t request_counter;

	/* changed only in task context */
	volatile atomic_counter_t service_counter;

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


u8 DIM_Startup(void *dim_base_address, u32 mlb_clock);

void DIM_Shutdown(void);

bool DIM_GetLockState(void);

u16 DIM_NormCtrlAsyncBufferSize(u16 buf_size);

u16 DIM_NormIsocBufferSize(u16 buf_size, u16 packet_length);

u16 DIM_NormSyncBufferSize(u16 buf_size, u16 bytes_per_frame);

u8 DIM_InitControl(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		   u16 max_buffer_size);

u8 DIM_InitAsync(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		 u16 max_buffer_size);

u8 DIM_InitIsoc(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		u16 packet_length);

u8 DIM_InitSync(struct dim_channel *ch, u8 is_tx, u16 ch_address,
		u16 bytes_per_frame);

u8 DIM_DestroyChannel(struct dim_channel *ch);

void DIM_ServiceIrq(struct dim_channel *const *channels);

u8 DIM_ServiceChannel(struct dim_channel *ch);

struct dim_ch_state_t *DIM_GetChannelState(struct dim_channel *ch,
		struct dim_ch_state_t *dim_ch_state_ptr);

bool DIM_EnqueueBuffer(struct dim_channel *ch, u32 buffer_addr,
		       u16 buffer_size);

bool DIM_DetachBuffers(struct dim_channel *ch, u16 buffers_number);

u32 DIM_ReadRegister(u8 register_index);


u32 DIMCB_IoRead(u32 *ptr32);

void DIMCB_IoWrite(u32 *ptr32, u32 value);

void DIMCB_OnError(u8 error_id, const char *error_message);

void DIMCB_OnFail(const char *filename, int linenum);


#ifdef __cplusplus
}
#endif

#endif /* _DIM2_HAL_H */
