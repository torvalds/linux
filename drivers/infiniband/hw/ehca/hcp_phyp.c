/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *   load store abstraction for ehca register access with tracing
 *
 *  Authors: Christoph Raisch <raisch@de.ibm.com>
 *           Hoang-Nam Nguyen <hnguyen@de.ibm.com>
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

#include "ehca_classes.h"
#include "hipz_hw.h"

int hcall_map_page(u64 physaddr, u64 *mapaddr)
{
	*mapaddr = (u64)(ioremap(physaddr, EHCA_PAGESIZE));
	return 0;
}

int hcall_unmap_page(u64 mapaddr)
{
	iounmap((volatile void __iomem*)mapaddr);
	return 0;
}

int hcp_galpas_ctor(struct h_galpas *galpas,
		    u64 paddr_kernel, u64 paddr_user)
{
	int ret = hcall_map_page(paddr_kernel, &galpas->kernel.fw_handle);
	if (ret)
		return ret;

	galpas->user.fw_handle = paddr_user;

	return 0;
}

int hcp_galpas_dtor(struct h_galpas *galpas)
{
	if (galpas->kernel.fw_handle) {
		int ret = hcall_unmap_page(galpas->kernel.fw_handle);
		if (ret)
			return ret;
	}

	galpas->user.fw_handle = galpas->kernel.fw_handle = 0;

	return 0;
}
