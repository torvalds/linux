/*
 * Allocator for I/O pins. All pins are allocated to GPIO at bootup.
 * Unassigned pins and GPIO pins can be allocated to a fixed interface
 * or the I/O processor instead.
 *
 * Copyright (c) 2004-2007 Axis Communications AB.
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <hwregs/reg_map.h>
#include <hwregs/reg_rdwr.h>
#include <pinmux.h>
#include <hwregs/pinmux_defs.h>

#undef DEBUG

#define PORT_PINS 18
#define PORTS 4

static char pins[PORTS][PORT_PINS];
static DEFINE_SPINLOCK(pinmux_lock);

static void crisv32_pinmux_set(int port);

int crisv32_pinmux_init(void)
{
	static int initialized;

	if (!initialized) {
		reg_pinmux_rw_pa pa = REG_RD(pinmux, regi_pinmux, rw_pa);
		initialized = 1;
		REG_WR_INT(pinmux, regi_pinmux, rw_hwprot, 0);
		pa.pa0 = pa.pa1 = pa.pa2 = pa.pa3 =
		    pa.pa4 = pa.pa5 = pa.pa6 = pa.pa7 = regk_pinmux_yes;
		REG_WR(pinmux, regi_pinmux, rw_pa, pa);
		crisv32_pinmux_alloc(PORT_B, 0, PORT_PINS - 1, pinmux_gpio);
		crisv32_pinmux_alloc(PORT_C, 0, PORT_PINS - 1, pinmux_gpio);
		crisv32_pinmux_alloc(PORT_D, 0, PORT_PINS - 1, pinmux_gpio);
		crisv32_pinmux_alloc(PORT_E, 0, PORT_PINS - 1, pinmux_gpio);
	}

	return 0;
}

int
crisv32_pinmux_alloc(int port, int first_pin, int last_pin, enum pin_mode mode)
{
	int i;
	unsigned long flags;

	crisv32_pinmux_init();

	if (port > PORTS || port < 0)
		return -EINVAL;

	spin_lock_irqsave(&pinmux_lock, flags);

	for (i = first_pin; i <= last_pin; i++) {
		if ((pins[port][i] != pinmux_none)
		    && (pins[port][i] != pinmux_gpio)
		    && (pins[port][i] != mode)) {
			spin_unlock_irqrestore(&pinmux_lock, flags);
#ifdef DEBUG
			panic("Pinmux alloc failed!\n");
#endif
			return -EPERM;
		}
	}

	for (i = first_pin; i <= last_pin; i++)
		pins[port][i] = mode;

	crisv32_pinmux_set(port);

	spin_unlock_irqrestore(&pinmux_lock, flags);

	return 0;
}

int crisv32_pinmux_alloc_fixed(enum fixed_function function)
{
	int ret = -EINVAL;
	char saved[sizeof pins];
	unsigned long flags;

	spin_lock_irqsave(&pinmux_lock, flags);

	/* Save internal data for recovery */
	memcpy(saved, pins, sizeof pins);

	crisv32_pinmux_init();	/* Must be done before we read rw_hwprot */

	reg_pinmux_rw_hwprot hwprot = REG_RD(pinmux, regi_pinmux, rw_hwprot);

	switch (function) {
	case pinmux_ser1:
		ret = crisv32_pinmux_alloc(PORT_C, 4, 7, pinmux_fixed);
		hwprot.ser1 = regk_pinmux_yes;
		break;
	case pinmux_ser2:
		ret = crisv32_pinmux_alloc(PORT_C, 8, 11, pinmux_fixed);
		hwprot.ser2 = regk_pinmux_yes;
		break;
	case pinmux_ser3:
		ret = crisv32_pinmux_alloc(PORT_C, 12, 15, pinmux_fixed);
		hwprot.ser3 = regk_pinmux_yes;
		break;
	case pinmux_sser0:
		ret = crisv32_pinmux_alloc(PORT_C, 0, 3, pinmux_fixed);
		ret |= crisv32_pinmux_alloc(PORT_C, 16, 16, pinmux_fixed);
		hwprot.sser0 = regk_pinmux_yes;
		break;
	case pinmux_sser1:
		ret = crisv32_pinmux_alloc(PORT_D, 0, 4, pinmux_fixed);
		hwprot.sser1 = regk_pinmux_yes;
		break;
	case pinmux_ata0:
		ret = crisv32_pinmux_alloc(PORT_D, 5, 7, pinmux_fixed);
		ret |= crisv32_pinmux_alloc(PORT_D, 15, 17, pinmux_fixed);
		hwprot.ata0 = regk_pinmux_yes;
		break;
	case pinmux_ata1:
		ret = crisv32_pinmux_alloc(PORT_D, 0, 4, pinmux_fixed);
		ret |= crisv32_pinmux_alloc(PORT_E, 17, 17, pinmux_fixed);
		hwprot.ata1 = regk_pinmux_yes;
		break;
	case pinmux_ata2:
		ret = crisv32_pinmux_alloc(PORT_C, 11, 15, pinmux_fixed);
		ret |= crisv32_pinmux_alloc(PORT_E, 3, 3, pinmux_fixed);
		hwprot.ata2 = regk_pinmux_yes;
		break;
	case pinmux_ata3:
		ret = crisv32_pinmux_alloc(PORT_C, 8, 10, pinmux_fixed);
		ret |= crisv32_pinmux_alloc(PORT_C, 0, 2, pinmux_fixed);
		hwprot.ata2 = regk_pinmux_yes;
		break;
	case pinmux_ata:
		ret = crisv32_pinmux_alloc(PORT_B, 0, 15, pinmux_fixed);
		ret |= crisv32_pinmux_alloc(PORT_D, 8, 15, pinmux_fixed);
		hwprot.ata = regk_pinmux_yes;
		break;
	case pinmux_eth1:
		ret = crisv32_pinmux_alloc(PORT_E, 0, 17, pinmux_fixed);
		hwprot.eth1 = regk_pinmux_yes;
		hwprot.eth1_mgm = regk_pinmux_yes;
		break;
	case pinmux_timer:
		ret = crisv32_pinmux_alloc(PORT_C, 16, 16, pinmux_fixed);
		hwprot.timer = regk_pinmux_yes;
		spin_unlock_irqrestore(&pinmux_lock, flags);
		return ret;
	}

	if (!ret)
		REG_WR(pinmux, regi_pinmux, rw_hwprot, hwprot);
	else
		memcpy(pins, saved, sizeof pins);

	spin_unlock_irqrestore(&pinmux_lock, flags);

	return ret;
}

