/*
 * Copyright (c) 2015, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, August 2015
 */

/*
 * CBOR (RFC 7049) mades a suitable test case for libxo's external
 * encoder API.  It's simple, streaming, well documented, and an
 * IETF standard.
 *
 * This encoder uses the "pretty" flag for diagnostics, which isn't
 * really kosher, but it's example code.
 */

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>

#include "xo.h"
#include "xo_encoder.h"
#include "xo_buf.h"

/*
 * memdump(): dump memory contents in hex/ascii
0         1         2         3         4         5         6         7
0123456789012345678901234567890123456789012345678901234567890123456789012345
XX XX XX XX  XX XX XX XX - XX XX XX XX  XX XX XX XX abcdefghijklmnop
 */
static void
cbor_memdump (FILE *fp, const char *title, const char *data,
         size_t len, const char *tag, int indent)
{
    enum { MAX_PER_LINE = 16 };
    char buf[ 80 ];
    char text[ 80 ];
    char *bp, *tp;
    size_t i;
#if 0
    static const int ends[ MAX_PER_LINE ] = { 2, 5, 8, 11, 15, 18, 21, 24,
                                              29, 32, 35, 38, 42, 45, 48, 51 };
#endif

    if (fp == NULL)
	fp = stdout;
    if (tag == NULL)
	tag = "";

    fprintf(fp, "%*s[%s] @ %p (%lx/%lu)\n", indent + 1, tag,
            title, data, (unsigned long) len, (unsigned long) len);

    while (len > 0) {
        bp = buf;
        tp = text;

        for (i = 0; i < MAX_PER_LINE && i < len; i++) {
            if (i && (i % 4) == 0) *bp++ = ' ';
            if (i == 8) {
                *bp++ = '-';
                *bp++ = ' ';
            }
            sprintf(bp, "%02x ", (unsigned char) *data);
            bp += strlen(bp);
            *tp++ = (isprint((int) *data) && *data >= ' ') ? *data : '.';
            data += 1;
        }

        *tp = 0;
        *bp = 0;
        fprintf(fp, "%*s%-54s%s\n", indent + 1, tag, buf, text);
        len -= i;
    }
}

/*
 * CBOR breaks the first byte into two pieces, the major type in the
 * top 3 bits and the minor value in the low 5 bits.  The value can be
 * a small value (0 .. 23), an 8-bit value (24), a 16-bit value (25),
 * a 32-bit value (26), or a 64-bit value (27).  A value of 31
 * represents an unknown length, which we'll use extensively for
 * streaming our content.
 */
#define CBOR_MAJOR_MASK	0xE0
#define CBOR_MINOR_MASK	0x1F
#define CBOR_MAJOR_SHIFT	5

#define CBOR_MAJOR(_x)	  ((_x) & CBOR_MAJOR_MASK)
#define CBOR_MAJOR_VAL(_x) ((_x) << CBOR_MAJOR_SHIFT)
#define CBOR_MINOR_VAL(_x) ((_x) & CBOR_MINOR_MASK)

/* Major type codes */
#define CBOR_UNSIGNED	CBOR_MAJOR_VAL(0) /* 0x00 */
#define CBOR_NEGATIVE	CBOR_MAJOR_VAL(1) /* 0x20 */
#define CBOR_BYTES	CBOR_MAJOR_VAL(2) /* 0x40 */
#define CBOR_STRING	CBOR_MAJOR_VAL(3) /* 0x60 */
#define CBOR_ARRAY	CBOR_MAJOR_VAL(4) /* 0x80 */
#define CBOR_MAP	CBOR_MAJOR_VAL(5) /* 0xa0 */
#define CBOR_SEMANTIC	CBOR_MAJOR_VAL(6) /* 0xc0 */
#define CBOR_SPECIAL	CBOR_MAJOR_VAL(7) /* 0xe0 */

#define CBOR_ULIMIT	24	/* Largest unsigned value */
#define CBOR_NLIMIT	23	/* Largest negative value */

#define CBOR_BREAK	0xFF
#define CBOR_INDEF	0x1F

#define CBOR_FALSE	0xF4
#define CBOR_TRUE	0xF5
#define CBOR_NULL	0xF6
#define CBOR_UNDEF	0xF7

