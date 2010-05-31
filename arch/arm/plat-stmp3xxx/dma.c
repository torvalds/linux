/*
 * DMA helper routines for Freescale STMP37XX/STMP378X
 *
 * Author: dmitry pervushin <dpervushin@embeddedalley.com>
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
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/sysdev.h>
#include <linux/cpufreq.h>

#include <asm/page.h>

#include <mach/platform.h>
#include <mach/dma.h>
#include <mach/regs-apbx.h>
#include <mach/regs-apbh.h>

static const size_t pool_item_size = sizeof(struct stmp3xxx_dma_command);
static const size_t pool_alignment = 8;
static struct stmp3xxx_dma_user {
	void *pool;
	int inuse;
	const char *name;
} channels[MAX_DMA_CHANNELS];

#define IS_VALID_CHANNEL(ch) ((ch) >= 0 && (ch) < MAX_DMA_CHANNELS)
#define IS_USED(ch) (channels[ch].inuse)

int stmp3xxx_dma_request(int ch, struct device *dev, const char *name)
{
	struct stmp3xxx_dma_user *user;
	int err = 0;

	user = channels + ch;
	if (!IS_VALID_CHANNEL(ch)) {
		err = -ENODEV;
		goto out;
	}
	if (IS_USED(ch)) {
		err = -EBUSY;
		goto out;
	}
	/* Create a pool to allocate dma commands from */
	user->pool = dma_pool_create(name, dev, pool_item_size,
				     pool_alignment, PAGE_SIZE);
	if (user->pool == NULL) {
		err = -ENOMEM;
		goto out;
	}
	user->name = name;
	user->inuse++;
out:
	return err;
}
EXPORT_SYMBOL(stmp3xxx_dma_request);

int stmp3xxx_dma_release(int ch)
{
	struct stmp3xxx_dma_user *user = channels + ch;
	int err = 0;

	if (!IS_VALID_CHANNEL(ch)) {
		err = -ENODEV;
		goto out;
	}
	if (!IS_USED(ch)) {
		err = -EBUSY;
		goto out;
	}
	BUG_ON(user->pool == NULL);
	dma_pool_destroy(user->pool);
	user->inuse--;
out:
	return err;
}
EXPORT_SYMBOL(stmp3xxx_dma_release);

int stmp3xxx_dma_read_semaphore(int channel)
{
	int sem = -1;

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		sem = __raw_readl(REGS_APBH_BASE + HW_APBH_CHn_SEMA +
				STMP3XXX_DMA_CHANNEL(channel) * 0x70);
		sem &= BM_APBH_CHn_SEMA_PHORE;
		sem >>= BP_APBH_CHn_SEMA_PHORE;
		break;

	case STMP3XXX_BUS_APBX:
		sem = __raw_readl(REGS_APBX_BASE + HW_APBX_CHn_SEMA +
				STMP3XXX_DMA_CHANNEL(channel) * 0x70);
		sem &= BM_APBX_CHn_SEMA_PHORE;
		sem >>= BP_APBX_CHn_SEMA_PHORE;
		break;
	default:
		BUG();
	}
	return sem;
}
EXPORT_SYMBOL(stmp3xxx_dma_read_semaphore);

int stmp3xxx_dma_allocate_command(int channel,
				  struct stmp3xxx_dma_descriptor *descriptor)
{
	struct stmp3xxx_dma_user *user = channels + channel;
	int err = 0;

	if (!IS_VALID_CHANNEL(channel)) {
		err = -ENODEV;
		goto out;
	}
	if (!IS_USED(channel)) {
		err = -EBUSY;
		goto out;
	}
	if (descriptor == NULL) {
		err = -EINVAL;
		goto out;
	}

	/* Allocate memory for a command from the buffer */
	descriptor->command =
	    dma_pool_alloc(user->pool, GFP_KERNEL, &descriptor->handle);

	/* Check it worked */
	if (!descriptor->command) {
		err = -ENOMEM;
		goto out;
	}

	memset(descriptor->command, 0, pool_item_size);
out:
	WARN_ON(err);
	return err;
}
EXPORT_SYMBOL(stmp3xxx_dma_allocate_command);

int stmp3xxx_dma_free_command(int channel,
			      struct stmp3xxx_dma_descriptor *descriptor)
{
	int err = 0;

	if (!IS_VALID_CHANNEL(channel)) {
		err = -ENODEV;
		goto out;
	}
	if (!IS_USED(channel)) {
		err = -EBUSY;
		goto out;
	}

	/* Return the command memory to the pool */
	dma_pool_free(channels[channel].pool, descriptor->command,
		      descriptor->handle);

	/* Initialise descriptor so we're not tempted to use it */
	descriptor->command = NULL;
	descriptor->handle = 0;
	descriptor->virtual_buf_ptr = NULL;
	descriptor->next_descr = NULL;

	WARN_ON(err);
out:
	return err;
}
EXPORT_SYMBOL(stmp3xxx_dma_free_command);

