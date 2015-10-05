/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  HW abstraction register functions
 *
 *  Authors: Christoph Raisch <raisch@de.ibm.com>
 *           Reinhard Ernst <rernst@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __HIPZ_FNS_H__
#define __HIPZ_FNS_H__

#include "ehca_classes.h"
#include "hipz_hw.h"

#include "hipz_fns_core.h"

#define hipz_galpa_store_eq(gal, offset, value) \
	hipz_galpa_store(gal, EQTEMM_OFFSET(offset), value)

#define hipz_galpa_load_eq(gal, offset) \
	hipz_galpa_load(gal, EQTEMM_OFFSET(offset))

#define hipz_galpa_store_qped(gal, offset, value) \
	hipz_galpa_store(gal, QPEDMM_OFFSET(offset), value)

#define hipz_galpa_load_qped(gal, offset) \
	hipz_galpa_load(gal, QPEDMM_OFFSET(offset))

#define hipz_galpa_store_mrmw(gal, offset, value) \
	hipz_galpa_store(gal, MRMWMM_OFFSET(offset), value)

#define hipz_galpa_load_mrmw(gal, offset) \
	hipz_galpa_load(gal, MRMWMM_OFFSET(offset))

#endif
