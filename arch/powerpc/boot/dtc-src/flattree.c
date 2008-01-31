/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *                                                                   USA
 */

#include "dtc.h"

#define FTF_FULLPATH	0x1
#define FTF_VARALIGN	0x2
#define FTF_NAMEPROPS	0x4
#define FTF_BOOTCPUID	0x8
#define FTF_STRTABSIZE	0x10
#define FTF_STRUCTSIZE	0x20
#define FTF_NOPS	0x40

static struct version_info {
	int version;
	int last_comp_version;
	int hdr_size;
	int flags;
} version_table[] = {
	{1, 1, FDT_V1_SIZE,
	 FTF_FULLPATH|FTF_VARALIGN|FTF_NAMEPROPS},
	{2, 1, FDT_V2_SIZE,
	 FTF_FULLPATH|FTF_VARALIGN|FTF_NAMEPROPS|FTF_BOOTCPUID},
	{3, 1, FDT_V3_SIZE,
	 FTF_FULLPATH|FTF_VARALIGN|FTF_NAMEPROPS|FTF_BOOTCPUID|FTF_STRTABSIZE},
	{16, 16, FDT_V3_SIZE,
	 FTF_BOOTCPUID|FTF_STRTABSIZE|FTF_NOPS},
	{17, 16, FDT_V17_SIZE,
	 FTF_BOOTCPUID|FTF_STRTABSIZE|FTF_STRUCTSIZE|FTF_NOPS},
};

struct emitter {
	void (*cell)(void *, cell_t);
	void (*string)(void *, char *, int);
	void (*align)(void *, int);
	void (*data)(void *, struct data);
	void (*beginnode)(void *, const char *);
	void (*endnode)(void *, const char *);
	void (*property)(void *, const char *);
};

static void bin_emit_cell(void *e, cell_t val)
{
	struct data *dtbuf = e;

	*dtbuf = data_append_cell(*dtbuf, val);
}

static void bin_emit_string(void *e, char *str, int len)
{
	struct data *dtbuf = e;

	if (len == 0)
		len = strlen(str);

	*dtbuf = data_append_data(*dtbuf, str, len);
	*dtbuf = data_append_byte(*dtbuf, '\0');
}

static void bin_emit_align(void *e, int a)
{
	struct data *dtbuf = e;

	*dtbuf = data_append_align(*dtbuf, a);
}

static void bin_emit_data(void *e, struct data d)
{
	struct data *dtbuf = e;

	*dtbuf = data_append_data(*dtbuf, d.val, d.len);
}

static void bin_emit_beginnode(void *e, const char *label)
{
	bin_emit_cell(e, FDT_BEGIN_NODE);
}

static void bin_emit_endnode(void *e, const char *label)
{
	bin_emit_cell(e, FDT_END_NODE);
}

static void bin_emit_property(void *e, const char *label)
{
	bin_emit_cell(e, FDT_PROP);
}

static struct emitter bin_emitter = {
	.cell = bin_emit_cell,
	.string = bin_emit_string,
	.align = bin_emit_align,
	.data = bin_emit_data,
	.beginnode = bin_emit_beginnode,
	.endnode = bin_emit_endnode,
	.property = bin_emit_property,
};

static void emit_label(FILE *f, const char *prefix, const char *label)
{
	fprintf(f, "\t.globl\t%s_%s\n", prefix, label);
	fprintf(f, "%s_%s:\n", prefix, label);
	fprintf(f, "_%s_%s:\n", prefix, label);
}

static void emit_offset_label(FILE *f, const char *label, int offset)
{
	fprintf(f, "\t.globl\t%s\n", label);
	fprintf(f, "%s\t= . + %d\n", label, offset);
}

static void asm_emit_cell(void *e, cell_t val)
{
	FILE *f = e;

	fprintf(f, "\t.long\t0x%x\n", val);
}

static void asm_emit_string(void *e, char *str, int len)
{
	FILE *f = e;
	char c = 0;

	if (len != 0) {
		/* XXX: ewww */
		c = str[len];
		str[len] = '\0';
	}

	fprintf(f, "\t.string\t\"%s\"\n", str);

	if (len != 0) {
		str[len] = c;
	}
}

static void asm_emit_align(void *e, int a)
{
	FILE *f = e;

	fprintf(f, "\t.balign\t%d\n", a);
}

