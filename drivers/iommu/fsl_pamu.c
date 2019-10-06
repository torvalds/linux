// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 */

#define pr_fmt(fmt)    "fsl-pamu: %s: " fmt, __func__

#include "fsl_pamu.h"

#include <linux/fsl/guts.h>
#include <linux/interrupt.h>
#include <linux/genalloc.h>

#include <asm/mpc85xx.h>

/* define indexes for each operation mapping scenario */
#define OMI_QMAN        0x00
#define OMI_FMAN        0x01
#define OMI_QMAN_PRIV   0x02
#define OMI_CAAM        0x03

#define make64(high, low) (((u64)(high) << 32) | (low))

struct pamu_isr_data {
	void __iomem *pamu_reg_base;	/* Base address of PAMU regs */
	unsigned int count;		/* The number of PAMUs */
};

static struct paace *ppaact;
static struct paace *spaact;

static bool probed;			/* Has PAMU been probed? */

/*
 * Table for matching compatible strings, for device tree
 * guts node, for QorIQ SOCs.
 * "fsl,qoriq-device-config-2.0" corresponds to T4 & B4
 * SOCs. For the older SOCs "fsl,qoriq-device-config-1.0"
 * string would be used.
 */
static const struct of_device_id guts_device_ids[] = {
	{ .compatible = "fsl,qoriq-device-config-1.0", },
	{ .compatible = "fsl,qoriq-device-config-2.0", },
	{}
};

/*
 * Table for matching compatible strings, for device tree
 * L3 cache controller node.
 * "fsl,t4240-l3-cache-controller" corresponds to T4,
 * "fsl,b4860-l3-cache-controller" corresponds to B4 &
 * "fsl,p4080-l3-cache-controller" corresponds to other,
 * SOCs.
 */
static const struct of_device_id l3_device_ids[] = {
	{ .compatible = "fsl,t4240-l3-cache-controller", },
	{ .compatible = "fsl,b4860-l3-cache-controller", },
	{ .compatible = "fsl,p4080-l3-cache-controller", },
	{}
};

/* maximum subwindows permitted per liodn */
static u32 max_subwindow_count;

/* Pool for fspi allocation */
static struct gen_pool *spaace_pool;

/**
 * pamu_get_max_subwin_cnt() - Return the maximum supported
 * subwindow count per liodn.
 *
 */
u32 pamu_get_max_subwin_cnt(void)
{
	return max_subwindow_count;
}

/**
 * pamu_get_ppaace() - Return the primary PACCE
 * @liodn: liodn PAACT index for desired PAACE
 *
 * Returns the ppace pointer upon success else return
 * null.
 */
static struct paace *pamu_get_ppaace(int liodn)
{
	if (!ppaact || liodn >= PAACE_NUMBER_ENTRIES) {
		pr_debug("PPAACT doesn't exist\n");
		return NULL;
	}

	return &ppaact[liodn];
}

/**
 * pamu_enable_liodn() - Set valid bit of PACCE
 * @liodn: liodn PAACT index for desired PAACE
 *
 * Returns 0 upon success else error code < 0 returned
 */
int pamu_enable_liodn(int liodn)
{
	struct paace *ppaace;

	ppaace = pamu_get_ppaace(liodn);
	if (!ppaace) {
		pr_debug("Invalid primary paace entry\n");
		return -ENOENT;
	}

	if (!get_bf(ppaace->addr_bitfields, PPAACE_AF_WSE)) {
		pr_debug("liodn %d not configured\n", liodn);
		return -EINVAL;
	}

	/* Ensure that all other stores to the ppaace complete first */
	mb();

	set_bf(ppaace->addr_bitfields, PAACE_AF_V, PAACE_V_VALID);
	mb();

	return 0;
}

/**
 * pamu_disable_liodn() - Clears valid bit of PACCE
 * @liodn: liodn PAACT index for desired PAACE
 *
 * Returns 0 upon success else error code < 0 returned
 */
int pamu_disable_liodn(int liodn)
{
	struct paace *ppaace;

	ppaace = pamu_get_ppaace(liodn);
	if (!ppaace) {
		pr_debug("Invalid primary paace entry\n");
		return -ENOENT;
	}

	set_bf(ppaace->addr_bitfields, PAACE_AF_V, PAACE_V_INVALID);
	mb();

	return 0;
}

/* Derive the window size encoding for a particular PAACE entry */
static unsigned int map_addrspace_size_to_wse(phys_addr_t addrspace_size)
{
	/* Bug if not a power of 2 */
	BUG_ON(addrspace_size & (addrspace_size - 1));

	/* window size is 2^(WSE+1) bytes */
	return fls64(addrspace_size) - 2;
}

/* Derive the PAACE window count encoding for the subwindow count */
static unsigned int map_subwindow_cnt_to_wce(u32 subwindow_cnt)
{
	/* window count is 2^(WCE+1) bytes */
	return __ffs(subwindow_cnt) - 1;
}

/*
 * Set the PAACE type as primary and set the coherency required domain
 * attribute
 */
static void pamu_init_ppaace(struct paace *ppaace)
{
	set_bf(ppaace->addr_bitfields, PAACE_AF_PT, PAACE_PT_PRIMARY);

	set_bf(ppaace->domain_attr.to_host.coherency_required, PAACE_DA_HOST_CR,
	       PAACE_M_COHERENCE_REQ);
}

/*
 * Set the PAACE type as secondary and set the coherency required domain
 * attribute.
 */
static void pamu_init_spaace(struct paace *spaace)
{
	set_bf(spaace->addr_bitfields, PAACE_AF_PT, PAACE_PT_SECONDARY);
	set_bf(spaace->domain_attr.to_host.coherency_required, PAACE_DA_HOST_CR,
	       PAACE_M_COHERENCE_REQ);
}

