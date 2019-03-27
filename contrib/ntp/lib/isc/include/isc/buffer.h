/*
 * Copyright (C) 2004-2008, 2010  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: buffer.h,v 1.55 2010/12/20 23:47:21 tbox Exp $ */

#ifndef ISC_BUFFER_H
#define ISC_BUFFER_H 1

/*****
 ***** Module Info
 *****/

/*! \file isc/buffer.h
 *
 * \brief A buffer is a region of memory, together with a set of related subregions.
 * Buffers are used for parsing and I/O operations.
 *
 * The 'used region' and the 'available' region are disjoint, and their
 * union is the buffer's region.  The used region extends from the beginning
 * of the buffer region to the last used byte.  The available region
 * extends from one byte greater than the last used byte to the end of the
 * buffer's region.  The size of the used region can be changed using various
 * buffer commands.  Initially, the used region is empty.
 *
 * The used region is further subdivided into two disjoint regions: the
 * 'consumed region' and the 'remaining region'.  The union of these two
 * regions is the used region.  The consumed region extends from the beginning
 * of the used region to the byte before the 'current' offset (if any).  The
 * 'remaining' region the current pointer to the end of the used
 * region.  The size of the consumed region can be changed using various
 * buffer commands.  Initially, the consumed region is empty.
 *
 * The 'active region' is an (optional) subregion of the remaining region.
 * It extends from the current offset to an offset in the remaining region
 * that is selected with isc_buffer_setactive().  Initially, the active region
 * is empty.  If the current offset advances beyond the chosen offset, the
 * active region will also be empty.
 *
 * \verbatim
 *  /------------entire length---------------\
 *  /----- used region -----\/-- available --\
 *  +----------------------------------------+
 *  | consumed  | remaining |                |
 *  +----------------------------------------+
 *  a           b     c     d                e
 *
 * a == base of buffer.
 * b == current pointer.  Can be anywhere between a and d.
 * c == active pointer.  Meaningful between b and d.
 * d == used pointer.
 * e == length of buffer.
 *
 * a-e == entire length of buffer.
 * a-d == used region.
 * a-b == consumed region.
 * b-d == remaining region.
 * b-c == optional active region.
 *\endverbatim
 *
 * The following invariants are maintained by all routines:
 *
 *\code
 *	length > 0
 *
 *	base is a valid pointer to length bytes of memory
 *
 *	0 <= used <= length
 *
 *	0 <= current <= used
 *
 *	0 <= active <= used
 *	(although active < current implies empty active region)
 *\endcode
 *
 * \li MP:
 *	Buffers have no synchronization.  Clients must ensure exclusive
 *	access.
 *
 * \li Reliability:
 *	No anticipated impact.
 *
 * \li Resources:
 *	Memory: 1 pointer + 6 unsigned integers per buffer.
 *
 * \li Security:
 *	No anticipated impact.
 *
 * \li Standards:
 *	None.
 */

/***
 *** Imports
 ***/

#include <isc/lang.h>
#include <isc/magic.h>
#include <isc/types.h>

/*!
 * To make many functions be inline macros (via \#define) define this.
 * If it is undefined, a function will be used.
 */
/* #define ISC_BUFFER_USEINLINE */

ISC_LANG_BEGINDECLS

/*@{*/
/*!
 *** Magic numbers
 ***/
#define ISC_BUFFER_MAGIC		0x42756621U	/* Buf!. */
#define ISC_BUFFER_VALID(b)		ISC_MAGIC_VALID(b, ISC_BUFFER_MAGIC)
/*@}*/

/*
 * The following macros MUST be used only on valid buffers.  It is the
 * caller's responsibility to ensure this by using the ISC_BUFFER_VALID
 * check above, or by calling another isc_buffer_*() function (rather than
 * another macro.)
 */

/*@{*/
/*!
 * Fundamental buffer elements.  (A through E in the introductory comment.)
 */
#define isc_buffer_base(b)    ((void *)(b)->base)			  /*a*/
#define isc_buffer_current(b) \
		((void *)((unsigned char *)(b)->base + (b)->current))     /*b*/
#define isc_buffer_active(b)  \
		((void *)((unsigned char *)(b)->base + (b)->active))      /*c*/
#define isc_buffer_used(b)    \
		((void *)((unsigned char *)(b)->base + (b)->used))        /*d*/
#define isc_buffer_length(b)  ((b)->length)				  /*e*/
/*@}*/

/*@{*/
/*!
 * Derived lengths.  (Described in the introductory comment.)
 */
