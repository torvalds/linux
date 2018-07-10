#!/bin/sh

# Disabling features we do not match exactly:
system_mke2fs='/sbin/mke2fs -O ^resize_inode'
bbox_mke2fs='./busybox mke2fs'

gen_image() { # params: mke2fs_invocation image_name
    >$2
    dd seek=$((kilobytes-1)) bs=1K count=1 </dev/zero of=$2 >/dev/null 2>&1 || exit 1
    $1 -F $2 $kilobytes >$2.raw_out 2>&1 || return 1
    cat $2.raw_out \
    | grep -v '^mke2fs [0-9]*\.[0-9]*\.[0-9]* ' \
    | grep -v '^Maximum filesystem' \
    | grep -v '^Writing inode tables' \
    | grep -v '^Writing superblocks and filesystem accounting information' \
    | grep -v '^This filesystem will be automatically checked every' \
    | grep -v '^180 days, whichever comes first' \
    | sed 's/blocks* unused./blocks unused/' \
    | sed 's/block groups*/block groups/' \
    | sed 's/ *$//' \
    | sed 's/blocks (.*%) reserved/blocks reserved/' \
    | grep -v '^$' \
    >$2.out
}

test_mke2fs() {
    echo Testing $kilobytes

    gen_image "$system_mke2fs" image_std || return 1
    gen_image "$bbox_mke2fs"   image_bb  || return 1

    diff -ua image_bb.out image_std.out >image.out.diff || {
	cat image.out.diff
	return 1
    }

    e2fsck -f -n image_bb >image_bb_e2fsck.out 2>&1 || {
	echo "e2fsck error on image_bb"
	cat image_bb_e2fsck.out
	exit 1
    }
}

# -:bbox +:standard

# kilobytes=60 is the minimal allowed size
kilobytes=60
while true; do
    test_mke2fs || exit 1
    kilobytes=$((kilobytes+1))
    test $kilobytes = 200 && break
done

# Transition from one block group to two
# fails in [8378..8410] range unless -O ^resize_inode
kilobytes=$((1 * 8*1024 - 50))
while true; do
    test_mke2fs || exit 1
    kilobytes=$((kilobytes+1))
    test $kilobytes = $((1 * 8*1024 + 300)) && break
done

# Transition from 2 block groups to 3
# works
kilobytes=$((2 * 8*1024 - 50))
while true; do
    test_mke2fs || exit 1
    kilobytes=$((kilobytes+1))
    test $kilobytes = $((2 * 8*1024 + 400)) && break
done

# Transition from 3 block groups to 4
# fails in [24825..24922] range unless -O ^resize_inode
kilobytes=$((3 * 8*1024 - 50))
while true; do
    test_mke2fs || exit 1
    kilobytes=$((kilobytes+1))
    test $kilobytes = $((3 * 8*1024 + 500)) && break
done

# Transition from 4 block groups to 5
# works
kilobytes=$((4 * 8*1024 - 50))
while true; do
    test_mke2fs || exit 1
    kilobytes=$((kilobytes+1))
    test $kilobytes = $((4 * 8*1024 + 600)) && break
done

# Transition from 5 block groups to 6
# fails in [41230..41391] range unless -O ^resize_inode
kilobytes=$((5 * 8*1024 - 50))
while true; do
    test_mke2fs || exit 1
    kilobytes=$((kilobytes+1))
    test $kilobytes = $((5 * 8*1024 + 700)) && break
done
exit

# Random sizes
while true; do
    kilobytes=$(( (RANDOM*RANDOM) % 5000000 + 60))
    test_mke2fs || exit 1
done
