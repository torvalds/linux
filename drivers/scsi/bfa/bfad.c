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
 *  bfad.c Linux driver PCI interface module.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include "bfad_drv.h"
#include "bfad_im.h"
#include "bfad_tm.h"
#include "bfad_ipfc.h"
#include "bfad_trcmod.h"
#include <fcb/bfa_fcb_vf.h>
#include <fcb/bfa_fcb_rport.h>
#include <fcb/bfa_fcb_port.h>
#include <fcb/bfa_fcb.h>

BFA_TRC_FILE(LDRV, BFAD);
DEFINE_MUTEX(bfad_mutex);
LIST_HEAD(bfad_list);
static int      bfad_inst;
int bfad_supported_fc4s;

static char     *host_name;
static char     *os_name;
static char     *os_patch;
static int      num_rports;
static int      num_ios;
static int      num_tms;
static int      num_fcxps;
static int      num_ufbufs;
static int      reqq_size;
static int      rspq_size;
static int      num_sgpgs;
static int      rport_del_timeout = BFA_FCS_RPORT_DEF_DEL_TIMEOUT;
static int      bfa_io_max_sge = BFAD_IO_MAX_SGE;
static int      log_level = BFA_LOG_WARNING;
static int      ioc_auto_recover = BFA_TRUE;
static int      ipfc_enable = BFA_FALSE;
static int	fdmi_enable = BFA_TRUE;
int 		bfa_lun_queue_depth = BFAD_LUN_QUEUE_DEPTH;
int      	bfa_linkup_delay = -1;
int		bfa_debugfs_enable = 1;

