/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/pci_regs.h>
#include <linux/pci_ids.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <asm/opal.h>
#include <asm/msi_bitmap.h>
#include <asm/pci-bridge.h> /* for struct pci_controller */
#include <asm/pnv-pci.h>

#include "cxl.h"


#define CXL_PCI_VSEC_ID	0x1280
#define CXL_VSEC_MIN_SIZE 0x80

#define CXL_READ_VSEC_LENGTH(dev, vsec, dest)			\
	{							\
		pci_read_config_word(dev, vsec + 0x6, dest);	\
		*dest >>= 4;					\
	}
#define CXL_READ_VSEC_NAFUS(dev, vsec, dest) \
	pci_read_config_byte(dev, vsec + 0x8, dest)

#define CXL_READ_VSEC_STATUS(dev, vsec, dest) \
	pci_read_config_byte(dev, vsec + 0x9, dest)
#define CXL_STATUS_SECOND_PORT  0x80
#define CXL_STATUS_MSI_X_FULL   0x40
#define CXL_STATUS_MSI_X_SINGLE 0x20
#define CXL_STATUS_FLASH_RW     0x08
#define CXL_STATUS_FLASH_RO     0x04
#define CXL_STATUS_LOADABLE_AFU 0x02
#define CXL_STATUS_LOADABLE_PSL 0x01
/* If we see these features we won't try to use the card */
#define CXL_UNSUPPORTED_FEATURES \
	(CXL_STATUS_MSI_X_FULL | CXL_STATUS_MSI_X_SINGLE)

#define CXL_READ_VSEC_MODE_CONTROL(dev, vsec, dest) \
	pci_read_config_byte(dev, vsec + 0xa, dest)
#define CXL_WRITE_VSEC_MODE_CONTROL(dev, vsec, val) \
	pci_write_config_byte(dev, vsec + 0xa, val)
#define CXL_VSEC_PROTOCOL_MASK   0xe0
#define CXL_VSEC_PROTOCOL_1024TB 0x80
#define CXL_VSEC_PROTOCOL_512TB  0x40
#define CXL_VSEC_PROTOCOL_256TB  0x20 /* Power 8 uses this */
#define CXL_VSEC_PROTOCOL_ENABLE 0x01

#define CXL_READ_VSEC_PSL_REVISION(dev, vsec, dest) \
	pci_read_config_word(dev, vsec + 0xc, dest)
#define CXL_READ_VSEC_CAIA_MINOR(dev, vsec, dest) \
	pci_read_config_byte(dev, vsec + 0xe, dest)
#define CXL_READ_VSEC_CAIA_MAJOR(dev, vsec, dest) \
	pci_read_config_byte(dev, vsec + 0xf, dest)
#define CXL_READ_VSEC_BASE_IMAGE(dev, vsec, dest) \
	pci_read_config_word(dev, vsec + 0x10, dest)

#define CXL_READ_VSEC_IMAGE_STATE(dev, vsec, dest) \
	pci_read_config_byte(dev, vsec + 0x13, dest)
#define CXL_WRITE_VSEC_IMAGE_STATE(dev, vsec, val) \
	pci_write_config_byte(dev, vsec + 0x13, val)
#define CXL_VSEC_USER_IMAGE_LOADED 0x80 /* RO */
#define CXL_VSEC_PERST_LOADS_IMAGE 0x20 /* RW */
#define CXL_VSEC_PERST_SELECT_USER 0x10 /* RW */

#define CXL_READ_VSEC_AFU_DESC_OFF(dev, vsec, dest) \
	pci_read_config_dword(dev, vsec + 0x20, dest)
#define CXL_READ_VSEC_AFU_DESC_SIZE(dev, vsec, dest) \
	pci_read_config_dword(dev, vsec + 0x24, dest)
#define CXL_READ_VSEC_PS_OFF(dev, vsec, dest) \
	pci_read_config_dword(dev, vsec + 0x28, dest)
#define CXL_READ_VSEC_PS_SIZE(dev, vsec, dest) \
	pci_read_config_dword(dev, vsec + 0x2c, dest)


/* This works a little different than the p1/p2 register accesses to make it
 * easier to pull out individual fields */
#define AFUD_READ(afu, off)		in_be64(afu->afu_desc_mmio + off)
#define EXTRACT_PPC_BIT(val, bit)	(!!(val & PPC_BIT(bit)))
#define EXTRACT_PPC_BITS(val, bs, be)	((val & PPC_BITMASK(bs, be)) >> PPC_BITLSHIFT(be))

