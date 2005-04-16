/*
 * Copyright (C) 2002 Mark Debbage (Mark.Debbage@superh.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <asm/string.h>

// This is a simplistic optimization of memcpy to increase the
// granularity of access beyond one byte using aligned
// loads and stores. This is not an optimal implementation
// for SH-5 (especially with regard to prefetching and the cache),
// and a better version should be provided later ...

void *memcpy(void *dest, const void *src, size_t count)
{
	char *d = (char *) dest, *s = (char *) src;

	if (count >= 32) {
		int i = 8 - (((unsigned long) d) & 0x7);

		if (i != 8)
			while (i-- && count--) {
				*d++ = *s++;
			}

		if (((((unsigned long) d) & 0x7) == 0) &&
		    ((((unsigned long) s) & 0x7) == 0)) {
			while (count >= 32) {
				unsigned long long t1, t2, t3, t4;
				t1 = *(unsigned long long *) (s);
				t2 = *(unsigned long long *) (s + 8);
				t3 = *(unsigned long long *) (s + 16);
				t4 = *(unsigned long long *) (s + 24);
				*(unsigned long long *) (d) = t1;
				*(unsigned long long *) (d + 8) = t2;
				*(unsigned long long *) (d + 16) = t3;
				*(unsigned long long *) (d + 24) = t4;
				d += 32;
				s += 32;
				count -= 32;
			}
			while (count >= 8) {
				*(unsigned long long *) d =
				    *(unsigned long long *) s;
				d += 8;
				s += 8;
				count -= 8;
			}
		}

		if (((((unsigned long) d) & 0x3) == 0) &&
		    ((((unsigned long) s) & 0x3) == 0)) {
			while (count >= 4) {
				*(unsigned long *) d = *(unsigned long *) s;
				d += 4;
				s += 4;
				count -= 4;
			}
		}

		if (((((unsigned long) d) & 0x1) == 0) &&
		    ((((unsigned long) s) & 0x1) == 0)) {
			while (count >= 2) {
				*(unsigned short *) d = *(unsigned short *) s;
				d += 2;
				s += 2;
				count -= 2;
			}
		}
	}

	while (count--) {
		*d++ = *s++;
	}

	return d;
}