#define isc_buffer_usedlength(b)	((b)->used)		      /* d-a */
#define isc_buffer_consumedlength(b)	((b)->current)		      /* b-a */
#define isc_buffer_remaininglength(b)	((b)->used - (b)->current)    /* d-b */
#define isc_buffer_activelength(b)	((b)->active - (b)->current)  /* c-b */
#define isc_buffer_availablelength(b)	((b)->length - (b)->used)     /* e-d */
/*@}*/

/*!
 * Note that the buffer structure is public.  This is principally so buffer
 * operations can be implemented using macros.  Applications are strongly
 * discouraged from directly manipulating the structure.
 */

struct isc_buffer {
	unsigned int		magic;
	void		       *base;
	/*@{*/
	/*! The following integers are byte offsets from 'base'. */
	unsigned int		length;
	unsigned int		used;
	unsigned int 		current;
	unsigned int 		active;
	/*@}*/
	/*! linkable */
	ISC_LINK(isc_buffer_t)	link;
	/*! private internal elements */
	isc_mem_t	       *mctx;
};

/***
 *** Functions
 ***/

isc_result_t
isc_buffer_allocate(isc_mem_t *mctx, isc_buffer_t **dynbuffer,
		    unsigned int length);
/*!<
 * \brief Allocate a dynamic linkable buffer which has "length" bytes in the
 * data region.
 *
 * Requires:
 *\li	"mctx" is valid.
 *
 *\li	"dynbuffer" is non-NULL, and "*dynbuffer" is NULL.
 *
 * Returns:
 *\li	ISC_R_SUCCESS		- success
 *\li	ISC_R_NOMEMORY		- no memory available
 *
 * Note:
 *\li	Changing the buffer's length field is not permitted.
 */

void
isc_buffer_free(isc_buffer_t **dynbuffer);
/*!<
 * \brief Release resources allocated for a dynamic buffer.
 *
 * Requires:
 *\li	"dynbuffer" is not NULL.
 *
 *\li	"*dynbuffer" is a valid dynamic buffer.
 *
 * Ensures:
 *\li	"*dynbuffer" will be NULL on return, and all memory associated with
 *	the dynamic buffer is returned to the memory context used in
 *	isc_buffer_allocate().
 */

void
isc__buffer_init(isc_buffer_t *b, const void *base, unsigned int length);
/*!<
 * \brief Make 'b' refer to the 'length'-byte region starting at base.
 *
 * Requires:
 *
 *\li	'length' > 0
 *
 *\li	'base' is a pointer to a sequence of 'length' bytes.
 *
 */

void
isc__buffer_initnull(isc_buffer_t *b);
/*!<
 *\brief Initialize a buffer 'b' with a null data and zero length/
 */

void
isc_buffer_reinit(isc_buffer_t *b, void *base, unsigned int length);
/*!<
 * \brief Make 'b' refer to the 'length'-byte region starting at base.
 * Any existing data will be copied.
 *
 * Requires:
 *
 *\li	'length' > 0 AND length >= previous length
 *
 *\li	'base' is a pointer to a sequence of 'length' bytes.
 *
 */

void
isc__buffer_invalidate(isc_buffer_t *b);
/*!<
 * \brief Make 'b' an invalid buffer.
 *
 * Requires:
 *\li	'b' is a valid buffer.
 *
 * Ensures:
 *\li	If assertion checking is enabled, future attempts to use 'b' without
 *	calling isc_buffer_init() on it will cause an assertion failure.
 */

void
isc__buffer_region(isc_buffer_t *b, isc_region_t *r);
/*!<
 * \brief Make 'r' refer to the region of 'b'.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer.
 *
 *\li	'r' points to a region structure.
 */

void
isc__buffer_usedregion(isc_buffer_t *b, isc_region_t *r);
/*!<
 * \brief Make 'r' refer to the used region of 'b'.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer.
 *
 *\li	'r' points to a region structure.
 */

void
isc__buffer_availableregion(isc_buffer_t *b, isc_region_t *r);
/*!<
 * \brief Make 'r' refer to the available region of 'b'.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer.
 *
 *\li	'r' points to a region structure.
 */

void
isc__buffer_add(isc_buffer_t *b, unsigned int n);
/*!<
 * \brief Increase the 'used' region of 'b' by 'n' bytes.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer
 *
 *\li	used + n <= length
 *
 */

