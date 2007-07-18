/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1997 Martin Mares
 *  Copyright (C) 2007 H. Peter Anvin
 */

/*
 * This file builds a disk-image from two different files:
 *
 * - setup: 8086 machine code, sets up system parm
 * - system: 80386 code for actual system
 *
 * It does some checking that all files are of the correct type, and
 * just writes the result to stdout, removing headers and padding to
 * the right amount. It also writes some system data to stderr.
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
#include <sys/sysmacros.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <asm/boot.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;

#define DEFAULT_MAJOR_ROOT 0
#define DEFAULT_MINOR_ROOT 0

/* Minimal number of setup sectors */
#define SETUP_SECT_MIN 5
#define SETUP_SECT_MAX 64

/* This must be large enough to hold the entire setup */
u8 buf[SETUP_SECT_MAX*512];
int is_big_kernel;

static void die(const char * str, ...)
{
	va_list args;
	va_start(args, str);
	vfprintf(stderr, str, args);
	fputc('\n', stderr);
	exit(1);
}

static void usage(void)
{
	die("Usage: build [-b] setup system [rootdev] [> image]");
}

int main(int argc, char ** argv)
{
	unsigned int i, sz, setup_sectors;
	int c;
	u32 sys_size;
	u8 major_root, minor_root;
	struct stat sb;
	FILE *file;
	int fd;
	void *kernel;

	if (argc > 2 && !strcmp(argv[1], "-b"))
	  {
	    is_big_kernel = 1;
	    argc--, argv++;
	  }
	if ((argc < 3) || (argc > 4))
		usage();
	if (argc > 3) {
		if (!strcmp(argv[3], "CURRENT")) {
			if (stat("/", &sb)) {
				perror("/");
				die("Couldn't stat /");
			}
			major_root = major(sb.st_dev);
			minor_root = minor(sb.st_dev);
		} else if (strcmp(argv[3], "FLOPPY")) {
			if (stat(argv[3], &sb)) {
				perror(argv[3]);
				die("Couldn't stat root device.");
			}
			major_root = major(sb.st_rdev);
			minor_root = minor(sb.st_rdev);
		} else {
			major_root = 0;
			minor_root = 0;
		}
	} else {
		major_root = DEFAULT_MAJOR_ROOT;
		minor_root = DEFAULT_MINOR_ROOT;
	}
	fprintf(stderr, "Root device is (%d, %d)\n", major_root, minor_root);

	/* Copy the setup code */
	file = fopen(argv[1], "r");
	if (!file)
		die("Unable to open `%s': %m", argv[1]);
	c = fread(buf, 1, sizeof(buf), file);
	if (ferror(file))
		die("read-error on `setup'");
	if (c < 1024)
		die("The setup must be at least 1024 bytes");
	if (buf[510] != 0x55 || buf[511] != 0xaa)
		die("Boot block hasn't got boot flag (0xAA55)");
	fclose(file);

	/* Pad unused space with zeros */
	setup_sectors = (c + 511) / 512;
	if (setup_sectors < SETUP_SECT_MIN)
		setup_sectors = SETUP_SECT_MIN;
	i = setup_sectors*512;
	memset(buf+c, 0, i-c);

	/* Set the default root device */
	buf[508] = minor_root;
	buf[509] = major_root;

	fprintf(stderr, "Setup is %d bytes (padded to %d bytes).\n", c, i);

	/* Open and stat the kernel file */
	fd = open(argv[2], O_RDONLY);
	if (fd < 0)
		die("Unable to open `%s': %m", argv[2]);
	if (fstat(fd, &sb))
		die("Unable to stat `%s': %m", argv[2]);
	sz = sb.st_size;
	fprintf (stderr, "System is %d kB\n", (sz+1023)/1024);
	kernel = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
	if (kernel == MAP_FAILED)
		die("Unable to mmap '%s': %m", argv[2]);
	sys_size = (sz + 15) / 16;
	if (!is_big_kernel && sys_size > DEF_SYSSIZE)
		die("System is too big. Try using bzImage or modules.");

	/* Patch the setup code with the appropriate size parameters */
	buf[0x1f1] = setup_sectors-1;
	buf[0x1f4] = sys_size;
	buf[0x1f5] = sys_size >> 8;
	buf[0x1f6] = sys_size >> 16;
	buf[0x1f7] = sys_size >> 24;

	if (fwrite(buf, 1, i, stdout) != i)
		die("Writing setup failed");

	/* Copy the kernel code */
	if (fwrite(kernel, 1, sz, stdout) != sz)
		die("Writing kernel failed");
	close(fd);

	/* Everything is OK */
	return 0;
}
