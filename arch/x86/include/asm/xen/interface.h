/******************************************************************************
 * arch-x86_32.h
 *
 * Guest OS interface to x86 Xen.
 *
 * Copyright (c) 2004, K A Fraser
 */

#ifndef _ASM_X86_XEN_INTERFACE_H
#define _ASM_X86_XEN_INTERFACE_H

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
/* Guest handles for primitive C types. */
__DEFINE_GUEST_HANDLE(uchar, unsigned char);
__DEFINE_GUEST_HANDLE(uint,  unsigned int);
DEFINE_GUEST_HANDLE(char);
DEFINE_GUEST_HANDLE(int);
DEFINE_GUEST_HANDLE(void);
DEFINE_GUEST_HANDLE(uint64_t);
DEFINE_GUEST_HANDLE(uint32_t);
DEFINE_GUEST_HANDLE(xen_pfn_t);
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
 */
#define FIRST_RESERVED_GDT_PAGE  14
#define FIRST_RESERVED_GDT_BYTE  (FIRST_RESERVED_GDT_PAGE * 4096)
#define FIRST_RESERVED_GDT_ENTRY (FIRST_RESERVED_GDT_BYTE / 8)

/*
 * Send an array of these to HYPERVISOR_set_trap_table()
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
    unsigned long max_pfn;                  /* max pfn that appears in table */
    /* Frame containing list of mfns containing list of mfns containing p2m. */
    unsigned long pfn_to_mfn_frame_list_list;
    unsigned long nmi_reason;
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
 */
struct vcpu_guest_context {
    /* FPU registers come first so they can be aligned for FXSAVE/FXRSTOR. */
    struct { char x[512]; } fpu_ctxt;       /* User-level FPU registers     */
#define VGCF_I387_VALID (1<<0)
#define VGCF_HVM_GUEST  (1<<1)
#define VGCF_IN_KERNEL  (1<<2)
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
