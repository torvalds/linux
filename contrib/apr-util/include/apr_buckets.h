/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @file apr_buckets.h
 * @brief APR-UTIL Buckets/Bucket Brigades
 */

#ifndef APR_BUCKETS_H
#define APR_BUCKETS_H

#if defined(APR_BUCKET_DEBUG) && !defined(APR_RING_DEBUG)
#define APR_RING_DEBUG
#endif

#include "apu.h"
#include "apr_network_io.h"
#include "apr_file_io.h"
#include "apr_general.h"
#include "apr_mmap.h"
#include "apr_errno.h"
#include "apr_ring.h"
#include "apr.h"
#if APR_HAVE_SYS_UIO_H
#include <sys/uio.h>	/* for struct iovec */
#endif
#if APR_HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup APR_Util_Bucket_Brigades Bucket Brigades
 * @ingroup APR_Util
 * @{ 
 */

/** default bucket buffer size - 8KB minus room for memory allocator headers */
#define APR_BUCKET_BUFF_SIZE 8000

/** Determines how a bucket or brigade should be read */
typedef enum {
    APR_BLOCK_READ,   /**< block until data becomes available */
    APR_NONBLOCK_READ /**< return immediately if no data is available */
} apr_read_type_e;

/**
 * The one-sentence buzzword-laden overview: Bucket brigades represent
 * a complex data stream that can be passed through a layered IO
 * system without unnecessary copying. A longer overview follows...
 *
 * A bucket brigade is a doubly linked list (ring) of buckets, so we
 * aren't limited to inserting at the front and removing at the end.
 * Buckets are only passed around as members of a brigade, although
 * singleton buckets can occur for short periods of time.
 *
 * Buckets are data stores of various types. They can refer to data in
 * memory, or part of a file or mmap area, or the output of a process,
 * etc. Buckets also have some type-dependent accessor functions:
 * read, split, copy, setaside, and destroy.
 *
 * read returns the address and size of the data in the bucket. If the
 * data isn't in memory then it is read in and the bucket changes type
 * so that it can refer to the new location of the data. If all the
 * data doesn't fit in the bucket then a new bucket is inserted into
 * the brigade to hold the rest of it.
 *
 * split divides the data in a bucket into two regions. After a split
 * the original bucket refers to the first part of the data and a new
 * bucket inserted into the brigade after the original bucket refers
 * to the second part of the data. Reference counts are maintained as
 * necessary.
 *
 * setaside ensures that the data in the bucket has a long enough
 * lifetime. Sometimes it is convenient to create a bucket referring
 * to data on the stack in the expectation that it will be consumed
 * (output to the network) before the stack is unwound. If that
 * expectation turns out not to be valid, the setaside function is
 * called to move the data somewhere safer.
 *
 * copy makes a duplicate of the bucket structure as long as it's
 * possible to have multiple references to a single copy of the
 * data itself.  Not all bucket types can be copied.
 *
 * destroy maintains the reference counts on the resources used by a
 * bucket and frees them if necessary.
 *
 * Note: all of the above functions have wrapper macros (apr_bucket_read(),
 * apr_bucket_destroy(), etc), and those macros should be used rather
 * than using the function pointers directly.
 *
 * To write a bucket brigade, they are first made into an iovec, so that we
 * don't write too little data at one time.  Currently we ignore compacting the
 * buckets into as few buckets as possible, but if we really want good
 * performance, then we need to compact the buckets before we convert to an
 * iovec, or possibly while we are converting to an iovec.
 */

/*
 * Forward declaration of the main types.
 */

/** @see apr_bucket_brigade */
typedef struct apr_bucket_brigade apr_bucket_brigade;
/** @see apr_bucket */
typedef struct apr_bucket apr_bucket;
/** @see apr_bucket_alloc_t */
typedef struct apr_bucket_alloc_t apr_bucket_alloc_t;

/** @see apr_bucket_type_t */
typedef struct apr_bucket_type_t apr_bucket_type_t;

/**
 * Basic bucket type
 */
struct apr_bucket_type_t {
    /**
     * The name of the bucket type
     */
    const char *name;
    /** 
     * The number of functions this bucket understands.  Can not be less than
     * five.
     */
    int num_func;
    /**
     * Whether the bucket contains metadata (ie, information that
     * describes the regular contents of the brigade).  The metadata
     * is not returned by apr_bucket_read() and is not indicated by
     * the ->length of the apr_bucket itself.  In other words, an
     * empty bucket is safe to arbitrarily remove if and only if it
     * contains no metadata.  In this sense, "data" is just raw bytes
     * that are the "content" of the brigade and "metadata" describes
     * that data but is not a proper part of it.
     */
    enum {
        /** This bucket type represents actual data to send to the client. */
        APR_BUCKET_DATA = 0,
        /** This bucket type represents metadata. */
        APR_BUCKET_METADATA = 1
    } is_metadata;
    /**
     * Free the private data and any resources used by the bucket (if they
     *  aren't shared with another bucket).  This function is required to be
     *  implemented for all bucket types, though it might be a no-op on some
     *  of them (namely ones that never allocate any private data structures).
     * @param data The private data pointer from the bucket to be destroyed
     */
    void (*destroy)(void *data);

    /**
     * Read the data from the bucket. This is required to be implemented
     *  for all bucket types.
     * @param b The bucket to read from
     * @param str A place to store the data read.  Allocation should only be
     *            done if absolutely necessary. 
     * @param len The amount of data read.
     * @param block Should this read function block if there is more data that
     *              cannot be read immediately.
     */
    apr_status_t (*read)(apr_bucket *b, const char **str, apr_size_t *len, 
                         apr_read_type_e block);
    
    /**
     * Make it possible to set aside the data for at least as long as the
     *  given pool. Buckets containing data that could potentially die before
     *  this pool (e.g. the data resides on the stack, in a child pool of
     *  the given pool, or in a disjoint pool) must somehow copy, shift, or
     *  transform the data to have the proper lifetime.
     * @param e The bucket to convert
     * @remark Some bucket types contain data that will always outlive the
     *         bucket itself. For example no data (EOS and FLUSH), or the data
     *         resides in global, constant memory (IMMORTAL), or the data is on
     *      the heap (HEAP). For these buckets, apr_bucket_setaside_noop can
     *      be used.
     */
    apr_status_t (*setaside)(apr_bucket *e, apr_pool_t *pool);

    /**
     * Split one bucket in two at the specified position by duplicating
     *  the bucket structure (not the data) and modifying any necessary
     *  start/end/offset information.  If it's not possible to do this
     *  for the bucket type (perhaps the length of the data is indeterminate,
     *  as with pipe and socket buckets), then APR_ENOTIMPL is returned.
     * @param e The bucket to split
     * @param point The offset of the first byte in the new bucket
     */
    apr_status_t (*split)(apr_bucket *e, apr_size_t point);

    /**
     * Copy the bucket structure (not the data), assuming that this is
     *  possible for the bucket type. If it's not, APR_ENOTIMPL is returned.
     * @param e The bucket to copy
     * @param c Returns a pointer to the new bucket
     */
    apr_status_t (*copy)(apr_bucket *e, apr_bucket **c);

};

/**
 * apr_bucket structures are allocated on the malloc() heap and
 * their lifetime is controlled by the parent apr_bucket_brigade
 * structure. Buckets can move from one brigade to another e.g. by
 * calling APR_BRIGADE_CONCAT(). In general the data in a bucket has
 * the same lifetime as the bucket and is freed when the bucket is
 * destroyed; if the data is shared by more than one bucket (e.g.
 * after a split) the data is freed when the last bucket goes away.
 */
struct apr_bucket {
    /** Links to the rest of the brigade */
    APR_RING_ENTRY(apr_bucket) link;
    /** The type of bucket.  */
    const apr_bucket_type_t *type;
    /** The length of the data in the bucket.  This could have been implemented
     *  with a function, but this is an optimization, because the most
     *  common thing to do will be to get the length.  If the length is unknown,
     *  the value of this field will be (apr_size_t)(-1).
     */
    apr_size_t length;
    /** The start of the data in the bucket relative to the private base
     *  pointer.  The vast majority of bucket types allow a fixed block of
     *  data to be referenced by multiple buckets, each bucket pointing to
     *  a different segment of the data.  That segment starts at base+start
     *  and ends at base+start+length.  
     *  If the length == (apr_size_t)(-1), then start == -1.
     */
    apr_off_t start;
    /** type-dependent data hangs off this pointer */
    void *data;	
    /**
     * Pointer to function used to free the bucket. This function should
     * always be defined and it should be consistent with the memory
     * function used to allocate the bucket. For example, if malloc() is 
     * used to allocate the bucket, this pointer should point to free().
     * @param e Pointer to the bucket being freed
     */
    void (*free)(void *e);
    /** The freelist from which this bucket was allocated */
    apr_bucket_alloc_t *list;
};

/** A list of buckets */
struct apr_bucket_brigade {
    /** The pool to associate the brigade with.  The data is not allocated out
     *  of the pool, but a cleanup is registered with this pool.  If the 
     *  brigade is destroyed by some mechanism other than pool destruction,
     *  the destroying function is responsible for killing the cleanup.
     */
    apr_pool_t *p;
    /** The buckets in the brigade are on this list. */
    /*
     * The apr_bucket_list structure doesn't actually need a name tag
     * because it has no existence independent of struct apr_bucket_brigade;
     * the ring macros are designed so that you can leave the name tag
     * argument empty in this situation but apparently the Windows compiler
     * doesn't like that.
     */
    APR_RING_HEAD(apr_bucket_list, apr_bucket) list;
    /** The freelist from which this bucket was allocated */
    apr_bucket_alloc_t *bucket_alloc;
};


/**
 * Function called when a brigade should be flushed
 */
typedef apr_status_t (*apr_brigade_flush)(apr_bucket_brigade *bb, void *ctx);

/*
 * define APR_BUCKET_DEBUG if you want your brigades to be checked for
 * validity at every possible instant.  this will slow your code down
 * substantially but is a very useful debugging tool.
 */
#ifdef APR_BUCKET_DEBUG

#define APR_BRIGADE_CHECK_CONSISTENCY(b)				\
        APR_RING_CHECK_CONSISTENCY(&(b)->list, apr_bucket, link)

#define APR_BUCKET_CHECK_CONSISTENCY(e)					\
        APR_RING_CHECK_ELEM_CONSISTENCY((e), apr_bucket, link)

#else
/**
 * checks the ring pointers in a bucket brigade for consistency.  an
 * abort() will be triggered if any inconsistencies are found.
 *   note: this is a no-op unless APR_BUCKET_DEBUG is defined.
 * @param b The brigade
 */
#define APR_BRIGADE_CHECK_CONSISTENCY(b)
/**
 * checks the brigade a bucket is in for ring consistency.  an
 * abort() will be triggered if any inconsistencies are found.
 *   note: this is a no-op unless APR_BUCKET_DEBUG is defined.
 * @param e The bucket
 */
#define APR_BUCKET_CHECK_CONSISTENCY(e)
#endif


/**
 * Wrappers around the RING macros to reduce the verbosity of the code
 * that handles bucket brigades.
 */
/**
 * The magic pointer value that indicates the head of the brigade
 * @remark This is used to find the beginning and end of the brigade, eg:
 * <pre>
 *      while (e != APR_BRIGADE_SENTINEL(b)) {
 *          ...
 *          e = APR_BUCKET_NEXT(e);
 *      }
 * </pre>
 * @param  b The brigade
 * @return The magic pointer value
 */
#define APR_BRIGADE_SENTINEL(b)	APR_RING_SENTINEL(&(b)->list, apr_bucket, link)

/**
 * Determine if the bucket brigade is empty
 * @param b The brigade to check
 * @return true or false
 */
#define APR_BRIGADE_EMPTY(b)	APR_RING_EMPTY(&(b)->list, apr_bucket, link)

/**
 * Return the first bucket in a brigade
 * @param b The brigade to query
 * @return The first bucket in the brigade
 */
#define APR_BRIGADE_FIRST(b)	APR_RING_FIRST(&(b)->list)
/**
 * Return the last bucket in a brigade
 * @param b The brigade to query
 * @return The last bucket in the brigade
 */
#define APR_BRIGADE_LAST(b)	APR_RING_LAST(&(b)->list)

/**
 * Insert a single bucket at the front of a brigade
 * @param b The brigade to add to
 * @param e The bucket to insert
 */
#define APR_BRIGADE_INSERT_HEAD(b, e) do {				\
	apr_bucket *ap__b = (e);                                        \
	APR_RING_INSERT_HEAD(&(b)->list, ap__b, apr_bucket, link);	\
        APR_BRIGADE_CHECK_CONSISTENCY((b));				\
    } while (0)

