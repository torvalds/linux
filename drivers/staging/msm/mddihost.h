/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Code Aurora nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef MDDIHOST_H
#define MDDIHOST_H

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include "linux/proc_fs.h"
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>

#include <mach/hardware.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>

#include "msm_fb_panel.h"

#undef FEATURE_MDDI_MC4
#undef FEATURE_MDDI_S6D0142
#undef FEATURE_MDDI_HITACHI
#define FEATURE_MDDI_SHARP
#define FEATURE_MDDI_TOSHIBA
#undef FEATURE_MDDI_E751
#define FEATURE_MDDI_CORONA
#define FEATURE_MDDI_PRISM

#define T_MSM7500

typedef enum {
	format_16bpp,
	format_18bpp,
	format_24bpp
} mddi_video_format;

typedef enum {
	MDDI_LCD_NONE = 0,
	MDDI_LCD_MC4,
	MDDI_LCD_S6D0142,
	MDDI_LCD_SHARP,
	MDDI_LCD_E751,
	MDDI_LCD_CORONA,
	MDDI_LCD_HITACHI,
	MDDI_LCD_TOSHIBA,
	MDDI_LCD_PRISM,
	MDDI_LCD_TP2,
	MDDI_NUM_LCD_TYPES,
	MDDI_LCD_DEFAULT = MDDI_LCD_TOSHIBA
} mddi_lcd_type;

typedef enum {
	MDDI_HOST_PRIM = 0,
	MDDI_HOST_EXT,
	MDDI_NUM_HOST_CORES
} mddi_host_type;

typedef enum {
	MDDI_DRIVER_RESET,	/* host core registers have not been written. */
	MDDI_DRIVER_DISABLED,	/* registers written, interrupts disabled. */
	MDDI_DRIVER_ENABLED	/* registers written, interrupts enabled. */
} mddi_host_driver_state_type;

typedef enum {
	MDDI_GPIO_INT_0 = 0,
	MDDI_GPIO_INT_1,
	MDDI_GPIO_INT_2,
	MDDI_GPIO_INT_3,
	MDDI_GPIO_INT_4,
	MDDI_GPIO_INT_5,
	MDDI_GPIO_INT_6,
	MDDI_GPIO_INT_7,
	MDDI_GPIO_INT_8,
	MDDI_GPIO_INT_9,
	MDDI_GPIO_INT_10,
	MDDI_GPIO_INT_11,
	MDDI_GPIO_INT_12,
	MDDI_GPIO_INT_13,
	MDDI_GPIO_INT_14,
	MDDI_GPIO_INT_15,
	MDDI_GPIO_NUM_INTS
} mddi_gpio_int_type;

enum mddi_data_packet_size_type {
	MDDI_DATA_PACKET_4_BYTES  = 4,
	MDDI_DATA_PACKET_8_BYTES  = 8,
	MDDI_DATA_PACKET_12_BYTES = 12,
	MDDI_DATA_PACKET_16_BYTES = 16,
	MDDI_DATA_PACKET_24_BYTES = 24
};

typedef struct {
	uint32 addr;
	uint32 value;
} mddi_reg_write_type;

boolean mddi_vsync_set_handler(msm_fb_vsync_handler_type handler, void *arg);

typedef void (*mddi_llist_done_cb_type) (void);

typedef void (*mddi_rev_handler_type) (void *);

boolean mddi_set_rev_handler(mddi_rev_handler_type handler, uint16 pkt_type);

#define MDDI_DEFAULT_PRIM_PIX_ATTR 0xC3
#define MDDI_DEFAULT_SECD_PIX_ATTR 0xC0

typedef int gpio_int_polarity_type;
typedef int gpio_int_handler_type;

typedef struct {
	void (*vsync_detected) (boolean);
} mddi_lcd_func_type;

extern mddi_lcd_func_type mddi_lcd;
void mddi_init(void);

void mddi_powerdown(void);

void mddi_host_start_ext_display(void);
void mddi_host_stop_ext_display(void);

extern spinlock_t mddi_host_spin_lock;
#ifdef T_MSM7500
void mddi_reset(void);
#ifdef FEATURE_DUAL_PROC_MODEM_DISPLAY
void mddi_host_switch_proc_control(boolean on);
#endif
#endif
void mddi_host_exit_power_collapse(void);

void mddi_queue_splash_screen
    (void *buf_ptr,
     boolean clear_area,
     int16 src_width,
     int16 src_starting_row,
     int16 src_starting_column,
     int16 num_of_rows,
     int16 num_of_columns, int16 dst_starting_row, int16 dst_starting_column);

void mddi_queue_image
    (void *buf_ptr,
     uint8 stereo_video,
     boolean clear_area,
     int16 src_width,
     int16 src_starting_row,
     int16 src_starting_column,
     int16 num_of_rows,
     int16 num_of_columns, int16 dst_starting_row, int16 dst_starting_column);

int mddi_host_register_read
    (uint32 reg_addr,
     uint32 *reg_value_ptr, boolean wait, mddi_host_type host_idx);
int mddi_host_register_write
    (uint32 reg_addr, uint32 reg_val,
     enum mddi_data_packet_size_type packet_size,
     boolean wait, mddi_llist_done_cb_type done_cb, mddi_host_type host);
boolean mddi_host_register_write_int
    (uint32 reg_addr,
     uint32 reg_val, mddi_llist_done_cb_type done_cb, mddi_host_type host);
boolean mddi_host_register_read_int
    (uint32 reg_addr, uint32 *reg_value_ptr, mddi_host_type host_idx);
void mddi_queue_register_write_static
    (uint32 reg_addr,
     uint32 reg_val, boolean wait, mddi_llist_done_cb_type done_cb);
void mddi_queue_static_window_adjust
    (const mddi_reg_write_type *reg_write,
     uint16 num_writes, mddi_llist_done_cb_type done_cb);

#define mddi_queue_register_read(reg, val_ptr, wait, sig) \
	mddi_host_register_read(reg, val_ptr, wait, MDDI_HOST_PRIM)
#define mddi_queue_register_write(reg, val, wait, sig) \
	mddi_host_register_write(reg, val, MDDI_DATA_PACKET_4_BYTES,\
	wait, NULL, MDDI_HOST_PRIM)
#define mddi_queue_register_write_extn(reg, val, pkt_size, wait, sig) \
	mddi_host_register_write(reg, val, pkt_size, \
	wait, NULL, MDDI_HOST_PRIM)
#define mddi_queue_register_write_int(reg, val) \
	mddi_host_register_write_int(reg, val, NULL, MDDI_HOST_PRIM)
#define mddi_queue_register_read_int(reg, val_ptr) \
	mddi_host_register_read_int(reg, val_ptr, MDDI_HOST_PRIM)
#define mddi_queue_register_writes(reg_ptr, val, wait, sig) \
	mddi_host_register_writes(reg_ptr, val, wait, sig, MDDI_HOST_PRIM)

void mddi_wait(uint16 time_ms);
void mddi_assign_max_pkt_dimensions(uint16 image_cols,
				    uint16 image_rows,
				    uint16 bpp,
				    uint16 *max_cols, uint16 * max_rows);
uint16 mddi_assign_pkt_height(uint16 pkt_width, uint16 pkt_height, uint16 bpp);
void mddi_queue_reverse_encapsulation(boolean wait);
void mddi_disable(int lock);
#endif /* MDDIHOST_H */
