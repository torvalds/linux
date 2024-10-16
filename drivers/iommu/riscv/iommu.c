// SPDX-License-Identifier: GPL-2.0-only
/*
 * IOMMU API for RISC-V IOMMU implementations.
 *
 * Copyright © 2022-2024 Rivos Inc.
 * Copyright © 2023 FORTH-ICS/CARV
 *
 * Authors
 *	Tomasz Jeznach <tjeznach@rivosinc.com>
 *	Nick Kossifidis <mick@ics.forth.gr>
 */

#define pr_fmt(fmt) "riscv-iommu: " fmt

#include <linux/compiler.h>
#include <linux/crash_dump.h>
#include <linux/init.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "../iommu-pages.h"
#include "iommu-bits.h"
#include "iommu.h"

/* Timeouts in [us] */
#define RISCV_IOMMU_DDTP_TIMEOUT	50000

/* RISC-V IOMMU PPN <> PHYS address conversions, PHYS <=> PPN[53:10] */
#define phys_to_ppn(pa)  (((pa) >> 2) & (((1ULL << 44) - 1) << 10))
#define ppn_to_phys(pn)	 (((pn) << 2) & (((1ULL << 44) - 1) << 12))

#define dev_to_iommu(dev) \
	iommu_get_iommu_dev(dev, struct riscv_iommu_device, iommu)

/* Device resource-managed allocations */
struct riscv_iommu_devres {
	void *addr;
	int order;
};

static void riscv_iommu_devres_pages_release(struct device *dev, void *res)
{
	struct riscv_iommu_devres *devres = res;

	iommu_free_pages(devres->addr, devres->order);
}

static int riscv_iommu_devres_pages_match(struct device *dev, void *res, void *p)
{
	struct riscv_iommu_devres *devres = res;
	struct riscv_iommu_devres *target = p;

	return devres->addr == target->addr;
}

static void *riscv_iommu_get_pages(struct riscv_iommu_device *iommu, int order)
{
	struct riscv_iommu_devres *devres;
	void *addr;

	addr = iommu_alloc_pages_node(dev_to_node(iommu->dev),
				      GFP_KERNEL_ACCOUNT, order);
	if (unlikely(!addr))
		return NULL;

	devres = devres_alloc(riscv_iommu_devres_pages_release,
			      sizeof(struct riscv_iommu_devres), GFP_KERNEL);

	if (unlikely(!devres)) {
		iommu_free_pages(addr, order);
		return NULL;
	}

	devres->addr = addr;
	devres->order = order;

	devres_add(iommu->dev, devres);

	return addr;
}

static void riscv_iommu_free_pages(struct riscv_iommu_device *iommu, void *addr)
{
	struct riscv_iommu_devres devres = { .addr = addr };

	devres_release(iommu->dev, riscv_iommu_devres_pages_release,
		       riscv_iommu_devres_pages_match, &devres);
}

