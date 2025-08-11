/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 AIROHA Inc
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 */

#define NPU_NUM_CORES		8

enum airoha_npu_wlan_set_cmd {
	WLAN_FUNC_SET_WAIT_PCIE_ADDR,
	WLAN_FUNC_SET_WAIT_DESC,
	WLAN_FUNC_SET_WAIT_NPU_INIT_DONE,
	WLAN_FUNC_SET_WAIT_TRAN_TO_CPU,
	WLAN_FUNC_SET_WAIT_BA_WIN_SIZE,
	WLAN_FUNC_SET_WAIT_DRIVER_MODEL,
	WLAN_FUNC_SET_WAIT_DEL_STA,
	WLAN_FUNC_SET_WAIT_DRAM_BA_NODE_ADDR,
	WLAN_FUNC_SET_WAIT_PKT_BUF_ADDR,
	WLAN_FUNC_SET_WAIT_IS_TEST_NOBA,
	WLAN_FUNC_SET_WAIT_FLUSHONE_TIMEOUT,
	WLAN_FUNC_SET_WAIT_FLUSHALL_TIMEOUT,
	WLAN_FUNC_SET_WAIT_IS_FORCE_TO_CPU,
	WLAN_FUNC_SET_WAIT_PCIE_STATE,
	WLAN_FUNC_SET_WAIT_PCIE_PORT_TYPE,
	WLAN_FUNC_SET_WAIT_ERROR_RETRY_TIMES,
	WLAN_FUNC_SET_WAIT_BAR_INFO,
	WLAN_FUNC_SET_WAIT_FAST_FLAG,
	WLAN_FUNC_SET_WAIT_NPU_BAND0_ONCPU,
	WLAN_FUNC_SET_WAIT_TX_RING_PCIE_ADDR,
	WLAN_FUNC_SET_WAIT_TX_DESC_HW_BASE,
	WLAN_FUNC_SET_WAIT_TX_BUF_SPACE_HW_BASE,
	WLAN_FUNC_SET_WAIT_RX_RING_FOR_TXDONE_HW_BASE,
	WLAN_FUNC_SET_WAIT_TX_PKT_BUF_ADDR,
	WLAN_FUNC_SET_WAIT_INODE_TXRX_REG_ADDR,
	WLAN_FUNC_SET_WAIT_INODE_DEBUG_FLAG,
	WLAN_FUNC_SET_WAIT_INODE_HW_CFG_INFO,
	WLAN_FUNC_SET_WAIT_INODE_STOP_ACTION,
	WLAN_FUNC_SET_WAIT_INODE_PCIE_SWAP,
	WLAN_FUNC_SET_WAIT_RATELIMIT_CTRL,
	WLAN_FUNC_SET_WAIT_HWNAT_INIT,
	WLAN_FUNC_SET_WAIT_ARHT_CHIP_INFO,
	WLAN_FUNC_SET_WAIT_TX_BUF_CHECK_ADDR,
	WLAN_FUNC_SET_WAIT_TOKEN_ID_SIZE,
};

enum airoha_npu_wlan_get_cmd {
	WLAN_FUNC_GET_WAIT_NPU_INFO,
	WLAN_FUNC_GET_WAIT_LAST_RATE,
	WLAN_FUNC_GET_WAIT_COUNTER,
	WLAN_FUNC_GET_WAIT_DBG_COUNTER,
	WLAN_FUNC_GET_WAIT_RXDESC_BASE,
	WLAN_FUNC_GET_WAIT_WCID_DBG_COUNTER,
	WLAN_FUNC_GET_WAIT_DMA_ADDR,
	WLAN_FUNC_GET_WAIT_RING_SIZE,
	WLAN_FUNC_GET_WAIT_NPU_SUPPORT_MAP,
	WLAN_FUNC_GET_WAIT_MDC_LOCK_ADDRESS,
	WLAN_FUNC_GET_WAIT_NPU_VERSION,
};

struct airoha_npu {
	struct device *dev;
	struct regmap *regmap;

	struct airoha_npu_core {
		struct airoha_npu *npu;
		/* protect concurrent npu memory accesses */
		spinlock_t lock;
		struct work_struct wdt_work;
	} cores[NPU_NUM_CORES];

	struct airoha_foe_stats __iomem *stats;

	struct {
		int (*ppe_init)(struct airoha_npu *npu);
		int (*ppe_deinit)(struct airoha_npu *npu);
		int (*ppe_flush_sram_entries)(struct airoha_npu *npu,
					      dma_addr_t foe_addr,
					      int sram_num_entries);
		int (*ppe_foe_commit_entry)(struct airoha_npu *npu,
					    dma_addr_t foe_addr,
					    u32 entry_size, u32 hash,
					    bool ppe2);
		int (*wlan_init_reserved_memory)(struct airoha_npu *npu);
		int (*wlan_send_msg)(struct airoha_npu *npu, int ifindex,
				     enum airoha_npu_wlan_set_cmd func_id,
				     void *data, int data_len, gfp_t gfp);
		int (*wlan_get_msg)(struct airoha_npu *npu, int ifindex,
				    enum airoha_npu_wlan_get_cmd func_id,
				    void *data, int data_len, gfp_t gfp);
		u32 (*wlan_get_queue_addr)(struct airoha_npu *npu, int qid,
					   bool xmit);
		void (*wlan_set_irq_status)(struct airoha_npu *npu, u32 val);
		u32 (*wlan_get_irq_status)(struct airoha_npu *npu, int q);
		void (*wlan_enable_irq)(struct airoha_npu *npu, int q);
		void (*wlan_disable_irq)(struct airoha_npu *npu, int q);
	} ops;
};

struct airoha_npu *airoha_npu_get(struct device *dev, dma_addr_t *stats_addr);
void airoha_npu_put(struct airoha_npu *npu);
