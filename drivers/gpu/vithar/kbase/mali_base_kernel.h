/*
 *
 * (C) COPYRIGHT 2010, 2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file
 * Base structures shared with the kernel.
 */

#ifndef _BASE_KERNEL_H_
#define _BASE_KERNEL_H_

#include <kbase/src/mali_base_mem_priv.h>

/*
 * Dependency stuff, keep it private for now. May want to expose it if
 * we decide to make the number of semaphores a configurable
 * option.
 */
#define BASEP_JD_SEM_PER_WORD_LOG2      5
#define BASEP_JD_SEM_PER_WORD           (1 << BASEP_JD_SEM_PER_WORD_LOG2)
#define BASEP_JD_SEM_WORD_NR(x)         ((x) >> BASEP_JD_SEM_PER_WORD_LOG2)
#define BASEP_JD_SEM_MASK_IN_WORD(x)    (1 << ((x) & (BASEP_JD_SEM_PER_WORD - 1)))
#define BASEP_JD_SEM_ARRAY_SIZE         BASEP_JD_SEM_WORD_NR(256)

/* Size of the ring buffer */
#define BASEP_JCTX_RB_NRPAGES           16

#define BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS 3

#define BASE_MAX_COHERENT_GROUPS 16


/**
 * @addtogroup base_user_api User-side Base APIs
 * @{
 */

/**
 * @addtogroup base_user_api_memory User-side Base Memory APIs
 * @{
 */

/**
 * @brief Memory allocation, access/hint flags
 *
 * A combination of MEM_PROT/MEM_HINT flags must be passed to each allocator
 * in order to determine the best cache policy. Some combinations are
 * of course invalid (eg @c MEM_PROT_CPU_WR | @c MEM_HINT_CPU_RD),
 * which defines a @a write-only region on the CPU side, which is
 * heavily read by the CPU...
 * Other flags are only meaningful to a particular allocator.
 * More flags can be added to this list, as long as they don't clash
 * (see ::BASE_MEM_FLAGS_NR_BITS for the number of the first free bit).
 */
typedef u32 base_mem_alloc_flags;


/**
 * @brief Memory allocation, access/hint flags
 *
 * See ::base_mem_alloc_flags.
 *
 */
enum
{
	BASE_MEM_PROT_CPU_RD =      (1U << 0), /**< Read access CPU side */
	BASE_MEM_PROT_CPU_WR =      (1U << 1), /**< Write access CPU side */
	BASE_MEM_PROT_GPU_RD =      (1U << 2), /**< Read access GPU side */
	BASE_MEM_PROT_GPU_WR =      (1U << 3), /**< Write access GPU side */
	BASE_MEM_PROT_GPU_EX =      (1U << 4), /**< Execute allowed on the GPU side */

	BASE_MEM_HINT_CPU_RD =      (1U << 5), /**< Heavily read CPU side */
	BASE_MEM_HINT_CPU_WR =      (1U << 6), /**< Heavily written CPU side */
	BASE_MEM_HINT_GPU_RD =      (1U << 7), /**< Heavily read GPU side */
	BASE_MEM_HINT_GPU_WR =      (1U << 8), /**< Heavily written GPU side */

	BASEP_MEM_GROWABLE   =      (1U << 9), /**< Growable memory. This is a private flag that is set automatically. Not valid for PMEM. */
	BASE_MEM_GROW_ON_GPF =      (1U << 10), /**< Grow backing store on GPU Page Fault */

	BASE_MEM_COHERENT_SYSTEM =  (1U << 11),/**< Page coherence Outer shareable */
	BASE_MEM_COHERENT_LOCAL =   (1U << 12) /**< Page coherence Inner shareable */
};

/**
 * @brief Number of bits used as flags for base memory management
 *
 * Must be kept in sync with the ::base_mem_alloc_flags flags
 */
#define BASE_MEM_FLAGS_NR_BITS  13

/**
 * @brief Result codes of changing the size of the backing store allocated to a tmem region
 */
typedef enum base_backing_threshold_status
{
	BASE_BACKING_THRESHOLD_OK = 0,                      /**< Resize successful */
	BASE_BACKING_THRESHOLD_ERROR_NOT_GROWABLE = -1,     /**< Not a growable tmem object */
	BASE_BACKING_THRESHOLD_ERROR_OOM = -2,              /**< Increase failed due to an out-of-memory condition */
	BASE_BACKING_THRESHOLD_ERROR_MAPPED = -3,           /**< Resize attempted on buffer while it was mapped, which is not permitted */
	BASE_BACKING_THRESHOLD_ERROR_INVALID_ARGUMENTS = -4 /**< Invalid arguments (not tmem, illegal size request, etc.) */
} base_backing_threshold_status;

/**
 * @addtogroup base_user_api_memory_defered User-side Base Defered Memory Coherency APIs
 * @{
 */

/**
 * @brief a basic memory operation (sync-set).
 *
 * The content of this structure is private, and should only be used
 * by the accessors.
 */
