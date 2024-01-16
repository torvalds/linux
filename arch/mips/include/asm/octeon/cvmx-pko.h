/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

/**
 *
 * Interface to the hardware Packet Output unit.
 *
 * Starting with SDK 1.7.0, the PKO output functions now support
 * two types of locking. CVMX_PKO_LOCK_ATOMIC_TAG continues to
 * function similarly to previous SDKs by using POW atomic tags
 * to preserve ordering and exclusivity. As a new option, you
 * can now pass CVMX_PKO_LOCK_CMD_QUEUE which uses a ll/sc
 * memory based locking instead. This locking has the advantage
 * of not affecting the tag state but doesn't preserve packet
 * ordering. CVMX_PKO_LOCK_CMD_QUEUE is appropriate in most
 * generic code while CVMX_PKO_LOCK_CMD_QUEUE should be used
 * with hand tuned fast path code.
 *
 * Some of other SDK differences visible to the command queuing:
 * - PKO indexes are no longer stored in the FAU. A large
 *   percentage of the FAU register block used to be tied up
 *   maintaining PKO queue pointers. These are now stored in a
 *   global named block.
 * - The PKO <b>use_locking</b> parameter can now have a global
 *   effect. Since all application use the same named block,
 *   queue locking correctly applies across all operating
 *   systems when using CVMX_PKO_LOCK_CMD_QUEUE.
 * - PKO 3 word commands are now supported. Use
 *   cvmx_pko_send_packet_finish3().
 *
 */

#ifndef __CVMX_PKO_H__
#define __CVMX_PKO_H__

#include <asm/octeon/cvmx-fpa.h>
#include <asm/octeon/cvmx-pow.h>
#include <asm/octeon/cvmx-cmd-queue.h>
#include <asm/octeon/cvmx-pko-defs.h>

/* Adjust the command buffer size by 1 word so that in the case of using only
 * two word PKO commands no command words stradle buffers.  The useful values
 * for this are 0 and 1. */
#define CVMX_PKO_COMMAND_BUFFER_SIZE_ADJUST (1)

#define CVMX_PKO_MAX_OUTPUT_QUEUES_STATIC 256
#define CVMX_PKO_MAX_OUTPUT_QUEUES	((OCTEON_IS_MODEL(OCTEON_CN31XX) || \
	OCTEON_IS_MODEL(OCTEON_CN3010) || OCTEON_IS_MODEL(OCTEON_CN3005) || \
	OCTEON_IS_MODEL(OCTEON_CN50XX)) ? 32 : \
		(OCTEON_IS_MODEL(OCTEON_CN58XX) || \
		OCTEON_IS_MODEL(OCTEON_CN56XX)) ? 256 : 128)
#define CVMX_PKO_NUM_OUTPUT_PORTS	40
/* use this for queues that are not used */
#define CVMX_PKO_MEM_QUEUE_PTRS_ILLEGAL_PID 63
#define CVMX_PKO_QUEUE_STATIC_PRIORITY	9
#define CVMX_PKO_ILLEGAL_QUEUE	0xFFFF
#define CVMX_PKO_MAX_QUEUE_DEPTH 0

typedef enum {
	CVMX_PKO_SUCCESS,
	CVMX_PKO_INVALID_PORT,
	CVMX_PKO_INVALID_QUEUE,
	CVMX_PKO_INVALID_PRIORITY,
	CVMX_PKO_NO_MEMORY,
	CVMX_PKO_PORT_ALREADY_SETUP,
	CVMX_PKO_CMD_QUEUE_INIT_ERROR
} cvmx_pko_status_t;

/**
 * This enumeration represents the differnet locking modes supported by PKO.
 */
typedef enum {
	/*
	 * PKO doesn't do any locking. It is the responsibility of the
	 * application to make sure that no other core is accessing
	 * the same queue at the same time
	 */
	CVMX_PKO_LOCK_NONE = 0,
	/*
	 * PKO performs an atomic tagswitch to insure exclusive access
	 * to the output queue. This will maintain packet ordering on
	 * output.
	 */
	CVMX_PKO_LOCK_ATOMIC_TAG = 1,
	/*
	 * PKO uses the common command queue locks to insure exclusive
	 * access to the output queue. This is a memory based
	 * ll/sc. This is the most portable locking mechanism.
	 */
	CVMX_PKO_LOCK_CMD_QUEUE = 2,
} cvmx_pko_lock_t;

