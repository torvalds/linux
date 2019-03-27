/*  tables.c - tables serialization code
 *
 *  Copyright (c) 1990 The Regents of the University of California.
 *  All rights reserved.
 *
 *  This code is derived from software contributed to Berkeley by
 *  Vern Paxson.
 *
 *  The United States Government has rights in this work pursuant
 *  to contract no. DE-AC03-76SF00098 between the United States
 *  Department of Energy and the University of California.
 *
 *  This file is part of flex.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  Neither the name of the University nor the names of its contributors
 *  may be used to endorse or promote products derived from this software
 *  without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 *  IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 *  PURPOSE.
 */


#include "flexdef.h"
#include "tables.h"

/** Convert size_t to t_flag.
 *  @param n in {1,2,4}
 *  @return YYTD_DATA*. 
 */
#define BYTES2TFLAG(n)\
    (((n) == sizeof(flex_int8_t))\
        ? YYTD_DATA8\
        :(((n)== sizeof(flex_int16_t))\
            ? YYTD_DATA16\
            : YYTD_DATA32))

/** Clear YYTD_DATA* bit flags
 * @return the flag with the YYTD_DATA* bits cleared
 */
#define TFLAGS_CLRDATA(flg) ((flg) & ~(YYTD_DATA8 | YYTD_DATA16 | YYTD_DATA32))

int     yytbl_write32 (struct yytbl_writer *wr, flex_uint32_t v);
int     yytbl_write16 (struct yytbl_writer *wr, flex_uint16_t v);
int     yytbl_write8 (struct yytbl_writer *wr, flex_uint8_t v);
int     yytbl_writen (struct yytbl_writer *wr, void *v, flex_int32_t len);
static flex_int32_t yytbl_data_geti (const struct yytbl_data *tbl, int i);
/* XXX Not used
static flex_int32_t yytbl_data_getijk (const struct yytbl_data *tbl, int i,
				  int j, int k);
 */


/** Initialize the table writer.
 *  @param wr an uninitialized writer
 *  @param the output file
 *  @return 0 on success
 */
int yytbl_writer_init (struct yytbl_writer *wr, FILE * out)
{
	wr->out = out;
	wr->total_written = 0;
	return 0;
}

/** Initialize a table header.
 *  @param th  The uninitialized structure
 *  @param version_str the  version string
 *  @param name the name of this table set
 */
int yytbl_hdr_init (struct yytbl_hdr *th, const char *version_str,
		    const char *name)
{
	memset (th, 0, sizeof (struct yytbl_hdr));

	th->th_magic = YYTBL_MAGIC;
	th->th_hsize = 14 + strlen (version_str) + 1 + strlen (name) + 1;
	th->th_hsize += yypad64 (th->th_hsize);
	th->th_ssize = 0;	// Not known at this point.
	th->th_flags = 0;
	th->th_version = copy_string (version_str);
	th->th_name = copy_string (name);
	return 0;
}

/** Allocate and initialize a table data structure.
 *  @param tbl a pointer to an uninitialized table
 *  @param id  the table identifier
 *  @return 0 on success
 */
int yytbl_data_init (struct yytbl_data *td, enum yytbl_id id)
{

	memset (td, 0, sizeof (struct yytbl_data));
	td->td_id = id;
	td->td_flags = YYTD_DATA32;
	return 0;
}

/** Clean up table and data array.
 *  @param td will be destroyed
 *  @return 0 on success
 */
int yytbl_data_destroy (struct yytbl_data *td)
{
	if (td->td_data)
		free (td->td_data);
	td->td_data = 0;
	free (td);
	return 0;
}

/** Write enough padding to bring the file pointer to a 64-bit boundary. */
static int yytbl_write_pad64 (struct yytbl_writer *wr)
{
	int     pad, bwritten = 0;

	pad = yypad64 (wr->total_written);
	while (pad-- > 0)
		if (yytbl_write8 (wr, 0) < 0)
			return -1;
		else
			bwritten++;
	return bwritten;
}