typedef struct base_syncset
{
	basep_syncset basep_sset;
} base_syncset;

/** @} end group base_user_api_memory_defered */

/** @} end group base_user_api_memory */

/**
 * @addtogroup base_user_api_job_dispatch User-side Base Job Dispatcher APIs
 * @{
 */

/**
 * @brief A pre- or post- dual dependency.
 *
 * This structure is used to express either
 * @li a single or dual pre-dependency (a job depending on one or two
 * other jobs),
 * @li a single or dual post-dependency (a job resolving a dependency
 * for one or two other jobs).
 *
 * The dependency itself is specified as a u8, where 0 indicates no
 * dependency. A single dependency is expressed by having one of the
 * dependencies set to 0.
 */
typedef struct base_jd_dep {
	u8      dep[2]; /**< pre/post dependencies */
} base_jd_dep;

/**
 * @brief Per-job data
 *
 * This structure is used to store per-job data, and is completly unused
 * by the Base driver. It can be used to store things such as callback
 * function pointer, data to handle job completion. It is guaranteed to be
 * untouched by the Base driver.
 */
typedef struct base_jd_udata
{
	u64     blob[1]; /**< per-job data array */
} base_jd_udata;

/**
 * @brief Job chain hardware requirements.
 *
 * A job chain must specify what GPU features it needs to allow the
 * driver to schedule the job correctly.  By not specifying the
 * correct settings can/will cause an early job termination.  Multiple
 * values can be ORed together to specify multiple requirements.
 * Special case is ::BASE_JD_REQ_DEP, which is used to express complex
 * dependencies, and that doesn't execute anything on the hardware.
 */
typedef u16 base_jd_core_req;

/* Requirements that come from the HW */
#define BASE_JD_REQ_DEP 0           /**< No requirement, dependency only */
#define BASE_JD_REQ_FS  (1U << 0)   /**< Requires fragment shaders */
#define BASE_JD_REQ_CS  (1U << 1)   /**< Requires compute shaders */
#define BASE_JD_REQ_T   (1U << 2)   /**< Requires tiling */
#define BASE_JD_REQ_CF  (1U << 3)   /**< Requires cache flushes */
#define BASE_JD_REQ_V   (1U << 4)   /**< Requires value writeback */

/* SW-only requirements - the HW does not expose these as part of the job slot capabilities */
/**
 * SW Only requirement: this job chain might not be soft-stoppable (Non-Soft
 * Stoppable), and so must be scheduled separately from all other job-chains
 * that are soft-stoppable.
 *
 * In absence of this requirement, then the job-chain is assumed to be
 * soft-stoppable. That is, if it does not release the GPU "soon after" it is
 * soft-stopped, then it will be killed. In contrast, NSS job chains can
 * release the GPU "a long time after" they are soft-stopped.
 *
 * "soon after" and "a long time after" are implementation defined, and
 * configurable in the device driver by the system integrator.
 */
#define BASE_JD_REQ_NSS             (1U << 5)

/**
 * SW Only requirement: the job chain requires a coherent core group. We don't
 * mind which coherent core group is used.
 */
#define BASE_JD_REQ_COHERENT_GROUP  (1U << 6)

/**
 * SW Only requirement: The performance counters should be enabled only when
 * they are needed, to reduce power consumption.
 */

#define BASE_JD_REQ_PERMON          (1U << 7)

/**
 * SW Only requirement: Software defined job. Jobs with this bit set will not be submitted
 * to the hardware but will cause some action to happen within the driver
 */
#define BASE_JD_REQ_SOFT_JOB        (1U << 8)

#define BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME      (BASE_JD_REQ_SOFT_JOB | 0x1)

/**
* These requirement bits are currently unused in base_jd_core_req (currently a u16)
*/

#define BASEP_JD_REQ_RESERVED_BIT9  ( 1U << 9 )
#define BASEP_JD_REQ_RESERVED_BIT10 ( 1U << 10 )
#define BASEP_JD_REQ_RESERVED_BIT11 ( 1U << 11 )
#define BASEP_JD_REQ_RESERVED_BIT12 ( 1U << 12 )
#define BASEP_JD_REQ_RESERVED_BIT13 ( 1U << 13 )
#define BASEP_JD_REQ_RESERVED_BIT14 ( 1U << 14 )
#define BASEP_JD_REQ_RESERVED_BIT15 ( 1U << 15 )

/**
* Mask of all the currently unused requirement bits in base_jd_core_req.
*/

#define BASEP_JD_REQ_RESERVED ( BASEP_JD_REQ_RESERVED_BIT9  |\
                                BASEP_JD_REQ_RESERVED_BIT10 | BASEP_JD_REQ_RESERVED_BIT11 |\
                                BASEP_JD_REQ_RESERVED_BIT12 | BASEP_JD_REQ_RESERVED_BIT13 |\
                                BASEP_JD_REQ_RESERVED_BIT14 | BASEP_JD_REQ_RESERVED_BIT15 )