static void asm_emit_data(void *e, struct data d)
{
	FILE *f = e;
	int off = 0;
	struct marker *m;

	m = d.markers;
	while (m) {
		if (m->type == LABEL)
			emit_offset_label(f, m->ref, m->offset);
		m = m->next;
	}

	while ((d.len - off) >= sizeof(u32)) {
		fprintf(f, "\t.long\t0x%x\n",
			be32_to_cpu(*((u32 *)(d.val+off))));
		off += sizeof(u32);
	}

	if ((d.len - off) >= sizeof(u16)) {
		fprintf(f, "\t.short\t0x%hx\n",
			be16_to_cpu(*((u16 *)(d.val+off))));
		off += sizeof(u16);
	}

	if ((d.len - off) >= 1) {
		fprintf(f, "\t.byte\t0x%hhx\n", d.val[off]);
		off += 1;
	}

	assert(off == d.len);
}

static void asm_emit_beginnode(void *e, const char *label)
{
	FILE *f = e;

	if (label) {
		fprintf(f, "\t.globl\t%s\n", label);
		fprintf(f, "%s:\n", label);
	}
	fprintf(f, "\t.long\tFDT_BEGIN_NODE\n");
}

static void asm_emit_endnode(void *e, const char *label)
{
	FILE *f = e;

	fprintf(f, "\t.long\tFDT_END_NODE\n");
	if (label) {
		fprintf(f, "\t.globl\t%s_end\n", label);
		fprintf(f, "%s_end:\n", label);
	}
}

static void asm_emit_property(void *e, const char *label)
{
	FILE *f = e;

	if (label) {
		fprintf(f, "\t.globl\t%s\n", label);
		fprintf(f, "%s:\n", label);
	}
	fprintf(f, "\t.long\tFDT_PROP\n");
}

static struct emitter asm_emitter = {
	.cell = asm_emit_cell,
	.string = asm_emit_string,
	.align = asm_emit_align,
	.data = asm_emit_data,
	.beginnode = asm_emit_beginnode,
	.endnode = asm_emit_endnode,
	.property = asm_emit_property,
};

static int stringtable_insert(struct data *d, const char *str)
{
	int i;

	/* FIXME: do this more efficiently? */

	for (i = 0; i < d->len; i++) {
		if (streq(str, d->val + i))
			return i;
	}

	*d = data_append_data(*d, str, strlen(str)+1);
	return i;
}

static void flatten_tree(struct node *tree, struct emitter *emit,
			 void *etarget, struct data *strbuf,
			 struct version_info *vi)
{
	struct property *prop;
	struct node *child;
	int seen_name_prop = 0;

	emit->beginnode(etarget, tree->label);

	if (vi->flags & FTF_FULLPATH)
		emit->string(etarget, tree->fullpath, 0);
	else
		emit->string(etarget, tree->name, 0);

	emit->align(etarget, sizeof(cell_t));

	for_each_property(tree, prop) {
		int nameoff;

		if (streq(prop->name, "name"))
			seen_name_prop = 1;

		nameoff = stringtable_insert(strbuf, prop->name);

		emit->property(etarget, prop->label);
		emit->cell(etarget, prop->val.len);
		emit->cell(etarget, nameoff);

		if ((vi->flags & FTF_VARALIGN) && (prop->val.len >= 8))
			emit->align(etarget, 8);

		emit->data(etarget, prop->val);
		emit->align(etarget, sizeof(cell_t));
	}

	if ((vi->flags & FTF_NAMEPROPS) && !seen_name_prop) {
		emit->property(etarget, NULL);
		emit->cell(etarget, tree->basenamelen+1);
		emit->cell(etarget, stringtable_insert(strbuf, "name"));

		if ((vi->flags & FTF_VARALIGN) && ((tree->basenamelen+1) >= 8))
			emit->align(etarget, 8);

		emit->string(etarget, tree->name, tree->basenamelen);
		emit->align(etarget, sizeof(cell_t));
	}

	for_each_child(tree, child) {
		flatten_tree(child, emit, etarget, strbuf, vi);
	}

	emit->endnode(etarget, tree->label);
}

