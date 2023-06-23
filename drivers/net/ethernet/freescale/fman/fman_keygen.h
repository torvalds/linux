/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later */
/*
 * Copyright 2017 NXP
 */

#ifndef __KEYGEN_H
#define __KEYGEN_H

#include <linux/io.h>

struct fman_keygen;
struct fman_kg_regs;

struct fman_keygen *keygen_init(struct fman_kg_regs __iomem *keygen_regs);

int keygen_port_hashing_init(struct fman_keygen *keygen, u8 hw_port_id,
			     u32 hash_base_fqid, u32 hash_size);

#endif /* __KEYGEN_H */
