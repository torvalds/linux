/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_ACCEL_DEVICES_H_
#define ADF_ACCEL_DEVICES_H_
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/ratelimit.h>
#include <linux/types.h>
#include "adf_cfg_common.h"
#include "adf_rl.h"
#include "adf_telemetry.h"
#include "adf_pfvf_msg.h"
#include "icp_qat_hw.h"

#define ADF_DH895XCC_DEVICE_NAME "dh895xcc"
#define ADF_DH895XCCVF_DEVICE_NAME "dh895xccvf"
#define ADF_C62X_DEVICE_NAME "c6xx"
#define ADF_C62XVF_DEVICE_NAME "c6xxvf"
#define ADF_C3XXX_DEVICE_NAME "c3xxx"
#define ADF_C3XXXVF_DEVICE_NAME "c3xxxvf"
#define ADF_4XXX_DEVICE_NAME "4xxx"
#define ADF_420XX_DEVICE_NAME "420xx"
#define ADF_4XXX_PCI_DEVICE_ID 0x4940
#define ADF_4XXXIOV_PCI_DEVICE_ID 0x4941
#define ADF_401XX_PCI_DEVICE_ID 0x4942
#define ADF_401XXIOV_PCI_DEVICE_ID 0x4943
#define ADF_402XX_PCI_DEVICE_ID 0x4944
#define ADF_402XXIOV_PCI_DEVICE_ID 0x4945
#define ADF_420XX_PCI_DEVICE_ID 0x4946
#define ADF_420XXIOV_PCI_DEVICE_ID 0x4947
#define ADF_DEVICE_FUSECTL_OFFSET 0x40
#define ADF_DEVICE_LEGFUSE_OFFSET 0x4C
#define ADF_DEVICE_FUSECTL_MASK 0x80000000
#define ADF_PCI_MAX_BARS 3
#define ADF_DEVICE_NAME_LENGTH 32
#define ADF_ETR_MAX_RINGS_PER_BANK 16
#define ADF_MAX_MSIX_VECTOR_NAME 48
#define ADF_DEVICE_NAME_PREFIX "qat_"

enum adf_accel_capabilities {
	ADF_ACCEL_CAPABILITIES_NULL = 0,
	ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC = 1,
	ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC = 2,
	ADF_ACCEL_CAPABILITIES_CIPHER = 4,
	ADF_ACCEL_CAPABILITIES_AUTHENTICATION = 8,
	ADF_ACCEL_CAPABILITIES_COMPRESSION = 32,
	ADF_ACCEL_CAPABILITIES_LZS_COMPRESSION = 64,
	ADF_ACCEL_CAPABILITIES_RANDOM_NUMBER = 128
};

struct adf_bar {
	resource_size_t base_addr;
	void __iomem *virt_addr;
	resource_size_t size;
};

struct adf_irq {
	bool enabled;
	char name[ADF_MAX_MSIX_VECTOR_NAME];
};

struct adf_accel_msix {
	struct adf_irq *irqs;
	u32 num_entries;
};

struct adf_accel_pci {
	struct pci_dev *pci_dev;
	struct adf_accel_msix msix_entries;
	struct adf_bar pci_bars[ADF_PCI_MAX_BARS];
	u8 revid;
	u8 sku;
};

enum dev_state {
	DEV_DOWN = 0,
	DEV_UP
};

enum dev_sku_info {
	DEV_SKU_1 = 0,
	DEV_SKU_2,
	DEV_SKU_3,
	DEV_SKU_4,
	DEV_SKU_VF,
	DEV_SKU_UNKNOWN,
};

enum ras_errors {
	ADF_RAS_CORR,
	ADF_RAS_UNCORR,
	ADF_RAS_FATAL,
	ADF_RAS_ERRORS,
};

struct adf_error_counters {
	atomic_t counter[ADF_RAS_ERRORS];
	bool sysfs_added;
	bool enabled;
};

static inline const char *get_sku_info(enum dev_sku_info info)
{
	switch (info) {
	case DEV_SKU_1:
		return "SKU1";
	case DEV_SKU_2:
		return "SKU2";
	case DEV_SKU_3:
		return "SKU3";
	case DEV_SKU_4:
		return "SKU4";
	case DEV_SKU_VF:
		return "SKUVF";
	case DEV_SKU_UNKNOWN:
	default:
		break;
	}
	return "Unknown SKU";
}

struct adf_hw_device_class {
	const char *name;
	const enum adf_device_type type;
	u32 instances;
};

