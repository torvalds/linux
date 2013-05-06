#ifndef __PLAT_EFUSE_H
#define __PLAT_EFUSE_H

#include <asm/types.h>

int efuse_readregs(u32 addr, u32 length, u8 *pData);

#endif
