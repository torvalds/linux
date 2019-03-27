#ifndef KMP_DISPATCH_HIER_H
#define KMP_DISPATCH_HIER_H
#include "kmp.h"
#include "kmp_dispatch.h"

// Layer type for scheduling hierarchy
enum kmp_hier_layer_e {
  LAYER_THREAD = -1,
  LAYER_L1,
  LAYER_L2,
  LAYER_L3,
  LAYER_NUMA,
  LAYER_LOOP,
  LAYER_LAST
};

// Convert hierarchy type (LAYER_L1, LAYER_L2, etc.) to C-style string
static inline const char *__kmp_get_hier_str(kmp_hier_layer_e type) {
  switch (type) {
  case kmp_hier_layer_e::LAYER_THREAD:
    return "THREAD";
  case kmp_hier_layer_e::LAYER_L1:
    return "L1";
  case kmp_hier_layer_e::LAYER_L2:
    return "L2";
  case kmp_hier_layer_e::LAYER_L3:
    return "L3";
  case kmp_hier_layer_e::LAYER_NUMA:
    return "NUMA";
  case kmp_hier_layer_e::LAYER_LOOP:
    return "WHOLE_LOOP";
  case kmp_hier_layer_e::LAYER_LAST:
    return "LAST";
  }
  KMP_ASSERT(0);
  // Appease compilers, should never get here
  return "ERROR";
}

// Structure to store values parsed from OMP_SCHEDULE for scheduling hierarchy
typedef struct kmp_hier_sched_env_t {
  int size;
  int capacity;
  enum sched_type *scheds;
  kmp_int32 *small_chunks;
  kmp_int64 *large_chunks;
  kmp_hier_layer_e *layers;
  // Append a level of the hierarchy
  void append(enum sched_type sched, kmp_int32 chunk, kmp_hier_layer_e layer) {
    if (capacity == 0) {
      scheds = (enum sched_type *)__kmp_allocate(sizeof(enum sched_type) *
                                                 kmp_hier_layer_e::LAYER_LAST);
      small_chunks = (kmp_int32 *)__kmp_allocate(sizeof(kmp_int32) *
                                                 kmp_hier_layer_e::LAYER_LAST);
      large_chunks = (kmp_int64 *)__kmp_allocate(sizeof(kmp_int64) *
                                                 kmp_hier_layer_e::LAYER_LAST);
      layers = (kmp_hier_layer_e *)__kmp_allocate(sizeof(kmp_hier_layer_e) *
                                                  kmp_hier_layer_e::LAYER_LAST);
      capacity = kmp_hier_layer_e::LAYER_LAST;
    }
    int current_size = size;
    KMP_DEBUG_ASSERT(current_size < kmp_hier_layer_e::LAYER_LAST);
    scheds[current_size] = sched;
    layers[current_size] = layer;
    small_chunks[current_size] = chunk;
    large_chunks[current_size] = (kmp_int64)chunk;
    size++;
  }
  // Sort the hierarchy using selection sort, size will always be small
  // (less than LAYER_LAST) so it is not necessary to use an nlog(n) algorithm
  void sort() {
    if (size <= 1)
      return;
    for (int i = 0; i < size; ++i) {
      int switch_index = i;
      for (int j = i + 1; j < size; ++j) {
        if (layers[j] < layers[switch_index])
          switch_index = j;
      }
      if (switch_index != i) {
        kmp_hier_layer_e temp1 = layers[i];
        enum sched_type temp2 = scheds[i];
        kmp_int32 temp3 = small_chunks[i];
        kmp_int64 temp4 = large_chunks[i];
        layers[i] = layers[switch_index];
        scheds[i] = scheds[switch_index];
        small_chunks[i] = small_chunks[switch_index];
        large_chunks[i] = large_chunks[switch_index];
        layers[switch_index] = temp1;
        scheds[switch_index] = temp2;
        small_chunks[switch_index] = temp3;
        large_chunks[switch_index] = temp4;
      }
    }
  }
  // Free all memory
  void deallocate() {
    if (capacity > 0) {
      __kmp_free(scheds);
      __kmp_free(layers);
      __kmp_free(small_chunks);
      __kmp_free(large_chunks);
      scheds = NULL;
      layers = NULL;
      small_chunks = NULL;
      large_chunks = NULL;
    }
    size = 0;
    capacity = 0;
  }
} kmp_hier_sched_env_t;

extern int __kmp_dispatch_hand_threading;
extern kmp_hier_sched_env_t __kmp_hier_scheds;

// Sizes of layer arrays bounded by max number of detected L1s, L2s, etc.
extern int __kmp_hier_max_units[kmp_hier_layer_e::LAYER_LAST + 1];
extern int __kmp_hier_threads_per[kmp_hier_layer_e::LAYER_LAST + 1];

extern int __kmp_dispatch_get_index(int tid, kmp_hier_layer_e type);
extern int __kmp_dispatch_get_id(int gtid, kmp_hier_layer_e type);
extern int __kmp_dispatch_get_t1_per_t2(kmp_hier_layer_e t1,
                                        kmp_hier_layer_e t2);
extern void __kmp_dispatch_free_hierarchies(kmp_team_t *team);

