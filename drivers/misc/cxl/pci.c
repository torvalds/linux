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
#include <asm/pnv-pci.h>
#include <asm/io.h>
#include <asm/reg.h>

#include "cxl.h"
#include <misc/cxl.h>


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
#define CXL_VSEC_PROTOCOL_256TB  0x20 /* Power 8/9 uses this */
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
#define AFUD_READ(afu, off)		in_be64(afu->native->afu_desc_mmio + off)
#define AFUD_READ_LE(afu, off)		in_le64(afu->native->afu_desc_mmio + off)
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

static const struct pci_device_id cxl_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_IBM, 0x0477), },
	{ PCI_DEVICE(PCI_VENDOR_ID_IBM, 0x044b), },
	{ PCI_DEVICE(PCI_VENDOR_ID_IBM, 0x04cf), },
	{ PCI_DEVICE(PCI_VENDOR_ID_IBM, 0x0601), },
	{ PCI_DEVICE(PCI_VENDOR_ID_IBM, 0x0623), },
	{ PCI_DEVICE(PCI_VENDOR_ID_IBM, 0x0628), },
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
		p2_base(dev), p2_size(dev));
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
	u64 val, afu_cr_num, afu_cr_off, afu_cr_len;
	int i;

#define show_reg(name, what) \
	dev_info(&afu->dev, "afu desc: %30s: %#llx\n", name, what)

	val = AFUD_READ_INFO(afu);
	show_reg("num_ints_per_process", AFUD_NUM_INTS_PER_PROC(val));
	show_reg("num_of_processes", AFUD_NUM_PROCS(val));
	show_reg("num_of_afu_CRs", AFUD_NUM_CRS(val));
	show_reg("req_prog_mode", val & 0xffffULL);
	afu_cr_num = AFUD_NUM_CRS(val);

	val = AFUD_READ(afu, 0x8);
	show_reg("Reserved", val);
	val = AFUD_READ(afu, 0x10);
	show_reg("Reserved", val);
	val = AFUD_READ(afu, 0x18);
	show_reg("Reserved", val);

	val = AFUD_READ_CR(afu);
	show_reg("Reserved", (val >> (63-7)) & 0xff);
	show_reg("AFU_CR_len", AFUD_CR_LEN(val));
	afu_cr_len = AFUD_CR_LEN(val) * 256;

	val = AFUD_READ_CR_OFF(afu);
	afu_cr_off = val;
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

	for (i = 0; i < afu_cr_num; i++) {
		val = AFUD_READ_LE(afu, afu_cr_off + i * afu_cr_len);
		show_reg("CR Vendor", val & 0xffff);
		show_reg("CR Device", (val >> 16) & 0xffff);
	}
#undef show_reg
}

#define P8_CAPP_UNIT0_ID 0xBA
#define P8_CAPP_UNIT1_ID 0XBE
#define P9_CAPP_UNIT0_ID 0xC0
#define P9_CAPP_UNIT1_ID 0xE0

static int get_phb_index(struct device_node *np, u32 *phb_index)
{
	if (of_property_read_u32(np, "ibm,phb-index", phb_index))
		return -ENODEV;
	return 0;
}

static u64 get_capp_unit_id(struct device_node *np, u32 phb_index)
{
	/*
	 * POWER 8:
	 *  - For chips other than POWER8NVL, we only have CAPP 0,
	 *    irrespective of which PHB is used.
	 *  - For POWER8NVL, assume CAPP 0 is attached to PHB0 and
	 *    CAPP 1 is attached to PHB1.
	 */
	if (cxl_is_power8()) {
		if (!pvr_version_is(PVR_POWER8NVL))
			return P8_CAPP_UNIT0_ID;

		if (phb_index == 0)
			return P8_CAPP_UNIT0_ID;

		if (phb_index == 1)
			return P8_CAPP_UNIT1_ID;
	}

	/*
	 * POWER 9:
	 *   PEC0 (PHB0). Capp ID = CAPP0 (0b1100_0000)
	 *   PEC1 (PHB1 - PHB2). No capi mode
	 *   PEC2 (PHB3 - PHB4 - PHB5): Capi mode on PHB3 only. Capp ID = CAPP1 (0b1110_0000)
	 */
	if (cxl_is_power9()) {
		if (phb_index == 0)
			return P9_CAPP_UNIT0_ID;

		if (phb_index == 3)
			return P9_CAPP_UNIT1_ID;
	}

	return 0;
}

int cxl_calc_capp_routing(struct pci_dev *dev, u64 *chipid,
			     u32 *phb_index, u64 *capp_unit_id)
{
	int rc;
	struct device_node *np;
	const __be32 *prop;

	if (!(np = pnv_pci_get_phb_node(dev)))
		return -ENODEV;

	while (np && !(prop = of_get_property(np, "ibm,chip-id", NULL)))
		np = of_get_next_parent(np);
	if (!np)
		return -ENODEV;

	*chipid = be32_to_cpup(prop);

	rc = get_phb_index(np, phb_index);
	if (rc) {
		pr_err("cxl: invalid phb index\n");
		return rc;
	}

	*capp_unit_id = get_capp_unit_id(np, *phb_index);
	of_node_put(np);
	if (!*capp_unit_id) {
		pr_err("cxl: invalid capp unit id (phb_index: %d)\n",
		       *phb_index);
		return -ENODEV;
	}

	return 0;
}

static DEFINE_MUTEX(indications_mutex);

static int get_phb_indications(struct pci_dev *dev, u64 *capiind, u64 *asnind,
			       u64 *nbwind)
{
	static u64 nbw, asn, capi = 0;
	struct device_node *np;
	const __be32 *prop;

	mutex_lock(&indications_mutex);
	if (!capi) {
		if (!(np = pnv_pci_get_phb_node(dev))) {
			mutex_unlock(&indications_mutex);
			return -ENODEV;
		}

		prop = of_get_property(np, "ibm,phb-indications", NULL);
		if (!prop) {
			nbw = 0x0300UL; /* legacy values */
			asn = 0x0400UL;
			capi = 0x0200UL;
		} else {
			nbw = (u64)be32_to_cpu(prop[2]);
			asn = (u64)be32_to_cpu(prop[1]);
			capi = (u64)be32_to_cpu(prop[0]);
		}
		of_node_put(np);
	}
	*capiind = capi;
	*asnind = asn;
	*nbwind = nbw;
	mutex_unlock(&indications_mutex);
	return 0;
}

int cxl_get_xsl9_dsnctl(struct pci_dev *dev, u64 capp_unit_id, u64 *reg)
{
	u64 xsl_dsnctl;
	u64 capiind, asnind, nbwind;

	/*
	 * CAPI Identifier bits [0:7]
	 * bit 61:60 MSI bits --> 0
	 * bit 59 TVT selector --> 0
	 */
	if (get_phb_indications(dev, &capiind, &asnind, &nbwind))
		return -ENODEV;

	/*
	 * Tell XSL where to route data to.
	 * The field chipid should match the PHB CAPI_CMPM register
	 */
	xsl_dsnctl = (capiind << (63-15)); /* Bit 57 */
	xsl_dsnctl |= (capp_unit_id << (63-15));

	/* nMMU_ID Defaults to: b’000001001’*/
	xsl_dsnctl |= ((u64)0x09 << (63-28));

	/*
	 * Used to identify CAPI packets which should be sorted into
	 * the Non-Blocking queues by the PHB. This field should match
	 * the PHB PBL_NBW_CMPM register
	 * nbwind=0x03, bits [57:58], must include capi indicator.
	 * Not supported on P9 DD1.
	 */
	xsl_dsnctl |= (nbwind << (63-55));

	/*
	 * Upper 16b address bits of ASB_Notify messages sent to the
	 * system. Need to match the PHB’s ASN Compare/Mask Register.
	 * Not supported on P9 DD1.
	 */
	xsl_dsnctl |= asnind;

	*reg = xsl_dsnctl;
	return 0;
}

