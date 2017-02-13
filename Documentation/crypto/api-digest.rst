Message Digest Algorithm Definitions
------------------------------------

.. kernel-doc:: include/crypto/hash.h
   :doc: Message Digest Algorithm Definitions

.. kernel-doc:: include/crypto/hash.h
   :functions: hash_alg_common ahash_alg shash_alg

Asynchronous Message Digest API
-------------------------------

.. kernel-doc:: include/crypto/hash.h
   :doc: Asynchronous Message Digest API

.. kernel-doc:: include/crypto/hash.h
   :functions: crypto_alloc_ahash crypto_free_ahash crypto_ahash_init crypto_ahash_digestsize crypto_ahash_reqtfm crypto_ahash_reqsize crypto_ahash_setkey crypto_ahash_finup crypto_ahash_final crypto_ahash_digest crypto_ahash_export crypto_ahash_import

Asynchronous Hash Request Handle
--------------------------------

.. kernel-doc:: include/crypto/hash.h
   :doc: Asynchronous Hash Request Handle

.. kernel-doc:: include/crypto/hash.h
   :functions: ahash_request_set_tfm ahash_request_alloc ahash_request_free ahash_request_set_callback ahash_request_set_crypt

Synchronous Message Digest API
------------------------------

.. kernel-doc:: include/crypto/hash.h
   :doc: Synchronous Message Digest API

.. kernel-doc:: include/crypto/hash.h
   :functions: crypto_alloc_shash crypto_free_shash crypto_shash_blocksize crypto_shash_digestsize crypto_shash_descsize crypto_shash_setkey crypto_shash_digest crypto_shash_export crypto_shash_import crypto_shash_init crypto_shash_update crypto_shash_final crypto_shash_finup
