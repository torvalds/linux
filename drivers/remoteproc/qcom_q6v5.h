/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __QCOM_Q6V5_H__
#define __QCOM_Q6V5_H__

#include <linux/kernel.h>
#include <linux/completion.h>

struct rproc;
struct qcom_smem_state;

struct qcom_q6v5 {
	struct device *dev;
	struct rproc *rproc;

	struct qcom_smem_state *state;
	unsigned stop_bit;

	int wdog_irq;
	int fatal_irq;
	int ready_irq;
	int handover_irq;
	int stop_irq;

	bool handover_issued;

	struct completion start_done;
	struct completion stop_done;

	int crash_reason;

	bool running;

	void (*handover)(struct qcom_q6v5 *q6v5);
};

int qcom_q6v5_init(struct qcom_q6v5 *q6v5, struct platform_device *pdev,
		   struct rproc *rproc, int crash_reason,
		   void (*handover)(struct qcom_q6v5 *q6v5));

int qcom_q6v5_prepare(struct qcom_q6v5 *q6v5);
int qcom_q6v5_unprepare(struct qcom_q6v5 *q6v5);
int qcom_q6v5_request_stop(struct qcom_q6v5 *q6v5);
int qcom_q6v5_wait_for_start(struct qcom_q6v5 *q6v5, int timeout);
unsigned long qcom_q6v5_panic(struct qcom_q6v5 *q6v5);

#endif