module_param(os_name, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(os_name, "OS name of the hba host machine");
module_param(os_patch, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(os_patch, "OS patch level of the hba host machine");
module_param(host_name, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(host_name, "Hostname of the hba host machine");
module_param(num_rports, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_rports, "Max number of rports supported per port"
		" (physical/logical), default=1024");
module_param(num_ios, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_ios, "Max number of ioim requests, default=2000");
module_param(num_tms, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_tms, "Max number of task im requests, default=128");
module_param(num_fcxps, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_fcxps, "Max number of fcxp requests, default=64");
module_param(num_ufbufs, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_ufbufs, "Max number of unsolicited frame buffers,"
		" default=64");
module_param(reqq_size, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(reqq_size, "Max number of request queue elements,"
		" default=256");
module_param(rspq_size, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(rspq_size, "Max number of response queue elements,"
		" default=64");
module_param(num_sgpgs, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(num_sgpgs, "Number of scatter/gather pages, default=2048");
module_param(rport_del_timeout, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(rport_del_timeout, "Rport delete timeout, default=90 secs,"
		" Range[>0]");
module_param(bfa_lun_queue_depth, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bfa_lun_queue_depth, "Lun queue depth, default=32,"
		" Range[>0]");
module_param(bfa_io_max_sge, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bfa_io_max_sge, "Max io scatter/gather elements, default=255");
module_param(log_level, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(log_level, "Driver log level, default=3,"
		" Range[Critical:1|Error:2|Warning:3|Info:4]");
module_param(ioc_auto_recover, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ioc_auto_recover, "IOC auto recovery, default=1,"
		" Range[off:0|on:1]");
module_param(ipfc_enable, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ipfc_enable, "Enable IPoFC, default=0, Range[off:0|on:1]");
module_param(bfa_linkup_delay, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bfa_linkup_delay, "Link up delay, default=30 secs for boot"
		" port. Otherwise Range[>0]");
module_param(fdmi_enable, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(fdmi_enable, "Enables fdmi registration, default=1,"
		" Range[false:0|true:1]");
module_param(bfa_debugfs_enable, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bfa_debugfs_enable, "Enables debugfs feature, default=1,"
		" Range[false:0|true:1]");

/*
 * Stores the module parm num_sgpgs value;
 * used to reset for bfad next instance.
 */
static int num_sgpgs_parm;

static bfa_status_t
bfad_fc4_probe(struct bfad_s *bfad)
{
	int             rc;

	rc = bfad_im_probe(bfad);
	if (rc != BFA_STATUS_OK)
		goto ext;

	bfad_tm_probe(bfad);

	if (ipfc_enable)
		bfad_ipfc_probe(bfad);

	bfad->bfad_flags |= BFAD_FC4_PROBE_DONE;
ext:
	return rc;
}

static void
bfad_fc4_probe_undo(struct bfad_s *bfad)
{
	bfad_im_probe_undo(bfad);
	bfad_tm_probe_undo(bfad);
	if (ipfc_enable)
		bfad_ipfc_probe_undo(bfad);
	bfad->bfad_flags &= ~BFAD_FC4_PROBE_DONE;
}

static void
bfad_fc4_probe_post(struct bfad_s *bfad)
{
	if (bfad->im)
		bfad_im_probe_post(bfad->im);

	bfad_tm_probe_post(bfad);
	if (ipfc_enable)
		bfad_ipfc_probe_post(bfad);
}

static bfa_status_t
bfad_fc4_port_new(struct bfad_s *bfad, struct bfad_port_s *port, int roles)
{
	int             rc = BFA_STATUS_FAILED;

	if (roles & BFA_PORT_ROLE_FCP_IM)
		rc = bfad_im_port_new(bfad, port);
	if (rc != BFA_STATUS_OK)
		goto ext;

	if (roles & BFA_PORT_ROLE_FCP_TM)
		rc = bfad_tm_port_new(bfad, port);
	if (rc != BFA_STATUS_OK)
		goto ext;

	if ((roles & BFA_PORT_ROLE_FCP_IPFC) && ipfc_enable)
		rc = bfad_ipfc_port_new(bfad, port, port->pvb_type);
ext:
	return rc;
}

static void
bfad_fc4_port_delete(struct bfad_s *bfad, struct bfad_port_s *port, int roles)
{
	if (roles & BFA_PORT_ROLE_FCP_IM)
		bfad_im_port_delete(bfad, port);

	if (roles & BFA_PORT_ROLE_FCP_TM)
		bfad_tm_port_delete(bfad, port);

	if ((roles & BFA_PORT_ROLE_FCP_IPFC) && ipfc_enable)
		bfad_ipfc_port_delete(bfad, port);
}

/**
 *  BFA callbacks
 */
void
bfad_hcb_comp(void *arg, bfa_status_t status)
{
	struct bfad_hal_comp *fcomp = (struct bfad_hal_comp *)arg;

	fcomp->status = status;
	complete(&fcomp->comp);
}

/**
 * bfa_init callback
 */
void
bfa_cb_init(void *drv, bfa_status_t init_status)
{
	struct bfad_s  *bfad = drv;

	if (init_status == BFA_STATUS_OK) {
		bfad->bfad_flags |= BFAD_HAL_INIT_DONE;

		/* If BFAD_HAL_INIT_FAIL flag is set:
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



/**
 *  BFA_FCS callbacks
 */
static struct bfad_port_s *
bfad_get_drv_port(struct bfad_s *bfad, struct bfad_vf_s *vf_drv,
		  struct bfad_vport_s *vp_drv)
{
	return (vp_drv) ? (&(vp_drv)->drv_port)
		: ((vf_drv) ? (&(vf_drv)->base_port) : (&(bfad)->pport));
}

struct bfad_port_s *
bfa_fcb_port_new(struct bfad_s *bfad, struct bfa_fcs_port_s *port,
		 enum bfa_port_role roles, struct bfad_vf_s *vf_drv,
		 struct bfad_vport_s *vp_drv)
{
	bfa_status_t    rc;
	struct bfad_port_s *port_drv;

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
	rc = bfad_fc4_port_new(bfad, port_drv, roles);
	if (rc != BFA_STATUS_OK) {
		bfad_fc4_port_delete(bfad, port_drv, roles);
		port_drv = NULL;
	}

	return port_drv;
}

void
bfa_fcb_port_delete(struct bfad_s *bfad, enum bfa_port_role roles,
		    struct bfad_vf_s *vf_drv, struct bfad_vport_s *vp_drv)
{
	struct bfad_port_s *port_drv;

	/*
	 * this will be only called from rmmod context
	 */
	if (vp_drv && !vp_drv->comp_del) {
		port_drv = bfad_get_drv_port(bfad, vf_drv, vp_drv);
		bfa_trc(bfad, roles);
		bfad_fc4_port_delete(bfad, port_drv, roles);
	}
}

void
bfa_fcb_port_online(struct bfad_s *bfad, enum bfa_port_role roles,
		    struct bfad_vf_s *vf_drv, struct bfad_vport_s *vp_drv)
{
	struct bfad_port_s *port_drv = bfad_get_drv_port(bfad, vf_drv, vp_drv);

	if (roles & BFA_PORT_ROLE_FCP_IM)
		bfad_im_port_online(bfad, port_drv);

	if (roles & BFA_PORT_ROLE_FCP_TM)
		bfad_tm_port_online(bfad, port_drv);

	if ((roles & BFA_PORT_ROLE_FCP_IPFC) && ipfc_enable)
		bfad_ipfc_port_online(bfad, port_drv);

	bfad->bfad_flags |= BFAD_PORT_ONLINE;
}

void
bfa_fcb_port_offline(struct bfad_s *bfad, enum bfa_port_role roles,
		     struct bfad_vf_s *vf_drv, struct bfad_vport_s *vp_drv)
{
	struct bfad_port_s *port_drv = bfad_get_drv_port(bfad, vf_drv, vp_drv);

	if (roles & BFA_PORT_ROLE_FCP_IM)
		bfad_im_port_offline(bfad, port_drv);

	if (roles & BFA_PORT_ROLE_FCP_TM)
		bfad_tm_port_offline(bfad, port_drv);

	if ((roles & BFA_PORT_ROLE_FCP_IPFC) && ipfc_enable)
		bfad_ipfc_port_offline(bfad, port_drv);
}

void
bfa_fcb_vport_delete(struct bfad_vport_s *vport_drv)
{
	if (vport_drv->comp_del) {
		complete(vport_drv->comp_del);
		return;
	}
}

/**
 * FCS RPORT alloc callback, after successful PLOGI by FCS
 */
bfa_status_t
bfa_fcb_rport_alloc(struct bfad_s *bfad, struct bfa_fcs_rport_s **rport,
		    struct bfad_rport_s **rport_drv)
{
	bfa_status_t    rc = BFA_STATUS_OK;

	*rport_drv = kzalloc(sizeof(struct bfad_rport_s), GFP_ATOMIC);
	if (*rport_drv == NULL) {
		rc = BFA_STATUS_ENOMEM;
		goto ext;
	}

	*rport = &(*rport_drv)->fcs_rport;

ext:
	return rc;
}

/**
 * @brief
 * FCS PBC VPORT Create
 */
void
bfa_fcb_pbc_vport_create(struct bfad_s *bfad, struct bfi_pbc_vport_s pbc_vport)
{

	struct bfad_pcfg_s *pcfg;

	pcfg = kzalloc(sizeof(struct bfad_pcfg_s), GFP_ATOMIC);
	if (!pcfg) {
		bfa_trc(bfad, 0);
		return;
	}

	pcfg->port_cfg.roles = BFA_PORT_ROLE_FCP_IM;
	pcfg->port_cfg.pwwn = pbc_vport.vp_pwwn;
	pcfg->port_cfg.nwwn = pbc_vport.vp_nwwn;
	pcfg->port_cfg.preboot_vp  = BFA_TRUE;

	list_add_tail(&pcfg->list_entry, &bfad->pbc_pcfg_list);

	return;
}

void
bfad_hal_mem_release(struct bfad_s *bfad)
{
	int             i;
	struct bfa_meminfo_s *hal_meminfo = &bfad->meminfo;
	struct bfa_mem_elem_s *meminfo_elem;

	for (i = 0; i < BFA_MEM_TYPE_MAX; i++) {
		meminfo_elem = &hal_meminfo->meminfo[i];
		if (meminfo_elem->kva != NULL) {
			switch (meminfo_elem->mem_type) {
			case BFA_MEM_TYPE_KVA:
				vfree(meminfo_elem->kva);
				break;
			case BFA_MEM_TYPE_DMA:
				dma_free_coherent(&bfad->pcidev->dev,
						meminfo_elem->mem_len,
						meminfo_elem->kva,
						(dma_addr_t) meminfo_elem->dma);
				break;
			default:
				bfa_assert(0);
				break;
			}
		}
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
	if (num_fcxps > 0)
		bfa_cfg->fwcfg.num_fcxp_reqs = num_fcxps;
	if (num_ufbufs > 0)
		bfa_cfg->fwcfg.num_uf_bufs = num_ufbufs;
	if (reqq_size > 0)
		bfa_cfg->drvcfg.num_reqq_elems = reqq_size;
	if (rspq_size > 0)
		bfa_cfg->drvcfg.num_rspq_elems = rspq_size;
	if (num_sgpgs > 0)
		bfa_cfg->drvcfg.num_sgpgs = num_sgpgs;

	/*
	 * populate the hal values back to the driver for sysfs use.
	 * otherwise, the default values will be shown as 0 in sysfs
	 */
	num_rports = bfa_cfg->fwcfg.num_rports;
	num_ios    = bfa_cfg->fwcfg.num_ioim_reqs;
	num_tms	   = bfa_cfg->fwcfg.num_tskim_reqs;
	num_fcxps  = bfa_cfg->fwcfg.num_fcxp_reqs;
	num_ufbufs = bfa_cfg->fwcfg.num_uf_bufs;
	reqq_size  = bfa_cfg->drvcfg.num_reqq_elems;
	rspq_size  = bfa_cfg->drvcfg.num_rspq_elems;
	num_sgpgs  = bfa_cfg->drvcfg.num_sgpgs;
}

bfa_status_t
bfad_hal_mem_alloc(struct bfad_s *bfad)
{
	struct bfa_meminfo_s *hal_meminfo = &bfad->meminfo;
	struct bfa_mem_elem_s *meminfo_elem;
	bfa_status_t    rc = BFA_STATUS_OK;
	dma_addr_t      phys_addr;
	int             retry_count = 0;
	int             reset_value = 1;
	int             min_num_sgpgs = 512;
	void           *kva;
	int             i;

	bfa_cfg_get_default(&bfad->ioc_cfg);

retry:
	bfad_update_hal_cfg(&bfad->ioc_cfg);
	bfad->cfg_data.ioc_queue_depth = bfad->ioc_cfg.fwcfg.num_ioim_reqs;
	bfa_cfg_get_meminfo(&bfad->ioc_cfg, hal_meminfo);

	for (i = 0; i < BFA_MEM_TYPE_MAX; i++) {
		meminfo_elem = &hal_meminfo->meminfo[i];
		switch (meminfo_elem->mem_type) {
		case BFA_MEM_TYPE_KVA:
			kva = vmalloc(meminfo_elem->mem_len);
			if (kva == NULL) {
				bfad_hal_mem_release(bfad);
				rc = BFA_STATUS_ENOMEM;
				goto ext;
			}
			memset(kva, 0, meminfo_elem->mem_len);
			meminfo_elem->kva = kva;
			break;
		case BFA_MEM_TYPE_DMA:
			kva = dma_alloc_coherent(&bfad->pcidev->dev,
					meminfo_elem->mem_len,
					&phys_addr, GFP_KERNEL);
			if (kva == NULL) {
				bfad_hal_mem_release(bfad);
				/*
				 * If we cannot allocate with default
				 * num_sgpages try with half the value.
				 */
				if (num_sgpgs > min_num_sgpgs) {
					printk(KERN_INFO "bfad[%d]: memory"
						" allocation failed with"
						" num_sgpgs: %d\n",
						bfad->inst_no, num_sgpgs);
					nextLowerInt(&num_sgpgs);
					printk(KERN_INFO "bfad[%d]: trying to"
						" allocate memory with"
						" num_sgpgs: %d\n",
						bfad->inst_no, num_sgpgs);
					retry_count++;
					goto retry;
				} else {
					if (num_sgpgs_parm > 0)
						num_sgpgs = num_sgpgs_parm;
					else {
						reset_value =
							(1 << retry_count);
						num_sgpgs *= reset_value;
					}
					rc = BFA_STATUS_ENOMEM;
					goto ext;
				}
			}

			if (num_sgpgs_parm > 0)
				num_sgpgs = num_sgpgs_parm;
			else {
				reset_value = (1 << retry_count);
				num_sgpgs *= reset_value;
			}

			memset(kva, 0, meminfo_elem->mem_len);
			meminfo_elem->kva = kva;
			meminfo_elem->dma = phys_addr;
			break;
		default:
			break;

		}
	}
ext:
	return rc;
}

/**
 * Create a vport under a vf.
 */
bfa_status_t
bfad_vport_create(struct bfad_s *bfad, u16 vf_id,
			struct bfa_port_cfg_s *port_cfg, struct device *dev)
{
	struct bfad_vport_s *vport;
	int rc = BFA_STATUS_OK;
	unsigned long   flags;
	struct completion fcomp;

	vport = kzalloc(sizeof(struct bfad_vport_s), GFP_KERNEL);
	if (!vport) {
		rc = BFA_STATUS_ENOMEM;
		goto ext;
	}

	vport->drv_port.bfad = bfad;
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (port_cfg->preboot_vp == BFA_TRUE)
		rc = bfa_fcs_pbc_vport_create(&vport->fcs_vport,
				&bfad->bfa_fcs, vf_id, port_cfg, vport);
	else
		rc = bfa_fcs_vport_create(&vport->fcs_vport,
				&bfad->bfa_fcs, vf_id, port_cfg, vport);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (rc != BFA_STATUS_OK)
		goto ext_free_vport;

	if (port_cfg->roles & BFA_PORT_ROLE_FCP_IM) {
		rc = bfad_im_scsi_host_alloc(bfad, vport->drv_port.im_port,
							dev);
		if (rc != BFA_STATUS_OK)
			goto ext_free_fcs_vport;
	}

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_fcs_vport_start(&vport->fcs_vport);
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

/**
 * Create a vf and its base vport implicitely.
 */
bfa_status_t
bfad_vf_create(struct bfad_s *bfad, u16 vf_id,
	       struct bfa_port_cfg_s *port_cfg)
{
	struct bfad_vf_s *vf;
	int             rc = BFA_STATUS_OK;

	vf = kzalloc(sizeof(struct bfad_vf_s), GFP_KERNEL);
	if (!vf) {
		rc = BFA_STATUS_FAILED;
		goto ext;
	}

	rc = bfa_fcs_vf_create(&vf->fcs_vf, &bfad->bfa_fcs, vf_id, port_cfg,
			       vf);
	if (rc != BFA_STATUS_OK)
		kfree(vf);
ext:
	return rc;
}

void
bfad_bfa_tmo(unsigned long data)
{
	struct bfad_s  *bfad = (struct bfad_s *)data;
	unsigned long   flags;
	struct list_head  doneq;

	spin_lock_irqsave(&bfad->bfad_lock, flags);

	bfa_timer_tick(&bfad->bfa);

	bfa_comp_deq(&bfad->bfa, &doneq);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (!list_empty(&doneq)) {
		bfa_comp_process(&bfad->bfa, &doneq);
		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfa_comp_free(&bfad->bfa, &doneq);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	}

	mod_timer(&bfad->hal_tmo, jiffies + msecs_to_jiffies(BFA_TIMER_FREQ));
}

void
bfad_init_timer(struct bfad_s *bfad)
{
	init_timer(&bfad->hal_tmo);
	bfad->hal_tmo.function = bfad_bfa_tmo;
	bfad->hal_tmo.data = (unsigned long)bfad;

	mod_timer(&bfad->hal_tmo, jiffies + msecs_to_jiffies(BFA_TIMER_FREQ));
}

int
bfad_pci_init(struct pci_dev *pdev, struct bfad_s *bfad)
{
	int             rc = -ENODEV;

	if (pci_enable_device(pdev)) {
		BFA_PRINTF(BFA_ERR, "pci_enable_device fail %p\n", pdev);
		goto out;
	}

	if (pci_request_regions(pdev, BFAD_DRIVER_NAME))
		goto out_disable_device;

	pci_set_master(pdev);


	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) != 0)
		if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32)) != 0) {
			BFA_PRINTF(BFA_ERR, "pci_set_dma_mask fail %p\n", pdev);
			goto out_release_region;
		}

	bfad->pci_bar0_kva = pci_iomap(pdev, 0, pci_resource_len(pdev, 0));

	if (bfad->pci_bar0_kva == NULL) {
		BFA_PRINTF(BFA_ERR, "Fail to map bar0\n");
		goto out_release_region;
	}

	bfad->hal_pcidev.pci_slot = PCI_SLOT(pdev->devfn);
	bfad->hal_pcidev.pci_func = PCI_FUNC(pdev->devfn);
	bfad->hal_pcidev.pci_bar_kva = bfad->pci_bar0_kva;
	bfad->hal_pcidev.device_id = pdev->device;
	bfad->pci_name = pci_name(pdev);

	bfad->pci_attr.vendor_id = pdev->vendor;
	bfad->pci_attr.device_id = pdev->device;
	bfad->pci_attr.ssid = pdev->subsystem_device;
	bfad->pci_attr.ssvid = pdev->subsystem_vendor;
	bfad->pci_attr.pcifn = PCI_FUNC(pdev->devfn);

	bfad->pcidev = pdev;
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
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

void
bfad_fcs_port_cfg(struct bfad_s *bfad)
{
	struct bfa_port_cfg_s port_cfg;
	struct bfa_pport_attr_s attr;
	char            symname[BFA_SYMNAME_MAXLEN];

	sprintf(symname, "%s-%d", BFAD_DRIVER_NAME, bfad->inst_no);
	memcpy(port_cfg.sym_name.symname, symname, strlen(symname));
	bfa_fcport_get_attr(&bfad->bfa, &attr);
	port_cfg.nwwn = attr.nwwn;
	port_cfg.pwwn = attr.pwwn;

	bfa_fcs_cfg_base_port(&bfad->bfa_fcs, &port_cfg);
}

bfa_status_t
bfad_drv_init(struct bfad_s *bfad)
{
	bfa_status_t    rc;
	unsigned long   flags;
	struct bfa_fcs_driver_info_s driver_info;

	bfad->cfg_data.rport_del_timeout = rport_del_timeout;
	bfad->cfg_data.lun_queue_depth = bfa_lun_queue_depth;
	bfad->cfg_data.io_max_sge = bfa_io_max_sge;
	bfad->cfg_data.binding_method = FCP_PWWN_BINDING;

	rc = bfad_hal_mem_alloc(bfad);
	if (rc != BFA_STATUS_OK) {
		printk(KERN_WARNING "bfad%d bfad_hal_mem_alloc failure\n",
		       bfad->inst_no);
		printk(KERN_WARNING
			"Not enough memory to attach all Brocade HBA ports,"
			" System may need more memory.\n");
		goto out_hal_mem_alloc_failure;
	}

	bfa_init_log(&bfad->bfa, bfad->logmod);
	bfa_init_trc(&bfad->bfa, bfad->trcmod);
	bfa_init_aen(&bfad->bfa, bfad->aen);
	memset(bfad->file_map, 0, sizeof(bfad->file_map));
	bfa_init_plog(&bfad->bfa, &bfad->plog_buf);
	bfa_plog_init(&bfad->plog_buf);
	bfa_plog_str(&bfad->plog_buf, BFA_PL_MID_DRVR, BFA_PL_EID_DRIVER_START,
		     0, "Driver Attach");

	bfa_attach(&bfad->bfa, bfad, &bfad->ioc_cfg, &bfad->meminfo,
		   &bfad->hal_pcidev);

	init_completion(&bfad->comp);

	/*
	 * Enable Interrupt and wait bfa_init completion
	 */
	if (bfad_setup_intr(bfad)) {
		printk(KERN_WARNING "bfad%d: bfad_setup_intr failed\n",
		       bfad->inst_no);
		goto out_setup_intr_failure;
	}

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_init(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	/*
	 * Set up interrupt handler for each vectors
	 */
	if ((bfad->bfad_flags & BFAD_MSIX_ON)
	    && bfad_install_msix_handler(bfad)) {
		printk(KERN_WARNING "%s: install_msix failed, bfad%d\n",
		       __func__, bfad->inst_no);
	}

	bfad_init_timer(bfad);

	wait_for_completion(&bfad->comp);

	memset(&driver_info, 0, sizeof(driver_info));
	strncpy(driver_info.version, BFAD_DRIVER_VERSION,
		sizeof(driver_info.version) - 1);
	__kernel_param_lock();
	if (host_name)
		strncpy(driver_info.host_machine_name, host_name,
			sizeof(driver_info.host_machine_name) - 1);
	if (os_name)
		strncpy(driver_info.host_os_name, os_name,
			sizeof(driver_info.host_os_name) - 1);
	if (os_patch)
		strncpy(driver_info.host_os_patch, os_patch,
			sizeof(driver_info.host_os_patch) - 1);
	__kernel_param_unlock();

	strncpy(driver_info.os_device_name, bfad->pci_name,
		sizeof(driver_info.os_device_name - 1));

	/*
	 * FCS INIT
	 */
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_fcs_log_init(&bfad->bfa_fcs, bfad->logmod);
	bfa_fcs_trc_init(&bfad->bfa_fcs, bfad->trcmod);
	bfa_fcs_aen_init(&bfad->bfa_fcs, bfad->aen);
	bfa_fcs_attach(&bfad->bfa_fcs, &bfad->bfa, bfad, BFA_FALSE);

	/* Do FCS init only when HAL init is done */
	if ((bfad->bfad_flags & BFAD_HAL_INIT_DONE)) {
		bfa_fcs_init(&bfad->bfa_fcs);
		bfad->bfad_flags |= BFAD_FCS_INIT_DONE;
	}

	bfa_fcs_driver_info_init(&bfad->bfa_fcs, &driver_info);
	bfa_fcs_set_fdmi_param(&bfad->bfa_fcs, fdmi_enable);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	bfad->bfad_flags |= BFAD_DRV_INIT_DONE;
	return BFA_STATUS_OK;

out_setup_intr_failure:
	bfa_detach(&bfad->bfa);
	bfad_hal_mem_release(bfad);
out_hal_mem_alloc_failure:
	return BFA_STATUS_FAILED;
}

void
bfad_drv_uninit(struct bfad_s *bfad)
{
	unsigned long   flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	init_completion(&bfad->comp);
	bfa_stop(&bfad->bfa);
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
	unsigned long   flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_start(&bfad->bfa);
	bfa_fcs_start(&bfad->bfa_fcs);
	bfad->bfad_flags |= BFAD_HAL_START_DONE;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	bfad_fc4_probe_post(bfad);
}

void
bfad_drv_stop(struct bfad_s *bfad)
{
	unsigned long   flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	init_completion(&bfad->comp);
	bfad->pport.flags |= BFAD_PORT_DELETE;
	bfa_fcs_exit(&bfad->bfa_fcs);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	wait_for_completion(&bfad->comp);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	init_completion(&bfad->comp);
	bfa_stop(&bfad->bfa);
	bfad->bfad_flags &= ~BFAD_HAL_START_DONE;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	wait_for_completion(&bfad->comp);
}

bfa_status_t
bfad_cfg_pport(struct bfad_s *bfad, enum bfa_port_role role)
{
	int             rc = BFA_STATUS_OK;

	/*
	 * Allocate scsi_host for the physical port
	 */
	if ((bfad_supported_fc4s & BFA_PORT_ROLE_FCP_IM)
	    && (role & BFA_PORT_ROLE_FCP_IM)) {
		if (bfad->pport.im_port == NULL) {
			rc = BFA_STATUS_FAILED;
			goto out;
		}

		rc = bfad_im_scsi_host_alloc(bfad, bfad->pport.im_port,
						&bfad->pcidev->dev);
		if (rc != BFA_STATUS_OK)
			goto out;

		bfad->pport.roles |= BFA_PORT_ROLE_FCP_IM;
	}

	/* Setup the debugfs node for this scsi_host */
	if (bfa_debugfs_enable)
		bfad_debugfs_init(&bfad->pport);

	bfad->bfad_flags |= BFAD_CFG_PPORT_DONE;

out:
	return rc;
}

void
bfad_uncfg_pport(struct bfad_s *bfad)
{
	 /* Remove the debugfs node for this scsi_host */
	kfree(bfad->regdata);
	bfad_debugfs_exit(&bfad->pport);

	if ((bfad->pport.roles & BFA_PORT_ROLE_FCP_IPFC) && ipfc_enable) {
		bfad_ipfc_port_delete(bfad, &bfad->pport);
		bfad->pport.roles &= ~BFA_PORT_ROLE_FCP_IPFC;
	}

	if ((bfad_supported_fc4s & BFA_PORT_ROLE_FCP_IM)
	    && (bfad->pport.roles & BFA_PORT_ROLE_FCP_IM)) {
		bfad_im_scsi_host_free(bfad, bfad->pport.im_port);
		bfad_im_port_clean(bfad->pport.im_port);
		kfree(bfad->pport.im_port);
		bfad->pport.roles &= ~BFA_PORT_ROLE_FCP_IM;
	}

	bfad->bfad_flags &= ~BFAD_CFG_PPORT_DONE;
}

void
bfad_drv_log_level_set(struct bfad_s *bfad)
{
	if (log_level > BFA_LOG_INVALID && log_level <= BFA_LOG_LEVEL_MAX)
		bfa_log_set_level_all(&bfad->log_data, log_level);
}

bfa_status_t
bfad_start_ops(struct bfad_s *bfad)
{
	int retval;
	struct bfad_pcfg_s *pcfg, *pcfg_new;

	/* PPORT FCS config */
	bfad_fcs_port_cfg(bfad);

	retval = bfad_cfg_pport(bfad, BFA_PORT_ROLE_FCP_IM);
	if (retval != BFA_STATUS_OK)
		goto out_cfg_pport_failure;

	/* BFAD level FC4 (IM/TM/IPFC) specific resource allocation */
	retval = bfad_fc4_probe(bfad);
	if (retval != BFA_STATUS_OK) {
		printk(KERN_WARNING "bfad_fc4_probe failed\n");
		goto out_fc4_probe_failure;
	}

	bfad_drv_start(bfad);

	/* pbc vport creation */
	list_for_each_entry_safe(pcfg, pcfg_new,  &bfad->pbc_pcfg_list,
					list_entry) {
		struct fc_vport_identifiers vid;
		struct fc_vport *fc_vport;

		memset(&vid, 0, sizeof(vid));
		vid.roles = FC_PORT_ROLE_FCP_INITIATOR;
		vid.vport_type = FC_PORTTYPE_NPIV;
		vid.disable = false;
		vid.node_name = wwn_to_u64((u8 *)&pcfg->port_cfg.nwwn);
		vid.port_name = wwn_to_u64((u8 *)&pcfg->port_cfg.pwwn);
		fc_vport = fc_vport_create(bfad->pport.im_port->shost, 0, &vid);
		if (!fc_vport)
			printk(KERN_WARNING "bfad%d: failed to create pbc vport"
				" %llx\n", bfad->inst_no, vid.port_name);
		list_del(&pcfg->list_entry);
		kfree(pcfg);

	}

	/*
	 * If bfa_linkup_delay is set to -1 default; try to retrive the
	 * value using the bfad_os_get_linkup_delay(); else use the
	 * passed in module param value as the bfa_linkup_delay.
	 */
	if (bfa_linkup_delay < 0) {

		bfa_linkup_delay = bfad_os_get_linkup_delay(bfad);
		bfad_os_rport_online_wait(bfad);
		bfa_linkup_delay = -1;

	} else {
		bfad_os_rport_online_wait(bfad);
	}

	bfa_log(bfad->logmod, BFA_LOG_LINUX_DEVICE_CLAIMED, bfad->pci_name);

	return BFA_STATUS_OK;

out_fc4_probe_failure:
	bfad_fc4_probe_undo(bfad);
	bfad_uncfg_pport(bfad);
out_cfg_pport_failure:
	return BFA_STATUS_FAILED;
}

int
bfad_worker(void *ptr)
{
	struct bfad_s *bfad;
	unsigned long   flags;

	bfad = (struct bfad_s *)ptr;

	while (!kthread_should_stop()) {

		/* Check if the FCS init is done from bfad_drv_init;
		 * if not done do FCS init and set the flag.
		 */
		if (!(bfad->bfad_flags & BFAD_FCS_INIT_DONE)) {
			spin_lock_irqsave(&bfad->bfad_lock, flags);
			bfa_fcs_init(&bfad->bfa_fcs);
			bfad->bfad_flags |= BFAD_FCS_INIT_DONE;
			spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		}

		/* Start the bfad operations after HAL init done */
		bfad_start_ops(bfad);

		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfad->bfad_tsk = NULL;
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);

		break;
	}

	return 0;
}

 /*
  *  PCI_entry PCI driver entries * {
  */

/**
 * PCI probe entry.
 */
int
bfad_pci_probe(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	struct bfad_s  *bfad;
	int             error = -ENODEV, retval;

	/*
	 * For single port cards - only claim function 0
	 */
	if ((pdev->device == BFA_PCI_DEVICE_ID_FC_8G1P)
	    && (PCI_FUNC(pdev->devfn) != 0))
		return -ENODEV;

	BFA_TRACE(BFA_INFO, "bfad_pci_probe entry");

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

	/*
	 * LOG/TRACE INIT
	 */
	bfa_trc_init(bfad->trcmod);
	bfa_trc(bfad, bfad_inst);

	bfad->logmod = &bfad->log_data;
	bfa_log_init(bfad->logmod, (char *)pci_name(pdev), bfa_os_printf);

	bfad_drv_log_level_set(bfad);

	bfad->aen = &bfad->aen_buf;

	if (!(bfad_load_fwimg(pdev))) {
		printk(KERN_WARNING "bfad_load_fwimg failure!\n");
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

	spin_lock_init(&bfad->bfad_lock);
	pci_set_drvdata(pdev, bfad);

	bfad->ref_count = 0;
	bfad->pport.bfad = bfad;
	INIT_LIST_HEAD(&bfad->pbc_pcfg_list);

	bfad->bfad_tsk = kthread_create(bfad_worker, (void *) bfad, "%s",
					"bfad_worker");
	if (IS_ERR(bfad->bfad_tsk)) {
		printk(KERN_INFO "bfad[%d]: Kernel thread"
			" creation failed!\n",
			bfad->inst_no);
		goto out_kthread_create_failure;
	}

	retval = bfad_drv_init(bfad);
	if (retval != BFA_STATUS_OK)
		goto out_drv_init_failure;
	if (!(bfad->bfad_flags & BFAD_HAL_INIT_DONE)) {
		bfad->bfad_flags |= BFAD_HAL_INIT_FAIL;
		printk(KERN_WARNING "bfad%d: hal init failed\n", bfad->inst_no);
		goto ok;
	}

	retval = bfad_start_ops(bfad);
	if (retval != BFA_STATUS_OK)
		goto out_start_ops_failure;

	kthread_stop(bfad->bfad_tsk);
	bfad->bfad_tsk = NULL;

ok:
	return 0;

out_start_ops_failure:
	bfad_drv_uninit(bfad);
out_drv_init_failure:
	kthread_stop(bfad->bfad_tsk);
out_kthread_create_failure:
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

/**
 * PCI remove entry.
 */
void
bfad_pci_remove(struct pci_dev *pdev)
{
	struct bfad_s  *bfad = pci_get_drvdata(pdev);
	unsigned long   flags;

	bfa_trc(bfad, bfad->inst_no);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (bfad->bfad_tsk != NULL)
		kthread_stop(bfad->bfad_tsk);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if ((bfad->bfad_flags & BFAD_DRV_INIT_DONE)
	    && !(bfad->bfad_flags & BFAD_HAL_INIT_DONE)) {

		spin_lock_irqsave(&bfad->bfad_lock, flags);
		init_completion(&bfad->comp);
		bfa_stop(&bfad->bfa);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		wait_for_completion(&bfad->comp);

		bfad_remove_intr(bfad);
		del_timer_sync(&bfad->hal_tmo);
		goto hal_detach;
	} else if (!(bfad->bfad_flags & BFAD_DRV_INIT_DONE)) {
		goto remove_sysfs;
	}

	if (bfad->bfad_flags & BFAD_HAL_START_DONE) {
		bfad_drv_stop(bfad);
	} else if (bfad->bfad_flags & BFAD_DRV_INIT_DONE) {
		/* Invoking bfa_stop() before bfa_detach
		 * when HAL and DRV init are success
		 * but HAL start did not occur.
		 */
		spin_lock_irqsave(&bfad->bfad_lock, flags);
		init_completion(&bfad->comp);
		bfa_stop(&bfad->bfa);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		wait_for_completion(&bfad->comp);
	}

	bfad_remove_intr(bfad);
	del_timer_sync(&bfad->hal_tmo);

	if (bfad->bfad_flags & BFAD_FC4_PROBE_DONE)
		bfad_fc4_probe_undo(bfad);

	if (bfad->bfad_flags & BFAD_CFG_PPORT_DONE)
		bfad_uncfg_pport(bfad);

hal_detach:
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_detach(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	bfad_hal_mem_release(bfad);
remove_sysfs:

	mutex_lock(&bfad_mutex);
	bfad_inst--;
	list_del(&bfad->list_entry);
	mutex_unlock(&bfad_mutex);
	bfad_pci_uninit(pdev, bfad);

	kfree(bfad->trcmod);
	kfree(bfad);
}


static struct pci_device_id bfad_id_table[] = {
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

	{0, 0},
};

MODULE_DEVICE_TABLE(pci, bfad_id_table);

static struct pci_driver bfad_pci_driver = {
	.name = BFAD_DRIVER_NAME,
	.id_table = bfad_id_table,
	.probe = bfad_pci_probe,
	.remove = __devexit_p(bfad_pci_remove),
};

/**
 *  Linux driver module functions
 */
bfa_status_t
bfad_fc4_module_init(void)
{
	int             rc;

	rc = bfad_im_module_init();
	if (rc != BFA_STATUS_OK)
		goto ext;

	bfad_tm_module_init();
	if (ipfc_enable)
		bfad_ipfc_module_init();
ext:
	return rc;
}

void
bfad_fc4_module_exit(void)
{
	if (ipfc_enable)
		bfad_ipfc_module_exit();
	bfad_tm_module_exit();
	bfad_im_module_exit();
}

/**
 * Driver module init.
 */
static int      __init
bfad_init(void)
{
	int             error = 0;

	printk(KERN_INFO "Brocade BFA FC/FCOE SCSI driver - version: %s\n",
	       BFAD_DRIVER_VERSION);

	if (num_sgpgs > 0)
		num_sgpgs_parm = num_sgpgs;

	error = bfad_fc4_module_init();
	if (error) {
		error = -ENOMEM;
		printk(KERN_WARNING "bfad_fc4_module_init failure\n");
		goto ext;
	}

	if (!strcmp(FCPI_NAME, " fcpim"))
		bfad_supported_fc4s |= BFA_PORT_ROLE_FCP_IM;
	if (!strcmp(FCPT_NAME, " fcptm"))
		bfad_supported_fc4s |= BFA_PORT_ROLE_FCP_TM;
	if (!strcmp(IPFC_NAME, " ipfc"))
		bfad_supported_fc4s |= BFA_PORT_ROLE_FCP_IPFC;

	bfa_ioc_auto_recover(ioc_auto_recover);
	bfa_fcs_rport_set_del_timeout(rport_del_timeout);
	error = pci_register_driver(&bfad_pci_driver);

	if (error) {
		printk(KERN_WARNING "bfad pci_register_driver failure\n");
		goto ext;
	}

	return 0;

ext:
	bfad_fc4_module_exit();
	return error;
}

/**
 * Driver module exit.
 */
static void     __exit
bfad_exit(void)
{
	pci_unregister_driver(&bfad_pci_driver);
	bfad_fc4_module_exit();
	bfad_free_fwimg();
}

#define BFAD_PROTO_NAME FCPI_NAME FCPT_NAME IPFC_NAME

module_init(bfad_init);
module_exit(bfad_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Brocade Fibre Channel HBA Driver" BFAD_PROTO_NAME);
MODULE_AUTHOR("Brocade Communications Systems, Inc.");
MODULE_VERSION(BFAD_DRIVER_VERSION);


