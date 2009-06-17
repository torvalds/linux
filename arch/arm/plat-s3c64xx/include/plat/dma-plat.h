/* linux/arch/arm/plat-s3c64xx/include/plat/dma-plat.h
 *
 * Copyright 2009 Openmoko, Inc.
 * Copyright 2009 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C64XX DMA core
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define DMACH_LOW_LEVEL (1<<28) /* use this to specifiy hardware ch no */

struct s3c64xx_dma_buff;

/** s3c64xx_dma_buff - S3C64XX DMA buffer descriptor
 * @next: Pointer to next buffer in queue or ring.
 * @pw: Client provided identifier
 * @lli: Pointer to hardware descriptor this buffer is associated with.
 * @lli_dma: Hardare address of the descriptor.
 */
struct s3c64xx_dma_buff {
	struct s3c64xx_dma_buff *next;

	void			*pw;
	struct pl080_lli	*lli;
	dma_addr_t		 lli_dma;
};

struct s3c64xx_dmac;

struct s3c2410_dma_chan {
	unsigned char		 number;      /* number of this dma channel */
	unsigned char		 in_use;      /* channel allocated */
	unsigned char		 bit;	      /* bit for enable/disable/etc */
	unsigned char		 hw_width;
	unsigned char		 peripheral;

	unsigned int		 flags;
	enum s3c2410_dmasrc	 source;


	dma_addr_t		dev_addr;

	struct s3c2410_dma_client *client;
	struct s3c64xx_dmac	*dmac;		/* pointer to controller */

	void __iomem		*regs;

	/* cdriver callbacks */
	s3c2410_dma_cbfn_t	 callback_fn;	/* buffer done callback */
	s3c2410_dma_opfn_t	 op_fn;		/* channel op callback */

	/* buffer list and information */
	struct s3c64xx_dma_buff	*curr;		/* current dma buffer */
	struct s3c64xx_dma_buff	*next;		/* next buffer to load */
	struct s3c64xx_dma_buff	*end;		/* end of queue */

	/* note, when channel is running in circular mode, curr is the
	 * first buffer enqueued, end is the last and curr is where the
	 * last buffer-done event is set-at. The buffers are not freed
	 * and the last buffer hardware descriptor points back to the
	 * first.
	 */
};

#include <plat/dma-core.h>
