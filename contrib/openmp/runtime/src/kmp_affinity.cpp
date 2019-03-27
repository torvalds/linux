/*
 * kmp_affinity.cpp -- affinity management
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp.h"
#include "kmp_affinity.h"
#include "kmp_i18n.h"
#include "kmp_io.h"
#include "kmp_str.h"
#include "kmp_wrapper_getpid.h"
#if KMP_USE_HIER_SCHED
#include "kmp_dispatch_hier.h"
#endif

// Store the real or imagined machine hierarchy here
static hierarchy_info machine_hierarchy;

void __kmp_cleanup_hierarchy() { machine_hierarchy.fini(); }

void __kmp_get_hierarchy(kmp_uint32 nproc, kmp_bstate_t *thr_bar) {
  kmp_uint32 depth;
  // The test below is true if affinity is available, but set to "none". Need to
  // init on first use of hierarchical barrier.
  if (TCR_1(machine_hierarchy.uninitialized))
    machine_hierarchy.init(NULL, nproc);

  // Adjust the hierarchy in case num threads exceeds original
  if (nproc > machine_hierarchy.base_num_threads)
    machine_hierarchy.resize(nproc);

  depth = machine_hierarchy.depth;
  KMP_DEBUG_ASSERT(depth > 0);

  thr_bar->depth = depth;
  thr_bar->base_leaf_kids = (kmp_uint8)machine_hierarchy.numPerLevel[0] - 1;
  thr_bar->skip_per_level = machine_hierarchy.skipPerLevel;
}

#if KMP_AFFINITY_SUPPORTED

bool KMPAffinity::picked_api = false;

void *KMPAffinity::Mask::operator new(size_t n) { return __kmp_allocate(n); }
void *KMPAffinity::Mask::operator new[](size_t n) { return __kmp_allocate(n); }
void KMPAffinity::Mask::operator delete(void *p) { __kmp_free(p); }
void KMPAffinity::Mask::operator delete[](void *p) { __kmp_free(p); }
void *KMPAffinity::operator new(size_t n) { return __kmp_allocate(n); }
void KMPAffinity::operator delete(void *p) { __kmp_free(p); }

void KMPAffinity::pick_api() {
  KMPAffinity *affinity_dispatch;
  if (picked_api)
    return;
#if KMP_USE_HWLOC
  // Only use Hwloc if affinity isn't explicitly disabled and
  // user requests Hwloc topology method
  if (__kmp_affinity_top_method == affinity_top_method_hwloc &&
      __kmp_affinity_type != affinity_disabled) {
    affinity_dispatch = new KMPHwlocAffinity();
  } else
#endif
  {
    affinity_dispatch = new KMPNativeAffinity();
  }
  __kmp_affinity_dispatch = affinity_dispatch;
  picked_api = true;
}

void KMPAffinity::destroy_api() {
  if (__kmp_affinity_dispatch != NULL) {
    delete __kmp_affinity_dispatch;
    __kmp_affinity_dispatch = NULL;
    picked_api = false;
  }
}

#define KMP_ADVANCE_SCAN(scan)                                                 \
  while (*scan != '\0') {                                                      \
    scan++;                                                                    \
  }

// Print the affinity mask to the character array in a pretty format.
// The format is a comma separated list of non-negative integers or integer
// ranges: e.g., 1,2,3-5,7,9-15
// The format can also be the string "{<empty>}" if no bits are set in mask
char *__kmp_affinity_print_mask(char *buf, int buf_len,
                                kmp_affin_mask_t *mask) {
  int start = 0, finish = 0, previous = 0;
  bool first_range;
  KMP_ASSERT(buf);
  KMP_ASSERT(buf_len >= 40);
  KMP_ASSERT(mask);
  char *scan = buf;
  char *end = buf + buf_len - 1;

  // Check for empty set.
  if (mask->begin() == mask->end()) {
    KMP_SNPRINTF(scan, end - scan + 1, "{<empty>}");
    KMP_ADVANCE_SCAN(scan);
    KMP_ASSERT(scan <= end);
    return buf;
  }

  first_range = true;
  start = mask->begin();
  while (1) {
    // Find next range
    // [start, previous] is inclusive range of contiguous bits in mask
    for (finish = mask->next(start), previous = start;
         finish == previous + 1 && finish != mask->end();
         finish = mask->next(finish)) {
      previous = finish;
    }

    // The first range does not need a comma printed before it, but the rest
    // of the ranges do need a comma beforehand
    if (!first_range) {
      KMP_SNPRINTF(scan, end - scan + 1, "%s", ",");
      KMP_ADVANCE_SCAN(scan);
    } else {
      first_range = false;
    }
    // Range with three or more contiguous bits in the affinity mask
    if (previous - start > 1) {
      KMP_SNPRINTF(scan, end - scan + 1, "%d-%d", static_cast<int>(start),
                   static_cast<int>(previous));
    } else {
      // Range with one or two contiguous bits in the affinity mask
      KMP_SNPRINTF(scan, end - scan + 1, "%d", static_cast<int>(start));
      KMP_ADVANCE_SCAN(scan);
      if (previous - start > 0) {
        KMP_SNPRINTF(scan, end - scan + 1, ",%d", static_cast<int>(previous));
      }
    }
    KMP_ADVANCE_SCAN(scan);
    // Start over with new start point
    start = finish;
    if (start == mask->end())
      break;
    // Check for overflow
    if (end - scan < 2)
      break;
  }

  // Check for overflow
  KMP_ASSERT(scan <= end);
  return buf;
}
#undef KMP_ADVANCE_SCAN

// Print the affinity mask to the string buffer object in a pretty format
// The format is a comma separated list of non-negative integers or integer
// ranges: e.g., 1,2,3-5,7,9-15
// The format can also be the string "{<empty>}" if no bits are set in mask
kmp_str_buf_t *__kmp_affinity_str_buf_mask(kmp_str_buf_t *buf,
                                           kmp_affin_mask_t *mask) {
  int start = 0, finish = 0, previous = 0;
  bool first_range;
  KMP_ASSERT(buf);
  KMP_ASSERT(mask);

  __kmp_str_buf_clear(buf);

  // Check for empty set.
  if (mask->begin() == mask->end()) {
    __kmp_str_buf_print(buf, "%s", "{<empty>}");
    return buf;
  }

  first_range = true;
  start = mask->begin();
  while (1) {
    // Find next range
    // [start, previous] is inclusive range of contiguous bits in mask
    for (finish = mask->next(start), previous = start;
         finish == previous + 1 && finish != mask->end();
         finish = mask->next(finish)) {
      previous = finish;
    }

    // The first range does not need a comma printed before it, but the rest
    // of the ranges do need a comma beforehand
    if (!first_range) {
      __kmp_str_buf_print(buf, "%s", ",");
    } else {
      first_range = false;
    }
    // Range with three or more contiguous bits in the affinity mask
    if (previous - start > 1) {
      __kmp_str_buf_print(buf, "%d-%d", static_cast<int>(start),
                          static_cast<int>(previous));
    } else {
      // Range with one or two contiguous bits in the affinity mask
      __kmp_str_buf_print(buf, "%d", static_cast<int>(start));
      if (previous - start > 0) {
        __kmp_str_buf_print(buf, ",%d", static_cast<int>(previous));
      }
    }
    // Start over with new start point
    start = finish;
    if (start == mask->end())
      break;
  }
  return buf;
}

void __kmp_affinity_entire_machine_mask(kmp_affin_mask_t *mask) {
  KMP_CPU_ZERO(mask);

#if KMP_GROUP_AFFINITY

  if (__kmp_num_proc_groups > 1) {
    int group;
    KMP_DEBUG_ASSERT(__kmp_GetActiveProcessorCount != NULL);
    for (group = 0; group < __kmp_num_proc_groups; group++) {
      int i;
      int num = __kmp_GetActiveProcessorCount(group);
      for (i = 0; i < num; i++) {
        KMP_CPU_SET(i + group * (CHAR_BIT * sizeof(DWORD_PTR)), mask);
      }
    }
  } else

#endif /* KMP_GROUP_AFFINITY */

  {
    int proc;
    for (proc = 0; proc < __kmp_xproc; proc++) {
      KMP_CPU_SET(proc, mask);
    }
  }
}

// When sorting by labels, __kmp_affinity_assign_child_nums() must first be
// called to renumber the labels from [0..n] and place them into the child_num
// vector of the address object.  This is done in case the labels used for
// the children at one node of the hierarchy differ from those used for
// another node at the same level.  Example:  suppose the machine has 2 nodes
// with 2 packages each.  The first node contains packages 601 and 602, and
// second node contains packages 603 and 604.  If we try to sort the table
// for "scatter" affinity, the table will still be sorted 601, 602, 603, 604
// because we are paying attention to the labels themselves, not the ordinal
// child numbers.  By using the child numbers in the sort, the result is
// {0,0}=601, {0,1}=603, {1,0}=602, {1,1}=604.
static void __kmp_affinity_assign_child_nums(AddrUnsPair *address2os,
                                             int numAddrs) {
  KMP_DEBUG_ASSERT(numAddrs > 0);
  int depth = address2os->first.depth;
  unsigned *counts = (unsigned *)__kmp_allocate(depth * sizeof(unsigned));
  unsigned *lastLabel = (unsigned *)__kmp_allocate(depth * sizeof(unsigned));
  int labCt;
  for (labCt = 0; labCt < depth; labCt++) {
    address2os[0].first.childNums[labCt] = counts[labCt] = 0;
    lastLabel[labCt] = address2os[0].first.labels[labCt];
  }
  int i;
  for (i = 1; i < numAddrs; i++) {
    for (labCt = 0; labCt < depth; labCt++) {
      if (address2os[i].first.labels[labCt] != lastLabel[labCt]) {
        int labCt2;
        for (labCt2 = labCt + 1; labCt2 < depth; labCt2++) {
          counts[labCt2] = 0;
          lastLabel[labCt2] = address2os[i].first.labels[labCt2];
        }
        counts[labCt]++;
        lastLabel[labCt] = address2os[i].first.labels[labCt];
        break;
      }
    }
    for (labCt = 0; labCt < depth; labCt++) {
      address2os[i].first.childNums[labCt] = counts[labCt];
    }
    for (; labCt < (int)Address::maxDepth; labCt++) {
      address2os[i].first.childNums[labCt] = 0;
    }
  }
  __kmp_free(lastLabel);
  __kmp_free(counts);
}

// All of the __kmp_affinity_create_*_map() routines should set
// __kmp_affinity_masks to a vector of affinity mask objects of length
// __kmp_affinity_num_masks, if __kmp_affinity_type != affinity_none, and return
// the number of levels in the machine topology tree (zero if
// __kmp_affinity_type == affinity_none).
//
// All of the __kmp_affinity_create_*_map() routines should set
// *__kmp_affin_fullMask to the affinity mask for the initialization thread.
// They need to save and restore the mask, and it could be needed later, so
// saving it is just an optimization to avoid calling kmp_get_system_affinity()
// again.
kmp_affin_mask_t *__kmp_affin_fullMask = NULL;

static int nCoresPerPkg, nPackages;
static int __kmp_nThreadsPerCore;
#ifndef KMP_DFLT_NTH_CORES
static int __kmp_ncores;
#endif
static int *__kmp_pu_os_idx = NULL;

// __kmp_affinity_uniform_topology() doesn't work when called from
// places which support arbitrarily many levels in the machine topology
// map, i.e. the non-default cases in __kmp_affinity_create_cpuinfo_map()
// __kmp_affinity_create_x2apicid_map().
inline static bool __kmp_affinity_uniform_topology() {
  return __kmp_avail_proc == (__kmp_nThreadsPerCore * nCoresPerPkg * nPackages);
}

// Print out the detailed machine topology map, i.e. the physical locations
// of each OS proc.
static void __kmp_affinity_print_topology(AddrUnsPair *address2os, int len,
                                          int depth, int pkgLevel,
                                          int coreLevel, int threadLevel) {
  int proc;

  KMP_INFORM(OSProcToPhysicalThreadMap, "KMP_AFFINITY");
  for (proc = 0; proc < len; proc++) {
    int level;
    kmp_str_buf_t buf;
    __kmp_str_buf_init(&buf);
    for (level = 0; level < depth; level++) {
      if (level == threadLevel) {
        __kmp_str_buf_print(&buf, "%s ", KMP_I18N_STR(Thread));
      } else if (level == coreLevel) {
        __kmp_str_buf_print(&buf, "%s ", KMP_I18N_STR(Core));
      } else if (level == pkgLevel) {
        __kmp_str_buf_print(&buf, "%s ", KMP_I18N_STR(Package));
      } else if (level > pkgLevel) {
        __kmp_str_buf_print(&buf, "%s_%d ", KMP_I18N_STR(Node),
                            level - pkgLevel - 1);
      } else {
        __kmp_str_buf_print(&buf, "L%d ", level);
      }
      __kmp_str_buf_print(&buf, "%d ", address2os[proc].first.labels[level]);
    }
    KMP_INFORM(OSProcMapToPack, "KMP_AFFINITY", address2os[proc].second,
               buf.str);
    __kmp_str_buf_free(&buf);
  }
}

#if KMP_USE_HWLOC

static void __kmp_affinity_print_hwloc_tp(AddrUnsPair *addrP, int len,
                                          int depth, int *levels) {
  int proc;
  kmp_str_buf_t buf;
  __kmp_str_buf_init(&buf);
  KMP_INFORM(OSProcToPhysicalThreadMap, "KMP_AFFINITY");
  for (proc = 0; proc < len; proc++) {
    __kmp_str_buf_print(&buf, "%s %d ", KMP_I18N_STR(Package),
                        addrP[proc].first.labels[0]);
    if (depth > 1) {
      int level = 1; // iterate over levels
      int label = 1; // iterate over labels
      if (__kmp_numa_detected)
        // node level follows package
        if (levels[level++] > 0)
          __kmp_str_buf_print(&buf, "%s %d ", KMP_I18N_STR(Node),
                              addrP[proc].first.labels[label++]);
      if (__kmp_tile_depth > 0)
        // tile level follows node if any, or package
        if (levels[level++] > 0)
          __kmp_str_buf_print(&buf, "%s %d ", KMP_I18N_STR(Tile),
                              addrP[proc].first.labels[label++]);
      if (levels[level++] > 0)
        // core level follows
        __kmp_str_buf_print(&buf, "%s %d ", KMP_I18N_STR(Core),
                            addrP[proc].first.labels[label++]);
      if (levels[level++] > 0)
        // thread level is the latest
        __kmp_str_buf_print(&buf, "%s %d ", KMP_I18N_STR(Thread),
                            addrP[proc].first.labels[label++]);
      KMP_DEBUG_ASSERT(label == depth);
    }
    KMP_INFORM(OSProcMapToPack, "KMP_AFFINITY", addrP[proc].second, buf.str);
    __kmp_str_buf_clear(&buf);
  }
  __kmp_str_buf_free(&buf);
}

static int nNodePerPkg, nTilePerPkg, nTilePerNode, nCorePerNode, nCorePerTile;

// This function removes the topology levels that are radix 1 and don't offer
// further information about the topology.  The most common example is when you
// have one thread context per core, we don't want the extra thread context
// level if it offers no unique labels.  So they are removed.
// return value: the new depth of address2os
static int __kmp_affinity_remove_radix_one_levels(AddrUnsPair *addrP, int nTh,
                                                  int depth, int *levels) {
  int level;
  int i;
  int radix1_detected;
  int new_depth = depth;
  for (level = depth - 1; level > 0; --level) {
    // Detect if this level is radix 1
    radix1_detected = 1;
    for (i = 1; i < nTh; ++i) {
      if (addrP[0].first.labels[level] != addrP[i].first.labels[level]) {
        // There are differing label values for this level so it stays
        radix1_detected = 0;
        break;
      }
    }
    if (!radix1_detected)
      continue;
    // Radix 1 was detected
    --new_depth;
    levels[level] = -1; // mark level as not present in address2os array
    if (level == new_depth) {
      // "turn off" deepest level, just decrement the depth that removes
      // the level from address2os array
      for (i = 0; i < nTh; ++i) {
        addrP[i].first.depth--;
      }
    } else {
      // For other levels, we move labels over and also reduce the depth
      int j;
      for (j = level; j < new_depth; ++j) {
        for (i = 0; i < nTh; ++i) {
          addrP[i].first.labels[j] = addrP[i].first.labels[j + 1];
          addrP[i].first.depth--;
        }
        levels[j + 1] -= 1;
      }
    }
  }
  return new_depth;
}

// Returns the number of objects of type 'type' below 'obj' within the topology
// tree structure. e.g., if obj is a HWLOC_OBJ_PACKAGE object, and type is
// HWLOC_OBJ_PU, then this will return the number of PU's under the SOCKET
// object.
static int __kmp_hwloc_get_nobjs_under_obj(hwloc_obj_t obj,
                                           hwloc_obj_type_t type) {
  int retval = 0;
  hwloc_obj_t first;
  for (first = hwloc_get_obj_below_by_type(__kmp_hwloc_topology, obj->type,
                                           obj->logical_index, type, 0);
       first != NULL &&
       hwloc_get_ancestor_obj_by_type(__kmp_hwloc_topology, obj->type, first) ==
           obj;
       first = hwloc_get_next_obj_by_type(__kmp_hwloc_topology, first->type,
                                          first)) {
    ++retval;
  }
  return retval;
}

static int __kmp_hwloc_count_children_by_depth(hwloc_topology_t t,
                                               hwloc_obj_t o, unsigned depth,
                                               hwloc_obj_t *f) {
  if (o->depth == depth) {
    if (*f == NULL)
      *f = o; // output first descendant found
    return 1;
  }
  int sum = 0;
  for (unsigned i = 0; i < o->arity; i++)
    sum += __kmp_hwloc_count_children_by_depth(t, o->children[i], depth, f);
  return sum; // will be 0 if no one found (as PU arity is 0)
}

static int __kmp_hwloc_count_children_by_type(hwloc_topology_t t, hwloc_obj_t o,
                                              hwloc_obj_type_t type,
                                              hwloc_obj_t *f) {
  if (!hwloc_compare_types(o->type, type)) {
    if (*f == NULL)
      *f = o; // output first descendant found
    return 1;
  }
  int sum = 0;
  for (unsigned i = 0; i < o->arity; i++)
    sum += __kmp_hwloc_count_children_by_type(t, o->children[i], type, f);
  return sum; // will be 0 if no one found (as PU arity is 0)
}

static int __kmp_hwloc_process_obj_core_pu(AddrUnsPair *addrPair,
                                           int &nActiveThreads,
                                           int &num_active_cores,
                                           hwloc_obj_t obj, int depth,
                                           int *labels) {
  hwloc_obj_t core = NULL;
  hwloc_topology_t &tp = __kmp_hwloc_topology;
  int NC = __kmp_hwloc_count_children_by_type(tp, obj, HWLOC_OBJ_CORE, &core);
  for (int core_id = 0; core_id < NC; ++core_id, core = core->next_cousin) {
    hwloc_obj_t pu = NULL;
    KMP_DEBUG_ASSERT(core != NULL);
    int num_active_threads = 0;
    int NT = __kmp_hwloc_count_children_by_type(tp, core, HWLOC_OBJ_PU, &pu);
    // int NT = core->arity; pu = core->first_child; // faster?
    for (int pu_id = 0; pu_id < NT; ++pu_id, pu = pu->next_cousin) {
      KMP_DEBUG_ASSERT(pu != NULL);
      if (!KMP_CPU_ISSET(pu->os_index, __kmp_affin_fullMask))
        continue; // skip inactive (inaccessible) unit
      Address addr(depth + 2);
      KA_TRACE(20, ("Hwloc inserting %d (%d) %d (%d) %d (%d) into address2os\n",
                    obj->os_index, obj->logical_index, core->os_index,
                    core->logical_index, pu->os_index, pu->logical_index));
      for (int i = 0; i < depth; ++i)
        addr.labels[i] = labels[i]; // package, etc.
      addr.labels[depth] = core_id; // core
      addr.labels[depth + 1] = pu_id; // pu
      addrPair[nActiveThreads] = AddrUnsPair(addr, pu->os_index);
      __kmp_pu_os_idx[nActiveThreads] = pu->os_index;
      nActiveThreads++;
      ++num_active_threads; // count active threads per core
    }
    if (num_active_threads) { // were there any active threads on the core?
      ++__kmp_ncores; // count total active cores
      ++num_active_cores; // count active cores per socket
      if (num_active_threads > __kmp_nThreadsPerCore)
        __kmp_nThreadsPerCore = num_active_threads; // calc maximum
    }
  }
  return 0;
}

// Check if NUMA node detected below the package,
// and if tile object is detected and return its depth
static int __kmp_hwloc_check_numa() {
  hwloc_topology_t &tp = __kmp_hwloc_topology;
  hwloc_obj_t hT, hC, hL, hN, hS; // hwloc objects (pointers to)
  int depth;

  // Get some PU
  hT = hwloc_get_obj_by_type(tp, HWLOC_OBJ_PU, 0);
  if (hT == NULL) // something has gone wrong
    return 1;

  // check NUMA node below PACKAGE
  hN = hwloc_get_ancestor_obj_by_type(tp, HWLOC_OBJ_NUMANODE, hT);
  hS = hwloc_get_ancestor_obj_by_type(tp, HWLOC_OBJ_PACKAGE, hT);
  KMP_DEBUG_ASSERT(hS != NULL);
  if (hN != NULL && hN->depth > hS->depth) {
    __kmp_numa_detected = TRUE; // socket includes node(s)
    if (__kmp_affinity_gran == affinity_gran_node) {
      __kmp_affinity_gran == affinity_gran_numa;
    }
  }

  // check tile, get object by depth because of multiple caches possible
  depth = hwloc_get_cache_type_depth(tp, 2, HWLOC_OBJ_CACHE_UNIFIED);
  hL = hwloc_get_ancestor_obj_by_depth(tp, depth, hT);
  hC = NULL; // not used, but reset it here just in case
  if (hL != NULL &&
      __kmp_hwloc_count_children_by_type(tp, hL, HWLOC_OBJ_CORE, &hC) > 1)
    __kmp_tile_depth = depth; // tile consists of multiple cores
  return 0;
}