template <typename T> struct kmp_hier_shared_bdata_t {
  typedef typename traits_t<T>::signed_t ST;
  volatile kmp_uint64 val[2];
  kmp_int32 status[2];
  T lb[2];
  T ub[2];
  ST st[2];
  dispatch_shared_info_template<T> sh[2];
  void zero() {
    val[0] = val[1] = 0;
    status[0] = status[1] = 0;
    lb[0] = lb[1] = 0;
    ub[0] = ub[1] = 0;
    st[0] = st[1] = 0;
    sh[0].u.s.iteration = sh[1].u.s.iteration = 0;
  }
  void set_next_hand_thread(T nlb, T nub, ST nst, kmp_int32 nstatus,
                            kmp_uint64 index) {
    lb[1 - index] = nlb;
    ub[1 - index] = nub;
    st[1 - index] = nst;
    status[1 - index] = nstatus;
  }
  void set_next(T nlb, T nub, ST nst, kmp_int32 nstatus, kmp_uint64 index) {
    lb[1 - index] = nlb;
    ub[1 - index] = nub;
    st[1 - index] = nst;
    status[1 - index] = nstatus;
    sh[1 - index].u.s.iteration = 0;
  }

  kmp_int32 get_next_status(kmp_uint64 index) const {
    return status[1 - index];
  }
  T get_next_lb(kmp_uint64 index) const { return lb[1 - index]; }
  T get_next_ub(kmp_uint64 index) const { return ub[1 - index]; }
  ST get_next_st(kmp_uint64 index) const { return st[1 - index]; }
  dispatch_shared_info_template<T> volatile *get_next_sh(kmp_uint64 index) {
    return &(sh[1 - index]);
  }

  kmp_int32 get_curr_status(kmp_uint64 index) const { return status[index]; }
  T get_curr_lb(kmp_uint64 index) const { return lb[index]; }
  T get_curr_ub(kmp_uint64 index) const { return ub[index]; }
  ST get_curr_st(kmp_uint64 index) const { return st[index]; }
  dispatch_shared_info_template<T> volatile *get_curr_sh(kmp_uint64 index) {
    return &(sh[index]);
  }
};

/*
 * In the barrier implementations, num_active is the number of threads that are
 * attached to the kmp_hier_top_unit_t structure in the scheduling hierarchy.
 * bdata is the shared barrier data that resides on the kmp_hier_top_unit_t
 * structure. tdata is the thread private data that resides on the thread
 * data structure.
 *
 * The reset_shared() method is used to initialize the barrier data on the
 * kmp_hier_top_unit_t hierarchy structure
 *
 * The reset_private() method is used to initialize the barrier data on the
 * thread's private dispatch buffer structure
 *
 * The barrier() method takes an id, which is that thread's id for the
 * kmp_hier_top_unit_t structure, and implements the barrier.  All threads wait
 * inside barrier() until all fellow threads who are attached to that
 * kmp_hier_top_unit_t structure have arrived.
 */

// Core barrier implementation
// Can be used in a unit with between 2 to 8 threads
template <typename T> class core_barrier_impl {
  static inline kmp_uint64 get_wait_val(int num_active) {
    kmp_uint64 wait_val;
    switch (num_active) {
    case 2:
      wait_val = 0x0101LL;
      break;
    case 3:
      wait_val = 0x010101LL;
      break;
    case 4:
      wait_val = 0x01010101LL;
      break;
    case 5:
      wait_val = 0x0101010101LL;
      break;
    case 6:
      wait_val = 0x010101010101LL;
      break;
    case 7:
      wait_val = 0x01010101010101LL;
      break;
    case 8:
      wait_val = 0x0101010101010101LL;
      break;
    default:
      // don't use the core_barrier_impl for more than 8 threads
      KMP_ASSERT(0);
    }
    return wait_val;
  }

public:
  static void reset_private(kmp_int32 num_active,
                            kmp_hier_private_bdata_t *tdata);
  static void reset_shared(kmp_int32 num_active,
                           kmp_hier_shared_bdata_t<T> *bdata);
  static void barrier(kmp_int32 id, kmp_hier_shared_bdata_t<T> *bdata,
                      kmp_hier_private_bdata_t *tdata);
};

template <typename T>
void core_barrier_impl<T>::reset_private(kmp_int32 num_active,
                                         kmp_hier_private_bdata_t *tdata) {
  tdata->num_active = num_active;
  tdata->index = 0;
  tdata->wait_val[0] = tdata->wait_val[1] = get_wait_val(num_active);
}
template <typename T>
void core_barrier_impl<T>::reset_shared(kmp_int32 num_active,
                                        kmp_hier_shared_bdata_t<T> *bdata) {
  bdata->val[0] = bdata->val[1] = 0LL;
  bdata->status[0] = bdata->status[1] = 0LL;
}
template <typename T>
void core_barrier_impl<T>::barrier(kmp_int32 id,
                                   kmp_hier_shared_bdata_t<T> *bdata,
                                   kmp_hier_private_bdata_t *tdata) {
  kmp_uint64 current_index = tdata->index;
  kmp_uint64 next_index = 1 - current_index;
  kmp_uint64 current_wait_value = tdata->wait_val[current_index];
  kmp_uint64 next_wait_value =
      (current_wait_value ? 0 : get_wait_val(tdata->num_active));
  KD_TRACE(10, ("core_barrier_impl::barrier(): T#%d current_index:%llu "
                "next_index:%llu curr_wait:%llu next_wait:%llu\n",
                __kmp_get_gtid(), current_index, next_index, current_wait_value,
                next_wait_value));
  char v = (current_wait_value ? 0x1 : 0x0);
  (RCAST(volatile char *, &(bdata->val[current_index])))[id] = v;
  __kmp_wait_yield<kmp_uint64>(&(bdata->val[current_index]), current_wait_value,
                               __kmp_eq<kmp_uint64> USE_ITT_BUILD_ARG(NULL));
  tdata->wait_val[current_index] = next_wait_value;
  tdata->index = next_index;
}

// Counter barrier implementation
// Can be used in a unit with arbitrary number of active threads
template <typename T> class counter_barrier_impl {
public:
  static void reset_private(kmp_int32 num_active,
                            kmp_hier_private_bdata_t *tdata);
  static void reset_shared(kmp_int32 num_active,
                           kmp_hier_shared_bdata_t<T> *bdata);
  static void barrier(kmp_int32 id, kmp_hier_shared_bdata_t<T> *bdata,
                      kmp_hier_private_bdata_t *tdata);
};

