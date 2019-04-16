/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * COMEDI ISA DMA support functions
 * Copyright (c) 2014 H Hartley Sweeten <hsweeten@visionengravers.com>
 */

#ifndef _COMEDI_ISADMA_H
#define _COMEDI_ISADMA_H

#include <linux/types.h>

struct comedi_device;

/*
 * These are used to avoid issues when <asm/dma.h> and the DMA_MODE_
 * defines are not available.
 */
#define COMEDI_ISADMA_READ	0
#define COMEDI_ISADMA_WRITE	1

/**
 * struct comedi_isadma_desc - cookie for ISA DMA
 * @virt_addr:	virtual address of buffer
 * @hw_addr:	hardware (bus) address of buffer
 * @chan:	DMA channel
 * @maxsize:	allocated size of buffer (in bytes)
 * @size:	transfer size (in bytes)
 * @mode:	DMA_MODE_READ or DMA_MODE_WRITE
 */
struct comedi_isadma_desc {
	void *virt_addr;
	dma_addr_t hw_addr;
	unsigned int chan;
	unsigned int maxsize;
	unsigned int size;
	char mode;
};

/**
 * struct comedi_isadma - ISA DMA data
 * @desc:	cookie for each DMA buffer
 * @n_desc:	the number of cookies
 * @cur_dma:	the current cookie in use
 * @chan:	the first DMA channel requested
 * @chan2:	the second DMA channel requested
 */
struct comedi_isadma {
	struct comedi_isadma_desc *desc;
	int n_desc;
	int cur_dma;
	unsigned int chan;
	unsigned int chan2;
};

#if IS_ENABLED(CONFIG_ISA_DMA_API)

void comedi_isadma_program(struct comedi_isadma_desc *desc);
unsigned int comedi_isadma_disable(unsigned int dma_chan);
unsigned int comedi_isadma_disable_on_sample(unsigned int dma_chan,
					     unsigned int size);
unsigned int comedi_isadma_poll(struct comedi_isadma *dma);
void comedi_isadma_set_mode(struct comedi_isadma_desc *desc, char dma_dir);

struct comedi_isadma *comedi_isadma_alloc(struct comedi_device *dev,
					  int n_desc, unsigned int dma_chan1,
					  unsigned int dma_chan2,
					  unsigned int maxsize, char dma_dir);
void comedi_isadma_free(struct comedi_isadma *dma);

#else	/* !IS_ENABLED(CONFIG_ISA_DMA_API) */

static inline void comedi_isadma_program(struct comedi_isadma_desc *desc)
{
}

static inline unsigned int comedi_isadma_disable(unsigned int dma_chan)
{
	return 0;
}

static inline unsigned int
comedi_isadma_disable_on_sample(unsigned int dma_chan, unsigned int size)
{
	return 0;
}

static inline unsigned int comedi_isadma_poll(struct comedi_isadma *dma)
{
	return 0;
}

static inline void comedi_isadma_set_mode(struct comedi_isadma_desc *desc,
					  char dma_dir)
{
}

static inline struct comedi_isadma *
comedi_isadma_alloc(struct comedi_device *dev, int n_desc,
		    unsigned int dma_chan1, unsigned int dma_chan2,
		    unsigned int maxsize, char dma_dir)
{
	return NULL;
}

static inline void comedi_isadma_free(struct comedi_isadma *dma)
{
}

#endif	/* !IS_ENABLED(CONFIG_ISA_DMA_API) */

#endif	/* #ifndef _COMEDI_ISADMA_H */
