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
/* function longjmp: 680D */
#else
/* function longjmp: 6A0B */
#endif

#ifndef ISP2401
/* function tmpmem_init_dmem: 6558 */
#else
/* function tmpmem_init_dmem: 671E */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_set_addr_B: 3C50 */
#else
/* function ia_css_dmaproxy_sp_set_addr_B: 3DC5 */

/* function ia_css_pipe_data_init_tagger_resources: AC7 */
#endif

/* function debug_buffer_set_ddr_addr: DD */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_mipi
#define HIVE_MEM_vbuf_mipi scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_vbuf_mipi 0x7398
#else
#define HIVE_ADDR_vbuf_mipi 0x7444
#endif
#define HIVE_SIZE_vbuf_mipi 12
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_mipi scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_vbuf_mipi 0x7398
#else
#define HIVE_ADDR_sp_vbuf_mipi 0x7444
#endif
#define HIVE_SIZE_sp_vbuf_mipi 12

#ifndef ISP2401
/* function ia_css_event_sp_decode: 3E41 */
#else
/* function ia_css_event_sp_decode: 3FB6 */
#endif

#ifndef ISP2401
/* function ia_css_queue_get_size: 51BF */
#else
/* function ia_css_queue_get_size: 53C8 */
#endif

#ifndef ISP2401
/* function ia_css_queue_load: 5800 */
#else
/* function ia_css_queue_load: 59DF */
#endif

#ifndef ISP2401
/* function setjmp: 6816 */
#else
/* function setjmp: 6A14 */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_sfi_get_current_frame: 27BF */
#else
/* function ia_css_pipeline_sp_sfi_get_current_frame: 2790 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_sp2host_isys_event_queue
#define HIVE_MEM_sem_for_sp2host_isys_event_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_sp2host_isys_event_queue 0x5760
#else
#define HIVE_ADDR_sem_for_sp2host_isys_event_queue 0x57FC
#endif
#define HIVE_SIZE_sem_for_sp2host_isys_event_queue 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_sp2host_isys_event_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_sp2host_isys_event_queue 0x5760
#else
#define HIVE_ADDR_sp_sem_for_sp2host_isys_event_queue 0x57FC
#endif
#define HIVE_SIZE_sp_sem_for_sp2host_isys_event_queue 20

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_wait_for_ack: 6DA9 */
#else
/* function ia_css_dmaproxy_sp_wait_for_ack: 6FF7 */
#endif

#ifndef ISP2401
/* function ia_css_sp_rawcopy_func: 596B */
#else
/* function ia_css_sp_rawcopy_func: 5B4A */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_pop_marked: 3339 */
#else
/* function ia_css_tagger_buf_sp_pop_marked: 345C */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_N_CSI_RX_BE_SID_WIDTH
#define HIVE_MEM_N_CSI_RX_BE_SID_WIDTH scalar_processor_2400_dmem
#define HIVE_ADDR_N_CSI_RX_BE_SID_WIDTH 0x1D0
#define HIVE_SIZE_N_CSI_RX_BE_SID_WIDTH 12
#else
#endif
#endif
#define HIVE_MEM_sp_N_CSI_RX_BE_SID_WIDTH scalar_processor_2400_dmem
#define HIVE_ADDR_sp_N_CSI_RX_BE_SID_WIDTH 0x1D0
#define HIVE_SIZE_sp_N_CSI_RX_BE_SID_WIDTH 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_stage
#define HIVE_MEM_isp_stage scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isp_stage 0x6C98
#else
#define HIVE_ADDR_isp_stage 0x6D48
#endif
#define HIVE_SIZE_isp_stage 832
#else
#endif
#endif
#define HIVE_MEM_sp_isp_stage scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_stage 0x6C98
#else
#define HIVE_ADDR_sp_isp_stage 0x6D48
#endif
#define HIVE_SIZE_sp_isp_stage 832

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_raw
#define HIVE_MEM_vbuf_raw scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_vbuf_raw 0x37C
#else
#define HIVE_ADDR_vbuf_raw 0x394
#endif
#define HIVE_SIZE_vbuf_raw 4
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_raw scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_vbuf_raw 0x37C
#else
#define HIVE_ADDR_sp_vbuf_raw 0x394
#endif
#define HIVE_SIZE_sp_vbuf_raw 4

#ifndef ISP2401
/* function ia_css_sp_bin_copy_func: 594C */
#else
/* function ia_css_sp_bin_copy_func: 5B2B */
#endif

#ifndef ISP2401
/* function ia_css_queue_item_store: 554E */
#else
/* function ia_css_queue_item_store: 572D */
#endif

#ifndef ISP2401
/* function input_system_reset: 1286 */
#else
/* function input_system_reset: 1201 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_metadata_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_metadata_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_metadata_bufs 0x5B38
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_metadata_bufs 0x5BE4
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_metadata_bufs 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_metadata_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_metadata_bufs 0x5B38
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_metadata_bufs 0x5BE4
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_metadata_bufs 20

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_buffer_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_buffer_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_buffer_bufs 0x5B4C
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_buffer_bufs 0x5BF8
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_buffer_bufs 160
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_buffer_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_buffer_bufs 0x5B4C
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_buffer_bufs 0x5BF8
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_buffer_bufs 160

/* function sp_start_isp: 39C */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_binary_group
#define HIVE_MEM_sp_binary_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_binary_group 0x7088
#else
#define HIVE_ADDR_sp_binary_group 0x7138
#endif
#define HIVE_SIZE_sp_binary_group 32
#else
#endif
#endif
#define HIVE_MEM_sp_sp_binary_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_binary_group 0x7088
#else
#define HIVE_ADDR_sp_sp_binary_group 0x7138
#endif
#define HIVE_SIZE_sp_sp_binary_group 32

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_sw_state
#define HIVE_MEM_sp_sw_state scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sw_state 0x7344
#else
#define HIVE_ADDR_sp_sw_state 0x73F0
#endif
#define HIVE_SIZE_sp_sw_state 4
#else
#endif
#endif
#define HIVE_MEM_sp_sp_sw_state scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_sw_state 0x7344
#else
#define HIVE_ADDR_sp_sp_sw_state 0x73F0
#endif
#define HIVE_SIZE_sp_sp_sw_state 4

#ifndef ISP2401
/* function ia_css_thread_sp_main: 13F7 */
#else
/* function ia_css_thread_sp_main: 136D */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_init_internal_buffers: 4047 */
#else
/* function ia_css_ispctrl_sp_init_internal_buffers: 41F7 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp2host_psys_event_queue_handle
#define HIVE_MEM_sp2host_psys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp2host_psys_event_queue_handle 0x5BEC
#else
#define HIVE_ADDR_sp2host_psys_event_queue_handle 0x5C98
#endif
#define HIVE_SIZE_sp2host_psys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_sp2host_psys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp2host_psys_event_queue_handle 0x5BEC
#else
#define HIVE_ADDR_sp_sp2host_psys_event_queue_handle 0x5C98
#endif
#define HIVE_SIZE_sp_sp2host_psys_event_queue_handle 12

#ifndef ISP2401
/* function pixelgen_unit_test: E68 */
#else
/* function pixelgen_unit_test: E62 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_sp2host_psys_event_queue
#define HIVE_MEM_sem_for_sp2host_psys_event_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_sp2host_psys_event_queue 0x5774
#else
#define HIVE_ADDR_sem_for_sp2host_psys_event_queue 0x5810
#endif
#define HIVE_SIZE_sem_for_sp2host_psys_event_queue 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_sp2host_psys_event_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_sp2host_psys_event_queue 0x5774
#else
#define HIVE_ADDR_sp_sem_for_sp2host_psys_event_queue 0x5810
#endif
#define HIVE_SIZE_sp_sem_for_sp2host_psys_event_queue 20

#ifndef ISP2401
/* function ia_css_tagger_sp_propagate_frame: 2D52 */

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_stop_copy_preview
#define HIVE_MEM_sp_stop_copy_preview scalar_processor_2400_dmem
#define HIVE_ADDR_sp_stop_copy_preview 0x7328
#define HIVE_SIZE_sp_stop_copy_preview 4
#else
#endif
#endif
#define HIVE_MEM_sp_sp_stop_copy_preview scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_stop_copy_preview 0x7328
#define HIVE_SIZE_sp_sp_stop_copy_preview 4
#else
/* function ia_css_tagger_sp_propagate_frame: 2D23 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_handles
#define HIVE_MEM_vbuf_handles scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_vbuf_handles 0x73A4
#else
#define HIVE_ADDR_vbuf_handles 0x7450
#endif
#define HIVE_SIZE_vbuf_handles 960
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_handles scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_vbuf_handles 0x73A4
#else
#define HIVE_ADDR_sp_vbuf_handles 0x7450
#endif
#define HIVE_SIZE_sp_vbuf_handles 960

#ifndef ISP2401
/* function ia_css_queue_store: 56B4 */

/* function ia_css_sp_flash_register: 356E */
#else
/* function ia_css_queue_store: 5893 */
#endif

#ifndef ISP2401
/* function ia_css_sp_rawcopy_dummy_function: 5CF7 */
#else
/* function ia_css_sp_flash_register: 3691 */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_init: 201C */
#else
/* function ia_css_pipeline_sp_init: 1FD7 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_configure: 2C42 */
#else
/* function ia_css_tagger_sp_configure: 2C13 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_end_binary: 3E8A */
#else
/* function ia_css_ispctrl_sp_end_binary: 3FFF */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs
#define HIVE_MEM_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 0x5BF8
#else
#define HIVE_ADDR_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 0x5CA4
#endif
#define HIVE_SIZE_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 0x5BF8
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 0x5CA4
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_h_pipe_private_per_frame_ddr_ptrs 20

#ifndef ISP2401
/* function pixelgen_tpg_run: F1E */
#else
/* function pixelgen_tpg_run: F18 */
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
#define HIVE_ADDR_sp_all_cb_elems_frame 0x5788
#else
#define HIVE_ADDR_sp_all_cb_elems_frame 0x5824
#endif
#define HIVE_SIZE_sp_all_cb_elems_frame 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cb_elems_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cb_elems_frame 0x5788
#else
#define HIVE_ADDR_sp_sp_all_cb_elems_frame 0x5824
#endif
#define HIVE_SIZE_sp_sp_all_cb_elems_frame 16

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp2host_isys_event_queue_handle
#define HIVE_MEM_sp2host_isys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp2host_isys_event_queue_handle 0x5C0C
#else
#define HIVE_ADDR_sp2host_isys_event_queue_handle 0x5CB8
#endif
#define HIVE_SIZE_sp2host_isys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_sp2host_isys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp2host_isys_event_queue_handle 0x5C0C
#else
#define HIVE_ADDR_sp_sp2host_isys_event_queue_handle 0x5CB8
#endif
#define HIVE_SIZE_sp_sp2host_isys_event_queue_handle 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host_sp_com
#define HIVE_MEM_host_sp_com scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host_sp_com 0x3E48
#else
#define HIVE_ADDR_host_sp_com 0x3E6C
#endif
#define HIVE_SIZE_host_sp_com 220
#else
#endif
#endif
#define HIVE_MEM_sp_host_sp_com scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host_sp_com 0x3E48
#else
#define HIVE_ADDR_sp_host_sp_com 0x3E6C
#endif
#define HIVE_SIZE_sp_host_sp_com 220