static struct data flatten_reserve_list(struct reserve_info *reservelist,
				 struct version_info *vi)
{
	struct reserve_info *re;
	struct data d = empty_data;
	static struct fdt_reserve_entry null_re = {0,0};
	int    j;

	for (re = reservelist; re; re = re->next) {
		d = data_append_re(d, &re->re);
	}
	/*
	 * Add additional reserved slots if the user asked for them.
	 */
	for (j = 0; j < reservenum; j++) {
		d = data_append_re(d, &null_re);
	}

	return d;
}

static void make_fdt_header(struct fdt_header *fdt,
			    struct version_info *vi,
			    int reservesize, int dtsize, int strsize,
			    int boot_cpuid_phys)
{
	int reserve_off;

	reservesize += sizeof(struct fdt_reserve_entry);

	memset(fdt, 0xff, sizeof(*fdt));

	fdt->magic = cpu_to_be32(FDT_MAGIC);
	fdt->version = cpu_to_be32(vi->version);
	fdt->last_comp_version = cpu_to_be32(vi->last_comp_version);

	/* Reserve map should be doubleword aligned */
	reserve_off = ALIGN(vi->hdr_size, 8);

	fdt->off_mem_rsvmap = cpu_to_be32(reserve_off);
	fdt->off_dt_struct = cpu_to_be32(reserve_off + reservesize);
	fdt->off_dt_strings = cpu_to_be32(reserve_off + reservesize
					  + dtsize);
	fdt->totalsize = cpu_to_be32(reserve_off + reservesize + dtsize + strsize);

	if (vi->flags & FTF_BOOTCPUID)
		fdt->boot_cpuid_phys = cpu_to_be32(boot_cpuid_phys);
	if (vi->flags & FTF_STRTABSIZE)
		fdt->size_dt_strings = cpu_to_be32(strsize);
	if (vi->flags & FTF_STRUCTSIZE)
		fdt->size_dt_struct = cpu_to_be32(dtsize);
}

void dt_to_blob(FILE *f, struct boot_info *bi, int version,
		int boot_cpuid_phys)
{
	struct version_info *vi = NULL;
	int i;
	struct data blob       = empty_data;
	struct data reservebuf = empty_data;
	struct data dtbuf      = empty_data;
	struct data strbuf     = empty_data;
	struct fdt_header fdt;
	int padlen = 0;

	for (i = 0; i < ARRAY_SIZE(version_table); i++) {
		if (version_table[i].version == version)
			vi = &version_table[i];
	}
	if (!vi)
		die("Unknown device tree blob version %d\n", version);

	flatten_tree(bi->dt, &bin_emitter, &dtbuf, &strbuf, vi);
	bin_emit_cell(&dtbuf, FDT_END);

	reservebuf = flatten_reserve_list(bi->reservelist, vi);

	/* Make header */
	make_fdt_header(&fdt, vi, reservebuf.len, dtbuf.len, strbuf.len,
			boot_cpuid_phys);

	/*
	 * If the user asked for more space than is used, adjust the totalsize.
	 */
	if (minsize > 0) {
		padlen = minsize - be32_to_cpu(fdt.totalsize);
		if ((padlen < 0) && (quiet < 1))
			fprintf(stderr,
				"Warning: blob size %d >= minimum size %d\n",
				be32_to_cpu(fdt.totalsize), minsize);
	}

	if (padsize > 0)
		padlen = padsize;

	if (padlen > 0) {
		int tsize = be32_to_cpu(fdt.totalsize);
		tsize += padlen;
		fdt.totalsize = cpu_to_be32(tsize);
	}

	/*
	 * Assemble the blob: start with the header, add with alignment
	 * the reserve buffer, add the reserve map terminating zeroes,
	 * the device tree itself, and finally the strings.
	 */
	blob = data_append_data(blob, &fdt, sizeof(fdt));
	blob = data_append_align(blob, 8);
	blob = data_merge(blob, reservebuf);
	blob = data_append_zeroes(blob, sizeof(struct fdt_reserve_entry));
	blob = data_merge(blob, dtbuf);
	blob = data_merge(blob, strbuf);

	/*
	 * If the user asked for more space than is used, pad out the blob.
	 */
	if (padlen > 0)
		blob = data_append_zeroes(blob, padlen);

	fwrite(blob.val, blob.len, 1, f);

	if (ferror(f))
		die("Error writing device tree blob: %s\n", strerror(errno));

	/*
	 * data_merge() frees the right-hand element so only the blob
	 * remains to be freed.
	 */
	data_free(blob);
}