#define AFUD_READ_INFO(afu)		AFUD_READ(afu, 0x0)
#define   AFUD_NUM_INTS_PER_PROC(val)	EXTRACT_PPC_BITS(val,  0, 15)
#define   AFUD_NUM_PROCS(val)		EXTRACT_PPC_BITS(val, 16, 31)
#define   AFUD_NUM_CRS(val)		EXTRACT_PPC_BITS(val, 32, 47)
#define   AFUD_MULTIMODE(val)		EXTRACT_PPC_BIT(val, 48)
#define   AFUD_PUSH_BLOCK_TRANSFER(val)	EXTRACT_PPC_BIT(val, 55)
#define   AFUD_DEDICATED_PROCESS(val)	EXTRACT_PPC_BIT(val, 59)
#define   AFUD_AFU_DIRECTED(val)	EXTRACT_PPC_BIT(val, 61)
#define   AFUD_TIME_SLICED(val)		EXTRACT_PPC_BIT(val, 63)
#define AFUD_READ_CR(afu)		AFUD_READ(afu, 0x20)
#define   AFUD_CR_LEN(val)		EXTRACT_PPC_BITS(val, 8, 63)
#define AFUD_READ_CR_OFF(afu)		AFUD_READ(afu, 0x28)
#define AFUD_READ_PPPSA(afu)		AFUD_READ(afu, 0x30)
#define   AFUD_PPPSA_PP(val)		EXTRACT_PPC_BIT(val, 6)
#define   AFUD_PPPSA_PSA(val)		EXTRACT_PPC_BIT(val, 7)
#define   AFUD_PPPSA_LEN(val)		EXTRACT_PPC_BITS(val, 8, 63)
#define AFUD_READ_PPPSA_OFF(afu)	AFUD_READ(afu, 0x38)
#define AFUD_READ_EB(afu)		AFUD_READ(afu, 0x40)
#define   AFUD_EB_LEN(val)		EXTRACT_PPC_BITS(val, 8, 63)
#define AFUD_READ_EB_OFF(afu)		AFUD_READ(afu, 0x48)

static DEFINE_PCI_DEVICE_TABLE(cxl_pci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_IBM, 0x0477), },
	{ PCI_DEVICE(PCI_VENDOR_ID_IBM, 0x044b), },
	{ PCI_DEVICE(PCI_VENDOR_ID_IBM, 0x04cf), },
	{ PCI_DEVICE_CLASS(0x120000, ~0), },

	{ }
};
MODULE_DEVICE_TABLE(pci, cxl_pci_tbl);


/*
 * Mostly using these wrappers to avoid confusion:
 * priv 1 is BAR2, while priv 2 is BAR0
 */
static inline resource_size_t p1_base(struct pci_dev *dev)
{
	return pci_resource_start(dev, 2);
}

static inline resource_size_t p1_size(struct pci_dev *dev)
{
	return pci_resource_len(dev, 2);
}

static inline resource_size_t p2_base(struct pci_dev *dev)
{
	return pci_resource_start(dev, 0);
}

static inline resource_size_t p2_size(struct pci_dev *dev)
{
	return pci_resource_len(dev, 0);
}

static int find_cxl_vsec(struct pci_dev *dev)
{
	int vsec = 0;
	u16 val;

	while ((vsec = pci_find_next_ext_capability(dev, vsec, PCI_EXT_CAP_ID_VNDR))) {
		pci_read_config_word(dev, vsec + 0x4, &val);
		if (val == CXL_PCI_VSEC_ID)
			return vsec;
	}
	return 0;

}

static void dump_cxl_config_space(struct pci_dev *dev)
{
	int vsec;
	u32 val;

	dev_info(&dev->dev, "dump_cxl_config_space\n");

	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &val);
	dev_info(&dev->dev, "BAR0: %#.8x\n", val);
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_1, &val);
	dev_info(&dev->dev, "BAR1: %#.8x\n", val);
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_2, &val);
	dev_info(&dev->dev, "BAR2: %#.8x\n", val);
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_3, &val);
	dev_info(&dev->dev, "BAR3: %#.8x\n", val);
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_4, &val);
	dev_info(&dev->dev, "BAR4: %#.8x\n", val);
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_5, &val);
	dev_info(&dev->dev, "BAR5: %#.8x\n", val);

	dev_info(&dev->dev, "p1 regs: %#llx, len: %#llx\n",
		p1_base(dev), p1_size(dev));
	dev_info(&dev->dev, "p2 regs: %#llx, len: %#llx\n",
		p1_base(dev), p2_size(dev));
	dev_info(&dev->dev, "BAR 4/5: %#llx, len: %#llx\n",
		pci_resource_start(dev, 4), pci_resource_len(dev, 4));

	if (!(vsec = find_cxl_vsec(dev)))
		return;

