/*
 *  drivers/s390/cio/chp.h
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#ifndef S390_CHP_H
#define S390_CHP_H S390_CHP_H

#include <linux/types.h>
#include <linux/device.h>

#include "chpid.h"
#include "chsc.h"

struct channel_path {
	struct chp_id chpid;
	int state;
	struct channel_path_desc desc;
	/* Channel-measurement related stuff: */
	int cmg;
	int shared;
	void *cmg_chars;
	struct device dev;
};

int chp_get_status(struct chp_id chpid);
u8 chp_get_sch_opm(struct subchannel *sch);
int chp_is_registered(struct chp_id chpid);
void *chp_get_chp_desc(struct chp_id chpid);
int chp_process_crw(int id, int available);
void chp_remove_cmg_attr(struct channel_path *chp);
int chp_add_cmg_attr(struct channel_path *chp);
int chp_new(struct chp_id chpid);

#endif /* S390_CHP_H */
