/*
 * Copyright 2018 Red Hat Inc.
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
#include "nouveau_svm.h"
#include "nouveau_drv.h"
#include "nouveau_chan.h"
#include "nouveau_dmem.h"

#include <nvif/event.h>
#include <nvif/object.h>
#include <nvif/vmm.h>

#include <nvif/class.h>
#include <nvif/clb069.h>
#include <nvif/ifc00d.h>

#include <linux/sched/mm.h>
#include <linux/sort.h>
#include <linux/hmm.h>
#include <linux/memremap.h>
#include <linux/rmap.h>

struct nouveau_svm {
	struct nouveau_drm *drm;
	struct mutex mutex;
	struct list_head inst;

	struct nouveau_svm_fault_buffer {
		int id;
		struct nvif_object object;
		u32 entries;
		u32 getaddr;
		u32 putaddr;
		u32 get;
		u32 put;
		struct nvif_event notify;
		struct work_struct work;

		struct nouveau_svm_fault {
			u64 inst;
			u64 addr;
			u64 time;
			u32 engine;
			u8  gpc;
			u8  hub;
			u8  access;
			u8  client;
			u8  fault;
			struct nouveau_svmm *svmm;
		} **fault;
		int fault_nr;
	} buffer[];
};

#define FAULT_ACCESS_READ 0
#define FAULT_ACCESS_WRITE 1
#define FAULT_ACCESS_ATOMIC 2
#define FAULT_ACCESS_PREFETCH 3

#define SVM_DBG(s,f,a...) NV_DEBUG((s)->drm, "svm: "f"\n", ##a)
#define SVM_ERR(s,f,a...) NV_WARN((s)->drm, "svm: "f"\n", ##a)

struct nouveau_pfnmap_args {
	struct nvif_ioctl_v0 i;
	struct nvif_ioctl_mthd_v0 m;
	struct nvif_vmm_pfnmap_v0 p;
};

struct nouveau_ivmm {
	struct nouveau_svmm *svmm;
	u64 inst;
	struct list_head head;
};

static struct nouveau_ivmm *
nouveau_ivmm_find(struct nouveau_svm *svm, u64 inst)
{
	struct nouveau_ivmm *ivmm;
	list_for_each_entry(ivmm, &svm->inst, head) {
		if (ivmm->inst == inst)
			return ivmm;
	}
	return NULL;
}

#define SVMM_DBG(s,f,a...)                                                     \
	NV_DEBUG((s)->vmm->cli->drm, "svm-%p: "f"\n", (s), ##a)
#define SVMM_ERR(s,f,a...)                                                     \
	NV_WARN((s)->vmm->cli->drm, "svm-%p: "f"\n", (s), ##a)

int
nouveau_svmm_bind(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct drm_nouveau_svm_bind *args = data;
	unsigned target, cmd;
	unsigned long addr, end;
	struct mm_struct *mm;

	args->va_start &= PAGE_MASK;
	args->va_end = ALIGN(args->va_end, PAGE_SIZE);

	/* Sanity check arguments */
	if (args->reserved0 || args->reserved1)
		return -EINVAL;
	if (args->header & (~NOUVEAU_SVM_BIND_VALID_MASK))
		return -EINVAL;
	if (args->va_start >= args->va_end)
		return -EINVAL;

	cmd = args->header >> NOUVEAU_SVM_BIND_COMMAND_SHIFT;
	cmd &= NOUVEAU_SVM_BIND_COMMAND_MASK;
	switch (cmd) {
	case NOUVEAU_SVM_BIND_COMMAND__MIGRATE:
		break;
	default:
		return -EINVAL;
	}

	/* FIXME support CPU target ie all target value < GPU_VRAM */
	target = args->header >> NOUVEAU_SVM_BIND_TARGET_SHIFT;
	target &= NOUVEAU_SVM_BIND_TARGET_MASK;
	switch (target) {
	case NOUVEAU_SVM_BIND_TARGET__GPU_VRAM:
		break;
	default:
		return -EINVAL;
	}

	/*
	 * FIXME: For now refuse non 0 stride, we need to change the migrate
	 * kernel function to handle stride to avoid to create a mess within
	 * each device driver.
	 */
	if (args->stride)
		return -EINVAL;

	/*
	 * Ok we are ask to do something sane, for now we only support migrate
	 * commands but we will add things like memory policy (what to do on
	 * page fault) and maybe some other commands.
	 */

	mm = get_task_mm(current);
	if (!mm) {
		return -EINVAL;
	}
	mmap_read_lock(mm);

	if (!cli->svm.svmm) {
		mmap_read_unlock(mm);
		mmput(mm);
		return -EINVAL;
	}

	for (addr = args->va_start, end = args->va_end; addr < end;) {
		struct vm_area_struct *vma;
		unsigned long next;

		vma = find_vma_intersection(mm, addr, end);
		if (!vma)
			break;

		addr = max(addr, vma->vm_start);
		next = min(vma->vm_end, end);
		/* This is a best effort so we ignore errors */
		nouveau_dmem_migrate_vma(cli->drm, cli->svm.svmm, vma, addr,
					 next);
		addr = next;
	}

	/*
	 * FIXME Return the number of page we have migrated, again we need to
	 * update the migrate API to return that information so that we can
	 * report it to user space.
	 */
	args->result = 0;

	mmap_read_unlock(mm);
	mmput(mm);

	return 0;
}

