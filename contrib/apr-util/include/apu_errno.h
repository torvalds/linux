/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APU_ERRNO_H
#define APU_ERRNO_H

/**
 * @file apu_errno.h
 * @brief APR-Util Error Codes
 */

#include "apr.h"
#include "apr_errno.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup apu_errno Error Codes
 * @ingroup APR_Util
 * @{
 */

/**
 * @defgroup APR_Util_Error APR_Util Error Values
 * <PRE>
 * <b>APU ERROR VALUES</b>
 * APR_ENOKEY         The key provided was empty or NULL
 * APR_ENOIV          The initialisation vector provided was NULL
 * APR_EKEYTYPE       The key type was not recognised
 * APR_ENOSPACE       The buffer supplied was not big enough
 * APR_ECRYPT         An error occurred while encrypting or decrypting
 * APR_EPADDING       Padding was not supported
 * APR_EKEYLENGTH     The key length was incorrect
 * APR_ENOCIPHER      The cipher provided was not recognised
 * APR_ENODIGEST      The digest provided was not recognised
 * APR_ENOENGINE      The engine provided was not recognised
 * APR_EINITENGINE    The engine could not be initialised
 * APR_EREINIT        Underlying crypto has already been initialised
 * </PRE>
 *
 * <PRE>
 * <b>APR STATUS VALUES</b>
 * APR_INCHILD        Program is currently executing in the child
 * </PRE>
 * @{
 */
/** @see APR_STATUS_IS_ENOKEY */
#define APR_ENOKEY           (APR_UTIL_START_STATUS + 1)
/** @see APR_STATUS_IS_ENOIV */
#define APR_ENOIV            (APR_UTIL_START_STATUS + 2)
/** @see APR_STATUS_IS_EKEYTYPE */
#define APR_EKEYTYPE         (APR_UTIL_START_STATUS + 3)
/** @see APR_STATUS_IS_ENOSPACE */
#define APR_ENOSPACE         (APR_UTIL_START_STATUS + 4)
/** @see APR_STATUS_IS_ECRYPT */
#define APR_ECRYPT           (APR_UTIL_START_STATUS + 5)
/** @see APR_STATUS_IS_EPADDING */
#define APR_EPADDING         (APR_UTIL_START_STATUS + 6)
/** @see APR_STATUS_IS_EKEYLENGTH */
#define APR_EKEYLENGTH       (APR_UTIL_START_STATUS + 7)
/** @see APR_STATUS_IS_ENOCIPHER */
#define APR_ENOCIPHER        (APR_UTIL_START_STATUS + 8)
/** @see APR_STATUS_IS_ENODIGEST */
#define APR_ENODIGEST        (APR_UTIL_START_STATUS + 9)
/** @see APR_STATUS_IS_ENOENGINE */
#define APR_ENOENGINE        (APR_UTIL_START_STATUS + 10)
/** @see APR_STATUS_IS_EINITENGINE */
#define APR_EINITENGINE      (APR_UTIL_START_STATUS + 11)
/** @see APR_STATUS_IS_EREINIT */
#define APR_EREINIT          (APR_UTIL_START_STATUS + 12)
/** @} */

/**
 * @defgroup APU_STATUS_IS Status Value Tests
 * @warning For any particular error condition, more than one of these tests
 *      may match. This is because platform-specific error codes may not
 *      always match the semantics of the POSIX codes these tests (and the
 *      corresponding APR error codes) are named after. A notable example
 *      are the APR_STATUS_IS_ENOENT and APR_STATUS_IS_ENOTDIR tests on
 *      Win32 platforms. The programmer should always be aware of this and
 *      adjust the order of the tests accordingly.
 * @{
 */

/** @} */

/**
 * @addtogroup APR_Util_Error
 * @{
 */
/**
 * The key was empty or not provided
 */
#define APR_STATUS_IS_ENOKEY(s)        ((s) == APR_ENOKEY)
/**
 * The initialisation vector was not provided
 */
#define APR_STATUS_IS_ENOIV(s)        ((s) == APR_ENOIV)
/**
 * The key type was not recognised
 */
#define APR_STATUS_IS_EKEYTYPE(s)        ((s) == APR_EKEYTYPE)
/**
 * The buffer provided was not big enough
 */
#define APR_STATUS_IS_ENOSPACE(s)        ((s) == APR_ENOSPACE)
/**
 * An error occurred while encrypting or decrypting
 */
#define APR_STATUS_IS_ECRYPT(s)        ((s) == APR_ECRYPT)
/**
 * An error occurred while padding
 */
#define APR_STATUS_IS_EPADDING(s)        ((s) == APR_EPADDING)
/**
 * An error occurred with the key length
 */
#define APR_STATUS_IS_EKEYLENGTH(s)        ((s) == APR_EKEYLENGTH)
/**
 * The cipher provided was not recognised
 */
#define APR_STATUS_IS_ENOCIPHER(s)        ((s) == APR_ENOCIPHER)
/**
 * The digest provided was not recognised
 */
#define APR_STATUS_IS_ENODIGEST(s)        ((s) == APR_ENODIGEST)
/**
 * The engine provided was not recognised
 */
#define APR_STATUS_IS_ENOENGINE(s)        ((s) == APR_ENOENGINE)
/**
 * The engine could not be initialised
 */
#define APR_STATUS_IS_EINITENGINE(s)        ((s) == APR_EINITENGINE)
/**
 * Crypto has already been initialised
 */
#define APR_STATUS_IS_EREINIT(s)        ((s) == APR_EREINIT)
/** @} */

/**
 * This structure allows the underlying API error codes to be returned
 * along with plain text error messages that explain to us mere mortals
 * what really happened.
 */
typedef struct apu_err_t {
    const char *reason;
    const char *msg;
    int rc;
} apu_err_t;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* ! APU_ERRNO_H */
