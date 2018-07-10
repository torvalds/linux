#!/bin/sh

# hush's stderr with leak debug enabled
output=output

freelist=`grep 'free 0x' "$output" | cut -d' ' -f2 | sort | uniq | xargs`

grep -v free "$output" >"$output.leaked"

i=8
list=
for freed in $freelist; do
    list="$list -e $freed"
    test $((--i)) != 0 && continue
    echo Dropping $list
    grep -F -v $list <"$output.leaked" >"$output.temp"
    mv "$output.temp" "$output.leaked"
    i=8
    list=
done
if test "$list"; then
    echo Dropping $list
    grep -F -v $list <"$output.leaked" >"$output.temp"
    mv "$output.temp" "$output.leaked"
fi
