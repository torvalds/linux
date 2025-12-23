/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023,2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CORESIGHT_CORESIGHT_TPDA_H
#define _CORESIGHT_CORESIGHT_TPDA_H

#define TPDA_CR			(0x000)
#define TPDA_Pn_CR(n)		(0x004 + (n * 4))
#define TPDA_FPID_CR		(0x084)
#define TPDA_SYNCR		(0x08C)
#define TPDA_FLUSH_CR		(0x090)

/* Cross trigger global (all ports) flush request bit */
#define TPDA_CR_FLREQ		BIT(0)
/* Cross trigger FREQ packets timestamp bit */
#define TPDA_CR_FREQTS		BIT(2)
/* Cross trigger FREQ packet request bit */
#define TPDA_CR_FRIE		BIT(3)
/* Cross trigger FLAG packet request interface bit */
#define TPDA_CR_FLRIE		BIT(4)
/* Cross trigger synchronization bit */
#define TPDA_CR_SRIE		BIT(5)
/* Bits 6 ~ 12 is for atid value */
#define TPDA_CR_ATID		GENMASK(12, 6)
/*
 * Channel mode bit of the packetization of CMB/MCB traffic
 * 0 - raw channel mapping mode
 * 1 - channel pair marking mode
 */
#define TPDA_CR_CMBCHANMODE	BIT(20)

/* Aggregator port enable bit */
#define TPDA_Pn_CR_ENA		BIT(0)
/* Aggregator port CMB data set element size bit */
#define TPDA_Pn_CR_CMBSIZE		GENMASK(7, 6)
/* Aggregator port DSB data set element size bit */
#define TPDA_Pn_CR_DSBSIZE		BIT(8)

/* TPDA_SYNCR count mask */
#define TPDA_SYNCR_COUNT_MASK		GENMASK(11, 0)
/* TPDA_SYNCR mode control bit */
#define TPDA_SYNCR_MODE_CTRL_MASK	GENMASK(12, 12)

#define TPDA_MAX_INPORTS	32

/**
 * struct tpda_drvdata - specifics associated to an TPDA component
 * @base:       memory mapped base address for this component.
 * @dev:        The device entity associated to this component.
 * @csdev:      component vitals needed by the framework.
 * @spinlock:   lock for the drvdata value.
 * @enable:     enable status of the component.
 * @dsb_esize   Record the DSB element size.
 * @cmb_esize   Record the CMB element size.
 * @trig_async:	Enable/disable cross trigger synchronization sequence interface.
 * @trig_flag_ts: Enable/disable cross trigger FLAG packet request interface.
 * @trig_freq:	Enable/disable cross trigger FREQ packet request interface.
 * @freq_ts:	Enable/disable the timestamp for all FREQ packets.
 * @cmbchan_mode: Configure the CMB/MCMB channel mode.
 * @syncr_mode:	Setting the mode for counting packets.
 * @syncr_count: Setting the value of the count.
 */
struct tpda_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct coresight_device	*csdev;
	spinlock_t		spinlock;
	u8			atid;
	u32			dsb_esize;
	u32			cmb_esize;
	bool			trig_async;
	bool			trig_flag_ts;
	bool			trig_freq;
	bool			freq_ts;
	bool			cmbchan_mode;
	bool			syncr_mode;
	u32			syncr_count;
};

/* Enumerate members of global control register(cr) */
enum tpda_cr_mem {
	FREQTS,
	FRIE,
	FLRIE,
	SRIE,
	CMBCHANMODE
};

/**
 * struct tpda_trig_sysfs_attribute - Record the member variables of cross
 * trigger register that need to be operated by sysfs file
 * @attr:	The device attribute
 * @mem:	The member in the control register data structure
 */
struct tpda_trig_sysfs_attribute {
	struct device_attribute attr;
	enum tpda_cr_mem mem;
};

#define tpda_trig_sysfs_rw(name, mem)				\
	(&((struct tpda_trig_sysfs_attribute[]) {		\
	   {							\
		__ATTR(name, 0644, tpda_trig_sysfs_show,	\
		tpda_trig_sysfs_store),				\
		mem,						\
	   }							\
	   })[0].attr.attr)

#endif  /* _CORESIGHT_CORESIGHT_TPDA_H */
