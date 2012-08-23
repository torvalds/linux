/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
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

/*
 * Contains base driver definitions.
 */

/*
 *  bfa_drv.h Linux driver data structures.
 */

#ifndef __BFAD_DRV_H__
#define __BFAD_DRV_H__

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/aer.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_bsg_fc.h>
#include <scsi/scsi_devinfo.h>

#include "bfa_modules.h"
#include "bfa_fcs.h"
#include "bfa_defs_fcs.h"

#include "bfa_plog.h"
#include "bfa_cs.h"

#define BFAD_DRIVER_NAME	"bfa"
#ifdef BFA_DRIVER_VERSION
#define BFAD_DRIVER_VERSION    BFA_DRIVER_VERSION
#else
#define BFAD_DRIVER_VERSION    "3.1.2.0"
#endif

#define BFAD_PROTO_NAME FCPI_NAME
#define BFAD_IRQ_FLAGS IRQF_SHARED

#ifndef FC_PORTSPEED_8GBIT
#define FC_PORTSPEED_8GBIT 0x10
#endif

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
#define BFAD_FCS_INIT_DONE			0x00000080
#define BFAD_HAL_INIT_FAIL			0x00000100
#define BFAD_FC4_PROBE_DONE			0x00000200
#define BFAD_PORT_DELETE			0x00000001
#define BFAD_INTX_ON				0x00000400
#define BFAD_EEH_BUSY				0x00000800
#define BFAD_EEH_PCI_CHANNEL_IO_PERM_FAILURE	0x00001000
/*
 * BFAD related definition
 */
#define SCSI_SCAN_DELAY		HZ
#define BFAD_STOP_TIMEOUT	30
#define BFAD_SUSPEND_TIMEOUT	BFAD_STOP_TIMEOUT

/*
 * BFAD configuration parameter default values
 */
#define BFAD_LUN_QUEUE_DEPTH	32
#define BFAD_IO_MAX_SGE		SG_ALL
#define BFAD_MIN_SECTORS	128 /* 64k   */
#define BFAD_MAX_SECTORS	0xFFFF  /* 32 MB */

#define bfad_isr_t irq_handler_t

#define MAX_MSIX_ENTRY 22

struct bfad_msix_s {
	struct bfad_s *bfad;
	struct msix_entry msix;
	char name[32];
};

/*
 * Only append to the enums defined here to avoid any versioning
 * needed between trace utility and driver version
 */
enum {
	BFA_TRC_LDRV_BFAD		= 1,
	BFA_TRC_LDRV_IM			= 2,
	BFA_TRC_LDRV_BSG		= 3,
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
	struct bfad_s	*bfad;
	struct bfa_fcs_lport_s *fcs_port;
	u32	roles;
	s32		flags;
	u32	supported_fc4s;
	enum bfad_port_pvb_type pvb_type;
	struct bfad_im_port_s *im_port;	/* IM specific data */
	/* port debugfs specific data */
	struct dentry *port_debugfs_root;
};

/*
 * VPORT data structure
 */
struct bfad_vport_s {
	struct bfad_port_s     drv_port;
	struct bfa_fcs_vport_s fcs_vport;
	struct completion *comp_del;
	struct list_head list_entry;
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
	u32	rport_del_timeout;
	u32	ioc_queue_depth;
	u32	lun_queue_depth;
	u32	io_max_sge;
	u32	binding_method;
};

union bfad_tmp_buf {
	/* From struct bfa_adapter_attr_s */
	char		manufacturer[BFA_ADAPTER_MFG_NAME_LEN];
	char		serial_num[BFA_ADAPTER_SERIAL_NUM_LEN];
	char		model[BFA_ADAPTER_MODEL_NAME_LEN];
	char		fw_ver[BFA_VERSION_LEN];
	char		optrom_ver[BFA_VERSION_LEN];

	/* From struct bfa_ioc_pci_attr_s */
	u8		chip_rev[BFA_IOC_CHIP_REV_LEN];  /*  chip revision */

	wwn_t		wwn[BFA_FCS_MAX_LPORTS];
};

/*
 * BFAD (PCI function) data structure
 */
struct bfad_s {
	bfa_sm_t	sm;	/* state machine */
	struct list_head list_entry;
	struct bfa_s	bfa;
	struct bfa_fcs_s bfa_fcs;
	struct pci_dev *pcidev;
	const char *pci_name;
	struct bfa_pcidev_s hal_pcidev;
	struct bfa_ioc_pci_attr_s pci_attr;
	void __iomem   *pci_bar0_kva;
	void __iomem   *pci_bar2_kva;
	struct completion comp;
	struct completion suspend;
	struct completion enable_comp;
	struct completion disable_comp;
	bfa_boolean_t   disable_active;
	struct bfad_port_s     pport;	/* physical port of the BFAD */
	struct bfa_meminfo_s meminfo;
	struct bfa_iocfc_cfg_s   ioc_cfg;
	u32	inst_no;	/* BFAD instance number */
	u32	bfad_flags;
	spinlock_t      bfad_lock;
	struct task_struct *bfad_tsk;
	struct bfad_cfg_param_s cfg_data;
	struct bfad_msix_s msix_tab[MAX_MSIX_ENTRY];
	int		nvec;
	char	adapter_name[BFA_ADAPTER_SYM_NAME_LEN];
	char	port_name[BFA_ADAPTER_SYM_NAME_LEN];
	struct timer_list hal_tmo;
	unsigned long   hs_start;
	struct bfad_im_s *im;		/* IM specific data */
	struct bfa_trc_mod_s  *trcmod;
	struct bfa_plog_s      plog_buf;
	int		ref_count;
	union bfad_tmp_buf tmp_buf;
	struct fc_host_statistics link_stats;
	struct list_head pbc_vport_list;
	/* debugfs specific data */
	char *regdata;
	u32 reglen;
	struct dentry *bfad_dentry_files[5];
	struct list_head	free_aen_q;
	struct list_head	active_aen_q;
	struct bfa_aen_entry_s	aen_list[BFA_AEN_MAX_ENTRY];
	spinlock_t		bfad_aen_spinlock;
	struct list_head	vport_list;
};

