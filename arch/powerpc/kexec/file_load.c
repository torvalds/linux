// SPDX-License-Identifier: GPL-2.0-only
/*
 * powerpc code to implement the kexec_file_load syscall
 *
 * Copyright (C) 2004  Adam Litke (agl@us.ibm.com)
 * Copyright (C) 2004  IBM Corp.
 * Copyright (C) 2004,2005  Milton D Miller II, IBM Corporation
 * Copyright (C) 2005  R Sharada (sharada@in.ibm.com)
 * Copyright (C) 2006  Mohan Kumar M (mohan@in.ibm.com)
 * Copyright (C) 2016  IBM Corporation
 *
 * Based on kexec-tools' kexec-elf-ppc64.c, fs2dt.c.
 * Heavily modified for the kernel by
 * Thiago Jung Bauermann <bauerman@linux.vnet.ibm.com>.
 */

#include <linux/slab.h>
#include <linux/kexec.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <asm/setup.h>
#include <asm/ima.h>

#define SLAVE_CODE_SIZE		256	/* First 0x100 bytes */

/**
 * setup_kdump_cmdline - Prepend "elfcorehdr=<addr> " to command line
 *                       of kdump kernel for exporting the core.
 * @image:               Kexec image
 * @cmdline:             Command line parameters to update.
 * @cmdline_len:         Length of the cmdline parameters.
 *
 * kdump segment must be setup before calling this function.
 *
 * Returns new cmdline buffer for kdump kernel on success, NULL otherwise.
 */
char *setup_kdump_cmdline(struct kimage *image, char *cmdline,
			  unsigned long cmdline_len)
{
	int elfcorehdr_strlen;
	char *cmdline_ptr;

	cmdline_ptr = kzalloc(COMMAND_LINE_SIZE, GFP_KERNEL);
	if (!cmdline_ptr)
		return NULL;

	elfcorehdr_strlen = sprintf(cmdline_ptr, "elfcorehdr=0x%lx ",
				    image->arch.elfcorehdr_addr);

	if (elfcorehdr_strlen + cmdline_len > COMMAND_LINE_SIZE) {
		pr_err("Appending elfcorehdr=<addr> exceeds cmdline size\n");
		kfree(cmdline_ptr);
		return NULL;
	}

	memcpy(cmdline_ptr + elfcorehdr_strlen, cmdline, cmdline_len);
	// Ensure it's nul terminated
	cmdline_ptr[COMMAND_LINE_SIZE - 1] = '\0';
	return cmdline_ptr;
}

/**
 * setup_purgatory - initialize the purgatory's global variables
 * @image:		kexec image.
 * @slave_code:		Slave code for the purgatory.
 * @fdt:		Flattened device tree for the next kernel.
 * @kernel_load_addr:	Address where the kernel is loaded.
 * @fdt_load_addr:	Address where the flattened device tree is loaded.
 *
 * Return: 0 on success, or negative errno on error.
 */
int setup_purgatory(struct kimage *image, const void *slave_code,
		    const void *fdt, unsigned long kernel_load_addr,
		    unsigned long fdt_load_addr)
{
	unsigned int *slave_code_buf, master_entry;
	int ret;

	slave_code_buf = kmalloc(SLAVE_CODE_SIZE, GFP_KERNEL);
	if (!slave_code_buf)
		return -ENOMEM;

	/* Get the slave code from the new kernel and put it in purgatory. */
	ret = kexec_purgatory_get_set_symbol(image, "purgatory_start",
					     slave_code_buf, SLAVE_CODE_SIZE,
					     true);
	if (ret) {
		kfree(slave_code_buf);
		return ret;
	}

	master_entry = slave_code_buf[0];
	memcpy(slave_code_buf, slave_code, SLAVE_CODE_SIZE);
	slave_code_buf[0] = master_entry;
	ret = kexec_purgatory_get_set_symbol(image, "purgatory_start",
					     slave_code_buf, SLAVE_CODE_SIZE,
					     false);
	kfree(slave_code_buf);

	ret = kexec_purgatory_get_set_symbol(image, "kernel", &kernel_load_addr,
					     sizeof(kernel_load_addr), false);
	if (ret)
		return ret;
	ret = kexec_purgatory_get_set_symbol(image, "dt_offset", &fdt_load_addr,
					     sizeof(fdt_load_addr), false);
	if (ret)
		return ret;

	return 0;
}

/**
 * delete_fdt_mem_rsv - delete memory reservation with given address and size
 *
 * Return: 0 on success, or negative errno on error.
 */
