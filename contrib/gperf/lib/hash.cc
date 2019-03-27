/* 
Copyright (C) 1990, 2000, 2002 Free Software Foundation
    written by Doug Lea <dl@rocky.oswego.edu>
*/

#include <hash.h>

/*
 Some useful hash function.
 It's not a particularly good hash function (<< 5 would be better than << 4),
 but people believe in it because it comes from Dragon book.
*/

unsigned int
hashpjw (const unsigned char *x, unsigned int len) // From Dragon book, p436
{
  unsigned int h = 0;
  unsigned int g;

  for (; len > 0; len--)
    {
      h = (h << 4) + *x++;
      if ((g = h & 0xf0000000) != 0)
        h = (h ^ (g >> 24)) ^ g;
    }
  return h;
}
