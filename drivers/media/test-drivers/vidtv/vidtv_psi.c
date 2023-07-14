// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains the logic to work with MPEG Program-Specific Information.
 * These are defined both in ISO/IEC 13818-1 (systems) and ETSI EN 300 468.
 * PSI is carried in the form of table structures, and although each table might
 * technically be broken into one or more sections, we do not do this here,
 * hence 'table' and 'section' are interchangeable for vidtv.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s, %d: " fmt, __func__, __LINE__

#include <linux/bcd.h>
#include <linux/crc32.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/types.h>

#include "vidtv_common.h"
#include "vidtv_psi.h"
#include "vidtv_ts.h"

#define CRC_SIZE_IN_BYTES 4
#define MAX_VERSION_NUM 32
#define INITIAL_CRC 0xffffffff
#define ISO_LANGUAGE_CODE_LEN 3

static const u32 CRC_LUT[256] = {
	/* from libdvbv5 */
	0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
	0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
	0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
	0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
	0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
	0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
	0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
	0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
	0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
	0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
	0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
	0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
	0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
	0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
	0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
	0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
	0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
	0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
	0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
	0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
	0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
	0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
	0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
	0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
	0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
	0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
	0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
	0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
	0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
	0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
	0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
	0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
	0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
	0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
	0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
	0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
	0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
	0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
	0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
	0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
	0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
	0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
	0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

static u32 dvb_crc32(u32 crc, u8 *data, u32 len)
{
	/* from libdvbv5 */
	while (len--)
		crc = (crc << 8) ^ CRC_LUT[((crc >> 24) ^ *data++) & 0xff];
	return crc;
}

static void vidtv_psi_update_version_num(struct vidtv_psi_table_header *h)
{
	h->version++;
}

static u16 vidtv_psi_get_sec_len(struct vidtv_psi_table_header *h)
{
	u16 mask;

	mask = GENMASK(11, 0);

	return be16_to_cpu(h->bitfield) & mask;
}

u16 vidtv_psi_get_pat_program_pid(struct vidtv_psi_table_pat_program *p)
{
	u16 mask;

	mask = GENMASK(12, 0);

	return be16_to_cpu(p->bitfield) & mask;
}

u16 vidtv_psi_pmt_stream_get_elem_pid(struct vidtv_psi_table_pmt_stream *s)
{
	u16 mask;

	mask = GENMASK(12, 0);

	return be16_to_cpu(s->bitfield) & mask;
}

static void vidtv_psi_set_desc_loop_len(__be16 *bitfield, u16 new_len,
					u8 desc_len_nbits)
{
	__be16 new;
	u16 mask;

	mask = GENMASK(15, desc_len_nbits);

	new = cpu_to_be16((be16_to_cpu(*bitfield) & mask) | new_len);
	*bitfield = new;
}

static void vidtv_psi_set_sec_len(struct vidtv_psi_table_header *h, u16 new_len)
{
	u16 old_len = vidtv_psi_get_sec_len(h);
	__be16 new;
	u16 mask;

	mask = GENMASK(15, 13);

	new = cpu_to_be16((be16_to_cpu(h->bitfield) & mask) | new_len);

	if (old_len > MAX_SECTION_LEN)
		pr_warn_ratelimited("section length: %d > %d, old len was %d\n",
				    new_len,
				    MAX_SECTION_LEN,
				    old_len);

	h->bitfield = new;
}

/*
 * Packetize PSI sections into TS packets:
 * push a TS header (4bytes) every 184 bytes
 * manage the continuity_counter
 * add stuffing (i.e. padding bytes) after the CRC
 */
static u32 vidtv_psi_ts_psi_write_into(struct psi_write_args *args)
{
	struct vidtv_mpeg_ts ts_header = {
		.sync_byte = TS_SYNC_BYTE,
		.bitfield = cpu_to_be16((args->new_psi_section << 14) | args->pid),
		.scrambling = 0,
		.payload = 1,
		.adaptation_field = 0, /* no adaptation field */
	};
	u32 nbytes_past_boundary = (args->dest_offset % TS_PACKET_LEN);
	bool aligned = (nbytes_past_boundary == 0);
	u32 remaining_len = args->len;
	u32 payload_write_len = 0;
	u32 payload_offset = 0;
	u32 nbytes = 0;

	if (!args->crc && !args->is_crc)
		pr_warn_ratelimited("Missing CRC for chunk\n");

	if (args->crc)
		*args->crc = dvb_crc32(*args->crc, args->from, args->len);

	if (args->new_psi_section && !aligned) {
		pr_warn_ratelimited("Cannot write a new PSI section in a misaligned buffer\n");

		/* forcibly align and hope for the best */
		nbytes += vidtv_memset(args->dest_buf,
				       args->dest_offset + nbytes,
				       args->dest_buf_sz,
				       TS_FILL_BYTE,
				       TS_PACKET_LEN - nbytes_past_boundary);
	}

	while (remaining_len) {
		nbytes_past_boundary = (args->dest_offset + nbytes) % TS_PACKET_LEN;
		aligned = (nbytes_past_boundary == 0);

		if (aligned) {
			/* if at a packet boundary, write a new TS header */
			ts_header.continuity_counter = *args->continuity_counter;

			nbytes += vidtv_memcpy(args->dest_buf,
					       args->dest_offset + nbytes,
					       args->dest_buf_sz,
					       &ts_header,
					       sizeof(ts_header));
			/*
			 * This will trigger a discontinuity if the buffer is full,
			 * effectively dropping the packet.
			 */
			vidtv_ts_inc_cc(args->continuity_counter);
		}

		/* write the pointer_field in the first byte of the payload */
		if (args->new_psi_section)
			nbytes += vidtv_memset(args->dest_buf,
					       args->dest_offset + nbytes,
					       args->dest_buf_sz,
					       0x0,
					       1);

		/* write as much of the payload as possible */
		nbytes_past_boundary = (args->dest_offset + nbytes) % TS_PACKET_LEN;
		payload_write_len = min(TS_PACKET_LEN - nbytes_past_boundary, remaining_len);

		nbytes += vidtv_memcpy(args->dest_buf,
				       args->dest_offset + nbytes,
				       args->dest_buf_sz,
				       args->from + payload_offset,
				       payload_write_len);

		/* 'payload_write_len' written from a total of 'len' requested*/
		remaining_len -= payload_write_len;
		payload_offset += payload_write_len;
	}

	/*
	 * fill the rest of the packet if there is any remaining space unused
	 */

	nbytes_past_boundary = (args->dest_offset + nbytes) % TS_PACKET_LEN;

	if (args->is_crc)
		nbytes += vidtv_memset(args->dest_buf,
				       args->dest_offset + nbytes,
				       args->dest_buf_sz,
				       TS_FILL_BYTE,
				       TS_PACKET_LEN - nbytes_past_boundary);

	return nbytes;
}

static u32 table_section_crc32_write_into(struct crc32_write_args *args)
{
	struct psi_write_args psi_args = {
		.dest_buf           = args->dest_buf,
		.from               = &args->crc,
		.len                = CRC_SIZE_IN_BYTES,
		.dest_offset        = args->dest_offset,
		.pid                = args->pid,
		.new_psi_section    = false,
		.continuity_counter = args->continuity_counter,
		.is_crc             = true,
		.dest_buf_sz        = args->dest_buf_sz,
	};

	/* the CRC is the last entry in the section */

	return vidtv_psi_ts_psi_write_into(&psi_args);
}

static void vidtv_psi_desc_chain(struct vidtv_psi_desc *head, struct vidtv_psi_desc *desc)
{
	if (head) {
		while (head->next)
			head = head->next;

		head->next = desc;
	}
}

struct vidtv_psi_desc_service *vidtv_psi_service_desc_init(struct vidtv_psi_desc *head,
							   enum service_type service_type,
							   char *service_name,
							   char *provider_name)
{
	struct vidtv_psi_desc_service *desc;
	u32 service_name_len = service_name ? strlen(service_name) : 0;
	u32 provider_name_len = provider_name ? strlen(provider_name) : 0;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	desc->type = SERVICE_DESCRIPTOR;

	desc->length = sizeof_field(struct vidtv_psi_desc_service, service_type)
		       + sizeof_field(struct vidtv_psi_desc_service, provider_name_len)
		       + provider_name_len
		       + sizeof_field(struct vidtv_psi_desc_service, service_name_len)
		       + service_name_len;

	desc->service_type = service_type;

	desc->service_name_len = service_name_len;

	if (service_name && service_name_len)
		desc->service_name = kstrdup(service_name, GFP_KERNEL);

	desc->provider_name_len = provider_name_len;

	if (provider_name && provider_name_len)
		desc->provider_name = kstrdup(provider_name, GFP_KERNEL);

	vidtv_psi_desc_chain(head, (struct vidtv_psi_desc *)desc);
	return desc;
}

struct vidtv_psi_desc_registration
*vidtv_psi_registration_desc_init(struct vidtv_psi_desc *head,
				  __be32 format_id,
				  u8 *additional_ident_info,
				  u32 additional_info_len)
{
	struct vidtv_psi_desc_registration *desc;

	desc = kzalloc(sizeof(*desc) + sizeof(format_id) + additional_info_len, GFP_KERNEL);
	if (!desc)
		return NULL;

	desc->type = REGISTRATION_DESCRIPTOR;

	desc->length = sizeof_field(struct vidtv_psi_desc_registration, format_id)
		       + additional_info_len;

	desc->format_id = format_id;

	if (additional_ident_info && additional_info_len)
		memcpy(desc->additional_identification_info,
		       additional_ident_info,
		       additional_info_len);

	vidtv_psi_desc_chain(head, (struct vidtv_psi_desc *)desc);
	return desc;
}

struct vidtv_psi_desc_network_name
*vidtv_psi_network_name_desc_init(struct vidtv_psi_desc *head, char *network_name)
{
	u32 network_name_len = network_name ? strlen(network_name) : 0;
	struct vidtv_psi_desc_network_name *desc;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	desc->type = NETWORK_NAME_DESCRIPTOR;

	desc->length = network_name_len;

	if (network_name && network_name_len)
		desc->network_name = kstrdup(network_name, GFP_KERNEL);

	vidtv_psi_desc_chain(head, (struct vidtv_psi_desc *)desc);
	return desc;
}

struct vidtv_psi_desc_service_list
*vidtv_psi_service_list_desc_init(struct vidtv_psi_desc *head,
				  struct vidtv_psi_desc_service_list_entry *entry)
{
	struct vidtv_psi_desc_service_list_entry *curr_e = NULL;
	struct vidtv_psi_desc_service_list_entry *head_e = NULL;
	struct vidtv_psi_desc_service_list_entry *prev_e = NULL;
	struct vidtv_psi_desc_service_list *desc;
	u16 length = 0;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	desc->type = SERVICE_LIST_DESCRIPTOR;

	while (entry) {
		curr_e = kzalloc(sizeof(*curr_e), GFP_KERNEL);
		if (!curr_e) {
			while (head_e) {
				curr_e = head_e;
				head_e = head_e->next;
				kfree(curr_e);
			}
			kfree(desc);
			return NULL;
		}

		curr_e->service_id = entry->service_id;
		curr_e->service_type = entry->service_type;

		length += sizeof(struct vidtv_psi_desc_service_list_entry) -
			  sizeof(struct vidtv_psi_desc_service_list_entry *);

		if (!head_e)
			head_e = curr_e;
		if (prev_e)
			prev_e->next = curr_e;

		prev_e = curr_e;
		entry = entry->next;
	}

	desc->length = length;
	desc->service_list = head_e;

	vidtv_psi_desc_chain(head, (struct vidtv_psi_desc *)desc);
	return desc;
}

struct vidtv_psi_desc_short_event
*vidtv_psi_short_event_desc_init(struct vidtv_psi_desc *head,
				 char *iso_language_code,
				 char *event_name,
				 char *text)
{
	u32 iso_len =  iso_language_code ? strlen(iso_language_code) : 0;
	u32 event_name_len = event_name ? strlen(event_name) : 0;
	struct vidtv_psi_desc_short_event *desc;
	u32 text_len =  text ? strlen(text) : 0;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	desc->type = SHORT_EVENT_DESCRIPTOR;

	desc->length = ISO_LANGUAGE_CODE_LEN +
		       sizeof_field(struct vidtv_psi_desc_short_event, event_name_len) +
		       event_name_len +
		       sizeof_field(struct vidtv_psi_desc_short_event, text_len) +
		       text_len;

	desc->event_name_len = event_name_len;
	desc->text_len = text_len;

	if (iso_len != ISO_LANGUAGE_CODE_LEN)
		iso_language_code = "eng";

	desc->iso_language_code = kstrdup(iso_language_code, GFP_KERNEL);

	if (event_name && event_name_len)
		desc->event_name = kstrdup(event_name, GFP_KERNEL);

	if (text && text_len)
		desc->text = kstrdup(text, GFP_KERNEL);

	vidtv_psi_desc_chain(head, (struct vidtv_psi_desc *)desc);
	return desc;
}

struct vidtv_psi_desc *vidtv_psi_desc_clone(struct vidtv_psi_desc *desc)
{
	struct vidtv_psi_desc_network_name *desc_network_name;
	struct vidtv_psi_desc_service_list *desc_service_list;
	struct vidtv_psi_desc_short_event  *desc_short_event;
	struct vidtv_psi_desc_service *service;
	struct vidtv_psi_desc *head = NULL;
	struct vidtv_psi_desc *prev = NULL;
	struct vidtv_psi_desc *curr = NULL;

	while (desc) {
		switch (desc->type) {
		case SERVICE_DESCRIPTOR:
			service = (struct vidtv_psi_desc_service *)desc;
			curr = (struct vidtv_psi_desc *)
			       vidtv_psi_service_desc_init(head,
							   service->service_type,
							   service->service_name,
							   service->provider_name);
		break;

		case NETWORK_NAME_DESCRIPTOR:
			desc_network_name = (struct vidtv_psi_desc_network_name *)desc;
			curr = (struct vidtv_psi_desc *)
			       vidtv_psi_network_name_desc_init(head,
								desc_network_name->network_name);
		break;

		case SERVICE_LIST_DESCRIPTOR:
			desc_service_list = (struct vidtv_psi_desc_service_list *)desc;
			curr = (struct vidtv_psi_desc *)
			       vidtv_psi_service_list_desc_init(head,
								desc_service_list->service_list);
		break;

		case SHORT_EVENT_DESCRIPTOR:
			desc_short_event = (struct vidtv_psi_desc_short_event *)desc;
			curr = (struct vidtv_psi_desc *)
			       vidtv_psi_short_event_desc_init(head,
							       desc_short_event->iso_language_code,
							       desc_short_event->event_name,
							       desc_short_event->text);
		break;

		case REGISTRATION_DESCRIPTOR:
		default:
			curr = kmemdup(desc, sizeof(*desc) + desc->length, GFP_KERNEL);
			if (!curr)
				return NULL;
		}

		if (!curr)
			return NULL;

		curr->next = NULL;
		if (!head)
			head = curr;
		if (prev)
			prev->next = curr;

		prev = curr;
		desc = desc->next;
	}

	return head;
}

void vidtv_psi_desc_destroy(struct vidtv_psi_desc *desc)
{
	struct vidtv_psi_desc_service_list_entry *sl_entry_tmp = NULL;
	struct vidtv_psi_desc_service_list_entry *sl_entry = NULL;
	struct vidtv_psi_desc *curr = desc;
	struct vidtv_psi_desc *tmp  = NULL;

	while (curr) {
		tmp  = curr;
		curr = curr->next;

		switch (tmp->type) {
		case SERVICE_DESCRIPTOR:
			kfree(((struct vidtv_psi_desc_service *)tmp)->provider_name);
			kfree(((struct vidtv_psi_desc_service *)tmp)->service_name);

			break;
		case REGISTRATION_DESCRIPTOR:
			/* nothing to do */
			break;

		case NETWORK_NAME_DESCRIPTOR:
			kfree(((struct vidtv_psi_desc_network_name *)tmp)->network_name);
			break;

		case SERVICE_LIST_DESCRIPTOR:
			sl_entry = ((struct vidtv_psi_desc_service_list *)tmp)->service_list;
			while (sl_entry) {
				sl_entry_tmp = sl_entry;
				sl_entry = sl_entry->next;
				kfree(sl_entry_tmp);
			}
			break;

		case SHORT_EVENT_DESCRIPTOR:
			kfree(((struct vidtv_psi_desc_short_event *)tmp)->iso_language_code);
			kfree(((struct vidtv_psi_desc_short_event *)tmp)->event_name);
			kfree(((struct vidtv_psi_desc_short_event *)tmp)->text);
		break;

		default:
			pr_warn_ratelimited("Possible leak: not handling descriptor type %d\n",
					    tmp->type);
			break;
		}

		kfree(tmp);
	}
}

static u16
vidtv_psi_desc_comp_loop_len(struct vidtv_psi_desc *desc)
{
	u32 length = 0;

	if (!desc)
		return 0;

	while (desc) {
		length += sizeof_field(struct vidtv_psi_desc, type);
		length += sizeof_field(struct vidtv_psi_desc, length);
		length += desc->length; /* from 'length' field until the end of the descriptor */
		desc    = desc->next;
	}

	return length;
}

void vidtv_psi_desc_assign(struct vidtv_psi_desc **to,
			   struct vidtv_psi_desc *desc)
{
	if (desc == *to)
		return;

	if (*to)
		vidtv_psi_desc_destroy(*to);

	*to = desc;
}

void vidtv_pmt_desc_assign(struct vidtv_psi_table_pmt *pmt,
			   struct vidtv_psi_desc **to,
			   struct vidtv_psi_desc *desc)
{
	vidtv_psi_desc_assign(to, desc);
	vidtv_psi_pmt_table_update_sec_len(pmt);

	if (vidtv_psi_get_sec_len(&pmt->header) > MAX_SECTION_LEN)
		vidtv_psi_desc_assign(to, NULL);

	vidtv_psi_update_version_num(&pmt->header);
}

void vidtv_sdt_desc_assign(struct vidtv_psi_table_sdt *sdt,
			   struct vidtv_psi_desc **to,
			   struct vidtv_psi_desc *desc)
{
	vidtv_psi_desc_assign(to, desc);
	vidtv_psi_sdt_table_update_sec_len(sdt);

	if (vidtv_psi_get_sec_len(&sdt->header) > MAX_SECTION_LEN)
		vidtv_psi_desc_assign(to, NULL);

	vidtv_psi_update_version_num(&sdt->header);
}

static u32 vidtv_psi_desc_write_into(struct desc_write_args *args)
{
	struct psi_write_args psi_args = {
		.dest_buf           = args->dest_buf,
		.from               = &args->desc->type,
		.pid                = args->pid,
		.new_psi_section    = false,
		.continuity_counter = args->continuity_counter,
		.is_crc             = false,
		.dest_buf_sz        = args->dest_buf_sz,
		.crc                = args->crc,
		.len		    = sizeof_field(struct vidtv_psi_desc, type) +
				      sizeof_field(struct vidtv_psi_desc, length),
	};
	struct vidtv_psi_desc_service_list_entry *serv_list_entry = NULL;
	u32 nbytes = 0;

	psi_args.dest_offset        = args->dest_offset + nbytes;

	nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

	switch (args->desc->type) {
	case SERVICE_DESCRIPTOR:
		psi_args.dest_offset = args->dest_offset + nbytes;
		psi_args.len = sizeof_field(struct vidtv_psi_desc_service, service_type) +
			       sizeof_field(struct vidtv_psi_desc_service, provider_name_len);
		psi_args.from = &((struct vidtv_psi_desc_service *)args->desc)->service_type;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		psi_args.dest_offset = args->dest_offset + nbytes;
		psi_args.len = ((struct vidtv_psi_desc_service *)args->desc)->provider_name_len;
		psi_args.from = ((struct vidtv_psi_desc_service *)args->desc)->provider_name;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		psi_args.dest_offset = args->dest_offset + nbytes;
		psi_args.len = sizeof_field(struct vidtv_psi_desc_service, service_name_len);
		psi_args.from = &((struct vidtv_psi_desc_service *)args->desc)->service_name_len;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		psi_args.dest_offset = args->dest_offset + nbytes;
		psi_args.len = ((struct vidtv_psi_desc_service *)args->desc)->service_name_len;
		psi_args.from = ((struct vidtv_psi_desc_service *)args->desc)->service_name;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);
		break;

	case NETWORK_NAME_DESCRIPTOR:
		psi_args.dest_offset = args->dest_offset + nbytes;
		psi_args.len = args->desc->length;
		psi_args.from = ((struct vidtv_psi_desc_network_name *)args->desc)->network_name;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);
		break;

	case SERVICE_LIST_DESCRIPTOR:
		serv_list_entry = ((struct vidtv_psi_desc_service_list *)args->desc)->service_list;
		while (serv_list_entry) {
			psi_args.dest_offset = args->dest_offset + nbytes;
			psi_args.len = sizeof(struct vidtv_psi_desc_service_list_entry) -
				       sizeof(struct vidtv_psi_desc_service_list_entry *);
			psi_args.from = serv_list_entry;

			nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

			serv_list_entry = serv_list_entry->next;
		}
		break;

	case SHORT_EVENT_DESCRIPTOR:
		psi_args.dest_offset = args->dest_offset + nbytes;
		psi_args.len = ISO_LANGUAGE_CODE_LEN;
		psi_args.from = ((struct vidtv_psi_desc_short_event *)
				  args->desc)->iso_language_code;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		psi_args.dest_offset = args->dest_offset + nbytes;
		psi_args.len = sizeof_field(struct vidtv_psi_desc_short_event, event_name_len);
		psi_args.from = &((struct vidtv_psi_desc_short_event *)
				  args->desc)->event_name_len;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		psi_args.dest_offset = args->dest_offset + nbytes;
		psi_args.len = ((struct vidtv_psi_desc_short_event *)args->desc)->event_name_len;
		psi_args.from = ((struct vidtv_psi_desc_short_event *)args->desc)->event_name;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		psi_args.dest_offset = args->dest_offset + nbytes;
		psi_args.len = sizeof_field(struct vidtv_psi_desc_short_event, text_len);
		psi_args.from = &((struct vidtv_psi_desc_short_event *)args->desc)->text_len;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		psi_args.dest_offset = args->dest_offset + nbytes;
		psi_args.len = ((struct vidtv_psi_desc_short_event *)args->desc)->text_len;
		psi_args.from = ((struct vidtv_psi_desc_short_event *)args->desc)->text;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		break;

	case REGISTRATION_DESCRIPTOR:
	default:
		psi_args.dest_offset = args->dest_offset + nbytes;
		psi_args.len = args->desc->length;
		psi_args.from = &args->desc->data;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);
		break;
	}

	return nbytes;
}

