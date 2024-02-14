/* SPDX-License-Identifier: (GPL-2.0 or MIT) */
/*
 * DSA driver for:
 * Hirschmann Hellcreek TSN switch.
 *
 * Copyright (C) 2019-2021 Linutronix GmbH
 * Author Kurt Kanzenbach <kurt@linutronix.de>
 */

#ifndef _HELLCREEK_H_
#define _HELLCREEK_H_

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/leds.h>
#include <linux/platform_data/hirschmann-hellcreek.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/timecounter.h>
#include <net/dsa.h>
#include <net/pkt_sched.h>

/* Ports:
 *  - 0: CPU
 *  - 1: Tunnel
 *  - 2: TSN front port 1
 *  - 3: TSN front port 2
 *  - ...
 */
#define CPU_PORT			0
#define TUNNEL_PORT			1

#define HELLCREEK_VLAN_NO_MEMBER	0x0
#define HELLCREEK_VLAN_UNTAGGED_MEMBER	0x1
#define HELLCREEK_VLAN_TAGGED_MEMBER	0x3
#define HELLCREEK_NUM_EGRESS_QUEUES	8
#define HELLCREEK_DEFAULT_MAX_SDU	1536

/* Register definitions */
#define HR_MODID_C			(0 * 2)
#define HR_REL_L_C			(1 * 2)
#define HR_REL_H_C			(2 * 2)
#define HR_BLD_L_C			(3 * 2)
#define HR_BLD_H_C			(4 * 2)
#define HR_CTRL_C			(5 * 2)
#define HR_CTRL_C_READY			BIT(14)
#define HR_CTRL_C_TRANSITION		BIT(13)
#define HR_CTRL_C_ENABLE		BIT(0)

#define HR_PSEL				(0xa6 * 2)
#define HR_PSEL_PTWSEL_SHIFT		4
#define HR_PSEL_PTWSEL_MASK		GENMASK(5, 4)
#define HR_PSEL_PRTCWSEL_SHIFT		0
#define HR_PSEL_PRTCWSEL_MASK		GENMASK(2, 0)

#define HR_PTCFG			(0xa7 * 2)
#define HR_PTCFG_MLIMIT_EN		BIT(13)
#define HR_PTCFG_UMC_FLT		BIT(10)
#define HR_PTCFG_UUC_FLT		BIT(9)
#define HR_PTCFG_UNTRUST		BIT(8)
#define HR_PTCFG_TAG_REQUIRED		BIT(7)
#define HR_PTCFG_PPRIO_SHIFT		4
#define HR_PTCFG_PPRIO_MASK		GENMASK(6, 4)
#define HR_PTCFG_INGRESSFLT		BIT(3)
#define HR_PTCFG_BLOCKED		BIT(2)
#define HR_PTCFG_LEARNING_EN		BIT(1)
#define HR_PTCFG_ADMIN_EN		BIT(0)

#define HR_PRTCCFG			(0xa8 * 2)
#define HR_PRTCCFG_PCP_TC_MAP_SHIFT	0
#define HR_PRTCCFG_PCP_TC_MAP_MASK	GENMASK(2, 0)

#define HR_PTPRTCCFG			(0xa9 * 2)
#define HR_PTPRTCCFG_SET_QTRACK		BIT(15)
#define HR_PTPRTCCFG_REJECT		BIT(14)
#define HR_PTPRTCCFG_MAXSDU_SHIFT	0
#define HR_PTPRTCCFG_MAXSDU_MASK	GENMASK(10, 0)

#define HR_CSEL				(0x8d * 2)
#define HR_CSEL_SHIFT			0
#define HR_CSEL_MASK			GENMASK(7, 0)
#define HR_CRDL				(0x8e * 2)
#define HR_CRDH				(0x8f * 2)

#define HR_SWTRC_CFG			(0x90 * 2)
#define HR_SWTRC0			(0x91 * 2)
#define HR_SWTRC1			(0x92 * 2)
#define HR_PFREE			(0x93 * 2)
#define HR_MFREE			(0x94 * 2)

#define HR_FDBAGE			(0x97 * 2)
#define HR_FDBMAX			(0x98 * 2)
#define HR_FDBRDL			(0x99 * 2)
#define HR_FDBRDM			(0x9a * 2)
#define HR_FDBRDH			(0x9b * 2)

#define HR_FDBMDRD			(0x9c * 2)
#define HR_FDBMDRD_PORTMASK_SHIFT	0
#define HR_FDBMDRD_PORTMASK_MASK	GENMASK(3, 0)
#define HR_FDBMDRD_AGE_SHIFT		4
#define HR_FDBMDRD_AGE_MASK		GENMASK(7, 4)
#define HR_FDBMDRD_OBT			BIT(8)
#define HR_FDBMDRD_PASS_BLOCKED		BIT(9)
#define HR_FDBMDRD_STATIC		BIT(11)
#define HR_FDBMDRD_REPRIO_TC_SHIFT	12
#define HR_FDBMDRD_REPRIO_TC_MASK	GENMASK(14, 12)
#define HR_FDBMDRD_REPRIO_EN		BIT(15)

