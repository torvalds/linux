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
#include "cwsr_trap_handler.h"
#include "kfd_iommu.h"
#include "amdgpu_amdkfd.h"

#define MQD_SIZE_ALIGNED 768

/*
 * kfd_locked is used to lock the kfd driver during suspend or reset
 * once locked, kfd driver will stop any further GPU execution.
 * create process (open) will return -EAGAIN.
 */
static atomic_t kfd_locked = ATOMIC_INIT(0);

#ifdef KFD_SUPPORT_IOMMU_V2
static const struct kfd_device_info kaveri_device_info = {
	.asic_family = CHIP_KAVERI,
	.max_pasid_bits = 16,
	/* max num of queues for KV.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = false,
	.needs_iommu_device = true,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info carrizo_device_info = {
	.asic_family = CHIP_CARRIZO,
	.max_pasid_bits = 16,
	/* max num of queues for CZ.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = true,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info raven_device_info = {
	.asic_family = CHIP_RAVEN,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = true,
	.needs_pci_atomics = true,
	.num_sdma_engines = 1,
	.num_sdma_queues_per_engine = 2,
};
#endif

static const struct kfd_device_info hawaii_device_info = {
	.asic_family = CHIP_HAWAII,
	.max_pasid_bits = 16,
	/* max num of queues for KV.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = false,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info tonga_device_info = {
	.asic_family = CHIP_TONGA,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = false,
	.needs_iommu_device = false,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info fiji_device_info = {
	.asic_family = CHIP_FIJI,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info fiji_vf_device_info = {
	.asic_family = CHIP_FIJI,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};


static const struct kfd_device_info polaris10_device_info = {
	.asic_family = CHIP_POLARIS10,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info polaris10_vf_device_info = {
	.asic_family = CHIP_POLARIS10,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info polaris11_device_info = {
	.asic_family = CHIP_POLARIS11,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info polaris12_device_info = {
	.asic_family = CHIP_POLARIS12,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info vega10_device_info = {
	.asic_family = CHIP_VEGA10,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info vega10_vf_device_info = {
	.asic_family = CHIP_VEGA10,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info vega12_device_info = {
	.asic_family = CHIP_VEGA12,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 2,
};

static const struct kfd_device_info vega20_device_info = {
	.asic_family = CHIP_VEGA20,
	.max_pasid_bits = 16,
	.max_no_of_hqd	= 24,
	.doorbell_size	= 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_sdma_queues_per_engine = 8,
};

struct kfd_deviceid {
	unsigned short did;
	const struct kfd_device_info *device_info;
};

static const struct kfd_deviceid supported_devices[] = {
#ifdef KFD_SUPPORT_IOMMU_V2
	{ 0x1304, &kaveri_device_info },	/* Kaveri */
	{ 0x1305, &kaveri_device_info },	/* Kaveri */
	{ 0x1306, &kaveri_device_info },	/* Kaveri */
	{ 0x1307, &kaveri_device_info },	/* Kaveri */
	{ 0x1309, &kaveri_device_info },	/* Kaveri */
	{ 0x130A, &kaveri_device_info },	/* Kaveri */
	{ 0x130B, &kaveri_device_info },	/* Kaveri */
	{ 0x130C, &kaveri_device_info },	/* Kaveri */
	{ 0x130D, &kaveri_device_info },	/* Kaveri */
	{ 0x130E, &kaveri_device_info },	/* Kaveri */
	{ 0x130F, &kaveri_device_info },	/* Kaveri */
	{ 0x1310, &kaveri_device_info },	/* Kaveri */
	{ 0x1311, &kaveri_device_info },	/* Kaveri */
	{ 0x1312, &kaveri_device_info },	/* Kaveri */
	{ 0x1313, &kaveri_device_info },	/* Kaveri */
	{ 0x1315, &kaveri_device_info },	/* Kaveri */
	{ 0x1316, &kaveri_device_info },	/* Kaveri */
	{ 0x1317, &kaveri_device_info },	/* Kaveri */
	{ 0x1318, &kaveri_device_info },	/* Kaveri */
	{ 0x131B, &kaveri_device_info },	/* Kaveri */
	{ 0x131C, &kaveri_device_info },	/* Kaveri */
	{ 0x131D, &kaveri_device_info },	/* Kaveri */
	{ 0x9870, &carrizo_device_info },	/* Carrizo */
	{ 0x9874, &carrizo_device_info },	/* Carrizo */
	{ 0x9875, &carrizo_device_info },	/* Carrizo */
	{ 0x9876, &carrizo_device_info },	/* Carrizo */
	{ 0x9877, &carrizo_device_info },	/* Carrizo */
	{ 0x15DD, &raven_device_info },		/* Raven */
#endif
	{ 0x67A0, &hawaii_device_info },	/* Hawaii */
	{ 0x67A1, &hawaii_device_info },	/* Hawaii */
	{ 0x67A2, &hawaii_device_info },	/* Hawaii */
	{ 0x67A8, &hawaii_device_info },	/* Hawaii */
	{ 0x67A9, &hawaii_device_info },	/* Hawaii */
	{ 0x67AA, &hawaii_device_info },	/* Hawaii */
	{ 0x67B0, &hawaii_device_info },	/* Hawaii */
	{ 0x67B1, &hawaii_device_info },	/* Hawaii */
	{ 0x67B8, &hawaii_device_info },	/* Hawaii */
	{ 0x67B9, &hawaii_device_info },	/* Hawaii */
	{ 0x67BA, &hawaii_device_info },	/* Hawaii */
	{ 0x67BE, &hawaii_device_info },	/* Hawaii */
	{ 0x6920, &tonga_device_info },		/* Tonga */
	{ 0x6921, &tonga_device_info },		/* Tonga */
	{ 0x6928, &tonga_device_info },		/* Tonga */
	{ 0x6929, &tonga_device_info },		/* Tonga */
	{ 0x692B, &tonga_device_info },		/* Tonga */
	{ 0x6938, &tonga_device_info },		/* Tonga */
	{ 0x6939, &tonga_device_info },		/* Tonga */
	{ 0x7300, &fiji_device_info },		/* Fiji */
	{ 0x730F, &fiji_vf_device_info },	/* Fiji vf*/
	{ 0x67C0, &polaris10_device_info },	/* Polaris10 */
	{ 0x67C1, &polaris10_device_info },	/* Polaris10 */
	{ 0x67C2, &polaris10_device_info },	/* Polaris10 */
	{ 0x67C4, &polaris10_device_info },	/* Polaris10 */
	{ 0x67C7, &polaris10_device_info },	/* Polaris10 */
	{ 0x67C8, &polaris10_device_info },	/* Polaris10 */
	{ 0x67C9, &polaris10_device_info },	/* Polaris10 */
	{ 0x67CA, &polaris10_device_info },	/* Polaris10 */
	{ 0x67CC, &polaris10_device_info },	/* Polaris10 */
	{ 0x67CF, &polaris10_device_info },	/* Polaris10 */
	{ 0x67D0, &polaris10_vf_device_info },	/* Polaris10 vf*/
	{ 0x67DF, &polaris10_device_info },	/* Polaris10 */
	{ 0x67E0, &polaris11_device_info },	/* Polaris11 */
	{ 0x67E1, &polaris11_device_info },	/* Polaris11 */
	{ 0x67E3, &polaris11_device_info },	/* Polaris11 */
	{ 0x67E7, &polaris11_device_info },	/* Polaris11 */
	{ 0x67E8, &polaris11_device_info },	/* Polaris11 */
	{ 0x67E9, &polaris11_device_info },	/* Polaris11 */
	{ 0x67EB, &polaris11_device_info },	/* Polaris11 */
	{ 0x67EF, &polaris11_device_info },	/* Polaris11 */
	{ 0x67FF, &polaris11_device_info },	/* Polaris11 */
	{ 0x6980, &polaris12_device_info },	/* Polaris12 */
	{ 0x6981, &polaris12_device_info },	/* Polaris12 */
	{ 0x6985, &polaris12_device_info },	/* Polaris12 */
	{ 0x6986, &polaris12_device_info },	/* Polaris12 */
	{ 0x6987, &polaris12_device_info },	/* Polaris12 */
	{ 0x6995, &polaris12_device_info },	/* Polaris12 */
	{ 0x6997, &polaris12_device_info },	/* Polaris12 */
	{ 0x699F, &polaris12_device_info },	/* Polaris12 */
	{ 0x6860, &vega10_device_info },	/* Vega10 */
	{ 0x6861, &vega10_device_info },	/* Vega10 */
	{ 0x6862, &vega10_device_info },	/* Vega10 */
	{ 0x6863, &vega10_device_info },	/* Vega10 */
	{ 0x6864, &vega10_device_info },	/* Vega10 */
	{ 0x6867, &vega10_device_info },	/* Vega10 */
	{ 0x6868, &vega10_device_info },	/* Vega10 */
	{ 0x6869, &vega10_device_info },	/* Vega10 */
	{ 0x686A, &vega10_device_info },	/* Vega10 */
	{ 0x686B, &vega10_device_info },	/* Vega10 */
	{ 0x686C, &vega10_vf_device_info },	/* Vega10  vf*/
	{ 0x686D, &vega10_device_info },	/* Vega10 */
	{ 0x686E, &vega10_device_info },	/* Vega10 */
	{ 0x686F, &vega10_device_info },	/* Vega10 */
	{ 0x687F, &vega10_device_info },	/* Vega10 */
	{ 0x69A0, &vega12_device_info },	/* Vega12 */
	{ 0x69A1, &vega12_device_info },	/* Vega12 */
	{ 0x69A2, &vega12_device_info },	/* Vega12 */
	{ 0x69A3, &vega12_device_info },	/* Vega12 */
	{ 0x69AF, &vega12_device_info },	/* Vega12 */
	{ 0x66a0, &vega20_device_info },	/* Vega20 */
	{ 0x66a1, &vega20_device_info },	/* Vega20 */
	{ 0x66a2, &vega20_device_info },	/* Vega20 */
	{ 0x66a3, &vega20_device_info },	/* Vega20 */
	{ 0x66a4, &vega20_device_info },	/* Vega20 */
	{ 0x66a7, &vega20_device_info },	/* Vega20 */
	{ 0x66af, &vega20_device_info }		/* Vega20 */
};

