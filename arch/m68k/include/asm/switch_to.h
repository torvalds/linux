/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_SWITCH_TO_H
#define _M68K_SWITCH_TO_H

/*
 * switch_to(n) should switch tasks to task ptr, first checking that
 * ptr isn't the current task, in which case it does nothing.  This
 * also clears the TS-flag if the task we switched to has used the
 * math co-processor latest.
 */
/*
 * switch_to() saves the extra registers, that are not saved
 * automatically by SAVE_SWITCH_STACK in resume(), ie. d0-d5 and
 * a0-a1. Some of these are used by schedule() and its predecessors
 * and so we might get see unexpected behaviors when a task returns
 * with unexpected register values.
 *
 * syscall stores these registers itself and none of them are used
 * by syscall after the function in the syscall has been called.
 *
 * Beware that resume now expects *next to be in d1 and the offset of
 * tss to be in a1. This saves a few instructions as we no longer have
 * to push them onto the stack and read them back right after.
 *
 * 02/17/96 - Jes Sorensen (jds@kom.auc.dk)
 *
 * Changed 96/09/19 by Andreas Schwab
 * pass prev in a0, next in a1
 */
asmlinkage void resume(void);
#define switch_to(prev,next,last) do { \
  register void *_prev __asm__ ("a0") = (prev); \
  register void *_next __asm__ ("a1") = (next); \
  register void *_last __asm__ ("d1"); \
  __asm__ __volatile__("jbsr resume" \
		       : "=a" (_prev), "=a" (_next), "=d" (_last) \
		       : "0" (_prev), "1" (_next) \
		       : "d0", "d2", "d3", "d4", "d5"); \
  (last) = _last; \
} while (0)

#endif /* _M68K_SWITCH_TO_H */