static int init_implementation_adapter_regs_psl9(struct cxl *adapter,
						 struct pci_dev *dev)
{
	u64 xsl_dsnctl, psl_fircntl;
	u64 chipid;
	u32 phb_index;
	u64 capp_unit_id;
	u64 psl_debug;
	int rc;

	rc = cxl_calc_capp_routing(dev, &chipid, &phb_index, &capp_unit_id);
	if (rc)
		return rc;

	rc = cxl_get_xsl9_dsnctl(dev, capp_unit_id, &xsl_dsnctl);
	if (rc)
		return rc;

	cxl_p1_write(adapter, CXL_XSL9_DSNCTL, xsl_dsnctl);

	/* Set fir_cntl to recommended value for production env */
	psl_fircntl = (0x2ULL << (63-3)); /* ce_report */
	psl_fircntl |= (0x1ULL << (63-6)); /* FIR_report */
	psl_fircntl |= 0x1ULL; /* ce_thresh */
	cxl_p1_write(adapter, CXL_PSL9_FIR_CNTL, psl_fircntl);

	/* Setup the PSL to transmit packets on the PCIe before the
	 * CAPP is enabled. Make sure that CAPP virtual machines are disabled
	 */
	cxl_p1_write(adapter, CXL_PSL9_DSNDCTL, 0x0001001000012A10ULL);

	/*
	 * A response to an ASB_Notify request is returned by the
	 * system as an MMIO write to the address defined in
	 * the PSL_TNR_ADDR register.
	 * keep the Reset Value: 0x00020000E0000000
	 */

	/* Enable XSL rty limit */
	cxl_p1_write(adapter, CXL_XSL9_DEF, 0x51F8000000000005ULL);

	/* Change XSL_INV dummy read threshold */
	cxl_p1_write(adapter, CXL_XSL9_INV, 0x0000040007FFC200ULL);

	if (phb_index == 3) {
		/* disable machines 31-47 and 20-27 for DMA */
		cxl_p1_write(adapter, CXL_PSL9_APCDEDTYPE, 0x40000FF3FFFF0000ULL);
	}

	/* Snoop machines */
	cxl_p1_write(adapter, CXL_PSL9_APCDEDALLOC, 0x800F000200000000ULL);

	/* Enable NORST and DD2 features */
	cxl_p1_write(adapter, CXL_PSL9_DEBUG, 0xC000000000000000ULL);

	/*
	 * Check if PSL has data-cache. We need to flush adapter datacache
	 * when as its about to be removed.
	 */
	psl_debug = cxl_p1_read(adapter, CXL_PSL9_DEBUG);
	if (psl_debug & CXL_PSL_DEBUG_CDC) {
		dev_dbg(&dev->dev, "No data-cache present\n");
		adapter->native->no_data_cache = true;
	}

	return 0;
}

static int init_implementation_adapter_regs_psl8(struct cxl *adapter, struct pci_dev *dev)
{
	u64 psl_dsnctl, psl_fircntl;
	u64 chipid;
	u32 phb_index;
	u64 capp_unit_id;
	int rc;

	rc = cxl_calc_capp_routing(dev, &chipid, &phb_index, &capp_unit_id);
	if (rc)
		return rc;

	psl_dsnctl = 0x0000900000000000ULL; /* pteupd ttype, scdone */
	psl_dsnctl |= (0x2ULL << (63-38)); /* MMIO hang pulse: 256 us */
	/* Tell PSL where to route data to */
	psl_dsnctl |= (chipid << (63-5));
	psl_dsnctl |= (capp_unit_id << (63-13));

	cxl_p1_write(adapter, CXL_PSL_DSNDCTL, psl_dsnctl);
	cxl_p1_write(adapter, CXL_PSL_RESLCKTO, 0x20000000200ULL);
	/* snoop write mask */
	cxl_p1_write(adapter, CXL_PSL_SNWRALLOC, 0x00000000FFFFFFFFULL);
	/* set fir_cntl to recommended value for production env */
	psl_fircntl = (0x2ULL << (63-3)); /* ce_report */
	psl_fircntl |= (0x1ULL << (63-6)); /* FIR_report */
	psl_fircntl |= 0x1ULL; /* ce_thresh */
	cxl_p1_write(adapter, CXL_PSL_FIR_CNTL, psl_fircntl);
	/* for debugging with trace arrays */
	cxl_p1_write(adapter, CXL_PSL_TRACE, 0x0000FF7C00000000ULL);

	return 0;
}

/* PSL */
#define TBSYNC_CAL(n) (((u64)n & 0x7) << (63-3))
#define TBSYNC_CNT(n) (((u64)n & 0x7) << (63-6))
/* For the PSL this is a multiple for 0 < n <= 7: */
#define PSL_2048_250MHZ_CYCLES 1

static void write_timebase_ctrl_psl8(struct cxl *adapter)
{
	cxl_p1_write(adapter, CXL_PSL_TB_CTLSTAT,
		     TBSYNC_CNT(2 * PSL_2048_250MHZ_CYCLES));
}

static u64 timebase_read_psl9(struct cxl *adapter)
{
	return cxl_p1_read(adapter, CXL_PSL9_Timebase);
}

static u64 timebase_read_psl8(struct cxl *adapter)
{
	return cxl_p1_read(adapter, CXL_PSL_Timebase);
}

static void cxl_setup_psl_timebase(struct cxl *adapter, struct pci_dev *dev)
{
	struct device_node *np;

	adapter->psl_timebase_synced = false;

	if (!(np = pnv_pci_get_phb_node(dev)))
		return;

	/* Do not fail when CAPP timebase sync is not supported by OPAL */
	of_node_get(np);
	if (! of_get_property(np, "ibm,capp-timebase-sync", NULL)) {
		of_node_put(np);
		dev_info(&dev->dev, "PSL timebase inactive: OPAL support missing\n");
		return;
	}
	of_node_put(np);

	/*
	 * Setup PSL Timebase Control and Status register
	 * with the recommended Timebase Sync Count value
	 */
	if (adapter->native->sl_ops->write_timebase_ctrl)
		adapter->native->sl_ops->write_timebase_ctrl(adapter);

	/* Enable PSL Timebase */
	cxl_p1_write(adapter, CXL_PSL_Control, 0x0000000000000000);
	cxl_p1_write(adapter, CXL_PSL_Control, CXL_PSL_Control_tb);

	return;
}

static int init_implementation_afu_regs_psl9(struct cxl_afu *afu)
{
	return 0;
}

static int init_implementation_afu_regs_psl8(struct cxl_afu *afu)
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

int cxl_pci_setup_irq(struct cxl *adapter, unsigned int hwirq,
		unsigned int virq)
{
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);

	return pnv_cxl_ioda_msi_setup(dev, hwirq, virq);
}

int cxl_update_image_control(struct cxl *adapter)
{
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);
	int rc;
	int vsec;
	u8 image_state;

	if (!(vsec = find_cxl_vsec(dev))) {
		dev_err(&dev->dev, "ABORTING: CXL VSEC not found!\n");
		return -ENODEV;
	}

	if ((rc = CXL_READ_VSEC_IMAGE_STATE(dev, vsec, &image_state))) {
		dev_err(&dev->dev, "failed to read image state: %i\n", rc);
		return rc;
	}

	if (adapter->perst_loads_image)
		image_state |= CXL_VSEC_PERST_LOADS_IMAGE;
	else
		image_state &= ~CXL_VSEC_PERST_LOADS_IMAGE;

	if (adapter->perst_select_user)
		image_state |= CXL_VSEC_PERST_SELECT_USER;
	else
		image_state &= ~CXL_VSEC_PERST_SELECT_USER;

	if ((rc = CXL_WRITE_VSEC_IMAGE_STATE(dev, vsec, image_state))) {
		dev_err(&dev->dev, "failed to update image control: %i\n", rc);
		return rc;
	}

	return 0;
}

