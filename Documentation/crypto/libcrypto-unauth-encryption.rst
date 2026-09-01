.. SPDX-License-Identifier: GPL-2.0-or-later

Unauthenticated encryption
==========================

These APIs provide support for unauthenticated encryption and decryption,
including bare stream ciphers and other length-preserving algorithms such as
block ciphers in XTS mode.  The legitimate use cases for these algorithms are:

- Support for legacy protocols that really should have chosen an authenticated
  mode (or even another primitive entirely) but didn't.

- Internal components of authenticated modes.  For example, AES-CTR is used by
  AES-GCM and AES-CCM internally.

- Storage encryption that cannot accommodate ciphertext expansion.  Usually
  AES-XTS is used for this.

- Stream ciphers for key derivation and random number generation.

Besides the above, these shouldn't be used.

AES-CBC and AES-CBC-CTS
-----------------------

This API provides support for AES in the CBC and CBC-CTS modes of operation.

.. kernel-doc:: include/crypto/aes-cbc.h

AES-CTR and AES-XCTR
--------------------

This API provides support for AES in the CTR and XCTR modes of operation.

.. kernel-doc:: include/crypto/aes-ctr.h

AES-ECB
-------

This API provides support for AES in the ECB mode of operation.

.. kernel-doc:: include/crypto/aes-ecb.h

AES-XTS
-------

This API provides support for AES in the XTS mode of operation.

.. kernel-doc:: include/crypto/aes-xts.h
