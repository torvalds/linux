#! /bin/sh

# ========================================================================
#
# Copyright (c) 2017 Thomas Pornin <pornin@bolet.org>
#
# Permission is hereby granted, free of charge, to any person obtaining 
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be 
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# ========================================================================
#
# This script is used to generate the 'Rules.mk' file from the list
# of source file included below. If the list changes (e.g. to add a
# new source file), then add it here and rerun this script.
#
# ========================================================================

# Solaris compatibility: switch to a more POSIX-compliant /bin/sh.
if [ -z "$BR_SCRIPT_LOOP" ] ; then
	BR_SCRIPT_LOOP=yes
	export BR_SCRIPT_LOOP
	if [ -x /usr/xpg6/bin/sh ] ; then
		exec /usr/xpg6/bin/sh "$0" "$@"
	fi
	if [ -x /usr/xpg4/bin/sh ] ; then
		exec /usr/xpg4/bin/sh "$0" "$@"
	fi
fi

# Exit on first error.
set -e

# Source files. Please keep in alphabetical order.
coresrc=" \
	src/settings.c \
	src/aead/ccm.c \
	src/aead/eax.c \
	src/aead/gcm.c \
	src/codec/ccopy.c \
	src/codec/dec16be.c \
	src/codec/dec16le.c \
	src/codec/dec32be.c \
	src/codec/dec32le.c \
	src/codec/dec64be.c \
	src/codec/dec64le.c \
	src/codec/enc16be.c \
	src/codec/enc16le.c \
	src/codec/enc32be.c \
	src/codec/enc32le.c \
	src/codec/enc64be.c \
	src/codec/enc64le.c \
	src/codec/pemdec.c \
	src/codec/pemenc.c \
	src/ec/ec_all_m15.c \
	src/ec/ec_all_m31.c \
	src/ec/ec_c25519_i15.c \
	src/ec/ec_c25519_i31.c \
	src/ec/ec_c25519_m15.c \
	src/ec/ec_c25519_m31.c \
	src/ec/ec_c25519_m62.c \
	src/ec/ec_c25519_m64.c \
	src/ec/ec_curve25519.c \
	src/ec/ec_default.c \
	src/ec/ec_keygen.c \
	src/ec/ec_p256_m15.c \
	src/ec/ec_p256_m31.c \
	src/ec/ec_p256_m62.c \
	src/ec/ec_p256_m64.c \
	src/ec/ec_prime_i15.c \
	src/ec/ec_prime_i31.c \
	src/ec/ec_pubkey.c \
	src/ec/ec_secp256r1.c \
	src/ec/ec_secp384r1.c \
	src/ec/ec_secp521r1.c \
	src/ec/ecdsa_atr.c \
	src/ec/ecdsa_default_sign_asn1.c \
	src/ec/ecdsa_default_sign_raw.c \
	src/ec/ecdsa_default_vrfy_asn1.c \
	src/ec/ecdsa_default_vrfy_raw.c \
	src/ec/ecdsa_i15_bits.c \
	src/ec/ecdsa_i15_sign_asn1.c \
	src/ec/ecdsa_i15_sign_raw.c \
	src/ec/ecdsa_i15_vrfy_asn1.c \
	src/ec/ecdsa_i15_vrfy_raw.c \
	src/ec/ecdsa_i31_bits.c \
	src/ec/ecdsa_i31_sign_asn1.c \
	src/ec/ecdsa_i31_sign_raw.c \
	src/ec/ecdsa_i31_vrfy_asn1.c \
	src/ec/ecdsa_i31_vrfy_raw.c \
	src/ec/ecdsa_rta.c \
	src/hash/dig_oid.c \
	src/hash/dig_size.c \
	src/hash/ghash_ctmul.c \
	src/hash/ghash_ctmul32.c \
	src/hash/ghash_ctmul64.c \
	src/hash/ghash_pclmul.c \
	src/hash/ghash_pwr8.c \
	src/hash/md5.c \
	src/hash/md5sha1.c \
	src/hash/mgf1.c \
	src/hash/multihash.c \
	src/hash/sha1.c \
	src/hash/sha2big.c \
	src/hash/sha2small.c \
	src/int/i15_add.c \
	src/int/i15_bitlen.c \
	src/int/i15_decmod.c \
	src/int/i15_decode.c \
	src/int/i15_decred.c \
	src/int/i15_encode.c \
	src/int/i15_fmont.c \
	src/int/i15_iszero.c \
	src/int/i15_moddiv.c \
	src/int/i15_modpow.c \
	src/int/i15_modpow2.c \
	src/int/i15_montmul.c \
	src/int/i15_mulacc.c \
	src/int/i15_muladd.c \
	src/int/i15_ninv15.c \
	src/int/i15_reduce.c \
	src/int/i15_rshift.c \
	src/int/i15_sub.c \
	src/int/i15_tmont.c \
	src/int/i31_add.c \
	src/int/i31_bitlen.c \
	src/int/i31_decmod.c \
	src/int/i31_decode.c \
	src/int/i31_decred.c \
	src/int/i31_encode.c \
	src/int/i31_fmont.c \
	src/int/i31_iszero.c \
	src/int/i31_moddiv.c \
	src/int/i31_modpow.c \
	src/int/i31_modpow2.c \
	src/int/i31_montmul.c \
	src/int/i31_mulacc.c \
	src/int/i31_muladd.c \
	src/int/i31_ninv31.c \
	src/int/i31_reduce.c \
	src/int/i31_rshift.c \
	src/int/i31_sub.c \
	src/int/i31_tmont.c \
	src/int/i32_add.c \
	src/int/i32_bitlen.c \
	src/int/i32_decmod.c \
	src/int/i32_decode.c \
	src/int/i32_decred.c \
	src/int/i32_div32.c \
	src/int/i32_encode.c \
	src/int/i32_fmont.c \
	src/int/i32_iszero.c \
	src/int/i32_modpow.c \
	src/int/i32_montmul.c \
	src/int/i32_mulacc.c \
	src/int/i32_muladd.c \
	src/int/i32_ninv32.c \
	src/int/i32_reduce.c \
	src/int/i32_sub.c \
	src/int/i32_tmont.c \
	src/int/i62_modpow2.c \
	src/kdf/hkdf.c \
	src/kdf/shake.c \
	src/mac/hmac.c \
	src/mac/hmac_ct.c \
	src/rand/aesctr_drbg.c \
	src/rand/hmac_drbg.c \
	src/rand/sysrng.c \
	src/rsa/rsa_default_keygen.c \
	src/rsa/rsa_default_modulus.c \
	src/rsa/rsa_default_oaep_decrypt.c \
	src/rsa/rsa_default_oaep_encrypt.c \
	src/rsa/rsa_default_pkcs1_sign.c \
	src/rsa/rsa_default_pkcs1_vrfy.c \
	src/rsa/rsa_default_priv.c \
	src/rsa/rsa_default_privexp.c \
	src/rsa/rsa_default_pss_sign.c \
	src/rsa/rsa_default_pss_vrfy.c \
	src/rsa/rsa_default_pub.c \
	src/rsa/rsa_default_pubexp.c \
	src/rsa/rsa_i15_keygen.c \
	src/rsa/rsa_i15_modulus.c \
	src/rsa/rsa_i15_oaep_decrypt.c \
	src/rsa/rsa_i15_oaep_encrypt.c \
	src/rsa/rsa_i15_pkcs1_sign.c \
	src/rsa/rsa_i15_pkcs1_vrfy.c \
	src/rsa/rsa_i15_priv.c \
	src/rsa/rsa_i15_privexp.c \
	src/rsa/rsa_i15_pss_sign.c \
	src/rsa/rsa_i15_pss_vrfy.c \
	src/rsa/rsa_i15_pub.c \
	src/rsa/rsa_i15_pubexp.c \
	src/rsa/rsa_i31_keygen.c \
	src/rsa/rsa_i31_keygen_inner.c \
	src/rsa/rsa_i31_modulus.c \
	src/rsa/rsa_i31_oaep_decrypt.c \
	src/rsa/rsa_i31_oaep_encrypt.c \
	src/rsa/rsa_i31_pkcs1_sign.c \
	src/rsa/rsa_i31_pkcs1_vrfy.c \
	src/rsa/rsa_i31_priv.c \
	src/rsa/rsa_i31_privexp.c \
	src/rsa/rsa_i31_pss_sign.c \
	src/rsa/rsa_i31_pss_vrfy.c \
	src/rsa/rsa_i31_pub.c \
	src/rsa/rsa_i31_pubexp.c \
	src/rsa/rsa_i32_oaep_decrypt.c \
	src/rsa/rsa_i32_oaep_encrypt.c \
	src/rsa/rsa_i32_pkcs1_sign.c \
	src/rsa/rsa_i32_pkcs1_vrfy.c \
	src/rsa/rsa_i32_priv.c \
	src/rsa/rsa_i32_pss_sign.c \
	src/rsa/rsa_i32_pss_vrfy.c \
	src/rsa/rsa_i32_pub.c \
	src/rsa/rsa_i62_keygen.c \
	src/rsa/rsa_i62_oaep_decrypt.c \
	src/rsa/rsa_i62_oaep_encrypt.c \
	src/rsa/rsa_i62_pkcs1_sign.c \
	src/rsa/rsa_i62_pkcs1_vrfy.c \
	src/rsa/rsa_i62_priv.c \
	src/rsa/rsa_i62_pss_sign.c \
	src/rsa/rsa_i62_pss_vrfy.c \
	src/rsa/rsa_i62_pub.c \
	src/rsa/rsa_oaep_pad.c \
	src/rsa/rsa_oaep_unpad.c \
	src/rsa/rsa_pkcs1_sig_pad.c \
	src/rsa/rsa_pkcs1_sig_unpad.c \
	src/rsa/rsa_pss_sig_pad.c \
	src/rsa/rsa_pss_sig_unpad.c \
	src/rsa/rsa_ssl_decrypt.c \
	src/ssl/prf.c \
	src/ssl/prf_md5sha1.c \
	src/ssl/prf_sha256.c \
	src/ssl/prf_sha384.c \
	src/ssl/ssl_ccert_single_ec.c \
	src/ssl/ssl_ccert_single_rsa.c \
	src/ssl/ssl_client.c \
	src/ssl/ssl_client_default_rsapub.c \
	src/ssl/ssl_client_full.c \
	src/ssl/ssl_engine.c \
	src/ssl/ssl_engine_default_aescbc.c \
	src/ssl/ssl_engine_default_aesccm.c \
	src/ssl/ssl_engine_default_aesgcm.c \
	src/ssl/ssl_engine_default_chapol.c \
	src/ssl/ssl_engine_default_descbc.c \
	src/ssl/ssl_engine_default_ec.c \
	src/ssl/ssl_engine_default_ecdsa.c \
	src/ssl/ssl_engine_default_rsavrfy.c \
	src/ssl/ssl_hashes.c \
	src/ssl/ssl_hs_client.c \
	src/ssl/ssl_hs_server.c \
	src/ssl/ssl_io.c \
	src/ssl/ssl_keyexport.c \
	src/ssl/ssl_lru.c \
	src/ssl/ssl_rec_cbc.c \
	src/ssl/ssl_rec_ccm.c \
	src/ssl/ssl_rec_chapol.c \
	src/ssl/ssl_rec_gcm.c \
	src/ssl/ssl_scert_single_ec.c \
	src/ssl/ssl_scert_single_rsa.c \
	src/ssl/ssl_server.c \
	src/ssl/ssl_server_full_ec.c \
	src/ssl/ssl_server_full_rsa.c \
	src/ssl/ssl_server_mine2c.c \
	src/ssl/ssl_server_mine2g.c \
	src/ssl/ssl_server_minf2c.c \
	src/ssl/ssl_server_minf2g.c \
	src/ssl/ssl_server_minr2g.c \
	src/ssl/ssl_server_minu2g.c \
	src/ssl/ssl_server_minv2g.c \
	src/symcipher/aes_big_cbcdec.c \
	src/symcipher/aes_big_cbcenc.c \
	src/symcipher/aes_big_ctr.c \
	src/symcipher/aes_big_ctrcbc.c \
	src/symcipher/aes_big_dec.c \
	src/symcipher/aes_big_enc.c \
	src/symcipher/aes_common.c \
	src/symcipher/aes_ct.c \
	src/symcipher/aes_ct64.c \
	src/symcipher/aes_ct64_cbcdec.c \
	src/symcipher/aes_ct64_cbcenc.c \
	src/symcipher/aes_ct64_ctr.c \
	src/symcipher/aes_ct64_ctrcbc.c \
	src/symcipher/aes_ct64_dec.c \
	src/symcipher/aes_ct64_enc.c \
	src/symcipher/aes_ct_cbcdec.c \
	src/symcipher/aes_ct_cbcenc.c \
	src/symcipher/aes_ct_ctr.c \
	src/symcipher/aes_ct_ctrcbc.c \
	src/symcipher/aes_ct_dec.c \
	src/symcipher/aes_ct_enc.c \
	src/symcipher/aes_pwr8.c \
	src/symcipher/aes_pwr8_cbcdec.c \
	src/symcipher/aes_pwr8_cbcenc.c \
	src/symcipher/aes_pwr8_ctr.c \
	src/symcipher/aes_pwr8_ctrcbc.c \
	src/symcipher/aes_small_cbcdec.c \
	src/symcipher/aes_small_cbcenc.c \
	src/symcipher/aes_small_ctr.c \
	src/symcipher/aes_small_ctrcbc.c \
	src/symcipher/aes_small_dec.c \
	src/symcipher/aes_small_enc.c \
	src/symcipher/aes_x86ni.c \
	src/symcipher/aes_x86ni_cbcdec.c \
	src/symcipher/aes_x86ni_cbcenc.c \
	src/symcipher/aes_x86ni_ctr.c \
	src/symcipher/aes_x86ni_ctrcbc.c \
	src/symcipher/chacha20_ct.c \
	src/symcipher/chacha20_sse2.c \
	src/symcipher/des_ct.c \
	src/symcipher/des_ct_cbcdec.c \
	src/symcipher/des_ct_cbcenc.c \
	src/symcipher/des_support.c \
	src/symcipher/des_tab.c \
	src/symcipher/des_tab_cbcdec.c \
	src/symcipher/des_tab_cbcenc.c \
	src/symcipher/poly1305_ctmul.c \
	src/symcipher/poly1305_ctmul32.c \
	src/symcipher/poly1305_ctmulq.c \
	src/symcipher/poly1305_i15.c \
	src/x509/asn1enc.c \
	src/x509/encode_ec_pk8der.c \
	src/x509/encode_ec_rawder.c \
	src/x509/encode_rsa_pk8der.c \
	src/x509/encode_rsa_rawder.c \
	src/x509/skey_decoder.c \
	src/x509/x509_decoder.c \
	src/x509/x509_knownkey.c \
	src/x509/x509_minimal.c \
	src/x509/x509_minimal_full.c"