/* Lookup and initialize device context info structure. */
static struct riscv_iommu_dc *riscv_iommu_get_dc(struct riscv_iommu_device *iommu,
						 unsigned int devid)
{
	const bool base_format = !(iommu->caps & RISCV_IOMMU_CAPABILITIES_MSI_FLAT);
	unsigned int depth;
	unsigned long ddt, old, new;
	void *ptr;
	u8 ddi_bits[3] = { 0 };
	u64 *ddtp = NULL;

	/* Make sure the mode is valid */
	if (iommu->ddt_mode < RISCV_IOMMU_DDTP_IOMMU_MODE_1LVL ||
	    iommu->ddt_mode > RISCV_IOMMU_DDTP_IOMMU_MODE_3LVL)
		return NULL;

	/*
	 * Device id partitioning for base format:
	 * DDI[0]: bits 0 - 6   (1st level) (7 bits)
	 * DDI[1]: bits 7 - 15  (2nd level) (9 bits)
	 * DDI[2]: bits 16 - 23 (3rd level) (8 bits)
	 *
	 * For extended format:
	 * DDI[0]: bits 0 - 5   (1st level) (6 bits)
	 * DDI[1]: bits 6 - 14  (2nd level) (9 bits)
	 * DDI[2]: bits 15 - 23 (3rd level) (9 bits)
	 */
	if (base_format) {
		ddi_bits[0] = 7;
		ddi_bits[1] = 7 + 9;
		ddi_bits[2] = 7 + 9 + 8;
	} else {
		ddi_bits[0] = 6;
		ddi_bits[1] = 6 + 9;
		ddi_bits[2] = 6 + 9 + 9;
	}

	/* Make sure device id is within range */
	depth = iommu->ddt_mode - RISCV_IOMMU_DDTP_IOMMU_MODE_1LVL;
	if (devid >= (1 << ddi_bits[depth]))
		return NULL;

	/* Get to the level of the non-leaf node that holds the device context */
	for (ddtp = iommu->ddt_root; depth-- > 0;) {
		const int split = ddi_bits[depth];
		/*
		 * Each non-leaf node is 64bits wide and on each level
		 * nodes are indexed by DDI[depth].
		 */
		ddtp += (devid >> split) & 0x1FF;

		/*
		 * Check if this node has been populated and if not
		 * allocate a new level and populate it.
		 */
		do {
			ddt = READ_ONCE(*(unsigned long *)ddtp);
			if (ddt & RISCV_IOMMU_DDTE_V) {
				ddtp = __va(ppn_to_phys(ddt));
				break;
			}

			ptr = riscv_iommu_get_pages(iommu, 0);
			if (!ptr)
				return NULL;

			new = phys_to_ppn(__pa(ptr)) | RISCV_IOMMU_DDTE_V;
			old = cmpxchg_relaxed((unsigned long *)ddtp, ddt, new);

			if (old == ddt) {
				ddtp = (u64 *)ptr;
				break;
			}

			/* Race setting DDT detected, re-read and retry. */
			riscv_iommu_free_pages(iommu, ptr);
		} while (1);
	}

	/*
	 * Grab the node that matches DDI[depth], note that when using base
	 * format the device context is 4 * 64bits, and the extended format
	 * is 8 * 64bits, hence the (3 - base_format) below.
	 */
	ddtp += (devid & ((64 << base_format) - 1)) << (3 - base_format);

	return (struct riscv_iommu_dc *)ddtp;
}

/*
 * This is best effort IOMMU translation shutdown flow.
 * Disable IOMMU without waiting for hardware response.
 */
static void riscv_iommu_disable(struct riscv_iommu_device *iommu)
{
	riscv_iommu_writeq(iommu, RISCV_IOMMU_REG_DDTP, 0);
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_CQCSR, 0);
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_FQCSR, 0);
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_PQCSR, 0);
}

#define riscv_iommu_read_ddtp(iommu) ({ \
	u64 ddtp; \
	riscv_iommu_readq_timeout((iommu), RISCV_IOMMU_REG_DDTP, ddtp, \
				  !(ddtp & RISCV_IOMMU_DDTP_BUSY), 10, \
				  RISCV_IOMMU_DDTP_TIMEOUT); \
	ddtp; })

static int riscv_iommu_iodir_alloc(struct riscv_iommu_device *iommu)
{
	u64 ddtp;
	unsigned int mode;

	ddtp = riscv_iommu_read_ddtp(iommu);
	if (ddtp & RISCV_IOMMU_DDTP_BUSY)
		return -EBUSY;

	/*
	 * It is optional for the hardware to report a fixed address for device
	 * directory root page when DDT.MODE is OFF or BARE.
	 */
	mode = FIELD_GET(RISCV_IOMMU_DDTP_IOMMU_MODE, ddtp);
	if (mode == RISCV_IOMMU_DDTP_IOMMU_MODE_BARE ||
	    mode == RISCV_IOMMU_DDTP_IOMMU_MODE_OFF) {
		/* Use WARL to discover hardware fixed DDT PPN */
		riscv_iommu_writeq(iommu, RISCV_IOMMU_REG_DDTP,
				   FIELD_PREP(RISCV_IOMMU_DDTP_IOMMU_MODE, mode));
		ddtp = riscv_iommu_read_ddtp(iommu);
		if (ddtp & RISCV_IOMMU_DDTP_BUSY)
			return -EBUSY;

		iommu->ddt_phys = ppn_to_phys(ddtp);
		if (iommu->ddt_phys)
			iommu->ddt_root = devm_ioremap(iommu->dev,
						       iommu->ddt_phys, PAGE_SIZE);
		if (iommu->ddt_root)
			memset(iommu->ddt_root, 0, PAGE_SIZE);
	}

	if (!iommu->ddt_root) {
		iommu->ddt_root = riscv_iommu_get_pages(iommu, 0);
		iommu->ddt_phys = __pa(iommu->ddt_root);
	}

	if (!iommu->ddt_root)
		return -ENOMEM;

	return 0;
}

