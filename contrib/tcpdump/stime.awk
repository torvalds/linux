$6 !~ /^ack/ && $5 !~ /[SFR]/ 	{
	# given a tcpdump ftp trace, output one line for each send
	# in the form
	#   <send time> <seq no>
	# where <send time> is the time packet was sent (in seconds with
	# zero at time of first packet) and <seq no> is the tcp sequence
	# number of the packet divided by 1024 (i.e., Kbytes sent).
	#
	# convert time to seconds
	n = split ($1,t,":")
	tim = t[1]*3600 + t[2]*60 + t[3]
	if (! tzero) {
		tzero = tim
		OFS = "\t"
	}
	# get packet sequence number
	i = index($6,":")
	printf "%7.2f\t%g\n", tim-tzero, substr($6,1,i-1)/1024
	}