/* Unlink channel instance from SVMM. */
void
nouveau_svmm_part(struct nouveau_svmm *svmm, u64 inst)
{
	struct nouveau_ivmm *ivmm;
	if (svmm) {
		mutex_lock(&svmm->vmm->cli->drm->svm->mutex);
		ivmm = nouveau_ivmm_find(svmm->vmm->cli->drm->svm, inst);
		if (ivmm) {
			list_del(&ivmm->head);
			kfree(ivmm);
		}
		mutex_unlock(&svmm->vmm->cli->drm->svm->mutex);
	}
}

/* Link channel instance to SVMM. */
int
nouveau_svmm_join(struct nouveau_svmm *svmm, u64 inst)
{
	struct nouveau_ivmm *ivmm;
	if (svmm) {
		if (!(ivmm = kmalloc(sizeof(*ivmm), GFP_KERNEL)))
			return -ENOMEM;
		ivmm->svmm = svmm;
		ivmm->inst = inst;

		mutex_lock(&svmm->vmm->cli->drm->svm->mutex);
		list_add(&ivmm->head, &svmm->vmm->cli->drm->svm->inst);
		mutex_unlock(&svmm->vmm->cli->drm->svm->mutex);
	}
	return 0;
}

/* Invalidate SVMM address-range on GPU. */
void
nouveau_svmm_invalidate(struct nouveau_svmm *svmm, u64 start, u64 limit)
{
	if (limit > start) {
		nvif_object_mthd(&svmm->vmm->vmm.object, NVIF_VMM_V0_PFNCLR,
				 &(struct nvif_vmm_pfnclr_v0) {
					.addr = start,
					.size = limit - start,
				 }, sizeof(struct nvif_vmm_pfnclr_v0));
	}
}

static int
nouveau_svmm_invalidate_range_start(struct mmu_notifier *mn,
				    const struct mmu_notifier_range *update)
{
	struct nouveau_svmm *svmm =
		container_of(mn, struct nouveau_svmm, notifier);
	unsigned long start = update->start;
	unsigned long limit = update->end;

	if (!mmu_notifier_range_blockable(update))
		return -EAGAIN;

	SVMM_DBG(svmm, "invalidate %016lx-%016lx", start, limit);

	mutex_lock(&svmm->mutex);
	if (unlikely(!svmm->vmm))
		goto out;

	/*
	 * Ignore invalidation callbacks for device private pages since
	 * the invalidation is handled as part of the migration process.
	 */
	if (update->event == MMU_NOTIFY_MIGRATE &&
	    update->owner == svmm->vmm->cli->drm->dev)
		goto out;

	if (limit > svmm->unmanaged.start && start < svmm->unmanaged.limit) {
		if (start < svmm->unmanaged.start) {
			nouveau_svmm_invalidate(svmm, start,
						svmm->unmanaged.limit);
		}
		start = svmm->unmanaged.limit;
	}

	nouveau_svmm_invalidate(svmm, start, limit);

out:
	mutex_unlock(&svmm->mutex);
	return 0;
}

static void nouveau_svmm_free_notifier(struct mmu_notifier *mn)
{
	kfree(container_of(mn, struct nouveau_svmm, notifier));
}

static const struct mmu_notifier_ops nouveau_mn_ops = {
	.invalidate_range_start = nouveau_svmm_invalidate_range_start,
	.free_notifier = nouveau_svmm_free_notifier,
};

void
nouveau_svmm_fini(struct nouveau_svmm **psvmm)
{
	struct nouveau_svmm *svmm = *psvmm;
	if (svmm) {
		mutex_lock(&svmm->mutex);
		svmm->vmm = NULL;
		mutex_unlock(&svmm->mutex);
		mmu_notifier_put(&svmm->notifier);
		*psvmm = NULL;
	}
}