#ifndef ISP2401
/* function ia_css_queue_get_free_space: 5313 */
#else
/* function ia_css_queue_get_free_space: 54F2 */
#endif

#ifndef ISP2401
/* function exec_image_pipe: 5E6 */
#else
/* function exec_image_pipe: 57A */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_init_dmem_data
#define HIVE_MEM_sp_init_dmem_data scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_init_dmem_data 0x7348
#else
#define HIVE_ADDR_sp_init_dmem_data 0x73F4
#endif
#define HIVE_SIZE_sp_init_dmem_data 24
#else
#endif
#endif
#define HIVE_MEM_sp_sp_init_dmem_data scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_init_dmem_data 0x7348
#else
#define HIVE_ADDR_sp_sp_init_dmem_data 0x73F4
#endif
#define HIVE_SIZE_sp_sp_init_dmem_data 24

#ifndef ISP2401
/* function ia_css_sp_metadata_start: 5DD1 */
#else
/* function ia_css_sp_metadata_start: 5EB3 */
#endif

#ifndef ISP2401
/* function ia_css_bufq_sp_init_buffer_queues: 35BF */
#else
/* function ia_css_bufq_sp_init_buffer_queues: 36E2 */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_stop: 1FFF */
#else
/* function ia_css_pipeline_sp_stop: 1FBA */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_connect_pipes: 312C */
#else
/* function ia_css_tagger_sp_connect_pipes: 30FD */
#endif

#ifndef ISP2401
/* function sp_isys_copy_wait: 644 */
#else
/* function sp_isys_copy_wait: 5D8 */
#endif

/* function is_isp_debug_buffer_full: 337 */

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_configure_channel_from_info: 3BD3 */
#else
/* function ia_css_dmaproxy_sp_configure_channel_from_info: 3D35 */
#endif

#ifndef ISP2401
/* function encode_and_post_timer_event: AA8 */
#else
/* function encode_and_post_timer_event: A3C */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_input_system_bz2788_active
#define HIVE_MEM_input_system_bz2788_active scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_input_system_bz2788_active 0x250C
#else
#define HIVE_ADDR_input_system_bz2788_active 0x2524
#endif
#define HIVE_SIZE_input_system_bz2788_active 4
#else
#endif
#endif
#define HIVE_MEM_sp_input_system_bz2788_active scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_input_system_bz2788_active 0x250C
#else
#define HIVE_ADDR_sp_input_system_bz2788_active 0x2524
#endif
#define HIVE_SIZE_sp_input_system_bz2788_active 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_N_IBUF_CTRL_PROCS
#define HIVE_MEM_N_IBUF_CTRL_PROCS scalar_processor_2400_dmem
#define HIVE_ADDR_N_IBUF_CTRL_PROCS 0x1FC
#define HIVE_SIZE_N_IBUF_CTRL_PROCS 12
#else
#endif
#endif
#define HIVE_MEM_sp_N_IBUF_CTRL_PROCS scalar_processor_2400_dmem
#define HIVE_ADDR_sp_N_IBUF_CTRL_PROCS 0x1FC
#define HIVE_SIZE_sp_N_IBUF_CTRL_PROCS 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_per_frame_data
#define HIVE_MEM_sp_per_frame_data scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_per_frame_data 0x3F24
#else
#define HIVE_ADDR_sp_per_frame_data 0x3F48
#endif
#define HIVE_SIZE_sp_per_frame_data 4
#else
#endif
#endif
#define HIVE_MEM_sp_sp_per_frame_data scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_per_frame_data 0x3F24
#else
#define HIVE_ADDR_sp_sp_per_frame_data 0x3F48
#endif
#define HIVE_SIZE_sp_sp_per_frame_data 4

#ifndef ISP2401
/* function ia_css_rmgr_sp_vbuf_dequeue: 62AC */
#else
/* function ia_css_rmgr_sp_vbuf_dequeue: 6472 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_psys_event_queue_handle
#define HIVE_MEM_host2sp_psys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host2sp_psys_event_queue_handle 0x5C18
#else
#define HIVE_ADDR_host2sp_psys_event_queue_handle 0x5CC4
#endif
#define HIVE_SIZE_host2sp_psys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_psys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host2sp_psys_event_queue_handle 0x5C18
#else
#define HIVE_ADDR_sp_host2sp_psys_event_queue_handle 0x5CC4
#endif
#define HIVE_SIZE_sp_host2sp_psys_event_queue_handle 12

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_xmem_bin_addr
#define HIVE_MEM_xmem_bin_addr scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_xmem_bin_addr 0x3F28
#else
#define HIVE_ADDR_xmem_bin_addr 0x3F4C
#endif
#define HIVE_SIZE_xmem_bin_addr 4
#else
#endif
#endif
#define HIVE_MEM_sp_xmem_bin_addr scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_xmem_bin_addr 0x3F28
#else
#define HIVE_ADDR_sp_xmem_bin_addr 0x3F4C
#endif
#define HIVE_SIZE_sp_xmem_bin_addr 4

#ifndef ISP2401
/* function tmr_clock_init: 16F9 */
#else
/* function tmr_clock_init: 166F */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_run: 1ABF */
#else
/* function ia_css_pipeline_sp_run: 1A61 */
#endif

#ifndef ISP2401
/* function memcpy: 68B6 */
#else
/* function memcpy: 6AB4 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_N_ISYS2401_DMA_CHANNEL_PROCS
#define HIVE_MEM_N_ISYS2401_DMA_CHANNEL_PROCS scalar_processor_2400_dmem
#define HIVE_ADDR_N_ISYS2401_DMA_CHANNEL_PROCS 0x214
#define HIVE_SIZE_N_ISYS2401_DMA_CHANNEL_PROCS 4
#else
#endif
#endif
#define HIVE_MEM_sp_N_ISYS2401_DMA_CHANNEL_PROCS scalar_processor_2400_dmem
#define HIVE_ADDR_sp_N_ISYS2401_DMA_CHANNEL_PROCS 0x214
#define HIVE_SIZE_sp_N_ISYS2401_DMA_CHANNEL_PROCS 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_GP_DEVICE_BASE
#define HIVE_MEM_GP_DEVICE_BASE scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_GP_DEVICE_BASE 0x384
#else
#define HIVE_ADDR_GP_DEVICE_BASE 0x39C
#endif
#define HIVE_SIZE_GP_DEVICE_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_GP_DEVICE_BASE scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_GP_DEVICE_BASE 0x384
#else
#define HIVE_ADDR_sp_GP_DEVICE_BASE 0x39C
#endif
#define HIVE_SIZE_sp_GP_DEVICE_BASE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_thread_sp_ready_queue
#define HIVE_MEM_ia_css_thread_sp_ready_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_thread_sp_ready_queue 0x278
#else
#define HIVE_ADDR_ia_css_thread_sp_ready_queue 0x27C
#endif
#define HIVE_SIZE_ia_css_thread_sp_ready_queue 12
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_thread_sp_ready_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_thread_sp_ready_queue 0x278
#else
#define HIVE_ADDR_sp_ia_css_thread_sp_ready_queue 0x27C
#endif
#define HIVE_SIZE_sp_ia_css_thread_sp_ready_queue 12

#ifndef ISP2401
/* function stream2mmio_send_command: E0A */
#else
/* function stream2mmio_send_command: E04 */
#endif

#ifndef ISP2401
/* function ia_css_uds_sp_scale_params: 65BF */
#else
/* function ia_css_uds_sp_scale_params: 67BD */
#endif

#ifndef ISP2401
/* function ia_css_circbuf_increase_size: 14DC */
#else
/* function ia_css_circbuf_increase_size: 1452 */
#endif

#ifndef ISP2401
/* function __divu: 6834 */
#else
/* function __divu: 6A32 */
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_get_state: 131F */
#else
/* function ia_css_thread_sp_get_state: 1295 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_cont_capt_stop
#define HIVE_MEM_sem_for_cont_capt_stop scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_cont_capt_stop 0x5798
#else
#define HIVE_ADDR_sem_for_cont_capt_stop 0x5834
#endif
#define HIVE_SIZE_sem_for_cont_capt_stop 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_cont_capt_stop scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_cont_capt_stop 0x5798
#else
#define HIVE_ADDR_sp_sem_for_cont_capt_stop 0x5834
#endif
#define HIVE_SIZE_sp_sem_for_cont_capt_stop 20

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_N_SHORT_PACKET_LUT_ENTRIES
#define HIVE_MEM_N_SHORT_PACKET_LUT_ENTRIES scalar_processor_2400_dmem
#define HIVE_ADDR_N_SHORT_PACKET_LUT_ENTRIES 0x1AC
#define HIVE_SIZE_N_SHORT_PACKET_LUT_ENTRIES 12
#else
#endif
#endif
#define HIVE_MEM_sp_N_SHORT_PACKET_LUT_ENTRIES scalar_processor_2400_dmem
#define HIVE_ADDR_sp_N_SHORT_PACKET_LUT_ENTRIES 0x1AC
#define HIVE_SIZE_sp_N_SHORT_PACKET_LUT_ENTRIES 12

#ifndef ISP2401
/* function thread_fiber_sp_main: 14D5 */
#else
/* function thread_fiber_sp_main: 144B */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_isp_pipe_thread
#define HIVE_MEM_sp_isp_pipe_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_pipe_thread 0x58DC
#define HIVE_SIZE_sp_isp_pipe_thread 340
#else
#define HIVE_ADDR_sp_isp_pipe_thread 0x5978
#define HIVE_SIZE_sp_isp_pipe_thread 360
#endif
#else
#endif
#endif
#define HIVE_MEM_sp_sp_isp_pipe_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_isp_pipe_thread 0x58DC
#define HIVE_SIZE_sp_sp_isp_pipe_thread 340
#else
#define HIVE_ADDR_sp_sp_isp_pipe_thread 0x5978
#define HIVE_SIZE_sp_sp_isp_pipe_thread 360
#endif

#ifndef ISP2401
/* function ia_css_parambuf_sp_handle_parameter_sets: 193F */
#else
/* function ia_css_parambuf_sp_handle_parameter_sets: 18B5 */
#endif

#ifndef ISP2401
/* function ia_css_spctrl_sp_set_state: 5DED */
#else
/* function ia_css_spctrl_sp_set_state: 5ECF */
#endif

#ifndef ISP2401
/* function ia_css_thread_sem_sp_signal: 6A99 */
#else
/* function ia_css_thread_sem_sp_signal: 6D18 */
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

#ifndef ISP2401
/* function ia_css_virtual_isys_sp_isr_init: 5E8C */
#else
/* function ia_css_virtual_isys_sp_isr_init: 5F70 */
#endif

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
/* function ia_css_isys_sp_generate_exp_id: 613C */

/* function ia_css_rmgr_sp_init: 61A7 */
#else
/* function ia_css_isys_sp_generate_exp_id: 6302 */
#endif

#ifndef ISP2401
/* function ia_css_thread_sem_sp_init: 6B6A */
#else
/* function ia_css_rmgr_sp_init: 636D */
#endif

