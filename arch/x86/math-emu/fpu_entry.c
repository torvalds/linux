/*---------------------------------------------------------------------------+
 |  fpu_entry.c                                                              |
 |                                                                           |
 | The entry functions for wm-FPU-emu                                        |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1996,1997                                    |
 |                  W. Metzenthen, 22 Parker St, Ormond, Vic 3163, Australia |
 |                  E-mail   billm@suburbia.net                              |
 |                                                                           |
 | See the files "README" and "COPYING" for further copyright and warranty   |
 | information.                                                              |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 | Note:                                                                     |
 |    The file contains code which accesses user memory.                     |
 |    Emulator static data may change when user memory is accessed, due to   |
 |    other processes using the emulator while swapping is in progress.      |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 | math_emulate(), restore_i387_soft() and save_i387_soft() are the only     |
 | entry points for wm-FPU-emu.                                              |
 +---------------------------------------------------------------------------*/

#include <linux/signal.h>
#include <linux/regset.h>

#include <asm/uaccess.h>
#include <asm/desc.h>
#include <asm/user.h>

#include "fpu_system.h"
#include "fpu_emu.h"
#include "exception.h"
#include "control_w.h"
#include "status_w.h"

#define __BAD__ FPU_illegal	/* Illegal on an 80486, causes SIGILL */

#ifndef NO_UNDOC_CODE		/* Un-documented FPU op-codes supported by default. */

/* WARNING: These codes are not documented by Intel in their 80486 manual
   and may not work on FPU clones or later Intel FPUs. */

/* Changes to support the un-doc codes provided by Linus Torvalds. */

#define _d9_d8_ fstp_i		/* unofficial code (19) */
#define _dc_d0_ fcom_st		/* unofficial code (14) */
#define _dc_d8_ fcompst		/* unofficial code (1c) */
#define _dd_c8_ fxch_i		/* unofficial code (0d) */
#define _de_d0_ fcompst		/* unofficial code (16) */
#define _df_c0_ ffreep		/* unofficial code (07) ffree + pop */
#define _df_c8_ fxch_i		/* unofficial code (0f) */
#define _df_d0_ fstp_i		/* unofficial code (17) */
#define _df_d8_ fstp_i		/* unofficial code (1f) */

static FUNC const st_instr_table[64] = {
	fadd__, fld_i_, __BAD__, __BAD__, fadd_i, ffree_, faddp_, _df_c0_,
	fmul__, fxch_i, __BAD__, __BAD__, fmul_i, _dd_c8_, fmulp_, _df_c8_,
	fcom_st, fp_nop, __BAD__, __BAD__, _dc_d0_, fst_i_, _de_d0_, _df_d0_,
	fcompst, _d9_d8_, __BAD__, __BAD__, _dc_d8_, fstp_i, fcompp, _df_d8_,
	fsub__, FPU_etc, __BAD__, finit_, fsubri, fucom_, fsubrp, fstsw_,
	fsubr_, fconst, fucompp, __BAD__, fsub_i, fucomp, fsubp_, __BAD__,
	fdiv__, FPU_triga, __BAD__, __BAD__, fdivri, __BAD__, fdivrp, __BAD__,
	fdivr_, FPU_trigb, __BAD__, __BAD__, fdiv_i, __BAD__, fdivp_, __BAD__,
};

#else /* Support only documented FPU op-codes */

static FUNC const st_instr_table[64] = {
	fadd__, fld_i_, __BAD__, __BAD__, fadd_i, ffree_, faddp_, __BAD__,
	fmul__, fxch_i, __BAD__, __BAD__, fmul_i, __BAD__, fmulp_, __BAD__,
	fcom_st, fp_nop, __BAD__, __BAD__, __BAD__, fst_i_, __BAD__, __BAD__,
	fcompst, __BAD__, __BAD__, __BAD__, __BAD__, fstp_i, fcompp, __BAD__,
	fsub__, FPU_etc, __BAD__, finit_, fsubri, fucom_, fsubrp, fstsw_,
	fsubr_, fconst, fucompp, __BAD__, fsub_i, fucomp, fsubp_, __BAD__,
	fdiv__, FPU_triga, __BAD__, __BAD__, fdivri, __BAD__, fdivrp, __BAD__,
	fdivr_, FPU_trigb, __BAD__, __BAD__, fdiv_i, __BAD__, fdivp_, __BAD__,
};

