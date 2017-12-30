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

#ifndef VI_STRUCTS_H_
#define VI_STRUCTS_H_

struct vi_sdma_mqd {
	uint32_t sdmax_rlcx_rb_cntl;
	uint32_t sdmax_rlcx_rb_base;
	uint32_t sdmax_rlcx_rb_base_hi;
	uint32_t sdmax_rlcx_rb_rptr;
	uint32_t sdmax_rlcx_rb_wptr;
	uint32_t sdmax_rlcx_rb_wptr_poll_cntl;
	uint32_t sdmax_rlcx_rb_wptr_poll_addr_hi;
	uint32_t sdmax_rlcx_rb_wptr_poll_addr_lo;
	uint32_t sdmax_rlcx_rb_rptr_addr_hi;
	uint32_t sdmax_rlcx_rb_rptr_addr_lo;
	uint32_t sdmax_rlcx_ib_cntl;
	uint32_t sdmax_rlcx_ib_rptr;
	uint32_t sdmax_rlcx_ib_offset;
	uint32_t sdmax_rlcx_ib_base_lo;
	uint32_t sdmax_rlcx_ib_base_hi;
	uint32_t sdmax_rlcx_ib_size;
	uint32_t sdmax_rlcx_skip_cntl;
	uint32_t sdmax_rlcx_context_status;
	uint32_t sdmax_rlcx_doorbell;
	uint32_t sdmax_rlcx_virtual_addr;
	uint32_t sdmax_rlcx_ape1_cntl;
	uint32_t sdmax_rlcx_doorbell_log;
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
};

