// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/sizes.h>

static const void *get_prop(const void *fdt, const char *node_path,
			    const char *property, int minlen)
{
	const void *prop;
	int offset, len;

	offset = fdt_path_offset(fdt, node_path);
	if (offset < 0)
		return NULL;

	prop = fdt_getprop(fdt, offset, property, &len);
	if (!prop || len < minlen)
		return NULL;

	return prop;
}

static uint32_t get_cells(const void *fdt, const char *name)
{
	const fdt32_t *prop = get_prop(fdt, "/", name, sizeof(fdt32_t));

	if (!prop) {
		/* default */
		return 1;
	}

	return fdt32_ld(prop);
}

static uint64_t get_val(const fdt32_t *cells, uint32_t ncells)
{
	uint64_t r;

	r = fdt32_ld(cells);
	if (ncells > 1)
		r = (r << 32) | fdt32_ld(cells + 1);

	return r;
}

/*
 * Check the start of physical memory
 *
 * Traditionally, the start address of physical memory is obtained by masking
 * the program counter.  However, this does require that this address is a
 * multiple of 128 MiB, precluding booting Linux on platforms where this
 * requirement is not fulfilled.
 * Hence validate the calculated address against the memory information in the
 * DTB, and, if out-of-range, replace it by the real start address.
 * To preserve backwards compatibility (systems reserving a block of memory
 * at the start of physical memory, kdump, ...), the traditional method is
 * always used if it yields a valid address.
 *
 * Return value: start address of physical memory to use
 */
uint32_t fdt_check_mem_start(uint32_t mem_start, const void *fdt)
{
	uint32_t addr_cells, size_cells, base;
	uint32_t fdt_mem_start = 0xffffffff;
	const fdt32_t *reg, *endp;
	uint64_t size, end;
	const char *type;
	int offset, len;

	if (!fdt)
		return mem_start;

	if (fdt_magic(fdt) != FDT_MAGIC)
		return mem_start;

	/* There may be multiple cells on LPAE platforms */
	addr_cells = get_cells(fdt, "#address-cells");
	size_cells = get_cells(fdt, "#size-cells");
	if (addr_cells > 2 || size_cells > 2)
		return mem_start;

	/* Walk all memory nodes and regions */
	for (offset = fdt_next_node(fdt, -1, NULL); offset >= 0;
	     offset = fdt_next_node(fdt, offset, NULL)) {
		type = fdt_getprop(fdt, offset, "device_type", NULL);
		if (!type || strcmp(type, "memory"))
			continue;

		reg = fdt_getprop(fdt, offset, "linux,usable-memory", &len);
		if (!reg)
			reg = fdt_getprop(fdt, offset, "reg", &len);
		if (!reg)
			continue;

		for (endp = reg + (len / sizeof(fdt32_t));
		     endp - reg >= addr_cells + size_cells;
		     reg += addr_cells + size_cells) {
			size = get_val(reg + addr_cells, size_cells);
			if (!size)
				continue;

			if (addr_cells > 1 && fdt32_ld(reg)) {
				/* Outside 32-bit address space, skipping */
				continue;
			}

			base = fdt32_ld(reg + addr_cells - 1);
			end = base + size;
			if (mem_start >= base && mem_start < end) {
				/* Calculated address is valid, use it */
				return mem_start;
			}

			if (base < fdt_mem_start)
				fdt_mem_start = base;
		}
	}

	if (fdt_mem_start == 0xffffffff) {
		/* No usable memory found, falling back to default */
		return mem_start;
	}

	/*
	 * The calculated address is not usable.
	 * Use the lowest usable physical memory address from the DTB instead,
	 * and make sure this is a multiple of 2 MiB for phys/virt patching.
	 */
	return round_up(fdt_mem_start, SZ_2M);
}
