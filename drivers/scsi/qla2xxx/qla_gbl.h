/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 */
#ifndef __QLA_GBL_H
#define	__QLA_GBL_H

#include <linux/interrupt.h>

/*
 * Global Function Prototypes in qla_init.c source file.
 */
int  qla2x00_alloc_fce_trace(scsi_qla_host_t *);
void qla2x00_free_fce_trace(struct qla_hw_data *ha);
void qla_enable_fce_trace(scsi_qla_host_t *);
extern int qla2x00_initialize_adapter(scsi_qla_host_t *);
extern int qla24xx_post_prli_work(struct scsi_qla_host *vha, fc_port_t *fcport);

extern int qla2100_pci_config(struct scsi_qla_host *);
extern int qla2300_pci_config(struct scsi_qla_host *);
extern int qla24xx_pci_config(scsi_qla_host_t *);
extern int qla25xx_pci_config(scsi_qla_host_t *);
extern int qla2x00_reset_chip(struct scsi_qla_host *);
extern int qla24xx_reset_chip(struct scsi_qla_host *);
extern int qla2x00_chip_diag(struct scsi_qla_host *);
extern int qla24xx_chip_diag(struct scsi_qla_host *);
extern void qla2x00_config_rings(struct scsi_qla_host *);
extern void qla24xx_config_rings(struct scsi_qla_host *);
extern int qla2x00_reset_adapter(struct scsi_qla_host *);
extern int qla24xx_reset_adapter(struct scsi_qla_host *);
extern int qla2x00_nvram_config(struct scsi_qla_host *);
extern int qla24xx_nvram_config(struct scsi_qla_host *);
extern int qla81xx_nvram_config(struct scsi_qla_host *);
extern void qla2x00_update_fw_options(struct scsi_qla_host *);
extern void qla24xx_update_fw_options(scsi_qla_host_t *);

extern int qla2x00_load_risc(struct scsi_qla_host *, uint32_t *);
extern int qla24xx_load_risc(scsi_qla_host_t *, uint32_t *);
extern int qla81xx_load_risc(scsi_qla_host_t *, uint32_t *);

extern int qla2x00_perform_loop_resync(scsi_qla_host_t *);
extern int qla2x00_loop_resync(scsi_qla_host_t *);
extern void qla2x00_clear_loop_id(fc_port_t *fcport);

extern int qla2x00_fabric_login(scsi_qla_host_t *, fc_port_t *, uint16_t *);
extern int qla2x00_local_device_login(scsi_qla_host_t *, fc_port_t *);

extern int qla24xx_els_dcmd_iocb(scsi_qla_host_t *, int, port_id_t);
extern int qla24xx_els_dcmd2_iocb(scsi_qla_host_t *, int, fc_port_t *);
extern void qla2x00_els_dcmd2_free(scsi_qla_host_t *vha,
				   struct els_plogi *els_plogi);

extern int qla2x00_abort_isp(scsi_qla_host_t *);
extern void qla2x00_abort_isp_cleanup(scsi_qla_host_t *);
extern void qla2x00_quiesce_io(scsi_qla_host_t *);

extern void qla2x00_update_fcport(scsi_qla_host_t *, fc_port_t *);
void qla_register_fcport_fn(struct work_struct *);
extern void qla2x00_alloc_fw_dump(scsi_qla_host_t *);
extern void qla2x00_try_to_stop_firmware(scsi_qla_host_t *);

extern int qla2x00_get_thermal_temp(scsi_qla_host_t *, uint16_t *);

extern void qla84xx_put_chip(struct scsi_qla_host *);