#ifndef ISP2401
#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_is_isp_requested
#define HIVE_MEM_is_isp_requested scalar_processor_2400_dmem
#define HIVE_ADDR_is_isp_requested 0x390
#define HIVE_SIZE_is_isp_requested 4
#else
#endif
#endif
#define HIVE_MEM_sp_is_isp_requested scalar_processor_2400_dmem
#define HIVE_ADDR_sp_is_isp_requested 0x390
#define HIVE_SIZE_sp_is_isp_requested 4
#else
/* function ia_css_thread_sem_sp_init: 6DE7 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_reading_cb_frame
#define HIVE_MEM_sem_for_reading_cb_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_reading_cb_frame 0x57AC
#else
#define HIVE_ADDR_sem_for_reading_cb_frame 0x5848
#endif
#define HIVE_SIZE_sem_for_reading_cb_frame 40
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_reading_cb_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_reading_cb_frame 0x57AC
#else
#define HIVE_ADDR_sp_sem_for_reading_cb_frame 0x5848
#endif
#define HIVE_SIZE_sp_sem_for_reading_cb_frame 40

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_execute: 3B3B */
#else
#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_is_isp_requested
#define HIVE_MEM_is_isp_requested scalar_processor_2400_dmem
#define HIVE_ADDR_is_isp_requested 0x3A8
#define HIVE_SIZE_is_isp_requested 4
#else
#endif
#endif
#define HIVE_MEM_sp_is_isp_requested scalar_processor_2400_dmem
#define HIVE_ADDR_sp_is_isp_requested 0x3A8
#define HIVE_SIZE_sp_is_isp_requested 4

/* function ia_css_dmaproxy_sp_execute: 3C9B */
#endif

#ifndef ISP2401
/* function csi_rx_backend_rst: CE6 */
#else
/* function csi_rx_backend_rst: CE0 */
#endif

#ifndef ISP2401
/* function ia_css_queue_is_empty: 51FA */
#else
/* function ia_css_queue_is_empty: 7144 */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_has_stopped: 1FF5 */
#else
/* function ia_css_pipeline_sp_has_stopped: 1FB0 */
#endif

#ifndef ISP2401
/* function ia_css_circbuf_extract: 15E0 */
#else
/* function ia_css_circbuf_extract: 1556 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_is_locked_from_start: 344F */
#else
/* function ia_css_tagger_buf_sp_is_locked_from_start: 3572 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_current_sp_thread
#define HIVE_MEM_current_sp_thread scalar_processor_2400_dmem
#define HIVE_ADDR_current_sp_thread 0x274
#define HIVE_SIZE_current_sp_thread 4
#else
#endif
#endif
#define HIVE_MEM_sp_current_sp_thread scalar_processor_2400_dmem
#define HIVE_ADDR_sp_current_sp_thread 0x274
#define HIVE_SIZE_sp_current_sp_thread 4

#ifndef ISP2401
/* function ia_css_spctrl_sp_get_spid: 5DF4 */
#else
/* function ia_css_spctrl_sp_get_spid: 5ED6 */
#endif

#ifndef ISP2401
/* function ia_css_bufq_sp_reset_buffers: 3646 */
#else
/* function ia_css_bufq_sp_reset_buffers: 3769 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_read_byte_addr: 6DD7 */
#else
/* function ia_css_dmaproxy_sp_read_byte_addr: 7025 */
#endif

#ifndef ISP2401
/* function ia_css_rmgr_sp_uninit: 61A0 */
#else
/* function ia_css_rmgr_sp_uninit: 6366 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_threads_stack
#define HIVE_MEM_sp_threads_stack scalar_processor_2400_dmem
#define HIVE_ADDR_sp_threads_stack 0x164
#define HIVE_SIZE_sp_threads_stack 24
#else
#endif
#endif
#define HIVE_MEM_sp_sp_threads_stack scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_threads_stack 0x164
#define HIVE_SIZE_sp_sp_threads_stack 24

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_N_STREAM2MMIO_SID_PROCS
#define HIVE_MEM_N_STREAM2MMIO_SID_PROCS scalar_processor_2400_dmem
#define HIVE_ADDR_N_STREAM2MMIO_SID_PROCS 0x218
#define HIVE_SIZE_N_STREAM2MMIO_SID_PROCS 12
#else
#endif
#endif
#define HIVE_MEM_sp_N_STREAM2MMIO_SID_PROCS scalar_processor_2400_dmem
#define HIVE_ADDR_sp_N_STREAM2MMIO_SID_PROCS 0x218
#define HIVE_SIZE_sp_N_STREAM2MMIO_SID_PROCS 12

#ifndef ISP2401
/* function ia_css_circbuf_peek: 15C2 */
#else
/* function ia_css_circbuf_peek: 1538 */
#endif

#ifndef ISP2401
/* function ia_css_parambuf_sp_wait_for_in_param: 1708 */
#else
/* function ia_css_parambuf_sp_wait_for_in_param: 167E */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cb_elems_param
#define HIVE_MEM_sp_all_cb_elems_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_all_cb_elems_param 0x57D4
#else
#define HIVE_ADDR_sp_all_cb_elems_param 0x5870
#endif
#define HIVE_SIZE_sp_all_cb_elems_param 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cb_elems_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cb_elems_param 0x57D4
#else
#define HIVE_ADDR_sp_sp_all_cb_elems_param 0x5870
#endif
#define HIVE_SIZE_sp_sp_all_cb_elems_param 16

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_pipeline_sp_curr_binary_id
#define HIVE_MEM_pipeline_sp_curr_binary_id scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_pipeline_sp_curr_binary_id 0x284
#else
#define HIVE_ADDR_pipeline_sp_curr_binary_id 0x288
#endif
#define HIVE_SIZE_pipeline_sp_curr_binary_id 4
#else
#endif
#endif
#define HIVE_MEM_sp_pipeline_sp_curr_binary_id scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_pipeline_sp_curr_binary_id 0x284
#else
#define HIVE_ADDR_sp_pipeline_sp_curr_binary_id 0x288
#endif
#define HIVE_SIZE_sp_pipeline_sp_curr_binary_id 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_frame_desc
#define HIVE_MEM_sp_all_cbs_frame_desc scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_all_cbs_frame_desc 0x57E4
#else
#define HIVE_ADDR_sp_all_cbs_frame_desc 0x5880
#endif
#define HIVE_SIZE_sp_all_cbs_frame_desc 8
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_frame_desc scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cbs_frame_desc 0x57E4
#else
#define HIVE_ADDR_sp_sp_all_cbs_frame_desc 0x5880
#endif
#define HIVE_SIZE_sp_sp_all_cbs_frame_desc 8

#ifndef ISP2401
/* function sp_isys_copy_func_v2: 629 */
#else
/* function sp_isys_copy_func_v2: 5BD */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_reading_cb_param
#define HIVE_MEM_sem_for_reading_cb_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_reading_cb_param 0x57EC
#else
#define HIVE_ADDR_sem_for_reading_cb_param 0x5888
#endif
#define HIVE_SIZE_sem_for_reading_cb_param 40
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_reading_cb_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_reading_cb_param 0x57EC
#else
#define HIVE_ADDR_sp_sem_for_reading_cb_param 0x5888
#endif
#define HIVE_SIZE_sp_sem_for_reading_cb_param 40

#ifndef ISP2401
/* function ia_css_queue_get_used_space: 52C7 */
#else
/* function ia_css_queue_get_used_space: 54A6 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_cont_capt_start
#define HIVE_MEM_sem_for_cont_capt_start scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_cont_capt_start 0x5814
#else
#define HIVE_ADDR_sem_for_cont_capt_start 0x58B0
#endif
#define HIVE_SIZE_sem_for_cont_capt_start 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_cont_capt_start scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_cont_capt_start 0x5814
#else
#define HIVE_ADDR_sp_sem_for_cont_capt_start 0x58B0
#endif
#define HIVE_SIZE_sp_sem_for_cont_capt_start 20

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_tmp_heap
#define HIVE_MEM_tmp_heap scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_tmp_heap 0x70A8
#else
#define HIVE_ADDR_tmp_heap 0x7158
#endif
#define HIVE_SIZE_tmp_heap 640
#else
#endif
#endif
#define HIVE_MEM_sp_tmp_heap scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_tmp_heap 0x70A8
#else
#define HIVE_ADDR_sp_tmp_heap 0x7158
#endif
#define HIVE_SIZE_sp_tmp_heap 640

#ifndef ISP2401
/* function ia_css_rmgr_sp_get_num_vbuf: 64B0 */
#else
/* function ia_css_rmgr_sp_get_num_vbuf: 6676 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_output_compute_dma_info: 4863 */
#else
/* function ia_css_ispctrl_sp_output_compute_dma_info: 4A27 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_lock_exp_id: 2A0F */
#else
/* function ia_css_tagger_sp_lock_exp_id: 29E0 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_s3a_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_s3a_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_s3a_bufs 0x5C24
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_s3a_bufs 0x5CD0
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_s3a_bufs 60
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_s3a_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_s3a_bufs 0x5C24
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_s3a_bufs 0x5CD0
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_s3a_bufs 60

#ifndef ISP2401
/* function ia_css_queue_is_full: 535E */
#else
/* function ia_css_queue_is_full: 553D */
#endif

/* function debug_buffer_init_isp: E4 */

#ifndef ISP2401
/* function ia_css_tagger_sp_exp_id_is_locked: 2945 */
#else
/* function ia_css_tagger_sp_exp_id_is_locked: 2916 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_rmgr_sp_mipi_frame_sem
#define HIVE_MEM_ia_css_rmgr_sp_mipi_frame_sem scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_rmgr_sp_mipi_frame_sem 0x7764
#else
#define HIVE_ADDR_ia_css_rmgr_sp_mipi_frame_sem 0x7810
#endif
#define HIVE_SIZE_ia_css_rmgr_sp_mipi_frame_sem 60
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_rmgr_sp_mipi_frame_sem scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_rmgr_sp_mipi_frame_sem 0x7764
#else
#define HIVE_ADDR_sp_ia_css_rmgr_sp_mipi_frame_sem 0x7810
#endif
#define HIVE_SIZE_sp_ia_css_rmgr_sp_mipi_frame_sem 60

#ifndef ISP2401
/* function ia_css_rmgr_sp_refcount_dump: 6287 */
#else
/* function ia_css_rmgr_sp_refcount_dump: 644D */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_isp_parameters_id
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_isp_parameters_id scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_isp_parameters_id 0x5C60
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_isp_parameters_id 0x5D0C
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_isp_parameters_id 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_isp_parameters_id scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_isp_parameters_id 0x5C60
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_isp_parameters_id 0x5D0C
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
/* function sp_event_proxy_func: 78D */
#else
/* function sp_event_proxy_func: 721 */
#endif

