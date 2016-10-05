/*
 * dvb-math provides some complex fixed-point math
 * operations shared between the dvb related stuff
 *
 * Copyright (C) 2006 Christoph Pfister (christophpfister@gmail.com)
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __DVB_MATH_H
#define __DVB_MATH_H

#include <linux/types.h>

/**
 * cintlog2 - computes log2 of a value; the result is shifted left by 24 bits
 *
 * @value: The value (must be != 0)
 *
 * to use rational values you can use the following method:
 *
 *   intlog2(value) = intlog2(value * 2^x) - x * 2^24
 *
 * Some usecase examples:
 *
 *	intlog2(8) will give 3 << 24 = 3 * 2^24
 *
 *	intlog2(9) will give 3 << 24 + ... = 3.16... * 2^24
 *
 *	intlog2(1.5) = intlog2(3) - 2^24 = 0.584... * 2^24
 *
 *
 * return: log2(value) * 2^24
 */
extern unsigned int intlog2(u32 value);

/**
 * intlog10 - computes log10 of a value; the result is shifted left by 24 bits
 *
 * @value: The value (must be != 0)
 *
 * to use rational values you can use the following method:
 *
 *   intlog10(value) = intlog10(value * 10^x) - x * 2^24
 *
 * An usecase example:
 *
 *	intlog10(1000) will give 3 << 24 = 3 * 2^24
 *
 *   due to the implementation intlog10(1000) might be not exactly 3 * 2^24
 *
 * look at intlog2 for similar examples
 *
 * return: log10(value) * 2^24
 */
extern unsigned int intlog10(u32 value);

#endif