#define CBOR_LEN8	0x18	/* 24 - 8-bit value */
#define CBOR_LEN16	0x19	/* 25 - 16-bit value */
#define CBOR_LEN32	0x1a	/* 26 - 32-bit value */
#define CBOR_LEN64	0x1b	/* 27 - 64-bit value */
#define CBOR_LEN128	0x1c	/* 28 - 128-bit value */

typedef struct cbor_private_s {
    xo_buffer_t c_data;		/* Our data buffer */
    unsigned c_indent;		/* Indent level */
    unsigned c_open_leaf_list;	/* Open leaf list construct? */
} cbor_private_t;

static void
cbor_encode_uint (xo_buffer_t *xbp, uint64_t minor, unsigned limit)
{
    char *bp = xbp->xb_curp;
    int i, m;

    if (minor > (1ULL << 32)) {
	*bp++ |= CBOR_LEN64;
	m = 64;

    } else if (minor > (1<<16)) {
	*bp++ |= CBOR_LEN32;
	m = 32;

    } else if (minor > (1<<8)) {
	*bp++ |= CBOR_LEN16;
	m = 16;

    } else if (minor > limit) {
	*bp++ |= CBOR_LEN8;
	m = 8;
    } else {
	*bp++ |= minor & CBOR_MINOR_MASK;
	m = 0;
    }

    if (m) {
	for (i = m - 8; i >= 0; i -= 8)
	    *bp++ = minor >> i;
    }

    xbp->xb_curp = bp;
}

static void
cbor_append (xo_handle_t *xop, cbor_private_t *cbor, xo_buffer_t *xbp,
	     unsigned major, unsigned minor, const char *data)
{
    if (!xo_buf_has_room(xbp, minor + 2))
	return;

    unsigned offset = xo_buf_offset(xbp);

    *xbp->xb_curp = major;
    cbor_encode_uint(xbp, minor, CBOR_ULIMIT);
    if (data)
	xo_buf_append(xbp, data, minor);

    if (xo_get_flags(xop) & XOF_PRETTY)
	cbor_memdump(stdout, "append", xo_buf_data(xbp, offset),
		     xbp->xb_curp - xbp->xb_bufp - offset, "",
		     cbor->c_indent * 2);
}

static int
cbor_create (xo_handle_t *xop)
{
    cbor_private_t *cbor = xo_realloc(NULL, sizeof(*cbor));
    if (cbor == NULL)
	return -1;

    bzero(cbor, sizeof(*cbor));
    xo_buf_init(&cbor->c_data);

    xo_set_private(xop, cbor);

    cbor_append(xop, cbor, &cbor->c_data, CBOR_MAP | CBOR_INDEF, 0, NULL);

    return 0;
}

static int
cbor_content (xo_handle_t *xop, cbor_private_t *cbor, xo_buffer_t *xbp,
	      const char *value)
{
    int rc = 0;

    unsigned offset = xo_buf_offset(xbp);

    if (value == NULL || *value == '\0' || strcmp(value, "true") == 0)
	cbor_append(xop, cbor, &cbor->c_data, CBOR_TRUE, 0, NULL);
    else if (strcmp(value, "false") == 0)
	cbor_append(xop, cbor, &cbor->c_data, CBOR_FALSE, 0, NULL);
    else {
	int negative = 0;
	if (*value == '-') {
	    value += 1;
	    negative = 1;
	}

	char *ep;
	unsigned long long ival;
	ival = strtoull(value, &ep, 0);
	if (ival == ULLONG_MAX)	/* Sometimes a string is just a string */
	    cbor_append(xop, cbor, xbp, CBOR_STRING, strlen(value), value);
	else {
	    *xbp->xb_curp = negative ? CBOR_NEGATIVE : CBOR_UNSIGNED;
	    if (negative)
		ival -= 1;	/* Don't waste a negative zero */
	    cbor_encode_uint(xbp, ival, negative ? CBOR_NLIMIT : CBOR_ULIMIT);
	}
    }

    if (xo_get_flags(xop) & XOF_PRETTY)
	cbor_memdump(stdout, "content", xo_buf_data(xbp, offset),
		     xbp->xb_curp - xbp->xb_bufp - offset, "",
		     cbor->c_indent * 2);

    return rc;
}