void
isc__buffer_subtract(isc_buffer_t *b, unsigned int n);
/*!<
 * \brief Decrease the 'used' region of 'b' by 'n' bytes.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer
 *
 *\li	used >= n
 *
 */

void
isc__buffer_clear(isc_buffer_t *b);
/*!<
 * \brief Make the used region empty.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer
 *
 * Ensures:
 *
 *\li	used = 0
 *
 */

void
isc__buffer_consumedregion(isc_buffer_t *b, isc_region_t *r);
/*!<
 * \brief Make 'r' refer to the consumed region of 'b'.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer.
 *
 *\li	'r' points to a region structure.
 */

void
isc__buffer_remainingregion(isc_buffer_t *b, isc_region_t *r);
/*!<
 * \brief Make 'r' refer to the remaining region of 'b'.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer.
 *
 *\li	'r' points to a region structure.
 */

void
isc__buffer_activeregion(isc_buffer_t *b, isc_region_t *r);
/*!<
 * \brief Make 'r' refer to the active region of 'b'.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer.
 *
 *\li	'r' points to a region structure.
 */

void
isc__buffer_setactive(isc_buffer_t *b, unsigned int n);
/*!<
 * \brief Sets the end of the active region 'n' bytes after current.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer.
 *
 *\li	current + n <= used
 */

void
isc__buffer_first(isc_buffer_t *b);
/*!<
 * \brief Make the consumed region empty.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer
 *
 * Ensures:
 *
 *\li	current == 0
 *
 */

void
isc__buffer_forward(isc_buffer_t *b, unsigned int n);
/*!<
 * \brief Increase the 'consumed' region of 'b' by 'n' bytes.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer
 *
 *\li	current + n <= used
 *
 */

void
isc__buffer_back(isc_buffer_t *b, unsigned int n);
/*!<
 * \brief Decrease the 'consumed' region of 'b' by 'n' bytes.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer
 *
 *\li	n <= current
 *
 */

void
isc_buffer_compact(isc_buffer_t *b);
/*!<
 * \brief Compact the used region by moving the remaining region so it occurs
 * at the start of the buffer.  The used region is shrunk by the size of
 * the consumed region, and the consumed region is then made empty.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer
 *
 * Ensures:
 *
 *\li	current == 0
 *
 *\li	The size of the used region is now equal to the size of the remaining
 *	region (as it was before the call).  The contents of the used region
 *	are those of the remaining region (as it was before the call).
 */

isc_uint8_t
isc_buffer_getuint8(isc_buffer_t *b);
/*!<
 * \brief Read an unsigned 8-bit integer from 'b' and return it.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer.
 *
 *\li	The length of the available region of 'b' is at least 1.
 *
 * Ensures:
 *
 *\li	The current pointer in 'b' is advanced by 1.
 *
 * Returns:
 *
 *\li	A 8-bit unsigned integer.
 */

void
isc__buffer_putuint8(isc_buffer_t *b, isc_uint8_t val);
/*!<
 * \brief Store an unsigned 8-bit integer from 'val' into 'b'.
 *
 * Requires:
 *\li	'b' is a valid buffer.
 *
 *\li	The length of the unused region of 'b' is at least 1.
 *
 * Ensures:
 *\li	The used pointer in 'b' is advanced by 1.
 */

isc_uint16_t
isc_buffer_getuint16(isc_buffer_t *b);
/*!<
 * \brief Read an unsigned 16-bit integer in network byte order from 'b', convert
 * it to host byte order, and return it.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer.
 *
 *\li	The length of the available region of 'b' is at least 2.
 *
 * Ensures:
 *
 *\li	The current pointer in 'b' is advanced by 2.
 *
 * Returns:
 *
 *\li	A 16-bit unsigned integer.
 */

void
isc__buffer_putuint16(isc_buffer_t *b, isc_uint16_t val);
/*!<
 * \brief Store an unsigned 16-bit integer in host byte order from 'val'
 * into 'b' in network byte order.
 *
 * Requires:
 *\li	'b' is a valid buffer.
 *
 *\li	The length of the unused region of 'b' is at least 2.
 *
 * Ensures:
 *\li	The used pointer in 'b' is advanced by 2.
 */

isc_uint32_t
isc_buffer_getuint32(isc_buffer_t *b);
/*!<
 * \brief Read an unsigned 32-bit integer in network byte order from 'b', convert
 * it to host byte order, and return it.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer.
 *
 *\li	The length of the available region of 'b' is at least 4.
 *
 * Ensures:
 *
 *\li	The current pointer in 'b' is advanced by 4.
 *
 * Returns:
 *
 *\li	A 32-bit unsigned integer.
 */