static int kfd_gtt_sa_init(struct kfd_dev *kfd, unsigned int buf_size,
				unsigned int chunk_size);
static void kfd_gtt_sa_fini(struct kfd_dev *kfd);

static int kfd_resume(struct kfd_dev *kfd);

static const struct kfd_device_info *lookup_device_info(unsigned short did)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(supported_devices); i++) {
		if (supported_devices[i].did == did) {
			WARN_ON(!supported_devices[i].device_info);
			return supported_devices[i].device_info;
		}
	}

	dev_warn(kfd_device, "DID %04x is missing in supported_devices\n",
		 did);

	return NULL;
}

struct kfd_dev *kgd2kfd_probe(struct kgd_dev *kgd,
	struct pci_dev *pdev, const struct kfd2kgd_calls *f2g)
{
	struct kfd_dev *kfd;
	int ret;
	const struct kfd_device_info *device_info =
					lookup_device_info(pdev->device);

	if (!device_info) {
		dev_err(kfd_device, "kgd2kfd_probe failed\n");
		return NULL;
	}

	kfd = kzalloc(sizeof(*kfd), GFP_KERNEL);
	if (!kfd)
		return NULL;

	/* Allow BIF to recode atomics to PCIe 3.0 AtomicOps.
	 * 32 and 64-bit requests are possible and must be
	 * supported.
	 */
	ret = pci_enable_atomic_ops_to_root(pdev,
			PCI_EXP_DEVCAP2_ATOMIC_COMP32 |
			PCI_EXP_DEVCAP2_ATOMIC_COMP64);
	if (device_info->needs_pci_atomics && ret < 0) {
		dev_info(kfd_device,
			 "skipped device %x:%x, PCI rejects atomics\n",
			 pdev->vendor, pdev->device);
		kfree(kfd);
		return NULL;
	} else if (!ret)
		kfd->pci_atomic_requested = true;

