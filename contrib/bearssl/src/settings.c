/*
 * Copyright (c) 2017 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "inner.h"

static const br_config_option config[] = {
	{ "BR_64",
#if BR_64
	 1
#else
	 0
#endif
	},
	{ "BR_AES_X86NI",
#if BR_AES_X86NI
	 1
#else
	 0
#endif
	},
	{ "BR_amd64",
#if BR_amd64
	 1
#else
	 0
#endif
	},
	{ "BR_ARMEL_CORTEXM_GCC",
#if BR_ARMEL_CORTEXM_GCC
	 1
#else
	 0
#endif
	},
	{ "BR_BE_UNALIGNED",
#if BR_BE_UNALIGNED
	 1
#else
	 0
#endif
	},
	{ "BR_CLANG",
#if BR_CLANG
	 1
#else
	 0
#endif
	},
	{ "BR_CLANG_3_7",
#if BR_CLANG_3_7
	 1
#else
	 0
#endif
	},
	{ "BR_CLANG_3_8",
#if BR_CLANG_3_8
	 1
#else
	 0
#endif
	},
	{ "BR_CT_MUL15",
#if BR_CT_MUL15
	 1
#else
	 0
#endif
	},
	{ "BR_CT_MUL31",
#if BR_CT_MUL31
	 1
#else
	 0
#endif
	},
	{ "BR_GCC",
#if BR_GCC
	 1
#else
	 0
#endif
	},
	{ "BR_GCC_4_4",
#if BR_GCC_4_4
	 1
#else
	 0
#endif
	},
	{ "BR_GCC_4_5",
#if BR_GCC_4_5
	 1
#else
	 0
#endif
	},
	{ "BR_GCC_4_6",
#if BR_GCC_4_6
	 1
#else
	 0
#endif
	},
	{ "BR_GCC_4_7",
#if BR_GCC_4_7
	 1
#else
	 0
#endif
	},
	{ "BR_GCC_4_8",
#if BR_GCC_4_8
	 1
#else
	 0
#endif
	},
	{ "BR_GCC_4_9",
#if BR_GCC_4_9
	 1
#else
	 0
#endif
	},
	{ "BR_GCC_5_0",
#if BR_GCC_5_0
	 1
#else
	 0
#endif
	},
	{ "BR_i386",
#if BR_i386
	 1
#else
	 0
#endif
	},
	{ "BR_INT128",
#if BR_INT128
	 1
#else
	 0
#endif
	},
	{ "BR_LE_UNALIGNED",
#if BR_LE_UNALIGNED
	 1
#else
	 0
#endif
	},
	{ "BR_LOMUL",
#if BR_LOMUL
	 1
#else
	 0
#endif
	},
	{ "BR_MAX_EC_SIZE", BR_MAX_EC_SIZE },
	{ "BR_MAX_RSA_SIZE", BR_MAX_RSA_SIZE },
	{ "BR_MAX_RSA_FACTOR", BR_MAX_RSA_FACTOR },
	{ "BR_MSC",
#if BR_MSC
	 1
#else
	 0
#endif
	},
	{ "BR_MSC_2005",
#if BR_MSC_2005
	 1
#else
	 0
#endif
	},
	{ "BR_MSC_2008",
#if BR_MSC_2008
	 1
#else
	 0
#endif
	},
	{ "BR_MSC_2010",
#if BR_MSC_2010
	 1
#else
	 0
#endif
	},
	{ "BR_MSC_2012",
#if BR_MSC_2012
	 1
#else
	 0
#endif
	},
	{ "BR_MSC_2013",
#if BR_MSC_2013
	 1
#else
	 0
#endif
	},
	{ "BR_MSC_2015",
#if BR_MSC_2015
	 1
#else
	 0
#endif
	},
	{ "BR_POWER8",
#if BR_POWER8
	 1
#else
	 0
#endif
	},
	{ "BR_RDRAND",
#if BR_RDRAND
	 1
#else
	 0
#endif
	},
	{ "BR_SLOW_MUL",
#if BR_SLOW_MUL
	 1
#else
	 0
#endif
	},
	{ "BR_SLOW_MUL15",
#if BR_SLOW_MUL15
	 1
#else
	 0
#endif
	},
	{ "BR_SSE2",
#if BR_SSE2
	 1
#else
	 0
#endif
	},
	{ "BR_UMUL128",
#if BR_UMUL128
	 1
#else
	 0
#endif
	},
	{ "BR_USE_UNIX_TIME",
#if BR_USE_UNIX_TIME
	 1
#else
	 0
#endif
	},
	{ "BR_USE_WIN32_RAND",
#if BR_USE_WIN32_RAND
	 1
#else
	 0
#endif
	},
	{ "BR_USE_WIN32_TIME",
#if BR_USE_WIN32_TIME
	 1
#else
	 0
#endif
	},

	{ NULL, 0 }
};

/* see bearssl.h */
const br_config_option *
br_get_config(void)
{
	return config;
}
