/* Just a replacement, if the original malloc is not
   GNU-compliant. See autoconf documentation. */

#if HAVE_CONFIG_H
#include <ldns/config.h>
#endif

void *calloc();

#if !HAVE_BZERO && HAVE_MEMSET
# define bzero(buf, bytes)	((void) memset (buf, 0, bytes))
#endif

void *
calloc(size_t num, size_t size)
{
	void *new = malloc(num * size);
	if (!new) {
		return NULL;
	}
	bzero(new, num * size);
	return new;
}

