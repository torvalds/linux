/*
 * zfcp device driver
 *
 * External function declarations.
 *
 * Copyright IBM Corporation 2002, 2008
 */

#ifndef ZFCP_EXT_H
#define ZFCP_EXT_H

#include "zfcp_def.h"

/* zfcp_aux.c */
extern struct zfcp_unit *zfcp_get_unit_by_lun(struct zfcp_port *, u64);
extern struct zfcp_port *zfcp_get_port_by_wwpn(struct zfcp_adapter *, u64);
extern int zfcp_adapter_enqueue(struct ccw_device *);
extern void zfcp_adapter_dequeue(struct zfcp_adapter *);
extern struct zfcp_port *zfcp_port_enqueue(struct zfcp_adapter *, u64, u32,
					   u32);
extern void zfcp_port_dequeue(struct zfcp_port *);
extern struct zfcp_unit *zfcp_unit_enqueue(struct zfcp_port *, u64);
extern void zfcp_unit_dequeue(struct zfcp_unit *);
extern int zfcp_reqlist_isempty(struct zfcp_adapter *);
extern void zfcp_sg_free_table(struct scatterlist *, int);
extern int zfcp_sg_setup_table(struct scatterlist *, int);

/* zfcp_ccw.c */
extern int zfcp_ccw_register(void);
extern struct zfcp_adapter *zfcp_get_adapter_by_busid(char *);

/* zfcp_cfdc.c */
extern struct miscdevice zfcp_cfdc_misc;

/* zfcp_dbf.c */
extern int zfcp_adapter_debug_register(struct zfcp_adapter *);
extern void zfcp_adapter_debug_unregister(struct zfcp_adapter *);
extern void zfcp_rec_dbf_event_thread(u8, struct zfcp_adapter *);
extern void zfcp_rec_dbf_event_thread_lock(u8, struct zfcp_adapter *);
extern void zfcp_rec_dbf_event_adapter(u8, void *, struct zfcp_adapter *);
extern void zfcp_rec_dbf_event_port(u8, void *, struct zfcp_port *);
extern void zfcp_rec_dbf_event_unit(u8, void *, struct zfcp_unit *);
extern void zfcp_rec_dbf_event_trigger(u8, void *, u8, u8, void *,
				       struct zfcp_adapter *,
				       struct zfcp_port *, struct zfcp_unit *);
extern void zfcp_rec_dbf_event_action(u8, struct zfcp_erp_action *);
extern void zfcp_hba_dbf_event_fsf_response(struct zfcp_fsf_req *);
extern void zfcp_hba_dbf_event_fsf_unsol(const char *, struct zfcp_adapter *,
					 struct fsf_status_read_buffer *);
extern void zfcp_hba_dbf_event_qdio(struct zfcp_adapter *, unsigned int, int,
				    int);
extern void zfcp_hba_dbf_event_berr(struct zfcp_adapter *,
				    struct zfcp_fsf_req *);
extern void zfcp_san_dbf_event_ct_request(struct zfcp_fsf_req *);
extern void zfcp_san_dbf_event_ct_response(struct zfcp_fsf_req *);
extern void zfcp_san_dbf_event_els_request(struct zfcp_fsf_req *);
extern void zfcp_san_dbf_event_els_response(struct zfcp_fsf_req *);
extern void zfcp_san_dbf_event_incoming_els(struct zfcp_fsf_req *);
extern void zfcp_scsi_dbf_event_result(const char *, int, struct zfcp_adapter *,
				       struct scsi_cmnd *,
				       struct zfcp_fsf_req *);
extern void zfcp_scsi_dbf_event_abort(const char *, struct zfcp_adapter *,
				      struct scsi_cmnd *, struct zfcp_fsf_req *,
				      unsigned long);
extern void zfcp_scsi_dbf_event_devreset(const char *, u8, struct zfcp_unit *,
					 struct scsi_cmnd *);

/* zfcp_erp.c */
extern void zfcp_erp_modify_adapter_status(struct zfcp_adapter *, u8, void *,
					   u32, int);
extern void zfcp_erp_adapter_reopen(struct zfcp_adapter *, int, u8, void *);
extern void zfcp_erp_adapter_shutdown(struct zfcp_adapter *, int, u8, void *);
extern void zfcp_erp_adapter_failed(struct zfcp_adapter *, u8, void *);
extern void zfcp_erp_modify_port_status(struct zfcp_port *, u8, void *, u32,
					int);
extern int  zfcp_erp_port_reopen(struct zfcp_port *, int, u8, void *);
extern void zfcp_erp_port_shutdown(struct zfcp_port *, int, u8, void *);
extern void zfcp_erp_port_forced_reopen(struct zfcp_port *, int, u8, void *);
extern void zfcp_erp_port_failed(struct zfcp_port *, u8, void *);
extern void zfcp_erp_modify_unit_status(struct zfcp_unit *, u8, void *, u32,
					int);