#define show_reg(name, what) \
	dev_info(&dev->dev, "cxl vsec: %30s: %#x\n", name, what)

	pci_read_config_dword(dev, vsec + 0x0, &val);
	show_reg("Cap ID", (val >> 0) & 0xffff);
	show_reg("Cap Ver", (val >> 16) & 0xf);
	show_reg("Next Cap Ptr", (val >> 20) & 0xfff);
	pci_read_config_dword(dev, vsec + 0x4, &val);
	show_reg("VSEC ID", (val >> 0) & 0xffff);
	show_reg("VSEC Rev", (val >> 16) & 0xf);
	show_reg("VSEC Length",	(val >> 20) & 0xfff);
	pci_read_config_dword(dev, vsec + 0x8, &val);
	show_reg("Num AFUs", (val >> 0) & 0xff);
	show_reg("Status", (val >> 8) & 0xff);
	show_reg("Mode Control", (val >> 16) & 0xff);
	show_reg("Reserved", (val >> 24) & 0xff);
	pci_read_config_dword(dev, vsec + 0xc, &val);
	show_reg("PSL Rev", (val >> 0) & 0xffff);
	show_reg("CAIA Ver", (val >> 16) & 0xffff);
	pci_read_config_dword(dev, vsec + 0x10, &val);
	show_reg("Base Image Rev", (val >> 0) & 0xffff);
	show_reg("Reserved", (val >> 16) & 0x0fff);
	show_reg("Image Control", (val >> 28) & 0x3);
	show_reg("Reserved", (val >> 30) & 0x1);
	show_reg("Image Loaded", (val >> 31) & 0x1);

	pci_read_config_dword(dev, vsec + 0x14, &val);
	show_reg("Reserved", val);
	pci_read_config_dword(dev, vsec + 0x18, &val);
	show_reg("Reserved", val);
	pci_read_config_dword(dev, vsec + 0x1c, &val);
	show_reg("Reserved", val);

	pci_read_config_dword(dev, vsec + 0x20, &val);
	show_reg("AFU Descriptor Offset", val);
	pci_read_config_dword(dev, vsec + 0x24, &val);
	show_reg("AFU Descriptor Size", val);
	pci_read_config_dword(dev, vsec + 0x28, &val);
	show_reg("Problem State Offset", val);
	pci_read_config_dword(dev, vsec + 0x2c, &val);
	show_reg("Problem State Size", val);

	pci_read_config_dword(dev, vsec + 0x30, &val);
	show_reg("Reserved", val);
	pci_read_config_dword(dev, vsec + 0x34, &val);
	show_reg("Reserved", val);
	pci_read_config_dword(dev, vsec + 0x38, &val);
	show_reg("Reserved", val);
	pci_read_config_dword(dev, vsec + 0x3c, &val);
	show_reg("Reserved", val);

	pci_read_config_dword(dev, vsec + 0x40, &val);
	show_reg("PSL Programming Port", val);
	pci_read_config_dword(dev, vsec + 0x44, &val);
	show_reg("PSL Programming Control", val);

	pci_read_config_dword(dev, vsec + 0x48, &val);
	show_reg("Reserved", val);
	pci_read_config_dword(dev, vsec + 0x4c, &val);
	show_reg("Reserved", val);

	pci_read_config_dword(dev, vsec + 0x50, &val);
	show_reg("Flash Address Register", val);
	pci_read_config_dword(dev, vsec + 0x54, &val);
	show_reg("Flash Size Register", val);
	pci_read_config_dword(dev, vsec + 0x58, &val);
	show_reg("Flash Status/Control Register", val);
	pci_read_config_dword(dev, vsec + 0x58, &val);
	show_reg("Flash Data Port", val);

#undef show_reg
}

static void dump_afu_descriptor(struct cxl_afu *afu)
{
	u64 val;

#define show_reg(name, what) \
	dev_info(&afu->dev, "afu desc: %30s: %#llx\n", name, what)

	val = AFUD_READ_INFO(afu);
	show_reg("num_ints_per_process", AFUD_NUM_INTS_PER_PROC(val));
	show_reg("num_of_processes", AFUD_NUM_PROCS(val));
	show_reg("num_of_afu_CRs", AFUD_NUM_CRS(val));
	show_reg("req_prog_mode", val & 0xffffULL);

	val = AFUD_READ(afu, 0x8);
	show_reg("Reserved", val);
	val = AFUD_READ(afu, 0x10);
	show_reg("Reserved", val);
	val = AFUD_READ(afu, 0x18);
	show_reg("Reserved", val);

	val = AFUD_READ_CR(afu);
	show_reg("Reserved", (val >> (63-7)) & 0xff);
	show_reg("AFU_CR_len", AFUD_CR_LEN(val));

	val = AFUD_READ_CR_OFF(afu);
	show_reg("AFU_CR_offset", val);

	val = AFUD_READ_PPPSA(afu);
	show_reg("PerProcessPSA_control", (val >> (63-7)) & 0xff);
	show_reg("PerProcessPSA Length", AFUD_PPPSA_LEN(val));

	val = AFUD_READ_PPPSA_OFF(afu);
	show_reg("PerProcessPSA_offset", val);

	val = AFUD_READ_EB(afu);
	show_reg("Reserved", (val >> (63-7)) & 0xff);
	show_reg("AFU_EB_len", AFUD_EB_LEN(val));

	val = AFUD_READ_EB_OFF(afu);
	show_reg("AFU_EB_offset", val);

#undef show_reg
}

