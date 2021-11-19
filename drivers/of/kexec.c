// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Arm Limited
 *
 * Based on arch/arm64/kernel/machine_kexec_file.c:
 *  Copyright (C) 2018 Linaro Limited
 *
 * And arch/powerpc/kexec/file_load.c:
 *  Copyright (C) 2016  IBM Corporation
 */

#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/memblock.h>
#include <linux/libfdt.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/types.h>

#define RNG_SEED_SIZE		128

/*
 * Additional space needed for the FDT buffer so that we can add initrd,
 * bootargs, kaslr-seed, rng-seed, useable-memory-range and elfcorehdr.
 */
#define FDT_EXTRA_SPACE 0x1000

/**
 * fdt_find_and_del_mem_rsv - delete memory reservation with given address and size
 *
 * @fdt:	Flattened device tree for the current kernel.
 * @start:	Starting address of the reserved memory.
 * @size:	Size of the reserved memory.
 *
 * Return: 0 on success, or negative errno on error.
 */
static int fdt_find_and_del_mem_rsv(void *fdt, unsigned long start, unsigned long size)
{
	int i, ret, num_rsvs = fdt_num_mem_rsv(fdt);

	for (i = 0; i < num_rsvs; i++) {
		u64 rsv_start, rsv_size;

		ret = fdt_get_mem_rsv(fdt, i, &rsv_start, &rsv_size);
		if (ret) {
			pr_err("Malformed device tree.\n");
			return -EINVAL;
		}

		if (rsv_start == start && rsv_size == size) {
			ret = fdt_del_mem_rsv(fdt, i);
			if (ret) {
				pr_err("Error deleting device tree reservation.\n");
				return -EINVAL;
			}

			return 0;
		}
	}

	return -ENOENT;
}

/**
 * get_addr_size_cells - Get address and size of root node
 *
 * @addr_cells: Return address of the root node
 * @size_cells: Return size of the root node
 *
 * Return: 0 on success, or negative errno on error.
 */
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

/**
 * do_get_kexec_buffer - Get address and size of device tree property
 *
 * @prop: Device tree property
 * @len: Size of @prop
 * @addr: Return address of the node
 * @size: Return size of the node
 *
 * Return: 0 on success, or negative errno on error.
 */
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

	if (!IS_ENABLED(CONFIG_HAVE_IMA_KEXEC))
		return -ENOTSUPP;

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

	if (!IS_ENABLED(CONFIG_HAVE_IMA_KEXEC))
		return -ENOTSUPP;

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
 * @fdt: Flattened Device Tree to update
 * @chosen_node: Offset to the chosen node in the device tree
 *
 * The IMA measurement buffer is of no use to a subsequent kernel, so we always
 * remove it from the device tree.
 */
static void remove_ima_buffer(void *fdt, int chosen_node)
{
	int ret, len;
	unsigned long addr;
	size_t size;
	const void *prop;

	if (!IS_ENABLED(CONFIG_HAVE_IMA_KEXEC))
		return;

	prop = fdt_getprop(fdt, chosen_node, "linux,ima-kexec-buffer", &len);
	if (!prop)
		return;

	ret = do_get_kexec_buffer(prop, len, &addr, &size);
	fdt_delprop(fdt, chosen_node, "linux,ima-kexec-buffer");
	if (ret)
		return;

	ret = fdt_find_and_del_mem_rsv(fdt, addr, size);
	if (!ret)
		pr_debug("Removed old IMA buffer reservation.\n");
}

#ifdef CONFIG_IMA_KEXEC
/**
 * setup_ima_buffer - add IMA buffer information to the fdt
 * @image:		kexec image being loaded.
 * @fdt:		Flattened device tree for the next kernel.
 * @chosen_node:	Offset to the chosen node.
 *
 * Return: 0 on success, or negative errno on error.
 */
