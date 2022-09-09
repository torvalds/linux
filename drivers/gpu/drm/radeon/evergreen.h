/* radeon_evergreen.h -- Private header for radeon driver -*- linux-c -*-
 *
 * Copyright 2010 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __RADEON_EVERGREEN_H__
#define __RADEON_EVERGREEN_H__

struct evergreen_mc_save;
struct evergreen_power_info;
struct radeon_device;

bool evergreen_is_display_hung(struct radeon_device *rdev);
void evergreen_print_gpu_status_regs(struct radeon_device *rdev);
void evergreen_mc_stop(struct radeon_device *rdev, struct evergreen_mc_save *save);
void evergreen_mc_resume(struct radeon_device *rdev, struct evergreen_mc_save *save);
int evergreen_mc_wait_for_idle(struct radeon_device *rdev);
void evergreen_mc_program(struct radeon_device *rdev);
void evergreen_irq_suspend(struct radeon_device *rdev);
int evergreen_mc_init(struct radeon_device *rdev);
void evergreen_fix_pci_max_read_req_size(struct radeon_device *rdev);
void evergreen_pcie_gen2_enable(struct radeon_device *rdev);
void evergreen_program_aspm(struct radeon_device *rdev);
void sumo_rlc_fini(struct radeon_device *rdev);
int sumo_rlc_init(struct radeon_device *rdev);
void evergreen_gpu_pci_config_reset(struct radeon_device *rdev);
u32 evergreen_get_number_of_dram_channels(struct radeon_device *rdev);
u32 evergreen_gpu_check_soft_reset(struct radeon_device *rdev);
int evergreen_rlc_resume(struct radeon_device *rdev);
struct evergreen_power_info *evergreen_get_pi(struct radeon_device *rdev);

#endif				/* __RADEON_EVERGREEN_H__ */
