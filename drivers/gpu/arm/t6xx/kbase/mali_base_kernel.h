/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
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

/* For now we support the legacy API as well as the new API */
#define BASE_LEGACY_JD_API 1

#include <kbase/src/mali_base_mem_priv.h>

/*
 * Dependency stuff, keep it private for now. May want to expose it if
 * we decide to make the number of semaphores a configurable
 * option.
 */
#define BASE_JD_ATOM_COUNT              256

#define BASEP_JD_SEM_PER_WORD_LOG2      5
#define BASEP_JD_SEM_PER_WORD           (1 << BASEP_JD_SEM_PER_WORD_LOG2)
#define BASEP_JD_SEM_WORD_NR(x)         ((x) >> BASEP_JD_SEM_PER_WORD_LOG2)
#define BASEP_JD_SEM_MASK_IN_WORD(x)    (1 << ((x) & (BASEP_JD_SEM_PER_WORD - 1)))
#define BASEP_JD_SEM_ARRAY_SIZE         BASEP_JD_SEM_WORD_NR(BASE_JD_ATOM_COUNT)

#if BASE_LEGACY_JD_API
/* Size of the ring buffer */
#define BASEP_JCTX_RB_NRPAGES           4
#endif /* BASE_LEGACY_JD_API */

#define BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS 3

#define BASE_MAX_COHERENT_GROUPS 16

#if defined CDBG_ASSERT
#define LOCAL_ASSERT CDBG_ASSERT
#elif defined OSK_ASSERT
#define LOCAL_ASSERT OSK_ASSERT
#else
#error assert macro not defined!
#endif

#if defined OSK_PAGE_MASK
	#define LOCAL_PAGE_LSB ~OSK_PAGE_MASK
#else
	#include <osu/mali_osu.h>

	#if defined CONFIG_CPU_PAGE_SIZE_LOG2
		#define LOCAL_PAGE_LSB ((1ul << CONFIG_CPU_PAGE_SIZE_LOG2) - 1)
	#else
		#error Failed to find page size
	#endif
#endif

/** 32/64-bit neutral way to represent pointers */
typedef union kbase_pointer
{
	void * value;     /**< client should store their pointers here */
	u32 compat_value; /**< 64-bit kernels should fetch value here when handling 32-bit clients */
	u64 sizer;        /**< Force 64-bit storage for all clients regardless */
} kbase_pointer;

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
	BASE_MEM_CACHED_GPU  =      (1U << 5), /**< Should be cached */
	BASE_MEM_CACHED_CPU  =      (1U << 6), /**< Should be cached */

	BASEP_MEM_GROWABLE   =      (1U << 7), /**< Growable memory. This is a private flag that is set automatically. Not valid for PMEM. */
	BASE_MEM_GROW_ON_GPF =      (1U << 8), /**< Grow backing store on GPU Page Fault */

	BASE_MEM_COHERENT_SYSTEM =  (1U << 9), /**< Page coherence Outer shareable */
	BASE_MEM_COHERENT_LOCAL =   (1U << 10), /**< Page coherence Inner shareable */
	BASE_MEM_DONT_ZERO_INIT =   (1U << 11) /**< Optimization: No need to zero initialize */
};

/**
 * @brief Memory types supported by @a base_tmem_import
 *
 * Each type defines what the supported handle type is.
 *
 * If any new type is added here ARM must be contacted
 * to allocate a numeric value for it.
 * Do not just add a new type without synchronizing with ARM
 * as future releases from ARM might include other new types
 * which could clash with your custom types.
 */
typedef enum base_tmem_import_type
{
	BASE_TMEM_IMPORT_TYPE_INVALID = 0,
	/** UMP import. Handle type is ump_secure_id. */
	BASE_TMEM_IMPORT_TYPE_UMP = 1,
	/** UMM import. Handle type is a file descriptor (int) */
	BASE_TMEM_IMPORT_TYPE_UMM = 2
} base_tmem_import_type;

