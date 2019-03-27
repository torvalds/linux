/* Parse a time duration and return a seconds count
   Copyright (C) 2008-2015 Free Software Foundation, Inc.
   Written by Bruce Korb <bkorb@gnu.org>, 2008.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <config.h>

/* Specification.  */
#include "parse-duration.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intprops.h"

#ifndef NUL
#define NUL '\0'
#endif

#define cch_t char const

typedef enum {
  NOTHING_IS_DONE,
  YEAR_IS_DONE,
  MONTH_IS_DONE,
  WEEK_IS_DONE,
  DAY_IS_DONE,
  HOUR_IS_DONE,
  MINUTE_IS_DONE,
  SECOND_IS_DONE
} whats_done_t;

#define SEC_PER_MIN     60
#define SEC_PER_HR      (SEC_PER_MIN * 60)
#define SEC_PER_DAY     (SEC_PER_HR  * 24)
#define SEC_PER_WEEK    (SEC_PER_DAY * 7)
#define SEC_PER_MONTH   (SEC_PER_DAY * 30)
#define SEC_PER_YEAR    (SEC_PER_DAY * 365)

#undef  MAX_DURATION
#define MAX_DURATION    TYPE_MAXIMUM(time_t)

/* Wrapper around strtoul that does not require a cast.  */
static unsigned long
str_const_to_ul (cch_t * str, cch_t ** ppz, int base)
{
  char * pz;
  int rv = strtoul (str, &pz, base);
  *ppz = pz;
  return rv;
}

/* Wrapper around strtol that does not require a cast.  */
static long
str_const_to_l (cch_t * str, cch_t ** ppz, int base)
{
  char * pz;
  int rv = strtol (str, &pz, base);
  *ppz = pz;
  return rv;
}

/* Returns BASE + VAL * SCALE, interpreting BASE = BAD_TIME
   with errno set as an error situation, and returning BAD_TIME
   with errno set in an error situation.  */
static time_t
scale_n_add (time_t base, time_t val, int scale)
{
  if (base == BAD_TIME)
    {
      if (errno == 0)
        errno = EINVAL;
      return BAD_TIME;
    }

  if (val > MAX_DURATION / scale)
    {
      errno = ERANGE;
      return BAD_TIME;
    }

  val *= scale;
  if (base > MAX_DURATION - val)
    {
      errno = ERANGE;
      return BAD_TIME;
    }

  return base + val;
}

/* After a number HH has been parsed, parse subsequent :MM or :MM:SS.  */
static time_t
parse_hr_min_sec (time_t start, cch_t * pz)
{
  int lpct = 0;

  errno = 0;

  /* For as long as our scanner pointer points to a colon *AND*
     we've not looped before, then keep looping.  (two iterations max) */
  while ((*pz == ':') && (lpct++ <= 1))
    {
      unsigned long v = str_const_to_ul (pz+1, &pz, 10);

      if (errno != 0)
        return BAD_TIME;

      start = scale_n_add (v, start, 60);

      if (errno != 0)
        return BAD_TIME;
    }

  /* allow for trailing spaces */
  while (isspace ((unsigned char)*pz))
    pz++;
  if (*pz != NUL)
    {
      errno = EINVAL;
      return BAD_TIME;
    }

  return start;
}

/* Parses a value and returns BASE + value * SCALE, interpreting
   BASE = BAD_TIME with errno set as an error situation, and returning
   BAD_TIME with errno set in an error situation.  */
static time_t
parse_scaled_value (time_t base, cch_t ** ppz, cch_t * endp, int scale)
{
  cch_t * pz = *ppz;
  time_t val;

  if (base == BAD_TIME)
    return base;

  errno = 0;
  val = str_const_to_ul (pz, &pz, 10);
  if (errno != 0)
    return BAD_TIME;
  while (isspace ((unsigned char)*pz))
    pz++;
  if (pz != endp)
    {
      errno = EINVAL;
      return BAD_TIME;
    }

  *ppz = pz;
  return scale_n_add (base, val, scale);
}

/* Parses the syntax YEAR-MONTH-DAY.
   PS points into the string, after "YEAR", before "-MONTH-DAY".  */
static time_t
parse_year_month_day (cch_t * pz, cch_t * ps)
{
  time_t res = 0;

  res = parse_scaled_value (0, &pz, ps, SEC_PER_YEAR);

  pz++; /* over the first '-' */
  ps = strchr (pz, '-');
  if (ps == NULL)
    {
      errno = EINVAL;
      return BAD_TIME;
    }
  res = parse_scaled_value (res, &pz, ps, SEC_PER_MONTH);

  pz++; /* over the second '-' */
  ps = pz + strlen (pz);
  return parse_scaled_value (res, &pz, ps, SEC_PER_DAY);
}

