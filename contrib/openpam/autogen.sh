#!/bin/sh
#
# $OpenPAM: autogen.sh 938 2017-04-30 21:34:42Z des $
#

libtoolize --copy --force
aclocal -I m4
autoheader
automake --add-missing --copy --foreign
autoconf
