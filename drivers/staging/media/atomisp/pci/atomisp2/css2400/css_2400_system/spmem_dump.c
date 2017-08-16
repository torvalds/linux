/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _sp_map_h_
#define _sp_map_h_


#ifndef _hrt_dummy_use_blob_sp
#define _hrt_dummy_use_blob_sp()
#endif

#define _hrt_cell_load_program_sp(proc) _hrt_cell_load_program_embedded(proc, sp)

#ifndef ISP2401
/* function input_system_acquisition_stop: ADE */
#else
/* function input_system_acquisition_stop: AD8 */
#endif

#ifndef ISP2401
/* function longjmp: 684E */
#else
/* function longjmp: 69C1 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_HIVE_IF_SRST_MASK
#define HIVE_MEM_HIVE_IF_SRST_MASK scalar_processor_2400_dmem
#define HIVE_ADDR_HIVE_IF_SRST_MASK 0x1C8
#define HIVE_SIZE_HIVE_IF_SRST_MASK 16
#else
#endif
#endif
#define HIVE_MEM_sp_HIVE_IF_SRST_MASK scalar_processor_2400_dmem
#define HIVE_ADDR_sp_HIVE_IF_SRST_MASK 0x1C8
#define HIVE_SIZE_sp_HIVE_IF_SRST_MASK 16

#ifndef ISP2401
/* function tmpmem_init_dmem: 6580 */
#else
/* function tmpmem_init_dmem: 66BB */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_token_map_receive_ack: 5EC4 */
#else
/* function ia_css_isys_sp_token_map_receive_ack: 5FFF */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_set_addr_B: 332C */
#else
/* function ia_css_dmaproxy_sp_set_addr_B: 3520 */

/* function ia_css_pipe_data_init_tagger_resources: A4F */
#endif

/* function debug_buffer_set_ddr_addr: DD */

#ifndef ISP2401
/* function receiver_port_reg_load: AC2 */
#else
/* function receiver_port_reg_load: ABC */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_mipi
#define HIVE_MEM_vbuf_mipi scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_vbuf_mipi 0x631C
#else
#define HIVE_ADDR_vbuf_mipi 0x6378
#endif
#define HIVE_SIZE_vbuf_mipi 12
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_mipi scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_vbuf_mipi 0x631C
#else
#define HIVE_ADDR_sp_vbuf_mipi 0x6378
#endif
#define HIVE_SIZE_sp_vbuf_mipi 12

#ifndef ISP2401
/* function ia_css_event_sp_decode: 351D */
#else
/* function ia_css_event_sp_decode: 3711 */
#endif

#ifndef ISP2401
/* function ia_css_queue_get_size: 48A5 */
#else
/* function ia_css_queue_get_size: 4B2D */
#endif

#ifndef ISP2401
/* function ia_css_queue_load: 4EE6 */
#else
/* function ia_css_queue_load: 5144 */
#endif

#ifndef ISP2401
/* function setjmp: 6857 */
#else
/* function setjmp: 69CA */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_sp2host_isys_event_queue
#define HIVE_MEM_sem_for_sp2host_isys_event_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_sp2host_isys_event_queue 0x4684
#else
#define HIVE_ADDR_sem_for_sp2host_isys_event_queue 0x46CC
#endif
#define HIVE_SIZE_sem_for_sp2host_isys_event_queue 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_sp2host_isys_event_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_sp2host_isys_event_queue 0x4684
#else
#define HIVE_ADDR_sp_sem_for_sp2host_isys_event_queue 0x46CC
#endif
#define HIVE_SIZE_sp_sem_for_sp2host_isys_event_queue 20

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_wait_for_ack: 6E07 */
#else
/* function ia_css_dmaproxy_sp_wait_for_ack: 6F4B */
#endif

#ifndef ISP2401
/* function ia_css_sp_rawcopy_func: 510B */
#else
/* function ia_css_sp_rawcopy_func: 5369 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_pop_marked: 29F7 */
#else
/* function ia_css_tagger_buf_sp_pop_marked: 2B99 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_stage
#define HIVE_MEM_isp_stage scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isp_stage 0x5C00
#else
#define HIVE_ADDR_isp_stage 0x5C60
#endif
#define HIVE_SIZE_isp_stage 832
#else
#endif
#endif
#define HIVE_MEM_sp_isp_stage scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_stage 0x5C00
#else
#define HIVE_ADDR_sp_isp_stage 0x5C60
#endif
#define HIVE_SIZE_sp_isp_stage 832

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_raw
#define HIVE_MEM_vbuf_raw scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_vbuf_raw 0x2F4
#else
#define HIVE_ADDR_vbuf_raw 0x30C
#endif
#define HIVE_SIZE_vbuf_raw 4
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_raw scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_vbuf_raw 0x2F4
#else
#define HIVE_ADDR_sp_vbuf_raw 0x30C
#endif
#define HIVE_SIZE_sp_vbuf_raw 4

#ifndef ISP2401
/* function ia_css_sp_bin_copy_func: 5032 */
#else
/* function ia_css_sp_bin_copy_func: 5290 */
#endif

#ifndef ISP2401
/* function ia_css_queue_item_store: 4C34 */
#else
/* function ia_css_queue_item_store: 4E92 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_metadata_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_metadata_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_metadata_bufs 0x4AA0
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_metadata_bufs 0x4AFC
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_metadata_bufs 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_metadata_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_metadata_bufs 0x4AA0
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_metadata_bufs 0x4AFC
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_metadata_bufs 20

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_buffer_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_buffer_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_buffer_bufs 0x4AB4
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_buffer_bufs 0x4B10
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_buffer_bufs 160
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_buffer_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_buffer_bufs 0x4AB4
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_buffer_bufs 0x4B10
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_buffer_bufs 160

/* function sp_start_isp: 45D */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_binary_group
#define HIVE_MEM_sp_binary_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_binary_group 0x5FF0
#else
#define HIVE_ADDR_sp_binary_group 0x6050
#endif
#define HIVE_SIZE_sp_binary_group 32
#else
#endif
#endif
#define HIVE_MEM_sp_sp_binary_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_binary_group 0x5FF0
#else
#define HIVE_ADDR_sp_sp_binary_group 0x6050
#endif
#define HIVE_SIZE_sp_sp_binary_group 32

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_sw_state
#define HIVE_MEM_sp_sw_state scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sw_state 0x62AC
#else
#define HIVE_ADDR_sp_sw_state 0x6308
#endif
#define HIVE_SIZE_sp_sw_state 4
#else
#endif
#endif
#define HIVE_MEM_sp_sp_sw_state scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_sw_state 0x62AC
#else
#define HIVE_ADDR_sp_sp_sw_state 0x6308
#endif
#define HIVE_SIZE_sp_sp_sw_state 4

#ifndef ISP2401
/* function ia_css_thread_sp_main: D5B */
#else
/* function ia_css_thread_sp_main: D50 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_init_internal_buffers: 3723 */
#else
/* function ia_css_ispctrl_sp_init_internal_buffers: 3952 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp2host_psys_event_queue_handle
#define HIVE_MEM_sp2host_psys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp2host_psys_event_queue_handle 0x4B54
#else
#define HIVE_ADDR_sp2host_psys_event_queue_handle 0x4BB0
#endif
#define HIVE_SIZE_sp2host_psys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_sp2host_psys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp2host_psys_event_queue_handle 0x4B54
#else
#define HIVE_ADDR_sp_sp2host_psys_event_queue_handle 0x4BB0
#endif
#define HIVE_SIZE_sp_sp2host_psys_event_queue_handle 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_sp2host_psys_event_queue
#define HIVE_MEM_sem_for_sp2host_psys_event_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_sp2host_psys_event_queue 0x4698
#else
#define HIVE_ADDR_sem_for_sp2host_psys_event_queue 0x46E0
#endif
#define HIVE_SIZE_sem_for_sp2host_psys_event_queue 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_sp2host_psys_event_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_sp2host_psys_event_queue 0x4698
#else
#define HIVE_ADDR_sp_sem_for_sp2host_psys_event_queue 0x46E0
#endif
#define HIVE_SIZE_sp_sem_for_sp2host_psys_event_queue 20

#ifndef ISP2401
/* function ia_css_tagger_sp_propagate_frame: 2410 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_stop_copy_preview
#define HIVE_MEM_sp_stop_copy_preview scalar_processor_2400_dmem
#define HIVE_ADDR_sp_stop_copy_preview 0x6290
#define HIVE_SIZE_sp_stop_copy_preview 4
#else
#endif
#endif
#define HIVE_MEM_sp_sp_stop_copy_preview scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_stop_copy_preview 0x6290
#define HIVE_SIZE_sp_sp_stop_copy_preview 4
#else
/* function ia_css_tagger_sp_propagate_frame: 2460 */
#endif

#ifndef ISP2401
/* function input_system_reg_load: B17 */
#else
/* function input_system_reg_load: B11 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_handles
#define HIVE_MEM_vbuf_handles scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_vbuf_handles 0x6328
#else
#define HIVE_ADDR_vbuf_handles 0x6384
#endif
#define HIVE_SIZE_vbuf_handles 960
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_handles scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_vbuf_handles 0x6328
#else
#define HIVE_ADDR_sp_vbuf_handles 0x6384
#endif
#define HIVE_SIZE_sp_vbuf_handles 960

#ifndef ISP2401
/* function ia_css_queue_store: 4D9A */

/* function ia_css_sp_flash_register: 2C2C */
#else
/* function ia_css_queue_store: 4FF8 */
#endif

#ifndef ISP2401
/* function ia_css_sp_rawcopy_dummy_function: 5652 */
#else
/* function ia_css_sp_flash_register: 2DCE */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_backend_create: 5B37 */
#else
/* function ia_css_isys_sp_backend_create: 5C72 */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_init: 1833 */
#else
/* function ia_css_pipeline_sp_init: 186D */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_configure: 2300 */
#else
/* function ia_css_tagger_sp_configure: 2350 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_end_binary: 3566 */
#else
/* function ia_css_ispctrl_sp_end_binary: 375A */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs
#define HIVE_MEM_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 0x4B60
#else
#define HIVE_ADDR_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 0x4BBC
#endif
#define HIVE_SIZE_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 0x4B60
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 0x4BBC
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 20

#ifndef ISP2401
/* function receiver_port_reg_store: AC9 */
#else
/* function receiver_port_reg_store: AC3 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_event_is_pending_mask
#define HIVE_MEM_event_is_pending_mask scalar_processor_2400_dmem
#define HIVE_ADDR_event_is_pending_mask 0x5C
#define HIVE_SIZE_event_is_pending_mask 44
#else
#endif
#endif
#define HIVE_MEM_sp_event_is_pending_mask scalar_processor_2400_dmem
#define HIVE_ADDR_sp_event_is_pending_mask 0x5C
#define HIVE_SIZE_sp_event_is_pending_mask 44

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cb_elems_frame
#define HIVE_MEM_sp_all_cb_elems_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_all_cb_elems_frame 0x46AC
#else
#define HIVE_ADDR_sp_all_cb_elems_frame 0x46F4
#endif
#define HIVE_SIZE_sp_all_cb_elems_frame 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cb_elems_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cb_elems_frame 0x46AC
#else
#define HIVE_ADDR_sp_sp_all_cb_elems_frame 0x46F4
#endif
#define HIVE_SIZE_sp_sp_all_cb_elems_frame 16

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp2host_isys_event_queue_handle
#define HIVE_MEM_sp2host_isys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp2host_isys_event_queue_handle 0x4B74
#else
#define HIVE_ADDR_sp2host_isys_event_queue_handle 0x4BD0
#endif
#define HIVE_SIZE_sp2host_isys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_sp2host_isys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp2host_isys_event_queue_handle 0x4B74
#else
#define HIVE_ADDR_sp_sp2host_isys_event_queue_handle 0x4BD0
#endif
#define HIVE_SIZE_sp_sp2host_isys_event_queue_handle 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host_sp_com
#define HIVE_MEM_host_sp_com scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host_sp_com 0x4114
#else
#define HIVE_ADDR_host_sp_com 0x4134
#endif
#define HIVE_SIZE_host_sp_com 220
#else
#endif
#endif
#define HIVE_MEM_sp_host_sp_com scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host_sp_com 0x4114
#else
#define HIVE_ADDR_sp_host_sp_com 0x4134
#endif
#define HIVE_SIZE_sp_host_sp_com 220

#ifndef ISP2401
/* function ia_css_queue_get_free_space: 49F9 */
#else
/* function ia_css_queue_get_free_space: 4C57 */
#endif

