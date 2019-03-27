/*
 * kmp_error.cpp -- KPTS functions for error checking at runtime
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
#include "kmp_error.h"
#include "kmp_i18n.h"
#include "kmp_str.h"

/* ------------------------------------------------------------------------ */

#define MIN_STACK 100

static char const *cons_text_c[] = {
    "(none)", "\"parallel\"", "work-sharing", /* this is not called "for"
                                                 because of lowering of
                                                 "sections" pragmas */
    "\"ordered\" work-sharing", /* this is not called "for ordered" because of
                                   lowering of "sections" pragmas */
    "\"sections\"",
    "work-sharing", /* this is not called "single" because of lowering of
                       "sections" pragmas */
    "\"taskq\"", "\"taskq\"", "\"taskq ordered\"", "\"critical\"",
    "\"ordered\"", /* in PARALLEL */
    "\"ordered\"", /* in PDO */
    "\"ordered\"", /* in TASKQ */
    "\"master\"", "\"reduce\"", "\"barrier\""};

#define get_src(ident) ((ident) == NULL ? NULL : (ident)->psource)

#define PUSH_MSG(ct, ident)                                                    \
  "\tpushing on stack: %s (%s)\n", cons_text_c[(ct)], get_src((ident))
#define POP_MSG(p)                                                             \
  "\tpopping off stack: %s (%s)\n", cons_text_c[(p)->stack_data[tos].type],    \
      get_src((p)->stack_data[tos].ident)

static int const cons_text_c_num = sizeof(cons_text_c) / sizeof(char const *);

/* --------------- START OF STATIC LOCAL ROUTINES ------------------------- */

static void __kmp_check_null_func(void) { /* nothing to do */
}

static void __kmp_expand_cons_stack(int gtid, struct cons_header *p) {
  int i;
  struct cons_data *d;

  /* TODO for monitor perhaps? */
  if (gtid < 0)
    __kmp_check_null_func();

  KE_TRACE(10, ("expand cons_stack (%d %d)\n", gtid, __kmp_get_gtid()));

  d = p->stack_data;

  p->stack_size = (p->stack_size * 2) + 100;

  /* TODO free the old data */
  p->stack_data = (struct cons_data *)__kmp_allocate(sizeof(struct cons_data) *
                                                     (p->stack_size + 1));

  for (i = p->stack_top; i >= 0; --i)
    p->stack_data[i] = d[i];

  /* NOTE: we do not free the old stack_data */
}

// NOTE: Function returns allocated memory, caller must free it!
static char *__kmp_pragma(int ct, ident_t const *ident) {
  char const *cons = NULL; // Construct name.
  char *file = NULL; // File name.
  char *func = NULL; // Function (routine) name.
  char *line = NULL; // Line number.
  kmp_str_buf_t buffer;
  kmp_msg_t prgm;
  __kmp_str_buf_init(&buffer);
  if (0 < ct && ct < cons_text_c_num) {
    cons = cons_text_c[ct];
  } else {
    KMP_DEBUG_ASSERT(0);
  }
  if (ident != NULL && ident->psource != NULL) {
    char *tail = NULL;
    __kmp_str_buf_print(&buffer, "%s",
                        ident->psource); // Copy source to buffer.
    // Split string in buffer to file, func, and line.
    tail = buffer.str;
    __kmp_str_split(tail, ';', NULL, &tail);
    __kmp_str_split(tail, ';', &file, &tail);
    __kmp_str_split(tail, ';', &func, &tail);
    __kmp_str_split(tail, ';', &line, &tail);
  }
  prgm = __kmp_msg_format(kmp_i18n_fmt_Pragma, cons, file, func, line);
  __kmp_str_buf_free(&buffer);
  return prgm.str;
} // __kmp_pragma

/* ----------------- END OF STATIC LOCAL ROUTINES ------------------------- */

void __kmp_error_construct(kmp_i18n_id_t id, // Message identifier.
                           enum cons_type ct, // Construct type.
                           ident_t const *ident // Construct ident.
                           ) {
  char *construct = __kmp_pragma(ct, ident);
  __kmp_fatal(__kmp_msg_format(id, construct), __kmp_msg_null);
  KMP_INTERNAL_FREE(construct);
}

