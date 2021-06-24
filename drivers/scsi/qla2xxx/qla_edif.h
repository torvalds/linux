/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Marvell Fibre Channel HBA Driver
 * Copyright (c)  2021    Marvell
 */
#ifndef __QLA_EDIF_H
#define __QLA_EDIF_H

struct qla_scsi_host;
#define EDIF_APP_ID 0x73730001

enum enode_flags_t {
	ENODE_ACTIVE = 0x1,
};

struct pur_core {
	enum enode_flags_t	enode_flags;
	spinlock_t		pur_lock;
	struct  list_head	head;
};

enum db_flags_t {
	EDB_ACTIVE = 0x1,
};

struct edif_dbell {
	enum db_flags_t		db_flags;
	spinlock_t		db_lock;
	struct  list_head	head;
	struct	completion	dbell;
};

#endif	/* __QLA_EDIF_H */
