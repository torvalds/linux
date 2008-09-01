/*
 * File:         include/asm-blackfin/mach-common/clocks.h
 * Based on:     include/asm-blackfin/mach-bf537/bf537.h
 * Author:	 Robin Getz <rgetz@blackfin.uclinux.org>
 *
 * Created:      25Jul07
 * Description:  Common Clock definitions for various kernel files
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

#ifndef _BFIN_CLOCKS_H
#define _BFIN_CLOCKS_H

#ifdef CONFIG_CCLK_DIV_1
# define CONFIG_CCLK_ACT_DIV   CCLK_DIV1
# define CONFIG_CCLK_DIV 1
#endif

#ifdef CONFIG_CCLK_DIV_2
# define CONFIG_CCLK_ACT_DIV   CCLK_DIV2
# define CONFIG_CCLK_DIV 2
#endif

#ifdef CONFIG_CCLK_DIV_4
# define CONFIG_CCLK_ACT_DIV   CCLK_DIV4
# define CONFIG_CCLK_DIV 4
#endif

#ifdef CONFIG_CCLK_DIV_8
# define CONFIG_CCLK_ACT_DIV   CCLK_DIV8
# define CONFIG_CCLK_DIV 8
#endif

#ifndef CONFIG_PLL_BYPASS
# ifndef CONFIG_CLKIN_HALF
#  define CONFIG_VCO_HZ   (CONFIG_CLKIN_HZ * CONFIG_VCO_MULT)
# else
#  define CONFIG_VCO_HZ   ((CONFIG_CLKIN_HZ * CONFIG_VCO_MULT)/2)
# endif

# define CONFIG_CCLK_HZ  (CONFIG_VCO_HZ/CONFIG_CCLK_DIV)
# define CONFIG_SCLK_HZ  (CONFIG_VCO_HZ/CONFIG_SCLK_DIV)

#else
# define CONFIG_VCO_HZ   (CONFIG_CLKIN_HZ)
# define CONFIG_CCLK_HZ  (CONFIG_CLKIN_HZ)
# define CONFIG_SCLK_HZ  (CONFIG_CLKIN_HZ)
# define CONFIG_VCO_MULT 0
#endif

#endif
