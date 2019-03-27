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

/*

  Readers and users of this function are referred to the ISO-8601
  specification, with particular attention to "Durations".

  At the time of writing, this worked:

  http://en.wikipedia.org/wiki/ISO_8601#Durations

  The string must start with a 'P', 'T' or a digit.

  ==== if it is a digit

  the string may contain:  NNN Y NNN M NNN W NNN d NNN h NNN m NNN s
  This represents NNN years, NNN months, NNN weeks, NNN days, NNN hours,
    NNN minutes and NNN seconds.
  The embedded white space is optional.
  These terms must appear in this order.
  Case is significant:  'M' is months and 'm' is minutes.
  The final "s" is optional.
  All of the terms ("NNN" plus designator) are optional.
  Minutes and seconds may optionally be represented as NNN:NNN.
  Also, hours, minute and seconds may be represented as NNN:NNN:NNN.
  There is no limitation on the value of any of the terms, except
  that the final result must fit in a time_t value.

  ==== if it is a 'P' or 'T', please see ISO-8601 for a rigorous definition.

  The 'P' term may be followed by any of three formats:
    yyyymmdd
    yy-mm-dd
    yy Y mm M ww W dd D

  or it may be empty and followed by a 'T'.  The "yyyymmdd" must be eight
  digits long.

  NOTE!  Months are always 30 days and years are always 365 days long.
  5 years is always 1825 days, not 1826 or 1827 depending on leap year
  considerations.  3 months is always 90 days.  There is no consideration
  for how many days are in the current, next or previous months.

  For the final format:
  *  Embedded white space is allowed, but it is optional.
  *  All of the terms are optional.  Any or all-but-one may be omitted.
  *  The meanings are yy years, mm months, ww weeks and dd days.
  *  The terms must appear in this order.

  ==== The 'T' term may be followed by any of these formats:

    hhmmss
    hh:mm:ss
    hh H mm M ss S

  For the final format:
  *  Embedded white space is allowed, but it is optional.
  *  All of the terms are optional.  Any or all-but-one may be omitted.
  *  The terms must appear in this order.

 */
#ifndef GNULIB_PARSE_DURATION_H
#define GNULIB_PARSE_DURATION_H

#include <time.h>

/* Return value when a valid duration cannot be parsed.  */
#define BAD_TIME        ((time_t)~0)

/* Parses the given string.  If it has the syntax of a valid duration,
   this duration is returned.  Otherwise, the return value is BAD_TIME,
   and errno is set to either EINVAL (bad syntax) or ERANGE (out of range).  */
extern time_t parse_duration (char const * in_pz);

#endif /* GNULIB_PARSE_DURATION_H */
