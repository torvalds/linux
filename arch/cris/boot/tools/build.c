// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/tools/build.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This file builds a disk-image from three different files:
 *
 * - bootsect: exactly 512 bytes of 8086 machine code, loads the rest
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
 */

#include <stdio.h>	/* fprintf */
#include <string.h>
#include <stdlib.h>	/* contains exit */
#include <sys/types.h>	/* unistd.h needs this */
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>	/* contains read/write */
#include <fcntl.h>
#include <errno.h>

#define MINIX_HEADER 32

#define N_MAGIC_OFFSET 1024
#ifndef __BFD__
static int GCC_HEADER = sizeof(struct exec);
#endif

#ifdef __BIG_KERNEL__
#define SYS_SIZE 0xffff
#else
#define SYS_SIZE DEF_SYSSIZE
#endif

#define DEFAULT_MAJOR_ROOT 0
#define DEFAULT_MINOR_ROOT 0

/* max nr of sectors of setup: don't change unless you also change
 * bootsect etc */
#define SETUP_SECTS 4

#define STRINGIFY(x) #x

typedef union {
	int i;
	long l;
	short s[2];
	char b[4];
} conv;

long intel_long(long l)
{
	conv t;

	t.b[0] = l & 0xff; l >>= 8;
	t.b[1] = l & 0xff; l >>= 8;
	t.b[2] = l & 0xff; l >>= 8;
	t.b[3] = l & 0xff; l >>= 8;
	return t.l;
}

int intel_int(int i)
{
	conv t;

	t.b[0] = i & 0xff; i >>= 8;
        t.b[1] = i & 0xff; i >>= 8;
        t.b[2] = i & 0xff; i >>= 8;
        t.b[3] = i & 0xff; i >>= 8;
        return t.i;
}

short intel_short(short l)
{
	conv t;

	t.b[0] = l & 0xff; l >>= 8;
	t.b[1] = l & 0xff; l >>= 8;
	return t.s[0];
}

void die(const char * str)
{
	fprintf(stderr,"%s\n",str);
	exit(1);
}

void usage(void)
{
	die("Usage: build bootsect setup system [rootdev] [> image]");
}

