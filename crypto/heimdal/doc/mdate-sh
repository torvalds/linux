#!/bin/sh
# Get modification time of a file or directory and pretty-print it.
# Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.
# written by Ulrich Drepper <drepper@gnu.ai.mit.edu>, June 1995
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

# Prevent date giving response in another language.
LANG=C
export LANG
LC_ALL=C
export LC_ALL
LC_TIME=C
export LC_TIME

# Get the extended ls output of the file or directory.
# On HPUX /bin/sh, "set" interprets "-rw-r--r--" as options, so the "x" below.
if ls -L /dev/null 1>/dev/null 2>&1; then
  set - x`ls -L -l -d $1`
else
  set - x`ls -l -d $1`
fi
# The month is at least the fourth argument
# (3 shifts here, the next inside the loop).
shift
shift
shift

# Find the month.  Next argument is day, followed by the year or time.
month=
until test $month
do
  shift
  case $1 in
    Jan) month=January; nummonth=1;;
    Feb) month=February; nummonth=2;;
    Mar) month=March; nummonth=3;;
    Apr) month=April; nummonth=4;;
    May) month=May; nummonth=5;;
    Jun) month=June; nummonth=6;;
    Jul) month=July; nummonth=7;;
    Aug) month=August; nummonth=8;;
    Sep) month=September; nummonth=9;;
    Oct) month=October; nummonth=10;;
    Nov) month=November; nummonth=11;;
    Dec) month=December; nummonth=12;;
  esac
done

day=$2

# Here we have to deal with the problem that the ls output gives either
# the time of day or the year.
case $3 in
  *:*) set `date`; eval year=\$$#
       case $2 in
	 Jan) nummonthtod=1;;
	 Feb) nummonthtod=2;;
	 Mar) nummonthtod=3;;
	 Apr) nummonthtod=4;;
	 May) nummonthtod=5;;
	 Jun) nummonthtod=6;;
	 Jul) nummonthtod=7;;
	 Aug) nummonthtod=8;;
	 Sep) nummonthtod=9;;
	 Oct) nummonthtod=10;;
	 Nov) nummonthtod=11;;
	 Dec) nummonthtod=12;;
       esac
       # For the first six month of the year the time notation can also
       # be used for files modified in the last year.
       if (expr $nummonth \> $nummonthtod) > /dev/null;
       then
	 year=`expr $year - 1`
       fi;;
  *) year=$3;;
esac

# The result.
echo $day $month $year