/*
 * Return the spaace (corresponding to the secondary window index)
 * for a particular ppaace.
 */
static struct paace *pamu_get_spaace(struct paace *paace, u32 wnum)
{
	u32 subwin_cnt;
	struct paace *spaace = NULL;

	subwin_cnt = 1UL << (get_bf(paace->impl_attr, PAACE_IA_WCE) + 1);

	if (wnum < subwin_cnt)
		spaace = &spaact[paace->fspi + wnum];
	else
		pr_debug("secondary paace out of bounds\n");

	return spaace;
}

/**
 * pamu_get_fspi_and_allocate() - Allocates fspi index and reserves subwindows
 *                                required for primary PAACE in the secondary
 *                                PAACE table.
 * @subwin_cnt: Number of subwindows to be reserved.
 *
 * A PPAACE entry may have a number of associated subwindows. A subwindow
 * corresponds to a SPAACE entry in the SPAACT table. Each PAACE entry stores
 * the index (fspi) of the first SPAACE entry in the SPAACT table. This
 * function returns the index of the first SPAACE entry. The remaining
 * SPAACE entries are reserved contiguously from that index.
 *
 * Returns a valid fspi index in the range of 0 - SPAACE_NUMBER_ENTRIES on success.
 * If no SPAACE entry is available or the allocator can not reserve the required
 * number of contiguous entries function returns ULONG_MAX indicating a failure.
 *
 */
static unsigned long pamu_get_fspi_and_allocate(u32 subwin_cnt)
{
	unsigned long spaace_addr;

	spaace_addr = gen_pool_alloc(spaace_pool, subwin_cnt * sizeof(struct paace));
	if (!spaace_addr)
		return ULONG_MAX;

	return (spaace_addr - (unsigned long)spaact) / (sizeof(struct paace));
}

/* Release the subwindows reserved for a particular LIODN */
void pamu_free_subwins(int liodn)
{
	struct paace *ppaace;
	u32 subwin_cnt, size;

	ppaace = pamu_get_ppaace(liodn);
	if (!ppaace) {
		pr_debug("Invalid liodn entry\n");
		return;
	}

	if (get_bf(ppaace->addr_bitfields, PPAACE_AF_MW)) {
		subwin_cnt = 1UL << (get_bf(ppaace->impl_attr, PAACE_IA_WCE) + 1);
		size = (subwin_cnt - 1) * sizeof(struct paace);
		gen_pool_free(spaace_pool, (unsigned long)&spaact[ppaace->fspi], size);
		set_bf(ppaace->addr_bitfields, PPAACE_AF_MW, 0);
	}
}

/*
 * Function used for updating stash destination for the coressponding
 * LIODN.
 */
int  pamu_update_paace_stash(int liodn, u32 subwin, u32 value)
{
	struct paace *paace;

	paace = pamu_get_ppaace(liodn);
	if (!paace) {
		pr_debug("Invalid liodn entry\n");
		return -ENOENT;
	}
	if (subwin) {
		paace = pamu_get_spaace(paace, subwin - 1);
		if (!paace)
			return -ENOENT;
	}
	set_bf(paace->impl_attr, PAACE_IA_CID, value);

	mb();

	return 0;
}

/* Disable a subwindow corresponding to the LIODN */
int pamu_disable_spaace(int liodn, u32 subwin)
{
	struct paace *paace;

	paace = pamu_get_ppaace(liodn);
	if (!paace) {
		pr_debug("Invalid liodn entry\n");
		return -ENOENT;
	}
	if (subwin) {
		paace = pamu_get_spaace(paace, subwin - 1);
		if (!paace)
			return -ENOENT;
		set_bf(paace->addr_bitfields, PAACE_AF_V, PAACE_V_INVALID);
	} else {
		set_bf(paace->addr_bitfields, PAACE_AF_AP,
		       PAACE_AP_PERMS_DENIED);
	}

	mb();

	return 0;
}

/**
 * pamu_config_paace() - Sets up PPAACE entry for specified liodn
 *
 * @liodn: Logical IO device number
 * @win_addr: starting address of DSA window
 * @win-size: size of DSA window
 * @omi: Operation mapping index -- if ~omi == 0 then omi not defined
 * @rpn: real (true physical) page number
 * @stashid: cache stash id for associated cpu -- if ~stashid == 0 then
 *	     stashid not defined
 * @snoopid: snoop id for hardware coherency -- if ~snoopid == 0 then
 *	     snoopid not defined
 * @subwin_cnt: number of sub-windows
 * @prot: window permissions
 *
 * Returns 0 upon success else error code < 0 returned
 */
