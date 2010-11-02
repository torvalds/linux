/*
 * Common INTC2 register accessors
 *
 * Copyright (C) 2007, 2008 Magnus Damm
 * Copyright (C) 2009, 2010 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/io.h>
#include "internals.h"

unsigned long intc_phys_to_virt(struct intc_desc_int *d, unsigned long address)
{
	struct intc_window *window;
	int k;

	/* scan through physical windows and convert address */
	for (k = 0; k < d->nr_windows; k++) {
		window = d->window + k;

		if (address < window->phys)
			continue;

		if (address >= (window->phys + window->size))
			continue;

		address -= window->phys;
		address += (unsigned long)window->virt;

		return address;
	}

	/* no windows defined, register must be 1:1 mapped virt:phys */
	return address;
}

unsigned int intc_get_reg(struct intc_desc_int *d, unsigned long address)
{
	unsigned int k;

	address = intc_phys_to_virt(d, address);

	for (k = 0; k < d->nr_reg; k++) {
		if (d->reg[k] == address)
			return k;
	}

	BUG();
	return 0;
}

unsigned int intc_set_field_from_handle(unsigned int value,
					unsigned int field_value,
					unsigned int handle)
{
	unsigned int width = _INTC_WIDTH(handle);
	unsigned int shift = _INTC_SHIFT(handle);

	value &= ~(((1 << width) - 1) << shift);
	value |= field_value << shift;
	return value;
}

unsigned long intc_get_field_from_handle(unsigned int value, unsigned int handle)
{
	unsigned int width = _INTC_WIDTH(handle);
	unsigned int shift = _INTC_SHIFT(handle);
	unsigned int mask = ((1 << width) - 1) << shift;

	return (value & mask) >> shift;
}

static unsigned long test_8(unsigned long addr, unsigned long h,
			    unsigned long ignore)
{
	return intc_get_field_from_handle(__raw_readb(addr), h);
}

static unsigned long test_16(unsigned long addr, unsigned long h,
			     unsigned long ignore)
{
	return intc_get_field_from_handle(__raw_readw(addr), h);
}

static unsigned long test_32(unsigned long addr, unsigned long h,
			     unsigned long ignore)
{
	return intc_get_field_from_handle(__raw_readl(addr), h);
}

static unsigned long write_8(unsigned long addr, unsigned long h,
			     unsigned long data)
{
	__raw_writeb(intc_set_field_from_handle(0, data, h), addr);
	(void)__raw_readb(addr);	/* Defeat write posting */
	return 0;
}

static unsigned long write_16(unsigned long addr, unsigned long h,
			      unsigned long data)
{
	__raw_writew(intc_set_field_from_handle(0, data, h), addr);
	(void)__raw_readw(addr);	/* Defeat write posting */
	return 0;
}

static unsigned long write_32(unsigned long addr, unsigned long h,
			      unsigned long data)
{
	__raw_writel(intc_set_field_from_handle(0, data, h), addr);
	(void)__raw_readl(addr);	/* Defeat write posting */
	return 0;
}

static unsigned long modify_8(unsigned long addr, unsigned long h,
			      unsigned long data)
{
	unsigned long flags;
	unsigned int value;
	local_irq_save(flags);
	value = intc_set_field_from_handle(__raw_readb(addr), data, h);
	__raw_writeb(value, addr);
	(void)__raw_readb(addr);	/* Defeat write posting */
	local_irq_restore(flags);
	return 0;
}

static unsigned long modify_16(unsigned long addr, unsigned long h,
			       unsigned long data)
{
	unsigned long flags;
	unsigned int value;
	local_irq_save(flags);
	value = intc_set_field_from_handle(__raw_readw(addr), data, h);
	__raw_writew(value, addr);
	(void)__raw_readw(addr);	/* Defeat write posting */
	local_irq_restore(flags);
	return 0;
}

static unsigned long modify_32(unsigned long addr, unsigned long h,
			       unsigned long data)
{
	unsigned long flags;
	unsigned int value;
	local_irq_save(flags);
	value = intc_set_field_from_handle(__raw_readl(addr), data, h);
	__raw_writel(value, addr);
	(void)__raw_readl(addr);	/* Defeat write posting */
	local_irq_restore(flags);
	return 0;
}

static unsigned long intc_mode_field(unsigned long addr,
				     unsigned long handle,
				     unsigned long (*fn)(unsigned long,
						unsigned long,
						unsigned long),
				     unsigned int irq)
{
	return fn(addr, handle, ((1 << _INTC_WIDTH(handle)) - 1));
}

static unsigned long intc_mode_zero(unsigned long addr,
				    unsigned long handle,
				    unsigned long (*fn)(unsigned long,
					       unsigned long,
					       unsigned long),
				    unsigned int irq)
{
	return fn(addr, handle, 0);
}

static unsigned long intc_mode_prio(unsigned long addr,
				    unsigned long handle,
				    unsigned long (*fn)(unsigned long,
					       unsigned long,
					       unsigned long),
				    unsigned int irq)
{
	return fn(addr, handle, intc_get_prio_level(irq));
}

unsigned long (*intc_reg_fns[])(unsigned long addr,
				unsigned long h,
				unsigned long data) = {
	[REG_FN_TEST_BASE + 0] = test_8,
	[REG_FN_TEST_BASE + 1] = test_16,
	[REG_FN_TEST_BASE + 3] = test_32,
	[REG_FN_WRITE_BASE + 0] = write_8,
	[REG_FN_WRITE_BASE + 1] = write_16,
	[REG_FN_WRITE_BASE + 3] = write_32,
	[REG_FN_MODIFY_BASE + 0] = modify_8,
	[REG_FN_MODIFY_BASE + 1] = modify_16,
	[REG_FN_MODIFY_BASE + 3] = modify_32,
};

unsigned long (*intc_enable_fns[])(unsigned long addr,
				   unsigned long handle,
				   unsigned long (*fn)(unsigned long,
					    unsigned long,
					    unsigned long),
				   unsigned int irq) = {
	[MODE_ENABLE_REG] = intc_mode_field,
	[MODE_MASK_REG] = intc_mode_zero,
	[MODE_DUAL_REG] = intc_mode_field,
	[MODE_PRIO_REG] = intc_mode_prio,
	[MODE_PCLR_REG] = intc_mode_prio,
};

unsigned long (*intc_disable_fns[])(unsigned long addr,
				    unsigned long handle,
				    unsigned long (*fn)(unsigned long,
					     unsigned long,
					     unsigned long),
				    unsigned int irq) = {
	[MODE_ENABLE_REG] = intc_mode_zero,
	[MODE_MASK_REG] = intc_mode_field,
	[MODE_DUAL_REG] = intc_mode_field,
	[MODE_PRIO_REG] = intc_mode_zero,
	[MODE_PCLR_REG] = intc_mode_field,
};

unsigned long (*intc_enable_noprio_fns[])(unsigned long addr,
					  unsigned long handle,
					  unsigned long (*fn)(unsigned long,
						unsigned long,
						unsigned long),
					  unsigned int irq) = {
	[MODE_ENABLE_REG] = intc_mode_field,
	[MODE_MASK_REG] = intc_mode_zero,
	[MODE_DUAL_REG] = intc_mode_field,
	[MODE_PRIO_REG] = intc_mode_field,
	[MODE_PCLR_REG] = intc_mode_field,
};