/**
 * Insert a single bucket at the end of a brigade
 * @param b The brigade to add to
 * @param e The bucket to insert
 */
#define APR_BRIGADE_INSERT_TAIL(b, e) do {				\
	apr_bucket *ap__b = (e);					\
	APR_RING_INSERT_TAIL(&(b)->list, ap__b, apr_bucket, link);	\
        APR_BRIGADE_CHECK_CONSISTENCY((b));				\
    } while (0)

/**
 * Concatenate brigade b onto the end of brigade a, leaving brigade b empty
 * @param a The first brigade
 * @param b The second brigade
 */
#define APR_BRIGADE_CONCAT(a, b) do {					\
        APR_RING_CONCAT(&(a)->list, &(b)->list, apr_bucket, link);	\
        APR_BRIGADE_CHECK_CONSISTENCY((a));				\
    } while (0)

/**
 * Prepend brigade b onto the beginning of brigade a, leaving brigade b empty
 * @param a The first brigade
 * @param b The second brigade
 */
#define APR_BRIGADE_PREPEND(a, b) do {					\
        APR_RING_PREPEND(&(a)->list, &(b)->list, apr_bucket, link);	\
        APR_BRIGADE_CHECK_CONSISTENCY((a));				\
    } while (0)

/**
 * Insert a single bucket before a specified bucket
 * @param a The bucket to insert before
 * @param b The bucket to insert
 */
#define APR_BUCKET_INSERT_BEFORE(a, b) do {				\
	apr_bucket *ap__a = (a), *ap__b = (b);				\
	APR_RING_INSERT_BEFORE(ap__a, ap__b, link);			\
        APR_BUCKET_CHECK_CONSISTENCY(ap__a);				\
    } while (0)