/**
 * Bits we can tag into a memory handle.
 * We use the lower 12 bits as our handles are page-multiples, thus not using the 12 LSBs
 */
enum
{
	BASE_MEM_TAGS_MASK      = ((1U << 12) - 1),   /**< Mask to get hold of the tag bits/see if there are tag bits */
	BASE_MEM_TAG_IMPORTED  =   (1U << 0)          /**< Tagged as imported */
	/* max 1u << 11 supported */
};


/**
 * @brief Number of bits used as flags for base memory management
 *
 * Must be kept in sync with the ::base_mem_alloc_flags flags
 */
#define BASE_MEM_FLAGS_NR_BITS  12

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

/**
 * Handle to represent imported memory object.
 * Simple opague handle to imported memory, can't be used
 * with anything but base_external_resource_init to bind to an atom.
 */
typedef struct base_import_handle
{
	struct
	{
		mali_addr64 handle;
	} basep;
} base_import_handle;


/** @} end group base_user_api_memory */

/**
 * @addtogroup base_user_api_job_dispatch User-side Base Job Dispatcher APIs
 * @{
 */

typedef int platform_fence_type;
#define INVALID_PLATFORM_FENCE ((platform_fence_type)-1)

/**
 * Base stream handle.
 *
 * References an underlying base stream object.
 */
typedef struct base_stream
{
	struct
	{
		int fd;
	}
	basep;
} base_stream;

/**
 * Base fence handle.
 *
 * References an underlying base fence object.
 */
typedef struct base_fence
{
	struct
	{
		int fd;
		int stream_fd;
	}
	basep;
} base_fence;

#if BASE_LEGACY_JD_API
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
#endif /* BASE_LEGACY_JD_API */

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
	u64     blob[2]; /**< per-job data array */
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
/**
 * Requires compute shaders
 * This covers any of the following Midgard Job types:
 * - Vertex Shader Job
 * - Geometry Shader Job
 * - An actual Compute Shader Job
 *
 * Compare this with @ref BASE_JD_REQ_ONLY_COMPUTE, which specifies that the
 * job is specifically just the "Compute Shader" job type, and not the "Vertex
 * Shader" nor the "Geometry Shader" job type.
 */
#define BASE_JD_REQ_CS  (1U << 1)
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

#define BASE_JD_REQ_PERMON               (1U << 7)

/**
 * SW Only requirement: External resources are referenced by this atom.
 * When external resources are referenced no syncsets can be bundled with the atom
 * but should instead be part of a NULL jobs inserted into the dependency tree.
 * The first pre_dep object must be configured for the external resouces to use,
 * the second pre_dep object can be used to create other dependencies.
 */
#define BASE_JD_REQ_EXTERNAL_RESOURCES   (1U << 8)

/**
 * SW Only requirement: Software defined job. Jobs with this bit set will not be submitted
 * to the hardware but will cause some action to happen within the driver
 */
#define BASE_JD_REQ_SOFT_JOB        (1U << 9)

#define BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME      (BASE_JD_REQ_SOFT_JOB | 0x1)
#define BASE_JD_REQ_SOFT_FENCE_TRIGGER          (BASE_JD_REQ_SOFT_JOB | 0x2)
#define BASE_JD_REQ_SOFT_FENCE_WAIT             (BASE_JD_REQ_SOFT_JOB | 0x3)


/**
 * HW Requirement: Requires Compute shaders (but not Vertex or Geometry Shaders)
 *
 * This indicates that the Job Chain contains Midgard Jobs of the 'Compute Shaders' type.
 *
 * In contrast to @ref BASE_JD_REQ_CS, this does \b not indicate that the Job
 * Chain contains 'Geometry Shader' or 'Vertex Shader' jobs.
 *
 * @note This is a more flexible variant of the @ref BASE_CONTEXT_HINT_ONLY_COMPUTE flag,
 * allowing specific jobs to be marked as 'Only Compute' instead of the entire context
 */
