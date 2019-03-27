/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr.h"
#include "apr_private.h"

#include "apr_lib.h"
#include "apr_strings.h"
#include "apr_network_io.h"
#include "apr_portable.h"
#include "apr_errno.h"
#include <math.h>
#if APR_HAVE_CTYPE_H
#include <ctype.h>
#endif
#if APR_HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if APR_HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if APR_HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#if APR_HAVE_LIMITS_H
#include <limits.h>
#endif
#if APR_HAVE_STRING_H
#include <string.h>
#endif

typedef enum {
    NO = 0, YES = 1
} boolean_e;

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#define NUL '\0'

static const char null_string[] = "(null)";
#define S_NULL ((char *)null_string)
#define S_NULL_LEN 6

#define FLOAT_DIGITS 6
#define EXPONENT_LENGTH 10

/*
 * NUM_BUF_SIZE is the size of the buffer used for arithmetic conversions
 *
 * NOTICE: this is a magic number; do not decrease it
 */
#define NUM_BUF_SIZE 512

/*
 * cvt - IEEE floating point formatting routines.
 *       Derived from UNIX V7, Copyright(C) Caldera International Inc.
 */

/*
 *    apr_ecvt converts to decimal
 *      the number of digits is specified by ndigit
 *      decpt is set to the position of the decimal point
 *      sign is set to 0 for positive, 1 for negative
 */

#define NDIG 80

/* buf must have at least NDIG bytes */
static char *apr_cvt(double arg, int ndigits, int *decpt, int *sign, 
                     int eflag, char *buf)
{
    register int r2;
    double fi, fj;
    register char *p, *p1;
    
    if (ndigits >= NDIG - 1)
        ndigits = NDIG - 2;
    r2 = 0;
    *sign = 0;
    p = &buf[0];
    if (arg < 0) {
        *sign = 1;
        arg = -arg;
    }
    arg = modf(arg, &fi);
    p1 = &buf[NDIG];
    /*
     * Do integer part
     */
    if (fi != 0) {
        p1 = &buf[NDIG];
        while (p1 > &buf[0] && fi != 0) {
            fj = modf(fi / 10, &fi);
            *--p1 = (int) ((fj + .03) * 10) + '0';
            r2++;
        }
        while (p1 < &buf[NDIG])
            *p++ = *p1++;
    }
    else if (arg > 0) {
        while ((fj = arg * 10) < 1) {
            arg = fj;
            r2--;
        }
    }
    p1 = &buf[ndigits];
    if (eflag == 0)
        p1 += r2;
    if (p1 < &buf[0]) {
        *decpt = -ndigits;
        buf[0] = '\0';
        return (buf);
    }
    *decpt = r2;
    while (p <= p1 && p < &buf[NDIG]) {
        arg *= 10;
        arg = modf(arg, &fj);
        *p++ = (int) fj + '0';
    }
    if (p1 >= &buf[NDIG]) {
        buf[NDIG - 1] = '\0';
        return (buf);
    }
    p = p1;
    *p1 += 5;
    while (*p1 > '9') {
        *p1 = '0';
        if (p1 > buf)
            ++ * --p1;
        else {
            *p1 = '1';
            (*decpt)++;
            if (eflag == 0) {
                if (p > buf)
                    *p = '0';
                p++;
            }
        }
    }
    *p = '\0';
    return (buf);
}

static char *apr_ecvt(double arg, int ndigits, int *decpt, int *sign, char *buf)
{
    return (apr_cvt(arg, ndigits, decpt, sign, 1, buf));
}

static char *apr_fcvt(double arg, int ndigits, int *decpt, int *sign, char *buf)
{
    return (apr_cvt(arg, ndigits, decpt, sign, 0, buf));
}

/*
 * apr_gcvt  - Floating output conversion to
 * minimal length string
 */

static char *apr_gcvt(double number, int ndigit, char *buf, boolean_e altform)
{
    int sign, decpt;
    register char *p1, *p2;
    register int i;
    char buf1[NDIG];

    p1 = apr_ecvt(number, ndigit, &decpt, &sign, buf1);
    p2 = buf;
    if (sign)
        *p2++ = '-';
    for (i = ndigit - 1; i > 0 && p1[i] == '0'; i--)
        ndigit--;
    if ((decpt >= 0 && decpt - ndigit > 4)
        || (decpt < 0 && decpt < -3)) {                /* use E-style */
        decpt--;
        *p2++ = *p1++;
        *p2++ = '.';
        for (i = 1; i < ndigit; i++)
            *p2++ = *p1++;
        *p2++ = 'e';
        if (decpt < 0) {
            decpt = -decpt;
            *p2++ = '-';
        }
        else
            *p2++ = '+';
        if (decpt / 100 > 0)
            *p2++ = decpt / 100 + '0';
        if (decpt / 10 > 0)
            *p2++ = (decpt % 100) / 10 + '0';
        *p2++ = decpt % 10 + '0';
    }
    else {
        if (decpt <= 0) {
            if (*p1 != '0')
                *p2++ = '.';
            while (decpt < 0) {
                decpt++;
                *p2++ = '0';
            }
        }
        for (i = 1; i <= ndigit; i++) {
            *p2++ = *p1++;
            if (i == decpt)
                *p2++ = '.';
        }
        if (ndigit < decpt) {
            while (ndigit++ < decpt)
                *p2++ = '0';
            *p2++ = '.';
        }
    }
    if (p2[-1] == '.' && !altform)
        p2--;
    *p2 = '\0';
    return (buf);
}

