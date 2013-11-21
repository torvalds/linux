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
 *  bfad.c Linux driver PCI interface module.
 */
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <asm/uaccess.h>
#include <asm/fcntl.h>

#include "bfad_drv.h"
#include "bfad_im.h"
#include "bfa_fcs.h"
#include "bfa_defs.h"
#include "bfa.h"

BFA_TRC_FILE(LDRV, BFAD);
DEFINE_MUTEX(bfad_mutex);
LIST_HEAD(bfad_list);

static int	bfad_inst;
static int      num_sgpgs_parm;
int		supported_fc4s;
char		*host_name, *os_name, *os_patch;
int		num_rports, num_ios, num_tms;
int		num_fcxps, num_ufbufs;
int		reqq_size, rspq_size, num_sgpgs;
int		rport_del_timeout = BFA_FCS_RPORT_DEF_DEL_TIMEOUT;
int		bfa_lun_queue_depth = BFAD_LUN_QUEUE_DEPTH;
int		bfa_io_max_sge = BFAD_IO_MAX_SGE;
int		bfa_log_level = 3; /* WARNING log level */
int		ioc_auto_recover = BFA_TRUE;
int		bfa_linkup_delay = -1;
int		fdmi_enable = BFA_TRUE;
int		pcie_max_read_reqsz;
int		bfa_debugfs_enable = 1;
int		msix_disable_cb = 0, msix_disable_ct = 0;
int		max_xfer_size = BFAD_MAX_SECTORS >> 1;
int		max_rport_logins = BFA_FCS_MAX_RPORT_LOGINS;

/* Firmware releated */
u32	bfi_image_cb_size, bfi_image_ct_size, bfi_image_ct2_size;
u32	*bfi_image_cb, *bfi_image_ct, *bfi_image_ct2;

#define BFAD_FW_FILE_CB		"cbfw-3.2.1.1.bin"
#define BFAD_FW_FILE_CT		"ctfw-3.2.1.1.bin"
#define BFAD_FW_FILE_CT2	"ct2fw-3.2.1.1.bin"

static u32 *bfad_load_fwimg(struct pci_dev *pdev);
static void bfad_free_fwimg(void);
static void bfad_read_firmware(struct pci_dev *pdev, u32 **bfi_image,
		u32 *bfi_image_size, char *fw_name);

static const char *msix_name_ct[] = {
	"ctrl",
	"cpe0", "cpe1", "cpe2", "cpe3",
	"rme0", "rme1", "rme2", "rme3" };

static const char *msix_name_cb[] = {
	"cpe0", "cpe1", "cpe2", "cpe3",
	"rme0", "rme1", "rme2", "rme3",
	"eemc", "elpu0", "elpu1", "epss", "mlpu" };

MODULE_FIRMWARE(BFAD_FW_FILE_CB);
MODULE_FIRMWARE(BFAD_FW_FILE_CT);
MODULE_FIRMWARE(BFAD_FW_FILE_CT2);

