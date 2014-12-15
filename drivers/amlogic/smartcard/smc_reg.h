/*
 * AMLOGIC Smart card driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef _SMC_REG_H
#define _SMC_REG_H

#include <asm/byteorder.h>

#ifdef __LITTLE_ENDIAN
  #ifndef __LITTLE_ENDIAN__
    #define __LITTLE_ENDIAN__
  #endif
#endif

#define F_DEFAULT 372
#define D_DEFAULT 1
#define FREQ_DEFAULT 4000 //KHz
#define FIFO_THRESHOLD_DEFAULT 1
#define ETU_DIVIDER_CLOCK_HZ    24000 // KHz
#define ATR_HOLDOFF_EN 1
#define ATR_CLK_MUX_DEFAULT 4
#define ATR_HOLDOFF_TCNT_DEFAULT 255
#define ATR_FINAL_TCNT_DEFAULT 40000
#define DET_FILTER_SEL_DEFAULT 3
#define IO_FILTER_SEL_DEFAULT 3
#define BWT_BASE_DEFAULT 999
#define N_DEFAULT 0
#define CWI_DEFAULT 13
#define BWI_DEFAULT 4
#define BGT_DEFAULT 20
#define CLK_SRC_DEFAULT 0

typedef struct SMCCARD_HW_Reg0
{
#ifdef __LITTLE_ENDIAN__
	unsigned etu_divider: 16;           // Bit 15:0
	unsigned first_etu_offset: 3;       // Bit 18:16
	unsigned enable: 1;                 // Bit 19
	unsigned recv_fifo_threshold: 4;    // Bit 23:20
	unsigned clk_en: 1;                 // Bit 24
	unsigned clk_oen: 1;                // Bit 25
	unsigned rst_level: 1;              // Bit 26
	unsigned start_atr: 1;              // Bit 27
	unsigned unused: 1;                 // Bit 28
	unsigned io_level: 1;               // Bit 29
	unsigned card_detect: 1;            // Bit 30
	unsigned start_atr_en: 1;           // Bit 31
#else
	unsigned start_atr_en: 1;           // Bit 31
	unsigned card_detect: 1;            // Bit 30
	unsigned io_level: 1;               // Bit 29
	unsigned unused: 1;                 // Bit 28
	unsigned start_atr: 1;              // Bit 27
	unsigned rst_level: 1;              // Bit 26
	unsigned clk_oen: 1;                // Bit 25
	unsigned clk_en: 1;                 // Bit 24
	unsigned recv_fifo_threshold: 4;    // Bit 23:20
	unsigned enable: 1;                 // Bit 19
	unsigned first_etu_offset: 3;       // Bit 18:16
	unsigned etu_divider: 16;           // Bit 15:0
#endif
} SMCCARD_HW_Reg0_t;

typedef struct SMC_ANSWER_TO_RST
{
#ifdef __LITTLE_ENDIAN__
	unsigned atr_final_tcnt: 16;        // Bit 15:0
	unsigned atr_holdoff_tcnt: 8;       // Bit 23:16
	unsigned atr_clk_mux: 3;            // Bit 26:24
	unsigned atr_holdoff_en: 1;         // Bit 27
	unsigned unused: 4;                 // Bit 31:28
#else
	unsigned unused: 4;                 // Bit 31:28
	unsigned atr_holdoff_en: 1;         // Bit 27
	unsigned atr_clk_mux: 3;            // Bit 26:24
	unsigned atr_holdoff_tcnt: 8;       // Bit 23:16
	unsigned atr_final_tcnt: 16;        // Bit 15:0
#endif
} SMC_ANSWER_TO_RST_t;

typedef struct SMCCARD_HW_Reg2
{
#ifdef __LITTLE_ENDIAN__
	unsigned xmit_invert: 1;            // Bit 0
	unsigned xmit_lsb_msb: 1;           // Bit 1
	unsigned xmit_parity: 1;            // Bit 2
	unsigned xmit_retries: 3;           // Bit 5:3
	unsigned xmit_repeat_dis: 1;        // Bit 6
	unsigned recv_invert: 1;            // Bit 7
	unsigned recv_lsb_msb: 1;           // Bit 8
	unsigned recv_parity: 1;            // Bit 9
	unsigned recv_no_parity: 1;         // Bit 10
	unsigned pulse_irq: 1;                // Bit 11
	unsigned clk_tcnt: 8;               // Bit 19:12
	unsigned det_filter_sel: 3;         // Bit 22:20
	unsigned io_filter_sel: 3;          // Bit 25:23
	unsigned recv_retry_cnt: 3;         // Bit 28:26
	unsigned clk_sel: 3;                // Bit 31:29
#else
	unsigned clk_sel: 3;                // Bit 31:29
	unsigned recv_retry_cnt: 3;         // Bit 28:26
	unsigned io_filter_sel: 3;          // Bit 25:23
	unsigned det_filter_sel: 3;         // Bit 22:20
	unsigned clk_tcnt: 8;               // Bit 19:12
	unsigned pulse_irq: 1;                // Bit 11
	unsigned recv_no_parity: 1;         // Bit 10
	unsigned recv_parity: 1;            // Bit 9
	unsigned recv_lsb_msb: 1;           // Bit 8
	unsigned recv_invert: 1;            // Bit 7
	unsigned xmit_repeat_dis: 1;        // Bit 6
	unsigned xmit_retries: 3;           // Bit 5:3
	unsigned xmit_parity: 1;            // Bit 2
	unsigned xmit_lsb_msb: 1;           // Bit 1
	unsigned xmit_invert: 1;            // Bit 0
#endif
} SMCCARD_HW_Reg2_t;


typedef struct SMC_STATUS_Reg
{
#ifdef __LITTLE_ENDIAN__
	unsigned recv_fifo_threshold_status: 1;  // Bit 0
	unsigned send_fifo_last_byte_status: 1;  // Bit 1
	unsigned cwt_expeired_status: 1;         // Bit 2
	unsigned bwt_expeired_status: 1;         // Bit 3
	unsigned write_full_send_fifo_status: 1; // Bit 4
	unsigned send_and_recv_confilt_status: 1;// Bit 5
	unsigned recv_error_status: 1;           // Bit 6
	unsigned send_error_status: 1;           // Bit 7
	unsigned rst_expired_status: 1;          // Bit 8
	unsigned card_detect_status: 1;          // Bit 9
	unsigned unused: 6;                      // Bit 15:10
	unsigned recv_fifo_bytes_number: 4;      // Bit 19:16
	unsigned recv_fifo_empty_status: 1;      // Bit 20
	unsigned recv_fifo_full_status: 1;       // Bit 21
	unsigned send_fifo_bytes_number: 4;      // Bit 25:22
	unsigned send_fifo_empty_status: 1;      // Bit 26
	unsigned send_fifo_full_status: 1;       // Bit 27
	unsigned recv_data_from_card_status: 1;  // Bit 28
	unsigned recv_module_enable_status: 1;   // Bit 29
	unsigned send_module_enable_status: 1;   // Bit 30
	unsigned wait_for_atr_status: 1;         // Bit 31
#else
	unsigned wait_for_atr_status: 1;         // Bit 31
	unsigned send_module_enable_status: 1;   // Bit 30
	unsigned recv_module_enable_status: 1;   // Bit 29
	unsigned recv_data_from_card_status: 1;  // Bit 28
	unsigned send_fifo_full_status: 1;       // Bit 27
	unsigned send_fifo_empty_status: 1;      // Bit 26
	unsigned send_fifo_bytes_number: 4;      // Bit 25:22
	unsigned recv_fifo_full_status: 1;       // Bit 21
	unsigned recv_fifo_empty_status: 1;      // Bit 20
	unsigned recv_fifo_bytes_number: 4;      // Bit 19:16
	unsigned unused: 6;                      // Bit 15:10
	unsigned card_detect_status: 1;          // Bit 9
	unsigned rst_expired_status: 1;          // Bit 8
	unsigned send_error_status: 1;           // Bit 7
	unsigned recv_error_status: 1;           // Bit 6
	unsigned send_and_recv_confilt_status: 1;// Bit 5
	unsigned write_full_send_fifo_status: 1; // Bit 4
	unsigned bwt_expeired_status: 1;         // Bit 3
	unsigned cwt_expeired_status: 1;         // Bit 2
	unsigned send_fifo_last_byte_status: 1;  // Bit 1
	unsigned recv_fifo_threshold_status: 1;  // Bit 0
#endif
} SMC_STATUS_Reg_t;

typedef struct SMC_INTERRUPT_Reg
{
#ifdef __LITTLE_ENDIAN__
	unsigned recv_fifo_bytes_threshold_int: 1;      // Bit 0
	unsigned send_fifo_last_byte_int: 1;            // Bit 1
	unsigned cwt_expeired_int: 1;                   // Bit 2
	unsigned bwt_expeired_int: 1;                   // Bit 3
	unsigned write_full_fifo_int: 1;                // Bit 4
	unsigned send_and_recv_confilt_int: 1;          // Bit 5
	unsigned recv_error_int: 1;                     // Bit 6
	unsigned send_error_int: 1;                     // Bit 7
	unsigned rst_expired_int: 1;                    // Bit 8
	unsigned card_detect_int: 1;                    // Bit 9
	unsigned unused1: 6;                            // Bit 15:10
	unsigned recv_fifo_bytes_threshold_int_mask: 1;	// Bit 16
	unsigned send_fifo_last_byte_int_mask: 1;       // Bit 17
	unsigned cwt_expeired_int_mask: 1;              // Bit 18
	unsigned bwt_expeired_int_mask: 1;              // Bit 19
	unsigned write_full_fifo_int_mask: 1;           // Bit 20
	unsigned send_and_recv_confilt_int_mask: 1;     // Bit 21
	unsigned recv_error_int_mask: 1;                // Bit 22
	unsigned send_error_int_mask: 1;                // Bit 23
	unsigned rst_expired_int_mask: 1;               // Bit 24
	unsigned card_detect_int_mask: 1;               // Bit 25
	unsigned unused2: 6;                            // Bit 31:26
#else
	unsigned unused2: 6;                            // Bit 31:26
	unsigned card_detect_int_mask: 1;               // Bit 25
	unsigned rst_expired_int_mask: 1;               // Bit 24
	unsigned send_error_int_mask: 1;                // Bit 23
	unsigned recv_error_int_mask: 1;                // Bit 22
	unsigned send_and_recv_confilt_int_mask: 1;     // Bit 21
	unsigned write_full_fifo_int_mask: 1;           // Bit 20
	unsigned bwt_expeired_int_mask: 1;              // Bit 19
	unsigned cwt_expeired_int_mask: 1;              // Bit 18
	unsigned send_fifo_last_byte_int_mask: 1;       // Bit 17
	unsigned recv_fifo_bytes_threshold_int_mask: 1;	// Bit 16
	unsigned unused1: 6;                            // Bit 15:10
	unsigned card_detect_int: 1;                    // Bit 9
	unsigned rst_expired_int: 1;                    // Bit 8
	unsigned send_error_int: 1;                     // Bit 7
	unsigned recv_error_int: 1;                     // Bit 6
	unsigned send_and_recv_confilt_int: 1;          // Bit 5
	unsigned write_full_fifo_int: 1;                // Bit 4
	unsigned bwt_expeired_int: 1;                   // Bit 3
	unsigned cwt_expeired_int: 1;                   // Bit 2
	unsigned send_fifo_last_byte_int: 1;            // Bit 1
	unsigned recv_fifo_bytes_threshold_int: 1;      // Bit 0
#endif
} SMC_INTERRUPT_Reg_t;

typedef struct SMCCARD_HW_Reg5
{
#ifdef __LITTLE_ENDIAN__
	unsigned bwt_base_time_gnt: 16;        // Bit 15:0
	unsigned btw_detect_en: 1;             // Bit 16
	unsigned cwt_detect_en: 1;             // Bit 17
	unsigned etu_msr_en: 1;                // Bit 18
	unsigned unused: 1;                    // Bit 19
	unsigned etu_msr_cnt: 12;              // Bit 31:20
#else
	unsigned etu_msr_cnt: 12;              // Bit 31:20
	unsigned unused: 1;                    // Bit 19
	unsigned etu_msr_en: 1;                // Bit 18
	unsigned cwt_detect_en: 1;             // Bit 17
	unsigned btw_detect_en: 1;             // Bit 16
	unsigned bwt_base_time_gnt: 16;        // Bit 15:0
#endif
} SMCCARD_HW_Reg5_t;

typedef struct SMCCARD_HW_Reg6
{
#ifdef __LITTLE_ENDIAN__
	unsigned N_parameter: 8;               // Bit 7:0
	unsigned cwi_value: 4;                 // Bit 11:8
	unsigned bgt: 8;                       // Bit 19:12
	unsigned bwi: 4;                       // Bit 23:20
	unsigned unused: 8;                    // Bit 31:24
#else
	unsigned unused: 8;                    // Bit 31:24
	unsigned bwi: 4;                       // Bit 23:20
	unsigned bgt: 8;                       // Bit 19:12
	unsigned cwi_value: 4;                 // Bit 11:8
	unsigned N_parameter: 8;               // Bit 7:0
#endif
} SMCCARD_HW_Reg6_t;

#endif

