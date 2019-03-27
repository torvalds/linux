/*
 * Copyright (C) 2004-2008  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2002  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: buffer.c,v 1.49 2008/09/25 04:02:39 tbox Exp $ */

/*! \file */

#include <config.h>

#include <isc/buffer.h>
#include <isc/mem.h>
#include <isc/region.h>
#include <isc/string.h>
#include <isc/util.h>

void
isc__buffer_init(isc_buffer_t *b, const void *base, unsigned int length) {
	/*
	 * Make 'b' refer to the 'length'-byte region starting at 'base'.
	 * XXXDCL see the comment in buffer.h about base being const.
	 */

	REQUIRE(b != NULL);

	ISC__BUFFER_INIT(b, base, length);
}

void
isc__buffer_initnull(isc_buffer_t *b) {
	/*
	 * Initialize a new buffer which has no backing store.  This can
	 * later be grown as needed and swapped in place.
	 */

	ISC__BUFFER_INIT(b, NULL, 0);
}

void
isc_buffer_reinit(isc_buffer_t *b, void *base, unsigned int length) {
	/*
	 * Re-initialize the buffer enough to reconfigure the base of the
	 * buffer.  We will swap in the new buffer, after copying any
	 * data we contain into the new buffer and adjusting all of our
	 * internal pointers.
	 *
	 * The buffer must not be smaller than the length of the original
	 * buffer.
	 */
	REQUIRE(b->length <= length);
	REQUIRE(base != NULL);

	(void)memmove(base, b->base, b->length);
	b->base = base;
	b->length = length;
}

void
isc__buffer_invalidate(isc_buffer_t *b) {
	/*
	 * Make 'b' an invalid buffer.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(!ISC_LINK_LINKED(b, link));
	REQUIRE(b->mctx == NULL);

	ISC__BUFFER_INVALIDATE(b);
}

void
isc__buffer_region(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_REGION(b, r);
}

void
isc__buffer_usedregion(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the used region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_USEDREGION(b, r);
}

void
isc__buffer_availableregion(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the available region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_AVAILABLEREGION(b, r);
}

void
isc__buffer_add(isc_buffer_t *b, unsigned int n) {
	/*
	 * Increase the 'used' region of 'b' by 'n' bytes.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + n <= b->length);

	ISC__BUFFER_ADD(b, n);
}

void
isc__buffer_subtract(isc_buffer_t *b, unsigned int n) {
	/*
	 * Decrease the 'used' region of 'b' by 'n' bytes.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used >= n);

	ISC__BUFFER_SUBTRACT(b, n);
}

void
isc__buffer_clear(isc_buffer_t *b) {
	/*
	 * Make the used region empty.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));

	ISC__BUFFER_CLEAR(b);
}

void
isc__buffer_consumedregion(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the consumed region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_CONSUMEDREGION(b, r);
}

void
isc__buffer_remainingregion(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the remaining region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_REMAININGREGION(b, r);
}

void
isc__buffer_activeregion(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the active region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_ACTIVEREGION(b, r);
}

void
isc__buffer_setactive(isc_buffer_t *b, unsigned int n) {
	/*
	 * Sets the end of the active region 'n' bytes after current.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->current + n <= b->used);

	ISC__BUFFER_SETACTIVE(b, n);
}

void
isc__buffer_first(isc_buffer_t *b) {
	/*
	 * Make the consumed region empty.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));

	ISC__BUFFER_FIRST(b);
}

void
isc__buffer_forward(isc_buffer_t *b, unsigned int n) {
	/*
	 * Increase the 'consumed' region of 'b' by 'n' bytes.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->current + n <= b->used);

	ISC__BUFFER_FORWARD(b, n);
}

void
isc__buffer_back(isc_buffer_t *b, unsigned int n) {
	/*
	 * Decrease the 'consumed' region of 'b' by 'n' bytes.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(n <= b->current);

	ISC__BUFFER_BACK(b, n);
}

void
isc_buffer_compact(isc_buffer_t *b) {
	unsigned int length;
	void *src;

	/*
	 * Compact the used region by moving the remaining region so it occurs
	 * at the start of the buffer.  The used region is shrunk by the size
	 * of the consumed region, and the consumed region is then made empty.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));

	src = isc_buffer_current(b);
	length = isc_buffer_remaininglength(b);
	(void)memmove(b->base, src, (size_t)length);

	if (b->active > b->current)
		b->active -= b->current;
	else
		b->active = 0;
	b->current = 0;
	b->used = length;
}

isc_uint8_t
isc_buffer_getuint8(isc_buffer_t *b) {
	unsigned char *cp;
	isc_uint8_t result;

	/*
	 * Read an unsigned 8-bit integer from 'b' and return it.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used - b->current >= 1);

	cp = isc_buffer_current(b);
	b->current += 1;
	result = ((isc_uint8_t)(cp[0]));

	return (result);
}

void
isc__buffer_putuint8(isc_buffer_t *b, isc_uint8_t val) {
	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + 1 <= b->length);

	ISC__BUFFER_PUTUINT8(b, val);
}

isc_uint16_t
isc_buffer_getuint16(isc_buffer_t *b) {
	unsigned char *cp;
	isc_uint16_t result;

	/*
	 * Read an unsigned 16-bit integer in network byte order from 'b',
	 * convert it to host byte order, and return it.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used - b->current >= 2);

	cp = isc_buffer_current(b);
	b->current += 2;
	result = ((unsigned int)(cp[0])) << 8;
	result |= ((unsigned int)(cp[1]));

	return (result);
}

void
isc__buffer_putuint16(isc_buffer_t *b, isc_uint16_t val) {
	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + 2 <= b->length);

	ISC__BUFFER_PUTUINT16(b, val);
}

void
isc__buffer_putuint24(isc_buffer_t *b, isc_uint32_t val) {
	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + 3 <= b->length);

	ISC__BUFFER_PUTUINT24(b, val);
}

isc_uint32_t
isc_buffer_getuint32(isc_buffer_t *b) {
	unsigned char *cp;
	isc_uint32_t result;

	/*
	 * Read an unsigned 32-bit integer in network byte order from 'b',
	 * convert it to host byte order, and return it.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used - b->current >= 4);

	cp = isc_buffer_current(b);
	b->current += 4;
	result = ((unsigned int)(cp[0])) << 24;
	result |= ((unsigned int)(cp[1])) << 16;
	result |= ((unsigned int)(cp[2])) << 8;
	result |= ((unsigned int)(cp[3]));

	return (result);
}

void
isc__buffer_putuint32(isc_buffer_t *b, isc_uint32_t val) {
	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + 4 <= b->length);

	ISC__BUFFER_PUTUINT32(b, val);
}

isc_uint64_t
isc_buffer_getuint48(isc_buffer_t *b) {
	unsigned char *cp;
	isc_uint64_t result;

	/*
	 * Read an unsigned 48-bit integer in network byte order from 'b',
	 * convert it to host byte order, and return it.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used - b->current >= 6);

	cp = isc_buffer_current(b);
	b->current += 6;
	result = ((isc_int64_t)(cp[0])) << 40;
	result |= ((isc_int64_t)(cp[1])) << 32;
	result |= ((isc_int64_t)(cp[2])) << 24;
	result |= ((isc_int64_t)(cp[3])) << 16;
	result |= ((isc_int64_t)(cp[4])) << 8;
	result |= ((isc_int64_t)(cp[5]));

	return (result);
}

void
isc__buffer_putuint48(isc_buffer_t *b, isc_uint64_t val) {
	isc_uint16_t valhi;
	isc_uint32_t vallo;

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + 6 <= b->length);

	valhi = (isc_uint16_t)(val >> 32);
	vallo = (isc_uint32_t)(val & 0xFFFFFFFF);
	ISC__BUFFER_PUTUINT16(b, valhi);
	ISC__BUFFER_PUTUINT32(b, vallo);
}

void
isc__buffer_putmem(isc_buffer_t *b, const unsigned char *base,
		   unsigned int length)
{
	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + length <= b->length);

	ISC__BUFFER_PUTMEM(b, base, length);
}

void
isc__buffer_putstr(isc_buffer_t *b, const char *source) {
	size_t l;
	unsigned char *cp;

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(source != NULL);

	/*
	 * Do not use ISC__BUFFER_PUTSTR(), so strlen is only done once.
	 */
	l = strlen(source);

	REQUIRE(l <= isc_buffer_availablelength(b));

	cp = isc_buffer_used(b);
	memcpy(cp, source, l);
	b->used += (u_int)l; /* checked above - no overflow here */
}

