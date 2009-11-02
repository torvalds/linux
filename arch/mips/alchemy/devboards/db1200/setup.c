/*
 * Alchemy/AMD/RMI DB1200 board setup.
 *
 * Licensed under the terms outlined in the file COPYING in the root of
 * this source archive.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-db1x00/bcsr.h>
#include <asm/mach-db1x00/db1200.h>
#include <asm/processor.h>
#include <asm/reboot.h>

const char *get_system_type(void)
{
	return "Alchemy Db1200";
}

static void board_power_off(void)
{
	bcsr_write(BCSR_RESETS, 0);
	bcsr_write(BCSR_SYSTEM, BCSR_SYSTEM_PWROFF | BCSR_SYSTEM_RESET);
}

void board_reset(void)
{
	bcsr_write(BCSR_RESETS, 0);
	bcsr_write(BCSR_SYSTEM, 0);
}

void __init board_setup(void)
{
	unsigned long freq0, clksrc, div, pfc;
	unsigned short whoami;

	bcsr_init(DB1200_BCSR_PHYS_ADDR,
		  DB1200_BCSR_PHYS_ADDR + DB1200_BCSR_HEXLED_OFS);

	whoami = bcsr_read(BCSR_WHOAMI);
	printk(KERN_INFO "Alchemy/AMD/RMI DB1200 Board, CPLD Rev %d"
		"  Board-ID %d  Daughtercard ID %d\n",
		(whoami >> 4) & 0xf, (whoami >> 8) & 0xf, whoami & 0xf);

	/* SMBus/SPI on PSC0, Audio on PSC1 */
	pfc = __raw_readl((void __iomem *)SYS_PINFUNC);
	pfc &= ~(SYS_PINFUNC_P0A | SYS_PINFUNC_P0B);
	pfc &= ~(SYS_PINFUNC_P1A | SYS_PINFUNC_P1B | SYS_PINFUNC_FS3);
	pfc |= SYS_PINFUNC_P1C;	/* SPI is configured later */
	__raw_writel(pfc, (void __iomem *)SYS_PINFUNC);
	wmb();

	/* Clock configurations: PSC0: ~50MHz via Clkgen0, derived from
	 * CPU clock; all other clock generators off/unused.
	 */
	div = (get_au1x00_speed() + 25000000) / 50000000;
	if (div & 1)
		div++;
	div = ((div >> 1) - 1) & 0xff;

	freq0 = div << SYS_FC_FRDIV0_BIT;
	__raw_writel(freq0, (void __iomem *)SYS_FREQCTRL0);
	wmb();
	freq0 |= SYS_FC_FE0;	/* enable F0 */
	__raw_writel(freq0, (void __iomem *)SYS_FREQCTRL0);
	wmb();

	/* psc0_intclk comes 1:1 from F0 */
	clksrc = SYS_CS_MUX_FQ0 << SYS_CS_ME0_BIT;
	__raw_writel(clksrc, (void __iomem *)SYS_CLKSRC);
	wmb();

	pm_power_off = board_power_off;
	_machine_halt = board_power_off;
	_machine_restart = (void(*)(char *))board_reset;
}

/* use the hexleds to count the number of times the cpu has entered
 * wait, the dots to indicate whether the CPU is currently idle or
 * active (dots off = sleeping, dots on = working) for cases where
 * the number doesn't change for a long(er) period of time.
 */
static void db1200_wait(void)
{
	__asm__("	.set	push			\n"
		"	.set	mips3			\n"
		"	.set	noreorder		\n"
		"	cache	0x14, 0(%0)		\n"
		"	cache	0x14, 32(%0)		\n"
		"	cache	0x14, 64(%0)		\n"
		/* dots off: we're about to call wait */
		"	lui	$26, 0xb980		\n"
		"	ori	$27, $0, 3		\n"
		"	sb	$27, 0x18($26)		\n"
		"	sync				\n"
		"	nop				\n"
		"	wait				\n"
		"	nop				\n"
		"	nop				\n"
		"	nop				\n"
		"	nop				\n"
		"	nop				\n"
		/* dots on: there's work to do, increment cntr */
		"	lui	$26, 0xb980		\n"
		"	sb	$0, 0x18($26)		\n"
		"	lui	$26, 0xb9c0		\n"
		"	lb	$27, 0($26)		\n"
		"	addiu	$27, $27, 1		\n"
		"	sb	$27, 0($26)		\n"
		"	sync				\n"
		"	.set	pop			\n"
		: : "r" (db1200_wait));
}

static int __init db1200_arch_init(void)
{
	/* GPIO7 is low-level triggered CPLD cascade */
	set_irq_type(AU1200_GPIO7_INT, IRQF_TRIGGER_LOW);
	bcsr_init_irq(DB1200_INT_BEGIN, DB1200_INT_END, AU1200_GPIO7_INT);

	/* do not autoenable these: CPLD has broken edge int handling,
	 * and the CD handler setup requires manual enabling to work
	 * around that.
	 */
	irq_to_desc(DB1200_SD0_INSERT_INT)->status |= IRQ_NOAUTOEN;
	irq_to_desc(DB1200_SD0_EJECT_INT)->status |= IRQ_NOAUTOEN;

	if (cpu_wait)
		cpu_wait = db1200_wait;

	return 0;
}
arch_initcall(db1200_arch_init);
