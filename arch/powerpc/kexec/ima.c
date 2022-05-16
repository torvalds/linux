// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 IBM Corporation
 *
 * Authors:
 * Thiago Jung Bauermann <bauerman@linux.vnet.ibm.com>
 */

#include <linux/slab.h>
#include <linux/kexec.h>
#include <linux/of.h>
#include <linux/memblock.h>
#include <linux/libfdt.h>

static int get_addr_size_cells(int *addr_cells, int *size_cells)
{
	struct device_node *root;

	root = of_find_node_by_path("/");
	if (!root)
		return -EINVAL;

	*addr_cells = of_n_addr_cells(root);
	*size_cells = of_n_size_cells(root);

	of_node_put(root);

	return 0;
}

static int do_get_kexec_buffer(const void *prop, int len, unsigned long *addr,
			       size_t *size)
{
	int ret, addr_cells, size_cells;

	ret = get_addr_size_cells(&addr_cells, &size_cells);
	if (ret)
		return ret;

	if (len < 4 * (addr_cells + size_cells))
		return -ENOENT;

	*addr = of_read_number(prop, addr_cells);
	*size = of_read_number(prop + 4 * addr_cells, size_cells);

	return 0;
}

/**
 * ima_get_kexec_buffer - get IMA buffer from the previous kernel
 * @addr:	On successful return, set to point to the buffer contents.
 * @size:	On successful return, set to the buffer size.
 *
 * Return: 0 on success, negative errno on error.
 */
int ima_get_kexec_buffer(void **addr, size_t *size)
{
	int ret, len;
	unsigned long tmp_addr;
	size_t tmp_size;
	const void *prop;

	prop = of_get_property(of_chosen, "linux,ima-kexec-buffer", &len);
	if (!prop)
		return -ENOENT;

	ret = do_get_kexec_buffer(prop, len, &tmp_addr, &tmp_size);
	if (ret)
		return ret;

	*addr = __va(tmp_addr);
	*size = tmp_size;

	return 0;
}

/**
 * ima_free_kexec_buffer - free memory used by the IMA buffer
 */
int ima_free_kexec_buffer(void)
{
	int ret;
	unsigned long addr;
	size_t size;
	struct property *prop;

	prop = of_find_property(of_chosen, "linux,ima-kexec-buffer", NULL);
	if (!prop)
		return -ENOENT;

	ret = do_get_kexec_buffer(prop->value, prop->length, &addr, &size);
	if (ret)
		return ret;

	ret = of_remove_property(of_chosen, prop);
	if (ret)
		return ret;

	return memblock_free(addr, size);

}

/**
 * remove_ima_buffer - remove the IMA buffer property and reservation from @fdt
 *
 * The IMA measurement buffer is of no use to a subsequent kernel, so we always
 * remove it from the device tree.
 */
void remove_ima_buffer(void *fdt, int chosen_node)
{
	int ret, len;
	unsigned long addr;
	size_t size;
	const void *prop;

	prop = fdt_getprop(fdt, chosen_node, "linux,ima-kexec-buffer", &len);
	if (!prop)
		return;

	ret = do_get_kexec_buffer(prop, len, &addr, &size);
	fdt_delprop(fdt, chosen_node, "linux,ima-kexec-buffer");
	if (ret)
		return;

	ret = delete_fdt_mem_rsv(fdt, addr, size);
	if (!ret)
		pr_debug("Removed old IMA buffer reservation.\n");
}

#ifdef CONFIG_IMA_KEXEC
/**
 * arch_ima_add_kexec_buffer - do arch-specific steps to add the IMA buffer
 *
 * Architectures should use this function to pass on the IMA buffer
 * information to the next kernel.
 *
 * Return: 0 on success, negative errno on error.
 */
int arch_ima_add_kexec_buffer(struct kimage *image, unsigned long load_addr,
			      size_t size)
{
	image->arch.ima_buffer_addr = load_addr;
	image->arch.ima_buffer_size = size;

	return 0;
}

static int write_number(void *p, u64 value, int cells)
{
	if (cells == 1) {
		u32 tmp;

		if (value > U32_MAX)
			return -EINVAL;

		tmp = cpu_to_be32(value);
		memcpy(p, &tmp, sizeof(tmp));
	} else if (cells == 2) {
		u64 tmp;

		tmp = cpu_to_be64(value);
		memcpy(p, &tmp, sizeof(tmp));
	} else
		return -EINVAL;

	return 0;
}

/**
 * setup_ima_buffer - add IMA buffer information to the fdt
 * @image:		kexec image being loaded.
 * @fdt:		Flattened device tree for the next kernel.
 * @chosen_node:	Offset to the chosen node.
 *
 * Return: 0 on success, or negative errno on error.
 */
int setup_ima_buffer(const struct kimage *image, void *fdt, int chosen_node)
{
	int ret, addr_cells, size_cells, entry_size;
	u8 value[16];

	remove_ima_buffer(fdt, chosen_node);
	if (!image->arch.ima_buffer_size)
		return 0;

	ret = get_addr_size_cells(&addr_cells, &size_cells);
	if (ret)
		return ret;

	entry_size = 4 * (addr_cells + size_cells);

	if (entry_size > sizeof(value))
		return -EINVAL;

	ret = write_number(value, image->arch.ima_buffer_addr, addr_cells);
	if (ret)
		return ret;

	ret = write_number(value + 4 * addr_cells, image->arch.ima_buffer_size,
			   size_cells);
	if (ret)
		return ret;

	ret = fdt_setprop(fdt, chosen_node, "linux,ima-kexec-buffer", value,
			  entry_size);
	if (ret < 0)
		return -EINVAL;

	ret = fdt_add_mem_rsv(fdt, image->arch.ima_buffer_addr,
			      image->arch.ima_buffer_size);
	if (ret)
		return -EINVAL;

	pr_debug("IMA buffer at 0x%llx, size = 0x%zx\n",
		 image->arch.ima_buffer_addr, image->arch.ima_buffer_size);

	return 0;
}
#endif /* CONFIG_IMA_KEXEC */
