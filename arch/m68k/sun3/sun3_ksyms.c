#include <linux/module.h>
#include <linux/types.h>
#include <asm/dvma.h>
#include <asm/idprom.h>

/*
 * Add things here when you find the need for it.
 */
EXPORT_SYMBOL(dvma_map_align);
EXPORT_SYMBOL(dvma_unmap);
EXPORT_SYMBOL(dvma_malloc_align);
EXPORT_SYMBOL(dvma_free);
EXPORT_SYMBOL(idprom);
