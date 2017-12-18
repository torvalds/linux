/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_ASM_DSEMUL_H__
#define __MIPS_ASM_DSEMUL_H__

#include <asm/break.h>
#include <asm/inst.h>

/* Break instruction with special math emu break code set */
#define BREAK_MATH(micromips)	(((micromips) ? 0x7 : 0xd) | (BRK_MEMU << 16))

/* When used as a frame index, indicates the lack of a frame */
#define BD_EMUFRAME_NONE	((int)BIT(31))

struct mm_struct;
struct pt_regs;
struct task_struct;

/**
 * mips_dsemul() - 'Emulate' an instruction from a branch delay slot
 * @regs:	User thread register context.
 * @ir:		The instruction to be 'emulated'.
 * @branch_pc:	The PC of the branch instruction.
 * @cont_pc:	The PC to continue at following 'emulation'.
 *
 * Emulate or execute an arbitrary MIPS instruction within the context of
 * the current user thread. This is used primarily to handle instructions
 * in the delay slots of emulated branch instructions, for example FP
 * branch instructions on systems without an FPU.
 *
 * Return: Zero on success, negative if ir is a NOP, signal number on failure.
 */
extern int mips_dsemul(struct pt_regs *regs, mips_instruction ir,
		       unsigned long branch_pc, unsigned long cont_pc);

/**
 * do_dsemulret() - Return from a delay slot 'emulation' frame
 * @xcp:	User thread register context.
 *
 * Call in response to the BRK_MEMU break instruction used to return to
 * the kernel from branch delay slot 'emulation' frames following a call
 * to mips_dsemul(). Restores the user thread PC to the value that was
 * passed as the cpc parameter to mips_dsemul().
 *
 * Return: True if an emulation frame was returned from, else false.
 */
extern bool do_dsemulret(struct pt_regs *xcp);

/**
 * dsemul_thread_cleanup() - Cleanup thread 'emulation' frame
 * @tsk: The task structure associated with the thread
 *
 * If the thread @tsk has a branch delay slot 'emulation' frame
 * allocated to it then free that frame.
 *
 * Return: True if a frame was freed, else false.
 */
extern bool dsemul_thread_cleanup(struct task_struct *tsk);

/**
 * dsemul_thread_rollback() - Rollback from an 'emulation' frame
 * @regs:	User thread register context.
 *
 * If the current thread, whose register context is represented by @regs,
 * is executing within a delay slot 'emulation' frame then exit that
 * frame. The PC will be rolled back to the branch if the instruction
 * that was being 'emulated' has not yet executed, or advanced to the
 * continuation PC if it has.
 *
 * Return: True if a frame was exited, else false.
 */
extern bool dsemul_thread_rollback(struct pt_regs *regs);

/**
 * dsemul_mm_cleanup() - Cleanup per-mm delay slot 'emulation' state
 * @mm:		The struct mm_struct to cleanup state for.
 *
 * Cleanup state for the given @mm, ensuring that any memory allocated
 * for delay slot 'emulation' book-keeping is freed. This is to be called
 * before @mm is freed in order to avoid memory leaks.
 */
extern void dsemul_mm_cleanup(struct mm_struct *mm);

#endif /* __MIPS_ASM_DSEMUL_H__ */
