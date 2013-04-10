#ifndef _ASM_POWERPC_MPIC_H
#define _ASM_POWERPC_MPIC_H
#ifdef __KERNEL__

#include <linux/irq.h>
#include <asm/dcr.h>
#include <asm/msi_bitmap.h>

/*
 * Global registers
 */

#define MPIC_GREG_BASE			0x01000

#define MPIC_GREG_FEATURE_0		0x00000
#define		MPIC_GREG_FEATURE_LAST_SRC_MASK		0x07ff0000
#define		MPIC_GREG_FEATURE_LAST_SRC_SHIFT	16
#define		MPIC_GREG_FEATURE_LAST_CPU_MASK		0x00001f00
#define		MPIC_GREG_FEATURE_LAST_CPU_SHIFT	8
#define		MPIC_GREG_FEATURE_VERSION_MASK		0xff
#define MPIC_GREG_FEATURE_1		0x00010
#define MPIC_GREG_GLOBAL_CONF_0		0x00020
#define		MPIC_GREG_GCONF_RESET			0x80000000
/* On the FSL mpic implementations the Mode field is expand to be
 * 2 bits wide:
 *	0b00 = pass through (interrupts routed to IRQ0)
 *	0b01 = Mixed mode
 *	0b10 = reserved
 *	0b11 = External proxy / coreint
 */
#define		MPIC_GREG_GCONF_COREINT			0x60000000
#define		MPIC_GREG_GCONF_8259_PTHROU_DIS		0x20000000
#define		MPIC_GREG_GCONF_NO_BIAS			0x10000000
#define		MPIC_GREG_GCONF_BASE_MASK		0x000fffff
#define		MPIC_GREG_GCONF_MCK			0x08000000
#define MPIC_GREG_GLOBAL_CONF_1		0x00030
#define MPIC_GREG_VENDOR_0		0x00040
#define MPIC_GREG_VENDOR_1		0x00050
#define MPIC_GREG_VENDOR_2		0x00060
#define MPIC_GREG_VENDOR_3		0x00070
#define MPIC_GREG_VENDOR_ID		0x00080
#define 	MPIC_GREG_VENDOR_ID_STEPPING_MASK	0x00ff0000
#define 	MPIC_GREG_VENDOR_ID_STEPPING_SHIFT	16
#define 	MPIC_GREG_VENDOR_ID_DEVICE_ID_MASK	0x0000ff00
#define 	MPIC_GREG_VENDOR_ID_DEVICE_ID_SHIFT	8
#define 	MPIC_GREG_VENDOR_ID_VENDOR_ID_MASK	0x000000ff
#define MPIC_GREG_PROCESSOR_INIT	0x00090
#define MPIC_GREG_IPI_VECTOR_PRI_0	0x000a0
#define MPIC_GREG_IPI_VECTOR_PRI_1	0x000b0
#define MPIC_GREG_IPI_VECTOR_PRI_2	0x000c0
#define MPIC_GREG_IPI_VECTOR_PRI_3	0x000d0
#define MPIC_GREG_IPI_STRIDE		0x10
#define MPIC_GREG_SPURIOUS		0x000e0
#define MPIC_GREG_TIMER_FREQ		0x000f0

/*
 *
 * Timer registers
 */
#define MPIC_TIMER_BASE			0x01100
#define MPIC_TIMER_STRIDE		0x40
#define MPIC_TIMER_GROUP_STRIDE		0x1000

#define MPIC_TIMER_CURRENT_CNT		0x00000
#define MPIC_TIMER_BASE_CNT		0x00010
#define MPIC_TIMER_VECTOR_PRI		0x00020
#define MPIC_TIMER_DESTINATION		0x00030

/*
 * Per-Processor registers
 */

#define MPIC_CPU_THISBASE		0x00000
#define MPIC_CPU_BASE			0x20000
#define MPIC_CPU_STRIDE			0x01000