struct arb_info {
	u32 arb_cfg;
	u32 arb_offset;
	u32 wt2sam_offset;
};

struct admin_info {
	u32 admin_msg_ur;
	u32 admin_msg_lr;
	u32 mailbox_offset;
};

struct adf_hw_csr_ops {
	u64 (*build_csr_ring_base_addr)(dma_addr_t addr, u32 size);
	u32 (*read_csr_ring_head)(void __iomem *csr_base_addr, u32 bank,
				  u32 ring);
	void (*write_csr_ring_head)(void __iomem *csr_base_addr, u32 bank,
				    u32 ring, u32 value);
	u32 (*read_csr_ring_tail)(void __iomem *csr_base_addr, u32 bank,
				  u32 ring);
	void (*write_csr_ring_tail)(void __iomem *csr_base_addr, u32 bank,
				    u32 ring, u32 value);
	u32 (*read_csr_e_stat)(void __iomem *csr_base_addr, u32 bank);
	void (*write_csr_ring_config)(void __iomem *csr_base_addr, u32 bank,
				      u32 ring, u32 value);
	void (*write_csr_ring_base)(void __iomem *csr_base_addr, u32 bank,
				    u32 ring, dma_addr_t addr);
	void (*write_csr_int_flag)(void __iomem *csr_base_addr, u32 bank,
				   u32 value);
	void (*write_csr_int_srcsel)(void __iomem *csr_base_addr, u32 bank);
	void (*write_csr_int_col_en)(void __iomem *csr_base_addr, u32 bank,
				     u32 value);
	void (*write_csr_int_col_ctl)(void __iomem *csr_base_addr, u32 bank,
				      u32 value);
	void (*write_csr_int_flag_and_col)(void __iomem *csr_base_addr,
					   u32 bank, u32 value);
	void (*write_csr_ring_srv_arb_en)(void __iomem *csr_base_addr, u32 bank,
					  u32 value);
};

struct adf_cfg_device_data;
struct adf_accel_dev;
struct adf_etr_data;
struct adf_etr_ring_data;

struct adf_ras_ops {
	void (*enable_ras_errors)(struct adf_accel_dev *accel_dev);
	void (*disable_ras_errors)(struct adf_accel_dev *accel_dev);
	bool (*handle_interrupt)(struct adf_accel_dev *accel_dev,
				 bool *reset_required);
};

struct adf_pfvf_ops {
	int (*enable_comms)(struct adf_accel_dev *accel_dev);
	u32 (*get_pf2vf_offset)(u32 i);
	u32 (*get_vf2pf_offset)(u32 i);
	void (*enable_vf2pf_interrupts)(void __iomem *pmisc_addr, u32 vf_mask);
	void (*disable_all_vf2pf_interrupts)(void __iomem *pmisc_addr);
	u32 (*disable_pending_vf2pf_interrupts)(void __iomem *pmisc_addr);
	int (*send_msg)(struct adf_accel_dev *accel_dev, struct pfvf_message msg,
			u32 pfvf_offset, struct mutex *csr_lock);
	struct pfvf_message (*recv_msg)(struct adf_accel_dev *accel_dev,
					u32 pfvf_offset, u8 compat_ver);
};

struct adf_dc_ops {
	void (*build_deflate_ctx)(void *ctx);
};

struct adf_dev_err_mask {
	u32 cppagentcmdpar_mask;
	u32 parerr_ath_cph_mask;
	u32 parerr_cpr_xlt_mask;
	u32 parerr_dcpr_ucs_mask;
	u32 parerr_pke_mask;
	u32 parerr_wat_wcp_mask;
	u32 ssmfeatren_mask;
};

