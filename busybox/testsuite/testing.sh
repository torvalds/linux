# Simple test harness infrastructure for BusyBox
#
# Copyright 2005 by Rob Landley
#
# License is GPLv2, see LICENSE in the busybox tarball for full license text.

# This file defines two functions, "testing" and "optional"
# and a couple more...

# The following environment variables may be set to enable optional behavior
# in "testing":
#    VERBOSE - Print the diff -u of each failed test case.
#    DEBUG - Enable command tracing.
#    SKIP - do not perform this test (this is set by "optional")
#
# The "testing" function takes five arguments:
#	$1) Test description
#	$2) Command(s) to run. May have pipes, redirects, etc
#	$3) Expected result on stdout
#	$4) Data to be written to file "input"
#	$5) Data to be written to stdin
#
# The exit value of testing is the exit value of $2 it ran.
#
# The environment variable "FAILCOUNT" contains a cumulative total of the
# number of failed tests.

# The "optional" function is used to skip certain tests, ala:
#   optional FEATURE_THINGY
#
# The "optional" function checks the environment variable "OPTIONFLAGS",
# which is either empty (in which case it always clears SKIP) or
# else contains a colon-separated list of features (in which case the function
# clears SKIP if the flag was found, or sets it to 1 if the flag was not found).

export FAILCOUNT=0
export SKIP=

# Helper for helpers. Oh my...

test x"$ECHO" != x"" || {
	ECHO="echo"
	test x"`echo -ne`" = x"" || {
		# Compile and use a replacement 'echo' which understands -e -n
		ECHO="$PWD/echo-ne"
		test -x "$ECHO" || {
			gcc -Os -o "$ECHO" ../scripts/echo.c || exit 1
		}
	}
	export ECHO
}

# Helper functions

optional()
{
	SKIP=
	while test "$1"; do
		case "${OPTIONFLAGS}" in
			*:$1:*) ;;
			*) SKIP=1; return ;;
		esac
		shift
	done
}

# The testing function

testing()
{
  NAME="$1"
  [ -n "$1" ] || NAME="$2"

  if [ $# -ne 5 ]
  then
    echo "Test $NAME has wrong number of arguments: $# (must be 5)" >&2
    exit 1
  fi

  [ -z "$DEBUG" ] || set -x

  if [ -n "$SKIP" ]
  then
    echo "SKIPPED: $NAME"
    return 0
  fi

  $ECHO -ne "$3" > expected
  $ECHO -ne "$4" > input
  [ -z "$VERBOSE" ] || echo ======================
  [ -z "$VERBOSE" ] || echo "echo -ne '$4' >input"
  [ -z "$VERBOSE" ] || echo "echo -ne '$5' | $2"
  $ECHO -ne "$5" | eval "$2" > actual
  RETVAL=$?

  if cmp expected actual >/dev/null 2>/dev/null
  then
    echo "PASS: $NAME"
  else
    FAILCOUNT=$(($FAILCOUNT + 1))
    echo "FAIL: $NAME"
    [ -z "$VERBOSE" ] || diff -u expected actual
  fi
  rm -f input expected actual

  [ -z "$DEBUG" ] || set +x

  return $RETVAL
}

# Recursively grab an executable and all the libraries needed to run it.
# Source paths beginning with / will be copied into destpath, otherwise
# the file is assumed to already be there and only its library dependencies
# are copied.

mkchroot()
{
  [ $# -lt 2 ] && return

  $ECHO -n .

  dest=$1
  shift
  for i in "$@"
  do
    #bashism: [ "${i:0:1}" == "/" ] || i=$(which $i)
    i=$(which $i) # no-op for /bin/prog
    [ -f "$dest/$i" ] && continue
    if [ -e "$i" ]
    then
      d=`echo "$i" | grep -o '.*/'` &&
      mkdir -p "$dest/$d" &&
      cat "$i" > "$dest/$i" &&
      chmod +x "$dest/$i"
    else
      echo "Not found: $i"
    fi
    mkchroot "$dest" $(ldd "$i" | egrep -o '/.* ')
  done
}

# Set up a chroot environment and run commands within it.
# Needed commands listed on command line
# Script fed to stdin.

dochroot()
{
  mkdir tmpdir4chroot
  mount -t ramfs tmpdir4chroot tmpdir4chroot
  mkdir -p tmpdir4chroot/{etc,sys,proc,tmp,dev}
  cp -L testing.sh tmpdir4chroot

  # Copy utilities from command line arguments

  $ECHO -n "Setup chroot"
  mkchroot tmpdir4chroot $*
  echo

  mknod tmpdir4chroot/dev/tty c 5 0
  mknod tmpdir4chroot/dev/null c 1 3
  mknod tmpdir4chroot/dev/zero c 1 5

  # Copy script from stdin

  cat > tmpdir4chroot/test.sh
  chmod +x tmpdir4chroot/test.sh
  chroot tmpdir4chroot /test.sh
  umount -l tmpdir4chroot
  rmdir tmpdir4chroot
}
