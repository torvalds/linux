
#define AES_MAXNR 14

struct AES_KEY {
	unsigned int rd_key[4 * (AES_MAXNR + 1)];
	int rounds;
};

struct AES_CTX {
	struct AES_KEY enc_key;
	struct AES_KEY dec_key;
};

asmlinkage void AES_encrypt(const u8 *in, u8 *out, struct AES_KEY *ctx);
asmlinkage void AES_decrypt(const u8 *in, u8 *out, struct AES_KEY *ctx);
asmlinkage int private_AES_set_decrypt_key(const unsigned char *userKey,
					   const int bits, struct AES_KEY *key);
asmlinkage int private_AES_set_encrypt_key(const unsigned char *userKey,
					   const int bits, struct AES_KEY *key);
