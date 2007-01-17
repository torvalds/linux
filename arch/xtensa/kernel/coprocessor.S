/*
 * arch/xtensa/kernel/coprocessor.S
 *
 * Xtensa processor configuration-specific table of coprocessor and
 * other custom register layout information.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 - 2005 Tensilica Inc.
 *
 * Marc Gauthier <marc@tensilica.com> <marc@alumni.uwaterloo.ca>
 */

/*
 * This module contains a table that describes the layout of the various
 * custom registers and states associated with each coprocessor, as well
 * as those not associated with any coprocessor ("extra state").
 * This table is included with core dumps and is available via the ptrace
 * interface, allowing the layout of such register/state information to
 * be modified in the kernel without affecting the debugger.  Each
 * register or state is identified using a 32-bit "libdb target number"
 * assigned when the Xtensa processor is generated.
 */

#include <linux/linkage.h>
#include <asm/processor.h>

#if XCHAL_HAVE_CP

#define CP_LAST ((XCHAL_CP_MAX - 1) * COPROCESSOR_INFO_SIZE)

ENTRY(release_coprocessors)

	entry	a1, 16
						# a2: task
	movi	a3, 1 << XCHAL_CP_MAX 		# a3: coprocessor-bit
	movi	a4, coprocessor_info+CP_LAST	# a4: owner-table
						# a5: tmp
	movi	a6, 0				# a6: 0
	rsil	a7, LOCKLEVEL			# a7: PS

1:	/* Check if task is coprocessor owner of coprocessor[i]. */

	l32i	a5, a4, COPROCESSOR_INFO_OWNER
	srli	a3, a3, 1
	beqz	a3, 1f
	addi	a4, a4, -8
	beq	a2, a5, 1b

	/* Found an entry: Clear entry CPENABLE bit to disable CP. */

	rsr	a5, CPENABLE
	s32i	a6, a4, COPROCESSOR_INFO_OWNER
	xor	a5, a3, a5
	wsr	a5, CPENABLE

	bnez	a3, 1b

1:	wsr	a7, PS
	rsync
	retw


ENTRY(disable_coprocessor)
	entry	sp, 16
	rsil	a7, LOCKLEVEL
	rsr	a3, CPENABLE
	movi	a4, 1
	ssl	a2
	sll	a4, a4
	and	a4, a3, a4
	xor	a3, a3, a4
	wsr	a3, CPENABLE
	wsr	a7, PS
	rsync
	retw

ENTRY(enable_coprocessor)
	entry	sp, 16
	rsil	a7, LOCKLEVEL
	rsr	a3, CPENABLE
	movi	a4, 1
	ssl	a2
	sll	a4, a4
	or	a3, a3, a4
	wsr	a3, CPENABLE
	wsr	a7, PS
	rsync
	retw


ENTRY(save_coprocessor_extra)
	entry	sp, 16
	xchal_extra_store_funcbody
	retw

ENTRY(restore_coprocessor_extra)
	entry	sp, 16
	xchal_extra_load_funcbody
	retw

ENTRY(save_coprocessor_registers)
	entry	sp, 16
	xchal_cpi_store_funcbody
	retw

ENTRY(restore_coprocessor_registers)
	entry	sp, 16
	xchal_cpi_load_funcbody
	retw


/*
 *  The Xtensa compile-time HAL (core.h) XCHAL_*_SA_CONTENTS_LIBDB macros
 *  describe the contents of coprocessor & extra save areas in terms of
 *  undefined CONTENTS_LIBDB_{SREG,UREG,REGF} macros.  We define these
 *  latter macros here; they expand into a table of the format we want.
 *  The general format is:
 *
 *	CONTENTS_LIBDB_SREG(libdbnum, offset, size, align, rsv1, name, sregnum,
 *			    bitmask, rsv2, rsv3)
 *	CONTENTS_LIBDB_UREG(libdbnum, offset, size, align, rsv1, name, uregnum,
 *			    bitmask, rsv2, rsv3)
 *	CONTENTS_LIBDB_REGF(libdbnum, offset, size, align, rsv1, name, index,
 *			    numentries, contentsize, regname_base,
 *			    regfile_name, rsv2, rsv3)
 *
 *  For this table, we only care about the <libdbnum>, <offset> and <size>
 *  fields.
 */

