# Automatically generated rules. Use 'mkrules.sh' to modify/regenerate.

OBJ = \
 $(OBJDIR)$Psettings$O \
 $(OBJDIR)$Pccm$O \
 $(OBJDIR)$Peax$O \
 $(OBJDIR)$Pgcm$O \
 $(OBJDIR)$Pccopy$O \
 $(OBJDIR)$Pdec16be$O \
 $(OBJDIR)$Pdec16le$O \
 $(OBJDIR)$Pdec32be$O \
 $(OBJDIR)$Pdec32le$O \
 $(OBJDIR)$Pdec64be$O \
 $(OBJDIR)$Pdec64le$O \
 $(OBJDIR)$Penc16be$O \
 $(OBJDIR)$Penc16le$O \
 $(OBJDIR)$Penc32be$O \
 $(OBJDIR)$Penc32le$O \
 $(OBJDIR)$Penc64be$O \
 $(OBJDIR)$Penc64le$O \
 $(OBJDIR)$Ppemdec$O \
 $(OBJDIR)$Ppemenc$O \
 $(OBJDIR)$Pec_all_m15$O \
 $(OBJDIR)$Pec_all_m31$O \
 $(OBJDIR)$Pec_c25519_i15$O \
 $(OBJDIR)$Pec_c25519_i31$O \
 $(OBJDIR)$Pec_c25519_m15$O \
 $(OBJDIR)$Pec_c25519_m31$O \
 $(OBJDIR)$Pec_c25519_m62$O \
 $(OBJDIR)$Pec_c25519_m64$O \
 $(OBJDIR)$Pec_curve25519$O \
 $(OBJDIR)$Pec_default$O \
 $(OBJDIR)$Pec_keygen$O \
 $(OBJDIR)$Pec_p256_m15$O \
 $(OBJDIR)$Pec_p256_m31$O \
 $(OBJDIR)$Pec_p256_m62$O \
 $(OBJDIR)$Pec_p256_m64$O \
 $(OBJDIR)$Pec_prime_i15$O \
 $(OBJDIR)$Pec_prime_i31$O \
 $(OBJDIR)$Pec_pubkey$O \
 $(OBJDIR)$Pec_secp256r1$O \
 $(OBJDIR)$Pec_secp384r1$O \
 $(OBJDIR)$Pec_secp521r1$O \
 $(OBJDIR)$Pecdsa_atr$O \
 $(OBJDIR)$Pecdsa_default_sign_asn1$O \
 $(OBJDIR)$Pecdsa_default_sign_raw$O \
 $(OBJDIR)$Pecdsa_default_vrfy_asn1$O \
 $(OBJDIR)$Pecdsa_default_vrfy_raw$O \
 $(OBJDIR)$Pecdsa_i15_bits$O \
 $(OBJDIR)$Pecdsa_i15_sign_asn1$O \
 $(OBJDIR)$Pecdsa_i15_sign_raw$O \
 $(OBJDIR)$Pecdsa_i15_vrfy_asn1$O \
 $(OBJDIR)$Pecdsa_i15_vrfy_raw$O \
 $(OBJDIR)$Pecdsa_i31_bits$O \
 $(OBJDIR)$Pecdsa_i31_sign_asn1$O \
 $(OBJDIR)$Pecdsa_i31_sign_raw$O \
 $(OBJDIR)$Pecdsa_i31_vrfy_asn1$O \
 $(OBJDIR)$Pecdsa_i31_vrfy_raw$O \
 $(OBJDIR)$Pecdsa_rta$O \
 $(OBJDIR)$Pdig_oid$O \
 $(OBJDIR)$Pdig_size$O \
 $(OBJDIR)$Pghash_ctmul$O \
 $(OBJDIR)$Pghash_ctmul32$O \
 $(OBJDIR)$Pghash_ctmul64$O \
 $(OBJDIR)$Pghash_pclmul$O \
 $(OBJDIR)$Pghash_pwr8$O \
 $(OBJDIR)$Pmd5$O \
 $(OBJDIR)$Pmd5sha1$O \
 $(OBJDIR)$Pmgf1$O \
 $(OBJDIR)$Pmultihash$O \
 $(OBJDIR)$Psha1$O \
 $(OBJDIR)$Psha2big$O \
 $(OBJDIR)$Psha2small$O \
 $(OBJDIR)$Pi15_add$O \
 $(OBJDIR)$Pi15_bitlen$O \
 $(OBJDIR)$Pi15_decmod$O \
 $(OBJDIR)$Pi15_decode$O \
 $(OBJDIR)$Pi15_decred$O \
 $(OBJDIR)$Pi15_encode$O \
 $(OBJDIR)$Pi15_fmont$O \
 $(OBJDIR)$Pi15_iszero$O \
 $(OBJDIR)$Pi15_moddiv$O \
 $(OBJDIR)$Pi15_modpow$O \
 $(OBJDIR)$Pi15_modpow2$O \
 $(OBJDIR)$Pi15_montmul$O \
 $(OBJDIR)$Pi15_mulacc$O \
 $(OBJDIR)$Pi15_muladd$O \
 $(OBJDIR)$Pi15_ninv15$O \
 $(OBJDIR)$Pi15_reduce$O \
 $(OBJDIR)$Pi15_rshift$O \
 $(OBJDIR)$Pi15_sub$O \
 $(OBJDIR)$Pi15_tmont$O \
 $(OBJDIR)$Pi31_add$O \
 $(OBJDIR)$Pi31_bitlen$O \
 $(OBJDIR)$Pi31_decmod$O \
 $(OBJDIR)$Pi31_decode$O \
 $(OBJDIR)$Pi31_decred$O \
 $(OBJDIR)$Pi31_encode$O \
 $(OBJDIR)$Pi31_fmont$O \
 $(OBJDIR)$Pi31_iszero$O \
 $(OBJDIR)$Pi31_moddiv$O \
 $(OBJDIR)$Pi31_modpow$O \
 $(OBJDIR)$Pi31_modpow2$O \
 $(OBJDIR)$Pi31_montmul$O \
 $(OBJDIR)$Pi31_mulacc$O \
 $(OBJDIR)$Pi31_muladd$O \
 $(OBJDIR)$Pi31_ninv31$O \
 $(OBJDIR)$Pi31_reduce$O \
 $(OBJDIR)$Pi31_rshift$O \
 $(OBJDIR)$Pi31_sub$O \
 $(OBJDIR)$Pi31_tmont$O \
 $(OBJDIR)$Pi32_add$O \
 $(OBJDIR)$Pi32_bitlen$O \
 $(OBJDIR)$Pi32_decmod$O \
 $(OBJDIR)$Pi32_decode$O \
 $(OBJDIR)$Pi32_decred$O \
 $(OBJDIR)$Pi32_div32$O \
 $(OBJDIR)$Pi32_encode$O \
 $(OBJDIR)$Pi32_fmont$O \
 $(OBJDIR)$Pi32_iszero$O \
 $(OBJDIR)$Pi32_modpow$O \
 $(OBJDIR)$Pi32_montmul$O \
 $(OBJDIR)$Pi32_mulacc$O \
 $(OBJDIR)$Pi32_muladd$O \
 $(OBJDIR)$Pi32_ninv32$O \
 $(OBJDIR)$Pi32_reduce$O \
 $(OBJDIR)$Pi32_sub$O \
 $(OBJDIR)$Pi32_tmont$O \
 $(OBJDIR)$Pi62_modpow2$O \
 $(OBJDIR)$Phkdf$O \
 $(OBJDIR)$Pshake$O \
 $(OBJDIR)$Phmac$O \
 $(OBJDIR)$Phmac_ct$O \
 $(OBJDIR)$Paesctr_drbg$O \
 $(OBJDIR)$Phmac_drbg$O \
 $(OBJDIR)$Psysrng$O \
 $(OBJDIR)$Prsa_default_keygen$O \
 $(OBJDIR)$Prsa_default_modulus$O \
 $(OBJDIR)$Prsa_default_oaep_decrypt$O \
 $(OBJDIR)$Prsa_default_oaep_encrypt$O \
 $(OBJDIR)$Prsa_default_pkcs1_sign$O \
 $(OBJDIR)$Prsa_default_pkcs1_vrfy$O \
 $(OBJDIR)$Prsa_default_priv$O \
 $(OBJDIR)$Prsa_default_privexp$O \
 $(OBJDIR)$Prsa_default_pss_sign$O \
 $(OBJDIR)$Prsa_default_pss_vrfy$O \
 $(OBJDIR)$Prsa_default_pub$O \
 $(OBJDIR)$Prsa_default_pubexp$O \
 $(OBJDIR)$Prsa_i15_keygen$O \
 $(OBJDIR)$Prsa_i15_modulus$O \
 $(OBJDIR)$Prsa_i15_oaep_decrypt$O \
 $(OBJDIR)$Prsa_i15_oaep_encrypt$O \
 $(OBJDIR)$Prsa_i15_pkcs1_sign$O \
 $(OBJDIR)$Prsa_i15_pkcs1_vrfy$O \
 $(OBJDIR)$Prsa_i15_priv$O \
 $(OBJDIR)$Prsa_i15_privexp$O \
 $(OBJDIR)$Prsa_i15_pss_sign$O \
 $(OBJDIR)$Prsa_i15_pss_vrfy$O \
 $(OBJDIR)$Prsa_i15_pub$O \
 $(OBJDIR)$Prsa_i15_pubexp$O \
 $(OBJDIR)$Prsa_i31_keygen$O \
 $(OBJDIR)$Prsa_i31_keygen_inner$O \
 $(OBJDIR)$Prsa_i31_modulus$O \
 $(OBJDIR)$Prsa_i31_oaep_decrypt$O \
 $(OBJDIR)$Prsa_i31_oaep_encrypt$O \
 $(OBJDIR)$Prsa_i31_pkcs1_sign$O \
 $(OBJDIR)$Prsa_i31_pkcs1_vrfy$O \
 $(OBJDIR)$Prsa_i31_priv$O \
 $(OBJDIR)$Prsa_i31_privexp$O \
 $(OBJDIR)$Prsa_i31_pss_sign$O \
 $(OBJDIR)$Prsa_i31_pss_vrfy$O \
 $(OBJDIR)$Prsa_i31_pub$O \
 $(OBJDIR)$Prsa_i31_pubexp$O \
 $(OBJDIR)$Prsa_i32_oaep_decrypt$O \
 $(OBJDIR)$Prsa_i32_oaep_encrypt$O \
 $(OBJDIR)$Prsa_i32_pkcs1_sign$O \
 $(OBJDIR)$Prsa_i32_pkcs1_vrfy$O \
 $(OBJDIR)$Prsa_i32_priv$O \
 $(OBJDIR)$Prsa_i32_pss_sign$O \
 $(OBJDIR)$Prsa_i32_pss_vrfy$O \
 $(OBJDIR)$Prsa_i32_pub$O \
 $(OBJDIR)$Prsa_i62_keygen$O \
 $(OBJDIR)$Prsa_i62_oaep_decrypt$O \
 $(OBJDIR)$Prsa_i62_oaep_encrypt$O \
 $(OBJDIR)$Prsa_i62_pkcs1_sign$O \
 $(OBJDIR)$Prsa_i62_pkcs1_vrfy$O \
 $(OBJDIR)$Prsa_i62_priv$O \
 $(OBJDIR)$Prsa_i62_pss_sign$O \
 $(OBJDIR)$Prsa_i62_pss_vrfy$O \
 $(OBJDIR)$Prsa_i62_pub$O \
 $(OBJDIR)$Prsa_oaep_pad$O \
 $(OBJDIR)$Prsa_oaep_unpad$O \
 $(OBJDIR)$Prsa_pkcs1_sig_pad$O \
 $(OBJDIR)$Prsa_pkcs1_sig_unpad$O \
 $(OBJDIR)$Prsa_pss_sig_pad$O \
 $(OBJDIR)$Prsa_pss_sig_unpad$O \
 $(OBJDIR)$Prsa_ssl_decrypt$O \
 $(OBJDIR)$Pprf$O \
 $(OBJDIR)$Pprf_md5sha1$O \
 $(OBJDIR)$Pprf_sha256$O \
 $(OBJDIR)$Pprf_sha384$O \
 $(OBJDIR)$Pssl_ccert_single_ec$O \
 $(OBJDIR)$Pssl_ccert_single_rsa$O \
 $(OBJDIR)$Pssl_client$O \
 $(OBJDIR)$Pssl_client_default_rsapub$O \
 $(OBJDIR)$Pssl_client_full$O \
 $(OBJDIR)$Pssl_engine$O \
 $(OBJDIR)$Pssl_engine_default_aescbc$O \
 $(OBJDIR)$Pssl_engine_default_aesccm$O \
 $(OBJDIR)$Pssl_engine_default_aesgcm$O \
 $(OBJDIR)$Pssl_engine_default_chapol$O \
 $(OBJDIR)$Pssl_engine_default_descbc$O \
 $(OBJDIR)$Pssl_engine_default_ec$O \
 $(OBJDIR)$Pssl_engine_default_ecdsa$O \
 $(OBJDIR)$Pssl_engine_default_rsavrfy$O \
 $(OBJDIR)$Pssl_hashes$O \
 $(OBJDIR)$Pssl_hs_client$O \
 $(OBJDIR)$Pssl_hs_server$O \
 $(OBJDIR)$Pssl_io$O \
 $(OBJDIR)$Pssl_keyexport$O \
 $(OBJDIR)$Pssl_lru$O \
 $(OBJDIR)$Pssl_rec_cbc$O \
 $(OBJDIR)$Pssl_rec_ccm$O \
 $(OBJDIR)$Pssl_rec_chapol$O \
 $(OBJDIR)$Pssl_rec_gcm$O \
 $(OBJDIR)$Pssl_scert_single_ec$O \
 $(OBJDIR)$Pssl_scert_single_rsa$O \
 $(OBJDIR)$Pssl_server$O \
 $(OBJDIR)$Pssl_server_full_ec$O \
 $(OBJDIR)$Pssl_server_full_rsa$O \
 $(OBJDIR)$Pssl_server_mine2c$O \
 $(OBJDIR)$Pssl_server_mine2g$O \
 $(OBJDIR)$Pssl_server_minf2c$O \
 $(OBJDIR)$Pssl_server_minf2g$O \
 $(OBJDIR)$Pssl_server_minr2g$O \
 $(OBJDIR)$Pssl_server_minu2g$O \
 $(OBJDIR)$Pssl_server_minv2g$O \
 $(OBJDIR)$Paes_big_cbcdec$O \
 $(OBJDIR)$Paes_big_cbcenc$O \
 $(OBJDIR)$Paes_big_ctr$O \
 $(OBJDIR)$Paes_big_ctrcbc$O \
 $(OBJDIR)$Paes_big_dec$O \
 $(OBJDIR)$Paes_big_enc$O \
 $(OBJDIR)$Paes_common$O \
 $(OBJDIR)$Paes_ct$O \
 $(OBJDIR)$Paes_ct64$O \
 $(OBJDIR)$Paes_ct64_cbcdec$O \
 $(OBJDIR)$Paes_ct64_cbcenc$O \
 $(OBJDIR)$Paes_ct64_ctr$O \
 $(OBJDIR)$Paes_ct64_ctrcbc$O \
 $(OBJDIR)$Paes_ct64_dec$O \
 $(OBJDIR)$Paes_ct64_enc$O \
 $(OBJDIR)$Paes_ct_cbcdec$O \
 $(OBJDIR)$Paes_ct_cbcenc$O \
 $(OBJDIR)$Paes_ct_ctr$O \
 $(OBJDIR)$Paes_ct_ctrcbc$O \
 $(OBJDIR)$Paes_ct_dec$O \
 $(OBJDIR)$Paes_ct_enc$O \
 $(OBJDIR)$Paes_pwr8$O \
 $(OBJDIR)$Paes_pwr8_cbcdec$O \
 $(OBJDIR)$Paes_pwr8_cbcenc$O \
 $(OBJDIR)$Paes_pwr8_ctr$O \
 $(OBJDIR)$Paes_pwr8_ctrcbc$O \
 $(OBJDIR)$Paes_small_cbcdec$O \
 $(OBJDIR)$Paes_small_cbcenc$O \
 $(OBJDIR)$Paes_small_ctr$O \
 $(OBJDIR)$Paes_small_ctrcbc$O \
 $(OBJDIR)$Paes_small_dec$O \
 $(OBJDIR)$Paes_small_enc$O \
 $(OBJDIR)$Paes_x86ni$O \
 $(OBJDIR)$Paes_x86ni_cbcdec$O \
 $(OBJDIR)$Paes_x86ni_cbcenc$O \
 $(OBJDIR)$Paes_x86ni_ctr$O \
 $(OBJDIR)$Paes_x86ni_ctrcbc$O \
 $(OBJDIR)$Pchacha20_ct$O \
 $(OBJDIR)$Pchacha20_sse2$O \
 $(OBJDIR)$Pdes_ct$O \
 $(OBJDIR)$Pdes_ct_cbcdec$O \
 $(OBJDIR)$Pdes_ct_cbcenc$O \
 $(OBJDIR)$Pdes_support$O \
 $(OBJDIR)$Pdes_tab$O \
 $(OBJDIR)$Pdes_tab_cbcdec$O \
 $(OBJDIR)$Pdes_tab_cbcenc$O \
 $(OBJDIR)$Ppoly1305_ctmul$O \
 $(OBJDIR)$Ppoly1305_ctmul32$O \
 $(OBJDIR)$Ppoly1305_ctmulq$O \
 $(OBJDIR)$Ppoly1305_i15$O \
 $(OBJDIR)$Pasn1enc$O \
 $(OBJDIR)$Pencode_ec_pk8der$O \
 $(OBJDIR)$Pencode_ec_rawder$O \
 $(OBJDIR)$Pencode_rsa_pk8der$O \
 $(OBJDIR)$Pencode_rsa_rawder$O \
 $(OBJDIR)$Pskey_decoder$O \
 $(OBJDIR)$Px509_decoder$O \
 $(OBJDIR)$Px509_knownkey$O \
 $(OBJDIR)$Px509_minimal$O \
 $(OBJDIR)$Px509_minimal_full$O