#define MPIC_CPU_IPI_DISPATCH_0		0x00040
#define MPIC_CPU_IPI_DISPATCH_1		0x00050
#define MPIC_CPU_IPI_DISPATCH_2		0x00060
#define MPIC_CPU_IPI_DISPATCH_3		0x00070
#define MPIC_CPU_IPI_DISPATCH_STRIDE	0x00010
#define MPIC_CPU_CURRENT_TASK_PRI	0x00080
#define 	MPIC_CPU_TASKPRI_MASK			0x0000000f
#define MPIC_CPU_WHOAMI			0x00090
#define 	MPIC_CPU_WHOAMI_MASK			0x0000001f
#define MPIC_CPU_INTACK			0x000a0
#define MPIC_CPU_EOI			0x000b0
#define MPIC_CPU_MCACK			0x000c0

/*
 * Per-source registers
 */

#define MPIC_IRQ_BASE			0x10000
#define MPIC_IRQ_STRIDE			0x00020
#define MPIC_IRQ_VECTOR_PRI		0x00000
#define 	MPIC_VECPRI_MASK			0x80000000
#define 	MPIC_VECPRI_ACTIVITY			0x40000000	/* Read Only */
#define 	MPIC_VECPRI_PRIORITY_MASK		0x000f0000
#define 	MPIC_VECPRI_PRIORITY_SHIFT		16
#define 	MPIC_VECPRI_VECTOR_MASK			0x000007ff
#define 	MPIC_VECPRI_POLARITY_POSITIVE		0x00800000
#define 	MPIC_VECPRI_POLARITY_NEGATIVE		0x00000000
#define 	MPIC_VECPRI_POLARITY_MASK		0x00800000
#define 	MPIC_VECPRI_SENSE_LEVEL			0x00400000
#define 	MPIC_VECPRI_SENSE_EDGE			0x00000000
#define 	MPIC_VECPRI_SENSE_MASK			0x00400000
#define MPIC_IRQ_DESTINATION		0x00010

#define MPIC_FSL_BRR1			0x00000
#define 	MPIC_FSL_BRR1_VER			0x0000ffff

#define MPIC_MAX_IRQ_SOURCES	2048
#define MPIC_MAX_CPUS		32
#define MPIC_MAX_ISU		32

#define MPIC_MAX_ERR      32
#define MPIC_FSL_ERR_INT  16

/*
 * Tsi108 implementation of MPIC has many differences from the original one
 */

/*
 * Global registers
 */

#define TSI108_GREG_BASE		0x00000
#define TSI108_GREG_FEATURE_0		0x00000
#define TSI108_GREG_GLOBAL_CONF_0	0x00004
#define TSI108_GREG_VENDOR_ID		0x0000c
#define TSI108_GREG_IPI_VECTOR_PRI_0	0x00204		/* Doorbell 0 */
#define TSI108_GREG_IPI_STRIDE		0x0c
#define TSI108_GREG_SPURIOUS		0x00010
#define TSI108_GREG_TIMER_FREQ		0x00014

/*
 * Timer registers
 */
#define TSI108_TIMER_BASE		0x0030
#define TSI108_TIMER_STRIDE		0x10
#define TSI108_TIMER_CURRENT_CNT	0x00000
#define TSI108_TIMER_BASE_CNT		0x00004
#define TSI108_TIMER_VECTOR_PRI		0x00008
#define TSI108_TIMER_DESTINATION	0x0000c

/*
 * Per-Processor registers
 */
#define TSI108_CPU_BASE			0x00300
#define TSI108_CPU_STRIDE		0x00040
#define TSI108_CPU_IPI_DISPATCH_0	0x00200
#define TSI108_CPU_IPI_DISPATCH_STRIDE	0x00000
#define TSI108_CPU_CURRENT_TASK_PRI	0x00000
#define TSI108_CPU_WHOAMI		0xffffffff
#define TSI108_CPU_INTACK		0x00004
#define TSI108_CPU_EOI			0x00008
#define TSI108_CPU_MCACK		0x00004 /* Doesn't really exist here */

/*
 * Per-source registers
 */