#define BASE_JD_REQ_ONLY_COMPUTE    (1U << 10)

/**
 * HW Requirement: Use the base_jd_atom::device_nr field to specify a
 * particular core group
 *
 * If both BASE_JD_REQ_COHERENT_GROUP and this flag are set, this flag takes priority
 *
 * This is only guaranteed to work for BASE_JD_REQ_ONLY_COMPUTE atoms.
 */
#define BASE_JD_REQ_SPECIFIC_COHERENT_GROUP ( 1U << 11 )

/**
 * SW Flag: If this bit is set then the successful completion of this atom
 * will not cause an event to be sent to userspace
 */
#define BASE_JD_REQ_EVENT_ONLY_ON_FAILURE   ( 1U << 12 )

/**
* These requirement bits are currently unused in base_jd_core_req (currently a u16)
*/

#define BASEP_JD_REQ_RESERVED_BIT13 ( 1U << 13 )
#define BASEP_JD_REQ_RESERVED_BIT14 ( 1U << 14 )
#define BASEP_JD_REQ_RESERVED_BIT15 ( 1U << 15 )

/**
* Mask of all the currently unused requirement bits in base_jd_core_req.
*/

#define BASEP_JD_REQ_RESERVED ( BASEP_JD_REQ_RESERVED_BIT13 |\
                                BASEP_JD_REQ_RESERVED_BIT14 | BASEP_JD_REQ_RESERVED_BIT15 )

/**
 * Mask of all bits in base_jd_core_req that control the type of the atom.
 *
 * This allows dependency only atoms to have flags set
 */
#define BASEP_JD_REQ_ATOM_TYPE ( ~(BASEP_JD_REQ_RESERVED | BASE_JD_REQ_EVENT_ONLY_ON_FAILURE |\
                                   BASE_JD_REQ_EXTERNAL_RESOURCES ) )

#if BASE_LEGACY_JD_API
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
	mali_addr64         jc;             /**< job-chain GPU address */
	base_jd_udata       udata;          /**< user data */
	base_jd_dep         pre_dep;        /**< pre-dependencies */
	base_jd_dep         post_dep;       /**< post-dependencies */
	base_jd_core_req    core_req;       /**< core requirements */
	u16                 nr_syncsets;    /**< nr of syncsets following the atom */
	u16                 nr_extres;      /**< nr of external resources following the atom */

	/** @brief Relative priority.
	 *
	 * A positive value requests a lower priority, whilst a negative value
	 * requests a higher priority. Only privileged processes may request a
	 * higher priority. For unprivileged processes, a negative priority will
	 * be interpreted as zero.
	 */
	s8                  prio;

	/**
	 * @brief Device number to use, depending on @ref base_jd_core_req flags set.
	 *
	 * When BASE_JD_REQ_SPECIFIC_COHERENT_GROUP is set, a 'device' is one of
	 * the coherent core groups, and so this targets a particular coherent
	 * core-group. They are numbered from 0 to (mali_base_gpu_coherent_group_info::num_groups - 1),
	 * and the cores targeted by this device_nr will usually be those specified by
	 * (mali_base_gpu_coherent_group_info::group[device_nr].core_mask).
	 * Further, two atoms from different processes using the same \a device_nr
	 * at the same time will always target the same coherent core-group.
	 *
	 * There are exceptions to when the device_nr is ignored:
	 * - when any process in the system uses a BASE_JD_REQ_CS or
	 * BASE_JD_REQ_ONLY_COMPUTE atom that can run on all cores across all
	 * coherency groups (i.e. also does \b not have the
	 * BASE_JD_REQ_COHERENT_GROUP or BASE_JD_REQ_SPECIFIC_COHERENT_GROUP flags
	 * set). In this case, such atoms would block device_nr==1 being used due
	 * to restrictions on affinity, perhaps indefinitely. To ensure progress is
	 * made, the atoms targeted for device_nr 1 will instead be redirected to
	 * device_nr 0
	 * - When any process in the system is using 'NSS' (BASE_JD_REQ_NSS) atoms,
	 * because there'd be very high latency on atoms targeting a coregroup
	 * that is also in use by NSS atoms. To ensure progress is
	 * made, the atoms targeted for device_nr 1 will instead be redirected to
	 * device_nr 0
	 * - During certain HW workarounds, such as BASE_HW_ISSUE_8987, where
	 * BASE_JD_REQ_ONLY_COMPUTE atoms must not use the same cores as other
	 * atoms. In this case, all atoms are targeted to device_nr == min( num_groups, 1 )
	 *
	 * Note that the 'device' number for a coherent coregroup cannot exceed
	 * (BASE_MAX_COHERENT_GROUPS - 1).
	 */
	u8                  device_nr;
} base_jd_atom;
#endif /* BASE_LEGACY_JD_API */