/**
 * Insert a single bucket after a specified bucket
 * @param a The bucket to insert after
 * @param b The bucket to insert
 */
#define APR_BUCKET_INSERT_AFTER(a, b) do {				\
	apr_bucket *ap__a = (a), *ap__b = (b);				\
	APR_RING_INSERT_AFTER(ap__a, ap__b, link);			\
        APR_BUCKET_CHECK_CONSISTENCY(ap__a);				\
    } while (0)

/**
 * Get the next bucket in the list
 * @param e The current bucket
 * @return The next bucket
 */
#define APR_BUCKET_NEXT(e)	APR_RING_NEXT((e), link)
/**
 * Get the previous bucket in the list
 * @param e The current bucket
 * @return The previous bucket
 */
#define APR_BUCKET_PREV(e)	APR_RING_PREV((e), link)

/**
 * Remove a bucket from its bucket brigade
 * @param e The bucket to remove
 */
#define APR_BUCKET_REMOVE(e)	APR_RING_REMOVE((e), link)

/**
 * Initialize a new bucket's prev/next pointers
 * @param e The bucket to initialize
 */
#define APR_BUCKET_INIT(e)	APR_RING_ELEM_INIT((e), link)

/**
 * Determine if a bucket contains metadata.  An empty bucket is
 * safe to arbitrarily remove if and only if this is false.
 * @param e The bucket to inspect
 * @return true or false
 */
#define APR_BUCKET_IS_METADATA(e)    ((e)->type->is_metadata)

/**
 * Determine if a bucket is a FLUSH bucket
 * @param e The bucket to inspect
 * @return true or false
 */
#define APR_BUCKET_IS_FLUSH(e)       ((e)->type == &apr_bucket_type_flush)
/**
 * Determine if a bucket is an EOS bucket
 * @param e The bucket to inspect
 * @return true or false
 */
#define APR_BUCKET_IS_EOS(e)         ((e)->type == &apr_bucket_type_eos)
/**
 * Determine if a bucket is a FILE bucket
 * @param e The bucket to inspect
 * @return true or false
 */
#define APR_BUCKET_IS_FILE(e)        ((e)->type == &apr_bucket_type_file)
/**
 * Determine if a bucket is a PIPE bucket
 * @param e The bucket to inspect
 * @return true or false
 */
#define APR_BUCKET_IS_PIPE(e)        ((e)->type == &apr_bucket_type_pipe)
/**
 * Determine if a bucket is a SOCKET bucket
 * @param e The bucket to inspect
 * @return true or false
 */
#define APR_BUCKET_IS_SOCKET(e)      ((e)->type == &apr_bucket_type_socket)
/**
 * Determine if a bucket is a HEAP bucket
 * @param e The bucket to inspect
 * @return true or false
 */
#define APR_BUCKET_IS_HEAP(e)        ((e)->type == &apr_bucket_type_heap)
/**
 * Determine if a bucket is a TRANSIENT bucket
 * @param e The bucket to inspect
 * @return true or false
 */
#define APR_BUCKET_IS_TRANSIENT(e)   ((e)->type == &apr_bucket_type_transient)
/**
 * Determine if a bucket is a IMMORTAL bucket
 * @param e The bucket to inspect
 * @return true or false
 */
#define APR_BUCKET_IS_IMMORTAL(e)    ((e)->type == &apr_bucket_type_immortal)
#if APR_HAS_MMAP
/**
 * Determine if a bucket is a MMAP bucket
 * @param e The bucket to inspect
 * @return true or false
 */
#define APR_BUCKET_IS_MMAP(e)        ((e)->type == &apr_bucket_type_mmap)
#endif
/**
 * Determine if a bucket is a POOL bucket
 * @param e The bucket to inspect
 * @return true or false
 */
#define APR_BUCKET_IS_POOL(e)        ((e)->type == &apr_bucket_type_pool)

/*
 * General-purpose reference counting for the various bucket types.
 *
 * Any bucket type that keeps track of the resources it uses (i.e.
 * most of them except for IMMORTAL, TRANSIENT, and EOS) needs to
 * attach a reference count to the resource so that it can be freed
 * when the last bucket that uses it goes away. Resource-sharing may
 * occur because of bucket splits or buckets that refer to globally
 * cached data. */

/** @see apr_bucket_refcount */
typedef struct apr_bucket_refcount apr_bucket_refcount;
/**
 * The structure used to manage the shared resource must start with an
 * apr_bucket_refcount which is updated by the general-purpose refcount
 * code. A pointer to the bucket-type-dependent private data structure
 * can be cast to a pointer to an apr_bucket_refcount and vice versa.
 */
struct apr_bucket_refcount {
    /** The number of references to this bucket */
    int          refcount;
};

/*  *****  Reference-counted bucket types  *****  */

/** @see apr_bucket_heap */
typedef struct apr_bucket_heap apr_bucket_heap;
/**
 * A bucket referring to data allocated off the heap.
 */
struct apr_bucket_heap {
    /** Number of buckets using this memory */
    apr_bucket_refcount  refcount;
    /** The start of the data actually allocated.  This should never be
     * modified, it is only used to free the bucket.
     */
    char    *base;
    /** how much memory was allocated */
    apr_size_t  alloc_len;
    /** function to use to delete the data */
    void (*free_func)(void *data);
};

/** @see apr_bucket_pool */
typedef struct apr_bucket_pool apr_bucket_pool;
/**
 * A bucket referring to data allocated from a pool
 */
struct apr_bucket_pool {
    /** The pool bucket must be able to be easily morphed to a heap
     * bucket if the pool gets cleaned up before all references are
     * destroyed.  This apr_bucket_heap structure is populated automatically
     * when the pool gets cleaned up, and subsequent calls to pool_read()
     * will result in the apr_bucket in question being morphed into a
     * regular heap bucket.  (To avoid having to do many extra refcount
     * manipulations and b->data manipulations, the apr_bucket_pool
     * struct actually *contains* the apr_bucket_heap struct that it
     * will become as its first element; the two share their
     * apr_bucket_refcount members.)
     */
    apr_bucket_heap  heap;
    /** The block of data actually allocated from the pool.
     * Segments of this block are referenced by adjusting
     * the start and length of the apr_bucket accordingly.
     * This will be NULL after the pool gets cleaned up.
     */
    const char *base;
    /** The pool the data was allocated from.  When the pool
     * is cleaned up, this gets set to NULL as an indicator
     * to pool_read() that the data is now on the heap and
     * so it should morph the bucket into a regular heap
     * bucket before continuing.
     */
    apr_pool_t *pool;
    /** The freelist this structure was allocated from, which is
     * needed in the cleanup phase in order to allocate space on the heap
     */
    apr_bucket_alloc_t *list;
};

#if APR_HAS_MMAP
/** @see apr_bucket_mmap */
typedef struct apr_bucket_mmap apr_bucket_mmap;
/**
 * A bucket referring to an mmap()ed file
 */
struct apr_bucket_mmap {
    /** Number of buckets using this memory */
    apr_bucket_refcount  refcount;
    /** The mmap this sub_bucket refers to */
    apr_mmap_t *mmap;
};
#endif