#define HR_FDBWDL			(0x9d * 2)
#define HR_FDBWDM			(0x9e * 2)
#define HR_FDBWDH			(0x9f * 2)
#define HR_FDBWRM0			(0xa0 * 2)
#define HR_FDBWRM0_PORTMASK_SHIFT	0
#define HR_FDBWRM0_PORTMASK_MASK	GENMASK(3, 0)
#define HR_FDBWRM0_OBT			BIT(8)
#define HR_FDBWRM0_PASS_BLOCKED		BIT(9)
#define HR_FDBWRM0_REPRIO_TC_SHIFT	12
#define HR_FDBWRM0_REPRIO_TC_MASK	GENMASK(14, 12)
#define HR_FDBWRM0_REPRIO_EN		BIT(15)
#define HR_FDBWRM1			(0xa1 * 2)

#define HR_FDBWRCMD			(0xa2 * 2)
#define HR_FDBWRCMD_FDBDEL		BIT(9)

#define HR_SWCFG			(0xa3 * 2)
#define HR_SWCFG_GM_STATEMD		BIT(15)
#define HR_SWCFG_LAS_MODE_SHIFT		12
#define HR_SWCFG_LAS_MODE_MASK		GENMASK(13, 12)
#define HR_SWCFG_LAS_OFF		(0x00)
#define HR_SWCFG_LAS_ON			(0x01)
#define HR_SWCFG_LAS_STATIC		(0x10)
#define HR_SWCFG_CT_EN			BIT(11)
#define HR_SWCFG_VLAN_UNAWARE		BIT(10)
#define HR_SWCFG_ALWAYS_OBT		BIT(9)
#define HR_SWCFG_FDBAGE_EN		BIT(5)
#define HR_SWCFG_FDBLRN_EN		BIT(4)

#define HR_SWSTAT			(0xa4 * 2)
#define HR_SWSTAT_FAIL			BIT(4)
#define HR_SWSTAT_BUSY			BIT(0)

#define HR_SWCMD			(0xa5 * 2)
#define HW_SWCMD_FLUSH			BIT(0)

#define HR_VIDCFG			(0xaa * 2)
#define HR_VIDCFG_VID_SHIFT		0
#define HR_VIDCFG_VID_MASK		GENMASK(11, 0)
#define HR_VIDCFG_PVID			BIT(12)

#define HR_VIDMBRCFG			(0xab * 2)
#define HR_VIDMBRCFG_P0MBR_SHIFT	0
#define HR_VIDMBRCFG_P0MBR_MASK		GENMASK(1, 0)
#define HR_VIDMBRCFG_P1MBR_SHIFT	2
#define HR_VIDMBRCFG_P1MBR_MASK		GENMASK(3, 2)
#define HR_VIDMBRCFG_P2MBR_SHIFT	4
#define HR_VIDMBRCFG_P2MBR_MASK		GENMASK(5, 4)
#define HR_VIDMBRCFG_P3MBR_SHIFT	6
#define HR_VIDMBRCFG_P3MBR_MASK		GENMASK(7, 6)

#define HR_FEABITS0			(0xac * 2)
#define HR_FEABITS0_FDBBINS_SHIFT	4
#define HR_FEABITS0_FDBBINS_MASK	GENMASK(7, 4)
#define HR_FEABITS0_PCNT_SHIFT		8
#define HR_FEABITS0_PCNT_MASK		GENMASK(11, 8)
#define HR_FEABITS0_MCNT_SHIFT		12
#define HR_FEABITS0_MCNT_MASK		GENMASK(15, 12)