OBJBRSSL = \
 $(OBJDIR)$Pbrssl$O \
 $(OBJDIR)$Pcerts$O \
 $(OBJDIR)$Pchain$O \
 $(OBJDIR)$Pclient$O \
 $(OBJDIR)$Perrors$O \
 $(OBJDIR)$Pfiles$O \
 $(OBJDIR)$Pimpl$O \
 $(OBJDIR)$Pkeys$O \
 $(OBJDIR)$Pnames$O \
 $(OBJDIR)$Pserver$O \
 $(OBJDIR)$Pskey$O \
 $(OBJDIR)$Psslio$O \
 $(OBJDIR)$Pta$O \
 $(OBJDIR)$Ptwrch$O \
 $(OBJDIR)$Pvector$O \
 $(OBJDIR)$Pverify$O \
 $(OBJDIR)$Pxmem$O
OBJTESTCRYPTO = \
 $(OBJDIR)$Ptest_crypto$O
OBJTESTSPEED = \
 $(OBJDIR)$Ptest_speed$O
OBJTESTX509 = \
 $(OBJDIR)$Ptest_x509$O
HEADERSPUB = inc$Pbearssl.h inc$Pbearssl_aead.h inc$Pbearssl_block.h inc$Pbearssl_ec.h inc$Pbearssl_hash.h inc$Pbearssl_hmac.h inc$Pbearssl_kdf.h inc$Pbearssl_pem.h inc$Pbearssl_prf.h inc$Pbearssl_rand.h inc$Pbearssl_rsa.h inc$Pbearssl_ssl.h inc$Pbearssl_x509.h
HEADERSPRIV = $(HEADERSPUB) src$Pconfig.h src$Pinner.h
HEADERSTOOLS = $(HEADERSPUB) tools$Pbrssl.h
T0SRC = T0$PBlobWriter.cs T0$PCPU.cs T0$PCodeElement.cs T0$PCodeElementJump.cs T0$PCodeElementUInt.cs T0$PCodeElementUIntExpr.cs T0$PCodeElementUIntInt.cs T0$PCodeElementUIntUInt.cs T0$PConstData.cs T0$POpcode.cs T0$POpcodeCall.cs T0$POpcodeConst.cs T0$POpcodeGetLocal.cs T0$POpcodeJump.cs T0$POpcodeJumpIf.cs T0$POpcodeJumpIfNot.cs T0$POpcodeJumpUncond.cs T0$POpcodePutLocal.cs T0$POpcodeRet.cs T0$PSType.cs T0$PT0Comp.cs T0$PTPointerBase.cs T0$PTPointerBlob.cs T0$PTPointerExpr.cs T0$PTPointerNull.cs T0$PTPointerXT.cs T0$PTValue.cs T0$PWord.cs T0$PWordBuilder.cs T0$PWordData.cs T0$PWordInterpreted.cs T0$PWordNative.cs
T0KERN =

