// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
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
 */

#include <linux/bsearch.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_pm4_headers_vi.h"
#include "kfd_pm4_headers_aldebaran.h"
#include "cwsr_trap_handler.h"
#include "amdgpu_amdkfd.h"
#include "kfd_smi_events.h"
#include "kfd_svm.h"
#include "kfd_migrate.h"
#include "amdgpu.h"
#include "amdgpu_xcp.h"

#define MQD_SIZE_ALIGNED 768

/*
 * kfd_locked is used to lock the kfd driver during suspend or reset
 * once locked, kfd driver will stop any further GPU execution.
 * create process (open) will return -EAGAIN.
 */
static int kfd_locked;

#ifdef CONFIG_DRM_AMDGPU_CIK
extern const struct kfd2kgd_calls gfx_v7_kfd2kgd;
#endif
extern const struct kfd2kgd_calls gfx_v8_kfd2kgd;
extern const struct kfd2kgd_calls gfx_v9_kfd2kgd;
extern const struct kfd2kgd_calls arcturus_kfd2kgd;
extern const struct kfd2kgd_calls aldebaran_kfd2kgd;
extern const struct kfd2kgd_calls gc_9_4_3_kfd2kgd;
extern const struct kfd2kgd_calls gfx_v10_kfd2kgd;
extern const struct kfd2kgd_calls gfx_v10_3_kfd2kgd;
extern const struct kfd2kgd_calls gfx_v11_kfd2kgd;
extern const struct kfd2kgd_calls gfx_v12_kfd2kgd;

static int kfd_gtt_sa_init(struct kfd_dev *kfd, unsigned int buf_size,
				unsigned int chunk_size);
static void kfd_gtt_sa_fini(struct kfd_dev *kfd);

static int kfd_resume(struct kfd_node *kfd);

static void kfd_device_info_set_sdma_info(struct kfd_dev *kfd)
{
	uint32_t sdma_version = amdgpu_ip_version(kfd->adev, SDMA0_HWIP, 0);

	switch (sdma_version) {
	case IP_VERSION(4, 0, 0):/* VEGA10 */
	case IP_VERSION(4, 0, 1):/* VEGA12 */
	case IP_VERSION(4, 1, 0):/* RAVEN */
	case IP_VERSION(4, 1, 1):/* RAVEN */
	case IP_VERSION(4, 1, 2):/* RENOIR */
	case IP_VERSION(5, 2, 1):/* VANGOGH */
	case IP_VERSION(5, 2, 3):/* YELLOW_CARP */
	case IP_VERSION(5, 2, 6):/* GC 10.3.6 */
	case IP_VERSION(5, 2, 7):/* GC 10.3.7 */
		kfd->device_info.num_sdma_queues_per_engine = 2;
		break;
	case IP_VERSION(4, 2, 0):/* VEGA20 */
	case IP_VERSION(4, 2, 2):/* ARCTURUS */
	case IP_VERSION(4, 4, 0):/* ALDEBARAN */
	case IP_VERSION(4, 4, 2):
	case IP_VERSION(4, 4, 5):
	case IP_VERSION(5, 0, 0):/* NAVI10 */
	case IP_VERSION(5, 0, 1):/* CYAN_SKILLFISH */
	case IP_VERSION(5, 0, 2):/* NAVI14 */
	case IP_VERSION(5, 0, 5):/* NAVI12 */
	case IP_VERSION(5, 2, 0):/* SIENNA_CICHLID */
	case IP_VERSION(5, 2, 2):/* NAVY_FLOUNDER */
	case IP_VERSION(5, 2, 4):/* DIMGREY_CAVEFISH */
	case IP_VERSION(5, 2, 5):/* BEIGE_GOBY */
	case IP_VERSION(6, 0, 0):
	case IP_VERSION(6, 0, 1):
	case IP_VERSION(6, 0, 2):
	case IP_VERSION(6, 0, 3):
	case IP_VERSION(6, 1, 0):
	case IP_VERSION(6, 1, 1):
	case IP_VERSION(6, 1, 2):
	case IP_VERSION(7, 0, 0):
	case IP_VERSION(7, 0, 1):
		kfd->device_info.num_sdma_queues_per_engine = 8;
		break;
	default:
		dev_warn(kfd_device,
			"Default sdma queue per engine(8) is set due to mismatch of sdma ip block(SDMA_HWIP:0x%x).\n",
			sdma_version);
		kfd->device_info.num_sdma_queues_per_engine = 8;
	}

	bitmap_zero(kfd->device_info.reserved_sdma_queues_bitmap, KFD_MAX_SDMA_QUEUES);

	switch (sdma_version) {
	case IP_VERSION(6, 0, 0):
	case IP_VERSION(6, 0, 1):
	case IP_VERSION(6, 0, 2):
	case IP_VERSION(6, 0, 3):
	case IP_VERSION(6, 1, 0):
	case IP_VERSION(6, 1, 1):
	case IP_VERSION(6, 1, 2):
	case IP_VERSION(7, 0, 0):
	case IP_VERSION(7, 0, 1):
		/* Reserve 1 for paging and 1 for gfx */
		kfd->device_info.num_reserved_sdma_queues_per_engine = 2;
		/* BIT(0)=engine-0 queue-0; BIT(1)=engine-1 queue-0; BIT(2)=engine-0 queue-1; ... */
		bitmap_set(kfd->device_info.reserved_sdma_queues_bitmap, 0,
			   kfd->adev->sdma.num_instances *
			   kfd->device_info.num_reserved_sdma_queues_per_engine);
		break;
	default:
		break;
	}
}