static u32
vidtv_psi_table_header_write_into(struct header_write_args *args)
{
	struct psi_write_args psi_args = {
		.dest_buf           = args->dest_buf,
		.from               = args->h,
		.len                = sizeof(struct vidtv_psi_table_header),
		.dest_offset        = args->dest_offset,
		.pid                = args->pid,
		.new_psi_section    = true,
		.continuity_counter = args->continuity_counter,
		.is_crc             = false,
		.dest_buf_sz        = args->dest_buf_sz,
		.crc                = args->crc,
	};

	return vidtv_psi_ts_psi_write_into(&psi_args);
}

void
vidtv_psi_pat_table_update_sec_len(struct vidtv_psi_table_pat *pat)
{
	u16 length = 0;
	u32 i;

	/* see ISO/IEC 13818-1 : 2000 p.43 */

	/* from immediately after 'section_length' until 'last_section_number'*/
	length += PAT_LEN_UNTIL_LAST_SECTION_NUMBER;

	/* do not count the pointer */
	for (i = 0; i < pat->num_pat; ++i)
		length += sizeof(struct vidtv_psi_table_pat_program) -
			  sizeof(struct vidtv_psi_table_pat_program *);

	length += CRC_SIZE_IN_BYTES;

	vidtv_psi_set_sec_len(&pat->header, length);
}

