/*  -*- buffer-read-only: t -*- vi: set ro:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * Lustre is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \file lustre_dlm_flags.h
 * The flags and collections of flags (masks) for \see struct ldlm_lock.
 *
 * \addtogroup LDLM Lustre Distributed Lock Manager
 * @{
 *
 * \name flags
 * The flags and collections of flags (masks) for \see struct ldlm_lock.
 * @{
 */
#ifndef LDLM_ALL_FLAGS_MASK

/** l_flags bits marked as "all_flags" bits */
#define LDLM_FL_ALL_FLAGS_MASK          0x00FFFFFFC08F932FULL

/** l_flags bits marked as "ast" bits */
#define LDLM_FL_AST_MASK                0x0000000080008000ULL

/** l_flags bits marked as "blocked" bits */
#define LDLM_FL_BLOCKED_MASK            0x000000000000000EULL

/** l_flags bits marked as "gone" bits */
#define LDLM_FL_GONE_MASK               0x0006004000000000ULL

/** l_flags bits marked as "hide_lock" bits */
#define LDLM_FL_HIDE_LOCK_MASK          0x0000206400000000ULL

/** l_flags bits marked as "inherit" bits */
#define LDLM_FL_INHERIT_MASK            0x0000000000800000ULL

/** l_flags bits marked as "local_only" bits */
#define LDLM_FL_LOCAL_ONLY_MASK         0x00FFFFFF00000000ULL

/** l_flags bits marked as "on_wire" bits */
#define LDLM_FL_ON_WIRE_MASK            0x00000000C08F932FULL

/** extent, mode, or resource changed */
#define LDLM_FL_LOCK_CHANGED            0x0000000000000001ULL /* bit 0 */
#define ldlm_is_lock_changed(_l)        LDLM_TEST_FLAG((_l), 1ULL <<  0)
#define ldlm_set_lock_changed(_l)       LDLM_SET_FLAG((_l), 1ULL <<  0)
#define ldlm_clear_lock_changed(_l)     LDLM_CLEAR_FLAG((_l), 1ULL <<  0)

/**
 * Server placed lock on granted list, or a recovering client wants the
 * lock added to the granted list, no questions asked.
 */
#define LDLM_FL_BLOCK_GRANTED           0x0000000000000002ULL /* bit 1 */
#define ldlm_is_block_granted(_l)       LDLM_TEST_FLAG((_l), 1ULL <<  1)
#define ldlm_set_block_granted(_l)      LDLM_SET_FLAG((_l), 1ULL <<  1)
#define ldlm_clear_block_granted(_l)    LDLM_CLEAR_FLAG((_l), 1ULL <<  1)

/**
 * Server placed lock on conv list, or a recovering client wants the lock
 * added to the conv list, no questions asked.
 */
#define LDLM_FL_BLOCK_CONV              0x0000000000000004ULL /* bit 2 */
#define ldlm_is_block_conv(_l)          LDLM_TEST_FLAG((_l), 1ULL <<  2)
#define ldlm_set_block_conv(_l)         LDLM_SET_FLAG((_l), 1ULL <<  2)
#define ldlm_clear_block_conv(_l)       LDLM_CLEAR_FLAG((_l), 1ULL <<  2)

/**
 * Server placed lock on wait list, or a recovering client wants the lock
 * added to the wait list, no questions asked.
 */
#define LDLM_FL_BLOCK_WAIT              0x0000000000000008ULL /* bit 3 */
#define ldlm_is_block_wait(_l)          LDLM_TEST_FLAG((_l), 1ULL <<  3)
#define ldlm_set_block_wait(_l)         LDLM_SET_FLAG((_l), 1ULL <<  3)
#define ldlm_clear_block_wait(_l)       LDLM_CLEAR_FLAG((_l), 1ULL <<  3)

/** blocking or cancel packet was queued for sending. */
#define LDLM_FL_AST_SENT                0x0000000000000020ULL /* bit 5 */
#define ldlm_is_ast_sent(_l)            LDLM_TEST_FLAG((_l), 1ULL <<  5)
#define ldlm_set_ast_sent(_l)           LDLM_SET_FLAG((_l), 1ULL <<  5)
#define ldlm_clear_ast_sent(_l)         LDLM_CLEAR_FLAG((_l), 1ULL <<  5)

