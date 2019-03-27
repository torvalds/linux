#!/bin/sh
#
# Script to summarize ipeerstats, loopstats and clockstats files
#
# This script can be run from a cron job once per day, week or month. It
# runs the file-specific summary script and appends the summary data to 
# designated files.
#
DATE=`date +20%y%m%d`
S=/usr/local/bin/S
SIN=S.in
SOUT=S.out
LOOP=loop_summary
PEER=peer_summary
CLOCK=clock_summary

rm -f $SIN $SOUT

#
# Summarize loopstats files
#
for f in loopstats.[12][0-9][0-9][0-9][0-1][0-9][0-3][0-9]; do
	d=`echo $f | cut -f2 -d.`
	if [ -f $f ] && [ $DATE != $d ]; then
		echo " " >>$LOOP
		echo $f >>$LOOP
		awk -f loop.awk $f >>$LOOP
		if [ -f $S ]; then
			echo "file1<-"\"${f}\" >>$SIN
			echo "source("\""loop.S"\"")" >>$SIN
			echo "unix("\""rm ${f}"\"")" >>$SIN
		else
			rm -f $f
		fi
	fi
done

#
# Summarize peerstats files
#
for f in peerstats.199[4-9][0-1][0-9][0-3][0-9]; do
	d=`echo $f | cut -f2 -d.`
	if [ -f $f ] && [ $DATE != $d ]; then
		echo " " >>$PEER
		echo $f >>$PEER
		awk -f peer.awk $f >>$PEER
		rm -f $f
	fi
done

#
# Summarize clockstats files
#
for f in clockstats.199[4-9][0-1][0-9][0-3][0-9]; do
	d=`echo $f | cut -f2 -d.`
	if [ -f $f ] && [ $DATE != $d ]; then
		echo " " >>$CLOCK
		echo $f >>$CLOCK
		awk -f clock.awk $f >>$CLOCK
		if [ -f /dev/gps[0-9] ]; then
			awk -f itf.awk $f >itf.$d
			awk -f etf.awk $f >etf.$d
			awk -f ensemble.awk $f >ensemble.$d
			awk -f tdata.awk $f >tdata.$d
		fi
		rm -f $f
	fi
done

#
# Process clockstat files with S and generate PostScript plots
#
for f in itf etf ensemble tdata; do
	for d in ${f}.199[4-9][0-1][0-9][0-3][0-9]; do
		if [ -f $d ]; then
			if [ -f $S ]; then
				echo "file1<-"\"${d}\" >>$SIN
				echo "source("\"${f}.S\"")" >>$SIN
				echo "unix("\""rm ${d}"\"")" >>$SIN
			else
				rm -f $d
			fi
		fi
	done
done
if [ -f $SIN ]; then
	$S BATCH $SIN $SOUT
fi
