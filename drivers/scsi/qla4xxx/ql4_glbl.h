/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2012 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#ifndef __QLA4x_GBL_H
#define	__QLA4x_GBL_H

struct iscsi_cls_conn;

int qla4xxx_hw_reset(struct scsi_qla_host *ha);
int ql4xxx_lock_drvr_wait(struct scsi_qla_host *a);
int qla4xxx_send_command_to_isp(struct scsi_qla_host *ha, struct srb *srb);
int qla4xxx_initialize_adapter(struct scsi_qla_host *ha, int is_reset);
int qla4xxx_soft_reset(struct scsi_qla_host *ha);
irqreturn_t qla4xxx_intr_handler(int irq, void *dev_id);

void qla4xxx_free_ddb(struct scsi_qla_host *ha, struct ddb_entry *ddb_entry);
void qla4xxx_process_aen(struct scsi_qla_host *ha, uint8_t process_aen);

int qla4xxx_get_dhcp_ip_address(struct scsi_qla_host *ha);
int qla4xxx_abort_task(struct scsi_qla_host *ha, struct srb *srb);
int qla4xxx_reset_lun(struct scsi_qla_host *ha, struct ddb_entry *ddb_entry,
		      int lun);
int qla4xxx_reset_target(struct scsi_qla_host *ha,
			 struct ddb_entry *ddb_entry);
int qla4xxx_get_flash(struct scsi_qla_host *ha, dma_addr_t dma_addr,
		      uint32_t offset, uint32_t len);
int qla4xxx_get_firmware_status(struct scsi_qla_host *ha);
int qla4xxx_get_firmware_state(struct scsi_qla_host *ha);
int qla4xxx_initialize_fw_cb(struct scsi_qla_host *ha);

/* FIXME: Goodness!  this really wants a small struct to hold the
 * parameters. On x86 the args will get passed on the stack! */
int qla4xxx_get_fwddb_entry(struct scsi_qla_host *ha,
			    uint16_t fw_ddb_index,
			    struct dev_db_entry *fw_ddb_entry,
			    dma_addr_t fw_ddb_entry_dma,
			    uint32_t *num_valid_ddb_entries,
			    uint32_t *next_ddb_index,
			    uint32_t *fw_ddb_device_state,
			    uint32_t *conn_err_detail,
			    uint16_t *tcp_source_port_num,
			    uint16_t *connection_id);

int qla4xxx_set_ddb_entry(struct scsi_qla_host * ha, uint16_t fw_ddb_index,
			  dma_addr_t fw_ddb_entry_dma, uint32_t *mbx_sts);
uint8_t qla4xxx_get_ifcb(struct scsi_qla_host *ha, uint32_t *mbox_cmd,
			 uint32_t *mbox_sts, dma_addr_t init_fw_cb_dma);
int qla4xxx_conn_close_sess_logout(struct scsi_qla_host *ha,
				   uint16_t fw_ddb_index,
				   uint16_t connection_id,
				   uint16_t option);
int qla4xxx_disable_acb(struct scsi_qla_host *ha);
int qla4xxx_set_acb(struct scsi_qla_host *ha, uint32_t *mbox_cmd,
		    uint32_t *mbox_sts, dma_addr_t acb_dma);
int qla4xxx_get_acb(struct scsi_qla_host *ha, dma_addr_t acb_dma,
		    uint32_t acb_type, uint32_t len);
int qla4xxx_get_ip_state(struct scsi_qla_host *ha, uint32_t acb_idx,
			 uint32_t ip_idx, uint32_t *sts);
void qla4xxx_mark_device_missing(struct iscsi_cls_session *cls_session);
u16 rd_nvram_word(struct scsi_qla_host *ha, int offset);
u8 rd_nvram_byte(struct scsi_qla_host *ha, int offset);
void qla4xxx_get_crash_record(struct scsi_qla_host *ha);
int qla4xxx_is_nvram_configuration_valid(struct scsi_qla_host *ha);
int qla4xxx_about_firmware(struct scsi_qla_host *ha);
void qla4xxx_interrupt_service_routine(struct scsi_qla_host *ha,
				       uint32_t intr_status);
