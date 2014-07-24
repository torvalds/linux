#!/bin/bash
#
# Find the 'UBUNTU: SAUCE:' patches that have been dropped from
# the previous release.
#
PREV_REL=artful
PREV_REPO=git://kernel.ubuntu.com/ubuntu/ubuntu-${PREV_REL}.git

git fetch ${PREV_REPO} master-next
git log --pretty=oneline FETCH_HEAD|grep SAUCE|while read c m;do echo $m;done |sort > $$.prev-rel
git log --pretty=oneline |grep SAUCE|while read c m;do echo $m;done |sort > $$.curr-rel

diff -u $$.prev-rel $$.curr-rel |grep "^-"
rm -f $$.prev-rel $$.curr-rel

