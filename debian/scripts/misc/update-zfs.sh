#!/bin/bash
#
# Update spl/zfs from the Ubuntu archive. You will have to manually check
# to see if the version has been updated.
#
SPL_MAJOR_VER=${_SPL_MAJOR_VER:-0.6.5.4}
SPL_MINOR_VER=${_SPL_MINOR_VER:-0ubuntu1}
SPL_DKMS=${_SPL_DKMS:-http://archive.ubuntu.com/ubuntu/pool/universe/s/spl-linux/spl-dkms_${SPL_MAJOR_VER}-${SPL_MINOR_VER}_amd64.deb}

ZFS_MAJOR_VER=${_ZFS_MAJOR_VER:-0.6.5.4}
ZFS_MINOR_VER=${_ZFS_MINOR_VER:-0ubuntu1}
ZFS_DKMS=${_ZFS_DKMS:-http://archive.ubuntu.com/ubuntu/pool/universe/z/zfs-linux/zfs-dkms_${ZFS_MAJOR_VER}-${ZFS_MINOR_VER}_amd64.deb}

function update_from_archive {

	URL=$1
	DEST_DIR=$2
	VER=$3

	rm -rf ${DEST_DIR}.tmp
	wget -O ${DEST_DIR}.deb ${URL}
	dpkg -x ${DEST_DIR}.deb ${DEST_DIR}.tmp
	mkdir -p ${DEST_DIR}
	rsync -aL --delete ${DEST_DIR}.tmp/usr/src/${DEST_DIR}-${VER}*/ ${DEST_DIR}/
	rm -rf ${DEST_DIR}.deb ${DEST_DIR}.tmp
	find ${DEST_DIR} -type f | while read f;do git add -f $f;done
}

update_from_archive ${SPL_DKMS} spl ${SPL_MAJOR_VER}
update_from_archive ${ZFS_DKMS} zfs ${ZFS_MAJOR_VER}

git add -u
git commit -s -m"UBUNTU: SAUCE: (noup) Update spl to ${SPL_MAJOR_VER}-${SPL_MINOR_VER}, zfs to ${ZFS_MAJOR_VER}-${ZFS_MINOR_VER}"
