/* $OpenBSD: xmss_hash.c,v 1.2 2018/02/26 03:56:44 dtucker Exp $ */
/*
hash.c version 20160722
Andreas Hülsing
Joost Rijneveld
Public domain.
*/

#include "includes.h"
#ifdef WITH_XMSS

#include "xmss_hash_address.h"
#include "xmss_commons.h"
#include "xmss_hash.h"

#include <stddef.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

int core_hash_SHA2(unsigned char *, const unsigned int, const unsigned char *,
    unsigned int, const unsigned char *, unsigned long long, unsigned int);

unsigned char* addr_to_byte(unsigned char *bytes, const uint32_t addr[8]){
#if IS_LITTLE_ENDIAN==1 
  int i = 0;
  for(i=0;i<8;i++)
    to_byte(bytes+i*4, addr[i],4);
  return bytes;  
#else
  memcpy(bytes, addr, 32);
  return bytes; 
#endif   
}

int core_hash_SHA2(unsigned char *out, const unsigned int type, const unsigned char *key, unsigned int keylen, const unsigned char *in, unsigned long long inlen, unsigned int n){  
  unsigned long long i = 0;
  unsigned char buf[inlen + n + keylen];
  
  // Input is (toByte(X, 32) || KEY || M) 
  
  // set toByte
  to_byte(buf, type, n);
  
  for (i=0; i < keylen; i++) {
    buf[i+n] = key[i];
  }
  
  for (i=0; i < inlen; i++) {
    buf[keylen + n + i] = in[i];
  }

  if (n == 32) {
    SHA256(buf, inlen + keylen + n, out);
    return 0;
  }
  else {
    if (n == 64) {
      SHA512(buf, inlen + keylen + n, out);
      return 0;
    }
  }
  return 1;
}

/**
 * Implements PRF
 */
int prf(unsigned char *out, const unsigned char *in, const unsigned char *key, unsigned int keylen)
{ 
  return core_hash_SHA2(out, 3, key, keylen, in, 32, keylen);
}

/*
 * Implemts H_msg
 */
int h_msg(unsigned char *out, const unsigned char *in, unsigned long long inlen, const unsigned char *key, const unsigned int keylen, const unsigned int n)
{
  if (keylen != 3*n){
    // H_msg takes 3n-bit keys, but n does not match the keylength of keylen
    return -1;
  }  
  return core_hash_SHA2(out, 2, key, keylen, in, inlen, n);
}

/**
 * We assume the left half is in in[0]...in[n-1]
 */
int hash_h(unsigned char *out, const unsigned char *in, const unsigned char *pub_seed, uint32_t addr[8], const unsigned int n)
{

  unsigned char buf[2*n];
  unsigned char key[n];
  unsigned char bitmask[2*n];
  unsigned char byte_addr[32];
  unsigned int i;

  setKeyAndMask(addr, 0);
  addr_to_byte(byte_addr, addr);
  prf(key, byte_addr, pub_seed, n);
  // Use MSB order
  setKeyAndMask(addr, 1);
  addr_to_byte(byte_addr, addr);
  prf(bitmask, byte_addr, pub_seed, n);
  setKeyAndMask(addr, 2);
  addr_to_byte(byte_addr, addr);
  prf(bitmask+n, byte_addr, pub_seed, n);
  for (i = 0; i < 2*n; i++) {
    buf[i] = in[i] ^ bitmask[i];
  }
  return core_hash_SHA2(out, 1, key, n, buf, 2*n, n);
}

int hash_f(unsigned char *out, const unsigned char *in, const unsigned char *pub_seed, uint32_t addr[8], const unsigned int n)
{
  unsigned char buf[n];
  unsigned char key[n];
  unsigned char bitmask[n];
  unsigned char byte_addr[32];
  unsigned int i;

  setKeyAndMask(addr, 0);  
  addr_to_byte(byte_addr, addr);  
  prf(key, byte_addr, pub_seed, n);
  
  setKeyAndMask(addr, 1);
  addr_to_byte(byte_addr, addr);
  prf(bitmask, byte_addr, pub_seed, n);
  
  for (i = 0; i < n; i++) {
    buf[i] = in[i] ^ bitmask[i];
  }
  return core_hash_SHA2(out, 0, key, n, buf, n, n);
}
#endif /* WITH_XMSS */
