// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2022 Broadcom Corporation
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <core.h>
#include <bus.h>
#include <fwvid.h>
#include <fwil.h>

#include "vops.h"

static int brcmf_cyw_set_sae_pwd(struct brcmf_if *ifp,
				 struct cfg80211_crypto_settings *crypto)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_wsec_sae_pwd_le sae_pwd;
	u16 pwd_len = crypto->sae_pwd_len;
	int err;

	if (pwd_len > BRCMF_WSEC_MAX_SAE_PASSWORD_LEN) {
		bphy_err(drvr, "sae_password must be less than %d\n",
			 BRCMF_WSEC_MAX_SAE_PASSWORD_LEN);
		return -EINVAL;
	}

	sae_pwd.key_len = cpu_to_le16(pwd_len);
	memcpy(sae_pwd.key, crypto->sae_pwd, pwd_len);

	err = brcmf_fil_iovar_data_set(ifp, "sae_password", &sae_pwd,
				       sizeof(sae_pwd));
	if (err < 0)
		bphy_err(drvr, "failed to set SAE password in firmware (len=%u)\n",
			 pwd_len);

	return err;
}

const struct brcmf_fwvid_ops brcmf_cyw_ops = {
	.set_sae_password = brcmf_cyw_set_sae_pwd,
};
