/******************************************************************************
 * arch-x86_32.h
 *
 * Guest OS interface to x86 Xen.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2004-2006, K A Fraser
 */

#ifndef _ASM_X86_XEN_INTERFACE_H
#define _ASM_X86_XEN_INTERFACE_H

/*
 * XEN_GUEST_HANDLE represents a guest pointer, when passed as a field
 * in a struct in memory.
 * XEN_GUEST_HANDLE_PARAM represent a guest pointer, when passed as an
 * hypercall argument.
 * XEN_GUEST_HANDLE_PARAM and XEN_GUEST_HANDLE are the same on X86 but
 * they might not be on other architectures.
 */
#ifdef __XEN__
#define __DEFINE_GUEST_HANDLE(name, type) \
    typedef struct { type *p; } __guest_handle_ ## name
#else
#define __DEFINE_GUEST_HANDLE(name, type) \
    typedef type * __guest_handle_ ## name
#endif

#define DEFINE_GUEST_HANDLE_STRUCT(name) \
	__DEFINE_GUEST_HANDLE(name, struct name)
#define DEFINE_GUEST_HANDLE(name) __DEFINE_GUEST_HANDLE(name, name)
#define GUEST_HANDLE(name)        __guest_handle_ ## name

#ifdef __XEN__
#if defined(__i386__)
#define set_xen_guest_handle(hnd, val)			\
	do {						\
		if (sizeof(hnd) == 8)			\
			*(uint64_t *)&(hnd) = 0;	\
		(hnd).p = val;				\
	} while (0)
#elif defined(__x86_64__)
#define set_xen_guest_handle(hnd, val)	do { (hnd).p = val; } while (0)
#endif
#else
#if defined(__i386__)
#define set_xen_guest_handle(hnd, val)			\
	do {						\
		if (sizeof(hnd) == 8)			\
			*(uint64_t *)&(hnd) = 0;	\
		(hnd) = val;				\
	} while (0)
#elif defined(__x86_64__)
#define set_xen_guest_handle(hnd, val)	do { (hnd) = val; } while (0)
#endif
#endif

#ifndef __ASSEMBLY__
/* Explicitly size integers that represent pfns in the public interface
 * with Xen so that on ARM we can have one ABI that works for 32 and 64
 * bit guests. */
typedef unsigned long xen_pfn_t;
#define PRI_xen_pfn "lx"
typedef unsigned long xen_ulong_t;
#define PRI_xen_ulong "lx"
typedef long xen_long_t;
#define PRI_xen_long "lx"

/* Guest handles for primitive C types. */
__DEFINE_GUEST_HANDLE(uchar, unsigned char);
__DEFINE_GUEST_HANDLE(uint,  unsigned int);
DEFINE_GUEST_HANDLE(char);
DEFINE_GUEST_HANDLE(int);
DEFINE_GUEST_HANDLE(void);
DEFINE_GUEST_HANDLE(uint64_t);
DEFINE_GUEST_HANDLE(uint32_t);
DEFINE_GUEST_HANDLE(xen_pfn_t);
DEFINE_GUEST_HANDLE(xen_ulong_t);
#endif

#ifndef HYPERVISOR_VIRT_START
#define HYPERVISOR_VIRT_START mk_unsigned_long(__HYPERVISOR_VIRT_START)
#endif

#define MACH2PHYS_VIRT_START  mk_unsigned_long(__MACH2PHYS_VIRT_START)
#define MACH2PHYS_VIRT_END    mk_unsigned_long(__MACH2PHYS_VIRT_END)
#define MACH2PHYS_NR_ENTRIES  ((MACH2PHYS_VIRT_END-MACH2PHYS_VIRT_START)>>__MACH2PHYS_SHIFT)

/* Maximum number of virtual CPUs in multi-processor guests. */
#define MAX_VIRT_CPUS 32

/*
 * SEGMENT DESCRIPTOR TABLES
 */
