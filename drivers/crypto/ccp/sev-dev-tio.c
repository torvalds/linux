// SPDX-License-Identifier: GPL-2.0-only

// Interface to PSP for CCP/SEV-TIO/SNP-VM

#include <linux/pci.h>
#include <linux/tsm.h>
#include <linux/psp.h>
#include <linux/vmalloc.h>
#include <linux/bitfield.h>
#include <linux/pci-doe.h>
#include <asm/sev-common.h>
#include <asm/sev.h>
#include <asm/page.h>
#include "sev-dev.h"
#include "sev-dev-tio.h"

#define to_tio_status(dev_data)	\
		(container_of((dev_data), struct tio_dsm, data)->sev->tio_status)

#define SLA_PAGE_TYPE_DATA	0
#define SLA_PAGE_TYPE_SCATTER	1
#define SLA_PAGE_SIZE_4K	0
#define SLA_PAGE_SIZE_2M	1
#define SLA_SZ(s)		((s).page_size == SLA_PAGE_SIZE_2M ? SZ_2M : SZ_4K)
#define SLA_SCATTER_LEN(s)	(SLA_SZ(s) / sizeof(struct sla_addr_t))
#define SLA_EOL			((struct sla_addr_t) { .pfn = ((1UL << 40) - 1) })
#define SLA_NULL		((struct sla_addr_t) { 0 })
#define IS_SLA_NULL(s)		((s).sla == SLA_NULL.sla)
#define IS_SLA_EOL(s)		((s).sla == SLA_EOL.sla)

static phys_addr_t sla_to_pa(struct sla_addr_t sla)
{
	u64 pfn = sla.pfn;
	u64 pa = pfn << PAGE_SHIFT;

	return pa;
}

static void *sla_to_va(struct sla_addr_t sla)
{
	void *va = __va(__sme_clr(sla_to_pa(sla)));

	return va;
}

#define sla_to_pfn(sla)		(__pa(sla_to_va(sla)) >> PAGE_SHIFT)
#define sla_to_page(sla)	virt_to_page(sla_to_va(sla))

static struct sla_addr_t make_sla(struct page *pg, bool stp)
{
	u64 pa = __sme_set(page_to_phys(pg));
	struct sla_addr_t ret = {
		.pfn = pa >> PAGE_SHIFT,
		.page_size = SLA_PAGE_SIZE_4K, /* Do not do SLA_PAGE_SIZE_2M ATM */
		.page_type = stp ? SLA_PAGE_TYPE_SCATTER : SLA_PAGE_TYPE_DATA
	};

	return ret;
}

/* the BUFFER Structure */
#define SLA_BUFFER_FLAG_ENCRYPTION	BIT(0)

/*
 * struct sla_buffer_hdr - Scatter list address buffer header
 *
 * @capacity_sz: Total capacity of the buffer in bytes
 * @payload_sz: Size of buffer payload in bytes, must be multiple of 32B
 * @flags: Buffer flags (SLA_BUFFER_FLAG_ENCRYPTION: buffer is encrypted)
 * @iv: Initialization vector used for encryption
 * @authtag: Authentication tag for encrypted buffer
 */
struct sla_buffer_hdr {
	u32 capacity_sz;
	u32 payload_sz; /* The size of BUFFER_PAYLOAD in bytes. Must be multiple of 32B */
	u32 flags;
	u8 reserved1[4];
	u8 iv[16];	/* IV used for the encryption of this buffer */
	u8 authtag[16]; /* Authentication tag for this buffer */
	u8 reserved2[16];
} __packed;

enum spdm_data_type_t {
	DOBJ_DATA_TYPE_SPDM = 0x1,
	DOBJ_DATA_TYPE_SECURE_SPDM = 0x2,
};

struct spdm_dobj_hdr_req {
	struct spdm_dobj_hdr hdr; /* hdr.id == SPDM_DOBJ_ID_REQ */
	u8 data_type; /* spdm_data_type_t */
	u8 reserved2[5];
} __packed;

