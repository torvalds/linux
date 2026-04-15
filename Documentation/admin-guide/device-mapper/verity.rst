=========
dm-verity
=========

Device-Mapper's "verity" target provides transparent integrity checking of
block devices using a cryptographic digest provided by the kernel crypto API.
This target is read-only.

Construction Parameters
=======================

::

    <version> <dev> <hash_dev>
    <data_block_size> <hash_block_size>
    <num_data_blocks> <hash_start_block>
    <algorithm> <digest> <salt>
    [<#opt_params> <opt_params>]

<version>
    This is the type of the on-disk hash format.

    0 is the original format used in the Chromium OS.
      The salt is appended when hashing, digests are stored continuously and
      the rest of the block is padded with zeroes.

    1 is the current format that should be used for new devices.
      The salt is prepended when hashing and each digest is
      padded with zeroes to the power of two.

<dev>
    This is the device containing data, the integrity of which needs to be
    checked.  It may be specified as a path, like /dev/sdaX, or a device number,
    <major>:<minor>.

<hash_dev>
    This is the device that supplies the hash tree data.  It may be
    specified similarly to the device path and may be the same device.  If the
    same device is used, the hash_start should be outside the configured
    dm-verity device.

<data_block_size>
    The block size on a data device in bytes.
    Each block corresponds to one digest on the hash device.

<hash_block_size>
    The size of a hash block in bytes.

<num_data_blocks>
    The number of data blocks on the data device.  Additional blocks are
    inaccessible.  You can place hashes to the same partition as data, in this
    case hashes are placed after <num_data_blocks>.

<hash_start_block>
    This is the offset, in <hash_block_size>-blocks, from the start of hash_dev
    to the root block of the hash tree.

<algorithm>
    The cryptographic hash algorithm used for this device.  This should
    be the name of the algorithm, like "sha1".

<digest>
    The hexadecimal encoding of the cryptographic hash of the root hash block
    and the salt.  This hash should be trusted as there is no other authenticity
    beyond this point.

<salt>
    The hexadecimal encoding of the salt value.

<#opt_params>
    Number of optional parameters. If there are no optional parameters,
    the optional parameters section can be skipped or #opt_params can be zero.
    Otherwise #opt_params is the number of following arguments.

    Example of optional parameters section:
        1 ignore_corruption

ignore_corruption
    Log corrupted blocks, but allow read operations to proceed normally.

restart_on_corruption
    Restart the system when a corrupted block is discovered. This option is
    not compatible with ignore_corruption and requires user space support to
    avoid restart loops.

panic_on_corruption
    Panic the device when a corrupted block is discovered. This option is
    not compatible with ignore_corruption and restart_on_corruption.

restart_on_error
    Restart the system when an I/O error is detected.
    This option can be combined with the restart_on_corruption option.

panic_on_error
    Panic the device when an I/O error is detected. This option is
    not compatible with the restart_on_error option but can be combined
    with the panic_on_corruption option.

ignore_zero_blocks
    Do not verify blocks that are expected to contain zeroes and always return
    zeroes instead. This may be useful if the partition contains unused blocks
    that are not guaranteed to contain zeroes.

use_fec_from_device <fec_dev>
    Use forward error correction (FEC) parity data from the specified device to
    try to automatically recover from corruption and I/O errors.

    If this option is given, then <fec_roots> and <fec_blocks> must also be
    given.  <hash_block_size> must also be equal to <data_block_size>.

    <fec_dev> can be the same as <dev>, in which case <fec_start> must be
    outside the data area.  It can also be the same as <hash_dev>, in which case
    <fec_start> must be outside the hash and optional additional metadata areas.

    If the data <dev> is encrypted, the <fec_dev> should be too.

    For more information, see `Forward error correction`_.

fec_roots <num>
    The number of parity bytes in each 255-byte Reed-Solomon codeword.  The
    Reed-Solomon code used will be an RS(255, k) code where k = 255 - fec_roots.

    The supported values are 2 through 24 inclusive.  Higher values provide
    stronger error correction.  However, the minimum value of 2 already provides
    strong error correction due to the use of interleaving, so 2 is the
    recommended value for most users.  fec_roots=2 corresponds to an
    RS(255, 253) code, which has a space overhead of about 0.8%.

fec_blocks <num>
    The total number of <data_block_size> blocks that are error-checked using
    FEC.  This must be at least the sum of <num_data_blocks> and the number of
    blocks needed by the hash tree.  It can include additional metadata blocks,
    which are assumed to be accessible on <hash_dev> following the hash blocks.

    Note that this is *not* the number of parity blocks.  The number of parity
    blocks is inferred from <fec_blocks>, <fec_roots>, and <data_block_size>.

fec_start <offset>
    This is the offset, in <data_block_size> blocks, from the start of <fec_dev>
    to the beginning of the parity data.

check_at_most_once
    Verify data blocks only the first time they are read from the data device,
    rather than every time.  This reduces the overhead of dm-verity so that it
    can be used on systems that are memory and/or CPU constrained.  However, it
    provides a reduced level of security because only offline tampering of the
    data device's content will be detected, not online tampering.

    Hash blocks are still verified each time they are read from the hash device,
    since verification of hash blocks is less performance critical than data
    blocks, and a hash block will not be verified any more after all the data
    blocks it covers have been verified anyway.

root_hash_sig_key_desc <key_description>
    This is the description of the USER_KEY that the kernel will lookup to get
    the pkcs7 signature of the roothash. The pkcs7 signature is used to validate
    the root hash during the creation of the device mapper block device.
    Verification of roothash depends on the config DM_VERITY_VERIFY_ROOTHASH_SIG
    being set in the kernel.  The signatures are checked against the builtin
    trusted keyring by default, or the secondary trusted keyring if
    DM_VERITY_VERIFY_ROOTHASH_SIG_SECONDARY_KEYRING is set.  The secondary
    trusted keyring includes by default the builtin trusted keyring, and it can
    also gain new certificates at run time if they are signed by a certificate
    already in the secondary trusted keyring.

try_verify_in_tasklet
    If verity hashes are in cache and the IO size does not exceed the limit,
    verify data blocks in bottom half instead of workqueue. This option can
    reduce IO latency. The size limits can be configured via
    /sys/module/dm_verity/parameters/use_bh_bytes. The four parameters
    correspond to limits for IOPRIO_CLASS_NONE, IOPRIO_CLASS_RT,
    IOPRIO_CLASS_BE and IOPRIO_CLASS_IDLE in turn.
    For example:
    <none>,<rt>,<be>,<idle>
    4096,4096,4096,4096

Theory of operation
===================

dm-verity is meant to be set up as part of a verified boot path.  This
may be anything ranging from a boot using tboot or trustedgrub to just
booting from a known-good device (like a USB drive or CD).

When a dm-verity device is configured, it is expected that the caller
has been authenticated in some way (cryptographic signatures, etc).
After instantiation, all hashes will be verified on-demand during
disk access.  If they cannot be verified up to the root node of the
tree, the root hash, then the I/O will fail.  This should detect
tampering with any data on the device and the hash data.

Cryptographic hashes are used to assert the integrity of the device on a
per-block basis. This allows for a lightweight hash computation on first read
into the page cache. Block hashes are stored linearly, aligned to the nearest
block size.

Hash Tree
---------

Each node in the tree is a cryptographic hash.  If it is a leaf node, the hash
of some data block on disk is calculated. If it is an intermediary node,
the hash of a number of child nodes is calculated.

Each entry in the tree is a collection of neighboring nodes that fit in one
block.  The number is determined based on block_size and the size of the
selected cryptographic digest algorithm.  The hashes are linearly-ordered in
this entry and any unaligned trailing space is ignored but included when
calculating the parent node.

The tree looks something like:

	alg = sha256, num_blocks = 32768, block_size = 4096

::

                                 [   root    ]
                                /    . . .    \
                     [entry_0]                 [entry_1]
                    /  . . .  \                 . . .   \
         [entry_0_0]   . . .  [entry_0_127]    . . . .  [entry_1_127]
           / ... \             /   . . .  \             /           \
     blk_0 ... blk_127  blk_16256   blk_16383      blk_32640 . . . blk_32767

Forward error correction
------------------------

dm-verity's optional forward error correction (FEC) support adds strong error
correction capabilities to dm-verity.  It allows systems that would be rendered
inoperable by errors to continue operating, albeit with reduced performance.

FEC uses Reed-Solomon (RS) codes that are interleaved across the entire
device(s), allowing long bursts of corrupt or unreadable blocks to be recovered.

dm-verity validates any FEC-corrected block against the wanted hash before using
it.  Therefore, FEC doesn't affect the security properties of dm-verity.

The integration of FEC with dm-verity provides significant benefits over a
separate error correction layer:

- dm-verity invokes FEC only when a block's hash doesn't match the wanted hash
  or the block cannot be read at all.  As a result, FEC doesn't add overhead to
  the common case where no error occurs.

- dm-verity hashes are also used to identify erasure locations for RS decoding.
  This allows correcting twice as many errors.

FEC uses an RS(255, k) code where k = 255 - fec_roots.  fec_roots is usually 2.
This means that each k (usually 253) message bytes have fec_roots (usually 2)
bytes of parity data added to get a 255-byte codeword.  (Many external sources
call RS codewords "blocks".  Since dm-verity already uses the term "block" to
mean something else, we'll use the clearer term "RS codeword".)

FEC checks fec_blocks blocks of message data in total, consisting of:

1. The data blocks from the data device
2. The hash blocks from the hash device
3. Optional additional metadata that follows the hash blocks on the hash device

dm-verity assumes that the FEC parity data was computed as if the following
procedure were followed:

1. Concatenate the message data from the above sources.
2. Zero-pad to the next multiple of k blocks.  Let msg be the resulting byte
   array, and msglen its length in bytes.
3. For 0 <= i < msglen / k (for each RS codeword):
     a. Select msg[i + j * msglen / k] for 0 <= j < k.
        Consider these to be the 'k' message bytes of an RS codeword.
     b. Compute the corresponding 'fec_roots' parity bytes of the RS codeword,
        and concatenate them to the FEC parity data.

Step 3a interleaves the RS codewords across the entire device using an
interleaving degree of data_block_size * ceil(fec_blocks / k).  This is the
maximal interleaving, such that the message data consists of a region containing
byte 0 of all the RS codewords, then a region containing byte 1 of all the RS
codewords, and so on up to the region for byte 'k - 1'.  Note that the number of
codewords is set to a multiple of data_block_size; thus, the regions are
block-aligned, and there is an implicit zero padding of up to 'k - 1' blocks.

This interleaving allows long bursts of errors to be corrected.  It provides
much stronger error correction than storage devices typically provide, while
keeping the space overhead low.

The cost is slow decoding: correcting a single block usually requires reading
254 extra blocks spread evenly across the device(s).  However, that is
acceptable because dm-verity uses FEC only when there is actually an error.

The list below contains additional details about the RS codes used by
dm-verity's FEC.  Userspace programs that generate the parity data need to use
these parameters for the parity data to match exactly:

- Field used is GF(256)
- Bytes are mapped to/from GF(256) elements in the natural way, where bits 0
  through 7 (low-order to high-order) map to the coefficients of x^0 through x^7
- Field generator polynomial is x^8 + x^4 + x^3 + x^2 + 1
- The codes used are systematic, BCH-view codes
- Primitive element alpha is 'x'
- First consecutive root of code generator polynomial is 'x^0'

On-disk format
==============

The verity kernel code does not read the verity metadata on-disk header.
It only reads the hash blocks which directly follow the header.
It is expected that a user-space tool will verify the integrity of the
verity header.

Alternatively, the header can be omitted and the dmsetup parameters can
be passed via the kernel command-line in a rooted chain of trust where
the command-line is verified.

Directly following the header (and with sector number padded to the next hash
block boundary) are the hash blocks which are stored a depth at a time
(starting from the root), sorted in order of increasing index.

The full specification of kernel parameters and on-disk metadata format
is available at the cryptsetup project's wiki page

  https://gitlab.com/cryptsetup/cryptsetup/wikis/DMVerity

Status
======
1. V (for Valid) is returned if every check performed so far was valid.
   If any check failed, C (for Corruption) is returned.
2. Number of corrected blocks by Forward Error Correction.
   '-' if Forward Error Correction is not enabled.

Example
=======
Set up a device::

  # dmsetup create vroot --readonly --table \
    "0 2097152 verity 1 /dev/sda1 /dev/sda2 4096 4096 262144 1 sha256 "\
    "4392712ba01368efdf14b05c76f9e4df0d53664630b5d48632ed17a137f39076 "\
    "1234000000000000000000000000000000000000000000000000000000000000"

A command line tool veritysetup is available to compute or verify
the hash tree or activate the kernel device. This is available from
the cryptsetup upstream repository https://gitlab.com/cryptsetup/cryptsetup/
(as a libcryptsetup extension).

Create hash on the device::

  # veritysetup format /dev/sda1 /dev/sda2
  ...
  Root hash: 4392712ba01368efdf14b05c76f9e4df0d53664630b5d48632ed17a137f39076

Activate the device::

  # veritysetup create vroot /dev/sda1 /dev/sda2 \
    4392712ba01368efdf14b05c76f9e4df0d53664630b5d48632ed17a137f39076
