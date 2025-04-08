/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_AMD_NB_H
#define _ASM_X86_AMD_NB_H

#include <linux/ioport.h>
#include <linux/pci.h>
#include <asm/amd_node.h>

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

struct amd_l3_cache {
	unsigned indices;
	u8	 subcaches[4];
};

struct amd_northbridge {
	struct pci_dev *misc;
	struct pci_dev *link;
	struct amd_l3_cache l3_cache;
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
static inline struct amd_northbridge *node_to_amd_nb(int node)
{
	return NULL;
}
#define amd_gart_present(x)	false

#endif


#endif /* _ASM_X86_AMD_NB_H */
