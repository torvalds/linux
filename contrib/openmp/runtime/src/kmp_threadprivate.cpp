/*
 * kmp_threadprivate.cpp -- OpenMP threadprivate support library
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
#include "kmp_i18n.h"
#include "kmp_itt.h"

#define USE_CHECKS_COMMON

#define KMP_INLINE_SUBR 1

void kmp_threadprivate_insert_private_data(int gtid, void *pc_addr,
                                           void *data_addr, size_t pc_size);
struct private_common *kmp_threadprivate_insert(int gtid, void *pc_addr,
                                                void *data_addr,
                                                size_t pc_size);

struct shared_table __kmp_threadprivate_d_table;

static
#ifdef KMP_INLINE_SUBR
    __forceinline
#endif
    struct private_common *
    __kmp_threadprivate_find_task_common(struct common_table *tbl, int gtid,
                                         void *pc_addr)

{
  struct private_common *tn;

#ifdef KMP_TASK_COMMON_DEBUG
  KC_TRACE(10, ("__kmp_threadprivate_find_task_common: thread#%d, called with "
                "address %p\n",
                gtid, pc_addr));
  dump_list();
#endif

  for (tn = tbl->data[KMP_HASH(pc_addr)]; tn; tn = tn->next) {
    if (tn->gbl_addr == pc_addr) {
#ifdef KMP_TASK_COMMON_DEBUG
      KC_TRACE(10, ("__kmp_threadprivate_find_task_common: thread#%d, found "
                    "node %p on list\n",
                    gtid, pc_addr));
#endif
      return tn;
    }
  }
  return 0;
}

static
#ifdef KMP_INLINE_SUBR
    __forceinline
#endif
    struct shared_common *
    __kmp_find_shared_task_common(struct shared_table *tbl, int gtid,
                                  void *pc_addr) {
  struct shared_common *tn;

  for (tn = tbl->data[KMP_HASH(pc_addr)]; tn; tn = tn->next) {
    if (tn->gbl_addr == pc_addr) {
#ifdef KMP_TASK_COMMON_DEBUG
      KC_TRACE(
          10,
          ("__kmp_find_shared_task_common: thread#%d, found node %p on list\n",
           gtid, pc_addr));
#endif
      return tn;
    }
  }
  return 0;
}

// Create a template for the data initialized storage. Either the template is
// NULL indicating zero fill, or the template is a copy of the original data.
static struct private_data *__kmp_init_common_data(void *pc_addr,
                                                   size_t pc_size) {
  struct private_data *d;
  size_t i;
  char *p;

  d = (struct private_data *)__kmp_allocate(sizeof(struct private_data));
  /*
      d->data = 0;  // AC: commented out because __kmp_allocate zeroes the
     memory
      d->next = 0;
  */
  d->size = pc_size;
  d->more = 1;

  p = (char *)pc_addr;

  for (i = pc_size; i > 0; --i) {
    if (*p++ != '\0') {
      d->data = __kmp_allocate(pc_size);
      KMP_MEMCPY(d->data, pc_addr, pc_size);
      break;
    }
  }

  return d;
}

// Initialize the data area from the template.
static void __kmp_copy_common_data(void *pc_addr, struct private_data *d) {
  char *addr = (char *)pc_addr;
  int i, offset;

  for (offset = 0; d != 0; d = d->next) {
    for (i = d->more; i > 0; --i) {
      if (d->data == 0)
        memset(&addr[offset], '\0', d->size);
      else
        KMP_MEMCPY(&addr[offset], d->data, d->size);
      offset += d->size;
    }
  }
}