int pamu_config_ppaace(int liodn, phys_addr_t win_addr, phys_addr_t win_size,
		       u32 omi, unsigned long rpn, u32 snoopid, u32 stashid,
		       u32 subwin_cnt, int prot)
{
	struct paace *ppaace;
	unsigned long fspi;

	if ((win_size & (win_size - 1)) || win_size < PAMU_PAGE_SIZE) {
		pr_debug("window size too small or not a power of two %pa\n",
			 &win_size);
		return -EINVAL;
	}

	if (win_addr & (win_size - 1)) {
		pr_debug("window address is not aligned with window size\n");
		return -EINVAL;
	}

	ppaace = pamu_get_ppaace(liodn);
	if (!ppaace)
		return -ENOENT;

	/* window size is 2^(WSE+1) bytes */
	set_bf(ppaace->addr_bitfields, PPAACE_AF_WSE,
	       map_addrspace_size_to_wse(win_size));

	pamu_init_ppaace(ppaace);

	ppaace->wbah = win_addr >> (PAMU_PAGE_SHIFT + 20);
	set_bf(ppaace->addr_bitfields, PPAACE_AF_WBAL,
	       (win_addr >> PAMU_PAGE_SHIFT));

	/* set up operation mapping if it's configured */
	if (omi < OME_NUMBER_ENTRIES) {
		set_bf(ppaace->impl_attr, PAACE_IA_OTM, PAACE_OTM_INDEXED);
		ppaace->op_encode.index_ot.omi = omi;
	} else if (~omi != 0) {
		pr_debug("bad operation mapping index: %d\n", omi);
		return -EINVAL;
	}

	/* configure stash id */
	if (~stashid != 0)
		set_bf(ppaace->impl_attr, PAACE_IA_CID, stashid);

	/* configure snoop id */
	if (~snoopid != 0)
		ppaace->domain_attr.to_host.snpid = snoopid;

	if (subwin_cnt) {
		/* The first entry is in the primary PAACE instead */
		fspi = pamu_get_fspi_and_allocate(subwin_cnt - 1);
		if (fspi == ULONG_MAX) {
			pr_debug("spaace indexes exhausted\n");
			return -EINVAL;
		}

		/* window count is 2^(WCE+1) bytes */
		set_bf(ppaace->impl_attr, PAACE_IA_WCE,
		       map_subwindow_cnt_to_wce(subwin_cnt));
		set_bf(ppaace->addr_bitfields, PPAACE_AF_MW, 0x1);
		ppaace->fspi = fspi;
	} else {
		set_bf(ppaace->impl_attr, PAACE_IA_ATM, PAACE_ATM_WINDOW_XLATE);
		ppaace->twbah = rpn >> 20;
		set_bf(ppaace->win_bitfields, PAACE_WIN_TWBAL, rpn);
		set_bf(ppaace->addr_bitfields, PAACE_AF_AP, prot);
		set_bf(ppaace->impl_attr, PAACE_IA_WCE, 0);
		set_bf(ppaace->addr_bitfields, PPAACE_AF_MW, 0);
	}
	mb();

	return 0;
}

/**
 * pamu_config_spaace() - Sets up SPAACE entry for specified subwindow
 *
 * @liodn:  Logical IO device number
 * @subwin_cnt:  number of sub-windows associated with dma-window
 * @subwin: subwindow index
 * @subwin_size: size of subwindow
 * @omi: Operation mapping index
 * @rpn: real (true physical) page number
 * @snoopid: snoop id for hardware coherency -- if ~snoopid == 0 then
 *			  snoopid not defined
 * @stashid: cache stash id for associated cpu
 * @enable: enable/disable subwindow after reconfiguration
 * @prot: sub window permissions
 *
 * Returns 0 upon success else error code < 0 returned
 */
int pamu_config_spaace(int liodn, u32 subwin_cnt, u32 subwin,
		       phys_addr_t subwin_size, u32 omi, unsigned long rpn,
		       u32 snoopid, u32 stashid, int enable, int prot)
{
	struct paace *paace;

	/* setup sub-windows */
	if (!subwin_cnt) {
		pr_debug("Invalid subwindow count\n");
		return -EINVAL;
	}

	paace = pamu_get_ppaace(liodn);
	if (subwin > 0 && subwin < subwin_cnt && paace) {
		paace = pamu_get_spaace(paace, subwin - 1);

		if (paace && !(paace->addr_bitfields & PAACE_V_VALID)) {
			pamu_init_spaace(paace);
			set_bf(paace->addr_bitfields, SPAACE_AF_LIODN, liodn);
		}
	}

	if (!paace) {
		pr_debug("Invalid liodn entry\n");
		return -ENOENT;
	}

	if ((subwin_size & (subwin_size - 1)) || subwin_size < PAMU_PAGE_SIZE) {
		pr_debug("subwindow size out of range, or not a power of 2\n");
		return -EINVAL;
	}

	if (rpn == ULONG_MAX) {
		pr_debug("real page number out of range\n");
		return -EINVAL;
	}

	/* window size is 2^(WSE+1) bytes */
	set_bf(paace->win_bitfields, PAACE_WIN_SWSE,
	       map_addrspace_size_to_wse(subwin_size));

	set_bf(paace->impl_attr, PAACE_IA_ATM, PAACE_ATM_WINDOW_XLATE);
	paace->twbah = rpn >> 20;
	set_bf(paace->win_bitfields, PAACE_WIN_TWBAL, rpn);
	set_bf(paace->addr_bitfields, PAACE_AF_AP, prot);

	/* configure snoop id */
	if (~snoopid != 0)
		paace->domain_attr.to_host.snpid = snoopid;

	/* set up operation mapping if it's configured */
	if (omi < OME_NUMBER_ENTRIES) {
		set_bf(paace->impl_attr, PAACE_IA_OTM, PAACE_OTM_INDEXED);
		paace->op_encode.index_ot.omi = omi;
	} else if (~omi != 0) {
		pr_debug("bad operation mapping index: %d\n", omi);
		return -EINVAL;
	}

	if (~stashid != 0)
		set_bf(paace->impl_attr, PAACE_IA_CID, stashid);

	smp_wmb();

	if (enable)
		set_bf(paace->addr_bitfields, PAACE_AF_V, PAACE_V_VALID);

	mb();

	return 0;
}

/**
 * get_ome_index() - Returns the index in the operation mapping table
 *                   for device.
 * @*omi_index: pointer for storing the index value
 *
 */
void get_ome_index(u32 *omi_index, struct device *dev)
{
	if (of_device_is_compatible(dev->of_node, "fsl,qman-portal"))
		*omi_index = OMI_QMAN;
	if (of_device_is_compatible(dev->of_node, "fsl,qman"))
		*omi_index = OMI_QMAN_PRIV;
}

