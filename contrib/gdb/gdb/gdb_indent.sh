#!/bin/sh

# Try to find a GNU indent.  There could be a BSD indent in front of a
# GNU gindent so when indent is found, keep looking.

gindent=
indent=
paths=`echo $PATH | sed \
	-e 's/::/:.:/g' \
	-e 's/^:/.:/' \
	-e 's/:$/:./' \
	-e 's/:/ /g'`
for path in $paths
do
    if test ! -n "${gindent}" -a -x ${path}/gindent
    then
	gindent=${path}/gindent
	break
    elif test ! -n "${indent}" -a -x ${path}/indent
    then
	indent=${path}/indent
    fi
done

if test -n "${gindent}"
then
    indent=${gindent}
elif test -n "${indent}"
then
    :
else
    echo "Indent not found" 1>&2
fi


# Check that the indent found is both GNU and a reasonable version.
# Different indent versions give different indentation.

m1=2
m2=2
m3=9

version=`${indent} --version 2>/dev/null < /dev/null`
case "${version}" in
    *GNU* ) ;;
    * ) echo "error: GNU indent $m1.$m2.$m3 expected" 1>&2 ; exit 1;;
esac
v1=`echo "${version}" | sed 's/^.* \([0-9]*\)\.\([0-9]*\)\.\([0-9]*\)$/\1/'`
v2=`echo "${version}" | sed 's/^.* \([0-9]*\)\.\([0-9]*\)\.\([0-9]*\)$/\2/'`
v3=`echo "${version}" | sed 's/^.* \([0-9]*\)\.\([0-9]*\)\.\([0-9]*\)$/\3/'`

if test $m1 -ne $v1 -o $m2 -ne $v2 -o $m3 -gt $v3
then
    echo "error: Must be GNU indent version $m1.$m2.$m3 or later" 1>&2
    exit 1
fi

if test $m3 -ne $v3
then
    echo "warning: GNU indent version $m1.$m2.$m3 recommended" 1>&2
fi

# Check that we're in the GDB source directory

case `pwd` in
    */gdb ) ;;
    */sim/* ) ;;
    * ) echo "Not in GDB directory" 1>&2 ; exit 1 ;;
esac


# Run indent per GDB specs

types="\
-T FILE \
-T bfd -T asection -T pid_t \
-T prgregset_t -T fpregset_t -T gregset_t -T sigset_t \
-T td_thrhandle_t -T td_event_msg_t -T td_thr_events_t \
-T td_notify_t -T td_thr_iter_f -T td_thrinfo_t \
`cat *.h | sed -n \
    -e 's/^.*[^a-z0-9_]\([a-z0-9_]*_ftype\).*$/-T \1/p' \
    -e 's/^.*[^a-z0-9_]\([a-z0-9_]*_func\).*$/-T \1/p' \
    -e 's/^typedef.*[^a-zA-Z0-9_]\([a-zA-Z0-9_]*[a-zA-Z0-9_]\);$/-T \1/p' \
    | sort -u`"

${indent} ${types} "$@"
