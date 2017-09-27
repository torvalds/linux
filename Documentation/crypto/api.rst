Programming Interface
=====================

Please note that the kernel crypto API contains the AEAD givcrypt API
(crypto_aead_giv\* and aead_givcrypt\* function calls in
include/crypto/aead.h). This API is obsolete and will be removed in the
future. To obtain the functionality of an AEAD cipher with internal IV
generation, use the IV generator as a regular cipher. For example,
rfc4106(gcm(aes)) is the AEAD cipher with external IV generation and
seqniv(rfc4106(gcm(aes))) implies that the kernel crypto API generates
the IV. Different IV generators are available.

.. class:: toc-title

	   Table of contents

.. toctree::
   :maxdepth: 2

   api-skcipher
   api-aead
   api-digest
   api-rng
   api-akcipher
   api-kpp
