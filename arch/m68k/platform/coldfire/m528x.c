/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/528x/config.c
 *
 *	Sub-architcture dependent initialization code for the Freescale
 *	5280, 5281 and 5282 CPUs.
 *
 *	Copyright (C) 1999-2003, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>
#include <asm/mcfgpio.h>

/***************************************************************************/

struct mcf_gpio_chip mcf_gpio_chips[] = {
	MCFGPS(NQ, 1, 7, MCFEPORT_EPDDR, MCFEPORT_EPDR, MCFEPORT_EPPDR),
	MCFGPS(TA, 8, 4, MCFGPTA_GPTDDR, MCFGPTA_GPTPORT, MCFGPTB_GPTPORT),
	MCFGPS(TB, 16, 4, MCFGPTB_GPTDDR, MCFGPTB_GPTPORT, MCFGPTB_GPTPORT),
	MCFGPS(QA, 24, 4, MCFQADC_DDRQA, MCFQADC_PORTQA, MCFQADC_PORTQA),
	MCFGPS(QB, 32, 4, MCFQADC_DDRQB, MCFQADC_PORTQB, MCFQADC_PORTQB),
	MCFGPF(A, 40, 8),
	MCFGPF(B, 48, 8),
	MCFGPF(C, 56, 8),
	MCFGPF(D, 64, 8),
	MCFGPF(E, 72, 8),
	MCFGPF(F, 80, 8),
	MCFGPF(G, 88, 8),
	MCFGPF(H, 96, 8),
	MCFGPF(J, 104, 8),
	MCFGPF(DD, 112, 8),
	MCFGPF(EH, 120, 8),
	MCFGPF(EL, 128, 8),
	MCFGPF(AS, 136, 6),
	MCFGPF(QS, 144, 7),
	MCFGPF(SD, 152, 6),
	MCFGPF(TC, 160, 4),
	MCFGPF(TD, 168, 4),
	MCFGPF(UA, 176, 4),
};

unsigned int mcf_gpio_chips_size = ARRAY_SIZE(mcf_gpio_chips);

/***************************************************************************/

#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)

static void __init m528x_qspi_init(void)
{
	/* setup Port QS for QSPI with gpio CS control */
	__raw_writeb(0x07, MCFGPIO_PQSPAR);
}

#endif /* IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI) */

/***************************************************************************/

static void __init m528x_uarts_init(void)
{
	u8 port;

	/* make sure PUAPAR is set for UART0 and UART1 */
	port = readb(MCF5282_GPIO_PUAPAR);
	port |= 0x03 | (0x03 << 2);
	writeb(port, MCF5282_GPIO_PUAPAR);
}

/***************************************************************************/

static void __init m528x_fec_init(void)
{
	u16 v16;

	/* Set multi-function pins to ethernet mode for fec0 */
	v16 = readw(MCF_IPSBAR + 0x100056);
	writew(v16 | 0xf00, MCF_IPSBAR + 0x100056);
	writeb(0xc0, MCF_IPSBAR + 0x100058);
}

/***************************************************************************/

#ifdef CONFIG_WILDFIRE
void wildfire_halt(void)
{
	writeb(0, 0x30000007);
	writeb(0x2, 0x30000007);
}
#endif

#ifdef CONFIG_WILDFIREMOD
void wildfiremod_halt(void)
{
	printk(KERN_INFO "WildFireMod hibernating...\n");

	/* Set portE.5 to Digital IO */
	MCF5282_GPIO_PEPAR &= ~(1 << (5 * 2));

	/* Make portE.5 an output */
	MCF5282_GPIO_DDRE |= (1 << 5);

	/* Now toggle portE.5 from low to high */
	MCF5282_GPIO_PORTE &= ~(1 << 5);
	MCF5282_GPIO_PORTE |= (1 << 5);

	printk(KERN_EMERG "Failed to hibernate. Halting!\n");
}
#endif

void __init config_BSP(char *commandp, int size)
{
#ifdef CONFIG_WILDFIRE
	mach_halt = wildfire_halt;
#endif
#ifdef CONFIG_WILDFIREMOD
	mach_halt = wildfiremod_halt;
#endif
	mach_sched_init = hw_timer_init;
	m528x_uarts_init();
	m528x_fec_init();
#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)
	m528x_qspi_init();
#endif
}

/***************************************************************************/
