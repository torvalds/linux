/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#ifndef _FNIC_NVME_H
#define _FNIC_NVME_H

#include "fdls_fc.h"
#include "fnic_fdls.h"

#define FNIC_NVME_TPORT_REMOVE_WAIT (5 * 1000)
#define FNIC_NVME_TPORT_LIST_EMPTY_WAIT (FNIC_NVME_TPORT_REMOVE_WAIT * 2)
#define FNIC_TPORT_CLEANUP_WAIT_COUNT 8
#define FNIC_NVME_LPORT_REMOVE_WAIT (2 * 60 * 1000)

#define FNIC_LS_REQ_FLAGS_NONE      0x0
#define FNIC_LS_REQ_FLAGS_ABORTED   0x1
#define FNIC_LS_REQ_FLAGS_DONE      0x2
#define FNIC_LS_REQ_ABORT_COMPLETED 0x4
#define FNIC_STATUS_LS_REQ_ABORTED 0x1

#define FNIC_LS_REQ_MIN_TMO_SECS (2)
#define FNIC_LS_REQ_MAX_TMO_SECS (5)
#define FNIC_NVME_ADMIN_IO_TIMEOUT 30000	/* mSec */
#define FNIC_NVME_NO_FREE_TAG     (0xFFFF)

#define FNIC_LS_REQ_TMO_MSECS(tmo)    (((tmo >= FNIC_LS_REQ_MIN_TMO_SECS) && \
					(tmo <= FNIC_LS_REQ_MAX_TMO_SECS)) ? \
					(tmo * 1000) : (FNIC_LS_REQ_MIN_TMO_SECS * 1000))

#define IS_ADMIN_IO(_io_req)     \
	(NVME_CMD_FLAGS(_io_req) & FNIC_NVME_ADMIN_IO_TIMER_PENDING)

extern unsigned int nvme_max_ios_to_process;
extern unsigned int nvme_dev_loss_tmo;
extern spinlock_t fnic_list_lock;

enum nvfnic_lsreq_state_e {
	FNIC_LS_REQ_CMD_INIT = 0,
	FNIC_LS_REQ_CMD_PENDING,
	FNIC_LS_REQ_CMD_ABTS_PENDING,
	FNIC_LS_REQ_CMD_COMPLETE,
	FNIC_LS_REQ_ABTS_COMPLETE,
	FNIC_LS_REQ_CMD_ABTS_STARTED,
};

struct fnic_nvme_tag {
	struct list_head free_list;
	int tag_id;
};

struct nvfnic_ls_req {
	struct list_head list;
	struct nvmefc_ls_req *ls_req;
	uint16_t oxid;
	struct timer_list ls_req_timer;
	struct fnic *fnic;
	struct fnic_tport_s *tport;
	int state;
	unsigned int flags;
};

#if IS_REACHABLE(CONFIG_NVME_FC)
int nvfnic_nvme_io_done_handler(void *arg);
struct fnic_io_req *nvfnic_find_io_req_by_tag(struct fnic *fnic, uint16_t tag);
void nvfnic_reset_fcpio_tag_pool(struct fnic_iport_s *iport);
int nvfnic_add_lport(struct fnic *fnic);
void nvfnic_fcpio_abort(struct nvme_fc_local_port *lport,
			struct nvme_fc_remote_port *rport,
			void *hw_queue_handle, struct nvmefc_fcp_req *fcp_req);
void nvfnic_remote_port_delete(struct nvme_fc_remote_port *rport);
void nvfnic_local_port_delete(struct nvme_fc_local_port *lport);
int nvfnic_dma_map_sgl(struct fnic *fnic, struct fnic_io_req *io_req,
		       int sg_count);
void nvfnic_dma_unmap_sgl(struct fnic *fnic, struct fnic_io_req *io_req);
int nvfnic_get_sg_count(struct fnic_io_req *io_req);
void nvfnic_release_nvme_ioreq_buf(struct fnic_iport_s *iport,
				   struct fnic_io_req *io_req);
void nvfnic_dump_nvcmd(struct fnic_io_req *io_req, uint8_t flags);
bool _cleanup_tport_io(struct sbitmap *map, unsigned int tag, void *data);
void nvfnic_flush_nvme_io_list(struct fnic *fnic);
void nvfnic_fcpio_cmpl(struct fnic_io_req *io_req);
bool nvfnic_transport_ready(struct fnic_iport_s *iport,
			    struct fnic_tport_s *tport);
int nvfnic_alloc_fcpio_tag(struct fnic_iport_s *iport,
				struct fnic_io_req *io_req);
int nvfnic_queuecommand(struct fnic_io_req *io_req);
void nvfnic_free_fcpio_tag(struct fnic_iport_s *iport,
			   struct fnic_io_req *io_req);
void nvfnic_delete_lport(struct fnic_iport_s *iport);
int nvfnic_add_tport(struct fnic *fnic, struct fnic_tport_s *tport,
		     unsigned long flags);
void nvfnic_cleanup_all_nvme_ios(struct fnic *fnic);
void nvfnic_delete_tport_work(struct work_struct *work);
void nvfnic_delete_tport(struct fnic_iport_s *iport,
			 struct fnic_tport_s *tport, unsigned long flags);