module_param(os_name, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(os_name, "OS name of the hba host machine");
module_param(os_patch, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(os_patch, "OS patch level of the hba host machine");
module_param(host_name, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(host_name, "Hostname of the hba host machine");
module_param(num_rports, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_rports, "Max number of rports supported per port "
				"(physical/logical), default=1024");
module_param(num_ios, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_ios, "Max number of ioim requests, default=2000");
module_param(num_tms, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_tms, "Max number of task im requests, default=128");
module_param(num_fcxps, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_fcxps, "Max number of fcxp requests, default=64");
module_param(num_ufbufs, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_ufbufs, "Max number of unsolicited frame "
				"buffers, default=64");
module_param(reqq_size, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(reqq_size, "Max number of request queue elements, "
				"default=256");
module_param(rspq_size, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(rspq_size, "Max number of response queue elements, "
				"default=64");
module_param(num_sgpgs, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_sgpgs, "Number of scatter/gather pages, default=2048");
module_param(rport_del_timeout, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(rport_del_timeout, "Rport delete timeout, default=90 secs, "
					"Range[>0]");
module_param(bfa_lun_queue_depth, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bfa_lun_queue_depth, "Lun queue depth, default=32, Range[>0]");
module_param(bfa_io_max_sge, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bfa_io_max_sge, "Max io scatter/gather elements, default=255");
module_param(bfa_log_level, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bfa_log_level, "Driver log level, default=3, "
				"Range[Critical:1|Error:2|Warning:3|Info:4]");
module_param(ioc_auto_recover, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ioc_auto_recover, "IOC auto recovery, default=1, "
				"Range[off:0|on:1]");
module_param(bfa_linkup_delay, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bfa_linkup_delay, "Link up delay, default=30 secs for "
			"boot port. Otherwise 10 secs in RHEL4 & 0 for "
			"[RHEL5, SLES10, ESX40] Range[>0]");
module_param(msix_disable_cb, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(msix_disable_cb, "Disable Message Signaled Interrupts "
			"for Brocade-415/425/815/825 cards, default=0, "
			" Range[false:0|true:1]");
module_param(msix_disable_ct, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(msix_disable_ct, "Disable Message Signaled Interrupts "
			"if possible for Brocade-1010/1020/804/1007/902/1741 "
			"cards, default=0, Range[false:0|true:1]");
module_param(fdmi_enable, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(fdmi_enable, "Enables fdmi registration, default=1, "
				"Range[false:0|true:1]");
module_param(pcie_max_read_reqsz, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(pcie_max_read_reqsz, "PCIe max read request size, default=0 "
		"(use system setting), Range[128|256|512|1024|2048|4096]");
module_param(bfa_debugfs_enable, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bfa_debugfs_enable, "Enables debugfs feature, default=1,"
		" Range[false:0|true:1]");
module_param(max_xfer_size, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_xfer_size, "default=32MB,"
		" Range[64k|128k|256k|512k|1024k|2048k]");
module_param(max_rport_logins, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_rport_logins, "Max number of logins to initiator and target rports on a port (physical/logical), default=1024");

static void
bfad_sm_uninit(struct bfad_s *bfad, enum bfad_sm_event event);
static void
bfad_sm_created(struct bfad_s *bfad, enum bfad_sm_event event);
static void
bfad_sm_initializing(struct bfad_s *bfad, enum bfad_sm_event event);
static void
bfad_sm_operational(struct bfad_s *bfad, enum bfad_sm_event event);
static void
bfad_sm_stopping(struct bfad_s *bfad, enum bfad_sm_event event);
static void
bfad_sm_failed(struct bfad_s *bfad, enum bfad_sm_event event);
static void
bfad_sm_fcs_exit(struct bfad_s *bfad, enum bfad_sm_event event);

/*
 * Beginning state for the driver instance, awaiting the pci_probe event
 */
static void
bfad_sm_uninit(struct bfad_s *bfad, enum bfad_sm_event event)
{
	bfa_trc(bfad, event);

	switch (event) {
	case BFAD_E_CREATE:
		bfa_sm_set_state(bfad, bfad_sm_created);
		bfad->bfad_tsk = kthread_create(bfad_worker, (void *) bfad,
						"%s", "bfad_worker");
		if (IS_ERR(bfad->bfad_tsk)) {
			printk(KERN_INFO "bfad[%d]: Kernel thread "
				"creation failed!\n", bfad->inst_no);
			bfa_sm_send_event(bfad, BFAD_E_KTHREAD_CREATE_FAILED);
		}
		bfa_sm_send_event(bfad, BFAD_E_INIT);
		break;

	case BFAD_E_STOP:
		/* Ignore stop; already in uninit */
		break;

	default:
		bfa_sm_fault(bfad, event);
	}
}

/*
 * Driver Instance is created, awaiting event INIT to initialize the bfad
 */
static void
bfad_sm_created(struct bfad_s *bfad, enum bfad_sm_event event)
{
	unsigned long flags;
	bfa_status_t ret;

	bfa_trc(bfad, event);

	switch (event) {
	case BFAD_E_INIT:
		bfa_sm_set_state(bfad, bfad_sm_initializing);

		init_completion(&bfad->comp);

		/* Enable Interrupt and wait bfa_init completion */
		if (bfad_setup_intr(bfad)) {
			printk(KERN_WARNING "bfad%d: bfad_setup_intr failed\n",
					bfad->inst_no);
			bfa_sm_send_event(bfad, BFAD_E_INIT_FAILED);
			break;
		}

		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfa_iocfc_init(&bfad->bfa);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);

		/* Set up interrupt handler for each vectors */
		if ((bfad->bfad_flags & BFAD_MSIX_ON) &&
			bfad_install_msix_handler(bfad)) {
			printk(KERN_WARNING "%s: install_msix failed, bfad%d\n",
				__func__, bfad->inst_no);
		}

		bfad_init_timer(bfad);

		wait_for_completion(&bfad->comp);

		if ((bfad->bfad_flags & BFAD_HAL_INIT_DONE)) {
			bfa_sm_send_event(bfad, BFAD_E_INIT_SUCCESS);
		} else {
			printk(KERN_WARNING
				"bfa %s: bfa init failed\n",
				bfad->pci_name);
			spin_lock_irqsave(&bfad->bfad_lock, flags);
			bfa_fcs_init(&bfad->bfa_fcs);
			spin_unlock_irqrestore(&bfad->bfad_lock, flags);

			ret = bfad_cfg_pport(bfad, BFA_LPORT_ROLE_FCP_IM);
			if (ret != BFA_STATUS_OK) {
				init_completion(&bfad->comp);

				spin_lock_irqsave(&bfad->bfad_lock, flags);
				bfad->pport.flags |= BFAD_PORT_DELETE;
				bfa_fcs_exit(&bfad->bfa_fcs);
				spin_unlock_irqrestore(&bfad->bfad_lock, flags);

				wait_for_completion(&bfad->comp);

				bfa_sm_send_event(bfad, BFAD_E_INIT_FAILED);
				break;
			}
			bfad->bfad_flags |= BFAD_HAL_INIT_FAIL;
			bfa_sm_send_event(bfad, BFAD_E_HAL_INIT_FAILED);
		}

		break;

	case BFAD_E_KTHREAD_CREATE_FAILED:
		bfa_sm_set_state(bfad, bfad_sm_uninit);
		break;

	default:
		bfa_sm_fault(bfad, event);
	}
}

static void
bfad_sm_initializing(struct bfad_s *bfad, enum bfad_sm_event event)
{
	int	retval;
	unsigned long	flags;

	bfa_trc(bfad, event);

	switch (event) {
	case BFAD_E_INIT_SUCCESS:
		kthread_stop(bfad->bfad_tsk);
		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfad->bfad_tsk = NULL;
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);

		retval = bfad_start_ops(bfad);
		if (retval != BFA_STATUS_OK) {
			bfa_sm_set_state(bfad, bfad_sm_failed);
			break;
		}
		bfa_sm_set_state(bfad, bfad_sm_operational);
		break;

	case BFAD_E_INIT_FAILED:
		bfa_sm_set_state(bfad, bfad_sm_uninit);
		kthread_stop(bfad->bfad_tsk);
		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfad->bfad_tsk = NULL;
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		break;

	case BFAD_E_HAL_INIT_FAILED:
		bfa_sm_set_state(bfad, bfad_sm_failed);
		break;
	default:
		bfa_sm_fault(bfad, event);
	}
}

static void
bfad_sm_failed(struct bfad_s *bfad, enum bfad_sm_event event)
{
	int	retval;

	bfa_trc(bfad, event);

	switch (event) {
	case BFAD_E_INIT_SUCCESS:
		retval = bfad_start_ops(bfad);
		if (retval != BFA_STATUS_OK)
			break;
		bfa_sm_set_state(bfad, bfad_sm_operational);
		break;

	case BFAD_E_STOP:
		bfa_sm_set_state(bfad, bfad_sm_fcs_exit);
		bfa_sm_send_event(bfad, BFAD_E_FCS_EXIT_COMP);
		break;

	case BFAD_E_EXIT_COMP:
		bfa_sm_set_state(bfad, bfad_sm_uninit);
		bfad_remove_intr(bfad);
		del_timer_sync(&bfad->hal_tmo);
		break;

	default:
		bfa_sm_fault(bfad, event);
	}
}

static void
bfad_sm_operational(struct bfad_s *bfad, enum bfad_sm_event event)
{
	bfa_trc(bfad, event);

	switch (event) {
	case BFAD_E_STOP:
		bfa_sm_set_state(bfad, bfad_sm_fcs_exit);
		bfad_fcs_stop(bfad);
		break;

	default:
		bfa_sm_fault(bfad, event);
	}
}

static void
bfad_sm_fcs_exit(struct bfad_s *bfad, enum bfad_sm_event event)
{
	bfa_trc(bfad, event);

	switch (event) {
	case BFAD_E_FCS_EXIT_COMP:
		bfa_sm_set_state(bfad, bfad_sm_stopping);
		bfad_stop(bfad);
		break;

	default:
		bfa_sm_fault(bfad, event);
	}
}

static void
bfad_sm_stopping(struct bfad_s *bfad, enum bfad_sm_event event)
{
	bfa_trc(bfad, event);

	switch (event) {
	case BFAD_E_EXIT_COMP:
		bfa_sm_set_state(bfad, bfad_sm_uninit);
		bfad_remove_intr(bfad);
		del_timer_sync(&bfad->hal_tmo);
		bfad_im_probe_undo(bfad);
		bfad->bfad_flags &= ~BFAD_FC4_PROBE_DONE;
		bfad_uncfg_pport(bfad);
		break;

	default:
		bfa_sm_fault(bfad, event);
		break;
	}
}

/*
 *  BFA callbacks
 */
void
bfad_hcb_comp(void *arg, bfa_status_t status)
{
	struct bfad_hal_comp *fcomp = (struct bfad_hal_comp *)arg;

	fcomp->status = status;
	complete(&fcomp->comp);
}

/*
 * bfa_init callback
 */
void
bfa_cb_init(void *drv, bfa_status_t init_status)
{
	struct bfad_s	      *bfad = drv;

	if (init_status == BFA_STATUS_OK) {
		bfad->bfad_flags |= BFAD_HAL_INIT_DONE;

		/*
		 * If BFAD_HAL_INIT_FAIL flag is set:
		 * Wake up the kernel thread to start
		 * the bfad operations after HAL init done
		 */
		if ((bfad->bfad_flags & BFAD_HAL_INIT_FAIL)) {
			bfad->bfad_flags &= ~BFAD_HAL_INIT_FAIL;
			wake_up_process(bfad->bfad_tsk);
		}
	}

	complete(&bfad->comp);
}

/*
 *  BFA_FCS callbacks
 */
struct bfad_port_s *
bfa_fcb_lport_new(struct bfad_s *bfad, struct bfa_fcs_lport_s *port,
		 enum bfa_lport_role roles, struct bfad_vf_s *vf_drv,
		 struct bfad_vport_s *vp_drv)
{
	bfa_status_t	rc;
	struct bfad_port_s    *port_drv;

	if (!vp_drv && !vf_drv) {
		port_drv = &bfad->pport;
		port_drv->pvb_type = BFAD_PORT_PHYS_BASE;
	} else if (!vp_drv && vf_drv) {
		port_drv = &vf_drv->base_port;
		port_drv->pvb_type = BFAD_PORT_VF_BASE;
	} else if (vp_drv && !vf_drv) {
		port_drv = &vp_drv->drv_port;
		port_drv->pvb_type = BFAD_PORT_PHYS_VPORT;
	} else {
		port_drv = &vp_drv->drv_port;
		port_drv->pvb_type = BFAD_PORT_VF_VPORT;
	}

	port_drv->fcs_port = port;
	port_drv->roles = roles;

	if (roles & BFA_LPORT_ROLE_FCP_IM) {
		rc = bfad_im_port_new(bfad, port_drv);
		if (rc != BFA_STATUS_OK) {
			bfad_im_port_delete(bfad, port_drv);
			port_drv = NULL;
		}
	}

	return port_drv;
}

/*
 * FCS RPORT alloc callback, after successful PLOGI by FCS
 */
bfa_status_t
bfa_fcb_rport_alloc(struct bfad_s *bfad, struct bfa_fcs_rport_s **rport,
		    struct bfad_rport_s **rport_drv)
{
	bfa_status_t	rc = BFA_STATUS_OK;

	*rport_drv = kzalloc(sizeof(struct bfad_rport_s), GFP_ATOMIC);
	if (*rport_drv == NULL) {
		rc = BFA_STATUS_ENOMEM;
		goto ext;
	}

	*rport = &(*rport_drv)->fcs_rport;

ext:
	return rc;
}

/*
 * FCS PBC VPORT Create
 */
void
bfa_fcb_pbc_vport_create(struct bfad_s *bfad, struct bfi_pbc_vport_s pbc_vport)
{

	struct bfa_lport_cfg_s port_cfg = {0};
	struct bfad_vport_s   *vport;
	int rc;

	vport = kzalloc(sizeof(struct bfad_vport_s), GFP_KERNEL);
	if (!vport) {
		bfa_trc(bfad, 0);
		return;
	}

	vport->drv_port.bfad = bfad;
	port_cfg.roles = BFA_LPORT_ROLE_FCP_IM;
	port_cfg.pwwn = pbc_vport.vp_pwwn;
	port_cfg.nwwn = pbc_vport.vp_nwwn;
	port_cfg.preboot_vp  = BFA_TRUE;

	rc = bfa_fcs_pbc_vport_create(&vport->fcs_vport, &bfad->bfa_fcs, 0,
				  &port_cfg, vport);

	if (rc != BFA_STATUS_OK) {
		bfa_trc(bfad, 0);
		return;
	}

	list_add_tail(&vport->list_entry, &bfad->pbc_vport_list);
}

void
bfad_hal_mem_release(struct bfad_s *bfad)
{
	struct bfa_meminfo_s *hal_meminfo = &bfad->meminfo;
	struct bfa_mem_dma_s *dma_info, *dma_elem;
	struct bfa_mem_kva_s *kva_info, *kva_elem;
	struct list_head *dm_qe, *km_qe;

	dma_info = &hal_meminfo->dma_info;
	kva_info = &hal_meminfo->kva_info;

	/* Iterate through the KVA meminfo queue */
	list_for_each(km_qe, &kva_info->qe) {
		kva_elem = (struct bfa_mem_kva_s *) km_qe;
		vfree(kva_elem->kva);
	}

	/* Iterate through the DMA meminfo queue */
	list_for_each(dm_qe, &dma_info->qe) {
		dma_elem = (struct bfa_mem_dma_s *) dm_qe;
		dma_free_coherent(&bfad->pcidev->dev,
				dma_elem->mem_len, dma_elem->kva,
				(dma_addr_t) dma_elem->dma);
	}

	memset(hal_meminfo, 0, sizeof(struct bfa_meminfo_s));
}

void
bfad_update_hal_cfg(struct bfa_iocfc_cfg_s *bfa_cfg)
{
	if (num_rports > 0)
		bfa_cfg->fwcfg.num_rports = num_rports;
	if (num_ios > 0)
		bfa_cfg->fwcfg.num_ioim_reqs = num_ios;
	if (num_tms > 0)
		bfa_cfg->fwcfg.num_tskim_reqs = num_tms;
	if (num_fcxps > 0 && num_fcxps <= BFA_FCXP_MAX)
		bfa_cfg->fwcfg.num_fcxp_reqs = num_fcxps;
	if (num_ufbufs > 0 && num_ufbufs <= BFA_UF_MAX)
		bfa_cfg->fwcfg.num_uf_bufs = num_ufbufs;
	if (reqq_size > 0)
		bfa_cfg->drvcfg.num_reqq_elems = reqq_size;
	if (rspq_size > 0)
		bfa_cfg->drvcfg.num_rspq_elems = rspq_size;
	if (num_sgpgs > 0 && num_sgpgs <= BFA_SGPG_MAX)
		bfa_cfg->drvcfg.num_sgpgs = num_sgpgs;

	/*
	 * populate the hal values back to the driver for sysfs use.
	 * otherwise, the default values will be shown as 0 in sysfs
	 */
	num_rports = bfa_cfg->fwcfg.num_rports;
	num_ios = bfa_cfg->fwcfg.num_ioim_reqs;
	num_tms = bfa_cfg->fwcfg.num_tskim_reqs;
	num_fcxps = bfa_cfg->fwcfg.num_fcxp_reqs;
	num_ufbufs = bfa_cfg->fwcfg.num_uf_bufs;
	reqq_size = bfa_cfg->drvcfg.num_reqq_elems;
	rspq_size = bfa_cfg->drvcfg.num_rspq_elems;
	num_sgpgs = bfa_cfg->drvcfg.num_sgpgs;
}

bfa_status_t
bfad_hal_mem_alloc(struct bfad_s *bfad)
{
	struct bfa_meminfo_s *hal_meminfo = &bfad->meminfo;
	struct bfa_mem_dma_s *dma_info, *dma_elem;
	struct bfa_mem_kva_s *kva_info, *kva_elem;
	struct list_head *dm_qe, *km_qe;
	bfa_status_t	rc = BFA_STATUS_OK;
	dma_addr_t	phys_addr;

	bfa_cfg_get_default(&bfad->ioc_cfg);
	bfad_update_hal_cfg(&bfad->ioc_cfg);
	bfad->cfg_data.ioc_queue_depth = bfad->ioc_cfg.fwcfg.num_ioim_reqs;
	bfa_cfg_get_meminfo(&bfad->ioc_cfg, hal_meminfo, &bfad->bfa);

	dma_info = &hal_meminfo->dma_info;
	kva_info = &hal_meminfo->kva_info;

	/* Iterate through the KVA meminfo queue */
	list_for_each(km_qe, &kva_info->qe) {
		kva_elem = (struct bfa_mem_kva_s *) km_qe;
		kva_elem->kva = vmalloc(kva_elem->mem_len);
		if (kva_elem->kva == NULL) {
			bfad_hal_mem_release(bfad);
			rc = BFA_STATUS_ENOMEM;
			goto ext;
		}
		memset(kva_elem->kva, 0, kva_elem->mem_len);
	}

	/* Iterate through the DMA meminfo queue */
	list_for_each(dm_qe, &dma_info->qe) {
		dma_elem = (struct bfa_mem_dma_s *) dm_qe;
		dma_elem->kva = dma_alloc_coherent(&bfad->pcidev->dev,
						dma_elem->mem_len,
						&phys_addr, GFP_KERNEL);
		if (dma_elem->kva == NULL) {
			bfad_hal_mem_release(bfad);
			rc = BFA_STATUS_ENOMEM;
			goto ext;
		}
		dma_elem->dma = phys_addr;
		memset(dma_elem->kva, 0, dma_elem->mem_len);
	}
ext:
	return rc;
}

/*
 * Create a vport under a vf.
 */
bfa_status_t
bfad_vport_create(struct bfad_s *bfad, u16 vf_id,
		  struct bfa_lport_cfg_s *port_cfg, struct device *dev)
{
	struct bfad_vport_s   *vport;
	int		rc = BFA_STATUS_OK;
	unsigned long	flags;
	struct completion fcomp;

	vport = kzalloc(sizeof(struct bfad_vport_s), GFP_KERNEL);
	if (!vport) {
		rc = BFA_STATUS_ENOMEM;
		goto ext;
	}

	vport->drv_port.bfad = bfad;
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	rc = bfa_fcs_vport_create(&vport->fcs_vport, &bfad->bfa_fcs, vf_id,
				  port_cfg, vport);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (rc != BFA_STATUS_OK)
		goto ext_free_vport;

	if (port_cfg->roles & BFA_LPORT_ROLE_FCP_IM) {
		rc = bfad_im_scsi_host_alloc(bfad, vport->drv_port.im_port,
							dev);
		if (rc != BFA_STATUS_OK)
			goto ext_free_fcs_vport;
	}

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_fcs_vport_start(&vport->fcs_vport);
	list_add_tail(&vport->list_entry, &bfad->vport_list);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return BFA_STATUS_OK;

ext_free_fcs_vport:
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	vport->comp_del = &fcomp;
	init_completion(vport->comp_del);
	bfa_fcs_vport_delete(&vport->fcs_vport);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	wait_for_completion(vport->comp_del);
ext_free_vport:
	kfree(vport);
ext:
	return rc;
}

void
bfad_bfa_tmo(unsigned long data)
{
	struct bfad_s	      *bfad = (struct bfad_s *) data;
	unsigned long	flags;
	struct list_head	       doneq;

	spin_lock_irqsave(&bfad->bfad_lock, flags);

	bfa_timer_beat(&bfad->bfa.timer_mod);

	bfa_comp_deq(&bfad->bfa, &doneq);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (!list_empty(&doneq)) {
		bfa_comp_process(&bfad->bfa, &doneq);
		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfa_comp_free(&bfad->bfa, &doneq);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	}

	mod_timer(&bfad->hal_tmo,
		  jiffies + msecs_to_jiffies(BFA_TIMER_FREQ));
}

void
bfad_init_timer(struct bfad_s *bfad)
{
	init_timer(&bfad->hal_tmo);
	bfad->hal_tmo.function = bfad_bfa_tmo;
	bfad->hal_tmo.data = (unsigned long)bfad;

	mod_timer(&bfad->hal_tmo,
		  jiffies + msecs_to_jiffies(BFA_TIMER_FREQ));
}

int
bfad_pci_init(struct pci_dev *pdev, struct bfad_s *bfad)
{
	int		rc = -ENODEV;

	if (pci_enable_device(pdev)) {
		printk(KERN_ERR "pci_enable_device fail %p\n", pdev);
		goto out;
	}

	if (pci_request_regions(pdev, BFAD_DRIVER_NAME))
		goto out_disable_device;

	pci_set_master(pdev);


	if ((pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) != 0) ||
	    (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64)) != 0)) {
		if ((pci_set_dma_mask(pdev, DMA_BIT_MASK(32)) != 0) ||
		   (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)) != 0)) {
			printk(KERN_ERR "pci_set_dma_mask fail %p\n", pdev);
			goto out_release_region;
		}
	}

	/* Enable PCIE Advanced Error Recovery (AER) if kernel supports */
	pci_enable_pcie_error_reporting(pdev);

	bfad->pci_bar0_kva = pci_iomap(pdev, 0, pci_resource_len(pdev, 0));
	bfad->pci_bar2_kva = pci_iomap(pdev, 2, pci_resource_len(pdev, 2));

	if (bfad->pci_bar0_kva == NULL) {
		printk(KERN_ERR "Fail to map bar0\n");
		goto out_release_region;
	}

	bfad->hal_pcidev.pci_slot = PCI_SLOT(pdev->devfn);
	bfad->hal_pcidev.pci_func = PCI_FUNC(pdev->devfn);
	bfad->hal_pcidev.pci_bar_kva = bfad->pci_bar0_kva;
	bfad->hal_pcidev.device_id = pdev->device;
	bfad->hal_pcidev.ssid = pdev->subsystem_device;
	bfad->pci_name = pci_name(pdev);

	bfad->pci_attr.vendor_id = pdev->vendor;
	bfad->pci_attr.device_id = pdev->device;
	bfad->pci_attr.ssid = pdev->subsystem_device;
	bfad->pci_attr.ssvid = pdev->subsystem_vendor;
	bfad->pci_attr.pcifn = PCI_FUNC(pdev->devfn);

	bfad->pcidev = pdev;

	/* Adjust PCIe Maximum Read Request Size */
	if (pci_is_pcie(pdev) && pcie_max_read_reqsz) {
		if (pcie_max_read_reqsz >= 128 &&
		    pcie_max_read_reqsz <= 4096 &&
		    is_power_of_2(pcie_max_read_reqsz)) {
			int max_rq = pcie_get_readrq(pdev);
			printk(KERN_WARNING "BFA[%s]: "
				"pcie_max_read_request_size is %d, "
				"reset to %d\n", bfad->pci_name, max_rq,
				pcie_max_read_reqsz);
			pcie_set_readrq(pdev, pcie_max_read_reqsz);
		} else {
			printk(KERN_WARNING "BFA[%s]: invalid "
			       "pcie_max_read_request_size %d ignored\n",
			       bfad->pci_name, pcie_max_read_reqsz);
		}
	}

	pci_save_state(pdev);

	return 0;

out_release_region:
	pci_release_regions(pdev);
out_disable_device:
	pci_disable_device(pdev);
out:
	return rc;
}

void
bfad_pci_uninit(struct pci_dev *pdev, struct bfad_s *bfad)
{
	pci_iounmap(pdev, bfad->pci_bar0_kva);
	pci_iounmap(pdev, bfad->pci_bar2_kva);
	pci_release_regions(pdev);
	/* Disable PCIE Advanced Error Recovery (AER) */
	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);
}

bfa_status_t
bfad_drv_init(struct bfad_s *bfad)
{
	bfa_status_t	rc;
	unsigned long	flags;

	bfad->cfg_data.rport_del_timeout = rport_del_timeout;
	bfad->cfg_data.lun_queue_depth = bfa_lun_queue_depth;
	bfad->cfg_data.io_max_sge = bfa_io_max_sge;
	bfad->cfg_data.binding_method = FCP_PWWN_BINDING;

	rc = bfad_hal_mem_alloc(bfad);
	if (rc != BFA_STATUS_OK) {
		printk(KERN_WARNING "bfad%d bfad_hal_mem_alloc failure\n",
		       bfad->inst_no);
		printk(KERN_WARNING
			"Not enough memory to attach all Brocade HBA ports, %s",
			"System may need more memory.\n");
		return BFA_STATUS_FAILED;
	}

	bfad->bfa.trcmod = bfad->trcmod;
	bfad->bfa.plog = &bfad->plog_buf;
	bfa_plog_init(&bfad->plog_buf);
	bfa_plog_str(&bfad->plog_buf, BFA_PL_MID_DRVR, BFA_PL_EID_DRIVER_START,
		     0, "Driver Attach");

	bfa_attach(&bfad->bfa, bfad, &bfad->ioc_cfg, &bfad->meminfo,
		   &bfad->hal_pcidev);

	/* FCS INIT */
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfad->bfa_fcs.trcmod = bfad->trcmod;
	bfa_fcs_attach(&bfad->bfa_fcs, &bfad->bfa, bfad, BFA_FALSE);
	bfad->bfa_fcs.fdmi_enabled = fdmi_enable;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	bfad->bfad_flags |= BFAD_DRV_INIT_DONE;

	return BFA_STATUS_OK;
}

void
bfad_drv_uninit(struct bfad_s *bfad)
{
	unsigned long   flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	init_completion(&bfad->comp);
	bfa_iocfc_stop(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	wait_for_completion(&bfad->comp);

	del_timer_sync(&bfad->hal_tmo);
	bfa_isr_disable(&bfad->bfa);
	bfa_detach(&bfad->bfa);
	bfad_remove_intr(bfad);
	bfad_hal_mem_release(bfad);

	bfad->bfad_flags &= ~BFAD_DRV_INIT_DONE;
}

void
bfad_drv_start(struct bfad_s *bfad)
{
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_iocfc_start(&bfad->bfa);
	bfa_fcs_pbc_vport_init(&bfad->bfa_fcs);
	bfa_fcs_fabric_modstart(&bfad->bfa_fcs);
	bfad->bfad_flags |= BFAD_HAL_START_DONE;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (bfad->im)
		flush_workqueue(bfad->im->drv_workq);
}

void
bfad_fcs_stop(struct bfad_s *bfad)
{
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	init_completion(&bfad->comp);
	bfad->pport.flags |= BFAD_PORT_DELETE;
	bfa_fcs_exit(&bfad->bfa_fcs);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	wait_for_completion(&bfad->comp);

	bfa_sm_send_event(bfad, BFAD_E_FCS_EXIT_COMP);
}

void
bfad_stop(struct bfad_s *bfad)
{
	unsigned long	flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	init_completion(&bfad->comp);
	bfa_iocfc_stop(&bfad->bfa);
	bfad->bfad_flags &= ~BFAD_HAL_START_DONE;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	wait_for_completion(&bfad->comp);

	bfa_sm_send_event(bfad, BFAD_E_EXIT_COMP);
}

bfa_status_t
bfad_cfg_pport(struct bfad_s *bfad, enum bfa_lport_role role)
{
	int		rc = BFA_STATUS_OK;

	/* Allocate scsi_host for the physical port */
	if ((supported_fc4s & BFA_LPORT_ROLE_FCP_IM) &&
	    (role & BFA_LPORT_ROLE_FCP_IM)) {
		if (bfad->pport.im_port == NULL) {
			rc = BFA_STATUS_FAILED;
			goto out;
		}

		rc = bfad_im_scsi_host_alloc(bfad, bfad->pport.im_port,
						&bfad->pcidev->dev);
		if (rc != BFA_STATUS_OK)
			goto out;

		bfad->pport.roles |= BFA_LPORT_ROLE_FCP_IM;
	}

	bfad->bfad_flags |= BFAD_CFG_PPORT_DONE;

out:
	return rc;
}

void
bfad_uncfg_pport(struct bfad_s *bfad)
{
	if ((supported_fc4s & BFA_LPORT_ROLE_FCP_IM) &&
	    (bfad->pport.roles & BFA_LPORT_ROLE_FCP_IM)) {
		bfad_im_scsi_host_free(bfad, bfad->pport.im_port);
		bfad_im_port_clean(bfad->pport.im_port);
		kfree(bfad->pport.im_port);
		bfad->pport.roles &= ~BFA_LPORT_ROLE_FCP_IM;
	}

	bfad->bfad_flags &= ~BFAD_CFG_PPORT_DONE;
}

bfa_status_t
bfad_start_ops(struct bfad_s *bfad) {

	int	retval;
	unsigned long	flags;
	struct bfad_vport_s *vport, *vport_new;
	struct bfa_fcs_driver_info_s driver_info;

	/* Limit min/max. xfer size to [64k-32MB] */
	if (max_xfer_size < BFAD_MIN_SECTORS >> 1)
		max_xfer_size = BFAD_MIN_SECTORS >> 1;
	if (max_xfer_size > BFAD_MAX_SECTORS >> 1)
		max_xfer_size = BFAD_MAX_SECTORS >> 1;

	/* Fill the driver_info info to fcs*/
	memset(&driver_info, 0, sizeof(driver_info));
	strncpy(driver_info.version, BFAD_DRIVER_VERSION,
		sizeof(driver_info.version) - 1);
	if (host_name)
		strncpy(driver_info.host_machine_name, host_name,
			sizeof(driver_info.host_machine_name) - 1);
	if (os_name)
		strncpy(driver_info.host_os_name, os_name,
			sizeof(driver_info.host_os_name) - 1);
	if (os_patch)
		strncpy(driver_info.host_os_patch, os_patch,
			sizeof(driver_info.host_os_patch) - 1);

	strncpy(driver_info.os_device_name, bfad->pci_name,
		sizeof(driver_info.os_device_name) - 1);

	/* FCS driver info init */
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_fcs_driver_info_init(&bfad->bfa_fcs, &driver_info);

	if (bfad->bfad_flags & BFAD_CFG_PPORT_DONE)
		bfa_fcs_update_cfg(&bfad->bfa_fcs);
	else
		bfa_fcs_init(&bfad->bfa_fcs);

	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (!(bfad->bfad_flags & BFAD_CFG_PPORT_DONE)) {
		retval = bfad_cfg_pport(bfad, BFA_LPORT_ROLE_FCP_IM);
		if (retval != BFA_STATUS_OK)
			return BFA_STATUS_FAILED;
	}

	/* Setup fc host fixed attribute if the lk supports */
	bfad_fc_host_init(bfad->pport.im_port);

	/* BFAD level FC4 IM specific resource allocation */
	retval = bfad_im_probe(bfad);
	if (retval != BFA_STATUS_OK) {
		printk(KERN_WARNING "bfad_im_probe failed\n");
		if (bfa_sm_cmp_state(bfad, bfad_sm_initializing))
			bfa_sm_set_state(bfad, bfad_sm_failed);
		return BFA_STATUS_FAILED;
	} else
		bfad->bfad_flags |= BFAD_FC4_PROBE_DONE;

	bfad_drv_start(bfad);

	/* Complete pbc vport create */
	list_for_each_entry_safe(vport, vport_new, &bfad->pbc_vport_list,
				list_entry) {
		struct fc_vport_identifiers vid;
		struct fc_vport *fc_vport;
		char pwwn_buf[BFA_STRING_32];

		memset(&vid, 0, sizeof(vid));
		vid.roles = FC_PORT_ROLE_FCP_INITIATOR;
		vid.vport_type = FC_PORTTYPE_NPIV;
		vid.disable = false;
		vid.node_name = wwn_to_u64((u8 *)
				(&((vport->fcs_vport).lport.port_cfg.nwwn)));
		vid.port_name = wwn_to_u64((u8 *)
				(&((vport->fcs_vport).lport.port_cfg.pwwn)));
		fc_vport = fc_vport_create(bfad->pport.im_port->shost, 0, &vid);
		if (!fc_vport) {
			wwn2str(pwwn_buf, vid.port_name);
			printk(KERN_WARNING "bfad%d: failed to create pbc vport"
				" %s\n", bfad->inst_no, pwwn_buf);
		}
		list_del(&vport->list_entry);
		kfree(vport);
	}

	/*
	 * If bfa_linkup_delay is set to -1 default; try to retrive the
	 * value using the bfad_get_linkup_delay(); else use the
	 * passed in module param value as the bfa_linkup_delay.
	 */
	if (bfa_linkup_delay < 0) {
		bfa_linkup_delay = bfad_get_linkup_delay(bfad);
		bfad_rport_online_wait(bfad);
		bfa_linkup_delay = -1;
	} else
		bfad_rport_online_wait(bfad);

	BFA_LOG(KERN_INFO, bfad, bfa_log_level, "bfa device claimed\n");

	return BFA_STATUS_OK;
}

int
bfad_worker(void *ptr)
{
	struct bfad_s *bfad;
	unsigned long   flags;

	bfad = (struct bfad_s *)ptr;

	while (!kthread_should_stop()) {

		/* Send event BFAD_E_INIT_SUCCESS */
		bfa_sm_send_event(bfad, BFAD_E_INIT_SUCCESS);

		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfad->bfad_tsk = NULL;
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);

		break;
	}

	return 0;
}

/*
 *  BFA driver interrupt functions
 */
irqreturn_t
bfad_intx(int irq, void *dev_id)
{
	struct bfad_s	*bfad = dev_id;
	struct list_head	doneq;
	unsigned long	flags;
	bfa_boolean_t rc;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	rc = bfa_intx(&bfad->bfa);
	if (!rc) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		return IRQ_NONE;
	}

	bfa_comp_deq(&bfad->bfa, &doneq);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (!list_empty(&doneq)) {
		bfa_comp_process(&bfad->bfa, &doneq);

		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfa_comp_free(&bfad->bfa, &doneq);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	}

	return IRQ_HANDLED;

}

static irqreturn_t
bfad_msix(int irq, void *dev_id)
{
	struct bfad_msix_s *vec = dev_id;
	struct bfad_s *bfad = vec->bfad;
	struct list_head doneq;
	unsigned long   flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);

	bfa_msix(&bfad->bfa, vec->msix.entry);
	bfa_comp_deq(&bfad->bfa, &doneq);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (!list_empty(&doneq)) {
		bfa_comp_process(&bfad->bfa, &doneq);

		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfa_comp_free(&bfad->bfa, &doneq);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	}

	return IRQ_HANDLED;
}