/**
 * get_stash_id - Returns stash destination id corresponding to a
 *                cache type and vcpu.
 * @stash_dest_hint: L1, L2 or L3
 * @vcpu: vpcu target for a particular cache type.
 *
 * Returs stash on success or ~(u32)0 on failure.
 *
 */
u32 get_stash_id(u32 stash_dest_hint, u32 vcpu)
{
	const u32 *prop;
	struct device_node *node;
	u32 cache_level;
	int len, found = 0;
	int i;

	/* Fastpath, exit early if L3/CPC cache is target for stashing */
	if (stash_dest_hint == PAMU_ATTR_CACHE_L3) {
		node = of_find_matching_node(NULL, l3_device_ids);
		if (node) {
			prop = of_get_property(node, "cache-stash-id", NULL);
			if (!prop) {
				pr_debug("missing cache-stash-id at %pOF\n",
					 node);
				of_node_put(node);
				return ~(u32)0;
			}
			of_node_put(node);
			return be32_to_cpup(prop);
		}
		return ~(u32)0;
	}

	for_each_of_cpu_node(node) {
		prop = of_get_property(node, "reg", &len);
		for (i = 0; i < len / sizeof(u32); i++) {
			if (be32_to_cpup(&prop[i]) == vcpu) {
				found = 1;
				goto found_cpu_node;
			}
		}
	}
found_cpu_node:

	/* find the hwnode that represents the cache */
	for (cache_level = PAMU_ATTR_CACHE_L1; (cache_level < PAMU_ATTR_CACHE_L3) && found; cache_level++) {
		if (stash_dest_hint == cache_level) {
			prop = of_get_property(node, "cache-stash-id", NULL);
			if (!prop) {
				pr_debug("missing cache-stash-id at %pOF\n",
					 node);
				of_node_put(node);
				return ~(u32)0;
			}
			of_node_put(node);
			return be32_to_cpup(prop);
		}

		prop = of_get_property(node, "next-level-cache", NULL);
		if (!prop) {
			pr_debug("can't find next-level-cache at %pOF\n", node);
			of_node_put(node);
			return ~(u32)0;  /* can't traverse any further */
		}
		of_node_put(node);

		/* advance to next node in cache hierarchy */
		node = of_find_node_by_phandle(*prop);
		if (!node) {
			pr_debug("Invalid node for cache hierarchy\n");
			return ~(u32)0;
		}
	}

	pr_debug("stash dest not found for %d on vcpu %d\n",
		 stash_dest_hint, vcpu);
	return ~(u32)0;
}

/* Identify if the PAACT table entry belongs to QMAN, BMAN or QMAN Portal */
#define QMAN_PAACE 1
#define QMAN_PORTAL_PAACE 2
#define BMAN_PAACE 3

/**
 * Setup operation mapping and stash destinations for QMAN and QMAN portal.
 * Memory accesses to QMAN and BMAN private memory need not be coherent, so
 * clear the PAACE entry coherency attribute for them.
 */
static void setup_qbman_paace(struct paace *ppaace, int  paace_type)
{
	switch (paace_type) {
	case QMAN_PAACE:
		set_bf(ppaace->impl_attr, PAACE_IA_OTM, PAACE_OTM_INDEXED);
		ppaace->op_encode.index_ot.omi = OMI_QMAN_PRIV;
		/* setup QMAN Private data stashing for the L3 cache */
		set_bf(ppaace->impl_attr, PAACE_IA_CID, get_stash_id(PAMU_ATTR_CACHE_L3, 0));
		set_bf(ppaace->domain_attr.to_host.coherency_required, PAACE_DA_HOST_CR,
		       0);
		break;
	case QMAN_PORTAL_PAACE:
		set_bf(ppaace->impl_attr, PAACE_IA_OTM, PAACE_OTM_INDEXED);
		ppaace->op_encode.index_ot.omi = OMI_QMAN;
		/* Set DQRR and Frame stashing for the L3 cache */
		set_bf(ppaace->impl_attr, PAACE_IA_CID, get_stash_id(PAMU_ATTR_CACHE_L3, 0));
		break;
	case BMAN_PAACE:
		set_bf(ppaace->domain_attr.to_host.coherency_required, PAACE_DA_HOST_CR,
		       0);
		break;
	}
}

/**
 * Setup the operation mapping table for various devices. This is a static
 * table where each table index corresponds to a particular device. PAMU uses
 * this table to translate device transaction to appropriate corenet
 * transaction.
 */
static void setup_omt(struct ome *omt)
{
	struct ome *ome;

	/* Configure OMI_QMAN */
	ome = &omt[OMI_QMAN];

	ome->moe[IOE_READ_IDX] = EOE_VALID | EOE_READ;
	ome->moe[IOE_EREAD0_IDX] = EOE_VALID | EOE_RSA;
	ome->moe[IOE_WRITE_IDX] = EOE_VALID | EOE_WRITE;
	ome->moe[IOE_EWRITE0_IDX] = EOE_VALID | EOE_WWSAO;

	ome->moe[IOE_DIRECT0_IDX] = EOE_VALID | EOE_LDEC;
	ome->moe[IOE_DIRECT1_IDX] = EOE_VALID | EOE_LDECPE;

	/* Configure OMI_FMAN */
	ome = &omt[OMI_FMAN];
	ome->moe[IOE_READ_IDX]  = EOE_VALID | EOE_READI;
	ome->moe[IOE_WRITE_IDX] = EOE_VALID | EOE_WRITE;

	/* Configure OMI_QMAN private */
	ome = &omt[OMI_QMAN_PRIV];
	ome->moe[IOE_READ_IDX]  = EOE_VALID | EOE_READ;
	ome->moe[IOE_WRITE_IDX] = EOE_VALID | EOE_WRITE;
	ome->moe[IOE_EREAD0_IDX] = EOE_VALID | EOE_RSA;
	ome->moe[IOE_EWRITE0_IDX] = EOE_VALID | EOE_WWSA;

	/* Configure OMI_CAAM */
	ome = &omt[OMI_CAAM];
	ome->moe[IOE_READ_IDX]  = EOE_VALID | EOE_READI;
	ome->moe[IOE_WRITE_IDX] = EOE_VALID | EOE_WRITE;
}

