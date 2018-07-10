#!/bin/sh
# Test that concatenated gz files are unpacking correctly.
# It also tests that unpacking in general is working right.
# Since zip code has many corner cases, run it for a few hours
# to get a decent coverage (200000 tests or more).

gzip="gzip"
gunzip="../busybox gunzip"
# Or the other way around:
#gzip="../busybox gzip"
#gunzip="gunzip"

c=0
i=$PID
while true; do
    c=$((c+1))

    # RANDOM is not very random on some shells. Spice it up.
    # 100003 is prime
    len1=$(( (((RANDOM*RANDOM)^i) & 0x7ffffff) % 100003 ))
    i=$((i * 1664525 + 1013904223))
    len2=$(( (((RANDOM*RANDOM)^i) & 0x7ffffff) % 100003 ))

    # Just using urandom will make gzip use method 0 (store) -
    # not good for test coverage!
    cat /dev/urandom | while true; do read junk; echo "junk $c $i $junk"; done \
    | dd bs=$len1 count=1 >z1 2>/dev/null
    cat /dev/urandom | while true; do read junk; echo "junk $c $i $junk"; done \
    | dd bs=$len2 count=1 >z2 2>/dev/null

    $gzip <z1 >zz.gz
    $gzip <z2 >>zz.gz
    $gunzip -c zz.gz >z9 || {
	echo "Exitcode $?"
	exit
    }
    sum=`cat z1 z2 | md5sum`
    sum9=`md5sum <z9`
    test "$sum" == "$sum9" || {
	echo "md5sums don't match"
	exit
    }
    echo "Test $c ok: len1=$len1 len2=$len2 sum=$sum"

    sum=`cat z1 z2 z1 z2 | md5sum`
    rm z1.gz z2.gz 2>/dev/null
    $gzip z1
    $gzip z2
    cat z1.gz z2.gz z1.gz z2.gz >zz.gz
    $gunzip -c zz.gz >z9 || {
	echo "Exitcode $? (2)"
	exit
    }
    sum9=`md5sum <z9`
    test "$sum" == "$sum9" || {
	echo "md5sums don't match (1)"
	exit
    }

    echo "Test $c ok: len1=$len1 len2=$len2 sum=$sum (2)"
done
