#ifndef _ASM_X86_IO_APIC_H
#define _ASM_X86_IO_APIC_H

#include <linux/types.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>
#include <asm/irq_vectors.h>
#include <asm/x86_init.h>
/*
 * Intel IO-APIC support for SMP and UP systems.
 *
 * Copyright (C) 1997, 1998, 1999, 2000 Ingo Molnar
 */

/* I/O Unit Redirection Table */
#define IO_APIC_REDIR_VECTOR_MASK	0x000FF
#define IO_APIC_REDIR_DEST_LOGICAL	0x00800
#define IO_APIC_REDIR_DEST_PHYSICAL	0x00000
#define IO_APIC_REDIR_SEND_PENDING	(1 << 12)
#define IO_APIC_REDIR_REMOTE_IRR	(1 << 14)
#define IO_APIC_REDIR_LEVEL_TRIGGER	(1 << 15)
#define IO_APIC_REDIR_MASKED		(1 << 16)

/*
 * The structure of the IO-APIC:
 */
union IO_APIC_reg_00 {
	u32	raw;
	struct {
		u32	__reserved_2	: 14,
			LTS		:  1,
			delivery_type	:  1,
			__reserved_1	:  8,
			ID		:  8;
	} __attribute__ ((packed)) bits;
};

union IO_APIC_reg_01 {
	u32	raw;
	struct {
		u32	version		:  8,
			__reserved_2	:  7,
			PRQ		:  1,
			entries		:  8,
			__reserved_1	:  8;
	} __attribute__ ((packed)) bits;
};

union IO_APIC_reg_02 {
	u32	raw;
	struct {
		u32	__reserved_2	: 24,
			arbitration	:  4,
			__reserved_1	:  4;
	} __attribute__ ((packed)) bits;
};

union IO_APIC_reg_03 {
	u32	raw;
	struct {
		u32	boot_DT		:  1,
			__reserved_1	: 31;
	} __attribute__ ((packed)) bits;
};

struct IO_APIC_route_entry {
	__u32	vector		:  8,
		delivery_mode	:  3,	/* 000: FIXED
					 * 001: lowest prio
					 * 111: ExtINT
					 */
		dest_mode	:  1,	/* 0: physical, 1: logical */
		delivery_status	:  1,
		polarity	:  1,
		irr		:  1,
		trigger		:  1,	/* 0: edge, 1: level */
		mask		:  1,	/* 0: enabled, 1: disabled */
		__reserved_2	: 15;

	__u32	__reserved_3	: 24,
		dest		:  8;
} __attribute__ ((packed));

struct IR_IO_APIC_route_entry {
	__u64	vector		: 8,
		zero		: 3,
		index2		: 1,
		delivery_status : 1,
		polarity	: 1,
		irr		: 1,
		trigger		: 1,
		mask		: 1,
		reserved	: 31,
		format		: 1,
		index		: 15;
} __attribute__ ((packed));

#define IOAPIC_AUTO     -1
#define IOAPIC_EDGE     0
#define IOAPIC_LEVEL    1
#define	IOAPIC_MAP_ALLOC		0x1
#define	IOAPIC_MAP_CHECK		0x2

#ifdef CONFIG_X86_IO_APIC

/*
 * # of IO-APICs and # of IRQ routing registers
 */
extern int nr_ioapics;

extern int mpc_ioapic_id(int ioapic);
extern unsigned int mpc_ioapic_addr(int ioapic);
extern struct mp_ioapic_gsi *mp_ioapic_gsi_routing(int ioapic);

#define MP_MAX_IOAPIC_PIN 127

/* # of MP IRQ source entries */
extern int mp_irq_entries;

/* MP IRQ source entries */
extern struct mpc_intsrc mp_irqs[MAX_IRQ_SOURCES];

/* Older SiS APIC requires we rewrite the index register */
extern int sis_apic_bug;

/* 1 if "noapic" boot option passed */
extern int skip_ioapic_setup;

/* 1 if "noapic" boot option passed */
extern int noioapicquirk;

/* -1 if "noapic" boot option passed */
extern int noioapicreroute;

extern unsigned long io_apic_irqs;

#define IO_APIC_IRQ(x) (((x) >= NR_IRQS_LEGACY) || ((1 << (x)) & io_apic_irqs))

/*
 * If we use the IO-APIC for IRQ routing, disable automatic
 * assignment of PCI IRQ's.
 */
#define io_apic_assign_pci_irqs \
	(mp_irq_entries && !skip_ioapic_setup && io_apic_irqs)

struct irq_cfg;
extern void ioapic_insert_resources(void);

extern int native_setup_ioapic_entry(int, struct IO_APIC_route_entry *,
				     unsigned int, int,
				     struct io_apic_irq_attr *);
extern void eoi_ioapic_irq(unsigned int irq, struct irq_cfg *cfg);

extern void native_eoi_ioapic_pin(int apic, int pin, int vector);

