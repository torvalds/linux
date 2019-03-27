/* $OpenBSD: sc25519.c,v 1.3 2013/12/09 11:03:45 markus Exp $ */

/*
 * Public Domain, Authors: Daniel J. Bernstein, Niels Duif, Tanja Lange,
 * Peter Schwabe, Bo-Yin Yang.
 * Copied from supercop-20130419/crypto_sign/ed25519/ref/sc25519.c
 */

#include "includes.h"

#include "sc25519.h"

/*Arithmetic modulo the group order m = 2^252 +  27742317777372353535851937790883648493 = 7237005577332262213973186563042994240857116359379907606001950938285454250989 */

static const crypto_uint32 m[32] = {0xED, 0xD3, 0xF5, 0x5C, 0x1A, 0x63, 0x12, 0x58, 0xD6, 0x9C, 0xF7, 0xA2, 0xDE, 0xF9, 0xDE, 0x14, 
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

static const crypto_uint32 mu[33] = {0x1B, 0x13, 0x2C, 0x0A, 0xA3, 0xE5, 0x9C, 0xED, 0xA7, 0x29, 0x63, 0x08, 0x5D, 0x21, 0x06, 0x21, 
                                     0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F};

static crypto_uint32 lt(crypto_uint32 a,crypto_uint32 b) /* 16-bit inputs */
{
  unsigned int x = a;
  x -= (unsigned int) b; /* 0..65535: no; 4294901761..4294967295: yes */
  x >>= 31; /* 0: no; 1: yes */
  return x;
}

/* Reduce coefficients of r before calling reduce_add_sub */
static void reduce_add_sub(sc25519 *r)
{
  crypto_uint32 pb = 0;
  crypto_uint32 b;
  crypto_uint32 mask;
  int i;
  unsigned char t[32];

  for(i=0;i<32;i++) 
  {
    pb += m[i];
    b = lt(r->v[i],pb);
    t[i] = r->v[i]-pb+(b<<8);
    pb = b;
  }
  mask = b - 1;
  for(i=0;i<32;i++) 
    r->v[i] ^= mask & (r->v[i] ^ t[i]);
}

/* Reduce coefficients of x before calling barrett_reduce */
static void barrett_reduce(sc25519 *r, const crypto_uint32 x[64])
{
  /* See HAC, Alg. 14.42 */
  int i,j;
  crypto_uint32 q2[66];
  crypto_uint32 *q3 = q2 + 33;
  crypto_uint32 r1[33];
  crypto_uint32 r2[33];
  crypto_uint32 carry;
  crypto_uint32 pb = 0;
  crypto_uint32 b;

  for (i = 0;i < 66;++i) q2[i] = 0;
  for (i = 0;i < 33;++i) r2[i] = 0;

  for(i=0;i<33;i++)
    for(j=0;j<33;j++)
      if(i+j >= 31) q2[i+j] += mu[i]*x[j+31];
  carry = q2[31] >> 8;
  q2[32] += carry;
  carry = q2[32] >> 8;
  q2[33] += carry;

  for(i=0;i<33;i++)r1[i] = x[i];
  for(i=0;i<32;i++)
    for(j=0;j<33;j++)
      if(i+j < 33) r2[i+j] += m[i]*q3[j];

  for(i=0;i<32;i++)
  {
    carry = r2[i] >> 8;
    r2[i+1] += carry;
    r2[i] &= 0xff;
  }

  for(i=0;i<32;i++) 
  {
    pb += r2[i];
    b = lt(r1[i],pb);
    r->v[i] = r1[i]-pb+(b<<8);
    pb = b;
  }

  /* XXX: Can it really happen that r<0?, See HAC, Alg 14.42, Step 3 
   * If so: Handle  it here!
   */

  reduce_add_sub(r);
  reduce_add_sub(r);
}

void sc25519_from32bytes(sc25519 *r, const unsigned char x[32])
{
  int i;
  crypto_uint32 t[64];
  for(i=0;i<32;i++) t[i] = x[i];
  for(i=32;i<64;++i) t[i] = 0;
  barrett_reduce(r, t);
}

void shortsc25519_from16bytes(shortsc25519 *r, const unsigned char x[16])
{
  int i;
  for(i=0;i<16;i++) r->v[i] = x[i];
}

void sc25519_from64bytes(sc25519 *r, const unsigned char x[64])
{
  int i;
  crypto_uint32 t[64];
  for(i=0;i<64;i++) t[i] = x[i];
  barrett_reduce(r, t);
}

void sc25519_from_shortsc(sc25519 *r, const shortsc25519 *x)
{
  int i;
  for(i=0;i<16;i++)
    r->v[i] = x->v[i];
  for(i=0;i<16;i++)
    r->v[16+i] = 0;
}

void sc25519_to32bytes(unsigned char r[32], const sc25519 *x)
{
  int i;
  for(i=0;i<32;i++) r[i] = x->v[i];
}

int sc25519_iszero_vartime(const sc25519 *x)
{
  int i;
  for(i=0;i<32;i++)
    if(x->v[i] != 0) return 0;
  return 1;
}

int sc25519_isshort_vartime(const sc25519 *x)
{
  int i;
  for(i=31;i>15;i--)
    if(x->v[i] != 0) return 0;
  return 1;
}

int sc25519_lt_vartime(const sc25519 *x, const sc25519 *y)
{
  int i;
  for(i=31;i>=0;i--)
  {
    if(x->v[i] < y->v[i]) return 1;
    if(x->v[i] > y->v[i]) return 0;
  }
  return 0;
}

void sc25519_add(sc25519 *r, const sc25519 *x, const sc25519 *y)
{
  int i, carry;
  for(i=0;i<32;i++) r->v[i] = x->v[i] + y->v[i];
  for(i=0;i<31;i++)
  {
    carry = r->v[i] >> 8;
    r->v[i+1] += carry;
    r->v[i] &= 0xff;
  }
  reduce_add_sub(r);
}

void sc25519_sub_nored(sc25519 *r, const sc25519 *x, const sc25519 *y)
{
  crypto_uint32 b = 0;
  crypto_uint32 t;
  int i;
  for(i=0;i<32;i++)
  {
    t = x->v[i] - y->v[i] - b;
    r->v[i] = t & 255;
    b = (t >> 8) & 1;
  }
}

void sc25519_mul(sc25519 *r, const sc25519 *x, const sc25519 *y)
{
  int i,j,carry;
  crypto_uint32 t[64];
  for(i=0;i<64;i++)t[i] = 0;

  for(i=0;i<32;i++)
    for(j=0;j<32;j++)
      t[i+j] += x->v[i] * y->v[j];

  /* Reduce coefficients */
  for(i=0;i<63;i++)
  {
    carry = t[i] >> 8;
    t[i+1] += carry;
    t[i] &= 0xff;
  }

  barrett_reduce(r, t);
}

void sc25519_mul_shortsc(sc25519 *r, const sc25519 *x, const shortsc25519 *y)
{
  sc25519 t;
  sc25519_from_shortsc(&t, y);
  sc25519_mul(r, x, &t);
}

void sc25519_window3(signed char r[85], const sc25519 *s)
{
  char carry;
  int i;
  for(i=0;i<10;i++)
  {
    r[8*i+0]  =  s->v[3*i+0]       & 7;
    r[8*i+1]  = (s->v[3*i+0] >> 3) & 7;
    r[8*i+2]  = (s->v[3*i+0] >> 6) & 7;
    r[8*i+2] ^= (s->v[3*i+1] << 2) & 7;
    r[8*i+3]  = (s->v[3*i+1] >> 1) & 7;
    r[8*i+4]  = (s->v[3*i+1] >> 4) & 7;
    r[8*i+5]  = (s->v[3*i+1] >> 7) & 7;
    r[8*i+5] ^= (s->v[3*i+2] << 1) & 7;
    r[8*i+6]  = (s->v[3*i+2] >> 2) & 7;
    r[8*i+7]  = (s->v[3*i+2] >> 5) & 7;
  }
  r[8*i+0]  =  s->v[3*i+0]       & 7;
  r[8*i+1]  = (s->v[3*i+0] >> 3) & 7;
  r[8*i+2]  = (s->v[3*i+0] >> 6) & 7;
  r[8*i+2] ^= (s->v[3*i+1] << 2) & 7;
  r[8*i+3]  = (s->v[3*i+1] >> 1) & 7;
  r[8*i+4]  = (s->v[3*i+1] >> 4) & 7;

  /* Making it signed */
  carry = 0;
  for(i=0;i<84;i++)
  {
    r[i] += carry;
    r[i+1] += r[i] >> 3;
    r[i] &= 7;
    carry = r[i] >> 2;
    r[i] -= carry<<3;
  }
  r[84] += carry;
}

void sc25519_window5(signed char r[51], const sc25519 *s)
{
  char carry;
  int i;
  for(i=0;i<6;i++)
  {
    r[8*i+0]  =  s->v[5*i+0]       & 31;
    r[8*i+1]  = (s->v[5*i+0] >> 5) & 31;
    r[8*i+1] ^= (s->v[5*i+1] << 3) & 31;
    r[8*i+2]  = (s->v[5*i+1] >> 2) & 31;
    r[8*i+3]  = (s->v[5*i+1] >> 7) & 31;
    r[8*i+3] ^= (s->v[5*i+2] << 1) & 31;
    r[8*i+4]  = (s->v[5*i+2] >> 4) & 31;
    r[8*i+4] ^= (s->v[5*i+3] << 4) & 31;
    r[8*i+5]  = (s->v[5*i+3] >> 1) & 31;
    r[8*i+6]  = (s->v[5*i+3] >> 6) & 31;
    r[8*i+6] ^= (s->v[5*i+4] << 2) & 31;
    r[8*i+7]  = (s->v[5*i+4] >> 3) & 31;
  }
  r[8*i+0]  =  s->v[5*i+0]       & 31;
  r[8*i+1]  = (s->v[5*i+0] >> 5) & 31;
  r[8*i+1] ^= (s->v[5*i+1] << 3) & 31;
  r[8*i+2]  = (s->v[5*i+1] >> 2) & 31;

  /* Making it signed */
  carry = 0;
  for(i=0;i<50;i++)
  {
    r[i] += carry;
    r[i+1] += r[i] >> 5;
    r[i] &= 31;
    carry = r[i] >> 4;
    r[i] -= carry<<5;
  }
  r[50] += carry;
}

void sc25519_2interleave2(unsigned char r[127], const sc25519 *s1, const sc25519 *s2)
{
  int i;
  for(i=0;i<31;i++)
  {
    r[4*i]   = ( s1->v[i]       & 3) ^ (( s2->v[i]       & 3) << 2);
    r[4*i+1] = ((s1->v[i] >> 2) & 3) ^ (((s2->v[i] >> 2) & 3) << 2);
    r[4*i+2] = ((s1->v[i] >> 4) & 3) ^ (((s2->v[i] >> 4) & 3) << 2);
    r[4*i+3] = ((s1->v[i] >> 6) & 3) ^ (((s2->v[i] >> 6) & 3) << 2);
  }
  r[124] = ( s1->v[31]       & 3) ^ (( s2->v[31]       & 3) << 2);
  r[125] = ((s1->v[31] >> 2) & 3) ^ (((s2->v[31] >> 2) & 3) << 2);
  r[126] = ((s1->v[31] >> 4) & 3) ^ (((s2->v[31] >> 4) & 3) << 2);
}
