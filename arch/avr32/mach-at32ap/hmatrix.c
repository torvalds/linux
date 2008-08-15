/*
 * High-Speed Bus Matrix helper functions
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/chip.h>
#include <mach/hmatrix.h>

static inline void __hmatrix_write_reg(unsigned long offset, u32 value)
{
	__raw_writel(value, (void __iomem __force *)(HMATRIX_BASE + offset));
}

static inline u32 __hmatrix_read_reg(unsigned long offset)
{
	return __raw_readl((void __iomem __force *)(HMATRIX_BASE + offset));
}

/**
 * hmatrix_write_reg - write HMATRIX configuration register
 * @offset: register offset
 * @value: value to be written to the register at @offset
 */
void hmatrix_write_reg(unsigned long offset, u32 value)
{
	clk_enable(&at32_hmatrix_clk);
	__hmatrix_write_reg(offset, value);
	__hmatrix_read_reg(offset);
	clk_disable(&at32_hmatrix_clk);
}

/**
 * hmatrix_read_reg - read HMATRIX configuration register
 * @offset: register offset
 *
 * Returns the value of the register at @offset.
 */
u32 hmatrix_read_reg(unsigned long offset)
{
	u32 value;

	clk_enable(&at32_hmatrix_clk);
	value = __hmatrix_read_reg(offset);
	clk_disable(&at32_hmatrix_clk);

	return value;
}

/**
 * hmatrix_sfr_set_bits - set bits in a slave's Special Function Register
 * @slave_id: operate on the SFR belonging to this slave
 * @mask: mask of bits to be set in the SFR
 */
void hmatrix_sfr_set_bits(unsigned int slave_id, u32 mask)
{
	u32 value;

	clk_enable(&at32_hmatrix_clk);
	value = __hmatrix_read_reg(HMATRIX_SFR(slave_id));
	value |= mask;
	__hmatrix_write_reg(HMATRIX_SFR(slave_id), value);
	__hmatrix_read_reg(HMATRIX_SFR(slave_id));
	clk_disable(&at32_hmatrix_clk);
}

/**
 * hmatrix_sfr_set_bits - clear bits in a slave's Special Function Register
 * @slave_id: operate on the SFR belonging to this slave
 * @mask: mask of bits to be cleared in the SFR
 */
void hmatrix_sfr_clear_bits(unsigned int slave_id, u32 mask)
{
	u32 value;

	clk_enable(&at32_hmatrix_clk);
	value = __hmatrix_read_reg(HMATRIX_SFR(slave_id));
	value &= ~mask;
	__hmatrix_write_reg(HMATRIX_SFR(slave_id), value);
	__hmatrix_read_reg(HMATRIX_SFR(slave_id));
	clk_disable(&at32_hmatrix_clk);
}