typedef u8 base_atom_id; /**< Type big enough to store an atom number in */

typedef struct base_jd_atom_v2
{
	mali_addr64         jc;             /**< job-chain GPU address */
	base_jd_core_req    core_req;       /**< core requirements */
	base_jd_udata       udata;          /**< user data */
	kbase_pointer       extres_list;    /**< list of external resources */
	u16                 nr_extres;      /**< nr of external resources */
	base_atom_id        pre_dep[2];     /**< pre-dependencies */
	base_atom_id        atom_number;    /**< unique number to identify the atom */
	s8                  prio;           /**< priority - smaller is higher priority */
	u8                  device_nr;      /**< coregroup when BASE_JD_REQ_SPECIFIC_COHERENT_GROUP specified */
} base_jd_atom_v2;

#if BASE_LEGACY_JD_API
/* Structure definition works around the fact that C89 doesn't allow arrays of size 0 */
typedef struct basep_jd_atom_ss
{
	base_jd_atom    atom;
	base_syncset    syncsets[1];
} basep_jd_atom_ss;
#endif /* BASE_LEGACY_JD_API */

typedef enum base_external_resource_access
{
	BASE_EXT_RES_ACCESS_SHARED,
	BASE_EXT_RES_ACCESS_EXCLUSIVE
} base_external_resource_access;

typedef struct base_external_resource
{
	u64  ext_resource;
} base_external_resource;

#if BASE_LEGACY_JD_API
/* Structure definition works around the fact that C89 doesn't allow arrays of size 0 */
typedef struct basep_jd_atom_ext_res
{
	base_jd_atom  atom;
	base_external_resource resources[1];
} basep_jd_atom_ext_res;

static INLINE size_t base_jd_atom_size_ex(u32 syncset_count, u32 external_res_count)
{
	int size;

	LOCAL_ASSERT( 0 == syncset_count || 0 == external_res_count );

	size = syncset_count      ? offsetof(basep_jd_atom_ss, syncsets[0]) + (sizeof(base_syncset) * syncset_count) :
	       external_res_count ? offsetof(basep_jd_atom_ext_res, resources[0]) + (sizeof(base_external_resource) * external_res_count) :
	                            sizeof(base_jd_atom);

	/* Atom minimum size set to 64 bytes to ensure that the maximum
	 * number of atoms in the ring buffer is limited to 256 */
	return MAX(64, size);
}

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
	return base_jd_atom_size_ex(nr, 0);
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
static INLINE base_syncset *base_jd_get_atom_syncset(base_jd_atom *atom, u16 n)
{
	LOCAL_ASSERT(atom != NULL);
	LOCAL_ASSERT(0 == (atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES));
	LOCAL_ASSERT(n <= atom->nr_syncsets);
	return &((basep_jd_atom_ss *)atom)->syncsets[n];
}
#endif /* BASE_LEGACY_JD_API */

