// SPDX-License-Identifier: GPL-2.0
#include <linux/zlib.h>

/* bits taken from ppc */

extern void *avail_ram, *end_avail;

void exit (void)
{
  for (;;);
}

void *zalloc(unsigned size)
{
        void *p = avail_ram;

        size = (size + 7) & -8;
        avail_ram += size;
        if (avail_ram > end_avail) {
                //puts("oops... out of memory\n");
                //pause();
                exit ();
        }
        return p;
}

#define HEAD_CRC        2
#define EXTRA_FIELD     4
#define ORIG_NAME       8
#define COMMENT         0x10
#define RESERVED        0xe0

#define DEFLATED        8

void gunzip (void *dst, int dstlen, unsigned char *src, int *lenp)
{
	z_stream s;
	int r, i, flags;

        /* skip header */
        i = 10;
        flags = src[3];
        if (src[2] != DEFLATED || (flags & RESERVED) != 0) {
                //puts("bad gzipped data\n");
                exit();
        }
        if ((flags & EXTRA_FIELD) != 0)
                i = 12 + src[10] + (src[11] << 8);
        if ((flags & ORIG_NAME) != 0)
                while (src[i++] != 0)
                        ;
        if ((flags & COMMENT) != 0)
                while (src[i++] != 0)
                        ;
        if ((flags & HEAD_CRC) != 0)
                i += 2;
        if (i >= *lenp) {
                //puts("gunzip: ran out of data in header\n");
                exit();
        }

	s.workspace = zalloc(zlib_inflate_workspacesize());
        r = zlib_inflateInit2(&s, -MAX_WBITS);
        if (r != Z_OK) {
                //puts("inflateInit2 returned "); puthex(r); puts("\n");
                exit();
        }
        s.next_in = src + i;
        s.avail_in = *lenp - i;
        s.next_out = dst;
        s.avail_out = dstlen;
        r = zlib_inflate(&s, Z_FINISH);
        if (r != Z_OK && r != Z_STREAM_END) {
                //puts("inflate returned "); puthex(r); puts("\n");
                exit();
        }
        *lenp = s.next_out - (unsigned char *) dst;
        zlib_inflateEnd(&s);
}

