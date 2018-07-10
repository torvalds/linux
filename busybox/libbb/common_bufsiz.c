/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2016 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FEATURE_USE_BSS_TAIL
//config:	bool "Use the end of BSS page"
//config:	default n
//config:	help
//config:	Attempt to reclaim a small unused part of BSS.
//config:
//config:	Executables have the following parts:
//config:	= read-only executable code and constants, also known as "text"
//config:	= read-write data
//config:	= non-initialized (zeroed on demand) data, also known as "bss"
//config:
//config:	At link time, "text" is padded to a full page. At runtime, all "text"
//config:	pages are mapped RO and executable.
//config:
//config:	"Data" starts on the next page boundary, but is not padded
//config:	to a full page at the end. "Bss" starts wherever "data" ends.
//config:	At runtime, "data" pages are mapped RW and they are file-backed
//config:	(this includes a small portion of "bss" which may live in the last
//config:	partial page of "data").
//config:	Pages which are fully in "bss" are mapped to anonymous memory.
//config:
//config:	"Bss" end is usually not page-aligned. There is an unused space
//config:	in the last page. Linker marks its start with the "_end" symbol.
//config:
//config:	This option will attempt to use that space for bb_common_bufsiz1[]
//config:	array. If it fits after _end, it will be used, and COMMON_BUFSIZE
//config:	will be enlarged from its guaranteed minimum size of 1 kbyte.
//config:	This may require recompilation a second time, since value of _end
//config:	is known only after final link.
//config:
//config:	If you are getting a build error like this:
//config:		appletlib.c:(.text.main+0xd): undefined reference to '_end'
//config:	disable this option.

//kbuild:lib-y += common_bufsiz.o

#include "libbb.h"
#include "common_bufsiz.h"

#if !ENABLE_FEATURE_USE_BSS_TAIL

/* We use it for "global" data via *(struct global*)bb_common_bufsiz1.
 * Since gcc insists on aligning struct global's members, it would be a pity
 * (and an alignment fault on some CPUs) to mess it up. */
char bb_common_bufsiz1[COMMON_BUFSIZE] ALIGNED(sizeof(long long));

#else

# ifndef setup_common_bufsiz
/* For now, this is never used:
 * scripts/generate_BUFSIZ.sh never generates "malloced" bufsiz1:
 *	enum { COMMON_BUFSIZE = 1024 };
 *	extern char *const bb_common_bufsiz1;
 *	void setup_common_bufsiz(void);
 * This has proved to be worse than the approach of defining
 * larger bb_common_bufsiz1[] array.
 */

/*
 * It is not defined as a dummy macro.
 * It means we have to provide this function.
 */
char *const bb_common_bufsiz1 __attribute__ ((section (".data")));
void setup_common_bufsiz(void)
{
	if (!bb_common_bufsiz1)
		*(char**)&bb_common_bufsiz1 = xzalloc(COMMON_BUFSIZE);
}
# else
#  ifndef bb_common_bufsiz1
   /* bb_common_bufsiz1[] is not aliased to _end[] */
char bb_common_bufsiz1[COMMON_BUFSIZE] ALIGNED(sizeof(long long));
#  endif
# endif

#endif