struct spdm_dobj_hdr_resp {
	struct spdm_dobj_hdr hdr; /* hdr.id == SPDM_DOBJ_ID_RESP */
	u8 data_type; /* spdm_data_type_t */
	u8 reserved2[5];
} __packed;

/* Defined in sev-dev-tio.h so sev-dev-tsm.c can read types of blobs */
struct spdm_dobj_hdr_cert;
struct spdm_dobj_hdr_meas;
struct spdm_dobj_hdr_report;

/* Used in all SPDM-aware TIO commands */
struct spdm_ctrl {
	struct sla_addr_t req;
	struct sla_addr_t resp;
	struct sla_addr_t scratch;
	struct sla_addr_t output;
} __packed;

static size_t sla_dobj_id_to_size(u8 id)
{
	size_t n;

	BUILD_BUG_ON(sizeof(struct spdm_dobj_hdr_resp) != 0x10);
	switch (id) {
	case SPDM_DOBJ_ID_REQ:
		n = sizeof(struct spdm_dobj_hdr_req);
		break;
	case SPDM_DOBJ_ID_RESP:
		n = sizeof(struct spdm_dobj_hdr_resp);
		break;
	default:
		WARN_ON(1);
		n = 0;
		break;
	}

	return n;
}

#define SPDM_DOBJ_HDR_SIZE(hdr)		sla_dobj_id_to_size((hdr)->id)
#define SPDM_DOBJ_DATA(hdr)		((u8 *)(hdr) + SPDM_DOBJ_HDR_SIZE(hdr))
#define SPDM_DOBJ_LEN(hdr)		((hdr)->length - SPDM_DOBJ_HDR_SIZE(hdr))

#define sla_to_dobj_resp_hdr(buf)	((struct spdm_dobj_hdr_resp *) \
					sla_to_dobj_hdr_check((buf), SPDM_DOBJ_ID_RESP))
#define sla_to_dobj_req_hdr(buf)	((struct spdm_dobj_hdr_req *) \
					sla_to_dobj_hdr_check((buf), SPDM_DOBJ_ID_REQ))

static struct spdm_dobj_hdr *sla_to_dobj_hdr(struct sla_buffer_hdr *buf)
{
	if (!buf)
		return NULL;

	return (struct spdm_dobj_hdr *) &buf[1];
}

static struct spdm_dobj_hdr *sla_to_dobj_hdr_check(struct sla_buffer_hdr *buf, u32 check_dobjid)
{
	struct spdm_dobj_hdr *hdr = sla_to_dobj_hdr(buf);

	if (WARN_ON_ONCE(!hdr))
		return NULL;

	if (hdr->id != check_dobjid) {
		pr_err("! ERROR: expected %d, found %d\n", check_dobjid, hdr->id);
		return NULL;
	}

	return hdr;
}

static void *sla_to_data(struct sla_buffer_hdr *buf, u32 dobjid)
{
	struct spdm_dobj_hdr *hdr = sla_to_dobj_hdr(buf);

	if (WARN_ON_ONCE(dobjid != SPDM_DOBJ_ID_REQ && dobjid != SPDM_DOBJ_ID_RESP))
		return NULL;

	if (!hdr)
		return NULL;

	return (u8 *) hdr + sla_dobj_id_to_size(dobjid);
}

/*
 * struct sev_data_tio_status - SEV_CMD_TIO_STATUS command
 *
 * @length: Length of this command buffer in bytes
 * @status_paddr: System physical address of the TIO_STATUS structure
 */
struct sev_data_tio_status {
	u32 length;
	u8 reserved[4];
	u64 status_paddr;
} __packed;

/* TIO_INIT */
struct sev_data_tio_init {
	u32 length;
	u8 reserved[12];
} __packed;

/*
 * struct sev_data_tio_dev_create - TIO_DEV_CREATE command
 *
 * @length: Length in bytes of this command buffer
 * @dev_ctx_sla: Scatter list address pointing to a buffer to be used as a device context buffer
 * @device_id: PCIe Routing Identifier of the device to connect to
 * @root_port_id: PCIe Routing Identifier of the root port of the device
 * @segment_id: PCIe Segment Identifier of the device to connect to
 */
