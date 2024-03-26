/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2022 Linaro Ltd.
 */
#ifndef _IPA_H_
#define _IPA_H_

#include <linux/types.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/pm_wakeup.h>

#include "ipa_version.h"
#include "gsi.h"
#include "ipa_mem.h"
#include "ipa_qmi.h"
#include "ipa_endpoint.h"
#include "ipa_interrupt.h"

struct clk;
struct icc_path;
struct net_device;

struct ipa_power;
struct ipa_smp2p;
struct ipa_interrupt;

/**
 * struct ipa - IPA information
 * @gsi:		Embedded GSI structure
 * @version:		IPA hardware version
 * @dev:		IPA device pointer
 * @completion:		Used to signal pipeline clear transfer complete
 * @nb:			Notifier block used for remoteproc SSR
 * @notifier:		Remoteproc SSR notifier
 * @smp2p:		SMP2P information
 * @power:		IPA power information
 * @table_addr:		DMA address of filter/route table content
 * @table_virt:		Virtual address of filter/route table content
 * @route_count:	Total number of entries in a routing table
 * @modem_route_count:	Number of modem entries in a routing table
 * @filter_count:	Maximum number of entries in a filter table
 * @interrupt:		IPA Interrupt information
 * @uc_powered:		true if power is active by proxy for microcontroller
 * @uc_loaded:		true after microcontroller has reported it's ready
 * @reg_virt:		Virtual address used for IPA register access
 * @regs:		IPA register definitions
 * @mem_addr:		DMA address of IPA-local memory space
 * @mem_virt:		Virtual address of IPA-local memory space
 * @mem_offset:		Offset from @mem_virt used for access to IPA memory
 * @mem_size:		Total size (bytes) of memory at @mem_virt
 * @mem_count:		Number of entries in the mem array
 * @mem:		Array of IPA-local memory region descriptors
 * @imem_iova:		I/O virtual address of IPA region in IMEM
 * @imem_size:		Size of IMEM region
 * @smem_iova:		I/O virtual address of IPA region in SMEM
 * @smem_size:		Size of SMEM region
 * @zero_addr:		DMA address of preallocated zero-filled memory
 * @zero_virt:		Virtual address of preallocated zero-filled memory
 * @zero_size:		Size (bytes) of preallocated zero-filled memory
 * @endpoint_count:	Number of defined bits in most bitmaps below
 * @available_count:	Number of defined bits in the available bitmap
 * @defined:		Bitmap of endpoints defined in config data
 * @available:		Bitmap of endpoints supported by hardware
 * @filtered:		Bitmap of endpoints that support filtering
 * @set_up:		Bitmap of endpoints that are set up for use
 * @enabled:		Bitmap of currently enabled endpoints
 * @modem_tx_count:	Number of defined modem TX endoints
 * @endpoint:		Array of endpoint information
 * @channel_map:	Mapping of GSI channel to IPA endpoint
 * @name_map:		Mapping of IPA endpoint name to IPA endpoint
 * @setup_complete:	Flag indicating whether setup stage has completed
 * @modem_state:	State of modem (stopped, running)
 * @modem_netdev:	Network device structure used for modem
 * @qmi:		QMI information
 */
struct ipa {
	struct gsi gsi;
	enum ipa_version version;
	struct device *dev;
	struct completion completion;
	struct notifier_block nb;
	void *notifier;
	struct ipa_smp2p *smp2p;
	struct ipa_power *power;

	dma_addr_t table_addr;
	__le64 *table_virt;
	u32 route_count;
	u32 modem_route_count;
	u32 filter_count;

	struct ipa_interrupt *interrupt;
	bool uc_powered;
	bool uc_loaded;

	void __iomem *reg_virt;
	const struct regs *regs;

	dma_addr_t mem_addr;
	void *mem_virt;
	u32 mem_offset;
	u32 mem_size;
	u32 mem_count;
	const struct ipa_mem *mem;

	unsigned long imem_iova;
	size_t imem_size;

	unsigned long smem_iova;
	size_t smem_size;

	dma_addr_t zero_addr;
	void *zero_virt;
	size_t zero_size;

	/* Bitmaps indicating endpoint state */
	u32 endpoint_count;
	u32 available_count;
	unsigned long *defined;		/* Defined in configuration data */
	unsigned long *available;	/* Supported by hardware */
	u64 filtered;			/* Support filtering (AP and modem) */
	unsigned long *set_up;
	unsigned long *enabled;

	u32 modem_tx_count;
	struct ipa_endpoint endpoint[IPA_ENDPOINT_MAX];
	struct ipa_endpoint *channel_map[GSI_CHANNEL_COUNT_MAX];
	struct ipa_endpoint *name_map[IPA_ENDPOINT_COUNT];

	bool setup_complete;

	atomic_t modem_state;		/* enum ipa_modem_state */
	struct net_device *modem_netdev;
	struct ipa_qmi qmi;
};

/**
 * ipa_setup() - Perform IPA setup
 * @ipa:		IPA pointer
 *
 * IPA initialization is broken into stages:  init; config; and setup.
 * (These have inverses exit, deconfig, and teardown.)
 *
 * Activities performed at the init stage can be done without requiring
 * any access to IPA hardware.  Activities performed at the config stage
 * require IPA power, because they involve access to IPA registers.
 * The setup stage is performed only after the GSI hardware is ready
 * (more on this below).  The setup stage allows the AP to perform
 * more complex initialization by issuing "immediate commands" using
 * a special interface to the IPA.
 *
 * This function, @ipa_setup(), starts the setup stage.
 *
 * In order for the GSI hardware to be functional it needs firmware to be
 * loaded (in addition to some other low-level initialization).  This early
 * GSI initialization can be done either by Trust Zone on the AP or by the
 * modem.
 *
 * If it's done by Trust Zone, the AP loads the GSI firmware and supplies
 * it to Trust Zone to verify and install.  When this completes, if
 * verification was successful, the GSI layer is ready and ipa_setup()
 * implements the setup phase of initialization.
 *
 * If the modem performs early GSI initialization, the AP needs to know
 * when this has occurred.  An SMP2P interrupt is used for this purpose,
 * and receipt of that interrupt triggers the call to ipa_setup().
 */
int ipa_setup(struct ipa *ipa);

#endif /* _IPA_H_ */
