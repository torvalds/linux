#ifndef DRBD_STATE_H
#define DRBD_STATE_H

struct drbd_conf;
struct drbd_tconn;

/**
 * DOC: DRBD State macros
 *
 * These macros are used to express state changes in easily readable form.
 *
 * The NS macros expand to a mask and a value, that can be bit ored onto the
 * current state as soon as the spinlock (req_lock) was taken.
 *
 * The _NS macros are used for state functions that get called with the
 * spinlock. These macros expand directly to the new state value.
 *
 * Besides the basic forms NS() and _NS() additional _?NS[23] are defined
 * to express state changes that affect more than one aspect of the state.
 *
 * E.g. NS2(conn, C_CONNECTED, peer, R_SECONDARY)
 * Means that the network connection was established and that the peer
 * is in secondary role.
 */
#define role_MASK R_MASK
#define peer_MASK R_MASK
#define disk_MASK D_MASK
#define pdsk_MASK D_MASK
#define conn_MASK C_MASK
#define susp_MASK 1
#define user_isp_MASK 1
#define aftr_isp_MASK 1
#define susp_nod_MASK 1
#define susp_fen_MASK 1

#define NS(T, S) \
	({ union drbd_state mask; mask.i = 0; mask.T = T##_MASK; mask; }), \
	({ union drbd_state val; val.i = 0; val.T = (S); val; })
#define NS2(T1, S1, T2, S2) \
	({ union drbd_state mask; mask.i = 0; mask.T1 = T1##_MASK; \
	  mask.T2 = T2##_MASK; mask; }), \
	({ union drbd_state val; val.i = 0; val.T1 = (S1); \
	  val.T2 = (S2); val; })
#define NS3(T1, S1, T2, S2, T3, S3) \
	({ union drbd_state mask; mask.i = 0; mask.T1 = T1##_MASK; \
	  mask.T2 = T2##_MASK; mask.T3 = T3##_MASK; mask; }), \
	({ union drbd_state val;  val.i = 0; val.T1 = (S1); \
	  val.T2 = (S2); val.T3 = (S3); val; })

#define _NS(D, T, S) \
	D, ({ union drbd_state __ns; __ns.i = D->state.i; __ns.T = (S); __ns; })
#define _NS2(D, T1, S1, T2, S2) \
	D, ({ union drbd_state __ns; __ns.i = D->state.i; __ns.T1 = (S1); \
	__ns.T2 = (S2); __ns; })
#define _NS3(D, T1, S1, T2, S2, T3, S3) \
	D, ({ union drbd_state __ns; __ns.i = D->state.i; __ns.T1 = (S1); \
	__ns.T2 = (S2); __ns.T3 = (S3); __ns; })

enum chg_state_flags {
	CS_HARD	= 1,
	CS_VERBOSE = 2,
	CS_WAIT_COMPLETE = 4,
	CS_SERIALIZE    = 8,
	CS_ORDERED      = CS_WAIT_COMPLETE + CS_SERIALIZE,
	CS_NO_CSTATE_CHG = 16, /* Do not display changes in cstate. Internal to drbd_state.c */
	CS_LOCAL_ONLY = 32, /* Do not consider a device pair wide state change */
};

extern enum drbd_state_rv drbd_change_state(struct drbd_conf *mdev,
					    enum chg_state_flags f,
					    union drbd_state mask,
					    union drbd_state val);
extern void drbd_force_state(struct drbd_conf *, union drbd_state,
			union drbd_state);
extern enum drbd_state_rv _drbd_request_state(struct drbd_conf *,
					      union drbd_state,
					      union drbd_state,
					      enum chg_state_flags);
extern enum drbd_state_rv __drbd_set_state(struct drbd_conf *, union drbd_state,
					   enum chg_state_flags,
					   struct completion *done);
extern void print_st_err(struct drbd_conf *, union drbd_state,
			union drbd_state, int);

enum drbd_state_rv
_conn_request_state(struct drbd_tconn *tconn, union drbd_state mask, union drbd_state val,
		    enum chg_state_flags flags);

enum drbd_state_rv
conn_request_state(struct drbd_tconn *tconn, union drbd_state mask, union drbd_state val,
		   enum chg_state_flags flags);

extern void drbd_resume_al(struct drbd_conf *mdev);
extern bool conn_all_vols_unconf(struct drbd_tconn *tconn);

/**
 * drbd_request_state() - Reqest a state change
 * @mdev:	DRBD device.
 * @mask:	mask of state bits to change.
 * @val:	value of new state bits.
 *
 * This is the most graceful way of requesting a state change. It is verbose
 * quite verbose in case the state change is not possible, and all those
 * state changes are globally serialized.
 */
static inline int drbd_request_state(struct drbd_conf *mdev,
				     union drbd_state mask,
				     union drbd_state val)
{
	return _drbd_request_state(mdev, mask, val, CS_VERBOSE + CS_ORDERED);
}

enum drbd_role conn_highest_role(struct drbd_tconn *tconn);
enum drbd_role conn_highest_peer(struct drbd_tconn *tconn);
enum drbd_disk_state conn_highest_disk(struct drbd_tconn *tconn);
enum drbd_disk_state conn_highest_pdsk(struct drbd_tconn *tconn);

#endif
