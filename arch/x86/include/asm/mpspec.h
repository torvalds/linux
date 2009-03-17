#ifndef _ASM_X86_MPSPEC_H
#define _ASM_X86_MPSPEC_H

#include <linux/init.h>

#include <asm/mpspec_def.h>

extern int apic_version[MAX_APICS];
extern int pic_mode;

#ifdef CONFIG_X86_32
#include <mach_mpspec.h>

extern unsigned int def_to_bigsmp;
extern u8 apicid_2_node[];

#ifdef CONFIG_X86_NUMAQ
extern int mp_bus_id_to_node[MAX_MP_BUSSES];
extern int mp_bus_id_to_local[MAX_MP_BUSSES];
extern int quad_local_to_mp_bus_id [NR_CPUS/4][4];
#endif

#define MAX_APICID 256

#else

#define MAX_MP_BUSSES 256
/* Each PCI slot may be a combo card with its own bus.  4 IRQ pins per slot. */
#define MAX_IRQ_SOURCES (MAX_MP_BUSSES * 4)

#endif

extern void early_find_smp_config(void);
extern void early_get_smp_config(void);

#if defined(CONFIG_MCA) || defined(CONFIG_EISA)
extern int mp_bus_id_to_type[MAX_MP_BUSSES];
#endif

extern DECLARE_BITMAP(mp_bus_not_pci, MAX_MP_BUSSES);

extern unsigned int boot_cpu_physical_apicid;
extern unsigned int max_physical_apicid;
extern int smp_found_config;
extern int mpc_default_type;
extern unsigned long mp_lapic_addr;

extern void find_smp_config(void);
extern void get_smp_config(void);
#ifdef CONFIG_X86_MPPARSE
extern void early_reserve_e820_mpc_new(void);
#else
static inline void early_reserve_e820_mpc_new(void) { }
#endif

void __cpuinit generic_processor_info(int apicid, int version);
#ifdef CONFIG_ACPI
extern void mp_register_ioapic(int id, u32 address, u32 gsi_base);
extern void mp_override_legacy_irq(u8 bus_irq, u8 polarity, u8 trigger,
				   u32 gsi);
extern void mp_config_acpi_legacy_irqs(void);
extern int mp_register_gsi(u32 gsi, int edge_level, int active_high_low);
extern int acpi_probe_gsi(void);
#ifdef CONFIG_X86_IO_APIC
extern int mp_config_acpi_gsi(unsigned char number, unsigned int devfn, u8 pin,
				u32 gsi, int triggering, int polarity);
#else
static inline int
mp_config_acpi_gsi(unsigned char number, unsigned int devfn, u8 pin,
		   u32 gsi, int triggering, int polarity)
{
	return 0;
}
#endif
#else /* !CONFIG_ACPI: */
static inline int acpi_probe_gsi(void)
{
	return 0;
}
#endif /* CONFIG_ACPI */

#define PHYSID_ARRAY_SIZE	BITS_TO_LONGS(MAX_APICS)

struct physid_mask {
	unsigned long mask[PHYSID_ARRAY_SIZE];
};

typedef struct physid_mask physid_mask_t;

#define physid_set(physid, map)			set_bit(physid, (map).mask)
#define physid_clear(physid, map)		clear_bit(physid, (map).mask)
#define physid_isset(physid, map)		test_bit(physid, (map).mask)
#define physid_test_and_set(physid, map)			\
	test_and_set_bit(physid, (map).mask)

#define physids_and(dst, src1, src2)					\
	bitmap_and((dst).mask, (src1).mask, (src2).mask, MAX_APICS)

#define physids_or(dst, src1, src2)					\
	bitmap_or((dst).mask, (src1).mask, (src2).mask, MAX_APICS)

#define physids_clear(map)					\
	bitmap_zero((map).mask, MAX_APICS)

#define physids_complement(dst, src)				\
	bitmap_complement((dst).mask, (src).mask, MAX_APICS)

#define physids_empty(map)					\
	bitmap_empty((map).mask, MAX_APICS)

#define physids_equal(map1, map2)				\
	bitmap_equal((map1).mask, (map2).mask, MAX_APICS)

#define physids_weight(map)					\
	bitmap_weight((map).mask, MAX_APICS)

#define physids_shift_right(d, s, n)				\
	bitmap_shift_right((d).mask, (s).mask, n, MAX_APICS)

#define physids_shift_left(d, s, n)				\
	bitmap_shift_left((d).mask, (s).mask, n, MAX_APICS)

#define physids_coerce(map)			((map).mask[0])

#define physids_promote(physids)					\
	({								\
		physid_mask_t __physid_mask = PHYSID_MASK_NONE;		\
		__physid_mask.mask[0] = physids;			\
		__physid_mask;						\
	})

/* Note: will create very large stack frames if physid_mask_t is big */
#define physid_mask_of_physid(physid)					\
	({								\
		physid_mask_t __physid_mask = PHYSID_MASK_NONE;		\
		physid_set(physid, __physid_mask);			\
		__physid_mask;						\
	})

static inline void physid_set_mask_of_physid(int physid, physid_mask_t *map)
{
	physids_clear(*map);
	physid_set(physid, *map);
}

#define PHYSID_MASK_ALL		{ {[0 ... PHYSID_ARRAY_SIZE-1] = ~0UL} }
#define PHYSID_MASK_NONE	{ {[0 ... PHYSID_ARRAY_SIZE-1] = 0UL} }

extern physid_mask_t phys_cpu_present_map;

#endif /* _ASM_X86_MPSPEC_H */
