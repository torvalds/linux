// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright IBM Corp. 2019
 *  Author(s): Harald Freudenberger <freude@linux.ibm.com>
 *	       Ingo Franzki <ifranzki@linux.ibm.com>
 *
 *  Collection of CCA misc functions used by zcrypt and pkey
 */

#define KMSG_COMPONENT "zcrypt"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/zcrypt.h>
#include <asm/pkey.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_debug.h"
#include "zcrypt_msgtype6.h"
#include "zcrypt_ccamisc.h"

#define DEBUG_DBG(...)	ZCRYPT_DBF(DBF_DEBUG, ##__VA_ARGS__)
#define DEBUG_INFO(...) ZCRYPT_DBF(DBF_INFO, ##__VA_ARGS__)
#define DEBUG_WARN(...) ZCRYPT_DBF(DBF_WARN, ##__VA_ARGS__)
#define DEBUG_ERR(...)	ZCRYPT_DBF(DBF_ERR, ##__VA_ARGS__)

/* Size of parameter block used for all cca requests/replies */
#define PARMBSIZE 512

/* Size of vardata block used for some of the cca requests/replies */
#define VARDATASIZE 4096

struct cca_info_list_entry {
	struct list_head list;
	u16 cardnr;
	u16 domain;
	struct cca_info info;
};

/* a list with cca_info_list_entry entries */
static LIST_HEAD(cca_info_list);
static DEFINE_SPINLOCK(cca_info_list_lock);

/*
 * Simple check if the token is a valid CCA secure AES key
 * token. If keybitsize is given, the bitsize of the key is
 * also checked. Returns 0 on success or errno value on failure.
 */
int cca_check_secaeskeytoken(debug_info_t *dbg, int dbflvl,
			     const u8 *token, int keybitsize)

{
	struct secaeskeytoken *t = (struct secaeskeytoken *) token;

#define DBF(...) debug_sprintf_event(dbg, dbflvl, ##__VA_ARGS__)

	if (t->type != TOKTYPE_CCA_INTERNAL) {
		if (dbg)
			DBF("%s token check failed, type 0x%02x != 0x%02x\n",
			    __func__, (int) t->type, TOKTYPE_CCA_INTERNAL);
		return -EINVAL;
	}
	if (t->version != TOKVER_CCA_AES) {
		if (dbg)
			DBF("%s token check failed, version 0x%02x != 0x%02x\n",
			    __func__, (int) t->version, TOKVER_CCA_AES);
		return -EINVAL;
	}
	if (keybitsize > 0 && t->bitsize != keybitsize) {
		if (dbg)
			DBF("%s token check failed, bitsize %d != %d\n",
			    __func__, (int) t->bitsize, keybitsize);
		return -EINVAL;
	}

#undef DBF

	return 0;
}
EXPORT_SYMBOL(cca_check_secaeskeytoken);

/*
 * Allocate consecutive memory for request CPRB, request param
 * block, reply CPRB and reply param block and fill in values
 * for the common fields. Returns 0 on success or errno value
 * on failure.
 */
static int alloc_and_prep_cprbmem(size_t paramblen,
				  u8 **pcprbmem,
				  struct CPRBX **preqCPRB,
				  struct CPRBX **prepCPRB)
{
	u8 *cprbmem;
	size_t cprbplusparamblen = sizeof(struct CPRBX) + paramblen;
	struct CPRBX *preqcblk, *prepcblk;

	/*
	 * allocate consecutive memory for request CPRB, request param
	 * block, reply CPRB and reply param block
	 */
	cprbmem = kcalloc(2, cprbplusparamblen, GFP_KERNEL);
	if (!cprbmem)
		return -ENOMEM;

	preqcblk = (struct CPRBX *) cprbmem;
	prepcblk = (struct CPRBX *) (cprbmem + cprbplusparamblen);

	/* fill request cprb struct */
	preqcblk->cprb_len = sizeof(struct CPRBX);
	preqcblk->cprb_ver_id = 0x02;
	memcpy(preqcblk->func_id, "T2", 2);
	preqcblk->rpl_msgbl = cprbplusparamblen;
	if (paramblen) {
		preqcblk->req_parmb =
			((u8 *) preqcblk) + sizeof(struct CPRBX);
		preqcblk->rpl_parmb =
			((u8 *) prepcblk) + sizeof(struct CPRBX);
	}

