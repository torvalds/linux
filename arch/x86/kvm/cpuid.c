/*
 * Kernel-based Virtual Machine driver for Linux
 * cpuid support routines
 *
 * derived from arch/x86/kvm/x86.c
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates.
 * Copyright IBM Corporation, 2008
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <asm/i387.h> /* For use_eager_fpu.  Ugh! */
#include <asm/fpu-internal.h> /* For use_eager_fpu.  Ugh! */
#include <asm/user.h>
#include <asm/xsave.h>
#include "cpuid.h"
#include "lapic.h"
#include "mmu.h"
#include "trace.h"

static u32 xstate_required_size(u64 xstate_bv, bool compacted)
{
	int feature_bit = 0;
	u32 ret = XSAVE_HDR_SIZE + XSAVE_HDR_OFFSET;

	xstate_bv &= XSTATE_EXTEND_MASK;
	while (xstate_bv) {
		if (xstate_bv & 0x1) {
		        u32 eax, ebx, ecx, edx, offset;
		        cpuid_count(0xD, feature_bit, &eax, &ebx, &ecx, &edx);
			offset = compacted ? ret : ebx;
			ret = max(ret, offset + eax);
		}

		xstate_bv >>= 1;
		feature_bit++;
	}

	return ret;
}

u64 kvm_supported_xcr0(void)
{
	u64 xcr0 = KVM_SUPPORTED_XCR0 & host_xcr0;

	if (!kvm_x86_ops->mpx_supported())
		xcr0 &= ~(XSTATE_BNDREGS | XSTATE_BNDCSR);

	return xcr0;
}

#define F(x) bit(X86_FEATURE_##x)

int kvm_update_cpuid(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;
	struct kvm_lapic *apic = vcpu->arch.apic;

	best = kvm_find_cpuid_entry(vcpu, 1, 0);
	if (!best)
		return 0;

	/* Update OSXSAVE bit */
	if (cpu_has_xsave && best->function == 0x1) {
		best->ecx &= ~F(OSXSAVE);
		if (kvm_read_cr4_bits(vcpu, X86_CR4_OSXSAVE))
			best->ecx |= F(OSXSAVE);
	}

	if (apic) {
		if (best->ecx & F(TSC_DEADLINE_TIMER))
			apic->lapic_timer.timer_mode_mask = 3 << 17;
		else
			apic->lapic_timer.timer_mode_mask = 1 << 17;
	}

	best = kvm_find_cpuid_entry(vcpu, 0xD, 0);
	if (!best) {
		vcpu->arch.guest_supported_xcr0 = 0;
		vcpu->arch.guest_xstate_size = XSAVE_HDR_SIZE + XSAVE_HDR_OFFSET;
	} else {
		vcpu->arch.guest_supported_xcr0 =
			(best->eax | ((u64)best->edx << 32)) &
			kvm_supported_xcr0();
		vcpu->arch.guest_xstate_size = best->ebx =
			xstate_required_size(vcpu->arch.xcr0, false);
	}

	best = kvm_find_cpuid_entry(vcpu, 0xD, 1);
	if (best && (best->eax & (F(XSAVES) | F(XSAVEC))))
		best->ebx = xstate_required_size(vcpu->arch.xcr0, true);

	vcpu->arch.eager_fpu = guest_cpuid_has_mpx(vcpu);

	/*
	 * The existing code assumes virtual address is 48-bit in the canonical
	 * address checks; exit if it is ever changed.
	 */
	best = kvm_find_cpuid_entry(vcpu, 0x80000008, 0);
	if (best && ((best->eax & 0xff00) >> 8) != 48 &&
		((best->eax & 0xff00) >> 8) != 0)
		return -EINVAL;

	/* Update physical-address width */
	vcpu->arch.maxphyaddr = cpuid_query_maxphyaddr(vcpu);

	kvm_pmu_cpuid_update(vcpu);
	return 0;
}

