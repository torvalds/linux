/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#ifndef _IONIC_DEBUGFS_H_
#define _IONIC_DEBUGFS_H_

#include <linux/debugfs.h>

#ifdef CONFIG_DEBUG_FS

void ionic_debugfs_create(void);
void ionic_debugfs_destroy(void);
void ionic_debugfs_add_dev(struct ionic *ionic);
void ionic_debugfs_del_dev(struct ionic *ionic);
void ionic_debugfs_add_ident(struct ionic *ionic);
void ionic_debugfs_add_sizes(struct ionic *ionic);
void ionic_debugfs_add_lif(struct ionic_lif *lif);
void ionic_debugfs_add_qcq(struct ionic_lif *lif, struct ionic_qcq *qcq);
void ionic_debugfs_del_lif(struct ionic_lif *lif);
void ionic_debugfs_del_qcq(struct ionic_qcq *qcq);
#else
static inline void ionic_debugfs_create(void) { }
static inline void ionic_debugfs_destroy(void) { }
static inline void ionic_debugfs_add_dev(struct ionic *ionic) { }
static inline void ionic_debugfs_del_dev(struct ionic *ionic) { }
static inline void ionic_debugfs_add_ident(struct ionic *ionic) { }
static inline void ionic_debugfs_add_sizes(struct ionic *ionic) { }
static inline void ionic_debugfs_add_lif(struct ionic_lif *lif) { }
static inline void ionic_debugfs_add_qcq(struct ionic_lif *lif, struct ionic_qcq *qcq) { }
static inline void ionic_debugfs_del_lif(struct ionic_lif *lif) { }
static inline void ionic_debugfs_del_qcq(struct ionic_qcq *qcq) { }
#endif

#endif /* _IONIC_DEBUGFS_H_ */