void vidtv_psi_pmt_table_update_sec_len(struct vidtv_psi_table_pmt *pmt)
{
	struct vidtv_psi_table_pmt_stream *s = pmt->stream;
	u16 desc_loop_len;
	u16 length = 0;

	/* see ISO/IEC 13818-1 : 2000 p.46 */

	/* from immediately after 'section_length' until 'program_info_length'*/
	length += PMT_LEN_UNTIL_PROGRAM_INFO_LENGTH;

	desc_loop_len = vidtv_psi_desc_comp_loop_len(pmt->descriptor);
	vidtv_psi_set_desc_loop_len(&pmt->bitfield2, desc_loop_len, 10);

	length += desc_loop_len;

	while (s) {
		/* skip both pointers at the end */
		length += sizeof(struct vidtv_psi_table_pmt_stream) -
			  sizeof(struct vidtv_psi_desc *) -
			  sizeof(struct vidtv_psi_table_pmt_stream *);

		desc_loop_len = vidtv_psi_desc_comp_loop_len(s->descriptor);
		vidtv_psi_set_desc_loop_len(&s->bitfield2, desc_loop_len, 10);

		length += desc_loop_len;

		s = s->next;
	}

	length += CRC_SIZE_IN_BYTES;

	vidtv_psi_set_sec_len(&pmt->header, length);
}

