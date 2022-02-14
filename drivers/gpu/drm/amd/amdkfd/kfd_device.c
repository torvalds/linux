/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
#include "kfd_iommu.h"
#include "amdgpu_amdkfd.h"
#include "kfd_smi_events.h"
#include "kfd_migrate.h"
#include "amdgpu.h"

#define MQD_SIZE_ALIGNED 768

/*
 * kfd_locked is used to lock the kfd driver during suspend or reset
 * once locked, kfd driver will stop any further GPU execution.
 * create process (open) will return -EAGAIN.
 */
static atomic_t kfd_locked = ATOMIC_INIT(0);

#ifdef CONFIG_DRM_AMDGPU_CIK
extern const struct kfd2kgd_calls gfx_v7_kfd2kgd;
#endif
extern const struct kfd2kgd_calls gfx_v8_kfd2kgd;
extern const struct kfd2kgd_calls gfx_v9_kfd2kgd;
extern const struct kfd2kgd_calls arcturus_kfd2kgd;
extern const struct kfd2kgd_calls aldebaran_kfd2kgd;
extern const struct kfd2kgd_calls gfx_v10_kfd2kgd;
extern const struct kfd2kgd_calls gfx_v10_3_kfd2kgd;

static int kfd_gtt_sa_init(struct kfd_dev *kfd, unsigned int buf_size,
				unsigned int chunk_size);
static void kfd_gtt_sa_fini(struct kfd_dev *kfd);

static int kfd_resume(struct kfd_dev *kfd);