/*
 * Get the maximum number of PAACT table entries
 * and subwindows supported by PAMU
 */
static void get_pamu_cap_values(unsigned long pamu_reg_base)
{
	u32 pc_val;

	pc_val = in_be32((u32 *)(pamu_reg_base + PAMU_PC3));
	/* Maximum number of subwindows per liodn */
	max_subwindow_count = 1 << (1 + PAMU_PC3_MWCE(pc_val));
}

/* Setup PAMU registers pointing to PAACT, SPAACT and OMT */
static int setup_one_pamu(unsigned long pamu_reg_base, unsigned long pamu_reg_size,
			  phys_addr_t ppaact_phys, phys_addr_t spaact_phys,
			  phys_addr_t omt_phys)
{
	u32 *pc;
	struct pamu_mmap_regs *pamu_regs;

	pc = (u32 *) (pamu_reg_base + PAMU_PC);
	pamu_regs = (struct pamu_mmap_regs *)
		(pamu_reg_base + PAMU_MMAP_REGS_BASE);

	/* set up pointers to corenet control blocks */

	out_be32(&pamu_regs->ppbah, upper_32_bits(ppaact_phys));
	out_be32(&pamu_regs->ppbal, lower_32_bits(ppaact_phys));
	ppaact_phys = ppaact_phys + PAACT_SIZE;
	out_be32(&pamu_regs->pplah, upper_32_bits(ppaact_phys));
	out_be32(&pamu_regs->pplal, lower_32_bits(ppaact_phys));

	out_be32(&pamu_regs->spbah, upper_32_bits(spaact_phys));
	out_be32(&pamu_regs->spbal, lower_32_bits(spaact_phys));
	spaact_phys = spaact_phys + SPAACT_SIZE;
	out_be32(&pamu_regs->splah, upper_32_bits(spaact_phys));
	out_be32(&pamu_regs->splal, lower_32_bits(spaact_phys));

	out_be32(&pamu_regs->obah, upper_32_bits(omt_phys));
	out_be32(&pamu_regs->obal, lower_32_bits(omt_phys));
	omt_phys = omt_phys + OMT_SIZE;
	out_be32(&pamu_regs->olah, upper_32_bits(omt_phys));
	out_be32(&pamu_regs->olal, lower_32_bits(omt_phys));

	/*
	 * set PAMU enable bit,
	 * allow ppaact & omt to be cached
	 * & enable PAMU access violation interrupts.
	 */

	out_be32((u32 *)(pamu_reg_base + PAMU_PICS),
		 PAMU_ACCESS_VIOLATION_ENABLE);
	out_be32(pc, PAMU_PC_PE | PAMU_PC_OCE | PAMU_PC_SPCC | PAMU_PC_PPCC);
	return 0;
}

/* Enable all device LIODNS */
static void setup_liodns(void)
{
	int i, len;
	struct paace *ppaace;
	struct device_node *node = NULL;
	const u32 *prop;

	for_each_node_with_property(node, "fsl,liodn") {
		prop = of_get_property(node, "fsl,liodn", &len);
		for (i = 0; i < len / sizeof(u32); i++) {
			int liodn;

			liodn = be32_to_cpup(&prop[i]);
			if (liodn >= PAACE_NUMBER_ENTRIES) {
				pr_debug("Invalid LIODN value %d\n", liodn);
				continue;
			}
			ppaace = pamu_get_ppaace(liodn);
			pamu_init_ppaace(ppaace);
			/* window size is 2^(WSE+1) bytes */
			set_bf(ppaace->addr_bitfields, PPAACE_AF_WSE, 35);
			ppaace->wbah = 0;
			set_bf(ppaace->addr_bitfields, PPAACE_AF_WBAL, 0);
			set_bf(ppaace->impl_attr, PAACE_IA_ATM,
			       PAACE_ATM_NO_XLATE);
			set_bf(ppaace->addr_bitfields, PAACE_AF_AP,
			       PAACE_AP_PERMS_ALL);
			if (of_device_is_compatible(node, "fsl,qman-portal"))
				setup_qbman_paace(ppaace, QMAN_PORTAL_PAACE);
			if (of_device_is_compatible(node, "fsl,qman"))
				setup_qbman_paace(ppaace, QMAN_PAACE);
			if (of_device_is_compatible(node, "fsl,bman"))
				setup_qbman_paace(ppaace, BMAN_PAACE);
			mb();
			pamu_enable_liodn(liodn);
		}
	}
}