int qla4xxx_init_rings(struct scsi_qla_host *ha);
void qla4xxx_srb_compl(struct kref *ref);
struct srb *qla4xxx_del_from_active_array(struct scsi_qla_host *ha,
		uint32_t index);
int qla4xxx_process_ddb_changed(struct scsi_qla_host *ha, uint32_t fw_ddb_index,
		uint32_t state, uint32_t conn_error);
void qla4xxx_dump_buffer(void *b, uint32_t size);
int qla4xxx_send_marker_iocb(struct scsi_qla_host *ha,
	struct ddb_entry *ddb_entry, int lun, uint16_t mrkr_mod);
int qla4xxx_set_flash(struct scsi_qla_host *ha, dma_addr_t dma_addr,
		      uint32_t offset, uint32_t length, uint32_t options);
int qla4xxx_mailbox_command(struct scsi_qla_host *ha, uint8_t inCount,
		uint8_t outCount, uint32_t *mbx_cmd, uint32_t *mbx_sts);
int qla4xxx_get_chap_index(struct scsi_qla_host *ha, char *username,
			   char *password, int bidi, uint16_t *chap_index);

void qla4xxx_queue_iocb(struct scsi_qla_host *ha);
void qla4xxx_complete_iocb(struct scsi_qla_host *ha);
int qla4xxx_get_sys_info(struct scsi_qla_host *ha);
int qla4xxx_iospace_config(struct scsi_qla_host *ha);
void qla4xxx_pci_config(struct scsi_qla_host *ha);
int qla4xxx_start_firmware(struct scsi_qla_host *ha);
irqreturn_t qla4xxx_intr_handler(int irq, void *dev_id);
uint16_t qla4xxx_rd_shdw_req_q_out(struct scsi_qla_host *ha);
uint16_t qla4xxx_rd_shdw_rsp_q_in(struct scsi_qla_host *ha);
int qla4xxx_request_irqs(struct scsi_qla_host *ha);
void qla4xxx_free_irqs(struct scsi_qla_host *ha);
void qla4xxx_process_response_queue(struct scsi_qla_host *ha);
void qla4xxx_wake_dpc(struct scsi_qla_host *ha);
void qla4xxx_get_conn_event_log(struct scsi_qla_host *ha);
void qla4xxx_mailbox_premature_completion(struct scsi_qla_host *ha);
void qla4xxx_dump_registers(struct scsi_qla_host *ha);
uint8_t qla4xxx_update_local_ifcb(struct scsi_qla_host *ha,
				  uint32_t *mbox_cmd,
				  uint32_t *mbox_sts,
				  struct addr_ctrl_blk *init_fw_cb,
				  dma_addr_t init_fw_cb_dma);

void qla4_8xxx_pci_config(struct scsi_qla_host *);
int qla4_8xxx_iospace_config(struct scsi_qla_host *ha);
int qla4_8xxx_load_risc(struct scsi_qla_host *);
irqreturn_t qla4_82xx_intr_handler(int irq, void *dev_id);
void qla4_82xx_queue_iocb(struct scsi_qla_host *ha);
void qla4_82xx_complete_iocb(struct scsi_qla_host *ha);

int qla4_82xx_crb_win_lock(struct scsi_qla_host *);
void qla4_82xx_crb_win_unlock(struct scsi_qla_host *);
int qla4_82xx_pci_get_crb_addr_2M(struct scsi_qla_host *, ulong *);
void qla4_82xx_wr_32(struct scsi_qla_host *, ulong, u32);
uint32_t qla4_82xx_rd_32(struct scsi_qla_host *, ulong);
int qla4_82xx_pci_mem_read_2M(struct scsi_qla_host *, u64, void *, int);
int qla4_82xx_pci_mem_write_2M(struct scsi_qla_host *ha, u64, void *, int);
int qla4_82xx_isp_reset(struct scsi_qla_host *ha);
void qla4_82xx_interrupt_service_routine(struct scsi_qla_host *ha,
		uint32_t intr_status);