void crisv32_pinmux_set(int port)
{
	int i;
	int gpio_val = 0;
	int iop_val = 0;

	for (i = 0; i < PORT_PINS; i++) {
		if (pins[port][i] == pinmux_gpio)
			gpio_val |= (1 << i);
		else if (pins[port][i] == pinmux_iop)
			iop_val |= (1 << i);
	}

	REG_WRITE(int, regi_pinmux + REG_RD_ADDR_pinmux_rw_pb_gio + 8 * port,
		  gpio_val);
	REG_WRITE(int, regi_pinmux + REG_RD_ADDR_pinmux_rw_pb_iop + 8 * port,
		  iop_val);

#ifdef DEBUG
	crisv32_pinmux_dump();
#endif
}

int crisv32_pinmux_dealloc(int port, int first_pin, int last_pin)
{
	int i;
	unsigned long flags;

	crisv32_pinmux_init();

	if (port > PORTS || port < 0)
		return -EINVAL;

	spin_lock_irqsave(&pinmux_lock, flags);

	for (i = first_pin; i <= last_pin; i++)
		pins[port][i] = pinmux_none;

	crisv32_pinmux_set(port);
	spin_unlock_irqrestore(&pinmux_lock, flags);

	return 0;
}

int crisv32_pinmux_dealloc_fixed(enum fixed_function function)
{
	int ret = -EINVAL;
	char saved[sizeof pins];
	unsigned long flags;

	spin_lock_irqsave(&pinmux_lock, flags);

	/* Save internal data for recovery */
	memcpy(saved, pins, sizeof pins);

	crisv32_pinmux_init();	/* Must be done before we read rw_hwprot */

	reg_pinmux_rw_hwprot hwprot = REG_RD(pinmux, regi_pinmux, rw_hwprot);

	switch (function) {
	case pinmux_ser1:
		ret = crisv32_pinmux_dealloc(PORT_C, 4, 7);
		hwprot.ser1 = regk_pinmux_no;
		break;
	case pinmux_ser2:
		ret = crisv32_pinmux_dealloc(PORT_C, 8, 11);
		hwprot.ser2 = regk_pinmux_no;
		break;
	case pinmux_ser3:
		ret = crisv32_pinmux_dealloc(PORT_C, 12, 15);
		hwprot.ser3 = regk_pinmux_no;
		break;
	case pinmux_sser0:
		ret = crisv32_pinmux_dealloc(PORT_C, 0, 3);
		ret |= crisv32_pinmux_dealloc(PORT_C, 16, 16);
		hwprot.sser0 = regk_pinmux_no;
		break;
	case pinmux_sser1:
		ret = crisv32_pinmux_dealloc(PORT_D, 0, 4);
		hwprot.sser1 = regk_pinmux_no;
		break;
	case pinmux_ata0:
		ret = crisv32_pinmux_dealloc(PORT_D, 5, 7);
		ret |= crisv32_pinmux_dealloc(PORT_D, 15, 17);
		hwprot.ata0 = regk_pinmux_no;
		break;
	case pinmux_ata1:
		ret = crisv32_pinmux_dealloc(PORT_D, 0, 4);
		ret |= crisv32_pinmux_dealloc(PORT_E, 17, 17);
		hwprot.ata1 = regk_pinmux_no;
		break;
	case pinmux_ata2:
		ret = crisv32_pinmux_dealloc(PORT_C, 11, 15);
		ret |= crisv32_pinmux_dealloc(PORT_E, 3, 3);
		hwprot.ata2 = regk_pinmux_no;
		break;
	case pinmux_ata3:
		ret = crisv32_pinmux_dealloc(PORT_C, 8, 10);
		ret |= crisv32_pinmux_dealloc(PORT_C, 0, 2);
		hwprot.ata2 = regk_pinmux_no;
		break;
	case pinmux_ata:
		ret = crisv32_pinmux_dealloc(PORT_B, 0, 15);
		ret |= crisv32_pinmux_dealloc(PORT_D, 8, 15);
		hwprot.ata = regk_pinmux_no;
		break;
	case pinmux_eth1:
		ret = crisv32_pinmux_dealloc(PORT_E, 0, 17);
		hwprot.eth1 = regk_pinmux_no;
		hwprot.eth1_mgm = regk_pinmux_no;
		break;
	case pinmux_timer:
		ret = crisv32_pinmux_dealloc(PORT_C, 16, 16);
		hwprot.timer = regk_pinmux_no;
		spin_unlock_irqrestore(&pinmux_lock, flags);
		return ret;
	}

	if (!ret)
		REG_WR(pinmux, regi_pinmux, rw_hwprot, hwprot);
	else
		memcpy(pins, saved, sizeof pins);

	spin_unlock_irqrestore(&pinmux_lock, flags);

	return ret;
}

void crisv32_pinmux_dump(void)
{
	int i, j;

	crisv32_pinmux_init();

	for (i = 0; i < PORTS; i++) {
		printk(KERN_DEBUG "Port %c\n", 'B' + i);
		for (j = 0; j < PORT_PINS; j++)
			printk(KERN_DEBUG "  Pin %d = %d\n", j, pins[i][j]);
	}
}

__initcall(crisv32_pinmux_init);