/** @see apr_bucket_file */
typedef struct apr_bucket_file apr_bucket_file;
/**
 * A bucket referring to an file
 */
struct apr_bucket_file {
    /** Number of buckets using this memory */
    apr_bucket_refcount  refcount;
    /** The file this bucket refers to */
    apr_file_t *fd;
    /** The pool into which any needed structures should
     *  be created while reading from this file bucket */
    apr_pool_t *readpool;
#if APR_HAS_MMAP
    /** Whether this bucket should be memory-mapped if
     *  a caller tries to read from it */
    int can_mmap;
#endif /* APR_HAS_MMAP */
};

/** @see apr_bucket_structs */
typedef union apr_bucket_structs apr_bucket_structs;
/**
 * A union of all bucket structures so we know what
 * the max size is.
 */
union apr_bucket_structs {
    apr_bucket      b;      /**< Bucket */
    apr_bucket_heap heap;   /**< Heap */
    apr_bucket_pool pool;   /**< Pool */
#if APR_HAS_MMAP
    apr_bucket_mmap mmap;   /**< MMap */
#endif
    apr_bucket_file file;   /**< File */
};

/**
 * The amount that apr_bucket_alloc() should allocate in the common case.
 * Note: this is twice as big as apr_bucket_structs to allow breathing
 * room for third-party bucket types.
 */
#define APR_BUCKET_ALLOC_SIZE  APR_ALIGN_DEFAULT(2*sizeof(apr_bucket_structs))

/*  *****  Bucket Brigade Functions  *****  */
/**
 * Create a new bucket brigade.  The bucket brigade is originally empty.
 * @param p The pool to associate with the brigade.  Data is not allocated out
 *          of the pool, but a cleanup is registered.
 * @param list The bucket allocator to use
 * @return The empty bucket brigade
 */
APU_DECLARE(apr_bucket_brigade *) apr_brigade_create(apr_pool_t *p,
                                                     apr_bucket_alloc_t *list);

/**
 * destroy an entire bucket brigade.  This includes destroying all of the
 * buckets within the bucket brigade's bucket list. 
 * @param b The bucket brigade to destroy
 */
APU_DECLARE(apr_status_t) apr_brigade_destroy(apr_bucket_brigade *b);

/**
 * empty out an entire bucket brigade.  This includes destroying all of the
 * buckets within the bucket brigade's bucket list.  This is similar to
 * apr_brigade_destroy(), except that it does not deregister the brigade's
 * pool cleanup function.
 * @param data The bucket brigade to clean up
 * @remark Generally, you should use apr_brigade_destroy().  This function
 *         can be useful in situations where you have a single brigade that
 *         you wish to reuse many times by destroying all of the buckets in
 *         the brigade and putting new buckets into it later.
 */
APU_DECLARE(apr_status_t) apr_brigade_cleanup(void *data);

/**
 * Move the buckets from the tail end of the existing brigade @a b into
 * the brigade @a a. If @a a is NULL a new brigade is created. Buckets
 * from @a e to the last bucket (inclusively) of brigade @a b are moved
 * from @a b to the returned brigade @a a.
 *
 * @param b The brigade to split
 * @param e The first bucket to move
 * @param a The brigade which should be used for the result or NULL if
 *          a new brigade should be created. The brigade @a a will be
 *          cleared if it is not empty.
 * @return The brigade supplied in @a a or a new one if @a a was NULL.
 * @warning Note that this function allocates a new brigade if @a a is
 * NULL so memory consumption should be carefully considered.
 */
APU_DECLARE(apr_bucket_brigade *) apr_brigade_split_ex(apr_bucket_brigade *b,
                                                       apr_bucket *e,
                                                       apr_bucket_brigade *a);

/**
 * Create a new bucket brigade and move the buckets from the tail end
 * of an existing brigade into the new brigade.  Buckets from 
 * @a e to the last bucket (inclusively) of brigade @a b
 * are moved from @a b to the returned brigade.
 * @param b The brigade to split 
 * @param e The first bucket to move
 * @return The new brigade
 * @warning Note that this function always allocates a new brigade
 * so memory consumption should be carefully considered.
 */
APU_DECLARE(apr_bucket_brigade *) apr_brigade_split(apr_bucket_brigade *b,
                                                    apr_bucket *e);

/**
 * Partition a bucket brigade at a given offset (in bytes from the start of
 * the brigade).  This is useful whenever a filter wants to use known ranges
 * of bytes from the brigade; the ranges can even overlap.
 * @param b The brigade to partition
 * @param point The offset at which to partition the brigade
 * @param after_point Returns a pointer to the first bucket after the partition
 * @return APR_SUCCESS on success, APR_INCOMPLETE if the contents of the
 * brigade were shorter than @a point, or an error code.
 * @remark if APR_INCOMPLETE is returned, @a after_point will be set to
 * the brigade sentinel.
 */
APU_DECLARE(apr_status_t) apr_brigade_partition(apr_bucket_brigade *b,
                                                apr_off_t point,
                                                apr_bucket **after_point);

/**
 * Return the total length of the brigade.
 * @param bb The brigade to compute the length of
 * @param read_all Read unknown-length buckets to force a size
 * @param length Returns the length of the brigade (up to the end, or up
 *               to a bucket read error), or -1 if the brigade has buckets
 *               of indeterminate length and read_all is 0.
 */
APU_DECLARE(apr_status_t) apr_brigade_length(apr_bucket_brigade *bb,
                                             int read_all,
                                             apr_off_t *length);

/**
 * Take a bucket brigade and store the data in a flat char*
 * @param bb The bucket brigade to create the char* from
 * @param c The char* to write into
 * @param len The maximum length of the char array. On return, it is the
 *            actual length of the char array.
 */
APU_DECLARE(apr_status_t) apr_brigade_flatten(apr_bucket_brigade *bb,
                                              char *c,
                                              apr_size_t *len);

/**
 * Creates a pool-allocated string representing a flat bucket brigade
 * @param bb The bucket brigade to create the char array from
 * @param c On return, the allocated char array
 * @param len On return, the length of the char array.
 * @param pool The pool to allocate the string from.
 */
APU_DECLARE(apr_status_t) apr_brigade_pflatten(apr_bucket_brigade *bb, 
                                               char **c,
                                               apr_size_t *len,
                                               apr_pool_t *pool);

/**
 * Split a brigade to represent one LF line.
 * @param bbOut The bucket brigade that will have the LF line appended to.
 * @param bbIn The input bucket brigade to search for a LF-line.
 * @param block The blocking mode to be used to split the line.
 * @param maxbytes The maximum bytes to read.  If this many bytes are seen
 *                 without a LF, the brigade will contain a partial line.
 */
APU_DECLARE(apr_status_t) apr_brigade_split_line(apr_bucket_brigade *bbOut,
                                                 apr_bucket_brigade *bbIn,
                                                 apr_read_type_e block,
                                                 apr_off_t maxbytes);

/**
 * Create an iovec of the elements in a bucket_brigade... return number 
 * of elements used.  This is useful for writing to a file or to the
 * network efficiently.
 * @param b The bucket brigade to create the iovec from
 * @param vec The iovec to create
 * @param nvec The number of elements in the iovec. On return, it is the
 *             number of iovec elements actually filled out.
 */