/*
 * Discover supported DDT modes starting from requested value,
 * configure DDTP register with accepted mode and root DDT address.
 * Accepted iommu->ddt_mode is updated on success.
 */
static int riscv_iommu_iodir_set_mode(struct riscv_iommu_device *iommu,
				      unsigned int ddtp_mode)
{
	struct device *dev = iommu->dev;
	u64 ddtp, rq_ddtp;
	unsigned int mode, rq_mode = ddtp_mode;

	ddtp = riscv_iommu_read_ddtp(iommu);
	if (ddtp & RISCV_IOMMU_DDTP_BUSY)
		return -EBUSY;

	/* Disallow state transition from xLVL to xLVL. */
	mode = FIELD_GET(RISCV_IOMMU_DDTP_IOMMU_MODE, ddtp);
	if (mode != RISCV_IOMMU_DDTP_IOMMU_MODE_BARE &&
	    mode != RISCV_IOMMU_DDTP_IOMMU_MODE_OFF &&
	    rq_mode != RISCV_IOMMU_DDTP_IOMMU_MODE_BARE &&
	    rq_mode != RISCV_IOMMU_DDTP_IOMMU_MODE_OFF)
		return -EINVAL;

	do {
		rq_ddtp = FIELD_PREP(RISCV_IOMMU_DDTP_IOMMU_MODE, rq_mode);
		if (rq_mode > RISCV_IOMMU_DDTP_IOMMU_MODE_BARE)
			rq_ddtp |= phys_to_ppn(iommu->ddt_phys);

		riscv_iommu_writeq(iommu, RISCV_IOMMU_REG_DDTP, rq_ddtp);
		ddtp = riscv_iommu_read_ddtp(iommu);
		if (ddtp & RISCV_IOMMU_DDTP_BUSY) {
			dev_err(dev, "timeout when setting ddtp (ddt mode: %u, read: %llx)\n",
				rq_mode, ddtp);
			return -EBUSY;
		}

		/* Verify IOMMU hardware accepts new DDTP config. */
		mode = FIELD_GET(RISCV_IOMMU_DDTP_IOMMU_MODE, ddtp);

		if (rq_mode == mode)
			break;

		/* Hardware mandatory DDTP mode has not been accepted. */
		if (rq_mode < RISCV_IOMMU_DDTP_IOMMU_MODE_1LVL && rq_ddtp != ddtp) {
			dev_err(dev, "DDTP update failed hw: %llx vs %llx\n",
				ddtp, rq_ddtp);
			return -EINVAL;
		}

		/*
		 * Mode field is WARL, an IOMMU may support a subset of
		 * directory table levels in which case if we tried to set
		 * an unsupported number of levels we'll readback either
		 * a valid xLVL or off/bare. If we got off/bare, try again
		 * with a smaller xLVL.
		 */
		if (mode < RISCV_IOMMU_DDTP_IOMMU_MODE_1LVL &&
		    rq_mode > RISCV_IOMMU_DDTP_IOMMU_MODE_1LVL) {
			dev_dbg(dev, "DDTP hw mode %u vs %u\n", mode, rq_mode);
			rq_mode--;
			continue;
		}

		/*
		 * We tried all supported modes and IOMMU hardware failed to
		 * accept new settings, something went very wrong since off/bare
		 * and at least one xLVL must be supported.
		 */
		dev_err(dev, "DDTP hw mode %u, failed to set %u\n",
			mode, ddtp_mode);
		return -EINVAL;
	} while (1);

	iommu->ddt_mode = mode;
	if (mode != ddtp_mode)
		dev_dbg(dev, "DDTP hw mode %u, requested %u\n", mode, ddtp_mode);

	return 0;
}

