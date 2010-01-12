/*
 * <mach/asp.h> - DaVinci Audio Serial Port support
 */
#ifndef __ASM_ARCH_DAVINCI_ASP_H
#define __ASM_ARCH_DAVINCI_ASP_H

#include <mach/irqs.h>
#include <mach/edma.h>

/* Bases of dm644x and dm355 register banks */
#define DAVINCI_ASP0_BASE	0x01E02000
#define DAVINCI_ASP1_BASE	0x01E04000

/* Bases of dm365 register banks */
#define DAVINCI_DM365_ASP0_BASE	0x01D02000

/* Bases of dm646x register banks */
#define	DAVINCI_DM646X_MCASP0_REG_BASE		0x01D01000
#define DAVINCI_DM646X_MCASP1_REG_BASE		0x01D01800

/* Bases of da850/da830 McASP0  register banks */
#define DAVINCI_DA8XX_MCASP0_REG_BASE	0x01D00000

/* Bases of da830 McASP1 register banks */
#define DAVINCI_DA830_MCASP1_REG_BASE	0x01D04000

/* EDMA channels of dm644x and dm355 */
#define DAVINCI_DMA_ASP0_TX	2
#define DAVINCI_DMA_ASP0_RX	3
#define DAVINCI_DMA_ASP1_TX	8
#define DAVINCI_DMA_ASP1_RX	9

/* EDMA channels of dm646x */
#define	DAVINCI_DM646X_DMA_MCASP0_AXEVT0	6
#define	DAVINCI_DM646X_DMA_MCASP0_AREVT0	9
#define	DAVINCI_DM646X_DMA_MCASP1_AXEVT1	12

/* EDMA channels of da850/da830 McASP0 */
#define	DAVINCI_DA8XX_DMA_MCASP0_AREVT	0
#define	DAVINCI_DA8XX_DMA_MCASP0_AXEVT	1

/* EDMA channels of da830 McASP1 */
#define	DAVINCI_DA830_DMA_MCASP1_AREVT	2
#define	DAVINCI_DA830_DMA_MCASP1_AXEVT	3

/* Interrupts */
#define DAVINCI_ASP0_RX_INT	IRQ_MBRINT
#define DAVINCI_ASP0_TX_INT	IRQ_MBXINT
#define DAVINCI_ASP1_RX_INT	IRQ_MBRINT
#define DAVINCI_ASP1_TX_INT	IRQ_MBXINT

struct snd_platform_data {
	u32 tx_dma_offset;
	u32 rx_dma_offset;
	enum dma_event_q eventq_no;	/* event queue number */
	unsigned int codec_fmt;
	/*
	 * Allowing this is more efficient and eliminates left and right swaps
	 * caused by underruns, but will swap the left and right channels
	 * when compared to previous behavior.
	 */
	unsigned enable_channel_combine:1;
	unsigned sram_size_playback;
	unsigned sram_size_capture;

	/* McASP specific fields */
	int tdm_slots;
	u8 op_mode;
	u8 num_serializer;
	u8 *serial_dir;
	u8 version;
	u8 txnumevt;
	u8 rxnumevt;
};

enum {
	MCASP_VERSION_1 = 0,	/* DM646x */
	MCASP_VERSION_2,	/* DA8xx/OMAPL1x */
};

#define INACTIVE_MODE	0
#define TX_MODE		1
#define RX_MODE		2

#define DAVINCI_MCASP_IIS_MODE	0
#define DAVINCI_MCASP_DIT_MODE	1

#endif /* __ASM_ARCH_DAVINCI_ASP_H */
