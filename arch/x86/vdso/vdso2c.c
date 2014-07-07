#include <inttypes.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <err.h>

#include <sys/mman.h>
#include <sys/types.h>

#include <tools/le_byteshift.h>

#include <linux/elf.h>
#include <linux/types.h>

const char *outfilename;

/* Symbols that we need in vdso2c. */
enum {
	sym_vvar_page,
	sym_hpet_page,
	sym_end_mapping,
	sym_VDSO_FAKE_SECTION_TABLE_START,
	sym_VDSO_FAKE_SECTION_TABLE_END,
};

const int special_pages[] = {
	sym_vvar_page,
	sym_hpet_page,
};

struct vdso_sym {
	const char *name;
	bool export;
};

struct vdso_sym required_syms[] = {
	[sym_vvar_page] = {"vvar_page", true},
	[sym_hpet_page] = {"hpet_page", true},
	[sym_end_mapping] = {"end_mapping", true},
	[sym_VDSO_FAKE_SECTION_TABLE_START] = {
		"VDSO_FAKE_SECTION_TABLE_START", false
	},
	[sym_VDSO_FAKE_SECTION_TABLE_END] = {
		"VDSO_FAKE_SECTION_TABLE_END", false
	},
	{"VDSO32_NOTE_MASK", true},
	{"VDSO32_SYSENTER_RETURN", true},
	{"__kernel_vsyscall", true},
	{"__kernel_sigreturn", true},
	{"__kernel_rt_sigreturn", true},
};

__attribute__((format(printf, 1, 2))) __attribute__((noreturn))
static void fail(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	fprintf(stderr, "Error: ");
	vfprintf(stderr, format, ap);
	unlink(outfilename);
	exit(1);
	va_end(ap);
}

/*
 * Evil macros for little-endian reads and writes
 */
#define GLE(x, bits, ifnot)						\
	__builtin_choose_expr(						\
		(sizeof(*(x)) == bits/8),				\
		(__typeof__(*(x)))get_unaligned_le##bits(x), ifnot)

extern void bad_get_le(void);
#define LAST_GLE(x)							\
	__builtin_choose_expr(sizeof(*(x)) == 1, *(x), bad_get_le())

#define GET_LE(x)							\
	GLE(x, 64, GLE(x, 32, GLE(x, 16, LAST_GLE(x))))

#define PLE(x, val, bits, ifnot)					\
	__builtin_choose_expr(						\
		(sizeof(*(x)) == bits/8),				\
		put_unaligned_le##bits((val), (x)), ifnot)

extern void bad_put_le(void);
#define LAST_PLE(x, val)						\
	__builtin_choose_expr(sizeof(*(x)) == 1, *(x) = (val), bad_put_le())

#define PUT_LE(x, val)					\
	PLE(x, val, 64, PLE(x, val, 32, PLE(x, val, 16, LAST_PLE(x, val))))


#define NSYMS (sizeof(required_syms) / sizeof(required_syms[0]))

#define BITSFUNC3(name, bits) name##bits
#define BITSFUNC2(name, bits) BITSFUNC3(name, bits)
#define BITSFUNC(name) BITSFUNC2(name, ELF_BITS)

#define ELF_BITS_XFORM2(bits, x) Elf##bits##_##x
#define ELF_BITS_XFORM(bits, x) ELF_BITS_XFORM2(bits, x)
#define ELF(x) ELF_BITS_XFORM(ELF_BITS, x)

#define ELF_BITS 64
#include "vdso2c.h"
#undef ELF_BITS

#define ELF_BITS 32
#include "vdso2c.h"
#undef ELF_BITS

static void go(void *addr, size_t len, FILE *outfile, const char *name)
{
	Elf64_Ehdr *hdr = (Elf64_Ehdr *)addr;

	if (hdr->e_ident[EI_CLASS] == ELFCLASS64) {
		go64(addr, len, outfile, name);
	} else if (hdr->e_ident[EI_CLASS] == ELFCLASS32) {
		go32(addr, len, outfile, name);
	} else {
		fail("unknown ELF class\n");
	}
}

int main(int argc, char **argv)
{
	int fd;
	off_t len;
	void *addr;
	FILE *outfile;
	char *name, *tmp;
	int namelen;

	if (argc != 3) {
		printf("Usage: vdso2c INPUT OUTPUT\n");
		return 1;
	}

	/*
	 * Figure out the struct name.  If we're writing to a .so file,
	 * generate raw output insted.
	 */
	name = strdup(argv[2]);
	namelen = strlen(name);
	if (namelen >= 3 && !strcmp(name + namelen - 3, ".so")) {
		name = NULL;
	} else {
		tmp = strrchr(name, '/');
		if (tmp)
			name = tmp + 1;
		tmp = strchr(name, '.');
		if (tmp)
			*tmp = '\0';
		for (tmp = name; *tmp; tmp++)
			if (*tmp == '-')
				*tmp = '_';
	}

	fd = open(argv[1], O_RDONLY);
	if (fd == -1)
		err(1, "%s", argv[1]);

	len = lseek(fd, 0, SEEK_END);
	if (len == (off_t)-1)
		err(1, "lseek");

	addr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED)
		err(1, "mmap");

	outfilename = argv[2];
	outfile = fopen(outfilename, "w");
	if (!outfile)
		err(1, "%s", argv[2]);

	go(addr, (size_t)len, outfile, name);

	munmap(addr, len);
	fclose(outfile);

	return 0;
}
