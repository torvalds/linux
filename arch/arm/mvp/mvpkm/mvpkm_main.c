/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Hypervisor Support
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 * @file
 *
 * @brief The kernel level driver.
 */

#define __KERNEL_SYSCALLS__
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/syscalls.h>
#include <linux/kmod.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/smp.h>
#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/sysfs.h>
#include <linux/pid.h>
#include <linux/highmem.h>
#include <linux/syscalls.h>

#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif

#include <net/sock.h>

#include <asm/cacheflush.h>
#include <asm/memory.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include "mvp.h"
#include "mvp_version.h"
#include "mvpkm_types.h"
#include "mvpkm_private.h"
#include "mvpkm_kernel.h"
#include "actions.h"
#include "wscalls.h"
#include "arm_inline.h"
#include "tsc.h"
#include "mksck_kernel.h"
#include "mmu_types.h"
#include "mvp_timer.h"
#include "qp.h"
#include "qp_host_kernel.h"
#include "cpufreq_kernel.h"
#include "mvpkm_comm_ev.h"
#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER
#include "mvp_balloon.h"
#endif


/*********************************************************************
 *
 * Definition of the file operations
 *
 *********************************************************************/
static _Bool LockedListAdd(MvpkmVM *vm,
                           __u32 mpn,
                           __u32 order,
                           PhysMem_RegionType forRegion);
static _Bool LockedListDel(MvpkmVM *vm, __u32 mpn);
static void  LockedListUnlockAll(MvpkmVM *vm);
static _Bool LockedListLookup(MvpkmVM *vm, __u32 mpn);
static int   SetupMonitor(MvpkmVM *vm);
static int   RunMonitor(MvpkmVM *vm);
static MPN   AllocZeroedFreePages(MvpkmVM *vm,
                                  uint32 order,
                                  _Bool highmem,
                                  PhysMem_RegionType forRegion,
                                  HKVA *hkvaRet);
static HKVA  MapWSPHKVA(MvpkmVM *vm, HkvaMapInfo *mapInfo);
static void  UnmapWSPHKVA(MvpkmVM *vm);
static int   MvpkmWaitForInt(MvpkmVM *vm, _Bool suspend);
static void  ReleaseVM(MvpkmVM *vm);

/*
 * Mksck open request must come from this uid. It must be root until
 * it is set via an ioctl from mvpd.
 */
uid_t Mvpkm_vmwareUid = 0;
EXPORT_SYMBOL(Mvpkm_vmwareUid);

/*
 * Minimum hidden app oom_adj, provided by mvpd, since we can't get it directly
 * from the lowmemorykiller module.
 */
static int minHiddenAppOOMAdj;

/*
 * vCPU cpu affinity to let monitor/guest run on some CPUs only (when possible)
 */
static DECLARE_BITMAP(vcpuAffinity, NR_CPUS);

/*********************************************************************
 *
 * Sysfs nodes
 *
 *********************************************************************/
/*
 * kobject for our sysfs representation, used for global nodes.
 */
static struct kobject *mvpkmKObj;

/*
 * kobject for the balloon exports.
 */
static struct kobject *balloonKObj;

/**
 * @brief sysfs show function for global version attribute.
 *
 * @param kobj reference to kobj nested in MvpkmVM struct.
 * @param attr kobj_attribute reference, not used.
 * @param buf PAGE_SIZEd buffer to write to.
 *
 * @return number of characters printed (not including trailing null character).
 */
static ssize_t
version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
   return snprintf(buf, PAGE_SIZE, MVP_VERSION_FORMATSTR "\n", MVP_VERSION_FORMATARGS);
}

static struct kobj_attribute versionAttr = __ATTR_RO(version);

/**
 * @brief sysfs show function for global background_pages attribute.
 *
 * Used by vmx balloon policy controller to gauge the amount of freeable
 * anonymous memory.
 *
 * @param kobj reference to kobj nested in MvpkmVM struct.
 * @param attr kobj_attribute reference, not used.
 * @param buf PAGE_SIZEd buffer to write to.
 *
 * @return number of characters printed (not including trailing null character).
 */
static ssize_t
background_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
#ifndef CONFIG_ANDROID_LOW_MEMORY_KILLER
   return snprintf(buf, PAGE_SIZE, "0\n");
#else
   return snprintf(buf, PAGE_SIZE, "%d\n", Balloon_AndroidBackgroundPages(minHiddenAppOOMAdj));
#endif
}

static struct kobj_attribute backgroundAttr = __ATTR_RO(background);

/**
 * @brief sysfs show function to export the other_file calculation in
 *        lowmemorykiller.
 *
 * It's helpful, in the balloon controller, to know what the lowmemorykiller
 * module is using to know when the system has crossed a minfree threshold.
 * Since there exists a number of different other_file calculations in various
 * lowmemorykiller patches (@see{MVP-1674}), and the module itself doesn't
 * provide a clean export of this figure, we provide it on a case-by-case basis
 * for the various supported hosts here.
 *
 * @param kobj reference to kobj nested in MvpkmVM struct.
 * @param attr kobj_attribute reference, not used.
 * @param buf PAGE_SIZEd buffer to write to.
 *
 * @return number of characters printed (not including trailing null character).
 */
static ssize_t
other_file_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
   int32 other_file = 0;

#ifndef LOWMEMKILLER_VARIANT
#define LOWMEMKILLER_VARIANT 0
#endif

#ifndef LOWMEMKILLER_MD5
#define LOWMEMKILLER_MD5 0
#endif

#ifndef LOWMEMKILLER_SHRINK_MD5
#define LOWMEMKILLER_SHRINK_MD5 0
#endif

   /*
    * The build system hashes the lowmemorykiller section related to the
    * other_file calculation in the kernel source for us, here we have to
    * provide the code.
    */
#if LOWMEMKILLER_VARIANT == 1
   /*
    * This is the same as the non-exported global_reclaimable_pages() when there
    * is no swap.
    */
   other_file = global_page_state(NR_ACTIVE_FILE) +
      global_page_state(NR_INACTIVE_FILE);
#elif LOWMEMKILLER_VARIANT == 2
   other_file = global_page_state(NR_FILE_PAGES);
#elif LOWMEMKILLER_VARIANT == 3
   other_file = global_page_state(NR_FILE_PAGES) - global_page_state(NR_SHMEM);
#elif LOWMEMKILLER_VARIANT == 4
   /*
    * Here free/file pages are fungible and max(free, file) isn't used, but we
    * can continue to use max(free, file) since max(free, file) = other_file in
    * this case.
    */
   other_file = global_page_state(NR_FREE_PAGES) + global_page_state(NR_FILE_PAGES);
#elif defined(NONANDROID)
   /*
    * Non-Android host platforms don't have ballooning enabled.
    */
#else
   /*
    * If you get this message, you need to run 'make lowmem-info' and inspect
    * lowmemorykiller.c. If the "other_file = ..." calculation in lowmem_shrink
    * appears above, simply add the "Shrink#" to an existing entry in
    * lowmemkiller-variant.sh, pointing to the variant number above. Otherwise,
    * provide a new entry above and variant number, with the appropriate
    * other_file calculation and update lowmemkiller-variant.sh accordingly.
    */
//#warning "Unknown lowmemorykiller variant in hosted/module/mvpkm_main.c, falling back on default (see other_file_show for the remedy)"
   /*
    * Fall back on default - this may bias strangely for/against the host, but
    * nothing catastrophic should result.
    */
   other_file = global_page_state(NR_FILE_PAGES);
#endif

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)
   return snprintf(buf,
                   PAGE_SIZE,
                   "%d %d %s %s\n",
                   other_file,
                   LOWMEMKILLER_VARIANT,
                   STRINGIFY(LOWMEMKILLER_MD5),
                   STRINGIFY(LOWMEMKILLER_SHRINK_MD5));
#undef _STRINGIFY
#undef STRINGIFY
}

static struct kobj_attribute otherFileAttr = __ATTR_RO(other_file);

/*
 * kset for our sysfs representation, used for per-VM nodes.
 */
static struct kset *mvpkmKSet;

static ssize_t MvpkmAttrShow(struct kobject *kobj,
                             struct attribute *attr,
                             char *buf);
static ssize_t MvpkmAttrStore(struct kobject *kobj,
                              struct attribute *attr,
                              const char *buf,
                              size_t count);

static void MvpkmKObjRelease(struct kobject *kobj)
   __attribute__ ((optimize ("-fomit-frame-pointer")));


/**
 * @brief Releases the vm structure containing the kobject.
 *
 * @param kobj the vm's kobject.
 */

static void
MvpkmKObjRelease(struct kobject *kobj)
{
   MvpkmVM *vm = container_of(kobj, MvpkmVM, kobj);

   ReleaseVM(vm);

   module_put(THIS_MODULE);
}


/**
 * @name mvpkm ktype attribute structures for locked_pages.
 *
 * @{
 */
static struct sysfs_ops mvpkmSysfsOps = {
   .show = MvpkmAttrShow,
   .store = MvpkmAttrStore
};

static struct attribute mvpkmLockedPagesAttr = {
   .name = "locked_pages",
   .mode = 0444,
};

static struct attribute mvpkmBalloonWatchdogAttr = {
   .name = "balloon_watchdog",
   .mode = 0666
};

static struct attribute mvpkmMonitorAttr = {
   .name = "monitor",
   .mode = 0400,
};

static struct attribute *mvpkmDefaultAttrs[] = {
   &mvpkmLockedPagesAttr,
   &mvpkmBalloonWatchdogAttr,
   &mvpkmMonitorAttr,
   NULL,
};

static struct kobj_type mvpkmKType = {
   .sysfs_ops = &mvpkmSysfsOps,
   .release = MvpkmKObjRelease,
   .default_attrs = mvpkmDefaultAttrs,
};
/*@}*/

/*
 * As it is not very common for host kernels to have SYS_HYPERVISOR enabled and
 * you have to "hack" a Kconfig file to enable it, just include the
 * functionality inline if it is not enabled.
 */
#ifndef CONFIG_SYS_HYPERVISOR
struct kobject *hypervisor_kobj;
EXPORT_SYMBOL_GPL(hypervisor_kobj);
#endif


/*
 * kobject and kset utilities.
 */

extern struct kobject *kset_find_obj(struct kset *, const char *)
   __attribute__((weak));


