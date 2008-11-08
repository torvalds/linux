#include <linux/module.h>
#include <linux/irq.h>
#include <arch/dma.h>
#include <arch/intmem.h>
#include <mach/pinmux.h>
#include <arch/io.h>

/* Functions for allocating DMA channels */
EXPORT_SYMBOL(crisv32_request_dma);
EXPORT_SYMBOL(crisv32_free_dma);

/* Functions for handling internal RAM */
EXPORT_SYMBOL(crisv32_intmem_alloc);
EXPORT_SYMBOL(crisv32_intmem_free);
EXPORT_SYMBOL(crisv32_intmem_phys_to_virt);
EXPORT_SYMBOL(crisv32_intmem_virt_to_phys);

/* Functions for handling pinmux */
EXPORT_SYMBOL(crisv32_pinmux_alloc);
EXPORT_SYMBOL(crisv32_pinmux_alloc_fixed);
EXPORT_SYMBOL(crisv32_pinmux_dealloc);
EXPORT_SYMBOL(crisv32_pinmux_dealloc_fixed);
EXPORT_SYMBOL(crisv32_io_get_name);
EXPORT_SYMBOL(crisv32_io_get);

/* Functions masking/unmasking interrupts */
EXPORT_SYMBOL(mask_irq);
EXPORT_SYMBOL(unmask_irq);
