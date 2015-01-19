
/*******************************************************************
 * 
 *  Copyright C 2005 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description: 
 *
 *  Author: Amlogic Software
 *  Created: Thu Aug 30 17:34:00 2006
 *
 *******************************************************************/


#ifndef xd_enc_h
#define xd_enc_h

#define     ENC_PERIPHS_PIN_MUX_0       ((volatile unsigned long *)(0x01200000+0xb0))
#define     ENC_PERIPHS_PIN_MUX_1       ((volatile unsigned long *)(0x01200000+0xb4))
#define     ENC_PERIPHS_PIN_MUX_2       ((volatile unsigned long *)(0x01200000+0xb8))

#define     ENC_IF_BASE_ADDR   0x01200000
#define     ENC_STREAM_EVENT_INFO      (volatile unsigned long *)(ENC_IF_BASE_ADDR + 0x210)
#define     ENC_STREAM_OUTPUT_CONFIG   (volatile unsigned long *)(ENC_IF_BASE_ADDR + 0x214)
#define     ENC_C_D_BUS_CONTROL        (volatile unsigned long *)(ENC_IF_BASE_ADDR + 0x218)
#define     ENC_C_DATA                 (volatile unsigned long *)(ENC_IF_BASE_ADDR + 0x21c)
#define     ENC_STREAM_BUS_CONFIG      (volatile unsigned long *)(ENC_IF_BASE_ADDR + 0x220)
#define     ENC_STREAM_DATA_IN_CONFIG  (volatile unsigned long *)(ENC_IF_BASE_ADDR + 0x224)
#define     ENC_STREAM_WAIT_IRQ_CONFIG (volatile unsigned long *)(ENC_IF_BASE_ADDR + 0x228)
#define     ENC_STREAM_EVENT_CTL       (volatile unsigned long *)(ENC_IF_BASE_ADDR + 0x22c)

#define xd_event_length_bit  24
#define xd_bus_1st_sel_1_bit 22  // 00-gpio, 01-reserved, 10-addr, 11-data
#define xd_bus_2nd_sel_1_bit 20
#define xd_bus_1st_sel_0_bit 18
#define xd_bus_2nd_sel_0_bit 16
#define xd_stream_gpio_bit   6

#define xd_bus_width_1_bit 28
#define xd_bus_start_pin_1_bit 24
#define xd_bus_sel_chang_point_1_bit 16
#define xd_bus_width_0_bit 12
#define xd_bus_start_pin_0_bit 8
#define xd_bus_sel_chang_point_0_bit 0

#define xd_clock_divide_bit      24 
#define xd_clock_output_sel_bit  20 

#define xd_inc_event_addr_bit       19 
#define xd_async_fifo_endian_bit    18 
#define xd_send_to_async_fifo_bit   17 
#define xd_data_in_serial_lsb_bit   16 
#define xd_invert_no_wait_condition_2_0_bit   15 
#define xd_invert_no_wait_condition_2_1_bit   14 
#define xd_invert_no_wait_condition_2_2_bit   13 
#define xd_invert_data_bus_bit   12 
#define xd_invert_clock_in_bit   11 
#define xd_event_wait_clk_en_bit 10 
#define xd_data_in_serial_bit     9 
#define xd_invert_data_in_clk_bit 8 
#define xd_data_in_begin_bit      4 
#define xd_data_in_clk_sel_bit    0 


#define     xd_no_wait_condition_0_bit  28
#define     xd_no_wait_condition_1_bit  24
#define     xd_no_wait_condition_2_bit  20
#define     xd_irq_input_sel_bit        16
#define     xd_interrupt_status_bit     13
#define     xd_invert_irq_0_bit         11
#define     xd_invert_irq_1_bit         10
#define     xd_enable_transfer_end_irq   9
#define     xd_enable_second_ext_irq_bit 8
#define     xd_no_wait_condition_check_point_bit 0 

#define     xd_no_wait_condition_0_bit  28
#define     xd_no_wait_condition_1_bit  24
#define     xd_no_wait_condition_2_bit  20
#define     xd_irq_input_sel_bit        16
#define     xd_interrupt_status_bit     13
#define     xd_enable_sdata_irq         12
#define     xd_invert_irq_0_bit         11
#define     xd_invert_irq_1_bit         10
#define     xd_enable_transfer_end_irq   9
#define     xd_enable_second_ext_irq_bit 8
#define     xd_no_wait_condition_check_point_bit 0 

