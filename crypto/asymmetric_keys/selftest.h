/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Helper function for self-testing PKCS#7 signature verification.
 *
 * Copyright (C) 2024 Joachim Vandersmissen <git@jvdsn.com>
 */

void fips_signature_selftest(const char *name,
			     const u8 *keys, size_t keys_len,
			     const u8 *data, size_t data_len,
			     const u8 *sig, size_t sig_len);

#ifdef CONFIG_FIPS_SIGNATURE_SELFTEST_RSA
void __init fips_signature_selftest_rsa(void);
#else
static inline void __init fips_signature_selftest_rsa(void) { }
#endif

#ifdef CONFIG_FIPS_SIGNATURE_SELFTEST_ECDSA
void __init fips_signature_selftest_ecdsa(void);
#else
static inline void __init fips_signature_selftest_ecdsa(void) { }
#endif