struct sev_data_tio_dev_create {
	u32 length;
	u8 reserved1[4];
	struct sla_addr_t dev_ctx_sla;
	u16 device_id;
	u16 root_port_id;
	u8 segment_id;
	u8 reserved2[11];
} __packed;

/*
 * struct sev_data_tio_dev_connect - TIO_DEV_CONNECT command
 *
 * @length: Length in bytes of this command buffer
 * @spdm_ctrl: SPDM control structure defined in Section 5.1
 * @dev_ctx_sla: Scatter list address of the device context buffer
 * @tc_mask: Bitmask of the traffic classes to initialize for SEV-TIO usage.
 *           Setting the kth bit of the TC_MASK to 1 indicates that the traffic
 *           class k will be initialized
 * @cert_slot: Slot number of the certificate requested for constructing the SPDM session
 * @ide_stream_id: IDE stream IDs to be associated with this device.
 *                 Valid only if corresponding bit in TC_MASK is set
 */
struct sev_data_tio_dev_connect {
	u32 length;
	u8 reserved1[4];
	struct spdm_ctrl spdm_ctrl;
	u8 reserved2[8];
	struct sla_addr_t dev_ctx_sla;
	u8 tc_mask;
	u8 cert_slot;
	u8 reserved3[6];
	u8 ide_stream_id[8];
	u8 reserved4[8];
} __packed;

/*
 * struct sev_data_tio_dev_disconnect - TIO_DEV_DISCONNECT command
 *
 * @length: Length in bytes of this command buffer
 * @flags: Command flags (TIO_DEV_DISCONNECT_FLAG_FORCE: force disconnect)
 * @spdm_ctrl: SPDM control structure defined in Section 5.1
 * @dev_ctx_sla: Scatter list address of the device context buffer
 */
#define TIO_DEV_DISCONNECT_FLAG_FORCE	BIT(0)

struct sev_data_tio_dev_disconnect {
	u32 length;
	u32 flags;
	struct spdm_ctrl spdm_ctrl;
	struct sla_addr_t dev_ctx_sla;
} __packed;

/*
 * struct sev_data_tio_dev_meas - TIO_DEV_MEASUREMENTS command
 *
 * @length: Length in bytes of this command buffer
 * @flags: Command flags (TIO_DEV_MEAS_FLAG_RAW_BITSTREAM: request raw measurements)
 * @spdm_ctrl: SPDM control structure defined in Section 5.1
 * @dev_ctx_sla: Scatter list address of the device context buffer
 * @meas_nonce: Nonce for measurement freshness verification
 */
#define TIO_DEV_MEAS_FLAG_RAW_BITSTREAM	BIT(0)

struct sev_data_tio_dev_meas {
	u32 length;
	u32 flags;
	struct spdm_ctrl spdm_ctrl;
	struct sla_addr_t dev_ctx_sla;
	u8 meas_nonce[32];
} __packed;

/*
 * struct sev_data_tio_dev_certs - TIO_DEV_CERTIFICATES command
 *
 * @length: Length in bytes of this command buffer
 * @spdm_ctrl: SPDM control structure defined in Section 5.1
 * @dev_ctx_sla: Scatter list address of the device context buffer
 */
struct sev_data_tio_dev_certs {
	u32 length;
	u8 reserved[4];
	struct spdm_ctrl spdm_ctrl;
	struct sla_addr_t dev_ctx_sla;
} __packed;

/*
 * struct sev_data_tio_dev_reclaim - TIO_DEV_RECLAIM command
 *
 * @length: Length in bytes of this command buffer
 * @dev_ctx_sla: Scatter list address of the device context buffer
 *
 * This command reclaims resources associated with a device context.
 */
struct sev_data_tio_dev_reclaim {
	u32 length;
	u8 reserved[4];
	struct sla_addr_t dev_ctx_sla;
} __packed;

