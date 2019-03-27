#!/bin/sh
# $Id: progress2,v 1.7 2018/06/17 20:45:25 tom Exp $

. ./setup-vars

. ./setup-tempfile

ls -1 >$tempfile
(
while true
do
read text
test -z "$text" && break
ls -ld "$text" || break
sleep 1
done <$tempfile
) |

$DIALOG --title "PROGRESS" "$@" --progressbox "This is a detailed description\nof the progress-box." 20 70

retval=$?
. ./report-button