#define xd_clock_divide_ext_bit   24
#define xd_s_bus_start_bit        20
#define xd_no_sclk_on_pin_bit     19
#define xd_invert_sclk_in_bit     18
#define xd_sdata_send_busy_bit    17
#define xd_one_sdata_received_bit 16
#define xd_sdata_parity_bit       15
#define xd_sdata_send_type_bit    14
#define xd_sdata_receive_type_bit 13
#define xd_invert_request_out_bit 12
#define xd_request_out_sel_bit     8
#define xd_stop_request_count_bit  0

#define XD_BUS_OUTPUT_GPIO     0
#define XD_BUS_OUTPUT_RESERVED 1
#define XD_BUS_OUTPUT_ADDR     2
#define XD_BUS_OUTPUT_DATA     3

#define XD_CONFIG_INPUT        0xffff 
#define XD_CONFIG_OUTPUT    0xfffe 
#define XD_NO_OUTOUT_CF0 (0x00000000 | XD_CONFIG_OUTPUT)   // RE
#define XD_NO_OUTOUT_CF1 (0x00010000 | XD_CONFIG_OUTPUT)   // WE
#define XD_NO_OUTOUT_CF2 (0x00020000 | XD_CONFIG_INPUT)    // R/-B
#define XD_NO_OUTOUT_CF3 (0x00030000 | XD_CONFIG_OUTPUT)   // CE
#define XD_NO_OUTOUT_CF4 (0x00040000 | XD_CONFIG_OUTPUT)   // ALE
#define XD_NO_OUTOUT_CF5 (0x00050000 | XD_CONFIG_OUTPUT)   // CLE


#define XD_RW_PULSE_WIDTH 6 

#define XD_EVENT_LENGTH (XD_RW_PULSE_WIDTH + 3)
#define XD_RW_PULSE_START 0
#define XD_RW_PULSE_END (XD_RW_PULSE_WIDTH + XD_RW_PULSE_START)
#define XD_CLE_START 0
#define XD_CLE_END (XD_RW_PULSE_WIDTH + XD_CLE_START + 1)
#define XD_ALE_START 0
#define XD_ALE_END (XD_RW_PULSE_WIDTH + XD_CLE_START + 2)

#define XD_DATA_PIN_BEGIN 8
#define XD_RD_PULSE_PIN   0


#define XD_DATA_IN_CONFIG ( \
			     (0 << xd_clock_divide_bit) \
                |(0 << xd_clock_output_sel_bit ) \
                |(0 << xd_inc_event_addr_bit ) \
                |(1 << xd_async_fifo_endian_bit ) \
                |(0 << xd_invert_clock_in_bit ) \
                |(0 << xd_event_wait_clk_en_bit ) \
                |(0 << xd_data_in_serial_bit ) \
			    |(0 << xd_invert_data_in_clk_bit) \
			    |(XD_DATA_PIN_BEGIN << xd_data_in_begin_bit) \
			    |(XD_RD_PULSE_PIN << xd_data_in_clk_sel_bit) \
			  )

#define XD_WAIT_IRQ_CONFIG ( \
			     (0 << xd_no_wait_condition_0_bit) \
                |(0 << xd_irq_input_sel_bit ) \
                |(0 << xd_invert_irq_0_bit ) \
                |(0 << xd_invert_irq_1_bit ) \
			    |(1 << xd_enable_transfer_end_irq) \
			    |(0 << xd_enable_second_ext_irq_bit) \
			    |(0xff << xd_no_wait_condition_check_point_bit) \
			  )

#define XD_WR_EVENT_INFO ( \
			     (XD_EVENT_LENGTH << xd_event_length_bit) \
                |(XD_BUS_OUTPUT_DATA << xd_bus_1st_sel_0_bit ) \
                |(XD_BUS_OUTPUT_DATA << xd_bus_2nd_sel_0_bit ) \
			    |(0x3ff<<xd_stream_gpio_bit) \
			    |0x0f \
			  )

