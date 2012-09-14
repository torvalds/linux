/******************************************************************************
 * arch-ia64/hypervisor-if.h
 *
 * Guest OS interface to IA64 Xen.
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
 * Copyright by those who contributed. (in alphabetical order)
 *
 * Anthony Xu <anthony.xu@intel.com>
 * Eddie Dong <eddie.dong@intel.com>
 * Fred Yang <fred.yang@intel.com>
 * Kevin Tian <kevin.tian@intel.com>
 * Alex Williamson <alex.williamson@hp.com>
 * Chris Wright <chrisw@sous-sol.org>
 * Christian Limpach <Christian.Limpach@cl.cam.ac.uk>
 * Dietmar Hahn <dietmar.hahn@fujitsu-siemens.com>
 * Hollis Blanchard <hollisb@us.ibm.com>
 * Isaku Yamahata <yamahata@valinux.co.jp>
 * Jan Beulich <jbeulich@novell.com>
 * John Levon <john.levon@sun.com>
 * Kazuhiro Suzuki <kaz@jp.fujitsu.com>
 * Keir Fraser <keir.fraser@citrix.com>
 * Kouya Shimura <kouya@jp.fujitsu.com>
 * Masaki Kanno <kanno.masaki@jp.fujitsu.com>
 * Matt Chapman <matthewc@hp.com>
 * Matthew Chapman <matthewc@hp.com>
 * Samuel Thibault <samuel.thibault@eu.citrix.com>
 * Tomonari Horikoshi <t.horikoshi@jp.fujitsu.com>
 * Tristan Gingold <tgingold@free.fr>
 * Tsunehisa Doi <Doi.Tsunehisa@jp.fujitsu.com>
 * Yutaka Ezaki <yutaka.ezaki@jp.fujitsu.com>
 * Zhang Xin <xing.z.zhang@intel.com>
 * Zhang xiantao <xiantao.zhang@intel.com>
 * dan.magenheimer@hp.com
 * ian.pratt@cl.cam.ac.uk
 * michael.fetterman@cl.cam.ac.uk
 */

#ifndef _ASM_IA64_XEN_INTERFACE_H
#define _ASM_IA64_XEN_INTERFACE_H

#define __DEFINE_GUEST_HANDLE(name, type)	\
	typedef struct { type *p; } __guest_handle_ ## name

#define DEFINE_GUEST_HANDLE_STRUCT(name)	\
	__DEFINE_GUEST_HANDLE(name, struct name)
#define DEFINE_GUEST_HANDLE(name)	__DEFINE_GUEST_HANDLE(name, name)
#define GUEST_HANDLE(name)		__guest_handle_ ## name
#define GUEST_HANDLE_64(name)		GUEST_HANDLE(name)
#define set_xen_guest_handle(hnd, val)	do { (hnd).p = val; } while (0)

#ifndef __ASSEMBLY__
/* Explicitly size integers that represent pfns in the public interface
 * with Xen so that we could have one ABI that works for 32 and 64 bit
 * guests. */
typedef unsigned long xen_pfn_t;
typedef unsigned long xen_ulong_t;
/* Guest handles for primitive C types. */
__DEFINE_GUEST_HANDLE(uchar, unsigned char);
__DEFINE_GUEST_HANDLE(uint, unsigned int);
__DEFINE_GUEST_HANDLE(ulong, unsigned long);

DEFINE_GUEST_HANDLE(char);
DEFINE_GUEST_HANDLE(int);
DEFINE_GUEST_HANDLE(long);
DEFINE_GUEST_HANDLE(void);
DEFINE_GUEST_HANDLE(uint64_t);
DEFINE_GUEST_HANDLE(uint32_t);

DEFINE_GUEST_HANDLE(xen_pfn_t);
#define PRI_xen_pfn	"lx"
#endif

/* Arch specific VIRQs definition */
#define VIRQ_ITC	VIRQ_ARCH_0	/* V. Virtual itc timer */
#define VIRQ_MCA_CMC	VIRQ_ARCH_1	/* MCA cmc interrupt */
#define VIRQ_MCA_CPE	VIRQ_ARCH_2	/* MCA cpe interrupt */

/* Maximum number of virtual CPUs in multi-processor guests. */
/* keep sizeof(struct shared_page) <= PAGE_SIZE.
 * this is checked in arch/ia64/xen/hypervisor.c. */
#define MAX_VIRT_CPUS	64

#ifndef __ASSEMBLY__

#define INVALID_MFN	(~0UL)

union vac {
	unsigned long value;
	struct {
		int a_int:1;
		int a_from_int_cr:1;
		int a_to_int_cr:1;
		int a_from_psr:1;
		int a_from_cpuid:1;
		int a_cover:1;
		int a_bsw:1;
		long reserved:57;
	};
};

