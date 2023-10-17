/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MSHYPER_H
#define _ASM_X86_MSHYPER_H

#include <linux/types.h>
#include <linux/nmi.h>
#include <linux/msi.h>
#include <linux/io.h>
#include <asm/hyperv-tlfs.h>
#include <asm/nospec-branch.h>
#include <asm/paravirt.h>
#include <asm/mshyperv.h>

/*
 * Hyper-V always provides a single IO-APIC at this MMIO address.
 * Ideally, the value should be looked up in ACPI tables, but it
 * is needed for mapping the IO-APIC early in boot on Confidential
 * VMs, before ACPI functions can be used.
 */
#define HV_IOAPIC_BASE_ADDRESS 0xfec00000

#define HV_VTL_NORMAL 0x0
#define HV_VTL_SECURE 0x1
#define HV_VTL_MGMT   0x2

union hv_ghcb;

DECLARE_STATIC_KEY_FALSE(isolation_type_snp);
DECLARE_STATIC_KEY_FALSE(isolation_type_tdx);

typedef int (*hyperv_fill_flush_list_func)(
		struct hv_guest_mapping_flush_list *flush,
		void *data);

void hyperv_vector_handler(struct pt_regs *regs);

static inline unsigned char hv_get_nmi_reason(void)
{
	return 0;
}

#if IS_ENABLED(CONFIG_HYPERV)
extern int hyperv_init_cpuhp;
extern bool hyperv_paravisor_present;

extern void *hv_hypercall_pg;

extern u64 hv_current_partition_id;

extern union hv_ghcb * __percpu *hv_ghcb_pg;

bool hv_isolation_type_snp(void);
bool hv_isolation_type_tdx(void);
u64 hv_tdx_hypercall(u64 control, u64 param1, u64 param2);

/*
 * DEFAULT INIT GPAT and SEGMENT LIMIT value in struct VMSA
 * to start AP in enlightened SEV guest.
 */
#define HV_AP_INIT_GPAT_DEFAULT		0x0007040600070406ULL
#define HV_AP_SEGMENT_LIMIT		0xffffffff

int hv_call_deposit_pages(int node, u64 partition_id, u32 num_pages);
int hv_call_add_logical_proc(int node, u32 lp_index, u32 acpi_id);
int hv_call_create_vp(int node, u64 partition_id, u32 vp_index, u32 flags);

/*
 * If the hypercall involves no input or output parameters, the hypervisor
 * ignores the corresponding GPA pointer.
 */
