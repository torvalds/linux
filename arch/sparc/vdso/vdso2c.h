/*
 * Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file is included up to twice from vdso2c.c.  It generates code for
 * 32-bit and 64-bit vDSOs.  We will eventually need both for 64-bit builds,
 * since 32-bit vDSOs will then be built for 32-bit userspace.
 */

static void BITSFUNC(go)(void *raw_addr, size_t raw_len,
			 void *stripped_addr, size_t stripped_len,
			 FILE *outfile, const char *name)
{
	int found_load = 0;
	unsigned long load_size = -1;  /* Work around bogus warning */
	unsigned long mapping_size;
	int i;
	unsigned long j;

	ELF(Shdr) *symtab_hdr = NULL, *strtab_hdr;
	ELF(Ehdr) *hdr = (ELF(Ehdr) *)raw_addr;
	ELF(Dyn) *dyn = 0, *dyn_end = 0;
	INT_BITS syms[NSYMS] = {};

	ELF(Phdr) *pt = (ELF(Phdr) *)(raw_addr + GET_BE(&hdr->e_phoff));

	/* Walk the segment table. */
	for (i = 0; i < GET_BE(&hdr->e_phnum); i++) {
		if (GET_BE(&pt[i].p_type) == PT_LOAD) {
			if (found_load)
				fail("multiple PT_LOAD segs\n");

			if (GET_BE(&pt[i].p_offset) != 0 ||
			    GET_BE(&pt[i].p_vaddr) != 0)
				fail("PT_LOAD in wrong place\n");

			if (GET_BE(&pt[i].p_memsz) != GET_BE(&pt[i].p_filesz))
				fail("cannot handle memsz != filesz\n");

			load_size = GET_BE(&pt[i].p_memsz);
			found_load = 1;
		} else if (GET_BE(&pt[i].p_type) == PT_DYNAMIC) {
			dyn = raw_addr + GET_BE(&pt[i].p_offset);
			dyn_end = raw_addr + GET_BE(&pt[i].p_offset) +
				GET_BE(&pt[i].p_memsz);
		}
	}
	if (!found_load)
		fail("no PT_LOAD seg\n");

	if (stripped_len < load_size)
		fail("stripped input is too short\n");

	/* Walk the dynamic table */
	for (i = 0; dyn + i < dyn_end &&
		     GET_BE(&dyn[i].d_tag) != DT_NULL; i++) {
		typeof(dyn[i].d_tag) tag = GET_BE(&dyn[i].d_tag);
		typeof(dyn[i].d_un.d_val) val = GET_BE(&dyn[i].d_un.d_val);

		if ((tag == DT_RELSZ || tag == DT_RELASZ) && (val != 0))
			fail("vdso image contains dynamic relocations\n");
	}

	/* Walk the section table */
	for (i = 0; i < GET_BE(&hdr->e_shnum); i++) {
		ELF(Shdr) *sh = raw_addr + GET_BE(&hdr->e_shoff) +
			GET_BE(&hdr->e_shentsize) * i;
		if (GET_BE(&sh->sh_type) == SHT_SYMTAB)
			symtab_hdr = sh;
	}

	if (!symtab_hdr)
		fail("no symbol table\n");

	strtab_hdr = raw_addr + GET_BE(&hdr->e_shoff) +
		GET_BE(&hdr->e_shentsize) * GET_BE(&symtab_hdr->sh_link);

	/* Walk the symbol table */
	for (i = 0;
	     i < GET_BE(&symtab_hdr->sh_size) / GET_BE(&symtab_hdr->sh_entsize);
	     i++) {
		int k;

		ELF(Sym) *sym = raw_addr + GET_BE(&symtab_hdr->sh_offset) +
			GET_BE(&symtab_hdr->sh_entsize) * i;
		const char *name = raw_addr + GET_BE(&strtab_hdr->sh_offset) +
			GET_BE(&sym->st_name);

		for (k = 0; k < NSYMS; k++) {
			if (!strcmp(name, required_syms[k].name)) {
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
				syms[k] = GET_BE(&sym->st_value);
			}
		}
	}

	/* Validate mapping addresses. */
	if (syms[sym_vvar_start] % 8192)
		fail("vvar_begin must be a multiple of 8192\n");

	if (!name) {
		fwrite(stripped_addr, stripped_len, 1, outfile);
		return;
	}

	mapping_size = (stripped_len + 8191) / 8192 * 8192;

	fprintf(outfile, "/* AUTOMATICALLY GENERATED -- DO NOT EDIT */\n\n");
	fprintf(outfile, "#include <linux/cache.h>\n");
	fprintf(outfile, "#include <asm/vdso.h>\n");
	fprintf(outfile, "\n");
	fprintf(outfile,
		"static unsigned char raw_data[%lu] __ro_after_init __aligned(8192)= {",
		mapping_size);
	for (j = 0; j < stripped_len; j++) {
		if (j % 10 == 0)
			fprintf(outfile, "\n\t");
		fprintf(outfile, "0x%02X, ",
			(int)((unsigned char *)stripped_addr)[j]);
	}
	fprintf(outfile, "\n};\n\n");

	fprintf(outfile, "const struct vdso_image %s_builtin = {\n", name);
	fprintf(outfile, "\t.data = raw_data,\n");
	fprintf(outfile, "\t.size = %lu,\n", mapping_size);
	for (i = 0; i < NSYMS; i++) {
		if (required_syms[i].export && syms[i])
			fprintf(outfile, "\t.sym_%s = %" PRIi64 ",\n",
				required_syms[i].name, (int64_t)syms[i]);
	}
	fprintf(outfile, "};\n");
}
