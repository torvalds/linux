/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2025 Cisco Systems, Inc.  All rights reserved. */

#ifndef _ENIC_ADMIN_H_
#define _ENIC_ADMIN_H_

#define ENIC_ADMIN_DESC_COUNT	64
#define ENIC_ADMIN_BUF_SIZE	2048

struct enic;

int enic_admin_channel_open(struct enic *enic);
void enic_admin_channel_close(struct enic *enic);

#endif /* _ENIC_ADMIN_H_ */
