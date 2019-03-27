#! /usr/bin/atf-sh
#	$NetBSD: t_in_cksum.sh,v 1.2 2015/01/06 15:13:16 martin Exp $
#

TIMING_LOOPS=10000
incksum="$(atf_get_srcdir)/in_cksum"

fail() {
    atf_fail "see output for details"
}

mbufs() {
    ${incksum} -l 16 -u $0 -i ${TIMING_LOOPS} \
	1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 \
	|| fail
    ${incksum} -l 16 -u $0 -i ${TIMING_LOOPS} \
	1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 \
	1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 \
	|| fail
    ${incksum} -l 64 -u $0 -i ${TIMING_LOOPS} \
	1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 \
	1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 \
	|| fail
    ${incksum} -l 16 -u $0 -i ${TIMING_LOOPS}	\
	1 3 1 3 1 3 1 \
	|| fail
}

sizes() {
    ${incksum} -l 16 -u $1 -i ${TIMING_LOOPS}	2048 || fail
    ${incksum} -l 16 -u $1 -i ${TIMING_LOOPS}	40 || fail
    ${incksum} -l 16 -u $1 -i ${TIMING_LOOPS}	1536 || fail
    ${incksum} -l 16 -u $1 -i ${TIMING_LOOPS}	576 || fail
    ${incksum} -l 16 -u $1 -i ${TIMING_LOOPS}	1536 1536 1536 1536 1536 640 \
	 || fail
}

atf_test_case mbufs_aligned

mbufs_aligned_head() {
	atf_set "descr" "Test in_cksum mbuf chains aligned"
}

mbufs_aligned_body() {
	mbufs 0
}

mbufs_unaligned_head() {
	atf_set "descr" "Test in_cksum mbuf chains unaligned"
}

mbufs_unaligned_body() {
	mbufs 1
}

sizes_aligned_head() {
	atf_set "descr" "Test in_cksum sizes aligned"
}

sizes_aligned_body() {
	sizes 0
}

sizes_unaligned_head() {
	atf_set "descr" "Test in_cksum sizes unaligned"
}

sizes_unaligned_body() {
	sizes 1
}

atf_init_test_cases()
{
	atf_add_test_case mbufs_aligned
	atf_add_test_case mbufs_unaligned
	atf_add_test_case sizes_aligned
	atf_add_test_case sizes_unaligned
}