/**
 * Lock is being replayed.  This could probably be implied by the fact that
 * one of BLOCK_{GRANTED,CONV,WAIT} is set, but that is pretty dangerous.
 */
#define LDLM_FL_REPLAY                  0x0000000000000100ULL /* bit 8 */
#define ldlm_is_replay(_l)              LDLM_TEST_FLAG((_l), 1ULL <<  8)
#define ldlm_set_replay(_l)             LDLM_SET_FLAG((_l), 1ULL <<  8)
#define ldlm_clear_replay(_l)           LDLM_CLEAR_FLAG((_l), 1ULL <<  8)

/** Don't grant lock, just do intent. */
#define LDLM_FL_INTENT_ONLY             0x0000000000000200ULL /* bit 9 */
#define ldlm_is_intent_only(_l)         LDLM_TEST_FLAG((_l), 1ULL <<  9)
#define ldlm_set_intent_only(_l)        LDLM_SET_FLAG((_l), 1ULL <<  9)
#define ldlm_clear_intent_only(_l)      LDLM_CLEAR_FLAG((_l), 1ULL <<  9)

/** lock request has intent */
#define LDLM_FL_HAS_INTENT              0x0000000000001000ULL /* bit 12 */
#define ldlm_is_has_intent(_l)          LDLM_TEST_FLAG((_l), 1ULL << 12)
#define ldlm_set_has_intent(_l)         LDLM_SET_FLAG((_l), 1ULL << 12)
#define ldlm_clear_has_intent(_l)       LDLM_CLEAR_FLAG((_l), 1ULL << 12)

/** flock deadlock detected */
#define LDLM_FL_FLOCK_DEADLOCK          0x0000000000008000ULL /* bit  15 */
#define ldlm_is_flock_deadlock(_l)      LDLM_TEST_FLAG((_l), 1ULL << 15)
#define ldlm_set_flock_deadlock(_l)     LDLM_SET_FLAG((_l), 1ULL << 15)
#define ldlm_clear_flock_deadlock(_l)   LDLM_CLEAR_FLAG((_l), 1ULL << 15)

/** discard (no writeback) on cancel */
#define LDLM_FL_DISCARD_DATA            0x0000000000010000ULL /* bit 16 */
#define ldlm_is_discard_data(_l)        LDLM_TEST_FLAG((_l), 1ULL << 16)
#define ldlm_set_discard_data(_l)       LDLM_SET_FLAG((_l), 1ULL << 16)
#define ldlm_clear_discard_data(_l)     LDLM_CLEAR_FLAG((_l), 1ULL << 16)

/** Blocked by group lock - wait indefinitely */
#define LDLM_FL_NO_TIMEOUT              0x0000000000020000ULL /* bit 17 */
#define ldlm_is_no_timeout(_l)          LDLM_TEST_FLAG((_l), 1ULL << 17)
#define ldlm_set_no_timeout(_l)         LDLM_SET_FLAG((_l), 1ULL << 17)
#define ldlm_clear_no_timeout(_l)       LDLM_CLEAR_FLAG((_l), 1ULL << 17)

/**
 * Server told not to wait if blocked. For AGL, OST will not send glimpse
 * callback.
 */
#define LDLM_FL_BLOCK_NOWAIT            0x0000000000040000ULL /* bit 18 */
#define ldlm_is_block_nowait(_l)        LDLM_TEST_FLAG((_l), 1ULL << 18)
#define ldlm_set_block_nowait(_l)       LDLM_SET_FLAG((_l), 1ULL << 18)
#define ldlm_clear_block_nowait(_l)     LDLM_CLEAR_FLAG((_l), 1ULL << 18)