int cxl_pci_alloc_one_irq(struct cxl *adapter)
{
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);

	return pnv_cxl_alloc_hwirqs(dev, 1);
}

void cxl_pci_release_one_irq(struct cxl *adapter, int hwirq)
{
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);

	return pnv_cxl_release_hwirqs(dev, hwirq, 1);
}

int cxl_pci_alloc_irq_ranges(struct cxl_irq_ranges *irqs,
			struct cxl *adapter, unsigned int num)
{
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);

	return pnv_cxl_alloc_hwirq_ranges(irqs, dev, num);
}

void cxl_pci_release_irq_ranges(struct cxl_irq_ranges *irqs,
				struct cxl *adapter)
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
	 * For POWER 8/9 that means bits 48:49 must be set to 10
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

static int pci_map_slice_regs(struct cxl_afu *afu, struct cxl *adapter, struct pci_dev *dev)
{
	u64 p1n_base, p2n_base, afu_desc;
	const u64 p1n_size = 0x100;
	const u64 p2n_size = 0x1000;

	p1n_base = p1_base(dev) + 0x10000 + (afu->slice * p1n_size);
	p2n_base = p2_base(dev) + (afu->slice * p2n_size);
	afu->psn_phys = p2_base(dev) + (adapter->native->ps_off + (afu->slice * adapter->ps_size));
	afu_desc = p2_base(dev) + adapter->native->afu_desc_off + (afu->slice * adapter->native->afu_desc_size);

	if (!(afu->native->p1n_mmio = ioremap(p1n_base, p1n_size)))
		goto err;
	if (!(afu->p2n_mmio = ioremap(p2n_base, p2n_size)))
		goto err1;
	if (afu_desc) {
		if (!(afu->native->afu_desc_mmio = ioremap(afu_desc, adapter->native->afu_desc_size)))
			goto err2;
	}

	return 0;
err2:
	iounmap(afu->p2n_mmio);
err1:
	iounmap(afu->native->p1n_mmio);
err:
	dev_err(&afu->dev, "Error mapping AFU MMIO regions\n");
	return -ENOMEM;
}

static void pci_unmap_slice_regs(struct cxl_afu *afu)
{
	if (afu->p2n_mmio) {
		iounmap(afu->p2n_mmio);
		afu->p2n_mmio = NULL;
	}
	if (afu->native->p1n_mmio) {
		iounmap(afu->native->p1n_mmio);
		afu->native->p1n_mmio = NULL;
	}
	if (afu->native->afu_desc_mmio) {
		iounmap(afu->native->afu_desc_mmio);
		afu->native->afu_desc_mmio = NULL;
	}
}

void cxl_pci_release_afu(struct device *dev)
{
	struct cxl_afu *afu = to_cxl_afu(dev);

	pr_devel("%s\n", __func__);

	idr_destroy(&afu->contexts_idr);
	cxl_release_spa(afu);

	kfree(afu->native);
	kfree(afu);
}

/* Expects AFU struct to have recently been zeroed out */
static int cxl_read_afu_descriptor(struct cxl_afu *afu)
{
	u64 val;

	val = AFUD_READ_INFO(afu);
	afu->pp_irqs = AFUD_NUM_INTS_PER_PROC(val);
	afu->max_procs_virtualised = AFUD_NUM_PROCS(val);
	afu->crs_num = AFUD_NUM_CRS(val);

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
		afu->native->pp_offset = AFUD_READ_PPPSA_OFF(afu);

	val = AFUD_READ_CR(afu);
	afu->crs_len = AFUD_CR_LEN(val) * 256;
	afu->crs_offset = AFUD_READ_CR_OFF(afu);


	/* eb_len is in multiple of 4K */
	afu->eb_len = AFUD_EB_LEN(AFUD_READ_EB(afu)) * 4096;
	afu->eb_offset = AFUD_READ_EB_OFF(afu);

	/* eb_off is 4K aligned so lower 12 bits are always zero */
	if (EXTRACT_PPC_BITS(afu->eb_offset, 0, 11) != 0) {
		dev_warn(&afu->dev,
			 "Invalid AFU error buffer offset %Lx\n",
			 afu->eb_offset);
		dev_info(&afu->dev,
			 "Ignoring AFU error buffer in the descriptor\n");
		/* indicate that no afu buffer exists */
		afu->eb_len = 0;
	}

	return 0;
}

static int cxl_afu_descriptor_looks_ok(struct cxl_afu *afu)
{
	int i, rc;
	u32 val;

	if (afu->psa && afu->adapter->ps_size <
			(afu->native->pp_offset + afu->pp_size*afu->max_procs_virtualised)) {
		dev_err(&afu->dev, "per-process PSA can't fit inside the PSA!\n");
		return -ENODEV;
	}

	if (afu->pp_psa && (afu->pp_size < PAGE_SIZE))
		dev_warn(&afu->dev, "AFU uses pp_size(%#016llx) < PAGE_SIZE per-process PSA!\n", afu->pp_size);

	for (i = 0; i < afu->crs_num; i++) {
		rc = cxl_ops->afu_cr_read32(afu, i, 0, &val);
		if (rc || val == 0) {
			dev_err(&afu->dev, "ABORTING: AFU configuration record %i is invalid\n", i);
			return -EINVAL;
		}
	}

	if ((afu->modes_supported & ~CXL_MODE_DEDICATED) && afu->max_procs_virtualised == 0) {
		/*
		 * We could also check this for the dedicated process model
		 * since the architecture indicates it should be set to 1, but
		 * in that case we ignore the value and I'd rather not risk
		 * breaking any existing dedicated process AFUs that left it as
		 * 0 (not that I'm aware of any). It is clearly an error for an
		 * AFU directed AFU to set this to 0, and would have previously
		 * triggered a bug resulting in the maximum not being enforced
		 * at all since idr_alloc treats 0 as no maximum.
		 */
		dev_err(&afu->dev, "AFU does not support any processes\n");
		return -EINVAL;
	}

	return 0;
}

static int sanitise_afu_regs_psl9(struct cxl_afu *afu)
{
	u64 reg;

	/*
	 * Clear out any regs that contain either an IVTE or address or may be
	 * waiting on an acknowledgment to try to be a bit safer as we bring
	 * it online
	 */
	reg = cxl_p2n_read(afu, CXL_AFU_Cntl_An);
	if ((reg & CXL_AFU_Cntl_An_ES_MASK) != CXL_AFU_Cntl_An_ES_Disabled) {
		dev_warn(&afu->dev, "WARNING: AFU was not disabled: %#016llx\n", reg);
		if (cxl_ops->afu_reset(afu))
			return -EIO;
		if (cxl_afu_disable(afu))
			return -EIO;
		if (cxl_psl_purge(afu))
			return -EIO;
	}
	cxl_p1n_write(afu, CXL_PSL_SPAP_An, 0x0000000000000000);
	cxl_p1n_write(afu, CXL_PSL_AMBAR_An, 0x0000000000000000);
	reg = cxl_p2n_read(afu, CXL_PSL_DSISR_An);
	if (reg) {
		dev_warn(&afu->dev, "AFU had pending DSISR: %#016llx\n", reg);
		if (reg & CXL_PSL9_DSISR_An_TF)
			cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_AE);
		else
			cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_A);
	}
	if (afu->adapter->native->sl_ops->register_serr_irq) {
		reg = cxl_p1n_read(afu, CXL_PSL_SERR_An);
		if (reg) {
			if (reg & ~0x000000007fffffff)
				dev_warn(&afu->dev, "AFU had pending SERR: %#016llx\n", reg);
			cxl_p1n_write(afu, CXL_PSL_SERR_An, reg & ~0xffff);
		}
	}
	reg = cxl_p2n_read(afu, CXL_PSL_ErrStat_An);
	if (reg) {
		dev_warn(&afu->dev, "AFU had pending error status: %#016llx\n", reg);
		cxl_p2n_write(afu, CXL_PSL_ErrStat_An, reg);
	}

	return 0;
}