static int __kmp_affinity_create_hwloc_map(AddrUnsPair **address2os,
                                           kmp_i18n_id_t *const msg_id) {
  hwloc_topology_t &tp = __kmp_hwloc_topology; // shortcut of a long name
  *address2os = NULL;
  *msg_id = kmp_i18n_null;

  // Save the affinity mask for the current thread.
  kmp_affin_mask_t *oldMask;
  KMP_CPU_ALLOC(oldMask);
  __kmp_get_system_affinity(oldMask, TRUE);
  __kmp_hwloc_check_numa();

  if (!KMP_AFFINITY_CAPABLE()) {
    // Hack to try and infer the machine topology using only the data
    // available from cpuid on the current thread, and __kmp_xproc.
    KMP_ASSERT(__kmp_affinity_type == affinity_none);

    nCoresPerPkg = __kmp_hwloc_get_nobjs_under_obj(
        hwloc_get_obj_by_type(tp, HWLOC_OBJ_PACKAGE, 0), HWLOC_OBJ_CORE);
    __kmp_nThreadsPerCore = __kmp_hwloc_get_nobjs_under_obj(
        hwloc_get_obj_by_type(tp, HWLOC_OBJ_CORE, 0), HWLOC_OBJ_PU);
    __kmp_ncores = __kmp_xproc / __kmp_nThreadsPerCore;
    nPackages = (__kmp_xproc + nCoresPerPkg - 1) / nCoresPerPkg;
    if (__kmp_affinity_verbose) {
      KMP_INFORM(AffNotCapableUseLocCpuidL11, "KMP_AFFINITY");
      KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
      if (__kmp_affinity_uniform_topology()) {
        KMP_INFORM(Uniform, "KMP_AFFINITY");
      } else {
        KMP_INFORM(NonUniform, "KMP_AFFINITY");
      }
      KMP_INFORM(Topology, "KMP_AFFINITY", nPackages, nCoresPerPkg,
                 __kmp_nThreadsPerCore, __kmp_ncores);
    }
    KMP_CPU_FREE(oldMask);
    return 0;
  }

  int depth = 3;
  int levels[5] = {0, 1, 2, 3, 4}; // package, [node,] [tile,] core, thread
  int labels[3] = {0}; // package [,node] [,tile] - head of lables array
  if (__kmp_numa_detected)
    ++depth;
  if (__kmp_tile_depth)
    ++depth;

  // Allocate the data structure to be returned.
  AddrUnsPair *retval =
      (AddrUnsPair *)__kmp_allocate(sizeof(AddrUnsPair) * __kmp_avail_proc);
  KMP_DEBUG_ASSERT(__kmp_pu_os_idx == NULL);
  __kmp_pu_os_idx = (int *)__kmp_allocate(sizeof(int) * __kmp_avail_proc);

  // When affinity is off, this routine will still be called to set
  // __kmp_ncores, as well as __kmp_nThreadsPerCore,
  // nCoresPerPkg, & nPackages.  Make sure all these vars are set
  // correctly, and return if affinity is not enabled.

  hwloc_obj_t socket, node, tile;
  int nActiveThreads = 0;
  int socket_id = 0;
  // re-calculate globals to count only accessible resources
  __kmp_ncores = nPackages = nCoresPerPkg = __kmp_nThreadsPerCore = 0;
  nNodePerPkg = nTilePerPkg = nTilePerNode = nCorePerNode = nCorePerTile = 0;
  for (socket = hwloc_get_obj_by_type(tp, HWLOC_OBJ_PACKAGE, 0); socket != NULL;
       socket = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PACKAGE, socket),
      socket_id++) {
    labels[0] = socket_id;
    if (__kmp_numa_detected) {
      int NN;
      int n_active_nodes = 0;
      node = NULL;
      NN = __kmp_hwloc_count_children_by_type(tp, socket, HWLOC_OBJ_NUMANODE,
                                              &node);
      for (int node_id = 0; node_id < NN; ++node_id, node = node->next_cousin) {
        labels[1] = node_id;
        if (__kmp_tile_depth) {
          // NUMA + tiles
          int NT;
          int n_active_tiles = 0;
          tile = NULL;
          NT = __kmp_hwloc_count_children_by_depth(tp, node, __kmp_tile_depth,
                                                   &tile);
          for (int tl_id = 0; tl_id < NT; ++tl_id, tile = tile->next_cousin) {
            labels[2] = tl_id;
            int n_active_cores = 0;
            __kmp_hwloc_process_obj_core_pu(retval, nActiveThreads,
                                            n_active_cores, tile, 3, labels);
            if (n_active_cores) { // were there any active cores on the socket?
              ++n_active_tiles; // count active tiles per node
              if (n_active_cores > nCorePerTile)
                nCorePerTile = n_active_cores; // calc maximum
            }
          }
          if (n_active_tiles) { // were there any active tiles on the socket?
            ++n_active_nodes; // count active nodes per package
            if (n_active_tiles > nTilePerNode)
              nTilePerNode = n_active_tiles; // calc maximum
          }
        } else {
          // NUMA, no tiles
          int n_active_cores = 0;
          __kmp_hwloc_process_obj_core_pu(retval, nActiveThreads,
                                          n_active_cores, node, 2, labels);
          if (n_active_cores) { // were there any active cores on the socket?
            ++n_active_nodes; // count active nodes per package
            if (n_active_cores > nCorePerNode)
              nCorePerNode = n_active_cores; // calc maximum
          }
        }
      }
      if (n_active_nodes) { // were there any active nodes on the socket?
        ++nPackages; // count total active packages
        if (n_active_nodes > nNodePerPkg)
          nNodePerPkg = n_active_nodes; // calc maximum
      }
    } else {
      if (__kmp_tile_depth) {
        // no NUMA, tiles
        int NT;
        int n_active_tiles = 0;
        tile = NULL;
        NT = __kmp_hwloc_count_children_by_depth(tp, socket, __kmp_tile_depth,
                                                 &tile);
        for (int tl_id = 0; tl_id < NT; ++tl_id, tile = tile->next_cousin) {
          labels[1] = tl_id;
          int n_active_cores = 0;
          __kmp_hwloc_process_obj_core_pu(retval, nActiveThreads,
                                          n_active_cores, tile, 2, labels);
          if (n_active_cores) { // were there any active cores on the socket?
            ++n_active_tiles; // count active tiles per package
            if (n_active_cores > nCorePerTile)
              nCorePerTile = n_active_cores; // calc maximum
          }
        }
        if (n_active_tiles) { // were there any active tiles on the socket?
          ++nPackages; // count total active packages
          if (n_active_tiles > nTilePerPkg)
            nTilePerPkg = n_active_tiles; // calc maximum
        }
      } else {
        // no NUMA, no tiles
        int n_active_cores = 0;
        __kmp_hwloc_process_obj_core_pu(retval, nActiveThreads, n_active_cores,
                                        socket, 1, labels);
        if (n_active_cores) { // were there any active cores on the socket?
          ++nPackages; // count total active packages
          if (n_active_cores > nCoresPerPkg)
            nCoresPerPkg = n_active_cores; // calc maximum
        }
      }
    }
  }

  // If there's only one thread context to bind to, return now.
  KMP_DEBUG_ASSERT(nActiveThreads == __kmp_avail_proc);
  KMP_ASSERT(nActiveThreads > 0);
  if (nActiveThreads == 1) {
    __kmp_ncores = nPackages = 1;
    __kmp_nThreadsPerCore = nCoresPerPkg = 1;
    if (__kmp_affinity_verbose) {
      char buf[KMP_AFFIN_MASK_PRINT_LEN];
      __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN, oldMask);

      KMP_INFORM(AffUsingHwloc, "KMP_AFFINITY");
      if (__kmp_affinity_respect_mask) {
        KMP_INFORM(InitOSProcSetRespect, "KMP_AFFINITY", buf);
      } else {
        KMP_INFORM(InitOSProcSetNotRespect, "KMP_AFFINITY", buf);
      }
      KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
      KMP_INFORM(Uniform, "KMP_AFFINITY");
      KMP_INFORM(Topology, "KMP_AFFINITY", nPackages, nCoresPerPkg,
                 __kmp_nThreadsPerCore, __kmp_ncores);
    }

    if (__kmp_affinity_type == affinity_none) {
      __kmp_free(retval);
      KMP_CPU_FREE(oldMask);
      return 0;
    }

    // Form an Address object which only includes the package level.
    Address addr(1);
    addr.labels[0] = retval[0].first.labels[0];
    retval[0].first = addr;

    if (__kmp_affinity_gran_levels < 0) {
      __kmp_affinity_gran_levels = 0;
    }

    if (__kmp_affinity_verbose) {
      __kmp_affinity_print_topology(retval, 1, 1, 0, -1, -1);
    }

    *address2os = retval;
    KMP_CPU_FREE(oldMask);
    return 1;
  }

  // Sort the table by physical Id.
  qsort(retval, nActiveThreads, sizeof(*retval),
        __kmp_affinity_cmp_Address_labels);

  // Check to see if the machine topology is uniform
  int nPUs = nPackages * __kmp_nThreadsPerCore;
  if (__kmp_numa_detected) {
    if (__kmp_tile_depth) { // NUMA + tiles
      nPUs *= (nNodePerPkg * nTilePerNode * nCorePerTile);
    } else { // NUMA, no tiles
      nPUs *= (nNodePerPkg * nCorePerNode);
    }
  } else {
    if (__kmp_tile_depth) { // no NUMA, tiles
      nPUs *= (nTilePerPkg * nCorePerTile);
    } else { // no NUMA, no tiles
      nPUs *= nCoresPerPkg;
    }
  }
  unsigned uniform = (nPUs == nActiveThreads);

  // Print the machine topology summary.
  if (__kmp_affinity_verbose) {
    char mask[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(mask, KMP_AFFIN_MASK_PRINT_LEN, oldMask);
    if (__kmp_affinity_respect_mask) {
      KMP_INFORM(InitOSProcSetRespect, "KMP_AFFINITY", mask);
    } else {
      KMP_INFORM(InitOSProcSetNotRespect, "KMP_AFFINITY", mask);
    }
    KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
    if (uniform) {
      KMP_INFORM(Uniform, "KMP_AFFINITY");
    } else {
      KMP_INFORM(NonUniform, "KMP_AFFINITY");
    }
    if (__kmp_numa_detected) {
      if (__kmp_tile_depth) { // NUMA + tiles
        KMP_INFORM(TopologyExtraNoTi, "KMP_AFFINITY", nPackages, nNodePerPkg,
                   nTilePerNode, nCorePerTile, __kmp_nThreadsPerCore,
                   __kmp_ncores);
      } else { // NUMA, no tiles
        KMP_INFORM(TopologyExtraNode, "KMP_AFFINITY", nPackages, nNodePerPkg,
                   nCorePerNode, __kmp_nThreadsPerCore, __kmp_ncores);
        nPUs *= (nNodePerPkg * nCorePerNode);
      }
    } else {
      if (__kmp_tile_depth) { // no NUMA, tiles
        KMP_INFORM(TopologyExtraTile, "KMP_AFFINITY", nPackages, nTilePerPkg,
                   nCorePerTile, __kmp_nThreadsPerCore, __kmp_ncores);
      } else { // no NUMA, no tiles
        kmp_str_buf_t buf;
        __kmp_str_buf_init(&buf);
        __kmp_str_buf_print(&buf, "%d", nPackages);
        KMP_INFORM(TopologyExtra, "KMP_AFFINITY", buf.str, nCoresPerPkg,
                   __kmp_nThreadsPerCore, __kmp_ncores);
        __kmp_str_buf_free(&buf);
      }
    }
  }

  if (__kmp_affinity_type == affinity_none) {
    __kmp_free(retval);
    KMP_CPU_FREE(oldMask);
    return 0;
  }

  int depth_full = depth; // number of levels before compressing
  // Find any levels with radiix 1, and remove them from the map
  // (except for the package level).
  depth = __kmp_affinity_remove_radix_one_levels(retval, nActiveThreads, depth,
                                                 levels);
  KMP_DEBUG_ASSERT(__kmp_affinity_gran != affinity_gran_default);
  if (__kmp_affinity_gran_levels < 0) {
    // Set the granularity level based on what levels are modeled
    // in the machine topology map.
    __kmp_affinity_gran_levels = 0; // lowest level (e.g. fine)
    if (__kmp_affinity_gran > affinity_gran_thread) {
      for (int i = 1; i <= depth_full; ++i) {
        if (__kmp_affinity_gran <= i) // only count deeper levels
          break;
        if (levels[depth_full - i] > 0)
          __kmp_affinity_gran_levels++;
      }
    }
    if (__kmp_affinity_gran > affinity_gran_package)
      __kmp_affinity_gran_levels++; // e.g. granularity = group
  }

  if (__kmp_affinity_verbose)
    __kmp_affinity_print_hwloc_tp(retval, nActiveThreads, depth, levels);

  KMP_CPU_FREE(oldMask);
  *address2os = retval;
  return depth;
}
#endif // KMP_USE_HWLOC

// If we don't know how to retrieve the machine's processor topology, or
// encounter an error in doing so, this routine is called to form a "flat"
// mapping of os thread id's <-> processor id's.
static int __kmp_affinity_create_flat_map(AddrUnsPair **address2os,
                                          kmp_i18n_id_t *const msg_id) {
  *address2os = NULL;
  *msg_id = kmp_i18n_null;

  // Even if __kmp_affinity_type == affinity_none, this routine might still
  // called to set __kmp_ncores, as well as
  // __kmp_nThreadsPerCore, nCoresPerPkg, & nPackages.
  if (!KMP_AFFINITY_CAPABLE()) {
    KMP_ASSERT(__kmp_affinity_type == affinity_none);
    __kmp_ncores = nPackages = __kmp_xproc;
    __kmp_nThreadsPerCore = nCoresPerPkg = 1;
    if (__kmp_affinity_verbose) {
      KMP_INFORM(AffFlatTopology, "KMP_AFFINITY");
      KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
      KMP_INFORM(Uniform, "KMP_AFFINITY");
      KMP_INFORM(Topology, "KMP_AFFINITY", nPackages, nCoresPerPkg,
                 __kmp_nThreadsPerCore, __kmp_ncores);
    }
    return 0;
  }

  // When affinity is off, this routine will still be called to set
  // __kmp_ncores, as well as __kmp_nThreadsPerCore, nCoresPerPkg, & nPackages.
  // Make sure all these vars are set correctly, and return now if affinity is
  // not enabled.
  __kmp_ncores = nPackages = __kmp_avail_proc;
  __kmp_nThreadsPerCore = nCoresPerPkg = 1;
  if (__kmp_affinity_verbose) {
    char buf[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN,
                              __kmp_affin_fullMask);

    KMP_INFORM(AffCapableUseFlat, "KMP_AFFINITY");
    if (__kmp_affinity_respect_mask) {
      KMP_INFORM(InitOSProcSetRespect, "KMP_AFFINITY", buf);
    } else {
      KMP_INFORM(InitOSProcSetNotRespect, "KMP_AFFINITY", buf);
    }
    KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
    KMP_INFORM(Uniform, "KMP_AFFINITY");
    KMP_INFORM(Topology, "KMP_AFFINITY", nPackages, nCoresPerPkg,
               __kmp_nThreadsPerCore, __kmp_ncores);
  }
  KMP_DEBUG_ASSERT(__kmp_pu_os_idx == NULL);
  __kmp_pu_os_idx = (int *)__kmp_allocate(sizeof(int) * __kmp_avail_proc);
  if (__kmp_affinity_type == affinity_none) {
    int avail_ct = 0;
    int i;
    KMP_CPU_SET_ITERATE(i, __kmp_affin_fullMask) {
      if (!KMP_CPU_ISSET(i, __kmp_affin_fullMask))
        continue;
      __kmp_pu_os_idx[avail_ct++] = i; // suppose indices are flat
    }
    return 0;
  }

  // Contruct the data structure to be returned.
  *address2os =
      (AddrUnsPair *)__kmp_allocate(sizeof(**address2os) * __kmp_avail_proc);
  int avail_ct = 0;
  int i;
  KMP_CPU_SET_ITERATE(i, __kmp_affin_fullMask) {
    // Skip this proc if it is not included in the machine model.
    if (!KMP_CPU_ISSET(i, __kmp_affin_fullMask)) {
      continue;
    }
    __kmp_pu_os_idx[avail_ct] = i; // suppose indices are flat
    Address addr(1);
    addr.labels[0] = i;
    (*address2os)[avail_ct++] = AddrUnsPair(addr, i);
  }
  if (__kmp_affinity_verbose) {
    KMP_INFORM(OSProcToPackage, "KMP_AFFINITY");
  }

  if (__kmp_affinity_gran_levels < 0) {
    // Only the package level is modeled in the machine topology map,
    // so the #levels of granularity is either 0 or 1.
    if (__kmp_affinity_gran > affinity_gran_package) {
      __kmp_affinity_gran_levels = 1;
    } else {
      __kmp_affinity_gran_levels = 0;
    }
  }
  return 1;
}

#if KMP_GROUP_AFFINITY

// If multiple Windows* OS processor groups exist, we can create a 2-level
// topology map with the groups at level 0 and the individual procs at level 1.
// This facilitates letting the threads float among all procs in a group,
// if granularity=group (the default when there are multiple groups).
static int __kmp_affinity_create_proc_group_map(AddrUnsPair **address2os,
                                                kmp_i18n_id_t *const msg_id) {
  *address2os = NULL;
  *msg_id = kmp_i18n_null;

  // If we aren't affinity capable, then return now.
  // The flat mapping will be used.
  if (!KMP_AFFINITY_CAPABLE()) {
    // FIXME set *msg_id
    return -1;
  }

  // Contruct the data structure to be returned.
  *address2os =
      (AddrUnsPair *)__kmp_allocate(sizeof(**address2os) * __kmp_avail_proc);
  KMP_DEBUG_ASSERT(__kmp_pu_os_idx == NULL);
  __kmp_pu_os_idx = (int *)__kmp_allocate(sizeof(int) * __kmp_avail_proc);
  int avail_ct = 0;
  int i;
  KMP_CPU_SET_ITERATE(i, __kmp_affin_fullMask) {
    // Skip this proc if it is not included in the machine model.
    if (!KMP_CPU_ISSET(i, __kmp_affin_fullMask)) {
      continue;
    }
    __kmp_pu_os_idx[avail_ct] = i; // suppose indices are flat
    Address addr(2);
    addr.labels[0] = i / (CHAR_BIT * sizeof(DWORD_PTR));
    addr.labels[1] = i % (CHAR_BIT * sizeof(DWORD_PTR));
    (*address2os)[avail_ct++] = AddrUnsPair(addr, i);

    if (__kmp_affinity_verbose) {
      KMP_INFORM(AffOSProcToGroup, "KMP_AFFINITY", i, addr.labels[0],
                 addr.labels[1]);
    }
  }

  if (__kmp_affinity_gran_levels < 0) {
    if (__kmp_affinity_gran == affinity_gran_group) {
      __kmp_affinity_gran_levels = 1;
    } else if ((__kmp_affinity_gran == affinity_gran_fine) ||
               (__kmp_affinity_gran == affinity_gran_thread)) {
      __kmp_affinity_gran_levels = 0;
    } else {
      const char *gran_str = NULL;
      if (__kmp_affinity_gran == affinity_gran_core) {
        gran_str = "core";
      } else if (__kmp_affinity_gran == affinity_gran_package) {
        gran_str = "package";
      } else if (__kmp_affinity_gran == affinity_gran_node) {
        gran_str = "node";
      } else {
        KMP_ASSERT(0);
      }

      // Warning: can't use affinity granularity \"gran\" with group topology
      // method, using "thread"
      __kmp_affinity_gran_levels = 0;
    }
  }
  return 2;
}

#endif /* KMP_GROUP_AFFINITY */

#if KMP_ARCH_X86 || KMP_ARCH_X86_64

static int __kmp_cpuid_mask_width(int count) {
  int r = 0;

  while ((1 << r) < count)
    ++r;
  return r;
}

class apicThreadInfo {
public:
  unsigned osId; // param to __kmp_affinity_bind_thread
  unsigned apicId; // from cpuid after binding
  unsigned maxCoresPerPkg; //      ""
  unsigned maxThreadsPerPkg; //      ""
  unsigned pkgId; // inferred from above values
  unsigned coreId; //      ""
  unsigned threadId; //      ""
};

static int __kmp_affinity_cmp_apicThreadInfo_phys_id(const void *a,
                                                     const void *b) {
  const apicThreadInfo *aa = (const apicThreadInfo *)a;
  const apicThreadInfo *bb = (const apicThreadInfo *)b;
  if (aa->pkgId < bb->pkgId)
    return -1;
  if (aa->pkgId > bb->pkgId)
    return 1;
  if (aa->coreId < bb->coreId)
    return -1;
  if (aa->coreId > bb->coreId)
    return 1;
  if (aa->threadId < bb->threadId)
    return -1;
  if (aa->threadId > bb->threadId)
    return 1;
  return 0;
}

