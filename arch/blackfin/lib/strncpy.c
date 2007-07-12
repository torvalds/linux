#include <linux/types.h>

#define strncpy __inline_strncpy
#include <asm/string.h>
#undef strncpy

char *strncpy(char *dest, const char *src, size_t n)
{
	return __inline_strncpy(dest, src, n);
}