	*pcprbmem = cprbmem;
	*preqCPRB = preqcblk;
	*prepCPRB = prepcblk;

	return 0;
}

/*
 * Free the cprb memory allocated with the function above.
 * If the scrub value is not zero, the memory is filled
 * with zeros before freeing (useful if there was some
 * clear key material in there).
 */
static void free_cprbmem(void *mem, size_t paramblen, int scrub)
{
	if (scrub)
		memzero_explicit(mem, 2 * (sizeof(struct CPRBX) + paramblen));
	kfree(mem);
}

/*
 * Helper function to prepare the xcrb struct
 */
static inline void prep_xcrb(struct ica_xcRB *pxcrb,
			     u16 cardnr,
			     struct CPRBX *preqcblk,
			     struct CPRBX *prepcblk)
{
	memset(pxcrb, 0, sizeof(*pxcrb));
	pxcrb->agent_ID = 0x4341; /* 'CA' */
	pxcrb->user_defined = (cardnr == 0xFFFF ? AUTOSELECT : cardnr);
	pxcrb->request_control_blk_length =
		preqcblk->cprb_len + preqcblk->req_parml;
	pxcrb->request_control_blk_addr = (void __user *) preqcblk;
	pxcrb->reply_control_blk_length = preqcblk->rpl_msgbl;
	pxcrb->reply_control_blk_addr = (void __user *) prepcblk;
}

/*
 * Helper function which calls zcrypt_send_cprb with
 * memory management segment adjusted to kernel space
 * so that the copy_from_user called within this
 * function do in fact copy from kernel space.
 */
static inline int _zcrypt_send_cprb(struct ica_xcRB *xcrb)
{
	int rc;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	rc = zcrypt_send_cprb(xcrb);
	set_fs(old_fs);

	return rc;
}

/*
 * Generate (random) CCA AES DATA secure key.
 */