// On IA-32 architecture and Intel(R) 64 architecture, we attempt to use
// an algorithm which cycles through the available os threads, setting
// the current thread's affinity mask to that thread, and then retrieves
// the Apic Id for each thread context using the cpuid instruction.
static int __kmp_affinity_create_apicid_map(AddrUnsPair **address2os,
                                            kmp_i18n_id_t *const msg_id) {
  kmp_cpuid buf;
  *address2os = NULL;
  *msg_id = kmp_i18n_null;

  // Check if cpuid leaf 4 is supported.
  __kmp_x86_cpuid(0, 0, &buf);
  if (buf.eax < 4) {
    *msg_id = kmp_i18n_str_NoLeaf4Support;
    return -1;
  }

  // The algorithm used starts by setting the affinity to each available thread
  // and retrieving info from the cpuid instruction, so if we are not capable of
  // calling __kmp_get_system_affinity() and _kmp_get_system_affinity(), then we
  // need to do something else - use the defaults that we calculated from
  // issuing cpuid without binding to each proc.
  if (!KMP_AFFINITY_CAPABLE()) {
    // Hack to try and infer the machine topology using only the data
    // available from cpuid on the current thread, and __kmp_xproc.
    KMP_ASSERT(__kmp_affinity_type == affinity_none);

    // Get an upper bound on the number of threads per package using cpuid(1).
    // On some OS/chps combinations where HT is supported by the chip but is
    // disabled, this value will be 2 on a single core chip. Usually, it will be
    // 2 if HT is enabled and 1 if HT is disabled.
    __kmp_x86_cpuid(1, 0, &buf);
    int maxThreadsPerPkg = (buf.ebx >> 16) & 0xff;
    if (maxThreadsPerPkg == 0) {
      maxThreadsPerPkg = 1;
    }

    // The num cores per pkg comes from cpuid(4). 1 must be added to the encoded
    // value.
    //
    // The author of cpu_count.cpp treated this only an upper bound on the
    // number of cores, but I haven't seen any cases where it was greater than
    // the actual number of cores, so we will treat it as exact in this block of
    // code.
    //
    // First, we need to check if cpuid(4) is supported on this chip. To see if
    // cpuid(n) is supported, issue cpuid(0) and check if eax has the value n or
    // greater.
    __kmp_x86_cpuid(0, 0, &buf);
    if (buf.eax >= 4) {
      __kmp_x86_cpuid(4, 0, &buf);
      nCoresPerPkg = ((buf.eax >> 26) & 0x3f) + 1;
    } else {
      nCoresPerPkg = 1;
    }

    // There is no way to reliably tell if HT is enabled without issuing the
    // cpuid instruction from every thread, can correlating the cpuid info, so
    // if the machine is not affinity capable, we assume that HT is off. We have
    // seen quite a few machines where maxThreadsPerPkg is 2, yet the machine
    // does not support HT.
    //
    // - Older OSes are usually found on machines with older chips, which do not
    //   support HT.
    // - The performance penalty for mistakenly identifying a machine as HT when
    //   it isn't (which results in blocktime being incorrecly set to 0) is
    //   greater than the penalty when for mistakenly identifying a machine as
    //   being 1 thread/core when it is really HT enabled (which results in
    //   blocktime being incorrectly set to a positive value).
    __kmp_ncores = __kmp_xproc;
    nPackages = (__kmp_xproc + nCoresPerPkg - 1) / nCoresPerPkg;
    __kmp_nThreadsPerCore = 1;
    if (__kmp_affinity_verbose) {
      KMP_INFORM(AffNotCapableUseLocCpuid, "KMP_AFFINITY");
      KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
      if (__kmp_affinity_uniform_topology()) {
        KMP_INFORM(Uniform, "KMP_AFFINITY");
      } else {
        KMP_INFORM(NonUniform, "KMP_AFFINITY");
      }
      KMP_INFORM(Topology, "KMP_AFFINITY", nPackages, nCoresPerPkg,
                 __kmp_nThreadsPerCore, __kmp_ncores);
    }
    return 0;
  }

  // From here on, we can assume that it is safe to call
  // __kmp_get_system_affinity() and __kmp_set_system_affinity(), even if
  // __kmp_affinity_type = affinity_none.

  // Save the affinity mask for the current thread.
  kmp_affin_mask_t *oldMask;
  KMP_CPU_ALLOC(oldMask);
  KMP_ASSERT(oldMask != NULL);
  __kmp_get_system_affinity(oldMask, TRUE);

  // Run through each of the available contexts, binding the current thread
  // to it, and obtaining the pertinent information using the cpuid instr.
  //
  // The relevant information is:
  // - Apic Id: Bits 24:31 of ebx after issuing cpuid(1) - each thread context
  //     has a uniqie Apic Id, which is of the form pkg# : core# : thread#.
  // - Max Threads Per Pkg: Bits 16:23 of ebx after issuing cpuid(1). The value
  //     of this field determines the width of the core# + thread# fields in the
  //     Apic Id. It is also an upper bound on the number of threads per
  //     package, but it has been verified that situations happen were it is not
  //     exact. In particular, on certain OS/chip combinations where Intel(R)
  //     Hyper-Threading Technology is supported by the chip but has been
  //     disabled, the value of this field will be 2 (for a single core chip).
  //     On other OS/chip combinations supporting Intel(R) Hyper-Threading
  //     Technology, the value of this field will be 1 when Intel(R)
  //     Hyper-Threading Technology is disabled and 2 when it is enabled.
  // - Max Cores Per Pkg:  Bits 26:31 of eax after issuing cpuid(4). The value
  //     of this field (+1) determines the width of the core# field in the Apic
  //     Id. The comments in "cpucount.cpp" say that this value is an upper
  //     bound, but the IA-32 architecture manual says that it is exactly the
  //     number of cores per package, and I haven't seen any case where it
  //     wasn't.
  //
  // From this information, deduce the package Id, core Id, and thread Id,
  // and set the corresponding fields in the apicThreadInfo struct.
  unsigned i;
  apicThreadInfo *threadInfo = (apicThreadInfo *)__kmp_allocate(
      __kmp_avail_proc * sizeof(apicThreadInfo));
  unsigned nApics = 0;
  KMP_CPU_SET_ITERATE(i, __kmp_affin_fullMask) {
    // Skip this proc if it is not included in the machine model.
    if (!KMP_CPU_ISSET(i, __kmp_affin_fullMask)) {
      continue;
    }
    KMP_DEBUG_ASSERT((int)nApics < __kmp_avail_proc);

    __kmp_affinity_dispatch->bind_thread(i);
    threadInfo[nApics].osId = i;

    // The apic id and max threads per pkg come from cpuid(1).
    __kmp_x86_cpuid(1, 0, &buf);
    if (((buf.edx >> 9) & 1) == 0) {
      __kmp_set_system_affinity(oldMask, TRUE);
      __kmp_free(threadInfo);
      KMP_CPU_FREE(oldMask);
      *msg_id = kmp_i18n_str_ApicNotPresent;
      return -1;
    }
    threadInfo[nApics].apicId = (buf.ebx >> 24) & 0xff;
    threadInfo[nApics].maxThreadsPerPkg = (buf.ebx >> 16) & 0xff;
    if (threadInfo[nApics].maxThreadsPerPkg == 0) {
      threadInfo[nApics].maxThreadsPerPkg = 1;
    }

    // Max cores per pkg comes from cpuid(4). 1 must be added to the encoded
    // value.
    //
    // First, we need to check if cpuid(4) is supported on this chip. To see if
    // cpuid(n) is supported, issue cpuid(0) and check if eax has the value n
    // or greater.
    __kmp_x86_cpuid(0, 0, &buf);
    if (buf.eax >= 4) {
      __kmp_x86_cpuid(4, 0, &buf);
      threadInfo[nApics].maxCoresPerPkg = ((buf.eax >> 26) & 0x3f) + 1;
    } else {
      threadInfo[nApics].maxCoresPerPkg = 1;
    }

    // Infer the pkgId / coreId / threadId using only the info obtained locally.
    int widthCT = __kmp_cpuid_mask_width(threadInfo[nApics].maxThreadsPerPkg);
    threadInfo[nApics].pkgId = threadInfo[nApics].apicId >> widthCT;

    int widthC = __kmp_cpuid_mask_width(threadInfo[nApics].maxCoresPerPkg);
    int widthT = widthCT - widthC;
    if (widthT < 0) {
      // I've never seen this one happen, but I suppose it could, if the cpuid
      // instruction on a chip was really screwed up. Make sure to restore the
      // affinity mask before the tail call.
      __kmp_set_system_affinity(oldMask, TRUE);
      __kmp_free(threadInfo);
      KMP_CPU_FREE(oldMask);
      *msg_id = kmp_i18n_str_InvalidCpuidInfo;
      return -1;
    }

    int maskC = (1 << widthC) - 1;
    threadInfo[nApics].coreId = (threadInfo[nApics].apicId >> widthT) & maskC;

    int maskT = (1 << widthT) - 1;
    threadInfo[nApics].threadId = threadInfo[nApics].apicId & maskT;

    nApics++;
  }

  // We've collected all the info we need.
  // Restore the old affinity mask for this thread.
  __kmp_set_system_affinity(oldMask, TRUE);

  // If there's only one thread context to bind to, form an Address object
  // with depth 1 and return immediately (or, if affinity is off, set
  // address2os to NULL and return).
  //
  // If it is configured to omit the package level when there is only a single
  // package, the logic at the end of this routine won't work if there is only
  // a single thread - it would try to form an Address object with depth 0.
  KMP_ASSERT(nApics > 0);
  if (nApics == 1) {
    __kmp_ncores = nPackages = 1;
    __kmp_nThreadsPerCore = nCoresPerPkg = 1;
    if (__kmp_affinity_verbose) {
      char buf[KMP_AFFIN_MASK_PRINT_LEN];
      __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN, oldMask);

      KMP_INFORM(AffUseGlobCpuid, "KMP_AFFINITY");
      if (__kmp_affinity_respect_mask) {
        KMP_INFORM(InitOSProcSetRespect, "KMP_AFFINITY", buf);
      } else {
        KMP_INFORM(InitOSProcSetNotRespect, "KMP_AFFINITY", buf);
      }
      KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
      KMP_INFORM(Uniform, "KMP_AFFINITY");
      KMP_INFORM(Topology, "KMP_AFFINITY", nPackages, nCoresPerPkg,
                 __kmp_nThreadsPerCore, __kmp_ncores);
    }

    if (__kmp_affinity_type == affinity_none) {
      __kmp_free(threadInfo);
      KMP_CPU_FREE(oldMask);
      return 0;
    }

    *address2os = (AddrUnsPair *)__kmp_allocate(sizeof(AddrUnsPair));
    Address addr(1);
    addr.labels[0] = threadInfo[0].pkgId;
    (*address2os)[0] = AddrUnsPair(addr, threadInfo[0].osId);

    if (__kmp_affinity_gran_levels < 0) {
      __kmp_affinity_gran_levels = 0;
    }

    if (__kmp_affinity_verbose) {
      __kmp_affinity_print_topology(*address2os, 1, 1, 0, -1, -1);
    }

    __kmp_free(threadInfo);
    KMP_CPU_FREE(oldMask);
    return 1;
  }

  // Sort the threadInfo table by physical Id.
  qsort(threadInfo, nApics, sizeof(*threadInfo),
        __kmp_affinity_cmp_apicThreadInfo_phys_id);

  // The table is now sorted by pkgId / coreId / threadId, but we really don't
  // know the radix of any of the fields. pkgId's may be sparsely assigned among
  // the chips on a system. Although coreId's are usually assigned
  // [0 .. coresPerPkg-1] and threadId's are usually assigned
  // [0..threadsPerCore-1], we don't want to make any such assumptions.
  //
  // For that matter, we don't know what coresPerPkg and threadsPerCore (or the
  // total # packages) are at this point - we want to determine that now. We
  // only have an upper bound on the first two figures.
  //
  // We also perform a consistency check at this point: the values returned by
  // the cpuid instruction for any thread bound to a given package had better
  // return the same info for maxThreadsPerPkg and maxCoresPerPkg.
  nPackages = 1;
  nCoresPerPkg = 1;
  __kmp_nThreadsPerCore = 1;
  unsigned nCores = 1;

  unsigned pkgCt = 1; // to determine radii
  unsigned lastPkgId = threadInfo[0].pkgId;
  unsigned coreCt = 1;
  unsigned lastCoreId = threadInfo[0].coreId;
  unsigned threadCt = 1;
  unsigned lastThreadId = threadInfo[0].threadId;

  // intra-pkg consist checks
  unsigned prevMaxCoresPerPkg = threadInfo[0].maxCoresPerPkg;
  unsigned prevMaxThreadsPerPkg = threadInfo[0].maxThreadsPerPkg;

  for (i = 1; i < nApics; i++) {
    if (threadInfo[i].pkgId != lastPkgId) {
      nCores++;
      pkgCt++;
      lastPkgId = threadInfo[i].pkgId;
      if ((int)coreCt > nCoresPerPkg)
        nCoresPerPkg = coreCt;
      coreCt = 1;
      lastCoreId = threadInfo[i].coreId;
      if ((int)threadCt > __kmp_nThreadsPerCore)
        __kmp_nThreadsPerCore = threadCt;
      threadCt = 1;
      lastThreadId = threadInfo[i].threadId;

      // This is a different package, so go on to the next iteration without
      // doing any consistency checks. Reset the consistency check vars, though.
      prevMaxCoresPerPkg = threadInfo[i].maxCoresPerPkg;
      prevMaxThreadsPerPkg = threadInfo[i].maxThreadsPerPkg;
      continue;
    }

    if (threadInfo[i].coreId != lastCoreId) {
      nCores++;
      coreCt++;
      lastCoreId = threadInfo[i].coreId;
      if ((int)threadCt > __kmp_nThreadsPerCore)
        __kmp_nThreadsPerCore = threadCt;
      threadCt = 1;
      lastThreadId = threadInfo[i].threadId;
    } else if (threadInfo[i].threadId != lastThreadId) {
      threadCt++;
      lastThreadId = threadInfo[i].threadId;
    } else {
      __kmp_free(threadInfo);
      KMP_CPU_FREE(oldMask);
      *msg_id = kmp_i18n_str_LegacyApicIDsNotUnique;
      return -1;
    }

    // Check to make certain that the maxCoresPerPkg and maxThreadsPerPkg
    // fields agree between all the threads bounds to a given package.
    if ((prevMaxCoresPerPkg != threadInfo[i].maxCoresPerPkg) ||
        (prevMaxThreadsPerPkg != threadInfo[i].maxThreadsPerPkg)) {
      __kmp_free(threadInfo);
      KMP_CPU_FREE(oldMask);
      *msg_id = kmp_i18n_str_InconsistentCpuidInfo;
      return -1;
    }
  }
  nPackages = pkgCt;
  if ((int)coreCt > nCoresPerPkg)
    nCoresPerPkg = coreCt;
  if ((int)threadCt > __kmp_nThreadsPerCore)
    __kmp_nThreadsPerCore = threadCt;

  // When affinity is off, this routine will still be called to set
  // __kmp_ncores, as well as __kmp_nThreadsPerCore, nCoresPerPkg, & nPackages.
  // Make sure all these vars are set correctly, and return now if affinity is
  // not enabled.
  __kmp_ncores = nCores;
  if (__kmp_affinity_verbose) {
    char buf[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN, oldMask);

    KMP_INFORM(AffUseGlobCpuid, "KMP_AFFINITY");
    if (__kmp_affinity_respect_mask) {
      KMP_INFORM(InitOSProcSetRespect, "KMP_AFFINITY", buf);
    } else {
      KMP_INFORM(InitOSProcSetNotRespect, "KMP_AFFINITY", buf);
    }
    KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
    if (__kmp_affinity_uniform_topology()) {
      KMP_INFORM(Uniform, "KMP_AFFINITY");
    } else {
      KMP_INFORM(NonUniform, "KMP_AFFINITY");
    }
    KMP_INFORM(Topology, "KMP_AFFINITY", nPackages, nCoresPerPkg,
               __kmp_nThreadsPerCore, __kmp_ncores);
  }
  KMP_DEBUG_ASSERT(__kmp_pu_os_idx == NULL);
  KMP_DEBUG_ASSERT(nApics == (unsigned)__kmp_avail_proc);
  __kmp_pu_os_idx = (int *)__kmp_allocate(sizeof(int) * __kmp_avail_proc);
  for (i = 0; i < nApics; ++i) {
    __kmp_pu_os_idx[i] = threadInfo[i].osId;
  }
  if (__kmp_affinity_type == affinity_none) {
    __kmp_free(threadInfo);
    KMP_CPU_FREE(oldMask);
    return 0;
  }

  // Now that we've determined the number of packages, the number of cores per
  // package, and the number of threads per core, we can construct the data
  // structure that is to be returned.
  int pkgLevel = 0;
  int coreLevel = (nCoresPerPkg <= 1) ? -1 : 1;
  int threadLevel =
      (__kmp_nThreadsPerCore <= 1) ? -1 : ((coreLevel >= 0) ? 2 : 1);
  unsigned depth = (pkgLevel >= 0) + (coreLevel >= 0) + (threadLevel >= 0);

  KMP_ASSERT(depth > 0);
  *address2os = (AddrUnsPair *)__kmp_allocate(sizeof(AddrUnsPair) * nApics);

  for (i = 0; i < nApics; ++i) {
    Address addr(depth);
    unsigned os = threadInfo[i].osId;
    int d = 0;

    if (pkgLevel >= 0) {
      addr.labels[d++] = threadInfo[i].pkgId;
    }
    if (coreLevel >= 0) {
      addr.labels[d++] = threadInfo[i].coreId;
    }
    if (threadLevel >= 0) {
      addr.labels[d++] = threadInfo[i].threadId;
    }
    (*address2os)[i] = AddrUnsPair(addr, os);
  }

  if (__kmp_affinity_gran_levels < 0) {
    // Set the granularity level based on what levels are modeled in the machine
    // topology map.
    __kmp_affinity_gran_levels = 0;
    if ((threadLevel >= 0) && (__kmp_affinity_gran > affinity_gran_thread)) {
      __kmp_affinity_gran_levels++;
    }
    if ((coreLevel >= 0) && (__kmp_affinity_gran > affinity_gran_core)) {
      __kmp_affinity_gran_levels++;
    }
    if ((pkgLevel >= 0) && (__kmp_affinity_gran > affinity_gran_package)) {
      __kmp_affinity_gran_levels++;
    }
  }

  if (__kmp_affinity_verbose) {
    __kmp_affinity_print_topology(*address2os, nApics, depth, pkgLevel,
                                  coreLevel, threadLevel);
  }

  __kmp_free(threadInfo);
  KMP_CPU_FREE(oldMask);
  return depth;
}

// Intel(R) microarchitecture code name Nehalem, Dunnington and later
// architectures support a newer interface for specifying the x2APIC Ids,
// based on cpuid leaf 11.
static int __kmp_affinity_create_x2apicid_map(AddrUnsPair **address2os,
                                              kmp_i18n_id_t *const msg_id) {
  kmp_cpuid buf;
  *address2os = NULL;
  *msg_id = kmp_i18n_null;

  // Check to see if cpuid leaf 11 is supported.
  __kmp_x86_cpuid(0, 0, &buf);
  if (buf.eax < 11) {
    *msg_id = kmp_i18n_str_NoLeaf11Support;
    return -1;
  }
  __kmp_x86_cpuid(11, 0, &buf);
  if (buf.ebx == 0) {
    *msg_id = kmp_i18n_str_NoLeaf11Support;
    return -1;
  }

  // Find the number of levels in the machine topology. While we're at it, get
  // the default values for __kmp_nThreadsPerCore & nCoresPerPkg. We will try to
  // get more accurate values later by explicitly counting them, but get
  // reasonable defaults now, in case we return early.
  int level;
  int threadLevel = -1;
  int coreLevel = -1;
  int pkgLevel = -1;
  __kmp_nThreadsPerCore = nCoresPerPkg = nPackages = 1;

  for (level = 0;; level++) {
    if (level > 31) {
      // FIXME: Hack for DPD200163180
      //
      // If level is big then something went wrong -> exiting
      //
      // There could actually be 32 valid levels in the machine topology, but so
      // far, the only machine we have seen which does not exit this loop before
      // iteration 32 has fubar x2APIC settings.
      //
      // For now, just reject this case based upon loop trip count.
      *msg_id = kmp_i18n_str_InvalidCpuidInfo;
      return -1;
    }
    __kmp_x86_cpuid(11, level, &buf);
    if (buf.ebx == 0) {
      if (pkgLevel < 0) {
        // Will infer nPackages from __kmp_xproc
        pkgLevel = level;
        level++;
      }
      break;
    }
    int kind = (buf.ecx >> 8) & 0xff;
    if (kind == 1) {
      // SMT level
      threadLevel = level;
      coreLevel = -1;
      pkgLevel = -1;
      __kmp_nThreadsPerCore = buf.ebx & 0xffff;
      if (__kmp_nThreadsPerCore == 0) {
        *msg_id = kmp_i18n_str_InvalidCpuidInfo;
        return -1;
      }
    } else if (kind == 2) {
      // core level
      coreLevel = level;
      pkgLevel = -1;
      nCoresPerPkg = buf.ebx & 0xffff;
      if (nCoresPerPkg == 0) {
        *msg_id = kmp_i18n_str_InvalidCpuidInfo;
        return -1;
      }
    } else {
      if (level <= 0) {
        *msg_id = kmp_i18n_str_InvalidCpuidInfo;
        return -1;
      }
      if (pkgLevel >= 0) {
        continue;
      }
      pkgLevel = level;
      nPackages = buf.ebx & 0xffff;
      if (nPackages == 0) {
        *msg_id = kmp_i18n_str_InvalidCpuidInfo;
        return -1;
      }
    }
  }
  int depth = level;

  // In the above loop, "level" was counted from the finest level (usually
  // thread) to the coarsest.  The caller expects that we will place the labels
  // in (*address2os)[].first.labels[] in the inverse order, so we need to
  // invert the vars saying which level means what.
  if (threadLevel >= 0) {
    threadLevel = depth - threadLevel - 1;
  }
  if (coreLevel >= 0) {
    coreLevel = depth - coreLevel - 1;
  }
  KMP_DEBUG_ASSERT(pkgLevel >= 0);
  pkgLevel = depth - pkgLevel - 1;

  // The algorithm used starts by setting the affinity to each available thread
  // and retrieving info from the cpuid instruction, so if we are not capable of
  // calling __kmp_get_system_affinity() and _kmp_get_system_affinity(), then we
  // need to do something else - use the defaults that we calculated from
  // issuing cpuid without binding to each proc.
  if (!KMP_AFFINITY_CAPABLE()) {
    // Hack to try and infer the machine topology using only the data
    // available from cpuid on the current thread, and __kmp_xproc.
    KMP_ASSERT(__kmp_affinity_type == affinity_none);

    __kmp_ncores = __kmp_xproc / __kmp_nThreadsPerCore;
    nPackages = (__kmp_xproc + nCoresPerPkg - 1) / nCoresPerPkg;
    if (__kmp_affinity_verbose) {
      KMP_INFORM(AffNotCapableUseLocCpuidL11, "KMP_AFFINITY");
      KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
      if (__kmp_affinity_uniform_topology()) {
        KMP_INFORM(Uniform, "KMP_AFFINITY");
      } else {
        KMP_INFORM(NonUniform, "KMP_AFFINITY");
      }
      KMP_INFORM(Topology, "KMP_AFFINITY", nPackages, nCoresPerPkg,
                 __kmp_nThreadsPerCore, __kmp_ncores);
    }
    return 0;
  }

  // From here on, we can assume that it is safe to call
  // __kmp_get_system_affinity() and __kmp_set_system_affinity(), even if
  // __kmp_affinity_type = affinity_none.

  // Save the affinity mask for the current thread.
  kmp_affin_mask_t *oldMask;
  KMP_CPU_ALLOC(oldMask);
  __kmp_get_system_affinity(oldMask, TRUE);

  // Allocate the data structure to be returned.
  AddrUnsPair *retval =
      (AddrUnsPair *)__kmp_allocate(sizeof(AddrUnsPair) * __kmp_avail_proc);

  // Run through each of the available contexts, binding the current thread
  // to it, and obtaining the pertinent information using the cpuid instr.
  unsigned int proc;
  int nApics = 0;
  KMP_CPU_SET_ITERATE(proc, __kmp_affin_fullMask) {
    // Skip this proc if it is not included in the machine model.
    if (!KMP_CPU_ISSET(proc, __kmp_affin_fullMask)) {
      continue;
    }
    KMP_DEBUG_ASSERT(nApics < __kmp_avail_proc);

    __kmp_affinity_dispatch->bind_thread(proc);

    // Extract labels for each level in the machine topology map from Apic ID.
    Address addr(depth);
    int prev_shift = 0;

    for (level = 0; level < depth; level++) {
      __kmp_x86_cpuid(11, level, &buf);
      unsigned apicId = buf.edx;
      if (buf.ebx == 0) {
        if (level != depth - 1) {
          KMP_CPU_FREE(oldMask);
          *msg_id = kmp_i18n_str_InconsistentCpuidInfo;
          return -1;
        }
        addr.labels[depth - level - 1] = apicId >> prev_shift;
        level++;
        break;
      }
      int shift = buf.eax & 0x1f;
      int mask = (1 << shift) - 1;
      addr.labels[depth - level - 1] = (apicId & mask) >> prev_shift;
      prev_shift = shift;
    }
    if (level != depth) {
      KMP_CPU_FREE(oldMask);
      *msg_id = kmp_i18n_str_InconsistentCpuidInfo;
      return -1;
    }

    retval[nApics] = AddrUnsPair(addr, proc);
    nApics++;
  }

  // We've collected all the info we need.
  // Restore the old affinity mask for this thread.
  __kmp_set_system_affinity(oldMask, TRUE);

  // If there's only one thread context to bind to, return now.
  KMP_ASSERT(nApics > 0);
  if (nApics == 1) {
    __kmp_ncores = nPackages = 1;
    __kmp_nThreadsPerCore = nCoresPerPkg = 1;
    if (__kmp_affinity_verbose) {
      char buf[KMP_AFFIN_MASK_PRINT_LEN];
      __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN, oldMask);

      KMP_INFORM(AffUseGlobCpuidL11, "KMP_AFFINITY");
      if (__kmp_affinity_respect_mask) {
        KMP_INFORM(InitOSProcSetRespect, "KMP_AFFINITY", buf);
      } else {
        KMP_INFORM(InitOSProcSetNotRespect, "KMP_AFFINITY", buf);
      }
      KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
      KMP_INFORM(Uniform, "KMP_AFFINITY");
      KMP_INFORM(Topology, "KMP_AFFINITY", nPackages, nCoresPerPkg,
                 __kmp_nThreadsPerCore, __kmp_ncores);
    }

    if (__kmp_affinity_type == affinity_none) {
      __kmp_free(retval);
      KMP_CPU_FREE(oldMask);
      return 0;
    }

    // Form an Address object which only includes the package level.
    Address addr(1);
    addr.labels[0] = retval[0].first.labels[pkgLevel];
    retval[0].first = addr;

    if (__kmp_affinity_gran_levels < 0) {
      __kmp_affinity_gran_levels = 0;
    }

    if (__kmp_affinity_verbose) {
      __kmp_affinity_print_topology(retval, 1, 1, 0, -1, -1);
    }

    *address2os = retval;
    KMP_CPU_FREE(oldMask);
    return 1;
  }

  // Sort the table by physical Id.
  qsort(retval, nApics, sizeof(*retval), __kmp_affinity_cmp_Address_labels);

  // Find the radix at each of the levels.
  unsigned *totals = (unsigned *)__kmp_allocate(depth * sizeof(unsigned));
  unsigned *counts = (unsigned *)__kmp_allocate(depth * sizeof(unsigned));
  unsigned *maxCt = (unsigned *)__kmp_allocate(depth * sizeof(unsigned));
  unsigned *last = (unsigned *)__kmp_allocate(depth * sizeof(unsigned));
  for (level = 0; level < depth; level++) {
    totals[level] = 1;
    maxCt[level] = 1;
    counts[level] = 1;
    last[level] = retval[0].first.labels[level];
  }

  // From here on, the iteration variable "level" runs from the finest level to
  // the coarsest, i.e. we iterate forward through
  // (*address2os)[].first.labels[] - in the previous loops, we iterated
  // backwards.
  for (proc = 1; (int)proc < nApics; proc++) {
    int level;
    for (level = 0; level < depth; level++) {
      if (retval[proc].first.labels[level] != last[level]) {
        int j;
        for (j = level + 1; j < depth; j++) {
          totals[j]++;
          counts[j] = 1;
          // The line below causes printing incorrect topology information in
          // case the max value for some level (maxCt[level]) is encountered
          // earlier than some less value while going through the array. For
          // example, let pkg0 has 4 cores and pkg1 has 2 cores. Then
          // maxCt[1] == 2
          // whereas it must be 4.
          // TODO!!! Check if it can be commented safely
          // maxCt[j] = 1;
          last[j] = retval[proc].first.labels[j];
        }
        totals[level]++;
        counts[level]++;
        if (counts[level] > maxCt[level]) {
          maxCt[level] = counts[level];
        }
        last[level] = retval[proc].first.labels[level];
        break;
      } else if (level == depth - 1) {
        __kmp_free(last);
        __kmp_free(maxCt);
        __kmp_free(counts);
        __kmp_free(totals);
        __kmp_free(retval);
        KMP_CPU_FREE(oldMask);
        *msg_id = kmp_i18n_str_x2ApicIDsNotUnique;
        return -1;
      }
    }
  }

  // When affinity is off, this routine will still be called to set
  // __kmp_ncores, as well as __kmp_nThreadsPerCore, nCoresPerPkg, & nPackages.
  // Make sure all these vars are set correctly, and return if affinity is not
  // enabled.
  if (threadLevel >= 0) {
    __kmp_nThreadsPerCore = maxCt[threadLevel];
  } else {
    __kmp_nThreadsPerCore = 1;
  }
  nPackages = totals[pkgLevel];

  if (coreLevel >= 0) {
    __kmp_ncores = totals[coreLevel];
    nCoresPerPkg = maxCt[coreLevel];
  } else {
    __kmp_ncores = nPackages;
    nCoresPerPkg = 1;
  }

  // Check to see if the machine topology is uniform
  unsigned prod = maxCt[0];
  for (level = 1; level < depth; level++) {
    prod *= maxCt[level];
  }
  bool uniform = (prod == totals[level - 1]);

  // Print the machine topology summary.
  if (__kmp_affinity_verbose) {
    char mask[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(mask, KMP_AFFIN_MASK_PRINT_LEN, oldMask);

    KMP_INFORM(AffUseGlobCpuidL11, "KMP_AFFINITY");
    if (__kmp_affinity_respect_mask) {
      KMP_INFORM(InitOSProcSetRespect, "KMP_AFFINITY", mask);
    } else {
      KMP_INFORM(InitOSProcSetNotRespect, "KMP_AFFINITY", mask);
    }
    KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
    if (uniform) {
      KMP_INFORM(Uniform, "KMP_AFFINITY");
    } else {
      KMP_INFORM(NonUniform, "KMP_AFFINITY");
    }

    kmp_str_buf_t buf;
    __kmp_str_buf_init(&buf);

    __kmp_str_buf_print(&buf, "%d", totals[0]);
    for (level = 1; level <= pkgLevel; level++) {
      __kmp_str_buf_print(&buf, " x %d", maxCt[level]);
    }
    KMP_INFORM(TopologyExtra, "KMP_AFFINITY", buf.str, nCoresPerPkg,
               __kmp_nThreadsPerCore, __kmp_ncores);

    __kmp_str_buf_free(&buf);
  }
  KMP_DEBUG_ASSERT(__kmp_pu_os_idx == NULL);
  KMP_DEBUG_ASSERT(nApics == __kmp_avail_proc);
  __kmp_pu_os_idx = (int *)__kmp_allocate(sizeof(int) * __kmp_avail_proc);
  for (proc = 0; (int)proc < nApics; ++proc) {
    __kmp_pu_os_idx[proc] = retval[proc].second;
  }
  if (__kmp_affinity_type == affinity_none) {
    __kmp_free(last);
    __kmp_free(maxCt);
    __kmp_free(counts);
    __kmp_free(totals);
    __kmp_free(retval);
    KMP_CPU_FREE(oldMask);
    return 0;
  }

  // Find any levels with radiix 1, and remove them from the map
  // (except for the package level).
  int new_depth = 0;
  for (level = 0; level < depth; level++) {
    if ((maxCt[level] == 1) && (level != pkgLevel)) {
      continue;
    }
    new_depth++;
  }

  // If we are removing any levels, allocate a new vector to return,
  // and copy the relevant information to it.
  if (new_depth != depth) {
    AddrUnsPair *new_retval =
        (AddrUnsPair *)__kmp_allocate(sizeof(AddrUnsPair) * nApics);
    for (proc = 0; (int)proc < nApics; proc++) {
      Address addr(new_depth);
      new_retval[proc] = AddrUnsPair(addr, retval[proc].second);
    }
    int new_level = 0;
    int newPkgLevel = -1;
    int newCoreLevel = -1;
    int newThreadLevel = -1;
    for (level = 0; level < depth; level++) {
      if ((maxCt[level] == 1) && (level != pkgLevel)) {
        // Remove this level. Never remove the package level
        continue;
      }
      if (level == pkgLevel) {
        newPkgLevel = new_level;
      }
      if (level == coreLevel) {
        newCoreLevel = new_level;
      }
      if (level == threadLevel) {
        newThreadLevel = new_level;
      }
      for (proc = 0; (int)proc < nApics; proc++) {
        new_retval[proc].first.labels[new_level] =
            retval[proc].first.labels[level];
      }
      new_level++;
    }

    __kmp_free(retval);
    retval = new_retval;
    depth = new_depth;
    pkgLevel = newPkgLevel;
    coreLevel = newCoreLevel;
    threadLevel = newThreadLevel;
  }

  if (__kmp_affinity_gran_levels < 0) {
    // Set the granularity level based on what levels are modeled
    // in the machine topology map.
    __kmp_affinity_gran_levels = 0;
    if ((threadLevel >= 0) && (__kmp_affinity_gran > affinity_gran_thread)) {
      __kmp_affinity_gran_levels++;
    }
    if ((coreLevel >= 0) && (__kmp_affinity_gran > affinity_gran_core)) {
      __kmp_affinity_gran_levels++;
    }
    if (__kmp_affinity_gran > affinity_gran_package) {
      __kmp_affinity_gran_levels++;
    }
  }

  if (__kmp_affinity_verbose) {
    __kmp_affinity_print_topology(retval, nApics, depth, pkgLevel, coreLevel,
                                  threadLevel);
  }

  __kmp_free(last);
  __kmp_free(maxCt);
  __kmp_free(counts);
  __kmp_free(totals);
  KMP_CPU_FREE(oldMask);
  *address2os = retval;
  return depth;
}

#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

#define osIdIndex 0
#define threadIdIndex 1
#define coreIdIndex 2
#define pkgIdIndex 3
#define nodeIdIndex 4

typedef unsigned *ProcCpuInfo;
static unsigned maxIndex = pkgIdIndex;

