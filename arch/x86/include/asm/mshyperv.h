/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MSHYPER_H
#define _ASM_X86_MSHYPER_H

#include <linux/types.h>
#include <linux/nmi.h>
#include <linux/msi.h>
#include <asm/io.h>
#include <asm/hyperv-tlfs.h>
#include <asm/nospec-branch.h>
#include <asm/paravirt.h>

typedef int (*hyperv_fill_flush_list_func)(
		struct hv_guest_mapping_flush_list *flush,
		void *data);

#define hv_init_timer(timer, tick) \
	wrmsrl(HV_X64_MSR_STIMER0_COUNT + (2*timer), tick)
#define hv_init_timer_config(timer, val) \
	wrmsrl(HV_X64_MSR_STIMER0_CONFIG + (2*timer), val)

#define hv_get_simp(val) rdmsrl(HV_X64_MSR_SIMP, val)
#define hv_set_simp(val) wrmsrl(HV_X64_MSR_SIMP, val)

#define hv_get_siefp(val) rdmsrl(HV_X64_MSR_SIEFP, val)
#define hv_set_siefp(val) wrmsrl(HV_X64_MSR_SIEFP, val)

#define hv_get_synic_state(val) rdmsrl(HV_X64_MSR_SCONTROL, val)
#define hv_set_synic_state(val) wrmsrl(HV_X64_MSR_SCONTROL, val)

#define hv_get_vp_index(index) rdmsrl(HV_X64_MSR_VP_INDEX, index)

#define hv_signal_eom() wrmsrl(HV_X64_MSR_EOM, 0)

#define hv_get_synint_state(int_num, val) \
	rdmsrl(HV_X64_MSR_SINT0 + int_num, val)
#define hv_set_synint_state(int_num, val) \
	wrmsrl(HV_X64_MSR_SINT0 + int_num, val)
#define hv_recommend_using_aeoi() \
	(!(ms_hyperv.hints & HV_DEPRECATING_AEOI_RECOMMENDED))

#define hv_get_crash_ctl(val) \
	rdmsrl(HV_X64_MSR_CRASH_CTL, val)

#define hv_get_time_ref_count(val) \
	rdmsrl(HV_X64_MSR_TIME_REF_COUNT, val)

#define hv_get_reference_tsc(val) \
	rdmsrl(HV_X64_MSR_REFERENCE_TSC, val)
#define hv_set_reference_tsc(val) \
	wrmsrl(HV_X64_MSR_REFERENCE_TSC, val)
#define hv_set_clocksource_vdso(val) \
	((val).vdso_clock_mode = VDSO_CLOCKMODE_HVCLOCK)
#define hv_enable_vdso_clocksource() \
	vclocks_set_used(VDSO_CLOCKMODE_HVCLOCK);
#define hv_get_raw_timer() rdtsc_ordered()
#define hv_get_vector() HYPERVISOR_CALLBACK_VECTOR

/*
 * Reference to pv_ops must be inline so objtool
 * detection of noinstr violations can work correctly.
 */
static __always_inline void hv_setup_sched_clock(void *sched_clock)
{
#ifdef CONFIG_PARAVIRT
	pv_ops.time.sched_clock = sched_clock;
#endif
}

void hyperv_vector_handler(struct pt_regs *regs);

static inline void hv_enable_stimer0_percpu_irq(int irq) {}
static inline void hv_disable_stimer0_percpu_irq(int irq) {}


#if IS_ENABLED(CONFIG_HYPERV)
extern void *hv_hypercall_pg;
extern void  __percpu  **hyperv_pcpu_input_arg;

static inline u64 hv_do_hypercall(u64 control, void *input, void *output)
{
	u64 input_address = input ? virt_to_phys(input) : 0;
	u64 output_address = output ? virt_to_phys(output) : 0;
	u64 hv_status;

#ifdef CONFIG_X86_64
	if (!hv_hypercall_pg)
		return U64_MAX;

	__asm__ __volatile__("mov %4, %%r8\n"
			     CALL_NOSPEC
			     : "=a" (hv_status), ASM_CALL_CONSTRAINT,
			       "+c" (control), "+d" (input_address)
			     :  "r" (output_address),
				THUNK_TARGET(hv_hypercall_pg)
			     : "cc", "memory", "r8", "r9", "r10", "r11");
#else
	u32 input_address_hi = upper_32_bits(input_address);
	u32 input_address_lo = lower_32_bits(input_address);
	u32 output_address_hi = upper_32_bits(output_address);
	u32 output_address_lo = lower_32_bits(output_address);

	if (!hv_hypercall_pg)
		return U64_MAX;

	__asm__ __volatile__(CALL_NOSPEC
			     : "=A" (hv_status),
			       "+c" (input_address_lo), ASM_CALL_CONSTRAINT
			     : "A" (control),
			       "b" (input_address_hi),
			       "D"(output_address_hi), "S"(output_address_lo),
			       THUNK_TARGET(hv_hypercall_pg)
			     : "cc", "memory");
#endif /* !x86_64 */
	return hv_status;
}