all: $(STATICLIB) $(DLL) $(TOOLS) $(TESTS)

no:

lib: $(BEARSSLLIB)

dll: $(BEARSSLDLL)

tools: $(BRSSL)

tests: $(TESTCRYPTO) $(TESTSPEED) $(TESTX509)

T0: kT0

kT0: $(T0COMP) src$Pssl$Pssl_hs_common.t0 src$Pssl$Pssl_hs_client.t0 src$Pssl$Pssl_hs_server.t0 src$Px509$Pasn1.t0 src$Px509$Pskey_decoder.t0 src$Px509$Px509_decoder.t0 src$Px509$Px509_minimal.t0
	$(RUNT0COMP) -o src$Pcodec$Ppemdec -r br_pem_decoder src$Pcodec$Ppemdec.t0
	$(RUNT0COMP) -o src$Pssl$Pssl_hs_client -r br_ssl_hs_client src$Pssl$Pssl_hs_common.t0 src$Pssl$Pssl_hs_client.t0
	$(RUNT0COMP) -o src$Pssl$Pssl_hs_server -r br_ssl_hs_server src$Pssl$Pssl_hs_common.t0 src$Pssl$Pssl_hs_server.t0
	$(RUNT0COMP) -o src$Px509$Pskey_decoder -r br_skey_decoder src$Px509$Pasn1.t0 src$Px509$Pskey_decoder.t0
	$(RUNT0COMP) -o src$Px509$Px509_decoder -r br_x509_decoder src$Px509$Pasn1.t0 src$Px509$Px509_decoder.t0
	$(RUNT0COMP) -o src$Px509$Px509_minimal -r br_x509_minimal src$Px509$Pasn1.t0 src$Px509$Px509_minimal.t0

$(T0COMP): $(T0SRC) $(T0KERN)
	$(MKT0COMP)

clean:
	-$(RM) $(OBJDIR)$P*$O
	-$(RM) $(BEARSSLLIB) $(BEARSSLDLL) $(BRSSL) $(TESTCRYPTO) $(TESTSPEED) $(TESTX509)

$(OBJDIR):
	-$(MKDIR) $(OBJDIR)

$(BEARSSLLIB): $(OBJDIR) $(OBJ)
	$(AR) $(ARFLAGS) $(AROUT)$(BEARSSLLIB) $(OBJ)

$(BEARSSLDLL): $(OBJDIR) $(OBJ)
	$(LDDLL) $(LDDLLFLAGS) $(LDDLLOUT)$(BEARSSLDLL) $(OBJ)

$(BRSSL): $(BEARSSLLIB) $(OBJBRSSL)
	$(LD) $(LDFLAGS) $(LDOUT)$(BRSSL) $(OBJBRSSL) $(BEARSSLLIB)

$(TESTCRYPTO): $(BEARSSLLIB) $(OBJTESTCRYPTO)
	$(LD) $(LDFLAGS) $(LDOUT)$(TESTCRYPTO) $(OBJTESTCRYPTO) $(BEARSSLLIB)

$(TESTSPEED): $(BEARSSLLIB) $(OBJTESTSPEED)
	$(LD) $(LDFLAGS) $(LDOUT)$(TESTSPEED) $(OBJTESTSPEED) $(BEARSSLLIB)

$(TESTX509): $(BEARSSLLIB) $(OBJTESTX509)
	$(LD) $(LDFLAGS) $(LDOUT)$(TESTX509) $(OBJTESTX509) $(BEARSSLLIB)

$(OBJDIR)$Psettings$O: src$Psettings.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Psettings$O src$Psettings.c

$(OBJDIR)$Pccm$O: src$Paead$Pccm.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pccm$O src$Paead$Pccm.c

$(OBJDIR)$Peax$O: src$Paead$Peax.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Peax$O src$Paead$Peax.c

$(OBJDIR)$Pgcm$O: src$Paead$Pgcm.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pgcm$O src$Paead$Pgcm.c

$(OBJDIR)$Pccopy$O: src$Pcodec$Pccopy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pccopy$O src$Pcodec$Pccopy.c

$(OBJDIR)$Pdec16be$O: src$Pcodec$Pdec16be.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdec16be$O src$Pcodec$Pdec16be.c

$(OBJDIR)$Pdec16le$O: src$Pcodec$Pdec16le.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdec16le$O src$Pcodec$Pdec16le.c

$(OBJDIR)$Pdec32be$O: src$Pcodec$Pdec32be.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdec32be$O src$Pcodec$Pdec32be.c

$(OBJDIR)$Pdec32le$O: src$Pcodec$Pdec32le.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdec32le$O src$Pcodec$Pdec32le.c

$(OBJDIR)$Pdec64be$O: src$Pcodec$Pdec64be.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdec64be$O src$Pcodec$Pdec64be.c

$(OBJDIR)$Pdec64le$O: src$Pcodec$Pdec64le.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdec64le$O src$Pcodec$Pdec64le.c

$(OBJDIR)$Penc16be$O: src$Pcodec$Penc16be.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Penc16be$O src$Pcodec$Penc16be.c

$(OBJDIR)$Penc16le$O: src$Pcodec$Penc16le.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Penc16le$O src$Pcodec$Penc16le.c

$(OBJDIR)$Penc32be$O: src$Pcodec$Penc32be.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Penc32be$O src$Pcodec$Penc32be.c

$(OBJDIR)$Penc32le$O: src$Pcodec$Penc32le.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Penc32le$O src$Pcodec$Penc32le.c

$(OBJDIR)$Penc64be$O: src$Pcodec$Penc64be.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Penc64be$O src$Pcodec$Penc64be.c

$(OBJDIR)$Penc64le$O: src$Pcodec$Penc64le.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Penc64le$O src$Pcodec$Penc64le.c

$(OBJDIR)$Ppemdec$O: src$Pcodec$Ppemdec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Ppemdec$O src$Pcodec$Ppemdec.c

$(OBJDIR)$Ppemenc$O: src$Pcodec$Ppemenc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Ppemenc$O src$Pcodec$Ppemenc.c

$(OBJDIR)$Pec_all_m15$O: src$Pec$Pec_all_m15.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_all_m15$O src$Pec$Pec_all_m15.c

$(OBJDIR)$Pec_all_m31$O: src$Pec$Pec_all_m31.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_all_m31$O src$Pec$Pec_all_m31.c

$(OBJDIR)$Pec_c25519_i15$O: src$Pec$Pec_c25519_i15.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_c25519_i15$O src$Pec$Pec_c25519_i15.c

$(OBJDIR)$Pec_c25519_i31$O: src$Pec$Pec_c25519_i31.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_c25519_i31$O src$Pec$Pec_c25519_i31.c

$(OBJDIR)$Pec_c25519_m15$O: src$Pec$Pec_c25519_m15.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_c25519_m15$O src$Pec$Pec_c25519_m15.c

$(OBJDIR)$Pec_c25519_m31$O: src$Pec$Pec_c25519_m31.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_c25519_m31$O src$Pec$Pec_c25519_m31.c

$(OBJDIR)$Pec_c25519_m62$O: src$Pec$Pec_c25519_m62.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_c25519_m62$O src$Pec$Pec_c25519_m62.c

$(OBJDIR)$Pec_c25519_m64$O: src$Pec$Pec_c25519_m64.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_c25519_m64$O src$Pec$Pec_c25519_m64.c

$(OBJDIR)$Pec_curve25519$O: src$Pec$Pec_curve25519.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_curve25519$O src$Pec$Pec_curve25519.c

$(OBJDIR)$Pec_default$O: src$Pec$Pec_default.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_default$O src$Pec$Pec_default.c

$(OBJDIR)$Pec_keygen$O: src$Pec$Pec_keygen.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_keygen$O src$Pec$Pec_keygen.c

$(OBJDIR)$Pec_p256_m15$O: src$Pec$Pec_p256_m15.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_p256_m15$O src$Pec$Pec_p256_m15.c

$(OBJDIR)$Pec_p256_m31$O: src$Pec$Pec_p256_m31.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_p256_m31$O src$Pec$Pec_p256_m31.c

$(OBJDIR)$Pec_p256_m62$O: src$Pec$Pec_p256_m62.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_p256_m62$O src$Pec$Pec_p256_m62.c