static int sanitise_afu_regs_psl8(struct cxl_afu *afu)
{
	u64 reg;

	/*
	 * Clear out any regs that contain either an IVTE or address or may be
	 * waiting on an acknowledgement to try to be a bit safer as we bring
	 * it online
	 */
	reg = cxl_p2n_read(afu, CXL_AFU_Cntl_An);
	if ((reg & CXL_AFU_Cntl_An_ES_MASK) != CXL_AFU_Cntl_An_ES_Disabled) {
		dev_warn(&afu->dev, "WARNING: AFU was not disabled: %#016llx\n", reg);
		if (cxl_ops->afu_reset(afu))
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
		dev_warn(&afu->dev, "AFU had pending DSISR: %#016llx\n", reg);
		if (reg & CXL_PSL_DSISR_TRANS)
			cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_AE);
		else
			cxl_p2n_write(afu, CXL_PSL_TFC_An, CXL_PSL_TFC_An_A);
	}
	if (afu->adapter->native->sl_ops->register_serr_irq) {
		reg = cxl_p1n_read(afu, CXL_PSL_SERR_An);
		if (reg) {
			if (reg & ~0xffff)
				dev_warn(&afu->dev, "AFU had pending SERR: %#016llx\n", reg);
			cxl_p1n_write(afu, CXL_PSL_SERR_An, reg & ~0xffff);
		}
	}
	reg = cxl_p2n_read(afu, CXL_PSL_ErrStat_An);
	if (reg) {
		dev_warn(&afu->dev, "AFU had pending error status: %#016llx\n", reg);
		cxl_p2n_write(afu, CXL_PSL_ErrStat_An, reg);
	}

	return 0;
}

#define ERR_BUFF_MAX_COPY_SIZE PAGE_SIZE
/*
 * afu_eb_read:
 * Called from sysfs and reads the afu error info buffer. The h/w only supports
 * 4/8 bytes aligned access. So in case the requested offset/count arent 8 byte
 * aligned the function uses a bounce buffer which can be max PAGE_SIZE.
 */
ssize_t cxl_pci_afu_read_err_buffer(struct cxl_afu *afu, char *buf,
				loff_t off, size_t count)
{
	loff_t aligned_start, aligned_end;
	size_t aligned_length;
	void *tbuf;
	const void __iomem *ebuf = afu->native->afu_desc_mmio + afu->eb_offset;

	if (count == 0 || off < 0 || (size_t)off >= afu->eb_len)
		return 0;

	/* calculate aligned read window */
	count = min((size_t)(afu->eb_len - off), count);
	aligned_start = round_down(off, 8);
	aligned_end = round_up(off + count, 8);
	aligned_length = aligned_end - aligned_start;

	/* max we can copy in one read is PAGE_SIZE */
	if (aligned_length > ERR_BUFF_MAX_COPY_SIZE) {
		aligned_length = ERR_BUFF_MAX_COPY_SIZE;
		count = ERR_BUFF_MAX_COPY_SIZE - (off & 0x7);
	}

	/* use bounce buffer for copy */
	tbuf = (void *)__get_free_page(GFP_KERNEL);
	if (!tbuf)
		return -ENOMEM;

	/* perform aligned read from the mmio region */
	memcpy_fromio(tbuf, ebuf + aligned_start, aligned_length);
	memcpy(buf, tbuf + (off & 0x7), count);

	free_page((unsigned long)tbuf);

	return count;
}

static int pci_configure_afu(struct cxl_afu *afu, struct cxl *adapter, struct pci_dev *dev)
{
	int rc;

	if ((rc = pci_map_slice_regs(afu, adapter, dev)))
		return rc;

	if (adapter->native->sl_ops->sanitise_afu_regs) {
		rc = adapter->native->sl_ops->sanitise_afu_regs(afu);
		if (rc)
			goto err1;
	}

	/* We need to reset the AFU before we can read the AFU descriptor */
	if ((rc = cxl_ops->afu_reset(afu)))
		goto err1;

	if (cxl_verbose)
		dump_afu_descriptor(afu);

	if ((rc = cxl_read_afu_descriptor(afu)))
		goto err1;

	if ((rc = cxl_afu_descriptor_looks_ok(afu)))
		goto err1;

	if (adapter->native->sl_ops->afu_regs_init)
		if ((rc = adapter->native->sl_ops->afu_regs_init(afu)))
			goto err1;

	if (adapter->native->sl_ops->register_serr_irq)
		if ((rc = adapter->native->sl_ops->register_serr_irq(afu)))
			goto err1;

	if ((rc = cxl_native_register_psl_irq(afu)))
		goto err2;

	atomic_set(&afu->configured_state, 0);
	return 0;

err2:
	if (adapter->native->sl_ops->release_serr_irq)
		adapter->native->sl_ops->release_serr_irq(afu);
err1:
	pci_unmap_slice_regs(afu);
	return rc;
}

static void pci_deconfigure_afu(struct cxl_afu *afu)
{
	/*
	 * It's okay to deconfigure when AFU is already locked, otherwise wait
	 * until there are no readers
	 */
	if (atomic_read(&afu->configured_state) != -1) {
		while (atomic_cmpxchg(&afu->configured_state, 0, -1) != -1)
			schedule();
	}
	cxl_native_release_psl_irq(afu);
	if (afu->adapter->native->sl_ops->release_serr_irq)
		afu->adapter->native->sl_ops->release_serr_irq(afu);
	pci_unmap_slice_regs(afu);
}

static int pci_init_afu(struct cxl *adapter, int slice, struct pci_dev *dev)
{
	struct cxl_afu *afu;
	int rc = -ENOMEM;

	afu = cxl_alloc_afu(adapter, slice);
	if (!afu)
		return -ENOMEM;

	afu->native = kzalloc(sizeof(struct cxl_afu_native), GFP_KERNEL);
	if (!afu->native)
		goto err_free_afu;

	mutex_init(&afu->native->spa_mutex);

	rc = dev_set_name(&afu->dev, "afu%i.%i", adapter->adapter_num, slice);
	if (rc)
		goto err_free_native;

	rc = pci_configure_afu(afu, adapter, dev);
	if (rc)
		goto err_free_native;

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

	adapter->afu[afu->slice] = afu;

	if ((rc = cxl_pci_vphb_add(afu)))
		dev_info(&afu->dev, "Can't register vPHB\n");

	return 0;

err_put1:
	pci_deconfigure_afu(afu);
	cxl_debugfs_afu_remove(afu);
	device_unregister(&afu->dev);
	return rc;

err_free_native:
	kfree(afu->native);
err_free_afu:
	kfree(afu);
	return rc;

}

static void cxl_pci_remove_afu(struct cxl_afu *afu)
{
	pr_devel("%s\n", __func__);

	if (!afu)
		return;

	cxl_pci_vphb_remove(afu);
	cxl_sysfs_afu_remove(afu);
	cxl_debugfs_afu_remove(afu);

	spin_lock(&afu->adapter->afu_list_lock);
	afu->adapter->afu[afu->slice] = NULL;
	spin_unlock(&afu->adapter->afu_list_lock);

	cxl_context_detach_all(afu);
	cxl_ops->afu_deactivate_mode(afu, afu->current_mode);

	pci_deconfigure_afu(afu);
	device_unregister(&afu->dev);
}