/**
 * @brief Soft-atom fence trigger setup.
 *
 * Sets up an atom to be a SW-only atom signaling a fence
 * when it reaches the run state.
 *
 * Using the existing base dependency system the fence can
 * be set to trigger when a GPU job has finished.
 *
 * The base fence object must not be terminated until the atom
 * has been submitted to @a base_jd_submit_bag and @a base_jd_submit_bag has returned.
 *
 * @a fence must be a valid fence set up with @a base_fence_init.
 * Calling this function with a uninitialized fence results in undefined behavior.
 *
 * @param[out] atom A pre-allocated atom to configure as a fence trigger SW atom
 * @param[in] fence The base fence object to trigger.
 */
static INLINE void base_jd_fence_trigger_setup(base_jd_atom * atom, base_fence * fence)
{
	LOCAL_ASSERT(atom);
	LOCAL_ASSERT(fence);
	LOCAL_ASSERT(fence->basep.fd == INVALID_PLATFORM_FENCE);
	LOCAL_ASSERT(fence->basep.stream_fd >= 0);
	atom->jc = (uintptr_t)fence;
	atom->core_req = BASE_JD_REQ_SOFT_FENCE_TRIGGER;
}

static INLINE void base_jd_fence_trigger_setup_v2(base_jd_atom_v2 * atom, base_fence * fence)
{
	LOCAL_ASSERT(atom);
	LOCAL_ASSERT(fence);
	LOCAL_ASSERT(fence->basep.fd == INVALID_PLATFORM_FENCE);
	LOCAL_ASSERT(fence->basep.stream_fd >= 0);
	atom->jc = (uintptr_t)fence;
	atom->core_req = BASE_JD_REQ_SOFT_FENCE_TRIGGER;
}

/**
 * @brief Soft-atom fence wait setup.
 *
 * Sets up an atom to be a SW-only atom waiting on a fence.
 * When the fence becomes triggered the atom becomes runnable
 * and completes immediately.
 *
 * Using the existing base dependency system the fence can
 * be set to block a GPU job until it has been triggered.
 *
 * The base fence object must not be terminated until the atom
 * has been submitted to @a base_jd_submit_bag and @a base_jd_submit_bag has returned.
 *
 * @a fence must be a valid fence set up with @a base_fence_init or @a base_fence_import.
 * Calling this function with a uninitialized fence results in undefined behavior.
 *
 * @param[out] atom A pre-allocated atom to configure as a fence wait SW atom
 * @param[in] fence The base fence object to wait on
 */
static INLINE void base_jd_fence_wait_setup(base_jd_atom * atom, base_fence * fence)
{
	LOCAL_ASSERT(atom);
	LOCAL_ASSERT(fence);
	LOCAL_ASSERT(fence->basep.fd >= 0);
	atom->jc = (uintptr_t)fence;
	atom->core_req = BASE_JD_REQ_SOFT_FENCE_WAIT;
}

static INLINE void base_jd_fence_wait_setup_v2(base_jd_atom_v2 * atom, base_fence * fence)
{
	LOCAL_ASSERT(atom);
	LOCAL_ASSERT(fence);
	LOCAL_ASSERT(fence->basep.fd >= 0);
	atom->jc = (uintptr_t)fence;
	atom->core_req = BASE_JD_REQ_SOFT_FENCE_WAIT;
}

#if BASE_LEGACY_JD_API
/**
 * @brief Atom external resource accessor
 *
 * This functions returns a pointer to the nth external resource tracked by the atom.
 *
 * @param[in] atom The allocated atom
 * @param     n    The number of the external resource to return a pointer to
 * @return a pointer to the nth external resource
 */