#define TR_QTRACK			(0xb1 * 2)
#define TR_TGDVER			(0xb3 * 2)
#define TR_TGDVER_REV_MIN_MASK		GENMASK(7, 0)
#define TR_TGDVER_REV_MIN_SHIFT		0
#define TR_TGDVER_REV_MAJ_MASK		GENMASK(15, 8)
#define TR_TGDVER_REV_MAJ_SHIFT		8
#define TR_TGDSEL			(0xb4 * 2)
#define TR_TGDSEL_TDGSEL_MASK		GENMASK(1, 0)
#define TR_TGDSEL_TDGSEL_SHIFT		0
#define TR_TGDCTRL			(0xb5 * 2)
#define TR_TGDCTRL_GATE_EN		BIT(0)
#define TR_TGDCTRL_CYC_SNAP		BIT(4)
#define TR_TGDCTRL_SNAP_EST		BIT(5)
#define TR_TGDCTRL_ADMINGATESTATES_MASK	GENMASK(15, 8)
#define TR_TGDCTRL_ADMINGATESTATES_SHIFT	8
#define TR_TGDSTAT0			(0xb6 * 2)
#define TR_TGDSTAT1			(0xb7 * 2)
#define TR_ESTWRL			(0xb8 * 2)
#define TR_ESTWRH			(0xb9 * 2)
#define TR_ESTCMD			(0xba * 2)
#define TR_ESTCMD_ESTSEC_MASK		GENMASK(2, 0)
#define TR_ESTCMD_ESTSEC_SHIFT		0
#define TR_ESTCMD_ESTARM		BIT(4)
#define TR_ESTCMD_ESTSWCFG		BIT(5)
#define TR_EETWRL			(0xbb * 2)
#define TR_EETWRH			(0xbc * 2)
#define TR_EETCMD			(0xbd * 2)
#define TR_EETCMD_EETSEC_MASK		GEMASK(2, 0)
#define TR_EETCMD_EETSEC_SHIFT		0
#define TR_EETCMD_EETARM		BIT(4)
#define TR_CTWRL			(0xbe * 2)
#define TR_CTWRH			(0xbf * 2)
#define TR_LCNSL			(0xc1 * 2)
#define TR_LCNSH			(0xc2 * 2)
#define TR_LCS				(0xc3 * 2)
#define TR_GCLDAT			(0xc4 * 2)
#define TR_GCLDAT_GCLWRGATES_MASK	GENMASK(7, 0)
#define TR_GCLDAT_GCLWRGATES_SHIFT	0
#define TR_GCLDAT_GCLWRLAST		BIT(8)
#define TR_GCLDAT_GCLOVRI		BIT(9)
#define TR_GCLTIL			(0xc5 * 2)
#define TR_GCLTIH			(0xc6 * 2)
#define TR_GCLCMD			(0xc7 * 2)
#define TR_GCLCMD_GCLWRADR_MASK		GENMASK(7, 0)
#define TR_GCLCMD_GCLWRADR_SHIFT	0
#define TR_GCLCMD_INIT_GATE_STATES_MASK	GENMASK(15, 8)
#define TR_GCLCMD_INIT_GATE_STATES_SHIFT	8

struct hellcreek_counter {
	u8 offset;
	const char *name;
};

struct hellcreek;

/* State flags for hellcreek_port_hwtstamp::state */
enum {
	HELLCREEK_HWTSTAMP_ENABLED,
	HELLCREEK_HWTSTAMP_TX_IN_PROGRESS,
};

/* A structure to hold hardware timestamping information per port */
struct hellcreek_port_hwtstamp {
	/* Timestamping state */
	unsigned long state;

	/* Resources for receive timestamping */
	struct sk_buff_head rx_queue; /* For synchronization messages */

	/* Resources for transmit timestamping */
	unsigned long tx_tstamp_start;
	struct sk_buff *tx_skb;

	/* Current timestamp configuration */
	struct hwtstamp_config tstamp_config;
};

struct hellcreek_port {
	struct hellcreek *hellcreek;
	unsigned long *vlan_dev_bitmap;
	int port;
	u16 ptcfg;		/* ptcfg shadow */
	u64 *counter_values;

	/* Per-port timestamping resources */
	struct hellcreek_port_hwtstamp port_hwtstamp;

	/* Per-port Qbv schedule information */
	struct tc_taprio_qopt_offload *current_schedule;
	struct delayed_work schedule_work;
};

struct hellcreek_fdb_entry {
	size_t idx;
	unsigned char mac[ETH_ALEN];
	u8 portmask;
	u8 age;
	u8 is_obt;
	u8 pass_blocked;
	u8 is_static;
	u8 reprio_tc;
	u8 reprio_en;
};

struct hellcreek {
	const struct hellcreek_platform_data *pdata;
	struct device *dev;
	struct dsa_switch *ds;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_info;
	struct hellcreek_port *ports;
	struct delayed_work overflow_work;
	struct led_classdev led_is_gm;
	struct led_classdev led_sync_good;
	struct mutex reg_lock;	/* Switch IP register lock */
	struct mutex vlan_lock;	/* VLAN bitmaps lock */
	struct mutex ptp_lock;	/* PTP IP register lock */
	struct devlink_region *vlan_region;
	struct devlink_region *fdb_region;
	void __iomem *base;
	void __iomem *ptp_base;
	u16 swcfg;		/* swcfg shadow */
	u8 *vidmbrcfg;		/* vidmbrcfg shadow */
	u64 seconds;		/* PTP seconds */
	u64 last_ts;		/* Used for overflow detection */
	u16 status_out;		/* ptp.status_out shadow */
	size_t fdb_entries;
};

/* A Qbv schedule can only started up to 8 seconds in the future. If the delta
 * between the base time and the current ptp time is larger than 8 seconds, then
 * use periodic work to check for the schedule to be started. The delayed work
 * cannot be armed directly to $base_time - 8 + X, because for large deltas the
 * PTP frequency matters.
 */
#define HELLCREEK_SCHEDULE_PERIOD	(2 * HZ)
#define dw_to_hellcreek_port(dw)				\
	container_of(dw, struct hellcreek_port, schedule_work)

/* Devlink resources */
enum hellcreek_devlink_resource_id {
	HELLCREEK_DEVLINK_PARAM_ID_VLAN_TABLE,
	HELLCREEK_DEVLINK_PARAM_ID_FDB_TABLE,
};

struct hellcreek_devlink_vlan_entry {
	u16 vid;
	u16 member;
};

#endif /* _HELLCREEK_H_ */