int cca_genseckey(u16 cardnr, u16 domain,
		  u32 keytype, u8 seckey[SECKEYBLOBSIZE])
{
	int i, rc, keysize;
	int seckeysize;
	u8 *mem;
	struct CPRBX *preqcblk, *prepcblk;
	struct ica_xcRB xcrb;
	struct kgreqparm {
		u8  subfunc_code[2];
		u16 rule_array_len;
		struct lv1 {
			u16 len;
			char  key_form[8];
			char  key_length[8];
			char  key_type1[8];
			char  key_type2[8];
		} lv1;
		struct lv2 {
			u16 len;
			struct keyid {
				u16 len;
				u16 attr;
				u8  data[SECKEYBLOBSIZE];
			} keyid[6];
		} lv2;
	} __packed * preqparm;
	struct kgrepparm {
		u8  subfunc_code[2];
		u16 rule_array_len;
		struct lv3 {
			u16 len;
			u16 keyblocklen;
			struct {
				u16 toklen;
				u16 tokattr;
				u8  tok[0];
				/* ... some more data ... */
			} keyblock;
		} lv3;
	} __packed * prepparm;

	/* get already prepared memory for 2 cprbs with param block each */
	rc = alloc_and_prep_cprbmem(PARMBSIZE, &mem, &preqcblk, &prepcblk);
	if (rc)
		return rc;

	/* fill request cprb struct */
	preqcblk->domain = domain;

	/* fill request cprb param block with KG request */
	preqparm = (struct kgreqparm *) preqcblk->req_parmb;
	memcpy(preqparm->subfunc_code, "KG", 2);
	preqparm->rule_array_len = sizeof(preqparm->rule_array_len);
	preqparm->lv1.len = sizeof(struct lv1);
	memcpy(preqparm->lv1.key_form,	 "OP      ", 8);
	switch (keytype) {
	case PKEY_KEYTYPE_AES_128:
		keysize = 16;
		memcpy(preqparm->lv1.key_length, "KEYLN16 ", 8);
		break;
	case PKEY_KEYTYPE_AES_192:
		keysize = 24;
		memcpy(preqparm->lv1.key_length, "KEYLN24 ", 8);
		break;
	case PKEY_KEYTYPE_AES_256:
		keysize = 32;
		memcpy(preqparm->lv1.key_length, "KEYLN32 ", 8);
		break;
	default:
		DEBUG_ERR("%s unknown/unsupported keytype %d\n",
			  __func__, keytype);
		rc = -EINVAL;
		goto out;
	}
	memcpy(preqparm->lv1.key_type1,  "AESDATA ", 8);
	preqparm->lv2.len = sizeof(struct lv2);
	for (i = 0; i < 6; i++) {
		preqparm->lv2.keyid[i].len = sizeof(struct keyid);
		preqparm->lv2.keyid[i].attr = (i == 2 ? 0x30 : 0x10);
	}
	preqcblk->req_parml = sizeof(struct kgreqparm);

	/* fill xcrb struct */
	prep_xcrb(&xcrb, cardnr, preqcblk, prepcblk);

	/* forward xcrb with request CPRB and reply CPRB to zcrypt dd */
	rc = _zcrypt_send_cprb(&xcrb);
	if (rc) {
		DEBUG_ERR("%s zcrypt_send_cprb (cardnr=%d domain=%d) failed, errno %d\n",
			  __func__, (int) cardnr, (int) domain, rc);
		goto out;
	}

	/* check response returncode and reasoncode */
	if (prepcblk->ccp_rtcode != 0) {
		DEBUG_ERR("%s secure key generate failure, card response %d/%d\n",
			  __func__,
			  (int) prepcblk->ccp_rtcode,
			  (int) prepcblk->ccp_rscode);
		rc = -EIO;
		goto out;
	}

	/* process response cprb param block */
	prepcblk->rpl_parmb = ((u8 *) prepcblk) + sizeof(struct CPRBX);
	prepparm = (struct kgrepparm *) prepcblk->rpl_parmb;

	/* check length of the returned secure key token */
	seckeysize = prepparm->lv3.keyblock.toklen
		- sizeof(prepparm->lv3.keyblock.toklen)
		- sizeof(prepparm->lv3.keyblock.tokattr);
	if (seckeysize != SECKEYBLOBSIZE) {
		DEBUG_ERR("%s secure token size mismatch %d != %d bytes\n",
			  __func__, seckeysize, SECKEYBLOBSIZE);
		rc = -EIO;
		goto out;
	}

	/* check secure key token */
	rc = cca_check_secaeskeytoken(zcrypt_dbf_info, DBF_ERR,
				      prepparm->lv3.keyblock.tok, 8*keysize);
	if (rc) {
		rc = -EIO;
		goto out;
	}

	/* copy the generated secure key token */
	memcpy(seckey, prepparm->lv3.keyblock.tok, SECKEYBLOBSIZE);

out:
	free_cprbmem(mem, PARMBSIZE, 0);
	return rc;
}
EXPORT_SYMBOL(cca_genseckey);

/*
 * Generate an CCA AES DATA secure key with given key value.
 */
