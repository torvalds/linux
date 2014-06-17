#ifndef _ASM_X86_MPSPEC_H
#define _ASM_X86_MPSPEC_H


#include <asm/mpspec_def.h>
#include <asm/x86_init.h>
#include <asm/apicdef.h>

extern int apic_version[];
extern int pic_mode;

#ifdef CONFIG_X86_32

/*
 * Summit or generic (i.e. installer) kernels need lots of bus entries.
 * Maximum 256 PCI busses, plus 1 ISA bus in each of 4 cabinets.
 */
#if CONFIG_BASE_SMALL == 0
# define MAX_MP_BUSSES		260
#else
# define MAX_MP_BUSSES		32
#endif

#define MAX_IRQ_SOURCES		256

extern unsigned int def_to_bigsmp;

#else /* CONFIG_X86_64: */

#define MAX_MP_BUSSES		256
/* Each PCI slot may be a combo card with its own bus.  4 IRQ pins per slot. */
#define MAX_IRQ_SOURCES		(MAX_MP_BUSSES * 4)

#endif /* CONFIG_X86_64 */

#ifdef CONFIG_EISA
extern int mp_bus_id_to_type[MAX_MP_BUSSES];
#endif

extern DECLARE_BITMAP(mp_bus_not_pci, MAX_MP_BUSSES);

extern unsigned int boot_cpu_physical_apicid;
extern unsigned int max_physical_apicid;
extern int mpc_default_type;
extern unsigned long mp_lapic_addr;

#ifdef CONFIG_X86_LOCAL_APIC
extern int smp_found_config;
#else
# define smp_found_config 0
#endif

static inline void get_smp_config(void)
{
	x86_init.mpparse.get_smp_config(0);
}

static inline void early_get_smp_config(void)
{
	x86_init.mpparse.get_smp_config(1);
}

static inline void find_smp_config(void)
{
	x86_init.mpparse.find_smp_config();
}

#ifdef CONFIG_X86_MPPARSE
extern void early_reserve_e820_mpc_new(void);
extern int enable_update_mptable;
extern int default_mpc_apic_id(struct mpc_cpu *m);
extern void default_smp_read_mpc_oem(struct mpc_table *mpc);
# ifdef CONFIG_X86_IO_APIC
extern void default_mpc_oem_bus_info(struct mpc_bus *m, char *str);
# else
#  define default_mpc_oem_bus_info NULL
# endif
extern void default_find_smp_config(void);
extern void default_get_smp_config(unsigned int early);
#else
static inline void early_reserve_e820_mpc_new(void) { }
#define enable_update_mptable 0
#define default_mpc_apic_id NULL
#define default_smp_read_mpc_oem NULL
#define default_mpc_oem_bus_info NULL
#define default_find_smp_config x86_init_noop
#define default_get_smp_config x86_init_uint_noop
#endif

int generic_processor_info(int apicid, int version);
#ifdef CONFIG_ACPI
extern void mp_register_ioapic(int id, u32 address, u32 gsi_base);
extern void mp_override_legacy_irq(u8 bus_irq, u8 polarity, u8 trigger,
				   u32 gsi);
extern void mp_config_acpi_legacy_irqs(void);
struct device;
extern int mp_register_gsi(struct device *dev, u32 gsi, int edge_level,
				 int active_high_low);
#endif /* CONFIG_ACPI */

#define PHYSID_ARRAY_SIZE	BITS_TO_LONGS(MAX_LOCAL_APIC)

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
	bitmap_and((dst).mask, (src1).mask, (src2).mask, MAX_LOCAL_APIC)

#define physids_or(dst, src1, src2)					\
	bitmap_or((dst).mask, (src1).mask, (src2).mask, MAX_LOCAL_APIC)

#define physids_clear(map)					\
	bitmap_zero((map).mask, MAX_LOCAL_APIC)

#define physids_complement(dst, src)				\
	bitmap_complement((dst).mask, (src).mask, MAX_LOCAL_APIC)

#define physids_empty(map)					\
	bitmap_empty((map).mask, MAX_LOCAL_APIC)

#define physids_equal(map1, map2)				\
	bitmap_equal((map1).mask, (map2).mask, MAX_LOCAL_APIC)

#define physids_weight(map)					\
	bitmap_weight((map).mask, MAX_LOCAL_APIC)

#define physids_shift_right(d, s, n)				\
	bitmap_shift_right((d).mask, (s).mask, n, MAX_LOCAL_APIC)

#define physids_shift_left(d, s, n)				\
	bitmap_shift_left((d).mask, (s).mask, n, MAX_LOCAL_APIC)

static inline unsigned long physids_coerce(physid_mask_t *map)
{
	return map->mask[0];
}

static inline void physids_promote(unsigned long physids, physid_mask_t *map)
{
	physids_clear(*map);
	map->mask[0] = physids;
}

static inline void physid_set_mask_of_physid(int physid, physid_mask_t *map)
{
	physids_clear(*map);
	physid_set(physid, *map);
}

#define PHYSID_MASK_ALL		{ {[0 ... PHYSID_ARRAY_SIZE-1] = ~0UL} }
#define PHYSID_MASK_NONE	{ {[0 ... PHYSID_ARRAY_SIZE-1] = 0UL} }

extern physid_mask_t phys_cpu_present_map;

extern int generic_mps_oem_check(struct mpc_table *, char *, char *);

extern int default_acpi_madt_oem_check(char *, char *);

#endif /* _ASM_X86_MPSPEC_H */