/**
 * @brief Finds a kobject in a kset. The actual implementation is copied from
 *    kernel source in lib/kobject.c. Although the symbol is extern-declared,
 *    it is not EXPORT_SYMBOL-ed. We use a weak reference in case the symbol
 *    might be exported in future kernel versions.
 *
 * @param kset set to search.
 * @param name object name.
 *
 * @return retained kobject if found, NULL otherwise.
 */

struct kobject *
kset_find_obj(struct kset *kset,
              const char *name)
{
   struct kobject *k;
   struct kobject *ret = NULL;

   spin_lock(&kset->list_lock);
   list_for_each_entry(k, &kset->list, entry) {
      if (kobject_name(k) && !strcmp(kobject_name(k), name)) {
         ret = kobject_get(k);
         break;
     }
   }
   spin_unlock(&kset->list_lock);
   return ret;
}


/**
 * @brief Finds one of the VM's pre-defined ksets.
 *
 * @param vmID a VM ID.
 * @param name name of one of the VM's pre-defined ksets.
 *
 * @return retained kset if found, NULL otherwise.
 */

struct kset *
Mvpkm_FindVMNamedKSet(int vmID,
                      const char *name)
{
   MvpkmVM *vm;
   struct kobject *kobj;
   char vmName[32] = {}; /* Large enough to hold externally-formatted int32. */
   struct kset *res = NULL;

   if (!mvpkmKSet) {
      return NULL;
   }

   snprintf(vmName, sizeof vmName, "%d", vmID);
   vmName[sizeof vmName - 1] = '\0'; /* Always null-terminate, no overflow. */

   kobj = kset_find_obj(mvpkmKSet, vmName);
   if (!kobj) {
      return NULL;
   }

   vm = container_of(kobj, MvpkmVM, kobj);

   if (!strcmp(name, "devices")) {
      res = kset_get(vm->devicesKSet);
   } else if (!strcmp(name, "misc")) {
      res = kset_get(vm->miscKSet);
   }

   kobject_put(kobj);
   return res;
}

EXPORT_SYMBOL(Mvpkm_FindVMNamedKSet);



/*********************************************************************
 *
 * Standard Linux miscellaneous device registration
 *
 *********************************************************************/

MODULE_LICENSE("GPL"); // for kallsyms_lookup_name

static int MvpkmFault(struct vm_area_struct *vma, struct vm_fault *vmf);


/**
 * @brief Linux vma operations for /dev/mem-like kernel module mmap. We
 *        enforce the restriction that only MPNs that have been allocated
 *        to the opened VM may be mapped and also increment the reference
 *        count (via vm_insert_page), so that even if the memory is later
 *        freed by the VM, host process vma's containing the MPN can't
 *        compromise the system.
 *
 *        However, only trusted host processes (e.g. the vmx) should be allowed
 *        to use this interface, since you can mmap the monitor's code/data/
 *        page tables etc. with it. Untrusted host processes are limited to
 *        typed messages for sharing memory with the monitor. Unix file system
 *        access permissions are the intended method of restricting access.
 *        Unfortunately, today _any_ host process utilizing Mksck requires
 *        access to mvpkm to setup its Mksck pages and obtain socket info via
 *        ioctls - we probably should be exporting two devices, one for trusted
 *        and one for arbitrary host processes to avoid this confusion of
 *        concerns.
 */
static struct vm_operations_struct mvpkmVMOps = {
   .fault = MvpkmFault
};

/*
 * Generic kernel module file ops. These functions will be registered
 * at the time the kernel module is loaded.
 */
static long MvpkmUnlockedIoctl(struct file *filep,
                               unsigned int cmd,
                               unsigned long arg);
static int MvpkmOpen(struct inode *inode, struct file *filp);
static int MvpkmRelease(struct inode *inode, struct file *filp);
static int MvpkmMMap(struct file *file, struct vm_area_struct *vma);

/**
 * @brief the file_operation structure contains the callback functions
 *        that are registered with Linux to handle file operations on
 *        the mvpkm device.
 *
 *        The structure contains other members that the mvpkm device
 *        does not use. Those members are auto-initialized to NULL.
 *
 *        WARNING, this structure has changed after Linux kernel 2.6.19:
 *        readv/writev are changed to aio_read/aio_write (neither is used here).
 */
static const struct file_operations mvpkmFileOps = {
   .owner            = THIS_MODULE,
   .unlocked_ioctl   = MvpkmUnlockedIoctl,
   .open             = MvpkmOpen,
   .release          = MvpkmRelease,
   .mmap             = MvpkmMMap
};

/**
 * @brief The mvpkm device identifying information to be used to register
 *        the device with the Linux kernel.
 */
static struct miscdevice mvpkmDev = {
   .minor  = 165,
   .name   = "mvpkm",
   .fops   = &mvpkmFileOps
};

/**
 * Mvpkm is loaded by mvpd and only mvpd will be allowed to open
 * it. There is a very simple way to verify that: record the process
 * id (thread group id) at the time the module is loaded and test it
 * at the time the module is opened.
 */
static struct pid *initTgid;


#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER
/**
 * @name Slab shrinker for triggering balloon adjustment.
 *
 * @note shrinker us used as a trigger for guest balloon.
 *
 * @{
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
static int MvpkmShrink(struct shrinker *this, struct shrink_control *sc);
#else
static int MvpkmShrink(struct shrinker *this, int nrToScan, gfp_t gfpMask);
#endif

static struct shrinker mvpkmShrinker = {
   .shrink = MvpkmShrink,
   .seeks = DEFAULT_SEEKS
};
/*@}*/
#endif

