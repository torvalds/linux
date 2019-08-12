/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _I8042_IO_H
#define _I8042_IO_H


/*
 * Names.
 */

#define I8042_KBD_PHYS_DESC "isa0060/serio0"
#define I8042_AUX_PHYS_DESC "isa0060/serio1"
#define I8042_MUX_PHYS_DESC "isa0060/serio%d"

/*
 * IRQs.
 */

#ifdef __alpha__
# define I8042_KBD_IRQ	1
# define I8042_AUX_IRQ	(RTC_PORT(0) == 0x170 ? 9 : 12)	/* Jensen is special */
#elif defined(__arm__)
/* defined in include/asm-arm/arch-xxx/irqs.h */
#include <asm/irq.h>
#elif defined(CONFIG_SH_CAYMAN)
#include <asm/irq.h>
#elif defined(CONFIG_PPC)
extern int of_i8042_kbd_irq;
extern int of_i8042_aux_irq;
# define I8042_KBD_IRQ  of_i8042_kbd_irq
# define I8042_AUX_IRQ  of_i8042_aux_irq
#else
# define I8042_KBD_IRQ	1
# define I8042_AUX_IRQ	12
#endif


/*
 * Register numbers.
 */

#define I8042_COMMAND_REG	0x64
#define I8042_STATUS_REG	0x64
#define I8042_DATA_REG		0x60

static inline int i8042_read_data(void)
{
	return inb(I8042_DATA_REG);
}

static inline int i8042_read_status(void)
{
	return inb(I8042_STATUS_REG);
}

static inline void i8042_write_data(int val)
{
	outb(val, I8042_DATA_REG);
}

static inline void i8042_write_command(int val)
{
	outb(val, I8042_COMMAND_REG);
}

static inline int i8042_platform_init(void)
{
/*
 * On some platforms touching the i8042 data register region can do really
 * bad things. Because of this the region is always reserved on such boxes.
 */
#if defined(CONFIG_PPC)
	if (check_legacy_ioport(I8042_DATA_REG))
		return -ENODEV;
#endif
#if !defined(__sh__) && !defined(__alpha__)
	if (!request_region(I8042_DATA_REG, 16, "i8042"))
		return -EBUSY;
#endif

	i8042_reset = I8042_RESET_ALWAYS;
	return 0;
}

static inline void i8042_platform_exit(void)
{
#if !defined(__sh__) && !defined(__alpha__)
	release_region(I8042_DATA_REG, 16);
#endif
}

#endif /* _I8042_IO_H */
