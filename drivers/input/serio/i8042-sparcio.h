#ifndef _I8042_SPARCIO_H
#define _I8042_SPARCIO_H

#include <linux/config.h>
#include <asm/io.h>

#ifdef CONFIG_PCI
#include <asm/oplib.h>
#include <asm/ebus.h>
#endif

static int i8042_kbd_irq = -1;
static int i8042_aux_irq = -1;
#define I8042_KBD_IRQ i8042_kbd_irq
#define I8042_AUX_IRQ i8042_aux_irq

#define I8042_KBD_PHYS_DESC "sparcps2/serio0"
#define I8042_AUX_PHYS_DESC "sparcps2/serio1"
#define I8042_MUX_PHYS_DESC "sparcps2/serio%d"

static void __iomem *kbd_iobase;

#define I8042_COMMAND_REG	(kbd_iobase + 0x64UL)
#define I8042_DATA_REG		(kbd_iobase + 0x60UL)

static inline int i8042_read_data(void)
{
	return readb(kbd_iobase + 0x60UL);
}

static inline int i8042_read_status(void)
{
	return readb(kbd_iobase + 0x64UL);
}

static inline void i8042_write_data(int val)
{
	writeb(val, kbd_iobase + 0x60UL);
}

static inline void i8042_write_command(int val)
{
	writeb(val, kbd_iobase + 0x64UL);
}

#define OBP_PS2KBD_NAME1	"kb_ps2"
#define OBP_PS2KBD_NAME2	"keyboard"
#define OBP_PS2MS_NAME1		"kdmouse"
#define OBP_PS2MS_NAME2		"mouse"

static int __init i8042_platform_init(void)
{
#ifndef CONFIG_PCI
	return -ENODEV;
#else
	char prop[128];
	int len;

	len = prom_getproperty(prom_root_node, "name", prop, sizeof(prop));
	if (len < 0) {
		printk("i8042: Cannot get name property of root OBP node.\n");
		return -ENODEV;
	}
	if (strncmp(prop, "SUNW,JavaStation-1", len) == 0) {
		/* Hardcoded values for MrCoffee.  */
		i8042_kbd_irq = i8042_aux_irq = 13 | 0x20;
		kbd_iobase = ioremap(0x71300060, 8);
		if (!kbd_iobase)
			return -ENODEV;
	} else {
		struct linux_ebus *ebus;
		struct linux_ebus_device *edev;
		struct linux_ebus_child *child;

		for_each_ebus(ebus) {
			for_each_ebusdev(edev, ebus) {
				if (!strcmp(edev->prom_name, "8042"))
					goto edev_found;
			}
		}
		return -ENODEV;

	edev_found:
		for_each_edevchild(edev, child) {
			if (!strcmp(child->prom_name, OBP_PS2KBD_NAME1) ||
			    !strcmp(child->prom_name, OBP_PS2KBD_NAME2)) {
				i8042_kbd_irq = child->irqs[0];
				kbd_iobase =
					ioremap(child->resource[0].start, 8);
			}
			if (!strcmp(child->prom_name, OBP_PS2MS_NAME1) ||
			    !strcmp(child->prom_name, OBP_PS2MS_NAME2))
				i8042_aux_irq = child->irqs[0];
		}
		if (i8042_kbd_irq == -1 ||
		    i8042_aux_irq == -1) {
			printk("i8042: Error, 8042 device lacks both kbd and "
			       "mouse nodes.\n");
			return -ENODEV;
		}
	}

	i8042_reset = 1;

	return 0;
#endif /* CONFIG_PCI */
}

static inline void i8042_platform_exit(void)
{
#ifdef CONFIG_PCI
	iounmap(kbd_iobase);
#endif
}

#endif /* _I8042_SPARCIO_H */
