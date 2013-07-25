#ifndef _ASM_X86_DESC_H
#define _ASM_X86_DESC_H

#include <asm/desc_defs.h>
#include <asm/ldt.h>
#include <asm/mmu.h>

#include <linux/smp.h>
#include <linux/percpu.h>

static inline void fill_ldt(struct desc_struct *desc, const struct user_desc *info)
{
	desc->limit0		= info->limit & 0x0ffff;

	desc->base0		= (info->base_addr & 0x0000ffff);
	desc->base1		= (info->base_addr & 0x00ff0000) >> 16;

	desc->type		= (info->read_exec_only ^ 1) << 1;
	desc->type	       |= info->contents << 2;

	desc->s			= 1;
	desc->dpl		= 0x3;
	desc->p			= info->seg_not_present ^ 1;
	desc->limit		= (info->limit & 0xf0000) >> 16;
	desc->avl		= info->useable;
	desc->d			= info->seg_32bit;
	desc->g			= info->limit_in_pages;

	desc->base2		= (info->base_addr & 0xff000000) >> 24;
	/*
	 * Don't allow setting of the lm bit. It would confuse
	 * user_64bit_mode and would get overridden by sysret anyway.
	 */
	desc->l			= 0;
}

extern struct desc_ptr idt_descr;
extern gate_desc idt_table[];
extern struct desc_ptr debug_idt_descr;
extern gate_desc debug_idt_table[];

struct gdt_page {
	struct desc_struct gdt[GDT_ENTRIES];
} __attribute__((aligned(PAGE_SIZE)));

DECLARE_PER_CPU_PAGE_ALIGNED(struct gdt_page, gdt_page);

static inline struct desc_struct *get_cpu_gdt_table(unsigned int cpu)
{
	return per_cpu(gdt_page, cpu).gdt;
}

#ifdef CONFIG_X86_64

static inline void pack_gate(gate_desc *gate, unsigned type, unsigned long func,
			     unsigned dpl, unsigned ist, unsigned seg)
{
	gate->offset_low	= PTR_LOW(func);
	gate->segment		= __KERNEL_CS;
	gate->ist		= ist;
	gate->p			= 1;
	gate->dpl		= dpl;
	gate->zero0		= 0;
	gate->zero1		= 0;
	gate->type		= type;
	gate->offset_middle	= PTR_MIDDLE(func);
	gate->offset_high	= PTR_HIGH(func);
}

#else
static inline void pack_gate(gate_desc *gate, unsigned char type,
			     unsigned long base, unsigned dpl, unsigned flags,
			     unsigned short seg)
{
	gate->a = (seg << 16) | (base & 0xffff);
	gate->b = (base & 0xffff0000) | (((0x80 | type | (dpl << 5)) & 0xff) << 8);
}

#endif

static inline int desc_empty(const void *ptr)
{
	const u32 *desc = ptr;

	return !(desc[0] | desc[1]);
}

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define load_TR_desc()				native_load_tr_desc()
#define load_gdt(dtr)				native_load_gdt(dtr)
#define load_idt(dtr)				native_load_idt(dtr)
#define load_tr(tr)				asm volatile("ltr %0"::"m" (tr))
#define load_ldt(ldt)				asm volatile("lldt %0"::"m" (ldt))

#define store_gdt(dtr)				native_store_gdt(dtr)
#define store_idt(dtr)				native_store_idt(dtr)
#define store_tr(tr)				(tr = native_store_tr())

#define load_TLS(t, cpu)			native_load_tls(t, cpu)
#define set_ldt					native_set_ldt

#define write_ldt_entry(dt, entry, desc)	native_write_ldt_entry(dt, entry, desc)
#define write_gdt_entry(dt, entry, desc, type)	native_write_gdt_entry(dt, entry, desc, type)
#define write_idt_entry(dt, entry, g)		native_write_idt_entry(dt, entry, g)

static inline void paravirt_alloc_ldt(struct desc_struct *ldt, unsigned entries)
{
}

static inline void paravirt_free_ldt(struct desc_struct *ldt, unsigned entries)
{
}
#endif	/* CONFIG_PARAVIRT */

#define store_ldt(ldt) asm("sldt %0" : "=m"(ldt))

