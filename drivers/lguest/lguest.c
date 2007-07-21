/*
 * Lguest specific paravirt-ops implementation
 *
 * Copyright (C) 2006, Rusty Russell <rusty@rustcorp.com.au> IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/kernel.h>
#include <linux/start_kernel.h>
#include <linux/string.h>
#include <linux/console.h>
#include <linux/screen_info.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/lguest.h>
#include <linux/lguest_launcher.h>
#include <linux/lguest_bus.h>
#include <asm/paravirt.h>
#include <asm/param.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/desc.h>
#include <asm/setup.h>
#include <asm/e820.h>
#include <asm/mce.h>
#include <asm/io.h>

/* Declarations for definitions in lguest_guest.S */
extern char lguest_noirq_start[], lguest_noirq_end[];
extern const char lgstart_cli[], lgend_cli[];
extern const char lgstart_sti[], lgend_sti[];
extern const char lgstart_popf[], lgend_popf[];
extern const char lgstart_pushf[], lgend_pushf[];
extern const char lgstart_iret[], lgend_iret[];
extern void lguest_iret(void);

struct lguest_data lguest_data = {
	.hcall_status = { [0 ... LHCALL_RING_SIZE-1] = 0xFF },
	.noirq_start = (u32)lguest_noirq_start,
	.noirq_end = (u32)lguest_noirq_end,
	.blocked_interrupts = { 1 }, /* Block timer interrupts */
};
struct lguest_device_desc *lguest_devices;
static cycle_t clock_base;

static enum paravirt_lazy_mode lazy_mode;
static void lguest_lazy_mode(enum paravirt_lazy_mode mode)
{
	if (mode == PARAVIRT_LAZY_FLUSH) {
		if (unlikely(lazy_mode != PARAVIRT_LAZY_NONE))
			hcall(LHCALL_FLUSH_ASYNC, 0, 0, 0);
	} else {
		lazy_mode = mode;
		if (mode == PARAVIRT_LAZY_NONE)
			hcall(LHCALL_FLUSH_ASYNC, 0, 0, 0);
	}
}

static void lazy_hcall(unsigned long call,
		       unsigned long arg1,
		       unsigned long arg2,
		       unsigned long arg3)
{
	if (lazy_mode == PARAVIRT_LAZY_NONE)
		hcall(call, arg1, arg2, arg3);
	else
		async_hcall(call, arg1, arg2, arg3);
}

void async_hcall(unsigned long call,
		 unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	/* Note: This code assumes we're uniprocessor. */
	static unsigned int next_call;
	unsigned long flags;

	local_irq_save(flags);
	if (lguest_data.hcall_status[next_call] != 0xFF) {
		/* Table full, so do normal hcall which will flush table. */
		hcall(call, arg1, arg2, arg3);
	} else {
		lguest_data.hcalls[next_call].eax = call;
		lguest_data.hcalls[next_call].edx = arg1;
		lguest_data.hcalls[next_call].ebx = arg2;
		lguest_data.hcalls[next_call].ecx = arg3;
		/* Make sure host sees arguments before "valid" flag. */
		wmb();
		lguest_data.hcall_status[next_call] = 0;
		if (++next_call == LHCALL_RING_SIZE)
			next_call = 0;
	}
	local_irq_restore(flags);
}

void lguest_send_dma(unsigned long key, struct lguest_dma *dma)
{
	dma->used_len = 0;
	hcall(LHCALL_SEND_DMA, key, __pa(dma), 0);
}

int lguest_bind_dma(unsigned long key, struct lguest_dma *dmas,
		    unsigned int num, u8 irq)
{
	if (!hcall(LHCALL_BIND_DMA, key, __pa(dmas), (num << 8) | irq))
		return -ENOMEM;
	return 0;
}

void lguest_unbind_dma(unsigned long key, struct lguest_dma *dmas)
{
	hcall(LHCALL_BIND_DMA, key, __pa(dmas), 0);
}

/* For guests, device memory can be used as normal memory, so we cast away the
 * __iomem to quieten sparse. */
void *lguest_map(unsigned long phys_addr, unsigned long pages)
{
	return (__force void *)ioremap(phys_addr, PAGE_SIZE*pages);
}