struct vi_mqd {
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
	uint32_t compute_dispatch_id;
	uint32_t compute_threadgroup_id;
	uint32_t compute_relaunch;
	uint32_t compute_wave_restore_addr_lo;
	uint32_t compute_wave_restore_addr_hi;
	uint32_t compute_wave_restore_control;
	uint32_t reserved9;
	uint32_t reserved10;
	uint32_t reserved11;
	uint32_t reserved12;
	uint32_t reserved13;
	uint32_t reserved14;
	uint32_t reserved15;
	uint32_t reserved16;
	uint32_t reserved17;
	uint32_t reserved18;
	uint32_t reserved19;
	uint32_t reserved20;
	uint32_t reserved21;
	uint32_t reserved22;
	uint32_t reserved23;
	uint32_t reserved24;
	uint32_t reserved25;
	uint32_t reserved26;
	uint32_t reserved27;
	uint32_t reserved28;
	uint32_t reserved29;
	uint32_t reserved30;
	uint32_t reserved31;
	uint32_t reserved32;
	uint32_t reserved33;
	uint32_t reserved34;
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
	uint32_t reserved35;
	uint32_t reserved36;
	uint32_t reserved37;
	uint32_t cp_mqd_query_time_lo;
	uint32_t cp_mqd_query_time_hi;
	uint32_t cp_mqd_connect_start_time_lo;
	uint32_t cp_mqd_connect_start_time_hi;
	uint32_t cp_mqd_connect_end_time_lo;
	uint32_t cp_mqd_connect_end_time_hi;
	uint32_t cp_mqd_connect_end_wf_count;
	uint32_t cp_mqd_connect_end_pq_rptr;
	uint32_t cp_mqd_connect_endvi_sdma_mqd_pq_wptr;
	uint32_t cp_mqd_connect_end_ib_rptr;
	uint32_t reserved38;
	uint32_t reserved39;
	uint32_t cp_mqd_save_start_time_lo;
	uint32_t cp_mqd_save_start_time_hi;
	uint32_t cp_mqd_save_end_time_lo;
	uint32_t cp_mqd_save_end_time_hi;
	uint32_t cp_mqd_restore_start_time_lo;
	uint32_t cp_mqd_restore_start_time_hi;
	uint32_t cp_mqd_restore_end_time_lo;
	uint32_t cp_mqd_restore_end_time_hi;
	uint32_t disable_queue;
	uint32_t reserved41;
	uint32_t gds_cs_ctxsw_cnt0;
	uint32_t gds_cs_ctxsw_cnt1;
	uint32_t gds_cs_ctxsw_cnt2;
	uint32_t gds_cs_ctxsw_cnt3;
	uint32_t reserved42;
	uint32_t reserved43;
	uint32_t cp_pq_exe_status_lo;
	uint32_t cp_pq_exe_status_hi;
	uint32_t cp_packet_id_lo;
	uint32_t cp_packet_id_hi;
	uint32_t cp_packet_exe_status_lo;
	uint32_t cp_packet_exe_status_hi;
	uint32_t gds_save_base_addr_lo;
	uint32_t gds_save_base_addr_hi;
	uint32_t gds_save_mask_lo;
	uint32_t gds_save_mask_hi;
	uint32_t ctx_save_base_addr_lo;
	uint32_t ctx_save_base_addr_hi;
	uint32_t dynamic_cu_mask_addr_lo;
	uint32_t dynamic_cu_mask_addr_hi;
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
	uint32_t cp_hqd_hq_status1;
	uint32_t cp_hqd_hq_control1;
	uint32_t cp_hqd_eop_base_addr_lo;
	uint32_t cp_hqd_eop_base_addr_hi;
	uint32_t cp_hqd_eop_control;
	uint32_t cp_hqd_eop_rptr;
	uint32_t cp_hqd_eop_wptr;
	uint32_t cp_hqd_eop_done_events;
	uint32_t cp_hqd_ctx_save_base_addr_lo;
	uint32_t cp_hqd_ctx_save_base_addr_hi;
	uint32_t cp_hqd_ctx_save_control;
	uint32_t cp_hqd_cntl_stack_offset;
	uint32_t cp_hqd_cntl_stack_size;
	uint32_t cp_hqd_wg_state_offset;
	uint32_t cp_hqd_ctx_save_size;
	uint32_t cp_hqd_gds_resource_state;
	uint32_t cp_hqd_error;
	uint32_t cp_hqd_eop_wptr_mem;
	uint32_t cp_hqd_eop_dones;
	uint32_t reserved46;
	uint32_t reserved47;
	uint32_t reserved48;
	uint32_t reserved49;
	uint32_t reserved50;
	uint32_t reserved51;
	uint32_t reserved52;
	uint32_t reserved53;
	uint32_t reserved54;
	uint32_t reserved55;
	uint32_t iqtimer_pkt_header;
	uint32_t iqtimer_pkt_dw0;
	uint32_t iqtimer_pkt_dw1;
	uint32_t iqtimer_pkt_dw2;
	uint32_t iqtimer_pkt_dw3;
	uint32_t iqtimer_pkt_dw4;
	uint32_t iqtimer_pkt_dw5;
	uint32_t iqtimer_pkt_dw6;
	uint32_t iqtimer_pkt_dw7;
	uint32_t iqtimer_pkt_dw8;
	uint32_t iqtimer_pkt_dw9;
	uint32_t iqtimer_pkt_dw10;
	uint32_t iqtimer_pkt_dw11;
	uint32_t iqtimer_pkt_dw12;
	uint32_t iqtimer_pkt_dw13;
	uint32_t iqtimer_pkt_dw14;
	uint32_t iqtimer_pkt_dw15;
	uint32_t iqtimer_pkt_dw16;
	uint32_t iqtimer_pkt_dw17;
	uint32_t iqtimer_pkt_dw18;
	uint32_t iqtimer_pkt_dw19;
	uint32_t iqtimer_pkt_dw20;
	uint32_t iqtimer_pkt_dw21;
	uint32_t iqtimer_pkt_dw22;
	uint32_t iqtimer_pkt_dw23;
	uint32_t iqtimer_pkt_dw24;
	uint32_t iqtimer_pkt_dw25;
	uint32_t iqtimer_pkt_dw26;
	uint32_t iqtimer_pkt_dw27;
	uint32_t iqtimer_pkt_dw28;
	uint32_t iqtimer_pkt_dw29;
	uint32_t iqtimer_pkt_dw30;
	uint32_t iqtimer_pkt_dw31;
	uint32_t reserved56;
	uint32_t reserved57;
	uint32_t reserved58;
	uint32_t set_resources_header;
	uint32_t set_resources_dw1;
	uint32_t set_resources_dw2;
	uint32_t set_resources_dw3;
	uint32_t set_resources_dw4;
	uint32_t set_resources_dw5;
	uint32_t set_resources_dw6;
	uint32_t set_resources_dw7;
	uint32_t reserved59;
	uint32_t reserved60;
	uint32_t reserved61;
	uint32_t reserved62;
	uint32_t reserved63;
	uint32_t reserved64;
	uint32_t reserved65;
	uint32_t reserved66;
	uint32_t reserved67;
	uint32_t reserved68;
	uint32_t reserved69;
	uint32_t reserved70;
	uint32_t reserved71;
	uint32_t reserved72;
	uint32_t reserved73;
	uint32_t reserved74;
	uint32_t reserved75;
	uint32_t reserved76;
	uint32_t reserved77;
	uint32_t reserved78;
	uint32_t reserved_t[256];
};

struct vi_mqd_allocation {
	struct vi_mqd mqd;
	uint32_t wptr_poll_mem;
	uint32_t rptr_report_mem;
	uint32_t dynamic_cu_mask;
	uint32_t dynamic_rb_mask;
};

