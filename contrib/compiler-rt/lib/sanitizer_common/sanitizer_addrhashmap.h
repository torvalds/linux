//===-- sanitizer_addrhashmap.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Concurrent uptr->T hashmap.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ADDRHASHMAP_H
#define SANITIZER_ADDRHASHMAP_H

#include "sanitizer_common.h"
#include "sanitizer_mutex.h"
#include "sanitizer_atomic.h"
#include "sanitizer_allocator_internal.h"

namespace __sanitizer {

// Concurrent uptr->T hashmap.
// T must be a POD type, kSize is preferably a prime but can be any number.
// Usage example:
//
// typedef AddrHashMap<uptr, 11> Map;
// Map m;
// {
//   Map::Handle h(&m, addr);
//   use h.operator->() to access the data
//   if h.created() then the element was just created, and the current thread
//     has exclusive access to it
//   otherwise the current thread has only read access to the data
// }
// {
//   Map::Handle h(&m, addr, true);
//   this will remove the data from the map in Handle dtor
//   the current thread has exclusive access to the data
//   if !h.exists() then the element never existed
// }
template<typename T, uptr kSize>
class AddrHashMap {
 private:
  struct Cell {
    atomic_uintptr_t addr;
    T                val;
  };

  struct AddBucket {
    uptr cap;
    uptr size;
    Cell cells[1];  // variable len
  };

  static const uptr kBucketSize = 3;

  struct Bucket {
    RWMutex          mtx;
    atomic_uintptr_t add;
    Cell             cells[kBucketSize];
  };

 public:
  AddrHashMap();

  class Handle {
   public:
    Handle(AddrHashMap<T, kSize> *map, uptr addr);
    Handle(AddrHashMap<T, kSize> *map, uptr addr, bool remove);
    Handle(AddrHashMap<T, kSize> *map, uptr addr, bool remove, bool create);

    ~Handle();
    T *operator->();
    T &operator*();
    const T &operator*() const;
    bool created() const;
    bool exists() const;

   private:
    friend AddrHashMap<T, kSize>;
    AddrHashMap<T, kSize> *map_;
    Bucket                *bucket_;
    Cell                  *cell_;
    uptr                   addr_;
    uptr                   addidx_;
    bool                   created_;
    bool                   remove_;
    bool                   create_;
  };

 private:
  friend class Handle;
  Bucket *table_;