static int is_efer_nx(void)
{
	unsigned long long efer = 0;

	rdmsrl_safe(MSR_EFER, &efer);
	return efer & EFER_NX;
}

static void cpuid_fix_nx_cap(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_cpuid_entry2 *e, *entry;

	entry = NULL;
	for (i = 0; i < vcpu->arch.cpuid_nent; ++i) {
		e = &vcpu->arch.cpuid_entries[i];
		if (e->function == 0x80000001) {
			entry = e;
			break;
		}
	}
	if (entry && (entry->edx & F(NX)) && !is_efer_nx()) {
		entry->edx &= ~F(NX);
		printk(KERN_INFO "kvm: guest NX capability removed\n");
	}
}

int cpuid_query_maxphyaddr(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x80000000, 0);
	if (!best || best->eax < 0x80000008)
		goto not_found;
	best = kvm_find_cpuid_entry(vcpu, 0x80000008, 0);
	if (best)
		return best->eax & 0xff;
not_found:
	return 36;
}
EXPORT_SYMBOL_GPL(cpuid_query_maxphyaddr);

/* when an old userspace process fills a new kernel module */
int kvm_vcpu_ioctl_set_cpuid(struct kvm_vcpu *vcpu,
			     struct kvm_cpuid *cpuid,
			     struct kvm_cpuid_entry __user *entries)
{
	int r, i;
	struct kvm_cpuid_entry *cpuid_entries;

	r = -E2BIG;
	if (cpuid->nent > KVM_MAX_CPUID_ENTRIES)
		goto out;
	r = -ENOMEM;
	cpuid_entries = vmalloc(sizeof(struct kvm_cpuid_entry) * cpuid->nent);
	if (!cpuid_entries)
		goto out;
	r = -EFAULT;
	if (copy_from_user(cpuid_entries, entries,
			   cpuid->nent * sizeof(struct kvm_cpuid_entry)))
		goto out_free;
	for (i = 0; i < cpuid->nent; i++) {
		vcpu->arch.cpuid_entries[i].function = cpuid_entries[i].function;
		vcpu->arch.cpuid_entries[i].eax = cpuid_entries[i].eax;
		vcpu->arch.cpuid_entries[i].ebx = cpuid_entries[i].ebx;
		vcpu->arch.cpuid_entries[i].ecx = cpuid_entries[i].ecx;
		vcpu->arch.cpuid_entries[i].edx = cpuid_entries[i].edx;
		vcpu->arch.cpuid_entries[i].index = 0;
		vcpu->arch.cpuid_entries[i].flags = 0;
		vcpu->arch.cpuid_entries[i].padding[0] = 0;
		vcpu->arch.cpuid_entries[i].padding[1] = 0;
		vcpu->arch.cpuid_entries[i].padding[2] = 0;
	}
	vcpu->arch.cpuid_nent = cpuid->nent;
	cpuid_fix_nx_cap(vcpu);
	kvm_apic_set_version(vcpu);
	kvm_x86_ops->cpuid_update(vcpu);
	r = kvm_update_cpuid(vcpu);

out_free:
	vfree(cpuid_entries);
out:
	return r;
}

int kvm_vcpu_ioctl_set_cpuid2(struct kvm_vcpu *vcpu,
			      struct kvm_cpuid2 *cpuid,
			      struct kvm_cpuid_entry2 __user *entries)
{
	int r;

	r = -E2BIG;
	if (cpuid->nent > KVM_MAX_CPUID_ENTRIES)
		goto out;
	r = -EFAULT;
	if (copy_from_user(&vcpu->arch.cpuid_entries, entries,
			   cpuid->nent * sizeof(struct kvm_cpuid_entry2)))
		goto out;
	vcpu->arch.cpuid_nent = cpuid->nent;
	kvm_apic_set_version(vcpu);
	kvm_x86_ops->cpuid_update(vcpu);
	r = kvm_update_cpuid(vcpu);
out:
	return r;
}

