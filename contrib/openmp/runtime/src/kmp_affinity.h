/*
 * kmp_affinity.h -- header for affinity management
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_AFFINITY_H
#define KMP_AFFINITY_H

#include "kmp.h"
#include "kmp_os.h"

#if KMP_AFFINITY_SUPPORTED
#if KMP_USE_HWLOC
class KMPHwlocAffinity : public KMPAffinity {
public:
  class Mask : public KMPAffinity::Mask {
    hwloc_cpuset_t mask;

  public:
    Mask() {
      mask = hwloc_bitmap_alloc();
      this->zero();
    }
    ~Mask() { hwloc_bitmap_free(mask); }
    void set(int i) override { hwloc_bitmap_set(mask, i); }
    bool is_set(int i) const override { return hwloc_bitmap_isset(mask, i); }
    void clear(int i) override { hwloc_bitmap_clr(mask, i); }
    void zero() override { hwloc_bitmap_zero(mask); }
    void copy(const KMPAffinity::Mask *src) override {
      const Mask *convert = static_cast<const Mask *>(src);
      hwloc_bitmap_copy(mask, convert->mask);
    }
    void bitwise_and(const KMPAffinity::Mask *rhs) override {
      const Mask *convert = static_cast<const Mask *>(rhs);
      hwloc_bitmap_and(mask, mask, convert->mask);
    }
    void bitwise_or(const KMPAffinity::Mask *rhs) override {
      const Mask *convert = static_cast<const Mask *>(rhs);
      hwloc_bitmap_or(mask, mask, convert->mask);
    }
    void bitwise_not() override { hwloc_bitmap_not(mask, mask); }
    int begin() const override { return hwloc_bitmap_first(mask); }
    int end() const override { return -1; }
    int next(int previous) const override {
      return hwloc_bitmap_next(mask, previous);
    }
    int get_system_affinity(bool abort_on_error) override {
      KMP_ASSERT2(KMP_AFFINITY_CAPABLE(),
                  "Illegal get affinity operation when not capable");
      int retval =
          hwloc_get_cpubind(__kmp_hwloc_topology, mask, HWLOC_CPUBIND_THREAD);
      if (retval >= 0) {
        return 0;
      }
      int error = errno;
      if (abort_on_error) {
        __kmp_fatal(KMP_MSG(FatalSysError), KMP_ERR(error), __kmp_msg_null);
      }
      return error;
    }
    int set_system_affinity(bool abort_on_error) const override {
      KMP_ASSERT2(KMP_AFFINITY_CAPABLE(),
                  "Illegal get affinity operation when not capable");
      int retval =
          hwloc_set_cpubind(__kmp_hwloc_topology, mask, HWLOC_CPUBIND_THREAD);
      if (retval >= 0) {
        return 0;
      }
      int error = errno;
      if (abort_on_error) {
        __kmp_fatal(KMP_MSG(FatalSysError), KMP_ERR(error), __kmp_msg_null);
      }
      return error;
    }
    int get_proc_group() const override {
      int group = -1;
#if KMP_OS_WINDOWS
      if (__kmp_num_proc_groups == 1) {
        return 1;
      }
      for (int i = 0; i < __kmp_num_proc_groups; i++) {
        // On windows, the long type is always 32 bits
        unsigned long first_32_bits = hwloc_bitmap_to_ith_ulong(mask, i * 2);
        unsigned long second_32_bits =
            hwloc_bitmap_to_ith_ulong(mask, i * 2 + 1);
        if (first_32_bits == 0 && second_32_bits == 0) {
          continue;
        }
        if (group >= 0) {
          return -1;
        }
        group = i;
      }
#endif /* KMP_OS_WINDOWS */
      return group;
    }
  };
  void determine_capable(const char *var) override {
    const hwloc_topology_support *topology_support;
    if (__kmp_hwloc_topology == NULL) {
      if (hwloc_topology_init(&__kmp_hwloc_topology) < 0) {
        __kmp_hwloc_error = TRUE;
        if (__kmp_affinity_verbose)
          KMP_WARNING(AffHwlocErrorOccurred, var, "hwloc_topology_init()");
      }
      if (hwloc_topology_load(__kmp_hwloc_topology) < 0) {
        __kmp_hwloc_error = TRUE;
        if (__kmp_affinity_verbose)
          KMP_WARNING(AffHwlocErrorOccurred, var, "hwloc_topology_load()");
      }
    }
    topology_support = hwloc_topology_get_support(__kmp_hwloc_topology);
    // Is the system capable of setting/getting this thread's affinity?
    // Also, is topology discovery possible? (pu indicates ability to discover
    // processing units). And finally, were there no errors when calling any
    // hwloc_* API functions?
    if (topology_support && topology_support->cpubind->set_thisthread_cpubind &&
        topology_support->cpubind->get_thisthread_cpubind &&
        topology_support->discovery->pu && !__kmp_hwloc_error) {
      // enables affinity according to KMP_AFFINITY_CAPABLE() macro
      KMP_AFFINITY_ENABLE(TRUE);
    } else {
      // indicate that hwloc didn't work and disable affinity
      __kmp_hwloc_error = TRUE;
      KMP_AFFINITY_DISABLE();
    }
  }
  void bind_thread(int which) override {
    KMP_ASSERT2(KMP_AFFINITY_CAPABLE(),
                "Illegal set affinity operation when not capable");
    KMPAffinity::Mask *mask;
    KMP_CPU_ALLOC_ON_STACK(mask);
    KMP_CPU_ZERO(mask);
    KMP_CPU_SET(which, mask);
    __kmp_set_system_affinity(mask, TRUE);
    KMP_CPU_FREE_FROM_STACK(mask);
  }
  KMPAffinity::Mask *allocate_mask() override { return new Mask(); }
  void deallocate_mask(KMPAffinity::Mask *m) override { delete m; }
  KMPAffinity::Mask *allocate_mask_array(int num) override {
    return new Mask[num];
  }
  void deallocate_mask_array(KMPAffinity::Mask *array) override {
    Mask *hwloc_array = static_cast<Mask *>(array);
    delete[] hwloc_array;
  }
  KMPAffinity::Mask *index_mask_array(KMPAffinity::Mask *array,
                                      int index) override {
    Mask *hwloc_array = static_cast<Mask *>(array);
    return &(hwloc_array[index]);
  }
  api_type get_api_type() const override { return HWLOC; }
};
#endif /* KMP_USE_HWLOC */

