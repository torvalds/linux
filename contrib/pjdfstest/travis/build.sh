#!/bin/sh

set -e

cd $(dirname $0)/..
autoreconf -ifs
./configure
make
