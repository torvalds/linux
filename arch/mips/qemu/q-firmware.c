#include <linux/init.h>
#include <linux/string.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/io.h>

#define QEMU_PORT_BASE 0xb4000000

void __init prom_init(void)
{
	int *cmdline;

	cmdline = (int *) (CKSEG0 + (0x10 << 20) - 260);
	if (*cmdline == 0x12345678) {
		if (*(char *)(cmdline + 1))
			strcpy(arcs_cmdline, (char *)(cmdline + 1));
		add_memory_region(0x0<<20, cmdline[-1], BOOT_MEM_RAM);
	} else {
		add_memory_region(0x0<<20, 0x10<<20, BOOT_MEM_RAM);
	}


	set_io_port_base(QEMU_PORT_BASE);
}
