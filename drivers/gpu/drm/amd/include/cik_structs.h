/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef CIK_STRUCTS_H_
#define CIK_STRUCTS_H_

struct cik_mqd {
	uint32_t header;
	uint32_t compute_dispatch_initiator;
	uint32_t compute_dim_x;
	uint32_t compute_dim_y;
	uint32_t compute_dim_z;
	uint32_t compute_start_x;
	uint32_t compute_start_y;
	uint32_t compute_start_z;
	uint32_t compute_num_thread_x;
	uint32_t compute_num_thread_y;
	uint32_t compute_num_thread_z;
	uint32_t compute_pipelinestat_enable;
	uint32_t compute_perfcount_enable;
	uint32_t compute_pgm_lo;
	uint32_t compute_pgm_hi;
	uint32_t compute_tba_lo;
	uint32_t compute_tba_hi;
	uint32_t compute_tma_lo;
	uint32_t compute_tma_hi;
	uint32_t compute_pgm_rsrc1;
	uint32_t compute_pgm_rsrc2;
	uint32_t compute_vmid;
	uint32_t compute_resource_limits;
	uint32_t compute_static_thread_mgmt_se0;
	uint32_t compute_static_thread_mgmt_se1;
	uint32_t compute_tmpring_size;
	uint32_t compute_static_thread_mgmt_se2;
	uint32_t compute_static_thread_mgmt_se3;
	uint32_t compute_restart_x;
	uint32_t compute_restart_y;
	uint32_t compute_restart_z;
	uint32_t compute_thread_trace_enable;
	uint32_t compute_misc_reserved;
	uint32_t compute_user_data_0;
	uint32_t compute_user_data_1;
	uint32_t compute_user_data_2;
	uint32_t compute_user_data_3;
	uint32_t compute_user_data_4;
	uint32_t compute_user_data_5;
	uint32_t compute_user_data_6;
	uint32_t compute_user_data_7;
	uint32_t compute_user_data_8;
	uint32_t compute_user_data_9;
	uint32_t compute_user_data_10;
	uint32_t compute_user_data_11;
	uint32_t compute_user_data_12;
	uint32_t compute_user_data_13;
	uint32_t compute_user_data_14;
	uint32_t compute_user_data_15;
	uint32_t cp_compute_csinvoc_count_lo;
	uint32_t cp_compute_csinvoc_count_hi;
	uint32_t cp_mqd_base_addr_lo;
	uint32_t cp_mqd_base_addr_hi;
	uint32_t cp_hqd_active;
	uint32_t cp_hqd_vmid;
	uint32_t cp_hqd_persistent_state;
	uint32_t cp_hqd_pipe_priority;
	uint32_t cp_hqd_queue_priority;
	uint32_t cp_hqd_quantum;
	uint32_t cp_hqd_pq_base_lo;
	uint32_t cp_hqd_pq_base_hi;
	uint32_t cp_hqd_pq_rptr;
	uint32_t cp_hqd_pq_rptr_report_addr_lo;
	uint32_t cp_hqd_pq_rptr_report_addr_hi;
	uint32_t cp_hqd_pq_wptr_poll_addr_lo;
	uint32_t cp_hqd_pq_wptr_poll_addr_hi;
	uint32_t cp_hqd_pq_doorbell_control;
	uint32_t cp_hqd_pq_wptr;
	uint32_t cp_hqd_pq_control;
	uint32_t cp_hqd_ib_base_addr_lo;
	uint32_t cp_hqd_ib_base_addr_hi;
	uint32_t cp_hqd_ib_rptr;
	uint32_t cp_hqd_ib_control;
	uint32_t cp_hqd_iq_timer;
	uint32_t cp_hqd_iq_rptr;
	uint32_t cp_hqd_dequeue_request;
	uint32_t cp_hqd_dma_offload;
	uint32_t cp_hqd_sema_cmd;
	uint32_t cp_hqd_msg_type;
	uint32_t cp_hqd_atomic0_preop_lo;
	uint32_t cp_hqd_atomic0_preop_hi;
	uint32_t cp_hqd_atomic1_preop_lo;
	uint32_t cp_hqd_atomic1_preop_hi;
	uint32_t cp_hqd_hq_status0;
	uint32_t cp_hqd_hq_control0;
	uint32_t cp_mqd_control;
	uint32_t cp_mqd_query_time_lo;
	uint32_t cp_mqd_query_time_hi;
	uint32_t cp_mqd_connect_start_time_lo;
	uint32_t cp_mqd_connect_start_time_hi;
	uint32_t cp_mqd_connect_end_time_lo;
	uint32_t cp_mqd_connect_end_time_hi;
	uint32_t cp_mqd_connect_end_wf_count;
	uint32_t cp_mqd_connect_end_pq_rptr;
	uint32_t cp_mqd_connect_end_pq_wptr;
	uint32_t cp_mqd_connect_end_ib_rptr;
	uint32_t reserved_96;
	uint32_t reserved_97;
	uint32_t reserved_98;
	uint32_t reserved_99;
	uint32_t iqtimer_pkt_header;
	uint32_t iqtimer_pkt_dw0;
	uint32_t iqtimer_pkt_dw1;
	uint32_t iqtimer_pkt_dw2;
	uint32_t iqtimer_pkt_dw3;
	uint32_t iqtimer_pkt_dw4;
	uint32_t iqtimer_pkt_dw5;
	uint32_t iqtimer_pkt_dw6;
	uint32_t reserved_108;
	uint32_t reserved_109;
	uint32_t reserved_110;
	uint32_t reserved_111;
	uint32_t queue_doorbell_id0;
	uint32_t queue_doorbell_id1;
	uint32_t queue_doorbell_id2;
	uint32_t queue_doorbell_id3;
	uint32_t queue_doorbell_id4;
	uint32_t queue_doorbell_id5;
	uint32_t queue_doorbell_id6;
	uint32_t queue_doorbell_id7;
	uint32_t queue_doorbell_id8;
	uint32_t queue_doorbell_id9;
	uint32_t queue_doorbell_id10;
	uint32_t queue_doorbell_id11;
	uint32_t queue_doorbell_id12;
	uint32_t queue_doorbell_id13;
	uint32_t queue_doorbell_id14;
	uint32_t queue_doorbell_id15;
};

