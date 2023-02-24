.. SPDX-License-Identifier: GPL-2.0

.. _inline_encryption:

=================
Inline Encryption
=================

Background
==========

Inline encryption hardware sits logically between memory and disk, and can
en/decrypt data as it goes in/out of the disk.  For each I/O request, software
can control exactly how the inline encryption hardware will en/decrypt the data
in terms of key, algorithm, data unit size (the granularity of en/decryption),
and data unit number (a value that determines the initialization vector(s)).

Some inline encryption hardware accepts all encryption parameters including raw
keys directly in low-level I/O requests.  However, most inline encryption
hardware instead has a fixed number of "keyslots" and requires that the key,
algorithm, and data unit size first be programmed into a keyslot.  Each
low-level I/O request then just contains a keyslot index and data unit number.

Note that inline encryption hardware is very different from traditional crypto
accelerators, which are supported through the kernel crypto API.  Traditional
crypto accelerators operate on memory regions, whereas inline encryption
hardware operates on I/O requests.  Thus, inline encryption hardware needs to be
managed by the block layer, not the kernel crypto API.

Inline encryption hardware is also very different from "self-encrypting drives",
such as those based on the TCG Opal or ATA Security standards.  Self-encrypting
drives don't provide fine-grained control of encryption and provide no way to
verify the correctness of the resulting ciphertext.  Inline encryption hardware
provides fine-grained control of encryption, including the choice of key and
initialization vector for each sector, and can be tested for correctness.

Objective
=========

We want to support inline encryption in the kernel.  To make testing easier, we
also want support for falling back to the kernel crypto API when actual inline
encryption hardware is absent.  We also want inline encryption to work with
layered devices like device-mapper and loopback (i.e. we want to be able to use
the inline encryption hardware of the underlying devices if present, or else
fall back to crypto API en/decryption).

Constraints and notes
=====================

- We need a way for upper layers (e.g. filesystems) to specify an encryption
  context to use for en/decrypting a bio, and device drivers (e.g. UFSHCD) need
  to be able to use that encryption context when they process the request.
  Encryption contexts also introduce constraints on bio merging; the block layer
  needs to be aware of these constraints.

- Different inline encryption hardware has different supported algorithms,
  supported data unit sizes, maximum data unit numbers, etc.  We call these
  properties the "crypto capabilities".  We need a way for device drivers to
  advertise crypto capabilities to upper layers in a generic way.

- Inline encryption hardware usually (but not always) requires that keys be
  programmed into keyslots before being used.  Since programming keyslots may be
  slow and there may not be very many keyslots, we shouldn't just program the
  key for every I/O request, but rather keep track of which keys are in the
  keyslots and reuse an already-programmed keyslot when possible.

- Upper layers typically define a specific end-of-life for crypto keys, e.g.
  when an encrypted directory is locked or when a crypto mapping is torn down.
  At these times, keys are wiped from memory.  We must provide a way for upper
  layers to also evict keys from any keyslots they are present in.

- When possible, device-mapper devices must be able to pass through the inline
  encryption support of their underlying devices.  However, it doesn't make
  sense for device-mapper devices to have keyslots themselves.

Basic design
============

We introduce ``struct blk_crypto_key`` to represent an inline encryption key and
how it will be used.  This includes the type of the key (standard or
hardware-wrapped); the actual bytes of the key; the size of the key; the
algorithm and data unit size the key will be used with; and the number of bytes
needed to represent the maximum data unit number the key will be used with.

We introduce ``struct bio_crypt_ctx`` to represent an encryption context.  It
contains a data unit number and a pointer to a blk_crypto_key.  We add pointers
to a bio_crypt_ctx to ``struct bio`` and ``struct request``; this allows users
of the block layer (e.g. filesystems) to provide an encryption context when
creating a bio and have it be passed down the stack for processing by the block
layer and device drivers.  Note that the encryption context doesn't explicitly
say whether to encrypt or decrypt, as that is implicit from the direction of the
bio; WRITE means encrypt, and READ means decrypt.

