#ifndef _SKEIN_H_
#define _SKEIN_H_     1
/**************************************************************************
**
** Interface declarations and internal definitions for Skein hashing.
**
** Source code author: Doug Whiting, 2008.
**
** This algorithm and source code is released to the public domain.
**
***************************************************************************
**
** The following compile-time switches may be defined to control some
** tradeoffs between speed, code size, error checking, and security.
**
** The "default" note explains what happens when the switch is not defined.
**
**  SKEIN_ERR_CHECK        -- how error checking is handled inside Skein
**                            code. If not defined, most error checking
**                            is disabled (for performance). Otherwise,
**                            the switch value is interpreted as:
**                                0: use assert()      to flag errors
**                                1: return SKEIN_FAIL to flag errors
**
***************************************************************************/

/*Skein digest sizes for crypto api*/
#define SKEIN256_DIGEST_BIT_SIZE 256
#define SKEIN512_DIGEST_BIT_SIZE 512
#define SKEIN1024_DIGEST_BIT_SIZE 1024

/* below two prototype assume we are handed aligned data */
#define skein_put64_lsb_first(dst08, src64, b_cnt) memcpy(dst08, src64, b_cnt)
#define skein_get64_lsb_first(dst64, src08, w_cnt) \
		memcpy(dst64, src08, 8*(w_cnt))
#define skein_swap64(w64)  (w64)

enum {
	SKEIN_SUCCESS         =      0, /* return codes from Skein calls */
	SKEIN_FAIL            =      1,
	SKEIN_BAD_HASHLEN     =      2
};

#define  SKEIN_MODIFIER_WORDS   2 /* number of modifier (tweak) words */

#define  SKEIN_256_STATE_WORDS  4
#define  SKEIN_512_STATE_WORDS  8
#define  SKEIN_1024_STATE_WORDS 16
#define  SKEIN_MAX_STATE_WORDS	16

#define  SKEIN_256_STATE_BYTES  (8*SKEIN_256_STATE_WORDS)
#define  SKEIN_512_STATE_BYTES  (8*SKEIN_512_STATE_WORDS)
#define  SKEIN_1024_STATE_BYTES  (8*SKEIN_1024_STATE_WORDS)

#define  SKEIN_256_STATE_BITS  (64*SKEIN_256_STATE_WORDS)
#define  SKEIN_512_STATE_BITS  (64*SKEIN_512_STATE_WORDS)
#define  SKEIN_1024_STATE_BITS  (64*SKEIN_1024_STATE_WORDS)

#define  SKEIN_256_BLOCK_BYTES  (8*SKEIN_256_STATE_WORDS)
#define  SKEIN_512_BLOCK_BYTES  (8*SKEIN_512_STATE_WORDS)
#define  SKEIN_1024_BLOCK_BYTES  (8*SKEIN_1024_STATE_WORDS)

struct skein_ctx_hdr {
	size_t hash_bit_len;		/* size of hash result, in bits */
	size_t b_cnt;			/* current byte count in buffer b[] */
	u64 tweak[SKEIN_MODIFIER_WORDS]; /* tweak[0]=byte cnt, tweak[1]=flags */
};

struct skein_256_ctx { /* 256-bit Skein hash context structure */
	struct skein_ctx_hdr h;		/* common header context variables */
	u64 x[SKEIN_256_STATE_WORDS];	/* chaining variables */
	u8 b[SKEIN_256_BLOCK_BYTES];	/* partial block buf (8-byte aligned) */
};

struct skein_512_ctx { /* 512-bit Skein hash context structure */
	struct skein_ctx_hdr h;		/* common header context variables */
	u64 x[SKEIN_512_STATE_WORDS];	/* chaining variables */
	u8 b[SKEIN_512_BLOCK_BYTES];	/* partial block buf (8-byte aligned) */
};

struct skein_1024_ctx { /* 1024-bit Skein hash context structure */
	struct skein_ctx_hdr h;		/* common header context variables */
	u64 x[SKEIN_1024_STATE_WORDS];	/* chaining variables */
	u8 b[SKEIN_1024_BLOCK_BYTES];	/* partial block buf (8-byte aligned) */
};