int
nouveau_svmm_init(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_svmm *svmm;
	struct drm_nouveau_svm_init *args = data;
	int ret;

	/* We need to fail if svm is disabled */
	if (!cli->drm->svm)
		return -ENOSYS;

	/* Allocate tracking for SVM-enabled VMM. */
	if (!(svmm = kzalloc(sizeof(*svmm), GFP_KERNEL)))
		return -ENOMEM;
	svmm->vmm = &cli->svm;
	svmm->unmanaged.start = args->unmanaged_addr;
	svmm->unmanaged.limit = args->unmanaged_addr + args->unmanaged_size;
	mutex_init(&svmm->mutex);

	/* Check that SVM isn't already enabled for the client. */
	mutex_lock(&cli->mutex);
	if (cli->svm.cli) {
		ret = -EBUSY;
		goto out_free;
	}

	/* Allocate a new GPU VMM that can support SVM (managed by the
	 * client, with replayable faults enabled).
	 *
	 * All future channel/memory allocations will make use of this
	 * VMM instead of the standard one.
	 */
	ret = nvif_vmm_ctor(&cli->mmu, "svmVmm",
			    cli->vmm.vmm.object.oclass, MANAGED,
			    args->unmanaged_addr, args->unmanaged_size,
			    &(struct gp100_vmm_v0) {
				.fault_replay = true,
			    }, sizeof(struct gp100_vmm_v0), &cli->svm.vmm);
	if (ret)
		goto out_free;

	mmap_write_lock(current->mm);
	svmm->notifier.ops = &nouveau_mn_ops;
	ret = __mmu_notifier_register(&svmm->notifier, current->mm);
	if (ret)
		goto out_mm_unlock;
	/* Note, ownership of svmm transfers to mmu_notifier */

	cli->svm.svmm = svmm;
	cli->svm.cli = cli;
	mmap_write_unlock(current->mm);
	mutex_unlock(&cli->mutex);
	return 0;

out_mm_unlock:
	mmap_write_unlock(current->mm);
out_free:
	mutex_unlock(&cli->mutex);
	kfree(svmm);
	return ret;
}

/* Issue fault replay for GPU to retry accesses that faulted previously. */
static void
nouveau_svm_fault_replay(struct nouveau_svm *svm)
{
	SVM_DBG(svm, "replay");
	WARN_ON(nvif_object_mthd(&svm->drm->client.vmm.vmm.object,
				 GP100_VMM_VN_FAULT_REPLAY,
				 &(struct gp100_vmm_fault_replay_vn) {},
				 sizeof(struct gp100_vmm_fault_replay_vn)));
}

/* Cancel a replayable fault that could not be handled.
 *
 * Cancelling the fault will trigger recovery to reset the engine
 * and kill the offending channel (ie. GPU SIGSEGV).
 */
static void
nouveau_svm_fault_cancel(struct nouveau_svm *svm,
			 u64 inst, u8 hub, u8 gpc, u8 client)
{
	SVM_DBG(svm, "cancel %016llx %d %02x %02x", inst, hub, gpc, client);
	WARN_ON(nvif_object_mthd(&svm->drm->client.vmm.vmm.object,
				 GP100_VMM_VN_FAULT_CANCEL,
				 &(struct gp100_vmm_fault_cancel_v0) {
					.hub = hub,
					.gpc = gpc,
					.client = client,
					.inst = inst,
				 }, sizeof(struct gp100_vmm_fault_cancel_v0)));
}

static void
nouveau_svm_fault_cancel_fault(struct nouveau_svm *svm,
			       struct nouveau_svm_fault *fault)
{
	nouveau_svm_fault_cancel(svm, fault->inst,
				      fault->hub,
				      fault->gpc,
				      fault->client);
}

static int
nouveau_svm_fault_priority(u8 fault)
{
	switch (fault) {
	case FAULT_ACCESS_PREFETCH:
		return 0;
	case FAULT_ACCESS_READ:
		return 1;
	case FAULT_ACCESS_WRITE:
		return 2;
	case FAULT_ACCESS_ATOMIC:
		return 3;
	default:
		WARN_ON_ONCE(1);
		return -1;
	}
}

static int
nouveau_svm_fault_cmp(const void *a, const void *b)
{
	const struct nouveau_svm_fault *fa = *(struct nouveau_svm_fault **)a;
	const struct nouveau_svm_fault *fb = *(struct nouveau_svm_fault **)b;
	int ret;
	if ((ret = (s64)fa->inst - fb->inst))
		return ret;
	if ((ret = (s64)fa->addr - fb->addr))
		return ret;
	return nouveau_svm_fault_priority(fa->access) -
		nouveau_svm_fault_priority(fb->access);
}

