/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000, 07 MIPS Technologies, Inc.
 *
 * GIC Register Definitions
 *
 */
#ifndef _ASM_GICREGS_H
#define _ASM_GICREGS_H

#undef	GICISBYTELITTLEENDIAN

/* Constants */
#define GIC_POL_POS			1
#define GIC_POL_NEG			0
#define GIC_TRIG_EDGE			1
#define GIC_TRIG_LEVEL			0

#define GIC_NUM_INTRS			(24 + NR_CPUS * 2)

#define MSK(n) ((1 << (n)) - 1)
#define REG32(addr)		(*(volatile unsigned int *) (addr))
#define REG(base, offs)		REG32((unsigned long)(base) + offs##_##OFS)
#define REGP(base, phys)	REG32((unsigned long)(base) + (phys))

/* Accessors */
#define GIC_REG(segment, offset) \
	REG32(_gic_base + segment##_##SECTION_OFS + offset##_##OFS)
#define GIC_REG_ADDR(segment, offset) \
	REG32(_gic_base + segment##_##SECTION_OFS + offset)

#define GIC_ABS_REG(segment, offset) \
	(_gic_base + segment##_##SECTION_OFS + offset##_##OFS)
#define GIC_REG_ABS_ADDR(segment, offset) \
	(_gic_base + segment##_##SECTION_OFS + offset)

#ifdef GICISBYTELITTLEENDIAN
#define GICREAD(reg, data)	((data) = (reg), (data) = le32_to_cpu(data))
#define GICWRITE(reg, data)	((reg) = cpu_to_le32(data))
#define GICBIS(reg, bits)			\
	({unsigned int data;			\
		GICREAD(reg, data);		\
		data |= bits;			\
		GICWRITE(reg, data);		\
	})

#else
#define GICREAD(reg, data)	((data) = (reg))
#define GICWRITE(reg, data)	((reg) = (data))
#define GICBIS(reg, bits)	((reg) |= (bits))
#endif


/* GIC Address Space */
#define SHARED_SECTION_OFS		0x0000
#define SHARED_SECTION_SIZE		0x8000
#define VPE_LOCAL_SECTION_OFS		0x8000
#define VPE_LOCAL_SECTION_SIZE		0x4000
#define VPE_OTHER_SECTION_OFS		0xc000
#define VPE_OTHER_SECTION_SIZE		0x4000
#define USM_VISIBLE_SECTION_OFS		0x10000
#define USM_VISIBLE_SECTION_SIZE	0x10000

/* Register Map for Shared Section */

#define GIC_SH_CONFIG_OFS		0x0000

/* Shared Global Counter */
#define GIC_SH_COUNTER_31_00_OFS	0x0010
#define GIC_SH_COUNTER_63_32_OFS	0x0014
#define GIC_SH_REVISIONID_OFS		0x0020

/* Interrupt Polarity */
#define GIC_SH_POL_31_0_OFS		0x0100
#define GIC_SH_POL_63_32_OFS		0x0104
#define GIC_SH_POL_95_64_OFS		0x0108
#define GIC_SH_POL_127_96_OFS		0x010c
#define GIC_SH_POL_159_128_OFS		0x0110
#define GIC_SH_POL_191_160_OFS		0x0114
#define GIC_SH_POL_223_192_OFS		0x0118
#define GIC_SH_POL_255_224_OFS		0x011c

/* Edge/Level Triggering */
#define GIC_SH_TRIG_31_0_OFS		0x0180
#define GIC_SH_TRIG_63_32_OFS		0x0184
#define GIC_SH_TRIG_95_64_OFS		0x0188
#define GIC_SH_TRIG_127_96_OFS		0x018c
#define GIC_SH_TRIG_159_128_OFS		0x0190
#define GIC_SH_TRIG_191_160_OFS		0x0194
#define GIC_SH_TRIG_223_192_OFS		0x0198
#define GIC_SH_TRIG_255_224_OFS		0x019c

/* Dual Edge Triggering */
#define GIC_SH_DUAL_31_0_OFS		0x0200
#define GIC_SH_DUAL_63_32_OFS		0x0204
#define GIC_SH_DUAL_95_64_OFS		0x0208
#define GIC_SH_DUAL_127_96_OFS		0x020c
#define GIC_SH_DUAL_159_128_OFS		0x0210
#define GIC_SH_DUAL_191_160_OFS		0x0214
#define GIC_SH_DUAL_223_192_OFS		0x0218
#define GIC_SH_DUAL_255_224_OFS		0x021c

/* Set/Clear corresponding bit in Edge Detect Register */
#define GIC_SH_WEDGE_OFS		0x0280

/* Reset Mask - Disables Interrupt */
#define GIC_SH_RMASK_31_0_OFS		0x0300
#define GIC_SH_RMASK_63_32_OFS		0x0304
#define GIC_SH_RMASK_95_64_OFS		0x0308
#define GIC_SH_RMASK_127_96_OFS		0x030c
#define GIC_SH_RMASK_159_128_OFS	0x0310
#define GIC_SH_RMASK_191_160_OFS	0x0314
#define GIC_SH_RMASK_223_192_OFS	0x0318
#define GIC_SH_RMASK_255_224_OFS	0x031c

/* Set Mask (WO) - Enables Interrupt */
#define GIC_SH_SMASK_31_0_OFS		0x0380
#define GIC_SH_SMASK_63_32_OFS		0x0384
#define GIC_SH_SMASK_95_64_OFS		0x0388
#define GIC_SH_SMASK_127_96_OFS		0x038c
#define GIC_SH_SMASK_159_128_OFS	0x0390
#define GIC_SH_SMASK_191_160_OFS	0x0394
#define GIC_SH_SMASK_223_192_OFS	0x0398
#define GIC_SH_SMASK_255_224_OFS	0x039c

/* Global Interrupt Mask Register (RO) - Bit Set == Interrupt enabled */
#define GIC_SH_MASK_31_0_OFS		0x0400
#define GIC_SH_MASK_63_32_OFS		0x0404
#define GIC_SH_MASK_95_64_OFS		0x0408
#define GIC_SH_MASK_127_96_OFS		0x040c
#define GIC_SH_MASK_159_128_OFS		0x0410
#define GIC_SH_MASK_191_160_OFS		0x0414
#define GIC_SH_MASK_223_192_OFS		0x0418
#define GIC_SH_MASK_255_224_OFS		0x041c

/* Pending Global Interrupts (RO) */
#define GIC_SH_PEND_31_0_OFS		0x0480
#define GIC_SH_PEND_63_32_OFS		0x0484
#define GIC_SH_PEND_95_64_OFS		0x0488
#define GIC_SH_PEND_127_96_OFS		0x048c
#define GIC_SH_PEND_159_128_OFS		0x0490
#define GIC_SH_PEND_191_160_OFS		0x0494
#define GIC_SH_PEND_223_192_OFS		0x0498
#define GIC_SH_PEND_255_224_OFS		0x049c

#define GIC_SH_INTR_MAP_TO_PIN_BASE_OFS 0x0500

/* Maps Interrupt X to a Pin */
#define GIC_SH_MAP_TO_PIN(intr) \
	(GIC_SH_INTR_MAP_TO_PIN_BASE_OFS + (4 * intr))

#define GIC_SH_INTR_MAP_TO_VPE_BASE_OFS 0x2000

/* Maps Interrupt X to a VPE */
#define GIC_SH_MAP_TO_VPE_REG_OFF(intr, vpe) \
	(GIC_SH_INTR_MAP_TO_VPE_BASE_OFS + (32 * (intr)) + (((vpe) / 32) * 4))
#define GIC_SH_MAP_TO_VPE_REG_BIT(vpe)	(1 << ((vpe) % 32))

/* Convert an interrupt number to a byte offset/bit for multi-word registers */
#define GIC_INTR_OFS(intr) (((intr) / 32)*4)
#define GIC_INTR_BIT(intr) ((intr) % 32)

/* Polarity : Reset Value is always 0 */
#define GIC_SH_SET_POLARITY_OFS		0x0100
#define GIC_SET_POLARITY(intr, pol) \
	GICBIS(GIC_REG_ADDR(SHARED, GIC_SH_SET_POLARITY_OFS + \
		GIC_INTR_OFS(intr)), (pol) << GIC_INTR_BIT(intr))

/* Triggering : Reset Value is always 0 */
#define GIC_SH_SET_TRIGGER_OFS		0x0180
#define GIC_SET_TRIGGER(intr, trig) \
	GICBIS(GIC_REG_ADDR(SHARED, GIC_SH_SET_TRIGGER_OFS + \
		GIC_INTR_OFS(intr)), (trig) << GIC_INTR_BIT(intr))

/* Mask manipulation */
#define GIC_SH_SMASK_OFS		0x0380
#define GIC_SET_INTR_MASK(intr) \
	GICWRITE(GIC_REG_ADDR(SHARED, GIC_SH_SMASK_OFS + \
		GIC_INTR_OFS(intr)), 1 << GIC_INTR_BIT(intr))
#define GIC_SH_RMASK_OFS		0x0300
#define GIC_CLR_INTR_MASK(intr) \
	GICWRITE(GIC_REG_ADDR(SHARED, GIC_SH_RMASK_OFS + \
		GIC_INTR_OFS(intr)), 1 << GIC_INTR_BIT(intr))

/* Register Map for Local Section */
#define GIC_VPE_CTL_OFS			0x0000
#define GIC_VPE_PEND_OFS		0x0004
#define GIC_VPE_MASK_OFS		0x0008
#define GIC_VPE_RMASK_OFS		0x000c
#define GIC_VPE_SMASK_OFS		0x0010
#define GIC_VPE_WD_MAP_OFS		0x0040
#define GIC_VPE_COMPARE_MAP_OFS		0x0044
#define GIC_VPE_TIMER_MAP_OFS		0x0048
#define GIC_VPE_PERFCTR_MAP_OFS		0x0050
#define GIC_VPE_SWINT0_MAP_OFS		0x0054
#define GIC_VPE_SWINT1_MAP_OFS		0x0058
#define GIC_VPE_OTHER_ADDR_OFS		0x0080
#define GIC_VPE_WD_CONFIG0_OFS		0x0090
#define GIC_VPE_WD_COUNT0_OFS		0x0094
#define GIC_VPE_WD_INITIAL0_OFS		0x0098
#define GIC_VPE_COMPARE_LO_OFS		0x00a0
#define GIC_VPE_COMPARE_HI		0x00a4

#define GIC_VPE_EIC_SHADOW_SET_BASE	0x0100
#define GIC_VPE_EIC_SS(intr) \
	(GIC_VPE_EIC_SHADOW_SET_BASE + (4 * intr))

#define GIC_VPE_EIC_VEC_BASE		0x0800
#define GIC_VPE_EIC_VEC(intr) \
	(GIC_VPE_EIC_VEC_BASE + (4 * intr))

#define GIC_VPE_TENABLE_NMI_OFS		0x1000
#define GIC_VPE_TENABLE_YQ_OFS		0x1004
#define GIC_VPE_TENABLE_INT_31_0_OFS	0x1080
#define GIC_VPE_TENABLE_INT_63_32_OFS	0x1084

/* User Mode Visible Section Register Map */
#define GIC_UMV_SH_COUNTER_31_00_OFS	0x0000
#define GIC_UMV_SH_COUNTER_63_32_OFS	0x0004

/* Masks */
#define GIC_SH_CONFIG_COUNTSTOP_SHF	28
#define GIC_SH_CONFIG_COUNTSTOP_MSK	(MSK(1) << GIC_SH_CONFIG_COUNTSTOP_SHF)

#define GIC_SH_CONFIG_COUNTBITS_SHF	24
#define GIC_SH_CONFIG_COUNTBITS_MSK	(MSK(4) << GIC_SH_CONFIG_COUNTBITS_SHF)

#define GIC_SH_CONFIG_NUMINTRS_SHF	16
#define GIC_SH_CONFIG_NUMINTRS_MSK	(MSK(8) << GIC_SH_CONFIG_NUMINTRS_SHF)

#define GIC_SH_CONFIG_NUMVPES_SHF	0
#define GIC_SH_CONFIG_NUMVPES_MSK	(MSK(8) << GIC_SH_CONFIG_NUMVPES_SHF)

#define GIC_SH_WEDGE_SET(intr)		(intr | (0x1 << 31))
#define GIC_SH_WEDGE_CLR(intr)		(intr & ~(0x1 << 31))

#define GIC_MAP_TO_PIN_SHF		31
#define GIC_MAP_TO_PIN_MSK		(MSK(1) << GIC_MAP_TO_PIN_SHF)
#define GIC_MAP_TO_NMI_SHF		30
#define GIC_MAP_TO_NMI_MSK		(MSK(1) << GIC_MAP_TO_NMI_SHF)
#define GIC_MAP_TO_YQ_SHF		29
#define GIC_MAP_TO_YQ_MSK		(MSK(1) << GIC_MAP_TO_YQ_SHF)
#define GIC_MAP_SHF			0
#define GIC_MAP_MSK			(MSK(6) << GIC_MAP_SHF)

/* GIC_VPE_CTL Masks */
#define GIC_VPE_CTL_PERFCNT_RTBL_SHF	2
#define GIC_VPE_CTL_PERFCNT_RTBL_MSK	(MSK(1) << GIC_VPE_CTL_PERFCNT_RTBL_SHF)
#define GIC_VPE_CTL_TIMER_RTBL_SHF	1
#define GIC_VPE_CTL_TIMER_RTBL_MSK	(MSK(1) << GIC_VPE_CTL_TIMER_RTBL_SHF)
#define GIC_VPE_CTL_EIC_MODE_SHF	0
#define GIC_VPE_CTL_EIC_MODE_MSK	(MSK(1) << GIC_VPE_CTL_EIC_MODE_SHF)

/* GIC_VPE_PEND Masks */
#define GIC_VPE_PEND_WD_SHF		0
#define GIC_VPE_PEND_WD_MSK		(MSK(1) << GIC_VPE_PEND_WD_SHF)
#define GIC_VPE_PEND_CMP_SHF		1
#define GIC_VPE_PEND_CMP_MSK		(MSK(1) << GIC_VPE_PEND_CMP_SHF)
#define GIC_VPE_PEND_TIMER_SHF		2
#define GIC_VPE_PEND_TIMER_MSK		(MSK(1) << GIC_VPE_PEND_TIMER_SHF)
#define GIC_VPE_PEND_PERFCOUNT_SHF	3
#define GIC_VPE_PEND_PERFCOUNT_MSK	(MSK(1) << GIC_VPE_PEND_PERFCOUNT_SHF)
#define GIC_VPE_PEND_SWINT0_SHF		4
#define GIC_VPE_PEND_SWINT0_MSK		(MSK(1) << GIC_VPE_PEND_SWINT0_SHF)
#define GIC_VPE_PEND_SWINT1_SHF		5
#define GIC_VPE_PEND_SWINT1_MSK		(MSK(1) << GIC_VPE_PEND_SWINT1_SHF)

/* GIC_VPE_RMASK Masks */
#define GIC_VPE_RMASK_WD_SHF		0
#define GIC_VPE_RMASK_WD_MSK		(MSK(1) << GIC_VPE_RMASK_WD_SHF)
#define GIC_VPE_RMASK_CMP_SHF		1
#define GIC_VPE_RMASK_CMP_MSK		(MSK(1) << GIC_VPE_RMASK_CMP_SHF)
#define GIC_VPE_RMASK_TIMER_SHF		2
#define GIC_VPE_RMASK_TIMER_MSK		(MSK(1) << GIC_VPE_RMASK_TIMER_SHF)
#define GIC_VPE_RMASK_PERFCNT_SHF	3
#define GIC_VPE_RMASK_PERFCNT_MSK	(MSK(1) << GIC_VPE_RMASK_PERFCNT_SHF)
#define GIC_VPE_RMASK_SWINT0_SHF	4
#define GIC_VPE_RMASK_SWINT0_MSK	(MSK(1) << GIC_VPE_RMASK_SWINT0_SHF)
#define GIC_VPE_RMASK_SWINT1_SHF	5
#define GIC_VPE_RMASK_SWINT1_MSK	(MSK(1) << GIC_VPE_RMASK_SWINT1_SHF)

/* GIC_VPE_SMASK Masks */
#define GIC_VPE_SMASK_WD_SHF		0
#define GIC_VPE_SMASK_WD_MSK		(MSK(1) << GIC_VPE_SMASK_WD_SHF)
#define GIC_VPE_SMASK_CMP_SHF		1
#define GIC_VPE_SMASK_CMP_MSK		(MSK(1) << GIC_VPE_SMASK_CMP_SHF)
#define GIC_VPE_SMASK_TIMER_SHF		2
#define GIC_VPE_SMASK_TIMER_MSK		(MSK(1) << GIC_VPE_SMASK_TIMER_SHF)
#define GIC_VPE_SMASK_PERFCNT_SHF	3
#define GIC_VPE_SMASK_PERFCNT_MSK	(MSK(1) << GIC_VPE_SMASK_PERFCNT_SHF)
#define GIC_VPE_SMASK_SWINT0_SHF	4
#define GIC_VPE_SMASK_SWINT0_MSK	(MSK(1) << GIC_VPE_SMASK_SWINT0_SHF)
#define GIC_VPE_SMASK_SWINT1_SHF	5
#define GIC_VPE_SMASK_SWINT1_MSK	(MSK(1) << GIC_VPE_SMASK_SWINT1_SHF)

/*
 * Set the Mapping of Interrupt X to a VPE.
 */
#define GIC_SH_MAP_TO_VPE_SMASK(intr, vpe) \
	GICWRITE(GIC_REG_ADDR(SHARED, GIC_SH_MAP_TO_VPE_REG_OFF(intr, vpe)), \
		 GIC_SH_MAP_TO_VPE_REG_BIT(vpe))

struct gic_pcpu_mask {
	DECLARE_BITMAP(pcpu_mask, GIC_NUM_INTRS);
};

struct gic_pending_regs {
	DECLARE_BITMAP(pending, GIC_NUM_INTRS);
};

struct gic_intrmask_regs {
	DECLARE_BITMAP(intrmask, GIC_NUM_INTRS);
};

/*
 * Interrupt Meta-data specification. The ipiflag helps
 * in building ipi_map.
 */
struct gic_intr_map {
	unsigned int cpunum;	/* Directed to this CPU */
#define GIC_UNUSED		0xdead			/* Dummy data */
	unsigned int pin;	/* Directed to this Pin */
	unsigned int polarity;	/* Polarity : +/-	*/
	unsigned int trigtype;	/* Trigger  : Edge/Levl */
	unsigned int flags;	/* Misc flags	*/
#define GIC_FLAG_IPI	       0x01
#define GIC_FLAG_TRANSPARENT   0x02
};

/*
 * This is only used in EIC mode. This helps to figure out which
 * shared interrupts we need to process when we get a vector interrupt.
 */
#define GIC_MAX_SHARED_INTR  0x5
struct gic_shared_intr_map {
	unsigned int num_shared_intr;
	unsigned int intr_list[GIC_MAX_SHARED_INTR];
	unsigned int local_intr_mask;
};

/* GIC nomenclature for Core Interrupt Pins. */
#define GIC_CPU_INT0		0 /* Core Interrupt 2 */
#define GIC_CPU_INT1		1 /* .		      */
#define GIC_CPU_INT2		2 /* .		      */
#define GIC_CPU_INT3		3 /* .		      */
#define GIC_CPU_INT4		4 /* .		      */
#define GIC_CPU_INT5		5 /* Core Interrupt 5 */

/* Local GIC interrupts. */
#define GIC_INT_TMR		(GIC_CPU_INT5)
#define GIC_INT_PERFCTR		(GIC_CPU_INT5)

/* Add 2 to convert non-EIC hardware interrupt to EIC vector number. */
#define GIC_CPU_TO_VEC_OFFSET	(2)

/* Mapped interrupt to pin X, then GIC will generate the vector (X+1). */
#define GIC_PIN_TO_VEC_OFFSET	(1)

extern unsigned int gic_present;
extern unsigned int gic_frequency;
extern unsigned long _gic_base;
extern unsigned int gic_irq_base;
extern unsigned int gic_irq_flags[];
extern struct gic_shared_intr_map gic_shared_intr_map[];

extern void gic_init(unsigned long gic_base_addr,
	unsigned long gic_addrspace_size, struct gic_intr_map *intrmap,
	unsigned int intrmap_size, unsigned int irqbase);

extern void gic_clocksource_init(unsigned int);
extern unsigned int gic_get_int(void);
extern void gic_send_ipi(unsigned int intr);
extern unsigned int plat_ipi_call_int_xlate(unsigned int);
extern unsigned int plat_ipi_resched_int_xlate(unsigned int);
extern void gic_bind_eic_interrupt(int irq, int set);
extern unsigned int gic_get_timer_pending(void);
extern void gic_enable_interrupt(int irq_vec);
extern void gic_disable_interrupt(int irq_vec);
extern void gic_irq_ack(struct irq_data *d);
extern void gic_finish_irq(struct irq_data *d);
extern void gic_platform_init(int irqs, struct irq_chip *irq_controller);

#endif /* _ASM_GICREGS_H */
