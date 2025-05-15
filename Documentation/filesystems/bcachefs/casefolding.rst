.. SPDX-License-Identifier: GPL-2.0

Casefolding
===========

bcachefs has support for case-insensitive file and directory
lookups using the regular `chattr +F` (`S_CASEFOLD`, `FS_CASEFOLD_FL`)
casefolding attributes.

The main usecase for casefolding is compatibility with software written
against other filesystems that rely on casefolded lookups
(eg. NTFS and Wine/Proton).
Taking advantage of file-system level casefolding can lead to great
loading time gains in many applications and games.

Casefolding support requires a kernel with the `CONFIG_UNICODE` enabled.
Once a directory has been flagged for casefolding, a feature bit
is enabled on the superblock which marks the filesystem as using
casefolding.
When the feature bit for casefolding is enabled, it is no longer possible
to mount that filesystem on kernels without `CONFIG_UNICODE` enabled.

On the lookup/query side: casefolding is implemented by allocating a new
string of `BCH_NAME_MAX` length using the `utf8_casefold` function to
casefold the query string.

On the dirent side: casefolding is implemented by ensuring the `bkey`'s
hash is made from the casefolded string and storing the cached casefolded
name with the regular name in the dirent.

The structure looks like this:

* Regular:    [dirent data][regular name][nul][nul]...
* Casefolded: [dirent data][reg len][cf len][regular name][casefolded name][nul][nul]...

(Do note, the number of NULs here is merely for illustration; their count can
vary per-key, and they may not even be present if the key is aligned to
`sizeof(u64)`.)

This is efficient as it means that for all file lookups that require casefolding,
it has identical performance to a regular lookup:
a hash comparison and a `memcmp` of the name.

Rationale
---------

Several designs were considered for this system:
One was to introduce a dirent_v2, however that would be painful especially as
the hash system only has support for a single key type. This would also need
`BCH_NAME_MAX` to change between versions, and a new feature bit.

Another option was to store without the two lengths, and just take the length of
the regular name and casefolded name contiguously / 2 as the length. This would
assume that the regular length == casefolded length, but that could potentially
not be true, if the uppercase unicode glyph had a different UTF-8 encoding than
the lowercase unicode glyph.
It would be possible to disregard the casefold cache for those cases, but it was
decided to simply encode the two string lengths in the key to avoid random
performance issues if this edgecase was ever hit.

The option settled on was to use a free-bit in d_type to mark a dirent as having
a casefold cache, and then treat the first 4 bytes the name block as lengths.
You can see this in the `d_cf_name_block` member of union in `bch_dirent`.

The feature bit was used to allow casefolding support to be enabled for the majority
of users, but some allow users who have no need for the feature to still use bcachefs as
`CONFIG_UNICODE` can increase the kernel side a significant amount due to the tables used,
which may be decider between using bcachefs for eg. embedded platforms.

Other filesystems like ext4 and f2fs have a super-block level option for casefolding
encoding, but bcachefs currently does not provide this. ext4 and f2fs do not expose
any encodings than a single UTF-8 version. When future encodings are desirable,
they will be added trivially using the opts mechanism.

dentry/dcache considerations
----------------------------

Currently, in casefolded directories, bcachefs (like other filesystems) will not cache
negative dentry's.

This is because currently doing so presents a problem in the following scenario:

 - Lookup file "blAH" in a casefolded directory
 - Creation of file "BLAH" in a casefolded directory
 - Lookup file "blAH" in a casefolded directory

This would fail if negative dentry's were cached.

This is slightly suboptimal, but could be fixed in future with some vfs work.