int kvm_vcpu_ioctl_get_cpuid2(struct kvm_vcpu *vcpu,
			      struct kvm_cpuid2 *cpuid,
			      struct kvm_cpuid_entry2 __user *entries)
{
	int r;

	r = -E2BIG;
	if (cpuid->nent < vcpu->arch.cpuid_nent)
		goto out;
	r = -EFAULT;
	if (copy_to_user(entries, &vcpu->arch.cpuid_entries,
			 vcpu->arch.cpuid_nent * sizeof(struct kvm_cpuid_entry2)))
		goto out;
	return 0;

out:
	cpuid->nent = vcpu->arch.cpuid_nent;
	return r;
}

static void cpuid_mask(u32 *word, int wordnum)
{
	*word &= boot_cpu_data.x86_capability[wordnum];
}

static void do_cpuid_1_ent(struct kvm_cpuid_entry2 *entry, u32 function,
			   u32 index)
{
	entry->function = function;
	entry->index = index;
	cpuid_count(entry->function, entry->index,
		    &entry->eax, &entry->ebx, &entry->ecx, &entry->edx);
	entry->flags = 0;
}

static int __do_cpuid_ent_emulated(struct kvm_cpuid_entry2 *entry,
				   u32 func, u32 index, int *nent, int maxnent)
{
	switch (func) {
	case 0:
		entry->eax = 1;		/* only one leaf currently */
		++*nent;
		break;
	case 1:
		entry->ecx = F(MOVBE);
		++*nent;
		break;
	default:
		break;
	}

	entry->function = func;
	entry->index = index;

	return 0;
}

