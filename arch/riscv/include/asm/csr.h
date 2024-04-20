/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 */

#ifndef _ASM_RISCV_CSR_H
#define _ASM_RISCV_CSR_H

#include <asm/asm.h>
#include <linux/bits.h>

/* Status register flags */
#define SR_SIE		_AC(0x00000002, UL) /* Supervisor Interrupt Enable */
#define SR_MIE		_AC(0x00000008, UL) /* Machine Interrupt Enable */
#define SR_SPIE		_AC(0x00000020, UL) /* Previous Supervisor IE */
#define SR_MPIE		_AC(0x00000080, UL) /* Previous Machine IE */
#define SR_SPP		_AC(0x00000100, UL) /* Previously Supervisor */
#define SR_MPP		_AC(0x00001800, UL) /* Previously Machine */
#define SR_SUM		_AC(0x00040000, UL) /* Supervisor User Memory Access */

#define SR_FS		_AC(0x00006000, UL) /* Floating-point Status */
#define SR_FS_OFF	_AC(0x00000000, UL)
#define SR_FS_INITIAL	_AC(0x00002000, UL)
#define SR_FS_CLEAN	_AC(0x00004000, UL)
#define SR_FS_DIRTY	_AC(0x00006000, UL)

#define SR_VS		_AC(0x00000600, UL) /* Vector Status */
#define SR_VS_OFF	_AC(0x00000000, UL)
#define SR_VS_INITIAL	_AC(0x00000200, UL)
#define SR_VS_CLEAN	_AC(0x00000400, UL)
#define SR_VS_DIRTY	_AC(0x00000600, UL)

#define SR_XS		_AC(0x00018000, UL) /* Extension Status */
#define SR_XS_OFF	_AC(0x00000000, UL)
#define SR_XS_INITIAL	_AC(0x00008000, UL)
#define SR_XS_CLEAN	_AC(0x00010000, UL)
#define SR_XS_DIRTY	_AC(0x00018000, UL)

#define SR_FS_VS	(SR_FS | SR_VS) /* Vector and Floating-Point Unit */

#ifndef CONFIG_64BIT
#define SR_SD		_AC(0x80000000, UL) /* FS/VS/XS dirty */
#else
#define SR_SD		_AC(0x8000000000000000, UL) /* FS/VS/XS dirty */
#endif

#ifdef CONFIG_64BIT
#define SR_UXL		_AC(0x300000000, UL) /* XLEN mask for U-mode */
#define SR_UXL_32	_AC(0x100000000, UL) /* XLEN = 32 for U-mode */
#define SR_UXL_64	_AC(0x200000000, UL) /* XLEN = 64 for U-mode */
#endif

/* SATP flags */
#ifndef CONFIG_64BIT
#define SATP_PPN	_AC(0x003FFFFF, UL)
#define SATP_MODE_32	_AC(0x80000000, UL)
#define SATP_MODE_SHIFT	31
#define SATP_ASID_BITS	9
#define SATP_ASID_SHIFT	22
#define SATP_ASID_MASK	_AC(0x1FF, UL)
#else
#define SATP_PPN	_AC(0x00000FFFFFFFFFFF, UL)
#define SATP_MODE_39	_AC(0x8000000000000000, UL)
#define SATP_MODE_48	_AC(0x9000000000000000, UL)
#define SATP_MODE_57	_AC(0xa000000000000000, UL)
#define SATP_MODE_SHIFT	60
#define SATP_ASID_BITS	16
#define SATP_ASID_SHIFT	44
#define SATP_ASID_MASK	_AC(0xFFFF, UL)
#endif

/* Exception cause high bit - is an interrupt if set */
#define CAUSE_IRQ_FLAG		(_AC(1, UL) << (__riscv_xlen - 1))

/* Interrupt causes (minus the high bit) */
#define IRQ_S_SOFT		1
#define IRQ_VS_SOFT		2
#define IRQ_M_SOFT		3
#define IRQ_S_TIMER		5
#define IRQ_VS_TIMER		6
#define IRQ_M_TIMER		7
#define IRQ_S_EXT		9
#define IRQ_VS_EXT		10
#define IRQ_M_EXT		11
#define IRQ_S_GEXT		12
#define IRQ_PMU_OVF		13
#define IRQ_LOCAL_MAX		(IRQ_PMU_OVF + 1)
#define IRQ_LOCAL_MASK		GENMASK((IRQ_LOCAL_MAX - 1), 0)