extern int save_ioapic_entries(void);
extern void mask_ioapic_entries(void);
extern int restore_ioapic_entries(void);

extern void setup_ioapic_ids_from_mpc(void);
extern void setup_ioapic_ids_from_mpc_nocheck(void);

struct io_apic_irq_attr {
	int ioapic;
	int ioapic_pin;
	int trigger;
	int polarity;
};

enum ioapic_domain_type {
	IOAPIC_DOMAIN_INVALID,
	IOAPIC_DOMAIN_LEGACY,
	IOAPIC_DOMAIN_STRICT,
	IOAPIC_DOMAIN_DYNAMIC,
};

struct device_node;
struct irq_domain;
struct irq_domain_ops;

struct ioapic_domain_cfg {
	enum ioapic_domain_type		type;
	const struct irq_domain_ops	*ops;
	struct device_node		*dev;
};

struct mp_ioapic_gsi{
	u32 gsi_base;
	u32 gsi_end;
};
extern u32 gsi_top;

extern int mp_find_ioapic(u32 gsi);
extern int mp_find_ioapic_pin(int ioapic, u32 gsi);
extern u32 mp_pin_to_gsi(int ioapic, int pin);
extern int mp_map_gsi_to_irq(u32 gsi, unsigned int flags);
extern void mp_unmap_irq(int irq);
extern int mp_register_ioapic(int id, u32 address, u32 gsi_base,
			      struct ioapic_domain_cfg *cfg);
extern int mp_unregister_ioapic(u32 gsi_base);
extern int mp_ioapic_registered(u32 gsi_base);
extern int mp_irqdomain_map(struct irq_domain *domain, unsigned int virq,
			    irq_hw_number_t hwirq);
extern void mp_irqdomain_unmap(struct irq_domain *domain, unsigned int virq);
extern int mp_set_gsi_attr(u32 gsi, int trigger, int polarity, int node);
extern void __init pre_init_apic_IRQ0(void);

extern void mp_save_irq(struct mpc_intsrc *m);

extern void disable_ioapic_support(void);

extern void __init native_io_apic_init_mappings(void);
extern unsigned int native_io_apic_read(unsigned int apic, unsigned int reg);
extern void native_io_apic_write(unsigned int apic, unsigned int reg, unsigned int val);
extern void native_io_apic_modify(unsigned int apic, unsigned int reg, unsigned int val);
extern void native_disable_io_apic(void);
extern void native_io_apic_print_entries(unsigned int apic, unsigned int nr_entries);
extern void intel_ir_io_apic_print_entries(unsigned int apic, unsigned int nr_entries);
extern int native_ioapic_set_affinity(struct irq_data *,
				      const struct cpumask *,
				      bool);

static inline unsigned int io_apic_read(unsigned int apic, unsigned int reg)
{
	return x86_io_apic_ops.read(apic, reg);
}

static inline void io_apic_write(unsigned int apic, unsigned int reg, unsigned int value)
{
	x86_io_apic_ops.write(apic, reg, value);
}
static inline void io_apic_modify(unsigned int apic, unsigned int reg, unsigned int value)
{
	x86_io_apic_ops.modify(apic, reg, value);
}

extern void io_apic_eoi(unsigned int apic, unsigned int vector);

extern void setup_IO_APIC(void);
extern void enable_IO_APIC(void);
extern void disable_IO_APIC(void);
extern void setup_ioapic_dest(void);
extern int IO_APIC_get_PCI_irq_vector(int bus, int devfn, int pin);
extern void print_IO_APICs(void);
#else  /* !CONFIG_X86_IO_APIC */

#define IO_APIC_IRQ(x)		0
#define io_apic_assign_pci_irqs 0
#define setup_ioapic_ids_from_mpc x86_init_noop
static inline void ioapic_insert_resources(void) { }
static inline void print_IO_APICs(void) {}
#define gsi_top (NR_IRQS_LEGACY)
static inline int mp_find_ioapic(u32 gsi) { return 0; }
static inline u32 mp_pin_to_gsi(int ioapic, int pin) { return UINT_MAX; }
static inline int mp_map_gsi_to_irq(u32 gsi, unsigned int flags) { return gsi; }
static inline void mp_unmap_irq(int irq) { }

static inline int save_ioapic_entries(void)
{
	return -ENOMEM;
}

static inline void mask_ioapic_entries(void) { }
static inline int restore_ioapic_entries(void)
{
	return -ENOMEM;
}

static inline void mp_save_irq(struct mpc_intsrc *m) { };
static inline void disable_ioapic_support(void) { }
#define native_io_apic_init_mappings	NULL
#define native_io_apic_read		NULL
#define native_io_apic_write		NULL
#define native_io_apic_modify		NULL
#define native_disable_io_apic		NULL
#define native_io_apic_print_entries	NULL
#define native_ioapic_set_affinity	NULL
#define native_setup_ioapic_entry	NULL
#define native_eoi_ioapic_pin		NULL
#endif

#endif /* _ASM_X86_IO_APIC_H */