int main(int argc, char ** argv)
{
	int i,c,id,sz,tmp_int;
	unsigned long sys_size, tmp_long;
	char buf[1024];
#ifndef __BFD__
	struct exec *ex = (struct exec *)buf;
#endif
	char major_root, minor_root;
	struct stat sb;
	unsigned char setup_sectors;

	if ((argc < 4) || (argc > 5))
		usage();
	if (argc > 4) {
		if (!strcmp(argv[4], "CURRENT")) {
			if (stat("/", &sb)) {
				perror("/");
				die("Couldn't stat /");
			}
			major_root = major(sb.st_dev);
			minor_root = minor(sb.st_dev);
		} else if (strcmp(argv[4], "FLOPPY")) {
			if (stat(argv[4], &sb)) {
				perror(argv[4]);
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
	for (i=0;i<sizeof buf; i++) buf[i]=0;
	if ((id=open(argv[1],O_RDONLY,0))<0)
		die("Unable to open 'boot'");
	if (read(id,buf,MINIX_HEADER) != MINIX_HEADER)
		die("Unable to read header of 'boot'");
	if (((long *) buf)[0]!=intel_long(0x04100301))
		die("Non-Minix header of 'boot'");
	if (((long *) buf)[1]!=intel_long(MINIX_HEADER))
		die("Non-Minix header of 'boot'");
	if (((long *) buf)[3] != 0)
		die("Illegal data segment in 'boot'");
	if (((long *) buf)[4] != 0)
		die("Illegal bss in 'boot'");
	if (((long *) buf)[5] != 0)
		die("Non-Minix header of 'boot'");
	if (((long *) buf)[7] != 0)
		die("Illegal symbol table in 'boot'");
	i=read(id,buf,sizeof buf);
	fprintf(stderr,"Boot sector %d bytes.\n",i);
	if (i != 512)
		die("Boot block must be exactly 512 bytes");
	if ((*(unsigned short *)(buf+510)) != (unsigned short)intel_short(0xAA55))
		die("Boot block hasn't got boot flag (0xAA55)");
	buf[508] = (char) minor_root;
	buf[509] = (char) major_root;	
	i=write(1,buf,512);
	if (i!=512)
		die("Write call failed");
	close (id);
	
	if ((id=open(argv[2],O_RDONLY,0))<0)
		die("Unable to open 'setup'");
	if (read(id,buf,MINIX_HEADER) != MINIX_HEADER)
		die("Unable to read header of 'setup'");
	if (((long *) buf)[0]!=intel_long(0x04100301))
		die("Non-Minix header of 'setup'");
	if (((long *) buf)[1]!=intel_long(MINIX_HEADER))
		die("Non-Minix header of 'setup'");
	if (((long *) buf)[3] != 0)
		die("Illegal data segment in 'setup'");
	if (((long *) buf)[4] != 0)
		die("Illegal bss in 'setup'");
	if (((long *) buf)[5] != 0)
		die("Non-Minix header of 'setup'");
	if (((long *) buf)[7] != 0)
		die("Illegal symbol table in 'setup'");
	for (i=0 ; (c=read(id,buf,sizeof buf))>0 ; i+=c )
#ifdef __BIG_KERNEL__
	{
		if (!i) {
			/* Working with memcpy because of alignment constraints
			   on Sparc - Gertjan */
			memcpy(&tmp_long, &buf[2], sizeof(long));
			if (tmp_long != intel_long(0x53726448) )
				die("Wrong magic in loader header of 'setup'");
			memcpy(&tmp_int, &buf[6], sizeof(int));
			if (tmp_int < intel_int(0x200))
				die("Wrong version of loader header of 'setup'");
			buf[0x11] = 1; /* LOADED_HIGH */
			tmp_long = intel_long(0x100000);
			memcpy(&buf[0x14], &tmp_long, sizeof(long));  /* code32_start */
		}
#endif
		if (write(1,buf,c)!=c)
			die("Write call failed");
#ifdef __BIG_KERNEL__
	}
#endif
	if (c != 0)
		die("read-error on 'setup'");
	close (id);
	setup_sectors = (unsigned char)((i + 511) / 512);
	/* for compatibility with LILO */
	if (setup_sectors < SETUP_SECTS)
		setup_sectors = SETUP_SECTS;
	fprintf(stderr,"Setup is %d bytes.\n",i);
	for (c=0 ; c<sizeof(buf) ; c++)
		buf[c] = '\0';
	while (i < setup_sectors * 512) {
		c = setup_sectors * 512 - i;
		if (c > sizeof(buf))
			c = sizeof(buf);
		if (write(1,buf,c) != c)
			die("Write call failed");
		i += c;
	}
	
	if ((id=open(argv[3],O_RDONLY,0))<0)
		die("Unable to open 'system'");
#ifndef __BFD__
	if (read(id,buf,GCC_HEADER) != GCC_HEADER)
		die("Unable to read header of 'system'");
	if (N_MAGIC(*ex) == ZMAGIC) {
		GCC_HEADER = N_MAGIC_OFFSET;
		lseek(id, GCC_HEADER, SEEK_SET);
	} else if (N_MAGIC(*ex) != QMAGIC)
		die("Non-GCC header of 'system'");
	fprintf(stderr,"System is %d kB (%d kB code, %d kB data and %d kB bss)\n",
		(ex->a_text+ex->a_data+ex->a_bss)/1024,
		ex->a_text /1024,
		ex->a_data /1024,
		ex->a_bss  /1024);
	sz = N_SYMOFF(*ex) - GCC_HEADER + 4;
#else
	if (fstat (id, &sb)) {
	  perror ("fstat");
	  die ("Unable to stat 'system'");
	}
	sz = sb.st_size;
	fprintf (stderr, "System is %d kB\n", sz/1024);
#endif
	sys_size = (sz + 15) / 16;
	if (sys_size > SYS_SIZE)
		die("System is too big");
	while (sz > 0) {
		int l, n;

		l = sz;
		if (l > sizeof(buf))
			l = sizeof(buf);
		if ((n=read(id, buf, l)) != l) {
			if (n == -1) 
				perror(argv[1]);
			else
				fprintf(stderr, "Unexpected EOF\n");
			die("Can't read 'system'");
		}
		if (write(1, buf, l) != l)
			die("Write failed");
		sz -= l;
	}
	close(id);
	if (lseek(1, 497, 0) == 497) {
		if (write(1, &setup_sectors, 1) != 1)
			die("Write of setup sectors failed");
	}
	if (lseek(1,500,0) == 500) {
		buf[0] = (sys_size & 0xff);
		buf[1] = ((sys_size >> 8) & 0xff);
		if (write(1, buf, 2) != 2)
			die("Write failed");
	}
	return(0);
}
