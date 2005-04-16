/*
 * linux/arch/arm/mach-sa1100/lart.c
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"


#warning "include/asm/arch-sa1100/ide.h needs fixing for lart"

static struct map_desc lart_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xe8000000, 0x00000000, 0x00400000, MT_DEVICE }, /* main flash memory */
  { 0xec000000, 0x08000000, 0x00400000, MT_DEVICE }  /* main flash, alternative location */
};

static void __init lart_map_io(void)
{
	sa1100_map_io();
	iotable_init(lart_io_desc, ARRAY_SIZE(lart_io_desc));

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
	sa1100_register_uart(2, 2);

	GAFR |= (GPIO_UART_TXD | GPIO_UART_RXD);
	GPDR |= GPIO_UART_TXD;
	GPDR &= ~GPIO_UART_RXD;
	PPAR |= PPAR_UPR;
}

MACHINE_START(LART, "LART")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	MAPIO(lart_map_io)
	INITIRQ(sa1100_init_irq)
	.timer		= &sa1100_timer,
MACHINE_END