#define XD_RD_EVENT_INFO ( \
			     (XD_EVENT_LENGTH << xd_event_length_bit) \
                |(XD_BUS_OUTPUT_GPIO << xd_bus_1st_sel_0_bit ) \
                |(XD_BUS_OUTPUT_GPIO << xd_bus_2nd_sel_0_bit ) \
			    |(0x3ff<<xd_stream_gpio_bit) \
			    |0x0f \
			  )


#define XD_CMD_WR_OUTOUT_CF0 (0x00000000 | XD_CONFIG_OUTPUT) // Re_n
#define XD_CMD_WR_OUTOUT_CF1 (0x00010000 | (XD_RW_PULSE_END << 8) | XD_RW_PULSE_START)   // We_n
#define XD_CMD_WR_OUTOUT_CF2 (0x00020000 | XD_CONFIG_INPUT) // Rb_n input
#define XD_CMD_WR_OUTOUT_CF3 (0x00030000 | XD_CONFIG_OUTPUT) // chip select
#define XD_CMD_WR_OUTOUT_CF4 (0x00040000 | XD_CONFIG_OUTPUT) // Ale 
#define XD_CMD_WR_OUTOUT_CF5 (0x00050000 | (XD_CLE_END << 8) | XD_CLE_START) // Cle

#define XD_ADDR_WR_OUTOUT_CF0 (0x00000000 | XD_CONFIG_OUTPUT) // Re_n
#define XD_ADDR_WR_OUTOUT_CF1 (0x00010000 | (XD_RW_PULSE_END << 8) | XD_RW_PULSE_START)   // We_n
#define XD_ADDR_WR_OUTOUT_CF2 (0x00020000 | XD_CONFIG_INPUT) // Rb_n input
#define XD_ADDR_WR_OUTOUT_CF3 (0x00030000 | XD_CONFIG_OUTPUT) // chip select
#define XD_ADDR_WR_OUTOUT_CF4 (0x00040000 | (XD_ALE_END << 8) | XD_ALE_START) // Ale 
#define XD_ADDR_WR_OUTOUT_CF5 (0x00050000 | XD_CONFIG_OUTPUT) // Cle

#define XD_DATA_READ_OUTOUT_CF0 (0x00000000 | (XD_RW_PULSE_END << 8) | XD_RW_PULSE_START) // Re_n
#define XD_DATA_READ_OUTOUT_CF1 (0x00010000 | XD_CONFIG_OUTPUT)   // We_n
#define XD_DATA_READ_OUTOUT_CF2 (0x00020000 | XD_CONFIG_INPUT) // Rb_n input
#define XD_DATA_READ_OUTOUT_CF3 (0x00030000 | XD_CONFIG_OUTPUT) // chip select
#define XD_DATA_READ_OUTOUT_CF4 (0x00040000 | XD_CONFIG_OUTPUT) // Ale 
#define XD_DATA_READ_OUTOUT_CF5 (0x00050000 | XD_CONFIG_OUTPUT) // Cle

#define XD_DATA_WR_OUTOUT_CF0 (0x00000000 | XD_CONFIG_OUTPUT) // Re_n
#define XD_DATA_WR_OUTOUT_CF1 (0x00010000 | (XD_RW_PULSE_END << 8) | XD_RW_PULSE_START)   // We_n
#define XD_DATA_WR_OUTOUT_CF2 (0x00020000 | XD_CONFIG_INPUT) // Rb_n input
#define XD_DATA_WR_OUTOUT_CF3 (0x00030000 | XD_CONFIG_OUTPUT) // chip select
#define XD_DATA_WR_OUTOUT_CF4 (0x00040000 | XD_CONFIG_OUTPUT) // Ale 
#define XD_DATA_WR_OUTOUT_CF5 (0x00050000 | XD_CONFIG_OUTPUT) // Cle

#define XD_BUS_CONFIG ( \
			     (0 << xd_bus_width_1_bit) \
                |(0 << xd_bus_start_pin_1_bit ) \
                |(0xff << xd_bus_sel_chang_point_1_bit ) \
			    |(8 << xd_bus_width_0_bit) \
                |(XD_DATA_PIN_BEGIN << xd_bus_start_pin_0_bit ) \
                |(0xff << xd_bus_sel_chang_point_0_bit ) \
			  )

#endif
