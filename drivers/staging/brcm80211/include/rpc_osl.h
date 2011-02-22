/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _rpcosl_h_
#define _rpcosl_h_

typedef struct rpc_osl rpc_osl_t;
extern rpc_osl_t *rpc_osl_attach(struct osl_info *osh);
extern void rpc_osl_detach(rpc_osl_t *rpc_osh);

#define RPC_OSL_LOCK(rpc_osh) rpc_osl_lock((rpc_osh))
#define RPC_OSL_UNLOCK(rpc_osh) rpc_osl_unlock((rpc_osh))
#define RPC_OSL_WAIT(rpc_osh, to, ptimedout)	rpc_osl_wait((rpc_osh), (to), (ptimedout))
#define RPC_OSL_WAKE(rpc_osh)			rpc_osl_wake((rpc_osh))
extern void rpc_osl_lock(rpc_osl_t *rpc_osh);
extern void rpc_osl_unlock(rpc_osl_t *rpc_osh);
extern int rpc_osl_wait(rpc_osl_t *rpc_osh, uint ms, bool *ptimedout);
extern void rpc_osl_wake(rpc_osl_t *rpc_osh);

#endif				/* _rpcosl_h_ */
