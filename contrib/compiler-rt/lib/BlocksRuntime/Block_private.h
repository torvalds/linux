/*
 * Block_private.h
 *
 * Copyright 2008-2010 Apple, Inc. Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _BLOCK_PRIVATE_H_
#define _BLOCK_PRIVATE_H_

#if !defined(BLOCK_EXPORT)
#   if defined(__cplusplus)
#       define BLOCK_EXPORT extern "C"
#   else
#       define BLOCK_EXPORT extern
#   endif
#endif

#ifndef _MSC_VER
#include <stdbool.h>
#else
/* MSVC doesn't have <stdbool.h>. Compensate. */
typedef char bool;
#define true (bool)1
#define false (bool)0
#endif

#if defined(__cplusplus)
extern "C" {
#endif


enum {
    BLOCK_REFCOUNT_MASK =     (0xffff),
    BLOCK_NEEDS_FREE =        (1 << 24),
    BLOCK_HAS_COPY_DISPOSE =  (1 << 25),
    BLOCK_HAS_CTOR =          (1 << 26), /* Helpers have C++ code. */
    BLOCK_IS_GC =             (1 << 27),
    BLOCK_IS_GLOBAL =         (1 << 28),
    BLOCK_HAS_DESCRIPTOR =    (1 << 29)
};


/* Revised new layout. */
struct Block_descriptor {
    unsigned long int reserved;
    unsigned long int size;
    void (*copy)(void *dst, void *src);
    void (*dispose)(void *);
};


struct Block_layout {
    void *isa;
    int flags;
    int reserved; 
    void (*invoke)(void *, ...);
    struct Block_descriptor *descriptor;
    /* Imported variables. */
};


struct Block_byref {
    void *isa;
    struct Block_byref *forwarding;
    int flags; /* refcount; */
    int size;
    void (*byref_keep)(struct Block_byref *dst, struct Block_byref *src);
    void (*byref_destroy)(struct Block_byref *);
    /* long shared[0]; */
};


struct Block_byref_header {
    void *isa;
    struct Block_byref *forwarding;
    int flags;
    int size;
};


/* Runtime support functions used by compiler when generating copy/dispose helpers. */

enum {
    /* See function implementation for a more complete description of these fields and combinations */
    BLOCK_FIELD_IS_OBJECT   =  3,  /* id, NSObject, __attribute__((NSObject)), block, ... */
    BLOCK_FIELD_IS_BLOCK    =  7,  /* a block variable */
    BLOCK_FIELD_IS_BYREF    =  8,  /* the on stack structure holding the __block variable */
    BLOCK_FIELD_IS_WEAK     = 16,  /* declared __weak, only used in byref copy helpers */
    BLOCK_BYREF_CALLER      = 128  /* called from __block (byref) copy/dispose support routines. */
};

/* Runtime entry point called by compiler when assigning objects inside copy helper routines */
BLOCK_EXPORT void _Block_object_assign(void *destAddr, const void *object, const int flags);
    /* BLOCK_FIELD_IS_BYREF is only used from within block copy helpers */


/* runtime entry point called by the compiler when disposing of objects inside dispose helper routine */
BLOCK_EXPORT void _Block_object_dispose(const void *object, const int flags);



/* Other support functions */

/* Runtime entry to get total size of a closure */
BLOCK_EXPORT unsigned long int Block_size(void *block_basic);



/* the raw data space for runtime classes for blocks */
/* class+meta used for stack, malloc, and collectable based blocks */
BLOCK_EXPORT void * _NSConcreteStackBlock[32];
BLOCK_EXPORT void * _NSConcreteMallocBlock[32];
BLOCK_EXPORT void * _NSConcreteAutoBlock[32];
BLOCK_EXPORT void * _NSConcreteFinalizingBlock[32];
BLOCK_EXPORT void * _NSConcreteGlobalBlock[32];
BLOCK_EXPORT void * _NSConcreteWeakBlockVariable[32];


/* the intercept routines that must be used under GC */
BLOCK_EXPORT void _Block_use_GC( void *(*alloc)(const unsigned long, const bool isOne, const bool isObject),
                                  void (*setHasRefcount)(const void *, const bool),
                                  void (*gc_assign_strong)(void *, void **),
                                  void (*gc_assign_weak)(const void *, void *),
                                  void (*gc_memmove)(void *, void *, unsigned long));

/* earlier version, now simply transitional */
BLOCK_EXPORT void _Block_use_GC5( void *(*alloc)(const unsigned long, const bool isOne, const bool isObject),
                                  void (*setHasRefcount)(const void *, const bool),
                                  void (*gc_assign_strong)(void *, void **),
                                  void (*gc_assign_weak)(const void *, void *));

BLOCK_EXPORT void _Block_use_RR( void (*retain)(const void *),
                                 void (*release)(const void *));

/* make a collectable GC heap based Block.  Not useful under non-GC. */
BLOCK_EXPORT void *_Block_copy_collectable(const void *aBlock);

/* thread-unsafe diagnostic */
BLOCK_EXPORT const char *_Block_dump(const void *block);


/* Obsolete */

/* first layout */
struct Block_basic {
    void *isa;
    int Block_flags;  /* int32_t */
    int Block_size;  /* XXX should be packed into Block_flags */
    void (*Block_invoke)(void *);
    void (*Block_copy)(void *dst, void *src);  /* iff BLOCK_HAS_COPY_DISPOSE */
    void (*Block_dispose)(void *);             /* iff BLOCK_HAS_COPY_DISPOSE */
    /* long params[0];  // where const imports, __block storage references, etc. get laid down */
};


#if defined(__cplusplus)
}
#endif


#endif /* _BLOCK_PRIVATE_H_ */