static void kfd_device_info_set_sdma_queue_num(struct kfd_dev *kfd)
{
	uint32_t sdma_version = kfd->adev->ip_versions[SDMA0_HWIP][0];

	switch (sdma_version) {
	case IP_VERSION(4, 0, 0):/* VEGA10 */
	case IP_VERSION(4, 0, 1):/* VEGA12 */
	case IP_VERSION(4, 1, 0):/* RAVEN */
	case IP_VERSION(4, 1, 1):/* RAVEN */
	case IP_VERSION(4, 1, 2):/* RENOIR */
	case IP_VERSION(5, 2, 1):/* VANGOGH */
	case IP_VERSION(5, 2, 3):/* YELLOW_CARP */
		kfd->device_info.num_sdma_queues_per_engine = 2;
		break;
	case IP_VERSION(4, 2, 0):/* VEGA20 */
	case IP_VERSION(4, 2, 2):/* ARCTURUS */
	case IP_VERSION(4, 4, 0):/* ALDEBARAN */
	case IP_VERSION(5, 0, 0):/* NAVI10 */
	case IP_VERSION(5, 0, 1):/* CYAN_SKILLFISH */
	case IP_VERSION(5, 0, 2):/* NAVI14 */
	case IP_VERSION(5, 0, 5):/* NAVI12 */
	case IP_VERSION(5, 2, 0):/* SIENNA_CICHLID */
	case IP_VERSION(5, 2, 2):/* NAVY_FLOUNDER */
	case IP_VERSION(5, 2, 4):/* DIMGREY_CAVEFISH */
	case IP_VERSION(5, 2, 5):/* BEIGE_GOBY */
		kfd->device_info.num_sdma_queues_per_engine = 8;
		break;
	default:
		dev_warn(kfd_device,
			"Default sdma queue per engine(8) is set due to mismatch of sdma ip block(SDMA_HWIP:0x%x).\n",
			sdma_version);
		kfd->device_info.num_sdma_queues_per_engine = 8;
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
	case IP_VERSION(10, 3, 1): /* VANGOGH */
	case IP_VERSION(10, 3, 3): /* YELLOW_CARP */
	case IP_VERSION(10, 1, 3): /* CYAN_SKILLFISH */
	case IP_VERSION(10, 1, 4):
	case IP_VERSION(10, 1, 10): /* NAVI10 */
	case IP_VERSION(10, 1, 2): /* NAVI12 */
	case IP_VERSION(10, 1, 1): /* NAVI14 */
	case IP_VERSION(10, 3, 0): /* SIENNA_CICHLID */
	case IP_VERSION(10, 3, 2): /* NAVY_FLOUNDER */
	case IP_VERSION(10, 3, 4): /* DIMGREY_CAVEFISH */
	case IP_VERSION(10, 3, 5): /* BEIGE_GOBY */
		kfd->device_info.event_interrupt_class = &event_interrupt_class_v9;
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

		kfd_device_info_set_sdma_queue_num(kfd);

		kfd_device_info_set_event_interrupt_class(kfd);

		/* Raven */
		if (gc_version == IP_VERSION(9, 1, 0) ||
		    gc_version == IP_VERSION(9, 2, 2))
			kfd->device_info.needs_iommu_device = true;

		if (gc_version < IP_VERSION(11, 0, 0)) {
			/* Navi2x+, Navi1x+ */
			if (gc_version >= IP_VERSION(10, 3, 0))
				kfd->device_info.no_atomic_fw_version = 92;
			else if (gc_version >= IP_VERSION(10, 1, 1))
				kfd->device_info.no_atomic_fw_version = 145;

			/* Navi1x+ */
			if (gc_version >= IP_VERSION(10, 1, 1))
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

		if (asic_type == CHIP_KAVERI ||
		    asic_type == CHIP_CARRIZO)
			kfd->device_info.needs_iommu_device = true;

		if (asic_type != CHIP_HAWAII && !vf)
			kfd->device_info.needs_pci_atomics = true;
	}
}

struct kfd_dev *kgd2kfd_probe(struct amdgpu_device *adev, bool vf)
{
	struct kfd_dev *kfd = NULL;
	const struct kfd2kgd_calls *f2g = NULL;
	struct pci_dev *pdev = adev->pdev;
	uint32_t gfx_target_version = 0;

	switch (adev->asic_type) {
#ifdef KFD_SUPPORT_IOMMU_V2
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
#endif
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
		gfx_target_version = 80003;
		f2g = &gfx_v8_kfd2kgd;
		break;
	case CHIP_POLARIS10:
		gfx_target_version = 80003;
		f2g = &gfx_v8_kfd2kgd;
		break;
	case CHIP_POLARIS11:
		gfx_target_version = 80003;
		if (!vf)
			f2g = &gfx_v8_kfd2kgd;
		break;
	case CHIP_POLARIS12:
		gfx_target_version = 80003;
		if (!vf)
			f2g = &gfx_v8_kfd2kgd;
		break;
	case CHIP_VEGAM:
		gfx_target_version = 80003;
		if (!vf)
			f2g = &gfx_v8_kfd2kgd;
		break;
	default:
		switch (adev->ip_versions[GC_HWIP][0]) {
		/* Vega 10 */
		case IP_VERSION(9, 0, 1):
			gfx_target_version = 90000;
			f2g = &gfx_v9_kfd2kgd;
			break;
#ifdef KFD_SUPPORT_IOMMU_V2
		/* Raven */
		case IP_VERSION(9, 1, 0):
		case IP_VERSION(9, 2, 2):
			gfx_target_version = 90002;
			if (!vf)
				f2g = &gfx_v9_kfd2kgd;
			break;
#endif
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
		default:
			break;
		}
		break;
	}

	if (!f2g) {
		if (adev->ip_versions[GC_HWIP][0])
			dev_err(kfd_device, "GC IP %06x %s not supported in kfd\n",
				adev->ip_versions[GC_HWIP][0], vf ? "VF" : "");
		else
			dev_err(kfd_device, "%s %s not supported in kfd\n",
				amdgpu_asic_name[adev->asic_type], vf ? "VF" : "");
		return NULL;
	}

	kfd = kzalloc(sizeof(*kfd), GFP_KERNEL);
	if (!kfd)
		return NULL;

	kfd->adev = adev;
	kfd_device_info_init(kfd, vf, gfx_target_version);
	kfd->pdev = pdev;
	kfd->init_complete = false;
	kfd->kfd2kgd = f2g;
	atomic_set(&kfd->compute_profile, 0);

	mutex_init(&kfd->doorbell_mutex);
	memset(&kfd->doorbell_available_index, 0,
		sizeof(kfd->doorbell_available_index));

	atomic_set(&kfd->sram_ecc_flag, 0);

	ida_init(&kfd->doorbell_ida);

	return kfd;
}

static void kfd_cwsr_init(struct kfd_dev *kfd)
{
	if (cwsr_enable && kfd->device_info.supports_cwsr) {
		if (KFD_GC_VERSION(kfd) < IP_VERSION(9, 0, 1)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx8_hex) > PAGE_SIZE);
			kfd->cwsr_isa = cwsr_trap_gfx8_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_gfx8_hex);
		} else if (KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 1)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_arcturus_hex) > PAGE_SIZE);
			kfd->cwsr_isa = cwsr_trap_arcturus_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_arcturus_hex);
		} else if (KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 2)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_aldebaran_hex) > PAGE_SIZE);
			kfd->cwsr_isa = cwsr_trap_aldebaran_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_aldebaran_hex);
		} else if (KFD_GC_VERSION(kfd) < IP_VERSION(10, 1, 1)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx9_hex) > PAGE_SIZE);
			kfd->cwsr_isa = cwsr_trap_gfx9_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_gfx9_hex);
		} else if (KFD_GC_VERSION(kfd) < IP_VERSION(10, 3, 0)) {
			BUILD_BUG_ON(sizeof(cwsr_trap_nv1x_hex) > PAGE_SIZE);
			kfd->cwsr_isa = cwsr_trap_nv1x_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_nv1x_hex);
		} else {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx10_hex) > PAGE_SIZE);
			kfd->cwsr_isa = cwsr_trap_gfx10_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_gfx10_hex);
		}

		kfd->cwsr_enabled = true;
	}
}