void __kmp_error_construct2(kmp_i18n_id_t id, // Message identifier.
                            enum cons_type ct, // First construct type.
                            ident_t const *ident, // First construct ident.
                            struct cons_data const *cons // Second construct.
                            ) {
  char *construct1 = __kmp_pragma(ct, ident);
  char *construct2 = __kmp_pragma(cons->type, cons->ident);
  __kmp_fatal(__kmp_msg_format(id, construct1, construct2), __kmp_msg_null);
  KMP_INTERNAL_FREE(construct1);
  KMP_INTERNAL_FREE(construct2);
}

struct cons_header *__kmp_allocate_cons_stack(int gtid) {
  struct cons_header *p;

  /* TODO for monitor perhaps? */
  if (gtid < 0) {
    __kmp_check_null_func();
  }
  KE_TRACE(10, ("allocate cons_stack (%d)\n", gtid));
  p = (struct cons_header *)__kmp_allocate(sizeof(struct cons_header));
  p->p_top = p->w_top = p->s_top = 0;
  p->stack_data = (struct cons_data *)__kmp_allocate(sizeof(struct cons_data) *
                                                     (MIN_STACK + 1));
  p->stack_size = MIN_STACK;
  p->stack_top = 0;
  p->stack_data[0].type = ct_none;
  p->stack_data[0].prev = 0;
  p->stack_data[0].ident = NULL;
  return p;
}

void __kmp_free_cons_stack(void *ptr) {
  struct cons_header *p = (struct cons_header *)ptr;
  if (p != NULL) {
    if (p->stack_data != NULL) {
      __kmp_free(p->stack_data);
      p->stack_data = NULL;
    }
    __kmp_free(p);
  }
}