We also introduce ``struct blk_crypto_profile`` to contain all generic inline
encryption-related state for a particular inline encryption device.  The
blk_crypto_profile serves as the way that drivers for inline encryption hardware
advertise their crypto capabilities and provide certain functions (e.g.,
functions to program and evict keys) to upper layers.  Each device driver that
wants to support inline encryption will construct a blk_crypto_profile, then
associate it with the disk's request_queue.

The blk_crypto_profile also manages the hardware's keyslots, when applicable.
This happens in the block layer, so that users of the block layer can just
specify encryption contexts and don't need to know about keyslots at all, nor do
device drivers need to care about most details of keyslot management.

Specifically, for each keyslot, the block layer (via the blk_crypto_profile)
keeps track of which blk_crypto_key that keyslot contains (if any), and how many
in-flight I/O requests are using it.  When the block layer creates a
``struct request`` for a bio that has an encryption context, it grabs a keyslot
that already contains the key if possible.  Otherwise it waits for an idle
keyslot (a keyslot that isn't in-use by any I/O), then programs the key into the
least-recently-used idle keyslot using the function the device driver provided.
In both cases, the resulting keyslot is stored in the ``crypt_keyslot`` field of
the request, where it is then accessible to device drivers and is released after
the request completes.

``struct request`` also contains a pointer to the original bio_crypt_ctx.
Requests can be built from multiple bios, and the block layer must take the
encryption context into account when trying to merge bios and requests.  For two
bios/requests to be merged, they must have compatible encryption contexts: both
unencrypted, or both encrypted with the same key and contiguous data unit
numbers.  Only the encryption context for the first bio in a request is
retained, since the remaining bios have been verified to be merge-compatible
with the first bio.

To make it possible for inline encryption to work with request_queue based
layered devices, when a request is cloned, its encryption context is cloned as
well.  When the cloned request is submitted, it is then processed as usual; this
includes getting a keyslot from the clone's target device if needed.

blk-crypto-fallback
===================

It is desirable for the inline encryption support of upper layers (e.g.
filesystems) to be testable without real inline encryption hardware, and
likewise for the block layer's keyslot management logic.  It is also desirable
to allow upper layers to just always use inline encryption rather than have to
implement encryption in multiple ways.

Therefore, we also introduce *blk-crypto-fallback*, which is an implementation
of inline encryption using the kernel crypto API.  blk-crypto-fallback is built
into the block layer, so it works on any block device without any special setup.
Essentially, when a bio with an encryption context is submitted to a
block_device that doesn't support that encryption context, the block layer will
handle en/decryption of the bio using blk-crypto-fallback.

For encryption, the data cannot be encrypted in-place, as callers usually rely
on it being unmodified.  Instead, blk-crypto-fallback allocates bounce pages,
fills a new bio with those bounce pages, encrypts the data into those bounce
pages, and submits that "bounce" bio.  When the bounce bio completes,
blk-crypto-fallback completes the original bio.  If the original bio is too
large, multiple bounce bios may be required; see the code for details.

For decryption, blk-crypto-fallback "wraps" the bio's completion callback
(``bi_complete``) and private data (``bi_private``) with its own, unsets the
bio's encryption context, then submits the bio.  If the read completes
successfully, blk-crypto-fallback restores the bio's original completion
callback and private data, then decrypts the bio's data in-place using the
kernel crypto API.  Decryption happens from a workqueue, as it may sleep.
Afterwards, blk-crypto-fallback completes the bio.

In both cases, the bios that blk-crypto-fallback submits no longer have an
encryption context.  Therefore, lower layers only see standard unencrypted I/O.

blk-crypto-fallback also defines its own blk_crypto_profile and has its own
"keyslots"; its keyslots contain ``struct crypto_skcipher`` objects.  The reason
for this is twofold.  First, it allows the keyslot management logic to be tested
without actual inline encryption hardware.  Second, similar to actual inline
encryption hardware, the crypto API doesn't accept keys directly in requests but
rather requires that keys be set ahead of time, and setting keys can be
expensive; moreover, allocating a crypto_skcipher can't happen on the I/O path
at all due to the locks it takes.  Therefore, the concept of keyslots still
makes sense for blk-crypto-fallback.

Note that regardless of whether real inline encryption hardware or
blk-crypto-fallback is used, the ciphertext written to disk (and hence the
on-disk format of data) will be the same (assuming that both the inline
encryption hardware's implementation and the kernel crypto API's implementation
of the algorithm being used adhere to spec and function correctly).

blk-crypto-fallback is optional and is controlled by the
``CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK`` kernel configuration option.

API presented to users of the block layer
=========================================

``blk_crypto_config_supported()`` allows users to check ahead of time whether
inline encryption with particular crypto settings will work on a particular
block_device -- either via hardware or via blk-crypto-fallback.  This function
takes in a ``struct blk_crypto_config`` which is like blk_crypto_key, but omits
the actual bytes of the key and instead just contains the algorithm, data unit
size, etc.  This function can be useful if blk-crypto-fallback is disabled.

``blk_crypto_init_key()`` allows users to initialize a blk_crypto_key.

Users must call ``blk_crypto_start_using_key()`` before actually starting to use
a blk_crypto_key on a block_device (even if ``blk_crypto_config_supported()``
was called earlier).  This is needed to initialize blk-crypto-fallback if it
will be needed.  This must not be called from the data path, as this may have to
allocate resources, which may deadlock in that case.

Next, to attach an encryption context to a bio, users should call
``bio_crypt_set_ctx()``.  This function allocates a bio_crypt_ctx and attaches
it to a bio, given the blk_crypto_key and the data unit number that will be used
for en/decryption.  Users don't need to worry about freeing the bio_crypt_ctx
later, as that happens automatically when the bio is freed or reset.

Finally, when done using inline encryption with a blk_crypto_key on a
block_device, users must call ``blk_crypto_evict_key()``.  This ensures that
the key is evicted from all keyslots it may be programmed into and unlinked from
any kernel data structures it may be linked into.

In summary, for users of the block layer, the lifecycle of a blk_crypto_key is
as follows:

1. ``blk_crypto_config_supported()`` (optional)
2. ``blk_crypto_init_key()``
3. ``blk_crypto_start_using_key()``
4. ``bio_crypt_set_ctx()`` (potentially many times)
5. ``blk_crypto_evict_key()`` (after all I/O has completed)
6. Zeroize the blk_crypto_key (this has no dedicated function)

If a blk_crypto_key is being used on multiple block_devices, then
``blk_crypto_config_supported()`` (if used), ``blk_crypto_start_using_key()``,
and ``blk_crypto_evict_key()`` must be called on each block_device.

API presented to device drivers
===============================

A device driver that wants to support inline encryption must set up a
blk_crypto_profile in the request_queue of its device.  To do this, it first
must call ``blk_crypto_profile_init()`` (or its resource-managed variant
``devm_blk_crypto_profile_init()``), providing the number of keyslots.

Next, it must advertise its crypto capabilities by setting fields in the
blk_crypto_profile, e.g. ``modes_supported`` and ``max_dun_bytes_supported``.

It then must set function pointers in the ``ll_ops`` field of the
blk_crypto_profile to tell upper layers how to control the inline encryption
hardware, e.g. how to program and evict keyslots.  Most drivers will need to
implement ``keyslot_program`` and ``keyslot_evict``.  For details, see the
comments for ``struct blk_crypto_ll_ops``.

Once the driver registers a blk_crypto_profile with a request_queue, I/O
requests the driver receives via that queue may have an encryption context.  All
encryption contexts will be compatible with the crypto capabilities declared in
the blk_crypto_profile, so drivers don't need to worry about handling
unsupported requests.  Also, if a nonzero number of keyslots was declared in the
blk_crypto_profile, then all I/O requests that have an encryption context will
also have a keyslot which was already programmed with the appropriate key.

If the driver implements runtime suspend and its blk_crypto_ll_ops don't work
while the device is runtime-suspended, then the driver must also set the ``dev``
field of the blk_crypto_profile to point to the ``struct device`` that will be
resumed before any of the low-level operations are called.

If there are situations where the inline encryption hardware loses the contents
of its keyslots, e.g. device resets, the driver must handle reprogramming the
keyslots.  To do this, the driver may call ``blk_crypto_reprogram_all_keys()``.

Finally, if the driver used ``blk_crypto_profile_init()`` instead of
``devm_blk_crypto_profile_init()``, then it is responsible for calling
``blk_crypto_profile_destroy()`` when the crypto profile is no longer needed.

Layered Devices
===============

Request queue based layered devices like dm-rq that wish to support inline
encryption need to create their own blk_crypto_profile for their request_queue,
and expose whatever functionality they choose. When a layered device wants to
pass a clone of that request to another request_queue, blk-crypto will
initialize and prepare the clone as necessary; see
``blk_crypto_insert_cloned_request()``.

Interaction between inline encryption and blk integrity
=======================================================

At the time of this patch, there is no real hardware that supports both these
features. However, these features do interact with each other, and it's not
completely trivial to make them both work together properly. In particular,
when a WRITE bio wants to use inline encryption on a device that supports both
features, the bio will have an encryption context specified, after which
its integrity information is calculated (using the plaintext data, since
the encryption will happen while data is being written), and the data and
integrity info is sent to the device. Obviously, the integrity info must be
verified before the data is encrypted. After the data is encrypted, the device
must not store the integrity info that it received with the plaintext data
since that might reveal information about the plaintext data. As such, it must
re-generate the integrity info from the ciphertext data and store that on disk
instead. Another issue with storing the integrity info of the plaintext data is
that it changes the on disk format depending on whether hardware inline
encryption support is present or the kernel crypto API fallback is used (since
if the fallback is used, the device will receive the integrity info of the
ciphertext, not that of the plaintext).

Because there isn't any real hardware yet, it seems prudent to assume that
hardware implementations might not implement both features together correctly,
and disallow the combination for now. Whenever a device supports integrity, the
kernel will pretend that the device does not support hardware inline encryption
(by setting the blk_crypto_profile in the request_queue of the device to NULL).
When the crypto API fallback is enabled, this means that all bios with and
encryption context will use the fallback, and IO will complete as usual.  When
the fallback is disabled, a bio with an encryption context will be failed.

.. _hardware_wrapped_keys:

Hardware-wrapped keys
=====================

Motivation and threat model
---------------------------

Linux storage encryption (dm-crypt, fscrypt, eCryptfs, etc.) traditionally
relies on the raw encryption key(s) being present in kernel memory so that the
encryption can be performed.  This traditionally isn't seen as a problem because
the key(s) won't be present during an offline attack, which is the main type of
attack that storage encryption is intended to protect from.

However, there is an increasing desire to also protect users' data from other
types of attacks (to the extent possible), including:

- Cold boot attacks, where an attacker with physical access to a system suddenly
  powers it off, then immediately dumps the system memory to extract recently
  in-use encryption keys, then uses these keys to decrypt user data on-disk.

- Online attacks where the attacker is able to read kernel memory without fully
  compromising the system, followed by an offline attack where any extracted
  keys can be used to decrypt user data on-disk.  An example of such an online
  attack would be if the attacker is able to run some code on the system that
  exploits a Meltdown-like vulnerability but is unable to escalate privileges.

- Online attacks where the attacker fully compromises the system, but their data
  exfiltration is significantly time-limited and/or bandwidth-limited, so in
  order to completely exfiltrate the data they need to extract the encryption
  keys to use in a later offline attack.

Hardware-wrapped keys are a feature of inline encryption hardware that is
designed to protect users' data from the above attacks (to the extent possible),
without introducing limitations such as a maximum number of keys.

Note that it is impossible to **fully** protect users' data from these attacks.
Even in the attacks where the attacker "just" gets read access to kernel memory,
they can still extract any user data that is present in memory, including
plaintext pagecache pages of encrypted files.  The focus here is just on
protecting the encryption keys, as those instantly give access to **all** user
data in any following offline attack, rather than just some of it (where which
data is included in that "some" might not be controlled by the attacker).

Solution overview
-----------------

Inline encryption hardware typically has "keyslots" into which software can
program keys for the hardware to use; the contents of keyslots typically can't
be read back by software.  As such, the above security goals could be achieved
if the kernel simply erased its copy of the key(s) after programming them into
keyslot(s) and thereafter only referred to them via keyslot number.

However, that naive approach runs into the problem that it limits the number of
unlocked keys to the number of keyslots, which typically is a small number.  In
cases where there is only one encryption key system-wide (e.g., a full-disk
encryption key), that can be tolerable.  However, in general there can be many
logged-in users with many different keys, and/or many running applications with
application-specific encrypted storage areas.  This is especially true if
file-based encryption (e.g. fscrypt) is being used.

Thus, it is important for the kernel to still have a way to "remind" the
hardware about a key, without actually having the raw key itself.  This would
ensure that the number of hardware keyslots only limits the number of active I/O
requests, not other things such as the number of logged-in users, the number of
running apps, or the number of encrypted storage areas that apps can create.

Somewhat less importantly, it is also desirable that the raw keys are never
visible to software at all, even while being initially unlocked.  This would
ensure that a read-only compromise of system memory will never allow a key to be
extracted to be used off-system, even if it occurs when a key is being unlocked.

To solve all these problems, some vendors of inline encryption hardware have
made their hardware support *hardware-wrapped keys*.  Hardware-wrapped keys
are encrypted keys that can only be unwrapped (decrypted) and used by hardware
-- either by the inline encryption hardware itself, or by a dedicated hardware
block that can directly provision keys to the inline encryption hardware.

(We refer to them as "hardware-wrapped keys" rather than simply "wrapped keys"
to add some clarity in cases where there could be other types of wrapped keys,
such as in file-based encryption.  Key wrapping is a commonly used technique.)

The key which wraps (encrypts) hardware-wrapped keys is a hardware-internal key
that is never exposed to software; it is either a persistent key (a "long-term
wrapping key") or a per-boot key (an "ephemeral wrapping key").  The long-term
wrapped form of the key is what is initially unlocked, but it is erased from
memory as soon as it is converted into an ephemerally-wrapped key.  In-use
hardware-wrapped keys are always ephemerally-wrapped, not long-term wrapped.

As inline encryption hardware can only be used to encrypt/decrypt data on-disk,
the hardware also includes a level of indirection; it doesn't use the unwrapped
key directly for inline encryption, but rather derives both an inline encryption
key and a "software secret" from it.  Software can use the "software secret" for
tasks that can't use the inline encryption hardware, such as filenames
encryption.  The software secret is not protected from memory compromise.

Key hierarchy
-------------

Here is the key hierarchy for a hardware-wrapped key::

                       Hardware-wrapped key
                                |
                                |
                          <Hardware KDF>
                                |
                  -----------------------------
                  |                           |
        Inline encryption key           Software secret

The components are:

- *Hardware-wrapped key*: a key for the hardware's KDF (Key Derivation
  Function), in ephemerally-wrapped form.  The key wrapping algorithm is a
  hardware implementation detail that doesn't impact kernel operation, but a
  strong authenticated encryption algorithm such as AES-256-GCM is recommended.

- *Hardware KDF*: a KDF (Key Derivation Function) which the hardware uses to
  derive subkeys after unwrapping the wrapped key.  The hardware's choice of KDF
  doesn't impact kernel operation, but it does need to be known for testing
  purposes, and it's also assumed to have at least a 256-bit security strength.
  All known hardware uses the SP800-108 KDF in Counter Mode with AES-256-CMAC,
  with a particular choice of labels and contexts; new hardware should use this
  already-vetted KDF.

- *Inline encryption key*: a derived key which the hardware directly provisions
  to a keyslot of the inline encryption hardware, without exposing it to
  software.  In all known hardware, this will always be an AES-256-XTS key.
  However, in principle other encryption algorithms could be supported too.
  Hardware must derive distinct subkeys for each supported encryption algorithm.

- *Software secret*: a derived key which the hardware returns to software so
  that software can use it for cryptographic tasks that can't use inline
  encryption.  This value is cryptographically isolated from the inline
  encryption key, i.e. knowing one doesn't reveal the other.  (The KDF ensures
  this.)  Currently, the software secret is always 32 bytes and thus is suitable
  for cryptographic applications that require up to a 256-bit security strength.
  Some use cases (e.g. full-disk encryption) won't require the software secret.