static int setup_ima_buffer(const struct kimage *image, void *fdt,
			    int chosen_node)
{
	int ret;

	if (!image->ima_buffer_size)
		return 0;

	ret = fdt_appendprop_addrrange(fdt, 0, chosen_node,
				       "linux,ima-kexec-buffer",
				       image->ima_buffer_addr,
				       image->ima_buffer_size);
	if (ret < 0)
		return -EINVAL;

	ret = fdt_add_mem_rsv(fdt, image->ima_buffer_addr,
			      image->ima_buffer_size);
	if (ret)
		return -EINVAL;

	pr_debug("IMA buffer at 0x%llx, size = 0x%zx\n",
		 image->ima_buffer_addr, image->ima_buffer_size);

	return 0;
}
#else /* CONFIG_IMA_KEXEC */
static inline int setup_ima_buffer(const struct kimage *image, void *fdt,
				   int chosen_node)
{
	return 0;
}
#endif /* CONFIG_IMA_KEXEC */

/*
 * of_kexec_alloc_and_setup_fdt - Alloc and setup a new Flattened Device Tree
 *
 * @image:		kexec image being loaded.
 * @initrd_load_addr:	Address where the next initrd will be loaded.
 * @initrd_len:		Size of the next initrd, or 0 if there will be none.
 * @cmdline:		Command line for the next kernel, or NULL if there will
 *			be none.
 * @extra_fdt_size:	Additional size for the new FDT buffer.
 *
 * Return: fdt on success, or NULL errno on error.
 */