static INLINE base_external_resource *base_jd_get_external_resource(base_jd_atom *atom, u16 n)
{
	LOCAL_ASSERT(atom != NULL);
	LOCAL_ASSERT(BASE_JD_REQ_EXTERNAL_RESOURCES == (atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES));
	LOCAL_ASSERT(n <= atom->nr_extres);
	return &((basep_jd_atom_ext_res*)atom)->resources[n];
}
#endif /* BASE_LEGACY_JD_API */

/**
 * @brief External resource info initialization.
 *
 * Sets up a external resource object to reference
 * a memory allocation and the type of access requested.
 *
 * @param[in] res     The resource object to initialize
 * @param     handle  The handle to the imported memory object
 * @param     access  The type of access requested
 */
static INLINE void base_external_resource_init(base_external_resource * res, base_import_handle handle, base_external_resource_access access)
{
	mali_addr64 address;
	address = handle.basep.handle;

	LOCAL_ASSERT(res != NULL);
	LOCAL_ASSERT(0 == (address & LOCAL_PAGE_LSB));
	LOCAL_ASSERT(access == BASE_EXT_RES_ACCESS_SHARED || access == BASE_EXT_RES_ACCESS_EXCLUSIVE);

	res->ext_resource = address | (access & LOCAL_PAGE_LSB);
}

#if BASE_LEGACY_JD_API
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
	LOCAL_ASSERT(atom != NULL);
	return (atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES) ? (base_jd_atom *)base_jd_get_external_resource(atom, atom->nr_extres) :
	                                                           (base_jd_atom *)base_jd_get_atom_syncset(atom, atom->nr_syncsets);
}
#endif /* BASE_LEGACY_JD_API */

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
	BASE_JD_EVENT_JOB_INVALID       = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x003,

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
#if BASE_LEGACY_JD_API
typedef struct base_jd_event
{
	base_jd_event_code      event_code; /**< event code */
	void                  * data;       /**< event specific data */
} base_jd_event;
#endif

typedef struct base_jd_event_v2
{
	base_jd_event_code      event_code; /**< event code */
	base_atom_id            atom_number;/**< the atom number that has completed */
	base_jd_udata           udata;      /**< user data */
} base_jd_event_v2;

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

	u32 gpu_id;
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

/**
 * @addtogroup base_user_api_core User-side Base core APIs
 * @{
 */

/**
 * \enum base_context_create_flags
 *
 * Flags to pass to ::base_context_init.
 * Flags can be ORed together to enable multiple things.
 *
 * These share the same space as @ref basep_context_private_flags, and so must
 * not collide with them.
 */
enum base_context_create_flags
{
	/** No flags set */
	BASE_CONTEXT_CREATE_FLAG_NONE               = 0,

	/** Base context is embedded in a cctx object (flag used for CINSTR software counter macros) */
	BASE_CONTEXT_CCTX_EMBEDDED                  = (1u << 0),

	/** Base context is a 'System Monitor' context for Hardware counters.
	 *
	 * One important side effect of this is that job submission is disabled. */
	BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED = (1u << 1),

	/** Base context flag indicating a 'hint' that this context uses Compute
	 * Jobs only.
	 *
	 * Specifially, this means that it only sends atoms that <b>do not</b>
	 * contain the following @ref base_jd_core_req :
	 * - BASE_JD_REQ_FS
	 * - BASE_JD_REQ_T
	 *
	 * Violation of these requirements will cause the Job-Chains to be rejected.
	 *
	 * In addition, it is inadvisable for the atom's Job-Chains to contain Jobs
	 * of the following @ref midg_job_type (whilst it may work now, it may not
	 * work in future) :
	 * - @ref MIDG_JOB_VERTEX
	 * - @ref MIDG_JOB_GEOMETRY
	 *
	 * @note An alternative to using this is to specify the BASE_JD_REQ_ONLY_COMPUTE
	 * requirement in atoms.
	 */
	BASE_CONTEXT_HINT_ONLY_COMPUTE              = (1u << 2)
};

/**
 * Bitpattern describing the ::base_context_create_flags that can be passed to base_context_init()
 */
