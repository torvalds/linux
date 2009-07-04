/*
   Simple utility to make a single-image install kernel with initial ramdisk
   for Sparc tftpbooting without need to set up nfs.

   Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
   Pete Zaitcev <zaitcev@yahoo.com> endian fixes for cross-compiles, 2000.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */
   
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

/*
 * Note: run this on an a.out kernel (use elftoaout for it),
 * as PROM looks for a.out image only.
 */

unsigned short ld2(char *p)
{
	return (p[0] << 8) | p[1];
}

unsigned int ld4(char *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

void st4(char *p, unsigned int x)
{
	p[0] = x >> 24;
	p[1] = x >> 16;
	p[2] = x >> 8;
	p[3] = x;
}

void usage(void)
{
	/* fs_img.gz is an image of initial ramdisk. */
	fprintf(stderr, "Usage: piggyback vmlinux.aout System.map fs_img.gz\n");
	fprintf(stderr, "\tKernel image will be modified in place.\n");
	exit(1);
}

void die(char *str)
{
	perror (str);
	exit(1);
}

int main(int argc,char **argv)
{
	static char aout_magic[] = { 0x01, 0x03, 0x01, 0x07 };
	char buffer[1024], *q, *r;
	unsigned int i, j, k, start, end, offset;
	FILE *map;
	struct stat s;
	int image, tail;

	if (argc != 4) usage();
	start = end = 0;
	if (stat (argv[3], &s) < 0) die (argv[3]);
	map = fopen (argv[2], "r");
	if (!map) die(argv[2]);
	while (fgets (buffer, 1024, map)) {
		if (!strcmp (buffer + 8, " T start\n") || !strcmp (buffer + 16, " T start\n"))
			start = strtoul (buffer, NULL, 16);
		else if (!strcmp (buffer + 8, " A _end\n") || !strcmp (buffer + 16, " A _end\n"))
			end = strtoul (buffer, NULL, 16);
	}
	fclose (map);
	if (!start || !end) {
		fprintf (stderr, "Could not determine start and end from System.map\n");
		exit(1);
	}
	if ((image = open(argv[1],O_RDWR)) < 0) die(argv[1]);
	if (read(image,buffer,512) != 512) die(argv[1]);
	if (memcmp (buffer, "\177ELF", 4) == 0) {
		q = buffer + ld4(buffer + 28);
		i = ld4(q + 4) + ld4(buffer + 24) - ld4(q + 8);
		if (lseek(image,i,0) < 0) die("lseek");
		if (read(image,buffer,512) != 512) die(argv[1]);
		j = 0;
	} else if (memcmp(buffer, aout_magic, 4) == 0) {
		i = j = 32;
	} else {
		fprintf (stderr, "Not ELF nor a.out. Don't blame me.\n");
		exit(1);
	}
	k = i;
	i += (ld2(buffer + j + 2)<<2) - 512;
	if (lseek(image,i,0) < 0) die("lseek");
	if (read(image,buffer,1024) != 1024) die(argv[1]);
	for (q = buffer, r = q + 512; q < r; q += 4) {
		if (*q == 'H' && q[1] == 'd' && q[2] == 'r' && q[3] == 'S')
			break;
	}
	if (q == r) {
		fprintf (stderr, "Couldn't find headers signature in the kernel.\n");
		exit(1);
	}
	offset = i + (q - buffer) + 10;
	if (lseek(image, offset, 0) < 0) die ("lseek");

	st4(buffer, 0);
	st4(buffer + 4, 0x01000000);
	st4(buffer + 8, (end + 32 + 4095) & ~4095);
	st4(buffer + 12, s.st_size);

	if (write(image,buffer+2,14) != 14) die (argv[1]);
	if (lseek(image, k - start + ((end + 32 + 4095) & ~4095), 0) < 0) die ("lseek");
	if ((tail = open(argv[3],O_RDONLY)) < 0) die(argv[3]);
	while ((i = read (tail,buffer,1024)) > 0)
		if (write(image,buffer,i) != i) die (argv[1]);
	if (close(image) < 0) die("close");
	if (close(tail) < 0) die("close");
    	return 0;
}
