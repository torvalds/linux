/*
 * This file is part of the zfcp device driver for
 * FCP adapters for IBM System z9 and zSeries.
 *
 * (C) Copyright IBM Corp. 2002, 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef ZFCP_EXT_H
#define ZFCP_EXT_H

#include "zfcp_def.h"

extern struct zfcp_data zfcp_data;

/******************************** SYSFS  *************************************/
extern struct attribute_group *zfcp_driver_attr_groups[];
extern int  zfcp_sysfs_adapter_create_files(struct device *);
extern void zfcp_sysfs_adapter_remove_files(struct device *);
extern int  zfcp_sysfs_port_create_files(struct device *, u32);
extern void zfcp_sysfs_port_remove_files(struct device *, u32);
extern int  zfcp_sysfs_unit_create_files(struct device *);
extern void zfcp_sysfs_unit_remove_files(struct device *);
extern void zfcp_sysfs_port_release(struct device *);
extern void zfcp_sysfs_unit_release(struct device *);

/**************************** CONFIGURATION  *********************************/
extern struct zfcp_unit *zfcp_get_unit_by_lun(struct zfcp_port *, fcp_lun_t);
extern struct zfcp_port *zfcp_get_port_by_wwpn(struct zfcp_adapter *, wwn_t);
extern struct zfcp_port *zfcp_get_port_by_did(struct zfcp_adapter *, u32);
struct zfcp_adapter *zfcp_get_adapter_by_busid(char *);
extern struct zfcp_adapter *zfcp_adapter_enqueue(struct ccw_device *);
extern int    zfcp_adapter_debug_register(struct zfcp_adapter *);
extern void   zfcp_adapter_dequeue(struct zfcp_adapter *);
extern void   zfcp_adapter_debug_unregister(struct zfcp_adapter *);
extern struct zfcp_port *zfcp_port_enqueue(struct zfcp_adapter *, wwn_t,
					   u32, u32);
extern void   zfcp_port_dequeue(struct zfcp_port *);
extern struct zfcp_unit *zfcp_unit_enqueue(struct zfcp_port *, fcp_lun_t);
extern void   zfcp_unit_dequeue(struct zfcp_unit *);

/******************************* S/390 IO ************************************/
extern int  zfcp_ccw_register(void);

extern void zfcp_qdio_zero_sbals(struct qdio_buffer **, int, int);
extern int  zfcp_qdio_allocate(struct zfcp_adapter *);
extern int  zfcp_qdio_allocate_queues(struct zfcp_adapter *);
extern void zfcp_qdio_free_queues(struct zfcp_adapter *);
extern int  zfcp_qdio_determine_pci(struct zfcp_qdio_queue *,
				    struct zfcp_fsf_req *);

extern volatile struct qdio_buffer_element *zfcp_qdio_sbale_req
	(struct zfcp_fsf_req *, int, int);
extern volatile struct qdio_buffer_element *zfcp_qdio_sbale_curr
	(struct zfcp_fsf_req *);
extern int zfcp_qdio_sbals_from_sg
	(struct zfcp_fsf_req *, unsigned long, struct scatterlist *, int, int);
extern int zfcp_qdio_sbals_from_scsicmnd
	(struct zfcp_fsf_req *, unsigned long, struct scsi_cmnd *);


/******************************** FSF ****************************************/
extern int  zfcp_fsf_open_port(struct zfcp_erp_action *);
extern int  zfcp_fsf_close_port(struct zfcp_erp_action *);
extern int  zfcp_fsf_close_physical_port(struct zfcp_erp_action *);

extern int  zfcp_fsf_open_unit(struct zfcp_erp_action *);
extern int  zfcp_fsf_close_unit(struct zfcp_erp_action *);

extern int  zfcp_fsf_exchange_config_data(struct zfcp_erp_action *);
extern int  zfcp_fsf_exchange_config_data_sync(struct zfcp_adapter *,
                                              struct fsf_qtcb_bottom_config *);
extern int  zfcp_fsf_exchange_port_data(struct zfcp_erp_action *);
extern int  zfcp_fsf_exchange_port_data_sync(struct zfcp_adapter *,
                                             struct fsf_qtcb_bottom_port *);
extern int  zfcp_fsf_control_file(struct zfcp_adapter *, struct zfcp_fsf_req **,
				  u32, u32, struct zfcp_sg_list *);
extern void zfcp_fsf_start_timer(struct zfcp_fsf_req *, unsigned long);
extern void zfcp_erp_start_timer(struct zfcp_fsf_req *);
extern void zfcp_fsf_req_dismiss_all(struct zfcp_adapter *);
extern int  zfcp_fsf_status_read(struct zfcp_adapter *, int);
extern int zfcp_fsf_req_create(struct zfcp_adapter *, u32, int, mempool_t *,
			       unsigned long *, struct zfcp_fsf_req **);
extern int zfcp_fsf_send_ct(struct zfcp_send_ct *, mempool_t *,
			    struct zfcp_erp_action *);
