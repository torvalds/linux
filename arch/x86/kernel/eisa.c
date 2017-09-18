/*
 * EISA specific code
 *
 * This file is licensed under the GPL V2
 */
#include <linux/ioport.h>
#include <linux/eisa.h>
#include <linux/io.h>

static __init int eisa_bus_probe(void)
{
	void __iomem *p = ioremap(0x0FFFD9, 4);

	if (readl(p) == 'E' + ('I'<<8) + ('S'<<16) + ('A'<<24))
		EISA_bus = 1;
	iounmap(p);
	return 0;
}
subsys_initcall(eisa_bus_probe);
