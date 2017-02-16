/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#ifndef __QLA_GBL_H
#define	__QLA_GBL_H

#include <linux/interrupt.h>

/*
 * Global Function Prototypes in qla_init.c source file.
 */
extern int qla2x00_initialize_adapter(scsi_qla_host_t *);

extern int qla2100_pci_config(struct scsi_qla_host *);
extern int qla2300_pci_config(struct scsi_qla_host *);
extern int qla24xx_pci_config(scsi_qla_host_t *);
extern int qla25xx_pci_config(scsi_qla_host_t *);
extern void qla2x00_reset_chip(struct scsi_qla_host *);
extern void qla24xx_reset_chip(struct scsi_qla_host *);
extern int qla2x00_chip_diag(struct scsi_qla_host *);
extern int qla24xx_chip_diag(struct scsi_qla_host *);
extern void qla2x00_config_rings(struct scsi_qla_host *);
extern void qla24xx_config_rings(struct scsi_qla_host *);
extern void qla2x00_reset_adapter(struct scsi_qla_host *);
extern void qla24xx_reset_adapter(struct scsi_qla_host *);
extern int qla2x00_nvram_config(struct scsi_qla_host *);
extern int qla24xx_nvram_config(struct scsi_qla_host *);
extern int qla81xx_nvram_config(struct scsi_qla_host *);
extern void qla2x00_update_fw_options(struct scsi_qla_host *);
extern void qla24xx_update_fw_options(scsi_qla_host_t *);
extern void qla81xx_update_fw_options(scsi_qla_host_t *);
extern int qla2x00_load_risc(struct scsi_qla_host *, uint32_t *);
extern int qla24xx_load_risc(scsi_qla_host_t *, uint32_t *);
extern int qla81xx_load_risc(scsi_qla_host_t *, uint32_t *);

extern int qla2x00_perform_loop_resync(scsi_qla_host_t *);
extern int qla2x00_loop_resync(scsi_qla_host_t *);

extern int qla2x00_find_new_loop_id(scsi_qla_host_t *, fc_port_t *);

extern int qla2x00_fabric_login(scsi_qla_host_t *, fc_port_t *, uint16_t *);
extern int qla2x00_local_device_login(scsi_qla_host_t *, fc_port_t *);

extern int qla24xx_els_dcmd_iocb(scsi_qla_host_t *, int, port_id_t);

extern void qla2x00_update_fcports(scsi_qla_host_t *);

extern int qla2x00_abort_isp(scsi_qla_host_t *);
extern void qla2x00_abort_isp_cleanup(scsi_qla_host_t *);
extern void qla2x00_quiesce_io(scsi_qla_host_t *);

extern void qla2x00_update_fcport(scsi_qla_host_t *, fc_port_t *);

extern void qla2x00_alloc_fw_dump(scsi_qla_host_t *);
extern void qla2x00_try_to_stop_firmware(scsi_qla_host_t *);

extern int qla2x00_get_thermal_temp(scsi_qla_host_t *, uint16_t *);

extern void qla84xx_put_chip(struct scsi_qla_host *);

