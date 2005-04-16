/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: ib_sa.h 1389 2004-12-27 22:56:47Z roland $
 */

#ifndef IB_SA_H
#define IB_SA_H

#include <linux/compiler.h>

#include <ib_verbs.h>
#include <ib_mad.h>

enum {
	IB_SA_CLASS_VERSION	= 2,	/* IB spec version 1.1/1.2 */

	IB_SA_METHOD_DELETE	= 0x15
};

enum ib_sa_selector {
	IB_SA_GTE  = 0,
	IB_SA_LTE  = 1,
	IB_SA_EQ   = 2,
	/*
	 * The meaning of "best" depends on the attribute: for
	 * example, for MTU best will return the largest available
	 * MTU, while for packet life time, best will return the
	 * smallest available life time.
	 */
	IB_SA_BEST = 3
};

enum ib_sa_rate {
	IB_SA_RATE_2_5_GBPS = 2,
	IB_SA_RATE_5_GBPS   = 5,
	IB_SA_RATE_10_GBPS  = 3,
	IB_SA_RATE_20_GBPS  = 6,
	IB_SA_RATE_30_GBPS  = 4,
	IB_SA_RATE_40_GBPS  = 7,
	IB_SA_RATE_60_GBPS  = 8,
	IB_SA_RATE_80_GBPS  = 9,
	IB_SA_RATE_120_GBPS = 10
};

static inline int ib_sa_rate_enum_to_int(enum ib_sa_rate rate)
{
	switch (rate) {
	case IB_SA_RATE_2_5_GBPS: return  1;
	case IB_SA_RATE_5_GBPS:   return  2;
	case IB_SA_RATE_10_GBPS:  return  4;
	case IB_SA_RATE_20_GBPS:  return  8;
	case IB_SA_RATE_30_GBPS:  return 12;
	case IB_SA_RATE_40_GBPS:  return 16;
	case IB_SA_RATE_60_GBPS:  return 24;
	case IB_SA_RATE_80_GBPS:  return 32;
	case IB_SA_RATE_120_GBPS: return 48;
	default: 	          return -1;
	}
}

typedef u64 __bitwise ib_sa_comp_mask;

#define IB_SA_COMP_MASK(n)	((__force ib_sa_comp_mask) cpu_to_be64(1ull << n))

/*
 * Structures for SA records are named "struct ib_sa_xxx_rec."  No
 * attempt is made to pack structures to match the physical layout of
 * SA records in SA MADs; all packing and unpacking is handled by the
 * SA query code.
 *
 * For a record with structure ib_sa_xxx_rec, the naming convention
 * for the component mask value for field yyy is IB_SA_XXX_REC_YYY (we
 * never use different abbreviations or otherwise change the spelling
 * of xxx/yyy between ib_sa_xxx_rec.yyy and IB_SA_XXX_REC_YYY).
 *
 * Reserved rows are indicated with comments to help maintainability.
 */

/* reserved:								 0 */
/* reserved:								 1 */
#define IB_SA_PATH_REC_DGID				IB_SA_COMP_MASK( 2)
#define IB_SA_PATH_REC_SGID				IB_SA_COMP_MASK( 3)
#define IB_SA_PATH_REC_DLID				IB_SA_COMP_MASK( 4)
#define IB_SA_PATH_REC_SLID				IB_SA_COMP_MASK( 5)
#define IB_SA_PATH_REC_RAW_TRAFFIC			IB_SA_COMP_MASK( 6)
/* reserved:								 7 */
#define IB_SA_PATH_REC_FLOW_LABEL       		IB_SA_COMP_MASK( 8)
#define IB_SA_PATH_REC_HOP_LIMIT			IB_SA_COMP_MASK( 9)
#define IB_SA_PATH_REC_TRAFFIC_CLASS			IB_SA_COMP_MASK(10)
#define IB_SA_PATH_REC_REVERSIBLE			IB_SA_COMP_MASK(11)
#define IB_SA_PATH_REC_NUMB_PATH			IB_SA_COMP_MASK(12)
#define IB_SA_PATH_REC_PKEY				IB_SA_COMP_MASK(13)
/* reserved:								14 */
#define IB_SA_PATH_REC_SL				IB_SA_COMP_MASK(15)
#define IB_SA_PATH_REC_MTU_SELECTOR			IB_SA_COMP_MASK(16)
#define IB_SA_PATH_REC_MTU				IB_SA_COMP_MASK(17)
#define IB_SA_PATH_REC_RATE_SELECTOR			IB_SA_COMP_MASK(18)
#define IB_SA_PATH_REC_RATE				IB_SA_COMP_MASK(19)
#define IB_SA_PATH_REC_PACKET_LIFE_TIME_SELECTOR	IB_SA_COMP_MASK(20)
#define IB_SA_PATH_REC_PACKET_LIFE_TIME			IB_SA_COMP_MASK(21)
#define IB_SA_PATH_REC_PREFERENCE			IB_SA_COMP_MASK(22)

