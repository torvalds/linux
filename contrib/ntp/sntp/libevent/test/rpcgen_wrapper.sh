#!/bin/sh
# libevent rpcgen_wrapper.sh
# Transforms event_rpcgen.py failure into success for make, only if
# regress.gen.c and regress.gen.h already exist in $srcdir.  This
# is needed for "make distcheck" to pass the read-only $srcdir build,
# as with read-only sources fresh from tarball, regress.gen.[ch] will
# be correct in $srcdir but unwritable.  This previously triggered
# Makefile.am to create stub regress.gen.c and regress.gen.h in the
# distcheck _build directory, which were then detected as leftover
# files in the build tree after distclean, breaking distcheck.
# Note that regress.gen.[ch] are not in fresh git clones, making
# working Python a requirement for make distcheck of a git tree.

exit_updated() {
#    echo "Updated ${srcdir}/regress.gen.c and ${srcdir}/regress.gen.h"
    exit 0
}

exit_reuse() {
    echo "event_rpcgen.py failed, ${srcdir}/regress.gen.\[ch\] will be reused." >&2
    exit 0
}

exit_failed() {
    echo "Could not generate regress.gen.\[ch\] using event_rpcgen.sh" >&2
    exit 1
}

if [ -x /usr/bin/python2 ] ; then
  PYTHON2=/usr/bin/python2
elif [ "x`which python2`" != x ] ; then
  PYTHON2=python2
else
  PYTHON2=python
fi

srcdir=$1
srcdir=${srcdir:-.}

${PYTHON2} ${srcdir}/../event_rpcgen.py --quiet ${srcdir}/regress.rpc \
               test/regress.gen.h test/regress.gen.c

case "$?" in
 0)
    exit_updated
    ;;
 *)
    test -r ${srcdir}/regress.gen.c -a -r ${srcdir}/regress.gen.h && \
	exit_reuse
    exit_failed
    ;;
esac