struct vi_ce_ib_state {
	uint32_t    ce_ib_completion_status;
	uint32_t    ce_constegnine_count;
	uint32_t    ce_ibOffset_ib1;
	uint32_t    ce_ibOffset_ib2;
}; /* Total of 4 DWORD */

struct vi_de_ib_state {
	uint32_t    ib_completion_status;
	uint32_t    de_constEngine_count;
	uint32_t    ib_offset_ib1;
	uint32_t    ib_offset_ib2;
	uint32_t    preamble_begin_ib1;
	uint32_t    preamble_begin_ib2;
	uint32_t    preamble_end_ib1;
	uint32_t    preamble_end_ib2;
	uint32_t    draw_indirect_baseLo;
	uint32_t    draw_indirect_baseHi;
	uint32_t    disp_indirect_baseLo;
	uint32_t    disp_indirect_baseHi;
	uint32_t    gds_backup_addrlo;
	uint32_t    gds_backup_addrhi;
	uint32_t    index_base_addrlo;
	uint32_t    index_base_addrhi;
	uint32_t    sample_cntl;
}; /* Total of 17 DWORD */

struct vi_ce_ib_state_chained_ib {
	/* section of non chained ib part */
	uint32_t    ce_ib_completion_status;
	uint32_t    ce_constegnine_count;
	uint32_t    ce_ibOffset_ib1;
	uint32_t    ce_ibOffset_ib2;

	/* section of chained ib */
	uint32_t    ce_chainib_addrlo_ib1;
	uint32_t    ce_chainib_addrlo_ib2;
	uint32_t    ce_chainib_addrhi_ib1;
	uint32_t    ce_chainib_addrhi_ib2;
	uint32_t    ce_chainib_size_ib1;
	uint32_t    ce_chainib_size_ib2;
}; /* total 10 DWORD */

struct vi_de_ib_state_chained_ib {
	/* section of non chained ib part */
	uint32_t    ib_completion_status;
	uint32_t    de_constEngine_count;
	uint32_t    ib_offset_ib1;
	uint32_t    ib_offset_ib2;

	/* section of chained ib */
	uint32_t    chain_ib_addrlo_ib1;
	uint32_t    chain_ib_addrlo_ib2;
	uint32_t    chain_ib_addrhi_ib1;
	uint32_t    chain_ib_addrhi_ib2;
	uint32_t    chain_ib_size_ib1;
	uint32_t    chain_ib_size_ib2;

	/* section of non chained ib part */
	uint32_t    preamble_begin_ib1;
	uint32_t    preamble_begin_ib2;
	uint32_t    preamble_end_ib1;
	uint32_t    preamble_end_ib2;

	/* section of chained ib */
	uint32_t    chain_ib_pream_addrlo_ib1;
	uint32_t    chain_ib_pream_addrlo_ib2;
	uint32_t    chain_ib_pream_addrhi_ib1;
	uint32_t    chain_ib_pream_addrhi_ib2;

	/* section of non chained ib part */
	uint32_t    draw_indirect_baseLo;
	uint32_t    draw_indirect_baseHi;
	uint32_t    disp_indirect_baseLo;
	uint32_t    disp_indirect_baseHi;
	uint32_t    gds_backup_addrlo;
	uint32_t    gds_backup_addrhi;
	uint32_t    index_base_addrlo;
	uint32_t    index_base_addrhi;
	uint32_t    sample_cntl;
}; /* Total of 27 DWORD */

struct vi_gfx_meta_data {
	/* 4 DWORD, address must be 4KB aligned */
	struct vi_ce_ib_state        ce_payload;
	uint32_t                     reserved1[60];
	/* 17 DWORD, address must be 64B aligned */
	struct vi_de_ib_state        de_payload;
	/* PFP IB base address which get pre-empted */
	uint32_t                     DeIbBaseAddrLo;
	uint32_t                     DeIbBaseAddrHi;
	uint32_t                     reserved2[941];
}; /* Total of 4K Bytes */

struct vi_gfx_meta_data_chained_ib {
	/* 10 DWORD, address must be 4KB aligned */
	struct vi_ce_ib_state_chained_ib       ce_payload;
	uint32_t                               reserved1[54];
	/* 27 DWORD, address must be 64B aligned */
	struct vi_de_ib_state_chained_ib       de_payload;
	/* PFP IB base address which get pre-empted */
	uint32_t                               DeIbBaseAddrLo;
	uint32_t                               DeIbBaseAddrHi;
	uint32_t                               reserved2[931];
}; /* Total of 4K Bytes */

#endif /* VI_STRUCTS_H_ */
