/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/mach/dma.h
 *
 *  Copyright (C) 1998-2000 Russell King
 *
 *  This header file describes the interface between the generic DMA handler
 *  (dma.c) and the architecture-specific DMA backends (dma-*.c)
 */

struct dma_struct;
typedef struct dma_struct dma_t;

struct dma_ops {
	int	(*request)(unsigned int, dma_t *);		/* optional */
	void	(*free)(unsigned int, dma_t *);			/* optional */
	void	(*enable)(unsigned int, dma_t *);		/* mandatory */
	void 	(*disable)(unsigned int, dma_t *);		/* mandatory */
	int	(*residue)(unsigned int, dma_t *);		/* optional */
	int	(*setspeed)(unsigned int, dma_t *, int);	/* optional */
	const char *type;
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

	const struct dma_ops *d_ops;
};

/*
 * isa_dma_add - add an ISA-style DMA channel
 */
extern int isa_dma_add(unsigned int, dma_t *dma);

/*
 * Add the ISA DMA controller.  Always takes channels 0-7.
 */
extern void isa_init_dma(void);