module_param_array(vcpuAffinity, ulong, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(vcpuAffinity, "vCPU affinity");


/**
 * @brief Initialize the mvpkm device, register it with the Linux kernel.
 *
 * @return A zero is returned on success and a negative errno code for failure.
 *         (Same as the return policy of misc_register(9).)
 */

static int __init
MvpkmInit(void)
{
   int err = 0;
   _Bool mksckInited = false;
   _Bool cpuFreqInited = false;

   printk(KERN_INFO "Mvpkm: " MVP_VERSION_FORMATSTR "\n", MVP_VERSION_FORMATARGS);
   printk(KERN_INFO "Mvpkm: loaded from process %s tgid=%d, pid=%d\n",
          current->comm,
          task_tgid_vnr(current),
          task_pid_vnr(current));

   if (bitmap_empty(vcpuAffinity, NR_CPUS)) {
      bitmap_copy(vcpuAffinity, cpumask_bits(cpu_possible_mask), NR_CPUS);
   }

   if ((err = misc_register(&mvpkmDev))) {
      return -ENOENT;
   }

   if ((err = Mksck_Init())) {
      goto error;
   } else {
      mksckInited = true;
   }

   QP_HostInit();

   CpuFreq_Init();
   cpuFreqInited = true;

   /*
    * Reference mvpd (module loader) tgid struct, so that we can avoid
    * attacks based on pid number wraparound.
    */
   initTgid = get_pid(task_tgid(current));

#ifndef CONFIG_SYS_HYPERVISOR
   hypervisor_kobj = kobject_create_and_add("hypervisor", NULL);
   if (!hypervisor_kobj) {
      err = -ENOMEM;
      goto error;
   }
#endif

   if (!(mvpkmKObj = kobject_create_and_add("mvp", hypervisor_kobj)) ||
       !(balloonKObj = kobject_create_and_add("lowmem", mvpkmKObj)) ||
       !(mvpkmKSet = kset_create_and_add("vm", NULL, mvpkmKObj))) {
      err = -ENOMEM;
      goto error;
   }

   if ((err = sysfs_create_file(mvpkmKObj, &versionAttr.attr))) {
      goto error;
   }

   if ((err = sysfs_create_file(balloonKObj, &backgroundAttr.attr))) {
      goto error;
   }

   if ((err = sysfs_create_file(balloonKObj, &otherFileAttr.attr))) {
      goto error;
   }

#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER
   register_shrinker(&mvpkmShrinker);
#endif

   MksckPageInfo_Init();

   return 0;

error:
   if (mvpkmKSet) {
      kset_unregister(mvpkmKSet);
   }

   if (balloonKObj) {
      kobject_del(balloonKObj);
      kobject_put(balloonKObj);
   }

   if (mvpkmKObj) {
      kobject_del(mvpkmKObj);
      kobject_put(mvpkmKObj);
   }

#ifndef CONFIG_SYS_HYPERVISOR
   if (hypervisor_kobj) {
      kobject_del(hypervisor_kobj);
      kobject_put(hypervisor_kobj);
   }
#endif

   if (cpuFreqInited) {
      CpuFreq_Exit();
   }

   if (mksckInited) {
      Mksck_Exit();
   }

   if (initTgid) {
      put_pid(initTgid);
   }

   misc_deregister(&mvpkmDev);
   return err;
}

/**
 * @brief De-register the mvpkm device with the Linux kernel.
 */
void
MvpkmExit(void)
{
   PRINTK(KERN_INFO "MvpkmExit called !\n");

   MksckPageInfo_Exit();

#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER
   unregister_shrinker(&mvpkmShrinker);
#endif

   kset_unregister(mvpkmKSet);
   kobject_del(balloonKObj);
   kobject_put(balloonKObj);
   kobject_del(mvpkmKObj);
   kobject_put(mvpkmKObj);
#ifndef CONFIG_SYS_HYPERVISOR
   kobject_del(hypervisor_kobj);
   kobject_put(hypervisor_kobj);
#endif

   CpuFreq_Exit();

   Mksck_Exit();

   put_pid(initTgid);

   misc_deregister(&mvpkmDev);
}

/*
 * The standard module registration macros of Linux.
 */
module_init(MvpkmInit);
module_exit(MvpkmExit);

module_param_named(minHiddenAppOOMAdj, minHiddenAppOOMAdj, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(minHiddenAppOOMAdj, "minimum hidden app oom_adj, as per lowmemorykiller");

#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER
/**
 * @brief Balloon watchdog timeout callback.
 *
 * Terminate the VM since it's not responsive.
 *
 * @param data vm reference representation.
 */
static void
WatchdogCB(unsigned long data)
{
   MvpkmVM *vm = (MvpkmVM *)data;

   printk("Balloon watchdog expired (%d s)!\n", BALLOON_WATCHDOG_TIMEOUT_SECS);

   Mvpkm_WakeGuest(vm, ACTION_ABORT);
}

/**
 * @brief Slab shrinker.
 *
 * Called by Linux kernel when we're under memory pressure. We treat all locked
 * pages as a slab for this purpose, similar to the Android low memory killer.
 *
 * @param this     reference to registered shrinker for callback context.
 * @param nrToScan number of entries to scan. If 0 then just return the number
 *                 of present entries. We ignore the value of nrToScan when > 1
 *                 since the shrinker is a trigger to readjust guest balloons,
 *                 where the actual balloon size is determined in conjunction
 *                 with the guest.
 * @param gfpMask ignored.
 *
 * @return number of locked pages.
 */
static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
MvpkmShrink(struct shrinker *this, struct shrink_control *sc)
#else
MvpkmShrink(struct shrinker *this, int nrToScan, gfp_t gfpMask)
#endif
{
   uint32 locked = 0;
   struct kobject *k;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
   int nrToScan = sc->nr_to_scan;
#endif

   spin_lock(&mvpkmKSet->list_lock);

   list_for_each_entry(k, &mvpkmKSet->list, entry) {
      MvpkmVM *vm = container_of(k, MvpkmVM, kobj);

      locked += ATOMIC_GETO(vm->usedPages);

      /*
       * Try and grab the WSP semaphore - if we fail, we must be VM setup or
       * teardown, no point trying to wake the guest.
       */
      if (nrToScan > 0 &&
          down_read_trylock(&vm->wspSem)) {

         if (vm->wsp) {
            Mvpkm_WakeGuest(vm, ACTION_BALLOON);

            /*
             * Balloon watchdog.
             */
            if (vm->balloonWDEnabled) {
               struct timer_list *t = &vm->balloonWDTimer;

               if (!timer_pending(t)) {
                  t->data = (unsigned long)vm;
                  t->function = WatchdogCB;
                  t->expires = jiffies + BALLOON_WATCHDOG_TIMEOUT_SECS * HZ;
                  add_timer(t);
               }
            }
         }

         up_read(&vm->wspSem);
      }
   }

   spin_unlock(&mvpkmKSet->list_lock);

   return locked;
}
#endif


/**
 * @brief The open file operation. Initializes the vm specific structure.
 */
int
MvpkmOpen(struct inode *inode, struct file *filp)
{
   MvpkmVM *vm;

   if (initTgid != task_tgid(current)) {
      printk(KERN_ERR "%s: MVPKM can be opened only from MVPD (process %d).\n",
             __FUNCTION__, pid_vnr(initTgid));
      return -EPERM;
   }
   printk(KERN_DEBUG "%s: Allocating an MvpkmVM structure from process %s tgid=%d, pid=%d\n",
          __FUNCTION__,
          current->comm,
          task_tgid_vnr(current),
          task_pid_vnr(current));

   vm = kmalloc(sizeof(MvpkmVM), GFP_KERNEL);
   if (!vm) {
      return -ENOMEM;
   }

   memset(vm, 0, sizeof *vm);

   init_timer(&vm->balloonWDTimer);
   init_rwsem(&vm->lockedSem);
   init_rwsem(&vm->wspSem);
   init_rwsem(&vm->monThreadTaskSem);
   vm->monThreadTask = NULL;
   vm->isMonitorInited = false;

   filp->private_data = vm;

   if (!Mvpkm_vmwareUid) {
      Mvpkm_vmwareUid = current_euid();
   }

   return 0;
}

/**
 * @brief Releases a VMs resources
 * @param  vm vm to release
 */
static void
ReleaseVM(MvpkmVM *vm)
{
   del_timer_sync(&vm->balloonWDTimer);

   down_write(&vm->wspSem);

   if (vm->isMonitorInited) {
      MonitorTimer_Request(&vm->monTimer, 0);
#ifdef CONFIG_HAS_WAKELOCK
      wake_lock_destroy(&vm->wakeLock);
#endif
      Mksck_WspRelease(vm->wsp);
      vm->wsp = NULL;
   }

   up_write(&vm->wspSem);

   LockedListUnlockAll(vm);

   UnmapWSPHKVA(vm);

   /*
    * All sockets potentially connected to sockets of this vm's vmId will fail
    * at send now. DGRAM sockets are note required to tear down connection
    * explicitly.
    */

   kfree(vm);
}

/**
 * @brief The release file operation. Releases the vm specific
 *        structure including all the locked pages.
 *
 * @param inode Unused
 * @param filp  which VM we're dealing with
 * @return 0
 */
int
MvpkmRelease(struct inode *inode, struct file *filp)
{
   MvpkmVM *vm = filp->private_data;

   /*
    * Tear down any queue pairs associated with this VM
    */
   if (vm->isMonitorInited) {
      ASSERT(vm->wsp);
      QP_DetachAll(vm->wsp->guestId);
   }

   /*
    * Release the VM's ksets.
    */

   kset_unregister(vm->miscKSet);
   kset_unregister(vm->devicesKSet);

   if (vm->haveKObj) {
      /*
       * Release the VM's kobject.
       * 'vm' will be kfree-d in its kobject's release function.
       */

      kobject_del(&vm->kobj);
      kobject_put(&vm->kobj);
   } else {
      ReleaseVM(vm);
   }

   filp->private_data = NULL;

   printk(KERN_INFO "%s: Released MvpkmVM structure from process %s tgid=%d, pid=%d\n",
          __FUNCTION__,
          current->comm,
          task_tgid_vnr(current),
          task_pid_vnr(current));

   return 0;
}

/**
 * @brief Page fault handler for /dev/mem-like regions (see mvpkmVMOps
 *        block comment).
 */
static int
MvpkmFault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
   unsigned long address = (unsigned long)vmf->virtual_address;
   MPN mpn = vmf->pgoff;
   MvpkmVM *vm = vma->vm_file->private_data;


   /*
    * Only insert pages belonging to the VM. The check is slow, O(n) in the
    * number of MPNs associated with the VM, but it doesn't matter - the mmap
    * interface should only be used by trusted processes at initialization
    * time and for debugging.
    *
    * The mpn can be either in the memory reserved the monitor or mvpd
    * through the regular mechanisms or it could be a mksck page.
    */
   if (!pfn_valid(mpn)) {
      printk(KERN_ERR "MvpkmMMap: Failed to insert %x @ %lx, mpn invalid\n",
             mpn,
             address);
   } else if (LockedListLookup(vm, mpn)) {
      if (vm_insert_page(vma, address, pfn_to_page(mpn)) == 0) {
         return VM_FAULT_NOPAGE;
      }

      printk(KERN_ERR "MvpkmMMap: Failed to insert %x @ %lx \n",
             mpn,
             address);
   } else if (MksckPage_LookupAndInsertPage(vma, address, mpn) == 0) {
      return VM_FAULT_NOPAGE;
   }

   if (vm->stubPageMPN) {
      if (vm_insert_page(vma, address, pfn_to_page(vm->stubPageMPN)) == 0) {
         printk(KERN_INFO "MvpkmMMap: mapped the stub page at %x @ %lx \n",
                mpn,
                address);
         return VM_FAULT_NOPAGE;
      }

      printk(KERN_ERR "MvpkmMMap: Could not insert stub page %x @ %lx \n",
             mpn,
             address);

   }

   return VM_FAULT_SIGBUS;
}

/**
 * @brief sysfs show function for per-VM locked_pages attribute.
 *
 * @param kobj reference to kobj nested in MvpkmVM struct.
 * @param attr attribute reference.
 * @param buf PAGE_SIZEd buffer to write to.
 *
 * @return number of characters printed (not including trailing null character).
 */
static ssize_t
MvpkmAttrShow(struct kobject *kobj,
              struct attribute *attr,
              char *buf)
{
   if (attr == &mvpkmLockedPagesAttr) {
      MvpkmVM *vm = container_of(kobj, MvpkmVM, kobj);

      return snprintf(buf, PAGE_SIZE, "%d\n", ATOMIC_GETO(vm->usedPages));
   } else if (attr == &mvpkmMonitorAttr) {
      MvpkmVM *vm = container_of(kobj, MvpkmVM, kobj);

      return snprintf(buf,
                      PAGE_SIZE,
                      "hostActions %x callno %d\n",
                      ATOMIC_GETO(vm->wsp->hostActions),
                      WSP_Params(vm->wsp)->callno);
   } else {
      return -EPERM;
   }
}

/**
 * @brief sysfs store function for per-VM locked_pages attribute.
 *
 * @param kobj reference to kobj nested in MvpkmVM struct.
 * @param attr attribute reference.
 * @param buf PAGE_SIZEd buffer to write to.
 * @param buf input buffer.
 * @param count input buffer length.
 *
 * @return number of bytes consumed or negative error code.
 */
static ssize_t
MvpkmAttrStore(struct kobject *kobj,
               struct attribute *attr,
               const char *buf,
               size_t count)
{
   if (attr == &mvpkmBalloonWatchdogAttr) {
      MvpkmVM *vm = container_of(kobj, MvpkmVM, kobj);

      /*
       * Enable balloon watchdog on first write.  This includes all ballooning
       * capable guest.
       */
      vm->balloonWDEnabled = true;
      del_timer_sync(&vm->balloonWDTimer);

      return 1;
   } else {
      return -EPERM;
   }
}

/**
 * @brief Map machine address space region into host process.
 *
 * @param file file reference (ignored).
 * @param vma Linux virtual memory area defining the region.
 *
 * @return 0 on success, otherwise error code.
 */
static int
MvpkmMMap(struct file *file, struct vm_area_struct *vma)
{
   vma->vm_ops = &mvpkmVMOps;

   return 0;
}

#ifdef CONFIG_ARM_LPAE
/**
 * @brief Determine host cacheability/shareability attributes.
 *
 * Used to ensure monitor/guest shared mappings are consistent with
 * those of host user/kernel.
 *
 * @param[out] attribMAN when setting up the HW monitor this provides the
 *                       attributes in the generic ARM_MemAttrNormal form,
 *                       suitable for configuring the monitor and guest's
 *                       [H]MAIR0 and setting the shareability attributes of
 *                       the LPAE descriptors.
 */
static void
DetermineMemAttrLPAE(ARM_MemAttrNormal *attribMAN)
{
   /*
    * We use set_pte_ext to sample what {S,TEX,CB} bits Linux is using for
    * normal kernel/user L2D mappings. These bits should be consistent both
    * with each other and what we use in the monitor since we share various
    * pages with both host processes, the kernel module and monitor, and the
    * ARM ARM requires that synonyms have the same cacheability attributes,
    * see end of A3.5.{4,7} ARM DDI 0406A.
    */
   HKVA hkva = __get_free_pages(GFP_KERNEL, 0);

   ARM_LPAE_L3D *pt = (ARM_LPAE_L3D *)hkva;
   ARM_LPAE_L3D *kernL3D = &pt[0], *userL3D = &pt[1];
   uint32 attr, mair0, mair1;

   set_pte_ext((pte_t *)kernL3D, pfn_pte(0, PAGE_KERNEL), 0);
   set_pte_ext((pte_t *)userL3D, pfn_pte(0, PAGE_NONE), 0);

   printk(KERN_INFO
          "DetermineMemAttr: Kernel L3D AttrIndx=%x SH=%x\n",
          kernL3D->blockS1.attrIndx,
          kernL3D->blockS1.sh);

   printk(KERN_INFO
          "DetermineMemAttr: User   L3D AttrIndx=%x SH=%x\n",
          userL3D->blockS1.attrIndx,
          userL3D->blockS1.sh);

   ASSERT(kernL3D->blockS1.attrIndx == userL3D->blockS1.attrIndx);
   ASSERT(kernL3D->blockS1.sh == userL3D->blockS1.sh);

   switch (kernL3D->blockS1.sh) {
      case 0: {
         attribMAN->share = ARM_SHARE_ATTR_NONE;
         break;
      }
      case 2: {
         attribMAN->share = ARM_SHARE_ATTR_OUTER;
         break;
      }
      case 3: {
         attribMAN->share = ARM_SHARE_ATTR_INNER;
         break;
      }
      default: {
         FATAL();
      }
   }

   ARM_MRC_CP15(MAIR0, mair0);
   ARM_MRC_CP15(MAIR1, mair1);

   attr = MVP_EXTRACT_FIELD(kernL3D->blockS1.attrIndx >= 4 ? mair1 : mair0,
                            8 * (kernL3D->blockS1.attrIndx % 4),
                            8);

   /*
    * See B4-1615 ARM DDI 0406C-2c for magic.
    */
#define MAIR_ATTR_2_CACHE_ATTR(x, y) \
   switch (x) { \
      case 2: { \
         (y) = ARM_CACHE_ATTR_NORMAL_WT; \
         break; \
      } \
      case 3: { \
         (y) = ARM_CACHE_ATTR_NORMAL_WB; \
         break; \
      } \
      default: { \
         FATAL(); \
      } \
   }

   MAIR_ATTR_2_CACHE_ATTR(MVP_EXTRACT_FIELD(attr, 2, 2), attribMAN->innerCache);
   MAIR_ATTR_2_CACHE_ATTR(MVP_EXTRACT_FIELD(attr, 6, 2), attribMAN->outerCache);

#undef MAIR_ATTR_2_CACHE_ATTR

   printk(KERN_INFO
          "DetermineMemAttr: innerCache %x outerCache %x share %x\n",
          attribMAN->innerCache,
          attribMAN->outerCache,
          attribMAN->share);

   free_pages(hkva, 0);
}

#else

/**
 * @brief Determine host cacheability/shareability attributes.
 *
 * Used to ensure monitor/guest shared mappings are consistent with
 * those of host user/kernel.
 *
 * @param[out] attribL2D when setting up the LPV monitor a template L2D
 *                       containing cacheability attributes {S, TEX,CB} used by
 *                       host kernel for normal memory mappings. These may be
 *                       used directly for monitor/guest mappings, since both
 *                       worlds share a common {TRE, PRRR, NMRR}.
 * @param[out] attribMAN when setting up TTBR0 in the LPV monitor and the page
 *                       tables for the HW monitor this provides the attributes
 *                       in the generic ARM_MemAttrNormal form, suitable for
 *                       configuring TTBR0 + the monitor and guest's [H]MAIR0
 *                       and setting the shareability attributes of the LPAE
 *                       descriptors.
 */
static void
DetermineMemAttrNonLPAE(ARM_L2D *attribL2D, ARM_MemAttrNormal *attribMAN)
{
   /*
    * We use set_pte_ext to sample what {S,TEX,CB} bits Linux is using for
    * normal kernel/user L2D mappings. These bits should be consistent both
    * with each other and what we use in the monitor since we share various
    * pages with both host processes, the kernel module and monitor, and the
    * ARM ARM requires that synonyms have the same cacheability attributes,
    * see end of A3.5.{4,7} ARM DDI 0406A.
    */
   HKVA hkva = __get_free_pages(GFP_KERNEL, 0);
   uint32 sctlr;
   ARM_L2D *pt = (ARM_L2D *)hkva;
   ARM_L2D *kernL2D = &pt[0], *userL2D = &pt[1];

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 38)
   /*
    * Linux uses the magic 2048 offset in set_pte_ext. See include/asm/pgtable.h
    * for PAGE_NONE and PAGE_KERNEL semantics.
    */
   const uint32 set_pte_ext_offset = 2048;
#else
   /*
    * Linux 2.6.38 switched the order of Linux vs hardware page tables.
    * See mainline d30e45eeabefadc6039d7f876a59e5f5f6cb11c6.
    */
   const uint32 set_pte_ext_offset = 0;
#endif

   set_pte_ext((pte_t *)(kernL2D + set_pte_ext_offset/sizeof(ARM_L2D)),
               pfn_pte(0, PAGE_KERNEL),
               0);
   set_pte_ext((pte_t *)(userL2D + set_pte_ext_offset/sizeof(ARM_L2D)),
               pfn_pte(0, PAGE_NONE),
               0);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
   /*
    * Linux 2.6.38 switched the order of Linux vs hardware page tables.
    * See mainline d30e45eeabefadc6039d7f876a59e5f5f6cb11c6.
    */
   kernL2D += 2048/sizeof(ARM_L2D);
   userL2D += 2048/sizeof(ARM_L2D);
#endif

   printk(KERN_INFO
          "DetermineMemAttr: Kernel L2D TEX=%x CB=%x S=%x\n",
          kernL2D->small.tex,
          kernL2D->small.cb,
          kernL2D->small.s);

   printk(KERN_INFO
          "DetermineMemAttr: User   L2D TEX=%x CB=%x S=%x\n",
          userL2D->small.tex,
          userL2D->small.cb,
          userL2D->small.s);

   ASSERT((kernL2D->small.tex & 1) == (userL2D->small.tex & 1));
   ASSERT(kernL2D->small.cb == userL2D->small.cb);
   ASSERT(kernL2D->small.s == userL2D->small.s);

   *attribL2D = *kernL2D;

   /*
    * We now decode TEX remap and obtain the more generic form for use in
    * the LPV monitor's TTBR0 initialization and the HW monitor.
    */

   ARM_MRC_CP15(CONTROL_REGISTER, sctlr);

   if (sctlr & ARM_CP15_CNTL_TRE) {
      uint32 prrr, nmrr, indx, type, innerCache, outerCache, outerShare,
             share;

      printk(KERN_INFO
             "DetermineMemAttr: TEX remapping enabled\n");

      ARM_MRC_CP15(PRIMARY_REGION_REMAP, prrr);
      ARM_MRC_CP15(NORMAL_MEMORY_REMAP, nmrr);

      printk(KERN_INFO
             "DetermineMemAttr: PRRR=%x NMRR=%x\n",
             prrr,
             nmrr);

      /*
       * Decode PRRR/NMRR below. See B3.7 ARM DDI 0406B for register
       * encodings, tables and magic numbers.
       */

      indx = (MVP_BIT(kernL2D->small.tex, 0) << 2) | kernL2D->small.cb;

      /*
       * Only normal memory makes sense here.
       */
      type = MVP_EXTRACT_FIELD(prrr, 2 * indx, 2);
      ASSERT(type == 2);

      innerCache = MVP_EXTRACT_FIELD(nmrr, 2 * indx, 2);
      outerCache = MVP_EXTRACT_FIELD(nmrr, 16 + 2 * indx, 2);
      outerShare = !MVP_BIT(prrr, 24 + indx);
      share = MVP_BIT(prrr, 18 + kernL2D->small.s);

      printk(KERN_INFO
             "DetermineMemAttr: type %x innerCache %x outerCache %x"
             " share %x outerShare %x\n",
             type,
             innerCache,
             outerCache,
             share,
             outerShare);

      if (share) {
         if (outerShare) {
            attribMAN->share = ARM_SHARE_ATTR_OUTER;
         } else {
            attribMAN->share = ARM_SHARE_ATTR_INNER;
         }
      } else {
         attribMAN->share = ARM_SHARE_ATTR_NONE;
      }

      attribMAN->innerCache = innerCache;
      attribMAN->outerCache = outerCache;
   } else {
      NOT_IMPLEMENTED_JIRA(1849);
   }

   free_pages(hkva, 0);
}
#endif