void lguest_unmap(void *addr)
{
	iounmap((__force void __iomem *)addr);
}

static unsigned long save_fl(void)
{
	return lguest_data.irq_enabled;
}

static void restore_fl(unsigned long flags)
{
	/* FIXME: Check if interrupt pending... */
	lguest_data.irq_enabled = flags;
}

static void irq_disable(void)
{
	lguest_data.irq_enabled = 0;
}

static void irq_enable(void)
{
	/* FIXME: Check if interrupt pending... */
	lguest_data.irq_enabled = X86_EFLAGS_IF;
}

static void lguest_write_idt_entry(struct desc_struct *dt,
				   int entrynum, u32 low, u32 high)
{
	write_dt_entry(dt, entrynum, low, high);
	hcall(LHCALL_LOAD_IDT_ENTRY, entrynum, low, high);
}

static void lguest_load_idt(const struct Xgt_desc_struct *desc)
{
	unsigned int i;
	struct desc_struct *idt = (void *)desc->address;

	for (i = 0; i < (desc->size+1)/8; i++)
		hcall(LHCALL_LOAD_IDT_ENTRY, i, idt[i].a, idt[i].b);
}

static void lguest_load_gdt(const struct Xgt_desc_struct *desc)
{
	BUG_ON((desc->size+1)/8 != GDT_ENTRIES);
	hcall(LHCALL_LOAD_GDT, __pa(desc->address), GDT_ENTRIES, 0);
}

static void lguest_write_gdt_entry(struct desc_struct *dt,
				   int entrynum, u32 low, u32 high)
{
	write_dt_entry(dt, entrynum, low, high);
	hcall(LHCALL_LOAD_GDT, __pa(dt), GDT_ENTRIES, 0);
}

static void lguest_load_tls(struct thread_struct *t, unsigned int cpu)
{
	lazy_hcall(LHCALL_LOAD_TLS, __pa(&t->tls_array), cpu, 0);
}

static void lguest_set_ldt(const void *addr, unsigned entries)
{
}

static void lguest_load_tr_desc(void)
{
}

static void lguest_cpuid(unsigned int *eax, unsigned int *ebx,
			 unsigned int *ecx, unsigned int *edx)
{
	int function = *eax;

	native_cpuid(eax, ebx, ecx, edx);
	switch (function) {
	case 1:	/* Basic feature request. */
		/* We only allow kernel to see SSE3, CMPXCHG16B and SSSE3 */
		*ecx &= 0x00002201;
		/* SSE, SSE2, FXSR, MMX, CMOV, CMPXCHG8B, FPU. */
		*edx &= 0x07808101;
		/* Host wants to know when we flush kernel pages: set PGE. */
		*edx |= 0x00002000;
		break;
	case 0x80000000:
		/* Futureproof this a little: if they ask how much extended
		 * processor information, limit it to known fields. */
		if (*eax > 0x80000008)
			*eax = 0x80000008;
		break;
	}
}

static unsigned long current_cr0, current_cr3;
static void lguest_write_cr0(unsigned long val)
{
	lazy_hcall(LHCALL_TS, val & 8, 0, 0);
	current_cr0 = val;
}

static unsigned long lguest_read_cr0(void)
{
	return current_cr0;
}

static void lguest_clts(void)
{
	lazy_hcall(LHCALL_TS, 0, 0, 0);
	current_cr0 &= ~8U;
}

static unsigned long lguest_read_cr2(void)
{
	return lguest_data.cr2;
}

static void lguest_write_cr3(unsigned long cr3)
{
	lazy_hcall(LHCALL_NEW_PGTABLE, cr3, 0, 0);
	current_cr3 = cr3;
}

static unsigned long lguest_read_cr3(void)
{
	return current_cr3;
}

/* Used to enable/disable PGE, but we don't care. */
static unsigned long lguest_read_cr4(void)
{
	return 0;
}

static void lguest_write_cr4(unsigned long val)
{
}

static void lguest_set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
	lazy_hcall(LHCALL_SET_PTE, __pa(mm->pgd), addr, pteval.pte_low);
}

