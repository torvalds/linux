/* $OpenBSD: xmss_fast.c,v 1.3 2018/03/22 07:06:11 markus Exp $ */
/*
xmss_fast.c version 20160722
Andreas Hülsing
Joost Rijneveld
Public domain.
*/

#include "includes.h"
#ifdef WITH_XMSS

#include <stdlib.h>
#include <string.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include "xmss_fast.h"
#include "crypto_api.h"
#include "xmss_wots.h"
#include "xmss_hash.h"

#include "xmss_commons.h"
#include "xmss_hash_address.h"
// For testing
#include "stdio.h"



/**
 * Used for pseudorandom keygeneration,
 * generates the seed for the WOTS keypair at address addr
 *
 * takes n byte sk_seed and returns n byte seed using 32 byte address addr.
 */
static void get_seed(unsigned char *seed, const unsigned char *sk_seed, int n, uint32_t addr[8])
{
  unsigned char bytes[32];
  // Make sure that chain addr, hash addr, and key bit are 0!
  setChainADRS(addr,0);
  setHashADRS(addr,0);
  setKeyAndMask(addr,0);
  // Generate pseudorandom value
  addr_to_byte(bytes, addr);
  prf(seed, bytes, sk_seed, n);
}

/**
 * Initialize xmss params struct
 * parameter names are the same as in the draft
 * parameter k is K as used in the BDS algorithm
 */
int xmss_set_params(xmss_params *params, int n, int h, int w, int k)
{
  if (k >= h || k < 2 || (h - k) % 2) {
    fprintf(stderr, "For BDS traversal, H - K must be even, with H > K >= 2!\n");
    return 1;
  }
  params->h = h;
  params->n = n;
  params->k = k;
  wots_params wots_par;
  wots_set_params(&wots_par, n, w);
  params->wots_par = wots_par;
  return 0;
}

/**
 * Initialize BDS state struct
 * parameter names are the same as used in the description of the BDS traversal
 */
void xmss_set_bds_state(bds_state *state, unsigned char *stack, int stackoffset, unsigned char *stacklevels, unsigned char *auth, unsigned char *keep, treehash_inst *treehash, unsigned char *retain, int next_leaf)
{
  state->stack = stack;
  state->stackoffset = stackoffset;
  state->stacklevels = stacklevels;
  state->auth = auth;
  state->keep = keep;
  state->treehash = treehash;
  state->retain = retain;
  state->next_leaf = next_leaf;
}

/**
 * Initialize xmssmt_params struct
 * parameter names are the same as in the draft
 *
 * Especially h is the total tree height, i.e. the XMSS trees have height h/d
 */
int xmssmt_set_params(xmssmt_params *params, int n, int h, int d, int w, int k)
{
  if (h % d) {
    fprintf(stderr, "d must divide h without remainder!\n");
    return 1;
  }
  params->h = h;
  params->d = d;
  params->n = n;
  params->index_len = (h + 7) / 8;
  xmss_params xmss_par;
  if (xmss_set_params(&xmss_par, n, (h/d), w, k)) {
    return 1;
  }
  params->xmss_par = xmss_par;
  return 0;
}

/**
 * Computes a leaf from a WOTS public key using an L-tree.
 */
static void l_tree(unsigned char *leaf, unsigned char *wots_pk, const xmss_params *params, const unsigned char *pub_seed, uint32_t addr[8])
{
  unsigned int l = params->wots_par.len;
  unsigned int n = params->n;
  uint32_t i = 0;
  uint32_t height = 0;
  uint32_t bound;

  //ADRS.setTreeHeight(0);
  setTreeHeight(addr, height);
  
  while (l > 1) {
     bound = l >> 1; //floor(l / 2);
     for (i = 0; i < bound; i++) {
       //ADRS.setTreeIndex(i);
       setTreeIndex(addr, i);
       //wots_pk[i] = RAND_HASH(pk[2i], pk[2i + 1], SEED, ADRS);
       hash_h(wots_pk+i*n, wots_pk+i*2*n, pub_seed, addr, n);
     }
     //if ( l % 2 == 1 ) {
     if (l & 1) {
       //pk[floor(l / 2) + 1] = pk[l];
       memcpy(wots_pk+(l>>1)*n, wots_pk+(l-1)*n, n);
       //l = ceil(l / 2);
       l=(l>>1)+1;
     }
     else {
       //l = ceil(l / 2);
       l=(l>>1);
     }
     //ADRS.setTreeHeight(ADRS.getTreeHeight() + 1);
     height++;
     setTreeHeight(addr, height);
   }
   //return pk[0];
   memcpy(leaf, wots_pk, n);
}

/**
 * Computes the leaf at a given address. First generates the WOTS key pair, then computes leaf using l_tree. As this happens position independent, we only require that addr encodes the right ltree-address.
 */
static void gen_leaf_wots(unsigned char *leaf, const unsigned char *sk_seed, const xmss_params *params, const unsigned char *pub_seed, uint32_t ltree_addr[8], uint32_t ots_addr[8])
{
  unsigned char seed[params->n];
  unsigned char pk[params->wots_par.keysize];

  get_seed(seed, sk_seed, params->n, ots_addr);
  wots_pkgen(pk, seed, &(params->wots_par), pub_seed, ots_addr);

  l_tree(leaf, pk, params, pub_seed, ltree_addr);
}

