#!/bin/bash

AUFS=aufs4-standalone

#
# Before you run this be sure you've removed or reverted the 'UBUNTU: SAUCE: AUFS" patch.
#
#
# Make sure the current working directory is at the top of the
# linux tree.
#
if ! grep PATCHLEVEL Makefile
then
	echo "You must run this script from the top of the linux tree"
	exit 1
fi

clean=0
if [ "$#" = 1 ]; then
	AUFS="$1"
else
	clean=1
	rm -rf ${AUFS}
	git clone https://github.com/sfjro/aufs5-standalone.git ${AUFS}
	(cd ${AUFS}; git checkout -b aufs5.x-rcN remotes/origin/aufs5.x-rcN)
fi

cp ${AUFS}/include/uapi/linux/aufs_type.h include/uapi/linux
rsync -av ${AUFS}/fs/ fs/
rsync -av ${AUFS}/Documentation/ Documentation/

PATCHES="${PATCHES} aufs5-kbuild.patch"
PATCHES="${PATCHES} aufs5-base.patch"
PATCHES="${PATCHES} aufs5-mmap.patch"
PATCHES="${PATCHES} aufs5-standalone.patch"
PATCHES="${PATCHES} aufs5-loopback.patch"
#PATCHES="${PATCHES} vfs-ino.patch"
#PATCHES="${PATCHES} tmpfs-idr.patch"

for i in ${PATCHES}
do
	patch -p1 < ${AUFS}/$i
done

[ "$clean" = 1 ] && rm -rf ${AUFS}
git add mm/prfile.c
git add -u
find . -name "*.orig" | xargs rm
find . |grep aufs | xargs git add
git commit -s -m"UBUNTU: SAUCE: AUFS"
