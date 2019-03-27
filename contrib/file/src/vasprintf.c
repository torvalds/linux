/*
 * Copyright (c) Ian F. Darwin 1986-1995.
 * Software written by Ian F. Darwin and others;
 * maintained 1995-present by Christos Zoulas and others.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*###########################################################################
  #                                                                           #
  #                                vasprintf                                  #
  #                                                                           #
  #               Copyright (c) 2002-2005 David TAILLANDIER                   #
  #                                                                           #
  ###########################################################################*/

/*

This software is distributed under the "modified BSD licence".

This software is also released with GNU license (GPL) in another file (same
source-code, only license differ).



Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer. Redistributions in binary
form must reproduce the above copyright notice, this list of conditions and
the following disclaimer in the documentation and/or other materials
provided with the distribution. The name of the author may not be used to
endorse or promote products derived from this software without specific
prior written permission. 

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

====================

Hacked from xnprintf version of 26th February 2005 to provide only
vasprintf by Reuben Thomas <rrt@sc3d.org>.

====================


'printf' function family use the following format string:

%[flag][width][.prec][modifier]type

%% is the escape sequence to print a '%'
%  followed by an unknown format will print the characters without
trying to do any interpretation

flag:   none   +     -     #     (blank)
width:  n    0n    *
prec:   none   .0    .n     .*
modifier:    F N L h l ll z t    ('F' and 'N' are ms-dos/16-bit specific)
type:  d i o u x X f e g E G c s p n


The function needs to allocate memory to store the full text before to
actually writing it.  i.e if you want to fnprintf() 1000 characters, the
functions will allocate 1000 bytes.
This behaviour can be modified: you have to customise the code to flush the
internal buffer (writing to screen or file) when it reach a given size. Then
the buffer can have a shorter length. But what? If you really need to write
HUGE string, don't use printf!
During the process, some other memory is allocated (1024 bytes minimum)
to handle the output of partial sprintf() calls. If you have only 10000 bytes
free in memory, you *may* not be able to nprintf() a 8000 bytes-long text.

note: if a buffer overflow occurs, exit() is called. This situation should
never appear ... but if you want to be *really* sure, you have to modify the
code to handle those situations (only one place to modify).
A buffer overflow can only occur if your sprintf() do strange things or when
you use strange formats.

*/
#include "file.h"

#ifndef	lint
FILE_RCSID("@(#)$File: vasprintf.c,v 1.14 2017/08/13 00:21:47 christos Exp $")
#endif	/* lint */

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif

#define ALLOC_CHUNK 2048
#define ALLOC_SECURITY_MARGIN 1024   /* big value because some platforms have very big 'G' exponent */
#if ALLOC_CHUNK < ALLOC_SECURITY_MARGIN
#    error  !!! ALLOC_CHUNK < ALLOC_SECURITY_MARGIN !!!
#endif
/* note: to have some interest, ALLOC_CHUNK should be much greater than ALLOC_SECURITY_MARGIN */

/*
 *  To save a lot of push/pop, every variable are stored into this
 *  structure, which is passed among nearly every sub-functions.
 */
typedef struct {
  const char * src_string;        /* current position into intput string */
  char *       buffer_base;       /* output buffer */
  char *       dest_string;       /* current position into output string */
  size_t       buffer_len;        /* length of output buffer */
  size_t       real_len;          /* real current length of output text */
  size_t       pseudo_len;        /* total length of output text if it were not limited in size */
  size_t       maxlen;
  va_list      vargs;             /* pointer to current position into vargs */
  char *       sprintf_string;
  FILE *       fprintf_file;
} xprintf_struct;

/*
 *  Realloc buffer if needed
 *  Return value:  0 = ok
 *               EOF = not enought memory
 */
static int realloc_buff(xprintf_struct *s, size_t len)
{
  char * ptr;

  if (len + ALLOC_SECURITY_MARGIN + s->real_len > s->buffer_len) {
    len += s->real_len + ALLOC_CHUNK;
    ptr = (char *)realloc((void *)(s->buffer_base), len);
    if (ptr == NULL) {
      s->buffer_base = NULL;
      return EOF;
    }

    s->dest_string = ptr + (size_t)(s->dest_string - s->buffer_base);
    s->buffer_base = ptr;
    s->buffer_len = len;

    (s->buffer_base)[s->buffer_len - 1] = 1; /* overflow marker */
  }

  return 0;
}

/*
 *  Prints 'usual' characters    up to next '%'
 *                            or up to end of text
 */