void vidtv_psi_sdt_table_update_sec_len(struct vidtv_psi_table_sdt *sdt)
{
	struct vidtv_psi_table_sdt_service *s = sdt->service;
	u16 desc_loop_len;
	u16 length = 0;

	/* see ETSI EN 300 468 V 1.10.1 p.24 */

	/*
	 * from immediately after 'section_length' until
	 * 'reserved_for_future_use'
	 */
	length += SDT_LEN_UNTIL_RESERVED_FOR_FUTURE_USE;

	while (s) {
		/* skip both pointers at the end */
		length += sizeof(struct vidtv_psi_table_sdt_service) -
			  sizeof(struct vidtv_psi_desc *) -
			  sizeof(struct vidtv_psi_table_sdt_service *);

		desc_loop_len = vidtv_psi_desc_comp_loop_len(s->descriptor);
		vidtv_psi_set_desc_loop_len(&s->bitfield, desc_loop_len, 12);

		length += desc_loop_len;

		s = s->next;
	}

	length += CRC_SIZE_IN_BYTES;
	vidtv_psi_set_sec_len(&sdt->header, length);
}

struct vidtv_psi_table_pat_program*
vidtv_psi_pat_program_init(struct vidtv_psi_table_pat_program *head,
			   u16 service_id,
			   u16 program_map_pid)
{
	struct vidtv_psi_table_pat_program *program;
	const u16 RESERVED = 0x07;

	program = kzalloc(sizeof(*program), GFP_KERNEL);
	if (!program)
		return NULL;

	program->service_id = cpu_to_be16(service_id);

	/* pid for the PMT section in the TS */
	program->bitfield = cpu_to_be16((RESERVED << 13) | program_map_pid);
	program->next = NULL;

	if (head) {
		while (head->next)
			head = head->next;

		head->next = program;
	}

	return program;
}

void
vidtv_psi_pat_program_destroy(struct vidtv_psi_table_pat_program *p)
{
	struct vidtv_psi_table_pat_program *tmp  = NULL;
	struct vidtv_psi_table_pat_program *curr = p;

	while (curr) {
		tmp  = curr;
		curr = curr->next;
		kfree(tmp);
	}
}

/* This function transfers ownership of p to the table */
void
vidtv_psi_pat_program_assign(struct vidtv_psi_table_pat *pat,
			     struct vidtv_psi_table_pat_program *p)
{
	struct vidtv_psi_table_pat_program *program;
	u16 program_count;

	do {
		program_count = 0;
		program = p;

		if (p == pat->program)
			return;

		while (program) {
			++program_count;
			program = program->next;
		}

		pat->num_pat = program_count;
		pat->program  = p;

		/* Recompute section length */
		vidtv_psi_pat_table_update_sec_len(pat);

		p = NULL;
	} while (vidtv_psi_get_sec_len(&pat->header) > MAX_SECTION_LEN);

	vidtv_psi_update_version_num(&pat->header);
}

struct vidtv_psi_table_pat *vidtv_psi_pat_table_init(u16 transport_stream_id)
{
	struct vidtv_psi_table_pat *pat;
	const u16 SYNTAX = 0x1;
	const u16 ZERO = 0x0;
	const u16 ONES = 0x03;

	pat = kzalloc(sizeof(*pat), GFP_KERNEL);
	if (!pat)
		return NULL;

	pat->header.table_id = 0x0;

	pat->header.bitfield = cpu_to_be16((SYNTAX << 15) | (ZERO << 14) | (ONES << 12));
	pat->header.id           = cpu_to_be16(transport_stream_id);
	pat->header.current_next = 0x1;

	pat->header.version = 0x1f;

	pat->header.one2         = 0x03;
	pat->header.section_id   = 0x0;
	pat->header.last_section = 0x0;

	vidtv_psi_pat_table_update_sec_len(pat);

	return pat;
}

u32 vidtv_psi_pat_write_into(struct vidtv_psi_pat_write_args *args)
{
	struct vidtv_psi_table_pat_program *p = args->pat->program;
	struct header_write_args h_args       = {
		.dest_buf           = args->buf,
		.dest_offset        = args->offset,
		.pid                = VIDTV_PAT_PID,
		.h                  = &args->pat->header,
		.continuity_counter = args->continuity_counter,
		.dest_buf_sz        = args->buf_sz,
	};
	struct psi_write_args psi_args        = {
		.dest_buf           = args->buf,
		.pid                = VIDTV_PAT_PID,
		.new_psi_section    = false,
		.continuity_counter = args->continuity_counter,
		.is_crc             = false,
		.dest_buf_sz        = args->buf_sz,
	};
	struct crc32_write_args c_args        = {
		.dest_buf           = args->buf,
		.pid                = VIDTV_PAT_PID,
		.dest_buf_sz        = args->buf_sz,
	};
	u32 crc = INITIAL_CRC;
	u32 nbytes = 0;

	vidtv_psi_pat_table_update_sec_len(args->pat);

	h_args.crc = &crc;

	nbytes += vidtv_psi_table_header_write_into(&h_args);

	/* note that the field 'u16 programs' is not really part of the PAT */

	psi_args.crc = &crc;

	while (p) {
		/* copy the PAT programs */
		psi_args.from = p;
		/* skip the pointer */
		psi_args.len = sizeof(*p) -
			       sizeof(struct vidtv_psi_table_pat_program *);
		psi_args.dest_offset = args->offset + nbytes;
		psi_args.continuity_counter = args->continuity_counter;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		p = p->next;
	}

	c_args.dest_offset        = args->offset + nbytes;
	c_args.continuity_counter = args->continuity_counter;
	c_args.crc                = cpu_to_be32(crc);

	/* Write the CRC32 at the end */
	nbytes += table_section_crc32_write_into(&c_args);

	return nbytes;
}

void
vidtv_psi_pat_table_destroy(struct vidtv_psi_table_pat *p)
{
	vidtv_psi_pat_program_destroy(p->program);
	kfree(p);
}

