#ifndef __ASM_CPU_SH3_DMA_H
#define __ASM_CPU_SH3_DMA_H

#if defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721) || \
    defined(CONFIG_CPU_SUBTYPE_SH7710) || \
    defined(CONFIG_CPU_SUBTYPE_SH7712)
#define SH_DMAC_BASE0	0xa4010020
#else /* SH7705/06/07/09 */
#define SH_DMAC_BASE0	0xa4000020
#endif

#define DMTE0_IRQ	48
#define DMTE4_IRQ	76

/* Definitions for the SuperH DMAC */
#define TM_BURST	0x00000020
#define TS_8		0x00000000
#define TS_16		0x00000008
#define TS_32		0x00000010
#define TS_128		0x00000018

#endif /* __ASM_CPU_SH3_DMA_H */