#ifndef ISP2401
/* function exec_image_pipe: 6C4 */
#else
/* function exec_image_pipe: 658 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_init_dmem_data
#define HIVE_MEM_sp_init_dmem_data scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_init_dmem_data 0x62B0
#else
#define HIVE_ADDR_sp_init_dmem_data 0x630C
#endif
#define HIVE_SIZE_sp_init_dmem_data 24
#else
#endif
#endif
#define HIVE_MEM_sp_sp_init_dmem_data scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_init_dmem_data 0x62B0
#else
#define HIVE_ADDR_sp_sp_init_dmem_data 0x630C
#endif
#define HIVE_SIZE_sp_sp_init_dmem_data 24

#ifndef ISP2401
/* function ia_css_sp_metadata_start: 5914 */
#else
/* function ia_css_sp_metadata_start: 5A4F */
#endif

#ifndef ISP2401
/* function ia_css_bufq_sp_init_buffer_queues: 2C9B */
#else
/* function ia_css_bufq_sp_init_buffer_queues: 2E3D */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_stop: 1816 */
#else
/* function ia_css_pipeline_sp_stop: 1850 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_connect_pipes: 27EA */
#else
/* function ia_css_tagger_sp_connect_pipes: 283A */
#endif

#ifndef ISP2401
/* function sp_isys_copy_wait: 70D */
#else
/* function sp_isys_copy_wait: 6A1 */
#endif

/* function is_isp_debug_buffer_full: 337 */

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_configure_channel_from_info: 32AF */
#else
/* function ia_css_dmaproxy_sp_configure_channel_from_info: 3490 */
#endif

#ifndef ISP2401
/* function encode_and_post_timer_event: A30 */
#else
/* function encode_and_post_timer_event: 9C4 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_per_frame_data
#define HIVE_MEM_sp_per_frame_data scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_per_frame_data 0x41F0
#else
#define HIVE_ADDR_sp_per_frame_data 0x4210
#endif
#define HIVE_SIZE_sp_per_frame_data 4
#else
#endif
#endif
#define HIVE_MEM_sp_sp_per_frame_data scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_per_frame_data 0x41F0
#else
#define HIVE_ADDR_sp_sp_per_frame_data 0x4210
#endif
#define HIVE_SIZE_sp_sp_per_frame_data 4

#ifndef ISP2401
/* function ia_css_rmgr_sp_vbuf_dequeue: 62D4 */
#else
/* function ia_css_rmgr_sp_vbuf_dequeue: 640F */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_psys_event_queue_handle
#define HIVE_MEM_host2sp_psys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host2sp_psys_event_queue_handle 0x4B80
#else
#define HIVE_ADDR_host2sp_psys_event_queue_handle 0x4BDC
#endif
#define HIVE_SIZE_host2sp_psys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_psys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host2sp_psys_event_queue_handle 0x4B80
#else
#define HIVE_ADDR_sp_host2sp_psys_event_queue_handle 0x4BDC
#endif
#define HIVE_SIZE_sp_host2sp_psys_event_queue_handle 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_xmem_bin_addr
#define HIVE_MEM_xmem_bin_addr scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_xmem_bin_addr 0x41F4
#else
#define HIVE_ADDR_xmem_bin_addr 0x4214
#endif
#define HIVE_SIZE_xmem_bin_addr 4
#else
#endif
#endif
#define HIVE_MEM_sp_xmem_bin_addr scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_xmem_bin_addr 0x41F4
#else
#define HIVE_ADDR_sp_xmem_bin_addr 0x4214
#endif
#define HIVE_SIZE_sp_xmem_bin_addr 4

#ifndef ISP2401
/* function tmr_clock_init: 65A0 */
#else
/* function tmr_clock_init: 66DB */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_run: 1403 */
#else
/* function ia_css_pipeline_sp_run: 1424 */
#endif

#ifndef ISP2401
/* function memcpy: 68F7 */
#else
/* function memcpy: 6A6A */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_GP_DEVICE_BASE
#define HIVE_MEM_GP_DEVICE_BASE scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_GP_DEVICE_BASE 0x2FC
#else
#define HIVE_ADDR_GP_DEVICE_BASE 0x314
#endif
#define HIVE_SIZE_GP_DEVICE_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_GP_DEVICE_BASE scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_GP_DEVICE_BASE 0x2FC
#else
#define HIVE_ADDR_sp_GP_DEVICE_BASE 0x314
#endif
#define HIVE_SIZE_sp_GP_DEVICE_BASE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_thread_sp_ready_queue
#define HIVE_MEM_ia_css_thread_sp_ready_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_thread_sp_ready_queue 0x1E0
#else
#define HIVE_ADDR_ia_css_thread_sp_ready_queue 0x1E4
#endif
#define HIVE_SIZE_ia_css_thread_sp_ready_queue 12
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_thread_sp_ready_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_thread_sp_ready_queue 0x1E0
#else
#define HIVE_ADDR_sp_ia_css_thread_sp_ready_queue 0x1E4
#endif
#define HIVE_SIZE_sp_ia_css_thread_sp_ready_queue 12

#ifndef ISP2401
/* function input_system_reg_store: B1E */
#else
/* function input_system_reg_store: B18 */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_frontend_start: 5D4D */
#else
/* function ia_css_isys_sp_frontend_start: 5E88 */
#endif

#ifndef ISP2401
/* function ia_css_uds_sp_scale_params: 6600 */
#else
/* function ia_css_uds_sp_scale_params: 6773 */
#endif

#ifndef ISP2401
/* function ia_css_circbuf_increase_size: E40 */
#else
/* function ia_css_circbuf_increase_size: E35 */
#endif

#ifndef ISP2401
/* function __divu: 6875 */
#else
/* function __divu: 69E8 */
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_get_state: C83 */
#else
/* function ia_css_thread_sp_get_state: C78 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_cont_capt_stop
#define HIVE_MEM_sem_for_cont_capt_stop scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_cont_capt_stop 0x46BC
#else
#define HIVE_ADDR_sem_for_cont_capt_stop 0x4704
#endif
#define HIVE_SIZE_sem_for_cont_capt_stop 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_cont_capt_stop scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_cont_capt_stop 0x46BC
#else
#define HIVE_ADDR_sp_sem_for_cont_capt_stop 0x4704
#endif
#define HIVE_SIZE_sp_sem_for_cont_capt_stop 20

#ifndef ISP2401
/* function thread_fiber_sp_main: E39 */
#else
/* function thread_fiber_sp_main: E2E */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_isp_pipe_thread
#define HIVE_MEM_sp_isp_pipe_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_pipe_thread 0x4800
#define HIVE_SIZE_sp_isp_pipe_thread 340
#else
#define HIVE_ADDR_sp_isp_pipe_thread 0x4848
#define HIVE_SIZE_sp_isp_pipe_thread 360
#endif
#else
#endif
#endif
#define HIVE_MEM_sp_sp_isp_pipe_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_isp_pipe_thread 0x4800
#define HIVE_SIZE_sp_sp_isp_pipe_thread 340
#else
#define HIVE_ADDR_sp_sp_isp_pipe_thread 0x4848
#define HIVE_SIZE_sp_sp_isp_pipe_thread 360
#endif

#ifndef ISP2401
/* function ia_css_parambuf_sp_handle_parameter_sets: 128A */
#else
/* function ia_css_parambuf_sp_handle_parameter_sets: 127F */
#endif

#ifndef ISP2401
/* function ia_css_spctrl_sp_set_state: 5943 */
#else
/* function ia_css_spctrl_sp_set_state: 5A7E */
#endif

#ifndef ISP2401
/* function ia_css_thread_sem_sp_signal: 6AF7 */
#else
/* function ia_css_thread_sem_sp_signal: 6C6C */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_IRQ_BASE
#define HIVE_MEM_IRQ_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_IRQ_BASE 0x2C
#define HIVE_SIZE_IRQ_BASE 16
#else
#endif
#endif
#define HIVE_MEM_sp_IRQ_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_IRQ_BASE 0x2C
#define HIVE_SIZE_sp_IRQ_BASE 16

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_TIMED_CTRL_BASE
#define HIVE_MEM_TIMED_CTRL_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_TIMED_CTRL_BASE 0x40
#define HIVE_SIZE_TIMED_CTRL_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_TIMED_CTRL_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_TIMED_CTRL_BASE 0x40
#define HIVE_SIZE_sp_TIMED_CTRL_BASE 4

#ifndef ISP2401
/* function ia_css_isys_sp_isr: 6FDC */

/* function ia_css_isys_sp_generate_exp_id: 60E5 */
#else
/* function ia_css_isys_sp_isr: 7139 */
#endif

#ifndef ISP2401
/* function ia_css_rmgr_sp_init: 61CF */
#else
/* function ia_css_isys_sp_generate_exp_id: 6220 */
#endif

#ifndef ISP2401
/* function ia_css_thread_sem_sp_init: 6BC8 */
#else
/* function ia_css_rmgr_sp_init: 630A */
#endif

#ifndef ISP2401
#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_is_isp_requested
#define HIVE_MEM_is_isp_requested scalar_processor_2400_dmem
#define HIVE_ADDR_is_isp_requested 0x308
#define HIVE_SIZE_is_isp_requested 4
#else
#endif
#endif
#define HIVE_MEM_sp_is_isp_requested scalar_processor_2400_dmem
#define HIVE_ADDR_sp_is_isp_requested 0x308
#define HIVE_SIZE_sp_is_isp_requested 4
#else
/* function ia_css_thread_sem_sp_init: 6D3B */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_reading_cb_frame
#define HIVE_MEM_sem_for_reading_cb_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_reading_cb_frame 0x46D0
#else
#define HIVE_ADDR_sem_for_reading_cb_frame 0x4718
#endif
#define HIVE_SIZE_sem_for_reading_cb_frame 40
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_reading_cb_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_reading_cb_frame 0x46D0
#else
#define HIVE_ADDR_sp_sem_for_reading_cb_frame 0x4718
#endif
#define HIVE_SIZE_sp_sem_for_reading_cb_frame 40

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_execute: 3217 */
#else
#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_is_isp_requested
#define HIVE_MEM_is_isp_requested scalar_processor_2400_dmem
#define HIVE_ADDR_is_isp_requested 0x320
#define HIVE_SIZE_is_isp_requested 4
#else
#endif
#endif
#define HIVE_MEM_sp_is_isp_requested scalar_processor_2400_dmem
#define HIVE_ADDR_sp_is_isp_requested 0x320
#define HIVE_SIZE_sp_is_isp_requested 4

/* function ia_css_dmaproxy_sp_execute: 33F6 */
#endif