static void kfd_device_info_set_event_interrupt_class(struct kfd_dev *kfd)
{
	uint32_t gc_version = KFD_GC_VERSION(kfd);

	switch (gc_version) {
	case IP_VERSION(9, 0, 1): /* VEGA10 */
	case IP_VERSION(9, 1, 0): /* RAVEN */
	case IP_VERSION(9, 2, 1): /* VEGA12 */
	case IP_VERSION(9, 2, 2): /* RAVEN */
	case IP_VERSION(9, 3, 0): /* RENOIR */
	case IP_VERSION(9, 4, 0): /* VEGA20 */
	case IP_VERSION(9, 4, 1): /* ARCTURUS */
	case IP_VERSION(9, 4, 2): /* ALDEBARAN */
		kfd->device_info.event_interrupt_class = &event_interrupt_class_v9;
		break;
	case IP_VERSION(9, 4, 3): /* GC 9.4.3 */
	case IP_VERSION(9, 4, 4): /* GC 9.4.4 */
		kfd->device_info.event_interrupt_class =
						&event_interrupt_class_v9_4_3;
		break;
	case IP_VERSION(10, 3, 1): /* VANGOGH */
	case IP_VERSION(10, 3, 3): /* YELLOW_CARP */
	case IP_VERSION(10, 3, 6): /* GC 10.3.6 */
	case IP_VERSION(10, 3, 7): /* GC 10.3.7 */
	case IP_VERSION(10, 1, 3): /* CYAN_SKILLFISH */
	case IP_VERSION(10, 1, 4):
	case IP_VERSION(10, 1, 10): /* NAVI10 */
	case IP_VERSION(10, 1, 2): /* NAVI12 */
	case IP_VERSION(10, 1, 1): /* NAVI14 */
	case IP_VERSION(10, 3, 0): /* SIENNA_CICHLID */
	case IP_VERSION(10, 3, 2): /* NAVY_FLOUNDER */
	case IP_VERSION(10, 3, 4): /* DIMGREY_CAVEFISH */
	case IP_VERSION(10, 3, 5): /* BEIGE_GOBY */
		kfd->device_info.event_interrupt_class = &event_interrupt_class_v10;
		break;
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 1):
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 3):
	case IP_VERSION(11, 0, 4):
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(11, 5, 1):
	case IP_VERSION(11, 5, 2):
		kfd->device_info.event_interrupt_class = &event_interrupt_class_v11;
		break;
	case IP_VERSION(12, 0, 0):
	case IP_VERSION(12, 0, 1):
		/* GFX12_TODO: Change to v12 version. */
		kfd->device_info.event_interrupt_class = &event_interrupt_class_v11;
		break;
	default:
		dev_warn(kfd_device, "v9 event interrupt handler is set due to "
			"mismatch of gc ip block(GC_HWIP:0x%x).\n", gc_version);
		kfd->device_info.event_interrupt_class = &event_interrupt_class_v9;
	}
}

static void kfd_device_info_init(struct kfd_dev *kfd,
				 bool vf, uint32_t gfx_target_version)
{
	uint32_t gc_version = KFD_GC_VERSION(kfd);
	uint32_t asic_type = kfd->adev->asic_type;

	kfd->device_info.max_pasid_bits = 16;
	kfd->device_info.max_no_of_hqd = 24;
	kfd->device_info.num_of_watch_points = 4;
	kfd->device_info.mqd_size_aligned = MQD_SIZE_ALIGNED;
	kfd->device_info.gfx_target_version = gfx_target_version;

	if (KFD_IS_SOC15(kfd)) {
		kfd->device_info.doorbell_size = 8;
		kfd->device_info.ih_ring_entry_size = 8 * sizeof(uint32_t);
		kfd->device_info.supports_cwsr = true;

		kfd_device_info_set_sdma_info(kfd);

		kfd_device_info_set_event_interrupt_class(kfd);

		if (gc_version < IP_VERSION(11, 0, 0)) {
			/* Navi2x+, Navi1x+ */
			if (gc_version == IP_VERSION(10, 3, 6))
				kfd->device_info.no_atomic_fw_version = 14;
			else if (gc_version == IP_VERSION(10, 3, 7))
				kfd->device_info.no_atomic_fw_version = 3;
			else if (gc_version >= IP_VERSION(10, 3, 0))
				kfd->device_info.no_atomic_fw_version = 92;
			else if (gc_version >= IP_VERSION(10, 1, 1))
				kfd->device_info.no_atomic_fw_version = 145;

			/* Navi1x+ */
			if (gc_version >= IP_VERSION(10, 1, 1))
				kfd->device_info.needs_pci_atomics = true;
		} else if (gc_version < IP_VERSION(12, 0, 0)) {
			/*
			 * PCIe atomics support acknowledgment in GFX11 RS64 CPFW requires
			 * MEC version >= 509. Prior RS64 CPFW versions (and all F32) require
			 * PCIe atomics support.
			 */
			kfd->device_info.needs_pci_atomics = true;
			kfd->device_info.no_atomic_fw_version = kfd->adev->gfx.rs64_enable ? 509 : 0;
		} else {
			kfd->device_info.needs_pci_atomics = true;
		}
	} else {
		kfd->device_info.doorbell_size = 4;
		kfd->device_info.ih_ring_entry_size = 4 * sizeof(uint32_t);
		kfd->device_info.event_interrupt_class = &event_interrupt_class_cik;
		kfd->device_info.num_sdma_queues_per_engine = 2;

		if (asic_type != CHIP_KAVERI &&
		    asic_type != CHIP_HAWAII &&
		    asic_type != CHIP_TONGA)
			kfd->device_info.supports_cwsr = true;

		if (asic_type != CHIP_HAWAII && !vf)
			kfd->device_info.needs_pci_atomics = true;
	}
}

struct kfd_dev *kgd2kfd_probe(struct amdgpu_device *adev, bool vf)
{
	struct kfd_dev *kfd = NULL;
	const struct kfd2kgd_calls *f2g = NULL;
	uint32_t gfx_target_version = 0;

