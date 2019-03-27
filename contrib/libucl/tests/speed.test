#!/bin/sh

PROG=${TEST_BINARY_DIR}/test_speed

sh -c "xz -c < /dev/null > /dev/null"
echo 'Running speed tests'
for _tin in ${TEST_DIR}/*.xz ; do
	echo "Unpacking $_tin..."
	xz -cd < $_tin > ${TEST_OUT_DIR}/test_file
	# Preread file to cheat benchmark!
	cat ${TEST_OUT_DIR}/test_file > /dev/null
	echo "Starting benchmarking for $_tin..."
	$PROG ${TEST_OUT_DIR}/test_file
	if [ $? -ne 0 ] ; then
		echo "Test: $_tin failed"
		rm ${TEST_OUT_DIR}/test_file
		exit 1
	fi
	rm ${TEST_OUT_DIR}/test_file
done

