/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAAM/SEC 4.x driver backend
 * Private/internal definitions between modules
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 * Copyright 2019 NXP
 */

#ifndef INTERN_H
#define INTERN_H

#include "ctrl.h"
#include <crypto/engine.h>

/* Currently comes from Kconfig param as a ^2 (driver-required) */
#define JOBR_DEPTH (1 << CONFIG_CRYPTO_DEV_FSL_CAAM_RINGSIZE)

/* Kconfig params for interrupt coalescing if selected (else zero) */
#ifdef CONFIG_CRYPTO_DEV_FSL_CAAM_INTC
#define JOBR_INTC JRCFG_ICEN
#define JOBR_INTC_TIME_THLD CONFIG_CRYPTO_DEV_FSL_CAAM_INTC_TIME_THLD
#define JOBR_INTC_COUNT_THLD CONFIG_CRYPTO_DEV_FSL_CAAM_INTC_COUNT_THLD
#else
#define JOBR_INTC 0
#define JOBR_INTC_TIME_THLD 0
#define JOBR_INTC_COUNT_THLD 0
#endif

/*
 * Storage for tracking each in-process entry moving across a ring
 * Each entry on an output ring needs one of these
 */
struct caam_jrentry_info {
	void (*callbk)(struct device *dev, u32 *desc, u32 status, void *arg);
	void *cbkarg;	/* Argument per ring entry */
	u32 *desc_addr_virt;	/* Stored virt addr for postprocessing */
	dma_addr_t desc_addr_dma;	/* Stored bus addr for done matching */
	u32 desc_size;	/* Stored size for postprocessing, header derived */
};

/* Private sub-storage for a single JobR */
struct caam_drv_private_jr {
	struct list_head	list_node;	/* Job Ring device list */
	struct device		*dev;
	int ridx;
	struct caam_job_ring __iomem *rregs;	/* JobR's register space */
	struct tasklet_struct irqtask;
	int irq;			/* One per queue */

	/* Number of scatterlist crypt transforms active on the JobR */
	atomic_t tfm_count ____cacheline_aligned;

	/* Job ring info */
	struct caam_jrentry_info *entinfo;	/* Alloc'ed 1 per ring entry */
	spinlock_t inplock ____cacheline_aligned; /* Input ring index lock */
	u32 inpring_avail;	/* Number of free entries in input ring */
	int head;			/* entinfo (s/w ring) head index */
	void *inpring;			/* Base of input ring, alloc
					 * DMA-safe */
	int out_ring_read_index;	/* Output index "tail" */
	int tail;			/* entinfo (s/w ring) tail index */
	void *outring;			/* Base of output ring, DMA-safe */
	struct crypto_engine *engine;
};

/*
 * Driver-private storage for a single CAAM block instance
 */
struct caam_drv_private {
	/* Physical-presence section */
	struct caam_ctrl __iomem *ctrl; /* controller region */
	struct caam_deco __iomem *deco; /* DECO/CCB views */
	struct caam_assurance __iomem *assure;
	struct caam_queue_if __iomem *qi; /* QI control region */
	struct caam_job_ring __iomem *jr[4];	/* JobR's register space */

	struct iommu_domain *domain;

	/*
	 * Detected geometry block. Filled in from device tree if powerpc,
	 * or from register-based version detection code
	 */
	u8 total_jobrs;		/* Total Job Rings in device */
	u8 qi_present;		/* Nonzero if QI present in device */
	u8 mc_en;		/* Nonzero if MC f/w is active */
	int secvio_irq;		/* Security violation interrupt number */
	int virt_en;		/* Virtualization enabled in CAAM */
	int era;		/* CAAM Era (internal HW revision) */

#define	RNG4_MAX_HANDLES 2
	/* RNG4 block */
	u32 rng4_sh_init;	/* This bitmap shows which of the State
				   Handles of the RNG4 block are initialized
				   by this driver */

	struct clk_bulk_data *clks;
	int num_clks;
	/*
	 * debugfs entries for developer view into driver/device
	 * variables at runtime.
	 */
#ifdef CONFIG_DEBUG_FS
	struct dentry *ctl; /* controller dir */
	struct debugfs_blob_wrapper ctl_kek_wrap, ctl_tkek_wrap, ctl_tdsk_wrap;
#endif
};

#ifdef CONFIG_CRYPTO_DEV_FSL_CAAM_CRYPTO_API

int caam_algapi_init(struct device *dev);
void caam_algapi_exit(void);

#else

static inline int caam_algapi_init(struct device *dev)
{
	return 0;
}

static inline void caam_algapi_exit(void)
{
}

#endif /* CONFIG_CRYPTO_DEV_FSL_CAAM_CRYPTO_API */

#ifdef CONFIG_CRYPTO_DEV_FSL_CAAM_AHASH_API

int caam_algapi_hash_init(struct device *dev);
void caam_algapi_hash_exit(void);

#else

static inline int caam_algapi_hash_init(struct device *dev)
{
	return 0;
}

static inline void caam_algapi_hash_exit(void)
{
}

#endif /* CONFIG_CRYPTO_DEV_FSL_CAAM_AHASH_API */

#ifdef CONFIG_CRYPTO_DEV_FSL_CAAM_PKC_API

int caam_pkc_init(struct device *dev);
void caam_pkc_exit(void);

#else

static inline int caam_pkc_init(struct device *dev)
{
	return 0;
}

static inline void caam_pkc_exit(void)
{
}

#endif /* CONFIG_CRYPTO_DEV_FSL_CAAM_PKC_API */

#ifdef CONFIG_CRYPTO_DEV_FSL_CAAM_RNG_API

int caam_rng_init(struct device *dev);
void caam_rng_exit(void);

#else

static inline int caam_rng_init(struct device *dev)
{
	return 0;
}

static inline void caam_rng_exit(void)
{
}

#endif /* CONFIG_CRYPTO_DEV_FSL_CAAM_RNG_API */

#ifdef CONFIG_CAAM_QI

int caam_qi_algapi_init(struct device *dev);
void caam_qi_algapi_exit(void);

#else

static inline int caam_qi_algapi_init(struct device *dev)
{
	return 0;
}

static inline void caam_qi_algapi_exit(void)
{
}

#endif /* CONFIG_CAAM_QI */

#ifdef CONFIG_DEBUG_FS
static int caam_debugfs_u64_get(void *data, u64 *val)
{
	*val = caam64_to_cpu(*(u64 *)data);
	return 0;
}

static int caam_debugfs_u32_get(void *data, u64 *val)
{
	*val = caam32_to_cpu(*(u32 *)data);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(caam_fops_u32_ro, caam_debugfs_u32_get, NULL, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(caam_fops_u64_ro, caam_debugfs_u64_get, NULL, "%llu\n");
#endif

static inline u64 caam_get_dma_mask(struct device *dev)
{
	struct device_node *nprop = dev->of_node;

	if (caam_ptr_sz != sizeof(u64))
		return DMA_BIT_MASK(32);

	if (caam_dpaa2)
		return DMA_BIT_MASK(49);

	if (of_device_is_compatible(nprop, "fsl,sec-v5.0-job-ring") ||
	    of_device_is_compatible(nprop, "fsl,sec-v5.0"))
		return DMA_BIT_MASK(40);

	return DMA_BIT_MASK(36);
}


#endif /* INTERN_H */
