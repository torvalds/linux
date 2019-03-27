# program to produce external time/frequence statistics from clockstats files
#
# usage: awk -f etf.awk clockstats
#
# format of input record
# 49165 40.473 127.127.10.1 93:178:00:00:39.238 ETF
# +175.0 +176.8 2.0 +3.729E-11 +1.000E-10 +3.511E-11 4.005E-13 500
#
# format of output record (time values in nanoseconds)
#  MJD      sec      time    freq
# 49165    40.473   175.0  3.729e-11
#
# select ETF records with valid format
{
	if (NF >= 9 && $5 == "ETF") {
		printf "%5s %9.3f %7.1f %10.3e\n", $1, $2, $6, $9
	}
}

