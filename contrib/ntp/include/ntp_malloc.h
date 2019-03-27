/*
 * Define malloc and friends.
 */
#ifndef NTP_MALLOC_H
#define NTP_MALLOC_H

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#else
# ifdef HAVE_MALLOC_H
#  include <malloc.h>
# endif
#endif

/*
 * Deal with platform differences declaring alloca()
 * This comes nearly verbatim from:
 *
 * http://www.gnu.org/software/autoconf/manual/autoconf.html#Particular-Functions
 *
 * The only modifications were to remove C++ support and guard against
 * redefining alloca.
 */
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#elif defined __GNUC__
# ifndef alloca
#  define alloca __builtin_alloca
# endif
#elif defined _AIX
# ifndef alloca
#  define alloca __alloca
# endif
#elif defined _MSC_VER
# include <malloc.h>
# ifndef alloca
#  define alloca _alloca
# endif
#else
# include <stddef.h>
void * alloca(size_t);
#endif

#ifdef EREALLOC_IMPL
# define EREALLOC_CALLSITE	/* preserve __FILE__ and __LINE__ */
#else
# define EREALLOC_IMPL(ptr, newsz, filenm, loc) \
	 realloc(ptr, (newsz))
#endif

#ifdef HAVE_STRINGS_H
# include <strings.h>
# define zero_mem(p, s)		bzero(p, s)
#endif

#ifndef zero_mem
# define zero_mem(p, s)		memset(p, 0, s)
#endif
#define ZERO(var)		zero_mem(&(var), sizeof(var))

#endif	/* NTP_MALLOC_H */
