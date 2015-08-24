#include <linux/module.h>
#include <linux/linkage.h>

/*
 * libgcc functions - functions that are used internally by the
 * compiler...  (prototypes are not correct though, but that
 * doesn't really matter since they're not versioned).
 */
asmlinkage long __ucmpdi2(long long, long long);
asmlinkage long long __ashldi3(long long, int);
asmlinkage long long __ashrdi3(long long, int);
asmlinkage long long __lshrdi3(long long, int);
asmlinkage long __divsi3(long, long);
asmlinkage long __modsi3(long, long);
asmlinkage unsigned long __umodsi3(unsigned long, unsigned long);
asmlinkage long long __muldi3(long long, long long);
asmlinkage long __mulsi3(long, long);
asmlinkage long __udivsi3(long, long);
asmlinkage void *memcpy(void *, const void *, size_t);
asmlinkage void *memset(void *, int, size_t);
asmlinkage long strncpy_from_user(void *to, void *from, size_t n);

	/* gcc lib functions */
EXPORT_SYMBOL(__ucmpdi2);
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__divsi3);
EXPORT_SYMBOL(__modsi3);
EXPORT_SYMBOL(__umodsi3);
EXPORT_SYMBOL(__muldi3);
EXPORT_SYMBOL(__mulsi3);
EXPORT_SYMBOL(__udivsi3);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(strncpy_from_user);
