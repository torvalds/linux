#!/bin/sh 
#
# ntpgroper host ...
#
# This script checks each hostname given as an argument to see if
# it is running NTP.  It reports one of the following messages (assume
# the host is named "dumbo.hp.com":
#
#   dumbo.hp.com not registered in DNS
#   dumbo.hp.com not responding to ping
#   dumbo.hp.com refused ntpq connection
#   dumbo.hp.com not responding to NTP
#   dumbo.hp.com answers NTP version 2, stratum: 3, ref: telford.nsa.hp.com
#   dumbo.hp.com answers NTP version 3, stratum: 3, ref: telford.nsa.hp.com
#
# It ain't pretty, but it is kinda useful.
#
# Walter Underwood, 11 Feb 1993, wunder@hpl.hp.com
#
# converted to /bin/sh from /bin/ksh by scott@ee.udel.edu 24 Mar 1993

PATH="/usr/local/etc:$PATH" export PATH

verbose=1
logfile=/tmp/cntp-log$$
ntpqlog=/tmp/cntp-ntpq$$

# I wrap the whole thing in parens so that it is possible to redirect
# all the output somewhere, if desired.
(
for host in $*
do
    # echo "Trying $host."

    gethost $host > /dev/null 2>&1
    if [ $? -ne 0 ]
    then
        echo "$host not registered in DNS"
        continue
    fi

    ping $host 64 1 > /dev/null 2>&1 
    if [ $? -ne 0 ]
    then
        echo "$host not responding to ping"
	continue
    fi
    
    # Attempt to contact with version 3 ntp, then try version 2.
    for version in 3 2
    do

        ntpq -c "ntpversion $version" -p $host > $ntpqlog 2>&1

        if fgrep -s 'Connection refused' $ntpqlog
        then
            echo "$host refused ntpq connection"
            break
        fi

        responding=1
        fgrep -s 'timed out, nothing received' $ntpqlog > /dev/null && responding=0

        if   [ $responding -eq 1 ]
        then
	    ntpq -c "ntpversion $version" -c rl $host > $ntpqlog

            # First we extract the reference ID (usually a host or a clock)
            synchost=`fgrep "refid=" $ntpqlog`
            #synchost=${synchost##*refid=} # strip off the beginning of the line
            #synchost=${synchost%%,*}      # strip off the end  
	    synchost=`expr "$synchost" : '.*refid=\([^,]*\),.*'`

            # Next, we get the stratum
            stratum=`fgrep "stratum=" $ntpqlog`
            #stratum=${stratum##*stratum=}
            #stratum=${stratum%%,*}
	    stratum=`expr "$stratum" : '.*stratum=\([^,]*\),.*'`

	    echo "$host answers NTP version $version, stratum: $stratum, ref: $synchost"
            break;
        fi

	if [ $version -eq 2 -a $responding -eq 0 ]
        then
            echo "$host not responding to NTP"
        fi
    done
done
)
# ) >> $logfile

if [ -f $ntpqlog ]; then
    rm $ntpqlog
fi