extern int qla2x00_async_login(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_async_logout(struct scsi_qla_host *, fc_port_t *);
extern int qla2x00_async_adisc(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_async_tm_cmd(fc_port_t *, uint32_t, uint32_t, uint32_t);
extern void qla2x00_async_login_done(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern void qla2x00_async_logout_done(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern void qla2x00_async_adisc_done(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
struct qla_work_evt *qla2x00_alloc_work(struct scsi_qla_host *,
    enum qla_work_type);
extern int qla24xx_async_gnl(struct scsi_qla_host *, fc_port_t *);
int qla2x00_post_work(struct scsi_qla_host *vha, struct qla_work_evt *e);
extern void *qla2x00_alloc_iocbs(struct scsi_qla_host *, srb_t *);
extern void *qla2x00_alloc_iocbs_ready(struct scsi_qla_host *, srb_t *);
extern int qla24xx_update_fcport_fcp_prio(scsi_qla_host_t *, fc_port_t *);

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
extern uint8_t qla27xx_find_valid_image(struct scsi_qla_host *);
extern struct qla_qpair *qla2xxx_create_qpair(struct scsi_qla_host *,
	int, int);
extern int qla2xxx_delete_qpair(struct scsi_qla_host *, struct qla_qpair *);
void qla2x00_fcport_event_handler(scsi_qla_host_t *, struct event_arg *);
int qla24xx_async_gpdb(struct scsi_qla_host *, fc_port_t *, u8);
int qla24xx_async_notify_ack(scsi_qla_host_t *, fc_port_t *,
	struct imm_ntfy_from_isp *, int);
int qla24xx_post_newsess_work(struct scsi_qla_host *, port_id_t *, u8 *,
    void *);
int qla24xx_fcport_handle_login(struct scsi_qla_host *, fc_port_t *);

/*
 * Global Data in qla_os.c source file.
 */
extern char qla2x00_version_str[];

extern struct kmem_cache *srb_cachep;

extern int ql2xlogintimeout;
extern int qlport_down_retry;
extern int ql2xplogiabsentdevice;
extern int ql2xloginretrycount;
extern int ql2xfdmienable;
extern int ql2xallocfwdump;
extern int ql2xextended_error_logging;
extern int ql2xiidmaenable;
extern int ql2xmqsupport;
extern int ql2xfwloadbin;
extern int ql2xetsenable;
extern int ql2xshiftctondsd;
extern int ql2xdbwr;
extern int ql2xasynctmfenable;
extern int ql2xgffidenable;
extern int ql2xenabledif;
extern int ql2xenablehba_err_chk;
extern int ql2xtargetreset;
extern int ql2xdontresethba;
extern uint64_t ql2xmaxlun;
extern int ql2xmdcapmask;
extern int ql2xmdenable;
extern int ql2xexlogins;
extern int ql2xexchoffld;
extern int ql2xfwholdabts;
extern int ql2xmvasynctoatio;

extern int qla2x00_loop_reset(scsi_qla_host_t *);
extern void qla2x00_abort_all_cmds(scsi_qla_host_t *, int);
extern int qla2x00_post_aen_work(struct scsi_qla_host *, enum
    fc_host_event_code, u32);
extern int qla2x00_post_idc_ack_work(struct scsi_qla_host *, uint16_t *);
extern int qla2x00_post_async_login_work(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_post_async_logout_work(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_post_async_logout_done_work(struct scsi_qla_host *,
    fc_port_t *, uint16_t *);
extern int qla2x00_post_async_adisc_work(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_post_async_adisc_done_work(struct scsi_qla_host *,
    fc_port_t *, uint16_t *);
extern int qla2x00_set_exlogins_buffer(struct scsi_qla_host *);
extern void qla2x00_free_exlogin_buffer(struct qla_hw_data *);
extern int qla2x00_set_exchoffld_buffer(struct scsi_qla_host *);
extern void qla2x00_free_exchoffld_buffer(struct qla_hw_data *);

extern int qla81xx_restart_mpi_firmware(scsi_qla_host_t *);

extern struct scsi_qla_host *qla2x00_create_host(struct scsi_host_template *,
	struct qla_hw_data *);
extern void qla2x00_free_host(struct scsi_qla_host *);
extern void qla2x00_relogin(struct scsi_qla_host *);
extern void qla2x00_do_work(struct scsi_qla_host *);
extern void qla2x00_free_fcports(struct scsi_qla_host *);

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

extern int qla2x00_post_uevent_work(struct scsi_qla_host *, u32);
extern void qla2x00_disable_board_on_pci_error(struct work_struct *);
extern void qla2x00_sp_compl(void *, int);
extern void qla2xxx_qpair_sp_free_dma(void *);
extern void qla2xxx_qpair_sp_compl(void *, int);
extern int qla24xx_post_upd_fcport_work(struct scsi_qla_host *, fc_port_t *);
void qla2x00_handle_login_done_event(struct scsi_qla_host *, fc_port_t *,
	uint16_t *);
int qla24xx_post_gnl_work(struct scsi_qla_host *, fc_port_t *);

/*
 * Global Functions in qla_mid.c source file.
 */
extern struct scsi_host_template qla2xxx_driver_template;
extern struct scsi_transport_template *qla2xxx_transport_vport_template;
extern void qla2x00_timer(scsi_qla_host_t *);
extern void qla2x00_start_timer(scsi_qla_host_t *, void *, unsigned long);
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
extern scsi_qla_host_t * qla24xx_create_vhost(struct fc_vport *);

extern void qla2x00_sp_free_dma(void *);
extern char *qla2x00_get_fw_version_str(struct scsi_qla_host *, char *);

extern void qla2x00_mark_device_lost(scsi_qla_host_t *, fc_port_t *, int, int);
extern void qla2x00_mark_all_devices_lost(scsi_qla_host_t *, int);

extern struct fw_blob *qla2x00_request_firmware(scsi_qla_host_t *);

extern int qla2x00_wait_for_hba_online(scsi_qla_host_t *);
extern int qla2x00_wait_for_chip_reset(scsi_qla_host_t *);
extern int qla2x00_wait_for_fcoe_ctx_reset(scsi_qla_host_t *);

extern void qla2xxx_wake_dpc(struct scsi_qla_host *);
extern void qla2x00_alert_all_vps(struct rsp_que *, uint16_t *);
extern void qla2x00_async_event(scsi_qla_host_t *, struct rsp_que *,
	uint16_t *);
extern int  qla2x00_vp_abort_isp(scsi_qla_host_t *);

/*
 * Global Function Prototypes in qla_iocb.c source file.
 */

extern uint16_t qla2x00_calc_iocbs_32(uint16_t);
extern uint16_t qla2x00_calc_iocbs_64(uint16_t);
extern void qla2x00_build_scsi_iocbs_32(srb_t *, cmd_entry_t *, uint16_t);
extern void qla2x00_build_scsi_iocbs_64(srb_t *, cmd_entry_t *, uint16_t);
extern void qla24xx_build_scsi_iocbs(srb_t *, struct cmd_type_7 *,
	uint16_t, struct req_que *);
extern int qla2x00_start_scsi(srb_t *sp);
extern int qla24xx_start_scsi(srb_t *sp);
int qla2x00_marker(struct scsi_qla_host *, struct req_que *, struct rsp_que *,
						uint16_t, uint64_t, uint8_t);
extern int qla2x00_start_sp(srb_t *);
extern int qla24xx_dif_start_scsi(srb_t *);
extern int qla2x00_start_bidir(srb_t *, struct scsi_qla_host *, uint32_t);
extern int qla2xxx_dif_start_scsi_mq(srb_t *);
extern unsigned long qla2x00_get_async_timeout(struct scsi_qla_host *);

extern void *qla2x00_alloc_iocbs(scsi_qla_host_t *, srb_t *);
extern int qla2x00_issue_marker(scsi_qla_host_t *, int);
extern int qla24xx_walk_and_build_sglist_no_difb(struct qla_hw_data *, srb_t *,
	uint32_t *, uint16_t, struct qla_tgt_cmd *);
extern int qla24xx_walk_and_build_sglist(struct qla_hw_data *, srb_t *,
	uint32_t *, uint16_t, struct qla_tgt_cmd *);
extern int qla24xx_walk_and_build_prot_sglist(struct qla_hw_data *, srb_t *,
	uint32_t *, uint16_t, struct qla_tgt_cmd *);
extern int qla24xx_get_one_block_sg(uint32_t, struct qla2_sgx *, uint32_t *);
extern int qla24xx_configure_prot_mode(srb_t *, uint16_t *);
extern int qla24xx_build_scsi_crc_2_iocbs(srb_t *,
	struct cmd_type_crc_2 *, uint16_t, uint16_t, uint16_t);

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
qla2x00_get_fcal_position_map(scsi_qla_host_t *ha, char *pos_map);

extern int
qla2x00_get_link_status(scsi_qla_host_t *, uint16_t, struct link_statistics *,
    dma_addr_t);

extern int
qla24xx_get_isp_stats(scsi_qla_host_t *, struct link_statistics *,
    dma_addr_t, uint);

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
fc_port_t *qla2x00_find_fcport_by_loopid(scsi_qla_host_t *, uint16_t);
fc_port_t *qla2x00_find_fcport_by_wwpn(scsi_qla_host_t *, u8 *, u8);
fc_port_t *qla2x00_find_fcport_by_nportid(scsi_qla_host_t *, port_id_t *, u8);

/*
 * Global Function Prototypes in qla_sup.c source file.
 */
extern void qla2x00_release_nvram_protection(scsi_qla_host_t *);
extern uint32_t *qla24xx_read_flash_data(scsi_qla_host_t *, uint32_t *,
					 uint32_t, uint32_t);
extern uint8_t *qla2x00_read_nvram_data(scsi_qla_host_t *, uint8_t *, uint32_t,
					uint32_t);
extern uint8_t *qla24xx_read_nvram_data(scsi_qla_host_t *, uint8_t *, uint32_t,
					uint32_t);
extern int qla2x00_write_nvram_data(scsi_qla_host_t *, uint8_t *, uint32_t,
				    uint32_t);
extern int qla24xx_write_nvram_data(scsi_qla_host_t *, uint8_t *, uint32_t,
				    uint32_t);
extern uint8_t *qla25xx_read_nvram_data(scsi_qla_host_t *, uint8_t *, uint32_t,
					uint32_t);
extern int qla25xx_write_nvram_data(scsi_qla_host_t *, uint8_t *, uint32_t,
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

extern uint8_t *qla2x00_read_optrom_data(struct scsi_qla_host *, uint8_t *,
					 uint32_t, uint32_t);
extern int qla2x00_write_optrom_data(struct scsi_qla_host *, uint8_t *,
				     uint32_t, uint32_t);
extern uint8_t *qla24xx_read_optrom_data(struct scsi_qla_host *, uint8_t *,
					 uint32_t, uint32_t);
extern int qla24xx_write_optrom_data(struct scsi_qla_host *, uint8_t *,
				     uint32_t, uint32_t);
extern uint8_t *qla25xx_read_optrom_data(struct scsi_qla_host *, uint8_t *,
					 uint32_t, uint32_t);
extern uint8_t *qla8044_read_optrom_data(struct scsi_qla_host *,
					 uint8_t *, uint32_t, uint32_t);
extern void qla8044_watchdog(struct scsi_qla_host *vha);

extern int qla2x00_get_flash_version(scsi_qla_host_t *, void *);
extern int qla24xx_get_flash_version(scsi_qla_host_t *, void *);
extern int qla82xx_get_flash_version(scsi_qla_host_t *, void *);

extern int qla2xxx_get_flash_info(scsi_qla_host_t *);
extern int qla2xxx_get_vpd_field(scsi_qla_host_t *, char *, char *, size_t);

extern void qla2xxx_flash_npiv_conf(scsi_qla_host_t *);
extern int qla24xx_read_fcp_prio_cfg(scsi_qla_host_t *);

/*
 * Global Function Prototypes in qla_dbg.c source file.
 */
extern void qla2100_fw_dump(scsi_qla_host_t *, int);
extern void qla2300_fw_dump(scsi_qla_host_t *, int);
extern void qla24xx_fw_dump(scsi_qla_host_t *, int);
extern void qla25xx_fw_dump(scsi_qla_host_t *, int);
extern void qla81xx_fw_dump(scsi_qla_host_t *, int);
extern void qla82xx_fw_dump(scsi_qla_host_t *, int);
extern void qla8044_fw_dump(scsi_qla_host_t *, int);

extern void qla27xx_fwdump(scsi_qla_host_t *, int);
extern ulong qla27xx_fwdt_calculate_dump_size(struct scsi_qla_host *);
extern int qla27xx_fwdt_template_valid(void *);
extern ulong qla27xx_fwdt_template_size(void *);
extern const void *qla27xx_fwdt_template_default(void);
extern ulong qla27xx_fwdt_template_default_size(void);

extern void qla2x00_dump_regs(scsi_qla_host_t *);
extern void qla2x00_dump_buffer(uint8_t *, uint32_t);
extern void qla2x00_dump_buffer_zipped(uint8_t *, uint32_t);
extern void ql_dump_regs(uint32_t, scsi_qla_host_t *, int32_t);
extern void ql_dump_buffer(uint32_t, scsi_qla_host_t *, int32_t,
			   uint8_t *, uint32_t);
extern void qla2xxx_dump_post_process(scsi_qla_host_t *, int);

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
extern int qla2x00_rff_id(scsi_qla_host_t *);
extern int qla2x00_rnn_id(scsi_qla_host_t *);
extern int qla2x00_rsnn_nn(scsi_qla_host_t *);
extern void *qla2x00_prep_ms_fdmi_iocb(scsi_qla_host_t *, uint32_t, uint32_t);
extern void *qla24xx_prep_ms_fdmi_iocb(scsi_qla_host_t *, uint32_t, uint32_t);
extern int qla2x00_fdmi_register(scsi_qla_host_t *);
extern int qla2x00_gfpn_id(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_gpsc(scsi_qla_host_t *, sw_info_t *);
extern void qla2x00_get_sym_node_name(scsi_qla_host_t *, uint8_t *, size_t);
extern int qla2x00_chk_ms_status(scsi_qla_host_t *, ms_iocb_entry_t *,
	struct ct_sns_rsp *, const char *);
extern void qla2x00_async_iocb_timeout(void *data);
extern int qla24xx_async_gidpn(scsi_qla_host_t *, fc_port_t *);
int qla24xx_post_gidpn_work(struct scsi_qla_host *, fc_port_t *);
void qla24xx_handle_gidpn_event(scsi_qla_host_t *, struct event_arg *);

extern void qla2x00_free_fcport(fc_port_t *);

extern int qla24xx_post_gpnid_work(struct scsi_qla_host *, port_id_t *);
extern int qla24xx_async_gpnid(scsi_qla_host_t *, port_id_t *);
void qla24xx_async_gpnid_done(scsi_qla_host_t *, srb_t*);
void qla24xx_handle_gpnid_event(scsi_qla_host_t *, struct event_arg *);

int qla24xx_post_gpsc_work(struct scsi_qla_host *, fc_port_t *);
int qla24xx_async_gpsc(scsi_qla_host_t *, fc_port_t *);
int qla2x00_mgmt_svr_login(scsi_qla_host_t *);

/*
 * Global Function Prototypes in qla_attr.c source file.
 */
struct device_attribute;
extern struct device_attribute *qla2x00_host_attrs[];
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
	uint16_t, int, uint8_t);
extern int qla25xx_create_rsp_que(struct qla_hw_data *, uint16_t, uint8_t,
	uint16_t, struct qla_qpair *);

extern void qla2x00_init_response_q_entries(struct rsp_que *);
extern int qla25xx_delete_req_que(struct scsi_qla_host *, struct req_que *);
extern int qla25xx_delete_rsp_que(struct scsi_qla_host *, struct rsp_que *);
extern int qla25xx_delete_queues(struct scsi_qla_host *);
extern uint16_t qla24xx_rd_req_reg(struct qla_hw_data *, uint16_t);
extern uint16_t qla25xx_rd_req_reg(struct qla_hw_data *, uint16_t);
extern void qla24xx_wrt_req_reg(struct qla_hw_data *, uint16_t, uint16_t);
extern void qla25xx_wrt_req_reg(struct qla_hw_data *, uint16_t, uint16_t);
extern void qla25xx_wrt_rsp_reg(struct qla_hw_data *, uint16_t, uint16_t);
extern void qla24xx_wrt_rsp_reg(struct qla_hw_data *, uint16_t, uint16_t);

/* qlafx00 related functions */
extern int qlafx00_pci_config(struct scsi_qla_host *);
extern int qlafx00_initialize_adapter(struct scsi_qla_host *);
extern void qlafx00_soft_reset(scsi_qla_host_t *);
extern int qlafx00_chip_diag(scsi_qla_host_t *);
extern void qlafx00_config_rings(struct scsi_qla_host *);
extern char *qlafx00_pci_info_str(struct scsi_qla_host *, char *);
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
extern int qlafx00_process_aen(struct scsi_qla_host *, struct qla_work_evt *);
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
extern int qlafx00_loop_reset(scsi_qla_host_t *vha);

/* qla82xx related functions */

/* PCI related functions */
extern int qla82xx_pci_config(struct scsi_qla_host *);
extern int qla82xx_pci_mem_read_2M(struct qla_hw_data *, u64, void *, int);
extern int qla82xx_pci_region_offset(struct pci_dev *, int);
extern int qla82xx_iospace_config(struct qla_hw_data *);

/* Initialization related functions */
extern void qla82xx_reset_chip(struct scsi_qla_host *);
extern void qla82xx_config_rings(struct scsi_qla_host *);
extern void qla82xx_watchdog(scsi_qla_host_t *);
extern int qla82xx_start_firmware(scsi_qla_host_t *);

/* Firmware and flash related functions */
extern int qla82xx_load_risc(scsi_qla_host_t *, uint32_t *);
extern uint8_t *qla82xx_read_optrom_data(struct scsi_qla_host *, uint8_t *,
					 uint32_t, uint32_t);
extern int qla82xx_write_optrom_data(struct scsi_qla_host *, uint8_t *,
				     uint32_t, uint32_t);

/* Mailbox related functions */
extern int qla82xx_abort_isp(scsi_qla_host_t *);
extern int qla82xx_restart_isp(scsi_qla_host_t *);

/* IOCB related functions */
extern int qla82xx_start_scsi(srb_t *);
extern void qla2x00_sp_free(void *);
extern void qla2x00_sp_timeout(unsigned long);
extern void qla2x00_bsg_job_done(void *, int);
extern void qla2x00_bsg_sp_free(void *);
extern void qla2x00_start_iocbs(struct scsi_qla_host *, struct req_que *);

/* Interrupt related */
extern irqreturn_t qla82xx_intr_handler(int, void *);
extern irqreturn_t qla82xx_msi_handler(int, void *);
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
extern int qla82xx_rdmem(struct qla_hw_data *, u64, void *, int);
extern int qla82xx_wrmem(struct qla_hw_data *, u64, void *, int);

/* ISP 8021 IDC */
extern void qla82xx_clear_drv_active(struct qla_hw_data *);
extern uint32_t  qla82xx_wait_for_state_change(scsi_qla_host_t *, uint32_t);
extern int qla82xx_idc_lock(struct qla_hw_data *);
extern void qla82xx_idc_unlock(struct qla_hw_data *);
extern int qla82xx_device_state_handler(scsi_qla_host_t *);
extern void qla8xxx_dev_failed_handler(scsi_qla_host_t *);
extern void qla82xx_clear_qsnt_ready(scsi_qla_host_t *);

extern void qla2x00_set_model_info(scsi_qla_host_t *, uint8_t *,
				   size_t, char *);
extern int qla82xx_mbx_intr_enable(scsi_qla_host_t *);
extern int qla82xx_mbx_intr_disable(scsi_qla_host_t *);
extern void qla82xx_start_iocbs(scsi_qla_host_t *);
extern int qla82xx_fcoe_ctx_reset(scsi_qla_host_t *);
extern int qla82xx_check_md_needed(scsi_qla_host_t *);
extern void qla82xx_chip_reset_cleanup(scsi_qla_host_t *);
extern int qla81xx_set_led_config(scsi_qla_host_t *, uint16_t *);
extern int qla81xx_get_led_config(scsi_qla_host_t *, uint16_t *);
extern int qla82xx_mbx_beacon_ctl(scsi_qla_host_t *, int);
extern char *qdev_state(uint32_t);
extern void qla82xx_clear_pending_mbx(scsi_qla_host_t *);
extern int qla82xx_read_temperature(scsi_qla_host_t *);
extern int qla8044_read_temperature(scsi_qla_host_t *);

/* BSG related functions */
extern int qla24xx_bsg_request(struct bsg_job *);
extern int qla24xx_bsg_timeout(struct bsg_job *);
extern int qla84xx_reset_chip(scsi_qla_host_t *, uint16_t);
extern int qla2x00_issue_iocb_timeout(scsi_qla_host_t *, void *,
	dma_addr_t, size_t, uint32_t);
extern int qla2x00_get_idma_speed(scsi_qla_host_t *, uint16_t,
	uint16_t *, uint16_t *);

/* 83xx related functions */
extern void qla83xx_fw_dump(scsi_qla_host_t *, int);

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
extern int qla8044_write_optrom_data(struct scsi_qla_host *, uint8_t *,
				     uint32_t, uint32_t);
extern irqreturn_t qla8044_intr_handler(int, void *);
extern void qla82xx_mbx_completion(scsi_qla_host_t *, uint16_t);
extern int qla8044_abort_isp(scsi_qla_host_t *);
extern int qla8044_check_fw_alive(struct scsi_qla_host *);

extern void qlt_host_reset_handler(struct qla_hw_data *ha);
extern int qla_get_exlogin_status(scsi_qla_host_t *, uint16_t *,
	uint16_t *);
extern int qla_set_exlogin_mem_cfg(scsi_qla_host_t *vha, dma_addr_t phys_addr);
extern int qla_get_exchoffld_status(scsi_qla_host_t *, uint16_t *, uint16_t *);
extern int qla_set_exchoffld_mem_cfg(scsi_qla_host_t *, dma_addr_t);
extern void qlt_handle_abts_recv(struct scsi_qla_host *, response_t *);

int qla24xx_async_notify_ack(scsi_qla_host_t *, fc_port_t *,
	struct imm_ntfy_from_isp *, int);
void qla24xx_do_nack_work(struct scsi_qla_host *, struct qla_work_evt *);
void qlt_plogi_ack_link(struct scsi_qla_host *, struct qlt_plogi_ack_t *,
	struct fc_port *, enum qlt_plogi_link_t);
void qlt_plogi_ack_unref(struct scsi_qla_host *, struct qlt_plogi_ack_t *);
extern void qlt_schedule_sess_for_deletion(struct fc_port *, bool);
extern void qlt_schedule_sess_for_deletion_lock(struct fc_port *);
extern struct fc_port *qlt_find_sess_invalidate_other(scsi_qla_host_t *,
	uint64_t wwn, port_id_t port_id, uint16_t loop_id, struct fc_port **);
void qla24xx_delete_sess_fn(struct work_struct *);
void qlt_unknown_atio_work_fn(struct work_struct *);

#endif /* _QLA_GBL_H */