static int kfd_gws_init(struct kfd_dev *kfd)
{
	int ret = 0;

	if (kfd->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS)
		return 0;

	if (hws_gws_support || (KFD_IS_SOC15(kfd) &&
		((KFD_GC_VERSION(kfd) == IP_VERSION(9, 0, 1)
			&& kfd->mec2_fw_version >= 0x81b3) ||
		(KFD_GC_VERSION(kfd) <= IP_VERSION(9, 4, 0)
			&& kfd->mec2_fw_version >= 0x1b3)  ||
		(KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 1)
			&& kfd->mec2_fw_version >= 0x30)   ||
		(KFD_GC_VERSION(kfd) == IP_VERSION(9, 4, 2)
			&& kfd->mec2_fw_version >= 0x28))))
		ret = amdgpu_amdkfd_alloc_gws(kfd->adev,
				kfd->adev->gds.gws_size, &kfd->gws);

	return ret;
}

static void kfd_smi_init(struct kfd_dev *dev) {
	INIT_LIST_HEAD(&dev->smi_clients);
	spin_lock_init(&dev->smi_lock);
}

bool kgd2kfd_device_init(struct kfd_dev *kfd,
			 struct drm_device *ddev,
			 const struct kgd2kfd_shared_resources *gpu_resources)
{
	unsigned int size, map_process_packet_size;

	kfd->ddev = ddev;
	kfd->mec_fw_version = amdgpu_amdkfd_get_fw_version(kfd->adev,
			KGD_ENGINE_MEC1);
	kfd->mec2_fw_version = amdgpu_amdkfd_get_fw_version(kfd->adev,
			KGD_ENGINE_MEC2);
	kfd->sdma_fw_version = amdgpu_amdkfd_get_fw_version(kfd->adev,
			KGD_ENGINE_SDMA1);
	kfd->shared_resources = *gpu_resources;

	kfd->vm_info.first_vmid_kfd = ffs(gpu_resources->compute_vmid_bitmap)-1;
	kfd->vm_info.last_vmid_kfd = fls(gpu_resources->compute_vmid_bitmap)-1;
	kfd->vm_info.vmid_num_kfd = kfd->vm_info.last_vmid_kfd
			- kfd->vm_info.first_vmid_kfd + 1;

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
			 kfd->pdev->vendor, kfd->pdev->device,
			 kfd->mec_fw_version,
			 kfd->device_info.no_atomic_fw_version);
		return false;
	}

	/* Verify module parameters regarding mapped process number*/
	if ((hws_max_conc_proc < 0)
			|| (hws_max_conc_proc > kfd->vm_info.vmid_num_kfd)) {
		dev_err(kfd_device,
			"hws_max_conc_proc %d must be between 0 and %d, use %d instead\n",
			hws_max_conc_proc, kfd->vm_info.vmid_num_kfd,
			kfd->vm_info.vmid_num_kfd);
		kfd->max_proc_per_quantum = kfd->vm_info.vmid_num_kfd;
	} else
		kfd->max_proc_per_quantum = hws_max_conc_proc;

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

	kfd->hive_id = kfd->adev->gmc.xgmi.hive_id;

	kfd->noretry = kfd->adev->gmc.noretry;

	if (kfd_interrupt_init(kfd)) {
		dev_err(kfd_device, "Error initializing interrupts\n");
		goto kfd_interrupt_error;
	}

	kfd->dqm = device_queue_manager_init(kfd);
	if (!kfd->dqm) {
		dev_err(kfd_device, "Error initializing queue manager\n");
		goto device_queue_manager_error;
	}

	/* If supported on this device, allocate global GWS that is shared
	 * by all KFD processes
	 */
	if (kfd_gws_init(kfd)) {
		dev_err(kfd_device, "Could not allocate %d gws\n",
			kfd->adev->gds.gws_size);
		goto gws_error;
	}

	/* If CRAT is broken, won't set iommu enabled */
	kfd_double_confirm_iommu_support(kfd);

	if (kfd_iommu_device_init(kfd)) {
		kfd->use_iommu_v2 = false;
		dev_err(kfd_device, "Error initializing iommuv2\n");
		goto device_iommu_error;
	}

	kfd_cwsr_init(kfd);

	svm_migrate_init(kfd->adev);

	if(kgd2kfd_resume_iommu(kfd))
		goto device_iommu_error;

	if (kfd_resume(kfd))
		goto kfd_resume_error;

	if (kfd_topology_add_device(kfd)) {
		dev_err(kfd_device, "Error adding device to topology\n");
		goto kfd_topology_add_device_error;
	}

	kfd_smi_init(kfd);

	kfd->init_complete = true;
	dev_info(kfd_device, "added device %x:%x\n", kfd->pdev->vendor,
		 kfd->pdev->device);

	pr_debug("Starting kfd with the following scheduling policy %d\n",
		kfd->dqm->sched_policy);

	goto out;

