.. SPDX-License-Identifier: GPL-2.0-or-later

Hash functions, MACs, and XOFs
==============================

AES-CMAC and AES-XCBC-MAC
-------------------------

This API provides support for the AES-CMAC and AES-XCBC-MAC message
authentication codes.

.. kernel-doc:: include/crypto/aes-cbc-macs.h

BLAKE2b
-------

This API provides support for the BLAKE2b cryptographic hash function.

.. kernel-doc:: include/crypto/blake2b.h

BLAKE2s
-------

This API provides support for the BLAKE2s cryptographic hash function.

.. kernel-doc:: include/crypto/blake2s.h

GHASH and POLYVAL
-----------------

This API provides support for the GHASH and POLYVAL universal hash functions.
These algorithms are used only as internal components of other algorithms.

.. kernel-doc:: include/crypto/gf128hash.h

MD5
---

This API provides support for the MD5 cryptographic hash function and HMAC-MD5.
This algorithm is obsolete and is supported only for backwards compatibility.

.. kernel-doc:: include/crypto/md5.h

NH
--

This API provides support for the NH universal hash function.  This algorithm is
used only as an internal component of other algorithms.

.. kernel-doc:: include/crypto/nh.h

Poly1305
--------

This API provides support for the Poly1305 universal hash function.  This
algorithm is used only as an internal component of other algorithms.

.. kernel-doc:: include/crypto/poly1305.h

SHA-1
-----

This API provides support for the SHA-1 cryptographic hash function and
HMAC-SHA1.  This algorithm is obsolete and is supported only for backwards
compatibility.

.. kernel-doc:: include/crypto/sha1.h

SHA-2
-----

This API provides support for the SHA-2 family of cryptographic hash functions,
including SHA-224, SHA-256, SHA-384, and SHA-512.  This also includes their
corresponding HMACs: HMAC-SHA224, HMAC-SHA256, HMAC-SHA384, and HMAC-SHA512.

.. kernel-doc:: include/crypto/sha2.h

SHA-3
-----

The SHA-3 API is documented in :ref:`sha3`.

SM3
---

This API provides support for the SM3 cryptographic hash function.

.. kernel-doc:: include/crypto/sm3.h
