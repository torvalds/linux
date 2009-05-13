/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  Firmware calls
 *
 *  Authors: Christoph Raisch <raisch@de.ibm.com>
 *           Hoang-Nam Nguyen <hnguyen@de.ibm.com>
 *           Waleri Fomin <fomin@de.ibm.com>
 *           Gerd Bayer <gerd.bayer@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __HCP_PHYP_H__
#define __HCP_PHYP_H__


/*
 * eHCA page (mapped into memory)
 * resource to access eHCA register pages in CPU address space
*/
struct h_galpa {
	u64 fw_handle;
	/* for pSeries this is a 64bit memory address where
	   I/O memory is mapped into CPU address space (kv) */
};

/*
 * resource to access eHCA address space registers, all types
 */
struct h_galpas {
	u32 pid;		/*PID of userspace galpa checking */
	struct h_galpa user;	/* user space accessible resource,
				   set to 0 if unused */
	struct h_galpa kernel;	/* kernel space accessible resource,
				   set to 0 if unused */
};

static inline u64 hipz_galpa_load(struct h_galpa galpa, u32 offset)
{
	u64 addr = galpa.fw_handle + offset;
	return *(volatile u64 __force *)addr;
}

static inline void hipz_galpa_store(struct h_galpa galpa, u32 offset, u64 value)
{
	u64 addr = galpa.fw_handle + offset;
	*(volatile u64 __force *)addr = value;
}

int hcp_galpas_ctor(struct h_galpas *galpas, int is_user,
		    u64 paddr_kernel, u64 paddr_user);

int hcp_galpas_dtor(struct h_galpas *galpas);

int hcall_map_page(u64 physaddr, u64 * mapaddr);

int hcall_unmap_page(u64 mapaddr);

#endif
