Asymmetric Signature Algorithm Definitions
------------------------------------------

.. kernel-doc:: include/crypto/sig.h
   :functions: sig_alg

Asymmetric Signature API
------------------------

.. kernel-doc:: include/crypto/sig.h
   :doc: Generic Public Key Signature API

.. kernel-doc:: include/crypto/sig.h
   :functions: crypto_alloc_sig crypto_free_sig crypto_sig_set_pubkey crypto_sig_set_privkey crypto_sig_keysize crypto_sig_maxsize crypto_sig_digestsize crypto_sig_sign crypto_sig_verify

