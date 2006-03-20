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


/*
 * enum dmx_success: Success codes for the Demux Callback API.
 */

enum dmx_success {
  DMX_OK = 0, /* Received Ok */
  DMX_LENGTH_ERROR, /* Incorrect length */
  DMX_OVERRUN_ERROR, /* Receiver ring buffer overrun */
  DMX_CRC_ERROR, /* Incorrect CRC */
  DMX_FRAME_ERROR, /* Frame alignment error */
  DMX_FIFO_ERROR, /* Receiver FIFO overrun */
  DMX_MISSED_ERROR /* Receiver missed packet */
} ;

/*--------------------------------------------------------------------------*/
/* TS packet reception */
/*--------------------------------------------------------------------------*/

/* TS filter type for set() */

#define TS_PACKET       1   /* send TS packets (188 bytes) to callback (default) */
#define	TS_PAYLOAD_ONLY 2   /* in case TS_PACKET is set, only send the TS
			       payload (<=184 bytes per packet) to callback */
#define TS_DECODER      4   /* send stream to built-in decoder (if present) */

/* PES type for filters which write to built-in decoder */
/* these should be kept identical to the types in dmx.h */

enum dmx_ts_pes
{  /* also send packets to decoder (if it exists) */
	DMX_TS_PES_AUDIO0,
	DMX_TS_PES_VIDEO0,
	DMX_TS_PES_TELETEXT0,
	DMX_TS_PES_SUBTITLE0,
	DMX_TS_PES_PCR0,

	DMX_TS_PES_AUDIO1,
	DMX_TS_PES_VIDEO1,
	DMX_TS_PES_TELETEXT1,
	DMX_TS_PES_SUBTITLE1,
	DMX_TS_PES_PCR1,

	DMX_TS_PES_AUDIO2,
	DMX_TS_PES_VIDEO2,
	DMX_TS_PES_TELETEXT2,
	DMX_TS_PES_SUBTITLE2,
	DMX_TS_PES_PCR2,

	DMX_TS_PES_AUDIO3,
	DMX_TS_PES_VIDEO3,
	DMX_TS_PES_TELETEXT3,
	DMX_TS_PES_SUBTITLE3,
	DMX_TS_PES_PCR3,

	DMX_TS_PES_OTHER
};

#define DMX_TS_PES_AUDIO    DMX_TS_PES_AUDIO0
#define DMX_TS_PES_VIDEO    DMX_TS_PES_VIDEO0
#define DMX_TS_PES_TELETEXT DMX_TS_PES_TELETEXT0
#define DMX_TS_PES_SUBTITLE DMX_TS_PES_SUBTITLE0
#define DMX_TS_PES_PCR      DMX_TS_PES_PCR0


struct dmx_ts_feed {
	int is_filtering; /* Set to non-zero when filtering in progress */
	struct dmx_demux *parent; /* Back-pointer */
	void *priv; /* Pointer to private data of the API client */
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

struct dmx_section_filter {
	u8 filter_value [DMX_MAX_FILTER_SIZE];
	u8 filter_mask [DMX_MAX_FILTER_SIZE];
	u8 filter_mode [DMX_MAX_FILTER_SIZE];
	struct dmx_section_feed* parent; /* Back-pointer */
	void* priv; /* Pointer to private data of the API client */
};

struct dmx_section_feed {
	int is_filtering; /* Set to non-zero when filtering in progress */
	struct dmx_demux* parent; /* Back-pointer */
	void* priv; /* Pointer to private data of the API client */

	int check_crc;
	u32 crc_val;

	u8 *secbuf;
	u8 secbuf_base[DMX_MAX_SECFEED_SIZE];
	u16 secbufp, seclen, tsfeedp;

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
			   struct dmx_ts_feed* source,
			   enum dmx_success success);

typedef int (*dmx_section_cb) (	const u8 * buffer1,
				size_t buffer1_len,
				const u8 * buffer2,
				size_t buffer2_len,
				struct dmx_section_filter * source,
				enum dmx_success success);

/*--------------------------------------------------------------------------*/
/* DVB Front-End */
/*--------------------------------------------------------------------------*/

enum dmx_frontend_source {
	DMX_MEMORY_FE,
	DMX_FRONTEND_0,
	DMX_FRONTEND_1,
	DMX_FRONTEND_2,
	DMX_FRONTEND_3,
	DMX_STREAM_0,    /* external stream input, e.g. LVDS */
	DMX_STREAM_1,
	DMX_STREAM_2,
	DMX_STREAM_3
};

struct dmx_frontend {
	struct list_head connectivity_list; /* List of front-ends that can
					       be connected to a particular
					       demux */
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

struct dmx_demux {
	u32 capabilities;            /* Bitfield of capability flags */
	struct dmx_frontend* frontend;    /* Front-end connected to the demux */
	void* priv;                  /* Pointer to private data of the API client */
	int (*open) (struct dmx_demux* demux);
	int (*close) (struct dmx_demux* demux);
	int (*write) (struct dmx_demux* demux, const char* buf, size_t count);
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

	int (*get_caps) (struct dmx_demux* demux, struct dmx_caps *caps);

	int (*set_source) (struct dmx_demux* demux, const dmx_source_t *src);

	int (*get_stc) (struct dmx_demux* demux, unsigned int num,
			u64 *stc, unsigned int *base);
};

#endif /* #ifndef __DEMUX_H */