#if KMP_OS_LINUX
/* On some of the older OS's that we build on, these constants aren't present
   in <asm/unistd.h> #included from <sys.syscall.h>. They must be the same on
   all systems of the same arch where they are defined, and they cannot change.
   stone forever. */
#include <sys/syscall.h>
#if KMP_ARCH_X86 || KMP_ARCH_ARM
#ifndef __NR_sched_setaffinity
#define __NR_sched_setaffinity 241
#elif __NR_sched_setaffinity != 241
#error Wrong code for setaffinity system call.
#endif /* __NR_sched_setaffinity */
#ifndef __NR_sched_getaffinity
#define __NR_sched_getaffinity 242
#elif __NR_sched_getaffinity != 242
#error Wrong code for getaffinity system call.
#endif /* __NR_sched_getaffinity */
#elif KMP_ARCH_AARCH64
#ifndef __NR_sched_setaffinity
#define __NR_sched_setaffinity 122
#elif __NR_sched_setaffinity != 122
#error Wrong code for setaffinity system call.
#endif /* __NR_sched_setaffinity */
#ifndef __NR_sched_getaffinity
#define __NR_sched_getaffinity 123
#elif __NR_sched_getaffinity != 123
#error Wrong code for getaffinity system call.
#endif /* __NR_sched_getaffinity */
#elif KMP_ARCH_X86_64
#ifndef __NR_sched_setaffinity
#define __NR_sched_setaffinity 203
#elif __NR_sched_setaffinity != 203
#error Wrong code for setaffinity system call.
#endif /* __NR_sched_setaffinity */
#ifndef __NR_sched_getaffinity
#define __NR_sched_getaffinity 204
#elif __NR_sched_getaffinity != 204
#error Wrong code for getaffinity system call.
#endif /* __NR_sched_getaffinity */
#elif KMP_ARCH_PPC64
#ifndef __NR_sched_setaffinity
#define __NR_sched_setaffinity 222
#elif __NR_sched_setaffinity != 222
#error Wrong code for setaffinity system call.
#endif /* __NR_sched_setaffinity */
#ifndef __NR_sched_getaffinity
#define __NR_sched_getaffinity 223
#elif __NR_sched_getaffinity != 223
#error Wrong code for getaffinity system call.
#endif /* __NR_sched_getaffinity */
#elif KMP_ARCH_MIPS
#ifndef __NR_sched_setaffinity
#define __NR_sched_setaffinity 4239
#elif __NR_sched_setaffinity != 4239
#error Wrong code for setaffinity system call.
#endif /* __NR_sched_setaffinity */
#ifndef __NR_sched_getaffinity
#define __NR_sched_getaffinity 4240
#elif __NR_sched_getaffinity != 4240
#error Wrong code for getaffinity system call.
#endif /* __NR_sched_getaffinity */
#elif KMP_ARCH_MIPS64
#ifndef __NR_sched_setaffinity
#define __NR_sched_setaffinity 5195
#elif __NR_sched_setaffinity != 5195
#error Wrong code for setaffinity system call.
#endif /* __NR_sched_setaffinity */
#ifndef __NR_sched_getaffinity
#define __NR_sched_getaffinity 5196
#elif __NR_sched_getaffinity != 5196
#error Wrong code for getaffinity system call.
#endif /* __NR_sched_getaffinity */
#error Unknown or unsupported architecture
#endif /* KMP_ARCH_* */
class KMPNativeAffinity : public KMPAffinity {
  class Mask : public KMPAffinity::Mask {
    typedef unsigned char mask_t;
    static const int BITS_PER_MASK_T = sizeof(mask_t) * CHAR_BIT;