static void dump_stringtable_asm(FILE *f, struct data strbuf)
{
	const char *p;
	int len;

	p = strbuf.val;

	while (p < (strbuf.val + strbuf.len)) {
		len = strlen(p);
		fprintf(f, "\t.string \"%s\"\n", p);
		p += len+1;
	}
}

void dt_to_asm(FILE *f, struct boot_info *bi, int version, int boot_cpuid_phys)
{
	struct version_info *vi = NULL;
	int i;
	struct data strbuf = empty_data;
	struct reserve_info *re;
	const char *symprefix = "dt";

	for (i = 0; i < ARRAY_SIZE(version_table); i++) {
		if (version_table[i].version == version)
			vi = &version_table[i];
	}
	if (!vi)
		die("Unknown device tree blob version %d\n", version);

	fprintf(f, "/* autogenerated by dtc, do not edit */\n\n");
	fprintf(f, "#define FDT_MAGIC 0x%x\n", FDT_MAGIC);
	fprintf(f, "#define FDT_BEGIN_NODE 0x%x\n", FDT_BEGIN_NODE);
	fprintf(f, "#define FDT_END_NODE 0x%x\n", FDT_END_NODE);
	fprintf(f, "#define FDT_PROP 0x%x\n", FDT_PROP);
	fprintf(f, "#define FDT_END 0x%x\n", FDT_END);
	fprintf(f, "\n");

	emit_label(f, symprefix, "blob_start");
	emit_label(f, symprefix, "header");
	fprintf(f, "\t.long\tFDT_MAGIC\t\t\t\t/* magic */\n");
	fprintf(f, "\t.long\t_%s_blob_abs_end - _%s_blob_start\t/* totalsize */\n",
		symprefix, symprefix);
	fprintf(f, "\t.long\t_%s_struct_start - _%s_blob_start\t/* off_dt_struct */\n",
		symprefix, symprefix);
	fprintf(f, "\t.long\t_%s_strings_start - _%s_blob_start\t/* off_dt_strings */\n",
		symprefix, symprefix);
	fprintf(f, "\t.long\t_%s_reserve_map - _%s_blob_start\t/* off_dt_strings */\n",
		symprefix, symprefix);
	fprintf(f, "\t.long\t%d\t\t\t\t\t/* version */\n", vi->version);
	fprintf(f, "\t.long\t%d\t\t\t\t\t/* last_comp_version */\n",
		vi->last_comp_version);

	if (vi->flags & FTF_BOOTCPUID)
		fprintf(f, "\t.long\t%i\t\t\t\t\t/* boot_cpuid_phys */\n",
			boot_cpuid_phys);

	if (vi->flags & FTF_STRTABSIZE)
		fprintf(f, "\t.long\t_%s_strings_end - _%s_strings_start\t/* size_dt_strings */\n",
			symprefix, symprefix);

	if (vi->flags & FTF_STRUCTSIZE)
		fprintf(f, "\t.long\t_%s_struct_end - _%s_struct_start\t/* size_dt_struct */\n",
			symprefix, symprefix);

	/*
	 * Reserve map entries.
	 * Align the reserve map to a doubleword boundary.
	 * Each entry is an (address, size) pair of u64 values.
	 * Always supply a zero-sized temination entry.
	 */
	asm_emit_align(f, 8);
	emit_label(f, symprefix, "reserve_map");

	fprintf(f, "/* Memory reserve map from source file */\n");

	/*
	 * Use .long on high and low halfs of u64s to avoid .quad
	 * as it appears .quad isn't available in some assemblers.
	 */
	for (re = bi->reservelist; re; re = re->next) {
		if (re->label) {
			fprintf(f, "\t.globl\t%s\n", re->label);
			fprintf(f, "%s:\n", re->label);
		}
		fprintf(f, "\t.long\t0x%08x, 0x%08x\n",
			(unsigned int)(re->re.address >> 32),
			(unsigned int)(re->re.address & 0xffffffff));
		fprintf(f, "\t.long\t0x%08x, 0x%08x\n",
			(unsigned int)(re->re.size >> 32),
			(unsigned int)(re->re.size & 0xffffffff));
	}
	for (i = 0; i < reservenum; i++) {
		fprintf(f, "\t.long\t0, 0\n\t.long\t0, 0\n");
	}

	fprintf(f, "\t.long\t0, 0\n\t.long\t0, 0\n");

	emit_label(f, symprefix, "struct_start");
	flatten_tree(bi->dt, &asm_emitter, f, &strbuf, vi);
	fprintf(f, "\t.long\tFDT_END\n");
	emit_label(f, symprefix, "struct_end");

	emit_label(f, symprefix, "strings_start");
	dump_stringtable_asm(f, strbuf);
	emit_label(f, symprefix, "strings_end");

	emit_label(f, symprefix, "blob_end");

	/*
	 * If the user asked for more space than is used, pad it out.
	 */
	if (minsize > 0) {
		fprintf(f, "\t.space\t%d - (_%s_blob_end - _%s_blob_start), 0\n",
			minsize, symprefix, symprefix);
	}
	if (padsize > 0) {
		fprintf(f, "\t.space\t%d, 0\n", padsize);
	}
	emit_label(f, symprefix, "blob_abs_end");

	data_free(strbuf);
}