static struct sla_buffer_hdr *sla_buffer_map(struct sla_addr_t sla)
{
	struct sla_buffer_hdr *buf;

	BUILD_BUG_ON(sizeof(struct sla_buffer_hdr) != 0x40);
	if (IS_SLA_NULL(sla))
		return NULL;

	if (sla.page_type == SLA_PAGE_TYPE_SCATTER) {
		struct sla_addr_t *scatter = sla_to_va(sla);
		unsigned int i, npages = 0;

		for (i = 0; i < SLA_SCATTER_LEN(sla); ++i) {
			if (WARN_ON_ONCE(SLA_SZ(scatter[i]) > SZ_4K))
				return NULL;

			if (WARN_ON_ONCE(scatter[i].page_type == SLA_PAGE_TYPE_SCATTER))
				return NULL;

			if (IS_SLA_EOL(scatter[i])) {
				npages = i;
				break;
			}
		}
		if (WARN_ON_ONCE(!npages))
			return NULL;

		struct page **pp = kmalloc_array(npages, sizeof(pp[0]), GFP_KERNEL);

		if (!pp)
			return NULL;

		for (i = 0; i < npages; ++i)
			pp[i] = sla_to_page(scatter[i]);

		buf = vm_map_ram(pp, npages, 0);
		kfree(pp);
	} else {
		struct page *pg = sla_to_page(sla);

		buf = vm_map_ram(&pg, 1, 0);
	}

	return buf;
}

static void sla_buffer_unmap(struct sla_addr_t sla, struct sla_buffer_hdr *buf)
{
	if (!buf)
		return;

	if (sla.page_type == SLA_PAGE_TYPE_SCATTER) {
		struct sla_addr_t *scatter = sla_to_va(sla);
		unsigned int i, npages = 0;

		for (i = 0; i < SLA_SCATTER_LEN(sla); ++i) {
			if (IS_SLA_EOL(scatter[i])) {
				npages = i;
				break;
			}
		}
		if (!npages)
			return;

		vm_unmap_ram(buf, npages);
	} else {
		vm_unmap_ram(buf, 1);
	}
}

static void dobj_response_init(struct sla_buffer_hdr *buf)
{
	struct spdm_dobj_hdr *dobj = sla_to_dobj_hdr(buf);

	dobj->id = SPDM_DOBJ_ID_RESP;
	dobj->version.major = 0x1;
	dobj->version.minor = 0;
	dobj->length = 0;
	buf->payload_sz = sla_dobj_id_to_size(dobj->id) + dobj->length;
}

static void sla_free(struct sla_addr_t sla, size_t len, bool firmware_state)
{
	unsigned int npages = PAGE_ALIGN(len) >> PAGE_SHIFT;
	struct sla_addr_t *scatter = NULL;
	int ret = 0, i;

	if (IS_SLA_NULL(sla))
		return;

	if (firmware_state) {
		if (sla.page_type == SLA_PAGE_TYPE_SCATTER) {
			scatter = sla_to_va(sla);

			for (i = 0; i < npages; ++i) {
				if (IS_SLA_EOL(scatter[i]))
					break;

				ret = snp_reclaim_pages(sla_to_pa(scatter[i]), 1, false);
				if (ret)
					break;
			}
		} else {
			ret = snp_reclaim_pages(sla_to_pa(sla), 1, false);
		}
	}

	if (WARN_ON(ret))
		return;

	if (scatter) {
		for (i = 0; i < npages; ++i) {
			if (IS_SLA_EOL(scatter[i]))
				break;
			free_page((unsigned long)sla_to_va(scatter[i]));
		}
	}

	free_page((unsigned long)sla_to_va(sla));
}

static struct sla_addr_t sla_alloc(size_t len, bool firmware_state)
{
	unsigned long i, npages = PAGE_ALIGN(len) >> PAGE_SHIFT;
	struct sla_addr_t *scatter = NULL;
	struct sla_addr_t ret = SLA_NULL;
	struct sla_buffer_hdr *buf;
	struct page *pg;

