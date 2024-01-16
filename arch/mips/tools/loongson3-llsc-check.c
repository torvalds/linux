// SPDX-License-Identifier: GPL-2.0-only
#include <byteswap.h>
#include <elf.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef be32toh
/* If libc provides le{16,32,64}toh() then we'll use them */
#elif BYTE_ORDER == LITTLE_ENDIAN
# define le16toh(x)	(x)
# define le32toh(x)	(x)
# define le64toh(x)	(x)
#elif BYTE_ORDER == BIG_ENDIAN
# define le16toh(x)	bswap_16(x)
# define le32toh(x)	bswap_32(x)
# define le64toh(x)	bswap_64(x)
#endif

/* MIPS opcodes, in bits 31:26 of an instruction */
#define OP_SPECIAL	0x00
#define OP_REGIMM	0x01
#define OP_BEQ		0x04
#define OP_BNE		0x05
#define OP_BLEZ		0x06
#define OP_BGTZ		0x07
#define OP_BEQL		0x14
#define OP_BNEL		0x15
#define OP_BLEZL	0x16
#define OP_BGTZL	0x17
#define OP_LL		0x30
#define OP_LLD		0x34
#define OP_SC		0x38
#define OP_SCD		0x3c

/* Bits 20:16 of OP_REGIMM instructions */
#define REGIMM_BLTZ	0x00
#define REGIMM_BGEZ	0x01
#define REGIMM_BLTZL	0x02
#define REGIMM_BGEZL	0x03
#define REGIMM_BLTZAL	0x10
#define REGIMM_BGEZAL	0x11
#define REGIMM_BLTZALL	0x12
#define REGIMM_BGEZALL	0x13

/* Bits 5:0 of OP_SPECIAL instructions */
#define SPECIAL_SYNC	0x0f

static void usage(FILE *f)
{
	fprintf(f, "Usage: loongson3-llsc-check /path/to/vmlinux\n");
}

static int se16(uint16_t x)
{
	return (int16_t)x;
}

static bool is_ll(uint32_t insn)
{
	switch (insn >> 26) {
	case OP_LL:
	case OP_LLD:
		return true;

	default:
		return false;
	}
}

static bool is_sc(uint32_t insn)
{
	switch (insn >> 26) {
	case OP_SC:
	case OP_SCD:
		return true;

	default:
		return false;
	}
}

static bool is_sync(uint32_t insn)
{
	/* Bits 31:11 should all be zeroes */
	if (insn >> 11)
		return false;

	/* Bits 5:0 specify the SYNC special encoding */
	if ((insn & 0x3f) != SPECIAL_SYNC)
		return false;

	return true;
}

static bool is_branch(uint32_t insn, int *off)
{
	switch (insn >> 26) {
	case OP_BEQ:
	case OP_BEQL:
	case OP_BNE:
	case OP_BNEL:
	case OP_BGTZ:
	case OP_BGTZL:
	case OP_BLEZ:
	case OP_BLEZL:
		*off = se16(insn) + 1;
		return true;

	case OP_REGIMM:
		switch ((insn >> 16) & 0x1f) {
		case REGIMM_BGEZ:
		case REGIMM_BGEZL:
		case REGIMM_BGEZAL:
		case REGIMM_BGEZALL:
		case REGIMM_BLTZ:
		case REGIMM_BLTZL:
		case REGIMM_BLTZAL:
		case REGIMM_BLTZALL:
			*off = se16(insn) + 1;
			return true;

		default:
			return false;
		}

	default:
		return false;
	}
}

