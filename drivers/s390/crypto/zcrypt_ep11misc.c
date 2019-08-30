// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright IBM Corp. 2019
 *  Author(s): Harald Freudenberger <freude@linux.ibm.com>
 *
 *  Collection of EP11 misc functions used by zcrypt and pkey
 */

#define KMSG_COMPONENT "zcrypt"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <asm/zcrypt.h>
#include <asm/pkey.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_debug.h"
#include "zcrypt_msgtype6.h"
#include "zcrypt_ep11misc.h"

#define DEBUG_DBG(...)	ZCRYPT_DBF(DBF_DEBUG, ##__VA_ARGS__)
#define DEBUG_INFO(...) ZCRYPT_DBF(DBF_INFO, ##__VA_ARGS__)
#define DEBUG_WARN(...) ZCRYPT_DBF(DBF_WARN, ##__VA_ARGS__)
#define DEBUG_ERR(...)	ZCRYPT_DBF(DBF_ERR, ##__VA_ARGS__)

/* ep11 card info cache */
struct card_list_entry {
	struct list_head list;
	u16 cardnr;
	struct ep11_card_info info;
};
static LIST_HEAD(card_list);
static DEFINE_SPINLOCK(card_list_lock);

static int card_cache_fetch(u16 cardnr, struct ep11_card_info *ci)
{
	int rc = -ENOENT;
	struct card_list_entry *ptr;

	spin_lock_bh(&card_list_lock);
	list_for_each_entry(ptr, &card_list, list) {
		if (ptr->cardnr == cardnr) {
			memcpy(ci, &ptr->info, sizeof(*ci));
			rc = 0;
			break;
		}
	}
	spin_unlock_bh(&card_list_lock);

	return rc;
}

static void card_cache_update(u16 cardnr, const struct ep11_card_info *ci)
{
	int found = 0;
	struct card_list_entry *ptr;

	spin_lock_bh(&card_list_lock);
	list_for_each_entry(ptr, &card_list, list) {
		if (ptr->cardnr == cardnr) {
			memcpy(&ptr->info, ci, sizeof(*ci));
			found = 1;
			break;
		}
	}
	if (!found) {
		ptr = kmalloc(sizeof(*ptr), GFP_ATOMIC);
		if (!ptr) {
			spin_unlock_bh(&card_list_lock);
			return;
		}
		ptr->cardnr = cardnr;
		memcpy(&ptr->info, ci, sizeof(*ci));
		list_add(&ptr->list, &card_list);
	}
	spin_unlock_bh(&card_list_lock);
}

static void card_cache_scrub(u16 cardnr)
{
	struct card_list_entry *ptr;

	spin_lock_bh(&card_list_lock);
	list_for_each_entry(ptr, &card_list, list) {
		if (ptr->cardnr == cardnr) {
			list_del(&ptr->list);
			kfree(ptr);
			break;
		}
	}
	spin_unlock_bh(&card_list_lock);
}

static void __exit card_cache_free(void)
{
	struct card_list_entry *ptr, *pnext;

	spin_lock_bh(&card_list_lock);
	list_for_each_entry_safe(ptr, pnext, &card_list, list) {
		list_del(&ptr->list);
		kfree(ptr);
	}
	spin_unlock_bh(&card_list_lock);
}

/*
 * Helper function which calls zcrypt_send_ep11_cprb with
 * memory management segment adjusted to kernel space
 * so that the copy_from_user called within this
 * function do in fact copy from kernel space.
 */
static inline int _zcrypt_send_ep11_cprb(struct ep11_urb *urb)
{
	int rc;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	rc = zcrypt_send_ep11_cprb(urb);
	set_fs(old_fs);

	return rc;
}

/*
 * Allocate and prepare ep11 cprb plus additional payload.
 */
static struct ep11_cprb *alloc_ep11_cprb(size_t payload_len)
{
	size_t len = sizeof(struct ep11_cprb) + payload_len;
	struct ep11_cprb *cprb;

	cprb = kmalloc(len, GFP_KERNEL);
	if (!cprb)
		return NULL;