int cca_clr2seckey(u16 cardnr, u16 domain, u32 keytype,
		   const u8 *clrkey, u8 seckey[SECKEYBLOBSIZE])
{
	int rc, keysize, seckeysize;
	u8 *mem;
	struct CPRBX *preqcblk, *prepcblk;
	struct ica_xcRB xcrb;
	struct cmreqparm {
		u8  subfunc_code[2];
		u16 rule_array_len;
		char  rule_array[8];
		struct lv1 {
			u16 len;
			u8  clrkey[0];
		} lv1;
		struct lv2 {
			u16 len;
			struct keyid {
				u16 len;
				u16 attr;
				u8  data[SECKEYBLOBSIZE];
			} keyid;
		} lv2;
	} __packed * preqparm;
	struct lv2 *plv2;
	struct cmrepparm {
		u8  subfunc_code[2];
		u16 rule_array_len;
		struct lv3 {
			u16 len;
			u16 keyblocklen;
			struct {
				u16 toklen;
				u16 tokattr;
				u8  tok[0];
				/* ... some more data ... */
			} keyblock;
		} lv3;
	} __packed * prepparm;

	/* get already prepared memory for 2 cprbs with param block each */
	rc = alloc_and_prep_cprbmem(PARMBSIZE, &mem, &preqcblk, &prepcblk);
	if (rc)
		return rc;

	/* fill request cprb struct */
	preqcblk->domain = domain;

	/* fill request cprb param block with CM request */
	preqparm = (struct cmreqparm *) preqcblk->req_parmb;
	memcpy(preqparm->subfunc_code, "CM", 2);
	memcpy(preqparm->rule_array, "AES     ", 8);
	preqparm->rule_array_len =
		sizeof(preqparm->rule_array_len) + sizeof(preqparm->rule_array);
	switch (keytype) {
	case PKEY_KEYTYPE_AES_128:
		keysize = 16;
		break;
	case PKEY_KEYTYPE_AES_192:
		keysize = 24;
		break;
	case PKEY_KEYTYPE_AES_256:
		keysize = 32;
		break;
	default:
		DEBUG_ERR("%s unknown/unsupported keytype %d\n",
			  __func__, keytype);
		rc = -EINVAL;
		goto out;
	}
	preqparm->lv1.len = sizeof(struct lv1) + keysize;
	memcpy(preqparm->lv1.clrkey, clrkey, keysize);
	plv2 = (struct lv2 *) (((u8 *) &preqparm->lv2) + keysize);
	plv2->len = sizeof(struct lv2);
	plv2->keyid.len = sizeof(struct keyid);
	plv2->keyid.attr = 0x30;
	preqcblk->req_parml = sizeof(struct cmreqparm) + keysize;

	/* fill xcrb struct */
	prep_xcrb(&xcrb, cardnr, preqcblk, prepcblk);

	/* forward xcrb with request CPRB and reply CPRB to zcrypt dd */
	rc = _zcrypt_send_cprb(&xcrb);
	if (rc) {
		DEBUG_ERR("%s zcrypt_send_cprb (cardnr=%d domain=%d) failed, rc=%d\n",
			  __func__, (int) cardnr, (int) domain, rc);
		goto out;
	}

	/* check response returncode and reasoncode */
	if (prepcblk->ccp_rtcode != 0) {
		DEBUG_ERR("%s clear key import failure, card response %d/%d\n",
			  __func__,
			  (int) prepcblk->ccp_rtcode,
			  (int) prepcblk->ccp_rscode);
		rc = -EIO;
		goto out;
	}

	/* process response cprb param block */
	prepcblk->rpl_parmb = ((u8 *) prepcblk) + sizeof(struct CPRBX);
	prepparm = (struct cmrepparm *) prepcblk->rpl_parmb;

	/* check length of the returned secure key token */
	seckeysize = prepparm->lv3.keyblock.toklen
		- sizeof(prepparm->lv3.keyblock.toklen)
		- sizeof(prepparm->lv3.keyblock.tokattr);
	if (seckeysize != SECKEYBLOBSIZE) {
		DEBUG_ERR("%s secure token size mismatch %d != %d bytes\n",
			  __func__, seckeysize, SECKEYBLOBSIZE);
		rc = -EIO;
		goto out;
	}

	/* check secure key token */
	rc = cca_check_secaeskeytoken(zcrypt_dbf_info, DBF_ERR,
				      prepparm->lv3.keyblock.tok, 8*keysize);
	if (rc) {
		rc = -EIO;
		goto out;
	}

	/* copy the generated secure key token */
	memcpy(seckey, prepparm->lv3.keyblock.tok, SECKEYBLOBSIZE);

out:
	free_cprbmem(mem, PARMBSIZE, 1);
	return rc;
}
EXPORT_SYMBOL(cca_clr2seckey);

/*
 * Derive proteced key from an CCA AES DATA secure key.
 */