/*  Map all XCHAL CONTENTS macros to the reg_entry asm macro defined below:  */

#define CONTENTS_LIBDB_SREG(libdbnum,offset,size,align,rsv1,name,sregnum,     \
			    bitmask, rsv2, rsv3)			      \
		reg_entry libdbnum, offset, size ;
#define CONTENTS_LIBDB_UREG(libdbnum,offset,size,align,rsv1,name,uregnum,     \
			    bitmask, rsv2, rsv3)			      \
		reg_entry libdbnum, offset, size ;
#define CONTENTS_LIBDB_REGF(libdbnum, offset, size, align, rsv1, name, index, \
			    numentries, contentsize, regname_base,	      \
			    regfile_name, rsv2, rsv3)			      \
		reg_entry libdbnum, offset, size ;

/* A single table entry: */
	.macro	reg_entry	libdbnum, offset, size
	 .ifne	(__last_offset-(__last_group_offset+\offset))
	  /* padding entry */
	  .word	(0xFC000000+__last_offset-(__last_group_offset+\offset))
	 .endif
	 .word	\libdbnum				/* actual entry */
	 .set	__last_offset, __last_group_offset+\offset+\size
	.endm	/* reg_entry */


/* Table entry that marks the beginning of a group (coprocessor or "extra"): */
	.macro	reg_group	cpnum, num_entries, align
	 .set	__last_group_offset, (__last_offset + \align- 1) & -\align
	 .ifne	\num_entries
	  .word	0xFD000000+(\cpnum<<16)+\num_entries
	 .endif
	.endm	/* reg_group */

/*
 * Register info tables.
 */

	.section .rodata, "a"
	.globl	_xtensa_reginfo_tables
	.globl	_xtensa_reginfo_table_size
	.align	4
_xtensa_reginfo_table_size:
	.word	_xtensa_reginfo_table_end - _xtensa_reginfo_tables

_xtensa_reginfo_tables:
	.set	__last_offset, 0
	reg_group 0xFF, XCHAL_EXTRA_SA_CONTENTS_LIBDB_NUM, XCHAL_EXTRA_SA_ALIGN
	XCHAL_EXTRA_SA_CONTENTS_LIBDB
	reg_group 0, XCHAL_CP0_SA_CONTENTS_LIBDB_NUM, XCHAL_CP0_SA_ALIGN
	XCHAL_CP0_SA_CONTENTS_LIBDB
	reg_group 1, XCHAL_CP1_SA_CONTENTS_LIBDB_NUM, XCHAL_CP1_SA_ALIGN
	XCHAL_CP1_SA_CONTENTS_LIBDB
	reg_group 2, XCHAL_CP2_SA_CONTENTS_LIBDB_NUM, XCHAL_CP2_SA_ALIGN
	XCHAL_CP2_SA_CONTENTS_LIBDB
	reg_group 3, XCHAL_CP3_SA_CONTENTS_LIBDB_NUM, XCHAL_CP3_SA_ALIGN
	XCHAL_CP3_SA_CONTENTS_LIBDB
	reg_group 4, XCHAL_CP4_SA_CONTENTS_LIBDB_NUM, XCHAL_CP4_SA_ALIGN
	XCHAL_CP4_SA_CONTENTS_LIBDB
	reg_group 5, XCHAL_CP5_SA_CONTENTS_LIBDB_NUM, XCHAL_CP5_SA_ALIGN
	XCHAL_CP5_SA_CONTENTS_LIBDB
	reg_group 6, XCHAL_CP6_SA_CONTENTS_LIBDB_NUM, XCHAL_CP6_SA_ALIGN
	XCHAL_CP6_SA_CONTENTS_LIBDB
	reg_group 7, XCHAL_CP7_SA_CONTENTS_LIBDB_NUM, XCHAL_CP7_SA_ALIGN
	XCHAL_CP7_SA_CONTENTS_LIBDB
	.word	0xFC000000	/* invalid register number,marks end of table*/
_xtensa_reginfo_table_end:
#endif

