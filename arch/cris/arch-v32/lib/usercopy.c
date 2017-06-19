/*
 * User address space access functions.
 * The non-inlined parts of asm-cris/uaccess.h are here.
 *
 * Copyright (C) 2000, 2003 Axis Communications AB.
 *
 * Written by Hans-Peter Nilsson.
 * Pieces used from memcpy, originally by Kenny Ranerup long time ago.
 */

#include <linux/uaccess.h>

/* Asm:s have been tweaked (within the domain of correctness) to give
   satisfactory results for "gcc version 3.2.1 Axis release R53/1.53-v32".

   Check regularly...

   Note that for CRISv32, the PC saved at a bus-fault is the address
   *at* the faulting instruction, with a special case for instructions
   in delay slots: then it's the address of the branch.  Note also that
   in contrast to v10, a postincrement in the instruction is *not*
   performed at a bus-fault; the register is seen having the original
   value in fault handlers.  */


/* Copy to userspace.  This is based on the memcpy used for
   kernel-to-kernel copying; see "string.c".  */

unsigned long __copy_user(void __user *pdst, const void *psrc, unsigned long pn)
{
  /* We want the parameters put in special registers.
     Make sure the compiler is able to make something useful of this.
     As it is now: r10 -> r13; r11 -> r11 (nop); r12 -> r12 (nop).

     FIXME: Comment for old gcc version.  Check.
     If gcc was alright, it really would need no temporaries, and no
     stack space to save stuff on. */

  register char *dst __asm__ ("r13") = pdst;
  register const char *src __asm__ ("r11") = psrc;
  register int n __asm__ ("r12") = pn;
  register int retn __asm__ ("r10") = 0;


  /* When src is aligned but not dst, this makes a few extra needless
     cycles.  I believe it would take as many to check that the
     re-alignment was unnecessary.  */
  if (((unsigned long) dst & 3) != 0
      /* Don't align if we wouldn't copy more than a few bytes; so we
	 don't have to check further for overflows.  */
      && n >= 3)
  {
    if ((unsigned long) dst & 1)
    {
      __asm_copy_to_user_1 (dst, src, retn);
      n--;
    }

    if ((unsigned long) dst & 2)
    {
      __asm_copy_to_user_2 (dst, src, retn);
      n -= 2;
    }
  }

  /* Movem is dirt cheap.  The overheap is low enough to always use the
     minimum possible block size as the threshold.  */
  if (n >= 44)
  {
    /* For large copies we use 'movem'.  */

    /* It is not optimal to tell the compiler about clobbering any
       registers; that will move the saving/restoring of those registers
       to the function prologue/epilogue, and make non-movem sizes
       suboptimal.  */
    __asm__ volatile ("\
        ;; Check that the register asm declaration got right.		\n\
        ;; The GCC manual explicitly says TRT will happen.		\n\
	.ifnc %0%1%2%3,$r13$r11$r12$r10					\n\
	.err								\n\
	.endif								\n\
									\n\
	;; Save the registers we'll use in the movem process		\n\
	;; on the stack.						\n\
	subq	11*4,$sp						\n\
	movem	$r10,[$sp]						\n\
									\n\
	;; Now we've got this:						\n\
	;; r11 - src							\n\
	;; r13 - dst							\n\
	;; r12 - n							\n\
									\n\
	;; Update n for the first loop					\n\
	subq	44,$r12							\n\
0:									\n\
	movem	[$r11+],$r10						\n\
	subq   44,$r12							\n\
1:	bge	0b							\n\
	movem	$r10,[$r13+]						\n\
3:									\n\
	addq   44,$r12  ;; compensate for last loop underflowing n	\n\
									\n\
	;; Restore registers from stack					\n\
	movem [$sp+],$r10						\n\
2:									\n\
	.section .fixup,\"ax\"						\n\
4:									\n\
; When failing on any of the 1..44 bytes in a chunk, we adjust back the	\n\
; source pointer and just drop through	to the by-16 and by-4 loops to	\n\
; get the correct number of failing bytes.  This necessarily means a	\n\
; few extra exceptions, but invalid user pointers shouldn't happen in	\n\
; time-critical code anyway.						\n\
	jump 3b								\n\
	subq 44,$r11							\n\
									\n\
	.previous							\n\
	.section __ex_table,\"a\"					\n\
	.dword 1b,4b							\n\
	.previous"

     /* Outputs */ : "=r" (dst), "=r" (src), "=r" (n), "=r" (retn)
     /* Inputs */ : "0" (dst), "1" (src), "2" (n), "3" (retn));

  }

  while (n >= 16)
  {
    __asm_copy_to_user_16 (dst, src, retn);
    n -= 16;
  }

  /* Having a separate by-four loops cuts down on cache footprint.
     FIXME:  Test with and without; increasing switch to be 0..15.  */
  while (n >= 4)
  {
    __asm_copy_to_user_4 (dst, src, retn);
    n -= 4;
  }

  switch (n)
  {
    case 0:
      break;
    case 1:
      __asm_copy_to_user_1 (dst, src, retn);
      break;
    case 2:
      __asm_copy_to_user_2 (dst, src, retn);
      break;
    case 3:
      __asm_copy_to_user_3 (dst, src, retn);
      break;
  }

  return retn;
}
EXPORT_SYMBOL(__copy_user);

