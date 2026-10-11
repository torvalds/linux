/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The host<->hyp hypercall interface.
 *
 * Copyright (C) 2026 Google LLC
 * Author: Fuad Tabba <fuad.tabba@linux.dev>
 */

#ifndef __ARM64_KVM_HCALL_H__
#define __ARM64_KVM_HCALL_H__

#include <linux/args.h>
#include <linux/arm-smccc.h>
#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/types.h>

#include <asm/barrier.h>
#include <asm/kvm_asm.h>
#include <asm/spectre.h>
#include <asm/virt.h>

typedef u16 pkvm_handle_t;

struct kvm;
struct kvm_s2_mmu;
struct kvm_vcpu;
struct vgic_v3_cpu_if;
struct vgic_v5_cpu_if;

/*
 * A host VA carried by a hypercall argument. At EL2 such a pointer must not
 * be dereferenced until it is translated with kern_hyp_va_host(); sparse
 * flags any use that skips the translation. The tag describes the EL2 view
 * only: the host dereferences its own VAs freely.
 */
#if defined(__KVM_NVHE_HYPERVISOR__) && defined(__CHECKER__)
#define __kern	__attribute__((noderef, address_space(__kern)))
#else
#define __kern
#endif

/*
 * Hypercall signatures are declared as (type, name) argument pairs.
 * __KVM_HCALL_MAP() applies a macro to each pair, in the mold of __MAP()
 * in <linux/syscalls.h>. The ladder is indexed by list entries, two per
 * argument; __KVM_HCALL_MAP_N() takes that count explicitly.
 */
#define __KVM_HCALL_MAP2(m, t, a, ...) m(t, a)
#define __KVM_HCALL_MAP4(m, t, a, ...) m(t, a), __KVM_HCALL_MAP2(m, __VA_ARGS__)
#define __KVM_HCALL_MAP6(m, t, a, ...) m(t, a), __KVM_HCALL_MAP4(m, __VA_ARGS__)
#define __KVM_HCALL_MAP8(m, t, a, ...) m(t, a), __KVM_HCALL_MAP6(m, __VA_ARGS__)
#define __KVM_HCALL_MAP10(m, t, a, ...) m(t, a), __KVM_HCALL_MAP8(m, __VA_ARGS__)
#define __KVM_HCALL_MAP12(m, t, a, ...) m(t, a), __KVM_HCALL_MAP10(m, __VA_ARGS__)
#define __KVM_HCALL_MAP_N(n, m, ...) CONCATENATE(__KVM_HCALL_MAP, n)(m, __VA_ARGS__)
#define __KVM_HCALL_MAP(m, ...) __KVM_HCALL_MAP_N(COUNT_ARGS(__VA_ARGS__), m, __VA_ARGS__)

#define __KVM_HCALL_DECL(t, a)	t a
#define __KVM_HCALL_LONG(t, a)	unsigned long a
#define __KVM_HCALL_CAST(t, a)	(__force t) a
#define __KVM_HCALL_ARGS(t, a)	a