/*
 * A number of GDT entries are reserved by Xen. These are not situated at the
 * start of the GDT because some stupid OSes export hard-coded selector values
 * in their ABI. These hard-coded values are always near the start of the GDT,
 * so Xen places itself out of the way, at the far end of the GDT.
 *
 * NB The LDT is set using the MMUEXT_SET_LDT op of HYPERVISOR_mmuext_op
 */
#define FIRST_RESERVED_GDT_PAGE  14
#define FIRST_RESERVED_GDT_BYTE  (FIRST_RESERVED_GDT_PAGE * 4096)
#define FIRST_RESERVED_GDT_ENTRY (FIRST_RESERVED_GDT_BYTE / 8)

/*
 * Send an array of these to HYPERVISOR_set_trap_table().
 * Terminate the array with a sentinel entry, with traps[].address==0.
 * The privilege level specifies which modes may enter a trap via a software
 * interrupt. On x86/64, since rings 1 and 2 are unavailable, we allocate
 * privilege levels as follows:
 *  Level == 0: No one may enter
 *  Level == 1: Kernel may enter
 *  Level == 2: Kernel may enter
 *  Level == 3: Everyone may enter
 */
#define TI_GET_DPL(_ti)		((_ti)->flags & 3)
#define TI_GET_IF(_ti)		((_ti)->flags & 4)
#define TI_SET_DPL(_ti, _dpl)	((_ti)->flags |= (_dpl))
#define TI_SET_IF(_ti, _if)	((_ti)->flags |= ((!!(_if))<<2))

#ifndef __ASSEMBLY__
struct trap_info {
    uint8_t       vector;  /* exception vector                              */
    uint8_t       flags;   /* 0-3: privilege level; 4: clear event enable?  */
    uint16_t      cs;      /* code selector                                 */
    unsigned long address; /* code offset                                   */
};
DEFINE_GUEST_HANDLE_STRUCT(trap_info);

struct arch_shared_info {
	/*
	 * Number of valid entries in the p2m table(s) anchored at
	 * pfn_to_mfn_frame_list_list and/or p2m_vaddr.
	 */
	unsigned long max_pfn;
	/*
	 * Frame containing list of mfns containing list of mfns containing p2m.
	 * A value of 0 indicates it has not yet been set up, ~0 indicates it
	 * has been set to invalid e.g. due to the p2m being too large for the
	 * 3-level p2m tree. In this case the linear mapper p2m list anchored
	 * at p2m_vaddr is to be used.
	 */
	xen_pfn_t pfn_to_mfn_frame_list_list;
	unsigned long nmi_reason;
	/*
	 * Following three fields are valid if p2m_cr3 contains a value
	 * different from 0.
	 * p2m_cr3 is the root of the address space where p2m_vaddr is valid.
	 * p2m_cr3 is in the same format as a cr3 value in the vcpu register
	 * state and holds the folded machine frame number (via xen_pfn_to_cr3)
	 * of a L3 or L4 page table.
	 * p2m_vaddr holds the virtual address of the linear p2m list. All
	 * entries in the range [0...max_pfn[ are accessible via this pointer.
	 * p2m_generation will be incremented by the guest before and after each
	 * change of the mappings of the p2m list. p2m_generation starts at 0
	 * and a value with the least significant bit set indicates that a
	 * mapping update is in progress. This allows guest external software
	 * (e.g. in Dom0) to verify that read mappings are consistent and
	 * whether they have changed since the last check.
	 * Modifying a p2m element in the linear p2m list is allowed via an
	 * atomic write only.
	 */
	unsigned long p2m_cr3;		/* cr3 value of the p2m address space */
	unsigned long p2m_vaddr;	/* virtual address of the p2m list */
	unsigned long p2m_generation;	/* generation count of p2m mapping */
};
#endif	/* !__ASSEMBLY__ */

