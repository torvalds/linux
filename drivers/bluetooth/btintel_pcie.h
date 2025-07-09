/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *  Bluetooth support for Intel PCIe devices
 *
 *  Copyright (C) 2024  Intel Corporation
 */

/* Control and Status Register(BTINTEL_PCIE_CSR) */
#define BTINTEL_PCIE_CSR_BASE			(0x000)
#define BTINTEL_PCIE_CSR_FUNC_CTRL_REG		(BTINTEL_PCIE_CSR_BASE + 0x024)
#define BTINTEL_PCIE_CSR_HW_REV_REG		(BTINTEL_PCIE_CSR_BASE + 0x028)
#define BTINTEL_PCIE_CSR_RF_ID_REG		(BTINTEL_PCIE_CSR_BASE + 0x09C)
#define BTINTEL_PCIE_CSR_BOOT_STAGE_REG		(BTINTEL_PCIE_CSR_BASE + 0x108)
#define BTINTEL_PCIE_CSR_IPC_CONTROL_REG	(BTINTEL_PCIE_CSR_BASE + 0x10C)
#define BTINTEL_PCIE_CSR_IPC_STATUS_REG		(BTINTEL_PCIE_CSR_BASE + 0x110)
#define BTINTEL_PCIE_CSR_IPC_SLEEP_CTL_REG	(BTINTEL_PCIE_CSR_BASE + 0x114)
#define BTINTEL_PCIE_CSR_CI_ADDR_LSB_REG	(BTINTEL_PCIE_CSR_BASE + 0x118)
#define BTINTEL_PCIE_CSR_CI_ADDR_MSB_REG	(BTINTEL_PCIE_CSR_BASE + 0x11C)
#define BTINTEL_PCIE_CSR_IMG_RESPONSE_REG	(BTINTEL_PCIE_CSR_BASE + 0x12C)
#define BTINTEL_PCIE_CSR_MBOX_1_REG		(BTINTEL_PCIE_CSR_BASE + 0x170)
#define BTINTEL_PCIE_CSR_MBOX_2_REG		(BTINTEL_PCIE_CSR_BASE + 0x174)
#define BTINTEL_PCIE_CSR_MBOX_3_REG		(BTINTEL_PCIE_CSR_BASE + 0x178)
#define BTINTEL_PCIE_CSR_MBOX_4_REG		(BTINTEL_PCIE_CSR_BASE + 0x17C)
#define BTINTEL_PCIE_CSR_MBOX_STATUS_REG	(BTINTEL_PCIE_CSR_BASE + 0x180)
#define BTINTEL_PCIE_PRPH_DEV_ADDR_REG		(BTINTEL_PCIE_CSR_BASE + 0x440)
#define BTINTEL_PCIE_PRPH_DEV_RD_REG		(BTINTEL_PCIE_CSR_BASE + 0x458)
#define BTINTEL_PCIE_CSR_HBUS_TARG_WRPTR	(BTINTEL_PCIE_CSR_BASE + 0x460)

/* BTINTEL_PCIE_CSR Function Control Register */
#define BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_ENA		(BIT(0))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_INIT		(BIT(6))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_INIT		(BIT(7))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_ACCESS_STS	(BIT(20))

#define BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_ACCESS_REQ	(BIT(21))
/* Stop MAC Access disconnection request */
#define BTINTEL_PCIE_CSR_FUNC_CTRL_STOP_MAC_ACCESS_DIS	(BIT(22))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_XTAL_CLK_REQ		(BIT(23))

#define BTINTEL_PCIE_CSR_FUNC_CTRL_BUS_MASTER_STS	(BIT(28))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_BUS_MASTER_DISCON	(BIT(29))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_SW_RESET		(BIT(31))

