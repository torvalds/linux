/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#ifndef _AIE4_PCI_H_
#define _AIE4_PCI_H_

#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/pci.h>

#include "aie.h"
#include "amdxdna_mailbox.h"

struct cert_comp {
	struct amdxdna_dev_hdl          *ndev;
	u32                             msix_idx;
	int                             irq;
	struct kref                     kref;
	wait_queue_head_t               waitq;
};

struct amdxdna_hwctx_priv {
	struct amdxdna_gem_obj          *umq_bo;
	u64                             *umq_read_index;
	u64                             *umq_write_index;

	struct cert_comp                *cert_comp;
	u32                             hw_ctx_id;
};

struct amdxdna_dev_priv {
	const char              *npufw_path;
	const char              *certfw_path;
	u32			mbox_bar;
	u32			mbox_rbuf_bar;
	u64			mbox_info_off;
	u32			doorbell_off;

	struct aie_bar_off_pair	psp_regs_off[PSP_MAX_REGS];
	struct aie_bar_off_pair	smu_regs_off[SMU_MAX_REGS];
};

struct amdxdna_dev_hdl {
	struct aie_device		aie;
	const struct amdxdna_dev_priv	*priv;
	void			__iomem *mbox_base;
	void			__iomem *rbuf_base;

	struct mailbox			*mbox;
	u32				partition_id;

	struct xarray                   cert_comp_xa; /* device level indexed by msix id */
	struct mutex                    cert_comp_lock; /* protects cert_comp operations*/

	void				*work_buf;
	dma_addr_t			work_buf_addr;
	u32				work_buf_size;
};

/* aie4_message.c */
int aie4_query_aie_metadata(struct amdxdna_dev_hdl *ndev,
			    struct amdxdna_drm_query_aie_metadata *metadata);
int aie4_suspend_fw(struct amdxdna_dev_hdl *ndev);
int aie4_attach_work_buffer(struct amdxdna_dev_hdl *ndev);

/* aie4_ctx.c */
int aie4_hwctx_init(struct amdxdna_hwctx *hwctx);
void aie4_hwctx_fini(struct amdxdna_hwctx *hwctx);
int aie4_cmd_wait(struct amdxdna_hwctx *hwctx, u64 seq, u32 timeout);
int aie4_hwctx_valid_doorbell(struct amdxdna_client *client, u32 vm_pgoff);

/* aie4_sriov.c */
#if IS_ENABLED(CONFIG_PCI_IOV)
int aie4_sriov_configure(struct amdxdna_dev *xdna, int num_vfs);
int aie4_sriov_stop(struct amdxdna_dev_hdl *ndev);
#else
#define aie4_sriov_configure NULL
static inline int aie4_sriov_stop(struct amdxdna_dev_hdl *ndev)
{
	return 0;
}
#endif

extern const struct amdxdna_dev_ops aie4_pf_ops;
extern const struct amdxdna_dev_ops aie4_vf_ops;

#endif /* _AIE4_PCI_H_ */