static int init_implementation_adapter_regs(struct cxl *adapter, struct pci_dev *dev)
{
	struct device_node *np;
	const __be32 *prop;
	u64 psl_dsnctl;
	u64 chipid;

	if (!(np = pnv_pci_to_phb_node(dev)))
		return -ENODEV;

	while (np && !(prop = of_get_property(np, "ibm,chip-id", NULL)))
		np = of_get_next_parent(np);
	if (!np)
		return -ENODEV;
	chipid = be32_to_cpup(prop);
	of_node_put(np);

	/* Tell PSL where to route data to */
	psl_dsnctl = 0x02E8900002000000ULL | (chipid << (63-5));
	cxl_p1_write(adapter, CXL_PSL_DSNDCTL, psl_dsnctl);
	cxl_p1_write(adapter, CXL_PSL_RESLCKTO, 0x20000000200ULL);
	/* snoop write mask */
	cxl_p1_write(adapter, CXL_PSL_SNWRALLOC, 0x00000000FFFFFFFFULL);
	/* set fir_accum */
	cxl_p1_write(adapter, CXL_PSL_FIR_CNTL, 0x0800000000000000ULL);
	/* for debugging with trace arrays */
	cxl_p1_write(adapter, CXL_PSL_TRACE, 0x0000FF7C00000000ULL);

	return 0;
}

static int init_implementation_afu_regs(struct cxl_afu *afu)
{
	/* read/write masks for this slice */
	cxl_p1n_write(afu, CXL_PSL_APCALLOC_A, 0xFFFFFFFEFEFEFEFEULL);
	/* APC read/write masks for this slice */
	cxl_p1n_write(afu, CXL_PSL_COALLOC_A, 0xFF000000FEFEFEFEULL);
	/* for debugging with trace arrays */
	cxl_p1n_write(afu, CXL_PSL_SLICE_TRACE, 0x0000FFFF00000000ULL);
	cxl_p1n_write(afu, CXL_PSL_RXCTL_A, CXL_PSL_RXCTL_AFUHP_4S);

	return 0;
}

int cxl_setup_irq(struct cxl *adapter, unsigned int hwirq,
			 unsigned int virq)
{
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);

	return pnv_cxl_ioda_msi_setup(dev, hwirq, virq);
}

int cxl_alloc_one_irq(struct cxl *adapter)
{
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);

	return pnv_cxl_alloc_hwirqs(dev, 1);
}

void cxl_release_one_irq(struct cxl *adapter, int hwirq)
{
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);

	return pnv_cxl_release_hwirqs(dev, hwirq, 1);
}

int cxl_alloc_irq_ranges(struct cxl_irq_ranges *irqs, struct cxl *adapter, unsigned int num)
{
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);

	return pnv_cxl_alloc_hwirq_ranges(irqs, dev, num);
}

void cxl_release_irq_ranges(struct cxl_irq_ranges *irqs, struct cxl *adapter)
{
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);

	pnv_cxl_release_hwirq_ranges(irqs, dev);
}

static int setup_cxl_bars(struct pci_dev *dev)
{
	/* Safety check in case we get backported to < 3.17 without M64 */
	if ((p1_base(dev) < 0x100000000ULL) ||
	    (p2_base(dev) < 0x100000000ULL)) {
		dev_err(&dev->dev, "ABORTING: M32 BAR assignment incompatible with CXL\n");
		return -ENODEV;
	}

	/*
	 * BAR 4/5 has a special meaning for CXL and must be programmed with a
	 * special value corresponding to the CXL protocol address range.
	 * For POWER 8 that means bits 48:49 must be set to 10
	 */
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_4, 0x00000000);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_5, 0x00020000);

	return 0;
}

/* pciex node: ibm,opal-m64-window = <0x3d058 0x0 0x3d058 0x0 0x8 0x0>; */
static int switch_card_to_cxl(struct pci_dev *dev)
{
	int vsec;
	u8 val;
	int rc;

	dev_info(&dev->dev, "switch card to CXL\n");

	if (!(vsec = find_cxl_vsec(dev))) {
		dev_err(&dev->dev, "ABORTING: CXL VSEC not found!\n");
		return -ENODEV;
	}

	if ((rc = CXL_READ_VSEC_MODE_CONTROL(dev, vsec, &val))) {
		dev_err(&dev->dev, "failed to read current mode control: %i", rc);
		return rc;
	}
	val &= ~CXL_VSEC_PROTOCOL_MASK;
	val |= CXL_VSEC_PROTOCOL_256TB | CXL_VSEC_PROTOCOL_ENABLE;
	if ((rc = CXL_WRITE_VSEC_MODE_CONTROL(dev, vsec, val))) {
		dev_err(&dev->dev, "failed to enable CXL protocol: %i", rc);
		return rc;
	}
	/*
	 * The CAIA spec (v0.12 11.6 Bi-modal Device Support) states
	 * we must wait 100ms after this mode switch before touching
	 * PCIe config space.
	 */
	msleep(100);

	return 0;
}