/*
 * The INS_CHAR macro inserts a character in the buffer and writes
 * the buffer back to disk if necessary
 * It uses the char pointers sp and bep:
 *      sp points to the next available character in the buffer
 *      bep points to the end-of-buffer+1
 * While using this macro, note that the nextb pointer is NOT updated.
 *
 * NOTE: Evaluation of the c argument should not have any side-effects
 */
#define INS_CHAR(c, sp, bep, cc)                    \
{                                                   \
    if (sp) {                                       \
        if (sp >= bep) {                            \
            vbuff->curpos = sp;                     \
            if (flush_func(vbuff))                  \
                return -1;                          \
            sp = vbuff->curpos;                     \
            bep = vbuff->endpos;                    \
        }                                           \
        *sp++ = (c);                                \
    }                                               \
    cc++;                                           \
}

#define NUM(c) (c - '0')

#define STR_TO_DEC(str, num)                        \
    num = NUM(*str++);                              \
    while (apr_isdigit(*str))                       \
    {                                               \
        num *= 10 ;                                 \
        num += NUM(*str++);                         \
    }

/*
 * This macro does zero padding so that the precision
 * requirement is satisfied. The padding is done by
 * adding '0's to the left of the string that is going
 * to be printed. We don't allow precision to be large
 * enough that we continue past the start of s.
 *
 * NOTE: this makes use of the magic info that s is
 * always based on num_buf with a size of NUM_BUF_SIZE.
 */
#define FIX_PRECISION(adjust, precision, s, s_len)  \
    if (adjust) {                                   \
        apr_size_t p = (precision + 1 < NUM_BUF_SIZE) \
                     ? precision : NUM_BUF_SIZE - 1;  \
        while (s_len < p)                           \
        {                                           \
            *--s = '0';                             \
            s_len++;                                \
        }                                           \
    }

/*
 * Macro that does padding. The padding is done by printing
 * the character ch.
 */
#define PAD(width, len, ch)                         \
do                                                  \
{                                                   \
    INS_CHAR(ch, sp, bep, cc);                      \
    width--;                                        \
}                                                   \
while (width > len)

/*
 * Prefix the character ch to the string str
 * Increase length
 * Set the has_prefix flag
 */
#define PREFIX(str, length, ch)                     \
    *--str = ch;                                    \
    length++;                                       \
    has_prefix=YES;


/*
 * Convert num to its decimal format.
 * Return value:
 *   - a pointer to a string containing the number (no sign)
 *   - len contains the length of the string
 *   - is_negative is set to TRUE or FALSE depending on the sign
 *     of the number (always set to FALSE if is_unsigned is TRUE)
 *
 * The caller provides a buffer for the string: that is the buf_end argument
 * which is a pointer to the END of the buffer + 1 (i.e. if the buffer
 * is declared as buf[ 100 ], buf_end should be &buf[ 100 ])
 *
 * Note: we have 2 versions. One is used when we need to use quads
 * (conv_10_quad), the other when we don't (conv_10). We're assuming the
 * latter is faster.
 */
static char *conv_10(register apr_int32_t num, register int is_unsigned,
                     register int *is_negative, char *buf_end,
                     register apr_size_t *len)
{
    register char *p = buf_end;
    register apr_uint32_t magnitude = num;

    if (is_unsigned) {
        *is_negative = FALSE;
    }
    else {
        *is_negative = (num < 0);

        /*
         * On a 2's complement machine, negating the most negative integer 
         * results in a number that cannot be represented as a signed integer.
         * Here is what we do to obtain the number's magnitude:
         *      a. add 1 to the number
         *      b. negate it (becomes positive)
         *      c. convert it to unsigned
         *      d. add 1
         */
        if (*is_negative) {
            apr_int32_t t = num + 1;
            magnitude = ((apr_uint32_t) -t) + 1;
        }
    }

    /*
     * We use a do-while loop so that we write at least 1 digit 
     */
    do {
        register apr_uint32_t new_magnitude = magnitude / 10;

        *--p = (char) (magnitude - new_magnitude * 10 + '0');
        magnitude = new_magnitude;
    }
    while (magnitude);

    *len = buf_end - p;
    return (p);
}

