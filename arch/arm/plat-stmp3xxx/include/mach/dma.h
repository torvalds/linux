/*
 * Freescale STMP37XX/STMP378X DMA helper interface
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __ASM_PLAT_STMP3XXX_DMA_H
#define __ASM_PLAT_STMP3XXX_DMA_H

#include <linux/platform_device.h>
#include <linux/dmapool.h>

#if !defined(MAX_PIO_WORDS)
#define MAX_PIO_WORDS   (15)
#endif

#define STMP3XXX_BUS_APBH		0
#define STMP3XXX_BUS_APBX		1
#define STMP3XXX_DMA_MAX_CHANNEL	16
#define STMP3XXX_DMA_BUS(dma)		((dma) / 16)
#define STMP3XXX_DMA_CHANNEL(dma)	((dma) % 16)
#define STMP3XXX_DMA(channel, bus)	((bus) * 16 + (channel))
#define MAX_DMA_ADDRESS			0xffffffff
#define MAX_DMA_CHANNELS		32

struct stmp3xxx_dma_command {
	u32 next;
	u32 cmd;
	union {
		u32 buf_ptr;
		u32 alternate;
	};
	u32 pio_words[MAX_PIO_WORDS];
};

struct stmp3xxx_dma_descriptor {
	struct stmp3xxx_dma_command *command;
	dma_addr_t handle;

	/* The virtual address of the buffer pointer */
	void *virtual_buf_ptr;
	/* The next descriptor in a the DMA chain (optional) */
	struct stmp3xxx_dma_descriptor *next_descr;
};

struct stmp37xx_circ_dma_chain {
	unsigned total_count;
	struct stmp3xxx_dma_descriptor *chain;

	unsigned free_index;
	unsigned free_count;
	unsigned active_index;
	unsigned active_count;
	unsigned cooked_index;
	unsigned cooked_count;

	int bus;
	unsigned channel;
};

static inline struct stmp3xxx_dma_descriptor
    *stmp3xxx_dma_circ_get_free_head(struct stmp37xx_circ_dma_chain *chain)
{
	return &(chain->chain[chain->free_index]);
}

static inline struct stmp3xxx_dma_descriptor
    *stmp3xxx_dma_circ_get_cooked_head(struct stmp37xx_circ_dma_chain *chain)
{
	return &(chain->chain[chain->cooked_index]);
}

int stmp3xxx_dma_request(int ch, struct device *dev, const char *name);
int stmp3xxx_dma_release(int ch);
int stmp3xxx_dma_allocate_command(int ch,
				  struct stmp3xxx_dma_descriptor *descriptor);
int stmp3xxx_dma_free_command(int ch,
			      struct stmp3xxx_dma_descriptor *descriptor);
void stmp3xxx_dma_continue(int channel, u32 semaphore);
void stmp3xxx_dma_go(int ch, struct stmp3xxx_dma_descriptor *head,
		     u32 semaphore);
int stmp3xxx_dma_running(int ch);
int stmp3xxx_dma_make_chain(int ch, struct stmp37xx_circ_dma_chain *chain,
			    struct stmp3xxx_dma_descriptor descriptors[],
			    unsigned items);
void stmp3xxx_dma_free_chain(struct stmp37xx_circ_dma_chain *chain);
void stmp37xx_circ_clear_chain(struct stmp37xx_circ_dma_chain *chain);
void stmp37xx_circ_advance_free(struct stmp37xx_circ_dma_chain *chain,
		unsigned count);
void stmp37xx_circ_advance_active(struct stmp37xx_circ_dma_chain *chain,
		unsigned count);
unsigned stmp37xx_circ_advance_cooked(struct stmp37xx_circ_dma_chain *chain);
int stmp3xxx_dma_read_semaphore(int ch);
void stmp3xxx_dma_init(void);
void stmp3xxx_dma_set_alt_target(int ch, int target);
void stmp3xxx_dma_suspend(void);
void stmp3xxx_dma_resume(void);

/*
 * STMP37xx and STMP378x have different DMA control
 * registers layout
 */

void stmp3xxx_arch_dma_freeze(int ch);
void stmp3xxx_arch_dma_unfreeze(int ch);
void stmp3xxx_arch_dma_reset_channel(int ch);
void stmp3xxx_arch_dma_enable_interrupt(int ch);
void stmp3xxx_arch_dma_clear_interrupt(int ch);
int stmp3xxx_arch_dma_is_interrupt(int ch);

static inline void stmp3xxx_dma_reset_channel(int ch)
{
	stmp3xxx_arch_dma_reset_channel(ch);
}


static inline void stmp3xxx_dma_freeze(int ch)
{
	stmp3xxx_arch_dma_freeze(ch);
}

static inline void stmp3xxx_dma_unfreeze(int ch)
{
	stmp3xxx_arch_dma_unfreeze(ch);
}

static inline void stmp3xxx_dma_enable_interrupt(int ch)
{
	stmp3xxx_arch_dma_enable_interrupt(ch);
}

static inline void stmp3xxx_dma_clear_interrupt(int ch)
{
	stmp3xxx_arch_dma_clear_interrupt(ch);
}

static inline int stmp3xxx_dma_is_interrupt(int ch)
{
	return stmp3xxx_arch_dma_is_interrupt(ch);
}

#endif /* __ASM_PLAT_STMP3XXX_DMA_H */
