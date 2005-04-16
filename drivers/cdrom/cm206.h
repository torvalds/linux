/* cm206.h Header file for cm206.c.
   Copyright (c) 1995 David van Leeuwen 
*/

#ifndef LINUX_CM206_H
#define LINUX_CM206_H

#include <linux/ioctl.h>

/* First, the cm260 stuff */
/* The ports and irq used. Although CM206_BASE and CM206_IRQ are defined
   below, the values are not used unless autoprobing is turned off and 
   no LILO boot options or module command line options are given. Change
   these values to your own as last resort if autoprobing and options
   don't work. */

#define CM206_BASE 0x340
#define CM206_IRQ 11

#define r_data_status (cm206_base)
#define r_uart_receive (cm206_base+0x2)
#define r_fifo_output_buffer (cm206_base+0x4)
#define r_line_status (cm206_base+0x6)
#define r_data_control (cm206_base+0x8)
#define r_uart_transmit (cm206_base+0xa)
#define r_test_clock (cm206_base+0xc)
#define r_test_control (cm206_base+0xe)

/* the data_status flags */
#define ds_ram_size 0x4000
#define ds_toc_ready 0x2000
#define ds_fifo_empty 0x1000
#define ds_sync_error 0x800
#define ds_crc_error 0x400
#define ds_data_error 0x200
#define ds_fifo_overflow 0x100
#define ds_data_ready 0x80

/* the line_status flags */
#define ls_attention 0x10
#define ls_parity_error 0x8
#define ls_overrun 0x4
#define ls_receive_buffer_full 0x2
#define ls_transmitter_buffer_empty 0x1

/* the data control register flags */
#define dc_read_q_channel 0x4000
#define dc_mask_sync_error 0x2000
#define dc_toc_enable 0x1000
#define dc_no_stop_on_error 0x800
#define dc_break 0x400
#define dc_initialize 0x200
#define dc_mask_transmit_ready 0x100
#define dc_flag_enable 0x80

/* Define the default data control register flags here */
#define dc_normal (dc_mask_sync_error | dc_no_stop_on_error | \
		   dc_mask_transmit_ready)

/* now some constants related to the cm206 */
/* another drive status byte, echoed by the cm206 on most commands */

#define dsb_error_condition 0x1
#define dsb_play_in_progress 0x4
#define dsb_possible_media_change 0x8
#define dsb_disc_present 0x10
#define dsb_drive_not_ready 0x20
#define dsb_tray_locked 0x40
#define dsb_tray_not_closed 0x80

#define dsb_not_useful (dsb_drive_not_ready | dsb_tray_not_closed)

/* the cm206 command set */

#define c_close_tray 0
#define c_lock_tray 0x01
#define c_unlock_tray 0x04
#define c_open_tray 0x05
#define c_seek 0x10
#define c_read_data 0x20
#define c_force_1x 0x21
#define c_force_2x 0x22
#define c_auto_mode 0x23
#define c_play 0x30
#define c_set_audio_mode 0x31
#define c_read_current_q 0x41
#define c_stream_q 0x42
#define c_drive_status 0x50
#define c_disc_status 0x51
#define c_audio_status 0x52
#define c_drive_configuration 0x53
#define c_read_upc 0x60
#define c_stop 0x70
#define c_calc_checksum 0xe5

#define c_gimme 0xf8

/* finally, the (error) condition that the drive can be in      *
 * OK, this is not always an error, but let's prefix it with e_ */

#define e_none 0
#define e_illegal_command 0x01
#define e_sync 0x02
#define e_seek 0x03
#define e_parity 0x04
#define e_focus 0x05
#define e_header_sync 0x06
#define e_code_incompatibility 0x07
#define e_reset_done 0x08
#define e_bad_parameter 0x09
#define e_radial 0x0a
#define e_sub_code 0x0b
#define e_no_data_track 0x0c
#define e_scan 0x0d
#define e_tray_open 0x0f
#define e_no_disc 0x10
#define e_tray stalled 0x11

/* drive configuration masks */

#define dcf_revision_code 0x7
#define dcf_transfer_rate 0x60
#define dcf_motorized_tray 0x80

/* disc status byte */

#define cds_multi_session 0x2
#define cds_all_audio 0x8
#define cds_xa_mode 0xf0

/* finally some ioctls for the driver */

#define CM206CTL_GET_STAT _IO( 0x20, 0 )
#define CM206CTL_GET_LAST_STAT _IO( 0x20, 1 )

#ifdef STATISTICS

/* This is an ugly way to guarantee that the names of the statistics
 * are the same in the code and in the diagnostics program.  */

#ifdef __KERNEL__
#define x(a) st_ ## a
#define y enum
#else
#define x(a) #a
#define y char * stats_name[] = 
#endif

y {x(interrupt), x(data_ready), x(fifo_overflow), x(data_error),
     x(crc_error), x(sync_error), x(lost_intr), x(echo),
     x(write_timeout), x(receive_timeout), x(read_timeout),
     x(dsb_timeout), x(stop_0xff), x(back_read_timeout),
     x(sector_transferred), x(read_restarted), x(read_background),
     x(bh), x(open), x(ioctl_multisession), x(attention)
#ifdef __KERNEL__
     , x(last_entry)
#endif
 };

#ifdef __KERNEL__
#define NR_STATS st_last_entry
#else
#define NR_STATS (sizeof(stats_name)/sizeof(char*))
#endif

#undef y
#undef x

#endif /* STATISTICS */

#endif /* LINUX_CM206_H */