#ifndef ISP2401
/* function ia_css_queue_is_empty: 48E0 */
#else
/* function ia_css_queue_is_empty: 7098 */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_has_stopped: 180C */
#else
/* function ia_css_pipeline_sp_has_stopped: 1846 */
#endif

#ifndef ISP2401
/* function ia_css_circbuf_extract: F44 */
#else
/* function ia_css_circbuf_extract: F39 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_is_locked_from_start: 2B0D */
#else
/* function ia_css_tagger_buf_sp_is_locked_from_start: 2CAF */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_current_sp_thread
#define HIVE_MEM_current_sp_thread scalar_processor_2400_dmem
#define HIVE_ADDR_current_sp_thread 0x1DC
#define HIVE_SIZE_current_sp_thread 4
#else
#endif
#endif
#define HIVE_MEM_sp_current_sp_thread scalar_processor_2400_dmem
#define HIVE_ADDR_sp_current_sp_thread 0x1DC
#define HIVE_SIZE_sp_current_sp_thread 4

#ifndef ISP2401
/* function ia_css_spctrl_sp_get_spid: 594A */
#else
/* function ia_css_spctrl_sp_get_spid: 5A85 */
#endif

#ifndef ISP2401
/* function ia_css_bufq_sp_reset_buffers: 2D22 */
#else
/* function ia_css_bufq_sp_reset_buffers: 2EC4 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_read_byte_addr: 6E35 */
#else
/* function ia_css_dmaproxy_sp_read_byte_addr: 6F79 */
#endif

#ifndef ISP2401
/* function ia_css_rmgr_sp_uninit: 61C8 */
#else
/* function ia_css_rmgr_sp_uninit: 6303 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_threads_stack
#define HIVE_MEM_sp_threads_stack scalar_processor_2400_dmem
#define HIVE_ADDR_sp_threads_stack 0x164
#define HIVE_SIZE_sp_threads_stack 28
#else
#endif
#endif
#define HIVE_MEM_sp_sp_threads_stack scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_threads_stack 0x164
#define HIVE_SIZE_sp_sp_threads_stack 28

#ifndef ISP2401
/* function ia_css_circbuf_peek: F26 */
#else
/* function ia_css_circbuf_peek: F1B */
#endif

#ifndef ISP2401
/* function ia_css_parambuf_sp_wait_for_in_param: 1053 */
#else
/* function ia_css_parambuf_sp_wait_for_in_param: 1048 */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_token_map_get_exp_id: 5FAD */
#else
/* function ia_css_isys_sp_token_map_get_exp_id: 60E8 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cb_elems_param
#define HIVE_MEM_sp_all_cb_elems_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_all_cb_elems_param 0x46F8
#else
#define HIVE_ADDR_sp_all_cb_elems_param 0x4740
#endif
#define HIVE_SIZE_sp_all_cb_elems_param 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cb_elems_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cb_elems_param 0x46F8
#else
#define HIVE_ADDR_sp_sp_all_cb_elems_param 0x4740
#endif
#define HIVE_SIZE_sp_sp_all_cb_elems_param 16

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_pipeline_sp_curr_binary_id
#define HIVE_MEM_pipeline_sp_curr_binary_id scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_pipeline_sp_curr_binary_id 0x1EC
#else
#define HIVE_ADDR_pipeline_sp_curr_binary_id 0x1F0
#endif
#define HIVE_SIZE_pipeline_sp_curr_binary_id 4
#else
#endif
#endif
#define HIVE_MEM_sp_pipeline_sp_curr_binary_id scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_pipeline_sp_curr_binary_id 0x1EC
#else
#define HIVE_ADDR_sp_pipeline_sp_curr_binary_id 0x1F0
#endif
#define HIVE_SIZE_sp_pipeline_sp_curr_binary_id 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_frame_desc
#define HIVE_MEM_sp_all_cbs_frame_desc scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_all_cbs_frame_desc 0x4708
#else
#define HIVE_ADDR_sp_all_cbs_frame_desc 0x4750
#endif
#define HIVE_SIZE_sp_all_cbs_frame_desc 8
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_frame_desc scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cbs_frame_desc 0x4708
#else
#define HIVE_ADDR_sp_sp_all_cbs_frame_desc 0x4750
#endif
#define HIVE_SIZE_sp_sp_all_cbs_frame_desc 8

#ifndef ISP2401
/* function sp_isys_copy_func_v2: 706 */
#else
/* function sp_isys_copy_func_v2: 69A */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_reading_cb_param
#define HIVE_MEM_sem_for_reading_cb_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_reading_cb_param 0x4710
#else
#define HIVE_ADDR_sem_for_reading_cb_param 0x4758
#endif
#define HIVE_SIZE_sem_for_reading_cb_param 40
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_reading_cb_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_reading_cb_param 0x4710
#else
#define HIVE_ADDR_sp_sem_for_reading_cb_param 0x4758
#endif
#define HIVE_SIZE_sp_sem_for_reading_cb_param 40

#ifndef ISP2401
/* function ia_css_queue_get_used_space: 49AD */
#else
/* function ia_css_queue_get_used_space: 4C0B */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_cont_capt_start
#define HIVE_MEM_sem_for_cont_capt_start scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_cont_capt_start 0x4738
#else
#define HIVE_ADDR_sem_for_cont_capt_start 0x4780
#endif
#define HIVE_SIZE_sem_for_cont_capt_start 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_cont_capt_start scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_cont_capt_start 0x4738
#else
#define HIVE_ADDR_sp_sem_for_cont_capt_start 0x4780
#endif
#define HIVE_SIZE_sp_sem_for_cont_capt_start 20

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_tmp_heap
#define HIVE_MEM_tmp_heap scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_tmp_heap 0x6010
#else
#define HIVE_ADDR_tmp_heap 0x6070
#endif
#define HIVE_SIZE_tmp_heap 640
#else
#endif
#endif
#define HIVE_MEM_sp_tmp_heap scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_tmp_heap 0x6010
#else
#define HIVE_ADDR_sp_tmp_heap 0x6070
#endif
#define HIVE_SIZE_sp_tmp_heap 640

#ifndef ISP2401
/* function ia_css_rmgr_sp_get_num_vbuf: 64D8 */
#else
/* function ia_css_rmgr_sp_get_num_vbuf: 6613 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_output_compute_dma_info: 3F49 */
#else
/* function ia_css_ispctrl_sp_output_compute_dma_info: 418C */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_lock_exp_id: 20CD */
#else
/* function ia_css_tagger_sp_lock_exp_id: 211D */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_s3a_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_s3a_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_s3a_bufs 0x4B8C
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_s3a_bufs 0x4BE8
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_s3a_bufs 60
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_s3a_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_s3a_bufs 0x4B8C
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_s3a_bufs 0x4BE8
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_s3a_bufs 60

#ifndef ISP2401
/* function ia_css_queue_is_full: 4A44 */
#else
/* function ia_css_queue_is_full: 4CA2 */
#endif

/* function debug_buffer_init_isp: E4 */

#ifndef ISP2401
/* function ia_css_isys_sp_frontend_uninit: 5D07 */
#else
/* function ia_css_isys_sp_frontend_uninit: 5E42 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_exp_id_is_locked: 2003 */
#else
/* function ia_css_tagger_sp_exp_id_is_locked: 2053 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_rmgr_sp_mipi_frame_sem
#define HIVE_MEM_ia_css_rmgr_sp_mipi_frame_sem scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_rmgr_sp_mipi_frame_sem 0x66E8
#else
#define HIVE_ADDR_ia_css_rmgr_sp_mipi_frame_sem 0x6744
#endif
#define HIVE_SIZE_ia_css_rmgr_sp_mipi_frame_sem 60
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_rmgr_sp_mipi_frame_sem scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_rmgr_sp_mipi_frame_sem 0x66E8
#else
#define HIVE_ADDR_sp_ia_css_rmgr_sp_mipi_frame_sem 0x6744
#endif
#define HIVE_SIZE_sp_ia_css_rmgr_sp_mipi_frame_sem 60

#ifndef ISP2401
/* function ia_css_rmgr_sp_refcount_dump: 62AF */
#else
/* function ia_css_rmgr_sp_refcount_dump: 63EA */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_isp_parameters_id
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_isp_parameters_id scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_isp_parameters_id 0x4BC8
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_isp_parameters_id 0x4C24
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_isp_parameters_id 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_isp_parameters_id scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_isp_parameters_id 0x4BC8
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_isp_parameters_id 0x4C24
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_isp_parameters_id 20

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_pipe_threads
#define HIVE_MEM_sp_pipe_threads scalar_processor_2400_dmem
#define HIVE_ADDR_sp_pipe_threads 0x150
#define HIVE_SIZE_sp_pipe_threads 20
#else
#endif
#endif
#define HIVE_MEM_sp_sp_pipe_threads scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_pipe_threads 0x150
#define HIVE_SIZE_sp_sp_pipe_threads 20

#ifndef ISP2401
/* function sp_event_proxy_func: 71B */
#else
/* function sp_event_proxy_func: 6AF */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_isys_event_queue_handle
#define HIVE_MEM_host2sp_isys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host2sp_isys_event_queue_handle 0x4BDC
#else
#define HIVE_ADDR_host2sp_isys_event_queue_handle 0x4C38
#endif
#define HIVE_SIZE_host2sp_isys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_isys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host2sp_isys_event_queue_handle 0x4BDC
#else
#define HIVE_ADDR_sp_host2sp_isys_event_queue_handle 0x4C38
#endif
#define HIVE_SIZE_sp_host2sp_isys_event_queue_handle 12

#ifndef ISP2401
/* function ia_css_thread_sp_yield: 6A70 */
#else
/* function ia_css_thread_sp_yield: 6BEA */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_param_desc
#define HIVE_MEM_sp_all_cbs_param_desc scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_all_cbs_param_desc 0x474C
#else
#define HIVE_ADDR_sp_all_cbs_param_desc 0x4794
#endif
#define HIVE_SIZE_sp_all_cbs_param_desc 8
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_param_desc scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cbs_param_desc 0x474C
#else
#define HIVE_ADDR_sp_sp_all_cbs_param_desc 0x4794
#endif
#define HIVE_SIZE_sp_sp_all_cbs_param_desc 8

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_dmaproxy_sp_invalidate_tlb
#define HIVE_MEM_ia_css_dmaproxy_sp_invalidate_tlb scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_dmaproxy_sp_invalidate_tlb 0x5BF4
#else
#define HIVE_ADDR_ia_css_dmaproxy_sp_invalidate_tlb 0x5C50
#endif
#define HIVE_SIZE_ia_css_dmaproxy_sp_invalidate_tlb 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_dmaproxy_sp_invalidate_tlb scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_dmaproxy_sp_invalidate_tlb 0x5BF4
#else
#define HIVE_ADDR_sp_ia_css_dmaproxy_sp_invalidate_tlb 0x5C50
#endif
#define HIVE_SIZE_sp_ia_css_dmaproxy_sp_invalidate_tlb 4

#ifndef ISP2401
/* function ia_css_thread_sp_fork: D10 */
#else
/* function ia_css_thread_sp_fork: D05 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_destroy: 27F4 */
#else
/* function ia_css_tagger_sp_destroy: 2844 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_vmem_read: 31B7 */
#else
/* function ia_css_dmaproxy_sp_vmem_read: 3396 */
#endif

#ifndef ISP2401
/* function ia_css_ifmtr_sp_init: 6136 */
#else
/* function ia_css_ifmtr_sp_init: 6271 */
#endif

