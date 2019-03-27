/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

typedef struct name_funcs_st NAME_FUNCS;
DEFINE_STACK_OF(NAME_FUNCS)
DEFINE_LHASH_OF(OBJ_NAME);
typedef struct added_obj_st ADDED_OBJ;
DEFINE_LHASH_OF(ADDED_OBJ);