/* Value for BTINTEL_PCIE_CSR_BOOT_STAGE register */
#define BTINTEL_PCIE_CSR_BOOT_STAGE_ROM		(BIT(0))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_IML		(BIT(1))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_OPFW		(BIT(2))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_ROM_LOCKDOWN	(BIT(10))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_IML_LOCKDOWN	(BIT(11))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_DEVICE_ERR		(BIT(12))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_ABORT_HANDLER	(BIT(13))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_DEVICE_HALTED	(BIT(14))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_MAC_ACCESS_ON	(BIT(16))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_ALIVE		(BIT(23))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_D3_STATE_READY	(BIT(24))

/* Registers for MSI-X */
#define BTINTEL_PCIE_CSR_MSIX_BASE		(0x2000)
#define BTINTEL_PCIE_CSR_MSIX_FH_INT_CAUSES	(BTINTEL_PCIE_CSR_MSIX_BASE + 0x0800)
#define BTINTEL_PCIE_CSR_MSIX_FH_INT_MASK	(BTINTEL_PCIE_CSR_MSIX_BASE + 0x0804)
#define BTINTEL_PCIE_CSR_MSIX_HW_INT_CAUSES	(BTINTEL_PCIE_CSR_MSIX_BASE + 0x0808)
#define BTINTEL_PCIE_CSR_MSIX_HW_INT_MASK	(BTINTEL_PCIE_CSR_MSIX_BASE + 0x080C)
#define BTINTEL_PCIE_CSR_MSIX_AUTOMASK_ST	(BTINTEL_PCIE_CSR_MSIX_BASE + 0x0810)
#define BTINTEL_PCIE_CSR_MSIX_AUTOMASK_EN	(BTINTEL_PCIE_CSR_MSIX_BASE + 0x0814)
#define BTINTEL_PCIE_CSR_MSIX_IVAR_BASE		(BTINTEL_PCIE_CSR_MSIX_BASE + 0x0880)
#define BTINTEL_PCIE_CSR_MSIX_IVAR(cause)	(BTINTEL_PCIE_CSR_MSIX_IVAR_BASE + (cause))

/* IOSF Debug Register */
#define BTINTEL_PCIE_DBGC_BASE_ADDR			(0xf3800300)
#define BTINTEL_PCIE_DBGC_CUR_DBGBUFF_STATUS		(BTINTEL_PCIE_DBGC_BASE_ADDR + 0x1C)
#define BTINTEL_PCIE_DBGC_DBGBUFF_WRAP_ARND		(BTINTEL_PCIE_DBGC_BASE_ADDR + 0x2C)

#define BTINTEL_PCIE_DBG_IDX_BIT_MASK		0x0F
#define BTINTEL_PCIE_DBGC_DBG_BUF_IDX(data)	(((data) >> 24) & BTINTEL_PCIE_DBG_IDX_BIT_MASK)
#define BTINTEL_PCIE_DBG_OFFSET_BIT_MASK	0xFFFFFF

/* The DRAM buffer count, each buffer size, and
 * fragment buffer size
 */
#define BTINTEL_PCIE_DBGC_BUFFER_COUNT		16
#define BTINTEL_PCIE_DBGC_BUFFER_SIZE		(256 * 1024) /* 256 KB */

#define BTINTEL_PCIE_DBGC_FRAG_VERSION		1
#define BTINTEL_PCIE_DBGC_FRAG_BUFFER_COUNT	BTINTEL_PCIE_DBGC_BUFFER_COUNT

/* Magic number(4), version(4), size of payload length(4) */
#define BTINTEL_PCIE_DBGC_FRAG_HEADER_SIZE	12

/* Num of alloc Dbg buff (4) + (LSB(4), MSB(4), Size(4)) for each buffer */
#define BTINTEL_PCIE_DBGC_FRAG_PAYLOAD_SIZE	196

/* Causes for the FH register interrupts */
enum msix_fh_int_causes {
	BTINTEL_PCIE_MSIX_FH_INT_CAUSES_0	= BIT(0),	/* cause 0 */
	BTINTEL_PCIE_MSIX_FH_INT_CAUSES_1	= BIT(1),	/* cause 1 */
};

