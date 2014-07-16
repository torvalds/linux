/*
 * This file is included twice from vdso2c.c.  It generates code for 32-bit
 * and 64-bit vDSOs.  We need both for 64-bit builds, since 32-bit vDSOs
 * are built for 32-bit userspace.
 */

/*
 * We're writing a section table for a few reasons:
 *
 * The Go runtime had a couple of bugs: it would read the section
 * table to try to figure out how many dynamic symbols there were (it
 * shouldn't have looked at the section table at all) and, if there
 * were no SHT_SYNDYM section table entry, it would use an
 * uninitialized value for the number of symbols.  An empty DYNSYM
 * table would work, but I see no reason not to write a valid one (and
 * keep full performance for old Go programs).  This hack is only
 * needed on x86_64.
 *
 * The bug was introduced on 2012-08-31 by:
 * https://code.google.com/p/go/source/detail?r=56ea40aac72b
 * and was fixed on 2014-06-13 by:
 * https://code.google.com/p/go/source/detail?r=fc1cd5e12595
 *
 * Binutils has issues debugging the vDSO: it reads the section table to
 * find SHT_NOTE; it won't look at PT_NOTE for the in-memory vDSO, which
 * would break build-id if we removed the section table.  Binutils
 * also requires that shstrndx != 0.  See:
 * https://sourceware.org/bugzilla/show_bug.cgi?id=17064
 *
 * elfutils might not look for PT_NOTE if there is a section table at
 * all.  I don't know whether this matters for any practical purpose.
 *
 * For simplicity, rather than hacking up a partial section table, we
 * just write a mostly complete one.  We omit non-dynamic symbols,
 * though, since they're rather large.
 *
 * Once binutils gets fixed, we might be able to drop this for all but
 * the 64-bit vdso, since build-id only works in kernel RPMs, and
 * systems that update to new enough kernel RPMs will likely update
 * binutils in sync.  build-id has never worked for home-built kernel
 * RPMs without manual symlinking, and I suspect that no one ever does
 * that.
 */
struct BITSFUNC(fake_sections)
{
	ELF(Shdr) *table;
	unsigned long table_offset;
	int count, max_count;

	int in_shstrndx;
	unsigned long shstr_offset;
	const char *shstrtab;
	size_t shstrtab_len;

	int out_shstrndx;
};

static unsigned int BITSFUNC(find_shname)(struct BITSFUNC(fake_sections) *out,
					  const char *name)
{
	const char *outname = out->shstrtab;
	while (outname - out->shstrtab < out->shstrtab_len) {
		if (!strcmp(name, outname))
			return (outname - out->shstrtab) + out->shstr_offset;
		outname += strlen(outname) + 1;
	}

	if (*name)
		printf("Warning: could not find output name \"%s\"\n", name);
	return out->shstr_offset + out->shstrtab_len - 1;  /* Use a null. */
}

static void BITSFUNC(init_sections)(struct BITSFUNC(fake_sections) *out)
{
	if (!out->in_shstrndx)
		fail("didn't find the fake shstrndx\n");

	memset(out->table, 0, out->max_count * sizeof(ELF(Shdr)));

	if (out->max_count < 1)
		fail("we need at least two fake output sections\n");

	PUT_LE(&out->table[0].sh_type, SHT_NULL);
	PUT_LE(&out->table[0].sh_name, BITSFUNC(find_shname)(out, ""));

	out->count = 1;
}

static void BITSFUNC(copy_section)(struct BITSFUNC(fake_sections) *out,
				   int in_idx, const ELF(Shdr) *in,
				   const char *name)
{
	uint64_t flags = GET_LE(&in->sh_flags);

	bool copy = flags & SHF_ALLOC &&
		(GET_LE(&in->sh_size) ||
		 (GET_LE(&in->sh_type) != SHT_RELA &&
		  GET_LE(&in->sh_type) != SHT_REL)) &&
		strcmp(name, ".altinstructions") &&
		strcmp(name, ".altinstr_replacement");

	if (!copy)
		return;

	if (out->count >= out->max_count)
		fail("too many copied sections (max = %d)\n", out->max_count);

	if (in_idx == out->in_shstrndx)
		out->out_shstrndx = out->count;

	out->table[out->count] = *in;
	PUT_LE(&out->table[out->count].sh_name,
	       BITSFUNC(find_shname)(out, name));

	/* elfutils requires that a strtab have the correct type. */
	if (!strcmp(name, ".fake_shstrtab"))
		PUT_LE(&out->table[out->count].sh_type, SHT_STRTAB);

	out->count++;
}

