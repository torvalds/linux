#ifndef __UM_DMA_H
#define __UM_DMA_H

#include <asm/io.h>

extern unsigned long uml_physmem;

#define MAX_DMA_ADDRESS (uml_physmem)

#endif