static int __kmp_affinity_cmp_ProcCpuInfo_phys_id(const void *a,
                                                  const void *b) {
  unsigned i;
  const unsigned *aa = *(unsigned *const *)a;
  const unsigned *bb = *(unsigned *const *)b;
  for (i = maxIndex;; i--) {
    if (aa[i] < bb[i])
      return -1;
    if (aa[i] > bb[i])
      return 1;
    if (i == osIdIndex)
      break;
  }
  return 0;
}

#if KMP_USE_HIER_SCHED
// Set the array sizes for the hierarchy layers
static void __kmp_dispatch_set_hierarchy_values() {
  // Set the maximum number of L1's to number of cores
  // Set the maximum number of L2's to to either number of cores / 2 for
  // Intel(R) Xeon Phi(TM) coprocessor formally codenamed Knights Landing
  // Or the number of cores for Intel(R) Xeon(R) processors
  // Set the maximum number of NUMA nodes and L3's to number of packages
  __kmp_hier_max_units[kmp_hier_layer_e::LAYER_THREAD + 1] =
      nPackages * nCoresPerPkg * __kmp_nThreadsPerCore;
  __kmp_hier_max_units[kmp_hier_layer_e::LAYER_L1 + 1] = __kmp_ncores;
#if KMP_ARCH_X86_64 && (KMP_OS_LINUX || KMP_OS_WINDOWS)
  if (__kmp_mic_type >= mic3)
    __kmp_hier_max_units[kmp_hier_layer_e::LAYER_L2 + 1] = __kmp_ncores / 2;
  else
#endif // KMP_ARCH_X86_64 && (KMP_OS_LINUX || KMP_OS_WINDOWS)
    __kmp_hier_max_units[kmp_hier_layer_e::LAYER_L2 + 1] = __kmp_ncores;
  __kmp_hier_max_units[kmp_hier_layer_e::LAYER_L3 + 1] = nPackages;
  __kmp_hier_max_units[kmp_hier_layer_e::LAYER_NUMA + 1] = nPackages;
  __kmp_hier_max_units[kmp_hier_layer_e::LAYER_LOOP + 1] = 1;
  // Set the number of threads per unit
  // Number of hardware threads per L1/L2/L3/NUMA/LOOP
  __kmp_hier_threads_per[kmp_hier_layer_e::LAYER_THREAD + 1] = 1;
  __kmp_hier_threads_per[kmp_hier_layer_e::LAYER_L1 + 1] =
      __kmp_nThreadsPerCore;
#if KMP_ARCH_X86_64 && (KMP_OS_LINUX || KMP_OS_WINDOWS)
  if (__kmp_mic_type >= mic3)
    __kmp_hier_threads_per[kmp_hier_layer_e::LAYER_L2 + 1] =
        2 * __kmp_nThreadsPerCore;
  else
#endif // KMP_ARCH_X86_64 && (KMP_OS_LINUX || KMP_OS_WINDOWS)
    __kmp_hier_threads_per[kmp_hier_layer_e::LAYER_L2 + 1] =
        __kmp_nThreadsPerCore;
  __kmp_hier_threads_per[kmp_hier_layer_e::LAYER_L3 + 1] =
      nCoresPerPkg * __kmp_nThreadsPerCore;
  __kmp_hier_threads_per[kmp_hier_layer_e::LAYER_NUMA + 1] =
      nCoresPerPkg * __kmp_nThreadsPerCore;
  __kmp_hier_threads_per[kmp_hier_layer_e::LAYER_LOOP + 1] =
      nPackages * nCoresPerPkg * __kmp_nThreadsPerCore;
}

// Return the index into the hierarchy for this tid and layer type (L1, L2, etc)
// i.e., this thread's L1 or this thread's L2, etc.
int __kmp_dispatch_get_index(int tid, kmp_hier_layer_e type) {
  int index = type + 1;
  int num_hw_threads = __kmp_hier_max_units[kmp_hier_layer_e::LAYER_THREAD + 1];
  KMP_DEBUG_ASSERT(type != kmp_hier_layer_e::LAYER_LAST);
  if (type == kmp_hier_layer_e::LAYER_THREAD)
    return tid;
  else if (type == kmp_hier_layer_e::LAYER_LOOP)
    return 0;
  KMP_DEBUG_ASSERT(__kmp_hier_max_units[index] != 0);
  if (tid >= num_hw_threads)
    tid = tid % num_hw_threads;
  return (tid / __kmp_hier_threads_per[index]) % __kmp_hier_max_units[index];
}

// Return the number of t1's per t2
int __kmp_dispatch_get_t1_per_t2(kmp_hier_layer_e t1, kmp_hier_layer_e t2) {
  int i1 = t1 + 1;
  int i2 = t2 + 1;
  KMP_DEBUG_ASSERT(i1 <= i2);
  KMP_DEBUG_ASSERT(t1 != kmp_hier_layer_e::LAYER_LAST);
  KMP_DEBUG_ASSERT(t2 != kmp_hier_layer_e::LAYER_LAST);
  KMP_DEBUG_ASSERT(__kmp_hier_threads_per[i1] != 0);
  // (nthreads/t2) / (nthreads/t1) = t1 / t2
  return __kmp_hier_threads_per[i2] / __kmp_hier_threads_per[i1];
}
#endif // KMP_USE_HIER_SCHED

// Parse /proc/cpuinfo (or an alternate file in the same format) to obtain the
// affinity map.
static int __kmp_affinity_create_cpuinfo_map(AddrUnsPair **address2os,
                                             int *line,
                                             kmp_i18n_id_t *const msg_id,
                                             FILE *f) {
  *address2os = NULL;
  *msg_id = kmp_i18n_null;

  // Scan of the file, and count the number of "processor" (osId) fields,
  // and find the highest value of <n> for a node_<n> field.
  char buf[256];
  unsigned num_records = 0;
  while (!feof(f)) {
    buf[sizeof(buf) - 1] = 1;
    if (!fgets(buf, sizeof(buf), f)) {
      // Read errors presumably because of EOF
      break;
    }

    char s1[] = "processor";
    if (strncmp(buf, s1, sizeof(s1) - 1) == 0) {
      num_records++;
      continue;
    }

    // FIXME - this will match "node_<n> <garbage>"
    unsigned level;
    if (KMP_SSCANF(buf, "node_%u id", &level) == 1) {
      if (nodeIdIndex + level >= maxIndex) {
        maxIndex = nodeIdIndex + level;
      }
      continue;
    }
  }

  // Check for empty file / no valid processor records, or too many. The number
  // of records can't exceed the number of valid bits in the affinity mask.
  if (num_records == 0) {
    *line = 0;
    *msg_id = kmp_i18n_str_NoProcRecords;
    return -1;
  }
  if (num_records > (unsigned)__kmp_xproc) {
    *line = 0;
    *msg_id = kmp_i18n_str_TooManyProcRecords;
    return -1;
  }

  // Set the file pointer back to the begginning, so that we can scan the file
  // again, this time performing a full parse of the data. Allocate a vector of
  // ProcCpuInfo object, where we will place the data. Adding an extra element
  // at the end allows us to remove a lot of extra checks for termination
  // conditions.
  if (fseek(f, 0, SEEK_SET) != 0) {
    *line = 0;
    *msg_id = kmp_i18n_str_CantRewindCpuinfo;
    return -1;
  }

  // Allocate the array of records to store the proc info in.  The dummy
  // element at the end makes the logic in filling them out easier to code.
  unsigned **threadInfo =
      (unsigned **)__kmp_allocate((num_records + 1) * sizeof(unsigned *));
  unsigned i;
  for (i = 0; i <= num_records; i++) {
    threadInfo[i] =
        (unsigned *)__kmp_allocate((maxIndex + 1) * sizeof(unsigned));
  }

#define CLEANUP_THREAD_INFO                                                    \
  for (i = 0; i <= num_records; i++) {                                         \
    __kmp_free(threadInfo[i]);                                                 \
  }                                                                            \
  __kmp_free(threadInfo);

  // A value of UINT_MAX means that we didn't find the field
  unsigned __index;

#define INIT_PROC_INFO(p)                                                      \
  for (__index = 0; __index <= maxIndex; __index++) {                          \
    (p)[__index] = UINT_MAX;                                                   \
  }

  for (i = 0; i <= num_records; i++) {
    INIT_PROC_INFO(threadInfo[i]);
  }

  unsigned num_avail = 0;
  *line = 0;
  while (!feof(f)) {
    // Create an inner scoping level, so that all the goto targets at the end of
    // the loop appear in an outer scoping level. This avoids warnings about
    // jumping past an initialization to a target in the same block.
    {
      buf[sizeof(buf) - 1] = 1;
      bool long_line = false;
      if (!fgets(buf, sizeof(buf), f)) {
        // Read errors presumably because of EOF
        // If there is valid data in threadInfo[num_avail], then fake
        // a blank line in ensure that the last address gets parsed.
        bool valid = false;
        for (i = 0; i <= maxIndex; i++) {
          if (threadInfo[num_avail][i] != UINT_MAX) {
            valid = true;
          }
        }
        if (!valid) {
          break;
        }
        buf[0] = 0;
      } else if (!buf[sizeof(buf) - 1]) {
        // The line is longer than the buffer.  Set a flag and don't
        // emit an error if we were going to ignore the line, anyway.
        long_line = true;

#define CHECK_LINE                                                             \
  if (long_line) {                                                             \
    CLEANUP_THREAD_INFO;                                                       \
    *msg_id = kmp_i18n_str_LongLineCpuinfo;                                    \
    return -1;                                                                 \
  }
      }
      (*line)++;

      char s1[] = "processor";
      if (strncmp(buf, s1, sizeof(s1) - 1) == 0) {
        CHECK_LINE;
        char *p = strchr(buf + sizeof(s1) - 1, ':');
        unsigned val;
        if ((p == NULL) || (KMP_SSCANF(p + 1, "%u\n", &val) != 1))
          goto no_val;
        if (threadInfo[num_avail][osIdIndex] != UINT_MAX)
#if KMP_ARCH_AARCH64
          // Handle the old AArch64 /proc/cpuinfo layout differently,
          // it contains all of the 'processor' entries listed in a
          // single 'Processor' section, therefore the normal looking
          // for duplicates in that section will always fail.
          num_avail++;
#else
          goto dup_field;
#endif
        threadInfo[num_avail][osIdIndex] = val;
#if KMP_OS_LINUX && !(KMP_ARCH_X86 || KMP_ARCH_X86_64)
        char path[256];
        KMP_SNPRINTF(
            path, sizeof(path),
            "/sys/devices/system/cpu/cpu%u/topology/physical_package_id",
            threadInfo[num_avail][osIdIndex]);
        __kmp_read_from_file(path, "%u", &threadInfo[num_avail][pkgIdIndex]);

        KMP_SNPRINTF(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu%u/topology/core_id",
                     threadInfo[num_avail][osIdIndex]);
        __kmp_read_from_file(path, "%u", &threadInfo[num_avail][coreIdIndex]);
        continue;
#else
      }
      char s2[] = "physical id";
      if (strncmp(buf, s2, sizeof(s2) - 1) == 0) {
        CHECK_LINE;
        char *p = strchr(buf + sizeof(s2) - 1, ':');
        unsigned val;
        if ((p == NULL) || (KMP_SSCANF(p + 1, "%u\n", &val) != 1))
          goto no_val;
        if (threadInfo[num_avail][pkgIdIndex] != UINT_MAX)
          goto dup_field;
        threadInfo[num_avail][pkgIdIndex] = val;
        continue;
      }
      char s3[] = "core id";
      if (strncmp(buf, s3, sizeof(s3) - 1) == 0) {
        CHECK_LINE;
        char *p = strchr(buf + sizeof(s3) - 1, ':');
        unsigned val;
        if ((p == NULL) || (KMP_SSCANF(p + 1, "%u\n", &val) != 1))
          goto no_val;
        if (threadInfo[num_avail][coreIdIndex] != UINT_MAX)
          goto dup_field;
        threadInfo[num_avail][coreIdIndex] = val;
        continue;
#endif // KMP_OS_LINUX && USE_SYSFS_INFO
      }
      char s4[] = "thread id";
      if (strncmp(buf, s4, sizeof(s4) - 1) == 0) {
        CHECK_LINE;
        char *p = strchr(buf + sizeof(s4) - 1, ':');
        unsigned val;
        if ((p == NULL) || (KMP_SSCANF(p + 1, "%u\n", &val) != 1))
          goto no_val;
        if (threadInfo[num_avail][threadIdIndex] != UINT_MAX)
          goto dup_field;
        threadInfo[num_avail][threadIdIndex] = val;
        continue;
      }
      unsigned level;
      if (KMP_SSCANF(buf, "node_%u id", &level) == 1) {
        CHECK_LINE;
        char *p = strchr(buf + sizeof(s4) - 1, ':');
        unsigned val;
        if ((p == NULL) || (KMP_SSCANF(p + 1, "%u\n", &val) != 1))
          goto no_val;
        KMP_ASSERT(nodeIdIndex + level <= maxIndex);
        if (threadInfo[num_avail][nodeIdIndex + level] != UINT_MAX)
          goto dup_field;
        threadInfo[num_avail][nodeIdIndex + level] = val;
        continue;
      }

      // We didn't recognize the leading token on the line. There are lots of
      // leading tokens that we don't recognize - if the line isn't empty, go on
      // to the next line.
      if ((*buf != 0) && (*buf != '\n')) {
        // If the line is longer than the buffer, read characters
        // until we find a newline.
        if (long_line) {
          int ch;
          while (((ch = fgetc(f)) != EOF) && (ch != '\n'))
            ;
        }
        continue;
      }

      // A newline has signalled the end of the processor record.
      // Check that there aren't too many procs specified.
      if ((int)num_avail == __kmp_xproc) {
        CLEANUP_THREAD_INFO;
        *msg_id = kmp_i18n_str_TooManyEntries;
        return -1;
      }

      // Check for missing fields.  The osId field must be there, and we
      // currently require that the physical id field is specified, also.
      if (threadInfo[num_avail][osIdIndex] == UINT_MAX) {
        CLEANUP_THREAD_INFO;
        *msg_id = kmp_i18n_str_MissingProcField;
        return -1;
      }
      if (threadInfo[0][pkgIdIndex] == UINT_MAX) {
        CLEANUP_THREAD_INFO;
        *msg_id = kmp_i18n_str_MissingPhysicalIDField;
        return -1;
      }

      // Skip this proc if it is not included in the machine model.
      if (!KMP_CPU_ISSET(threadInfo[num_avail][osIdIndex],
                         __kmp_affin_fullMask)) {
        INIT_PROC_INFO(threadInfo[num_avail]);
        continue;
      }

      // We have a successful parse of this proc's info.
      // Increment the counter, and prepare for the next proc.
      num_avail++;
      KMP_ASSERT(num_avail <= num_records);
      INIT_PROC_INFO(threadInfo[num_avail]);
    }
    continue;

  no_val:
    CLEANUP_THREAD_INFO;
    *msg_id = kmp_i18n_str_MissingValCpuinfo;
    return -1;

  dup_field:
    CLEANUP_THREAD_INFO;
    *msg_id = kmp_i18n_str_DuplicateFieldCpuinfo;
    return -1;
  }
  *line = 0;

#if KMP_MIC && REDUCE_TEAM_SIZE
  unsigned teamSize = 0;
#endif // KMP_MIC && REDUCE_TEAM_SIZE

  // check for num_records == __kmp_xproc ???

  // If there's only one thread context to bind to, form an Address object with
  // depth 1 and return immediately (or, if affinity is off, set address2os to
  // NULL and return).
  //
  // If it is configured to omit the package level when there is only a single
  // package, the logic at the end of this routine won't work if there is only a
  // single thread - it would try to form an Address object with depth 0.
  KMP_ASSERT(num_avail > 0);
  KMP_ASSERT(num_avail <= num_records);
  if (num_avail == 1) {
    __kmp_ncores = 1;
    __kmp_nThreadsPerCore = nCoresPerPkg = nPackages = 1;
    if (__kmp_affinity_verbose) {
      if (!KMP_AFFINITY_CAPABLE()) {
        KMP_INFORM(AffNotCapableUseCpuinfo, "KMP_AFFINITY");
        KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
        KMP_INFORM(Uniform, "KMP_AFFINITY");
      } else {
        char buf[KMP_AFFIN_MASK_PRINT_LEN];
        __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN,
                                  __kmp_affin_fullMask);
        KMP_INFORM(AffCapableUseCpuinfo, "KMP_AFFINITY");
        if (__kmp_affinity_respect_mask) {
          KMP_INFORM(InitOSProcSetRespect, "KMP_AFFINITY", buf);
        } else {
          KMP_INFORM(InitOSProcSetNotRespect, "KMP_AFFINITY", buf);
        }
        KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
        KMP_INFORM(Uniform, "KMP_AFFINITY");
      }
      int index;
      kmp_str_buf_t buf;
      __kmp_str_buf_init(&buf);
      __kmp_str_buf_print(&buf, "1");
      for (index = maxIndex - 1; index > pkgIdIndex; index--) {
        __kmp_str_buf_print(&buf, " x 1");
      }
      KMP_INFORM(TopologyExtra, "KMP_AFFINITY", buf.str, 1, 1, 1);
      __kmp_str_buf_free(&buf);
    }

    if (__kmp_affinity_type == affinity_none) {
      CLEANUP_THREAD_INFO;
      return 0;
    }

    *address2os = (AddrUnsPair *)__kmp_allocate(sizeof(AddrUnsPair));
    Address addr(1);
    addr.labels[0] = threadInfo[0][pkgIdIndex];
    (*address2os)[0] = AddrUnsPair(addr, threadInfo[0][osIdIndex]);

    if (__kmp_affinity_gran_levels < 0) {
      __kmp_affinity_gran_levels = 0;
    }

    if (__kmp_affinity_verbose) {
      __kmp_affinity_print_topology(*address2os, 1, 1, 0, -1, -1);
    }

    CLEANUP_THREAD_INFO;
    return 1;
  }

  // Sort the threadInfo table by physical Id.
  qsort(threadInfo, num_avail, sizeof(*threadInfo),
        __kmp_affinity_cmp_ProcCpuInfo_phys_id);

  // The table is now sorted by pkgId / coreId / threadId, but we really don't
  // know the radix of any of the fields. pkgId's may be sparsely assigned among
  // the chips on a system. Although coreId's are usually assigned
  // [0 .. coresPerPkg-1] and threadId's are usually assigned
  // [0..threadsPerCore-1], we don't want to make any such assumptions.
  //
  // For that matter, we don't know what coresPerPkg and threadsPerCore (or the
  // total # packages) are at this point - we want to determine that now. We
  // only have an upper bound on the first two figures.
  unsigned *counts =
      (unsigned *)__kmp_allocate((maxIndex + 1) * sizeof(unsigned));
  unsigned *maxCt =
      (unsigned *)__kmp_allocate((maxIndex + 1) * sizeof(unsigned));
  unsigned *totals =
      (unsigned *)__kmp_allocate((maxIndex + 1) * sizeof(unsigned));
  unsigned *lastId =
      (unsigned *)__kmp_allocate((maxIndex + 1) * sizeof(unsigned));

  bool assign_thread_ids = false;
  unsigned threadIdCt;
  unsigned index;

restart_radix_check:
  threadIdCt = 0;

  // Initialize the counter arrays with data from threadInfo[0].
  if (assign_thread_ids) {
    if (threadInfo[0][threadIdIndex] == UINT_MAX) {
      threadInfo[0][threadIdIndex] = threadIdCt++;
    } else if (threadIdCt <= threadInfo[0][threadIdIndex]) {
      threadIdCt = threadInfo[0][threadIdIndex] + 1;
    }
  }
  for (index = 0; index <= maxIndex; index++) {
    counts[index] = 1;
    maxCt[index] = 1;
    totals[index] = 1;
    lastId[index] = threadInfo[0][index];
    ;
  }

  // Run through the rest of the OS procs.
  for (i = 1; i < num_avail; i++) {
    // Find the most significant index whose id differs from the id for the
    // previous OS proc.
    for (index = maxIndex; index >= threadIdIndex; index--) {
      if (assign_thread_ids && (index == threadIdIndex)) {
        // Auto-assign the thread id field if it wasn't specified.
        if (threadInfo[i][threadIdIndex] == UINT_MAX) {
          threadInfo[i][threadIdIndex] = threadIdCt++;
        }
        // Apparently the thread id field was specified for some entries and not
        // others. Start the thread id counter off at the next higher thread id.
        else if (threadIdCt <= threadInfo[i][threadIdIndex]) {
          threadIdCt = threadInfo[i][threadIdIndex] + 1;
        }
      }
      if (threadInfo[i][index] != lastId[index]) {
        // Run through all indices which are less significant, and reset the
        // counts to 1. At all levels up to and including index, we need to
        // increment the totals and record the last id.
        unsigned index2;
        for (index2 = threadIdIndex; index2 < index; index2++) {
          totals[index2]++;
          if (counts[index2] > maxCt[index2]) {
            maxCt[index2] = counts[index2];
          }
          counts[index2] = 1;
          lastId[index2] = threadInfo[i][index2];
        }
        counts[index]++;
        totals[index]++;
        lastId[index] = threadInfo[i][index];

        if (assign_thread_ids && (index > threadIdIndex)) {

#if KMP_MIC && REDUCE_TEAM_SIZE
          // The default team size is the total #threads in the machine
          // minus 1 thread for every core that has 3 or more threads.
          teamSize += (threadIdCt <= 2) ? (threadIdCt) : (threadIdCt - 1);
#endif // KMP_MIC && REDUCE_TEAM_SIZE

          // Restart the thread counter, as we are on a new core.
          threadIdCt = 0;

          // Auto-assign the thread id field if it wasn't specified.
          if (threadInfo[i][threadIdIndex] == UINT_MAX) {
            threadInfo[i][threadIdIndex] = threadIdCt++;
          }

          // Aparrently the thread id field was specified for some entries and
          // not others. Start the thread id counter off at the next higher
          // thread id.
          else if (threadIdCt <= threadInfo[i][threadIdIndex]) {
            threadIdCt = threadInfo[i][threadIdIndex] + 1;
          }
        }
        break;
      }
    }
    if (index < threadIdIndex) {
      // If thread ids were specified, it is an error if they are not unique.
      // Also, check that we waven't already restarted the loop (to be safe -
      // shouldn't need to).
      if ((threadInfo[i][threadIdIndex] != UINT_MAX) || assign_thread_ids) {
        __kmp_free(lastId);
        __kmp_free(totals);
        __kmp_free(maxCt);
        __kmp_free(counts);
        CLEANUP_THREAD_INFO;
        *msg_id = kmp_i18n_str_PhysicalIDsNotUnique;
        return -1;
      }

      // If the thread ids were not specified and we see entries entries that
      // are duplicates, start the loop over and assign the thread ids manually.
      assign_thread_ids = true;
      goto restart_radix_check;
    }
  }

#if KMP_MIC && REDUCE_TEAM_SIZE
  // The default team size is the total #threads in the machine
  // minus 1 thread for every core that has 3 or more threads.
  teamSize += (threadIdCt <= 2) ? (threadIdCt) : (threadIdCt - 1);
#endif // KMP_MIC && REDUCE_TEAM_SIZE

  for (index = threadIdIndex; index <= maxIndex; index++) {
    if (counts[index] > maxCt[index]) {
      maxCt[index] = counts[index];
    }
  }

  __kmp_nThreadsPerCore = maxCt[threadIdIndex];
  nCoresPerPkg = maxCt[coreIdIndex];
  nPackages = totals[pkgIdIndex];

  // Check to see if the machine topology is uniform
  unsigned prod = totals[maxIndex];
  for (index = threadIdIndex; index < maxIndex; index++) {
    prod *= maxCt[index];
  }
  bool uniform = (prod == totals[threadIdIndex]);

  // When affinity is off, this routine will still be called to set
  // __kmp_ncores, as well as __kmp_nThreadsPerCore, nCoresPerPkg, & nPackages.
  // Make sure all these vars are set correctly, and return now if affinity is
  // not enabled.
  __kmp_ncores = totals[coreIdIndex];

  if (__kmp_affinity_verbose) {
    if (!KMP_AFFINITY_CAPABLE()) {
      KMP_INFORM(AffNotCapableUseCpuinfo, "KMP_AFFINITY");
      KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
      if (uniform) {
        KMP_INFORM(Uniform, "KMP_AFFINITY");
      } else {
        KMP_INFORM(NonUniform, "KMP_AFFINITY");
      }
    } else {
      char buf[KMP_AFFIN_MASK_PRINT_LEN];
      __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN,
                                __kmp_affin_fullMask);
      KMP_INFORM(AffCapableUseCpuinfo, "KMP_AFFINITY");
      if (__kmp_affinity_respect_mask) {
        KMP_INFORM(InitOSProcSetRespect, "KMP_AFFINITY", buf);
      } else {
        KMP_INFORM(InitOSProcSetNotRespect, "KMP_AFFINITY", buf);
      }
      KMP_INFORM(AvailableOSProc, "KMP_AFFINITY", __kmp_avail_proc);
      if (uniform) {
        KMP_INFORM(Uniform, "KMP_AFFINITY");
      } else {
        KMP_INFORM(NonUniform, "KMP_AFFINITY");
      }
    }
    kmp_str_buf_t buf;
    __kmp_str_buf_init(&buf);

    __kmp_str_buf_print(&buf, "%d", totals[maxIndex]);
    for (index = maxIndex - 1; index >= pkgIdIndex; index--) {
      __kmp_str_buf_print(&buf, " x %d", maxCt[index]);
    }
    KMP_INFORM(TopologyExtra, "KMP_AFFINITY", buf.str, maxCt[coreIdIndex],
               maxCt[threadIdIndex], __kmp_ncores);

    __kmp_str_buf_free(&buf);
  }

#if KMP_MIC && REDUCE_TEAM_SIZE
  // Set the default team size.
  if ((__kmp_dflt_team_nth == 0) && (teamSize > 0)) {
    __kmp_dflt_team_nth = teamSize;
    KA_TRACE(20, ("__kmp_affinity_create_cpuinfo_map: setting "
                  "__kmp_dflt_team_nth = %d\n",
                  __kmp_dflt_team_nth));
  }