	switch (adev->asic_type) {
#ifdef CONFIG_DRM_AMDGPU_CIK
	case CHIP_KAVERI:
		gfx_target_version = 70000;
		if (!vf)
			f2g = &gfx_v7_kfd2kgd;
		break;
#endif
	case CHIP_CARRIZO:
		gfx_target_version = 80001;
		if (!vf)
			f2g = &gfx_v8_kfd2kgd;
		break;
#ifdef CONFIG_DRM_AMDGPU_CIK
	case CHIP_HAWAII:
		gfx_target_version = 70001;
		if (!amdgpu_exp_hw_support)
			pr_info(
	"KFD support on Hawaii is experimental. See modparam exp_hw_support\n"
				);
		else if (!vf)
			f2g = &gfx_v7_kfd2kgd;
		break;
#endif
	case CHIP_TONGA:
		gfx_target_version = 80002;
		if (!vf)
			f2g = &gfx_v8_kfd2kgd;
		break;
	case CHIP_FIJI:
	case CHIP_POLARIS10:
		gfx_target_version = 80003;
		f2g = &gfx_v8_kfd2kgd;
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		gfx_target_version = 80003;
		if (!vf)
			f2g = &gfx_v8_kfd2kgd;
		break;
	default:
		switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
		/* Vega 10 */
		case IP_VERSION(9, 0, 1):
			gfx_target_version = 90000;
			f2g = &gfx_v9_kfd2kgd;
			break;
		/* Raven */
		case IP_VERSION(9, 1, 0):
		case IP_VERSION(9, 2, 2):
			gfx_target_version = 90002;
			if (!vf)
				f2g = &gfx_v9_kfd2kgd;
			break;
		/* Vega12 */
		case IP_VERSION(9, 2, 1):
			gfx_target_version = 90004;
			if (!vf)
				f2g = &gfx_v9_kfd2kgd;
			break;
		/* Renoir */
		case IP_VERSION(9, 3, 0):
			gfx_target_version = 90012;
			if (!vf)
				f2g = &gfx_v9_kfd2kgd;
			break;
		/* Vega20 */
		case IP_VERSION(9, 4, 0):
			gfx_target_version = 90006;
			if (!vf)
				f2g = &gfx_v9_kfd2kgd;
			break;
		/* Arcturus */
		case IP_VERSION(9, 4, 1):
			gfx_target_version = 90008;
			f2g = &arcturus_kfd2kgd;
			break;
		/* Aldebaran */
		case IP_VERSION(9, 4, 2):
			gfx_target_version = 90010;
			f2g = &aldebaran_kfd2kgd;
			break;
		case IP_VERSION(9, 4, 3):
			gfx_target_version = adev->rev_id >= 1 ? 90402
					   : adev->flags & AMD_IS_APU ? 90400
					   : 90401;
			f2g = &gc_9_4_3_kfd2kgd;
			break;
		case IP_VERSION(9, 4, 4):
			gfx_target_version = 90402;
			f2g = &gc_9_4_3_kfd2kgd;
			break;
		/* Navi10 */
		case IP_VERSION(10, 1, 10):
			gfx_target_version = 100100;
			if (!vf)
				f2g = &gfx_v10_kfd2kgd;
			break;
		/* Navi12 */
		case IP_VERSION(10, 1, 2):
			gfx_target_version = 100101;
			f2g = &gfx_v10_kfd2kgd;
			break;
		/* Navi14 */
		case IP_VERSION(10, 1, 1):
			gfx_target_version = 100102;
			if (!vf)
				f2g = &gfx_v10_kfd2kgd;
			break;
		/* Cyan Skillfish */
		case IP_VERSION(10, 1, 3):
		case IP_VERSION(10, 1, 4):
			gfx_target_version = 100103;
			if (!vf)
				f2g = &gfx_v10_kfd2kgd;
			break;
		/* Sienna Cichlid */
		case IP_VERSION(10, 3, 0):
			gfx_target_version = 100300;
			f2g = &gfx_v10_3_kfd2kgd;
			break;
		/* Navy Flounder */
		case IP_VERSION(10, 3, 2):
			gfx_target_version = 100301;
			f2g = &gfx_v10_3_kfd2kgd;
			break;
		/* Van Gogh */
		case IP_VERSION(10, 3, 1):
			gfx_target_version = 100303;
			if (!vf)
				f2g = &gfx_v10_3_kfd2kgd;
			break;
		/* Dimgrey Cavefish */
		case IP_VERSION(10, 3, 4):
			gfx_target_version = 100302;
			f2g = &gfx_v10_3_kfd2kgd;
			break;
		/* Beige Goby */
		case IP_VERSION(10, 3, 5):
			gfx_target_version = 100304;
			f2g = &gfx_v10_3_kfd2kgd;
			break;
		/* Yellow Carp */
		case IP_VERSION(10, 3, 3):
			gfx_target_version = 100305;
			if (!vf)
				f2g = &gfx_v10_3_kfd2kgd;
			break;
		case IP_VERSION(10, 3, 6):
		case IP_VERSION(10, 3, 7):
			gfx_target_version = 100306;
			if (!vf)
				f2g = &gfx_v10_3_kfd2kgd;
			break;
		case IP_VERSION(11, 0, 0):
			gfx_target_version = 110000;
			f2g = &gfx_v11_kfd2kgd;
			break;
		case IP_VERSION(11, 0, 1):
		case IP_VERSION(11, 0, 4):
			gfx_target_version = 110003;
			f2g = &gfx_v11_kfd2kgd;
			break;
		case IP_VERSION(11, 0, 2):
			gfx_target_version = 110002;
			f2g = &gfx_v11_kfd2kgd;
			break;
		case IP_VERSION(11, 0, 3):
			/* Note: Compiler version is 11.0.1 while HW version is 11.0.3 */
			gfx_target_version = 110001;
			f2g = &gfx_v11_kfd2kgd;
			break;
		case IP_VERSION(11, 5, 0):
			gfx_target_version = 110500;
			f2g = &gfx_v11_kfd2kgd;
			break;
		case IP_VERSION(11, 5, 1):
			gfx_target_version = 110501;
			f2g = &gfx_v11_kfd2kgd;
			break;
		case IP_VERSION(11, 5, 2):
			gfx_target_version = 110502;
			f2g = &gfx_v11_kfd2kgd;
			break;
		case IP_VERSION(12, 0, 0):
			gfx_target_version = 120000;
			f2g = &gfx_v12_kfd2kgd;
			break;
		case IP_VERSION(12, 0, 1):
			gfx_target_version = 120001;
			f2g = &gfx_v12_kfd2kgd;
			break;
		default:
			break;
		}
		break;
	}

	if (!f2g) {
		if (amdgpu_ip_version(adev, GC_HWIP, 0))
			dev_info(kfd_device,
				"GC IP %06x %s not supported in kfd\n",
				amdgpu_ip_version(adev, GC_HWIP, 0),
				vf ? "VF" : "");
		else
			dev_info(kfd_device, "%s %s not supported in kfd\n",
				amdgpu_asic_name[adev->asic_type], vf ? "VF" : "");
		return NULL;
	}

	kfd = kzalloc(sizeof(*kfd), GFP_KERNEL);
	if (!kfd)
		return NULL;

	kfd->adev = adev;
	kfd_device_info_init(kfd, vf, gfx_target_version);
	kfd->init_complete = false;
	kfd->kfd2kgd = f2g;
	atomic_set(&kfd->compute_profile, 0);

	mutex_init(&kfd->doorbell_mutex);

	ida_init(&kfd->doorbell_ida);

	return kfd;
}

static void kfd_cwsr_init(struct kfd_dev *kfd)
{
	if (cwsr_enable && kfd->device_info.supports_cwsr) {
		if (KFD_GC_VERSION(kfd) < IP_VERSION(9, 0, 1)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx8_hex)
					     > KFD_CWSR_TMA_OFFSET);
			kfd->cwsr_isa = cwsr_trap_gfx8_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_gfx8_hex);
		} else if (KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 1)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_arcturus_hex)
					     > KFD_CWSR_TMA_OFFSET);
			kfd->cwsr_isa = cwsr_trap_arcturus_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_arcturus_hex);
		} else if (KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 2)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_aldebaran_hex)
					     > KFD_CWSR_TMA_OFFSET);
			kfd->cwsr_isa = cwsr_trap_aldebaran_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_aldebaran_hex);
		} else if (KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 3) ||
			   KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 4)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx9_4_3_hex)
					     > KFD_CWSR_TMA_OFFSET);
			kfd->cwsr_isa = cwsr_trap_gfx9_4_3_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_gfx9_4_3_hex);
		} else if (KFD_GC_VERSION(kfd) < IP_VERSION(10, 1, 1)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx9_hex)
					     > KFD_CWSR_TMA_OFFSET);
			kfd->cwsr_isa = cwsr_trap_gfx9_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_gfx9_hex);
		} else if (KFD_GC_VERSION(kfd) < IP_VERSION(10, 3, 0)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_nv1x_hex)
					     > KFD_CWSR_TMA_OFFSET);
			kfd->cwsr_isa = cwsr_trap_nv1x_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_nv1x_hex);
		} else if (KFD_GC_VERSION(kfd) < IP_VERSION(11, 0, 0)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx10_hex)
					     > KFD_CWSR_TMA_OFFSET);
			kfd->cwsr_isa = cwsr_trap_gfx10_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_gfx10_hex);
		} else if (KFD_GC_VERSION(kfd) < IP_VERSION(12, 0, 0)) {
			/* The gfx11 cwsr trap handler must fit inside a single
			   page. */
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx11_hex) > PAGE_SIZE);
			kfd->cwsr_isa = cwsr_trap_gfx11_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_gfx11_hex);
		} else {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx12_hex) > PAGE_SIZE);
			kfd->cwsr_isa = cwsr_trap_gfx12_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_gfx12_hex);
		}

		kfd->cwsr_enabled = true;
	}
}