int cca_sec2protkey(u16 cardnr, u16 domain,
		    const u8 seckey[SECKEYBLOBSIZE],
		    u8 *protkey, u32 *protkeylen,
		    u32 *keytype)
{
	int rc;
	u8 *mem;
	struct CPRBX *preqcblk, *prepcblk;
	struct ica_xcRB xcrb;
	struct uskreqparm {
		u8  subfunc_code[2];
		u16 rule_array_len;
		struct lv1 {
			u16 len;
			u16 attr_len;
			u16 attr_flags;
		} lv1;
		struct lv2 {
			u16 len;
			u16 attr_len;
			u16 attr_flags;
			u8  token[0];	      /* cca secure key token */
		} lv2;
	} __packed * preqparm;
	struct uskrepparm {
		u8  subfunc_code[2];
		u16 rule_array_len;
		struct lv3 {
			u16 len;
			u16 attr_len;
			u16 attr_flags;
			struct cpacfkeyblock {
				u8  version;  /* version of this struct */
				u8  flags[2];
				u8  algo;
				u8  form;
				u8  pad1[3];
				u16 len;
				u8  key[64];  /* the key (len bytes) */
				u16 keyattrlen;
				u8  keyattr[32];
				u8  pad2[1];
				u8  vptype;
				u8  vp[32];  /* verification pattern */
			} keyblock;
		} lv3;
	} __packed * prepparm;

	/* get already prepared memory for 2 cprbs with param block each */
	rc = alloc_and_prep_cprbmem(PARMBSIZE, &mem, &preqcblk, &prepcblk);
	if (rc)
		return rc;

	/* fill request cprb struct */
	preqcblk->domain = domain;

	/* fill request cprb param block with USK request */
	preqparm = (struct uskreqparm *) preqcblk->req_parmb;
	memcpy(preqparm->subfunc_code, "US", 2);
	preqparm->rule_array_len = sizeof(preqparm->rule_array_len);
	preqparm->lv1.len = sizeof(struct lv1);
	preqparm->lv1.attr_len = sizeof(struct lv1) - sizeof(preqparm->lv1.len);
	preqparm->lv1.attr_flags = 0x0001;
	preqparm->lv2.len = sizeof(struct lv2) + SECKEYBLOBSIZE;
	preqparm->lv2.attr_len = sizeof(struct lv2)
		- sizeof(preqparm->lv2.len) + SECKEYBLOBSIZE;
	preqparm->lv2.attr_flags = 0x0000;
	memcpy(preqparm->lv2.token, seckey, SECKEYBLOBSIZE);
	preqcblk->req_parml = sizeof(struct uskreqparm) + SECKEYBLOBSIZE;

	/* fill xcrb struct */
	prep_xcrb(&xcrb, cardnr, preqcblk, prepcblk);

	/* forward xcrb with request CPRB and reply CPRB to zcrypt dd */
	rc = _zcrypt_send_cprb(&xcrb);
	if (rc) {
		DEBUG_ERR("%s zcrypt_send_cprb (cardnr=%d domain=%d) failed, rc=%d\n",
			  __func__, (int) cardnr, (int) domain, rc);
		goto out;
	}

	/* check response returncode and reasoncode */
	if (prepcblk->ccp_rtcode != 0) {
		DEBUG_ERR("%s unwrap secure key failure, card response %d/%d\n",
			  __func__,
			  (int) prepcblk->ccp_rtcode,
			  (int) prepcblk->ccp_rscode);
		rc = -EIO;
		goto out;
	}
	if (prepcblk->ccp_rscode != 0) {
		DEBUG_WARN("%s unwrap secure key warning, card response %d/%d\n",
			   __func__,
			   (int) prepcblk->ccp_rtcode,
			   (int) prepcblk->ccp_rscode);
	}

	/* process response cprb param block */
	prepcblk->rpl_parmb = ((u8 *) prepcblk) + sizeof(struct CPRBX);
	prepparm = (struct uskrepparm *) prepcblk->rpl_parmb;

	/* check the returned keyblock */
	if (prepparm->lv3.keyblock.version != 0x01) {
		DEBUG_ERR("%s reply param keyblock version mismatch 0x%02x != 0x01\n",
			  __func__, (int) prepparm->lv3.keyblock.version);
		rc = -EIO;
		goto out;
	}

	/* copy the tanslated protected key */
	switch (prepparm->lv3.keyblock.len) {
	case 16+32:
		/* AES 128 protected key */
		if (keytype)
			*keytype = PKEY_KEYTYPE_AES_128;
		break;
	case 24+32:
		/* AES 192 protected key */
		if (keytype)
			*keytype = PKEY_KEYTYPE_AES_192;
		break;
	case 32+32:
		/* AES 256 protected key */
		if (keytype)
			*keytype = PKEY_KEYTYPE_AES_256;
		break;
	default:
		DEBUG_ERR("%s unknown/unsupported keytype %d\n",
			  __func__, prepparm->lv3.keyblock.len);
		rc = -EIO;
		goto out;
	}
	memcpy(protkey, prepparm->lv3.keyblock.key, prepparm->lv3.keyblock.len);
	if (protkeylen)
		*protkeylen = prepparm->lv3.keyblock.len;

out:
	free_cprbmem(mem, PARMBSIZE, 0);
	return rc;
}
EXPORT_SYMBOL(cca_sec2protkey);

