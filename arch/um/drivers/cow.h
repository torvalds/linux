#ifndef __COW_H__
#define __COW_H__

#include <asm/types.h>

#if __BYTE_ORDER == __BIG_ENDIAN
# define ntohll(x) (x)
# define htonll(x) (x)
#elif __BYTE_ORDER == __LITTLE_ENDIAN
# define ntohll(x)  bswap_64(x)
# define htonll(x)  bswap_64(x)
#else
#error "__BYTE_ORDER not defined"
#endif

extern int init_cow_file(int fd, char *cow_file, char *backing_file,
			 int sectorsize, int alignment, int *bitmap_offset_out,
			 unsigned long *bitmap_len_out, int *data_offset_out);

extern int file_reader(__u64 offset, char *buf, int len, void *arg);
extern int read_cow_header(int (*reader)(__u64, char *, int, void *),
			   void *arg, __u32 *version_out,
			   char **backing_file_out, time_t *mtime_out,
			   unsigned long long *size_out, int *sectorsize_out,
			   __u32 *align_out, int *bitmap_offset_out);

extern int write_cow_header(char *cow_file, int fd, char *backing_file,
			    int sectorsize, int alignment,
			    unsigned long long *size);

extern void cow_sizes(int version, __u64 size, int sectorsize, int align,
		      int bitmap_offset, unsigned long *bitmap_len_out,
		      int *data_offset_out);

#endif

/*
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
