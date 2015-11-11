
int sha512_arm_update(struct shash_desc *desc, const u8 *data,
		      unsigned int len);

int sha512_arm_finup(struct shash_desc *desc, const u8 *data,
		     unsigned int len, u8 *out);

extern struct shash_alg sha512_neon_algs[2];