typedef struct {
	uint32_t packets;
	uint64_t octets;
	uint64_t doorbell;
} cvmx_pko_port_status_t;

/**
 * This structure defines the address to use on a packet enqueue
 */
typedef union {
	uint64_t u64;
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		/* Must CVMX_IO_SEG */
		uint64_t mem_space:2;
		/* Must be zero */
		uint64_t reserved:13;
		/* Must be one */
		uint64_t is_io:1;
		/* The ID of the device on the non-coherent bus */
		uint64_t did:8;
		/* Must be zero */
		uint64_t reserved2:4;
		/* Must be zero */
		uint64_t reserved3:18;
		/*
		 * The hardware likes to have the output port in
		 * addition to the output queue,
		 */
		uint64_t port:6;
		/*
		 * The output queue to send the packet to (0-127 are
		 * legal)
		 */
		uint64_t queue:9;
		/* Must be zero */
		uint64_t reserved4:3;
#else
	        uint64_t reserved4:3;
	        uint64_t queue:9;
	        uint64_t port:9;
	        uint64_t reserved3:15;
	        uint64_t reserved2:4;
	        uint64_t did:8;
	        uint64_t is_io:1;
	        uint64_t reserved:13;
	        uint64_t mem_space:2;
#endif
	} s;
} cvmx_pko_doorbell_address_t;

/**
 * Structure of the first packet output command word.
 */
union cvmx_pko_command_word0 {
	uint64_t u64;
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		/*
		 * The size of the reg1 operation - could be 8, 16,
		 * 32, or 64 bits.
		 */
		uint64_t size1:2;
		/*
		 * The size of the reg0 operation - could be 8, 16,
		 * 32, or 64 bits.
		 */
		uint64_t size0:2;
		/*
		 * If set, subtract 1, if clear, subtract packet
		 * size.
		 */
		uint64_t subone1:1;
		/*
		 * The register, subtract will be done if reg1 is
		 * non-zero.
		 */
		uint64_t reg1:11;
		/* If set, subtract 1, if clear, subtract packet size */
		uint64_t subone0:1;
		/* The register, subtract will be done if reg0 is non-zero */
		uint64_t reg0:11;
		/*
		 * When set, interpret segment pointer and segment
		 * bytes in little endian order.
		 */
		uint64_t le:1;
		/*
		 * When set, packet data not allocated in L2 cache by
		 * PKO.
		 */
		uint64_t n2:1;
		/*
		 * If set and rsp is set, word3 contains a pointer to
		 * a work queue entry.
		 */
		uint64_t wqp:1;
		/* If set, the hardware will send a response when done */
		uint64_t rsp:1;
		/*
		 * If set, the supplied pkt_ptr is really a pointer to
		 * a list of pkt_ptr's.
		 */
		uint64_t gather:1;
		/*
		 * If ipoffp1 is non zero, (ipoffp1-1) is the number
		 * of bytes to IP header, and the hardware will
		 * calculate and insert the UDP/TCP checksum.
		 */
		uint64_t ipoffp1:7;
		/*
		 * If set, ignore the I bit (force to zero) from all
		 * pointer structures.
		 */
		uint64_t ignore_i:1;
		/*
		 * If clear, the hardware will attempt to free the
		 * buffers containing the packet.
		 */
		uint64_t dontfree:1;
		/*
		 * The total number of segs in the packet, if gather
		 * set, also gather list length.
		 */
		uint64_t segs:6;
		/* Including L2, but no trailing CRC */
		uint64_t total_bytes:16;
#else
	        uint64_t total_bytes:16;
	        uint64_t segs:6;
	        uint64_t dontfree:1;
	        uint64_t ignore_i:1;
	        uint64_t ipoffp1:7;
	        uint64_t gather:1;
	        uint64_t rsp:1;
	        uint64_t wqp:1;
	        uint64_t n2:1;
	        uint64_t le:1;
	        uint64_t reg0:11;
	        uint64_t subone0:1;
	        uint64_t reg1:11;
	        uint64_t subone1:1;
	        uint64_t size0:2;
	        uint64_t size1:2;
#endif
	} s;
};