#endif /* NO_UNDOC_CODE */

#define _NONE_ 0		/* Take no special action */
#define _REG0_ 1		/* Need to check for not empty st(0) */
#define _REGI_ 2		/* Need to check for not empty st(0) and st(rm) */
#define _REGi_ 0		/* Uses st(rm) */
#define _PUSH_ 3		/* Need to check for space to push onto stack */
#define _null_ 4		/* Function illegal or not implemented */
#define _REGIi 5		/* Uses st(0) and st(rm), result to st(rm) */
#define _REGIp 6		/* Uses st(0) and st(rm), result to st(rm) then pop */
#define _REGIc 0		/* Compare st(0) and st(rm) */
#define _REGIn 0		/* Uses st(0) and st(rm), but handle checks later */

#ifndef NO_UNDOC_CODE

/* Un-documented FPU op-codes supported by default. (see above) */

static u_char const type_table[64] = {
	_REGI_, _NONE_, _null_, _null_, _REGIi, _REGi_, _REGIp, _REGi_,
	_REGI_, _REGIn, _null_, _null_, _REGIi, _REGI_, _REGIp, _REGI_,
	_REGIc, _NONE_, _null_, _null_, _REGIc, _REG0_, _REGIc, _REG0_,
	_REGIc, _REG0_, _null_, _null_, _REGIc, _REG0_, _REGIc, _REG0_,
	_REGI_, _NONE_, _null_, _NONE_, _REGIi, _REGIc, _REGIp, _NONE_,
	_REGI_, _NONE_, _REGIc, _null_, _REGIi, _REGIc, _REGIp, _null_,
	_REGI_, _NONE_, _null_, _null_, _REGIi, _null_, _REGIp, _null_,
	_REGI_, _NONE_, _null_, _null_, _REGIi, _null_, _REGIp, _null_
};

#else /* Support only documented FPU op-codes */

static u_char const type_table[64] = {
	_REGI_, _NONE_, _null_, _null_, _REGIi, _REGi_, _REGIp, _null_,
	_REGI_, _REGIn, _null_, _null_, _REGIi, _null_, _REGIp, _null_,
	_REGIc, _NONE_, _null_, _null_, _null_, _REG0_, _null_, _null_,
	_REGIc, _null_, _null_, _null_, _null_, _REG0_, _REGIc, _null_,
	_REGI_, _NONE_, _null_, _NONE_, _REGIi, _REGIc, _REGIp, _NONE_,
	_REGI_, _NONE_, _REGIc, _null_, _REGIi, _REGIc, _REGIp, _null_,
	_REGI_, _NONE_, _null_, _null_, _REGIi, _null_, _REGIp, _null_,
	_REGI_, _NONE_, _null_, _null_, _REGIi, _null_, _REGIp, _null_
};

#endif /* NO_UNDOC_CODE */

#ifdef RE_ENTRANT_CHECKING
u_char emulating = 0;
#endif /* RE_ENTRANT_CHECKING */

static int valid_prefix(u_char *Byte, u_char __user ** fpu_eip,
			overrides * override);

