/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DRBD_STATE_H
#define DRBD_STATE_H

struct drbd_device;
struct drbd_connection;

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
	D, ({ union drbd_state __ns; __ns = drbd_read_state(D); __ns.T = (S); __ns; })
#define _NS2(D, T1, S1, T2, S2) \
	D, ({ union drbd_state __ns; __ns = drbd_read_state(D); __ns.T1 = (S1); \
	__ns.T2 = (S2); __ns; })
#define _NS3(D, T1, S1, T2, S2, T3, S3) \
	D, ({ union drbd_state __ns; __ns = drbd_read_state(D); __ns.T1 = (S1); \
	__ns.T2 = (S2); __ns.T3 = (S3); __ns; })

enum chg_state_flags {
	CS_HARD	         = 1 << 0,
	CS_VERBOSE       = 1 << 1,
	CS_WAIT_COMPLETE = 1 << 2,
	CS_SERIALIZE     = 1 << 3,
	CS_ORDERED       = CS_WAIT_COMPLETE + CS_SERIALIZE,
	CS_LOCAL_ONLY    = 1 << 4, /* Do not consider a device pair wide state change */
	CS_DC_ROLE       = 1 << 5, /* DC = display as connection state change */
	CS_DC_PEER       = 1 << 6,
	CS_DC_CONN       = 1 << 7,
	CS_DC_DISK       = 1 << 8,
	CS_DC_PDSK       = 1 << 9,
	CS_DC_SUSP       = 1 << 10,
	CS_DC_MASK       = CS_DC_ROLE + CS_DC_PEER + CS_DC_CONN + CS_DC_DISK + CS_DC_PDSK,
	CS_IGN_OUTD_FAIL = 1 << 11,

	/* Make sure no meta data IO is in flight, by calling
	 * drbd_md_get_buffer().  Used for graceful detach. */
	CS_INHIBIT_MD_IO = 1 << 12,
};

/* drbd_dev_state and drbd_state are different types. This is to stress the
   small difference. There is no suspended flag (.susp), and no suspended
   while fence handler runs flas (susp_fen). */
union drbd_dev_state {
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned role:2 ;   /* 3/4	 primary/secondary/unknown */
		unsigned peer:2 ;   /* 3/4	 primary/secondary/unknown */
		unsigned conn:5 ;   /* 17/32	 cstates */
		unsigned disk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned pdsk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned _unused:1 ;
		unsigned aftr_isp:1 ; /* isp .. imposed sync pause */
		unsigned peer_isp:1 ;
		unsigned user_isp:1 ;
		unsigned _pad:11;   /* 0	 unused */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned _pad:11;
		unsigned user_isp:1 ;
		unsigned peer_isp:1 ;
		unsigned aftr_isp:1 ; /* isp .. imposed sync pause */
		unsigned _unused:1 ;
		unsigned pdsk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned disk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned conn:5 ;   /* 17/32	 cstates */
		unsigned peer:2 ;   /* 3/4	 primary/secondary/unknown */
		unsigned role:2 ;   /* 3/4	 primary/secondary/unknown */
#else
# error "this endianess is not supported"
#endif
	};
	unsigned int i;
};

extern enum drbd_state_rv drbd_change_state(struct drbd_device *device,
					    enum chg_state_flags f,
					    union drbd_state mask,
					    union drbd_state val);
extern void drbd_force_state(struct drbd_device *, union drbd_state,
			union drbd_state);
extern enum drbd_state_rv _drbd_request_state(struct drbd_device *,
					      union drbd_state,
					      union drbd_state,
					      enum chg_state_flags);

extern enum drbd_state_rv
_drbd_request_state_holding_state_mutex(struct drbd_device *, union drbd_state,
					union drbd_state, enum chg_state_flags);

extern enum drbd_state_rv _drbd_set_state(struct drbd_device *, union drbd_state,
					  enum chg_state_flags,
					  struct completion *done);
extern void print_st_err(struct drbd_device *, union drbd_state,
			union drbd_state, enum drbd_state_rv);

enum drbd_state_rv
_conn_request_state(struct drbd_connection *connection, union drbd_state mask, union drbd_state val,
		    enum chg_state_flags flags);

enum drbd_state_rv
conn_request_state(struct drbd_connection *connection, union drbd_state mask, union drbd_state val,
		   enum chg_state_flags flags);

extern void drbd_resume_al(struct drbd_device *device);
extern bool conn_all_vols_unconf(struct drbd_connection *connection);

/**
 * drbd_request_state() - Request a state change
 * @device:	DRBD device.
 * @mask:	mask of state bits to change.
 * @val:	value of new state bits.
 *
 * This is the most graceful way of requesting a state change. It is verbose
 * quite verbose in case the state change is not possible, and all those
 * state changes are globally serialized.
 */
static inline int drbd_request_state(struct drbd_device *device,
				     union drbd_state mask,
				     union drbd_state val)
{
	return _drbd_request_state(device, mask, val, CS_VERBOSE + CS_ORDERED);
}

/* for use in adm_detach() (drbd_adm_detach(), drbd_adm_down()) */
int drbd_request_detach_interruptible(struct drbd_device *device);

enum drbd_role conn_highest_role(struct drbd_connection *connection);
enum drbd_role conn_highest_peer(struct drbd_connection *connection);
enum drbd_disk_state conn_highest_disk(struct drbd_connection *connection);
enum drbd_disk_state conn_lowest_disk(struct drbd_connection *connection);
enum drbd_disk_state conn_highest_pdsk(struct drbd_connection *connection);
enum drbd_conns conn_lowest_conn(struct drbd_connection *connection);

#endif