struct vidtv_psi_table_pmt_stream*
vidtv_psi_pmt_stream_init(struct vidtv_psi_table_pmt_stream *head,
			  enum vidtv_psi_stream_types stream_type,
			  u16 es_pid)
{
	struct vidtv_psi_table_pmt_stream *stream;
	const u16 RESERVED1 = 0x07;
	const u16 RESERVED2 = 0x0f;
	const u16 ZERO = 0x0;
	u16 desc_loop_len;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return NULL;

	stream->type = stream_type;

	stream->bitfield = cpu_to_be16((RESERVED1 << 13) | es_pid);

	desc_loop_len = vidtv_psi_desc_comp_loop_len(stream->descriptor);

	stream->bitfield2 = cpu_to_be16((RESERVED2 << 12) |
					(ZERO << 10)      |
					desc_loop_len);
	stream->next = NULL;

	if (head) {
		while (head->next)
			head = head->next;

		head->next = stream;
	}

	return stream;
}

void vidtv_psi_pmt_stream_destroy(struct vidtv_psi_table_pmt_stream *s)
{
	struct vidtv_psi_table_pmt_stream *tmp_stream  = NULL;
	struct vidtv_psi_table_pmt_stream *curr_stream = s;

	while (curr_stream) {
		tmp_stream  = curr_stream;
		curr_stream = curr_stream->next;
		vidtv_psi_desc_destroy(tmp_stream->descriptor);
		kfree(tmp_stream);
	}
}

void vidtv_psi_pmt_stream_assign(struct vidtv_psi_table_pmt *pmt,
				 struct vidtv_psi_table_pmt_stream *s)
{
	do {
		/* This function transfers ownership of s to the table */
		if (s == pmt->stream)
			return;

		pmt->stream = s;
		vidtv_psi_pmt_table_update_sec_len(pmt);

		s = NULL;
	} while (vidtv_psi_get_sec_len(&pmt->header) > MAX_SECTION_LEN);

	vidtv_psi_update_version_num(&pmt->header);
}

u16 vidtv_psi_pmt_get_pid(struct vidtv_psi_table_pmt *section,
			  struct vidtv_psi_table_pat *pat)
{
	struct vidtv_psi_table_pat_program *program = pat->program;

	/*
	 * service_id is the same as program_number in the
	 * corresponding program_map_section
	 * see ETSI EN 300 468 v1.15.1 p. 24
	 */
	while (program) {
		if (program->service_id == section->header.id)
			return vidtv_psi_get_pat_program_pid(program);

		program = program->next;
	}

	return TS_LAST_VALID_PID + 1; /* not found */
}

struct vidtv_psi_table_pmt *vidtv_psi_pmt_table_init(u16 program_number,
						     u16 pcr_pid)
{
	struct vidtv_psi_table_pmt *pmt;
	const u16 RESERVED1 = 0x07;
	const u16 RESERVED2 = 0x0f;
	const u16 SYNTAX = 0x1;
	const u16 ONES = 0x03;
	const u16 ZERO = 0x0;
	u16 desc_loop_len;

	pmt = kzalloc(sizeof(*pmt), GFP_KERNEL);
	if (!pmt)
		return NULL;

	if (!pcr_pid)
		pcr_pid = 0x1fff;

	pmt->header.table_id = 0x2;

	pmt->header.bitfield = cpu_to_be16((SYNTAX << 15) | (ZERO << 14) | (ONES << 12));

	pmt->header.id = cpu_to_be16(program_number);
	pmt->header.current_next = 0x1;

	pmt->header.version = 0x1f;

	pmt->header.one2 = ONES;
	pmt->header.section_id   = 0;
	pmt->header.last_section = 0;

	pmt->bitfield = cpu_to_be16((RESERVED1 << 13) | pcr_pid);

	desc_loop_len = vidtv_psi_desc_comp_loop_len(pmt->descriptor);

	pmt->bitfield2 = cpu_to_be16((RESERVED2 << 12) |
				     (ZERO << 10)      |
				     desc_loop_len);

	vidtv_psi_pmt_table_update_sec_len(pmt);

	return pmt;
}

u32 vidtv_psi_pmt_write_into(struct vidtv_psi_pmt_write_args *args)
{
	struct vidtv_psi_desc *table_descriptor   = args->pmt->descriptor;
	struct vidtv_psi_table_pmt_stream *stream = args->pmt->stream;
	struct vidtv_psi_desc *stream_descriptor;
	u32 crc = INITIAL_CRC;
	u32 nbytes = 0;
	struct header_write_args h_args = {
		.dest_buf           = args->buf,
		.dest_offset        = args->offset,
		.h                  = &args->pmt->header,
		.pid                = args->pid,
		.continuity_counter = args->continuity_counter,
		.dest_buf_sz        = args->buf_sz,
	};
	struct psi_write_args psi_args  = {
		.dest_buf = args->buf,
		.from     = &args->pmt->bitfield,
		.len      = sizeof_field(struct vidtv_psi_table_pmt, bitfield) +
			    sizeof_field(struct vidtv_psi_table_pmt, bitfield2),
		.pid                = args->pid,
		.new_psi_section    = false,
		.is_crc             = false,
		.dest_buf_sz        = args->buf_sz,
		.crc                = &crc,
	};
	struct desc_write_args d_args   = {
		.dest_buf           = args->buf,
		.desc               = table_descriptor,
		.pid                = args->pid,
		.dest_buf_sz        = args->buf_sz,
	};
	struct crc32_write_args c_args  = {
		.dest_buf           = args->buf,
		.pid                = args->pid,
		.dest_buf_sz        = args->buf_sz,
	};

	vidtv_psi_pmt_table_update_sec_len(args->pmt);

	h_args.crc                = &crc;

	nbytes += vidtv_psi_table_header_write_into(&h_args);

	/* write the two bitfields */
	psi_args.dest_offset        = args->offset + nbytes;
	psi_args.continuity_counter = args->continuity_counter;
	nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

	while (table_descriptor) {
		/* write the descriptors, if any */
		d_args.dest_offset        = args->offset + nbytes;
		d_args.continuity_counter = args->continuity_counter;
		d_args.crc                = &crc;

		nbytes += vidtv_psi_desc_write_into(&d_args);

		table_descriptor = table_descriptor->next;
	}

	psi_args.len += sizeof_field(struct vidtv_psi_table_pmt_stream, type);
	while (stream) {
		/* write the streams, if any */
		psi_args.from = stream;
		psi_args.dest_offset = args->offset + nbytes;
		psi_args.continuity_counter = args->continuity_counter;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		stream_descriptor = stream->descriptor;

		while (stream_descriptor) {
			/* write the stream descriptors, if any */
			d_args.dest_offset        = args->offset + nbytes;
			d_args.desc               = stream_descriptor;
			d_args.continuity_counter = args->continuity_counter;
			d_args.crc                = &crc;

			nbytes += vidtv_psi_desc_write_into(&d_args);

			stream_descriptor = stream_descriptor->next;
		}

		stream = stream->next;
	}

	c_args.dest_offset        = args->offset + nbytes;
	c_args.crc                = cpu_to_be32(crc);
	c_args.continuity_counter = args->continuity_counter;

	/* Write the CRC32 at the end */
	nbytes += table_section_crc32_write_into(&c_args);

	return nbytes;
}

void vidtv_psi_pmt_table_destroy(struct vidtv_psi_table_pmt *pmt)
{
	vidtv_psi_desc_destroy(pmt->descriptor);
	vidtv_psi_pmt_stream_destroy(pmt->stream);
	kfree(pmt);
}