/*
 * Initialize the MSIX entry table.
 */
static void
bfad_init_msix_entry(struct bfad_s *bfad, struct msix_entry *msix_entries,
			 int mask, int max_bit)
{
	int	i;
	int	match = 0x00000001;

	for (i = 0, bfad->nvec = 0; i < MAX_MSIX_ENTRY; i++) {
		if (mask & match) {
			bfad->msix_tab[bfad->nvec].msix.entry = i;
			bfad->msix_tab[bfad->nvec].bfad = bfad;
			msix_entries[bfad->nvec].entry = i;
			bfad->nvec++;
		}

		match <<= 1;
	}

}

int
bfad_install_msix_handler(struct bfad_s *bfad)
{
	int i, error = 0;

	for (i = 0; i < bfad->nvec; i++) {
		sprintf(bfad->msix_tab[i].name, "bfa-%s-%s",
				bfad->pci_name,
				((bfa_asic_id_cb(bfad->hal_pcidev.device_id)) ?
				msix_name_cb[i] : msix_name_ct[i]));

		error = request_irq(bfad->msix_tab[i].msix.vector,
				    (irq_handler_t) bfad_msix, 0,
				    bfad->msix_tab[i].name, &bfad->msix_tab[i]);
		bfa_trc(bfad, i);
		bfa_trc(bfad, bfad->msix_tab[i].msix.vector);
		if (error) {
			int	j;

			for (j = 0; j < i; j++)
				free_irq(bfad->msix_tab[j].msix.vector,
						&bfad->msix_tab[j]);

			bfad->bfad_flags &= ~BFAD_MSIX_ON;
			pci_disable_msix(bfad->pcidev);

			return 1;
		}
	}

	return 0;
}

