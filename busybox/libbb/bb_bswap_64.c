/*
 * Utility routines.
 *
 * Copyright (C) 2010 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

#include "libbb.h"

#if !(ULONG_MAX > 0xffffffff)
uint64_t FAST_FUNC bb_bswap_64(uint64_t x)
{
	return bswap_64(x);
}
#endif
