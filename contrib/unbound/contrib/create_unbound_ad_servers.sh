#!/bin/sh
#
# Convert the Yoyo.org anti-ad server listing
# into an unbound dns spoof redirection list.
# Modified by Y.Voinov (c) 2014

# Note: Wget required!

# Variables
dst_dir="/etc/opt/csw/unbound"
work_dir="/tmp"
list_addr="http://pgl.yoyo.org/adservers/serverlist.php?hostformat=nohtml&showintro=1&startdate%5Bday%5D=&startdate%5Bmonth%5D=&startdate%5Byear%5D="

# OS commands
CAT=`which cat`
ECHO=`which echo`
WGET=`which wget`

# Check Wget installed
if [ ! -f $WGET ]; then
 echo "Wget not found. Exiting..."
 exit 1
fi

$WGET -O $work_dir/yoyo_ad_servers "$list_addr" && \
$CAT $work_dir/yoyo_ad_servers | \
while read line ; \
 do \
   $ECHO "local-zone: \"$line\" redirect" ;\
   $ECHO "local-data: \"$line A 127.0.0.1\"" ;\
 done > \
$dst_dir/unbound_ad_servers

echo "Done."
#  then add an include line to your unbound.conf pointing to the full path of
#  the unbound_ad_servers file:
#
#   include: $dst_dir/unbound_ad_servers
#