/**
 * @brief The ioctl file operation.
 *
 * The ioctl command is the main communication method between the
 * vmx and the mvpkm kernel module.
 *
 * @param filp which VM we're dealing with
 * @param cmd select which cmd function needs to be performed
 * @param arg argument for command
 * @return error code, 0 on success
 */
long
MvpkmUnlockedIoctl(struct file  *filp,
                   unsigned int  cmd,
                   unsigned long arg)
{
   MvpkmVM *vm = filp->private_data;
   int retval = 0;

   switch (cmd) {


      case MVPKM_DISABLE_FAULT: {
         if (!vm->stubPageMPN) {
            uint32 *ptr;

            vm->stubPageMPN =
               AllocZeroedFreePages(vm, 0, false, MEMREGION_MAINMEM, (HKVA*)&ptr);
            if (!vm->stubPageMPN) {
               break;
            }
            ptr[0] = MVPKM_STUBPAGE_BEG;
            ptr[PAGE_SIZE/sizeof(uint32) - 1] = MVPKM_STUBPAGE_END;
         }
         break;
      }

      /*
       * Allocate some pinned pages from kernel.
       * Returns -ENOMEM if no host pages available for allocation.
       */
      case MVPKM_LOCK_MPN: {
         struct MvpkmLockMPN buf;

         if (copy_from_user(&buf, (void *)arg, sizeof buf)) {
            return -EFAULT;
         }

         buf.mpn = AllocZeroedFreePages(vm,
                                        buf.order,
                                        false,
                                        buf.forRegion,
                                        NULL);
         if (buf.mpn == 0) {
            return -ENOMEM;
         }

         if (copy_to_user((void *)arg, &buf, sizeof buf)) {
            return -EFAULT;
         }
         break;
      }

      case MVPKM_UNLOCK_MPN: {
         struct MvpkmLockMPN buf;

         if (copy_from_user(&buf, (void *)arg, sizeof buf)) {
            return -EFAULT;
         }

         if (!LockedListDel(vm, buf.mpn)) {
            return -EINVAL;
         }
         break;
      }

      case MVPKM_MAP_WSPHKVA: {
         MvpkmMapHKVA mvpkmMapInfo;
         HkvaMapInfo mapInfo[WSP_PAGE_COUNT];

         if (copy_from_user(&mvpkmMapInfo, (void *)arg, sizeof mvpkmMapInfo)) {
            return -EFAULT;
         }

         if (copy_from_user(mapInfo, (void *)mvpkmMapInfo.mapInfo, sizeof mapInfo)) {
            return -EFAULT;
         }

         mvpkmMapInfo.hkva = MapWSPHKVA(vm, mapInfo);
         BUG_ON(mvpkmMapInfo.hkva == 0);

         if (mvpkmMapInfo.forRegion == MEMREGION_WSP) {
            vm->wsp = (WorldSwitchPage *) mvpkmMapInfo.hkva;
         }

         if (copy_to_user((void *)arg, &mvpkmMapInfo, sizeof mvpkmMapInfo)) {
            return -EFAULT;
         }
         break;
      }

      case MVPKM_RUN_MONITOR: {
         if (!vm->isMonitorInited) {
            vm->isMonitorInited = ((retval = SetupMonitor(vm)) == 0);
         }

         if (vm->isMonitorInited) {
            retval = RunMonitor(vm);
         }

         break;
      }

      case MVPKM_ABORT_MONITOR: {
         if (!vm->isMonitorInited) {
            return -EINVAL;
         }

         ASSERT(vm->wsp != NULL);

         Mvpkm_WakeGuest(vm, ACTION_ABORT);
         break;
      }

      case MVPKM_CPU_INFO: {
         struct MvpkmCpuInfo buf;
         uint32 mpidr;

#ifdef CONFIG_ARM_LPAE
         DetermineMemAttrLPAE(&buf.attribMAN);
         /**
          * We need to add support to the LPV monitor for LPAE page tables if we
          * want to use it on a LPAE host, due to the costs involved in
          * transitioning between LPAE and non-LPAE page tables without Hyp
          * assistance.
          *
          * @knownjira{MVP-2184}
          */
         buf.attribL2D.u = 0;
#else
         DetermineMemAttrNonLPAE(&buf.attribL2D, &buf.attribMAN);
#endif
         /*
          * Are MP extensions implemented? See B4-1618 ARM DDI 0406C-2c for
          * magic.
          */
         ARM_MRC_CP15(MPIDR, mpidr);

         buf.mpExt = mpidr & ARM_CP15_MPIDR_MP;

         if (copy_to_user((int *)arg, &buf, sizeof(struct MvpkmCpuInfo))) {
            retval = -EFAULT;
         }
         break;
      }

      default: {
         retval = -EINVAL;
         break;
      }
   }

   PRINTK(KERN_INFO "returning from IOCTL(%d) retval = %d %s\n",
          cmd, retval, signal_pending(current)?"(pending signal)":"" );

   return retval;
}