/**
 * @brief A single job chain, with pre/post dependendencies and mem ops
 *
 * This structure is used to describe a single job-chain to be submitted
 * as part of a bag.
 * It contains all the necessary information for Base to take care of this
 * job-chain, including core requirements, priority, syncsets and
 * dependencies.
 */
typedef struct base_jd_atom
{
	base_jd_udata       udata;          /**< user data */
	mali_addr64         jc;             /**< job-chain GPU address */
	base_jd_dep         pre_dep;        /**< pre-dependencies */
	base_jd_dep         post_dep;       /**< post-dependencies */
	u16                 nr_syncsets;    /**< nr of syncsets following the atom */
	base_jd_core_req    core_req;       /**< core requirements */

	/** @brief Relative priority.
	 *
	 * A positive value requests a lower priority, whilst a negative value
	 * requests a higher priority. Only privileged processes may request a
	 * higher priority. For unprivileged processes, a negative priority will
	 * be interpreted as zero.
	 */
	s8                  prio;
} base_jd_atom;

/* Lot of hacks to cope with the fact that C89 doesn't allow arrays of size 0 */
typedef struct basep_jd_atom_ss
{
	base_jd_atom    atom;
	base_syncset    syncsets[1];
} basep_jd_atom_ss;

/**
 * @brief Atom size evaluator
 *
 * This function returns the size in bytes of a ::base_jd_atom
 * containing @a n syncsets. It must be used to compute the size of a
 * bag before allocation.
 *
 * @param nr the number of syncsets for this atom
 * @return the atom size in bytes
 */
static INLINE size_t base_jd_atom_size(u32 nr)
{
	return nr ? offsetof(basep_jd_atom_ss, syncsets[nr]) : sizeof(base_jd_atom);
}

/**
 * @brief Atom syncset accessor
 *
 * This function returns a pointer to the nth syncset allocated
 * together with an atom.
 *
 * @param[in] atom The allocated atom
 * @param     n    The number of the syncset to be returned
 * @return a pointer to the nth syncset.
 */
static INLINE base_syncset *base_jd_get_atom_syncset(base_jd_atom *atom, int n)
{
#if defined CDBG_ASSERT
	CDBG_ASSERT(atom != NULL);
	CDBG_ASSERT( (n >= 0) && (n <= atom->nr_syncsets) );
#elif defined OSK_ASSERT
	OSK_ASSERT(atom != NULL);
	OSK_ASSERT( (n >= 0) && (n <= atom->nr_syncsets) );
#else
#error assert macro not defined!
#endif
	return &((basep_jd_atom_ss *)atom)->syncsets[n];
}

/**
 * @brief Next atom accessor
 *
 * This function returns a pointer to the next allocated atom. It
 * relies on the fact that the current atom has been correctly
 * initialized (relies on the base_jd_atom::nr_syncsets field).
 *
 * @param[in] atom The allocated atom
 * @return a pointer to the next atom.
 */
static INLINE base_jd_atom *base_jd_get_next_atom(base_jd_atom *atom)
{
#if defined CDBG_ASSERT
	CDBG_ASSERT(atom != NULL);
#elif defined OSK_ASSERT
	OSK_ASSERT(atom != NULL);
#else
#error assert macro not defined!
#endif
	return (base_jd_atom *)base_jd_get_atom_syncset(atom, atom->nr_syncsets);
}

/**
 * @brief Job chain event code bits
 * Defines the bits used to create ::base_jd_event_code
 */
enum
{
	BASE_JD_SW_EVENT_KERNEL = (1u << 15), /**< Kernel side event */
	BASE_JD_SW_EVENT = (1u << 14), /**< SW defined event */
	BASE_JD_SW_EVENT_SUCCESS = (1u << 13), /**< Event idicates success (SW events only) */
	BASE_JD_SW_EVENT_JOB = (0u << 11), /**< Job related event */
	BASE_JD_SW_EVENT_BAG = (1u << 11), /**< Bag related event */
	BASE_JD_SW_EVENT_INFO = (2u << 11), /**< Misc/info event */
	BASE_JD_SW_EVENT_RESERVED = (3u << 11), /**< Reserved event type */
	BASE_JD_SW_EVENT_TYPE_MASK = (3u << 11) /**< Mask to extract the type from an event code */
};

/**
 * @brief Job chain event codes
 *
 * HW and low-level SW events are represented by event codes.
 * The status of jobs which succeeded are also represented by
 * an event code (see ::BASE_JD_EVENT_DONE).
 * Events are usually reported as part of a ::base_jd_event.
 *
 * The event codes are encoded in the following way:
 * @li 10:0  - subtype
 * @li 12:11 - type
 * @li 13    - SW success (only valid if the SW bit is set)
 * @li 14    - SW event (HW event if not set)
 * @li 15    - Kernel event (should never be seen in userspace)
 *
 * Events are split up into ranges as follows:
 * - BASE_JD_EVENT_RANGE_\<description\>_START
 * - BASE_JD_EVENT_RANGE_\<description\>_END
 *
 * \a code is in \<description\>'s range when:
 * - <tt>BASE_JD_EVENT_RANGE_\<description\>_START <= code < BASE_JD_EVENT_RANGE_\<description\>_END </tt>
 *
 * Ranges can be asserted for adjacency by testing that the END of the previous
 * is equal to the START of the next. This is useful for optimizing some tests
 * for range.
 *
 * A limitation is that the last member of this enum must explicitly be handled
 * (with an assert-unreachable statement) in switch statements that use
 * variables of this type. Otherwise, the compiler warns that we have not
 * handled that enum value.
 */
