#ifndef __NVIF_PUSH906F_H__
#define __NVIF_PUSH906F_H__
#include <nvif/push.h>

#include <nvhw/class/cl906f.h>

#ifndef PUSH906F_SUBC
// Host methods
#define PUSH906F_SUBC_NV906F	0
#define PUSH906F_SUBC_NVC36F	0

// Twod
#define PUSH906F_SUBC_NV902D	3

// MemoryToMemoryFormat
#define PUSH906F_SUBC_NV9039	4

// DmaCopy
#define PUSH906F_SUBC_NV90B5	4
#define PUSH906F_SUBC_NVA0B5	4
#endif

#define PUSH_HDR(p,o,n,f,s,m,c) do {                                                \
        PUSH_ASSERT(!((s) & ~DRF_MASK(NV906F_DMA_METHOD_SUBCHANNEL)), "subc");      \
        PUSH_ASSERT(!((m) & ~(DRF_MASK(NV906F_DMA_METHOD_ADDRESS) << 2)), "mthd");   \
        PUSH_ASSERT(!((c) & ~DRF_MASK(NV906F_DMA_METHOD_COUNT)), "count/immd");     \
        PUSH_DATA__((p), NVVAL(NV906F, DMA, METHOD_ADDRESS, (m) >> 2) |             \
			 NVVAL(NV906F, DMA, METHOD_SUBCHANNEL, (s)) |               \
			 NVVAL(NV906F, DMA, METHOD_COUNT, (c)) |                    \
			 NVDEF(NV906F, DMA, SEC_OP, o),                             \
		    " "n" subc %d mthd 0x%04x "f" - %s",                            \
		    (u32)(s), (u32)(m), (u32)(c), __func__);                        \
} while(0)

#define PUSH_MTHD_INC 4:4
#define PUSH_MTHD_HDR(p,c,m,n) \
	PUSH_HDR(p, INC_METHOD, "incr", "size %d", PUSH906F_SUBC_##c, m, n)

#define PUSH_NINC_INC 0:0
#define PUSH_NINC_HDR(p,c,m,n) \
	PUSH_HDR(p, NON_INC_METHOD, "ninc", "size %d", PUSH906F_SUBC_##c, m, n)

#define PUSH_IMMD_HDR(p,c,m,n) \
	PUSH_HDR(p, IMMD_DATA_METHOD, "immd", "data 0x%04x", PUSH906F_SUBC_##c, m, n)

#define PUSH_1INC_INC 4:0
#define PUSH_1INC_HDR(p,c,m,n) \
	PUSH_HDR(p, ONE_INC, "oinc", "size %d", PUSH906F_SUBC_##c, m, n)
#endif
