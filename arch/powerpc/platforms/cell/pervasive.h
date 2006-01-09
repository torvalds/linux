/*
 * Cell Pervasive Monitor and Debug interface and HW structures
 *
 * (C) Copyright IBM Corporation 2005
 *
 * Authors: Maximino Aguilar (maguilar@us.ibm.com)
 *          David J. Erb (djerb@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#ifndef PERVASIVE_H
#define PERVASIVE_H

struct pmd_regs {
	u8 pad_0x0000_0x0800[0x0800 - 0x0000];			/* 0x0000 */

	/* Thermal Sensor Registers */
	u64  ts_ctsr1;						/* 0x0800 */
	u64  ts_ctsr2;						/* 0x0808 */
	u64  ts_mtsr1;						/* 0x0810 */
	u64  ts_mtsr2;						/* 0x0818 */
	u64  ts_itr1;						/* 0x0820 */
	u64  ts_itr2;						/* 0x0828 */
	u64  ts_gitr;						/* 0x0830 */
	u64  ts_isr;						/* 0x0838 */
	u64  ts_imr;						/* 0x0840 */
	u64  tm_cr1;						/* 0x0848 */
	u64  tm_cr2;						/* 0x0850 */
	u64  tm_simr;						/* 0x0858 */
	u64  tm_tpr;						/* 0x0860 */
	u64  tm_str1;						/* 0x0868 */
	u64  tm_str2;						/* 0x0870 */
	u64  tm_tsr;						/* 0x0878 */

	/* Power Management */
	u64  pm_control;					/* 0x0880 */
#define PMD_PAUSE_ZERO_CONTROL		0x10000
	u64  pm_status;						/* 0x0888 */

	/* Time Base Register */
	u64  tbr;						/* 0x0890 */

	u8   pad_0x0898_0x1000 [0x1000 - 0x0898];		/* 0x0898 */
};

void __init cell_pervasive_init(void);

#endif
