/^ *[0-9]/	{
	# print out the median time to each hop along a route.
	tottime = 0; n = 0;
	for (f = 5; f <= NF; ++f) {
		if ($f == "ms") {
			++n
			time[n] = $(f - 1)
		}
	}
	if (n > 0) {
		# insertion sort the times to find the median
		for (i = 2; i <= n; ++i) {
			v = time[i]; j = i - 1;
			while (time[j] > v) {
				time[j+1] = time[j];
				j = j - 1;
				if (j < 0)
					break;
			}
			time[j+1] = v;
		}
		if (n > 1 && (n % 2) == 0)
			median = (time[n/2] + time[(n/2) + 1]) / 2
		else
			median = time[(n+1)/2]

		print $1, median
	}
}