asmlinkage void math_emulate(long arg)
{
	u_char FPU_modrm, byte1;
	unsigned short code;
	fpu_addr_modes addr_modes;
	int unmasked;
	FPU_REG loaded_data;
	FPU_REG *st0_ptr;
	u_char loaded_tag, st0_tag;
	void __user *data_address;
	struct address data_sel_off;
	struct address entry_sel_off;
	unsigned long code_base = 0;
	unsigned long code_limit = 0;	/* Initialized to stop compiler warnings */
	struct desc_struct code_descriptor;

#ifdef RE_ENTRANT_CHECKING
	if (emulating) {
		printk("ERROR: wm-FPU-emu is not RE-ENTRANT!\n");
	}
	RE_ENTRANT_CHECK_ON;
#endif /* RE_ENTRANT_CHECKING */

	if (!used_math()) {
		finit();
		set_used_math();
	}

	SETUP_DATA_AREA(arg);

	FPU_ORIG_EIP = FPU_EIP;

	if ((FPU_EFLAGS & 0x00020000) != 0) {
		/* Virtual 8086 mode */
		addr_modes.default_mode = VM86;
		FPU_EIP += code_base = FPU_CS << 4;
		code_limit = code_base + 0xffff;	/* Assumes code_base <= 0xffff0000 */
	} else if (FPU_CS == __USER_CS && FPU_DS == __USER_DS) {
		addr_modes.default_mode = 0;
	} else if (FPU_CS == __KERNEL_CS) {
		printk("math_emulate: %04x:%08lx\n", FPU_CS, FPU_EIP);
		panic("Math emulation needed in kernel");
	} else {

		if ((FPU_CS & 4) != 4) {	/* Must be in the LDT */
			/* Can only handle segmented addressing via the LDT
			   for now, and it must be 16 bit */
			printk("FPU emulator: Unsupported addressing mode\n");
			math_abort(FPU_info, SIGILL);
		}

		code_descriptor = LDT_DESCRIPTOR(FPU_CS);
		if (SEG_D_SIZE(code_descriptor)) {
			/* The above test may be wrong, the book is not clear */
			/* Segmented 32 bit protected mode */
			addr_modes.default_mode = SEG32;
		} else {
			/* 16 bit protected mode */
			addr_modes.default_mode = PM16;
		}
		FPU_EIP += code_base = SEG_BASE_ADDR(code_descriptor);
		code_limit = code_base
		    + (SEG_LIMIT(code_descriptor) +
		       1) * SEG_GRANULARITY(code_descriptor)
		    - 1;
		if (code_limit < code_base)
			code_limit = 0xffffffff;
	}

	FPU_lookahead = !(FPU_EFLAGS & X86_EFLAGS_TF);

	if (!valid_prefix(&byte1, (u_char __user **) & FPU_EIP,
			  &addr_modes.override)) {
		RE_ENTRANT_CHECK_OFF;
		printk
		    ("FPU emulator: Unknown prefix byte 0x%02x, probably due to\n"
		     "FPU emulator: self-modifying code! (emulation impossible)\n",
		     byte1);
		RE_ENTRANT_CHECK_ON;
		EXCEPTION(EX_INTERNAL | 0x126);
		math_abort(FPU_info, SIGILL);
	}

      do_another_FPU_instruction:

	no_ip_update = 0;

	FPU_EIP++;		/* We have fetched the prefix and first code bytes. */

	if (addr_modes.default_mode) {
		/* This checks for the minimum instruction bytes.
		   We also need to check any extra (address mode) code access. */
		if (FPU_EIP > code_limit)
			math_abort(FPU_info, SIGSEGV);
	}

	if ((byte1 & 0xf8) != 0xd8) {
		if (byte1 == FWAIT_OPCODE) {
			if (partial_status & SW_Summary)
				goto do_the_FPU_interrupt;
			else
				goto FPU_fwait_done;
		}
#ifdef PARANOID
		EXCEPTION(EX_INTERNAL | 0x128);
		math_abort(FPU_info, SIGILL);
#endif /* PARANOID */
	}

	RE_ENTRANT_CHECK_OFF;
	FPU_code_access_ok(1);
	FPU_get_user(FPU_modrm, (u_char __user *) FPU_EIP);
	RE_ENTRANT_CHECK_ON;
	FPU_EIP++;

	if (partial_status & SW_Summary) {
		/* Ignore the error for now if the current instruction is a no-wait
		   control instruction */
		/* The 80486 manual contradicts itself on this topic,
		   but a real 80486 uses the following instructions:
		   fninit, fnstenv, fnsave, fnstsw, fnstenv, fnclex.
		 */
		code = (FPU_modrm << 8) | byte1;
		if (!((((code & 0xf803) == 0xe003) ||	/* fnclex, fninit, fnstsw */
		       (((code & 0x3003) == 0x3001) &&	/* fnsave, fnstcw, fnstenv,
							   fnstsw */
			((code & 0xc000) != 0xc000))))) {
			/*
			 *  We need to simulate the action of the kernel to FPU
			 *  interrupts here.
			 */
		      do_the_FPU_interrupt:

			FPU_EIP = FPU_ORIG_EIP;	/* Point to current FPU instruction. */

			RE_ENTRANT_CHECK_OFF;
			current->thread.trap_no = 16;
			current->thread.error_code = 0;
			send_sig(SIGFPE, current, 1);
			return;
		}
	}

	entry_sel_off.offset = FPU_ORIG_EIP;
	entry_sel_off.selector = FPU_CS;
	entry_sel_off.opcode = (byte1 << 8) | FPU_modrm;
	entry_sel_off.empty = 0;

	FPU_rm = FPU_modrm & 7;

	if (FPU_modrm < 0300) {
		/* All of these instructions use the mod/rm byte to get a data address */

		if ((addr_modes.default_mode & SIXTEEN)
		    ^ (addr_modes.override.address_size == ADDR_SIZE_PREFIX))
			data_address =
			    FPU_get_address_16(FPU_modrm, &FPU_EIP,
					       &data_sel_off, addr_modes);
		else
			data_address =
			    FPU_get_address(FPU_modrm, &FPU_EIP, &data_sel_off,
					    addr_modes);

		if (addr_modes.default_mode) {
			if (FPU_EIP - 1 > code_limit)
				math_abort(FPU_info, SIGSEGV);
		}

		if (!(byte1 & 1)) {
			unsigned short status1 = partial_status;

			st0_ptr = &st(0);
			st0_tag = FPU_gettag0();

			/* Stack underflow has priority */
			if (NOT_EMPTY_ST0) {
				if (addr_modes.default_mode & PROTECTED) {
					/* This table works for 16 and 32 bit protected mode */
					if (access_limit <
					    data_sizes_16[(byte1 >> 1) & 3])
						math_abort(FPU_info, SIGSEGV);
				}

				unmasked = 0;	/* Do this here to stop compiler warnings. */
				switch ((byte1 >> 1) & 3) {
				case 0:
					unmasked =
					    FPU_load_single((float __user *)
							    data_address,
							    &loaded_data);
					loaded_tag = unmasked & 0xff;
					unmasked &= ~0xff;
					break;
				case 1:
					loaded_tag =
					    FPU_load_int32((long __user *)
							   data_address,
							   &loaded_data);
					break;
				case 2:
					unmasked =
					    FPU_load_double((double __user *)
							    data_address,
							    &loaded_data);
					loaded_tag = unmasked & 0xff;
					unmasked &= ~0xff;
					break;
				case 3:
				default:	/* Used here to suppress gcc warnings. */
					loaded_tag =
					    FPU_load_int16((short __user *)
							   data_address,
							   &loaded_data);
					break;
				}

				/* No more access to user memory, it is safe
				   to use static data now */

				/* NaN operands have the next priority. */
				/* We have to delay looking at st(0) until after
				   loading the data, because that data might contain an SNaN */
				if (((st0_tag == TAG_Special) && isNaN(st0_ptr))
				    || ((loaded_tag == TAG_Special)
					&& isNaN(&loaded_data))) {
					/* Restore the status word; we might have loaded a
					   denormal. */
					partial_status = status1;
					if ((FPU_modrm & 0x30) == 0x10) {
						/* fcom or fcomp */
						EXCEPTION(EX_Invalid);
						setcc(SW_C3 | SW_C2 | SW_C0);
						if ((FPU_modrm & 0x08)
						    && (control_word &
							CW_Invalid))
							FPU_pop();	/* fcomp, masked, so we pop. */
					} else {
						if (loaded_tag == TAG_Special)
							loaded_tag =
							    FPU_Special
							    (&loaded_data);
#ifdef PECULIAR_486
						/* This is not really needed, but gives behaviour
						   identical to an 80486 */
						if ((FPU_modrm & 0x28) == 0x20)
							/* fdiv or fsub */
							real_2op_NaN
							    (&loaded_data,
							     loaded_tag, 0,
							     &loaded_data);
						else
#endif /* PECULIAR_486 */
							/* fadd, fdivr, fmul, or fsubr */
							real_2op_NaN
							    (&loaded_data,
							     loaded_tag, 0,
							     st0_ptr);
					}
					goto reg_mem_instr_done;
				}

				if (unmasked && !((FPU_modrm & 0x30) == 0x10)) {
					/* Is not a comparison instruction. */
					if ((FPU_modrm & 0x38) == 0x38) {
						/* fdivr */
						if ((st0_tag == TAG_Zero) &&
						    ((loaded_tag == TAG_Valid)
						     || (loaded_tag ==
							 TAG_Special
							 &&
							 isdenormal
							 (&loaded_data)))) {
							if (FPU_divide_by_zero
							    (0,
							     getsign
							     (&loaded_data))
							    < 0) {
								/* We use the fact here that the unmasked
								   exception in the loaded data was for a
								   denormal operand */
								/* Restore the state of the denormal op bit */
								partial_status
								    &=
								    ~SW_Denorm_Op;
								partial_status
								    |=
								    status1 &
								    SW_Denorm_Op;
							} else
								setsign(st0_ptr,
									getsign
									(&loaded_data));
						}
					}
					goto reg_mem_instr_done;
				}

				switch ((FPU_modrm >> 3) & 7) {
				case 0:	/* fadd */
					clear_C1();
					FPU_add(&loaded_data, loaded_tag, 0,
						control_word);
					break;
				case 1:	/* fmul */
					clear_C1();
					FPU_mul(&loaded_data, loaded_tag, 0,
						control_word);
					break;
				case 2:	/* fcom */
					FPU_compare_st_data(&loaded_data,
							    loaded_tag);
					break;
				case 3:	/* fcomp */
					if (!FPU_compare_st_data
					    (&loaded_data, loaded_tag)
					    && !unmasked)
						FPU_pop();
					break;
				case 4:	/* fsub */
					clear_C1();
					FPU_sub(LOADED | loaded_tag,
						(int)&loaded_data,
						control_word);
					break;
				case 5:	/* fsubr */
					clear_C1();
					FPU_sub(REV | LOADED | loaded_tag,
						(int)&loaded_data,
						control_word);
					break;
				case 6:	/* fdiv */
					clear_C1();
					FPU_div(LOADED | loaded_tag,
						(int)&loaded_data,
						control_word);
					break;
				case 7:	/* fdivr */
					clear_C1();
					if (st0_tag == TAG_Zero)
						partial_status = status1;	/* Undo any denorm tag,
										   zero-divide has priority. */
					FPU_div(REV | LOADED | loaded_tag,
						(int)&loaded_data,
						control_word);
					break;
				}
			} else {
				if ((FPU_modrm & 0x30) == 0x10) {
					/* The instruction is fcom or fcomp */
					EXCEPTION(EX_StackUnder);
					setcc(SW_C3 | SW_C2 | SW_C0);
					if ((FPU_modrm & 0x08)
					    && (control_word & CW_Invalid))
						FPU_pop();	/* fcomp */
				} else
					FPU_stack_underflow();
			}
		      reg_mem_instr_done:
			operand_address = data_sel_off;
		} else {
			if (!(no_ip_update =
			      FPU_load_store(((FPU_modrm & 0x38) | (byte1 & 6))
					     >> 1, addr_modes, data_address))) {
				operand_address = data_sel_off;
			}
		}

	} else {
		/* None of these instructions access user memory */
		u_char instr_index = (FPU_modrm & 0x38) | (byte1 & 7);

#ifdef PECULIAR_486
		/* This is supposed to be undefined, but a real 80486 seems
		   to do this: */
		operand_address.offset = 0;
		operand_address.selector = FPU_DS;
#endif /* PECULIAR_486 */

		st0_ptr = &st(0);
		st0_tag = FPU_gettag0();
		switch (type_table[(int)instr_index]) {
		case _NONE_:	/* also _REGIc: _REGIn */
			break;
		case _REG0_:
			if (!NOT_EMPTY_ST0) {
				FPU_stack_underflow();
				goto FPU_instruction_done;
			}
			break;
		case _REGIi:
			if (!NOT_EMPTY_ST0 || !NOT_EMPTY(FPU_rm)) {
				FPU_stack_underflow_i(FPU_rm);
				goto FPU_instruction_done;
			}
			break;
		case _REGIp:
			if (!NOT_EMPTY_ST0 || !NOT_EMPTY(FPU_rm)) {
				FPU_stack_underflow_pop(FPU_rm);
				goto FPU_instruction_done;
			}
			break;
		case _REGI_:
			if (!NOT_EMPTY_ST0 || !NOT_EMPTY(FPU_rm)) {
				FPU_stack_underflow();
				goto FPU_instruction_done;
			}
			break;
		case _PUSH_:	/* Only used by the fld st(i) instruction */
			break;
		case _null_:
			FPU_illegal();
			goto FPU_instruction_done;
		default:
			EXCEPTION(EX_INTERNAL | 0x111);
			goto FPU_instruction_done;
		}
		(*st_instr_table[(int)instr_index]) ();

	      FPU_instruction_done:
		;
	}

	if (!no_ip_update)
		instruction_address = entry_sel_off;

      FPU_fwait_done:

#ifdef DEBUG
	RE_ENTRANT_CHECK_OFF;
	FPU_printall();
	RE_ENTRANT_CHECK_ON;
#endif /* DEBUG */

	if (FPU_lookahead && !need_resched()) {
		FPU_ORIG_EIP = FPU_EIP - code_base;
		if (valid_prefix(&byte1, (u_char __user **) & FPU_EIP,
				 &addr_modes.override))
			goto do_another_FPU_instruction;
	}

	if (addr_modes.default_mode)
		FPU_EIP -= code_base;

	RE_ENTRANT_CHECK_OFF;
}

