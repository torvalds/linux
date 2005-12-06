#include <linux/init.h>
#include <asm/io.h>
#include <asm/time.h>

#define QEMU_PORT_BASE 0xb4000000

const char *get_system_type(void)
{
	return "Qemu";
}

static void __init qemu_timer_setup(struct irqaction *irq)
{
	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	setup_irq(0, irq);
}

void __init plat_setup(void)
{
	set_io_port_base(QEMU_PORT_BASE);
	board_timer_setup = qemu_timer_setup;
}
