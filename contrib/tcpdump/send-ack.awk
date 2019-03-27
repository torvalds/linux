BEGIN	{
	# we need the number of bytes in a packet to do the output
	# in packet numbers rather than byte numbers.
	if (packetsize <= 0)
		packetsize = 512
	expectNext = 1
	lastwin = -1
	}
	{
	# convert tcp trace to send/ack form.
	n = split ($1,t,":")
	tim = t[1]*3600 + t[2]*60 + t[3]
	if (NR <= 1) {
		tzero = tim
		ltim = tim
		OFS = "\t"
	}
	if ($6 != "ack") {
		# we have a data packet record:
		# ignore guys with syn, fin or reset 'cause we
		# can't handle their sequence numbers.  Try to
		# detect and add a flag character for 'anomalies':
		#   * -> re-sent packet
		#   - -> packet after hole (missing packet(s))
		#   # -> odd size packet
		if ($5 !~ /[SFR]/) {
			i = index($6,":")
			j = index($6,"(")
			strtSeq = substr($6,1,i-1)
			endSeq = substr($6,i+1,j-i-1)
			len = endSeq - strtSeq
			id = endSeq
			if (! timeOf[id])
				timeOf[id] = tim
			if (endSeq - expectNext < 0)
				flag = "*"
			else {
				if (strtSeq - expectNext > 0)
					flag = "-"
				else if (len != packetsize)
					flag = "#"
				else
					flag = " "
				expectNext = endSeq
			}
			printf "%7.2f\t%7.2f\t%s send %s %d", tim-tzero, tim-ltim,\
				flag, $5, strtSeq
			if (++timesSent[id] > 1)
				printf "  (%.2f) [%d]", tim - timeOf[id], timesSent[id]
			if (len != packetsize)
				printf " <%d>", len
		}
	} else {
		id = $7

		printf "%7.2f\t%7.2f\t%s  ack %s %d", tim-tzero, tim-ltim,\
			flag, $5, id
		if ($9 != lastwin) {
			printf "  win %d", $9
			lastwin = $9
		}
		printf "  (%.2f)", tim - timeOf[id]
		if (++timesAcked[id] > 1)
			printf " [%d]", timesAcked[id]
	}
	printf "\n"
	ltim = tim
	}
