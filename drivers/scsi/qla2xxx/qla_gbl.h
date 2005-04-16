/********************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic ISP2x00 device driver for Linux 2.6.x
* Copyright (C) 2003-2004 QLogic Corporation
* (www.qlogic.com)
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2, or (at your option) any
* later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
*
******************************************************************************
* Global include file.
******************************************************************************/


#ifndef __QLA_GBL_H
#define	__QLA_GBL_H

#include <linux/interrupt.h>

extern void qla2x00_remove_one(struct pci_dev *);
extern int qla2x00_probe_one(struct pci_dev *, struct qla_board_info *);

/*
 * Global Function Prototypes in qla_init.c source file.
 */
extern int qla2x00_initialize_adapter(scsi_qla_host_t *);
extern fc_port_t *qla2x00_alloc_fcport(scsi_qla_host_t *, int);

extern int qla2x00_loop_resync(scsi_qla_host_t *);

extern int qla2x00_find_new_loop_id(scsi_qla_host_t *, fc_port_t *);
extern int qla2x00_fabric_login(scsi_qla_host_t *, fc_port_t *, uint16_t *);
extern int qla2x00_local_device_login(scsi_qla_host_t *, uint16_t);

extern void qla2x00_restart_queues(scsi_qla_host_t *, uint8_t);

extern void qla2x00_rescan_fcports(scsi_qla_host_t *);

extern void qla2x00_tgt_free(scsi_qla_host_t *ha, uint16_t t);

extern int qla2x00_abort_isp(scsi_qla_host_t *);

/*
 * Global Data in qla_os.c source file.
 */
extern char qla2x00_version_str[];

extern int num_hosts;
extern int apiHBAInstance;

extern struct _qla2x00stats qla2x00_stats;
extern int ql2xretrycount;
extern int ql2xlogintimeout;
extern int qlport_down_retry;
extern int ql2xmaxqdepth;
extern int displayConfig;
extern int ql2xplogiabsentdevice;
extern int ql2xenablezio;
extern int ql2xintrdelaytimer;
extern int ql2xloginretrycount;

extern int ConfigRequired;

extern int Bind;
extern int ql2xsuspendcount;
#if defined(MODULE)
extern char *ql2xopts;
#endif

extern char *qla2x00_get_fw_version_str(struct scsi_qla_host *, char *);

extern void qla2x00_cmd_timeout(srb_t *);

extern int __qla2x00_suspend_lun(scsi_qla_host_t *, os_lun_t *, int, int, int);

extern void qla2x00_done(scsi_qla_host_t *);
extern void qla2x00_next(scsi_qla_host_t *);
extern void qla2x00_flush_failover_q(scsi_qla_host_t *, os_lun_t *);
extern void qla2x00_reset_lun_fo_counts(scsi_qla_host_t *, os_lun_t *);

extern void qla2x00_extend_timeout(struct scsi_cmnd *, int);

extern void qla2x00_mark_device_lost(scsi_qla_host_t *, fc_port_t *, int);
extern void qla2x00_mark_all_devices_lost(scsi_qla_host_t *);

extern void qla2x00_abort_queues(scsi_qla_host_t *, uint8_t);

extern void qla2x00_blink_led(scsi_qla_host_t *);

extern int qla2x00_down_timeout(struct semaphore *, unsigned long);

/*
 * Global Function Prototypes in qla_iocb.c source file.
 */
extern void qla2x00_isp_cmd(scsi_qla_host_t *);

extern uint16_t qla2x00_calc_iocbs_32(uint16_t);
extern uint16_t qla2x00_calc_iocbs_64(uint16_t);
extern void qla2x00_build_scsi_iocbs_32(srb_t *, cmd_entry_t *, uint16_t);
extern void qla2x00_build_scsi_iocbs_64(srb_t *, cmd_entry_t *, uint16_t);
extern int qla2x00_start_scsi(srb_t *sp);
int qla2x00_marker(scsi_qla_host_t *, uint16_t, uint16_t, uint8_t);
int __qla2x00_marker(scsi_qla_host_t *, uint16_t, uint16_t, uint8_t);

/*
 * Global Function Prototypes in qla_mbx.c source file.
 */
extern int
qla2x00_load_ram(scsi_qla_host_t *, dma_addr_t, uint16_t, uint16_t);

extern int
qla2x00_load_ram_ext(scsi_qla_host_t *, dma_addr_t, uint32_t, uint16_t);

extern int
qla2x00_execute_fw(scsi_qla_host_t *);