struct ib_sa_path_rec {
	/* reserved */
	/* reserved */
	union ib_gid dgid;
	union ib_gid sgid;
	u16          dlid;
	u16          slid;
	int          raw_traffic;
	/* reserved */
	u32          flow_label;
	u8           hop_limit;
	u8           traffic_class;
	int          reversible;
	u8           numb_path;
	u16          pkey;
	/* reserved */
	u8           sl;
	u8           mtu_selector;
	enum ib_mtu  mtu;
	u8           rate_selector;
	u8           rate;
	u8           packet_life_time_selector;
	u8           packet_life_time;
	u8           preference;
};

#define IB_SA_MCMEMBER_REC_MGID				IB_SA_COMP_MASK( 0)
#define IB_SA_MCMEMBER_REC_PORT_GID			IB_SA_COMP_MASK( 1)
#define IB_SA_MCMEMBER_REC_QKEY				IB_SA_COMP_MASK( 2)
#define IB_SA_MCMEMBER_REC_MLID				IB_SA_COMP_MASK( 3)
#define IB_SA_MCMEMBER_REC_MTU_SELECTOR			IB_SA_COMP_MASK( 4)
#define IB_SA_MCMEMBER_REC_MTU				IB_SA_COMP_MASK( 5)
#define IB_SA_MCMEMBER_REC_TRAFFIC_CLASS		IB_SA_COMP_MASK( 6)
#define IB_SA_MCMEMBER_REC_PKEY				IB_SA_COMP_MASK( 7)
#define IB_SA_MCMEMBER_REC_RATE_SELECTOR		IB_SA_COMP_MASK( 8)
#define IB_SA_MCMEMBER_REC_RATE				IB_SA_COMP_MASK( 9)
#define IB_SA_MCMEMBER_REC_PACKET_LIFE_TIME_SELECTOR	IB_SA_COMP_MASK(10)
#define IB_SA_MCMEMBER_REC_PACKET_LIFE_TIME		IB_SA_COMP_MASK(11)
#define IB_SA_MCMEMBER_REC_SL				IB_SA_COMP_MASK(12)
#define IB_SA_MCMEMBER_REC_FLOW_LABEL			IB_SA_COMP_MASK(13)
#define IB_SA_MCMEMBER_REC_HOP_LIMIT			IB_SA_COMP_MASK(14)
#define IB_SA_MCMEMBER_REC_SCOPE			IB_SA_COMP_MASK(15)
#define IB_SA_MCMEMBER_REC_JOIN_STATE			IB_SA_COMP_MASK(16)
#define IB_SA_MCMEMBER_REC_PROXY_JOIN			IB_SA_COMP_MASK(17)

struct ib_sa_mcmember_rec {
	union ib_gid mgid;
	union ib_gid port_gid;
	u32          qkey;
	u16          mlid;
	u8           mtu_selector;
	enum         ib_mtu mtu;
	u8           traffic_class;
	u16          pkey;
	u8 	     rate_selector;
	u8 	     rate;
	u8 	     packet_life_time_selector;
	u8 	     packet_life_time;
	u8           sl;
	u32          flow_label;
	u8           hop_limit;
	u8           scope;
	u8           join_state;
	int          proxy_join;
};

struct ib_sa_query;

void ib_sa_cancel_query(int id, struct ib_sa_query *query);

