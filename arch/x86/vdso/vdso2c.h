/*
 * This file is included twice from vdso2c.c.  It generates code for 32-bit
 * and 64-bit vDSOs.  We need both for 64-bit builds, since 32-bit vDSOs
 * are built for 32-bit userspace.
 */

static void GOFUNC(void *addr, size_t len, FILE *outfile, const char *name)
{
	int found_load = 0;
	unsigned long load_size = -1;  /* Work around bogus warning */
	unsigned long data_size;
	Elf_Ehdr *hdr = (Elf_Ehdr *)addr;
	int i;
	unsigned long j;
	Elf_Shdr *symtab_hdr = NULL, *strtab_hdr, *secstrings_hdr,
		*alt_sec = NULL;
	Elf_Dyn *dyn = 0, *dyn_end = 0;
	const char *secstrings;
	uint64_t syms[NSYMS] = {};

	Elf_Phdr *pt = (Elf_Phdr *)(addr + GET_LE(&hdr->e_phoff));

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
			dyn = addr + GET_LE(&pt[i].p_offset);
			dyn_end = addr + GET_LE(&pt[i].p_offset) +
				GET_LE(&pt[i].p_memsz);
		}
	}
	if (!found_load)
		fail("no PT_LOAD seg\n");
	data_size = (load_size + 4095) / 4096 * 4096;

	/* Walk the dynamic table */
	for (i = 0; dyn + i < dyn_end &&
		     GET_LE(&dyn[i].d_tag) != DT_NULL; i++) {
		typeof(dyn[i].d_tag) tag = GET_LE(&dyn[i].d_tag);
		if (tag == DT_REL || tag == DT_RELSZ ||
		    tag == DT_RELENT || tag == DT_TEXTREL)
			fail("vdso image contains dynamic relocations\n");
	}

	/* Walk the section table */
	secstrings_hdr = addr + GET_LE(&hdr->e_shoff) +
		GET_LE(&hdr->e_shentsize)*GET_LE(&hdr->e_shstrndx);
	secstrings = addr + GET_LE(&secstrings_hdr->sh_offset);
	for (i = 0; i < GET_LE(&hdr->e_shnum); i++) {
		Elf_Shdr *sh = addr + GET_LE(&hdr->e_shoff) +
			GET_LE(&hdr->e_shentsize) * i;
		if (GET_LE(&sh->sh_type) == SHT_SYMTAB)
			symtab_hdr = sh;

		if (!strcmp(secstrings + GET_LE(&sh->sh_name),
			    ".altinstructions"))
			alt_sec = sh;
	}

	if (!symtab_hdr)
		fail("no symbol table\n");

	strtab_hdr = addr + GET_LE(&hdr->e_shoff) +
		GET_LE(&hdr->e_shentsize) * GET_LE(&symtab_hdr->sh_link);

	/* Walk the symbol table */
	for (i = 0;
	     i < GET_LE(&symtab_hdr->sh_size) / GET_LE(&symtab_hdr->sh_entsize);
	     i++) {
		int k;
		Elf_Sym *sym = addr + GET_LE(&symtab_hdr->sh_offset) +
			GET_LE(&symtab_hdr->sh_entsize) * i;
		const char *name = addr + GET_LE(&strtab_hdr->sh_offset) +
			GET_LE(&sym->st_name);
		for (k = 0; k < NSYMS; k++) {
			if (!strcmp(name, required_syms[k])) {
				if (syms[k]) {
					fail("duplicate symbol %s\n",
					     required_syms[k]);
				}
				syms[k] = GET_LE(&sym->st_value);
			}
		}
	}

	/* Validate mapping addresses. */
	for (i = 0; i < sizeof(special_pages) / sizeof(special_pages[0]); i++) {
		if (!syms[i])
			continue;  /* The mapping isn't used; ignore it. */

		if (syms[i] % 4096)
			fail("%s must be a multiple of 4096\n",
			     required_syms[i]);
		if (syms[i] < data_size)
			fail("%s must be after the text mapping\n",
			     required_syms[i]);
		if (syms[sym_end_mapping] < syms[i] + 4096)
			fail("%s overruns end_mapping\n", required_syms[i]);
	}
	if (syms[sym_end_mapping] % 4096)
		fail("end_mapping must be a multiple of 4096\n");

	/* Remove sections. */
	hdr->e_shoff = 0;
	hdr->e_shentsize = 0;
	hdr->e_shnum = 0;
	hdr->e_shstrndx = htole16(SHN_UNDEF);

	if (!name) {
		fwrite(addr, load_size, 1, outfile);
		return;
	}

	fprintf(outfile, "/* AUTOMATICALLY GENERATED -- DO NOT EDIT */\n\n");
	fprintf(outfile, "#include <linux/linkage.h>\n");
	fprintf(outfile, "#include <asm/page_types.h>\n");
	fprintf(outfile, "#include <asm/vdso.h>\n");
	fprintf(outfile, "\n");
	fprintf(outfile,
		"static unsigned char raw_data[%lu] __page_aligned_data = {",
		data_size);
	for (j = 0; j < load_size; j++) {
		if (j % 10 == 0)
			fprintf(outfile, "\n\t");
		fprintf(outfile, "0x%02X, ", (int)((unsigned char *)addr)[j]);
	}
	fprintf(outfile, "\n};\n\n");

	fprintf(outfile, "static struct page *pages[%lu];\n\n",
		data_size / 4096);

	fprintf(outfile, "const struct vdso_image %s = {\n", name);
	fprintf(outfile, "\t.data = raw_data,\n");
	fprintf(outfile, "\t.size = %lu,\n", data_size);
	fprintf(outfile, "\t.text_mapping = {\n");
	fprintf(outfile, "\t\t.name = \"[vdso]\",\n");
	fprintf(outfile, "\t\t.pages = pages,\n");
	fprintf(outfile, "\t},\n");
	if (alt_sec) {
		fprintf(outfile, "\t.alt = %lu,\n",
			(unsigned long)GET_LE(&alt_sec->sh_offset));
		fprintf(outfile, "\t.alt_len = %lu,\n",
			(unsigned long)GET_LE(&alt_sec->sh_size));
	}
	for (i = 0; i < NSYMS; i++) {
		if (syms[i])
			fprintf(outfile, "\t.sym_%s = 0x%" PRIx64 ",\n",
				required_syms[i], syms[i]);
	}
	fprintf(outfile, "};\n");
}
