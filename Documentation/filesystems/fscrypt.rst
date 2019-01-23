=====================================
Filesystem-level encryption (fscrypt)
=====================================

Introduction
============

fscrypt is a library which filesystems can hook into to support
transparent encryption of files and directories.

Note: "fscrypt" in this document refers to the kernel-level portion,
implemented in ``fs/crypto/``, as opposed to the userspace tool
`fscrypt <https://github.com/google/fscrypt>`_.  This document only
covers the kernel-level portion.  For command-line examples of how to
use encryption, see the documentation for the userspace tool `fscrypt
<https://github.com/google/fscrypt>`_.  Also, it is recommended to use
the fscrypt userspace tool, or other existing userspace tools such as
`fscryptctl <https://github.com/google/fscryptctl>`_ or `Android's key
management system
<https://source.android.com/security/encryption/file-based>`_, over
using the kernel's API directly.  Using existing tools reduces the
chance of introducing your own security bugs.  (Nevertheless, for
completeness this documentation covers the kernel's API anyway.)

Unlike dm-crypt, fscrypt operates at the filesystem level rather than
at the block device level.  This allows it to encrypt different files
with different keys and to have unencrypted files on the same
filesystem.  This is useful for multi-user systems where each user's
data-at-rest needs to be cryptographically isolated from the others.
However, except for filenames, fscrypt does not encrypt filesystem
metadata.

Unlike eCryptfs, which is a stacked filesystem, fscrypt is integrated
directly into supported filesystems --- currently ext4, F2FS, and
UBIFS.  This allows encrypted files to be read and written without
caching both the decrypted and encrypted pages in the pagecache,
thereby nearly halving the memory used and bringing it in line with
unencrypted files.  Similarly, half as many dentries and inodes are
needed.  eCryptfs also limits encrypted filenames to 143 bytes,
causing application compatibility issues; fscrypt allows the full 255
bytes (NAME_MAX).  Finally, unlike eCryptfs, the fscrypt API can be
used by unprivileged users, with no need to mount anything.

fscrypt does not support encrypting files in-place.  Instead, it
supports marking an empty directory as encrypted.  Then, after
userspace provides the key, all regular files, directories, and
symbolic links created in that directory tree are transparently
encrypted.

Threat model
============

Offline attacks
---------------

Provided that userspace chooses a strong encryption key, fscrypt
protects the confidentiality of file contents and filenames in the
event of a single point-in-time permanent offline compromise of the
block device content.  fscrypt does not protect the confidentiality of
non-filename metadata, e.g. file sizes, file permissions, file
timestamps, and extended attributes.  Also, the existence and location
of holes (unallocated blocks which logically contain all zeroes) in
files is not protected.

fscrypt is not guaranteed to protect confidentiality or authenticity
if an attacker is able to manipulate the filesystem offline prior to
an authorized user later accessing the filesystem.

Online attacks
--------------

fscrypt (and storage encryption in general) can only provide limited
protection, if any at all, against online attacks.  In detail:

fscrypt is only resistant to side-channel attacks, such as timing or
electromagnetic attacks, to the extent that the underlying Linux
Cryptographic API algorithms are.  If a vulnerable algorithm is used,
such as a table-based implementation of AES, it may be possible for an
attacker to mount a side channel attack against the online system.
Side channel attacks may also be mounted against applications
consuming decrypted data.

After an encryption key has been provided, fscrypt is not designed to
hide the plaintext file contents or filenames from other users on the
same system, regardless of the visibility of the keyring key.
Instead, existing access control mechanisms such as file mode bits,
POSIX ACLs, LSMs, or mount namespaces should be used for this purpose.
Also note that as long as the encryption keys are *anywhere* in
memory, an online attacker can necessarily compromise them by mounting
a physical attack or by exploiting any kernel security vulnerability
which provides an arbitrary memory read primitive.

While it is ostensibly possible to "evict" keys from the system,
recently accessed encrypted files will remain accessible at least
until the filesystem is unmounted or the VFS caches are dropped, e.g.
using ``echo 2 > /proc/sys/vm/drop_caches``.  Even after that, if the
RAM is compromised before being powered off, it will likely still be
possible to recover portions of the plaintext file contents, if not
some of the encryption keys as well.  (Since Linux v4.12, all
in-kernel keys related to fscrypt are sanitized before being freed.
However, userspace would need to do its part as well.)

Currently, fscrypt does not prevent a user from maliciously providing
an incorrect key for another user's existing encrypted files.  A
protection against this is planned.

Key hierarchy
=============

Master Keys
-----------

Each encrypted directory tree is protected by a *master key*.  Master
keys can be up to 64 bytes long, and must be at least as long as the
greater of the key length needed by the contents and filenames
encryption modes being used.  For example, if AES-256-XTS is used for
contents encryption, the master key must be 64 bytes (512 bits).  Note
that the XTS mode is defined to require a key twice as long as that
required by the underlying block cipher.

To "unlock" an encrypted directory tree, userspace must provide the
appropriate master key.  There can be any number of master keys, each
of which protects any number of directory trees on any number of
filesystems.

Userspace should generate master keys either using a cryptographically
secure random number generator, or by using a KDF (Key Derivation
Function).  Note that whenever a KDF is used to "stretch" a
lower-entropy secret such as a passphrase, it is critical that a KDF
designed for this purpose be used, such as scrypt, PBKDF2, or Argon2.

Per-file keys
-------------

Master keys are not used to encrypt file contents or names directly.
Instead, a unique key is derived for each encrypted file, including
each regular file, directory, and symbolic link.  This has several
advantages:

- In cryptosystems, the same key material should never be used for
  different purposes.  Using the master key as both an XTS key for
  contents encryption and as a CTS-CBC key for filenames encryption
  would violate this rule.
- Per-file keys simplify the choice of IVs (Initialization Vectors)
  for contents encryption.  Without per-file keys, to ensure IV
  uniqueness both the inode and logical block number would need to be
  encoded in the IVs.  This would make it impossible to renumber
  inodes, which e.g. ``resize2fs`` can do when resizing an ext4
  filesystem.  With per-file keys, it is sufficient to encode just the
  logical block number in the IVs.
- Per-file keys strengthen the encryption of filenames, where IVs are
  reused out of necessity.  With a unique key per directory, IV reuse
  is limited to within a single directory.
- Per-file keys allow individual files to be securely erased simply by
  securely erasing their keys.  (Not yet implemented.)

A KDF (Key Derivation Function) is used to derive per-file keys from
the master key.  This is done instead of wrapping a randomly-generated
key for each file because it reduces the size of the encryption xattr,
which for some filesystems makes the xattr more likely to fit in-line
in the filesystem's inode table.  With a KDF, only a 16-byte nonce is
required --- long enough to make key reuse extremely unlikely.  A
wrapped key, on the other hand, would need to be up to 64 bytes ---
the length of an AES-256-XTS key.  Furthermore, currently there is no
requirement to support unlocking a file with multiple alternative
master keys or to support rotating master keys.  Instead, the master
keys may be wrapped in userspace, e.g. as done by the `fscrypt
<https://github.com/google/fscrypt>`_ tool.

The current KDF encrypts the master key using the 16-byte nonce as an
AES-128-ECB key.  The output is used as the derived key.  If the
output is longer than needed, then it is truncated to the needed
length.  Truncation is the norm for directories and symlinks, since
those use the CTS-CBC encryption mode which requires a key half as
long as that required by the XTS encryption mode.

Note: this KDF meets the primary security requirement, which is to
produce unique derived keys that preserve the entropy of the master
key, assuming that the master key is already a good pseudorandom key.
However, it is nonstandard and has some problems such as being
reversible, so it is generally considered to be a mistake!  It may be
replaced with HKDF or another more standard KDF in the future.

Encryption modes and usage
==========================

fscrypt allows one encryption mode to be specified for file contents
and one encryption mode to be specified for filenames.  Different
directory trees are permitted to use different encryption modes.
Currently, the following pairs of encryption modes are supported:

- AES-256-XTS for contents and AES-256-CTS-CBC for filenames
- AES-128-CBC for contents and AES-128-CTS-CBC for filenames

It is strongly recommended to use AES-256-XTS for contents encryption.
AES-128-CBC was added only for low-powered embedded devices with
crypto accelerators such as CAAM or CESA that do not support XTS.

New encryption modes can be added relatively easily, without changes
to individual filesystems.  However, authenticated encryption (AE)
modes are not currently supported because of the difficulty of dealing
with ciphertext expansion.

For file contents, each filesystem block is encrypted independently.
Currently, only the case where the filesystem block size is equal to
the system's page size (usually 4096 bytes) is supported.  With the
XTS mode of operation (recommended), the logical block number within
the file is used as the IV.  With the CBC mode of operation (not
recommended), ESSIV is used; specifically, the IV for CBC is the
logical block number encrypted with AES-256, where the AES-256 key is
the SHA-256 hash of the inode's data encryption key.

For filenames, the full filename is encrypted at once.  Because of the
requirements to retain support for efficient directory lookups and
filenames of up to 255 bytes, a constant initialization vector (IV) is
used.  However, each encrypted directory uses a unique key, which
limits IV reuse to within a single directory.  Note that IV reuse in
the context of CTS-CBC encryption means that when the original
filenames share a common prefix at least as long as the cipher block
size (16 bytes for AES), the corresponding encrypted filenames will
also share a common prefix.  This is undesirable; it may be fixed in
the future by switching to an encryption mode that is a strong
pseudorandom permutation on arbitrary-length messages, e.g. the HEH
(Hash-Encrypt-Hash) mode.

Since filenames are encrypted with the CTS-CBC mode of operation, the
plaintext and ciphertext filenames need not be multiples of the AES
block size, i.e. 16 bytes.  However, the minimum size that can be
encrypted is 16 bytes, so shorter filenames are NUL-padded to 16 bytes
before being encrypted.  In addition, to reduce leakage of filename
lengths via their ciphertexts, all filenames are NUL-padded to the
next 4, 8, 16, or 32-byte boundary (configurable).  32 is recommended
since this provides the best confidentiality, at the cost of making
directory entries consume slightly more space.  Note that since NUL
(``\0``) is not otherwise a valid character in filenames, the padding
will never produce duplicate plaintexts.

Symbolic link targets are considered a type of filename and are
encrypted in the same way as filenames in directory entries.  Each
symlink also uses a unique key; hence, the hardcoded IV is not a
problem for symlinks.

User API
========

Setting an encryption policy
----------------------------

The FS_IOC_SET_ENCRYPTION_POLICY ioctl sets an encryption policy on an
empty directory or verifies that a directory or regular file already
has the specified encryption policy.  It takes in a pointer to a
:c:type:`struct fscrypt_policy`, defined as follows::

    #define FS_KEY_DESCRIPTOR_SIZE  8

    struct fscrypt_policy {
            __u8 version;
            __u8 contents_encryption_mode;
            __u8 filenames_encryption_mode;
            __u8 flags;
            __u8 master_key_descriptor[FS_KEY_DESCRIPTOR_SIZE];
    };

This structure must be initialized as follows:

- ``version`` must be 0.

- ``contents_encryption_mode`` and ``filenames_encryption_mode`` must
  be set to constants from ``<linux/fs.h>`` which identify the
  encryption modes to use.  If unsure, use
  FS_ENCRYPTION_MODE_AES_256_XTS (1) for ``contents_encryption_mode``
  and FS_ENCRYPTION_MODE_AES_256_CTS (4) for
  ``filenames_encryption_mode``.

- ``flags`` must be set to a value from ``<linux/fs.h>`` which
  identifies the amount of NUL-padding to use when encrypting
  filenames.  If unsure, use FS_POLICY_FLAGS_PAD_32 (0x3).

- ``master_key_descriptor`` specifies how to find the master key in
  the keyring; see `Adding keys`_.  It is up to userspace to choose a
  unique ``master_key_descriptor`` for each master key.  The e4crypt
  and fscrypt tools use the first 8 bytes of
  ``SHA-512(SHA-512(master_key))``, but this particular scheme is not
  required.  Also, the master key need not be in the keyring yet when
  FS_IOC_SET_ENCRYPTION_POLICY is executed.  However, it must be added
  before any files can be created in the encrypted directory.

If the file is not yet encrypted, then FS_IOC_SET_ENCRYPTION_POLICY
verifies that the file is an empty directory.  If so, the specified
encryption policy is assigned to the directory, turning it into an
encrypted directory.  After that, and after providing the
corresponding master key as described in `Adding keys`_, all regular
files, directories (recursively), and symlinks created in the
directory will be encrypted, inheriting the same encryption policy.
The filenames in the directory's entries will be encrypted as well.

Alternatively, if the file is already encrypted, then
FS_IOC_SET_ENCRYPTION_POLICY validates that the specified encryption
policy exactly matches the actual one.  If they match, then the ioctl
returns 0.  Otherwise, it fails with EEXIST.  This works on both
regular files and directories, including nonempty directories.

Note that the ext4 filesystem does not allow the root directory to be
encrypted, even if it is empty.  Users who want to encrypt an entire
filesystem with one key should consider using dm-crypt instead.

FS_IOC_SET_ENCRYPTION_POLICY can fail with the following errors:

- ``EACCES``: the file is not owned by the process's uid, nor does the
  process have the CAP_FOWNER capability in a namespace with the file
  owner's uid mapped
- ``EEXIST``: the file is already encrypted with an encryption policy
  different from the one specified
- ``EINVAL``: an invalid encryption policy was specified (invalid
  version, mode(s), or flags)
- ``ENOTDIR``: the file is unencrypted and is a regular file, not a
  directory
- ``ENOTEMPTY``: the file is unencrypted and is a nonempty directory
- ``ENOTTY``: this type of filesystem does not implement encryption
- ``EOPNOTSUPP``: the kernel was not configured with encryption
  support for this filesystem, or the filesystem superblock has not
  had encryption enabled on it.  (For example, to use encryption on an
  ext4 filesystem, CONFIG_EXT4_ENCRYPTION must be enabled in the
  kernel config, and the superblock must have had the "encrypt"
  feature flag enabled using ``tune2fs -O encrypt`` or ``mkfs.ext4 -O
  encrypt``.)
- ``EPERM``: this directory may not be encrypted, e.g. because it is
  the root directory of an ext4 filesystem
- ``EROFS``: the filesystem is readonly

Getting an encryption policy
----------------------------

The FS_IOC_GET_ENCRYPTION_POLICY ioctl retrieves the :c:type:`struct
fscrypt_policy`, if any, for a directory or regular file.  See above
for the struct definition.  No additional permissions are required
beyond the ability to open the file.

FS_IOC_GET_ENCRYPTION_POLICY can fail with the following errors:

- ``EINVAL``: the file is encrypted, but it uses an unrecognized
  encryption context format
- ``ENODATA``: the file is not encrypted
- ``ENOTTY``: this type of filesystem does not implement encryption
- ``EOPNOTSUPP``: the kernel was not configured with encryption
  support for this filesystem

Note: if you only need to know whether a file is encrypted or not, on
most filesystems it is also possible to use the FS_IOC_GETFLAGS ioctl
and check for FS_ENCRYPT_FL, or to use the statx() system call and
check for STATX_ATTR_ENCRYPTED in stx_attributes.

Getting the per-filesystem salt
-------------------------------

Some filesystems, such as ext4 and F2FS, also support the deprecated
ioctl FS_IOC_GET_ENCRYPTION_PWSALT.  This ioctl retrieves a randomly
generated 16-byte value stored in the filesystem superblock.  This
value is intended to used as a salt when deriving an encryption key
from a passphrase or other low-entropy user credential.

FS_IOC_GET_ENCRYPTION_PWSALT is deprecated.  Instead, prefer to
generate and manage any needed salt(s) in userspace.

Adding keys
-----------

To provide a master key, userspace must add it to an appropriate
keyring using the add_key() system call (see:
``Documentation/security/keys/core.rst``).  The key type must be
"logon"; keys of this type are kept in kernel memory and cannot be
read back by userspace.  The key description must be "fscrypt:"
followed by the 16-character lower case hex representation of the
``master_key_descriptor`` that was set in the encryption policy.  The
key payload must conform to the following structure::

    #define FS_MAX_KEY_SIZE 64

    struct fscrypt_key {
            u32 mode;
            u8 raw[FS_MAX_KEY_SIZE];
            u32 size;
    };

``mode`` is ignored; just set it to 0.  The actual key is provided in
``raw`` with ``size`` indicating its size in bytes.  That is, the
bytes ``raw[0..size-1]`` (inclusive) are the actual key.

The key description prefix "fscrypt:" may alternatively be replaced
with a filesystem-specific prefix such as "ext4:".  However, the
filesystem-specific prefixes are deprecated and should not be used in
new programs.

There are several different types of keyrings in which encryption keys
may be placed, such as a session keyring, a user session keyring, or a
user keyring.  Each key must be placed in a keyring that is "attached"
to all processes that might need to access files encrypted with it, in
the sense that request_key() will find the key.  Generally, if only
processes belonging to a specific user need to access a given
encrypted directory and no session keyring has been installed, then
that directory's key should be placed in that user's user session
keyring or user keyring.  Otherwise, a session keyring should be
installed if needed, and the key should be linked into that session
keyring, or in a keyring linked into that session keyring.

Note: introducing the complex visibility semantics of keyrings here
was arguably a mistake --- especially given that by design, after any
process successfully opens an encrypted file (thereby setting up the
per-file key), possessing the keyring key is not actually required for
any process to read/write the file until its in-memory inode is
evicted.  In the future there probably should be a way to provide keys
directly to the filesystem instead, which would make the intended
semantics clearer.

Access semantics
================

With the key
------------

With the encryption key, encrypted regular files, directories, and
symlinks behave very similarly to their unencrypted counterparts ---
after all, the encryption is intended to be transparent.  However,
astute users may notice some differences in behavior:

- Unencrypted files, or files encrypted with a different encryption
  policy (i.e. different key, modes, or flags), cannot be renamed or
  linked into an encrypted directory; see `Encryption policy
  enforcement`_.  Attempts to do so will fail with EXDEV.  However,
  encrypted files can be renamed within an encrypted directory, or
  into an unencrypted directory.

  Note: "moving" an unencrypted file into an encrypted directory, e.g.
  with the `mv` program, is implemented in userspace by a copy
  followed by a delete.  Be aware that the original unencrypted data
  may remain recoverable from free space on the disk; prefer to keep
  all files encrypted from the very beginning.  The `shred` program
  may be used to overwrite the source files but isn't guaranteed to be
  effective on all filesystems and storage devices.

- Direct I/O is not supported on encrypted files.  Attempts to use
  direct I/O on such files will fall back to buffered I/O.

- The fallocate operations FALLOC_FL_COLLAPSE_RANGE,
  FALLOC_FL_INSERT_RANGE, and FALLOC_FL_ZERO_RANGE are not supported
  on encrypted files and will fail with EOPNOTSUPP.

- Online defragmentation of encrypted files is not supported.  The
  EXT4_IOC_MOVE_EXT and F2FS_IOC_MOVE_RANGE ioctls will fail with
  EOPNOTSUPP.

- The ext4 filesystem does not support data journaling with encrypted
  regular files.  It will fall back to ordered data mode instead.

- DAX (Direct Access) is not supported on encrypted files.

- The st_size of an encrypted symlink will not necessarily give the
  length of the symlink target as required by POSIX.  It will actually
  give the length of the ciphertext, which will be slightly longer
  than the plaintext due to NUL-padding and an extra 2-byte overhead.

- The maximum length of an encrypted symlink is 2 bytes shorter than
  the maximum length of an unencrypted symlink.  For example, on an
  EXT4 filesystem with a 4K block size, unencrypted symlinks can be up
  to 4095 bytes long, while encrypted symlinks can only be up to 4093
  bytes long (both lengths excluding the terminating null).

Note that mmap *is* supported.  This is possible because the pagecache
for an encrypted file contains the plaintext, not the ciphertext.

Without the key
---------------

Some filesystem operations may be performed on encrypted regular
files, directories, and symlinks even before their encryption key has
been provided:

- File metadata may be read, e.g. using stat().

- Directories may be listed, in which case the filenames will be
  listed in an encoded form derived from their ciphertext.  The
  current encoding algorithm is described in `Filename hashing and
  encoding`_.  The algorithm is subject to change, but it is
  guaranteed that the presented filenames will be no longer than
  NAME_MAX bytes, will not contain the ``/`` or ``\0`` characters, and
  will uniquely identify directory entries.

  The ``.`` and ``..`` directory entries are special.  They are always
  present and are not encrypted or encoded.

- Files may be deleted.  That is, nondirectory files may be deleted
  with unlink() as usual, and empty directories may be deleted with
  rmdir() as usual.  Therefore, ``rm`` and ``rm -r`` will work as
  expected.

- Symlink targets may be read and followed, but they will be presented
  in encrypted form, similar to filenames in directories.  Hence, they
  are unlikely to point to anywhere useful.

Without the key, regular files cannot be opened or truncated.
Attempts to do so will fail with ENOKEY.  This implies that any
regular file operations that require a file descriptor, such as
read(), write(), mmap(), fallocate(), and ioctl(), are also forbidden.

Also without the key, files of any type (including directories) cannot
be created or linked into an encrypted directory, nor can a name in an
encrypted directory be the source or target of a rename, nor can an
O_TMPFILE temporary file be created in an encrypted directory.  All
such operations will fail with ENOKEY.

It is not currently possible to backup and restore encrypted files
without the encryption key.  This would require special APIs which
have not yet been implemented.

Encryption policy enforcement
=============================

After an encryption policy has been set on a directory, all regular
files, directories, and symbolic links created in that directory
(recursively) will inherit that encryption policy.  Special files ---
that is, named pipes, device nodes, and UNIX domain sockets --- will
not be encrypted.

Except for those special files, it is forbidden to have unencrypted
files, or files encrypted with a different encryption policy, in an
encrypted directory tree.  Attempts to link or rename such a file into
an encrypted directory will fail with EXDEV.  This is also enforced
during ->lookup() to provide limited protection against offline
attacks that try to disable or downgrade encryption in known locations
where applications may later write sensitive data.  It is recommended
that systems implementing a form of "verified boot" take advantage of
this by validating all top-level encryption policies prior to access.

Implementation details
======================

Encryption context
------------------

An encryption policy is represented on-disk by a :c:type:`struct
fscrypt_context`.  It is up to individual filesystems to decide where
to store it, but normally it would be stored in a hidden extended
attribute.  It should *not* be exposed by the xattr-related system
calls such as getxattr() and setxattr() because of the special
semantics of the encryption xattr.  (In particular, there would be
much confusion if an encryption policy were to be added to or removed
from anything other than an empty directory.)  The struct is defined
as follows::

    #define FS_KEY_DESCRIPTOR_SIZE  8
    #define FS_KEY_DERIVATION_NONCE_SIZE 16

    struct fscrypt_context {
            u8 format;
            u8 contents_encryption_mode;
            u8 filenames_encryption_mode;
            u8 flags;
            u8 master_key_descriptor[FS_KEY_DESCRIPTOR_SIZE];
            u8 nonce[FS_KEY_DERIVATION_NONCE_SIZE];
    };

Note that :c:type:`struct fscrypt_context` contains the same
information as :c:type:`struct fscrypt_policy` (see `Setting an
encryption policy`_), except that :c:type:`struct fscrypt_context`
also contains a nonce.  The nonce is randomly generated by the kernel
and is used to derive the inode's encryption key as described in
`Per-file keys`_.

Data path changes
-----------------

For the read path (->readpage()) of regular files, filesystems can
read the ciphertext into the page cache and decrypt it in-place.  The
page lock must be held until decryption has finished, to prevent the
page from becoming visible to userspace prematurely.

For the write path (->writepage()) of regular files, filesystems
cannot encrypt data in-place in the page cache, since the cached
plaintext must be preserved.  Instead, filesystems must encrypt into a
temporary buffer or "bounce page", then write out the temporary
buffer.  Some filesystems, such as UBIFS, already use temporary
buffers regardless of encryption.  Other filesystems, such as ext4 and
F2FS, have to allocate bounce pages specially for encryption.

Filename hashing and encoding
-----------------------------

Modern filesystems accelerate directory lookups by using indexed
directories.  An indexed directory is organized as a tree keyed by
filename hashes.  When a ->lookup() is requested, the filesystem
normally hashes the filename being looked up so that it can quickly
find the corresponding directory entry, if any.

With encryption, lookups must be supported and efficient both with and
without the encryption key.  Clearly, it would not work to hash the
plaintext filenames, since the plaintext filenames are unavailable
without the key.  (Hashing the plaintext filenames would also make it
impossible for the filesystem's fsck tool to optimize encrypted
directories.)  Instead, filesystems hash the ciphertext filenames,
i.e. the bytes actually stored on-disk in the directory entries.  When
asked to do a ->lookup() with the key, the filesystem just encrypts
the user-supplied name to get the ciphertext.

Lookups without the key are more complicated.  The raw ciphertext may
contain the ``\0`` and ``/`` characters, which are illegal in
filenames.  Therefore, readdir() must base64-encode the ciphertext for
presentation.  For most filenames, this works fine; on ->lookup(), the
filesystem just base64-decodes the user-supplied name to get back to
the raw ciphertext.

However, for very long filenames, base64 encoding would cause the
filename length to exceed NAME_MAX.  To prevent this, readdir()
actually presents long filenames in an abbreviated form which encodes
a strong "hash" of the ciphertext filename, along with the optional
filesystem-specific hash(es) needed for directory lookups.  This
allows the filesystem to still, with a high degree of confidence, map
the filename given in ->lookup() back to a particular directory entry
that was previously listed by readdir().  See :c:type:`struct
fscrypt_digested_name` in the source for more details.

Note that the precise way that filenames are presented to userspace
without the key is subject to change in the future.  It is only meant
as a way to temporarily present valid filenames so that commands like
``rm -r`` work as expected on encrypted directories.
