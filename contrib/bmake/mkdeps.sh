:
# NAME:
#	mkdeps - generate dependencies
#
# SYNOPSIS:
#	mkdeps [options] file ...
#
# DESCRIPTION:
#	This script updates "makefile" with dependencies for
#	"file"(s).  It borrows ideas from various makedepend scripts
#	and should be compatible with most.
#
#	By default we use grep to extract include file names from
#	source files.  We source an "rc" file '$Mydir/.${Myname}rc' which
#	can contain variable assignments such as:
#.nf
#   
#	cpp_c=/usr/lib/cpp
#	cpp_cc=g++ -E
#	...
#
#.fi
#	If the variable 'cpp_$suffix' is set, we use it as our cpp in
#	place of grep.  The program referenced by these variables are
#	expected to produce output like:
#.nf
#
#	# 10 \"/usr/include/stdio.h\" 1
#
#.fi
#	This allows us to skip most of our processing.  For lex,yacc
#	and other source files, grep is probably just as quick and
#	certainly more portable.
#
#	If the "rc" file does not exist, we create it and attempt to
#	find cpp or an equivalent cc invocation to assign to 'cpp_c'.
#		
# AUTHOR:
#	Simon J. Gerraty <sjg@zen.void.oz.au>
#

# RCSid:
#	$Id: mkdeps.sh,v 1.23 2002/11/29 06:58:59 sjg Exp $
#
#	@(#) Copyright (c) 1993 Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that 
#	the above copyright notice and this notice are
#	left intact. 
#      
#	Please send copies of changes and bug-fixes to:
#	sjg@zen.void.oz.au
#

Myname=`basename $0 .sh`
Mydir=`dirname $0`

case `echo -n .` in
-n*)	N=; C="\c";;
*)	N=-n; C=;;
esac

cc_include=-I/usr/include

TF=/tmp/dep.$$
EF=/tmp/deperr.$$
> $EF

case "$*" in
*-n*)				# don't use rc file
  rc=/dev/null
  norc=yes;;
*)
  rc=$Mydir/.${Myname}rc
  ;;
esac

update=
Include=include

if [ x"$norc" = x -a -f $rc ]; then
  . $rc
else
  # if /usr/lib/cpp or equivalent is available it is better than
  # grepping .c files.
  # See what (if anything) works on this system...
  echo : > $rc
  echo "# pre-processor for .c files" >> $rc
  # try a couple of sane places first
  for d in /usr/libexec /usr/lib /usr/bin /lib /usr/ccs/bin
  do
    cpp_c=$d/cpp
    [ -x $cpp_c ] && break
  done
  
  if [ -x $cpp_c ]; then
    echo cpp_c=$cpp_c >> $rc
  else
    cpp_c=
    # rats see if cc can be used
    echo "#include <stdio.h>" > /tmp/f$$.c
    echo "main() { return 0; }" >> /tmp/f$$.c
    # try some sensible args to cc
    for arg in -E -P -M
    do
      ok=`${REALCC:-${CC:-cc}} $arg /tmp/f$$.c 2>/dev/null | grep '^#.*stdio.h' | tail -1`
      case "$ok" in
      "") ;;
      *)
        cpp_c="${REALCC:-${CC:-cc}} $arg"
        echo cpp_c="'$cpp_c'" >> $rc
        break;;
      esac
    done
    rm -f /tmp/f$$.c
  fi
fi

clean_up() {
  trap "" 2 3
  trap 0
  if [ -s $EF ]; then
          egrep -vi "included from|warning" $EF > ${EF}2
          if [ -s ${EF}2 ]; then
	          cat $EF >&2
                  rm -f .depend
                  ests=1
	  fi
  fi
  rm -f $TF $EF*
  exit ${ests:-0}
}

# this lot does not work on HPsUX - complain to Hp.
trap clean_up 0
trap exit 2 3

get_incs() {
  case "$cpp" in
  grep)
    # set IGNORE="<" to skip system includes
    egrep '^#[ 	]*include' $* | egrep -v "$IGNORE" | \
      sed -e 's/^.*include[^"<]*["<]//' -e 's/[">].*//g';;
  *)
    # $cpp (eg. /usr/lib/cpp or cc -E) should produce output like:
    # 1 "/usr/include/stdio.h" 2
    # set IGNORE=/usr/include to skip system includes
    $cpp $cpp_opts $cc_include $* 2>> $EF | egrep '^#.*\.h"' | sed 's,^#.*"\(.*\)".*,\1,' |
      egrep -v "$IGNORE" | sort -u;;
  esac
}