void
isc__buffer_putuint32(isc_buffer_t *b, isc_uint32_t val);
/*!<
 * \brief Store an unsigned 32-bit integer in host byte order from 'val'
 * into 'b' in network byte order.
 *
 * Requires:
 *\li	'b' is a valid buffer.
 *
 *\li	The length of the unused region of 'b' is at least 4.
 *
 * Ensures:
 *\li	The used pointer in 'b' is advanced by 4.
 */

isc_uint64_t
isc_buffer_getuint48(isc_buffer_t *b);
/*!<
 * \brief Read an unsigned 48-bit integer in network byte order from 'b',
 * convert it to host byte order, and return it.
 *
 * Requires:
 *
 *\li	'b' is a valid buffer.
 *
 *\li	The length of the available region of 'b' is at least 6.
 *
 * Ensures:
 *
 *\li	The current pointer in 'b' is advanced by 6.
 *
 * Returns:
 *
 *\li	A 48-bit unsigned integer (stored in a 64-bit integer).
 */

void
isc__buffer_putuint48(isc_buffer_t *b, isc_uint64_t val);
/*!<
 * \brief Store an unsigned 48-bit integer in host byte order from 'val'
 * into 'b' in network byte order.
 *
 * Requires:
 *\li	'b' is a valid buffer.
 *
 *\li	The length of the unused region of 'b' is at least 6.
 *
 * Ensures:
 *\li	The used pointer in 'b' is advanced by 6.
 */

void
isc__buffer_putuint24(isc_buffer_t *b, isc_uint32_t val);
/*!<
 * Store an unsigned 24-bit integer in host byte order from 'val'
 * into 'b' in network byte order.
 *
 * Requires:
 *\li	'b' is a valid buffer.
 *
 *	The length of the unused region of 'b' is at least 3.
 *
 * Ensures:
 *\li	The used pointer in 'b' is advanced by 3.
 */

void
isc__buffer_putmem(isc_buffer_t *b, const unsigned char *base,
		   unsigned int length);
/*!<
 * \brief Copy 'length' bytes of memory at 'base' into 'b'.
 *
 * Requires:
 *\li	'b' is a valid buffer.
 *
 *\li	'base' points to 'length' bytes of valid memory.
 *
 */

void
isc__buffer_putstr(isc_buffer_t *b, const char *source);
/*!<
 * \brief Copy 'source' into 'b', not including terminating NUL.
 *
 * Requires:
 *\li	'b' is a valid buffer.
 *
 *\li	'source' to be a valid NULL terminated string.
 *
 *\li	strlen(source) <= isc_buffer_available(b)
 */

isc_result_t
isc_buffer_copyregion(isc_buffer_t *b, const isc_region_t *r);
/*!<
 * \brief Copy the contents of 'r' into 'b'.
 *
 * Requires:
 *\li	'b' is a valid buffer.
 *
 *\li	'r' is a valid region.
 *
 * Returns:
 *
 *\li	ISC_R_SUCCESS
 *\li	ISC_R_NOSPACE			The available region of 'b' is not
 *					big enough.
 */

ISC_LANG_ENDDECLS

/*
 * Inline macro versions of the functions.  These should never be called
 * directly by an application, but will be used by the functions within
 * buffer.c.  The callers should always use "isc_buffer_*()" names, never
 * ones beginning with "isc__"
 */

/*! \note
 * XXXDCL Something more could be done with initializing buffers that
 * point to const data.  For example, a new function, isc_buffer_initconst,
 * could be used, and a new boolean flag in the buffer structure could
 * indicate whether the buffer was initialized with that function.
 * (isc_bufer_init itself would be reprototyped to *not* have its "base"
 * parameter be const.)  Then if the boolean were true, the isc_buffer_put*
 * functions could assert a contractual requirement for a non-const buffer.
 * One drawback is that the isc_buffer_* functions (macros) that return
 * pointers would still need to return non-const pointers to avoid compiler
 * warnings, so it would be up to code that uses them to have to deal
 * with the possibility that the buffer was initialized as const --
 * a problem that they *already* have to deal with but have absolutely
 * no ability to.  With a new isc_buffer_isconst() function returning
 * true/false, they could at least assert a contractual requirement for
 * non-const buffers when needed.
 */