/** return blocking lock */
#define LDLM_FL_TEST_LOCK               0x0000000000080000ULL /* bit 19 */
#define ldlm_is_test_lock(_l)           LDLM_TEST_FLAG((_l), 1ULL << 19)
#define ldlm_set_test_lock(_l)          LDLM_SET_FLAG((_l), 1ULL << 19)
#define ldlm_clear_test_lock(_l)        LDLM_CLEAR_FLAG((_l), 1ULL << 19)

/**
 * Immediately cancel such locks when they block some other locks. Send
 * cancel notification to original lock holder, but expect no reply. This
 * is for clients (like liblustre) that cannot be expected to reliably
 * response to blocking AST.
 */
#define LDLM_FL_CANCEL_ON_BLOCK         0x0000000000800000ULL /* bit 23 */
#define ldlm_is_cancel_on_block(_l)     LDLM_TEST_FLAG((_l), 1ULL << 23)
#define ldlm_set_cancel_on_block(_l)    LDLM_SET_FLAG((_l), 1ULL << 23)
#define ldlm_clear_cancel_on_block(_l)  LDLM_CLEAR_FLAG((_l), 1ULL << 23)

/**
 * measure lock contention and return -EUSERS if locking contention is high */
#define LDLM_FL_DENY_ON_CONTENTION        0x0000000040000000ULL /* bit 30 */
#define ldlm_is_deny_on_contention(_l)    LDLM_TEST_FLAG((_l), 1ULL << 30)
#define ldlm_set_deny_on_contention(_l)   LDLM_SET_FLAG((_l), 1ULL << 30)
#define ldlm_clear_deny_on_contention(_l) LDLM_CLEAR_FLAG((_l), 1ULL << 30)

/**
 * These are flags that are mapped into the flags and ASTs of blocking
 * locks Add FL_DISCARD to blocking ASTs */
#define LDLM_FL_AST_DISCARD_DATA        0x0000000080000000ULL /* bit 31 */
#define ldlm_is_ast_discard_data(_l)    LDLM_TEST_FLAG((_l), 1ULL << 31)
#define ldlm_set_ast_discard_data(_l)   LDLM_SET_FLAG((_l), 1ULL << 31)
#define ldlm_clear_ast_discard_data(_l) LDLM_CLEAR_FLAG((_l), 1ULL << 31)

/**
 * Used for marking lock as a target for -EINTR while cp_ast sleep emulation
 * + race with upcoming bl_ast.
 */
#define LDLM_FL_FAIL_LOC                0x0000000100000000ULL /* bit 32 */
#define ldlm_is_fail_loc(_l)            LDLM_TEST_FLAG((_l), 1ULL << 32)
#define ldlm_set_fail_loc(_l)           LDLM_SET_FLAG((_l), 1ULL << 32)
#define ldlm_clear_fail_loc(_l)         LDLM_CLEAR_FLAG((_l), 1ULL << 32)

/**
 * Used while processing the unused list to know that we have already
 * handled this lock and decided to skip it.
 */
#define LDLM_FL_SKIPPED                 0x0000000200000000ULL /* bit 33 */
#define ldlm_is_skipped(_l)             LDLM_TEST_FLAG((_l), 1ULL << 33)
#define ldlm_set_skipped(_l)            LDLM_SET_FLAG((_l), 1ULL << 33)
#define ldlm_clear_skipped(_l)          LDLM_CLEAR_FLAG((_l), 1ULL << 33)

/** this lock is being destroyed */
#define LDLM_FL_CBPENDING               0x0000000400000000ULL /* bit 34 */
#define ldlm_is_cbpending(_l)           LDLM_TEST_FLAG((_l), 1ULL << 34)
#define ldlm_set_cbpending(_l)          LDLM_SET_FLAG((_l), 1ULL << 34)
#define ldlm_clear_cbpending(_l)        LDLM_CLEAR_FLAG((_l), 1ULL << 34)

/** not a real flag, not saved in lock */
#define LDLM_FL_WAIT_NOREPROC           0x0000000800000000ULL /* bit 35 */
#define ldlm_is_wait_noreproc(_l)       LDLM_TEST_FLAG((_l), 1ULL << 35)
#define ldlm_set_wait_noreproc(_l)      LDLM_SET_FLAG((_l), 1ULL << 35)
#define ldlm_clear_wait_noreproc(_l)    LDLM_CLEAR_FLAG((_l), 1ULL << 35)

