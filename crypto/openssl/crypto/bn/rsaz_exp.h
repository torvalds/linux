/*
 * Copyright 2013-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2012, Intel Corporation. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * Originally written by Shay Gueron (1, 2), and Vlad Krasnov (1)
 * (1) Intel Corporation, Israel Development Center, Haifa, Israel
 * (2) University of Haifa, Israel
 */

#ifndef RSAZ_EXP_H
# define RSAZ_EXP_H

# undef RSAZ_ENABLED
# if defined(OPENSSL_BN_ASM_MONT) && \
        (defined(__x86_64) || defined(__x86_64__) || \
         defined(_M_AMD64) || defined(_M_X64))
#  define RSAZ_ENABLED

#  include <openssl/bn.h>

void RSAZ_1024_mod_exp_avx2(BN_ULONG result[16],
                            const BN_ULONG base_norm[16],
                            const BN_ULONG exponent[16],
                            const BN_ULONG m_norm[16], const BN_ULONG RR[16],
                            BN_ULONG k0);
int rsaz_avx2_eligible(void);

void RSAZ_512_mod_exp(BN_ULONG result[8],
                      const BN_ULONG base_norm[8], const BN_ULONG exponent[8],
                      const BN_ULONG m_norm[8], BN_ULONG k0,
                      const BN_ULONG RR[8]);

# endif

#endif
