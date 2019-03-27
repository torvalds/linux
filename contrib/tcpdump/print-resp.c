/*
 * Copyright (c) 2015 The TCPDUMP project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Initial contribution by Andrew Darqui (andrew.darqui@gmail.com).
 */

/* \summary: REdis Serialization Protocol (RESP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>
#include "netdissect.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "extract.h"

static const char tstr[] = " [|RESP]";

/*
 * For information regarding RESP, see: http://redis.io/topics/protocol
 */

#define RESP_SIMPLE_STRING    '+'
#define RESP_ERROR            '-'
#define RESP_INTEGER          ':'
#define RESP_BULK_STRING      '$'
#define RESP_ARRAY            '*'

#define resp_print_empty(ndo)            ND_PRINT((ndo, " empty"))
#define resp_print_null(ndo)             ND_PRINT((ndo, " null"))
#define resp_print_length_too_large(ndo) ND_PRINT((ndo, " length too large"))
#define resp_print_length_negative(ndo)  ND_PRINT((ndo, " length negative and not -1"))
#define resp_print_invalid(ndo)          ND_PRINT((ndo, " invalid"))

void       resp_print(netdissect_options *, const u_char *, u_int);
static int resp_parse(netdissect_options *, register const u_char *, int);
static int resp_print_string_error_integer(netdissect_options *, register const u_char *, int);
static int resp_print_simple_string(netdissect_options *, register const u_char *, int);
static int resp_print_integer(netdissect_options *, register const u_char *, int);
static int resp_print_error(netdissect_options *, register const u_char *, int);
static int resp_print_bulk_string(netdissect_options *, register const u_char *, int);
static int resp_print_bulk_array(netdissect_options *, register const u_char *, int);
static int resp_print_inline(netdissect_options *, register const u_char *, int);
static int resp_get_length(netdissect_options *, register const u_char *, int, const u_char **);

#define LCHECK2(_tot_len, _len) \
    {                           \
        if (_tot_len < _len)    \
            goto trunc;         \
    }

#define LCHECK(_tot_len) LCHECK2(_tot_len, 1)

/*
 * FIND_CRLF:
 * Attempts to move our 'ptr' forward until a \r\n is found,
 * while also making sure we don't exceed the buffer '_len'
 * or go past the end of the captured data.
 * If we exceed or go past the end of the captured data,
 * jump to trunc.
 */
#define FIND_CRLF(_ptr, _len)                   \
    for (;;) {                                  \
        LCHECK2(_len, 2);                       \
        ND_TCHECK2(*_ptr, 2);                   \
        if (*_ptr == '\r' && *(_ptr+1) == '\n') \
            break;                              \
        _ptr++;                                 \
        _len--;                                 \
    }

/*
 * CONSUME_CRLF
 * Consume a CRLF that we've just found.
 */
#define CONSUME_CRLF(_ptr, _len) \
    _ptr += 2;                   \
    _len -= 2;

/*
 * FIND_CR_OR_LF
 * Attempts to move our '_ptr' forward until a \r or \n is found,
 * while also making sure we don't exceed the buffer '_len'
 * or go past the end of the captured data.
 * If we exceed or go past the end of the captured data,
 * jump to trunc.
 */
#define FIND_CR_OR_LF(_ptr, _len)           \
    for (;;) {                              \
        LCHECK(_len);                       \
        ND_TCHECK(*_ptr);                   \
        if (*_ptr == '\r' || *_ptr == '\n') \
            break;                          \
        _ptr++;                             \
        _len--;                             \
    }

/*
 * CONSUME_CR_OR_LF
 * Consume all consecutive \r and \n bytes.
 * If we exceed '_len' or go past the end of the captured data,
 * jump to trunc.
 */