# Source files for the 'brssl' command-line tool.
toolssrc=" \
	tools/brssl.c \
	tools/certs.c \
	tools/chain.c \
	tools/client.c \
	tools/errors.c \
	tools/files.c \
	tools/impl.c \
	tools/keys.c \
	tools/names.c \
	tools/server.c \
	tools/skey.c \
	tools/sslio.c \
	tools/ta.c \
	tools/twrch.c \
	tools/vector.c \
	tools/verify.c \
	tools/xmem.c"

# Source files the the 'testcrypto' command-line tool.
testcryptosrc=" \
	test/test_crypto.c"

# Source files the the 'testspeed' command-line tool.
testspeedsrc=" \
	test/test_speed.c"

# Source files the the 'testx509' command-line tool.
testx509src=" \
	test/test_x509.c"

# Public header files.
headerspub=" \
	inc/bearssl.h \
	inc/bearssl_aead.h \
	inc/bearssl_block.h \
	inc/bearssl_ec.h \
	inc/bearssl_hash.h \
	inc/bearssl_hmac.h \
	inc/bearssl_kdf.h \
	inc/bearssl_pem.h \
	inc/bearssl_prf.h \
	inc/bearssl_rand.h \
	inc/bearssl_rsa.h \
	inc/bearssl_ssl.h \
	inc/bearssl_x509.h"