/* CSR typedefs have been moved to cvmx-csr-*.h */

/**
 * Definition of internal state for Packet output processing
 */
typedef struct {
	/* ptr to start of buffer, offset kept in FAU reg */
	uint64_t *start_ptr;
} cvmx_pko_state_elem_t;

/**
 * Call before any other calls to initialize the packet
 * output system.
 */
extern void cvmx_pko_initialize_global(void);

/**
 * Enables the packet output hardware. It must already be
 * configured.
 */
extern void cvmx_pko_enable(void);

/**
 * Disables the packet output. Does not affect any configuration.
 */
extern void cvmx_pko_disable(void);

/**
 * Shutdown and free resources required by packet output.
 */

extern void cvmx_pko_shutdown(void);

/**
 * Configure a output port and the associated queues for use.
 *
 * @port:	Port to configure.
 * @base_queue: First queue number to associate with this port.
 * @num_queues: Number of queues t oassociate with this port
 * @priority:	Array of priority levels for each queue. Values are
 *		     allowed to be 1-8. A value of 8 get 8 times the traffic
 *		     of a value of 1. There must be num_queues elements in the
 *		     array.
 */
extern cvmx_pko_status_t cvmx_pko_config_port(uint64_t port,
					      uint64_t base_queue,
					      uint64_t num_queues,
					      const uint64_t priority[]);

/**
 * Ring the packet output doorbell. This tells the packet
 * output hardware that "len" command words have been added
 * to its pending list.	 This command includes the required
 * CVMX_SYNCWS before the doorbell ring.
 *
 * @port:   Port the packet is for
 * @queue:  Queue the packet is for
 * @len:    Length of the command in 64 bit words
 */
static inline void cvmx_pko_doorbell(uint64_t port, uint64_t queue,
				     uint64_t len)
{
	cvmx_pko_doorbell_address_t ptr;

	ptr.u64 = 0;
	ptr.s.mem_space = CVMX_IO_SEG;
	ptr.s.did = CVMX_OCT_DID_PKT_SEND;
	ptr.s.is_io = 1;
	ptr.s.port = port;
	ptr.s.queue = queue;
	/*
	 * Need to make sure output queue data is in DRAM before
	 * doorbell write.
	 */
	CVMX_SYNCWS;
	cvmx_write_io(ptr.u64, len);
}

/**
 * Prepare to send a packet.  This may initiate a tag switch to
 * get exclusive access to the output queue structure, and
 * performs other prep work for the packet send operation.
 *
 * cvmx_pko_send_packet_finish() MUST be called after this function is called,
 * and must be called with the same port/queue/use_locking arguments.
 *
 * The use_locking parameter allows the caller to use three
 * possible locking modes.
 * - CVMX_PKO_LOCK_NONE
 *	- PKO doesn't do any locking. It is the responsibility
 *	    of the application to make sure that no other core
 *	    is accessing the same queue at the same time.
 * - CVMX_PKO_LOCK_ATOMIC_TAG
 *	- PKO performs an atomic tagswitch to insure exclusive
 *	    access to the output queue. This will maintain
 *	    packet ordering on output.
 * - CVMX_PKO_LOCK_CMD_QUEUE
 *	- PKO uses the common command queue locks to insure
 *	    exclusive access to the output queue. This is a
 *	    memory based ll/sc. This is the most portable
 *	    locking mechanism.
 *
 * NOTE: If atomic locking is used, the POW entry CANNOT be
 * descheduled, as it does not contain a valid WQE pointer.
 *
 * @port:   Port to send it on
 * @queue:  Queue to use
 * @use_locking: CVMX_PKO_LOCK_NONE, CVMX_PKO_LOCK_ATOMIC_TAG, or
 *		 CVMX_PKO_LOCK_CMD_QUEUE
 */