void stmp3xxx_dma_go(int channel,
		     struct stmp3xxx_dma_descriptor *head, u32 semaphore)
{
	int ch = STMP3XXX_DMA_CHANNEL(channel);
	void __iomem *c, *s;

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		c = REGS_APBH_BASE + HW_APBH_CHn_NXTCMDAR + 0x70 * ch;
		s = REGS_APBH_BASE + HW_APBH_CHn_SEMA + 0x70 * ch;
		break;

	case STMP3XXX_BUS_APBX:
		c = REGS_APBX_BASE + HW_APBX_CHn_NXTCMDAR + 0x70 * ch;
		s = REGS_APBX_BASE + HW_APBX_CHn_SEMA + 0x70 * ch;
		break;

	default:
		return;
	}

	/* Set next command */
	__raw_writel(head->handle, c);
	/* Set counting semaphore (kicks off transfer). Assumes
	   peripheral has been set up correctly */
	__raw_writel(semaphore, s);
}
EXPORT_SYMBOL(stmp3xxx_dma_go);

int stmp3xxx_dma_running(int channel)
{
	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		return (__raw_readl(REGS_APBH_BASE + HW_APBH_CHn_SEMA +
			0x70 * STMP3XXX_DMA_CHANNEL(channel))) &
			    BM_APBH_CHn_SEMA_PHORE;

	case STMP3XXX_BUS_APBX:
		return (__raw_readl(REGS_APBX_BASE + HW_APBX_CHn_SEMA +
			0x70 * STMP3XXX_DMA_CHANNEL(channel))) &
			    BM_APBX_CHn_SEMA_PHORE;
	default:
		BUG();
		return 0;
	}
}
EXPORT_SYMBOL(stmp3xxx_dma_running);

/*
 * Circular dma chain management
 */
void stmp3xxx_dma_free_chain(struct stmp37xx_circ_dma_chain *chain)
{
	int i;

	for (i = 0; i < chain->total_count; i++)
		stmp3xxx_dma_free_command(
			STMP3XXX_DMA(chain->channel, chain->bus),
			&chain->chain[i]);
}
EXPORT_SYMBOL(stmp3xxx_dma_free_chain);

int stmp3xxx_dma_make_chain(int ch, struct stmp37xx_circ_dma_chain *chain,
			    struct stmp3xxx_dma_descriptor descriptors[],
			    unsigned items)
{
	int i;
	int err = 0;

	if (items == 0)
		return err;

	for (i = 0; i < items; i++) {
		err = stmp3xxx_dma_allocate_command(ch, &descriptors[i]);
		if (err) {
			WARN_ON(err);
			/*
			 * Couldn't allocate the whole chain.
			 * deallocate what has been allocated
			 */
			if (i) {
				do {
					stmp3xxx_dma_free_command(ch,
								  &descriptors
								  [i]);
				} while (i-- > 0);
			}
			return err;
		}

		/* link them! */
		if (i > 0) {
			descriptors[i - 1].next_descr = &descriptors[i];
			descriptors[i - 1].command->next =
						descriptors[i].handle;
		}
	}

	/* make list circular */
	descriptors[items - 1].next_descr = &descriptors[0];
	descriptors[items - 1].command->next = descriptors[0].handle;

	chain->total_count = items;
	chain->chain = descriptors;
	chain->free_index = 0;
	chain->active_index = 0;
	chain->cooked_index = 0;
	chain->free_count = items;
	chain->active_count = 0;
	chain->cooked_count = 0;
	chain->bus = STMP3XXX_DMA_BUS(ch);
	chain->channel = STMP3XXX_DMA_CHANNEL(ch);
	return err;
}
EXPORT_SYMBOL(stmp3xxx_dma_make_chain);

void stmp37xx_circ_clear_chain(struct stmp37xx_circ_dma_chain *chain)
{
	BUG_ON(stmp3xxx_dma_running(STMP3XXX_DMA(chain->channel, chain->bus)));
	chain->free_index = 0;
	chain->active_index = 0;
	chain->cooked_index = 0;
	chain->free_count = chain->total_count;
	chain->active_count = 0;
	chain->cooked_count = 0;
}
EXPORT_SYMBOL(stmp37xx_circ_clear_chain);

void stmp37xx_circ_advance_free(struct stmp37xx_circ_dma_chain *chain,
		unsigned count)
{
	BUG_ON(chain->cooked_count < count);

	chain->cooked_count -= count;
	chain->cooked_index += count;
	chain->cooked_index %= chain->total_count;
	chain->free_count += count;
}
EXPORT_SYMBOL(stmp37xx_circ_advance_free);

