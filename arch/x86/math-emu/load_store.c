// SPDX-License-Identifier: GPL-2.0
/*---------------------------------------------------------------------------+
 |  load_store.c                                                             |
 |                                                                           |
 | This file contains most of the code to interpret the FPU instructions     |
 | which load and store from user memory.                                    |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1997                                         |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail   billm@suburbia.net             |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 | Note:                                                                     |
 |    The file contains code which accesses user memory.                     |
 |    Emulator static data may change when user memory is accessed, due to   |
 |    other processes using the emulator while swapping is in progress.      |
 +---------------------------------------------------------------------------*/

#include <linux/uaccess.h>

#include "fpu_system.h"
#include "exception.h"
#include "fpu_emu.h"
#include "status_w.h"
#include "control_w.h"

#define _NONE_ 0		/* st0_ptr etc not needed */
#define _REG0_ 1		/* Will be storing st(0) */
#define _PUSH_ 3		/* Need to check for space to push onto stack */
#define _null_ 4		/* Function illegal or not implemented */

#define pop_0()	{ FPU_settag0(TAG_Empty); top++; }

/* index is a 5-bit value: (3-bit FPU_modrm.reg field | opcode[2,1]) */
static u_char const type_table[32] = {
	_PUSH_, _PUSH_, _PUSH_, _PUSH_, /* /0: d9:fld f32,  db:fild m32,  dd:fld f64,  df:fild m16 */
	_null_, _REG0_, _REG0_, _REG0_, /* /1: d9:undef,    db,dd,df:fisttp m32/64/16 */
	_REG0_, _REG0_, _REG0_, _REG0_, /* /2: d9:fst f32,  db:fist m32,  dd:fst f64,  df:fist m16 */
	_REG0_, _REG0_, _REG0_, _REG0_, /* /3: d9:fstp f32, db:fistp m32, dd:fstp f64, df:fistp m16 */
	_NONE_, _null_, _NONE_, _PUSH_,
	_NONE_, _PUSH_, _null_, _PUSH_,
	_NONE_, _null_, _NONE_, _REG0_,
	_NONE_, _REG0_, _NONE_, _REG0_
};

u_char const data_sizes_16[32] = {
	4, 4, 8, 2,
	0, 4, 8, 2, /* /1: d9:undef, db,dd,df:fisttp */
	4, 4, 8, 2,
	4, 4, 8, 2,
	14, 0, 94, 10, 2, 10, 0, 8,
	14, 0, 94, 10, 2, 10, 2, 8
};

static u_char const data_sizes_32[32] = {
	4, 4, 8, 2,
	0, 4, 8, 2, /* /1: d9:undef, db,dd,df:fisttp */
	4, 4, 8, 2,
	4, 4, 8, 2,
	28, 0, 108, 10, 2, 10, 0, 8,
	28, 0, 108, 10, 2, 10, 2, 8
};