kfd_topology_add_device_error:
kfd_resume_error:
device_iommu_error:
gws_error:
	device_queue_manager_uninit(kfd->dqm);
device_queue_manager_error:
	kfd_interrupt_exit(kfd);
kfd_interrupt_error:
	kfd_doorbell_fini(kfd);
kfd_doorbell_error:
	kfd_gtt_sa_fini(kfd);
kfd_gtt_sa_init_error:
	amdgpu_amdkfd_free_gtt_mem(kfd->adev, kfd->gtt_mem);
alloc_gtt_mem_failure:
	if (kfd->gws)
		amdgpu_amdkfd_free_gws(kfd->adev, kfd->gws);
	dev_err(kfd_device,
		"device %x:%x NOT added due to errors\n",
		kfd->pdev->vendor, kfd->pdev->device);
out:
	return kfd->init_complete;
}

void kgd2kfd_device_exit(struct kfd_dev *kfd)
{
	if (kfd->init_complete) {
		device_queue_manager_uninit(kfd->dqm);
		kfd_interrupt_exit(kfd);
		kfd_topology_remove_device(kfd);
		kfd_doorbell_fini(kfd);
		ida_destroy(&kfd->doorbell_ida);
		kfd_gtt_sa_fini(kfd);
		amdgpu_amdkfd_free_gtt_mem(kfd->adev, kfd->gtt_mem);
		if (kfd->gws)
			amdgpu_amdkfd_free_gws(kfd->adev, kfd->gws);
	}

	kfree(kfd);
}