#ifndef ISP2401
/* function ibuf_ctrl_run: D7F */
#else
/* function ibuf_ctrl_run: D79 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_isys_event_queue_handle
#define HIVE_MEM_host2sp_isys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host2sp_isys_event_queue_handle 0x5C74
#else
#define HIVE_ADDR_host2sp_isys_event_queue_handle 0x5D20
#endif
#define HIVE_SIZE_host2sp_isys_event_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_isys_event_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host2sp_isys_event_queue_handle 0x5C74
#else
#define HIVE_ADDR_sp_host2sp_isys_event_queue_handle 0x5D20
#endif
#define HIVE_SIZE_sp_host2sp_isys_event_queue_handle 12

#ifndef ISP2401
/* function ia_css_thread_sp_yield: 6A12 */
#else
/* function ia_css_thread_sp_yield: 6C96 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_param_desc
#define HIVE_MEM_sp_all_cbs_param_desc scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_all_cbs_param_desc 0x5828
#else
#define HIVE_ADDR_sp_all_cbs_param_desc 0x58C4
#endif
#define HIVE_SIZE_sp_all_cbs_param_desc 8
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_param_desc scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cbs_param_desc 0x5828
#else
#define HIVE_ADDR_sp_sp_all_cbs_param_desc 0x58C4
#endif
#define HIVE_SIZE_sp_sp_all_cbs_param_desc 8

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_dmaproxy_sp_invalidate_tlb
#define HIVE_MEM_ia_css_dmaproxy_sp_invalidate_tlb scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_dmaproxy_sp_invalidate_tlb 0x6C8C
#else
#define HIVE_ADDR_ia_css_dmaproxy_sp_invalidate_tlb 0x6D38
#endif
#define HIVE_SIZE_ia_css_dmaproxy_sp_invalidate_tlb 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_dmaproxy_sp_invalidate_tlb scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_dmaproxy_sp_invalidate_tlb 0x6C8C
#else
#define HIVE_ADDR_sp_ia_css_dmaproxy_sp_invalidate_tlb 0x6D38
#endif
#define HIVE_SIZE_sp_ia_css_dmaproxy_sp_invalidate_tlb 4

#ifndef ISP2401
/* function ia_css_thread_sp_fork: 13AC */
#else
/* function ia_css_thread_sp_fork: 1322 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_destroy: 3136 */
#else
/* function ia_css_tagger_sp_destroy: 3107 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_vmem_read: 3ADB */
#else
/* function ia_css_dmaproxy_sp_vmem_read: 3C3B */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_N_LONG_PACKET_LUT_ENTRIES
#define HIVE_MEM_N_LONG_PACKET_LUT_ENTRIES scalar_processor_2400_dmem
#define HIVE_ADDR_N_LONG_PACKET_LUT_ENTRIES 0x1B8
#define HIVE_SIZE_N_LONG_PACKET_LUT_ENTRIES 12
#else
#endif
#endif
#define HIVE_MEM_sp_N_LONG_PACKET_LUT_ENTRIES scalar_processor_2400_dmem
#define HIVE_ADDR_sp_N_LONG_PACKET_LUT_ENTRIES 0x1B8
#define HIVE_SIZE_sp_N_LONG_PACKET_LUT_ENTRIES 12

#ifndef ISP2401
/* function initialize_sp_group: 5F6 */
#else
/* function initialize_sp_group: 58A */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_peek: 325B */
#else
/* function ia_css_tagger_buf_sp_peek: 337E */
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_init: 13D8 */
#else
/* function ia_css_thread_sp_init: 134E */
#endif

#ifndef ISP2401
/* function ia_css_isys_sp_reset_exp_id: 6133 */
#else
/* function qos_scheduler_update_fps: 67AD */
#endif

#ifndef ISP2401
/* function qos_scheduler_update_fps: 65AF */
#else
/* function ia_css_isys_sp_reset_exp_id: 62F9 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_set_stream_base_addr: 4F38 */
#else
/* function ia_css_ispctrl_sp_set_stream_base_addr: 5114 */
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
/* function ibuf_ctrl_transfer: D67 */
#else
/* function ibuf_ctrl_transfer: D61 */

/* function __ia_css_queue_is_empty_text: 5403 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_read: 3B51 */
#else
/* function ia_css_dmaproxy_sp_read: 3CB1 */
#endif

#ifndef ISP2401
/* function virtual_isys_stream_is_capture_done: 5EB0 */
#else
/* function virtual_isys_stream_is_capture_done: 5F94 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_raw_copy_line_count
#define HIVE_MEM_raw_copy_line_count scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_raw_copy_line_count 0x360
#else
#define HIVE_ADDR_raw_copy_line_count 0x378
#endif
#define HIVE_SIZE_raw_copy_line_count 4
#else
#endif
#endif
#define HIVE_MEM_sp_raw_copy_line_count scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_raw_copy_line_count 0x360
#else
#define HIVE_ADDR_sp_raw_copy_line_count 0x378
#endif
#define HIVE_SIZE_sp_raw_copy_line_count 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_tag_cmd_queue_handle
#define HIVE_MEM_host2sp_tag_cmd_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host2sp_tag_cmd_queue_handle 0x5C80
#else
#define HIVE_ADDR_host2sp_tag_cmd_queue_handle 0x5D2C
#endif
#define HIVE_SIZE_host2sp_tag_cmd_queue_handle 12
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_tag_cmd_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host2sp_tag_cmd_queue_handle 0x5C80
#else
#define HIVE_ADDR_sp_host2sp_tag_cmd_queue_handle 0x5D2C
#endif
#define HIVE_SIZE_sp_host2sp_tag_cmd_queue_handle 12

#ifndef ISP2401
/* function ia_css_queue_peek: 523D */
#else
/* function ia_css_queue_peek: 541C */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_frame_cnt
#define HIVE_MEM_ia_css_flash_sp_frame_cnt scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_flash_sp_frame_cnt 0x5B2C
#else
#define HIVE_ADDR_ia_css_flash_sp_frame_cnt 0x5BD8
#endif
#define HIVE_SIZE_ia_css_flash_sp_frame_cnt 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_frame_cnt scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_flash_sp_frame_cnt 0x5B2C
#else
#define HIVE_ADDR_sp_ia_css_flash_sp_frame_cnt 0x5BD8
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

#ifndef ISP2401
/* function csi_rx_frontend_stop: C11 */
#else
/* function csi_rx_frontend_stop: C0B */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_thread
#define HIVE_MEM_isp_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isp_thread 0x6FD8
#else
#define HIVE_ADDR_isp_thread 0x7088
#endif
#define HIVE_SIZE_isp_thread 4
#else
#endif
#endif
#define HIVE_MEM_sp_isp_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_thread 0x6FD8
#else
#define HIVE_ADDR_sp_isp_thread 0x7088
#endif
#define HIVE_SIZE_sp_isp_thread 4

#ifndef ISP2401
/* function encode_and_post_sp_event_non_blocking: AF0 */
#else
/* function encode_and_post_sp_event_non_blocking: A84 */
#endif

/* function is_ddr_debug_buffer_full: 2CC */

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_get_oldest_marked_offset: 32AB */
#else
/* function ia_css_tagger_buf_sp_get_oldest_marked_offset: 33CE */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_threads_fiber
#define HIVE_MEM_sp_threads_fiber scalar_processor_2400_dmem
#define HIVE_ADDR_sp_threads_fiber 0x194
#define HIVE_SIZE_sp_threads_fiber 24
#else
#endif
#endif
#define HIVE_MEM_sp_sp_threads_fiber scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_threads_fiber 0x194
#define HIVE_SIZE_sp_sp_threads_fiber 24

#ifndef ISP2401
/* function encode_and_post_sp_event: A79 */
#else
/* function encode_and_post_sp_event: A0D */
#endif

/* function debug_enqueue_ddr: EE */

#ifndef ISP2401
/* function ia_css_rmgr_sp_refcount_init_vbuf: 6242 */
#else
/* function ia_css_rmgr_sp_refcount_init_vbuf: 6408 */
#endif

#ifndef ISP2401
/* function dmaproxy_sp_read_write: 6E86 */
#else
/* function dmaproxy_sp_read_write: 70C3 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_dmaproxy_isp_dma_cmd_buffer
#define HIVE_MEM_ia_css_dmaproxy_isp_dma_cmd_buffer scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_dmaproxy_isp_dma_cmd_buffer 0x6C90
#else
#define HIVE_ADDR_ia_css_dmaproxy_isp_dma_cmd_buffer 0x6D3C
#endif
#define HIVE_SIZE_ia_css_dmaproxy_isp_dma_cmd_buffer 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_dmaproxy_isp_dma_cmd_buffer scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_dmaproxy_isp_dma_cmd_buffer 0x6C90
#else
#define HIVE_ADDR_sp_ia_css_dmaproxy_isp_dma_cmd_buffer 0x6D3C
#endif
#define HIVE_SIZE_sp_ia_css_dmaproxy_isp_dma_cmd_buffer 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host2sp_buffer_queue_handle
#define HIVE_MEM_host2sp_buffer_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host2sp_buffer_queue_handle 0x5C8C
#else
#define HIVE_ADDR_host2sp_buffer_queue_handle 0x5D38
#endif
#define HIVE_SIZE_host2sp_buffer_queue_handle 480
#else
#endif
#endif
#define HIVE_MEM_sp_host2sp_buffer_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host2sp_buffer_queue_handle 0x5C8C
#else
#define HIVE_ADDR_sp_host2sp_buffer_queue_handle 0x5D38
#endif
#define HIVE_SIZE_sp_host2sp_buffer_queue_handle 480

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_in_service
#define HIVE_MEM_ia_css_flash_sp_in_service scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_flash_sp_in_service 0x3054
#else
#define HIVE_ADDR_ia_css_flash_sp_in_service 0x3074
#endif
#define HIVE_SIZE_ia_css_flash_sp_in_service 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_in_service scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_flash_sp_in_service 0x3054
#else
#define HIVE_ADDR_sp_ia_css_flash_sp_in_service 0x3074
#endif
#define HIVE_SIZE_sp_ia_css_flash_sp_in_service 4

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_process: 6B92 */
#else
/* function ia_css_dmaproxy_sp_process: 6E0F */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_mark_from_end: 3533 */
#else
/* function ia_css_tagger_buf_sp_mark_from_end: 3656 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_init_cs: 3F77 */
#else
/* function ia_css_ispctrl_sp_init_cs: 40FA */
#endif

#ifndef ISP2401
/* function ia_css_spctrl_sp_init: 5E02 */
#else
/* function ia_css_spctrl_sp_init: 5EE4 */
#endif

#ifndef ISP2401
/* function sp_event_proxy_init: 7A2 */
#else
/* function sp_event_proxy_init: 736 */
#endif

#ifndef ISP2401
/* function input_system_input_port_close: 109B */
#else
/* function input_system_input_port_close: 1095 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_previous_clock_tick
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_previous_clock_tick scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_previous_clock_tick 0x5E6C
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_previous_clock_tick 0x5F18
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_previous_clock_tick 40
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_previous_clock_tick scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_previous_clock_tick 0x5E6C
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_previous_clock_tick 0x5F18
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_previous_clock_tick 40

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_output
#define HIVE_MEM_sp_output scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_output 0x3F2C
#else
#define HIVE_ADDR_sp_output 0x3F50
#endif
#define HIVE_SIZE_sp_output 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_output scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_output 0x3F2C
#else
#define HIVE_ADDR_sp_sp_output 0x3F50
#endif
#define HIVE_SIZE_sp_sp_output 16

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_sems_for_host2sp_buf_queues
#define HIVE_MEM_ia_css_bufq_sp_sems_for_host2sp_buf_queues scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_sems_for_host2sp_buf_queues 0x5E94
#else
#define HIVE_ADDR_ia_css_bufq_sp_sems_for_host2sp_buf_queues 0x5F40
#endif
#define HIVE_SIZE_ia_css_bufq_sp_sems_for_host2sp_buf_queues 800
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_sems_for_host2sp_buf_queues scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_sems_for_host2sp_buf_queues 0x5E94
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_sems_for_host2sp_buf_queues 0x5F40
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_sems_for_host2sp_buf_queues 800

#ifndef ISP2401
/* function pixelgen_prbs_config: E93 */
#else
/* function pixelgen_prbs_config: E8D */
#endif

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
/* function sp_dma_proxy_reset_channels: 3DAB */
#else
/* function sp_dma_proxy_reset_channels: 3F20 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_update_size: 322A */
#else
/* function ia_css_tagger_sp_update_size: 334D */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_host_sp_queue
#define HIVE_MEM_ia_css_bufq_host_sp_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_host_sp_queue 0x61B4
#else
#define HIVE_ADDR_ia_css_bufq_host_sp_queue 0x6260
#endif
#define HIVE_SIZE_ia_css_bufq_host_sp_queue 2008
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_host_sp_queue scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_host_sp_queue 0x61B4
#else
#define HIVE_ADDR_sp_ia_css_bufq_host_sp_queue 0x6260
#endif
#define HIVE_SIZE_sp_ia_css_bufq_host_sp_queue 2008