#if KMP_DEBUG
static void dump_cons_stack(int gtid, struct cons_header *p) {
  int i;
  int tos = p->stack_top;
  kmp_str_buf_t buffer;
  __kmp_str_buf_init(&buffer);
  __kmp_str_buf_print(
      &buffer,
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-\n");
  __kmp_str_buf_print(&buffer,
                      "Begin construct stack with %d items for thread %d\n",
                      tos, gtid);
  __kmp_str_buf_print(&buffer, "     stack_top=%d { P=%d, W=%d, S=%d }\n", tos,
                      p->p_top, p->w_top, p->s_top);
  for (i = tos; i > 0; i--) {
    struct cons_data *c = &(p->stack_data[i]);
    __kmp_str_buf_print(
        &buffer, "        stack_data[%2d] = { %s (%s) %d %p }\n", i,
        cons_text_c[c->type], get_src(c->ident), c->prev, c->name);
  }
  __kmp_str_buf_print(&buffer, "End construct stack for thread %d\n", gtid);
  __kmp_str_buf_print(
      &buffer,
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-\n");
  __kmp_debug_printf("%s", buffer.str);
  __kmp_str_buf_free(&buffer);
}
#endif

void __kmp_push_parallel(int gtid, ident_t const *ident) {
  int tos;
  struct cons_header *p = __kmp_threads[gtid]->th.th_cons;

  KMP_DEBUG_ASSERT(__kmp_threads[gtid]->th.th_cons);
  KE_TRACE(10, ("__kmp_push_parallel (%d %d)\n", gtid, __kmp_get_gtid()));
  KE_TRACE(100, (PUSH_MSG(ct_parallel, ident)));
  if (p->stack_top >= p->stack_size) {
    __kmp_expand_cons_stack(gtid, p);
  }
  tos = ++p->stack_top;
  p->stack_data[tos].type = ct_parallel;
  p->stack_data[tos].prev = p->p_top;
  p->stack_data[tos].ident = ident;
  p->stack_data[tos].name = NULL;
  p->p_top = tos;
  KE_DUMP(1000, dump_cons_stack(gtid, p));
}

void __kmp_check_workshare(int gtid, enum cons_type ct, ident_t const *ident) {
  struct cons_header *p = __kmp_threads[gtid]->th.th_cons;

  KMP_DEBUG_ASSERT(__kmp_threads[gtid]->th.th_cons);
  KE_TRACE(10, ("__kmp_check_workshare (%d %d)\n", gtid, __kmp_get_gtid()));

  if (p->stack_top >= p->stack_size) {
    __kmp_expand_cons_stack(gtid, p);
  }
  if (p->w_top > p->p_top &&
      !(IS_CONS_TYPE_TASKQ(p->stack_data[p->w_top].type) &&
        IS_CONS_TYPE_TASKQ(ct))) {
    // We are already in a WORKSHARE construct for this PARALLEL region.
    __kmp_error_construct2(kmp_i18n_msg_CnsInvalidNesting, ct, ident,
                           &p->stack_data[p->w_top]);
  }
  if (p->s_top > p->p_top) {
    // We are already in a SYNC construct for this PARALLEL region.
    __kmp_error_construct2(kmp_i18n_msg_CnsInvalidNesting, ct, ident,
                           &p->stack_data[p->s_top]);
  }
}

void __kmp_push_workshare(int gtid, enum cons_type ct, ident_t const *ident) {
  int tos;
  struct cons_header *p = __kmp_threads[gtid]->th.th_cons;
  KE_TRACE(10, ("__kmp_push_workshare (%d %d)\n", gtid, __kmp_get_gtid()));
  __kmp_check_workshare(gtid, ct, ident);
  KE_TRACE(100, (PUSH_MSG(ct, ident)));
  tos = ++p->stack_top;
  p->stack_data[tos].type = ct;
  p->stack_data[tos].prev = p->w_top;
  p->stack_data[tos].ident = ident;
  p->stack_data[tos].name = NULL;
  p->w_top = tos;
  KE_DUMP(1000, dump_cons_stack(gtid, p));
}

void
#if KMP_USE_DYNAMIC_LOCK
__kmp_check_sync( int gtid, enum cons_type ct, ident_t const * ident, kmp_user_lock_p lck, kmp_uint32 seq )
#else
__kmp_check_sync( int gtid, enum cons_type ct, ident_t const * ident, kmp_user_lock_p lck )
#endif
{
  struct cons_header *p = __kmp_threads[gtid]->th.th_cons;

  KE_TRACE(10, ("__kmp_check_sync (gtid=%d)\n", __kmp_get_gtid()));

  if (p->stack_top >= p->stack_size)
    __kmp_expand_cons_stack(gtid, p);

  if (ct == ct_ordered_in_parallel || ct == ct_ordered_in_pdo ||
      ct == ct_ordered_in_taskq) {
    if (p->w_top <= p->p_top) {
/* we are not in a worksharing construct */
#ifdef BUILD_PARALLEL_ORDERED
      /* do not report error messages for PARALLEL ORDERED */
      KMP_ASSERT(ct == ct_ordered_in_parallel);
#else
      __kmp_error_construct(kmp_i18n_msg_CnsBoundToWorksharing, ct, ident);
#endif /* BUILD_PARALLEL_ORDERED */
    } else {
      /* inside a WORKSHARING construct for this PARALLEL region */
      if (!IS_CONS_TYPE_ORDERED(p->stack_data[p->w_top].type)) {
        if (p->stack_data[p->w_top].type == ct_taskq) {
          __kmp_error_construct2(kmp_i18n_msg_CnsNotInTaskConstruct, ct, ident,
                                 &p->stack_data[p->w_top]);
        } else {
          __kmp_error_construct2(kmp_i18n_msg_CnsNoOrderedClause, ct, ident,
                                 &p->stack_data[p->w_top]);
        }
      }
    }
    if (p->s_top > p->p_top && p->s_top > p->w_top) {
      /* inside a sync construct which is inside a worksharing construct */
      int index = p->s_top;
      enum cons_type stack_type;

      stack_type = p->stack_data[index].type;

      if (stack_type == ct_critical ||
          ((stack_type == ct_ordered_in_parallel ||
            stack_type == ct_ordered_in_pdo ||
            stack_type ==
                ct_ordered_in_taskq) && /* C doesn't allow named ordered;
                                           ordered in ordered gets error */
           p->stack_data[index].ident != NULL &&
           (p->stack_data[index].ident->flags & KMP_IDENT_KMPC))) {
        /* we are in ORDERED which is inside an ORDERED or CRITICAL construct */
        __kmp_error_construct2(kmp_i18n_msg_CnsInvalidNesting, ct, ident,
                               &p->stack_data[index]);
      }
    }
  } else if (ct == ct_critical) {
#if KMP_USE_DYNAMIC_LOCK
    if (lck != NULL &&
        __kmp_get_user_lock_owner(lck, seq) ==
            gtid) { /* this thread already has lock for this critical section */
#else
    if (lck != NULL &&
        __kmp_get_user_lock_owner(lck) ==
            gtid) { /* this thread already has lock for this critical section */
#endif
      int index = p->s_top;
      struct cons_data cons = {NULL, ct_critical, 0, NULL};
      /* walk up construct stack and try to find critical with matching name */
      while (index != 0 && p->stack_data[index].name != lck) {
        index = p->stack_data[index].prev;
      }
      if (index != 0) {
        /* found match on the stack (may not always because of interleaved
         * critical for Fortran) */
        cons = p->stack_data[index];
      }
      /* we are in CRITICAL which is inside a CRITICAL construct of same name */
      __kmp_error_construct2(kmp_i18n_msg_CnsNestingSameName, ct, ident, &cons);
    }
  } else if (ct == ct_master || ct == ct_reduce) {
    if (p->w_top > p->p_top) {
      /* inside a WORKSHARING construct for this PARALLEL region */
      __kmp_error_construct2(kmp_i18n_msg_CnsInvalidNesting, ct, ident,
                             &p->stack_data[p->w_top]);
    }
    if (ct == ct_reduce && p->s_top > p->p_top) {
      /* inside a another SYNC construct for this PARALLEL region */
      __kmp_error_construct2(kmp_i18n_msg_CnsInvalidNesting, ct, ident,
                             &p->stack_data[p->s_top]);
    }
  }
}

void
#if KMP_USE_DYNAMIC_LOCK
__kmp_push_sync( int gtid, enum cons_type ct, ident_t const * ident, kmp_user_lock_p lck, kmp_uint32 seq )
#else
__kmp_push_sync( int gtid, enum cons_type ct, ident_t const * ident, kmp_user_lock_p lck )
#endif
{
  int tos;
  struct cons_header *p = __kmp_threads[gtid]->th.th_cons;

  KMP_ASSERT(gtid == __kmp_get_gtid());
  KE_TRACE(10, ("__kmp_push_sync (gtid=%d)\n", gtid));
#if KMP_USE_DYNAMIC_LOCK
  __kmp_check_sync(gtid, ct, ident, lck, seq);
#else
  __kmp_check_sync(gtid, ct, ident, lck);
#endif
  KE_TRACE(100, (PUSH_MSG(ct, ident)));
  tos = ++p->stack_top;
  p->stack_data[tos].type = ct;
  p->stack_data[tos].prev = p->s_top;
  p->stack_data[tos].ident = ident;
  p->stack_data[tos].name = lck;
  p->s_top = tos;
  KE_DUMP(1000, dump_cons_stack(gtid, p));
}

/* ------------------------------------------------------------------------ */

void __kmp_pop_parallel(int gtid, ident_t const *ident) {
  int tos;
  struct cons_header *p = __kmp_threads[gtid]->th.th_cons;
  tos = p->stack_top;
  KE_TRACE(10, ("__kmp_pop_parallel (%d %d)\n", gtid, __kmp_get_gtid()));
  if (tos == 0 || p->p_top == 0) {
    __kmp_error_construct(kmp_i18n_msg_CnsDetectedEnd, ct_parallel, ident);
  }
  if (tos != p->p_top || p->stack_data[tos].type != ct_parallel) {
    __kmp_error_construct2(kmp_i18n_msg_CnsExpectedEnd, ct_parallel, ident,
                           &p->stack_data[tos]);
  }
  KE_TRACE(100, (POP_MSG(p)));
  p->p_top = p->stack_data[tos].prev;
  p->stack_data[tos].type = ct_none;
  p->stack_data[tos].ident = NULL;
  p->stack_top = tos - 1;
  KE_DUMP(1000, dump_cons_stack(gtid, p));
}

enum cons_type __kmp_pop_workshare(int gtid, enum cons_type ct,
                                   ident_t const *ident) {
  int tos;
  struct cons_header *p = __kmp_threads[gtid]->th.th_cons;

  tos = p->stack_top;
  KE_TRACE(10, ("__kmp_pop_workshare (%d %d)\n", gtid, __kmp_get_gtid()));
  if (tos == 0 || p->w_top == 0) {
    __kmp_error_construct(kmp_i18n_msg_CnsDetectedEnd, ct, ident);
  }

  if (tos != p->w_top ||
      (p->stack_data[tos].type != ct &&
       // below are two exceptions to the rule that construct types must match
       !(p->stack_data[tos].type == ct_pdo_ordered && ct == ct_pdo) &&
       !(p->stack_data[tos].type == ct_task_ordered && ct == ct_task))) {
    __kmp_check_null_func();
    __kmp_error_construct2(kmp_i18n_msg_CnsExpectedEnd, ct, ident,
                           &p->stack_data[tos]);
  }
  KE_TRACE(100, (POP_MSG(p)));
  p->w_top = p->stack_data[tos].prev;
  p->stack_data[tos].type = ct_none;
  p->stack_data[tos].ident = NULL;
  p->stack_top = tos - 1;
  KE_DUMP(1000, dump_cons_stack(gtid, p));
  return p->stack_data[p->w_top].type;
}

void __kmp_pop_sync(int gtid, enum cons_type ct, ident_t const *ident) {
  int tos;
  struct cons_header *p = __kmp_threads[gtid]->th.th_cons;
  tos = p->stack_top;
  KE_TRACE(10, ("__kmp_pop_sync (%d %d)\n", gtid, __kmp_get_gtid()));
  if (tos == 0 || p->s_top == 0) {
    __kmp_error_construct(kmp_i18n_msg_CnsDetectedEnd, ct, ident);
  }
  if (tos != p->s_top || p->stack_data[tos].type != ct) {
    __kmp_check_null_func();
    __kmp_error_construct2(kmp_i18n_msg_CnsExpectedEnd, ct, ident,
                           &p->stack_data[tos]);
  }
  if (gtid < 0) {
    __kmp_check_null_func();
  }
  KE_TRACE(100, (POP_MSG(p)));
  p->s_top = p->stack_data[tos].prev;
  p->stack_data[tos].type = ct_none;
  p->stack_data[tos].ident = NULL;
  p->stack_top = tos - 1;
  KE_DUMP(1000, dump_cons_stack(gtid, p));
}

/* ------------------------------------------------------------------------ */

void __kmp_check_barrier(int gtid, enum cons_type ct, ident_t const *ident) {
  struct cons_header *p = __kmp_threads[gtid]->th.th_cons;
  KE_TRACE(10, ("__kmp_check_barrier (loc: %p, gtid: %d %d)\n", ident, gtid,
                __kmp_get_gtid()));
  if (ident != 0) {
    __kmp_check_null_func();
  }
  if (p->w_top > p->p_top) {
    /* we are already in a WORKSHARING construct for this PARALLEL region */
    __kmp_error_construct2(kmp_i18n_msg_CnsInvalidNesting, ct, ident,
                           &p->stack_data[p->w_top]);
  }
  if (p->s_top > p->p_top) {
    /* we are already in a SYNC construct for this PARALLEL region */
    __kmp_error_construct2(kmp_i18n_msg_CnsInvalidNesting, ct, ident,
                           &p->stack_data[p->s_top]);
  }
}