#define ISC__BUFFER_INIT(_b, _base, _length) \
	do { \
		union { \
			const void *	konst; \
			void *		var; \
		} _u; \
		_u.konst = (_base); \
		(_b)->base = _u.var; \
		(_b)->length = (_length); \
		(_b)->used = 0; \
		(_b)->current = 0; \
		(_b)->active = 0; \
		(_b)->mctx = NULL; \
		ISC_LINK_INIT(_b, link); \
		(_b)->magic = ISC_BUFFER_MAGIC; \
	} while (0)

#define ISC__BUFFER_INITNULL(_b) ISC__BUFFER_INIT(_b, NULL, 0)

#define ISC__BUFFER_INVALIDATE(_b) \
	do { \
		(_b)->magic = 0; \
		(_b)->base = NULL; \
		(_b)->length = 0; \
		(_b)->used = 0; \
		(_b)->current = 0; \
		(_b)->active = 0; \
	} while (0)

#define ISC__BUFFER_REGION(_b, _r) \
	do { \
		(_r)->base = (_b)->base; \
		(_r)->length = (_b)->length; \
	} while (0)

#define ISC__BUFFER_USEDREGION(_b, _r) \
	do { \
		(_r)->base = (_b)->base; \
		(_r)->length = (_b)->used; \
	} while (0)

#define ISC__BUFFER_AVAILABLEREGION(_b, _r) \
	do { \
		(_r)->base = isc_buffer_used(_b); \
		(_r)->length = isc_buffer_availablelength(_b); \
	} while (0)

#define ISC__BUFFER_ADD(_b, _n) \
	do { \
		(_b)->used += (_n); \
	} while (0)

#define ISC__BUFFER_SUBTRACT(_b, _n) \
	do { \
		(_b)->used -= (_n); \
		if ((_b)->current > (_b)->used) \
			(_b)->current = (_b)->used; \
		if ((_b)->active > (_b)->used) \
			(_b)->active = (_b)->used; \
	} while (0)

#define ISC__BUFFER_CLEAR(_b) \
	do { \
		(_b)->used = 0; \
		(_b)->current = 0; \
		(_b)->active = 0; \
	} while (0)

#define ISC__BUFFER_CONSUMEDREGION(_b, _r) \
	do { \
		(_r)->base = (_b)->base; \
		(_r)->length = (_b)->current; \
	} while (0)

#define ISC__BUFFER_REMAININGREGION(_b, _r) \
	do { \
		(_r)->base = isc_buffer_current(_b); \
		(_r)->length = isc_buffer_remaininglength(_b); \
	} while (0)

#define ISC__BUFFER_ACTIVEREGION(_b, _r) \
	do { \
		if ((_b)->current < (_b)->active) { \
			(_r)->base = isc_buffer_current(_b); \
			(_r)->length = isc_buffer_activelength(_b); \
		} else { \
			(_r)->base = NULL; \
			(_r)->length = 0; \
		} \
	} while (0)

#define ISC__BUFFER_SETACTIVE(_b, _n) \
	do { \
		(_b)->active = (_b)->current + (_n); \
	} while (0)

#define ISC__BUFFER_FIRST(_b) \
	do { \
		(_b)->current = 0; \
	} while (0)

#define ISC__BUFFER_FORWARD(_b, _n) \
	do { \
		(_b)->current += (_n); \
	} while (0)

#define ISC__BUFFER_BACK(_b, _n) \
	do { \
		(_b)->current -= (_n); \
	} while (0)

#define ISC__BUFFER_PUTMEM(_b, _base, _length) \
	do { \
		memcpy(isc_buffer_used(_b), (_base), (_length)); \
		(_b)->used += (_length); \
	} while (0)

#define ISC__BUFFER_PUTSTR(_b, _source) \
	do { \
		unsigned int _length; \
		unsigned char *_cp; \
		_length = strlen(_source); \
		_cp = isc_buffer_used(_b); \
		memcpy(_cp, (_source), _length); \
		(_b)->used += (_length); \
	} while (0)

#define ISC__BUFFER_PUTUINT8(_b, _val) \
	do { \
		unsigned char *_cp; \
		isc_uint8_t _val2 = (_val); \
		_cp = isc_buffer_used(_b); \
		(_b)->used++; \
		_cp[0] = _val2 & 0x00ff; \
	} while (0)

#define ISC__BUFFER_PUTUINT16(_b, _val) \
	do { \
		unsigned char *_cp; \
		isc_uint16_t _val2 = (_val); \
		_cp = isc_buffer_used(_b); \
		(_b)->used += 2; \
		_cp[0] = (unsigned char)((_val2 & 0xff00U) >> 8); \
		_cp[1] = (unsigned char)(_val2 & 0x00ffU); \
	} while (0)