#define TSI108_IRQ_BASE			0x00100
#define TSI108_IRQ_STRIDE		0x00008
#define TSI108_IRQ_VECTOR_PRI		0x00000
#define TSI108_VECPRI_VECTOR_MASK	0x000000ff
#define TSI108_VECPRI_POLARITY_POSITIVE	0x01000000
#define TSI108_VECPRI_POLARITY_NEGATIVE	0x00000000
#define TSI108_VECPRI_SENSE_LEVEL	0x02000000
#define TSI108_VECPRI_SENSE_EDGE	0x00000000
#define TSI108_VECPRI_POLARITY_MASK	0x01000000
#define TSI108_VECPRI_SENSE_MASK	0x02000000
#define TSI108_IRQ_DESTINATION		0x00004

/* weird mpic register indices and mask bits in the HW info array */
enum {
	MPIC_IDX_GREG_BASE = 0,
	MPIC_IDX_GREG_FEATURE_0,
	MPIC_IDX_GREG_GLOBAL_CONF_0,
	MPIC_IDX_GREG_VENDOR_ID,
	MPIC_IDX_GREG_IPI_VECTOR_PRI_0,
	MPIC_IDX_GREG_IPI_STRIDE,
	MPIC_IDX_GREG_SPURIOUS,
	MPIC_IDX_GREG_TIMER_FREQ,

	MPIC_IDX_TIMER_BASE,
	MPIC_IDX_TIMER_STRIDE,
	MPIC_IDX_TIMER_CURRENT_CNT,
	MPIC_IDX_TIMER_BASE_CNT,
	MPIC_IDX_TIMER_VECTOR_PRI,
	MPIC_IDX_TIMER_DESTINATION,

	MPIC_IDX_CPU_BASE,
	MPIC_IDX_CPU_STRIDE,
	MPIC_IDX_CPU_IPI_DISPATCH_0,
	MPIC_IDX_CPU_IPI_DISPATCH_STRIDE,
	MPIC_IDX_CPU_CURRENT_TASK_PRI,
	MPIC_IDX_CPU_WHOAMI,
	MPIC_IDX_CPU_INTACK,
	MPIC_IDX_CPU_EOI,
	MPIC_IDX_CPU_MCACK,

	MPIC_IDX_IRQ_BASE,
	MPIC_IDX_IRQ_STRIDE,
	MPIC_IDX_IRQ_VECTOR_PRI,

	MPIC_IDX_VECPRI_VECTOR_MASK,
	MPIC_IDX_VECPRI_POLARITY_POSITIVE,
	MPIC_IDX_VECPRI_POLARITY_NEGATIVE,
	MPIC_IDX_VECPRI_SENSE_LEVEL,
	MPIC_IDX_VECPRI_SENSE_EDGE,
	MPIC_IDX_VECPRI_POLARITY_MASK,
	MPIC_IDX_VECPRI_SENSE_MASK,
	MPIC_IDX_IRQ_DESTINATION,
	MPIC_IDX_END
};


#ifdef CONFIG_MPIC_U3_HT_IRQS
/* Fixup table entry */
struct mpic_irq_fixup
{
	u8 __iomem	*base;
	u8 __iomem	*applebase;
	u32		data;
	unsigned int	index;
};
#endif /* CONFIG_MPIC_U3_HT_IRQS */


enum mpic_reg_type {
	mpic_access_mmio_le,
	mpic_access_mmio_be,
#ifdef CONFIG_PPC_DCR
	mpic_access_dcr
#endif
};

struct mpic_reg_bank {
	u32 __iomem	*base;
#ifdef CONFIG_PPC_DCR
	dcr_host_t	dhost;
#endif /* CONFIG_PPC_DCR */
};

struct mpic_irq_save {
	u32		vecprio,
			dest;
#ifdef CONFIG_MPIC_U3_HT_IRQS
	u32		fixup_data;
#endif
};

/* The instance data of a given MPIC */
struct mpic
{
	/* The OpenFirmware dt node for this MPIC */
	struct device_node *node;

	/* The remapper for this MPIC */
	struct irq_domain	*irqhost;

	/* The "linux" controller struct */
	struct irq_chip		hc_irq;
#ifdef CONFIG_MPIC_U3_HT_IRQS
	struct irq_chip		hc_ht_irq;
#endif
#ifdef CONFIG_SMP
	struct irq_chip		hc_ipi;
#endif
	struct irq_chip		hc_tm;
	struct irq_chip		hc_err;
	const char		*name;
	/* Flags */
	unsigned int		flags;
	/* How many irq sources in a given ISU */
	unsigned int		isu_size;
	unsigned int		isu_shift;
	unsigned int		isu_mask;
	/* Number of sources */
	unsigned int		num_sources;