APU_DECLARE(apr_status_t) apr_brigade_to_iovec(apr_bucket_brigade *b, 
                                               struct iovec *vec, int *nvec);

/**
 * This function writes a list of strings into a bucket brigade. 
 * @param b The bucket brigade to add to
 * @param flush The flush function to use if the brigade is full
 * @param ctx The structure to pass to the flush function
 * @param va A list of strings to add
 * @return APR_SUCCESS or error code.
 */
APU_DECLARE(apr_status_t) apr_brigade_vputstrs(apr_bucket_brigade *b,
                                               apr_brigade_flush flush,
                                               void *ctx,
                                               va_list va);

/**
 * This function writes a string into a bucket brigade.
 *
 * The apr_brigade_write function attempts to be efficient with the
 * handling of heap buckets. Regardless of the amount of data stored
 * inside a heap bucket, heap buckets are a fixed size to promote their
 * reuse.
 *
 * If an attempt is made to write a string to a brigade that already 
 * ends with a heap bucket, this function will attempt to pack the
 * string into the remaining space in the previous heap bucket, before
 * allocating a new heap bucket.
 *
 * This function always returns APR_SUCCESS, unless a flush function is
 * passed, in which case the return value of the flush function will be
 * returned if used.
 * @param b The bucket brigade to add to
 * @param flush The flush function to use if the brigade is full
 * @param ctx The structure to pass to the flush function
 * @param str The string to add
 * @param nbyte The number of bytes to write
 * @return APR_SUCCESS or error code
 */
APU_DECLARE(apr_status_t) apr_brigade_write(apr_bucket_brigade *b,
                                            apr_brigade_flush flush, void *ctx,
                                            const char *str, apr_size_t nbyte);

/**
 * This function writes multiple strings into a bucket brigade.
 * @param b The bucket brigade to add to
 * @param flush The flush function to use if the brigade is full
 * @param ctx The structure to pass to the flush function
 * @param vec The strings to add (address plus length for each)
 * @param nvec The number of entries in iovec
 * @return APR_SUCCESS or error code
 */
APU_DECLARE(apr_status_t) apr_brigade_writev(apr_bucket_brigade *b,
                                             apr_brigade_flush flush,
                                             void *ctx,
                                             const struct iovec *vec,
                                             apr_size_t nvec);

/**
 * This function writes a string into a bucket brigade.
 * @param bb The bucket brigade to add to
 * @param flush The flush function to use if the brigade is full
 * @param ctx The structure to pass to the flush function
 * @param str The string to add
 * @return APR_SUCCESS or error code
 */
APU_DECLARE(apr_status_t) apr_brigade_puts(apr_bucket_brigade *bb,
                                           apr_brigade_flush flush, void *ctx,
                                           const char *str);

/**
 * This function writes a character into a bucket brigade.
 * @param b The bucket brigade to add to
 * @param flush The flush function to use if the brigade is full
 * @param ctx The structure to pass to the flush function
 * @param c The character to add
 * @return APR_SUCCESS or error code
 */
APU_DECLARE(apr_status_t) apr_brigade_putc(apr_bucket_brigade *b,
                                           apr_brigade_flush flush, void *ctx,
                                           const char c);

/**
 * This function writes an unspecified number of strings into a bucket brigade.
 * @param b The bucket brigade to add to
 * @param flush The flush function to use if the brigade is full
 * @param ctx The structure to pass to the flush function
 * @param ... The strings to add
 * @return APR_SUCCESS or error code
 */
APU_DECLARE_NONSTD(apr_status_t) apr_brigade_putstrs(apr_bucket_brigade *b,
                                                     apr_brigade_flush flush,
                                                     void *ctx, ...);

/**
 * Evaluate a printf and put the resulting string at the end 
 * of the bucket brigade.
 * @param b The brigade to write to
 * @param flush The flush function to use if the brigade is full
 * @param ctx The structure to pass to the flush function
 * @param fmt The format of the string to write
 * @param ... The arguments to fill out the format
 * @return APR_SUCCESS or error code
 */
APU_DECLARE_NONSTD(apr_status_t) apr_brigade_printf(apr_bucket_brigade *b, 
                                                    apr_brigade_flush flush,
                                                    void *ctx,
                                                    const char *fmt, ...)
        __attribute__((format(printf,4,5)));

/**
 * Evaluate a printf and put the resulting string at the end 
 * of the bucket brigade.
 * @param b The brigade to write to
 * @param flush The flush function to use if the brigade is full
 * @param ctx The structure to pass to the flush function
 * @param fmt The format of the string to write
 * @param va The arguments to fill out the format
 * @return APR_SUCCESS or error code
 */
APU_DECLARE(apr_status_t) apr_brigade_vprintf(apr_bucket_brigade *b, 
                                              apr_brigade_flush flush,
                                              void *ctx,
                                              const char *fmt, va_list va);

/**
 * Utility function to insert a file (or a segment of a file) onto the
 * end of the brigade.  The file is split into multiple buckets if it
 * is larger than the maximum size which can be represented by a
 * single bucket.
 * @param bb the brigade to insert into
 * @param f the file to insert
 * @param start the offset of the start of the segment
 * @param len the length of the segment of the file to insert
 * @param p pool from which file buckets are allocated
 * @return the last bucket inserted
 */
APU_DECLARE(apr_bucket *) apr_brigade_insert_file(apr_bucket_brigade *bb,
                                                  apr_file_t *f,
                                                  apr_off_t start,
                                                  apr_off_t len,
                                                  apr_pool_t *p);



/*  *****  Bucket freelist functions *****  */
/**
 * Create a bucket allocator.
 * @param p This pool's underlying apr_allocator_t is used to allocate memory
 *          for the bucket allocator.  When the pool is destroyed, the bucket
 *          allocator's cleanup routine will free all memory that has been
 *          allocated from it.
 * @remark  The reason the allocator gets its memory from the pool's
 *          apr_allocator_t rather than from the pool itself is because
 *          the bucket allocator will free large memory blocks back to the
 *          allocator when it's done with them, thereby preventing memory
 *          footprint growth that would occur if we allocated from the pool.
 * @warning The allocator must never be used by more than one thread at a time.
 */
APU_DECLARE_NONSTD(apr_bucket_alloc_t *) apr_bucket_alloc_create(apr_pool_t *p);

/**
 * Create a bucket allocator.
 * @param allocator This apr_allocator_t is used to allocate both the bucket
 *          allocator and all memory handed out by the bucket allocator.  The
 *          caller is responsible for destroying the bucket allocator and the
 *          apr_allocator_t -- no automatic cleanups will happen.
 * @warning The allocator must never be used by more than one thread at a time.
 */
APU_DECLARE_NONSTD(apr_bucket_alloc_t *) apr_bucket_alloc_create_ex(apr_allocator_t *allocator);

/**
 * Destroy a bucket allocator.
 * @param list The allocator to be destroyed
 */
APU_DECLARE_NONSTD(void) apr_bucket_alloc_destroy(apr_bucket_alloc_t *list);

/**
 * Allocate memory for use by the buckets.
 * @param size The amount to allocate.
 * @param list The allocator from which to allocate the memory.
 */
