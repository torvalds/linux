#ifndef __NVIF_PUSHC37B_H__
#define __NVIF_PUSHC37B_H__
#include <nvif/push.h>

#include <nvhw/class/clc37b.h>

#define PUSH_HDR(p,m,c) do {                                                    \
        PUSH_ASSERT(!((m) & ~DRF_SMASK(NVC37B_DMA_METHOD_OFFSET)), "mthd");     \
        PUSH_ASSERT(!((c) & ~DRF_MASK(NVC37B_DMA_METHOD_COUNT)), "size");       \
        PUSH_DATA__((p), NVDEF(NVC37B, DMA, OPCODE, METHOD) |                   \
			 NVVAL(NVC37B, DMA, METHOD_COUNT, (c)) |                \
			 NVVAL(NVC37B, DMA, METHOD_OFFSET, (m) >> 2),           \
		    " mthd 0x%04x size %d - %s", (u32)(m), (u32)(c), __func__); \
} while(0)

#define PUSH_MTHD_HDR(p,s,m,c) PUSH_HDR(p,m,c)
#define PUSH_MTHD_INC 4:4
#endif