static void
nouveau_svm_fault_cache(struct nouveau_svm *svm,
			struct nouveau_svm_fault_buffer *buffer, u32 offset)
{
	struct nvif_object *memory = &buffer->object;
	const u32 instlo = nvif_rd32(memory, offset + 0x00);
	const u32 insthi = nvif_rd32(memory, offset + 0x04);
	const u32 addrlo = nvif_rd32(memory, offset + 0x08);
	const u32 addrhi = nvif_rd32(memory, offset + 0x0c);
	const u32 timelo = nvif_rd32(memory, offset + 0x10);
	const u32 timehi = nvif_rd32(memory, offset + 0x14);
	const u32 engine = nvif_rd32(memory, offset + 0x18);
	const u32   info = nvif_rd32(memory, offset + 0x1c);
	const u64   inst = (u64)insthi << 32 | instlo;
	const u8     gpc = (info & 0x1f000000) >> 24;
	const u8     hub = (info & 0x00100000) >> 20;
	const u8  client = (info & 0x00007f00) >> 8;
	struct nouveau_svm_fault *fault;

	//XXX: i think we're supposed to spin waiting */
	if (WARN_ON(!(info & 0x80000000)))
		return;

	nvif_mask(memory, offset + 0x1c, 0x80000000, 0x00000000);

	if (!buffer->fault[buffer->fault_nr]) {
		fault = kmalloc(sizeof(*fault), GFP_KERNEL);
		if (WARN_ON(!fault)) {
			nouveau_svm_fault_cancel(svm, inst, hub, gpc, client);
			return;
		}
		buffer->fault[buffer->fault_nr] = fault;
	}

	fault = buffer->fault[buffer->fault_nr++];
	fault->inst   = inst;
	fault->addr   = (u64)addrhi << 32 | addrlo;
	fault->time   = (u64)timehi << 32 | timelo;
	fault->engine = engine;
	fault->gpc    = gpc;
	fault->hub    = hub;
	fault->access = (info & 0x000f0000) >> 16;
	fault->client = client;
	fault->fault  = (info & 0x0000001f);

	SVM_DBG(svm, "fault %016llx %016llx %02x",
		fault->inst, fault->addr, fault->access);
}

struct svm_notifier {
	struct mmu_interval_notifier notifier;
	struct nouveau_svmm *svmm;
};

static bool nouveau_svm_range_invalidate(struct mmu_interval_notifier *mni,
					 const struct mmu_notifier_range *range,
					 unsigned long cur_seq)
{
	struct svm_notifier *sn =
		container_of(mni, struct svm_notifier, notifier);

	if (range->event == MMU_NOTIFY_EXCLUSIVE &&
	    range->owner == sn->svmm->vmm->cli->drm->dev)
		return true;

	/*
	 * serializes the update to mni->invalidate_seq done by caller and
	 * prevents invalidation of the PTE from progressing while HW is being
	 * programmed. This is very hacky and only works because the normal
	 * notifier that does invalidation is always called after the range
	 * notifier.
	 */
	if (mmu_notifier_range_blockable(range))
		mutex_lock(&sn->svmm->mutex);
	else if (!mutex_trylock(&sn->svmm->mutex))
		return false;
	mmu_interval_set_seq(mni, cur_seq);
	mutex_unlock(&sn->svmm->mutex);
	return true;
}

static const struct mmu_interval_notifier_ops nouveau_svm_mni_ops = {
	.invalidate = nouveau_svm_range_invalidate,
};

static void nouveau_hmm_convert_pfn(struct nouveau_drm *drm,
				    struct hmm_range *range,
				    struct nouveau_pfnmap_args *args)
{
	struct page *page;

	/*
	 * The address prepared here is passed through nvif_object_ioctl()
	 * to an eventual DMA map in something like gp100_vmm_pgt_pfn()
	 *
	 * This is all just encoding the internal hmm representation into a
	 * different nouveau internal representation.
	 */
	if (!(range->hmm_pfns[0] & HMM_PFN_VALID)) {
		args->p.phys[0] = 0;
		return;
	}