#endif // KMP_MIC && REDUCE_TEAM_SIZE

  KMP_DEBUG_ASSERT(__kmp_pu_os_idx == NULL);
  KMP_DEBUG_ASSERT(num_avail == (unsigned)__kmp_avail_proc);
  __kmp_pu_os_idx = (int *)__kmp_allocate(sizeof(int) * __kmp_avail_proc);
  for (i = 0; i < num_avail; ++i) { // fill the os indices
    __kmp_pu_os_idx[i] = threadInfo[i][osIdIndex];
  }

  if (__kmp_affinity_type == affinity_none) {
    __kmp_free(lastId);
    __kmp_free(totals);
    __kmp_free(maxCt);
    __kmp_free(counts);
    CLEANUP_THREAD_INFO;
    return 0;
  }

  // Count the number of levels which have more nodes at that level than at the
  // parent's level (with there being an implicit root node of the top level).
  // This is equivalent to saying that there is at least one node at this level
  // which has a sibling. These levels are in the map, and the package level is
  // always in the map.
  bool *inMap = (bool *)__kmp_allocate((maxIndex + 1) * sizeof(bool));
  for (index = threadIdIndex; index < maxIndex; index++) {
    KMP_ASSERT(totals[index] >= totals[index + 1]);
    inMap[index] = (totals[index] > totals[index + 1]);
  }
  inMap[maxIndex] = (totals[maxIndex] > 1);
  inMap[pkgIdIndex] = true;

  int depth = 0;
  for (index = threadIdIndex; index <= maxIndex; index++) {
    if (inMap[index]) {
      depth++;
    }
  }
  KMP_ASSERT(depth > 0);

  // Construct the data structure that is to be returned.
  *address2os = (AddrUnsPair *)__kmp_allocate(sizeof(AddrUnsPair) * num_avail);
  int pkgLevel = -1;
  int coreLevel = -1;
  int threadLevel = -1;

  for (i = 0; i < num_avail; ++i) {
    Address addr(depth);
    unsigned os = threadInfo[i][osIdIndex];
    int src_index;
    int dst_index = 0;

    for (src_index = maxIndex; src_index >= threadIdIndex; src_index--) {
      if (!inMap[src_index]) {
        continue;
      }
      addr.labels[dst_index] = threadInfo[i][src_index];
      if (src_index == pkgIdIndex) {
        pkgLevel = dst_index;
      } else if (src_index == coreIdIndex) {
        coreLevel = dst_index;
      } else if (src_index == threadIdIndex) {
        threadLevel = dst_index;
      }
      dst_index++;
    }
    (*address2os)[i] = AddrUnsPair(addr, os);
  }

  if (__kmp_affinity_gran_levels < 0) {
    // Set the granularity level based on what levels are modeled
    // in the machine topology map.
    unsigned src_index;
    __kmp_affinity_gran_levels = 0;
    for (src_index = threadIdIndex; src_index <= maxIndex; src_index++) {
      if (!inMap[src_index]) {
        continue;
      }
      switch (src_index) {
      case threadIdIndex:
        if (__kmp_affinity_gran > affinity_gran_thread) {
          __kmp_affinity_gran_levels++;
        }

        break;
      case coreIdIndex:
        if (__kmp_affinity_gran > affinity_gran_core) {
          __kmp_affinity_gran_levels++;
        }
        break;

      case pkgIdIndex:
        if (__kmp_affinity_gran > affinity_gran_package) {
          __kmp_affinity_gran_levels++;
        }
        break;
      }
    }
  }

  if (__kmp_affinity_verbose) {
    __kmp_affinity_print_topology(*address2os, num_avail, depth, pkgLevel,
                                  coreLevel, threadLevel);
  }

  __kmp_free(inMap);
  __kmp_free(lastId);
  __kmp_free(totals);
  __kmp_free(maxCt);
  __kmp_free(counts);
  CLEANUP_THREAD_INFO;
  return depth;
}

// Create and return a table of affinity masks, indexed by OS thread ID.
// This routine handles OR'ing together all the affinity masks of threads
// that are sufficiently close, if granularity > fine.
static kmp_affin_mask_t *__kmp_create_masks(unsigned *maxIndex,
                                            unsigned *numUnique,
                                            AddrUnsPair *address2os,
                                            unsigned numAddrs) {
  // First form a table of affinity masks in order of OS thread id.
  unsigned depth;
  unsigned maxOsId;
  unsigned i;

  KMP_ASSERT(numAddrs > 0);
  depth = address2os[0].first.depth;

  maxOsId = 0;
  for (i = numAddrs - 1;; --i) {
    unsigned osId = address2os[i].second;
    if (osId > maxOsId) {
      maxOsId = osId;
    }
    if (i == 0)
      break;
  }
  kmp_affin_mask_t *osId2Mask;
  KMP_CPU_ALLOC_ARRAY(osId2Mask, (maxOsId + 1));

  // Sort the address2os table according to physical order. Doing so will put
  // all threads on the same core/package/node in consecutive locations.
  qsort(address2os, numAddrs, sizeof(*address2os),
        __kmp_affinity_cmp_Address_labels);

  KMP_ASSERT(__kmp_affinity_gran_levels >= 0);
  if (__kmp_affinity_verbose && (__kmp_affinity_gran_levels > 0)) {
    KMP_INFORM(ThreadsMigrate, "KMP_AFFINITY", __kmp_affinity_gran_levels);
  }
  if (__kmp_affinity_gran_levels >= (int)depth) {
    if (__kmp_affinity_verbose ||
        (__kmp_affinity_warnings && (__kmp_affinity_type != affinity_none))) {
      KMP_WARNING(AffThreadsMayMigrate);
    }
  }

  // Run through the table, forming the masks for all threads on each core.
  // Threads on the same core will have identical "Address" objects, not
  // considering the last level, which must be the thread id. All threads on a
  // core will appear consecutively.
  unsigned unique = 0;
  unsigned j = 0; // index of 1st thread on core
  unsigned leader = 0;
  Address *leaderAddr = &(address2os[0].first);
  kmp_affin_mask_t *sum;
  KMP_CPU_ALLOC_ON_STACK(sum);
  KMP_CPU_ZERO(sum);
  KMP_CPU_SET(address2os[0].second, sum);
  for (i = 1; i < numAddrs; i++) {
    // If this thread is sufficiently close to the leader (within the
    // granularity setting), then set the bit for this os thread in the
    // affinity mask for this group, and go on to the next thread.
    if (leaderAddr->isClose(address2os[i].first, __kmp_affinity_gran_levels)) {
      KMP_CPU_SET(address2os[i].second, sum);
      continue;
    }

    // For every thread in this group, copy the mask to the thread's entry in
    // the osId2Mask table.  Mark the first address as a leader.
    for (; j < i; j++) {
      unsigned osId = address2os[j].second;
      KMP_DEBUG_ASSERT(osId <= maxOsId);
      kmp_affin_mask_t *mask = KMP_CPU_INDEX(osId2Mask, osId);
      KMP_CPU_COPY(mask, sum);
      address2os[j].first.leader = (j == leader);
    }
    unique++;

    // Start a new mask.
    leader = i;
    leaderAddr = &(address2os[i].first);
    KMP_CPU_ZERO(sum);
    KMP_CPU_SET(address2os[i].second, sum);
  }

  // For every thread in last group, copy the mask to the thread's
  // entry in the osId2Mask table.
  for (; j < i; j++) {
    unsigned osId = address2os[j].second;
    KMP_DEBUG_ASSERT(osId <= maxOsId);
    kmp_affin_mask_t *mask = KMP_CPU_INDEX(osId2Mask, osId);
    KMP_CPU_COPY(mask, sum);
    address2os[j].first.leader = (j == leader);
  }
  unique++;
  KMP_CPU_FREE_FROM_STACK(sum);

  *maxIndex = maxOsId;
  *numUnique = unique;
  return osId2Mask;
}

// Stuff for the affinity proclist parsers.  It's easier to declare these vars
// as file-static than to try and pass them through the calling sequence of
// the recursive-descent OMP_PLACES parser.
static kmp_affin_mask_t *newMasks;
static int numNewMasks;
static int nextNewMask;

#define ADD_MASK(_mask)                                                        \
  {                                                                            \
    if (nextNewMask >= numNewMasks) {                                          \
      int i;                                                                   \
      numNewMasks *= 2;                                                        \
      kmp_affin_mask_t *temp;                                                  \
      KMP_CPU_INTERNAL_ALLOC_ARRAY(temp, numNewMasks);                         \
      for (i = 0; i < numNewMasks / 2; i++) {                                  \
        kmp_affin_mask_t *src = KMP_CPU_INDEX(newMasks, i);                    \
        kmp_affin_mask_t *dest = KMP_CPU_INDEX(temp, i);                       \
        KMP_CPU_COPY(dest, src);                                               \
      }                                                                        \
      KMP_CPU_INTERNAL_FREE_ARRAY(newMasks, numNewMasks / 2);                  \
      newMasks = temp;                                                         \
    }                                                                          \
    KMP_CPU_COPY(KMP_CPU_INDEX(newMasks, nextNewMask), (_mask));               \
    nextNewMask++;                                                             \
  }

#define ADD_MASK_OSID(_osId, _osId2Mask, _maxOsId)                             \
  {                                                                            \
    if (((_osId) > _maxOsId) ||                                                \
        (!KMP_CPU_ISSET((_osId), KMP_CPU_INDEX((_osId2Mask), (_osId))))) {     \
      if (__kmp_affinity_verbose ||                                            \
          (__kmp_affinity_warnings &&                                          \
           (__kmp_affinity_type != affinity_none))) {                          \
        KMP_WARNING(AffIgnoreInvalidProcID, _osId);                            \
      }                                                                        \
    } else {                                                                   \
      ADD_MASK(KMP_CPU_INDEX(_osId2Mask, (_osId)));                            \
    }                                                                          \
  }

// Re-parse the proclist (for the explicit affinity type), and form the list
// of affinity newMasks indexed by gtid.
static void __kmp_affinity_process_proclist(kmp_affin_mask_t **out_masks,
                                            unsigned int *out_numMasks,
                                            const char *proclist,
                                            kmp_affin_mask_t *osId2Mask,
                                            int maxOsId) {
  int i;
  const char *scan = proclist;
  const char *next = proclist;

  // We use malloc() for the temporary mask vector, so that we can use
  // realloc() to extend it.
  numNewMasks = 2;
  KMP_CPU_INTERNAL_ALLOC_ARRAY(newMasks, numNewMasks);
  nextNewMask = 0;
  kmp_affin_mask_t *sumMask;
  KMP_CPU_ALLOC(sumMask);
  int setSize = 0;

  for (;;) {
    int start, end, stride;

    SKIP_WS(scan);
    next = scan;
    if (*next == '\0') {
      break;
    }

    if (*next == '{') {
      int num;
      setSize = 0;
      next++; // skip '{'
      SKIP_WS(next);
      scan = next;

      // Read the first integer in the set.
      KMP_ASSERT2((*next >= '0') && (*next <= '9'), "bad proclist");
      SKIP_DIGITS(next);
      num = __kmp_str_to_int(scan, *next);
      KMP_ASSERT2(num >= 0, "bad explicit proc list");

      // Copy the mask for that osId to the sum (union) mask.
      if ((num > maxOsId) ||
          (!KMP_CPU_ISSET(num, KMP_CPU_INDEX(osId2Mask, num)))) {
        if (__kmp_affinity_verbose ||
            (__kmp_affinity_warnings &&
             (__kmp_affinity_type != affinity_none))) {
          KMP_WARNING(AffIgnoreInvalidProcID, num);
        }
        KMP_CPU_ZERO(sumMask);
      } else {
        KMP_CPU_COPY(sumMask, KMP_CPU_INDEX(osId2Mask, num));
        setSize = 1;
      }

      for (;;) {
        // Check for end of set.
        SKIP_WS(next);
        if (*next == '}') {
          next++; // skip '}'
          break;
        }

        // Skip optional comma.
        if (*next == ',') {
          next++;
        }
        SKIP_WS(next);

        // Read the next integer in the set.
        scan = next;
        KMP_ASSERT2((*next >= '0') && (*next <= '9'), "bad explicit proc list");

        SKIP_DIGITS(next);
        num = __kmp_str_to_int(scan, *next);
        KMP_ASSERT2(num >= 0, "bad explicit proc list");

        // Add the mask for that osId to the sum mask.
        if ((num > maxOsId) ||
            (!KMP_CPU_ISSET(num, KMP_CPU_INDEX(osId2Mask, num)))) {
          if (__kmp_affinity_verbose ||
              (__kmp_affinity_warnings &&
               (__kmp_affinity_type != affinity_none))) {
            KMP_WARNING(AffIgnoreInvalidProcID, num);
          }
        } else {
          KMP_CPU_UNION(sumMask, KMP_CPU_INDEX(osId2Mask, num));
          setSize++;
        }
      }
      if (setSize > 0) {
        ADD_MASK(sumMask);
      }

      SKIP_WS(next);
      if (*next == ',') {
        next++;
      }
      scan = next;
      continue;
    }

    // Read the first integer.
    KMP_ASSERT2((*next >= '0') && (*next <= '9'), "bad explicit proc list");
    SKIP_DIGITS(next);
    start = __kmp_str_to_int(scan, *next);
    KMP_ASSERT2(start >= 0, "bad explicit proc list");
    SKIP_WS(next);

    // If this isn't a range, then add a mask to the list and go on.
    if (*next != '-') {
      ADD_MASK_OSID(start, osId2Mask, maxOsId);

      // Skip optional comma.
      if (*next == ',') {
        next++;
      }
      scan = next;
      continue;
    }

    // This is a range.  Skip over the '-' and read in the 2nd int.
    next++; // skip '-'
    SKIP_WS(next);
    scan = next;
    KMP_ASSERT2((*next >= '0') && (*next <= '9'), "bad explicit proc list");
    SKIP_DIGITS(next);
    end = __kmp_str_to_int(scan, *next);
    KMP_ASSERT2(end >= 0, "bad explicit proc list");

    // Check for a stride parameter
    stride = 1;
    SKIP_WS(next);
    if (*next == ':') {
      // A stride is specified.  Skip over the ':" and read the 3rd int.
      int sign = +1;
      next++; // skip ':'
      SKIP_WS(next);
      scan = next;
      if (*next == '-') {
        sign = -1;
        next++;
        SKIP_WS(next);
        scan = next;
      }
      KMP_ASSERT2((*next >= '0') && (*next <= '9'), "bad explicit proc list");
      SKIP_DIGITS(next);
      stride = __kmp_str_to_int(scan, *next);
      KMP_ASSERT2(stride >= 0, "bad explicit proc list");
      stride *= sign;
    }

    // Do some range checks.
    KMP_ASSERT2(stride != 0, "bad explicit proc list");
    if (stride > 0) {
      KMP_ASSERT2(start <= end, "bad explicit proc list");
    } else {
      KMP_ASSERT2(start >= end, "bad explicit proc list");
    }
    KMP_ASSERT2((end - start) / stride <= 65536, "bad explicit proc list");

    // Add the mask for each OS proc # to the list.
    if (stride > 0) {
      do {
        ADD_MASK_OSID(start, osId2Mask, maxOsId);
        start += stride;
      } while (start <= end);
    } else {
      do {
        ADD_MASK_OSID(start, osId2Mask, maxOsId);
        start += stride;
      } while (start >= end);
    }

    // Skip optional comma.
    SKIP_WS(next);
    if (*next == ',') {
      next++;
    }
    scan = next;
  }

  *out_numMasks = nextNewMask;
  if (nextNewMask == 0) {
    *out_masks = NULL;
    KMP_CPU_INTERNAL_FREE_ARRAY(newMasks, numNewMasks);
    return;
  }
  KMP_CPU_ALLOC_ARRAY((*out_masks), nextNewMask);
  for (i = 0; i < nextNewMask; i++) {
    kmp_affin_mask_t *src = KMP_CPU_INDEX(newMasks, i);
    kmp_affin_mask_t *dest = KMP_CPU_INDEX((*out_masks), i);
    KMP_CPU_COPY(dest, src);
  }
  KMP_CPU_INTERNAL_FREE_ARRAY(newMasks, numNewMasks);
  KMP_CPU_FREE(sumMask);
}

#if OMP_40_ENABLED

/*-----------------------------------------------------------------------------
Re-parse the OMP_PLACES proc id list, forming the newMasks for the different
places.  Again, Here is the grammar:

place_list := place
place_list := place , place_list
place := num
place := place : num
place := place : num : signed
place := { subplacelist }
place := ! place                  // (lowest priority)
subplace_list := subplace
subplace_list := subplace , subplace_list
subplace := num
subplace := num : num
subplace := num : num : signed
signed := num
signed := + signed
signed := - signed
-----------------------------------------------------------------------------*/

static void __kmp_process_subplace_list(const char **scan,
                                        kmp_affin_mask_t *osId2Mask,
                                        int maxOsId, kmp_affin_mask_t *tempMask,
                                        int *setSize) {
  const char *next;

  for (;;) {
    int start, count, stride, i;

    // Read in the starting proc id
    SKIP_WS(*scan);
    KMP_ASSERT2((**scan >= '0') && (**scan <= '9'), "bad explicit places list");
    next = *scan;
    SKIP_DIGITS(next);
    start = __kmp_str_to_int(*scan, *next);
    KMP_ASSERT(start >= 0);
    *scan = next;

    // valid follow sets are ',' ':' and '}'
    SKIP_WS(*scan);
    if (**scan == '}' || **scan == ',') {
      if ((start > maxOsId) ||
          (!KMP_CPU_ISSET(start, KMP_CPU_INDEX(osId2Mask, start)))) {
        if (__kmp_affinity_verbose ||
            (__kmp_affinity_warnings &&
             (__kmp_affinity_type != affinity_none))) {
          KMP_WARNING(AffIgnoreInvalidProcID, start);
        }
      } else {
        KMP_CPU_UNION(tempMask, KMP_CPU_INDEX(osId2Mask, start));
        (*setSize)++;
      }
      if (**scan == '}') {
        break;
      }
      (*scan)++; // skip ','
      continue;
    }
    KMP_ASSERT2(**scan == ':', "bad explicit places list");
    (*scan)++; // skip ':'

    // Read count parameter
    SKIP_WS(*scan);
    KMP_ASSERT2((**scan >= '0') && (**scan <= '9'), "bad explicit places list");
    next = *scan;
    SKIP_DIGITS(next);
    count = __kmp_str_to_int(*scan, *next);
    KMP_ASSERT(count >= 0);
    *scan = next;

    // valid follow sets are ',' ':' and '}'
    SKIP_WS(*scan);
    if (**scan == '}' || **scan == ',') {
      for (i = 0; i < count; i++) {
        if ((start > maxOsId) ||
            (!KMP_CPU_ISSET(start, KMP_CPU_INDEX(osId2Mask, start)))) {
          if (__kmp_affinity_verbose ||
              (__kmp_affinity_warnings &&
               (__kmp_affinity_type != affinity_none))) {
            KMP_WARNING(AffIgnoreInvalidProcID, start);
          }
          break; // don't proliferate warnings for large count
        } else {
          KMP_CPU_UNION(tempMask, KMP_CPU_INDEX(osId2Mask, start));
          start++;
          (*setSize)++;
        }
      }
      if (**scan == '}') {
        break;
      }
      (*scan)++; // skip ','
      continue;
    }
    KMP_ASSERT2(**scan == ':', "bad explicit places list");
    (*scan)++; // skip ':'

    // Read stride parameter
    int sign = +1;
    for (;;) {
      SKIP_WS(*scan);
      if (**scan == '+') {
        (*scan)++; // skip '+'
        continue;
      }
      if (**scan == '-') {
        sign *= -1;
        (*scan)++; // skip '-'
        continue;
      }
      break;
    }
    SKIP_WS(*scan);
    KMP_ASSERT2((**scan >= '0') && (**scan <= '9'), "bad explicit places list");
    next = *scan;
    SKIP_DIGITS(next);
    stride = __kmp_str_to_int(*scan, *next);
    KMP_ASSERT(stride >= 0);
    *scan = next;
    stride *= sign;

    // valid follow sets are ',' and '}'
    SKIP_WS(*scan);
    if (**scan == '}' || **scan == ',') {
      for (i = 0; i < count; i++) {
        if ((start > maxOsId) ||
            (!KMP_CPU_ISSET(start, KMP_CPU_INDEX(osId2Mask, start)))) {
          if (__kmp_affinity_verbose ||
              (__kmp_affinity_warnings &&
               (__kmp_affinity_type != affinity_none))) {
            KMP_WARNING(AffIgnoreInvalidProcID, start);
          }
          break; // don't proliferate warnings for large count
        } else {
          KMP_CPU_UNION(tempMask, KMP_CPU_INDEX(osId2Mask, start));
          start += stride;
          (*setSize)++;
        }
      }
      if (**scan == '}') {
        break;
      }
      (*scan)++; // skip ','
      continue;
    }

    KMP_ASSERT2(0, "bad explicit places list");
  }
}

static void __kmp_process_place(const char **scan, kmp_affin_mask_t *osId2Mask,
                                int maxOsId, kmp_affin_mask_t *tempMask,
                                int *setSize) {
  const char *next;

  // valid follow sets are '{' '!' and num
  SKIP_WS(*scan);
  if (**scan == '{') {
    (*scan)++; // skip '{'
    __kmp_process_subplace_list(scan, osId2Mask, maxOsId, tempMask, setSize);
    KMP_ASSERT2(**scan == '}', "bad explicit places list");
    (*scan)++; // skip '}'
  } else if (**scan == '!') {
    (*scan)++; // skip '!'
    __kmp_process_place(scan, osId2Mask, maxOsId, tempMask, setSize);
    KMP_CPU_COMPLEMENT(maxOsId, tempMask);
  } else if ((**scan >= '0') && (**scan <= '9')) {
    next = *scan;
    SKIP_DIGITS(next);
    int num = __kmp_str_to_int(*scan, *next);
    KMP_ASSERT(num >= 0);
    if ((num > maxOsId) ||
        (!KMP_CPU_ISSET(num, KMP_CPU_INDEX(osId2Mask, num)))) {
      if (__kmp_affinity_verbose ||
          (__kmp_affinity_warnings && (__kmp_affinity_type != affinity_none))) {
        KMP_WARNING(AffIgnoreInvalidProcID, num);
      }
    } else {
      KMP_CPU_UNION(tempMask, KMP_CPU_INDEX(osId2Mask, num));
      (*setSize)++;
    }
    *scan = next; // skip num
  } else {
    KMP_ASSERT2(0, "bad explicit places list");
  }
}

// static void
void __kmp_affinity_process_placelist(kmp_affin_mask_t **out_masks,
                                      unsigned int *out_numMasks,
                                      const char *placelist,
                                      kmp_affin_mask_t *osId2Mask,
                                      int maxOsId) {
  int i, j, count, stride, sign;
  const char *scan = placelist;
  const char *next = placelist;

  numNewMasks = 2;
  KMP_CPU_INTERNAL_ALLOC_ARRAY(newMasks, numNewMasks);
  nextNewMask = 0;

  // tempMask is modified based on the previous or initial
  //   place to form the current place
  // previousMask contains the previous place
  kmp_affin_mask_t *tempMask;
  kmp_affin_mask_t *previousMask;
  KMP_CPU_ALLOC(tempMask);
  KMP_CPU_ZERO(tempMask);
  KMP_CPU_ALLOC(previousMask);
  KMP_CPU_ZERO(previousMask);
  int setSize = 0;

  for (;;) {
    __kmp_process_place(&scan, osId2Mask, maxOsId, tempMask, &setSize);

    // valid follow sets are ',' ':' and EOL
    SKIP_WS(scan);
    if (*scan == '\0' || *scan == ',') {
      if (setSize > 0) {
        ADD_MASK(tempMask);
      }
      KMP_CPU_ZERO(tempMask);
      setSize = 0;
      if (*scan == '\0') {
        break;
      }
      scan++; // skip ','
      continue;
    }

    KMP_ASSERT2(*scan == ':', "bad explicit places list");
    scan++; // skip ':'

    // Read count parameter
    SKIP_WS(scan);
    KMP_ASSERT2((*scan >= '0') && (*scan <= '9'), "bad explicit places list");
    next = scan;
    SKIP_DIGITS(next);
    count = __kmp_str_to_int(scan, *next);
    KMP_ASSERT(count >= 0);
    scan = next;

    // valid follow sets are ',' ':' and EOL
    SKIP_WS(scan);
    if (*scan == '\0' || *scan == ',') {
      stride = +1;
    } else {
      KMP_ASSERT2(*scan == ':', "bad explicit places list");
      scan++; // skip ':'

      // Read stride parameter
      sign = +1;
      for (;;) {
        SKIP_WS(scan);
        if (*scan == '+') {
          scan++; // skip '+'
          continue;
        }
        if (*scan == '-') {
          sign *= -1;
          scan++; // skip '-'
          continue;
        }
        break;
      }
      SKIP_WS(scan);
      KMP_ASSERT2((*scan >= '0') && (*scan <= '9'), "bad explicit places list");
      next = scan;
      SKIP_DIGITS(next);
      stride = __kmp_str_to_int(scan, *next);
      KMP_DEBUG_ASSERT(stride >= 0);
      scan = next;
      stride *= sign;
    }

    // Add places determined by initial_place : count : stride
    for (i = 0; i < count; i++) {
      if (setSize == 0) {
        break;
      }
      // Add the current place, then build the next place (tempMask) from that
      KMP_CPU_COPY(previousMask, tempMask);
      ADD_MASK(previousMask);
      KMP_CPU_ZERO(tempMask);
      setSize = 0;
      KMP_CPU_SET_ITERATE(j, previousMask) {
        if (!KMP_CPU_ISSET(j, previousMask)) {
          continue;
        }
        if ((j + stride > maxOsId) || (j + stride < 0) ||
            (!KMP_CPU_ISSET(j, __kmp_affin_fullMask)) ||
            (!KMP_CPU_ISSET(j + stride,
                            KMP_CPU_INDEX(osId2Mask, j + stride)))) {
          if ((__kmp_affinity_verbose ||
               (__kmp_affinity_warnings &&
                (__kmp_affinity_type != affinity_none))) &&
              i < count - 1) {
            KMP_WARNING(AffIgnoreInvalidProcID, j + stride);
          }
          continue;
        }
        KMP_CPU_SET(j + stride, tempMask);
        setSize++;
      }
    }
    KMP_CPU_ZERO(tempMask);
    setSize = 0;

    // valid follow sets are ',' and EOL
    SKIP_WS(scan);
    if (*scan == '\0') {
      break;
    }
    if (*scan == ',') {
      scan++; // skip ','
      continue;
    }

    KMP_ASSERT2(0, "bad explicit places list");
  }

  *out_numMasks = nextNewMask;
  if (nextNewMask == 0) {
    *out_masks = NULL;
    KMP_CPU_INTERNAL_FREE_ARRAY(newMasks, numNewMasks);
    return;
  }
  KMP_CPU_ALLOC_ARRAY((*out_masks), nextNewMask);
  KMP_CPU_FREE(tempMask);
  KMP_CPU_FREE(previousMask);
  for (i = 0; i < nextNewMask; i++) {
    kmp_affin_mask_t *src = KMP_CPU_INDEX(newMasks, i);
    kmp_affin_mask_t *dest = KMP_CPU_INDEX((*out_masks), i);
    KMP_CPU_COPY(dest, src);
  }
  KMP_CPU_INTERNAL_FREE_ARRAY(newMasks, numNewMasks);
}