#ifndef ISP2401
/* function initialize_sp_group: 6D4 */
#else
/* function initialize_sp_group: 668 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_peek: 2919 */
#else
/* function ia_css_tagger_buf_sp_peek: 2ABB */
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_init: D3C */
#else
/* function ia_css_thread_sp_init: D31 */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_reset_exp_id: 60DD */
#else
/* function ia_css_isys_sp_reset_exp_id: 6218 */
#endif

#ifndef ISP2401
/* function qos_scheduler_update_fps: 65F0 */
#else
/* function qos_scheduler_update_fps: 6763 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_set_stream_base_addr: 461E */
#else
/* function ia_css_ispctrl_sp_set_stream_base_addr: 4879 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_DMEM_BASE
#define HIVE_MEM_ISP_DMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_ISP_DMEM_BASE 0x10
#define HIVE_SIZE_ISP_DMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_DMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ISP_DMEM_BASE 0x10
#define HIVE_SIZE_sp_ISP_DMEM_BASE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_SP_DMEM_BASE
#define HIVE_MEM_SP_DMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_SP_DMEM_BASE 0x4
#define HIVE_SIZE_SP_DMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_SP_DMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_SP_DMEM_BASE 0x4
#define HIVE_SIZE_sp_SP_DMEM_BASE 4

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_read: 322D */
#else
/* function __ia_css_queue_is_empty_text: 4B68 */

/* function ia_css_dmaproxy_sp_read: 340C */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_raw_copy_line_count
#define HIVE_MEM_raw_copy_line_count scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_raw_copy_line_count 0x2C8
#else
#define HIVE_ADDR_raw_copy_line_count 0x2E0
#endif
#define HIVE_SIZE_raw_copy_line_count 4
#else
#endif
#endif
#define HIVE_MEM_sp_raw_copy_line_count scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_raw_copy_line_count 0x2C8
#else
#define HIVE_ADDR_sp_raw_copy_line_count 0x2E0
#endif
#define HIVE_SIZE_sp_raw_copy_line_count 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_tag_cmd_queue_handle
#define HIVE_MEM_host2sp_tag_cmd_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host2sp_tag_cmd_queue_handle 0x4BE8
#else
#define HIVE_ADDR_host2sp_tag_cmd_queue_handle 0x4C44
#endif
#define HIVE_SIZE_host2sp_tag_cmd_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_tag_cmd_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host2sp_tag_cmd_queue_handle 0x4BE8
#else
#define HIVE_ADDR_sp_host2sp_tag_cmd_queue_handle 0x4C44
#endif
#define HIVE_SIZE_sp_host2sp_tag_cmd_queue_handle 12

#ifndef ISP2401
/* function ia_css_queue_peek: 4923 */
#else
/* function ia_css_queue_peek: 4B81 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_frame_cnt
#define HIVE_MEM_ia_css_flash_sp_frame_cnt scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_flash_sp_frame_cnt 0x4A94
#else
#define HIVE_ADDR_ia_css_flash_sp_frame_cnt 0x4AF0
#endif
#define HIVE_SIZE_ia_css_flash_sp_frame_cnt 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_frame_cnt scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_flash_sp_frame_cnt 0x4A94
#else
#define HIVE_ADDR_sp_ia_css_flash_sp_frame_cnt 0x4AF0
#endif
#define HIVE_SIZE_sp_ia_css_flash_sp_frame_cnt 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_event_can_send_token_mask
#define HIVE_MEM_event_can_send_token_mask scalar_processor_2400_dmem
#define HIVE_ADDR_event_can_send_token_mask 0x88
#define HIVE_SIZE_event_can_send_token_mask 44
#else
#endif
#endif
#define HIVE_MEM_sp_event_can_send_token_mask scalar_processor_2400_dmem
#define HIVE_ADDR_sp_event_can_send_token_mask 0x88
#define HIVE_SIZE_sp_event_can_send_token_mask 44

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_thread
#define HIVE_MEM_isp_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isp_thread 0x5F40
#else
#define HIVE_ADDR_isp_thread 0x5FA0
#endif
#define HIVE_SIZE_isp_thread 4
#else
#endif
#endif
#define HIVE_MEM_sp_isp_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_thread 0x5F40
#else
#define HIVE_ADDR_sp_isp_thread 0x5FA0
#endif
#define HIVE_SIZE_sp_isp_thread 4

#ifndef ISP2401
/* function encode_and_post_sp_event_non_blocking: A78 */
#else
/* function encode_and_post_sp_event_non_blocking: A0C */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_frontend_destroy: 5DDF */
#else
/* function ia_css_isys_sp_frontend_destroy: 5F1A */
#endif

/* function is_ddr_debug_buffer_full: 2CC */

#ifndef ISP2401
/* function ia_css_isys_sp_frontend_stop: 5D1F */
#else
/* function ia_css_isys_sp_frontend_stop: 5E5A */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_token_map_init: 607B */
#else
/* function ia_css_isys_sp_token_map_init: 61B6 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_get_oldest_marked_offset: 2969 */
#else
/* function ia_css_tagger_buf_sp_get_oldest_marked_offset: 2B0B */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_threads_fiber
#define HIVE_MEM_sp_threads_fiber scalar_processor_2400_dmem
#define HIVE_ADDR_sp_threads_fiber 0x19C
#define HIVE_SIZE_sp_threads_fiber 28
#else
#endif
#endif
#define HIVE_MEM_sp_sp_threads_fiber scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_threads_fiber 0x19C
#define HIVE_SIZE_sp_sp_threads_fiber 28

#ifndef ISP2401
/* function encode_and_post_sp_event: A01 */
#else
/* function encode_and_post_sp_event: 995 */
#endif

/* function debug_enqueue_ddr: EE */

#ifndef ISP2401
/* function ia_css_rmgr_sp_refcount_init_vbuf: 626A */
#else
/* function ia_css_rmgr_sp_refcount_init_vbuf: 63A5 */
#endif

#ifndef ISP2401
/* function dmaproxy_sp_read_write: 6EE4 */
#else
/* function dmaproxy_sp_read_write: 7017 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_dmaproxy_isp_dma_cmd_buffer
#define HIVE_MEM_ia_css_dmaproxy_isp_dma_cmd_buffer scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_dmaproxy_isp_dma_cmd_buffer 0x5BF8
#else
#define HIVE_ADDR_ia_css_dmaproxy_isp_dma_cmd_buffer 0x5C54
#endif
#define HIVE_SIZE_ia_css_dmaproxy_isp_dma_cmd_buffer 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_dmaproxy_isp_dma_cmd_buffer scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_dmaproxy_isp_dma_cmd_buffer 0x5BF8
#else
#define HIVE_ADDR_sp_ia_css_dmaproxy_isp_dma_cmd_buffer 0x5C54
#endif
#define HIVE_SIZE_sp_ia_css_dmaproxy_isp_dma_cmd_buffer 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_buffer_queue_handle
#define HIVE_MEM_host2sp_buffer_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host2sp_buffer_queue_handle 0x4BF4
#else
#define HIVE_ADDR_host2sp_buffer_queue_handle 0x4C50
#endif
#define HIVE_SIZE_host2sp_buffer_queue_handle 480
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_buffer_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host2sp_buffer_queue_handle 0x4BF4
#else
#define HIVE_ADDR_sp_host2sp_buffer_queue_handle 0x4C50
#endif
#define HIVE_SIZE_sp_host2sp_buffer_queue_handle 480

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_in_service
#define HIVE_MEM_ia_css_flash_sp_in_service scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_flash_sp_in_service 0x3178
#else
#define HIVE_ADDR_ia_css_flash_sp_in_service 0x3198
#endif
#define HIVE_SIZE_ia_css_flash_sp_in_service 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_in_service scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_flash_sp_in_service 0x3178
#else
#define HIVE_ADDR_sp_ia_css_flash_sp_in_service 0x3198
#endif
#define HIVE_SIZE_sp_ia_css_flash_sp_in_service 4

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_process: 6BF0 */
#else
/* function ia_css_dmaproxy_sp_process: 6D63 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_mark_from_end: 2BF1 */
#else
/* function ia_css_tagger_buf_sp_mark_from_end: 2D93 */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_backend_rcv_acquire_ack: 59EC */
#else
/* function ia_css_isys_sp_backend_rcv_acquire_ack: 5B27 */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_backend_pre_acquire_request: 5A02 */
#else
/* function ia_css_isys_sp_backend_pre_acquire_request: 5B3D */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_init_cs: 3653 */
#else
/* function ia_css_ispctrl_sp_init_cs: 3855 */
#endif

#ifndef ISP2401
/* function ia_css_spctrl_sp_init: 5958 */
#else
/* function ia_css_spctrl_sp_init: 5A93 */
#endif

#ifndef ISP2401
/* function sp_event_proxy_init: 730 */
#else
/* function sp_event_proxy_init: 6C4 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_previous_clock_tick
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_previous_clock_tick scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_previous_clock_tick 0x4DD4
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_previous_clock_tick 0x4E30
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_previous_clock_tick 40
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_previous_clock_tick scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_previous_clock_tick 0x4DD4
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_previous_clock_tick 0x4E30
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_previous_clock_tick 40

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_output
#define HIVE_MEM_sp_output scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_output 0x41F8
#else
#define HIVE_ADDR_sp_output 0x4218
#endif
#define HIVE_SIZE_sp_output 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_output scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_output 0x41F8
#else
#define HIVE_ADDR_sp_sp_output 0x4218
#endif
#define HIVE_SIZE_sp_sp_output 16

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_sems_for_host2sp_buf_queues
#define HIVE_MEM_ia_css_bufq_sp_sems_for_host2sp_buf_queues scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_sems_for_host2sp_buf_queues 0x4DFC
#else
#define HIVE_ADDR_ia_css_bufq_sp_sems_for_host2sp_buf_queues 0x4E58
#endif
#define HIVE_SIZE_ia_css_bufq_sp_sems_for_host2sp_buf_queues 800
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_sems_for_host2sp_buf_queues scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_sems_for_host2sp_buf_queues 0x4DFC
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_sems_for_host2sp_buf_queues 0x4E58
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_sems_for_host2sp_buf_queues 800

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_CTRL_BASE
#define HIVE_MEM_ISP_CTRL_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_ISP_CTRL_BASE 0x8
#define HIVE_SIZE_ISP_CTRL_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_CTRL_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ISP_CTRL_BASE 0x8
#define HIVE_SIZE_sp_ISP_CTRL_BASE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_INPUT_FORMATTER_BASE
#define HIVE_MEM_INPUT_FORMATTER_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_INPUT_FORMATTER_BASE 0x4C
#define HIVE_SIZE_INPUT_FORMATTER_BASE 16
#else
#endif
#endif
#define HIVE_MEM_sp_INPUT_FORMATTER_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_INPUT_FORMATTER_BASE 0x4C
#define HIVE_SIZE_sp_INPUT_FORMATTER_BASE 16

#ifndef ISP2401
/* function sp_dma_proxy_reset_channels: 3487 */
#else
/* function sp_dma_proxy_reset_channels: 367B */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_backend_acquire: 5B0D */
#else
/* function ia_css_isys_sp_backend_acquire: 5C48 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_update_size: 28E8 */
#else
/* function ia_css_tagger_sp_update_size: 2A8A */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_host_sp_queue
#define HIVE_MEM_ia_css_bufq_host_sp_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_host_sp_queue 0x511C
#else
#define HIVE_ADDR_ia_css_bufq_host_sp_queue 0x5178
#endif
#define HIVE_SIZE_ia_css_bufq_host_sp_queue 2008
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_host_sp_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_host_sp_queue 0x511C
#else
#define HIVE_ADDR_sp_ia_css_bufq_host_sp_queue 0x5178
#endif
#define HIVE_SIZE_sp_ia_css_bufq_host_sp_queue 2008