/*
 * Setup MSIX based interrupt.
 */
int
bfad_setup_intr(struct bfad_s *bfad)
{
	int error = 0;
	u32 mask = 0, i, num_bit = 0, max_bit = 0;
	struct msix_entry msix_entries[MAX_MSIX_ENTRY];
	struct pci_dev *pdev = bfad->pcidev;
	u16	reg;

	/* Call BFA to get the msix map for this PCI function.  */
	bfa_msix_getvecs(&bfad->bfa, &mask, &num_bit, &max_bit);

	/* Set up the msix entry table */
	bfad_init_msix_entry(bfad, msix_entries, mask, max_bit);

	if ((bfa_asic_id_ctc(pdev->device) && !msix_disable_ct) ||
	   (bfa_asic_id_cb(pdev->device) && !msix_disable_cb)) {

		error = pci_enable_msix(bfad->pcidev, msix_entries, bfad->nvec);
		if (error) {
			/* In CT1 & CT2, try to allocate just one vector */
			if (bfa_asic_id_ctc(pdev->device)) {
				printk(KERN_WARNING "bfa %s: trying one msix "
				       "vector failed to allocate %d[%d]\n",
				       bfad->pci_name, bfad->nvec, error);
				bfad->nvec = 1;
				error = pci_enable_msix(bfad->pcidev,
						msix_entries, bfad->nvec);
			}

			/*
			 * Only error number of vector is available.
			 * We don't have a mechanism to map multiple
			 * interrupts into one vector, so even if we
			 * can try to request less vectors, we don't
			 * know how to associate interrupt events to
			 *  vectors. Linux doesn't duplicate vectors
			 * in the MSIX table for this case.
			 */
			if (error) {
				printk(KERN_WARNING "bfad%d: "
				       "pci_enable_msix failed (%d), "
				       "use line based.\n",
					bfad->inst_no, error);
				goto line_based;
			}
		}

		/* Disable INTX in MSI-X mode */
		pci_read_config_word(pdev, PCI_COMMAND, &reg);

		if (!(reg & PCI_COMMAND_INTX_DISABLE))
			pci_write_config_word(pdev, PCI_COMMAND,
				reg | PCI_COMMAND_INTX_DISABLE);

		/* Save the vectors */
		for (i = 0; i < bfad->nvec; i++) {
			bfa_trc(bfad, msix_entries[i].vector);
			bfad->msix_tab[i].msix.vector = msix_entries[i].vector;
		}

		bfa_msix_init(&bfad->bfa, bfad->nvec);

		bfad->bfad_flags |= BFAD_MSIX_ON;

		return error;
	}

line_based:
	error = 0;
	if (request_irq
	    (bfad->pcidev->irq, (irq_handler_t) bfad_intx, BFAD_IRQ_FLAGS,
	     BFAD_DRIVER_NAME, bfad) != 0) {
		/* Enable interrupt handler failed */
		return 1;
	}
	bfad->bfad_flags |= BFAD_INTX_ON;

	return error;
}