/* BFAD state machine events */
enum bfad_sm_event {
	BFAD_E_CREATE			= 1,
	BFAD_E_KTHREAD_CREATE_FAILED	= 2,
	BFAD_E_INIT			= 3,
	BFAD_E_INIT_SUCCESS		= 4,
	BFAD_E_INIT_FAILED		= 5,
	BFAD_E_INTR_INIT_FAILED		= 6,
	BFAD_E_FCS_EXIT_COMP		= 7,
	BFAD_E_EXIT_COMP		= 8,
	BFAD_E_STOP			= 9
};

/*
 * RPORT data structure
 */
struct bfad_rport_s {
	struct bfa_fcs_rport_s fcs_rport;
};

struct bfad_buf_info {
	void		*virt;
	dma_addr_t      phys;
	u32	size;
};

struct bfad_fcxp {
	struct bfad_port_s    *port;
	struct bfa_rport_s *bfa_rport;
	bfa_status_t    req_status;
	u16	tag;
	u16	rsp_len;
	u16	rsp_maxlen;
	u8		use_ireqbuf;
	u8		use_irspbuf;
	u32	num_req_sgles;
	u32	num_rsp_sgles;
	struct fchs_s	fchs;
	void		*reqbuf_info;
	void		*rspbuf_info;
	struct bfa_sge_s  *req_sge;
	struct bfa_sge_s  *rsp_sge;
	fcxp_send_cb_t  send_cbfn;
	void		*send_cbarg;
	void		*bfa_fcxp;
	struct completion comp;
};

struct bfad_hal_comp {
	bfa_status_t    status;
	struct completion comp;
};

#define BFA_LOG(level, bfad, mask, fmt, arg...)				\
do {									\
	if (((mask) == 4) || (level[1] <= '4'))				\
		dev_printk(level, &((bfad)->pcidev)->dev, fmt, ##arg);	\
} while (0)

bfa_status_t	bfad_vport_create(struct bfad_s *bfad, u16 vf_id,
				  struct bfa_lport_cfg_s *port_cfg,
				  struct device *dev);
bfa_status_t	bfad_vf_create(struct bfad_s *bfad, u16 vf_id,
			       struct bfa_lport_cfg_s *port_cfg);
bfa_status_t	bfad_cfg_pport(struct bfad_s *bfad, enum bfa_lport_role role);
bfa_status_t	bfad_drv_init(struct bfad_s *bfad);
bfa_status_t	bfad_start_ops(struct bfad_s *bfad);
void		bfad_drv_start(struct bfad_s *bfad);
void		bfad_uncfg_pport(struct bfad_s *bfad);
void		bfad_stop(struct bfad_s *bfad);
void		bfad_fcs_stop(struct bfad_s *bfad);
void		bfad_remove_intr(struct bfad_s *bfad);
void		bfad_hal_mem_release(struct bfad_s *bfad);
void		bfad_hcb_comp(void *arg, bfa_status_t status);

int		bfad_setup_intr(struct bfad_s *bfad);
void		bfad_remove_intr(struct bfad_s *bfad);
void		bfad_update_hal_cfg(struct bfa_iocfc_cfg_s *bfa_cfg);
bfa_status_t	bfad_hal_mem_alloc(struct bfad_s *bfad);
void		bfad_bfa_tmo(unsigned long data);
void		bfad_init_timer(struct bfad_s *bfad);
int		bfad_pci_init(struct pci_dev *pdev, struct bfad_s *bfad);
void		bfad_pci_uninit(struct pci_dev *pdev, struct bfad_s *bfad);
void		bfad_drv_uninit(struct bfad_s *bfad);
int		bfad_worker(void *ptr);
void		bfad_debugfs_init(struct bfad_port_s *port);
void		bfad_debugfs_exit(struct bfad_port_s *port);

void bfad_pci_remove(struct pci_dev *pdev);
int bfad_pci_probe(struct pci_dev *pdev, const struct pci_device_id *pid);
void bfad_rport_online_wait(struct bfad_s *bfad);
int bfad_get_linkup_delay(struct bfad_s *bfad);
int bfad_install_msix_handler(struct bfad_s *bfad);

extern struct idr bfad_im_port_index;
extern struct pci_device_id bfad_id_table[];
extern struct list_head bfad_list;
extern char	*os_name;
extern char	*os_patch;
extern char	*host_name;
extern int	num_rports;
extern int	num_ios;
extern int	num_tms;
extern int	num_fcxps;
extern int	num_ufbufs;
extern int	reqq_size;
extern int	rspq_size;
extern int	num_sgpgs;
extern int      rport_del_timeout;
extern int      bfa_lun_queue_depth;
extern int      bfa_io_max_sge;
extern int      bfa_log_level;
extern int      ioc_auto_recover;
extern int      bfa_linkup_delay;
extern int      msix_disable_cb;
extern int      msix_disable_ct;
extern int      fdmi_enable;
extern int      supported_fc4s;
extern int	pcie_max_read_reqsz;
extern int	max_xfer_size;
extern int bfa_debugfs_enable;
extern struct mutex bfad_mutex;

#endif /* __BFAD_DRV_H__ */