#ifndef ISP2401
/* function thread_fiber_sp_create: 1444 */
#else
/* function thread_fiber_sp_create: 13BA */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_set_increments: 3C3D */
#else
/* function ia_css_dmaproxy_sp_set_increments: 3DB2 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_writing_cb_frame
#define HIVE_MEM_sem_for_writing_cb_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_writing_cb_frame 0x5830
#else
#define HIVE_ADDR_sem_for_writing_cb_frame 0x58CC
#endif
#define HIVE_SIZE_sem_for_writing_cb_frame 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_writing_cb_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_writing_cb_frame 0x5830
#else
#define HIVE_ADDR_sp_sem_for_writing_cb_frame 0x58CC
#endif
#define HIVE_SIZE_sp_sem_for_writing_cb_frame 20

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_writing_cb_param
#define HIVE_MEM_sem_for_writing_cb_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_writing_cb_param 0x5844
#else
#define HIVE_ADDR_sem_for_writing_cb_param 0x58E0
#endif
#define HIVE_SIZE_sem_for_writing_cb_param 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_writing_cb_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_writing_cb_param 0x5844
#else
#define HIVE_ADDR_sp_sem_for_writing_cb_param 0x58E0
#endif
#define HIVE_SIZE_sp_sem_for_writing_cb_param 20

#ifndef ISP2401
/* function pixelgen_tpg_is_done: F0D */
#else
/* function pixelgen_tpg_is_done: F07 */
#endif

#ifndef ISP2401
/* function ia_css_isys_stream_capture_indication: 5FB6 */
#else
/* function ia_css_isys_stream_capture_indication: 60D7 */
#endif

/* function sp_start_isp_entry: 392 */
#ifndef HIVE_MULTIPLE_PROGRAMS
#ifdef HIVE_ADDR_sp_start_isp_entry
#endif
#define HIVE_ADDR_sp_start_isp_entry 0x392
#endif
#define HIVE_ADDR_sp_sp_start_isp_entry 0x392

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_unmark_all: 34B7 */
#else
/* function ia_css_tagger_buf_sp_unmark_all: 35DA */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_unmark_from_start: 34F8 */
#else
/* function ia_css_tagger_buf_sp_unmark_from_start: 361B */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_channel_acquire: 3DD7 */
#else
/* function ia_css_dmaproxy_sp_channel_acquire: 3F4C */
#endif

#ifndef ISP2401
/* function ia_css_rmgr_sp_add_num_vbuf: 648C */
#else
/* function ia_css_rmgr_sp_add_num_vbuf: 6652 */
#endif

#ifndef ISP2401
/* function ibuf_ctrl_config: D8B */
#else
/* function ibuf_ctrl_config: D85 */
#endif

#ifndef ISP2401
/* function ia_css_isys_stream_stop: 602E */
#else
/* function ia_css_isys_stream_stop: 61F4 */
#endif

#ifndef ISP2401
/* function __ia_css_dmaproxy_sp_wait_for_ack_text: 3AA7 */
#else
/* function __ia_css_dmaproxy_sp_wait_for_ack_text: 3C07 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_acquire_buf_elem: 291D */
#else
/* function ia_css_tagger_sp_acquire_buf_elem: 28EE */
#endif

#ifndef ISP2401
/* function ia_css_bufq_sp_is_dynamic_buffer: 3990 */
#else
/* function ia_css_bufq_sp_is_dynamic_buffer: 3AB3 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_group
#define HIVE_MEM_sp_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_group 0x3F3C
#define HIVE_SIZE_sp_group 6176
#else
#define HIVE_ADDR_sp_group 0x3F60
#define HIVE_SIZE_sp_group 6296
#endif
#else
#endif
#endif
#define HIVE_MEM_sp_sp_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_group 0x3F3C
#define HIVE_SIZE_sp_sp_group 6176
#else
#define HIVE_ADDR_sp_sp_group 0x3F60
#define HIVE_SIZE_sp_sp_group 6296
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_event_proxy_thread
#define HIVE_MEM_sp_event_proxy_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_event_proxy_thread 0x5A30
#define HIVE_SIZE_sp_event_proxy_thread 68
#else
#define HIVE_ADDR_sp_event_proxy_thread 0x5AE0
#define HIVE_SIZE_sp_event_proxy_thread 72
#endif
#else
#endif
#endif
#define HIVE_MEM_sp_sp_event_proxy_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_event_proxy_thread 0x5A30
#define HIVE_SIZE_sp_sp_event_proxy_thread 68
#else
#define HIVE_ADDR_sp_sp_event_proxy_thread 0x5AE0
#define HIVE_SIZE_sp_sp_event_proxy_thread 72
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_kill: 1372 */
#else
/* function ia_css_thread_sp_kill: 12E8 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_create: 31E4 */
#else
/* function ia_css_tagger_sp_create: 32FB */
#endif

#ifndef ISP2401
/* function tmpmem_acquire_dmem: 6539 */
#else
/* function tmpmem_acquire_dmem: 66FF */
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
/* function ia_css_dmaproxy_sp_channel_release: 3DC3 */
#else
/* function ia_css_dmaproxy_sp_channel_release: 3F38 */
#endif

#ifndef ISP2401
/* function pixelgen_prbs_run: E81 */
#else
/* function pixelgen_prbs_run: E7B */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_is_idle: 3DA3 */
#else
/* function ia_css_dmaproxy_sp_is_idle: 3F18 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_qos_start
#define HIVE_MEM_sem_for_qos_start scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_qos_start 0x5858
#else
#define HIVE_ADDR_sem_for_qos_start 0x58F4
#endif
#define HIVE_SIZE_sem_for_qos_start 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_qos_start scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_qos_start 0x5858
#else
#define HIVE_ADDR_sp_sem_for_qos_start 0x58F4
#endif
#define HIVE_SIZE_sp_sem_for_qos_start 20

#ifndef ISP2401
/* function isp_hmem_load: B63 */
#else
/* function isp_hmem_load: B5D */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_release_buf_elem: 28F9 */
#else
/* function ia_css_tagger_sp_release_buf_elem: 28CA */
#endif

#ifndef ISP2401
/* function ia_css_eventq_sp_send: 3E19 */
#else
/* function ia_css_eventq_sp_send: 3F8E */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_unlock_from_start: 33E7 */
#else
/* function ia_css_tagger_buf_sp_unlock_from_start: 350A */
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
/* function sp_isys_copy_request: 6ED */
#else
/* function sp_isys_copy_request: 681 */
#endif

#ifndef ISP2401
/* function ia_css_rmgr_sp_refcount_retain_vbuf: 631C */
#else
/* function ia_css_rmgr_sp_refcount_retain_vbuf: 64E2 */
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_set_priority: 136A */
#else
/* function ia_css_thread_sp_set_priority: 12E0 */
#endif

#ifndef ISP2401
/* function sizeof_hmem: C0A */
#else
/* function sizeof_hmem: C04 */
#endif

#ifndef ISP2401
/* function input_system_channel_open: 1241 */
#else
/* function input_system_channel_open: 11BC */
#endif

#ifndef ISP2401
/* function pixelgen_tpg_stop: EFB */
#else
/* function pixelgen_tpg_stop: EF5 */
#endif

#ifndef ISP2401
/* function tmpmem_release_dmem: 6528 */
#else
/* function tmpmem_release_dmem: 66EE */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_set_width_exception: 3C28 */
#else
/* function __ia_css_dmaproxy_sp_process_text: 3BAB */
#endif

#ifndef ISP2401
/* function sp_event_assert: 929 */
#else
/* function ia_css_dmaproxy_sp_set_width_exception: 3D9D */
#endif

#ifndef ISP2401
/* function ia_css_flash_sp_init_internal_params: 35B4 */
#else
/* function sp_event_assert: 8BD */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_pop_unmarked_and_unlocked: 32ED */
#else
/* function ia_css_flash_sp_init_internal_params: 36D7 */
#endif

#ifndef ISP2401
/* function __modu: 687A */
#else
/* function ia_css_tagger_buf_sp_pop_unmarked_and_unlocked: 3410 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_init_isp_vector: 3AAD */
#else
/* function __modu: 6A78 */
#endif

#ifndef ISP2401
/* function input_system_channel_transfer: 122A */
#else
/* function ia_css_dmaproxy_sp_init_isp_vector: 3C0D */

/* function input_system_channel_transfer: 11A5 */
#endif

/* function isp_vamem_store: 0 */

#ifdef ISP2401
/* function ia_css_tagger_sp_set_copy_pipe: 32F2 */

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
/* function ia_css_queue_local_init: 5528 */
#else
/* function ia_css_queue_local_init: 5707 */
#endif

#ifndef ISP2401
/* function sp_event_proxy_callout_func: 6947 */
#else
/* function sp_event_proxy_callout_func: 6B45 */
#endif

#ifndef ISP2401
/* function qos_scheduler_schedule_stage: 6580 */
#else
/* function qos_scheduler_schedule_stage: 6759 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_thread_sp_num_ready_threads
#define HIVE_MEM_ia_css_thread_sp_num_ready_threads scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_thread_sp_num_ready_threads 0x5A78
#else
#define HIVE_ADDR_ia_css_thread_sp_num_ready_threads 0x5B28
#endif
#define HIVE_SIZE_ia_css_thread_sp_num_ready_threads 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_thread_sp_num_ready_threads scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_thread_sp_num_ready_threads 0x5A78
#else
#define HIVE_ADDR_sp_ia_css_thread_sp_num_ready_threads 0x5B28
#endif
#define HIVE_SIZE_sp_ia_css_thread_sp_num_ready_threads 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_threads_stack_size
#define HIVE_MEM_sp_threads_stack_size scalar_processor_2400_dmem
#define HIVE_ADDR_sp_threads_stack_size 0x17C
#define HIVE_SIZE_sp_threads_stack_size 24
#else
#endif
#endif
#define HIVE_MEM_sp_sp_threads_stack_size scalar_processor_2400_dmem
#define HIVE_ADDR_sp_sp_threads_stack_size 0x17C
#define HIVE_SIZE_sp_sp_threads_stack_size 24

#ifndef ISP2401
/* function ia_css_ispctrl_sp_isp_done_row_striping: 4849 */
#else
/* function ia_css_ispctrl_sp_isp_done_row_striping: 4A0D */
#endif

#ifndef ISP2401
/* function __ia_css_virtual_isys_sp_isr_text: 5E45 */
#else
/* function __ia_css_virtual_isys_sp_isr_text: 5F4E */
#endif

#ifndef ISP2401
/* function ia_css_queue_dequeue: 53A6 */
#else
/* function ia_css_queue_dequeue: 5585 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_configure_channel: 6DEE */
#else
/* function is_qos_standalone_mode: 6734 */