# Private header files.
headerspriv=" \
	src/config.h \
	src/inner.h"

# Header files for the 'brssl' command-line tool.
headerstools=" \
	tools/brssl.h"

# T0 compiler source code.
t0compsrc=" \
	T0/BlobWriter.cs \
	T0/CPU.cs \
	T0/CodeElement.cs \
	T0/CodeElementJump.cs \
	T0/CodeElementUInt.cs \
	T0/CodeElementUIntExpr.cs \
	T0/CodeElementUIntInt.cs \
	T0/CodeElementUIntUInt.cs \
	T0/ConstData.cs \
	T0/Opcode.cs \
	T0/OpcodeCall.cs \
	T0/OpcodeConst.cs \
	T0/OpcodeGetLocal.cs \
	T0/OpcodeJump.cs \
	T0/OpcodeJumpIf.cs \
	T0/OpcodeJumpIfNot.cs \
	T0/OpcodeJumpUncond.cs \
	T0/OpcodePutLocal.cs \
	T0/OpcodeRet.cs \
	T0/SType.cs \
	T0/T0Comp.cs \
	T0/TPointerBase.cs \
	T0/TPointerBlob.cs \
	T0/TPointerExpr.cs \
	T0/TPointerNull.cs \
	T0/TPointerXT.cs \
	T0/TValue.cs \
	T0/Word.cs \
	T0/WordBuilder.cs \
	T0/WordData.cs \
	T0/WordInterpreted.cs \
	T0/WordNative.cs"