/* Exception causes */
#define EXC_INST_MISALIGNED	0
#define EXC_INST_ACCESS		1
#define EXC_INST_ILLEGAL	2
#define EXC_BREAKPOINT		3
#define EXC_LOAD_MISALIGNED	4
#define EXC_LOAD_ACCESS		5
#define EXC_STORE_MISALIGNED	6
#define EXC_STORE_ACCESS	7
#define EXC_SYSCALL		8
#define EXC_HYPERVISOR_SYSCALL	9
#define EXC_SUPERVISOR_SYSCALL	10
#define EXC_INST_PAGE_FAULT	12
#define EXC_LOAD_PAGE_FAULT	13
#define EXC_STORE_PAGE_FAULT	15
#define EXC_INST_GUEST_PAGE_FAULT	20
#define EXC_LOAD_GUEST_PAGE_FAULT	21
#define EXC_VIRTUAL_INST_FAULT		22
#define EXC_STORE_GUEST_PAGE_FAULT	23

/* PMP configuration */
#define PMP_R			0x01
#define PMP_W			0x02
#define PMP_X			0x04
#define PMP_A			0x18
#define PMP_A_TOR		0x08
#define PMP_A_NA4		0x10
#define PMP_A_NAPOT		0x18
#define PMP_L			0x80

/* HSTATUS flags */
#ifdef CONFIG_64BIT
#define HSTATUS_VSXL		_AC(0x300000000, UL)
#define HSTATUS_VSXL_SHIFT	32
#endif
#define HSTATUS_VTSR		_AC(0x00400000, UL)
#define HSTATUS_VTW		_AC(0x00200000, UL)
#define HSTATUS_VTVM		_AC(0x00100000, UL)
#define HSTATUS_VGEIN		_AC(0x0003f000, UL)
#define HSTATUS_VGEIN_SHIFT	12
#define HSTATUS_HU		_AC(0x00000200, UL)
#define HSTATUS_SPVP		_AC(0x00000100, UL)
#define HSTATUS_SPV		_AC(0x00000080, UL)
#define HSTATUS_GVA		_AC(0x00000040, UL)
#define HSTATUS_VSBE		_AC(0x00000020, UL)

/* HGATP flags */
#define HGATP_MODE_OFF		_AC(0, UL)
#define HGATP_MODE_SV32X4	_AC(1, UL)
#define HGATP_MODE_SV39X4	_AC(8, UL)
#define HGATP_MODE_SV48X4	_AC(9, UL)
#define HGATP_MODE_SV57X4	_AC(10, UL)

#define HGATP32_MODE_SHIFT	31
#define HGATP32_VMID_SHIFT	22
#define HGATP32_VMID		GENMASK(28, 22)
#define HGATP32_PPN		GENMASK(21, 0)

#define HGATP64_MODE_SHIFT	60
#define HGATP64_VMID_SHIFT	44
#define HGATP64_VMID		GENMASK(57, 44)
#define HGATP64_PPN		GENMASK(43, 0)

#define HGATP_PAGE_SHIFT	12

#ifdef CONFIG_64BIT
#define HGATP_PPN		HGATP64_PPN
#define HGATP_VMID_SHIFT	HGATP64_VMID_SHIFT
#define HGATP_VMID		HGATP64_VMID
#define HGATP_MODE_SHIFT	HGATP64_MODE_SHIFT
#else
#define HGATP_PPN		HGATP32_PPN
#define HGATP_VMID_SHIFT	HGATP32_VMID_SHIFT
#define HGATP_VMID		HGATP32_VMID
#define HGATP_MODE_SHIFT	HGATP32_MODE_SHIFT
#endif