/* Parses the syntax YYYYMMDD.  */
static time_t
parse_yearmonthday (cch_t * in_pz)
{
  time_t res = 0;
  char   buf[8];
  cch_t * pz;

  if (strlen (in_pz) != 8)
    {
      errno = EINVAL;
      return BAD_TIME;
    }

  memcpy (buf, in_pz, 4);
  buf[4] = NUL;
  pz = buf;
  res = parse_scaled_value (0, &pz, buf + 4, SEC_PER_YEAR);

  memcpy (buf, in_pz + 4, 2);
  buf[2] = NUL;
  pz =   buf;
  res = parse_scaled_value (res, &pz, buf + 2, SEC_PER_MONTH);

  memcpy (buf, in_pz + 6, 2);
  buf[2] = NUL;
  pz =   buf;
  return parse_scaled_value (res, &pz, buf + 2, SEC_PER_DAY);
}

/* Parses the syntax yy Y mm M ww W dd D.  */
static time_t
parse_YMWD (cch_t * pz)
{
  time_t res = 0;
  cch_t * ps = strchr (pz, 'Y');
  if (ps != NULL)
    {
      res = parse_scaled_value (0, &pz, ps, SEC_PER_YEAR);
      pz++;
    }

  ps = strchr (pz, 'M');
  if (ps != NULL)
    {
      res = parse_scaled_value (res, &pz, ps, SEC_PER_MONTH);
      pz++;
    }

  ps = strchr (pz, 'W');
  if (ps != NULL)
    {
      res = parse_scaled_value (res, &pz, ps, SEC_PER_WEEK);
      pz++;
    }

  ps = strchr (pz, 'D');
  if (ps != NULL)
    {
      res = parse_scaled_value (res, &pz, ps, SEC_PER_DAY);
      pz++;
    }

  while (isspace ((unsigned char)*pz))
    pz++;
  if (*pz != NUL)
    {
      errno = EINVAL;
      return BAD_TIME;
    }

  return res;
}

/* Parses the syntax HH:MM:SS.
   PS points into the string, after "HH", before ":MM:SS".  */
static time_t
parse_hour_minute_second (cch_t * pz, cch_t * ps)
{
  time_t res = 0;

  res = parse_scaled_value (0, &pz, ps, SEC_PER_HR);

  pz++;
  ps = strchr (pz, ':');
  if (ps == NULL)
    {
      errno = EINVAL;
      return BAD_TIME;
    }

  res = parse_scaled_value (res, &pz, ps, SEC_PER_MIN);

  pz++;
  ps = pz + strlen (pz);
  return parse_scaled_value (res, &pz, ps, 1);
}

/* Parses the syntax HHMMSS.  */
static time_t
parse_hourminutesecond (cch_t * in_pz)
{
  time_t res = 0;
  char   buf[4];
  cch_t * pz;

  if (strlen (in_pz) != 6)
    {
      errno = EINVAL;
      return BAD_TIME;
    }

  memcpy (buf, in_pz, 2);
  buf[2] = NUL;
  pz = buf;
  res = parse_scaled_value (0, &pz, buf + 2, SEC_PER_HR);

  memcpy (buf, in_pz + 2, 2);
  buf[2] = NUL;
  pz =   buf;
  res = parse_scaled_value (res, &pz, buf + 2, SEC_PER_MIN);

  memcpy (buf, in_pz + 4, 2);
  buf[2] = NUL;
  pz =   buf;
  return parse_scaled_value (res, &pz, buf + 2, 1);
}

/* Parses the syntax hh H mm M ss S.  */
static time_t
parse_HMS (cch_t * pz)
{
  time_t res = 0;
  cch_t * ps = strchr (pz, 'H');
  if (ps != NULL)
    {
      res = parse_scaled_value (0, &pz, ps, SEC_PER_HR);
      pz++;
    }

  ps = strchr (pz, 'M');
  if (ps != NULL)
    {
      res = parse_scaled_value (res, &pz, ps, SEC_PER_MIN);
      pz++;
    }

  ps = strchr (pz, 'S');
  if (ps != NULL)
    {
      res = parse_scaled_value (res, &pz, ps, 1);
      pz++;
    }

  while (isspace ((unsigned char)*pz))
    pz++;
  if (*pz != NUL)
    {
      errno = EINVAL;
      return BAD_TIME;
    }

  return res;
}

/* Parses a time (hours, minutes, seconds) specification in either syntax.  */
static time_t
parse_time (cch_t * pz)
{
  cch_t * ps;
  time_t  res = 0;

  /*
   *  Scan for a hyphen
   */
  ps = strchr (pz, ':');
  if (ps != NULL)
    {
      res = parse_hour_minute_second (pz, ps);
    }

  /*
   *  Try for a 'H', 'M' or 'S' suffix
   */
  else if (ps = strpbrk (pz, "HMS"),
           ps == NULL)
    {
      /* Its a YYYYMMDD format: */
      res = parse_hourminutesecond (pz);
    }

  else
    res = parse_HMS (pz);

  return res;
}

/* Returns a substring of the given string, with spaces at the beginning and at
   the end destructively removed, per SNOBOL.  */
