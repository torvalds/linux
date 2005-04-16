#include <linux/types.h>

void * memset(void * s, int c, size_t count)
{
  void *xs = s;
  size_t temp, temp1;

  if (!count)
    return xs;
  c &= 0xff;
  c |= c << 8;
  c |= c << 16;
  if ((long) s & 1)
    {
      char *cs = s;
      *cs++ = c;
      s = cs;
      count--;
    }
  if (count > 2 && (long) s & 2)
    {
      short *ss = s;
      *ss++ = c;
      s = ss;
      count -= 2;
    }
  temp = count >> 2;
  if (temp)
    {
      long *ls = s;

      __asm__ __volatile__("movel %1,%2\n\t"
			   "andw  #7,%2\n\t"
			   "lsrl  #3,%1\n\t"
			   "negw  %2\n\t"
			   "jmp   %%pc@(2f,%2:w:2)\n\t"
			   "1:\t"
			   "movel %3,%0@+\n\t"
			   "movel %3,%0@+\n\t"
			   "movel %3,%0@+\n\t"
			   "movel %3,%0@+\n\t"
			   "movel %3,%0@+\n\t"
			   "movel %3,%0@+\n\t"
			   "movel %3,%0@+\n\t"
			   "movel %3,%0@+\n\t"
			   "2:\t"
			   "dbra  %1,1b\n\t"
			   "clrw  %1\n\t"
			   "subql #1,%1\n\t"
			   "jpl   1b\n\t"
			   : "=a" (ls), "=d" (temp), "=&d" (temp1)
			   : "d" (c), "0" (ls), "1" (temp)
			   );
      s = ls;
    }
  if (count & 2)
    {
      short *ss = s;
      *ss++ = c;
      s = ss;
    }
  if (count & 1)
    {
      char *cs = s;
      *cs = c;
    }
  return xs;
}