  public:
    mask_t *mask;
    Mask() { mask = (mask_t *)__kmp_allocate(__kmp_affin_mask_size); }
    ~Mask() {
      if (mask)
        __kmp_free(mask);
    }
    void set(int i) override {
      mask[i / BITS_PER_MASK_T] |= ((mask_t)1 << (i % BITS_PER_MASK_T));
    }
    bool is_set(int i) const override {
      return (mask[i / BITS_PER_MASK_T] & ((mask_t)1 << (i % BITS_PER_MASK_T)));
    }
    void clear(int i) override {
      mask[i / BITS_PER_MASK_T] &= ~((mask_t)1 << (i % BITS_PER_MASK_T));
    }
    void zero() override {
      for (size_t i = 0; i < __kmp_affin_mask_size; ++i)
        mask[i] = 0;
    }
    void copy(const KMPAffinity::Mask *src) override {
      const Mask *convert = static_cast<const Mask *>(src);
      for (size_t i = 0; i < __kmp_affin_mask_size; ++i)
        mask[i] = convert->mask[i];
    }
    void bitwise_and(const KMPAffinity::Mask *rhs) override {
      const Mask *convert = static_cast<const Mask *>(rhs);
      for (size_t i = 0; i < __kmp_affin_mask_size; ++i)
        mask[i] &= convert->mask[i];
    }
    void bitwise_or(const KMPAffinity::Mask *rhs) override {
      const Mask *convert = static_cast<const Mask *>(rhs);
      for (size_t i = 0; i < __kmp_affin_mask_size; ++i)
        mask[i] |= convert->mask[i];
    }
    void bitwise_not() override {
      for (size_t i = 0; i < __kmp_affin_mask_size; ++i)
        mask[i] = ~(mask[i]);
    }
    int begin() const override {
      int retval = 0;
      while (retval < end() && !is_set(retval))
        ++retval;
      return retval;
    }
    int end() const override { return __kmp_affin_mask_size * BITS_PER_MASK_T; }
    int next(int previous) const override {
      int retval = previous + 1;
      while (retval < end() && !is_set(retval))
        ++retval;
      return retval;
    }
    int get_system_affinity(bool abort_on_error) override {
      KMP_ASSERT2(KMP_AFFINITY_CAPABLE(),
                  "Illegal get affinity operation when not capable");
      int retval =
          syscall(__NR_sched_getaffinity, 0, __kmp_affin_mask_size, mask);
      if (retval >= 0) {
        return 0;
      }
      int error = errno;
      if (abort_on_error) {
        __kmp_fatal(KMP_MSG(FatalSysError), KMP_ERR(error), __kmp_msg_null);
      }
      return error;
    }
    int set_system_affinity(bool abort_on_error) const override {
      KMP_ASSERT2(KMP_AFFINITY_CAPABLE(),
                  "Illegal get affinity operation when not capable");
      int retval =
          syscall(__NR_sched_setaffinity, 0, __kmp_affin_mask_size, mask);
      if (retval >= 0) {
        return 0;
      }
      int error = errno;
      if (abort_on_error) {
        __kmp_fatal(KMP_MSG(FatalSysError), KMP_ERR(error), __kmp_msg_null);
      }
      return error;
    }
  };
  void determine_capable(const char *env_var) override {
    __kmp_affinity_determine_capable(env_var);
  }
  void bind_thread(int which) override { __kmp_affinity_bind_thread(which); }
  KMPAffinity::Mask *allocate_mask() override {
    KMPNativeAffinity::Mask *retval = new Mask();
    return retval;
  }
  void deallocate_mask(KMPAffinity::Mask *m) override {
    KMPNativeAffinity::Mask *native_mask =
        static_cast<KMPNativeAffinity::Mask *>(m);
    delete native_mask;
  }
  KMPAffinity::Mask *allocate_mask_array(int num) override {
    return new Mask[num];
  }
  void deallocate_mask_array(KMPAffinity::Mask *array) override {
    Mask *linux_array = static_cast<Mask *>(array);
    delete[] linux_array;
  }
  KMPAffinity::Mask *index_mask_array(KMPAffinity::Mask *array,
                                      int index) override {
    Mask *linux_array = static_cast<Mask *>(array);
    return &(linux_array[index]);
  }
  api_type get_api_type() const override { return NATIVE_OS; }
};
#endif /* KMP_OS_LINUX */

