.. SPDX-License-Identifier: GPL-2.0

=====
BTRFS
=====

Btrfs is a copy on write filesystem for Linux aimed at implementing advanced
features while focusing on fault tolerance, repair and easy administration.
Jointly developed by several companies, licensed under the GPL and open for
contribution from anyone.

The main Btrfs features include:

    * Extent based file storage (2^64 max file size)
    * Space efficient packing of small files
    * Space efficient indexed directories
    * Dynamic inode allocation
    * Writable snapshots
    * Subvolumes (separate internal filesystem roots)
    * Object level mirroring and striping
    * Checksums on data and metadata (multiple algorithms available)
    * Compression (multiple algorithms available)
    * Reflink, deduplication
    * Scrub (on-line checksum verification)
    * Hierarchical quota groups (subvolume and snapshot support)
    * Integrated multiple device support, with several raid algorithms
    * Offline filesystem check
    * Efficient incremental backup and FS mirroring (send/receive)
    * Trim/discard
    * Online filesystem defragmentation
    * Swapfile support
    * Zoned mode
    * Read/write metadata verification
    * Online resize (shrink, grow)

For more information please refer to the documentation site or wiki

  https://btrfs.readthedocs.io


that maintains information about administration tasks, frequently asked
questions, use cases, mount options, comprehensible changelogs, features,
manual pages, source code repositories, contacts etc.
