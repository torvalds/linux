#!/bin/sh

tmp=$1
file=${tmp:=VERSION}
gitver=$(git describe 2>/dev/null | tr - .)
filever=$(cat ${file} 2>/dev/null)

version=${gitver}
: ${version:=$filever}

echo "$version"