$(OBJDIR)$Pec_p256_m64$O: src$Pec$Pec_p256_m64.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_p256_m64$O src$Pec$Pec_p256_m64.c

$(OBJDIR)$Pec_prime_i15$O: src$Pec$Pec_prime_i15.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_prime_i15$O src$Pec$Pec_prime_i15.c

$(OBJDIR)$Pec_prime_i31$O: src$Pec$Pec_prime_i31.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_prime_i31$O src$Pec$Pec_prime_i31.c

$(OBJDIR)$Pec_pubkey$O: src$Pec$Pec_pubkey.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_pubkey$O src$Pec$Pec_pubkey.c

$(OBJDIR)$Pec_secp256r1$O: src$Pec$Pec_secp256r1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_secp256r1$O src$Pec$Pec_secp256r1.c

$(OBJDIR)$Pec_secp384r1$O: src$Pec$Pec_secp384r1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_secp384r1$O src$Pec$Pec_secp384r1.c

$(OBJDIR)$Pec_secp521r1$O: src$Pec$Pec_secp521r1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pec_secp521r1$O src$Pec$Pec_secp521r1.c

$(OBJDIR)$Pecdsa_atr$O: src$Pec$Pecdsa_atr.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_atr$O src$Pec$Pecdsa_atr.c

$(OBJDIR)$Pecdsa_default_sign_asn1$O: src$Pec$Pecdsa_default_sign_asn1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_default_sign_asn1$O src$Pec$Pecdsa_default_sign_asn1.c

$(OBJDIR)$Pecdsa_default_sign_raw$O: src$Pec$Pecdsa_default_sign_raw.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_default_sign_raw$O src$Pec$Pecdsa_default_sign_raw.c

$(OBJDIR)$Pecdsa_default_vrfy_asn1$O: src$Pec$Pecdsa_default_vrfy_asn1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_default_vrfy_asn1$O src$Pec$Pecdsa_default_vrfy_asn1.c

$(OBJDIR)$Pecdsa_default_vrfy_raw$O: src$Pec$Pecdsa_default_vrfy_raw.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_default_vrfy_raw$O src$Pec$Pecdsa_default_vrfy_raw.c

$(OBJDIR)$Pecdsa_i15_bits$O: src$Pec$Pecdsa_i15_bits.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_i15_bits$O src$Pec$Pecdsa_i15_bits.c

$(OBJDIR)$Pecdsa_i15_sign_asn1$O: src$Pec$Pecdsa_i15_sign_asn1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_i15_sign_asn1$O src$Pec$Pecdsa_i15_sign_asn1.c

$(OBJDIR)$Pecdsa_i15_sign_raw$O: src$Pec$Pecdsa_i15_sign_raw.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_i15_sign_raw$O src$Pec$Pecdsa_i15_sign_raw.c

$(OBJDIR)$Pecdsa_i15_vrfy_asn1$O: src$Pec$Pecdsa_i15_vrfy_asn1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_i15_vrfy_asn1$O src$Pec$Pecdsa_i15_vrfy_asn1.c

$(OBJDIR)$Pecdsa_i15_vrfy_raw$O: src$Pec$Pecdsa_i15_vrfy_raw.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_i15_vrfy_raw$O src$Pec$Pecdsa_i15_vrfy_raw.c

$(OBJDIR)$Pecdsa_i31_bits$O: src$Pec$Pecdsa_i31_bits.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_i31_bits$O src$Pec$Pecdsa_i31_bits.c

$(OBJDIR)$Pecdsa_i31_sign_asn1$O: src$Pec$Pecdsa_i31_sign_asn1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_i31_sign_asn1$O src$Pec$Pecdsa_i31_sign_asn1.c

$(OBJDIR)$Pecdsa_i31_sign_raw$O: src$Pec$Pecdsa_i31_sign_raw.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_i31_sign_raw$O src$Pec$Pecdsa_i31_sign_raw.c

$(OBJDIR)$Pecdsa_i31_vrfy_asn1$O: src$Pec$Pecdsa_i31_vrfy_asn1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_i31_vrfy_asn1$O src$Pec$Pecdsa_i31_vrfy_asn1.c

$(OBJDIR)$Pecdsa_i31_vrfy_raw$O: src$Pec$Pecdsa_i31_vrfy_raw.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_i31_vrfy_raw$O src$Pec$Pecdsa_i31_vrfy_raw.c

$(OBJDIR)$Pecdsa_rta$O: src$Pec$Pecdsa_rta.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pecdsa_rta$O src$Pec$Pecdsa_rta.c

$(OBJDIR)$Pdig_oid$O: src$Phash$Pdig_oid.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdig_oid$O src$Phash$Pdig_oid.c

$(OBJDIR)$Pdig_size$O: src$Phash$Pdig_size.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdig_size$O src$Phash$Pdig_size.c

$(OBJDIR)$Pghash_ctmul$O: src$Phash$Pghash_ctmul.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pghash_ctmul$O src$Phash$Pghash_ctmul.c

$(OBJDIR)$Pghash_ctmul32$O: src$Phash$Pghash_ctmul32.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pghash_ctmul32$O src$Phash$Pghash_ctmul32.c

$(OBJDIR)$Pghash_ctmul64$O: src$Phash$Pghash_ctmul64.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pghash_ctmul64$O src$Phash$Pghash_ctmul64.c

$(OBJDIR)$Pghash_pclmul$O: src$Phash$Pghash_pclmul.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pghash_pclmul$O src$Phash$Pghash_pclmul.c

$(OBJDIR)$Pghash_pwr8$O: src$Phash$Pghash_pwr8.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pghash_pwr8$O src$Phash$Pghash_pwr8.c

$(OBJDIR)$Pmd5$O: src$Phash$Pmd5.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pmd5$O src$Phash$Pmd5.c

$(OBJDIR)$Pmd5sha1$O: src$Phash$Pmd5sha1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pmd5sha1$O src$Phash$Pmd5sha1.c

$(OBJDIR)$Pmgf1$O: src$Phash$Pmgf1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pmgf1$O src$Phash$Pmgf1.c

$(OBJDIR)$Pmultihash$O: src$Phash$Pmultihash.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pmultihash$O src$Phash$Pmultihash.c

$(OBJDIR)$Psha1$O: src$Phash$Psha1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Psha1$O src$Phash$Psha1.c

$(OBJDIR)$Psha2big$O: src$Phash$Psha2big.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Psha2big$O src$Phash$Psha2big.c

$(OBJDIR)$Psha2small$O: src$Phash$Psha2small.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Psha2small$O src$Phash$Psha2small.c

$(OBJDIR)$Pi15_add$O: src$Pint$Pi15_add.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_add$O src$Pint$Pi15_add.c

$(OBJDIR)$Pi15_bitlen$O: src$Pint$Pi15_bitlen.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_bitlen$O src$Pint$Pi15_bitlen.c

$(OBJDIR)$Pi15_decmod$O: src$Pint$Pi15_decmod.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_decmod$O src$Pint$Pi15_decmod.c

$(OBJDIR)$Pi15_decode$O: src$Pint$Pi15_decode.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_decode$O src$Pint$Pi15_decode.c

$(OBJDIR)$Pi15_decred$O: src$Pint$Pi15_decred.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_decred$O src$Pint$Pi15_decred.c

$(OBJDIR)$Pi15_encode$O: src$Pint$Pi15_encode.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_encode$O src$Pint$Pi15_encode.c

$(OBJDIR)$Pi15_fmont$O: src$Pint$Pi15_fmont.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_fmont$O src$Pint$Pi15_fmont.c

$(OBJDIR)$Pi15_iszero$O: src$Pint$Pi15_iszero.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_iszero$O src$Pint$Pi15_iszero.c

$(OBJDIR)$Pi15_moddiv$O: src$Pint$Pi15_moddiv.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_moddiv$O src$Pint$Pi15_moddiv.c

$(OBJDIR)$Pi15_modpow$O: src$Pint$Pi15_modpow.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_modpow$O src$Pint$Pi15_modpow.c

$(OBJDIR)$Pi15_modpow2$O: src$Pint$Pi15_modpow2.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_modpow2$O src$Pint$Pi15_modpow2.c

$(OBJDIR)$Pi15_montmul$O: src$Pint$Pi15_montmul.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_montmul$O src$Pint$Pi15_montmul.c

$(OBJDIR)$Pi15_mulacc$O: src$Pint$Pi15_mulacc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_mulacc$O src$Pint$Pi15_mulacc.c

$(OBJDIR)$Pi15_muladd$O: src$Pint$Pi15_muladd.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_muladd$O src$Pint$Pi15_muladd.c

$(OBJDIR)$Pi15_ninv15$O: src$Pint$Pi15_ninv15.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_ninv15$O src$Pint$Pi15_ninv15.c

$(OBJDIR)$Pi15_reduce$O: src$Pint$Pi15_reduce.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_reduce$O src$Pint$Pi15_reduce.c

$(OBJDIR)$Pi15_rshift$O: src$Pint$Pi15_rshift.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_rshift$O src$Pint$Pi15_rshift.c

$(OBJDIR)$Pi15_sub$O: src$Pint$Pi15_sub.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_sub$O src$Pint$Pi15_sub.c

$(OBJDIR)$Pi15_tmont$O: src$Pint$Pi15_tmont.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi15_tmont$O src$Pint$Pi15_tmont.c

$(OBJDIR)$Pi31_add$O: src$Pint$Pi31_add.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_add$O src$Pint$Pi31_add.c

$(OBJDIR)$Pi31_bitlen$O: src$Pint$Pi31_bitlen.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_bitlen$O src$Pint$Pi31_bitlen.c

$(OBJDIR)$Pi31_decmod$O: src$Pint$Pi31_decmod.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_decmod$O src$Pint$Pi31_decmod.c