	/* vector numbers used for internal sources (ipi/timers) */
	unsigned int		ipi_vecs[4];
	unsigned int		timer_vecs[8];
	/* vector numbers used for FSL MPIC error interrupts */
	unsigned int		err_int_vecs[MPIC_MAX_ERR];

	/* Spurious vector to program into unused sources */
	unsigned int		spurious_vec;

#ifdef CONFIG_MPIC_U3_HT_IRQS
	/* The fixup table */
	struct mpic_irq_fixup	*fixups;
	raw_spinlock_t	fixup_lock;
#endif

	/* Register access method */
	enum mpic_reg_type	reg_type;

	/* The physical base address of the MPIC */
	phys_addr_t paddr;

	/* The various ioremap'ed bases */
	struct mpic_reg_bank	thiscpuregs;
	struct mpic_reg_bank	gregs;
	struct mpic_reg_bank	tmregs;
	struct mpic_reg_bank	cpuregs[MPIC_MAX_CPUS];
	struct mpic_reg_bank	isus[MPIC_MAX_ISU];

	/* ioremap'ed base for error interrupt registers */
	u32 __iomem	*err_regs;

	/* Protected sources */
	unsigned long		*protected;

#ifdef CONFIG_MPIC_WEIRD
	/* Pointer to HW info array */
	u32			*hw_set;
#endif

#ifdef CONFIG_PCI_MSI
	struct msi_bitmap	msi_bitmap;
#endif

#ifdef CONFIG_MPIC_BROKEN_REGREAD
	u32			isu_reg0_shadow[MPIC_MAX_IRQ_SOURCES];
#endif

	/* link */
	struct mpic		*next;

#ifdef CONFIG_PM
	struct mpic_irq_save	*save_data;
#endif
};

extern struct bus_type mpic_subsys;

/*
 * MPIC flags (passed to mpic_alloc)
 *
 * The top 4 bits contain an MPIC bhw id that is used to index the
 * register offsets and some masks when CONFIG_MPIC_WEIRD is set.
 * Note setting any ID (leaving those bits to 0) means standard MPIC
 */

/*
 * This is a secondary ("chained") controller; it only uses the CPU0
 * registers.  Primary controllers have IPIs and affinity control.
 */
#define MPIC_SECONDARY			0x00000001

/* Set this for a big-endian MPIC */
#define MPIC_BIG_ENDIAN			0x00000002
/* Broken U3 MPIC */
#define MPIC_U3_HT_IRQS			0x00000004
/* Broken IPI registers (autodetected) */
#define MPIC_BROKEN_IPI			0x00000008
/* Spurious vector requires EOI */
#define MPIC_SPV_EOI			0x00000020
/* No passthrough disable */
#define MPIC_NO_PTHROU_DIS		0x00000040
/* DCR based MPIC */
#define MPIC_USES_DCR			0x00000080
/* MPIC has 11-bit vector fields (or larger) */
#define MPIC_LARGE_VECTORS		0x00000100
/* Enable delivery of prio 15 interrupts as MCK instead of EE */
#define MPIC_ENABLE_MCK			0x00000200
/* Disable bias among target selection, spread interrupts evenly */
#define MPIC_NO_BIAS			0x00000400
/* Destination only supports a single CPU at a time */
#define MPIC_SINGLE_DEST_CPU		0x00001000
/* Enable CoreInt delivery of interrupts */
#define MPIC_ENABLE_COREINT		0x00002000
/* Do not reset the MPIC during initialization */
#define MPIC_NO_RESET			0x00004000
/* Freescale MPIC (compatible includes "fsl,mpic") */
#define MPIC_FSL			0x00008000
/* Freescale MPIC supports EIMR (error interrupt mask register).
 * This flag is set for MPIC version >= 4.1 (version determined
 * from the BRR1 register).
*/
#define MPIC_FSL_HAS_EIMR		0x00010000

