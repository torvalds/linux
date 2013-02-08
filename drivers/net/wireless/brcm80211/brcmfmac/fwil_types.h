/*
 * Copyright (c) 2012 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


#ifndef FWIL_TYPES_H_
#define FWIL_TYPES_H_

#include <linux/if_ether.h>

enum brcmf_fil_p2p_if_types {
	BRCMF_FIL_P2P_IF_CLIENT,
	BRCMF_FIL_P2P_IF_GO,
	BRCMF_FIL_P2P_IF_DYNBCN_GO,
	BRCMF_FIL_P2P_IF_DEV,
};

struct brcmf_fil_p2p_if_le {
	u8 addr[ETH_ALEN];
	__le16 type;
	__le16 chspec;
};

struct brcmf_fil_chan_info_le {
	__le32 hw_channel;
	__le32 target_channel;
	__le32 scan_channel;
};

struct brcmf_fil_bss_enable_le {
	__le32 bsscfg_idx;
	__le32 enable;
};

#endif /* FWIL_TYPES_H_ */