static int kfd_gws_init(struct kfd_node *node)
{
	int ret = 0;
	struct kfd_dev *kfd = node->kfd;
	uint32_t mes_rev = node->adev->mes.sched_version & AMDGPU_MES_VERSION_MASK;

	if (node->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS)
		return 0;

	if (hws_gws_support || (KFD_IS_SOC15(node) &&
		((KFD_GC_VERSION(node) == IP_VERSION(9, 0, 1)
			&& kfd->mec2_fw_version >= 0x81b3) ||
		(KFD_GC_VERSION(node) <= IP_VERSION(9, 4, 0)
			&& kfd->mec2_fw_version >= 0x1b3)  ||
		(KFD_GC_VERSION(node) == IP_VERSION(9, 4, 1)
			&& kfd->mec2_fw_version >= 0x30)   ||
		(KFD_GC_VERSION(node) == IP_VERSION(9, 4, 2)
			&& kfd->mec2_fw_version >= 0x28) ||
		(KFD_GC_VERSION(node) == IP_VERSION(9, 4, 3) ||
		 KFD_GC_VERSION(node) == IP_VERSION(9, 4, 4)) ||
		(KFD_GC_VERSION(node) >= IP_VERSION(10, 3, 0)
			&& KFD_GC_VERSION(node) < IP_VERSION(11, 0, 0)
			&& kfd->mec2_fw_version >= 0x6b) ||
		(KFD_GC_VERSION(node) >= IP_VERSION(11, 0, 0)
			&& KFD_GC_VERSION(node) < IP_VERSION(12, 0, 0)
			&& mes_rev >= 68))))
		ret = amdgpu_amdkfd_alloc_gws(node->adev,
				node->adev->gds.gws_size, &node->gws);

	return ret;
}

static void kfd_smi_init(struct kfd_node *dev)
{
	INIT_LIST_HEAD(&dev->smi_clients);
	spin_lock_init(&dev->smi_lock);
}

static int kfd_init_node(struct kfd_node *node)
{
	int err = -1;

	if (kfd_interrupt_init(node)) {
		dev_err(kfd_device, "Error initializing interrupts\n");
		goto kfd_interrupt_error;
	}

	node->dqm = device_queue_manager_init(node);
	if (!node->dqm) {
		dev_err(kfd_device, "Error initializing queue manager\n");
		goto device_queue_manager_error;
	}

	if (kfd_gws_init(node)) {
		dev_err(kfd_device, "Could not allocate %d gws\n",
			node->adev->gds.gws_size);
		goto gws_error;
	}

	if (kfd_resume(node))
		goto kfd_resume_error;

	if (kfd_topology_add_device(node)) {
		dev_err(kfd_device, "Error adding device to topology\n");
		goto kfd_topology_add_device_error;
	}

	kfd_smi_init(node);

	return 0;

kfd_topology_add_device_error:
kfd_resume_error:
gws_error:
	device_queue_manager_uninit(node->dqm);
device_queue_manager_error:
	kfd_interrupt_exit(node);
kfd_interrupt_error:
	if (node->gws)
		amdgpu_amdkfd_free_gws(node->adev, node->gws);

	/* Cleanup the node memory here */
	kfree(node);
	return err;
}

static void kfd_cleanup_nodes(struct kfd_dev *kfd, unsigned int num_nodes)
{
	struct kfd_node *knode;
	unsigned int i;

	for (i = 0; i < num_nodes; i++) {
		knode = kfd->nodes[i];
		device_queue_manager_uninit(knode->dqm);
		kfd_interrupt_exit(knode);
		kfd_topology_remove_device(knode);
		if (knode->gws)
			amdgpu_amdkfd_free_gws(knode->adev, knode->gws);
		kfree(knode);
		kfd->nodes[i] = NULL;
	}
}

static void kfd_setup_interrupt_bitmap(struct kfd_node *node,
				       unsigned int kfd_node_idx)
{
	struct amdgpu_device *adev = node->adev;
	uint32_t xcc_mask = node->xcc_mask;
	uint32_t xcc, mapped_xcc;
	/*
	 * Interrupt bitmap is setup for processing interrupts from
	 * different XCDs and AIDs.
	 * Interrupt bitmap is defined as follows:
	 * 1. Bits 0-15 - correspond to the NodeId field.
	 *    Each bit corresponds to NodeId number. For example, if
	 *    a KFD node has interrupt bitmap set to 0x7, then this
	 *    KFD node will process interrupts with NodeId = 0, 1 and 2
	 *    in the IH cookie.
	 * 2. Bits 16-31 - unused.
	 *
	 * Please note that the kfd_node_idx argument passed to this
	 * function is not related to NodeId field received in the
	 * IH cookie.
	 *
	 * In CPX mode, a KFD node will process an interrupt if:
	 * - the Node Id matches the corresponding bit set in
	 *   Bits 0-15.
	 * - AND VMID reported in the interrupt lies within the
	 *   VMID range of the node.
	 */
	for_each_inst(xcc, xcc_mask) {
		mapped_xcc = GET_INST(GC, xcc);
		node->interrupt_bitmap |= (mapped_xcc % 2 ? 5 : 3) << (4 * (mapped_xcc / 2));
	}
	dev_info(kfd_device, "Node: %d, interrupt_bitmap: %x\n", kfd_node_idx,
							node->interrupt_bitmap);
}