/** write the header.
 *  @param out the output stream
 *  @param th table header to be written
 *  @return -1 on error, or bytes written on success.
 */
int yytbl_hdr_fwrite (struct yytbl_writer *wr, const struct yytbl_hdr *th)
{
	int  sz, rv;
	int     bwritten = 0;

	if (yytbl_write32 (wr, th->th_magic) < 0
	    || yytbl_write32 (wr, th->th_hsize) < 0)
		flex_die (_("th_magic|th_hsize write32 failed"));
	bwritten += 8;

	if (fgetpos (wr->out, &(wr->th_ssize_pos)) != 0)
		flex_die (_("fgetpos failed"));

	if (yytbl_write32 (wr, th->th_ssize) < 0
	    || yytbl_write16 (wr, th->th_flags) < 0)
		flex_die (_("th_ssize|th_flags write failed"));
	bwritten += 6;

	sz = strlen (th->th_version) + 1;
	if ((rv = yytbl_writen (wr, th->th_version, sz)) != sz)
		flex_die (_("th_version writen failed"));
	bwritten += rv;

	sz = strlen (th->th_name) + 1;
	if ((rv = yytbl_writen (wr, th->th_name, sz)) != sz)
		flex_die (_("th_name writen failed"));
	bwritten += rv;

	/* add padding */
	if ((rv = yytbl_write_pad64 (wr)) < 0)
		flex_die (_("pad64 failed"));
	bwritten += rv;

	/* Sanity check */
	if (bwritten != (int) th->th_hsize)
		flex_die (_("pad64 failed"));

	return bwritten;
}


/** Write this table.
 *  @param out the file writer
 *  @param td table data to be written
 *  @return -1 on error, or bytes written on success.
 */
int yytbl_data_fwrite (struct yytbl_writer *wr, struct yytbl_data *td)
{
	int  rv;
	flex_int32_t bwritten = 0;
	flex_int32_t i, total_len;
	fpos_t  pos;

	if ((rv = yytbl_write16 (wr, td->td_id)) < 0)
		return -1;
	bwritten += rv;

	if ((rv = yytbl_write16 (wr, td->td_flags)) < 0)
		return -1;
	bwritten += rv;

	if ((rv = yytbl_write32 (wr, td->td_hilen)) < 0)
		return -1;
	bwritten += rv;

	if ((rv = yytbl_write32 (wr, td->td_lolen)) < 0)
		return -1;
	bwritten += rv;

	total_len = yytbl_calc_total_len (td);
	for (i = 0; i < total_len; i++) {
		switch (YYTDFLAGS2BYTES (td->td_flags)) {
		case sizeof (flex_int8_t):
			rv = yytbl_write8 (wr, yytbl_data_geti (td, i));
			break;
		case sizeof (flex_int16_t):
			rv = yytbl_write16 (wr, yytbl_data_geti (td, i));
			break;
		case sizeof (flex_int32_t):
			rv = yytbl_write32 (wr, yytbl_data_geti (td, i));
			break;
		default:
			flex_die (_("invalid td_flags detected"));
		}
		if (rv < 0) {
			flex_die (_("error while writing tables"));
			return -1;
		}
		bwritten += rv;
	}

	/* Sanity check */
	if (bwritten != (int) (12 + total_len * YYTDFLAGS2BYTES (td->td_flags))) {
		flex_die (_("insanity detected"));
		return -1;
	}

	/* add padding */
	if ((rv = yytbl_write_pad64 (wr)) < 0) {
		flex_die (_("pad64 failed"));
		return -1;
	}
	bwritten += rv;

	/* Now go back and update the th_hsize member */
	if (fgetpos (wr->out, &pos) != 0
	    || fsetpos (wr->out, &(wr->th_ssize_pos)) != 0
	    || yytbl_write32 (wr, wr->total_written) < 0
	    || fsetpos (wr->out, &pos)) {
		flex_die (_("get|set|fwrite32 failed"));
		return -1;
	}
	else
		/* Don't count the int we just wrote. */
		wr->total_written -= sizeof (flex_int32_t);
	return bwritten;
}

