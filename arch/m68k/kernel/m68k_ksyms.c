#include <linux/module.h>

asmlinkage long long __ashldi3 (long long, int);
asmlinkage long long __ashrdi3 (long long, int);
asmlinkage long long __lshrdi3 (long long, int);
asmlinkage long long __muldi3 (long long, long long);

/* The following are special because they're not called
   explicitly (the C compiler generates them).  Fortunately,
   their interface isn't gonna change any time soon now, so
   it's OK to leave it out of version control.  */
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__muldi3);

#if defined(CONFIG_M68000) || defined(CONFIG_COLDFIRE)
/*
 * Simpler 68k and ColdFire parts also need a few other gcc functions.
 */
extern long long __divsi3(long long, long long);
extern long long __modsi3(long long, long long);
extern long long __mulsi3(long long, long long);
extern long long __udivsi3(long long, long long);
extern long long __umodsi3(long long, long long);

EXPORT_SYMBOL(__divsi3);
EXPORT_SYMBOL(__modsi3);
EXPORT_SYMBOL(__mulsi3);
EXPORT_SYMBOL(__udivsi3);
EXPORT_SYMBOL(__umodsi3);
#endif