static int treehash_minheight_on_stack(bds_state* state, const xmss_params *params, const treehash_inst *treehash) {
  unsigned int r = params->h, i;
  for (i = 0; i < treehash->stackusage; i++) {
    if (state->stacklevels[state->stackoffset - i - 1] < r) {
      r = state->stacklevels[state->stackoffset - i - 1];
    }
  }
  return r;
}

/**
 * Merkle's TreeHash algorithm. The address only needs to initialize the first 78 bits of addr. Everything else will be set by treehash.
 * Currently only used for key generation.
 *
 */
static void treehash_setup(unsigned char *node, int height, int index, bds_state *state, const unsigned char *sk_seed, const xmss_params *params, const unsigned char *pub_seed, const uint32_t addr[8])
{
  unsigned int idx = index;
  unsigned int n = params->n;
  unsigned int h = params->h;
  unsigned int k = params->k;
  // use three different addresses because at this point we use all three formats in parallel
  uint32_t ots_addr[8];
  uint32_t ltree_addr[8];
  uint32_t  node_addr[8];
  // only copy layer and tree address parts
  memcpy(ots_addr, addr, 12);
  // type = ots
  setType(ots_addr, 0);
  memcpy(ltree_addr, addr, 12);
  setType(ltree_addr, 1);
  memcpy(node_addr, addr, 12);
  setType(node_addr, 2);

  uint32_t lastnode, i;
  unsigned char stack[(height+1)*n];
  unsigned int stacklevels[height+1];
  unsigned int stackoffset=0;
  unsigned int nodeh;

  lastnode = idx+(1<<height);

  for (i = 0; i < h-k; i++) {
    state->treehash[i].h = i;
    state->treehash[i].completed = 1;
    state->treehash[i].stackusage = 0;
  }

  i = 0;
  for (; idx < lastnode; idx++) {
    setLtreeADRS(ltree_addr, idx);
    setOTSADRS(ots_addr, idx);
    gen_leaf_wots(stack+stackoffset*n, sk_seed, params, pub_seed, ltree_addr, ots_addr);
    stacklevels[stackoffset] = 0;
    stackoffset++;
    if (h - k > 0 && i == 3) {
      memcpy(state->treehash[0].node, stack+stackoffset*n, n);
    }
    while (stackoffset>1 && stacklevels[stackoffset-1] == stacklevels[stackoffset-2])
    {
      nodeh = stacklevels[stackoffset-1];
      if (i >> nodeh == 1) {
        memcpy(state->auth + nodeh*n, stack+(stackoffset-1)*n, n);
      }
      else {
        if (nodeh < h - k && i >> nodeh == 3) {
          memcpy(state->treehash[nodeh].node, stack+(stackoffset-1)*n, n);
        }
        else if (nodeh >= h - k) {
          memcpy(state->retain + ((1 << (h - 1 - nodeh)) + nodeh - h + (((i >> nodeh) - 3) >> 1)) * n, stack+(stackoffset-1)*n, n);
        }
      }
      setTreeHeight(node_addr, stacklevels[stackoffset-1]);
      setTreeIndex(node_addr, (idx >> (stacklevels[stackoffset-1]+1)));
      hash_h(stack+(stackoffset-2)*n, stack+(stackoffset-2)*n, pub_seed,
          node_addr, n);
      stacklevels[stackoffset-2]++;
      stackoffset--;
    }
    i++;
  }

  for (i = 0; i < n; i++)
    node[i] = stack[i];
}

static void treehash_update(treehash_inst *treehash, bds_state *state, const unsigned char *sk_seed, const xmss_params *params, const unsigned char *pub_seed, const uint32_t addr[8]) {
  int n = params->n;

  uint32_t ots_addr[8];
  uint32_t ltree_addr[8];
  uint32_t  node_addr[8];
  // only copy layer and tree address parts
  memcpy(ots_addr, addr, 12);
  // type = ots
  setType(ots_addr, 0);
  memcpy(ltree_addr, addr, 12);
  setType(ltree_addr, 1);
  memcpy(node_addr, addr, 12);
  setType(node_addr, 2);

  setLtreeADRS(ltree_addr, treehash->next_idx);
  setOTSADRS(ots_addr, treehash->next_idx);

  unsigned char nodebuffer[2 * n];
  unsigned int nodeheight = 0;
  gen_leaf_wots(nodebuffer, sk_seed, params, pub_seed, ltree_addr, ots_addr);
  while (treehash->stackusage > 0 && state->stacklevels[state->stackoffset-1] == nodeheight) {
    memcpy(nodebuffer + n, nodebuffer, n);
    memcpy(nodebuffer, state->stack + (state->stackoffset-1)*n, n);
    setTreeHeight(node_addr, nodeheight);
    setTreeIndex(node_addr, (treehash->next_idx >> (nodeheight+1)));
    hash_h(nodebuffer, nodebuffer, pub_seed, node_addr, n);
    nodeheight++;
    treehash->stackusage--;
    state->stackoffset--;
  }
  if (nodeheight == treehash->h) { // this also implies stackusage == 0
    memcpy(treehash->node, nodebuffer, n);
    treehash->completed = 1;
  }
  else {
    memcpy(state->stack + state->stackoffset*n, nodebuffer, n);
    treehash->stackusage++;
    state->stacklevels[state->stackoffset] = nodeheight;
    state->stackoffset++;
    treehash->next_idx++;
  }
}