Example: in the case of fscrypt, the fscrypt master key (the key that protects a
particular set of encrypted directories) is made hardware-wrapped.  The inline
encryption key is used as the file contents encryption key, while the software
secret (rather than the master key directly) is used to key fscrypt's KDF
(HKDF-SHA512) to derive other subkeys such as filenames encryption keys.

Note that currently this design assumes a single inline encryption key per
hardware-wrapped key, without any further key derivation.  Thus, in the case of
fscrypt, currently hardware-wrapped keys are only compatible with the "inline
encryption optimized" settings, which use one file contents encryption key per
encryption policy rather than one per file.  This design could be extended to
make the hardware derive per-file keys using per-file nonces passed down the
storage stack, and in fact some hardware already supports this; future work is
planned to remove this limitation by adding the corresponding kernel support.

Kernel support
--------------

The inline encryption support of the kernel's block layer ("blk-crypto") has
been extended to support hardware-wrapped keys as an alternative to standard
keys, when hardware support is available.  This works in the following way:

- A ``key_types_supported`` field is added to the crypto capabilities in
  ``struct blk_crypto_profile``.  This allows device drivers to declare that
  they support standard keys, hardware-wrapped keys, or both.

- ``struct blk_crypto_key`` can now contain a hardware-wrapped key as an
  alternative to a standard key; a ``key_type`` field is added to
  ``struct blk_crypto_config`` to distinguish between the different key types.
  This allows users of blk-crypto to en/decrypt data using a hardware-wrapped
  key in a way very similar to using a standard key.

