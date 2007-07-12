#include <linux/types.h>

#define strcpy __inline_strcpy
#include <asm/string.h>
#undef strcpy

char *strcpy(char *dest, const char *src)
{
	return __inline_strcpy(dest, src);
}
