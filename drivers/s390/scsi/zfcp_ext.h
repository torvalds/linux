/*
 * zfcp device driver
 *
 * External function declarations.
 *
 * Copyright IBM Corporation 2002, 2010
 */

#ifndef ZFCP_EXT_H
#define ZFCP_EXT_H

#include <linux/types.h>
#include <scsi/fc/fc_els.h>
#include "zfcp_def.h"
#include "zfcp_fc.h"

/* zfcp_aux.c */
extern struct zfcp_unit *zfcp_get_unit_by_lun(struct zfcp_port *, u64);
extern struct zfcp_port *zfcp_get_port_by_wwpn(struct zfcp_adapter *, u64);
extern struct zfcp_adapter *zfcp_adapter_enqueue(struct ccw_device *);
extern struct zfcp_port *zfcp_port_enqueue(struct zfcp_adapter *, u64, u32,
					   u32);
extern struct zfcp_unit *zfcp_unit_enqueue(struct zfcp_port *, u64);
extern void zfcp_sg_free_table(struct scatterlist *, int);
extern int zfcp_sg_setup_table(struct scatterlist *, int);
extern void zfcp_device_unregister(struct device *,
				   const struct attribute_group *);
extern void zfcp_adapter_release(struct kref *);
extern void zfcp_adapter_unregister(struct zfcp_adapter *);

/* zfcp_ccw.c */
extern int zfcp_ccw_priv_sch(struct zfcp_adapter *);
extern struct ccw_driver zfcp_ccw_driver;
extern struct zfcp_adapter *zfcp_ccw_adapter_by_cdev(struct ccw_device *);
extern void zfcp_ccw_adapter_put(struct zfcp_adapter *);

/* zfcp_cfdc.c */
extern struct miscdevice zfcp_cfdc_misc;

/* zfcp_dbf.c */
extern int zfcp_dbf_adapter_register(struct zfcp_adapter *);
extern void zfcp_dbf_adapter_unregister(struct zfcp_dbf *);
extern void zfcp_dbf_rec_thread(char *, struct zfcp_dbf *);
extern void zfcp_dbf_rec_thread_lock(char *, struct zfcp_dbf *);
extern void zfcp_dbf_rec_adapter(char *, void *, struct zfcp_dbf *);
extern void zfcp_dbf_rec_port(char *, void *, struct zfcp_port *);
extern void zfcp_dbf_rec_unit(char *, void *, struct zfcp_unit *);
extern void zfcp_dbf_rec_trigger(char *, void *, u8, u8, void *,
				 struct zfcp_adapter *, struct zfcp_port *,
				 struct zfcp_unit *);
extern void zfcp_dbf_rec_action(char *, struct zfcp_erp_action *);
extern void _zfcp_dbf_hba_fsf_response(const char *, int, struct zfcp_fsf_req *,
				       struct zfcp_dbf *);
extern void _zfcp_dbf_hba_fsf_unsol(const char *, int level, struct zfcp_dbf *,
					  struct fsf_status_read_buffer *);
extern void zfcp_dbf_hba_qdio(struct zfcp_dbf *, unsigned int, int, int);
extern void zfcp_dbf_hba_berr(struct zfcp_dbf *, struct zfcp_fsf_req *);
extern void zfcp_dbf_san_ct_request(struct zfcp_fsf_req *, u32);
extern void zfcp_dbf_san_ct_response(struct zfcp_fsf_req *);
extern void zfcp_dbf_san_els_request(struct zfcp_fsf_req *);
extern void zfcp_dbf_san_els_response(struct zfcp_fsf_req *);
extern void zfcp_dbf_san_incoming_els(struct zfcp_fsf_req *);
extern void _zfcp_dbf_scsi(const char *, const char *, int, struct zfcp_dbf *,
			   struct scsi_cmnd *, struct zfcp_fsf_req *,
			   unsigned long);

/* zfcp_erp.c */
extern void zfcp_erp_modify_adapter_status(struct zfcp_adapter *, char *,
					   void *, u32, int);
extern void zfcp_erp_adapter_reopen(struct zfcp_adapter *, int, char *, void *);
extern void zfcp_erp_adapter_shutdown(struct zfcp_adapter *, int, char *,
				      void *);
extern void zfcp_erp_adapter_failed(struct zfcp_adapter *, char *, void *);
extern void zfcp_erp_modify_port_status(struct zfcp_port *, char *, void *, u32,
					int);
extern int  zfcp_erp_port_reopen(struct zfcp_port *, int, char *, void *);
extern void zfcp_erp_port_shutdown(struct zfcp_port *, int, char *, void *);
extern void zfcp_erp_port_forced_reopen(struct zfcp_port *, int, char *,
					void *);
extern void zfcp_erp_port_failed(struct zfcp_port *, char *, void *);
extern void zfcp_erp_modify_unit_status(struct zfcp_unit *, char *, void *, u32,
					int);
extern void zfcp_erp_unit_reopen(struct zfcp_unit *, int, char *, void *);
extern void zfcp_erp_unit_shutdown(struct zfcp_unit *, int, char *, void *);
extern void zfcp_erp_unit_failed(struct zfcp_unit *, char *, void *);
extern int  zfcp_erp_thread_setup(struct zfcp_adapter *);
extern void zfcp_erp_thread_kill(struct zfcp_adapter *);
extern void zfcp_erp_wait(struct zfcp_adapter *);
extern void zfcp_erp_notify(struct zfcp_erp_action *, unsigned long);
extern void zfcp_erp_port_boxed(struct zfcp_port *, char *, void *);
extern void zfcp_erp_unit_boxed(struct zfcp_unit *, char *, void *);
extern void zfcp_erp_port_access_denied(struct zfcp_port *, char *, void *);
extern void zfcp_erp_unit_access_denied(struct zfcp_unit *, char *, void *);
extern void zfcp_erp_adapter_access_changed(struct zfcp_adapter *, char *,
					    void *);