	if (npages == 0)
		return ret;

	if (WARN_ON_ONCE(npages > ((PAGE_SIZE / sizeof(struct sla_addr_t)) + 1)))
		return ret;

	BUILD_BUG_ON(PAGE_SIZE < SZ_4K);

	if (npages > 1) {
		pg = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!pg)
			return SLA_NULL;

		ret = make_sla(pg, true);
		scatter = page_to_virt(pg);
		for (i = 0; i < npages; ++i) {
			pg = alloc_page(GFP_KERNEL | __GFP_ZERO);
			if (!pg)
				goto no_reclaim_exit;

			scatter[i] = make_sla(pg, false);
		}
		scatter[i] = SLA_EOL;
	} else {
		pg = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!pg)
			return SLA_NULL;

		ret = make_sla(pg, false);
	}

	buf = sla_buffer_map(ret);
	if (!buf)
		goto no_reclaim_exit;

	buf->capacity_sz = (npages << PAGE_SHIFT);
	sla_buffer_unmap(ret, buf);

	if (firmware_state) {
		if (scatter) {
			for (i = 0; i < npages; ++i) {
				if (rmp_make_private(sla_to_pfn(scatter[i]), 0,
						     PG_LEVEL_4K, 0, true))
					goto free_exit;
			}
		} else {
			if (rmp_make_private(sla_to_pfn(ret), 0, PG_LEVEL_4K, 0, true))
				goto no_reclaim_exit;
		}
	}

	return ret;

no_reclaim_exit:
	firmware_state = false;
free_exit:
	sla_free(ret, len, firmware_state);
	return SLA_NULL;
}

/* Expands a buffer, only firmware owned buffers allowed for now */
static int sla_expand(struct sla_addr_t *sla, size_t *len)
{
	struct sla_buffer_hdr *oldbuf = sla_buffer_map(*sla), *newbuf;
	struct sla_addr_t oldsla = *sla, newsla;
	size_t oldlen = *len, newlen;

	if (!oldbuf)
		return -EFAULT;

	newlen = oldbuf->capacity_sz;
	if (oldbuf->capacity_sz == oldlen) {
		/* This buffer does not require expansion, must be another buffer */
		sla_buffer_unmap(oldsla, oldbuf);
		return 1;
	}

	pr_notice("Expanding BUFFER from %ld to %ld bytes\n", oldlen, newlen);

	newsla = sla_alloc(newlen, true);
	if (IS_SLA_NULL(newsla))
		return -ENOMEM;

	newbuf = sla_buffer_map(newsla);
	if (!newbuf) {
		sla_free(newsla, newlen, true);
		return -EFAULT;
	}

	memcpy(newbuf, oldbuf, oldlen);

	sla_buffer_unmap(newsla, newbuf);
	sla_free(oldsla, oldlen, true);
	*sla = newsla;
	*len = newlen;

	return 0;
}

static int sev_tio_do_cmd(int cmd, void *data, size_t data_len, int *psp_ret,
			  struct tsm_dsm_tio *dev_data)
{
	int rc;

	*psp_ret = 0;
	rc = sev_do_cmd(cmd, data, psp_ret);

	if (WARN_ON(!rc && *psp_ret == SEV_RET_SPDM_REQUEST))
		return -EIO;

	if (rc == 0 && *psp_ret == SEV_RET_EXPAND_BUFFER_LENGTH_REQUEST) {
		int rc1, rc2;

		rc1 = sla_expand(&dev_data->output, &dev_data->output_len);
		if (rc1 < 0)
			return rc1;

		rc2 = sla_expand(&dev_data->scratch, &dev_data->scratch_len);
		if (rc2 < 0)
			return rc2;

		if (!rc1 && !rc2)
			/* Neither buffer requires expansion, this is wrong */
			return -EFAULT;

		*psp_ret = 0;
		rc = sev_do_cmd(cmd, data, psp_ret);
	}