static int cxl_map_slice_regs(struct cxl_afu *afu, struct cxl *adapter, struct pci_dev *dev)
{
	u64 p1n_base, p2n_base, afu_desc;
	const u64 p1n_size = 0x100;
	const u64 p2n_size = 0x1000;

	p1n_base = p1_base(dev) + 0x10000 + (afu->slice * p1n_size);
	p2n_base = p2_base(dev) + (afu->slice * p2n_size);
	afu->psn_phys = p2_base(dev) + (adapter->ps_off + (afu->slice * adapter->ps_size));
	afu_desc = p2_base(dev) + adapter->afu_desc_off + (afu->slice * adapter->afu_desc_size);

	if (!(afu->p1n_mmio = ioremap(p1n_base, p1n_size)))
		goto err;
	if (!(afu->p2n_mmio = ioremap(p2n_base, p2n_size)))
		goto err1;
	if (afu_desc) {
		if (!(afu->afu_desc_mmio = ioremap(afu_desc, adapter->afu_desc_size)))
			goto err2;
	}

	return 0;
err2:
	iounmap(afu->p2n_mmio);
err1:
	iounmap(afu->p1n_mmio);
err:
	dev_err(&afu->dev, "Error mapping AFU MMIO regions\n");
	return -ENOMEM;
}

static void cxl_unmap_slice_regs(struct cxl_afu *afu)
{
	if (afu->p1n_mmio)
		iounmap(afu->p2n_mmio);
	if (afu->p1n_mmio)
		iounmap(afu->p1n_mmio);
}

static void cxl_release_afu(struct device *dev)
{
	struct cxl_afu *afu = to_cxl_afu(dev);

	pr_devel("cxl_release_afu\n");

	kfree(afu);
}

static struct cxl_afu *cxl_alloc_afu(struct cxl *adapter, int slice)
{
	struct cxl_afu *afu;

	if (!(afu = kzalloc(sizeof(struct cxl_afu), GFP_KERNEL)))
		return NULL;

	afu->adapter = adapter;
	afu->dev.parent = &adapter->dev;
	afu->dev.release = cxl_release_afu;
	afu->slice = slice;
	idr_init(&afu->contexts_idr);
	mutex_init(&afu->contexts_lock);
	spin_lock_init(&afu->afu_cntl_lock);
	mutex_init(&afu->spa_mutex);

	afu->prefault_mode = CXL_PREFAULT_NONE;
	afu->irqs_max = afu->adapter->user_irqs;

	return afu;
}

/* Expects AFU struct to have recently been zeroed out */
static int cxl_read_afu_descriptor(struct cxl_afu *afu)
{
	u64 val;

	val = AFUD_READ_INFO(afu);
	afu->pp_irqs = AFUD_NUM_INTS_PER_PROC(val);
	afu->max_procs_virtualised = AFUD_NUM_PROCS(val);

	if (AFUD_AFU_DIRECTED(val))
		afu->modes_supported |= CXL_MODE_DIRECTED;
	if (AFUD_DEDICATED_PROCESS(val))
		afu->modes_supported |= CXL_MODE_DEDICATED;
	if (AFUD_TIME_SLICED(val))
		afu->modes_supported |= CXL_MODE_TIME_SLICED;

	val = AFUD_READ_PPPSA(afu);
	afu->pp_size = AFUD_PPPSA_LEN(val) * 4096;
	afu->psa = AFUD_PPPSA_PSA(val);
	if ((afu->pp_psa = AFUD_PPPSA_PP(val)))
		afu->pp_offset = AFUD_READ_PPPSA_OFF(afu);

	return 0;
}

static int cxl_afu_descriptor_looks_ok(struct cxl_afu *afu)
{
	if (afu->psa && afu->adapter->ps_size <
			(afu->pp_offset + afu->pp_size*afu->max_procs_virtualised)) {
		dev_err(&afu->dev, "per-process PSA can't fit inside the PSA!\n");
		return -ENODEV;
	}

	if (afu->pp_psa && (afu->pp_size < PAGE_SIZE))
		dev_warn(&afu->dev, "AFU uses < PAGE_SIZE per-process PSA!");

	return 0;
}

