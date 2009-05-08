/*
	mv_sas.h - Marvell 88SE6440 SAS/SATA support

	Copyright 2007 Red Hat, Inc.
	Copyright 2008 Marvell. <kewei@marvell.com>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation; either version 2,
	or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty
	of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public
	License along with this program; see the file COPYING.	If not,
	write to the Free Software Foundation, 675 Mass Ave, Cambridge,
	MA 02139, USA.

 */

#ifndef _MV_SAS_H_
#define _MV_SAS_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <scsi/libsas.h>
#include <scsi/scsi_tcq.h>
#include <scsi/sas_ata.h>
#include <linux/version.h>
#include "mv_defs.h"

#define DRV_NAME	"mvsas"
#define DRV_VERSION	"0.5.2"
#define _MV_DUMP	0
#define MVS_DISABLE_NVRAM
#define MVS_DISABLE_MSI

#define MVS_ID_NOT_MAPPED	0x7f
#define MVS_CHIP_SLOT_SZ	(1U << mvi->chip->slot_width)

#define for_each_phy(__lseq_mask, __mc, __lseq, __rest)			\
	for ((__mc) = (__lseq_mask), (__lseq) = 0;			\
					(__mc) != 0 && __rest;		\
					(++__lseq), (__mc) >>= 1)

struct mvs_chip_info {
	u32		n_phy;
	u32		srs_sz;
	u32		slot_width;
};

struct mvs_err_info {
	__le32			flags;
	__le32			flags2;
};

struct mvs_cmd_hdr {
	__le32			flags;	/* PRD tbl len; SAS, SATA ctl */
	__le32			lens;	/* cmd, max resp frame len */
	__le32			tags;	/* targ port xfer tag; tag */
	__le32			data_len;	/* data xfer len */
	__le64			cmd_tbl;	/* command table address */
	__le64			open_frame;	/* open addr frame address */
	__le64			status_buf;	/* status buffer address */
	__le64			prd_tbl;		/* PRD tbl address */
	__le32			reserved[4];
};

struct mvs_port {
	struct asd_sas_port	sas_port;
	u8			port_attached;
	u8			taskfileset;
	u8			wide_port_phymap;
	struct list_head	list;
};

struct mvs_phy {
	struct mvs_port		*port;
	struct asd_sas_phy	sas_phy;
	struct sas_identify	identify;
	struct scsi_device	*sdev;
	u64		dev_sas_addr;
	u64		att_dev_sas_addr;
	u32		att_dev_info;
	u32		dev_info;
	u32		phy_type;
	u32		phy_status;
	u32		irq_status;
	u32		frame_rcvd_size;
	u8		frame_rcvd[32];
	u8		phy_attached;
	enum sas_linkrate	minimum_linkrate;
	enum sas_linkrate	maximum_linkrate;
};

struct mvs_slot_info {
	struct list_head list;
	struct sas_task *task;
	u32 n_elem;
	u32 tx;

	/* DMA buffer for storing cmd tbl, open addr frame, status buffer,
	 * and PRD table
	 */
	void *buf;
	dma_addr_t buf_dma;
#if _MV_DUMP
	u32 cmd_size;
#endif

	void *response;
	struct mvs_port *port;
};

struct mvs_info {
	unsigned long flags;

	/* host-wide lock */
	spinlock_t lock;

	/* our device */
	struct pci_dev *pdev;

	/* enhanced mode registers */
	void __iomem *regs;

	/* peripheral registers */
	void __iomem *peri_regs;

	u8 sas_addr[SAS_ADDR_SIZE];

	/* SCSI/SAS glue */
	struct sas_ha_struct sas;
	struct Scsi_Host *shost;

	/* TX (delivery) DMA ring */
	__le32 *tx;
	dma_addr_t tx_dma;

	/* cached next-producer idx */
	u32 tx_prod;

	/* RX (completion) DMA ring */
	__le32 *rx;
	dma_addr_t rx_dma;

	/* RX consumer idx */
	u32 rx_cons;

	/* RX'd FIS area */
	__le32 *rx_fis;
	dma_addr_t rx_fis_dma;

	/* DMA command header slots */
	struct mvs_cmd_hdr *slot;
	dma_addr_t slot_dma;

	const struct mvs_chip_info *chip;

	u8 tags[MVS_SLOTS];
	struct mvs_slot_info slot_info[MVS_SLOTS];
				/* further per-slot information */
	struct mvs_phy phy[MVS_MAX_PHYS];
	struct mvs_port port[MVS_MAX_PHYS];
#ifdef MVS_USE_TASKLET
	struct tasklet_struct tasklet;
#endif
};

int mvs_phy_control(struct asd_sas_phy *sas_phy, enum phy_func func,
			void *funcdata);
int mvs_slave_configure(struct scsi_device *sdev);
void mvs_scan_start(struct Scsi_Host *shost);
int mvs_scan_finished(struct Scsi_Host *shost, unsigned long time);
int mvs_task_exec(struct sas_task *task, const int num, gfp_t gfp_flags);
int mvs_task_abort(struct sas_task *task);
void mvs_port_formed(struct asd_sas_phy *sas_phy);
int mvs_I_T_nexus_reset(struct domain_device *dev);
void mvs_int_full(struct mvs_info *mvi);
void mvs_tag_init(struct mvs_info *mvi);
int mvs_nvram_read(struct mvs_info *mvi, u32 addr, void *buf, u32 buflen);
int __devinit mvs_hw_init(struct mvs_info *mvi);
void __devinit mvs_print_info(struct mvs_info *mvi);
void mvs_hba_interrupt_enable(struct mvs_info *mvi);
void mvs_hba_interrupt_disable(struct mvs_info *mvi);
void mvs_detect_porttype(struct mvs_info *mvi, int i);
u8 mvs_assign_reg_set(struct mvs_info *mvi, struct mvs_port *port);
void mvs_enable_xmt(struct mvs_info *mvi, int PhyId);
void __devinit mvs_phy_hacks(struct mvs_info *mvi);
void mvs_free_reg_set(struct mvs_info *mvi, struct mvs_port *port);

#endif
