/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/**
 * Contains base driver definitions.
 */

/**
 *  bfa_drv.h Linux driver data structures.
 */

#ifndef __BFAD_DRV_H__
#define __BFAD_DRV_H__

#include "bfa_os_inc.h"

#include <bfa.h>
#include <bfa_svc.h>
#include <fcs/bfa_fcs.h>
#include <defs/bfa_defs_pci.h>
#include <defs/bfa_defs_port.h>
#include <defs/bfa_defs_rport.h>
#include <fcs/bfa_fcs_rport.h>
#include <defs/bfa_defs_vport.h>
#include <fcs/bfa_fcs_vport.h>

#include <cs/bfa_plog.h>
#include "aen/bfa_aen.h"
#include <log/bfa_log_linux.h>

#define BFAD_DRIVER_NAME        "bfa"
#ifdef BFA_DRIVER_VERSION
#define BFAD_DRIVER_VERSION    BFA_DRIVER_VERSION
#else
#define BFAD_DRIVER_VERSION    "2.0.0.0"
#endif


#define BFAD_IRQ_FLAGS IRQF_SHARED

/*
 * BFAD flags
 */
#define BFAD_MSIX_ON				0x00000001
#define BFAD_HAL_INIT_DONE			0x00000002
#define BFAD_DRV_INIT_DONE			0x00000004
#define BFAD_CFG_PPORT_DONE			0x00000008
#define BFAD_HAL_START_DONE			0x00000010
#define BFAD_PORT_ONLINE			0x00000020
#define BFAD_RPORT_ONLINE			0x00000040
#define BFAD_FCS_INIT_DONE                      0x00000080
#define BFAD_HAL_INIT_FAIL                      0x00000100
#define BFAD_FC4_PROBE_DONE                     0x00000200
#define BFAD_PORT_DELETE			0x00000001

/*
 * BFAD related definition
 */
#define SCSI_SCAN_DELAY		HZ
#define BFAD_STOP_TIMEOUT	30
#define BFAD_SUSPEND_TIMEOUT	BFAD_STOP_TIMEOUT

/*
 * BFAD configuration parameter default values
 */
#define BFAD_LUN_QUEUE_DEPTH 		32
#define BFAD_IO_MAX_SGE 		SG_ALL

#define bfad_isr_t irq_handler_t

#define MAX_MSIX_ENTRY 22

struct bfad_msix_s {
	struct bfad_s *bfad;
	struct msix_entry msix;
};

enum bfad_port_pvb_type {
	BFAD_PORT_PHYS_BASE = 0,
	BFAD_PORT_PHYS_VPORT = 1,
	BFAD_PORT_VF_BASE = 2,
	BFAD_PORT_VF_VPORT = 3,
};

/*
 * PORT data structure
 */
struct bfad_port_s {
	struct list_head list_entry;
	struct bfad_s         *bfad;
	struct bfa_fcs_port_s *fcs_port;
	u32        roles;
	s32         flags;
	u32        supported_fc4s;
	u8		ipfc_flags;
	enum bfad_port_pvb_type pvb_type;
	struct bfad_im_port_s *im_port;	/* IM specific data */
	struct bfad_tm_port_s *tm_port;	/* TM specific data */
	struct bfad_ipfc_port_s *ipfc_port;	/* IPFC specific data */
};

/*
 * VPORT data structure
 */
struct bfad_vport_s {
	struct bfad_port_s     drv_port;
	struct bfa_fcs_vport_s fcs_vport;
	struct completion *comp_del;
};

/*
 * VF data structure
 */
struct bfad_vf_s {
	bfa_fcs_vf_t    fcs_vf;
	struct bfad_port_s    base_port;	/* base port for vf */
	struct bfad_s   *bfad;
};

struct bfad_cfg_param_s {
	u32        rport_del_timeout;
	u32        ioc_queue_depth;
	u32        lun_queue_depth;
	u32        io_max_sge;
	u32        binding_method;
};

#define BFAD_AEN_MAX_APPS 8
struct bfad_aen_file_s {
	struct list_head  qe;
	struct bfad_s *bfad;
	s32 ri;
	s32 app_id;
};

/*
 * BFAD (PCI function) data structure
 */
struct bfad_s {
	struct list_head list_entry;
	struct bfa_s       bfa;
	struct bfa_fcs_s       bfa_fcs;
	struct pci_dev *pcidev;
	const char *pci_name;
	struct bfa_pcidev_s hal_pcidev;
	struct bfa_ioc_pci_attr_s pci_attr;
	unsigned long   pci_bar0_map;
	void __iomem   *pci_bar0_kva;
	struct completion comp;
	struct completion suspend;
	struct completion disable_comp;
	bfa_boolean_t   disable_active;
	struct bfad_port_s     pport;	/* physical port of the BFAD */
	struct bfa_meminfo_s meminfo;
	struct bfa_iocfc_cfg_s   ioc_cfg;
	u32        inst_no;	/* BFAD instance number */
	u32        bfad_flags;
	spinlock_t      bfad_lock;
	struct task_struct *bfad_tsk;
	struct bfad_cfg_param_s cfg_data;
	struct bfad_msix_s msix_tab[MAX_MSIX_ENTRY];
	int             nvec;
	char            adapter_name[BFA_ADAPTER_SYM_NAME_LEN];
	char            port_name[BFA_ADAPTER_SYM_NAME_LEN];
	struct timer_list hal_tmo;
	unsigned long   hs_start;
	struct bfad_im_s *im;		/* IM specific data */
	struct bfad_tm_s *tm;		/* TM specific data */
	struct bfad_ipfc_s *ipfc;	/* IPFC specific data */
	struct bfa_log_mod_s   log_data;
	struct bfa_trc_mod_s  *trcmod;
	struct bfa_log_mod_s  *logmod;
	struct bfa_aen_s      *aen;
	struct bfa_aen_s       aen_buf;
	struct bfad_aen_file_s file_buf[BFAD_AEN_MAX_APPS];
	struct list_head         file_q;
	struct list_head         file_free_q;
	struct bfa_plog_s      plog_buf;
	int             ref_count;
	bfa_boolean_t	ipfc_enabled;
	struct fc_host_statistics link_stats;

