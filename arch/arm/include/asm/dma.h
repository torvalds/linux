#ifndef __ASM_ARM_DMA_H
#define __ASM_ARM_DMA_H

#include <asm/memory.h>

/*
 * This is the maximum virtual address which can be DMA'd from.
 */
#ifndef MAX_DMA_ADDRESS
#define MAX_DMA_ADDRESS	0xffffffff
#endif
/*
 * This is used to support drivers written for the x86 ISA DMA API.
 * It should not be re-used except for that purpose.
 */
#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/scatterlist.h>


#define RK28_DMA_CH0      0
#define RK28_DMA_CH1      1
#define RK28_DMA_CH2      2
#define RK28_DMA_CH3      3
#define RK28_DMA_CH4      4
#define RK28_DMA_CH5      5

#define MAX_DMA_CHANNELS      6


/*
"sd_mmc",
"uart_2",
"uart_3",
"sdio",
"i2s",
"spi_m",
"spi_s",
"uart_0",
"uart_1",
*/

/*
 * The DMA modes reflect the settings for the ISA DMA controller
 */
#define DMA_MODE_MASK	 0xcc

#define DMA_MODE_READ	 0x44
#define DMA_MODE_WRITE	 0x48
#define DMA_MODE_CASCADE 0xc0
#define DMA_AUTOINIT	 0x10


/*
 * The DMA irq modes 
 */
#define DMA_IRQ_DELAY_MODE	     0x00   /*performan in software irq handleer*/
#define DMA_IRQ_RIGHTNOW_MODE	 0x01   /*performan in dwdma irq handleer*/


/* Request a DMA channel
 *
 * Some architectures may need to do allocate an interrupt
 */
extern int  request_dma(unsigned int chan, const char * device_id);

/* Free a DMA channel
 *
 * Some architectures may need to do free an interrupt
 */
extern int free_dma(unsigned int chan);

/* Enable DMA for this channel
 *
 * On some architectures, this may have other side effects like
 * enabling an interrupt and setting the DMA registers.
 */
extern int enable_dma(unsigned int chan);

/* Disable DMA for this channel
 *
 * On some architectures, this may have other side effects like
 * disabling an interrupt or whatever.
 */
extern int disable_dma(unsigned int chan);

/* Test whether the specified channel has an active DMA transfer
 */
extern int dma_channel_active(unsigned int chan);

/* Set the DMA scatter gather list for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA address immediately, but defer it to the enable_dma().
 */
extern void set_dma_sg(unsigned int chan, struct scatterlist *sg, int nr_sg);

/* Set the DMA address for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA address immediately, but defer it to the enable_dma().
 */
extern void __set_dma_addr(unsigned int chan, void *addr);

#define set_dma_addr(chan, addr)	__set_dma_addr(chan, addr)//__set_dma_addr(chan, bus_to_virt(addr))

/* Set the DMA byte count for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA count immediately, but defer it to the enable_dma().
 */
extern void set_dma_count(unsigned int chan, unsigned long count);

/* Set the transfer direction for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA transfer direction immediately, but defer it to the
 * enable_dma().
 */
extern void set_dma_mode(unsigned int chan, unsigned int mode);
#if 0
/* Set the transfer speed for this channel
 */
extern void set_dma_speed(unsigned int chan, int cycle_ns);

/* Get DMA residue count. After a DMA transfer, this
 * should return zero. Reading this while a DMA transfer is
 * still in progress will return unpredictable results.
 * If called before the channel has been used, it may return 1.
 * Otherwise, it returns the number of _bytes_ left to transfer.
 */
extern int  get_dma_residue(unsigned int chan);
#endif
/* Set dam irq callback that perform when dma transfer has completed
 */
extern void set_dma_handler (unsigned int chan, void (*irq_handler) (int, void *), void *data, unsigned int irq_mode);


/*
 * get dma transfer position
 */
extern void get_dma_position(unsigned int chan, dma_addr_t *src_pos, dma_addr_t *dst_pos);

#endif /* __ASM_ARM_DMA_H */