t0compkern=" \
	T0/kern.t0"

# Function to turn slashes into $P (macro for path separator).
escsep() {
	printf '%s' "$1" | sed 's/\//$P/g'
}

# Create rules file.
rm -f Rules.mk
cat > Rules.mk <<EOF
# Automatically generated rules. Use 'mkrules.sh' to modify/regenerate.
EOF

(printf "\nOBJ ="
for f in $coresrc ; do
	printf ' \\\n $(OBJDIR)$P%s' "$(basename "$f" .c)\$O"
done
printf "\nOBJBRSSL ="
for f in $toolssrc ; do
	printf ' \\\n $(OBJDIR)$P%s' "$(basename "$f" .c)\$O"
done
printf "\nOBJTESTCRYPTO ="
for f in $testcryptosrc ; do
	printf ' \\\n $(OBJDIR)$P%s' "$(basename "$f" .c)\$O"
done
printf "\nOBJTESTSPEED ="
for f in $testspeedsrc ; do
	printf ' \\\n $(OBJDIR)$P%s' "$(basename "$f" .c)\$O"
done
printf "\nOBJTESTX509 ="
for f in $testx509src ; do
	printf ' \\\n $(OBJDIR)$P%s' "$(basename "$f" .c)\$O"
done
printf "\nHEADERSPUB ="
for f in $headerspub ; do
	printf " %s" "$(escsep "$f")"
done
printf "\nHEADERSPRIV = %s" '$(HEADERSPUB)'
for f in $headerspriv ; do
	printf " %s" "$(escsep "$f")"
done
printf "\nHEADERSTOOLS = %s" '$(HEADERSPUB)'
for f in $headerstools ; do
	printf " %s" "$(escsep "$f")"
done
printf "\nT0SRC ="
for f in $t0compsrc ; do
	printf " %s" "$(escsep "$f")"
done
printf "\nT0KERN ="
for f in $t0kernsrc ; do
	printf " %s" "$(escsep "$f")"
done
printf "\n") >> Rules.mk