/* we are called from __kmp_serial_initialize() with __kmp_initz_lock held. */
void __kmp_common_initialize(void) {
  if (!TCR_4(__kmp_init_common)) {
    int q;
#ifdef KMP_DEBUG
    int gtid;
#endif

    __kmp_threadpriv_cache_list = NULL;

#ifdef KMP_DEBUG
    /* verify the uber masters were initialized */
    for (gtid = 0; gtid < __kmp_threads_capacity; gtid++)
      if (__kmp_root[gtid]) {
        KMP_DEBUG_ASSERT(__kmp_root[gtid]->r.r_uber_thread);
        for (q = 0; q < KMP_HASH_TABLE_SIZE; ++q)
          KMP_DEBUG_ASSERT(
              !__kmp_root[gtid]->r.r_uber_thread->th.th_pri_common->data[q]);
        /*                    __kmp_root[ gitd ]-> r.r_uber_thread ->
         * th.th_pri_common -> data[ q ] = 0;*/
      }
#endif /* KMP_DEBUG */

    for (q = 0; q < KMP_HASH_TABLE_SIZE; ++q)
      __kmp_threadprivate_d_table.data[q] = 0;

    TCW_4(__kmp_init_common, TRUE);
  }
}

/* Call all destructors for threadprivate data belonging to all threads.
   Currently unused! */
void __kmp_common_destroy(void) {
  if (TCR_4(__kmp_init_common)) {
    int q;

    TCW_4(__kmp_init_common, FALSE);

    for (q = 0; q < KMP_HASH_TABLE_SIZE; ++q) {
      int gtid;
      struct private_common *tn;
      struct shared_common *d_tn;

      /* C++ destructors need to be called once per thread before exiting.
         Don't call destructors for master thread though unless we used copy
         constructor */

      for (d_tn = __kmp_threadprivate_d_table.data[q]; d_tn;
           d_tn = d_tn->next) {
        if (d_tn->is_vec) {
          if (d_tn->dt.dtorv != 0) {
            for (gtid = 0; gtid < __kmp_all_nth; ++gtid) {
              if (__kmp_threads[gtid]) {
                if ((__kmp_foreign_tp) ? (!KMP_INITIAL_GTID(gtid))
                                       : (!KMP_UBER_GTID(gtid))) {
                  tn = __kmp_threadprivate_find_task_common(
                      __kmp_threads[gtid]->th.th_pri_common, gtid,
                      d_tn->gbl_addr);
                  if (tn) {
                    (*d_tn->dt.dtorv)(tn->par_addr, d_tn->vec_len);
                  }
                }
              }
            }
            if (d_tn->obj_init != 0) {
              (*d_tn->dt.dtorv)(d_tn->obj_init, d_tn->vec_len);
            }
          }
        } else {
          if (d_tn->dt.dtor != 0) {
            for (gtid = 0; gtid < __kmp_all_nth; ++gtid) {
              if (__kmp_threads[gtid]) {
                if ((__kmp_foreign_tp) ? (!KMP_INITIAL_GTID(gtid))
                                       : (!KMP_UBER_GTID(gtid))) {
                  tn = __kmp_threadprivate_find_task_common(
                      __kmp_threads[gtid]->th.th_pri_common, gtid,
                      d_tn->gbl_addr);
                  if (tn) {
                    (*d_tn->dt.dtor)(tn->par_addr);
                  }
                }
              }
            }
            if (d_tn->obj_init != 0) {
              (*d_tn->dt.dtor)(d_tn->obj_init);
            }
          }
        }
      }
      __kmp_threadprivate_d_table.data[q] = 0;
    }
  }
}