static int usual_char(xprintf_struct * s)
{
  size_t len;

  len = strcspn(s->src_string, "%");     /* reachs the next '%' or end of input string */
  /* note: 'len' is never 0 because the presence of '%' */
  /* or end-of-line is checked in the calling function  */

  if (realloc_buff(s,len) == EOF)
    return EOF;

  memcpy(s->dest_string, s->src_string, len);
  s->src_string += len;
  s->dest_string += len;
  s->real_len += len;
  s->pseudo_len += len;

  return 0;
}

/*
 *  Return value: 0 = ok
 *                EOF = error
 */
static int print_it(xprintf_struct *s, size_t approx_len,
                    const char *format_string, ...)
{
  va_list varg;
  int vsprintf_len;
  size_t len;

  if (realloc_buff(s,approx_len) == EOF)
    return EOF;

  va_start(varg, format_string);
  vsprintf_len = vsprintf(s->dest_string, format_string, varg);
  va_end(varg);

  /* Check for overflow */
  assert((s->buffer_base)[s->buffer_len - 1] == 1);

  if (vsprintf_len == EOF) /* must be done *after* overflow-check */
    return EOF;

  s->pseudo_len += vsprintf_len;
  len = strlen(s->dest_string);
  s->real_len += len;
  s->dest_string += len;

  return 0;
}

/*
 *  Prints a string (%s)
 *  We need special handling because:
 *     a: the length of the string is unknown
 *     b: when .prec is used, we must not access any extra byte of the
 *        string (of course, if the original sprintf() does... what the
 *        hell, not my problem)
 *
 *  Return value: 0 = ok
 *                EOF = error
 */
static int type_s(xprintf_struct *s, int width, int prec,
                  const char *format_string, const char *arg_string)
{
  size_t string_len;

  if (arg_string == NULL)
    return print_it(s, (size_t)6, "(null)", 0);

  /* hand-made strlen() whitch stops when 'prec' is reached. */
  /* if 'prec' is -1 then it is never reached. */
  string_len = 0;
  while (arg_string[string_len] != 0 && (size_t)prec != string_len)
    string_len++;

  if (width != -1 && string_len < (size_t)width)
    string_len = (size_t)width;

  return print_it(s, string_len, format_string, arg_string);
}

/*
 *  Read a serie of digits. Stop when non-digit is found.
 *  Return value: the value read (between 0 and 32767).
 *  Note: no checks are made against overflow. If the string contain a big
 *  number, then the return value won't be what we want (but, in this case,
 *  the programmer don't know whatr he wants, then no problem).
 */
static int getint(const char **string)
{
  int i = 0;

  while (isdigit((unsigned char)**string) != 0) {
    i = i * 10 + (**string - '0');
    (*string)++;
  }

  if (i < 0 || i > 32767)
    i = 32767; /* if we have i==-10 this is not because the number is */
  /* negative; this is because the number is big */
  return i;
}

/*
 *  Read a part of the format string. A part is 'usual characters' (ie "blabla")
 *  or '%%' escape sequence (to print a single '%') or any combination of
 *  format specifier (ie "%i" or "%10.2d").
 *  After the current part is managed, the function returns to caller with
 *  everything ready to manage the following part.
 *  The caller must ensure than the string is not empty, i.e. the first byte
 *  is not zero.
 *
 *  Return value:  0 = ok
 *                 EOF = error
 */