static inline void native_write_idt_entry(gate_desc *idt, int entry, const gate_desc *gate)
{
	memcpy(&idt[entry], gate, sizeof(*gate));
}

static inline void native_write_ldt_entry(struct desc_struct *ldt, int entry, const void *desc)
{
	memcpy(&ldt[entry], desc, 8);
}

static inline void
native_write_gdt_entry(struct desc_struct *gdt, int entry, const void *desc, int type)
{
	unsigned int size;

	switch (type) {
	case DESC_TSS:	size = sizeof(tss_desc);	break;
	case DESC_LDT:	size = sizeof(ldt_desc);	break;
	default:	size = sizeof(*gdt);		break;
	}

	memcpy(&gdt[entry], desc, size);
}

static inline void pack_descriptor(struct desc_struct *desc, unsigned long base,
				   unsigned long limit, unsigned char type,
				   unsigned char flags)
{
	desc->a = ((base & 0xffff) << 16) | (limit & 0xffff);
	desc->b = (base & 0xff000000) | ((base & 0xff0000) >> 16) |
		(limit & 0x000f0000) | ((type & 0xff) << 8) |
		((flags & 0xf) << 20);
	desc->p = 1;
}


static inline void set_tssldt_descriptor(void *d, unsigned long addr, unsigned type, unsigned size)
{
#ifdef CONFIG_X86_64
	struct ldttss_desc64 *desc = d;

	memset(desc, 0, sizeof(*desc));

	desc->limit0		= size & 0xFFFF;
	desc->base0		= PTR_LOW(addr);
	desc->base1		= PTR_MIDDLE(addr) & 0xFF;
	desc->type		= type;
	desc->p			= 1;
	desc->limit1		= (size >> 16) & 0xF;
	desc->base2		= (PTR_MIDDLE(addr) >> 8) & 0xFF;
	desc->base3		= PTR_HIGH(addr);
#else
	pack_descriptor((struct desc_struct *)d, addr, size, 0x80 | type, 0);
#endif
}

static inline void __set_tss_desc(unsigned cpu, unsigned int entry, void *addr)
{
	struct desc_struct *d = get_cpu_gdt_table(cpu);
	tss_desc tss;

	/*
	 * sizeof(unsigned long) coming from an extra "long" at the end
	 * of the iobitmap. See tss_struct definition in processor.h
	 *
	 * -1? seg base+limit should be pointing to the address of the
	 * last valid byte
	 */
	set_tssldt_descriptor(&tss, (unsigned long)addr, DESC_TSS,
			      IO_BITMAP_OFFSET + IO_BITMAP_BYTES +
			      sizeof(unsigned long) - 1);
	write_gdt_entry(d, entry, &tss, DESC_TSS);
}

#define set_tss_desc(cpu, addr) __set_tss_desc(cpu, GDT_ENTRY_TSS, addr)

static inline void native_set_ldt(const void *addr, unsigned int entries)
{
	if (likely(entries == 0))
		asm volatile("lldt %w0"::"q" (0));
	else {
		unsigned cpu = smp_processor_id();
		ldt_desc ldt;

		set_tssldt_descriptor(&ldt, (unsigned long)addr, DESC_LDT,
				      entries * LDT_ENTRY_SIZE - 1);
		write_gdt_entry(get_cpu_gdt_table(cpu), GDT_ENTRY_LDT,
				&ldt, DESC_LDT);
		asm volatile("lldt %w0"::"q" (GDT_ENTRY_LDT*8));
	}
}

static inline void native_load_tr_desc(void)
{
	asm volatile("ltr %w0"::"q" (GDT_ENTRY_TSS*8));
}

static inline void native_load_gdt(const struct desc_ptr *dtr)
{
	asm volatile("lgdt %0"::"m" (*dtr));
}

static inline void native_load_idt(const struct desc_ptr *dtr)
{
	asm volatile("lidt %0"::"m" (*dtr));
}

static inline void native_store_gdt(struct desc_ptr *dtr)
{
	asm volatile("sgdt %0":"=m" (*dtr));
}

static inline void native_store_idt(struct desc_ptr *dtr)
{
	asm volatile("sidt %0":"=m" (*dtr));
}

