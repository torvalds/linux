/*
 *  arch/arm/include/asm/mach/dma.h
 *
 *  Copyright (C) 1998-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This header file describes the interface between the generic DMA handler
 *  (dma.c) and the architecture-specific DMA backends (dma-*.c)
 */

struct dma_struct;
typedef struct dma_struct dma_t;

struct dma_ops {
	int	(*request)(unsigned int, dma_t *);		/* optional */
	int	(*free)(unsigned int, dma_t *);			/* optional */
	int	(*enable)(unsigned int, dma_t *);		/* mandatory */
	int	(*disable)(unsigned int, dma_t *);		/* mandatory */
	void (*position)(unsigned int, dma_t *);
#if 0	
	int	(*residue)(unsigned int, dma_t *);		/* optional */
	int	(*setspeed)(unsigned int, dma_t *, int);	/* optional */
	const char *type;
#endif
};

struct dma_struct {
	void		*addr;		/* single DMA address		*/
	unsigned long	count;		/* single DMA size		*/
	struct scatterlist buf;		/* single DMA			*/
	int		sgcount;	/* number of DMA SG		*/
	struct scatterlist *sg;		/* DMA Scatter-Gather List	*/

	unsigned int	active:1;	/* Transfer active		*/
	unsigned int	invalid:1;	/* Address/Count changed	*/

	unsigned int	dma_mode;	/* DMA mode			*/
	int		speed;		/* DMA speed			*/

	unsigned int	lock;		/* Device is allocated		*/
	const char	*device_id;	/* Device name			*/

    void (*irqHandle)(int irq, void *dev_id);    /*irq callback*/
    void *data;
    unsigned int irq_mode;

    dma_addr_t  src_pos;
    dma_addr_t  dst_pos;
    
	const struct dma_ops *d_ops;
};

/*
 * isa_dma_add - add an ISA-style DMA channel
 */
extern int dma_add(unsigned int, dma_t *dma);