#define BASE_CONTEXT_CREATE_ALLOWED_FLAGS \
	( ((u32)BASE_CONTEXT_CCTX_EMBEDDED) | \
	  ((u32)BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED) | \
	  ((u32)BASE_CONTEXT_HINT_ONLY_COMPUTE) )

/**
 * Bitpattern describing the ::base_context_create_flags that can be passed to the kernel
 */
#define BASE_CONTEXT_CREATE_KERNEL_FLAGS \
	( ((u32)BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED) | \
	  ((u32)BASE_CONTEXT_HINT_ONLY_COMPUTE) )


/**
 * Private flags used on the base context
 *
 * These start at bit 31, and run down to zero.
 *
 * They share the same space as @ref base_context_create_flags, and so must
 * not collide with them.
 */
enum basep_context_private_flags
{
	/** Private flag tracking whether job descriptor dumping is disabled */
	BASEP_CONTEXT_FLAG_JOB_DUMP_DISABLED = (1 << 31)
};

/** @} end group base_user_api_core */

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

/**
 * @addtogroup base_api Base APIs
 * @{
 */
/**
 * @addtogroup basecpuprops Base CPU Properties
 * @{
 */

/**
 * @brief CPU Property Flag for base_cpu_props::cpu_flags, indicating a
 * Little Endian System. If not set in base_cpu_props::cpu_flags, then the
 * system is Big Endian.
 *
 * The compile-time equivalent is @ref CONFIG_CPU_LITTLE_ENDIAN.
 */
#define BASE_CPU_PROPERTY_FLAG_LITTLE_ENDIAN F_BIT_0

/** @brief Platform Dynamic CPU properties structure */
typedef struct base_cpu_props {
    u32 nr_cores;            /**< Number of CPU cores */

    /**
     * CPU page size as a Logarithm to Base 2. The compile-time
     * equivalent is @ref CONFIG_CPU_PAGE_SIZE_LOG2
     */
    u32 cpu_page_size_log2;

    /**
     * CPU L1 Data cache line size as a Logarithm to Base 2. The compile-time
     * equivalent is @ref CONFIG_CPU_L1_DCACHE_LINE_SIZE_LOG2.
     */
    u32 cpu_l1_dcache_line_size_log2;

    /**
     * CPU L1 Data cache size, in bytes. The compile-time equivalient is
     * @ref CONFIG_CPU_L1_DCACHE_SIZE.
     *
     * This CPU Property is mainly provided to implement OpenCL's
     * clGetDeviceInfo(), which allows the CL_DEVICE_GLOBAL_MEM_CACHE_SIZE
     * hint to be queried.
     */
    u32 cpu_l1_dcache_size;

    /**
     * CPU Property Flags bitpattern.
     *
     * This is a combination of bits as specified by the macros prefixed with
     * 'BASE_CPU_PROPERTY_FLAG_'.
     */
    u32 cpu_flags;

    /**
     * Maximum clock speed in MHz.
     * @usecase 'Maximum' CPU Clock Speed information is required by OpenCL's
     * clGetDeviceInfo() function for the CL_DEVICE_MAX_CLOCK_FREQUENCY hint.
     */
    u32 max_cpu_clock_speed_mhz;

    /**
     * @brief Total memory, in bytes.
     *
     * This is the theoretical maximum memory available to the CPU. It is
     * unlikely that a client will be able to allocate all of this memory for
     * their own purposes, but this at least provides an upper bound on the
     * memory available to the CPU.
     *
     * This is required for OpenCL's clGetDeviceInfo() call when
     * CL_DEVICE_GLOBAL_MEM_SIZE is requested, for OpenCL CPU devices.
     */
    u64 available_memory_size;
} base_cpu_props;
/** @} end group basecpuprops */

/** @} end group base_api */

#endif /* _BASE_KERNEL_H_ */