static inline unsigned long native_store_tr(void)
{
	unsigned long tr;

	asm volatile("str %0":"=r" (tr));

	return tr;
}

static inline void native_load_tls(struct thread_struct *t, unsigned int cpu)
{
	struct desc_struct *gdt = get_cpu_gdt_table(cpu);
	unsigned int i;

	for (i = 0; i < GDT_ENTRY_TLS_ENTRIES; i++)
		gdt[GDT_ENTRY_TLS_MIN + i] = t->tls_array[i];
}

#define _LDT_empty(info)				\
	((info)->base_addr		== 0	&&	\
	 (info)->limit			== 0	&&	\
	 (info)->contents		== 0	&&	\
	 (info)->read_exec_only		== 1	&&	\
	 (info)->seg_32bit		== 0	&&	\
	 (info)->limit_in_pages		== 0	&&	\
	 (info)->seg_not_present	== 1	&&	\
	 (info)->useable		== 0)

#ifdef CONFIG_X86_64
#define LDT_empty(info) (_LDT_empty(info) && ((info)->lm == 0))
#else
#define LDT_empty(info) (_LDT_empty(info))
#endif

static inline void clear_LDT(void)
{
	set_ldt(NULL, 0);
}

/*
 * load one particular LDT into the current CPU
 */
static inline void load_LDT_nolock(mm_context_t *pc)
{
	set_ldt(pc->ldt, pc->size);
}

static inline void load_LDT(mm_context_t *pc)
{
	preempt_disable();
	load_LDT_nolock(pc);
	preempt_enable();
}

static inline unsigned long get_desc_base(const struct desc_struct *desc)
{
	return (unsigned)(desc->base0 | ((desc->base1) << 16) | ((desc->base2) << 24));
}

static inline void set_desc_base(struct desc_struct *desc, unsigned long base)
{
	desc->base0 = base & 0xffff;
	desc->base1 = (base >> 16) & 0xff;
	desc->base2 = (base >> 24) & 0xff;
}

static inline unsigned long get_desc_limit(const struct desc_struct *desc)
{
	return desc->limit0 | (desc->limit << 16);
}

static inline void set_desc_limit(struct desc_struct *desc, unsigned long limit)
{
	desc->limit0 = limit & 0xffff;
	desc->limit = (limit >> 16) & 0xf;
}

#ifdef CONFIG_X86_64
static inline void set_nmi_gate(int gate, void *addr)
{
	gate_desc s;

	pack_gate(&s, GATE_INTERRUPT, (unsigned long)addr, 0, 0, __KERNEL_CS);
	write_idt_entry(debug_idt_table, gate, &s);
}
#endif

#ifdef CONFIG_TRACING
extern struct desc_ptr trace_idt_descr;
extern gate_desc trace_idt_table[];
static inline void write_trace_idt_entry(int entry, const gate_desc *gate)
{
	write_idt_entry(trace_idt_table, entry, gate);
}
#else
static inline void write_trace_idt_entry(int entry, const gate_desc *gate)
{
}
#endif

static inline void _set_gate(int gate, unsigned type, void *addr,
			     unsigned dpl, unsigned ist, unsigned seg)
{
	gate_desc s;

	pack_gate(&s, type, (unsigned long)addr, dpl, ist, seg);
	/*
	 * does not need to be atomic because it is only done once at
	 * setup time
	 */
	write_idt_entry(idt_table, gate, &s);
	write_trace_idt_entry(gate, &s);
}

/*
 * This needs to use 'idt_table' rather than 'idt', and
 * thus use the _nonmapped_ version of the IDT, as the
 * Pentium F0 0F bugfix can have resulted in the mapped
 * IDT being write-protected.
 */
static inline void set_intr_gate(unsigned int n, void *addr)
{
	BUG_ON((unsigned)n > 0xFF);
	_set_gate(n, GATE_INTERRUPT, addr, 0, 0, __KERNEL_CS);
}

extern int first_system_vector;
/* used_vectors is BITMAP for irq is not managed by percpu vector_irq */
extern unsigned long used_vectors[];

static inline void alloc_system_vector(int vector)
{
	if (!test_bit(vector, used_vectors)) {
		set_bit(vector, used_vectors);
		if (first_system_vector > vector)
			first_system_vector = vector;
	} else {
		BUG();
	}
}