static int sanitise_afu_regs(struct cxl_afu *afu)
{
	u64 reg;

	/*
	 * Clear out any regs that contain either an IVTE or address or may be
	 * waiting on an acknowledgement to try to be a bit safer as we bring
	 * it online
	 */
	reg = cxl_p2n_read(afu, CXL_AFU_Cntl_An);
	if ((reg & CXL_AFU_Cntl_An_ES_MASK) != CXL_AFU_Cntl_An_ES_Disabled) {
		dev_warn(&afu->dev, "WARNING: AFU was not disabled: %#.16llx\n", reg);
		if (cxl_afu_reset(afu))
			return -EIO;
		if (cxl_afu_disable(afu))
			return -EIO;
		if (cxl_psl_purge(afu))
			return -EIO;
	}
	cxl_p1n_write(afu, CXL_PSL_SPAP_An, 0x0000000000000000);
	cxl_p1n_write(afu, CXL_PSL_IVTE_Limit_An, 0x0000000000000000);
	cxl_p1n_write(afu, CXL_PSL_IVTE_Offset_An, 0x0000000000000000);
	cxl_p1n_write(afu, CXL_PSL_AMBAR_An, 0x0000000000000000);
	cxl_p1n_write(afu, CXL_PSL_SPOffset_An, 0x0000000000000000);
	cxl_p1n_write(afu, CXL_HAURP_An, 0x0000000000000000);
	cxl_p2n_write(afu, CXL_CSRP_An, 0x0000000000000000);
	cxl_p2n_write(afu, CXL_AURP1_An, 0x0000000000000000);
	cxl_p2n_write(afu, CXL_AURP0_An, 0x0000000000000000);
	cxl_p2n_write(afu, CXL_SSTP1_An, 0x0000000000000000);
	cxl_p2n_write(afu, CXL_SSTP0_An, 0x0000000000000000);
	reg = cxl_p2n_read(afu, CXL_PSL_DSISR_An);
	if (reg) {
		dev_warn(&afu->dev, "AFU had pending DSISR: %#.16llx\n", reg);
		if (reg & CXL_PSL_DSISR_TRANS)
			cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_AE);
		else
			cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_A);
	}
	reg = cxl_p1n_read(afu, CXL_PSL_SERR_An);
	if (reg) {
		if (reg & ~0xffff)
			dev_warn(&afu->dev, "AFU had pending SERR: %#.16llx\n", reg);
		cxl_p1n_write(afu, CXL_PSL_SERR_An, reg & ~0xffff);
	}
	reg = cxl_p2n_read(afu, CXL_PSL_ErrStat_An);
	if (reg) {
		dev_warn(&afu->dev, "AFU had pending error status: %#.16llx\n", reg);
		cxl_p2n_write(afu, CXL_PSL_ErrStat_An, reg);
	}

	return 0;
}

static int cxl_init_afu(struct cxl *adapter, int slice, struct pci_dev *dev)
{
	struct cxl_afu *afu;
	bool free = true;
	int rc;

	if (!(afu = cxl_alloc_afu(adapter, slice)))
		return -ENOMEM;

	if ((rc = dev_set_name(&afu->dev, "afu%i.%i", adapter->adapter_num, slice)))
		goto err1;

	if ((rc = cxl_map_slice_regs(afu, adapter, dev)))
		goto err1;

	if ((rc = sanitise_afu_regs(afu)))
		goto err2;

	/* We need to reset the AFU before we can read the AFU descriptor */
	if ((rc = cxl_afu_reset(afu)))
		goto err2;

	if (cxl_verbose)
		dump_afu_descriptor(afu);

	if ((rc = cxl_read_afu_descriptor(afu)))
		goto err2;

	if ((rc = cxl_afu_descriptor_looks_ok(afu)))
		goto err2;

	if ((rc = init_implementation_afu_regs(afu)))
		goto err2;

	if ((rc = cxl_register_serr_irq(afu)))
		goto err2;

	if ((rc = cxl_register_psl_irq(afu)))
		goto err3;

	/* Don't care if this fails */
	cxl_debugfs_afu_add(afu);

	/*
	 * After we call this function we must not free the afu directly, even
	 * if it returns an error!
	 */
	if ((rc = cxl_register_afu(afu)))
		goto err_put1;

	if ((rc = cxl_sysfs_afu_add(afu)))
		goto err_put1;


	if ((rc = cxl_afu_select_best_mode(afu)))
		goto err_put2;

	adapter->afu[afu->slice] = afu;

	return 0;

err_put2:
	cxl_sysfs_afu_remove(afu);
err_put1:
	device_unregister(&afu->dev);
	free = false;
	cxl_debugfs_afu_remove(afu);
	cxl_release_psl_irq(afu);
err3:
	cxl_release_serr_irq(afu);
err2:
	cxl_unmap_slice_regs(afu);
err1:
	if (free)
		kfree(afu);
	return rc;
}

static void cxl_remove_afu(struct cxl_afu *afu)
{
	pr_devel("cxl_remove_afu\n");

	if (!afu)
		return;

	cxl_sysfs_afu_remove(afu);
	cxl_debugfs_afu_remove(afu);

	spin_lock(&afu->adapter->afu_list_lock);
	afu->adapter->afu[afu->slice] = NULL;
	spin_unlock(&afu->adapter->afu_list_lock);

	cxl_context_detach_all(afu);
	cxl_afu_deactivate_mode(afu);

	cxl_release_psl_irq(afu);
	cxl_release_serr_irq(afu);
	cxl_unmap_slice_regs(afu);

	device_unregister(&afu->dev);
}


