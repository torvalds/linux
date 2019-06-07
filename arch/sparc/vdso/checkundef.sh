#!/bin/sh
objdump="$1"
file="$2"
$objdump -t "$file" | grep '*UUND*' | grep -v '#scratch' > /dev/null 2>&1
if [ $? -eq 1 ]; then
    exit 0
else
    echo "$file: undefined symbols found" >&2
    exit 1
fi
