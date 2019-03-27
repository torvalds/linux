$6 ~ /^ack/ && $5 !~ /[SFR]/ 	{
	# given a tcpdump ftp trace, output one line for each ack
	# in the form
	#   <ack time> <seq no>
	# where <ack time> is the time packet was acked (in seconds with
	# zero at time of first packet) and <seq no> is the tcp sequence
	# number of the ack divided by 1024 (i.e., Kbytes acked).
	#
	# convert time to seconds
	n = split ($1,t,":")
	tim = t[1]*3600 + t[2]*60 + t[3]
	if (! tzero) {
		tzero = tim
		OFS = "\t"
	}
	# get packet sequence number
	printf "%7.2f\t%g\n", tim-tzero, $7/1024
	}