static char *conv_10_quad(apr_int64_t num, register int is_unsigned,
                     register int *is_negative, char *buf_end,
                     register apr_size_t *len)
{
    register char *p = buf_end;
    apr_uint64_t magnitude = num;

    /*
     * We see if we can use the faster non-quad version by checking the
     * number against the largest long value it can be. If <=, we
     * punt to the quicker version.
     */
    if ((magnitude <= APR_UINT32_MAX && is_unsigned)
        || (num <= APR_INT32_MAX && num >= APR_INT32_MIN && !is_unsigned))
            return(conv_10((apr_int32_t)num, is_unsigned, is_negative, buf_end, len));

    if (is_unsigned) {
        *is_negative = FALSE;
    }
    else {
        *is_negative = (num < 0);

        /*
         * On a 2's complement machine, negating the most negative integer 
         * results in a number that cannot be represented as a signed integer.
         * Here is what we do to obtain the number's magnitude:
         *      a. add 1 to the number
         *      b. negate it (becomes positive)
         *      c. convert it to unsigned
         *      d. add 1
         */
        if (*is_negative) {
            apr_int64_t t = num + 1;
            magnitude = ((apr_uint64_t) -t) + 1;
        }
    }

    /*
     * We use a do-while loop so that we write at least 1 digit 
     */
    do {
        apr_uint64_t new_magnitude = magnitude / 10;

        *--p = (char) (magnitude - new_magnitude * 10 + '0');
        magnitude = new_magnitude;
    }
    while (magnitude);

    *len = buf_end - p;
    return (p);
}

static char *conv_in_addr(struct in_addr *ia, char *buf_end, apr_size_t *len)
{
    unsigned addr = ntohl(ia->s_addr);
    char *p = buf_end;
    int is_negative;
    apr_size_t sub_len;

    p = conv_10((addr & 0x000000FF)      , TRUE, &is_negative, p, &sub_len);
    *--p = '.';
    p = conv_10((addr & 0x0000FF00) >>  8, TRUE, &is_negative, p, &sub_len);
    *--p = '.';
    p = conv_10((addr & 0x00FF0000) >> 16, TRUE, &is_negative, p, &sub_len);
    *--p = '.';
    p = conv_10((addr & 0xFF000000) >> 24, TRUE, &is_negative, p, &sub_len);

    *len = buf_end - p;
    return (p);
}


/* Must be passed a buffer of size NUM_BUF_SIZE where buf_end points
 * to 1 byte past the end of the buffer. */
static char *conv_apr_sockaddr(apr_sockaddr_t *sa, char *buf_end, apr_size_t *len)
{
    char *p = buf_end;
    int is_negative;
    apr_size_t sub_len;
    char *ipaddr_str;

    p = conv_10(sa->port, TRUE, &is_negative, p, &sub_len);
    *--p = ':';
    ipaddr_str = buf_end - NUM_BUF_SIZE;
    if (apr_sockaddr_ip_getbuf(ipaddr_str, sa->addr_str_len, sa)) {
        /* Should only fail if the buffer is too small, which it
         * should not be; but fail safe anyway: */
        *--p = '?';
        *len = buf_end - p;
        return p;
    }
    sub_len = strlen(ipaddr_str);
#if APR_HAVE_IPV6
    if (sa->family == APR_INET6 &&
        !IN6_IS_ADDR_V4MAPPED(&sa->sa.sin6.sin6_addr)) {
        *(p - 1) = ']';
        p -= sub_len + 2;
        *p = '[';
        memcpy(p + 1, ipaddr_str, sub_len);
    }
    else
#endif
    {
        p -= sub_len;
        memcpy(p, ipaddr_str, sub_len);
    }

    *len = buf_end - p;
    return (p);
}



#if APR_HAS_THREADS
static char *conv_os_thread_t(apr_os_thread_t *tid, char *buf_end, apr_size_t *len)
{
    union {
        apr_os_thread_t tid;
        apr_uint64_t u64;
        apr_uint32_t u32;
    } u;
    int is_negative;

    u.tid = *tid;
    switch(sizeof(u.tid)) {
    case sizeof(apr_int32_t):
        return conv_10(u.u32, TRUE, &is_negative, buf_end, len);
    case sizeof(apr_int64_t):
        return conv_10_quad(u.u64, TRUE, &is_negative, buf_end, len);
    default:
        /* not implemented; stick 0 in the buffer */
        return conv_10(0, TRUE, &is_negative, buf_end, len);
    }
}
#endif



/*
 * Convert a floating point number to a string formats 'f', 'e' or 'E'.
 * The result is placed in buf, and len denotes the length of the string
 * The sign is returned in the is_negative argument (and is not placed
 * in buf).
 */