#define RISCV_IOMMU_FSC_BARE 0

/*
 * Update IODIR for the device.
 *
 * During the execution of riscv_iommu_probe_device(), IODIR entries are
 * allocated for the device's identifiers.  Device context invalidation
 * becomes necessary only if one of the updated entries was previously
 * marked as valid, given that invalid device context entries are not
 * cached by the IOMMU hardware.
 * In this implementation, updating a valid device context while the
 * device is not quiesced might be disruptive, potentially causing
 * interim translation faults.
 */
static void riscv_iommu_iodir_update(struct riscv_iommu_device *iommu,
				     struct device *dev, u64 fsc, u64 ta)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct riscv_iommu_dc *dc;
	u64 tc;
	int i;

	/* Device context invalidation ignored for now. */

	/*
	 * For device context with DC_TC_PDTV = 0, translation attributes valid bit
	 * is stored as DC_TC_V bit (both sharing the same location at BIT(0)).
	 */
	for (i = 0; i < fwspec->num_ids; i++) {
		dc = riscv_iommu_get_dc(iommu, fwspec->ids[i]);
		tc = READ_ONCE(dc->tc);
		tc |= ta & RISCV_IOMMU_DC_TC_V;

		WRITE_ONCE(dc->fsc, fsc);
		WRITE_ONCE(dc->ta, ta & RISCV_IOMMU_PC_TA_PSCID);
		/* Update device context, write TC.V as the last step. */
		dma_wmb();
		WRITE_ONCE(dc->tc, tc);
	}
}

static int riscv_iommu_attach_blocking_domain(struct iommu_domain *iommu_domain,
					      struct device *dev)
{
	struct riscv_iommu_device *iommu = dev_to_iommu(dev);

	riscv_iommu_iodir_update(iommu, dev, RISCV_IOMMU_FSC_BARE, 0);

	return 0;
}

static struct iommu_domain riscv_iommu_blocking_domain = {
	.type = IOMMU_DOMAIN_BLOCKED,
	.ops = &(const struct iommu_domain_ops) {
		.attach_dev = riscv_iommu_attach_blocking_domain,
	}
};

static int riscv_iommu_attach_identity_domain(struct iommu_domain *iommu_domain,
					      struct device *dev)
{
	struct riscv_iommu_device *iommu = dev_to_iommu(dev);

	riscv_iommu_iodir_update(iommu, dev, RISCV_IOMMU_FSC_BARE, RISCV_IOMMU_PC_TA_V);

	return 0;
}

static struct iommu_domain riscv_iommu_identity_domain = {
	.type = IOMMU_DOMAIN_IDENTITY,
	.ops = &(const struct iommu_domain_ops) {
		.attach_dev = riscv_iommu_attach_identity_domain,
	}
};

static int riscv_iommu_device_domain_type(struct device *dev)
{
	return IOMMU_DOMAIN_IDENTITY;
}

static struct iommu_group *riscv_iommu_device_group(struct device *dev)
{
	if (dev_is_pci(dev))
		return pci_device_group(dev);
	return generic_device_group(dev);
}

static int riscv_iommu_of_xlate(struct device *dev, const struct of_phandle_args *args)
{
	return iommu_fwspec_add_ids(dev, args->args, 1);
}

static struct iommu_device *riscv_iommu_probe_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct riscv_iommu_device *iommu;
	struct riscv_iommu_dc *dc;
	u64 tc;
	int i;

	if (!fwspec || !fwspec->iommu_fwnode->dev || !fwspec->num_ids)
		return ERR_PTR(-ENODEV);

	iommu = dev_get_drvdata(fwspec->iommu_fwnode->dev);
	if (!iommu)
		return ERR_PTR(-ENODEV);

	/*
	 * IOMMU hardware operating in fail-over BARE mode will provide
	 * identity translation for all connected devices anyway...
	 */
	if (iommu->ddt_mode <= RISCV_IOMMU_DDTP_IOMMU_MODE_BARE)
		return ERR_PTR(-ENODEV);

	/*
	 * Allocate and pre-configure device context entries in
	 * the device directory. Do not mark the context valid yet.
	 */
	tc = 0;
	if (iommu->caps & RISCV_IOMMU_CAPABILITIES_AMO_HWAD)
		tc |= RISCV_IOMMU_DC_TC_SADE;
	for (i = 0; i < fwspec->num_ids; i++) {
		dc = riscv_iommu_get_dc(iommu, fwspec->ids[i]);
		if (!dc)
			return ERR_PTR(-ENODEV);
		if (READ_ONCE(dc->tc) & RISCV_IOMMU_DC_TC_V)
			dev_warn(dev, "already attached to IOMMU device directory\n");
		WRITE_ONCE(dc->tc, tc);
	}

	return &iommu->iommu;
}