static inline int __do_cpuid_ent(struct kvm_cpuid_entry2 *entry, u32 function,
				 u32 index, int *nent, int maxnent)
{
	int r;
	unsigned f_nx = is_efer_nx() ? F(NX) : 0;
#ifdef CONFIG_X86_64
	unsigned f_gbpages = (kvm_x86_ops->get_lpage_level() == PT_PDPE_LEVEL)
				? F(GBPAGES) : 0;
	unsigned f_lm = F(LM);
#else
	unsigned f_gbpages = 0;
	unsigned f_lm = 0;
#endif
	unsigned f_rdtscp = kvm_x86_ops->rdtscp_supported() ? F(RDTSCP) : 0;
	unsigned f_invpcid = kvm_x86_ops->invpcid_supported() ? F(INVPCID) : 0;
	unsigned f_mpx = kvm_x86_ops->mpx_supported() ? F(MPX) : 0;
	unsigned f_xsaves = kvm_x86_ops->xsaves_supported() ? F(XSAVES) : 0;

	/* cpuid 1.edx */
	const u32 kvm_supported_word0_x86_features =
		F(FPU) | F(VME) | F(DE) | F(PSE) |
		F(TSC) | F(MSR) | F(PAE) | F(MCE) |
		F(CX8) | F(APIC) | 0 /* Reserved */ | F(SEP) |
		F(MTRR) | F(PGE) | F(MCA) | F(CMOV) |
		F(PAT) | F(PSE36) | 0 /* PSN */ | F(CLFLUSH) |
		0 /* Reserved, DS, ACPI */ | F(MMX) |
		F(FXSR) | F(XMM) | F(XMM2) | F(SELFSNOOP) |
		0 /* HTT, TM, Reserved, PBE */;
	/* cpuid 0x80000001.edx */
	const u32 kvm_supported_word1_x86_features =
		F(FPU) | F(VME) | F(DE) | F(PSE) |
		F(TSC) | F(MSR) | F(PAE) | F(MCE) |
		F(CX8) | F(APIC) | 0 /* Reserved */ | F(SYSCALL) |
		F(MTRR) | F(PGE) | F(MCA) | F(CMOV) |
		F(PAT) | F(PSE36) | 0 /* Reserved */ |
		f_nx | 0 /* Reserved */ | F(MMXEXT) | F(MMX) |
		F(FXSR) | F(FXSR_OPT) | f_gbpages | f_rdtscp |
		0 /* Reserved */ | f_lm | F(3DNOWEXT) | F(3DNOW);
	/* cpuid 1.ecx */
	const u32 kvm_supported_word4_x86_features =
		/* NOTE: MONITOR (and MWAIT) are emulated as NOP,
		 * but *not* advertised to guests via CPUID ! */
		F(XMM3) | F(PCLMULQDQ) | 0 /* DTES64, MONITOR */ |
		0 /* DS-CPL, VMX, SMX, EST */ |
		0 /* TM2 */ | F(SSSE3) | 0 /* CNXT-ID */ | 0 /* Reserved */ |
		F(FMA) | F(CX16) | 0 /* xTPR Update, PDCM */ |
		F(PCID) | 0 /* Reserved, DCA */ | F(XMM4_1) |
		F(XMM4_2) | F(X2APIC) | F(MOVBE) | F(POPCNT) |
		0 /* Reserved*/ | F(AES) | F(XSAVE) | 0 /* OSXSAVE */ | F(AVX) |
		F(F16C) | F(RDRAND);
	/* cpuid 0x80000001.ecx */
	const u32 kvm_supported_word6_x86_features =
		F(LAHF_LM) | F(CMP_LEGACY) | 0 /*SVM*/ | 0 /* ExtApicSpace */ |
		F(CR8_LEGACY) | F(ABM) | F(SSE4A) | F(MISALIGNSSE) |
		F(3DNOWPREFETCH) | F(OSVW) | 0 /* IBS */ | F(XOP) |
		0 /* SKINIT, WDT, LWP */ | F(FMA4) | F(TBM);

	/* cpuid 0xC0000001.edx */
	const u32 kvm_supported_word5_x86_features =
		F(XSTORE) | F(XSTORE_EN) | F(XCRYPT) | F(XCRYPT_EN) |
		F(ACE2) | F(ACE2_EN) | F(PHE) | F(PHE_EN) |
		F(PMM) | F(PMM_EN);

	/* cpuid 7.0.ebx */
	const u32 kvm_supported_word9_x86_features =
		F(FSGSBASE) | F(BMI1) | F(HLE) | F(AVX2) | F(SMEP) |
		F(BMI2) | F(ERMS) | f_invpcid | F(RTM) | f_mpx | F(RDSEED) |
		F(ADX) | F(SMAP) | F(AVX512F) | F(AVX512PF) | F(AVX512ER) |
		F(AVX512CD);

	/* cpuid 0xD.1.eax */
	const u32 kvm_supported_word10_x86_features =
		F(XSAVEOPT) | F(XSAVEC) | F(XGETBV1) | f_xsaves;

	/* all calls to cpuid_count() should be made on the same cpu */
	get_cpu();

	r = -E2BIG;

	if (*nent >= maxnent)
		goto out;

	do_cpuid_1_ent(entry, function, index);
	++*nent;

	switch (function) {
	case 0:
		entry->eax = min(entry->eax, (u32)0xd);
		break;
	case 1:
		entry->edx &= kvm_supported_word0_x86_features;
		cpuid_mask(&entry->edx, 0);
		entry->ecx &= kvm_supported_word4_x86_features;
		cpuid_mask(&entry->ecx, 4);
		/* we support x2apic emulation even if host does not support
		 * it since we emulate x2apic in software */
		entry->ecx |= F(X2APIC);
		break;
	/* function 2 entries are STATEFUL. That is, repeated cpuid commands
	 * may return different values. This forces us to get_cpu() before
	 * issuing the first command, and also to emulate this annoying behavior
	 * in kvm_emulate_cpuid() using KVM_CPUID_FLAG_STATE_READ_NEXT */
	case 2: {
		int t, times = entry->eax & 0xff;

		entry->flags |= KVM_CPUID_FLAG_STATEFUL_FUNC;
		entry->flags |= KVM_CPUID_FLAG_STATE_READ_NEXT;
		for (t = 1; t < times; ++t) {
			if (*nent >= maxnent)
				goto out;

			do_cpuid_1_ent(&entry[t], function, 0);
			entry[t].flags |= KVM_CPUID_FLAG_STATEFUL_FUNC;
			++*nent;
		}
		break;
	}
	/* function 4 has additional index. */
	case 4: {
		int i, cache_type;

		entry->flags |= KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
		/* read more entries until cache_type is zero */
		for (i = 1; ; ++i) {
			if (*nent >= maxnent)
				goto out;

			cache_type = entry[i - 1].eax & 0x1f;
			if (!cache_type)
				break;
			do_cpuid_1_ent(&entry[i], function, i);
			entry[i].flags |=
			       KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
			++*nent;
		}
		break;
	}
	case 7: {
		entry->flags |= KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
		/* Mask ebx against host capability word 9 */
		if (index == 0) {
			entry->ebx &= kvm_supported_word9_x86_features;
			cpuid_mask(&entry->ebx, 9);
			// TSC_ADJUST is emulated
			entry->ebx |= F(TSC_ADJUST);
		} else
			entry->ebx = 0;
		entry->eax = 0;
		entry->ecx = 0;
		entry->edx = 0;
		break;
	}
	case 9:
		break;
	case 0xa: { /* Architectural Performance Monitoring */
		struct x86_pmu_capability cap;
		union cpuid10_eax eax;
		union cpuid10_edx edx;

		perf_get_x86_pmu_capability(&cap);

		/*
		 * Only support guest architectural pmu on a host
		 * with architectural pmu.
		 */
		if (!cap.version)
			memset(&cap, 0, sizeof(cap));

		eax.split.version_id = min(cap.version, 2);
		eax.split.num_counters = cap.num_counters_gp;
		eax.split.bit_width = cap.bit_width_gp;
		eax.split.mask_length = cap.events_mask_len;

		edx.split.num_counters_fixed = cap.num_counters_fixed;
		edx.split.bit_width_fixed = cap.bit_width_fixed;
		edx.split.reserved = 0;

		entry->eax = eax.full;
		entry->ebx = cap.events_mask;
		entry->ecx = 0;
		entry->edx = edx.full;
		break;
	}
	/* function 0xb has additional index. */
	case 0xb: {
		int i, level_type;

		entry->flags |= KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
		/* read more entries until level_type is zero */
		for (i = 1; ; ++i) {
			if (*nent >= maxnent)
				goto out;

			level_type = entry[i - 1].ecx & 0xff00;
			if (!level_type)
				break;
			do_cpuid_1_ent(&entry[i], function, i);
			entry[i].flags |=
			       KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
			++*nent;
		}
		break;
	}
	case 0xd: {
		int idx, i;
		u64 supported = kvm_supported_xcr0();

		entry->eax &= supported;
		entry->ebx = xstate_required_size(supported, false);
		entry->ecx = entry->ebx;
		entry->edx &= supported >> 32;
		entry->flags |= KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
		if (!supported)
			break;

		for (idx = 1, i = 1; idx < 64; ++idx) {
			u64 mask = ((u64)1 << idx);
			if (*nent >= maxnent)
				goto out;

			do_cpuid_1_ent(&entry[i], function, idx);
			if (idx == 1) {
				entry[i].eax &= kvm_supported_word10_x86_features;
				entry[i].ebx = 0;
				if (entry[i].eax & (F(XSAVES)|F(XSAVEC)))
					entry[i].ebx =
						xstate_required_size(supported,
								     true);
			} else {
				if (entry[i].eax == 0 || !(supported & mask))
					continue;
				if (WARN_ON_ONCE(entry[i].ecx & 1))
					continue;
			}
			entry[i].ecx = 0;
			entry[i].edx = 0;
			entry[i].flags |=
			       KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
			++*nent;
			++i;
		}
		break;
	}
	case KVM_CPUID_SIGNATURE: {
		static const char signature[12] = "KVMKVMKVM\0\0";
		const u32 *sigptr = (const u32 *)signature;
		entry->eax = KVM_CPUID_FEATURES;
		entry->ebx = sigptr[0];
		entry->ecx = sigptr[1];
		entry->edx = sigptr[2];
		break;
	}
	case KVM_CPUID_FEATURES:
		entry->eax = (1 << KVM_FEATURE_CLOCKSOURCE) |
			     (1 << KVM_FEATURE_NOP_IO_DELAY) |
			     (1 << KVM_FEATURE_CLOCKSOURCE2) |
			     (1 << KVM_FEATURE_ASYNC_PF) |
			     (1 << KVM_FEATURE_PV_EOI) |
			     (1 << KVM_FEATURE_CLOCKSOURCE_STABLE_BIT) |
			     (1 << KVM_FEATURE_PV_UNHALT);

		if (sched_info_on())
			entry->eax |= (1 << KVM_FEATURE_STEAL_TIME);

		entry->ebx = 0;
		entry->ecx = 0;
		entry->edx = 0;
		break;
	case 0x80000000:
		entry->eax = min(entry->eax, 0x8000001a);
		break;
	case 0x80000001:
		entry->edx &= kvm_supported_word1_x86_features;
		cpuid_mask(&entry->edx, 1);
		entry->ecx &= kvm_supported_word6_x86_features;
		cpuid_mask(&entry->ecx, 6);
		break;
	case 0x80000007: /* Advanced power management */
		/* invariant TSC is CPUID.80000007H:EDX[8] */
		entry->edx &= (1 << 8);
		/* mask against host */
		entry->edx &= boot_cpu_data.x86_power;
		entry->eax = entry->ebx = entry->ecx = 0;
		break;
	case 0x80000008: {
		unsigned g_phys_as = (entry->eax >> 16) & 0xff;
		unsigned virt_as = max((entry->eax >> 8) & 0xff, 48U);
		unsigned phys_as = entry->eax & 0xff;

		if (!g_phys_as)
			g_phys_as = phys_as;
		entry->eax = g_phys_as | (virt_as << 8);
		entry->ebx = entry->edx = 0;
		break;
	}
	case 0x80000019:
		entry->ecx = entry->edx = 0;
		break;
	case 0x8000001a:
		break;
	case 0x8000001d:
		break;
	/*Add support for Centaur's CPUID instruction*/
	case 0xC0000000:
		/*Just support up to 0xC0000004 now*/
		entry->eax = min(entry->eax, 0xC0000004);
		break;
	case 0xC0000001:
		entry->edx &= kvm_supported_word5_x86_features;
		cpuid_mask(&entry->edx, 5);
		break;
	case 3: /* Processor serial number */
	case 5: /* MONITOR/MWAIT */
	case 6: /* Thermal management */
	case 0xC0000002:
	case 0xC0000003:
	case 0xC0000004:
	default:
		entry->eax = entry->ebx = entry->ecx = entry->edx = 0;
		break;
	}

	kvm_x86_ops->set_supported_cpuid(function, entry);

	r = 0;

out:
	put_cpu();

	return r;
}