$(OBJDIR)$Pi31_decode$O: src$Pint$Pi31_decode.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_decode$O src$Pint$Pi31_decode.c

$(OBJDIR)$Pi31_decred$O: src$Pint$Pi31_decred.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_decred$O src$Pint$Pi31_decred.c

$(OBJDIR)$Pi31_encode$O: src$Pint$Pi31_encode.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_encode$O src$Pint$Pi31_encode.c

$(OBJDIR)$Pi31_fmont$O: src$Pint$Pi31_fmont.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_fmont$O src$Pint$Pi31_fmont.c

$(OBJDIR)$Pi31_iszero$O: src$Pint$Pi31_iszero.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_iszero$O src$Pint$Pi31_iszero.c

$(OBJDIR)$Pi31_moddiv$O: src$Pint$Pi31_moddiv.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_moddiv$O src$Pint$Pi31_moddiv.c

$(OBJDIR)$Pi31_modpow$O: src$Pint$Pi31_modpow.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_modpow$O src$Pint$Pi31_modpow.c

$(OBJDIR)$Pi31_modpow2$O: src$Pint$Pi31_modpow2.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_modpow2$O src$Pint$Pi31_modpow2.c

$(OBJDIR)$Pi31_montmul$O: src$Pint$Pi31_montmul.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_montmul$O src$Pint$Pi31_montmul.c

$(OBJDIR)$Pi31_mulacc$O: src$Pint$Pi31_mulacc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_mulacc$O src$Pint$Pi31_mulacc.c

$(OBJDIR)$Pi31_muladd$O: src$Pint$Pi31_muladd.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_muladd$O src$Pint$Pi31_muladd.c

$(OBJDIR)$Pi31_ninv31$O: src$Pint$Pi31_ninv31.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_ninv31$O src$Pint$Pi31_ninv31.c

$(OBJDIR)$Pi31_reduce$O: src$Pint$Pi31_reduce.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_reduce$O src$Pint$Pi31_reduce.c

$(OBJDIR)$Pi31_rshift$O: src$Pint$Pi31_rshift.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_rshift$O src$Pint$Pi31_rshift.c

$(OBJDIR)$Pi31_sub$O: src$Pint$Pi31_sub.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_sub$O src$Pint$Pi31_sub.c

$(OBJDIR)$Pi31_tmont$O: src$Pint$Pi31_tmont.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi31_tmont$O src$Pint$Pi31_tmont.c

$(OBJDIR)$Pi32_add$O: src$Pint$Pi32_add.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_add$O src$Pint$Pi32_add.c

$(OBJDIR)$Pi32_bitlen$O: src$Pint$Pi32_bitlen.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_bitlen$O src$Pint$Pi32_bitlen.c

$(OBJDIR)$Pi32_decmod$O: src$Pint$Pi32_decmod.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_decmod$O src$Pint$Pi32_decmod.c

$(OBJDIR)$Pi32_decode$O: src$Pint$Pi32_decode.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_decode$O src$Pint$Pi32_decode.c

$(OBJDIR)$Pi32_decred$O: src$Pint$Pi32_decred.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_decred$O src$Pint$Pi32_decred.c

$(OBJDIR)$Pi32_div32$O: src$Pint$Pi32_div32.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_div32$O src$Pint$Pi32_div32.c

$(OBJDIR)$Pi32_encode$O: src$Pint$Pi32_encode.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_encode$O src$Pint$Pi32_encode.c

$(OBJDIR)$Pi32_fmont$O: src$Pint$Pi32_fmont.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_fmont$O src$Pint$Pi32_fmont.c

$(OBJDIR)$Pi32_iszero$O: src$Pint$Pi32_iszero.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_iszero$O src$Pint$Pi32_iszero.c

$(OBJDIR)$Pi32_modpow$O: src$Pint$Pi32_modpow.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_modpow$O src$Pint$Pi32_modpow.c

$(OBJDIR)$Pi32_montmul$O: src$Pint$Pi32_montmul.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_montmul$O src$Pint$Pi32_montmul.c

$(OBJDIR)$Pi32_mulacc$O: src$Pint$Pi32_mulacc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_mulacc$O src$Pint$Pi32_mulacc.c

$(OBJDIR)$Pi32_muladd$O: src$Pint$Pi32_muladd.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_muladd$O src$Pint$Pi32_muladd.c

$(OBJDIR)$Pi32_ninv32$O: src$Pint$Pi32_ninv32.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_ninv32$O src$Pint$Pi32_ninv32.c

$(OBJDIR)$Pi32_reduce$O: src$Pint$Pi32_reduce.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_reduce$O src$Pint$Pi32_reduce.c

$(OBJDIR)$Pi32_sub$O: src$Pint$Pi32_sub.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_sub$O src$Pint$Pi32_sub.c

$(OBJDIR)$Pi32_tmont$O: src$Pint$Pi32_tmont.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi32_tmont$O src$Pint$Pi32_tmont.c

$(OBJDIR)$Pi62_modpow2$O: src$Pint$Pi62_modpow2.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pi62_modpow2$O src$Pint$Pi62_modpow2.c

$(OBJDIR)$Phkdf$O: src$Pkdf$Phkdf.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Phkdf$O src$Pkdf$Phkdf.c

$(OBJDIR)$Pshake$O: src$Pkdf$Pshake.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pshake$O src$Pkdf$Pshake.c

$(OBJDIR)$Phmac$O: src$Pmac$Phmac.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Phmac$O src$Pmac$Phmac.c

$(OBJDIR)$Phmac_ct$O: src$Pmac$Phmac_ct.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Phmac_ct$O src$Pmac$Phmac_ct.c

$(OBJDIR)$Paesctr_drbg$O: src$Prand$Paesctr_drbg.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paesctr_drbg$O src$Prand$Paesctr_drbg.c

$(OBJDIR)$Phmac_drbg$O: src$Prand$Phmac_drbg.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Phmac_drbg$O src$Prand$Phmac_drbg.c

$(OBJDIR)$Psysrng$O: src$Prand$Psysrng.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Psysrng$O src$Prand$Psysrng.c

$(OBJDIR)$Prsa_default_keygen$O: src$Prsa$Prsa_default_keygen.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_keygen$O src$Prsa$Prsa_default_keygen.c

$(OBJDIR)$Prsa_default_modulus$O: src$Prsa$Prsa_default_modulus.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_modulus$O src$Prsa$Prsa_default_modulus.c

$(OBJDIR)$Prsa_default_oaep_decrypt$O: src$Prsa$Prsa_default_oaep_decrypt.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_oaep_decrypt$O src$Prsa$Prsa_default_oaep_decrypt.c

$(OBJDIR)$Prsa_default_oaep_encrypt$O: src$Prsa$Prsa_default_oaep_encrypt.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_oaep_encrypt$O src$Prsa$Prsa_default_oaep_encrypt.c

$(OBJDIR)$Prsa_default_pkcs1_sign$O: src$Prsa$Prsa_default_pkcs1_sign.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_pkcs1_sign$O src$Prsa$Prsa_default_pkcs1_sign.c

$(OBJDIR)$Prsa_default_pkcs1_vrfy$O: src$Prsa$Prsa_default_pkcs1_vrfy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_pkcs1_vrfy$O src$Prsa$Prsa_default_pkcs1_vrfy.c

$(OBJDIR)$Prsa_default_priv$O: src$Prsa$Prsa_default_priv.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_priv$O src$Prsa$Prsa_default_priv.c

$(OBJDIR)$Prsa_default_privexp$O: src$Prsa$Prsa_default_privexp.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_privexp$O src$Prsa$Prsa_default_privexp.c

$(OBJDIR)$Prsa_default_pss_sign$O: src$Prsa$Prsa_default_pss_sign.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_pss_sign$O src$Prsa$Prsa_default_pss_sign.c

$(OBJDIR)$Prsa_default_pss_vrfy$O: src$Prsa$Prsa_default_pss_vrfy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_pss_vrfy$O src$Prsa$Prsa_default_pss_vrfy.c

$(OBJDIR)$Prsa_default_pub$O: src$Prsa$Prsa_default_pub.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_pub$O src$Prsa$Prsa_default_pub.c

$(OBJDIR)$Prsa_default_pubexp$O: src$Prsa$Prsa_default_pubexp.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_default_pubexp$O src$Prsa$Prsa_default_pubexp.c

$(OBJDIR)$Prsa_i15_keygen$O: src$Prsa$Prsa_i15_keygen.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_keygen$O src$Prsa$Prsa_i15_keygen.c

$(OBJDIR)$Prsa_i15_modulus$O: src$Prsa$Prsa_i15_modulus.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_modulus$O src$Prsa$Prsa_i15_modulus.c

$(OBJDIR)$Prsa_i15_oaep_decrypt$O: src$Prsa$Prsa_i15_oaep_decrypt.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_oaep_decrypt$O src$Prsa$Prsa_i15_oaep_decrypt.c

$(OBJDIR)$Prsa_i15_oaep_encrypt$O: src$Prsa$Prsa_i15_oaep_encrypt.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_oaep_encrypt$O src$Prsa$Prsa_i15_oaep_encrypt.c

$(OBJDIR)$Prsa_i15_pkcs1_sign$O: src$Prsa$Prsa_i15_pkcs1_sign.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_pkcs1_sign$O src$Prsa$Prsa_i15_pkcs1_sign.c

$(OBJDIR)$Prsa_i15_pkcs1_vrfy$O: src$Prsa$Prsa_i15_pkcs1_vrfy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_pkcs1_vrfy$O src$Prsa$Prsa_i15_pkcs1_vrfy.c

$(OBJDIR)$Prsa_i15_priv$O: src$Prsa$Prsa_i15_priv.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_priv$O src$Prsa$Prsa_i15_priv.c

$(OBJDIR)$Prsa_i15_privexp$O: src$Prsa$Prsa_i15_privexp.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_privexp$O src$Prsa$Prsa_i15_privexp.c

