#ifdef WITH_XMSS
/* $OpenBSD: xmss_hash.h,v 1.2 2018/02/26 03:56:44 dtucker Exp $ */
/*
hash.h version 20160722
Andreas Hülsing
Joost Rijneveld
Public domain.
*/

#ifndef HASH_H
#define HASH_H

#define IS_LITTLE_ENDIAN 1

unsigned char* addr_to_byte(unsigned char *bytes, const uint32_t addr[8]);
int prf(unsigned char *out, const unsigned char *in, const unsigned char *key, unsigned int keylen);
int h_msg(unsigned char *out,const unsigned char *in,unsigned long long inlen, const unsigned char *key, const unsigned int keylen, const unsigned int n);
int hash_h(unsigned char *out, const unsigned char *in, const unsigned char *pub_seed, uint32_t addr[8], const unsigned int n);
int hash_f(unsigned char *out, const unsigned char *in, const unsigned char *pub_seed, uint32_t addr[8], const unsigned int n);

#endif
#endif /* WITH_XMSS */