void stmp37xx_circ_advance_active(struct stmp37xx_circ_dma_chain *chain,
		unsigned count)
{
	void __iomem *c;
	u32 mask_clr, mask;
	BUG_ON(chain->free_count < count);

	chain->free_count -= count;
	chain->free_index += count;
	chain->free_index %= chain->total_count;
	chain->active_count += count;

	switch (chain->bus) {
	case STMP3XXX_BUS_APBH:
		c = REGS_APBH_BASE + HW_APBH_CHn_SEMA + 0x70 * chain->channel;
		mask_clr = BM_APBH_CHn_SEMA_INCREMENT_SEMA;
		mask = BF(count, APBH_CHn_SEMA_INCREMENT_SEMA);
		break;
	case STMP3XXX_BUS_APBX:
		c = REGS_APBX_BASE + HW_APBX_CHn_SEMA + 0x70 * chain->channel;
		mask_clr = BM_APBX_CHn_SEMA_INCREMENT_SEMA;
		mask = BF(count, APBX_CHn_SEMA_INCREMENT_SEMA);
		break;
	default:
		BUG();
		return;
	}

	/* Set counting semaphore (kicks off transfer). Assumes
	   peripheral has been set up correctly */
	stmp3xxx_clearl(mask_clr, c);
	stmp3xxx_setl(mask, c);
}
EXPORT_SYMBOL(stmp37xx_circ_advance_active);

unsigned stmp37xx_circ_advance_cooked(struct stmp37xx_circ_dma_chain *chain)
{
	unsigned cooked;

	cooked = chain->active_count -
	  stmp3xxx_dma_read_semaphore(STMP3XXX_DMA(chain->channel, chain->bus));

	chain->active_count -= cooked;
	chain->active_index += cooked;
	chain->active_index %= chain->total_count;

	chain->cooked_count += cooked;

	return cooked;
}
EXPORT_SYMBOL(stmp37xx_circ_advance_cooked);

void stmp3xxx_dma_set_alt_target(int channel, int function)
{
#if defined(CONFIG_ARCH_STMP37XX)
	unsigned bits = 4;
#elif defined(CONFIG_ARCH_STMP378X)
	unsigned bits = 2;
#else
#error wrong arch
#endif
	int shift = STMP3XXX_DMA_CHANNEL(channel) * bits;
	unsigned mask = (1<<bits) - 1;
	void __iomem *c;

	BUG_ON(function < 0 || function >= (1<<bits));
	pr_debug("%s: channel = %d, using mask %x, "
		 "shift = %d\n", __func__, channel, mask, shift);

	switch (STMP3XXX_DMA_BUS(channel)) {
	case STMP3XXX_BUS_APBH:
		c = REGS_APBH_BASE + HW_APBH_DEVSEL;
		break;
	case STMP3XXX_BUS_APBX:
		c = REGS_APBX_BASE + HW_APBX_DEVSEL;
		break;
	default:
		BUG();
	}
	stmp3xxx_clearl(mask << shift, c);
	stmp3xxx_setl(mask << shift, c);
}
EXPORT_SYMBOL(stmp3xxx_dma_set_alt_target);

void stmp3xxx_dma_suspend(void)
{
	stmp3xxx_setl(BM_APBH_CTRL0_CLKGATE, REGS_APBH_BASE + HW_APBH_CTRL0);
	stmp3xxx_setl(BM_APBX_CTRL0_CLKGATE, REGS_APBX_BASE + HW_APBX_CTRL0);
}

void stmp3xxx_dma_resume(void)
{
	stmp3xxx_clearl(BM_APBH_CTRL0_CLKGATE | BM_APBH_CTRL0_SFTRST,
			REGS_APBH_BASE + HW_APBH_CTRL0);
	stmp3xxx_clearl(BM_APBX_CTRL0_CLKGATE | BM_APBX_CTRL0_SFTRST,
			REGS_APBX_BASE + HW_APBX_CTRL0);
}

#ifdef CONFIG_CPU_FREQ

struct dma_notifier_block {
	struct notifier_block nb;
	void *data;
};

static int dma_cpufreq_notifier(struct notifier_block *self,
				unsigned long phase, void *p)
{
	switch (phase) {
	case CPUFREQ_POSTCHANGE:
		stmp3xxx_dma_resume();
		break;

	case CPUFREQ_PRECHANGE:
		stmp3xxx_dma_suspend();
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct dma_notifier_block dma_cpufreq_nb = {
	.nb = {
		.notifier_call = dma_cpufreq_notifier,
	},
};
#endif /* CONFIG_CPU_FREQ */

void __init stmp3xxx_dma_init(void)
{
	stmp3xxx_clearl(BM_APBH_CTRL0_CLKGATE | BM_APBH_CTRL0_SFTRST,
			REGS_APBH_BASE + HW_APBH_CTRL0);
	stmp3xxx_clearl(BM_APBX_CTRL0_CLKGATE | BM_APBX_CTRL0_SFTRST,
			REGS_APBX_BASE + HW_APBX_CTRL0);
#ifdef CONFIG_CPU_FREQ
	cpufreq_register_notifier(&dma_cpufreq_nb.nb,
				CPUFREQ_TRANSITION_NOTIFIER);
#endif /* CONFIG_CPU_FREQ */
}
