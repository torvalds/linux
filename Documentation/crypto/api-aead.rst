Authenticated Encryption With Associated Data (AEAD) Algorithm Definitions
--------------------------------------------------------------------------

.. kernel-doc:: include/crypto/aead.h
   :doc: Authenticated Encryption With Associated Data (AEAD) Cipher API

.. kernel-doc:: include/crypto/aead.h
   :functions: aead_request aead_alg

Authenticated Encryption With Associated Data (AEAD) Cipher API
---------------------------------------------------------------

.. kernel-doc:: include/crypto/aead.h
   :functions: crypto_alloc_aead crypto_free_aead crypto_aead_ivsize crypto_aead_authsize crypto_aead_blocksize crypto_aead_setkey crypto_aead_setauthsize crypto_aead_encrypt crypto_aead_decrypt

Asynchronous AEAD Request Handle
--------------------------------

.. kernel-doc:: include/crypto/aead.h
   :doc: Asynchronous AEAD Request Handle

.. kernel-doc:: include/crypto/aead.h
   :functions: crypto_aead_reqsize aead_request_set_tfm aead_request_alloc aead_request_free aead_request_set_callback aead_request_set_crypt aead_request_set_ad
