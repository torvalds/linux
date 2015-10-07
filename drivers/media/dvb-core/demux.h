/*
 * demux.h
 *
 * Copyright (c) 2002 Convergence GmbH
 *
 * based on code:
 * Copyright (c) 2000 Nokia Research Center
 *                    Tampere, FINLAND
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef __DEMUX_H
#define __DEMUX_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/time.h>
#include <linux/dvb/dmx.h>

/*--------------------------------------------------------------------------*/
/* Common definitions */
/*--------------------------------------------------------------------------*/

/*
 * DMX_MAX_FILTER_SIZE: Maximum length (in bytes) of a section/PES filter.
 */

#ifndef DMX_MAX_FILTER_SIZE
#define DMX_MAX_FILTER_SIZE 18
#endif

/*
 * DMX_MAX_SECFEED_SIZE: Maximum length (in bytes) of a private section feed filter.
 */

#ifndef DMX_MAX_SECTION_SIZE
#define DMX_MAX_SECTION_SIZE 4096
#endif
#ifndef DMX_MAX_SECFEED_SIZE
#define DMX_MAX_SECFEED_SIZE (DMX_MAX_SECTION_SIZE + 188)
#endif

/*--------------------------------------------------------------------------*/
/* TS packet reception */
/*--------------------------------------------------------------------------*/

/* TS filter type for set() */

#define TS_PACKET       1   /* send TS packets (188 bytes) to callback (default) */
#define	TS_PAYLOAD_ONLY 2   /* in case TS_PACKET is set, only send the TS
			       payload (<=184 bytes per packet) to callback */
#define TS_DECODER      4   /* send stream to built-in decoder (if present) */
#define TS_DEMUX        8   /* in case TS_PACKET is set, send the TS to
			       the demux device, not to the dvr device */

/**
 * struct dmx_ts_feed - Structure that contains a TS feed filter
 *
 * @is_filtering:	Set to non-zero when filtering in progress
 * @parent:		pointer to struct dmx_demux
 * @priv:		pointer to private data of the API client
 * @set:		sets the TS filter
 * @start_filtering:	starts TS filtering
 * @stop_filtering:	stops TS filtering
 *
 * A TS feed is typically mapped to a hardware PID filter on the demux chip.
 * Using this API, the client can set the filtering properties to start/stop
 * filtering TS packets on a particular TS feed.
 */
struct dmx_ts_feed {
	int is_filtering;
	struct dmx_demux *parent;
	void *priv;
	int (*set) (struct dmx_ts_feed *feed,
		    u16 pid,
		    int type,
		    enum dmx_ts_pes pes_type,
		    size_t circular_buffer_size,
		    struct timespec timeout);
	int (*start_filtering) (struct dmx_ts_feed* feed);
	int (*stop_filtering) (struct dmx_ts_feed* feed);
};

/*--------------------------------------------------------------------------*/
/* Section reception */
/*--------------------------------------------------------------------------*/

/**
 * struct dmx_section_filter - Structure that describes a section filter
 *
 * @filter_value: Contains up to 16 bytes (128 bits) of the TS section header
 *		  that will be matched by the section filter
 * @filter_mask:  Contains a 16 bytes (128 bits) filter mask with the bits
 *		  specified by @filter_value that will be used on the filter
 *		  match logic.
 * @filter_mode:  Contains a 16 bytes (128 bits) filter mode.
 * @parent:	  Pointer to struct dmx_section_feed.
 * @priv:	  Pointer to private data of the API client.
 *
 *
 * The @filter_mask controls which bits of @filter_value are compared with
 * the section headers/payload. On a binary value of 1 in filter_mask, the
 * corresponding bits are compared. The filter only accepts sections that are
 * equal to filter_value in all the tested bit positions.
 */
struct dmx_section_filter {
	u8 filter_value [DMX_MAX_FILTER_SIZE];
	u8 filter_mask [DMX_MAX_FILTER_SIZE];
	u8 filter_mode [DMX_MAX_FILTER_SIZE];
	struct dmx_section_feed* parent; /* Back-pointer */
	void* priv; /* Pointer to private data of the API client */
};

