#!/bin/sh
# $Id: progress,v 1.7 2018/06/17 20:45:25 tom Exp $

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

$DIALOG --title "PROGRESS" "$@" --progressbox 20 70

retval=$?
. ./report-button