#if KMP_OS_WINDOWS
class KMPNativeAffinity : public KMPAffinity {
  class Mask : public KMPAffinity::Mask {
    typedef ULONG_PTR mask_t;
    static const int BITS_PER_MASK_T = sizeof(mask_t) * CHAR_BIT;
    mask_t *mask;

  public:
    Mask() {
      mask = (mask_t *)__kmp_allocate(sizeof(mask_t) * __kmp_num_proc_groups);
    }
    ~Mask() {
      if (mask)
        __kmp_free(mask);
    }
    void set(int i) override {
      mask[i / BITS_PER_MASK_T] |= ((mask_t)1 << (i % BITS_PER_MASK_T));
    }
    bool is_set(int i) const override {
      return (mask[i / BITS_PER_MASK_T] & ((mask_t)1 << (i % BITS_PER_MASK_T)));
    }
    void clear(int i) override {
      mask[i / BITS_PER_MASK_T] &= ~((mask_t)1 << (i % BITS_PER_MASK_T));
    }
    void zero() override {
      for (int i = 0; i < __kmp_num_proc_groups; ++i)
        mask[i] = 0;
    }
    void copy(const KMPAffinity::Mask *src) override {
      const Mask *convert = static_cast<const Mask *>(src);
      for (int i = 0; i < __kmp_num_proc_groups; ++i)
        mask[i] = convert->mask[i];
    }
    void bitwise_and(const KMPAffinity::Mask *rhs) override {
      const Mask *convert = static_cast<const Mask *>(rhs);
      for (int i = 0; i < __kmp_num_proc_groups; ++i)
        mask[i] &= convert->mask[i];
    }
    void bitwise_or(const KMPAffinity::Mask *rhs) override {
      const Mask *convert = static_cast<const Mask *>(rhs);
      for (int i = 0; i < __kmp_num_proc_groups; ++i)
        mask[i] |= convert->mask[i];
    }
    void bitwise_not() override {
      for (int i = 0; i < __kmp_num_proc_groups; ++i)
        mask[i] = ~(mask[i]);
    }
    int begin() const override {
      int retval = 0;
      while (retval < end() && !is_set(retval))
        ++retval;
      return retval;
    }
    int end() const override { return __kmp_num_proc_groups * BITS_PER_MASK_T; }
    int next(int previous) const override {
      int retval = previous + 1;
      while (retval < end() && !is_set(retval))
        ++retval;
      return retval;
    }
    int set_system_affinity(bool abort_on_error) const override {
      if (__kmp_num_proc_groups > 1) {
        // Check for a valid mask.
        GROUP_AFFINITY ga;
        int group = get_proc_group();
        if (group < 0) {
          if (abort_on_error) {
            KMP_FATAL(AffinityInvalidMask, "kmp_set_affinity");
          }
          return -1;
        }
        // Transform the bit vector into a GROUP_AFFINITY struct
        // and make the system call to set affinity.
        ga.Group = group;
        ga.Mask = mask[group];
        ga.Reserved[0] = ga.Reserved[1] = ga.Reserved[2] = 0;

        KMP_DEBUG_ASSERT(__kmp_SetThreadGroupAffinity != NULL);
        if (__kmp_SetThreadGroupAffinity(GetCurrentThread(), &ga, NULL) == 0) {
          DWORD error = GetLastError();
          if (abort_on_error) {
            __kmp_fatal(KMP_MSG(CantSetThreadAffMask), KMP_ERR(error),
                        __kmp_msg_null);
          }
          return error;
        }
      } else {
        if (!SetThreadAffinityMask(GetCurrentThread(), *mask)) {
          DWORD error = GetLastError();
          if (abort_on_error) {
            __kmp_fatal(KMP_MSG(CantSetThreadAffMask), KMP_ERR(error),
                        __kmp_msg_null);
          }
          return error;
        }
      }
      return 0;
    }
    int get_system_affinity(bool abort_on_error) override {
      if (__kmp_num_proc_groups > 1) {
        this->zero();
        GROUP_AFFINITY ga;
        KMP_DEBUG_ASSERT(__kmp_GetThreadGroupAffinity != NULL);
        if (__kmp_GetThreadGroupAffinity(GetCurrentThread(), &ga) == 0) {
          DWORD error = GetLastError();
          if (abort_on_error) {
            __kmp_fatal(KMP_MSG(FunctionError, "GetThreadGroupAffinity()"),
                        KMP_ERR(error), __kmp_msg_null);
          }
          return error;
        }
        if ((ga.Group < 0) || (ga.Group > __kmp_num_proc_groups) ||
            (ga.Mask == 0)) {
          return -1;
        }
        mask[ga.Group] = ga.Mask;
      } else {
        mask_t newMask, sysMask, retval;
        if (!GetProcessAffinityMask(GetCurrentProcess(), &newMask, &sysMask)) {
          DWORD error = GetLastError();
          if (abort_on_error) {
            __kmp_fatal(KMP_MSG(FunctionError, "GetProcessAffinityMask()"),
                        KMP_ERR(error), __kmp_msg_null);
          }
          return error;
        }
        retval = SetThreadAffinityMask(GetCurrentThread(), newMask);
        if (!retval) {
          DWORD error = GetLastError();
          if (abort_on_error) {
            __kmp_fatal(KMP_MSG(FunctionError, "SetThreadAffinityMask()"),
                        KMP_ERR(error), __kmp_msg_null);
          }
          return error;
        }
        newMask = SetThreadAffinityMask(GetCurrentThread(), retval);
        if (!newMask) {
          DWORD error = GetLastError();
          if (abort_on_error) {
            __kmp_fatal(KMP_MSG(FunctionError, "SetThreadAffinityMask()"),
                        KMP_ERR(error), __kmp_msg_null);
          }
        }
        *mask = retval;
      }
      return 0;
    }
    int get_proc_group() const override {
      int group = -1;
      if (__kmp_num_proc_groups == 1) {
        return 1;
      }
      for (int i = 0; i < __kmp_num_proc_groups; i++) {
        if (mask[i] == 0)
          continue;
        if (group >= 0)
          return -1;
        group = i;
      }
      return group;
    }
  };
  void determine_capable(const char *env_var) override {
    __kmp_affinity_determine_capable(env_var);
  }
  void bind_thread(int which) override { __kmp_affinity_bind_thread(which); }
  KMPAffinity::Mask *allocate_mask() override { return new Mask(); }
  void deallocate_mask(KMPAffinity::Mask *m) override { delete m; }
  KMPAffinity::Mask *allocate_mask_array(int num) override {
    return new Mask[num];
  }
  void deallocate_mask_array(KMPAffinity::Mask *array) override {
    Mask *windows_array = static_cast<Mask *>(array);
    delete[] windows_array;
  }
  KMPAffinity::Mask *index_mask_array(KMPAffinity::Mask *array,
                                      int index) override {
    Mask *windows_array = static_cast<Mask *>(array);
    return &(windows_array[index]);
  }
  api_type get_api_type() const override { return NATIVE_OS; }
};
#endif /* KMP_OS_WINDOWS */
#endif /* KMP_AFFINITY_SUPPORTED */

