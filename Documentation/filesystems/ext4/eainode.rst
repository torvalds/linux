.. SPDX-License-Identifier: GPL-2.0

Large Extended Attribute Values
-------------------------------

To enable ext4 to store extended attribute values that do not fit in the
inode or in the single extended attribute block attached to an inode,
the EA_INODE feature allows us to store the value in the data blocks of
a regular file inode. This “EA inode” is linked only from the extended
attribute name index and must not appear in a directory entry. The
inode's i_atime field is used to store a checksum of the xattr value;
and i_ctime/i_version store a 64-bit reference count, which enables
sharing of large xattr values between multiple owning inodes. For
backward compatibility with older versions of this feature, the
i_mtime/i_generation *may* store a back-reference to the inode number
and i_generation of the **one** owning inode (in cases where the EA
inode is not referenced by multiple inodes) to verify that the EA inode
is the correct one being accessed.