typedef enum base_jd_event_code
{
	/* HW defined exceptions */

	/** Start of HW Non-fault status codes
	 *
	 * @note Obscurely, BASE_JD_EVENT_TERMINATED indicates a real fault,
	 * because the job was hard-stopped
	 */
	BASE_JD_EVENT_RANGE_HW_NONFAULT_START = 0,

	/* non-fatal exceptions */
	BASE_JD_EVENT_NOT_STARTED = 0x00, /**< Can't be seen by userspace, treated as 'previous job done' */
	BASE_JD_EVENT_DONE = 0x01, 
	BASE_JD_EVENT_STOPPED = 0x03,     /**< Can't be seen by userspace, becomes TERMINATED, DONE or JOB_CANCELLED */
	BASE_JD_EVENT_TERMINATED = 0x04,  /**< This is actually a fault status code - the job was hard stopped */
	BASE_JD_EVENT_ACTIVE = 0x08,      /**< Can't be seen by userspace, jobs only returned on complete/fail/cancel */

	/** End of HW Non-fault status codes
	 *
	 * @note Obscurely, BASE_JD_EVENT_TERMINATED indicates a real fault,
	 * because the job was hard-stopped
	 */
	BASE_JD_EVENT_RANGE_HW_NONFAULT_END = 0x40,

	/** Start of HW fault and SW Error status codes */
	BASE_JD_EVENT_RANGE_HW_FAULT_OR_SW_ERROR_START = 0x40,

	/* job exceptions */
	BASE_JD_EVENT_JOB_CONFIG_FAULT = 0x40,
	BASE_JD_EVENT_JOB_POWER_FAULT = 0x41,
	BASE_JD_EVENT_JOB_READ_FAULT = 0x42,
	BASE_JD_EVENT_JOB_WRITE_FAULT = 0x43,
	BASE_JD_EVENT_JOB_AFFINITY_FAULT = 0x44,
	BASE_JD_EVENT_JOB_BUS_FAULT = 0x48,
	BASE_JD_EVENT_INSTR_INVALID_PC = 0x50,
	BASE_JD_EVENT_INSTR_INVALID_ENC = 0x51,
	BASE_JD_EVENT_INSTR_TYPE_MISMATCH = 0x52,
	BASE_JD_EVENT_INSTR_OPERAND_FAULT = 0x53,
	BASE_JD_EVENT_INSTR_TLS_FAULT = 0x54,
	BASE_JD_EVENT_INSTR_BARRIER_FAULT = 0x55,
	BASE_JD_EVENT_INSTR_ALIGN_FAULT = 0x56,
	BASE_JD_EVENT_DATA_INVALID_FAULT = 0x58,
	BASE_JD_EVENT_TILE_RANGE_FAULT = 0x59,
	BASE_JD_EVENT_STATE_FAULT = 0x5A,
	BASE_JD_EVENT_OUT_OF_MEMORY = 0x60,
	BASE_JD_EVENT_UNKNOWN = 0x7F,
	
	/* GPU exceptions */
	BASE_JD_EVENT_DELAYED_BUS_FAULT = 0x80,
	BASE_JD_EVENT_SHAREABILITY_FAULT = 0x88,
	
	/* MMU exceptions */
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL1  = 0xC1,
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL2  = 0xC2,
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL3  = 0xC3,
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL4  = 0xC4,
	BASE_JD_EVENT_PERMISSION_FAULT          = 0xC8,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL1 = 0xD1,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL2 = 0xD2,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL3 = 0xD3,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL4 = 0xD4,
	BASE_JD_EVENT_ACCESS_FLAG               = 0xD8,

	/* SW defined exceptions */
	BASE_JD_EVENT_MEM_GROWTH_FAILED = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x000,
	BASE_JD_EVENT_TIMED_OUT         = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x001,
	BASE_JD_EVENT_JOB_CANCELLED     = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x002,
	BASE_JD_EVENT_BAG_INVALID       = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_BAG | 0x003,

	/** End of HW fault and SW Error status codes */
	BASE_JD_EVENT_RANGE_HW_FAULT_OR_SW_ERROR_END = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_RESERVED | 0x3FF,

	/** Start of SW Success status codes */
	BASE_JD_EVENT_RANGE_SW_SUCCESS_START = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_SUCCESS | 0x000,

	BASE_JD_EVENT_PROGRESS_REPORT   = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_SUCCESS | BASE_JD_SW_EVENT_JOB  | 0x000,
	BASE_JD_EVENT_BAG_DONE          = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_SUCCESS | BASE_JD_SW_EVENT_BAG  | 0x000,
	BASE_JD_EVENT_DRV_TERMINATED    = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_SUCCESS | BASE_JD_SW_EVENT_INFO | 0x000,

	/** End of SW Success status codes */
	BASE_JD_EVENT_RANGE_SW_SUCCESS_END = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_SUCCESS | BASE_JD_SW_EVENT_RESERVED | 0x3FF,

	/** Start of Kernel-only status codes. Such codes are never returned to user-space */
	BASE_JD_EVENT_RANGE_KERNEL_ONLY_START = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_KERNEL | 0x000,
	BASE_JD_EVENT_REMOVED_FROM_NEXT = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_KERNEL | BASE_JD_SW_EVENT_JOB | 0x000,

	/** End of Kernel-only status codes. */
	BASE_JD_EVENT_RANGE_KERNEL_ONLY_END = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_KERNEL | BASE_JD_SW_EVENT_RESERVED | 0x3FF
} base_jd_event_code;

