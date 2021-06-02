/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __COW_H__
#define __COW_H__

#include <asm/types.h>

extern int init_cow_file(int fd, char *cow_file, char *backing_file,
			 int sectorsize, int alignment, int *bitmap_offset_out,
			 unsigned long *bitmap_len_out, int *data_offset_out);

extern int file_reader(__u64 offset, char *buf, int len, void *arg);
extern int read_cow_header(int (*reader)(__u64, char *, int, void *),
			   void *arg, __u32 *version_out,
			   char **backing_file_out, long long *mtime_out,
			   unsigned long long *size_out, int *sectorsize_out,
			   __u32 *align_out, int *bitmap_offset_out);

extern int write_cow_header(char *cow_file, int fd, char *backing_file,
			    int sectorsize, int alignment,
			    unsigned long long *size);

extern void cow_sizes(int version, __u64 size, int sectorsize, int align,
		      int bitmap_offset, unsigned long *bitmap_len_out,
		      int *data_offset_out);

#endif
