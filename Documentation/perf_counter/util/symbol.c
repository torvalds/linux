#include "util.h"
#include "../perf.h"
#include "symbol.h"

#include <libelf.h>
#include <gelf.h>
#include <elf.h>

static struct symbol *symbol__new(uint64_t start, uint64_t len,
				  const char *name, unsigned int priv_size)
{
	size_t namelen = strlen(name) + 1;
	struct symbol *self = malloc(priv_size + sizeof(*self) + namelen);

	if (self != NULL) {
		if (priv_size) {
			memset(self, 0, priv_size);
			self = ((void *)self) + priv_size;
		}
		self->start = start;
		self->end   = start + len;
		memcpy(self->name, name, namelen);
	}

	return self;
}

static void symbol__delete(struct symbol *self, unsigned int priv_size)
{
	free(((void *)self) - priv_size);
}

static size_t symbol__fprintf(struct symbol *self, FILE *fp)
{
	return fprintf(fp, " %llx-%llx %s\n",
		       self->start, self->end, self->name);
}

struct dso *dso__new(const char *name, unsigned int sym_priv_size)
{
	struct dso *self = malloc(sizeof(*self) + strlen(name) + 1);

	if (self != NULL) {
		strcpy(self->name, name);
		self->syms = RB_ROOT;
		self->sym_priv_size = sym_priv_size;
	}

	return self;
}

static void dso__delete_symbols(struct dso *self)
{
	struct symbol *pos;
	struct rb_node *next = rb_first(&self->syms);

	while (next) {
		pos = rb_entry(next, struct symbol, rb_node);
		next = rb_next(&pos->rb_node);
		symbol__delete(pos, self->sym_priv_size);
	}
}

void dso__delete(struct dso *self)
{
	dso__delete_symbols(self);
	free(self);
}

static void dso__insert_symbol(struct dso *self, struct symbol *sym)
{
	struct rb_node **p = &self->syms.rb_node;
	struct rb_node *parent = NULL;
	const uint64_t ip = sym->start;
	struct symbol *s;

	while (*p != NULL) {
		parent = *p;
		s = rb_entry(parent, struct symbol, rb_node);
		if (ip < s->start)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&sym->rb_node, parent, p);
	rb_insert_color(&sym->rb_node, &self->syms);
}

struct symbol *dso__find_symbol(struct dso *self, uint64_t ip)
{
	struct rb_node *n;

	if (self == NULL)
		return NULL;

	n = self->syms.rb_node;

	while (n) {
		struct symbol *s = rb_entry(n, struct symbol, rb_node);

		if (ip < s->start)
			n = n->rb_left;
		else if (ip > s->end)
			n = n->rb_right;
		else
			return s;
	}

	return NULL;
}

size_t dso__fprintf(struct dso *self, FILE *fp)
{
	size_t ret = fprintf(fp, "dso: %s\n", self->name);

	struct rb_node *nd;
	for (nd = rb_first(&self->syms); nd; nd = rb_next(nd)) {
		struct symbol *pos = rb_entry(nd, struct symbol, rb_node);
		ret += symbol__fprintf(pos, fp);
	}

	return ret;
}

static int hex(char ch)
{
	if ((ch >= '0') && (ch <= '9'))
		return ch - '0';
	if ((ch >= 'a') && (ch <= 'f'))
		return ch - 'a' + 10;
	if ((ch >= 'A') && (ch <= 'F'))
		return ch - 'A' + 10;
	return -1;
}

/*
 * While we find nice hex chars, build a long_val.
 * Return number of chars processed.
 */
static int hex2long(char *ptr, unsigned long *long_val)
{
	const char *p = ptr;
	*long_val = 0;

	while (*p) {
		const int hex_val = hex(*p);

		if (hex_val < 0)
			break;

		*long_val = (*long_val << 4) | hex_val;
		p++;
	}

	return p - ptr;
}

static int dso__load_kallsyms(struct dso *self)
{
	struct rb_node *nd, *prevnd;
	char *line = NULL;
	size_t n;
	FILE *file = fopen("/proc/kallsyms", "r");

	if (file == NULL)
		goto out_failure;

	while (!feof(file)) {
		unsigned long start;
		struct symbol *sym;
		int line_len, len;
		char symbol_type;

		line_len = getline(&line, &n, file);
		if (line_len < 0)
			break;

		if (!line)
			goto out_failure;

		line[--line_len] = '\0'; /* \n */

		len = hex2long(line, &start);

		len++;
		if (len + 2 >= line_len)
			continue;

		symbol_type = toupper(line[len]);
		/*
		 * We're interested only in code ('T'ext)
		 */
		if (symbol_type != 'T' && symbol_type != 'W')
			continue;
		/*
		 * Well fix up the end later, when we have all sorted.
		 */
		sym = symbol__new(start, 0xdead, line + len + 2,
				  self->sym_priv_size);

		if (sym == NULL)
			goto out_delete_line;

		dso__insert_symbol(self, sym);
	}

	/*
	 * Now that we have all sorted out, just set the ->end of all
	 * symbols
	 */
	prevnd = rb_first(&self->syms);

	if (prevnd == NULL)
		goto out_delete_line;

	for (nd = rb_next(prevnd); nd; nd = rb_next(nd)) {
		struct symbol *prev = rb_entry(prevnd, struct symbol, rb_node),
			      *curr = rb_entry(nd, struct symbol, rb_node);

		prev->end = curr->start - 1;
		prevnd = nd;
	}

	free(line);
	fclose(file);

	return 0;

out_delete_line:
	free(line);
out_failure:
	return -1;
}

