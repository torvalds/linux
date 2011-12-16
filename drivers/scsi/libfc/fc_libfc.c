/*
 * Copyright(c) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/crc32.h>
#include <linux/module.h>

#include <scsi/libfc.h>
#include <scsi/fc_encode.h>

#include "fc_libfc.h"

MODULE_AUTHOR("Open-FCoE.org");
MODULE_DESCRIPTION("libfc");
MODULE_LICENSE("GPL v2");

unsigned int fc_debug_logging;
module_param_named(debug_logging, fc_debug_logging, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug_logging, "a bit mask of logging levels");

DEFINE_MUTEX(fc_prov_mutex);
static LIST_HEAD(fc_local_ports);
struct blocking_notifier_head fc_lport_notifier_head =
		BLOCKING_NOTIFIER_INIT(fc_lport_notifier_head);
EXPORT_SYMBOL(fc_lport_notifier_head);

/*
 * Providers which primarily send requests and PRLIs.
 */
struct fc4_prov *fc_active_prov[FC_FC4_PROV_SIZE] = {
	[0] = &fc_rport_t0_prov,
	[FC_TYPE_FCP] = &fc_rport_fcp_init,
};

/*
 * Providers which receive requests.
 */
struct fc4_prov *fc_passive_prov[FC_FC4_PROV_SIZE] = {
	[FC_TYPE_ELS] = &fc_lport_els_prov,
};

/**
 * libfc_init() - Initialize libfc.ko
 */
static int __init libfc_init(void)
{
	int rc = 0;

	rc = fc_setup_fcp();
	if (rc)
		return rc;

	rc = fc_setup_exch_mgr();
	if (rc)
		goto destroy_pkt_cache;

	rc = fc_setup_rport();
	if (rc)
		goto destroy_em;

	return rc;
destroy_em:
	fc_destroy_exch_mgr();
destroy_pkt_cache:
	fc_destroy_fcp();
	return rc;
}
module_init(libfc_init);

/**
 * libfc_exit() - Tear down libfc.ko
 */
static void __exit libfc_exit(void)
{
	fc_destroy_fcp();
	fc_destroy_exch_mgr();
	fc_destroy_rport();
}
module_exit(libfc_exit);

/**
 * fc_copy_buffer_to_sglist() - This routine copies the data of a buffer
 *				into a scatter-gather list (SG list).
 *
 * @buf: pointer to the data buffer.
 * @len: the byte-length of the data buffer.
 * @sg: pointer to the pointer of the SG list.
 * @nents: pointer to the remaining number of entries in the SG list.
 * @offset: pointer to the current offset in the SG list.
 * @km_type: dedicated page table slot type for kmap_atomic.
 * @crc: pointer to the 32-bit crc value.
 *	 If crc is NULL, CRC is not calculated.
 */
u32 fc_copy_buffer_to_sglist(void *buf, size_t len,
			     struct scatterlist *sg,
			     u32 *nents, size_t *offset,
			     enum km_type km_type, u32 *crc)
{
	size_t remaining = len;
	u32 copy_len = 0;

	while (remaining > 0 && sg) {
		size_t off, sg_bytes;
		void *page_addr;

		if (*offset >= sg->length) {
			/*
			 * Check for end and drop resources
			 * from the last iteration.
			 */
			if (!(*nents))
				break;
			--(*nents);
			*offset -= sg->length;
			sg = sg_next(sg);
			continue;
		}
		sg_bytes = min(remaining, sg->length - *offset);

		/*
		 * The scatterlist item may be bigger than PAGE_SIZE,
		 * but we are limited to mapping PAGE_SIZE at a time.
		 */
		off = *offset + sg->offset;
		sg_bytes = min(sg_bytes,
			       (size_t)(PAGE_SIZE - (off & ~PAGE_MASK)));
		page_addr = kmap_atomic(sg_page(sg) + (off >> PAGE_SHIFT),
					km_type);
		if (crc)
			*crc = crc32(*crc, buf, sg_bytes);
		memcpy((char *)page_addr + (off & ~PAGE_MASK), buf, sg_bytes);
		kunmap_atomic(page_addr, km_type);
		buf += sg_bytes;
		*offset += sg_bytes;
		remaining -= sg_bytes;
		copy_len += sg_bytes;
	}
	return copy_len;
}

/**
 * fc_fill_hdr() -  fill FC header fields based on request
 * @fp: reply frame containing header to be filled in
 * @in_fp: request frame containing header to use in filling in reply
 * @r_ctl: R_CTL value for header
 * @f_ctl: F_CTL value for header, with 0 pad
 * @seq_cnt: sequence count for the header, ignored if frame has a sequence
 * @parm_offset: parameter / offset value
 */
void fc_fill_hdr(struct fc_frame *fp, const struct fc_frame *in_fp,
		 enum fc_rctl r_ctl, u32 f_ctl, u16 seq_cnt, u32 parm_offset)
{
	struct fc_frame_header *fh;
	struct fc_frame_header *in_fh;
	struct fc_seq *sp;
	u32 fill;

	fh = __fc_frame_header_get(fp);
	in_fh = __fc_frame_header_get(in_fp);

	if (f_ctl & FC_FC_END_SEQ) {
		fill = -fr_len(fp) & 3;
		if (fill) {
			/* TODO, this may be a problem with fragmented skb */
			memset(skb_put(fp_skb(fp), fill), 0, fill);
			f_ctl |= fill;
		}
		fr_eof(fp) = FC_EOF_T;
	} else {
		WARN_ON(fr_len(fp) % 4 != 0);	/* no pad to non last frame */
		fr_eof(fp) = FC_EOF_N;
	}