int cxl_pci_reset(struct cxl *adapter)
{
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);
	int rc;

	if (adapter->perst_same_image) {
		dev_warn(&dev->dev,
			 "cxl: refusing to reset/reflash when perst_reloads_same_image is set.\n");
		return -EINVAL;
	}

	dev_info(&dev->dev, "CXL reset\n");

	/*
	 * The adapter is about to be reset, so ignore errors.
	 */
	cxl_data_cache_flush(adapter);

	/* pcie_warm_reset requests a fundamental pci reset which includes a
	 * PERST assert/deassert.  PERST triggers a loading of the image
	 * if "user" or "factory" is selected in sysfs */
	if ((rc = pci_set_pcie_reset_state(dev, pcie_warm_reset))) {
		dev_err(&dev->dev, "cxl: pcie_warm_reset failed\n");
		return rc;
	}

	return rc;
}

static int cxl_map_adapter_regs(struct cxl *adapter, struct pci_dev *dev)
{
	if (pci_request_region(dev, 2, "priv 2 regs"))
		goto err1;
	if (pci_request_region(dev, 0, "priv 1 regs"))
		goto err2;

	pr_devel("cxl_map_adapter_regs: p1: %#016llx %#llx, p2: %#016llx %#llx",
			p1_base(dev), p1_size(dev), p2_base(dev), p2_size(dev));

	if (!(adapter->native->p1_mmio = ioremap(p1_base(dev), p1_size(dev))))
		goto err3;

	if (!(adapter->native->p2_mmio = ioremap(p2_base(dev), p2_size(dev))))
		goto err4;

	return 0;

err4:
	iounmap(adapter->native->p1_mmio);
	adapter->native->p1_mmio = NULL;
err3:
	pci_release_region(dev, 0);
err2:
	pci_release_region(dev, 2);
err1:
	return -ENOMEM;
}

static void cxl_unmap_adapter_regs(struct cxl *adapter)
{
	if (adapter->native->p1_mmio) {
		iounmap(adapter->native->p1_mmio);
		adapter->native->p1_mmio = NULL;
		pci_release_region(to_pci_dev(adapter->dev.parent), 2);
	}
	if (adapter->native->p2_mmio) {
		iounmap(adapter->native->p2_mmio);
		adapter->native->p2_mmio = NULL;
		pci_release_region(to_pci_dev(adapter->dev.parent), 0);
	}
}

static int cxl_read_vsec(struct cxl *adapter, struct pci_dev *dev)
{
	int vsec;
	u32 afu_desc_off, afu_desc_size;
	u32 ps_off, ps_size;
	u16 vseclen;
	u8 image_state;

	if (!(vsec = find_cxl_vsec(dev))) {
		dev_err(&dev->dev, "ABORTING: CXL VSEC not found!\n");
		return -ENODEV;
	}

	CXL_READ_VSEC_LENGTH(dev, vsec, &vseclen);
	if (vseclen < CXL_VSEC_MIN_SIZE) {
		dev_err(&dev->dev, "ABORTING: CXL VSEC too short\n");
		return -EINVAL;
	}

	CXL_READ_VSEC_STATUS(dev, vsec, &adapter->vsec_status);
	CXL_READ_VSEC_PSL_REVISION(dev, vsec, &adapter->psl_rev);
	CXL_READ_VSEC_CAIA_MAJOR(dev, vsec, &adapter->caia_major);
	CXL_READ_VSEC_CAIA_MINOR(dev, vsec, &adapter->caia_minor);
	CXL_READ_VSEC_BASE_IMAGE(dev, vsec, &adapter->base_image);
	CXL_READ_VSEC_IMAGE_STATE(dev, vsec, &image_state);
	adapter->user_image_loaded = !!(image_state & CXL_VSEC_USER_IMAGE_LOADED);
	adapter->perst_select_user = !!(image_state & CXL_VSEC_USER_IMAGE_LOADED);
	adapter->perst_loads_image = !!(image_state & CXL_VSEC_PERST_LOADS_IMAGE);

	CXL_READ_VSEC_NAFUS(dev, vsec, &adapter->slices);
	CXL_READ_VSEC_AFU_DESC_OFF(dev, vsec, &afu_desc_off);
	CXL_READ_VSEC_AFU_DESC_SIZE(dev, vsec, &afu_desc_size);
	CXL_READ_VSEC_PS_OFF(dev, vsec, &ps_off);
	CXL_READ_VSEC_PS_SIZE(dev, vsec, &ps_size);

	/* Convert everything to bytes, because there is NO WAY I'd look at the
	 * code a month later and forget what units these are in ;-) */
	adapter->native->ps_off = ps_off * 64 * 1024;
	adapter->ps_size = ps_size * 64 * 1024;
	adapter->native->afu_desc_off = afu_desc_off * 64 * 1024;
	adapter->native->afu_desc_size = afu_desc_size * 64 * 1024;

	/* Total IRQs - 1 PSL ERROR - #AFU*(1 slice error + 1 DSI) */
	adapter->user_irqs = pnv_cxl_get_irq_count(dev) - 1 - 2*adapter->slices;

	return 0;
}

/*
 * Workaround a PCIe Host Bridge defect on some cards, that can cause
 * malformed Transaction Layer Packet (TLP) errors to be erroneously
 * reported. Mask this error in the Uncorrectable Error Mask Register.
 *
 * The upper nibble of the PSL revision is used to distinguish between
 * different cards. The affected ones have it set to 0.
 */
static void cxl_fixup_malformed_tlp(struct cxl *adapter, struct pci_dev *dev)
{
	int aer;
	u32 data;

	if (adapter->psl_rev & 0xf000)
		return;
	if (!(aer = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR)))
		return;
	pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_MASK, &data);
	if (data & PCI_ERR_UNC_MALF_TLP)
		if (data & PCI_ERR_UNC_INTN)
			return;
	data |= PCI_ERR_UNC_MALF_TLP;
	data |= PCI_ERR_UNC_INTN;
	pci_write_config_dword(dev, aer + PCI_ERR_UNCOR_MASK, data);
}

static bool cxl_compatible_caia_version(struct cxl *adapter)
{
	if (cxl_is_power8() && (adapter->caia_major == 1))
		return true;

	if (cxl_is_power9() && (adapter->caia_major == 2))
		return true;

	return false;
}

static int cxl_vsec_looks_ok(struct cxl *adapter, struct pci_dev *dev)
{
	if (adapter->vsec_status & CXL_STATUS_SECOND_PORT)
		return -EBUSY;

	if (adapter->vsec_status & CXL_UNSUPPORTED_FEATURES) {
		dev_err(&dev->dev, "ABORTING: CXL requires unsupported features\n");
		return -EINVAL;
	}

	if (!cxl_compatible_caia_version(adapter)) {
		dev_info(&dev->dev, "Ignoring card. PSL type is not supported (caia version: %d)\n",
			 adapter->caia_major);
		return -ENODEV;
	}

	if (!adapter->slices) {
		/* Once we support dynamic reprogramming we can use the card if
		 * it supports loadable AFUs */
		dev_err(&dev->dev, "ABORTING: Device has no AFUs\n");
		return -EINVAL;
	}

	if (!adapter->native->afu_desc_off || !adapter->native->afu_desc_size) {
		dev_err(&dev->dev, "ABORTING: VSEC shows no AFU descriptors\n");
		return -EINVAL;
	}

	if (adapter->ps_size > p2_size(dev) - adapter->native->ps_off) {
		dev_err(&dev->dev, "ABORTING: Problem state size larger than "
				   "available in BAR2: 0x%llx > 0x%llx\n",
			 adapter->ps_size, p2_size(dev) - adapter->native->ps_off);
		return -EINVAL;
	}

	return 0;
}

