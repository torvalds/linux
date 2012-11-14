#include <linux/module.h>
#include <asm/string.h>
#include <asm/checksum.h>

#ifndef CONFIG_X86_32
/*XXX: we need them because they would be exported by x86_64 */
#if (__GNUC__ == 4 && __GNUC_MINOR__ >= 3) || __GNUC__ > 4 || defined(__clang__)
EXPORT_SYMBOL(memcpy);
#else
EXPORT_SYMBOL(__memcpy);
#endif
#endif
EXPORT_SYMBOL(csum_partial);
