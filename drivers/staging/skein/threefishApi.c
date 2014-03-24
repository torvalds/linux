

#include <linux/string.h>
#include <threefishApi.h>

void threefishSetKey(struct threefish_key *keyCtx,
			enum threefish_size stateSize,
			u64 *keyData, u64 *tweak)
{
	int keyWords = stateSize / 64;
	int i;
	u64 parity = KeyScheduleConst;

	keyCtx->tweak[0] = tweak[0];
	keyCtx->tweak[1] = tweak[1];
	keyCtx->tweak[2] = tweak[0] ^ tweak[1];

	for (i = 0; i < keyWords; i++) {
		keyCtx->key[i] = keyData[i];
		parity ^= keyData[i];
	}
	keyCtx->key[i] = parity;
	keyCtx->stateSize = stateSize;
}

void threefishEncryptBlockBytes(struct threefish_key *keyCtx, u8 *in,
				u8 *out)
{
	u64 plain[SKEIN_MAX_STATE_WORDS];        /* max number of words*/
	u64 cipher[SKEIN_MAX_STATE_WORDS];

	Skein_Get64_LSB_First(plain, in, keyCtx->stateSize / 64);
	threefishEncryptBlockWords(keyCtx, plain, cipher);
	Skein_Put64_LSB_First(out, cipher, keyCtx->stateSize / 8);
}

void threefishEncryptBlockWords(struct threefish_key *keyCtx, u64 *in,
				u64 *out)
{
	switch (keyCtx->stateSize) {
	case Threefish256:
		threefishEncrypt256(keyCtx, in, out);
		break;
	case Threefish512:
		threefishEncrypt512(keyCtx, in, out);
		break;
	case Threefish1024:
		threefishEncrypt1024(keyCtx, in, out);
		break;
	}
}

void threefishDecryptBlockBytes(struct threefish_key *keyCtx, u8 *in,
				u8 *out)
{
	u64 plain[SKEIN_MAX_STATE_WORDS];        /* max number of words*/
	u64 cipher[SKEIN_MAX_STATE_WORDS];

	Skein_Get64_LSB_First(cipher, in, keyCtx->stateSize / 64);
	threefishDecryptBlockWords(keyCtx, cipher, plain);
	Skein_Put64_LSB_First(out, plain, keyCtx->stateSize / 8);
}

void threefishDecryptBlockWords(struct threefish_key *keyCtx, u64 *in,
				u64 *out)
{
	switch (keyCtx->stateSize) {
	case Threefish256:
		threefishDecrypt256(keyCtx, in, out);
		break;
	case Threefish512:
		threefishDecrypt512(keyCtx, in, out);
		break;
	case Threefish1024:
		threefishDecrypt1024(keyCtx, in, out);
		break;
	}
}