#ifndef ISP2401
/* function thread_fiber_sp_create: DA8 */
#else
/* function thread_fiber_sp_create: D9D */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_set_increments: 3319 */
#else
/* function ia_css_dmaproxy_sp_set_increments: 350D */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_writing_cb_frame
#define HIVE_MEM_sem_for_writing_cb_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_writing_cb_frame 0x4754
#else
#define HIVE_ADDR_sem_for_writing_cb_frame 0x479C
#endif
#define HIVE_SIZE_sem_for_writing_cb_frame 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_writing_cb_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_writing_cb_frame 0x4754
#else
#define HIVE_ADDR_sp_sem_for_writing_cb_frame 0x479C
#endif
#define HIVE_SIZE_sp_sem_for_writing_cb_frame 20

#ifndef ISP2401
/* function receiver_reg_store: AD7 */
#else
/* function receiver_reg_store: AD1 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_writing_cb_param
#define HIVE_MEM_sem_for_writing_cb_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_writing_cb_param 0x4768
#else
#define HIVE_ADDR_sem_for_writing_cb_param 0x47B0
#endif
#define HIVE_SIZE_sem_for_writing_cb_param 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_writing_cb_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_writing_cb_param 0x4768
#else
#define HIVE_ADDR_sp_sem_for_writing_cb_param 0x47B0
#endif
#define HIVE_SIZE_sp_sem_for_writing_cb_param 20

/* function sp_start_isp_entry: 453 */
#ifndef HIVE_MULTIPLE_PROGRAMS
#ifdef HIVE_ADDR_sp_start_isp_entry
#endif
#define HIVE_ADDR_sp_start_isp_entry 0x453
#endif
#define HIVE_ADDR_sp_sp_start_isp_entry 0x453

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_unmark_all: 2B75 */
#else
/* function ia_css_tagger_buf_sp_unmark_all: 2D17 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_unmark_from_start: 2BB6 */
#else
/* function ia_css_tagger_buf_sp_unmark_from_start: 2D58 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_channel_acquire: 34B3 */
#else
/* function ia_css_dmaproxy_sp_channel_acquire: 36A7 */
#endif

#ifndef ISP2401
/* function ia_css_rmgr_sp_add_num_vbuf: 64B4 */
#else
/* function ia_css_rmgr_sp_add_num_vbuf: 65EF */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_token_map_create: 60C4 */
#else
/* function ia_css_isys_sp_token_map_create: 61FF */
#endif

#ifndef ISP2401
/* function __ia_css_dmaproxy_sp_wait_for_ack_text: 3183 */
#else
/* function __ia_css_dmaproxy_sp_wait_for_ack_text: 3362 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_acquire_buf_elem: 1FDB */
#else
/* function ia_css_tagger_sp_acquire_buf_elem: 202B */
#endif

#ifndef ISP2401
/* function ia_css_bufq_sp_is_dynamic_buffer: 306C */
#else
/* function ia_css_bufq_sp_is_dynamic_buffer: 320E */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_group
#define HIVE_MEM_sp_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_group 0x4208
#define HIVE_SIZE_sp_group 1144
#else
#define HIVE_ADDR_sp_group 0x4228
#define HIVE_SIZE_sp_group 1184
#endif
#else
#endif
#endif
#define HIVE_MEM_sp_sp_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_group 0x4208
#define HIVE_SIZE_sp_sp_group 1144
#else
#define HIVE_ADDR_sp_sp_group 0x4228
#define HIVE_SIZE_sp_sp_group 1184
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_event_proxy_thread
#define HIVE_MEM_sp_event_proxy_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_event_proxy_thread 0x4954
#define HIVE_SIZE_sp_event_proxy_thread 68
#else
#define HIVE_ADDR_sp_event_proxy_thread 0x49B0
#define HIVE_SIZE_sp_event_proxy_thread 72
#endif
#else
#endif
#endif
#define HIVE_MEM_sp_sp_event_proxy_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_event_proxy_thread 0x4954
#define HIVE_SIZE_sp_sp_event_proxy_thread 68
#else
#define HIVE_ADDR_sp_sp_event_proxy_thread 0x49B0
#define HIVE_SIZE_sp_sp_event_proxy_thread 72
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_kill: CD6 */
#else
/* function ia_css_thread_sp_kill: CCB */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_create: 28A2 */
#else
/* function ia_css_tagger_sp_create: 2A38 */
#endif

#ifndef ISP2401
/* function tmpmem_acquire_dmem: 6561 */
#else
/* function tmpmem_acquire_dmem: 669C */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_MMU_BASE
#define HIVE_MEM_MMU_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_MMU_BASE 0x24
#define HIVE_SIZE_MMU_BASE 8
#else
#endif
#endif
#define HIVE_MEM_sp_MMU_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_MMU_BASE 0x24
#define HIVE_SIZE_sp_MMU_BASE 8

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_channel_release: 349F */
#else
/* function ia_css_dmaproxy_sp_channel_release: 3693 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_is_idle: 347F */
#else
/* function ia_css_dmaproxy_sp_is_idle: 3673 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_qos_start
#define HIVE_MEM_sem_for_qos_start scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_qos_start 0x477C
#else
#define HIVE_ADDR_sem_for_qos_start 0x47C4
#endif
#define HIVE_SIZE_sem_for_qos_start 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_qos_start scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_qos_start 0x477C
#else
#define HIVE_ADDR_sp_sem_for_qos_start 0x47C4
#endif
#define HIVE_SIZE_sp_sem_for_qos_start 20

#ifndef ISP2401
/* function isp_hmem_load: B55 */
#else
/* function isp_hmem_load: B4F */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_release_buf_elem: 1FB7 */
#else
/* function ia_css_tagger_sp_release_buf_elem: 2007 */
#endif

#ifndef ISP2401
/* function ia_css_eventq_sp_send: 34F5 */
#else
/* function ia_css_eventq_sp_send: 36E9 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_isys_sp_error_cnt
#define HIVE_MEM_ia_css_isys_sp_error_cnt scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_isys_sp_error_cnt 0x62D4
#else
#define HIVE_ADDR_ia_css_isys_sp_error_cnt 0x6330
#endif
#define HIVE_SIZE_ia_css_isys_sp_error_cnt 16
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_isys_sp_error_cnt scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_isys_sp_error_cnt 0x62D4
#else
#define HIVE_ADDR_sp_ia_css_isys_sp_error_cnt 0x6330
#endif
#define HIVE_SIZE_sp_ia_css_isys_sp_error_cnt 16

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_unlock_from_start: 2AA5 */
#else
/* function ia_css_tagger_buf_sp_unlock_from_start: 2C47 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_debug_buffer_ddr_address
#define HIVE_MEM_debug_buffer_ddr_address scalar_processor_2400_dmem
#define HIVE_ADDR_debug_buffer_ddr_address 0xBC
#define HIVE_SIZE_debug_buffer_ddr_address 4
#else
#endif
#endif
#define HIVE_MEM_sp_debug_buffer_ddr_address scalar_processor_2400_dmem
#define HIVE_ADDR_sp_debug_buffer_ddr_address 0xBC
#define HIVE_SIZE_sp_debug_buffer_ddr_address 4

#ifndef ISP2401
/* function sp_isys_copy_request: 714 */
#else
/* function sp_isys_copy_request: 6A8 */
#endif

#ifndef ISP2401
/* function ia_css_rmgr_sp_refcount_retain_vbuf: 6344 */
#else
/* function ia_css_rmgr_sp_refcount_retain_vbuf: 647F */
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_set_priority: CCE */
#else
/* function ia_css_thread_sp_set_priority: CC3 */
#endif

#ifndef ISP2401
/* function sizeof_hmem: BFC */
#else
/* function sizeof_hmem: BF6 */
#endif

#ifndef ISP2401
/* function tmpmem_release_dmem: 6550 */
#else
/* function tmpmem_release_dmem: 668B */
#endif

/* function cnd_input_system_cfg: 392 */

#ifndef ISP2401
/* function __ia_css_sp_rawcopy_func_critical: 6F65 */
#else
/* function __ia_css_sp_rawcopy_func_critical: 70C2 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_set_width_exception: 3304 */
#else
/* function __ia_css_dmaproxy_sp_process_text: 3306 */
#endif

#ifndef ISP2401
/* function sp_event_assert: 8B1 */
#else
/* function ia_css_dmaproxy_sp_set_width_exception: 34F8 */
#endif

#ifndef ISP2401
/* function ia_css_flash_sp_init_internal_params: 2C90 */
#else
/* function sp_event_assert: 845 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_pop_unmarked_and_unlocked: 29AB */
#else
/* function ia_css_flash_sp_init_internal_params: 2E32 */
#endif

#ifndef ISP2401
/* function __modu: 68BB */
#else
/* function ia_css_tagger_buf_sp_pop_unmarked_and_unlocked: 2B4D */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_init_isp_vector: 3189 */
#else
/* function __modu: 6A2E */

/* function ia_css_dmaproxy_sp_init_isp_vector: 3368 */
#endif

/* function isp_vamem_store: 0 */

#ifdef ISP2401
/* function ia_css_tagger_sp_set_copy_pipe: 2A2F */

#endif
#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_GDC_BASE
#define HIVE_MEM_GDC_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_GDC_BASE 0x44
#define HIVE_SIZE_GDC_BASE 8
#else
#endif
#endif
#define HIVE_MEM_sp_GDC_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_GDC_BASE 0x44
#define HIVE_SIZE_sp_GDC_BASE 8

#ifndef ISP2401
/* function ia_css_queue_local_init: 4C0E */
#else
/* function ia_css_queue_local_init: 4E6C */
#endif

#ifndef ISP2401
/* function sp_event_proxy_callout_func: 6988 */
#else
/* function sp_event_proxy_callout_func: 6AFB */
#endif

#ifndef ISP2401
/* function qos_scheduler_schedule_stage: 65C1 */
#else
/* function qos_scheduler_schedule_stage: 670F */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_thread_sp_num_ready_threads
#define HIVE_MEM_ia_css_thread_sp_num_ready_threads scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_thread_sp_num_ready_threads 0x49E0
#else
#define HIVE_ADDR_ia_css_thread_sp_num_ready_threads 0x4A40
#endif
#define HIVE_SIZE_ia_css_thread_sp_num_ready_threads 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_thread_sp_num_ready_threads scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_thread_sp_num_ready_threads 0x49E0
#else
#define HIVE_ADDR_sp_ia_css_thread_sp_num_ready_threads 0x4A40
#endif
#define HIVE_SIZE_sp_ia_css_thread_sp_num_ready_threads 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_threads_stack_size
#define HIVE_MEM_sp_threads_stack_size scalar_processor_2400_dmem
#define HIVE_ADDR_sp_threads_stack_size 0x180
#define HIVE_SIZE_sp_threads_stack_size 28
#else
#endif
#endif
#define HIVE_MEM_sp_sp_threads_stack_size scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_threads_stack_size 0x180
#define HIVE_SIZE_sp_sp_threads_stack_size 28

#ifndef ISP2401
/* function ia_css_ispctrl_sp_isp_done_row_striping: 3F2F */
#else
/* function ia_css_ispctrl_sp_isp_done_row_striping: 4172 */
#endif

#ifndef ISP2401
/* function __ia_css_isys_sp_isr_text: 5E09 */
#else
/* function __ia_css_isys_sp_isr_text: 5F44 */
#endif

#ifndef ISP2401
/* function ia_css_queue_dequeue: 4A8C */
#else
/* function ia_css_queue_dequeue: 4CEA */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_configure_channel: 6E4C */
#else
/* function is_qos_standalone_mode: 66EA */