uint16_t qla4_82xx_rd_shdw_req_q_out(struct scsi_qla_host *ha);
uint16_t qla4_82xx_rd_shdw_rsp_q_in(struct scsi_qla_host *ha);
int qla4_8xxx_get_sys_info(struct scsi_qla_host *ha);
void qla4_8xxx_watchdog(struct scsi_qla_host *ha);
int qla4_8xxx_stop_firmware(struct scsi_qla_host *ha);
int qla4_8xxx_get_flash_info(struct scsi_qla_host *ha);
void qla4_82xx_enable_intrs(struct scsi_qla_host *ha);
void qla4_82xx_disable_intrs(struct scsi_qla_host *ha);
int qla4_8xxx_enable_msix(struct scsi_qla_host *ha);
void qla4_8xxx_disable_msix(struct scsi_qla_host *ha);
irqreturn_t qla4_8xxx_msi_handler(int irq, void *dev_id);
irqreturn_t qla4_8xxx_default_intr_handler(int irq, void *dev_id);
irqreturn_t qla4_8xxx_msix_rsp_q(int irq, void *dev_id);
void qla4xxx_mark_all_devices_missing(struct scsi_qla_host *ha);
void qla4xxx_dead_adapter_cleanup(struct scsi_qla_host *ha);
int qla4_82xx_idc_lock(struct scsi_qla_host *ha);
void qla4_82xx_idc_unlock(struct scsi_qla_host *ha);
int qla4_8xxx_device_state_handler(struct scsi_qla_host *ha);
void qla4_8xxx_need_qsnt_handler(struct scsi_qla_host *ha);
void qla4_8xxx_clear_drv_active(struct scsi_qla_host *ha);
void qla4_8xxx_set_drv_active(struct scsi_qla_host *ha);
int qla4xxx_conn_open(struct scsi_qla_host *ha, uint16_t fw_ddb_index);
int qla4xxx_set_param_ddbentry(struct scsi_qla_host *ha,
			       struct ddb_entry *ddb_entry,
			       struct iscsi_cls_conn *cls_conn,
			       uint32_t *mbx_sts);
int qla4xxx_session_logout_ddb(struct scsi_qla_host *ha,
			       struct ddb_entry *ddb_entry, int options);
int qla4xxx_req_ddb_entry(struct scsi_qla_host *ha, uint32_t fw_ddb_index,
			  uint32_t *mbx_sts);
int qla4xxx_clear_ddb_entry(struct scsi_qla_host *ha, uint32_t fw_ddb_index);
int qla4xxx_send_passthru0(struct iscsi_task *task);
void qla4xxx_free_ddb_index(struct scsi_qla_host *ha);
int qla4xxx_get_mgmt_data(struct scsi_qla_host *ha, uint16_t fw_ddb_index,
			  uint16_t stats_size, dma_addr_t stats_dma);
void qla4xxx_update_session_conn_param(struct scsi_qla_host *ha,
				       struct ddb_entry *ddb_entry);
void qla4xxx_update_session_conn_fwddb_param(struct scsi_qla_host *ha,
					     struct ddb_entry *ddb_entry);
int qla4xxx_bootdb_by_index(struct scsi_qla_host *ha,
			    struct dev_db_entry *fw_ddb_entry,
			    dma_addr_t fw_ddb_entry_dma, uint16_t ddb_index);
int qla4xxx_get_chap(struct scsi_qla_host *ha, char *username,
		     char *password, uint16_t idx);
int qla4xxx_get_nvram(struct scsi_qla_host *ha, dma_addr_t nvram_dma,
		      uint32_t offset, uint32_t size);
int qla4xxx_set_nvram(struct scsi_qla_host *ha, dma_addr_t nvram_dma,
		      uint32_t offset, uint32_t size);