struct inbuf {
	char *base, *limit, *ptr;
};

static void inbuf_init(struct inbuf *inb, void *base, void *limit)
{
	inb->base = base;
	inb->limit = limit;
	inb->ptr = inb->base;
}

static void flat_read_chunk(struct inbuf *inb, void *p, int len)
{
	if ((inb->ptr + len) > inb->limit)
		die("Premature end of data parsing flat device tree\n");

	memcpy(p, inb->ptr, len);

	inb->ptr += len;
}

static u32 flat_read_word(struct inbuf *inb)
{
	u32 val;

	assert(((inb->ptr - inb->base) % sizeof(val)) == 0);

	flat_read_chunk(inb, &val, sizeof(val));

	return be32_to_cpu(val);
}

static void flat_realign(struct inbuf *inb, int align)
{
	int off = inb->ptr - inb->base;

	inb->ptr = inb->base + ALIGN(off, align);
	if (inb->ptr > inb->limit)
		die("Premature end of data parsing flat device tree\n");
}

static char *flat_read_string(struct inbuf *inb)
{
	int len = 0;
	const char *p = inb->ptr;
	char *str;

	do {
		if (p >= inb->limit)
			die("Premature end of data parsing flat device tree\n");
		len++;
	} while ((*p++) != '\0');

	str = strdup(inb->ptr);

	inb->ptr += len;

	flat_realign(inb, sizeof(u32));

	return str;
}

static struct data flat_read_data(struct inbuf *inb, int len)
{
	struct data d = empty_data;

	if (len == 0)
		return empty_data;

	d = data_grow_for(d, len);
	d.len = len;

	flat_read_chunk(inb, d.val, len);

	flat_realign(inb, sizeof(u32));

	return d;
}

static char *flat_read_stringtable(struct inbuf *inb, int offset)
{
	const char *p;

	p = inb->base + offset;
	while (1) {
		if (p >= inb->limit || p < inb->base)
			die("String offset %d overruns string table\n",
			    offset);

		if (*p == '\0')
			break;

		p++;
	}

	return strdup(inb->base + offset);
}

static struct property *flat_read_property(struct inbuf *dtbuf,
					   struct inbuf *strbuf, int flags)
{
	u32 proplen, stroff;
	char *name;
	struct data val;

	proplen = flat_read_word(dtbuf);
	stroff = flat_read_word(dtbuf);

	name = flat_read_stringtable(strbuf, stroff);

	if ((flags & FTF_VARALIGN) && (proplen >= 8))
		flat_realign(dtbuf, 8);

	val = flat_read_data(dtbuf, proplen);

	return build_property(name, val, NULL);
}


static struct reserve_info *flat_read_mem_reserve(struct inbuf *inb)
{
	struct reserve_info *reservelist = NULL;
	struct reserve_info *new;
	const char *p;
	struct fdt_reserve_entry re;