struct adf_hw_device_data {
	struct adf_hw_device_class *dev_class;
	u32 (*get_accel_mask)(struct adf_hw_device_data *self);
	u32 (*get_ae_mask)(struct adf_hw_device_data *self);
	u32 (*get_accel_cap)(struct adf_accel_dev *accel_dev);
	u32 (*get_sram_bar_id)(struct adf_hw_device_data *self);
	u32 (*get_misc_bar_id)(struct adf_hw_device_data *self);
	u32 (*get_etr_bar_id)(struct adf_hw_device_data *self);
	u32 (*get_num_aes)(struct adf_hw_device_data *self);
	u32 (*get_num_accels)(struct adf_hw_device_data *self);
	void (*get_arb_info)(struct arb_info *arb_csrs_info);
	void (*get_admin_info)(struct admin_info *admin_csrs_info);
	enum dev_sku_info (*get_sku)(struct adf_hw_device_data *self);
	u16 (*get_ring_to_svc_map)(struct adf_accel_dev *accel_dev);
	int (*alloc_irq)(struct adf_accel_dev *accel_dev);
	void (*free_irq)(struct adf_accel_dev *accel_dev);
	void (*enable_error_correction)(struct adf_accel_dev *accel_dev);
	int (*init_admin_comms)(struct adf_accel_dev *accel_dev);
	void (*exit_admin_comms)(struct adf_accel_dev *accel_dev);
	int (*send_admin_init)(struct adf_accel_dev *accel_dev);
	int (*start_timer)(struct adf_accel_dev *accel_dev);
	void (*stop_timer)(struct adf_accel_dev *accel_dev);
	void (*check_hb_ctrs)(struct adf_accel_dev *accel_dev);
	uint32_t (*get_hb_clock)(struct adf_hw_device_data *self);
	int (*measure_clock)(struct adf_accel_dev *accel_dev);
	int (*init_arb)(struct adf_accel_dev *accel_dev);
	void (*exit_arb)(struct adf_accel_dev *accel_dev);
	const u32 *(*get_arb_mapping)(struct adf_accel_dev *accel_dev);
	int (*init_device)(struct adf_accel_dev *accel_dev);
	int (*enable_pm)(struct adf_accel_dev *accel_dev);
	bool (*handle_pm_interrupt)(struct adf_accel_dev *accel_dev);
	void (*disable_iov)(struct adf_accel_dev *accel_dev);
	void (*configure_iov_threads)(struct adf_accel_dev *accel_dev,
				      bool enable);
	void (*enable_ints)(struct adf_accel_dev *accel_dev);
	void (*set_ssm_wdtimer)(struct adf_accel_dev *accel_dev);
	int (*ring_pair_reset)(struct adf_accel_dev *accel_dev, u32 bank_nr);
	void (*reset_device)(struct adf_accel_dev *accel_dev);
	void (*set_msix_rttable)(struct adf_accel_dev *accel_dev);
	const char *(*uof_get_name)(struct adf_accel_dev *accel_dev, u32 obj_num);
	u32 (*uof_get_num_objs)(struct adf_accel_dev *accel_dev);
	u32 (*uof_get_ae_mask)(struct adf_accel_dev *accel_dev, u32 obj_num);
	int (*get_rp_group)(struct adf_accel_dev *accel_dev, u32 ae_mask);
	u32 (*get_ena_thd_mask)(struct adf_accel_dev *accel_dev, u32 obj_num);
	int (*dev_config)(struct adf_accel_dev *accel_dev);
	struct adf_pfvf_ops pfvf_ops;
	struct adf_hw_csr_ops csr_ops;
	struct adf_dc_ops dc_ops;
	struct adf_ras_ops ras_ops;
	struct adf_dev_err_mask dev_err_mask;
	struct adf_rl_hw_data rl_data;
	struct adf_tl_hw_data tl_data;
	const char *fw_name;
	const char *fw_mmp_name;
	u32 fuses;
	u32 straps;
	u32 accel_capabilities_mask;
	u32 extended_dc_capabilities;
	u16 fw_capabilities;
	u32 clock_frequency;
	u32 instance_id;
	u16 accel_mask;
	u32 ae_mask;
	u32 admin_ae_mask;
	u16 tx_rings_mask;
	u16 ring_to_svc_map;
	u32 thd_to_arb_map[ICP_QAT_HW_AE_DELIMITER];
	u8 tx_rx_gap;
	u8 num_banks;
	u16 num_banks_per_vf;
	u8 num_rings_per_bank;
	u8 num_accel;
	u8 num_logical_accel;
	u8 num_engines;
	u32 num_hb_ctrs;
	u8 num_rps;
};

/* CSR write macro */
#define ADF_CSR_WR(csr_base, csr_offset, val) \
	__raw_writel(val, csr_base + csr_offset)

/* CSR read macro */
#define ADF_CSR_RD(csr_base, csr_offset) __raw_readl(csr_base + csr_offset)

#define ADF_CFG_NUM_SERVICES	4
#define ADF_SRV_TYPE_BIT_LEN	3
#define ADF_SRV_TYPE_MASK	0x7
#define ADF_AE_ADMIN_THREAD	7
#define ADF_NUM_THREADS_PER_AE	8
#define ADF_NUM_PKE_STRAND	2
#define ADF_AE_STRAND0_THREAD	8
#define ADF_AE_STRAND1_THREAD	9