  void acquire(Handle *h);
  void release(Handle *h);
  uptr calcHash(uptr addr);
};

template<typename T, uptr kSize>
AddrHashMap<T, kSize>::Handle::Handle(AddrHashMap<T, kSize> *map, uptr addr) {
  map_ = map;
  addr_ = addr;
  remove_ = false;
  create_ = true;
  map_->acquire(this);
}

template<typename T, uptr kSize>
AddrHashMap<T, kSize>::Handle::Handle(AddrHashMap<T, kSize> *map, uptr addr,
    bool remove) {
  map_ = map;
  addr_ = addr;
  remove_ = remove;
  create_ = true;
  map_->acquire(this);
}

template<typename T, uptr kSize>
AddrHashMap<T, kSize>::Handle::Handle(AddrHashMap<T, kSize> *map, uptr addr,
    bool remove, bool create) {
  map_ = map;
  addr_ = addr;
  remove_ = remove;
  create_ = create;
  map_->acquire(this);
}

template<typename T, uptr kSize>
AddrHashMap<T, kSize>::Handle::~Handle() {
  map_->release(this);
}

template <typename T, uptr kSize>
T *AddrHashMap<T, kSize>::Handle::operator->() {
  return &cell_->val;
}

template <typename T, uptr kSize>
const T &AddrHashMap<T, kSize>::Handle::operator*() const {
  return cell_->val;
}

template <typename T, uptr kSize>
T &AddrHashMap<T, kSize>::Handle::operator*() {
  return cell_->val;
}

template<typename T, uptr kSize>
bool AddrHashMap<T, kSize>::Handle::created() const {
  return created_;
}

template<typename T, uptr kSize>
bool AddrHashMap<T, kSize>::Handle::exists() const {
  return cell_ != nullptr;
}

template<typename T, uptr kSize>
AddrHashMap<T, kSize>::AddrHashMap() {
  table_ = (Bucket*)MmapOrDie(kSize * sizeof(table_[0]), "AddrHashMap");
}

template<typename T, uptr kSize>
void AddrHashMap<T, kSize>::acquire(Handle *h) {
  uptr addr = h->addr_;
  uptr hash = calcHash(addr);
  Bucket *b = &table_[hash];

  h->created_ = false;
  h->addidx_ = -1U;
  h->bucket_ = b;
  h->cell_ = nullptr;

  // If we want to remove the element, we need exclusive access to the bucket,
  // so skip the lock-free phase.
  if (h->remove_)
    goto locked;

 retry:
  // First try to find an existing element w/o read mutex.
  CHECK(!h->remove_);
  // Check the embed cells.
  for (uptr i = 0; i < kBucketSize; i++) {
    Cell *c = &b->cells[i];
    uptr addr1 = atomic_load(&c->addr, memory_order_acquire);
    if (addr1 == addr) {
      h->cell_ = c;
      return;
    }
  }

  // Check the add cells with read lock.
  if (atomic_load(&b->add, memory_order_relaxed)) {
    b->mtx.ReadLock();
    AddBucket *add = (AddBucket*)atomic_load(&b->add, memory_order_relaxed);
    for (uptr i = 0; i < add->size; i++) {
      Cell *c = &add->cells[i];
      uptr addr1 = atomic_load(&c->addr, memory_order_relaxed);
      if (addr1 == addr) {
        h->addidx_ = i;
        h->cell_ = c;
        return;
      }
    }
    b->mtx.ReadUnlock();
  }

 locked:
  // Re-check existence under write lock.
  // Embed cells.
  b->mtx.Lock();
  for (uptr i = 0; i < kBucketSize; i++) {
    Cell *c = &b->cells[i];
    uptr addr1 = atomic_load(&c->addr, memory_order_relaxed);
    if (addr1 == addr) {
      if (h->remove_) {
        h->cell_ = c;
        return;
      }
      b->mtx.Unlock();
      goto retry;
    }
  }

  // Add cells.
  AddBucket *add = (AddBucket*)atomic_load(&b->add, memory_order_relaxed);
  if (add) {
    for (uptr i = 0; i < add->size; i++) {
      Cell *c = &add->cells[i];
      uptr addr1 = atomic_load(&c->addr, memory_order_relaxed);
      if (addr1 == addr) {
        if (h->remove_) {
          h->addidx_ = i;
          h->cell_ = c;
          return;
        }
        b->mtx.Unlock();
        goto retry;
      }
    }
  }

  // The element does not exist, no need to create it if we want to remove.
  if (h->remove_ || !h->create_) {
    b->mtx.Unlock();
    return;
  }

  // Now try to create it under the mutex.
  h->created_ = true;
  // See if we have a free embed cell.
  for (uptr i = 0; i < kBucketSize; i++) {
    Cell *c = &b->cells[i];
    uptr addr1 = atomic_load(&c->addr, memory_order_relaxed);
    if (addr1 == 0) {
      h->cell_ = c;
      return;
    }
  }

  // Store in the add cells.
  if (!add) {
    // Allocate a new add array.
    const uptr kInitSize = 64;
    add = (AddBucket*)InternalAlloc(kInitSize);
    internal_memset(add, 0, kInitSize);
    add->cap = (kInitSize - sizeof(*add)) / sizeof(add->cells[0]) + 1;
    add->size = 0;
    atomic_store(&b->add, (uptr)add, memory_order_relaxed);
  }
  if (add->size == add->cap) {
    // Grow existing add array.
    uptr oldsize = sizeof(*add) + (add->cap - 1) * sizeof(add->cells[0]);
    uptr newsize = oldsize * 2;
    AddBucket *add1 = (AddBucket*)InternalAlloc(newsize);
    internal_memset(add1, 0, newsize);
    add1->cap = (newsize - sizeof(*add)) / sizeof(add->cells[0]) + 1;
    add1->size = add->size;
    internal_memcpy(add1->cells, add->cells, add->size * sizeof(add->cells[0]));
    InternalFree(add);
    atomic_store(&b->add, (uptr)add1, memory_order_relaxed);
    add = add1;
  }
  // Store.
  uptr i = add->size++;
  Cell *c = &add->cells[i];
  CHECK_EQ(atomic_load(&c->addr, memory_order_relaxed), 0);
  h->addidx_ = i;
  h->cell_ = c;
}

template<typename T, uptr kSize>
void AddrHashMap<T, kSize>::release(Handle *h) {
  if (!h->cell_)
    return;
  Bucket *b = h->bucket_;
  Cell *c = h->cell_;
  uptr addr1 = atomic_load(&c->addr, memory_order_relaxed);
  if (h->created_) {
    // Denote completion of insertion.
    CHECK_EQ(addr1, 0);
    // After the following store, the element becomes available
    // for lock-free reads.
    atomic_store(&c->addr, h->addr_, memory_order_release);
    b->mtx.Unlock();
  } else if (h->remove_) {
    // Denote that the cell is empty now.
    CHECK_EQ(addr1, h->addr_);
    atomic_store(&c->addr, 0, memory_order_release);
    // See if we need to compact the bucket.
    AddBucket *add = (AddBucket*)atomic_load(&b->add, memory_order_relaxed);
    if (h->addidx_ == -1U) {
      // Removed from embed array, move an add element into the freed cell.
      if (add && add->size != 0) {
        uptr last = --add->size;
        Cell *c1 = &add->cells[last];
        c->val = c1->val;
        uptr addr1 = atomic_load(&c1->addr, memory_order_relaxed);
        atomic_store(&c->addr, addr1, memory_order_release);
        atomic_store(&c1->addr, 0, memory_order_release);
      }
    } else {
      // Removed from add array, compact it.
      uptr last = --add->size;
      Cell *c1 = &add->cells[last];
      if (c != c1) {
        *c = *c1;
        atomic_store(&c1->addr, 0, memory_order_relaxed);
      }
    }
    if (add && add->size == 0) {
      // FIXME(dvyukov): free add?
    }
    b->mtx.Unlock();
  } else {
    CHECK_EQ(addr1, h->addr_);
    if (h->addidx_ != -1U)
      b->mtx.ReadUnlock();
  }
}

template<typename T, uptr kSize>
uptr AddrHashMap<T, kSize>::calcHash(uptr addr) {
  addr += addr << 10;
  addr ^= addr >> 6;
  return addr % kSize;
}

} // namespace __sanitizer

#endif // SANITIZER_ADDRHASHMAP_H