static inline u64 rotl_64(u64 x, u8 N)
{
	return (x << N) | (x >> (64 - N));
}

/* Skein APIs for (incremental) "straight hashing" */
int skein_256_init(struct skein_256_ctx *ctx, size_t hash_bit_len);
int skein_512_init(struct skein_512_ctx *ctx, size_t hash_bit_len);
int skein_1024_init(struct skein_1024_ctx *ctx, size_t hash_bit_len);

int skein_256_update(struct skein_256_ctx *ctx, const u8 *msg,
		     size_t msg_byte_cnt);
int skein_512_update(struct skein_512_ctx *ctx, const u8 *msg,
		     size_t msg_byte_cnt);
int skein_1024_update(struct skein_1024_ctx *ctx, const u8 *msg,
		      size_t msg_byte_cnt);

int skein_256_final(struct skein_256_ctx *ctx, u8 *hash_val);
int skein_512_final(struct skein_512_ctx *ctx, u8 *hash_val);
int skein_1024_final(struct skein_1024_ctx *ctx, u8 *hash_val);

/*
**   Skein APIs for "extended" initialization: MAC keys, tree hashing.
**   After an init_ext() call, just use update/final calls as with init().
**
**   Notes: Same parameters as _init() calls, plus tree_info/key/key_bytes.
**          When key_bytes == 0 and tree_info == SKEIN_SEQUENTIAL,
**              the results of init_ext() are identical to calling init().
**          The function init() may be called once to "precompute" the IV for
**              a given hash_bit_len value, then by saving a copy of the context
**              the IV computation may be avoided in later calls.
**          Similarly, the function init_ext() may be called once per MAC key
**              to precompute the MAC IV, then a copy of the context saved and
**              reused for each new MAC computation.
**/
int skein_256_init_ext(struct skein_256_ctx *ctx, size_t hash_bit_len,
		       u64 tree_info, const u8 *key, size_t key_bytes);
int skein_512_init_ext(struct skein_512_ctx *ctx, size_t hash_bit_len,
		       u64 tree_info, const u8 *key, size_t key_bytes);
int skein_1024_init_ext(struct skein_1024_ctx *ctx, size_t hash_bit_len,
			u64 tree_info, const u8 *key, size_t key_bytes);

/*
**   Skein APIs for MAC and tree hash:
**      final_pad:  pad, do final block, but no OUTPUT type
**      output:     do just the output stage
*/
int skein_256_final_pad(struct skein_256_ctx *ctx, u8 *hash_val);
int skein_512_final_pad(struct skein_512_ctx *ctx, u8 *hash_val);
int skein_1024_final_pad(struct skein_1024_ctx *ctx, u8 *hash_val);

#ifndef SKEIN_TREE_HASH
#define SKEIN_TREE_HASH (1)
#endif
#if  SKEIN_TREE_HASH
int skein_256_output(struct skein_256_ctx *ctx, u8 *hash_val);
int skein_512_output(struct skein_512_ctx *ctx, u8 *hash_val);
int skein_1024_output(struct skein_1024_ctx *ctx, u8 *hash_val);
#endif

/*****************************************************************
** "Internal" Skein definitions
**    -- not needed for sequential hashing API, but will be
**           helpful for other uses of Skein (e.g., tree hash mode).
**    -- included here so that they can be shared between
**           reference and optimized code.
******************************************************************/

/* tweak word tweak[1]: bit field starting positions */
#define SKEIN_T1_BIT(BIT)       ((BIT) - 64)      /* second word  */

#define SKEIN_T1_POS_TREE_LVL   SKEIN_T1_BIT(112) /* 112..118 hash tree level */
#define SKEIN_T1_POS_BIT_PAD    SKEIN_T1_BIT(119) /* 119 part. final in byte */
#define SKEIN_T1_POS_BLK_TYPE   SKEIN_T1_BIT(120) /* 120..125 type field `*/
#define SKEIN_T1_POS_FIRST      SKEIN_T1_BIT(126) /* 126      first blk flag */
#define SKEIN_T1_POS_FINAL      SKEIN_T1_BIT(127) /* 127      final blk flag */

