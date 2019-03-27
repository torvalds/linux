# Generate the 'leapseconds' file from 'leap-seconds.list'.

# This file is in the public domain.

BEGIN {
  print "# Allowance for leap seconds added to each time zone file."
  print ""
  print "# This file is in the public domain."
  print ""
  print "# This file is generated automatically from the data in the public-domain"
  print "# leap-seconds.list file, which can be copied from"
  print "# <ftp://ftp.nist.gov/pub/time/leap-seconds.list>"
  print "# or <ftp://ftp.boulder.nist.gov/pub/time/leap-seconds.list>"
  print "# or <ftp://tycho.usno.navy.mil/pub/ntp/leap-seconds.list>."
  print "# For more about leap-seconds.list, please see"
  print "# The NTP Timescale and Leap Seconds"
  print "# <https://www.eecis.udel.edu/~mills/leap.html>."
  print ""
  print "# The International Earth Rotation and Reference Systems Service"
  print "# periodically uses leap seconds to keep UTC to within 0.9 s of UT1"
  print "# (which measures the true angular orientation of the earth in space)"
  print "# and publishes leap second data in a copyrighted file"
  print "# <https://hpiers.obspm.fr/iers/bul/bulc/Leap_Second.dat>."
  print "# See: Levine J. Coordinated Universal Time and the leap second."
  print "# URSI Radio Sci Bull. 2016;89(4):30-6. doi:10.23919/URSIRSB.2016.7909995"
  print "# <https://ieeexplore.ieee.org/document/7909995>."
  print ""
  print "# There were no leap seconds before 1972, because the official mechanism"
  print "# accounting for the discrepancy between atomic time and the earth's rotation"
  print "# did not exist.  The first (\"1 Jan 1972\") data line in leap-seconds.list"
  print "# does not denote a leap second; it denotes the start of the current definition"
  print"# of UTC."
  print ""
  print "# The correction (+ or -) is made at the given time, so lines"
  print "# will typically look like:"
  print "#	Leap	YEAR	MON	DAY	23:59:60	+	R/S"
  print "# or"
  print "#	Leap	YEAR	MON	DAY	23:59:59	-	R/S"
  print ""
  print "# If the leap second is Rolling (R) the given time is local time (unused here)."

  monthabbr[ 1] = "Jan"
  monthabbr[ 2] = "Feb"
  monthabbr[ 3] = "Mar"
  monthabbr[ 4] = "Apr"
  monthabbr[ 5] = "May"
  monthabbr[ 6] = "Jun"
  monthabbr[ 7] = "Jul"
  monthabbr[ 8] = "Aug"
  monthabbr[ 9] = "Sep"
  monthabbr[10] = "Oct"
  monthabbr[11] = "Nov"
  monthabbr[12] = "Dec"
  for (i in monthabbr) {
      monthnum[monthabbr[i]] = i
      monthlen[i] = 31
  }
  monthlen[2] = 28
  monthlen[4] = monthlen[6] = monthlen[9] = monthlen[11] = 30
}

/^#\tUpdated through/ || /^#\tFile expires on:/ {
    last_lines = last_lines $0 "\n"
}

/^#[$][ \t]/ { updated = $2 }
/^#[@][ \t]/ { expires = $2 }

/^#/ { next }

{
    NTP_timestamp = $1
    TAI_minus_UTC = $2
    hash_mark = $3
    one = $4
    month = $5
    year = $6
    if (old_TAI_minus_UTC) {
	if (old_TAI_minus_UTC < TAI_minus_UTC) {
	    sign = "23:59:60\t+"
	} else {
	    sign = "23:59:59\t-"
	}
	m = monthnum[month] - 1
	if (m == 0) {
	    year--;
	    m = 12
	}
	month = monthabbr[m]
	day = monthlen[m]
	day += m == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)
	printf "Leap\t%s\t%s\t%s\t%s\tS\n", year, month, day, sign
    }
    old_TAI_minus_UTC = TAI_minus_UTC
}

END {
    # The difference between the NTP and POSIX epochs is 70 years
    # (including 17 leap days), each 24 hours of 60 minutes of 60
    # seconds each.
    epoch_minus_NTP = ((1970 - 1900) * 365 + 17) * 24 * 60 * 60

    print ""
    print "# POSIX timestamps for the data in this file:"
    printf "#updated %s\n", updated - epoch_minus_NTP
    printf "#expires %s\n", expires - epoch_minus_NTP
    printf "\n%s", last_lines
}
