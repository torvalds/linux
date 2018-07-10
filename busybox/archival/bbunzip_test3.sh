#!/bin/sh
# Leak test for gunzip. Watch top for growing process size.
# In this case we look for leaks in "concatenated .gz" code -
# we feed gunzip with a stream of .gz files.

i=$PID
c=0
while true; do
    c=$((c + 1))
    echo "Block# $c" >&2
    # RANDOM is not very random on some shells. Spice it up.
    i=$((i * 1664525 + 1013904223))
    # 100003 is prime
    len=$(( (((RANDOM*RANDOM)^i) & 0x7ffffff) % 100003 ))

    # Just using urandom will make gzip use method 0 (store) -
    # not good for test coverage!
    cat /dev/urandom \
    | while true; do read junk; echo "junk $c $i $junk"; done \
    | dd bs=$len count=1 2>/dev/null \
    | gzip >xxx.gz
    cat xxx.gz xxx.gz xxx.gz xxx.gz xxx.gz xxx.gz xxx.gz xxx.gz
done | ../busybox gunzip -c >/dev/null