/** cancellation callback already run */
#define LDLM_FL_CANCEL                  0x0000001000000000ULL /* bit 36 */
#define ldlm_is_cancel(_l)              LDLM_TEST_FLAG((_l), 1ULL << 36)
#define ldlm_set_cancel(_l)             LDLM_SET_FLAG((_l), 1ULL << 36)
#define ldlm_clear_cancel(_l)           LDLM_CLEAR_FLAG((_l), 1ULL << 36)

/** whatever it might mean */
#define LDLM_FL_LOCAL_ONLY              0x0000002000000000ULL /* bit 37 */
#define ldlm_is_local_only(_l)          LDLM_TEST_FLAG((_l), 1ULL << 37)
#define ldlm_set_local_only(_l)         LDLM_SET_FLAG((_l), 1ULL << 37)
#define ldlm_clear_local_only(_l)       LDLM_CLEAR_FLAG((_l), 1ULL << 37)

/** don't run the cancel callback under ldlm_cli_cancel_unused */
#define LDLM_FL_FAILED                  0x0000004000000000ULL /* bit 38 */
#define ldlm_is_failed(_l)              LDLM_TEST_FLAG((_l), 1ULL << 38)
#define ldlm_set_failed(_l)             LDLM_SET_FLAG((_l), 1ULL << 38)
#define ldlm_clear_failed(_l)           LDLM_CLEAR_FLAG((_l), 1ULL << 38)

/** lock cancel has already been sent */
#define LDLM_FL_CANCELING               0x0000008000000000ULL /* bit 39 */
#define ldlm_is_canceling(_l)           LDLM_TEST_FLAG((_l), 1ULL << 39)
#define ldlm_set_canceling(_l)          LDLM_SET_FLAG((_l), 1ULL << 39)
#define ldlm_clear_canceling(_l)        LDLM_CLEAR_FLAG((_l), 1ULL << 39)

/** local lock (ie, no srv/cli split) */
#define LDLM_FL_LOCAL                   0x0000010000000000ULL /* bit 40 */
#define ldlm_is_local(_l)               LDLM_TEST_FLAG((_l), 1ULL << 40)
#define ldlm_set_local(_l)              LDLM_SET_FLAG((_l), 1ULL << 40)
#define ldlm_clear_local(_l)            LDLM_CLEAR_FLAG((_l), 1ULL << 40)

/**
 * XXX FIXME: This is being added to b_size as a low-risk fix to the
 * fact that the LVB filling happens _after_ the lock has been granted,
 * so another thread can match it before the LVB has been updated.  As a
 * dirty hack, we set LDLM_FL_LVB_READY only after we've done the LVB poop.
 * this is only needed on LOV/OSC now, where LVB is actually used and
 * callers must set it in input flags.
 *
 * The proper fix is to do the granting inside of the completion AST,
 * which can be replaced with a LVB-aware wrapping function for OSC locks.
 * That change is pretty high-risk, though, and would need a lot more
 * testing.
 */
#define LDLM_FL_LVB_READY               0x0000020000000000ULL /* bit 41 */
#define ldlm_is_lvb_ready(_l)           LDLM_TEST_FLAG((_l), 1ULL << 41)
#define ldlm_set_lvb_ready(_l)          LDLM_SET_FLAG((_l), 1ULL << 41)
#define ldlm_clear_lvb_ready(_l)        LDLM_CLEAR_FLAG((_l), 1ULL << 41)

/**
 * A lock contributes to the known minimum size (KMS) calculation until it
 * has finished the part of its cancellation that performs write back on its
 * dirty pages.  It can remain on the granted list during this whole time.
 * Threads racing to update the KMS after performing their writeback need
 * to know to exclude each other's locks from the calculation as they walk
 * the granted list.
 */