/* function ia_css_dmaproxy_sp_configure_channel: 703C */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_current_thread_fiber_sp
#define HIVE_MEM_current_thread_fiber_sp scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_current_thread_fiber_sp 0x5A80
#else
#define HIVE_ADDR_current_thread_fiber_sp 0x5B2C
#endif
#define HIVE_SIZE_current_thread_fiber_sp 4
#else
#endif
#endif
#define HIVE_MEM_sp_current_thread_fiber_sp scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_current_thread_fiber_sp 0x5A80
#else
#define HIVE_ADDR_sp_current_thread_fiber_sp 0x5B2C
#endif
#define HIVE_SIZE_sp_current_thread_fiber_sp 4

#ifndef ISP2401
/* function ia_css_circbuf_pop: 1674 */
#else
/* function ia_css_circbuf_pop: 15EA */
#endif

#ifndef ISP2401
/* function memset: 68F9 */
#else
/* function memset: 6AF7 */
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
/* function pixelgen_prbs_stop: E6F */
#else
/* function pixelgen_prbs_stop: E69 */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_acc_stage_enable: 1FC0 */
#else
/* function ia_css_pipeline_acc_stage_enable: 1F69 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_unlock_exp_id: 296A */
#else
/* function ia_css_tagger_sp_unlock_exp_id: 293B */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_ph
#define HIVE_MEM_isp_ph scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isp_ph 0x7360
#else
#define HIVE_ADDR_isp_ph 0x740C
#endif
#define HIVE_SIZE_isp_ph 28
#else
#endif
#endif
#define HIVE_MEM_sp_isp_ph scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_ph 0x7360
#else
#define HIVE_ADDR_sp_isp_ph 0x740C
#endif
#define HIVE_SIZE_sp_isp_ph 28

#ifndef ISP2401
/* function ia_css_ispctrl_sp_init_ds: 40D6 */
#else
/* function ia_css_ispctrl_sp_init_ds: 4286 */
#endif

#ifndef ISP2401
/* function get_xmem_base_addr_raw: 4479 */
#else
/* function get_xmem_base_addr_raw: 4635 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_param
#define HIVE_MEM_sp_all_cbs_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_all_cbs_param 0x586C
#else
#define HIVE_ADDR_sp_all_cbs_param 0x5908
#endif
#define HIVE_SIZE_sp_all_cbs_param 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_param scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cbs_param 0x586C
#else
#define HIVE_ADDR_sp_sp_all_cbs_param 0x5908
#endif
#define HIVE_SIZE_sp_sp_all_cbs_param 16

#ifndef ISP2401
/* function pixelgen_tpg_config: F30 */
#else
/* function pixelgen_tpg_config: F2A */
#endif

#ifndef ISP2401
/* function ia_css_circbuf_create: 16C2 */
#else
/* function ia_css_circbuf_create: 1638 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_sp_group
#define HIVE_MEM_sem_for_sp_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_sp_group 0x587C
#else
#define HIVE_ADDR_sem_for_sp_group 0x5918
#endif
#define HIVE_SIZE_sem_for_sp_group 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_sp_group scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_sp_group 0x587C
#else
#define HIVE_ADDR_sp_sem_for_sp_group 0x5918
#endif
#define HIVE_SIZE_sp_sem_for_sp_group 20

#ifndef ISP2401
/* function csi_rx_frontend_run: C22 */
#else
/* function csi_rx_frontend_run: C1C */

/* function __ia_css_dmaproxy_sp_configure_channel_text: 3D7C */
#endif

#ifndef ISP2401
/* function ia_css_framebuf_sp_wait_for_in_frame: 64B7 */
#else
/* function ia_css_framebuf_sp_wait_for_in_frame: 667D */
#endif

#ifndef ISP2401
/* function ia_css_isys_stream_open: 60E3 */
#else
/* function ia_css_isys_stream_open: 62A9 */
#endif

#ifndef ISP2401
/* function ia_css_sp_rawcopy_tag_frame: 5C71 */
#else
/* function ia_css_sp_rawcopy_tag_frame: 5E35 */
#endif

#ifndef ISP2401
/* function input_system_channel_configure: 125D */
#else
/* function input_system_channel_configure: 11D8 */
#endif

#ifndef ISP2401
/* function isp_hmem_clear: B33 */
#else
/* function isp_hmem_clear: B2D */
#endif

#ifndef ISP2401
/* function ia_css_framebuf_sp_release_in_frame: 64FA */
#else
/* function ia_css_framebuf_sp_release_in_frame: 66C0 */
#endif

#ifndef ISP2401
/* function stream2mmio_config: E1B */
#else
/* function stream2mmio_config: E15 */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_start_binary: 3F55 */
#else
/* function ia_css_ispctrl_sp_start_binary: 40D8 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_h_pipe_private_ddr_ptrs
#define HIVE_MEM_ia_css_bufq_sp_h_pipe_private_ddr_ptrs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 0x698C
#else
#define HIVE_ADDR_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 0x6A38
#endif
#define HIVE_SIZE_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 20
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_h_pipe_private_ddr_ptrs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 0x698C
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 0x6A38
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_h_pipe_private_ddr_ptrs 20

#ifndef ISP2401
/* function ia_css_eventq_sp_recv: 3DEB */
#else
/* function ia_css_eventq_sp_recv: 3F60 */
#endif

#ifndef ISP2401
/* function csi_rx_frontend_config: C7A */
#else
/* function csi_rx_frontend_config: C74 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_pool
#define HIVE_MEM_isp_pool scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isp_pool 0x370
#else
#define HIVE_ADDR_isp_pool 0x388
#endif
#define HIVE_SIZE_isp_pool 4
#else
#endif
#endif
#define HIVE_MEM_sp_isp_pool scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_pool 0x370
#else
#define HIVE_ADDR_sp_isp_pool 0x388
#endif
#define HIVE_SIZE_sp_isp_pool 4

#ifndef ISP2401
/* function ia_css_rmgr_sp_rel_gen: 61E9 */
#else
/* function ia_css_rmgr_sp_rel_gen: 63AF */

/* function ia_css_tagger_sp_unblock_clients: 31C3 */
#endif

#ifndef ISP2401
/* function css_get_frame_processing_time_end: 28E9 */
#else
/* function css_get_frame_processing_time_end: 28BA */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_event_any_pending_mask
#define HIVE_MEM_event_any_pending_mask scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_event_any_pending_mask 0x388
#else
#define HIVE_ADDR_event_any_pending_mask 0x3A0
#endif
#define HIVE_SIZE_event_any_pending_mask 8
#else
#endif
#endif
#define HIVE_MEM_sp_event_any_pending_mask scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_event_any_pending_mask 0x388
#else
#define HIVE_ADDR_sp_event_any_pending_mask 0x3A0
#endif
#define HIVE_SIZE_sp_event_any_pending_mask 8

#ifndef ISP2401
/* function ia_css_pipeline_sp_get_pipe_io_status: 1AB8 */
#else
/* function ia_css_pipeline_sp_get_pipe_io_status: 1A5A */
#endif

/* function sh_css_decode_tag_descr: 352 */

/* function debug_enqueue_isp: 27B */

#ifndef ISP2401
/* function qos_scheduler_update_stage_budget: 656E */
#else
/* function qos_scheduler_update_stage_budget: 673C */
#endif

#ifndef ISP2401
/* function ia_css_spctrl_sp_uninit: 5DFB */
#else
/* function ia_css_spctrl_sp_uninit: 5EDD */
#endif

#ifndef ISP2401
/* function csi_rx_backend_run: C68 */
#else
/* function csi_rx_backend_run: C62 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_dis_bufs
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_dis_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_dis_bufs 0x69A0
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_dis_bufs 0x6A4C
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_dis_bufs 140
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_dis_bufs scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_dis_bufs 0x69A0
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_dis_bufs 0x6A4C
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_dis_bufs 140

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_lock_from_start: 341B */
#else
/* function ia_css_tagger_buf_sp_lock_from_start: 353E */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_isp_idle
#define HIVE_MEM_sem_for_isp_idle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_isp_idle 0x5890
#else
#define HIVE_ADDR_sem_for_isp_idle 0x592C
#endif
#define HIVE_SIZE_sem_for_isp_idle 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_isp_idle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_isp_idle 0x5890
#else
#define HIVE_ADDR_sp_sem_for_isp_idle 0x592C
#endif
#define HIVE_SIZE_sp_sem_for_isp_idle 20

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_write_byte_addr: 3B0A */
#else
/* function ia_css_dmaproxy_sp_write_byte_addr: 3C6A */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_init: 3A81 */
#else
/* function ia_css_dmaproxy_sp_init: 3BE1 */
#endif

#ifndef ISP2401
/* function ia_css_bufq_sp_release_dynamic_buf_clock_tick: 3686 */
#else
/* function ia_css_bufq_sp_release_dynamic_buf_clock_tick: 37A9 */
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

#ifndef ISP2401
/* function input_system_channel_sync: 11A4 */
#else
/* function input_system_channel_sync: 6C10 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_rawcopy_sp_tagger
#define HIVE_MEM_ia_css_rawcopy_sp_tagger scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_rawcopy_sp_tagger 0x732C
#else
#define HIVE_ADDR_ia_css_rawcopy_sp_tagger 0x73D8
#endif
#define HIVE_SIZE_ia_css_rawcopy_sp_tagger 24
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_rawcopy_sp_tagger scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_rawcopy_sp_tagger 0x732C
#else
#define HIVE_ADDR_sp_ia_css_rawcopy_sp_tagger 0x73D8
#endif
#define HIVE_SIZE_sp_ia_css_rawcopy_sp_tagger 24

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_pipe_private_exp_ids
#define HIVE_MEM_ia_css_bufq_sp_pipe_private_exp_ids scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_exp_ids 0x6A2C
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_exp_ids 0x6AD8
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_exp_ids 70
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_exp_ids scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_exp_ids 0x6A2C
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_exp_ids 0x6AD8
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_exp_ids 70

#ifndef ISP2401
/* function ia_css_queue_item_load: 561A */
#else
/* function ia_css_queue_item_load: 57F9 */
#endif

#ifndef ISP2401
/* function ia_css_spctrl_sp_get_state: 5DE6 */
#else
/* function ia_css_spctrl_sp_get_state: 5EC8 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_callout_sp_thread
#define HIVE_MEM_callout_sp_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_callout_sp_thread 0x5A74
#else
#define HIVE_ADDR_callout_sp_thread 0x278
#endif
#define HIVE_SIZE_callout_sp_thread 4
#else
#endif
#endif
#define HIVE_MEM_sp_callout_sp_thread scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_callout_sp_thread 0x5A74
#else
#define HIVE_ADDR_sp_callout_sp_thread 0x278
#endif
#define HIVE_SIZE_sp_callout_sp_thread 4

#ifndef ISP2401
/* function thread_fiber_sp_init: 14CB */
#else
/* function thread_fiber_sp_init: 1441 */
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

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_isp_input_stream_format
#define HIVE_MEM_sp_isp_input_stream_format scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_input_stream_format 0x3E2C
#else
#define HIVE_ADDR_sp_isp_input_stream_format 0x3E50
#endif
#define HIVE_SIZE_sp_isp_input_stream_format 20
#else
#endif
#endif
#define HIVE_MEM_sp_sp_isp_input_stream_format scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_isp_input_stream_format 0x3E2C
#else
#define HIVE_ADDR_sp_sp_isp_input_stream_format 0x3E50
#endif
#define HIVE_SIZE_sp_sp_isp_input_stream_format 20

