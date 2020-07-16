// SPDX-License-Identifier: GPL-2.0-only
/*
 * Coredump functionality for Remoteproc framework.
 *
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/devcoredump.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/remoteproc.h>
#include "remoteproc_internal.h"
#include "remoteproc_elf_helpers.h"

/**
 * rproc_coredump_cleanup() - clean up dump_segments list
 * @rproc: the remote processor handle
 */
void rproc_coredump_cleanup(struct rproc *rproc)
{
	struct rproc_dump_segment *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &rproc->dump_segments, node) {
		list_del(&entry->node);
		kfree(entry);
	}
}

/**
 * rproc_coredump_add_segment() - add segment of device memory to coredump
 * @rproc:	handle of a remote processor
 * @da:		device address
 * @size:	size of segment
 *
 * Add device memory to the list of segments to be included in a coredump for
 * the remoteproc.
 *
 * Return: 0 on success, negative errno on error.
 */
int rproc_coredump_add_segment(struct rproc *rproc, dma_addr_t da, size_t size)
{
	struct rproc_dump_segment *segment;

	segment = kzalloc(sizeof(*segment), GFP_KERNEL);
	if (!segment)
		return -ENOMEM;

	segment->da = da;
	segment->size = size;

	list_add_tail(&segment->node, &rproc->dump_segments);

	return 0;
}
EXPORT_SYMBOL(rproc_coredump_add_segment);

/**
 * rproc_coredump_add_custom_segment() - add custom coredump segment
 * @rproc:	handle of a remote processor
 * @da:		device address
 * @size:	size of segment
 * @dumpfn:	custom dump function called for each segment during coredump
 * @priv:	private data
 *
 * Add device memory to the list of segments to be included in the coredump
 * and associate the segment with the given custom dump function and private
 * data.
 *
 * Return: 0 on success, negative errno on error.
 */
int rproc_coredump_add_custom_segment(struct rproc *rproc,
				      dma_addr_t da, size_t size,
				      void (*dumpfn)(struct rproc *rproc,
						     struct rproc_dump_segment *segment,
						     void *dest),
				      void *priv)
{
	struct rproc_dump_segment *segment;

	segment = kzalloc(sizeof(*segment), GFP_KERNEL);
	if (!segment)
		return -ENOMEM;

	segment->da = da;
	segment->size = size;
	segment->priv = priv;
	segment->dump = dumpfn;

	list_add_tail(&segment->node, &rproc->dump_segments);

	return 0;
}
EXPORT_SYMBOL(rproc_coredump_add_custom_segment);

/**
 * rproc_coredump_set_elf_info() - set coredump elf information
 * @rproc:	handle of a remote processor
 * @class:	elf class for coredump elf file
 * @machine:	elf machine for coredump elf file
 *
 * Set elf information which will be used for coredump elf file.
 *
 * Return: 0 on success, negative errno on error.
 */
int rproc_coredump_set_elf_info(struct rproc *rproc, u8 class, u16 machine)
{
	if (class != ELFCLASS64 && class != ELFCLASS32)
		return -EINVAL;

	rproc->elf_class = class;
	rproc->elf_machine = machine;

	return 0;
}
EXPORT_SYMBOL(rproc_coredump_set_elf_info);

/**
 * rproc_coredump() - perform coredump
 * @rproc:	rproc handle
 *
 * This function will generate an ELF header for the registered segments
 * and create a devcoredump device associated with rproc.
 */
void rproc_coredump(struct rproc *rproc)
{
	struct rproc_dump_segment *segment;
	void *phdr;
	void *ehdr;
	size_t data_size;
	size_t offset;
	void *data;
	void *ptr;
	u8 class = rproc->elf_class;
	int phnum = 0;

	if (list_empty(&rproc->dump_segments))
		return;

	if (class == ELFCLASSNONE) {
		dev_err(&rproc->dev, "Elf class is not set\n");
		return;
	}

	data_size = elf_size_of_hdr(class);
	list_for_each_entry(segment, &rproc->dump_segments, node) {
		data_size += elf_size_of_phdr(class) + segment->size;

		phnum++;
	}

	data = vmalloc(data_size);
	if (!data)
		return;

	ehdr = data;

	memset(ehdr, 0, elf_size_of_hdr(class));
	/* e_ident field is common for both elf32 and elf64 */
	elf_hdr_init_ident(ehdr, class);

	elf_hdr_set_e_type(class, ehdr, ET_CORE);
	elf_hdr_set_e_machine(class, ehdr, rproc->elf_machine);
	elf_hdr_set_e_version(class, ehdr, EV_CURRENT);
	elf_hdr_set_e_entry(class, ehdr, rproc->bootaddr);
	elf_hdr_set_e_phoff(class, ehdr, elf_size_of_hdr(class));
	elf_hdr_set_e_ehsize(class, ehdr, elf_size_of_hdr(class));
	elf_hdr_set_e_phentsize(class, ehdr, elf_size_of_phdr(class));
	elf_hdr_set_e_phnum(class, ehdr, phnum);

	phdr = data + elf_hdr_get_e_phoff(class, ehdr);
	offset = elf_hdr_get_e_phoff(class, ehdr);
	offset += elf_size_of_phdr(class) * elf_hdr_get_e_phnum(class, ehdr);

	list_for_each_entry(segment, &rproc->dump_segments, node) {
		memset(phdr, 0, elf_size_of_phdr(class));
		elf_phdr_set_p_type(class, phdr, PT_LOAD);
		elf_phdr_set_p_offset(class, phdr, offset);
		elf_phdr_set_p_vaddr(class, phdr, segment->da);
		elf_phdr_set_p_paddr(class, phdr, segment->da);
		elf_phdr_set_p_filesz(class, phdr, segment->size);
		elf_phdr_set_p_memsz(class, phdr, segment->size);
		elf_phdr_set_p_flags(class, phdr, PF_R | PF_W | PF_X);
		elf_phdr_set_p_align(class, phdr, 0);

		if (segment->dump) {
			segment->dump(rproc, segment, data + offset);
		} else {
			ptr = rproc_da_to_va(rproc, segment->da, segment->size);
			if (!ptr) {
				dev_err(&rproc->dev,
					"invalid coredump segment (%pad, %zu)\n",
					&segment->da, segment->size);
				memset(data + offset, 0xff, segment->size);
			} else {
				memcpy(data + offset, ptr, segment->size);
			}
		}

		offset += elf_phdr_get_p_filesz(class, phdr);
		phdr += elf_size_of_phdr(class);
	}

	dev_coredumpv(&rproc->dev, data, data_size, GFP_KERNEL);
}