struct vidtv_psi_table_sdt *vidtv_psi_sdt_table_init(u16 network_id,
						     u16 transport_stream_id)
{
	struct vidtv_psi_table_sdt *sdt;
	const u16 RESERVED = 0xff;
	const u16 SYNTAX = 0x1;
	const u16 ONES = 0x03;
	const u16 ONE = 0x1;

	sdt  = kzalloc(sizeof(*sdt), GFP_KERNEL);
	if (!sdt)
		return NULL;

	sdt->header.table_id = 0x42;
	sdt->header.bitfield = cpu_to_be16((SYNTAX << 15) | (ONE << 14) | (ONES << 12));

	/*
	 * This is a 16-bit field which serves as a label for identification
	 * of the TS, about which the SDT informs, from any other multiplex
	 * within the delivery system.
	 */
	sdt->header.id = cpu_to_be16(transport_stream_id);
	sdt->header.current_next = ONE;

	sdt->header.version = 0x1f;

	sdt->header.one2  = ONES;
	sdt->header.section_id   = 0;
	sdt->header.last_section = 0;

	/*
	 * FIXME: The network_id range from 0xFF01 to 0xFFFF is used to
	 * indicate temporary private use. For now, let's use the first
	 * value.
	 * This can be changed to something more useful, when support for
	 * NIT gets added
	 */
	sdt->network_id = cpu_to_be16(network_id);
	sdt->reserved = RESERVED;

	vidtv_psi_sdt_table_update_sec_len(sdt);

	return sdt;
}

u32 vidtv_psi_sdt_write_into(struct vidtv_psi_sdt_write_args *args)
{
	struct header_write_args h_args = {
		.dest_buf           = args->buf,
		.dest_offset        = args->offset,
		.h                  = &args->sdt->header,
		.pid                = VIDTV_SDT_PID,
		.dest_buf_sz        = args->buf_sz,
	};
	struct psi_write_args psi_args  = {
		.dest_buf = args->buf,
		.len = sizeof_field(struct vidtv_psi_table_sdt, network_id) +
		       sizeof_field(struct vidtv_psi_table_sdt, reserved),
		.pid                = VIDTV_SDT_PID,
		.new_psi_section    = false,
		.is_crc             = false,
		.dest_buf_sz        = args->buf_sz,
	};
	struct desc_write_args d_args   = {
		.dest_buf           = args->buf,
		.pid                = VIDTV_SDT_PID,
		.dest_buf_sz        = args->buf_sz,
	};
	struct crc32_write_args c_args  = {
		.dest_buf           = args->buf,
		.pid                = VIDTV_SDT_PID,
		.dest_buf_sz        = args->buf_sz,
	};
	struct vidtv_psi_table_sdt_service *service = args->sdt->service;
	struct vidtv_psi_desc *service_desc;
	u32 nbytes  = 0;
	u32 crc = INITIAL_CRC;

	/* see ETSI EN 300 468 v1.15.1 p. 11 */

	vidtv_psi_sdt_table_update_sec_len(args->sdt);

	h_args.continuity_counter = args->continuity_counter;
	h_args.crc                = &crc;

	nbytes += vidtv_psi_table_header_write_into(&h_args);

	psi_args.from               = &args->sdt->network_id;
	psi_args.dest_offset        = args->offset + nbytes;
	psi_args.continuity_counter = args->continuity_counter;
	psi_args.crc                = &crc;

	/* copy u16 network_id + u8 reserved)*/
	nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

	/* skip both pointers at the end */
	psi_args.len = sizeof(struct vidtv_psi_table_sdt_service) -
		       sizeof(struct vidtv_psi_desc *) -
		       sizeof(struct vidtv_psi_table_sdt_service *);

	while (service) {
		/* copy the services, if any */
		psi_args.from = service;
		psi_args.dest_offset = args->offset + nbytes;
		psi_args.continuity_counter = args->continuity_counter;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		service_desc = service->descriptor;

		while (service_desc) {
			/* copy the service descriptors, if any */
			d_args.dest_offset        = args->offset + nbytes;
			d_args.desc               = service_desc;
			d_args.continuity_counter = args->continuity_counter;
			d_args.crc                = &crc;

			nbytes += vidtv_psi_desc_write_into(&d_args);

			service_desc = service_desc->next;
		}

		service = service->next;
	}

	c_args.dest_offset        = args->offset + nbytes;
	c_args.crc                = cpu_to_be32(crc);
	c_args.continuity_counter = args->continuity_counter;

	/* Write the CRC at the end */
	nbytes += table_section_crc32_write_into(&c_args);

	return nbytes;
}

void vidtv_psi_sdt_table_destroy(struct vidtv_psi_table_sdt *sdt)
{
	vidtv_psi_sdt_service_destroy(sdt->service);
	kfree(sdt);
}

struct vidtv_psi_table_sdt_service
*vidtv_psi_sdt_service_init(struct vidtv_psi_table_sdt_service *head,
			    u16 service_id,
			    bool eit_schedule,
			    bool eit_present_following)
{
	struct vidtv_psi_table_sdt_service *service;

	service = kzalloc(sizeof(*service), GFP_KERNEL);
	if (!service)
		return NULL;

	/*
	 * ETSI 300 468: this is a 16bit field which serves as a label to
	 * identify this service from any other service within the TS.
	 * The service id is the same as the program number in the
	 * corresponding program_map_section
	 */
	service->service_id            = cpu_to_be16(service_id);
	service->EIT_schedule          = eit_schedule;
	service->EIT_present_following = eit_present_following;
	service->reserved              = 0x3f;

	service->bitfield = cpu_to_be16(RUNNING << 13);

	if (head) {
		while (head->next)
			head = head->next;

		head->next = service;
	}

	return service;
}

void
vidtv_psi_sdt_service_destroy(struct vidtv_psi_table_sdt_service *service)
{
	struct vidtv_psi_table_sdt_service *curr = service;
	struct vidtv_psi_table_sdt_service *tmp  = NULL;

	while (curr) {
		tmp  = curr;
		curr = curr->next;
		vidtv_psi_desc_destroy(tmp->descriptor);
		kfree(tmp);
	}
}

void
vidtv_psi_sdt_service_assign(struct vidtv_psi_table_sdt *sdt,
			     struct vidtv_psi_table_sdt_service *service)
{
	do {
		if (service == sdt->service)
			return;

		sdt->service = service;

		/* recompute section length */
		vidtv_psi_sdt_table_update_sec_len(sdt);

		service = NULL;
	} while (vidtv_psi_get_sec_len(&sdt->header) > MAX_SECTION_LEN);

	vidtv_psi_update_version_num(&sdt->header);
}

/*
 * PMTs contain information about programs. For each program,
 * there is one PMT section. This function will create a section
 * for each program found in the PAT
 */
struct vidtv_psi_table_pmt**
vidtv_psi_pmt_create_sec_for_each_pat_entry(struct vidtv_psi_table_pat *pat,
					    u16 pcr_pid)

{
	struct vidtv_psi_table_pat_program *program;
	struct vidtv_psi_table_pmt **pmt_secs;
	u32 i = 0, num_pmt = 0;

	/*
	 * The number of PMT entries is the number of PAT entries
	 * that contain service_id. That exclude special tables, like NIT
	 */
	program = pat->program;
	while (program) {
		if (program->service_id)
			num_pmt++;
		program = program->next;
	}

	pmt_secs = kcalloc(num_pmt,
			   sizeof(struct vidtv_psi_table_pmt *),
			   GFP_KERNEL);
	if (!pmt_secs)
		return NULL;

	for (program = pat->program; program; program = program->next) {
		if (!program->service_id)
			continue;
		pmt_secs[i] = vidtv_psi_pmt_table_init(be16_to_cpu(program->service_id),
						       pcr_pid);

		if (!pmt_secs[i]) {
			while (i > 0) {
				i--;
				vidtv_psi_pmt_table_destroy(pmt_secs[i]);
			}
			return NULL;
		}
		i++;
	}
	pat->num_pmt = num_pmt;

	return pmt_secs;
}

/* find the PMT section associated with 'program_num' */
struct vidtv_psi_table_pmt
*vidtv_psi_find_pmt_sec(struct vidtv_psi_table_pmt **pmt_sections,
			u16 nsections,
			u16 program_num)
{
	struct vidtv_psi_table_pmt *sec = NULL;
	u32 i;

	for (i = 0; i < nsections; ++i) {
		sec = pmt_sections[i];
		if (be16_to_cpu(sec->header.id) == program_num)
			return sec;
	}

	return NULL; /* not found */
}

