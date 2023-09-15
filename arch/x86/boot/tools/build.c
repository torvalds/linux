// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1997 Martin Mares
 *  Copyright (C) 2007 H. Peter Anvin
 */

/*
 * This file builds a disk-image from three different files:
 *
 * - setup: 8086 machine code, sets up system parm
 * - system: 80386 code for actual system
 * - zoffset.h: header with ZO_* defines
 *
 * It does some checking that all files are of the correct type, and writes
 * the result to the specified destination, removing headers and padding to
 * the right amount. It also writes some system data to stdout.
 */

/*
 * Changes by tytso to allow root device specification
 * High loaded stuff by Hans Lermen & Werner Almesberger, Feb. 1996
 * Cross compiling fixes by Gertjan van Wingerde, July 1996
 * Rewritten by Martin Mares, April 1997
 * Substantially overhauled by H. Peter Anvin, April 2007
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <tools/le_byteshift.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* Minimal number of setup sectors */
#define SETUP_SECT_MIN 5
#define SETUP_SECT_MAX 64

/* This must be large enough to hold the entire setup */
u8 buf[SETUP_SECT_MAX*512];

static unsigned long _edata;

/*----------------------------------------------------------------------*/

static const u32 crctab32[] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419,
	0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4,
	0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07,
	0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856,
	0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4,
	0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
	0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3,
	0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a,
	0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599,
	0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190,
	0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f,
	0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e,
	0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
	0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed,
	0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3,
	0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
	0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a,
	0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5,
	0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010,
	0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17,
	0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6,
	0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615,
	0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
	0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344,
	0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a,
	0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
	0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1,
	0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c,
	0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef,
	0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe,
	0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31,
	0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c,
	0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
	0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b,
	0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1,
	0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
	0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278,
	0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7,
	0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66,
	0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605,
	0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8,
	0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b,
	0x2d02ef8d
};

static u32 partial_crc32_one(u8 c, u32 crc)
{
	return crctab32[(crc ^ c) & 0xff] ^ (crc >> 8);
}

static u32 partial_crc32(const u8 *s, int len, u32 crc)
{
	while (len--)
		crc = partial_crc32_one(*s++, crc);
	return crc;
}

static void die(const char * str, ...)
{
	va_list args;
	va_start(args, str);
	vfprintf(stderr, str, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

static void usage(void)
{
	die("Usage: build setup system zoffset.h image");
}

/*
 * Parse zoffset.h and find the entry points. We could just #include zoffset.h
 * but that would mean tools/build would have to be rebuilt every time. It's
 * not as if parsing it is hard...
 */
#define PARSE_ZOFS(p, sym) do { \
	if (!strncmp(p, "#define ZO_" #sym " ", 11+sizeof(#sym)))	\
		sym = strtoul(p + 11 + sizeof(#sym), NULL, 16);		\
} while (0)

static void parse_zoffset(char *fname)
{
	FILE *file;
	char *p;
	int c;

	file = fopen(fname, "r");
	if (!file)
		die("Unable to open `%s': %m", fname);
	c = fread(buf, 1, sizeof(buf) - 1, file);
	if (ferror(file))
		die("read-error on `zoffset.h'");
	fclose(file);
	buf[c] = 0;

	p = (char *)buf;

	while (p && *p) {
		PARSE_ZOFS(p, _edata);

		p = strchr(p, '\n');
		while (p && (*p == '\r' || *p == '\n'))
			p++;
	}
}

int main(int argc, char ** argv)
{
	unsigned int i, sz, setup_sectors;
	int c;
	struct stat sb;
	FILE *file, *dest;
	int fd;
	void *kernel;
	u32 crc = 0xffffffffUL;

	if (argc != 5)
		usage();
	parse_zoffset(argv[3]);

	dest = fopen(argv[4], "w");
	if (!dest)
		die("Unable to write `%s': %m", argv[4]);

	/* Copy the setup code */
	file = fopen(argv[1], "r");
	if (!file)
		die("Unable to open `%s': %m", argv[1]);
	c = fread(buf, 1, sizeof(buf), file);
	if (ferror(file))
		die("read-error on `setup'");
	if (c < 1024)
		die("The setup must be at least 1024 bytes");
	if (get_unaligned_le16(&buf[510]) != 0xAA55)
		die("Boot block hasn't got boot flag (0xAA55)");
	fclose(file);

	/* Pad unused space with zeros */
	setup_sectors = (c + 4095) / 4096;
	setup_sectors *= 8;
	if (setup_sectors < SETUP_SECT_MIN)
		setup_sectors = SETUP_SECT_MIN;
	i = setup_sectors*512;
	memset(buf+c, 0, i-c);

	/* Open and stat the kernel file */
	fd = open(argv[2], O_RDONLY);
	if (fd < 0)
		die("Unable to open `%s': %m", argv[2]);
	if (fstat(fd, &sb))
		die("Unable to stat `%s': %m", argv[2]);
	if (_edata != sb.st_size)
		die("Unexpected file size `%s': %u != %u", argv[2], _edata,
		    sb.st_size);
	sz = _edata - 4;
	kernel = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
	if (kernel == MAP_FAILED)
		die("Unable to mmap '%s': %m", argv[2]);

	crc = partial_crc32(buf, i, crc);
	if (fwrite(buf, 1, i, dest) != i)
		die("Writing setup failed");

	/* Copy the kernel code */
	crc = partial_crc32(kernel, sz, crc);
	if (fwrite(kernel, 1, sz, dest) != sz)
		die("Writing kernel failed");

	/* Write the CRC */
	put_unaligned_le32(crc, buf);
	if (fwrite(buf, 1, 4, dest) != 4)
		die("Writing CRC failed");

	/* Catch any delayed write failures */
	if (fclose(dest))
		die("Writing image failed");

	close(fd);

	/* Everything is OK */
	return 0;
}