static int cxl_map_adapter_regs(struct cxl *adapter, struct pci_dev *dev)
{
	if (pci_request_region(dev, 2, "priv 2 regs"))
		goto err1;
	if (pci_request_region(dev, 0, "priv 1 regs"))
		goto err2;

	pr_devel("cxl_map_adapter_regs: p1: %#.16llx %#llx, p2: %#.16llx %#llx",
			p1_base(dev), p1_size(dev), p2_base(dev), p2_size(dev));

	if (!(adapter->p1_mmio = ioremap(p1_base(dev), p1_size(dev))))
		goto err3;

	if (!(adapter->p2_mmio = ioremap(p2_base(dev), p2_size(dev))))
		goto err4;

	return 0;

err4:
	iounmap(adapter->p1_mmio);
	adapter->p1_mmio = NULL;
err3:
	pci_release_region(dev, 0);
err2:
	pci_release_region(dev, 2);
err1:
	return -ENOMEM;
}

static void cxl_unmap_adapter_regs(struct cxl *adapter)
{
	if (adapter->p1_mmio)
		iounmap(adapter->p1_mmio);
	if (adapter->p2_mmio)
		iounmap(adapter->p2_mmio);
}

static int cxl_read_vsec(struct cxl *adapter, struct pci_dev *dev)
{
	int vsec;
	u32 afu_desc_off, afu_desc_size;
	u32 ps_off, ps_size;
	u16 vseclen;
	u8 image_state;

	if (!(vsec = find_cxl_vsec(dev))) {
		dev_err(&adapter->dev, "ABORTING: CXL VSEC not found!\n");
		return -ENODEV;
	}

	CXL_READ_VSEC_LENGTH(dev, vsec, &vseclen);
	if (vseclen < CXL_VSEC_MIN_SIZE) {
		pr_err("ABORTING: CXL VSEC too short\n");
		return -EINVAL;
	}

	CXL_READ_VSEC_STATUS(dev, vsec, &adapter->vsec_status);
	CXL_READ_VSEC_PSL_REVISION(dev, vsec, &adapter->psl_rev);
	CXL_READ_VSEC_CAIA_MAJOR(dev, vsec, &adapter->caia_major);
	CXL_READ_VSEC_CAIA_MINOR(dev, vsec, &adapter->caia_minor);
	CXL_READ_VSEC_BASE_IMAGE(dev, vsec, &adapter->base_image);
	CXL_READ_VSEC_IMAGE_STATE(dev, vsec, &image_state);
	adapter->user_image_loaded = !!(image_state & CXL_VSEC_USER_IMAGE_LOADED);
	adapter->perst_loads_image = !!(image_state & CXL_VSEC_PERST_LOADS_IMAGE);
	adapter->perst_select_user = !!(image_state & CXL_VSEC_PERST_SELECT_USER);

	CXL_READ_VSEC_NAFUS(dev, vsec, &adapter->slices);
	CXL_READ_VSEC_AFU_DESC_OFF(dev, vsec, &afu_desc_off);
	CXL_READ_VSEC_AFU_DESC_SIZE(dev, vsec, &afu_desc_size);
	CXL_READ_VSEC_PS_OFF(dev, vsec, &ps_off);
	CXL_READ_VSEC_PS_SIZE(dev, vsec, &ps_size);

	/* Convert everything to bytes, because there is NO WAY I'd look at the
	 * code a month later and forget what units these are in ;-) */
	adapter->ps_off = ps_off * 64 * 1024;
	adapter->ps_size = ps_size * 64 * 1024;
	adapter->afu_desc_off = afu_desc_off * 64 * 1024;
	adapter->afu_desc_size = afu_desc_size *64 * 1024;

	/* Total IRQs - 1 PSL ERROR - #AFU*(1 slice error + 1 DSI) */
	adapter->user_irqs = pnv_cxl_get_irq_count(dev) - 1 - 2*adapter->slices;

	return 0;
}

static int cxl_vsec_looks_ok(struct cxl *adapter, struct pci_dev *dev)
{
	if (adapter->vsec_status & CXL_STATUS_SECOND_PORT)
		return -EBUSY;

	if (adapter->vsec_status & CXL_UNSUPPORTED_FEATURES) {
		dev_err(&adapter->dev, "ABORTING: CXL requires unsupported features\n");
		return -EINVAL;
	}

	if (!adapter->slices) {
		/* Once we support dynamic reprogramming we can use the card if
		 * it supports loadable AFUs */
		dev_err(&adapter->dev, "ABORTING: Device has no AFUs\n");
		return -EINVAL;
	}

	if (!adapter->afu_desc_off || !adapter->afu_desc_size) {
		dev_err(&adapter->dev, "ABORTING: VSEC shows no AFU descriptors\n");
		return -EINVAL;
	}

	if (adapter->ps_size > p2_size(dev) - adapter->ps_off) {
		dev_err(&adapter->dev, "ABORTING: Problem state size larger than "
				   "available in BAR2: 0x%llx > 0x%llx\n",
			 adapter->ps_size, p2_size(dev) - adapter->ps_off);
		return -EINVAL;
	}

	return 0;
}

