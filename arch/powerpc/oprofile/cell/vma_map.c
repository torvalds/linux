/*
 * Cell Broadband Engine OProfile Support
 *
 * (C) Copyright IBM Corporation 2006
 *
 * Author: Maynard Johnson <maynardj@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* The code in this source file is responsible for generating
 * vma-to-fileOffset maps for both overlay and non-overlay SPU
 * applications.
 */

#include <linux/mm.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/elf.h>
#include "pr_util.h"


void vma_map_free(struct vma_to_fileoffset_map *map)
{
	while (map) {
		struct vma_to_fileoffset_map *next = map->next;
		kfree(map);
		map = next;
	}
}

unsigned int
vma_map_lookup(struct vma_to_fileoffset_map *map, unsigned int vma,
	       const struct spu *aSpu, int *grd_val)
{
	/*
	 * Default the offset to the physical address + a flag value.
	 * Addresses of dynamically generated code can't be found in the vma
	 * map.  For those addresses the flagged value will be sent on to
	 * the user space tools so they can be reported rather than just
	 * thrown away.
	 */
	u32 offset = 0x10000000 + vma;
	u32 ovly_grd;

	for (; map; map = map->next) {
		if (vma < map->vma || vma >= map->vma + map->size)
			continue;

		if (map->guard_ptr) {
			ovly_grd = *(u32 *)(aSpu->local_store + map->guard_ptr);
			if (ovly_grd != map->guard_val)
				continue;
			*grd_val = ovly_grd;
		}
		offset = vma - map->vma + map->offset;
		break;
	}

	return offset;
}

static struct vma_to_fileoffset_map *
vma_map_add(struct vma_to_fileoffset_map *map, unsigned int vma,
	    unsigned int size, unsigned int offset, unsigned int guard_ptr,
	    unsigned int guard_val)
{
	struct vma_to_fileoffset_map *new =
		kzalloc(sizeof(struct vma_to_fileoffset_map), GFP_KERNEL);
	if (!new) {
		printk(KERN_ERR "SPU_PROF: %s, line %d: malloc failed\n",
		       __func__, __LINE__);
		vma_map_free(map);
		return NULL;
	}

	new->next = map;
	new->vma = vma;
	new->size = size;
	new->offset = offset;
	new->guard_ptr = guard_ptr;
	new->guard_val = guard_val;

	return new;
}


/* Parse SPE ELF header and generate a list of vma_maps.
 * A pointer to the first vma_map in the generated list
 * of vma_maps is returned.  */
