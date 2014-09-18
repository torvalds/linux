/*
 * bcsr.h -- Db1xxx/Pb1xxx Devboard CPLD registers ("BCSR") abstraction.
 *
 * All Alchemy development boards (except, of course, the weird PB1000)
 * have a few registers in a CPLD with standardised layout; they mostly
 * only differ in base address.
 * All registers are 16bits wide with 32bit spacing.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <asm/addrspace.h>
#include <asm/io.h>
#include <asm/mach-db1x00/bcsr.h>

static struct bcsr_reg {
	void __iomem *raddr;
	spinlock_t lock;
} bcsr_regs[BCSR_CNT];

static void __iomem *bcsr_virt; /* KSEG1 addr of BCSR base */
static int bcsr_csc_base;	/* linux-irq of first cascaded irq */

void __init bcsr_init(unsigned long bcsr1_phys, unsigned long bcsr2_phys)
{
	int i;

	bcsr1_phys = KSEG1ADDR(CPHYSADDR(bcsr1_phys));
	bcsr2_phys = KSEG1ADDR(CPHYSADDR(bcsr2_phys));

	bcsr_virt = (void __iomem *)bcsr1_phys;

	for (i = 0; i < BCSR_CNT; i++) {
		if (i >= BCSR_HEXLEDS)
			bcsr_regs[i].raddr = (void __iomem *)bcsr2_phys +
					(0x04 * (i - BCSR_HEXLEDS));
		else
			bcsr_regs[i].raddr = (void __iomem *)bcsr1_phys +
					(0x04 * i);

		spin_lock_init(&bcsr_regs[i].lock);
	}
}

unsigned short bcsr_read(enum bcsr_id reg)
{
	unsigned short r;
	unsigned long flags;

	spin_lock_irqsave(&bcsr_regs[reg].lock, flags);
	r = __raw_readw(bcsr_regs[reg].raddr);
	spin_unlock_irqrestore(&bcsr_regs[reg].lock, flags);
	return r;
}
EXPORT_SYMBOL_GPL(bcsr_read);

void bcsr_write(enum bcsr_id reg, unsigned short val)
{
	unsigned long flags;

	spin_lock_irqsave(&bcsr_regs[reg].lock, flags);
	__raw_writew(val, bcsr_regs[reg].raddr);
	wmb();
	spin_unlock_irqrestore(&bcsr_regs[reg].lock, flags);
}
EXPORT_SYMBOL_GPL(bcsr_write);

void bcsr_mod(enum bcsr_id reg, unsigned short clr, unsigned short set)
{
	unsigned short r;
	unsigned long flags;

	spin_lock_irqsave(&bcsr_regs[reg].lock, flags);
	r = __raw_readw(bcsr_regs[reg].raddr);
	r &= ~clr;
	r |= set;
	__raw_writew(r, bcsr_regs[reg].raddr);
	wmb();
	spin_unlock_irqrestore(&bcsr_regs[reg].lock, flags);
}
EXPORT_SYMBOL_GPL(bcsr_mod);

/*
 * DB1200/PB1200 CPLD IRQ muxer
 */
static void bcsr_csc_handler(unsigned int irq, struct irq_desc *d)
{
	unsigned short bisr = __raw_readw(bcsr_virt + BCSR_REG_INTSTAT);

	disable_irq_nosync(irq);
	generic_handle_irq(bcsr_csc_base + __ffs(bisr));
	enable_irq(irq);
}

static void bcsr_irq_mask(struct irq_data *d)
{
	unsigned short v = 1 << (d->irq - bcsr_csc_base);
	__raw_writew(v, bcsr_virt + BCSR_REG_MASKCLR);
	wmb();
}

static void bcsr_irq_maskack(struct irq_data *d)
{
	unsigned short v = 1 << (d->irq - bcsr_csc_base);
	__raw_writew(v, bcsr_virt + BCSR_REG_MASKCLR);
	__raw_writew(v, bcsr_virt + BCSR_REG_INTSTAT);	/* ack */
	wmb();
}

static void bcsr_irq_unmask(struct irq_data *d)
{
	unsigned short v = 1 << (d->irq - bcsr_csc_base);
	__raw_writew(v, bcsr_virt + BCSR_REG_MASKSET);
	wmb();
}

static struct irq_chip bcsr_irq_type = {
	.name		= "CPLD",
	.irq_mask	= bcsr_irq_mask,
	.irq_mask_ack	= bcsr_irq_maskack,
	.irq_unmask	= bcsr_irq_unmask,
};

void __init bcsr_init_irq(int csc_start, int csc_end, int hook_irq)
{
	unsigned int irq;

	/* mask & enable & ack all */
	__raw_writew(0xffff, bcsr_virt + BCSR_REG_MASKCLR);
	__raw_writew(0xffff, bcsr_virt + BCSR_REG_INTSET);
	__raw_writew(0xffff, bcsr_virt + BCSR_REG_INTSTAT);
	wmb();

	bcsr_csc_base = csc_start;

	for (irq = csc_start; irq <= csc_end; irq++)
		irq_set_chip_and_handler_name(irq, &bcsr_irq_type,
					      handle_level_irq, "level");

	irq_set_chained_handler(hook_irq, bcsr_csc_handler);
}
