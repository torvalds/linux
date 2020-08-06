#ifndef __NVIF_PUSH206E_H__
#define __NVIF_PUSH206E_H__
#include <nvif/push006c.h>

#include <nvhw/class/cl206e.h>

#define PUSH_CALL(p,o) do {                                         \
        PUSH_ASSERT(!((o) & ~0xffffffffcULL), "offset");            \
	PUSH_DATA__((p), NVDEF(NV206E, DMA, OPCODE2, CALL) |        \
			 NVVAL(NV206E, DMA, CALL_OFFSET, (o) >> 2), \
		    " call 0x%08x - %s", (u32)(o), __func__);       \
} while(0)
#endif