/**
 * struct dmx_section_feed - Structure that contains a section feed filter
 *
 * @is_filtering:	Set to non-zero when filtering in progress
 * @parent:		pointer to struct dmx_demux
 * @priv:		pointer to private data of the API client
 * @check_crc:		If non-zero, check the CRC values of filtered sections.
 * @set:		sets the section filter
 * @allocate_filter:	This function is used to allocate a section filter on
 *			the demux. It should only be called when no filtering
 *			is in progress on this section feed. If a filter cannot
 *			be allocated, the function fails with -ENOSPC.
 * @release_filter:	This function releases all the resources of a
 * 			previously allocated section filter. The function
 *			should not be called while filtering is in progress
 *			on this section feed. After calling this function,
 *			the caller should not try to dereference the filter
 *			pointer.
 * @start_filtering:	starts section filtering
 * @stop_filtering:	stops section filtering
 *
 * A TS feed is typically mapped to a hardware PID filter on the demux chip.
 * Using this API, the client can set the filtering properties to start/stop
 * filtering TS packets on a particular TS feed.
 */
struct dmx_section_feed {
	int is_filtering;
	struct dmx_demux* parent;
	void* priv;

	int check_crc;

	/* private: Used internally at dvb_demux.c */
	u32 crc_val;

	u8 *secbuf;
	u8 secbuf_base[DMX_MAX_SECFEED_SIZE];
	u16 secbufp, seclen, tsfeedp;

	/* public: */
	int (*set) (struct dmx_section_feed* feed,
		    u16 pid,
		    size_t circular_buffer_size,
		    int check_crc);
	int (*allocate_filter) (struct dmx_section_feed* feed,
				struct dmx_section_filter** filter);
	int (*release_filter) (struct dmx_section_feed* feed,
			       struct dmx_section_filter* filter);
	int (*start_filtering) (struct dmx_section_feed* feed);
	int (*stop_filtering) (struct dmx_section_feed* feed);
};

/*--------------------------------------------------------------------------*/
/* Callback functions */
/*--------------------------------------------------------------------------*/

typedef int (*dmx_ts_cb) ( const u8 * buffer1,
			   size_t buffer1_length,
			   const u8 * buffer2,
			   size_t buffer2_length,
			   struct dmx_ts_feed* source);

typedef int (*dmx_section_cb) (	const u8 * buffer1,
				size_t buffer1_len,
				const u8 * buffer2,
				size_t buffer2_len,
				struct dmx_section_filter * source);

/*--------------------------------------------------------------------------*/
/* DVB Front-End */
/*--------------------------------------------------------------------------*/

/**
 * enum dmx_frontend_source - Used to identify the type of frontend
 *
 * @DMX_MEMORY_FE:	The source of the demux is memory. It means that
 *			the MPEG-TS to be filtered comes from userspace,
 *			via write() syscall.
 *
 * @DMX_FRONTEND_0:	The source of the demux is a frontend connected
 *			to the demux.
 */
enum dmx_frontend_source {
	DMX_MEMORY_FE,
	DMX_FRONTEND_0,
};

/**
 * struct dmx_frontend - Structure that lists the frontends associated with
 *			 a demux
 *
 * @connectivity_list:	List of front-ends that can be connected to a
 *			particular demux;
 * @source:		Type of the frontend.
 *
 * FIXME: this structure should likely be replaced soon by some
 *	media-controller based logic.
 */
struct dmx_frontend {
	struct list_head connectivity_list;
	enum dmx_frontend_source source;
};

/*--------------------------------------------------------------------------*/
/* MPEG-2 TS Demux */
/*--------------------------------------------------------------------------*/

/*
 * Flags OR'ed in the capabilities field of struct dmx_demux.
 */

#define DMX_TS_FILTERING                        1
#define DMX_PES_FILTERING                       2
#define DMX_SECTION_FILTERING                   4
#define DMX_MEMORY_BASED_FILTERING              8    /* write() available */
#define DMX_CRC_CHECKING                        16
#define DMX_TS_DESCRAMBLING                     32

/*
 * Demux resource type identifier.
*/

/*
 * DMX_FE_ENTRY(): Casts elements in the list of registered
 * front-ends from the generic type struct list_head
 * to the type * struct dmx_frontend
 *.
*/

#define DMX_FE_ENTRY(list) list_entry(list, struct dmx_frontend, connectivity_list)

