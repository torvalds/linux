.. SPDX-License-Identifier: GPL-2.0

===========================
Kerberos V Cryptography API
===========================

.. Contents:

  - Overview.
    - Small Buffer.
  - Encoding Type.
  - Key Derivation.
    - PRF+ Calculation.
    - Kc, Ke And Ki Derivation.
  - Crypto Functions.
    - Preparation Functions.
    - Encryption Mode.
    - Checksum Mode.
  - The krb5enc AEAD algorithm

Overview
========

This API provides Kerberos 5-style cryptography for key derivation, encryption
and checksumming for use in network filesystems and can be used to implement
the low-level crypto that's needed for GSSAPI.

The following crypto types are supported::

	KRB5_ENCTYPE_AES128_CTS_HMAC_SHA1_96
	KRB5_ENCTYPE_AES256_CTS_HMAC_SHA1_96
	KRB5_ENCTYPE_AES128_CTS_HMAC_SHA256_128
	KRB5_ENCTYPE_AES256_CTS_HMAC_SHA384_192
	KRB5_ENCTYPE_CAMELLIA128_CTS_CMAC
	KRB5_ENCTYPE_CAMELLIA256_CTS_CMAC

	KRB5_CKSUMTYPE_HMAC_SHA1_96_AES128
	KRB5_CKSUMTYPE_HMAC_SHA1_96_AES256
	KRB5_CKSUMTYPE_CMAC_CAMELLIA128
	KRB5_CKSUMTYPE_CMAC_CAMELLIA256
	KRB5_CKSUMTYPE_HMAC_SHA256_128_AES128
	KRB5_CKSUMTYPE_HMAC_SHA384_192_AES256

The API can be included by::

	#include <crypto/krb5.h>

Small Buffer
------------

To pass small pieces of data about, such as keys, a buffer structure is
defined, giving a pointer to the data and the size of that data::

	struct krb5_buffer {
		unsigned int	len;
		void		*data;
	};

Encoding Type
=============

The encoding type is defined by the following structure::

	struct krb5_enctype {
		int		etype;
		int		ctype;
		const char	*name;
		u16		key_bytes;
		u16		key_len;
		u16		Kc_len;
		u16		Ke_len;
		u16		Ki_len;
		u16		prf_len;
		u16		block_len;
		u16		conf_len;
		u16		cksum_len;
		...
	};