static char *conv_fp(register char format, register double num,
    boolean_e add_dp, int precision, int *is_negative,
    char *buf, apr_size_t *len)
{
    register char *s = buf;
    register char *p;
    int decimal_point;
    char buf1[NDIG];

    if (format == 'f')
        p = apr_fcvt(num, precision, &decimal_point, is_negative, buf1);
    else /* either e or E format */
        p = apr_ecvt(num, precision + 1, &decimal_point, is_negative, buf1);

    /*
     * Check for Infinity and NaN
     */
    if (apr_isalpha(*p)) {
        *len = strlen(p);
        memcpy(buf, p, *len + 1);
        *is_negative = FALSE;
        return (buf);
    }

    if (format == 'f') {
        if (decimal_point <= 0) {
            *s++ = '0';
            if (precision > 0) {
                *s++ = '.';
                while (decimal_point++ < 0)
                    *s++ = '0';
            }
            else if (add_dp)
                *s++ = '.';
        }
        else {
            while (decimal_point-- > 0)
                *s++ = *p++;
            if (precision > 0 || add_dp)
                *s++ = '.';
        }
    }
    else {
        *s++ = *p++;
        if (precision > 0 || add_dp)
            *s++ = '.';
    }

    /*
     * copy the rest of p, the NUL is NOT copied
     */
    while (*p)
        *s++ = *p++;

    if (format != 'f') {
        char temp[EXPONENT_LENGTH];        /* for exponent conversion */
        apr_size_t t_len;
        int exponent_is_negative;

        *s++ = format;                /* either e or E */
        decimal_point--;
        if (decimal_point != 0) {
            p = conv_10((apr_int32_t) decimal_point, FALSE, &exponent_is_negative,
                        &temp[EXPONENT_LENGTH], &t_len);
            *s++ = exponent_is_negative ? '-' : '+';

            /*
             * Make sure the exponent has at least 2 digits
             */
            if (t_len == 1)
                *s++ = '0';
            while (t_len--)
                *s++ = *p++;
        }
        else {
            *s++ = '+';
            *s++ = '0';
            *s++ = '0';
        }
    }

    *len = s - buf;
    return (buf);
}


/*
 * Convert num to a base X number where X is a power of 2. nbits determines X.
 * For example, if nbits is 3, we do base 8 conversion
 * Return value:
 *      a pointer to a string containing the number
 *
 * The caller provides a buffer for the string: that is the buf_end argument
 * which is a pointer to the END of the buffer + 1 (i.e. if the buffer
 * is declared as buf[ 100 ], buf_end should be &buf[ 100 ])
 *
 * As with conv_10, we have a faster version which is used when
 * the number isn't quad size.
 */