/**
 * struct dmx_demux - Structure that contains the demux capabilities and
 *		      callbacks.
 *
 * @capabilities: Bitfield of capability flags
 *
 * @frontend: Front-end connected to the demux
 *
 * @priv: Pointer to private data of the API client
 *
 * @open: This function reserves the demux for use by the caller and, if
 *	necessary, initializes the demux. When the demux is no longer needed,
 * 	the function @close should be called. It should be possible for
 *	multiple clients to access the demux at the same time. Thus, the
 *	function implementation should increment the demux usage count when
 *	@open is called and decrement it when @close is called.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	It returns
 *		0 on success;
 * 		-EUSERS, if maximum usage count was reached;
 *		-EINVAL, on bad parameter.
 *
 * @close: This function reserves the demux for use by the caller and, if
 *	necessary, initializes the demux. When the demux is no longer needed,
 *	the function @close should be called. It should be possible for
 *	multiple clients to access the demux at the same time. Thus, the
 *	function implementation should increment the demux usage count when
 *	@open is called and decrement it when @close is called.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	It returns
 *		0 on success;
 *		-ENODEV, if demux was not in use (e. g. no users);
 *		-EINVAL, on bad parameter.
 *
 * @write: This function provides the demux driver with a memory buffer
 *	containing TS packets. Instead of receiving TS packets from the DVB
 *	front-end, the demux driver software will read packets from memory.
 *	Any clients of this demux with active TS, PES or Section filters will
 *	receive filtered data via the Demux callback API (see 0). The function
 *	returns when all the data in the buffer has been consumed by the demux.
 *	Demux hardware typically cannot read TS from memory. If this is the
 *	case, memory-based filtering has to be implemented entirely in software.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	The @buf function parameter contains a pointer to the TS data in
 *	kernel-space memory.
 *	The @count function parameter contains the length of the TS data.
 *	It returns
 *		0 on success;
 *		-ERESTARTSYS, if mutex lock was interrupted;
 *		-EINTR, if a signal handling is pending;
 *		-ENODEV, if demux was removed;
 *		-EINVAL, on bad parameter.
 *
 * @allocate_ts_feed: Allocates a new TS feed, which is used to filter the TS
 *	packets carrying a certain PID. The TS feed normally corresponds to a
 *	hardware PID filter on the demux chip.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	The @feed function parameter contains a pointer to the TS feed API and
 *	instance data.
 *	The @callback function parameter contains a pointer to the callback
 *	function for passing received TS packet.
 *	It returns
 *		0 on success;
 *		-ERESTARTSYS, if mutex lock was interrupted;
 *		-EBUSY, if no more TS feeds is available;
 *		-EINVAL, on bad parameter.
 *
 * @release_ts_feed: Releases the resources allocated with @allocate_ts_feed.
 *	Any filtering in progress on the TS feed should be stopped before
 *	calling this function.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	The @feed function parameter contains a pointer to the TS feed API and
 *	instance data.
 *	It returns
 *		0 on success;
 *		-EINVAL on bad parameter.
 *
 * @allocate_section_feed: Allocates a new section feed, i.e. a demux resource
 *	for filtering and receiving sections. On platforms with hardware
 *	support for section filtering, a section feed is directly mapped to
 *	the demux HW. On other platforms, TS packets are first PID filtered in
 *	hardware and a hardware section filter then emulated in software. The
 *	caller obtains an API pointer of type dmx_section_feed_t as an out
 *	parameter. Using this API the caller can set filtering parameters and
 *	start receiving sections.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	The @feed function parameter contains a pointer to the TS feed API and
 *	instance data.
 *	The @callback function parameter contains a pointer to the callback
 *	function for passing received TS packet.
 *	It returns
 *		0 on success;
 *		-EBUSY, if no more TS feeds is available;
 *		-EINVAL, on bad parameter.
 *
 * @release_section_feed: Releases the resources allocated with
 *	@allocate_section_feed, including allocated filters. Any filtering in
 *	progress on the section feed should be stopped before calling this
 *	function.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	The @feed function parameter contains a pointer to the TS feed API and
 *	instance data.
 *	It returns
 *		0 on success;
 *		-EINVAL, on bad parameter.
 *
 * @add_frontend: Registers a connectivity between a demux and a front-end,
 *	i.e., indicates that the demux can be connected via a call to
 *	@connect_frontend to use the given front-end as a TS source. The
 *	client of this function has to allocate dynamic or static memory for
 *	the frontend structure and initialize its fields before calling this
 *	function. This function is normally called during the driver
 *	initialization. The caller must not free the memory of the frontend
 *	struct before successfully calling @remove_frontend.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	The @frontend function parameter contains a pointer to the front-end
 *	instance data.
 *	It returns
 *		0 on success;
 *		-EINVAL, on bad parameter.
 *
 * @remove_frontend: Indicates that the given front-end, registered by a call
 *	to @add_frontend, can no longer be connected as a TS source by this
 *	demux. The function should be called when a front-end driver or a demux
 *	driver is removed from the system. If the front-end is in use, the
 *	function fails with the return value of -EBUSY. After successfully
 *	calling this function, the caller can free the memory of the frontend
 *	struct if it was dynamically allocated before the @add_frontend
 *	operation.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	The @frontend function parameter contains a pointer to the front-end
 *	instance data.
 *	It returns
 *		0 on success;
 *		-ENODEV, if the front-end was not found,
 *		-EINVAL, on bad parameter.
 *
 * @get_frontends: Provides the APIs of the front-ends that have been
 *	registered for this demux. Any of the front-ends obtained with this
 *	call can be used as a parameter for @connect_frontend. The include
 *	file demux.h contains the macro DMX_FE_ENTRY() for converting an
 *	element of the generic type struct &list_head * to the type
 * 	struct &dmx_frontend *. The caller must not free the memory of any of
 *	the elements obtained via this function call.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	It returns a struct list_head pointer to the list of front-end
 *	interfaces, or NULL in the case of an empty list.
 *
 * @connect_frontend: Connects the TS output of the front-end to the input of
 *	the demux. A demux can only be connected to a front-end registered to
 *	the demux with the function @add_frontend. It may or may not be
 *	possible to connect multiple demuxes to the same front-end, depending
 *	on the capabilities of the HW platform. When not used, the front-end
 *	should be released by calling @disconnect_frontend.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	The @frontend function parameter contains a pointer to the front-end
 *	instance data.
 *	It returns
 *		0 on success;
 *		-EINVAL, on bad parameter.
 *
 * @disconnect_frontend: Disconnects the demux and a front-end previously
 *	connected by a @connect_frontend call.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	It returns
 *		0 on success;
 *		-EINVAL on bad parameter.
 *
 * @get_pes_pids: Get the PIDs for DMX_PES_AUDIO0, DMX_PES_VIDEO0,
 *	DMX_PES_TELETEXT0, DMX_PES_SUBTITLE0 and DMX_PES_PCR0.
 *	The @demux function parameter contains a pointer to the demux API and
 *	instance data.
 *	The @pids function parameter contains an array with five u16 elements
 *	where the PIDs will be stored.
 *	It returns
 *		0 on success;
 *		-EINVAL on bad parameter.
 */