static const struct iommu_ops riscv_iommu_ops = {
	.of_xlate = riscv_iommu_of_xlate,
	.identity_domain = &riscv_iommu_identity_domain,
	.blocked_domain = &riscv_iommu_blocking_domain,
	.release_domain = &riscv_iommu_blocking_domain,
	.def_domain_type = riscv_iommu_device_domain_type,
	.device_group = riscv_iommu_device_group,
	.probe_device = riscv_iommu_probe_device,
};

static int riscv_iommu_init_check(struct riscv_iommu_device *iommu)
{
	u64 ddtp;

	/*
	 * Make sure the IOMMU is switched off or in pass-through mode during
	 * regular boot flow and disable translation when we boot into a kexec
	 * kernel and the previous kernel left them enabled.
	 */
	ddtp = riscv_iommu_readq(iommu, RISCV_IOMMU_REG_DDTP);
	if (ddtp & RISCV_IOMMU_DDTP_BUSY)
		return -EBUSY;

	if (FIELD_GET(RISCV_IOMMU_DDTP_IOMMU_MODE, ddtp) >
	     RISCV_IOMMU_DDTP_IOMMU_MODE_BARE) {
		if (!is_kdump_kernel())
			return -EBUSY;
		riscv_iommu_disable(iommu);
	}

	/* Configure accesses to in-memory data structures for CPU-native byte order. */
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN) !=
	    !!(iommu->fctl & RISCV_IOMMU_FCTL_BE)) {
		if (!(iommu->caps & RISCV_IOMMU_CAPABILITIES_END))
			return -EINVAL;
		riscv_iommu_writel(iommu, RISCV_IOMMU_REG_FCTL,
				   iommu->fctl ^ RISCV_IOMMU_FCTL_BE);
		iommu->fctl = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_FCTL);
		if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN) !=
		    !!(iommu->fctl & RISCV_IOMMU_FCTL_BE))
			return -EINVAL;
	}

	return 0;
}

void riscv_iommu_remove(struct riscv_iommu_device *iommu)
{
	iommu_device_unregister(&iommu->iommu);
	iommu_device_sysfs_remove(&iommu->iommu);
	riscv_iommu_iodir_set_mode(iommu, RISCV_IOMMU_DDTP_IOMMU_MODE_OFF);
}

int riscv_iommu_init(struct riscv_iommu_device *iommu)
{
	int rc;

	rc = riscv_iommu_init_check(iommu);
	if (rc)
		return dev_err_probe(iommu->dev, rc, "unexpected device state\n");

	rc = riscv_iommu_iodir_alloc(iommu);
	if (rc)
		return rc;

	rc = riscv_iommu_iodir_set_mode(iommu, RISCV_IOMMU_DDTP_IOMMU_MODE_MAX);
	if (rc)
		return rc;

	rc = iommu_device_sysfs_add(&iommu->iommu, NULL, NULL, "riscv-iommu@%s",
				    dev_name(iommu->dev));
	if (rc) {
		dev_err_probe(iommu->dev, rc, "cannot register sysfs interface\n");
		goto err_iodir_off;
	}

	rc = iommu_device_register(&iommu->iommu, &riscv_iommu_ops, iommu->dev);
	if (rc) {
		dev_err_probe(iommu->dev, rc, "cannot register iommu interface\n");
		goto err_remove_sysfs;
	}

	return 0;

err_remove_sysfs:
	iommu_device_sysfs_remove(&iommu->iommu);
err_iodir_off:
	riscv_iommu_iodir_set_mode(iommu, RISCV_IOMMU_DDTP_IOMMU_MODE_OFF);
	return rc;
}
