.. SPDX-License-Identifier: GPL-2.0

Verity files
------------

ext4 supports fs-verity, which is a filesystem feature that provides
Merkle tree based hashing for individual readonly files.  Most of
fs-verity is common to all filesystems that support it; see
:ref:`Documentation/filesystems/fsverity.rst <fsverity>` for the
fs-verity documentation.  However, the on-disk layout of the verity
metadata is filesystem-specific.  On ext4, the verity metadata is
stored after the end of the file data itself, in the following format:

- Zero-padding to the next 65536-byte boundary.  This padding need not
  actually be allocated on-disk, i.e. it may be a hole.

- The Merkle tree, as documented in
  :ref:`Documentation/filesystems/fsverity.rst
  <fsverity_merkle_tree>`, with the tree levels stored in order from
  root to leaf, and the tree blocks within each level stored in their
  natural order.

- Zero-padding to the next filesystem block boundary.

- The verity descriptor, as documented in
  :ref:`Documentation/filesystems/fsverity.rst <fsverity_descriptor>`,
  with optionally appended signature blob.

- Zero-padding to the next offset that is 4 bytes before a filesystem
  block boundary.

- The size of the verity descriptor in bytes, as a 4-byte little
  endian integer.

Verity inodes have EXT4_VERITY_FL set, and they must use extents, i.e.
EXT4_EXTENTS_FL must be set and EXT4_INLINE_DATA_FL must be clear.
They can have EXT4_ENCRYPT_FL set, in which case the verity metadata
is encrypted as well as the data itself.

Verity files cannot have blocks allocated past the end of the verity
metadata.