static int do_cpuid_ent(struct kvm_cpuid_entry2 *entry, u32 func,
			u32 idx, int *nent, int maxnent, unsigned int type)
{
	if (type == KVM_GET_EMULATED_CPUID)
		return __do_cpuid_ent_emulated(entry, func, idx, nent, maxnent);

	return __do_cpuid_ent(entry, func, idx, nent, maxnent);
}

#undef F

struct kvm_cpuid_param {
	u32 func;
	u32 idx;
	bool has_leaf_count;
	bool (*qualifier)(const struct kvm_cpuid_param *param);
};

static bool is_centaur_cpu(const struct kvm_cpuid_param *param)
{
	return boot_cpu_data.x86_vendor == X86_VENDOR_CENTAUR;
}

static bool sanity_check_entries(struct kvm_cpuid_entry2 __user *entries,
				 __u32 num_entries, unsigned int ioctl_type)
{
	int i;
	__u32 pad[3];

	if (ioctl_type != KVM_GET_EMULATED_CPUID)
		return false;

	/*
	 * We want to make sure that ->padding is being passed clean from
	 * userspace in case we want to use it for something in the future.
	 *
	 * Sadly, this wasn't enforced for KVM_GET_SUPPORTED_CPUID and so we
	 * have to give ourselves satisfied only with the emulated side. /me
	 * sheds a tear.
	 */
	for (i = 0; i < num_entries; i++) {
		if (copy_from_user(pad, entries[i].padding, sizeof(pad)))
			return true;

		if (pad[0] || pad[1] || pad[2])
			return true;
	}
	return false;
}

