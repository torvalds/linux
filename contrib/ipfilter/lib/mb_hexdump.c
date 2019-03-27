/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: mb_hexdump.c,v 1.1.2.3 2012/07/22 08:04:24 darren_r Exp $
 */

#include "ipf.h"

void
mb_hexdump(m, fp)
	mb_t *m;
	FILE *fp;
{
	u_char *s;
	int len;
	int i;

	for (; m != NULL; m = m->mb_next) {
		len = m->mb_len;
		for (s = (u_char *)m->mb_data, i = 0; i < len; i++) {
			fprintf(fp, "%02x", *s++ & 0xff);
			if (len - i > 1) {
				i++;
				fprintf(fp, "%02x", *s++ & 0xff);
			}
			fputc(' ', fp);
		}
	}
	fputc('\n', fp);
}
