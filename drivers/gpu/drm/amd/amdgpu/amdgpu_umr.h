/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <linux/ioctl.h>

/*
 * MMIO debugfs IOCTL structure
 */
struct amdgpu_debugfs_regs2_iocdata {
	__u32 use_srbm, use_grbm, pg_lock;
	struct {
		__u32 se, sh, instance;
	} grbm;
	struct {
		__u32 me, pipe, queue, vmid;
	} srbm;
};

struct amdgpu_debugfs_regs2_iocdata_v2 {
	__u32 use_srbm, use_grbm, pg_lock;
	struct {
		__u32 se, sh, instance;
	} grbm;
	struct {
		__u32 me, pipe, queue, vmid;
	} srbm;
	u32 xcc_id;
};

struct amdgpu_debugfs_gprwave_iocdata {
	u32 gpr_or_wave, se, sh, cu, wave, simd, xcc_id;
	struct {
		u32 thread, vpgr_or_sgpr;
	} gpr;
};

/*
 * MMIO debugfs state data (per file* handle)
 */
struct amdgpu_debugfs_regs2_data {
	struct amdgpu_device *adev;
	struct mutex lock;
	struct amdgpu_debugfs_regs2_iocdata_v2 id;
};

struct amdgpu_debugfs_gprwave_data {
	struct amdgpu_device *adev;
	struct mutex lock;
	struct amdgpu_debugfs_gprwave_iocdata id;
};

enum AMDGPU_DEBUGFS_REGS2_CMDS {
	AMDGPU_DEBUGFS_REGS2_CMD_SET_STATE=0,
	AMDGPU_DEBUGFS_REGS2_CMD_SET_STATE_V2,
};

enum AMDGPU_DEBUGFS_GPRWAVE_CMDS {
	AMDGPU_DEBUGFS_GPRWAVE_CMD_SET_STATE=0,
};

//reg2 interface
#define AMDGPU_DEBUGFS_REGS2_IOC_SET_STATE _IOWR(0x20, AMDGPU_DEBUGFS_REGS2_CMD_SET_STATE, struct amdgpu_debugfs_regs2_iocdata)
#define AMDGPU_DEBUGFS_REGS2_IOC_SET_STATE_V2 _IOWR(0x20, AMDGPU_DEBUGFS_REGS2_CMD_SET_STATE_V2, struct amdgpu_debugfs_regs2_iocdata_v2)

//gprwave interface
#define AMDGPU_DEBUGFS_GPRWAVE_IOC_SET_STATE _IOWR(0x20, AMDGPU_DEBUGFS_GPRWAVE_CMD_SET_STATE, struct amdgpu_debugfs_gprwave_iocdata)