	page = hmm_pfn_to_page(range->hmm_pfns[0]);
	/*
	 * Only map compound pages to the GPU if the CPU is also mapping the
	 * page as a compound page. Otherwise, the PTE protections might not be
	 * consistent (e.g., CPU only maps part of a compound page).
	 * Note that the underlying page might still be larger than the
	 * CPU mapping (e.g., a PUD sized compound page partially mapped with
	 * a PMD sized page table entry).
	 */
	if (hmm_pfn_to_map_order(range->hmm_pfns[0])) {
		unsigned long addr = args->p.addr;

		args->p.page = hmm_pfn_to_map_order(range->hmm_pfns[0]) +
				PAGE_SHIFT;
		args->p.size = 1UL << args->p.page;
		args->p.addr &= ~(args->p.size - 1);
		page -= (addr - args->p.addr) >> PAGE_SHIFT;
	}
	if (is_device_private_page(page))
		args->p.phys[0] = nouveau_dmem_page_addr(page) |
				NVIF_VMM_PFNMAP_V0_V |
				NVIF_VMM_PFNMAP_V0_VRAM;
	else
		args->p.phys[0] = page_to_phys(page) |
				NVIF_VMM_PFNMAP_V0_V |
				NVIF_VMM_PFNMAP_V0_HOST;
	if (range->hmm_pfns[0] & HMM_PFN_WRITE)
		args->p.phys[0] |= NVIF_VMM_PFNMAP_V0_W;
}

static int nouveau_atomic_range_fault(struct nouveau_svmm *svmm,
			       struct nouveau_drm *drm,
			       struct nouveau_pfnmap_args *args, u32 size,
			       struct svm_notifier *notifier)
{
	unsigned long timeout =
		jiffies + msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);
	struct mm_struct *mm = svmm->notifier.mm;
	struct folio *folio;
	struct page *page;
	unsigned long start = args->p.addr;
	unsigned long notifier_seq;
	int ret = 0;

	ret = mmu_interval_notifier_insert(&notifier->notifier, mm,
					args->p.addr, args->p.size,
					&nouveau_svm_mni_ops);
	if (ret)
		return ret;

	while (true) {
		if (time_after(jiffies, timeout)) {
			ret = -EBUSY;
			goto out;
		}

		notifier_seq = mmu_interval_read_begin(&notifier->notifier);
		mmap_read_lock(mm);
		ret = make_device_exclusive_range(mm, start, start + PAGE_SIZE,
					    &page, drm->dev);
		mmap_read_unlock(mm);
		if (ret <= 0 || !page) {
			ret = -EINVAL;
			goto out;
		}
		folio = page_folio(page);

		mutex_lock(&svmm->mutex);
		if (!mmu_interval_read_retry(&notifier->notifier,
					     notifier_seq))
			break;
		mutex_unlock(&svmm->mutex);

		folio_unlock(folio);
		folio_put(folio);
	}

	/* Map the page on the GPU. */
	args->p.page = 12;
	args->p.size = PAGE_SIZE;
	args->p.addr = start;
	args->p.phys[0] = page_to_phys(page) |
		NVIF_VMM_PFNMAP_V0_V |
		NVIF_VMM_PFNMAP_V0_W |
		NVIF_VMM_PFNMAP_V0_A |
		NVIF_VMM_PFNMAP_V0_HOST;

	ret = nvif_object_ioctl(&svmm->vmm->vmm.object, args, size, NULL);
	mutex_unlock(&svmm->mutex);

	folio_unlock(folio);
	folio_put(folio);

out:
	mmu_interval_notifier_remove(&notifier->notifier);
	return ret;
}

static int nouveau_range_fault(struct nouveau_svmm *svmm,
			       struct nouveau_drm *drm,
			       struct nouveau_pfnmap_args *args, u32 size,
			       unsigned long hmm_flags,
			       struct svm_notifier *notifier)
{
	unsigned long timeout =
		jiffies + msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);
	/* Have HMM fault pages within the fault window to the GPU. */
	unsigned long hmm_pfns[1];
	struct hmm_range range = {
		.notifier = &notifier->notifier,
		.default_flags = hmm_flags,
		.hmm_pfns = hmm_pfns,
		.dev_private_owner = drm->dev,
	};
	struct mm_struct *mm = svmm->notifier.mm;
	int ret;

	ret = mmu_interval_notifier_insert(&notifier->notifier, mm,
					args->p.addr, args->p.size,
					&nouveau_svm_mni_ops);
	if (ret)
		return ret;

	range.start = notifier->notifier.interval_tree.start;
	range.end = notifier->notifier.interval_tree.last + 1;

	while (true) {
		if (time_after(jiffies, timeout)) {
			ret = -EBUSY;
			goto out;
		}

		range.notifier_seq = mmu_interval_read_begin(range.notifier);
		mmap_read_lock(mm);
		ret = hmm_range_fault(&range);
		mmap_read_unlock(mm);
		if (ret) {
			if (ret == -EBUSY)
				continue;
			goto out;
		}

		mutex_lock(&svmm->mutex);
		if (mmu_interval_read_retry(range.notifier,
					    range.notifier_seq)) {
			mutex_unlock(&svmm->mutex);
			continue;
		}
		break;
	}

	nouveau_hmm_convert_pfn(drm, &range, args);

	ret = nvif_object_ioctl(&svmm->vmm->vmm.object, args, size, NULL);
	mutex_unlock(&svmm->mutex);