/* Causes for the HW register interrupts */
enum msix_hw_int_causes {
	BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP0	= BIT(0),	/* cause 32 */
	BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP1	= BIT(1),	/* cause 33 */
	BTINTEL_PCIE_MSIX_HW_INT_CAUSES_HWEXP	= BIT(3),	/* cause 35 */
};

/* PCIe device states
 * Host-Device interface is active
 * Host-Device interface is inactive(as reflected by IPC_SLEEP_CONTROL_CSR_AD)
 * Host-Device interface is inactive(as reflected by IPC_SLEEP_CONTROL_CSR_AD)
 */
enum {
	BTINTEL_PCIE_STATE_D0 = 0,
	BTINTEL_PCIE_STATE_D3_HOT = 2,
	BTINTEL_PCIE_STATE_D3_COLD = 3,
};

enum {
	BTINTEL_PCIE_CORE_HALTED,
	BTINTEL_PCIE_HWEXP_INPROGRESS,
	BTINTEL_PCIE_COREDUMP_INPROGRESS
};

enum btintel_pcie_tlv_type {
	BTINTEL_CNVI_BT,
	BTINTEL_WRITE_PTR,
	BTINTEL_WRAP_CTR,
	BTINTEL_TRIGGER_REASON,
	BTINTEL_FW_SHA,
	BTINTEL_CNVR_TOP,
	BTINTEL_CNVI_TOP,
	BTINTEL_DUMP_TIME,
	BTINTEL_FW_BUILD,
};

/* causes for the MBOX interrupts */
enum msix_mbox_int_causes {
	BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX1 = BIT(0), /* cause MBOX1 */
	BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX2 = BIT(1), /* cause MBOX2 */
	BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX3 = BIT(2), /* cause MBOX3 */
	BTINTEL_PCIE_CSR_MBOX_STATUS_MBOX4 = BIT(3), /* cause MBOX4 */
};

#define BTINTEL_PCIE_MSIX_NON_AUTO_CLEAR_CAUSE	BIT(7)

/* Minimum and Maximum number of MSI-X Vector
 * Intel Bluetooth PCIe support only 1 vector
 */
#define BTINTEL_PCIE_MSIX_VEC_MAX	1
#define BTINTEL_PCIE_MSIX_VEC_MIN	1

/* Default poll time for MAC access during init */
#define BTINTEL_DEFAULT_MAC_ACCESS_TIMEOUT_US	200000

/* Default interrupt timeout in msec */
#define BTINTEL_DEFAULT_INTR_TIMEOUT_MS	3000

/* The number of descriptors in TX queues */
#define BTINTEL_PCIE_TX_DESCS_COUNT	32

/* The number of descriptors in RX queues */
#define BTINTEL_PCIE_RX_DESCS_COUNT	64

/* Number of Queue for TX and RX
 * It indicates the index of the IA(Index Array)
 */
enum {
	BTINTEL_PCIE_TXQ_NUM = 0,
	BTINTEL_PCIE_RXQ_NUM = 1,
	BTINTEL_PCIE_NUM_QUEUES = 2,
};

/* The size of DMA buffer for TX and RX in bytes */
#define BTINTEL_PCIE_BUFFER_SIZE	4096

/* DMA allocation alignment */
#define BTINTEL_PCIE_DMA_POOL_ALIGNMENT	256

#define BTINTEL_PCIE_TX_WAIT_TIMEOUT_MS		500

/* Doorbell vector for TFD */
#define BTINTEL_PCIE_TX_DB_VEC	0

/* Doorbell vector for FRBD */
#define BTINTEL_PCIE_RX_DB_VEC	513

/* RBD buffer size mapping */
#define BTINTEL_PCIE_RBD_SIZE_4K	0x04