isc_result_t
isc_buffer_copyregion(isc_buffer_t *b, const isc_region_t *r) {
	unsigned char *base;
	unsigned int available;

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	/*
	 * XXXDCL
	 */
	base = isc_buffer_used(b);
	available = isc_buffer_availablelength(b);
	if (r->length > available)
		return (ISC_R_NOSPACE);
	memcpy(base, r->base, r->length);
	b->used += r->length;

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_buffer_allocate(isc_mem_t *mctx, isc_buffer_t **dynbuffer,
		    unsigned int length)
{
	isc_buffer_t *dbuf;

	REQUIRE(dynbuffer != NULL);
	REQUIRE(*dynbuffer == NULL);

	dbuf = isc_mem_get(mctx, length + sizeof(isc_buffer_t));
	if (dbuf == NULL)
		return (ISC_R_NOMEMORY);

	isc_buffer_init(dbuf, ((unsigned char *)dbuf) + sizeof(isc_buffer_t),
			length);
	dbuf->mctx = mctx;

	*dynbuffer = dbuf;

	return (ISC_R_SUCCESS);
}

void
isc_buffer_free(isc_buffer_t **dynbuffer) {
	unsigned int real_length;
	isc_buffer_t *dbuf;
	isc_mem_t *mctx;

	REQUIRE(dynbuffer != NULL);
	REQUIRE(ISC_BUFFER_VALID(*dynbuffer));
	REQUIRE((*dynbuffer)->mctx != NULL);

	dbuf = *dynbuffer;
	*dynbuffer = NULL;	/* destroy external reference */

	real_length = dbuf->length + sizeof(isc_buffer_t);
	mctx = dbuf->mctx;
	dbuf->mctx = NULL;
	isc_buffer_invalidate(dbuf);

	isc_mem_put(mctx, dbuf, real_length);
}