static inline u64 hv_do_hypercall(u64 control, void *input, void *output)
{
	u64 input_address = input ? virt_to_phys(input) : 0;
	u64 output_address = output ? virt_to_phys(output) : 0;
	u64 hv_status;

#ifdef CONFIG_X86_64
	if (hv_isolation_type_tdx() && !hyperv_paravisor_present)
		return hv_tdx_hypercall(control, input_address, output_address);

	if (hv_isolation_type_snp() && !hyperv_paravisor_present) {
		__asm__ __volatile__("mov %4, %%r8\n"
				     "vmmcall"
				     : "=a" (hv_status), ASM_CALL_CONSTRAINT,
				       "+c" (control), "+d" (input_address)
				     :  "r" (output_address)
				     : "cc", "memory", "r8", "r9", "r10", "r11");
		return hv_status;
	}

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

/* Hypercall to the L0 hypervisor */
static inline u64 hv_do_nested_hypercall(u64 control, void *input, void *output)
{
	return hv_do_hypercall(control | HV_HYPERCALL_NESTED, input, output);
}

/* Fast hypercall with 8 bytes of input and no output */
static inline u64 _hv_do_fast_hypercall8(u64 control, u64 input1)
{
	u64 hv_status;

#ifdef CONFIG_X86_64
	if (hv_isolation_type_tdx() && !hyperv_paravisor_present)
		return hv_tdx_hypercall(control, input1, 0);

	if (hv_isolation_type_snp() && !hyperv_paravisor_present) {
		__asm__ __volatile__(
				"vmmcall"
				: "=a" (hv_status), ASM_CALL_CONSTRAINT,
				"+c" (control), "+d" (input1)
				:: "cc", "r8", "r9", "r10", "r11");
	} else {
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

static inline u64 hv_do_fast_hypercall8(u16 code, u64 input1)
{
	u64 control = (u64)code | HV_HYPERCALL_FAST_BIT;

	return _hv_do_fast_hypercall8(control, input1);
}

static inline u64 hv_do_fast_nested_hypercall8(u16 code, u64 input1)
{
	u64 control = (u64)code | HV_HYPERCALL_FAST_BIT | HV_HYPERCALL_NESTED;

	return _hv_do_fast_hypercall8(control, input1);
}

/* Fast hypercall with 16 bytes of input */
static inline u64 _hv_do_fast_hypercall16(u64 control, u64 input1, u64 input2)
{
	u64 hv_status;

#ifdef CONFIG_X86_64
	if (hv_isolation_type_tdx() && !hyperv_paravisor_present)
		return hv_tdx_hypercall(control, input1, input2);

	if (hv_isolation_type_snp() && !hyperv_paravisor_present) {
		__asm__ __volatile__("mov %4, %%r8\n"
				     "vmmcall"
				     : "=a" (hv_status), ASM_CALL_CONSTRAINT,
				       "+c" (control), "+d" (input1)
				     : "r" (input2)
				     : "cc", "r8", "r9", "r10", "r11");
	} else {
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

static inline u64 hv_do_fast_hypercall16(u16 code, u64 input1, u64 input2)
{
	u64 control = (u64)code | HV_HYPERCALL_FAST_BIT;

	return _hv_do_fast_hypercall16(control, input1, input2);
}

static inline u64 hv_do_fast_nested_hypercall16(u16 code, u64 input1, u64 input2)
{
	u64 control = (u64)code | HV_HYPERCALL_FAST_BIT | HV_HYPERCALL_NESTED;

	return _hv_do_fast_hypercall16(control, input1, input2);
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

struct irq_domain *hv_create_pci_msi_domain(void);

int hv_map_ioapic_interrupt(int ioapic_id, bool level, int vcpu, int vector,
		struct hv_interrupt_entry *entry);
int hv_unmap_ioapic_interrupt(int ioapic_id, struct hv_interrupt_entry *entry);

#ifdef CONFIG_AMD_MEM_ENCRYPT
bool hv_ghcb_negotiate_protocol(void);
void __noreturn hv_ghcb_terminate(unsigned int set, unsigned int reason);
int hv_snp_boot_ap(int cpu, unsigned long start_ip);
#else
static inline bool hv_ghcb_negotiate_protocol(void) { return false; }
static inline void hv_ghcb_terminate(unsigned int set, unsigned int reason) {}
static inline int hv_snp_boot_ap(int cpu, unsigned long start_ip) { return 0; }
#endif

#if defined(CONFIG_AMD_MEM_ENCRYPT) || defined(CONFIG_INTEL_TDX_GUEST)
void hv_vtom_init(void);
void hv_ivm_msr_write(u64 msr, u64 value);
void hv_ivm_msr_read(u64 msr, u64 *value);
#else
static inline void hv_vtom_init(void) {}
static inline void hv_ivm_msr_write(u64 msr, u64 value) {}
static inline void hv_ivm_msr_read(u64 msr, u64 *value) {}
#endif

static inline bool hv_is_synic_reg(unsigned int reg)
{
	return (reg >= HV_REGISTER_SCONTROL) &&
	       (reg <= HV_REGISTER_SINT15);
}

static inline bool hv_is_sint_reg(unsigned int reg)
{
	return (reg >= HV_REGISTER_SINT0) &&
	       (reg <= HV_REGISTER_SINT15);
}

u64 hv_get_register(unsigned int reg);
void hv_set_register(unsigned int reg, u64 value);
u64 hv_get_non_nested_register(unsigned int reg);
void hv_set_non_nested_register(unsigned int reg, u64 value);

static __always_inline u64 hv_raw_get_register(unsigned int reg)
{
	return __rdmsr(reg);
}

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
static inline void hv_set_register(unsigned int reg, u64 value) { }
static inline u64 hv_get_register(unsigned int reg) { return 0; }
static inline void hv_set_non_nested_register(unsigned int reg, u64 value) { }
static inline u64 hv_get_non_nested_register(unsigned int reg) { return 0; }
#endif /* CONFIG_HYPERV */


#ifdef CONFIG_HYPERV_VTL_MODE
void __init hv_vtl_init_platform(void);
int __init hv_vtl_early_init(void);
#else
static inline void __init hv_vtl_init_platform(void) {}
static inline int __init hv_vtl_early_init(void) { return 0; }
#endif

#include <asm-generic/mshyperv.h>

#endif
