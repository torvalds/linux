#include "linux/types.h"
#include "linux/module.h"

/* Some of this are builtin function (some are not but could in the future),
 * so I *must* declare good prototypes for them and then EXPORT them.
 * The kernel code uses the macro defined by include/linux/string.h,
 * so I undef macros; the userspace code does not include that and I
 * add an EXPORT for the glibc one.
 */

#undef strlen
#undef strstr
#undef memcpy
#undef memset

extern size_t strlen(const char *);
extern void *memmove(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int printf(const char *, ...);

/* If it's not defined, the export is included in lib/string.c.*/
#ifdef __HAVE_ARCH_STRSTR
EXPORT_SYMBOL(strstr);
#endif

#ifndef __x86_64__
extern void *memcpy(void *, const void *, size_t);
EXPORT_SYMBOL(memcpy);
#endif

EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(printf);

/* Here, instead, I can provide a fake prototype. Yes, someone cares: genksyms.
 * However, the modules will use the CRC defined *here*, no matter if it is
 * good; so the versions of these symbols will always match
 */
#define EXPORT_SYMBOL_PROTO(sym)       \
	int sym(void);                  \
	EXPORT_SYMBOL(sym);

extern void readdir64(void) __attribute__((weak));
EXPORT_SYMBOL(readdir64);
extern void truncate64(void) __attribute__((weak));
EXPORT_SYMBOL(truncate64);

#ifdef SUBARCH_i386
EXPORT_SYMBOL(vsyscall_ehdr);
EXPORT_SYMBOL(vsyscall_end);
#endif

EXPORT_SYMBOL_PROTO(__errno_location);

EXPORT_SYMBOL_PROTO(access);
EXPORT_SYMBOL_PROTO(open);
EXPORT_SYMBOL_PROTO(open64);
EXPORT_SYMBOL_PROTO(close);
EXPORT_SYMBOL_PROTO(read);
EXPORT_SYMBOL_PROTO(write);
EXPORT_SYMBOL_PROTO(dup2);
EXPORT_SYMBOL_PROTO(__xstat);
EXPORT_SYMBOL_PROTO(__lxstat);
EXPORT_SYMBOL_PROTO(__lxstat64);
EXPORT_SYMBOL_PROTO(__fxstat64);
EXPORT_SYMBOL_PROTO(lseek);
EXPORT_SYMBOL_PROTO(lseek64);
EXPORT_SYMBOL_PROTO(chown);
EXPORT_SYMBOL_PROTO(fchown);
EXPORT_SYMBOL_PROTO(truncate);
EXPORT_SYMBOL_PROTO(ftruncate64);
EXPORT_SYMBOL_PROTO(utime);
EXPORT_SYMBOL_PROTO(utimes);
EXPORT_SYMBOL_PROTO(futimes);
EXPORT_SYMBOL_PROTO(chmod);
EXPORT_SYMBOL_PROTO(fchmod);
EXPORT_SYMBOL_PROTO(rename);
EXPORT_SYMBOL_PROTO(__xmknod);

EXPORT_SYMBOL_PROTO(symlink);
EXPORT_SYMBOL_PROTO(link);
EXPORT_SYMBOL_PROTO(unlink);
EXPORT_SYMBOL_PROTO(readlink);

EXPORT_SYMBOL_PROTO(mkdir);
EXPORT_SYMBOL_PROTO(rmdir);
EXPORT_SYMBOL_PROTO(opendir);
EXPORT_SYMBOL_PROTO(readdir);
EXPORT_SYMBOL_PROTO(closedir);
EXPORT_SYMBOL_PROTO(seekdir);
EXPORT_SYMBOL_PROTO(telldir);

EXPORT_SYMBOL_PROTO(ioctl);

EXPORT_SYMBOL_PROTO(pread64);
EXPORT_SYMBOL_PROTO(pwrite64);

EXPORT_SYMBOL_PROTO(statfs);
EXPORT_SYMBOL_PROTO(statfs64);

EXPORT_SYMBOL_PROTO(getuid);

EXPORT_SYMBOL_PROTO(fsync);
EXPORT_SYMBOL_PROTO(fdatasync);

/* Export symbols used by GCC for the stack protector. */
extern void __stack_smash_handler(void *) __attribute__((weak));
EXPORT_SYMBOL(__stack_smash_handler);

extern long __guard __attribute__((weak));
EXPORT_SYMBOL(__guard);
