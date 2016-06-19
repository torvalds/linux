#ifndef __CVMX_CONFIG_H__
#define __CVMX_CONFIG_H__

/************************* Config Specific Defines ************************/
#define CVMX_LLM_NUM_PORTS 1
#define CVMX_NULL_POINTER_PROTECT 1
#define CVMX_ENABLE_DEBUG_PRINTS 1
/* PKO queues per port for interface 0 (ports 0-15) */
#define CVMX_PKO_QUEUES_PER_PORT_INTERFACE0 1
/* PKO queues per port for interface 1 (ports 16-31) */
#define CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 1
/* Limit on the number of PKO ports enabled for interface 0 */
#define CVMX_PKO_MAX_PORTS_INTERFACE0 CVMX_HELPER_PKO_MAX_PORTS_INTERFACE0
/* Limit on the number of PKO ports enabled for interface 1 */
#define CVMX_PKO_MAX_PORTS_INTERFACE1 CVMX_HELPER_PKO_MAX_PORTS_INTERFACE1
/* PKO queues per port for PCI (ports 32-35) */
#define CVMX_PKO_QUEUES_PER_PORT_PCI 1
/* PKO queues per port for Loop devices (ports 36-39) */
#define CVMX_PKO_QUEUES_PER_PORT_LOOP 1

/************************* FPA allocation *********************************/
/* Pool sizes in bytes, must be multiple of a cache line */
#define CVMX_FPA_POOL_0_SIZE (16 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_1_SIZE (1 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_2_SIZE (8 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_3_SIZE (0 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_4_SIZE (0 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_5_SIZE (0 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_6_SIZE (0 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_7_SIZE (0 * CVMX_CACHE_LINE_SIZE)

/* Pools in use */
/* Packet buffers */
#define CVMX_FPA_PACKET_POOL		    (0)
#define CVMX_FPA_PACKET_POOL_SIZE	    CVMX_FPA_POOL_0_SIZE
/* Work queue entries */
#define CVMX_FPA_WQE_POOL		    (1)
#define CVMX_FPA_WQE_POOL_SIZE		    CVMX_FPA_POOL_1_SIZE
/* PKO queue command buffers */
#define CVMX_FPA_OUTPUT_BUFFER_POOL	    (2)
#define CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE    CVMX_FPA_POOL_2_SIZE

/*************************  FAU allocation ********************************/
/* The fetch and add registers are allocated here.  They are arranged
 * in order of descending size so that all alignment constraints are
 * automatically met.  The enums are linked so that the following enum
 * continues allocating where the previous one left off, so the
 * numbering within each enum always starts with zero.	The macros
 * take care of the address increment size, so the values entered
 * always increase by 1.  FAU registers are accessed with byte
 * addresses.
 */

#define CVMX_FAU_REG_64_ADDR(x) ((x << 3) + CVMX_FAU_REG_64_START)
typedef enum {
	CVMX_FAU_REG_64_START	= 0,
	CVMX_FAU_REG_64_END	= CVMX_FAU_REG_64_ADDR(0),
} cvmx_fau_reg_64_t;

#define CVMX_FAU_REG_32_ADDR(x) ((x << 2) + CVMX_FAU_REG_32_START)
typedef enum {
	CVMX_FAU_REG_32_START	= CVMX_FAU_REG_64_END,
	CVMX_FAU_REG_32_END	= CVMX_FAU_REG_32_ADDR(0),
} cvmx_fau_reg_32_t;

#define CVMX_FAU_REG_16_ADDR(x) ((x << 1) + CVMX_FAU_REG_16_START)
typedef enum {
	CVMX_FAU_REG_16_START	= CVMX_FAU_REG_32_END,
	CVMX_FAU_REG_16_END	= CVMX_FAU_REG_16_ADDR(0),
} cvmx_fau_reg_16_t;

#define CVMX_FAU_REG_8_ADDR(x) ((x) + CVMX_FAU_REG_8_START)
typedef enum {
	CVMX_FAU_REG_8_START	= CVMX_FAU_REG_16_END,
	CVMX_FAU_REG_8_END	= CVMX_FAU_REG_8_ADDR(0),
} cvmx_fau_reg_8_t;