/*********************************************************************
 *
 * Locked page management
 *
 *********************************************************************/

/*
 * Pages locked by the kernel module are remembered so an unlockAll
 * operation can be performed when the vmm is closed. The locked page
 * identifiers are stored in a red-black tree to support O(log n)
 * removal and search (required for /dev/mem-like mmap).
 */

/**
 * @brief Descriptor of a locked page range
 */
typedef struct {
   struct {
      __u32 mpn       : 20; ///< MPN.
      __u32 order     : 6;  ///< Size/alignment exponent for page.
      __u32 forRegion : 6;  ///< Annotation to identify guest page allocation
   } page;
   struct rb_node rb;
} LockedPage;

static void FreeLockedPages(LockedPage *lp);

/**
 * @brief Search for an mpn inside a RB tree of LockedPages. The mpn
 *        will match a LockedPage as long as it is covered by the
 *        entry, i.e. in a non-zero order entry it doesn't have to be
 *        the base MPN.
 *
 *        This must be called with the relevant vm->lockedSem held.
 *
 * @param root RB tree root.
 * @param mpn MPN to search for.
 *
 * @return reference to LockedPage entry if found, otherwise NULL.
 */
static LockedPage *
LockedListSearch(struct rb_root *root, __u32 mpn)
{
   struct rb_node *n = root->rb_node;

   while (n) {
      LockedPage *lp = rb_entry(n, LockedPage, rb);

      if (lp->page.mpn == (mpn & (~0UL << lp->page.order))) {
         return lp;
      }

      if (mpn < lp->page.mpn) {
         n = n->rb_left;
      } else {
         n = n->rb_right;
      }
   }

   return NULL;
}

/**
 * @brief Delete an mpn from the list of locked pages.
 *
 * @param vm Mvpkm module control structure pointer
 * @param mpn MPN to be unlocked and freed for reuse
 * @return true if list contained MPN and it was deleted from list
 */

static _Bool
LockedListDel(MvpkmVM *vm, __u32 mpn)
{
   LockedPage *lp;

   down_write(&vm->lockedSem);

   lp = LockedListSearch(&vm->lockedRoot, mpn);

   /*
    * The MPN should be in the locked pages RB tree and it should be the
    * base of an entry, i.e. we can't fragment existing allocations for
    * a VM.
    */
   if (lp == NULL || lp->page.mpn != mpn) {
      up_write(&vm->lockedSem);
      return false;
   }

   FreeLockedPages(lp);

   if (lp->page.forRegion == MEMREGION_MAINMEM) {
      ATOMIC_SUBV(vm->usedPages, 1U << lp->page.order);
   }

   rb_erase(&lp->rb, &vm->lockedRoot);
   kfree(lp);

   up_write(&vm->lockedSem);

   return true;
}

/**
 * @brief Scan the list of locked pages to see if an MPN matches.
 *
 * @param vm Mvpkm module control structure pointer
 * @param mpn MPN to check
 *
 * @return true iff list contains MPN.
 */
static _Bool
LockedListLookup(MvpkmVM *vm, __u32 mpn)
{
   LockedPage *lp;

   down_read(&vm->lockedSem);

   lp = LockedListSearch(&vm->lockedRoot, mpn);

   up_read(&vm->lockedSem);

   return lp != NULL;
}

/**
 * @brief Add a new mpn to the locked pages RB tree.
 *
 * @param vm control structure pointer
 *
 * @param mpn mpn of page that was locked with get_user_pages or some sort of
 *            get that is undone by put_page.
 *            The mpn is assumed to be non-zero
 * @param order size/alignment exponent for page
 * @param forRegion Annotation for Page pool to identify guest page allocations
 *
 * @return false: couldn't allocate internal memory to record mpn in<br>
 *         true:  successful.
 */
static _Bool
LockedListAdd(MvpkmVM *vm,
              __u32 mpn,
              __u32 order,
              PhysMem_RegionType forRegion)
{
   struct rb_node *parent, **p;
   LockedPage *tp, *lp = kmalloc(sizeof *lp, GFP_KERNEL);

   if (!lp) {
      return false;
   }

   lp->page.mpn       = mpn;
   lp->page.order     = order;
   lp->page.forRegion = forRegion;

   down_write(&vm->lockedSem);

   if (forRegion == MEMREGION_MAINMEM) {
      ATOMIC_ADDV(vm->usedPages, 1U << order);
   }

   /*
    * Insert as a red leaf in the tree (see include/linux/rbtree.h).
    */
   p = &vm->lockedRoot.rb_node;
   parent = NULL;

   while (*p) {
      parent = *p;
      tp = rb_entry(parent, LockedPage, rb);

      /*
       * MPN should not already exist in the tree.
       */
      ASSERT(tp->page.mpn != (mpn & (~0UL << tp->page.order)));

      if (mpn < tp->page.mpn) {
         p = &(*p)->rb_left;
      } else {
         p = &(*p)->rb_right;
      }
   }

   rb_link_node(&lp->rb, parent, p);

   /*
    * Restructure tree if necessary (see include/linux/rbtree.h).
    */
   rb_insert_color(&lp->rb, &vm->lockedRoot);

   up_write(&vm->lockedSem);

   return true;
}

/**
 * @brief Traverse RB locked tree, freeing every entry.
 *
 *        This must be called with the relevant vm->lockedSem held.
 *
 * @param node reference to RB node at root of subtree.
 */
static void
LockedListNuke(struct rb_node *node)
{
   while (node) {
      if (node->rb_left) {
         node = node->rb_left;
      } else if (node->rb_right) {
         node = node->rb_right;
      } else {
         /*
          * We found a leaf, free it and go back to parent.
          */
         LockedPage *lp = rb_entry(node, LockedPage, rb);

         if ((node = rb_parent(node))) {
            if (node->rb_left) {
               node->rb_left = NULL;
            } else {
               node->rb_right = NULL;
            }
         }

         FreeLockedPages(lp);
         kfree(lp);
      }
   }
}

/**
 * @brief Unlock all pages at vm close time.
 *
 * @param vm control structure pointer
 */
static void
LockedListUnlockAll(MvpkmVM *vm)
{

   down_write(&vm->lockedSem);

   LockedListNuke(vm->lockedRoot.rb_node);

   ATOMIC_SETV(vm->usedPages, 0);

   up_write(&vm->lockedSem);
}


/**
 * @brief Allocate zeroed free pages
 *
 * @param[in] vm which VM the pages are for so they will be freed when the vm
 *               closes
 * @param[in] order log2(number of contiguous pages to allocate)
 * @param[in] highmem is it OK to allocate this page in ZONE_HIGHMEM? This
 *                    option should only be specified for pages the host kernel
 *                    will not need to address directly.
 * @param[out] hkvaRet where to return host kernel virtual address of the
 *                     allocated pages, if non-NULL, and ONLY IF !highmem.
 * @param forRegion Annotation for Page pool to identify guest page allocations
 * @return 0: no host memory available<br>
 *      else: starting MPN<br>
 *            *hkvaRet = filled in
 */