/* Support for prefix bytes is not yet complete. To properly handle
   all prefix bytes, further changes are needed in the emulator code
   which accesses user address space. Access to separate segments is
   important for msdos emulation. */
static int valid_prefix(u_char *Byte, u_char __user **fpu_eip,
			overrides * override)
{
	u_char byte;
	u_char __user *ip = *fpu_eip;

	*override = (overrides) {
	0, 0, PREFIX_DEFAULT};	/* defaults */

	RE_ENTRANT_CHECK_OFF;
	FPU_code_access_ok(1);
	FPU_get_user(byte, ip);
	RE_ENTRANT_CHECK_ON;

	while (1) {
		switch (byte) {
		case ADDR_SIZE_PREFIX:
			override->address_size = ADDR_SIZE_PREFIX;
			goto do_next_byte;

		case OP_SIZE_PREFIX:
			override->operand_size = OP_SIZE_PREFIX;
			goto do_next_byte;

		case PREFIX_CS:
			override->segment = PREFIX_CS_;
			goto do_next_byte;
		case PREFIX_ES:
			override->segment = PREFIX_ES_;
			goto do_next_byte;
		case PREFIX_SS:
			override->segment = PREFIX_SS_;
			goto do_next_byte;
		case PREFIX_FS:
			override->segment = PREFIX_FS_;
			goto do_next_byte;
		case PREFIX_GS:
			override->segment = PREFIX_GS_;
			goto do_next_byte;
		case PREFIX_DS:
			override->segment = PREFIX_DS_;
			goto do_next_byte;

/* lock is not a valid prefix for FPU instructions,
   let the cpu handle it to generate a SIGILL. */
/*	case PREFIX_LOCK: */

			/* rep.. prefixes have no meaning for FPU instructions */
		case PREFIX_REPE:
		case PREFIX_REPNE:

		      do_next_byte:
			ip++;
			RE_ENTRANT_CHECK_OFF;
			FPU_code_access_ok(1);
			FPU_get_user(byte, ip);
			RE_ENTRANT_CHECK_ON;
			break;
		case FWAIT_OPCODE:
			*Byte = byte;
			return 1;
		default:
			if ((byte & 0xf8) == 0xd8) {
				*Byte = byte;
				*fpu_eip = ip;
				return 1;
			} else {
				/* Not a valid sequence of prefix bytes followed by
				   an FPU instruction. */
				*Byte = byte;	/* Needed for error message. */
				return 0;
			}
		}
	}
}