/**
 * Computes a root node given a leaf and an authapth
 */
static void validate_authpath(unsigned char *root, const unsigned char *leaf, unsigned long leafidx, const unsigned char *authpath, const xmss_params *params, const unsigned char *pub_seed, uint32_t addr[8])
{
  unsigned int n = params->n;

  uint32_t i, j;
  unsigned char buffer[2*n];

  // If leafidx is odd (last bit = 1), current path element is a right child and authpath has to go to the left.
  // Otherwise, it is the other way around
  if (leafidx & 1) {
    for (j = 0; j < n; j++)
      buffer[n+j] = leaf[j];
    for (j = 0; j < n; j++)
      buffer[j] = authpath[j];
  }
  else {
    for (j = 0; j < n; j++)
      buffer[j] = leaf[j];
    for (j = 0; j < n; j++)
      buffer[n+j] = authpath[j];
  }
  authpath += n;

  for (i=0; i < params->h-1; i++) {
    setTreeHeight(addr, i);
    leafidx >>= 1;
    setTreeIndex(addr, leafidx);
    if (leafidx&1) {
      hash_h(buffer+n, buffer, pub_seed, addr, n);
      for (j = 0; j < n; j++)
        buffer[j] = authpath[j];
    }
    else {
      hash_h(buffer, buffer, pub_seed, addr, n);
      for (j = 0; j < n; j++)
        buffer[j+n] = authpath[j];
    }
    authpath += n;
  }
  setTreeHeight(addr, (params->h-1));
  leafidx >>= 1;
  setTreeIndex(addr, leafidx);
  hash_h(root, buffer, pub_seed, addr, n);
}

/**
 * Performs one treehash update on the instance that needs it the most.
 * Returns 1 if such an instance was not found
 **/
static char bds_treehash_update(bds_state *state, unsigned int updates, const unsigned char *sk_seed, const xmss_params *params, unsigned char *pub_seed, const uint32_t addr[8]) {
  uint32_t i, j;
  unsigned int level, l_min, low;
  unsigned int h = params->h;
  unsigned int k = params->k;
  unsigned int used = 0;

  for (j = 0; j < updates; j++) {
    l_min = h;
    level = h - k;
    for (i = 0; i < h - k; i++) {
      if (state->treehash[i].completed) {
        low = h;
      }
      else if (state->treehash[i].stackusage == 0) {
        low = i;
      }
      else {
        low = treehash_minheight_on_stack(state, params, &(state->treehash[i]));
      }
      if (low < l_min) {
        level = i;
        l_min = low;
      }
    }
    if (level == h - k) {
      break;
    }
    treehash_update(&(state->treehash[level]), state, sk_seed, params, pub_seed, addr);
    used++;
  }
  return updates - used;
}

/**
 * Updates the state (typically NEXT_i) by adding a leaf and updating the stack
 * Returns 1 if all leaf nodes have already been processed
 **/
static char bds_state_update(bds_state *state, const unsigned char *sk_seed, const xmss_params *params, unsigned char *pub_seed, const uint32_t addr[8]) {
  uint32_t ltree_addr[8];
  uint32_t node_addr[8];
  uint32_t ots_addr[8];

  int n = params->n;
  int h = params->h;
  int k = params->k;

  int nodeh;
  int idx = state->next_leaf;
  if (idx == 1 << h) {
    return 1;
  }

  // only copy layer and tree address parts
  memcpy(ots_addr, addr, 12);
  // type = ots
  setType(ots_addr, 0);
  memcpy(ltree_addr, addr, 12);
  setType(ltree_addr, 1);
  memcpy(node_addr, addr, 12);
  setType(node_addr, 2);
  
  setOTSADRS(ots_addr, idx);
  setLtreeADRS(ltree_addr, idx);

  gen_leaf_wots(state->stack+state->stackoffset*n, sk_seed, params, pub_seed, ltree_addr, ots_addr);

  state->stacklevels[state->stackoffset] = 0;
  state->stackoffset++;
  if (h - k > 0 && idx == 3) {
    memcpy(state->treehash[0].node, state->stack+state->stackoffset*n, n);
  }
  while (state->stackoffset>1 && state->stacklevels[state->stackoffset-1] == state->stacklevels[state->stackoffset-2]) {
    nodeh = state->stacklevels[state->stackoffset-1];
    if (idx >> nodeh == 1) {
      memcpy(state->auth + nodeh*n, state->stack+(state->stackoffset-1)*n, n);
    }
    else {
      if (nodeh < h - k && idx >> nodeh == 3) {
        memcpy(state->treehash[nodeh].node, state->stack+(state->stackoffset-1)*n, n);
      }
      else if (nodeh >= h - k) {
        memcpy(state->retain + ((1 << (h - 1 - nodeh)) + nodeh - h + (((idx >> nodeh) - 3) >> 1)) * n, state->stack+(state->stackoffset-1)*n, n);
      }
    }
    setTreeHeight(node_addr, state->stacklevels[state->stackoffset-1]);
    setTreeIndex(node_addr, (idx >> (state->stacklevels[state->stackoffset-1]+1)));
    hash_h(state->stack+(state->stackoffset-2)*n, state->stack+(state->stackoffset-2)*n, pub_seed, node_addr, n);

    state->stacklevels[state->stackoffset-2]++;
    state->stackoffset--;
  }
  state->next_leaf++;
  return 0;
}

