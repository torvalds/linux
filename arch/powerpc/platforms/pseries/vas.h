/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2020-21 IBM Corp.
 */

#ifndef _VAS_H
#define _VAS_H
#include <asm/vas.h>
#include <linux/mutex.h>
#include <linux/stringify.h>

/*
 * VAS window modify flags
 */
#define VAS_MOD_WIN_CLOSE	PPC_BIT(0)
#define VAS_MOD_WIN_JOBS_KILL	PPC_BIT(1)
#define VAS_MOD_WIN_DR		PPC_BIT(3)
#define VAS_MOD_WIN_PR		PPC_BIT(4)
#define VAS_MOD_WIN_SF		PPC_BIT(5)
#define VAS_MOD_WIN_TA		PPC_BIT(6)
#define VAS_MOD_WIN_FLAGS	(VAS_MOD_WIN_JOBS_KILL | VAS_MOD_WIN_DR | \
				VAS_MOD_WIN_PR | VAS_MOD_WIN_SF)

#define VAS_WIN_ACTIVE		0x0
#define VAS_WIN_CLOSED		0x1
#define VAS_WIN_INACTIVE	0x2	/* Inactive due to HW failure */
/* Process of being modified, deallocated, or quiesced */
#define VAS_WIN_MOD_IN_PROCESS	0x3

#define VAS_COPY_PASTE_USER_MODE	0x00000001
#define VAS_COP_OP_USER_MODE		0x00000010

/*
 * Co-processor feature - GZIP QoS windows or GZIP default windows
 */
enum vas_cop_feat_type {
	VAS_GZIP_QOS_FEAT_TYPE,
	VAS_GZIP_DEF_FEAT_TYPE,
	VAS_MAX_FEAT_TYPE,
};

/*
 * Use to get feature specific capabilities from the
 * hypervisor.
 */
struct hv_vas_cop_feat_caps {
	__be64	descriptor;
	u8	win_type;		/* Default or QoS type */
	u8	user_mode;
	__be16	max_lpar_creds;
	__be16	max_win_creds;
	union {
		__be16	reserved;
		__be16	def_lpar_creds; /* Used for default capabilities */
	};
	__be16	target_lpar_creds;
} __packed __aligned(0x1000);

/*
 * Feature specific (QoS or default) capabilities.
 */
struct vas_cop_feat_caps {
	u64		descriptor;
	u8		win_type;	/* Default or QoS type */
	u8		user_mode;	/* User mode copy/paste or COP HCALL */
	u16		max_lpar_creds;	/* Max credits available in LPAR */
	/* Max credits can be assigned per window */
	u16		max_win_creds;
	union {
		u16	reserved;	/* Used for QoS credit type */
		u16	def_lpar_creds; /* Used for default credit type */
	};
	/* Total LPAR available credits. Can be different from max LPAR */
	/* credits due to DLPAR operation */
	atomic_t	target_lpar_creds;
	atomic_t	used_lpar_creds; /* Used credits so far */
	u16		avail_lpar_creds; /* Remaining available credits */
};

/*
 * Feature (QoS or Default) specific to store capabilities and
 * the list of open windows.
 */
struct vas_caps {
	struct vas_cop_feat_caps caps;
	struct list_head list;	/* List of open windows */
};

/*
 * To get window information from the hypervisor.
 */
struct hv_vas_win_lpar {
	__be16	version;
	u8	win_type;
	u8	status;
	__be16	credits;	/* No of credits assigned to this window */
	__be16	reserved;
	__be32	pid;		/* LPAR Process ID */
	__be32	tid;		/* LPAR Thread ID */
	__be64	win_addr;	/* Paste address */
	__be32	interrupt;	/* Interrupt when NX request completes */
	__be32	fault;		/* Interrupt when NX sees fault */
	/* Associativity Domain Identifiers as returned in */
	/* H_HOME_NODE_ASSOCIATIVITY */
	__be64	domain[6];
	__be64	win_util;	/* Number of bytes processed */
} __packed __aligned(0x1000);

struct pseries_vas_window {
	struct vas_window vas_win;
	u64 win_addr;		/* Physical paste address */
	u8 win_type;		/* QoS or Default window */
	u32 complete_irq;	/* Completion interrupt */
	u32 fault_irq;		/* Fault interrupt */
	u64 domain[6];		/* Associativity domain Ids */
				/* this window is allocated */
	u64 util;

	/* List of windows opened which is used for LPM */
	struct list_head win_list;
	u64 flags;
	char *name;
	int fault_virq;
};
#endif /* _VAS_H */