ssize_t cxl_pci_read_adapter_vpd(struct cxl *adapter, void *buf, size_t len)
{
	return pci_read_vpd(to_pci_dev(adapter->dev.parent), 0, len, buf);
}

static void cxl_release_adapter(struct device *dev)
{
	struct cxl *adapter = to_cxl_adapter(dev);

	pr_devel("cxl_release_adapter\n");

	cxl_remove_adapter_nr(adapter);

	kfree(adapter->native);
	kfree(adapter);
}

#define CXL_PSL_ErrIVTE_tberror (0x1ull << (63-31))

static int sanitise_adapter_regs(struct cxl *adapter)
{
	int rc = 0;

	/* Clear PSL tberror bit by writing 1 to it */
	cxl_p1_write(adapter, CXL_PSL_ErrIVTE, CXL_PSL_ErrIVTE_tberror);

	if (adapter->native->sl_ops->invalidate_all) {
		/* do not invalidate ERAT entries when not reloading on PERST */
		if (cxl_is_power9() && (adapter->perst_loads_image))
			return 0;
		rc = adapter->native->sl_ops->invalidate_all(adapter);
	}

	return rc;
}

/* This should contain *only* operations that can safely be done in
 * both creation and recovery.
 */
static int cxl_configure_adapter(struct cxl *adapter, struct pci_dev *dev)
{
	int rc;

	adapter->dev.parent = &dev->dev;
	adapter->dev.release = cxl_release_adapter;
	pci_set_drvdata(dev, adapter);

	rc = pci_enable_device(dev);
	if (rc) {
		dev_err(&dev->dev, "pci_enable_device failed: %i\n", rc);
		return rc;
	}

	if ((rc = cxl_read_vsec(adapter, dev)))
		return rc;

	if ((rc = cxl_vsec_looks_ok(adapter, dev)))
	        return rc;

	cxl_fixup_malformed_tlp(adapter, dev);

	if ((rc = setup_cxl_bars(dev)))
		return rc;

	if ((rc = switch_card_to_cxl(dev)))
		return rc;

	if ((rc = cxl_update_image_control(adapter)))
		return rc;

	if ((rc = cxl_map_adapter_regs(adapter, dev)))
		return rc;

	if ((rc = sanitise_adapter_regs(adapter)))
		goto err;

	if ((rc = adapter->native->sl_ops->adapter_regs_init(adapter, dev)))
		goto err;

	/* Required for devices using CAPP DMA mode, harmless for others */
	pci_set_master(dev);

	adapter->tunneled_ops_supported = false;

	if (cxl_is_power9()) {
		if (pnv_pci_set_tunnel_bar(dev, 0x00020000E0000000ull, 1))
			dev_info(&dev->dev, "Tunneled operations unsupported\n");
		else
			adapter->tunneled_ops_supported = true;
	}

	if ((rc = pnv_phb_to_cxl_mode(dev, adapter->native->sl_ops->capi_mode)))
		goto err;

	/* If recovery happened, the last step is to turn on snooping.
	 * In the non-recovery case this has no effect */
	if ((rc = pnv_phb_to_cxl_mode(dev, OPAL_PHB_CAPI_MODE_SNOOP_ON)))
		goto err;

	/* Ignore error, adapter init is not dependant on timebase sync */
	cxl_setup_psl_timebase(adapter, dev);

	if ((rc = cxl_native_register_psl_err_irq(adapter)))
		goto err;

	return 0;

err:
	cxl_unmap_adapter_regs(adapter);
	return rc;

}

static void cxl_deconfigure_adapter(struct cxl *adapter)
{
	struct pci_dev *pdev = to_pci_dev(adapter->dev.parent);

	if (cxl_is_power9())
		pnv_pci_set_tunnel_bar(pdev, 0x00020000E0000000ull, 0);

	cxl_native_release_psl_err_irq(adapter);
	cxl_unmap_adapter_regs(adapter);

	pci_disable_device(pdev);
}

static void cxl_stop_trace_psl9(struct cxl *adapter)
{
	int traceid;
	u64 trace_state, trace_mask;
	struct pci_dev *dev = to_pci_dev(adapter->dev.parent);

	/* read each tracearray state and issue mmio to stop them is needed */
	for (traceid = 0; traceid <= CXL_PSL9_TRACEID_MAX; ++traceid) {
		trace_state = cxl_p1_read(adapter, CXL_PSL9_CTCCFG);
		trace_mask = (0x3ULL << (62 - traceid * 2));
		trace_state = (trace_state & trace_mask) >> (62 - traceid * 2);
		dev_dbg(&dev->dev, "cxl: Traceid-%d trace_state=0x%0llX\n",
			traceid, trace_state);

		/* issue mmio if the trace array isn't in FIN state */
		if (trace_state != CXL_PSL9_TRACESTATE_FIN)
			cxl_p1_write(adapter, CXL_PSL9_TRACECFG,
				     0x8400000000000000ULL | traceid);
	}
}

static void cxl_stop_trace_psl8(struct cxl *adapter)
{
	int slice;

	/* Stop the trace */
	cxl_p1_write(adapter, CXL_PSL_TRACE, 0x8000000000000017LL);

	/* Stop the slice traces */
	spin_lock(&adapter->afu_list_lock);
	for (slice = 0; slice < adapter->slices; slice++) {
		if (adapter->afu[slice])
			cxl_p1n_write(adapter->afu[slice], CXL_PSL_SLICE_TRACE,
				      0x8000000000000000LL);
	}
	spin_unlock(&adapter->afu_list_lock);
}

static const struct cxl_service_layer_ops psl9_ops = {
	.adapter_regs_init = init_implementation_adapter_regs_psl9,
	.invalidate_all = cxl_invalidate_all_psl9,
	.afu_regs_init = init_implementation_afu_regs_psl9,
	.sanitise_afu_regs = sanitise_afu_regs_psl9,
	.register_serr_irq = cxl_native_register_serr_irq,
	.release_serr_irq = cxl_native_release_serr_irq,
	.handle_interrupt = cxl_irq_psl9,
	.fail_irq = cxl_fail_irq_psl,
	.activate_dedicated_process = cxl_activate_dedicated_process_psl9,
	.attach_afu_directed = cxl_attach_afu_directed_psl9,
	.attach_dedicated_process = cxl_attach_dedicated_process_psl9,
	.update_dedicated_ivtes = cxl_update_dedicated_ivtes_psl9,
	.debugfs_add_adapter_regs = cxl_debugfs_add_adapter_regs_psl9,
	.debugfs_add_afu_regs = cxl_debugfs_add_afu_regs_psl9,
	.psl_irq_dump_registers = cxl_native_irq_dump_regs_psl9,
	.err_irq_dump_registers = cxl_native_err_irq_dump_regs_psl9,
	.debugfs_stop_trace = cxl_stop_trace_psl9,
	.timebase_read = timebase_read_psl9,
	.capi_mode = OPAL_PHB_CAPI_MODE_CAPI,
	.needs_reset_before_disable = true,
};

