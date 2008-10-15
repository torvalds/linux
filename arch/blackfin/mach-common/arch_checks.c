/*
 * File:         arch/blackfin/mach-common/arch_checks.c
 * Based on:
 * Author:	 Robin Getz <rgetz@blackfin.uclinux.org>
 *
 * Created:      25Jul07
 * Description:  Do some checking to make sure things are OK
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <asm/fixed_code.h>
#include <mach/anomaly.h>
#include <asm/clocks.h>

#ifdef CONFIG_BFIN_KERNEL_CLOCK

# if (CONFIG_VCO_HZ > CONFIG_MAX_VCO_HZ)
#  error "VCO selected is more than maximum value. Please change the VCO multipler"
# endif

# if (CONFIG_SCLK_HZ > CONFIG_MAX_SCLK_HZ)
# error "Sclk value selected is more than maximum. Please select a proper value for SCLK multiplier"
# endif

# if (CONFIG_SCLK_HZ < CONFIG_MIN_SCLK_HZ)
# error "Sclk value selected is less than minimum. Please select a proper value for SCLK multiplier"
# endif

# if (ANOMALY_05000273) && (CONFIG_SCLK_HZ * 2 > CONFIG_CCLK_HZ)
# error "ANOMALY 05000273, please make sure CCLK is at least 2x SCLK"
# endif

# if (CONFIG_SCLK_HZ > CONFIG_CCLK_HZ) && (CONFIG_SCLK_HZ != CONFIG_CLKIN_HZ) && (CONFIG_CCLK_HZ != CONFIG_CLKIN_HZ)
# error "Please select sclk less than cclk"
# endif

#endif /* CONFIG_BFIN_KERNEL_CLOCK */

#if CONFIG_BOOT_LOAD < FIXED_CODE_END
# error "The kernel load address must be after the fixed code section"
#endif

#if (CONFIG_BOOT_LOAD & 0x3)
# error "The kernel load address must be 4 byte aligned"
#endif