/* tweak word tweak[1]: flag bit definition(s) */
#define SKEIN_T1_FLAG_FIRST     (((u64)  1) << SKEIN_T1_POS_FIRST)
#define SKEIN_T1_FLAG_FINAL     (((u64)  1) << SKEIN_T1_POS_FINAL)
#define SKEIN_T1_FLAG_BIT_PAD   (((u64)  1) << SKEIN_T1_POS_BIT_PAD)

/* tweak word tweak[1]: tree level bit field mask */
#define SKEIN_T1_TREE_LVL_MASK  (((u64)0x7F) << SKEIN_T1_POS_TREE_LVL)
#define SKEIN_T1_TREE_LEVEL(n)  (((u64) (n)) << SKEIN_T1_POS_TREE_LVL)

/* tweak word tweak[1]: block type field */
#define SKEIN_BLK_TYPE_KEY       (0) /* key, for MAC and KDF */
#define SKEIN_BLK_TYPE_CFG       (4) /* configuration block */
#define SKEIN_BLK_TYPE_PERS      (8) /* personalization string */
#define SKEIN_BLK_TYPE_PK       (12) /* pubkey (for digital sigs) */
#define SKEIN_BLK_TYPE_KDF      (16) /* key identifier for KDF */
#define SKEIN_BLK_TYPE_NONCE    (20) /* nonce for PRNG */
#define SKEIN_BLK_TYPE_MSG      (48) /* message processing */
#define SKEIN_BLK_TYPE_OUT      (63) /* output stage */
#define SKEIN_BLK_TYPE_MASK     (63) /* bit field mask */

#define SKEIN_T1_BLK_TYPE(T)   (((u64) (SKEIN_BLK_TYPE_##T)) << \
					SKEIN_T1_POS_BLK_TYPE)
#define SKEIN_T1_BLK_TYPE_KEY   SKEIN_T1_BLK_TYPE(KEY)  /* for MAC and KDF */
#define SKEIN_T1_BLK_TYPE_CFG   SKEIN_T1_BLK_TYPE(CFG)  /* config block */
#define SKEIN_T1_BLK_TYPE_PERS  SKEIN_T1_BLK_TYPE(PERS) /* personalization */
#define SKEIN_T1_BLK_TYPE_PK    SKEIN_T1_BLK_TYPE(PK)   /* pubkey (for sigs) */
#define SKEIN_T1_BLK_TYPE_KDF   SKEIN_T1_BLK_TYPE(KDF)  /* key ident for KDF */
#define SKEIN_T1_BLK_TYPE_NONCE SKEIN_T1_BLK_TYPE(NONCE)/* nonce for PRNG */
#define SKEIN_T1_BLK_TYPE_MSG   SKEIN_T1_BLK_TYPE(MSG)  /* message processing */
#define SKEIN_T1_BLK_TYPE_OUT   SKEIN_T1_BLK_TYPE(OUT)  /* output stage */
#define SKEIN_T1_BLK_TYPE_MASK  SKEIN_T1_BLK_TYPE(MASK) /* field bit mask */

#define SKEIN_T1_BLK_TYPE_CFG_FINAL    (SKEIN_T1_BLK_TYPE_CFG | \
					SKEIN_T1_FLAG_FINAL)
#define SKEIN_T1_BLK_TYPE_OUT_FINAL    (SKEIN_T1_BLK_TYPE_OUT | \
					SKEIN_T1_FLAG_FINAL)

#define SKEIN_VERSION           (1)

#ifndef SKEIN_ID_STRING_LE      /* allow compile-time personalization */
#define SKEIN_ID_STRING_LE      (0x33414853) /* "SHA3" (little-endian)*/
#endif

#define SKEIN_MK_64(hi32, lo32)  ((lo32) + (((u64) (hi32)) << 32))
#define SKEIN_SCHEMA_VER        SKEIN_MK_64(SKEIN_VERSION, SKEIN_ID_STRING_LE)
#define SKEIN_KS_PARITY         SKEIN_MK_64(0x1BD11BDA, 0xA9FC1A22)

#define SKEIN_CFG_STR_LEN       (4*8)

