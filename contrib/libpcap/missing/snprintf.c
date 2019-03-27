/*
 * Copyright (c) 1995-1999 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#include <pcap-int.h>

enum format_flags {
    minus_flag     =  1,
    plus_flag      =  2,
    space_flag     =  4,
    alternate_flag =  8,
    zero_flag      = 16
};

/*
 * Common state
 */

struct state {
  unsigned char *str;
  unsigned char *s;
  unsigned char *theend;
  size_t sz;
  size_t max_sz;
  int (*append_char)(struct state *, unsigned char);
  int (*reserve)(struct state *, size_t);
  /* XXX - methods */
};

#ifndef HAVE_VSNPRINTF
static int
sn_reserve (struct state *state, size_t n)
{
  return state->s + n > state->theend;
}

static int
sn_append_char (struct state *state, unsigned char c)
{
  if (sn_reserve (state, 1)) {
    return 1;
  } else {
    *state->s++ = c;
    return 0;
  }
}
#endif

#if 0
static int
as_reserve (struct state *state, size_t n)
{
  if (state->s + n > state->theend) {
    int off = state->s - state->str;
    unsigned char *tmp;

    if (state->max_sz && state->sz >= state->max_sz)
      return 1;

    state->sz = max(state->sz * 2, state->sz + n);
    if (state->max_sz)
      state->sz = min(state->sz, state->max_sz);
    tmp = realloc (state->str, state->sz);
    if (tmp == NULL)
      return 1;
    state->str = tmp;
    state->s = state->str + off;
    state->theend = state->str + state->sz - 1;
  }
  return 0;
}

static int
as_append_char (struct state *state, unsigned char c)
{
  if(as_reserve (state, 1))
    return 1;
  else {
    *state->s++ = c;
    return 0;
  }
}
#endif

static int
append_number(struct state *state,
	      unsigned long num, unsigned base, char *rep,
	      int width, int prec, int flags, int minusp)
{
  int len = 0;
  int i;

  /* given precision, ignore zero flag */
  if(prec != -1)
    flags &= ~zero_flag;
  else
    prec = 1;
  /* zero value with zero precision -> "" */
  if(prec == 0 && num == 0)
    return 0;
  do{
    if((*state->append_char)(state, rep[num % base]))
      return 1;
    len++;
    num /= base;
  }while(num);
  prec -= len;
  /* pad with prec zeros */
  while(prec-- > 0){
    if((*state->append_char)(state, '0'))
      return 1;
    len++;
  }
  /* add length of alternate prefix (added later) to len */
  if(flags & alternate_flag && (base == 16 || base == 8))
    len += base / 8;
  /* pad with zeros */
  if(flags & zero_flag){
    width -= len;
    if(minusp || (flags & space_flag) || (flags & plus_flag))
      width--;
    while(width-- > 0){
      if((*state->append_char)(state, '0'))
	return 1;
      len++;
    }
  }
  /* add alternate prefix */
  if(flags & alternate_flag && (base == 16 || base == 8)){
    if(base == 16)
      if((*state->append_char)(state, rep[10] + 23)) /* XXX */
	return 1;
    if((*state->append_char)(state, '0'))
      return 1;
  }
  /* add sign */
  if(minusp){
    if((*state->append_char)(state, '-'))
      return 1;
    len++;
  } else if(flags & plus_flag) {
    if((*state->append_char)(state, '+'))
      return 1;
    len++;
  } else if(flags & space_flag) {
    if((*state->append_char)(state, ' '))
      return 1;
    len++;
  }
  if(flags & minus_flag)
    /* swap before padding with spaces */
    for(i = 0; i < len / 2; i++){
      char c = state->s[-i-1];
      state->s[-i-1] = state->s[-len+i];
      state->s[-len+i] = c;
    }
  width -= len;
  while(width-- > 0){
    if((*state->append_char)(state,  ' '))
      return 1;
    len++;
  }
  if(!(flags & minus_flag))
    /* swap after padding with spaces */
    for(i = 0; i < len / 2; i++){
      char c = state->s[-i-1];
      state->s[-i-1] = state->s[-len+i];
      state->s[-len+i] = c;
    }

  return 0;
}