/*
 * The name CVMX_FAU_REG_AVAIL_BASE is provided to indicate the first
 * available FAU address that is not allocated in cvmx-config.h. This
 * is 64 bit aligned.
 */
#define CVMX_FAU_REG_AVAIL_BASE ((CVMX_FAU_REG_8_END + 0x7) & (~0x7ULL))
#define CVMX_FAU_REG_END (2048)

/********************** scratch memory allocation *************************/
/* Scratchpad memory allocation.  Note that these are byte memory
 * addresses.  Some uses of scratchpad (IOBDMA for example) require
 * the use of 8-byte aligned addresses, so proper alignment needs to
 * be taken into account.
 */
/* Generic scratch iobdma area */
#define CVMX_SCR_SCRATCH	       (0)
/* First location available after cvmx-config.h allocated region. */
#define CVMX_SCR_REG_AVAIL_BASE	       (8)

/*
 * CVMX_HELPER_FIRST_MBUFF_SKIP is the number of bytes to reserve
 * before the beginning of the packet. If necessary, override the
 * default here.  See the IPD section of the hardware manual for MBUFF
 * SKIP details.
 */
#define CVMX_HELPER_FIRST_MBUFF_SKIP 184

/*
 * CVMX_HELPER_NOT_FIRST_MBUFF_SKIP is the number of bytes to reserve
 * in each chained packet element. If necessary, override the default
 * here.
 */
#define CVMX_HELPER_NOT_FIRST_MBUFF_SKIP 0

/*
 * CVMX_HELPER_ENABLE_BACK_PRESSURE controls whether back pressure is
 * enabled for all input ports. This controls if IPD sends
 * backpressure to all ports if Octeon's FPA pools don't have enough
 * packet or work queue entries. Even when this is off, it is still
 * possible to get backpressure from individual hardware ports. When
 * configuring backpressure, also check
 * CVMX_HELPER_DISABLE_*_BACKPRESSURE below. If necessary, override
 * the default here.
 */
#define CVMX_HELPER_ENABLE_BACK_PRESSURE 1

/*
 * CVMX_HELPER_ENABLE_IPD controls if the IPD is enabled in the helper
 * function. Once it is enabled the hardware starts accepting
 * packets. You might want to skip the IPD enable if configuration
 * changes are need from the default helper setup. If necessary,
 * override the default here.
 */
#define CVMX_HELPER_ENABLE_IPD 0

/*
 * CVMX_HELPER_INPUT_TAG_TYPE selects the type of tag that the IPD assigns
 * to incoming packets.
 */
#define CVMX_HELPER_INPUT_TAG_TYPE CVMX_POW_TAG_TYPE_ORDERED

#define CVMX_ENABLE_PARAMETER_CHECKING 0

/*
 * The following select which fields are used by the PIP to generate
 * the tag on INPUT
 * 0: don't include
 * 1: include
 */
#define CVMX_HELPER_INPUT_TAG_IPV6_SRC_IP	0
#define CVMX_HELPER_INPUT_TAG_IPV6_DST_IP	0
#define CVMX_HELPER_INPUT_TAG_IPV6_SRC_PORT	0
#define CVMX_HELPER_INPUT_TAG_IPV6_DST_PORT	0
#define CVMX_HELPER_INPUT_TAG_IPV6_NEXT_HEADER	0
#define CVMX_HELPER_INPUT_TAG_IPV4_SRC_IP	0
#define CVMX_HELPER_INPUT_TAG_IPV4_DST_IP	0
#define CVMX_HELPER_INPUT_TAG_IPV4_SRC_PORT	0
#define CVMX_HELPER_INPUT_TAG_IPV4_DST_PORT	0
#define CVMX_HELPER_INPUT_TAG_IPV4_PROTOCOL	0
#define CVMX_HELPER_INPUT_TAG_INPUT_PORT	1

/* Select skip mode for input ports */
#define CVMX_HELPER_INPUT_PORT_SKIP_MODE	CVMX_PIP_PORT_CFG_MODE_SKIPL2

/*
 * Force backpressure to be disabled.  This overrides all other
 * backpressure configuration.
 */
#define CVMX_HELPER_DISABLE_RGMII_BACKPRESSURE 0

#endif /* __CVMX_CONFIG_H__ */
