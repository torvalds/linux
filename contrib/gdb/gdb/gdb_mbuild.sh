#!/bin/sh

#  Multi-build script for testing compilation of all maintained
#  configs of GDB.

#  Copyright 2002, 2003 Free Software Foundation, Inc.

#  Contributed by Richard Earnshaw  (rearnsha@arm.com)

#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.

#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.

#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

usage()
{
    cat <<EOF
Usage: gdb_mbuild.sh [ <options> ... ] <srcdir> <builddir>
 Options:
   -j <makejobs>  Run <makejobs> in parallel.  Passed to make.
	          On a single cpu machine, 2 is recommended.
   -k             Keep going.  Do not stop after the first build fails.
   --keep         Keep builds.  Do not remove each build when finished.
   -e <regexp>    Regular expression for selecting the targets to build.
   -f             Force rebuild.  Even rebuild previously built directories.
   -v             Be more (and more, and more) verbose.
 Arguments:
   <srcdir>       Source code directory.
   <builddir>     Build directory.
 Environment variables examined (with default if not defined):
   MAKE (make)"
EOF
    exit 1;
cat <<NOTYET
  -b <maxbuilds> Run <maxbuild> builds in parallel.
		 On a single cpu machine, 1 is recommended.
NOTYET
}

### COMMAND LINE OPTIONS

makejobs=
maxbuilds=1
keepgoing=
force=false
targexp=""
verbose=0
keep=false
while test $# -gt 0
do
    case "$1" in
    -j )
	# Number of parallel make jobs.
	shift
	test $# -ge 1 || usage
	makejobs="-j $1"
	;;
    -b | -c )
	# Number of builds to fire off in parallel.
	shift
	test $# -ge 1 || usage
	maxbuilds=$1
	;;
    -k )
	# Should we soldier on after the first build fails?
	keepgoing=-k
	;;
    --keep )
        keep=true
	;;
    -e )
	# A regular expression for selecting targets
	shift
	test $# -ge 1 || usage
	targexp="${targexp} -e ${1}"
	;;
    -f )
	# Force a rebuild
	force=true ;
	;;
    -v )
	# Be more, and more, and more, verbose
	verbose=`expr ${verbose} + 1`
	;;
    -* ) usage ;;
    *) break ;;
    esac
    shift
done


### COMMAND LINE PARAMETERS

if test $# -ne 2
then
    usage
fi

# Convert these to absolute directory paths.

# Where the sources live
srcdir=`cd $1 && /bin/pwd` || exit 1

# Where the builds occur
builddir=`cd $2 && /bin/pwd` || exit 1

### ENVIRONMENT PARAMETERS

# Version of make to use
make=${MAKE:-make}
MAKE=${make}
export MAKE


# Where to look for the list of targets to test
maintainers=${srcdir}/gdb/MAINTAINERS
if [ ! -r ${maintainers} ]
then
    echo Maintainers file ${maintainers} not found
    exit 1
fi

# Get the list of targets and the build options
alltarg=`cat ${maintainers} | tr -s '[\t]' '[ ]' | sed -n '
/^[ ]*[-a-z0-9\.]*[ ]*[(]*--target=.*/ !d
s/^.*--target=//
s/).*$//
h
:loop
  g
  /^[^ ]*,/ !b end
  s/,[^ ]*//
  p
  g
  s/^[^,]*,//
  h
b loop
:end
p
' | if test "${targexp}" = ""
then
    grep -v -e broken -e OBSOLETE
else
    grep ${targexp}
fi`


# Usage: fail <message> <test-that-should-succeed>.  Should the build
# fail?  If the test is true, and we don't want to keep going, print
# the message and shoot everything in sight and abort the build.

fail ()
{
    msg="$1" ; shift
    if test "$@"
    then
	echo "${target}: ${msg}"
	if test "${keepgoing}" != ""
	then
	    #exit 1
	    continue
	else
	    kill $$
	    exit 1
	fi
    fi
}


# Usage: log <level> <logfile>.  Write standard input to <logfile> and
# stdout (if verbose >= level).

log ()
{
    if test ${verbose} -ge $1
    then
	tee $2
    else
	cat > $2
    fi
}



# Warn the user of what is comming, print the list of targets

echo "$alltarg"
echo ""


# For each target, configure, build and test it.