union vdc {
	unsigned long value;
	struct {
		int d_vmsw:1;
		int d_extint:1;
		int d_ibr_dbr:1;
		int d_pmc:1;
		int d_to_pmd:1;
		int d_itm:1;
		long reserved:58;
	};
};

struct mapped_regs {
	union vac vac;
	union vdc vdc;
	unsigned long virt_env_vaddr;
	unsigned long reserved1[29];
	unsigned long vhpi;
	unsigned long reserved2[95];
	union {
		unsigned long vgr[16];
		unsigned long bank1_regs[16];	/* bank1 regs (r16-r31)
						   when bank0 active */
	};
	union {
		unsigned long vbgr[16];
		unsigned long bank0_regs[16];	/* bank0 regs (r16-r31)
						   when bank1 active */
	};
	unsigned long vnat;
	unsigned long vbnat;
	unsigned long vcpuid[5];
	unsigned long reserved3[11];
	unsigned long vpsr;
	unsigned long vpr;
	unsigned long reserved4[76];
	union {
		unsigned long vcr[128];
		struct {
			unsigned long dcr;	/* CR0 */
			unsigned long itm;
			unsigned long iva;
			unsigned long rsv1[5];
			unsigned long pta;	/* CR8 */
			unsigned long rsv2[7];
			unsigned long ipsr;	/* CR16 */
			unsigned long isr;
			unsigned long rsv3;
			unsigned long iip;
			unsigned long ifa;
			unsigned long itir;
			unsigned long iipa;
			unsigned long ifs;
			unsigned long iim;	/* CR24 */
			unsigned long iha;
			unsigned long rsv4[38];
			unsigned long lid;	/* CR64 */
			unsigned long ivr;
			unsigned long tpr;
			unsigned long eoi;
			unsigned long irr[4];
			unsigned long itv;	/* CR72 */
			unsigned long pmv;
			unsigned long cmcv;
			unsigned long rsv5[5];
			unsigned long lrr0;	/* CR80 */
			unsigned long lrr1;
			unsigned long rsv6[46];
		};
	};
	union {
		unsigned long reserved5[128];
		struct {
			unsigned long precover_ifs;
			unsigned long unat;	/* not sure if this is needed
						   until NaT arch is done */
			int interrupt_collection_enabled; /* virtual psr.ic */

			/* virtual interrupt deliverable flag is
			 * evtchn_upcall_mask in shared info area now.
			 * interrupt_mask_addr is the address
			 * of evtchn_upcall_mask for current vcpu
			 */
			unsigned char *interrupt_mask_addr;
			int pending_interruption;
			unsigned char vpsr_pp;
			unsigned char vpsr_dfh;
			unsigned char hpsr_dfh;
			unsigned char hpsr_mfh;
			unsigned long reserved5_1[4];
			int metaphysical_mode;	/* 1 = use metaphys mapping
						   0 = use virtual */
			int banknum;		/* 0 or 1, which virtual
						   register bank is active */
			unsigned long rrs[8];	/* region registers */
			unsigned long krs[8];	/* kernel registers */
			unsigned long tmp[16];	/* temp registers
						   (e.g. for hyperprivops) */

			/* itc paravirtualization
			 * vAR.ITC = mAR.ITC + itc_offset
			 * itc_last is one which was lastly passed to
			 * the guest OS in order to prevent it from
			 * going backwords.
			 */
			unsigned long itc_offset;
			unsigned long itc_last;
		};
	};
};

struct arch_vcpu_info {
	/* nothing */
};

/*
 * This structure is used for magic page in domain pseudo physical address
 * space and the result of XENMEM_machine_memory_map.
 * As the XENMEM_machine_memory_map result,
 * xen_memory_map::nr_entries indicates the size in bytes
 * including struct xen_ia64_memmap_info. Not the number of entries.
 */
struct xen_ia64_memmap_info {
	uint64_t efi_memmap_size;	/* size of EFI memory map */
	uint64_t efi_memdesc_size;	/* size of an EFI memory map
					 * descriptor */
	uint32_t efi_memdesc_version;	/* memory descriptor version */
	void *memdesc[0];		/* array of efi_memory_desc_t */
};

struct arch_shared_info {
	/* PFN of the start_info page.	*/
	unsigned long start_info_pfn;

	/* Interrupt vector for event channel.	*/
	int evtchn_vector;

	/* PFN of memmap_info page */
	unsigned int memmap_info_num_pages;	/* currently only = 1 case is
						   supported. */
	unsigned long memmap_info_pfn;

	uint64_t pad[31];
};

struct xen_callback {
	unsigned long ip;
};
typedef struct xen_callback xen_callback_t;

#endif /* !__ASSEMBLY__ */

#include <asm/pvclock-abi.h>