/**
 * Returns the auth path for node leaf_idx and computes the auth path for the
 * next leaf node, using the algorithm described by Buchmann, Dahmen and Szydlo
 * in "Post Quantum Cryptography", Springer 2009.
 */
static void bds_round(bds_state *state, const unsigned long leaf_idx, const unsigned char *sk_seed, const xmss_params *params, unsigned char *pub_seed, uint32_t addr[8])
{
  unsigned int i;
  unsigned int n = params->n;
  unsigned int h = params->h;
  unsigned int k = params->k;

  unsigned int tau = h;
  unsigned int startidx;
  unsigned int offset, rowidx;
  unsigned char buf[2 * n];

  uint32_t ots_addr[8];
  uint32_t ltree_addr[8];
  uint32_t  node_addr[8];
  // only copy layer and tree address parts
  memcpy(ots_addr, addr, 12);
  // type = ots
  setType(ots_addr, 0);
  memcpy(ltree_addr, addr, 12);
  setType(ltree_addr, 1);
  memcpy(node_addr, addr, 12);
  setType(node_addr, 2);

  for (i = 0; i < h; i++) {
    if (! ((leaf_idx >> i) & 1)) {
      tau = i;
      break;
    }
  }

  if (tau > 0) {
    memcpy(buf,     state->auth + (tau-1) * n, n);
    // we need to do this before refreshing state->keep to prevent overwriting
    memcpy(buf + n, state->keep + ((tau-1) >> 1) * n, n);
  }
  if (!((leaf_idx >> (tau + 1)) & 1) && (tau < h - 1)) {
    memcpy(state->keep + (tau >> 1)*n, state->auth + tau*n, n);
  }
  if (tau == 0) {
    setLtreeADRS(ltree_addr, leaf_idx);
    setOTSADRS(ots_addr, leaf_idx);
    gen_leaf_wots(state->auth, sk_seed, params, pub_seed, ltree_addr, ots_addr);
  }
  else {
    setTreeHeight(node_addr, (tau-1));
    setTreeIndex(node_addr, leaf_idx >> tau);
    hash_h(state->auth + tau * n, buf, pub_seed, node_addr, n);
    for (i = 0; i < tau; i++) {
      if (i < h - k) {
        memcpy(state->auth + i * n, state->treehash[i].node, n);
      }
      else {
        offset = (1 << (h - 1 - i)) + i - h;
        rowidx = ((leaf_idx >> i) - 1) >> 1;
        memcpy(state->auth + i * n, state->retain + (offset + rowidx) * n, n);
      }
    }

    for (i = 0; i < ((tau < h - k) ? tau : (h - k)); i++) {
      startidx = leaf_idx + 1 + 3 * (1 << i);
      if (startidx < 1U << h) {
        state->treehash[i].h = i;
        state->treehash[i].next_idx = startidx;
        state->treehash[i].completed = 0;
        state->treehash[i].stackusage = 0;
      }
    }
  }
}

/*
 * Generates a XMSS key pair for a given parameter set.
 * Format sk: [(32bit) idx || SK_SEED || SK_PRF || PUB_SEED || root]
 * Format pk: [root || PUB_SEED] omitting algo oid.
 */
int xmss_keypair(unsigned char *pk, unsigned char *sk, bds_state *state, xmss_params *params)
{
  unsigned int n = params->n;
  // Set idx = 0
  sk[0] = 0;
  sk[1] = 0;
  sk[2] = 0;
  sk[3] = 0;
  // Init SK_SEED (n byte), SK_PRF (n byte), and PUB_SEED (n byte)
  randombytes(sk+4, 3*n);
  // Copy PUB_SEED to public key
  memcpy(pk+n, sk+4+2*n, n);

  uint32_t addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  // Compute root
  treehash_setup(pk, params->h, 0, state, sk+4, params, sk+4+2*n, addr);
  // copy root to sk
  memcpy(sk+4+3*n, pk, n);
  return 0;
}

/**
 * Signs a message.
 * Returns
 * 1. an array containing the signature followed by the message AND
 * 2. an updated secret key!
 *
 */
