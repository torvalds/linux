// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 */

#include <linux/irqchip.h>
#include <linux/logic_pio.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/bootinfo.h>
#include <asm/traps.h>
#include <asm/smp-ops.h>
#include <asm/cacheflush.h>
#include <asm/fw/fw.h>

#include <loongson.h>
#include <boot_param.h>

#define NODE_ID_OFFSET_ADDR	((void __iomem *)TO_UNCAC(0x1001041c))

u32 node_id_offset;

static void __init mips_nmi_setup(void)
{
	void *base;

	base = (void *)(CAC_BASE + 0x380);
	memcpy(base, except_vec_nmi, 0x80);
	flush_icache_range((unsigned long)base, (unsigned long)base + 0x80);
}

void ls7a_early_config(void)
{
	node_id_offset = ((readl(NODE_ID_OFFSET_ADDR) >> 8) & 0x1f) + 36;
}

void rs780e_early_config(void)
{
	node_id_offset = 37;
}

void virtual_early_config(void)
{
	node_id_offset = 44;
}

void __init szmem(unsigned int node)
{
	u32 i, mem_type;
	static unsigned long num_physpages;
	u64 node_id, node_psize, start_pfn, end_pfn, mem_start, mem_size;

	/* Otherwise come from DTB */
	if (loongson_sysconf.fw_interface != LOONGSON_LEFI)
		return;

	/* Parse memory information and activate */
	for (i = 0; i < loongson_memmap->nr_map; i++) {
		node_id = loongson_memmap->map[i].node_id;
		if (node_id != node)
			continue;

		mem_type = loongson_memmap->map[i].mem_type;
		mem_size = loongson_memmap->map[i].mem_size;
		mem_start = loongson_memmap->map[i].mem_start;

		switch (mem_type) {
		case SYSTEM_RAM_LOW:
		case SYSTEM_RAM_HIGH:
			start_pfn = ((node_id << 44) + mem_start) >> PAGE_SHIFT;
			node_psize = (mem_size << 20) >> PAGE_SHIFT;
			end_pfn  = start_pfn + node_psize;
			num_physpages += node_psize;
			pr_info("Node%d: mem_type:%d, mem_start:0x%llx, mem_size:0x%llx MB\n",
				(u32)node_id, mem_type, mem_start, mem_size);
			pr_info("       start_pfn:0x%llx, end_pfn:0x%llx, num_physpages:0x%lx\n",
				start_pfn, end_pfn, num_physpages);
			memblock_add_node(PFN_PHYS(start_pfn), PFN_PHYS(node_psize), node);
			break;
		case SYSTEM_RAM_RESERVED:
			pr_info("Node%d: mem_type:%d, mem_start:0x%llx, mem_size:0x%llx MB\n",
				(u32)node_id, mem_type, mem_start, mem_size);
			memblock_reserve(((node_id << 44) + mem_start), mem_size << 20);
			break;
		}
	}
}

#ifndef CONFIG_NUMA
static void __init prom_init_memory(void)
{
	szmem(0);
}
#endif

void __init prom_init(void)
{
	fw_init_cmdline();

	if (fw_arg2 == 0 || (fdt_magic(fw_arg2) == FDT_MAGIC)) {
		loongson_sysconf.fw_interface = LOONGSON_DTB;
		prom_dtb_init_env();
	} else {
		loongson_sysconf.fw_interface = LOONGSON_LEFI;
		prom_lefi_init_env();
	}

	/* init base address of io space */
	set_io_port_base(PCI_IOBASE);

	if (loongson_sysconf.early_config)
		loongson_sysconf.early_config();

#ifdef CONFIG_NUMA
	prom_init_numa_memory();
#else
	prom_init_memory();
#endif

	/* Hardcode to CPU UART 0 */
	if ((read_c0_prid() & PRID_IMP_MASK) == PRID_IMP_LOONGSON_64R)
		setup_8250_early_printk_port(TO_UNCAC(LOONGSON_REG_BASE), 0, 1024);
	else
		setup_8250_early_printk_port(TO_UNCAC(LOONGSON_REG_BASE + 0x1e0), 0, 1024);

	register_smp_ops(&loongson3_smp_ops);
	board_nmi_handler_setup = mips_nmi_setup;
}

static int __init add_legacy_isa_io(struct fwnode_handle *fwnode, resource_size_t hw_start,
				    resource_size_t size)
{
	int ret = 0;
	struct logic_pio_hwaddr *range;
	unsigned long vaddr;

	range = kzalloc(sizeof(*range), GFP_ATOMIC);
	if (!range)
		return -ENOMEM;

	range->fwnode = fwnode;
	range->size = size;
	range->hw_start = hw_start;
	range->flags = LOGIC_PIO_CPU_MMIO;

	ret = logic_pio_register_range(range);
	if (ret) {
		kfree(range);
		return ret;
	}

	/* Legacy ISA must placed at the start of PCI_IOBASE */
	if (range->io_start != 0) {
		logic_pio_unregister_range(range);
		kfree(range);
		return -EINVAL;
	}

	vaddr = PCI_IOBASE + range->io_start;

	ioremap_page_range(vaddr, vaddr + size, hw_start, pgprot_device(PAGE_KERNEL));

	return 0;
}

static __init void reserve_pio_range(void)
{
	struct device_node *np;

	for_each_node_by_name(np, "isa") {
		struct of_range range;
		struct of_range_parser parser;

		pr_info("ISA Bridge: %pOF\n", np);

		if (of_range_parser_init(&parser, np)) {
			pr_info("Failed to parse resources.\n");
			break;
		}

		for_each_of_range(&parser, &range) {
			switch (range.flags & IORESOURCE_TYPE_BITS) {
			case IORESOURCE_IO:
				pr_info(" IO 0x%016llx..0x%016llx  ->  0x%016llx\n",
					range.cpu_addr,
					range.cpu_addr + range.size - 1,
					range.bus_addr);
				if (add_legacy_isa_io(&np->fwnode, range.cpu_addr, range.size))
					pr_warn("Failed to reserve legacy IO in Logic PIO\n");
				break;
			case IORESOURCE_MEM:
				pr_info(" MEM 0x%016llx..0x%016llx  ->  0x%016llx\n",
					range.cpu_addr,
					range.cpu_addr + range.size - 1,
					range.bus_addr);
				break;
			}
		}
	}
}

void __init arch_init_irq(void)
{
	reserve_pio_range();
	irqchip_init();
}
