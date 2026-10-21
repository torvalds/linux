.. SPDX-License-Identifier: GPL-2.0-or-later

Crypto Key Zeroization
======================

This document describes the conventions for zeroizing crypto structures in the
kernel.

Note: the kernel follows traditional cryptographic terminology by using
the term "zeroizing" to mean erasing sensitive parameters to prevent
their disclosure if the system is later compromised.  This distinguishes
it from zeroing memory for other purposes such as initialization.

.. contents::

Overview
--------

Cryptographic key material and intermediate state (such as HMAC contexts) must
be zeroized after use to prevent sensitive data from lingering on the stack or
heap, where it could be leaked through memory disclosure vulnerabilities,
crash dumps, or cold-boot attacks.

For memory that has been allocated with kmalloc() or a similar function,
kfree_sensitive() should be used instead of kfree() to release the memory.

For other cases, the kernel provides memzero_explicit() for clearing the
memory.  Unlike plain memset(), memzero_explicit() is guaranteed not
to be optimized away by the compiler, even when the memory being cleared
appears to be dead.

The crypto library builds on memzero_explicit() by providing typed
zeroization helpers for each key and context structure.  These helpers serve
two purposes:

1. They make __cleanup() annotations possible, so that structures on
   the stack are automatically zeroized when they go out of scope.

2. They improve readability by replacing ``memzero_explicit(&key, sizeof(key))``
   with a self-documenting call like ``aes_zeroize_key(&key)``.


What to zeroize
---------------

The following types of structures hold sensitive material and should be
zeroized after use:

- **Key structures** (e.g. ``struct aes_key``, ``struct hmac_sha256_key``):
  contain expanded round keys or prepared key material.

- **HMAC/MAC context structures** (e.g. ``struct hmac_sha256_ctx``,
  ``struct aes_cmac_ctx``): contain inner and outer hash states derived from
  the key.

- **Hash context structures** (e.g. ``struct sha256_ctx``): may contain
  sensitive data being hashed.

Not all of these require explicit cleanup by callers.  Many ``..._final()``
functions already zeroize their context internally (see `Automatic vs. manual
zeroization`_ below).


Zeroization helpers
-------------------

Each crypto structure that callers may need to zeroize should have a
corresponding inline helper function.  The naming convention is::

    <algorithm>_zeroize_<type>(struct <algorithm>_<type> *p);

For example::

    void aes_zeroize_key(struct aes_key *key);
    void aes_zeroize_enckey(struct aes_enckey *key);
    void hmac_sha256_zeroize_ctx(struct hmac_sha256_ctx *ctx);
    void aes_cmac_zeroize_key(struct aes_cmac_key *key);
    void aes_cmac_zeroize_ctx(struct aes_cmac_ctx *ctx);

Each helper is a ``static inline`` function in the algorithm's header that
wraps ``memzero_explicit()``, for example::

    static inline void hmac_sha256_zeroize_ctx(struct hmac_sha256_ctx *ctx)
    {
            memzero_explicit(ctx, sizeof(*ctx));
    }


Using __cleanup for automatic zeroization
-----------------------------------------

The preferred way to zeroize stack-allocated key and context structures is
with the __cleanup() attribute.  This ensures zeroization happens on all
exit paths, including error returns and early exits. For example::

    static int my_aesxts_setkey(..., const u8 *key, unsigned int len)
    {
        struct crypto_aes_ctx aes __cleanup(aes_zeroize_ctx);
        ...

        /* Only half of the key data is cipher key */
        keylen = (len >> 1);
        ret = aes_expandkey(&aes, key, keylen);
        if (ret)
                return ret;

        ... do something with the cipher key ...

        /* The other half is the tweak key */
        ret = aes_expandkey(&aes, (u8 *)(key + keylen), keylen);
        if (ret)
                return ret;  /* <-- Could leak cipher key without __cleanup */

        ... do something with the tweak key ...

        /* No need for memzero_explicit() at the end thanks to the __cleanup */
        return 0;
    }

Note that __cleanup() attributes should not be used in functions that use
"goto" statements. The benefit of cleanup helpers is the removal of "gotos",
and that "goto" statements can jump between scopes, so the expectation is
that usage of "goto" and cleanup helpers is never mixed in the same function.


Automatic vs. manual zeroization
--------------------------------

Many ``..._final()`` functions in the crypto library automatically zeroize
their context before returning.  When this is the case, the kernel-doc for the
function documents it::

    After finishing, this zeroizes @ctx.  So the caller does not need to do it.

In these cases, callers on simple code paths (where ``..._final()`` is always
reached) do not need to add __cleanup() or explicit zeroization.
However, __cleanup() is still recommended whenever there are error paths
that bypass ``..._final()``, as it ensures zeroization on all paths.

For algorithms where ``..._final()`` does *not* zeroize the context (such as
the SHAKE XOFs, where ``shake_squeeze()`` can be called multiple times),
callers must explicitly zeroize the context by calling the appropriate helper
or using __cleanup(), for example::

    struct shake_ctx ctx __cleanup(shake_zeroize_ctx);

    shake256_init(&ctx);
    shake_update(&ctx, data, data_len);
    shake_squeeze(&ctx, out, out_len);
    /* ctx is automatically zeroized at end of scope */