/* VSIP & HVIP relation */
#define VSIP_TO_HVIP_SHIFT	(IRQ_VS_SOFT - IRQ_S_SOFT)
#define VSIP_VALID_MASK		((_AC(1, UL) << IRQ_S_SOFT) | \
				 (_AC(1, UL) << IRQ_S_TIMER) | \
				 (_AC(1, UL) << IRQ_S_EXT))

/* AIA CSR bits */
#define TOPI_IID_SHIFT		16
#define TOPI_IID_MASK		GENMASK(11, 0)
#define TOPI_IPRIO_MASK		GENMASK(7, 0)
#define TOPI_IPRIO_BITS		8

#define TOPEI_ID_SHIFT		16
#define TOPEI_ID_MASK		GENMASK(10, 0)
#define TOPEI_PRIO_MASK		GENMASK(10, 0)

#define ISELECT_IPRIO0		0x30
#define ISELECT_IPRIO15		0x3f
#define ISELECT_MASK		GENMASK(8, 0)

#define HVICTL_VTI		BIT(30)
#define HVICTL_IID		GENMASK(27, 16)
#define HVICTL_IID_SHIFT	16
#define HVICTL_DPR		BIT(9)
#define HVICTL_IPRIOM		BIT(8)
#define HVICTL_IPRIO		GENMASK(7, 0)

/* xENVCFG flags */
#define ENVCFG_STCE			(_AC(1, ULL) << 63)
#define ENVCFG_PBMTE			(_AC(1, ULL) << 62)
#define ENVCFG_CBZE			(_AC(1, UL) << 7)
#define ENVCFG_CBCFE			(_AC(1, UL) << 6)
#define ENVCFG_CBIE_SHIFT		4
#define ENVCFG_CBIE			(_AC(0x3, UL) << ENVCFG_CBIE_SHIFT)
#define ENVCFG_CBIE_ILL			_AC(0x0, UL)
#define ENVCFG_CBIE_FLUSH		_AC(0x1, UL)
#define ENVCFG_CBIE_INV			_AC(0x3, UL)
#define ENVCFG_FIOM			_AC(0x1, UL)

/* Smstateen bits */
#define SMSTATEEN0_AIA_IMSIC_SHIFT	58
#define SMSTATEEN0_AIA_IMSIC		(_ULL(1) << SMSTATEEN0_AIA_IMSIC_SHIFT)
#define SMSTATEEN0_AIA_SHIFT		59
#define SMSTATEEN0_AIA			(_ULL(1) << SMSTATEEN0_AIA_SHIFT)
#define SMSTATEEN0_AIA_ISEL_SHIFT	60
#define SMSTATEEN0_AIA_ISEL		(_ULL(1) << SMSTATEEN0_AIA_ISEL_SHIFT)
#define SMSTATEEN0_HSENVCFG_SHIFT	62
#define SMSTATEEN0_HSENVCFG		(_ULL(1) << SMSTATEEN0_HSENVCFG_SHIFT)
#define SMSTATEEN0_SSTATEEN0_SHIFT	63
#define SMSTATEEN0_SSTATEEN0		(_ULL(1) << SMSTATEEN0_SSTATEEN0_SHIFT)

