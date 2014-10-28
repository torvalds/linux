#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/serial_8250.h>
#include <asm/bootinfo.h>

#include "devices.h"
#include "ar5312.h"
#include "ar2315.h"

const char *get_system_type(void)
{
	return "Atheros (unknown)";
}

void __init ath25_serial_setup(u32 mapbase, int irq, unsigned int uartclk)
{
	struct uart_port s;

	memset(&s, 0, sizeof(s));

	s.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP;
	s.iotype = UPIO_MEM32;
	s.irq = irq;
	s.regshift = 2;
	s.mapbase = mapbase;
	s.uartclk = uartclk;

	early_serial_setup(&s);
}

static int __init ath25_arch_init(void)
{
	if (is_ar5312())
		ar5312_arch_init();
	else
		ar2315_arch_init();

	return 0;
}

arch_initcall(ath25_arch_init);
