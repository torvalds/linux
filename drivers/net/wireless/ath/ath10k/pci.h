/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _PCI_H_
#define _PCI_H_

#include <linux/interrupt.h>

#include "hw.h"
#include "ce.h"

/*
 * maximum number of bytes that can be handled atomically by DiagRead/DiagWrite
 */
#define DIAG_TRANSFER_LIMIT 2048

/*
 * maximum number of bytes that can be
 * handled atomically by DiagRead/DiagWrite
 */
#define DIAG_TRANSFER_LIMIT 2048

struct bmi_xfer {
	bool tx_done;
	bool rx_done;
	bool wait_for_resp;
	u32 resp_len;
};

/*
 * PCI-specific Target state
 *
 * NOTE: Structure is shared between Host software and Target firmware!
 *
 * Much of this may be of interest to the Host so
 * HOST_INTEREST->hi_interconnect_state points here
 * (and all members are 32-bit quantities in order to
 * facilitate Host access). In particular, Host software is
 * required to initialize pipe_cfg_addr and svc_to_pipe_map.
 */
struct pcie_state {
	/* Pipe configuration Target address */
	/* NB: ce_pipe_config[CE_COUNT] */
	u32 pipe_cfg_addr;

	/* Service to pipe map Target address */
	/* NB: service_to_pipe[PIPE_TO_CE_MAP_CN] */
	u32 svc_to_pipe_map;

	/* number of MSI interrupts requested */
	u32 msi_requested;

	/* number of MSI interrupts granted */
	u32 msi_granted;

	/* Message Signalled Interrupt address */
	u32 msi_addr;

	/* Base data */
	u32 msi_data;

	/*
	 * Data for firmware interrupt;
	 * MSI data for other interrupts are
	 * in various SoC registers
	 */
	u32 msi_fw_intr_data;

	/* PCIE_PWR_METHOD_* */
	u32 power_mgmt_method;

	/* PCIE_CONFIG_FLAG_* */
	u32 config_flags;
};

/* PCIE_CONFIG_FLAG definitions */
#define PCIE_CONFIG_FLAG_ENABLE_L1  0x0000001

/* Host software's Copy Engine configuration. */
#define CE_ATTR_FLAGS 0

/*
 * Configuration information for a Copy Engine pipe.
 * Passed from Host to Target during startup (one per CE).
 *
 * NOTE: Structure is shared between Host software and Target firmware!
 */
struct ce_pipe_config {
	__le32 pipenum;
	__le32 pipedir;
	__le32 nentries;
	__le32 nbytes_max;
	__le32 flags;
	__le32 reserved;
};

/*
 * Directions for interconnect pipe configuration.
 * These definitions may be used during configuration and are shared
 * between Host and Target.
 *
 * Pipe Directions are relative to the Host, so PIPEDIR_IN means
 * "coming IN over air through Target to Host" as with a WiFi Rx operation.
 * Conversely, PIPEDIR_OUT means "going OUT from Host through Target over air"
 * as with a WiFi Tx operation. This is somewhat awkward for the "middle-man"
 * Target since things that are "PIPEDIR_OUT" are coming IN to the Target
 * over the interconnect.
 */
#define PIPEDIR_NONE    0
#define PIPEDIR_IN      1  /* Target-->Host, WiFi Rx direction */
#define PIPEDIR_OUT     2  /* Host->Target, WiFi Tx direction */
#define PIPEDIR_INOUT   3  /* bidirectional */

/* Establish a mapping between a service/direction and a pipe. */
struct service_to_pipe {
	__le32 service_id;
	__le32 pipedir;
	__le32 pipenum;
};

/* Per-pipe state. */
struct ath10k_pci_pipe {
	/* Handle of underlying Copy Engine */
	struct ath10k_ce_pipe *ce_hdl;

	/* Our pipe number; facilitiates use of pipe_info ptrs. */
	u8 pipe_num;