/* symbolic CSR names: */
#define CSR_CYCLE		0xc00
#define CSR_TIME		0xc01
#define CSR_INSTRET		0xc02
#define CSR_HPMCOUNTER3		0xc03
#define CSR_HPMCOUNTER4		0xc04
#define CSR_HPMCOUNTER5		0xc05
#define CSR_HPMCOUNTER6		0xc06
#define CSR_HPMCOUNTER7		0xc07
#define CSR_HPMCOUNTER8		0xc08
#define CSR_HPMCOUNTER9		0xc09
#define CSR_HPMCOUNTER10	0xc0a
#define CSR_HPMCOUNTER11	0xc0b
#define CSR_HPMCOUNTER12	0xc0c
#define CSR_HPMCOUNTER13	0xc0d
#define CSR_HPMCOUNTER14	0xc0e
#define CSR_HPMCOUNTER15	0xc0f
#define CSR_HPMCOUNTER16	0xc10
#define CSR_HPMCOUNTER17	0xc11
#define CSR_HPMCOUNTER18	0xc12
#define CSR_HPMCOUNTER19	0xc13
#define CSR_HPMCOUNTER20	0xc14
#define CSR_HPMCOUNTER21	0xc15
#define CSR_HPMCOUNTER22	0xc16
#define CSR_HPMCOUNTER23	0xc17
#define CSR_HPMCOUNTER24	0xc18
#define CSR_HPMCOUNTER25	0xc19
#define CSR_HPMCOUNTER26	0xc1a
#define CSR_HPMCOUNTER27	0xc1b
#define CSR_HPMCOUNTER28	0xc1c
#define CSR_HPMCOUNTER29	0xc1d
#define CSR_HPMCOUNTER30	0xc1e
#define CSR_HPMCOUNTER31	0xc1f
#define CSR_CYCLEH		0xc80
#define CSR_TIMEH		0xc81
#define CSR_INSTRETH		0xc82
#define CSR_HPMCOUNTER3H	0xc83
#define CSR_HPMCOUNTER4H	0xc84
#define CSR_HPMCOUNTER5H	0xc85
#define CSR_HPMCOUNTER6H	0xc86
#define CSR_HPMCOUNTER7H	0xc87
#define CSR_HPMCOUNTER8H	0xc88
#define CSR_HPMCOUNTER9H	0xc89
#define CSR_HPMCOUNTER10H	0xc8a
#define CSR_HPMCOUNTER11H	0xc8b
#define CSR_HPMCOUNTER12H	0xc8c
#define CSR_HPMCOUNTER13H	0xc8d
#define CSR_HPMCOUNTER14H	0xc8e
#define CSR_HPMCOUNTER15H	0xc8f
#define CSR_HPMCOUNTER16H	0xc90
#define CSR_HPMCOUNTER17H	0xc91
#define CSR_HPMCOUNTER18H	0xc92
#define CSR_HPMCOUNTER19H	0xc93
#define CSR_HPMCOUNTER20H	0xc94
#define CSR_HPMCOUNTER21H	0xc95
#define CSR_HPMCOUNTER22H	0xc96
#define CSR_HPMCOUNTER23H	0xc97
#define CSR_HPMCOUNTER24H	0xc98
#define CSR_HPMCOUNTER25H	0xc99
#define CSR_HPMCOUNTER26H	0xc9a
#define CSR_HPMCOUNTER27H	0xc9b
#define CSR_HPMCOUNTER28H	0xc9c
#define CSR_HPMCOUNTER29H	0xc9d
#define CSR_HPMCOUNTER30H	0xc9e
#define CSR_HPMCOUNTER31H	0xc9f

#define CSR_SCOUNTOVF		0xda0

#define CSR_SSTATUS		0x100
#define CSR_SIE			0x104
#define CSR_STVEC		0x105
#define CSR_SCOUNTEREN		0x106
#define CSR_SENVCFG		0x10a
#define CSR_SSTATEEN0		0x10c
#define CSR_SSCRATCH		0x140
#define CSR_SEPC		0x141
#define CSR_SCAUSE		0x142
#define CSR_STVAL		0x143
#define CSR_SIP			0x144
#define CSR_SATP		0x180

#define CSR_STIMECMP		0x14D
#define CSR_STIMECMPH		0x15D

/* Supervisor-Level Window to Indirectly Accessed Registers (AIA) */
#define CSR_SISELECT		0x150
#define CSR_SIREG		0x151

/* Supervisor-Level Interrupts (AIA) */
#define CSR_STOPEI		0x15c
#define CSR_STOPI		0xdb0

/* Supervisor-Level High-Half CSRs (AIA) */
#define CSR_SIEH		0x114
#define CSR_SIPH		0x154

#define CSR_VSSTATUS		0x200
#define CSR_VSIE		0x204
#define CSR_VSTVEC		0x205
#define CSR_VSSCRATCH		0x240
#define CSR_VSEPC		0x241
#define CSR_VSCAUSE		0x242
#define CSR_VSTVAL		0x243
#define CSR_VSIP		0x244
#define CSR_VSATP		0x280
#define CSR_VSTIMECMP		0x24D
#define CSR_VSTIMECMPH		0x25D