#define LDLM_FL_KMS_IGNORE              0x0000040000000000ULL /* bit 42 */
#define ldlm_is_kms_ignore(_l)          LDLM_TEST_FLAG((_l), 1ULL << 42)
#define ldlm_set_kms_ignore(_l)         LDLM_SET_FLAG((_l), 1ULL << 42)
#define ldlm_clear_kms_ignore(_l)       LDLM_CLEAR_FLAG((_l), 1ULL << 42)

/** completion AST to be executed */
#define LDLM_FL_CP_REQD                 0x0000080000000000ULL /* bit 43 */
#define ldlm_is_cp_reqd(_l)             LDLM_TEST_FLAG((_l), 1ULL << 43)
#define ldlm_set_cp_reqd(_l)            LDLM_SET_FLAG((_l), 1ULL << 43)
#define ldlm_clear_cp_reqd(_l)          LDLM_CLEAR_FLAG((_l), 1ULL << 43)

/** cleanup_resource has already handled the lock */
#define LDLM_FL_CLEANED                 0x0000100000000000ULL /* bit 44 */
#define ldlm_is_cleaned(_l)             LDLM_TEST_FLAG((_l), 1ULL << 44)
#define ldlm_set_cleaned(_l)            LDLM_SET_FLAG((_l), 1ULL << 44)
#define ldlm_clear_cleaned(_l)          LDLM_CLEAR_FLAG((_l), 1ULL << 44)

/**
 * optimization hint: LDLM can run blocking callback from current context
 * w/o involving separate thread. in order to decrease cs rate
 */
#define LDLM_FL_ATOMIC_CB               0x0000200000000000ULL /* bit 45 */
#define ldlm_is_atomic_cb(_l)           LDLM_TEST_FLAG((_l), 1ULL << 45)
#define ldlm_set_atomic_cb(_l)          LDLM_SET_FLAG((_l), 1ULL << 45)
#define ldlm_clear_atomic_cb(_l)        LDLM_CLEAR_FLAG((_l), 1ULL << 45)

/**
 * It may happen that a client initiates two operations, e.g. unlink and
 * mkdir, such that the server sends a blocking AST for conflicting locks
 * to this client for the first operation, whereas the second operation
 * has canceled this lock and is waiting for rpc_lock which is taken by
 * the first operation. LDLM_FL_BL_AST is set by ldlm_callback_handler() in
 * the lock to prevent the Early Lock Cancel (ELC) code from cancelling it.
 *
 * LDLM_FL_BL_DONE is to be set by ldlm_cancel_callback() when lock cache is
 * dropped to let ldlm_callback_handler() return EINVAL to the server. It
 * is used when ELC RPC is already prepared and is waiting for rpc_lock,
 * too late to send a separate CANCEL RPC.
 */
#define LDLM_FL_BL_AST                  0x0000400000000000ULL /* bit 46 */
#define ldlm_is_bl_ast(_l)              LDLM_TEST_FLAG((_l), 1ULL << 46)
#define ldlm_set_bl_ast(_l)             LDLM_SET_FLAG((_l), 1ULL << 46)
#define ldlm_clear_bl_ast(_l)           LDLM_CLEAR_FLAG((_l), 1ULL << 46)

/** whatever it might mean */
#define LDLM_FL_BL_DONE                 0x0000800000000000ULL /* bit 47 */
#define ldlm_is_bl_done(_l)             LDLM_TEST_FLAG((_l), 1ULL << 47)
#define ldlm_set_bl_done(_l)            LDLM_SET_FLAG((_l), 1ULL << 47)
#define ldlm_clear_bl_done(_l)          LDLM_CLEAR_FLAG((_l), 1ULL << 47)

/**
 * Don't put lock into the LRU list, so that it is not canceled due
 * to aging.  Used by MGC locks, they are cancelled only at unmount or
 * by callback.
 */
#define LDLM_FL_NO_LRU                  0x0001000000000000ULL /* bit 48 */
#define ldlm_is_no_lru(_l)              LDLM_TEST_FLAG((_l), 1ULL << 48)
#define ldlm_set_no_lru(_l)             LDLM_SET_FLAG((_l), 1ULL << 48)
#define ldlm_clear_no_lru(_l)           LDLM_CLEAR_FLAG((_l), 1ULL << 48)

