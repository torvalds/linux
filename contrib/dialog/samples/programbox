#!/bin/sh
# $Id: programbox,v 1.3 2018/06/17 20:45:25 tom Exp $

. ./setup-vars

. ./setup-tempfile

ls -1 >$tempfile
(
while true
do
read text
test -z "$text" && break
ls -ld "$text" || break
sleep 0.1
done <$tempfile
) |

$DIALOG --title "PROGRAMBOX" "$@" --programbox 20 70

retval=$?
. ./report-button