static int check_ll(uint64_t pc, uint32_t *code, size_t sz)
{
	ssize_t i, max, sc_pos;
	int off;

	/*
	 * Every LL must be preceded by a sync instruction in order to ensure
	 * that instruction reordering doesn't allow a prior memory access to
	 * execute after the LL & cause erroneous results.
	 */
	if (!is_sync(le32toh(code[-1]))) {
		fprintf(stderr, "%" PRIx64 ": LL not preceded by sync\n", pc);
		return -EINVAL;
	}

	/* Find the matching SC instruction */
	max = sz / 4;
	for (sc_pos = 0; sc_pos < max; sc_pos++) {
		if (is_sc(le32toh(code[sc_pos])))
			break;
	}
	if (sc_pos >= max) {
		fprintf(stderr, "%" PRIx64 ": LL has no matching SC\n", pc);
		return -EINVAL;
	}

	/*
	 * Check branches within the LL/SC loop target sync instructions,
	 * ensuring that speculative execution can't generate memory accesses
	 * due to instructions outside of the loop.
	 */
	for (i = 0; i < sc_pos; i++) {
		if (!is_branch(le32toh(code[i]), &off))
			continue;

		/*
		 * If the branch target is within the LL/SC loop then we don't
		 * need to worry about it.
		 */
		if ((off >= -i) && (off <= sc_pos))
			continue;

		/* If the branch targets a sync instruction we're all good... */
		if (is_sync(le32toh(code[i + off])))
			continue;

		/* ...but if not, we have a problem */
		fprintf(stderr, "%" PRIx64 ": Branch target not a sync\n",
			pc + (i * 4));
		return -EINVAL;
	}

	return 0;
}

static int check_code(uint64_t pc, uint32_t *code, size_t sz)
{
	int err = 0;

	if (sz % 4) {
		fprintf(stderr, "%" PRIx64 ": Section size not a multiple of 4\n",
			pc);
		err = -EINVAL;
		sz -= (sz % 4);
	}

	if (is_ll(le32toh(code[0]))) {
		fprintf(stderr, "%" PRIx64 ": First instruction in section is an LL\n",
			pc);
		err = -EINVAL;
	}

#define advance() (	\
	code++,		\
	pc += 4,	\
	sz -= 4		\
)

	/*
	 * Skip the first instruction, allowing check_ll to look backwards
	 * unconditionally.
	 */
	advance();

	/* Now scan through the code looking for LL instructions */
	for (; sz; advance()) {
		if (is_ll(le32toh(code[0])))
			err |= check_ll(pc, code, sz);
	}

	return err;
}

int main(int argc, char *argv[])
{
	int vmlinux_fd, status, err, i;
	const char *vmlinux_path;
	struct stat st;
	Elf64_Ehdr *eh;
	Elf64_Shdr *sh;
	void *vmlinux;

	status = EXIT_FAILURE;

	if (argc < 2) {
		usage(stderr);
		goto out_ret;
	}

	vmlinux_path = argv[1];
	vmlinux_fd = open(vmlinux_path, O_RDONLY);
	if (vmlinux_fd == -1) {
		perror("Unable to open vmlinux");
		goto out_ret;
	}

	err = fstat(vmlinux_fd, &st);
	if (err) {
		perror("Unable to stat vmlinux");
		goto out_close;
	}

	vmlinux = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, vmlinux_fd, 0);
	if (vmlinux == MAP_FAILED) {
		perror("Unable to mmap vmlinux");
		goto out_close;
	}

	eh = vmlinux;
	if (memcmp(eh->e_ident, ELFMAG, SELFMAG)) {
		fprintf(stderr, "vmlinux is not an ELF?\n");
		goto out_munmap;
	}

	if (eh->e_ident[EI_CLASS] != ELFCLASS64) {
		fprintf(stderr, "vmlinux is not 64b?\n");
		goto out_munmap;
	}

	if (eh->e_ident[EI_DATA] != ELFDATA2LSB) {
		fprintf(stderr, "vmlinux is not little endian?\n");
		goto out_munmap;
	}

	for (i = 0; i < le16toh(eh->e_shnum); i++) {
		sh = vmlinux + le64toh(eh->e_shoff) + (i * le16toh(eh->e_shentsize));

		if (sh->sh_type != SHT_PROGBITS)
			continue;
		if (!(sh->sh_flags & SHF_EXECINSTR))
			continue;

		err = check_code(le64toh(sh->sh_addr),
				 vmlinux + le64toh(sh->sh_offset),
				 le64toh(sh->sh_size));
		if (err)
			goto out_munmap;
	}

	status = EXIT_SUCCESS;
out_munmap:
	munmap(vmlinux, st.st_size);
out_close:
	close(vmlinux_fd);
out_ret:
	fprintf(stdout, "loongson3-llsc-check returns %s\n",
		status ? "failure" : "success");
	return status;
}
