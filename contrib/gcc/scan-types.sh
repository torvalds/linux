#! /bin/sh
# Deduce values of standard ANSI and POSIX types (e.g. size_t, pid_t).
# Emits macros definitions for these, and some other types.
# Intended to be used to massage the sys-protos.h file.
# Expects one arg, which is the GCC source directory.

CC=${CC-"./xgcc -B$1/"}
CPP=${CPP-`echo ${CC} -E -I"$1/"`}
SED=sed

# Generate definitions for the standard types (such as mode_t)
# compatible with those in the standard C header files.
# It works by a dummy program through the C pre-processor, and then
# using sed to search for typedefs in the output.

cat >st-dummy.c <<!EOF!
#include <sys/types.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#ifdef size_t
typedef size_t Xsize_t;
#elif defined(__SIZE_TYPE__)
typedef __SIZE_TYPE__ Xsize_t;
#endif
#ifdef va_list
typedef va_list XXXva_list;
#endif
!EOF!

if ${CPP} st-dummy.c >TMP ; then true
else
  echo "scan-types: could not invoke ${CPP} on st-dummy.c" 1>&2 ; exit 1
fi
tr '	' ' ' <TMP >st-dummy.out

for TYPE in dev_t clock_t fpos_t gid_t ino_t mode_t nlink_t off_t pid_t size_t ssize_t time_t uid_t va_list int32_t uint_32_t ; do
    IMPORTED=`eval 'echo $'"$TYPE"`
    if [ -n "${IMPORTED}" ] ; then
	eval "$TYPE='$IMPORTED"
    else
	# Search st-dummy.out for a typedef for $TYPE, and write it out
	# to TMP in #define syntax.
	rm -f TMP
	${SED} -n -e "s|.*typedef  *\(.*\) X*$TYPE *;.*|\1|w TMP" <st-dummy.out>/dev/null
	# Now select the first definition.
        if [ -s TMP ]; then
	    # VALUE is now the typedef'd definition of $TYPE.
            eval "VALUE='`${SED} -e 's| *$||' -e '2,$d' <TMP`'"
	    # Unless VALUE contains a blank, look for a typedef for it
	    # in turn (this could be a loop, but that would be over-kill).
	    # Ensure $VALUE is double quoted to protect cases where it
	    # contains an asterisk and would cause filename expansion.
	    # E.g. when va_list is "char *".
	    if echo "$VALUE" | grep " " >/dev/null ; then true
	    else
		rm -f TMP
		${SED} -n -e "s|.*typedef[ 	][ 	]*\(.*[^a-zA-Z0-9_]\)${VALUE}[ 	]*;.*|\1|w TMP" <st-dummy.out>/dev/null
		if [ -s TMP ]; then
		    eval "VALUE='`${SED} -e '2,$d' -e 's|[ 	]*$||' <TMP`'"
		fi
	    fi
	    eval "$TYPE='$VALUE'"
	fi
    fi
done

cat <<!EOF!
#define ${macro_prefix}clock_t ${clock_t-int /* default */}
#define ${macro_prefix}dev_t ${dev_t-int /* default */}
#define ${macro_prefix}fpos_t ${fpos_t-long /* default */}
#define ${macro_prefix}gid_t ${gid_t-int /* default */}
#define ${macro_prefix}ino_t ${ino_t-int /* default */}
#define ${macro_prefix}mode_t ${mode_t-int /* default */}
#define ${macro_prefix}nlink_t ${nlink_t-int /* default */}
#define ${macro_prefix}off_t ${off_t-long /* default */}
#define ${macro_prefix}pid_t ${pid_t-int /* default */}
#define ${macro_prefix}ptrdiff_t __PTRDIFF_TYPE__
#define ${macro_prefix}size_t __SIZE_TYPE__
#define ${macro_prefix}time_t ${time_t-int /* default */}
#define ${macro_prefix}uid_t ${uid_t-int /* default */}
#define ${macro_prefix}wchar_t __WCHAR_TYPE__
#define ${macro_prefix}int32_t ${int32_t-int /* default */}
#define ${macro_prefix}uint32_t ${uint32_t-unsigned int /* default */}
!EOF!

# (wait_arg_t*) should be (int*), according to Posix, but
# BSD traditionally used (union wait*).  Use (void*) to allow either usage.
echo "#define ${macro_prefix}wait_arg_t void"

# ssize_t is the signed version of size_t
if [ -n "${ssize_t}" ] ; then
    echo "#define ${macro_prefix}ssize_t ${ssize_t}"
elif [ -z "${size_t}" ] ; then
    echo "#define ${macro_prefix}ssize_t long"
else
    # Remove "unsigned" from ${size_t} to get ${ssize_t}.
    tmp="`echo ${size_t} | ${SED} -e 's|unsigned||g' -e 's|  | |g'`"
    if [ -z "$tmp" ] ; then
	tmp=int
    else
	# check $tmp doesn't conflict with <unistd.h>
	echo "#include <unistd.h>
	extern $tmp read();" >st-dummy.c
	${CC} -c st-dummy.c >/dev/null 2>&1 || tmp=int
    fi
    echo "#define ${macro_prefix}ssize_t $tmp /* default */"
fi

# va_list can cause problems (e.g. some systems have va_list as a struct).
# Check to see if ${va_list-char*} really is compatible with stdarg.h.
cat >st-dummy.c <<!EOF!
#define X_va_list ${va_list-char* /* default */}
extern long foo(X_va_list ap); /* Check that X_va_list compiles on its own */
#include <stdarg.h>
long foo(X_va_list ap) { return va_arg(ap, long); }
long bar(int i, ...)
{ va_list ap; long j; va_start(ap, i); j = foo(ap); va_end(ap); return j; }
!EOF!
if ${CC} -c st-dummy.c >/dev/null 2>&1 ; then
  # Ok: We have something that works.
  echo "#define ${macro_prefix}va_list ${va_list-char* /* default */}"
else
  # No, it breaks.  Indicate that <stdarg.h> must be included.
  echo "#define ${macro_prefix}NEED_STDARG_H
#define ${macro_prefix}va_list va_list"
fi

# stuff needed for curses.h

# This isn't correct for SVR4 (for example).  However, we only
# use this when adding a missing prototype, so it shouldn't matter.
echo "#define chtype int"
# sys-protos.h uses non-standard names (due to the CHTYPE argument problem).
echo "#define box32 box"
echo "#define initscr32 initscr"
echo "#define w32addch waddch"
echo "#define w32insch winsch"

rm -f st-dummy.c st-dummy.o TMP st-dummy.out