void
bfad_remove_intr(struct bfad_s *bfad)
{
	int	i;

	if (bfad->bfad_flags & BFAD_MSIX_ON) {
		for (i = 0; i < bfad->nvec; i++)
			free_irq(bfad->msix_tab[i].msix.vector,
					&bfad->msix_tab[i]);

		pci_disable_msix(bfad->pcidev);
		bfad->bfad_flags &= ~BFAD_MSIX_ON;
	} else if (bfad->bfad_flags & BFAD_INTX_ON) {
		free_irq(bfad->pcidev->irq, bfad);
	}
}

/*
 * PCI probe entry.
 */
int
bfad_pci_probe(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	struct bfad_s	*bfad;
	int		error = -ENODEV, retval, i;

	/* For single port cards - only claim function 0 */
	if ((pdev->device == BFA_PCI_DEVICE_ID_FC_8G1P) &&
		(PCI_FUNC(pdev->devfn) != 0))
		return -ENODEV;

	bfad = kzalloc(sizeof(struct bfad_s), GFP_KERNEL);
	if (!bfad) {
		error = -ENOMEM;
		goto out;
	}

	bfad->trcmod = kzalloc(sizeof(struct bfa_trc_mod_s), GFP_KERNEL);
	if (!bfad->trcmod) {
		printk(KERN_WARNING "Error alloc trace buffer!\n");
		error = -ENOMEM;
		goto out_alloc_trace_failure;
	}

	/* TRACE INIT */
	bfa_trc_init(bfad->trcmod);
	bfa_trc(bfad, bfad_inst);

	/* AEN INIT */
	INIT_LIST_HEAD(&bfad->free_aen_q);
	INIT_LIST_HEAD(&bfad->active_aen_q);
	for (i = 0; i < BFA_AEN_MAX_ENTRY; i++)
		list_add_tail(&bfad->aen_list[i].qe, &bfad->free_aen_q);

	if (!(bfad_load_fwimg(pdev))) {
		kfree(bfad->trcmod);
		goto out_alloc_trace_failure;
	}

	retval = bfad_pci_init(pdev, bfad);
	if (retval) {
		printk(KERN_WARNING "bfad_pci_init failure!\n");
		error = retval;
		goto out_pci_init_failure;
	}

	mutex_lock(&bfad_mutex);
	bfad->inst_no = bfad_inst++;
	list_add_tail(&bfad->list_entry, &bfad_list);
	mutex_unlock(&bfad_mutex);

	/* Initializing the state machine: State set to uninit */
	bfa_sm_set_state(bfad, bfad_sm_uninit);

	spin_lock_init(&bfad->bfad_lock);
	spin_lock_init(&bfad->bfad_aen_spinlock);

	pci_set_drvdata(pdev, bfad);

	bfad->ref_count = 0;
	bfad->pport.bfad = bfad;
	INIT_LIST_HEAD(&bfad->pbc_vport_list);
	INIT_LIST_HEAD(&bfad->vport_list);

	/* Setup the debugfs node for this bfad */
	if (bfa_debugfs_enable)
		bfad_debugfs_init(&bfad->pport);

	retval = bfad_drv_init(bfad);
	if (retval != BFA_STATUS_OK)
		goto out_drv_init_failure;

	bfa_sm_send_event(bfad, BFAD_E_CREATE);

	if (bfa_sm_cmp_state(bfad, bfad_sm_uninit))
		goto out_bfad_sm_failure;

	return 0;

out_bfad_sm_failure:
	bfad_hal_mem_release(bfad);
out_drv_init_failure:
	/* Remove the debugfs node for this bfad */
	kfree(bfad->regdata);
	bfad_debugfs_exit(&bfad->pport);
	mutex_lock(&bfad_mutex);
	bfad_inst--;
	list_del(&bfad->list_entry);
	mutex_unlock(&bfad_mutex);
	bfad_pci_uninit(pdev, bfad);
out_pci_init_failure:
	kfree(bfad->trcmod);
out_alloc_trace_failure:
	kfree(bfad);
out:
	return error;
}