	/*
	 * Each entry is a pair of u64 (addr, size) values for 4 cell_t's.
	 * List terminates at an entry with size equal to zero.
	 *
	 * First pass, count entries.
	 */
	p = inb->ptr;
	while (1) {
		flat_read_chunk(inb, &re, sizeof(re));
		re.address  = be64_to_cpu(re.address);
		re.size = be64_to_cpu(re.size);
		if (re.size == 0)
			break;

		new = build_reserve_entry(re.address, re.size, NULL);
		reservelist = add_reserve_entry(reservelist, new);
	}

	return reservelist;
}


static char *nodename_from_path(const char *ppath, const char *cpath)
{
	const char *lslash;
	int plen;

	lslash = strrchr(cpath, '/');
	if (! lslash)
		return NULL;

	plen = lslash - cpath;

	if (streq(cpath, "/") && streq(ppath, ""))
		return "";

	if ((plen == 0) && streq(ppath, "/"))
		return strdup(lslash+1);

	if (! strneq(ppath, cpath, plen))
		return NULL;

	return strdup(lslash+1);
}

static const char PROPCHAR[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,._+*#?-";
static const char UNITCHAR[] = "0123456789abcdef,";

static int check_node_name(const char *name)
{
	const char *atpos;
	int basenamelen;

	atpos = strrchr(name, '@');

	if (atpos)
		basenamelen = atpos - name;
	else
		basenamelen = strlen(name);

	if (strspn(name, PROPCHAR) < basenamelen)
		return -1;

	if (atpos
	    && ((basenamelen + 1 + strspn(atpos+1, UNITCHAR)) < strlen(name)))
		return -1;

	return basenamelen;
}

static struct node *unflatten_tree(struct inbuf *dtbuf,
				   struct inbuf *strbuf,
				   const char *parent_path, int flags)
{
	struct node *node;
	u32 val;

	node = build_node(NULL, NULL);

	if (flags & FTF_FULLPATH) {
		node->fullpath = flat_read_string(dtbuf);
		node->name = nodename_from_path(parent_path, node->fullpath);

		if (! node->name)
			die("Path \"%s\" is not valid as a child of \"%s\"\n",
			    node->fullpath, parent_path);
	} else {
		node->name = flat_read_string(dtbuf);
		node->fullpath = join_path(parent_path, node->name);
	}

	node->basenamelen = check_node_name(node->name);
	if (node->basenamelen < 0) {
		fprintf(stderr, "Warning \"%s\" has incorrect format\n", node->name);
	}

	do {
		struct property *prop;
		struct node *child;

		val = flat_read_word(dtbuf);
		switch (val) {
		case FDT_PROP:
			if (node->children)
				fprintf(stderr, "Warning: Flat tree input has "
					"subnodes preceding a property.\n");
			prop = flat_read_property(dtbuf, strbuf, flags);
			add_property(node, prop);
			break;

		case FDT_BEGIN_NODE:
			child = unflatten_tree(dtbuf,strbuf, node->fullpath,
					       flags);
			add_child(node, child);
			break;

		case FDT_END_NODE:
			break;

		case FDT_END:
			die("Premature FDT_END in device tree blob\n");
			break;

		case FDT_NOP:
			if (!(flags & FTF_NOPS))
				fprintf(stderr, "Warning: NOP tag found in flat tree"
					" version <16\n");

			/* Ignore */
			break;

		default:
			die("Invalid opcode word %08x in device tree blob\n",
			    val);
		}
	} while (val != FDT_END_NODE);

	return node;
}


struct boot_info *dt_from_blob(FILE *f)
{
	u32 magic, totalsize, version, size_str, size_dt;
	u32 off_dt, off_str, off_mem_rsvmap;
	int rc;
	char *blob;
	struct fdt_header *fdt;
	char *p;
	struct inbuf dtbuf, strbuf;
	struct inbuf memresvbuf;
	int sizeleft;
	struct reserve_info *reservelist;
	struct node *tree;
	u32 val;
	int flags = 0;

	rc = fread(&magic, sizeof(magic), 1, f);
	if (ferror(f))
		die("Error reading DT blob magic number: %s\n",
		    strerror(errno));
	if (rc < 1) {
		if (feof(f))
			die("EOF reading DT blob magic number\n");
		else
			die("Mysterious short read reading magic number\n");
	}

	magic = be32_to_cpu(magic);
	if (magic != FDT_MAGIC)
		die("Blob has incorrect magic number\n");

	rc = fread(&totalsize, sizeof(totalsize), 1, f);
	if (ferror(f))
		die("Error reading DT blob size: %s\n", strerror(errno));
	if (rc < 1) {
		if (feof(f))
			die("EOF reading DT blob size\n");
		else
			die("Mysterious short read reading blob size\n");
	}

	totalsize = be32_to_cpu(totalsize);
	if (totalsize < FDT_V1_SIZE)
		die("DT blob size (%d) is too small\n", totalsize);

	blob = xmalloc(totalsize);

	fdt = (struct fdt_header *)blob;
	fdt->magic = cpu_to_be32(magic);
	fdt->totalsize = cpu_to_be32(totalsize);

	sizeleft = totalsize - sizeof(magic) - sizeof(totalsize);
	p = blob + sizeof(magic)  + sizeof(totalsize);

	while (sizeleft) {
		if (feof(f))
			die("EOF before reading %d bytes of DT blob\n",
			    totalsize);

		rc = fread(p, 1, sizeleft, f);
		if (ferror(f))
			die("Error reading DT blob: %s\n",
			    strerror(errno));

		sizeleft -= rc;
		p += rc;
	}

	off_dt = be32_to_cpu(fdt->off_dt_struct);
	off_str = be32_to_cpu(fdt->off_dt_strings);
	off_mem_rsvmap = be32_to_cpu(fdt->off_mem_rsvmap);
	version = be32_to_cpu(fdt->version);

	fprintf(stderr, "\tmagic:\t\t\t0x%x\n", magic);
	fprintf(stderr, "\ttotalsize:\t\t%d\n", totalsize);
	fprintf(stderr, "\toff_dt_struct:\t\t0x%x\n", off_dt);
	fprintf(stderr, "\toff_dt_strings:\t\t0x%x\n", off_str);
	fprintf(stderr, "\toff_mem_rsvmap:\t\t0x%x\n", off_mem_rsvmap);
	fprintf(stderr, "\tversion:\t\t0x%x\n", version );
	fprintf(stderr, "\tlast_comp_version:\t0x%x\n",
		be32_to_cpu(fdt->last_comp_version));

	if (off_mem_rsvmap >= totalsize)
		die("Mem Reserve structure offset exceeds total size\n");

	if (off_dt >= totalsize)
		die("DT structure offset exceeds total size\n");

	if (off_str > totalsize)
		die("String table offset exceeds total size\n");

	if (version >= 2)
		fprintf(stderr, "\tboot_cpuid_phys:\t0x%x\n",
			be32_to_cpu(fdt->boot_cpuid_phys));

	size_str = -1;
	if (version >= 3) {
		size_str = be32_to_cpu(fdt->size_dt_strings);
		fprintf(stderr, "\tsize_dt_strings:\t%d\n", size_str);
		if (off_str+size_str > totalsize)
			die("String table extends past total size\n");
	}

	if (version >= 17) {
		size_dt = be32_to_cpu(fdt->size_dt_struct);
		fprintf(stderr, "\tsize_dt_struct:\t\t%d\n", size_dt);
		if (off_dt+size_dt > totalsize)
			die("Structure block extends past total size\n");
	}

	if (version < 16) {
		flags |= FTF_FULLPATH | FTF_NAMEPROPS | FTF_VARALIGN;
	} else {
		flags |= FTF_NOPS;
	}

	inbuf_init(&memresvbuf,
		   blob + off_mem_rsvmap, blob + totalsize);
	inbuf_init(&dtbuf, blob + off_dt, blob + totalsize);
	if (size_str >= 0)
		inbuf_init(&strbuf, blob + off_str, blob + off_str + size_str);
	else
		inbuf_init(&strbuf, blob + off_str, blob + totalsize);

	reservelist = flat_read_mem_reserve(&memresvbuf);

	val = flat_read_word(&dtbuf);

	if (val != FDT_BEGIN_NODE)
		die("Device tree blob doesn't begin with FDT_BEGIN_NODE (begins with 0x%08x)\n", val);

	tree = unflatten_tree(&dtbuf, &strbuf, "", flags);

	val = flat_read_word(&dtbuf);
	if (val != FDT_END)
		die("Device tree blob doesn't end with FDT_END\n");

	free(blob);

	return build_boot_info(reservelist, tree);
}