/* Copy from user to kernel.  The return-value is the number of bytes that were
   inaccessible.  */
unsigned long __copy_user_in(void *pdst, const void __user *psrc,
				  unsigned long pn)
{
  /* We want the parameters put in special registers.
     Make sure the compiler is able to make something useful of this.
     As it is now: r10 -> r13; r11 -> r11 (nop); r12 -> r12 (nop).

     FIXME: Comment for old gcc version.  Check.
     If gcc was alright, it really would need no temporaries, and no
     stack space to save stuff on.  */

  register char *dst __asm__ ("r13") = pdst;
  register const char *src __asm__ ("r11") = psrc;
  register int n __asm__ ("r12") = pn;
  register int retn __asm__ ("r10") = 0;

  /* The best reason to align src is that we then know that a read-fault
     was for aligned bytes; there's no 1..3 remaining good bytes to
     pickle.  */
  if (((unsigned long) src & 3) != 0)
  {
    if (((unsigned long) src & 1) && n != 0)
    {
      __asm_copy_from_user_1 (dst, src, retn);
      n--;
      if (retn != 0)
        goto exception;
    }

    if (((unsigned long) src & 2) && n >= 2)
    {
      __asm_copy_from_user_2 (dst, src, retn);
      n -= 2;
      if (retn != 0)
        goto exception;
    }

  }

  /* Movem is dirt cheap.  The overheap is low enough to always use the
     minimum possible block size as the threshold.  */
  if (n >= 44)
  {
    /* It is not optimal to tell the compiler about clobbering any
       registers; that will move the saving/restoring of those registers
       to the function prologue/epilogue, and make non-movem sizes
       suboptimal.  */
    __asm__ volatile ("\
	.ifnc %0%1%2%3,$r13$r11$r12$r10					\n\
	.err								\n\
	.endif								\n\
									\n\
	;; Save the registers we'll use in the movem process		\n\
	;; on the stack.						\n\
	subq	11*4,$sp						\n\
	movem	$r10,[$sp]						\n\
									\n\
	;; Now we've got this:						\n\
	;; r11 - src							\n\
	;; r13 - dst							\n\
	;; r12 - n							\n\
									\n\
	;; Update n for the first loop					\n\
	subq	44,$r12							\n\
0:									\n\
	movem	[$r11+],$r10						\n\
									\n\
	subq   44,$r12							\n\
	bge	0b							\n\
	movem	$r10,[$r13+]						\n\
									\n\
4:									\n\
	addq   44,$r12  ;; compensate for last loop underflowing n	\n\
									\n\
	;; Restore registers from stack					\n\
	movem [$sp+],$r10						\n\
	.section .fixup,\"ax\"						\n\
									\n\
;; Do not jump back into the loop if we fail.  For some uses, we get a	\n\
;; page fault somewhere on the line.  Without checking for page limits,	\n\
;; we don't know where, but we need to copy accurately and keep an	\n\
;; accurate count; not just clear the whole line.  To do that, we fall	\n\
;; down in the code below, proceeding with smaller amounts.  It should	\n\
;; be kept in mind that we have to cater to code like what at one time	\n\
;; was in fs/super.c:							\n\
;;  i = size - copy_from_user((void *)page, data, size);		\n\
;; which would cause repeated faults while clearing the remainder of	\n\
;; the SIZE bytes at PAGE after the first fault.			\n\
;; A caveat here is that we must not fall through from a failing page	\n\
;; to a valid page.							\n\
									\n\
3:									\n\
	jump	4b ;; Fall through, pretending the fault didn't happen.	\n\
	nop								\n\
									\n\
	.previous							\n\
	.section __ex_table,\"a\"					\n\
	.dword 0b,3b							\n\
	.previous"

     /* Outputs */ : "=r" (dst), "=r" (src), "=r" (n), "=r" (retn)
     /* Inputs */ : "0" (dst), "1" (src), "2" (n), "3" (retn));
  }

  /* Either we directly start copying here, using dword copying in a loop,
     or we copy as much as possible with 'movem' and then the last block
     (<44 bytes) is copied here.  This will work since 'movem' will have
     updated src, dst and n.  (Except with failing src.)

     Since we want to keep src accurate, we can't use
     __asm_copy_from_user_N with N != (1, 2, 4); it updates dst and
     retn, but not src (by design; it's value is ignored elsewhere).  */

  while (n >= 4)
  {
    __asm_copy_from_user_4 (dst, src, retn);
    n -= 4;

    if (retn)
      goto exception;
  }

  /* If we get here, there were no memory read faults.  */
  switch (n)
  {
    /* These copies are at least "naturally aligned" (so we don't have
       to check each byte), due to the src alignment code before the
       movem loop.  The *_3 case *will* get the correct count for retn.  */
    case 0:
      /* This case deliberately left in (if you have doubts check the
	 generated assembly code).  */
      break;
    case 1:
      __asm_copy_from_user_1 (dst, src, retn);
      break;
    case 2:
      __asm_copy_from_user_2 (dst, src, retn);
      break;
    case 3:
      __asm_copy_from_user_3 (dst, src, retn);
      break;
  }

  /* If we get here, retn correctly reflects the number of failing
     bytes.  */
  return retn;

exception:
  return retn + n;
}
EXPORT_SYMBOL(__copy_user_in);