APU_DECLARE_NONSTD(void *) apr_bucket_alloc(apr_size_t size, apr_bucket_alloc_t *list);

/**
 * Free memory previously allocated with apr_bucket_alloc().
 * @param block The block of memory to be freed.
 */
APU_DECLARE_NONSTD(void) apr_bucket_free(void *block);


/*  *****  Bucket Functions  *****  */
/**
 * Free the resources used by a bucket. If multiple buckets refer to
 * the same resource it is freed when the last one goes away.
 * @see apr_bucket_delete()
 * @param e The bucket to destroy
 */
#define apr_bucket_destroy(e) do {					\
        (e)->type->destroy((e)->data);					\
        (e)->free(e);							\
    } while (0)

/**
 * Delete a bucket by removing it from its brigade (if any) and then
 * destroying it.
 * @remark This mainly acts as an aid in avoiding code verbosity.  It is
 * the preferred exact equivalent to:
 * <pre>
 *      APR_BUCKET_REMOVE(e);
 *      apr_bucket_destroy(e);
 * </pre>
 * @param e The bucket to delete
 */
#define apr_bucket_delete(e) do {					\
        APR_BUCKET_REMOVE(e);						\
        apr_bucket_destroy(e);						\
    } while (0)

/**
 * Read some data from the bucket.
 *
 * The apr_bucket_read function returns a convenient amount of data
 * from the bucket provided, writing the address and length of the
 * data to the pointers provided by the caller. The function tries
 * as hard as possible to avoid a memory copy.
 *
 * Buckets are expected to be a member of a brigade at the time they
 * are read.
 *
 * In typical application code, buckets are read in a loop, and after
 * each bucket is read and processed, it is moved or deleted from the
 * brigade and the next bucket read.
 *
 * The definition of "convenient" depends on the type of bucket that
 * is being read, and is decided by APR. In the case of memory based
 * buckets such as heap and immortal buckets, a pointer will be
 * returned to the location of the buffer containing the complete
 * contents of the bucket.
 *
 * Some buckets, such as the socket bucket, might have no concept
 * of length. If an attempt is made to read such a bucket, the
 * apr_bucket_read function will read a convenient amount of data
 * from the socket. The socket bucket is magically morphed into a
 * heap bucket containing the just-read data, and a new socket bucket
 * is inserted just after this heap bucket.
 *
 * To understand why apr_bucket_read might do this, consider the loop
 * described above to read and process buckets. The current bucket
 * is magically morphed into a heap bucket and returned to the caller.
 * The caller processes the data, and deletes the heap bucket, moving
 * onto the next bucket, the new socket bucket. This process repeats,
 * giving the illusion of a bucket brigade that contains potentially
 * infinite amounts of data. It is up to the caller to decide at what
 * point to stop reading buckets.
 *
 * Some buckets, such as the file bucket, might have a fixed size,
 * but be significantly larger than is practical to store in RAM in
 * one go. As with the socket bucket, if an attempt is made to read
 * from a file bucket, the file bucket is magically morphed into a
 * heap bucket containing a convenient amount of data read from the
 * current offset in the file. During the read, the offset will be
 * moved forward on the file, and a new file bucket will be inserted
 * directly after the current bucket representing the remainder of the
 * file. If the heap bucket was large enough to store the whole
 * remainder of the file, no more file buckets are inserted, and the
 * file bucket will disappear completely.
 *
 * The pattern for reading buckets described above does create the
 * illusion that the code is willing to swallow buckets that might be
 * too large for the system to handle in one go. This however is just
 * an illusion: APR will always ensure that large (file) or infinite
 * (socket) buckets are broken into convenient bite sized heap buckets
 * before data is returned to the caller.
 *
 * There is a potential gotcha to watch for: if buckets are read in a
 * loop, and aren't deleted after being processed, the potentially large
 * bucket will slowly be converted into RAM resident heap buckets. If
 * the file is larger than available RAM, an out of memory condition
 * could be caused if the application is not careful to manage this.
 *
 * @param e The bucket to read from
 * @param str The location to store a pointer to the data in
 * @param len The location to store the amount of data read
 * @param block Whether the read function blocks
 */
#define apr_bucket_read(e,str,len,block) (e)->type->read(e, str, len, block)

/**
 * Setaside data so that stack data is not destroyed on returning from
 * the function
 * @param e The bucket to setaside
 * @param p The pool to setaside into
 */
#define apr_bucket_setaside(e,p) (e)->type->setaside(e,p)

/**
 * Split one bucket in two at the point provided.
 * 
 * Once split, the original bucket becomes the first of the two new buckets.
 * 
 * (It is assumed that the bucket is a member of a brigade when this
 * function is called).
 * @param e The bucket to split
 * @param point The offset to split the bucket at
 */
#define apr_bucket_split(e,point) (e)->type->split(e, point)

/**
 * Copy a bucket.
 * @param e The bucket to copy
 * @param c Returns a pointer to the new bucket
 */
#define apr_bucket_copy(e,c) (e)->type->copy(e, c)

/* Bucket type handling */

/**
 * This function simply returns APR_SUCCESS to denote that the bucket does
 * not require anything to happen for its setaside() function. This is
 * appropriate for buckets that have "immortal" data -- the data will live
 * at least as long as the bucket.
 * @param data The bucket to setaside
 * @param pool The pool defining the desired lifetime of the bucket data
 * @return APR_SUCCESS
 */ 
APU_DECLARE_NONSTD(apr_status_t) apr_bucket_setaside_noop(apr_bucket *data,
                                                          apr_pool_t *pool);

/**
 * A place holder function that signifies that the setaside function was not
 * implemented for this bucket
 * @param data The bucket to setaside
 * @param pool The pool defining the desired lifetime of the bucket data
 * @return APR_ENOTIMPL
 */ 
APU_DECLARE_NONSTD(apr_status_t) apr_bucket_setaside_notimpl(apr_bucket *data,
                                                             apr_pool_t *pool);

/**
 * A place holder function that signifies that the split function was not
 * implemented for this bucket
 * @param data The bucket to split
 * @param point The location to split the bucket
 * @return APR_ENOTIMPL
 */ 
APU_DECLARE_NONSTD(apr_status_t) apr_bucket_split_notimpl(apr_bucket *data,
                                                          apr_size_t point);

/**
 * A place holder function that signifies that the copy function was not
 * implemented for this bucket
 * @param e The bucket to copy
 * @param c Returns a pointer to the new bucket
 * @return APR_ENOTIMPL
 */
APU_DECLARE_NONSTD(apr_status_t) apr_bucket_copy_notimpl(apr_bucket *e,
                                                         apr_bucket **c);

/**
 * A place holder function that signifies that this bucket does not need
 * to do anything special to be destroyed.  That's only the case for buckets
 * that either have no data (metadata buckets) or buckets whose data pointer
 * points to something that's not a bucket-type-specific structure, as with
 * simple buckets where data points to a string and pipe buckets where data
 * points directly to the apr_file_t.
 * @param data The bucket data to destroy
 */ 
APU_DECLARE_NONSTD(void) apr_bucket_destroy_noop(void *data);

/**
 * There is no apr_bucket_destroy_notimpl, because destruction is required
 * to be implemented (it could be a noop, but only if that makes sense for
 * the bucket type)
 */