static void cxl_release_adapter(struct device *dev)
{
	struct cxl *adapter = to_cxl_adapter(dev);

	pr_devel("cxl_release_adapter\n");

	kfree(adapter);
}

static struct cxl *cxl_alloc_adapter(struct pci_dev *dev)
{
	struct cxl *adapter;

	if (!(adapter = kzalloc(sizeof(struct cxl), GFP_KERNEL)))
		return NULL;

	adapter->dev.parent = &dev->dev;
	adapter->dev.release = cxl_release_adapter;
	pci_set_drvdata(dev, adapter);
	spin_lock_init(&adapter->afu_list_lock);

	return adapter;
}

static int sanitise_adapter_regs(struct cxl *adapter)
{
	cxl_p1_write(adapter, CXL_PSL_ErrIVTE, 0x0000000000000000);
	return cxl_tlb_slb_invalidate(adapter);
}

static struct cxl *cxl_init_adapter(struct pci_dev *dev)
{
	struct cxl *adapter;
	bool free = true;
	int rc;


	if (!(adapter = cxl_alloc_adapter(dev)))
		return ERR_PTR(-ENOMEM);

	if ((rc = switch_card_to_cxl(dev)))
		goto err1;

	if ((rc = cxl_alloc_adapter_nr(adapter)))
		goto err1;

	if ((rc = dev_set_name(&adapter->dev, "card%i", adapter->adapter_num)))
		goto err2;

	if ((rc = cxl_read_vsec(adapter, dev)))
		goto err2;

	if ((rc = cxl_vsec_looks_ok(adapter, dev)))
		goto err2;

	if ((rc = cxl_map_adapter_regs(adapter, dev)))
		goto err2;

	if ((rc = sanitise_adapter_regs(adapter)))
		goto err2;

	if ((rc = init_implementation_adapter_regs(adapter, dev)))
		goto err3;

	if ((rc = pnv_phb_to_cxl(dev)))
		goto err3;

	if ((rc = cxl_register_psl_err_irq(adapter)))
		goto err3;

	/* Don't care if this one fails: */
	cxl_debugfs_adapter_add(adapter);

	/*
	 * After we call this function we must not free the adapter directly,
	 * even if it returns an error!
	 */
	if ((rc = cxl_register_adapter(adapter)))
		goto err_put1;

	if ((rc = cxl_sysfs_adapter_add(adapter)))
		goto err_put1;

	return adapter;

err_put1:
	device_unregister(&adapter->dev);
	free = false;
	cxl_debugfs_adapter_remove(adapter);
	cxl_release_psl_err_irq(adapter);
err3:
	cxl_unmap_adapter_regs(adapter);
err2:
	cxl_remove_adapter_nr(adapter);
err1:
	if (free)
		kfree(adapter);
	return ERR_PTR(rc);
}

static void cxl_remove_adapter(struct cxl *adapter)
{
	struct pci_dev *pdev = to_pci_dev(adapter->dev.parent);

	pr_devel("cxl_release_adapter\n");

	cxl_sysfs_adapter_remove(adapter);
	cxl_debugfs_adapter_remove(adapter);
	cxl_release_psl_err_irq(adapter);
	cxl_unmap_adapter_regs(adapter);
	cxl_remove_adapter_nr(adapter);

	device_unregister(&adapter->dev);

	pci_release_region(pdev, 0);
	pci_release_region(pdev, 2);
	pci_disable_device(pdev);
}

static int cxl_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct cxl *adapter;
	int slice;
	int rc;

	pci_dev_get(dev);

	if (cxl_verbose)
		dump_cxl_config_space(dev);

	if ((rc = setup_cxl_bars(dev)))
		return rc;

	if ((rc = pci_enable_device(dev))) {
		dev_err(&dev->dev, "pci_enable_device failed: %i\n", rc);
		return rc;
	}

	adapter = cxl_init_adapter(dev);
	if (IS_ERR(adapter)) {
		dev_err(&dev->dev, "cxl_init_adapter failed: %li\n", PTR_ERR(adapter));
		return PTR_ERR(adapter);
	}

	for (slice = 0; slice < adapter->slices; slice++) {
		if ((rc = cxl_init_afu(adapter, slice, dev)))
			dev_err(&dev->dev, "AFU %i failed to initialise: %i\n", slice, rc);
	}

	return 0;
}

static void cxl_remove(struct pci_dev *dev)
{
	struct cxl *adapter = pci_get_drvdata(dev);
	int afu;

	dev_warn(&dev->dev, "pci remove\n");

	/*
	 * Lock to prevent someone grabbing a ref through the adapter list as
	 * we are removing it
	 */
	for (afu = 0; afu < adapter->slices; afu++)
		cxl_remove_afu(adapter->afu[afu]);
	cxl_remove_adapter(adapter);
}

struct pci_driver cxl_pci_driver = {
	.name = "cxl-pci",
	.id_table = cxl_pci_tbl,
	.probe = cxl_probe,
	.remove = cxl_remove,
};