int xmss_sign(unsigned char *sk, bds_state *state, unsigned char *sig_msg, unsigned long long *sig_msg_len, const unsigned char *msg, unsigned long long msglen, const xmss_params *params)
{
  unsigned int h = params->h;
  unsigned int n = params->n;
  unsigned int k = params->k;
  uint16_t i = 0;

  // Extract SK
  unsigned long idx = ((unsigned long)sk[0] << 24) | ((unsigned long)sk[1] << 16) | ((unsigned long)sk[2] << 8) | sk[3];
  unsigned char sk_seed[n];
  memcpy(sk_seed, sk+4, n);
  unsigned char sk_prf[n];
  memcpy(sk_prf, sk+4+n, n);
  unsigned char pub_seed[n];
  memcpy(pub_seed, sk+4+2*n, n);
  
  // index as 32 bytes string
  unsigned char idx_bytes_32[32];
  to_byte(idx_bytes_32, idx, 32);
  
  unsigned char hash_key[3*n]; 
  
  // Update SK
  sk[0] = ((idx + 1) >> 24) & 255;
  sk[1] = ((idx + 1) >> 16) & 255;
  sk[2] = ((idx + 1) >> 8) & 255;
  sk[3] = (idx + 1) & 255;
  // -- Secret key for this non-forward-secure version is now updated.
  // -- A productive implementation should use a file handle instead and write the updated secret key at this point!

  // Init working params
  unsigned char R[n];
  unsigned char msg_h[n];
  unsigned char ots_seed[n];
  uint32_t ots_addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  // ---------------------------------
  // Message Hashing
  // ---------------------------------

  // Message Hash:
  // First compute pseudorandom value
  prf(R, idx_bytes_32, sk_prf, n);
  // Generate hash key (R || root || idx)
  memcpy(hash_key, R, n);
  memcpy(hash_key+n, sk+4+3*n, n);
  to_byte(hash_key+2*n, idx, n);
  // Then use it for message digest
  h_msg(msg_h, msg, msglen, hash_key, 3*n, n);

  // Start collecting signature
  *sig_msg_len = 0;

  // Copy index to signature
  sig_msg[0] = (idx >> 24) & 255;
  sig_msg[1] = (idx >> 16) & 255;
  sig_msg[2] = (idx >> 8) & 255;
  sig_msg[3] = idx & 255;

  sig_msg += 4;
  *sig_msg_len += 4;

  // Copy R to signature
  for (i = 0; i < n; i++)
    sig_msg[i] = R[i];

  sig_msg += n;
  *sig_msg_len += n;

  // ----------------------------------
  // Now we start to "really sign"
  // ----------------------------------

  // Prepare Address
  setType(ots_addr, 0);
  setOTSADRS(ots_addr, idx);

  // Compute seed for OTS key pair
  get_seed(ots_seed, sk_seed, n, ots_addr);

  // Compute WOTS signature
  wots_sign(sig_msg, msg_h, ots_seed, &(params->wots_par), pub_seed, ots_addr);

  sig_msg += params->wots_par.keysize;
  *sig_msg_len += params->wots_par.keysize;

  // the auth path was already computed during the previous round
  memcpy(sig_msg, state->auth, h*n);

  if (idx < (1U << h) - 1) {
    bds_round(state, idx, sk_seed, params, pub_seed, ots_addr);
    bds_treehash_update(state, (h - k) >> 1, sk_seed, params, pub_seed, ots_addr);
  }

/* TODO: save key/bds state here! */

  sig_msg += params->h*n;
  *sig_msg_len += params->h*n;

  //Whipe secret elements?
  //zerobytes(tsk, CRYPTO_SECRETKEYBYTES);


  memcpy(sig_msg, msg, msglen);
  *sig_msg_len += msglen;

  return 0;
}

/**
 * Verifies a given message signature pair under a given public key.
 */
int xmss_sign_open(unsigned char *msg, unsigned long long *msglen, const unsigned char *sig_msg, unsigned long long sig_msg_len, const unsigned char *pk, const xmss_params *params)
{
  unsigned int n = params->n;

  unsigned long long i, m_len;
  unsigned long idx=0;
  unsigned char wots_pk[params->wots_par.keysize];
  unsigned char pkhash[n];
  unsigned char root[n];
  unsigned char msg_h[n];
  unsigned char hash_key[3*n];

  unsigned char pub_seed[n];
  memcpy(pub_seed, pk+n, n);

  // Init addresses
  uint32_t ots_addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint32_t ltree_addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint32_t node_addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  setType(ots_addr, 0);
  setType(ltree_addr, 1);
  setType(node_addr, 2);

  // Extract index
  idx = ((unsigned long)sig_msg[0] << 24) | ((unsigned long)sig_msg[1] << 16) | ((unsigned long)sig_msg[2] << 8) | sig_msg[3];
  printf("verify:: idx = %lu\n", idx);
  
  // Generate hash key (R || root || idx)
  memcpy(hash_key, sig_msg+4,n);
  memcpy(hash_key+n, pk, n);
  to_byte(hash_key+2*n, idx, n);
  
  sig_msg += (n+4);
  sig_msg_len -= (n+4);

  // hash message 
  unsigned long long tmp_sig_len = params->wots_par.keysize+params->h*n;
  m_len = sig_msg_len - tmp_sig_len;
  h_msg(msg_h, sig_msg + tmp_sig_len, m_len, hash_key, 3*n, n);

  //-----------------------
  // Verify signature
  //-----------------------

  // Prepare Address
  setOTSADRS(ots_addr, idx);
  // Check WOTS signature
  wots_pkFromSig(wots_pk, sig_msg, msg_h, &(params->wots_par), pub_seed, ots_addr);

  sig_msg += params->wots_par.keysize;
  sig_msg_len -= params->wots_par.keysize;

  // Compute Ltree
  setLtreeADRS(ltree_addr, idx);
  l_tree(pkhash, wots_pk, params, pub_seed, ltree_addr);

  // Compute root
  validate_authpath(root, pkhash, idx, sig_msg, params, pub_seed, node_addr);

  sig_msg += params->h*n;
  sig_msg_len -= params->h*n;

  for (i = 0; i < n; i++)
    if (root[i] != pk[i])
      goto fail;

  *msglen = sig_msg_len;
  for (i = 0; i < *msglen; i++)
    msg[i] = sig_msg[i];

  return 0;


fail:
  *msglen = sig_msg_len;
  for (i = 0; i < *msglen; i++)
    msg[i] = 0;
  *msglen = -1;
  return -1;
}