int FPU_load_store(u_char type, fpu_addr_modes addr_modes,
		   void __user * data_address)
{
	FPU_REG loaded_data;
	FPU_REG *st0_ptr;
	u_char st0_tag = TAG_Empty;	/* This is just to stop a gcc warning. */
	u_char loaded_tag;
	int sv_cw;

	st0_ptr = NULL;		/* Initialized just to stop compiler warnings. */

	if (addr_modes.default_mode & PROTECTED) {
		if (addr_modes.default_mode == SEG32) {
			if (access_limit < data_sizes_32[type])
				math_abort(FPU_info, SIGSEGV);
		} else if (addr_modes.default_mode == PM16) {
			if (access_limit < data_sizes_16[type])
				math_abort(FPU_info, SIGSEGV);
		}
#ifdef PARANOID
		else
			EXCEPTION(EX_INTERNAL | 0x140);
#endif /* PARANOID */
	}

	switch (type_table[type]) {
	case _NONE_:
		break;
	case _REG0_:
		st0_ptr = &st(0);	/* Some of these instructions pop after
					   storing */
		st0_tag = FPU_gettag0();
		break;
	case _PUSH_:
		{
			if (FPU_gettagi(-1) != TAG_Empty) {
				FPU_stack_overflow();
				return 0;
			}
			top--;
			st0_ptr = &st(0);
		}
		break;
	case _null_:
		FPU_illegal();
		return 0;
#ifdef PARANOID
	default:
		EXCEPTION(EX_INTERNAL | 0x141);
		return 0;
#endif /* PARANOID */
	}

	switch (type) {
	/* type is a 5-bit value: (3-bit FPU_modrm.reg field | opcode[2,1]) */
	case 000:		/* fld m32real (d9 /0) */
		clear_C1();
		loaded_tag =
		    FPU_load_single((float __user *)data_address, &loaded_data);
		if ((loaded_tag == TAG_Special)
		    && isNaN(&loaded_data)
		    && (real_1op_NaN(&loaded_data) < 0)) {
			top++;
			break;
		}
		FPU_copy_to_reg0(&loaded_data, loaded_tag);
		break;
	case 001:		/* fild m32int (db /0) */
		clear_C1();
		loaded_tag =
		    FPU_load_int32((long __user *)data_address, &loaded_data);
		FPU_copy_to_reg0(&loaded_data, loaded_tag);
		break;
	case 002:		/* fld m64real (dd /0) */
		clear_C1();
		loaded_tag =
		    FPU_load_double((double __user *)data_address,
				    &loaded_data);
		if ((loaded_tag == TAG_Special)
		    && isNaN(&loaded_data)
		    && (real_1op_NaN(&loaded_data) < 0)) {
			top++;
			break;
		}
		FPU_copy_to_reg0(&loaded_data, loaded_tag);
		break;
	case 003:		/* fild m16int (df /0) */
		clear_C1();
		loaded_tag =
		    FPU_load_int16((short __user *)data_address, &loaded_data);
		FPU_copy_to_reg0(&loaded_data, loaded_tag);
		break;
	/* case 004: undefined (d9 /1) */
	/* fisttp are enabled if CPUID(1).ECX(0) "sse3" is set */
	case 005:		/* fisttp m32int (db /1) */
		clear_C1();
		sv_cw = control_word;
		control_word |= RC_CHOP;
		if (FPU_store_int32
		    (st0_ptr, st0_tag, (long __user *)data_address))
			pop_0();	/* pop only if the number was actually stored
					   (see the 80486 manual p16-28) */
		control_word = sv_cw;
		break;
	case 006:		/* fisttp m64int (dd /1) */
		clear_C1();
		sv_cw = control_word;
		control_word |= RC_CHOP;
		if (FPU_store_int64
		    (st0_ptr, st0_tag, (long long __user *)data_address))
			pop_0();	/* pop only if the number was actually stored
					   (see the 80486 manual p16-28) */
		control_word = sv_cw;
		break;
	case 007:		/* fisttp m16int (df /1) */
		clear_C1();
		sv_cw = control_word;
		control_word |= RC_CHOP;
		if (FPU_store_int16
		    (st0_ptr, st0_tag, (short __user *)data_address))
			pop_0();	/* pop only if the number was actually stored
					   (see the 80486 manual p16-28) */
		control_word = sv_cw;
		break;
	case 010:		/* fst m32real */
		clear_C1();
		FPU_store_single(st0_ptr, st0_tag,
				 (float __user *)data_address);
		break;
	case 011:		/* fist m32int */
		clear_C1();
		FPU_store_int32(st0_ptr, st0_tag, (long __user *)data_address);
		break;
	case 012:		/* fst m64real */
		clear_C1();
		FPU_store_double(st0_ptr, st0_tag,
				 (double __user *)data_address);
		break;
	case 013:		/* fist m16int */
		clear_C1();
		FPU_store_int16(st0_ptr, st0_tag, (short __user *)data_address);
		break;
	case 014:		/* fstp m32real */
		clear_C1();
		if (FPU_store_single
		    (st0_ptr, st0_tag, (float __user *)data_address))
			pop_0();	/* pop only if the number was actually stored
					   (see the 80486 manual p16-28) */
		break;
	case 015:		/* fistp m32int */
		clear_C1();
		if (FPU_store_int32
		    (st0_ptr, st0_tag, (long __user *)data_address))
			pop_0();	/* pop only if the number was actually stored
					   (see the 80486 manual p16-28) */
		break;
	case 016:		/* fstp m64real */
		clear_C1();
		if (FPU_store_double
		    (st0_ptr, st0_tag, (double __user *)data_address))
			pop_0();	/* pop only if the number was actually stored
					   (see the 80486 manual p16-28) */
		break;
	case 017:		/* fistp m16int */
		clear_C1();
		if (FPU_store_int16
		    (st0_ptr, st0_tag, (short __user *)data_address))
			pop_0();	/* pop only if the number was actually stored
					   (see the 80486 manual p16-28) */
		break;
	case 020:		/* fldenv  m14/28byte */
		fldenv(addr_modes, (u_char __user *) data_address);
		/* Ensure that the values just loaded are not changed by
		   fix-up operations. */
		return 1;
	case 022:		/* frstor m94/108byte */
		frstor(addr_modes, (u_char __user *) data_address);
		/* Ensure that the values just loaded are not changed by
		   fix-up operations. */
		return 1;
	case 023:		/* fbld m80dec */
		clear_C1();
		loaded_tag = FPU_load_bcd((u_char __user *) data_address);
		FPU_settag0(loaded_tag);
		break;
	case 024:		/* fldcw */
		RE_ENTRANT_CHECK_OFF;
		FPU_access_ok(data_address, 2);
		FPU_get_user(control_word,
			     (unsigned short __user *)data_address);
		RE_ENTRANT_CHECK_ON;
		if (partial_status & ~control_word & CW_Exceptions)
			partial_status |= (SW_Summary | SW_Backward);
		else
			partial_status &= ~(SW_Summary | SW_Backward);
#ifdef PECULIAR_486
		control_word |= 0x40;	/* An 80486 appears to always set this bit */
#endif /* PECULIAR_486 */
		return 1;
	case 025:		/* fld m80real */
		clear_C1();
		loaded_tag =
		    FPU_load_extended((long double __user *)data_address, 0);
		FPU_settag0(loaded_tag);
		break;
	case 027:		/* fild m64int */
		clear_C1();
		loaded_tag = FPU_load_int64((long long __user *)data_address);
		if (loaded_tag == TAG_Error)
			return 0;
		FPU_settag0(loaded_tag);
		break;
	case 030:		/* fstenv  m14/28byte */
		fstenv(addr_modes, (u_char __user *) data_address);
		return 1;
	case 032:		/* fsave */
		fsave(addr_modes, (u_char __user *) data_address);
		return 1;
	case 033:		/* fbstp m80dec */
		clear_C1();
		if (FPU_store_bcd
		    (st0_ptr, st0_tag, (u_char __user *) data_address))
			pop_0();	/* pop only if the number was actually stored
					   (see the 80486 manual p16-28) */
		break;
	case 034:		/* fstcw m16int */
		RE_ENTRANT_CHECK_OFF;
		FPU_access_ok(data_address, 2);
		FPU_put_user(control_word,
			     (unsigned short __user *)data_address);
		RE_ENTRANT_CHECK_ON;
		return 1;
	case 035:		/* fstp m80real */
		clear_C1();
		if (FPU_store_extended
		    (st0_ptr, st0_tag, (long double __user *)data_address))
			pop_0();	/* pop only if the number was actually stored
					   (see the 80486 manual p16-28) */
		break;
	case 036:		/* fstsw m2byte */
		RE_ENTRANT_CHECK_OFF;
		FPU_access_ok(data_address, 2);
		FPU_put_user(status_word(),
			     (unsigned short __user *)data_address);
		RE_ENTRANT_CHECK_ON;
		return 1;
	case 037:		/* fistp m64int */
		clear_C1();
		if (FPU_store_int64
		    (st0_ptr, st0_tag, (long long __user *)data_address))
			pop_0();	/* pop only if the number was actually stored
					   (see the 80486 manual p16-28) */
		break;
	}
	return 0;
}