$(OBJDIR)$Prsa_i15_pss_sign$O: src$Prsa$Prsa_i15_pss_sign.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_pss_sign$O src$Prsa$Prsa_i15_pss_sign.c

$(OBJDIR)$Prsa_i15_pss_vrfy$O: src$Prsa$Prsa_i15_pss_vrfy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_pss_vrfy$O src$Prsa$Prsa_i15_pss_vrfy.c

$(OBJDIR)$Prsa_i15_pub$O: src$Prsa$Prsa_i15_pub.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_pub$O src$Prsa$Prsa_i15_pub.c

$(OBJDIR)$Prsa_i15_pubexp$O: src$Prsa$Prsa_i15_pubexp.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i15_pubexp$O src$Prsa$Prsa_i15_pubexp.c

$(OBJDIR)$Prsa_i31_keygen$O: src$Prsa$Prsa_i31_keygen.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_keygen$O src$Prsa$Prsa_i31_keygen.c

$(OBJDIR)$Prsa_i31_keygen_inner$O: src$Prsa$Prsa_i31_keygen_inner.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_keygen_inner$O src$Prsa$Prsa_i31_keygen_inner.c

$(OBJDIR)$Prsa_i31_modulus$O: src$Prsa$Prsa_i31_modulus.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_modulus$O src$Prsa$Prsa_i31_modulus.c

$(OBJDIR)$Prsa_i31_oaep_decrypt$O: src$Prsa$Prsa_i31_oaep_decrypt.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_oaep_decrypt$O src$Prsa$Prsa_i31_oaep_decrypt.c

$(OBJDIR)$Prsa_i31_oaep_encrypt$O: src$Prsa$Prsa_i31_oaep_encrypt.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_oaep_encrypt$O src$Prsa$Prsa_i31_oaep_encrypt.c

$(OBJDIR)$Prsa_i31_pkcs1_sign$O: src$Prsa$Prsa_i31_pkcs1_sign.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_pkcs1_sign$O src$Prsa$Prsa_i31_pkcs1_sign.c

$(OBJDIR)$Prsa_i31_pkcs1_vrfy$O: src$Prsa$Prsa_i31_pkcs1_vrfy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_pkcs1_vrfy$O src$Prsa$Prsa_i31_pkcs1_vrfy.c

$(OBJDIR)$Prsa_i31_priv$O: src$Prsa$Prsa_i31_priv.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_priv$O src$Prsa$Prsa_i31_priv.c

$(OBJDIR)$Prsa_i31_privexp$O: src$Prsa$Prsa_i31_privexp.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_privexp$O src$Prsa$Prsa_i31_privexp.c

$(OBJDIR)$Prsa_i31_pss_sign$O: src$Prsa$Prsa_i31_pss_sign.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_pss_sign$O src$Prsa$Prsa_i31_pss_sign.c

$(OBJDIR)$Prsa_i31_pss_vrfy$O: src$Prsa$Prsa_i31_pss_vrfy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_pss_vrfy$O src$Prsa$Prsa_i31_pss_vrfy.c

$(OBJDIR)$Prsa_i31_pub$O: src$Prsa$Prsa_i31_pub.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_pub$O src$Prsa$Prsa_i31_pub.c

$(OBJDIR)$Prsa_i31_pubexp$O: src$Prsa$Prsa_i31_pubexp.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i31_pubexp$O src$Prsa$Prsa_i31_pubexp.c

$(OBJDIR)$Prsa_i32_oaep_decrypt$O: src$Prsa$Prsa_i32_oaep_decrypt.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i32_oaep_decrypt$O src$Prsa$Prsa_i32_oaep_decrypt.c

$(OBJDIR)$Prsa_i32_oaep_encrypt$O: src$Prsa$Prsa_i32_oaep_encrypt.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i32_oaep_encrypt$O src$Prsa$Prsa_i32_oaep_encrypt.c

$(OBJDIR)$Prsa_i32_pkcs1_sign$O: src$Prsa$Prsa_i32_pkcs1_sign.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i32_pkcs1_sign$O src$Prsa$Prsa_i32_pkcs1_sign.c

$(OBJDIR)$Prsa_i32_pkcs1_vrfy$O: src$Prsa$Prsa_i32_pkcs1_vrfy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i32_pkcs1_vrfy$O src$Prsa$Prsa_i32_pkcs1_vrfy.c

$(OBJDIR)$Prsa_i32_priv$O: src$Prsa$Prsa_i32_priv.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i32_priv$O src$Prsa$Prsa_i32_priv.c

$(OBJDIR)$Prsa_i32_pss_sign$O: src$Prsa$Prsa_i32_pss_sign.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i32_pss_sign$O src$Prsa$Prsa_i32_pss_sign.c

$(OBJDIR)$Prsa_i32_pss_vrfy$O: src$Prsa$Prsa_i32_pss_vrfy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i32_pss_vrfy$O src$Prsa$Prsa_i32_pss_vrfy.c

$(OBJDIR)$Prsa_i32_pub$O: src$Prsa$Prsa_i32_pub.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i32_pub$O src$Prsa$Prsa_i32_pub.c

$(OBJDIR)$Prsa_i62_keygen$O: src$Prsa$Prsa_i62_keygen.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i62_keygen$O src$Prsa$Prsa_i62_keygen.c

$(OBJDIR)$Prsa_i62_oaep_decrypt$O: src$Prsa$Prsa_i62_oaep_decrypt.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i62_oaep_decrypt$O src$Prsa$Prsa_i62_oaep_decrypt.c

$(OBJDIR)$Prsa_i62_oaep_encrypt$O: src$Prsa$Prsa_i62_oaep_encrypt.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i62_oaep_encrypt$O src$Prsa$Prsa_i62_oaep_encrypt.c

$(OBJDIR)$Prsa_i62_pkcs1_sign$O: src$Prsa$Prsa_i62_pkcs1_sign.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i62_pkcs1_sign$O src$Prsa$Prsa_i62_pkcs1_sign.c

$(OBJDIR)$Prsa_i62_pkcs1_vrfy$O: src$Prsa$Prsa_i62_pkcs1_vrfy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i62_pkcs1_vrfy$O src$Prsa$Prsa_i62_pkcs1_vrfy.c

$(OBJDIR)$Prsa_i62_priv$O: src$Prsa$Prsa_i62_priv.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i62_priv$O src$Prsa$Prsa_i62_priv.c

$(OBJDIR)$Prsa_i62_pss_sign$O: src$Prsa$Prsa_i62_pss_sign.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i62_pss_sign$O src$Prsa$Prsa_i62_pss_sign.c

$(OBJDIR)$Prsa_i62_pss_vrfy$O: src$Prsa$Prsa_i62_pss_vrfy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i62_pss_vrfy$O src$Prsa$Prsa_i62_pss_vrfy.c

$(OBJDIR)$Prsa_i62_pub$O: src$Prsa$Prsa_i62_pub.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_i62_pub$O src$Prsa$Prsa_i62_pub.c

$(OBJDIR)$Prsa_oaep_pad$O: src$Prsa$Prsa_oaep_pad.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_oaep_pad$O src$Prsa$Prsa_oaep_pad.c

$(OBJDIR)$Prsa_oaep_unpad$O: src$Prsa$Prsa_oaep_unpad.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_oaep_unpad$O src$Prsa$Prsa_oaep_unpad.c

$(OBJDIR)$Prsa_pkcs1_sig_pad$O: src$Prsa$Prsa_pkcs1_sig_pad.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_pkcs1_sig_pad$O src$Prsa$Prsa_pkcs1_sig_pad.c

$(OBJDIR)$Prsa_pkcs1_sig_unpad$O: src$Prsa$Prsa_pkcs1_sig_unpad.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_pkcs1_sig_unpad$O src$Prsa$Prsa_pkcs1_sig_unpad.c

$(OBJDIR)$Prsa_pss_sig_pad$O: src$Prsa$Prsa_pss_sig_pad.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_pss_sig_pad$O src$Prsa$Prsa_pss_sig_pad.c

$(OBJDIR)$Prsa_pss_sig_unpad$O: src$Prsa$Prsa_pss_sig_unpad.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_pss_sig_unpad$O src$Prsa$Prsa_pss_sig_unpad.c

$(OBJDIR)$Prsa_ssl_decrypt$O: src$Prsa$Prsa_ssl_decrypt.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Prsa_ssl_decrypt$O src$Prsa$Prsa_ssl_decrypt.c

$(OBJDIR)$Pprf$O: src$Pssl$Pprf.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pprf$O src$Pssl$Pprf.c

$(OBJDIR)$Pprf_md5sha1$O: src$Pssl$Pprf_md5sha1.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pprf_md5sha1$O src$Pssl$Pprf_md5sha1.c

$(OBJDIR)$Pprf_sha256$O: src$Pssl$Pprf_sha256.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pprf_sha256$O src$Pssl$Pprf_sha256.c

$(OBJDIR)$Pprf_sha384$O: src$Pssl$Pprf_sha384.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pprf_sha384$O src$Pssl$Pprf_sha384.c

$(OBJDIR)$Pssl_ccert_single_ec$O: src$Pssl$Pssl_ccert_single_ec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_ccert_single_ec$O src$Pssl$Pssl_ccert_single_ec.c

$(OBJDIR)$Pssl_ccert_single_rsa$O: src$Pssl$Pssl_ccert_single_rsa.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_ccert_single_rsa$O src$Pssl$Pssl_ccert_single_rsa.c

$(OBJDIR)$Pssl_client$O: src$Pssl$Pssl_client.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_client$O src$Pssl$Pssl_client.c

$(OBJDIR)$Pssl_client_default_rsapub$O: src$Pssl$Pssl_client_default_rsapub.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_client_default_rsapub$O src$Pssl$Pssl_client_default_rsapub.c

