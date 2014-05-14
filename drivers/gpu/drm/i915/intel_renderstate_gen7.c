#include "intel_renderstate.h"

static const u32 gen7_null_state_relocs[] = {
};

static const u32 gen7_null_state_batch[] = {
	0x0a << 23, /* MI_BATCH_BUFFER_END */
};

RO_RENDERSTATE(7);
