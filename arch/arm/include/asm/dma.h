#ifndef __ASM_ARM_DMA_H
#define __ASM_ARM_DMA_H

#include <asm/memory.h>

/*
 * This is the maximum virtual address which can be DMA'd from.
 */
#ifndef MAX_DMA_ADDRESS
#define MAX_DMA_ADDRESS	0xffffffff
#endif

#ifdef CONFIG_ISA_DMA_API
/*
 * This is used to support drivers written for the x86 ISA DMA API.
 * It should not be re-used except for that purpose.
 */
#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/scatterlist.h>

typedef unsigned int dmach_t;

#include <mach/isa-dma.h>

/*
 * DMA modes
 */
typedef unsigned int dmamode_t;

#define DMA_MODE_MASK	3

#define DMA_MODE_READ	 0
#define DMA_MODE_WRITE	 1
#define DMA_MODE_CASCADE 2
#define DMA_AUTOINIT	 4

extern spinlock_t  dma_spin_lock;

static inline unsigned long claim_dma_lock(void)
{
	unsigned long flags;
	spin_lock_irqsave(&dma_spin_lock, flags);
	return flags;
}

static inline void release_dma_lock(unsigned long flags)
{
	spin_unlock_irqrestore(&dma_spin_lock, flags);
}

/* Clear the 'DMA Pointer Flip Flop'.
 * Write 0 for LSB/MSB, 1 for MSB/LSB access.
 */
#define clear_dma_ff(channel)

/* Set only the page register bits of the transfer address.
 *
 * NOTE: This is an architecture specific function, and should
 *       be hidden from the drivers
 */
extern void set_dma_page(dmach_t channel, char pagenr);

/* Request a DMA channel
 *
 * Some architectures may need to do allocate an interrupt
 */
extern int  request_dma(dmach_t channel, const char * device_id);

/* Free a DMA channel
 *
 * Some architectures may need to do free an interrupt
 */
extern void free_dma(dmach_t channel);

/* Enable DMA for this channel
 *
 * On some architectures, this may have other side effects like
 * enabling an interrupt and setting the DMA registers.
 */
extern void enable_dma(dmach_t channel);

/* Disable DMA for this channel
 *
 * On some architectures, this may have other side effects like
 * disabling an interrupt or whatever.
 */
extern void disable_dma(dmach_t channel);

/* Test whether the specified channel has an active DMA transfer
 */
extern int dma_channel_active(dmach_t channel);

/* Set the DMA scatter gather list for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA address immediately, but defer it to the enable_dma().
 */
extern void set_dma_sg(dmach_t channel, struct scatterlist *sg, int nr_sg);

/* Set the DMA address for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA address immediately, but defer it to the enable_dma().
 */
extern void __set_dma_addr(dmach_t channel, void *addr);
#define set_dma_addr(channel, addr)				\
	__set_dma_addr(channel, bus_to_virt(addr))

/* Set the DMA byte count for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA count immediately, but defer it to the enable_dma().
 */
extern void set_dma_count(dmach_t channel, unsigned long count);

/* Set the transfer direction for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA transfer direction immediately, but defer it to the
 * enable_dma().
 */
extern void set_dma_mode(dmach_t channel, dmamode_t mode);

/* Set the transfer speed for this channel
 */
extern void set_dma_speed(dmach_t channel, int cycle_ns);

/* Get DMA residue count. After a DMA transfer, this
 * should return zero. Reading this while a DMA transfer is
 * still in progress will return unpredictable results.
 * If called before the channel has been used, it may return 1.
 * Otherwise, it returns the number of _bytes_ left to transfer.
 */
extern int  get_dma_residue(dmach_t channel);

#ifndef NO_DMA
#define NO_DMA	255
#endif

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy    (0)
#endif

#endif /* CONFIG_ISA_DMA_API */

#endif /* __ASM_ARM_DMA_H */