static void BITSFUNC(go)(void *addr, size_t len,
			 FILE *outfile, const char *name)
{
	int found_load = 0;
	unsigned long load_size = -1;  /* Work around bogus warning */
	unsigned long data_size;
	ELF(Ehdr) *hdr = (ELF(Ehdr) *)addr;
	int i;
	unsigned long j;
	ELF(Shdr) *symtab_hdr = NULL, *strtab_hdr, *secstrings_hdr,
		*alt_sec = NULL;
	ELF(Dyn) *dyn = 0, *dyn_end = 0;
	const char *secstrings;
	uint64_t syms[NSYMS] = {};

	struct BITSFUNC(fake_sections) fake_sections = {};

	ELF(Phdr) *pt = (ELF(Phdr) *)(addr + GET_LE(&hdr->e_phoff));

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
		if (tag == DT_REL || tag == DT_RELSZ || tag == DT_RELA ||
		    tag == DT_RELENT || tag == DT_TEXTREL)
			fail("vdso image contains dynamic relocations\n");
	}

	/* Walk the section table */
	secstrings_hdr = addr + GET_LE(&hdr->e_shoff) +
		GET_LE(&hdr->e_shentsize)*GET_LE(&hdr->e_shstrndx);
	secstrings = addr + GET_LE(&secstrings_hdr->sh_offset);
	for (i = 0; i < GET_LE(&hdr->e_shnum); i++) {
		ELF(Shdr) *sh = addr + GET_LE(&hdr->e_shoff) +
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
		ELF(Sym) *sym = addr + GET_LE(&symtab_hdr->sh_offset) +
			GET_LE(&symtab_hdr->sh_entsize) * i;
		const char *name = addr + GET_LE(&strtab_hdr->sh_offset) +
			GET_LE(&sym->st_name);

		for (k = 0; k < NSYMS; k++) {
			if (!strcmp(name, required_syms[k].name)) {
				if (syms[k]) {
					fail("duplicate symbol %s\n",
					     required_syms[k].name);
				}
				syms[k] = GET_LE(&sym->st_value);
			}
		}

		if (!strcmp(name, "fake_shstrtab")) {
			ELF(Shdr) *sh;

			fake_sections.in_shstrndx = GET_LE(&sym->st_shndx);
			fake_sections.shstrtab = addr + GET_LE(&sym->st_value);
			fake_sections.shstrtab_len = GET_LE(&sym->st_size);
			sh = addr + GET_LE(&hdr->e_shoff) +
				GET_LE(&hdr->e_shentsize) *
				fake_sections.in_shstrndx;
			fake_sections.shstr_offset = GET_LE(&sym->st_value) -
				GET_LE(&sh->sh_addr);
		}
	}

	/* Build the output section table. */
	if (!syms[sym_VDSO_FAKE_SECTION_TABLE_START] ||
	    !syms[sym_VDSO_FAKE_SECTION_TABLE_END])
		fail("couldn't find fake section table\n");
	if ((syms[sym_VDSO_FAKE_SECTION_TABLE_END] -
	     syms[sym_VDSO_FAKE_SECTION_TABLE_START]) % sizeof(ELF(Shdr)))
		fail("fake section table size isn't a multiple of sizeof(Shdr)\n");
	fake_sections.table = addr + syms[sym_VDSO_FAKE_SECTION_TABLE_START];
	fake_sections.table_offset = syms[sym_VDSO_FAKE_SECTION_TABLE_START];
	fake_sections.max_count = (syms[sym_VDSO_FAKE_SECTION_TABLE_END] -
				   syms[sym_VDSO_FAKE_SECTION_TABLE_START]) /
		sizeof(ELF(Shdr));

	BITSFUNC(init_sections)(&fake_sections);
	for (i = 0; i < GET_LE(&hdr->e_shnum); i++) {
		ELF(Shdr) *sh = addr + GET_LE(&hdr->e_shoff) +
			GET_LE(&hdr->e_shentsize) * i;
		BITSFUNC(copy_section)(&fake_sections, i, sh,
				       secstrings + GET_LE(&sh->sh_name));
	}
	if (!fake_sections.out_shstrndx)
		fail("didn't generate shstrndx?!?\n");

	PUT_LE(&hdr->e_shoff, fake_sections.table_offset);
	PUT_LE(&hdr->e_shentsize, sizeof(ELF(Shdr)));
	PUT_LE(&hdr->e_shnum, fake_sections.count);
	PUT_LE(&hdr->e_shstrndx, fake_sections.out_shstrndx);

	/* Validate mapping addresses. */
	for (i = 0; i < sizeof(special_pages) / sizeof(special_pages[0]); i++) {
		if (!syms[i])
			continue;  /* The mapping isn't used; ignore it. */

		if (syms[i] % 4096)
			fail("%s must be a multiple of 4096\n",
			     required_syms[i].name);
		if (syms[i] < data_size)
			fail("%s must be after the text mapping\n",
			     required_syms[i].name);
		if (syms[sym_end_mapping] < syms[i] + 4096)
			fail("%s overruns end_mapping\n",
			     required_syms[i].name);
	}
	if (syms[sym_end_mapping] % 4096)
		fail("end_mapping must be a multiple of 4096\n");

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
		if (required_syms[i].export && syms[i])
			fprintf(outfile, "\t.sym_%s = 0x%" PRIx64 ",\n",
				required_syms[i].name, syms[i]);
	}
	fprintf(outfile, "};\n");
}