cat >> Rules.mk <<EOF

all: \$(STATICLIB) \$(DLL) \$(TOOLS) \$(TESTS)

no:

lib: \$(BEARSSLLIB)

dll: \$(BEARSSLDLL)

tools: \$(BRSSL)

tests: \$(TESTCRYPTO) \$(TESTSPEED) \$(TESTX509)

T0: kT0

kT0: \$(T0COMP) src\$Pssl\$Pssl_hs_common.t0 src\$Pssl\$Pssl_hs_client.t0 src\$Pssl\$Pssl_hs_server.t0 src\$Px509\$Pasn1.t0 src\$Px509\$Pskey_decoder.t0 src\$Px509\$Px509_decoder.t0 src\$Px509\$Px509_minimal.t0
	\$(RUNT0COMP) -o src\$Pcodec\$Ppemdec -r br_pem_decoder src\$Pcodec\$Ppemdec.t0
	\$(RUNT0COMP) -o src\$Pssl\$Pssl_hs_client -r br_ssl_hs_client src\$Pssl\$Pssl_hs_common.t0 src\$Pssl\$Pssl_hs_client.t0
	\$(RUNT0COMP) -o src\$Pssl\$Pssl_hs_server -r br_ssl_hs_server src\$Pssl\$Pssl_hs_common.t0 src\$Pssl\$Pssl_hs_server.t0
	\$(RUNT0COMP) -o src\$Px509\$Pskey_decoder -r br_skey_decoder src\$Px509\$Pasn1.t0 src\$Px509\$Pskey_decoder.t0
	\$(RUNT0COMP) -o src\$Px509\$Px509_decoder -r br_x509_decoder src\$Px509\$Pasn1.t0 src\$Px509\$Px509_decoder.t0
	\$(RUNT0COMP) -o src\$Px509\$Px509_minimal -r br_x509_minimal src\$Px509\$Pasn1.t0 src\$Px509\$Px509_minimal.t0