	if ((rc == 0 || rc == -EIO) && *psp_ret == SEV_RET_SPDM_REQUEST) {
		struct spdm_dobj_hdr_resp *resp_hdr;
		struct spdm_dobj_hdr_req *req_hdr;
		struct sev_tio_status *tio_status = to_tio_status(dev_data);
		size_t resp_len = tio_status->spdm_req_size_max -
			(sla_dobj_id_to_size(SPDM_DOBJ_ID_RESP) + sizeof(struct sla_buffer_hdr));

		if (!dev_data->cmd) {
			if (WARN_ON_ONCE(!data_len || (data_len != *(u32 *) data)))
				return -EINVAL;
			if (WARN_ON(data_len > sizeof(dev_data->cmd_data)))
				return -EFAULT;
			memcpy(dev_data->cmd_data, data, data_len);
			memset(&dev_data->cmd_data[data_len], 0xFF,
			       sizeof(dev_data->cmd_data) - data_len);
			dev_data->cmd = cmd;
		}

		req_hdr = sla_to_dobj_req_hdr(dev_data->reqbuf);
		resp_hdr = sla_to_dobj_resp_hdr(dev_data->respbuf);
		switch (req_hdr->data_type) {
		case DOBJ_DATA_TYPE_SPDM:
			rc = PCI_DOE_FEATURE_CMA;
			break;
		case DOBJ_DATA_TYPE_SECURE_SPDM:
			rc = PCI_DOE_FEATURE_SSESSION;
			break;
		default:
			return -EINVAL;
		}
		resp_hdr->data_type = req_hdr->data_type;
		dev_data->spdm.req_len = req_hdr->hdr.length -
			sla_dobj_id_to_size(SPDM_DOBJ_ID_REQ);
		dev_data->spdm.rsp_len = resp_len;
	} else if (dev_data && dev_data->cmd) {
		/* For either error or success just stop the bouncing */
		memset(dev_data->cmd_data, 0, sizeof(dev_data->cmd_data));
		dev_data->cmd = 0;
	}

	return rc;
}

int sev_tio_continue(struct tsm_dsm_tio *dev_data)
{
	struct spdm_dobj_hdr_resp *resp_hdr;
	int ret;

	if (!dev_data || !dev_data->cmd)
		return -EINVAL;

	resp_hdr = sla_to_dobj_resp_hdr(dev_data->respbuf);
	resp_hdr->hdr.length = ALIGN(sla_dobj_id_to_size(SPDM_DOBJ_ID_RESP) +
				     dev_data->spdm.rsp_len, 32);
	dev_data->respbuf->payload_sz = resp_hdr->hdr.length;

	ret = sev_tio_do_cmd(dev_data->cmd, dev_data->cmd_data, 0,
			     &dev_data->psp_ret, dev_data);
	if (ret)
		return ret;

	if (dev_data->psp_ret != SEV_RET_SUCCESS)
		return -EINVAL;

	return 0;
}

static void spdm_ctrl_init(struct spdm_ctrl *ctrl, struct tsm_dsm_tio *dev_data)
{
	ctrl->req = dev_data->req;
	ctrl->resp = dev_data->resp;
	ctrl->scratch = dev_data->scratch;
	ctrl->output = dev_data->output;
}

static void spdm_ctrl_free(struct tsm_dsm_tio *dev_data)
{
	struct sev_tio_status *tio_status = to_tio_status(dev_data);
	size_t len = tio_status->spdm_req_size_max -
		(sla_dobj_id_to_size(SPDM_DOBJ_ID_RESP) +
		 sizeof(struct sla_buffer_hdr));
	struct tsm_spdm *spdm = &dev_data->spdm;

	sla_buffer_unmap(dev_data->resp, dev_data->respbuf);
	sla_buffer_unmap(dev_data->req, dev_data->reqbuf);
	spdm->rsp = NULL;
	spdm->req = NULL;
	sla_free(dev_data->req, len, true);
	sla_free(dev_data->resp, len, false);
	sla_free(dev_data->scratch, tio_status->spdm_scratch_size_max, true);

	dev_data->req.sla = 0;
	dev_data->resp.sla = 0;
	dev_data->scratch.sla = 0;
	dev_data->respbuf = NULL;
	dev_data->reqbuf = NULL;
	sla_free(dev_data->output, tio_status->spdm_out_size_max, true);
}