#ifndef __KVM_NVHE_HYPERVISOR__
#define __kvm_call_hyp_nvhe(f, ...)					\
	({								\
		struct arm_smccc_res res;				\
									\
		arm_smccc_1_1_hvc(KVM_HOST_SMCCC_FUNC(f),		\
				  ##__VA_ARGS__, &res);			\
		if (WARN_ON(res.a0 != SMCCC_RET_SUCCESS))		\
			res.a1 = -EOPNOTSUPP;				\
									\
		res.a1;							\
	})

/*
 * Generate a typed stub for each declared hypercall. kvm_call_hyp_nvhe()
 * resolves to the stub, so a call with the wrong argument count or types
 * fails to compile instead of being silently truncated to an SMCCC function
 * number and a pile of registers. The stub inlines to the same SMCCC call
 * the untyped macro used to make.
 */
#define DECLARE_KVM_HOST_HCALL(ret, name, ...)				\
	static __always_inline						\
	ret nvhe_hvc_##name(__KVM_HCALL_MAP(__KVM_HCALL_DECL, __VA_ARGS__)) \
	{								\
		return (ret)__kvm_call_hyp_nvhe(name,			\
			__KVM_HCALL_MAP(__KVM_HCALL_ARGS, __VA_ARGS__));\
	}

#define DECLARE_KVM_HOST_HCALL0(ret, name)				\
	static __always_inline ret nvhe_hvc_##name(void)		\
	{								\
		return (ret)__kvm_call_hyp_nvhe(name);			\
	}

#define kvm_call_hyp_nvhe(f, ...)	nvhe_hvc_##f(__VA_ARGS__)

/*
 * The isb() below is there to guarantee the same behaviour on VHE as on !VHE,
 * where the eret to EL1 acts as a context synchronization event.
 */
#define kvm_call_hyp(f, ...)						\
	do {								\
		if (has_vhe()) {					\
			f(__VA_ARGS__);					\
			isb();						\
		} else {						\
			kvm_call_hyp_nvhe(f, ##__VA_ARGS__);		\
		}							\
	} while (0)

#define kvm_call_hyp_ret(f, ...)					\
	({								\
		typeof(f(__VA_ARGS__)) ret;				\
									\
		if (has_vhe()) {					\
			ret = f(__VA_ARGS__);				\
		} else {						\
			ret = kvm_call_hyp_nvhe(f, ##__VA_ARGS__);	\
		}							\
									\
		ret;							\
	})
#else /* __KVM_NVHE_HYPERVISOR__ */
#define kvm_call_hyp(f, ...) f(__VA_ARGS__)
#define kvm_call_hyp_ret(f, ...) f(__VA_ARGS__)
#define kvm_call_hyp_nvhe(f, ...) f(__VA_ARGS__)

/*
 * At EL2 each declaration emits the canonical signature of the hypercall,
 * which DEFINE_KVM_HOST_HCALL() in hyp-main.c checks the handler
 * definition against.
 */
#define DECLARE_KVM_HOST_HCALL(ret, name, ...)				\
	typedef ret kvm_host_hcall_sig_##name(__KVM_HCALL_MAP(__KVM_HCALL_DECL, __VA_ARGS__));
#define DECLARE_KVM_HOST_HCALL0(ret, name)				\
	typedef ret kvm_host_hcall_sig_##name(void);
#endif /* __KVM_NVHE_HYPERVISOR__ */

/* Hypercalls that are unavailable once pKVM has finalised. */
DECLARE_KVM_HOST_HCALL(int, __pkvm_init,
	phys_addr_t, phys, unsigned long, size,
	unsigned long *, per_cpu_base, u32, hyp_va_bits)
DECLARE_KVM_HOST_HCALL(ulong, __pkvm_create_private_mapping,
	phys_addr_t, phys, size_t, size, u64, prot)
DECLARE_KVM_HOST_HCALL(int, __pkvm_cpu_set_vector,
	enum arm64_hyp_spectre_vector, slot)
DECLARE_KVM_HOST_HCALL0(void, __kvm_enable_ssbs)
DECLARE_KVM_HOST_HCALL0(void, __vgic_v3_init_lrs)
DECLARE_KVM_HOST_HCALL0(u64, __vgic_v3_get_gic_config)

DECLARE_KVM_HOST_HCALL0(int, __pkvm_prot_finalize)

/* Hypercalls that are always available and common to [nh]VHE/pKVM. */
DECLARE_KVM_HOST_HCALL(void, __kvm_adjust_pc,
	struct kvm_vcpu __kern *, vcpu)
DECLARE_KVM_HOST_HCALL(int, __kvm_vcpu_run,
	struct kvm_vcpu __kern *, vcpu)
DECLARE_KVM_HOST_HCALL0(void, __kvm_flush_vm_context)
DECLARE_KVM_HOST_HCALL(void, __kvm_tlb_flush_vmid_ipa,
	struct kvm_s2_mmu __kern *, mmu, phys_addr_t, ipa, int, level)
DECLARE_KVM_HOST_HCALL(void, __kvm_tlb_flush_vmid_ipa_nsh,
	struct kvm_s2_mmu __kern *, mmu, phys_addr_t, ipa, int, level)
DECLARE_KVM_HOST_HCALL(void, __kvm_tlb_flush_vmid,
	struct kvm_s2_mmu __kern *, mmu)
DECLARE_KVM_HOST_HCALL(void, __kvm_tlb_flush_vmid_range,
	struct kvm_s2_mmu __kern *, mmu, phys_addr_t, start, unsigned long, pages)
DECLARE_KVM_HOST_HCALL(void, __kvm_flush_cpu_context,
	struct kvm_s2_mmu __kern *, mmu)
DECLARE_KVM_HOST_HCALL(void, __kvm_timer_set_cntvoff,
	u64, cntvoff)
DECLARE_KVM_HOST_HCALL(int, __tracing_load,
	void __kern *, desc_hva, size_t, desc_size)
DECLARE_KVM_HOST_HCALL0(void, __tracing_unload)
DECLARE_KVM_HOST_HCALL(int, __tracing_enable,
	bool, enable)
DECLARE_KVM_HOST_HCALL(int, __tracing_swap_reader,
	unsigned int, cpu)
DECLARE_KVM_HOST_HCALL(void, __tracing_update_clock,
	u32, mult, u32, shift, u64, epoch_ns, u64, epoch_cyc)
DECLARE_KVM_HOST_HCALL(int, __tracing_reset,
	unsigned int, cpu)
DECLARE_KVM_HOST_HCALL(int, __tracing_enable_event,
	unsigned short, id, bool, enable)
DECLARE_KVM_HOST_HCALL(void, __tracing_write_event,
	u64, id)
DECLARE_KVM_HOST_HCALL(void, __vgic_v5_make_resident,
		       struct vgic_v5_cpu_if __kern *, cpu_if);
DECLARE_KVM_HOST_HCALL(void, __vgic_v5_make_non_resident,
		       struct vgic_v5_cpu_if __kern *, cpu_if);
DECLARE_KVM_HOST_HCALL(void, __vgic_v3_save_aprs,
	struct vgic_v3_cpu_if __kern *, cpu_if)
DECLARE_KVM_HOST_HCALL(void, __vgic_v3_restore_vmcr_aprs,
	struct vgic_v3_cpu_if __kern *, cpu_if)
DECLARE_KVM_HOST_HCALL(void, __vgic_v5_save_apr,
	struct vgic_v5_cpu_if __kern *, cpu_if)
DECLARE_KVM_HOST_HCALL(void, __vgic_v5_restore_vmcr_apr,
	struct vgic_v5_cpu_if __kern *, cpu_if)
DECLARE_KVM_HOST_HCALL(void, __vgic_v5_vdpend,
		       u32, intid, bool, pending, u16, vm);

/* Hypercalls that are available only when pKVM has finalised. */
DECLARE_KVM_HOST_HCALL(int, __pkvm_host_share_hyp,
	u64, pfn)
DECLARE_KVM_HOST_HCALL(int, __pkvm_host_unshare_hyp,
	u64, pfn)
DECLARE_KVM_HOST_HCALL(int, __pkvm_host_donate_guest,
	u64, pfn, u64, gfn)
DECLARE_KVM_HOST_HCALL(int, __pkvm_host_share_guest,
	u64, pfn, u64, gfn, u64, nr_pages, u64, prot)
DECLARE_KVM_HOST_HCALL(int, __pkvm_host_unshare_guest,
	pkvm_handle_t, handle, u64, gfn, u64, nr_pages)
DECLARE_KVM_HOST_HCALL(int, __pkvm_host_relax_perms_guest,
	u64, gfn, u64, prot)
DECLARE_KVM_HOST_HCALL(int, __pkvm_host_wrprotect_guest,
	pkvm_handle_t, handle, u64, gfn, u64, nr_pages)
DECLARE_KVM_HOST_HCALL(int, __pkvm_host_test_clear_young_guest,
	pkvm_handle_t, handle, u64, gfn, u64, nr_pages, bool, mkold)
DECLARE_KVM_HOST_HCALL(int, __pkvm_host_mkyoung_guest,
	u64, gfn)
DECLARE_KVM_HOST_HCALL0(int, __pkvm_reserve_vm)
DECLARE_KVM_HOST_HCALL(void, __pkvm_unreserve_vm,
	pkvm_handle_t, handle)
DECLARE_KVM_HOST_HCALL(int, __pkvm_init_vm,
	struct kvm __kern *, host_kvm, void __kern *, vm_hva,
	void __kern *, pgd_hva)
DECLARE_KVM_HOST_HCALL(int, __pkvm_init_vcpu,
	pkvm_handle_t, handle, struct kvm_vcpu __kern *, host_vcpu,
	void __kern *, vcpu_hva)
DECLARE_KVM_HOST_HCALL0(int, __pkvm_vcpu_in_poison_fault)
DECLARE_KVM_HOST_HCALL(int, __pkvm_force_reclaim_guest_page,
	phys_addr_t, phys)
DECLARE_KVM_HOST_HCALL(int, __pkvm_reclaim_dying_guest_page,
	pkvm_handle_t, handle, u64, gfn)
DECLARE_KVM_HOST_HCALL(int, __pkvm_start_teardown_vm,
	pkvm_handle_t, handle)
DECLARE_KVM_HOST_HCALL(int, __pkvm_finalize_teardown_vm,
	pkvm_handle_t, handle)
DECLARE_KVM_HOST_HCALL(void, __pkvm_vcpu_load,
	pkvm_handle_t, handle, unsigned int, vcpu_idx, u64, hcr_el2)
DECLARE_KVM_HOST_HCALL0(void, __pkvm_vcpu_put)
DECLARE_KVM_HOST_HCALL0(void, __pkvm_vcpu_sync_state)
DECLARE_KVM_HOST_HCALL(void, __pkvm_tlb_flush_vmid,
	pkvm_handle_t, handle)

#endif /* __ARM64_KVM_HCALL_H__ */
