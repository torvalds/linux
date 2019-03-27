#!/bin/sh
# $Id: gauge0-input-fd,v 1.5 2010/01/13 10:20:03 tom Exp $
# modified "gauge0" script to use "--input-fd" option.

. ./setup-vars

exec 3<&0
PCT=10
(
sleep 1
while test $PCT != 110
do
cat <<EOF
XXX
$PCT
The new\n\
message ($PCT percent)
XXX
EOF
PCT=`expr $PCT + 10`
sleep 1
done
) |

$DIALOG --title "GAUGE" --input-fd 3 "$@" --gauge "Hi, this is a gauge widget" 0 0 0

exec 3<&-
