#ifndef _ASM_X86_APICDEF_H
#define _ASM_X86_APICDEF_H

/*
 * Constants for various Intel APICs. (local APIC, IOAPIC, etc.)
 *
 * Alan Cox <Alan.Cox@linux.org>, 1995.
 * Ingo Molnar <mingo@redhat.com>, 1999, 2000
 */

#define	APIC_DEFAULT_PHYS_BASE	0xfee00000

#define	APIC_ID		0x20

#define	APIC_LVR	0x30
#define		APIC_LVR_MASK		0xFF00FF
#define		GET_APIC_VERSION(x)	((x) & 0xFFu)
#define		GET_APIC_MAXLVT(x)	(((x) >> 16) & 0xFFu)
#ifdef CONFIG_X86_32
#  define	APIC_INTEGRATED(x)	((x) & 0xF0u)
#else
#  define	APIC_INTEGRATED(x)	(1)
#endif
#define		APIC_XAPIC(x)		((x) >= 0x14)
#define	APIC_TASKPRI	0x80
#define		APIC_TPRI_MASK		0xFFu
#define	APIC_ARBPRI	0x90
#define		APIC_ARBPRI_MASK	0xFFu
#define	APIC_PROCPRI	0xA0
#define	APIC_EOI	0xB0
#define		APIC_EIO_ACK		0x0
#define	APIC_RRR	0xC0
#define	APIC_LDR	0xD0
#define		APIC_LDR_MASK		(0xFFu << 24)
#define		GET_APIC_LOGICAL_ID(x)	(((x) >> 24) & 0xFFu)
#define		SET_APIC_LOGICAL_ID(x)	(((x) << 24))
#define		APIC_ALL_CPUS		0xFFu
#define	APIC_DFR	0xE0
#define		APIC_DFR_CLUSTER		0x0FFFFFFFul
#define		APIC_DFR_FLAT			0xFFFFFFFFul
#define	APIC_SPIV	0xF0
#define		APIC_SPIV_FOCUS_DISABLED	(1 << 9)
#define		APIC_SPIV_APIC_ENABLED		(1 << 8)
#define	APIC_ISR	0x100
#define	APIC_ISR_NR     0x8     /* Number of 32 bit ISR registers. */
#define	APIC_TMR	0x180
#define	APIC_IRR	0x200
#define	APIC_ESR	0x280
#define		APIC_ESR_SEND_CS	0x00001
#define		APIC_ESR_RECV_CS	0x00002
#define		APIC_ESR_SEND_ACC	0x00004
#define		APIC_ESR_RECV_ACC	0x00008
#define		APIC_ESR_SENDILL	0x00020
#define		APIC_ESR_RECVILL	0x00040
#define		APIC_ESR_ILLREGA	0x00080
#define	APIC_ICR	0x300
#define		APIC_DEST_SELF		0x40000
#define		APIC_DEST_ALLINC	0x80000
#define		APIC_DEST_ALLBUT	0xC0000
#define		APIC_ICR_RR_MASK	0x30000
#define		APIC_ICR_RR_INVALID	0x00000
#define		APIC_ICR_RR_INPROG	0x10000
#define		APIC_ICR_RR_VALID	0x20000
#define		APIC_INT_LEVELTRIG	0x08000
#define		APIC_INT_ASSERT		0x04000
#define		APIC_ICR_BUSY		0x01000
#define		APIC_DEST_LOGICAL	0x00800
#define		APIC_DEST_PHYSICAL	0x00000
#define		APIC_DM_FIXED		0x00000
#define		APIC_DM_LOWEST		0x00100
#define		APIC_DM_SMI		0x00200
#define		APIC_DM_REMRD		0x00300
#define		APIC_DM_NMI		0x00400
#define		APIC_DM_INIT		0x00500
#define		APIC_DM_STARTUP		0x00600
#define		APIC_DM_EXTINT		0x00700
#define		APIC_VECTOR_MASK	0x000FF
#define	APIC_ICR2	0x310
#define		GET_APIC_DEST_FIELD(x)	(((x) >> 24) & 0xFF)
#define		SET_APIC_DEST_FIELD(x)	((x) << 24)
#define	APIC_LVTT	0x320
#define	APIC_LVTTHMR	0x330
#define	APIC_LVTPC	0x340
#define	APIC_LVT0	0x350
#define		APIC_LVT_TIMER_BASE_MASK	(0x3 << 18)
#define		GET_APIC_TIMER_BASE(x)		(((x) >> 18) & 0x3)
#define		SET_APIC_TIMER_BASE(x)		(((x) << 18))
#define		APIC_TIMER_BASE_CLKIN		0x0
#define		APIC_TIMER_BASE_TMBASE		0x1
#define		APIC_TIMER_BASE_DIV		0x2
#define		APIC_LVT_TIMER_PERIODIC		(1 << 17)
#define		APIC_LVT_MASKED			(1 << 16)
#define		APIC_LVT_LEVEL_TRIGGER		(1 << 15)
#define		APIC_LVT_REMOTE_IRR		(1 << 14)
#define		APIC_INPUT_POLARITY		(1 << 13)
#define		APIC_SEND_PENDING		(1 << 12)
#define		APIC_MODE_MASK			0x700
#define		GET_APIC_DELIVERY_MODE(x)	(((x) >> 8) & 0x7)
#define		SET_APIC_DELIVERY_MODE(x, y)	(((x) & ~0x700) | ((y) << 8))
#define			APIC_MODE_FIXED		0x0
#define			APIC_MODE_NMI		0x4
#define			APIC_MODE_EXTINT	0x7
#define	APIC_LVT1	0x360
#define	APIC_LVTERR	0x370
#define	APIC_TMICT	0x380
#define	APIC_TMCCT	0x390
#define	APIC_TDCR	0x3E0
#define APIC_SELF_IPI	0x3F0
#define		APIC_TDR_DIV_TMBASE	(1 << 2)
#define		APIC_TDR_DIV_1		0xB
#define		APIC_TDR_DIV_2		0x0
#define		APIC_TDR_DIV_4		0x1
#define		APIC_TDR_DIV_8		0x2
#define		APIC_TDR_DIV_16		0x3
#define		APIC_TDR_DIV_32		0x8
#define		APIC_TDR_DIV_64		0x9
#define		APIC_TDR_DIV_128	0xA
#define	APIC_EILVT0     0x500
#define		APIC_EILVT_NR_AMD_K8	1	/* # of extended interrupts */
#define		APIC_EILVT_NR_AMD_10H	4
#define		APIC_EILVT_LVTOFF(x)	(((x) >> 4) & 0xF)
#define		APIC_EILVT_MSG_FIX	0x0
#define		APIC_EILVT_MSG_SMI	0x2
#define		APIC_EILVT_MSG_NMI	0x4
#define		APIC_EILVT_MSG_EXT	0x7
#define		APIC_EILVT_MASKED	(1 << 16)
#define	APIC_EILVT1     0x510
#define	APIC_EILVT2     0x520
#define	APIC_EILVT3     0x530