int kvm_dev_ioctl_get_cpuid(struct kvm_cpuid2 *cpuid,
			    struct kvm_cpuid_entry2 __user *entries,
			    unsigned int type)
{
	struct kvm_cpuid_entry2 *cpuid_entries;
	int limit, nent = 0, r = -E2BIG, i;
	u32 func;
	static const struct kvm_cpuid_param param[] = {
		{ .func = 0, .has_leaf_count = true },
		{ .func = 0x80000000, .has_leaf_count = true },
		{ .func = 0xC0000000, .qualifier = is_centaur_cpu, .has_leaf_count = true },
		{ .func = KVM_CPUID_SIGNATURE },
		{ .func = KVM_CPUID_FEATURES },
	};

	if (cpuid->nent < 1)
		goto out;
	if (cpuid->nent > KVM_MAX_CPUID_ENTRIES)
		cpuid->nent = KVM_MAX_CPUID_ENTRIES;

	if (sanity_check_entries(entries, cpuid->nent, type))
		return -EINVAL;

	r = -ENOMEM;
	cpuid_entries = vzalloc(sizeof(struct kvm_cpuid_entry2) * cpuid->nent);
	if (!cpuid_entries)
		goto out;

	r = 0;
	for (i = 0; i < ARRAY_SIZE(param); i++) {
		const struct kvm_cpuid_param *ent = &param[i];

		if (ent->qualifier && !ent->qualifier(ent))
			continue;

		r = do_cpuid_ent(&cpuid_entries[nent], ent->func, ent->idx,
				&nent, cpuid->nent, type);

		if (r)
			goto out_free;

		if (!ent->has_leaf_count)
			continue;

		limit = cpuid_entries[nent - 1].eax;
		for (func = ent->func + 1; func <= limit && nent < cpuid->nent && r == 0; ++func)
			r = do_cpuid_ent(&cpuid_entries[nent], func, ent->idx,
				     &nent, cpuid->nent, type);

		if (r)
			goto out_free;
	}

	r = -EFAULT;
	if (copy_to_user(entries, cpuid_entries,
			 nent * sizeof(struct kvm_cpuid_entry2)))
		goto out_free;
	cpuid->nent = nent;
	r = 0;

out_free:
	vfree(cpuid_entries);
out:
	return r;
}