#define CONSUME_CR_OR_LF(_ptr, _len)             \
    {                                            \
        int _found_cr_or_lf = 0;                 \
        for (;;) {                               \
            /*                                   \
             * Have we hit the end of data?      \
             */                                  \
            if (_len == 0 || !ND_TTEST(*_ptr)) { \
                /*                               \
                 * Yes.  Have we seen a \r       \
                 * or \n?                        \
                 */                              \
                if (_found_cr_or_lf) {           \
                    /*                           \
                     * Yes.  Just stop.          \
                     */                          \
                    break;                       \
                }                                \
                /*                               \
                 * No.  We ran out of packet.    \
                 */                              \
                goto trunc;                      \
            }                                    \
            if (*_ptr != '\r' && *_ptr != '\n')  \
                break;                           \
            _found_cr_or_lf = 1;                 \
            _ptr++;                              \
            _len--;                              \
        }                                        \
    }

/*
 * SKIP_OPCODE
 * Skip over the opcode character.
 * The opcode has already been fetched, so we know it's there, and don't
 * need to do any checks.
 */
#define SKIP_OPCODE(_ptr, _tot_len) \
    _ptr++;                         \
    _tot_len--;

/*
 * GET_LENGTH
 * Get a bulk string or array length.
 */
#define GET_LENGTH(_ndo, _tot_len, _ptr, _len)                \
    {                                                         \
        const u_char *_endp;                                  \
        _len = resp_get_length(_ndo, _ptr, _tot_len, &_endp); \
        _tot_len -= (_endp - _ptr);                           \
        _ptr = _endp;                                         \
    }

/*
 * TEST_RET_LEN
 * If ret_len is < 0, jump to the trunc tag which returns (-1)
 * and 'bubbles up' to printing tstr. Otherwise, return ret_len.
 */
#define TEST_RET_LEN(rl) \
    if (rl < 0) { goto trunc; } else { return rl; }

/*
 * TEST_RET_LEN_NORETURN
 * If ret_len is < 0, jump to the trunc tag which returns (-1)
 * and 'bubbles up' to printing tstr. Otherwise, continue onward.
 */
#define TEST_RET_LEN_NORETURN(rl) \
    if (rl < 0) { goto trunc; }

/*
 * RESP_PRINT_SEGMENT
 * Prints a segment in the form of: ' "<stuff>"\n"
 * Assumes the data has already been verified as present.
 */
#define RESP_PRINT_SEGMENT(_ndo, _bp, _len)            \
    ND_PRINT((_ndo, " \""));                           \
    if (fn_printn(_ndo, _bp, _len, _ndo->ndo_snapend)) \
        goto trunc;                                    \
    fn_print_char(_ndo, '"');

void
resp_print(netdissect_options *ndo, const u_char *bp, u_int length)
{
    int ret_len = 0, length_cur = length;

    if(!bp || length <= 0)
        return;

    ND_PRINT((ndo, ": RESP"));
    while (length_cur > 0) {
        /*
         * This block supports redis pipelining.
         * For example, multiple operations can be pipelined within the same string:
         * "*2\r\n\$4\r\nINCR\r\n\$1\r\nz\r\n*2\r\n\$4\r\nINCR\r\n\$1\r\nz\r\n*2\r\n\$4\r\nINCR\r\n\$1\r\nz\r\n"
         * or
         * "PING\r\nPING\r\nPING\r\n"
         * In order to handle this case, we must try and parse 'bp' until
         * 'length' bytes have been processed or we reach a trunc condition.
         */
        ret_len = resp_parse(ndo, bp, length_cur);
        TEST_RET_LEN_NORETURN(ret_len);
        bp += ret_len;
        length_cur -= ret_len;
    }

    return;

trunc:
    ND_PRINT((ndo, "%s", tstr));
}