struct vma_to_fileoffset_map *create_vma_map(const struct spu *aSpu,
					     unsigned long __spu_elf_start)
{
	static const unsigned char expected[EI_PAD] = {
		[EI_MAG0] = ELFMAG0,
		[EI_MAG1] = ELFMAG1,
		[EI_MAG2] = ELFMAG2,
		[EI_MAG3] = ELFMAG3,
		[EI_CLASS] = ELFCLASS32,
		[EI_DATA] = ELFDATA2MSB,
		[EI_VERSION] = EV_CURRENT,
		[EI_OSABI] = ELFOSABI_NONE
	};

	int grd_val;
	struct vma_to_fileoffset_map *map = NULL;
	void __user *spu_elf_start = (void __user *)__spu_elf_start;
	struct spu_overlay_info ovly;
	unsigned int overlay_tbl_offset = -1;
	Elf32_Phdr __user *phdr_start;
	Elf32_Shdr __user *shdr_start;
	Elf32_Ehdr ehdr;
	Elf32_Phdr phdr;
	Elf32_Shdr shdr, shdr_str;
	Elf32_Sym sym;
	int i, j;
	char name[32];

	unsigned int ovly_table_sym = 0;
	unsigned int ovly_buf_table_sym = 0;
	unsigned int ovly_table_end_sym = 0;
	unsigned int ovly_buf_table_end_sym = 0;
	struct spu_overlay_info __user *ovly_table;
	unsigned int n_ovlys;

	/* Get and validate ELF header.	 */

	if (copy_from_user(&ehdr, spu_elf_start, sizeof (ehdr)))
		goto fail;

	if (memcmp(ehdr.e_ident, expected, EI_PAD) != 0) {
		printk(KERN_ERR "SPU_PROF: "
		       "%s, line %d: Unexpected e_ident parsing SPU ELF\n",
		       __func__, __LINE__);
		goto fail;
	}
	if (ehdr.e_machine != EM_SPU) {
		printk(KERN_ERR "SPU_PROF: "
		       "%s, line %d: Unexpected e_machine parsing SPU ELF\n",
		       __func__,  __LINE__);
		goto fail;
	}
	if (ehdr.e_type != ET_EXEC) {
		printk(KERN_ERR "SPU_PROF: "
		       "%s, line %d: Unexpected e_type parsing SPU ELF\n",
		       __func__, __LINE__);
		goto fail;
	}
	phdr_start = spu_elf_start + ehdr.e_phoff;
	shdr_start = spu_elf_start + ehdr.e_shoff;

	/* Traverse program headers.  */
	for (i = 0; i < ehdr.e_phnum; i++) {
		if (copy_from_user(&phdr, phdr_start + i, sizeof(phdr)))
			goto fail;

		if (phdr.p_type != PT_LOAD)
			continue;
		if (phdr.p_flags & (1 << 27))
			continue;

		map = vma_map_add(map, phdr.p_vaddr, phdr.p_memsz,
				  phdr.p_offset, 0, 0);
		if (!map)
			goto fail;
	}

	pr_debug("SPU_PROF: Created non-overlay maps\n");
	/* Traverse section table and search for overlay-related symbols.  */
	for (i = 0; i < ehdr.e_shnum; i++) {
		if (copy_from_user(&shdr, shdr_start + i, sizeof(shdr)))
			goto fail;

		if (shdr.sh_type != SHT_SYMTAB)
			continue;
		if (shdr.sh_entsize != sizeof (sym))
			continue;

		if (copy_from_user(&shdr_str,
				   shdr_start + shdr.sh_link,
				   sizeof(shdr)))
			goto fail;

		if (shdr_str.sh_type != SHT_STRTAB)
			goto fail;;

		for (j = 0; j < shdr.sh_size / sizeof (sym); j++) {
			if (copy_from_user(&sym, spu_elf_start +
						 shdr.sh_offset +
						 j * sizeof (sym),
					   sizeof (sym)))
				goto fail;

			if (copy_from_user(name, 
					   spu_elf_start + shdr_str.sh_offset +
					   sym.st_name,
					   20))
				goto fail;

			if (memcmp(name, "_ovly_table", 12) == 0)
				ovly_table_sym = sym.st_value;
			if (memcmp(name, "_ovly_buf_table", 16) == 0)
				ovly_buf_table_sym = sym.st_value;
			if (memcmp(name, "_ovly_table_end", 16) == 0)
				ovly_table_end_sym = sym.st_value;
			if (memcmp(name, "_ovly_buf_table_end", 20) == 0)
				ovly_buf_table_end_sym = sym.st_value;
		}
	}

	/* If we don't have overlays, we're done.  */
	if (ovly_table_sym == 0 || ovly_buf_table_sym == 0
	    || ovly_table_end_sym == 0 || ovly_buf_table_end_sym == 0) {
		pr_debug("SPU_PROF: No overlay table found\n");
		goto out;
	} else {
		pr_debug("SPU_PROF: Overlay table found\n");
	}

	/* The _ovly_table symbol represents a table with one entry
	 * per overlay section.	 The _ovly_buf_table symbol represents
	 * a table with one entry per overlay region.
	 * The struct spu_overlay_info gives the structure of the _ovly_table
	 * entries.  The structure of _ovly_table_buf is simply one
	 * u32 word per entry.
	 */
	overlay_tbl_offset = vma_map_lookup(map, ovly_table_sym,
					    aSpu, &grd_val);
	if (overlay_tbl_offset < 0) {
		printk(KERN_ERR "SPU_PROF: "
		       "%s, line %d: Error finding SPU overlay table\n",
		       __func__, __LINE__);
		goto fail;
	}
	ovly_table = spu_elf_start + overlay_tbl_offset;

	n_ovlys = (ovly_table_end_sym -
		   ovly_table_sym) / sizeof (ovly);

	/* Traverse overlay table.  */
	for (i = 0; i < n_ovlys; i++) {
		if (copy_from_user(&ovly, ovly_table + i, sizeof (ovly)))
			goto fail;

		/* The ovly.vma/size/offset arguments are analogous to the same
		 * arguments used above for non-overlay maps.  The final two
		 * args are referred to as the guard pointer and the guard
		 * value.
		 * The guard pointer is an entry in the _ovly_buf_table,
		 * computed using ovly.buf as the index into the table.	 Since
		 * ovly.buf values begin at '1' to reference the first (or 0th)
		 * entry in the _ovly_buf_table, the computation subtracts 1
		 * from ovly.buf.
		 * The guard value is stored in the _ovly_buf_table entry and
		 * is an index (starting at 1) back to the _ovly_table entry
		 * that is pointing at this _ovly_buf_table entry.  So, for
		 * example, for an overlay scenario with one overlay segment
		 * and two overlay sections:
		 *	- Section 1 points to the first entry of the
		 *	  _ovly_buf_table, which contains a guard value
		 *	  of '1', referencing the first (index=0) entry of
		 *	  _ovly_table.
		 *	- Section 2 points to the second entry of the
		 *	  _ovly_buf_table, which contains a guard value
		 *	  of '2', referencing the second (index=1) entry of
		 *	  _ovly_table.
		 */
		map = vma_map_add(map, ovly.vma, ovly.size, ovly.offset,
				  ovly_buf_table_sym + (ovly.buf-1) * 4, i+1);
		if (!map)
			goto fail;
	}
	goto out;

 fail:
	map = NULL;
 out:
	return map;
}
