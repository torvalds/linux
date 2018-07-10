/*
 smemcap - a tool for meaningful memory reporting

 Copyright 2008-2009 Matt Mackall <mpm@selenic.com>

 This software may be used and distributed according to the terms of
 the GNU General Public License version 2 or later, incorporated
 herein by reference.
*/
//config:config SMEMCAP
//config:	bool "smemcap (2.5 kb)"
//config:	default y
//config:	help
//config:	smemcap is a tool for capturing process data for smem,
//config:	a memory usage statistic tool.

//applet:IF_SMEMCAP(APPLET(smemcap, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SMEMCAP) += smemcap.o

#include "libbb.h"
#include "bb_archive.h"

struct fileblock {
	struct fileblock *next;
	char data[TAR_BLOCK_SIZE];
};

static void writeheader(const char *path, struct stat *sb, int type)
{
	struct tar_header_t header;
	int i, sum;

	memset(&header, 0, TAR_BLOCK_SIZE);
	strcpy(header.name, path);
	sprintf(header.mode, "%o", sb->st_mode & 0777);
	/* careful to not overflow fields! */
	sprintf(header.uid, "%o", sb->st_uid & 07777777);
	sprintf(header.gid, "%o", sb->st_gid & 07777777);
	sprintf(header.size, "%o", (unsigned)sb->st_size);
	sprintf(header.mtime, "%llo", sb->st_mtime & 077777777777LL);
	header.typeflag = type;
	strcpy(header.magic, "ustar  "); /* like GNU tar */

	/* Calculate and store the checksum (the sum of all of the bytes of
	 * the header). The checksum field must be filled with blanks for the
	 * calculation. The checksum field is formatted differently from the
	 * other fields: it has 6 digits, a NUL, then a space -- rather than
	 * digits, followed by a NUL like the other fields... */
	header.chksum[7] = ' ';
	sum = ' ' * 7;
	for (i = 0; i < TAR_BLOCK_SIZE; i++)
		sum += ((unsigned char*)&header)[i];
	sprintf(header.chksum, "%06o", sum);

	xwrite(STDOUT_FILENO, &header, TAR_BLOCK_SIZE);
}

static void archivefile(const char *path)
{
	struct fileblock *start, *cur;
	struct fileblock **prev = &start;
	int fd, r;
	unsigned size = 0;
	struct stat s;

	/* buffer the file */
	fd = xopen(path, O_RDONLY);
	do {
		cur = xzalloc(sizeof(*cur));
		*prev = cur;
		prev = &cur->next;
		r = full_read(fd, cur->data, TAR_BLOCK_SIZE);
		if (r > 0)
			size += r;
	} while (r == TAR_BLOCK_SIZE);

	/* write archive header */
	fstat(fd, &s);
	close(fd);
	s.st_size = size;
	writeheader(path, &s, '0');

	/* dump file contents */
	for (cur = start; (int)size > 0; size -= TAR_BLOCK_SIZE) {
		xwrite(STDOUT_FILENO, cur->data, TAR_BLOCK_SIZE);
		start = cur;
		cur = cur->next;
		free(start);
	}
}

static void archivejoin(const char *sub, const char *name)
{
	char path[sizeof(long long)*3 + sizeof("/cmdline")];
	sprintf(path, "%s/%s", sub, name);
	archivefile(path);
}

//usage:#define smemcap_trivial_usage ">SMEMDATA.TAR"
//usage:#define smemcap_full_usage "\n\n"
//usage:       "Collect memory usage data in /proc and write it to stdout"

int smemcap_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int smemcap_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	DIR *d;
	struct dirent *de;

	xchdir("/proc");
	d = xopendir(".");

	archivefile("meminfo");
	archivefile("version");
	while ((de = readdir(d)) != NULL) {
		if (isdigit(de->d_name[0])) {
			struct stat s;
			memset(&s, 0, sizeof(s));
			s.st_mode = 0555;
			writeheader(de->d_name, &s, '5');
			archivejoin(de->d_name, "smaps");
			archivejoin(de->d_name, "cmdline");
			archivejoin(de->d_name, "stat");
		}
	}

	if (ENABLE_FEATURE_CLEAN_UP)
		closedir(d);

	return EXIT_SUCCESS;
}
