/*
 * Wrapper script for the realmode binary as a transport object
 * before copying to low memory.
 */
#include <linux/linkage.h>
#include <asm/page_types.h>

	.section ".init.data","aw"

	.balign PAGE_SIZE

GLOBAL(real_mode_blob)
	.incbin	"arch/x86/realmode/rm/realmode.bin"
END(real_mode_blob)

GLOBAL(real_mode_relocs)
	.incbin	"arch/x86/realmode/rm/realmode.relocs"
END(real_mode_relocs)
