/* $NetBSD: t_clock_subr.c,v 1.3 2017/01/13 21:30:39 christos Exp $ */

/*
 * Copyright (c) 2016 Jonathan A. Kollasch
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
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2016\
 Jonathan A. Kollasch. All rights reserved.");
__RCSID("$NetBSD: t_clock_subr.c,v 1.3 2017/01/13 21:30:39 christos Exp $");

#include <sys/types.h>
#include <dev/clock_subr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

#include "h_macros.h"

#define FILL(ti,ye,mo,da,wd,ho,mi,se) \
{ .time = (ti), .clock = { .dt_year = (ye), .dt_mon = (mo), .dt_day = (da), \
	  .dt_wday = (wd), .dt_hour = (ho), .dt_min = (mi), .dt_sec = (se), } }

static struct clock_test {
	time_t time;
	struct clock_ymdhms clock;
} const clock_tests[] = {
	FILL(          0,1970, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 1970
	FILL(   15638400,1970, 7, 1,3, 0, 0, 0), // Wed Jul  1 00:00:00 UTC 1970
	FILL(   31536000,1971, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 1971
	FILL(   47174400,1971, 7, 1,4, 0, 0, 0), // Thu Jul  1 00:00:00 UTC 1971
	FILL(   63072000,1972, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 1972
	FILL(   78796800,1972, 7, 1,6, 0, 0, 0), // Sat Jul  1 00:00:00 UTC 1972
	FILL(   94694400,1973, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 1973
	FILL(  110332800,1973, 7, 1,0, 0, 0, 0), // Sun Jul  1 00:00:00 UTC 1973
	FILL(  126230400,1974, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 1974
	FILL(  141868800,1974, 7, 1,1, 0, 0, 0), // Mon Jul  1 00:00:00 UTC 1974
	FILL(  157766400,1975, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 1975
	FILL(  173404800,1975, 7, 1,2, 0, 0, 0), // Tue Jul  1 00:00:00 UTC 1975
	FILL(  189302400,1976, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 1976
	FILL(  205027200,1976, 7, 1,4, 0, 0, 0), // Thu Jul  1 00:00:00 UTC 1976
	FILL(  220924800,1977, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 1977
	FILL(  236563200,1977, 7, 1,5, 0, 0, 0), // Fri Jul  1 00:00:00 UTC 1977
	FILL(  252460800,1978, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 1978
	FILL(  268099200,1978, 7, 1,6, 0, 0, 0), // Sat Jul  1 00:00:00 UTC 1978
	FILL(  283996800,1979, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 1979
	FILL(  299635200,1979, 7, 1,0, 0, 0, 0), // Sun Jul  1 00:00:00 UTC 1979
	FILL(  315532800,1980, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 1980
	FILL(  331257600,1980, 7, 1,2, 0, 0, 0), // Tue Jul  1 00:00:00 UTC 1980
	FILL(  347155200,1981, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 1981
	FILL(  355924803,1981, 4,12,0,12, 0, 3), // Sun Apr 12 12:00:03 UTC 1981
	FILL(  362793600,1981, 7, 1,3, 0, 0, 0), // Wed Jul  1 00:00:00 UTC 1981
	FILL(  378691200,1982, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 1982
	FILL(  394329600,1982, 7, 1,4, 0, 0, 0), // Thu Jul  1 00:00:00 UTC 1982
	FILL(  410227200,1983, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 1983
	FILL(  425865600,1983, 7, 1,5, 0, 0, 0), // Fri Jul  1 00:00:00 UTC 1983
	FILL(  441763200,1984, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 1984
	FILL(  457488000,1984, 7, 1,0, 0, 0, 0), // Sun Jul  1 00:00:00 UTC 1984
	FILL(  473385600,1985, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 1985
	FILL(  489024000,1985, 7, 1,1, 0, 0, 0), // Mon Jul  1 00:00:00 UTC 1985
	FILL(  504921600,1986, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 1986
	FILL(  520560000,1986, 7, 1,2, 0, 0, 0), // Tue Jul  1 00:00:00 UTC 1986
	FILL(  536457600,1987, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 1987
	FILL(  552096000,1987, 7, 1,3, 0, 0, 0), // Wed Jul  1 00:00:00 UTC 1987
	FILL(  567993600,1988, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 1988
	FILL(  583718400,1988, 7, 1,5, 0, 0, 0), // Fri Jul  1 00:00:00 UTC 1988
	FILL(  599616000,1989, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 1989
	FILL(  615254400,1989, 7, 1,6, 0, 0, 0), // Sat Jul  1 00:00:00 UTC 1989
	FILL(  631152000,1990, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 1990
	FILL(  646790400,1990, 7, 1,0, 0, 0, 0), // Sun Jul  1 00:00:00 UTC 1990
	FILL(  662688000,1991, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 1991
	FILL(  678326400,1991, 7, 1,1, 0, 0, 0), // Mon Jul  1 00:00:00 UTC 1991
	FILL(  694224000,1992, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 1992
	FILL(  709948800,1992, 7, 1,3, 0, 0, 0), // Wed Jul  1 00:00:00 UTC 1992
	FILL(  725846400,1993, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 1993
	FILL(  741484800,1993, 7, 1,4, 0, 0, 0), // Thu Jul  1 00:00:00 UTC 1993
	FILL(  757382400,1994, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 1994
	FILL(  773020800,1994, 7, 1,5, 0, 0, 0), // Fri Jul  1 00:00:00 UTC 1994
	FILL(  788918400,1995, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 1995
	FILL(  804556800,1995, 7, 1,6, 0, 0, 0), // Sat Jul  1 00:00:00 UTC 1995
	FILL(  820454400,1996, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 1996
	FILL(  836179200,1996, 7, 1,1, 0, 0, 0), // Mon Jul  1 00:00:00 UTC 1996
	FILL(  852076800,1997, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 1997
	FILL(  867715200,1997, 7, 1,2, 0, 0, 0), // Tue Jul  1 00:00:00 UTC 1997
	FILL(  883612800,1998, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 1998
	FILL(  899251200,1998, 7, 1,3, 0, 0, 0), // Wed Jul  1 00:00:00 UTC 1998
	FILL(  915148800,1999, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 1999
	FILL(  930787200,1999, 7, 1,4, 0, 0, 0), // Thu Jul  1 00:00:00 UTC 1999
	FILL(  946684799,1999,12,31,5,23,59,59), // Fri Dec 31 23:59:59 UTC 1999
	FILL(  946684800,2000, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2000
	FILL(  962409600,2000, 7, 1,6, 0, 0, 0), // Sat Jul  1 00:00:00 UTC 2000
	FILL(  978307200,2001, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2001
	FILL(  993945600,2001, 7, 1,0, 0, 0, 0), // Sun Jul  1 00:00:00 UTC 2001
	FILL( 1009843200,2002, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 2002
	FILL( 1025481600,2002, 7, 1,1, 0, 0, 0), // Mon Jul  1 00:00:00 UTC 2002
	FILL( 1041379200,2003, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 2003
	FILL( 1057017600,2003, 7, 1,2, 0, 0, 0), // Tue Jul  1 00:00:00 UTC 2003
	FILL( 1072915200,2004, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 2004
	FILL( 1088640000,2004, 7, 1,4, 0, 0, 0), // Thu Jul  1 00:00:00 UTC 2004
	FILL( 1104537600,2005, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2005
	FILL( 1120176000,2005, 7, 1,5, 0, 0, 0), // Fri Jul  1 00:00:00 UTC 2005
	FILL( 1136073600,2006, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 2006
	FILL( 1151712000,2006, 7, 1,6, 0, 0, 0), // Sat Jul  1 00:00:00 UTC 2006
	FILL( 1167609600,2007, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2007
	FILL( 1183248000,2007, 7, 1,0, 0, 0, 0), // Sun Jul  1 00:00:00 UTC 2007
	FILL( 1199145600,2008, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 2008
	FILL( 1214870400,2008, 7, 1,2, 0, 0, 0), // Tue Jul  1 00:00:00 UTC 2008
	FILL( 1230768000,2009, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 2009
	FILL( 1246406400,2009, 7, 1,3, 0, 0, 0), // Wed Jul  1 00:00:00 UTC 2009
	FILL( 1262304000,2010, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 2010
	FILL( 1277942400,2010, 7, 1,4, 0, 0, 0), // Thu Jul  1 00:00:00 UTC 2010
	FILL( 1293840000,2011, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2011
	FILL( 1309478400,2011, 7, 1,5, 0, 0, 0), // Fri Jul  1 00:00:00 UTC 2011
	FILL( 1311242220,2011, 7,21,4, 9,57, 0), // Thu Jul 21 09:57:00 UTC 2011
	FILL( 1325376000,2012, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 2012
	FILL( 1341100800,2012, 7, 1,0, 0, 0, 0), // Sun Jul  1 00:00:00 UTC 2012
	FILL( 1356998400,2013, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 2013
	FILL( 1372636800,2013, 7, 1,1, 0, 0, 0), // Mon Jul  1 00:00:00 UTC 2013
	FILL( 1388534400,2014, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 2014
	FILL( 1404172800,2014, 7, 1,2, 0, 0, 0), // Tue Jul  1 00:00:00 UTC 2014
	FILL( 1420070400,2015, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 2015
	FILL( 1435708800,2015, 7, 1,3, 0, 0, 0), // Wed Jul  1 00:00:00 UTC 2015
	FILL( 1451606400,2016, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 2016
	FILL( 1467331200,2016, 7, 1,5, 0, 0, 0), // Fri Jul  1 00:00:00 UTC 2016
	FILL( 1483228800,2017, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 2017
	FILL( 1498867200,2017, 7, 1,6, 0, 0, 0), // Sat Jul  1 00:00:00 UTC 2017
	FILL( 1514764800,2018, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2018
	FILL( 1530403200,2018, 7, 1,0, 0, 0, 0), // Sun Jul  1 00:00:00 UTC 2018
	FILL( 1546300800,2019, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 2019
	FILL( 1561939200,2019, 7, 1,1, 0, 0, 0), // Mon Jul  1 00:00:00 UTC 2019
	FILL( 1577836800,2020, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 2020
	FILL( 1593561600,2020, 7, 1,3, 0, 0, 0), // Wed Jul  1 00:00:00 UTC 2020
	FILL( 1609459200,2021, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 2021
	FILL( 1625097600,2021, 7, 1,4, 0, 0, 0), // Thu Jul  1 00:00:00 UTC 2021
	FILL( 1640995200,2022, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2022
	FILL( 1656633600,2022, 7, 1,5, 0, 0, 0), // Fri Jul  1 00:00:00 UTC 2022
	FILL( 1672531200,2023, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 2023
	FILL( 1688169600,2023, 7, 1,6, 0, 0, 0), // Sat Jul  1 00:00:00 UTC 2023
	FILL( 1704067200,2024, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2024
	FILL( 1719792000,2024, 7, 1,1, 0, 0, 0), // Mon Jul  1 00:00:00 UTC 2024
	FILL( 1735689599,2024,12,31,2,23,59,59), // Tue Dec 31 23:59:59 UTC 2024
	FILL( 1735689600,2025, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 2025
	FILL( 1751328000,2025, 7, 1,2, 0, 0, 0), // Tue Jul  1 00:00:00 UTC 2025
	FILL( 1767225600,2026, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 2026
	FILL( 1782864000,2026, 7, 1,3, 0, 0, 0), // Wed Jul  1 00:00:00 UTC 2026
	FILL( 1798761600,2027, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 2027
	FILL( 1814400000,2027, 7, 1,4, 0, 0, 0), // Thu Jul  1 00:00:00 UTC 2027
	FILL( 1830297600,2028, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2028
	FILL( 1846022400,2028, 7, 1,6, 0, 0, 0), // Sat Jul  1 00:00:00 UTC 2028
	FILL( 1861920000,2029, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2029
	FILL( 1877558400,2029, 7, 1,0, 0, 0, 0), // Sun Jul  1 00:00:00 UTC 2029
	FILL( 1893456000,2030, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 2030
	FILL( 1909094400,2030, 7, 1,1, 0, 0, 0), // Mon Jul  1 00:00:00 UTC 2030
	FILL( 2147483647,2038, 1,19,2, 3,14, 7), // Tue Jan 19 03:14:07 UTC 2038
	FILL( 2147483648,2038, 1,19,2, 3,14, 8), // Tue Jan 19 03:14:08 UTC 2038
	FILL( 2524607999,2049,12,31,5,23,59,59), // Fri Dec 31 23:59:59 UTC 2049
	FILL( 2524608000,2050, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2050
	FILL( 2556144000,2051, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 2051
	FILL( 2942956800,2063, 4, 5,4, 0, 0, 0), // Thu Apr  5 00:00:00 UTC 2063
	FILL( 3313526399,2074,12,31,1,23,59,59), // Mon Dec 31 23:59:59 UTC 2074
	FILL( 3313526400,2075, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 2075
	FILL( 3345062400,2076, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 2076
	FILL( 4102444799,2099,12,31,4,23,59,59), // Thu Dec 31 23:59:59 UTC 2099
	FILL( 4102444800,2100, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 2100
	FILL( 4133980800,2101, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2101
	FILL( 4891363199,2124,12,31,0,23,59,59), // Sun Dec 31 23:59:59 UTC 2124
	FILL( 4891363200,2125, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2125
	FILL( 4922899200,2126, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 2126
	FILL( 5680281599,2149,12,31,3,23,59,59), // Wed Dec 31 23:59:59 UTC 2149
	FILL( 5680281600,2150, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 2150
	FILL( 5711817600,2151, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 2151
	FILL( 6469199999,2174,12,31,6,23,59,59), // Sat Dec 31 23:59:59 UTC 2174
	FILL( 6469200000,2175, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 2175
	FILL( 6500736000,2176, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2176
	FILL( 7258118399,2199,12,31,2,23,59,59), // Tue Dec 31 23:59:59 UTC 2199
	FILL( 7258118400,2200, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 2200
	FILL( 7289654400,2201, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 2201
	FILL( 8047036799,2224,12,31,5,23,59,59), // Fri Dec 31 23:59:59 UTC 2224
	FILL( 8047036800,2225, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2225
	FILL( 8078572800,2226, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 2226
	FILL( 8835955199,2249,12,31,1,23,59,59), // Mon Dec 31 23:59:59 UTC 2249
	FILL( 8835955200,2250, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 2250
	FILL( 8867491200,2251, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 2251
	FILL( 9624873599,2274,12,31,4,23,59,59), // Thu Dec 31 23:59:59 UTC 2274
	FILL( 9624873600,2275, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 2275
	FILL( 9656409600,2276, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2276
	FILL(10413791999,2299,12,31,0,23,59,59), // Sun Dec 31 23:59:59 UTC 2299
	FILL(10413792000,2300, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2300
	FILL(10445328000,2301, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 2301
	FILL(11202710399,2324,12,31,3,23,59,59), // Wed Dec 31 23:59:59 UTC 2324
	FILL(11202710400,2325, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 2325
	FILL(11234246400,2326, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 2326
	FILL(11991628799,2349,12,31,6,23,59,59), // Sat Dec 31 23:59:59 UTC 2349
	FILL(11991628800,2350, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 2350
	FILL(12023164800,2351, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2351
	FILL(12780547199,2374,12,31,2,23,59,59), // Tue Dec 31 23:59:59 UTC 2374
	FILL(12780547200,2375, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 2375
	FILL(12812083200,2376, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 2376
	FILL(13569465599,2399,12,31,5,23,59,59), // Fri Dec 31 23:59:59 UTC 2399
	FILL(13569465600,2400, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2400
	FILL(13601088000,2401, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2401
	FILL(14358470399,2424,12,31,2,23,59,59), // Tue Dec 31 23:59:59 UTC 2424
	FILL(14358470400,2425, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 2425
	FILL(14390006400,2426, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 2426
	FILL(15147388799,2449,12,31,5,23,59,59), // Fri Dec 31 23:59:59 UTC 2449
	FILL(15147388800,2450, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2450
	FILL(15178924800,2451, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 2451
	FILL(15936307199,2474,12,31,1,23,59,59), // Mon Dec 31 23:59:59 UTC 2474
	FILL(15936307200,2475, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 2475
	FILL(15967843200,2476, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 2476
	FILL(16725225599,2499,12,31,4,23,59,59), // Thu Dec 31 23:59:59 UTC 2499
	FILL(16725225600,2500, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 2500
	FILL(16756761600,2501, 1, 1,6, 0, 0, 0), // Sat Jan  1 00:00:00 UTC 2501
	FILL(17514143999,2524,12,31,0,23,59,59), // Sun Dec 31 23:59:59 UTC 2524
	FILL(17514144000,2525, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2525
	FILL(17545680000,2526, 1, 1,2, 0, 0, 0), // Tue Jan  1 00:00:00 UTC 2526
	FILL(18303062399,2549,12,31,3,23,59,59), // Wed Dec 31 23:59:59 UTC 2549
	FILL(18303062400,2550, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 2550
	FILL(18334598400,2551, 1, 1,5, 0, 0, 0), // Fri Jan  1 00:00:00 UTC 2551
	FILL(19091980799,2574,12,31,6,23,59,59), // Sat Dec 31 23:59:59 UTC 2574
	FILL(19091980800,2575, 1, 1,0, 0, 0, 0), // Sun Jan  1 00:00:00 UTC 2575
	FILL(19123516800,2576, 1, 1,1, 0, 0, 0), // Mon Jan  1 00:00:00 UTC 2576
	FILL(19880899199,2599,12,31,2,23,59,59), // Tue Dec 31 23:59:59 UTC 2599
	FILL(19880899200,2600, 1, 1,3, 0, 0, 0), // Wed Jan  1 00:00:00 UTC 2600
	FILL(19912435200,2601, 1, 1,4, 0, 0, 0), // Thu Jan  1 00:00:00 UTC 2601
};
#undef FILL

ATF_TC(ymdhms_to_secs);
ATF_TC_HEAD(ymdhms_to_secs, tc)
{

	atf_tc_set_md_var(tc, "descr", "check clock_ymdhms_to_secs");
}
ATF_TC_BODY(ymdhms_to_secs, tc)
{
	time_t secs;
	size_t i;

	for (i = 0; i < __arraycount(clock_tests); i++) {
		secs = clock_ymdhms_to_secs(__UNCONST(&clock_tests[i].clock));
		ATF_CHECK_EQ_MSG(clock_tests[i].time, secs, "%jd != %jd",
		    (intmax_t)clock_tests[i].time, (intmax_t)secs);
	}
}

ATF_TC(secs_to_ymdhms);
ATF_TC_HEAD(secs_to_ymdhms, tc)
{

	atf_tc_set_md_var(tc, "descr", "check clock_secs_to_ymdhms");
}
ATF_TC_BODY(secs_to_ymdhms, tc)
{
	struct clock_ymdhms ymdhms;
	size_t i;

#define CHECK_FIELD(f) \
	ATF_CHECK_EQ_MSG(ymdhms.dt_##f, clock_tests[i].clock.dt_##f, \
	    "%jd != %jd for %jd", (intmax_t)ymdhms.dt_##f, \
	    (intmax_t)clock_tests[i].clock.dt_##f, \
	    (intmax_t)clock_tests[i].time)

	for (i = 0; i < __arraycount(clock_tests); i++) {
		clock_secs_to_ymdhms(clock_tests[i].time, &ymdhms);
		CHECK_FIELD(year);
		CHECK_FIELD(mon);
		CHECK_FIELD(day);
		CHECK_FIELD(wday);
		CHECK_FIELD(hour);
		CHECK_FIELD(min);
		CHECK_FIELD(sec);
	}
#undef CHECK_FIELD
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, ymdhms_to_secs);
	ATF_TP_ADD_TC(tp, secs_to_ymdhms);

	return atf_no_error();
}
