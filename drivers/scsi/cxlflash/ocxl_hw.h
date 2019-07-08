/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CXL Flash Device Driver
 *
 * Written by: Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *	       Uma Krishnan <ukrishn@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2018 IBM Corporation
 */

#define OCXL_MAX_IRQS	4	/* Max interrupts per process */

struct ocxlflash_irqs {
	int hwirq;
	u32 virq;
	u64 ptrig;
	void __iomem *vtrig;
};

/* OCXL hardware AFU associated with the host */
struct ocxl_hw_afu {
	struct ocxlflash_context *ocxl_ctx; /* Host context */
	struct pci_dev *pdev;		/* PCI device */
	struct device *dev;		/* Generic device */
	bool perst_same_image;		/* Same image loaded on perst */

	struct ocxl_fn_config fcfg;	/* DVSEC config of the function */
	struct ocxl_afu_config acfg;	/* AFU configuration data */

	int fn_actag_base;		/* Function acTag base */
	int fn_actag_enabled;		/* Function acTag number enabled */
	int afu_actag_base;		/* AFU acTag base */
	int afu_actag_enabled;		/* AFU acTag number enabled */

	phys_addr_t ppmmio_phys;	/* Per process MMIO space */
	phys_addr_t gmmio_phys;		/* Global AFU MMIO space */
	void __iomem *gmmio_virt;	/* Global MMIO map */

	void *link_token;		/* Link token for the SPA */
	struct idr idr;			/* IDR to manage contexts */
	int max_pasid;			/* Maximum number of contexts */
	bool is_present;		/* Function has AFUs defined */
};

enum ocxlflash_ctx_state {
	CLOSED,
	OPENED,
	STARTED
};

struct ocxlflash_context {
	struct ocxl_hw_afu *hw_afu;	/* HW AFU back pointer */
	struct address_space *mapping;	/* Mapping for pseudo filesystem */
	bool master;			/* Whether this is a master context */
	int pe;				/* Process element */

	phys_addr_t psn_phys;		/* Process mapping */
	u64 psn_size;			/* Process mapping size */

	spinlock_t slock;		/* Protects irq/fault/event updates */
	wait_queue_head_t wq;		/* Wait queue for poll and interrupts */
	struct mutex state_mutex;	/* Mutex to update context state */
	enum ocxlflash_ctx_state state;	/* Context state */

	struct ocxlflash_irqs *irqs;	/* Pointer to array of structures */
	int num_irqs;			/* Number of interrupts */
	bool pending_irq;		/* Pending interrupt on the context */
	ulong irq_bitmap;		/* Bits indicating pending irq num */

	u64 fault_addr;			/* Address that triggered the fault */
	u64 fault_dsisr;		/* Value of dsisr register at fault */
	bool pending_fault;		/* Pending translation fault */
};