static const struct cxl_service_layer_ops psl8_ops = {
	.adapter_regs_init = init_implementation_adapter_regs_psl8,
	.invalidate_all = cxl_invalidate_all_psl8,
	.afu_regs_init = init_implementation_afu_regs_psl8,
	.sanitise_afu_regs = sanitise_afu_regs_psl8,
	.register_serr_irq = cxl_native_register_serr_irq,
	.release_serr_irq = cxl_native_release_serr_irq,
	.handle_interrupt = cxl_irq_psl8,
	.fail_irq = cxl_fail_irq_psl,
	.activate_dedicated_process = cxl_activate_dedicated_process_psl8,
	.attach_afu_directed = cxl_attach_afu_directed_psl8,
	.attach_dedicated_process = cxl_attach_dedicated_process_psl8,
	.update_dedicated_ivtes = cxl_update_dedicated_ivtes_psl8,
	.debugfs_add_adapter_regs = cxl_debugfs_add_adapter_regs_psl8,
	.debugfs_add_afu_regs = cxl_debugfs_add_afu_regs_psl8,
	.psl_irq_dump_registers = cxl_native_irq_dump_regs_psl8,
	.err_irq_dump_registers = cxl_native_err_irq_dump_regs_psl8,
	.debugfs_stop_trace = cxl_stop_trace_psl8,
	.write_timebase_ctrl = write_timebase_ctrl_psl8,
	.timebase_read = timebase_read_psl8,
	.capi_mode = OPAL_PHB_CAPI_MODE_CAPI,
	.needs_reset_before_disable = true,
};

static void set_sl_ops(struct cxl *adapter, struct pci_dev *dev)
{
	if (cxl_is_power8()) {
		dev_info(&dev->dev, "Device uses a PSL8\n");
		adapter->native->sl_ops = &psl8_ops;
	} else {
		dev_info(&dev->dev, "Device uses a PSL9\n");
		adapter->native->sl_ops = &psl9_ops;
	}
}


static struct cxl *cxl_pci_init_adapter(struct pci_dev *dev)
{
	struct cxl *adapter;
	int rc;

	adapter = cxl_alloc_adapter();
	if (!adapter)
		return ERR_PTR(-ENOMEM);

	adapter->native = kzalloc(sizeof(struct cxl_native), GFP_KERNEL);
	if (!adapter->native) {
		rc = -ENOMEM;
		goto err_release;
	}

	set_sl_ops(adapter, dev);

	/* Set defaults for parameters which need to persist over
	 * configure/reconfigure
	 */
	adapter->perst_loads_image = true;
	adapter->perst_same_image = false;

	rc = cxl_configure_adapter(adapter, dev);
	if (rc) {
		pci_disable_device(dev);
		goto err_release;
	}

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

	/* Release the context lock as adapter is configured */
	cxl_adapter_context_unlock(adapter);

	return adapter;

err_put1:
	/* This should mirror cxl_remove_adapter, except without the
	 * sysfs parts
	 */
	cxl_debugfs_adapter_remove(adapter);
	cxl_deconfigure_adapter(adapter);
	device_unregister(&adapter->dev);
	return ERR_PTR(rc);

err_release:
	cxl_release_adapter(&adapter->dev);
	return ERR_PTR(rc);
}

static void cxl_pci_remove_adapter(struct cxl *adapter)
{
	pr_devel("cxl_remove_adapter\n");

	cxl_sysfs_adapter_remove(adapter);
	cxl_debugfs_adapter_remove(adapter);

	/*
	 * Flush adapter datacache as its about to be removed.
	 */
	cxl_data_cache_flush(adapter);

	cxl_deconfigure_adapter(adapter);

	device_unregister(&adapter->dev);
}

#define CXL_MAX_PCIEX_PARENT 2

int cxl_slot_is_switched(struct pci_dev *dev)
{
	struct device_node *np;
	int depth = 0;

	if (!(np = pci_device_to_OF_node(dev))) {
		pr_err("cxl: np = NULL\n");
		return -ENODEV;
	}
	of_node_get(np);
	while (np) {
		np = of_get_next_parent(np);
		if (!of_node_is_type(np, "pciex"))
			break;
		depth++;
	}
	of_node_put(np);
	return (depth > CXL_MAX_PCIEX_PARENT);
}

static int cxl_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct cxl *adapter;
	int slice;
	int rc;

	if (cxl_pci_is_vphb_device(dev)) {
		dev_dbg(&dev->dev, "cxl_init_adapter: Ignoring cxl vphb device\n");
		return -ENODEV;
	}

	if (cxl_slot_is_switched(dev)) {
		dev_info(&dev->dev, "Ignoring card on incompatible PCI slot\n");
		return -ENODEV;
	}

	if (cxl_is_power9() && !radix_enabled()) {
		dev_info(&dev->dev, "Only Radix mode supported\n");
		return -ENODEV;
	}

	if (cxl_verbose)
		dump_cxl_config_space(dev);

	adapter = cxl_pci_init_adapter(dev);
	if (IS_ERR(adapter)) {
		dev_err(&dev->dev, "cxl_init_adapter failed: %li\n", PTR_ERR(adapter));
		return PTR_ERR(adapter);
	}

	for (slice = 0; slice < adapter->slices; slice++) {
		if ((rc = pci_init_afu(adapter, slice, dev))) {
			dev_err(&dev->dev, "AFU %i failed to initialise: %i\n", slice, rc);
			continue;
		}

		rc = cxl_afu_select_best_mode(adapter->afu[slice]);
		if (rc)
			dev_err(&dev->dev, "AFU %i failed to start: %i\n", slice, rc);
	}

	return 0;
}

static void cxl_remove(struct pci_dev *dev)
{
	struct cxl *adapter = pci_get_drvdata(dev);
	struct cxl_afu *afu;
	int i;

	/*
	 * Lock to prevent someone grabbing a ref through the adapter list as
	 * we are removing it
	 */
	for (i = 0; i < adapter->slices; i++) {
		afu = adapter->afu[i];
		cxl_pci_remove_afu(afu);
	}
	cxl_pci_remove_adapter(adapter);
}

static pci_ers_result_t cxl_vphb_error_detected(struct cxl_afu *afu,
						pci_channel_state_t state)
{
	struct pci_dev *afu_dev;
	pci_ers_result_t result = PCI_ERS_RESULT_NEED_RESET;
	pci_ers_result_t afu_result = PCI_ERS_RESULT_NEED_RESET;

	/* There should only be one entry, but go through the list
	 * anyway
	 */
	if (afu->phb == NULL)
		return result;

	list_for_each_entry(afu_dev, &afu->phb->bus->devices, bus_list) {
		if (!afu_dev->driver)
			continue;

		afu_dev->error_state = state;

		if (afu_dev->driver->err_handler)
			afu_result = afu_dev->driver->err_handler->error_detected(afu_dev,
										  state);
		/* Disconnect trumps all, NONE trumps NEED_RESET */
		if (afu_result == PCI_ERS_RESULT_DISCONNECT)
			result = PCI_ERS_RESULT_DISCONNECT;
		else if ((afu_result == PCI_ERS_RESULT_NONE) &&
			 (result == PCI_ERS_RESULT_NEED_RESET))
			result = PCI_ERS_RESULT_NONE;
	}
	return result;
}