int kgd2kfd_pre_reset(struct kfd_dev *kfd)
{
	if (!kfd->init_complete)
		return 0;

	kfd_smi_event_update_gpu_reset(kfd, false);

	kfd->dqm->ops.pre_reset(kfd->dqm);

	kgd2kfd_suspend(kfd, false);

	kfd_signal_reset_event(kfd);
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

	if (!kfd->init_complete)
		return 0;

	ret = kfd_resume(kfd);
	if (ret)
		return ret;
	atomic_dec(&kfd_locked);

	atomic_set(&kfd->sram_ecc_flag, 0);

	kfd_smi_event_update_gpu_reset(kfd, true);

	return 0;
}

bool kfd_is_locked(void)
{
	return  (atomic_read(&kfd_locked) > 0);
}

void kgd2kfd_suspend(struct kfd_dev *kfd, bool run_pm)
{
	if (!kfd->init_complete)
		return;

	/* for runtime suspend, skip locking kfd */
	if (!run_pm) {
		/* For first KFD device suspend all the KFD processes */
		if (atomic_inc_return(&kfd_locked) == 1)
			kfd_suspend_all_processes();
	}

	kfd->dqm->ops.stop(kfd->dqm);
	kfd_iommu_suspend(kfd);
}

int kgd2kfd_resume(struct kfd_dev *kfd, bool run_pm)
{
	int ret, count;

	if (!kfd->init_complete)
		return 0;

	ret = kfd_resume(kfd);
	if (ret)
		return ret;

	/* for runtime resume, skip unlocking kfd */
	if (!run_pm) {
		count = atomic_dec_return(&kfd_locked);
		WARN_ONCE(count < 0, "KFD suspend / resume ref. error");
		if (count == 0)
			ret = kfd_resume_all_processes();
	}

	return ret;
}

int kgd2kfd_resume_iommu(struct kfd_dev *kfd)
{
	int err = 0;

	err = kfd_iommu_resume(kfd);
	if (err)
		dev_err(kfd_device,
			"Failed to resume IOMMU for device %x:%x\n",
			kfd->pdev->vendor, kfd->pdev->device);
	return err;
}

static int kfd_resume(struct kfd_dev *kfd)
{
	int err = 0;

	err = kfd->dqm->ops.start(kfd->dqm);
	if (err)
		dev_err(kfd_device,
			"Error starting queue manager for device %x:%x\n",
			kfd->pdev->vendor, kfd->pdev->device);

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
	uint32_t patched_ihre[KFD_MAX_RING_ENTRY_SIZE];
	bool is_patched = false;
	unsigned long flags;

	if (!kfd->init_complete)
		return;

	if (kfd->device_info.ih_ring_entry_size > sizeof(patched_ihre)) {
		dev_err_once(kfd_device, "Ring entry too small\n");
		return;
	}

	spin_lock_irqsave(&kfd->interrupt_lock, flags);

	if (kfd->interrupts_active
	    && interrupt_is_wanted(kfd, ih_ring_entry,
				   patched_ihre, &is_patched)
	    && enqueue_ih_ring_entry(kfd,
				     is_patched ? patched_ihre : ih_ring_entry))
		kfd_queue_work(kfd->ih_wq, &kfd->interrupt_work);

	spin_unlock_irqrestore(&kfd->interrupt_lock, flags);
}

int kgd2kfd_quiesce_mm(struct mm_struct *mm)
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
	r = kfd_process_evict_queues(p);

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
	unsigned int num_of_longs;

	if (WARN_ON(buf_size < chunk_size))
		return -EINVAL;
	if (WARN_ON(buf_size == 0))
		return -EINVAL;
	if (WARN_ON(chunk_size == 0))
		return -EINVAL;

	kfd->gtt_sa_chunk_size = chunk_size;
	kfd->gtt_sa_num_of_chunks = buf_size / chunk_size;

	num_of_longs = (kfd->gtt_sa_num_of_chunks + BITS_PER_LONG - 1) /
		BITS_PER_LONG;

	kfd->gtt_sa_bitmap = kcalloc(num_of_longs, sizeof(long), GFP_KERNEL);

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
	kfree(kfd->gtt_sa_bitmap);
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