	/* Convenience back pointer to hif_ce_state. */
	struct ath10k *hif_ce_state;

	size_t buf_sz;

	/* protects compl_free and num_send_allowed */
	spinlock_t pipe_lock;

	struct ath10k_pci *ar_pci;
	struct tasklet_struct intr;
};

struct ath10k_pci_supp_chip {
	u32 dev_id;
	u32 rev_id;
};

struct ath10k_pci {
	struct pci_dev *pdev;
	struct device *dev;
	struct ath10k *ar;
	void __iomem *mem;

	/*
	 * Number of MSI interrupts granted, 0 --> using legacy PCI line
	 * interrupts.
	 */
	int num_msi_intrs;

	struct tasklet_struct intr_tq;
	struct tasklet_struct msi_fw_err;

	struct ath10k_pci_pipe pipe_info[CE_COUNT_MAX];

	struct ath10k_hif_cb msg_callbacks_current;

	/* Copy Engine used for Diagnostic Accesses */
	struct ath10k_ce_pipe *ce_diag;

	/* FIXME: document what this really protects */
	spinlock_t ce_lock;

	/* Map CE id to ce_state */
	struct ath10k_ce_pipe ce_states[CE_COUNT_MAX];
	struct timer_list rx_post_retry;
};

static inline struct ath10k_pci *ath10k_pci_priv(struct ath10k *ar)
{
	return (struct ath10k_pci *)ar->drv_priv;
}

#define ATH10K_PCI_RX_POST_RETRY_MS 50
#define ATH_PCI_RESET_WAIT_MAX 10 /* ms */
#define PCIE_WAKE_TIMEOUT 10000	/* 10ms */

#define BAR_NUM 0

#define CDC_WAR_MAGIC_STR   0xceef0000
#define CDC_WAR_DATA_CE     4

/*
 * TODO: Should be a function call specific to each Target-type.
 * This convoluted macro converts from Target CPU Virtual Address Space to CE
 * Address Space. As part of this process, we conservatively fetch the current
 * PCIE_BAR. MOST of the time, this should match the upper bits of PCI space
 * for this device; but that's not guaranteed.
 */
#define TARG_CPU_SPACE_TO_CE_SPACE(ar, pci_addr, addr)			\
	(((ioread32((pci_addr)+(SOC_CORE_BASE_ADDRESS|			\
	  CORE_CTRL_ADDRESS)) & 0x7ff) << 21) |				\
	 0x100000 | ((addr) & 0xfffff))

/* Wait up to this many Ms for a Diagnostic Access CE operation to complete */
#define DIAG_ACCESS_CE_TIMEOUT_MS 10

/* Target exposes its registers for direct access. However before host can
 * access them it needs to make sure the target is awake (ath10k_pci_wake,
 * ath10k_pci_wake_wait, ath10k_pci_is_awake). Once target is awake it won't go
 * to sleep unless host tells it to (ath10k_pci_sleep).
 *
 * If host tries to access target registers without waking it up it can
 * scribble over host memory.
 *
 * If target is asleep waking it up may take up to even 2ms.
 */

static inline void ath10k_pci_write32(struct ath10k *ar, u32 offset,
				      u32 value)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	iowrite32(value, ar_pci->mem + offset);
}

static inline u32 ath10k_pci_read32(struct ath10k *ar, u32 offset)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	return ioread32(ar_pci->mem + offset);
}

static inline u32 ath10k_pci_soc_read32(struct ath10k *ar, u32 addr)
{
	return ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS + addr);
}

static inline void ath10k_pci_soc_write32(struct ath10k *ar, u32 addr, u32 val)
{
	ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + addr, val);
}

static inline u32 ath10k_pci_reg_read32(struct ath10k *ar, u32 addr)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	return ioread32(ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS + addr);
}

static inline void ath10k_pci_reg_write32(struct ath10k *ar, u32 addr, u32 val)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	iowrite32(val, ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS + addr);
}

#endif /* _PCI_H_ */