/* Call all destructors for threadprivate data belonging to this thread */
void __kmp_common_destroy_gtid(int gtid) {
  struct private_common *tn;
  struct shared_common *d_tn;

  if (!TCR_4(__kmp_init_gtid)) {
    // This is possible when one of multiple roots initiates early library
    // termination in a sequential region while other teams are active, and its
    // child threads are about to end.
    return;
  }

  KC_TRACE(10, ("__kmp_common_destroy_gtid: T#%d called\n", gtid));
  if ((__kmp_foreign_tp) ? (!KMP_INITIAL_GTID(gtid)) : (!KMP_UBER_GTID(gtid))) {

    if (TCR_4(__kmp_init_common)) {

      /* Cannot do this here since not all threads have destroyed their data */
      /* TCW_4(__kmp_init_common, FALSE); */

      for (tn = __kmp_threads[gtid]->th.th_pri_head; tn; tn = tn->link) {

        d_tn = __kmp_find_shared_task_common(&__kmp_threadprivate_d_table, gtid,
                                             tn->gbl_addr);

        KMP_DEBUG_ASSERT(d_tn);

        if (d_tn->is_vec) {
          if (d_tn->dt.dtorv != 0) {
            (void)(*d_tn->dt.dtorv)(tn->par_addr, d_tn->vec_len);
          }
          if (d_tn->obj_init != 0) {
            (void)(*d_tn->dt.dtorv)(d_tn->obj_init, d_tn->vec_len);
          }
        } else {
          if (d_tn->dt.dtor != 0) {
            (void)(*d_tn->dt.dtor)(tn->par_addr);
          }
          if (d_tn->obj_init != 0) {
            (void)(*d_tn->dt.dtor)(d_tn->obj_init);
          }
        }
      }
      KC_TRACE(30, ("__kmp_common_destroy_gtid: T#%d threadprivate destructors "
                    "complete\n",
                    gtid));
    }
  }
}

#ifdef KMP_TASK_COMMON_DEBUG
static void dump_list(void) {
  int p, q;

  for (p = 0; p < __kmp_all_nth; ++p) {
    if (!__kmp_threads[p])
      continue;
    for (q = 0; q < KMP_HASH_TABLE_SIZE; ++q) {
      if (__kmp_threads[p]->th.th_pri_common->data[q]) {
        struct private_common *tn;

        KC_TRACE(10, ("\tdump_list: gtid:%d addresses\n", p));

        for (tn = __kmp_threads[p]->th.th_pri_common->data[q]; tn;
             tn = tn->next) {
          KC_TRACE(10,
                   ("\tdump_list: THREADPRIVATE: Serial %p -> Parallel %p\n",
                    tn->gbl_addr, tn->par_addr));
        }
      }
    }
  }
}
#endif /* KMP_TASK_COMMON_DEBUG */

// NOTE: this routine is to be called only from the serial part of the program.
void kmp_threadprivate_insert_private_data(int gtid, void *pc_addr,
                                           void *data_addr, size_t pc_size) {
  struct shared_common **lnk_tn, *d_tn;
  KMP_DEBUG_ASSERT(__kmp_threads[gtid] &&
                   __kmp_threads[gtid]->th.th_root->r.r_active == 0);

  d_tn = __kmp_find_shared_task_common(&__kmp_threadprivate_d_table, gtid,
                                       pc_addr);

  if (d_tn == 0) {
    d_tn = (struct shared_common *)__kmp_allocate(sizeof(struct shared_common));

    d_tn->gbl_addr = pc_addr;
    d_tn->pod_init = __kmp_init_common_data(data_addr, pc_size);
    /*
            d_tn->obj_init = 0;  // AC: commented out because __kmp_allocate
       zeroes the memory
            d_tn->ct.ctor = 0;
            d_tn->cct.cctor = 0;;
            d_tn->dt.dtor = 0;
            d_tn->is_vec = FALSE;
            d_tn->vec_len = 0L;
    */
    d_tn->cmn_size = pc_size;

    __kmp_acquire_lock(&__kmp_global_lock, gtid);

    lnk_tn = &(__kmp_threadprivate_d_table.data[KMP_HASH(pc_addr)]);

    d_tn->next = *lnk_tn;
    *lnk_tn = d_tn;

    __kmp_release_lock(&__kmp_global_lock, gtid);
  }
}

