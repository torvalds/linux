#include <linux/init.h>
#include <asm/x86_init.h>

unsigned int xen_io_apic_read(unsigned apic, unsigned reg)
{
	if (reg == 0x1)
		return 0x00170020;
	else if (reg == 0x0)
		return apic << 24;

	return 0xfd;
}

void __init xen_init_apic(void)
{
	x86_io_apic_ops.read = xen_io_apic_read;
}