/*
 * query cryptographic facility from CCA adapter
 */
int cca_query_crypto_facility(u16 cardnr, u16 domain,
			      const char *keyword,
			      u8 *rarray, size_t *rarraylen,
			      u8 *varray, size_t *varraylen)
{
	int rc;
	u16 len;
	u8 *mem, *ptr;
	struct CPRBX *preqcblk, *prepcblk;
	struct ica_xcRB xcrb;
	struct fqreqparm {
		u8  subfunc_code[2];
		u16 rule_array_len;
		char  rule_array[8];
		struct lv1 {
			u16 len;
			u8  data[VARDATASIZE];
		} lv1;
		u16 dummylen;
	} __packed * preqparm;
	size_t parmbsize = sizeof(struct fqreqparm);
	struct fqrepparm {
		u8  subfunc_code[2];
		u8  lvdata[0];
	} __packed * prepparm;

	/* get already prepared memory for 2 cprbs with param block each */
	rc = alloc_and_prep_cprbmem(parmbsize, &mem, &preqcblk, &prepcblk);
	if (rc)
		return rc;

	/* fill request cprb struct */
	preqcblk->domain = domain;

	/* fill request cprb param block with FQ request */
	preqparm = (struct fqreqparm *) preqcblk->req_parmb;
	memcpy(preqparm->subfunc_code, "FQ", 2);
	memcpy(preqparm->rule_array, keyword, sizeof(preqparm->rule_array));
	preqparm->rule_array_len =
		sizeof(preqparm->rule_array_len) + sizeof(preqparm->rule_array);
	preqparm->lv1.len = sizeof(preqparm->lv1);
	preqparm->dummylen = sizeof(preqparm->dummylen);
	preqcblk->req_parml = parmbsize;

	/* fill xcrb struct */
	prep_xcrb(&xcrb, cardnr, preqcblk, prepcblk);

	/* forward xcrb with request CPRB and reply CPRB to zcrypt dd */
	rc = _zcrypt_send_cprb(&xcrb);
	if (rc) {
		DEBUG_ERR("%s zcrypt_send_cprb (cardnr=%d domain=%d) failed, rc=%d\n",
			  __func__, (int) cardnr, (int) domain, rc);
		goto out;
	}

	/* check response returncode and reasoncode */
	if (prepcblk->ccp_rtcode != 0) {
		DEBUG_ERR("%s unwrap secure key failure, card response %d/%d\n",
			  __func__,
			  (int) prepcblk->ccp_rtcode,
			  (int) prepcblk->ccp_rscode);
		rc = -EIO;
		goto out;
	}

	/* process response cprb param block */
	prepcblk->rpl_parmb = ((u8 *) prepcblk) + sizeof(struct CPRBX);
	prepparm = (struct fqrepparm *) prepcblk->rpl_parmb;
	ptr = prepparm->lvdata;

	/* check and possibly copy reply rule array */
	len = *((u16 *) ptr);
	if (len > sizeof(u16)) {
		ptr += sizeof(u16);
		len -= sizeof(u16);
		if (rarray && rarraylen && *rarraylen > 0) {
			*rarraylen = (len > *rarraylen ? *rarraylen : len);
			memcpy(rarray, ptr, *rarraylen);
		}
		ptr += len;
	}
	/* check and possible copy reply var array */
	len = *((u16 *) ptr);
	if (len > sizeof(u16)) {
		ptr += sizeof(u16);
		len -= sizeof(u16);
		if (varray && varraylen && *varraylen > 0) {
			*varraylen = (len > *varraylen ? *varraylen : len);
			memcpy(varray, ptr, *varraylen);
		}
		ptr += len;
	}

out:
	free_cprbmem(mem, parmbsize, 0);
	return rc;
}
EXPORT_SYMBOL(cca_query_crypto_facility);

