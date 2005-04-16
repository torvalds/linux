/* Normally compiler builtins are used, but sometimes the compiler calls out
   of line code. Based on asm-i386/string.h.
 */
#define _STRING_C
#include <linux/string.h>

#undef memmove
void *memmove(void * dest,const void *src,size_t count)
{
	if (dest < src) { 
		__inline_memcpy(dest,src,count);
	} else {
		char *p = (char *) dest + count;
		char *s = (char *) src + count;
		while (count--)
			*--p = *--s;
	}
	return dest;
} 
