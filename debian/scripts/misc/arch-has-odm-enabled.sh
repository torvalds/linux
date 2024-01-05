#!/bin/sh
# Evaluate whether arch ($1) will be built with do_odm_drivers set to true.
set -e

if [ "$1" = "" ]; then
	case $ARCH in
	x86)	ARCH=amd64;;
	*)	;;
	esac
else
	ARCH=$1
fi

TOPDIR=$(dirname $0)/../../..
. $TOPDIR/debian/debian.env
RULESDIR=$TOPDIR/$DEBIAN/rules.d

do_odm_drivers=false
for f in $ARCH.mk hooks.mk; do
	eval $(cat $RULESDIR/$f | sed -n -e '/do_odm_drivers/s/ \+//gp')
done
if [ "$do_odm_drivers" != "true" ]; then
	return 1
fi

return 0