static char *conv_p2(register apr_uint32_t num, register int nbits,
                     char format, char *buf_end, register apr_size_t *len)
{
    register int mask = (1 << nbits) - 1;
    register char *p = buf_end;
    static const char low_digits[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    register const char *digits = (format == 'X') ? upper_digits : low_digits;

    do {
        *--p = digits[num & mask];
        num >>= nbits;
    }
    while (num);

    *len = buf_end - p;
    return (p);
}

static char *conv_p2_quad(apr_uint64_t num, register int nbits,
                     char format, char *buf_end, register apr_size_t *len)
{
    register int mask = (1 << nbits) - 1;
    register char *p = buf_end;
    static const char low_digits[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    register const char *digits = (format == 'X') ? upper_digits : low_digits;

    if (num <= APR_UINT32_MAX)
        return(conv_p2((apr_uint32_t)num, nbits, format, buf_end, len));

    do {
        *--p = digits[num & mask];
        num >>= nbits;
    }
    while (num);

    *len = buf_end - p;
    return (p);
}

#if APR_HAS_THREADS
static char *conv_os_thread_t_hex(apr_os_thread_t *tid, char *buf_end, apr_size_t *len)
{
    union {
        apr_os_thread_t tid;
        apr_uint64_t u64;
        apr_uint32_t u32;
    } u;
    int is_negative;

    u.tid = *tid;
    switch(sizeof(u.tid)) {
    case sizeof(apr_int32_t):
        return conv_p2(u.u32, 4, 'x', buf_end, len);
    case sizeof(apr_int64_t):
        return conv_p2_quad(u.u64, 4, 'x', buf_end, len);
    default:
        /* not implemented; stick 0 in the buffer */
        return conv_10(0, TRUE, &is_negative, buf_end, len);
    }
}
#endif

/*
 * Do format conversion placing the output in buffer
 */
APR_DECLARE(int) apr_vformatter(int (*flush_func)(apr_vformatter_buff_t *),
    apr_vformatter_buff_t *vbuff, const char *fmt, va_list ap)
{
    register char *sp;
    register char *bep;
    register int cc = 0;
    register apr_size_t i;

    register char *s = NULL;
    char *q;
    apr_size_t s_len = 0;

    register apr_size_t min_width = 0;
    apr_size_t precision = 0;
    enum {
        LEFT, RIGHT
    } adjust;
    char pad_char;
    char prefix_char;

    double fp_num;
    apr_int64_t i_quad = 0;
    apr_uint64_t ui_quad;
    apr_int32_t i_num = 0;
    apr_uint32_t ui_num = 0;

    char num_buf[NUM_BUF_SIZE];
    char char_buf[2];                /* for printing %% and %<unknown> */

    enum var_type_enum {
            IS_QUAD, IS_LONG, IS_SHORT, IS_INT
    };
    enum var_type_enum var_type = IS_INT;

    /*
     * Flag variables
     */
    boolean_e alternate_form;
    boolean_e print_sign;
    boolean_e print_blank;
    boolean_e adjust_precision;
    boolean_e adjust_width;
    int is_negative;

    sp = vbuff->curpos;
    bep = vbuff->endpos;

    while (*fmt) {
        if (*fmt != '%') {
            INS_CHAR(*fmt, sp, bep, cc);
        }
        else {
            /*
             * Default variable settings
             */
            boolean_e print_something = YES;
            adjust = RIGHT;
            alternate_form = print_sign = print_blank = NO;
            pad_char = ' ';
            prefix_char = NUL;

            fmt++;

            /*
             * Try to avoid checking for flags, width or precision
             */
            if (!apr_islower(*fmt)) {
                /*
                 * Recognize flags: -, #, BLANK, +
                 */
                for (;; fmt++) {
                    if (*fmt == '-')
                        adjust = LEFT;
                    else if (*fmt == '+')
                        print_sign = YES;
                    else if (*fmt == '#')
                        alternate_form = YES;
                    else if (*fmt == ' ')
                        print_blank = YES;
                    else if (*fmt == '0')
                        pad_char = '0';
                    else
                        break;
                }

                /*
                 * Check if a width was specified
                 */
                if (apr_isdigit(*fmt)) {
                    STR_TO_DEC(fmt, min_width);
                    adjust_width = YES;
                }
                else if (*fmt == '*') {
                    int v = va_arg(ap, int);
                    fmt++;
                    adjust_width = YES;
                    if (v < 0) {
                        adjust = LEFT;
                        min_width = (apr_size_t)(-v);
                    }
                    else
                        min_width = (apr_size_t)v;
                }
                else
                    adjust_width = NO;

                /*
                 * Check if a precision was specified
                 */
                if (*fmt == '.') {
                    adjust_precision = YES;
                    fmt++;
                    if (apr_isdigit(*fmt)) {
                        STR_TO_DEC(fmt, precision);
                    }
                    else if (*fmt == '*') {
                        int v = va_arg(ap, int);
                        fmt++;
                        precision = (v < 0) ? 0 : (apr_size_t)v;
                    }
                    else
                        precision = 0;
                }
                else
                    adjust_precision = NO;
            }
            else
                adjust_precision = adjust_width = NO;

            /*
             * Modifier check.  In same cases, APR_OFF_T_FMT can be
             * "lld" and APR_INT64_T_FMT can be "ld" (that is, off_t is
             * "larger" than int64). Check that case 1st.
             * Note that if APR_OFF_T_FMT is "d",
             * the first if condition is never true. If APR_INT64_T_FMT
             * is "d' then the second if condition is never true.
             */
            if ((sizeof(APR_OFF_T_FMT) > sizeof(APR_INT64_T_FMT)) &&
                ((sizeof(APR_OFF_T_FMT) == 4 &&
                 fmt[0] == APR_OFF_T_FMT[0] &&
                 fmt[1] == APR_OFF_T_FMT[1]) ||
                (sizeof(APR_OFF_T_FMT) == 3 &&
                 fmt[0] == APR_OFF_T_FMT[0]) ||
                (sizeof(APR_OFF_T_FMT) > 4 &&
                 strncmp(fmt, APR_OFF_T_FMT, 
                         sizeof(APR_OFF_T_FMT) - 2) == 0))) {
                /* Need to account for trailing 'd' and null in sizeof() */
                var_type = IS_QUAD;
                fmt += (sizeof(APR_OFF_T_FMT) - 2);
            }
            else if ((sizeof(APR_INT64_T_FMT) == 4 &&
                 fmt[0] == APR_INT64_T_FMT[0] &&
                 fmt[1] == APR_INT64_T_FMT[1]) ||
                (sizeof(APR_INT64_T_FMT) == 3 &&
                 fmt[0] == APR_INT64_T_FMT[0]) ||
                (sizeof(APR_INT64_T_FMT) > 4 &&
                 strncmp(fmt, APR_INT64_T_FMT, 
                         sizeof(APR_INT64_T_FMT) - 2) == 0)) {
                /* Need to account for trailing 'd' and null in sizeof() */
                var_type = IS_QUAD;
                fmt += (sizeof(APR_INT64_T_FMT) - 2);
            }
            else if (*fmt == 'q') {
                var_type = IS_QUAD;
                fmt++;
            }
            else if (*fmt == 'l') {
                var_type = IS_LONG;
                fmt++;
            }
            else if (*fmt == 'h') {
                var_type = IS_SHORT;
                fmt++;
            }
            else {
                var_type = IS_INT;
            }

            /*
             * Argument extraction and printing.
             * First we determine the argument type.
             * Then, we convert the argument to a string.
             * On exit from the switch, s points to the string that
             * must be printed, s_len has the length of the string
             * The precision requirements, if any, are reflected in s_len.
             *
             * NOTE: pad_char may be set to '0' because of the 0 flag.
             *   It is reset to ' ' by non-numeric formats
             */
            switch (*fmt) {
            case 'u':
                if (var_type == IS_QUAD) {
                    i_quad = va_arg(ap, apr_uint64_t);
                    s = conv_10_quad(i_quad, 1, &is_negative,
                            &num_buf[NUM_BUF_SIZE], &s_len);
                }
                else {
                    if (var_type == IS_LONG)
                        i_num = (apr_int32_t) va_arg(ap, apr_uint32_t);
                    else if (var_type == IS_SHORT)
                        i_num = (apr_int32_t) (unsigned short) va_arg(ap, unsigned int);
                    else
                        i_num = (apr_int32_t) va_arg(ap, unsigned int);
                    s = conv_10(i_num, 1, &is_negative,
                            &num_buf[NUM_BUF_SIZE], &s_len);
                }
                FIX_PRECISION(adjust_precision, precision, s, s_len);
                break;

            case 'd':
            case 'i':
                if (var_type == IS_QUAD) {
                    i_quad = va_arg(ap, apr_int64_t);
                    s = conv_10_quad(i_quad, 0, &is_negative,
                            &num_buf[NUM_BUF_SIZE], &s_len);
                }
                else {
                    if (var_type == IS_LONG)
                        i_num = va_arg(ap, apr_int32_t);
                    else if (var_type == IS_SHORT)
                        i_num = (short) va_arg(ap, int);
                    else
                        i_num = va_arg(ap, int);
                    s = conv_10(i_num, 0, &is_negative,
                            &num_buf[NUM_BUF_SIZE], &s_len);
                }
                FIX_PRECISION(adjust_precision, precision, s, s_len);

                if (is_negative)
                    prefix_char = '-';
                else if (print_sign)
                    prefix_char = '+';
                else if (print_blank)
                    prefix_char = ' ';
                break;


            case 'o':
                if (var_type == IS_QUAD) {
                    ui_quad = va_arg(ap, apr_uint64_t);
                    s = conv_p2_quad(ui_quad, 3, *fmt,
                            &num_buf[NUM_BUF_SIZE], &s_len);
                }
                else {
                    if (var_type == IS_LONG)
                        ui_num = va_arg(ap, apr_uint32_t);
                    else if (var_type == IS_SHORT)
                        ui_num = (unsigned short) va_arg(ap, unsigned int);
                    else
                        ui_num = va_arg(ap, unsigned int);
                    s = conv_p2(ui_num, 3, *fmt,
                            &num_buf[NUM_BUF_SIZE], &s_len);
                }
                FIX_PRECISION(adjust_precision, precision, s, s_len);
                if (alternate_form && *s != '0') {
                    *--s = '0';
                    s_len++;
                }
                break;


            case 'x':
            case 'X':
                if (var_type == IS_QUAD) {
                    ui_quad = va_arg(ap, apr_uint64_t);
                    s = conv_p2_quad(ui_quad, 4, *fmt,
                            &num_buf[NUM_BUF_SIZE], &s_len);
                }
                else {
                    if (var_type == IS_LONG)
                        ui_num = va_arg(ap, apr_uint32_t);
                    else if (var_type == IS_SHORT)
                        ui_num = (unsigned short) va_arg(ap, unsigned int);
                    else
                        ui_num = va_arg(ap, unsigned int);
                    s = conv_p2(ui_num, 4, *fmt,
                            &num_buf[NUM_BUF_SIZE], &s_len);
                }
                FIX_PRECISION(adjust_precision, precision, s, s_len);
                if (alternate_form && ui_num != 0) {
                    *--s = *fmt;        /* 'x' or 'X' */
                    *--s = '0';
                    s_len += 2;
                }
                break;


            case 's':
                s = va_arg(ap, char *);
                if (s != NULL) {
                    if (!adjust_precision) {
                        s_len = strlen(s);
                    }
                    else {
                        /* From the C library standard in section 7.9.6.1:
                         * ...if the precision is specified, no more then
                         * that many characters are written.  If the
                         * precision is not specified or is greater
                         * than the size of the array, the array shall
                         * contain a null character.
                         *
                         * My reading is is precision is specified and
                         * is less then or equal to the size of the
                         * array, no null character is required.  So
                         * we can't do a strlen.
                         *
                         * This figures out the length of the string
                         * up to the precision.  Once it's long enough
                         * for the specified precision, we don't care
                         * anymore.
                         *
                         * NOTE: you must do the length comparison
                         * before the check for the null character.
                         * Otherwise, you'll check one beyond the
                         * last valid character.
                         */
                        const char *walk;

                        for (walk = s, s_len = 0;
                             (s_len < precision) && (*walk != '\0');
                             ++walk, ++s_len);
                    }
                }
                else {
                    s = S_NULL;
                    s_len = S_NULL_LEN;
                }
                pad_char = ' ';
                break;


            case 'f':
            case 'e':
            case 'E':
                fp_num = va_arg(ap, double);
                /*
                 * We use &num_buf[ 1 ], so that we have room for the sign
                 */
                s = NULL;
#ifdef HAVE_ISNAN
                if (isnan(fp_num)) {
                    s = "nan";
                    s_len = 3;
                }
#endif
#ifdef HAVE_ISINF
                if (!s && isinf(fp_num)) {
                    s = "inf";
                    s_len = 3;
                }
#endif
                if (!s) {
                    s = conv_fp(*fmt, fp_num, alternate_form,
                                (int)((adjust_precision == NO) ? FLOAT_DIGITS : precision),
                                &is_negative, &num_buf[1], &s_len);
                    if (is_negative)
                        prefix_char = '-';
                    else if (print_sign)
                        prefix_char = '+';
                    else if (print_blank)
                        prefix_char = ' ';
                }
                break;


            case 'g':
            case 'G':
                if (adjust_precision == NO)
                    precision = FLOAT_DIGITS;
                else if (precision == 0)
                    precision = 1;
                /*
                 * * We use &num_buf[ 1 ], so that we have room for the sign
                 */
                s = apr_gcvt(va_arg(ap, double), (int) precision, &num_buf[1],
                            alternate_form);
                if (*s == '-')
                    prefix_char = *s++;
                else if (print_sign)
                    prefix_char = '+';
                else if (print_blank)
                    prefix_char = ' ';

                s_len = strlen(s);

                if (alternate_form && (q = strchr(s, '.')) == NULL) {
                    s[s_len++] = '.';
                    s[s_len] = '\0'; /* delimit for following strchr() */
                }
                if (*fmt == 'G' && (q = strchr(s, 'e')) != NULL)
                    *q = 'E';
                break;


            case 'c':
                char_buf[0] = (char) (va_arg(ap, int));
                s = &char_buf[0];
                s_len = 1;
                pad_char = ' ';
                break;


            case '%':
                char_buf[0] = '%';
                s = &char_buf[0];
                s_len = 1;
                pad_char = ' ';
                break;


            case 'n':
                if (var_type == IS_QUAD)
                    *(va_arg(ap, apr_int64_t *)) = cc;
                else if (var_type == IS_LONG)
                    *(va_arg(ap, long *)) = cc;
                else if (var_type == IS_SHORT)
                    *(va_arg(ap, short *)) = cc;
                else
                    *(va_arg(ap, int *)) = cc;
                print_something = NO;
                break;

                /*
                 * This is where we extend the printf format, with a second
                 * type specifier
                 */
            case 'p':
                switch(*++fmt) {
                /*
                 * If the pointer size is equal to or smaller than the size
                 * of the largest unsigned int, we convert the pointer to a
                 * hex number, otherwise we print "%p" to indicate that we
                 * don't handle "%p".
                 */
                case 'p':
#if APR_SIZEOF_VOIDP == 8
                    if (sizeof(void *) <= sizeof(apr_uint64_t)) {
                        ui_quad = (apr_uint64_t) va_arg(ap, void *);
                        s = conv_p2_quad(ui_quad, 4, 'x',
                                &num_buf[NUM_BUF_SIZE], &s_len);
                    }
#else
                    if (sizeof(void *) <= sizeof(apr_uint32_t)) {
                        ui_num = (apr_uint32_t) va_arg(ap, void *);
                        s = conv_p2(ui_num, 4, 'x',
                                &num_buf[NUM_BUF_SIZE], &s_len);
                    }
#endif
                    else {
                        s = "%p";
                        s_len = 2;
                        prefix_char = NUL;
                    }
                    pad_char = ' ';
                    break;

                /* print an apr_sockaddr_t as a.b.c.d:port */
                case 'I':
                {
                    apr_sockaddr_t *sa;

                    sa = va_arg(ap, apr_sockaddr_t *);
                    if (sa != NULL) {
                        s = conv_apr_sockaddr(sa, &num_buf[NUM_BUF_SIZE], &s_len);
                        if (adjust_precision && precision < s_len)
                            s_len = precision;
                    }
                    else {
                        s = S_NULL;
                        s_len = S_NULL_LEN;
                    }
                    pad_char = ' ';
                }
                break;

                /* print a struct in_addr as a.b.c.d */
                case 'A':
                {
                    struct in_addr *ia;

                    ia = va_arg(ap, struct in_addr *);
                    if (ia != NULL) {
                        s = conv_in_addr(ia, &num_buf[NUM_BUF_SIZE], &s_len);
                        if (adjust_precision && precision < s_len)
                            s_len = precision;
                    }
                    else {
                        s = S_NULL;
                        s_len = S_NULL_LEN;
                    }
                    pad_char = ' ';
                }
                break;

                /* print the error for an apr_status_t */
                case 'm':
                {
                    apr_status_t *mrv;

                    mrv = va_arg(ap, apr_status_t *);
                    if (mrv != NULL) {
                        s = apr_strerror(*mrv, num_buf, NUM_BUF_SIZE-1);
                        s_len = strlen(s);
                    }
                    else {
                        s = S_NULL;
                        s_len = S_NULL_LEN;
                    }
                    pad_char = ' ';
                }
                break;

                case 'T':
#if APR_HAS_THREADS
                {
                    apr_os_thread_t *tid;

                    tid = va_arg(ap, apr_os_thread_t *);
                    if (tid != NULL) {
                        s = conv_os_thread_t(tid, &num_buf[NUM_BUF_SIZE], &s_len);
                        if (adjust_precision && precision < s_len)
                            s_len = precision;
                    }
                    else {
                        s = S_NULL;
                        s_len = S_NULL_LEN;
                    }
                    pad_char = ' ';
                }
#else
                    char_buf[0] = '0';
                    s = &char_buf[0];
                    s_len = 1;
                    pad_char = ' ';
#endif
                    break;

                case 't':
#if APR_HAS_THREADS
                {
                    apr_os_thread_t *tid;

                    tid = va_arg(ap, apr_os_thread_t *);
                    if (tid != NULL) {
                        s = conv_os_thread_t_hex(tid, &num_buf[NUM_BUF_SIZE], &s_len);
                        if (adjust_precision && precision < s_len)
                            s_len = precision;
                    }
                    else {
                        s = S_NULL;
                        s_len = S_NULL_LEN;
                    }
                    pad_char = ' ';
                }
#else
                    char_buf[0] = '0';
                    s = &char_buf[0];
                    s_len = 1;
                    pad_char = ' ';
#endif
                    break;

                case 'B':
                case 'F':
                case 'S':
                {
                    char buf[5];
                    apr_off_t size = 0;

                    if (*fmt == 'B') {
                        apr_uint32_t *arg = va_arg(ap, apr_uint32_t *);
                        size = (arg) ? *arg : 0;
                    }
                    else if (*fmt == 'F') {
                        apr_off_t *arg = va_arg(ap, apr_off_t *);
                        size = (arg) ? *arg : 0;
                    }
                    else {
                        apr_size_t *arg = va_arg(ap, apr_size_t *);
                        size = (arg) ? *arg : 0;
                    }

                    s = apr_strfsize(size, buf);
                    s_len = strlen(s);
                    pad_char = ' ';
                }
                break;

                case NUL:
                    /* if %p ends the string, oh well ignore it */
                    continue;

                default:
                    s = "bogus %p";
                    s_len = 8;
                    prefix_char = NUL;
                    (void)va_arg(ap, void *); /* skip the bogus argument on the stack */
                    break;
                }
                break;

            case NUL:
                /*
                 * The last character of the format string was %.
                 * We ignore it.
                 */
                continue;


                /*
                 * The default case is for unrecognized %'s.
                 * We print %<char> to help the user identify what
                 * option is not understood.
                 * This is also useful in case the user wants to pass
                 * the output of format_converter to another function
                 * that understands some other %<char> (like syslog).
                 * Note that we can't point s inside fmt because the
                 * unknown <char> could be preceded by width etc.
                 */
            default:
                char_buf[0] = '%';
                char_buf[1] = *fmt;
                s = char_buf;
                s_len = 2;
                pad_char = ' ';
                break;
            }

            if (prefix_char != NUL && s != S_NULL && s != char_buf) {
                *--s = prefix_char;
                s_len++;
            }

            if (adjust_width && adjust == RIGHT && min_width > s_len) {
                if (pad_char == '0' && prefix_char != NUL) {
                    INS_CHAR(*s, sp, bep, cc);
                    s++;
                    s_len--;
                    min_width--;
                }
                PAD(min_width, s_len, pad_char);
            }

            /*
             * Print the string s. 
             */
            if (print_something == YES) {
                for (i = s_len; i != 0; i--) {
                      INS_CHAR(*s, sp, bep, cc);
                    s++;
                }
            }

            if (adjust_width && adjust == LEFT && min_width > s_len)
                PAD(min_width, s_len, pad_char);
        }
        fmt++;
    }
    vbuff->curpos = sp;

    return cc;
}


static int snprintf_flush(apr_vformatter_buff_t *vbuff)
{
    /* if the buffer fills we have to abort immediately, there is no way
     * to "flush" an apr_snprintf... there's nowhere to flush it to.
     */
    return -1;
}


APR_DECLARE_NONSTD(int) apr_snprintf(char *buf, apr_size_t len, 
                                     const char *format, ...)
{
    int cc;
    va_list ap;
    apr_vformatter_buff_t vbuff;

    if (len == 0) {
        /* NOTE: This is a special case; we just want to return the number
         * of chars that would be written (minus \0) if the buffer
         * size was infinite. We leverage the fact that INS_CHAR
         * just does actual inserts iff the buffer pointer is non-NULL.
         * In this case, we don't care what buf is; it can be NULL, since
         * we don't touch it at all.
         */
        vbuff.curpos = NULL;
        vbuff.endpos = NULL;
    } else {
        /* save one byte for nul terminator */
        vbuff.curpos = buf;
        vbuff.endpos = buf + len - 1;
    }
    va_start(ap, format);
    cc = apr_vformatter(snprintf_flush, &vbuff, format, ap);
    va_end(ap);
    if (len != 0) {
        *vbuff.curpos = '\0';
    }
    return (cc == -1) ? (int)len - 1 : cc;
}


APR_DECLARE(int) apr_vsnprintf(char *buf, apr_size_t len, const char *format,
                               va_list ap)
{
    int cc;
    apr_vformatter_buff_t vbuff;

    if (len == 0) {
        /* See above note */
        vbuff.curpos = NULL;
        vbuff.endpos = NULL;
    } else {
        /* save one byte for nul terminator */
        vbuff.curpos = buf;
        vbuff.endpos = buf + len - 1;
    }
    cc = apr_vformatter(snprintf_flush, &vbuff, format, ap);
    if (len != 0) {
        *vbuff.curpos = '\0';
    }
    return (cc == -1) ? (int)len - 1 : cc;
}
