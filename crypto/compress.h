/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cryptographic API.
 *
 * Copyright 2015 LG Electronics Inc.
 * Copyright (c) 2016, Intel Corporation
 * Copyright (c) 2023 Herbert Xu <herbert@gondor.apana.org.au>
 */
#ifndef _LOCAL_CRYPTO_COMPRESS_H
#define _LOCAL_CRYPTO_COMPRESS_H

#include "internal.h"

struct acomp_req;
struct comp_alg_common;

int crypto_init_scomp_ops_async(struct crypto_tfm *tfm);

void comp_prepare_alg(struct comp_alg_common *alg);

#endif	/* _LOCAL_CRYPTO_COMPRESS_H */
