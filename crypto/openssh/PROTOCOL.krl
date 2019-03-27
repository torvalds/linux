This describes the key/certificate revocation list format for OpenSSH.

1. Overall format

The KRL consists of a header and zero or more sections. The header is:

#define KRL_MAGIC		0x5353484b524c0a00ULL  /* "SSHKRL\n\0" */
#define KRL_FORMAT_VERSION	1

	uint64	KRL_MAGIC
	uint32	KRL_FORMAT_VERSION
	uint64	krl_version
	uint64	generated_date
	uint64	flags
	string	reserved
	string	comment

Where "krl_version" is a version number that increases each time the KRL
is modified, "generated_date" is the time in seconds since 1970-01-01
00:00:00 UTC that the KRL was generated, "comment" is an optional comment
and "reserved" an extension field whose contents are currently ignored.
No "flags" are currently defined.

Following the header are zero or more sections, each consisting of:

	byte	section_type
	string	section_data

Where "section_type" indicates the type of the "section_data". An exception
to this is the KRL_SECTION_SIGNATURE section, that has a slightly different
format (see below).

The available section types are:

#define KRL_SECTION_CERTIFICATES		1
#define KRL_SECTION_EXPLICIT_KEY		2
#define KRL_SECTION_FINGERPRINT_SHA1		3
#define KRL_SECTION_SIGNATURE			4

2. Certificate section

These sections use type KRL_SECTION_CERTIFICATES to revoke certificates by
serial number or key ID. The consist of the CA key that issued the
certificates to be revoked and a reserved field whose contents is currently
ignored.

	string ca_key
	string reserved

Where "ca_key" is the standard SSH wire serialisation of the CA's
public key. Alternately, "ca_key" may be an empty string to indicate
the certificate section applies to all CAs (this is most useful when
revoking key IDs).

Followed by one or more sections:

	byte	cert_section_type
	string	cert_section_data

The certificate section types are:

#define KRL_SECTION_CERT_SERIAL_LIST	0x20
#define KRL_SECTION_CERT_SERIAL_RANGE	0x21
#define KRL_SECTION_CERT_SERIAL_BITMAP	0x22
#define KRL_SECTION_CERT_KEY_ID		0x23

2.1 Certificate serial list section

This section is identified as KRL_SECTION_CERT_SERIAL_LIST. It revokes
certificates by listing their serial numbers. The cert_section_data in this
case contains:

	uint64	revoked_cert_serial
	uint64	...

This section may appear multiple times.

2.2. Certificate serial range section

These sections use type KRL_SECTION_CERT_SERIAL_RANGE and hold
a range of serial numbers of certificates:

	uint64	serial_min
	uint64	serial_max

All certificates in the range serial_min <= serial <= serial_max are
revoked.

This section may appear multiple times.

2.3. Certificate serial bitmap section

Bitmap sections use type KRL_SECTION_CERT_SERIAL_BITMAP and revoke keys
by listing their serial number in a bitmap.

	uint64	serial_offset
	mpint	revoked_keys_bitmap

A bit set at index N in the bitmap corresponds to revocation of a keys with
serial number (serial_offset + N).

This section may appear multiple times.

2.4. Revoked key ID sections

KRL_SECTION_CERT_KEY_ID sections revoke particular certificate "key
ID" strings. This may be useful in revoking all certificates
associated with a particular identity, e.g. a host or a user.

	string	key_id[0]
	...

This section must contain at least one "key_id". This section may appear
multiple times.

3. Explicit key sections

These sections, identified as KRL_SECTION_EXPLICIT_KEY, revoke keys
(not certificates). They are less space efficient than serial numbers,
but are able to revoke plain keys.

	string	public_key_blob[0]
	....

This section must contain at least one "public_key_blob". The blob
must be a raw key (i.e. not a certificate).

This section may appear multiple times.

4. SHA1 fingerprint sections

These sections, identified as KRL_SECTION_FINGERPRINT_SHA1, revoke
plain keys (i.e. not certificates) by listing their SHA1 hashes:

	string	public_key_hash[0]
	....

This section must contain at least one "public_key_hash". The hash blob
is obtained by taking the SHA1 hash of the public key blob. Hashes in
this section must appear in numeric order, treating each hash as a big-
endian integer.

This section may appear multiple times.

5. KRL signature sections

The KRL_SECTION_SIGNATURE section serves a different purpose to the
preceding ones: to provide cryptographic authentication of a KRL that
is retrieved over a channel that does not provide integrity protection.
Its format is slightly different to the previously-described sections:
in order to simplify the signature generation, it includes as a "body"
two string components instead of one.

	byte	KRL_SECTION_SIGNATURE
	string	signature_key
	string	signature

The signature is calculated over the entire KRL from the KRL_MAGIC
to this subsection's "signature_key", including both and using the
signature generation rules appropriate for the type of "signature_key".

This section must appear last in the KRL. If multiple signature sections
appear, they must appear consecutively at the end of the KRL file.

Implementations that retrieve KRLs over untrusted channels must verify
signatures. Signature sections are optional for KRLs distributed by
trusted means.

$OpenBSD: PROTOCOL.krl,v 1.4 2018/04/10 00:10:49 djm Exp $
