/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_H_
#define _FBNIC_H_

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "fbnic_csr.h"
#include "fbnic_fw.h"
#include "fbnic_mac.h"

struct fbnic_dev {
	struct device *dev;
	struct net_device *netdev;

	u32 __iomem *uc_addr0;
	u32 __iomem *uc_addr4;
	const struct fbnic_mac *mac;
	unsigned int fw_msix_vector;
	unsigned short num_irqs;

	struct delayed_work service_task;

	struct fbnic_fw_mbx mbx[FBNIC_IPC_MBX_INDICES];
	/* Lock protecting Tx Mailbox queue to prevent possible races */
	spinlock_t fw_tx_lock;

	u64 dsn;
	u32 mps;
	u32 readrq;

	/* Number of TCQs/RCQs available on hardware */
	u16 max_num_queues;
};

/* Reserve entry 0 in the MSI-X "others" array until we have filled all
 * 32 of the possible interrupt slots. By doing this we can avoid any
 * potential conflicts should we need to enable one of the debug interrupt
 * causes later.
 */
enum {
	FBNIC_FW_MSIX_ENTRY,
	FBNIC_NON_NAPI_VECTORS
};

static inline bool fbnic_present(struct fbnic_dev *fbd)
{
	return !!READ_ONCE(fbd->uc_addr0);
}

static inline void fbnic_wr32(struct fbnic_dev *fbd, u32 reg, u32 val)
{
	u32 __iomem *csr = READ_ONCE(fbd->uc_addr0);

	if (csr)
		writel(val, csr + reg);
}

u32 fbnic_rd32(struct fbnic_dev *fbd, u32 reg);

static inline void fbnic_wrfl(struct fbnic_dev *fbd)
{
	fbnic_rd32(fbd, FBNIC_MASTER_SPARE_0);
}

static inline void
fbnic_rmw32(struct fbnic_dev *fbd, u32 reg, u32 mask, u32 val)
{
	u32 v;

	v = fbnic_rd32(fbd, reg);
	v &= ~mask;
	v |= val;
	fbnic_wr32(fbd, reg, v);
}

#define wr32(_f, _r, _v)	fbnic_wr32(_f, _r, _v)
#define rd32(_f, _r)		fbnic_rd32(_f, _r)
#define wrfl(_f)		fbnic_wrfl(_f)

bool fbnic_fw_present(struct fbnic_dev *fbd);
u32 fbnic_fw_rd32(struct fbnic_dev *fbd, u32 reg);
void fbnic_fw_wr32(struct fbnic_dev *fbd, u32 reg, u32 val);

#define fw_rd32(_f, _r)		fbnic_fw_rd32(_f, _r)
#define fw_wr32(_f, _r, _v)	fbnic_fw_wr32(_f, _r, _v)
#define fw_wrfl(_f)		fbnic_fw_rd32(_f, FBNIC_FW_ZERO_REG)

static inline bool fbnic_init_failure(struct fbnic_dev *fbd)
{
	return !fbd->netdev;
}

extern char fbnic_driver_name[];

void fbnic_devlink_free(struct fbnic_dev *fbd);
struct fbnic_dev *fbnic_devlink_alloc(struct pci_dev *pdev);
void fbnic_devlink_register(struct fbnic_dev *fbd);
void fbnic_devlink_unregister(struct fbnic_dev *fbd);

int fbnic_fw_enable_mbx(struct fbnic_dev *fbd);
void fbnic_fw_disable_mbx(struct fbnic_dev *fbd);

int fbnic_request_irq(struct fbnic_dev *dev, int nr, irq_handler_t handler,
		      unsigned long flags, const char *name, void *data);
void fbnic_free_irq(struct fbnic_dev *dev, int nr, void *data);
void fbnic_free_irqs(struct fbnic_dev *fbd);
int fbnic_alloc_irqs(struct fbnic_dev *fbd);

enum fbnic_boards {
	fbnic_board_asic
};

struct fbnic_info {
	unsigned int max_num_queues;
	unsigned int bar_mask;
};

#endif /* _FBNIC_H_ */
