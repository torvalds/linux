#include <linux/types.h>

void * memset(void * s, int c, size_t count)
{
  void *xs = s;
  size_t temp;

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
      for (; temp; temp--)
	*ls++ = c;
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