int ib_sa_path_rec_get(struct ib_device *device, u8 port_num,
		       struct ib_sa_path_rec *rec,
		       ib_sa_comp_mask comp_mask,
		       int timeout_ms, int gfp_mask,
		       void (*callback)(int status,
					struct ib_sa_path_rec *resp,
					void *context),
		       void *context,
		       struct ib_sa_query **query);

int ib_sa_mcmember_rec_query(struct ib_device *device, u8 port_num,
			     u8 method,
			     struct ib_sa_mcmember_rec *rec,
			     ib_sa_comp_mask comp_mask,
			     int timeout_ms, int gfp_mask,
			     void (*callback)(int status,
					      struct ib_sa_mcmember_rec *resp,
					      void *context),
			     void *context,
			     struct ib_sa_query **query);

/**
 * ib_sa_mcmember_rec_set - Start an MCMember set query
 * @device:device to send query on
 * @port_num: port number to send query on
 * @rec:MCMember Record to send in query
 * @comp_mask:component mask to send in query
 * @timeout_ms:time to wait for response
 * @gfp_mask:GFP mask to use for internal allocations
 * @callback:function called when query completes, times out or is
 * canceled
 * @context:opaque user context passed to callback
 * @sa_query:query context, used to cancel query
 *
 * Send an MCMember Set query to the SA (eg to join a multicast
 * group).  The callback function will be called when the query
 * completes (or fails); status is 0 for a successful response, -EINTR
 * if the query is canceled, -ETIMEDOUT is the query timed out, or
 * -EIO if an error occurred sending the query.  The resp parameter of
 * the callback is only valid if status is 0.
 *
 * If the return value of ib_sa_mcmember_rec_set() is negative, it is
 * an error code.  Otherwise it is a query ID that can be used to
 * cancel the query.
 */
static inline int
ib_sa_mcmember_rec_set(struct ib_device *device, u8 port_num,
		       struct ib_sa_mcmember_rec *rec,
		       ib_sa_comp_mask comp_mask,
		       int timeout_ms, int gfp_mask,
		       void (*callback)(int status,
					struct ib_sa_mcmember_rec *resp,
					void *context),
		       void *context,
		       struct ib_sa_query **query)
{
	return ib_sa_mcmember_rec_query(device, port_num,
					IB_MGMT_METHOD_SET,
					rec, comp_mask,
					timeout_ms, gfp_mask, callback,
					context, query);
}

/**
 * ib_sa_mcmember_rec_delete - Start an MCMember delete query
 * @device:device to send query on
 * @port_num: port number to send query on
 * @rec:MCMember Record to send in query
 * @comp_mask:component mask to send in query
 * @timeout_ms:time to wait for response
 * @gfp_mask:GFP mask to use for internal allocations
 * @callback:function called when query completes, times out or is
 * canceled
 * @context:opaque user context passed to callback
 * @sa_query:query context, used to cancel query
 *
 * Send an MCMember Delete query to the SA (eg to leave a multicast
 * group).  The callback function will be called when the query
 * completes (or fails); status is 0 for a successful response, -EINTR
 * if the query is canceled, -ETIMEDOUT is the query timed out, or
 * -EIO if an error occurred sending the query.  The resp parameter of
 * the callback is only valid if status is 0.
 *
 * If the return value of ib_sa_mcmember_rec_delete() is negative, it
 * is an error code.  Otherwise it is a query ID that can be used to
 * cancel the query.
 */
static inline int
ib_sa_mcmember_rec_delete(struct ib_device *device, u8 port_num,
			  struct ib_sa_mcmember_rec *rec,
			  ib_sa_comp_mask comp_mask,
			  int timeout_ms, int gfp_mask,
			  void (*callback)(int status,
					   struct ib_sa_mcmember_rec *resp,
					   void *context),
			  void *context,
			  struct ib_sa_query **query)
{
	return ib_sa_mcmember_rec_query(device, port_num,
					IB_SA_METHOD_DELETE,
					rec, comp_mask,
					timeout_ms, gfp_mask, callback,
					context, query);
}


#endif /* IB_SA_H */