class Address {
public:
  static const unsigned maxDepth = 32;
  unsigned labels[maxDepth];
  unsigned childNums[maxDepth];
  unsigned depth;
  unsigned leader;
  Address(unsigned _depth) : depth(_depth), leader(FALSE) {}
  Address &operator=(const Address &b) {
    depth = b.depth;
    for (unsigned i = 0; i < depth; i++) {
      labels[i] = b.labels[i];
      childNums[i] = b.childNums[i];
    }
    leader = FALSE;
    return *this;
  }
  bool operator==(const Address &b) const {
    if (depth != b.depth)
      return false;
    for (unsigned i = 0; i < depth; i++)
      if (labels[i] != b.labels[i])
        return false;
    return true;
  }
  bool isClose(const Address &b, int level) const {
    if (depth != b.depth)
      return false;
    if ((unsigned)level >= depth)
      return true;
    for (unsigned i = 0; i < (depth - level); i++)
      if (labels[i] != b.labels[i])
        return false;
    return true;
  }
  bool operator!=(const Address &b) const { return !operator==(b); }
  void print() const {
    unsigned i;
    printf("Depth: %u --- ", depth);
    for (i = 0; i < depth; i++) {
      printf("%u ", labels[i]);
    }
  }
};

class AddrUnsPair {
public:
  Address first;
  unsigned second;
  AddrUnsPair(Address _first, unsigned _second)
      : first(_first), second(_second) {}
  AddrUnsPair &operator=(const AddrUnsPair &b) {
    first = b.first;
    second = b.second;
    return *this;
  }
  void print() const {
    printf("first = ");
    first.print();
    printf(" --- second = %u", second);
  }
  bool operator==(const AddrUnsPair &b) const {
    if (first != b.first)
      return false;
    if (second != b.second)
      return false;
    return true;
  }
  bool operator!=(const AddrUnsPair &b) const { return !operator==(b); }
};

