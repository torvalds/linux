/*
 * bcsr.h -- Db1xxx/Pb1xxx Devboard CPLD registers ("BCSR") abstraction.
 *
 * All Alchemy development boards (except, of course, the weird PB1000)
 * have a few registers in a CPLD with standardised layout; they mostly
 * only differ in base address.
 * All registers are 16bits wide with 32bit spacing.
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/addrspace.h>
#include <asm/io.h>
#include <asm/mach-db1x00/bcsr.h>

static struct bcsr_reg {
	void __iomem *raddr;
	spinlock_t lock;
} bcsr_regs[BCSR_CNT];

void __init bcsr_init(unsigned long bcsr1_phys, unsigned long bcsr2_phys)
{
	int i;

	bcsr1_phys = KSEG1ADDR(CPHYSADDR(bcsr1_phys));
	bcsr2_phys = KSEG1ADDR(CPHYSADDR(bcsr2_phys));

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