	struct kobject *bfa_kobj;
	struct kobject *ioc_kobj;
	struct kobject *pport_kobj;
	struct kobject *lport_kobj;
};

/*
 * RPORT data structure
 */
struct bfad_rport_s {
	struct bfa_fcs_rport_s fcs_rport;
};

struct bfad_buf_info {
	void           *virt;
	dma_addr_t      phys;
	u32        size;
};

struct bfad_fcxp {
	struct bfad_port_s    *port;
	struct bfa_rport_s *bfa_rport;
	bfa_status_t    req_status;
	u16        tag;
	u16        rsp_len;
	u16        rsp_maxlen;
	u8         use_ireqbuf;
	u8         use_irspbuf;
	u32        num_req_sgles;
	u32        num_rsp_sgles;
	struct fchs_s          fchs;
	void           *reqbuf_info;
	void           *rspbuf_info;
	struct bfa_sge_s  *req_sge;
	struct bfa_sge_s  *rsp_sge;
	fcxp_send_cb_t  send_cbfn;
	void           *send_cbarg;
	void           *bfa_fcxp;
	struct completion comp;
};

struct bfad_hal_comp {
	bfa_status_t    status;
	struct completion comp;
};

/*
 * Macro to obtain the immediate lower power
 * of two for the integer.
 */
#define nextLowerInt(x)                         	\
do {                                            	\
	int j;                                  	\
	(*x)--;    		                	\
	for (j = 1; j < (sizeof(int) * 8); j <<= 1)     \
		(*x) = (*x) | (*x) >> j;        	\
	(*x)++;                  	        	\
	(*x) = (*x) >> 1;                       	\
} while (0)


bfa_status_t    bfad_vport_create(struct bfad_s *bfad, u16 vf_id,
				  struct bfa_port_cfg_s *port_cfg);
bfa_status_t    bfad_vf_create(struct bfad_s *bfad, u16 vf_id,
			       struct bfa_port_cfg_s *port_cfg);
bfa_status_t    bfad_cfg_pport(struct bfad_s *bfad, enum bfa_port_role role);
bfa_status_t    bfad_drv_init(struct bfad_s *bfad);
bfa_status_t	bfad_start_ops(struct bfad_s *bfad);
void            bfad_drv_start(struct bfad_s *bfad);
void            bfad_uncfg_pport(struct bfad_s *bfad);
void            bfad_drv_stop(struct bfad_s *bfad);
void            bfad_remove_intr(struct bfad_s *bfad);
void            bfad_hal_mem_release(struct bfad_s *bfad);
void            bfad_hcb_comp(void *arg, bfa_status_t status);

int             bfad_setup_intr(struct bfad_s *bfad);
void            bfad_remove_intr(struct bfad_s *bfad);

void		bfad_update_hal_cfg(struct bfa_iocfc_cfg_s *bfa_cfg);
bfa_status_t	bfad_hal_mem_alloc(struct bfad_s *bfad);
void		bfad_bfa_tmo(unsigned long data);
void		bfad_init_timer(struct bfad_s *bfad);
int		bfad_pci_init(struct pci_dev *pdev, struct bfad_s *bfad);
void		bfad_pci_uninit(struct pci_dev *pdev, struct bfad_s *bfad);
void		bfad_fcs_port_cfg(struct bfad_s *bfad);
void		bfad_drv_uninit(struct bfad_s *bfad);
void		bfad_drv_log_level_set(struct bfad_s *bfad);
bfa_status_t	bfad_fc4_module_init(void);
void		bfad_fc4_module_exit(void);

bfa_status_t	bfad_os_kthread_create(struct bfad_s *bfad);
void		bfad_os_kthread_stop(struct bfad_s *bfad);
void		bfad_os_kthread_wakeup(struct bfad_s *bfad);
int		bfad_os_kthread_should_stop(void);
int		bfad_worker (void *ptr);

void bfad_pci_remove(struct pci_dev *pdev);
int bfad_pci_probe(struct pci_dev *pdev, const struct pci_device_id *pid);
void bfad_os_rport_online_wait(struct bfad_s *bfad);
int bfad_os_get_linkup_delay(struct bfad_s *bfad);
int bfad_install_msix_handler(struct bfad_s *bfad);

extern struct idr bfad_im_port_index;
extern struct list_head bfad_list;
extern int bfa_lun_queue_depth;
extern int bfad_supported_fc4s;
extern int bfa_linkup_delay;

#endif /* __BFAD_DRV_H__ */
