/* Orinoco MIC helpers
 *
 * See copyright notice in main.c
 */
#ifndef _ORINOCO_MIC_H_
#define _ORINOCO_MIC_H_

#include <linux/types.h>
#include <crypto/hash.h>

#define MICHAEL_MIC_LEN 8

/* Forward declarations */
struct orinoco_private;
struct crypto_ahash;

int orinoco_mic_init(struct orinoco_private *priv);
void orinoco_mic_free(struct orinoco_private *priv);
int orinoco_mic(struct crypto_shash *tfm_michael, u8 *key,
		u8 *da, u8 *sa, u8 priority,
		u8 *data, size_t data_len, u8 *mic);

#endif /* ORINOCO_MIC_H */