extern void zfcp_erp_unit_reopen(struct zfcp_unit *, int, u8, void *);
extern void zfcp_erp_unit_shutdown(struct zfcp_unit *, int, u8, void *);
extern void zfcp_erp_unit_failed(struct zfcp_unit *, u8, void *);
extern int  zfcp_erp_thread_setup(struct zfcp_adapter *);
extern void zfcp_erp_thread_kill(struct zfcp_adapter *);
extern void zfcp_erp_wait(struct zfcp_adapter *);
extern void zfcp_erp_notify(struct zfcp_erp_action *, unsigned long);
extern void zfcp_erp_port_boxed(struct zfcp_port *, u8, void *);
extern void zfcp_erp_unit_boxed(struct zfcp_unit *, u8, void *);
extern void zfcp_erp_port_access_denied(struct zfcp_port *, u8, void *);
extern void zfcp_erp_unit_access_denied(struct zfcp_unit *, u8, void *);
extern void zfcp_erp_adapter_access_changed(struct zfcp_adapter *, u8, void *);
extern void zfcp_erp_timeout_handler(unsigned long);
extern void zfcp_erp_port_strategy_open_lookup(struct work_struct *);

/* zfcp_fc.c */
extern int zfcp_scan_ports(struct zfcp_adapter *);
extern void _zfcp_scan_ports_later(struct work_struct *);
extern void zfcp_fc_incoming_els(struct zfcp_fsf_req *);
extern int zfcp_fc_ns_gid_pn(struct zfcp_erp_action *);
extern void zfcp_fc_plogi_evaluate(struct zfcp_port *, struct fsf_plogi *);
extern void zfcp_test_link(struct zfcp_port *);
extern void zfcp_fc_nameserver_init(struct zfcp_adapter *);

/* zfcp_fsf.c */
extern int zfcp_fsf_open_port(struct zfcp_erp_action *);
extern int zfcp_fsf_open_wka_port(struct zfcp_wka_port *);
extern int zfcp_fsf_close_wka_port(struct zfcp_wka_port *);
extern int zfcp_fsf_close_port(struct zfcp_erp_action *);
extern int zfcp_fsf_close_physical_port(struct zfcp_erp_action *);
extern int zfcp_fsf_open_unit(struct zfcp_erp_action *);
extern int zfcp_fsf_close_unit(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_config_data(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_config_data_sync(struct zfcp_adapter *,
					      struct fsf_qtcb_bottom_config *);
extern int zfcp_fsf_exchange_port_data(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_port_data_sync(struct zfcp_adapter *,
					    struct fsf_qtcb_bottom_port *);
extern struct zfcp_fsf_req *zfcp_fsf_control_file(struct zfcp_adapter *,
						  struct zfcp_fsf_cfdc *);
extern void zfcp_fsf_req_dismiss_all(struct zfcp_adapter *);
extern int zfcp_fsf_status_read(struct zfcp_adapter *);
extern int zfcp_status_read_refill(struct zfcp_adapter *adapter);
extern int zfcp_fsf_send_ct(struct zfcp_send_ct *, mempool_t *,
			    struct zfcp_erp_action *);
extern int zfcp_fsf_send_els(struct zfcp_send_els *);
extern int zfcp_fsf_send_fcp_command_task(struct zfcp_adapter *,
					  struct zfcp_unit *,
					  struct scsi_cmnd *, int, int);
extern void zfcp_fsf_req_complete(struct zfcp_fsf_req *);
extern void zfcp_fsf_req_free(struct zfcp_fsf_req *);
extern struct zfcp_fsf_req *zfcp_fsf_send_fcp_ctm(struct zfcp_adapter *,
						  struct zfcp_unit *, u8, int);
extern struct zfcp_fsf_req *zfcp_fsf_abort_fcp_command(unsigned long,
						       struct zfcp_adapter *,
						       struct zfcp_unit *, int);

/* zfcp_qdio.c */
extern int zfcp_qdio_allocate(struct zfcp_adapter *);
extern void zfcp_qdio_free(struct zfcp_adapter *);
extern int zfcp_qdio_send(struct zfcp_fsf_req *);
extern struct qdio_buffer_element *zfcp_qdio_sbale_req(struct zfcp_fsf_req *);
extern struct qdio_buffer_element *zfcp_qdio_sbale_curr(struct zfcp_fsf_req *);
extern int zfcp_qdio_sbals_from_sg(struct zfcp_fsf_req *, unsigned long,
				   struct scatterlist *, int);
extern int zfcp_qdio_open(struct zfcp_adapter *);
extern void zfcp_qdio_close(struct zfcp_adapter *);

/* zfcp_scsi.c */
extern struct zfcp_data zfcp_data;
extern int zfcp_adapter_scsi_register(struct zfcp_adapter *);
extern void zfcp_adapter_scsi_unregister(struct zfcp_adapter *);
extern char *zfcp_get_fcp_sns_info_ptr(struct fcp_rsp_iu *);
extern struct fc_function_template zfcp_transport_functions;

/* zfcp_sysfs.c */
extern struct attribute_group zfcp_sysfs_unit_attrs;
extern struct attribute_group zfcp_sysfs_adapter_attrs;
extern struct attribute_group zfcp_sysfs_port_attrs;
extern struct device_attribute *zfcp_sysfs_sdev_attrs[];
extern struct device_attribute *zfcp_sysfs_shost_attrs[];

#endif	/* ZFCP_EXT_H */