	memset(cprb, 0, len);
	cprb->cprb_len = sizeof(struct ep11_cprb);
	cprb->cprb_ver_id = 0x04;
	memcpy(cprb->func_id, "T4", 2);
	cprb->ret_code = 0xFFFFFFFF;
	cprb->payload_len = payload_len;

	return cprb;
}

/*
 * Helper function which does an ep11 query with given query type.
 */
static int ep11_query_info(u16 cardnr, u16 domain, u32 query_type,
			   size_t buflen, u8 *buf)
{
	struct ep11_info_req_pl {
		u8  tag;
		u8  lenfmt;
		u8  func_tag;
		u8  func_len;
		u32 func;
		u8  dom_tag;
		u8  dom_len;
		u32 dom;
		u8  query_type_tag;
		u8  query_type_len;
		u32 query_type;
		u8  query_subtype_tag;
		u8  query_subtype_len;
		u32 query_subtype;
	} __packed * req_pl;
	struct ep11_info_rep_pl {
		u8  tag;
		u8  lenfmt;
		u16 len;
		u8  func_tag;
		u8  func_len;
		u32 func;
		u8  dom_tag;
		u8  dom_len;
		u32 dom;
		u8  rc_tag;
		u8  rc_len;
		u32 rc;
		u8  data_tag;
		u8  data_lenfmt;
		u16 data_len;
	} __packed * rep_pl;
	struct ep11_cprb *req = NULL, *rep = NULL;
	struct ep11_target_dev target;
	struct ep11_urb *urb = NULL;
	int rc = -ENOMEM;

	/* request cprb and payload */
	req = alloc_ep11_cprb(sizeof(struct ep11_info_req_pl));
	if (!req)
		goto out;
	req_pl = (struct ep11_info_req_pl *) (((u8 *) req) + sizeof(*req));
	req_pl->tag = 0x30;
	req_pl->lenfmt = sizeof(*req_pl) - 2 * sizeof(u8);
	req_pl->func_tag = 0x04;
	req_pl->func_len = sizeof(u32);
	req_pl->func = 0x00010026;
	req_pl->dom_tag = 0x04;
	req_pl->dom_len = sizeof(u32);
	req_pl->query_type_tag = 0x04;
	req_pl->query_type_len = sizeof(u32);
	req_pl->query_type = query_type;
	req_pl->query_subtype_tag = 0x04;
	req_pl->query_subtype_len = sizeof(u32);

	/* reply cprb and payload */
	rep = alloc_ep11_cprb(sizeof(struct ep11_info_rep_pl) + buflen);
	if (!rep)
		goto out;
	rep_pl = (struct ep11_info_rep_pl *) (((u8 *) rep) + sizeof(*rep));

	/* urb and target */
	urb = kmalloc(sizeof(struct ep11_urb), GFP_KERNEL);
	if (!urb)
		goto out;
	target.ap_id = cardnr;
	target.dom_id = domain;
	urb->targets_num = 1;
	urb->targets = (u8 __user *) &target;
	urb->req_len = sizeof(*req) + sizeof(*req_pl);
	urb->req = (u8 __user *) req;
	urb->resp_len = sizeof(*rep) + sizeof(*rep_pl) + buflen;
	urb->resp = (u8 __user *) rep;

	rc = _zcrypt_send_ep11_cprb(urb);
	if (rc) {
		DEBUG_ERR(
			"%s zcrypt_send_ep11_cprb(card=%d dom=%d) failed, rc=%d\n",
			__func__, (int) cardnr, (int) domain, rc);
		goto out;
	}

	rc = -EIO;
	if (rep_pl->tag != 0x30 || rep_pl->func_tag != 0x04 ||
	    rep_pl->dom_tag != 0x04 || rep_pl->rc_tag != 0x04) {
		DEBUG_ERR("%s reply tag mismatch\n", __func__);
		goto out;
	}
	if (rep_pl->rc != 0) {
		DEBUG_ERR("%s reply cprb payload rc=0x%04x\n",
			  __func__, rep_pl->rc);
		goto out;
	}
	if (rep_pl->data_tag != 0x04 || rep_pl->data_lenfmt != (0x80 + 2)) {
		DEBUG_ERR("%s unknown reply data format\n", __func__);
		goto out;
	}
	if (rep_pl->data_len > buflen) {
		DEBUG_ERR("%s mismatch between reply data len and buffer len\n",
			  __func__);
		goto out;
	}

	rc = 0;
	memcpy(buf, ((u8 *) rep_pl) + sizeof(*req_pl), rep_pl->data_len);

out:
	kfree(req);
	kfree(rep);
	kfree(urb);
	return rc;
}

