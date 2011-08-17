#!/bin/sh -x

#aclocal
#autoheader
#libtoolize --copy --force
#automake-1.9 -acf
#autoconf

autoreconf -i -f -v