/**
 * elf_symtab__for_each_symbol - iterate thru all the symbols
 *
 * @self: struct elf_symtab instance to iterate
 * @index: uint32_t index
 * @sym: GElf_Sym iterator
 */
#define elf_symtab__for_each_symbol(syms, nr_syms, index, sym) \
	for (index = 0, gelf_getsym(syms, index, &sym);\
	     index < nr_syms; \
	     index++, gelf_getsym(syms, index, &sym))

static inline uint8_t elf_sym__type(const GElf_Sym *sym)
{
	return GELF_ST_TYPE(sym->st_info);
}

static inline int elf_sym__is_function(const GElf_Sym *sym)
{
	return elf_sym__type(sym) == STT_FUNC &&
	       sym->st_name != 0 &&
	       sym->st_shndx != SHN_UNDEF &&
	       sym->st_size != 0;
}

static inline const char *elf_sym__name(const GElf_Sym *sym,
					const Elf_Data *symstrs)
{
	return symstrs->d_buf + sym->st_name;
}

static Elf_Scn *elf_section_by_name(Elf *elf, GElf_Ehdr *ep,
				    GElf_Shdr *shp, const char *name,
				    size_t *index)
{
	Elf_Scn *sec = NULL;
	size_t cnt = 1;

	while ((sec = elf_nextscn(elf, sec)) != NULL) {
		char *str;

		gelf_getshdr(sec, shp);
		str = elf_strptr(elf, ep->e_shstrndx, shp->sh_name);
		if (!strcmp(name, str)) {
			if (index)
				*index = cnt;
			break;
		}
		++cnt;
	}

	return sec;
}

static int dso__load_sym(struct dso *self, int fd, const char *name)
{
	Elf_Data *symstrs;
	uint32_t nr_syms;
	int err = -1;
	uint32_t index;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	Elf_Data *syms;
	GElf_Sym sym;
	Elf_Scn *sec;
	Elf *elf;
	int nr = 0;

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (elf == NULL) {
		fprintf(stderr, "%s: cannot read %s ELF file.\n",
			__func__, name);
		goto out_close;
	}

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		fprintf(stderr, "%s: cannot get elf header.\n", __func__);
		goto out_elf_end;
	}

	sec = elf_section_by_name(elf, &ehdr, &shdr, ".symtab", NULL);
	if (sec == NULL)
		sec = elf_section_by_name(elf, &ehdr, &shdr, ".dynsym", NULL);

	if (sec == NULL)
		goto out_elf_end;

	syms = elf_getdata(sec, NULL);
	if (syms == NULL)
		goto out_elf_end;

	sec = elf_getscn(elf, shdr.sh_link);
	if (sec == NULL)
		goto out_elf_end;

	symstrs = elf_getdata(sec, NULL);
	if (symstrs == NULL)
		goto out_elf_end;

	nr_syms = shdr.sh_size / shdr.sh_entsize;

	elf_symtab__for_each_symbol(syms, nr_syms, index, sym) {
		struct symbol *f;

		if (!elf_sym__is_function(&sym))
			continue;

		sec = elf_getscn(elf, sym.st_shndx);
		if (!sec)
			goto out_elf_end;

		gelf_getshdr(sec, &shdr);
		sym.st_value -= shdr.sh_addr - shdr.sh_offset;

		f = symbol__new(sym.st_value, sym.st_size,
				elf_sym__name(&sym, symstrs),
				self->sym_priv_size);
		if (!f)
			goto out_elf_end;

		dso__insert_symbol(self, f);

		nr++;
	}

	err = nr;
out_elf_end:
	elf_end(elf);
out_close:
	return err;
}

int dso__load(struct dso *self)
{
	int size = strlen(self->name) + sizeof("/usr/lib/debug%s.debug");
	char *name = malloc(size);
	int variant = 0;
	int ret = -1;
	int fd;

	if (!name)
		return -1;

more:
	do {
		switch (variant) {
		case 0: /* Fedora */
			snprintf(name, size, "/usr/lib/debug%s.debug", self->name);
			break;
		case 1: /* Ubuntu */
			snprintf(name, size, "/usr/lib/debug%s", self->name);
			break;
		case 2: /* Sane people */
			snprintf(name, size, "%s", self->name);
			break;

		default:
			goto out;
		}
		variant++;

		fd = open(name, O_RDONLY);
	} while (fd < 0);

	ret = dso__load_sym(self, fd, name);
	close(fd);

	/*
	 * Some people seem to have debuginfo files _WITHOUT_ debug info!?!?
	 */
	if (!ret)
		goto more;

out:
	free(name);
	return ret;
}

static int dso__load_vmlinux(struct dso *self, const char *vmlinux)
{
	int err, fd = open(vmlinux, O_RDONLY);

	if (fd < 0)
		return -1;

	err = dso__load_sym(self, fd, vmlinux);
	close(fd);

	return err;
}

int dso__load_kernel(struct dso *self, const char *vmlinux)
{
	int err = -1;

	if (vmlinux)
		err = dso__load_vmlinux(self, vmlinux);

	if (err)
		err = dso__load_kallsyms(self);

	return err;
}

void symbol__init(void)
{
	elf_version(EV_CURRENT);
}
