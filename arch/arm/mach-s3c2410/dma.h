/* arch/arm/mach-s3c2410/dma.h
 *
 * Copyright (C) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C24XX DMA support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

extern struct sysdev_class dma_sysclass;
extern struct s3c2410_dma_chan s3c2410_chans[S3C2410_DMA_CHANNELS];

#define DMA_CH_VALID		(1<<31)

struct s3c24xx_dma_addr {
	unsigned long		from;
	unsigned long		to;
};

/* struct s3c24xx_dma_map
 *
 * this holds the mapping information for the channel selected
 * to be connected to the specified device
*/

struct s3c24xx_dma_map {
	const char		*name;
	struct s3c24xx_dma_addr  hw_addr;

	unsigned long		 channels[S3C2410_DMA_CHANNELS];
};

struct s3c24xx_dma_selection {
	struct s3c24xx_dma_map	*map;
	unsigned long		 map_size;
	unsigned long		 dcon_mask;

	void	(*select)(struct s3c2410_dma_chan *chan,
			  struct s3c24xx_dma_map *map);
};

extern int s3c24xx_dma_init_map(struct s3c24xx_dma_selection *sel);