echo "$alltarg" | while read target gdbopts simopts
do

    trap "exit 1"  1 2 15
    dir=${builddir}/${target}

    # Should a scratch rebuild be forced, for perhaphs the entire
    # build be skipped?

    if ${force}
    then
	echo forcing ${target} ...
	rm -rf ${dir}
    elif test -f ${dir}
    then
	echo "${target}"
	continue
    else
	echo ${target} ...
    fi

    # Did the previous configure attempt fail?  If it did
    # restart from scratch.

    if test -d ${dir} -a ! -r ${dir}/Makefile
    then
	echo ... removing partially configured ${target}
	rm -rf ${dir}
	if test -d ${dir}
	then
	    echo "${target}: unable to remove directory ${dir}"
	    exit 1
	fi
    fi

    # From now on, we're in this target's build directory

    mkdir -p ${dir}
    cd ${dir} || exit 1

    # Configure, if not already.  Should this go back to being
    # separate and done in parallel?

    if test ! -r Makefile
    then
	# Default SIMOPTS to GDBOPTS.
	test -z "${simopts}" && simopts="${gdbopts}"
	# The config options
	__target="--target=${target}"
	__enable_gdb_build_warnings=`test -z "${gdbopts}" \
	    || echo "--enable-gdb-build-warnings=${gdbopts}"`
	__enable_sim_build_warnings=`test -z "${simopts}" \
	    || echo "--enable-sim-build-warnings=${simopts}"`
	__configure="${srcdir}/configure \
	    ${__target} \
	    ${__enable_gdb_build_warnings} \
	    ${__enable_sim_build_warnings}"
	echo ... ${__configure}
	trap "echo Removing partially configured ${dir} directory ...; rm -rf ${dir}; exit 1" 1 2 15
	${__configure} 2>&1 | log 2 Config.log
	trap "exit 1"  1 2 15
    fi
    fail "configure failed" ! -r Makefile
 
    # Build, if not built.

    if test ! -x gdb/gdb -a ! -x gdb/gdb.exe
    then
	# Iff the build fails remove the final build target so that
	# the follow-on code knows things failed.  Stops the follow-on
	# code thinking that a failed rebuild succedded (executable
	# left around from previous build).
	echo ... ${make} ${keepgoing} ${makejobs} ${target}
	( ${make} ${keepgoing} ${makejobs} all-gdb || rm -f gdb/gdb gdb/gdb.exe
	) 2>&1 | log 1 Build.log
    fi
    fail "compile failed" ! -x gdb/gdb -a ! -x gdb/gdb.exe
 
    # Check that the built GDB can at least print it's architecture.

    echo ... run ${target}
    rm -f core gdb.core ${dir}/gdb/x
    cat <<EOF > x
maint print architecture
quit
EOF
    ./gdb/gdb -batch -nx -x x 2>&1 | log 1 Gdb.log
    fail "gdb dumped core" -r core -o -r gdb.core
    fail "gdb printed no output" ! -s Gdb.log
    grep -e internal-error Gdb.log && fail "gdb panic" 1

    echo ... cleanup ${target}

    # Create a sed script that cleans up the output from GDB.
    rm -f mbuild.sed
    touch mbuild.sed || exit 1
    # Rules to replace <0xNNNN> with the corresponding function's
    # name.
    sed -n -e '/<0x0*>/d' -e 's/^.*<0x\([0-9a-f]*\)>.*$/0x\1/p' Gdb.log \
    | sort -u \
    | while read addr
    do
	func="`addr2line -f -e ./gdb/gdb -s ${addr} | sed -n -e 1p`"
	test ${verbose} -gt 0 && echo "${addr} ${func}" 1>&2
	echo "s/<${addr}>/<${func}>/g"
    done >> mbuild.sed
    # Rules to strip the leading paths off of file names.
    echo 's/"\/.*\/gdb\//"gdb\//g' >> mbuild.sed
    # Run the script
    sed -f mbuild.sed Gdb.log > Mbuild.log

    # Replace the build directory with a file as semaphore that stops
    # a rebuild. (should the logs be saved?)

    cd ${builddir}

    if ${keep}
    then
	:
    else
	rm -f ${target}.tmp
	mv ${target}/Mbuild.log ${target}.tmp
	rm -rf ${target}
	mv ${target}.tmp ${target}
    fi

    # Success!
    echo ... ${target} built

done

exit 0
