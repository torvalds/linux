#include "misc.h"

/* Avoid intereference from any defines in string_32.h */
#undef memcmp
int memcmp(const void *s1, const void *s2, size_t len)
{
	u8 diff;
	asm("repe; cmpsb; setnz %0"
	    : "=qm" (diff), "+D" (s1), "+S" (s2), "+c" (len));
	return diff;
}

#include "../string.c"
