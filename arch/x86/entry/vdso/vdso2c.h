/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is included twice from vdso2c.c.  It generates code for 32-bit
 * and 64-bit vDSOs.  We need both for 64-bit builds, since 32-bit vDSOs
 * are built for 32-bit userspace.
 */

static void BITSFUNC(go)(void *raw_addr, size_t raw_len,
			 void *stripped_addr, size_t stripped_len,
			 FILE *outfile, const char *image_name)
{
	int found_load = 0;
	unsigned long load_size = -1;  /* Work around bogus warning */
	unsigned long mapping_size;
	ELF(Ehdr) *hdr = (ELF(Ehdr) *)raw_addr;
	int i;
	unsigned long j;
	ELF(Shdr) *symtab_hdr = NULL, *strtab_hdr, *secstrings_hdr,
		*alt_sec = NULL;
	ELF(Dyn) *dyn = 0, *dyn_end = 0;
	const char *secstrings;
	INT_BITS syms[NSYMS] = {};

	ELF(Phdr) *pt = (ELF(Phdr) *)(raw_addr + GET_LE(&hdr->e_phoff));

	if (GET_LE(&hdr->e_type) != ET_DYN)
		fail("input is not a shared object\n");

	/* Walk the segment table. */
	for (i = 0; i < GET_LE(&hdr->e_phnum); i++) {
		if (GET_LE(&pt[i].p_type) == PT_LOAD) {
			if (found_load)
				fail("multiple PT_LOAD segs\n");

			if (GET_LE(&pt[i].p_offset) != 0 ||
			    GET_LE(&pt[i].p_vaddr) != 0)
				fail("PT_LOAD in wrong place\n");

			if (GET_LE(&pt[i].p_memsz) != GET_LE(&pt[i].p_filesz))
				fail("cannot handle memsz != filesz\n");

			load_size = GET_LE(&pt[i].p_memsz);
			found_load = 1;
		} else if (GET_LE(&pt[i].p_type) == PT_DYNAMIC) {
			dyn = raw_addr + GET_LE(&pt[i].p_offset);
			dyn_end = raw_addr + GET_LE(&pt[i].p_offset) +
				GET_LE(&pt[i].p_memsz);
		}
	}
	if (!found_load)
		fail("no PT_LOAD seg\n");

	if (stripped_len < load_size)
		fail("stripped input is too short\n");

	if (!dyn)
		fail("input has no PT_DYNAMIC section -- your toolchain is buggy\n");

	/* Walk the dynamic table */
	for (i = 0; dyn + i < dyn_end &&
		     GET_LE(&dyn[i].d_tag) != DT_NULL; i++) {
		typeof(dyn[i].d_tag) tag = GET_LE(&dyn[i].d_tag);
		if (tag == DT_REL || tag == DT_RELSZ || tag == DT_RELA ||
		    tag == DT_RELENT || tag == DT_TEXTREL)
			fail("vdso image contains dynamic relocations\n");
	}

	/* Walk the section table */
	secstrings_hdr = raw_addr + GET_LE(&hdr->e_shoff) +
		GET_LE(&hdr->e_shentsize)*GET_LE(&hdr->e_shstrndx);
	secstrings = raw_addr + GET_LE(&secstrings_hdr->sh_offset);
	for (i = 0; i < GET_LE(&hdr->e_shnum); i++) {
		ELF(Shdr) *sh = raw_addr + GET_LE(&hdr->e_shoff) +
			GET_LE(&hdr->e_shentsize) * i;
		if (GET_LE(&sh->sh_type) == SHT_SYMTAB)
			symtab_hdr = sh;

		if (!strcmp(secstrings + GET_LE(&sh->sh_name),
			    ".altinstructions"))
			alt_sec = sh;
	}

	if (!symtab_hdr)
		fail("no symbol table\n");

	strtab_hdr = raw_addr + GET_LE(&hdr->e_shoff) +
		GET_LE(&hdr->e_shentsize) * GET_LE(&symtab_hdr->sh_link);

	/* Walk the symbol table */
	for (i = 0;
	     i < GET_LE(&symtab_hdr->sh_size) / GET_LE(&symtab_hdr->sh_entsize);
	     i++) {
		int k;
		ELF(Sym) *sym = raw_addr + GET_LE(&symtab_hdr->sh_offset) +
			GET_LE(&symtab_hdr->sh_entsize) * i;
		const char *sym_name = raw_addr +
				       GET_LE(&strtab_hdr->sh_offset) +
				       GET_LE(&sym->st_name);

		for (k = 0; k < NSYMS; k++) {
			if (!strcmp(sym_name, required_syms[k].name)) {
				if (syms[k]) {
					fail("duplicate symbol %s\n",
					     required_syms[k].name);
				}

				/*
				 * Careful: we use negative addresses, but
				 * st_value is unsigned, so we rely
				 * on syms[k] being a signed type of the
				 * correct width.
				 */
				syms[k] = GET_LE(&sym->st_value);
			}
		}
	}

	/* Validate mapping addresses. */
	for (i = 0; i < sizeof(special_pages) / sizeof(special_pages[0]); i++) {
		INT_BITS symval = syms[special_pages[i]];

		if (!symval)
			continue;  /* The mapping isn't used; ignore it. */

		if (symval % 4096)
			fail("%s must be a multiple of 4096\n",
			     required_syms[i].name);
		if (symval + 4096 < syms[sym_vvar_start])
			fail("%s underruns vvar_start\n",
			     required_syms[i].name);
		if (symval + 4096 > 0)
			fail("%s is on the wrong side of the vdso text\n",
			     required_syms[i].name);
	}
	if (syms[sym_vvar_start] % 4096)
		fail("vvar_begin must be a multiple of 4096\n");

	if (!image_name) {
		fwrite(stripped_addr, stripped_len, 1, outfile);
		return;
	}

	mapping_size = (stripped_len + 4095) / 4096 * 4096;

	fprintf(outfile, "/* AUTOMATICALLY GENERATED -- DO NOT EDIT */\n\n");
	fprintf(outfile, "#include <linux/linkage.h>\n");
	fprintf(outfile, "#include <asm/page_types.h>\n");
	fprintf(outfile, "#include <asm/vdso.h>\n");
	fprintf(outfile, "\n");
	fprintf(outfile,
		"static unsigned char raw_data[%lu] __ro_after_init __aligned(PAGE_SIZE) = {",
		mapping_size);
	for (j = 0; j < stripped_len; j++) {
		if (j % 10 == 0)
			fprintf(outfile, "\n\t");
		fprintf(outfile, "0x%02X, ",
			(int)((unsigned char *)stripped_addr)[j]);
	}
	fprintf(outfile, "\n};\n\n");

	fprintf(outfile, "const struct vdso_image %s = {\n", image_name);
	fprintf(outfile, "\t.data = raw_data,\n");
	fprintf(outfile, "\t.size = %lu,\n", mapping_size);
	if (alt_sec) {
		fprintf(outfile, "\t.alt = %lu,\n",
			(unsigned long)GET_LE(&alt_sec->sh_offset));
		fprintf(outfile, "\t.alt_len = %lu,\n",
			(unsigned long)GET_LE(&alt_sec->sh_size));
	}
	for (i = 0; i < NSYMS; i++) {
		if (required_syms[i].export && syms[i])
			fprintf(outfile, "\t.sym_%s = %" PRIi64 ",\n",
				required_syms[i].name, (int64_t)syms[i]);
	}
	fprintf(outfile, "};\n");
}