void *of_kexec_alloc_and_setup_fdt(const struct kimage *image,
				   unsigned long initrd_load_addr,
				   unsigned long initrd_len,
				   const char *cmdline, size_t extra_fdt_size)
{
	void *fdt;
	int ret, chosen_node;
	const void *prop;
	size_t fdt_size;

	fdt_size = fdt_totalsize(initial_boot_params) +
		   (cmdline ? strlen(cmdline) : 0) +
		   FDT_EXTRA_SPACE +
		   extra_fdt_size;
	fdt = kvmalloc(fdt_size, GFP_KERNEL);
	if (!fdt)
		return NULL;

	ret = fdt_open_into(initial_boot_params, fdt, fdt_size);
	if (ret < 0) {
		pr_err("Error %d setting up the new device tree.\n", ret);
		goto out;
	}

	/* Remove memory reservation for the current device tree. */
	ret = fdt_find_and_del_mem_rsv(fdt, __pa(initial_boot_params),
				       fdt_totalsize(initial_boot_params));
	if (ret == -EINVAL) {
		pr_err("Error removing memory reservation.\n");
		goto out;
	}

	chosen_node = fdt_path_offset(fdt, "/chosen");
	if (chosen_node == -FDT_ERR_NOTFOUND)
		chosen_node = fdt_add_subnode(fdt, fdt_path_offset(fdt, "/"),
					      "chosen");
	if (chosen_node < 0) {
		ret = chosen_node;
		goto out;
	}

	ret = fdt_delprop(fdt, chosen_node, "linux,elfcorehdr");
	if (ret && ret != -FDT_ERR_NOTFOUND)
		goto out;
	ret = fdt_delprop(fdt, chosen_node, "linux,usable-memory-range");
	if (ret && ret != -FDT_ERR_NOTFOUND)
		goto out;

	/* Did we boot using an initrd? */
	prop = fdt_getprop(fdt, chosen_node, "linux,initrd-start", NULL);
	if (prop) {
		u64 tmp_start, tmp_end, tmp_size;

		tmp_start = fdt64_to_cpu(*((const fdt64_t *) prop));

		prop = fdt_getprop(fdt, chosen_node, "linux,initrd-end", NULL);
		if (!prop) {
			ret = -EINVAL;
			goto out;
		}

		tmp_end = fdt64_to_cpu(*((const fdt64_t *) prop));

		/*
		 * kexec reserves exact initrd size, while firmware may
		 * reserve a multiple of PAGE_SIZE, so check for both.
		 */
		tmp_size = tmp_end - tmp_start;
		ret = fdt_find_and_del_mem_rsv(fdt, tmp_start, tmp_size);
		if (ret == -ENOENT)
			ret = fdt_find_and_del_mem_rsv(fdt, tmp_start,
						       round_up(tmp_size, PAGE_SIZE));
		if (ret == -EINVAL)
			goto out;
	}

	/* add initrd-* */
	if (initrd_load_addr) {
		ret = fdt_setprop_u64(fdt, chosen_node, "linux,initrd-start",
				      initrd_load_addr);
		if (ret)
			goto out;

		ret = fdt_setprop_u64(fdt, chosen_node, "linux,initrd-end",
				      initrd_load_addr + initrd_len);
		if (ret)
			goto out;

		ret = fdt_add_mem_rsv(fdt, initrd_load_addr, initrd_len);
		if (ret)
			goto out;

	} else {
		ret = fdt_delprop(fdt, chosen_node, "linux,initrd-start");
		if (ret && (ret != -FDT_ERR_NOTFOUND))
			goto out;

		ret = fdt_delprop(fdt, chosen_node, "linux,initrd-end");
		if (ret && (ret != -FDT_ERR_NOTFOUND))
			goto out;
	}

	if (image->type == KEXEC_TYPE_CRASH) {
		/* add linux,elfcorehdr */
		ret = fdt_appendprop_addrrange(fdt, 0, chosen_node,
				"linux,elfcorehdr", image->elf_load_addr,
				image->elf_headers_sz);
		if (ret)
			goto out;

		/*
		 * Avoid elfcorehdr from being stomped on in kdump kernel by
		 * setting up memory reserve map.
		 */
		ret = fdt_add_mem_rsv(fdt, image->elf_load_addr,
				      image->elf_headers_sz);
		if (ret)
			goto out;

		/* add linux,usable-memory-range */
		ret = fdt_appendprop_addrrange(fdt, 0, chosen_node,
				"linux,usable-memory-range", crashk_res.start,
				crashk_res.end - crashk_res.start + 1);
		if (ret)
			goto out;
	}

	/* add bootargs */
	if (cmdline) {
		ret = fdt_setprop_string(fdt, chosen_node, "bootargs", cmdline);
		if (ret)
			goto out;
	} else {
		ret = fdt_delprop(fdt, chosen_node, "bootargs");
		if (ret && (ret != -FDT_ERR_NOTFOUND))
			goto out;
	}

	/* add kaslr-seed */
	ret = fdt_delprop(fdt, chosen_node, "kaslr-seed");
	if (ret == -FDT_ERR_NOTFOUND)
		ret = 0;
	else if (ret)
		goto out;

	if (rng_is_initialized()) {
		u64 seed = get_random_u64();

		ret = fdt_setprop_u64(fdt, chosen_node, "kaslr-seed", seed);
		if (ret)
			goto out;
	} else {
		pr_notice("RNG is not initialised: omitting \"%s\" property\n",
			  "kaslr-seed");
	}

	/* add rng-seed */
	if (rng_is_initialized()) {
		void *rng_seed;

		ret = fdt_setprop_placeholder(fdt, chosen_node, "rng-seed",
				RNG_SEED_SIZE, &rng_seed);
		if (ret)
			goto out;
		get_random_bytes(rng_seed, RNG_SEED_SIZE);
	} else {
		pr_notice("RNG is not initialised: omitting \"%s\" property\n",
			  "rng-seed");
	}

	ret = fdt_setprop(fdt, chosen_node, "linux,booted-from-kexec", NULL, 0);
	if (ret)
		goto out;

	remove_ima_buffer(fdt, chosen_node);
	ret = setup_ima_buffer(image, fdt, fdt_path_offset(fdt, "/chosen"));

out:
	if (ret) {
		kvfree(fdt);
		fdt = NULL;
	}

	return fdt;
}