static MPN
AllocZeroedFreePages(MvpkmVM *vm,
                     uint32 order,
                     _Bool highmem,
                     PhysMem_RegionType forRegion,
                     HKVA *hkvaRet)
{
   MPN mpn;
   struct page *page;

   if (order > PAGE_ALLOC_COSTLY_ORDER) {
      printk(KERN_WARNING "Order %d allocation for region %d exceeds the safe "
             "maximum order %d\n",
             order,
             forRegion,
             PAGE_ALLOC_COSTLY_ORDER);
   }

   /*
    * Get some pages for the requested range.  They will be physically
    * contiguous and have the requested alignment.  They will also
    * have a kernel virtual mapping if !highmem.
    *
    * We allocate out of ZONE_MOVABLE even though we can't just pick up our
    * bags. We do this to support platforms that explicitly configure
    * ZONE_MOVABLE, such as the Qualcomm MSM8960, to enable deep power down of
    * memory banks. When the kernel attempts to take a memory bank offline, it
    * will try and place the pages on the isolate LRU - only pages already on an
    * LRU, such as anon/file, can get there, so it will not be able to
    * migrate/move our pages (and hence the bank will not be offlined). The
    * other alternative is to live withing ZONE_NORMAL, and only have available
    * a small fraction of system memory. Long term we plan on hooking the
    * offlining callback in mvpkm and perform our own migration with the
    * cooperation of the monitor, but we don't have dev board to support this
    * today.
    *
    * @knownjira{MVP-3477}
    */
   page = alloc_pages(GFP_USER | __GFP_COMP | __GFP_ZERO |
                      (highmem ? __GFP_HIGHMEM | __GFP_MOVABLE : 0),
                      order);

   if (page == NULL) {
      return 0;
   }

   /*
    * Return the corresponding page number.
    */
   mpn = page_to_pfn(page);
   ASSERT(mpn != 0);

   /*
    * Remember to unlock the pages when the FD is closed.
    */
   if (!LockedListAdd(vm, mpn, order, forRegion)) {
      __free_pages(page, order);
      return 0;
   }

   if (hkvaRet) {
      *hkvaRet = highmem ? 0 : __phys_to_virt(page_to_phys(page));
   }

   return mpn;
}

/**
 * @brief Map already-pinned WSP memory in host kernel virtual address(HKVA)
 * space. Assumes 2 world switch pages on an 8k boundary.
 *
 * @param[in] vm which VM the HKVA Area is to be mapped for
 * @param[in] mapInfo array of MPNs and execute permission flags to be used in
                      inserting a new contiguous map in HKVA space
 * @return 0: HKVA space could not be mapped
           else: HKVA where mapping was inserted
 */
static HKVA
MapWSPHKVA(MvpkmVM *vm, HkvaMapInfo *mapInfo)
{
   unsigned int i;
   struct page **pages = NULL;
   struct page **pagesPtr;
   pgprot_t prot;
   int retval;
   int allocateCount = WSP_PAGE_COUNT + 1; // Reserve one page for alignment
   int pageIndex = 0;
   HKVA dummyPage = (HKVA)NULL;
   HKVA start;
   HKVA startSegment;
   HKVA endSegment;

   /*
    * Add one page for alignment purposes in case __get_vm_area returns an
    * unaligned address.
    */
   ASSERT(allocateCount == 3);
   ASSERT_ON_COMPILE(WSP_PAGE_COUNT == 2);

   /*
    * NOT_IMPLEMENTED if MapHKVA is called more than once.
    */
   BUG_ON(vm->wspHkvaArea);

   /*
    * Reserve virtual address space.
    */
   vm->wspHkvaArea = __get_vm_area((allocateCount * PAGE_SIZE), VM_ALLOC, MODULES_VADDR, MODULES_END);
   if (!vm->wspHkvaArea) {
      return 0;
   }

   pages = kmalloc(allocateCount * sizeof(struct page *), GFP_TEMPORARY);
   if (!pages) {
      goto err;
   }
   pagesPtr = pages;

   /*
    * Use a dummy page to boundary align the section, if needed.
    */
   dummyPage = __get_free_pages(GFP_KERNEL, 0);
   if (!dummyPage) {
      goto err;
   }
   vm->wspHKVADummyPage = dummyPage;

   /*
    * Back every entry with the dummy page.
    */
   for (i = 0; i < allocateCount; i++) {
      pages[i] = virt_to_page(dummyPage);
   }

   /*
    * World switch pages must not span a 1MB boundary in order to maintain only
    * a single L2 page table.
    */
   start = (HKVA)vm->wspHkvaArea->addr;
   startSegment = start & ~(ARM_L1D_SECTION_SIZE - 1);
   endSegment   = (start + PAGE_SIZE) & ~(ARM_L1D_SECTION_SIZE - 1);
   /*
    * Insert dummy page at pageIndex, if needed.
    */
   pageIndex = (startSegment != endSegment);

   /*
    * Back the rest with the actual world switch pages
    */
   for (i = pageIndex; i < pageIndex + WSP_PAGE_COUNT; i++) {
      pages[i] = pfn_to_page(mapInfo[i - pageIndex].mpn);
   }

   /*
    * Given the lack of functionality in the kernel for being able to mark
    * mappings for a given vm area with different sets of protection bits,
    * we simply mark the entire vm area as PAGE_KERNEL_EXEC for now
    * (i.e., union of all the protection bits). Given that the kernel
    * itself does something similar while loading modules, this should be a
    * reasonable workaround for now. In the future, we should set the
    * protection bits to strictly adhere to what has been requested in the
    * mapInfo parameter.
    */
   prot = PAGE_KERNEL_EXEC;

   retval = map_vm_area(vm->wspHkvaArea, prot, &pagesPtr);
   if (retval < 0) {
      goto err;
   }

   kfree(pages);

   return (HKVA)(vm->wspHkvaArea->addr) + pageIndex * PAGE_SIZE;

err:
   if (dummyPage) {
      free_pages(dummyPage, 0);
      vm->wspHKVADummyPage = (HKVA)NULL;
   }

   if (pages) {
      kfree(pages);
   }

   free_vm_area(vm->wspHkvaArea);
   vm->wspHkvaArea = (HKVA)NULL;

   return 0;
}

static void
UnmapWSPHKVA(MvpkmVM *vm)
{
   if (vm->wspHkvaArea) {
      free_vm_area(vm->wspHkvaArea);
   }

   if (vm->wspHKVADummyPage) {
      free_pages(vm->wspHKVADummyPage, 0);
      vm->wspHKVADummyPage = (HKVA)NULL;
   }
}

/**
 * @brief Clean and release locked pages
 *
 * @param lp Reference to the locked pages
 */
static void
FreeLockedPages(LockedPage *lp)
{
   struct page *page;
   int count;

   page = pfn_to_page(lp->page.mpn);
   count = page_count(page);

   if (count == 0) {
      printk(KERN_ERR "%s: found locked page with 0 reference (mpn %05x)\n",
             __func__, lp->page.mpn);
      return;
   }

   if (count == 1) {
      int i;

      /*
       * There is no other user for this page, clean it.
       *
       * We don't bother checking if the page was highmem or not, clear_highmem
       * works for both.
       * We clear the content of the page, and rely on the fact that the previous
       * worldswitch has cleaned the potential VIVT I-CACHE.
       */
      for (i = 0; i < (1 << lp->page.order); i++) {
         clear_highpage(page + i);
      }
   } else if (lp->page.forRegion != MEMREGION_MAINMEM) {
      printk(KERN_WARNING "%s: mpn 0x%05x for region %d is still in use\n",
             __func__, lp->page.mpn, lp->page.forRegion);
   }

   __free_pages(page, lp->page.order);
}

/*********************************************************************
 *
 * Communicate with monitor
 *
 *********************************************************************/

/**
 * @brief Register a new monitor page.
 *
 * @param vm  which virtual machine we're running
 * @return 0: successful<br>
 *      else: -errno
 */
static int
SetupMonitor(MvpkmVM *vm)
{
   int retval;
   WorldSwitchPage *wsp = vm->wsp;

   if (!wsp ||
       wsp->wspHKVA != (HKVA)wsp) {
      return -EINVAL;
   }

   if ((retval = Mksck_WspInitialize(vm))) {
      return retval;
   }

   vm->kobj.kset = mvpkmKSet;
   retval = kobject_init_and_add(&vm->kobj, &mvpkmKType, NULL, "%d", wsp->guestId);
   if (retval) {
      goto error;
   }

   /*
    * Get a reference to this module such that it cannot be unloaded until
    * our kobject's release function completes.
    */

   __module_get(THIS_MODULE);
   vm->haveKObj = true;

   /*
    * Caution: From here on, if we fail, we must not call kobject_put()
    * on vm->kobj since that may / will deallocate 'vm'. Unregistering VM
    * ksets on failures, is fine and should be done for proper ref counting.
    */

   vm->devicesKSet = kset_create_and_add("devices", NULL, &vm->kobj);
   if (!vm->devicesKSet) {
      retval = -ENOMEM;
      goto error;
   }

   vm->miscKSet = kset_create_and_add("misc", NULL, &vm->kobj);
   if (!vm->miscKSet) {
      kset_unregister(vm->devicesKSet);
      vm->devicesKSet = NULL;
      retval = -ENOMEM;
      goto error;
   }

   down_write(&vm->wspSem);

   /*
    * The VE monitor needs to issue a SMC to bootstrap Hyp mode.
    */
   if (wsp->monType == MONITOR_TYPE_VE) {
      /*
       * Here we assemble the monitor's HMAIR0 based on wsp->memAttr. We map
       * from the inner/outer normal page cacheability attributes obtained
       * from DetermineCacheabilityAttribs to the format required in 4.2.8
       * ARM PRD03-GENC-008469 13.0 (see this document for the magic numbers).
       *
       * Where a choice is available, we opt for read and/or write allocation.
       */
      static const uint32 normalCacheAttr2MAIR[4] = { 0x4, 0xf, 0xa, 0xe };

      uint32 hmair0 =
         ((normalCacheAttr2MAIR[wsp->memAttr.innerCache] |
           (normalCacheAttr2MAIR[wsp->memAttr.outerCache] << 4))
          << 8 * MVA_MEMORY) |
         (0x4 << 8 * MVA_DEVICE);

      /*
       * See B4.1.74 ARM DDI 0406C-2c for the HTCR magic.
       */
      uint32 htcr =
         0x80000000 |
         (wsp->memAttr.innerCache << 8) |
         (wsp->memAttr.outerCache << 10) |
         (wsp->memAttr.share << 12);

      /**
       * @knownjira{MVP-377}
       * Set HSCTLR to enable MMU and caches. We should really run the
       * monitor WXN, in non-MVP_DEVEL builds. See
       * 13.18 ARM PRD03-GENC-008353 11.0 for the magic.
       */
      static const uint32 hsctlr = 0x30c5187d;

      register uint32 r0 asm("r0") = wsp->monVA.excVec;
      register uint32 r1 asm("r1") = wsp->regSave.ve.mHTTBR;
      register uint32 r2 asm("r2") = htcr;
      register uint32 r3 asm("r3") = hmair0;
      register uint32 r4 asm("r4") = hsctlr;

      asm volatile (
         "smc 0"
         :
         : "r" (r0), "r" (r1), "r" (r2), "r" (r3), "r" (r4)
         : "memory"
         );
   }

   /*
    * Initialize guest wait-for-interrupt waitqueue.
    */
   init_waitqueue_head(&vm->wfiWaitQ);

   MonitorTimer_Setup(vm);

#ifdef CONFIG_HAS_WAKELOCK
   wake_lock_init(&vm->wakeLock, WAKE_LOCK_SUSPEND, "mvpkm");
#endif

   wsp->mvpkmVersion = MVP_VERSION_CODE;
   up_write(&vm->wspSem);
   /*
    * Ensure coherence of monitor loading and page tables.
    */
   flush_cache_all();
   return 0;

error:
   Mksck_WspRelease(wsp);
   vm->wsp = NULL;
   return retval;
}

