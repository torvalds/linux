/*
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Copyright 2002 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              stevel@mvista.com or source@mvista.com
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/delay.h>

#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

#include <asm/mach-rc32434/irq.h>
#include <asm/mach-rc32434/gpio.h>

struct intr_group {
	u32 mask;	/* mask of valid bits in pending/mask registers */
	volatile u32 *base_addr;
};

#define RC32434_NR_IRQS  (GROUP4_IRQ_BASE + 32)

#if (NR_IRQS < RC32434_NR_IRQS)
#error Too little irqs defined. Did you override <asm/irq.h> ?
#endif

static const struct intr_group intr_group[NUM_INTR_GROUPS] = {
	{
		.mask	= 0x0000efff,
		.base_addr = (u32 *) KSEG1ADDR(IC_GROUP0_PEND + 0 * IC_GROUP_OFFSET)},
	{
		.mask	= 0x00001fff,
		.base_addr = (u32 *) KSEG1ADDR(IC_GROUP0_PEND + 1 * IC_GROUP_OFFSET)},
	{
		.mask	= 0x00000007,
		.base_addr = (u32 *) KSEG1ADDR(IC_GROUP0_PEND + 2 * IC_GROUP_OFFSET)},
	{
		.mask	= 0x0003ffff,
		.base_addr = (u32 *) KSEG1ADDR(IC_GROUP0_PEND + 3 * IC_GROUP_OFFSET)},
	{
		.mask	= 0xffffffff,
		.base_addr = (u32 *) KSEG1ADDR(IC_GROUP0_PEND + 4 * IC_GROUP_OFFSET)}
};

#define READ_PEND(base) (*(base))
#define READ_MASK(base) (*(base + 2))
#define WRITE_MASK(base, val) (*(base + 2) = (val))

static inline int irq_to_group(unsigned int irq_nr)
{
	return (irq_nr - GROUP0_IRQ_BASE) >> 5;
}

static inline int group_to_ip(unsigned int group)
{
	return group + 2;
}

static inline void enable_local_irq(unsigned int ip)
{
	int ipnum = 0x100 << ip;

	set_c0_status(ipnum);
}

static inline void disable_local_irq(unsigned int ip)
{
	int ipnum = 0x100 << ip;

	clear_c0_status(ipnum);
}

static inline void ack_local_irq(unsigned int ip)
{
	int ipnum = 0x100 << ip;

	clear_c0_cause(ipnum);
}

static void rb532_enable_irq(unsigned int irq_nr)
{
	int ip = irq_nr - GROUP0_IRQ_BASE;
	unsigned int group, intr_bit;
	volatile unsigned int *addr;

	if (ip < 0)
		enable_local_irq(irq_nr);
	else {
		group = ip >> 5;

		ip &= (1 << 5) - 1;
		intr_bit = 1 << ip;

		enable_local_irq(group_to_ip(group));

		addr = intr_group[group].base_addr;
		WRITE_MASK(addr, READ_MASK(addr) & ~intr_bit);
	}
}

static void rb532_disable_irq(unsigned int irq_nr)
{
	int ip = irq_nr - GROUP0_IRQ_BASE;
	unsigned int group, intr_bit, mask;
	volatile unsigned int *addr;

	if (ip < 0) {
		disable_local_irq(irq_nr);
	} else {
		group = ip >> 5;

		ip &= (1 << 5) - 1;
		intr_bit = 1 << ip;
		addr = intr_group[group].base_addr;
		mask = READ_MASK(addr);
		mask |= intr_bit;
		WRITE_MASK(addr, mask);

		/* There is a maximum of 14 GPIO interrupts */
		if (group == GPIO_MAPPED_IRQ_GROUP && irq_nr <= (GROUP4_IRQ_BASE + 13))
			rb532_gpio_set_istat(0, irq_nr - GPIO_MAPPED_IRQ_BASE);

		/*
		 * if there are no more interrupts enabled in this
		 * group, disable corresponding IP
		 */
		if (mask == intr_group[group].mask)
			disable_local_irq(group_to_ip(group));
	}
}

static void rb532_mask_and_ack_irq(unsigned int irq_nr)
{
	rb532_disable_irq(irq_nr);
	ack_local_irq(group_to_ip(irq_to_group(irq_nr)));
}

static int rb532_set_type(unsigned int irq_nr, unsigned type)
{
	int gpio = irq_nr - GPIO_MAPPED_IRQ_BASE;
	int group = irq_to_group(irq_nr);

	if (group != GPIO_MAPPED_IRQ_GROUP || irq_nr > (GROUP4_IRQ_BASE + 13))
		return (type == IRQ_TYPE_LEVEL_HIGH) ? 0 : -EINVAL;

	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
		rb532_gpio_set_ilevel(1, gpio);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		rb532_gpio_set_ilevel(0, gpio);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct irq_chip rc32434_irq_type = {
	.name		= "RB532",
	.ack		= rb532_disable_irq,
	.mask		= rb532_disable_irq,
	.mask_ack	= rb532_mask_and_ack_irq,
	.unmask		= rb532_enable_irq,
	.set_type	= rb532_set_type,
};

void __init arch_init_irq(void)
{
	int i;

	pr_info("Initializing IRQ's: %d out of %d\n", RC32434_NR_IRQS, NR_IRQS);

	for (i = 0; i < RC32434_NR_IRQS; i++)
		set_irq_chip_and_handler(i,  &rc32434_irq_type,
					handle_level_irq);
}

/* Main Interrupt dispatcher */
asmlinkage void plat_irq_dispatch(void)
{
	unsigned int ip, pend, group;
	volatile unsigned int *addr;
	unsigned int cp0_cause = read_c0_cause() & read_c0_status();

	if (cp0_cause & CAUSEF_IP7) {
		do_IRQ(7);
	} else {
		ip = (cp0_cause & 0x7c00);
		if (ip) {
			group = 21 + (fls(ip) - 32);

			addr = intr_group[group].base_addr;

			pend = READ_PEND(addr);
			pend &= ~READ_MASK(addr);	/* only unmasked interrupts */
			pend = 39 + (fls(pend) - 32);
			do_IRQ((group << 5) + pend);
		}
	}
}