static char *
trim (char * pz)
{
  /* trim leading white space */
  while (isspace ((unsigned char)*pz))
    pz++;

  /* trim trailing white space */
  {
    char * pe = pz + strlen (pz);
    while ((pe > pz) && isspace ((unsigned char)pe[-1]))
      pe--;
    *pe = NUL;
  }

  return pz;
}

/*
 *  Parse the year/months/days of a time period
 */
static time_t
parse_period (cch_t * in_pz)
{
  char * pT;
  char * ps;
  char * pz   = strdup (in_pz);
  void * fptr = pz;
  time_t res  = 0;

  if (pz == NULL)
    {
      errno = ENOMEM;
      return BAD_TIME;
    }

  pT = strchr (pz, 'T');
  if (pT != NULL)
    {
      *(pT++) = NUL;
      pz = trim (pz);
      pT = trim (pT);
    }

  /*
   *  Scan for a hyphen
   */
  ps = strchr (pz, '-');
  if (ps != NULL)
    {
      res = parse_year_month_day (pz, ps);
    }

  /*
   *  Try for a 'Y', 'M' or 'D' suffix
   */
  else if (ps = strpbrk (pz, "YMWD"),
           ps == NULL)
    {
      /* Its a YYYYMMDD format: */
      res = parse_yearmonthday (pz);
    }

  else
    res = parse_YMWD (pz);

  if ((errno == 0) && (pT != NULL))
    {
      time_t val = parse_time (pT);
      res = scale_n_add (res, val, 1);
    }

  free (fptr);
  return res;
}

static time_t
parse_non_iso8601 (cch_t * pz)
{
  whats_done_t whatd_we_do = NOTHING_IS_DONE;

  time_t res = 0;

  do  {
    time_t val;

    errno = 0;
    val = str_const_to_l (pz, &pz, 10);
    if (errno != 0)
      goto bad_time;

    /*  IF we find a colon, then we're going to have a seconds value.
        We will not loop here any more.  We cannot already have parsed
        a minute value and if we've parsed an hour value, then the result
        value has to be less than an hour. */
    if (*pz == ':')
      {
        if (whatd_we_do >= MINUTE_IS_DONE)
          break;

        val = parse_hr_min_sec (val, pz);

        if ((whatd_we_do == HOUR_IS_DONE) && (val >= SEC_PER_HR))
          break;

        return scale_n_add (res, val, 1);
      }

    {
      unsigned int mult;

      /*  Skip over white space following the number we just parsed. */
      while (isspace ((unsigned char)*pz))
        pz++;

      switch (*pz)
        {
        default:  goto bad_time;
        case NUL:
          return scale_n_add (res, val, 1);

        case 'y': case 'Y':
          if (whatd_we_do >= YEAR_IS_DONE)
            goto bad_time;
          mult = SEC_PER_YEAR;
          whatd_we_do = YEAR_IS_DONE;
          break;

        case 'M':
          if (whatd_we_do >= MONTH_IS_DONE)
            goto bad_time;
          mult = SEC_PER_MONTH;
          whatd_we_do = MONTH_IS_DONE;
          break;

        case 'W':
          if (whatd_we_do >= WEEK_IS_DONE)
            goto bad_time;
          mult = SEC_PER_WEEK;
          whatd_we_do = WEEK_IS_DONE;
          break;

        case 'd': case 'D':
          if (whatd_we_do >= DAY_IS_DONE)
            goto bad_time;
          mult = SEC_PER_DAY;
          whatd_we_do = DAY_IS_DONE;
          break;

        case 'h':
          if (whatd_we_do >= HOUR_IS_DONE)
            goto bad_time;
          mult = SEC_PER_HR;
          whatd_we_do = HOUR_IS_DONE;
          break;

        case 'm':
          if (whatd_we_do >= MINUTE_IS_DONE)
            goto bad_time;
          mult = SEC_PER_MIN;
          whatd_we_do = MINUTE_IS_DONE;
          break;

        case 's':
          mult = 1;
          whatd_we_do = SECOND_IS_DONE;
          break;
        }

      res = scale_n_add (res, val, mult);

      pz++;
      while (isspace ((unsigned char)*pz))
        pz++;
      if (*pz == NUL)
        return res;

      if (! isdigit ((unsigned char)*pz))
        break;
    }

  } while (whatd_we_do < SECOND_IS_DONE);

 bad_time:
  errno = EINVAL;
  return BAD_TIME;
}

time_t
parse_duration (char const * pz)
{
  while (isspace ((unsigned char)*pz))
    pz++;

  switch (*pz)
    {
    case 'P':
      return parse_period (pz + 1);

    case 'T':
      return parse_time (pz + 1);

    default:
      if (isdigit ((unsigned char)*pz))
        return parse_non_iso8601 (pz);

      errno = EINVAL;
      return BAD_TIME;
    }
}

/*
 * Local Variables:
 * mode: C
 * c-file-style: "gnu"
 * indent-tabs-mode: nil
 * End:
 * end of parse-duration.c */