/**
 * @brief dummy function to drop the info parameter
 * @param info ignored
 */
static
void FlushAllCpuCaches(void *info)
{
   flush_cache_all();
}

/**
 * @brief return to where monitor called worldswitch
 *
 * @param vm  which virtual machine we're running
 * @return 0: successful, just call back when ready<br>
 *         1: successful, process code in WSP_Params(wsp)->callno<br>
 *      else: -errno
 */
static int
RunMonitor(MvpkmVM *vm)
{
   int ii;
   unsigned long flags;
   WorldSwitchPage *wsp = vm->wsp;
   int retval = 0;

   ASSERT(wsp);

#ifdef CONFIG_HAS_WAKELOCK
   wake_lock(&vm->wakeLock);
#endif

   /*
    * Set VCPUThread affinity
    */
   if (cpumask_intersects(to_cpumask(vcpuAffinity), cpu_active_mask)) {
      set_cpus_allowed_ptr(current, to_cpumask(vcpuAffinity));
   }

   /*
    * Record the the current task structure, so an ABORT will know,
    * who to wake.
    */
   down_write(&vm->monThreadTaskSem);
   vm->monThreadTask = get_current();
   up_write(&vm->monThreadTaskSem);

   /*
    * Keep going as long as the monitor is in critical section or
    * there are no pending signals such as SIGINT or SIGKILL.  Block
    * interrupts before checking so any IPI sent will remain pending
    * if our check just misses detecting the signal.
    */
   local_irq_save(flags);
   while (wsp->critSecCount > 0 ||
          (!signal_pending(current) &&
           !(ATOMIC_GETO(wsp->hostActions) & ACTION_ABORT))) {
      /*
       * ARMv7 Performance counters are per CPU core and might be disabled over
       * CPU core sleep if there is nothing else in the system to re-enable
       * them, so now that we have been allocated a CPU core to run the guest,
       * enable them and in particular the TSC (CCNT) which is used for monitor
       * timing between world switches.
       */
      {
         uint32 pmnc;
         uint32 pmcnt;

         /* make sure that the Performance Counters are enabled */
         ARM_MRC_CP15(PERF_MON_CONTROL_REGISTER, pmnc);
         if ((pmnc & (ARM_PMNC_E | ARM_PMNC_D)) != (ARM_PMNC_E)) {
            pmnc |=  ARM_PMNC_E;  // Enable TSC
            pmnc &= ~ARM_PMNC_D;  // Disable cycle count divider
            ARM_MCR_CP15(PERF_MON_CONTROL_REGISTER, pmnc);
         }

         /* make sure that the CCNT is enabled */
         ARM_MRC_CP15(PERF_MON_COUNT_SET, pmcnt);
         if ((pmcnt & ARM_PMCNT_C) != ARM_PMCNT_C) {
            pmcnt |= ARM_PMCNT_C;
            ARM_MCR_CP15(PERF_MON_COUNT_SET, pmcnt);
         }
      }

      /*
       * Update TSC to RATE64 ratio
       */
      {
         struct TscToRate64Cb *ttr = &__get_cpu_var(tscToRate64);
         wsp->tscToRate64Mult = ttr->mult;
         wsp->tscToRate64Shift = ttr->shift;
      }

      /*
       * Save the time of day for the monitor's timer facility.  The timing
       * facility in the vmm needs to compute current time in the host linux's
       * time representation.  It uses the formula:
       *    now = wsp->switchedAt64 + (uint32)(TSC_READ() - wsp->lowerTSC)
       *
       * Read the timestamp counter *immediately after* ktime_get() as that
       * will give the most consistent offset between reading the hardware
       * clock register in ktime_get() and reading the hardware timestamp
       * counter with TSC_READ().
       */
      ASSERT_ON_COMPILE(MVP_TIMER_RATE64 == NSEC_PER_SEC);
      {
         ktime_t now = ktime_get();
         TSC_READ(wsp->switchedAtTSC);
         wsp->switchedAt64 = ktime_to_ns(now);
      }

      /*
       * Save host FPU contents and load monitor contents.
       */
      SWITCH_VFP_TO_MONITOR;

      /*
       * Call into the monitor to run guest instructions until it wants us to
       * do something for it.  Note that any hardware interrupt request will
       * cause it to volunteer.
       */
      switch (wsp->monType) {
         case MONITOR_TYPE_LPV: {
            uint32 hostVBAR;

            ARM_MRC_CP15(VECTOR_BASE, hostVBAR);
            (*wsp->switchToMonitor)(&wsp->regSave);
            ARM_MCR_CP15(VECTOR_BASE, hostVBAR);
            break;
         }
         case MONITOR_TYPE_VE: {
            register uint32 r1 asm("r1") = wsp->regSave.ve.mHTTBR;

            asm volatile (
               ".word " MVP_STRINGIFY(ARM_INSTR_HVC_A1_ENC(0))
               : "=r" (r1) : "r" (r1) : "r0", "r2", "memory"
            );
            break;
         }
         default: FATAL();
      }

      /*
       * Save monitor FPU contents and load host contents.
       */
      SWITCH_VFP_TO_HOST;

      /*
       * Re-enable local interrupts now that we are back in the host world
       */
      local_irq_restore(flags);


      /*
       * Maybe the monitor wrote some messages to monitor->host sockets.
       * This will wake the corresponding host threads to receive them.
       */
      /**
       * @todo This lousy loop is in the critical path. It should be changed
       * to some faster algorithm to wake blocked host sockets.
       */
      for (ii = 0; ii < MKSCK_MAX_SHARES; ii++) {
         if (wsp->isPageMapped[ii]) {
            Mksck_WakeBlockedSockets(MksckPage_GetFromIdx(ii));
         }
      }

      switch (WSP_Params(wsp)->callno) {
         case WSCALL_ACQUIRE_PAGE: {
            uint32 i;

            for (i = 0; i < WSP_Params(wsp)->pages.pages; ++i) {
               MPN mpn = AllocZeroedFreePages(vm,
                                              WSP_Params(wsp)->pages.order,
                                              true,
                                              WSP_Params(wsp)->pages.forRegion,
                                              NULL);
               if (mpn == 0) {
                  printk(KERN_WARNING "WSCALL_ACQUIRE_PAGE: no order %u pages available\n",
                        WSP_Params(wsp)->pages.order);
                  WSP_Params(wsp)->pages.pages = i;
                  break;
               }

               WSP_Params(wsp)->pages.mpns[i] = mpn;
            }

            break;
         }
         case WSCALL_RELEASE_PAGE: {
            uint32 i;

            for (i = 0; i < WSP_Params(wsp)->pages.pages; ++i) {
               if (!LockedListDel(vm, WSP_Params(wsp)->pages.mpns[i])) {
                  WSP_Params(wsp)->pages.pages = i;
                  break;
               }
            }

            break;
         }
         case WSCALL_MUTEXLOCK: {
            retval = Mutex_Lock((void *)WSP_Params(wsp)->mutex.mtxHKVA,
                                WSP_Params(wsp)->mutex.mode);

            if (retval < 0) {
               WSP_Params(wsp)->mutex.ok = false;
               goto monitorExit;
            }

            /*
             * The locking succeeded. From this point on the monitor
             * is in critical section. Even if an interrupt comes
             * right here, it must return to the monitor to unlock the
             * mutex.
             */
            wsp->critSecCount++;
            WSP_Params(wsp)->mutex.ok = true;
            break;
         }
         case WSCALL_MUTEXUNLOCK: {
            Mutex_Unlock((void *)WSP_Params(wsp)->mutex.mtxHKVA,
                         WSP_Params(wsp)->mutex.mode);
            break;
         }
         case WSCALL_MUTEXUNLSLEEP: {
            /*
             * The vcpu has just come back from the monitor. During
             * the transition interrupts were disabled. Above,
             * however, interrupts were enabled again and it is
             * possible that a context switch happened into a thread
             * (serve_vmx) that instructed the vcpu thread to
             * abort. After returning to this thread the vcpu may
             * enter a sleep below never to return from it. To avoid
             * this deadlock we need to test the abort flag in
             * Mutex_UnlSleepTest.
             */
            retval =
               Mutex_UnlSleepTest((void *)WSP_Params(wsp)->mutex.mtxHKVA,
                                  WSP_Params(wsp)->mutex.mode,
                                  WSP_Params(wsp)->mutex.cvi,
                                  &wsp->hostActions,
                                  ACTION_ABORT);
            if (retval < 0) {
               goto monitorExit;
            }
            break;
         }
         case WSCALL_MUTEXUNLWAKE: {
            Mutex_UnlWake((void *)WSP_Params(wsp)->mutex.mtxHKVA,
                          WSP_Params(wsp)->mutex.mode,
                          WSP_Params(wsp)->mutex.cvi,
                          WSP_Params(wsp)->mutex.all);
            break;
         }

         /*
          * The monitor wants us to block (allowing other host threads to run)
          * until an async message is waiting for the monitor to process.
          *
          * If MvpkmWaitForInt() returns an error, it should only be if there
          * is another signal pending (such as SIGINT).  So we pretend it
          * completed normally, as the monitor is ready to be called again (it
          * will see no messages to process and wait again), and return to user
          * mode so the signals can be processed.
          */
         case WSCALL_WAIT: {
#ifdef CONFIG_HAS_WAKELOCK
            if (WSP_Params(wsp)->wait.suspendMode) {
               /* guest has ok'ed suspend mode, so release SUSPEND wakelock */
               wake_unlock(&vm->wakeLock);
               retval = MvpkmWaitForInt(vm, true);
               wake_lock(&vm->wakeLock);
               WSP_Params(wsp)->wait.suspendMode = 0;
            } else {
               /* guest has asked for WFI not suspend so keep holding SUSPEND
                * wakelock */
               retval = MvpkmWaitForInt(vm, false);
            }
#else
            retval = MvpkmWaitForInt(vm, WSP_Params(wsp)->wait.suspendMode);
#endif
            if (retval < 0) {
               goto monitorExit;
            }
            break;
         }

         /*
          * The only reason the monitor returned was because there was a
          * pending hardware interrupt.  The host serviced and cleared that
          * interrupt when we enabled interrupts above.  Now we call the
          * scheduler in case that interrupt woke another thread, we want to
          * allow that thread to run before returning to do more guest code.
          */
         case WSCALL_IRQ: {
            break;
         }

         case WSCALL_GET_PAGE_FROM_VMID: {
            MksckPage *mksckPage;
            mksckPage = MksckPage_GetFromVmIdIncRefc(WSP_Params(wsp)->pageMgmnt.vmId);

            if (mksckPage) {
               int ii;

               WSP_Params(wsp)->pageMgmnt.found = true;
               for (ii = 0; ii < MKSCKPAGE_TOTAL; ii++) {
                  WSP_Params(wsp)->pageMgmnt.mpn[ii] =
                     vmalloc_to_pfn( (void*)(((HKVA)mksckPage) + ii*PAGE_SIZE) );
               }

               ASSERT(!wsp->isPageMapped[MKSCK_VMID2IDX(mksckPage->vmId)]);
               wsp->isPageMapped[MKSCK_VMID2IDX(mksckPage->vmId)] = true;
            } else {
               WSP_Params(wsp)->pageMgmnt.found = false;
            }
            break;
         }

         case WSCALL_REMOVE_PAGE_FROM_VMID: {
            MksckPage *mksckPage;
            mksckPage = MksckPage_GetFromVmId(WSP_Params(wsp)->pageMgmnt.vmId);
            ASSERT(wsp->isPageMapped[MKSCK_VMID2IDX(mksckPage->vmId)]);
            wsp->isPageMapped[MKSCK_VMID2IDX(mksckPage->vmId)] = false;
            MksckPage_DecRefc(mksckPage);
            break;
         }

         /*
          * Read current wallclock time.
          */
         case WSCALL_READTOD: {
            struct timeval nowTV;
            do_gettimeofday(&nowTV);
            WSP_Params(wsp)->tod.now = nowTV.tv_sec;
            WSP_Params(wsp)->tod.nowusec = nowTV.tv_usec;
            break;
         }

         case WSCALL_LOG: {
            int len = strlen(WSP_Params(wsp)->log.messg);
            printk(KERN_INFO
                   "VMM: %s%s",
                   WSP_Params(wsp)->log.messg,
                   (WSP_Params(wsp)->log.messg[len-1] == '\n') ? "" : "\n");
            break;
         }

         case WSCALL_ABORT: {
            retval = WSP_Params(wsp)->abort.status;
            goto monitorExit;
         }

         case WSCALL_QP_GUEST_ATTACH: {
            int32 rc;
            QPInitArgs args;
            uint32 base;
            uint32 nrPages;

            args.id       = WSP_Params(wsp)->qp.id;
            args.capacity = WSP_Params(wsp)->qp.capacity;
            args.type     = WSP_Params(wsp)->qp.type;
            base          = WSP_Params(wsp)->qp.base;
            nrPages       = WSP_Params(wsp)->qp.nrPages;

            rc = QP_GuestAttachRequest(vm, &args, base, nrPages);

            WSP_Params(wsp)->qp.rc           = rc;
            WSP_Params(wsp)->qp.id           = args.id;
            break;
         }

         case WSCALL_QP_NOTIFY: {
            QPInitArgs args;

            args.id       = WSP_Params(wsp)->qp.id;
            args.capacity = WSP_Params(wsp)->qp.capacity;
            args.type     = WSP_Params(wsp)->qp.type;

            WSP_Params(wsp)->qp.rc = QP_NotifyListener(&args);
            break;
         }

         case WSCALL_MONITOR_TIMER: {
            MonitorTimer_Request(&vm->monTimer, WSP_Params(wsp)->timer.when64);
            break;
         }

         case WSCALL_COMM_SIGNAL: {
            Mvpkm_CommEvSignal(&WSP_Params(wsp)->commEvent.transpID,
                                WSP_Params(wsp)->commEvent.event);
            break;
         }

         case WSCALL_FLUSH_ALL_DCACHES: {
            /*
             * Broadcast Flush DCache request to all cores.
             * Block while waiting for all of them to get done.
             */
            on_each_cpu(FlushAllCpuCaches, NULL, 1);
            break;
         }
         default: {
            retval = -EPIPE;
            goto monitorExit;
         }
      }

      /*
       * The params.callno callback was handled in kernel mode and completed
       * successfully.  Repeat for another call without returning to user mode,
       * unless there are signals pending.
       *
       * But first, call the Linux scheduler to switch threads if there is
       * some other thread Linux wants to run now.
       */
      if (need_resched()) {
         schedule();
      }

      /*
       * Check if cpus allowed mask has to be updated.
       * Updating it must be done outside of an atomic context.
       */
      if (cpumask_intersects(to_cpumask(vcpuAffinity), cpu_active_mask) &&
          !cpumask_equal(to_cpumask(vcpuAffinity), &current->cpus_allowed)) {
         set_cpus_allowed_ptr(current, to_cpumask(vcpuAffinity));
      }

      local_irq_save(flags);
   }

   /*
    * There are signals pending so don't try to do any more monitor/guest
    * stuff.  But since we were at the point of just about to run the monitor,
    * return success status as user mode can simply call us back to run the
    * monitor again.
    */
   local_irq_restore(flags);

monitorExit:
   ASSERT(wsp->critSecCount == 0);

   if (ATOMIC_GETO(wsp->hostActions) & ACTION_ABORT) {
      PRINTK(KERN_INFO "Monitor has ABORT flag set.\n");
      retval = ExitStatusHostRequest;
   }

#ifdef CONFIG_HAS_WAKELOCK
   wake_unlock(&vm->wakeLock);
#endif

   down_write(&vm->monThreadTaskSem);
   vm->monThreadTask = NULL;
   up_write(&vm->monThreadTaskSem);

   return retval;
}