static int cca_info_cache_fetch(u16 cardnr, u16 domain, struct cca_info *ci)
{
	int rc = -ENOENT;
	struct cca_info_list_entry *ptr;

	spin_lock_bh(&cca_info_list_lock);
	list_for_each_entry(ptr, &cca_info_list, list) {
		if (ptr->cardnr == cardnr && ptr->domain == domain) {
			memcpy(ci, &ptr->info, sizeof(*ci));
			rc = 0;
			break;
		}
	}
	spin_unlock_bh(&cca_info_list_lock);

	return rc;
}

static void cca_info_cache_update(u16 cardnr, u16 domain,
				  const struct cca_info *ci)
{
	int found = 0;
	struct cca_info_list_entry *ptr;

	spin_lock_bh(&cca_info_list_lock);
	list_for_each_entry(ptr, &cca_info_list, list) {
		if (ptr->cardnr == cardnr &&
		    ptr->domain == domain) {
			memcpy(&ptr->info, ci, sizeof(*ci));
			found = 1;
			break;
		}
	}
	if (!found) {
		ptr = kmalloc(sizeof(*ptr), GFP_ATOMIC);
		if (!ptr) {
			spin_unlock_bh(&cca_info_list_lock);
			return;
		}
		ptr->cardnr = cardnr;
		ptr->domain = domain;
		memcpy(&ptr->info, ci, sizeof(*ci));
		list_add(&ptr->list, &cca_info_list);
	}
	spin_unlock_bh(&cca_info_list_lock);
}

static void cca_info_cache_scrub(u16 cardnr, u16 domain)
{
	struct cca_info_list_entry *ptr;

	spin_lock_bh(&cca_info_list_lock);
	list_for_each_entry(ptr, &cca_info_list, list) {
		if (ptr->cardnr == cardnr &&
		    ptr->domain == domain) {
			list_del(&ptr->list);
			kfree(ptr);
			break;
		}
	}
	spin_unlock_bh(&cca_info_list_lock);
}

static void __exit mkvp_cache_free(void)
{
	struct cca_info_list_entry *ptr, *pnext;

	spin_lock_bh(&cca_info_list_lock);
	list_for_each_entry_safe(ptr, pnext, &cca_info_list, list) {
		list_del(&ptr->list);
		kfree(ptr);
	}
	spin_unlock_bh(&cca_info_list_lock);
}

/*
 * Fetch cca_info values via query_crypto_facility from adapter.
 */
static int fetch_cca_info(u16 cardnr, u16 domain, struct cca_info *ci)
{
	int rc, found = 0;
	size_t rlen, vlen;
	u8 *rarray, *varray, *pg;

	pg = (u8 *) __get_free_page(GFP_KERNEL);
	if (!pg)
		return -ENOMEM;
	rarray = pg;
	varray = pg + PAGE_SIZE/2;
	rlen = vlen = PAGE_SIZE/2;

	rc = cca_query_crypto_facility(cardnr, domain, "STATICSA",
				       rarray, &rlen, varray, &vlen);
	if (rc == 0 && rlen >= 10*8 && vlen >= 204) {
		memset(ci, 0, sizeof(*ci));
		memcpy(ci->serial, rarray, 8);
		ci->new_mk_state = (char) rarray[7*8];
		ci->cur_mk_state = (char) rarray[8*8];
		ci->old_mk_state = (char) rarray[9*8];
		if (ci->old_mk_state == '2')
			memcpy(&ci->old_mkvp, varray + 172, 8);
		if (ci->cur_mk_state == '2')
			memcpy(&ci->cur_mkvp, varray + 184, 8);
		if (ci->new_mk_state == '3')
			memcpy(&ci->new_mkvp, varray + 196, 8);
		found = 1;
	}

	free_page((unsigned long) pg);

	return found ? 0 : -ENOENT;
}

/*
 * Fetch cca information about a CCA queue.
 */
