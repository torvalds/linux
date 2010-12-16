#include <linux/ioport.h>
#include <asm/e820.h>

void arch_remove_reservations(struct resource *avail)
{
	/* Trim out BIOS area (low 1MB) */
	if (avail->flags & IORESOURCE_MEM) {
		if (avail->start < BIOS_END)
			avail->start = BIOS_END;
	}
}