/* Zero userspace.  */
unsigned long __do_clear_user(void __user *pto, unsigned long pn)
{
  /* We want the parameters put in special registers.
     Make sure the compiler is able to make something useful of this.
      As it is now: r10 -> r13; r11 -> r11 (nop); r12 -> r12 (nop).

     FIXME: Comment for old gcc version.  Check.
     If gcc was alright, it really would need no temporaries, and no
     stack space to save stuff on. */

  register char *dst __asm__ ("r13") = pto;
  register int n __asm__ ("r12") = pn;
  register int retn __asm__ ("r10") = 0;


  if (((unsigned long) dst & 3) != 0
     /* Don't align if we wouldn't copy more than a few bytes.  */
      && n >= 3)
  {
    if ((unsigned long) dst & 1)
    {
      __asm_clear_1 (dst, retn);
      n--;
    }

    if ((unsigned long) dst & 2)
    {
      __asm_clear_2 (dst, retn);
      n -= 2;
    }
  }

  /* Decide which copying method to use.
     FIXME: This number is from the "ordinary" kernel memset.  */
  if (n >= 48)
  {
    /* For large clears we use 'movem' */

    /* It is not optimal to tell the compiler about clobbering any
       call-saved registers; that will move the saving/restoring of
       those registers to the function prologue/epilogue, and make
       non-movem sizes suboptimal.

       This method is not foolproof; it assumes that the "asm reg"
       declarations at the beginning of the function really are used
       here (beware: they may be moved to temporary registers).
       This way, we do not have to save/move the registers around into
       temporaries; we can safely use them straight away.

      If you want to check that the allocation was right; then
      check the equalities in the first comment.  It should say
      something like "r13=r13, r11=r11, r12=r12". */
    __asm__ volatile ("\
	.ifnc %0%1%2,$r13$r12$r10					\n\
	.err								\n\
	.endif								\n\
									\n\
	;; Save the registers we'll clobber in the movem process	\n\
	;; on the stack.  Don't mention them to gcc, it will only be	\n\
	;; upset.							\n\
	subq	11*4,$sp						\n\
	movem	$r10,[$sp]						\n\
									\n\
	clear.d $r0							\n\
	clear.d $r1							\n\
	clear.d $r2							\n\
	clear.d $r3							\n\
	clear.d $r4							\n\
	clear.d $r5							\n\
	clear.d $r6							\n\
	clear.d $r7							\n\
	clear.d $r8							\n\
	clear.d $r9							\n\
	clear.d $r10							\n\
	clear.d $r11							\n\
									\n\
	;; Now we've got this:						\n\
	;; r13 - dst							\n\
	;; r12 - n							\n\
									\n\
	;; Update n for the first loop					\n\
	subq	12*4,$r12						\n\
0:									\n\
	subq   12*4,$r12						\n\
1:									\n\
	bge	0b							\n\
	movem	$r11,[$r13+]						\n\
									\n\
	addq   12*4,$r12 ;; compensate for last loop underflowing n	\n\
									\n\
	;; Restore registers from stack					\n\
	movem [$sp+],$r10						\n\
2:									\n\
	.section .fixup,\"ax\"						\n\
3:									\n\
	movem [$sp],$r10						\n\
	addq 12*4,$r10							\n\
	addq 12*4,$r13							\n\
	movem $r10,[$sp]						\n\
	jump 0b								\n\
	clear.d $r10							\n\
									\n\
	.previous							\n\
	.section __ex_table,\"a\"					\n\
	.dword 1b,3b							\n\
	.previous"

     /* Outputs */ : "=r" (dst), "=r" (n), "=r" (retn)
     /* Inputs */ : "0" (dst), "1" (n), "2" (retn)
     /* Clobber */ : "r11");
  }

  while (n >= 16)
  {
    __asm_clear_16 (dst, retn);
    n -= 16;
  }

  /* Having a separate by-four loops cuts down on cache footprint.
     FIXME:  Test with and without; increasing switch to be 0..15.  */
  while (n >= 4)
  {
    __asm_clear_4 (dst, retn);
    n -= 4;
  }

  switch (n)
  {
    case 0:
      break;
    case 1:
      __asm_clear_1 (dst, retn);
      break;
    case 2:
      __asm_clear_2 (dst, retn);
      break;
    case 3:
      __asm_clear_3 (dst, retn);
      break;
  }

  return retn;
}
EXPORT_SYMBOL(__do_clear_user);