struct private_common *kmp_threadprivate_insert(int gtid, void *pc_addr,
                                                void *data_addr,
                                                size_t pc_size) {
  struct private_common *tn, **tt;
  struct shared_common *d_tn;

  /* +++++++++ START OF CRITICAL SECTION +++++++++ */
  __kmp_acquire_lock(&__kmp_global_lock, gtid);

  tn = (struct private_common *)__kmp_allocate(sizeof(struct private_common));

  tn->gbl_addr = pc_addr;

  d_tn = __kmp_find_shared_task_common(
      &__kmp_threadprivate_d_table, gtid,
      pc_addr); /* Only the MASTER data table exists. */

  if (d_tn != 0) {
    /* This threadprivate variable has already been seen. */

    if (d_tn->pod_init == 0 && d_tn->obj_init == 0) {
      d_tn->cmn_size = pc_size;

      if (d_tn->is_vec) {
        if (d_tn->ct.ctorv != 0) {
          /* Construct from scratch so no prototype exists */
          d_tn->obj_init = 0;
        } else if (d_tn->cct.cctorv != 0) {
          /* Now data initialize the prototype since it was previously
           * registered */
          d_tn->obj_init = (void *)__kmp_allocate(d_tn->cmn_size);
          (void)(*d_tn->cct.cctorv)(d_tn->obj_init, pc_addr, d_tn->vec_len);
        } else {
          d_tn->pod_init = __kmp_init_common_data(data_addr, d_tn->cmn_size);
        }
      } else {
        if (d_tn->ct.ctor != 0) {
          /* Construct from scratch so no prototype exists */
          d_tn->obj_init = 0;
        } else if (d_tn->cct.cctor != 0) {
          /* Now data initialize the prototype since it was previously
             registered */
          d_tn->obj_init = (void *)__kmp_allocate(d_tn->cmn_size);
          (void)(*d_tn->cct.cctor)(d_tn->obj_init, pc_addr);
        } else {
          d_tn->pod_init = __kmp_init_common_data(data_addr, d_tn->cmn_size);
        }
      }
    }
  } else {
    struct shared_common **lnk_tn;

    d_tn = (struct shared_common *)__kmp_allocate(sizeof(struct shared_common));
    d_tn->gbl_addr = pc_addr;
    d_tn->cmn_size = pc_size;
    d_tn->pod_init = __kmp_init_common_data(data_addr, pc_size);
    /*
            d_tn->obj_init = 0;  // AC: commented out because __kmp_allocate
       zeroes the memory
            d_tn->ct.ctor = 0;
            d_tn->cct.cctor = 0;
            d_tn->dt.dtor = 0;
            d_tn->is_vec = FALSE;
            d_tn->vec_len = 0L;
    */
    lnk_tn = &(__kmp_threadprivate_d_table.data[KMP_HASH(pc_addr)]);

    d_tn->next = *lnk_tn;
    *lnk_tn = d_tn;
  }

  tn->cmn_size = d_tn->cmn_size;

  if ((__kmp_foreign_tp) ? (KMP_INITIAL_GTID(gtid)) : (KMP_UBER_GTID(gtid))) {
    tn->par_addr = (void *)pc_addr;
  } else {
    tn->par_addr = (void *)__kmp_allocate(tn->cmn_size);
  }

  __kmp_release_lock(&__kmp_global_lock, gtid);
/* +++++++++ END OF CRITICAL SECTION +++++++++ */

#ifdef USE_CHECKS_COMMON
  if (pc_size > d_tn->cmn_size) {
    KC_TRACE(
        10, ("__kmp_threadprivate_insert: THREADPRIVATE: %p (%" KMP_UINTPTR_SPEC
             " ,%" KMP_UINTPTR_SPEC ")\n",
             pc_addr, pc_size, d_tn->cmn_size));
    KMP_FATAL(TPCommonBlocksInconsist);
  }
#endif /* USE_CHECKS_COMMON */

  tt = &(__kmp_threads[gtid]->th.th_pri_common->data[KMP_HASH(pc_addr)]);

#ifdef KMP_TASK_COMMON_DEBUG
  if (*tt != 0) {
    KC_TRACE(
        10,
        ("__kmp_threadprivate_insert: WARNING! thread#%d: collision on %p\n",
         gtid, pc_addr));
  }
#endif
  tn->next = *tt;
  *tt = tn;

#ifdef KMP_TASK_COMMON_DEBUG
  KC_TRACE(10,
           ("__kmp_threadprivate_insert: thread#%d, inserted node %p on list\n",
            gtid, pc_addr));
  dump_list();
#endif

  /* Link the node into a simple list */

  tn->link = __kmp_threads[gtid]->th.th_pri_head;
  __kmp_threads[gtid]->th.th_pri_head = tn;

  if ((__kmp_foreign_tp) ? (KMP_INITIAL_GTID(gtid)) : (KMP_UBER_GTID(gtid)))
    return tn;

  /* if C++ object with copy constructor, use it;
   * else if C++ object with constructor, use it for the non-master copies only;
   * else use pod_init and memcpy
   *
   * C++ constructors need to be called once for each non-master thread on
   * allocate
   * C++ copy constructors need to be called once for each thread on allocate */

  /* C++ object with constructors/destructors; don't call constructors for
     master thread though */
  if (d_tn->is_vec) {
    if (d_tn->ct.ctorv != 0) {
      (void)(*d_tn->ct.ctorv)(tn->par_addr, d_tn->vec_len);
    } else if (d_tn->cct.cctorv != 0) {
      (void)(*d_tn->cct.cctorv)(tn->par_addr, d_tn->obj_init, d_tn->vec_len);
    } else if (tn->par_addr != tn->gbl_addr) {
      __kmp_copy_common_data(tn->par_addr, d_tn->pod_init);
    }
  } else {
    if (d_tn->ct.ctor != 0) {
      (void)(*d_tn->ct.ctor)(tn->par_addr);
    } else if (d_tn->cct.cctor != 0) {
      (void)(*d_tn->cct.cctor)(tn->par_addr, d_tn->obj_init);
    } else if (tn->par_addr != tn->gbl_addr) {
      __kmp_copy_common_data(tn->par_addr, d_tn->pod_init);
    }
  }
  /* !BUILD_OPENMP_C
      if (tn->par_addr != tn->gbl_addr)
          __kmp_copy_common_data( tn->par_addr, d_tn->pod_init ); */

  return tn;
}

