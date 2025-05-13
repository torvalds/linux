/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_H_
#define _FBNIC_H_

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "fbnic_csr.h"
#include "fbnic_fw.h"
#include "fbnic_hw_stats.h"
#include "fbnic_mac.h"
#include "fbnic_rpc.h"

struct fbnic_napi_vector;

#define FBNIC_MAX_NAPI_VECTORS		128u

struct fbnic_dev {
	struct device *dev;
	struct net_device *netdev;
	struct dentry *dbg_fbd;
	struct device *hwmon;

	u32 __iomem *uc_addr0;
	u32 __iomem *uc_addr4;
	const struct fbnic_mac *mac;
	unsigned int fw_msix_vector;
	unsigned int pcs_msix_vector;
	unsigned short num_irqs;

	struct {
		u8 users;
		char name[IFNAMSIZ + 9];
	} napi_irq[FBNIC_MAX_NAPI_VECTORS];

	struct delayed_work service_task;

	struct fbnic_fw_mbx mbx[FBNIC_IPC_MBX_INDICES];
	struct fbnic_fw_cap fw_cap;
	struct fbnic_fw_completion *cmpl_data;
	/* Lock protecting Tx Mailbox queue to prevent possible races */
	spinlock_t fw_tx_lock;

	unsigned long last_heartbeat_request;
	unsigned long last_heartbeat_response;
	u8 fw_heartbeat_enabled;

	u64 dsn;
	u32 mps;
	u32 readrq;

	/* Local copy of the devices TCAM */
	struct fbnic_act_tcam act_tcam[FBNIC_RPC_TCAM_ACT_NUM_ENTRIES];
	struct fbnic_mac_addr mac_addr[FBNIC_RPC_TCAM_MACDA_NUM_ENTRIES];
	u8 mac_addr_boundary;
	u8 tce_tcam_last;

	/* IP TCAM */
	struct fbnic_ip_addr ip_src[FBNIC_RPC_TCAM_IP_ADDR_NUM_ENTRIES];
	struct fbnic_ip_addr ip_dst[FBNIC_RPC_TCAM_IP_ADDR_NUM_ENTRIES];
	struct fbnic_ip_addr ipo_src[FBNIC_RPC_TCAM_IP_ADDR_NUM_ENTRIES];
	struct fbnic_ip_addr ipo_dst[FBNIC_RPC_TCAM_IP_ADDR_NUM_ENTRIES];

	/* Number of TCQs/RCQs available on hardware */
	u16 max_num_queues;

	/* Lock protecting writes to @time_high, @time_offset of fbnic_netdev,
	 * and the HW time CSR machinery.
	 */
	spinlock_t time_lock;
	/* Externally accessible PTP clock, may be NULL */
	struct ptp_clock *ptp;
	struct ptp_clock_info ptp_info;
	/* Last @time_high refresh time in jiffies (to catch stalls) */
	unsigned long last_read;

	/* Local copy of hardware statistics */
	struct fbnic_hw_stats hw_stats;
};

/* Reserve entry 0 in the MSI-X "others" array until we have filled all
 * 32 of the possible interrupt slots. By doing this we can avoid any
 * potential conflicts should we need to enable one of the debug interrupt
 * causes later.
 */
enum {
	FBNIC_FW_MSIX_ENTRY,
	FBNIC_PCS_MSIX_ENTRY,
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

static inline bool fbnic_bmc_present(struct fbnic_dev *fbd)
{
	return fbd->fw_cap.bmc_present;
}

static inline bool fbnic_init_failure(struct fbnic_dev *fbd)
{
	return !fbd->netdev;
}

extern char fbnic_driver_name[];

void fbnic_devlink_free(struct fbnic_dev *fbd);
struct fbnic_dev *fbnic_devlink_alloc(struct pci_dev *pdev);
void fbnic_devlink_register(struct fbnic_dev *fbd);
void fbnic_devlink_unregister(struct fbnic_dev *fbd);

int fbnic_fw_request_mbx(struct fbnic_dev *fbd);
void fbnic_fw_free_mbx(struct fbnic_dev *fbd);

void fbnic_hwmon_register(struct fbnic_dev *fbd);
void fbnic_hwmon_unregister(struct fbnic_dev *fbd);

int fbnic_pcs_request_irq(struct fbnic_dev *fbd);
void fbnic_pcs_free_irq(struct fbnic_dev *fbd);

void fbnic_napi_name_irqs(struct fbnic_dev *fbd);
int fbnic_napi_request_irq(struct fbnic_dev *fbd,
			   struct fbnic_napi_vector *nv);
void fbnic_napi_free_irq(struct fbnic_dev *fbd,
			 struct fbnic_napi_vector *nv);
void fbnic_synchronize_irq(struct fbnic_dev *fbd, int nr);
int fbnic_request_irq(struct fbnic_dev *dev, int nr, irq_handler_t handler,
		      unsigned long flags, const char *name, void *data);
void fbnic_free_irq(struct fbnic_dev *dev, int nr, void *data);
void fbnic_free_irqs(struct fbnic_dev *fbd);
int fbnic_alloc_irqs(struct fbnic_dev *fbd);

void fbnic_get_fw_ver_commit_str(struct fbnic_dev *fbd, char *fw_version,
				 const size_t str_sz);

void fbnic_dbg_fbd_init(struct fbnic_dev *fbd);
void fbnic_dbg_fbd_exit(struct fbnic_dev *fbd);
void fbnic_dbg_init(void);
void fbnic_dbg_exit(void);

void fbnic_csr_get_regs(struct fbnic_dev *fbd, u32 *data, u32 *regs_version);
int fbnic_csr_regs_len(struct fbnic_dev *fbd);

void fbnic_config_txrx_usecs(struct fbnic_napi_vector *nv, u32 arm);
void fbnic_config_rx_frames(struct fbnic_napi_vector *nv);

enum fbnic_boards {
	fbnic_board_asic
};

struct fbnic_info {
	unsigned int max_num_queues;
	unsigned int bar_mask;
};

#endif /* _FBNIC_H_ */