/**
 * @brief Event reporting structure
 *
 * This structure is used by the kernel driver to report information
 * about GPU events. The can either be HW-specific events or low-level
 * SW events, such as job-chain completion.
 *
 * The event code contains an event type field which can be extracted
 * by ANDing with ::BASE_JD_SW_EVENT_TYPE_MASK.
 *
 * Based on the event type base_jd_event::data holds:
 * @li ::BASE_JD_SW_EVENT_JOB : the offset in the ring-buffer for the completed
 * job-chain
 * @li ::BASE_JD_SW_EVENT_BAG : The address of the ::base_jd_bag that has
 * been completed (ie all contained job-chains have been completed).
 * @li ::BASE_JD_SW_EVENT_INFO : base_jd_event::data not used
 */
typedef struct base_jd_event
{
	base_jd_event_code      event_code; /**< event code */
	void                  * data;       /**< event specific data */
} base_jd_event;

/**
 * @brief Structure for BASE_JD_REQ_SOFT_DUMP_CPU_GPU_COUNTERS jobs.
 *
 * This structure is stored into the memory pointed to by the @c jc field of @ref base_jd_atom.
 */
typedef struct base_dump_cpu_gpu_counters {
	u64 system_time;
	u64 cycle_counter;
	u64 sec;
	u32 usec;
} base_dump_cpu_gpu_counters;

/** @} end group base_user_api_job_dispatch */


#ifdef __KERNEL__
/*
 * The following typedefs should be removed when a midg types header is added.
 * See MIDCOM-1657 for details.
 */
typedef u32 midg_product_id;
typedef u32 midg_cache_features;
typedef u32 midg_tiler_features;
typedef u32 midg_mem_features;
typedef u32 midg_mmu_features;
typedef u32 midg_js_features;
typedef u32 midg_as_present;
typedef u32 midg_js_present;

#define MIDG_MAX_JOB_SLOTS 16

#else
#include <midg/mali_midg.h>
#endif