/*
 * Struct for Context Information (v2)
 *
 * All members are write-only for host and read-only for device.
 *
 * @version: Version of context information
 * @size: Size of context information
 * @config: Config with which host wants peripheral to execute
 *	Subset of capability register published by device
 * @addr_tr_hia: Address of TR Head Index Array
 * @addr_tr_tia: Address of TR Tail Index Array
 * @addr_cr_hia: Address of CR Head Index Array
 * @addr_cr_tia: Address of CR Tail Index Array
 * @num_tr_ia: Number of entries in TR Index Arrays
 * @num_cr_ia: Number of entries in CR Index Arrays
 * @rbd_siz: RBD Size { 0x4=4K }
 * @addr_tfdq: Address of TFD Queue(tx)
 * @addr_urbdq0: Address of URBD Queue(tx)
 * @num_tfdq: Number of TFD in TFD Queue(tx)
 * @num_urbdq0: Number of URBD in URBD Queue(tx)
 * @tfdq_db_vec: Queue number of TFD
 * @urbdq0_db_vec: Queue number of URBD
 * @addr_frbdq: Address of FRBD Queue(rx)
 * @addr_urbdq1: Address of URBD Queue(rx)
 * @num_frbdq: Number of FRBD in FRBD Queue(rx)
 * @frbdq_db_vec: Queue number of FRBD
 * @num_urbdq1: Number of URBD in URBD Queue(rx)
 * @urbdq_db_vec: Queue number of URBDQ1
 * @tr_msi_vec: Transfer Ring MSI-X Vector
 * @cr_msi_vec: Completion Ring MSI-X Vector
 * @dbgc_addr: DBGC first fragment address
 * @dbgc_size: DBGC buffer size
 * @early_enable: Enarly debug enable
 * @dbg_output_mode: Debug output mode
 *	Bit[4] DBGC O/P { 0=SRAM, 1=DRAM(not relevant for NPK) }
 *	Bit[5] DBGC I/P { 0=BDBG, 1=DBGI }
 *	Bits[6:7] DBGI O/P(relevant if bit[5] = 1)
 *	 0=BT DBGC, 1=WiFi DBGC, 2=NPK }
 * @dbg_preset: Debug preset
 * @ext_addr: Address of context information extension
 * @ext_size: Size of context information part
 *
 * Total 38 DWords
 */
struct ctx_info {
	u16	version;
	u16	size;
	u32	config;
	u32	reserved_dw02;
	u32	reserved_dw03;
	u64	addr_tr_hia;
	u64	addr_tr_tia;
	u64	addr_cr_hia;
	u64	addr_cr_tia;
	u16	num_tr_ia;
	u16	num_cr_ia;
	u32	rbd_size:4,
		reserved_dw13:28;
	u64	addr_tfdq;
	u64	addr_urbdq0;
	u16	num_tfdq;
	u16	num_urbdq0;
	u16	tfdq_db_vec;
	u16	urbdq0_db_vec;
	u64	addr_frbdq;
	u64	addr_urbdq1;
	u16	num_frbdq;
	u16	frbdq_db_vec;
	u16	num_urbdq1;
	u16	urbdq_db_vec;
	u16	tr_msi_vec;
	u16	cr_msi_vec;
	u32	reserved_dw27;
	u64	dbgc_addr;
	u32	dbgc_size;
	u32	early_enable:1,
		reserved_dw31:3,
		dbg_output_mode:4,
		dbg_preset:8,
		reserved2_dw31:16;
	u64	ext_addr;
	u32	ext_size;
	u32	test_param;
	u32	reserved_dw36;
	u32	reserved_dw37;
} __packed;

/* Transfer Descriptor for TX
 * @type: Not in use. Set to 0x0
 * @size: Size of data in the buffer
 * @addr: DMA Address of buffer
 */
struct tfd {
	u8	type;
	u16	size;
	u8	reserved;
	u64	addr;
	u32	reserved1;
} __packed;

/* URB Descriptor for TX
 * @tfd_index: Index of TFD in TFDQ + 1
 * @num_txq: Queue index of TFD Queue
 * @cmpl_count: Completion count. Always 0x01
 * @immediate_cmpl: Immediate completion flag: Always 0x01
 */