/*
 * Generates a XMSSMT key pair for a given parameter set.
 * Format sk: [(ceil(h/8) bit) idx || SK_SEED || SK_PRF || PUB_SEED || root]
 * Format pk: [root || PUB_SEED] omitting algo oid.
 */
int xmssmt_keypair(unsigned char *pk, unsigned char *sk, bds_state *states, unsigned char *wots_sigs, xmssmt_params *params)
{
  unsigned int n = params->n;
  unsigned int i;
  unsigned char ots_seed[params->n];
  // Set idx = 0
  for (i = 0; i < params->index_len; i++) {
    sk[i] = 0;
  }
  // Init SK_SEED (n byte), SK_PRF (n byte), and PUB_SEED (n byte)
  randombytes(sk+params->index_len, 3*n);
  // Copy PUB_SEED to public key
  memcpy(pk+n, sk+params->index_len+2*n, n);

  // Set address to point on the single tree on layer d-1
  uint32_t addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  setLayerADRS(addr, (params->d-1));
  // Set up state and compute wots signatures for all but topmost tree root
  for (i = 0; i < params->d - 1; i++) {
    // Compute seed for OTS key pair
    treehash_setup(pk, params->xmss_par.h, 0, states + i, sk+params->index_len, &(params->xmss_par), pk+n, addr);
    setLayerADRS(addr, (i+1));
    get_seed(ots_seed, sk+params->index_len, n, addr);
    wots_sign(wots_sigs + i*params->xmss_par.wots_par.keysize, pk, ots_seed, &(params->xmss_par.wots_par), pk+n, addr);
  }
  treehash_setup(pk, params->xmss_par.h, 0, states + i, sk+params->index_len, &(params->xmss_par), pk+n, addr);
  memcpy(sk+params->index_len+3*n, pk, n);
  return 0;
}

/**
 * Signs a message.
 * Returns
 * 1. an array containing the signature followed by the message AND
 * 2. an updated secret key!
 *
 */