/**
 * Set for locks that failed and where the server has been notified.
 *
 * Protected by lock and resource locks.
 */
#define LDLM_FL_FAIL_NOTIFIED           0x0002000000000000ULL /* bit 49 */
#define ldlm_is_fail_notified(_l)       LDLM_TEST_FLAG((_l), 1ULL << 49)
#define ldlm_set_fail_notified(_l)      LDLM_SET_FLAG((_l), 1ULL << 49)
#define ldlm_clear_fail_notified(_l)    LDLM_CLEAR_FLAG((_l), 1ULL << 49)

/**
 * Set for locks that were removed from class hash table and will
 * be destroyed when last reference to them is released. Set by
 * ldlm_lock_destroy_internal().
 *
 * Protected by lock and resource locks.
 */
#define LDLM_FL_DESTROYED               0x0004000000000000ULL /* bit 50 */
#define ldlm_is_destroyed(_l)           LDLM_TEST_FLAG((_l), 1ULL << 50)
#define ldlm_set_destroyed(_l)          LDLM_SET_FLAG((_l), 1ULL << 50)
#define ldlm_clear_destroyed(_l)        LDLM_CLEAR_FLAG((_l), 1ULL << 50)

/** flag whether this is a server namespace lock */
#define LDLM_FL_SERVER_LOCK             0x0008000000000000ULL /* bit 51 */
#define ldlm_is_server_lock(_l)         LDLM_TEST_FLAG((_l), 1ULL << 51)
#define ldlm_set_server_lock(_l)        LDLM_SET_FLAG((_l), 1ULL << 51)
#define ldlm_clear_server_lock(_l)      LDLM_CLEAR_FLAG((_l), 1ULL << 51)

/**
 * It's set in lock_res_and_lock() and unset in unlock_res_and_lock().
 *
 * NB: compared with check_res_locked(), checking this bit is cheaper.
 * Also, spin_is_locked() is deprecated for kernel code; one reason is
 * because it works only for SMP so user needs to add extra macros like
 * LASSERT_SPIN_LOCKED for uniprocessor kernels.
 */
#define LDLM_FL_RES_LOCKED              0x0010000000000000ULL /* bit 52 */
#define ldlm_is_res_locked(_l)          LDLM_TEST_FLAG((_l), 1ULL << 52)
#define ldlm_set_res_locked(_l)         LDLM_SET_FLAG((_l), 1ULL << 52)
#define ldlm_clear_res_locked(_l)       LDLM_CLEAR_FLAG((_l), 1ULL << 52)

/**
 * It's set once we call ldlm_add_waiting_lock_res_locked() to start the
 * lock-timeout timer and it will never be reset.
 *
 * Protected by lock and resource locks.
 */
#define LDLM_FL_WAITED                  0x0020000000000000ULL /* bit 53 */
#define ldlm_is_waited(_l)              LDLM_TEST_FLAG((_l), 1ULL << 53)
#define ldlm_set_waited(_l)             LDLM_SET_FLAG((_l), 1ULL << 53)
#define ldlm_clear_waited(_l)           LDLM_CLEAR_FLAG((_l), 1ULL << 53)

/** Flag whether this is a server namespace lock. */
#define LDLM_FL_NS_SRV                  0x0040000000000000ULL /* bit 54 */
#define ldlm_is_ns_srv(_l)              LDLM_TEST_FLAG((_l), 1ULL << 54)
#define ldlm_set_ns_srv(_l)             LDLM_SET_FLAG((_l), 1ULL << 54)
#define ldlm_clear_ns_srv(_l)           LDLM_CLEAR_FLAG((_l), 1ULL << 54)

/** Flag whether this lock can be reused. Used by exclusive open. */
#define LDLM_FL_EXCL                    0x0080000000000000ULL /* bit  55 */
#define ldlm_is_excl(_l)                LDLM_TEST_FLAG((_l), 1ULL << 55)
#define ldlm_set_excl(_l)               LDLM_SET_FLAG((_l), 1ULL << 55)
#define ldlm_clear_excl(_l)             LDLM_CLEAR_FLAG((_l), 1ULL << 55)