static int move_to_next_stateful_cpuid_entry(struct kvm_vcpu *vcpu, int i)
{
	struct kvm_cpuid_entry2 *e = &vcpu->arch.cpuid_entries[i];
	int j, nent = vcpu->arch.cpuid_nent;

	e->flags &= ~KVM_CPUID_FLAG_STATE_READ_NEXT;
	/* when no next entry is found, the current entry[i] is reselected */
	for (j = i + 1; ; j = (j + 1) % nent) {
		struct kvm_cpuid_entry2 *ej = &vcpu->arch.cpuid_entries[j];
		if (ej->function == e->function) {
			ej->flags |= KVM_CPUID_FLAG_STATE_READ_NEXT;
			return j;
		}
	}
	return 0; /* silence gcc, even though control never reaches here */
}

/* find an entry with matching function, matching index (if needed), and that
 * should be read next (if it's stateful) */
static int is_matching_cpuid_entry(struct kvm_cpuid_entry2 *e,
	u32 function, u32 index)
{
	if (e->function != function)
		return 0;
	if ((e->flags & KVM_CPUID_FLAG_SIGNIFCANT_INDEX) && e->index != index)
		return 0;
	if ((e->flags & KVM_CPUID_FLAG_STATEFUL_FUNC) &&
	    !(e->flags & KVM_CPUID_FLAG_STATE_READ_NEXT))
		return 0;
	return 1;
}