static int
cbor_handler (XO_ENCODER_HANDLER_ARGS)
{
    int rc = 0;
    cbor_private_t *cbor = private;
    xo_buffer_t *xbp = cbor ? &cbor->c_data : NULL;

    if (xo_get_flags(xop) & XOF_PRETTY) {
	printf("%*sop %s: [%s] [%s]\n", cbor ? cbor->c_indent * 2 + 4 : 0, "",
	       xo_encoder_op_name(op), name, value);
	fflush(stdout);
    }

    /* If we don't have private data, we're sunk */
    if (cbor == NULL && op != XO_OP_CREATE)
	return -1;

    switch (op) {
    case XO_OP_CREATE:		/* Called when the handle is init'd */
	rc = cbor_create(xop);
	break;

    case XO_OP_OPEN_CONTAINER:
	cbor_append(xop, cbor, xbp, CBOR_STRING, strlen(name), name);
	cbor_append(xop, cbor, xbp, CBOR_MAP | CBOR_INDEF, 0, NULL);
	cbor->c_indent += 1;
	break;

    case XO_OP_CLOSE_CONTAINER:
	cbor_append(xop, cbor, xbp, CBOR_BREAK, 0, NULL);
	cbor->c_indent -= 1;
	break;

    case XO_OP_OPEN_LIST:
	cbor_append(xop, cbor, xbp, CBOR_STRING, strlen(name), name);
	cbor_append(xop, cbor, xbp, CBOR_ARRAY | CBOR_INDEF, 0, NULL);
	cbor->c_indent += 1;
	break;

    case XO_OP_CLOSE_LIST:
	cbor_append(xop, cbor, xbp, CBOR_BREAK, 0, NULL);
	cbor->c_indent -= 1;
	break;

    case XO_OP_OPEN_LEAF_LIST:
	cbor_append(xop, cbor, xbp, CBOR_STRING, strlen(name), name);
	cbor_append(xop, cbor, xbp, CBOR_ARRAY | CBOR_INDEF, 0, NULL);
	cbor->c_indent += 1;
	cbor->c_open_leaf_list = 1;
	break;

    case XO_OP_CLOSE_LEAF_LIST:
	cbor_append(xop, cbor, xbp, CBOR_BREAK, 0, NULL);
	cbor->c_indent -= 1;
	cbor->c_open_leaf_list = 0;
	break;

    case XO_OP_OPEN_INSTANCE:
	cbor_append(xop, cbor, xbp, CBOR_MAP | CBOR_INDEF, 0, NULL);
	cbor->c_indent += 1;
	break;

    case XO_OP_CLOSE_INSTANCE:
	cbor_append(xop, cbor, xbp, CBOR_BREAK, 0, NULL);
	cbor->c_indent -= 1;
	break;

    case XO_OP_STRING:		   /* Quoted UTF-8 string */
	if (!cbor->c_open_leaf_list)
	    cbor_append(xop, cbor, xbp, CBOR_STRING, strlen(name), name);
	cbor_append(xop, cbor, xbp, CBOR_STRING, strlen(value), value);
	break;

    case XO_OP_CONTENT:		   /* Other content */
	if (!cbor->c_open_leaf_list)
	    cbor_append(xop, cbor, xbp, CBOR_STRING, strlen(name), name);

	/*
	 * It's content, not string, so we need to look at the
	 * string and build some content.  Turns out we only
	 * care about true, false, null, and numbers.
	 */
	cbor_content(xop, cbor, xbp, value);
	break;

    case XO_OP_FINISH:		   /* Clean up function */
	cbor_append(xop, cbor, xbp, CBOR_BREAK, 0, NULL);
	cbor->c_indent -= 1;
	break;

    case XO_OP_FLUSH:		   /* Clean up function */
	if (xo_get_flags(xop) & XOF_PRETTY)
	    cbor_memdump(stdout, "cbor",
			xbp->xb_bufp, xbp->xb_curp - xbp->xb_bufp,
			">", 0);
	else {
	    rc = write(1, xbp->xb_bufp, xbp->xb_curp - xbp->xb_bufp);
	    if (rc > 0)
		rc = 0;
	}
	break;

    case XO_OP_DESTROY:		   /* Clean up function */
	break;

    case XO_OP_ATTRIBUTE:	   /* Attribute name/value */
	break;

    case XO_OP_VERSION:		/* Version string */
	break;

    }

    return rc;
}

int
xo_encoder_library_init (XO_ENCODER_INIT_ARGS)
{
    arg->xei_handler = cbor_handler;

    return 0;
}