static void vidtv_psi_nit_table_update_sec_len(struct vidtv_psi_table_nit *nit)
{
	u16 length = 0;
	struct vidtv_psi_table_transport *t = nit->transport;
	u16 desc_loop_len;
	u16 transport_loop_len = 0;

	/*
	 * from immediately after 'section_length' until
	 * 'network_descriptor_length'
	 */
	length += NIT_LEN_UNTIL_NETWORK_DESCRIPTOR_LEN;

	desc_loop_len = vidtv_psi_desc_comp_loop_len(nit->descriptor);
	vidtv_psi_set_desc_loop_len(&nit->bitfield, desc_loop_len, 12);

	length += desc_loop_len;

	length += sizeof_field(struct vidtv_psi_table_nit, bitfield2);

	while (t) {
		/* skip both pointers at the end */
		transport_loop_len += sizeof(struct vidtv_psi_table_transport) -
				      sizeof(struct vidtv_psi_desc *) -
				      sizeof(struct vidtv_psi_table_transport *);

		length += transport_loop_len;

		desc_loop_len = vidtv_psi_desc_comp_loop_len(t->descriptor);
		vidtv_psi_set_desc_loop_len(&t->bitfield, desc_loop_len, 12);

		length += desc_loop_len;

		t = t->next;
	}

	// Actually sets the transport stream loop len, maybe rename this function later
	vidtv_psi_set_desc_loop_len(&nit->bitfield2, transport_loop_len, 12);
	length += CRC_SIZE_IN_BYTES;

	vidtv_psi_set_sec_len(&nit->header, length);
}

struct vidtv_psi_table_nit
*vidtv_psi_nit_table_init(u16 network_id,
			  u16 transport_stream_id,
			  char *network_name,
			  struct vidtv_psi_desc_service_list_entry *service_list)
{
	struct vidtv_psi_table_transport *transport;
	struct vidtv_psi_table_nit *nit;
	const u16 SYNTAX = 0x1;
	const u16 ONES = 0x03;
	const u16 ONE = 0x1;

	nit = kzalloc(sizeof(*nit), GFP_KERNEL);
	if (!nit)
		return NULL;

	transport = kzalloc(sizeof(*transport), GFP_KERNEL);
	if (!transport)
		goto free_nit;

	nit->header.table_id = 0x40; // ACTUAL_NETWORK

	nit->header.bitfield = cpu_to_be16((SYNTAX << 15) | (ONE << 14) | (ONES << 12));

	nit->header.id = cpu_to_be16(network_id);
	nit->header.current_next = ONE;

	nit->header.version = 0x1f;

	nit->header.one2  = ONES;
	nit->header.section_id   = 0;
	nit->header.last_section = 0;

	nit->bitfield = cpu_to_be16(0xf);
	nit->bitfield2 = cpu_to_be16(0xf);

	nit->descriptor = (struct vidtv_psi_desc *)
			  vidtv_psi_network_name_desc_init(NULL, network_name);
	if (!nit->descriptor)
		goto free_transport;

	transport->transport_id = cpu_to_be16(transport_stream_id);
	transport->network_id = cpu_to_be16(network_id);
	transport->bitfield = cpu_to_be16(0xf);
	transport->descriptor = (struct vidtv_psi_desc *)
				vidtv_psi_service_list_desc_init(NULL, service_list);
	if (!transport->descriptor)
		goto free_nit_desc;

	nit->transport = transport;

	vidtv_psi_nit_table_update_sec_len(nit);

	return nit;

free_nit_desc:
	vidtv_psi_desc_destroy((struct vidtv_psi_desc *)nit->descriptor);

free_transport:
	kfree(transport);
free_nit:
	kfree(nit);
	return NULL;
}

u32 vidtv_psi_nit_write_into(struct vidtv_psi_nit_write_args *args)
{
	struct header_write_args h_args = {
		.dest_buf           = args->buf,
		.dest_offset        = args->offset,
		.h                  = &args->nit->header,
		.pid                = VIDTV_NIT_PID,
		.dest_buf_sz        = args->buf_sz,
	};
	struct psi_write_args psi_args  = {
		.dest_buf           = args->buf,
		.from               = &args->nit->bitfield,
		.len                = sizeof_field(struct vidtv_psi_table_nit, bitfield),
		.pid                = VIDTV_NIT_PID,
		.new_psi_section    = false,
		.is_crc             = false,
		.dest_buf_sz        = args->buf_sz,
	};
	struct desc_write_args d_args   = {
		.dest_buf           = args->buf,
		.pid                = VIDTV_NIT_PID,
		.dest_buf_sz        = args->buf_sz,
	};
	struct crc32_write_args c_args  = {
		.dest_buf           = args->buf,
		.pid                = VIDTV_NIT_PID,
		.dest_buf_sz        = args->buf_sz,
	};
	struct vidtv_psi_desc *table_descriptor     = args->nit->descriptor;
	struct vidtv_psi_table_transport *transport = args->nit->transport;
	struct vidtv_psi_desc *transport_descriptor;
	u32 crc = INITIAL_CRC;
	u32 nbytes = 0;

	vidtv_psi_nit_table_update_sec_len(args->nit);

	h_args.continuity_counter = args->continuity_counter;
	h_args.crc                = &crc;

	nbytes += vidtv_psi_table_header_write_into(&h_args);

	/* write the bitfield */

	psi_args.dest_offset        = args->offset + nbytes;
	psi_args.continuity_counter = args->continuity_counter;
	psi_args.crc                = &crc;

	nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

	while (table_descriptor) {
		/* write the descriptors, if any */
		d_args.dest_offset        = args->offset + nbytes;
		d_args.desc               = table_descriptor;
		d_args.continuity_counter = args->continuity_counter;
		d_args.crc                = &crc;

		nbytes += vidtv_psi_desc_write_into(&d_args);

		table_descriptor = table_descriptor->next;
	}

	/* write the second bitfield */
	psi_args.from = &args->nit->bitfield2;
	psi_args.len = sizeof_field(struct vidtv_psi_table_nit, bitfield2);
	psi_args.dest_offset = args->offset + nbytes;

	nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

	psi_args.len  = sizeof_field(struct vidtv_psi_table_transport, transport_id) +
			sizeof_field(struct vidtv_psi_table_transport, network_id)   +
			sizeof_field(struct vidtv_psi_table_transport, bitfield);
	while (transport) {
		/* write the transport sections, if any */
		psi_args.from = transport;
		psi_args.dest_offset = args->offset + nbytes;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		transport_descriptor = transport->descriptor;

		while (transport_descriptor) {
			/* write the transport descriptors, if any */
			d_args.dest_offset        = args->offset + nbytes;
			d_args.desc               = transport_descriptor;
			d_args.continuity_counter = args->continuity_counter;
			d_args.crc                = &crc;

			nbytes += vidtv_psi_desc_write_into(&d_args);

			transport_descriptor = transport_descriptor->next;
		}

		transport = transport->next;
	}

	c_args.dest_offset        = args->offset + nbytes;
	c_args.crc                = cpu_to_be32(crc);
	c_args.continuity_counter = args->continuity_counter;

	/* Write the CRC32 at the end */
	nbytes += table_section_crc32_write_into(&c_args);

	return nbytes;
}

static void vidtv_psi_transport_destroy(struct vidtv_psi_table_transport *t)
{
	struct vidtv_psi_table_transport *tmp_t  = NULL;
	struct vidtv_psi_table_transport *curr_t = t;

	while (curr_t) {
		tmp_t  = curr_t;
		curr_t = curr_t->next;
		vidtv_psi_desc_destroy(tmp_t->descriptor);
		kfree(tmp_t);
	}
}

void vidtv_psi_nit_table_destroy(struct vidtv_psi_table_nit *nit)
{
	vidtv_psi_desc_destroy(nit->descriptor);
	vidtv_psi_transport_destroy(nit->transport);
	kfree(nit);
}