#endif /* OMP_40_ENABLED */

#undef ADD_MASK
#undef ADD_MASK_OSID

#if KMP_USE_HWLOC
static int __kmp_hwloc_skip_PUs_obj(hwloc_topology_t t, hwloc_obj_t o) {
  // skip PUs descendants of the object o
  int skipped = 0;
  hwloc_obj_t hT = NULL;
  int N = __kmp_hwloc_count_children_by_type(t, o, HWLOC_OBJ_PU, &hT);
  for (int i = 0; i < N; ++i) {
    KMP_DEBUG_ASSERT(hT);
    unsigned idx = hT->os_index;
    if (KMP_CPU_ISSET(idx, __kmp_affin_fullMask)) {
      KMP_CPU_CLR(idx, __kmp_affin_fullMask);
      KC_TRACE(200, ("KMP_HW_SUBSET: skipped proc %d\n", idx));
      ++skipped;
    }
    hT = hwloc_get_next_obj_by_type(t, HWLOC_OBJ_PU, hT);
  }
  return skipped; // count number of skipped units
}

static int __kmp_hwloc_obj_has_PUs(hwloc_topology_t t, hwloc_obj_t o) {
  // check if obj has PUs present in fullMask
  hwloc_obj_t hT = NULL;
  int N = __kmp_hwloc_count_children_by_type(t, o, HWLOC_OBJ_PU, &hT);
  for (int i = 0; i < N; ++i) {
    KMP_DEBUG_ASSERT(hT);
    unsigned idx = hT->os_index;
    if (KMP_CPU_ISSET(idx, __kmp_affin_fullMask))
      return 1; // found PU
    hT = hwloc_get_next_obj_by_type(t, HWLOC_OBJ_PU, hT);
  }
  return 0; // no PUs found
}
#endif // KMP_USE_HWLOC

static void __kmp_apply_thread_places(AddrUnsPair **pAddr, int depth) {
  AddrUnsPair *newAddr;
  if (__kmp_hws_requested == 0)
    goto _exit; // no topology limiting actions requested, exit
#if KMP_USE_HWLOC
  if (__kmp_affinity_dispatch->get_api_type() == KMPAffinity::HWLOC) {
    // Number of subobjects calculated dynamically, this works fine for
    // any non-uniform topology.
    // L2 cache objects are determined by depth, other objects - by type.
    hwloc_topology_t tp = __kmp_hwloc_topology;
    int nS = 0, nN = 0, nL = 0, nC = 0,
        nT = 0; // logical index including skipped
    int nCr = 0, nTr = 0; // number of requested units
    int nPkg = 0, nCo = 0, n_new = 0, n_old = 0, nCpP = 0, nTpC = 0; // counters
    hwloc_obj_t hT, hC, hL, hN, hS; // hwloc objects (pointers to)
    int L2depth, idx;

    // check support of extensions ----------------------------------
    int numa_support = 0, tile_support = 0;
    if (__kmp_pu_os_idx)
      hT = hwloc_get_pu_obj_by_os_index(tp,
                                        __kmp_pu_os_idx[__kmp_avail_proc - 1]);
    else
      hT = hwloc_get_obj_by_type(tp, HWLOC_OBJ_PU, __kmp_avail_proc - 1);
    if (hT == NULL) { // something's gone wrong
      KMP_WARNING(AffHWSubsetUnsupported);
      goto _exit;
    }
    // check NUMA node
    hN = hwloc_get_ancestor_obj_by_type(tp, HWLOC_OBJ_NUMANODE, hT);
    hS = hwloc_get_ancestor_obj_by_type(tp, HWLOC_OBJ_PACKAGE, hT);
    if (hN != NULL && hN->depth > hS->depth) {
      numa_support = 1; // 1 in case socket includes node(s)
    } else if (__kmp_hws_node.num > 0) {
      // don't support sockets inside NUMA node (no such HW found for testing)
      KMP_WARNING(AffHWSubsetUnsupported);
      goto _exit;
    }
    // check L2 cahce, get object by depth because of multiple caches
    L2depth = hwloc_get_cache_type_depth(tp, 2, HWLOC_OBJ_CACHE_UNIFIED);
    hL = hwloc_get_ancestor_obj_by_depth(tp, L2depth, hT);
    if (hL != NULL &&
        __kmp_hwloc_count_children_by_type(tp, hL, HWLOC_OBJ_CORE, &hC) > 1) {
      tile_support = 1; // no sense to count L2 if it includes single core
    } else if (__kmp_hws_tile.num > 0) {
      if (__kmp_hws_core.num == 0) {
        __kmp_hws_core = __kmp_hws_tile; // replace L2 with core
        __kmp_hws_tile.num = 0;
      } else {
        // L2 and core are both requested, but represent same object
        KMP_WARNING(AffHWSubsetInvalid);
        goto _exit;
      }
    }
    // end of check of extensions -----------------------------------

    // fill in unset items, validate settings -----------------------
    if (__kmp_hws_socket.num == 0)
      __kmp_hws_socket.num = nPackages; // use all available sockets
    if (__kmp_hws_socket.offset >= nPackages) {
      KMP_WARNING(AffHWSubsetManySockets);
      goto _exit;
    }
    if (numa_support) {
      hN = NULL;
      int NN = __kmp_hwloc_count_children_by_type(tp, hS, HWLOC_OBJ_NUMANODE,
                                                  &hN); // num nodes in socket
      if (__kmp_hws_node.num == 0)
        __kmp_hws_node.num = NN; // use all available nodes
      if (__kmp_hws_node.offset >= NN) {
        KMP_WARNING(AffHWSubsetManyNodes);
        goto _exit;
      }
      if (tile_support) {
        // get num tiles in node
        int NL = __kmp_hwloc_count_children_by_depth(tp, hN, L2depth, &hL);
        if (__kmp_hws_tile.num == 0) {
          __kmp_hws_tile.num = NL + 1;
        } // use all available tiles, some node may have more tiles, thus +1
        if (__kmp_hws_tile.offset >= NL) {
          KMP_WARNING(AffHWSubsetManyTiles);
          goto _exit;
        }
        int NC = __kmp_hwloc_count_children_by_type(tp, hL, HWLOC_OBJ_CORE,
                                                    &hC); // num cores in tile
        if (__kmp_hws_core.num == 0)
          __kmp_hws_core.num = NC; // use all available cores
        if (__kmp_hws_core.offset >= NC) {
          KMP_WARNING(AffHWSubsetManyCores);
          goto _exit;
        }
      } else { // tile_support
        int NC = __kmp_hwloc_count_children_by_type(tp, hN, HWLOC_OBJ_CORE,
                                                    &hC); // num cores in node
        if (__kmp_hws_core.num == 0)
          __kmp_hws_core.num = NC; // use all available cores
        if (__kmp_hws_core.offset >= NC) {
          KMP_WARNING(AffHWSubsetManyCores);
          goto _exit;
        }
      } // tile_support
    } else { // numa_support
      if (tile_support) {
        // get num tiles in socket
        int NL = __kmp_hwloc_count_children_by_depth(tp, hS, L2depth, &hL);
        if (__kmp_hws_tile.num == 0)
          __kmp_hws_tile.num = NL; // use all available tiles
        if (__kmp_hws_tile.offset >= NL) {
          KMP_WARNING(AffHWSubsetManyTiles);
          goto _exit;
        }
        int NC = __kmp_hwloc_count_children_by_type(tp, hL, HWLOC_OBJ_CORE,
                                                    &hC); // num cores in tile
        if (__kmp_hws_core.num == 0)
          __kmp_hws_core.num = NC; // use all available cores
        if (__kmp_hws_core.offset >= NC) {
          KMP_WARNING(AffHWSubsetManyCores);
          goto _exit;
        }
      } else { // tile_support
        int NC = __kmp_hwloc_count_children_by_type(tp, hS, HWLOC_OBJ_CORE,
                                                    &hC); // num cores in socket
        if (__kmp_hws_core.num == 0)
          __kmp_hws_core.num = NC; // use all available cores
        if (__kmp_hws_core.offset >= NC) {
          KMP_WARNING(AffHWSubsetManyCores);
          goto _exit;
        }
      } // tile_support
    }
    if (__kmp_hws_proc.num == 0)
      __kmp_hws_proc.num = __kmp_nThreadsPerCore; // use all available procs
    if (__kmp_hws_proc.offset >= __kmp_nThreadsPerCore) {
      KMP_WARNING(AffHWSubsetManyProcs);
      goto _exit;
    }
    // end of validation --------------------------------------------

    if (pAddr) // pAddr is NULL in case of affinity_none
      newAddr = (AddrUnsPair *)__kmp_allocate(sizeof(AddrUnsPair) *
                                              __kmp_avail_proc); // max size
    // main loop to form HW subset ----------------------------------
    hS = NULL;
    int NP = hwloc_get_nbobjs_by_type(tp, HWLOC_OBJ_PACKAGE);
    for (int s = 0; s < NP; ++s) {
      // Check Socket -----------------------------------------------
      hS = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PACKAGE, hS);
      if (!__kmp_hwloc_obj_has_PUs(tp, hS))
        continue; // skip socket if all PUs are out of fullMask
      ++nS; // only count objects those have PUs in affinity mask
      if (nS <= __kmp_hws_socket.offset ||
          nS > __kmp_hws_socket.num + __kmp_hws_socket.offset) {
        n_old += __kmp_hwloc_skip_PUs_obj(tp, hS); // skip socket
        continue; // move to next socket
      }
      nCr = 0; // count number of cores per socket
      // socket requested, go down the topology tree
      // check 4 cases: (+NUMA+Tile), (+NUMA-Tile), (-NUMA+Tile), (-NUMA-Tile)
      if (numa_support) {
        nN = 0;
        hN = NULL;
        // num nodes in current socket
        int NN =
            __kmp_hwloc_count_children_by_type(tp, hS, HWLOC_OBJ_NUMANODE, &hN);
        for (int n = 0; n < NN; ++n) {
          // Check NUMA Node ----------------------------------------
          if (!__kmp_hwloc_obj_has_PUs(tp, hN)) {
            hN = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_NUMANODE, hN);
            continue; // skip node if all PUs are out of fullMask
          }
          ++nN;
          if (nN <= __kmp_hws_node.offset ||
              nN > __kmp_hws_node.num + __kmp_hws_node.offset) {
            // skip node as not requested
            n_old += __kmp_hwloc_skip_PUs_obj(tp, hN); // skip node
            hN = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_NUMANODE, hN);
            continue; // move to next node
          }
          // node requested, go down the topology tree
          if (tile_support) {
            nL = 0;
            hL = NULL;
            int NL = __kmp_hwloc_count_children_by_depth(tp, hN, L2depth, &hL);
            for (int l = 0; l < NL; ++l) {
              // Check L2 (tile) ------------------------------------
              if (!__kmp_hwloc_obj_has_PUs(tp, hL)) {
                hL = hwloc_get_next_obj_by_depth(tp, L2depth, hL);
                continue; // skip tile if all PUs are out of fullMask
              }
              ++nL;
              if (nL <= __kmp_hws_tile.offset ||
                  nL > __kmp_hws_tile.num + __kmp_hws_tile.offset) {
                // skip tile as not requested
                n_old += __kmp_hwloc_skip_PUs_obj(tp, hL); // skip tile
                hL = hwloc_get_next_obj_by_depth(tp, L2depth, hL);
                continue; // move to next tile
              }
              // tile requested, go down the topology tree
              nC = 0;
              hC = NULL;
              // num cores in current tile
              int NC = __kmp_hwloc_count_children_by_type(tp, hL,
                                                          HWLOC_OBJ_CORE, &hC);
              for (int c = 0; c < NC; ++c) {
                // Check Core ---------------------------------------
                if (!__kmp_hwloc_obj_has_PUs(tp, hC)) {
                  hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
                  continue; // skip core if all PUs are out of fullMask
                }
                ++nC;
                if (nC <= __kmp_hws_core.offset ||
                    nC > __kmp_hws_core.num + __kmp_hws_core.offset) {
                  // skip node as not requested
                  n_old += __kmp_hwloc_skip_PUs_obj(tp, hC); // skip core
                  hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
                  continue; // move to next node
                }
                // core requested, go down to PUs
                nT = 0;
                nTr = 0;
                hT = NULL;
                // num procs in current core
                int NT = __kmp_hwloc_count_children_by_type(tp, hC,
                                                            HWLOC_OBJ_PU, &hT);
                for (int t = 0; t < NT; ++t) {
                  // Check PU ---------------------------------------
                  idx = hT->os_index;
                  if (!KMP_CPU_ISSET(idx, __kmp_affin_fullMask)) {
                    hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
                    continue; // skip PU if not in fullMask
                  }
                  ++nT;
                  if (nT <= __kmp_hws_proc.offset ||
                      nT > __kmp_hws_proc.num + __kmp_hws_proc.offset) {
                    // skip PU
                    KMP_CPU_CLR(idx, __kmp_affin_fullMask);
                    ++n_old;
                    KC_TRACE(200, ("KMP_HW_SUBSET: skipped proc %d\n", idx));
                    hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
                    continue; // move to next node
                  }
                  ++nTr;
                  if (pAddr) // collect requested thread's data
                    newAddr[n_new] = (*pAddr)[n_old];
                  ++n_new;
                  ++n_old;
                  hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
                } // threads loop
                if (nTr > 0) {
                  ++nCr; // num cores per socket
                  ++nCo; // total num cores
                  if (nTr > nTpC)
                    nTpC = nTr; // calc max threads per core
                }
                hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
              } // cores loop
              hL = hwloc_get_next_obj_by_depth(tp, L2depth, hL);
            } // tiles loop
          } else { // tile_support
            // no tiles, check cores
            nC = 0;
            hC = NULL;
            // num cores in current node
            int NC =
                __kmp_hwloc_count_children_by_type(tp, hN, HWLOC_OBJ_CORE, &hC);
            for (int c = 0; c < NC; ++c) {
              // Check Core ---------------------------------------
              if (!__kmp_hwloc_obj_has_PUs(tp, hC)) {
                hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
                continue; // skip core if all PUs are out of fullMask
              }
              ++nC;
              if (nC <= __kmp_hws_core.offset ||
                  nC > __kmp_hws_core.num + __kmp_hws_core.offset) {
                // skip node as not requested
                n_old += __kmp_hwloc_skip_PUs_obj(tp, hC); // skip core
                hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
                continue; // move to next node
              }
              // core requested, go down to PUs
              nT = 0;
              nTr = 0;
              hT = NULL;
              int NT =
                  __kmp_hwloc_count_children_by_type(tp, hC, HWLOC_OBJ_PU, &hT);
              for (int t = 0; t < NT; ++t) {
                // Check PU ---------------------------------------
                idx = hT->os_index;
                if (!KMP_CPU_ISSET(idx, __kmp_affin_fullMask)) {
                  hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
                  continue; // skip PU if not in fullMask
                }
                ++nT;
                if (nT <= __kmp_hws_proc.offset ||
                    nT > __kmp_hws_proc.num + __kmp_hws_proc.offset) {
                  // skip PU
                  KMP_CPU_CLR(idx, __kmp_affin_fullMask);
                  ++n_old;
                  KC_TRACE(200, ("KMP_HW_SUBSET: skipped proc %d\n", idx));
                  hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
                  continue; // move to next node
                }
                ++nTr;
                if (pAddr) // collect requested thread's data
                  newAddr[n_new] = (*pAddr)[n_old];
                ++n_new;
                ++n_old;
                hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
              } // threads loop
              if (nTr > 0) {
                ++nCr; // num cores per socket
                ++nCo; // total num cores
                if (nTr > nTpC)
                  nTpC = nTr; // calc max threads per core
              }
              hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
            } // cores loop
          } // tiles support
          hN = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_NUMANODE, hN);
        } // nodes loop
      } else { // numa_support
        // no NUMA support
        if (tile_support) {
          nL = 0;
          hL = NULL;
          // num tiles in current socket
          int NL = __kmp_hwloc_count_children_by_depth(tp, hS, L2depth, &hL);
          for (int l = 0; l < NL; ++l) {
            // Check L2 (tile) ------------------------------------
            if (!__kmp_hwloc_obj_has_PUs(tp, hL)) {
              hL = hwloc_get_next_obj_by_depth(tp, L2depth, hL);
              continue; // skip tile if all PUs are out of fullMask
            }
            ++nL;
            if (nL <= __kmp_hws_tile.offset ||
                nL > __kmp_hws_tile.num + __kmp_hws_tile.offset) {
              // skip tile as not requested
              n_old += __kmp_hwloc_skip_PUs_obj(tp, hL); // skip tile
              hL = hwloc_get_next_obj_by_depth(tp, L2depth, hL);
              continue; // move to next tile
            }
            // tile requested, go down the topology tree
            nC = 0;
            hC = NULL;
            // num cores per tile
            int NC =
                __kmp_hwloc_count_children_by_type(tp, hL, HWLOC_OBJ_CORE, &hC);
            for (int c = 0; c < NC; ++c) {
              // Check Core ---------------------------------------
              if (!__kmp_hwloc_obj_has_PUs(tp, hC)) {
                hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
                continue; // skip core if all PUs are out of fullMask
              }
              ++nC;
              if (nC <= __kmp_hws_core.offset ||
                  nC > __kmp_hws_core.num + __kmp_hws_core.offset) {
                // skip node as not requested
                n_old += __kmp_hwloc_skip_PUs_obj(tp, hC); // skip core
                hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
                continue; // move to next node
              }
              // core requested, go down to PUs
              nT = 0;
              nTr = 0;
              hT = NULL;
              // num procs per core
              int NT =
                  __kmp_hwloc_count_children_by_type(tp, hC, HWLOC_OBJ_PU, &hT);
              for (int t = 0; t < NT; ++t) {
                // Check PU ---------------------------------------
                idx = hT->os_index;
                if (!KMP_CPU_ISSET(idx, __kmp_affin_fullMask)) {
                  hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
                  continue; // skip PU if not in fullMask
                }
                ++nT;
                if (nT <= __kmp_hws_proc.offset ||
                    nT > __kmp_hws_proc.num + __kmp_hws_proc.offset) {
                  // skip PU
                  KMP_CPU_CLR(idx, __kmp_affin_fullMask);
                  ++n_old;
                  KC_TRACE(200, ("KMP_HW_SUBSET: skipped proc %d\n", idx));
                  hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
                  continue; // move to next node
                }
                ++nTr;
                if (pAddr) // collect requested thread's data
                  newAddr[n_new] = (*pAddr)[n_old];
                ++n_new;
                ++n_old;
                hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
              } // threads loop
              if (nTr > 0) {
                ++nCr; // num cores per socket
                ++nCo; // total num cores
                if (nTr > nTpC)
                  nTpC = nTr; // calc max threads per core
              }
              hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
            } // cores loop
            hL = hwloc_get_next_obj_by_depth(tp, L2depth, hL);
          } // tiles loop
        } else { // tile_support
          // no tiles, check cores
          nC = 0;
          hC = NULL;
          // num cores in socket
          int NC =
              __kmp_hwloc_count_children_by_type(tp, hS, HWLOC_OBJ_CORE, &hC);
          for (int c = 0; c < NC; ++c) {
            // Check Core -------------------------------------------
            if (!__kmp_hwloc_obj_has_PUs(tp, hC)) {
              hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
              continue; // skip core if all PUs are out of fullMask
            }
            ++nC;
            if (nC <= __kmp_hws_core.offset ||
                nC > __kmp_hws_core.num + __kmp_hws_core.offset) {
              // skip node as not requested
              n_old += __kmp_hwloc_skip_PUs_obj(tp, hC); // skip core
              hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
              continue; // move to next node
            }
            // core requested, go down to PUs
            nT = 0;
            nTr = 0;
            hT = NULL;
            // num procs per core
            int NT =
                __kmp_hwloc_count_children_by_type(tp, hC, HWLOC_OBJ_PU, &hT);
            for (int t = 0; t < NT; ++t) {
              // Check PU ---------------------------------------
              idx = hT->os_index;
              if (!KMP_CPU_ISSET(idx, __kmp_affin_fullMask)) {
                hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
                continue; // skip PU if not in fullMask
              }
              ++nT;
              if (nT <= __kmp_hws_proc.offset ||
                  nT > __kmp_hws_proc.num + __kmp_hws_proc.offset) {
                // skip PU
                KMP_CPU_CLR(idx, __kmp_affin_fullMask);
                ++n_old;
                KC_TRACE(200, ("KMP_HW_SUBSET: skipped proc %d\n", idx));
                hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
                continue; // move to next node
              }
              ++nTr;
              if (pAddr) // collect requested thread's data
                newAddr[n_new] = (*pAddr)[n_old];
              ++n_new;
              ++n_old;
              hT = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_PU, hT);
            } // threads loop
            if (nTr > 0) {
              ++nCr; // num cores per socket
              ++nCo; // total num cores
              if (nTr > nTpC)
                nTpC = nTr; // calc max threads per core
            }
            hC = hwloc_get_next_obj_by_type(tp, HWLOC_OBJ_CORE, hC);
          } // cores loop
        } // tiles support
      } // numa_support
      if (nCr > 0) { // found cores?
        ++nPkg; // num sockets
        if (nCr > nCpP)
          nCpP = nCr; // calc max cores per socket
      }
    } // sockets loop

    // check the subset is valid
    KMP_DEBUG_ASSERT(n_old == __kmp_avail_proc);
    KMP_DEBUG_ASSERT(nPkg > 0);
    KMP_DEBUG_ASSERT(nCpP > 0);
    KMP_DEBUG_ASSERT(nTpC > 0);
    KMP_DEBUG_ASSERT(nCo > 0);
    KMP_DEBUG_ASSERT(nPkg <= nPackages);
    KMP_DEBUG_ASSERT(nCpP <= nCoresPerPkg);
    KMP_DEBUG_ASSERT(nTpC <= __kmp_nThreadsPerCore);
    KMP_DEBUG_ASSERT(nCo <= __kmp_ncores);

    nPackages = nPkg; // correct num sockets
    nCoresPerPkg = nCpP; // correct num cores per socket
    __kmp_nThreadsPerCore = nTpC; // correct num threads per core
    __kmp_avail_proc = n_new; // correct num procs
    __kmp_ncores = nCo; // correct num cores
    // hwloc topology method end
  } else
#endif // KMP_USE_HWLOC
  {
    int n_old = 0, n_new = 0, proc_num = 0;
    if (__kmp_hws_node.num > 0 || __kmp_hws_tile.num > 0) {
      KMP_WARNING(AffHWSubsetNoHWLOC);
      goto _exit;
    }
    if (__kmp_hws_socket.num == 0)
      __kmp_hws_socket.num = nPackages; // use all available sockets
    if (__kmp_hws_core.num == 0)
      __kmp_hws_core.num = nCoresPerPkg; // use all available cores
    if (__kmp_hws_proc.num == 0 || __kmp_hws_proc.num > __kmp_nThreadsPerCore)
      __kmp_hws_proc.num = __kmp_nThreadsPerCore; // use all HW contexts
    if (!__kmp_affinity_uniform_topology()) {
      KMP_WARNING(AffHWSubsetNonUniform);
      goto _exit; // don't support non-uniform topology
    }
    if (depth > 3) {
      KMP_WARNING(AffHWSubsetNonThreeLevel);
      goto _exit; // don't support not-3-level topology
    }
    if (__kmp_hws_socket.offset + __kmp_hws_socket.num > nPackages) {
      KMP_WARNING(AffHWSubsetManySockets);
      goto _exit;
    }
    if (__kmp_hws_core.offset + __kmp_hws_core.num > nCoresPerPkg) {
      KMP_WARNING(AffHWSubsetManyCores);
      goto _exit;
    }
    // Form the requested subset
    if (pAddr) // pAddr is NULL in case of affinity_none
      newAddr = (AddrUnsPair *)__kmp_allocate(
          sizeof(AddrUnsPair) * __kmp_hws_socket.num * __kmp_hws_core.num *
          __kmp_hws_proc.num);
    for (int i = 0; i < nPackages; ++i) {
      if (i < __kmp_hws_socket.offset ||
          i >= __kmp_hws_socket.offset + __kmp_hws_socket.num) {
        // skip not-requested socket
        n_old += nCoresPerPkg * __kmp_nThreadsPerCore;
        if (__kmp_pu_os_idx != NULL) {
          // walk through skipped socket
          for (int j = 0; j < nCoresPerPkg; ++j) {
            for (int k = 0; k < __kmp_nThreadsPerCore; ++k) {
              KMP_CPU_CLR(__kmp_pu_os_idx[proc_num], __kmp_affin_fullMask);
              ++proc_num;
            }
          }
        }
      } else {
        // walk through requested socket
        for (int j = 0; j < nCoresPerPkg; ++j) {
          if (j < __kmp_hws_core.offset ||
              j >= __kmp_hws_core.offset +
                       __kmp_hws_core.num) { // skip not-requested core
            n_old += __kmp_nThreadsPerCore;
            if (__kmp_pu_os_idx != NULL) {
              for (int k = 0; k < __kmp_nThreadsPerCore; ++k) {
                KMP_CPU_CLR(__kmp_pu_os_idx[proc_num], __kmp_affin_fullMask);
                ++proc_num;
              }
            }
          } else {
            // walk through requested core
            for (int k = 0; k < __kmp_nThreadsPerCore; ++k) {
              if (k < __kmp_hws_proc.num) {
                if (pAddr) // collect requested thread's data
                  newAddr[n_new] = (*pAddr)[n_old];
                n_new++;
              } else {
                if (__kmp_pu_os_idx != NULL)
                  KMP_CPU_CLR(__kmp_pu_os_idx[proc_num], __kmp_affin_fullMask);
              }
              n_old++;
              ++proc_num;
            }
          }
        }
      }
    }
    KMP_DEBUG_ASSERT(n_old == nPackages * nCoresPerPkg * __kmp_nThreadsPerCore);
    KMP_DEBUG_ASSERT(n_new ==
                     __kmp_hws_socket.num * __kmp_hws_core.num *
                         __kmp_hws_proc.num);
    nPackages = __kmp_hws_socket.num; // correct nPackages
    nCoresPerPkg = __kmp_hws_core.num; // correct nCoresPerPkg
    __kmp_nThreadsPerCore = __kmp_hws_proc.num; // correct __kmp_nThreadsPerCore
    __kmp_avail_proc = n_new; // correct avail_proc
    __kmp_ncores = nPackages * __kmp_hws_core.num; // correct ncores
  } // non-hwloc topology method
  if (pAddr) {
    __kmp_free(*pAddr);
    *pAddr = newAddr; // replace old topology with new one
  }
  if (__kmp_affinity_verbose) {
    char m[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(m, KMP_AFFIN_MASK_PRINT_LEN,
                              __kmp_affin_fullMask);
    if (__kmp_affinity_respect_mask) {
      KMP_INFORM(InitOSProcSetRespect, "KMP_HW_SUBSET", m);
    } else {
      KMP_INFORM(InitOSProcSetNotRespect, "KMP_HW_SUBSET", m);
    }
    KMP_INFORM(AvailableOSProc, "KMP_HW_SUBSET", __kmp_avail_proc);
    kmp_str_buf_t buf;
    __kmp_str_buf_init(&buf);
    __kmp_str_buf_print(&buf, "%d", nPackages);
    KMP_INFORM(TopologyExtra, "KMP_HW_SUBSET", buf.str, nCoresPerPkg,
               __kmp_nThreadsPerCore, __kmp_ncores);
    __kmp_str_buf_free(&buf);
  }
_exit:
  if (__kmp_pu_os_idx != NULL) {
    __kmp_free(__kmp_pu_os_idx);
    __kmp_pu_os_idx = NULL;
  }
}

