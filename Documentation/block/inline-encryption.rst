.. SPDX-License-Identifier: GPL-2.0

=================
Inline Encryption
=================

Objective
=========

We want to support inline encryption (IE) in the kernel.
To allow for testing, we also want a crypto API fallback when actual
IE hardware is absent. We also want IE to work with layered devices
like dm and loopback (i.e. we want to be able to use the IE hardware
of the underlying devices if present, or else fall back to crypto API
en/decryption).


Constraints and notes
=====================

- IE hardware have a limited number of "keyslots" that can be programmed
  with an encryption context (key, algorithm, data unit size, etc.) at any time.
  One can specify a keyslot in a data request made to the device, and the
  device will en/decrypt the data using the encryption context programmed into
  that specified keyslot. When possible, we want to make multiple requests with
  the same encryption context share the same keyslot.

- We need a way for filesystems to specify an encryption context to use for
  en/decrypting a struct bio, and a device driver (like UFS) needs to be able
  to use that encryption context when it processes the bio.

- We need a way for device drivers to expose their capabilities in a unified
  way to the upper layers.


Design
======

We add a struct bio_crypt_ctx to struct bio that can represent an
encryption context, because we need to be able to pass this encryption
context from the FS layer to the device driver to act upon.

While IE hardware works on the notion of keyslots, the FS layer has no
knowledge of keyslots - it simply wants to specify an encryption context to
use while en/decrypting a bio.

We introduce a keyslot manager (KSM) that handles the translation from
encryption contexts specified by the FS to keyslots on the IE hardware.
This KSM also serves as the way IE hardware can expose their capabilities to
upper layers. The generic mode of operation is: each device driver that wants
to support IE will construct a KSM and set it up in its struct request_queue.
Upper layers that want to use IE on this device can then use this KSM in
the device's struct request_queue to translate an encryption context into
a keyslot. The presence of the KSM in the request queue shall be used to mean
that the device supports IE.

On the device driver end of the interface, the device driver needs to tell the
KSM how to actually manipulate the IE hardware in the device to do things like
programming the crypto key into the IE hardware into a particular keyslot. All
this is achieved through the :c:type:`struct keyslot_mgmt_ll_ops` that the
device driver passes to the KSM when creating it.

It uses refcounts to track which keyslots are idle (either they have no
encryption context programmed, or there are no in-flight struct bios
referencing that keyslot). When a new encryption context needs a keyslot, it
tries to find a keyslot that has already been programmed with the same
encryption context, and if there is no such keyslot, it evicts the least
recently used idle keyslot and programs the new encryption context into that
one. If no idle keyslots are available, then the caller will sleep until there
is at least one.


Blk-crypto
==========

The above is sufficient for simple cases, but does not work if there is a
need for a crypto API fallback, or if we are want to use IE with layered
devices. To these ends, we introduce blk-crypto. Blk-crypto allows us to
present a unified view of encryption to the FS (so FS only needs to specify
an encryption context and not worry about keyslots at all), and blk-crypto
can decide whether to delegate the en/decryption to IE hardware or to the
crypto API. Blk-crypto maintains an internal KSM that serves as the crypto
API fallback.

Blk-crypto needs to ensure that the encryption context is programmed into the
"correct" keyslot manager for IE. If a bio is submitted to a layered device
that eventually passes the bio down to a device that really does support IE, we
want the encryption context to be programmed into a keyslot for the KSM of the
device with IE support. However, blk-crypto does not know a priori whether a
particular device is the final device in the layering structure for a bio or
not. So in the case that a particular device does not support IE, since it is
possibly the final destination device for the bio, if the bio requires
encryption (i.e. the bio is doing a write operation), blk-crypto must fallback
to the crypto API *before* sending the bio to the device.

Blk-crypto ensures that:

