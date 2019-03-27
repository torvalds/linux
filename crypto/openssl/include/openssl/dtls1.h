/*
 * Copyright 2005-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_DTLS1_H
# define HEADER_DTLS1_H

#ifdef  __cplusplus
extern "C" {
#endif

# define DTLS1_VERSION                   0xFEFF
# define DTLS1_2_VERSION                 0xFEFD
# define DTLS_MIN_VERSION                DTLS1_VERSION
# define DTLS_MAX_VERSION                DTLS1_2_VERSION
# define DTLS1_VERSION_MAJOR             0xFE

# define DTLS1_BAD_VER                   0x0100

/* Special value for method supporting multiple versions */
# define DTLS_ANY_VERSION                0x1FFFF

/* lengths of messages */
/*
 * Actually the max cookie length in DTLS is 255. But we can't change this now
 * due to compatibility concerns.
 */
# define DTLS1_COOKIE_LENGTH                     256

# define DTLS1_RT_HEADER_LENGTH                  13

# define DTLS1_HM_HEADER_LENGTH                  12

# define DTLS1_HM_BAD_FRAGMENT                   -2
# define DTLS1_HM_FRAGMENT_RETRY                 -3

# define DTLS1_CCS_HEADER_LENGTH                  1

# define DTLS1_AL_HEADER_LENGTH                   2

/* Timeout multipliers (timeout slice is defined in apps/timeouts.h */
# define DTLS1_TMO_READ_COUNT                      2
# define DTLS1_TMO_WRITE_COUNT                     2

# define DTLS1_TMO_ALERT_COUNT                     12

#ifdef  __cplusplus
}
#endif
#endif
