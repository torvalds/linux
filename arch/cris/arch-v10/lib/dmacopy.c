/* $Id: dmacopy.c,v 1.1 2001/12/17 13:59:27 bjornw Exp $ 
 *
 * memcpy for large blocks, using memory-memory DMA channels 6 and 7 in Etrax
 */

#include <asm/svinto.h>
#include <asm/io.h>

#define D(x)

void *dma_memcpy(void *pdst,
		 const void *psrc,
		 unsigned int pn)
{
	static etrax_dma_descr indma, outdma;
	
	D(printk("dma_memcpy %d bytes... ", pn));

#if 0
	*R_GEN_CONFIG = genconfig_shadow = 
		(genconfig_shadow & ~0x3c0000) |
		IO_STATE(R_GEN_CONFIG, dma6, intdma7) |
		IO_STATE(R_GEN_CONFIG, dma7, intdma6);
#endif
	indma.sw_len = outdma.sw_len = pn;
	indma.ctrl = d_eol | d_eop;
	outdma.ctrl = d_eol;
	indma.buf = psrc;
	outdma.buf = pdst;

	*R_DMA_CH6_FIRST = &indma;
	*R_DMA_CH7_FIRST = &outdma;
	*R_DMA_CH6_CMD = IO_STATE(R_DMA_CH6_CMD, cmd, start);
	*R_DMA_CH7_CMD = IO_STATE(R_DMA_CH7_CMD, cmd, start);
	
	while(*R_DMA_CH7_CMD == 1) /* wait for completion */ ;

	D(printk("done\n"));

}