static irqreturn_t pamu_av_isr(int irq, void *arg)
{
	struct pamu_isr_data *data = arg;
	phys_addr_t phys;
	unsigned int i, j, ret;

	pr_emerg("access violation interrupt\n");

	for (i = 0; i < data->count; i++) {
		void __iomem *p = data->pamu_reg_base + i * PAMU_OFFSET;
		u32 pics = in_be32(p + PAMU_PICS);

		if (pics & PAMU_ACCESS_VIOLATION_STAT) {
			u32 avs1 = in_be32(p + PAMU_AVS1);
			struct paace *paace;

			pr_emerg("POES1=%08x\n", in_be32(p + PAMU_POES1));
			pr_emerg("POES2=%08x\n", in_be32(p + PAMU_POES2));
			pr_emerg("AVS1=%08x\n", avs1);
			pr_emerg("AVS2=%08x\n", in_be32(p + PAMU_AVS2));
			pr_emerg("AVA=%016llx\n",
				 make64(in_be32(p + PAMU_AVAH),
					in_be32(p + PAMU_AVAL)));
			pr_emerg("UDAD=%08x\n", in_be32(p + PAMU_UDAD));
			pr_emerg("POEA=%016llx\n",
				 make64(in_be32(p + PAMU_POEAH),
					in_be32(p + PAMU_POEAL)));

			phys = make64(in_be32(p + PAMU_POEAH),
				      in_be32(p + PAMU_POEAL));

			/* Assume that POEA points to a PAACE */
			if (phys) {
				u32 *paace = phys_to_virt(phys);

				/* Only the first four words are relevant */
				for (j = 0; j < 4; j++)
					pr_emerg("PAACE[%u]=%08x\n",
						 j, in_be32(paace + j));
			}

			/* clear access violation condition */
			out_be32(p + PAMU_AVS1, avs1 & PAMU_AV_MASK);
			paace = pamu_get_ppaace(avs1 >> PAMU_AVS1_LIODN_SHIFT);
			BUG_ON(!paace);
			/* check if we got a violation for a disabled LIODN */
			if (!get_bf(paace->addr_bitfields, PAACE_AF_V)) {
				/*
				 * As per hardware erratum A-003638, access
				 * violation can be reported for a disabled
				 * LIODN. If we hit that condition, disable
				 * access violation reporting.
				 */
				pics &= ~PAMU_ACCESS_VIOLATION_ENABLE;
			} else {
				/* Disable the LIODN */
				ret = pamu_disable_liodn(avs1 >> PAMU_AVS1_LIODN_SHIFT);
				BUG_ON(ret);
				pr_emerg("Disabling liodn %x\n",
					 avs1 >> PAMU_AVS1_LIODN_SHIFT);
			}
			out_be32((p + PAMU_PICS), pics);
		}
	}

	return IRQ_HANDLED;
}

#define LAWAR_EN		0x80000000
#define LAWAR_TARGET_MASK	0x0FF00000
#define LAWAR_TARGET_SHIFT	20
#define LAWAR_SIZE_MASK		0x0000003F
#define LAWAR_CSDID_MASK	0x000FF000
#define LAWAR_CSDID_SHIFT	12

#define LAW_SIZE_4K		0xb

struct ccsr_law {
	u32	lawbarh;	/* LAWn base address high */
	u32	lawbarl;	/* LAWn base address low */
	u32	lawar;		/* LAWn attributes */
	u32	reserved;
};

/*
 * Create a coherence subdomain for a given memory block.
 */
static int create_csd(phys_addr_t phys, size_t size, u32 csd_port_id)
{
	struct device_node *np;
	const __be32 *iprop;
	void __iomem *lac = NULL;	/* Local Access Control registers */
	struct ccsr_law __iomem *law;
	void __iomem *ccm = NULL;
	u32 __iomem *csdids;
	unsigned int i, num_laws, num_csds;
	u32 law_target = 0;
	u32 csd_id = 0;
	int ret = 0;

	np = of_find_compatible_node(NULL, NULL, "fsl,corenet-law");
	if (!np)
		return -ENODEV;

	iprop = of_get_property(np, "fsl,num-laws", NULL);
	if (!iprop) {
		ret = -ENODEV;
		goto error;
	}

	num_laws = be32_to_cpup(iprop);
	if (!num_laws) {
		ret = -ENODEV;
		goto error;
	}

	lac = of_iomap(np, 0);
	if (!lac) {
		ret = -ENODEV;
		goto error;
	}

	/* LAW registers are at offset 0xC00 */
	law = lac + 0xC00;

	of_node_put(np);

	np = of_find_compatible_node(NULL, NULL, "fsl,corenet-cf");
	if (!np) {
		ret = -ENODEV;
		goto error;
	}

	iprop = of_get_property(np, "fsl,ccf-num-csdids", NULL);
	if (!iprop) {
		ret = -ENODEV;
		goto error;
	}

	num_csds = be32_to_cpup(iprop);
	if (!num_csds) {
		ret = -ENODEV;
		goto error;
	}

	ccm = of_iomap(np, 0);
	if (!ccm) {
		ret = -ENOMEM;
		goto error;
	}

	/* The undocumented CSDID registers are at offset 0x600 */
	csdids = ccm + 0x600;

	of_node_put(np);
	np = NULL;

	/* Find an unused coherence subdomain ID */
	for (csd_id = 0; csd_id < num_csds; csd_id++) {
		if (!csdids[csd_id])
			break;
	}

	/* Store the Port ID in the (undocumented) proper CIDMRxx register */
	csdids[csd_id] = csd_port_id;

	/* Find the DDR LAW that maps to our buffer. */
	for (i = 0; i < num_laws; i++) {
		if (law[i].lawar & LAWAR_EN) {
			phys_addr_t law_start, law_end;

			law_start = make64(law[i].lawbarh, law[i].lawbarl);
			law_end = law_start +
				(2ULL << (law[i].lawar & LAWAR_SIZE_MASK));

			if (law_start <= phys && phys < law_end) {
				law_target = law[i].lawar & LAWAR_TARGET_MASK;
				break;
			}
		}
	}

	if (i == 0 || i == num_laws) {
		/* This should never happen */
		ret = -ENOENT;
		goto error;
	}

	/* Find a free LAW entry */
	while (law[--i].lawar & LAWAR_EN) {
		if (i == 0) {
			/* No higher priority LAW slots available */
			ret = -ENOENT;
			goto error;
		}
	}

	law[i].lawbarh = upper_32_bits(phys);
	law[i].lawbarl = lower_32_bits(phys);
	wmb();
	law[i].lawar = LAWAR_EN | law_target | (csd_id << LAWAR_CSDID_SHIFT) |
		(LAW_SIZE_4K + get_order(size));
	wmb();

error:
	if (ccm)
		iounmap(ccm);

	if (lac)
		iounmap(lac);

	if (np)
		of_node_put(np);

	return ret;
}

