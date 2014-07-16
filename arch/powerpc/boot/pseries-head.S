#include "ppc_asm.h"

	.text

	.globl _zimage_start
_zimage_start:
	FIXUP_ENDIAN
	b _zimage_start_lib
