/* SHA256 and SHA512-based Unix crypt implementation.
 * Released into the Public Domain by Ulrich Drepper <drepper@redhat.com>.
 */

/* Prefix for optional rounds specification.  */
static const char str_rounds[] ALIGN1 = "rounds=%u$";

/* Maximum salt string length.  */
#define SALT_LEN_MAX 16
/* Default number of rounds if not explicitly specified.  */
#define ROUNDS_DEFAULT 5000
/* Minimum number of rounds.  */
#define ROUNDS_MIN 1000
/* Maximum number of rounds.  */
#define ROUNDS_MAX 999999999

static char *
NOINLINE
sha_crypt(/*const*/ char *key_data, /*const*/ char *salt_data)
{
#undef sha_end
	void (*sha_begin)(void *ctx) FAST_FUNC;
	void (*sha_hash)(void *ctx, const void *buffer, size_t len) FAST_FUNC;
	unsigned (*sha_end)(void *ctx, void *resbuf) FAST_FUNC;
	int _32or64;

	char *result, *resptr;

	/* btw, sha256 needs [32] and uint32_t only */
	struct {
		unsigned char alt_result[64];
		unsigned char temp_result[64];
		union {
			sha256_ctx_t x;
			sha512_ctx_t y;
		} ctx;
		union {
			sha256_ctx_t x;
			sha512_ctx_t y;
		} alt_ctx;
	} L __attribute__((__aligned__(__alignof__(uint64_t))));
#define alt_result  (L.alt_result )
#define temp_result (L.temp_result)
#define ctx         (L.ctx        )
#define alt_ctx     (L.alt_ctx    )
	unsigned salt_len;
	unsigned key_len;
	unsigned cnt;
	unsigned rounds;
	char *cp;

	/* Analyze salt, construct already known part of result */
	cnt = strlen(salt_data) + 1 + 43 + 1;
	_32or64 = 32;
	if (salt_data[1] == '6') { /* sha512 */
		_32or64 *= 2; /*64*/
		cnt += 43;
	}
	result = resptr = xzalloc(cnt); /* will provide NUL terminator */
	*resptr++ = '$';
	*resptr++ = salt_data[1];
	*resptr++ = '$';
	rounds = ROUNDS_DEFAULT;
	salt_data += 3;
	if (strncmp(salt_data, str_rounds, 7) == 0) {
		/* 7 == strlen("rounds=") */
		char *endp;
		cnt = bb_strtou(salt_data + 7, &endp, 10);
		if (*endp == '$') {
			salt_data = endp + 1;
			rounds = cnt;
			if (rounds < ROUNDS_MIN)
				rounds = ROUNDS_MIN;
			if (rounds > ROUNDS_MAX)
				rounds = ROUNDS_MAX;
			/* add "rounds=NNNNN$" to result */
			resptr += sprintf(resptr, str_rounds, rounds);
		}
	}
	salt_len = strchrnul(salt_data, '$') - salt_data;
	if (salt_len > SALT_LEN_MAX)
		salt_len = SALT_LEN_MAX;
	/* xstrdup assures suitable alignment; also we will use it
	   as a scratch space later. */
	salt_data = xstrndup(salt_data, salt_len);
	/* add "salt$" to result */
	strcpy(resptr, salt_data);
	resptr += salt_len;
	*resptr++ = '$';
	/* key data doesn't need much processing */
	key_len = strlen(key_data);
	key_data = xstrdup(key_data);

	/* Which flavor of SHAnnn ops to use? */
	sha_begin = (void*)sha256_begin;
	sha_hash = (void*)sha256_hash;
	sha_end = (void*)sha256_end;
	if (_32or64 != 32) {
		sha_begin = (void*)sha512_begin;
		sha_hash = (void*)sha512_hash;
		sha_end = (void*)sha512_end;
	}

	/* Add KEY, SALT.  */
	sha_begin(&ctx);
	sha_hash(&ctx, key_data, key_len);
	sha_hash(&ctx, salt_data, salt_len);

	/* Compute alternate SHA sum with input KEY, SALT, and KEY.
	   The final result will be added to the first context.  */
	sha_begin(&alt_ctx);
	sha_hash(&alt_ctx, key_data, key_len);
	sha_hash(&alt_ctx, salt_data, salt_len);
	sha_hash(&alt_ctx, key_data, key_len);
	sha_end(&alt_ctx, alt_result);

	/* Add result of this to the other context.  */
	/* Add for any character in the key one byte of the alternate sum.  */
	for (cnt = key_len; cnt > _32or64; cnt -= _32or64)
		sha_hash(&ctx, alt_result, _32or64);
	sha_hash(&ctx, alt_result, cnt);

	/* Take the binary representation of the length of the key and for every
	   1 add the alternate sum, for every 0 the key.  */
	for (cnt = key_len; cnt != 0; cnt >>= 1)
		if ((cnt & 1) != 0)
			sha_hash(&ctx, alt_result, _32or64);
		else
			sha_hash(&ctx, key_data, key_len);

	/* Create intermediate result.  */
	sha_end(&ctx, alt_result);

	/* Start computation of P byte sequence.  */
	/* For every character in the password add the entire password.  */
	sha_begin(&alt_ctx);
	for (cnt = 0; cnt < key_len; ++cnt)
		sha_hash(&alt_ctx, key_data, key_len);
	sha_end(&alt_ctx, temp_result);

	/* NB: past this point, raw key_data is not used anymore */

	/* Create byte sequence P.  */
#define p_bytes key_data /* reuse the buffer as it is of the key_len size */
	cp = p_bytes; /* was: ... = alloca(key_len); */
	for (cnt = key_len; cnt >= _32or64; cnt -= _32or64) {
		cp = memcpy(cp, temp_result, _32or64);
		cp += _32or64;
	}
	memcpy(cp, temp_result, cnt);

	/* Start computation of S byte sequence.  */
	/* For every character in the password add the entire password.  */
	sha_begin(&alt_ctx);
	for (cnt = 0; cnt < 16 + alt_result[0]; ++cnt)
		sha_hash(&alt_ctx, salt_data, salt_len);
	sha_end(&alt_ctx, temp_result);

	/* NB: past this point, raw salt_data is not used anymore */

	/* Create byte sequence S.  */
#define s_bytes salt_data /* reuse the buffer as it is of the salt_len size */
	cp = s_bytes; /* was: ... = alloca(salt_len); */
	for (cnt = salt_len; cnt >= _32or64; cnt -= _32or64) {
		cp = memcpy(cp, temp_result, _32or64);
		cp += _32or64;
	}
	memcpy(cp, temp_result, cnt);

	/* Repeatedly run the collected hash value through SHA to burn
	   CPU cycles.  */
	for (cnt = 0; cnt < rounds; ++cnt) {
		sha_begin(&ctx);

		/* Add key or last result.  */
		if ((cnt & 1) != 0)
			sha_hash(&ctx, p_bytes, key_len);
		else
			sha_hash(&ctx, alt_result, _32or64);
		/* Add salt for numbers not divisible by 3.  */
		if (cnt % 3 != 0)
			sha_hash(&ctx, s_bytes, salt_len);
		/* Add key for numbers not divisible by 7.  */
		if (cnt % 7 != 0)
			sha_hash(&ctx, p_bytes, key_len);
		/* Add key or last result.  */
		if ((cnt & 1) != 0)
			sha_hash(&ctx, alt_result, _32or64);
		else
			sha_hash(&ctx, p_bytes, key_len);

		sha_end(&ctx, alt_result);
	}

	/* Append encrypted password to result buffer */
//TODO: replace with something like
//	bb_uuencode(cp, src, length, bb_uuenc_tbl_XXXbase64);
#define b64_from_24bit(B2, B1, B0, N) \
do { \
	unsigned w = ((B2) << 16) | ((B1) << 8) | (B0); \
	resptr = to64(resptr, w, N); \
} while (0)
	if (_32or64 == 32) { /* sha256 */
		unsigned i = 0;
		while (1) {
			unsigned j = i + 10;
			unsigned k = i + 20;
			if (j >= 30) j -= 30;
			if (k >= 30) k -= 30;
			b64_from_24bit(alt_result[i], alt_result[j], alt_result[k], 4);
			if (k == 29)
				break;
			i = k + 1;
		}
		b64_from_24bit(0, alt_result[31], alt_result[30], 3);
		/* was:
		b64_from_24bit(alt_result[0], alt_result[10], alt_result[20], 4);
		b64_from_24bit(alt_result[21], alt_result[1], alt_result[11], 4);
		b64_from_24bit(alt_result[12], alt_result[22], alt_result[2], 4);
		b64_from_24bit(alt_result[3], alt_result[13], alt_result[23], 4);
		b64_from_24bit(alt_result[24], alt_result[4], alt_result[14], 4);
		b64_from_24bit(alt_result[15], alt_result[25], alt_result[5], 4);
		b64_from_24bit(alt_result[6], alt_result[16], alt_result[26], 4);
		b64_from_24bit(alt_result[27], alt_result[7], alt_result[17], 4);
		b64_from_24bit(alt_result[18], alt_result[28], alt_result[8], 4);
		b64_from_24bit(alt_result[9], alt_result[19], alt_result[29], 4);
		b64_from_24bit(0, alt_result[31], alt_result[30], 3);
		*/
	} else {
		unsigned i = 0;
		while (1) {
			unsigned j = i + 21;
			unsigned k = i + 42;
			if (j >= 63) j -= 63;
			if (k >= 63) k -= 63;
			b64_from_24bit(alt_result[i], alt_result[j], alt_result[k], 4);
			if (j == 20)
				break;
			i = j + 1;
		}
		b64_from_24bit(0, 0, alt_result[63], 2);
		/* was:
		b64_from_24bit(alt_result[0], alt_result[21], alt_result[42], 4);
		b64_from_24bit(alt_result[22], alt_result[43], alt_result[1], 4);
		b64_from_24bit(alt_result[44], alt_result[2], alt_result[23], 4);
		b64_from_24bit(alt_result[3], alt_result[24], alt_result[45], 4);
		b64_from_24bit(alt_result[25], alt_result[46], alt_result[4], 4);
		b64_from_24bit(alt_result[47], alt_result[5], alt_result[26], 4);
		b64_from_24bit(alt_result[6], alt_result[27], alt_result[48], 4);
		b64_from_24bit(alt_result[28], alt_result[49], alt_result[7], 4);
		b64_from_24bit(alt_result[50], alt_result[8], alt_result[29], 4);
		b64_from_24bit(alt_result[9], alt_result[30], alt_result[51], 4);
		b64_from_24bit(alt_result[31], alt_result[52], alt_result[10], 4);
		b64_from_24bit(alt_result[53], alt_result[11], alt_result[32], 4);
		b64_from_24bit(alt_result[12], alt_result[33], alt_result[54], 4);
		b64_from_24bit(alt_result[34], alt_result[55], alt_result[13], 4);
		b64_from_24bit(alt_result[56], alt_result[14], alt_result[35], 4);
		b64_from_24bit(alt_result[15], alt_result[36], alt_result[57], 4);
		b64_from_24bit(alt_result[37], alt_result[58], alt_result[16], 4);
		b64_from_24bit(alt_result[59], alt_result[17], alt_result[38], 4);
		b64_from_24bit(alt_result[18], alt_result[39], alt_result[60], 4);
		b64_from_24bit(alt_result[40], alt_result[61], alt_result[19], 4);
		b64_from_24bit(alt_result[62], alt_result[20], alt_result[41], 4);
		b64_from_24bit(0, 0, alt_result[63], 2);
		*/
	}
	/* *resptr = '\0'; - xzalloc did it */
#undef b64_from_24bit

	/* Clear the buffer for the intermediate result so that people
	   attaching to processes or reading core dumps cannot get any
	   information.  */
	memset(&L, 0, sizeof(L)); /* [alt]_ctx and XXX_result buffers */
	memset(key_data, 0, key_len); /* also p_bytes */
	memset(salt_data, 0, salt_len); /* also s_bytes */
	free(key_data);
	free(salt_data);
#undef p_bytes
#undef s_bytes

	return result;
#undef alt_result
#undef temp_result
#undef ctx
#undef alt_ctx
}