/* We only support two-level pagetables at the moment. */
static void lguest_set_pmd(pmd_t *pmdp, pmd_t pmdval)
{
	*pmdp = pmdval;
	lazy_hcall(LHCALL_SET_PMD, __pa(pmdp)&PAGE_MASK,
		   (__pa(pmdp)&(PAGE_SIZE-1))/4, 0);
}

/* FIXME: Eliminate all callers of this. */
static void lguest_set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
	/* Don't bother with hypercall before initial setup. */
	if (current_cr3)
		lazy_hcall(LHCALL_FLUSH_TLB, 1, 0, 0);
}

static void lguest_flush_tlb_single(unsigned long addr)
{
	/* Simply set it to zero, and it will fault back in. */
	lazy_hcall(LHCALL_SET_PTE, current_cr3, addr, 0);
}

static void lguest_flush_tlb_user(void)
{
	lazy_hcall(LHCALL_FLUSH_TLB, 0, 0, 0);
}

static void lguest_flush_tlb_kernel(void)
{
	lazy_hcall(LHCALL_FLUSH_TLB, 1, 0, 0);
}

static void disable_lguest_irq(unsigned int irq)
{
	set_bit(irq, lguest_data.blocked_interrupts);
}

static void enable_lguest_irq(unsigned int irq)
{
	clear_bit(irq, lguest_data.blocked_interrupts);
	/* FIXME: If it's pending? */
}

static struct irq_chip lguest_irq_controller = {
	.name		= "lguest",
	.mask		= disable_lguest_irq,
	.mask_ack	= disable_lguest_irq,
	.unmask		= enable_lguest_irq,
};

static void __init lguest_init_IRQ(void)
{
	unsigned int i;

	for (i = 0; i < LGUEST_IRQS; i++) {
		int vector = FIRST_EXTERNAL_VECTOR + i;
		if (vector != SYSCALL_VECTOR) {
			set_intr_gate(vector, interrupt[i]);
			set_irq_chip_and_handler(i, &lguest_irq_controller,
						 handle_level_irq);
		}
	}
	irq_ctx_init(smp_processor_id());
}

static unsigned long lguest_get_wallclock(void)
{
	return hcall(LHCALL_GET_WALLCLOCK, 0, 0, 0);
}

static cycle_t lguest_clock_read(void)
{
	if (lguest_data.tsc_khz)
		return native_read_tsc();
	else
		return jiffies;
}

/* This is what we tell the kernel is our clocksource.  */
static struct clocksource lguest_clock = {
	.name		= "lguest",
	.rating		= 400,
	.read		= lguest_clock_read,
};

static unsigned long long lguest_sched_clock(void)
{
	return cyc2ns(&lguest_clock, lguest_clock_read() - clock_base);
}

/* We also need a "struct clock_event_device": Linux asks us to set it to go
 * off some time in the future.  Actually, James Morris figured all this out, I
 * just applied the patch. */
static int lguest_clockevent_set_next_event(unsigned long delta,
                                           struct clock_event_device *evt)
{
	if (delta < LG_CLOCK_MIN_DELTA) {
		if (printk_ratelimit())
			printk(KERN_DEBUG "%s: small delta %lu ns\n",
			       __FUNCTION__, delta);
		return -ETIME;
	}
	hcall(LHCALL_SET_CLOCKEVENT, delta, 0, 0);
	return 0;
}