#ifdef CONFIG_TRACING
static inline void trace_set_intr_gate(unsigned int gate, void *addr)
{
	gate_desc s;

	pack_gate(&s, GATE_INTERRUPT, (unsigned long)addr, 0, 0, __KERNEL_CS);
	write_idt_entry(trace_idt_table, gate, &s);
}

static inline void __trace_alloc_intr_gate(unsigned int n, void *addr)
{
	trace_set_intr_gate(n, addr);
}
#else
static inline void trace_set_intr_gate(unsigned int gate, void *addr)
{
}

#define __trace_alloc_intr_gate(n, addr)
#endif

static inline void __alloc_intr_gate(unsigned int n, void *addr)
{
	set_intr_gate(n, addr);
}

#define alloc_intr_gate(n, addr)				\
	do {							\
		alloc_system_vector(n);				\
		__alloc_intr_gate(n, addr);			\
		__trace_alloc_intr_gate(n, trace_##addr);	\
	} while (0)

/*
 * This routine sets up an interrupt gate at directory privilege level 3.
 */
static inline void set_system_intr_gate(unsigned int n, void *addr)
{
	BUG_ON((unsigned)n > 0xFF);
	_set_gate(n, GATE_INTERRUPT, addr, 0x3, 0, __KERNEL_CS);
}

static inline void set_system_trap_gate(unsigned int n, void *addr)
{
	BUG_ON((unsigned)n > 0xFF);
	_set_gate(n, GATE_TRAP, addr, 0x3, 0, __KERNEL_CS);
}

static inline void set_trap_gate(unsigned int n, void *addr)
{
	BUG_ON((unsigned)n > 0xFF);
	_set_gate(n, GATE_TRAP, addr, 0, 0, __KERNEL_CS);
}

static inline void set_task_gate(unsigned int n, unsigned int gdt_entry)
{
	BUG_ON((unsigned)n > 0xFF);
	_set_gate(n, GATE_TASK, (void *)0, 0, 0, (gdt_entry<<3));
}

static inline void set_intr_gate_ist(int n, void *addr, unsigned ist)
{
	BUG_ON((unsigned)n > 0xFF);
	_set_gate(n, GATE_INTERRUPT, addr, 0, ist, __KERNEL_CS);
}

static inline void set_system_intr_gate_ist(int n, void *addr, unsigned ist)
{
	BUG_ON((unsigned)n > 0xFF);
	_set_gate(n, GATE_INTERRUPT, addr, 0x3, ist, __KERNEL_CS);
}

#ifdef CONFIG_X86_64
DECLARE_PER_CPU(u32, debug_idt_ctr);
static inline bool is_debug_idt_enabled(void)
{
	if (this_cpu_read(debug_idt_ctr))
		return true;

	return false;
}

static inline void load_debug_idt(void)
{
	load_idt((const struct desc_ptr *)&debug_idt_descr);
}
#else
static inline bool is_debug_idt_enabled(void)
{
	return false;
}

static inline void load_debug_idt(void)
{
}
#endif

#ifdef CONFIG_TRACING
extern atomic_t trace_idt_ctr;
static inline bool is_trace_idt_enabled(void)
{
	if (atomic_read(&trace_idt_ctr))
		return true;

	return false;
}

static inline void load_trace_idt(void)
{
	load_idt((const struct desc_ptr *)&trace_idt_descr);
}
#else
static inline bool is_trace_idt_enabled(void)
{
	return false;
}

static inline void load_trace_idt(void)
{
}
#endif

/*
 * The load_current_idt() must be called with interrupts disabled
 * to avoid races. That way the IDT will always be set back to the expected
 * descriptor. It's also called when a CPU is being initialized, and
 * that doesn't need to disable interrupts, as nothing should be
 * bothering the CPU then.
 */
static inline void load_current_idt(void)
{
	if (is_debug_idt_enabled())
		load_debug_idt();
	else if (is_trace_idt_enabled())
		load_trace_idt();
	else
		load_idt((const struct desc_ptr *)&idt_descr);
}
#endif /* _ASM_X86_DESC_H */