bool kgd2kfd_device_init(struct kfd_dev *kfd,
			 const struct kgd2kfd_shared_resources *gpu_resources)
{
	unsigned int size, map_process_packet_size, i;
	struct kfd_node *node;
	uint32_t first_vmid_kfd, last_vmid_kfd, vmid_num_kfd;
	unsigned int max_proc_per_quantum;
	int partition_mode;
	int xcp_idx;

	kfd->mec_fw_version = amdgpu_amdkfd_get_fw_version(kfd->adev,
			KGD_ENGINE_MEC1);
	kfd->mec2_fw_version = amdgpu_amdkfd_get_fw_version(kfd->adev,
			KGD_ENGINE_MEC2);
	kfd->sdma_fw_version = amdgpu_amdkfd_get_fw_version(kfd->adev,
			KGD_ENGINE_SDMA1);
	kfd->shared_resources = *gpu_resources;

	kfd->num_nodes = amdgpu_xcp_get_num_xcp(kfd->adev->xcp_mgr);

	if (kfd->num_nodes == 0) {
		dev_err(kfd_device,
			"KFD num nodes cannot be 0, num_xcc_in_node: %d\n",
			kfd->adev->gfx.num_xcc_per_xcp);
		goto out;
	}

	/* Allow BIF to recode atomics to PCIe 3.0 AtomicOps.
	 * 32 and 64-bit requests are possible and must be
	 * supported.
	 */
	kfd->pci_atomic_requested = amdgpu_amdkfd_have_atomics_support(kfd->adev);
	if (!kfd->pci_atomic_requested &&
	    kfd->device_info.needs_pci_atomics &&
	    (!kfd->device_info.no_atomic_fw_version ||
	     kfd->mec_fw_version < kfd->device_info.no_atomic_fw_version)) {
		dev_info(kfd_device,
			 "skipped device %x:%x, PCI rejects atomics %d<%d\n",
			 kfd->adev->pdev->vendor, kfd->adev->pdev->device,
			 kfd->mec_fw_version,
			 kfd->device_info.no_atomic_fw_version);
		return false;
	}

	first_vmid_kfd = ffs(gpu_resources->compute_vmid_bitmap)-1;
	last_vmid_kfd = fls(gpu_resources->compute_vmid_bitmap)-1;
	vmid_num_kfd = last_vmid_kfd - first_vmid_kfd + 1;

	/* For GFX9.4.3, we need special handling for VMIDs depending on
	 * partition mode.
	 * In CPX mode, the VMID range needs to be shared between XCDs.
	 * Additionally, there are 13 VMIDs (3-15) available for KFD. To
	 * divide them equally, we change starting VMID to 4 and not use
	 * VMID 3.
	 * If the VMID range changes for GFX9.4.3, then this code MUST be
	 * revisited.
	 */
	if (kfd->adev->xcp_mgr) {
		partition_mode = amdgpu_xcp_query_partition_mode(kfd->adev->xcp_mgr,
								 AMDGPU_XCP_FL_LOCKED);
		if (partition_mode == AMDGPU_CPX_PARTITION_MODE &&
		    kfd->num_nodes != 1) {
			vmid_num_kfd /= 2;
			first_vmid_kfd = last_vmid_kfd + 1 - vmid_num_kfd*2;
		}
	}

	/* Verify module parameters regarding mapped process number*/
	if (hws_max_conc_proc >= 0)
		max_proc_per_quantum = min((u32)hws_max_conc_proc, vmid_num_kfd);
	else
		max_proc_per_quantum = vmid_num_kfd;

	/* calculate max size of mqds needed for queues */
	size = max_num_of_queues_per_device *
			kfd->device_info.mqd_size_aligned;

	/*
	 * calculate max size of runlist packet.
	 * There can be only 2 packets at once
	 */
	map_process_packet_size = KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 2) ?
				sizeof(struct pm4_mes_map_process_aldebaran) :
				sizeof(struct pm4_mes_map_process);
	size += (KFD_MAX_NUM_OF_PROCESSES * map_process_packet_size +
		max_num_of_queues_per_device * sizeof(struct pm4_mes_map_queues)
		+ sizeof(struct pm4_mes_runlist)) * 2;

	/* Add size of HIQ & DIQ */
	size += KFD_KERNEL_QUEUE_SIZE * 2;

	/* add another 512KB for all other allocations on gart (HPD, fences) */
	size += 512 * 1024;

	if (amdgpu_amdkfd_alloc_gtt_mem(
			kfd->adev, size, &kfd->gtt_mem,
			&kfd->gtt_start_gpu_addr, &kfd->gtt_start_cpu_ptr,
			false)) {
		dev_err(kfd_device, "Could not allocate %d bytes\n", size);
		goto alloc_gtt_mem_failure;
	}

	dev_info(kfd_device, "Allocated %d bytes on gart\n", size);

	/* Initialize GTT sa with 512 byte chunk size */
	if (kfd_gtt_sa_init(kfd, size, 512) != 0) {
		dev_err(kfd_device, "Error initializing gtt sub-allocator\n");
		goto kfd_gtt_sa_init_error;
	}

	if (kfd_doorbell_init(kfd)) {
		dev_err(kfd_device,
			"Error initializing doorbell aperture\n");
		goto kfd_doorbell_error;
	}

	if (amdgpu_use_xgmi_p2p)
		kfd->hive_id = kfd->adev->gmc.xgmi.hive_id;

	/*
	 * For GFX9.4.3, the KFD abstracts all partitions within a socket as
	 * xGMI connected in the topology so assign a unique hive id per
	 * device based on the pci device location if device is in PCIe mode.
	 */
	if (!kfd->hive_id &&
	    (KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 3) ||
	     KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 4)) &&
	    kfd->num_nodes > 1)
		kfd->hive_id = pci_dev_id(kfd->adev->pdev);

	kfd->noretry = kfd->adev->gmc.noretry;

	kfd_cwsr_init(kfd);

	dev_info(kfd_device, "Total number of KFD nodes to be created: %d\n",
				kfd->num_nodes);

	/* Allocate the KFD nodes */
	for (i = 0, xcp_idx = 0; i < kfd->num_nodes; i++) {
		node = kzalloc(sizeof(struct kfd_node), GFP_KERNEL);
		if (!node)
			goto node_alloc_error;

		node->node_id = i;
		node->adev = kfd->adev;
		node->kfd = kfd;
		node->kfd2kgd = kfd->kfd2kgd;
		node->vm_info.vmid_num_kfd = vmid_num_kfd;
		node->xcp = amdgpu_get_next_xcp(kfd->adev->xcp_mgr, &xcp_idx);
		/* TODO : Check if error handling is needed */
		if (node->xcp) {
			amdgpu_xcp_get_inst_details(node->xcp, AMDGPU_XCP_GFX,
						    &node->xcc_mask);
			++xcp_idx;
		} else {
			node->xcc_mask =
				(1U << NUM_XCC(kfd->adev->gfx.xcc_mask)) - 1;
		}

		if (node->xcp) {
			dev_info(kfd_device, "KFD node %d partition %d size %lldM\n",
				node->node_id, node->xcp->mem_id,
				KFD_XCP_MEMORY_SIZE(node->adev, node->node_id) >> 20);
		}

		if ((KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 3) ||
		     KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 4)) &&
		    partition_mode == AMDGPU_CPX_PARTITION_MODE &&
		    kfd->num_nodes != 1) {
			/* For GFX9.4.3 and CPX mode, first XCD gets VMID range
			 * 4-9 and second XCD gets VMID range 10-15.
			 */

			node->vm_info.first_vmid_kfd = (i%2 == 0) ?
						first_vmid_kfd :
						first_vmid_kfd+vmid_num_kfd;
			node->vm_info.last_vmid_kfd = (i%2 == 0) ?
						last_vmid_kfd-vmid_num_kfd :
						last_vmid_kfd;
			node->compute_vmid_bitmap =
				((0x1 << (node->vm_info.last_vmid_kfd + 1)) - 1) -
				((0x1 << (node->vm_info.first_vmid_kfd)) - 1);
		} else {
			node->vm_info.first_vmid_kfd = first_vmid_kfd;
			node->vm_info.last_vmid_kfd = last_vmid_kfd;
			node->compute_vmid_bitmap =
				gpu_resources->compute_vmid_bitmap;
		}
		node->max_proc_per_quantum = max_proc_per_quantum;
		atomic_set(&node->sram_ecc_flag, 0);

		amdgpu_amdkfd_get_local_mem_info(kfd->adev,
					&node->local_mem_info, node->xcp);

		if (KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 3) ||
		    KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 4))
			kfd_setup_interrupt_bitmap(node, i);

		/* Initialize the KFD node */
		if (kfd_init_node(node)) {
			dev_err(kfd_device, "Error initializing KFD node\n");
			goto node_init_error;
		}
		kfd->nodes[i] = node;
	}

	svm_range_set_max_pages(kfd->adev);

	spin_lock_init(&kfd->watch_points_lock);

	kfd->init_complete = true;
	dev_info(kfd_device, "added device %x:%x\n", kfd->adev->pdev->vendor,
		 kfd->adev->pdev->device);

	pr_debug("Starting kfd with the following scheduling policy %d\n",
		node->dqm->sched_policy);

	goto out;