	fh->fh_r_ctl = r_ctl;
	memcpy(fh->fh_d_id, in_fh->fh_s_id, sizeof(fh->fh_d_id));
	memcpy(fh->fh_s_id, in_fh->fh_d_id, sizeof(fh->fh_s_id));
	fh->fh_type = in_fh->fh_type;
	hton24(fh->fh_f_ctl, f_ctl);
	fh->fh_ox_id = in_fh->fh_ox_id;
	fh->fh_rx_id = in_fh->fh_rx_id;
	fh->fh_cs_ctl = 0;
	fh->fh_df_ctl = 0;
	fh->fh_parm_offset = htonl(parm_offset);

	sp = fr_seq(in_fp);
	if (sp) {
		fr_seq(fp) = sp;
		fh->fh_seq_id = sp->id;
		seq_cnt = sp->cnt;
	} else {
		fh->fh_seq_id = 0;
	}
	fh->fh_seq_cnt = ntohs(seq_cnt);
	fr_sof(fp) = seq_cnt ? FC_SOF_N3 : FC_SOF_I3;
	fr_encaps(fp) = fr_encaps(in_fp);
}
EXPORT_SYMBOL(fc_fill_hdr);

/**
 * fc_fill_reply_hdr() -  fill FC reply header fields based on request
 * @fp: reply frame containing header to be filled in
 * @in_fp: request frame containing header to use in filling in reply
 * @r_ctl: R_CTL value for reply
 * @parm_offset: parameter / offset value
 */
void fc_fill_reply_hdr(struct fc_frame *fp, const struct fc_frame *in_fp,
		       enum fc_rctl r_ctl, u32 parm_offset)
{
	struct fc_seq *sp;

	sp = fr_seq(in_fp);
	if (sp)
		fr_seq(fp) = fr_dev(in_fp)->tt.seq_start_next(sp);
	fc_fill_hdr(fp, in_fp, r_ctl, FC_FCTL_RESP, 0, parm_offset);
}
EXPORT_SYMBOL(fc_fill_reply_hdr);

/**
 * fc_fc4_conf_lport_params() - Modify "service_params" of specified lport
 * if there is service provider (target provider) registered with libfc
 * for specified "fc_ft_type"
 * @lport: Local port which service_params needs to be modified
 * @type: FC-4 type, such as FC_TYPE_FCP
 */
void fc_fc4_conf_lport_params(struct fc_lport *lport, enum fc_fh_type type)
{
	struct fc4_prov *prov_entry;
	BUG_ON(type >= FC_FC4_PROV_SIZE);
	BUG_ON(!lport);
	prov_entry = fc_passive_prov[type];
	if (type == FC_TYPE_FCP) {
		if (prov_entry && prov_entry->recv)
			lport->service_params |= FCP_SPPF_TARG_FCN;
	}
}

void fc_lport_iterate(void (*notify)(struct fc_lport *, void *), void *arg)
{
	struct fc_lport *lport;

	mutex_lock(&fc_prov_mutex);
	list_for_each_entry(lport, &fc_local_ports, lport_list)
		notify(lport, arg);
	mutex_unlock(&fc_prov_mutex);
}
EXPORT_SYMBOL(fc_lport_iterate);

/**
 * fc_fc4_register_provider() - register FC-4 upper-level provider.
 * @type: FC-4 type, such as FC_TYPE_FCP
 * @prov: structure describing provider including ops vector.
 *
 * Returns 0 on success, negative error otherwise.
 */
int fc_fc4_register_provider(enum fc_fh_type type, struct fc4_prov *prov)
{
	struct fc4_prov **prov_entry;
	int ret = 0;

	if (type >= FC_FC4_PROV_SIZE)
		return -EINVAL;
	mutex_lock(&fc_prov_mutex);
	prov_entry = (prov->recv ? fc_passive_prov : fc_active_prov) + type;
	if (*prov_entry)
		ret = -EBUSY;
	else
		*prov_entry = prov;
	mutex_unlock(&fc_prov_mutex);
	return ret;
}
EXPORT_SYMBOL(fc_fc4_register_provider);

/**
 * fc_fc4_deregister_provider() - deregister FC-4 upper-level provider.
 * @type: FC-4 type, such as FC_TYPE_FCP
 * @prov: structure describing provider including ops vector.
 */
void fc_fc4_deregister_provider(enum fc_fh_type type, struct fc4_prov *prov)
{
	BUG_ON(type >= FC_FC4_PROV_SIZE);
	mutex_lock(&fc_prov_mutex);
	if (prov->recv)
		rcu_assign_pointer(fc_passive_prov[type], NULL);
	else
		rcu_assign_pointer(fc_active_prov[type], NULL);
	mutex_unlock(&fc_prov_mutex);
	synchronize_rcu();
}
EXPORT_SYMBOL(fc_fc4_deregister_provider);

/**
 * fc_fc4_add_lport() - add new local port to list and run notifiers.
 * @lport:  The new local port.
 */
void fc_fc4_add_lport(struct fc_lport *lport)
{
	mutex_lock(&fc_prov_mutex);
	list_add_tail(&lport->lport_list, &fc_local_ports);
	blocking_notifier_call_chain(&fc_lport_notifier_head,
				     FC_LPORT_EV_ADD, lport);
	mutex_unlock(&fc_prov_mutex);
}

/**
 * fc_fc4_del_lport() - remove local port from list and run notifiers.
 * @lport:  The new local port.
 */
void fc_fc4_del_lport(struct fc_lport *lport)
{
	mutex_lock(&fc_prov_mutex);
	list_del(&lport->lport_list);
	blocking_notifier_call_chain(&fc_lport_notifier_head,
				     FC_LPORT_EV_DEL, lport);
	mutex_unlock(&fc_prov_mutex);
}
