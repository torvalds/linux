/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This Synopsys software and associated documentation (hereinafter the
 * "Software") is an unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you. The
 * Software IS NOT an item of Licensed Software or a Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Products
 * with Synopsys or any supplement thereto. Synopsys is a registered trademark
 * of Synopsys, Inc. Other names included in the SOFTWARE may be the
 * trademarks of their respective owners.
 *
 * The contents of this file are dual-licensed; you may select either version
 * 2 of the GNU General Public License ("GPL") or the BSD-3-Clause license
 * ("BSD-3-Clause"). The GPL is included in the COPYING file accompanying the
 * SOFTWARE. The BSD License is copied below.
 *
 * BSD-3-Clause License:
 * Copyright (c) 2012-2016 Synopsys, Inc. and/or its affiliates.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer, without
 *    modification.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. The names of the above-listed copyright holders may not be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NISTTRNG_PRIVATE_H
#define NISTTRNG_PRIVATE_H

#include "elppdu.h"
#include "nisttrng_hw.h"
#include "nisttrng_common.h"

int nisttrng_wait_on_busy(struct nist_trng_state *state);
int nisttrng_wait_on_done(struct nist_trng_state *state);
int nisttrng_wait_on_noise_rdy(struct nist_trng_state *state);
int nisttrng_get_alarms(struct nist_trng_state *state);
int nisttrng_reset_state(struct nist_trng_state *state);

/* ---------- Reminders ---------- */
int nisttrng_reset_counters(struct nist_trng_state *state);
int nisttrng_set_reminder_max_bits_per_req(struct nist_trng_state *state, unsigned long max_bits_per_req);
int nisttrng_set_reminder_max_req_per_seed(struct nist_trng_state *state, unsigned long long max_req_per_seed);
int nisttrng_check_seed_lifetime(struct nist_trng_state *state);

/* ---------- Set field APIs ---------- */
int nisttrng_set_sec_strength(struct nist_trng_state *state, int req_sec_strength);
int nisttrng_set_addin_present(struct nist_trng_state *state, int addin_present);
int nisttrng_set_pred_resist(struct nist_trng_state *state, int pred_resist);
int nisttrng_set_secure_mode(struct nist_trng_state *state, int secure_mode);
int nisttrng_set_nonce_mode(struct nist_trng_state *state, int nonce_mode);

/* ---------- Load data APIs ---------- */
int nisttrng_load_ps_addin(struct nist_trng_state *state, void *input_str);

/* ---------- Command APIs ---------- */
int nisttrng_get_entropy_input(struct nist_trng_state *state, void *input_nonce, int nonce_operation);
int nisttrng_refresh_addin(struct nist_trng_state *state, void *addin_str);
int nisttrng_gen_random(struct nist_trng_state *state, void *random_bits, unsigned long req_num_bytes);
int nisttrng_advance_state(struct nist_trng_state *state);
int nisttrng_kat(struct nist_trng_state *state, int kat_sel, int kat_vec);
int nisttrng_full_kat(struct nist_trng_state *state);
int nisttrng_zeroize(struct nist_trng_state *state);

/* ---------- edu related ---------- */

int nisttrng_rnc(struct nist_trng_state *state, int rnc_ctrl_cmd);
int nisttrng_wait_fifo_full(struct nist_trng_state *state);
#endif