node_init_error:
node_alloc_error:
	kfd_cleanup_nodes(kfd, i);
	kfd_doorbell_fini(kfd);
kfd_doorbell_error:
	kfd_gtt_sa_fini(kfd);
kfd_gtt_sa_init_error:
	amdgpu_amdkfd_free_gtt_mem(kfd->adev, &kfd->gtt_mem);
alloc_gtt_mem_failure:
	dev_err(kfd_device,
		"device %x:%x NOT added due to errors\n",
		kfd->adev->pdev->vendor, kfd->adev->pdev->device);
out:
	return kfd->init_complete;
}

void kgd2kfd_device_exit(struct kfd_dev *kfd)
{
	if (kfd->init_complete) {
		/* Cleanup KFD nodes */
		kfd_cleanup_nodes(kfd, kfd->num_nodes);
		/* Cleanup common/shared resources */
		kfd_doorbell_fini(kfd);
		ida_destroy(&kfd->doorbell_ida);
		kfd_gtt_sa_fini(kfd);
		amdgpu_amdkfd_free_gtt_mem(kfd->adev, &kfd->gtt_mem);
	}

	kfree(kfd);
}

int kgd2kfd_pre_reset(struct kfd_dev *kfd,
		      struct amdgpu_reset_context *reset_context)
{
	struct kfd_node *node;
	int i;

	if (!kfd->init_complete)
		return 0;

	for (i = 0; i < kfd->num_nodes; i++) {
		node = kfd->nodes[i];
		kfd_smi_event_update_gpu_reset(node, false, reset_context);
	}

	kgd2kfd_suspend(kfd, false);

	for (i = 0; i < kfd->num_nodes; i++)
		kfd_signal_reset_event(kfd->nodes[i]);

	return 0;
}

/*
 * Fix me. KFD won't be able to resume existing process for now.
 * We will keep all existing process in a evicted state and
 * wait the process to be terminated.
 */

int kgd2kfd_post_reset(struct kfd_dev *kfd)
{
	int ret;
	struct kfd_node *node;
	int i;

	if (!kfd->init_complete)
		return 0;

	for (i = 0; i < kfd->num_nodes; i++) {
		ret = kfd_resume(kfd->nodes[i]);
		if (ret)
			return ret;
	}

	mutex_lock(&kfd_processes_mutex);
	--kfd_locked;
	mutex_unlock(&kfd_processes_mutex);

	for (i = 0; i < kfd->num_nodes; i++) {
		node = kfd->nodes[i];
		atomic_set(&node->sram_ecc_flag, 0);
		kfd_smi_event_update_gpu_reset(node, true, NULL);
	}

	return 0;
}

bool kfd_is_locked(void)
{
	lockdep_assert_held(&kfd_processes_mutex);
	return  (kfd_locked > 0);
}

void kgd2kfd_suspend(struct kfd_dev *kfd, bool run_pm)
{
	struct kfd_node *node;
	int i;

	if (!kfd->init_complete)
		return;

	/* for runtime suspend, skip locking kfd */
	if (!run_pm) {
		mutex_lock(&kfd_processes_mutex);
		/* For first KFD device suspend all the KFD processes */
		if (++kfd_locked == 1)
			kfd_suspend_all_processes();
		mutex_unlock(&kfd_processes_mutex);
	}

	for (i = 0; i < kfd->num_nodes; i++) {
		node = kfd->nodes[i];
		node->dqm->ops.stop(node->dqm);
	}
}

int kgd2kfd_resume(struct kfd_dev *kfd, bool run_pm)
{
	int ret, i;

	if (!kfd->init_complete)
		return 0;

	for (i = 0; i < kfd->num_nodes; i++) {
		ret = kfd_resume(kfd->nodes[i]);
		if (ret)
			return ret;
	}

	/* for runtime resume, skip unlocking kfd */
	if (!run_pm) {
		mutex_lock(&kfd_processes_mutex);
		if (--kfd_locked == 0)
			ret = kfd_resume_all_processes();
		WARN_ONCE(kfd_locked < 0, "KFD suspend / resume ref. error");
		mutex_unlock(&kfd_processes_mutex);
	}

	return ret;
}

static int kfd_resume(struct kfd_node *node)
{
	int err = 0;

	err = node->dqm->ops.start(node->dqm);
	if (err)
		dev_err(kfd_device,
			"Error starting queue manager for device %x:%x\n",
			node->adev->pdev->vendor, node->adev->pdev->device);

	return err;
}

static inline void kfd_queue_work(struct workqueue_struct *wq,
				  struct work_struct *work)
{
	int cpu, new_cpu;

	cpu = new_cpu = smp_processor_id();
	do {
		new_cpu = cpumask_next(new_cpu, cpu_online_mask) % nr_cpu_ids;
		if (cpu_to_node(new_cpu) == numa_node_id())
			break;
	} while (cpu != new_cpu);

