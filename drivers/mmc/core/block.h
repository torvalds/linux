#ifndef _MMC_CORE_BLOCK_H
#define _MMC_CORE_BLOCK_H

struct mmc_queue;
struct request;

void mmc_blk_issue_rq(struct mmc_queue *mq, struct request *req);

#endif
