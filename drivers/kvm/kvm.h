#ifndef __KVM_H
#define __KVM_H

/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <linux/types.h>
#include <linux/hardirq.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/preempt.h>
#include <asm/signal.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>

#include "types.h"

#define KVM_MAX_VCPUS 4
#define KVM_ALIAS_SLOTS 4
#define KVM_MEMORY_SLOTS 8
/* memory slots that does not exposed to userspace */
#define KVM_PRIVATE_MEM_SLOTS 4
#define KVM_PERMILLE_MMU_PAGES 20
#define KVM_MIN_ALLOC_MMU_PAGES 64
#define KVM_NUM_MMU_PAGES 1024
#define KVM_MIN_FREE_MMU_PAGES 5
#define KVM_REFILL_PAGES 25
#define KVM_MAX_CPUID_ENTRIES 40

#define KVM_PIO_PAGE_OFFSET 1

/*
 * vcpu->requests bit members
 */
#define KVM_REQ_TLB_FLUSH          0

#define NR_PTE_CHAIN_ENTRIES 5

struct kvm_pte_chain {
	u64 *parent_ptes[NR_PTE_CHAIN_ENTRIES];
	struct hlist_node link;
};

/*
 * kvm_mmu_page_role, below, is defined as:
 *
 *   bits 0:3 - total guest paging levels (2-4, or zero for real mode)
 *   bits 4:7 - page table level for this shadow (1-4)
 *   bits 8:9 - page table quadrant for 2-level guests
 *   bit   16 - "metaphysical" - gfn is not a real page (huge page/real mode)
 *   bits 17:19 - "access" - the user, writable, and nx bits of a huge page pde
 */
union kvm_mmu_page_role {
	unsigned word;
	struct {
		unsigned glevels : 4;
		unsigned level : 4;
		unsigned quadrant : 2;
		unsigned pad_for_nice_hex_output : 6;
		unsigned metaphysical : 1;
		unsigned hugepage_access : 3;
	};
};

struct kvm_mmu_page {
	struct list_head link;
	struct hlist_node hash_link;

	/*
	 * The following two entries are used to key the shadow page in the
	 * hash table.
	 */
	gfn_t gfn;
	union kvm_mmu_page_role role;

	u64 *spt;
	/* hold the gfn of each spte inside spt */
	gfn_t *gfns;
	unsigned long slot_bitmap; /* One bit set per slot which has memory
				    * in this shadow page.
				    */
	int multimapped;         /* More than one parent_pte? */
	int root_count;          /* Currently serving as active root */
	union {
		u64 *parent_pte;               /* !multimapped */
		struct hlist_head parent_ptes; /* multimapped, kvm_pte_chain */
	};
};

struct kvm_vcpu;
extern struct kmem_cache *kvm_vcpu_cache;

/*
 * x86 supports 3 paging modes (4-level 64-bit, 3-level 64-bit, and 2-level
 * 32-bit).  The kvm_mmu structure abstracts the details of the current mmu
 * mode.
 */
struct kvm_mmu {
	void (*new_cr3)(struct kvm_vcpu *vcpu);
	int (*page_fault)(struct kvm_vcpu *vcpu, gva_t gva, u32 err);
	void (*free)(struct kvm_vcpu *vcpu);
	gpa_t (*gva_to_gpa)(struct kvm_vcpu *vcpu, gva_t gva);
	void (*prefetch_page)(struct kvm_vcpu *vcpu,
			      struct kvm_mmu_page *page);
	hpa_t root_hpa;
	int root_level;
	int shadow_root_level;

	u64 *pae_root;
};

#define KVM_NR_MEM_OBJS 40

/*
 * We don't want allocation failures within the mmu code, so we preallocate
 * enough memory for a single page fault in a cache.
 */
struct kvm_mmu_memory_cache {
	int nobjs;
	void *objects[KVM_NR_MEM_OBJS];
};

struct kvm_guest_debug {
	int enabled;
	unsigned long bp[4];
	int singlestep;
};

struct kvm_pio_request {
	unsigned long count;
	int cur_count;
	struct page *guest_pages[2];
	unsigned guest_page_offset;
	int in;
	int port;
	int size;
	int string;
	int down;
	int rep;
};

struct kvm_vcpu_stat {
	u32 pf_fixed;
	u32 pf_guest;
	u32 tlb_flush;
	u32 invlpg;

	u32 exits;
	u32 io_exits;
	u32 mmio_exits;
	u32 signal_exits;
	u32 irq_window_exits;
	u32 halt_exits;
	u32 halt_wakeup;
	u32 request_irq_exits;
	u32 irq_exits;
	u32 host_state_reload;
	u32 efer_reload;
	u32 fpu_reload;
	u32 insn_emulation;
	u32 insn_emulation_fail;
};

/*
 * It would be nice to use something smarter than a linear search, TBD...
 * Thankfully we dont expect many devices to register (famous last words :),
 * so until then it will suffice.  At least its abstracted so we can change
 * in one place.
 */
