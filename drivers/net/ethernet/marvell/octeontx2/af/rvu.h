/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RVU_H
#define RVU_H

#include "rvu_struct.h"
#include "mbox.h"

/* PCI device IDs */
#define	PCI_DEVID_OCTEONTX2_RVU_AF		0xA065

/* PCI BAR nos */
#define	PCI_AF_REG_BAR_NUM			0
#define	PCI_PF_REG_BAR_NUM			2
#define	PCI_MBOX_BAR_NUM			4

#define NAME_SIZE				32

/* PF_FUNC */
#define RVU_PFVF_PF_SHIFT	10
#define RVU_PFVF_PF_MASK	0x3F
#define RVU_PFVF_FUNC_SHIFT	0
#define RVU_PFVF_FUNC_MASK	0x3FF

struct rvu_work {
	struct	work_struct work;
	struct	rvu *rvu;
};

struct rsrc_bmap {
	unsigned long *bmap;	/* Pointer to resource bitmap */
	u16  max;		/* Max resource id or count */
};

struct rvu_block {
	struct rsrc_bmap lf;
	u16  *fn_map; /* LF to pcifunc mapping */
	bool multislot;
	bool implemented;
	u8   addr;  /* RVU_BLOCK_ADDR_E */
	u8   type;  /* RVU_BLOCK_TYPE_E */
	u8   lfshift;
	u64  lookup_reg;
	u64  pf_lfcnt_reg;
	u64  vf_lfcnt_reg;
	u64  lfcfg_reg;
	u64  msixcfg_reg;
	u64  lfreset_reg;
	unsigned char name[NAME_SIZE];
};

/* Structure for per RVU func info ie PF/VF */
struct rvu_pfvf {
	bool		npalf; /* Only one NPALF per RVU_FUNC */
	bool		nixlf; /* Only one NIXLF per RVU_FUNC */
	u16		sso;
	u16		ssow;
	u16		cptlfs;
	u16		timlfs;

	/* Block LF's MSIX vector info */
	struct rsrc_bmap msix;      /* Bitmap for MSIX vector alloc */
#define MSIX_BLKLF(blkaddr, lf) (((blkaddr) << 8) | ((lf) & 0xFF))
	u16		 *msix_lfmap; /* Vector to block LF mapping */
};

struct rvu_hwinfo {
	u8	total_pfs;   /* MAX RVU PFs HW supports */
	u16	total_vfs;   /* Max RVU VFs HW supports */
	u16	max_vfs_per_pf; /* Max VFs that can be attached to a PF */

	struct rvu_block block[BLK_COUNT]; /* Block info */
};

struct rvu {
	void __iomem		*afreg_base;
	void __iomem		*pfreg_base;
	struct pci_dev		*pdev;
	struct device		*dev;
	struct rvu_hwinfo       *hw;
	struct rvu_pfvf		*pf;
	struct rvu_pfvf		*hwvf;
	spinlock_t		rsrc_lock; /* Serialize resource alloc/free */

	/* Mbox */
	struct otx2_mbox	mbox;
	struct rvu_work		*mbox_wrk;
	struct workqueue_struct *mbox_wq;

	/* MSI-X */
	u16			num_vec;
	char			*irq_name;
	bool			*irq_allocated;
	dma_addr_t		msix_base_iova;

	/* CGX */
#define PF_CGXMAP_BASE		1 /* PF 0 is reserved for RVU PF */
	u8			cgx_mapped_pfs;
	u8			cgx_cnt; /* available cgx ports */
	u8			*pf2cgxlmac_map; /* pf to cgx_lmac map */
	u16			*cgxlmac2pf_map; /* bitmap of mapped pfs for
						  * every cgx lmac port
						  */
	void			**cgx_idmap; /* cgx id to cgx data map table */
	struct			work_struct cgx_evh_work;
	struct			workqueue_struct *cgx_evh_wq;
	spinlock_t		cgx_evq_lock; /* cgx event queue lock */
	struct list_head	cgx_evq_head; /* cgx event queue head */
};

static inline void rvu_write64(struct rvu *rvu, u64 block, u64 offset, u64 val)
{
	writeq(val, rvu->afreg_base + ((block << 28) | offset));
}

static inline u64 rvu_read64(struct rvu *rvu, u64 block, u64 offset)
{
	return readq(rvu->afreg_base + ((block << 28) | offset));
}

static inline void rvupf_write64(struct rvu *rvu, u64 offset, u64 val)
{
	writeq(val, rvu->pfreg_base + offset);
}

static inline u64 rvupf_read64(struct rvu *rvu, u64 offset)
{
	return readq(rvu->pfreg_base + offset);
}

/* Function Prototypes
 * RVU
 */

int rvu_alloc_bitmap(struct rsrc_bmap *rsrc);
int rvu_alloc_rsrc(struct rsrc_bmap *rsrc);
void rvu_free_rsrc(struct rsrc_bmap *rsrc, int id);
int rvu_rsrc_free_count(struct rsrc_bmap *rsrc);
int rvu_get_pf(u16 pcifunc);
struct rvu_pfvf *rvu_get_pfvf(struct rvu *rvu, int pcifunc);
void rvu_get_pf_numvfs(struct rvu *rvu, int pf, int *numvfs, int *hwvf);
bool is_block_implemented(struct rvu_hwinfo *hw, int blkaddr);
int rvu_get_lf(struct rvu *rvu, struct rvu_block *block, u16 pcifunc, u16 slot);
int rvu_get_blkaddr(struct rvu *rvu, int blktype, u16 pcifunc);
int rvu_poll_reg(struct rvu *rvu, u64 block, u64 offset, u64 mask, bool zero);

/* CGX APIs */
static inline bool is_pf_cgxmapped(struct rvu *rvu, u8 pf)
{
	return (pf >= PF_CGXMAP_BASE && pf <= rvu->cgx_mapped_pfs);
}

static inline void rvu_get_cgx_lmac_id(u8 map, u8 *cgx_id, u8 *lmac_id)
{
	*cgx_id = (map >> 4) & 0xF;
	*lmac_id = (map & 0xF);
}

int rvu_cgx_probe(struct rvu *rvu);
void rvu_cgx_wq_destroy(struct rvu *rvu);
int rvu_cgx_config_rxtx(struct rvu *rvu, u16 pcifunc, bool start);
int rvu_mbox_handler_CGX_START_RXTX(struct rvu *rvu, struct msg_req *req,
				    struct msg_rsp *rsp);
int rvu_mbox_handler_CGX_STOP_RXTX(struct rvu *rvu, struct msg_req *req,
				   struct msg_rsp *rsp);
int rvu_mbox_handler_CGX_STATS(struct rvu *rvu, struct msg_req *req,
			       struct cgx_stats_rsp *rsp);
#endif /* RVU_H */
