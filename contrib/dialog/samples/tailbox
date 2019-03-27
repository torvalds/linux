#!/bin/sh
# $Id: tailbox,v 1.7 2010/01/13 10:36:18 tom Exp $

. ./setup-vars

./killall listing
./listing >listing.out &

$DIALOG --title "TAIL BOX" "$@" \
        --tailbox listing.out 24 70

retval=$?

. ./report-button

./killall listing