template <typename T>
void counter_barrier_impl<T>::reset_private(kmp_int32 num_active,
                                            kmp_hier_private_bdata_t *tdata) {
  tdata->num_active = num_active;
  tdata->index = 0;
  tdata->wait_val[0] = tdata->wait_val[1] = (kmp_uint64)num_active;
}
template <typename T>
void counter_barrier_impl<T>::reset_shared(kmp_int32 num_active,
                                           kmp_hier_shared_bdata_t<T> *bdata) {
  bdata->val[0] = bdata->val[1] = 0LL;
  bdata->status[0] = bdata->status[1] = 0LL;
}
template <typename T>
void counter_barrier_impl<T>::barrier(kmp_int32 id,
                                      kmp_hier_shared_bdata_t<T> *bdata,
                                      kmp_hier_private_bdata_t *tdata) {
  volatile kmp_int64 *val;
  kmp_uint64 current_index = tdata->index;
  kmp_uint64 next_index = 1 - current_index;
  kmp_uint64 current_wait_value = tdata->wait_val[current_index];
  kmp_uint64 next_wait_value = current_wait_value + tdata->num_active;

  KD_TRACE(10, ("counter_barrier_impl::barrier(): T#%d current_index:%llu "
                "next_index:%llu curr_wait:%llu next_wait:%llu\n",
                __kmp_get_gtid(), current_index, next_index, current_wait_value,
                next_wait_value));
  val = RCAST(volatile kmp_int64 *, &(bdata->val[current_index]));
  KMP_TEST_THEN_INC64(val);
  __kmp_wait_yield<kmp_uint64>(&(bdata->val[current_index]), current_wait_value,
                               __kmp_ge<kmp_uint64> USE_ITT_BUILD_ARG(NULL));
  tdata->wait_val[current_index] = next_wait_value;
  tdata->index = next_index;
}

// Data associated with topology unit within a layer
// For example, one kmp_hier_top_unit_t corresponds to one L1 cache
template <typename T> struct kmp_hier_top_unit_t {
  typedef typename traits_t<T>::signed_t ST;
  typedef typename traits_t<T>::unsigned_t UT;
  kmp_int32 active; // number of topology units that communicate with this unit
  // chunk information (lower/upper bound, stride, etc.)
  dispatch_private_info_template<T> hier_pr;
  kmp_hier_top_unit_t<T> *hier_parent; // pointer to parent unit
  kmp_hier_shared_bdata_t<T> hier_barrier; // shared barrier data for this unit

  kmp_int32 get_hier_id() const { return hier_pr.hier_id; }
  void reset_shared_barrier() {
    KMP_DEBUG_ASSERT(active > 0);
    if (active == 1)
      return;
    hier_barrier.zero();
    if (active >= 2 && active <= 8) {
      core_barrier_impl<T>::reset_shared(active, &hier_barrier);
    } else {
      counter_barrier_impl<T>::reset_shared(active, &hier_barrier);
    }
  }
  void reset_private_barrier(kmp_hier_private_bdata_t *tdata) {
    KMP_DEBUG_ASSERT(tdata);
    KMP_DEBUG_ASSERT(active > 0);
    if (active == 1)
      return;
    if (active >= 2 && active <= 8) {
      core_barrier_impl<T>::reset_private(active, tdata);
    } else {
      counter_barrier_impl<T>::reset_private(active, tdata);
    }
  }
  void barrier(kmp_int32 id, kmp_hier_private_bdata_t *tdata) {
    KMP_DEBUG_ASSERT(tdata);
    KMP_DEBUG_ASSERT(active > 0);
    KMP_DEBUG_ASSERT(id >= 0 && id < active);
    if (active == 1) {
      tdata->index = 1 - tdata->index;
      return;
    }
    if (active >= 2 && active <= 8) {
      core_barrier_impl<T>::barrier(id, &hier_barrier, tdata);
    } else {
      counter_barrier_impl<T>::barrier(id, &hier_barrier, tdata);
    }
  }

  kmp_int32 get_next_status(kmp_uint64 index) const {
    return hier_barrier.get_next_status(index);
  }
  T get_next_lb(kmp_uint64 index) const {
    return hier_barrier.get_next_lb(index);
  }
  T get_next_ub(kmp_uint64 index) const {
    return hier_barrier.get_next_ub(index);
  }
  ST get_next_st(kmp_uint64 index) const {
    return hier_barrier.get_next_st(index);
  }
  dispatch_shared_info_template<T> volatile *get_next_sh(kmp_uint64 index) {
    return hier_barrier.get_next_sh(index);
  }

  kmp_int32 get_curr_status(kmp_uint64 index) const {
    return hier_barrier.get_curr_status(index);
  }
  T get_curr_lb(kmp_uint64 index) const {
    return hier_barrier.get_curr_lb(index);
  }
  T get_curr_ub(kmp_uint64 index) const {
    return hier_barrier.get_curr_ub(index);
  }
  ST get_curr_st(kmp_uint64 index) const {
    return hier_barrier.get_curr_st(index);
  }
  dispatch_shared_info_template<T> volatile *get_curr_sh(kmp_uint64 index) {
    return hier_barrier.get_curr_sh(index);
  }

  void set_next_hand_thread(T lb, T ub, ST st, kmp_int32 status,
                            kmp_uint64 index) {
    hier_barrier.set_next_hand_thread(lb, ub, st, status, index);
  }
  void set_next(T lb, T ub, ST st, kmp_int32 status, kmp_uint64 index) {
    hier_barrier.set_next(lb, ub, st, status, index);
  }
  dispatch_private_info_template<T> *get_my_pr() { return &hier_pr; }
  kmp_hier_top_unit_t<T> *get_parent() { return hier_parent; }
  dispatch_private_info_template<T> *get_parent_pr() {
    return &(hier_parent->hier_pr);
  }

  kmp_int32 is_active() const { return active; }
  kmp_int32 get_num_active() const { return active; }
  void print() {
    KD_TRACE(
        10,
        ("    kmp_hier_top_unit_t: active:%d pr:%p lb:%d ub:%d st:%d tc:%d\n",
         active, &hier_pr, hier_pr.u.p.lb, hier_pr.u.p.ub, hier_pr.u.p.st,
         hier_pr.u.p.tc));
  }
};