$(OBJDIR)$Pssl_client_full$O: src$Pssl$Pssl_client_full.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_client_full$O src$Pssl$Pssl_client_full.c

$(OBJDIR)$Pssl_engine$O: src$Pssl$Pssl_engine.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_engine$O src$Pssl$Pssl_engine.c

$(OBJDIR)$Pssl_engine_default_aescbc$O: src$Pssl$Pssl_engine_default_aescbc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_engine_default_aescbc$O src$Pssl$Pssl_engine_default_aescbc.c

$(OBJDIR)$Pssl_engine_default_aesccm$O: src$Pssl$Pssl_engine_default_aesccm.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_engine_default_aesccm$O src$Pssl$Pssl_engine_default_aesccm.c

$(OBJDIR)$Pssl_engine_default_aesgcm$O: src$Pssl$Pssl_engine_default_aesgcm.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_engine_default_aesgcm$O src$Pssl$Pssl_engine_default_aesgcm.c

$(OBJDIR)$Pssl_engine_default_chapol$O: src$Pssl$Pssl_engine_default_chapol.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_engine_default_chapol$O src$Pssl$Pssl_engine_default_chapol.c

$(OBJDIR)$Pssl_engine_default_descbc$O: src$Pssl$Pssl_engine_default_descbc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_engine_default_descbc$O src$Pssl$Pssl_engine_default_descbc.c

$(OBJDIR)$Pssl_engine_default_ec$O: src$Pssl$Pssl_engine_default_ec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_engine_default_ec$O src$Pssl$Pssl_engine_default_ec.c

$(OBJDIR)$Pssl_engine_default_ecdsa$O: src$Pssl$Pssl_engine_default_ecdsa.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_engine_default_ecdsa$O src$Pssl$Pssl_engine_default_ecdsa.c

$(OBJDIR)$Pssl_engine_default_rsavrfy$O: src$Pssl$Pssl_engine_default_rsavrfy.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_engine_default_rsavrfy$O src$Pssl$Pssl_engine_default_rsavrfy.c

$(OBJDIR)$Pssl_hashes$O: src$Pssl$Pssl_hashes.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_hashes$O src$Pssl$Pssl_hashes.c

$(OBJDIR)$Pssl_hs_client$O: src$Pssl$Pssl_hs_client.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_hs_client$O src$Pssl$Pssl_hs_client.c

$(OBJDIR)$Pssl_hs_server$O: src$Pssl$Pssl_hs_server.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_hs_server$O src$Pssl$Pssl_hs_server.c

$(OBJDIR)$Pssl_io$O: src$Pssl$Pssl_io.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_io$O src$Pssl$Pssl_io.c

$(OBJDIR)$Pssl_keyexport$O: src$Pssl$Pssl_keyexport.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_keyexport$O src$Pssl$Pssl_keyexport.c

$(OBJDIR)$Pssl_lru$O: src$Pssl$Pssl_lru.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_lru$O src$Pssl$Pssl_lru.c

$(OBJDIR)$Pssl_rec_cbc$O: src$Pssl$Pssl_rec_cbc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_rec_cbc$O src$Pssl$Pssl_rec_cbc.c

$(OBJDIR)$Pssl_rec_ccm$O: src$Pssl$Pssl_rec_ccm.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_rec_ccm$O src$Pssl$Pssl_rec_ccm.c

$(OBJDIR)$Pssl_rec_chapol$O: src$Pssl$Pssl_rec_chapol.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_rec_chapol$O src$Pssl$Pssl_rec_chapol.c

$(OBJDIR)$Pssl_rec_gcm$O: src$Pssl$Pssl_rec_gcm.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_rec_gcm$O src$Pssl$Pssl_rec_gcm.c

$(OBJDIR)$Pssl_scert_single_ec$O: src$Pssl$Pssl_scert_single_ec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_scert_single_ec$O src$Pssl$Pssl_scert_single_ec.c

$(OBJDIR)$Pssl_scert_single_rsa$O: src$Pssl$Pssl_scert_single_rsa.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_scert_single_rsa$O src$Pssl$Pssl_scert_single_rsa.c

$(OBJDIR)$Pssl_server$O: src$Pssl$Pssl_server.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_server$O src$Pssl$Pssl_server.c

$(OBJDIR)$Pssl_server_full_ec$O: src$Pssl$Pssl_server_full_ec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_server_full_ec$O src$Pssl$Pssl_server_full_ec.c

$(OBJDIR)$Pssl_server_full_rsa$O: src$Pssl$Pssl_server_full_rsa.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_server_full_rsa$O src$Pssl$Pssl_server_full_rsa.c

$(OBJDIR)$Pssl_server_mine2c$O: src$Pssl$Pssl_server_mine2c.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_server_mine2c$O src$Pssl$Pssl_server_mine2c.c

$(OBJDIR)$Pssl_server_mine2g$O: src$Pssl$Pssl_server_mine2g.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_server_mine2g$O src$Pssl$Pssl_server_mine2g.c

$(OBJDIR)$Pssl_server_minf2c$O: src$Pssl$Pssl_server_minf2c.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_server_minf2c$O src$Pssl$Pssl_server_minf2c.c

$(OBJDIR)$Pssl_server_minf2g$O: src$Pssl$Pssl_server_minf2g.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_server_minf2g$O src$Pssl$Pssl_server_minf2g.c

$(OBJDIR)$Pssl_server_minr2g$O: src$Pssl$Pssl_server_minr2g.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_server_minr2g$O src$Pssl$Pssl_server_minr2g.c

$(OBJDIR)$Pssl_server_minu2g$O: src$Pssl$Pssl_server_minu2g.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_server_minu2g$O src$Pssl$Pssl_server_minu2g.c

$(OBJDIR)$Pssl_server_minv2g$O: src$Pssl$Pssl_server_minv2g.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pssl_server_minv2g$O src$Pssl$Pssl_server_minv2g.c

$(OBJDIR)$Paes_big_cbcdec$O: src$Psymcipher$Paes_big_cbcdec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_big_cbcdec$O src$Psymcipher$Paes_big_cbcdec.c

$(OBJDIR)$Paes_big_cbcenc$O: src$Psymcipher$Paes_big_cbcenc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_big_cbcenc$O src$Psymcipher$Paes_big_cbcenc.c

$(OBJDIR)$Paes_big_ctr$O: src$Psymcipher$Paes_big_ctr.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_big_ctr$O src$Psymcipher$Paes_big_ctr.c

$(OBJDIR)$Paes_big_ctrcbc$O: src$Psymcipher$Paes_big_ctrcbc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_big_ctrcbc$O src$Psymcipher$Paes_big_ctrcbc.c

$(OBJDIR)$Paes_big_dec$O: src$Psymcipher$Paes_big_dec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_big_dec$O src$Psymcipher$Paes_big_dec.c

$(OBJDIR)$Paes_big_enc$O: src$Psymcipher$Paes_big_enc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_big_enc$O src$Psymcipher$Paes_big_enc.c

$(OBJDIR)$Paes_common$O: src$Psymcipher$Paes_common.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_common$O src$Psymcipher$Paes_common.c

$(OBJDIR)$Paes_ct$O: src$Psymcipher$Paes_ct.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct$O src$Psymcipher$Paes_ct.c

$(OBJDIR)$Paes_ct64$O: src$Psymcipher$Paes_ct64.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct64$O src$Psymcipher$Paes_ct64.c

$(OBJDIR)$Paes_ct64_cbcdec$O: src$Psymcipher$Paes_ct64_cbcdec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct64_cbcdec$O src$Psymcipher$Paes_ct64_cbcdec.c

$(OBJDIR)$Paes_ct64_cbcenc$O: src$Psymcipher$Paes_ct64_cbcenc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct64_cbcenc$O src$Psymcipher$Paes_ct64_cbcenc.c

$(OBJDIR)$Paes_ct64_ctr$O: src$Psymcipher$Paes_ct64_ctr.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct64_ctr$O src$Psymcipher$Paes_ct64_ctr.c

$(OBJDIR)$Paes_ct64_ctrcbc$O: src$Psymcipher$Paes_ct64_ctrcbc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct64_ctrcbc$O src$Psymcipher$Paes_ct64_ctrcbc.c

$(OBJDIR)$Paes_ct64_dec$O: src$Psymcipher$Paes_ct64_dec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct64_dec$O src$Psymcipher$Paes_ct64_dec.c

$(OBJDIR)$Paes_ct64_enc$O: src$Psymcipher$Paes_ct64_enc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct64_enc$O src$Psymcipher$Paes_ct64_enc.c

$(OBJDIR)$Paes_ct_cbcdec$O: src$Psymcipher$Paes_ct_cbcdec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct_cbcdec$O src$Psymcipher$Paes_ct_cbcdec.c

$(OBJDIR)$Paes_ct_cbcenc$O: src$Psymcipher$Paes_ct_cbcenc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct_cbcenc$O src$Psymcipher$Paes_ct_cbcenc.c

$(OBJDIR)$Paes_ct_ctr$O: src$Psymcipher$Paes_ct_ctr.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct_ctr$O src$Psymcipher$Paes_ct_ctr.c

$(OBJDIR)$Paes_ct_ctrcbc$O: src$Psymcipher$Paes_ct_ctrcbc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct_ctrcbc$O src$Psymcipher$Paes_ct_ctrcbc.c

$(OBJDIR)$Paes_ct_dec$O: src$Psymcipher$Paes_ct_dec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct_dec$O src$Psymcipher$Paes_ct_dec.c

$(OBJDIR)$Paes_ct_enc$O: src$Psymcipher$Paes_ct_enc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_ct_enc$O src$Psymcipher$Paes_ct_enc.c

$(OBJDIR)$Paes_pwr8$O: src$Psymcipher$Paes_pwr8.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_pwr8$O src$Psymcipher$Paes_pwr8.c

