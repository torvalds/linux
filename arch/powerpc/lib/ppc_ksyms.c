#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <net/checksum.h>

EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memchr);

EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strncmp);

#ifndef CONFIG_GENERIC_CSUM
EXPORT_SYMBOL(__csum_partial);
EXPORT_SYMBOL(csum_partial_copy_generic);
#endif

EXPORT_SYMBOL(__copy_tofrom_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(copy_page);

#ifdef CONFIG_PPC64
EXPORT_SYMBOL(__arch_hweight8);
EXPORT_SYMBOL(__arch_hweight16);
EXPORT_SYMBOL(__arch_hweight32);
EXPORT_SYMBOL(__arch_hweight64);
#endif