// Information regarding a single layer within the scheduling hierarchy
template <typename T> struct kmp_hier_layer_info_t {
  int num_active; // number of threads active in this level
  kmp_hier_layer_e type; // LAYER_L1, LAYER_L2, etc.
  enum sched_type sched; // static, dynamic, guided, etc.
  typename traits_t<T>::signed_t chunk; // chunk size associated with schedule
  int length; // length of the kmp_hier_top_unit_t array

  // Print this layer's information
  void print() {
    const char *t = __kmp_get_hier_str(type);
    KD_TRACE(
        10,
        ("    kmp_hier_layer_info_t: num_active:%d type:%s sched:%d chunk:%d "
         "length:%d\n",
         num_active, t, sched, chunk, length));
  }
};

/*
 * Structure to implement entire hierarchy
 *
 * The hierarchy is kept as an array of arrays to represent the different
 * layers.  Layer 0 is the lowest layer to layer num_layers - 1 which is the
 * highest layer.
 * Example:
 * [ 2 ] -> [ L3 | L3 ]
 * [ 1 ] -> [ L2 | L2 | L2 | L2 ]
 * [ 0 ] -> [ L1 | L1 | L1 | L1 | L1 | L1 | L1 | L1 ]
 * There is also an array of layer_info_t which has information regarding
 * each layer
 */