int kfd_gtt_sa_allocate(struct kfd_dev *kfd, unsigned int size,
			struct kfd_mem_obj **mem_obj)
{
	unsigned int found, start_search, cur_size;

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
		set_bit(found, kfd->gtt_sa_bitmap);
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
	for (found = (*mem_obj)->range_start;
		found <= (*mem_obj)->range_end;
		found++)
		set_bit(found, kfd->gtt_sa_bitmap);

kfd_gtt_out:
	mutex_unlock(&kfd->gtt_sa_lock);
	return 0;

kfd_gtt_no_free_chunk:
	pr_debug("Allocation failed with mem_obj = %p\n", *mem_obj);
	mutex_unlock(&kfd->gtt_sa_lock);
	kfree(*mem_obj);
	return -ENOMEM;
}

int kfd_gtt_sa_free(struct kfd_dev *kfd, struct kfd_mem_obj *mem_obj)
{
	unsigned int bit;

	/* Act like kfree when trying to free a NULL object */
	if (!mem_obj)
		return 0;

	pr_debug("Free mem_obj = %p, range_start = %d, range_end = %d\n",
			mem_obj, mem_obj->range_start, mem_obj->range_end);

	mutex_lock(&kfd->gtt_sa_lock);

	/* Mark the chunks as free */
	for (bit = mem_obj->range_start;
		bit <= mem_obj->range_end;
		bit++)
		clear_bit(bit, kfd->gtt_sa_bitmap);

	mutex_unlock(&kfd->gtt_sa_lock);

	kfree(mem_obj);
	return 0;
}

void kgd2kfd_set_sram_ecc_flag(struct kfd_dev *kfd)
{
	if (kfd)
		atomic_inc(&kfd->sram_ecc_flag);
}

void kfd_inc_compute_active(struct kfd_dev *kfd)
{
	if (atomic_inc_return(&kfd->compute_profile) == 1)
		amdgpu_amdkfd_set_compute_idle(kfd->adev, false);
}

void kfd_dec_compute_active(struct kfd_dev *kfd)
{
	int count = atomic_dec_return(&kfd->compute_profile);

	if (count == 0)
		amdgpu_amdkfd_set_compute_idle(kfd->adev, true);
	WARN_ONCE(count < 0, "Compute profile ref. count error");
}

void kgd2kfd_smi_event_throttle(struct kfd_dev *kfd, uint64_t throttle_bitmask)
{
	if (kfd && kfd->init_complete)
		kfd_smi_event_update_thermal_throttling(kfd, throttle_bitmask);
}

/* kfd_get_num_sdma_engines returns the number of PCIe optimized SDMA and
 * kfd_get_num_xgmi_sdma_engines returns the number of XGMI SDMA.
 * When the device has more than two engines, we reserve two for PCIe to enable
 * full-duplex and the rest are used as XGMI.
 */
unsigned int kfd_get_num_sdma_engines(struct kfd_dev *kdev)
{
	/* If XGMI is not supported, all SDMA engines are PCIe */
	if (!kdev->adev->gmc.xgmi.supported)
		return kdev->adev->sdma.num_instances;

	return min(kdev->adev->sdma.num_instances, 2);
}

unsigned int kfd_get_num_xgmi_sdma_engines(struct kfd_dev *kdev)
{
	/* After reserved for PCIe, the rest of engines are XGMI */
	return kdev->adev->sdma.num_instances - kfd_get_num_sdma_engines(kdev);
}

#if defined(CONFIG_DEBUG_FS)

/* This function will send a package to HIQ to hang the HWS
 * which will trigger a GPU reset and bring the HWS back to normal state
 */
int kfd_debugfs_hang_hws(struct kfd_dev *dev)
{
	if (dev->dqm->sched_policy != KFD_SCHED_POLICY_HWS) {
		pr_err("HWS is not enabled");
		return -EINVAL;
	}

	return dqm_debugfs_hang_hws(dev->dqm);
}

#endif