/* ------------------------------------------------------------------------ */
/* We are currently parallel, and we know the thread id.                    */
/* ------------------------------------------------------------------------ */

/*!
 @ingroup THREADPRIVATE

 @param loc source location information
 @param data  pointer to data being privatized
 @param ctor  pointer to constructor function for data
 @param cctor  pointer to copy constructor function for data
 @param dtor  pointer to destructor function for data

 Register constructors and destructors for thread private data.
 This function is called when executing in parallel, when we know the thread id.
*/
void __kmpc_threadprivate_register(ident_t *loc, void *data, kmpc_ctor ctor,
                                   kmpc_cctor cctor, kmpc_dtor dtor) {
  struct shared_common *d_tn, **lnk_tn;

  KC_TRACE(10, ("__kmpc_threadprivate_register: called\n"));

#ifdef USE_CHECKS_COMMON
  /* copy constructor must be zero for current code gen (Nov 2002 - jph) */
  KMP_ASSERT(cctor == 0);
#endif /* USE_CHECKS_COMMON */

  /* Only the global data table exists. */
  d_tn = __kmp_find_shared_task_common(&__kmp_threadprivate_d_table, -1, data);

  if (d_tn == 0) {
    d_tn = (struct shared_common *)__kmp_allocate(sizeof(struct shared_common));
    d_tn->gbl_addr = data;

    d_tn->ct.ctor = ctor;
    d_tn->cct.cctor = cctor;
    d_tn->dt.dtor = dtor;
    /*
            d_tn->is_vec = FALSE;  // AC: commented out because __kmp_allocate
       zeroes the memory
            d_tn->vec_len = 0L;
            d_tn->obj_init = 0;
            d_tn->pod_init = 0;
    */
    lnk_tn = &(__kmp_threadprivate_d_table.data[KMP_HASH(data)]);

    d_tn->next = *lnk_tn;
    *lnk_tn = d_tn;
  }
}