#define CSR_HSTATUS		0x600
#define CSR_HEDELEG		0x602
#define CSR_HIDELEG		0x603
#define CSR_HIE			0x604
#define CSR_HTIMEDELTA		0x605
#define CSR_HCOUNTEREN		0x606
#define CSR_HGEIE		0x607
#define CSR_HENVCFG		0x60a
#define CSR_HTIMEDELTAH		0x615
#define CSR_HENVCFGH		0x61a
#define CSR_HTVAL		0x643
#define CSR_HIP			0x644
#define CSR_HVIP		0x645
#define CSR_HTINST		0x64a
#define CSR_HGATP		0x680
#define CSR_HGEIP		0xe12

/* Virtual Interrupts and Interrupt Priorities (H-extension with AIA) */
#define CSR_HVIEN		0x608
#define CSR_HVICTL		0x609
#define CSR_HVIPRIO1		0x646
#define CSR_HVIPRIO2		0x647

/* VS-Level Window to Indirectly Accessed Registers (H-extension with AIA) */
#define CSR_VSISELECT		0x250
#define CSR_VSIREG		0x251

/* VS-Level Interrupts (H-extension with AIA) */
#define CSR_VSTOPEI		0x25c
#define CSR_VSTOPI		0xeb0

/* Hypervisor and VS-Level High-Half CSRs (H-extension with AIA) */
#define CSR_HIDELEGH		0x613
#define CSR_HVIENH		0x618
#define CSR_HVIPH		0x655
#define CSR_HVIPRIO1H		0x656
#define CSR_HVIPRIO2H		0x657
#define CSR_VSIEH		0x214
#define CSR_VSIPH		0x254

/* Hypervisor stateen CSRs */
#define CSR_HSTATEEN0		0x60c
#define CSR_HSTATEEN0H		0x61c

#define CSR_MSTATUS		0x300
#define CSR_MISA		0x301
#define CSR_MIDELEG		0x303
#define CSR_MIE			0x304
#define CSR_MTVEC		0x305
#define CSR_MENVCFG		0x30a
#define CSR_MENVCFGH		0x31a
#define CSR_MSCRATCH		0x340
#define CSR_MEPC		0x341
#define CSR_MCAUSE		0x342
#define CSR_MTVAL		0x343
#define CSR_MIP			0x344
#define CSR_PMPCFG0		0x3a0
#define CSR_PMPADDR0		0x3b0
#define CSR_MVENDORID		0xf11
#define CSR_MARCHID		0xf12
#define CSR_MIMPID		0xf13
#define CSR_MHARTID		0xf14

/* Machine-Level Window to Indirectly Accessed Registers (AIA) */
#define CSR_MISELECT		0x350
#define CSR_MIREG		0x351

/* Machine-Level Interrupts (AIA) */
#define CSR_MTOPEI		0x35c
#define CSR_MTOPI		0xfb0

/* Virtual Interrupts for Supervisor Level (AIA) */
#define CSR_MVIEN		0x308
#define CSR_MVIP		0x309

/* Machine-Level High-Half CSRs (AIA) */
#define CSR_MIDELEGH		0x313
#define CSR_MIEH		0x314
#define CSR_MVIENH		0x318
#define CSR_MVIPH		0x319
#define CSR_MIPH		0x354

#define CSR_VSTART		0x8
#define CSR_VCSR		0xf
#define CSR_VL			0xc20
#define CSR_VTYPE		0xc21
#define CSR_VLENB		0xc22

/* Scalar Crypto Extension - Entropy */
#define CSR_SEED		0x015
#define SEED_OPST_MASK		_AC(0xC0000000, UL)
#define SEED_OPST_BIST		_AC(0x00000000, UL)
#define SEED_OPST_WAIT		_AC(0x40000000, UL)
#define SEED_OPST_ES16		_AC(0x80000000, UL)
#define SEED_OPST_DEAD		_AC(0xC0000000, UL)
#define SEED_ENTROPY_MASK	_AC(0xFFFF, UL)