/**
 * @page page_base_user_api_gpuprops User-side Base GPU Property Query API
 *
 * The User-side Base GPU Property Query API encapsulates two
 * sub-modules:
 *
 * - @ref base_user_api_gpuprops_dyn "Dynamic GPU Properties"
 * - @ref base_plat_config_gpuprops "Base Platform Config GPU Properties"
 *
 * There is a related third module outside of Base, which is owned by the MIDG
 * module:
 * - @ref midg_gpuprops_static "Midgard Compile-time GPU Properties"
 *
 * Base only deals with properties that vary between different Midgard
 * implementations - the Dynamic GPU properties and the Platform Config
 * properties.
 *
 * For properties that are constant for the Midgard Architecture, refer to the
 * MIDG module. However, we will discuss their relevance here <b>just to
 * provide background information.</b>
 *
 * @section sec_base_user_api_gpuprops_about About the GPU Properties in Base and MIDG modules
 *
 * The compile-time properties (Platform Config, Midgard Compile-time
 * properties) are exposed as pre-processor macros.
 *
 * Complementing the compile-time properties are the Dynamic GPU
 * Properties, which act as a conduit for the Midgard Configuration
 * Discovery.
 *
 * In general, the dynamic properties are present to verify that the platform
 * has been configured correctly with the right set of Platform Config
 * Compile-time Properties.
 *
 * As a consistant guide across the entire DDK, the choice for dynamic or
 * compile-time should consider the following, in order:
 * -# Can the code be written so that it doesn't need to know the
 * implementation limits at all?
 * -# If you need the limits, get the information from the Dynamic Property
 * lookup. This should be done once as you fetch the context, and then cached
 * as part of the context data structure, so it's cheap to access.
 * -# If there's a clear and arguable inefficiency in using Dynamic Properties,
 * then use a Compile-Time Property (Platform Config, or Midgard Compile-time
 * property). Examples of where this might be sensible follow:
 *  - Part of a critical inner-loop
 *  - Frequent re-use throughout the driver, causing significant extra load
 * instructions or control flow that would be worthwhile optimizing out.
 *
 * We cannot provide an exhaustive set of examples, neither can we provide a
 * rule for every possible situation. Use common sense, and think about: what
 * the rest of the driver will be doing; how the compiler might represent the
 * value if it is a compile-time constant; whether an OEM shipping multiple
 * devices would benefit much more from a single DDK binary, instead of
 * insignificant micro-optimizations.
 *
 * @section sec_base_user_api_gpuprops_dyn Dynamic GPU Properties
 *
 * Dynamic GPU properties are presented in two sets:
 * -# the commonly used properties in @ref base_gpu_props, which have been
 * unpacked from GPU register bitfields.
 * -# The full set of raw, unprocessed properties in @ref midg_raw_gpu_props
 * (also a member of @ref base_gpu_props). All of these are presented in
 * the packed form, as presented by the GPU  registers themselves.
 *
 * @usecase The raw properties in @ref midg_raw_gpu_props are necessary to
 * allow a user of the Mali Tools (e.g. PAT) to determine "Why is this device
 * behaving differently?". In this case, all information about the
 * configuration is potentially useful, but it <b>does not need to be processed
 * by the driver</b>. Instead, the raw registers can be processed by the Mali
 * Tools software on the host PC.
 *
 * The properties returned extend the Midgard Configuration Discovery
 * registers. For example, GPU clock speed is not specified in the Midgard
 * Architecture, but is <b>necessary for OpenCL's clGetDeviceInfo() function</b>.
 *
 * The GPU properties are obtained by a call to
 * _mali_base_get_gpu_props(). This simply returns a pointer to a const
 * base_gpu_props structure. It is constant for the life of a base
 * context. Multiple calls to _mali_base_get_gpu_props() to a base context
 * return the same pointer to a constant structure. This avoids cache pollution
 * of the common data.
 *
 * This pointer must not be freed, because it does not point to the start of a
 * region allocated by the memory allocator; instead, just close the @ref
 * base_context.
 *
 *
 * @section sec_base_user_api_gpuprops_config Platform Config Compile-time Properties
 * 
 * The Platform Config File sets up gpu properties that are specific to a
 * certain platform. Properties that are 'Implementation Defined' in the
 * Midgard Architecture spec are placed here.
 *
 * @note Reference configurations are provided for Midgard Implementations, such as
 * the Mali-T600 family. The customer need not repeat this information, and can select one of
 * these reference configurations. For example, VA_BITS, PA_BITS and the
 * maximum number of samples per pixel might vary between Midgard Implementations, but
 * \b not for platforms using the Mali-T604. This information is placed in
 * the reference configuration files.
 *
 * The System Integrator creates the following structure:
 * - platform_XYZ
 * - platform_XYZ/plat
 * - platform_XYZ/plat/plat_config.h
 * 
 * They then edit plat_config.h, using the example plat_config.h files as a
 * guide.
 *
 * At the very least, the customer must set @ref CONFIG_GPU_CORE_TYPE, and will
 * receive a helpful \#error message if they do not do this correctly. This
 * selects the Reference Configuration for the Midgard Implementation. The rationale
 * behind this decision (against asking the customer to write \#include
 * <gpus/mali_t600.h> in their plat_config.h) is as follows: 
 * - This mechanism 'looks' like a regular config file (such as Linux's
 * .config)
 * - It is difficult to get wrong in a way that will produce strange build
 * errors: 
 *  - They need not know where the mali_t600.h, other_midg_gpu.h etc. files are stored - and
 *  so they won't accidentally pick another file with 'mali_t600' in its name
 *  - When the build doesn't work, the System Integrator may think the DDK is
 *  doesn't work, and attempt to fix it themselves:
 *   - For the @ref CONFIG_GPU_CORE_TYPE mechanism, the only way to get past the
 *   error is to set @ref CONFIG_GPU_CORE_TYPE, and this is what the \#error tells
 *   you.
 *   - For a \#include mechanism, checks must still be made elsewhere, which the
 *   System Integrator may try working around by setting \#defines (such as
 *   VA_BITS) themselves in their plat_config.h. In the  worst case, they may
 *   set the prevention-mechanism \#define of
 *   "A_CORRECT_MIDGARD_CORE_WAS_CHOSEN".
 *   - In this case, they would believe they are on the right track, because
 *   the build progresses with their fix, but with errors elsewhere.
 *
 * However, there is nothing to prevent the customer using \#include to organize
 * their own configurations files hierarchically. 
 *
 * The mechanism for the header file processing is as follows:
 *
 * @dot
   digraph plat_config_mechanism {
	   rankdir=BT
	   size="6,6"

       "mali_base.h";
	   "midg/midg.h";

	   node [ shape=box ];
	   {
	       rank = same; ordering = out; 

		   "midg/midg_gpu_props.h";
		   "base/midg_gpus/mali_t600.h";
		   "base/midg_gpus/other_midg_gpu.h";
	   }
	   { rank = same; "plat/plat_config.h"; }
	   {
	       rank = same;
		   "midg/midg.h" [ shape=box ];
		   gpu_chooser [ label="" style="invisible" width=0 height=0 fixedsize=true ];
		   select_gpu [ label="Mali-T600 | Other\n(select_gpu.h)" shape=polygon,sides=4,distortion=0.25 width=3.3 height=0.99 fixedsize=true ] ;
	   }
	   node [ shape=box ];
	   { rank = same; "plat/plat_config.h"; }
	   { rank = same; "mali_base.h"; }

	   
	   
	   "mali_base.h" -> "midg/midg.h" -> "midg/midg_gpu_props.h";
	   "mali_base.h" -> "plat/plat_config.h" ;
	   "mali_base.h" -> select_gpu ;

	   "plat/plat_config.h" -> gpu_chooser [style="dotted,bold" dir=none weight=4] ;
	   gpu_chooser -> select_gpu [style="dotted,bold"] ;

	   select_gpu -> "base/midg_gpus/mali_t600.h" ;
	   select_gpu -> "base/midg_gpus/other_midg_gpu.h" ;
   }
   @enddot
 * 
 *
 * @section sec_base_user_api_gpuprops_kernel Kernel Operation
 *
 * During Base Context Create time, user-side makes a single kernel call:
 * - A call to fill user memory with GPU information structures
 *
 * The kernel-side will fill the provided the entire processed @ref base_gpu_props
 * structure, because this information is required in both
 * user and kernel side; it does not make sense to decode it twice.
 *
 * Coherency groups must be derived from the bitmasks, but this can be done
 * kernel side, and just once at kernel startup: Coherency groups must already
 * be known kernel-side, to support chains that specify a 'Only Coherent Group'
 * SW requirement, or 'Only Coherent Group with Tiler' SW requirement.
 *
 * @section sec_base_user_api_gpuprops_cocalc Coherency Group calculation
 * Creation of the coherent group data is done at device-driver startup, and so
 * is one-time. This will most likely involve a loop with CLZ, shifting, and
 * bit clearing on the L2_PRESENT or L3_PRESENT masks, depending on whether the
 * system is L2 or L2+L3 Coherent. The number of shader cores is done by a
 * population count, since faulty cores may be disabled during production,
 * producing a non-contiguous mask.
 *
 * The memory requirements for this algoirthm can be determined either by a u64
 * population count on the L2/L3_PRESENT masks (a LUT helper already is
 * requried for the above), or simple assumption that there can be no more than
 * 16 coherent groups, since core groups are typically 4 cores.
 */