struct cik_sdma_rlc_registers {
	uint32_t sdma_rlc_rb_cntl;
	uint32_t sdma_rlc_rb_base;
	uint32_t sdma_rlc_rb_base_hi;
	uint32_t sdma_rlc_rb_rptr;
	uint32_t sdma_rlc_rb_wptr;
	uint32_t sdma_rlc_rb_wptr_poll_cntl;
	uint32_t sdma_rlc_rb_wptr_poll_addr_hi;
	uint32_t sdma_rlc_rb_wptr_poll_addr_lo;
	uint32_t sdma_rlc_rb_rptr_addr_hi;
	uint32_t sdma_rlc_rb_rptr_addr_lo;
	uint32_t sdma_rlc_ib_cntl;
	uint32_t sdma_rlc_ib_rptr;
	uint32_t sdma_rlc_ib_offset;
	uint32_t sdma_rlc_ib_base_lo;
	uint32_t sdma_rlc_ib_base_hi;
	uint32_t sdma_rlc_ib_size;
	uint32_t sdma_rlc_skip_cntl;
	uint32_t sdma_rlc_context_status;
	uint32_t sdma_rlc_doorbell;
	uint32_t sdma_rlc_virtual_addr;
	uint32_t sdma_rlc_ape1_cntl;
	uint32_t sdma_rlc_doorbell_log;
	uint32_t reserved_22;
	uint32_t reserved_23;
	uint32_t reserved_24;
	uint32_t reserved_25;
	uint32_t reserved_26;
	uint32_t reserved_27;
	uint32_t reserved_28;
	uint32_t reserved_29;
	uint32_t reserved_30;
	uint32_t reserved_31;
	uint32_t reserved_32;
	uint32_t reserved_33;
	uint32_t reserved_34;
	uint32_t reserved_35;
	uint32_t reserved_36;
	uint32_t reserved_37;
	uint32_t reserved_38;
	uint32_t reserved_39;
	uint32_t reserved_40;
	uint32_t reserved_41;
	uint32_t reserved_42;
	uint32_t reserved_43;
	uint32_t reserved_44;
	uint32_t reserved_45;
	uint32_t reserved_46;
	uint32_t reserved_47;
	uint32_t reserved_48;
	uint32_t reserved_49;
	uint32_t reserved_50;
	uint32_t reserved_51;
	uint32_t reserved_52;
	uint32_t reserved_53;
	uint32_t reserved_54;
	uint32_t reserved_55;
	uint32_t reserved_56;
	uint32_t reserved_57;
	uint32_t reserved_58;
	uint32_t reserved_59;
	uint32_t reserved_60;
	uint32_t reserved_61;
	uint32_t reserved_62;
	uint32_t reserved_63;
	uint32_t reserved_64;
	uint32_t reserved_65;
	uint32_t reserved_66;
	uint32_t reserved_67;
	uint32_t reserved_68;
	uint32_t reserved_69;
	uint32_t reserved_70;
	uint32_t reserved_71;
	uint32_t reserved_72;
	uint32_t reserved_73;
	uint32_t reserved_74;
	uint32_t reserved_75;
	uint32_t reserved_76;
	uint32_t reserved_77;
	uint32_t reserved_78;
	uint32_t reserved_79;
	uint32_t reserved_80;
	uint32_t reserved_81;
	uint32_t reserved_82;
	uint32_t reserved_83;
	uint32_t reserved_84;
	uint32_t reserved_85;
	uint32_t reserved_86;
	uint32_t reserved_87;
	uint32_t reserved_88;
	uint32_t reserved_89;
	uint32_t reserved_90;
	uint32_t reserved_91;
	uint32_t reserved_92;
	uint32_t reserved_93;
	uint32_t reserved_94;
	uint32_t reserved_95;
	uint32_t reserved_96;
	uint32_t reserved_97;
	uint32_t reserved_98;
	uint32_t reserved_99;
	uint32_t reserved_100;
	uint32_t reserved_101;
	uint32_t reserved_102;
	uint32_t reserved_103;
	uint32_t reserved_104;
	uint32_t reserved_105;
	uint32_t reserved_106;
	uint32_t reserved_107;
	uint32_t reserved_108;
	uint32_t reserved_109;
	uint32_t reserved_110;
	uint32_t reserved_111;
	uint32_t reserved_112;
	uint32_t reserved_113;
	uint32_t reserved_114;
	uint32_t reserved_115;
	uint32_t reserved_116;
	uint32_t reserved_117;
	uint32_t reserved_118;
	uint32_t reserved_119;
	uint32_t reserved_120;
	uint32_t reserved_121;
	uint32_t reserved_122;
	uint32_t reserved_123;
	uint32_t reserved_124;
	uint32_t reserved_125;
	uint32_t reserved_126;
	uint32_t reserved_127;
	uint32_t sdma_engine_id;
	uint32_t sdma_queue_id;
};



#endif /* CIK_STRUCTS_H_ */