struct kvm_io_bus {
	int                   dev_count;
#define NR_IOBUS_DEVS 6
	struct kvm_io_device *devs[NR_IOBUS_DEVS];
};

void kvm_io_bus_init(struct kvm_io_bus *bus);
void kvm_io_bus_destroy(struct kvm_io_bus *bus);
struct kvm_io_device *kvm_io_bus_find_dev(struct kvm_io_bus *bus, gpa_t addr);
void kvm_io_bus_register_dev(struct kvm_io_bus *bus,
			     struct kvm_io_device *dev);

#ifdef CONFIG_HAS_IOMEM
#define KVM_VCPU_MMIO 			\
	int mmio_needed;		\
	int mmio_read_completed;	\
	int mmio_is_write;		\
	int mmio_size;			\
	unsigned char mmio_data[8];	\
	gpa_t mmio_phys_addr;

#else
#define KVM_VCPU_MMIO

#endif

#define KVM_VCPU_COMM 					\
	struct kvm *kvm; 				\
	struct preempt_notifier preempt_notifier;	\
	int vcpu_id;					\
	struct mutex mutex;				\
	int   cpu;					\
	struct kvm_run *run;				\
	int guest_mode;					\
	unsigned long requests;				\
	struct kvm_guest_debug guest_debug;		\
	int fpu_active; 				\
	int guest_fpu_loaded;				\
	wait_queue_head_t wq;				\
	int sigset_active;				\
	sigset_t sigset;				\
	struct kvm_vcpu_stat stat;			\
	KVM_VCPU_MMIO

struct kvm_mem_alias {
	gfn_t base_gfn;
	unsigned long npages;
	gfn_t target_gfn;
};

struct kvm_memory_slot {
	gfn_t base_gfn;
	unsigned long npages;
	unsigned long flags;
	unsigned long *rmap;
	unsigned long *dirty_bitmap;
	unsigned long userspace_addr;
	int user_alloc;
};

struct kvm_vm_stat {
	u32 mmu_shadow_zapped;
	u32 mmu_pte_write;
	u32 mmu_pte_updated;
	u32 mmu_pde_zapped;
	u32 mmu_flooded;
	u32 mmu_recycled;
	u32 remote_tlb_flush;
};

struct kvm {
	struct mutex lock; /* protects everything except vcpus */
	struct mm_struct *mm; /* userspace tied to this vm */
	int naliases;
	struct kvm_mem_alias aliases[KVM_ALIAS_SLOTS];
	int nmemslots;
	struct kvm_memory_slot memslots[KVM_MEMORY_SLOTS +
					KVM_PRIVATE_MEM_SLOTS];
	/*
	 * Hash table of struct kvm_mmu_page.
	 */
	struct list_head active_mmu_pages;
	unsigned int n_free_mmu_pages;
	unsigned int n_requested_mmu_pages;
	unsigned int n_alloc_mmu_pages;
	struct hlist_head mmu_page_hash[KVM_NUM_MMU_PAGES];
	struct kvm_vcpu *vcpus[KVM_MAX_VCPUS];
	struct list_head vm_list;
	struct file *filp;
	struct kvm_io_bus mmio_bus;
	struct kvm_io_bus pio_bus;
	struct kvm_pic *vpic;
	struct kvm_ioapic *vioapic;
	int round_robin_prev_vcpu;
	unsigned int tss_addr;
	struct page *apic_access_page;
	struct kvm_vm_stat stat;
};

static inline struct kvm_pic *pic_irqchip(struct kvm *kvm)
{
	return kvm->vpic;
}

static inline struct kvm_ioapic *ioapic_irqchip(struct kvm *kvm)
{
	return kvm->vioapic;
}

static inline int irqchip_in_kernel(struct kvm *kvm)
{
	return pic_irqchip(kvm) != NULL;
}

struct descriptor_table {
	u16 limit;
	unsigned long base;
} __attribute__((packed));

/* The guest did something we don't support. */
#define pr_unimpl(vcpu, fmt, ...)					\
 do {									\
	if (printk_ratelimit())						\
		printk(KERN_ERR "kvm: %i: cpu%i " fmt,			\
		       current->tgid, (vcpu)->vcpu_id , ## __VA_ARGS__); \
 } while (0)

#define kvm_printf(kvm, fmt ...) printk(KERN_DEBUG fmt)
#define vcpu_printf(vcpu, fmt...) kvm_printf(vcpu->kvm, fmt)

int kvm_vcpu_init(struct kvm_vcpu *vcpu, struct kvm *kvm, unsigned id);
void kvm_vcpu_uninit(struct kvm_vcpu *vcpu);

void vcpu_load(struct kvm_vcpu *vcpu);
void vcpu_put(struct kvm_vcpu *vcpu);

void decache_vcpus_on_cpu(int cpu);


int kvm_init(void *opaque, unsigned int vcpu_size,
		  struct module *module);
void kvm_exit(void);

#define HPA_MSB ((sizeof(hpa_t) * 8) - 1)
#define HPA_ERR_MASK ((hpa_t)1 << HPA_MSB)
static inline int is_error_hpa(hpa_t hpa) { return hpa >> HPA_MSB; }
struct page *gva_to_page(struct kvm_vcpu *vcpu, gva_t gva);

extern struct page *bad_page;

int is_error_page(struct page *page);
int kvm_is_error_hva(unsigned long addr);
int kvm_set_memory_region(struct kvm *kvm,
			  struct kvm_userspace_memory_region *mem,
			  int user_alloc);
int __kvm_set_memory_region(struct kvm *kvm,
			    struct kvm_userspace_memory_region *mem,
			    int user_alloc);
int kvm_arch_set_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem,
				struct kvm_memory_slot old,
				int user_alloc);
