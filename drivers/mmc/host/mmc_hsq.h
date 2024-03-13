/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MMC_HSQ_H
#define LINUX_MMC_HSQ_H

struct hsq_slot {
	struct mmc_request *mrq;
};

struct mmc_hsq {
	struct mmc_host *mmc;
	struct mmc_request *mrq;
	wait_queue_head_t wait_queue;
	struct hsq_slot *slot;
	spinlock_t lock;
	struct work_struct retry_work;

	int next_tag;
	int num_slots;
	int qcnt;

	bool enabled;
	bool waiting_for_idle;
	bool recovery_halt;
};

int mmc_hsq_init(struct mmc_hsq *hsq, struct mmc_host *mmc);
void mmc_hsq_suspend(struct mmc_host *mmc);
int mmc_hsq_resume(struct mmc_host *mmc);
bool mmc_hsq_finalize_request(struct mmc_host *mmc, struct mmc_request *mrq);

#endif