#define ISC__BUFFER_PUTUINT24(_b, _val) \
	do { \
		unsigned char *_cp; \
		isc_uint32_t _val2 = (_val); \
		_cp = isc_buffer_used(_b); \
		(_b)->used += 3; \
		_cp[0] = (unsigned char)((_val2 & 0xff0000U) >> 16); \
		_cp[1] = (unsigned char)((_val2 & 0xff00U) >> 8); \
		_cp[2] = (unsigned char)(_val2 & 0x00ffU); \
	} while (0)

#define ISC__BUFFER_PUTUINT32(_b, _val) \
	do { \
		unsigned char *_cp; \
		isc_uint32_t _val2 = (_val); \
		_cp = isc_buffer_used(_b); \
		(_b)->used += 4; \
		_cp[0] = (unsigned char)((_val2 & 0xff000000) >> 24); \
		_cp[1] = (unsigned char)((_val2 & 0x00ff0000) >> 16); \
		_cp[2] = (unsigned char)((_val2 & 0x0000ff00) >> 8); \
		_cp[3] = (unsigned char)((_val2 & 0x000000ff)); \
	} while (0)

#if defined(ISC_BUFFER_USEINLINE)
#define isc_buffer_init			ISC__BUFFER_INIT
#define isc_buffer_initnull		ISC__BUFFER_INITNULL
#define isc_buffer_invalidate		ISC__BUFFER_INVALIDATE
#define isc_buffer_region		ISC__BUFFER_REGION
#define isc_buffer_usedregion		ISC__BUFFER_USEDREGION
#define isc_buffer_availableregion	ISC__BUFFER_AVAILABLEREGION
#define isc_buffer_add			ISC__BUFFER_ADD
#define isc_buffer_subtract		ISC__BUFFER_SUBTRACT
#define isc_buffer_clear		ISC__BUFFER_CLEAR
#define isc_buffer_consumedregion	ISC__BUFFER_CONSUMEDREGION
#define isc_buffer_remainingregion	ISC__BUFFER_REMAININGREGION
#define isc_buffer_activeregion		ISC__BUFFER_ACTIVEREGION
#define isc_buffer_setactive		ISC__BUFFER_SETACTIVE
#define isc_buffer_first		ISC__BUFFER_FIRST
#define isc_buffer_forward		ISC__BUFFER_FORWARD
#define isc_buffer_back			ISC__BUFFER_BACK
#define isc_buffer_putmem		ISC__BUFFER_PUTMEM
#define isc_buffer_putstr		ISC__BUFFER_PUTSTR
#define isc_buffer_putuint8		ISC__BUFFER_PUTUINT8
#define isc_buffer_putuint16		ISC__BUFFER_PUTUINT16
#define isc_buffer_putuint24		ISC__BUFFER_PUTUINT24
#define isc_buffer_putuint32		ISC__BUFFER_PUTUINT32
#else
#define isc_buffer_init			isc__buffer_init
#define isc_buffer_initnull		isc__buffer_initnull
#define isc_buffer_invalidate		isc__buffer_invalidate
#define isc_buffer_region		isc__buffer_region
#define isc_buffer_usedregion		isc__buffer_usedregion
#define isc_buffer_availableregion	isc__buffer_availableregion
#define isc_buffer_add			isc__buffer_add
#define isc_buffer_subtract		isc__buffer_subtract
#define isc_buffer_clear		isc__buffer_clear
#define isc_buffer_consumedregion	isc__buffer_consumedregion
#define isc_buffer_remainingregion	isc__buffer_remainingregion
#define isc_buffer_activeregion		isc__buffer_activeregion
#define isc_buffer_setactive		isc__buffer_setactive
#define isc_buffer_first		isc__buffer_first
#define isc_buffer_forward		isc__buffer_forward
#define isc_buffer_back			isc__buffer_back
#define isc_buffer_putmem		isc__buffer_putmem
#define isc_buffer_putstr		isc__buffer_putstr
#define isc_buffer_putuint8		isc__buffer_putuint8
#define isc_buffer_putuint16		isc__buffer_putuint16
#define isc_buffer_putuint24		isc__buffer_putuint24
#define isc_buffer_putuint32		isc__buffer_putuint32
#endif

/*
 * No inline method for this one (yet).
 */
#define isc_buffer_putuint48		isc__buffer_putuint48

#endif /* ISC_BUFFER_H */