// This function figures out the deepest level at which there is at least one
// cluster/core with more than one processing unit bound to it.
static int __kmp_affinity_find_core_level(const AddrUnsPair *address2os,
                                          int nprocs, int bottom_level) {
  int core_level = 0;

  for (int i = 0; i < nprocs; i++) {
    for (int j = bottom_level; j > 0; j--) {
      if (address2os[i].first.labels[j] > 0) {
        if (core_level < (j - 1)) {
          core_level = j - 1;
        }
      }
    }
  }
  return core_level;
}

// This function counts number of clusters/cores at given level.
static int __kmp_affinity_compute_ncores(const AddrUnsPair *address2os,
                                         int nprocs, int bottom_level,
                                         int core_level) {
  int ncores = 0;
  int i, j;

  j = bottom_level;
  for (i = 0; i < nprocs; i++) {
    for (j = bottom_level; j > core_level; j--) {
      if ((i + 1) < nprocs) {
        if (address2os[i + 1].first.labels[j] > 0) {
          break;
        }
      }
    }
    if (j == core_level) {
      ncores++;
    }
  }
  if (j > core_level) {
    // In case of ( nprocs < __kmp_avail_proc ) we may end too deep and miss one
    // core. May occur when called from __kmp_affinity_find_core().
    ncores++;
  }
  return ncores;
}

// This function finds to which cluster/core given processing unit is bound.
static int __kmp_affinity_find_core(const AddrUnsPair *address2os, int proc,
                                    int bottom_level, int core_level) {
  return __kmp_affinity_compute_ncores(address2os, proc + 1, bottom_level,
                                       core_level) -
         1;
}

// This function finds maximal number of processing units bound to a
// cluster/core at given level.
static int __kmp_affinity_max_proc_per_core(const AddrUnsPair *address2os,
                                            int nprocs, int bottom_level,
                                            int core_level) {
  int maxprocpercore = 0;

  if (core_level < bottom_level) {
    for (int i = 0; i < nprocs; i++) {
      int percore = address2os[i].first.labels[core_level + 1] + 1;

      if (percore > maxprocpercore) {
        maxprocpercore = percore;
      }
    }
  } else {
    maxprocpercore = 1;
  }
  return maxprocpercore;
}

static AddrUnsPair *address2os = NULL;
static int *procarr = NULL;
static int __kmp_aff_depth = 0;

#if KMP_USE_HIER_SCHED
#define KMP_EXIT_AFF_NONE                                                      \
  KMP_ASSERT(__kmp_affinity_type == affinity_none);                            \
  KMP_ASSERT(address2os == NULL);                                              \
  __kmp_apply_thread_places(NULL, 0);                                          \
  __kmp_create_affinity_none_places();                                         \
  __kmp_dispatch_set_hierarchy_values();                                       \
  return;
#else
#define KMP_EXIT_AFF_NONE                                                      \
  KMP_ASSERT(__kmp_affinity_type == affinity_none);                            \
  KMP_ASSERT(address2os == NULL);                                              \
  __kmp_apply_thread_places(NULL, 0);                                          \
  __kmp_create_affinity_none_places();                                         \
  return;
#endif

// Create a one element mask array (set of places) which only contains the
// initial process's affinity mask
static void __kmp_create_affinity_none_places() {
  KMP_ASSERT(__kmp_affin_fullMask != NULL);
  KMP_ASSERT(__kmp_affinity_type == affinity_none);
  __kmp_affinity_num_masks = 1;
  KMP_CPU_ALLOC_ARRAY(__kmp_affinity_masks, __kmp_affinity_num_masks);
  kmp_affin_mask_t *dest = KMP_CPU_INDEX(__kmp_affinity_masks, 0);
  KMP_CPU_COPY(dest, __kmp_affin_fullMask);
}

static int __kmp_affinity_cmp_Address_child_num(const void *a, const void *b) {
  const Address *aa = &(((const AddrUnsPair *)a)->first);
  const Address *bb = &(((const AddrUnsPair *)b)->first);
  unsigned depth = aa->depth;
  unsigned i;
  KMP_DEBUG_ASSERT(depth == bb->depth);
  KMP_DEBUG_ASSERT((unsigned)__kmp_affinity_compact <= depth);
  KMP_DEBUG_ASSERT(__kmp_affinity_compact >= 0);
  for (i = 0; i < (unsigned)__kmp_affinity_compact; i++) {
    int j = depth - i - 1;
    if (aa->childNums[j] < bb->childNums[j])
      return -1;
    if (aa->childNums[j] > bb->childNums[j])
      return 1;
  }
  for (; i < depth; i++) {
    int j = i - __kmp_affinity_compact;
    if (aa->childNums[j] < bb->childNums[j])
      return -1;
    if (aa->childNums[j] > bb->childNums[j])
      return 1;
  }
  return 0;
}

static void __kmp_aux_affinity_initialize(void) {
  if (__kmp_affinity_masks != NULL) {
    KMP_ASSERT(__kmp_affin_fullMask != NULL);
    return;
  }

  // Create the "full" mask - this defines all of the processors that we
  // consider to be in the machine model. If respect is set, then it is the
  // initialization thread's affinity mask. Otherwise, it is all processors that
  // we know about on the machine.
  if (__kmp_affin_fullMask == NULL) {
    KMP_CPU_ALLOC(__kmp_affin_fullMask);
  }
  if (KMP_AFFINITY_CAPABLE()) {
    if (__kmp_affinity_respect_mask) {
      __kmp_get_system_affinity(__kmp_affin_fullMask, TRUE);

      // Count the number of available processors.
      unsigned i;
      __kmp_avail_proc = 0;
      KMP_CPU_SET_ITERATE(i, __kmp_affin_fullMask) {
        if (!KMP_CPU_ISSET(i, __kmp_affin_fullMask)) {
          continue;
        }
        __kmp_avail_proc++;
      }
      if (__kmp_avail_proc > __kmp_xproc) {
        if (__kmp_affinity_verbose ||
            (__kmp_affinity_warnings &&
             (__kmp_affinity_type != affinity_none))) {
          KMP_WARNING(ErrorInitializeAffinity);
        }
        __kmp_affinity_type = affinity_none;
        KMP_AFFINITY_DISABLE();
        return;
      }
    } else {
      __kmp_affinity_entire_machine_mask(__kmp_affin_fullMask);
      __kmp_avail_proc = __kmp_xproc;
    }
  }

  if (__kmp_affinity_gran == affinity_gran_tile &&
      // check if user's request is valid
      __kmp_affinity_dispatch->get_api_type() == KMPAffinity::NATIVE_OS) {
    KMP_WARNING(AffTilesNoHWLOC, "KMP_AFFINITY");
    __kmp_affinity_gran = affinity_gran_package;
  }

  int depth = -1;
  kmp_i18n_id_t msg_id = kmp_i18n_null;

  // For backward compatibility, setting KMP_CPUINFO_FILE =>
  // KMP_TOPOLOGY_METHOD=cpuinfo
  if ((__kmp_cpuinfo_file != NULL) &&
      (__kmp_affinity_top_method == affinity_top_method_all)) {
    __kmp_affinity_top_method = affinity_top_method_cpuinfo;
  }

  if (__kmp_affinity_top_method == affinity_top_method_all) {
    // In the default code path, errors are not fatal - we just try using
    // another method. We only emit a warning message if affinity is on, or the
    // verbose flag is set, an the nowarnings flag was not set.
    const char *file_name = NULL;
    int line = 0;
#if KMP_USE_HWLOC
    if (depth < 0 &&
        __kmp_affinity_dispatch->get_api_type() == KMPAffinity::HWLOC) {
      if (__kmp_affinity_verbose) {
        KMP_INFORM(AffUsingHwloc, "KMP_AFFINITY");
      }
      if (!__kmp_hwloc_error) {
        depth = __kmp_affinity_create_hwloc_map(&address2os, &msg_id);
        if (depth == 0) {
          KMP_EXIT_AFF_NONE;
        } else if (depth < 0 && __kmp_affinity_verbose) {
          KMP_INFORM(AffIgnoringHwloc, "KMP_AFFINITY");
        }
      } else if (__kmp_affinity_verbose) {
        KMP_INFORM(AffIgnoringHwloc, "KMP_AFFINITY");
      }
    }
#endif

#if KMP_ARCH_X86 || KMP_ARCH_X86_64

    if (depth < 0) {
      if (__kmp_affinity_verbose) {
        KMP_INFORM(AffInfoStr, "KMP_AFFINITY", KMP_I18N_STR(Decodingx2APIC));
      }

      file_name = NULL;
      depth = __kmp_affinity_create_x2apicid_map(&address2os, &msg_id);
      if (depth == 0) {
        KMP_EXIT_AFF_NONE;
      }

      if (depth < 0) {
        if (__kmp_affinity_verbose) {
          if (msg_id != kmp_i18n_null) {
            KMP_INFORM(AffInfoStrStr, "KMP_AFFINITY",
                       __kmp_i18n_catgets(msg_id),
                       KMP_I18N_STR(DecodingLegacyAPIC));
          } else {
            KMP_INFORM(AffInfoStr, "KMP_AFFINITY",
                       KMP_I18N_STR(DecodingLegacyAPIC));
          }
        }

        file_name = NULL;
        depth = __kmp_affinity_create_apicid_map(&address2os, &msg_id);
        if (depth == 0) {
          KMP_EXIT_AFF_NONE;
        }
      }
    }

#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

#if KMP_OS_LINUX

    if (depth < 0) {
      if (__kmp_affinity_verbose) {
        if (msg_id != kmp_i18n_null) {
          KMP_INFORM(AffStrParseFilename, "KMP_AFFINITY",
                     __kmp_i18n_catgets(msg_id), "/proc/cpuinfo");
        } else {
          KMP_INFORM(AffParseFilename, "KMP_AFFINITY", "/proc/cpuinfo");
        }
      }

      FILE *f = fopen("/proc/cpuinfo", "r");
      if (f == NULL) {
        msg_id = kmp_i18n_str_CantOpenCpuinfo;
      } else {
        file_name = "/proc/cpuinfo";
        depth =
            __kmp_affinity_create_cpuinfo_map(&address2os, &line, &msg_id, f);
        fclose(f);
        if (depth == 0) {
          KMP_EXIT_AFF_NONE;
        }
      }
    }

#endif /* KMP_OS_LINUX */

#if KMP_GROUP_AFFINITY

    if ((depth < 0) && (__kmp_num_proc_groups > 1)) {
      if (__kmp_affinity_verbose) {
        KMP_INFORM(AffWindowsProcGroupMap, "KMP_AFFINITY");
      }

      depth = __kmp_affinity_create_proc_group_map(&address2os, &msg_id);
      KMP_ASSERT(depth != 0);
    }

#endif /* KMP_GROUP_AFFINITY */

    if (depth < 0) {
      if (__kmp_affinity_verbose && (msg_id != kmp_i18n_null)) {
        if (file_name == NULL) {
          KMP_INFORM(UsingFlatOS, __kmp_i18n_catgets(msg_id));
        } else if (line == 0) {
          KMP_INFORM(UsingFlatOSFile, file_name, __kmp_i18n_catgets(msg_id));
        } else {
          KMP_INFORM(UsingFlatOSFileLine, file_name, line,
                     __kmp_i18n_catgets(msg_id));
        }
      }
      // FIXME - print msg if msg_id = kmp_i18n_null ???

      file_name = "";
      depth = __kmp_affinity_create_flat_map(&address2os, &msg_id);
      if (depth == 0) {
        KMP_EXIT_AFF_NONE;
      }
      KMP_ASSERT(depth > 0);
      KMP_ASSERT(address2os != NULL);
    }
  }

#if KMP_USE_HWLOC
  else if (__kmp_affinity_top_method == affinity_top_method_hwloc) {
    KMP_ASSERT(__kmp_affinity_dispatch->get_api_type() == KMPAffinity::HWLOC);
    if (__kmp_affinity_verbose) {
      KMP_INFORM(AffUsingHwloc, "KMP_AFFINITY");
    }
    depth = __kmp_affinity_create_hwloc_map(&address2os, &msg_id);
    if (depth == 0) {
      KMP_EXIT_AFF_NONE;
    }
  }
#endif // KMP_USE_HWLOC

// If the user has specified that a paricular topology discovery method is to be
// used, then we abort if that method fails. The exception is group affinity,
// which might have been implicitly set.

#if KMP_ARCH_X86 || KMP_ARCH_X86_64

  else if (__kmp_affinity_top_method == affinity_top_method_x2apicid) {
    if (__kmp_affinity_verbose) {
      KMP_INFORM(AffInfoStr, "KMP_AFFINITY", KMP_I18N_STR(Decodingx2APIC));
    }

    depth = __kmp_affinity_create_x2apicid_map(&address2os, &msg_id);
    if (depth == 0) {
      KMP_EXIT_AFF_NONE;
    }
    if (depth < 0) {
      KMP_ASSERT(msg_id != kmp_i18n_null);
      KMP_FATAL(MsgExiting, __kmp_i18n_catgets(msg_id));
    }
  } else if (__kmp_affinity_top_method == affinity_top_method_apicid) {
    if (__kmp_affinity_verbose) {
      KMP_INFORM(AffInfoStr, "KMP_AFFINITY", KMP_I18N_STR(DecodingLegacyAPIC));
    }

    depth = __kmp_affinity_create_apicid_map(&address2os, &msg_id);
    if (depth == 0) {
      KMP_EXIT_AFF_NONE;
    }
    if (depth < 0) {
      KMP_ASSERT(msg_id != kmp_i18n_null);
      KMP_FATAL(MsgExiting, __kmp_i18n_catgets(msg_id));
    }
  }

#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

  else if (__kmp_affinity_top_method == affinity_top_method_cpuinfo) {
    const char *filename;
    if (__kmp_cpuinfo_file != NULL) {
      filename = __kmp_cpuinfo_file;
    } else {
      filename = "/proc/cpuinfo";
    }

    if (__kmp_affinity_verbose) {
      KMP_INFORM(AffParseFilename, "KMP_AFFINITY", filename);
    }

    FILE *f = fopen(filename, "r");
    if (f == NULL) {
      int code = errno;
      if (__kmp_cpuinfo_file != NULL) {
        __kmp_fatal(KMP_MSG(CantOpenFileForReading, filename), KMP_ERR(code),
                    KMP_HNT(NameComesFrom_CPUINFO_FILE), __kmp_msg_null);
      } else {
        __kmp_fatal(KMP_MSG(CantOpenFileForReading, filename), KMP_ERR(code),
                    __kmp_msg_null);
      }
    }
    int line = 0;
    depth = __kmp_affinity_create_cpuinfo_map(&address2os, &line, &msg_id, f);
    fclose(f);
    if (depth < 0) {
      KMP_ASSERT(msg_id != kmp_i18n_null);
      if (line > 0) {
        KMP_FATAL(FileLineMsgExiting, filename, line,
                  __kmp_i18n_catgets(msg_id));
      } else {
        KMP_FATAL(FileMsgExiting, filename, __kmp_i18n_catgets(msg_id));
      }
    }
    if (__kmp_affinity_type == affinity_none) {
      KMP_ASSERT(depth == 0);
      KMP_EXIT_AFF_NONE;
    }
  }

#if KMP_GROUP_AFFINITY

  else if (__kmp_affinity_top_method == affinity_top_method_group) {
    if (__kmp_affinity_verbose) {
      KMP_INFORM(AffWindowsProcGroupMap, "KMP_AFFINITY");
    }

    depth = __kmp_affinity_create_proc_group_map(&address2os, &msg_id);
    KMP_ASSERT(depth != 0);
    if (depth < 0) {
      KMP_ASSERT(msg_id != kmp_i18n_null);
      KMP_FATAL(MsgExiting, __kmp_i18n_catgets(msg_id));
    }
  }

#endif /* KMP_GROUP_AFFINITY */

  else if (__kmp_affinity_top_method == affinity_top_method_flat) {
    if (__kmp_affinity_verbose) {
      KMP_INFORM(AffUsingFlatOS, "KMP_AFFINITY");
    }

    depth = __kmp_affinity_create_flat_map(&address2os, &msg_id);
    if (depth == 0) {
      KMP_EXIT_AFF_NONE;
    }
    // should not fail
    KMP_ASSERT(depth > 0);
    KMP_ASSERT(address2os != NULL);
  }

#if KMP_USE_HIER_SCHED
  __kmp_dispatch_set_hierarchy_values();
#endif

  if (address2os == NULL) {
    if (KMP_AFFINITY_CAPABLE() &&
        (__kmp_affinity_verbose ||
         (__kmp_affinity_warnings && (__kmp_affinity_type != affinity_none)))) {
      KMP_WARNING(ErrorInitializeAffinity);
    }
    __kmp_affinity_type = affinity_none;
    __kmp_create_affinity_none_places();
    KMP_AFFINITY_DISABLE();
    return;
  }

  if (__kmp_affinity_gran == affinity_gran_tile
#if KMP_USE_HWLOC
      && __kmp_tile_depth == 0
#endif
      ) {
    // tiles requested but not detected, warn user on this
    KMP_WARNING(AffTilesNoTiles, "KMP_AFFINITY");
  }

  __kmp_apply_thread_places(&address2os, depth);

  // Create the table of masks, indexed by thread Id.
  unsigned maxIndex;
  unsigned numUnique;
  kmp_affin_mask_t *osId2Mask =
      __kmp_create_masks(&maxIndex, &numUnique, address2os, __kmp_avail_proc);
  if (__kmp_affinity_gran_levels == 0) {
    KMP_DEBUG_ASSERT((int)numUnique == __kmp_avail_proc);
  }

  // Set the childNums vector in all Address objects. This must be done before
  // we can sort using __kmp_affinity_cmp_Address_child_num(), which takes into
  // account the setting of __kmp_affinity_compact.
  __kmp_affinity_assign_child_nums(address2os, __kmp_avail_proc);

  switch (__kmp_affinity_type) {

  case affinity_explicit:
    KMP_DEBUG_ASSERT(__kmp_affinity_proclist != NULL);
#if OMP_40_ENABLED
    if (__kmp_nested_proc_bind.bind_types[0] == proc_bind_intel)
#endif
    {
      __kmp_affinity_process_proclist(
          &__kmp_affinity_masks, &__kmp_affinity_num_masks,
          __kmp_affinity_proclist, osId2Mask, maxIndex);
    }
#if OMP_40_ENABLED
    else {
      __kmp_affinity_process_placelist(
          &__kmp_affinity_masks, &__kmp_affinity_num_masks,
          __kmp_affinity_proclist, osId2Mask, maxIndex);
    }
#endif
    if (__kmp_affinity_num_masks == 0) {
      if (__kmp_affinity_verbose ||
          (__kmp_affinity_warnings && (__kmp_affinity_type != affinity_none))) {
        KMP_WARNING(AffNoValidProcID);
      }
      __kmp_affinity_type = affinity_none;
      __kmp_create_affinity_none_places();
      return;
    }
    break;

  // The other affinity types rely on sorting the Addresses according to some
  // permutation of the machine topology tree. Set __kmp_affinity_compact and
  // __kmp_affinity_offset appropriately, then jump to a common code fragment
  // to do the sort and create the array of affinity masks.

  case affinity_logical:
    __kmp_affinity_compact = 0;
    if (__kmp_affinity_offset) {
      __kmp_affinity_offset =
          __kmp_nThreadsPerCore * __kmp_affinity_offset % __kmp_avail_proc;
    }
    goto sortAddresses;

  case affinity_physical:
    if (__kmp_nThreadsPerCore > 1) {
      __kmp_affinity_compact = 1;
      if (__kmp_affinity_compact >= depth) {
        __kmp_affinity_compact = 0;
      }
    } else {
      __kmp_affinity_compact = 0;
    }
    if (__kmp_affinity_offset) {
      __kmp_affinity_offset =
          __kmp_nThreadsPerCore * __kmp_affinity_offset % __kmp_avail_proc;
    }
    goto sortAddresses;

  case affinity_scatter:
    if (__kmp_affinity_compact >= depth) {
      __kmp_affinity_compact = 0;
    } else {
      __kmp_affinity_compact = depth - 1 - __kmp_affinity_compact;
    }
    goto sortAddresses;

  case affinity_compact:
    if (__kmp_affinity_compact >= depth) {
      __kmp_affinity_compact = depth - 1;
    }
    goto sortAddresses;

  case affinity_balanced:
    if (depth <= 1) {
      if (__kmp_affinity_verbose || __kmp_affinity_warnings) {
        KMP_WARNING(AffBalancedNotAvail, "KMP_AFFINITY");
      }
      __kmp_affinity_type = affinity_none;
      __kmp_create_affinity_none_places();
      return;
    } else if (!__kmp_affinity_uniform_topology()) {
      // Save the depth for further usage
      __kmp_aff_depth = depth;

      int core_level = __kmp_affinity_find_core_level(
          address2os, __kmp_avail_proc, depth - 1);
      int ncores = __kmp_affinity_compute_ncores(address2os, __kmp_avail_proc,
                                                 depth - 1, core_level);
      int maxprocpercore = __kmp_affinity_max_proc_per_core(
          address2os, __kmp_avail_proc, depth - 1, core_level);

      int nproc = ncores * maxprocpercore;
      if ((nproc < 2) || (nproc < __kmp_avail_proc)) {
        if (__kmp_affinity_verbose || __kmp_affinity_warnings) {
          KMP_WARNING(AffBalancedNotAvail, "KMP_AFFINITY");
        }
        __kmp_affinity_type = affinity_none;
        return;
      }

      procarr = (int *)__kmp_allocate(sizeof(int) * nproc);
      for (int i = 0; i < nproc; i++) {
        procarr[i] = -1;
      }

      int lastcore = -1;
      int inlastcore = 0;
      for (int i = 0; i < __kmp_avail_proc; i++) {
        int proc = address2os[i].second;
        int core =
            __kmp_affinity_find_core(address2os, i, depth - 1, core_level);

        if (core == lastcore) {
          inlastcore++;
        } else {
          inlastcore = 0;
        }
        lastcore = core;

        procarr[core * maxprocpercore + inlastcore] = proc;
      }
    }
    if (__kmp_affinity_compact >= depth) {
      __kmp_affinity_compact = depth - 1;
    }

  sortAddresses:
    // Allocate the gtid->affinity mask table.
    if (__kmp_affinity_dups) {
      __kmp_affinity_num_masks = __kmp_avail_proc;
    } else {
      __kmp_affinity_num_masks = numUnique;
    }

#if OMP_40_ENABLED
    if ((__kmp_nested_proc_bind.bind_types[0] != proc_bind_intel) &&
        (__kmp_affinity_num_places > 0) &&
        ((unsigned)__kmp_affinity_num_places < __kmp_affinity_num_masks)) {
      __kmp_affinity_num_masks = __kmp_affinity_num_places;
    }
#endif

    KMP_CPU_ALLOC_ARRAY(__kmp_affinity_masks, __kmp_affinity_num_masks);

    // Sort the address2os table according to the current setting of
    // __kmp_affinity_compact, then fill out __kmp_affinity_masks.
    qsort(address2os, __kmp_avail_proc, sizeof(*address2os),
          __kmp_affinity_cmp_Address_child_num);
    {
      int i;
      unsigned j;
      for (i = 0, j = 0; i < __kmp_avail_proc; i++) {
        if ((!__kmp_affinity_dups) && (!address2os[i].first.leader)) {
          continue;
        }
        unsigned osId = address2os[i].second;
        kmp_affin_mask_t *src = KMP_CPU_INDEX(osId2Mask, osId);
        kmp_affin_mask_t *dest = KMP_CPU_INDEX(__kmp_affinity_masks, j);
        KMP_ASSERT(KMP_CPU_ISSET(osId, src));
        KMP_CPU_COPY(dest, src);
        if (++j >= __kmp_affinity_num_masks) {
          break;
        }
      }
      KMP_DEBUG_ASSERT(j == __kmp_affinity_num_masks);
    }
    break;

  default:
    KMP_ASSERT2(0, "Unexpected affinity setting");
  }

  KMP_CPU_FREE_ARRAY(osId2Mask, maxIndex + 1);
  machine_hierarchy.init(address2os, __kmp_avail_proc);
}
#undef KMP_EXIT_AFF_NONE