$(OBJDIR)$Paes_pwr8_cbcdec$O: src$Psymcipher$Paes_pwr8_cbcdec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_pwr8_cbcdec$O src$Psymcipher$Paes_pwr8_cbcdec.c

$(OBJDIR)$Paes_pwr8_cbcenc$O: src$Psymcipher$Paes_pwr8_cbcenc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_pwr8_cbcenc$O src$Psymcipher$Paes_pwr8_cbcenc.c

$(OBJDIR)$Paes_pwr8_ctr$O: src$Psymcipher$Paes_pwr8_ctr.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_pwr8_ctr$O src$Psymcipher$Paes_pwr8_ctr.c

$(OBJDIR)$Paes_pwr8_ctrcbc$O: src$Psymcipher$Paes_pwr8_ctrcbc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_pwr8_ctrcbc$O src$Psymcipher$Paes_pwr8_ctrcbc.c

$(OBJDIR)$Paes_small_cbcdec$O: src$Psymcipher$Paes_small_cbcdec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_small_cbcdec$O src$Psymcipher$Paes_small_cbcdec.c

$(OBJDIR)$Paes_small_cbcenc$O: src$Psymcipher$Paes_small_cbcenc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_small_cbcenc$O src$Psymcipher$Paes_small_cbcenc.c

$(OBJDIR)$Paes_small_ctr$O: src$Psymcipher$Paes_small_ctr.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_small_ctr$O src$Psymcipher$Paes_small_ctr.c

$(OBJDIR)$Paes_small_ctrcbc$O: src$Psymcipher$Paes_small_ctrcbc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_small_ctrcbc$O src$Psymcipher$Paes_small_ctrcbc.c

$(OBJDIR)$Paes_small_dec$O: src$Psymcipher$Paes_small_dec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_small_dec$O src$Psymcipher$Paes_small_dec.c

$(OBJDIR)$Paes_small_enc$O: src$Psymcipher$Paes_small_enc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_small_enc$O src$Psymcipher$Paes_small_enc.c

$(OBJDIR)$Paes_x86ni$O: src$Psymcipher$Paes_x86ni.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_x86ni$O src$Psymcipher$Paes_x86ni.c

$(OBJDIR)$Paes_x86ni_cbcdec$O: src$Psymcipher$Paes_x86ni_cbcdec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_x86ni_cbcdec$O src$Psymcipher$Paes_x86ni_cbcdec.c

$(OBJDIR)$Paes_x86ni_cbcenc$O: src$Psymcipher$Paes_x86ni_cbcenc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_x86ni_cbcenc$O src$Psymcipher$Paes_x86ni_cbcenc.c

$(OBJDIR)$Paes_x86ni_ctr$O: src$Psymcipher$Paes_x86ni_ctr.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_x86ni_ctr$O src$Psymcipher$Paes_x86ni_ctr.c

$(OBJDIR)$Paes_x86ni_ctrcbc$O: src$Psymcipher$Paes_x86ni_ctrcbc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Paes_x86ni_ctrcbc$O src$Psymcipher$Paes_x86ni_ctrcbc.c

$(OBJDIR)$Pchacha20_ct$O: src$Psymcipher$Pchacha20_ct.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pchacha20_ct$O src$Psymcipher$Pchacha20_ct.c

$(OBJDIR)$Pchacha20_sse2$O: src$Psymcipher$Pchacha20_sse2.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pchacha20_sse2$O src$Psymcipher$Pchacha20_sse2.c

$(OBJDIR)$Pdes_ct$O: src$Psymcipher$Pdes_ct.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdes_ct$O src$Psymcipher$Pdes_ct.c

$(OBJDIR)$Pdes_ct_cbcdec$O: src$Psymcipher$Pdes_ct_cbcdec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdes_ct_cbcdec$O src$Psymcipher$Pdes_ct_cbcdec.c

$(OBJDIR)$Pdes_ct_cbcenc$O: src$Psymcipher$Pdes_ct_cbcenc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdes_ct_cbcenc$O src$Psymcipher$Pdes_ct_cbcenc.c

$(OBJDIR)$Pdes_support$O: src$Psymcipher$Pdes_support.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdes_support$O src$Psymcipher$Pdes_support.c

$(OBJDIR)$Pdes_tab$O: src$Psymcipher$Pdes_tab.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdes_tab$O src$Psymcipher$Pdes_tab.c

$(OBJDIR)$Pdes_tab_cbcdec$O: src$Psymcipher$Pdes_tab_cbcdec.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdes_tab_cbcdec$O src$Psymcipher$Pdes_tab_cbcdec.c

$(OBJDIR)$Pdes_tab_cbcenc$O: src$Psymcipher$Pdes_tab_cbcenc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pdes_tab_cbcenc$O src$Psymcipher$Pdes_tab_cbcenc.c

$(OBJDIR)$Ppoly1305_ctmul$O: src$Psymcipher$Ppoly1305_ctmul.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Ppoly1305_ctmul$O src$Psymcipher$Ppoly1305_ctmul.c

$(OBJDIR)$Ppoly1305_ctmul32$O: src$Psymcipher$Ppoly1305_ctmul32.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Ppoly1305_ctmul32$O src$Psymcipher$Ppoly1305_ctmul32.c

$(OBJDIR)$Ppoly1305_ctmulq$O: src$Psymcipher$Ppoly1305_ctmulq.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Ppoly1305_ctmulq$O src$Psymcipher$Ppoly1305_ctmulq.c

$(OBJDIR)$Ppoly1305_i15$O: src$Psymcipher$Ppoly1305_i15.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Ppoly1305_i15$O src$Psymcipher$Ppoly1305_i15.c

$(OBJDIR)$Pasn1enc$O: src$Px509$Pasn1enc.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pasn1enc$O src$Px509$Pasn1enc.c

$(OBJDIR)$Pencode_ec_pk8der$O: src$Px509$Pencode_ec_pk8der.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pencode_ec_pk8der$O src$Px509$Pencode_ec_pk8der.c

$(OBJDIR)$Pencode_ec_rawder$O: src$Px509$Pencode_ec_rawder.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pencode_ec_rawder$O src$Px509$Pencode_ec_rawder.c

$(OBJDIR)$Pencode_rsa_pk8der$O: src$Px509$Pencode_rsa_pk8der.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pencode_rsa_pk8der$O src$Px509$Pencode_rsa_pk8der.c

$(OBJDIR)$Pencode_rsa_rawder$O: src$Px509$Pencode_rsa_rawder.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pencode_rsa_rawder$O src$Px509$Pencode_rsa_rawder.c

$(OBJDIR)$Pskey_decoder$O: src$Px509$Pskey_decoder.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pskey_decoder$O src$Px509$Pskey_decoder.c

$(OBJDIR)$Px509_decoder$O: src$Px509$Px509_decoder.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Px509_decoder$O src$Px509$Px509_decoder.c

$(OBJDIR)$Px509_knownkey$O: src$Px509$Px509_knownkey.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Px509_knownkey$O src$Px509$Px509_knownkey.c

$(OBJDIR)$Px509_minimal$O: src$Px509$Px509_minimal.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Px509_minimal$O src$Px509$Px509_minimal.c

$(OBJDIR)$Px509_minimal_full$O: src$Px509$Px509_minimal_full.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Px509_minimal_full$O src$Px509$Px509_minimal_full.c

$(OBJDIR)$Pbrssl$O: tools$Pbrssl.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pbrssl$O tools$Pbrssl.c

$(OBJDIR)$Pcerts$O: tools$Pcerts.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pcerts$O tools$Pcerts.c

$(OBJDIR)$Pchain$O: tools$Pchain.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pchain$O tools$Pchain.c

$(OBJDIR)$Pclient$O: tools$Pclient.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pclient$O tools$Pclient.c

$(OBJDIR)$Perrors$O: tools$Perrors.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Perrors$O tools$Perrors.c

$(OBJDIR)$Pfiles$O: tools$Pfiles.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pfiles$O tools$Pfiles.c

$(OBJDIR)$Pimpl$O: tools$Pimpl.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pimpl$O tools$Pimpl.c

$(OBJDIR)$Pkeys$O: tools$Pkeys.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pkeys$O tools$Pkeys.c

$(OBJDIR)$Pnames$O: tools$Pnames.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pnames$O tools$Pnames.c

$(OBJDIR)$Pserver$O: tools$Pserver.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pserver$O tools$Pserver.c

$(OBJDIR)$Pskey$O: tools$Pskey.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pskey$O tools$Pskey.c

$(OBJDIR)$Psslio$O: tools$Psslio.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Psslio$O tools$Psslio.c

$(OBJDIR)$Pta$O: tools$Pta.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pta$O tools$Pta.c

$(OBJDIR)$Ptwrch$O: tools$Ptwrch.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Ptwrch$O tools$Ptwrch.c

$(OBJDIR)$Pvector$O: tools$Pvector.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pvector$O tools$Pvector.c

$(OBJDIR)$Pverify$O: tools$Pverify.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pverify$O tools$Pverify.c

$(OBJDIR)$Pxmem$O: tools$Pxmem.c $(HEADERSTOOLS)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Pxmem$O tools$Pxmem.c

$(OBJDIR)$Ptest_crypto$O: test$Ptest_crypto.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Ptest_crypto$O test$Ptest_crypto.c

$(OBJDIR)$Ptest_speed$O: test$Ptest_speed.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) $(CCOUT)$(OBJDIR)$Ptest_speed$O test$Ptest_speed.c

$(OBJDIR)$Ptest_x509$O: test$Ptest_x509.c $(HEADERSPRIV)
	$(CC) $(CFLAGS) $(INCFLAGS) -DSRCDIRNAME=".." $(CCOUT)$(OBJDIR)$Ptest_x509$O test$Ptest_x509.c
