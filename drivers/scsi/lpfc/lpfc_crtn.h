/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

typedef int (*node_filter)(struct lpfc_nodelist *, void *);

struct fc_rport;
struct fc_frame_header;
void lpfc_down_link(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_sli_read_link_ste(struct lpfc_hba *);
void lpfc_dump_mem(struct lpfc_hba *, LPFC_MBOXQ_t *, uint16_t, uint16_t);
void lpfc_dump_wakeup_param(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_dump_static_vport(struct lpfc_hba *, LPFC_MBOXQ_t *, uint16_t);
int lpfc_sli4_dump_cfg_rg23(struct lpfc_hba *, struct lpfcMboxq *);
void lpfc_read_nv(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_config_async(struct lpfc_hba *, LPFC_MBOXQ_t *, uint32_t);

void lpfc_heart_beat(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_read_topology(struct lpfc_hba *, LPFC_MBOXQ_t *, struct lpfc_dmabuf *);
void lpfc_clear_la(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_issue_clear_la(struct lpfc_hba *, struct lpfc_vport *);
void lpfc_config_link(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_config_msi(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_read_sparam(struct lpfc_hba *, LPFC_MBOXQ_t *, int);
void lpfc_read_config(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_read_lnk_stat(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_reg_rpi(struct lpfc_hba *, uint16_t, uint32_t, uint8_t *,
		 LPFC_MBOXQ_t *, uint16_t);
void lpfc_set_var(struct lpfc_hba *, LPFC_MBOXQ_t *, uint32_t, uint32_t);
void lpfc_unreg_login(struct lpfc_hba *, uint16_t, uint32_t, LPFC_MBOXQ_t *);
void lpfc_unreg_did(struct lpfc_hba *, uint16_t, uint32_t, LPFC_MBOXQ_t *);
void lpfc_sli4_unreg_all_rpis(struct lpfc_vport *);

void lpfc_reg_vpi(struct lpfc_vport *, LPFC_MBOXQ_t *);
void lpfc_register_new_vport(struct lpfc_hba *, struct lpfc_vport *,
			struct lpfc_nodelist *);
void lpfc_unreg_vpi(struct lpfc_hba *, uint16_t, LPFC_MBOXQ_t *);
void lpfc_init_link(struct lpfc_hba *, LPFC_MBOXQ_t *, uint32_t, uint32_t);
void lpfc_request_features(struct lpfc_hba *, struct lpfcMboxq *);
void lpfc_supported_pages(struct lpfcMboxq *);
void lpfc_pc_sli4_params(struct lpfcMboxq *);
int lpfc_pc_sli4_params_get(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_sli4_mbox_rsrc_extent(struct lpfc_hba *, struct lpfcMboxq *,
			   uint16_t, uint16_t, bool);
int lpfc_get_sli4_parameters(struct lpfc_hba *, LPFC_MBOXQ_t *);
struct lpfc_vport *lpfc_find_vport_by_did(struct lpfc_hba *, uint32_t);
void lpfc_cleanup_rcv_buffers(struct lpfc_vport *);
void lpfc_rcv_seq_check_edtov(struct lpfc_vport *);
void lpfc_cleanup_rpis(struct lpfc_vport *, int);
void lpfc_cleanup_pending_mbox(struct lpfc_vport *);
int lpfc_linkdown(struct lpfc_hba *);
void lpfc_linkdown_port(struct lpfc_vport *);
void lpfc_port_link_failure(struct lpfc_vport *);
void lpfc_mbx_cmpl_read_topology(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_init_vpi_cmpl(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_cancel_all_vport_retry_delay_timer(struct lpfc_hba *);
void lpfc_retry_pport_discovery(struct lpfc_hba *);
void lpfc_release_rpi(struct lpfc_hba *, struct lpfc_vport *, uint16_t);

void lpfc_mbx_cmpl_local_config_link(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_reg_login(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_dflt_rpi(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_fabric_reg_login(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_ns_reg_login(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_fdmi_reg_login(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_reg_vfi(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_unregister_vfi_cmpl(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_enqueue_node(struct lpfc_vport *, struct lpfc_nodelist *);
void lpfc_dequeue_node(struct lpfc_vport *, struct lpfc_nodelist *);
struct lpfc_nodelist *lpfc_enable_node(struct lpfc_vport *,
					struct lpfc_nodelist *, int);
void lpfc_nlp_set_state(struct lpfc_vport *, struct lpfc_nodelist *, int);
void lpfc_drop_node(struct lpfc_vport *, struct lpfc_nodelist *);
void lpfc_set_disctmo(struct lpfc_vport *);
int  lpfc_can_disctmo(struct lpfc_vport *);
int  lpfc_unreg_rpi(struct lpfc_vport *, struct lpfc_nodelist *);
void lpfc_unreg_all_rpis(struct lpfc_vport *);
void lpfc_unreg_hba_rpis(struct lpfc_hba *);
void lpfc_unreg_default_rpis(struct lpfc_vport *);
void lpfc_issue_reg_vpi(struct lpfc_hba *, struct lpfc_vport *);

int lpfc_check_sli_ndlp(struct lpfc_hba *, struct lpfc_sli_ring *,
			struct lpfc_iocbq *, struct lpfc_nodelist *);
void lpfc_nlp_init(struct lpfc_vport *, struct lpfc_nodelist *, uint32_t);
struct lpfc_nodelist *lpfc_nlp_get(struct lpfc_nodelist *);
int  lpfc_nlp_put(struct lpfc_nodelist *);
int  lpfc_nlp_not_used(struct lpfc_nodelist *ndlp);
struct lpfc_nodelist *lpfc_setup_disc_node(struct lpfc_vport *, uint32_t);
void lpfc_disc_list_loopmap(struct lpfc_vport *);
void lpfc_disc_start(struct lpfc_vport *);
void lpfc_cleanup_discovery_resources(struct lpfc_vport *);
void lpfc_cleanup(struct lpfc_vport *);
void lpfc_disc_timeout(unsigned long);

int lpfc_unregister_fcf_prep(struct lpfc_hba *);
struct lpfc_nodelist *__lpfc_findnode_rpi(struct lpfc_vport *, uint16_t);
struct lpfc_nodelist *lpfc_findnode_rpi(struct lpfc_vport *, uint16_t);
void lpfc_worker_wake_up(struct lpfc_hba *);
int lpfc_workq_post_event(struct lpfc_hba *, void *, void *, uint32_t);
int lpfc_do_work(void *);
int lpfc_disc_state_machine(struct lpfc_vport *, struct lpfc_nodelist *, void *,
			    uint32_t);

void lpfc_do_scr_ns_plogi(struct lpfc_hba *, struct lpfc_vport *);
int lpfc_check_sparm(struct lpfc_vport *, struct lpfc_nodelist *,
		     struct serv_parm *, uint32_t, int);
int lpfc_els_abort(struct lpfc_hba *, struct lpfc_nodelist *);
void lpfc_more_plogi(struct lpfc_vport *);
void lpfc_more_adisc(struct lpfc_vport *);
void lpfc_end_rscn(struct lpfc_vport *);
int lpfc_els_chk_latt(struct lpfc_vport *);
int lpfc_els_abort_flogi(struct lpfc_hba *);
int lpfc_initial_flogi(struct lpfc_vport *);
void lpfc_issue_init_vfi(struct lpfc_vport *);
int lpfc_initial_fdisc(struct lpfc_vport *);
int lpfc_issue_els_plogi(struct lpfc_vport *, uint32_t, uint8_t);
int lpfc_issue_els_prli(struct lpfc_vport *, struct lpfc_nodelist *, uint8_t);
int lpfc_issue_els_adisc(struct lpfc_vport *, struct lpfc_nodelist *, uint8_t);
int lpfc_issue_els_logo(struct lpfc_vport *, struct lpfc_nodelist *, uint8_t);
int lpfc_issue_els_npiv_logo(struct lpfc_vport *, struct lpfc_nodelist *);
int lpfc_issue_els_scr(struct lpfc_vport *, uint32_t, uint8_t);
int lpfc_issue_fabric_reglogin(struct lpfc_vport *);
int lpfc_els_free_iocb(struct lpfc_hba *, struct lpfc_iocbq *);
int lpfc_ct_free_iocb(struct lpfc_hba *, struct lpfc_iocbq *);
int lpfc_els_rsp_acc(struct lpfc_vport *, uint32_t, struct lpfc_iocbq *,
		     struct lpfc_nodelist *, LPFC_MBOXQ_t *);
int lpfc_els_rsp_reject(struct lpfc_vport *, uint32_t, struct lpfc_iocbq *,
			struct lpfc_nodelist *, LPFC_MBOXQ_t *);
int lpfc_els_rsp_adisc_acc(struct lpfc_vport *, struct lpfc_iocbq *,
			   struct lpfc_nodelist *);
int lpfc_els_rsp_prli_acc(struct lpfc_vport *, struct lpfc_iocbq *,
			  struct lpfc_nodelist *);
void lpfc_cancel_retry_delay_tmo(struct lpfc_vport *, struct lpfc_nodelist *);
void lpfc_els_retry_delay(unsigned long);
void lpfc_els_retry_delay_handler(struct lpfc_nodelist *);
void lpfc_els_unsol_event(struct lpfc_hba *, struct lpfc_sli_ring *,
			  struct lpfc_iocbq *);
int lpfc_els_handle_rscn(struct lpfc_vport *);
void lpfc_els_flush_rscn(struct lpfc_vport *);
int lpfc_rscn_payload_check(struct lpfc_vport *, uint32_t);
void lpfc_els_flush_all_cmd(struct lpfc_hba *);
void lpfc_els_flush_cmd(struct lpfc_vport *);
int lpfc_els_disc_adisc(struct lpfc_vport *);
int lpfc_els_disc_plogi(struct lpfc_vport *);
void lpfc_els_timeout(unsigned long);
void lpfc_els_timeout_handler(struct lpfc_vport *);
struct lpfc_iocbq *lpfc_prep_els_iocb(struct lpfc_vport *, uint8_t, uint16_t,
				      uint8_t, struct lpfc_nodelist *,
				      uint32_t, uint32_t);
void lpfc_hb_timeout_handler(struct lpfc_hba *);

void lpfc_ct_unsol_event(struct lpfc_hba *, struct lpfc_sli_ring *,
			 struct lpfc_iocbq *);
int lpfc_ct_handle_unsol_abort(struct lpfc_hba *, struct hbq_dmabuf *);
int lpfc_issue_gidft(struct lpfc_vport *vport);
int lpfc_get_gidft_type(struct lpfc_vport *vport, struct lpfc_iocbq *iocbq);
int lpfc_ns_cmd(struct lpfc_vport *, int, uint8_t, uint32_t);
int lpfc_fdmi_cmd(struct lpfc_vport *, struct lpfc_nodelist *, int, uint32_t);
void lpfc_fdmi_num_disc_check(struct lpfc_vport *);
void lpfc_delayed_disc_tmo(unsigned long);
void lpfc_delayed_disc_timeout_handler(struct lpfc_vport *);

int lpfc_config_port_prep(struct lpfc_hba *);
void lpfc_update_vport_wwn(struct lpfc_vport *vport);
int lpfc_config_port_post(struct lpfc_hba *);
int lpfc_hba_down_prep(struct lpfc_hba *);
int lpfc_hba_down_post(struct lpfc_hba *);
void lpfc_hba_init(struct lpfc_hba *, uint32_t *);
int lpfc_post_buffer(struct lpfc_hba *, struct lpfc_sli_ring *, int);
void lpfc_decode_firmware_rev(struct lpfc_hba *, char *, int);
int lpfc_online(struct lpfc_hba *);
void lpfc_unblock_mgmt_io(struct lpfc_hba *);
void lpfc_offline_prep(struct lpfc_hba *, int);
void lpfc_offline(struct lpfc_hba *);
void lpfc_reset_hba(struct lpfc_hba *);
int lpfc_emptyq_wait(struct lpfc_hba *phba, struct list_head *hd,
			spinlock_t *slock);

int lpfc_fof_queue_create(struct lpfc_hba *);
int lpfc_fof_queue_setup(struct lpfc_hba *);
int lpfc_fof_queue_destroy(struct lpfc_hba *);
irqreturn_t lpfc_sli4_fof_intr_handler(int, void *);

int lpfc_sli_setup(struct lpfc_hba *);
int lpfc_sli4_setup(struct lpfc_hba *phba);
void lpfc_sli_queue_init(struct lpfc_hba *phba);
void lpfc_sli4_queue_init(struct lpfc_hba *phba);
struct lpfc_sli_ring *lpfc_sli4_calc_ring(struct lpfc_hba *phba,
					  struct lpfc_iocbq *iocbq);

void lpfc_handle_eratt(struct lpfc_hba *);
void lpfc_handle_latt(struct lpfc_hba *);
irqreturn_t lpfc_sli_intr_handler(int, void *);
irqreturn_t lpfc_sli_sp_intr_handler(int, void *);
irqreturn_t lpfc_sli_fp_intr_handler(int, void *);
irqreturn_t lpfc_sli4_intr_handler(int, void *);
irqreturn_t lpfc_sli4_hba_intr_handler(int, void *);

void lpfc_read_rev(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_sli4_swap_str(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_config_ring(struct lpfc_hba *, int, LPFC_MBOXQ_t *);
void lpfc_config_port(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_kill_board(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbox_put(struct lpfc_hba *, LPFC_MBOXQ_t *);
LPFC_MBOXQ_t *lpfc_mbox_get(struct lpfc_hba *);
void __lpfc_mbox_cmpl_put(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbox_cmpl_put(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_mbox_cmd_check(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_mbox_dev_check(struct lpfc_hba *);
int lpfc_mbox_tmo_val(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_init_vfi(struct lpfcMboxq *, struct lpfc_vport *);
void lpfc_reg_vfi(struct lpfcMboxq *, struct lpfc_vport *, dma_addr_t);
void lpfc_init_vpi(struct lpfc_hba *, struct lpfcMboxq *, uint16_t);
void lpfc_unreg_vfi(struct lpfcMboxq *, struct lpfc_vport *);
void lpfc_reg_fcfi(struct lpfc_hba *, struct lpfcMboxq *);
void lpfc_unreg_fcfi(struct lpfcMboxq *, uint16_t);
void lpfc_resume_rpi(struct lpfcMboxq *, struct lpfc_nodelist *);
int lpfc_check_pending_fcoe_event(struct lpfc_hba *, uint8_t);
void lpfc_issue_init_vpi(struct lpfc_vport *);

void lpfc_config_hbq(struct lpfc_hba *, uint32_t, struct lpfc_hbq_init *,
	uint32_t , LPFC_MBOXQ_t *);
struct hbq_dmabuf *lpfc_els_hbq_alloc(struct lpfc_hba *);
void lpfc_els_hbq_free(struct lpfc_hba *, struct hbq_dmabuf *);
struct hbq_dmabuf *lpfc_sli4_rb_alloc(struct lpfc_hba *);
void lpfc_sli4_rb_free(struct lpfc_hba *, struct hbq_dmabuf *);
void lpfc_sli4_build_dflt_fcf_record(struct lpfc_hba *, struct fcf_record *,
			uint16_t);
int lpfc_sli4_rq_put(struct lpfc_queue *hq, struct lpfc_queue *dq,
		     struct lpfc_rqe *hrqe, struct lpfc_rqe *drqe);
int lpfc_post_rq_buffer(struct lpfc_hba *phba, struct lpfc_queue *hq,
			struct lpfc_queue *dq, int count);
int lpfc_free_rq_buffer(struct lpfc_hba *phba, struct lpfc_queue *hq);
void lpfc_unregister_fcf(struct lpfc_hba *);
void lpfc_unregister_fcf_rescan(struct lpfc_hba *);
void lpfc_unregister_unused_fcf(struct lpfc_hba *);
int lpfc_sli4_redisc_fcf_table(struct lpfc_hba *);
void lpfc_fcf_redisc_wait_start_timer(struct lpfc_hba *);
void lpfc_sli4_fcf_dead_failthrough(struct lpfc_hba *);
uint16_t lpfc_sli4_fcf_rr_next_index_get(struct lpfc_hba *);
void lpfc_sli4_set_fcf_flogi_fail(struct lpfc_hba *, uint16_t);
int lpfc_sli4_fcf_rr_index_set(struct lpfc_hba *, uint16_t);
void lpfc_sli4_fcf_rr_index_clear(struct lpfc_hba *, uint16_t);
int lpfc_sli4_fcf_rr_next_proc(struct lpfc_vport *, uint16_t);
void lpfc_sli4_clear_fcf_rr_bmask(struct lpfc_hba *);

int lpfc_mem_alloc(struct lpfc_hba *, int align);
int lpfc_mem_alloc_active_rrq_pool_s4(struct lpfc_hba *);
void lpfc_mem_free(struct lpfc_hba *);
void lpfc_mem_free_all(struct lpfc_hba *);
void lpfc_stop_vport_timers(struct lpfc_vport *);

void lpfc_poll_timeout(unsigned long ptr);
void lpfc_poll_start_timer(struct lpfc_hba *);
void lpfc_poll_eratt(unsigned long);
int
lpfc_sli_handle_fast_ring_event(struct lpfc_hba *,
			struct lpfc_sli_ring *, uint32_t);

struct lpfc_iocbq *__lpfc_sli_get_iocbq(struct lpfc_hba *);
struct lpfc_iocbq * lpfc_sli_get_iocbq(struct lpfc_hba *);
void lpfc_sli_release_iocbq(struct lpfc_hba *, struct lpfc_iocbq *);
uint16_t lpfc_sli_next_iotag(struct lpfc_hba *, struct lpfc_iocbq *);
void lpfc_sli_cancel_iocbs(struct lpfc_hba *, struct list_head *, uint32_t,
			   uint32_t);
void lpfc_sli_wake_mbox_wait(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_selective_reset(struct lpfc_hba *);
void lpfc_reset_barrier(struct lpfc_hba *);
int lpfc_sli_brdready(struct lpfc_hba *, uint32_t);
int lpfc_sli_brdkill(struct lpfc_hba *);
int lpfc_sli_brdreset(struct lpfc_hba *);
int lpfc_sli_brdrestart(struct lpfc_hba *);
int lpfc_sli_hba_setup(struct lpfc_hba *);
int lpfc_sli_config_port(struct lpfc_hba *, int);
int lpfc_sli_host_down(struct lpfc_vport *);
int lpfc_sli_hba_down(struct lpfc_hba *);
int lpfc_sli_issue_mbox(struct lpfc_hba *, LPFC_MBOXQ_t *, uint32_t);
int lpfc_sli_handle_mb_event(struct lpfc_hba *);
void lpfc_sli_mbox_sys_shutdown(struct lpfc_hba *, int);
int lpfc_sli_check_eratt(struct lpfc_hba *);
void lpfc_sli_handle_slow_ring_event(struct lpfc_hba *,
				    struct lpfc_sli_ring *, uint32_t);
void lpfc_sli4_handle_received_buffer(struct lpfc_hba *, struct hbq_dmabuf *);
void lpfc_sli_def_mbox_cmpl(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_sli4_unreg_rpi_cmpl_clr(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_sli_issue_iocb(struct lpfc_hba *, uint32_t,
			struct lpfc_iocbq *, uint32_t);
int lpfc_sli4_issue_wqe(struct lpfc_hba *phba, uint32_t rnum,
			struct lpfc_iocbq *iocbq);
struct lpfc_sglq *__lpfc_clear_active_sglq(struct lpfc_hba *phba, uint16_t xri);
void lpfc_sli_pcimem_bcopy(void *, void *, uint32_t);
void lpfc_sli_bemem_bcopy(void *, void *, uint32_t);
void lpfc_sli_abort_iocb_ring(struct lpfc_hba *, struct lpfc_sli_ring *);
void lpfc_sli_abort_fcp_rings(struct lpfc_hba *phba);
void lpfc_sli_hba_iocb_abort(struct lpfc_hba *);
void lpfc_sli_flush_fcp_rings(struct lpfc_hba *);
int lpfc_sli_ringpostbuf_put(struct lpfc_hba *, struct lpfc_sli_ring *,
			     struct lpfc_dmabuf *);
struct lpfc_dmabuf *lpfc_sli_ringpostbuf_get(struct lpfc_hba *,
					     struct lpfc_sli_ring *,
					     dma_addr_t);

uint32_t lpfc_sli_get_buffer_tag(struct lpfc_hba *);
struct lpfc_dmabuf * lpfc_sli_ring_taggedbuf_get(struct lpfc_hba *,
			struct lpfc_sli_ring *, uint32_t );

int lpfc_sli_hbq_count(void);
int lpfc_sli_hbqbuf_add_hbqs(struct lpfc_hba *, uint32_t);
void lpfc_sli_hbqbuf_free_all(struct lpfc_hba *);
int lpfc_sli_hbq_size(void);
int lpfc_sli_issue_abort_iotag(struct lpfc_hba *, struct lpfc_sli_ring *,
			       struct lpfc_iocbq *);
int lpfc_sli_sum_iocb(struct lpfc_vport *, uint16_t, uint64_t, lpfc_ctx_cmd);
int lpfc_sli_abort_iocb(struct lpfc_vport *, struct lpfc_sli_ring *, uint16_t,
			uint64_t, lpfc_ctx_cmd);
int
lpfc_sli_abort_taskmgmt(struct lpfc_vport *, struct lpfc_sli_ring *,
			uint16_t, uint64_t, lpfc_ctx_cmd);

void lpfc_mbox_timeout(unsigned long);
void lpfc_mbox_timeout_handler(struct lpfc_hba *);

struct lpfc_nodelist *lpfc_findnode_did(struct lpfc_vport *, uint32_t);
struct lpfc_nodelist *lpfc_findnode_wwpn(struct lpfc_vport *,
					 struct lpfc_name *);

int lpfc_sli_issue_mbox_wait(struct lpfc_hba *, LPFC_MBOXQ_t *, uint32_t);

int lpfc_sli_issue_iocb_wait(struct lpfc_hba *, uint32_t,
			     struct lpfc_iocbq *, struct lpfc_iocbq *,
			     uint32_t);
void lpfc_sli_abort_fcp_cmpl(struct lpfc_hba *, struct lpfc_iocbq *,
			     struct lpfc_iocbq *);

void lpfc_sli_free_hbq(struct lpfc_hba *, struct hbq_dmabuf *);

void *lpfc_mbuf_alloc(struct lpfc_hba *, int, dma_addr_t *);
void __lpfc_mbuf_free(struct lpfc_hba *, void *, dma_addr_t);
void lpfc_mbuf_free(struct lpfc_hba *, void *, dma_addr_t);

void lpfc_in_buf_free(struct lpfc_hba *, struct lpfc_dmabuf *);
/* Function prototypes. */
const char* lpfc_info(struct Scsi_Host *);
int lpfc_scan_finished(struct Scsi_Host *, unsigned long);

int lpfc_init_api_table_setup(struct lpfc_hba *, uint8_t);
int lpfc_sli_api_table_setup(struct lpfc_hba *, uint8_t);
int lpfc_scsi_api_table_setup(struct lpfc_hba *, uint8_t);
int lpfc_mbox_api_table_setup(struct lpfc_hba *, uint8_t);
int lpfc_api_table_setup(struct lpfc_hba *, uint8_t);

void lpfc_get_cfgparam(struct lpfc_hba *);
void lpfc_get_vport_cfgparam(struct lpfc_vport *);
int lpfc_alloc_sysfs_attr(struct lpfc_vport *);
void lpfc_free_sysfs_attr(struct lpfc_vport *);
extern struct device_attribute *lpfc_hba_attrs[];
extern struct device_attribute *lpfc_vport_attrs[];
extern struct scsi_host_template lpfc_template;
extern struct scsi_host_template lpfc_template_s3;
extern struct scsi_host_template lpfc_template_nvme;
extern struct scsi_host_template lpfc_vport_template;
extern struct fc_function_template lpfc_transport_functions;
extern struct fc_function_template lpfc_vport_transport_functions;

int  lpfc_vport_symbolic_node_name(struct lpfc_vport *, char *, size_t);
int  lpfc_vport_symbolic_port_name(struct lpfc_vport *, char *,	size_t);
void lpfc_terminate_rport_io(struct fc_rport *);
void lpfc_dev_loss_tmo_callbk(struct fc_rport *rport);

struct lpfc_vport *lpfc_create_port(struct lpfc_hba *, int, struct device *);
int  lpfc_vport_disable(struct fc_vport *fc_vport, bool disable);
int lpfc_mbx_unreg_vpi(struct lpfc_vport *);
void destroy_port(struct lpfc_vport *);
int lpfc_get_instance(void);
void lpfc_host_attrib_init(struct Scsi_Host *);

extern void lpfc_debugfs_initialize(struct lpfc_vport *);
extern void lpfc_debugfs_terminate(struct lpfc_vport *);
extern void lpfc_debugfs_disc_trc(struct lpfc_vport *, int, char *, uint32_t,
	uint32_t, uint32_t);
extern void lpfc_debugfs_slow_ring_trc(struct lpfc_hba *, char *, uint32_t,
	uint32_t, uint32_t);
extern struct lpfc_hbq_init *lpfc_hbq_defs[];

/* SLI4 if_type 2 externs. */
int lpfc_sli4_alloc_resource_identifiers(struct lpfc_hba *);
int lpfc_sli4_dealloc_resource_identifiers(struct lpfc_hba *);
int lpfc_sli4_get_allocated_extnts(struct lpfc_hba *, uint16_t,
				   uint16_t *, uint16_t *);
int lpfc_sli4_get_avail_extnt_rsrc(struct lpfc_hba *, uint16_t,
					  uint16_t *, uint16_t *);

/* externs BlockGuard */
extern char *_dump_buf_data;
extern unsigned long _dump_buf_data_order;
extern char *_dump_buf_dif;
extern unsigned long _dump_buf_dif_order;
extern spinlock_t _dump_buf_lock;
extern int _dump_buf_done;
extern spinlock_t pgcnt_lock;
extern unsigned int pgcnt;
extern unsigned int lpfc_fcp_look_ahead;

/* Interface exported by fabric iocb scheduler */
void lpfc_fabric_abort_nport(struct lpfc_nodelist *);
void lpfc_fabric_abort_hba(struct lpfc_hba *);
void lpfc_fabric_block_timeout(unsigned long);
void lpfc_unblock_fabric_iocbs(struct lpfc_hba *);
void lpfc_rampdown_queue_depth(struct lpfc_hba *);
void lpfc_ramp_down_queue_handler(struct lpfc_hba *);
void lpfc_scsi_dev_block(struct lpfc_hba *);

void
lpfc_send_els_failure_event(struct lpfc_hba *, struct lpfc_iocbq *,
				struct lpfc_iocbq *);
struct lpfc_fast_path_event *lpfc_alloc_fast_evt(struct lpfc_hba *);
void lpfc_free_fast_evt(struct lpfc_hba *, struct lpfc_fast_path_event *);
void lpfc_create_static_vport(struct lpfc_hba *);
void lpfc_stop_hba_timers(struct lpfc_hba *);
void lpfc_stop_port(struct lpfc_hba *);
void __lpfc_sli4_stop_fcf_redisc_wait_timer(struct lpfc_hba *);
void lpfc_sli4_stop_fcf_redisc_wait_timer(struct lpfc_hba *);
void lpfc_parse_fcoe_conf(struct lpfc_hba *, uint8_t *, uint32_t);
int lpfc_parse_vpd(struct lpfc_hba *, uint8_t *, int);
void lpfc_start_fdiscs(struct lpfc_hba *phba);
struct lpfc_vport *lpfc_find_vport_by_vpid(struct lpfc_hba *, uint16_t);
struct lpfc_sglq *__lpfc_get_active_sglq(struct lpfc_hba *, uint16_t);
#define ScsiResult(host_code, scsi_code) (((host_code) << 16) | scsi_code)
#define HBA_EVENT_RSCN                   5
#define HBA_EVENT_LINK_UP                2
#define HBA_EVENT_LINK_DOWN              3

/* functions to support SGIOv4/bsg interface */
int lpfc_bsg_request(struct bsg_job *);
int lpfc_bsg_timeout(struct bsg_job *);
int lpfc_bsg_ct_unsol_event(struct lpfc_hba *, struct lpfc_sli_ring *,
			     struct lpfc_iocbq *);
int lpfc_bsg_ct_unsol_abort(struct lpfc_hba *, struct hbq_dmabuf *);
void __lpfc_sli_ringtx_put(struct lpfc_hba *, struct lpfc_sli_ring *,
	struct lpfc_iocbq *);
struct lpfc_iocbq *lpfc_sli_ringtx_get(struct lpfc_hba *,
	struct lpfc_sli_ring *);
int __lpfc_sli_issue_iocb(struct lpfc_hba *, uint32_t,
	struct lpfc_iocbq *, uint32_t);
uint32_t lpfc_drain_txq(struct lpfc_hba *);
void lpfc_clr_rrq_active(struct lpfc_hba *, uint16_t, struct lpfc_node_rrq *);
int lpfc_test_rrq_active(struct lpfc_hba *, struct lpfc_nodelist *, uint16_t);
void lpfc_handle_rrq_active(struct lpfc_hba *);
int lpfc_send_rrq(struct lpfc_hba *, struct lpfc_node_rrq *);
int lpfc_set_rrq_active(struct lpfc_hba *, struct lpfc_nodelist *,
	uint16_t, uint16_t, uint16_t);
uint16_t lpfc_sli4_xri_inrange(struct lpfc_hba *, uint16_t);
void lpfc_cleanup_vports_rrqs(struct lpfc_vport *, struct lpfc_nodelist *);
struct lpfc_node_rrq *lpfc_get_active_rrq(struct lpfc_vport *, uint16_t,
	uint32_t);
void lpfc_idiag_mbxacc_dump_bsg_mbox(struct lpfc_hba *, enum nemb_type,
	enum mbox_type, enum dma_type, enum sta_type,
	struct lpfc_dmabuf *, uint32_t);
void lpfc_idiag_mbxacc_dump_issue_mbox(struct lpfc_hba *, MAILBOX_t *);
int lpfc_wr_object(struct lpfc_hba *, struct list_head *, uint32_t, uint32_t *);
/* functions to support SR-IOV */
int lpfc_sli_probe_sriov_nr_virtfn(struct lpfc_hba *, int);
uint16_t lpfc_sli_sriov_nr_virtfn_get(struct lpfc_hba *);
int lpfc_sli4_queue_create(struct lpfc_hba *);
void lpfc_sli4_queue_destroy(struct lpfc_hba *);
void lpfc_sli4_abts_err_handler(struct lpfc_hba *, struct lpfc_nodelist *,
				struct sli4_wcqe_xri_aborted *);
void lpfc_sli_abts_recover_port(struct lpfc_vport *,
				struct lpfc_nodelist *);
int lpfc_hba_init_link_fc_topology(struct lpfc_hba *, uint32_t, uint32_t);
int lpfc_issue_reg_vfi(struct lpfc_vport *);
int lpfc_issue_unreg_vfi(struct lpfc_vport *);
int lpfc_selective_reset(struct lpfc_hba *);
int lpfc_sli4_read_config(struct lpfc_hba *);
void lpfc_sli4_node_prep(struct lpfc_hba *);
int lpfc_sli4_els_sgl_update(struct lpfc_hba *phba);
int lpfc_sli4_scsi_sgl_update(struct lpfc_hba *phba);
int lpfc_sli4_nvme_sgl_update(struct lpfc_hba *phba);
void lpfc_free_sgl_list(struct lpfc_hba *, struct list_head *);
uint32_t lpfc_sli_port_speed_get(struct lpfc_hba *);
int lpfc_sli4_request_firmware_update(struct lpfc_hba *, uint8_t);
void lpfc_sli4_offline_eratt(struct lpfc_hba *);

struct lpfc_device_data *lpfc_create_device_data(struct lpfc_hba *,
						struct lpfc_name *,
						struct lpfc_name *,
						uint64_t, uint32_t,  bool);
void lpfc_delete_device_data(struct lpfc_hba *, struct lpfc_device_data*);
struct lpfc_device_data *__lpfc_get_device_data(struct lpfc_hba *,
					struct list_head *list,
					struct lpfc_name *,
					struct lpfc_name *, uint64_t);
bool lpfc_enable_oas_lun(struct lpfc_hba *, struct lpfc_name *,
			 struct lpfc_name *, uint64_t, uint8_t);
bool lpfc_disable_oas_lun(struct lpfc_hba *, struct lpfc_name *,
			  struct lpfc_name *, uint64_t, uint8_t);
bool lpfc_find_next_oas_lun(struct lpfc_hba *, struct lpfc_name *,
			    struct lpfc_name *, uint64_t *, struct lpfc_name *,
			    struct lpfc_name *, uint64_t *,
			    uint32_t *, uint32_t *);
int lpfc_sli4_dump_page_a0(struct lpfc_hba *phba, struct lpfcMboxq *mbox);
void lpfc_mbx_cmpl_rdp_page_a0(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmb);

/* NVME interfaces. */
void lpfc_nvme_unregister_port(struct lpfc_vport *vport,
			struct lpfc_nodelist *ndlp);
int lpfc_nvme_register_port(struct lpfc_vport *vport,
			struct lpfc_nodelist *ndlp);
int lpfc_nvme_create_localport(struct lpfc_vport *vport);
void lpfc_nvme_destroy_localport(struct lpfc_vport *vport);
void lpfc_nvme_update_localport(struct lpfc_vport *vport);
void lpfc_nvme_mod_param_dep(struct lpfc_hba *phba);
void lpfc_nvme_abort_fcreq_cmpl(struct lpfc_hba *phba,
				struct lpfc_iocbq *cmdiocb,
				struct lpfc_wcqe_complete *abts_cmpl);
