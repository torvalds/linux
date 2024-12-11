/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _I8042_SPARCIO_H
#define _I8042_SPARCIO_H

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/oplib.h>
#include <asm/prom.h>

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

#ifdef CONFIG_PCI

static struct resource *kbd_res;

#define OBP_PS2KBD_NAME1	"kb_ps2"
#define OBP_PS2KBD_NAME2	"keyboard"
#define OBP_PS2MS_NAME1		"kdmouse"
#define OBP_PS2MS_NAME2		"mouse"

static int sparc_i8042_probe(struct platform_device *op)
{
	struct device_node *dp;

	for_each_child_of_node(op->dev.of_node, dp) {
		if (of_node_name_eq(dp, OBP_PS2KBD_NAME1) ||
		    of_node_name_eq(dp, OBP_PS2KBD_NAME2)) {
			struct platform_device *kbd = of_find_device_by_node(dp);
			unsigned int irq = kbd->archdata.irqs[0];
			if (irq == 0xffffffff)
				irq = op->archdata.irqs[0];
			i8042_kbd_irq = irq;
			kbd_iobase = of_ioremap(&kbd->resource[0],
						0, 8, "kbd");
			kbd_res = &kbd->resource[0];
		} else if (of_node_name_eq(dp, OBP_PS2MS_NAME1) ||
			   of_node_name_eq(dp, OBP_PS2MS_NAME2)) {
			struct platform_device *ms = of_find_device_by_node(dp);
			unsigned int irq = ms->archdata.irqs[0];
			if (irq == 0xffffffff)
				irq = op->archdata.irqs[0];
			i8042_aux_irq = irq;
		}
	}

	return 0;
}

static void sparc_i8042_remove(struct platform_device *op)
{
	of_iounmap(kbd_res, kbd_iobase, 8);
}

static const struct of_device_id sparc_i8042_match[] = {
	{
		.name = "8042",
	},
	{},
};
MODULE_DEVICE_TABLE(of, sparc_i8042_match);

static struct platform_driver sparc_i8042_driver = {
	.driver = {
		.name = "i8042",
		.of_match_table = sparc_i8042_match,
	},
	.probe		= sparc_i8042_probe,
	.remove		= sparc_i8042_remove,
};

static bool i8042_is_mr_coffee(void)
{
	struct device_node *root __free(device_node) = of_find_node_by_path("/");
	const char *name = of_get_property(root, "name", NULL);

	return name && !strcmp(name, "SUNW,JavaStation-1");
}

static int __init i8042_platform_init(void)
{
	if (i8042_is_mr_coffee()) {
		/* Hardcoded values for MrCoffee.  */
		i8042_kbd_irq = i8042_aux_irq = 13 | 0x20;
		kbd_iobase = ioremap(0x71300060, 8);
		if (!kbd_iobase)
			return -ENODEV;
	} else {
		int err = platform_driver_register(&sparc_i8042_driver);
		if (err)
			return err;

		if (i8042_kbd_irq == -1 ||
		    i8042_aux_irq == -1) {
			if (kbd_iobase) {
				of_iounmap(kbd_res, kbd_iobase, 8);
				kbd_iobase = (void __iomem *) NULL;
			}
			return -ENODEV;
		}
	}

	i8042_reset = I8042_RESET_ALWAYS;

	return 0;
}

static inline void i8042_platform_exit(void)
{
	if (!i8042_is_mr_coffee())
		platform_driver_unregister(&sparc_i8042_driver);
}

#else /* !CONFIG_PCI */
static int __init i8042_platform_init(void)
{
	return -ENODEV;
}

static inline void i8042_platform_exit(void)
{
}
#endif /* !CONFIG_PCI */

#endif /* _I8042_SPARCIO_H */