static void lguest_clockevent_set_mode(enum clock_event_mode mode,
                                      struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		/* A 0 argument shuts the clock down. */
		hcall(LHCALL_SET_CLOCKEVENT, 0, 0, 0);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* This is what we expect. */
		break;
	case CLOCK_EVT_MODE_PERIODIC:
		BUG();
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

/* This describes our primitive timer chip. */
static struct clock_event_device lguest_clockevent = {
	.name                   = "lguest",
	.features               = CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event         = lguest_clockevent_set_next_event,
	.set_mode               = lguest_clockevent_set_mode,
	.rating                 = INT_MAX,
	.mult                   = 1,
	.shift                  = 0,
	.min_delta_ns           = LG_CLOCK_MIN_DELTA,
	.max_delta_ns           = LG_CLOCK_MAX_DELTA,
};

/* This is the Guest timer interrupt handler (hardware interrupt 0).  We just
 * call the clockevent infrastructure and it does whatever needs doing. */
static void lguest_time_irq(unsigned int irq, struct irq_desc *desc)
{
	unsigned long flags;

	/* Don't interrupt us while this is running. */
	local_irq_save(flags);
	lguest_clockevent.event_handler(&lguest_clockevent);
	local_irq_restore(flags);
}

static void lguest_time_init(void)
{
	set_irq_handler(0, lguest_time_irq);

	/* We use the TSC if the Host tells us we can, otherwise a dumb
	 * jiffies-based clock. */
	if (lguest_data.tsc_khz) {
		lguest_clock.shift = 22;
		lguest_clock.mult = clocksource_khz2mult(lguest_data.tsc_khz,
							 lguest_clock.shift);
		lguest_clock.mask = CLOCKSOURCE_MASK(64);
		lguest_clock.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	} else {
		/* To understand this, start at kernel/time/jiffies.c... */
		lguest_clock.shift = 8;
		lguest_clock.mult = (((u64)NSEC_PER_SEC<<8)/ACTHZ) << 8;
		lguest_clock.mask = CLOCKSOURCE_MASK(32);
	}
	clock_base = lguest_clock_read();
	clocksource_register(&lguest_clock);

	/* We can't set cpumask in the initializer: damn C limitations! */
	lguest_clockevent.cpumask = cpumask_of_cpu(0);
	clockevents_register_device(&lguest_clockevent);

	enable_lguest_irq(0);
}

static void lguest_load_esp0(struct tss_struct *tss,
				     struct thread_struct *thread)
{
	lazy_hcall(LHCALL_SET_STACK, __KERNEL_DS|0x1, thread->esp0,
		   THREAD_SIZE/PAGE_SIZE);
}

static void lguest_set_debugreg(int regno, unsigned long value)
{
	/* FIXME: Implement */
}

static void lguest_wbinvd(void)
{
}

#ifdef CONFIG_X86_LOCAL_APIC
static void lguest_apic_write(unsigned long reg, unsigned long v)
{
}

static unsigned long lguest_apic_read(unsigned long reg)
{
	return 0;
}
#endif

static void lguest_safe_halt(void)
{
	hcall(LHCALL_HALT, 0, 0, 0);
}

static void lguest_power_off(void)
{
	hcall(LHCALL_CRASH, __pa("Power down"), 0, 0);
}

static int lguest_panic(struct notifier_block *nb, unsigned long l, void *p)
{
	hcall(LHCALL_CRASH, __pa(p), 0, 0);
	return NOTIFY_DONE;
}

static struct notifier_block paniced = {
	.notifier_call = lguest_panic
};

static __init char *lguest_memory_setup(void)
{
	/* We do this here because lockcheck barfs if before start_kernel */
	atomic_notifier_chain_register(&panic_notifier_list, &paniced);

	add_memory_region(E820_MAP->addr, E820_MAP->size, E820_MAP->type);
	return "LGUEST";
}

static const struct lguest_insns
{
	const char *start, *end;
} lguest_insns[] = {
	[PARAVIRT_PATCH(irq_disable)] = { lgstart_cli, lgend_cli },
	[PARAVIRT_PATCH(irq_enable)] = { lgstart_sti, lgend_sti },
	[PARAVIRT_PATCH(restore_fl)] = { lgstart_popf, lgend_popf },
	[PARAVIRT_PATCH(save_fl)] = { lgstart_pushf, lgend_pushf },
};
static unsigned lguest_patch(u8 type, u16 clobber, void *insns, unsigned len)
{
	unsigned int insn_len;

	/* Don't touch it if we don't have a replacement */
	if (type >= ARRAY_SIZE(lguest_insns) || !lguest_insns[type].start)
		return paravirt_patch_default(type, clobber, insns, len);

	insn_len = lguest_insns[type].end - lguest_insns[type].start;

	/* Similarly if we can't fit replacement. */
	if (len < insn_len)
		return paravirt_patch_default(type, clobber, insns, len);

	memcpy(insns, lguest_insns[type].start, insn_len);
	return insn_len;
}

__init void lguest_init(void *boot)
{
	/* Copy boot parameters first. */
	memcpy(&boot_params, boot, PARAM_SIZE);
	memcpy(boot_command_line, __va(boot_params.hdr.cmd_line_ptr),
	       COMMAND_LINE_SIZE);

	paravirt_ops.name = "lguest";
	paravirt_ops.paravirt_enabled = 1;
	paravirt_ops.kernel_rpl = 1;

	paravirt_ops.save_fl = save_fl;
	paravirt_ops.restore_fl = restore_fl;
	paravirt_ops.irq_disable = irq_disable;
	paravirt_ops.irq_enable = irq_enable;
	paravirt_ops.load_gdt = lguest_load_gdt;
	paravirt_ops.memory_setup = lguest_memory_setup;
	paravirt_ops.cpuid = lguest_cpuid;
	paravirt_ops.write_cr3 = lguest_write_cr3;
	paravirt_ops.flush_tlb_user = lguest_flush_tlb_user;
	paravirt_ops.flush_tlb_single = lguest_flush_tlb_single;
	paravirt_ops.flush_tlb_kernel = lguest_flush_tlb_kernel;
	paravirt_ops.set_pte = lguest_set_pte;
	paravirt_ops.set_pte_at = lguest_set_pte_at;
	paravirt_ops.set_pmd = lguest_set_pmd;
#ifdef CONFIG_X86_LOCAL_APIC
	paravirt_ops.apic_write = lguest_apic_write;
	paravirt_ops.apic_write_atomic = lguest_apic_write;
	paravirt_ops.apic_read = lguest_apic_read;
#endif
	paravirt_ops.load_idt = lguest_load_idt;
	paravirt_ops.iret = lguest_iret;
	paravirt_ops.load_esp0 = lguest_load_esp0;
	paravirt_ops.load_tr_desc = lguest_load_tr_desc;
	paravirt_ops.set_ldt = lguest_set_ldt;
	paravirt_ops.load_tls = lguest_load_tls;
	paravirt_ops.set_debugreg = lguest_set_debugreg;
	paravirt_ops.clts = lguest_clts;
	paravirt_ops.read_cr0 = lguest_read_cr0;
	paravirt_ops.write_cr0 = lguest_write_cr0;
	paravirt_ops.init_IRQ = lguest_init_IRQ;
	paravirt_ops.read_cr2 = lguest_read_cr2;
	paravirt_ops.read_cr3 = lguest_read_cr3;
	paravirt_ops.read_cr4 = lguest_read_cr4;
	paravirt_ops.write_cr4 = lguest_write_cr4;
	paravirt_ops.write_gdt_entry = lguest_write_gdt_entry;
	paravirt_ops.write_idt_entry = lguest_write_idt_entry;
	paravirt_ops.patch = lguest_patch;
	paravirt_ops.safe_halt = lguest_safe_halt;
	paravirt_ops.get_wallclock = lguest_get_wallclock;
	paravirt_ops.time_init = lguest_time_init;
	paravirt_ops.set_lazy_mode = lguest_lazy_mode;
	paravirt_ops.wbinvd = lguest_wbinvd;
	paravirt_ops.sched_clock = lguest_sched_clock;

	hcall(LHCALL_LGUEST_INIT, __pa(&lguest_data), 0, 0);

	/* We use top of mem for initial pagetables. */
	init_pg_tables_end = __pa(pg0);

	asm volatile ("mov %0, %%fs" : : "r" (__KERNEL_DS) : "memory");

	reserve_top_address(lguest_data.reserve_mem);

	lockdep_init();

	paravirt_disable_iospace();

	cpu_detect(&new_cpu_data);
	/* head.S usually sets up the first capability word, so do it here. */
	new_cpu_data.x86_capability[0] = cpuid_edx(1);

	/* Math is always hard! */
	new_cpu_data.hard_math = 1;

#ifdef CONFIG_X86_MCE
	mce_disabled = 1;
#endif

#ifdef CONFIG_ACPI
	acpi_disabled = 1;
	acpi_ht = 0;
#endif

	add_preferred_console("hvc", 0, NULL);

	pm_power_off = lguest_power_off;
	start_kernel();
}