/** Write n bytes.
 *  @param  wr   the table writer
 *  @param  v    data to be written
 *  @param  len  number of bytes
 *  @return  -1 on error. number of bytes written on success.
 */
int yytbl_writen (struct yytbl_writer *wr, void *v, flex_int32_t len)
{
	int  rv;

	rv = fwrite (v, 1, len, wr->out);
	if (rv != len)
		return -1;
	wr->total_written += len;
	return len;
}

/** Write four bytes in network byte order
 *  @param  wr  the table writer
 *  @param  v    a dword in host byte order
 *  @return  -1 on error. number of bytes written on success.
 */
int yytbl_write32 (struct yytbl_writer *wr, flex_uint32_t v)
{
	flex_uint32_t vnet;
	size_t  bytes, rv;

	vnet = htonl (v);
	bytes = sizeof (flex_uint32_t);
	rv = fwrite (&vnet, bytes, 1, wr->out);
	if (rv != 1)
		return -1;
	wr->total_written += bytes;
	return bytes;
}

/** Write two bytes in network byte order.
 *  @param  wr  the table writer
 *  @param  v    a word in host byte order
 *  @return  -1 on error. number of bytes written on success.
 */
int yytbl_write16 (struct yytbl_writer *wr, flex_uint16_t v)
{
	flex_uint16_t vnet;
	size_t  bytes, rv;

	vnet = htons (v);
	bytes = sizeof (flex_uint16_t);
	rv = fwrite (&vnet, bytes, 1, wr->out);
	if (rv != 1)
		return -1;
	wr->total_written += bytes;
	return bytes;
}

/** Write a byte.
 *  @param  wr  the table writer
 *  @param  v    the value to be written
 *  @return  -1 on error. number of bytes written on success.
 */
int yytbl_write8 (struct yytbl_writer *wr, flex_uint8_t v)
{
	size_t  bytes, rv;

	bytes = sizeof (flex_uint8_t);
	rv = fwrite (&v, bytes, 1, wr->out);
	if (rv != 1)
		return -1;
	wr->total_written += bytes;
	return bytes;
}


/* XXX Not Used */
#if 0
/** Extract data element [i][j] from array data tables. 
 * @param tbl data table
 * @param i index into higher dimension array. i should be zero for one-dimensional arrays.
 * @param j index into lower dimension array.
 * @param k index into struct, must be 0 or 1. Only valid for YYTD_ID_TRANSITION table
 * @return data[i][j + k]
 */
static flex_int32_t yytbl_data_getijk (const struct yytbl_data *tbl, int i,
				  int j, int k)
{
	flex_int32_t lo;

	k %= 2;
	lo = tbl->td_lolen;

	switch (YYTDFLAGS2BYTES (tbl->td_flags)) {
	case sizeof (flex_int8_t):
		return ((flex_int8_t *) (tbl->td_data))[(i * lo + j) * (k + 1) +
						   k];
	case sizeof (flex_int16_t):
		return ((flex_int16_t *) (tbl->td_data))[(i * lo + j) * (k +
								    1) +
						    k];
	case sizeof (flex_int32_t):
		return ((flex_int32_t *) (tbl->td_data))[(i * lo + j) * (k +
								    1) +
						    k];
	default:
		flex_die (_("invalid td_flags detected"));
		break;
	}

	return 0;
}
#endif /* Not used */

/** Extract data element [i] from array data tables treated as a single flat array of integers.
 * Be careful for 2-dimensional arrays or for YYTD_ID_TRANSITION, which is an array
 * of structs. 
 * @param tbl data table
 * @param i index into array.
 * @return data[i]
 */
