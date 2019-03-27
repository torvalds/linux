/*
 * Copyright 2004-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>
#if OPENSSL_API_COMPAT >= 0x00908000L
NON_EMPTY_TRANSLATION_UNIT
#else

# include <openssl/evp.h>

/*
 * Define some deprecated functions, so older programs don't crash and burn
 * too quickly.  On Windows and VMS, these will never be used, since
 * functions and variables in shared libraries are selected by entry point
 * location, not by name.
 */

# ifndef OPENSSL_NO_BF
#  undef EVP_bf_cfb
const EVP_CIPHER *EVP_bf_cfb(void);
const EVP_CIPHER *EVP_bf_cfb(void)
{
    return EVP_bf_cfb64();
}
# endif

# ifndef OPENSSL_NO_DES
#  undef EVP_des_cfb
const EVP_CIPHER *EVP_des_cfb(void);
const EVP_CIPHER *EVP_des_cfb(void)
{
    return EVP_des_cfb64();
}

#  undef EVP_des_ede3_cfb
const EVP_CIPHER *EVP_des_ede3_cfb(void);
const EVP_CIPHER *EVP_des_ede3_cfb(void)
{
    return EVP_des_ede3_cfb64();
}

#  undef EVP_des_ede_cfb
const EVP_CIPHER *EVP_des_ede_cfb(void);
const EVP_CIPHER *EVP_des_ede_cfb(void)
{
    return EVP_des_ede_cfb64();
}
# endif

# ifndef OPENSSL_NO_IDEA
#  undef EVP_idea_cfb
const EVP_CIPHER *EVP_idea_cfb(void);
const EVP_CIPHER *EVP_idea_cfb(void)
{
    return EVP_idea_cfb64();
}
# endif

# ifndef OPENSSL_NO_RC2
#  undef EVP_rc2_cfb
const EVP_CIPHER *EVP_rc2_cfb(void);
const EVP_CIPHER *EVP_rc2_cfb(void)
{
    return EVP_rc2_cfb64();
}
# endif

# ifndef OPENSSL_NO_CAST
#  undef EVP_cast5_cfb
const EVP_CIPHER *EVP_cast5_cfb(void);
const EVP_CIPHER *EVP_cast5_cfb(void)
{
    return EVP_cast5_cfb64();
}
# endif

# ifndef OPENSSL_NO_RC5
#  undef EVP_rc5_32_12_16_cfb
const EVP_CIPHER *EVP_rc5_32_12_16_cfb(void);
const EVP_CIPHER *EVP_rc5_32_12_16_cfb(void)
{
    return EVP_rc5_32_12_16_cfb64();
}
# endif

# undef EVP_aes_128_cfb
const EVP_CIPHER *EVP_aes_128_cfb(void);
const EVP_CIPHER *EVP_aes_128_cfb(void)
{
    return EVP_aes_128_cfb128();
}

# undef EVP_aes_192_cfb
const EVP_CIPHER *EVP_aes_192_cfb(void);
const EVP_CIPHER *EVP_aes_192_cfb(void)
{
    return EVP_aes_192_cfb128();
}

# undef EVP_aes_256_cfb
const EVP_CIPHER *EVP_aes_256_cfb(void);
const EVP_CIPHER *EVP_aes_256_cfb(void)
{
    return EVP_aes_256_cfb128();
}

#endif