/* There is no apr_bucket_read_notimpl, because it is a required function
 */


/* All of the bucket types implemented by the core */
/**
 * The flush bucket type.  This signifies that all data should be flushed to
 * the next filter.  The flush bucket should be sent with the other buckets.
 */
APU_DECLARE_DATA extern const apr_bucket_type_t apr_bucket_type_flush;
/**
 * The EOS bucket type.  This signifies that there will be no more data, ever.
 * All filters MUST send all data to the next filter when they receive a
 * bucket of this type
 */
APU_DECLARE_DATA extern const apr_bucket_type_t apr_bucket_type_eos;
/**
 * The FILE bucket type.  This bucket represents a file on disk
 */
APU_DECLARE_DATA extern const apr_bucket_type_t apr_bucket_type_file;
/**
 * The HEAP bucket type.  This bucket represents a data allocated from the
 * heap.
 */
APU_DECLARE_DATA extern const apr_bucket_type_t apr_bucket_type_heap;
#if APR_HAS_MMAP
/**
 * The MMAP bucket type.  This bucket represents an MMAP'ed file
 */
APU_DECLARE_DATA extern const apr_bucket_type_t apr_bucket_type_mmap;
#endif
/**
 * The POOL bucket type.  This bucket represents a data that was allocated
 * from a pool.  IF this bucket is still available when the pool is cleared,
 * the data is copied on to the heap.
 */
APU_DECLARE_DATA extern const apr_bucket_type_t apr_bucket_type_pool;
/**
 * The PIPE bucket type.  This bucket represents a pipe to another program.
 */
APU_DECLARE_DATA extern const apr_bucket_type_t apr_bucket_type_pipe;
/**
 * The IMMORTAL bucket type.  This bucket represents a segment of data that
 * the creator is willing to take responsibility for.  The core will do
 * nothing with the data in an immortal bucket
 */
APU_DECLARE_DATA extern const apr_bucket_type_t apr_bucket_type_immortal;
/**
 * The TRANSIENT bucket type.  This bucket represents a data allocated off
 * the stack.  When the setaside function is called, this data is copied on
 * to the heap
 */
APU_DECLARE_DATA extern const apr_bucket_type_t apr_bucket_type_transient;
/**
 * The SOCKET bucket type.  This bucket represents a socket to another machine
 */
APU_DECLARE_DATA extern const apr_bucket_type_t apr_bucket_type_socket;


/*  *****  Simple buckets  *****  */

/**
 * Split a simple bucket into two at the given point.  Most non-reference
 * counting buckets that allow multiple references to the same block of
 * data (eg transient and immortal) will use this as their split function
 * without any additional type-specific handling.
 * @param b The bucket to be split
 * @param point The offset of the first byte in the new bucket
 * @return APR_EINVAL if the point is not within the bucket;
 *         APR_ENOMEM if allocation failed;
 *         or APR_SUCCESS
 */
APU_DECLARE_NONSTD(apr_status_t) apr_bucket_simple_split(apr_bucket *b,
                                                         apr_size_t point);

/**
 * Copy a simple bucket.  Most non-reference-counting buckets that allow
 * multiple references to the same block of data (eg transient and immortal)
 * will use this as their copy function without any additional type-specific
 * handling.
 * @param a The bucket to copy
 * @param b Returns a pointer to the new bucket
 * @return APR_ENOMEM if allocation failed;
 *         or APR_SUCCESS
 */
APU_DECLARE_NONSTD(apr_status_t) apr_bucket_simple_copy(apr_bucket *a,
                                                        apr_bucket **b);


/*  *****  Shared, reference-counted buckets  *****  */

/**
 * Initialize a bucket containing reference-counted data that may be
 * shared. The caller must allocate the bucket if necessary and
 * initialize its type-dependent fields, and allocate and initialize
 * its own private data structure. This function should only be called
 * by type-specific bucket creation functions.
 * @param b The bucket to initialize
 * @param data A pointer to the private data structure
 *             with the reference count at the start
 * @param start The start of the data in the bucket
 *              relative to the private base pointer
 * @param length The length of the data in the bucket
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_shared_make(apr_bucket *b, void *data,
				                 apr_off_t start, 
                                                 apr_size_t length);

/**
 * Decrement the refcount of the data in the bucket. This function
 * should only be called by type-specific bucket destruction functions.
 * @param data The private data pointer from the bucket to be destroyed
 * @return TRUE or FALSE; TRUE if the reference count is now
 *         zero, indicating that the shared resource itself can
 *         be destroyed by the caller.
 */
APU_DECLARE(int) apr_bucket_shared_destroy(void *data);

/**
 * Split a bucket into two at the given point, and adjust the refcount
 * to the underlying data. Most reference-counting bucket types will
 * be able to use this function as their split function without any
 * additional type-specific handling.
 * @param b The bucket to be split
 * @param point The offset of the first byte in the new bucket
 * @return APR_EINVAL if the point is not within the bucket;
 *         APR_ENOMEM if allocation failed;
 *         or APR_SUCCESS
 */
APU_DECLARE_NONSTD(apr_status_t) apr_bucket_shared_split(apr_bucket *b,
                                                         apr_size_t point);

/**
 * Copy a refcounted bucket, incrementing the reference count. Most
 * reference-counting bucket types will be able to use this function
 * as their copy function without any additional type-specific handling.
 * @param a The bucket to copy
 * @param b Returns a pointer to the new bucket
 * @return APR_ENOMEM if allocation failed;
           or APR_SUCCESS
 */
APU_DECLARE_NONSTD(apr_status_t) apr_bucket_shared_copy(apr_bucket *a,
                                                        apr_bucket **b);


/*  *****  Functions to Create Buckets of varying types  *****  */
/*
 * Each bucket type foo has two initialization functions:
 * apr_bucket_foo_make which sets up some already-allocated memory as a
 * bucket of type foo; and apr_bucket_foo_create which allocates memory
 * for the bucket, calls apr_bucket_make_foo, and initializes the
 * bucket's list pointers. The apr_bucket_foo_make functions are used
 * inside the bucket code to change the type of buckets in place;
 * other code should call apr_bucket_foo_create. All the initialization
 * functions change nothing if they fail.
 */