	kfd->kgd = kgd;
	kfd->device_info = device_info;
	kfd->pdev = pdev;
	kfd->init_complete = false;
	kfd->kfd2kgd = f2g;

	mutex_init(&kfd->doorbell_mutex);
	memset(&kfd->doorbell_available_index, 0,
		sizeof(kfd->doorbell_available_index));

	return kfd;
}

static void kfd_cwsr_init(struct kfd_dev *kfd)
{
	if (cwsr_enable && kfd->device_info->supports_cwsr) {
		if (kfd->device_info->asic_family < CHIP_VEGA10) {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx8_hex) > PAGE_SIZE);
			kfd->cwsr_isa = cwsr_trap_gfx8_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_gfx8_hex);
		} else {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx9_hex) > PAGE_SIZE);
			kfd->cwsr_isa = cwsr_trap_gfx9_hex;
			kfd->cwsr_isa_size = sizeof(cwsr_trap_gfx9_hex);
		}

		kfd->cwsr_enabled = true;
	}
}

bool kgd2kfd_device_init(struct kfd_dev *kfd,
			 const struct kgd2kfd_shared_resources *gpu_resources)
{
	unsigned int size;

	kfd->mec_fw_version = kfd->kfd2kgd->get_fw_version(kfd->kgd,
			KGD_ENGINE_MEC1);
	kfd->sdma_fw_version = kfd->kfd2kgd->get_fw_version(kfd->kgd,
			KGD_ENGINE_SDMA1);
	kfd->shared_resources = *gpu_resources;

	kfd->vm_info.first_vmid_kfd = ffs(gpu_resources->compute_vmid_bitmap)-1;
	kfd->vm_info.last_vmid_kfd = fls(gpu_resources->compute_vmid_bitmap)-1;
	kfd->vm_info.vmid_num_kfd = kfd->vm_info.last_vmid_kfd
			- kfd->vm_info.first_vmid_kfd + 1;

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
			kfd->device_info->mqd_size_aligned;

	/*
	 * calculate max size of runlist packet.
	 * There can be only 2 packets at once
	 */
	size += (KFD_MAX_NUM_OF_PROCESSES * sizeof(struct pm4_mes_map_process) +
		max_num_of_queues_per_device * sizeof(struct pm4_mes_map_queues)
		+ sizeof(struct pm4_mes_runlist)) * 2;

	/* Add size of HIQ & DIQ */
	size += KFD_KERNEL_QUEUE_SIZE * 2;

	/* add another 512KB for all other allocations on gart (HPD, fences) */
	size += 512 * 1024;

	if (amdgpu_amdkfd_alloc_gtt_mem(
			kfd->kgd, size, &kfd->gtt_mem,
			&kfd->gtt_start_gpu_addr, &kfd->gtt_start_cpu_ptr,
			false)) {
		dev_err(kfd_device, "Could not allocate %d bytes\n", size);
		goto out;
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

	if (kfd->kfd2kgd->get_hive_id)
		kfd->hive_id = kfd->kfd2kgd->get_hive_id(kfd->kgd);

	if (kfd_topology_add_device(kfd)) {
		dev_err(kfd_device, "Error adding device to topology\n");
		goto kfd_topology_add_device_error;
	}

	if (kfd_interrupt_init(kfd)) {
		dev_err(kfd_device, "Error initializing interrupts\n");
		goto kfd_interrupt_error;
	}

	kfd->dqm = device_queue_manager_init(kfd);
	if (!kfd->dqm) {
		dev_err(kfd_device, "Error initializing queue manager\n");
		goto device_queue_manager_error;
	}

	if (kfd_iommu_device_init(kfd)) {
		dev_err(kfd_device, "Error initializing iommuv2\n");
		goto device_iommu_error;
	}

	kfd_cwsr_init(kfd);

	if (kfd_resume(kfd))
		goto kfd_resume_error;

	kfd->dbgmgr = NULL;

	kfd->init_complete = true;
	dev_info(kfd_device, "added device %x:%x\n", kfd->pdev->vendor,
		 kfd->pdev->device);

	pr_debug("Starting kfd with the following scheduling policy %d\n",
		kfd->dqm->sched_policy);

	goto out;

kfd_resume_error:
device_iommu_error:
	device_queue_manager_uninit(kfd->dqm);
device_queue_manager_error:
	kfd_interrupt_exit(kfd);
kfd_interrupt_error:
	kfd_topology_remove_device(kfd);
kfd_topology_add_device_error:
	kfd_doorbell_fini(kfd);
kfd_doorbell_error:
	kfd_gtt_sa_fini(kfd);
kfd_gtt_sa_init_error:
	amdgpu_amdkfd_free_gtt_mem(kfd->kgd, kfd->gtt_mem);
	dev_err(kfd_device,
		"device %x:%x NOT added due to errors\n",
		kfd->pdev->vendor, kfd->pdev->device);
out:
	return kfd->init_complete;
}

void kgd2kfd_device_exit(struct kfd_dev *kfd)
{
	if (kfd->init_complete) {
		kgd2kfd_suspend(kfd);
		device_queue_manager_uninit(kfd->dqm);
		kfd_interrupt_exit(kfd);
		kfd_topology_remove_device(kfd);
		kfd_doorbell_fini(kfd);
		kfd_gtt_sa_fini(kfd);
		amdgpu_amdkfd_free_gtt_mem(kfd->kgd, kfd->gtt_mem);
	}

	kfree(kfd);
}

int kgd2kfd_pre_reset(struct kfd_dev *kfd)
{
	if (!kfd->init_complete)
		return 0;
	kgd2kfd_suspend(kfd);

	/* hold dqm->lock to prevent further execution*/
	dqm_lock(kfd->dqm);

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
	int ret, count;

	if (!kfd->init_complete)
		return 0;

	dqm_unlock(kfd->dqm);

	ret = kfd_resume(kfd);
	if (ret)
		return ret;
	count = atomic_dec_return(&kfd_locked);
	WARN_ONCE(count != 0, "KFD reset ref. error");
	return 0;
}

bool kfd_is_locked(void)
{
	return  (atomic_read(&kfd_locked) > 0);
}

void kgd2kfd_suspend(struct kfd_dev *kfd)
{
	if (!kfd->init_complete)
		return;

	/* For first KFD device suspend all the KFD processes */
	if (atomic_inc_return(&kfd_locked) == 1)
		kfd_suspend_all_processes();

	kfd->dqm->ops.stop(kfd->dqm);

	kfd_iommu_suspend(kfd);
}

int kgd2kfd_resume(struct kfd_dev *kfd)
{
	int ret, count;

	if (!kfd->init_complete)
		return 0;

	ret = kfd_resume(kfd);
	if (ret)
		return ret;

	count = atomic_dec_return(&kfd_locked);
	WARN_ONCE(count < 0, "KFD suspend / resume ref. error");
	if (count == 0)
		ret = kfd_resume_all_processes();

	return ret;
}

static int kfd_resume(struct kfd_dev *kfd)
{
	int err = 0;

	err = kfd_iommu_resume(kfd);
	if (err) {
		dev_err(kfd_device,
			"Failed to resume IOMMU for device %x:%x\n",
			kfd->pdev->vendor, kfd->pdev->device);
		return err;
	}

	err = kfd->dqm->ops.start(kfd->dqm);
	if (err) {
		dev_err(kfd_device,
			"Error starting queue manager for device %x:%x\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto dqm_start_error;
	}

	return err;

dqm_start_error:
	kfd_iommu_suspend(kfd);
	return err;
}

/* This is called directly from KGD at ISR. */
void kgd2kfd_interrupt(struct kfd_dev *kfd, const void *ih_ring_entry)
{
	uint32_t patched_ihre[KFD_MAX_RING_ENTRY_SIZE];
	bool is_patched = false;
	unsigned long flags;

	if (!kfd->init_complete)
		return;

	if (kfd->device_info->ih_ring_entry_size > sizeof(patched_ihre)) {
		dev_err_once(kfd_device, "Ring entry too small\n");
		return;
	}

	spin_lock_irqsave(&kfd->interrupt_lock, flags);

	if (kfd->interrupts_active
	    && interrupt_is_wanted(kfd, ih_ring_entry,
				   patched_ihre, &is_patched)
	    && enqueue_ih_ring_entry(kfd,
				     is_patched ? patched_ihre : ih_ring_entry))
		queue_work(kfd->ih_wq, &kfd->interrupt_work);

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
	pr_debug("Allocation failed with mem_obj = %p\n", mem_obj);
	mutex_unlock(&kfd->gtt_sa_lock);
	kfree(mem_obj);
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

#if defined(CONFIG_DEBUG_FS)

/* This function will send a package to HIQ to hang the HWS
 * which will trigger a GPU reset and bring the HWS back to normal state
 */
int kfd_debugfs_hang_hws(struct kfd_dev *dev)
{
	int r = 0;

	if (dev->dqm->sched_policy != KFD_SCHED_POLICY_HWS) {
		pr_err("HWS is not enabled");
		return -EINVAL;
	}

	r = pm_debugfs_hang_hws(&dev->dqm->packets);
	if (!r)
		r = dqm_debugfs_execute_queues(dev->dqm);

	return r;
}

#endif
