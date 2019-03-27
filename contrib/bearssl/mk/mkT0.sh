#! /bin/sh

CSC=$(which mono-csc || which dmcs || echo "none")

if [ $CSC = "none" ]; then
	echo "Error: Please install mono-devel."
	exit 1
fi

set -e
$CSC /out:T0Comp.exe /main:T0Comp /res:T0/kern.t0,t0-kernel T0/*.cs
