# program to produce intewrnal time/frequence statistics from clockstats files
#
# usage: awk -f itf.awk clockstats
#
# format of input record
# 49227 67.846 127.127.10.1 93:240:00:00:51.816 ITF
# COCO 0 +2.0579E-07 -3.1037E-08 -7.7723E-11 +6.5455E-10 500.00 4.962819
#
# format of output record (time values in nanoseconds)
#  MJD      sec      time        freq
# 49227   67.846  +2.0579E-07  -7.7723E-11
#
# select ITF records with valid format
{
	if (NF >= 10 && $5 == "ITF") {
		printf "%5s %9.3f %7.1f %10.3e\n", $1, $2, $8 * 1e9, $10
	}
}