static int
resp_parse(netdissect_options *ndo, register const u_char *bp, int length)
{
    u_char op;
    int ret_len;

    LCHECK2(length, 1);
    ND_TCHECK(*bp);
    op = *bp;

    /* bp now points to the op, so these routines must skip it */
    switch(op) {
        case RESP_SIMPLE_STRING:  ret_len = resp_print_simple_string(ndo, bp, length);   break;
        case RESP_INTEGER:        ret_len = resp_print_integer(ndo, bp, length);         break;
        case RESP_ERROR:          ret_len = resp_print_error(ndo, bp, length);           break;
        case RESP_BULK_STRING:    ret_len = resp_print_bulk_string(ndo, bp, length);     break;
        case RESP_ARRAY:          ret_len = resp_print_bulk_array(ndo, bp, length);      break;
        default:                  ret_len = resp_print_inline(ndo, bp, length);          break;
    }

    /*
     * This gives up with a "truncated" indicator for all errors,
     * including invalid packet errors; that's what we want, as
     * we have to give up on further parsing in that case.
     */
    TEST_RET_LEN(ret_len);

trunc:
    return (-1);
}

static int
resp_print_simple_string(netdissect_options *ndo, register const u_char *bp, int length) {
    return resp_print_string_error_integer(ndo, bp, length);
}

static int
resp_print_integer(netdissect_options *ndo, register const u_char *bp, int length) {
    return resp_print_string_error_integer(ndo, bp, length);
}

static int
resp_print_error(netdissect_options *ndo, register const u_char *bp, int length) {
    return resp_print_string_error_integer(ndo, bp, length);
}

static int
resp_print_string_error_integer(netdissect_options *ndo, register const u_char *bp, int length) {
    int length_cur = length, len, ret_len;
    const u_char *bp_ptr;

    /* bp points to the op; skip it */
    SKIP_OPCODE(bp, length_cur);
    bp_ptr = bp;

    /*
     * bp now prints past the (+-;) opcode, so it's pointing to the first
     * character of the string (which could be numeric).
     * +OK\r\n
     * -ERR ...\r\n
     * :02912309\r\n
     *
     * Find the \r\n with FIND_CRLF().
     */
    FIND_CRLF(bp_ptr, length_cur);

    /*
     * bp_ptr points to the \r\n, so bp_ptr - bp is the length of text
     * preceding the \r\n.  That includes the opcode, so don't print
     * that.
     */
    len = (bp_ptr - bp);
    RESP_PRINT_SEGMENT(ndo, bp, len);
    ret_len = 1 /*<opcode>*/ + len /*<string>*/ + 2 /*<CRLF>*/;

    TEST_RET_LEN(ret_len);

trunc:
    return (-1);
}

static int
resp_print_bulk_string(netdissect_options *ndo, register const u_char *bp, int length) {
    int length_cur = length, string_len;

    /* bp points to the op; skip it */
    SKIP_OPCODE(bp, length_cur);

    /* <length>\r\n */
    GET_LENGTH(ndo, length_cur, bp, string_len);

    if (string_len >= 0) {
        /* Byte string of length string_len, starting at bp */
        if (string_len == 0)
            resp_print_empty(ndo);
        else {
            LCHECK2(length_cur, string_len);
            ND_TCHECK2(*bp, string_len);
            RESP_PRINT_SEGMENT(ndo, bp, string_len);
            bp += string_len;
            length_cur -= string_len;
        }

        /*
         * Find the \r\n at the end of the string and skip past it.
         * XXX - report an error if the \r\n isn't immediately after
         * the item?
         */
        FIND_CRLF(bp, length_cur);
        CONSUME_CRLF(bp, length_cur);
    } else {
        /* null, truncated, or invalid for some reason */
        switch(string_len) {
            case (-1):  resp_print_null(ndo);             break;
            case (-2):  goto trunc;
            case (-3):  resp_print_length_too_large(ndo); break;
            case (-4):  resp_print_length_negative(ndo);  break;
            default:    resp_print_invalid(ndo);          break;
        }
    }

    return (length - length_cur);

trunc:
    return (-1);
}