out:
	mmu_interval_notifier_remove(&notifier->notifier);

	return ret;
}

static void
nouveau_svm_fault(struct work_struct *work)
{
	struct nouveau_svm_fault_buffer *buffer = container_of(work, typeof(*buffer), work);
	struct nouveau_svm *svm = container_of(buffer, typeof(*svm), buffer[buffer->id]);
	struct nvif_object *device = &svm->drm->client.device.object;
	struct nouveau_svmm *svmm;
	struct {
		struct nouveau_pfnmap_args i;
		u64 phys[1];
	} args;
	unsigned long hmm_flags;
	u64 inst, start, limit;
	int fi, fn;
	int replay = 0, atomic = 0, ret;

	/* Parse available fault buffer entries into a cache, and update
	 * the GET pointer so HW can reuse the entries.
	 */
	SVM_DBG(svm, "fault handler");
	if (buffer->get == buffer->put) {
		buffer->put = nvif_rd32(device, buffer->putaddr);
		buffer->get = nvif_rd32(device, buffer->getaddr);
		if (buffer->get == buffer->put)
			return;
	}
	buffer->fault_nr = 0;

	SVM_DBG(svm, "get %08x put %08x", buffer->get, buffer->put);
	while (buffer->get != buffer->put) {
		nouveau_svm_fault_cache(svm, buffer, buffer->get * 0x20);
		if (++buffer->get == buffer->entries)
			buffer->get = 0;
	}
	nvif_wr32(device, buffer->getaddr, buffer->get);
	SVM_DBG(svm, "%d fault(s) pending", buffer->fault_nr);

	/* Sort parsed faults by instance pointer to prevent unnecessary
	 * instance to SVMM translations, followed by address and access
	 * type to reduce the amount of work when handling the faults.
	 */
	sort(buffer->fault, buffer->fault_nr, sizeof(*buffer->fault),
	     nouveau_svm_fault_cmp, NULL);

	/* Lookup SVMM structure for each unique instance pointer. */
	mutex_lock(&svm->mutex);
	for (fi = 0, svmm = NULL; fi < buffer->fault_nr; fi++) {
		if (!svmm || buffer->fault[fi]->inst != inst) {
			struct nouveau_ivmm *ivmm =
				nouveau_ivmm_find(svm, buffer->fault[fi]->inst);
			svmm = ivmm ? ivmm->svmm : NULL;
			inst = buffer->fault[fi]->inst;
			SVM_DBG(svm, "inst %016llx -> svm-%p", inst, svmm);
		}
		buffer->fault[fi]->svmm = svmm;
	}
	mutex_unlock(&svm->mutex);

	/* Process list of faults. */
	args.i.i.version = 0;
	args.i.i.type = NVIF_IOCTL_V0_MTHD;
	args.i.m.version = 0;
	args.i.m.method = NVIF_VMM_V0_PFNMAP;
	args.i.p.version = 0;

	for (fi = 0; fn = fi + 1, fi < buffer->fault_nr; fi = fn) {
		struct svm_notifier notifier;
		struct mm_struct *mm;

		/* Cancel any faults from non-SVM channels. */
		if (!(svmm = buffer->fault[fi]->svmm)) {
			nouveau_svm_fault_cancel_fault(svm, buffer->fault[fi]);
			continue;
		}
		SVMM_DBG(svmm, "addr %016llx", buffer->fault[fi]->addr);

		/* We try and group handling of faults within a small
		 * window into a single update.
		 */
		start = buffer->fault[fi]->addr;
		limit = start + PAGE_SIZE;
		if (start < svmm->unmanaged.limit)
			limit = min_t(u64, limit, svmm->unmanaged.start);

		/*
		 * Prepare the GPU-side update of all pages within the
		 * fault window, determining required pages and access
		 * permissions based on pending faults.
		 */
		args.i.p.addr = start;
		args.i.p.page = PAGE_SHIFT;
		args.i.p.size = PAGE_SIZE;
		/*
		 * Determine required permissions based on GPU fault
		 * access flags.
		 */
		switch (buffer->fault[fi]->access) {
		case 0: /* READ. */
			hmm_flags = HMM_PFN_REQ_FAULT;
			break;
		case 2: /* ATOMIC. */
			atomic = true;
			break;
		case 3: /* PREFETCH. */
			hmm_flags = 0;
			break;
		default:
			hmm_flags = HMM_PFN_REQ_FAULT | HMM_PFN_REQ_WRITE;
			break;
		}

		mm = svmm->notifier.mm;
		if (!mmget_not_zero(mm)) {
			nouveau_svm_fault_cancel_fault(svm, buffer->fault[fi]);
			continue;
		}

		notifier.svmm = svmm;
		if (atomic)
			ret = nouveau_atomic_range_fault(svmm, svm->drm,
							 &args.i, sizeof(args),
							 &notifier);
		else
			ret = nouveau_range_fault(svmm, svm->drm, &args.i,
						  sizeof(args), hmm_flags,
						  &notifier);
		mmput(mm);

		limit = args.i.p.addr + args.i.p.size;
		for (fn = fi; ++fn < buffer->fault_nr; ) {
			/* It's okay to skip over duplicate addresses from the
			 * same SVMM as faults are ordered by access type such
			 * that only the first one needs to be handled.
			 *
			 * ie. WRITE faults appear first, thus any handling of
			 * pending READ faults will already be satisfied.
			 * But if a large page is mapped, make sure subsequent
			 * fault addresses have sufficient access permission.
			 */
			if (buffer->fault[fn]->svmm != svmm ||
			    buffer->fault[fn]->addr >= limit ||
			    (buffer->fault[fi]->access == FAULT_ACCESS_READ &&
			     !(args.phys[0] & NVIF_VMM_PFNMAP_V0_V)) ||
			    (buffer->fault[fi]->access != FAULT_ACCESS_READ &&
			     buffer->fault[fi]->access != FAULT_ACCESS_PREFETCH &&
			     !(args.phys[0] & NVIF_VMM_PFNMAP_V0_W)) ||
			    (buffer->fault[fi]->access != FAULT_ACCESS_READ &&
			     buffer->fault[fi]->access != FAULT_ACCESS_WRITE &&
			     buffer->fault[fi]->access != FAULT_ACCESS_PREFETCH &&
			     !(args.phys[0] & NVIF_VMM_PFNMAP_V0_A)))
				break;
		}

		/* If handling failed completely, cancel all faults. */
		if (ret) {
			while (fi < fn) {
				struct nouveau_svm_fault *fault =
					buffer->fault[fi++];

				nouveau_svm_fault_cancel_fault(svm, fault);
			}
		} else
			replay++;
	}

	/* Issue fault replay to the GPU. */
	if (replay)
		nouveau_svm_fault_replay(svm);
}

