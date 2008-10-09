/*
 * File:         include/asm-blackfin/cplbinit.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
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
#ifndef __ASM_BFIN_CPLB_MPU_H
#define __ASM_BFIN_CPLB_MPU_H

struct cplb_entry {
	unsigned long data, addr;
};

struct mem_region {
	unsigned long start, end;
	unsigned long dcplb_data;
	unsigned long icplb_data;
};

extern struct cplb_entry dcplb_tbl[MAX_CPLBS];
extern struct cplb_entry icplb_tbl[MAX_CPLBS];
extern int first_switched_icplb;
extern int first_mask_dcplb;
extern int first_switched_dcplb;

extern int nr_dcplb_miss, nr_icplb_miss, nr_icplb_supv_miss, nr_dcplb_prot;
extern int nr_cplb_flush;

extern int page_mask_order;
extern int page_mask_nelts;

extern unsigned long *current_rwx_mask;

extern void flush_switched_cplbs(void);
extern void set_mask_dcplbs(unsigned long *);

extern void __noreturn panic_cplb_error(int seqstat, struct pt_regs *);

#endif /* __ASM_BFIN_CPLB_MPU_H */
