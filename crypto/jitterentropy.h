// SPDX-License-Identifier: GPL-2.0-or-later

extern void *jent_zalloc(unsigned int len);
extern void jent_zfree(void *ptr);
extern void jent_get_nstime(__u64 *out);
extern int jent_hash_time(void *hash_state, __u64 time, u8 *addtl,
			  unsigned int addtl_len, __u64 hash_loop_cnt,
			  unsigned int stuck);
int jent_read_random_block(void *hash_state, char *dst, unsigned int dst_len);

struct rand_data;
extern int jent_entropy_init(void *hash_state);
extern int jent_read_entropy(struct rand_data *ec, unsigned char *data,
			     unsigned int len);

extern struct rand_data *jent_entropy_collector_alloc(unsigned int osr,
						      unsigned int flags,
						      void *hash_state);
extern void jent_entropy_collector_free(struct rand_data *entropy_collector);

#ifdef CONFIG_CRYPTO_JITTERENTROPY_TESTINTERFACE
int jent_raw_hires_entropy_store(__u32 value);
void jent_testing_init(void);
void jent_testing_exit(void);
#else /* CONFIG_CRYPTO_JITTERENTROPY_TESTINTERFACE */
static inline int jent_raw_hires_entropy_store(__u32 value) { return 0; }
static inline void jent_testing_init(void) { }
static inline void jent_testing_exit(void) { }
#endif /* CONFIG_CRYPTO_JITTERENTROPY_TESTINTERFACE */
