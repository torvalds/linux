/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ni_labpc ISA DMA support.
 */

#ifndef _NI_LABPC_ISADMA_H
#define _NI_LABPC_ISADMA_H

#if IS_ENABLED(CONFIG_COMEDI_NI_LABPC_ISADMA)

void labpc_init_dma_chan(struct comedi_device *dev, unsigned int dma_chan);
void labpc_free_dma_chan(struct comedi_device *dev);
void labpc_setup_dma(struct comedi_device *dev, struct comedi_subdevice *s);
void labpc_drain_dma(struct comedi_device *dev);
void labpc_handle_dma_status(struct comedi_device *dev);

#else

static inline void labpc_init_dma_chan(struct comedi_device *dev,
				       unsigned int dma_chan)
{
}

static inline void labpc_free_dma_chan(struct comedi_device *dev)
{
}

static inline void labpc_setup_dma(struct comedi_device *dev,
				   struct comedi_subdevice *s)
{
}

static inline void labpc_drain_dma(struct comedi_device *dev)
{
}

static inline void labpc_handle_dma_status(struct comedi_device *dev)
{
}

#endif

#endif /* _NI_LABPC_ISADMA_H */