/* bit field definitions in config block tree_info word */
#define SKEIN_CFG_TREE_LEAF_SIZE_POS  (0)
#define SKEIN_CFG_TREE_NODE_SIZE_POS  (8)
#define SKEIN_CFG_TREE_MAX_LEVEL_POS  (16)

#define SKEIN_CFG_TREE_LEAF_SIZE_MSK (((u64)0xFF) << \
					SKEIN_CFG_TREE_LEAF_SIZE_POS)
#define SKEIN_CFG_TREE_NODE_SIZE_MSK (((u64)0xFF) << \
					SKEIN_CFG_TREE_NODE_SIZE_POS)
#define SKEIN_CFG_TREE_MAX_LEVEL_MSK (((u64)0xFF) << \
					SKEIN_CFG_TREE_MAX_LEVEL_POS)

#define SKEIN_CFG_TREE_INFO(leaf, node, max_lvl)                   \
	((((u64)(leaf))   << SKEIN_CFG_TREE_LEAF_SIZE_POS) |    \
	 (((u64)(node))   << SKEIN_CFG_TREE_NODE_SIZE_POS) |    \
	 (((u64)(max_lvl)) << SKEIN_CFG_TREE_MAX_LEVEL_POS))

/* use as tree_info in InitExt() call for sequential processing */
#define SKEIN_CFG_TREE_INFO_SEQUENTIAL SKEIN_CFG_TREE_INFO(0, 0, 0)

/*
**   Skein macros for getting/setting tweak words, etc.
**   These are useful for partial input bytes, hash tree init/update, etc.
**/
#define skein_get_tweak(ctx_ptr, TWK_NUM)          ((ctx_ptr)->h.tweak[TWK_NUM])
#define skein_set_tweak(ctx_ptr, TWK_NUM, t_val) { \
		(ctx_ptr)->h.tweak[TWK_NUM] = (t_val); \
	}

#define skein_get_T0(ctx_ptr)     skein_get_tweak(ctx_ptr, 0)
#define skein_get_T1(ctx_ptr)     skein_get_tweak(ctx_ptr, 1)
#define skein_set_T0(ctx_ptr, T0) skein_set_tweak(ctx_ptr, 0, T0)
#define skein_set_T1(ctx_ptr, T1) skein_set_tweak(ctx_ptr, 1, T1)

/* set both tweak words at once */
#define skein_set_T0_T1(ctx_ptr, T0, T1)           \
	{                                          \
	skein_set_T0(ctx_ptr, (T0));               \
	skein_set_T1(ctx_ptr, (T1));               \
	}

#define skein_set_type(ctx_ptr, BLK_TYPE)         \
	skein_set_T1(ctx_ptr, SKEIN_T1_BLK_TYPE_##BLK_TYPE)

/*
 * setup for starting with a new type:
 * h.tweak[0]=0; h.tweak[1] = NEW_TYPE; h.b_cnt=0;
 */
#define skein_start_new_type(ctx_ptr, BLK_TYPE) { \
		skein_set_T0_T1(ctx_ptr, 0, SKEIN_T1_FLAG_FIRST | \
				SKEIN_T1_BLK_TYPE_##BLK_TYPE); \
		(ctx_ptr)->h.b_cnt = 0; \
	}

#define skein_clear_first_flag(hdr) { \
		(hdr).tweak[1] &= ~SKEIN_T1_FLAG_FIRST; \
	}
#define skein_set_bit_pad_flag(hdr) { \
		(hdr).tweak[1] |=  SKEIN_T1_FLAG_BIT_PAD; \
	}

#define skein_set_tree_level(hdr, height) { \
		(hdr).tweak[1] |= SKEIN_T1_TREE_LEVEL(height); \
	}

/* ignore all asserts, for performance */
#define skein_assert_ret(x, ret_code)
#define skein_assert(x)

/*****************************************************************
** Skein block function constants (shared across Ref and Opt code)
******************************************************************/
enum {
	    /* SKEIN_256 round rotation constants */
	R_256_0_0 = 14, R_256_0_1 = 16,
	R_256_1_0 = 52, R_256_1_1 = 57,
	R_256_2_0 = 23, R_256_2_1 = 40,
	R_256_3_0 =  5, R_256_3_1 = 37,
	R_256_4_0 = 25, R_256_4_1 = 33,
	R_256_5_0 = 46, R_256_5_1 = 12,
	R_256_6_0 = 58, R_256_6_1 = 22,
	R_256_7_0 = 32, R_256_7_1 = 32,