void nvfnic_fcpio_nvme_fast_cmpl_handler(struct fnic *fnic,
					 struct fcpio_fw_req *desc);
void nvfnic_ls_rsp_recv(struct fnic_iport_s *iport,
			struct fc_frame_header *fchdr, int len);
void nvfnic_process_ls_abts_rsp(struct fnic_iport_s *iport,
				struct fc_frame_header *fchdr);
void nvfnic_admin_io_timeout(struct timer_list *t);
void nvfnic_fcpio_nvme_itmf_cmpl_handler(struct fnic *fnic,
					 struct fcpio_fw_req *desc);
void nvfnic_nvme_zero_devloss_tports(struct fnic *fnic);
bool nvfnic_queue_abort_io_req(struct fnic *fnic, int tag, u32 task_req,
			       struct fnic_io_req *io_req);
void nvfnic_ls_req_abort(struct nvme_fc_local_port *lport,
			 struct nvme_fc_remote_port *rport,
			 struct nvmefc_ls_req *lsreq);
int nvfnic_create_queue(struct nvme_fc_local_port *lport, unsigned int idx,
			u16 size, void **handle);
int nvfnic_ls_req_send(struct nvme_fc_local_port *lport,
		       struct nvme_fc_remote_port *rport,
		       struct nvmefc_ls_req *ls_req);
void nvfnic_ls_req_timeout(struct timer_list *t);
uint16_t nvfnic_alloc_ls_req_oxid(struct fnic_iport_s *iport);
struct nvfnic_ls_req *nvfnic_find_ls_req(struct fnic_tport_s *tport,
					 uint16_t oxid);
void nvfnic_terminate_tport_ios(struct fnic *fnic, struct fnic_tport_s *tport);
bool _terminate_tport_ios(struct sbitmap *map, unsigned int tag, void *data);
bool _cleanup_all_nvme_io(struct sbitmap *map, unsigned int tag, void *data);
void nvfnic_cleanup_all_nvme_ios(struct fnic *fnic);
int nvfnic_fcpio_send(struct nvme_fc_local_port *lport,
		      struct nvme_fc_remote_port *rport, void *hw_queue_handle,
		      struct nvmefc_fcp_req *fcp_req);
void nvfnic_fcpio_ersp_cmpl_handler(struct fnic *fnic,
				    struct fcpio_fw_req *desc, int sw_flag);
void nvfnic_terminate_tport_ls_reqs(struct fnic *fnic,
				    struct fnic_tport_s *tport);
void nvfnic_terminate_tport_admin_ios(struct fnic *fnic,
				      struct fnic_tport_s *tport);
void nvfnic_cleanup_tport_io(struct fnic *fnic, struct fnic_tport_s *tport);
void nvfnic_nvme_unload(struct fnic *fnic);
int nvfnic_get_nvmef_info(struct fnic *fnic, struct fnic_nvmef_info *info);
void nvfnic_exch_reset(struct fnic_iport_s *iport, struct fnic_tport_s *tport);
void nvfnic_nvme_iodone_work(struct work_struct *work);
#else
static inline int nvfnic_add_lport(struct fnic *fnic)
{
	return -EOPNOTSUPP;
}

static inline void nvfnic_nvme_unload(struct fnic *fnic)
{
}

static inline void nvfnic_flush_nvme_io_list(struct fnic *fnic)
{
}

static inline void nvfnic_nvme_iodone_work(struct work_struct *work)
{
}

static inline void nvfnic_exch_reset(struct fnic_iport_s *iport,
				     struct fnic_tport_s *tport)
{
}

static inline void nvfnic_process_ls_abts_rsp(struct fnic_iport_s *iport,
					      struct fc_frame_header *fchdr)
{
}

static inline void nvfnic_ls_rsp_recv(struct fnic_iport_s *iport,
				      struct fc_frame_header *fchdr, int len)
{
}

static inline int nvfnic_add_tport(struct fnic *fnic,
				   struct fnic_tport_s *tport,
				   unsigned long flags)
{
	return 0;
}

static inline void nvfnic_delete_tport(struct fnic_iport_s *iport,
				       struct fnic_tport_s *tport,
				       unsigned long flags)
{
}

static inline void nvfnic_cleanup_all_nvme_ios(struct fnic *fnic)
{
}

static inline void nvfnic_fcpio_nvme_fast_cmpl_handler(struct fnic *fnic,
						       struct fcpio_fw_req *desc)
{
}

static inline void nvfnic_fcpio_ersp_cmpl_handler(struct fnic *fnic,
						  struct fcpio_fw_req *desc,
						  int sw_flag)
{
}

static inline void nvfnic_fcpio_nvme_itmf_cmpl_handler(struct fnic *fnic,
						       struct fcpio_fw_req *desc)
{
}

static inline int nvfnic_get_nvmef_info(struct fnic *fnic,
					struct fnic_nvmef_info *info)
{
	return 0;
}
#endif

extern const char *fnic_fcpio_status_to_str(unsigned int status);
#endif				/* _FNIC_NVME_H */