- A new method ``blk_crypto_ll_ops::derive_sw_secret`` is added.  Device drivers
  that support hardware-wrapped keys must implement this method.  Users of
  blk-crypto can call ``blk_crypto_derive_sw_secret()`` to access this method.

- The programming and eviction of hardware-wrapped keys happens via
  ``blk_crypto_ll_ops::keyslot_program`` and
  ``blk_crypto_ll_ops::keyslot_evict``, just like it does for standard keys.  If
  a driver supports hardware-wrapped keys, then it must handle hardware-wrapped
  keys being passed to these methods.

blk-crypto-fallback doesn't support hardware-wrapped keys.  Therefore,
hardware-wrapped keys can only be used with actual inline encryption hardware.

Currently, the kernel only works with hardware-wrapped keys in
ephemerally-wrapped form.  No generic kernel interfaces are provided for
generating or importing hardware-wrapped keys in the first place, or converting
them to ephemerally-wrapped form.  In Android, SoC vendors are required to
support these operations in their KeyMint implementation (a hardware abstraction
layer in userspace); for details, see the `Android documentation
<https://source.android.com/security/encryption/hw-wrapped-keys>`_.

Testability
-----------

Both the hardware KDF and the inline encryption itself are well-defined
algorithms that don't depend on any secrets other than the unwrapped key.
Therefore, if the unwrapped key is known to software, these algorithms can be
reproduced in software in order to verify the ciphertext that is written to disk
by the inline encryption hardware.

However, the unwrapped key will only be known to software for testing if the
"import" functionality is used.  Proper testing is not possible in the
"generate" case where the hardware generates the key itself.  The correct
operation of the "generate" mode thus relies on the security and correctness of
the hardware RNG and its use to generate the key, as well as the testing of the
"import" mode as that should cover all parts other than the key generation.

For an example of a test that verifies the ciphertext written to disk in the
"import" mode, see the fscrypt hardware-wrapped key tests in xfstests, or
`Android's vts_kernel_encryption_test
<https://android.googlesource.com/platform/test/vts-testcase/kernel/+/refs/heads/master/encryption/>`_.