static inline void cvmx_pko_send_packet_prepare(uint64_t port, uint64_t queue,
						cvmx_pko_lock_t use_locking)
{
	if (use_locking == CVMX_PKO_LOCK_ATOMIC_TAG) {
		/*
		 * Must do a full switch here to handle all cases.  We
		 * use a fake WQE pointer, as the POW does not access
		 * this memory.	 The WQE pointer and group are only
		 * used if this work is descheduled, which is not
		 * supported by the
		 * cvmx_pko_send_packet_prepare/cvmx_pko_send_packet_finish
		 * combination.	 Note that this is a special case in
		 * which these fake values can be used - this is not a
		 * general technique.
		 */
		uint32_t tag =
		    CVMX_TAG_SW_BITS_INTERNAL << CVMX_TAG_SW_SHIFT |
		    CVMX_TAG_SUBGROUP_PKO << CVMX_TAG_SUBGROUP_SHIFT |
		    (CVMX_TAG_SUBGROUP_MASK & queue);
		cvmx_pow_tag_sw_full((struct cvmx_wqe *) cvmx_phys_to_ptr(0x80), tag,
				     CVMX_POW_TAG_TYPE_ATOMIC, 0);
	}
}

/**
 * Complete packet output. cvmx_pko_send_packet_prepare() must be
 * called exactly once before this, and the same parameters must be
 * passed to both cvmx_pko_send_packet_prepare() and
 * cvmx_pko_send_packet_finish().
 *
 * @port:   Port to send it on
 * @queue:  Queue to use
 * @pko_command:
 *		 PKO HW command word
 * @packet: Packet to send
 * @use_locking: CVMX_PKO_LOCK_NONE, CVMX_PKO_LOCK_ATOMIC_TAG, or
 *		 CVMX_PKO_LOCK_CMD_QUEUE
 *
 * Returns: CVMX_PKO_SUCCESS on success, or error code on
 * failure of output
 */
static inline cvmx_pko_status_t cvmx_pko_send_packet_finish(
	uint64_t port,
	uint64_t queue,
	union cvmx_pko_command_word0 pko_command,
	union cvmx_buf_ptr packet,
	cvmx_pko_lock_t use_locking)
{
	cvmx_cmd_queue_result_t result;
	if (use_locking == CVMX_PKO_LOCK_ATOMIC_TAG)
		cvmx_pow_tag_sw_wait();
	result = cvmx_cmd_queue_write2(CVMX_CMD_QUEUE_PKO(queue),
				       (use_locking == CVMX_PKO_LOCK_CMD_QUEUE),
				       pko_command.u64, packet.u64);
	if (likely(result == CVMX_CMD_QUEUE_SUCCESS)) {
		cvmx_pko_doorbell(port, queue, 2);
		return CVMX_PKO_SUCCESS;
	} else if ((result == CVMX_CMD_QUEUE_NO_MEMORY)
		   || (result == CVMX_CMD_QUEUE_FULL)) {
		return CVMX_PKO_NO_MEMORY;
	} else {
		return CVMX_PKO_INVALID_QUEUE;
	}
}

/**
 * Complete packet output. cvmx_pko_send_packet_prepare() must be
 * called exactly once before this, and the same parameters must be
 * passed to both cvmx_pko_send_packet_prepare() and
 * cvmx_pko_send_packet_finish().
 *
 * @port:   Port to send it on
 * @queue:  Queue to use
 * @pko_command:
 *		 PKO HW command word
 * @packet: Packet to send
 * @addr: Plysical address of a work queue entry or physical address
 *	  to zero on complete.
 * @use_locking: CVMX_PKO_LOCK_NONE, CVMX_PKO_LOCK_ATOMIC_TAG, or
 *		 CVMX_PKO_LOCK_CMD_QUEUE
 *
 * Returns: CVMX_PKO_SUCCESS on success, or error code on
 * failure of output
 */
