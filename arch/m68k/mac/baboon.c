/*
 * Baboon Custom IC Management
 *
 * The Baboon custom IC controls the IDE, PCMCIA and media bay on the
 * PowerBook 190. It multiplexes multiple interrupt sources onto the
 * Nubus slot $C interrupt.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/traps.h>
#include <asm/bootinfo.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_baboon.h>

/* #define DEBUG_BABOON */
/* #define DEBUG_IRQS */

int baboon_present,baboon_active;
volatile struct baboon *baboon;

irqreturn_t baboon_irq(int, void *);

#if 0
extern int macide_ack_intr(struct ata_channel *);
#endif

/*
 * Baboon initialization.
 */

void __init baboon_init(void)
{
	if (macintosh_config->ident != MAC_MODEL_PB190) {
		baboon = NULL;
		baboon_present = 0;
		return;
	}

	baboon = (struct baboon *) BABOON_BASE;
	baboon_present = 1;
	baboon_active = 0;

	printk("Baboon detected at %p\n", baboon);
}

/*
 * Register the Baboon interrupt dispatcher on nubus slot $C.
 */

void __init baboon_register_interrupts(void)
{
	request_irq(IRQ_NUBUS_C, baboon_irq, IRQ_FLG_LOCK|IRQ_FLG_FAST,
		    "baboon", (void *) baboon);
}

/*
 * Baboon interrupt handler. This works a lot like a VIA.
 */

irqreturn_t baboon_irq(int irq, void *dev_id)
{
	int irq_bit,i;
	unsigned char events;

#ifdef DEBUG_IRQS
	printk("baboon_irq: mb_control %02X mb_ifr %02X mb_status %02X active %02X\n",
		(uint) baboon->mb_control, (uint) baboon->mb_ifr,
		(uint) baboon->mb_status,  baboon_active);
#endif

	if (!(events = baboon->mb_ifr & 0x07))
		return IRQ_NONE;

	for (i = 0, irq_bit = 1 ; i < 3 ; i++, irq_bit <<= 1) {
	        if (events & irq_bit/* & baboon_active*/) {
			baboon_active &= ~irq_bit;
			m68k_handle_int(IRQ_BABOON_0 + i);
			baboon_active |= irq_bit;
			baboon->mb_ifr &= ~irq_bit;
		}
	}
#if 0
	if (baboon->mb_ifr & 0x02) macide_ack_intr(NULL);
	/* for now we need to smash all interrupts */
	baboon->mb_ifr &= ~events;
#endif
	return IRQ_HANDLED;
}

void baboon_irq_enable(int irq) {
	int irq_idx	= IRQ_IDX(irq);

#ifdef DEBUG_IRQUSE
	printk("baboon_irq_enable(%d)\n", irq);
#endif
	baboon_active |= (1 << irq_idx);
}

void baboon_irq_disable(int irq) {
	int irq_idx	= IRQ_IDX(irq);

#ifdef DEBUG_IRQUSE
	printk("baboon_irq_disable(%d)\n", irq);
#endif
	baboon_active &= ~(1 << irq_idx);
}

void baboon_irq_clear(int irq) {
	int irq_idx	= IRQ_IDX(irq);

	baboon->mb_ifr &= ~(1 << irq_idx);
}

int baboon_irq_pending(int irq)
{
	int irq_idx	= IRQ_IDX(irq);

	return baboon->mb_ifr & (1 << irq_idx);
}