\$(T0COMP): \$(T0SRC) \$(T0KERN)
	\$(MKT0COMP)

clean:
	-\$(RM) \$(OBJDIR)\$P*\$O
	-\$(RM) \$(BEARSSLLIB) \$(BEARSSLDLL) \$(BRSSL) \$(TESTCRYPTO) \$(TESTSPEED) \$(TESTX509)

\$(OBJDIR):
	-\$(MKDIR) \$(OBJDIR)

\$(BEARSSLLIB): \$(OBJDIR) \$(OBJ)
	\$(AR) \$(ARFLAGS) \$(AROUT)\$(BEARSSLLIB) \$(OBJ)

\$(BEARSSLDLL): \$(OBJDIR) \$(OBJ)
	\$(LDDLL) \$(LDDLLFLAGS) \$(LDDLLOUT)\$(BEARSSLDLL) \$(OBJ)

\$(BRSSL): \$(BEARSSLLIB) \$(OBJBRSSL)
	\$(LD) \$(LDFLAGS) \$(LDOUT)\$(BRSSL) \$(OBJBRSSL) \$(BEARSSLLIB)

\$(TESTCRYPTO): \$(BEARSSLLIB) \$(OBJTESTCRYPTO)
	\$(LD) \$(LDFLAGS) \$(LDOUT)\$(TESTCRYPTO) \$(OBJTESTCRYPTO) \$(BEARSSLLIB)

\$(TESTSPEED): \$(BEARSSLLIB) \$(OBJTESTSPEED)
	\$(LD) \$(LDFLAGS) \$(LDOUT)\$(TESTSPEED) \$(OBJTESTSPEED) \$(BEARSSLLIB)

\$(TESTX509): \$(BEARSSLLIB) \$(OBJTESTX509)
	\$(LD) \$(LDFLAGS) \$(LDOUT)\$(TESTX509) \$(OBJTESTX509) \$(BEARSSLLIB)
EOF

(for f in $coresrc ; do
	b="$(basename "$f" .c)\$O"
	g="$(escsep "$f")"
	printf '\n$(OBJDIR)$P%s: %s $(HEADERSPRIV)\n\t$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$P%s %s\n' "$b" "$g" "$b" "$g"
done

for f in $toolssrc ; do
	b="$(basename "$f" .c)\$O"
	g="$(escsep "$f")"
	printf '\n$(OBJDIR)$P%s: %s $(HEADERSTOOLS)\n\t$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$P%s %s\n' "$b" "$g" "$b" "$g"
done

for f in $testcryptosrc $testspeedsrc ; do
	b="$(basename "$f" .c)\$O"
	g="$(escsep "$f")"
	printf '\n$(OBJDIR)$P%s: %s $(HEADERSPRIV)\n\t$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$P%s %s\n' "$b" "$g" "$b" "$g"
done

for f in $testx509src ; do
	b="$(basename "$f" .c)\$O"
	g="$(escsep "$f")"
	printf '\n$(OBJDIR)$P%s: %s $(HEADERSPRIV)\n\t$(CC) $(CFLAGS) $(INCFLAGS) -DSRCDIRNAME=".." $(CCOUT)$(OBJDIR)$P%s %s\n' "$b" "$g" "$b" "$g"
done) >> Rules.mk