void __kmp_affinity_initialize(void) {
  // Much of the code above was written assumming that if a machine was not
  // affinity capable, then __kmp_affinity_type == affinity_none.  We now
  // explicitly represent this as __kmp_affinity_type == affinity_disabled.
  // There are too many checks for __kmp_affinity_type == affinity_none
  // in this code.  Instead of trying to change them all, check if
  // __kmp_affinity_type == affinity_disabled, and if so, slam it with
  // affinity_none, call the real initialization routine, then restore
  // __kmp_affinity_type to affinity_disabled.
  int disabled = (__kmp_affinity_type == affinity_disabled);
  if (!KMP_AFFINITY_CAPABLE()) {
    KMP_ASSERT(disabled);
  }
  if (disabled) {
    __kmp_affinity_type = affinity_none;
  }
  __kmp_aux_affinity_initialize();
  if (disabled) {
    __kmp_affinity_type = affinity_disabled;
  }
}

void __kmp_affinity_uninitialize(void) {
  if (__kmp_affinity_masks != NULL) {
    KMP_CPU_FREE_ARRAY(__kmp_affinity_masks, __kmp_affinity_num_masks);
    __kmp_affinity_masks = NULL;
  }
  if (__kmp_affin_fullMask != NULL) {
    KMP_CPU_FREE(__kmp_affin_fullMask);
    __kmp_affin_fullMask = NULL;
  }
  __kmp_affinity_num_masks = 0;
  __kmp_affinity_type = affinity_default;
#if OMP_40_ENABLED
  __kmp_affinity_num_places = 0;
#endif
  if (__kmp_affinity_proclist != NULL) {
    __kmp_free(__kmp_affinity_proclist);
    __kmp_affinity_proclist = NULL;
  }
  if (address2os != NULL) {
    __kmp_free(address2os);
    address2os = NULL;
  }
  if (procarr != NULL) {
    __kmp_free(procarr);
    procarr = NULL;
  }
#if KMP_USE_HWLOC
  if (__kmp_hwloc_topology != NULL) {
    hwloc_topology_destroy(__kmp_hwloc_topology);
    __kmp_hwloc_topology = NULL;
  }
#endif
  KMPAffinity::destroy_api();
}

void __kmp_affinity_set_init_mask(int gtid, int isa_root) {
  if (!KMP_AFFINITY_CAPABLE()) {
    return;
  }

  kmp_info_t *th = (kmp_info_t *)TCR_SYNC_PTR(__kmp_threads[gtid]);
  if (th->th.th_affin_mask == NULL) {
    KMP_CPU_ALLOC(th->th.th_affin_mask);
  } else {
    KMP_CPU_ZERO(th->th.th_affin_mask);
  }

  // Copy the thread mask to the kmp_info_t strucuture. If
  // __kmp_affinity_type == affinity_none, copy the "full" mask, i.e. one that
  // has all of the OS proc ids set, or if __kmp_affinity_respect_mask is set,
  // then the full mask is the same as the mask of the initialization thread.
  kmp_affin_mask_t *mask;
  int i;

#if OMP_40_ENABLED
  if (KMP_AFFINITY_NON_PROC_BIND)
#endif
  {
    if ((__kmp_affinity_type == affinity_none) ||
        (__kmp_affinity_type == affinity_balanced)) {
#if KMP_GROUP_AFFINITY
      if (__kmp_num_proc_groups > 1) {
        return;
      }
#endif
      KMP_ASSERT(__kmp_affin_fullMask != NULL);
      i = 0;
      mask = __kmp_affin_fullMask;
    } else {
      KMP_DEBUG_ASSERT(__kmp_affinity_num_masks > 0);
      i = (gtid + __kmp_affinity_offset) % __kmp_affinity_num_masks;
      mask = KMP_CPU_INDEX(__kmp_affinity_masks, i);
    }
  }
#if OMP_40_ENABLED
  else {
    if ((!isa_root) ||
        (__kmp_nested_proc_bind.bind_types[0] == proc_bind_false)) {
#if KMP_GROUP_AFFINITY
      if (__kmp_num_proc_groups > 1) {
        return;
      }
#endif
      KMP_ASSERT(__kmp_affin_fullMask != NULL);
      i = KMP_PLACE_ALL;
      mask = __kmp_affin_fullMask;
    } else {
      // int i = some hash function or just a counter that doesn't
      // always start at 0.  Use gtid for now.
      KMP_DEBUG_ASSERT(__kmp_affinity_num_masks > 0);
      i = (gtid + __kmp_affinity_offset) % __kmp_affinity_num_masks;
      mask = KMP_CPU_INDEX(__kmp_affinity_masks, i);
    }
  }
#endif

#if OMP_40_ENABLED
  th->th.th_current_place = i;
  if (isa_root) {
    th->th.th_new_place = i;
    th->th.th_first_place = 0;
    th->th.th_last_place = __kmp_affinity_num_masks - 1;
  } else if (KMP_AFFINITY_NON_PROC_BIND) {
    // When using a Non-OMP_PROC_BIND affinity method,
    // set all threads' place-partition-var to the entire place list
    th->th.th_first_place = 0;
    th->th.th_last_place = __kmp_affinity_num_masks - 1;
  }

  if (i == KMP_PLACE_ALL) {
    KA_TRACE(100, ("__kmp_affinity_set_init_mask: binding T#%d to all places\n",
                   gtid));
  } else {
    KA_TRACE(100, ("__kmp_affinity_set_init_mask: binding T#%d to place %d\n",
                   gtid, i));
  }
#else
  if (i == -1) {
    KA_TRACE(
        100,
        ("__kmp_affinity_set_init_mask: binding T#%d to __kmp_affin_fullMask\n",
         gtid));
  } else {
    KA_TRACE(100, ("__kmp_affinity_set_init_mask: binding T#%d to mask %d\n",
                   gtid, i));
  }
#endif /* OMP_40_ENABLED */

  KMP_CPU_COPY(th->th.th_affin_mask, mask);

  if (__kmp_affinity_verbose
      /* to avoid duplicate printing (will be correctly printed on barrier) */
      && (__kmp_affinity_type == affinity_none ||
          (i != KMP_PLACE_ALL && __kmp_affinity_type != affinity_balanced))) {
    char buf[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN,
                              th->th.th_affin_mask);
    KMP_INFORM(BoundToOSProcSet, "KMP_AFFINITY", (kmp_int32)getpid(),
               __kmp_gettid(), gtid, buf);
  }

#if KMP_OS_WINDOWS
  // On Windows* OS, the process affinity mask might have changed. If the user
  // didn't request affinity and this call fails, just continue silently.
  // See CQ171393.
  if (__kmp_affinity_type == affinity_none) {
    __kmp_set_system_affinity(th->th.th_affin_mask, FALSE);
  } else
#endif
    __kmp_set_system_affinity(th->th.th_affin_mask, TRUE);
}

#if OMP_40_ENABLED

void __kmp_affinity_set_place(int gtid) {
  if (!KMP_AFFINITY_CAPABLE()) {
    return;
  }

  kmp_info_t *th = (kmp_info_t *)TCR_SYNC_PTR(__kmp_threads[gtid]);

  KA_TRACE(100, ("__kmp_affinity_set_place: binding T#%d to place %d (current "
                 "place = %d)\n",
                 gtid, th->th.th_new_place, th->th.th_current_place));

  // Check that the new place is within this thread's partition.
  KMP_DEBUG_ASSERT(th->th.th_affin_mask != NULL);
  KMP_ASSERT(th->th.th_new_place >= 0);
  KMP_ASSERT((unsigned)th->th.th_new_place <= __kmp_affinity_num_masks);
  if (th->th.th_first_place <= th->th.th_last_place) {
    KMP_ASSERT((th->th.th_new_place >= th->th.th_first_place) &&
               (th->th.th_new_place <= th->th.th_last_place));
  } else {
    KMP_ASSERT((th->th.th_new_place <= th->th.th_first_place) ||
               (th->th.th_new_place >= th->th.th_last_place));
  }

  // Copy the thread mask to the kmp_info_t strucuture,
  // and set this thread's affinity.
  kmp_affin_mask_t *mask =
      KMP_CPU_INDEX(__kmp_affinity_masks, th->th.th_new_place);
  KMP_CPU_COPY(th->th.th_affin_mask, mask);
  th->th.th_current_place = th->th.th_new_place;

  if (__kmp_affinity_verbose) {
    char buf[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN,
                              th->th.th_affin_mask);
    KMP_INFORM(BoundToOSProcSet, "OMP_PROC_BIND", (kmp_int32)getpid(),
               __kmp_gettid(), gtid, buf);
  }
  __kmp_set_system_affinity(th->th.th_affin_mask, TRUE);
}

#endif /* OMP_40_ENABLED */

int __kmp_aux_set_affinity(void **mask) {
  int gtid;
  kmp_info_t *th;
  int retval;

  if (!KMP_AFFINITY_CAPABLE()) {
    return -1;
  }

  gtid = __kmp_entry_gtid();
  KA_TRACE(1000, ; {
    char buf[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN,
                              (kmp_affin_mask_t *)(*mask));
    __kmp_debug_printf(
        "kmp_set_affinity: setting affinity mask for thread %d = %s\n", gtid,
        buf);
  });

  if (__kmp_env_consistency_check) {
    if ((mask == NULL) || (*mask == NULL)) {
      KMP_FATAL(AffinityInvalidMask, "kmp_set_affinity");
    } else {
      unsigned proc;
      int num_procs = 0;

      KMP_CPU_SET_ITERATE(proc, ((kmp_affin_mask_t *)(*mask))) {
        if (!KMP_CPU_ISSET(proc, __kmp_affin_fullMask)) {
          KMP_FATAL(AffinityInvalidMask, "kmp_set_affinity");
        }
        if (!KMP_CPU_ISSET(proc, (kmp_affin_mask_t *)(*mask))) {
          continue;
        }
        num_procs++;
      }
      if (num_procs == 0) {
        KMP_FATAL(AffinityInvalidMask, "kmp_set_affinity");
      }

#if KMP_GROUP_AFFINITY
      if (__kmp_get_proc_group((kmp_affin_mask_t *)(*mask)) < 0) {
        KMP_FATAL(AffinityInvalidMask, "kmp_set_affinity");
      }
#endif /* KMP_GROUP_AFFINITY */
    }
  }

  th = __kmp_threads[gtid];
  KMP_DEBUG_ASSERT(th->th.th_affin_mask != NULL);
  retval = __kmp_set_system_affinity((kmp_affin_mask_t *)(*mask), FALSE);
  if (retval == 0) {
    KMP_CPU_COPY(th->th.th_affin_mask, (kmp_affin_mask_t *)(*mask));
  }

#if OMP_40_ENABLED
  th->th.th_current_place = KMP_PLACE_UNDEFINED;
  th->th.th_new_place = KMP_PLACE_UNDEFINED;
  th->th.th_first_place = 0;
  th->th.th_last_place = __kmp_affinity_num_masks - 1;

  // Turn off 4.0 affinity for the current tread at this parallel level.
  th->th.th_current_task->td_icvs.proc_bind = proc_bind_false;
#endif

  return retval;
}

int __kmp_aux_get_affinity(void **mask) {
  int gtid;
  int retval;
  kmp_info_t *th;

  if (!KMP_AFFINITY_CAPABLE()) {
    return -1;
  }

  gtid = __kmp_entry_gtid();
  th = __kmp_threads[gtid];
  KMP_DEBUG_ASSERT(th->th.th_affin_mask != NULL);

  KA_TRACE(1000, ; {
    char buf[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN,
                              th->th.th_affin_mask);
    __kmp_printf("kmp_get_affinity: stored affinity mask for thread %d = %s\n",
                 gtid, buf);
  });

  if (__kmp_env_consistency_check) {
    if ((mask == NULL) || (*mask == NULL)) {
      KMP_FATAL(AffinityInvalidMask, "kmp_get_affinity");
    }
  }

#if !KMP_OS_WINDOWS

  retval = __kmp_get_system_affinity((kmp_affin_mask_t *)(*mask), FALSE);
  KA_TRACE(1000, ; {
    char buf[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN,
                              (kmp_affin_mask_t *)(*mask));
    __kmp_printf("kmp_get_affinity: system affinity mask for thread %d = %s\n",
                 gtid, buf);
  });
  return retval;

#else

  KMP_CPU_COPY((kmp_affin_mask_t *)(*mask), th->th.th_affin_mask);
  return 0;

#endif /* KMP_OS_WINDOWS */
}

int __kmp_aux_get_affinity_max_proc() {
  if (!KMP_AFFINITY_CAPABLE()) {
    return 0;
  }
#if KMP_GROUP_AFFINITY
  if (__kmp_num_proc_groups > 1) {
    return (int)(__kmp_num_proc_groups * sizeof(DWORD_PTR) * CHAR_BIT);
  }
#endif
  return __kmp_xproc;
}

int __kmp_aux_set_affinity_mask_proc(int proc, void **mask) {
  if (!KMP_AFFINITY_CAPABLE()) {
    return -1;
  }

  KA_TRACE(1000, ; {
    int gtid = __kmp_entry_gtid();
    char buf[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN,
                              (kmp_affin_mask_t *)(*mask));
    __kmp_debug_printf("kmp_set_affinity_mask_proc: setting proc %d in "
                       "affinity mask for thread %d = %s\n",
                       proc, gtid, buf);
  });

  if (__kmp_env_consistency_check) {
    if ((mask == NULL) || (*mask == NULL)) {
      KMP_FATAL(AffinityInvalidMask, "kmp_set_affinity_mask_proc");
    }
  }

  if ((proc < 0) || (proc >= __kmp_aux_get_affinity_max_proc())) {
    return -1;
  }
  if (!KMP_CPU_ISSET(proc, __kmp_affin_fullMask)) {
    return -2;
  }

  KMP_CPU_SET(proc, (kmp_affin_mask_t *)(*mask));
  return 0;
}

int __kmp_aux_unset_affinity_mask_proc(int proc, void **mask) {
  if (!KMP_AFFINITY_CAPABLE()) {
    return -1;
  }

  KA_TRACE(1000, ; {
    int gtid = __kmp_entry_gtid();
    char buf[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN,
                              (kmp_affin_mask_t *)(*mask));
    __kmp_debug_printf("kmp_unset_affinity_mask_proc: unsetting proc %d in "
                       "affinity mask for thread %d = %s\n",
                       proc, gtid, buf);
  });

  if (__kmp_env_consistency_check) {
    if ((mask == NULL) || (*mask == NULL)) {
      KMP_FATAL(AffinityInvalidMask, "kmp_unset_affinity_mask_proc");
    }
  }

  if ((proc < 0) || (proc >= __kmp_aux_get_affinity_max_proc())) {
    return -1;
  }
  if (!KMP_CPU_ISSET(proc, __kmp_affin_fullMask)) {
    return -2;
  }

  KMP_CPU_CLR(proc, (kmp_affin_mask_t *)(*mask));
  return 0;
}

int __kmp_aux_get_affinity_mask_proc(int proc, void **mask) {
  if (!KMP_AFFINITY_CAPABLE()) {
    return -1;
  }

  KA_TRACE(1000, ; {
    int gtid = __kmp_entry_gtid();
    char buf[KMP_AFFIN_MASK_PRINT_LEN];
    __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN,
                              (kmp_affin_mask_t *)(*mask));
    __kmp_debug_printf("kmp_get_affinity_mask_proc: getting proc %d in "
                       "affinity mask for thread %d = %s\n",
                       proc, gtid, buf);
  });

  if (__kmp_env_consistency_check) {
    if ((mask == NULL) || (*mask == NULL)) {
      KMP_FATAL(AffinityInvalidMask, "kmp_get_affinity_mask_proc");
    }
  }

  if ((proc < 0) || (proc >= __kmp_aux_get_affinity_max_proc())) {
    return -1;
  }
  if (!KMP_CPU_ISSET(proc, __kmp_affin_fullMask)) {
    return 0;
  }

  return KMP_CPU_ISSET(proc, (kmp_affin_mask_t *)(*mask));
}

// Dynamic affinity settings - Affinity balanced
void __kmp_balanced_affinity(kmp_info_t *th, int nthreads) {
  KMP_DEBUG_ASSERT(th);
  bool fine_gran = true;
  int tid = th->th.th_info.ds.ds_tid;

  switch (__kmp_affinity_gran) {
  case affinity_gran_fine:
  case affinity_gran_thread:
    break;
  case affinity_gran_core:
    if (__kmp_nThreadsPerCore > 1) {
      fine_gran = false;
    }
    break;
  case affinity_gran_package:
    if (nCoresPerPkg > 1) {
      fine_gran = false;
    }
    break;
  default:
    fine_gran = false;
  }

  if (__kmp_affinity_uniform_topology()) {
    int coreID;
    int threadID;
    // Number of hyper threads per core in HT machine
    int __kmp_nth_per_core = __kmp_avail_proc / __kmp_ncores;
    // Number of cores
    int ncores = __kmp_ncores;
    if ((nPackages > 1) && (__kmp_nth_per_core <= 1)) {
      __kmp_nth_per_core = __kmp_avail_proc / nPackages;
      ncores = nPackages;
    }
    // How many threads will be bound to each core
    int chunk = nthreads / ncores;
    // How many cores will have an additional thread bound to it - "big cores"
    int big_cores = nthreads % ncores;
    // Number of threads on the big cores
    int big_nth = (chunk + 1) * big_cores;
    if (tid < big_nth) {
      coreID = tid / (chunk + 1);
      threadID = (tid % (chunk + 1)) % __kmp_nth_per_core;
    } else { // tid >= big_nth
      coreID = (tid - big_cores) / chunk;
      threadID = ((tid - big_cores) % chunk) % __kmp_nth_per_core;
    }

    KMP_DEBUG_ASSERT2(KMP_AFFINITY_CAPABLE(),
                      "Illegal set affinity operation when not capable");

    kmp_affin_mask_t *mask = th->th.th_affin_mask;
    KMP_CPU_ZERO(mask);

    if (fine_gran) {
      int osID = address2os[coreID * __kmp_nth_per_core + threadID].second;
      KMP_CPU_SET(osID, mask);
    } else {
      for (int i = 0; i < __kmp_nth_per_core; i++) {
        int osID;
        osID = address2os[coreID * __kmp_nth_per_core + i].second;
        KMP_CPU_SET(osID, mask);
      }
    }
    if (__kmp_affinity_verbose) {
      char buf[KMP_AFFIN_MASK_PRINT_LEN];
      __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN, mask);
      KMP_INFORM(BoundToOSProcSet, "KMP_AFFINITY", (kmp_int32)getpid(),
                 __kmp_gettid(), tid, buf);
    }
    __kmp_set_system_affinity(mask, TRUE);
  } else { // Non-uniform topology

    kmp_affin_mask_t *mask = th->th.th_affin_mask;
    KMP_CPU_ZERO(mask);

    int core_level = __kmp_affinity_find_core_level(
        address2os, __kmp_avail_proc, __kmp_aff_depth - 1);
    int ncores = __kmp_affinity_compute_ncores(address2os, __kmp_avail_proc,
                                               __kmp_aff_depth - 1, core_level);
    int nth_per_core = __kmp_affinity_max_proc_per_core(
        address2os, __kmp_avail_proc, __kmp_aff_depth - 1, core_level);

    // For performance gain consider the special case nthreads ==
    // __kmp_avail_proc
    if (nthreads == __kmp_avail_proc) {
      if (fine_gran) {
        int osID = address2os[tid].second;
        KMP_CPU_SET(osID, mask);
      } else {
        int core = __kmp_affinity_find_core(address2os, tid,
                                            __kmp_aff_depth - 1, core_level);
        for (int i = 0; i < __kmp_avail_proc; i++) {
          int osID = address2os[i].second;
          if (__kmp_affinity_find_core(address2os, i, __kmp_aff_depth - 1,
                                       core_level) == core) {
            KMP_CPU_SET(osID, mask);
          }
        }
      }
    } else if (nthreads <= ncores) {

      int core = 0;
      for (int i = 0; i < ncores; i++) {
        // Check if this core from procarr[] is in the mask
        int in_mask = 0;
        for (int j = 0; j < nth_per_core; j++) {
          if (procarr[i * nth_per_core + j] != -1) {
            in_mask = 1;
            break;
          }
        }
        if (in_mask) {
          if (tid == core) {
            for (int j = 0; j < nth_per_core; j++) {
              int osID = procarr[i * nth_per_core + j];
              if (osID != -1) {
                KMP_CPU_SET(osID, mask);
                // For fine granularity it is enough to set the first available
                // osID for this core
                if (fine_gran) {
                  break;
                }
              }
            }
            break;
          } else {
            core++;
          }
        }
      }
    } else { // nthreads > ncores
      // Array to save the number of processors at each core
      int *nproc_at_core = (int *)KMP_ALLOCA(sizeof(int) * ncores);
      // Array to save the number of cores with "x" available processors;
      int *ncores_with_x_procs =
          (int *)KMP_ALLOCA(sizeof(int) * (nth_per_core + 1));
      // Array to save the number of cores with # procs from x to nth_per_core
      int *ncores_with_x_to_max_procs =
          (int *)KMP_ALLOCA(sizeof(int) * (nth_per_core + 1));

      for (int i = 0; i <= nth_per_core; i++) {
        ncores_with_x_procs[i] = 0;
        ncores_with_x_to_max_procs[i] = 0;
      }

      for (int i = 0; i < ncores; i++) {
        int cnt = 0;
        for (int j = 0; j < nth_per_core; j++) {
          if (procarr[i * nth_per_core + j] != -1) {
            cnt++;
          }
        }
        nproc_at_core[i] = cnt;
        ncores_with_x_procs[cnt]++;
      }

      for (int i = 0; i <= nth_per_core; i++) {
        for (int j = i; j <= nth_per_core; j++) {
          ncores_with_x_to_max_procs[i] += ncores_with_x_procs[j];
        }
      }

      // Max number of processors
      int nproc = nth_per_core * ncores;
      // An array to keep number of threads per each context
      int *newarr = (int *)__kmp_allocate(sizeof(int) * nproc);
      for (int i = 0; i < nproc; i++) {
        newarr[i] = 0;
      }

      int nth = nthreads;
      int flag = 0;
      while (nth > 0) {
        for (int j = 1; j <= nth_per_core; j++) {
          int cnt = ncores_with_x_to_max_procs[j];
          for (int i = 0; i < ncores; i++) {
            // Skip the core with 0 processors
            if (nproc_at_core[i] == 0) {
              continue;
            }
            for (int k = 0; k < nth_per_core; k++) {
              if (procarr[i * nth_per_core + k] != -1) {
                if (newarr[i * nth_per_core + k] == 0) {
                  newarr[i * nth_per_core + k] = 1;
                  cnt--;
                  nth--;
                  break;
                } else {
                  if (flag != 0) {
                    newarr[i * nth_per_core + k]++;
                    cnt--;
                    nth--;
                    break;
                  }
                }
              }
            }
            if (cnt == 0 || nth == 0) {
              break;
            }
          }
          if (nth == 0) {
            break;
          }
        }
        flag = 1;
      }
      int sum = 0;
      for (int i = 0; i < nproc; i++) {
        sum += newarr[i];
        if (sum > tid) {
          if (fine_gran) {
            int osID = procarr[i];
            KMP_CPU_SET(osID, mask);
          } else {
            int coreID = i / nth_per_core;
            for (int ii = 0; ii < nth_per_core; ii++) {
              int osID = procarr[coreID * nth_per_core + ii];
              if (osID != -1) {
                KMP_CPU_SET(osID, mask);
              }
            }
          }
          break;
        }
      }
      __kmp_free(newarr);
    }

    if (__kmp_affinity_verbose) {
      char buf[KMP_AFFIN_MASK_PRINT_LEN];
      __kmp_affinity_print_mask(buf, KMP_AFFIN_MASK_PRINT_LEN, mask);
      KMP_INFORM(BoundToOSProcSet, "KMP_AFFINITY", (kmp_int32)getpid(),
                 __kmp_gettid(), tid, buf);
    }
    __kmp_set_system_affinity(mask, TRUE);
  }
}

#if KMP_OS_LINUX
// We don't need this entry for Windows because
// there is GetProcessAffinityMask() api
//
// The intended usage is indicated by these steps:
// 1) The user gets the current affinity mask
// 2) Then sets the affinity by calling this function
// 3) Error check the return value
// 4) Use non-OpenMP parallelization
// 5) Reset the affinity to what was stored in step 1)
#ifdef __cplusplus
extern "C"
#endif
    int
    kmp_set_thread_affinity_mask_initial()
// the function returns 0 on success,
//   -1 if we cannot bind thread
//   >0 (errno) if an error happened during binding
{
  int gtid = __kmp_get_gtid();
  if (gtid < 0) {
    // Do not touch non-omp threads
    KA_TRACE(30, ("kmp_set_thread_affinity_mask_initial: "
                  "non-omp thread, returning\n"));
    return -1;
  }
  if (!KMP_AFFINITY_CAPABLE() || !__kmp_init_middle) {
    KA_TRACE(30, ("kmp_set_thread_affinity_mask_initial: "
                  "affinity not initialized, returning\n"));
    return -1;
  }
  KA_TRACE(30, ("kmp_set_thread_affinity_mask_initial: "
                "set full mask for thread %d\n",
                gtid));
  KMP_DEBUG_ASSERT(__kmp_affin_fullMask != NULL);
  return __kmp_set_system_affinity(__kmp_affin_fullMask, FALSE);
}
#endif

#endif // KMP_AFFINITY_SUPPORTED