static int
nouveau_svm_event(struct nvif_event *event, void *argv, u32 argc)
{
	struct nouveau_svm_fault_buffer *buffer = container_of(event, typeof(*buffer), notify);

	schedule_work(&buffer->work);
	return NVIF_EVENT_KEEP;
}

static struct nouveau_pfnmap_args *
nouveau_pfns_to_args(void *pfns)
{
	return container_of(pfns, struct nouveau_pfnmap_args, p.phys);
}

u64 *
nouveau_pfns_alloc(unsigned long npages)
{
	struct nouveau_pfnmap_args *args;

	args = kzalloc(struct_size(args, p.phys, npages), GFP_KERNEL);
	if (!args)
		return NULL;

	args->i.type = NVIF_IOCTL_V0_MTHD;
	args->m.method = NVIF_VMM_V0_PFNMAP;
	args->p.page = PAGE_SHIFT;

	return args->p.phys;
}

void
nouveau_pfns_free(u64 *pfns)
{
	struct nouveau_pfnmap_args *args = nouveau_pfns_to_args(pfns);

	kfree(args);
}

void
nouveau_pfns_map(struct nouveau_svmm *svmm, struct mm_struct *mm,
		 unsigned long addr, u64 *pfns, unsigned long npages)
{
	struct nouveau_pfnmap_args *args = nouveau_pfns_to_args(pfns);

	args->p.addr = addr;
	args->p.size = npages << PAGE_SHIFT;

	mutex_lock(&svmm->mutex);

	nvif_object_ioctl(&svmm->vmm->vmm.object, args,
			  struct_size(args, p.phys, npages), NULL);

	mutex_unlock(&svmm->mutex);
}

static void
nouveau_svm_fault_buffer_fini(struct nouveau_svm *svm, int id)
{
	struct nouveau_svm_fault_buffer *buffer = &svm->buffer[id];

	nvif_event_block(&buffer->notify);
	flush_work(&buffer->work);
}

