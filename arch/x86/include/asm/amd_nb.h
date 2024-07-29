/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_AMD_NB_H
#define _ASM_X86_AMD_NB_H

#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/refcount.h>

struct amd_nb_bus_dev_range {
	u8 bus;
	u8 dev_base;
	u8 dev_limit;
};

extern const struct amd_nb_bus_dev_range amd_nb_bus_dev_ranges[];

extern bool early_is_amd_nb(u32 value);
extern struct resource *amd_get_mmconfig_range(struct resource *res);
extern void amd_flush_garts(void);
extern int amd_numa_init(void);
extern int amd_get_subcaches(int);
extern int amd_set_subcaches(int, unsigned long);

int __must_check amd_smn_read(u16 node, u32 address, u32 *value);
int __must_check amd_smn_write(u16 node, u32 address, u32 value);

struct amd_l3_cache {
	unsigned indices;
	u8	 subcaches[4];
};

struct threshold_block {
	unsigned int	 block;			/* Number within bank */
	unsigned int	 bank;			/* MCA bank the block belongs to */
	unsigned int	 cpu;			/* CPU which controls MCA bank */
	u32		 address;		/* MSR address for the block */
	u16		 interrupt_enable;	/* Enable/Disable APIC interrupt */
	bool		 interrupt_capable;	/* Bank can generate an interrupt. */

	u16		 threshold_limit;	/*
						 * Value upon which threshold
						 * interrupt is generated.
						 */

	struct kobject	 kobj;			/* sysfs object */
	struct list_head miscj;			/*
						 * List of threshold blocks
						 * within a bank.
						 */
};

struct threshold_bank {
	struct kobject		*kobj;
	struct threshold_block	*blocks;

	/* initialized to the number of CPUs on the node sharing this bank */
	refcount_t		cpus;
	unsigned int		shared;
};

struct amd_northbridge {
	struct pci_dev *root;
	struct pci_dev *misc;
	struct pci_dev *link;
	struct amd_l3_cache l3_cache;
	struct threshold_bank *bank4;
};

struct amd_northbridge_info {
	u16 num;
	u64 flags;
	struct amd_northbridge *nb;
};

#define AMD_NB_GART			BIT(0)
#define AMD_NB_L3_INDEX_DISABLE		BIT(1)
#define AMD_NB_L3_PARTITIONING		BIT(2)

#ifdef CONFIG_AMD_NB

u16 amd_nb_num(void);
bool amd_nb_has_feature(unsigned int feature);
struct amd_northbridge *node_to_amd_nb(int node);

static inline u16 amd_pci_dev_to_node_id(struct pci_dev *pdev)
{
	struct pci_dev *misc;
	int i;

	for (i = 0; i != amd_nb_num(); i++) {
		misc = node_to_amd_nb(i)->misc;

		if (pci_domain_nr(misc->bus) == pci_domain_nr(pdev->bus) &&
		    PCI_SLOT(misc->devfn) == PCI_SLOT(pdev->devfn))
			return i;
	}

	WARN(1, "Unable to find AMD Northbridge id for %s\n", pci_name(pdev));
	return 0;
}

static inline bool amd_gart_present(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return false;

	/* GART present only on Fam15h, up to model 0fh */
	if (boot_cpu_data.x86 == 0xf || boot_cpu_data.x86 == 0x10 ||
	    (boot_cpu_data.x86 == 0x15 && boot_cpu_data.x86_model < 0x10))
		return true;

	return false;
}

#else

#define amd_nb_num(x)		0
#define amd_nb_has_feature(x)	false
#define node_to_amd_nb(x)	NULL
#define amd_gart_present(x)	false

#endif


#endif /* _ASM_X86_AMD_NB_H */
