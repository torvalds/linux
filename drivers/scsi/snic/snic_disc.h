/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2014 Cisco Systems, Inc.  All rights reserved. */

#ifndef __SNIC_DISC_H
#define __SNIC_DISC_H

#include "snic_fwint.h"

enum snic_disc_state {
	SNIC_DISC_NONE,
	SNIC_DISC_INIT,
	SNIC_DISC_PENDING,
	SNIC_DISC_DONE
};

struct snic;
struct snic_disc {
	struct list_head tgt_list;
	enum snic_disc_state state;
	struct mutex mutex;
	u16	disc_id;
	u8	req_cnt;
	u32	nxt_tgt_id;
	u32	rtgt_cnt;
	u8	*rtgt_info;
	struct delayed_work disc_timeout;
	void (*cb)(struct snic *);
};

#define SNIC_TGT_NAM_LEN	16

enum snic_tgt_state {
	SNIC_TGT_STAT_NONE,
	SNIC_TGT_STAT_INIT,
	SNIC_TGT_STAT_ONLINE,	/* Target is Online */
	SNIC_TGT_STAT_OFFLINE,	/* Target is Offline */
	SNIC_TGT_STAT_DEL,
};

struct snic_tgt_priv {
	struct list_head list;
	enum snic_tgt_type typ;
	u16 disc_id;
	char *name[SNIC_TGT_NAM_LEN];

	union {
		/*DAS Target specific info */
		/*SAN Target specific info */
		u8 dummmy;
	} u;
};

/* snic tgt flags */
#define SNIC_TGT_SCAN_PENDING	0x01

struct snic_tgt {
	struct list_head list;
	u16	id;
	u16	channel;
	u32	flags;
	u32	scsi_tgt_id;
	enum snic_tgt_state state;
	struct device dev;
	struct work_struct scan_work;
	struct work_struct del_work;
	struct snic_tgt_priv tdata;
};


struct snic_fw_req;

void snic_disc_init(struct snic_disc *);
int snic_disc_start(struct snic *);
void snic_disc_term(struct snic *);
int snic_report_tgt_cmpl_handler(struct snic *, struct snic_fw_req *);
int snic_tgtinfo_cmpl_handler(struct snic *snic, struct snic_fw_req *fwreq);
void snic_process_report_tgts_rsp(struct work_struct *);
void snic_handle_tgt_disc(struct work_struct *);
void snic_handle_disc(struct work_struct *);
void snic_tgt_dev_release(struct device *);
void snic_tgt_del_all(struct snic *);

#define dev_to_tgt(d) \
	container_of(d, struct snic_tgt, dev)

static inline int
is_snic_target(struct device *dev)
{
	return dev->release == snic_tgt_dev_release;
}

#define starget_to_tgt(st)	\
	(is_snic_target(((struct scsi_target *) st)->dev.parent) ? \
		dev_to_tgt(st->dev.parent) : NULL)

#define snic_tgt_to_shost(t)	\
	dev_to_shost(t->dev.parent)

static inline int
snic_tgt_chkready(struct snic_tgt *tgt)
{
	if (tgt->state == SNIC_TGT_STAT_ONLINE)
		return 0;
	else
		return DID_NO_CONNECT << 16;
}

const char *snic_tgt_state_to_str(int);
int snic_tgt_scsi_abort_io(struct snic_tgt *);
#endif /* end of  __SNIC_DISC_H */
