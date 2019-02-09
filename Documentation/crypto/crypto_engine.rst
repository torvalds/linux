=============
CRYPTO ENGINE
=============

Overview
--------
The crypto engine API (CE), is a crypto queue manager.

Requirement
-----------
You have to put at start of your tfm_ctx the struct crypto_engine_ctx::

  struct your_tfm_ctx {
        struct crypto_engine_ctx enginectx;
        ...
  };

Why: Since CE manage only crypto_async_request, it cannot know the underlying
request_type and so have access only on the TFM.
So using container_of for accessing __ctx is impossible.
Furthermore, the crypto engine cannot know the "struct your_tfm_ctx",
so it must assume that crypto_engine_ctx is at start of it.

Order of operations
-------------------
You have to obtain a struct crypto_engine via crypto_engine_alloc_init().
And start it via crypto_engine_start().

Before transferring any request, you have to fill the enginectx.
- prepare_request: (taking a function pointer) If you need to do some processing before doing the request
- unprepare_request: (taking a function pointer) Undoing what's done in prepare_request
- do_one_request: (taking a function pointer) Do encryption for current request

Note: that those three functions get the crypto_async_request associated with the received request.
So your need to get the original request via container_of(areq, struct yourrequesttype_request, base);

When your driver receive a crypto_request, you have to transfer it to
the cryptoengine via one of:
- crypto_transfer_ablkcipher_request_to_engine()
- crypto_transfer_aead_request_to_engine()
- crypto_transfer_akcipher_request_to_engine()
- crypto_transfer_hash_request_to_engine()
- crypto_transfer_skcipher_request_to_engine()

At the end of the request process, a call to one of the following function is needed:
- crypto_finalize_ablkcipher_request
- crypto_finalize_aead_request
- crypto_finalize_akcipher_request
- crypto_finalize_hash_request
- crypto_finalize_skcipher_request