The fields of interest to the user of the API are as follows:

  * ``etype`` and ``ctype`` indicate the protocol number for this encoding
    type for encryption and checksumming respectively.  They hold
    ``KRB5_ENCTYPE_*`` and ``KRB5_CKSUMTYPE_*`` constants.

  * ``name`` is the formal name of the encoding.

  * ``key_len`` and ``key_bytes`` are the input key length and the derived key
    length.  (I think they only differ for DES, which isn't supported here).

  * ``Kc_len``, ``Ke_len`` and ``Ki_len`` are the sizes of the derived Kc, Ke
    and Ki keys.  Kc is used for in checksum mode; Ke and Ki are used in
    encryption mode.

  * ``prf_len`` is the size of the result from the PRF+ function calculation.

  * ``block_len``, ``conf_len`` and ``cksum_len`` are the encryption block
    length, confounder length and checksum length respectively.  All three are
    used in encryption mode, but only the checksum length is used in checksum
    mode.

The encoding type is looked up by number using the following function::

	const struct krb5_enctype *crypto_krb5_find_enctype(u32 enctype);

Key Derivation
==============

Once the application has selected an encryption type, the keys that will be
used to do the actual crypto can be derived from the transport key.

PRF+ Calculation
----------------

To aid in key derivation, a function to calculate the Kerberos GSSAPI
mechanism's PRF+ is provided::

	int crypto_krb5_calc_PRFplus(const struct krb5_enctype *krb5,
				     const struct krb5_buffer *K,
				     unsigned int L,
				     const struct krb5_buffer *S,
				     struct krb5_buffer *result,
				     gfp_t gfp);

This can be used to derive the transport key from a source key plus additional
data to limit its use.

Crypto Functions
================

Once the keys have been derived, crypto can be performed on the data.  The
caller must leave gaps in the buffer for the storage of the confounder (if
needed) and the checksum when preparing a message for transmission.  An enum
and a pair of functions are provided to aid in this::

	enum krb5_crypto_mode {
		KRB5_CHECKSUM_MODE,
		KRB5_ENCRYPT_MODE,
	};

	size_t crypto_krb5_how_much_buffer(const struct krb5_enctype *krb5,
					   enum krb5_crypto_mode mode,
					   size_t data_size, size_t *_offset);

	size_t crypto_krb5_how_much_data(const struct krb5_enctype *krb5,
					 enum krb5_crypto_mode mode,
					 size_t *_buffer_size, size_t *_offset);

All these functions take the encoding type and an indication the mode of crypto
(checksum-only or full encryption).

The first function returns how big the buffer will need to be to house a given
amount of data; the second function returns how much data will fit in a buffer
of a particular size, and adjusts down the size of the required buffer
accordingly.  In both cases, the offset of the data within the buffer is also
returned.

When a message has been received, the location and size of the data with the
message can be determined by calling::

	void crypto_krb5_where_is_the_data(const struct krb5_enctype *krb5,
					   enum krb5_crypto_mode mode,
					   size_t *_offset, size_t *_len);

The caller provides the offset and length of the message to the function, which
then alters those values to indicate the region containing the data (plus any
padding).  It is up to the caller to determine how much padding there is.

Preparation Functions
---------------------

Two functions are provided to allocated and prepare a crypto object for use by
the action functions::

	struct crypto_aead *
	crypto_krb5_prepare_encryption(const struct krb5_enctype *krb5,
				       const struct krb5_buffer *TK,
				       u32 usage, gfp_t gfp);
	struct crypto_shash *
	crypto_krb5_prepare_checksum(const struct krb5_enctype *krb5,
				     const struct krb5_buffer *TK,
				     u32 usage, gfp_t gfp);

Both of these functions take the encoding type, the transport key and the usage
value used to derive the appropriate subkey(s).  They create an appropriate
crypto object, an AEAD template for encryption and a synchronous hash for
checksumming, set the key(s) on it and configure it.  The caller is expected to
pass these handles to the action functions below.

Encryption Mode
---------------

A pair of functions are provided to encrypt and decrypt a message::

	ssize_t crypto_krb5_encrypt(const struct krb5_enctype *krb5,
				    struct crypto_aead *aead,
				    struct scatterlist *sg, unsigned int nr_sg,
				    size_t sg_len,
				    size_t data_offset, size_t data_len,
				    bool preconfounded);
	int crypto_krb5_decrypt(const struct krb5_enctype *krb5,
				struct crypto_aead *aead,
				struct scatterlist *sg, unsigned int nr_sg,
				size_t *_offset, size_t *_len);

In both cases, the input and output buffers are indicated by the same
scatterlist.

For the encryption function, the output buffer may be larger than is needed
(the amount of output generated is returned) and the location and size of the
data are indicated (which must match the encoding).  If no confounder is set,
the function will insert one.

For the decryption function, the offset and length of the message in buffer are
supplied and these are shrunk to fit the data.  The decryption function will
verify any checksums within the message and give an error if they don't match.

Checksum Mode
-------------

A pair of function are provided to generate the checksum on a message and to
verify that checksum::

	ssize_t crypto_krb5_get_mic(const struct krb5_enctype *krb5,
				    struct crypto_shash *shash,
				    const struct krb5_buffer *metadata,
				    struct scatterlist *sg, unsigned int nr_sg,
				    size_t sg_len,
				    size_t data_offset, size_t data_len);
	int crypto_krb5_verify_mic(const struct krb5_enctype *krb5,
				   struct crypto_shash *shash,
				   const struct krb5_buffer *metadata,
				   struct scatterlist *sg, unsigned int nr_sg,
				   size_t *_offset, size_t *_len);

In both cases, the input and output buffers are indicated by the same
scatterlist.  Additional metadata can be passed in which will get added to the
hash before the data.

For the get_mic function, the output buffer may be larger than is needed (the
amount of output generated is returned) and the location and size of the data
are indicated (which must match the encoding).

For the verification function, the offset and length of the message in buffer
are supplied and these are shrunk to fit the data.  An error will be returned
if the checksums don't match.

The krb5enc AEAD algorithm
==========================

A template AEAD crypto algorithm, called "krb5enc", is provided that hashes the
plaintext before encrypting it (the reverse of authenc).  The handle returned
by ``crypto_krb5_prepare_encryption()`` may be one of these, but there's no
requirement for the user of this API to interact with it directly.

For reference, its key format begins with a BE32 of the format number.  Only
format 1 is provided and that continues with a BE32 of the Ke key length
followed by a BE32 of the Ki key length, followed by the bytes from the Ke key
and then the Ki key.

Using specifically ordered words means that the static test data doesn't
require byteswapping.