/** test for ldlm_lock flag bit set */
#define LDLM_TEST_FLAG(_l, _b)        (((_l)->l_flags & (_b)) != 0)

/** set a ldlm_lock flag bit */
#define LDLM_SET_FLAG(_l, _b)         ((_l)->l_flags |= (_b))

/** clear a ldlm_lock flag bit */
#define LDLM_CLEAR_FLAG(_l, _b)       ((_l)->l_flags &= ~(_b))

/** Mask of flags inherited from parent lock when doing intents. */
#define LDLM_INHERIT_FLAGS            LDLM_FL_INHERIT_MASK

/** Mask of Flags sent in AST lock_flags to map into the receiving lock. */
#define LDLM_AST_FLAGS                LDLM_FL_AST_MASK

/** @} subgroup */
/** @} group */
#ifdef WIRESHARK_COMPILE
static int hf_lustre_ldlm_fl_lock_changed        = -1;
static int hf_lustre_ldlm_fl_block_granted       = -1;
static int hf_lustre_ldlm_fl_block_conv          = -1;
static int hf_lustre_ldlm_fl_block_wait          = -1;
static int hf_lustre_ldlm_fl_ast_sent            = -1;
static int hf_lustre_ldlm_fl_replay              = -1;
static int hf_lustre_ldlm_fl_intent_only         = -1;
static int hf_lustre_ldlm_fl_has_intent          = -1;
static int hf_lustre_ldlm_fl_flock_deadlock      = -1;
static int hf_lustre_ldlm_fl_discard_data        = -1;
static int hf_lustre_ldlm_fl_no_timeout          = -1;
static int hf_lustre_ldlm_fl_block_nowait        = -1;
static int hf_lustre_ldlm_fl_test_lock           = -1;
static int hf_lustre_ldlm_fl_cancel_on_block     = -1;
static int hf_lustre_ldlm_fl_deny_on_contention  = -1;
static int hf_lustre_ldlm_fl_ast_discard_data    = -1;
static int hf_lustre_ldlm_fl_fail_loc            = -1;
static int hf_lustre_ldlm_fl_skipped             = -1;
static int hf_lustre_ldlm_fl_cbpending           = -1;
static int hf_lustre_ldlm_fl_wait_noreproc       = -1;
static int hf_lustre_ldlm_fl_cancel              = -1;
static int hf_lustre_ldlm_fl_local_only          = -1;
static int hf_lustre_ldlm_fl_failed              = -1;
static int hf_lustre_ldlm_fl_canceling           = -1;
static int hf_lustre_ldlm_fl_local               = -1;
static int hf_lustre_ldlm_fl_lvb_ready           = -1;
static int hf_lustre_ldlm_fl_kms_ignore          = -1;
static int hf_lustre_ldlm_fl_cp_reqd             = -1;
static int hf_lustre_ldlm_fl_cleaned             = -1;
static int hf_lustre_ldlm_fl_atomic_cb           = -1;
static int hf_lustre_ldlm_fl_bl_ast              = -1;
static int hf_lustre_ldlm_fl_bl_done             = -1;
static int hf_lustre_ldlm_fl_no_lru              = -1;
static int hf_lustre_ldlm_fl_fail_notified       = -1;
static int hf_lustre_ldlm_fl_destroyed           = -1;
static int hf_lustre_ldlm_fl_server_lock         = -1;
static int hf_lustre_ldlm_fl_res_locked          = -1;
static int hf_lustre_ldlm_fl_waited              = -1;
static int hf_lustre_ldlm_fl_ns_srv              = -1;
static int hf_lustre_ldlm_fl_excl                = -1;

