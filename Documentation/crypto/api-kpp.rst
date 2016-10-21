Key-agreement Protocol Primitives (KPP) Cipher Algorithm Definitions
--------------------------------------------------------------------

.. kernel-doc:: include/crypto/kpp.h
   :functions: kpp_request

.. kernel-doc:: include/crypto/kpp.h
   :functions: crypto_kpp

.. kernel-doc:: include/crypto/kpp.h
   :functions: kpp_alg

.. kernel-doc:: include/crypto/kpp.h
   :functions: kpp_secret

Key-agreement Protocol Primitives (KPP) Cipher API
--------------------------------------------------

.. kernel-doc:: include/crypto/kpp.h
   :doc: Generic Key-agreement Protocol Primitives API

.. kernel-doc:: include/crypto/kpp.h
   :functions: crypto_alloc_kpp

.. kernel-doc:: include/crypto/kpp.h
   :functions: crypto_free_kpp

.. kernel-doc:: include/crypto/kpp.h
   :functions: crypto_kpp_set_secret

.. kernel-doc:: include/crypto/kpp.h
   :functions: crypto_kpp_generate_public_key

.. kernel-doc:: include/crypto/kpp.h
   :functions: crypto_kpp_compute_shared_secret

.. kernel-doc:: include/crypto/kpp.h
   :functions: crypto_kpp_maxsize

Key-agreement Protocol Primitives (KPP) Cipher Request Handle
-------------------------------------------------------------

.. kernel-doc:: include/crypto/kpp.h
   :functions: kpp_request_alloc

.. kernel-doc:: include/crypto/kpp.h
   :functions: kpp_request_free

.. kernel-doc:: include/crypto/kpp.h
   :functions: kpp_request_set_callback

.. kernel-doc:: include/crypto/kpp.h
   :functions: kpp_request_set_input

.. kernel-doc:: include/crypto/kpp.h
   :functions: kpp_request_set_output

ECDH Helper Functions
---------------------

.. kernel-doc:: include/crypto/ecdh.h
   :doc: ECDH Helper Functions

.. kernel-doc:: include/crypto/ecdh.h
   :functions: ecdh

.. kernel-doc:: include/crypto/ecdh.h
   :functions: crypto_ecdh_key_len

.. kernel-doc:: include/crypto/ecdh.h
   :functions: crypto_ecdh_encode_key

.. kernel-doc:: include/crypto/ecdh.h
   :functions: crypto_ecdh_decode_key

DH Helper Functions
-------------------

.. kernel-doc:: include/crypto/dh.h
   :doc: DH Helper Functions

.. kernel-doc:: include/crypto/dh.h
   :functions: dh

.. kernel-doc:: include/crypto/dh.h
   :functions: crypto_dh_key_len

.. kernel-doc:: include/crypto/dh.h
   :functions: crypto_dh_encode_key

.. kernel-doc:: include/crypto/dh.h
   :functions: crypto_dh_decode_key
