#ifndef BOOT_COMPRESSED_ERROR_H
#define BOOT_COMPRESSED_ERROR_H

#include <linux/compiler.h>

void warn(char *m);
void error(char *m) __noreturn;

#endif /* BOOT_COMPRESSED_ERROR_H */
