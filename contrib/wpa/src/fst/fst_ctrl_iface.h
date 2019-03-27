/*
 * FST module - internal Control interface definitions
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef FST_CTRL_IFACE_H
#define FST_CTRL_IFACE_H

#include "fst/fst_ctrl_aux.h"

#ifdef CONFIG_FST

/* receiver */
int fst_ctrl_iface_mb_info(const u8 *addr, char *buf, size_t buflen);

int fst_ctrl_iface_receive(const char *txtaddr, char *buf, size_t buflen);

extern const struct fst_ctrl *fst_ctrl_cli;

#else /* CONFIG_FST */

static inline int
fst_ctrl_iface_mb_info(const u8 *addr, char *buf, size_t buflen)
{
	return 0;
}

#endif /* CONFIG_FST */

int fst_read_next_int_param(const char *params, Boolean *valid, char **endp);
int fst_read_next_text_param(const char *params, char *buf, size_t buflen,
			     char **endp);
int fst_read_peer_addr(const char *mac, u8 *peer_addr);

struct fst_iface_cfg;

int fst_parse_attach_command(const char *cmd, char *ifname, size_t ifname_size,
			     struct fst_iface_cfg *cfg);
int fst_parse_detach_command(const char *cmd, char *ifname, size_t ifname_size);
int fst_iface_detach(const char *ifname);

#endif /* CTRL_IFACE_FST_H */