static pci_ers_result_t cxl_pci_error_detected(struct pci_dev *pdev,
					       pci_channel_state_t state)
{
	struct cxl *adapter = pci_get_drvdata(pdev);
	struct cxl_afu *afu;
	pci_ers_result_t result = PCI_ERS_RESULT_NEED_RESET, afu_result;
	int i;

	/* At this point, we could still have an interrupt pending.
	 * Let's try to get them out of the way before they do
	 * anything we don't like.
	 */
	schedule();

	/* If we're permanently dead, give up. */
	if (state == pci_channel_io_perm_failure) {
		for (i = 0; i < adapter->slices; i++) {
			afu = adapter->afu[i];
			/*
			 * Tell the AFU drivers; but we don't care what they
			 * say, we're going away.
			 */
			cxl_vphb_error_detected(afu, state);
		}
		return PCI_ERS_RESULT_DISCONNECT;
	}

	/* Are we reflashing?
	 *
	 * If we reflash, we could come back as something entirely
	 * different, including a non-CAPI card. As such, by default
	 * we don't participate in the process. We'll be unbound and
	 * the slot re-probed. (TODO: check EEH doesn't blindly rebind
	 * us!)
	 *
	 * However, this isn't the entire story: for reliablity
	 * reasons, we usually want to reflash the FPGA on PERST in
	 * order to get back to a more reliable known-good state.
	 *
	 * This causes us a bit of a problem: if we reflash we can't
	 * trust that we'll come back the same - we could have a new
	 * image and been PERSTed in order to load that
	 * image. However, most of the time we actually *will* come
	 * back the same - for example a regular EEH event.
	 *
	 * Therefore, we allow the user to assert that the image is
	 * indeed the same and that we should continue on into EEH
	 * anyway.
	 */
	if (adapter->perst_loads_image && !adapter->perst_same_image) {
		/* TODO take the PHB out of CXL mode */
		dev_info(&pdev->dev, "reflashing, so opting out of EEH!\n");
		return PCI_ERS_RESULT_NONE;
	}

	/*
	 * At this point, we want to try to recover.  We'll always
	 * need a complete slot reset: we don't trust any other reset.
	 *
	 * Now, we go through each AFU:
	 *  - We send the driver, if bound, an error_detected callback.
	 *    We expect it to clean up, but it can also tell us to give
	 *    up and permanently detach the card. To simplify things, if
	 *    any bound AFU driver doesn't support EEH, we give up on EEH.
	 *
	 *  - We detach all contexts associated with the AFU. This
	 *    does not free them, but puts them into a CLOSED state
	 *    which causes any the associated files to return useful
	 *    errors to userland. It also unmaps, but does not free,
	 *    any IRQs.
	 *
	 *  - We clean up our side: releasing and unmapping resources we hold
	 *    so we can wire them up again when the hardware comes back up.
	 *
	 * Driver authors should note:
	 *
	 *  - Any contexts you create in your kernel driver (except
	 *    those associated with anonymous file descriptors) are
	 *    your responsibility to free and recreate. Likewise with
	 *    any attached resources.
	 *
	 *  - We will take responsibility for re-initialising the
	 *    device context (the one set up for you in
	 *    cxl_pci_enable_device_hook and accessed through
	 *    cxl_get_context). If you've attached IRQs or other
	 *    resources to it, they remains yours to free.
	 *
	 * You can call the same functions to release resources as you
	 * normally would: we make sure that these functions continue
	 * to work when the hardware is down.
	 *
	 * Two examples:
	 *
	 * 1) If you normally free all your resources at the end of
	 *    each request, or if you use anonymous FDs, your
	 *    error_detected callback can simply set a flag to tell
	 *    your driver not to start any new calls. You can then
	 *    clear the flag in the resume callback.
	 *
	 * 2) If you normally allocate your resources on startup:
	 *     * Set a flag in error_detected as above.
	 *     * Let CXL detach your contexts.
	 *     * In slot_reset, free the old resources and allocate new ones.
	 *     * In resume, clear the flag to allow things to start.
	 */
	for (i = 0; i < adapter->slices; i++) {
		afu = adapter->afu[i];

		afu_result = cxl_vphb_error_detected(afu, state);

		cxl_context_detach_all(afu);
		cxl_ops->afu_deactivate_mode(afu, afu->current_mode);
		pci_deconfigure_afu(afu);

		/* Disconnect trumps all, NONE trumps NEED_RESET */
		if (afu_result == PCI_ERS_RESULT_DISCONNECT)
			result = PCI_ERS_RESULT_DISCONNECT;
		else if ((afu_result == PCI_ERS_RESULT_NONE) &&
			 (result == PCI_ERS_RESULT_NEED_RESET))
			result = PCI_ERS_RESULT_NONE;
	}

	/* should take the context lock here */
	if (cxl_adapter_context_lock(adapter) != 0)
		dev_warn(&adapter->dev,
			 "Couldn't take context lock with %d active-contexts\n",
			 atomic_read(&adapter->contexts_num));

	cxl_deconfigure_adapter(adapter);

	return result;
}

static pci_ers_result_t cxl_pci_slot_reset(struct pci_dev *pdev)
{
	struct cxl *adapter = pci_get_drvdata(pdev);
	struct cxl_afu *afu;
	struct cxl_context *ctx;
	struct pci_dev *afu_dev;
	pci_ers_result_t afu_result = PCI_ERS_RESULT_RECOVERED;
	pci_ers_result_t result = PCI_ERS_RESULT_RECOVERED;
	int i;

	if (cxl_configure_adapter(adapter, pdev))
		goto err;

	/*
	 * Unlock context activation for the adapter. Ideally this should be
	 * done in cxl_pci_resume but cxlflash module tries to activate the
	 * master context as part of slot_reset callback.
	 */
	cxl_adapter_context_unlock(adapter);

	for (i = 0; i < adapter->slices; i++) {
		afu = adapter->afu[i];

		if (pci_configure_afu(afu, adapter, pdev))
			goto err;

		if (cxl_afu_select_best_mode(afu))
			goto err;

		if (afu->phb == NULL)
			continue;

		list_for_each_entry(afu_dev, &afu->phb->bus->devices, bus_list) {
			/* Reset the device context.
			 * TODO: make this less disruptive
			 */
			ctx = cxl_get_context(afu_dev);

			if (ctx && cxl_release_context(ctx))
				goto err;

			ctx = cxl_dev_context_init(afu_dev);
			if (IS_ERR(ctx))
				goto err;

			afu_dev->dev.archdata.cxl_ctx = ctx;

			if (cxl_ops->afu_check_and_enable(afu))
				goto err;

			afu_dev->error_state = pci_channel_io_normal;

			/* If there's a driver attached, allow it to
			 * chime in on recovery. Drivers should check
			 * if everything has come back OK, but
			 * shouldn't start new work until we call
			 * their resume function.
			 */
			if (!afu_dev->driver)
				continue;

			if (afu_dev->driver->err_handler &&
			    afu_dev->driver->err_handler->slot_reset)
				afu_result = afu_dev->driver->err_handler->slot_reset(afu_dev);

			if (afu_result == PCI_ERS_RESULT_DISCONNECT)
				result = PCI_ERS_RESULT_DISCONNECT;
		}
	}
	return result;

err:
	/* All the bits that happen in both error_detected and cxl_remove
	 * should be idempotent, so we don't need to worry about leaving a mix
	 * of unconfigured and reconfigured resources.
	 */
	dev_err(&pdev->dev, "EEH recovery failed. Asking to be disconnected.\n");
	return PCI_ERS_RESULT_DISCONNECT;
}

static void cxl_pci_resume(struct pci_dev *pdev)
{
	struct cxl *adapter = pci_get_drvdata(pdev);
	struct cxl_afu *afu;
	struct pci_dev *afu_dev;
	int i;

	/* Everything is back now. Drivers should restart work now.
	 * This is not the place to be checking if everything came back up
	 * properly, because there's no return value: do that in slot_reset.
	 */
	for (i = 0; i < adapter->slices; i++) {
		afu = adapter->afu[i];

		if (afu->phb == NULL)
			continue;

		list_for_each_entry(afu_dev, &afu->phb->bus->devices, bus_list) {
			if (afu_dev->driver && afu_dev->driver->err_handler &&
			    afu_dev->driver->err_handler->resume)
				afu_dev->driver->err_handler->resume(afu_dev);
		}
	}
}

static const struct pci_error_handlers cxl_err_handler = {
	.error_detected = cxl_pci_error_detected,
	.slot_reset = cxl_pci_slot_reset,
	.resume = cxl_pci_resume,
};

struct pci_driver cxl_pci_driver = {
	.name = "cxl-pci",
	.id_table = cxl_pci_tbl,
	.probe = cxl_probe,
	.remove = cxl_remove,
	.shutdown = cxl_remove,
	.err_handler = &cxl_err_handler,
};
