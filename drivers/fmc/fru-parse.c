// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/ipmi-fru.h>

/* Some internal helpers */
static struct fru_type_length *
__fru_get_board_tl(struct fru_common_header *header, int nr)
{
	struct fru_board_info_area *bia;
	struct fru_type_length *tl;

	bia = fru_get_board_area(header);
	tl = bia->tl;
	while (nr > 0 && !fru_is_eof(tl)) {
		tl = fru_next_tl(tl);
		nr--;
	}
	if (fru_is_eof(tl))
		return NULL;
	return tl;
}

static char *__fru_alloc_get_tl(struct fru_common_header *header, int nr)
{
	struct fru_type_length *tl;
	char *res;

	tl = __fru_get_board_tl(header, nr);
	if (!tl)
		return NULL;

	res = fru_alloc(fru_strlen(tl) + 1);
	if (!res)
		return NULL;
	return fru_strcpy(res, tl);
}

/* Public checksum verifiers */
int fru_header_cksum_ok(struct fru_common_header *header)
{
	uint8_t *ptr = (void *)header;
	int i, sum;

	for (i = sum = 0; i < sizeof(*header); i++)
		sum += ptr[i];
	return (sum & 0xff) == 0;
}
int fru_bia_cksum_ok(struct fru_board_info_area *bia)
{
	uint8_t *ptr = (void *)bia;
	int i, sum;

	for (i = sum = 0; i < 8 * bia->area_len; i++)
		sum += ptr[i];
	return (sum & 0xff) == 0;
}

/* Get various stuff, trivial */
char *fru_get_board_manufacturer(struct fru_common_header *header)
{
	return __fru_alloc_get_tl(header, 0);
}
char *fru_get_product_name(struct fru_common_header *header)
{
	return __fru_alloc_get_tl(header, 1);
}
char *fru_get_serial_number(struct fru_common_header *header)
{
	return __fru_alloc_get_tl(header, 2);
}
char *fru_get_part_number(struct fru_common_header *header)
{
	return __fru_alloc_get_tl(header, 3);
}