/*
 * Provide information about an EP11 card.
 */
int ep11_get_card_info(u16 card, struct ep11_card_info *info, int verify)
{
	int rc;
	struct ep11_module_query_info {
		u32 API_ord_nr;
		u32 firmware_id;
		u8  FW_major_vers;
		u8  FW_minor_vers;
		u8  CSP_major_vers;
		u8  CSP_minor_vers;
		u8  fwid[32];
		u8  xcp_config_hash[32];
		u8  CSP_config_hash[32];
		u8  serial[16];
		u8  module_date_time[16];
		u64 op_mode;
		u32 PKCS11_flags;
		u32 ext_flags;
		u32 domains;
		u32 sym_state_bytes;
		u32 digest_state_bytes;
		u32 pin_blob_bytes;
		u32 SPKI_bytes;
		u32 priv_key_blob_bytes;
		u32 sym_blob_bytes;
		u32 max_payload_bytes;
		u32 CP_profile_bytes;
		u32 max_CP_index;
	} __packed * pmqi = NULL;

	rc = card_cache_fetch(card, info);
	if (rc || verify) {
		pmqi = kmalloc(sizeof(*pmqi), GFP_KERNEL);
		if (!pmqi)
			return -ENOMEM;
		rc = ep11_query_info(card, AUTOSEL_DOM,
				     0x01 /* module info query */,
				     sizeof(*pmqi), (u8 *) pmqi);
		if (rc) {
			if (rc == -ENODEV)
				card_cache_scrub(card);
			goto out;
		}
		memset(info, 0, sizeof(*info));
		info->API_ord_nr = pmqi->API_ord_nr;
		info->FW_version =
			(pmqi->FW_major_vers << 8) + pmqi->FW_minor_vers;
		memcpy(info->serial, pmqi->serial, sizeof(info->serial));
		info->op_mode = pmqi->op_mode;
		card_cache_update(card, info);
	}

out:
	kfree(pmqi);
	return rc;
}
EXPORT_SYMBOL(ep11_get_card_info);

/*
 * Provide information about a domain within an EP11 card.
 */
int ep11_get_domain_info(u16 card, u16 domain, struct ep11_domain_info *info)
{
	int rc;
	struct ep11_domain_query_info {
		u32 dom_index;
		u8  cur_WK_VP[32];
		u8  new_WK_VP[32];
		u32 dom_flags;
		u64 op_mode;
	} __packed * p_dom_info;

	p_dom_info = kmalloc(sizeof(*p_dom_info), GFP_KERNEL);
	if (!p_dom_info)
		return -ENOMEM;

	rc = ep11_query_info(card, domain, 0x03 /* domain info query */,
			     sizeof(*p_dom_info), (u8 *) p_dom_info);
	if (rc)
		goto out;

	memset(info, 0, sizeof(*info));
	info->cur_wk_state = '0';
	info->new_wk_state = '0';
	if (p_dom_info->dom_flags & 0x10 /* left imprint mode */) {
		if (p_dom_info->dom_flags & 0x02 /* cur wk valid */) {
			info->cur_wk_state = '1';
			memcpy(info->cur_wkvp, p_dom_info->cur_WK_VP, 32);
		}
		if (p_dom_info->dom_flags & 0x04 /* new wk present */
		    || p_dom_info->dom_flags & 0x08 /* new wk committed */) {
			info->new_wk_state =
				p_dom_info->dom_flags & 0x08 ? '2' : '1';
			memcpy(info->new_wkvp, p_dom_info->new_WK_VP, 32);
		}
	}
	info->op_mode = p_dom_info->op_mode;

out:
	kfree(p_dom_info);
	return rc;
}
EXPORT_SYMBOL(ep11_get_domain_info);

void __exit zcrypt_ep11misc_exit(void)
{
	card_cache_free();
}