static inline cvmx_pko_status_t cvmx_pko_send_packet_finish3(
	uint64_t port,
	uint64_t queue,
	union cvmx_pko_command_word0 pko_command,
	union cvmx_buf_ptr packet,
	uint64_t addr,
	cvmx_pko_lock_t use_locking)
{
	cvmx_cmd_queue_result_t result;
	if (use_locking == CVMX_PKO_LOCK_ATOMIC_TAG)
		cvmx_pow_tag_sw_wait();
	result = cvmx_cmd_queue_write3(CVMX_CMD_QUEUE_PKO(queue),
				       (use_locking == CVMX_PKO_LOCK_CMD_QUEUE),
				       pko_command.u64, packet.u64, addr);
	if (likely(result == CVMX_CMD_QUEUE_SUCCESS)) {
		cvmx_pko_doorbell(port, queue, 3);
		return CVMX_PKO_SUCCESS;
	} else if ((result == CVMX_CMD_QUEUE_NO_MEMORY)
		   || (result == CVMX_CMD_QUEUE_FULL)) {
		return CVMX_PKO_NO_MEMORY;
	} else {
		return CVMX_PKO_INVALID_QUEUE;
	}
}

/**
 * Return the pko output queue associated with a port and a specific core.
 * In normal mode (PKO lockless operation is disabled), the value returned
 * is the base queue.
 *
 * @port:   Port number
 * @core:   Core to get queue for
 *
 * Returns Core-specific output queue
 */
static inline int cvmx_pko_get_base_queue_per_core(int port, int core)
{
#ifndef CVMX_HELPER_PKO_MAX_PORTS_INTERFACE0
#define CVMX_HELPER_PKO_MAX_PORTS_INTERFACE0 16
#endif
#ifndef CVMX_HELPER_PKO_MAX_PORTS_INTERFACE1
#define CVMX_HELPER_PKO_MAX_PORTS_INTERFACE1 16
#endif

	if (port < CVMX_PKO_MAX_PORTS_INTERFACE0)
		return port * CVMX_PKO_QUEUES_PER_PORT_INTERFACE0 + core;
	else if (port >= 16 && port < 16 + CVMX_PKO_MAX_PORTS_INTERFACE1)
		return CVMX_PKO_MAX_PORTS_INTERFACE0 *
		    CVMX_PKO_QUEUES_PER_PORT_INTERFACE0 + (port -
							   16) *
		    CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 + core;
	else if ((port >= 32) && (port < 36))
		return CVMX_PKO_MAX_PORTS_INTERFACE0 *
		    CVMX_PKO_QUEUES_PER_PORT_INTERFACE0 +
		    CVMX_PKO_MAX_PORTS_INTERFACE1 *
		    CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 + (port -
							   32) *
		    CVMX_PKO_QUEUES_PER_PORT_PCI;
	else if ((port >= 36) && (port < 40))
		return CVMX_PKO_MAX_PORTS_INTERFACE0 *
		    CVMX_PKO_QUEUES_PER_PORT_INTERFACE0 +
		    CVMX_PKO_MAX_PORTS_INTERFACE1 *
		    CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 +
		    4 * CVMX_PKO_QUEUES_PER_PORT_PCI + (port -
							36) *
		    CVMX_PKO_QUEUES_PER_PORT_LOOP;
	else
		/* Given the limit on the number of ports we can map to
		 * CVMX_MAX_OUTPUT_QUEUES_STATIC queues (currently 256,
		 * divided among all cores), the remaining unmapped ports
		 * are assigned an illegal queue number */
		return CVMX_PKO_ILLEGAL_QUEUE;
}

/**
 * For a given port number, return the base pko output queue
 * for the port.
 *
 * @port:   Port number
 * Returns Base output queue
 */
static inline int cvmx_pko_get_base_queue(int port)
{
	if (OCTEON_IS_MODEL(OCTEON_CN68XX))
		return port;

	return cvmx_pko_get_base_queue_per_core(port, 0);
}