static int spdm_ctrl_alloc(struct tsm_dsm_tio *dev_data)
{
	struct sev_tio_status *tio_status = to_tio_status(dev_data);
	struct tsm_spdm *spdm = &dev_data->spdm;
	int ret;

	dev_data->req = sla_alloc(tio_status->spdm_req_size_max, true);
	dev_data->resp = sla_alloc(tio_status->spdm_req_size_max, false);
	dev_data->scratch_len = tio_status->spdm_scratch_size_max;
	dev_data->scratch = sla_alloc(dev_data->scratch_len, true);
	dev_data->output_len = tio_status->spdm_out_size_max;
	dev_data->output = sla_alloc(dev_data->output_len, true);

	if (IS_SLA_NULL(dev_data->req) || IS_SLA_NULL(dev_data->resp) ||
	    IS_SLA_NULL(dev_data->scratch) || IS_SLA_NULL(dev_data->dev_ctx)) {
		ret = -ENOMEM;
		goto free_spdm_exit;
	}

	dev_data->reqbuf = sla_buffer_map(dev_data->req);
	dev_data->respbuf = sla_buffer_map(dev_data->resp);
	if (!dev_data->reqbuf || !dev_data->respbuf) {
		ret = -EFAULT;
		goto free_spdm_exit;
	}

	spdm->req = sla_to_data(dev_data->reqbuf, SPDM_DOBJ_ID_REQ);
	spdm->rsp = sla_to_data(dev_data->respbuf, SPDM_DOBJ_ID_RESP);
	if (!spdm->req || !spdm->rsp) {
		ret = -EFAULT;
		goto free_spdm_exit;
	}

	dobj_response_init(dev_data->respbuf);

	return 0;

free_spdm_exit:
	spdm_ctrl_free(dev_data);
	return ret;
}

int sev_tio_init_locked(void *tio_status_page)
{
	struct sev_tio_status *tio_status = tio_status_page;
	struct sev_data_tio_status data_status = {
		.length = sizeof(data_status),
	};
	int ret, psp_ret;

	data_status.status_paddr = __psp_pa(tio_status_page);
	ret = __sev_do_cmd_locked(SEV_CMD_TIO_STATUS, &data_status, &psp_ret);
	if (ret)
		return ret;

	if (tio_status->length < offsetofend(struct sev_tio_status, tdictx_size) ||
	    tio_status->reserved)
		return -EFAULT;

	if (!tio_status->tio_en && !tio_status->tio_init_done)
		return -ENOENT;

	if (tio_status->tio_init_done)
		return -EBUSY;

	struct sev_data_tio_init ti = { .length = sizeof(ti) };

	ret = __sev_do_cmd_locked(SEV_CMD_TIO_INIT, &ti, &psp_ret);
	if (ret)
		return ret;

	ret = __sev_do_cmd_locked(SEV_CMD_TIO_STATUS, &data_status, &psp_ret);
	if (ret)
		return ret;

	return 0;
}

int sev_tio_dev_create(struct tsm_dsm_tio *dev_data, u16 device_id,
		       u16 root_port_id, u8 segment_id)
{
	struct sev_tio_status *tio_status = to_tio_status(dev_data);
	struct sev_data_tio_dev_create create = {
		.length = sizeof(create),
		.device_id = device_id,
		.root_port_id = root_port_id,
		.segment_id = segment_id,
	};
	void *data_pg;
	int ret;

	dev_data->dev_ctx = sla_alloc(tio_status->devctx_size, true);
	if (IS_SLA_NULL(dev_data->dev_ctx))
		return -ENOMEM;

	data_pg = snp_alloc_firmware_page(GFP_KERNEL_ACCOUNT);
	if (!data_pg) {
		ret = -ENOMEM;
		goto free_ctx_exit;
	}

	create.dev_ctx_sla = dev_data->dev_ctx;
	ret = sev_do_cmd(SEV_CMD_TIO_DEV_CREATE, &create, &dev_data->psp_ret);
	if (ret)
		goto free_data_pg_exit;

	dev_data->data_pg = data_pg;

	return 0;

free_data_pg_exit:
	snp_free_firmware_page(data_pg);
free_ctx_exit:
	sla_free(create.dev_ctx_sla, tio_status->devctx_size, true);
	return ret;
}