void *__kmpc_threadprivate(ident_t *loc, kmp_int32 global_tid, void *data,
                           size_t size) {
  void *ret;
  struct private_common *tn;

  KC_TRACE(10, ("__kmpc_threadprivate: T#%d called\n", global_tid));

#ifdef USE_CHECKS_COMMON
  if (!__kmp_init_serial)
    KMP_FATAL(RTLNotInitialized);
#endif /* USE_CHECKS_COMMON */

  if (!__kmp_threads[global_tid]->th.th_root->r.r_active && !__kmp_foreign_tp) {
    /* The parallel address will NEVER overlap with the data_address */
    /* dkp: 3rd arg to kmp_threadprivate_insert_private_data() is the
     * data_address; use data_address = data */

    KC_TRACE(20, ("__kmpc_threadprivate: T#%d inserting private data\n",
                  global_tid));
    kmp_threadprivate_insert_private_data(global_tid, data, data, size);

    ret = data;
  } else {
    KC_TRACE(
        50,
        ("__kmpc_threadprivate: T#%d try to find private data at address %p\n",
         global_tid, data));
    tn = __kmp_threadprivate_find_task_common(
        __kmp_threads[global_tid]->th.th_pri_common, global_tid, data);

    if (tn) {
      KC_TRACE(20, ("__kmpc_threadprivate: T#%d found data\n", global_tid));
#ifdef USE_CHECKS_COMMON
      if ((size_t)size > tn->cmn_size) {
        KC_TRACE(10, ("THREADPRIVATE: %p (%" KMP_UINTPTR_SPEC
                      " ,%" KMP_UINTPTR_SPEC ")\n",
                      data, size, tn->cmn_size));
        KMP_FATAL(TPCommonBlocksInconsist);
      }
#endif /* USE_CHECKS_COMMON */
    } else {
      /* The parallel address will NEVER overlap with the data_address */
      /* dkp: 3rd arg to kmp_threadprivate_insert() is the data_address; use
       * data_address = data */
      KC_TRACE(20, ("__kmpc_threadprivate: T#%d inserting data\n", global_tid));
      tn = kmp_threadprivate_insert(global_tid, data, data, size);
    }

    ret = tn->par_addr;
  }
  KC_TRACE(10, ("__kmpc_threadprivate: T#%d exiting; return value = %p\n",
                global_tid, ret));

  return ret;
}

static kmp_cached_addr_t *__kmp_find_cache(void *data) {
  kmp_cached_addr_t *ptr = __kmp_threadpriv_cache_list;
  while (ptr && ptr->data != data)
    ptr = ptr->next;
  return ptr;
}

