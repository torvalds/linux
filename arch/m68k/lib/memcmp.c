#include <linux/types.h>

int memcmp(const void * cs,const void * ct,size_t count)
{
  const unsigned char *su1, *su2;

  for( su1 = cs, su2 = ct; 0 < count; ++su1, ++su2, count--)
    if (*su1 != *su2)
      return((*su1 < *su2) ? -1 : +1);
  return(0);
}
