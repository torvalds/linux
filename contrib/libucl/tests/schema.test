#!/bin/sh

PROG=${TEST_BINARY_DIR}/test_schema
rm /tmp/_ucl_test_schema.out ||true
_succeed=0
_tests=0
for i in ${TEST_DIR}/schema/*.json ; do
	_name=`basename $i`
	printf "running schema test suite $_name... "
	$PROG >> /tmp/_ucl_test_schema.out < $i
	if [ $? -eq 0 ] ; then
	    echo "OK"
	    _succeed=$(($_succeed + 1))
	else
	    echo "Fail"
	fi
	_tests=$(($_tests + 1))
done

if [ $_tests -ne $_succeed ] ; then
    exit 1
fi
