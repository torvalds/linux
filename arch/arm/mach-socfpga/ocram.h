/*
 *  arch/arm/mach-socfpga/ocram.h
 *
 * Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MACH_SOCFPGA_OCRAM_H
#define MACH_SOCFPGA_OCRAM_H

#ifdef CONFIG_EDAC_ALTERA_OCRAM_ECC
void socfpga_init_ocram_ecc(void);
#else
inline void socfpga_init_ocram_ecc(void)
{
}
#endif

#endif /* #ifndef MACH_SOCFPGA_OCRAM_H */
