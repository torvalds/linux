// SPDX-License-Identifier: GPL-2.0-only
/*
 * Shared support code for AMD K8 northbridges and derivatives.
 * Copyright 2006 Andi Kleen, SUSE Labs.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/pci_ids.h>

#include <asm/amd/nb.h>
#include <asm/cpuid/api.h>

static u32 *flush_words;

static const struct pci_device_id amd_nb_misc_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MISC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_MISC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_NB_F3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_M10H_F3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_M30H_NB_F3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_15H_M60H_NB_F3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_16H_NB_F3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_16H_M30H_NB_F3) },
	{}
};

const struct amd_nb_bus_dev_range amd_nb_bus_dev_ranges[] __initconst = {
	{ 0x00, 0x18, 0x20 },
	{ 0xff, 0x00, 0x20 },
	{ 0xfe, 0x00, 0x20 },
	{ }
};

static struct amd_northbridge_info amd_northbridges;

u16 amd_nb_num(void)
{
	return amd_northbridges.num;
}
EXPORT_SYMBOL_GPL(amd_nb_num);

bool amd_nb_has_feature(unsigned int feature)
{
	return ((amd_northbridges.flags & feature) == feature);
}
EXPORT_SYMBOL_GPL(amd_nb_has_feature);

struct amd_northbridge *node_to_amd_nb(int node)
{
	return (node < amd_northbridges.num) ? &amd_northbridges.nb[node] : NULL;
}
EXPORT_SYMBOL_GPL(node_to_amd_nb);

static int amd_cache_northbridges(void)
{
	struct amd_northbridge *nb;
	u16 i;

	if (amd_northbridges.num)
		return 0;

	amd_northbridges.num = amd_num_nodes();

	nb = kcalloc(amd_northbridges.num, sizeof(struct amd_northbridge), GFP_KERNEL);
	if (!nb)
		return -ENOMEM;

	amd_northbridges.nb = nb;

	for (i = 0; i < amd_northbridges.num; i++) {
		node_to_amd_nb(i)->misc = amd_node_get_func(i, 3);

		/*
		 * Each Northbridge must have a 'misc' device.
		 * If not, then uninitialize everything.
		 */
		if (!node_to_amd_nb(i)->misc) {
			amd_northbridges.num = 0;
			kfree(nb);
			return -ENODEV;
		}

		node_to_amd_nb(i)->link = amd_node_get_func(i, 4);
	}

	if (amd_gart_present())
		amd_northbridges.flags |= AMD_NB_GART;

	if (!cpuid_amd_hygon_has_l3_cache())
		return 0;

	/*
	 * Some CPU families support L3 Cache Index Disable. There are some
	 * limitations because of E382 and E388 on family 0x10.
	 */
	if (boot_cpu_data.x86 == 0x10 &&
	    boot_cpu_data.x86_model >= 0x8 &&
	    (boot_cpu_data.x86_model > 0x9 ||
	     boot_cpu_data.x86_stepping >= 0x1))
		amd_northbridges.flags |= AMD_NB_L3_INDEX_DISABLE;

	if (boot_cpu_data.x86 == 0x15)
		amd_northbridges.flags |= AMD_NB_L3_INDEX_DISABLE;

	/* L3 cache partitioning is supported on family 0x15 */
	if (boot_cpu_data.x86 == 0x15)
		amd_northbridges.flags |= AMD_NB_L3_PARTITIONING;

	return 0;
}

/*
 * Ignores subdevice/subvendor but as far as I can figure out
 * they're useless anyways
 */
bool __init early_is_amd_nb(u32 device)
{
	const struct pci_device_id *id;
	u32 vendor = device & 0xffff;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD &&
	    boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
		return false;

	if (cpu_feature_enabled(X86_FEATURE_ZEN))
		return false;

	device >>= 16;
	for (id = amd_nb_misc_ids; id->vendor; id++)
		if (vendor == id->vendor && device == id->device)
			return true;
	return false;
}

struct resource *amd_get_mmconfig_range(struct resource *res)
{
	u64 base, msr;
	unsigned int segn_busn_bits;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD &&
	    boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
		return NULL;

	/* Assume CPUs from Fam10h have mmconfig, although not all VMs do */
	if (boot_cpu_data.x86 < 0x10 ||
	    rdmsrq_safe(MSR_FAM10H_MMIO_CONF_BASE, &msr))
		return NULL;

	/* mmconfig is not enabled */
	if (!(msr & FAM10H_MMIO_CONF_ENABLE))
		return NULL;

	base = msr & (FAM10H_MMIO_CONF_BASE_MASK<<FAM10H_MMIO_CONF_BASE_SHIFT);

	segn_busn_bits = (msr >> FAM10H_MMIO_CONF_BUSRANGE_SHIFT) &
			 FAM10H_MMIO_CONF_BUSRANGE_MASK;

	res->flags = IORESOURCE_MEM;
	res->start = base;
	res->end = base + (1ULL<<(segn_busn_bits + 20)) - 1;
	return res;
}