/* function ia_css_dmaproxy_sp_configure_channel: 6F90 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_current_thread_fiber_sp
#define HIVE_MEM_current_thread_fiber_sp scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_current_thread_fiber_sp 0x49E8
#else
#define HIVE_ADDR_current_thread_fiber_sp 0x4A44
#endif
#define HIVE_SIZE_current_thread_fiber_sp 4
#else
#endif
#endif
#define HIVE_MEM_sp_current_thread_fiber_sp scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_current_thread_fiber_sp 0x49E8
#else
#define HIVE_ADDR_sp_current_thread_fiber_sp 0x4A44
#endif
#define HIVE_SIZE_sp_current_thread_fiber_sp 4

#ifndef ISP2401
/* function ia_css_circbuf_pop: FD8 */
#else
/* function ia_css_circbuf_pop: FCD */
#endif

#ifndef ISP2401
/* function memset: 693A */
#else
/* function memset: 6AAD */
#endif

/* function irq_raise_set_token: B6 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_GPIO_BASE
#define HIVE_MEM_GPIO_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_GPIO_BASE 0x3C
#define HIVE_SIZE_GPIO_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_GPIO_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_GPIO_BASE 0x3C
#define HIVE_SIZE_sp_GPIO_BASE 4

#ifndef ISP2401
/* function ia_css_pipeline_acc_stage_enable: 17D7 */
#else
/* function ia_css_pipeline_acc_stage_enable: 17FF */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_unlock_exp_id: 2028 */
#else
/* function ia_css_tagger_sp_unlock_exp_id: 2078 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_ph
#define HIVE_MEM_isp_ph scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isp_ph 0x62E4
#else
#define HIVE_ADDR_isp_ph 0x6340
#endif
#define HIVE_SIZE_isp_ph 28
#else
#endif
#endif
#define HIVE_MEM_sp_isp_ph scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_ph 0x62E4
#else
#define HIVE_ADDR_sp_isp_ph 0x6340
#endif
#define HIVE_SIZE_sp_isp_ph 28

#ifndef ISP2401
/* function ia_css_isys_sp_token_map_flush: 6009 */
#else
/* function ia_css_isys_sp_token_map_flush: 6144 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_init_ds: 37B2 */
#else
/* function ia_css_ispctrl_sp_init_ds: 39E1 */
#endif

#ifndef ISP2401
/* function get_xmem_base_addr_raw: 3B5F */
#else
/* function get_xmem_base_addr_raw: 3D9A */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_param
#define HIVE_MEM_sp_all_cbs_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_all_cbs_param 0x4790
#else
#define HIVE_ADDR_sp_all_cbs_param 0x47D8
#endif
#define HIVE_SIZE_sp_all_cbs_param 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cbs_param 0x4790
#else
#define HIVE_ADDR_sp_sp_all_cbs_param 0x47D8
#endif
#define HIVE_SIZE_sp_sp_all_cbs_param 16

#ifndef ISP2401
/* function ia_css_circbuf_create: 1026 */
#else
/* function ia_css_circbuf_create: 101B */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_sp_group
#define HIVE_MEM_sem_for_sp_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_sp_group 0x47A0
#else
#define HIVE_ADDR_sem_for_sp_group 0x47E8
#endif
#define HIVE_SIZE_sem_for_sp_group 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_sp_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_sp_group 0x47A0
#else
#define HIVE_ADDR_sp_sem_for_sp_group 0x47E8
#endif
#define HIVE_SIZE_sp_sem_for_sp_group 20

#ifndef ISP2401
/* function ia_css_framebuf_sp_wait_for_in_frame: 64DF */
#else
/* function __ia_css_dmaproxy_sp_configure_channel_text: 34D7 */

/* function ia_css_framebuf_sp_wait_for_in_frame: 661A */
#endif

#ifndef ISP2401
/* function ia_css_sp_rawcopy_tag_frame: 556F */
#else
/* function ia_css_sp_rawcopy_tag_frame: 57B0 */
#endif

#ifndef ISP2401
/* function isp_hmem_clear: B25 */
#else
/* function isp_hmem_clear: B1F */
#endif

#ifndef ISP2401
/* function ia_css_framebuf_sp_release_in_frame: 6522 */
#else
/* function ia_css_framebuf_sp_release_in_frame: 665D */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_backend_snd_acquire_request: 5A5F */
#else
/* function ia_css_isys_sp_backend_snd_acquire_request: 5B9A */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_token_map_is_full: 5E90 */
#else
/* function ia_css_isys_sp_token_map_is_full: 5FCB */
#endif

#ifndef ISP2401
/* function input_system_acquisition_run: AF9 */
#else
/* function input_system_acquisition_run: AF3 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_start_binary: 3631 */
#else
/* function ia_css_ispctrl_sp_start_binary: 3833 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_h_pipe_private_ddr_ptrs
#define HIVE_MEM_ia_css_bufq_sp_h_pipe_private_ddr_ptrs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 0x58F4
#else
#define HIVE_ADDR_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 0x5950
#endif
#define HIVE_SIZE_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_h_pipe_private_ddr_ptrs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 0x58F4
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 0x5950
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 20

#ifndef ISP2401
/* function ia_css_eventq_sp_recv: 34C7 */
#else
/* function ia_css_eventq_sp_recv: 36BB */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_pool
#define HIVE_MEM_isp_pool scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isp_pool 0x2E8
#else
#define HIVE_ADDR_isp_pool 0x300
#endif
#define HIVE_SIZE_isp_pool 4
#else
#endif
#endif
#define HIVE_MEM_sp_isp_pool scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_pool 0x2E8
#else
#define HIVE_ADDR_sp_isp_pool 0x300
#endif
#define HIVE_SIZE_sp_isp_pool 4

#ifndef ISP2401
/* function ia_css_rmgr_sp_rel_gen: 6211 */
#else
/* function ia_css_rmgr_sp_rel_gen: 634C */

/* function ia_css_tagger_sp_unblock_clients: 2900 */
#endif

#ifndef ISP2401
/* function css_get_frame_processing_time_end: 1FA7 */
#else
/* function css_get_frame_processing_time_end: 1FF7 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_event_any_pending_mask
#define HIVE_MEM_event_any_pending_mask scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_event_any_pending_mask 0x300
#else
#define HIVE_ADDR_event_any_pending_mask 0x318
#endif
#define HIVE_SIZE_event_any_pending_mask 8
#else
#endif
#endif
#define HIVE_MEM_sp_event_any_pending_mask scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_event_any_pending_mask 0x300
#else
#define HIVE_ADDR_sp_event_any_pending_mask 0x318
#endif
#define HIVE_SIZE_sp_event_any_pending_mask 8

#ifndef ISP2401
/* function ia_css_isys_sp_backend_push: 5A16 */
#else
/* function ia_css_isys_sp_backend_push: 5B51 */
#endif

/* function sh_css_decode_tag_descr: 352 */

/* function debug_enqueue_isp: 27B */

#ifndef ISP2401
/* function qos_scheduler_update_stage_budget: 65AF */
#else
/* function qos_scheduler_update_stage_budget: 66F2 */
#endif

#ifndef ISP2401
/* function ia_css_spctrl_sp_uninit: 5951 */
#else
/* function ia_css_spctrl_sp_uninit: 5A8C */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_HIVE_IF_SWITCH_CODE
#define HIVE_MEM_HIVE_IF_SWITCH_CODE scalar_processor_2400_dmem
#define HIVE_ADDR_HIVE_IF_SWITCH_CODE 0x1D8
#define HIVE_SIZE_HIVE_IF_SWITCH_CODE 4
#else
#endif
#endif
#define HIVE_MEM_sp_HIVE_IF_SWITCH_CODE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_HIVE_IF_SWITCH_CODE 0x1D8
#define HIVE_SIZE_sp_HIVE_IF_SWITCH_CODE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_dis_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_dis_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_dis_bufs 0x5908
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_dis_bufs 0x5964
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_dis_bufs 140
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_dis_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_dis_bufs 0x5908
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_dis_bufs 0x5964
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_dis_bufs 140

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_lock_from_start: 2AD9 */
#else
/* function ia_css_tagger_buf_sp_lock_from_start: 2C7B */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_isp_idle
#define HIVE_MEM_sem_for_isp_idle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_isp_idle 0x47B4
#else
#define HIVE_ADDR_sem_for_isp_idle 0x47FC
#endif
#define HIVE_SIZE_sem_for_isp_idle 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_isp_idle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_isp_idle 0x47B4
#else
#define HIVE_ADDR_sp_sem_for_isp_idle 0x47FC
#endif
#define HIVE_SIZE_sp_sem_for_isp_idle 20

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_write_byte_addr: 31E6 */
#else
/* function ia_css_dmaproxy_sp_write_byte_addr: 33C5 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_init: 315D */
#else
/* function ia_css_dmaproxy_sp_init: 333C */
#endif

#ifndef ISP2401
/* function ia_css_bufq_sp_release_dynamic_buf_clock_tick: 2D62 */
#else
/* function ia_css_bufq_sp_release_dynamic_buf_clock_tick: 2F04 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_VAMEM_BASE
#define HIVE_MEM_ISP_VAMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_ISP_VAMEM_BASE 0x14
#define HIVE_SIZE_ISP_VAMEM_BASE 12
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_VAMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ISP_VAMEM_BASE 0x14
#define HIVE_SIZE_sp_ISP_VAMEM_BASE 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_rawcopy_sp_tagger
#define HIVE_MEM_ia_css_rawcopy_sp_tagger scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_rawcopy_sp_tagger 0x6294
#else
#define HIVE_ADDR_ia_css_rawcopy_sp_tagger 0x62F0
#endif
#define HIVE_SIZE_ia_css_rawcopy_sp_tagger 24
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_rawcopy_sp_tagger scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_rawcopy_sp_tagger 0x6294
#else
#define HIVE_ADDR_sp_ia_css_rawcopy_sp_tagger 0x62F0
#endif
#define HIVE_SIZE_sp_ia_css_rawcopy_sp_tagger 24

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_exp_ids
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_exp_ids scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_exp_ids 0x5994
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_exp_ids 0x59F0
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_exp_ids 70
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_exp_ids scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_exp_ids 0x5994
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_exp_ids 0x59F0
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_exp_ids 70

#ifndef ISP2401
/* function ia_css_queue_item_load: 4D00 */
#else
/* function ia_css_queue_item_load: 4F5E */
#endif

#ifndef ISP2401
/* function ia_css_spctrl_sp_get_state: 593C */
#else
/* function ia_css_spctrl_sp_get_state: 5A77 */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_token_map_uninit: 6026 */
#else
/* function ia_css_isys_sp_token_map_uninit: 6161 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_callout_sp_thread
#define HIVE_MEM_callout_sp_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_callout_sp_thread 0x49DC
#else
#define HIVE_ADDR_callout_sp_thread 0x1E0
#endif
#define HIVE_SIZE_callout_sp_thread 4
#else
#endif
#endif
#define HIVE_MEM_sp_callout_sp_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_callout_sp_thread 0x49DC
#else
#define HIVE_ADDR_sp_callout_sp_thread 0x1E0
#endif
#define HIVE_SIZE_sp_callout_sp_thread 4

#ifndef ISP2401
/* function thread_fiber_sp_init: E2F */
#else
/* function thread_fiber_sp_init: E24 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_SP_PMEM_BASE
#define HIVE_MEM_SP_PMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_SP_PMEM_BASE 0x0
#define HIVE_SIZE_SP_PMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_SP_PMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_SP_PMEM_BASE 0x0
#define HIVE_SIZE_sp_SP_PMEM_BASE 4

#ifndef ISP2401
/* function ia_css_isys_sp_token_map_snd_acquire_req: 5F96 */
#else
/* function ia_css_isys_sp_token_map_snd_acquire_req: 60D1 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_isp_input_stream_format
#define HIVE_MEM_sp_isp_input_stream_format scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_input_stream_format 0x40F8
#else
#define HIVE_ADDR_sp_isp_input_stream_format 0x4118
#endif
#define HIVE_SIZE_sp_isp_input_stream_format 20
#else
#endif
#endif
#define HIVE_MEM_sp_sp_isp_input_stream_format scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_isp_input_stream_format 0x40F8
#else
#define HIVE_ADDR_sp_sp_isp_input_stream_format 0x4118
#endif
#define HIVE_SIZE_sp_sp_isp_input_stream_format 20

#ifndef ISP2401
/* function __mod: 68A7 */
#else
/* function __mod: 6A1A */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_init_dmem_channel: 3247 */
#else
/* function ia_css_dmaproxy_sp_init_dmem_channel: 3426 */
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_join: CFF */
#else
/* function ia_css_thread_sp_join: CF4 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_add_command: 6F4F */
#else
/* function ia_css_dmaproxy_sp_add_command: 7082 */
#endif

