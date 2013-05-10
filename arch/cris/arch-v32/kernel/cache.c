#include <linux/module.h>
#include <asm/io.h>
#include <arch/cache.h>
#include <arch/hwregs/dma.h>

/* This file is used to workaround a cache bug, Guinness TR 106. */

inline void flush_dma_descr(struct dma_descr_data *descr, int flush_buf)
{
	/* Flush descriptor to make sure we get correct in_eop and after. */
	asm volatile ("ftagd [%0]" :: "r" (descr));
	/* Flush buffer pointed out by descriptor. */
	if (flush_buf)
		cris_flush_cache_range(phys_to_virt((unsigned)descr->buf),
				(unsigned)(descr->after - descr->buf));
}
EXPORT_SYMBOL(flush_dma_descr);

void flush_dma_list(struct dma_descr_data *descr)
{
	while (1) {
		flush_dma_descr(descr, 1);
		if (descr->eol)
			break;
		descr = phys_to_virt((unsigned)descr->next);
	}
}
EXPORT_SYMBOL(flush_dma_list);

/* From cacheflush.S */
EXPORT_SYMBOL(cris_flush_cache);
/* From cacheflush.S */
EXPORT_SYMBOL(cris_flush_cache_range);