int qla4xxx_restore_factory_defaults(struct scsi_qla_host *ha,
				     uint32_t region, uint32_t field0,
				     uint32_t field1);
int qla4xxx_get_ddb_index(struct scsi_qla_host *ha, uint16_t *ddb_index);
void qla4xxx_login_flash_ddb(struct iscsi_cls_session *cls_session);
int qla4xxx_unblock_ddb(struct iscsi_cls_session *cls_session);
int qla4xxx_unblock_flash_ddb(struct iscsi_cls_session *cls_session);
int qla4xxx_flash_ddb_change(struct scsi_qla_host *ha, uint32_t fw_ddb_index,
			     struct ddb_entry *ddb_entry, uint32_t state);
int qla4xxx_ddb_change(struct scsi_qla_host *ha, uint32_t fw_ddb_index,
		       struct ddb_entry *ddb_entry, uint32_t state);
void qla4xxx_build_ddb_list(struct scsi_qla_host *ha, int is_reset);
int qla4xxx_post_aen_work(struct scsi_qla_host *ha,
			  enum iscsi_host_event_code aen_code,
			  uint32_t data_size, uint8_t *data);
int qla4xxx_ping_iocb(struct scsi_qla_host *ha, uint32_t options,
		      uint32_t payload_size, uint32_t pid, uint8_t *ipaddr);
int qla4xxx_post_ping_evt_work(struct scsi_qla_host *ha,
			       uint32_t status, uint32_t pid,
			       uint32_t data_size, uint8_t *data);
int qla4xxx_flashdb_by_index(struct scsi_qla_host *ha,
			     struct dev_db_entry *fw_ddb_entry,
			     dma_addr_t fw_ddb_entry_dma, uint16_t ddb_index);

/* BSG Functions */
int qla4xxx_bsg_request(struct bsg_job *bsg_job);
int qla4xxx_process_vendor_specific(struct bsg_job *bsg_job);

void qla4xxx_arm_relogin_timer(struct ddb_entry *ddb_entry);
int qla4xxx_get_minidump_template(struct scsi_qla_host *ha,
				  dma_addr_t phys_addr);
int qla4xxx_req_template_size(struct scsi_qla_host *ha);
void qla4_8xxx_alloc_sysfs_attr(struct scsi_qla_host *ha);
void qla4_8xxx_free_sysfs_attr(struct scsi_qla_host *ha);
void qla4xxx_alloc_fw_dump(struct scsi_qla_host *ha);
int qla4_82xx_try_start_fw(struct scsi_qla_host *ha);
int qla4_8xxx_need_reset(struct scsi_qla_host *ha);
int qla4_82xx_md_rd_32(struct scsi_qla_host *ha, uint32_t off, uint32_t *data);
int qla4_82xx_md_wr_32(struct scsi_qla_host *ha, uint32_t off, uint32_t data);
void qla4_82xx_rom_lock_recovery(struct scsi_qla_host *ha);
void qla4_82xx_queue_mbox_cmd(struct scsi_qla_host *ha, uint32_t *mbx_cmd,
			      int incount);
void qla4_82xx_process_mbox_intr(struct scsi_qla_host *ha, int outcount);
void qla4xxx_queue_mbox_cmd(struct scsi_qla_host *ha, uint32_t *mbx_cmd,
			    int incount);
void qla4xxx_process_mbox_intr(struct scsi_qla_host *ha, int outcount);
void qla4_8xxx_dump_peg_reg(struct scsi_qla_host *ha);
void qla4_83xx_disable_intrs(struct scsi_qla_host *ha);
void qla4_83xx_enable_intrs(struct scsi_qla_host *ha);
int qla4_83xx_start_firmware(struct scsi_qla_host *ha);
irqreturn_t qla4_83xx_intr_handler(int irq, void *dev_id);
void qla4_83xx_interrupt_service_routine(struct scsi_qla_host *ha,
					 uint32_t intr_status);
