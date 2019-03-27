# program to produce loran ensemble statistics from clockstats files
#
# usage: awk -f ensemble.awk clockstats
#
# format of input record (time values in seconds)
# 49165 8.628 127.127.10.1 93:178:00:00:07.241 LORAN ENSEMBLE
# -6.43E-08 +5.02E-08 .091 +5.98E-08 +1.59E-08 .909 +4.85E-08 +3.52E-08
#
# format of output record (time values in nanoseconds)
#  MJD       sec     GPS    wgt    LORAN   wgt      avg   sigma
# 49165     8.628   -64.3  0.091    59.8  0.909    48.5    35.2
#
# select LORAN ENSEMBLE records with valid format and weights
{
	if (NF >= 14 && $6 == "ENSEMBLE" && $9 > 0 && $12 > 0)
		printf "%5s %9.3f %7.1f %6.3f %7.1f %6.3f %7.1f %7.1f\n", $1, $2, $7*1e9, $9, $10*1e9, $12, $13*1e9, $14*1e9
}