int sev_tio_dev_reclaim(struct tsm_dsm_tio *dev_data)
{
	struct sev_tio_status *tio_status = to_tio_status(dev_data);
	struct sev_data_tio_dev_reclaim r = {
		.length = sizeof(r),
		.dev_ctx_sla = dev_data->dev_ctx,
	};
	int ret;

	if (dev_data->data_pg) {
		snp_free_firmware_page(dev_data->data_pg);
		dev_data->data_pg = NULL;
	}

	if (IS_SLA_NULL(dev_data->dev_ctx))
		return 0;

	ret = sev_do_cmd(SEV_CMD_TIO_DEV_RECLAIM, &r, &dev_data->psp_ret);

	sla_free(dev_data->dev_ctx, tio_status->devctx_size, true);
	dev_data->dev_ctx = SLA_NULL;

	spdm_ctrl_free(dev_data);

	return ret;
}

int sev_tio_dev_connect(struct tsm_dsm_tio *dev_data, u8 tc_mask, u8 ids[8], u8 cert_slot)
{
	struct sev_data_tio_dev_connect connect = {
		.length = sizeof(connect),
		.tc_mask = tc_mask,
		.cert_slot = cert_slot,
		.dev_ctx_sla = dev_data->dev_ctx,
		.ide_stream_id = {
			ids[0], ids[1], ids[2], ids[3],
			ids[4], ids[5], ids[6], ids[7]
		},
	};
	int ret;

	if (WARN_ON(IS_SLA_NULL(dev_data->dev_ctx)))
		return -EFAULT;
	if (!(tc_mask & 1))
		return -EINVAL;

	ret = spdm_ctrl_alloc(dev_data);
	if (ret)
		return ret;

	spdm_ctrl_init(&connect.spdm_ctrl, dev_data);

	return sev_tio_do_cmd(SEV_CMD_TIO_DEV_CONNECT, &connect, sizeof(connect),
			      &dev_data->psp_ret, dev_data);
}

int sev_tio_dev_disconnect(struct tsm_dsm_tio *dev_data, bool force)
{
	struct sev_data_tio_dev_disconnect dc = {
		.length = sizeof(dc),
		.dev_ctx_sla = dev_data->dev_ctx,
		.flags = force ? TIO_DEV_DISCONNECT_FLAG_FORCE : 0,
	};

	if (WARN_ON_ONCE(IS_SLA_NULL(dev_data->dev_ctx)))
		return -EFAULT;

	spdm_ctrl_init(&dc.spdm_ctrl, dev_data);

	return sev_tio_do_cmd(SEV_CMD_TIO_DEV_DISCONNECT, &dc, sizeof(dc),
			      &dev_data->psp_ret, dev_data);
}

int sev_tio_cmd_buffer_len(int cmd)
{
	switch (cmd) {
	case SEV_CMD_TIO_STATUS:		return sizeof(struct sev_data_tio_status);
	case SEV_CMD_TIO_INIT:			return sizeof(struct sev_data_tio_init);
	case SEV_CMD_TIO_DEV_CREATE:		return sizeof(struct sev_data_tio_dev_create);
	case SEV_CMD_TIO_DEV_RECLAIM:		return sizeof(struct sev_data_tio_dev_reclaim);
	case SEV_CMD_TIO_DEV_CONNECT:		return sizeof(struct sev_data_tio_dev_connect);
	case SEV_CMD_TIO_DEV_DISCONNECT:	return sizeof(struct sev_data_tio_dev_disconnect);
	default:				return 0;
	}
}