int cca_get_info(u16 card, u16 dom, struct cca_info *ci, int verify)
{
	int rc;

	rc = cca_info_cache_fetch(card, dom, ci);
	if (rc || verify) {
		rc = fetch_cca_info(card, dom, ci);
		if (rc == 0)
			cca_info_cache_update(card, dom, ci);
	}

	return rc;
}
EXPORT_SYMBOL(cca_get_info);

/*
 * Search for a matching crypto card based on the Master Key
 * Verification Pattern provided inside a secure key.
 * Returns < 0 on failure, 0 if CURRENT MKVP matches and
 * 1 if OLD MKVP matches.
 */
int cca_findcard(const u8 *seckey, u16 *pcardnr, u16 *pdomain, int verify)
{
	const struct secaeskeytoken *t = (const struct secaeskeytoken *) seckey;
	struct zcrypt_device_status_ext *device_status;
	u16 card, dom;
	struct cca_info ci;
	int i, rc, oi = -1;

	/* some simple checks of the given secure key token */
	if (t->type != TOKTYPE_CCA_INTERNAL ||
	    t->version != TOKVER_CCA_AES ||
	    t->mkvp == 0)
		return -EINVAL;

	/* fetch status of all crypto cards */
	device_status = kmalloc_array(MAX_ZDEV_ENTRIES_EXT,
				      sizeof(struct zcrypt_device_status_ext),
				      GFP_KERNEL);
	if (!device_status)
		return -ENOMEM;
	zcrypt_device_status_mask_ext(device_status);

	/* walk through all crypto cards */
	for (i = 0; i < MAX_ZDEV_ENTRIES_EXT; i++) {
		card = AP_QID_CARD(device_status[i].qid);
		dom = AP_QID_QUEUE(device_status[i].qid);
		if (device_status[i].online &&
		    device_status[i].functions & 0x04) {
			/* enabled CCA card, check current mkvp from cache */
			if (cca_info_cache_fetch(card, dom, &ci) == 0 &&
			    ci.cur_mk_state == '2' &&
			    ci.cur_mkvp == t->mkvp) {
				if (!verify)
					break;
				/* verify: refresh card info */
				if (fetch_cca_info(card, dom, &ci) == 0) {
					cca_info_cache_update(card, dom, &ci);
					if (ci.cur_mk_state == '2' &&
					    ci.cur_mkvp == t->mkvp)
						break;
				}
			}
		} else {
			/* Card is offline and/or not a CCA card. */
			/* del mkvp entry from cache if it exists */
			cca_info_cache_scrub(card, dom);
		}
	}
	if (i >= MAX_ZDEV_ENTRIES_EXT) {
		/* nothing found, so this time without cache */
		for (i = 0; i < MAX_ZDEV_ENTRIES_EXT; i++) {
			if (!(device_status[i].online &&
			      device_status[i].functions & 0x04))
				continue;
			card = AP_QID_CARD(device_status[i].qid);
			dom = AP_QID_QUEUE(device_status[i].qid);
			/* fresh fetch mkvp from adapter */
			if (fetch_cca_info(card, dom, &ci) == 0) {
				cca_info_cache_update(card, dom, &ci);
				if (ci.cur_mk_state == '2' &&
				    ci.cur_mkvp == t->mkvp)
					break;
				if (ci.old_mk_state == '2' &&
				    ci.old_mkvp == t->mkvp &&
				    oi < 0)
					oi = i;
			}
		}
		if (i >= MAX_ZDEV_ENTRIES_EXT && oi >= 0) {
			/* old mkvp matched, use this card then */
			card = AP_QID_CARD(device_status[oi].qid);
			dom = AP_QID_QUEUE(device_status[oi].qid);
		}
	}
	if (i < MAX_ZDEV_ENTRIES_EXT || oi >= 0) {
		if (pcardnr)
			*pcardnr = card;
		if (pdomain)
			*pdomain = dom;
		rc = (i < MAX_ZDEV_ENTRIES_EXT ? 0 : 1);
	} else
		rc = -ENODEV;

	kfree(device_status);
	return rc;
}
EXPORT_SYMBOL(cca_findcard);

void __exit zcrypt_ccamisc_exit(void)
{
	mkvp_cache_free();
}
