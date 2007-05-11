#include <linux/types.h>

#define strcmp __inline_strcmp
#include <asm/string.h>
#undef strcmp

int strcmp(const char *dest, const char *src)
{
	        return __inline_strcmp(dest, src);
}

