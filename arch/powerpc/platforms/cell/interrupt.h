#ifndef ASM_CELL_PIC_H
#define ASM_CELL_PIC_H
#ifdef __KERNEL__
/*
 * Mapping of IIC pending bits into per-node
 * interrupt numbers.
 *
 * IRQ     FF CC SS PP   FF CC SS PP	Description
 *
 * 00-3f   80 02 +0 00 - 80 02 +0 3f	South Bridge
 * 00-3f   80 02 +b 00 - 80 02 +b 3f	South Bridge
 * 41-4a   80 00 +1 ** - 80 00 +a **	SPU Class 0
 * 51-5a   80 01 +1 ** - 80 01 +a **	SPU Class 1
 * 61-6a   80 02 +1 ** - 80 02 +a **	SPU Class 2
 * 70-7f   C0 ** ** 00 - C0 ** ** 0f	IPI
 *
 *    F flags
 *    C class
 *    S source
 *    P Priority
 *    + node number
 *    * don't care
 *
 * A node consists of a Cell Broadband Engine and an optional
 * south bridge device providing a maximum of 64 IRQs.
 * The south bridge may be connected to either IOIF0
 * or IOIF1.
 * Each SPE is represented as three IRQ lines, one per
 * interrupt class.
 * 16 IRQ numbers are reserved for inter processor
 * interruptions, although these are only used in the
 * range of the first node.
 *
 * This scheme needs 128 IRQ numbers per BIF node ID,
 * which means that with the total of 512 lines
 * available, we can have a maximum of four nodes.
 */

enum {
	IIC_EXT_OFFSET   = 0x00, /* Start of south bridge IRQs */
	IIC_NUM_EXT      = 0x40, /* Number of south bridge IRQs */
	IIC_SPE_OFFSET   = 0x40, /* Start of SPE interrupts */
	IIC_CLASS_STRIDE = 0x10, /* SPE IRQs per class    */
	IIC_IPI_OFFSET   = 0x70, /* Start of IPI IRQs */
	IIC_NUM_IPIS     = 0x10, /* IRQs reserved for IPI */
	IIC_NODE_STRIDE  = 0x80, /* Total IRQs per node   */
};

extern void iic_init_IRQ(void);
extern int  iic_get_irq(struct pt_regs *regs);
extern void iic_cause_IPI(int cpu, int mesg);
extern void iic_request_IPIs(void);
extern void iic_setup_cpu(void);
extern void iic_local_enable(void);
extern void iic_local_disable(void);

extern u8 iic_get_target_id(int cpu);

extern void spider_init_IRQ(void);
extern int spider_get_irq(unsigned long int_pending);

#endif
#endif /* ASM_CELL_PIC_H */
