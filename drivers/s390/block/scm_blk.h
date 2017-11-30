/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SCM_BLK_H
#define SCM_BLK_H

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/genhd.h>
#include <linux/list.h>

#include <asm/debug.h>
#include <asm/eadm.h>

#define SCM_NR_PARTS 8
#define SCM_QUEUE_DELAY 5

struct scm_blk_dev {
	struct request_queue *rq;
	struct gendisk *gendisk;
	struct blk_mq_tag_set tag_set;
	struct scm_device *scmdev;
	spinlock_t lock;
	atomic_t queued_reqs;
	enum {SCM_OPER, SCM_WR_PROHIBIT} state;
	struct list_head finished_requests;
};

struct scm_request {
	struct scm_blk_dev *bdev;
	struct aidaw *next_aidaw;
	struct request **request;
	struct aob *aob;
	struct list_head list;
	u8 retries;
	blk_status_t error;
};

#define to_aobrq(rq) container_of((void *) rq, struct aob_rq_header, data)

int scm_blk_dev_setup(struct scm_blk_dev *, struct scm_device *);
void scm_blk_dev_cleanup(struct scm_blk_dev *);
void scm_blk_set_available(struct scm_blk_dev *);
void scm_blk_irq(struct scm_device *, void *, blk_status_t);

struct aidaw *scm_aidaw_fetch(struct scm_request *scmrq, unsigned int bytes);

int scm_drv_init(void);
void scm_drv_cleanup(void);

extern debug_info_t *scm_debug;

#define SCM_LOG(imp, txt) do {					\
		debug_text_event(scm_debug, imp, txt);		\
	} while (0)

static inline void SCM_LOG_HEX(int level, void *data, int length)
{
	debug_event(scm_debug, level, data, length);
}

static inline void SCM_LOG_STATE(int level, struct scm_device *scmdev)
{
	struct {
		u64 address;
		u8 oper_state;
		u8 rank;
	} __packed data = {
		.address = scmdev->address,
		.oper_state = scmdev->attrs.oper_state,
		.rank = scmdev->attrs.rank,
	};

	SCM_LOG_HEX(level, &data, sizeof(data));
}

#endif /* SCM_BLK_H */