struct urbd0 {
	u32	tfd_index:16,
		num_txq:8,
		cmpl_count:4,
		reserved:3,
		immediate_cmpl:1;
} __packed;

/* FRB Descriptor for RX
 * @tag: RX buffer tag (index of RX buffer queue)
 * @addr: Address of buffer
 */
struct frbd {
	u32	tag:16,
		reserved:16;
	u32	reserved2;
	u64	addr;
} __packed;

/* URB Descriptor for RX
 * @frbd_tag: Tag from FRBD
 * @status: Status
 */
struct urbd1 {
	u32	frbd_tag:16,
		status:1,
		reserved:14,
		fixed:1;
} __packed;

/* RFH header in RX packet
 * @packet_len: Length of the data in the buffer
 * @rxq: RX Queue number
 * @cmd_id: Command ID. Not in Use
 */
struct rfh_hdr {
	u64	packet_len:16,
		rxq:6,
		reserved:10,
		cmd_id:16,
		reserved1:16;
} __packed;

/* Internal data buffer
 * @data: pointer to the data buffer
 * @p_addr: physical address of data buffer
 */
struct data_buf {
	u8		*data;
	dma_addr_t	data_p_addr;
};

/* Index Array */
struct ia {
	dma_addr_t	tr_hia_p_addr;
	u16		*tr_hia;
	dma_addr_t	tr_tia_p_addr;
	u16		*tr_tia;
	dma_addr_t	cr_hia_p_addr;
	u16		*cr_hia;
	dma_addr_t	cr_tia_p_addr;
	u16		*cr_tia;
};

/* Structure for TX Queue
 * @count: Number of descriptors
 * @tfds: Array of TFD
 * @urbd0s: Array of URBD0
 * @buf: Array of data_buf structure
 */
struct txq {
	u16		count;

	dma_addr_t	tfds_p_addr;
	struct tfd	*tfds;

	dma_addr_t	urbd0s_p_addr;
	struct urbd0	*urbd0s;

	dma_addr_t	buf_p_addr;
	void		*buf_v_addr;
	struct data_buf	*bufs;
};

/* Structure for RX Queue
 * @count: Number of descriptors
 * @frbds: Array of FRBD
 * @urbd1s: Array of URBD1
 * @buf: Array of data_buf structure
 */
struct rxq {
	u16		count;

	dma_addr_t	frbds_p_addr;
	struct frbd	*frbds;

	dma_addr_t	urbd1s_p_addr;
	struct urbd1	*urbd1s;

	dma_addr_t	buf_p_addr;
	void		*buf_v_addr;
	struct data_buf	*bufs;
};

/* Structure for DRAM Buffer
 * @count: Number of descriptors
 * @buf: Array of data_buf structure
 */
struct btintel_pcie_dbgc {
	u16		count;

	void		*frag_v_addr;
	dma_addr_t	frag_p_addr;
	u16		frag_size;

	dma_addr_t	buf_p_addr;
	void		*buf_v_addr;
	struct data_buf *bufs;
};

struct btintel_pcie_dump_header {
	const char	*driver_name;
	u32		cnvi_top;
	u32		cnvr_top;
	u16		fw_timestamp;
	u8		fw_build_type;
	u32		fw_build_num;
	u32		fw_git_sha1;
	u32		cnvi_bt;
	u32		write_ptr;
	u32		wrap_ctr;
	u16		trigger_reason;
	int		state;
};

