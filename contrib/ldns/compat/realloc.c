/* Just a replacement, if the original malloc is not
   GNU-compliant. Based on malloc.c */

#if HAVE_CONFIG_H
#include <ldns/config.h>
#endif
#undef realloc

#include <sys/types.h>

void *realloc (void*, size_t);
void *malloc (size_t);

/* Changes allocation to new sizes, copies over old data.
 * if oldptr is NULL, does a malloc.
 * if size is zero, allocate 1-byte block....
 *   (does not return NULL and free block)
 */

void *
rpl_realloc (void* ptr, size_t n)
{
  if (n == 0)
    n = 1;
  if(ptr == 0) {
    return malloc(n);
  }
  return realloc(ptr, n);
}