gen_deps() {
  llen=$1
  shift

  for ifile in $*
  do
    case "$cpp" in
    grep)
      # this lot is not needed if not using grep.
      for dir in $srcdir $dirlist /usr/include
      do
        [ -f "$dir/$ifile" ] && break
      done

      if [ ! -f "$dir/$ifile" ]; then
        # produce a useful error message (useful to emacs or error)
        iline=`grep -n ".*include.*[\"<]$ifile[\">]" $file | cut -d: -f1`
        echo "\"$file\", line $iline: cannot find include file \"$ifile\"" >> $EF
        # no point adding to dependency list as the resulting makefile
        # would not work anyway...
        continue
      fi
      ifile=$dir/$ifile
      
      # check whether we have done it yet
      case `grep "$ifile" $TF` in
      "") echo "$ifile" >> $TF;;
      *)	continue;;		# no repeats...
      esac
      ;;
    esac
    
    len=`expr "$ifile " : '.*'`
    if [ "`expr $llen + $len`" -gt ${width:-76} ]; then
      echo "\\" >> .depend
      echo $N "	$C" >> .depend
      llen=8
    fi
    echo $N "$ifile $C" >> .depend
    llen=`expr $llen + $len`
    
    case "$cpp" in
    grep)
      # this lot is not needed unless using grep.
      ilist=`get_incs $ifile` # recurse needed?
      [ "$ilist" ] && llen=`gen_deps $llen $ilist`
      ;;
    esac
  done
  echo $llen
}

for f in makefile Makefile
do
  test -s $f && { MAKEFILE=$f; break; }
done

MAKEFILE=${MAKEFILE:-makefile}
IGNORE=${IGNORE:-"^-"}		# won't happen
obj=o
cpp_opts=			# incase cpp != grep
vpath=
append=
progDep=

set -- `getopt "AanNV:s:w:o:I:D:b:f:i:p" "$@"`
for key in "$@"
do
  case $key in
  --)	shift; break;;
  -A)	Include=;;		# cat .depend >> $MAKEFILE
  -a)	append=yes; shift;;
  -n)	shift;;			# ignore rc
  -N)	update=no; shift;;	# don't update $MAKEFILE
  -I)	cpp_opts="$cpp_opts$1$2 "; dirlist="$dirlist $2"; shift 2;;
  -o)	obj=$2; shift 2;;
  -s)	shift 2;;		# can't handle it anyway...
  -w)	width=$2; shift 2;;
  -f)	MAKEFILE=$2; shift 2;;
  -b)	BASEDIR=$2; shift 2;;
  -i)	IGNORE="$2"; shift 2;;	# ignore headers matching this...
  -D)	cpp_opts="$cpp_opts$1$2 "; shift 2;;
  -V)	VPATH="$2"; shift 2;;	# where to look for files
  -p)	progDep=yes; shift;;
  esac
done

[ "$VPATH" ] && vpath=`IFS=:; set -- $VPATH; echo $*`

[ "$append" ] || > .depend

for file in $*
do
  cpp=
  suffix=`expr $file : '.*\.\([^.]*\)'`

  eval cpp=\"\${cpp_${suffix}:-grep}\"
  
  if [ ! -f $file -a "$vpath" ]; then
    for d in . $vpath
    do
      [ -f $d/$file ] && { file=$d/$file; break; }
    done
  fi
  srcdir=`dirname $file`
  base=`basename $file .$suffix`
  
  ilist=`get_incs $file`

  if [ "$ilist" ]; then
    > $TF
    if [ "$progDep" ]; then
      echo "$base:	$file \\" >> .depend
    else
      echo "$base.$obj:	$file \\" >> .depend
    fi
    echo $N "	$C" >> .depend
    llen=8
    llen=`gen_deps $llen $ilist`
    echo >> .depend
    echo >> .depend
  elif [ "$progDep" ]; then
    echo "$base:	$file" >> .depend
    echo >> .depend
  fi
done

if [ -s .depend ]; then
  # ./foo.h looks ugly
  mv .depend $TF
  { test "$BASEDIR" && sed -e "s;$BASEDIR;\$(BASEDIR);g" $TF || cat $TF; } |
    sed 's;\([^.]\)\./;\1;g' > .depend 

  #
  # Save the manually updated section of the makefile
  #
  if [ x$update != xno ]; then
    trap "" 2			# don't die if we got this far

    # if make doesn't support include, then append our deps...
    depended=`grep 'include.*\.depend' $MAKEFILE`
    test "$depended" && clean_up
  
    sed '/^# DO NOT DELETE.*depend.*$/,$d' < $MAKEFILE > $TF
    mv $TF $MAKEFILE
    cat <<! >> $MAKEFILE
# DO NOT DELETE THIS LINE -- make depend depends on it
# Do not edit anything below, it was added automagically by $Myname.

!
  
    case "$Include" in
    "")	cat .depend >> $MAKEFILE;;
    .include)	echo '.include ".depend"' >> $MAKEFILE;;
    include)	echo include .depend >> $MAKEFILE;;
    esac
  fi
fi
clean_up