static int
append_string (struct state *state,
	       unsigned char *arg,
	       int width,
	       int prec,
	       int flags)
{
  if(prec != -1)
    width -= prec;
  else
    width -= strlen((char *)arg);
  if(!(flags & minus_flag))
    while(width-- > 0)
      if((*state->append_char) (state, ' '))
	return 1;
  if (prec != -1) {
    while (*arg && prec--)
      if ((*state->append_char) (state, *arg++))
	return 1;
  } else {
    while (*arg)
      if ((*state->append_char) (state, *arg++))
	return 1;
  }
  if(flags & minus_flag)
    while(width-- > 0)
      if((*state->append_char) (state, ' '))
	return 1;
  return 0;
}

static int
append_char(struct state *state,
	    unsigned char arg,
	    int width,
	    int flags)
{
  while(!(flags & minus_flag) && --width > 0)
    if((*state->append_char) (state, ' '))
      return 1;

  if((*state->append_char) (state, arg))
    return 1;
  while((flags & minus_flag) && --width > 0)
    if((*state->append_char) (state, ' '))
      return 1;

  return 0;
}

/*
 * This can't be made into a function...
 */

#define PARSE_INT_FORMAT(res, arg, unsig) \
if (long_flag) \
     res = (unsig long)va_arg(arg, unsig long); \
else if (short_flag) \
     res = (unsig short)va_arg(arg, unsig int); \
else \
     res = (unsig int)va_arg(arg, unsig int)

/*
 * zyxprintf - return 0 or -1
 */

static int
xyzprintf (struct state *state, const char *char_format, va_list ap)
{
  const unsigned char *format = (const unsigned char *)char_format;
  unsigned char c;

  while((c = *format++)) {
    if (c == '%') {
      int flags      = 0;
      int width      = 0;
      int prec       = -1;
      int long_flag  = 0;
      int short_flag = 0;

      /* flags */
      while((c = *format++)){
	if(c == '-')
	  flags |= minus_flag;
	else if(c == '+')
	  flags |= plus_flag;
	else if(c == ' ')
	  flags |= space_flag;
	else if(c == '#')
	  flags |= alternate_flag;
	else if(c == '0')
	  flags |= zero_flag;
	else
	  break;
      }

      if((flags & space_flag) && (flags & plus_flag))
	flags ^= space_flag;

      if((flags & minus_flag) && (flags & zero_flag))
	flags ^= zero_flag;

      /* width */
      if (isdigit(c))
	do {
	  width = width * 10 + c - '0';
	  c = *format++;
	} while(isdigit(c));
      else if(c == '*') {
	width = va_arg(ap, int);
	c = *format++;
      }

      /* precision */
      if (c == '.') {
	prec = 0;
	c = *format++;
	if (isdigit(c))
	  do {
	    prec = prec * 10 + c - '0';
	    c = *format++;
	  } while(isdigit(c));
	else if (c == '*') {
	  prec = va_arg(ap, int);
	  c = *format++;
	}
      }

      /* size */

      if (c == 'h') {
	short_flag = 1;
	c = *format++;
      } else if (c == 'l') {
	long_flag = 1;
	c = *format++;
      }

      switch (c) {
      case 'c' :
	if(append_char(state, va_arg(ap, int), width, flags))
	  return -1;
	break;
      case 's' :
	if (append_string(state,
			  va_arg(ap, unsigned char*),
			  width,
			  prec,
			  flags))
	  return -1;
	break;
      case 'd' :
      case 'i' : {
	long arg;
	unsigned long num;
	int minusp = 0;

	PARSE_INT_FORMAT(arg, ap, signed);

	if (arg < 0) {
	  minusp = 1;
	  num = -arg;
	} else
	  num = arg;

	if (append_number (state, num, 10, "0123456789",
			   width, prec, flags, minusp))
	  return -1;
	break;
      }
      case 'u' : {
	unsigned long arg;

	PARSE_INT_FORMAT(arg, ap, unsigned);

	if (append_number (state, arg, 10, "0123456789",
			   width, prec, flags, 0))
	  return -1;
	break;
      }
      case 'o' : {
	unsigned long arg;

	PARSE_INT_FORMAT(arg, ap, unsigned);

	if (append_number (state, arg, 010, "01234567",
			   width, prec, flags, 0))
	  return -1;
	break;
      }
      case 'x' : {
	unsigned long arg;

	PARSE_INT_FORMAT(arg, ap, unsigned);

	if (append_number (state, arg, 0x10, "0123456789abcdef",
			   width, prec, flags, 0))
	  return -1;
	break;
      }
      case 'X' :{
	unsigned long arg;

	PARSE_INT_FORMAT(arg, ap, unsigned);

	if (append_number (state, arg, 0x10, "0123456789ABCDEF",
			   width, prec, flags, 0))
	  return -1;
	break;
      }
      case 'p' : {
	unsigned long arg = (unsigned long)va_arg(ap, void*);

	if (append_number (state, arg, 0x10, "0123456789ABCDEF",
			   width, prec, flags, 0))
	  return -1;
	break;
      }
      case 'n' : {
	int *arg = va_arg(ap, int*);
	*arg = state->s - state->str;
	break;
      }
      case '\0' :
	  --format;
	  /* FALLTHROUGH */
      case '%' :
	if ((*state->append_char)(state, c))
	  return -1;
	break;
      default :
	if (   (*state->append_char)(state, '%')
	    || (*state->append_char)(state, c))
	  return -1;
	break;
      }
    } else
      if ((*state->append_char) (state, c))
	return -1;
  }
  return 0;
}

