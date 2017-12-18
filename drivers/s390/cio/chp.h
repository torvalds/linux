/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 2007, 2010
 *    Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#ifndef S390_CHP_H
#define S390_CHP_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <asm/chpid.h>
#include "chsc.h"
#include "css.h"

#define CHP_STATUS_STANDBY		0
#define CHP_STATUS_CONFIGURED		1
#define CHP_STATUS_RESERVED		2
#define CHP_STATUS_NOT_RECOGNIZED	3

#define CHP_ONLINE 0
#define CHP_OFFLINE 1
#define CHP_VARY_ON 2
#define CHP_VARY_OFF 3

struct chp_link {
	struct chp_id chpid;
	u32 fla_mask;
	u16 fla;
};

static inline int chp_test_bit(u8 *bitmap, int num)
{
	int byte = num >> 3;
	int mask = 128 >> (num & 7);

	return (bitmap[byte] & mask) ? 1 : 0;
}


struct channel_path {
	struct device dev;
	struct chp_id chpid;
	struct mutex lock; /* Serialize access to below members. */
	int state;
	struct channel_path_desc desc;
	struct channel_path_desc_fmt1 desc_fmt1;
	/* Channel-measurement related stuff: */
	int cmg;
	int shared;
	struct cmg_chars cmg_chars;
};

/* Return channel_path struct for given chpid. */
static inline struct channel_path *chpid_to_chp(struct chp_id chpid)
{
	return css_by_id(chpid.cssid)->chps[chpid.id];
}

int chp_get_status(struct chp_id chpid);
u8 chp_get_sch_opm(struct subchannel *sch);
int chp_is_registered(struct chp_id chpid);
struct channel_path_desc *chp_get_chp_desc(struct chp_id chpid);
void chp_remove_cmg_attr(struct channel_path *chp);
int chp_add_cmg_attr(struct channel_path *chp);
int chp_update_desc(struct channel_path *chp);
int chp_new(struct chp_id chpid);
void chp_cfg_schedule(struct chp_id chpid, int configure);
void chp_cfg_cancel_deconfigure(struct chp_id chpid);
int chp_info_get_status(struct chp_id chpid);
int chp_ssd_get_mask(struct chsc_ssd_info *, struct chp_link *);
#endif /* S390_CHP_H */