#ifndef ISP2401
/* function __mod: 6866 */
#else
/* function __mod: 6A64 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_init_dmem_channel: 3B6B */
#else
/* function ia_css_dmaproxy_sp_init_dmem_channel: 3CCB */
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_join: 139B */
#else
/* function ia_css_thread_sp_join: 1311 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_add_command: 6EF1 */
#else
/* function ia_css_dmaproxy_sp_add_command: 712E */
#endif

#ifndef ISP2401
/* function ia_css_sp_metadata_thread_func: 5DDF */
#else
/* function ia_css_sp_metadata_thread_func: 5EC1 */
#endif

#ifndef ISP2401
/* function __sp_event_proxy_func_critical: 6934 */
#else
/* function __sp_event_proxy_func_critical: 6B32 */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_wait_for_isys_stream_N: 5F53 */
#else
/* function ia_css_pipeline_sp_wait_for_isys_stream_N: 6074 */
#endif

#ifndef ISP2401
/* function ia_css_sp_metadata_wait: 5DD8 */
#else
/* function ia_css_sp_metadata_wait: 5EBA */
#endif

#ifndef ISP2401
/* function ia_css_circbuf_peek_from_start: 15A4 */
#else
/* function ia_css_circbuf_peek_from_start: 151A */
#endif

#ifndef ISP2401
/* function ia_css_event_sp_encode: 3E76 */
#else
/* function ia_css_event_sp_encode: 3FEB */
#endif

#ifndef ISP2401
/* function ia_css_thread_sp_run: 140E */
#else
/* function ia_css_thread_sp_run: 1384 */
#endif

#ifndef ISP2401
/* function sp_isys_copy_func: 618 */
#else
/* function sp_isys_copy_func: 5AC */
#endif

#ifndef ISP2401
/* function ia_css_sp_isp_param_init_isp_memories: 50A3 */
#else
/* function ia_css_sp_isp_param_init_isp_memories: 52AC */
#endif

#ifndef ISP2401
/* function register_isr: 921 */
#else
/* function register_isr: 8B5 */
#endif

/* function irq_raise: C8 */

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_mmu_invalidate: 3A48 */
#else
/* function ia_css_dmaproxy_sp_mmu_invalidate: 3B71 */
#endif

#ifndef ISP2401
/* function csi_rx_backend_disable: C34 */
#else
/* function csi_rx_backend_disable: C2E */
#endif

#ifndef ISP2401
/* function pipeline_sp_initialize_stage: 2104 */
#else
/* function pipeline_sp_initialize_stage: 20BF */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_N_CSI_RX_FE_CTRL_DLANES
#define HIVE_MEM_N_CSI_RX_FE_CTRL_DLANES scalar_processor_2400_dmem
#define HIVE_ADDR_N_CSI_RX_FE_CTRL_DLANES 0x1C4
#define HIVE_SIZE_N_CSI_RX_FE_CTRL_DLANES 12
#else
#endif
#endif
#define HIVE_MEM_sp_N_CSI_RX_FE_CTRL_DLANES scalar_processor_2400_dmem
#define HIVE_ADDR_sp_N_CSI_RX_FE_CTRL_DLANES 0x1C4
#define HIVE_SIZE_sp_N_CSI_RX_FE_CTRL_DLANES 12

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_read_byte_addr_mmio: 6DC0 */
#else
/* function ia_css_dmaproxy_sp_read_byte_addr_mmio: 700E */
#endif

#ifndef ISP2401
/* function ia_css_ispctrl_sp_done_ds: 40BD */
#else
/* function ia_css_ispctrl_sp_done_ds: 426D */
#endif

#ifndef ISP2401
/* function csi_rx_backend_config: C8B */
#else
/* function csi_rx_backend_config: C85 */
#endif

#ifndef ISP2401
/* function ia_css_sp_isp_param_get_mem_inits: 507E */
#else
/* function ia_css_sp_isp_param_get_mem_inits: 5287 */
#endif

#ifndef ISP2401
/* function ia_css_parambuf_sp_init_buffer_queues: 1A85 */
#else
/* function ia_css_parambuf_sp_init_buffer_queues: 1A27 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_pfp_spref
#define HIVE_MEM_vbuf_pfp_spref scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_vbuf_pfp_spref 0x378
#else
#define HIVE_ADDR_vbuf_pfp_spref 0x390
#endif
#define HIVE_SIZE_vbuf_pfp_spref 4
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_pfp_spref scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_vbuf_pfp_spref 0x378
#else
#define HIVE_ADDR_sp_vbuf_pfp_spref 0x390
#endif
#define HIVE_SIZE_sp_vbuf_pfp_spref 4

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
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_frames 0x6A74
#else
#define HIVE_ADDR_ia_css_bufq_sp_pipe_private_frames 0x6B20
#endif
#define HIVE_SIZE_ia_css_bufq_sp_pipe_private_frames 280
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_pipe_private_frames scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_frames 0x6A74
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_pipe_private_frames 0x6B20
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_pipe_private_frames 280

#ifndef ISP2401
/* function qos_scheduler_init_stage_budget: 65A7 */
#else
/* function qos_scheduler_init_stage_budget: 679A */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp2host_buffer_queue_handle
#define HIVE_MEM_sp2host_buffer_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp2host_buffer_queue_handle 0x6B8C
#else
#define HIVE_ADDR_sp2host_buffer_queue_handle 0x6C38
#endif
#define HIVE_SIZE_sp2host_buffer_queue_handle 96
#else
#endif
#endif
#define HIVE_MEM_sp_sp2host_buffer_queue_handle scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp2host_buffer_queue_handle 0x6B8C
#else
#define HIVE_ADDR_sp_sp2host_buffer_queue_handle 0x6C38
#endif
#define HIVE_SIZE_sp_sp2host_buffer_queue_handle 96

#ifndef ISP2401
/* function ia_css_ispctrl_sp_init_isp_vars: 4D9D */
#else
/* function ia_css_ispctrl_sp_init_isp_vars: 4F79 */
#endif

#ifndef ISP2401
/* function ia_css_isys_stream_start: 6010 */
#else
/* function ia_css_isys_stream_start: 6187 */
#endif

#ifndef ISP2401
/* function sp_warning: 954 */
#else
/* function sp_warning: 8E8 */
#endif

#ifndef ISP2401
/* function ia_css_rmgr_sp_vbuf_enqueue: 62DC */
#else
/* function ia_css_rmgr_sp_vbuf_enqueue: 64A2 */
#endif

#ifndef ISP2401
/* function ia_css_tagger_sp_tag_exp_id: 2A84 */
#else
/* function ia_css_tagger_sp_tag_exp_id: 2A55 */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_sfi_release_current_frame: 276B */
#else
/* function ia_css_pipeline_sp_sfi_release_current_frame: 273C */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_write: 3B21 */
#else
/* function ia_css_dmaproxy_sp_write: 3C81 */
#endif

#ifndef ISP2401
/* function ia_css_isys_stream_start_async: 608A */
#else
/* function ia_css_isys_stream_start_async: 6250 */
#endif

#ifndef ISP2401
/* function ia_css_parambuf_sp_release_in_param: 1905 */
#else
/* function ia_css_parambuf_sp_release_in_param: 187B */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_irq_sw_interrupt_token
#define HIVE_MEM_irq_sw_interrupt_token scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_irq_sw_interrupt_token 0x3E28
#else
#define HIVE_ADDR_irq_sw_interrupt_token 0x3E4C
#endif
#define HIVE_SIZE_irq_sw_interrupt_token 4
#else
#endif
#endif
#define HIVE_MEM_sp_irq_sw_interrupt_token scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_irq_sw_interrupt_token 0x3E28
#else
#define HIVE_ADDR_sp_irq_sw_interrupt_token 0x3E4C
#endif
#define HIVE_SIZE_sp_irq_sw_interrupt_token 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_isp_addresses
#define HIVE_MEM_sp_isp_addresses scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_addresses 0x6FDC
#else
#define HIVE_ADDR_sp_isp_addresses 0x708C
#endif
#define HIVE_SIZE_sp_isp_addresses 172
#else
#endif
#endif
#define HIVE_MEM_sp_sp_isp_addresses scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_isp_addresses 0x6FDC
#else
#define HIVE_ADDR_sp_sp_isp_addresses 0x708C
#endif
#define HIVE_SIZE_sp_sp_isp_addresses 172

#ifndef ISP2401
/* function ia_css_rmgr_sp_acq_gen: 6201 */
#else
/* function ia_css_rmgr_sp_acq_gen: 63C7 */
#endif

#ifndef ISP2401
/* function input_system_input_port_open: 10ED */
#else
/* function input_system_input_port_open: 10E7 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isps
#define HIVE_MEM_isps scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isps 0x737C
#else
#define HIVE_ADDR_isps 0x7428
#endif
#define HIVE_SIZE_isps 28
#else
#endif
#endif
#define HIVE_MEM_sp_isps scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isps 0x737C
#else
#define HIVE_ADDR_sp_isps 0x7428
#endif
#define HIVE_SIZE_sp_isps 28

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_host_sp_queues_initialized
#define HIVE_MEM_host_sp_queues_initialized scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_host_sp_queues_initialized 0x3E40
#else
#define HIVE_ADDR_host_sp_queues_initialized 0x3E64
#endif
#define HIVE_SIZE_host_sp_queues_initialized 4
#else
#endif
#endif
#define HIVE_MEM_sp_host_sp_queues_initialized scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_host_sp_queues_initialized 0x3E40
#else
#define HIVE_ADDR_sp_host_sp_queues_initialized 0x3E64
#endif
#define HIVE_SIZE_sp_host_sp_queues_initialized 4

#ifndef ISP2401
/* function ia_css_queue_uninit: 54E6 */
#else
/* function ia_css_queue_uninit: 56C5 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_ispctrl_sp_isp_started
#define HIVE_MEM_ia_css_ispctrl_sp_isp_started scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_ispctrl_sp_isp_started 0x6C94
#else
#define HIVE_ADDR_ia_css_ispctrl_sp_isp_started 0x6D40
#endif
#define HIVE_SIZE_ia_css_ispctrl_sp_isp_started 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_ispctrl_sp_isp_started scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_ispctrl_sp_isp_started 0x6C94
#else
#define HIVE_ADDR_sp_ia_css_ispctrl_sp_isp_started 0x6D40
#endif
#define HIVE_SIZE_sp_ia_css_ispctrl_sp_isp_started 4

#ifndef ISP2401
/* function ia_css_bufq_sp_release_dynamic_buf: 36F2 */
#else
/* function ia_css_bufq_sp_release_dynamic_buf: 3815 */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_set_height_exception: 3C19 */
#else
/* function ia_css_dmaproxy_sp_set_height_exception: 3D8E */
#endif

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_init_vmem_channel: 3B9E */
#else
/* function ia_css_dmaproxy_sp_init_vmem_channel: 3CFF */
#endif

#ifndef ISP2401
/* function csi_rx_backend_stop: C57 */
#else
/* function csi_rx_backend_stop: C51 */
#endif

