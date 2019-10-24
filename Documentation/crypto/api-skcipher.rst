Block Cipher Algorithm Definitions
----------------------------------

.. kernel-doc:: include/linux/crypto.h
   :doc: Block Cipher Algorithm Definitions

.. kernel-doc:: include/linux/crypto.h
   :functions: crypto_alg ablkcipher_alg blkcipher_alg cipher_alg compress_alg

Symmetric Key Cipher API
------------------------

.. kernel-doc:: include/crypto/skcipher.h
   :doc: Symmetric Key Cipher API

.. kernel-doc:: include/crypto/skcipher.h
   :functions: crypto_alloc_skcipher crypto_free_skcipher crypto_has_skcipher crypto_skcipher_ivsize crypto_skcipher_blocksize crypto_skcipher_setkey crypto_skcipher_reqtfm crypto_skcipher_encrypt crypto_skcipher_decrypt

Symmetric Key Cipher Request Handle
-----------------------------------

.. kernel-doc:: include/crypto/skcipher.h
   :doc: Symmetric Key Cipher Request Handle

.. kernel-doc:: include/crypto/skcipher.h
   :functions: crypto_skcipher_reqsize skcipher_request_set_tfm skcipher_request_alloc skcipher_request_free skcipher_request_set_callback skcipher_request_set_crypt

Single Block Cipher API
-----------------------

.. kernel-doc:: include/linux/crypto.h
   :doc: Single Block Cipher API

.. kernel-doc:: include/linux/crypto.h
   :functions: crypto_alloc_cipher crypto_free_cipher crypto_has_cipher crypto_cipher_blocksize crypto_cipher_setkey crypto_cipher_encrypt_one crypto_cipher_decrypt_one

Asynchronous Block Cipher API - Deprecated
------------------------------------------

.. kernel-doc:: include/linux/crypto.h
   :doc: Asynchronous Block Cipher API

.. kernel-doc:: include/linux/crypto.h
   :functions: crypto_free_ablkcipher crypto_has_ablkcipher crypto_ablkcipher_ivsize crypto_ablkcipher_blocksize crypto_ablkcipher_setkey crypto_ablkcipher_reqtfm crypto_ablkcipher_encrypt crypto_ablkcipher_decrypt

Asynchronous Cipher Request Handle - Deprecated
-----------------------------------------------

.. kernel-doc:: include/linux/crypto.h
   :doc: Asynchronous Cipher Request Handle

.. kernel-doc:: include/linux/crypto.h
   :functions: crypto_ablkcipher_reqsize ablkcipher_request_set_tfm ablkcipher_request_alloc ablkcipher_request_free ablkcipher_request_set_callback ablkcipher_request_set_crypt

Synchronous Block Cipher API - Deprecated
-----------------------------------------

.. kernel-doc:: include/linux/crypto.h
   :doc: Synchronous Block Cipher API

.. kernel-doc:: include/linux/crypto.h
   :functions: crypto_alloc_blkcipher crypto_free_blkcipher crypto_has_blkcipher crypto_blkcipher_name crypto_blkcipher_ivsize crypto_blkcipher_blocksize crypto_blkcipher_setkey crypto_blkcipher_encrypt crypto_blkcipher_encrypt_iv crypto_blkcipher_decrypt crypto_blkcipher_decrypt_iv crypto_blkcipher_set_iv crypto_blkcipher_get_iv