- The bio's encryption context is programmed into a keyslot in the KSM of the
  request queue that the bio is being submitted to (or the crypto API fallback
  KSM if the request queue doesn't have a KSM), and that the ``processing_ksm``
  in the ``bi_crypt_context`` is set to this KSM

- That the bio has its own individual reference to the keyslot in this KSM.
  Once the bio passes through blk-crypto, its encryption context is programmed
  in some KSM. The "its own individual reference to the keyslot" ensures that
  keyslots can be released by each bio independently of other bios while
  ensuring that the bio has a valid reference to the keyslot when, for e.g., the
  crypto API fallback KSM in blk-crypto performs crypto on the device's behalf.
  The individual references are ensured by increasing the refcount for the
  keyslot in the ``processing_ksm`` when a bio with a programmed encryption
  context is cloned.


What blk-crypto does on bio submission
--------------------------------------

**Case 1:** blk-crypto is given a bio with only an encryption context that hasn't
been programmed into any keyslot in any KSM (for e.g. a bio from the FS).
  In this case, blk-crypto will program the encryption context into the KSM of the
  request queue the bio is being submitted to (and if this KSM does not exist,
  then it will program it into blk-crypto's internal KSM for crypto API
  fallback). The KSM that this encryption context was programmed into is stored
  as the ``processing_ksm`` in the bio's ``bi_crypt_context``.

**Case 2:** blk-crypto is given a bio whose encryption context has already been
programmed into a keyslot in the *crypto API fallback* KSM.
  In this case, blk-crypto does nothing; it treats the bio as not having
  specified an encryption context. Note that we cannot do here what we will do
  in Case 3 because we would have already encrypted the bio via the crypto API
  by this point.

**Case 3:** blk-crypto is given a bio whose encryption context has already been
programmed into a keyslot in some KSM (that is *not* the crypto API fallback
KSM).
  In this case, blk-crypto first releases that keyslot from that KSM and then
  treats the bio as in Case 1.

This way, when a device driver is processing a bio, it can be sure that
the bio's encryption context has been programmed into some KSM (either the
device driver's request queue's KSM, or blk-crypto's crypto API fallback KSM).
It then simply needs to check if the bio's processing_ksm is the device's
request queue's KSM. If so, then it should proceed with IE. If not, it should
simply do nothing with respect to crypto, because some other KSM (perhaps the
blk-crypto crypto API fallback KSM) is handling the en/decryption.

Blk-crypto will release the keyslot that is being held by the bio (and also
decrypt it if the bio is using the crypto API fallback KSM) once
``bio_remaining_done`` returns true for the bio.


Layered Devices
===============

Layered devices that wish to support IE need to create their own keyslot
manager for their request queue, and expose whatever functionality they choose.
When a layered device wants to pass a bio to another layer (either by
resubmitting the same bio, or by submitting a clone), it doesn't need to do
anything special because the bio (or the clone) will once again pass through
blk-crypto, which will work as described in Case 3. If a layered device wants
for some reason to do the IO by itself instead of passing it on to a child
device, but it also chose to expose IE capabilities by setting up a KSM in its
request queue, it is then responsible for en/decrypting the data itself. In
such cases, the device can choose to call the blk-crypto function
``blk_crypto_fallback_to_kernel_crypto_api`` (TODO: Not yet implemented), which will
cause the en/decryption to be done via the crypto API fallback.


Future Optimizations for layered devices
========================================

Creating a keyslot manager for the layered device uses up memory for each
keyslot, and in general, a layered device (like dm-linear) merely passes the
request on to a "child" device, so the keyslots in the layered device itself
might be completely unused. We can instead define a new type of KSM; the
"passthrough KSM", that layered devices can use to let blk-crypto know that
this layered device *will* pass the bio to some child device (and hence
through blk-crypto again, at which point blk-crypto can program the encryption
context, instead of programming it into the layered device's KSM). Again, if
the device "lies" and decides to do the IO itself instead of passing it on to
a child device, it is responsible for doing the en/decryption (and can choose
to call ``blk_crypto_fallback_to_kernel_crypto_api``). Another use case for the
"passthrough KSM" is for IE devices that want to manage their own keyslots/do
not have a limited number of keyslots.