static int __kmp_affinity_cmp_Address_labels(const void *a, const void *b) {
  const Address *aa = &(((const AddrUnsPair *)a)->first);
  const Address *bb = &(((const AddrUnsPair *)b)->first);
  unsigned depth = aa->depth;
  unsigned i;
  KMP_DEBUG_ASSERT(depth == bb->depth);
  for (i = 0; i < depth; i++) {
    if (aa->labels[i] < bb->labels[i])
      return -1;
    if (aa->labels[i] > bb->labels[i])
      return 1;
  }
  return 0;
}

/* A structure for holding machine-specific hierarchy info to be computed once
   at init. This structure represents a mapping of threads to the actual machine
   hierarchy, or to our best guess at what the hierarchy might be, for the
   purpose of performing an efficient barrier. In the worst case, when there is
   no machine hierarchy information, it produces a tree suitable for a barrier,
   similar to the tree used in the hyper barrier. */
class hierarchy_info {
public:
  /* Good default values for number of leaves and branching factor, given no
     affinity information. Behaves a bit like hyper barrier. */
  static const kmp_uint32 maxLeaves = 4;
  static const kmp_uint32 minBranch = 4;
  /** Number of levels in the hierarchy. Typical levels are threads/core,
      cores/package or socket, packages/node, nodes/machine, etc. We don't want
      to get specific with nomenclature. When the machine is oversubscribed we
      add levels to duplicate the hierarchy, doubling the thread capacity of the
      hierarchy each time we add a level. */
  kmp_uint32 maxLevels;