/**
 * Create an End of Stream bucket.  This indicates that there is no more data
 * coming from down the filter stack.  All filters should flush at this point.
 * @param list The freelist from which this bucket should be allocated
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_eos_create(apr_bucket_alloc_t *list);

/**
 * Make the bucket passed in an EOS bucket.  This indicates that there is no 
 * more data coming from down the filter stack.  All filters should flush at 
 * this point.
 * @param b The bucket to make into an EOS bucket
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_eos_make(apr_bucket *b);

/**
 * Create a flush  bucket.  This indicates that filters should flush their
 * data.  There is no guarantee that they will flush it, but this is the
 * best we can do.
 * @param list The freelist from which this bucket should be allocated
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_flush_create(apr_bucket_alloc_t *list);

/**
 * Make the bucket passed in a FLUSH  bucket.  This indicates that filters 
 * should flush their data.  There is no guarantee that they will flush it, 
 * but this is the best we can do.
 * @param b The bucket to make into a FLUSH bucket
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_flush_make(apr_bucket *b);

/**
 * Create a bucket referring to long-lived data.
 * @param buf The data to insert into the bucket
 * @param nbyte The size of the data to insert.
 * @param list The freelist from which this bucket should be allocated
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_immortal_create(const char *buf, 
                                                     apr_size_t nbyte,
                                                     apr_bucket_alloc_t *list);

/**
 * Make the bucket passed in a bucket refer to long-lived data
 * @param b The bucket to make into a IMMORTAL bucket
 * @param buf The data to insert into the bucket
 * @param nbyte The size of the data to insert.
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_immortal_make(apr_bucket *b, 
                                                   const char *buf, 
                                                   apr_size_t nbyte);

/**
 * Create a bucket referring to data on the stack.
 * @param buf The data to insert into the bucket
 * @param nbyte The size of the data to insert.
 * @param list The freelist from which this bucket should be allocated
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_transient_create(const char *buf, 
                                                      apr_size_t nbyte,
                                                      apr_bucket_alloc_t *list);

/**
 * Make the bucket passed in a bucket refer to stack data
 * @param b The bucket to make into a TRANSIENT bucket
 * @param buf The data to insert into the bucket
 * @param nbyte The size of the data to insert.
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_transient_make(apr_bucket *b, 
                                                    const char *buf,
                                                    apr_size_t nbyte);

/**
 * Create a bucket referring to memory on the heap. If the caller asks
 * for the data to be copied, this function always allocates 4K of
 * memory so that more data can be added to the bucket without
 * requiring another allocation. Therefore not all the data may be put
 * into the bucket. If copying is not requested then the bucket takes
 * over responsibility for free()ing the memory.
 * @param buf The buffer to insert into the bucket
 * @param nbyte The size of the buffer to insert.
 * @param free_func Function to use to free the data; NULL indicates that the
 *                  bucket should make a copy of the data
 * @param list The freelist from which this bucket should be allocated
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_heap_create(const char *buf, 
                                                 apr_size_t nbyte,
                                                 void (*free_func)(void *data),
                                                 apr_bucket_alloc_t *list);
/**
 * Make the bucket passed in a bucket refer to heap data
 * @param b The bucket to make into a HEAP bucket
 * @param buf The buffer to insert into the bucket
 * @param nbyte The size of the buffer to insert.
 * @param free_func Function to use to free the data; NULL indicates that the
 *                  bucket should make a copy of the data
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_heap_make(apr_bucket *b, const char *buf,
                                               apr_size_t nbyte,
                                               void (*free_func)(void *data));

/**
 * Create a bucket referring to memory allocated from a pool.
 *
 * @param buf The buffer to insert into the bucket
 * @param length The number of bytes referred to by this bucket
 * @param pool The pool the memory was allocated from
 * @param list The freelist from which this bucket should be allocated
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_pool_create(const char *buf, 
                                                 apr_size_t length,
                                                 apr_pool_t *pool,
                                                 apr_bucket_alloc_t *list);

/**
 * Make the bucket passed in a bucket refer to pool data
 * @param b The bucket to make into a pool bucket
 * @param buf The buffer to insert into the bucket
 * @param length The number of bytes referred to by this bucket
 * @param pool The pool the memory was allocated from
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_pool_make(apr_bucket *b, const char *buf,
                                               apr_size_t length, 
                                               apr_pool_t *pool);

#if APR_HAS_MMAP
/**
 * Create a bucket referring to mmap()ed memory.
 * @param mm The mmap to insert into the bucket
 * @param start The offset of the first byte in the mmap
 *              that this bucket refers to
 * @param length The number of bytes referred to by this bucket
 * @param list The freelist from which this bucket should be allocated
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_mmap_create(apr_mmap_t *mm, 
                                                 apr_off_t start,
                                                 apr_size_t length,
                                                 apr_bucket_alloc_t *list);

/**
 * Make the bucket passed in a bucket refer to an MMAP'ed file
 * @param b The bucket to make into a MMAP bucket
 * @param mm The mmap to insert into the bucket
 * @param start The offset of the first byte in the mmap
 *              that this bucket refers to
 * @param length The number of bytes referred to by this bucket
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_mmap_make(apr_bucket *b, apr_mmap_t *mm,
                                               apr_off_t start, 
                                               apr_size_t length);
#endif

/**
 * Create a bucket referring to a socket.
 * @param thissock The socket to put in the bucket
 * @param list The freelist from which this bucket should be allocated
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_socket_create(apr_socket_t *thissock,
                                                   apr_bucket_alloc_t *list);
/**
 * Make the bucket passed in a bucket refer to a socket
 * @param b The bucket to make into a SOCKET bucket
 * @param thissock The socket to put in the bucket
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_socket_make(apr_bucket *b, 
                                                 apr_socket_t *thissock);

/**
 * Create a bucket referring to a pipe.
 * @param thispipe The pipe to put in the bucket
 * @param list The freelist from which this bucket should be allocated
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_pipe_create(apr_file_t *thispipe,
                                                 apr_bucket_alloc_t *list);

/**
 * Make the bucket passed in a bucket refer to a pipe
 * @param b The bucket to make into a PIPE bucket
 * @param thispipe The pipe to put in the bucket
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_pipe_make(apr_bucket *b, 
                                               apr_file_t *thispipe);

/**
 * Create a bucket referring to a file.
 * @param fd The file to put in the bucket
 * @param offset The offset where the data of interest begins in the file
 * @param len The amount of data in the file we are interested in
 * @param p The pool into which any needed structures should be created
 *          while reading from this file bucket
 * @param list The freelist from which this bucket should be allocated
 * @return The new bucket, or NULL if allocation failed
 * @remark If the file is truncated such that the segment of the file
 * referenced by the bucket no longer exists, an attempt to read
 * from the bucket will fail with APR_EOF. 
 * @remark apr_brigade_insert_file() should generally be used to
 * insert files into brigades, since that function can correctly
 * handle large file issues.
 */
APU_DECLARE(apr_bucket *) apr_bucket_file_create(apr_file_t *fd,
                                                 apr_off_t offset,
                                                 apr_size_t len, 
                                                 apr_pool_t *p,
                                                 apr_bucket_alloc_t *list);

/**
 * Make the bucket passed in a bucket refer to a file
 * @param b The bucket to make into a FILE bucket
 * @param fd The file to put in the bucket
 * @param offset The offset where the data of interest begins in the file
 * @param len The amount of data in the file we are interested in
 * @param p The pool into which any needed structures should be created
 *          while reading from this file bucket
 * @return The new bucket, or NULL if allocation failed
 */
APU_DECLARE(apr_bucket *) apr_bucket_file_make(apr_bucket *b, apr_file_t *fd,
                                               apr_off_t offset,
                                               apr_size_t len, apr_pool_t *p);

/**
 * Enable or disable memory-mapping for a FILE bucket (default is enabled)
 * @param b The bucket
 * @param enabled Whether memory-mapping should be enabled
 * @return APR_SUCCESS normally, or an error code if the operation fails
 */
APU_DECLARE(apr_status_t) apr_bucket_file_enable_mmap(apr_bucket *b,
                                                      int enabled);

/** @} */
#ifdef __cplusplus
}
#endif

#endif /* !APR_BUCKETS_H */