static int
nouveau_svm_fault_buffer_init(struct nouveau_svm *svm, int id)
{
	struct nouveau_svm_fault_buffer *buffer = &svm->buffer[id];
	struct nvif_object *device = &svm->drm->client.device.object;

	buffer->get = nvif_rd32(device, buffer->getaddr);
	buffer->put = nvif_rd32(device, buffer->putaddr);
	SVM_DBG(svm, "get %08x put %08x (init)", buffer->get, buffer->put);

	return nvif_event_allow(&buffer->notify);
}

static void
nouveau_svm_fault_buffer_dtor(struct nouveau_svm *svm, int id)
{
	struct nouveau_svm_fault_buffer *buffer = &svm->buffer[id];
	int i;

	if (!nvif_object_constructed(&buffer->object))
		return;

	nouveau_svm_fault_buffer_fini(svm, id);

	if (buffer->fault) {
		for (i = 0; buffer->fault[i] && i < buffer->entries; i++)
			kfree(buffer->fault[i]);
		kvfree(buffer->fault);
	}

	nvif_event_dtor(&buffer->notify);
	nvif_object_dtor(&buffer->object);
}

static int
nouveau_svm_fault_buffer_ctor(struct nouveau_svm *svm, s32 oclass, int id)
{
	struct nouveau_svm_fault_buffer *buffer = &svm->buffer[id];
	struct nouveau_drm *drm = svm->drm;
	struct nvif_object *device = &drm->client.device.object;
	struct nvif_clb069_v0 args = {};
	int ret;

	buffer->id = id;

	ret = nvif_object_ctor(device, "svmFaultBuffer", 0, oclass, &args,
			       sizeof(args), &buffer->object);
	if (ret < 0) {
		SVM_ERR(svm, "Fault buffer allocation failed: %d", ret);
		return ret;
	}

	nvif_object_map(&buffer->object, NULL, 0);
	buffer->entries = args.entries;
	buffer->getaddr = args.get;
	buffer->putaddr = args.put;
	INIT_WORK(&buffer->work, nouveau_svm_fault);

	ret = nvif_event_ctor(&buffer->object, "svmFault", id, nouveau_svm_event, true, NULL, 0,
			      &buffer->notify);
	if (ret)
		return ret;

	buffer->fault = kvcalloc(buffer->entries, sizeof(*buffer->fault), GFP_KERNEL);
	if (!buffer->fault)
		return -ENOMEM;

	return nouveau_svm_fault_buffer_init(svm, id);
}

void
nouveau_svm_resume(struct nouveau_drm *drm)
{
	struct nouveau_svm *svm = drm->svm;
	if (svm)
		nouveau_svm_fault_buffer_init(svm, 0);
}

void
nouveau_svm_suspend(struct nouveau_drm *drm)
{
	struct nouveau_svm *svm = drm->svm;
	if (svm)
		nouveau_svm_fault_buffer_fini(svm, 0);
}

void
nouveau_svm_fini(struct nouveau_drm *drm)
{
	struct nouveau_svm *svm = drm->svm;
	if (svm) {
		nouveau_svm_fault_buffer_dtor(svm, 0);
		kfree(drm->svm);
		drm->svm = NULL;
	}
}

void
nouveau_svm_init(struct nouveau_drm *drm)
{
	static const struct nvif_mclass buffers[] = {
		{   VOLTA_FAULT_BUFFER_A, 0 },
		{ MAXWELL_FAULT_BUFFER_A, 0 },
		{}
	};
	struct nouveau_svm *svm;
	int ret;

	/* Disable on Volta and newer until channel recovery is fixed,
	 * otherwise clients will have a trivial way to trash the GPU
	 * for everyone.
	 */
	if (drm->client.device.info.family > NV_DEVICE_INFO_V0_PASCAL)
		return;

	drm->svm = svm = kzalloc(struct_size(drm->svm, buffer, 1), GFP_KERNEL);
	if (!drm->svm)
		return;

	drm->svm->drm = drm;
	mutex_init(&drm->svm->mutex);
	INIT_LIST_HEAD(&drm->svm->inst);

	ret = nvif_mclass(&drm->client.device.object, buffers);
	if (ret < 0) {
		SVM_DBG(svm, "No supported fault buffer class");
		nouveau_svm_fini(drm);
		return;
	}

	ret = nouveau_svm_fault_buffer_ctor(svm, buffers[ret].oclass, 0);
	if (ret) {
		nouveau_svm_fini(drm);
		return;
	}

	SVM_DBG(svm, "Initialised");
}
