// SPDX-License-Identifier: GPL-2.0
#include <linux/ioport.h>
#include <asm/io.h>

#include "pc873xx.h"

static unsigned pc873xx_probelist[] = {0x398, 0x26e, 0};

static char *pc873xx_names[] = {
	"PC87303", "PC87306", "PC87312", "PC87332", "PC87334"
};

static unsigned int base, model;


unsigned int __init pc873xx_get_base()
{
	return base;
}

char *__init pc873xx_get_model()
{
	return pc873xx_names[model];
}

static unsigned char __init pc873xx_read(unsigned int base, int reg)
{
	outb(reg, base);
	return inb(base + 1);
}

static void __init pc873xx_write(unsigned int base, int reg, unsigned char data)
{
	unsigned long flags;

	local_irq_save(flags);
	outb(reg, base);
	outb(data, base + 1);
	outb(data, base + 1);		/* Must be written twice */
	local_irq_restore(flags);
}

int __init pc873xx_probe(void)
{
	int val, index = 0;

	while ((base = pc873xx_probelist[index++])) {

		if (request_region(base, 2, "Super IO PC873xx") == NULL)
			continue;

		val = pc873xx_read(base, REG_SID);
		if ((val & 0xf0) == 0x10) {
			model = PC87332;
			break;
		} else if ((val & 0xf8) == 0x70) {
			model = PC87306;
			break;
		} else if ((val & 0xf8) == 0x50) {
			model = PC87334;
			break;
		} else if ((val & 0xf8) == 0x40) {
			model = PC87303;
			break;
		}

		release_region(base, 2);
	}

	return (base == 0) ? -1 : 1;
}

void __init pc873xx_enable_epp19(void)
{
	unsigned char data;

	printk(KERN_INFO "PC873xx enabling EPP v1.9\n");
	data = pc873xx_read(base, REG_PCR);
	pc873xx_write(base, REG_PCR, (data & 0xFC) | 0x02);
}

void __init pc873xx_enable_ide(void)
{
	unsigned char data;

	printk(KERN_INFO "PC873xx enabling IDE interrupt\n");
	data = pc873xx_read(base, REG_FER);
	pc873xx_write(base, REG_FER, data | 0x40);
}
