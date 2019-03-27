#!/bin/sh
# $Id: gauge,v 1.7 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

PCT=10
(
while test $PCT != 100
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

$DIALOG --title "GAUGE" "$@" --gauge "Hi, this is a gauge widget" 20 70 0