#define APIC_BASE (fix_to_virt(FIX_APIC_BASE))
#define APIC_BASE_MSR	0x800
#define X2APIC_ENABLE	(1UL << 10)

#ifdef CONFIG_X86_32
# define MAX_IO_APICS 64
#else
# define MAX_IO_APICS 128
# define MAX_LOCAL_APIC 32768
#endif

/*
 * All x86-64 systems are xAPIC compatible.
 * In the following, "apicid" is a physical APIC ID.
 */
#define XAPIC_DEST_CPUS_SHIFT	4
#define XAPIC_DEST_CPUS_MASK	((1u << XAPIC_DEST_CPUS_SHIFT) - 1)
#define XAPIC_DEST_CLUSTER_MASK	(XAPIC_DEST_CPUS_MASK << XAPIC_DEST_CPUS_SHIFT)
#define APIC_CLUSTER(apicid)	((apicid) & XAPIC_DEST_CLUSTER_MASK)
#define APIC_CLUSTERID(apicid)	(APIC_CLUSTER(apicid) >> XAPIC_DEST_CPUS_SHIFT)
#define APIC_CPUID(apicid)	((apicid) & XAPIC_DEST_CPUS_MASK)
#define NUM_APIC_CLUSTERS	((BAD_APICID + 1) >> XAPIC_DEST_CPUS_SHIFT)

