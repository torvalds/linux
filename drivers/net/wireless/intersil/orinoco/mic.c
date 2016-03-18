/* Orinoco MIC helpers
 *
 * See copyright notice in main.c
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/if_ether.h>
#include <linux/scatterlist.h>
#include <crypto/hash.h>

#include "orinoco.h"
#include "mic.h"

/********************************************************************/
/* Michael MIC crypto setup                                         */
/********************************************************************/
int orinoco_mic_init(struct orinoco_private *priv)
{
	priv->tx_tfm_mic = crypto_alloc_ahash("michael_mic", 0,
					      CRYPTO_ALG_ASYNC);
	if (IS_ERR(priv->tx_tfm_mic)) {
		printk(KERN_DEBUG "orinoco_mic_init: could not allocate "
		       "crypto API michael_mic\n");
		priv->tx_tfm_mic = NULL;
		return -ENOMEM;
	}

	priv->rx_tfm_mic = crypto_alloc_ahash("michael_mic", 0,
					      CRYPTO_ALG_ASYNC);
	if (IS_ERR(priv->rx_tfm_mic)) {
		printk(KERN_DEBUG "orinoco_mic_init: could not allocate "
		       "crypto API michael_mic\n");
		priv->rx_tfm_mic = NULL;
		return -ENOMEM;
	}

	return 0;
}

void orinoco_mic_free(struct orinoco_private *priv)
{
	if (priv->tx_tfm_mic)
		crypto_free_ahash(priv->tx_tfm_mic);
	if (priv->rx_tfm_mic)
		crypto_free_ahash(priv->rx_tfm_mic);
}

int orinoco_mic(struct crypto_ahash *tfm_michael, u8 *key,
		u8 *da, u8 *sa, u8 priority,
		u8 *data, size_t data_len, u8 *mic)
{
	AHASH_REQUEST_ON_STACK(req, tfm_michael);
	struct scatterlist sg[2];
	u8 hdr[ETH_HLEN + 2]; /* size of header + padding */
	int err;

	if (tfm_michael == NULL) {
		printk(KERN_WARNING "orinoco_mic: tfm_michael == NULL\n");
		return -1;
	}

	/* Copy header into buffer. We need the padding on the end zeroed */
	memcpy(&hdr[0], da, ETH_ALEN);
	memcpy(&hdr[ETH_ALEN], sa, ETH_ALEN);
	hdr[ETH_ALEN * 2] = priority;
	hdr[ETH_ALEN * 2 + 1] = 0;
	hdr[ETH_ALEN * 2 + 2] = 0;
	hdr[ETH_ALEN * 2 + 3] = 0;

	/* Use scatter gather to MIC header and data in one go */
	sg_init_table(sg, 2);
	sg_set_buf(&sg[0], hdr, sizeof(hdr));
	sg_set_buf(&sg[1], data, data_len);

	if (crypto_ahash_setkey(tfm_michael, key, MIC_KEYLEN))
		return -1;

	ahash_request_set_tfm(req, tfm_michael);
	ahash_request_set_callback(req, 0, NULL, NULL);
	ahash_request_set_crypt(req, sg, mic, data_len + sizeof(hdr));
	err = crypto_ahash_digest(req);
	ahash_request_zero(req);
	return err;
}