/* MPIC HW modification ID */
#define MPIC_REGSET_MASK		0xf0000000
#define MPIC_REGSET(val)		(((val) & 0xf ) << 28)
#define MPIC_GET_REGSET(flags)		(((flags) >> 28) & 0xf)

#define	MPIC_REGSET_STANDARD		MPIC_REGSET(0)	/* Original MPIC */
#define	MPIC_REGSET_TSI108		MPIC_REGSET(1)	/* Tsi108/109 PIC */

/* Get the version of primary MPIC */
extern u32 fsl_mpic_primary_get_version(void);

/* Allocate the controller structure and setup the linux irq descs
 * for the range if interrupts passed in. No HW initialization is
 * actually performed.
 * 
 * @phys_addr:	physial base address of the MPIC
 * @flags:	flags, see constants above
 * @isu_size:	number of interrupts in an ISU. Use 0 to use a
 *              standard ISU-less setup (aka powermac)
 * @irq_offset: first irq number to assign to this mpic
 * @irq_count:  number of irqs to use with this mpic IRQ sources. Pass 0
 *	        to match the number of sources
 * @ipi_offset: first irq number to assign to this mpic IPI sources,
 *		used only on primary mpic
 * @senses:	array of sense values
 * @senses_num: number of entries in the array
 *
 * Note about the sense array. If none is passed, all interrupts are
 * setup to be level negative unless MPIC_U3_HT_IRQS is set in which
 * case they are edge positive (and the array is ignored anyway).
 * The values in the array start at the first source of the MPIC,
 * that is senses[0] correspond to linux irq "irq_offset".
 */
extern struct mpic *mpic_alloc(struct device_node *node,
			       phys_addr_t phys_addr,
			       unsigned int flags,
			       unsigned int isu_size,
			       unsigned int irq_count,
			       const char *name);

/* Assign ISUs, to call before mpic_init()
 *
 * @mpic:	controller structure as returned by mpic_alloc()
 * @isu_num:	ISU number
 * @phys_addr:	physical address of the ISU
 */
extern void mpic_assign_isu(struct mpic *mpic, unsigned int isu_num,
			    phys_addr_t phys_addr);


/* Initialize the controller. After this has been called, none of the above
 * should be called again for this mpic
 */
extern void mpic_init(struct mpic *mpic);

/*
 * All of the following functions must only be used after the
 * ISUs have been assigned and the controller fully initialized
 * with mpic_init()
 */


/* Change the priority of an interrupt. Default is 8 for irqs and
 * 10 for IPIs. You can call this on both IPIs and IRQ numbers, but the
 * IPI number is then the offset'ed (linux irq number mapped to the IPI)
 */
extern void mpic_irq_set_priority(unsigned int irq, unsigned int pri);

/* Setup a non-boot CPU */
extern void mpic_setup_this_cpu(void);

/* Clean up for kexec (or cpu offline or ...) */
extern void mpic_teardown_this_cpu(int secondary);

/* Get the current cpu priority for this cpu (0..15) */
extern int mpic_cpu_get_priority(void);

/* Set the current cpu priority for this cpu */
extern void mpic_cpu_set_priority(int prio);

/* Request IPIs on primary mpic */
extern void mpic_request_ipis(void);

/* Send a message (IPI) to a given target (cpu number or MSG_*) */
void smp_mpic_message_pass(int target, int msg);

/* Unmask a specific virq */
extern void mpic_unmask_irq(struct irq_data *d);
/* Mask a specific virq */
extern void mpic_mask_irq(struct irq_data *d);
/* EOI a specific virq */
extern void mpic_end_irq(struct irq_data *d);

/* Fetch interrupt from a given mpic */
extern unsigned int mpic_get_one_irq(struct mpic *mpic);
/* This one gets from the primary mpic */
extern unsigned int mpic_get_irq(void);
/* This one gets from the primary mpic via CoreInt*/
extern unsigned int mpic_get_coreint_irq(void);
/* Fetch Machine Check interrupt from primary mpic */
extern unsigned int mpic_get_mcirq(void);

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_MPIC_H */
