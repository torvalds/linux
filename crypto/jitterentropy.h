// SPDX-License-Identifier: GPL-2.0-or-later

extern void *jent_zalloc(unsigned int len);
extern void jent_zfree(void *ptr);
extern int jent_fips_enabled(void);
extern void jent_panic(char *s);
extern void jent_memcpy(void *dest, const void *src, unsigned int n);
extern void jent_get_nstime(__u64 *out);

struct rand_data;
extern int jent_entropy_init(void);
extern int jent_read_entropy(struct rand_data *ec, unsigned char *data,
			     unsigned int len);

extern struct rand_data *jent_entropy_collector_alloc(unsigned int osr,
						      unsigned int flags);
extern void jent_entropy_collector_free(struct rand_data *entropy_collector);