/*
 * Table of SVRs and the corresponding PORT_ID values. Port ID corresponds to a
 * bit map of snoopers for a given range of memory mapped by a LAW.
 *
 * All future CoreNet-enabled SOCs will have this erratum(A-004510) fixed, so this
 * table should never need to be updated.  SVRs are guaranteed to be unique, so
 * there is no worry that a future SOC will inadvertently have one of these
 * values.
 */
static const struct {
	u32 svr;
	u32 port_id;
} port_id_map[] = {
	{(SVR_P2040 << 8) | 0x10, 0xFF000000},	/* P2040 1.0 */
	{(SVR_P2040 << 8) | 0x11, 0xFF000000},	/* P2040 1.1 */
	{(SVR_P2041 << 8) | 0x10, 0xFF000000},	/* P2041 1.0 */
	{(SVR_P2041 << 8) | 0x11, 0xFF000000},	/* P2041 1.1 */
	{(SVR_P3041 << 8) | 0x10, 0xFF000000},	/* P3041 1.0 */
	{(SVR_P3041 << 8) | 0x11, 0xFF000000},	/* P3041 1.1 */
	{(SVR_P4040 << 8) | 0x20, 0xFFF80000},	/* P4040 2.0 */
	{(SVR_P4080 << 8) | 0x20, 0xFFF80000},	/* P4080 2.0 */
	{(SVR_P5010 << 8) | 0x10, 0xFC000000},	/* P5010 1.0 */
	{(SVR_P5010 << 8) | 0x20, 0xFC000000},	/* P5010 2.0 */
	{(SVR_P5020 << 8) | 0x10, 0xFC000000},	/* P5020 1.0 */
	{(SVR_P5021 << 8) | 0x10, 0xFF800000},	/* P5021 1.0 */
	{(SVR_P5040 << 8) | 0x10, 0xFF800000},	/* P5040 1.0 */
};

#define SVR_SECURITY	0x80000	/* The Security (E) bit */

static int fsl_pamu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *pamu_regs = NULL;
	struct ccsr_guts __iomem *guts_regs = NULL;
	u32 pamubypenr, pamu_counter;
	unsigned long pamu_reg_off;
	unsigned long pamu_reg_base;
	struct pamu_isr_data *data = NULL;
	struct device_node *guts_node;
	u64 size;
	struct page *p;
	int ret = 0;
	int irq;
	phys_addr_t ppaact_phys;
	phys_addr_t spaact_phys;
	struct ome *omt;
	phys_addr_t omt_phys;
	size_t mem_size = 0;
	unsigned int order = 0;
	u32 csd_port_id = 0;
	unsigned i;
	/*
	 * enumerate all PAMUs and allocate and setup PAMU tables
	 * for each of them,
	 * NOTE : All PAMUs share the same LIODN tables.
	 */

	if (WARN_ON(probed))
		return -EBUSY;

	pamu_regs = of_iomap(dev->of_node, 0);
	if (!pamu_regs) {
		dev_err(dev, "ioremap of PAMU node failed\n");
		return -ENOMEM;
	}
	of_get_address(dev->of_node, 0, &size, NULL);

	irq = irq_of_parse_and_map(dev->of_node, 0);
	if (irq == NO_IRQ) {
		dev_warn(dev, "no interrupts listed in PAMU node\n");
		goto error;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto error;
	}
	data->pamu_reg_base = pamu_regs;
	data->count = size / PAMU_OFFSET;

	/* The ISR needs access to the regs, so we won't iounmap them */
	ret = request_irq(irq, pamu_av_isr, 0, "pamu", data);
	if (ret < 0) {
		dev_err(dev, "error %i installing ISR for irq %i\n", ret, irq);
		goto error;
	}

	guts_node = of_find_matching_node(NULL, guts_device_ids);
	if (!guts_node) {
		dev_err(dev, "could not find GUTS node %pOF\n", dev->of_node);
		ret = -ENODEV;
		goto error;
	}

	guts_regs = of_iomap(guts_node, 0);
	of_node_put(guts_node);
	if (!guts_regs) {
		dev_err(dev, "ioremap of GUTS node failed\n");
		ret = -ENODEV;
		goto error;
	}

	/* read in the PAMU capability registers */
	get_pamu_cap_values((unsigned long)pamu_regs);
	/*
	 * To simplify the allocation of a coherency domain, we allocate the
	 * PAACT and the OMT in the same memory buffer.  Unfortunately, this
	 * wastes more memory compared to allocating the buffers separately.
	 */
	/* Determine how much memory we need */
	mem_size = (PAGE_SIZE << get_order(PAACT_SIZE)) +
		(PAGE_SIZE << get_order(SPAACT_SIZE)) +
		(PAGE_SIZE << get_order(OMT_SIZE));
	order = get_order(mem_size);

	p = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!p) {
		dev_err(dev, "unable to allocate PAACT/SPAACT/OMT block\n");
		ret = -ENOMEM;
		goto error;
	}

	ppaact = page_address(p);
	ppaact_phys = page_to_phys(p);

	/* Make sure the memory is naturally aligned */
	if (ppaact_phys & ((PAGE_SIZE << order) - 1)) {
		dev_err(dev, "PAACT/OMT block is unaligned\n");
		ret = -ENOMEM;
		goto error;
	}

	spaact = (void *)ppaact + (PAGE_SIZE << get_order(PAACT_SIZE));
	omt = (void *)spaact + (PAGE_SIZE << get_order(SPAACT_SIZE));

	dev_dbg(dev, "ppaact virt=%p phys=%pa\n", ppaact, &ppaact_phys);

	/* Check to see if we need to implement the work-around on this SOC */

	/* Determine the Port ID for our coherence subdomain */
	for (i = 0; i < ARRAY_SIZE(port_id_map); i++) {
		if (port_id_map[i].svr == (mfspr(SPRN_SVR) & ~SVR_SECURITY)) {
			csd_port_id = port_id_map[i].port_id;
			dev_dbg(dev, "found matching SVR %08x\n",
				port_id_map[i].svr);
			break;
		}
	}

	if (csd_port_id) {
		dev_dbg(dev, "creating coherency subdomain at address %pa, size %zu, port id 0x%08x",
			&ppaact_phys, mem_size, csd_port_id);

		ret = create_csd(ppaact_phys, mem_size, csd_port_id);
		if (ret) {
			dev_err(dev, "could not create coherence subdomain\n");
			return ret;
		}
	}

	spaact_phys = virt_to_phys(spaact);
	omt_phys = virt_to_phys(omt);

	spaace_pool = gen_pool_create(ilog2(sizeof(struct paace)), -1);
	if (!spaace_pool) {
		ret = -ENOMEM;
		dev_err(dev, "Failed to allocate spaace gen pool\n");
		goto error;
	}

	ret = gen_pool_add(spaace_pool, (unsigned long)spaact, SPAACT_SIZE, -1);
	if (ret)
		goto error_genpool;

	pamubypenr = in_be32(&guts_regs->pamubypenr);

	for (pamu_reg_off = 0, pamu_counter = 0x80000000; pamu_reg_off < size;
	     pamu_reg_off += PAMU_OFFSET, pamu_counter >>= 1) {

		pamu_reg_base = (unsigned long)pamu_regs + pamu_reg_off;
		setup_one_pamu(pamu_reg_base, pamu_reg_off, ppaact_phys,
			       spaact_phys, omt_phys);
		/* Disable PAMU bypass for this PAMU */
		pamubypenr &= ~pamu_counter;
	}

	setup_omt(omt);

	/* Enable all relevant PAMU(s) */
	out_be32(&guts_regs->pamubypenr, pamubypenr);

	iounmap(guts_regs);

	/* Enable DMA for the LIODNs in the device tree */

	setup_liodns();

	probed = true;

	return 0;