#ifndef ISP2401
#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_num_ready_threads
#define HIVE_MEM_num_ready_threads scalar_processor_2400_dmem
#define HIVE_ADDR_num_ready_threads 0x5A7C
#define HIVE_SIZE_num_ready_threads 4
#else
#endif
#endif
#define HIVE_MEM_sp_num_ready_threads scalar_processor_2400_dmem
#define HIVE_ADDR_sp_num_ready_threads 0x5A7C
#define HIVE_SIZE_sp_num_ready_threads 4

/* function ia_css_dmaproxy_sp_write_byte_addr_mmio: 3AF3 */
#else
/* function ia_css_dmaproxy_sp_write_byte_addr_mmio: 3C53 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_vbuf_spref
#define HIVE_MEM_vbuf_spref scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_vbuf_spref 0x374
#else
#define HIVE_ADDR_vbuf_spref 0x38C
#endif
#define HIVE_SIZE_vbuf_spref 4
#else
#endif
#endif
#define HIVE_MEM_sp_vbuf_spref scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_vbuf_spref 0x374
#else
#define HIVE_ADDR_sp_vbuf_spref 0x38C
#endif
#define HIVE_SIZE_sp_vbuf_spref 4

#ifndef ISP2401
/* function ia_css_queue_enqueue: 5430 */
#else
/* function ia_css_queue_enqueue: 560F */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_request
#define HIVE_MEM_ia_css_flash_sp_request scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_flash_sp_request 0x5B30
#else
#define HIVE_ADDR_ia_css_flash_sp_request 0x5BDC
#endif
#define HIVE_SIZE_ia_css_flash_sp_request 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_request scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_flash_sp_request 0x5B30
#else
#define HIVE_ADDR_sp_ia_css_flash_sp_request 0x5BDC
#endif
#define HIVE_SIZE_sp_ia_css_flash_sp_request 4

#ifndef ISP2401
/* function ia_css_dmaproxy_sp_vmem_write: 3AC4 */
#else
/* function ia_css_dmaproxy_sp_vmem_write: 3C24 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_tagger_frames
#define HIVE_MEM_tagger_frames scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_tagger_frames 0x5A84
#else
#define HIVE_ADDR_tagger_frames 0x5B30
#endif
#define HIVE_SIZE_tagger_frames 168
#else
#endif
#endif
#define HIVE_MEM_sp_tagger_frames scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_tagger_frames 0x5A84
#else
#define HIVE_ADDR_sp_tagger_frames 0x5B30
#endif
#define HIVE_SIZE_sp_tagger_frames 168

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_reading_if
#define HIVE_MEM_sem_for_reading_if scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_reading_if 0x58A4
#else
#define HIVE_ADDR_sem_for_reading_if 0x5940
#endif
#define HIVE_SIZE_sem_for_reading_if 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_reading_if scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_reading_if 0x58A4
#else
#define HIVE_ADDR_sp_sem_for_reading_if 0x5940
#endif
#define HIVE_SIZE_sp_sem_for_reading_if 20

#ifndef ISP2401
/* function sp_generate_interrupts: 9D3 */
#else
/* function sp_generate_interrupts: 967 */

/* function ia_css_pipeline_sp_start: 1FC2 */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_start: 2007 */
#else
/* function ia_css_thread_default_callout: 6C8F */
#endif

#ifndef ISP2401
/* function csi_rx_backend_enable: C45 */
#else
/* function csi_rx_backend_enable: C3F */
#endif

#ifndef ISP2401
/* function ia_css_sp_rawcopy_init: 5953 */
#else
/* function ia_css_sp_rawcopy_init: 5B32 */
#endif

#ifndef ISP2401
/* function input_system_input_port_configure: 113F */
#else
/* function input_system_input_port_configure: 1139 */
#endif

#ifndef ISP2401
/* function tmr_clock_read: 16EF */
#else
/* function tmr_clock_read: 1665 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ISP_BAMEM_BASE
#define HIVE_MEM_ISP_BAMEM_BASE scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ISP_BAMEM_BASE 0x380
#else
#define HIVE_ADDR_ISP_BAMEM_BASE 0x398
#endif
#define HIVE_SIZE_ISP_BAMEM_BASE 4
#else
#endif
#endif
#define HIVE_MEM_sp_ISP_BAMEM_BASE scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ISP_BAMEM_BASE 0x380
#else
#define HIVE_ADDR_sp_ISP_BAMEM_BASE 0x398
#endif
#define HIVE_SIZE_sp_ISP_BAMEM_BASE 4

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_bufq_sp_sems_for_sp2host_buf_queues
#define HIVE_MEM_ia_css_bufq_sp_sems_for_sp2host_buf_queues scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_bufq_sp_sems_for_sp2host_buf_queues 0x6BEC
#else
#define HIVE_ADDR_ia_css_bufq_sp_sems_for_sp2host_buf_queues 0x6C98
#endif
#define HIVE_SIZE_ia_css_bufq_sp_sems_for_sp2host_buf_queues 160
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_bufq_sp_sems_for_sp2host_buf_queues scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_bufq_sp_sems_for_sp2host_buf_queues 0x6BEC
#else
#define HIVE_ADDR_sp_ia_css_bufq_sp_sems_for_sp2host_buf_queues 0x6C98
#endif
#define HIVE_SIZE_sp_ia_css_bufq_sp_sems_for_sp2host_buf_queues 160

#ifndef ISP2401
/* function isys2401_dma_config_legacy: DE0 */
#else
/* function isys2401_dma_config_legacy: DDA */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ibuf_ctrl_master_ports
#define HIVE_MEM_ibuf_ctrl_master_ports scalar_processor_2400_dmem
#define HIVE_ADDR_ibuf_ctrl_master_ports 0x208
#define HIVE_SIZE_ibuf_ctrl_master_ports 12
#else
#endif
#endif
#define HIVE_MEM_sp_ibuf_ctrl_master_ports scalar_processor_2400_dmem
#define HIVE_ADDR_sp_ibuf_ctrl_master_ports 0x208
#define HIVE_SIZE_sp_ibuf_ctrl_master_ports 12

#ifndef ISP2401
/* function css_get_frame_processing_time_start: 28F1 */
#else
/* function css_get_frame_processing_time_start: 28C2 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_all_cbs_frame
#define HIVE_MEM_sp_all_cbs_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_all_cbs_frame 0x58B8
#else
#define HIVE_ADDR_sp_all_cbs_frame 0x5954
#endif
#define HIVE_SIZE_sp_all_cbs_frame 16
#else
#endif
#endif
#define HIVE_MEM_sp_sp_all_cbs_frame scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_all_cbs_frame 0x58B8
#else
#define HIVE_ADDR_sp_sp_all_cbs_frame 0x5954
#endif
#define HIVE_SIZE_sp_sp_all_cbs_frame 16

#ifndef ISP2401
/* function ia_css_virtual_isys_sp_isr: 6F07 */
#else
/* function ia_css_virtual_isys_sp_isr: 716E */
#endif

#ifndef ISP2401
/* function thread_sp_queue_print: 142B */
#else
/* function thread_sp_queue_print: 13A1 */
#endif

#ifndef ISP2401
/* function sp_notify_eof: 97F */
#else
/* function sp_notify_eof: 913 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sem_for_str2mem
#define HIVE_MEM_sem_for_str2mem scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sem_for_str2mem 0x58C8
#else
#define HIVE_ADDR_sem_for_str2mem 0x5964
#endif
#define HIVE_SIZE_sem_for_str2mem 20
#else
#endif
#endif
#define HIVE_MEM_sp_sem_for_str2mem scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sem_for_str2mem 0x58C8
#else
#define HIVE_ADDR_sp_sem_for_str2mem 0x5964
#endif
#define HIVE_SIZE_sp_sem_for_str2mem 20

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_is_marked_from_start: 3483 */
#else
/* function ia_css_tagger_buf_sp_is_marked_from_start: 35A6 */
#endif

#ifndef ISP2401
/* function ia_css_bufq_sp_acquire_dynamic_buf: 38AA */
#else
/* function ia_css_bufq_sp_acquire_dynamic_buf: 39CD */
#endif

#ifndef ISP2401
/* function ia_css_pipeline_sp_sfi_mode_is_enabled: 28BF */
#else
/* function ia_css_pipeline_sp_sfi_mode_is_enabled: 2890 */
#endif

#ifndef ISP2401
/* function ia_css_circbuf_destroy: 16B9 */
#else
/* function ia_css_circbuf_destroy: 162F */
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
/* function ia_css_sp_isp_param_mem_load: 5011 */
#else
/* function ia_css_sp_isp_param_mem_load: 521A */
#endif

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_pop_from_start: 326F */
#else
/* function ia_css_tagger_buf_sp_pop_from_start: 3392 */
#endif

#ifndef ISP2401
/* function __div: 681E */
#else
/* function __div: 6A1C */
#endif

#ifndef ISP2401
/* function ia_css_rmgr_sp_refcount_release_vbuf: 62FB */
#else
/* function ia_css_rmgr_sp_refcount_release_vbuf: 64C1 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_ia_css_flash_sp_in_use
#define HIVE_MEM_ia_css_flash_sp_in_use scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_ia_css_flash_sp_in_use 0x5B34
#else
#define HIVE_ADDR_ia_css_flash_sp_in_use 0x5BE0
#endif
#define HIVE_SIZE_ia_css_flash_sp_in_use 4
#else
#endif
#endif
#define HIVE_MEM_sp_ia_css_flash_sp_in_use scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_ia_css_flash_sp_in_use 0x5B34
#else
#define HIVE_ADDR_sp_ia_css_flash_sp_in_use 0x5BE0
#endif
#define HIVE_SIZE_sp_ia_css_flash_sp_in_use 4

#ifndef ISP2401
/* function ia_css_thread_sem_sp_wait: 6AE4 */
#else
/* function ia_css_thread_sem_sp_wait: 6D63 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_sp_sleep_mode
#define HIVE_MEM_sp_sleep_mode scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sleep_mode 0x3E44
#else
#define HIVE_ADDR_sp_sleep_mode 0x3E68
#endif
#define HIVE_SIZE_sp_sleep_mode 4
#else
#endif
#endif
#define HIVE_MEM_sp_sp_sleep_mode scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_sp_sleep_mode 0x3E44
#else
#define HIVE_ADDR_sp_sp_sleep_mode 0x3E68
#endif
#define HIVE_SIZE_sp_sp_sleep_mode 4

#ifndef ISP2401
/* function ia_css_tagger_buf_sp_push: 337E */
#else
/* function ia_css_tagger_buf_sp_push: 34A1 */
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
/* function ia_css_queue_remote_init: 5508 */
#else
/* function ia_css_queue_remote_init: 56E7 */
#endif

#ifndef HIVE_MULTIPLE_PROGRAMS
#ifndef HIVE_MEM_isp_stop_req
#define HIVE_MEM_isp_stop_req scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_isp_stop_req 0x575C
#else
#define HIVE_ADDR_isp_stop_req 0x57F8
#endif
#define HIVE_SIZE_isp_stop_req 4
#else
#endif
#endif
#define HIVE_MEM_sp_isp_stop_req scalar_processor_2400_dmem
#ifndef ISP2401
#define HIVE_ADDR_sp_isp_stop_req 0x575C
#else
#define HIVE_ADDR_sp_isp_stop_req 0x57F8
#endif
#define HIVE_SIZE_sp_isp_stop_req 4

#ifndef ISP2401
/* function ia_css_pipeline_sp_sfi_request_next_frame: 2781 */
#else
/* function ia_css_pipeline_sp_sfi_request_next_frame: 2752 */
#endif

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
