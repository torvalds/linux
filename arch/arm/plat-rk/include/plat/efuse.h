#ifndef __PLAT_EFUSE_H
#define __PLAT_EFUSE_H

#include <asm/types.h>

/* On success, the number of bytes read is returned */
int efuse_readregs(u32 addr, u32 length, u8 *buf);

#endif