/**
 * @brief Guest is waiting for interrupts, sleep if necessary
 *
 * @param vm  which virtual machine we're running
 * @param suspend is the guest entering suspend or just WFI?
 * @return 0: woken up, hostActions should have pending events
 *        -ERESTARTSYS: broke out because other signals are pending
 *
 * This function is called in the VCPU context after the world switch to wait
 * for an incoming message.  If any message gets queued to this VCPU, the
 * sender will wake us up.
 */
int
MvpkmWaitForInt(MvpkmVM *vm, _Bool suspend)
{
   WorldSwitchPage *wsp = vm->wsp;
   wait_queue_head_t *q = &vm->wfiWaitQ;

   if (suspend) {
      return wait_event_interruptible(*q, ATOMIC_GETO(wsp->hostActions) != 0);
   } else {
      int ret;
      ret = wait_event_interruptible_timeout(*q, ATOMIC_GETO(wsp->hostActions) != 0, 10*HZ);
      if (ret == 0) {
         printk("MvpkmWaitForInt: guest stuck for 10s in WFI! (hostActions %08x)\n",
               ATOMIC_GETO(wsp->hostActions));
      }
      return ret > 0 ? 0 : ret;
   }
}


/**
 * @brief Force the guest to evaluate its hostActions flag field
 *
 * @param vm which guest needs waking
 * @param why why should be guest be woken up?
 *
 * This function updates the hostAction flag field as and wakes up the guest as
 * required so that it can evaluate it.  The guest could be executing guest
 * code in an SMP system, in that case send an IPI; or it could be sleeping, in
 * the case wake it up.
 */
void
Mvpkm_WakeGuest(MvpkmVM *vm, int why)
{
   ASSERT(why != 0);

   /* set the host action */
   if (ATOMIC_ORO(vm->wsp->hostActions, why) & why) {
      /* guest has already been woken up so no need to do it again */
      return;
   }

   /*
    * VCPU is certainly in 'wait for interrupt' wait. Wake it up !
    */
#ifdef CONFIG_HAS_WAKELOCK
   /*
    * To prevent the system to go in suspend mode before the monitor had a
    * chance on being scheduled, we will hold the VM wakelock from now.
    * As the wakelocks are not managed as reference counts, this is not an
    * an issue to take a wake_lock twice in a row.
    */
   wake_lock(&vm->wakeLock);
#endif

   /*
    * On a UP system, we ensure the monitor thread isn't blocked.
    *
    * On an MP system the other CPU might be running the guest. This
    * is noop on UP.
    *
    * When the guest is running, it is an invariant that monThreadTaskSem is not
    * held as a write lock, so we should not fail to acquire the lock.
    * Mvpkm_WakeGuest may be called from an atomic context, so we can't sleep
    * here.
    */
   if (down_read_trylock(&vm->monThreadTaskSem)) {
      if (vm->monThreadTask) {
         wake_up_process(vm->monThreadTask);
         kick_process(vm->monThreadTask);
      }
      up_read(&vm->monThreadTaskSem);
   } else {
      printk("Unexpected failure to acquire monThreadTaskSem!\n");
   }
}
