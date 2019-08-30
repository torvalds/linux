/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Copyright IBM Corp. 2019
 *  Author(s): Harald Freudenberger <freude@linux.ibm.com>
 *
 *  Collection of EP11 misc functions used by zcrypt and pkey
 */

#ifndef _ZCRYPT_EP11MISC_H_
#define _ZCRYPT_EP11MISC_H_

#include <asm/zcrypt.h>
#include <asm/pkey.h>

/* EP11 card info struct */
struct ep11_card_info {
	u32  API_ord_nr;    /* API ordinal number */
	u16  FW_version;    /* Firmware major and minor version */
	char serial[16];    /* serial number string (16 ascii, no 0x00 !) */
	u64  op_mode;	    /* card operational mode(s) */
};

/* EP11 domain info struct */
struct ep11_domain_info {
	char cur_wk_state;  /* '0' invalid, '1' valid */
	char new_wk_state;  /* '0' empty, '1' uncommitted, '2' committed */
	u8   cur_wkvp[32];  /* current wrapping key verification pattern */
	u8   new_wkvp[32];  /* new wrapping key verification pattern */
	u64  op_mode;	    /* domain operational mode(s) */
};

/*
 * Provide information about an EP11 card.
 */
int ep11_get_card_info(u16 card, struct ep11_card_info *info, int verify);

/*
 * Provide information about a domain within an EP11 card.
 */
int ep11_get_domain_info(u16 card, u16 domain, struct ep11_domain_info *info);

void zcrypt_ep11misc_exit(void);

#endif /* _ZCRYPT_EP11MISC_H_ */