extern void
qla2x00_get_fw_version(scsi_qla_host_t *, uint16_t *,
    uint16_t *, uint16_t *, uint16_t *, uint32_t *);

extern int
qla2x00_get_fw_options(scsi_qla_host_t *, uint16_t *);

extern int
qla2x00_set_fw_options(scsi_qla_host_t *, uint16_t *);

extern int
qla2x00_mbx_reg_test(scsi_qla_host_t *);

extern int
qla2x00_verify_checksum(scsi_qla_host_t *);

extern int
qla2x00_issue_iocb(scsi_qla_host_t *, void *, dma_addr_t, size_t);

extern int
qla2x00_abort_command(scsi_qla_host_t *, srb_t *);

#if USE_ABORT_TGT
extern int
qla2x00_abort_target(fc_port_t *fcport);
#endif

extern int
qla2x00_target_reset(scsi_qla_host_t *, uint16_t, uint16_t);

extern int
qla2x00_get_adapter_id(scsi_qla_host_t *, uint16_t *, uint8_t *, uint8_t *,
    uint8_t *, uint16_t *);

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
qla2x00_login_local_device(scsi_qla_host_t *, uint16_t, uint16_t *, uint8_t);

extern int
qla2x00_fabric_logout(scsi_qla_host_t *ha, uint16_t loop_id);

extern int
qla2x00_full_login_lip(scsi_qla_host_t *ha);

extern int
qla2x00_get_id_list(scsi_qla_host_t *, void *, dma_addr_t, uint16_t *);

extern int
qla2x00_get_resource_cnts(scsi_qla_host_t *, uint16_t *, uint16_t *, uint16_t *,
    uint16_t *);

extern int
qla2x00_get_fcal_position_map(scsi_qla_host_t *ha, char *pos_map);

/*
 * Global Function Prototypes in qla_isr.c source file.
 */
extern irqreturn_t qla2100_intr_handler(int, void *, struct pt_regs *);
extern irqreturn_t qla2300_intr_handler(int, void *, struct pt_regs *);
extern void qla2x00_process_response_queue(struct scsi_qla_host *);

/*
 * Global Function Prototypes in qla_sup.c source file.
 */
extern void qla2x00_lock_nvram_access(scsi_qla_host_t *);
extern void qla2x00_unlock_nvram_access(scsi_qla_host_t *);
extern void qla2x00_release_nvram_protection(scsi_qla_host_t *);
extern uint16_t qla2x00_get_nvram_word(scsi_qla_host_t *, uint32_t);
extern void qla2x00_write_nvram_word(scsi_qla_host_t *, uint32_t, uint16_t);
/*
 * Global Function Prototypes in qla_dbg.c source file.
 */
extern void qla2100_fw_dump(scsi_qla_host_t *, int);
extern void qla2300_fw_dump(scsi_qla_host_t *, int);
extern void qla2100_ascii_fw_dump(scsi_qla_host_t *);
extern void qla2300_ascii_fw_dump(scsi_qla_host_t *);
extern void qla2x00_dump_regs(scsi_qla_host_t *);
extern void qla2x00_dump_buffer(uint8_t *, uint32_t);
extern void qla2x00_print_scsi_cmd(struct scsi_cmnd *);

/*
 * Global Function Prototypes in qla_gs.c source file.
 */
extern int qla2x00_ga_nxt(scsi_qla_host_t *, fc_port_t *);
extern int qla2x00_gid_pt(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_gpn_id(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_gnn_id(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_rft_id(scsi_qla_host_t *);
extern int qla2x00_rff_id(scsi_qla_host_t *);
extern int qla2x00_rnn_id(scsi_qla_host_t *);
extern int qla2x00_rsnn_nn(scsi_qla_host_t *);

/*
 * Global Function Prototypes in qla_rscn.c source file.
 */
extern fc_port_t *qla2x00_alloc_rscn_fcport(scsi_qla_host_t *, int);
extern int qla2x00_handle_port_rscn(scsi_qla_host_t *, uint32_t, fc_port_t *,
    int);
extern void qla2x00_process_iodesc(scsi_qla_host_t *, struct mbx_entry *);
extern void qla2x00_cancel_io_descriptors(scsi_qla_host_t *);

/*
 * Global Function Prototypes in qla_xioctl.c source file.
 */
#define qla2x00_enqueue_aen(ha, cmd, mode)	do { } while (0)
#define qla2x00_alloc_ioctl_mem(ha)		(0)
#define qla2x00_free_ioctl_mem(ha)		do { } while (0)

#endif /* _QLA_GBL_H */
