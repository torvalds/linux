/* $FreeBSD$ */

#include "alloca.h"

#define	memalign(align, size) ({			\
	void *__ptr;					\
	if (posix_memalign(&__ptr, (align), (size)))	\
		__ptr = NULL;				\
	__ptr;						\
})

/*
 * Return true if the snprintf succeeded, false if there was
 * truncation or error:
 */
#define	check_snprintf(buf, len, fmt, ...) ({			\
	int rc = snprintf(buf, len, fmt, ##__VA_ARGS__);	\
	(rc < len && rc >= 0);					\
})

#define	min_t(type, x, y) ({			\
	type __min1 = (x);			\
	type __min2 = (y);			\
	__min1 < __min2 ? __min1 : __min2; })

#define	freeaddrinfo_null(x) do {               \
        if ((x) != NULL)			\
                freeaddrinfo(x);		\
} while (0)

#define	VALGRIND_MAKE_MEM_DEFINED(...)	0
#define	s6_addr32 __u6_addr.__u6_addr32
#define	__sum16 uint16_t
#define NRESOLVE_NEIGH 1
#define	STREAM_CLOEXEC "e"
#define	VERBS_PROVIDER_DIR "/usr/lib/"
#define	IBV_CONFIG_DIR "/etc/ibverbs/"
#define	MADV_DONTFORK MADV_NORMAL
#define	MADV_DOFORK MADV_NORMAL
#define	SWITCH_FALLTHROUGH (void)0