#ifdef CONFIG_X86_32
#include <asm/xen/interface_32.h>
#else
#include <asm/xen/interface_64.h>
#endif

#include <asm/pvclock-abi.h>

#ifndef __ASSEMBLY__
/*
 * The following is all CPU context. Note that the fpu_ctxt block is filled
 * in by FXSAVE if the CPU has feature FXSR; otherwise FSAVE is used.
 *
 * Also note that when calling DOMCTL_setvcpucontext and VCPU_initialise
 * for HVM and PVH guests, not all information in this structure is updated:
 *
 * - For HVM guests, the structures read include: fpu_ctxt (if
 * VGCT_I387_VALID is set), flags, user_regs, debugreg[*]
 *
 * - PVH guests are the same as HVM guests, but additionally use ctrlreg[3] to
 * set cr3. All other fields not used should be set to 0.
 */
struct vcpu_guest_context {
    /* FPU registers come first so they can be aligned for FXSAVE/FXRSTOR. */
    struct { char x[512]; } fpu_ctxt;       /* User-level FPU registers     */
#define VGCF_I387_VALID                (1<<0)
#define VGCF_IN_KERNEL                 (1<<2)
#define _VGCF_i387_valid               0
#define VGCF_i387_valid                (1<<_VGCF_i387_valid)
#define _VGCF_in_kernel                2
#define VGCF_in_kernel                 (1<<_VGCF_in_kernel)
#define _VGCF_failsafe_disables_events 3
#define VGCF_failsafe_disables_events  (1<<_VGCF_failsafe_disables_events)
#define _VGCF_syscall_disables_events  4
#define VGCF_syscall_disables_events   (1<<_VGCF_syscall_disables_events)
#define _VGCF_online                   5
#define VGCF_online                    (1<<_VGCF_online)
    unsigned long flags;                    /* VGCF_* flags                 */
    struct cpu_user_regs user_regs;         /* User-level CPU registers     */
    struct trap_info trap_ctxt[256];        /* Virtual IDT                  */
    unsigned long ldt_base, ldt_ents;       /* LDT (linear address, # ents) */
    unsigned long gdt_frames[16], gdt_ents; /* GDT (machine frames, # ents) */
    unsigned long kernel_ss, kernel_sp;     /* Virtual TSS (only SS1/SP1)   */
    /* NB. User pagetable on x86/64 is placed in ctrlreg[1]. */
    unsigned long ctrlreg[8];               /* CR0-CR7 (control registers)  */
    unsigned long debugreg[8];              /* DB0-DB7 (debug registers)    */
#ifdef __i386__
    unsigned long event_callback_cs;        /* CS:EIP of event callback     */
    unsigned long event_callback_eip;
    unsigned long failsafe_callback_cs;     /* CS:EIP of failsafe callback  */
    unsigned long failsafe_callback_eip;
#else
    unsigned long event_callback_eip;
    unsigned long failsafe_callback_eip;
    unsigned long syscall_callback_eip;
#endif
    unsigned long vm_assist;                /* VMASST_TYPE_* bitmap */
#ifdef __x86_64__
    /* Segment base addresses. */
    uint64_t      fs_base;
    uint64_t      gs_base_kernel;
    uint64_t      gs_base_user;
#endif
};
DEFINE_GUEST_HANDLE_STRUCT(vcpu_guest_context);

/* AMD PMU registers and structures */
struct xen_pmu_amd_ctxt {
	/*
	 * Offsets to counter and control MSRs (relative to xen_pmu_arch.c.amd).
	 * For PV(H) guests these fields are RO.
	 */
	uint32_t counters;
	uint32_t ctrls;

	/* Counter MSRs */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
	uint64_t regs[];
#elif defined(__GNUC__)
	uint64_t regs[0];
#endif
};

/* Intel PMU registers and structures */
struct xen_pmu_cntr_pair {
	uint64_t counter;
	uint64_t control;
};

