#ifndef ASM_CELL_PIC_H
#define ASM_CELL_PIC_H
#ifdef __KERNEL__
/*
 * Mapping of IIC pending bits into per-node interrupt numbers.
 *
 * Interrupt numbers are in the range 0...0x1ff where the top bit
 * (0x100) represent the source node. Only 2 nodes are supported with
 * the current code though it's trivial to extend that if necessary using
 * higher level bits
 *
 * The bottom 8 bits are split into 2 type bits and 6 data bits that
 * depend on the type:
 *
 * 00 (0x00 | data) : normal interrupt. data is (class << 4) | source
 * 01 (0x40 | data) : IO exception. data is the exception number as
 *                    defined by bit numbers in IIC_SR
 * 10 (0x80 | data) : IPI. data is the IPI number (obtained from the priority)
 *                    and node is always 0 (IPIs are per-cpu, their source is
 *                    not relevant)
 * 11 (0xc0 | data) : reserved
 *
 * In addition, interrupt number 0x80000000 is defined as always invalid
 * (that is the node field is expected to never extend to move than 23 bits)
 *
 */

enum {
	IIC_IRQ_INVALID		= 0x80000000u,
	IIC_IRQ_NODE_MASK	= 0x100,
	IIC_IRQ_NODE_SHIFT	= 8,
	IIC_IRQ_MAX		= 0x1ff,
	IIC_IRQ_TYPE_MASK	= 0xc0,
	IIC_IRQ_TYPE_NORMAL	= 0x00,
	IIC_IRQ_TYPE_IOEXC	= 0x40,
	IIC_IRQ_TYPE_IPI	= 0x80,
	IIC_IRQ_CLASS_SHIFT	= 4,
	IIC_IRQ_CLASS_0		= 0x00,
	IIC_IRQ_CLASS_1		= 0x10,
	IIC_IRQ_CLASS_2		= 0x20,
	IIC_SOURCE_COUNT	= 0x200,

	/* Here are defined the various source/dest units. Avoid using those
	 * definitions if you can, they are mostly here for reference
	 */
	IIC_UNIT_SPU_0		= 0x4,
	IIC_UNIT_SPU_1		= 0x7,
	IIC_UNIT_SPU_2		= 0x3,
	IIC_UNIT_SPU_3		= 0x8,
	IIC_UNIT_SPU_4		= 0x2,
	IIC_UNIT_SPU_5		= 0x9,
	IIC_UNIT_SPU_6		= 0x1,
	IIC_UNIT_SPU_7		= 0xa,
	IIC_UNIT_IOC_0		= 0x0,
	IIC_UNIT_IOC_1		= 0xb,
	IIC_UNIT_THREAD_0	= 0xe, /* target only */
	IIC_UNIT_THREAD_1	= 0xf, /* target only */
	IIC_UNIT_IIC		= 0xe, /* source only (IO exceptions) */

	/* Base numbers for the external interrupts */
	IIC_IRQ_EXT_IOIF0	=
		IIC_IRQ_TYPE_NORMAL | IIC_IRQ_CLASS_2 | IIC_UNIT_IOC_0,
	IIC_IRQ_EXT_IOIF1	=
		IIC_IRQ_TYPE_NORMAL | IIC_IRQ_CLASS_2 | IIC_UNIT_IOC_1,

	/* Base numbers for the IIC_ISR interrupts */
	IIC_IRQ_IOEX_TMI	= IIC_IRQ_TYPE_IOEXC | IIC_IRQ_CLASS_1 | 63,
	IIC_IRQ_IOEX_PMI	= IIC_IRQ_TYPE_IOEXC | IIC_IRQ_CLASS_1 | 62,
	IIC_IRQ_IOEX_ATI	= IIC_IRQ_TYPE_IOEXC | IIC_IRQ_CLASS_1 | 61,
	IIC_IRQ_IOEX_MATBFI	= IIC_IRQ_TYPE_IOEXC | IIC_IRQ_CLASS_1 | 60,
	IIC_IRQ_IOEX_ELDI	= IIC_IRQ_TYPE_IOEXC | IIC_IRQ_CLASS_1 | 59,

	/* Which bits in IIC_ISR are edge sensitive */
	IIC_ISR_EDGE_MASK	= 0x4ul,
};

extern void iic_init_IRQ(void);
extern void iic_message_pass(int cpu, int msg);
extern void iic_request_IPIs(void);
extern void iic_setup_cpu(void);

extern u8 iic_get_target_id(int cpu);

extern void spider_init_IRQ(void);

extern void iic_set_interrupt_routing(int cpu, int thread, int priority);

#endif
#endif /* ASM_CELL_PIC_H */