int xmssmt_sign(unsigned char *sk, bds_state *states, unsigned char *wots_sigs, unsigned char *sig_msg, unsigned long long *sig_msg_len, const unsigned char *msg, unsigned long long msglen, const xmssmt_params *params)
{
  unsigned int n = params->n;
  
  unsigned int tree_h = params->xmss_par.h;
  unsigned int h = params->h;
  unsigned int k = params->xmss_par.k;
  unsigned int idx_len = params->index_len;
  uint64_t idx_tree;
  uint32_t idx_leaf;
  uint64_t i, j;
  int needswap_upto = -1;
  unsigned int updates;

  unsigned char sk_seed[n];
  unsigned char sk_prf[n];
  unsigned char pub_seed[n];
  // Init working params
  unsigned char R[n];
  unsigned char msg_h[n];
  unsigned char hash_key[3*n];
  unsigned char ots_seed[n];
  uint32_t addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint32_t ots_addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  unsigned char idx_bytes_32[32];
  bds_state tmp;

  // Extract SK 
  unsigned long long idx = 0;
  for (i = 0; i < idx_len; i++) {
    idx |= ((unsigned long long)sk[i]) << 8*(idx_len - 1 - i);
  }

  memcpy(sk_seed, sk+idx_len, n);
  memcpy(sk_prf, sk+idx_len+n, n);
  memcpy(pub_seed, sk+idx_len+2*n, n);

  // Update SK
  for (i = 0; i < idx_len; i++) {
    sk[i] = ((idx + 1) >> 8*(idx_len - 1 - i)) & 255;
  }
  // -- Secret key for this non-forward-secure version is now updated.
  // -- A productive implementation should use a file handle instead and write the updated secret key at this point!


  // ---------------------------------
  // Message Hashing
  // ---------------------------------

  // Message Hash:
  // First compute pseudorandom value
  to_byte(idx_bytes_32, idx, 32);
  prf(R, idx_bytes_32, sk_prf, n);
  // Generate hash key (R || root || idx)
  memcpy(hash_key, R, n);
  memcpy(hash_key+n, sk+idx_len+3*n, n);
  to_byte(hash_key+2*n, idx, n);
  
  // Then use it for message digest
  h_msg(msg_h, msg, msglen, hash_key, 3*n, n);

  // Start collecting signature
  *sig_msg_len = 0;

  // Copy index to signature
  for (i = 0; i < idx_len; i++) {
    sig_msg[i] = (idx >> 8*(idx_len - 1 - i)) & 255;
  }

  sig_msg += idx_len;
  *sig_msg_len += idx_len;

  // Copy R to signature
  for (i = 0; i < n; i++)
    sig_msg[i] = R[i];

  sig_msg += n;
  *sig_msg_len += n;

  // ----------------------------------
  // Now we start to "really sign"
  // ----------------------------------

  // Handle lowest layer separately as it is slightly different...

  // Prepare Address
  setType(ots_addr, 0);
  idx_tree = idx >> tree_h;
  idx_leaf = (idx & ((1 << tree_h)-1));
  setLayerADRS(ots_addr, 0);
  setTreeADRS(ots_addr, idx_tree);
  setOTSADRS(ots_addr, idx_leaf);

  // Compute seed for OTS key pair
  get_seed(ots_seed, sk_seed, n, ots_addr);

  // Compute WOTS signature
  wots_sign(sig_msg, msg_h, ots_seed, &(params->xmss_par.wots_par), pub_seed, ots_addr);

  sig_msg += params->xmss_par.wots_par.keysize;
  *sig_msg_len += params->xmss_par.wots_par.keysize;

  memcpy(sig_msg, states[0].auth, tree_h*n);
  sig_msg += tree_h*n;
  *sig_msg_len += tree_h*n;

  // prepare signature of remaining layers
  for (i = 1; i < params->d; i++) {
    // put WOTS signature in place
    memcpy(sig_msg, wots_sigs + (i-1)*params->xmss_par.wots_par.keysize, params->xmss_par.wots_par.keysize);

    sig_msg += params->xmss_par.wots_par.keysize;
    *sig_msg_len += params->xmss_par.wots_par.keysize;

    // put AUTH nodes in place
    memcpy(sig_msg, states[i].auth, tree_h*n);
    sig_msg += tree_h*n;
    *sig_msg_len += tree_h*n;
  }

  updates = (tree_h - k) >> 1;

  setTreeADRS(addr, (idx_tree + 1));
  // mandatory update for NEXT_0 (does not count towards h-k/2) if NEXT_0 exists
  if ((1 + idx_tree) * (1 << tree_h) + idx_leaf < (1ULL << h)) {
    bds_state_update(&states[params->d], sk_seed, &(params->xmss_par), pub_seed, addr);
  }

  for (i = 0; i < params->d; i++) {
    // check if we're not at the end of a tree
    if (! (((idx + 1) & ((1ULL << ((i+1)*tree_h)) - 1)) == 0)) {
      idx_leaf = (idx >> (tree_h * i)) & ((1 << tree_h)-1);
      idx_tree = (idx >> (tree_h * (i+1)));
      setLayerADRS(addr, i);
      setTreeADRS(addr, idx_tree);
      if (i == (unsigned int) (needswap_upto + 1)) {
        bds_round(&states[i], idx_leaf, sk_seed, &(params->xmss_par), pub_seed, addr);
      }
      updates = bds_treehash_update(&states[i], updates, sk_seed, &(params->xmss_par), pub_seed, addr);
      setTreeADRS(addr, (idx_tree + 1));
      // if a NEXT-tree exists for this level;
      if ((1 + idx_tree) * (1 << tree_h) + idx_leaf < (1ULL << (h - tree_h * i))) {
        if (i > 0 && updates > 0 && states[params->d + i].next_leaf < (1ULL << h)) {
          bds_state_update(&states[params->d + i], sk_seed, &(params->xmss_par), pub_seed, addr);
          updates--;
        }
      }
    }
    else if (idx < (1ULL << h) - 1) {
      memcpy(&tmp, states+params->d + i, sizeof(bds_state));
      memcpy(states+params->d + i, states + i, sizeof(bds_state));
      memcpy(states + i, &tmp, sizeof(bds_state));

      setLayerADRS(ots_addr, (i+1));
      setTreeADRS(ots_addr, ((idx + 1) >> ((i+2) * tree_h)));
      setOTSADRS(ots_addr, (((idx >> ((i+1) * tree_h)) + 1) & ((1 << tree_h)-1)));

      get_seed(ots_seed, sk+params->index_len, n, ots_addr);
      wots_sign(wots_sigs + i*params->xmss_par.wots_par.keysize, states[i].stack, ots_seed, &(params->xmss_par.wots_par), pub_seed, ots_addr);

      states[params->d + i].stackoffset = 0;
      states[params->d + i].next_leaf = 0;

      updates--; // WOTS-signing counts as one update
      needswap_upto = i;
      for (j = 0; j < tree_h-k; j++) {
        states[i].treehash[j].completed = 1;
      }
    }
  }

  //Whipe secret elements?
  //zerobytes(tsk, CRYPTO_SECRETKEYBYTES);

  memcpy(sig_msg, msg, msglen);
  *sig_msg_len += msglen;

  return 0;
}