extern int zfcp_fsf_send_els(struct zfcp_send_els *);
extern int  zfcp_fsf_send_fcp_command_task(struct zfcp_adapter *,
					   struct zfcp_unit *,
					   struct scsi_cmnd *, int, int);
extern int  zfcp_fsf_req_complete(struct zfcp_fsf_req *);
extern void zfcp_fsf_incoming_els(struct zfcp_fsf_req *);
extern void zfcp_fsf_req_free(struct zfcp_fsf_req *);
extern struct zfcp_fsf_req *zfcp_fsf_send_fcp_command_task_management(
	struct zfcp_adapter *, struct zfcp_unit *, u8, int);
extern struct zfcp_fsf_req *zfcp_fsf_abort_fcp_command(
	unsigned long, struct zfcp_adapter *, struct zfcp_unit *, int);

/******************************* FC/FCP **************************************/
extern int  zfcp_nameserver_enqueue(struct zfcp_adapter *);
extern int  zfcp_ns_gid_pn_request(struct zfcp_erp_action *);
extern int  zfcp_check_ct_response(struct ct_hdr *);
extern int  zfcp_handle_els_rjt(u32, struct zfcp_ls_rjt_par *);
extern void zfcp_plogi_evaluate(struct zfcp_port *, struct fsf_plogi *);

/******************************* SCSI ****************************************/
extern int  zfcp_adapter_scsi_register(struct zfcp_adapter *);
extern void zfcp_adapter_scsi_unregister(struct zfcp_adapter *);
extern void zfcp_set_fcp_dl(struct fcp_cmnd_iu *, fcp_dl_t);
extern char *zfcp_get_fcp_rsp_info_ptr(struct fcp_rsp_iu *);
extern void set_host_byte(int *, char);
extern void set_driver_byte(int *, char);
extern char *zfcp_get_fcp_sns_info_ptr(struct fcp_rsp_iu *);
extern fcp_dl_t zfcp_get_fcp_dl(struct fcp_cmnd_iu *);

extern int zfcp_scsi_command_async(struct zfcp_adapter *,struct zfcp_unit *,
				   struct scsi_cmnd *, int);
extern int zfcp_scsi_command_sync(struct zfcp_unit *, struct scsi_cmnd *, int);
extern struct fc_function_template zfcp_transport_functions;

/******************************** ERP ****************************************/
extern void zfcp_erp_modify_adapter_status(struct zfcp_adapter *, u32, int);
extern int  zfcp_erp_adapter_reopen(struct zfcp_adapter *, int);
extern int  zfcp_erp_adapter_shutdown(struct zfcp_adapter *, int);
extern void zfcp_erp_adapter_failed(struct zfcp_adapter *);

extern void zfcp_erp_modify_port_status(struct zfcp_port *, u32, int);
extern int  zfcp_erp_port_reopen(struct zfcp_port *, int);
extern int  zfcp_erp_port_shutdown(struct zfcp_port *, int);
extern int  zfcp_erp_port_forced_reopen(struct zfcp_port *, int);
extern void zfcp_erp_port_failed(struct zfcp_port *);
extern int  zfcp_erp_port_reopen_all(struct zfcp_adapter *, int);

extern void zfcp_erp_modify_unit_status(struct zfcp_unit *, u32, int);
extern int  zfcp_erp_unit_reopen(struct zfcp_unit *, int);
extern int  zfcp_erp_unit_shutdown(struct zfcp_unit *, int);
extern void zfcp_erp_unit_failed(struct zfcp_unit *);

extern int  zfcp_erp_thread_setup(struct zfcp_adapter *);
extern int  zfcp_erp_thread_kill(struct zfcp_adapter *);
extern int  zfcp_erp_wait(struct zfcp_adapter *);
extern void zfcp_erp_async_handler(struct zfcp_erp_action *, unsigned long);

extern int  zfcp_test_link(struct zfcp_port *);

extern void zfcp_erp_port_boxed(struct zfcp_port *);
extern void zfcp_erp_unit_boxed(struct zfcp_unit *);
extern void zfcp_erp_port_access_denied(struct zfcp_port *);
extern void zfcp_erp_unit_access_denied(struct zfcp_unit *);
extern void zfcp_erp_adapter_access_changed(struct zfcp_adapter *);
extern void zfcp_erp_port_access_changed(struct zfcp_port *);
extern void zfcp_erp_unit_access_changed(struct zfcp_unit *);

/******************************** AUX ****************************************/
extern void zfcp_hba_dbf_event_fsf_response(struct zfcp_fsf_req *);
extern void zfcp_hba_dbf_event_fsf_unsol(const char *, struct zfcp_adapter *,
					 struct fsf_status_read_buffer *);
extern void zfcp_hba_dbf_event_qdio(struct zfcp_adapter *,
				    unsigned int, unsigned int, unsigned int,
				    int, int);

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
extern int zfcp_reqlist_isempty(struct zfcp_adapter *);

#endif	/* ZFCP_EXT_H */