/*!
 @ingroup THREADPRIVATE
 @param loc source location information
 @param global_tid  global thread number
 @param data  pointer to data to privatize
 @param size  size of data to privatize
 @param cache  pointer to cache
 @return pointer to private storage

 Allocate private storage for threadprivate data.
*/
void *
__kmpc_threadprivate_cached(ident_t *loc,
                            kmp_int32 global_tid, // gtid.
                            void *data, // Pointer to original global variable.
                            size_t size, // Size of original global variable.
                            void ***cache) {
  KC_TRACE(10, ("__kmpc_threadprivate_cached: T#%d called with cache: %p, "
                "address: %p, size: %" KMP_SIZE_T_SPEC "\n",
                global_tid, *cache, data, size));

  if (TCR_PTR(*cache) == 0) {
    __kmp_acquire_lock(&__kmp_global_lock, global_tid);

    if (TCR_PTR(*cache) == 0) {
      __kmp_acquire_bootstrap_lock(&__kmp_tp_cached_lock);
      // Compiler often passes in NULL cache, even if it's already been created
      void **my_cache;
      kmp_cached_addr_t *tp_cache_addr;
      // Look for an existing cache
      tp_cache_addr = __kmp_find_cache(data);
      if (!tp_cache_addr) { // Cache was never created; do it now
        __kmp_tp_cached = 1;
        KMP_ITT_IGNORE(my_cache = (void **)__kmp_allocate(
                           sizeof(void *) * __kmp_tp_capacity +
                           sizeof(kmp_cached_addr_t)););
        // No need to zero the allocated memory; __kmp_allocate does that.
        KC_TRACE(50, ("__kmpc_threadprivate_cached: T#%d allocated cache at "
                      "address %p\n",
                      global_tid, my_cache));
        /* TODO: free all this memory in __kmp_common_destroy using
         * __kmp_threadpriv_cache_list */
        /* Add address of mycache to linked list for cleanup later  */
        tp_cache_addr = (kmp_cached_addr_t *)&my_cache[__kmp_tp_capacity];
        tp_cache_addr->addr = my_cache;
        tp_cache_addr->data = data;
        tp_cache_addr->compiler_cache = cache;
        tp_cache_addr->next = __kmp_threadpriv_cache_list;
        __kmp_threadpriv_cache_list = tp_cache_addr;
      } else { // A cache was already created; use it
        my_cache = tp_cache_addr->addr;
        tp_cache_addr->compiler_cache = cache;
      }
      KMP_MB();

      TCW_PTR(*cache, my_cache);
      __kmp_release_bootstrap_lock(&__kmp_tp_cached_lock);

      KMP_MB();
    }
    __kmp_release_lock(&__kmp_global_lock, global_tid);
  }

  void *ret;
  if ((ret = TCR_PTR((*cache)[global_tid])) == 0) {
    ret = __kmpc_threadprivate(loc, global_tid, data, (size_t)size);

    TCW_PTR((*cache)[global_tid], ret);
  }
  KC_TRACE(10,
           ("__kmpc_threadprivate_cached: T#%d exiting; return value = %p\n",
            global_tid, ret));
  return ret;
}

// This function should only be called when both __kmp_tp_cached_lock and
// kmp_forkjoin_lock are held.
void __kmp_threadprivate_resize_cache(int newCapacity) {
  KC_TRACE(10, ("__kmp_threadprivate_resize_cache: called with size: %d\n",
                newCapacity));

  kmp_cached_addr_t *ptr = __kmp_threadpriv_cache_list;

  while (ptr) {
    if (ptr->data) { // this location has an active cache; resize it
      void **my_cache;
      KMP_ITT_IGNORE(my_cache =
                         (void **)__kmp_allocate(sizeof(void *) * newCapacity +
                                                 sizeof(kmp_cached_addr_t)););
      // No need to zero the allocated memory; __kmp_allocate does that.
      KC_TRACE(50, ("__kmp_threadprivate_resize_cache: allocated cache at %p\n",
                    my_cache));
      // Now copy old cache into new cache
      void **old_cache = ptr->addr;
      for (int i = 0; i < __kmp_tp_capacity; ++i) {
        my_cache[i] = old_cache[i];
      }

      // Add address of new my_cache to linked list for cleanup later
      kmp_cached_addr_t *tp_cache_addr;
      tp_cache_addr = (kmp_cached_addr_t *)&my_cache[newCapacity];
      tp_cache_addr->addr = my_cache;
      tp_cache_addr->data = ptr->data;
      tp_cache_addr->compiler_cache = ptr->compiler_cache;
      tp_cache_addr->next = __kmp_threadpriv_cache_list;
      __kmp_threadpriv_cache_list = tp_cache_addr;

      // Copy new cache to compiler's location: We can copy directly
      // to (*compiler_cache) if compiler guarantees it will keep
      // using the same location for the cache. This is not yet true
      // for some compilers, in which case we have to check if
      // compiler_cache is still pointing at old cache, and if so, we
      // can point it at the new cache with an atomic compare&swap
      // operation. (Old method will always work, but we should shift
      // to new method (commented line below) when Intel and Clang
      // compilers use new method.)
      (void)KMP_COMPARE_AND_STORE_PTR(tp_cache_addr->compiler_cache, old_cache,
                                      my_cache);
      // TCW_PTR(*(tp_cache_addr->compiler_cache), my_cache);

      // If the store doesn't happen here, the compiler's old behavior will
      // inevitably call __kmpc_threadprivate_cache with a new location for the
      // cache, and that function will store the resized cache there at that
      // point.

      // Nullify old cache's data pointer so we skip it next time
      ptr->data = NULL;
    }
    ptr = ptr->next;
  }
  // After all caches are resized, update __kmp_tp_capacity to the new size
  *(volatile int *)&__kmp_tp_capacity = newCapacity;
}

