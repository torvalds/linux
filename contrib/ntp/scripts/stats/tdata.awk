# program to produce loran tdata statistics from clockstats files
#
# usage: awk -f tdata.awk clockstats
#
# format of input record (missing replaced by -40.0)
# 49228 36.852 127.127.10.1 93:241:00:00:20.812 LORAN TDATA
# M OK 0 0 1169.14 -7.4 3.16E-07 .424
# W CV 0 0 3329.30 -16.4 1.81E-06 
# X OK 0 0 1737.19 -10.5 3.44E-07 .358
# Y OK 0 0 2182.07 -9.0 4.41E-07 .218
#
# format of output record (time in nanoseconds, signal values in dB)
#  MJD      sec      time     M      W      X      Y      Z
# 49228    36.852   175.0   -7.4  -16.4  -10.5   -9.0
#
# select LORAN TDATA records with valid format
{
	if (NF >= 7 && $6 == "TDATA") {
		m = w = x = y = z = -40.0
		for (i = 7; i < NF - 5; i++) {
			if ($i == "M" && $(i+1) == "OK") {
				i += 5
				m = $i
			}
			else if ($i == "W" && $(i+1) == "OK") {
				i += 5
				w = $i
			}
			else if ($i == "X" && $(i+1) == "OK") {
				i += 5
				x = $i
			}
			else if ($i == "Y" && $(i+1) == "OK") {
				i += 5
				y = $i
			}
			else if ($i == "Z" && $(i+1) == "OK") {
				i += 5
				z = $i
			}
                }
		printf "%5s %9.3f %6.1f %6.1f %6.1f %6.1f %6.1f\n", $1, $2, m, w, x, y, z
	}
}

