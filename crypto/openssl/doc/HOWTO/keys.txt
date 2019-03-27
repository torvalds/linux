<DRAFT!>
			HOWTO keys

1. Introduction

Keys are the basis of public key algorithms and PKI.  Keys usually
come in pairs, with one half being the public key and the other half
being the private key.  With OpenSSL, the private key contains the
public key information as well, so a public key doesn't need to be
generated separately.

Public keys come in several flavors, using different cryptographic
algorithms.  The most popular ones associated with certificates are
RSA and DSA, and this HOWTO will show how to generate each of them.


2. To generate a RSA key

A RSA key can be used both for encryption and for signing.

Generating a key for the RSA algorithm is quite easy, all you have to
do is the following:

  openssl genrsa -des3 -out privkey.pem 2048

With this variant, you will be prompted for a protecting password.  If
you don't want your key to be protected by a password, remove the flag
'-des3' from the command line above.

The number 2048 is the size of the key, in bits.  Today, 2048 or
higher is recommended for RSA keys, as fewer amount of bits is
consider insecure or to be insecure pretty soon.


3. To generate a DSA key

A DSA key can be used for signing only.  It is important to
know what a certificate request with a DSA key can really be used for.

Generating a key for the DSA algorithm is a two-step process.  First,
you have to generate parameters from which to generate the key:

  openssl dsaparam -out dsaparam.pem 2048

The number 2048 is the size of the key, in bits.  Today, 2048 or
higher is recommended for DSA keys, as fewer amount of bits is
consider insecure or to be insecure pretty soon.

When that is done, you can generate a key using the parameters in
question (actually, several keys can be generated from the same
parameters):

  openssl gendsa -des3 -out privkey.pem dsaparam.pem

With this variant, you will be prompted for a protecting password.  If
you don't want your key to be protected by a password, remove the flag
'-des3' from the command line above.


4. To generate an EC key

An EC key can be used both for key agreement (ECDH) and signing (ECDSA).

Generating a key for ECC is similar to generating a DSA key. These are
two-step processes. First, you have to get the EC parameters from which
the key will be generated:

  openssl ecparam -name prime256v1 -out prime256v1.pem

The prime256v1, or NIST P-256, which stands for 'X9.62/SECG curve over
a 256-bit prime field', is the name of an elliptic curve which generates the
parameters. You can use the following command to list all supported curves:

  openssl ecparam -list_curves

When that is done, you can generate a key using the created parameters (several
keys can be produced from the same parameters):

  openssl genpkey -des3 -paramfile prime256v1.pem -out private.key

With this variant, you will be prompted for a password to protect your key.
If you don't want your key to be protected by a password, remove the flag
'-des3' from the command line above.

You can also directly generate the key in one step:

  openssl ecparam -genkey -name prime256v1 -out private.key

or

  openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256


5. NOTE

If you intend to use the key together with a server certificate,
it may be reasonable to avoid protecting it with a password, since
otherwise someone would have to type in the password every time the
server needs to access the key.

For X25519 and X448, it's treated as a distinct algorithm but not as one of
the curves listed with 'ecparam -list_curves' option. You can use
the following command to generate an X25519 key:

  openssl genpkey -algorithm X25519 -out xkey.pem