void math_abort(struct info *info, unsigned int signal)
{
	FPU_EIP = FPU_ORIG_EIP;
	current->thread.trap_no = 16;
	current->thread.error_code = 0;
	send_sig(signal, current, 1);
	RE_ENTRANT_CHECK_OFF;
      __asm__("movl %0,%%esp ; ret": :"g"(((long)info) - 4));
#ifdef PARANOID
	printk("ERROR: wm-FPU-emu math_abort failed!\n");
#endif /* PARANOID */
}

#define S387 ((struct i387_soft_struct *)s387)
#define sstatus_word() \
  ((S387->swd & ~SW_Top & 0xffff) | ((S387->ftop << SW_Top_Shift) & SW_Top))

int fpregs_soft_set(struct task_struct *target,
		    const struct user_regset *regset,
		    unsigned int pos, unsigned int count,
		    const void *kbuf, const void __user *ubuf)
{
	struct i387_soft_struct *s387 = &target->thread.i387.soft;
	void *space = s387->st_space;
	int ret;
	int offset, other, i, tags, regnr, tag, newtop;

	RE_ENTRANT_CHECK_OFF;
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, s387, 0,
				 offsetof(struct i387_soft_struct, st_space));
	RE_ENTRANT_CHECK_ON;

	if (ret)
		return ret;

	S387->ftop = (S387->swd >> SW_Top_Shift) & 7;
	offset = (S387->ftop & 7) * 10;
	other = 80 - offset;

	RE_ENTRANT_CHECK_OFF;

	/* Copy all registers in stack order. */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 space + offset, 0, other);
	if (!ret && offset)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 space, 0, offset);

	RE_ENTRANT_CHECK_ON;

	/* The tags may need to be corrected now. */
	tags = S387->twd;
	newtop = S387->ftop;
	for (i = 0; i < 8; i++) {
		regnr = (i + newtop) & 7;
		if (((tags >> ((regnr & 7) * 2)) & 3) != TAG_Empty) {
			/* The loaded data over-rides all other cases. */
			tag =
			    FPU_tagof((FPU_REG *) ((u_char *) S387->st_space +
						   10 * regnr));
			tags &= ~(3 << (regnr * 2));
			tags |= (tag & 3) << (regnr * 2);
		}
	}
	S387->twd = tags;

	return ret;
}

int fpregs_soft_get(struct task_struct *target,
		    const struct user_regset *regset,
		    unsigned int pos, unsigned int count,
		    void *kbuf, void __user *ubuf)
{
	struct i387_soft_struct *s387 = &target->thread.i387.soft;
	const void *space = s387->st_space;
	int ret;
	int offset = (S387->ftop & 7) * 10, other = 80 - offset;

	RE_ENTRANT_CHECK_OFF;

#ifdef PECULIAR_486
	S387->cwd &= ~0xe080;
	/* An 80486 sets nearly all of the reserved bits to 1. */
	S387->cwd |= 0xffff0040;
	S387->swd = sstatus_word() | 0xffff0000;
	S387->twd |= 0xffff0000;
	S387->fcs &= ~0xf8000000;
	S387->fos |= 0xffff0000;
#endif /* PECULIAR_486 */

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, s387, 0,
				  offsetof(struct i387_soft_struct, st_space));

	/* Copy all registers in stack order. */
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  space + offset, 0, other);
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  space, 0, offset);

	RE_ENTRANT_CHECK_ON;

	return ret;
}
