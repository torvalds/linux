#ifdef __ASSEMBLY__

/* EcoVec board specific boot code:
 * converts the "partner-jet-script.txt" script into assembly
 * the assembly code is the first code to be executed in the romImage
 */

#include <asm/romimage-macros.h>
#include "partner-jet-setup.txt"

	/* execute icbi after enabling cache */
	mov.l	1f, r0
	icbi	@r0

	/* jump to cached area */
	mova	2f, r0
	jmp	@r0
	nop

	.align 2
1 :	.long 0xa8000000
2 :

#else /* __ASSEMBLY__ */

/* Ecovec board specific information:
 *
 * Set the following to enable MMCIF boot from the MMC card in CN12:
 *
 * DS1.5 = OFF (SH BOOT pin set to L)
 * DS2.6 = OFF (Select MMCIF on CN12 instead of SDHI1)
 * DS2.7 = ON  (Select MMCIF on CN12 instead of SDHI1)
 *
 */
#define HIZCRA		0xa4050158
#define PGDR		0xa405012c

static inline void mmcif_update_progress(int nr)
{
	/* disable Hi-Z for LED pins */
	__raw_writew(__raw_readw(HIZCRA) & ~(1 << 1), HIZCRA);

	/* update progress on LED4, LED5, LED6 and LED7 */
	__raw_writeb(1 << (nr - 1), PGDR);
}

#endif /* __ASSEMBLY__ */