struct xen_pmu_intel_ctxt {
	/*
	 * Offsets to fixed and architectural counter MSRs (relative to
	 * xen_pmu_arch.c.intel).
	 * For PV(H) guests these fields are RO.
	 */
	uint32_t fixed_counters;
	uint32_t arch_counters;

	/* PMU registers */
	uint64_t global_ctrl;
	uint64_t global_ovf_ctrl;
	uint64_t global_status;
	uint64_t fixed_ctrl;
	uint64_t ds_area;
	uint64_t pebs_enable;
	uint64_t debugctl;

	/* Fixed and architectural counter MSRs */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
	uint64_t regs[];
#elif defined(__GNUC__)
	uint64_t regs[0];
#endif
};

/* Sampled domain's registers */
struct xen_pmu_regs {
	uint64_t ip;
	uint64_t sp;
	uint64_t flags;
	uint16_t cs;
	uint16_t ss;
	uint8_t cpl;
	uint8_t pad[3];
};

/* PMU flags */
#define PMU_CACHED	   (1<<0) /* PMU MSRs are cached in the context */
#define PMU_SAMPLE_USER	   (1<<1) /* Sample is from user or kernel mode */
#define PMU_SAMPLE_REAL	   (1<<2) /* Sample is from realmode */
#define PMU_SAMPLE_PV	   (1<<3) /* Sample from a PV guest */

/*
 * Architecture-specific information describing state of the processor at
 * the time of PMU interrupt.
 * Fields of this structure marked as RW for guest should only be written by
 * the guest when PMU_CACHED bit in pmu_flags is set (which is done by the
 * hypervisor during PMU interrupt). Hypervisor will read updated data in
 * XENPMU_flush hypercall and clear PMU_CACHED bit.
 */
struct xen_pmu_arch {
	union {
		/*
		 * Processor's registers at the time of interrupt.
		 * WO for hypervisor, RO for guests.
		 */
		struct xen_pmu_regs regs;
		/*
		 * Padding for adding new registers to xen_pmu_regs in
		 * the future
		 */
#define XENPMU_REGS_PAD_SZ  64
		uint8_t pad[XENPMU_REGS_PAD_SZ];
	} r;

	/* WO for hypervisor, RO for guest */
	uint64_t pmu_flags;

	/*
	 * APIC LVTPC register.
	 * RW for both hypervisor and guest.
	 * Only APIC_LVT_MASKED bit is loaded by the hypervisor into hardware
	 * during XENPMU_flush or XENPMU_lvtpc_set.
	 */
	union {
		uint32_t lapic_lvtpc;
		uint64_t pad;
	} l;

	/*
	 * Vendor-specific PMU registers.
	 * RW for both hypervisor and guest (see exceptions above).
	 * Guest's updates to this field are verified and then loaded by the
	 * hypervisor into hardware during XENPMU_flush
	 */
	union {
		struct xen_pmu_amd_ctxt amd;
		struct xen_pmu_intel_ctxt intel;

		/*
		 * Padding for contexts (fixed parts only, does not include
		 * MSR banks that are specified by offsets)
		 */
#define XENPMU_CTXT_PAD_SZ  128
		uint8_t pad[XENPMU_CTXT_PAD_SZ];
	} c;
};

#endif	/* !__ASSEMBLY__ */

/*
 * Prefix forces emulation of some non-trapping instructions.
 * Currently only CPUID.
 */
#ifdef __ASSEMBLY__
#define XEN_EMULATE_PREFIX .byte 0x0f,0x0b,0x78,0x65,0x6e ;
#define XEN_CPUID          XEN_EMULATE_PREFIX cpuid
#else
#define XEN_EMULATE_PREFIX ".byte 0x0f,0x0b,0x78,0x65,0x6e ; "
#define XEN_CPUID          XEN_EMULATE_PREFIX "cpuid"
#endif

#endif /* _ASM_X86_XEN_INTERFACE_H */
