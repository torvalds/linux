/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Aic94xx SAS/SATA driver header file.
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * $Id: //depot/aic94xx/aic94xx.h#31 $
 */

#ifndef _AIC94XX_H_
#define _AIC94XX_H_

#include <linux/slab.h>
#include <linux/ctype.h>
#include <scsi/libsas.h>

#define ASD_DRIVER_NAME		"aic94xx"
#define ASD_DRIVER_DESCRIPTION	"Adaptec aic94xx SAS/SATA driver"

#define asd_printk(fmt, ...)	printk(KERN_NOTICE ASD_DRIVER_NAME ": " fmt, ## __VA_ARGS__)

#ifdef ASD_ENTER_EXIT
#define ENTER  printk(KERN_NOTICE "%s: ENTER %s\n", ASD_DRIVER_NAME, \
		__func__)
#define EXIT   printk(KERN_NOTICE "%s: --EXIT %s\n", ASD_DRIVER_NAME, \
		__func__)
#else
#define ENTER
#define EXIT
#endif

#ifdef ASD_DEBUG
#define ASD_DPRINTK asd_printk
#else
#define ASD_DPRINTK(fmt, ...)
#endif

/* 2*ITNL timeout + 1 second */
#define AIC94XX_SCB_TIMEOUT  (5*HZ)

extern struct kmem_cache *asd_dma_token_cache;
extern struct kmem_cache *asd_ascb_cache;

static inline void asd_stringify_sas_addr(char *p, const u8 *sas_addr)
{
	int i;
	for (i = 0; i < SAS_ADDR_SIZE; i++, p += 2)
		snprintf(p, 3, "%02X", sas_addr[i]);
	*p = '\0';
}

struct asd_ha_struct;
struct asd_ascb;

int  asd_read_ocm(struct asd_ha_struct *asd_ha);
int  asd_read_flash(struct asd_ha_struct *asd_ha);

int  asd_dev_found(struct domain_device *dev);
void asd_dev_gone(struct domain_device *dev);

void asd_invalidate_edb(struct asd_ascb *ascb, int edb_id);

int  asd_execute_task(struct sas_task *task, gfp_t gfp_flags);

void asd_set_dmamode(struct domain_device *dev);

/* ---------- TMFs ---------- */
int  asd_abort_task(struct sas_task *);
int  asd_abort_task_set(struct domain_device *, u8 *lun);
int  asd_clear_aca(struct domain_device *, u8 *lun);
int  asd_clear_task_set(struct domain_device *, u8 *lun);
int  asd_lu_reset(struct domain_device *, u8 *lun);
int  asd_I_T_nexus_reset(struct domain_device *dev);
int  asd_query_task(struct sas_task *);

/* ---------- Adapter and Port management ---------- */
int  asd_clear_nexus_port(struct asd_sas_port *port);
int  asd_clear_nexus_ha(struct sas_ha_struct *sas_ha);

/* ---------- Phy Management ---------- */
int  asd_control_phy(struct asd_sas_phy *phy, enum phy_func func, void *arg);

#endif