/*
 * PCI remove entry.
 */
void
bfad_pci_remove(struct pci_dev *pdev)
{
	struct bfad_s	      *bfad = pci_get_drvdata(pdev);
	unsigned long	flags;

	bfa_trc(bfad, bfad->inst_no);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (bfad->bfad_tsk != NULL) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		kthread_stop(bfad->bfad_tsk);
	} else {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	}

	/* Send Event BFAD_E_STOP */
	bfa_sm_send_event(bfad, BFAD_E_STOP);

	/* Driver detach and dealloc mem */
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_detach(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	bfad_hal_mem_release(bfad);

	/* Remove the debugfs node for this bfad */
	kfree(bfad->regdata);
	bfad_debugfs_exit(&bfad->pport);

	/* Cleaning the BFAD instance */
	mutex_lock(&bfad_mutex);
	bfad_inst--;
	list_del(&bfad->list_entry);
	mutex_unlock(&bfad_mutex);
	bfad_pci_uninit(pdev, bfad);

	kfree(bfad->trcmod);
	kfree(bfad);
}

/*
 * PCI Error Recovery entry, error detected.
 */
static pci_ers_result_t
bfad_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct bfad_s *bfad = pci_get_drvdata(pdev);
	unsigned long	flags;
	pci_ers_result_t ret = PCI_ERS_RESULT_NONE;

	dev_printk(KERN_ERR, &pdev->dev,
		   "error detected state: %d - flags: 0x%x\n",
		   state, bfad->bfad_flags);

	switch (state) {
	case pci_channel_io_normal: /* non-fatal error */
		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfad->bfad_flags &= ~BFAD_EEH_BUSY;
		/* Suspend/fail all bfa operations */
		bfa_ioc_suspend(&bfad->bfa.ioc);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		del_timer_sync(&bfad->hal_tmo);
		ret = PCI_ERS_RESULT_CAN_RECOVER;
		break;
	case pci_channel_io_frozen: /* fatal error */
		init_completion(&bfad->comp);
		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfad->bfad_flags |= BFAD_EEH_BUSY;
		/* Suspend/fail all bfa operations */
		bfa_ioc_suspend(&bfad->bfa.ioc);
		bfa_fcs_stop(&bfad->bfa_fcs);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		wait_for_completion(&bfad->comp);

		bfad_remove_intr(bfad);
		del_timer_sync(&bfad->hal_tmo);
		pci_disable_device(pdev);
		ret = PCI_ERS_RESULT_NEED_RESET;
		break;
	case pci_channel_io_perm_failure: /* PCI Card is DEAD */
		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfad->bfad_flags |= BFAD_EEH_BUSY |
				    BFAD_EEH_PCI_CHANNEL_IO_PERM_FAILURE;
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);

		/* If the error_detected handler is called with the reason
		 * pci_channel_io_perm_failure - it will subsequently call
		 * pci_remove() entry point to remove the pci device from the
		 * system - So defer the cleanup to pci_remove(); cleaning up
		 * here causes inconsistent state during pci_remove().
		 */
		ret = PCI_ERS_RESULT_DISCONNECT;
		break;
	default:
		WARN_ON(1);
	}

	return ret;
}