#ifndef ISP2401
/* function ia_css_sp_metadata_thread_func: 57F0 */
#else
/* function ia_css_sp_metadata_thread_func: 594F */
#endif

#ifndef ISP2401
/* function __sp_event_proxy_func_critical: 6975 */
#else
/* function __sp_event_proxy_func_critical: 6AE8 */
#endif

#ifndef ISP2401
/* function ia_css_sp_metadata_wait: 5903 */
#else
/* function ia_css_sp_metadata_wait: 5A3E */
#endif

#ifndef ISP2401
/* function ia_css_circbuf_peek_from_start: F08 */
#else
/* function ia_css_circbuf_peek_from_start: EFD */
#endif

#ifndef ISP2401
/* function ia_css_event_sp_encode: 3552 */
#else
/* function ia_css_event_sp_encode: 3746 */
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_run: D72 */
#else
/* function ia_css_thread_sp_run: D67 */
#endif

#ifndef ISP2401
/* function sp_isys_copy_func: 6F6 */
#else
/* function sp_isys_copy_func: 68A */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_backend_flush: 5A7F */
#else
/* function ia_css_isys_sp_backend_flush: 5BBA */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_backend_frame_exists: 599B */
#else
/* function ia_css_isys_sp_backend_frame_exists: 5AD6 */
#endif

#ifndef ISP2401
/* function ia_css_sp_isp_param_init_isp_memories: 4789 */
#else
/* function ia_css_sp_isp_param_init_isp_memories: 4A11 */
#endif

#ifndef ISP2401
/* function register_isr: 8A9 */
#else
/* function register_isr: 83D */
#endif

/* function irq_raise: C8 */

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_mmu_invalidate: 3124 */
#else
/* function ia_css_dmaproxy_sp_mmu_invalidate: 32CC */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_HIVE_IF_SRST_ADDRESS
#define HIVE_MEM_HIVE_IF_SRST_ADDRESS scalar_processor_2400_dmem
#define HIVE_ADDR_HIVE_IF_SRST_ADDRESS 0x1B8
#define HIVE_SIZE_HIVE_IF_SRST_ADDRESS 16
#else
#endif
#endif
#define HIVE_MEM_sp_HIVE_IF_SRST_ADDRESS scalar_processor_2400_dmem
#define HIVE_ADDR_sp_HIVE_IF_SRST_ADDRESS 0x1B8
#define HIVE_SIZE_sp_HIVE_IF_SRST_ADDRESS 16

#ifndef ISP2401
/* function pipeline_sp_initialize_stage: 190B */
#else
/* function pipeline_sp_initialize_stage: 1945 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_isys_sp_frontend_states
#define HIVE_MEM_ia_css_isys_sp_frontend_states scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_isys_sp_frontend_states 0x62C8
#else
#define HIVE_ADDR_ia_css_isys_sp_frontend_states 0x6324
#endif
#define HIVE_SIZE_ia_css_isys_sp_frontend_states 12
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_isys_sp_frontend_states scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_isys_sp_frontend_states 0x62C8
#else
#define HIVE_ADDR_sp_ia_css_isys_sp_frontend_states 0x6324
#endif
#define HIVE_SIZE_sp_ia_css_isys_sp_frontend_states 12

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_read_byte_addr_mmio: 6E1E */
#else
/* function ia_css_dmaproxy_sp_read_byte_addr_mmio: 6F62 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_done_ds: 3799 */
#else
/* function ia_css_ispctrl_sp_done_ds: 39C8 */
#endif

#ifndef ISP2401
/* function ia_css_sp_isp_param_get_mem_inits: 4764 */
#else
/* function ia_css_sp_isp_param_get_mem_inits: 49EC */
#endif

#ifndef ISP2401
/* function ia_css_parambuf_sp_init_buffer_queues: 13D0 */
#else
/* function ia_css_parambuf_sp_init_buffer_queues: 13F1 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_pfp_spref
#define HIVE_MEM_vbuf_pfp_spref scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_vbuf_pfp_spref 0x2F0
#else
#define HIVE_ADDR_vbuf_pfp_spref 0x308
#endif
#define HIVE_SIZE_vbuf_pfp_spref 4
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_pfp_spref scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_vbuf_pfp_spref 0x2F0
#else
#define HIVE_ADDR_sp_vbuf_pfp_spref 0x308
#endif
#define HIVE_SIZE_sp_vbuf_pfp_spref 4

#ifndef ISP2401
/* function input_system_cfg: ABB */
#else
/* function input_system_cfg: AB5 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_HMEM_BASE
#define HIVE_MEM_ISP_HMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_ISP_HMEM_BASE 0x20
#define HIVE_SIZE_ISP_HMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_HMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ISP_HMEM_BASE 0x20
#define HIVE_SIZE_sp_ISP_HMEM_BASE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_frames
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_frames scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_frames 0x59DC
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_frames 0x5A38
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_frames 280
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_frames scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_frames 0x59DC
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_frames 0x5A38
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_frames 280

#ifndef ISP2401
/* function qos_scheduler_init_stage_budget: 65E8 */
#else
/* function qos_scheduler_init_stage_budget: 6750 */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_backend_release: 5AF4 */
#else
/* function ia_css_isys_sp_backend_release: 5C2F */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_backend_destroy: 5B1E */
#else
/* function ia_css_isys_sp_backend_destroy: 5C59 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp2host_buffer_queue_handle
#define HIVE_MEM_sp2host_buffer_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp2host_buffer_queue_handle 0x5AF4
#else
#define HIVE_ADDR_sp2host_buffer_queue_handle 0x5B50
#endif
#define HIVE_SIZE_sp2host_buffer_queue_handle 96
#else
#endif
#endif
#define HIVE_MEM_sp_sp2host_buffer_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp2host_buffer_queue_handle 0x5AF4
#else
#define HIVE_ADDR_sp_sp2host_buffer_queue_handle 0x5B50
#endif
#define HIVE_SIZE_sp_sp2host_buffer_queue_handle 96

#ifndef ISP2401
/* function ia_css_isys_sp_token_map_check_mipi_frame_size: 5F5A */
#else
/* function ia_css_isys_sp_token_map_check_mipi_frame_size: 6095 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_init_isp_vars: 4483 */
#else
/* function ia_css_ispctrl_sp_init_isp_vars: 46DE */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_frontend_has_empty_mipi_buffer_cb: 5B70 */
#else
/* function ia_css_isys_sp_frontend_has_empty_mipi_buffer_cb: 5CAB */
#endif

#ifndef ISP2401
/* function sp_warning: 8DC */
#else
/* function sp_warning: 870 */
#endif

#ifndef ISP2401
/* function ia_css_rmgr_sp_vbuf_enqueue: 6304 */
#else
/* function ia_css_rmgr_sp_vbuf_enqueue: 643F */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_tag_exp_id: 2142 */
#else
/* function ia_css_tagger_sp_tag_exp_id: 2192 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_write: 31FD */
#else
/* function ia_css_dmaproxy_sp_write: 33DC */
#endif

#ifndef ISP2401
/* function ia_css_parambuf_sp_release_in_param: 1250 */
#else
/* function ia_css_parambuf_sp_release_in_param: 1245 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_irq_sw_interrupt_token
#define HIVE_MEM_irq_sw_interrupt_token scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_irq_sw_interrupt_token 0x40F4
#else
#define HIVE_ADDR_irq_sw_interrupt_token 0x4114
#endif
#define HIVE_SIZE_irq_sw_interrupt_token 4
#else
#endif
#endif
#define HIVE_MEM_sp_irq_sw_interrupt_token scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_irq_sw_interrupt_token 0x40F4
#else
#define HIVE_ADDR_sp_irq_sw_interrupt_token 0x4114
#endif
#define HIVE_SIZE_sp_irq_sw_interrupt_token 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_isp_addresses
#define HIVE_MEM_sp_isp_addresses scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_addresses 0x5F44
#else
#define HIVE_ADDR_sp_isp_addresses 0x5FA4
#endif
#define HIVE_SIZE_sp_isp_addresses 172
#else
#endif
#endif
#define HIVE_MEM_sp_sp_isp_addresses scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_isp_addresses 0x5F44
#else
#define HIVE_ADDR_sp_sp_isp_addresses 0x5FA4
#endif
#define HIVE_SIZE_sp_sp_isp_addresses 172

#ifndef ISP2401
/* function ia_css_rmgr_sp_acq_gen: 6229 */
#else
/* function ia_css_rmgr_sp_acq_gen: 6364 */
#endif

#ifndef ISP2401
/* function receiver_reg_load: AD0 */
#else
/* function receiver_reg_load: ACA */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isps
#define HIVE_MEM_isps scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isps 0x6300
#else
#define HIVE_ADDR_isps 0x635C
#endif
#define HIVE_SIZE_isps 28
#else
#endif
#endif
#define HIVE_MEM_sp_isps scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isps 0x6300
#else
#define HIVE_ADDR_sp_isps 0x635C
#endif
#define HIVE_SIZE_sp_isps 28

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host_sp_queues_initialized
#define HIVE_MEM_host_sp_queues_initialized scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host_sp_queues_initialized 0x410C
#else
#define HIVE_ADDR_host_sp_queues_initialized 0x412C
#endif
#define HIVE_SIZE_host_sp_queues_initialized 4
#else
#endif
#endif
#define HIVE_MEM_sp_host_sp_queues_initialized scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host_sp_queues_initialized 0x410C
#else
#define HIVE_ADDR_sp_host_sp_queues_initialized 0x412C
#endif
#define HIVE_SIZE_sp_host_sp_queues_initialized 4

#ifndef ISP2401
/* function ia_css_queue_uninit: 4BCC */
#else
/* function ia_css_queue_uninit: 4E2A */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_ispctrl_sp_isp_started
#define HIVE_MEM_ia_css_ispctrl_sp_isp_started scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_ispctrl_sp_isp_started 0x5BFC
#else
#define HIVE_ADDR_ia_css_ispctrl_sp_isp_started 0x5C58
#endif
#define HIVE_SIZE_ia_css_ispctrl_sp_isp_started 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_ispctrl_sp_isp_started scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_ispctrl_sp_isp_started 0x5BFC
#else
#define HIVE_ADDR_sp_ia_css_ispctrl_sp_isp_started 0x5C58
#endif
#define HIVE_SIZE_sp_ia_css_ispctrl_sp_isp_started 4

