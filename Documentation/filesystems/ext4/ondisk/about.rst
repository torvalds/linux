.. SPDX-License-Identifier: GPL-2.0

About this Book
===============

This document attempts to describe the on-disk format for ext4
filesystems. The same general ideas should apply to ext2/3 filesystems
as well, though they do not support all the features that ext4 supports,
and the fields will be shorter.

**NOTE**: This is a work in progress, based on notes that the author
(djwong) made while picking apart a filesystem by hand. The data
structure definitions should be current as of Linux 4.18 and
e2fsprogs-1.44. All comments and corrections are welcome, since there is
undoubtedly plenty of lore that might not be reflected in freshly
created demonstration filesystems.

License
-------
This book is licensed under the terms of the GNU Public License, v2.

Terminology
-----------

ext4 divides a storage device into an array of logical blocks both to
reduce bookkeeping overhead and to increase throughput by forcing larger
transfer sizes. Generally, the block size will be 4KiB (the same size as
pages on x86 and the block layer's default block size), though the
actual size is calculated as 2 ^ (10 + ``sb.s_log_block_size``) bytes.
Throughout this document, disk locations are given in terms of these
logical blocks, not raw LBAs, and not 1024-byte blocks. For the sake of
convenience, the logical block size will be referred to as
``$block_size`` throughout the rest of the document.

When referenced in ``preformatted text`` blocks, ``sb`` refers to fields
in the super block, and ``inode`` refers to fields in an inode table
entry.

Other References
----------------

Also see http://www.nongnu.org/ext2-doc/ for quite a collection of
information about ext2/3. Here's another old reference:
http://wiki.osdev.org/Ext2