  /** This is specifically the depth of the machine configuration hierarchy, in
      terms of the number of levels along the longest path from root to any
      leaf. It corresponds to the number of entries in numPerLevel if we exclude
      all but one trailing 1. */
  kmp_uint32 depth;
  kmp_uint32 base_num_threads;
  enum init_status { initialized = 0, not_initialized = 1, initializing = 2 };
  volatile kmp_int8 uninitialized; // 0=initialized, 1=not initialized,
  // 2=initialization in progress
  volatile kmp_int8 resizing; // 0=not resizing, 1=resizing

  /** Level 0 corresponds to leaves. numPerLevel[i] is the number of children
      the parent of a node at level i has. For example, if we have a machine
      with 4 packages, 4 cores/package and 2 HT per core, then numPerLevel =
      {2, 4, 4, 1, 1}. All empty levels are set to 1. */
  kmp_uint32 *numPerLevel;
  kmp_uint32 *skipPerLevel;

  void deriveLevels(AddrUnsPair *adr2os, int num_addrs) {
    int hier_depth = adr2os[0].first.depth;
    int level = 0;
    for (int i = hier_depth - 1; i >= 0; --i) {
      int max = -1;
      for (int j = 0; j < num_addrs; ++j) {
        int next = adr2os[j].first.childNums[i];
        if (next > max)
          max = next;
      }
      numPerLevel[level] = max + 1;
      ++level;
    }
  }

  hierarchy_info()
      : maxLevels(7), depth(1), uninitialized(not_initialized), resizing(0) {}

  void fini() {
    if (!uninitialized && numPerLevel) {
      __kmp_free(numPerLevel);
      numPerLevel = NULL;
      uninitialized = not_initialized;
    }
  }

  void init(AddrUnsPair *adr2os, int num_addrs) {
    kmp_int8 bool_result = KMP_COMPARE_AND_STORE_ACQ8(
        &uninitialized, not_initialized, initializing);
    if (bool_result == 0) { // Wait for initialization
      while (TCR_1(uninitialized) != initialized)
        KMP_CPU_PAUSE();
      return;
    }
    KMP_DEBUG_ASSERT(bool_result == 1);

    /* Added explicit initialization of the data fields here to prevent usage of
       dirty value observed when static library is re-initialized multiple times
       (e.g. when non-OpenMP thread repeatedly launches/joins thread that uses
       OpenMP). */
    depth = 1;
    resizing = 0;
    maxLevels = 7;
    numPerLevel =
        (kmp_uint32 *)__kmp_allocate(maxLevels * 2 * sizeof(kmp_uint32));
    skipPerLevel = &(numPerLevel[maxLevels]);
    for (kmp_uint32 i = 0; i < maxLevels;
         ++i) { // init numPerLevel[*] to 1 item per level
      numPerLevel[i] = 1;
      skipPerLevel[i] = 1;
    }

    // Sort table by physical ID
    if (adr2os) {
      qsort(adr2os, num_addrs, sizeof(*adr2os),
            __kmp_affinity_cmp_Address_labels);
      deriveLevels(adr2os, num_addrs);
    } else {
      numPerLevel[0] = maxLeaves;
      numPerLevel[1] = num_addrs / maxLeaves;
      if (num_addrs % maxLeaves)
        numPerLevel[1]++;
    }

    base_num_threads = num_addrs;
    for (int i = maxLevels - 1; i >= 0;
         --i) // count non-empty levels to get depth
      if (numPerLevel[i] != 1 || depth > 1) // only count one top-level '1'
        depth++;

    kmp_uint32 branch = minBranch;
    if (numPerLevel[0] == 1)
      branch = num_addrs / maxLeaves;
    if (branch < minBranch)
      branch = minBranch;
    for (kmp_uint32 d = 0; d < depth - 1; ++d) { // optimize hierarchy width
      while (numPerLevel[d] > branch ||
             (d == 0 && numPerLevel[d] > maxLeaves)) { // max 4 on level 0!
        if (numPerLevel[d] & 1)
          numPerLevel[d]++;
        numPerLevel[d] = numPerLevel[d] >> 1;
        if (numPerLevel[d + 1] == 1)
          depth++;
        numPerLevel[d + 1] = numPerLevel[d + 1] << 1;
      }
      if (numPerLevel[0] == 1) {
        branch = branch >> 1;
        if (branch < 4)
          branch = minBranch;
      }
    }

    for (kmp_uint32 i = 1; i < depth; ++i)
      skipPerLevel[i] = numPerLevel[i - 1] * skipPerLevel[i - 1];
    // Fill in hierarchy in the case of oversubscription
    for (kmp_uint32 i = depth; i < maxLevels; ++i)
      skipPerLevel[i] = 2 * skipPerLevel[i - 1];

    uninitialized = initialized; // One writer
  }