/**
 * @addtogroup base_user_api_gpuprops User-side Base GPU Property Query APIs
 * @{
 */


/**
 * @addtogroup base_user_api_gpuprops_dyn Dynamic HW Properties
 * @{
 */


#define BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS 3

#define BASE_MAX_COHERENT_GROUPS 16

struct mali_base_gpu_core_props
{
	/**
	 * Product specific value.
	 */
	midg_product_id product_id;

	/**
	 * Status of the GPU release.
     * No defined values, but starts at 0 and increases by one for each release
     * status (alpha, beta, EAC, etc.).
     * 4 bit values (0-15).
	 */
	u16 version_status;

	/**
	 * Minor release number of the GPU. "P" part of an "RnPn" release number.
     * 8 bit values (0-255).
	 */
	u16 minor_revision;

	/**
	 * Major release number of the GPU. "R" part of an "RnPn" release number.
     * 4 bit values (0-15).
	 */
	u16 major_revision;

	/**
	 * @usecase GPU clock speed is not specified in the Midgard Architecture, but is
	 * <b>necessary for OpenCL's clGetDeviceInfo() function</b>.
	 */
	u32 gpu_speed_mhz;

	/**
	 * @usecase GPU clock max/min speed is required for computing best/worst case
	 * in tasks as job scheduling ant irq_throttling. (It is not specified in the
	 *  Midgard Architecture).
	 */
	u32 gpu_freq_khz_max;
	u32 gpu_freq_khz_min;

	/**
	 * Size of the shader program counter, in bits.
	 */
	u32 log2_program_counter_size;

	/**
	 * TEXTURE_FEATURES_x registers, as exposed by the GPU. This is a
	 * bitpattern where a set bit indicates that the format is supported.
	 *
	 * Before using a texture format, it is recommended that the corresponding
	 * bit be checked.
	 */
	u32 texture_features[BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS];

	/**
	 * Theoretical maximum memory available to the GPU. It is unlikely that a
	 * client will be able to allocate all of this memory for their own
	 * purposes, but this at least provides an upper bound on the memory
	 * available to the GPU.
	 *
	 * This is required for OpenCL's clGetDeviceInfo() call when
	 * CL_DEVICE_GLOBAL_MEM_SIZE is requested, for OpenCL GPU devices. The
	 * client will not be expecting to allocate anywhere near this value.
	 */
	u64 gpu_available_memory_size;

