#!/bin/sh
# $Id: run_test.sh,v 1.24 2014/07/15 19:21:10 tom Exp $
# vi:ts=4 sw=4:

errors=0

# NEW is the file created by the testcase
# REF is the reference file against which to compare
test_diffs() {
	# echo "...test_diffs $NEW vs $REF"
	mv -f $NEW ${REF_DIR}/
	CMP=${REF_DIR}/${NEW}
	if test ! -f $CMP
	then
		echo "...not found $CMP"
		errors=1
	else
		sed	-e s,$NEW,$REF, \
			-e "s%$YACC_escaped%YACC%" \
			-e '/YYPATCH/s/[0-9][0-9]*/"yyyymmdd"/' \
			-e '/#define YYPATCH/s/PATCH/CHECK/' \
			-e 's,#line \([1-9][0-9]*\) "'$REF_DIR'/,#line \1 ",' \
			-e 's,#line \([1-9][0-9]*\) "'$TEST_DIR'/,#line \1 ",' \
			-e 's,\(YACC:.* line [0-9][0-9]* of "\)'$TEST_DIR/',\1./,' \
			< $CMP >$tmpfile \
			&& mv $tmpfile $CMP
		if test ! -f $REF
		then
			mv $CMP $REF
			echo "...saved $REF"
		elif ( cmp -s $REF $CMP )
		then
			echo "...ok $REF"
			rm -f $CMP
		else
			echo "...diff $REF"
			diff -u $REF $CMP
			errors=1
		fi
	fi
}

test_flags() {
	echo "** testing flags $*"
	root=$1
	ROOT=test-$root
	shift 1
	$YACC $* >$ROOT.output \
	    2>&1 >$ROOT.error
	for type in .output .error
	do
		NEW=$ROOT$type
		REF=$REF_DIR/$root$type
		test_diffs
	done
}

if test $# = 1
then
	PROG_DIR=`pwd`
	TEST_DIR=$1
	PROG_DIR=`echo "$PROG_DIR" | sed -e 's/ /\\\\ /g'`
	TEST_DIR=`echo "$TEST_DIR" | sed -e 's/ /\\\\ /g'`
else
	PROG_DIR=..
	TEST_DIR=.
fi

YACC=$PROG_DIR/yacc
YACC_escaped=`echo "$PROG_DIR/yacc" | sed -e 's/\./\\\./g'`

tmpfile=temp$$

ifBTYACC=`fgrep -l 'define YYBTYACC' $PROG_DIR/config.h > /dev/null; test $? != 0; echo $?`

if test $ifBTYACC = 0; then
	REF_DIR=${TEST_DIR}/yacc
else
	REF_DIR=${TEST_DIR}/btyacc
fi

rm -f ${REF_DIR}/test-*

echo '** '`date`

# Tests which do not need files
MYFILE=nosuchfile
test_flags help -z
test_flags big_b -B
test_flags big_l -L

# Test attempts to read non-existent file
rm -f $MYFILE.*
test_flags nostdin - $MYFILE.y
test_flags no_opts -- $MYFILE.y

# Test attempts to write to readonly file
touch $MYFILE.y

touch $MYFILE.c
chmod 444 $MYFILE.*
test_flags no_b_opt   -b
test_flags no_b_opt1  -bBASE -o $MYFILE.c $MYFILE.y

touch $MYFILE.c
chmod 444 $MYFILE.*
test_flags no_p_opt   -p
test_flags no_p_opt1  -pBASE -o $MYFILE.c $MYFILE.y
rm -f BASE$MYFILE.c

touch $MYFILE.dot
chmod 444 $MYFILE.*
test_flags no_graph   -g -o $MYFILE.c $MYFILE.y
rm -f $MYFILE.dot

touch $MYFILE.output
chmod 444 $MYFILE.*
test_flags no_verbose -v -o $MYFILE.c $MYFILE.y
test_flags no_output  -o $MYFILE.output $MYFILE.y
test_flags no_output1  -o$MYFILE.output $MYFILE.y
test_flags no_output2  -o
rm -f $MYFILE.output

touch $MYFILE.h
chmod 444 $MYFILE.*
test_flags no_defines -d -o $MYFILE.c $MYFILE.y
rm -f $MYFILE.h

touch $MYFILE.i
chmod 444 $MYFILE.*
test_flags no_include -i -o $MYFILE.c $MYFILE.y
rm -f $MYFILE.i

touch $MYFILE.code.c
chmod 444 $MYFILE.*
test_flags no_code_c -r -o $MYFILE.c $MYFILE.y
rm -f $MYFILE.code.c

rm -f $MYFILE.*

for input in ${TEST_DIR}/*.y
do
	case $input in
	test-*)
		echo "?? ignored $input"
		;;
	*)
		root=`basename $input .y`
		ROOT="test-$root"
		prefix=${root}_

		OPTS=
		OPT2=
		OOPT=
		TYPE=".error .output .tab.c .tab.h"
		case $input in
		${TEST_DIR}/btyacc_*)
			if test $ifBTYACC = 0; then continue; fi
			OPTS="$OPTS -B"
			prefix=`echo "$prefix" | sed -e 's/^btyacc_//'`
			;;
		${TEST_DIR}/grammar*)
			OPTS="$OPTS -g"
			TYPE="$TYPE .dot"
			;;
		${TEST_DIR}/code_debug*)
			OPTS="$OPTS -t -i"
			OOPT=rename_debug.c
			TYPE="$TYPE .i"
			prefix=
			;;
		${TEST_DIR}/code_*)
			OPTS="$OPTS -r"
			TYPE="$TYPE .code.c"
			prefix=`echo "$prefix" | sed -e 's/^code_//'`
			;;
		${TEST_DIR}/pure_*)
			OPTS="$OPTS -P"
			prefix=`echo "$prefix" | sed -e 's/^pure_//'`
			;;
		${TEST_DIR}/quote_*)
			OPT2="-s"
			;;
		${TEST_DIR}/inherit*|\
		${TEST_DIR}/err_inherit*)
			if test $ifBTYACC = 0; then continue; fi
			;;
		esac

		echo "** testing $input"

		test -n "$prefix" && prefix="-p $prefix"

		for opt2 in "" $OPT2
		do
			output=$OOPT
			if test -n "$output"
			then
				output="-o $output"
				error=`basename $OOPT .c`.error
			else
				error=${ROOT}${opt2}.error
			fi

			$YACC $OPTS $opt2 -v -d $output $prefix -b $ROOT${opt2} $input 2>$error
			for type in $TYPE
			do
				REF=${REF_DIR}/${root}${opt2}${type}

				# handle renaming due to "-o" option
				if test -n "$OOPT"
				then
					case $type in
					*.tab.c)
						type=.c
						;;
					*.tab.h)
						type=.h
						;;
					*)
						;;
					esac
					NEW=`basename $OOPT .c`${type}
					case $NEW in
					test-*)
						;;
					*)
						if test -f "$NEW"
						then
							REF=${REF_DIR}/$NEW
							mv $NEW test-$NEW
							NEW=test-$NEW
						fi
						;;
					esac
				else
					NEW=${ROOT}${opt2}${type}
				fi
				test_diffs
			done
		done
		;;
	esac
done

exit $errors