template <typename T> struct kmp_hier_t {
public:
  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::signed_t ST;

private:
  int next_recurse(ident_t *loc, int gtid, kmp_hier_top_unit_t<T> *current,
                   kmp_int32 *p_last, T *p_lb, T *p_ub, ST *p_st,
                   kmp_int32 previous_id, int hier_level) {
    int status;
    kmp_info_t *th = __kmp_threads[gtid];
    auto parent = current->get_parent();
    bool last_layer = (hier_level == get_num_layers() - 1);
    KMP_DEBUG_ASSERT(th);
    kmp_hier_private_bdata_t *tdata = &(th->th.th_hier_bar_data[hier_level]);
    KMP_DEBUG_ASSERT(current);
    KMP_DEBUG_ASSERT(hier_level >= 0);
    KMP_DEBUG_ASSERT(hier_level < get_num_layers());
    KMP_DEBUG_ASSERT(tdata);
    KMP_DEBUG_ASSERT(parent || last_layer);

    KD_TRACE(
        1, ("kmp_hier_t.next_recurse(): T#%d (%d) called\n", gtid, hier_level));

    T hier_id = (T)current->get_hier_id();
    // Attempt to grab next iteration range for this level
    if (previous_id == 0) {
      KD_TRACE(1, ("kmp_hier_t.next_recurse(): T#%d (%d) is master of unit\n",
                   gtid, hier_level));
      kmp_int32 contains_last;
      T my_lb, my_ub;
      ST my_st;
      T nproc;
      dispatch_shared_info_template<T> volatile *my_sh;
      dispatch_private_info_template<T> *my_pr;
      if (last_layer) {
        // last layer below the very top uses the single shared buffer
        // from the team struct.
        KD_TRACE(10,
                 ("kmp_hier_t.next_recurse(): T#%d (%d) using top level sh\n",
                  gtid, hier_level));
        my_sh = reinterpret_cast<dispatch_shared_info_template<T> volatile *>(
            th->th.th_dispatch->th_dispatch_sh_current);
        nproc = (T)get_top_level_nproc();
      } else {
        // middle layers use the shared buffer inside the kmp_hier_top_unit_t
        // structure
        KD_TRACE(10, ("kmp_hier_t.next_recurse(): T#%d (%d) using hier sh\n",
                      gtid, hier_level));
        my_sh =
            parent->get_curr_sh(th->th.th_hier_bar_data[hier_level + 1].index);
        nproc = (T)parent->get_num_active();
      }
      my_pr = current->get_my_pr();
      KMP_DEBUG_ASSERT(my_sh);
      KMP_DEBUG_ASSERT(my_pr);
      enum sched_type schedule = get_sched(hier_level);
      ST chunk = (ST)get_chunk(hier_level);
      status = __kmp_dispatch_next_algorithm<T>(gtid, my_pr, my_sh,
                                                &contains_last, &my_lb, &my_ub,
                                                &my_st, nproc, hier_id);
      KD_TRACE(
          10,
          ("kmp_hier_t.next_recurse(): T#%d (%d) next_pr_sh() returned %d\n",
           gtid, hier_level, status));
      // When no iterations are found (status == 0) and this is not the last
      // layer, attempt to go up the hierarchy for more iterations
      if (status == 0 && !last_layer) {
        status = next_recurse(loc, gtid, parent, &contains_last, &my_lb, &my_ub,
                              &my_st, hier_id, hier_level + 1);
        KD_TRACE(
            10,
            ("kmp_hier_t.next_recurse(): T#%d (%d) hier_next() returned %d\n",
             gtid, hier_level, status));
        if (status == 1) {
          kmp_hier_private_bdata_t *upper_tdata =
              &(th->th.th_hier_bar_data[hier_level + 1]);
          my_sh = parent->get_curr_sh(upper_tdata->index);
          KD_TRACE(10, ("kmp_hier_t.next_recurse(): T#%d (%d) about to init\n",
                        gtid, hier_level));
          __kmp_dispatch_init_algorithm(loc, gtid, my_pr, schedule,
                                        parent->get_curr_lb(upper_tdata->index),
                                        parent->get_curr_ub(upper_tdata->index),
                                        parent->get_curr_st(upper_tdata->index),
#if USE_ITT_BUILD
                                        NULL,
#endif
                                        chunk, nproc, hier_id);
          status = __kmp_dispatch_next_algorithm<T>(
              gtid, my_pr, my_sh, &contains_last, &my_lb, &my_ub, &my_st, nproc,
              hier_id);
          if (!status) {
            KD_TRACE(10, ("kmp_hier_t.next_recurse(): T#%d (%d) status not 1 "
                          "setting to 2!\n",
                          gtid, hier_level));
            status = 2;
          }
        }
      }
      current->set_next(my_lb, my_ub, my_st, status, tdata->index);
      // Propagate whether a unit holds the actual global last iteration
      // The contains_last attribute is sent downwards from the top to the
      // bottom of the hierarchy via the contains_last flag inside the
      // private dispatch buffers in the hierarchy's middle layers
      if (contains_last) {
        // If the next_algorithm() method returns 1 for p_last and it is the
        // last layer or our parent contains the last serial chunk, then the
        // chunk must contain the last serial iteration.
        if (last_layer || parent->hier_pr.flags.contains_last) {
          KD_TRACE(10, ("kmp_hier_t.next_recurse(): T#%d (%d) Setting this pr "
                        "to contain last.\n",
                        gtid, hier_level));
          current->hier_pr.flags.contains_last = contains_last;
        }
        if (!current->hier_pr.flags.contains_last)
          contains_last = FALSE;
      }
      if (p_last)
        *p_last = contains_last;
    } // if master thread of this unit
    if (hier_level > 0 || !__kmp_dispatch_hand_threading) {
      KD_TRACE(10,
               ("kmp_hier_t.next_recurse(): T#%d (%d) going into barrier.\n",
                gtid, hier_level));
      current->barrier(previous_id, tdata);
      KD_TRACE(10,
               ("kmp_hier_t.next_recurse(): T#%d (%d) released and exit %d\n",
                gtid, hier_level, current->get_curr_status(tdata->index)));
    } else {
      KMP_DEBUG_ASSERT(previous_id == 0);
      return status;
    }
    return current->get_curr_status(tdata->index);
  }

public:
  int top_level_nproc;
  int num_layers;
  bool valid;
  int type_size;
  kmp_hier_layer_info_t<T> *info;
  kmp_hier_top_unit_t<T> **layers;
  // Deallocate all memory from this hierarchy
  void deallocate() {
    for (int i = 0; i < num_layers; ++i)
      if (layers[i] != NULL) {
        __kmp_free(layers[i]);
      }
    if (layers != NULL) {
      __kmp_free(layers);
      layers = NULL;
    }
    if (info != NULL) {
      __kmp_free(info);
      info = NULL;
    }
    num_layers = 0;
    valid = false;
  }
  // Returns true if reallocation is needed else false
  bool need_to_reallocate(int n, const kmp_hier_layer_e *new_layers,
                          const enum sched_type *new_scheds,
                          const ST *new_chunks) const {
    if (!valid || layers == NULL || info == NULL ||
        traits_t<T>::type_size != type_size || n != num_layers)
      return true;
    for (int i = 0; i < n; ++i) {
      if (info[i].type != new_layers[i])
        return true;
      if (info[i].sched != new_scheds[i])
        return true;
      if (info[i].chunk != new_chunks[i])
        return true;
    }
    return false;
  }
  // A single thread should call this function while the other threads wait
  // create a new scheduling hierarchy consisting of new_layers, new_scheds
  // and new_chunks.  These should come pre-sorted according to
  // kmp_hier_layer_e value.  This function will try to avoid reallocation
  // if it can
  void allocate_hier(int n, const kmp_hier_layer_e *new_layers,
                     const enum sched_type *new_scheds, const ST *new_chunks) {
    top_level_nproc = 0;
    if (!need_to_reallocate(n, new_layers, new_scheds, new_chunks)) {
      KD_TRACE(
          10,
          ("kmp_hier_t<T>::allocate_hier: T#0 do not need to reallocate\n"));
      for (int i = 0; i < n; ++i) {
        info[i].num_active = 0;
        for (int j = 0; j < get_length(i); ++j)
          layers[i][j].active = 0;
      }
      return;
    }
    KD_TRACE(10, ("kmp_hier_t<T>::allocate_hier: T#0 full alloc\n"));
    deallocate();
    type_size = traits_t<T>::type_size;
    num_layers = n;
    info = (kmp_hier_layer_info_t<T> *)__kmp_allocate(
        sizeof(kmp_hier_layer_info_t<T>) * n);
    layers = (kmp_hier_top_unit_t<T> **)__kmp_allocate(
        sizeof(kmp_hier_top_unit_t<T> *) * n);
    for (int i = 0; i < n; ++i) {
      int max = 0;
      kmp_hier_layer_e layer = new_layers[i];
      info[i].num_active = 0;
      info[i].type = layer;
      info[i].sched = new_scheds[i];
      info[i].chunk = new_chunks[i];
      max = __kmp_hier_max_units[layer + 1];
      if (max == 0) {
        valid = false;
        KMP_WARNING(HierSchedInvalid, __kmp_get_hier_str(layer));
        deallocate();
        return;
      }
      info[i].length = max;
      layers[i] = (kmp_hier_top_unit_t<T> *)__kmp_allocate(
          sizeof(kmp_hier_top_unit_t<T>) * max);
      for (int j = 0; j < max; ++j) {
        layers[i][j].active = 0;
      }
    }
    valid = true;
  }
  // loc - source file location
  // gtid - global thread identifier
  // pr - this thread's private dispatch buffer (corresponding with gtid)
  // p_last (return value) - pointer to flag indicating this set of iterations
  // contains last
  //          iteration
  // p_lb (return value) - lower bound for this chunk of iterations
  // p_ub (return value) - upper bound for this chunk of iterations
  // p_st (return value) - stride for this chunk of iterations
  //
  // Returns 1 if there are more iterations to perform, 0 otherwise
  int next(ident_t *loc, int gtid, dispatch_private_info_template<T> *pr,
           kmp_int32 *p_last, T *p_lb, T *p_ub, ST *p_st) {
    int status;
    kmp_int32 contains_last = 0;
    kmp_info_t *th = __kmp_threads[gtid];
    kmp_hier_private_bdata_t *tdata = &(th->th.th_hier_bar_data[0]);
    auto parent = pr->get_parent();
    KMP_DEBUG_ASSERT(parent);
    KMP_DEBUG_ASSERT(th);
    KMP_DEBUG_ASSERT(tdata);
    KMP_DEBUG_ASSERT(parent);
    T nproc = (T)parent->get_num_active();
    T unit_id = (T)pr->get_hier_id();
    KD_TRACE(
        10,
        ("kmp_hier_t.next(): T#%d THREAD LEVEL nproc:%d unit_id:%d called\n",
         gtid, nproc, unit_id));
    // Handthreading implementation
    // Each iteration is performed by all threads on last unit (typically
    // cores/tiles)
    // e.g., threads 0,1,2,3 all execute iteration 0
    //       threads 0,1,2,3 all execute iteration 1
    //       threads 4,5,6,7 all execute iteration 2
    //       threads 4,5,6,7 all execute iteration 3
    //       ... etc.
    if (__kmp_dispatch_hand_threading) {
      KD_TRACE(10,
               ("kmp_hier_t.next(): T#%d THREAD LEVEL using hand threading\n",
                gtid));
      if (unit_id == 0) {
        // For hand threading, the sh buffer on the lowest level is only ever
        // modified and read by the master thread on that level.  Because of
        // this, we can always use the first sh buffer.
        auto sh = &(parent->hier_barrier.sh[0]);
        KMP_DEBUG_ASSERT(sh);
        status = __kmp_dispatch_next_algorithm<T>(
            gtid, pr, sh, &contains_last, p_lb, p_ub, p_st, nproc, unit_id);
        if (!status) {
          bool done = false;
          while (!done) {
            done = true;
            status = next_recurse(loc, gtid, parent, &contains_last, p_lb, p_ub,
                                  p_st, unit_id, 0);
            if (status == 1) {
              __kmp_dispatch_init_algorithm(loc, gtid, pr, pr->schedule,
                                            parent->get_next_lb(tdata->index),
                                            parent->get_next_ub(tdata->index),
                                            parent->get_next_st(tdata->index),
#if USE_ITT_BUILD
                                            NULL,
#endif
                                            pr->u.p.parm1, nproc, unit_id);
              sh->u.s.iteration = 0;
              status = __kmp_dispatch_next_algorithm<T>(
                  gtid, pr, sh, &contains_last, p_lb, p_ub, p_st, nproc,
                  unit_id);
              if (!status) {
                KD_TRACE(10,
                         ("kmp_hier_t.next(): T#%d THREAD LEVEL status == 0 "
                          "after next_pr_sh()"
                          "trying again.\n",
                          gtid));
                done = false;
              }
            } else if (status == 2) {
              KD_TRACE(10, ("kmp_hier_t.next(): T#%d THREAD LEVEL status == 2 "
                            "trying again.\n",
                            gtid));
              done = false;
            }
          }
        }
        parent->set_next_hand_thread(*p_lb, *p_ub, *p_st, status, tdata->index);
      } // if master thread of lowest unit level
      parent->barrier(pr->get_hier_id(), tdata);
      if (unit_id != 0) {
        *p_lb = parent->get_curr_lb(tdata->index);
        *p_ub = parent->get_curr_ub(tdata->index);
        *p_st = parent->get_curr_st(tdata->index);
        status = parent->get_curr_status(tdata->index);
      }
    } else {
      // Normal implementation
      // Each thread grabs an iteration chunk and executes it (no cooperation)
      auto sh = parent->get_curr_sh(tdata->index);
      KMP_DEBUG_ASSERT(sh);
      status = __kmp_dispatch_next_algorithm<T>(
          gtid, pr, sh, &contains_last, p_lb, p_ub, p_st, nproc, unit_id);
      KD_TRACE(10,
               ("kmp_hier_t.next(): T#%d THREAD LEVEL next_algorithm status:%d "
                "contains_last:%d p_lb:%d p_ub:%d p_st:%d\n",
                gtid, status, contains_last, *p_lb, *p_ub, *p_st));
      if (!status) {
        bool done = false;
        while (!done) {
          done = true;
          status = next_recurse(loc, gtid, parent, &contains_last, p_lb, p_ub,
                                p_st, unit_id, 0);
          if (status == 1) {
            sh = parent->get_curr_sh(tdata->index);
            __kmp_dispatch_init_algorithm(loc, gtid, pr, pr->schedule,
                                          parent->get_curr_lb(tdata->index),
                                          parent->get_curr_ub(tdata->index),
                                          parent->get_curr_st(tdata->index),
#if USE_ITT_BUILD
                                          NULL,
#endif
                                          pr->u.p.parm1, nproc, unit_id);
            status = __kmp_dispatch_next_algorithm<T>(
                gtid, pr, sh, &contains_last, p_lb, p_ub, p_st, nproc, unit_id);
            if (!status) {
              KD_TRACE(10, ("kmp_hier_t.next(): T#%d THREAD LEVEL status == 0 "
                            "after next_pr_sh()"
                            "trying again.\n",
                            gtid));
              done = false;
            }
          } else if (status == 2) {
            KD_TRACE(10, ("kmp_hier_t.next(): T#%d THREAD LEVEL status == 2 "
                          "trying again.\n",
                          gtid));
            done = false;
          }
        }
      }
    }
    if (contains_last && !parent->hier_pr.flags.contains_last) {
      KD_TRACE(10, ("kmp_hier_t.next(): T#%d THREAD LEVEL resetting "
                    "contains_last to FALSE\n",
                    gtid));
      contains_last = FALSE;
    }
    if (p_last)
      *p_last = contains_last;
    KD_TRACE(10, ("kmp_hier_t.next(): T#%d THREAD LEVEL exit status %d\n", gtid,
                  status));
    return status;
  }
  // These functions probe the layer info structure
  // Returns the type of topology unit given level
  kmp_hier_layer_e get_type(int level) const {
    KMP_DEBUG_ASSERT(level >= 0);
    KMP_DEBUG_ASSERT(level < num_layers);
    return info[level].type;
  }
  // Returns the schedule type at given level
  enum sched_type get_sched(int level) const {
    KMP_DEBUG_ASSERT(level >= 0);
    KMP_DEBUG_ASSERT(level < num_layers);
    return info[level].sched;
  }
  // Returns the chunk size at given level
  ST get_chunk(int level) const {
    KMP_DEBUG_ASSERT(level >= 0);
    KMP_DEBUG_ASSERT(level < num_layers);
    return info[level].chunk;
  }
  // Returns the number of active threads at given level
  int get_num_active(int level) const {
    KMP_DEBUG_ASSERT(level >= 0);
    KMP_DEBUG_ASSERT(level < num_layers);
    return info[level].num_active;
  }
  // Returns the length of topology unit array at given level
  int get_length(int level) const {
    KMP_DEBUG_ASSERT(level >= 0);
    KMP_DEBUG_ASSERT(level < num_layers);
    return info[level].length;
  }
  // Returns the topology unit given the level and index
  kmp_hier_top_unit_t<T> *get_unit(int level, int index) {
    KMP_DEBUG_ASSERT(level >= 0);
    KMP_DEBUG_ASSERT(level < num_layers);
    KMP_DEBUG_ASSERT(index >= 0);
    KMP_DEBUG_ASSERT(index < get_length(level));
    return &(layers[level][index]);
  }
  // Returns the number of layers in the hierarchy
  int get_num_layers() const { return num_layers; }
  // Returns the number of threads in the top layer
  // This is necessary because we don't store a topology unit as
  // the very top level and the scheduling algorithms need this information
  int get_top_level_nproc() const { return top_level_nproc; }
  // Return whether this hierarchy is valid or not
  bool is_valid() const { return valid; }
  // Print the hierarchy
  void print() {
    KD_TRACE(10, ("kmp_hier_t:\n"));
    for (int i = num_layers - 1; i >= 0; --i) {
      KD_TRACE(10, ("Info[%d] = ", i));
      info[i].print();
    }
    for (int i = num_layers - 1; i >= 0; --i) {
      KD_TRACE(10, ("Layer[%d] =\n", i));
      for (int j = 0; j < info[i].length; ++j) {
        layers[i][j].print();
      }
    }
  }
};

