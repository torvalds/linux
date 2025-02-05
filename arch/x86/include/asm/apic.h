/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_X86_APIC_H
#define _ASM_X86_APIC_H

#include <linux/cpumask.h>
#include <linux/static_call.h>

#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <asm/apicdef.h>
#include <linux/atomic.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/msr.h>
#include <asm/hardirq.h>
#include <asm/io.h>
#include <asm/posted_intr.h>

#define ARCH_APICTIMER_STOPS_ON_C3	1

/* Macros for apic_extnmi which controls external NMI masking */
#define APIC_EXTNMI_BSP		0 /* Default */
#define APIC_EXTNMI_ALL		1
#define APIC_EXTNMI_NONE	2

/*
 * Debugging macros
 */
#define APIC_QUIET   0
#define APIC_VERBOSE 1
#define APIC_DEBUG   2

/*
 * Define the default level of output to be very little This can be turned
 * up by using apic=verbose for more information and apic=debug for _lots_
 * of information.  apic_verbosity is defined in apic.c
 */
#define apic_printk(v, s, a...)			\
do {						\
	if ((v) <= apic_verbosity)		\
		printk(s, ##a);			\
} while (0)

#define apic_pr_verbose(s, a...)	apic_printk(APIC_VERBOSE, KERN_INFO s, ##a)
#define apic_pr_debug(s, a...)		apic_printk(APIC_DEBUG, KERN_DEBUG s, ##a)
#define apic_pr_debug_cont(s, a...)	apic_printk(APIC_DEBUG, KERN_CONT s, ##a)
/* Unconditional debug prints for code which is guarded by apic_verbosity already */
#define apic_dbg(s, a...)		printk(KERN_DEBUG s, ##a)

#if defined(CONFIG_X86_LOCAL_APIC) && defined(CONFIG_X86_32)
extern void x86_32_probe_apic(void);
#else
static inline void x86_32_probe_apic(void) { }
#endif

extern u32 cpuid_to_apicid[];

#define CPU_ACPIID_INVALID	U32_MAX

#ifdef CONFIG_X86_LOCAL_APIC

extern int apic_verbosity;
extern int local_apic_timer_c2_ok;

extern bool apic_is_disabled;
extern unsigned int lapic_timer_period;

extern enum apic_intr_mode_id apic_intr_mode;
enum apic_intr_mode_id {
	APIC_PIC,
	APIC_VIRTUAL_WIRE,
	APIC_VIRTUAL_WIRE_NO_CONFIG,
	APIC_SYMMETRIC_IO,
	APIC_SYMMETRIC_IO_NO_ROUTING
};

/*
 * With 82489DX we can't rely on apic feature bit
 * retrieved via cpuid but still have to deal with
 * such an apic chip so we assume that SMP configuration
 * is found from MP table (64bit case uses ACPI mostly
 * which set smp presence flag as well so we are safe
 * to use this helper too).
 */
static inline bool apic_from_smp_config(void)
{
	return smp_found_config && !apic_is_disabled;
}

/*
 * Basic functions accessing APICs.
 */
#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#endif

static inline void native_apic_mem_write(u32 reg, u32 v)
{
	volatile u32 *addr = (volatile u32 *)(APIC_BASE + reg);

	alternative_io("movl %0, %1", "xchgl %0, %1", X86_BUG_11AP,
		       ASM_OUTPUT2("=r" (v), "=m" (*addr)),
		       ASM_OUTPUT2("0" (v), "m" (*addr)));
}

static inline u32 native_apic_mem_read(u32 reg)
{
	return readl((void __iomem *)(APIC_BASE + reg));
}

static inline void native_apic_mem_eoi(void)
{
	native_apic_mem_write(APIC_EOI, APIC_EOI_ACK);
}

extern void native_apic_icr_write(u32 low, u32 id);
extern u64 native_apic_icr_read(void);

static inline bool apic_is_x2apic_enabled(void)
{
	u64 msr;

	if (rdmsrl_safe(MSR_IA32_APICBASE, &msr))
		return false;
	return msr & X2APIC_ENABLE;
}

extern void enable_IR_x2apic(void);

extern int lapic_get_maxlvt(void);
extern void clear_local_APIC(void);
extern void disconnect_bsp_APIC(int virt_wire_setup);
extern void disable_local_APIC(void);
extern void apic_soft_disable(void);
extern void lapic_shutdown(void);
extern void sync_Arb_IDs(void);
extern void init_bsp_APIC(void);
extern void apic_intr_mode_select(void);
extern void apic_intr_mode_init(void);
extern void init_apic_mappings(void);
void register_lapic_address(unsigned long address);
extern void setup_boot_APIC_clock(void);
extern void setup_secondary_APIC_clock(void);
extern void lapic_update_tsc_freq(void);

#ifdef CONFIG_X86_64
static inline bool apic_force_enable(unsigned long addr)
{
	return false;
}
#else
extern bool apic_force_enable(unsigned long addr);
#endif

extern void apic_ap_setup(void);

/*
 * On 32bit this is mach-xxx local
 */
#ifdef CONFIG_X86_64
extern int apic_is_clustered_box(void);
#else
static inline int apic_is_clustered_box(void)
{
	return 0;
}
#endif

extern int setup_APIC_eilvt(u8 lvt_off, u8 vector, u8 msg_type, u8 mask);
extern void lapic_assign_system_vectors(void);
extern void lapic_assign_legacy_vector(unsigned int isairq, bool replace);
extern void lapic_update_legacy_vectors(void);
extern void lapic_online(void);
extern void lapic_offline(void);
extern bool apic_needs_pit(void);

extern void apic_send_IPI_allbutself(unsigned int vector);

extern void topology_register_apic(u32 apic_id, u32 acpi_id, bool present);
extern void topology_register_boot_apic(u32 apic_id);
extern int topology_hotplug_apic(u32 apic_id, u32 acpi_id);
extern void topology_hotunplug_apic(unsigned int cpu);
extern void topology_apply_cmdline_limits_early(void);
extern void topology_init_possible_cpus(void);
extern void topology_reset_possible_cpus_up(void);

#else /* !CONFIG_X86_LOCAL_APIC */
static inline void lapic_shutdown(void) { }
#define local_apic_timer_c2_ok		1
static inline void init_apic_mappings(void) { }
static inline void disable_local_APIC(void) { }
# define setup_boot_APIC_clock x86_init_noop
# define setup_secondary_APIC_clock x86_init_noop
static inline void lapic_update_tsc_freq(void) { }
static inline void init_bsp_APIC(void) { }
static inline void apic_intr_mode_select(void) { }
static inline void apic_intr_mode_init(void) { }
static inline void lapic_assign_system_vectors(void) { }
static inline void lapic_assign_legacy_vector(unsigned int i, bool r) { }
static inline bool apic_needs_pit(void) { return true; }
static inline void topology_apply_cmdline_limits_early(void) { }
static inline void topology_init_possible_cpus(void) { }
#endif /* !CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_X86_X2APIC
static inline void native_apic_msr_write(u32 reg, u32 v)
{
	if (reg == APIC_DFR || reg == APIC_ID || reg == APIC_LDR ||
	    reg == APIC_LVR)
		return;

	wrmsr(APIC_BASE_MSR + (reg >> 4), v, 0);
}

static inline void native_apic_msr_eoi(void)
{
	__wrmsr(APIC_BASE_MSR + (APIC_EOI >> 4), APIC_EOI_ACK, 0);
}

static inline u32 native_apic_msr_read(u32 reg)
{
	u64 msr;

	if (reg == APIC_DFR)
		return -1;

	rdmsrl(APIC_BASE_MSR + (reg >> 4), msr);
	return (u32)msr;
}

static inline void native_x2apic_icr_write(u32 low, u32 id)
{
	wrmsrl(APIC_BASE_MSR + (APIC_ICR >> 4), ((__u64) id) << 32 | low);
}

static inline u64 native_x2apic_icr_read(void)
{
	unsigned long val;

	rdmsrl(APIC_BASE_MSR + (APIC_ICR >> 4), val);
	return val;
}

extern int x2apic_mode;
extern int x2apic_phys;
extern void __init x2apic_set_max_apicid(u32 apicid);
extern void x2apic_setup(void);
static inline int x2apic_enabled(void)
{
	return boot_cpu_has(X86_FEATURE_X2APIC) && apic_is_x2apic_enabled();
}

#define x2apic_supported()	(boot_cpu_has(X86_FEATURE_X2APIC))
#else /* !CONFIG_X86_X2APIC */
static inline void x2apic_setup(void) { }
static inline int x2apic_enabled(void) { return 0; }
static inline u32 native_apic_msr_read(u32 reg) { BUG(); }
#define x2apic_mode		(0)
#define	x2apic_supported()	(0)
#endif /* !CONFIG_X86_X2APIC */
extern void __init check_x2apic(void);

struct irq_data;

/*
 * Copyright 2004 James Cleverdon, IBM.
 *
 * Generic APIC sub-arch data struct.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */
struct apic {
	/* Hotpath functions first */
	void	(*eoi)(void);
	void	(*native_eoi)(void);
	void	(*write)(u32 reg, u32 v);
	u32	(*read)(u32 reg);

	/* IPI related functions */
	void	(*wait_icr_idle)(void);
	u32	(*safe_wait_icr_idle)(void);

	void	(*send_IPI)(int cpu, int vector);
	void	(*send_IPI_mask)(const struct cpumask *mask, int vector);
	void	(*send_IPI_mask_allbutself)(const struct cpumask *msk, int vec);
	void	(*send_IPI_allbutself)(int vector);
	void	(*send_IPI_all)(int vector);
	void	(*send_IPI_self)(int vector);

	u32	disable_esr		: 1,
		dest_mode_logical	: 1,
		x2apic_set_max_apicid	: 1,
		nmi_to_offline_cpu	: 1;

	u32	(*calc_dest_apicid)(unsigned int cpu);

	/* ICR related functions */
	u64	(*icr_read)(void);
	void	(*icr_write)(u32 low, u32 high);

	/* The limit of the APIC ID space. */
	u32	max_apic_id;

	/* Probe, setup and smpboot functions */
	int	(*probe)(void);
	int	(*acpi_madt_oem_check)(char *oem_id, char *oem_table_id);

	void	(*init_apic_ldr)(void);
	u32	(*cpu_present_to_apicid)(int mps_cpu);

	u32	(*get_apic_id)(u32 id);

	/* wakeup_secondary_cpu */
	int	(*wakeup_secondary_cpu)(u32 apicid, unsigned long start_eip);
	/* wakeup secondary CPU using 64-bit wakeup point */
	int	(*wakeup_secondary_cpu_64)(u32 apicid, unsigned long start_eip);

	char	*name;
};

struct apic_override {
	void	(*eoi)(void);
	void	(*native_eoi)(void);
	void	(*write)(u32 reg, u32 v);
	u32	(*read)(u32 reg);
	void	(*send_IPI)(int cpu, int vector);
	void	(*send_IPI_mask)(const struct cpumask *mask, int vector);
	void	(*send_IPI_mask_allbutself)(const struct cpumask *msk, int vec);
	void	(*send_IPI_allbutself)(int vector);
	void	(*send_IPI_all)(int vector);
	void	(*send_IPI_self)(int vector);
	u64	(*icr_read)(void);
	void	(*icr_write)(u32 low, u32 high);
	int	(*wakeup_secondary_cpu)(u32 apicid, unsigned long start_eip);
	int	(*wakeup_secondary_cpu_64)(u32 apicid, unsigned long start_eip);
};

/*
 * Pointer to the local APIC driver in use on this system (there's
 * always just one such driver in use - the kernel decides via an
 * early probing process which one it picks - and then sticks to it):
 */
extern struct apic *apic;

/*
 * APIC drivers are probed based on how they are listed in the .apicdrivers
 * section. So the order is important and enforced by the ordering
 * of different apic driver files in the Makefile.
 */
#define apic_driver(sym)					\
	static const struct apic *__apicdrivers_##sym __used		\
	__aligned(sizeof(struct apic *))			\
	__section(".apicdrivers") = { &sym }

extern struct apic *__apicdrivers[], *__apicdrivers_end[];

/*
 * APIC functionality to boot other CPUs - only used on SMP:
 */
#ifdef CONFIG_SMP
extern int lapic_can_unplug_cpu(void);
#endif

#ifdef CONFIG_X86_LOCAL_APIC
extern struct apic_override __x86_apic_override;

void __init apic_setup_apic_calls(void);
void __init apic_install_driver(struct apic *driver);

#define apic_update_callback(_callback, _fn) {					\
		__x86_apic_override._callback = _fn;				\
		apic->_callback = _fn;						\
		static_call_update(apic_call_##_callback, _fn);			\
		pr_info("APIC: %s() replaced with %ps()\n", #_callback, _fn);	\
}

#define DECLARE_APIC_CALL(__cb)							\
	DECLARE_STATIC_CALL(apic_call_##__cb, *apic->__cb)

DECLARE_APIC_CALL(eoi);
DECLARE_APIC_CALL(native_eoi);
DECLARE_APIC_CALL(icr_read);
DECLARE_APIC_CALL(icr_write);
DECLARE_APIC_CALL(read);
DECLARE_APIC_CALL(send_IPI);
DECLARE_APIC_CALL(send_IPI_mask);
DECLARE_APIC_CALL(send_IPI_mask_allbutself);
DECLARE_APIC_CALL(send_IPI_allbutself);
DECLARE_APIC_CALL(send_IPI_all);
DECLARE_APIC_CALL(send_IPI_self);
DECLARE_APIC_CALL(wait_icr_idle);
DECLARE_APIC_CALL(wakeup_secondary_cpu);
DECLARE_APIC_CALL(wakeup_secondary_cpu_64);
DECLARE_APIC_CALL(write);

static __always_inline u32 apic_read(u32 reg)
{
	return static_call(apic_call_read)(reg);
}

static __always_inline void apic_write(u32 reg, u32 val)
{
	static_call(apic_call_write)(reg, val);
}

static __always_inline void apic_eoi(void)
{
	static_call(apic_call_eoi)();
}

static __always_inline void apic_native_eoi(void)
{
	static_call(apic_call_native_eoi)();
}

static __always_inline u64 apic_icr_read(void)
{
	return static_call(apic_call_icr_read)();
}

static __always_inline void apic_icr_write(u32 low, u32 high)
{
	static_call(apic_call_icr_write)(low, high);
}

static __always_inline void __apic_send_IPI(int cpu, int vector)
{
	static_call(apic_call_send_IPI)(cpu, vector);
}

static __always_inline void __apic_send_IPI_mask(const struct cpumask *mask, int vector)
{
	static_call_mod(apic_call_send_IPI_mask)(mask, vector);
}

static __always_inline void __apic_send_IPI_mask_allbutself(const struct cpumask *mask, int vector)
{
	static_call(apic_call_send_IPI_mask_allbutself)(mask, vector);
}

static __always_inline void __apic_send_IPI_allbutself(int vector)
{
	static_call(apic_call_send_IPI_allbutself)(vector);
}

static __always_inline void __apic_send_IPI_all(int vector)
{
	static_call(apic_call_send_IPI_all)(vector);
}

static __always_inline void __apic_send_IPI_self(int vector)
{
	static_call_mod(apic_call_send_IPI_self)(vector);
}

static __always_inline void apic_wait_icr_idle(void)
{
	static_call_cond(apic_call_wait_icr_idle)();
}

static __always_inline u32 safe_apic_wait_icr_idle(void)
{
	return apic->safe_wait_icr_idle ? apic->safe_wait_icr_idle() : 0;
}

static __always_inline bool apic_id_valid(u32 apic_id)
{
	return apic_id <= apic->max_apic_id;
}

#else /* CONFIG_X86_LOCAL_APIC */

static inline u32 apic_read(u32 reg) { return 0; }
static inline void apic_write(u32 reg, u32 val) { }
static inline void apic_eoi(void) { }
static inline u64 apic_icr_read(void) { return 0; }
static inline void apic_icr_write(u32 low, u32 high) { }
static inline void apic_wait_icr_idle(void) { }
static inline u32 safe_apic_wait_icr_idle(void) { return 0; }
static inline void apic_native_eoi(void) { WARN_ON_ONCE(1); }
static inline void apic_setup_apic_calls(void) { }

#define apic_update_callback(_callback, _fn) do { } while (0)

#endif /* CONFIG_X86_LOCAL_APIC */

extern void apic_ack_irq(struct irq_data *data);

static inline bool lapic_vector_set_in_irr(unsigned int vector)
{
	u32 irr = apic_read(APIC_IRR + (vector / 32 * 0x10));

	return !!(irr & (1U << (vector % 32)));
}

static inline bool is_vector_pending(unsigned int vector)
{
	return lapic_vector_set_in_irr(vector) || pi_pending_this_cpu(vector);
}

/*
 * Warm reset vector position:
 */
#define TRAMPOLINE_PHYS_LOW		0x467
#define TRAMPOLINE_PHYS_HIGH		0x469

#ifdef CONFIG_X86_LOCAL_APIC

#include <asm/smp.h>

extern struct apic apic_noop;

static inline u32 read_apic_id(void)
{
	u32 reg = apic_read(APIC_ID);

	return apic->get_apic_id(reg);
}

#ifdef CONFIG_X86_64
typedef int (*wakeup_cpu_handler)(int apicid, unsigned long start_eip);
extern int default_acpi_madt_oem_check(char *, char *);
extern void x86_64_probe_apic(void);
#else
static inline int default_acpi_madt_oem_check(char *a, char *b) { return 0; }
static inline void x86_64_probe_apic(void) { }
#endif

extern u32 apic_default_calc_apicid(unsigned int cpu);
extern u32 apic_flat_calc_apicid(unsigned int cpu);

extern u32 default_cpu_present_to_apicid(int mps_cpu);

void apic_send_nmi_to_offline_cpu(unsigned int cpu);

#else /* CONFIG_X86_LOCAL_APIC */

static inline u32 read_apic_id(void) { return 0; }

#endif /* !CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_SMP
void apic_smt_update(void);
#else
static inline void apic_smt_update(void) { }
#endif

struct msi_msg;
struct irq_cfg;

extern void __irq_msi_compose_msg(struct irq_cfg *cfg, struct msi_msg *msg,
				  bool dmar);

extern void ioapic_zap_locks(void);

#endif /* _ASM_X86_APIC_H */
