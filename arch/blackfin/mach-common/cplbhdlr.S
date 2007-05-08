/*
 * File:         arch/blackfin/mach-common/cplbhdlr.S
 * Based on:
 * Author:       LG Soft India
 *
 * Created:      ?
 * Description:  CPLB exception handler
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

#include <linux/linkage.h>
#include <asm/cplb.h>
#include <asm/entry.h>

#ifdef CONFIG_EXCPT_IRQ_SYSC_L1
.section .l1.text
#else
.text
#endif

.type _cplb_mgr, STT_FUNC;
.type _panic_cplb_error, STT_FUNC;

.align 2

.global __cplb_hdr;
.type __cplb_hdr, STT_FUNC;
ENTRY(__cplb_hdr)
	R2 = SEQSTAT;

	/* Mask the contents of SEQSTAT and leave only EXCAUSE in R2 */
	R2 <<= 26;
	R2 >>= 26;

	R1 = 0x23; /* Data access CPLB protection violation */
	CC = R2 == R1;
	IF !CC JUMP .Lnot_data_write;
	R0 = 2;		/* is a write to data space*/
	JUMP .Lis_icplb_miss;

.Lnot_data_write:
	R1 = 0x2C; /* CPLB miss on an instruction fetch */
	CC = R2 == R1;
	R0 = 0;		/* is_data_miss == False*/
	IF CC JUMP .Lis_icplb_miss;

	R1 = 0x26;
	CC = R2 == R1;
	IF !CC JUMP .Lunknown;

	R0 = 1;		/* is_data_miss == True*/

.Lis_icplb_miss:

#if defined(CONFIG_BLKFIN_CACHE) || defined(CONFIG_BLKFIN_DCACHE)
# if defined(CONFIG_BLKFIN_CACHE) && !defined(CONFIG_BLKFIN_DCACHE)
	R1 = CPLB_ENABLE_ICACHE;
# endif
# if !defined(CONFIG_BLKFIN_CACHE) && defined(CONFIG_BLKFIN_DCACHE)
	R1 = CPLB_ENABLE_DCACHE;
# endif
# if defined(CONFIG_BLKFIN_CACHE) && defined(CONFIG_BLKFIN_DCACHE)
	R1 = CPLB_ENABLE_DCACHE | CPLB_ENABLE_ICACHE;
# endif
#else
	R1 = 0;
#endif

	[--SP] = RETS;
	CALL _cplb_mgr;
	RETS = [SP++];
	CC = R0 == 0;
	IF !CC JUMP .Lnot_replaced;
	RTS;

/*
 * Diagnostic exception handlers
 */
.Lunknown:
	R0 = CPLB_UNKNOWN_ERR;
	JUMP .Lcplb_error;

.Lnot_replaced:
	CC = R0 == CPLB_NO_UNLOCKED;
	IF !CC JUMP .Lnext_check;
	R0 = CPLB_NO_UNLOCKED;
	JUMP .Lcplb_error;

.Lnext_check:
	CC = R0 == CPLB_NO_ADDR_MATCH;
	IF !CC JUMP .Lnext_check2;
	R0 = CPLB_NO_ADDR_MATCH;
	JUMP .Lcplb_error;

.Lnext_check2:
	CC = R0 == CPLB_PROT_VIOL;
	IF !CC JUMP .Lstrange_return_from_cplb_mgr;
	R0 = CPLB_PROT_VIOL;
	JUMP .Lcplb_error;

.Lstrange_return_from_cplb_mgr:
	IDLE;
	CSYNC;
	JUMP .Lstrange_return_from_cplb_mgr;

.Lcplb_error:
	R1 = sp;
	SP += -12;
	call _panic_cplb_error;
	SP += 12;
	JUMP _handle_bad_cplb;