error_genpool:
	gen_pool_destroy(spaace_pool);

error:
	if (irq != NO_IRQ)
		free_irq(irq, data);

	if (data) {
		memset(data, 0, sizeof(struct pamu_isr_data));
		kfree(data);
	}

	if (pamu_regs)
		iounmap(pamu_regs);

	if (guts_regs)
		iounmap(guts_regs);

	if (ppaact)
		free_pages((unsigned long)ppaact, order);

	ppaact = NULL;

	return ret;
}

static struct platform_driver fsl_of_pamu_driver = {
	.driver = {
		.name = "fsl-of-pamu",
	},
	.probe = fsl_pamu_probe,
};

static __init int fsl_pamu_init(void)
{
	struct platform_device *pdev = NULL;
	struct device_node *np;
	int ret;

	/*
	 * The normal OF process calls the probe function at some
	 * indeterminate later time, after most drivers have loaded.  This is
	 * too late for us, because PAMU clients (like the Qman driver)
	 * depend on PAMU being initialized early.
	 *
	 * So instead, we "manually" call our probe function by creating the
	 * platform devices ourselves.
	 */

	/*
	 * We assume that there is only one PAMU node in the device tree.  A
	 * single PAMU node represents all of the PAMU devices in the SOC
	 * already.   Everything else already makes that assumption, and the
	 * binding for the PAMU nodes doesn't allow for any parent-child
	 * relationships anyway.  In other words, support for more than one
	 * PAMU node would require significant changes to a lot of code.
	 */

	np = of_find_compatible_node(NULL, NULL, "fsl,pamu");
	if (!np) {
		pr_err("could not find a PAMU node\n");
		return -ENODEV;
	}

	ret = platform_driver_register(&fsl_of_pamu_driver);
	if (ret) {
		pr_err("could not register driver (err=%i)\n", ret);
		goto error_driver_register;
	}

	pdev = platform_device_alloc("fsl-of-pamu", 0);
	if (!pdev) {
		pr_err("could not allocate device %pOF\n", np);
		ret = -ENOMEM;
		goto error_device_alloc;
	}
	pdev->dev.of_node = of_node_get(np);

	ret = pamu_domain_init();
	if (ret)
		goto error_device_add;

	ret = platform_device_add(pdev);
	if (ret) {
		pr_err("could not add device %pOF (err=%i)\n", np, ret);
		goto error_device_add;
	}

	return 0;

error_device_add:
	of_node_put(pdev->dev.of_node);
	pdev->dev.of_node = NULL;

	platform_device_put(pdev);

error_device_alloc:
	platform_driver_unregister(&fsl_of_pamu_driver);

error_driver_register:
	of_node_put(np);

	return ret;
}
arch_initcall(fsl_pamu_init);
