/******************************************************************************
 * linux/arch/ia64/xen/paravirtentry.S
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/init.h>
#include <asm/asmmacro.h>
#include <asm/asm-offsets.h>
#include <asm/paravirt_privop.h>
#include <asm/paravirt_patch.h>
#include "entry.h"

#define DATA8(sym, init_value)			\
	.pushsection .data..read_mostly ;	\
	.align 8 ;				\
	.global sym ;				\
	sym: ;					\
	data8 init_value ;			\
	.popsection

#define BRANCH(targ, reg, breg, type)					\
	PARAVIRT_PATCH_SITE_BR(PARAVIRT_PATCH_TYPE_BR_ ## type) ;	\
	;;								\
	movl reg=targ ;							\
	;;								\
	ld8 reg=[reg] ;							\
	;;								\
	mov breg=reg ;							\
	br.cond.sptk.many breg

#define BRANCH_PROC(sym, reg, breg, type)				\
	DATA8(paravirt_ ## sym ## _targ, ia64_native_ ## sym) ;		\
	GLOBAL_ENTRY(paravirt_ ## sym) ;				\
		BRANCH(paravirt_ ## sym ## _targ, reg, breg, type) ;	\
	END(paravirt_ ## sym)

#define BRANCH_PROC_UNWINFO(sym, reg, breg, type)			\
	DATA8(paravirt_ ## sym ## _targ, ia64_native_ ## sym) ;		\
	GLOBAL_ENTRY(paravirt_ ## sym) ;				\
		PT_REGS_UNWIND_INFO(0) ;				\
		BRANCH(paravirt_ ## sym ## _targ, reg, breg, type) ;	\
	END(paravirt_ ## sym)


BRANCH_PROC(switch_to, r22, b7, SWITCH_TO)
BRANCH_PROC_UNWINFO(leave_syscall, r22, b7, LEAVE_SYSCALL)
BRANCH_PROC(work_processed_syscall, r2, b7, WORK_PROCESSED_SYSCALL)
BRANCH_PROC_UNWINFO(leave_kernel, r22, b7, LEAVE_KERNEL)


#ifdef CONFIG_MODULES
#define __INIT_OR_MODULE	.text
#define __INITDATA_OR_MODULE	.data
#else
#define __INIT_OR_MODULE	__INIT
#define __INITDATA_OR_MODULE	__INITDATA
#endif /* CONFIG_MODULES */

	__INIT_OR_MODULE
	GLOBAL_ENTRY(paravirt_fc_i)
	fc.i r32
	br.ret.sptk.many rp
	END(paravirt_fc_i)
	__FINIT

	__INIT_OR_MODULE
	.align 32
	GLOBAL_ENTRY(paravirt_nop_b_inst_bundle)
	{
		nop.b 0
		nop.b 0
		nop.b 0
	}
	END(paravirt_nop_b_inst_bundle)
	__FINIT

	/* NOTE: nop.[mfi] has same format */
	__INIT_OR_MODULE
	GLOBAL_ENTRY(paravirt_nop_mfi_inst_bundle)
	{
		nop.m 0
		nop.f 0
		nop.i 0
	}
	END(paravirt_nop_mfi_inst_bundle)
	__FINIT

	__INIT_OR_MODULE
	GLOBAL_ENTRY(paravirt_nop_bundle)
paravirt_nop_bundle_start:
	{
		nop 0
		nop 0
		nop 0
	}
paravirt_nop_bundle_end:
	END(paravirt_nop_bundle)
	__FINIT

	__INITDATA_OR_MODULE
	.align 8
	.global paravirt_nop_bundle_size
paravirt_nop_bundle_size:
	data8	paravirt_nop_bundle_end - paravirt_nop_bundle_start