/* struct btintel_pcie_data
 * @pdev: pci device
 * @hdev: hdev device
 * @flags: driver state
 * @irq_lock: spinlock for MSI-X
 * @hci_rx_lock: spinlock for HCI RX flow
 * @base_addr: pci base address (from BAR)
 * @msix_entries: array of MSI-X entries
 * @msix_enabled: true if MSI-X is enabled;
 * @alloc_vecs: number of interrupt vectors allocated
 * @def_irq: default irq for all causes
 * @fh_init_mask: initial unmasked rxq causes
 * @hw_init_mask: initial unmaksed hw causes
 * @boot_stage_cache: cached value of boot stage register
 * @img_resp_cache: cached value of image response register
 * @cnvi: CNVi register value
 * @cnvr: CNVr register value
 * @gp0_received: condition for gp0 interrupt
 * @gp0_wait_q: wait_q for gp0 interrupt
 * @tx_wait_done: condition for tx interrupt
 * @tx_wait_q: wait_q for tx interrupt
 * @workqueue: workqueue for RX work
 * @rx_skb_q: SKB queue for RX packet
 * @rx_work: RX work struct to process the RX packet in @rx_skb_q
 * @dma_pool: DMA pool for descriptors, index array and ci
 * @dma_p_addr: DMA address for pool
 * @dma_v_addr: address of pool
 * @ci_p_addr: DMA address for CI struct
 * @ci: CI struct
 * @ia: Index Array struct
 * @txq: TX Queue struct
 * @rxq: RX Queue struct
 * @alive_intr_ctxt: Alive interrupt context
 */
struct btintel_pcie_data {
	struct pci_dev	*pdev;
	struct hci_dev	*hdev;

	unsigned long	flags;
	/* lock used in MSI-X interrupt */
	spinlock_t	irq_lock;
	/* lock to serialize rx events */
	spinlock_t	hci_rx_lock;

	void __iomem	*base_addr;

	struct msix_entry	msix_entries[BTINTEL_PCIE_MSIX_VEC_MAX];
	bool	msix_enabled;
	u32	alloc_vecs;
	u32	def_irq;

	u32	fh_init_mask;
	u32	hw_init_mask;

	u32	boot_stage_cache;
	u32	img_resp_cache;

	u32	cnvi;
	u32	cnvr;

	bool	gp0_received;
	wait_queue_head_t	gp0_wait_q;

	bool	tx_wait_done;
	wait_queue_head_t	tx_wait_q;

	struct workqueue_struct	*workqueue;
	struct sk_buff_head	rx_skb_q;
	struct work_struct	rx_work;

	struct dma_pool	*dma_pool;
	dma_addr_t	dma_p_addr;
	void		*dma_v_addr;

	dma_addr_t	ci_p_addr;
	struct ctx_info	*ci;
	struct ia	ia;
	struct txq	txq;
	struct rxq	rxq;
	u32	alive_intr_ctxt;
	struct btintel_pcie_dbgc	dbgc;
	struct btintel_pcie_dump_header dmp_hdr;
};

static inline u32 btintel_pcie_rd_reg32(struct btintel_pcie_data *data,
					u32 offset)
{
	return ioread32(data->base_addr + offset);
}

static inline void btintel_pcie_wr_reg8(struct btintel_pcie_data *data,
					u32 offset, u8 val)
{
	iowrite8(val, data->base_addr + offset);
}

static inline void btintel_pcie_wr_reg32(struct btintel_pcie_data *data,
					 u32 offset, u32 val)
{
	iowrite32(val, data->base_addr + offset);
}

static inline void btintel_pcie_set_reg_bits(struct btintel_pcie_data *data,
					     u32 offset, u32 bits)
{
	u32 r;

	r = ioread32(data->base_addr + offset);
	r |= bits;
	iowrite32(r, data->base_addr + offset);
}

static inline void btintel_pcie_clr_reg_bits(struct btintel_pcie_data *data,
					     u32 offset, u32 bits)
{
	u32 r;

	r = ioread32(data->base_addr + offset);
	r &= ~bits;
	iowrite32(r, data->base_addr + offset);
}

static inline u32 btintel_pcie_rd_dev_mem(struct btintel_pcie_data *data,
					  u32 addr)
{
	btintel_pcie_wr_reg32(data, BTINTEL_PCIE_PRPH_DEV_ADDR_REG, addr);
	return btintel_pcie_rd_reg32(data, BTINTEL_PCIE_PRPH_DEV_RD_REG);
}