int delete_fdt_mem_rsv(void *fdt, unsigned long start, unsigned long size)
{
	int i, ret, num_rsvs = fdt_num_mem_rsv(fdt);

	for (i = 0; i < num_rsvs; i++) {
		uint64_t rsv_start, rsv_size;

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

/*
 * setup_new_fdt - modify /chosen and memory reservation for the next kernel
 * @image:		kexec image being loaded.
 * @fdt:		Flattened device tree for the next kernel.
 * @initrd_load_addr:	Address where the next initrd will be loaded.
 * @initrd_len:		Size of the next initrd, or 0 if there will be none.
 * @cmdline:		Command line for the next kernel, or NULL if there will
 *			be none.
 *
 * Return: 0 on success, or negative errno on error.
 */
int setup_new_fdt(const struct kimage *image, void *fdt,
		  unsigned long initrd_load_addr, unsigned long initrd_len,
		  const char *cmdline)
{
	int ret, chosen_node;
	const void *prop;

	/* Remove memory reservation for the current device tree. */
	ret = delete_fdt_mem_rsv(fdt, __pa(initial_boot_params),
				 fdt_totalsize(initial_boot_params));
	if (ret == 0)
		pr_debug("Removed old device tree reservation.\n");
	else if (ret != -ENOENT)
		return ret;

	chosen_node = fdt_path_offset(fdt, "/chosen");
	if (chosen_node == -FDT_ERR_NOTFOUND) {
		chosen_node = fdt_add_subnode(fdt, fdt_path_offset(fdt, "/"),
					      "chosen");
		if (chosen_node < 0) {
			pr_err("Error creating /chosen.\n");
			return -EINVAL;
		}
	} else if (chosen_node < 0) {
		pr_err("Malformed device tree: error reading /chosen.\n");
		return -EINVAL;
	}

	/* Did we boot using an initrd? */
	prop = fdt_getprop(fdt, chosen_node, "linux,initrd-start", NULL);
	if (prop) {
		uint64_t tmp_start, tmp_end, tmp_size;

		tmp_start = fdt64_to_cpu(*((const fdt64_t *) prop));

		prop = fdt_getprop(fdt, chosen_node, "linux,initrd-end", NULL);
		if (!prop) {
			pr_err("Malformed device tree.\n");
			return -EINVAL;
		}
		tmp_end = fdt64_to_cpu(*((const fdt64_t *) prop));

		/*
		 * kexec reserves exact initrd size, while firmware may
		 * reserve a multiple of PAGE_SIZE, so check for both.
		 */
		tmp_size = tmp_end - tmp_start;
		ret = delete_fdt_mem_rsv(fdt, tmp_start, tmp_size);
		if (ret == -ENOENT)
			ret = delete_fdt_mem_rsv(fdt, tmp_start,
						 round_up(tmp_size, PAGE_SIZE));
		if (ret == 0)
			pr_debug("Removed old initrd reservation.\n");
		else if (ret != -ENOENT)
			return ret;

		/* If there's no new initrd, delete the old initrd's info. */
		if (initrd_len == 0) {
			ret = fdt_delprop(fdt, chosen_node,
					  "linux,initrd-start");
			if (ret) {
				pr_err("Error deleting linux,initrd-start.\n");
				return -EINVAL;
			}

			ret = fdt_delprop(fdt, chosen_node, "linux,initrd-end");
			if (ret) {
				pr_err("Error deleting linux,initrd-end.\n");
				return -EINVAL;
			}
		}
	}

	if (initrd_len) {
		ret = fdt_setprop_u64(fdt, chosen_node,
				      "linux,initrd-start",
				      initrd_load_addr);
		if (ret < 0)
			goto err;

		/* initrd-end is the first address after the initrd image. */
		ret = fdt_setprop_u64(fdt, chosen_node, "linux,initrd-end",
				      initrd_load_addr + initrd_len);
		if (ret < 0)
			goto err;

		ret = fdt_add_mem_rsv(fdt, initrd_load_addr, initrd_len);
		if (ret) {
			pr_err("Error reserving initrd memory: %s\n",
			       fdt_strerror(ret));
			return -EINVAL;
		}
	}

	if (cmdline != NULL) {
		ret = fdt_setprop_string(fdt, chosen_node, "bootargs", cmdline);
		if (ret < 0)
			goto err;
	} else {
		ret = fdt_delprop(fdt, chosen_node, "bootargs");
		if (ret && ret != -FDT_ERR_NOTFOUND) {
			pr_err("Error deleting bootargs.\n");
			return -EINVAL;
		}
	}

	if (image->type == KEXEC_TYPE_CRASH) {
		/*
		 * Avoid elfcorehdr from being stomped on in kdump kernel by
		 * setting up memory reserve map.
		 */
		ret = fdt_add_mem_rsv(fdt, image->arch.elfcorehdr_addr,
				      image->arch.elf_headers_sz);
		if (ret) {
			pr_err("Error reserving elfcorehdr memory: %s\n",
			       fdt_strerror(ret));
			goto err;
		}
	}

	ret = setup_ima_buffer(image, fdt, chosen_node);
	if (ret) {
		pr_err("Error setting up the new device tree.\n");
		return ret;
	}

	ret = fdt_setprop(fdt, chosen_node, "linux,booted-from-kexec", NULL, 0);
	if (ret)
		goto err;

	return 0;

err:
	pr_err("Error setting up the new device tree.\n");
	return -EINVAL;
}
