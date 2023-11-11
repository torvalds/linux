/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARM_DMA_H
#define __ASM_ARM_DMA_H

/*
 * This is the maximum virtual address which can be DMA'd from.
 */
#ifndef CONFIG_ZONE_DMA
#define MAX_DMA_ADDRESS	0xffffffffUL
#else
#define MAX_DMA_ADDRESS	({ \
	extern phys_addr_t arm_dma_zone_size; \
	arm_dma_zone_size && arm_dma_zone_size < (0x100000000ULL - PAGE_OFFSET) ? \
		(PAGE_OFFSET + arm_dma_zone_size) : 0xffffffffUL; })
#endif

#ifdef CONFIG_ISA_DMA_API
/*
 * This is used to support drivers written for the x86 ISA DMA API.
 * It should not be re-used except for that purpose.
 */
#include <linux/spinlock.h>
#include <linux/scatterlist.h>

#include <mach/isa-dma.h>

/*
 * The DMA modes reflect the settings for the ISA DMA controller
 */
#define DMA_MODE_MASK	 0xcc

#define DMA_MODE_READ	 0x44
#define DMA_MODE_WRITE	 0x48
#define DMA_MODE_CASCADE 0xc0
#define DMA_AUTOINIT	 0x10

extern raw_spinlock_t  dma_spin_lock;

static inline unsigned long claim_dma_lock(void)
{
	unsigned long flags;
	raw_spin_lock_irqsave(&dma_spin_lock, flags);
	return flags;
}

static inline void release_dma_lock(unsigned long flags)
{
	raw_spin_unlock_irqrestore(&dma_spin_lock, flags);
}

/* Clear the 'DMA Pointer Flip Flop'.
 * Write 0 for LSB/MSB, 1 for MSB/LSB access.
 */
#define clear_dma_ff(chan)

/* Set only the page register bits of the transfer address.
 *
 * NOTE: This is an architecture specific function, and should
 *       be hidden from the drivers
 */
extern void set_dma_page(unsigned int chan, char pagenr);

/* Request a DMA channel
 *
 * Some architectures may need to do allocate an interrupt
 */
extern int  request_dma(unsigned int chan, const char * device_id);

/* Free a DMA channel
 *
 * Some architectures may need to do free an interrupt
 */
extern void free_dma(unsigned int chan);

/* Enable DMA for this channel
 *
 * On some architectures, this may have other side effects like
 * enabling an interrupt and setting the DMA registers.
 */
extern void enable_dma(unsigned int chan);

/* Disable DMA for this channel
 *
 * On some architectures, this may have other side effects like
 * disabling an interrupt or whatever.
 */
extern void disable_dma(unsigned int chan);

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
#define set_dma_addr(chan, addr)				\
	__set_dma_addr(chan, (void *)isa_bus_to_virt(addr))

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

#ifndef NO_DMA
#define NO_DMA	255
#endif

#endif /* CONFIG_ISA_DMA_API */

#endif /* __ASM_ARM_DMA_H */