/*
 * the local APIC register structure, memory mapped. Not terribly well
 * tested, but we might eventually use this one in the future - the
 * problem why we cannot use it right now is the P5 APIC, it has an
 * errata which cannot take 8-bit reads and writes, only 32-bit ones ...
 */
#define u32 unsigned int

struct local_apic {

/*000*/	struct { u32 __reserved[4]; } __reserved_01;

/*010*/	struct { u32 __reserved[4]; } __reserved_02;

/*020*/	struct { /* APIC ID Register */
		u32   __reserved_1	: 24,
			phys_apic_id	:  4,
			__reserved_2	:  4;
		u32 __reserved[3];
	} id;

/*030*/	const
	struct { /* APIC Version Register */
		u32   version		:  8,
			__reserved_1	:  8,
			max_lvt		:  8,
			__reserved_2	:  8;
		u32 __reserved[3];
	} version;

/*040*/	struct { u32 __reserved[4]; } __reserved_03;

/*050*/	struct { u32 __reserved[4]; } __reserved_04;

/*060*/	struct { u32 __reserved[4]; } __reserved_05;

/*070*/	struct { u32 __reserved[4]; } __reserved_06;

/*080*/	struct { /* Task Priority Register */
		u32   priority	:  8,
			__reserved_1	: 24;
		u32 __reserved_2[3];
	} tpr;

/*090*/	const
	struct { /* Arbitration Priority Register */
		u32   priority	:  8,
			__reserved_1	: 24;
		u32 __reserved_2[3];
	} apr;

/*0A0*/	const
	struct { /* Processor Priority Register */
		u32   priority	:  8,
			__reserved_1	: 24;
		u32 __reserved_2[3];
	} ppr;

/*0B0*/	struct { /* End Of Interrupt Register */
		u32   eoi;
		u32 __reserved[3];
	} eoi;

/*0C0*/	struct { u32 __reserved[4]; } __reserved_07;

/*0D0*/	struct { /* Logical Destination Register */
		u32   __reserved_1	: 24,
			logical_dest	:  8;
		u32 __reserved_2[3];
	} ldr;

/*0E0*/	struct { /* Destination Format Register */
		u32   __reserved_1	: 28,
			model		:  4;
		u32 __reserved_2[3];
	} dfr;

/*0F0*/	struct { /* Spurious Interrupt Vector Register */
		u32	spurious_vector	:  8,
			apic_enabled	:  1,
			focus_cpu	:  1,
			__reserved_2	: 22;
		u32 __reserved_3[3];
	} svr;

/*100*/	struct { /* In Service Register */
/*170*/		u32 bitfield;
		u32 __reserved[3];
	} isr [8];

/*180*/	struct { /* Trigger Mode Register */
/*1F0*/		u32 bitfield;
		u32 __reserved[3];
	} tmr [8];

/*200*/	struct { /* Interrupt Request Register */
/*270*/		u32 bitfield;
		u32 __reserved[3];
	} irr [8];

/*280*/	union { /* Error Status Register */
		struct {
			u32   send_cs_error			:  1,
				receive_cs_error		:  1,
				send_accept_error		:  1,
				receive_accept_error		:  1,
				__reserved_1			:  1,
				send_illegal_vector		:  1,
				receive_illegal_vector		:  1,
				illegal_register_address	:  1,
				__reserved_2			: 24;
			u32 __reserved_3[3];
		} error_bits;
		struct {
			u32 errors;
			u32 __reserved_3[3];
		} all_errors;
	} esr;

/*290*/	struct { u32 __reserved[4]; } __reserved_08;

/*2A0*/	struct { u32 __reserved[4]; } __reserved_09;

/*2B0*/	struct { u32 __reserved[4]; } __reserved_10;

/*2C0*/	struct { u32 __reserved[4]; } __reserved_11;

/*2D0*/	struct { u32 __reserved[4]; } __reserved_12;

/*2E0*/	struct { u32 __reserved[4]; } __reserved_13;

/*2F0*/	struct { u32 __reserved[4]; } __reserved_14;

/*300*/	struct { /* Interrupt Command Register 1 */
		u32   vector			:  8,
			delivery_mode		:  3,
			destination_mode	:  1,
			delivery_status		:  1,
			__reserved_1		:  1,
			level			:  1,
			trigger			:  1,
			__reserved_2		:  2,
			shorthand		:  2,
			__reserved_3		:  12;
		u32 __reserved_4[3];
	} icr1;

/*310*/	struct { /* Interrupt Command Register 2 */
		union {
			u32   __reserved_1	: 24,
				phys_dest	:  4,
				__reserved_2	:  4;
			u32   __reserved_3	: 24,
				logical_dest	:  8;
		} dest;
		u32 __reserved_4[3];
	} icr2;

/*320*/	struct { /* LVT - Timer */
		u32   vector		:  8,
			__reserved_1	:  4,
			delivery_status	:  1,
			__reserved_2	:  3,
			mask		:  1,
			timer_mode	:  1,
			__reserved_3	: 14;
		u32 __reserved_4[3];
	} lvt_timer;

/*330*/	struct { /* LVT - Thermal Sensor */
		u32  vector		:  8,
			delivery_mode	:  3,
			__reserved_1	:  1,
			delivery_status	:  1,
			__reserved_2	:  3,
			mask		:  1,
			__reserved_3	: 15;
		u32 __reserved_4[3];
	} lvt_thermal;

/*340*/	struct { /* LVT - Performance Counter */
		u32   vector		:  8,
			delivery_mode	:  3,
			__reserved_1	:  1,
			delivery_status	:  1,
			__reserved_2	:  3,
			mask		:  1,
			__reserved_3	: 15;
		u32 __reserved_4[3];
	} lvt_pc;

/*350*/	struct { /* LVT - LINT0 */
		u32   vector		:  8,
			delivery_mode	:  3,
			__reserved_1	:  1,
			delivery_status	:  1,
			polarity	:  1,
			remote_irr	:  1,
			trigger		:  1,
			mask		:  1,
			__reserved_2	: 15;
		u32 __reserved_3[3];
	} lvt_lint0;

/*360*/	struct { /* LVT - LINT1 */
		u32   vector		:  8,
			delivery_mode	:  3,
			__reserved_1	:  1,
			delivery_status	:  1,
			polarity	:  1,
			remote_irr	:  1,
			trigger		:  1,
			mask		:  1,
			__reserved_2	: 15;
		u32 __reserved_3[3];
	} lvt_lint1;

/*370*/	struct { /* LVT - Error */
		u32   vector		:  8,
			__reserved_1	:  4,
			delivery_status	:  1,
			__reserved_2	:  3,
			mask		:  1,
			__reserved_3	: 15;
		u32 __reserved_4[3];
	} lvt_error;

/*380*/	struct { /* Timer Initial Count Register */
		u32   initial_count;
		u32 __reserved_2[3];
	} timer_icr;

/*390*/	const
	struct { /* Timer Current Count Register */
		u32   curr_count;
		u32 __reserved_2[3];
	} timer_ccr;

/*3A0*/	struct { u32 __reserved[4]; } __reserved_16;

/*3B0*/	struct { u32 __reserved[4]; } __reserved_17;

/*3C0*/	struct { u32 __reserved[4]; } __reserved_18;

/*3D0*/	struct { u32 __reserved[4]; } __reserved_19;

/*3E0*/	struct { /* Timer Divide Configuration Register */
		u32   divisor		:  4,
			__reserved_1	: 28;
		u32 __reserved_2[3];
	} timer_dcr;

/*3F0*/	struct { u32 __reserved[4]; } __reserved_20;

} __attribute__ ((packed));

#undef u32

#ifdef CONFIG_X86_32
 #define BAD_APICID 0xFFu
#else
 #define BAD_APICID 0xFFFFu
#endif
#endif /* _ASM_X86_APICDEF_H */
