#!/bin/sh
if [ $1 = "start" ]; then
        if [ -x /usr/local/bin/xntpd ]; then
                echo "Starting NTP daemon, takes about 1 minute... "
		# dosynctodr may need to be left alone as of with Solaris 2.6
		# The following line is unnecessary if you turn off 
		# dosynctodr in /etc/system.
                /usr/local/bin/tickadj -s  
                /usr/local/bin/ntpdate -v server1 server2
                sleep 5
                /usr/local/bin/xntpd
        fi
else
        if [ $1 = "stop" ]; then
                pid=`/usr/bin/ps -e | /usr/bin/grep xntpd | /usr/bin/sed -e 's/^  *//' -e 's/ .*//'`   
                if [ "${pid}" != "" ]; then
                        echo "Stopping Network Time Protocol daemon "
                        /usr/bin/kill ${pid}
                fi     
        fi
fi