const value_string lustre_ldlm_flags_vals[] = {
	{LDLM_FL_LOCK_CHANGED,        "LDLM_FL_LOCK_CHANGED"},
	{LDLM_FL_BLOCK_GRANTED,       "LDLM_FL_BLOCK_GRANTED"},
	{LDLM_FL_BLOCK_CONV,          "LDLM_FL_BLOCK_CONV"},
	{LDLM_FL_BLOCK_WAIT,          "LDLM_FL_BLOCK_WAIT"},
	{LDLM_FL_AST_SENT,            "LDLM_FL_AST_SENT"},
	{LDLM_FL_REPLAY,              "LDLM_FL_REPLAY"},
	{LDLM_FL_INTENT_ONLY,         "LDLM_FL_INTENT_ONLY"},
	{LDLM_FL_HAS_INTENT,          "LDLM_FL_HAS_INTENT"},
	{LDLM_FL_FLOCK_DEADLOCK,      "LDLM_FL_FLOCK_DEADLOCK"},
	{LDLM_FL_DISCARD_DATA,        "LDLM_FL_DISCARD_DATA"},
	{LDLM_FL_NO_TIMEOUT,          "LDLM_FL_NO_TIMEOUT"},
	{LDLM_FL_BLOCK_NOWAIT,        "LDLM_FL_BLOCK_NOWAIT"},
	{LDLM_FL_TEST_LOCK,           "LDLM_FL_TEST_LOCK"},
	{LDLM_FL_CANCEL_ON_BLOCK,     "LDLM_FL_CANCEL_ON_BLOCK"},
	{LDLM_FL_DENY_ON_CONTENTION,  "LDLM_FL_DENY_ON_CONTENTION"},
	{LDLM_FL_AST_DISCARD_DATA,    "LDLM_FL_AST_DISCARD_DATA"},
	{LDLM_FL_FAIL_LOC,            "LDLM_FL_FAIL_LOC"},
	{LDLM_FL_SKIPPED,             "LDLM_FL_SKIPPED"},
	{LDLM_FL_CBPENDING,           "LDLM_FL_CBPENDING"},
	{LDLM_FL_WAIT_NOREPROC,       "LDLM_FL_WAIT_NOREPROC"},
	{LDLM_FL_CANCEL,              "LDLM_FL_CANCEL"},
	{LDLM_FL_LOCAL_ONLY,          "LDLM_FL_LOCAL_ONLY"},
	{LDLM_FL_FAILED,              "LDLM_FL_FAILED"},
	{LDLM_FL_CANCELING,           "LDLM_FL_CANCELING"},
	{LDLM_FL_LOCAL,               "LDLM_FL_LOCAL"},
	{LDLM_FL_LVB_READY,           "LDLM_FL_LVB_READY"},
	{LDLM_FL_KMS_IGNORE,          "LDLM_FL_KMS_IGNORE"},
	{LDLM_FL_CP_REQD,             "LDLM_FL_CP_REQD"},
	{LDLM_FL_CLEANED,             "LDLM_FL_CLEANED"},
	{LDLM_FL_ATOMIC_CB,           "LDLM_FL_ATOMIC_CB"},
	{LDLM_FL_BL_AST,              "LDLM_FL_BL_AST"},
	{LDLM_FL_BL_DONE,             "LDLM_FL_BL_DONE"},
	{LDLM_FL_NO_LRU,              "LDLM_FL_NO_LRU"},
	{LDLM_FL_FAIL_NOTIFIED,       "LDLM_FL_FAIL_NOTIFIED"},
	{LDLM_FL_DESTROYED,           "LDLM_FL_DESTROYED"},
	{LDLM_FL_SERVER_LOCK,         "LDLM_FL_SERVER_LOCK"},
	{LDLM_FL_RES_LOCKED,          "LDLM_FL_RES_LOCKED"},
	{LDLM_FL_WAITED,              "LDLM_FL_WAITED"},
	{LDLM_FL_NS_SRV,              "LDLM_FL_NS_SRV"},
	{LDLM_FL_EXCL,                "LDLM_FL_EXCL"},
	{ 0, NULL }
};
#endif /*  WIRESHARK_COMPILE */
#endif /* LDLM_ALL_FLAGS_MASK */
