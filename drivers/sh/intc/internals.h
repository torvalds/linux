/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/sh_intc.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/radix-tree.h>
#include <linux/device.h>

#define _INTC_MK(fn, mode, addr_e, addr_d, width, shift) \
	((shift) | ((width) << 5) | ((fn) << 9) | ((mode) << 13) | \
	 ((addr_e) << 16) | ((addr_d << 24)))

#define _INTC_SHIFT(h)		(h & 0x1f)
#define _INTC_WIDTH(h)		((h >> 5) & 0xf)
#define _INTC_FN(h)		((h >> 9) & 0xf)
#define _INTC_MODE(h)		((h >> 13) & 0x7)
#define _INTC_ADDR_E(h)		((h >> 16) & 0xff)
#define _INTC_ADDR_D(h)		((h >> 24) & 0xff)

#ifdef CONFIG_SMP
#define IS_SMP(x)		(x.smp)
#define INTC_REG(d, x, c)	(d->reg[(x)] + ((d->smp[(x)] & 0xff) * c))
#define SMP_NR(d, x)		((d->smp[(x)] >> 8) ? (d->smp[(x)] >> 8) : 1)
#else
#define IS_SMP(x)		0
#define INTC_REG(d, x, c)	(d->reg[(x)])
#define SMP_NR(d, x)		1
#endif

struct intc_handle_int {
	unsigned int irq;
	unsigned long handle;
};

struct intc_window {
	phys_addr_t phys;
	void __iomem *virt;
	unsigned long size;
};

struct intc_map_entry {
	intc_enum enum_id;
	struct intc_desc_int *desc;
};

struct intc_subgroup_entry {
	unsigned int pirq;
	intc_enum enum_id;
	unsigned long handle;
};

struct intc_desc_int {
	struct list_head list;
	struct device dev;
	struct radix_tree_root tree;
	raw_spinlock_t lock;
	unsigned int index;
	unsigned long *reg;
#ifdef CONFIG_SMP
	unsigned long *smp;
#endif
	unsigned int nr_reg;
	struct intc_handle_int *prio;
	unsigned int nr_prio;
	struct intc_handle_int *sense;
	unsigned int nr_sense;
	struct intc_window *window;
	unsigned int nr_windows;
	struct irq_domain *domain;
	struct irq_chip chip;
	bool skip_suspend;
};


enum {
	REG_FN_ERR = 0,
	REG_FN_TEST_BASE = 1,
	REG_FN_WRITE_BASE = 5,
	REG_FN_MODIFY_BASE = 9
};

enum {	MODE_ENABLE_REG = 0, /* Bit(s) set -> interrupt enabled */
	MODE_MASK_REG,       /* Bit(s) set -> interrupt disabled */
	MODE_DUAL_REG,       /* Two registers, set bit to enable / disable */
	MODE_PRIO_REG,       /* Priority value written to enable interrupt */
	MODE_PCLR_REG,       /* Above plus all bits set to disable interrupt */
};

static inline struct intc_desc_int *get_intc_desc(unsigned int irq)
{
	struct irq_chip *chip = irq_get_chip(irq);

	return container_of(chip, struct intc_desc_int, chip);
}

/*
 * Grumble.
 */
static inline void activate_irq(int irq)
{
	irq_modify_status(irq, IRQ_NOREQUEST, IRQ_NOPROBE);
}

static inline int intc_handle_int_cmp(const void *a, const void *b)
{
	const struct intc_handle_int *_a = a;
	const struct intc_handle_int *_b = b;

	return _a->irq - _b->irq;
}

/* access.c */
extern unsigned long
(*intc_reg_fns[])(unsigned long addr, unsigned long h, unsigned long data);

extern unsigned long
(*intc_enable_fns[])(unsigned long addr, unsigned long handle,
		     unsigned long (*fn)(unsigned long,
				unsigned long, unsigned long),
		     unsigned int irq);
extern unsigned long
(*intc_disable_fns[])(unsigned long addr, unsigned long handle,
		      unsigned long (*fn)(unsigned long,
				unsigned long, unsigned long),
		      unsigned int irq);
extern unsigned long
(*intc_enable_noprio_fns[])(unsigned long addr, unsigned long handle,
		            unsigned long (*fn)(unsigned long,
				unsigned long, unsigned long),
			    unsigned int irq);

unsigned long intc_phys_to_virt(struct intc_desc_int *d, unsigned long address);
unsigned int intc_get_reg(struct intc_desc_int *d, unsigned long address);
unsigned int intc_set_field_from_handle(unsigned int value,
			    unsigned int field_value,
			    unsigned int handle);
unsigned long intc_get_field_from_handle(unsigned int value,
					 unsigned int handle);

/* balancing.c */
#ifdef CONFIG_INTC_BALANCING
void intc_balancing_enable(unsigned int irq);
void intc_balancing_disable(unsigned int irq);
void intc_set_dist_handle(unsigned int irq, struct intc_desc *desc,
			  struct intc_desc_int *d, intc_enum id);
#else
static inline void intc_balancing_enable(unsigned int irq) { }
static inline void intc_balancing_disable(unsigned int irq) { }
static inline void
intc_set_dist_handle(unsigned int irq, struct intc_desc *desc,
		     struct intc_desc_int *d, intc_enum id) { }
#endif

/* chip.c */
extern struct irq_chip intc_irq_chip;
void _intc_enable(struct irq_data *data, unsigned long handle);

/* core.c */
extern struct list_head intc_list;
extern raw_spinlock_t intc_big_lock;
extern struct bus_type intc_subsys;

unsigned int intc_get_dfl_prio_level(void);
unsigned int intc_get_prio_level(unsigned int irq);
void intc_set_prio_level(unsigned int irq, unsigned int level);

/* handle.c */
unsigned int intc_get_mask_handle(struct intc_desc *desc,
				  struct intc_desc_int *d,
				  intc_enum enum_id, int do_grps);
unsigned int intc_get_prio_handle(struct intc_desc *desc,
				  struct intc_desc_int *d,
				  intc_enum enum_id, int do_grps);
unsigned int intc_get_sense_handle(struct intc_desc *desc,
				   struct intc_desc_int *d,
				   intc_enum enum_id);
void intc_set_ack_handle(unsigned int irq, struct intc_desc *desc,
			 struct intc_desc_int *d, intc_enum id);
unsigned long intc_get_ack_handle(unsigned int irq);
void intc_enable_disable_enum(struct intc_desc *desc, struct intc_desc_int *d,
			      intc_enum enum_id, int enable);

/* irqdomain.c */
void intc_irq_domain_init(struct intc_desc_int *d, struct intc_hw_desc *hw);

/* virq.c */
void intc_subgroup_init(struct intc_desc *desc, struct intc_desc_int *d);
void intc_irq_xlate_set(unsigned int irq, intc_enum id, struct intc_desc_int *d);
struct intc_map_entry *intc_irq_xlate_get(unsigned int irq);
