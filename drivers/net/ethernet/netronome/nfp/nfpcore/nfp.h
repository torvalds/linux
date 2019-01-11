/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * nfp.h
 * Interface for NFP device access and query functions.
 */

#ifndef __NFP_H__
#define __NFP_H__

#include <linux/device.h>
#include <linux/types.h>

#include "nfp_cpp.h"

/* Implemented in nfp_hwinfo.c */

struct nfp_hwinfo;
struct nfp_hwinfo *nfp_hwinfo_read(struct nfp_cpp *cpp);
const char *nfp_hwinfo_lookup(struct nfp_hwinfo *hwinfo, const char *lookup);
char *nfp_hwinfo_get_packed_strings(struct nfp_hwinfo *hwinfo);
u32 nfp_hwinfo_get_packed_str_size(struct nfp_hwinfo *hwinfo);

/* Implemented in nfp_nsp.c, low level functions */

struct nfp_nsp;

struct nfp_cpp *nfp_nsp_cpp(struct nfp_nsp *state);
bool nfp_nsp_config_modified(struct nfp_nsp *state);
void nfp_nsp_config_set_modified(struct nfp_nsp *state, bool modified);
void *nfp_nsp_config_entries(struct nfp_nsp *state);
unsigned int nfp_nsp_config_idx(struct nfp_nsp *state);
void nfp_nsp_config_set_state(struct nfp_nsp *state, void *entries,
			      unsigned int idx);
void nfp_nsp_config_clear_state(struct nfp_nsp *state);
int nfp_nsp_read_eth_table(struct nfp_nsp *state, void *buf, unsigned int size);
int nfp_nsp_write_eth_table(struct nfp_nsp *state,
			    const void *buf, unsigned int size);
int nfp_nsp_read_identify(struct nfp_nsp *state, void *buf, unsigned int size);
int nfp_nsp_read_sensors(struct nfp_nsp *state, unsigned int sensor_mask,
			 void *buf, unsigned int size);

/* Implemented in nfp_resource.c */

/* All keys are CRC32-POSIX of the 8-byte identification string */

/* ARM/PCI vNIC Interfaces 0..3 */
#define NFP_RESOURCE_VNIC_PCI_0		"vnic.p0"
#define NFP_RESOURCE_VNIC_PCI_1		"vnic.p1"
#define NFP_RESOURCE_VNIC_PCI_2		"vnic.p2"
#define NFP_RESOURCE_VNIC_PCI_3		"vnic.p3"

/* NFP Hardware Info Database */
#define NFP_RESOURCE_NFP_HWINFO		"nfp.info"

/* Service Processor */
#define NFP_RESOURCE_NSP		"nfp.sp"
#define NFP_RESOURCE_NSP_DIAG		"arm.diag"

/* Netronone Flow Firmware Table */
#define NFP_RESOURCE_NFP_NFFW		"nfp.nffw"

/* MAC Statistics Accumulator */
#define NFP_RESOURCE_MAC_STATISTICS	"mac.stat"

int nfp_resource_table_init(struct nfp_cpp *cpp);

struct nfp_resource *
nfp_resource_acquire(struct nfp_cpp *cpp, const char *name);

void nfp_resource_release(struct nfp_resource *res);

int nfp_resource_wait(struct nfp_cpp *cpp, const char *name, unsigned int secs);

u32 nfp_resource_cpp_id(struct nfp_resource *res);

const char *nfp_resource_name(struct nfp_resource *res);

u64 nfp_resource_address(struct nfp_resource *res);

u64 nfp_resource_size(struct nfp_resource *res);

#endif /* !__NFP_H__ */
