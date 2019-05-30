/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MSHYPER_H
#define _ASM_X86_MSHYPER_H

#include <linux/types.h>
#include <linux/nmi.h>
#include <asm/io.h>
#include <asm/hyperv-tlfs.h>
#include <asm/nospec-branch.h>

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

#define hv_get_crash_ctl(val) \
	rdmsrl(HV_X64_MSR_CRASH_CTL, val)

void hyperv_callback_vector(void);
void hyperv_reenlightenment_vector(void);
#ifdef CONFIG_TRACING
#define trace_hyperv_callback_vector hyperv_callback_vector
#endif
void hyperv_vector_handler(struct pt_regs *regs);

/*
 * Routines for stimer0 Direct Mode handling.
 * On x86/x64, there are no percpu actions to take.
 */
void hv_stimer0_vector_handler(struct pt_regs *regs);
void hv_stimer0_callback_vector(void);

static inline void hv_enable_stimer0_percpu_irq(int irq) {}
static inline void hv_disable_stimer0_percpu_irq(int irq) {}


#if IS_ENABLED(CONFIG_HYPERV)
extern struct clocksource *hyperv_cs;
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

void hyperv_reenlightenment_intr(struct pt_regs *regs);
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

#else /* CONFIG_HYPERV */
static inline void hyperv_init(void) {}
static inline void hyperv_setup_mmu_ops(void) {}
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

#ifdef CONFIG_HYPERV_TSCPAGE
struct ms_hyperv_tsc_page *hv_get_tsc_page(void);
static inline u64 hv_read_tsc_page_tsc(const struct ms_hyperv_tsc_page *tsc_pg,
				       u64 *cur_tsc)
{
	u64 scale, offset;
	u32 sequence;

	/*
	 * The protocol for reading Hyper-V TSC page is specified in Hypervisor
	 * Top-Level Functional Specification ver. 3.0 and above. To get the
	 * reference time we must do the following:
	 * - READ ReferenceTscSequence
	 *   A special '0' value indicates the time source is unreliable and we
	 *   need to use something else. The currently published specification
	 *   versions (up to 4.0b) contain a mistake and wrongly claim '-1'
	 *   instead of '0' as the special value, see commit c35b82ef0294.
	 * - ReferenceTime =
	 *        ((RDTSC() * ReferenceTscScale) >> 64) + ReferenceTscOffset
	 * - READ ReferenceTscSequence again. In case its value has changed
	 *   since our first reading we need to discard ReferenceTime and repeat
	 *   the whole sequence as the hypervisor was updating the page in
	 *   between.
	 */
	do {
		sequence = READ_ONCE(tsc_pg->tsc_sequence);
		if (!sequence)
			return U64_MAX;
		/*
		 * Make sure we read sequence before we read other values from
		 * TSC page.
		 */
		smp_rmb();

		scale = READ_ONCE(tsc_pg->tsc_scale);
		offset = READ_ONCE(tsc_pg->tsc_offset);
		*cur_tsc = rdtsc_ordered();

		/*
		 * Make sure we read sequence after we read all other values
		 * from TSC page.
		 */
		smp_rmb();

	} while (READ_ONCE(tsc_pg->tsc_sequence) != sequence);

	return mul_u64_u64_shr(*cur_tsc, scale, 64) + offset;
}

static inline u64 hv_read_tsc_page(const struct ms_hyperv_tsc_page *tsc_pg)
{
	u64 cur_tsc;

	return hv_read_tsc_page_tsc(tsc_pg, &cur_tsc);
}

#else
static inline struct ms_hyperv_tsc_page *hv_get_tsc_page(void)
{
	return NULL;
}

static inline u64 hv_read_tsc_page_tsc(const struct ms_hyperv_tsc_page *tsc_pg,
				       u64 *cur_tsc)
{
	BUG();
	return U64_MAX;
}
#endif

#include <asm-generic/mshyperv.h>

#endif