int
restart_bfa(struct bfad_s *bfad)
{
	unsigned long flags;
	struct pci_dev *pdev = bfad->pcidev;

	bfa_attach(&bfad->bfa, bfad, &bfad->ioc_cfg,
		   &bfad->meminfo, &bfad->hal_pcidev);

	/* Enable Interrupt and wait bfa_init completion */
	if (bfad_setup_intr(bfad)) {
		dev_printk(KERN_WARNING, &pdev->dev,
			   "%s: bfad_setup_intr failed\n", bfad->pci_name);
		bfa_sm_send_event(bfad, BFAD_E_INIT_FAILED);
		return -1;
	}

	init_completion(&bfad->comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_iocfc_init(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	/* Set up interrupt handler for each vectors */
	if ((bfad->bfad_flags & BFAD_MSIX_ON) &&
	    bfad_install_msix_handler(bfad))
		dev_printk(KERN_WARNING, &pdev->dev,
			   "%s: install_msix failed.\n", bfad->pci_name);

	bfad_init_timer(bfad);
	wait_for_completion(&bfad->comp);
	bfad_drv_start(bfad);

	return 0;
}

/*
 * PCI Error Recovery entry, re-initialize the chip.
 */
static pci_ers_result_t
bfad_pci_slot_reset(struct pci_dev *pdev)
{
	struct bfad_s *bfad = pci_get_drvdata(pdev);
	u8 byte;

	dev_printk(KERN_ERR, &pdev->dev,
		   "bfad_pci_slot_reset flags: 0x%x\n", bfad->bfad_flags);

	if (pci_enable_device(pdev)) {
		dev_printk(KERN_ERR, &pdev->dev, "Cannot re-enable "
			   "PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_restore_state(pdev);

	/*
	 * Read some byte (e.g. DMA max. payload size which can't
	 * be 0xff any time) to make sure - we did not hit another PCI error
	 * in the middle of recovery. If we did, then declare permanent failure.
	 */
	pci_read_config_byte(pdev, 0x68, &byte);
	if (byte == 0xff) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "slot_reset failed ... got another PCI error !\n");
		goto out_disable_device;
	}

	pci_save_state(pdev);
	pci_set_master(pdev);

	if (pci_set_dma_mask(bfad->pcidev, DMA_BIT_MASK(64)) != 0)
		if (pci_set_dma_mask(bfad->pcidev, DMA_BIT_MASK(32)) != 0)
			goto out_disable_device;

	pci_cleanup_aer_uncorrect_error_status(pdev);

	if (restart_bfa(bfad) == -1)
		goto out_disable_device;

	pci_enable_pcie_error_reporting(pdev);
	dev_printk(KERN_WARNING, &pdev->dev,
		   "slot_reset completed  flags: 0x%x!\n", bfad->bfad_flags);

	return PCI_ERS_RESULT_RECOVERED;

out_disable_device:
	pci_disable_device(pdev);
	return PCI_ERS_RESULT_DISCONNECT;
}

static pci_ers_result_t
bfad_pci_mmio_enabled(struct pci_dev *pdev)
{
	unsigned long	flags;
	struct bfad_s *bfad = pci_get_drvdata(pdev);

	dev_printk(KERN_INFO, &pdev->dev, "mmio_enabled\n");

	/* Fetch FW diagnostic information */
	bfa_ioc_debug_save_ftrc(&bfad->bfa.ioc);

	/* Cancel all pending IOs */
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	init_completion(&bfad->comp);
	bfa_fcs_stop(&bfad->bfa_fcs);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	wait_for_completion(&bfad->comp);

	bfad_remove_intr(bfad);
	del_timer_sync(&bfad->hal_tmo);
	pci_disable_device(pdev);

	return PCI_ERS_RESULT_NEED_RESET;
}

static void
bfad_pci_resume(struct pci_dev *pdev)
{
	unsigned long	flags;
	struct bfad_s *bfad = pci_get_drvdata(pdev);

	dev_printk(KERN_WARNING, &pdev->dev, "resume\n");

	/* wait until the link is online */
	bfad_rport_online_wait(bfad);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfad->bfad_flags &= ~BFAD_EEH_BUSY;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

struct pci_device_id bfad_id_table[] = {
	{
		.vendor = BFA_PCI_VENDOR_ID_BROCADE,
		.device = BFA_PCI_DEVICE_ID_FC_8G2P,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
	},
	{
		.vendor = BFA_PCI_VENDOR_ID_BROCADE,
		.device = BFA_PCI_DEVICE_ID_FC_8G1P,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
	},
	{
		.vendor = BFA_PCI_VENDOR_ID_BROCADE,
		.device = BFA_PCI_DEVICE_ID_CT,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
		.class = (PCI_CLASS_SERIAL_FIBER << 8),
		.class_mask = ~0,
	},
	{
		.vendor = BFA_PCI_VENDOR_ID_BROCADE,
		.device = BFA_PCI_DEVICE_ID_CT_FC,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
		.class = (PCI_CLASS_SERIAL_FIBER << 8),
		.class_mask = ~0,
	},
	{
		.vendor = BFA_PCI_VENDOR_ID_BROCADE,
		.device = BFA_PCI_DEVICE_ID_CT2,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
		.class = (PCI_CLASS_SERIAL_FIBER << 8),
		.class_mask = ~0,
	},

	{
		.vendor = BFA_PCI_VENDOR_ID_BROCADE,
		.device = BFA_PCI_DEVICE_ID_CT2_QUAD,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
		.class = (PCI_CLASS_SERIAL_FIBER << 8),
		.class_mask = ~0,
	},
	{0, 0},
};

MODULE_DEVICE_TABLE(pci, bfad_id_table);

/*
 * PCI error recovery handlers.
 */
static struct pci_error_handlers bfad_err_handler = {
	.error_detected = bfad_pci_error_detected,
	.slot_reset = bfad_pci_slot_reset,
	.mmio_enabled = bfad_pci_mmio_enabled,
	.resume = bfad_pci_resume,
};

static struct pci_driver bfad_pci_driver = {
	.name = BFAD_DRIVER_NAME,
	.id_table = bfad_id_table,
	.probe = bfad_pci_probe,
	.remove = bfad_pci_remove,
	.err_handler = &bfad_err_handler,
};

/*
 * Driver module init.
 */
static int __init
bfad_init(void)
{
	int		error = 0;

	printk(KERN_INFO "Brocade BFA FC/FCOE SCSI driver - version: %s\n",
			BFAD_DRIVER_VERSION);

	if (num_sgpgs > 0)
		num_sgpgs_parm = num_sgpgs;

	error = bfad_im_module_init();
	if (error) {
		error = -ENOMEM;
		printk(KERN_WARNING "bfad_im_module_init failure\n");
		goto ext;
	}

	if (strcmp(FCPI_NAME, " fcpim") == 0)
		supported_fc4s |= BFA_LPORT_ROLE_FCP_IM;

	bfa_auto_recover = ioc_auto_recover;
	bfa_fcs_rport_set_del_timeout(rport_del_timeout);
	bfa_fcs_rport_set_max_logins(max_rport_logins);

	error = pci_register_driver(&bfad_pci_driver);
	if (error) {
		printk(KERN_WARNING "pci_register_driver failure\n");
		goto ext;
	}

	return 0;

ext:
	bfad_im_module_exit();
	return error;
}

/*
 * Driver module exit.
 */
static void __exit
bfad_exit(void)
{
	pci_unregister_driver(&bfad_pci_driver);
	bfad_im_module_exit();
	bfad_free_fwimg();
}

/* Firmware handling */
static void
bfad_read_firmware(struct pci_dev *pdev, u32 **bfi_image,
		u32 *bfi_image_size, char *fw_name)
{
	const struct firmware *fw;

	if (request_firmware(&fw, fw_name, &pdev->dev)) {
		printk(KERN_ALERT "Can't locate firmware %s\n", fw_name);
		*bfi_image = NULL;
		goto out;
	}

	*bfi_image = vmalloc(fw->size);
	if (NULL == *bfi_image) {
		printk(KERN_ALERT "Fail to allocate buffer for fw image "
			"size=%x!\n", (u32) fw->size);
		goto out;
	}

	memcpy(*bfi_image, fw->data, fw->size);
	*bfi_image_size = fw->size/sizeof(u32);
out:
	release_firmware(fw);
}

static u32 *
bfad_load_fwimg(struct pci_dev *pdev)
{
	if (pdev->device == BFA_PCI_DEVICE_ID_CT2) {
		if (bfi_image_ct2_size == 0)
			bfad_read_firmware(pdev, &bfi_image_ct2,
				&bfi_image_ct2_size, BFAD_FW_FILE_CT2);
		return bfi_image_ct2;
	} else if (bfa_asic_id_ct(pdev->device)) {
		if (bfi_image_ct_size == 0)
			bfad_read_firmware(pdev, &bfi_image_ct,
				&bfi_image_ct_size, BFAD_FW_FILE_CT);
		return bfi_image_ct;
	} else {
		if (bfi_image_cb_size == 0)
			bfad_read_firmware(pdev, &bfi_image_cb,
				&bfi_image_cb_size, BFAD_FW_FILE_CB);
		return bfi_image_cb;
	}
}

static void
bfad_free_fwimg(void)
{
	if (bfi_image_ct2_size && bfi_image_ct2)
		vfree(bfi_image_ct2);
	if (bfi_image_ct_size && bfi_image_ct)
		vfree(bfi_image_ct);
	if (bfi_image_cb_size && bfi_image_cb)
		vfree(bfi_image_cb);
}

module_init(bfad_init);
module_exit(bfad_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Brocade Fibre Channel HBA Driver" BFAD_PROTO_NAME);
MODULE_AUTHOR("Brocade Communications Systems, Inc.");
MODULE_VERSION(BFAD_DRIVER_VERSION);