#define GET_DEV(accel_dev) ((accel_dev)->accel_pci_dev.pci_dev->dev)
#define GET_BARS(accel_dev) ((accel_dev)->accel_pci_dev.pci_bars)
#define GET_HW_DATA(accel_dev) (accel_dev->hw_device)
#define GET_MAX_BANKS(accel_dev) (GET_HW_DATA(accel_dev)->num_banks)
#define GET_NUM_RINGS_PER_BANK(accel_dev) \
	GET_HW_DATA(accel_dev)->num_rings_per_bank
#define GET_SRV_TYPE(accel_dev, idx) \
	(((GET_HW_DATA(accel_dev)->ring_to_svc_map) >> (ADF_SRV_TYPE_BIT_LEN * (idx))) \
	& ADF_SRV_TYPE_MASK)
#define GET_ERR_MASK(accel_dev) (&GET_HW_DATA(accel_dev)->dev_err_mask)
#define GET_MAX_ACCELENGINES(accel_dev) (GET_HW_DATA(accel_dev)->num_engines)
#define GET_CSR_OPS(accel_dev) (&(accel_dev)->hw_device->csr_ops)
#define GET_PFVF_OPS(accel_dev) (&(accel_dev)->hw_device->pfvf_ops)
#define GET_DC_OPS(accel_dev) (&(accel_dev)->hw_device->dc_ops)
#define GET_TL_DATA(accel_dev) GET_HW_DATA(accel_dev)->tl_data
#define accel_to_pci_dev(accel_ptr) accel_ptr->accel_pci_dev.pci_dev

struct adf_admin_comms;
struct icp_qat_fw_loader_handle;
struct adf_fw_loader_data {
	struct icp_qat_fw_loader_handle *fw_loader;
	const struct firmware *uof_fw;
	const struct firmware *mmp_fw;
};

struct adf_accel_vf_info {
	struct adf_accel_dev *accel_dev;
	struct mutex pf2vf_lock; /* protect CSR access for PF2VF messages */
	struct ratelimit_state vf2pf_ratelimit;
	u32 vf_nr;
	bool init;
	bool restarting;
	u8 vf_compat_ver;
};

struct adf_dc_data {
	u8 *ovf_buff;
	size_t ovf_buff_sz;
	dma_addr_t ovf_buff_p;
};

struct adf_pm {
	struct dentry *debugfs_pm_status;
	bool present;
	int idle_irq_counters;
	int throttle_irq_counters;
	int fw_irq_counters;
	int host_ack_counter;
	int host_nack_counter;
	ssize_t (*print_pm_status)(struct adf_accel_dev *accel_dev,
				   char __user *buf, size_t count, loff_t *pos);
};

struct adf_sysfs {
	int ring_num;
	struct rw_semaphore lock; /* protects access to the fields in this struct */
};

struct adf_accel_dev {
	struct adf_etr_data *transport;
	struct adf_hw_device_data *hw_device;
	struct adf_cfg_device_data *cfg;
	struct adf_fw_loader_data *fw_loader;
	struct adf_admin_comms *admin;
	struct adf_telemetry *telemetry;
	struct adf_dc_data *dc_data;
	struct adf_pm power_management;
	struct list_head crypto_list;
	struct list_head compression_list;
	unsigned long status;
	atomic_t ref_count;
	struct dentry *debugfs_dir;
	struct dentry *fw_cntr_dbgfile;
	struct dentry *cnv_dbgfile;
	struct list_head list;
	struct module *owner;
	struct adf_accel_pci accel_pci_dev;
	struct adf_timer *timer;
	struct adf_heartbeat *heartbeat;
	struct adf_rl *rate_limiting;
	struct adf_sysfs sysfs;
	union {
		struct {
			/* protects VF2PF interrupts access */
			spinlock_t vf2pf_ints_lock;
			/* vf_info is non-zero when SR-IOV is init'ed */
			struct adf_accel_vf_info *vf_info;
		} pf;
		struct {
			bool irq_enabled;
			char irq_name[ADF_MAX_MSIX_VECTOR_NAME];
			struct tasklet_struct pf2vf_bh_tasklet;
			struct mutex vf2pf_lock; /* protect CSR access */
			struct completion msg_received;
			struct pfvf_message response; /* temp field holding pf2vf response */
			u8 pf_compat_ver;
		} vf;
	};
	struct adf_error_counters ras_errors;
	struct mutex state_lock; /* protect state of the device */
	bool is_vf;
	bool autoreset_on_error;
	u32 accel_id;
};
#endif