static int
resp_print_bulk_array(netdissect_options *ndo, register const u_char *bp, int length) {
    u_int length_cur = length;
    int array_len, i, ret_len;

    /* bp points to the op; skip it */
    SKIP_OPCODE(bp, length_cur);

    /* <array_length>\r\n */
    GET_LENGTH(ndo, length_cur, bp, array_len);

    if (array_len > 0) {
        /* non empty array */
        for (i = 0; i < array_len; i++) {
            ret_len = resp_parse(ndo, bp, length_cur);

            TEST_RET_LEN_NORETURN(ret_len);

            bp += ret_len;
            length_cur -= ret_len;
        }
    } else {
        /* empty, null, truncated, or invalid */
        switch(array_len) {
            case 0:     resp_print_empty(ndo);            break;
            case (-1):  resp_print_null(ndo);             break;
            case (-2):  goto trunc;
            case (-3):  resp_print_length_too_large(ndo); break;
            case (-4):  resp_print_length_negative(ndo);  break;
            default:    resp_print_invalid(ndo);          break;
        }
    }

    return (length - length_cur);

trunc:
    return (-1);
}

static int
resp_print_inline(netdissect_options *ndo, register const u_char *bp, int length) {
    int length_cur = length;
    int len;
    const u_char *bp_ptr;

    /*
     * Inline commands are simply 'strings' followed by \r or \n or both.
     * Redis will do its best to split/parse these strings.
     * This feature of redis is implemented to support the ability of
     * command parsing from telnet/nc sessions etc.
     *
     * <string><\r||\n||\r\n...>
     */

    /*
     * Skip forward past any leading \r, \n, or \r\n.
     */
    CONSUME_CR_OR_LF(bp, length_cur);
    bp_ptr = bp;

    /*
     * Scan forward looking for \r or \n.
     */
    FIND_CR_OR_LF(bp_ptr, length_cur);

    /*
     * Found it; bp_ptr points to the \r or \n, so bp_ptr - bp is the
     * Length of the line text that preceeds it.  Print it.
     */
    len = (bp_ptr - bp);
    RESP_PRINT_SEGMENT(ndo, bp, len);

    /*
     * Skip forward past the \r, \n, or \r\n.
     */
    CONSUME_CR_OR_LF(bp_ptr, length_cur);

    /*
     * Return the number of bytes we processed.
     */
    return (length - length_cur);

trunc:
    return (-1);
}

static int
resp_get_length(netdissect_options *ndo, register const u_char *bp, int len, const u_char **endp)
{
    int result;
    u_char c;
    int saw_digit;
    int neg;
    int too_large;

    if (len == 0)
        goto trunc;
    ND_TCHECK(*bp);
    too_large = 0;
    neg = 0;
    if (*bp == '-') {
        neg = 1;
        bp++;
        len--;
    }
    result = 0;
    saw_digit = 0;

    for (;;) {
        if (len == 0)
            goto trunc;
        ND_TCHECK(*bp);
        c = *bp;
        if (!(c >= '0' && c <= '9')) {
            if (!saw_digit) {
                bp++;
                goto invalid;
            }
            break;
        }
        c -= '0';
        if (result > (INT_MAX / 10)) {
            /* This will overflow an int when we multiply it by 10. */
            too_large = 1;
        } else {
            result *= 10;
            if (result == ((INT_MAX / 10) * 10) && c > (INT_MAX % 10)) {
                /* This will overflow an int when we add c */
                too_large = 1;
            } else
                result += c;
        }
        bp++;
        len--;
        saw_digit = 1;
    }

    /*
     * OK, we found a non-digit character.  It should be a \r, followed
     * by a \n.
     */
    if (*bp != '\r') {
        bp++;
        goto invalid;
    }
    bp++;
    len--;
    if (len == 0)
        goto trunc;
    ND_TCHECK(*bp);
    if (*bp != '\n') {
        bp++;
        goto invalid;
    }
    bp++;
    len--;
    *endp = bp;
    if (neg) {
        /* -1 means "null", anything else is invalid */
        if (too_large || result != 1)
            return (-4);
        result = -1;
    }
    return (too_large ? -3 : result);

trunc:
    *endp = bp;
    return (-2);

invalid:
    *endp = bp;
    return (-5);
}
