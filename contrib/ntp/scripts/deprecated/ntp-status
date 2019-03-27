#!/bin/sh

# From: Marc Brett <Marc.Brett@westgeo.com>

# Here's a quick hack which can give you the stratum, delay, offset
# for any number of ntp servers.

NTPDATE=/usr/local/bin/ntpdate
NSLOOKUP=/usr/sbin/nslookup
EGREP=/bin/egrep
AWK=/bin/awk
RM=/bin/rm
FILE=/tmp/ntp.$$

USAGE="Usage: $0 hostname [hostname ...]"

if [ $# -le 0 ]
then
	echo $USAGE 2>&1
	exit 1
fi

trap '$RM -f $FILE; exit' 1 2 3 4 13 15

for HOST in $*
do
    HOSTNAME=`$NSLOOKUP $HOST | $EGREP "Name:" | $AWK '{print $2}'`
    if [ -n "$HOSTNAME" ]
    then
	$NTPDATE -d $HOST 2>/dev/null | $EGREP '^stratum|^delay|^offset|^originate' > $FILE
	STRATUM=`$EGREP '^stratum' $FILE | $AWK '{print $2}'`
	OFFSET=`$EGREP '^offset' $FILE | $AWK '{print $2}'`
	DELAY=`$EGREP '^delay' $FILE | $AWK '{print $2}'`
	TIMESTAMP=`$EGREP '^originate' $FILE | $AWK '{print $4 " " $5 " " $6 " " $7 " " $8}'`
	if [ "$STRATUM" -ne 0 ]
	then
		echo "$HOSTNAME: stratum:$STRATUM delay:$DELAY offset:$OFFSET  $TIMESTAMP"
	else
		echo $HOSTNAME: Not running NTP
	fi
    fi

done

$RM -f $FILE