extern int qla2x00_async_login(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_async_logout(struct scsi_qla_host *, fc_port_t *);
extern int qla2x00_async_prlo(struct scsi_qla_host *, fc_port_t *);
extern int qla2x00_async_adisc(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_async_tm_cmd(fc_port_t *, uint32_t, uint64_t, uint32_t);
struct qla_work_evt *qla2x00_alloc_work(struct scsi_qla_host *,
    enum qla_work_type);
extern int qla24xx_async_gnl(struct scsi_qla_host *, fc_port_t *);
int qla2x00_post_work(struct scsi_qla_host *vha, struct qla_work_evt *e);
extern void *qla2x00_alloc_iocbs_ready(struct qla_qpair *, srb_t *);
extern int qla24xx_update_fcport_fcp_prio(scsi_qla_host_t *, fc_port_t *);
extern int qla24xx_async_abort_cmd(srb_t *, bool);

extern void qla2x00_set_fcport_state(fc_port_t *fcport, int state);
extern fc_port_t *
qla2x00_alloc_fcport(scsi_qla_host_t *, gfp_t );

extern int __qla83xx_set_idc_control(scsi_qla_host_t *, uint32_t);
extern int __qla83xx_get_idc_control(scsi_qla_host_t *, uint32_t *);
extern void qla83xx_idc_audit(scsi_qla_host_t *, int);
extern int qla83xx_nic_core_reset(scsi_qla_host_t *);
extern void qla83xx_reset_ownership(scsi_qla_host_t *);
extern int qla2xxx_mctp_dump(scsi_qla_host_t *);

extern int
qla2x00_alloc_outstanding_cmds(struct qla_hw_data *, struct req_que *);
extern int qla2x00_init_rings(scsi_qla_host_t *);
extern struct qla_qpair *qla2xxx_create_qpair(struct scsi_qla_host *,
	int, int, bool);
extern int qla2xxx_delete_qpair(struct scsi_qla_host *, struct qla_qpair *);
void qla2x00_handle_rscn(scsi_qla_host_t *vha, struct event_arg *ea);
void qla24xx_handle_plogi_done_event(struct scsi_qla_host *vha,
				     struct event_arg *ea);
void qla24xx_handle_relogin_event(scsi_qla_host_t *vha,
				  struct event_arg *ea);
int qla24xx_async_gpdb(struct scsi_qla_host *, fc_port_t *, u8);
int qla24xx_async_prli(struct scsi_qla_host *, fc_port_t *);
int qla24xx_async_notify_ack(scsi_qla_host_t *, fc_port_t *,
	struct imm_ntfy_from_isp *, int);
int qla24xx_post_newsess_work(struct scsi_qla_host *, port_id_t *, u8 *, u8*,
    void *, u8);
int qla24xx_fcport_handle_login(struct scsi_qla_host *, fc_port_t *);
int qla24xx_detect_sfp(scsi_qla_host_t *);
int qla24xx_post_gpdb_work(struct scsi_qla_host *, fc_port_t *, u8);

extern void qla28xx_get_aux_images(struct scsi_qla_host *,
    struct active_regions *);
extern void qla27xx_get_active_image(struct scsi_qla_host *,
    struct active_regions *);

void qla2x00_async_prlo_done(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_post_async_prlo_work(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_post_async_prlo_done_work(struct scsi_qla_host *,
    fc_port_t *, uint16_t *);
int qla_post_iidma_work(struct scsi_qla_host *vha, fc_port_t *fcport);
void qla_do_iidma_work(struct scsi_qla_host *vha, fc_port_t *fcport);
int qla2x00_reserve_mgmt_server_loop_id(scsi_qla_host_t *);
void qla_rscn_replay(fc_port_t *fcport);
void qla24xx_free_purex_item(struct purex_item *item);
extern bool qla24xx_risc_firmware_invalid(uint32_t *);
void qla_init_iocb_limit(scsi_qla_host_t *);

void qla_edif_list_del(fc_port_t *fcport);
void qla_edif_sadb_release(struct qla_hw_data *ha);
int qla_edif_sadb_build_free_pool(struct qla_hw_data *ha);
void qla_edif_sadb_release_free_pool(struct qla_hw_data *ha);
void qla_chk_edif_rx_sa_delete_pending(scsi_qla_host_t *vha,
		srb_t *sp, struct sts_entry_24xx *sts24);
void qlt_chk_edif_rx_sa_delete_pending(scsi_qla_host_t *vha, fc_port_t *fcport,
		struct ctio7_from_24xx *ctio);
void qla2x00_release_all_sadb(struct scsi_qla_host *vha, struct fc_port *fcport);
int qla_edif_process_els(scsi_qla_host_t *vha, struct bsg_job *bsgjob);
void qla_edif_sess_down(struct scsi_qla_host *vha, struct fc_port *sess);
void qla_edif_clear_appdata(struct scsi_qla_host *vha,
			    struct fc_port *fcport);
const char *sc_to_str(uint16_t cmd);
void qla_adjust_iocb_limit(scsi_qla_host_t *vha);

/*
 * Global Data in qla_os.c source file.
 */
extern char qla2x00_version_str[];

extern struct kmem_cache *srb_cachep;
extern struct kmem_cache *qla_tgt_plogi_cachep;

extern int ql2xlogintimeout;
extern int qlport_down_retry;
extern int ql2xplogiabsentdevice;
extern int ql2xloginretrycount;
extern int ql2xfdmienable;
extern int ql2xrdpenable;
extern int ql2xsmartsan;
extern int ql2xallocfwdump;
extern int ql2xextended_error_logging;
extern int ql2xextended_error_logging_ktrace;
extern int ql2xmqsupport;
extern int ql2xfwloadbin;
extern int ql2xshiftctondsd;
extern int ql2xdbwr;
extern int ql2xasynctmfenable;
extern int ql2xgffidenable;
extern int ql2xenabledif;
extern int ql2xenablehba_err_chk;
extern int ql2xdontresethba;
extern uint64_t ql2xmaxlun;
extern int ql2xmdcapmask;
extern int ql2xmdenable;
extern int ql2xexlogins;
extern int ql2xexchoffld;
extern int ql2xiniexchg;
extern int ql2xfwholdabts;
extern int ql2xmvasynctoatio;
extern int ql2xuctrlirq;
extern int ql2xnvmeenable;
extern int ql2xautodetectsfp;
extern int ql2xenablemsix;
extern int qla2xuseresexchforels;
extern int ql2xdifbundlinginternalbuffers;
extern int ql2xfulldump_on_mpifail;
extern int ql2xsecenable;
extern int ql2xenforce_iocb_limit;
extern int ql2xabts_wait_nvme;
extern u32 ql2xnvme_queues;
extern int ql2xfc2target;

extern int qla2x00_loop_reset(scsi_qla_host_t *);
extern void qla2x00_abort_all_cmds(scsi_qla_host_t *, int);
extern int qla2x00_post_aen_work(struct scsi_qla_host *, enum
    fc_host_event_code, u32);
extern int qla2x00_post_idc_ack_work(struct scsi_qla_host *, uint16_t *);
extern int qla2x00_post_async_login_work(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_post_async_logout_work(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_post_async_adisc_work(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_set_exlogins_buffer(struct scsi_qla_host *);
extern void qla2x00_free_exlogin_buffer(struct qla_hw_data *);
extern int qla2x00_set_exchoffld_buffer(struct scsi_qla_host *);
extern void qla2x00_free_exchoffld_buffer(struct qla_hw_data *);

extern int qla81xx_restart_mpi_firmware(scsi_qla_host_t *);

extern struct scsi_qla_host *qla2x00_create_host(const struct scsi_host_template *,
	struct qla_hw_data *);
extern void qla2x00_relogin(struct scsi_qla_host *);
extern void qla2x00_do_work(struct scsi_qla_host *);
extern void qla2x00_free_fcports(struct scsi_qla_host *);
extern void qla2x00_free_fcport(fc_port_t *);

extern void qla83xx_schedule_work(scsi_qla_host_t *, int);
extern void qla83xx_service_idc_aen(struct work_struct *);
extern void qla83xx_nic_core_unrecoverable_work(struct work_struct *);
extern void qla83xx_idc_state_handler_work(struct work_struct *);
extern void qla83xx_nic_core_reset_work(struct work_struct *);

extern void qla83xx_idc_lock(scsi_qla_host_t *, uint16_t);
extern void qla83xx_idc_unlock(scsi_qla_host_t *, uint16_t);
extern int qla83xx_idc_state_handler(scsi_qla_host_t *);
extern int qla83xx_set_drv_presence(scsi_qla_host_t *vha);
extern int __qla83xx_set_drv_presence(scsi_qla_host_t *vha);
extern int qla83xx_clear_drv_presence(scsi_qla_host_t *vha);
extern int __qla83xx_clear_drv_presence(scsi_qla_host_t *vha);

extern int qla2x00_post_uevent_work(struct scsi_qla_host *, u32);
extern void qla2x00_disable_board_on_pci_error(struct work_struct *);
extern void qla2x00_sp_compl(srb_t *sp, int);
extern void qla2xxx_qpair_sp_free_dma(srb_t *sp);
extern void qla2xxx_qpair_sp_compl(srb_t *sp, int);
extern void qla24xx_sched_upd_fcport(fc_port_t *);
int qla24xx_post_gnl_work(struct scsi_qla_host *, fc_port_t *);
int qla24xx_post_relogin_work(struct scsi_qla_host *vha);
void qla2x00_wait_for_sess_deletion(scsi_qla_host_t *);
void qla24xx_process_purex_rdp(struct scsi_qla_host *vha,
			       struct purex_item *pkt);
void qla_pci_set_eeh_busy(struct scsi_qla_host *);
void qla_schedule_eeh_work(struct scsi_qla_host *);
struct edif_sa_ctl *qla_edif_find_sa_ctl_by_index(fc_port_t *fcport,
						  int index, int dir);

/*
 * Global Functions in qla_mid.c source file.
 */
extern void qla_update_vp_map(struct scsi_qla_host *, int);
extern struct scsi_host_template qla2xxx_driver_template;
extern struct scsi_transport_template *qla2xxx_transport_vport_template;
extern void qla2x00_timer(struct timer_list *);
extern void qla2x00_start_timer(scsi_qla_host_t *, unsigned long);
extern void qla24xx_deallocate_vp_id(scsi_qla_host_t *);
extern int qla24xx_disable_vp (scsi_qla_host_t *);
extern int qla24xx_enable_vp (scsi_qla_host_t *);
extern int qla24xx_control_vp(scsi_qla_host_t *, int );
extern int qla24xx_modify_vp_config(scsi_qla_host_t *);
extern int qla2x00_send_change_request(scsi_qla_host_t *, uint16_t, uint16_t);
extern void qla2x00_vp_stop_timer(scsi_qla_host_t *);
extern int qla24xx_configure_vhba (scsi_qla_host_t *);
extern void qla24xx_report_id_acquisition(scsi_qla_host_t *,
    struct vp_rpt_id_entry_24xx *);
extern void qla2x00_do_dpc_all_vps(scsi_qla_host_t *);
extern int qla24xx_vport_create_req_sanity_check(struct fc_vport *);
extern scsi_qla_host_t *qla24xx_create_vhost(struct fc_vport *);

extern void qla2x00_sp_free_dma(srb_t *sp);

extern void qla2x00_mark_device_lost(scsi_qla_host_t *, fc_port_t *, int);
extern void qla2x00_mark_all_devices_lost(scsi_qla_host_t *);
extern int qla24xx_async_abort_cmd(srb_t *, bool);

extern struct fw_blob *qla2x00_request_firmware(scsi_qla_host_t *);

extern int qla2x00_wait_for_hba_online(scsi_qla_host_t *);
extern int qla2x00_wait_for_chip_reset(scsi_qla_host_t *);
extern int qla2x00_wait_for_fcoe_ctx_reset(scsi_qla_host_t *);

extern void qla2xxx_wake_dpc(struct scsi_qla_host *);
extern void qla2x00_alert_all_vps(struct rsp_que *, uint16_t *);
extern void qla2x00_async_event(scsi_qla_host_t *, struct rsp_que *,
	uint16_t *);
extern int  qla2x00_vp_abort_isp(scsi_qla_host_t *);
void qla_adjust_buf(struct scsi_qla_host *);

/*
 * Global Function Prototypes in qla_iocb.c source file.
 */
void qla_els_pt_iocb(struct scsi_qla_host *vha,
	struct els_entry_24xx *pkt, struct qla_els_pt_arg *a);
cont_a64_entry_t *qla2x00_prep_cont_type1_iocb(scsi_qla_host_t *vha,
		struct req_que *que);
extern uint16_t qla2x00_calc_iocbs_32(uint16_t);
extern uint16_t qla2x00_calc_iocbs_64(uint16_t);
extern void qla2x00_build_scsi_iocbs_32(srb_t *, cmd_entry_t *, uint16_t);
extern void qla2x00_build_scsi_iocbs_64(srb_t *, cmd_entry_t *, uint16_t);
extern void qla24xx_build_scsi_iocbs(srb_t *, struct cmd_type_7 *,
	uint16_t, struct req_que *);
extern uint32_t qla2xxx_get_next_handle(struct req_que *req);
extern int qla2x00_start_scsi(srb_t *sp);
extern int qla24xx_start_scsi(srb_t *sp);
int qla2x00_marker(struct scsi_qla_host *, struct qla_qpair *,
    uint16_t, uint64_t, uint8_t);
extern int qla2x00_start_sp(srb_t *);
extern int qla24xx_dif_start_scsi(srb_t *);
extern int qla2x00_start_bidir(srb_t *, struct scsi_qla_host *, uint32_t);
extern int qla2xxx_dif_start_scsi_mq(srb_t *);
extern void qla2x00_init_async_sp(srb_t *sp, unsigned long tmo,
				  void (*done)(struct srb *, int));
extern unsigned long qla2x00_get_async_timeout(struct scsi_qla_host *);

extern void *qla2x00_alloc_iocbs(struct scsi_qla_host *, srb_t *);
extern void *__qla2x00_alloc_iocbs(struct qla_qpair *, srb_t *);
extern int qla2x00_issue_marker(scsi_qla_host_t *, int);
extern int qla24xx_walk_and_build_sglist_no_difb(struct qla_hw_data *, srb_t *,
	struct dsd64 *, uint16_t, struct qla_tc_param *);
extern int qla24xx_walk_and_build_sglist(struct qla_hw_data *, srb_t *,
	struct dsd64 *, uint16_t, struct qla_tc_param *);
extern int qla24xx_walk_and_build_prot_sglist(struct qla_hw_data *, srb_t *,
	struct dsd64 *, uint16_t, struct qla_tgt_cmd *);
extern int qla24xx_get_one_block_sg(uint32_t, struct qla2_sgx *, uint32_t *);
extern int qla24xx_configure_prot_mode(srb_t *, uint16_t *);
extern int qla24xx_issue_sa_replace_iocb(scsi_qla_host_t *vha,
	struct qla_work_evt *e);
void qla2x00_sp_release(struct kref *kref);
void qla2x00_els_dcmd2_iocb_timeout(void *data);

/*
 * Global Function Prototypes in qla_mbx.c source file.
 */
extern int
qla2x00_load_ram(scsi_qla_host_t *, dma_addr_t, uint32_t, uint32_t);

extern int
qla2x00_dump_ram(scsi_qla_host_t *, dma_addr_t, uint32_t, uint32_t);

extern int
qla2x00_execute_fw(scsi_qla_host_t *, uint32_t);

extern int
qla2x00_get_fw_version(scsi_qla_host_t *);

extern int
qla2x00_get_fw_options(scsi_qla_host_t *, uint16_t *);

extern int
qla2x00_set_fw_options(scsi_qla_host_t *, uint16_t *);

extern int
qla2x00_mbx_reg_test(scsi_qla_host_t *);

extern int
qla2x00_verify_checksum(scsi_qla_host_t *, uint32_t);

extern int
qla2x00_issue_iocb(scsi_qla_host_t *, void *, dma_addr_t, size_t);

extern int
qla2x00_abort_command(srb_t *);

extern int
qla2x00_abort_target(struct fc_port *, uint64_t, int);

extern int
qla2x00_lun_reset(struct fc_port *, uint64_t, int);

extern int
qla2x00_get_adapter_id(scsi_qla_host_t *, uint16_t *, uint8_t *, uint8_t *,
    uint8_t *, uint16_t *, uint16_t *);

extern int
qla2x00_get_retry_cnt(scsi_qla_host_t *, uint8_t *, uint8_t *, uint16_t *);

extern int
qla2x00_init_firmware(scsi_qla_host_t *, uint16_t);

extern int
qla2x00_get_port_database(scsi_qla_host_t *, fc_port_t *, uint8_t);

extern int
qla24xx_get_port_database(scsi_qla_host_t *, u16, struct port_database_24xx *);

extern int
qla2x00_get_firmware_state(scsi_qla_host_t *, uint16_t *);

extern int
qla2x00_get_port_name(scsi_qla_host_t *, uint16_t, uint8_t *, uint8_t);

extern int
qla24xx_link_initialize(scsi_qla_host_t *);

extern int
qla2x00_lip_reset(scsi_qla_host_t *);

extern int
qla2x00_send_sns(scsi_qla_host_t *, dma_addr_t, uint16_t, size_t);

extern int
qla2x00_login_fabric(scsi_qla_host_t *, uint16_t, uint8_t, uint8_t, uint8_t,
    uint16_t *, uint8_t);
extern int
qla24xx_login_fabric(scsi_qla_host_t *, uint16_t, uint8_t, uint8_t, uint8_t,
    uint16_t *, uint8_t);

extern int
qla2x00_login_local_device(scsi_qla_host_t *, fc_port_t *, uint16_t *,
    uint8_t);

extern int
qla2x00_fabric_logout(scsi_qla_host_t *, uint16_t, uint8_t, uint8_t, uint8_t);

extern int
qla24xx_fabric_logout(scsi_qla_host_t *, uint16_t, uint8_t, uint8_t, uint8_t);

extern int
qla2x00_full_login_lip(scsi_qla_host_t *ha);

extern int
qla2x00_get_id_list(scsi_qla_host_t *, void *, dma_addr_t, uint16_t *);

extern int
qla2x00_get_resource_cnts(scsi_qla_host_t *);

extern int
qla2x00_get_fcal_position_map(scsi_qla_host_t *ha, char *pos_map,
		u8 *num_entries);

extern int
qla2x00_get_link_status(scsi_qla_host_t *, uint16_t, struct link_statistics *,
    dma_addr_t);

extern int
qla24xx_get_isp_stats(scsi_qla_host_t *, struct link_statistics *,
    dma_addr_t, uint16_t);

extern int qla24xx_abort_command(srb_t *);
extern int qla24xx_async_abort_command(srb_t *);
extern int
qla24xx_abort_target(struct fc_port *, uint64_t, int);
extern int
qla24xx_lun_reset(struct fc_port *, uint64_t, int);
extern int
qla2x00_eh_wait_for_pending_commands(scsi_qla_host_t *, unsigned int,
	uint64_t, enum nexus_wait_type);
extern int
qla2x00_system_error(scsi_qla_host_t *);

extern int
qla2x00_write_serdes_word(scsi_qla_host_t *, uint16_t, uint16_t);
extern int
qla2x00_read_serdes_word(scsi_qla_host_t *, uint16_t, uint16_t *);

extern int
qla8044_write_serdes_word(scsi_qla_host_t *, uint32_t, uint32_t);
extern int
qla8044_read_serdes_word(scsi_qla_host_t *, uint32_t, uint32_t *);

extern int
qla2x00_set_serdes_params(scsi_qla_host_t *, uint16_t, uint16_t, uint16_t);

extern int
qla2x00_stop_firmware(scsi_qla_host_t *);

extern int
qla2x00_enable_eft_trace(scsi_qla_host_t *, dma_addr_t, uint16_t);
extern int
qla2x00_disable_eft_trace(scsi_qla_host_t *);

extern int
qla2x00_enable_fce_trace(scsi_qla_host_t *, dma_addr_t, uint16_t , uint16_t *,
    uint32_t *);

extern int
qla2x00_disable_fce_trace(scsi_qla_host_t *, uint64_t *, uint64_t *);

extern int
qla82xx_set_driver_version(scsi_qla_host_t *, char *);

extern int
qla25xx_set_driver_version(scsi_qla_host_t *, char *);

extern int
qla25xx_set_els_cmds_supported(scsi_qla_host_t *);

extern int
qla24xx_get_buffer_credits(scsi_qla_host_t *, struct buffer_credit_24xx *,
	dma_addr_t);

extern int
qla2x00_read_sfp(scsi_qla_host_t *, dma_addr_t, uint8_t *,
	uint16_t, uint16_t, uint16_t, uint16_t);

extern int
qla2x00_write_sfp(scsi_qla_host_t *, dma_addr_t, uint8_t *,
	uint16_t, uint16_t, uint16_t, uint16_t);

extern int
qla2x00_set_idma_speed(scsi_qla_host_t *, uint16_t, uint16_t, uint16_t *);

extern int qla84xx_verify_chip(struct scsi_qla_host *, uint16_t *);

extern int qla81xx_idc_ack(scsi_qla_host_t *, uint16_t *);

extern int
qla81xx_fac_get_sector_size(scsi_qla_host_t *, uint32_t *);

extern int
qla81xx_fac_do_write_enable(scsi_qla_host_t *, int);

extern int
qla81xx_fac_erase_sector(scsi_qla_host_t *, uint32_t, uint32_t);

extern int qla81xx_fac_semaphore_access(scsi_qla_host_t *, int);

extern int
qla2x00_get_xgmac_stats(scsi_qla_host_t *, dma_addr_t, uint16_t, uint16_t *);

extern int
qla2x00_get_dcbx_params(scsi_qla_host_t *, dma_addr_t, uint16_t);

extern int
qla2x00_read_ram_word(scsi_qla_host_t *, uint32_t, uint32_t *);

extern int
qla2x00_write_ram_word(scsi_qla_host_t *, uint32_t, uint32_t);

extern int
qla81xx_write_mpi_register(scsi_qla_host_t *, uint16_t *);
extern int qla2x00_get_data_rate(scsi_qla_host_t *);
extern int qla24xx_set_fcp_prio(scsi_qla_host_t *, uint16_t, uint16_t,
	uint16_t *);
extern int
qla81xx_get_port_config(scsi_qla_host_t *, uint16_t *);

extern int
qla81xx_set_port_config(scsi_qla_host_t *, uint16_t *);

extern int
qla2x00_port_logout(scsi_qla_host_t *, struct fc_port *);

extern int
qla2x00_dump_mctp_data(scsi_qla_host_t *, dma_addr_t, uint32_t, uint32_t);

extern int
qla26xx_dport_diagnostics(scsi_qla_host_t *, void *, uint, uint);

extern int
qla26xx_dport_diagnostics_v2(scsi_qla_host_t *,
			     struct qla_dport_diag_v2 *,  mbx_cmd_t *);

int qla24xx_send_mb_cmd(struct scsi_qla_host *, mbx_cmd_t *);
int qla24xx_gpdb_wait(struct scsi_qla_host *, fc_port_t *, u8);
int qla24xx_print_fc_port_id(struct scsi_qla_host *, struct seq_file *, u16);
int qla24xx_gidlist_wait(struct scsi_qla_host *, void *, dma_addr_t,
    uint16_t *);
int __qla24xx_parse_gpdb(struct scsi_qla_host *, fc_port_t *,
	struct port_database_24xx *);
int qla24xx_get_port_login_templ(scsi_qla_host_t *, dma_addr_t,
				 void *, uint16_t);

extern int qla27xx_get_zio_threshold(scsi_qla_host_t *, uint16_t *);
extern int qla27xx_set_zio_threshold(scsi_qla_host_t *, uint16_t);
int qla24xx_res_count_wait(struct scsi_qla_host *, uint16_t *, int);

extern int qla28xx_secure_flash_update(scsi_qla_host_t *, uint16_t, uint16_t,
    uint32_t, dma_addr_t, uint32_t);

extern int qla2xxx_read_remote_register(scsi_qla_host_t *, uint32_t,
    uint32_t *);
extern int qla2xxx_write_remote_register(scsi_qla_host_t *, uint32_t,
    uint32_t);
void qla_no_op_mb(struct scsi_qla_host *vha);

/*
 * Global Function Prototypes in qla_isr.c source file.
 */
extern irqreturn_t qla2100_intr_handler(int, void *);
extern irqreturn_t qla2300_intr_handler(int, void *);
extern irqreturn_t qla24xx_intr_handler(int, void *);
extern void qla2x00_process_response_queue(struct rsp_que *);
extern void
qla24xx_process_response_queue(struct scsi_qla_host *, struct rsp_que *);
extern int qla2x00_request_irqs(struct qla_hw_data *, struct rsp_que *);
extern void qla2x00_free_irqs(scsi_qla_host_t *);

extern int qla2x00_get_data_rate(scsi_qla_host_t *);
extern const char *qla2x00_get_link_speed_str(struct qla_hw_data *, uint16_t);
extern srb_t *
qla2x00_get_sp_from_handle(scsi_qla_host_t *, const char *, struct req_que *,
	void *);
extern void
qla2x00_process_completed_request(struct scsi_qla_host *, struct req_que *,
	uint32_t);
extern irqreturn_t
qla2xxx_msix_rsp_q(int irq, void *dev_id);
extern irqreturn_t
qla2xxx_msix_rsp_q_hs(int irq, void *dev_id);
fc_port_t *qla2x00_find_fcport_by_loopid(scsi_qla_host_t *, uint16_t);
fc_port_t *qla2x00_find_fcport_by_wwpn(scsi_qla_host_t *, u8 *, u8);
fc_port_t *qla2x00_find_fcport_by_nportid(scsi_qla_host_t *, port_id_t *, u8);
void qla24xx_queue_purex_item(scsi_qla_host_t *, struct purex_item *,
			      void (*process_item)(struct scsi_qla_host *,
			      struct purex_item *));
void __qla_consume_iocb(struct scsi_qla_host *, void **, struct rsp_que **);
void qla2xxx_process_purls_iocb(void **pkt, struct rsp_que **rsp);

/*
 * Global Function Prototypes in qla_sup.c source file.
 */
extern int qla24xx_read_flash_data(scsi_qla_host_t *, uint32_t *,
    uint32_t, uint32_t);
extern uint8_t *qla2x00_read_nvram_data(scsi_qla_host_t *, void *, uint32_t,
    uint32_t);
extern uint8_t *qla24xx_read_nvram_data(scsi_qla_host_t *, void *, uint32_t,
    uint32_t);
extern int qla2x00_write_nvram_data(scsi_qla_host_t *, void *, uint32_t,
    uint32_t);
extern int qla24xx_write_nvram_data(scsi_qla_host_t *, void *, uint32_t,
    uint32_t);
extern uint8_t *qla25xx_read_nvram_data(scsi_qla_host_t *, void *, uint32_t,
    uint32_t);
extern int qla25xx_write_nvram_data(scsi_qla_host_t *, void *, uint32_t,
    uint32_t);

extern int qla2x00_is_a_vp_did(scsi_qla_host_t *, uint32_t);
bool qla2x00_check_reg32_for_disconnect(scsi_qla_host_t *, uint32_t);
bool qla2x00_check_reg16_for_disconnect(scsi_qla_host_t *, uint16_t);

extern int qla2x00_beacon_on(struct scsi_qla_host *);
extern int qla2x00_beacon_off(struct scsi_qla_host *);
extern void qla2x00_beacon_blink(struct scsi_qla_host *);
extern int qla24xx_beacon_on(struct scsi_qla_host *);
extern int qla24xx_beacon_off(struct scsi_qla_host *);
extern void qla24xx_beacon_blink(struct scsi_qla_host *);
extern void qla83xx_beacon_blink(struct scsi_qla_host *);
extern int qla82xx_beacon_on(struct scsi_qla_host *);
extern int qla82xx_beacon_off(struct scsi_qla_host *);
extern int qla83xx_wr_reg(scsi_qla_host_t *, uint32_t, uint32_t);
extern int qla83xx_rd_reg(scsi_qla_host_t *, uint32_t, uint32_t *);
extern int qla83xx_restart_nic_firmware(scsi_qla_host_t *);
extern int qla83xx_access_control(scsi_qla_host_t *, uint16_t, uint32_t,
				  uint32_t, uint16_t *);

extern void *qla2x00_read_optrom_data(struct scsi_qla_host *, void *,
					 uint32_t, uint32_t);
extern int qla2x00_write_optrom_data(struct scsi_qla_host *, void *,
				     uint32_t, uint32_t);
extern void *qla24xx_read_optrom_data(struct scsi_qla_host *, void *,
					 uint32_t, uint32_t);
extern int qla24xx_write_optrom_data(struct scsi_qla_host *, void *,
				     uint32_t, uint32_t);
extern void *qla25xx_read_optrom_data(struct scsi_qla_host *, void *,
					 uint32_t, uint32_t);
extern void *qla8044_read_optrom_data(struct scsi_qla_host *,
					 void *, uint32_t, uint32_t);
extern void qla8044_watchdog(struct scsi_qla_host *vha);

extern int qla2x00_get_flash_version(scsi_qla_host_t *, void *);
extern int qla24xx_get_flash_version(scsi_qla_host_t *, void *);
extern int qla82xx_get_flash_version(scsi_qla_host_t *, void *);

extern int qla2xxx_get_flash_info(scsi_qla_host_t *);
extern int qla2xxx_get_vpd_field(scsi_qla_host_t *, char *, char *, size_t);

extern void qla2xxx_flash_npiv_conf(scsi_qla_host_t *);
extern int qla24xx_read_fcp_prio_cfg(scsi_qla_host_t *);
extern int qla2x00_mailbox_passthru(struct bsg_job *bsg_job);
int qla2x00_sys_ld_info(struct bsg_job *bsg_job);
int __qla_copy_purex_to_buffer(struct scsi_qla_host *, void **,
	struct rsp_que **, u8 *, u32);
struct purex_item *qla27xx_copy_multiple_pkt(struct scsi_qla_host *vha,
	void **pkt, struct rsp_que **rsp, bool is_purls, bool byte_order);
int qla_mailbox_passthru(scsi_qla_host_t *vha, uint16_t *mbx_in,
			 uint16_t *mbx_out);

/*
 * Global Function Prototypes in qla_dbg.c source file.
 */
void qla2xxx_dump_fw(scsi_qla_host_t *vha);
void qla2100_fw_dump(scsi_qla_host_t *vha);
void qla2300_fw_dump(scsi_qla_host_t *vha);
void qla24xx_fw_dump(scsi_qla_host_t *vha);
void qla25xx_fw_dump(scsi_qla_host_t *vha);
void qla81xx_fw_dump(scsi_qla_host_t *vha);
void qla82xx_fw_dump(scsi_qla_host_t *vha);
void qla8044_fw_dump(scsi_qla_host_t *vha);

void qla27xx_fwdump(scsi_qla_host_t *vha);
extern void qla27xx_mpi_fwdump(scsi_qla_host_t *, int);
extern ulong qla27xx_fwdt_calculate_dump_size(struct scsi_qla_host *, void *);
extern int qla27xx_fwdt_template_valid(void *);
extern ulong qla27xx_fwdt_template_size(void *);

extern void qla2xxx_dump_post_process(scsi_qla_host_t *, int);
extern void ql_dump_regs(uint, scsi_qla_host_t *, uint);
extern void ql_dump_buffer(uint, scsi_qla_host_t *, uint, const void *, uint);
/*
 * Global Function Prototypes in qla_gs.c source file.
 */
extern void *qla2x00_prep_ms_iocb(scsi_qla_host_t *, struct ct_arg *);
extern void *qla24xx_prep_ms_iocb(scsi_qla_host_t *, struct ct_arg *);
extern int qla2x00_ga_nxt(scsi_qla_host_t *, fc_port_t *);
extern int qla2x00_gid_pt(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_gpn_id(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_gnn_id(scsi_qla_host_t *, sw_info_t *);
extern void qla2x00_gff_id(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_rft_id(scsi_qla_host_t *);
extern int qla2x00_rff_id(scsi_qla_host_t *, u8);
extern int qla2x00_rnn_id(scsi_qla_host_t *);
extern int qla2x00_rsnn_nn(scsi_qla_host_t *);
extern void *qla2x00_prep_ms_fdmi_iocb(scsi_qla_host_t *, uint32_t, uint32_t);
extern void *qla24xx_prep_ms_fdmi_iocb(scsi_qla_host_t *, uint32_t, uint32_t);
extern int qla2x00_fdmi_register(scsi_qla_host_t *);
extern int qla2x00_gfpn_id(scsi_qla_host_t *, sw_info_t *);
extern size_t qla2x00_get_sym_node_name(scsi_qla_host_t *, uint8_t *, size_t);
extern int qla2x00_chk_ms_status(scsi_qla_host_t *, ms_iocb_entry_t *,
	struct ct_sns_rsp *, const char *);
extern void qla2x00_async_iocb_timeout(void *data);

int qla24xx_post_gpsc_work(struct scsi_qla_host *, fc_port_t *);
int qla24xx_async_gpsc(scsi_qla_host_t *, fc_port_t *);
void qla24xx_handle_gpsc_event(scsi_qla_host_t *, struct event_arg *);
int qla2x00_mgmt_svr_login(scsi_qla_host_t *);
int qla24xx_async_gffid(scsi_qla_host_t *vha, fc_port_t *fcport, bool);
int qla_fab_async_scan(scsi_qla_host_t *, srb_t *);
void qla_fab_scan_start(struct scsi_qla_host *);
void qla_fab_scan_finish(scsi_qla_host_t *, srb_t *);
int qla24xx_post_gfpnid_work(struct scsi_qla_host *, fc_port_t *);
int qla24xx_async_gfpnid(scsi_qla_host_t *, fc_port_t *);
void qla24xx_handle_gfpnid_event(scsi_qla_host_t *, struct event_arg *);
void qla24xx_sp_unmap(scsi_qla_host_t *, srb_t *);
void qla_scan_work_fn(struct work_struct *);
uint qla25xx_fdmi_port_speed_capability(struct qla_hw_data *);
uint qla25xx_fdmi_port_speed_currently(struct qla_hw_data *);

/*
 * Global Function Prototypes in qla_attr.c source file.
 */
struct device_attribute;
extern const struct attribute_group *qla2x00_host_groups[];
struct fc_function_template;
extern struct fc_function_template qla2xxx_transport_functions;
extern struct fc_function_template qla2xxx_transport_vport_functions;
extern void qla2x00_alloc_sysfs_attr(scsi_qla_host_t *);
extern void qla2x00_free_sysfs_attr(scsi_qla_host_t *, bool);
extern void qla2x00_init_host_attr(scsi_qla_host_t *);
extern void qla2x00_alloc_sysfs_attr(scsi_qla_host_t *);
extern int qla2x00_loopback_test(scsi_qla_host_t *, struct msg_echo_lb *, uint16_t *);
extern int qla2x00_echo_test(scsi_qla_host_t *,
	struct msg_echo_lb *, uint16_t *);
extern int qla24xx_update_all_fcp_prio(scsi_qla_host_t *);
extern int qla24xx_fcp_prio_cfg_valid(scsi_qla_host_t *,
	struct qla_fcp_prio_cfg *, uint8_t);
/*
 * Global Function Prototypes in qla_dfs.c source file.
 */
extern int qla2x00_dfs_setup(scsi_qla_host_t *);
extern int qla2x00_dfs_remove(scsi_qla_host_t *);

/* Globa function prototypes for multi-q */
extern int qla25xx_request_irq(struct qla_hw_data *, struct qla_qpair *,
	struct qla_msix_entry *, int);
extern int qla25xx_init_req_que(struct scsi_qla_host *, struct req_que *);
extern int qla25xx_init_rsp_que(struct scsi_qla_host *, struct rsp_que *);
extern int qla25xx_create_req_que(struct qla_hw_data *, uint16_t, uint8_t,
	uint16_t, int, uint8_t, bool);
extern int qla25xx_create_rsp_que(struct qla_hw_data *, uint16_t, uint8_t,
	uint16_t, struct qla_qpair *, bool);

extern void qla2x00_init_response_q_entries(struct rsp_que *);
extern int qla25xx_delete_req_que(struct scsi_qla_host *, struct req_que *);
extern int qla25xx_delete_rsp_que(struct scsi_qla_host *, struct rsp_que *);
extern int qla25xx_delete_queues(struct scsi_qla_host *);

/* qlafx00 related functions */
extern int qlafx00_pci_config(struct scsi_qla_host *);
extern int qlafx00_initialize_adapter(struct scsi_qla_host *);
extern int qlafx00_soft_reset(scsi_qla_host_t *);
extern int qlafx00_chip_diag(scsi_qla_host_t *);
extern void qlafx00_config_rings(struct scsi_qla_host *);
extern char *qlafx00_pci_info_str(struct scsi_qla_host *, char *, size_t);
extern char *qlafx00_fw_version_str(struct scsi_qla_host *, char *, size_t);
extern irqreturn_t qlafx00_intr_handler(int, void *);
extern void qlafx00_enable_intrs(struct qla_hw_data *);
extern void qlafx00_disable_intrs(struct qla_hw_data *);
extern int qlafx00_abort_target(fc_port_t *, uint64_t, int);
extern int qlafx00_lun_reset(fc_port_t *, uint64_t, int);
extern int qlafx00_start_scsi(srb_t *);
extern int qlafx00_abort_isp(scsi_qla_host_t *);
extern int qlafx00_iospace_config(struct qla_hw_data *);
extern int qlafx00_init_firmware(scsi_qla_host_t *, uint16_t);
extern int qlafx00_driver_shutdown(scsi_qla_host_t *, int);
extern int qlafx00_fw_ready(scsi_qla_host_t *);
extern int qlafx00_configure_devices(scsi_qla_host_t *);
extern int qlafx00_reset_initialize(scsi_qla_host_t *);
extern int qlafx00_fx_disc(scsi_qla_host_t *, fc_port_t *, uint16_t);
extern void qlafx00_process_aen(struct scsi_qla_host *, struct qla_work_evt *);
extern int qlafx00_post_aenfx_work(struct scsi_qla_host *,  uint32_t,
				   uint32_t *, int);
extern uint32_t qlafx00_fw_state_show(struct device *,
				      struct device_attribute *, char *);
extern void qlafx00_get_host_speed(struct Scsi_Host *);
extern void qlafx00_init_response_q_entries(struct rsp_que *);

extern void qlafx00_tm_iocb(srb_t *, struct tsk_mgmt_entry_fx00 *);
extern void qlafx00_abort_iocb(srb_t *, struct abort_iocb_entry_fx00 *);
extern void qlafx00_fxdisc_iocb(srb_t *, struct fxdisc_entry_fx00 *);
extern void qlafx00_timer_routine(scsi_qla_host_t *);
extern int qlafx00_rescan_isp(scsi_qla_host_t *);

/* qla82xx related functions */

/* PCI related functions */
extern int qla82xx_pci_config(struct scsi_qla_host *);
extern int qla82xx_pci_mem_read_2M(struct qla_hw_data *, u64, void *, int);
extern int qla82xx_iospace_config(struct qla_hw_data *);

/* Initialization related functions */
extern int qla82xx_reset_chip(struct scsi_qla_host *);
extern void qla82xx_config_rings(struct scsi_qla_host *);
extern void qla82xx_watchdog(scsi_qla_host_t *);
extern int qla82xx_start_firmware(scsi_qla_host_t *);

/* Firmware and flash related functions */
extern int qla82xx_load_risc(scsi_qla_host_t *, uint32_t *);
extern void *qla82xx_read_optrom_data(struct scsi_qla_host *, void *,
					 uint32_t, uint32_t);
extern int qla82xx_write_optrom_data(struct scsi_qla_host *, void *,
				     uint32_t, uint32_t);

/* Mailbox related functions */
extern int qla82xx_abort_isp(scsi_qla_host_t *);
extern int qla82xx_restart_isp(scsi_qla_host_t *);

/* IOCB related functions */
extern int qla82xx_start_scsi(srb_t *);
extern void qla2x00_sp_free(srb_t *sp);
extern void qla2x00_sp_timeout(struct timer_list *);
extern void qla2x00_bsg_job_done(srb_t *sp, int);
extern void qla2x00_bsg_sp_free(srb_t *sp);
extern void qla2x00_start_iocbs(struct scsi_qla_host *, struct req_que *);

/* Interrupt related */
extern irqreturn_t qla82xx_intr_handler(int, void *);
extern irqreturn_t qla82xx_msix_default(int, void *);
extern irqreturn_t qla82xx_msix_rsp_q(int, void *);
extern void qla82xx_enable_intrs(struct qla_hw_data *);
extern void qla82xx_disable_intrs(struct qla_hw_data *);
extern void qla82xx_poll(int, void *);
extern void qla82xx_init_flags(struct qla_hw_data *);

/* ISP 8021 hardware related */
extern void qla82xx_set_drv_active(scsi_qla_host_t *);
extern int qla82xx_wr_32(struct qla_hw_data *, ulong, u32);
extern int qla82xx_rd_32(struct qla_hw_data *, ulong);

/* ISP 8021 IDC */
extern void qla82xx_clear_drv_active(struct qla_hw_data *);
extern int qla82xx_idc_lock(struct qla_hw_data *);
extern void qla82xx_idc_unlock(struct qla_hw_data *);
extern int qla82xx_device_state_handler(scsi_qla_host_t *);
extern void qla8xxx_dev_failed_handler(scsi_qla_host_t *);
extern void qla82xx_clear_qsnt_ready(scsi_qla_host_t *);

extern void qla2x00_set_model_info(scsi_qla_host_t *, uint8_t *, size_t,
				   const char *);
extern int qla82xx_mbx_intr_enable(scsi_qla_host_t *);
extern int qla82xx_mbx_intr_disable(scsi_qla_host_t *);
extern void qla82xx_start_iocbs(scsi_qla_host_t *);
extern int qla82xx_fcoe_ctx_reset(scsi_qla_host_t *);
extern int qla82xx_check_md_needed(scsi_qla_host_t *);
extern void qla82xx_chip_reset_cleanup(scsi_qla_host_t *);
extern int qla81xx_set_led_config(scsi_qla_host_t *, uint16_t *);
extern int qla81xx_get_led_config(scsi_qla_host_t *, uint16_t *);
extern int qla82xx_mbx_beacon_ctl(scsi_qla_host_t *, int);
extern const char *qdev_state(uint32_t);
extern void qla82xx_clear_pending_mbx(scsi_qla_host_t *);
extern int qla82xx_read_temperature(scsi_qla_host_t *);
extern int qla8044_read_temperature(scsi_qla_host_t *);
extern int qla2x00_read_sfp_dev(struct scsi_qla_host *, char *, int);
extern int ql26xx_led_config(scsi_qla_host_t *, uint16_t, uint16_t *);

/* BSG related functions */
extern int qla24xx_bsg_request(struct bsg_job *);
extern int qla24xx_bsg_timeout(struct bsg_job *);
extern int qla84xx_reset_chip(scsi_qla_host_t *, uint16_t);
extern int qla2x00_issue_iocb_timeout(scsi_qla_host_t *, void *,
	dma_addr_t, size_t, uint32_t);
extern int qla2x00_get_idma_speed(scsi_qla_host_t *, uint16_t,
	uint16_t *, uint16_t *);
extern int qla24xx_sadb_update(struct bsg_job *bsg_job);
extern int qla_post_sa_replace_work(struct scsi_qla_host *vha,
	 fc_port_t *fcport, uint16_t nport_handle, struct edif_sa_ctl *sa_ctl);

/* 83xx related functions */
void qla83xx_fw_dump(scsi_qla_host_t *vha);

/* Minidump related functions */
extern int qla82xx_md_get_template_size(scsi_qla_host_t *);
extern int qla82xx_md_get_template(scsi_qla_host_t *);
extern int qla82xx_md_alloc(scsi_qla_host_t *);
extern void qla82xx_md_free(scsi_qla_host_t *);
extern int qla82xx_md_collect(scsi_qla_host_t *);
extern void qla82xx_md_prep(scsi_qla_host_t *);
extern void qla82xx_set_reset_owner(scsi_qla_host_t *);
extern int qla82xx_validate_template_chksum(scsi_qla_host_t *vha);

/* Function declarations for ISP8044 */
extern int qla8044_idc_lock(struct qla_hw_data *ha);
extern void qla8044_idc_unlock(struct qla_hw_data *ha);
extern uint32_t qla8044_rd_reg(struct qla_hw_data *ha, ulong addr);
extern void qla8044_wr_reg(struct qla_hw_data *ha, ulong addr, uint32_t val);
extern void qla8044_read_reset_template(struct scsi_qla_host *ha);
extern void qla8044_set_idc_dontreset(struct scsi_qla_host *ha);
extern int qla8044_rd_direct(struct scsi_qla_host *vha, const uint32_t crb_reg);
extern void qla8044_wr_direct(struct scsi_qla_host *vha,
			      const uint32_t crb_reg, const uint32_t value);
extern int qla8044_device_state_handler(struct scsi_qla_host *vha);
extern void qla8044_clear_qsnt_ready(struct scsi_qla_host *vha);
extern void qla8044_clear_drv_active(struct qla_hw_data *);
void qla8044_get_minidump(struct scsi_qla_host *vha);
int qla8044_collect_md_data(struct scsi_qla_host *vha);
extern int qla8044_md_get_template(scsi_qla_host_t *);
extern int qla8044_write_optrom_data(struct scsi_qla_host *, void *,
				     uint32_t, uint32_t);
extern irqreturn_t qla8044_intr_handler(int, void *);
extern void qla82xx_mbx_completion(scsi_qla_host_t *, uint16_t);
extern int qla8044_abort_isp(scsi_qla_host_t *);
extern int qla8044_check_fw_alive(struct scsi_qla_host *);
extern int qla_get_exlogin_status(scsi_qla_host_t *, uint16_t *,
	uint16_t *);
extern int qla_set_exlogin_mem_cfg(scsi_qla_host_t *vha, dma_addr_t phys_addr);
extern int qla_get_exchoffld_status(scsi_qla_host_t *, uint16_t *, uint16_t *);
extern int qla_set_exchoffld_mem_cfg(scsi_qla_host_t *);
extern void qlt_handle_abts_recv(struct scsi_qla_host *, struct rsp_que *,
	response_t *);

struct scsi_qla_host *qla_find_host_by_d_id(struct scsi_qla_host *vha, be_id_t d_id);
int qla24xx_async_notify_ack(scsi_qla_host_t *, fc_port_t *,
	struct imm_ntfy_from_isp *, int);
void qla24xx_do_nack_work(struct scsi_qla_host *, struct qla_work_evt *);
void qlt_plogi_ack_link(struct scsi_qla_host *, struct qlt_plogi_ack_t *,
	struct fc_port *, enum qlt_plogi_link_t);
void qlt_plogi_ack_unref(struct scsi_qla_host *, struct qlt_plogi_ack_t *);
extern void qlt_schedule_sess_for_deletion(struct fc_port *);
extern struct fc_port *qlt_find_sess_invalidate_other(scsi_qla_host_t *,
	uint64_t wwn, port_id_t port_id, uint16_t loop_id, struct fc_port **);
void qla24xx_delete_sess_fn(struct work_struct *);
void qlt_unknown_atio_work_fn(struct work_struct *);
void qla_update_host_map(struct scsi_qla_host *, port_id_t);
void qla_remove_hostmap(struct qla_hw_data *ha);
void qlt_clr_qp_table(struct scsi_qla_host *vha);
void qlt_set_mode(struct scsi_qla_host *);
int qla2x00_set_data_rate(scsi_qla_host_t *vha, uint16_t mode);
extern void qla24xx_process_purex_list(struct purex_list *);
extern void qla2x00_dfs_create_rport(scsi_qla_host_t *vha, struct fc_port *fp);
extern void qla2x00_dfs_remove_rport(scsi_qla_host_t *vha, struct fc_port *fp);
extern void qla_wait_nvme_release_cmd_kref(srb_t *sp);
extern void qla_nvme_abort_set_option
		(struct abort_entry_24xx *abt, srb_t *sp);
extern void qla_nvme_abort_process_comp_status
		(struct abort_entry_24xx *abt, srb_t *sp);
struct scsi_qla_host *qla_find_host_by_vp_idx(struct scsi_qla_host *vha,
	uint16_t vp_idx);

/* nvme.c */
void qla_nvme_unregister_remote_port(struct fc_port *fcport);

/* qla_edif.c */
fc_port_t *qla2x00_find_fcport_by_pid(scsi_qla_host_t *vha, port_id_t *id);
void qla_edb_eventcreate(scsi_qla_host_t *vha, uint32_t dbtype, uint32_t data, uint32_t data2,
		fc_port_t *fcport);
void qla_edb_stop(scsi_qla_host_t *vha);
int32_t qla_edif_app_mgmt(struct bsg_job *bsg_job);
void qla_enode_init(scsi_qla_host_t *vha);
void qla_enode_stop(scsi_qla_host_t *vha);
void qla_edif_flush_sa_ctl_lists(fc_port_t *fcport);
void qla_edb_init(scsi_qla_host_t *vha);
void qla_edif_timer(scsi_qla_host_t *vha);
int qla28xx_start_scsi_edif(srb_t *sp);
void qla24xx_sa_update_iocb(srb_t *sp, struct sa_update_28xx *sa_update_iocb);
void qla24xx_sa_replace_iocb(srb_t *sp, struct sa_update_28xx *sa_update_iocb);
void qla24xx_auth_els(scsi_qla_host_t *vha, void **pkt, struct rsp_que **rsp);
void qla28xx_sa_update_iocb_entry(scsi_qla_host_t *vha, struct req_que *req,
		struct sa_update_28xx *pkt);
void qla_handle_els_plogi_done(scsi_qla_host_t *vha, struct event_arg *ea);

#define QLA2XX_HW_ERROR			BIT_0
#define QLA2XX_SHT_LNK_DWN		BIT_1
#define QLA2XX_INT_ERR			BIT_2
#define QLA2XX_CMD_TIMEOUT		BIT_3
#define QLA2XX_RESET_CMD_ERR		BIT_4
#define QLA2XX_TGT_SHT_LNK_DOWN		BIT_17

#define QLA2XX_MAX_LINK_DOWN_TIME	100

int qla2xxx_start_stats(struct Scsi_Host *shost, u32 flags);
int qla2xxx_stop_stats(struct Scsi_Host *shost, u32 flags);
int qla2xxx_reset_stats(struct Scsi_Host *shost, u32 flags);

int qla2xxx_get_ini_stats(struct Scsi_Host *shost, u32 flags, void *data, u64 size);
int qla2xxx_get_tgt_stats(struct Scsi_Host *shost, u32 flags,
			  struct fc_rport *rport, void *data, u64 size);
int qla2xxx_disable_port(struct Scsi_Host *shost);
int qla2xxx_enable_port(struct Scsi_Host *shost);

uint64_t qla2x00_get_num_tgts(scsi_qla_host_t *vha);
uint64_t qla2x00_count_set_bits(u32 num);
int qla_create_buf_pool(struct scsi_qla_host *, struct qla_qpair *);
void qla_free_buf_pool(struct qla_qpair *);
int qla_get_buf(struct scsi_qla_host *, struct qla_qpair *, struct qla_buf_dsc *);
void qla_put_buf(struct qla_qpair *, struct qla_buf_dsc *);
#endif /* _QLA_GBL_H */
