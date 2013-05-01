#ifndef SCM_BLK_H
#define SCM_BLK_H

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/list.h>

#include <asm/debug.h>
#include <asm/eadm.h>

#define SCM_NR_PARTS 8
#define SCM_QUEUE_DELAY 5

struct scm_blk_dev {
	struct tasklet_struct tasklet;
	struct request_queue *rq;
	struct gendisk *gendisk;
	struct scm_device *scmdev;
	spinlock_t rq_lock;	/* guard the request queue */
	spinlock_t lock;	/* guard the rest of the blockdev */
	atomic_t queued_reqs;
	struct list_head finished_requests;
#ifdef CONFIG_SCM_BLOCK_CLUSTER_WRITE
	struct list_head cluster_list;
#endif
};

struct scm_request {
	struct scm_blk_dev *bdev;
	struct request *request;
	struct aidaw *aidaw;
	struct aob *aob;
	struct list_head list;
	u8 retries;
	int error;
#ifdef CONFIG_SCM_BLOCK_CLUSTER_WRITE
	struct {
		enum {CLUSTER_NONE, CLUSTER_READ, CLUSTER_WRITE} state;
		struct list_head list;
		void **buf;
	} cluster;
#endif
};

#define to_aobrq(rq) container_of((void *) rq, struct aob_rq_header, data)

int scm_blk_dev_setup(struct scm_blk_dev *, struct scm_device *);
void scm_blk_dev_cleanup(struct scm_blk_dev *);
void scm_blk_irq(struct scm_device *, void *, int);

void scm_request_finish(struct scm_request *);
void scm_request_requeue(struct scm_request *);

int scm_drv_init(void);
void scm_drv_cleanup(void);

#ifdef CONFIG_SCM_BLOCK_CLUSTER_WRITE
void __scm_free_rq_cluster(struct scm_request *);
int __scm_alloc_rq_cluster(struct scm_request *);
void scm_request_cluster_init(struct scm_request *);
bool scm_reserve_cluster(struct scm_request *);
void scm_release_cluster(struct scm_request *);
void scm_blk_dev_cluster_setup(struct scm_blk_dev *);
bool scm_need_cluster_request(struct scm_request *);
void scm_initiate_cluster_request(struct scm_request *);
void scm_cluster_request_irq(struct scm_request *);
bool scm_test_cluster_request(struct scm_request *);
bool scm_cluster_size_valid(void);
#else /* CONFIG_SCM_BLOCK_CLUSTER_WRITE */
static inline void __scm_free_rq_cluster(struct scm_request *scmrq) {}
static inline int __scm_alloc_rq_cluster(struct scm_request *scmrq)
{
	return 0;
}
static inline void scm_request_cluster_init(struct scm_request *scmrq) {}
static inline bool scm_reserve_cluster(struct scm_request *scmrq)
{
	return true;
}
static inline void scm_release_cluster(struct scm_request *scmrq) {}
static inline void scm_blk_dev_cluster_setup(struct scm_blk_dev *bdev) {}
static inline bool scm_need_cluster_request(struct scm_request *scmrq)
{
	return false;
}
static inline void scm_initiate_cluster_request(struct scm_request *scmrq) {}
static inline void scm_cluster_request_irq(struct scm_request *scmrq) {}
static inline bool scm_test_cluster_request(struct scm_request *scmrq)
{
	return false;
}
static inline bool scm_cluster_size_valid(void)
{
	return true;
}
#endif /* CONFIG_SCM_BLOCK_CLUSTER_WRITE */

extern debug_info_t *scm_debug;

#define SCM_LOG(imp, txt) do {					\
		debug_text_event(scm_debug, imp, txt);		\
	} while (0)

static inline void SCM_LOG_HEX(int level, void *data, int length)
{
	if (level > scm_debug->level)
		return;
	while (length > 0) {
		debug_event(scm_debug, level, data, length);
		length -= scm_debug->buf_size;
		data += scm_debug->buf_size;
	}
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
