#ifndef PRISM2_CRYPT_H
#define PRISM2_CRYPT_H

struct hostap_crypto_ops {
	char *name;

	/* init new crypto context (e.g., allocate private data space,
	 * select IV, etc.); returns NULL on failure or pointer to allocated
	 * private data on success */
	void * (*init)(int keyidx);

	/* deinitialize crypto context and free allocated private data */
	void (*deinit)(void *priv);

	/* encrypt/decrypt return < 0 on error or >= 0 on success. The return
	 * value from decrypt_mpdu is passed as the keyidx value for
	 * decrypt_msdu. skb must have enough head and tail room for the
	 * encryption; if not, error will be returned; these functions are
	 * called for all MPDUs (i.e., fragments).
	 */
	int (*encrypt_mpdu)(struct sk_buff *skb, int hdr_len, void *priv);
	int (*decrypt_mpdu)(struct sk_buff *skb, int hdr_len, void *priv);

	/* These functions are called for full MSDUs, i.e. full frames.
	 * These can be NULL if full MSDU operations are not needed. */
	int (*encrypt_msdu)(struct sk_buff *skb, int hdr_len, void *priv);
	int (*decrypt_msdu)(struct sk_buff *skb, int keyidx, int hdr_len,
			    void *priv);

	int (*set_key)(void *key, int len, u8 *seq, void *priv);
	int (*get_key)(void *key, int len, u8 *seq, void *priv);

	/* procfs handler for printing out key information and possible
	 * statistics */
	char * (*print_stats)(char *p, void *priv);

	/* maximum number of bytes added by encryption; encrypt buf is
	 * allocated with extra_prefix_len bytes, copy of in_buf, and
	 * extra_postfix_len; encrypt need not use all this space, but
	 * the result must start at the beginning of the buffer and correct
	 * length must be returned */
	int extra_prefix_len, extra_postfix_len;
};


int hostap_register_crypto_ops(struct hostap_crypto_ops *ops);
int hostap_unregister_crypto_ops(struct hostap_crypto_ops *ops);
struct hostap_crypto_ops * hostap_get_crypto_ops(const char *name);

#endif /* PRISM2_CRYPT_H */
