/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/types.h>

#include <asm/barrier.h>

static DECLARE_BITMAP(irq_map, NR_IRQS);

int allocate_irqno(void)
{
	int irq;

again:
	irq = find_first_zero_bit(irq_map, NR_IRQS);

	if (irq >= NR_IRQS)
		return -ENOSPC;

	if (test_and_set_bit(irq, irq_map))
		goto again;

	return irq;
}

/*
 * Allocate the 16 legacy interrupts for i8259 devices.	 This happens early
 * in the kernel initialization so treating allocation failure as BUG() is
 * ok.
 */
void __init alloc_legacy_irqno(void)
{
	int i;

	for (i = 0; i <= 16; i++)
		BUG_ON(test_and_set_bit(i, irq_map));
}

void free_irqno(unsigned int irq)
{
	smp_mb__before_atomic();
	clear_bit(irq, irq_map);
	smp_mb__after_atomic();
}