void vidtv_psi_eit_table_update_sec_len(struct vidtv_psi_table_eit *eit)
{
	struct vidtv_psi_table_eit_event *e = eit->event;
	u16 desc_loop_len;
	u16 length = 0;

	/*
	 * from immediately after 'section_length' until
	 * 'last_table_id'
	 */
	length += EIT_LEN_UNTIL_LAST_TABLE_ID;

	while (e) {
		/* skip both pointers at the end */
		length += sizeof(struct vidtv_psi_table_eit_event) -
			  sizeof(struct vidtv_psi_desc *) -
			  sizeof(struct vidtv_psi_table_eit_event *);

		desc_loop_len = vidtv_psi_desc_comp_loop_len(e->descriptor);
		vidtv_psi_set_desc_loop_len(&e->bitfield, desc_loop_len, 12);

		length += desc_loop_len;

		e = e->next;
	}

	length += CRC_SIZE_IN_BYTES;

	vidtv_psi_set_sec_len(&eit->header, length);
}

void vidtv_psi_eit_event_assign(struct vidtv_psi_table_eit *eit,
				struct vidtv_psi_table_eit_event *e)
{
	do {
		if (e == eit->event)
			return;

		eit->event = e;
		vidtv_psi_eit_table_update_sec_len(eit);

		e = NULL;
	} while (vidtv_psi_get_sec_len(&eit->header) > EIT_MAX_SECTION_LEN);

	vidtv_psi_update_version_num(&eit->header);
}

struct vidtv_psi_table_eit
*vidtv_psi_eit_table_init(u16 network_id,
			  u16 transport_stream_id,
			  __be16 service_id)
{
	struct vidtv_psi_table_eit *eit;
	const u16 SYNTAX = 0x1;
	const u16 ONE = 0x1;
	const u16 ONES = 0x03;

	eit = kzalloc(sizeof(*eit), GFP_KERNEL);
	if (!eit)
		return NULL;

	eit->header.table_id = 0x4e; //actual_transport_stream: present/following

	eit->header.bitfield = cpu_to_be16((SYNTAX << 15) | (ONE << 14) | (ONES << 12));

	eit->header.id = service_id;
	eit->header.current_next = ONE;

	eit->header.version = 0x1f;

	eit->header.one2  = ONES;
	eit->header.section_id   = 0;
	eit->header.last_section = 0;

	eit->transport_id = cpu_to_be16(transport_stream_id);
	eit->network_id = cpu_to_be16(network_id);

	eit->last_segment = eit->header.last_section; /* not implemented */
	eit->last_table_id = eit->header.table_id; /* not implemented */

	vidtv_psi_eit_table_update_sec_len(eit);

	return eit;
}

u32 vidtv_psi_eit_write_into(struct vidtv_psi_eit_write_args *args)
{
	struct header_write_args h_args = {
		.dest_buf        = args->buf,
		.dest_offset     = args->offset,
		.h               = &args->eit->header,
		.pid             = VIDTV_EIT_PID,
		.dest_buf_sz     = args->buf_sz,
	};
	struct psi_write_args psi_args  = {
		.dest_buf        = args->buf,
		.len             = sizeof_field(struct vidtv_psi_table_eit, transport_id) +
				   sizeof_field(struct vidtv_psi_table_eit, network_id)   +
				   sizeof_field(struct vidtv_psi_table_eit, last_segment) +
				   sizeof_field(struct vidtv_psi_table_eit, last_table_id),
		.pid             = VIDTV_EIT_PID,
		.new_psi_section = false,
		.is_crc          = false,
		.dest_buf_sz     = args->buf_sz,
	};
	struct desc_write_args d_args   = {
		.dest_buf           = args->buf,
		.pid                = VIDTV_EIT_PID,
		.dest_buf_sz        = args->buf_sz,
	};
	struct crc32_write_args c_args  = {
		.dest_buf           = args->buf,
		.pid                = VIDTV_EIT_PID,
		.dest_buf_sz        = args->buf_sz,
	};
	struct vidtv_psi_table_eit_event *event = args->eit->event;
	struct vidtv_psi_desc *event_descriptor;
	u32 crc = INITIAL_CRC;
	u32 nbytes  = 0;

	vidtv_psi_eit_table_update_sec_len(args->eit);

	h_args.continuity_counter = args->continuity_counter;
	h_args.crc                = &crc;

	nbytes += vidtv_psi_table_header_write_into(&h_args);

	psi_args.from               = &args->eit->transport_id;
	psi_args.dest_offset        = args->offset + nbytes;
	psi_args.continuity_counter = args->continuity_counter;
	psi_args.crc                = &crc;

	nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

	/* skip both pointers at the end */
	psi_args.len = sizeof(struct vidtv_psi_table_eit_event) -
		       sizeof(struct vidtv_psi_desc *) -
		       sizeof(struct vidtv_psi_table_eit_event *);
	while (event) {
		/* copy the events, if any */
		psi_args.from = event;
		psi_args.dest_offset = args->offset + nbytes;

		nbytes += vidtv_psi_ts_psi_write_into(&psi_args);

		event_descriptor = event->descriptor;

		while (event_descriptor) {
			/* copy the event descriptors, if any */
			d_args.dest_offset        = args->offset + nbytes;
			d_args.desc               = event_descriptor;
			d_args.continuity_counter = args->continuity_counter;
			d_args.crc                = &crc;

			nbytes += vidtv_psi_desc_write_into(&d_args);

			event_descriptor = event_descriptor->next;
		}

		event = event->next;
	}

	c_args.dest_offset        = args->offset + nbytes;
	c_args.crc                = cpu_to_be32(crc);
	c_args.continuity_counter = args->continuity_counter;

	/* Write the CRC at the end */
	nbytes += table_section_crc32_write_into(&c_args);

	return nbytes;
}

struct vidtv_psi_table_eit_event
*vidtv_psi_eit_event_init(struct vidtv_psi_table_eit_event *head, u16 event_id)
{
	static const u8 DURATION[] = {0x23, 0x59, 0x59}; /* BCD encoded */
	struct vidtv_psi_table_eit_event *e;
	struct timespec64 ts;
	struct tm time;
	int mjd, l;
	__be16 mjd_be;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return NULL;

	e->event_id = cpu_to_be16(event_id);

	ts = ktime_to_timespec64(ktime_get_real());
	time64_to_tm(ts.tv_sec, 0, &time);

	/* Convert date to Modified Julian Date - per EN 300 468 Annex C */
	if (time.tm_mon < 2)
		l = 1;
	else
		l = 0;

	mjd = 14956 + time.tm_mday;
	mjd += (time.tm_year - l) * 36525 / 100;
	mjd += (time.tm_mon + 2 + l * 12) * 306001 / 10000;
	mjd_be = cpu_to_be16(mjd);

	/*
	 * Store MJD and hour/min/sec to the event.
	 *
	 * Let's make the event to start on a full hour
	 */
	memcpy(e->start_time, &mjd_be, sizeof(mjd_be));
	e->start_time[2] = bin2bcd(time.tm_hour);
	e->start_time[3] = 0;
	e->start_time[4] = 0;

	/*
	 * TODO: for now, the event will last for a day. Should be
	 * enough for testing purposes, but if one runs the driver
	 * for more than that, the current event will become invalid.
	 * So, we need a better code here in order to change the start
	 * time once the event expires.
	 */
	memcpy(e->duration, DURATION, sizeof(e->duration));

	e->bitfield = cpu_to_be16(RUNNING << 13);

	if (head) {
		while (head->next)
			head = head->next;

		head->next = e;
	}

	return e;
}

void vidtv_psi_eit_event_destroy(struct vidtv_psi_table_eit_event *e)
{
	struct vidtv_psi_table_eit_event *tmp_e  = NULL;
	struct vidtv_psi_table_eit_event *curr_e = e;

	while (curr_e) {
		tmp_e  = curr_e;
		curr_e = curr_e->next;
		vidtv_psi_desc_destroy(tmp_e->descriptor);
		kfree(tmp_e);
	}
}

void vidtv_psi_eit_table_destroy(struct vidtv_psi_table_eit *eit)
{
	vidtv_psi_eit_event_destroy(eit->event);
	kfree(eit);
}