#ifdef CONFIG_RISCV_M_MODE
# define CSR_STATUS	CSR_MSTATUS
# define CSR_IE		CSR_MIE
# define CSR_TVEC	CSR_MTVEC
# define CSR_ENVCFG	CSR_MENVCFG
# define CSR_SCRATCH	CSR_MSCRATCH
# define CSR_EPC	CSR_MEPC
# define CSR_CAUSE	CSR_MCAUSE
# define CSR_TVAL	CSR_MTVAL
# define CSR_IP		CSR_MIP

# define CSR_IEH		CSR_MIEH
# define CSR_ISELECT	CSR_MISELECT
# define CSR_IREG	CSR_MIREG
# define CSR_IPH		CSR_MIPH
# define CSR_TOPEI	CSR_MTOPEI
# define CSR_TOPI	CSR_MTOPI

# define SR_IE		SR_MIE
# define SR_PIE		SR_MPIE
# define SR_PP		SR_MPP

# define RV_IRQ_SOFT		IRQ_M_SOFT
# define RV_IRQ_TIMER	IRQ_M_TIMER
# define RV_IRQ_EXT		IRQ_M_EXT
#else /* CONFIG_RISCV_M_MODE */
# define CSR_STATUS	CSR_SSTATUS
# define CSR_IE		CSR_SIE
# define CSR_TVEC	CSR_STVEC
# define CSR_ENVCFG	CSR_SENVCFG
# define CSR_SCRATCH	CSR_SSCRATCH
# define CSR_EPC	CSR_SEPC
# define CSR_CAUSE	CSR_SCAUSE
# define CSR_TVAL	CSR_STVAL
# define CSR_IP		CSR_SIP

# define CSR_IEH		CSR_SIEH
# define CSR_ISELECT	CSR_SISELECT
# define CSR_IREG	CSR_SIREG
# define CSR_IPH		CSR_SIPH
# define CSR_TOPEI	CSR_STOPEI
# define CSR_TOPI	CSR_STOPI

# define SR_IE		SR_SIE
# define SR_PIE		SR_SPIE
# define SR_PP		SR_SPP

# define RV_IRQ_SOFT		IRQ_S_SOFT
# define RV_IRQ_TIMER	IRQ_S_TIMER
# define RV_IRQ_EXT		IRQ_S_EXT
# define RV_IRQ_PMU	IRQ_PMU_OVF
# define SIP_LCOFIP     (_AC(0x1, UL) << IRQ_PMU_OVF)

#endif /* !CONFIG_RISCV_M_MODE */

/* IE/IP (Supervisor/Machine Interrupt Enable/Pending) flags */
#define IE_SIE		(_AC(0x1, UL) << RV_IRQ_SOFT)
#define IE_TIE		(_AC(0x1, UL) << RV_IRQ_TIMER)
#define IE_EIE		(_AC(0x1, UL) << RV_IRQ_EXT)

#ifndef __ASSEMBLY__

#define csr_swap(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrrw %0, " __ASM_STR(csr) ", %1"\
			      : "=r" (__v) : "rK" (__v)		\
			      : "memory");			\
	__v;							\
})

#define csr_read(csr)						\
({								\
	register unsigned long __v;				\
	__asm__ __volatile__ ("csrr %0, " __ASM_STR(csr)	\
			      : "=r" (__v) :			\
			      : "memory");			\
	__v;							\
})

#define csr_write(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrw " __ASM_STR(csr) ", %0"	\
			      : : "rK" (__v)			\
			      : "memory");			\
})

#define csr_read_set(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrrs %0, " __ASM_STR(csr) ", %1"\
			      : "=r" (__v) : "rK" (__v)		\
			      : "memory");			\
	__v;							\
})

#define csr_set(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrs " __ASM_STR(csr) ", %0"	\
			      : : "rK" (__v)			\
			      : "memory");			\
})

#define csr_read_clear(csr, val)				\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrrc %0, " __ASM_STR(csr) ", %1"\
			      : "=r" (__v) : "rK" (__v)		\
			      : "memory");			\
	__v;							\
})

#define csr_clear(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrc " __ASM_STR(csr) ", %0"	\
			      : : "rK" (__v)			\
			      : "memory");			\
})

#endif /* __ASSEMBLY__ */

#endif /* _ASM_RISCV_CSR_H */
