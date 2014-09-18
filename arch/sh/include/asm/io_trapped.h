#ifndef __ASM_SH_IO_TRAPPED_H
#define __ASM_SH_IO_TRAPPED_H

#include <linux/list.h>
#include <linux/ioport.h>
#include <asm/page.h>

#define IO_TRAPPED_MAGIC 0xfeedbeef

struct trapped_io {
	unsigned int magic;
	struct resource *resource;
	unsigned int num_resources;
	unsigned int minimum_bus_width;
	struct list_head list;
	void __iomem *virt_base;
} __aligned(PAGE_SIZE);

#ifdef CONFIG_IO_TRAPPED
int register_trapped_io(struct trapped_io *tiop);
int handle_trapped_io(struct pt_regs *regs, unsigned long address);

void __iomem *match_trapped_io_handler(struct list_head *list,
				       unsigned long offset,
				       unsigned long size);

#ifdef CONFIG_HAS_IOMEM
extern struct list_head trapped_mem;

static inline void __iomem *
__ioremap_trapped(unsigned long offset, unsigned long size)
{
	return match_trapped_io_handler(&trapped_mem, offset, size);
}
#else
#define __ioremap_trapped(offset, size) NULL
#endif

#ifdef CONFIG_HAS_IOPORT_MAP
extern struct list_head trapped_io;

static inline void __iomem *
__ioport_map_trapped(unsigned long offset, unsigned long size)
{
	return match_trapped_io_handler(&trapped_io, offset, size);
}
#else
#define __ioport_map_trapped(offset, size) NULL
#endif

#else
#define register_trapped_io(tiop) (-1)
#define handle_trapped_io(tiop, address) 0
#define __ioremap_trapped(offset, size) NULL
#define __ioport_map_trapped(offset, size) NULL
#endif

#endif /* __ASM_SH_IO_TRAPPED_H */