static flex_int32_t yytbl_data_geti (const struct yytbl_data *tbl, int i)
{

	switch (YYTDFLAGS2BYTES (tbl->td_flags)) {
	case sizeof (flex_int8_t):
		return ((flex_int8_t *) (tbl->td_data))[i];
	case sizeof (flex_int16_t):
		return ((flex_int16_t *) (tbl->td_data))[i];
	case sizeof (flex_int32_t):
		return ((flex_int32_t *) (tbl->td_data))[i];
	default:
		flex_die (_("invalid td_flags detected"));
		break;
	}
	return 0;
}

/** Set data element [i] in array data tables treated as a single flat array of integers.
 * Be careful for 2-dimensional arrays or for YYTD_ID_TRANSITION, which is an array
 * of structs. 
 * @param tbl data table
 * @param i index into array.
 * @param newval new value for data[i]
 */
static void yytbl_data_seti (const struct yytbl_data *tbl, int i,
			     flex_int32_t newval)
{

	switch (YYTDFLAGS2BYTES (tbl->td_flags)) {
	case sizeof (flex_int8_t):
		((flex_int8_t *) (tbl->td_data))[i] = (flex_int8_t) newval;
		break;
	case sizeof (flex_int16_t):
		((flex_int16_t *) (tbl->td_data))[i] = (flex_int16_t) newval;
		break;
	case sizeof (flex_int32_t):
		((flex_int32_t *) (tbl->td_data))[i] = (flex_int32_t) newval;
		break;
	default:
		flex_die (_("invalid td_flags detected"));
		break;
	}
}

/** Calculate the number of bytes  needed to hold the largest
 *  absolute value in this data array.
 *  @param tbl  the data table
 *  @return sizeof(n) where n in {flex_int8_t, flex_int16_t, flex_int32_t}
 */
static size_t min_int_size (struct yytbl_data *tbl)
{
	flex_uint32_t i, total_len;
	flex_int32_t max = 0;

	total_len = yytbl_calc_total_len (tbl);

	for (i = 0; i < total_len; i++) {
		flex_int32_t n;

		n = abs (yytbl_data_geti (tbl, i));

		if (n > max)
			max = n;
	}

	if (max <= INT8_MAX)
		return sizeof (flex_int8_t);
	else if (max <= INT16_MAX)
		return sizeof (flex_int16_t);
	else
		return sizeof (flex_int32_t);
}

/** Transform data to smallest possible of (int32, int16, int8).
 * For example, we may have generated an int32 array due to user options
 * (e.g., %option align), but if the maximum value in that array
 * is 80 (for example), then we can serialize it with only 1 byte per int.
 * This is NOT the same as compressed DFA tables. We're just trying
 * to save storage space here.
 *
 * @param tbl the table to be compressed
 */
void yytbl_data_compress (struct yytbl_data *tbl)
{
	flex_int32_t i, newsz, total_len;
	struct yytbl_data newtbl;

	yytbl_data_init (&newtbl, tbl->td_id);
	newtbl.td_hilen = tbl->td_hilen;
	newtbl.td_lolen = tbl->td_lolen;
	newtbl.td_flags = tbl->td_flags;

	newsz = min_int_size (tbl);


	if (newsz == (int) YYTDFLAGS2BYTES (tbl->td_flags))
		/* No change in this table needed. */
		return;

	if (newsz > (int) YYTDFLAGS2BYTES (tbl->td_flags)) {
		flex_die (_("detected negative compression"));
		return;
	}

	total_len = yytbl_calc_total_len (tbl);
	newtbl.td_data = calloc (total_len, newsz);
	newtbl.td_flags =
		TFLAGS_CLRDATA (newtbl.td_flags) | BYTES2TFLAG (newsz);

	for (i = 0; i < total_len; i++) {
		flex_int32_t g;

		g = yytbl_data_geti (tbl, i);
		yytbl_data_seti (&newtbl, i, g);
	}


	/* Now copy over the old table */
	free (tbl->td_data);
	*tbl = newtbl;
}

/* vim:set noexpandtab cindent tabstop=8 softtabstop=0 shiftwidth=8 textwidth=0: */