/*!
 @ingroup THREADPRIVATE
 @param loc source location information
 @param data  pointer to data being privatized
 @param ctor  pointer to constructor function for data
 @param cctor  pointer to copy constructor function for data
 @param dtor  pointer to destructor function for data
 @param vector_length length of the vector (bytes or elements?)
 Register vector constructors and destructors for thread private data.
*/
void __kmpc_threadprivate_register_vec(ident_t *loc, void *data,
                                       kmpc_ctor_vec ctor, kmpc_cctor_vec cctor,
                                       kmpc_dtor_vec dtor,
                                       size_t vector_length) {
  struct shared_common *d_tn, **lnk_tn;

  KC_TRACE(10, ("__kmpc_threadprivate_register_vec: called\n"));

#ifdef USE_CHECKS_COMMON
  /* copy constructor must be zero for current code gen (Nov 2002 - jph) */
  KMP_ASSERT(cctor == 0);
#endif /* USE_CHECKS_COMMON */

  d_tn = __kmp_find_shared_task_common(
      &__kmp_threadprivate_d_table, -1,
      data); /* Only the global data table exists. */

  if (d_tn == 0) {
    d_tn = (struct shared_common *)__kmp_allocate(sizeof(struct shared_common));
    d_tn->gbl_addr = data;

    d_tn->ct.ctorv = ctor;
    d_tn->cct.cctorv = cctor;
    d_tn->dt.dtorv = dtor;
    d_tn->is_vec = TRUE;
    d_tn->vec_len = (size_t)vector_length;
    // d_tn->obj_init = 0;  // AC: __kmp_allocate zeroes the memory
    // d_tn->pod_init = 0;
    lnk_tn = &(__kmp_threadprivate_d_table.data[KMP_HASH(data)]);

    d_tn->next = *lnk_tn;
    *lnk_tn = d_tn;
  }
}

void __kmp_cleanup_threadprivate_caches() {
  kmp_cached_addr_t *ptr = __kmp_threadpriv_cache_list;

  while (ptr) {
    void **cache = ptr->addr;
    __kmp_threadpriv_cache_list = ptr->next;
    if (*ptr->compiler_cache)
      *ptr->compiler_cache = NULL;
    ptr->compiler_cache = NULL;
    ptr->data = NULL;
    ptr->addr = NULL;
    ptr->next = NULL;
    // Threadprivate data pointed at by cache entries are destroyed at end of
    // __kmp_launch_thread with __kmp_common_destroy_gtid.
    __kmp_free(cache); // implicitly frees ptr too
    ptr = __kmp_threadpriv_cache_list;
  }
}