#ifndef ISP2401
/* function ia_css_bufq_sp_release_dynamic_buf: 2DCE */
#else
/* function ia_css_bufq_sp_release_dynamic_buf: 2F70 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_set_height_exception: 32F5 */
#else
/* function ia_css_dmaproxy_sp_set_height_exception: 34E9 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_init_vmem_channel: 327A */
#else
/* function ia_css_dmaproxy_sp_init_vmem_channel: 345A */
#endif

#ifndef ISP2401
#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_num_ready_threads
#define HIVE_MEM_num_ready_threads scalar_processor_2400_dmem
#define HIVE_ADDR_num_ready_threads 0x49E4
#define HIVE_SIZE_num_ready_threads 4
#else
#endif
#endif
#define HIVE_MEM_sp_num_ready_threads scalar_processor_2400_dmem
#define HIVE_ADDR_sp_num_ready_threads 0x49E4
#define HIVE_SIZE_sp_num_ready_threads 4

/* function ia_css_dmaproxy_sp_write_byte_addr_mmio: 31CF */
#else
/* function ia_css_dmaproxy_sp_write_byte_addr_mmio: 33AE */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_spref
#define HIVE_MEM_vbuf_spref scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_vbuf_spref 0x2EC
#else
#define HIVE_ADDR_vbuf_spref 0x304
#endif
#define HIVE_SIZE_vbuf_spref 4
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_spref scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_vbuf_spref 0x2EC
#else
#define HIVE_ADDR_sp_vbuf_spref 0x304
#endif
#define HIVE_SIZE_sp_vbuf_spref 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_metadata_thread
#define HIVE_MEM_sp_metadata_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_metadata_thread 0x4998
#define HIVE_SIZE_sp_metadata_thread 68
#else
#define HIVE_ADDR_sp_metadata_thread 0x49F8
#define HIVE_SIZE_sp_metadata_thread 72
#endif
#else
#endif
#endif
#define HIVE_MEM_sp_sp_metadata_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_metadata_thread 0x4998
#define HIVE_SIZE_sp_sp_metadata_thread 68
#else
#define HIVE_ADDR_sp_sp_metadata_thread 0x49F8
#define HIVE_SIZE_sp_sp_metadata_thread 72
#endif

#ifndef ISP2401
/* function ia_css_queue_enqueue: 4B16 */
#else
/* function ia_css_queue_enqueue: 4D74 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_request
#define HIVE_MEM_ia_css_flash_sp_request scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_flash_sp_request 0x4A98
#else
#define HIVE_ADDR_ia_css_flash_sp_request 0x4AF4
#endif
#define HIVE_SIZE_ia_css_flash_sp_request 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_request scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_flash_sp_request 0x4A98
#else
#define HIVE_ADDR_sp_ia_css_flash_sp_request 0x4AF4
#endif
#define HIVE_SIZE_sp_ia_css_flash_sp_request 4

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_vmem_write: 31A0 */
#else
/* function ia_css_dmaproxy_sp_vmem_write: 337F */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_tagger_frames
#define HIVE_MEM_tagger_frames scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_tagger_frames 0x49EC
#else
#define HIVE_ADDR_tagger_frames 0x4A48
#endif
#define HIVE_SIZE_tagger_frames 168
#else
#endif
#endif
#define HIVE_MEM_sp_tagger_frames scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_tagger_frames 0x49EC
#else
#define HIVE_ADDR_sp_tagger_frames 0x4A48
#endif
#define HIVE_SIZE_sp_tagger_frames 168

#ifndef ISP2401
/* function ia_css_isys_sp_token_map_snd_capture_req: 5FB8 */
#else
/* function ia_css_isys_sp_token_map_snd_capture_req: 60F3 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_reading_if
#define HIVE_MEM_sem_for_reading_if scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_reading_if 0x47C8
#else
#define HIVE_ADDR_sem_for_reading_if 0x4810
#endif
#define HIVE_SIZE_sem_for_reading_if 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_reading_if scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_reading_if 0x47C8
#else
#define HIVE_ADDR_sp_sem_for_reading_if 0x4810
#endif
#define HIVE_SIZE_sp_sem_for_reading_if 20

#ifndef ISP2401
/* function sp_generate_interrupts: 95B */
#else
/* function sp_generate_interrupts: 8EF */

/* function ia_css_pipeline_sp_start: 1858 */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_start: 181E */
#else
/* function ia_css_thread_default_callout: 6BE3 */
#endif

#ifndef ISP2401
/* function ia_css_sp_rawcopy_init: 50F3 */
#else
/* function ia_css_sp_rawcopy_init: 5351 */
#endif

#ifndef ISP2401
/* function tmr_clock_read: 6596 */
#else
/* function tmr_clock_read: 66D1 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_BAMEM_BASE
#define HIVE_MEM_ISP_BAMEM_BASE scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ISP_BAMEM_BASE 0x2F8
#else
#define HIVE_ADDR_ISP_BAMEM_BASE 0x310
#endif
#define HIVE_SIZE_ISP_BAMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_BAMEM_BASE scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ISP_BAMEM_BASE 0x2F8
#else
#define HIVE_ADDR_sp_ISP_BAMEM_BASE 0x310
#endif
#define HIVE_SIZE_sp_ISP_BAMEM_BASE 4

#ifndef ISP2401
/* function ia_css_isys_sp_frontend_rcv_capture_ack: 5C1F */
#else
/* function ia_css_isys_sp_frontend_rcv_capture_ack: 5D5A */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_sems_for_sp2host_buf_queues
#define HIVE_MEM_ia_css_bufq_sp_sems_for_sp2host_buf_queues scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_sems_for_sp2host_buf_queues 0x5B54
#else
#define HIVE_ADDR_ia_css_bufq_sp_sems_for_sp2host_buf_queues 0x5BB0
#endif
#define HIVE_SIZE_ia_css_bufq_sp_sems_for_sp2host_buf_queues 160
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_sems_for_sp2host_buf_queues scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_sems_for_sp2host_buf_queues 0x5B54
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_sems_for_sp2host_buf_queues 0x5BB0
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_sems_for_sp2host_buf_queues 160

#ifndef ISP2401
/* function css_get_frame_processing_time_start: 1FAF */
#else
/* function css_get_frame_processing_time_start: 1FFF */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_frame
#define HIVE_MEM_sp_all_cbs_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_all_cbs_frame 0x47DC
#else
#define HIVE_ADDR_sp_all_cbs_frame 0x4824
#endif
#define HIVE_SIZE_sp_all_cbs_frame 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cbs_frame 0x47DC
#else
#define HIVE_ADDR_sp_sp_all_cbs_frame 0x4824
#endif
#define HIVE_SIZE_sp_sp_all_cbs_frame 16

#ifndef ISP2401
/* function thread_sp_queue_print: D8F */
#else
/* function thread_sp_queue_print: D84 */
#endif

#ifndef ISP2401
/* function sp_notify_eof: 907 */
#else
/* function sp_notify_eof: 89B */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_str2mem
#define HIVE_MEM_sem_for_str2mem scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_str2mem 0x47EC
#else
#define HIVE_ADDR_sem_for_str2mem 0x4834
#endif
#define HIVE_SIZE_sem_for_str2mem 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_str2mem scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_str2mem 0x47EC
#else
#define HIVE_ADDR_sp_sem_for_str2mem 0x4834
#endif
#define HIVE_SIZE_sp_sem_for_str2mem 20

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_is_marked_from_start: 2B41 */
#else
/* function ia_css_tagger_buf_sp_is_marked_from_start: 2CE3 */
#endif

#ifndef ISP2401
/* function ia_css_bufq_sp_acquire_dynamic_buf: 2F86 */
#else
/* function ia_css_bufq_sp_acquire_dynamic_buf: 3128 */
#endif

#ifndef ISP2401
/* function ia_css_circbuf_destroy: 101D */
#else
/* function ia_css_circbuf_destroy: 1012 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_PMEM_BASE
#define HIVE_MEM_ISP_PMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_ISP_PMEM_BASE 0xC
#define HIVE_SIZE_ISP_PMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_PMEM_BASE scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ISP_PMEM_BASE 0xC
#define HIVE_SIZE_sp_ISP_PMEM_BASE 4

#ifndef ISP2401
/* function ia_css_sp_isp_param_mem_load: 46F7 */
#else
/* function ia_css_sp_isp_param_mem_load: 497F */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_pop_from_start: 292D */
#else
/* function ia_css_tagger_buf_sp_pop_from_start: 2ACF */
#endif

#ifndef ISP2401
/* function __div: 685F */
#else
/* function __div: 69D2 */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_frontend_create: 5DF0 */
#else
/* function ia_css_isys_sp_frontend_create: 5F2B */
#endif

#ifndef ISP2401
/* function ia_css_rmgr_sp_refcount_release_vbuf: 6323 */
#else
/* function ia_css_rmgr_sp_refcount_release_vbuf: 645E */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_in_use
#define HIVE_MEM_ia_css_flash_sp_in_use scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_flash_sp_in_use 0x4A9C
#else
#define HIVE_ADDR_ia_css_flash_sp_in_use 0x4AF8
#endif
#define HIVE_SIZE_ia_css_flash_sp_in_use 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_in_use scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_flash_sp_in_use 0x4A9C
#else
#define HIVE_ADDR_sp_ia_css_flash_sp_in_use 0x4AF8
#endif
#define HIVE_SIZE_sp_ia_css_flash_sp_in_use 4

#ifndef ISP2401
/* function ia_css_thread_sem_sp_wait: 6B42 */
#else
/* function ia_css_thread_sem_sp_wait: 6CB7 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_sleep_mode
#define HIVE_MEM_sp_sleep_mode scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sleep_mode 0x4110
#else
#define HIVE_ADDR_sp_sleep_mode 0x4130
#endif
#define HIVE_SIZE_sp_sleep_mode 4
#else
#endif
#endif
#define HIVE_MEM_sp_sp_sleep_mode scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_sleep_mode 0x4110
#else
#define HIVE_ADDR_sp_sp_sleep_mode 0x4130
#endif
#define HIVE_SIZE_sp_sp_sleep_mode 4

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_push: 2A3C */
#else
/* function ia_css_tagger_buf_sp_push: 2BDE */
#endif

/* function mmu_invalidate_cache: D3 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_max_cb_elems
#define HIVE_MEM_sp_max_cb_elems scalar_processor_2400_dmem
#define HIVE_ADDR_sp_max_cb_elems 0x148
#define HIVE_SIZE_sp_max_cb_elems 8
#else
#endif
#endif
#define HIVE_MEM_sp_sp_max_cb_elems scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_max_cb_elems 0x148
#define HIVE_SIZE_sp_sp_max_cb_elems 8

#ifndef ISP2401
/* function ia_css_queue_remote_init: 4BEE */
#else
/* function ia_css_queue_remote_init: 4E4C */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_stop_req
#define HIVE_MEM_isp_stop_req scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isp_stop_req 0x4680
#else
#define HIVE_ADDR_isp_stop_req 0x46C8
#endif
#define HIVE_SIZE_isp_stop_req 4
#else
#endif
#endif
#define HIVE_MEM_sp_isp_stop_req scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_stop_req 0x4680
#else
#define HIVE_ADDR_sp_isp_stop_req 0x46C8
#endif
#define HIVE_SIZE_sp_isp_stop_req 4

#ifndef ISP2401
#define HIVE_ICACHE_sp_critical_SEGMENT_START 0
#define HIVE_ICACHE_sp_critical_NUM_SEGMENTS  1
#endif

#endif /* _sp_map_h_ */
#ifndef ISP2401
extern void sh_css_dump_sp_dmem(void);
void sh_css_dump_sp_dmem(void)
{
}
#endif