static int dispatch(xprintf_struct *s)
{
  const char *initial_ptr;
  char format_string[24]; /* max length may be something like  "% +-#032768.32768Ld" */
  char *format_ptr;
  int flag_plus, flag_minus, flag_space, flag_sharp, flag_zero;
  int width, prec, modifier, approx_width;
  char type;
  /* most of those variables are here to rewrite the format string */

#define SRCTXT  (s->src_string)
#define DESTTXT (s->dest_string)

  /* incoherent format string. Characters after the '%' will be printed with the next call */
#define INCOHERENT()         do {SRCTXT=initial_ptr; return 0;} while (0)     /* do/while to avoid */
#define INCOHERENT_TEST()    do {if(*SRCTXT==0)   INCOHERENT();} while (0)    /* a null statement  */

  /* 'normal' text */
  if (*SRCTXT != '%')
    return usual_char(s);

  /* we then have a '%' */
  SRCTXT++;
  /* don't check for end-of-string ; this is done later */

  /* '%%' escape sequence */
  if (*SRCTXT == '%') {
    if (realloc_buff(s, (size_t)1) == EOF) /* because we can have "%%%%%%%%..." */
      return EOF;
    *DESTTXT = '%';
    DESTTXT++;
    SRCTXT++;
    (s->real_len)++;
    (s->pseudo_len)++;
    return 0;
  }

  /* '%' managing */
  initial_ptr = SRCTXT;   /* save current pointer in case of incorrect */
  /* 'decoding'. Points just after the '%' so the '%' */
  /* won't be printed in any case, as required. */

  /* flag */
  flag_plus = flag_minus = flag_space = flag_sharp = flag_zero = 0;

  for (;; SRCTXT++) {
    if (*SRCTXT == ' ')
      flag_space = 1;
    else if (*SRCTXT == '+')
      flag_plus = 1;
    else if (*SRCTXT == '-')
      flag_minus = 1;
    else if (*SRCTXT == '#')
      flag_sharp = 1;
    else if (*SRCTXT == '0')
      flag_zero = 1;
    else
      break;
  }

  INCOHERENT_TEST();    /* here is the first test for end of string */

  /* width */
  if (*SRCTXT == '*') {         /* width given by next argument */
    SRCTXT++;
    width = va_arg(s->vargs, int);
    if ((size_t)width > 0x3fffU) /* 'size_t' to check against negative values too */
      width = 0x3fff;
  } else if (isdigit((unsigned char)*SRCTXT)) /* width given as ASCII number */
    width = getint(&SRCTXT);
  else
    width = -1;                 /* no width specified */

  INCOHERENT_TEST();

  /* .prec */
  if (*SRCTXT == '.') {
    SRCTXT++;
    if (*SRCTXT == '*') {       /* .prec given by next argument */
      SRCTXT++;
      prec = va_arg(s->vargs, int);
      if ((size_t)prec >= 0x3fffU) /* 'size_t' to check against negative values too */
        prec = 0x3fff;
    } else {                    /* .prec given as ASCII number */
      if (isdigit((unsigned char)*SRCTXT) == 0)
        INCOHERENT();
      prec = getint(&SRCTXT);
    }
    INCOHERENT_TEST();
  } else
    prec = -1;                  /* no .prec specified */

  /* modifier */
  switch (*SRCTXT) {
  case 'L':
  case 'h':
  case 'l':
  case 'z':
  case 't':
    modifier = *SRCTXT;
    SRCTXT++;
    if (modifier=='l' && *SRCTXT=='l') {
      SRCTXT++;
      modifier = 'L';  /* 'll' == 'L'      long long == long double */
    } /* only for compatibility ; not portable */
    INCOHERENT_TEST();
    break;
  default:
    modifier = -1;              /* no modifier specified */
    break;
  }

  /* type */
  type = *SRCTXT;
  if (strchr("diouxXfegEGcspn",type) == NULL)
    INCOHERENT();               /* unknown type */
  SRCTXT++;

  /* rewrite format-string */
  format_string[0] = '%';
  format_ptr = &(format_string[1]);

  if (flag_plus) {
    *format_ptr = '+';
    format_ptr++;
  }
  if (flag_minus) {
    *format_ptr = '-';
    format_ptr++;
  }
  if (flag_space) {
    *format_ptr = ' ';
    format_ptr++;
  }
  if (flag_sharp) {
    *format_ptr = '#';
    format_ptr++;
  }
  if (flag_zero) {
    *format_ptr = '0';
    format_ptr++;
  } /* '0' *must* be the last one */

  if (width != -1) {
    sprintf(format_ptr, "%i", width);
    format_ptr += strlen(format_ptr);
  }

  if (prec != -1) {
    *format_ptr = '.';
    format_ptr++;
    sprintf(format_ptr, "%i", prec);
    format_ptr += strlen(format_ptr);
  }

  if (modifier != -1) {
    if (modifier == 'L' && strchr("diouxX",type) != NULL) {
      *format_ptr = 'l';
      format_ptr++;
      *format_ptr = 'l';
      format_ptr++;
    } else {
      *format_ptr = modifier;
      format_ptr++;
    }
  }

  *format_ptr = type;
  format_ptr++;
  *format_ptr = 0;

  /* vague approximation of minimal length if width or prec are specified */
  approx_width = width + prec;
  if (approx_width < 0) /* because width == -1 and/or prec == -1 */
    approx_width = 0;

  switch (type) {
    /* int */
  case 'd':
  case 'i':
  case 'o':
  case 'u':
  case 'x':
  case 'X':
    switch (modifier) {
    case -1 :
      return print_it(s, (size_t)approx_width, format_string, va_arg(s->vargs, int));
    case 'L':
      return print_it(s, (size_t)approx_width, format_string, va_arg(s->vargs, long long int));
    case 'l':
      return print_it(s, (size_t)approx_width, format_string, va_arg(s->vargs, long int));
    case 'h':
      return print_it(s, (size_t)approx_width, format_string, va_arg(s->vargs, int));
    case 'z':
      return print_it(s, (size_t)approx_width, format_string, va_arg(s->vargs, size_t));
    case 't':
      return print_it(s, (size_t)approx_width, format_string, va_arg(s->vargs, ptrdiff_t));
      /* 'int' instead of 'short int' because default promotion is 'int' */
    default:
      INCOHERENT();
    }

    /* char */
  case 'c':
    if (modifier != -1)
      INCOHERENT();
    return print_it(s, (size_t)approx_width, format_string, va_arg(s->vargs, int));
    /* 'int' instead of 'char' because default promotion is 'int' */

    /* math */
  case 'e':
  case 'f':
  case 'g':
  case 'E':
  case 'G':
    switch (modifier) {
    case -1 : /* because of default promotion, no modifier means 'l' */
    case 'l':
      return print_it(s, (size_t)approx_width, format_string, va_arg(s->vargs, double));
    case 'L':
      return print_it(s, (size_t)approx_width, format_string, va_arg(s->vargs, long double));
    default:
      INCOHERENT();
    }

    /* string */
  case 's':
    return type_s(s, width, prec, format_string, va_arg(s->vargs, const char*));

    /* pointer */
  case 'p':
    if (modifier == -1)
      return print_it(s, (size_t)approx_width, format_string, va_arg(s->vargs, void *));
    INCOHERENT();

    /* store */
  case 'n':
    if (modifier == -1) {
      int * p;
      p = va_arg(s->vargs, int *);
      if (p != NULL) {
        *p = s->pseudo_len;
        return 0;
      }
      return EOF;
    }
    INCOHERENT();

  } /* switch */

  INCOHERENT();                 /* unknown type */

#undef INCOHERENT
#undef INCOHERENT_TEST
#undef SRCTXT
#undef DESTTXT
}