/**
 * Verifies a given message signature pair under a given public key.
 */
int xmssmt_sign_open(unsigned char *msg, unsigned long long *msglen, const unsigned char *sig_msg, unsigned long long sig_msg_len, const unsigned char *pk, const xmssmt_params *params)
{
  unsigned int n = params->n;

  unsigned int tree_h = params->xmss_par.h;
  unsigned int idx_len = params->index_len;
  uint64_t idx_tree;
  uint32_t idx_leaf;

  unsigned long long i, m_len;
  unsigned long long idx=0;
  unsigned char wots_pk[params->xmss_par.wots_par.keysize];
  unsigned char pkhash[n];
  unsigned char root[n];
  unsigned char msg_h[n];
  unsigned char hash_key[3*n];

  unsigned char pub_seed[n];
  memcpy(pub_seed, pk+n, n);

  // Init addresses
  uint32_t ots_addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint32_t ltree_addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint32_t node_addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  // Extract index
  for (i = 0; i < idx_len; i++) {
    idx |= ((unsigned long long)sig_msg[i]) << (8*(idx_len - 1 - i));
  }
  printf("verify:: idx = %llu\n", idx);
  sig_msg += idx_len;
  sig_msg_len -= idx_len;
  
  // Generate hash key (R || root || idx)
  memcpy(hash_key, sig_msg,n);
  memcpy(hash_key+n, pk, n);
  to_byte(hash_key+2*n, idx, n);

  sig_msg += n;
  sig_msg_len -= n;
  

  // hash message (recall, R is now on pole position at sig_msg
  unsigned long long tmp_sig_len = (params->d * params->xmss_par.wots_par.keysize) + (params->h * n);
  m_len = sig_msg_len - tmp_sig_len;
  h_msg(msg_h, sig_msg + tmp_sig_len, m_len, hash_key, 3*n, n);

  
  //-----------------------
  // Verify signature
  //-----------------------

  // Prepare Address
  idx_tree = idx >> tree_h;
  idx_leaf = (idx & ((1 << tree_h)-1));
  setLayerADRS(ots_addr, 0);
  setTreeADRS(ots_addr, idx_tree);
  setType(ots_addr, 0);

  memcpy(ltree_addr, ots_addr, 12);
  setType(ltree_addr, 1);

  memcpy(node_addr, ltree_addr, 12);
  setType(node_addr, 2);
  
  setOTSADRS(ots_addr, idx_leaf);

  // Check WOTS signature
  wots_pkFromSig(wots_pk, sig_msg, msg_h, &(params->xmss_par.wots_par), pub_seed, ots_addr);

  sig_msg += params->xmss_par.wots_par.keysize;
  sig_msg_len -= params->xmss_par.wots_par.keysize;

  // Compute Ltree
  setLtreeADRS(ltree_addr, idx_leaf);
  l_tree(pkhash, wots_pk, &(params->xmss_par), pub_seed, ltree_addr);

  // Compute root
  validate_authpath(root, pkhash, idx_leaf, sig_msg, &(params->xmss_par), pub_seed, node_addr);

  sig_msg += tree_h*n;
  sig_msg_len -= tree_h*n;

  for (i = 1; i < params->d; i++) {
    // Prepare Address
    idx_leaf = (idx_tree & ((1 << tree_h)-1));
    idx_tree = idx_tree >> tree_h;

    setLayerADRS(ots_addr, i);
    setTreeADRS(ots_addr, idx_tree);
    setType(ots_addr, 0);

    memcpy(ltree_addr, ots_addr, 12);
    setType(ltree_addr, 1);

    memcpy(node_addr, ltree_addr, 12);
    setType(node_addr, 2);

    setOTSADRS(ots_addr, idx_leaf);

    // Check WOTS signature
    wots_pkFromSig(wots_pk, sig_msg, root, &(params->xmss_par.wots_par), pub_seed, ots_addr);

    sig_msg += params->xmss_par.wots_par.keysize;
    sig_msg_len -= params->xmss_par.wots_par.keysize;

    // Compute Ltree
    setLtreeADRS(ltree_addr, idx_leaf);
    l_tree(pkhash, wots_pk, &(params->xmss_par), pub_seed, ltree_addr);

    // Compute root
    validate_authpath(root, pkhash, idx_leaf, sig_msg, &(params->xmss_par), pub_seed, node_addr);

    sig_msg += tree_h*n;
    sig_msg_len -= tree_h*n;

  }

  for (i = 0; i < n; i++)
    if (root[i] != pk[i])
      goto fail;

  *msglen = sig_msg_len;
  for (i = 0; i < *msglen; i++)
    msg[i] = sig_msg[i];

  return 0;


fail:
  *msglen = sig_msg_len;
  for (i = 0; i < *msglen; i++)
    msg[i] = 0;
  *msglen = -1;
  return -1;
}
#endif /* WITH_XMSS */
