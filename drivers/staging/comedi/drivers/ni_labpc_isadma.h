/*
 * ni_labpc ISA DMA support.
*/

#ifndef _NI_LABPC_ISADMA_H
#define _NI_LABPC_ISADMA_H

#define NI_LABPC_HAVE_ISA_DMA	IS_ENABLED(CONFIG_COMEDI_NI_LABPC_ISADMA)

#if NI_LABPC_HAVE_ISA_DMA

#else

#endif

#endif /* _NI_LABPC_ISADMA_H */