	/**
	 * @usecase Version string: For use by glGetString( GL_RENDERER ); (and similar
	 * for other APIs)
	 */
	const char * version_string;
};

/**
 *
 * More information is possible - but associativity and bus width are not
 * required by upper-level apis.

 */
struct mali_base_gpu_cache_props
{
	u32 log2_line_size;
	u32 log2_cache_size;
};

struct mali_base_gpu_tiler_props
{
	u32 bin_size_bytes; /* Max is 4*2^15 */
	u32 max_active_levels; /* Max is 2^15 */
};

/**
 * @brief descriptor for a coherent group
 * 
 * \c core_mask exposes all cores in that coherent group, and \c num_cores
 * provides a cached population-count for that mask.
 *
 * @note Whilst all cores are exposed in the mask, not all may be available to
 * the application, depending on the Kernel Job Scheduler policy. Therefore,
 * the application should not further restrict the core mask itself, as it may
 * result in an empty core mask. However, it can guarentee that there will be
 * at least one core available for each core group exposed .
 * 
 * @usecase Chains marked at certain user-side priorities (e.g. the Long-running
 * (batch) priority ) can be prevented from running on entire core groups by the
 * Kernel Chain Scheduler policy.
 *
 * @note if u64s must be 8-byte aligned, then this structure has 32-bits of wastage.
 */
struct mali_base_gpu_coherent_group
{
	u64 core_mask;         /**< Core restriction mask required for the group */
	u16 num_cores;         /**< Number of cores in the group */
};

/**
 * @brief Coherency group information
 *
 * Note that the sizes of the members could be reduced. However, the \c group
 * member might be 8-byte aligned to ensure the u64 core_mask is 8-byte
 * aligned, thus leading to wastage if the other members sizes were reduced.
 *
 * The groups are sorted by core mask. The core masks are non-repeating and do
 * not intersect.
 */
struct mali_base_gpu_coherent_group_info
{
	u32 num_groups;

	/**
	 * Number of core groups (coherent or not) in the GPU. Equivalent to the number of L2 Caches.
	 *
	 * The GPU Counter dumping writes 2048 bytes per core group, regardless of
	 * whether the core groups are coherent or not. Hence this member is needed
	 * to calculate how much memory is required for dumping.
	 *
	 * @note Do not use it to work out how many valid elements are in the
	 * group[] member. Use num_groups instead.
	 */
	u32 num_core_groups;

	/** 
	 * Coherency features of the memory, accessed by @ref midg_mem_features
	 * methods
	 */
	midg_mem_features coherency;

	/**
	 * Descriptors of coherent groups
	 */
	struct mali_base_gpu_coherent_group group[BASE_MAX_COHERENT_GROUPS];
};


/**
 * A complete description of the GPU's Hardware Configuration Discovery
 * registers.
 *
 * The information is presented inefficiently for access. For frequent access,
 * the values should be better expressed in an unpacked form in the
 * base_gpu_props structure.
 *
 * @usecase The raw properties in @ref midg_raw_gpu_props are necessary to
 * allow a user of the Mali Tools (e.g. PAT) to determine "Why is this device
 * behaving differently?". In this case, all information about the
 * configuration is potentially useful, but it <b>does not need to be processed
 * by the driver</b>. Instead, the raw registers can be processed by the Mali
 * Tools software on the host PC.
 *
 */
struct midg_raw_gpu_props
{
	u64 shader_present;
	u64 tiler_present;
	u64 l2_present;
	u64 l3_present;

	midg_cache_features l2_features;
	midg_cache_features l3_features;
	midg_mem_features mem_features;
	midg_mmu_features mmu_features;

	midg_as_present as_present;

	u32 js_present;
	midg_js_features js_features[MIDG_MAX_JOB_SLOTS];
	midg_tiler_features tiler_features;
};



/**
 * Return structure for _mali_base_get_gpu_props().
 *
 */
typedef struct mali_base_gpu_props
{
	struct mali_base_gpu_core_props core_props;
	struct mali_base_gpu_cache_props l2_props;
	struct mali_base_gpu_cache_props l3_props;
	struct mali_base_gpu_tiler_props tiler_props;

	/** This member is large, likely to be 128 bytes */
	struct midg_raw_gpu_props raw_props;

	/** This must be last member of the structure */
	struct mali_base_gpu_coherent_group_info coherency_info;
}base_gpu_props;

/** @} end group base_user_api_gpuprops_dyn */

/** @} end group base_user_api_gpuprops */

/** @} end group base_user_api */

/**
 * @addtogroup base_plat_config_gpuprops Base Platform Config GPU Properties
 * @{
 *
 * C Pre-processor macros are exposed here to do with Platform
 * Config.
 * 
 * These include:
 * - GPU Properties that are constant on a particular Midgard Family
 * Implementation e.g. Maximum samples per pixel on Mali-T600.
 * - General platform config for the GPU, such as the GPU major and minor
 * revison.
 */

/** @} end group base_plat_config_gpuprops */

/** @} end group base_api */

#endif /* _BASE_KERNEL_H_ */