/* Size of the shared_info area (this is not related to page size).  */
#define XSI_SHIFT			14
#define XSI_SIZE			(1 << XSI_SHIFT)
/* Log size of mapped_regs area (64 KB - only 4KB is used).  */
#define XMAPPEDREGS_SHIFT		12
#define XMAPPEDREGS_SIZE		(1 << XMAPPEDREGS_SHIFT)
/* Offset of XASI (Xen arch shared info) wrt XSI_BASE.	*/
#define XMAPPEDREGS_OFS			XSI_SIZE

/* Hyperprivops.  */
#define HYPERPRIVOP_START		0x1
#define HYPERPRIVOP_RFI			(HYPERPRIVOP_START + 0x0)
#define HYPERPRIVOP_RSM_DT		(HYPERPRIVOP_START + 0x1)
#define HYPERPRIVOP_SSM_DT		(HYPERPRIVOP_START + 0x2)
#define HYPERPRIVOP_COVER		(HYPERPRIVOP_START + 0x3)
#define HYPERPRIVOP_ITC_D		(HYPERPRIVOP_START + 0x4)
#define HYPERPRIVOP_ITC_I		(HYPERPRIVOP_START + 0x5)
#define HYPERPRIVOP_SSM_I		(HYPERPRIVOP_START + 0x6)
#define HYPERPRIVOP_GET_IVR		(HYPERPRIVOP_START + 0x7)
#define HYPERPRIVOP_GET_TPR		(HYPERPRIVOP_START + 0x8)
#define HYPERPRIVOP_SET_TPR		(HYPERPRIVOP_START + 0x9)
#define HYPERPRIVOP_EOI			(HYPERPRIVOP_START + 0xa)
#define HYPERPRIVOP_SET_ITM		(HYPERPRIVOP_START + 0xb)
#define HYPERPRIVOP_THASH		(HYPERPRIVOP_START + 0xc)
#define HYPERPRIVOP_PTC_GA		(HYPERPRIVOP_START + 0xd)
#define HYPERPRIVOP_ITR_D		(HYPERPRIVOP_START + 0xe)
#define HYPERPRIVOP_GET_RR		(HYPERPRIVOP_START + 0xf)
#define HYPERPRIVOP_SET_RR		(HYPERPRIVOP_START + 0x10)
#define HYPERPRIVOP_SET_KR		(HYPERPRIVOP_START + 0x11)
#define HYPERPRIVOP_FC			(HYPERPRIVOP_START + 0x12)
#define HYPERPRIVOP_GET_CPUID		(HYPERPRIVOP_START + 0x13)
#define HYPERPRIVOP_GET_PMD		(HYPERPRIVOP_START + 0x14)
#define HYPERPRIVOP_GET_EFLAG		(HYPERPRIVOP_START + 0x15)
#define HYPERPRIVOP_SET_EFLAG		(HYPERPRIVOP_START + 0x16)
#define HYPERPRIVOP_RSM_BE		(HYPERPRIVOP_START + 0x17)
#define HYPERPRIVOP_GET_PSR		(HYPERPRIVOP_START + 0x18)
#define HYPERPRIVOP_SET_RR0_TO_RR4	(HYPERPRIVOP_START + 0x19)
#define HYPERPRIVOP_MAX			(0x1a)

/* Fast and light hypercalls.  */
#define __HYPERVISOR_ia64_fast_eoi	__HYPERVISOR_arch_1

/* Xencomm macros.  */
#define XENCOMM_INLINE_MASK		0xf800000000000000UL
#define XENCOMM_INLINE_FLAG		0x8000000000000000UL

#ifndef __ASSEMBLY__

/*
 * Optimization features.
 * The hypervisor may do some special optimizations for guests. This hypercall
 * can be used to switch on/of these special optimizations.
 */
#define __HYPERVISOR_opt_feature	0x700UL

#define XEN_IA64_OPTF_OFF		0x0
#define XEN_IA64_OPTF_ON		0x1

/*
 * If this feature is switched on, the hypervisor inserts the
 * tlb entries without calling the guests traphandler.
 * This is useful in guests using region 7 for identity mapping
 * like the linux kernel does.
 */
#define XEN_IA64_OPTF_IDENT_MAP_REG7	1

/* Identity mapping of region 4 addresses in HVM. */
#define XEN_IA64_OPTF_IDENT_MAP_REG4	2

/* Identity mapping of region 5 addresses in HVM. */
#define XEN_IA64_OPTF_IDENT_MAP_REG5	3

#define XEN_IA64_OPTF_IDENT_MAP_NOT_SET	 (0)

struct xen_ia64_opt_feature {
	unsigned long cmd;	/* Which feature */
	unsigned char on;	/* Switch feature on/off */
	union {
		struct {
			/* The page protection bit mask of the pte.
			 * This will be or'ed with the pte. */
			unsigned long pgprot;
			unsigned long key;	/* A protection key for itir.*/
		};
	};
};

#endif /* __ASSEMBLY__ */

#endif /* _ASM_IA64_XEN_INTERFACE_H */