/* Fast hypercall with 8 bytes of input and no output */
static inline u64 hv_do_fast_hypercall8(u16 code, u64 input1)
{
	u64 hv_status, control = (u64)code | HV_HYPERCALL_FAST_BIT;

#ifdef CONFIG_X86_64
	{
		__asm__ __volatile__(CALL_NOSPEC
				     : "=a" (hv_status), ASM_CALL_CONSTRAINT,
				       "+c" (control), "+d" (input1)
				     : THUNK_TARGET(hv_hypercall_pg)
				     : "cc", "r8", "r9", "r10", "r11");
	}
#else
	{
		u32 input1_hi = upper_32_bits(input1);
		u32 input1_lo = lower_32_bits(input1);

		__asm__ __volatile__ (CALL_NOSPEC
				      : "=A"(hv_status),
					"+c"(input1_lo),
					ASM_CALL_CONSTRAINT
				      :	"A" (control),
					"b" (input1_hi),
					THUNK_TARGET(hv_hypercall_pg)
				      : "cc", "edi", "esi");
	}
#endif
		return hv_status;
}

/* Fast hypercall with 16 bytes of input */
static inline u64 hv_do_fast_hypercall16(u16 code, u64 input1, u64 input2)
{
	u64 hv_status, control = (u64)code | HV_HYPERCALL_FAST_BIT;

#ifdef CONFIG_X86_64
	{
		__asm__ __volatile__("mov %4, %%r8\n"
				     CALL_NOSPEC
				     : "=a" (hv_status), ASM_CALL_CONSTRAINT,
				       "+c" (control), "+d" (input1)
				     : "r" (input2),
				       THUNK_TARGET(hv_hypercall_pg)
				     : "cc", "r8", "r9", "r10", "r11");
	}
#else
	{
		u32 input1_hi = upper_32_bits(input1);
		u32 input1_lo = lower_32_bits(input1);
		u32 input2_hi = upper_32_bits(input2);
		u32 input2_lo = lower_32_bits(input2);

		__asm__ __volatile__ (CALL_NOSPEC
				      : "=A"(hv_status),
					"+c"(input1_lo), ASM_CALL_CONSTRAINT
				      :	"A" (control), "b" (input1_hi),
					"D"(input2_hi), "S"(input2_lo),
					THUNK_TARGET(hv_hypercall_pg)
				      : "cc");
	}
#endif
	return hv_status;
}

/*
 * Rep hypercalls. Callers of this functions are supposed to ensure that
 * rep_count and varhead_size comply with Hyper-V hypercall definition.
 */
static inline u64 hv_do_rep_hypercall(u16 code, u16 rep_count, u16 varhead_size,
				      void *input, void *output)
{
	u64 control = code;
	u64 status;
	u16 rep_comp;

	control |= (u64)varhead_size << HV_HYPERCALL_VARHEAD_OFFSET;
	control |= (u64)rep_count << HV_HYPERCALL_REP_COMP_OFFSET;

	do {
		status = hv_do_hypercall(control, input, output);
		if ((status & HV_HYPERCALL_RESULT_MASK) != HV_STATUS_SUCCESS)
			return status;

		/* Bits 32-43 of status have 'Reps completed' data. */
		rep_comp = (status & HV_HYPERCALL_REP_COMP_MASK) >>
			HV_HYPERCALL_REP_COMP_OFFSET;

		control &= ~HV_HYPERCALL_REP_START_MASK;
		control |= (u64)rep_comp << HV_HYPERCALL_REP_START_OFFSET;

		touch_nmi_watchdog();
	} while (rep_comp < rep_count);

	return status;
}

extern struct hv_vp_assist_page **hv_vp_assist_page;

static inline struct hv_vp_assist_page *hv_get_vp_assist_page(unsigned int cpu)
{
	if (!hv_vp_assist_page)
		return NULL;

	return hv_vp_assist_page[cpu];
}

void __init hyperv_init(void);
void hyperv_setup_mmu_ops(void);
void *hv_alloc_hyperv_page(void);
void *hv_alloc_hyperv_zeroed_page(void);
void hv_free_hyperv_page(unsigned long addr);
void set_hv_tscchange_cb(void (*cb)(void));
void clear_hv_tscchange_cb(void);
void hyperv_stop_tsc_emulation(void);
int hyperv_flush_guest_mapping(u64 as);
int hyperv_flush_guest_mapping_range(u64 as,
		hyperv_fill_flush_list_func fill_func, void *data);
int hyperv_fill_flush_guest_mapping_list(
		struct hv_guest_mapping_flush_list *flush,
		u64 start_gfn, u64 end_gfn);

#ifdef CONFIG_X86_64
void hv_apic_init(void);
void __init hv_init_spinlocks(void);
bool hv_vcpu_is_preempted(int vcpu);
#else
static inline void hv_apic_init(void) {}
#endif

static inline void hv_set_msi_entry_from_desc(union hv_msi_entry *msi_entry,
					      struct msi_desc *msi_desc)
{
	msi_entry->address = msi_desc->msg.address_lo;
	msi_entry->data = msi_desc->msg.data;
}

#else /* CONFIG_HYPERV */
static inline void hyperv_init(void) {}
static inline void hyperv_setup_mmu_ops(void) {}
static inline void *hv_alloc_hyperv_page(void) { return NULL; }
static inline void hv_free_hyperv_page(unsigned long addr) {}
static inline void set_hv_tscchange_cb(void (*cb)(void)) {}
static inline void clear_hv_tscchange_cb(void) {}
static inline void hyperv_stop_tsc_emulation(void) {};
static inline struct hv_vp_assist_page *hv_get_vp_assist_page(unsigned int cpu)
{
	return NULL;
}
static inline int hyperv_flush_guest_mapping(u64 as) { return -1; }
static inline int hyperv_flush_guest_mapping_range(u64 as,
		hyperv_fill_flush_list_func fill_func, void *data)
{
	return -1;
}
#endif /* CONFIG_HYPERV */


#include <asm-generic/mshyperv.h>

#endif
