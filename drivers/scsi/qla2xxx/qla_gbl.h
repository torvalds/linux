/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2008 QLogic Corporation
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

extern int qla2x00_loop_resync(scsi_qla_host_t *);

extern int qla2x00_fabric_login(scsi_qla_host_t *, fc_port_t *, uint16_t *);
extern int qla2x00_local_device_login(scsi_qla_host_t *, fc_port_t *);

extern void qla2x00_update_fcports(scsi_qla_host_t *);

extern int qla2x00_abort_isp(scsi_qla_host_t *);
extern void qla2x00_abort_isp_cleanup(scsi_qla_host_t *);

extern void qla2x00_update_fcport(scsi_qla_host_t *, fc_port_t *);

extern void qla2x00_alloc_fw_dump(scsi_qla_host_t *);
extern void qla2x00_try_to_stop_firmware(scsi_qla_host_t *);

extern void qla84xx_put_chip(struct scsi_qla_host *);

extern int qla2x00_async_login(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_async_logout(struct scsi_qla_host *, fc_port_t *);
extern int qla2x00_async_adisc(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_async_tm_cmd(fc_port_t *, uint32_t, uint32_t, uint32_t);
extern int qla2x00_async_marker(fc_port_t *, uint16_t, uint8_t);
extern void qla2x00_async_login_done(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern void qla2x00_async_logout_done(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern void qla2x00_async_adisc_done(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern void qla2x00_async_tm_cmd_done(struct scsi_qla_host *, fc_port_t *,
	struct srb_iocb *);
extern void qla2x00_async_marker_done(struct scsi_qla_host *, fc_port_t *,
	struct srb_iocb *);

extern fc_port_t *
qla2x00_alloc_fcport(scsi_qla_host_t *, gfp_t );
/*
 * Global Data in qla_os.c source file.
 */
extern char qla2x00_version_str[];

extern int ql2xlogintimeout;
extern int qlport_down_retry;
extern int ql2xplogiabsentdevice;
extern int ql2xloginretrycount;
extern int ql2xfdmienable;
extern int ql2xallocfwdump;
extern int ql2xextended_error_logging;
extern int ql2xiidmaenable;
extern int ql2xmaxqueues;
extern int ql2xmultique_tag;
extern int ql2xfwloadbin;
extern int ql2xetsenable;
extern int ql2xshiftctondsd;
extern int ql2xdbwr;
extern int ql2xdontresethba;
extern int ql2xasynctmfenable;
extern int ql2xenabledif;
extern int ql2xenablehba_err_chk;
extern int ql2xtargetreset;

extern int qla2x00_loop_reset(scsi_qla_host_t *);
extern void qla2x00_abort_all_cmds(scsi_qla_host_t *, int);
extern int qla2x00_post_aen_work(struct scsi_qla_host *, enum
    fc_host_event_code, u32);
extern int qla2x00_post_idc_ack_work(struct scsi_qla_host *, uint16_t *);
extern int qla2x00_post_async_login_work(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_post_async_login_done_work(struct scsi_qla_host *,
    fc_port_t *, uint16_t *);
extern int qla2x00_post_async_logout_work(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_post_async_logout_done_work(struct scsi_qla_host *,
    fc_port_t *, uint16_t *);
extern int qla2x00_post_async_adisc_work(struct scsi_qla_host *, fc_port_t *,
    uint16_t *);
extern int qla2x00_post_async_adisc_done_work(struct scsi_qla_host *,
    fc_port_t *, uint16_t *);
extern int qla2x00_post_uevent_work(struct scsi_qla_host *, u32);

extern int qla81xx_restart_mpi_firmware(scsi_qla_host_t *);

extern struct scsi_qla_host *qla2x00_create_host(struct scsi_host_template *,
	struct qla_hw_data *);
extern void qla2x00_free_host(struct scsi_qla_host *);
extern void qla2x00_relogin(struct scsi_qla_host *);
extern void qla2x00_do_work(struct scsi_qla_host *);

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

extern void qla2x00_sp_compl(struct qla_hw_data *, srb_t *);

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
extern int qla2x00_start_scsi(srb_t *sp);
extern int qla24xx_start_scsi(srb_t *sp);
int qla2x00_marker(struct scsi_qla_host *, struct req_que *, struct rsp_que *,
						uint16_t, uint16_t, uint8_t);
int __qla2x00_marker(struct scsi_qla_host *, struct req_que *, struct rsp_que *,
						uint16_t, uint16_t, uint8_t);
extern int qla2x00_start_sp(srb_t *);
extern void qla2x00_ctx_sp_free(srb_t *);
extern uint16_t qla24xx_calc_iocbs(uint16_t);
extern void qla24xx_build_scsi_iocbs(srb_t *, struct cmd_type_7 *, uint16_t);
extern int qla24xx_dif_start_scsi(srb_t *);


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
qla2x00_get_fw_version(scsi_qla_host_t *, uint16_t *, uint16_t *, uint16_t *,
    uint16_t *, uint32_t *, uint8_t *, uint32_t *, uint8_t *);

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
qla2x00_abort_target(struct fc_port *, unsigned int, int);

extern int
qla2x00_lun_reset(struct fc_port *, unsigned int, int);

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
qla2x00_get_resource_cnts(scsi_qla_host_t *, uint16_t *, uint16_t *,
    uint16_t *, uint16_t *, uint16_t *, uint16_t *);

extern int
qla2x00_get_fcal_position_map(scsi_qla_host_t *ha, char *pos_map);

extern int
qla2x00_get_link_status(scsi_qla_host_t *, uint16_t, struct link_statistics *,
    dma_addr_t);

extern int
qla24xx_get_isp_stats(scsi_qla_host_t *, struct link_statistics *,
    dma_addr_t);

extern int qla24xx_abort_command(srb_t *);
extern int
qla24xx_abort_target(struct fc_port *, unsigned int, int);
extern int
qla24xx_lun_reset(struct fc_port *, unsigned int, int);

extern int
qla2x00_system_error(scsi_qla_host_t *);

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
qla2x00_read_sfp(scsi_qla_host_t *, dma_addr_t, uint16_t, uint16_t, uint16_t);

extern int
qla2x00_read_edc(scsi_qla_host_t *, uint16_t, uint16_t, dma_addr_t,
    uint8_t *, uint16_t, uint16_t);

extern int
qla2x00_write_edc(scsi_qla_host_t *, uint16_t, uint16_t, dma_addr_t,
    uint8_t *, uint16_t, uint16_t);

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

extern int qla2x00_get_data_rate(scsi_qla_host_t *);
extern int qla24xx_set_fcp_prio(scsi_qla_host_t *, uint16_t, uint16_t,
	uint16_t *);

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

extern int qla2x00_beacon_on(struct scsi_qla_host *);
extern int qla2x00_beacon_off(struct scsi_qla_host *);
extern void qla2x00_beacon_blink(struct scsi_qla_host *);
extern int qla24xx_beacon_on(struct scsi_qla_host *);
extern int qla24xx_beacon_off(struct scsi_qla_host *);
extern void qla24xx_beacon_blink(struct scsi_qla_host *);

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

extern int qla2x00_get_flash_version(scsi_qla_host_t *, void *);
extern int qla24xx_get_flash_version(scsi_qla_host_t *, void *);

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
extern void qla2x00_dump_regs(scsi_qla_host_t *);
extern void qla2x00_dump_buffer(uint8_t *, uint32_t);
extern void qla2x00_dump_buffer_zipped(uint8_t *, uint32_t);

/*
 * Global Function Prototypes in qla_gs.c source file.
 */
extern void *qla2x00_prep_ms_iocb(scsi_qla_host_t *, uint32_t, uint32_t);
extern void *qla24xx_prep_ms_iocb(scsi_qla_host_t *, uint32_t, uint32_t);
extern int qla2x00_ga_nxt(scsi_qla_host_t *, fc_port_t *);
extern int qla2x00_gid_pt(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_gpn_id(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_gnn_id(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_rft_id(scsi_qla_host_t *);
extern int qla2x00_rff_id(scsi_qla_host_t *);
extern int qla2x00_rnn_id(scsi_qla_host_t *);
extern int qla2x00_rsnn_nn(scsi_qla_host_t *);
extern void *qla2x00_prep_ms_fdmi_iocb(scsi_qla_host_t *, uint32_t, uint32_t);
extern void *qla24xx_prep_ms_fdmi_iocb(scsi_qla_host_t *, uint32_t, uint32_t);
extern int qla2x00_fdmi_register(scsi_qla_host_t *);
extern int qla2x00_gfpn_id(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_gpsc(scsi_qla_host_t *, sw_info_t *);
extern void qla2x00_get_sym_node_name(scsi_qla_host_t *, uint8_t *);

/*
 * Global Function Prototypes in qla_attr.c source file.
 */
struct device_attribute;
extern struct device_attribute *qla2x00_host_attrs[];
struct fc_function_template;
extern struct fc_function_template qla2xxx_transport_functions;
extern struct fc_function_template qla2xxx_transport_vport_functions;
extern void qla2x00_alloc_sysfs_attr(scsi_qla_host_t *);
extern void qla2x00_free_sysfs_attr(scsi_qla_host_t *);
extern void qla2x00_init_host_attr(scsi_qla_host_t *);
extern void qla2x00_alloc_sysfs_attr(scsi_qla_host_t *);
extern void qla2x00_free_sysfs_attr(scsi_qla_host_t *);
extern int qla2x00_loopback_test(scsi_qla_host_t *, struct msg_echo_lb *, uint16_t *);
extern int qla2x00_echo_test(scsi_qla_host_t *,
	struct msg_echo_lb *, uint16_t *);
extern int qla24xx_update_all_fcp_prio(scsi_qla_host_t *);
extern int qla24xx_fcp_prio_cfg_valid(struct qla_fcp_prio_cfg *, uint8_t);

/*
 * Global Function Prototypes in qla_dfs.c source file.
 */
extern int qla2x00_dfs_setup(scsi_qla_host_t *);
extern int qla2x00_dfs_remove(scsi_qla_host_t *);

/* Globa function prototypes for multi-q */
extern int qla25xx_request_irq(struct rsp_que *);
extern int qla25xx_init_req_que(struct scsi_qla_host *, struct req_que *);
extern int qla25xx_init_rsp_que(struct scsi_qla_host *, struct rsp_que *);
extern int qla25xx_create_req_que(struct qla_hw_data *, uint16_t, uint8_t,
	uint16_t, int, uint8_t);
extern int qla25xx_create_rsp_que(struct qla_hw_data *, uint16_t, uint8_t,
	uint16_t, int);
extern int qla25xx_update_req_que(struct scsi_qla_host *, uint8_t, uint8_t);
extern void qla2x00_init_response_q_entries(struct rsp_que *);
extern int qla25xx_delete_req_que(struct scsi_qla_host *, struct req_que *);
extern int qla25xx_delete_rsp_que(struct scsi_qla_host *, struct rsp_que *);
extern int qla25xx_create_queues(struct scsi_qla_host *, uint8_t);
extern int qla25xx_delete_queues(struct scsi_qla_host *);
extern uint16_t qla24xx_rd_req_reg(struct qla_hw_data *, uint16_t);
extern uint16_t qla25xx_rd_req_reg(struct qla_hw_data *, uint16_t);
extern void qla24xx_wrt_req_reg(struct qla_hw_data *, uint16_t, uint16_t);
extern void qla25xx_wrt_req_reg(struct qla_hw_data *, uint16_t, uint16_t);
extern void qla25xx_wrt_rsp_reg(struct qla_hw_data *, uint16_t, uint16_t);
extern void qla24xx_wrt_rsp_reg(struct qla_hw_data *, uint16_t, uint16_t);

/* qla82xx related functions */

/* PCI related functions */
extern int qla82xx_pci_config(struct scsi_qla_host *);
extern int qla82xx_pci_mem_read_2M(struct qla_hw_data *, u64, void *, int);
extern int qla82xx_pci_mem_write_2M(struct qla_hw_data *, u64, void *, int);
extern char *qla82xx_pci_info_str(struct scsi_qla_host *, char *);
extern int qla82xx_pci_region_offset(struct pci_dev *, int);
extern int qla82xx_pci_region_len(struct pci_dev *, int);
extern int qla82xx_iospace_config(struct qla_hw_data *);

/* Initialization related functions */
extern void qla82xx_reset_chip(struct scsi_qla_host *);
extern void qla82xx_config_rings(struct scsi_qla_host *);
extern int qla82xx_nvram_config(struct scsi_qla_host *);
extern int qla82xx_pinit_from_rom(scsi_qla_host_t *);
extern int qla82xx_load_firmware(scsi_qla_host_t *);
extern int qla82xx_reset_hw(scsi_qla_host_t *);
extern int qla82xx_load_risc_blob(scsi_qla_host_t *, uint32_t *);
extern void qla82xx_watchdog(scsi_qla_host_t *);

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

/* Interrupt related */
extern irqreturn_t qla82xx_intr_handler(int, void *);
extern irqreturn_t qla82xx_msi_handler(int, void *);
extern irqreturn_t qla82xx_msix_default(int, void *);
extern irqreturn_t qla82xx_msix_rsp_q(int, void *);
extern void qla82xx_enable_intrs(struct qla_hw_data *);
extern void qla82xx_disable_intrs(struct qla_hw_data *);
extern void qla82xx_mbx_completion(scsi_qla_host_t *, uint16_t);
extern void qla82xx_poll(int, void *);
extern void qla82xx_init_flags(struct qla_hw_data *);

/* ISP 8021 hardware related */
extern int qla82xx_crb_win_lock(struct qla_hw_data *);
extern void qla82xx_crb_win_unlock(struct qla_hw_data *);
extern int qla82xx_pci_get_crb_addr_2M(struct qla_hw_data *, ulong *);
extern int qla82xx_wr_32(struct qla_hw_data *, ulong, u32);
extern int qla82xx_rd_32(struct qla_hw_data *, ulong);
extern int qla82xx_rdmem(struct qla_hw_data *, u64, void *, int);
extern int qla82xx_wrmem(struct qla_hw_data *, u64, void *, int);
extern int qla82xx_check_for_bad_spd(struct qla_hw_data *);
extern int qla82xx_load_fw(scsi_qla_host_t *);
extern int qla82xx_rom_lock(struct qla_hw_data *);
extern void qla82xx_rom_unlock(struct qla_hw_data *);
extern int qla82xx_rom_fast_read(struct qla_hw_data *, int , int *);
extern int qla82xx_do_rom_fast_read(struct qla_hw_data *, int, int *);
extern unsigned long qla82xx_decode_crb_addr(unsigned long);

/* ISP 8021 IDC */
extern void qla82xx_clear_drv_active(struct qla_hw_data *);
extern int qla82xx_idc_lock(struct qla_hw_data *);
extern void qla82xx_idc_unlock(struct qla_hw_data *);
extern int qla82xx_device_state_handler(scsi_qla_host_t *);

extern void qla2x00_set_model_info(scsi_qla_host_t *, uint8_t *,
    size_t, char *);
extern int qla82xx_mbx_intr_enable(scsi_qla_host_t *);
extern int qla82xx_mbx_intr_disable(scsi_qla_host_t *);
extern void qla82xx_start_iocbs(srb_t *);
extern int qla82xx_fcoe_ctx_reset(scsi_qla_host_t *);
extern void qla82xx_wait_for_pending_commands(scsi_qla_host_t *);

/* BSG related functions */
extern int qla24xx_bsg_request(struct fc_bsg_job *);
extern int qla24xx_bsg_timeout(struct fc_bsg_job *);
extern int qla84xx_reset_chip(scsi_qla_host_t *, uint16_t);
extern int qla2x00_issue_iocb_timeout(scsi_qla_host_t *, void *,
	dma_addr_t, size_t, uint32_t);
extern int qla2x00_get_idma_speed(scsi_qla_host_t *, uint16_t,
	uint16_t *, uint16_t *);
#endif /* _QLA_GBL_H */
