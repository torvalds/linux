/*#************************************************************************#*/
/*#-------------------------------------------------------------------------*/
/*#                                                                         */
/*# FUNCTION NAME: memcpy()                                                 */
/*#                                                                         */
/*# PARAMETERS:  void* dst;   Destination address.                          */
/*#              void* src;   Source address.                               */
/*#              int   len;   Number of bytes to copy.                      */
/*#                                                                         */
/*# RETURNS:     dst.                                                       */
/*#                                                                         */
/*# DESCRIPTION: Copies len bytes of memory from src to dst.  No guarantees */
/*#              about copying of overlapping memory areas. This routine is */
/*#              very sensitive to compiler changes in register allocation. */
/*#              Should really be rewritten to avoid this problem.          */
/*#                                                                         */
/*#-------------------------------------------------------------------------*/
/*#                                                                         */
/*# HISTORY                                                                 */
/*#                                                                         */
/*# DATE      NAME            CHANGES                                       */
/*# ----      ----            -------                                       */
/*# 941007    Kenny R         Creation                                      */
/*# 941011    Kenny R         Lots of optimizations and inlining.           */
/*# 941129    Ulf A           Adapted for use in libc.                      */
/*# 950216    HP              N==0 forgotten if non-aligned src/dst.        */
/*#                           Added some optimizations.                     */
/*# 001025    HP              Make src and dst char *.  Align dst to	    */
/*#			      dword, not just word-if-both-src-and-dst-	    */
/*#			      are-misaligned.				    */
/*#                                                                         */
/*#-------------------------------------------------------------------------*/

#include <linux/types.h>

void *memcpy(void *pdst,
             const void *psrc,
             size_t pn)
{
  /* Ok.  Now we want the parameters put in special registers.
     Make sure the compiler is able to make something useful of this.
      As it is now: r10 -> r13; r11 -> r11 (nop); r12 -> r12 (nop).

     If gcc was allright, it really would need no temporaries, and no
     stack space to save stuff on. */

  register void *return_dst __asm__ ("r10") = pdst;
  register char *dst __asm__ ("r13") = pdst;
  register const char *src __asm__ ("r11") = psrc;
  register int n __asm__ ("r12") = pn;
  
 
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
      n--;
      *(char*)dst = *(char*)src;
      src++;
      dst++;
    }

    if ((unsigned long) dst & 2)
    {
      n -= 2;
      *(short*)dst = *(short*)src;
      src += 2;
      dst += 2;
    }
  }

  /* Decide which copying method to use. */
  if (n >= 44*2)                /* Break even between movem and
                                   move16 is at 38.7*2, but modulo 44. */
  {
    /* For large copies we use 'movem' */

  /* It is not optimal to tell the compiler about clobbering any
     registers; that will move the saving/restoring of those registers
     to the function prologue/epilogue, and make non-movem sizes
     suboptimal.

      This method is not foolproof; it assumes that the "asm reg"
     declarations at the beginning of the function really are used
     here (beware: they may be moved to temporary registers).
      This way, we do not have to save/move the registers around into
     temporaries; we can safely use them straight away.

      If you want to check that the allocation was right; then
      check the equalities in the first comment.  It should say
      "r13=r13, r11=r11, r12=r12" */
    __asm__ volatile ("
        ;; Check that the following is true (same register names on
        ;; both sides of equal sign, as in r8=r8):
        ;; %0=r13, %1=r11, %2=r12
        ;;
	;; Save the registers we'll use in the movem process
	;; on the stack.
	subq 	11*4,$sp
	movem	$r10,[$sp]

        ;; Now we've got this:
	;; r11 - src
	;; r13 - dst
	;; r12 - n
	
        ;; Update n for the first loop
        subq    44,$r12
0:
	movem	[$r11+],$r10
        subq   44,$r12
        bge     0b
	movem	$r10,[$r13+]

        addq   44,$r12  ;; compensate for last loop underflowing n

	;; Restore registers from stack
        movem [$sp+],$r10" 

     /* Outputs */ : "=r" (dst), "=r" (src), "=r" (n) 
     /* Inputs */ : "0" (dst), "1" (src), "2" (n));
    
  }

  /* Either we directly starts copying, using dword copying
     in a loop, or we copy as much as possible with 'movem' 
     and then the last block (<44 bytes) is copied here.
     This will work since 'movem' will have updated src,dst,n. */

  while ( n >= 16 )
  {
    *((long*)dst)++ = *((long*)src)++;
    *((long*)dst)++ = *((long*)src)++;
    *((long*)dst)++ = *((long*)src)++;
    *((long*)dst)++ = *((long*)src)++;
    n -= 16;
  }

  /* A switch() is definitely the fastest although it takes a LOT of code.
   * Particularly if you inline code this.
   */
  switch (n)
  {
    case 0:
      break;
    case 1:
      *(char*)dst = *(char*)src;
      break;
    case 2:
      *(short*)dst = *(short*)src;
      break;
    case 3:
      *((short*)dst)++ = *((short*)src)++;
      *(char*)dst = *(char*)src;
      break;
    case 4:
      *((long*)dst)++ = *((long*)src)++;
      break;
    case 5:
      *((long*)dst)++ = *((long*)src)++;
      *(char*)dst = *(char*)src;
      break;
    case 6:
      *((long*)dst)++ = *((long*)src)++;
      *(short*)dst = *(short*)src;
      break;
    case 7:
      *((long*)dst)++ = *((long*)src)++;
      *((short*)dst)++ = *((short*)src)++;
      *(char*)dst = *(char*)src;
      break;
    case 8:
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      break;
    case 9:
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      *(char*)dst = *(char*)src;
      break;
    case 10:
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      *(short*)dst = *(short*)src;
      break;
    case 11:
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      *((short*)dst)++ = *((short*)src)++;
      *(char*)dst = *(char*)src;
      break;
    case 12:
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      break;
    case 13:
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      *(char*)dst = *(char*)src;
      break;
    case 14:
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      *(short*)dst = *(short*)src;
      break;
    case 15:
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      *((long*)dst)++ = *((long*)src)++;
      *((short*)dst)++ = *((short*)src)++;
      *(char*)dst = *(char*)src;
      break;
  }

  return return_dst; /* destination pointer. */
} /* memcpy() */
