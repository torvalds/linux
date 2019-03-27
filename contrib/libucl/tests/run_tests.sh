#!/bin/sh

if [ $# -lt 1 ] ; then
	echo 'Specify binary to run as the first argument'
	exit 1
fi


for _tin in ${TEST_DIR}/*.in ; do
	_t=`echo $_tin | sed -e 's/.in$//'`
	$1 $_t.in $_t.out
	if [ $? -ne 0 ] ; then
		echo "Test: $_t failed, output:"
		cat $_t.out
		rm $_t.out
		exit 1
	fi
	if [ -f $_t.res ] ; then
	diff -s $_t.out $_t.res -u 2>/dev/null
		if [ $? -ne 0 ] ; then
			rm $_t.out
			echo "Test: $_t output missmatch"
			exit 1
		fi
	fi
	rm $_t.out
done

if [ $# -gt 2 ] ; then
	$3 ${TEST_DIR}/generate.out
	diff -s ${TEST_DIR}/generate.out ${TEST_DIR}/generate.res -u 2>/dev/null
	if [ $? -ne 0 ] ; then
		rm ${TEST_DIR}/generate.out
		echo "Test: generate.res output missmatch"
    	exit 1
	fi
	rm ${TEST_DIR}/generate.out
fi

if [ $# -gt 3 ] ; then
	rm /tmp/_ucl_test_schema.out ||true
	for i in ${TEST_DIR}/schema/*.json ; do
		_name=`basename $i`
		printf "running schema test suite $_name... "
		cat $i | $4 >> /tmp/_ucl_test_schema.out && ( echo "OK" ) || ( echo "Fail" )
	done
fi

sh -c "xz -c < /dev/null > /dev/null"
if [ $? -eq 0 -a $# -gt 1 ] ; then
	echo 'Running speed tests'
	for _tin in ${TEST_DIR}/*.xz ; do
		echo "Unpacking $_tin..."
		xz -cd < $_tin > ${TEST_DIR}/test_file
		# Preread file to cheat benchmark!
		cat ${TEST_DIR}/test_file > /dev/null
		echo "Starting benchmarking for $_tin..."
		$2 ${TEST_DIR}/test_file
		if [ $? -ne 0 ] ; then
			echo "Test: $_tin failed"
			rm ${TEST_DIR}/test_file
			exit 1
		fi
		rm ${TEST_DIR}/test_file
	done
fi

