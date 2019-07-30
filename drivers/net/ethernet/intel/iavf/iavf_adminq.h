/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#ifndef _IAVF_ADMINQ_H_
#define _IAVF_ADMINQ_H_

#include "iavf_osdep.h"
#include "iavf_status.h"
#include "iavf_adminq_cmd.h"

#define IAVF_ADMINQ_DESC(R, i)   \
	(&(((struct iavf_aq_desc *)((R).desc_buf.va))[i]))

#define IAVF_ADMINQ_DESC_ALIGNMENT 4096

struct iavf_adminq_ring {
	struct iavf_virt_mem dma_head;	/* space for dma structures */
	struct iavf_dma_mem desc_buf;	/* descriptor ring memory */
	struct iavf_virt_mem cmd_buf;	/* command buffer memory */

	union {
		struct iavf_dma_mem *asq_bi;
		struct iavf_dma_mem *arq_bi;
	} r;

	u16 count;		/* Number of descriptors */
	u16 rx_buf_len;		/* Admin Receive Queue buffer length */

	/* used for interrupt processing */
	u16 next_to_use;
	u16 next_to_clean;

	/* used for queue tracking */
	u32 head;
	u32 tail;
	u32 len;
	u32 bah;
	u32 bal;
};

/* ASQ transaction details */
struct iavf_asq_cmd_details {
	void *callback; /* cast from type IAVF_ADMINQ_CALLBACK */
	u64 cookie;
	u16 flags_ena;
	u16 flags_dis;
	bool async;
	bool postpone;
	struct iavf_aq_desc *wb_desc;
};

#define IAVF_ADMINQ_DETAILS(R, i)   \
	(&(((struct iavf_asq_cmd_details *)((R).cmd_buf.va))[i]))

/* ARQ event information */
struct iavf_arq_event_info {
	struct iavf_aq_desc desc;
	u16 msg_len;
	u16 buf_len;
	u8 *msg_buf;
};

/* Admin Queue information */
struct iavf_adminq_info {
	struct iavf_adminq_ring arq;    /* receive queue */
	struct iavf_adminq_ring asq;    /* send queue */
	u32 asq_cmd_timeout;            /* send queue cmd write back timeout*/
	u16 num_arq_entries;            /* receive queue depth */
	u16 num_asq_entries;            /* send queue depth */
	u16 arq_buf_size;               /* receive queue buffer size */
	u16 asq_buf_size;               /* send queue buffer size */
	u16 fw_maj_ver;                 /* firmware major version */
	u16 fw_min_ver;                 /* firmware minor version */
	u32 fw_build;                   /* firmware build number */
	u16 api_maj_ver;                /* api major version */
	u16 api_min_ver;                /* api minor version */

	struct mutex asq_mutex; /* Send queue lock */
	struct mutex arq_mutex; /* Receive queue lock */

	/* last status values on send and receive queues */
	enum iavf_admin_queue_err asq_last_status;
	enum iavf_admin_queue_err arq_last_status;
};

/**
 * iavf_aq_rc_to_posix - convert errors to user-land codes
 * aq_ret: AdminQ handler error code can override aq_rc
 * aq_rc: AdminQ firmware error code to convert
 **/
static inline int iavf_aq_rc_to_posix(int aq_ret, int aq_rc)
{
	int aq_to_posix[] = {
		0,           /* IAVF_AQ_RC_OK */
		-EPERM,      /* IAVF_AQ_RC_EPERM */
		-ENOENT,     /* IAVF_AQ_RC_ENOENT */
		-ESRCH,      /* IAVF_AQ_RC_ESRCH */
		-EINTR,      /* IAVF_AQ_RC_EINTR */
		-EIO,        /* IAVF_AQ_RC_EIO */
		-ENXIO,      /* IAVF_AQ_RC_ENXIO */
		-E2BIG,      /* IAVF_AQ_RC_E2BIG */
		-EAGAIN,     /* IAVF_AQ_RC_EAGAIN */
		-ENOMEM,     /* IAVF_AQ_RC_ENOMEM */
		-EACCES,     /* IAVF_AQ_RC_EACCES */
		-EFAULT,     /* IAVF_AQ_RC_EFAULT */
		-EBUSY,      /* IAVF_AQ_RC_EBUSY */
		-EEXIST,     /* IAVF_AQ_RC_EEXIST */
		-EINVAL,     /* IAVF_AQ_RC_EINVAL */
		-ENOTTY,     /* IAVF_AQ_RC_ENOTTY */
		-ENOSPC,     /* IAVF_AQ_RC_ENOSPC */
		-ENOSYS,     /* IAVF_AQ_RC_ENOSYS */
		-ERANGE,     /* IAVF_AQ_RC_ERANGE */
		-EPIPE,      /* IAVF_AQ_RC_EFLUSHED */
		-ESPIPE,     /* IAVF_AQ_RC_BAD_ADDR */
		-EROFS,      /* IAVF_AQ_RC_EMODE */
		-EFBIG,      /* IAVF_AQ_RC_EFBIG */
	};

	/* aq_rc is invalid if AQ timed out */
	if (aq_ret == IAVF_ERR_ADMIN_QUEUE_TIMEOUT)
		return -EAGAIN;

	if (!((u32)aq_rc < (sizeof(aq_to_posix) / sizeof((aq_to_posix)[0]))))
		return -ERANGE;

	return aq_to_posix[aq_rc];
}

/* general information */
#define IAVF_AQ_LARGE_BUF	512
#define IAVF_ASQ_CMD_TIMEOUT	250000  /* usecs */

void iavf_fill_default_direct_cmd_desc(struct iavf_aq_desc *desc, u16 opcode);

#endif /* _IAVF_ADMINQ_H_ */