/**
 * For a given port number, return the number of pko output queues.
 *
 * @port:   Port number
 * Returns Number of output queues
 */
static inline int cvmx_pko_get_num_queues(int port)
{
	if (port < 16)
		return CVMX_PKO_QUEUES_PER_PORT_INTERFACE0;
	else if (port < 32)
		return CVMX_PKO_QUEUES_PER_PORT_INTERFACE1;
	else if (port < 36)
		return CVMX_PKO_QUEUES_PER_PORT_PCI;
	else if (port < 40)
		return CVMX_PKO_QUEUES_PER_PORT_LOOP;
	else
		return 0;
}

/**
 * Get the status counters for a port.
 *
 * @port_num: Port number to get statistics for.
 * @clear:    Set to 1 to clear the counters after they are read
 * @status:   Where to put the results.
 */
static inline void cvmx_pko_get_port_status(uint64_t port_num, uint64_t clear,
					    cvmx_pko_port_status_t *status)
{
	union cvmx_pko_reg_read_idx pko_reg_read_idx;
	union cvmx_pko_mem_count0 pko_mem_count0;
	union cvmx_pko_mem_count1 pko_mem_count1;

	pko_reg_read_idx.u64 = 0;
	pko_reg_read_idx.s.index = port_num;
	cvmx_write_csr(CVMX_PKO_REG_READ_IDX, pko_reg_read_idx.u64);

	pko_mem_count0.u64 = cvmx_read_csr(CVMX_PKO_MEM_COUNT0);
	status->packets = pko_mem_count0.s.count;
	if (clear) {
		pko_mem_count0.s.count = port_num;
		cvmx_write_csr(CVMX_PKO_MEM_COUNT0, pko_mem_count0.u64);
	}

	pko_mem_count1.u64 = cvmx_read_csr(CVMX_PKO_MEM_COUNT1);
	status->octets = pko_mem_count1.s.count;
	if (clear) {
		pko_mem_count1.s.count = port_num;
		cvmx_write_csr(CVMX_PKO_MEM_COUNT1, pko_mem_count1.u64);
	}

	if (OCTEON_IS_MODEL(OCTEON_CN3XXX)) {
		union cvmx_pko_mem_debug9 debug9;
		pko_reg_read_idx.s.index = cvmx_pko_get_base_queue(port_num);
		cvmx_write_csr(CVMX_PKO_REG_READ_IDX, pko_reg_read_idx.u64);
		debug9.u64 = cvmx_read_csr(CVMX_PKO_MEM_DEBUG9);
		status->doorbell = debug9.cn38xx.doorbell;
	} else {
		union cvmx_pko_mem_debug8 debug8;
		pko_reg_read_idx.s.index = cvmx_pko_get_base_queue(port_num);
		cvmx_write_csr(CVMX_PKO_REG_READ_IDX, pko_reg_read_idx.u64);
		debug8.u64 = cvmx_read_csr(CVMX_PKO_MEM_DEBUG8);
		status->doorbell = debug8.cn50xx.doorbell;
	}
}

/**
 * Rate limit a PKO port to a max packets/sec. This function is only
 * supported on CN57XX, CN56XX, CN55XX, and CN54XX.
 *
 * @port:      Port to rate limit
 * @packets_s: Maximum packet/sec
 * @burst:     Maximum number of packets to burst in a row before rate
 *		    limiting cuts in.
 *
 * Returns Zero on success, negative on failure
 */
extern int cvmx_pko_rate_limit_packets(int port, int packets_s, int burst);

/**
 * Rate limit a PKO port to a max bits/sec. This function is only
 * supported on CN57XX, CN56XX, CN55XX, and CN54XX.
 *
 * @port:   Port to rate limit
 * @bits_s: PKO rate limit in bits/sec
 * @burst:  Maximum number of bits to burst before rate
 *		 limiting cuts in.
 *
 * Returns Zero on success, negative on failure
 */
extern int cvmx_pko_rate_limit_bits(int port, uint64_t bits_s, int burst);

#endif /* __CVMX_PKO_H__ */