#ifndef HAVE_SNPRINTF
int
pcap_snprintf (char *str, size_t sz, const char *format, ...)
{
  va_list args;
  int ret;

  va_start(args, format);
  ret = pcap_vsnprintf (str, sz, format, args);

#ifdef PARANOIA
  {
    int ret2;
    char *tmp;

    tmp = malloc (sz);
    if (tmp == NULL)
      abort ();

    ret2 = pcap_vsprintf (tmp, format, args);
    if (ret != ret2 || strcmp(str, tmp))
      abort ();
    free (tmp);
  }
#endif

  va_end(args);
  return ret;
}
#endif

#if 0
#ifndef HAVE_ASPRINTF
int
asprintf (char **ret, const char *format, ...)
{
  va_list args;
  int val;

  va_start(args, format);
  val = vasprintf (ret, format, args);

#ifdef PARANOIA
  {
    int ret2;
    char *tmp;
    tmp = malloc (val + 1);
    if (tmp == NULL)
      abort ();

    ret2 = vsprintf (tmp, format, args);
    if (val != ret2 || strcmp(*ret, tmp))
      abort ();
    free (tmp);
  }
#endif

  va_end(args);
  return val;
}
#endif

#ifndef HAVE_ASNPRINTF
int
pcap_asnprintf (char **ret, size_t max_sz, const char *format, ...)
{
  va_list args;
  int val;

  va_start(args, format);
  val = pcap_vasnprintf (ret, max_sz, format, args);

#ifdef PARANOIA
  {
    int ret2;
    char *tmp;
    tmp = malloc (val + 1);
    if (tmp == NULL)
      abort ();

    ret2 = pcap_vsprintf (tmp, format, args);
    if (val != ret2 || strcmp(*ret, tmp))
      abort ();
    free (tmp);
  }
#endif

  va_end(args);
  return val;
}
#endif

#ifndef HAVE_VASPRINTF
int
pcap_vasprintf (char **ret, const char *format, va_list args)
{
  return pcap_vasnprintf (ret, 0, format, args);
}
#endif


#ifndef HAVE_VASNPRINTF
int
pcap_vasnprintf (char **ret, size_t max_sz, const char *format, va_list args)
{
  int st;
  size_t len;
  struct state state;

  state.max_sz = max_sz;
  state.sz     = 1;
  state.str    = malloc(state.sz);
  if (state.str == NULL) {
    *ret = NULL;
    return -1;
  }
  state.s = state.str;
  state.theend = state.s + state.sz - 1;
  state.append_char = as_append_char;
  state.reserve     = as_reserve;

  st = xyzprintf (&state, format, args);
  if (st) {
    free (state.str);
    *ret = NULL;
    return -1;
  } else {
    char *tmp;

    *state.s = '\0';
    len = state.s - state.str;
    tmp = realloc (state.str, len+1);
    if (tmp == NULL) {
      free (state.str);
      *ret = NULL;
      return -1;
    }
    *ret = tmp;
    return len;
  }
}
#endif
#endif

#ifndef HAVE_VSNPRINTF
int
pcap_vsnprintf (char *str, size_t sz, const char *format, va_list args)
{
  struct state state;
  int ret;
  unsigned char *ustr = (unsigned char *)str;

  state.max_sz = 0;
  state.sz     = sz;
  state.str    = ustr;
  state.s      = ustr;
  state.theend = ustr + sz - 1;
  state.append_char = sn_append_char;
  state.reserve     = sn_reserve;

  ret = xyzprintf (&state, format, args);
  *state.s = '\0';
  if (ret)
    return sz;
  else
    return state.s - state.str;
}
#endif