template <typename T>
void __kmp_dispatch_init_hierarchy(ident_t *loc, int n,
                                   kmp_hier_layer_e *new_layers,
                                   enum sched_type *new_scheds,
                                   typename traits_t<T>::signed_t *new_chunks,
                                   T lb, T ub,
                                   typename traits_t<T>::signed_t st) {
  typedef typename traits_t<T>::signed_t ST;
  typedef typename traits_t<T>::unsigned_t UT;
  int tid, gtid, num_hw_threads, num_threads_per_layer1, active;
  int my_buffer_index;
  kmp_info_t *th;
  kmp_team_t *team;
  dispatch_private_info_template<T> *pr;
  dispatch_shared_info_template<T> volatile *sh;
  gtid = __kmp_entry_gtid();
  tid = __kmp_tid_from_gtid(gtid);
#ifdef KMP_DEBUG
  KD_TRACE(10, ("__kmp_dispatch_init_hierarchy: T#%d called: %d layer(s)\n",
                gtid, n));
  for (int i = 0; i < n; ++i) {
    const char *layer = __kmp_get_hier_str(new_layers[i]);
    KD_TRACE(10, ("__kmp_dispatch_init_hierarchy: T#%d: new_layers[%d] = %s, "
                  "new_scheds[%d] = %d, new_chunks[%d] = %u\n",
                  gtid, i, layer, i, (int)new_scheds[i], i, new_chunks[i]));
  }
#endif // KMP_DEBUG
  KMP_DEBUG_ASSERT(n > 0);
  KMP_DEBUG_ASSERT(new_layers);
  KMP_DEBUG_ASSERT(new_scheds);
  KMP_DEBUG_ASSERT(new_chunks);
  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();
  th = __kmp_threads[gtid];
  team = th->th.th_team;
  active = !team->t.t_serialized;
  th->th.th_ident = loc;
  num_hw_threads = __kmp_hier_max_units[kmp_hier_layer_e::LAYER_THREAD + 1];
  if (!active) {
    KD_TRACE(10, ("__kmp_dispatch_init_hierarchy: T#%d not active parallel. "
                  "Using normal dispatch functions.\n",
                  gtid));
    pr = reinterpret_cast<dispatch_private_info_template<T> *>(
        th->th.th_dispatch->th_disp_buffer);
    KMP_DEBUG_ASSERT(pr);
    pr->flags.use_hier = FALSE;
    pr->flags.contains_last = FALSE;
    return;
  }
  KMP_DEBUG_ASSERT(th->th.th_dispatch ==
                   &th->th.th_team->t.t_dispatch[th->th.th_info.ds.ds_tid]);

  my_buffer_index = th->th.th_dispatch->th_disp_index;
  pr = reinterpret_cast<dispatch_private_info_template<T> *>(
      &th->th.th_dispatch
           ->th_disp_buffer[my_buffer_index % __kmp_dispatch_num_buffers]);
  sh = reinterpret_cast<dispatch_shared_info_template<T> volatile *>(
      &team->t.t_disp_buffer[my_buffer_index % __kmp_dispatch_num_buffers]);
  KMP_DEBUG_ASSERT(pr);
  KMP_DEBUG_ASSERT(sh);
  pr->flags.use_hier = TRUE;
  pr->u.p.tc = 0;
  // Have master allocate the hierarchy
  if (__kmp_tid_from_gtid(gtid) == 0) {
    KD_TRACE(10, ("__kmp_dispatch_init_hierarchy: T#%d pr:%p sh:%p allocating "
                  "hierarchy\n",
                  gtid, pr, sh));
    if (sh->hier == NULL) {
      sh->hier = (kmp_hier_t<T> *)__kmp_allocate(sizeof(kmp_hier_t<T>));
    }
    sh->hier->allocate_hier(n, new_layers, new_scheds, new_chunks);
    sh->u.s.iteration = 0;
  }
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);
  // Check to make sure the hierarchy is valid
  kmp_hier_t<T> *hier = sh->hier;
  if (!sh->hier->is_valid()) {
    pr->flags.use_hier = FALSE;
    return;
  }
  // Have threads allocate their thread-private barrier data if it hasn't
  // already been allocated
  if (th->th.th_hier_bar_data == NULL) {
    th->th.th_hier_bar_data = (kmp_hier_private_bdata_t *)__kmp_allocate(
        sizeof(kmp_hier_private_bdata_t) * kmp_hier_layer_e::LAYER_LAST);
  }
  // Have threads "register" themselves by modifiying the active count for each
  // level they are involved in. The active count will act as nthreads for that
  // level regarding the scheduling algorithms
  for (int i = 0; i < n; ++i) {
    int index = __kmp_dispatch_get_index(tid, hier->get_type(i));
    kmp_hier_top_unit_t<T> *my_unit = hier->get_unit(i, index);
    // Setup the thread's private dispatch buffer's hierarchy pointers
    if (i == 0)
      pr->hier_parent = my_unit;
    // If this unit is already active, then increment active count and wait
    if (my_unit->is_active()) {
      KD_TRACE(10, ("__kmp_dispatch_init_hierarchy: T#%d my_unit (%p) "
                    "is already active (%d)\n",
                    gtid, my_unit, my_unit->active));
      KMP_TEST_THEN_INC32(&(my_unit->active));
      break;
    }
    // Flag that this unit is active
    if (KMP_COMPARE_AND_STORE_ACQ32(&(my_unit->active), 0, 1)) {
      // Do not setup parent pointer for top level unit since it has no parent
      if (i < n - 1) {
        // Setup middle layer pointers to parents
        my_unit->get_my_pr()->hier_id =
            index % __kmp_dispatch_get_t1_per_t2(hier->get_type(i),
                                                 hier->get_type(i + 1));
        int parent_index = __kmp_dispatch_get_index(tid, hier->get_type(i + 1));
        my_unit->hier_parent = hier->get_unit(i + 1, parent_index);
      } else {
        // Setup top layer information (no parent pointers are set)
        my_unit->get_my_pr()->hier_id =
            index % __kmp_dispatch_get_t1_per_t2(hier->get_type(i),
                                                 kmp_hier_layer_e::LAYER_LOOP);
        KMP_TEST_THEN_INC32(&(hier->top_level_nproc));
        my_unit->hier_parent = nullptr;
      }
      // Set trip count to 0 so that next() operation will initially climb up
      // the hierarchy to get more iterations (early exit in next() for tc == 0)
      my_unit->get_my_pr()->u.p.tc = 0;
      // Increment this layer's number of active units
      KMP_TEST_THEN_INC32(&(hier->info[i].num_active));
      KD_TRACE(10, ("__kmp_dispatch_init_hierarchy: T#%d my_unit (%p) "
                    "incrementing num_active\n",
                    gtid, my_unit));
    } else {
      KMP_TEST_THEN_INC32(&(my_unit->active));
      break;
    }
  }
  // Set this thread's id
  num_threads_per_layer1 = __kmp_dispatch_get_t1_per_t2(
      kmp_hier_layer_e::LAYER_THREAD, hier->get_type(0));
  pr->hier_id = tid % num_threads_per_layer1;
  // For oversubscribed threads, increment their index within the lowest unit
  // This is done to prevent having two or more threads with id 0, id 1, etc.
  if (tid >= num_hw_threads)
    pr->hier_id += ((tid / num_hw_threads) * num_threads_per_layer1);
  KD_TRACE(
      10, ("__kmp_dispatch_init_hierarchy: T#%d setting lowest hier_id to %d\n",
           gtid, pr->hier_id));

  pr->flags.contains_last = FALSE;
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);

  // Now that the number of active threads at each level is determined,
  // the barrier data for each unit can be initialized and the last layer's
  // loop information can be initialized.
  int prev_id = pr->get_hier_id();
  for (int i = 0; i < n; ++i) {
    if (prev_id != 0)
      break;
    int index = __kmp_dispatch_get_index(tid, hier->get_type(i));
    kmp_hier_top_unit_t<T> *my_unit = hier->get_unit(i, index);
    // Only master threads of this unit within the hierarchy do initialization
    KD_TRACE(10, ("__kmp_dispatch_init_hierarchy: T#%d (%d) prev_id is 0\n",
                  gtid, i));
    my_unit->reset_shared_barrier();
    my_unit->hier_pr.flags.contains_last = FALSE;
    // Last layer, initialize the private buffers with entire loop information
    // Now the next next_algorithim() call will get the first chunk of
    // iterations properly
    if (i == n - 1) {
      __kmp_dispatch_init_algorithm<T>(
          loc, gtid, my_unit->get_my_pr(), hier->get_sched(i), lb, ub, st,
#if USE_ITT_BUILD
          NULL,
#endif
          hier->get_chunk(i), hier->get_num_active(i), my_unit->get_hier_id());
    }
    prev_id = my_unit->get_hier_id();
  }
  // Initialize each layer of the thread's private barrier data
  kmp_hier_top_unit_t<T> *unit = pr->hier_parent;
  for (int i = 0; i < n && unit; ++i, unit = unit->get_parent()) {
    kmp_hier_private_bdata_t *tdata = &(th->th.th_hier_bar_data[i]);
    unit->reset_private_barrier(tdata);
  }
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);

#ifdef KMP_DEBUG
  if (__kmp_tid_from_gtid(gtid) == 0) {
    for (int i = 0; i < n; ++i) {
      KD_TRACE(10,
               ("__kmp_dispatch_init_hierarchy: T#%d active count[%d] = %d\n",
                gtid, i, hier->get_num_active(i)));
    }
    hier->print();
  }
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);
#endif // KMP_DEBUG
}
#endif
