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

/* FW dump area */
#define REG_DUMP_COUNT_QCA988X 60

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
	struct completion done;
	bool wait_for_resp;
	u32 resp_len;
};

enum ath10k_pci_compl_state {
	ATH10K_PCI_COMPL_FREE = 0,
	ATH10K_PCI_COMPL_SEND,
	ATH10K_PCI_COMPL_RECV,
};

struct ath10k_pci_compl {
	struct list_head list;
	enum ath10k_pci_compl_state state;
	struct ath10k_ce_pipe *ce_state;
	struct ath10k_pci_pipe *pipe_info;
	void *transfer_context;
	unsigned int nbytes;
	unsigned int transfer_id;
	unsigned int flags;
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
	u32 pipenum;
	u32 pipedir;
	u32 nentries;
	u32 nbytes_max;
	u32 flags;
	u32 reserved;
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
	u32 service_id;
	u32 pipedir;
	u32 pipenum;
};

enum ath10k_pci_features {
	ATH10K_PCI_FEATURE_MSI_X		= 0,
	ATH10K_PCI_FEATURE_SOC_POWER_SAVE	= 1,

	/* keep last */
	ATH10K_PCI_FEATURE_COUNT
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

	/* List of free CE completion slots */
	struct list_head compl_free;

	/* Limit the number of outstanding send requests. */
	int num_sends_allowed;

	struct ath10k_pci *ar_pci;
	struct tasklet_struct intr;
};

struct ath10k_pci {
	struct pci_dev *pdev;
	struct device *dev;
	struct ath10k *ar;
	void __iomem *mem;

	DECLARE_BITMAP(features, ATH10K_PCI_FEATURE_COUNT);

	/*
	 * Number of MSI interrupts granted, 0 --> using legacy PCI line
	 * interrupts.
	 */
	int num_msi_intrs;

	struct tasklet_struct intr_tq;
	struct tasklet_struct msi_fw_err;

	/* Number of Copy Engines supported */
	unsigned int ce_count;

	int started;

	atomic_t keep_awake_count;
	bool verified_awake;

	/* List of CE completions to be processed */
	struct list_head compl_process;

	/* protects compl_processing and compl_process */
	spinlock_t compl_lock;

	bool compl_processing;

	struct ath10k_pci_pipe pipe_info[CE_COUNT_MAX];

	struct ath10k_hif_cb msg_callbacks_current;

	/* Target address used to signal a pending firmware event */
	u32 fw_indicator_address;

	/* Copy Engine used for Diagnostic Accesses */
	struct ath10k_ce_pipe *ce_diag;

	/* FIXME: document what this really protects */
	spinlock_t ce_lock;

	/* Map CE id to ce_state */
	struct ath10k_ce_pipe ce_states[CE_COUNT_MAX];
};

static inline struct ath10k_pci *ath10k_pci_priv(struct ath10k *ar)
{
	return ar->hif.priv;
}

static inline u32 ath10k_pci_reg_read32(void __iomem *mem, u32 addr)
{
	return ioread32(mem + PCIE_LOCAL_BASE_ADDRESS + addr);
}

static inline void ath10k_pci_reg_write32(void __iomem *mem, u32 addr, u32 val)
{
	iowrite32(val, mem + PCIE_LOCAL_BASE_ADDRESS + addr);
}

#define ATH_PCI_RESET_WAIT_MAX 10 /* ms */
#define PCIE_WAKE_TIMEOUT 5000	/* 5ms */

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

/*
 * This API allows the Host to access Target registers directly
 * and relatively efficiently over PCIe.
 * This allows the Host to avoid extra overhead associated with
 * sending a message to firmware and waiting for a response message
 * from firmware, as is done on other interconnects.
 *
 * Yet there is some complexity with direct accesses because the
 * Target's power state is not known a priori. The Host must issue
 * special PCIe reads/writes in order to explicitly wake the Target
 * and to verify that it is awake and will remain awake.
 *
 * Usage:
 *
 *   Use ath10k_pci_read32 and ath10k_pci_write32 to access Target space.
 *   These calls must be bracketed by ath10k_pci_wake and
 *   ath10k_pci_sleep.  A single BEGIN/END pair is adequate for
 *   multiple READ/WRITE operations.
 *
 *   Use ath10k_pci_wake to put the Target in a state in
 *   which it is legal for the Host to directly access it. This
 *   may involve waking the Target from a low power state, which
 *   may take up to 2Ms!
 *
 *   Use ath10k_pci_sleep to tell the Target that as far as
 *   this code path is concerned, it no longer needs to remain
 *   directly accessible.  BEGIN/END is under a reference counter;
 *   multiple code paths may issue BEGIN/END on a single targid.
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

void ath10k_do_pci_wake(struct ath10k *ar);
void ath10k_do_pci_sleep(struct ath10k *ar);

static inline void ath10k_pci_wake(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	if (test_bit(ATH10K_PCI_FEATURE_SOC_POWER_SAVE, ar_pci->features))
		ath10k_do_pci_wake(ar);
}

static inline void ath10k_pci_sleep(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	if (test_bit(ATH10K_PCI_FEATURE_SOC_POWER_SAVE, ar_pci->features))
		ath10k_do_pci_sleep(ar);
}

#endif /* _PCI_H_ */