	queue_work_on(new_cpu, wq, work);
}

/* This is called directly from KGD at ISR. */
void kgd2kfd_interrupt(struct kfd_dev *kfd, const void *ih_ring_entry)
{
	uint32_t patched_ihre[KFD_MAX_RING_ENTRY_SIZE], i;
	bool is_patched = false;
	unsigned long flags;
	struct kfd_node *node;

	if (!kfd->init_complete)
		return;

	if (kfd->device_info.ih_ring_entry_size > sizeof(patched_ihre)) {
		dev_err_once(kfd_device, "Ring entry too small\n");
		return;
	}

	for (i = 0; i < kfd->num_nodes; i++) {
		node = kfd->nodes[i];
		spin_lock_irqsave(&node->interrupt_lock, flags);

		if (node->interrupts_active
		    && interrupt_is_wanted(node, ih_ring_entry,
			    	patched_ihre, &is_patched)
		    && enqueue_ih_ring_entry(node,
			    	is_patched ? patched_ihre : ih_ring_entry)) {
			kfd_queue_work(node->ih_wq, &node->interrupt_work);
			spin_unlock_irqrestore(&node->interrupt_lock, flags);
			return;
		}
		spin_unlock_irqrestore(&node->interrupt_lock, flags);
	}

}

int kgd2kfd_quiesce_mm(struct mm_struct *mm, uint32_t trigger)
{
	struct kfd_process *p;
	int r;

	/* Because we are called from arbitrary context (workqueue) as opposed
	 * to process context, kfd_process could attempt to exit while we are
	 * running so the lookup function increments the process ref count.
	 */
	p = kfd_lookup_process_by_mm(mm);
	if (!p)
		return -ESRCH;

	WARN(debug_evictions, "Evicting pid %d", p->lead_thread->pid);
	r = kfd_process_evict_queues(p, trigger);

	kfd_unref_process(p);
	return r;
}

int kgd2kfd_resume_mm(struct mm_struct *mm)
{
	struct kfd_process *p;
	int r;

	/* Because we are called from arbitrary context (workqueue) as opposed
	 * to process context, kfd_process could attempt to exit while we are
	 * running so the lookup function increments the process ref count.
	 */
	p = kfd_lookup_process_by_mm(mm);
	if (!p)
		return -ESRCH;

	r = kfd_process_restore_queues(p);

	kfd_unref_process(p);
	return r;
}

/** kgd2kfd_schedule_evict_and_restore_process - Schedules work queue that will
 *   prepare for safe eviction of KFD BOs that belong to the specified
 *   process.
 *
 * @mm: mm_struct that identifies the specified KFD process
 * @fence: eviction fence attached to KFD process BOs
 *
 */
int kgd2kfd_schedule_evict_and_restore_process(struct mm_struct *mm,
					       struct dma_fence *fence)
{
	struct kfd_process *p;
	unsigned long active_time;
	unsigned long delay_jiffies = msecs_to_jiffies(PROCESS_ACTIVE_TIME_MS);

	if (!fence)
		return -EINVAL;

	if (dma_fence_is_signaled(fence))
		return 0;

	p = kfd_lookup_process_by_mm(mm);
	if (!p)
		return -ENODEV;

	if (fence->seqno == p->last_eviction_seqno)
		goto out;

	p->last_eviction_seqno = fence->seqno;

	/* Avoid KFD process starvation. Wait for at least
	 * PROCESS_ACTIVE_TIME_MS before evicting the process again
	 */
	active_time = get_jiffies_64() - p->last_restore_timestamp;
	if (delay_jiffies > active_time)
		delay_jiffies -= active_time;
	else
		delay_jiffies = 0;

	/* During process initialization eviction_work.dwork is initialized
	 * to kfd_evict_bo_worker
	 */
	WARN(debug_evictions, "Scheduling eviction of pid %d in %ld jiffies",
	     p->lead_thread->pid, delay_jiffies);
	schedule_delayed_work(&p->eviction_work, delay_jiffies);
out:
	kfd_unref_process(p);
	return 0;
}

static int kfd_gtt_sa_init(struct kfd_dev *kfd, unsigned int buf_size,
				unsigned int chunk_size)
{
	if (WARN_ON(buf_size < chunk_size))
		return -EINVAL;
	if (WARN_ON(buf_size == 0))
		return -EINVAL;
	if (WARN_ON(chunk_size == 0))
		return -EINVAL;

	kfd->gtt_sa_chunk_size = chunk_size;
	kfd->gtt_sa_num_of_chunks = buf_size / chunk_size;

	kfd->gtt_sa_bitmap = bitmap_zalloc(kfd->gtt_sa_num_of_chunks,
					   GFP_KERNEL);
	if (!kfd->gtt_sa_bitmap)
		return -ENOMEM;

	pr_debug("gtt_sa_num_of_chunks = %d, gtt_sa_bitmap = %p\n",
			kfd->gtt_sa_num_of_chunks, kfd->gtt_sa_bitmap);

	mutex_init(&kfd->gtt_sa_lock);

	return 0;
}

static void kfd_gtt_sa_fini(struct kfd_dev *kfd)
{
	mutex_destroy(&kfd->gtt_sa_lock);
	bitmap_free(kfd->gtt_sa_bitmap);
}

static inline uint64_t kfd_gtt_sa_calc_gpu_addr(uint64_t start_addr,
						unsigned int bit_num,
						unsigned int chunk_size)
{
	return start_addr + bit_num * chunk_size;
}

static inline uint32_t *kfd_gtt_sa_calc_cpu_addr(void *start_addr,
						unsigned int bit_num,
						unsigned int chunk_size)
{
	return (uint32_t *) ((uint64_t) start_addr + bit_num * chunk_size);
}

int kfd_gtt_sa_allocate(struct kfd_node *node, unsigned int size,
			struct kfd_mem_obj **mem_obj)
{
	unsigned int found, start_search, cur_size;
	struct kfd_dev *kfd = node->kfd;

	if (size == 0)
		return -EINVAL;

	if (size > kfd->gtt_sa_num_of_chunks * kfd->gtt_sa_chunk_size)
		return -ENOMEM;

	*mem_obj = kzalloc(sizeof(struct kfd_mem_obj), GFP_KERNEL);
	if (!(*mem_obj))
		return -ENOMEM;

	pr_debug("Allocated mem_obj = %p for size = %d\n", *mem_obj, size);

	start_search = 0;

	mutex_lock(&kfd->gtt_sa_lock);

kfd_gtt_restart_search:
	/* Find the first chunk that is free */
	found = find_next_zero_bit(kfd->gtt_sa_bitmap,
					kfd->gtt_sa_num_of_chunks,
					start_search);

	pr_debug("Found = %d\n", found);

	/* If there wasn't any free chunk, bail out */
	if (found == kfd->gtt_sa_num_of_chunks)
		goto kfd_gtt_no_free_chunk;