  // Resize the hierarchy if nproc changes to something larger than before
  void resize(kmp_uint32 nproc) {
    kmp_int8 bool_result = KMP_COMPARE_AND_STORE_ACQ8(&resizing, 0, 1);
    while (bool_result == 0) { // someone else is trying to resize
      KMP_CPU_PAUSE();
      if (nproc <= base_num_threads) // happy with other thread's resize
        return;
      else // try to resize
        bool_result = KMP_COMPARE_AND_STORE_ACQ8(&resizing, 0, 1);
    }
    KMP_DEBUG_ASSERT(bool_result != 0);
    if (nproc <= base_num_threads)
      return; // happy with other thread's resize

    // Calculate new maxLevels
    kmp_uint32 old_sz = skipPerLevel[depth - 1];
    kmp_uint32 incs = 0, old_maxLevels = maxLevels;
    // First see if old maxLevels is enough to contain new size
    for (kmp_uint32 i = depth; i < maxLevels && nproc > old_sz; ++i) {
      skipPerLevel[i] = 2 * skipPerLevel[i - 1];
      numPerLevel[i - 1] *= 2;
      old_sz *= 2;
      depth++;
    }
    if (nproc > old_sz) { // Not enough space, need to expand hierarchy
      while (nproc > old_sz) {
        old_sz *= 2;
        incs++;
        depth++;
      }
      maxLevels += incs;

      // Resize arrays
      kmp_uint32 *old_numPerLevel = numPerLevel;
      kmp_uint32 *old_skipPerLevel = skipPerLevel;
      numPerLevel = skipPerLevel = NULL;
      numPerLevel =
          (kmp_uint32 *)__kmp_allocate(maxLevels * 2 * sizeof(kmp_uint32));
      skipPerLevel = &(numPerLevel[maxLevels]);

      // Copy old elements from old arrays
      for (kmp_uint32 i = 0; i < old_maxLevels;
           ++i) { // init numPerLevel[*] to 1 item per level
        numPerLevel[i] = old_numPerLevel[i];
        skipPerLevel[i] = old_skipPerLevel[i];
      }

      // Init new elements in arrays to 1
      for (kmp_uint32 i = old_maxLevels; i < maxLevels;
           ++i) { // init numPerLevel[*] to 1 item per level
        numPerLevel[i] = 1;
        skipPerLevel[i] = 1;
      }

      // Free old arrays
      __kmp_free(old_numPerLevel);
    }

    // Fill in oversubscription levels of hierarchy
    for (kmp_uint32 i = old_maxLevels; i < maxLevels; ++i)
      skipPerLevel[i] = 2 * skipPerLevel[i - 1];

    base_num_threads = nproc;
    resizing = 0; // One writer
  }
};
#endif // KMP_AFFINITY_H