struct kvm_cpuid_entry2 *kvm_find_cpuid_entry(struct kvm_vcpu *vcpu,
					      u32 function, u32 index)
{
	int i;
	struct kvm_cpuid_entry2 *best = NULL;

	for (i = 0; i < vcpu->arch.cpuid_nent; ++i) {
		struct kvm_cpuid_entry2 *e;

		e = &vcpu->arch.cpuid_entries[i];
		if (is_matching_cpuid_entry(e, function, index)) {
			if (e->flags & KVM_CPUID_FLAG_STATEFUL_FUNC)
				move_to_next_stateful_cpuid_entry(vcpu, i);
			best = e;
			break;
		}
	}
	return best;
}
EXPORT_SYMBOL_GPL(kvm_find_cpuid_entry);

/*
 * If no match is found, check whether we exceed the vCPU's limit
 * and return the content of the highest valid _standard_ leaf instead.
 * This is to satisfy the CPUID specification.
 */
static struct kvm_cpuid_entry2* check_cpuid_limit(struct kvm_vcpu *vcpu,
                                                  u32 function, u32 index)
{
	struct kvm_cpuid_entry2 *maxlevel;

	maxlevel = kvm_find_cpuid_entry(vcpu, function & 0x80000000, 0);
	if (!maxlevel || maxlevel->eax >= function)
		return NULL;
	if (function & 0x80000000) {
		maxlevel = kvm_find_cpuid_entry(vcpu, 0, 0);
		if (!maxlevel)
			return NULL;
	}
	return kvm_find_cpuid_entry(vcpu, maxlevel->eax, index);
}

void kvm_cpuid(struct kvm_vcpu *vcpu, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
	u32 function = *eax, index = *ecx;
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, function, index);

	if (!best)
		best = check_cpuid_limit(vcpu, function, index);

	/*
	 * Perfmon not yet supported for L2 guest.
	 */
	if (is_guest_mode(vcpu) && function == 0xa)
		best = NULL;

	if (best) {
		*eax = best->eax;
		*ebx = best->ebx;
		*ecx = best->ecx;
		*edx = best->edx;
	} else
		*eax = *ebx = *ecx = *edx = 0;
	trace_kvm_cpuid(function, *eax, *ebx, *ecx, *edx);
}
EXPORT_SYMBOL_GPL(kvm_cpuid);

void kvm_emulate_cpuid(struct kvm_vcpu *vcpu)
{
	u32 function, eax, ebx, ecx, edx;

	function = eax = kvm_register_read(vcpu, VCPU_REGS_RAX);
	ecx = kvm_register_read(vcpu, VCPU_REGS_RCX);
	kvm_cpuid(vcpu, &eax, &ebx, &ecx, &edx);
	kvm_register_write(vcpu, VCPU_REGS_RAX, eax);
	kvm_register_write(vcpu, VCPU_REGS_RBX, ebx);
	kvm_register_write(vcpu, VCPU_REGS_RCX, ecx);
	kvm_register_write(vcpu, VCPU_REGS_RDX, edx);
	kvm_x86_ops->skip_emulated_instruction(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_emulate_cpuid);