gfn_t unalias_gfn(struct kvm *kvm, gfn_t gfn);
struct page *gfn_to_page(struct kvm *kvm, gfn_t gfn);
void kvm_release_page_clean(struct page *page);
void kvm_release_page_dirty(struct page *page);
int kvm_read_guest_page(struct kvm *kvm, gfn_t gfn, void *data, int offset,
			int len);
int kvm_read_guest(struct kvm *kvm, gpa_t gpa, void *data, unsigned long len);
int kvm_write_guest_page(struct kvm *kvm, gfn_t gfn, const void *data,
			 int offset, int len);
int kvm_write_guest(struct kvm *kvm, gpa_t gpa, const void *data,
		    unsigned long len);
int kvm_clear_guest_page(struct kvm *kvm, gfn_t gfn, int offset, int len);
int kvm_clear_guest(struct kvm *kvm, gpa_t gpa, unsigned long len);
struct kvm_memory_slot *gfn_to_memslot(struct kvm *kvm, gfn_t gfn);
int kvm_is_visible_gfn(struct kvm *kvm, gfn_t gfn);
void mark_page_dirty(struct kvm *kvm, gfn_t gfn);

void kvm_vcpu_block(struct kvm_vcpu *vcpu);
void kvm_resched(struct kvm_vcpu *vcpu);
void kvm_load_guest_fpu(struct kvm_vcpu *vcpu);
void kvm_put_guest_fpu(struct kvm_vcpu *vcpu);
void kvm_flush_remote_tlbs(struct kvm *kvm);

long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg);
long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg);
void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu);

int kvm_dev_ioctl_check_extension(long ext);

int kvm_get_dirty_log(struct kvm *kvm,
			struct kvm_dirty_log *log, int *is_dirty);
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
				struct kvm_dirty_log *log);

int kvm_vm_ioctl_set_memory_region(struct kvm *kvm,
				   struct
				   kvm_userspace_memory_region *mem,
				   int user_alloc);
long kvm_arch_vm_ioctl(struct file *filp,
		       unsigned int ioctl, unsigned long arg);
void kvm_arch_destroy_vm(struct kvm *kvm);

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu);
int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu);

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
				    struct kvm_translation *tr);

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs);
int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs);
int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs);
int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs);
int kvm_arch_vcpu_ioctl_debug_guest(struct kvm_vcpu *vcpu,
				    struct kvm_debug_guest *dbg);
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run);

int kvm_arch_init(void *opaque);
void kvm_arch_exit(void);

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu);

void kvm_arch_vcpu_free(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu);
struct kvm_vcpu *kvm_arch_vcpu_create(struct kvm *kvm, unsigned int id);
int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu);

int kvm_arch_vcpu_reset(struct kvm_vcpu *vcpu);
void kvm_arch_hardware_enable(void *garbage);
void kvm_arch_hardware_disable(void *garbage);
int kvm_arch_hardware_setup(void);
void kvm_arch_hardware_unsetup(void);
void kvm_arch_check_processor_compat(void *rtn);

void kvm_free_physmem(struct kvm *kvm);

struct  kvm *kvm_arch_create_vm(void);
void kvm_arch_destroy_vm(struct kvm *kvm);

static inline void kvm_guest_enter(void)
{
	account_system_vtime(current);
	current->flags |= PF_VCPU;
}

static inline void kvm_guest_exit(void)
{
	account_system_vtime(current);
	current->flags &= ~PF_VCPU;
}

static inline int memslot_id(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	return slot - kvm->memslots;
}

static inline gpa_t gfn_to_gpa(gfn_t gfn)
{
	return (gpa_t)gfn << PAGE_SHIFT;
}

enum kvm_stat_kind {
	KVM_STAT_VM,
	KVM_STAT_VCPU,
};

struct kvm_stats_debugfs_item {
	const char *name;
	int offset;
	enum kvm_stat_kind kind;
	struct dentry *dentry;
};
extern struct kvm_stats_debugfs_item debugfs_entries[];

#endif