	/* Update fields of mem_obj */
	(*mem_obj)->range_start = found;
	(*mem_obj)->range_end = found;
	(*mem_obj)->gpu_addr = kfd_gtt_sa_calc_gpu_addr(
					kfd->gtt_start_gpu_addr,
					found,
					kfd->gtt_sa_chunk_size);
	(*mem_obj)->cpu_ptr = kfd_gtt_sa_calc_cpu_addr(
					kfd->gtt_start_cpu_ptr,
					found,
					kfd->gtt_sa_chunk_size);

	pr_debug("gpu_addr = %p, cpu_addr = %p\n",
			(uint64_t *) (*mem_obj)->gpu_addr, (*mem_obj)->cpu_ptr);

	/* If we need only one chunk, mark it as allocated and get out */
	if (size <= kfd->gtt_sa_chunk_size) {
		pr_debug("Single bit\n");
		__set_bit(found, kfd->gtt_sa_bitmap);
		goto kfd_gtt_out;
	}

	/* Otherwise, try to see if we have enough contiguous chunks */
	cur_size = size - kfd->gtt_sa_chunk_size;
	do {
		(*mem_obj)->range_end =
			find_next_zero_bit(kfd->gtt_sa_bitmap,
					kfd->gtt_sa_num_of_chunks, ++found);
		/*
		 * If next free chunk is not contiguous than we need to
		 * restart our search from the last free chunk we found (which
		 * wasn't contiguous to the previous ones
		 */
		if ((*mem_obj)->range_end != found) {
			start_search = found;
			goto kfd_gtt_restart_search;
		}

		/*
		 * If we reached end of buffer, bail out with error
		 */
		if (found == kfd->gtt_sa_num_of_chunks)
			goto kfd_gtt_no_free_chunk;

		/* Check if we don't need another chunk */
		if (cur_size <= kfd->gtt_sa_chunk_size)
			cur_size = 0;
		else
			cur_size -= kfd->gtt_sa_chunk_size;

	} while (cur_size > 0);

	pr_debug("range_start = %d, range_end = %d\n",
		(*mem_obj)->range_start, (*mem_obj)->range_end);

	/* Mark the chunks as allocated */
	bitmap_set(kfd->gtt_sa_bitmap, (*mem_obj)->range_start,
		   (*mem_obj)->range_end - (*mem_obj)->range_start + 1);

kfd_gtt_out:
	mutex_unlock(&kfd->gtt_sa_lock);
	return 0;

kfd_gtt_no_free_chunk:
	pr_debug("Allocation failed with mem_obj = %p\n", *mem_obj);
	mutex_unlock(&kfd->gtt_sa_lock);
	kfree(*mem_obj);
	return -ENOMEM;
}

int kfd_gtt_sa_free(struct kfd_node *node, struct kfd_mem_obj *mem_obj)
{
	struct kfd_dev *kfd = node->kfd;

	/* Act like kfree when trying to free a NULL object */
	if (!mem_obj)
		return 0;

	pr_debug("Free mem_obj = %p, range_start = %d, range_end = %d\n",
			mem_obj, mem_obj->range_start, mem_obj->range_end);

	mutex_lock(&kfd->gtt_sa_lock);

	/* Mark the chunks as free */
	bitmap_clear(kfd->gtt_sa_bitmap, mem_obj->range_start,
		     mem_obj->range_end - mem_obj->range_start + 1);

	mutex_unlock(&kfd->gtt_sa_lock);

	kfree(mem_obj);
	return 0;
}

void kgd2kfd_set_sram_ecc_flag(struct kfd_dev *kfd)
{
	/*
	 * TODO: Currently update SRAM ECC flag for first node.
	 * This needs to be updated later when we can
	 * identify SRAM ECC error on other nodes also.
	 */
	if (kfd)
		atomic_inc(&kfd->nodes[0]->sram_ecc_flag);
}

void kfd_inc_compute_active(struct kfd_node *node)
{
	if (atomic_inc_return(&node->kfd->compute_profile) == 1)
		amdgpu_amdkfd_set_compute_idle(node->adev, false);
}

void kfd_dec_compute_active(struct kfd_node *node)
{
	int count = atomic_dec_return(&node->kfd->compute_profile);

	if (count == 0)
		amdgpu_amdkfd_set_compute_idle(node->adev, true);
	WARN_ONCE(count < 0, "Compute profile ref. count error");
}

void kgd2kfd_smi_event_throttle(struct kfd_dev *kfd, uint64_t throttle_bitmask)
{
	/*
	 * TODO: For now, raise the throttling event only on first node.
	 * This will need to change after we are able to determine
	 * which node raised the throttling event.
	 */
	if (kfd && kfd->init_complete)
		kfd_smi_event_update_thermal_throttling(kfd->nodes[0],
							throttle_bitmask);
}

/* kfd_get_num_sdma_engines returns the number of PCIe optimized SDMA and
 * kfd_get_num_xgmi_sdma_engines returns the number of XGMI SDMA.
 * When the device has more than two engines, we reserve two for PCIe to enable
 * full-duplex and the rest are used as XGMI.
 */
unsigned int kfd_get_num_sdma_engines(struct kfd_node *node)
{
	/* If XGMI is not supported, all SDMA engines are PCIe */
	if (!node->adev->gmc.xgmi.supported)
		return node->adev->sdma.num_instances/(int)node->kfd->num_nodes;

	return min(node->adev->sdma.num_instances/(int)node->kfd->num_nodes, 2);
}

unsigned int kfd_get_num_xgmi_sdma_engines(struct kfd_node *node)
{
	/* After reserved for PCIe, the rest of engines are XGMI */
	return node->adev->sdma.num_instances/(int)node->kfd->num_nodes -
		kfd_get_num_sdma_engines(node);
}

int kgd2kfd_check_and_lock_kfd(void)
{
	mutex_lock(&kfd_processes_mutex);
	if (!hash_empty(kfd_processes_table) || kfd_is_locked()) {
		mutex_unlock(&kfd_processes_mutex);
		return -EBUSY;
	}

	++kfd_locked;
	mutex_unlock(&kfd_processes_mutex);

	return 0;
}

void kgd2kfd_unlock_kfd(void)
{
	mutex_lock(&kfd_processes_mutex);
	--kfd_locked;
	mutex_unlock(&kfd_processes_mutex);
}

#if defined(CONFIG_DEBUG_FS)

/* This function will send a package to HIQ to hang the HWS
 * which will trigger a GPU reset and bring the HWS back to normal state
 */
int kfd_debugfs_hang_hws(struct kfd_node *dev)
{
	if (dev->dqm->sched_policy != KFD_SCHED_POLICY_HWS) {
		pr_err("HWS is not enabled");
		return -EINVAL;
	}

	return dqm_debugfs_hang_hws(dev->dqm);
}

#endif