int qla4_83xx_isp_reset(struct scsi_qla_host *ha);
void qla4_83xx_queue_iocb(struct scsi_qla_host *ha);
void qla4_83xx_complete_iocb(struct scsi_qla_host *ha);
uint32_t qla4_83xx_rd_reg(struct scsi_qla_host *ha, ulong addr);
void qla4_83xx_wr_reg(struct scsi_qla_host *ha, ulong addr, uint32_t val);
int qla4_83xx_rd_reg_indirect(struct scsi_qla_host *ha, uint32_t addr,
			      uint32_t *data);
int qla4_83xx_wr_reg_indirect(struct scsi_qla_host *ha, uint32_t addr,
			      uint32_t data);
int qla4_83xx_drv_lock(struct scsi_qla_host *ha);
void qla4_83xx_drv_unlock(struct scsi_qla_host *ha);
void qla4_83xx_rom_lock_recovery(struct scsi_qla_host *ha);
void qla4_83xx_queue_mbox_cmd(struct scsi_qla_host *ha, uint32_t *mbx_cmd,
			      int incount);
void qla4_83xx_process_mbox_intr(struct scsi_qla_host *ha, int outcount);
void qla4_83xx_read_reset_template(struct scsi_qla_host *ha);
void qla4_83xx_set_idc_dontreset(struct scsi_qla_host *ha);
int qla4_83xx_idc_dontreset(struct scsi_qla_host *ha);
int qla4_83xx_lockless_flash_read_u32(struct scsi_qla_host *ha,
				      uint32_t flash_addr, uint8_t *p_data,
				      int u32_word_count);
void qla4_83xx_clear_idc_dontreset(struct scsi_qla_host *ha);
void qla4_83xx_need_reset_handler(struct scsi_qla_host *ha);
int qla4_83xx_flash_read_u32(struct scsi_qla_host *ha, uint32_t flash_addr,
			     uint8_t *p_data, int u32_word_count);
void qla4_83xx_get_idc_param(struct scsi_qla_host *ha);
void qla4_8xxx_set_rst_ready(struct scsi_qla_host *ha);
void qla4_8xxx_clear_rst_ready(struct scsi_qla_host *ha);
int qla4_8xxx_device_bootstrap(struct scsi_qla_host *ha);
void qla4_8xxx_get_minidump(struct scsi_qla_host *ha);
int qla4_8xxx_intr_disable(struct scsi_qla_host *ha);
int qla4_8xxx_intr_enable(struct scsi_qla_host *ha);
int qla4_8xxx_set_param(struct scsi_qla_host *ha, int param);
int qla4_8xxx_update_idc_reg(struct scsi_qla_host *ha);
int qla4_83xx_post_idc_ack(struct scsi_qla_host *ha);
void qla4_83xx_disable_pause(struct scsi_qla_host *ha);
void qla4_83xx_enable_mbox_intrs(struct scsi_qla_host *ha);
int qla4_83xx_can_perform_reset(struct scsi_qla_host *ha);
int qla4xxx_get_default_ddb(struct scsi_qla_host *ha, uint32_t options,
			    dma_addr_t dma_addr);
int qla4xxx_get_uni_chap_at_index(struct scsi_qla_host *ha, char *username,
				  char *password, uint16_t chap_index);
int qla4xxx_disable_acb(struct scsi_qla_host *ha);
int qla4xxx_set_acb(struct scsi_qla_host *ha, uint32_t *mbox_cmd,
		    uint32_t *mbox_sts, dma_addr_t acb_dma);
int qla4xxx_get_acb(struct scsi_qla_host *ha, dma_addr_t acb_dma,
		    uint32_t acb_type, uint32_t len);
int qla4_84xx_config_acb(struct scsi_qla_host *ha, int acb_config);

extern int ql4xextended_error_logging;
extern int ql4xdontresethba;
extern int ql4xenablemsix;
extern int ql4xmdcapmask;
extern int ql4xenablemd;

extern struct device_attribute *qla4xxx_host_attrs[];
#endif /* _QLA4x_GBL_H */