struct dmx_demux {
	u32 capabilities;
	struct dmx_frontend* frontend;
	void* priv;
	int (*open) (struct dmx_demux* demux);
	int (*close) (struct dmx_demux* demux);
	int (*write) (struct dmx_demux* demux, const char __user *buf, size_t count);
	int (*allocate_ts_feed) (struct dmx_demux* demux,
				 struct dmx_ts_feed** feed,
				 dmx_ts_cb callback);
	int (*release_ts_feed) (struct dmx_demux* demux,
				struct dmx_ts_feed* feed);
	int (*allocate_section_feed) (struct dmx_demux* demux,
				      struct dmx_section_feed** feed,
				      dmx_section_cb callback);
	int (*release_section_feed) (struct dmx_demux* demux,
				     struct dmx_section_feed* feed);
	int (*add_frontend) (struct dmx_demux* demux,
			     struct dmx_frontend* frontend);
	int (*remove_frontend) (struct dmx_demux* demux,
				struct dmx_frontend* frontend);
	struct list_head* (*get_frontends) (struct dmx_demux* demux);
	int (*connect_frontend) (struct dmx_demux* demux,
				 struct dmx_frontend* frontend);
	int (*disconnect_frontend) (struct dmx_demux* demux);

	int (*get_pes_pids) (struct dmx_demux* demux, u16 *pids);

	/* private: Not used upstream and never documented */
#if 0
	int (*get_caps) (struct dmx_demux* demux, struct dmx_caps *caps);
	int (*set_source) (struct dmx_demux* demux, const dmx_source_t *src);
#endif
	/*
	 * private: Only used at av7110, to read some data from firmware.
	 *	As this was never documented, we have no clue about what's
	 * 	there, and its usage on other drivers aren't encouraged.
	 */
	int (*get_stc) (struct dmx_demux* demux, unsigned int num,
			u64 *stc, unsigned int *base);
};

#endif /* #ifndef __DEMUX_H */
