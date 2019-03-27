/* $OpenBSD: xmss_hash_address.c,v 1.2 2018/02/26 03:56:44 dtucker Exp $ */
/*
hash_address.c version 20160722
Andreas Hülsing
Joost Rijneveld
Public domain.
*/
#include "includes.h"
#ifdef WITH_XMSS

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include "xmss_hash_address.h"	/* prototypes */

void setLayerADRS(uint32_t adrs[8], uint32_t layer){
  adrs[0] = layer;
}

void setTreeADRS(uint32_t adrs[8], uint64_t tree){
  adrs[1] = (uint32_t) (tree >> 32);
  adrs[2] = (uint32_t) tree;
}

void setType(uint32_t adrs[8], uint32_t type){
  adrs[3] = type;
  int i;
  for(i = 4; i < 8; i++){
    adrs[i] = 0;
  }
}

void setKeyAndMask(uint32_t adrs[8], uint32_t keyAndMask){
  adrs[7] = keyAndMask;
}

// OTS

void setOTSADRS(uint32_t adrs[8], uint32_t ots){
  adrs[4] = ots;
}

void setChainADRS(uint32_t adrs[8], uint32_t chain){
  adrs[5] = chain;
}

void setHashADRS(uint32_t adrs[8], uint32_t hash){
  adrs[6] = hash;
}

// L-tree

void setLtreeADRS(uint32_t adrs[8], uint32_t ltree){
  adrs[4] = ltree;
}

// Hash Tree & L-tree

void setTreeHeight(uint32_t adrs[8], uint32_t treeHeight){
  adrs[5] = treeHeight;
}

void setTreeIndex(uint32_t adrs[8], uint32_t treeIndex){
  adrs[6] = treeIndex;
}
#endif /* WITH_XMSS */