	    /* SKEIN_512 round rotation constants */
	R_512_0_0 = 46, R_512_0_1 = 36, R_512_0_2 = 19, R_512_0_3 = 37,
	R_512_1_0 = 33, R_512_1_1 = 27, R_512_1_2 = 14, R_512_1_3 = 42,
	R_512_2_0 = 17, R_512_2_1 = 49, R_512_2_2 = 36, R_512_2_3 = 39,
	R_512_3_0 = 44, R_512_3_1 =  9, R_512_3_2 = 54, R_512_3_3 = 56,
	R_512_4_0 = 39, R_512_4_1 = 30, R_512_4_2 = 34, R_512_4_3 = 24,
	R_512_5_0 = 13, R_512_5_1 = 50, R_512_5_2 = 10, R_512_5_3 = 17,
	R_512_6_0 = 25, R_512_6_1 = 29, R_512_6_2 = 39, R_512_6_3 = 43,
	R_512_7_0 =  8, R_512_7_1 = 35, R_512_7_2 = 56, R_512_7_3 = 22,

	    /* SKEIN_1024 round rotation constants */
	R1024_0_0 = 24, R1024_0_1 = 13, R1024_0_2 =  8, R1024_0_3 = 47,
	R1024_0_4 =  8, R1024_0_5 = 17, R1024_0_6 = 22, R1024_0_7 = 37,
	R1024_1_0 = 38, R1024_1_1 = 19, R1024_1_2 = 10, R1024_1_3 = 55,
	R1024_1_4 = 49, R1024_1_5 = 18, R1024_1_6 = 23, R1024_1_7 = 52,
	R1024_2_0 = 33, R1024_2_1 =  4, R1024_2_2 = 51, R1024_2_3 = 13,
	R1024_2_4 = 34, R1024_2_5 = 41, R1024_2_6 = 59, R1024_2_7 = 17,
	R1024_3_0 =  5, R1024_3_1 = 20, R1024_3_2 = 48, R1024_3_3 = 41,
	R1024_3_4 = 47, R1024_3_5 = 28, R1024_3_6 = 16, R1024_3_7 = 25,
	R1024_4_0 = 41, R1024_4_1 =  9, R1024_4_2 = 37, R1024_4_3 = 31,
	R1024_4_4 = 12, R1024_4_5 = 47, R1024_4_6 = 44, R1024_4_7 = 30,
	R1024_5_0 = 16, R1024_5_1 = 34, R1024_5_2 = 56, R1024_5_3 = 51,
	R1024_5_4 =  4, R1024_5_5 = 53, R1024_5_6 = 42, R1024_5_7 = 41,
	R1024_6_0 = 31, R1024_6_1 = 44, R1024_6_2 = 47, R1024_6_3 = 46,
	R1024_6_4 = 19, R1024_6_5 = 42, R1024_6_6 = 44, R1024_6_7 = 25,
	R1024_7_0 =  9, R1024_7_1 = 48, R1024_7_2 = 35, R1024_7_3 = 52,
	R1024_7_4 = 23, R1024_7_5 = 31, R1024_7_6 = 37, R1024_7_7 = 20
};

#ifndef SKEIN_ROUNDS
#define SKEIN_256_ROUNDS_TOTAL (72)	/* # rounds for diff block sizes */
#define SKEIN_512_ROUNDS_TOTAL (72)
#define SKEIN_1024_ROUNDS_TOTAL (80)
#else			/* allow command-line define in range 8*(5..14)   */
#define SKEIN_256_ROUNDS_TOTAL (8*((((SKEIN_ROUNDS/100) + 5) % 10) + 5))
#define SKEIN_512_ROUNDS_TOTAL (8*((((SKEIN_ROUNDS/10)  + 5) % 10) + 5))
#define SKEIN_1024_ROUNDS_TOTAL (8*((((SKEIN_ROUNDS)     + 5) % 10) + 5))
#endif

#endif  /* ifndef _SKEIN_H_ */
