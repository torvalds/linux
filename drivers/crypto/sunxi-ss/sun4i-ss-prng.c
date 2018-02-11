#include "sun4i-ss.h"

int sun4i_ss_prng_seed(struct crypto_rng *tfm, const u8 *seed,
		       unsigned int slen)
{
	struct sun4i_ss_alg_template *algt;
	struct rng_alg *alg = crypto_rng_alg(tfm);

	algt = container_of(alg, struct sun4i_ss_alg_template, alg.rng);
	memcpy(algt->ss->seed, seed, slen);

	return 0;
}

int sun4i_ss_prng_generate(struct crypto_rng *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int dlen)
{
	struct sun4i_ss_alg_template *algt;
	struct rng_alg *alg = crypto_rng_alg(tfm);
	int i;
	u32 v;
	u32 *data = (u32 *)dst;
	const u32 mode = SS_OP_PRNG | SS_PRNG_CONTINUE | SS_ENABLED;
	size_t len;
	struct sun4i_ss_ctx *ss;
	unsigned int todo = (dlen / 4) * 4;

	algt = container_of(alg, struct sun4i_ss_alg_template, alg.rng);
	ss = algt->ss;

	spin_lock(&ss->slock);

	writel(mode, ss->base + SS_CTL);

	while (todo > 0) {
		/* write the seed */
		for (i = 0; i < SS_SEED_LEN / BITS_PER_LONG; i++)
			writel(ss->seed[i], ss->base + SS_KEY0 + i * 4);

		/* Read the random data */
		len = min_t(size_t, SS_DATA_LEN / BITS_PER_BYTE, todo);
		readsl(ss->base + SS_TXFIFO, data, len / 4);
		data += len / 4;
		todo -= len;

		/* Update the seed */
		for (i = 0; i < SS_SEED_LEN / BITS_PER_LONG; i++) {
			v = readl(ss->base + SS_KEY0 + i * 4);
			ss->seed[i] = v;
		}
	}

	writel(0, ss->base + SS_CTL);
	spin_unlock(&ss->slock);
	return dlen;
}
