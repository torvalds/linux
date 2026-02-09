/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __PSP_SEV_TIO_H__
#define __PSP_SEV_TIO_H__

#include <linux/pci-tsm.h>
#include <linux/pci-ide.h>
#include <linux/tsm.h>
#include <uapi/linux/psp-sev.h>

struct sla_addr_t {
	union {
		u64 sla;
		struct {
			u64 page_type	:1,
			    page_size	:1,
			    reserved1	:10,
			    pfn		:40,
			    reserved2	:12;
		};
	};
} __packed;

#define SEV_TIO_MAX_COMMAND_LENGTH	128

/* SPDM control structure for DOE */
struct tsm_spdm {
	unsigned long req_len;
	void *req;
	unsigned long rsp_len;
	void *rsp;
};

/* Describes TIO device */
struct tsm_dsm_tio {
	u8 cert_slot;
	struct sla_addr_t dev_ctx;
	struct sla_addr_t req;
	struct sla_addr_t resp;
	struct sla_addr_t scratch;
	struct sla_addr_t output;
	size_t output_len;
	size_t scratch_len;
	struct tsm_spdm spdm;
	struct sla_buffer_hdr *reqbuf; /* vmap'ed @req for DOE */
	struct sla_buffer_hdr *respbuf; /* vmap'ed @resp for DOE */

	int cmd;
	int psp_ret;
	u8 cmd_data[SEV_TIO_MAX_COMMAND_LENGTH];
	void *data_pg; /* Data page for DEV_STATUS/TDI_STATUS/TDI_INFO/ASID_FENCE */

#define TIO_IDE_MAX_TC	8
	struct pci_ide *ide[TIO_IDE_MAX_TC];
};

/* Describes TSM structure for PF0 pointed by pci_dev->tsm */
struct tio_dsm {
	struct pci_tsm_pf0 tsm;
	struct tsm_dsm_tio data;
	struct sev_device *sev;
};

/* Data object IDs */
#define SPDM_DOBJ_ID_NONE		0
#define SPDM_DOBJ_ID_REQ		1
#define SPDM_DOBJ_ID_RESP		2

struct spdm_dobj_hdr {
	u32 id;     /* Data object type identifier */
	u32 length; /* Length of the data object, INCLUDING THIS HEADER */
	struct { /* Version of the data object structure */
		u8 minor;
		u8 major;
	} version;
} __packed;

/**
 * struct sev_tio_status - TIO_STATUS command's info_paddr buffer
 *
 * @length: Length of this structure in bytes
 * @tio_en: Indicates that SNP_INIT_EX initialized the RMP for SEV-TIO
 * @tio_init_done: Indicates TIO_INIT has been invoked
 * @spdm_req_size_min: Minimum SPDM request buffer size in bytes
 * @spdm_req_size_max: Maximum SPDM request buffer size in bytes
 * @spdm_scratch_size_min: Minimum SPDM scratch buffer size in bytes
 * @spdm_scratch_size_max: Maximum SPDM scratch buffer size in bytes
 * @spdm_out_size_min: Minimum SPDM output buffer size in bytes
 * @spdm_out_size_max: Maximum for the SPDM output buffer size in bytes
 * @spdm_rsp_size_min: Minimum SPDM response buffer size in bytes
 * @spdm_rsp_size_max: Maximum SPDM response buffer size in bytes
 * @devctx_size: Size of a device context buffer in bytes
 * @tdictx_size: Size of a TDI context buffer in bytes
 * @tio_crypto_alg: TIO crypto algorithms supported
 */
struct sev_tio_status {
	u32 length;
	u32 tio_en	  :1,
	    tio_init_done :1,
	    reserved	  :30;
	u32 spdm_req_size_min;
	u32 spdm_req_size_max;
	u32 spdm_scratch_size_min;
	u32 spdm_scratch_size_max;
	u32 spdm_out_size_min;
	u32 spdm_out_size_max;
	u32 spdm_rsp_size_min;
	u32 spdm_rsp_size_max;
	u32 devctx_size;
	u32 tdictx_size;
	u32 tio_crypto_alg;
	u8 reserved2[12];
} __packed;

int sev_tio_init_locked(void *tio_status_page);
int sev_tio_continue(struct tsm_dsm_tio *dev_data);

int sev_tio_dev_create(struct tsm_dsm_tio *dev_data, u16 device_id, u16 root_port_id,
		       u8 segment_id);
int sev_tio_dev_connect(struct tsm_dsm_tio *dev_data, u8 tc_mask, u8 ids[8], u8 cert_slot);
int sev_tio_dev_disconnect(struct tsm_dsm_tio *dev_data, bool force);
int sev_tio_dev_reclaim(struct tsm_dsm_tio *dev_data);

#endif	/* __PSP_SEV_TIO_H__ */
