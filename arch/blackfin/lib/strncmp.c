#include <linux/types.h>

#define strncmp __inline_strncmp
#include <asm/string.h>
#undef strncmp

int strncmp(const char *cs, const char *ct, size_t count)
{
	        return __inline_strncmp(cs, ct, count);
}