/*
 *  Return value: number of *virtually* written characters
 *                EOF = error
 */
static int core(xprintf_struct *s)
{
  size_t save_len;
  char *dummy_base;

  /* basic checks */
  if ((int)(s->maxlen) <= 0) /* 'int' to check against some conversion */
    return EOF;           /* error for example if value is (int)-10 */
  s->maxlen--;      /* because initial maxlen counts final 0 */
  /* note: now 'maxlen' _can_ be zero */

  if (s->src_string == NULL)
    s->src_string = "(null)";

  /* struct init and memory allocation */
  s->buffer_base = NULL;
  s->buffer_len = 0;
  s->real_len = 0;
  s->pseudo_len = 0;
  if (realloc_buff(s, (size_t)0) == EOF)
    return EOF;
  s->dest_string = s->buffer_base;

  /* process source string */
  for (;;) {
    /* up to end of source string */
    if (*(s->src_string) == 0) {
      *(s->dest_string) = '\0';    /* final NUL */
      break;
    }

    if (dispatch(s) == EOF)
      goto free_EOF;

    /* up to end of dest string */
    if (s->real_len >= s->maxlen) {
      (s->buffer_base)[s->maxlen] = '\0'; /* final NUL */
      break;
    }
  }

  /* for (v)asnprintf */
  dummy_base = s->buffer_base;

  dummy_base = s->buffer_base + s->real_len;
  save_len = s->real_len;

  /* process the remaining of source string to compute 'pseudo_len'. We
   * overwrite again and again, starting at 'dummy_base' because we don't
   * need the text, only char count. */
  while(*(s->src_string) != 0) { /* up to end of source string */
    s->real_len = 0;
    s->dest_string = dummy_base;
    if (dispatch(s) == EOF)
      goto free_EOF;
  }

  s->buffer_base = (char *)realloc((void *)(s->buffer_base), save_len + 1);
  if (s->buffer_base == NULL)
    return EOF; /* should rarely happen because we shrink the buffer */
  return s->pseudo_len;

 free_EOF:
  free(s->buffer_base);
  return EOF;
}

int vasprintf(char **ptr, const char *format_string, va_list vargs)
{
  xprintf_struct s;
  int retval;

  s.src_string = format_string;
#ifdef va_copy
  va_copy (s.vargs, vargs);
#else
# ifdef __va_copy
  __va_copy (s.vargs, vargs);
# else
#  ifdef WIN32
  s.vargs = vargs;
#  else
  memcpy (&s.vargs, &vargs, sizeof (s.va_args));
#  endif /* WIN32 */
# endif /* __va_copy */
#endif /* va_copy */
  s.maxlen = (size_t)INT_MAX;

  retval = core(&s);
  va_end(s.vargs);
  if (retval == EOF) {
    *ptr = NULL;
    return EOF;
  }

  *ptr = s.buffer_base;
  return retval;
}
