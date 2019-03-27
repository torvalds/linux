# awk program to scan loopstats files and report errors/statistics
#
# usage: awk -f loop.awk loopstats
#
# format of loopstats record
#  MJD    sec   time (s)  freq (ppm)  poll
# 49235  3.943  0.000016   22.4716    6
#
# format of output dataset (time values in milliseconds, freq in ppm)
# loopstats.19960706
# loop 1180, 0+/-11.0, rms 2.3, freq -24.45+/-0.045, var 0.019
#
BEGIN {
	loop_tmax = loop_fmax = -1e9
	loop_tmin = loop_fmin = 1e9
}
#
# scan all records in file
#
{
	if (NF >= 5) {
		loop_count++
		if ($3 > loop_tmax)
			loop_tmax = $3
		if ($3 < loop_tmin)
			loop_tmin = $3
		if ($4 > loop_fmax)
			loop_fmax = $4
		if ($4 < loop_fmin)
			loop_fmin = $4
		loop_time += $3
		loop_time_rms += $3 * $3
		loop_freq += $4
		loop_freq_rms += $4 * $4
	}
} END {
	if (loop_count > 0) {
		loop_time /= loop_count
                loop_time_rms = sqrt(loop_time_rms / loop_count - loop_time * loop_time)
		loop_freq /= loop_count
		loop_freq_rms = sqrt(loop_freq_rms / loop_count - loop_freq * loop_freq)
		printf "loop %d, %.0f+/-%.1f, rms %.1f, freq %.2f+/-%0.3f, var %.3f\n", loop_count, (loop_tmax + loop_tmin) / 2 * 1e6, (loop_tmax - loop_tmin) / 2 * 1e6, loop_time_rms * 1e6, (loop_fmax + loop_fmin) / 2, (loop_fmax - loop_fmin) / 2, loop_freq_rms
	}
}