int amd_get_subcaches(int cpu)
{
	struct pci_dev *link = node_to_amd_nb(topology_amd_node_id(cpu))->link;
	unsigned int mask;

	if (!amd_nb_has_feature(AMD_NB_L3_PARTITIONING))
		return 0;

	pci_read_config_dword(link, 0x1d4, &mask);

	return (mask >> (4 * cpu_data(cpu).topo.core_id)) & 0xf;
}

int amd_set_subcaches(int cpu, unsigned long mask)
{
	static unsigned int reset, ban;
	struct amd_northbridge *nb = node_to_amd_nb(topology_amd_node_id(cpu));
	unsigned int reg;
	int cuid;

	if (!amd_nb_has_feature(AMD_NB_L3_PARTITIONING) || mask > 0xf)
		return -EINVAL;

	/* if necessary, collect reset state of L3 partitioning and BAN mode */
	if (reset == 0) {
		pci_read_config_dword(nb->link, 0x1d4, &reset);
		pci_read_config_dword(nb->misc, 0x1b8, &ban);
		ban &= 0x180000;
	}

	/* deactivate BAN mode if any subcaches are to be disabled */
	if (mask != 0xf) {
		pci_read_config_dword(nb->misc, 0x1b8, &reg);
		pci_write_config_dword(nb->misc, 0x1b8, reg & ~0x180000);
	}

	cuid = cpu_data(cpu).topo.core_id;
	mask <<= 4 * cuid;
	mask |= (0xf ^ (1 << cuid)) << 26;

	pci_write_config_dword(nb->link, 0x1d4, mask);

	/* reset BAN mode if L3 partitioning returned to reset state */
	pci_read_config_dword(nb->link, 0x1d4, &reg);
	if (reg == reset) {
		pci_read_config_dword(nb->misc, 0x1b8, &reg);
		reg &= ~0x180000;
		pci_write_config_dword(nb->misc, 0x1b8, reg | ban);
	}

	return 0;
}

static void amd_cache_gart(void)
{
	u16 i;

	if (!amd_nb_has_feature(AMD_NB_GART))
		return;

	flush_words = kmalloc_array(amd_northbridges.num, sizeof(u32), GFP_KERNEL);
	if (!flush_words) {
		amd_northbridges.flags &= ~AMD_NB_GART;
		pr_notice("Cannot initialize GART flush words, GART support disabled\n");
		return;
	}

	for (i = 0; i != amd_northbridges.num; i++)
		pci_read_config_dword(node_to_amd_nb(i)->misc, 0x9c, &flush_words[i]);
}

void amd_flush_garts(void)
{
	int flushed, i;
	unsigned long flags;
	static DEFINE_SPINLOCK(gart_lock);

	if (!amd_nb_has_feature(AMD_NB_GART))
		return;

	/*
	 * Avoid races between AGP and IOMMU. In theory it's not needed
	 * but I'm not sure if the hardware won't lose flush requests
	 * when another is pending. This whole thing is so expensive anyways
	 * that it doesn't matter to serialize more. -AK
	 */
	spin_lock_irqsave(&gart_lock, flags);
	flushed = 0;
	for (i = 0; i < amd_northbridges.num; i++) {
		pci_write_config_dword(node_to_amd_nb(i)->misc, 0x9c,
				       flush_words[i] | 1);
		flushed++;
	}
	for (i = 0; i < amd_northbridges.num; i++) {
		u32 w;
		/* Make sure the hardware actually executed the flush*/
		for (;;) {
			pci_read_config_dword(node_to_amd_nb(i)->misc,
					      0x9c, &w);
			if (!(w & 1))
				break;
			cpu_relax();
		}
	}
	spin_unlock_irqrestore(&gart_lock, flags);
	if (!flushed)
		pr_notice("nothing to flush?\n");
}
EXPORT_SYMBOL_GPL(amd_flush_garts);

static void __fix_erratum_688(void *info)
{
#define MSR_AMD64_IC_CFG 0xC0011021

	msr_set_bit(MSR_AMD64_IC_CFG, 3);
	msr_set_bit(MSR_AMD64_IC_CFG, 14);
}

/* Apply erratum 688 fix so machines without a BIOS fix work. */
static __init void fix_erratum_688(void)
{
	struct pci_dev *F4;
	u32 val;

	if (boot_cpu_data.x86 != 0x14)
		return;

	if (!amd_northbridges.num)
		return;

	F4 = node_to_amd_nb(0)->link;
	if (!F4)
		return;

	if (pci_read_config_dword(F4, 0x164, &val))
		return;

	if (val & BIT(2))
		return;

	on_each_cpu(__fix_erratum_688, NULL, 0);

	pr_info("x86/cpu/AMD: CPU erratum 688 worked around\n");
}

static __init int init_amd_nbs(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD &&
	    boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
		return 0;

	amd_cache_northbridges();
	amd_cache_gart();

	fix_erratum_688();

	return 0;
}

/* This has to go after the PCI subsystem */
fs_initcall(init_amd_nbs);
