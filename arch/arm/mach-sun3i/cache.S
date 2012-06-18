/*
 * arch/arm/mach-sun3i/cache.S
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#define     CLINE       5

		.type	_eLIBs_CleanFlushDCacheRegion,#function
		.globl _eLIBs_CleanFlushDCacheRegion
_eLIBs_CleanFlushDCacheRegion:
            bic     r0, r0, #(1 << CLINE) - 1         @//clip 2 cline adr
            mov     r1, r1, lsr #CLINE                @//bytes to cline
10:
            mcr p15, 0, r0, c7, c14, 1         @//清除清理d-cline@adr

            add     r0, r0, #(1 << CLINE)             @//+1 next line adr
            subs    r1, r1, #1                          @//-1 cline counter
            bne     10b
11:
            mov     pc, lr
