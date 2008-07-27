/*
 * sun4paddr.h:  Various physical addresses on sun4 machines
 *
 * Copyright (C) 1997 Anton Blanchard (anton@progsoc.uts.edu.au)
 * Copyright (C) 1998 Chris Davis (cdavis@cois.on.ca)
 * 
 * Now supports more sun4's
 */

#ifndef _SPARC_SUN4PADDR_H
#define _SPARC_SUN4PADDR_H

#define SUN4_IE_PHYSADDR		0xf5000000
#define SUN4_UNUSED_PHYSADDR		0

/* these work for me */
#define SUN4_200_MEMREG_PHYSADDR	0xf4000000
#define SUN4_200_CLOCK_PHYSADDR		0xf3000000
#define SUN4_200_BWTWO_PHYSADDR		0xfd000000
#define SUN4_200_ETH_PHYSADDR		0xf6000000
#define SUN4_200_SI_PHYSADDR		0xff200000

/* these were here before */
#define SUN4_300_MEMREG_PHYSADDR	0xf4000000
#define SUN4_300_CLOCK_PHYSADDR		0xf2000000
#define SUN4_300_TIMER_PHYSADDR		0xef000000
#define SUN4_300_ETH_PHYSADDR		0xf9000000
#define SUN4_300_BWTWO_PHYSADDR		0xfb400000
#define SUN4_300_DMA_PHYSADDR		0xfa001000
#define SUN4_300_ESP_PHYSADDR		0xfa000000

/* Are these right? */
#define SUN4_400_MEMREG_PHYSADDR	0xf4000000
#define SUN4_400_CLOCK_PHYSADDR		0xf2000000
#define SUN4_400_TIMER_PHYSADDR		0xef000000
#define SUN4_400_ETH_PHYSADDR		0xf9000000
#define SUN4_400_BWTWO_PHYSADDR		0xfb400000
#define SUN4_400_DMA_PHYSADDR		0xfa001000
#define SUN4_400_ESP_PHYSADDR		0xfa000000

/* 
	these are the actual values set and used in the code. Unused items set 
	to SUN_UNUSED_PHYSADDR 
 */

extern int sun4_memreg_physaddr; /* memory register (ecc?) */
extern int sun4_clock_physaddr;  /* system clock */
extern int sun4_timer_physaddr;  /* timer, where applicable */
extern int sun4_eth_physaddr;    /* onboard ethernet (ie/le) */
extern int sun4_si_physaddr;     /* sun3 scsi adapter */
extern int sun4_bwtwo_physaddr;  /* onboard bw2 */
extern int sun4_dma_physaddr;    /* scsi dma */
extern int sun4_esp_physaddr;    /* esp scsi */
extern int sun4_ie_physaddr;     /* interrupt enable */

#endif /* !(_SPARC_SUN4PADDR_H) */
