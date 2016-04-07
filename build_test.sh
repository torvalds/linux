#!/bin/bash

# Usage:
#   build/build_test.sh

export MAKE_ARGS=$@
export ROOT_DIR=$(dirname $(readlink -f $0))
export NET_TEST=${ROOT_DIR}/../kernel/tests/net/test
export BUILD_CONFIG=build/build.config.net_test

test=all_tests.sh
set -e
source ${ROOT_DIR}/envsetup.sh

echo "========================================================"
echo " Building kernel and running tests "

cd ${KERNEL_DIR}
$NET_TEST/run_net_test.sh --builder $test
echo $?
echo "======Finished running tests======"