extern void zfcp_erp_timeout_handler(unsigned long);

/* zfcp_fc.c */
extern void zfcp_fc_scan_ports(struct work_struct *);
extern void zfcp_fc_incoming_els(struct zfcp_fsf_req *);
extern void zfcp_fc_port_did_lookup(struct work_struct *);
extern void zfcp_fc_trigger_did_lookup(struct zfcp_port *);
extern void zfcp_fc_plogi_evaluate(struct zfcp_port *, struct fc_els_flogi *);
extern void zfcp_fc_test_link(struct zfcp_port *);
extern void zfcp_fc_link_test_work(struct work_struct *);
extern void zfcp_fc_wka_ports_force_offline(struct zfcp_fc_wka_ports *);
extern int zfcp_fc_gs_setup(struct zfcp_adapter *);
extern void zfcp_fc_gs_destroy(struct zfcp_adapter *);
extern int zfcp_fc_exec_bsg_job(struct fc_bsg_job *);
extern int zfcp_fc_timeout_bsg_job(struct fc_bsg_job *);

/* zfcp_fsf.c */
extern int zfcp_fsf_open_port(struct zfcp_erp_action *);
extern int zfcp_fsf_open_wka_port(struct zfcp_fc_wka_port *);
extern int zfcp_fsf_close_wka_port(struct zfcp_fc_wka_port *);
extern int zfcp_fsf_close_port(struct zfcp_erp_action *);
extern int zfcp_fsf_close_physical_port(struct zfcp_erp_action *);
extern int zfcp_fsf_open_unit(struct zfcp_erp_action *);
extern int zfcp_fsf_close_unit(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_config_data(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_config_data_sync(struct zfcp_qdio *,
					      struct fsf_qtcb_bottom_config *);
extern int zfcp_fsf_exchange_port_data(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_port_data_sync(struct zfcp_qdio *,
					    struct fsf_qtcb_bottom_port *);
extern struct zfcp_fsf_req *zfcp_fsf_control_file(struct zfcp_adapter *,
						  struct zfcp_fsf_cfdc *);
extern void zfcp_fsf_req_dismiss_all(struct zfcp_adapter *);
extern int zfcp_fsf_status_read(struct zfcp_qdio *);
extern int zfcp_status_read_refill(struct zfcp_adapter *adapter);
extern int zfcp_fsf_send_ct(struct zfcp_fc_wka_port *, struct zfcp_fsf_ct_els *,
			    mempool_t *, unsigned int);
extern int zfcp_fsf_send_els(struct zfcp_adapter *, u32,
			     struct zfcp_fsf_ct_els *, unsigned int);
extern int zfcp_fsf_send_fcp_command_task(struct zfcp_unit *,
					  struct scsi_cmnd *);
extern void zfcp_fsf_req_free(struct zfcp_fsf_req *);
extern struct zfcp_fsf_req *zfcp_fsf_send_fcp_ctm(struct zfcp_unit *, u8);
extern struct zfcp_fsf_req *zfcp_fsf_abort_fcp_command(unsigned long,
						       struct zfcp_unit *);
extern void zfcp_fsf_reqid_check(struct zfcp_qdio *, int);

/* zfcp_qdio.c */
extern int zfcp_qdio_setup(struct zfcp_adapter *);
extern void zfcp_qdio_destroy(struct zfcp_qdio *);
extern int zfcp_qdio_sbal_get(struct zfcp_qdio *);
extern int zfcp_qdio_send(struct zfcp_qdio *, struct zfcp_qdio_req *);
extern int zfcp_qdio_sbals_from_sg(struct zfcp_qdio *, struct zfcp_qdio_req *,
				   struct scatterlist *, int);
extern int zfcp_qdio_open(struct zfcp_qdio *);
extern void zfcp_qdio_close(struct zfcp_qdio *);

/* zfcp_scsi.c */
extern struct zfcp_data zfcp_data;
extern int zfcp_adapter_scsi_register(struct zfcp_adapter *);
extern void zfcp_adapter_scsi_unregister(struct zfcp_adapter *);
extern struct fc_function_template zfcp_transport_functions;
extern void zfcp_scsi_rport_work(struct work_struct *);
extern void zfcp_scsi_schedule_rport_register(struct zfcp_port *);
extern void zfcp_scsi_schedule_rport_block(struct zfcp_port *);
extern void zfcp_scsi_schedule_rports_block(struct zfcp_adapter *);
extern void zfcp_scsi_scan(struct work_struct *);

/* zfcp_sysfs.c */
extern struct attribute_group zfcp_sysfs_unit_attrs;
extern struct attribute_group zfcp_sysfs_adapter_attrs;
extern struct attribute_group zfcp_sysfs_port_attrs;
extern struct device_attribute *zfcp_sysfs_sdev_attrs[];
extern struct device_attribute *zfcp_sysfs_shost_attrs[];

#endif	/* ZFCP_EXT_H */
