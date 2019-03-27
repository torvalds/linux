/* $OpenBSD: xmss_wots.c,v 1.3 2018/04/10 00:10:49 djm Exp $ */
/*
wots.c version 20160722
Andreas Hülsing
Joost Rijneveld
Public domain.
*/

#include "includes.h"
#ifdef WITH_XMSS

#include <stdlib.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <limits.h>
#include "xmss_commons.h"
#include "xmss_hash.h"
#include "xmss_wots.h"
#include "xmss_hash_address.h"


/* libm-free version of log2() for wots */
static inline int
wots_log2(uint32_t v)
{
  int      b;

  for (b = sizeof (v) * CHAR_BIT - 1; b >= 0; b--) {
    if ((1U << b) & v) {
      return b;
    }
  }
  return 0;
}

void
wots_set_params(wots_params *params, int n, int w)
{
  params->n = n;
  params->w = w;
  params->log_w = wots_log2(params->w);
  params->len_1 = (CHAR_BIT * n) / params->log_w;
  params->len_2 = (wots_log2(params->len_1 * (w - 1)) / params->log_w) + 1;
  params->len = params->len_1 + params->len_2;
  params->keysize = params->len * params->n;
}

/**
 * Helper method for pseudorandom key generation
 * Expands an n-byte array into a len*n byte array
 * this is done using PRF
 */
static void expand_seed(unsigned char *outseeds, const unsigned char *inseed, const wots_params *params)
{
  uint32_t i = 0;
  unsigned char ctr[32];
  for(i = 0; i < params->len; i++){
    to_byte(ctr, i, 32);
    prf((outseeds + (i*params->n)), ctr, inseed, params->n);
  }
}

/**
 * Computes the chaining function.
 * out and in have to be n-byte arrays
 *
 * interprets in as start-th value of the chain
 * addr has to contain the address of the chain
 */
static void gen_chain(unsigned char *out, const unsigned char *in, unsigned int start, unsigned int steps, const wots_params *params, const unsigned char *pub_seed, uint32_t addr[8])
{
  uint32_t i, j;
  for (j = 0; j < params->n; j++)
    out[j] = in[j];

  for (i = start; i < (start+steps) && i < params->w; i++) {
    setHashADRS(addr, i);
    hash_f(out, out, pub_seed, addr, params->n);
  }
}

/**
 * base_w algorithm as described in draft.
 *
 *
 */
static void base_w(int *output, const int out_len, const unsigned char *input, const wots_params *params)
{
  int in = 0;
  int out = 0;
  uint32_t total = 0;
  int bits = 0;
  int consumed = 0;

  for (consumed = 0; consumed < out_len; consumed++) {
    if (bits == 0) {
      total = input[in];
      in++;
      bits += 8;
    }
    bits -= params->log_w;
    output[out] = (total >> bits) & (params->w - 1);
    out++;
  }
}

void wots_pkgen(unsigned char *pk, const unsigned char *sk, const wots_params *params, const unsigned char *pub_seed, uint32_t addr[8])
{
  uint32_t i;
  expand_seed(pk, sk, params);
  for (i=0; i < params->len; i++) {
    setChainADRS(addr, i);
    gen_chain(pk+i*params->n, pk+i*params->n, 0, params->w-1, params, pub_seed, addr);
  }
}


int wots_sign(unsigned char *sig, const unsigned char *msg, const unsigned char *sk, const wots_params *params, const unsigned char *pub_seed, uint32_t addr[8])
{
  //int basew[params->len];
  int csum = 0;
  uint32_t i = 0;
  int *basew = calloc(params->len, sizeof(int));
  if (basew == NULL)
    return -1;

  base_w(basew, params->len_1, msg, params);

  for (i=0; i < params->len_1; i++) {
    csum += params->w - 1 - basew[i];
  }

  csum = csum << (8 - ((params->len_2 * params->log_w) % 8));

  int len_2_bytes = ((params->len_2 * params->log_w) + 7) / 8;

  unsigned char csum_bytes[len_2_bytes];
  to_byte(csum_bytes, csum, len_2_bytes);

  int csum_basew[params->len_2];
  base_w(csum_basew, params->len_2, csum_bytes, params);

  for (i = 0; i < params->len_2; i++) {
    basew[params->len_1 + i] = csum_basew[i];
  }

  expand_seed(sig, sk, params);

  for (i = 0; i < params->len; i++) {
    setChainADRS(addr, i);
    gen_chain(sig+i*params->n, sig+i*params->n, 0, basew[i], params, pub_seed, addr);
  }
  free(basew);
  return 0;
}

int wots_pkFromSig(unsigned char *pk, const unsigned char *sig, const unsigned char *msg, const wots_params *params, const unsigned char *pub_seed, uint32_t addr[8])
{
  int csum = 0;
  uint32_t i = 0;
  int *basew = calloc(params->len, sizeof(int));
  if (basew == NULL)
    return -1;

  base_w(basew, params->len_1, msg, params);

  for (i=0; i < params->len_1; i++) {
    csum += params->w - 1 - basew[i];
  }

  csum = csum << (8 - ((params->len_2 * params->log_w) % 8));

  int len_2_bytes = ((params->len_2 * params->log_w) + 7) / 8;

  unsigned char csum_bytes[len_2_bytes];
  to_byte(csum_bytes, csum, len_2_bytes);

  int csum_basew[params->len_2];
  base_w(csum_basew, params->len_2, csum_bytes, params);

  for (i = 0; i < params->len_2; i++) {
    basew[params->len_1 + i] = csum_basew[i];
  }
  for (i=0; i < params->len; i++) {
    setChainADRS(addr, i);
    gen_chain(pk+i*params->n, sig+i*params->n, basew[i], params->w-1-basew[i], params, pub_seed, addr);
  }
  free(basew);
  return 0;
}
#endif /* WITH_XMSS */
