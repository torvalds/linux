/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_subsystem.h"
#include "mali_kernel_mem.h"
#include "mali_kernel_ioctl.h"
#include "mali_kernel_descriptor_mapping.h"
#include "mali_kernel_mem_mmu.h"
#include "mali_kernel_memory_engine.h"
#include "mali_block_allocator.h"
#include "mali_kernel_mem_os.h"
#include "mali_kernel_session_manager.h"
#include "mali_kernel_core.h"
#include "mali_kernel_rendercore.h"

#if defined USING_MALI400_L2_CACHE
#include "mali_kernel_l2_cache.h"
#endif

#if MALI_USE_UNIFIED_MEMORY_PROVIDER != 0
#include "ump_kernel_interface.h"
#endif

/* kernel side OS functions and user-kernel interface */
#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_ukk.h"
#include "mali_osk_bitops.h"
#include "mali_osk_list.h"

/**
 * Size of the MMU registers in bytes
 */
#define MALI_MMU_REGISTERS_SIZE 0x24

/**
 * Size of an MMU page in bytes
 */
#define MALI_MMU_PAGE_SIZE 0x1000

/**
 * Page directory index from address
 * Calculates the page directory index from the given address
 */
#define MALI_MMU_PDE_ENTRY(address) (((address)>>22) & 0x03FF)

/**
 * Page table index from address
 * Calculates the page table index from the given address
 */
#define MALI_MMU_PTE_ENTRY(address) (((address)>>12) & 0x03FF)

/**
 * Extract the memory address from an PDE/PTE entry
 */
#define MALI_MMU_ENTRY_ADDRESS(value) ((value) & 0xFFFFFC00)

/**
 * Calculate memory address from PDE and PTE
 */
#define MALI_MMU_ADDRESS(pde, pte) (((pde)<<22) | ((pte)<<12))

/**
 * Linux kernel version has marked SA_SHIRQ as deprecated, IRQF_SHARED should be used.
 * This is to handle older kernels which haven't done this swap.
 */
#ifndef IRQF_SHARED
#define IRQF_SHARED SA_SHIRQ
#endif /* IRQF_SHARED */

/**
 * Per-session memory descriptor mapping table sizes
 */
#define MALI_MEM_DESCRIPTORS_INIT 64
#define MALI_MEM_DESCRIPTORS_MAX 65536

/**
 * Used to disallow more than one core to run a MMU at the same time
 *
 * @note This value is hardwired into some systems' configuration files,
 * which \em might not be a header file (e.g. some external data configuration
 * file). Therefore, if this value is modified, its occurance must be
 * \b manually checked for in the entire driver source tree.
 */
#define MALI_MMU_DISALLOW_PARALLELL_WORK_OF_MALI_CORES 1

#define MALI_INVALID_PAGE ((u32)(~0))

/**
 *
 */
typedef enum mali_mmu_entry_flags
{
	MALI_MMU_FLAGS_PRESENT = 0x01,
	MALI_MMU_FLAGS_READ_PERMISSION = 0x02,
	MALI_MMU_FLAGS_WRITE_PERMISSION = 0x04,
	MALI_MMU_FLAGS_MASK = 0x07
} mali_mmu_entry_flags;

/**
 * MMU register numbers
 * Used in the register read/write routines.
 * See the hardware documentation for more information about each register
 */
typedef enum mali_mmu_register {
	MALI_MMU_REGISTER_DTE_ADDR = 0x0000, /**< Current Page Directory Pointer */
	MALI_MMU_REGISTER_STATUS = 0x0001, /**< Status of the MMU */
	MALI_MMU_REGISTER_COMMAND = 0x0002, /**< Command register, used to control the MMU */
	MALI_MMU_REGISTER_PAGE_FAULT_ADDR = 0x0003, /**< Logical address of the last page fault */
	MALI_MMU_REGISTER_ZAP_ONE_LINE = 0x004, /**< Used to invalidate the mapping of a single page from the MMU */
	MALI_MMU_REGISTER_INT_RAWSTAT = 0x0005, /**< Raw interrupt status, all interrupts visible */
	MALI_MMU_REGISTER_INT_CLEAR = 0x0006, /**< Indicate to the MMU that the interrupt has been received */
	MALI_MMU_REGISTER_INT_MASK = 0x0007, /**< Enable/disable types of interrupts */
	MALI_MMU_REGISTER_INT_STATUS = 0x0008 /**< Interrupt status based on the mask */
} mali_mmu_register;

/**
 * MMU interrupt register bits
 * Each cause of the interrupt is reported
 * through the (raw) interrupt status registers.
 * Multiple interrupts can be pending, so multiple bits
 * can be set at once.
 */
typedef enum mali_mmu_interrupt
{
	MALI_MMU_INTERRUPT_PAGE_FAULT = 0x01, /**< A page fault occured */
	MALI_MMU_INTERRUPT_READ_BUS_ERROR = 0x02 /**< A bus read error occured */
} mali_mmu_interrupt;

/**
 * MMU commands
 * These are the commands that can be sent
 * to the MMU unit.
 */
typedef enum mali_mmu_command
{
	MALI_MMU_COMMAND_ENABLE_PAGING = 0x00, /**< Enable paging (memory translation) */
	MALI_MMU_COMMAND_DISABLE_PAGING = 0x01, /**< Disable paging (memory translation) */
	MALI_MMU_COMMAND_ENABLE_STALL = 0x02, /**<  Enable stall on page fault */
	MALI_MMU_COMMAND_DISABLE_STALL = 0x03, /**< Disable stall on page fault */
	MALI_MMU_COMMAND_ZAP_CACHE = 0x04, /**< Zap the entire page table cache */
	MALI_MMU_COMMAND_PAGE_FAULT_DONE = 0x05, /**< Page fault processed */
	MALI_MMU_COMMAND_SOFT_RESET = 0x06 /**< Reset the MMU back to power-on settings */
} mali_mmu_command;

typedef enum mali_mmu_status_bits
{
	MALI_MMU_STATUS_BIT_PAGING_ENABLED      = 1 << 0,
	MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE   = 1 << 1,
	MALI_MMU_STATUS_BIT_STALL_ACTIVE        = 1 << 2,
	MALI_MMU_STATUS_BIT_IDLE                = 1 << 3,
	MALI_MMU_STATUS_BIT_REPLAY_BUFFER_EMPTY = 1 << 4,
	MALI_MMU_STATUS_BIT_PAGE_FAULT_IS_WRITE = 1 << 5,
} mali_mmu_status_bits;

/**
 * Defintion of the type used to represent memory used by a session.
 * Containts the pointer to the huge user space virtual memory area
 * used to access the Mali memory.
 */
typedef struct memory_session
{
	_mali_osk_lock_t *lock; /**< Lock protecting the vm manipulation */

	u32 mali_base_address; /**< Mali virtual memory area used by this session */
	mali_descriptor_mapping * descriptor_mapping; /**< Mapping between userspace descriptors and our pointers */

	u32 page_directory; /**< Physical address of the memory session's page directory */

	mali_io_address page_directory_mapped; /**< Pointer to the mapped version of the page directory into the kernel's address space */
	mali_io_address page_entries_mapped[1024]; /**< Pointers to the page tables which exists in the page directory mapped into the kernel's address space */
	u32   page_entries_usage_count[1024]; /**< Tracks usage count of the page table pages, so they can be releases on the last reference */

	_mali_osk_list_t active_mmus; /**< The MMUs in this session, in increasing order of ID (so we can lock them in the correct order when necessary) */
	_mali_osk_list_t memory_head; /**< Track all the memory allocated in this session, for freeing on abnormal termination */
} memory_session;

typedef struct mali_kernel_memory_mmu_idle_callback
{
	_mali_osk_list_t link;
	void (*callback)(void*);
	void * callback_argument;
} mali_kernel_memory_mmu_idle_callback;

/**
 * Definition of the MMU struct
 * Used to track a MMU unit in the system.
 * Contains information about the mapping of the registers
 */
typedef struct mali_kernel_memory_mmu
{
	int id; /**< ID of the MMU, no duplicate IDs may exist on the system */
	const char * description; /**< Description text received from the resource manager to help identify the resource for people */
	int irq_nr; /**< IRQ number */
	u32 base; /**< Physical address of the registers */
	mali_io_address mapped_registers; /**< Virtual mapping of the registers */
	u32 mapping_size; /**< Size of registers in bytes */
	_mali_osk_list_t list; /**< Used to link multiple MMU's into a list */
	_mali_osk_irq_t *irq;
	u32 flags; /**< Used to store if there is something special with this mmu. */

	_mali_osk_lock_t *lock; /**< Lock protecting access to the usage fields */
	/* usage fields */
	memory_session * active_session; /**< Active session, NULL if no session is active */
	u32 usage_count; /**< Number of nested activations of the active session */
	_mali_osk_list_t callbacks; /**< Callback registered for MMU idle notification */
	void *core;

	int in_page_fault_handler;

	_mali_osk_list_t session_link;
} mali_kernel_memory_mmu;

typedef struct dedicated_memory_info
{
	u32 base;
	u32 size;
	struct dedicated_memory_info * next;
} dedicated_memory_info;

/* types used for external_memory and ump_memory physical memory allocators, which are using the mali_allocation_engine */
#if MALI_USE_UNIFIED_MEMORY_PROVIDER != 0
typedef struct ump_mem_allocation
{
	mali_allocation_engine * engine;
	mali_memory_allocation * descriptor;
	u32 initial_offset;
	u32 size_allocated;
	ump_dd_handle ump_mem;
} ump_mem_allocation ;
#endif

typedef struct external_mem_allocation
{
	mali_allocation_engine * engine;
	mali_memory_allocation * descriptor;
	u32 initial_offset;
	u32 size;
} external_mem_allocation;

/*
   Subsystem interface implementation
*/
/**
 * Fixed block memory subsystem startup function.
 * Called by the driver core when the driver is loaded.
 * Registers the memory systems ioctl handler, resource handlers and memory map function with the core.
 *
 * @param id Identifier assigned by the core to the memory subsystem
 * @return 0 on success, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_initialize(mali_kernel_subsystem_identifier id);

/**
 * Fixed block memory subsystem shutdown function.
 * Called by the driver core when the driver is unloaded.
 * Cleans up
 * @param id Identifier assigned by the core to the memory subsystem
 */
static void mali_memory_core_terminate(mali_kernel_subsystem_identifier id);

/**
 * MMU Memory load complete notification function.
 * Called by the driver core when all drivers have loaded and all resources has been registered
 * Builds the memory overall memory list
 * @param id Identifier assigned by the core to the memory subsystem
 * @return 0 on success, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_load_complete(mali_kernel_subsystem_identifier id);

/**
 * Fixed block memory subsystem session begin notification
 * Called by the core when a new session to the driver is started.
 * Creates a memory session object and sets it as the subsystem slot data for this session
 * @param slot Pointer to the slot to use for storing per-session data
 * @return 0 on success, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_session_begin(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot, _mali_osk_notification_queue_t * queue);

/**
 * Fixed block memory subsystem session end notification
 * Called by the core when a session to the driver has ended.
 * Cleans up per session data, which includes checking and fixing memory leaks
 *
 * @param slot Pointer to the slot to use for storing per-session data
 */
static void mali_memory_core_session_end(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot);

/**
 * Fixed block memory subsystem system info filler
 * Called by the core when a system info update is needed
 * We fill in info about all the memory types we have
 * @param info Pointer to system info struct to update
 * @return 0 on success, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_system_info_fill(_mali_system_info* info);

/* our registered resource handlers */

/**
 * Fixed block memory subsystem's notification handler for MMU resource instances.
 * Registered with the core during startup.
 * Called by the core for each mmu described in the active architecture's config.h file.
 * @param resource The resource to handle (type MMU)
 * @return 0 if the MMU was found and initialized, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_resource_mmu(_mali_osk_resource_t * resource);

/**
 * Fixed block memory subsystem's notification handler for FPGA_FRAMEWORK resource instances.
 * Registered with the core during startup.
 * Called by the core for each fpga framework described in the active architecture's config.h file.
 * @param resource The resource to handle (type FPGA_FRAMEWORK)
 * @return 0 if the FPGA framework was found and initialized, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_resource_fpga(_mali_osk_resource_t * resource);


static _mali_osk_errcode_t mali_memory_core_resource_dedicated_memory(_mali_osk_resource_t * resource);
static _mali_osk_errcode_t mali_memory_core_resource_os_memory(_mali_osk_resource_t * resource);

/**
 * @brief Internal function for unmapping memory
 *
 * Worker function for unmapping memory from a user-process. We assume that the
 * session/descriptor's lock was obtained before entry. For example, the
 * wrapper _mali_ukk_mem_munmap() will lock the descriptor, then call this
 * function to do the actual unmapping. mali_memory_core_session_end() could
 * also call this directly (depending on compilation options), having locked
 * the descriptor.
 *
 * This function will fail if it is unable to put the MMU in stall mode (which
 * might be the case if a page fault is also being processed).
 *
 * @param args see _mali_uk_mem_munmap_s in "mali_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
static _mali_osk_errcode_t _mali_ukk_mem_munmap_internal( _mali_uk_mem_munmap_s *args );

/**
 * The MMU interrupt handler
 * Upper half of the MMU interrupt processing.
 * Called by the kernel when the MMU has triggered an interrupt.
 * The interrupt function supports IRQ sharing. So it'll probe the MMU in question
 * @param irq The irq number (not used)
 * @param dev_id Points to the MMU object being handled
 * @param regs Registers of interrupted process (not used)
 * @return Standard Linux interrupt result.
 *         Subset used by the driver is IRQ_HANDLED processed
 *                                      IRQ_NONE Not processed
 */
static _mali_osk_errcode_t mali_kernel_memory_mmu_interrupt_handler_upper_half(void * data);

/**
 * The MMU reset hander
 * Bottom half of the MMU interrupt processing for page faults and bus errors
 * @param work The item to operate on, NULL in our case
 */
static void mali_kernel_memory_mmu_interrupt_handler_bottom_half ( void *data );

/**
 * Read MMU register value
 * Reads the contents of the specified register.
 * @param unit The MMU to read from
 * @param reg The register to read
 * @return The contents of the register
 */
static u32 mali_mmu_register_read(mali_kernel_memory_mmu * unit, mali_mmu_register reg);

/**
 * Write to a MMU register
 * Writes the given value to the specified register
 * @param unit The MMU to write to
 * @param reg The register to write to
 * @param val The value to write to the register
 */
static void mali_mmu_register_write(mali_kernel_memory_mmu * unit, mali_mmu_register reg, u32 val);

/**
 * Issues the reset command to the MMU and waits for HW to be ready again
 * @param mmu The MMU to reset
 */
static void mali_mmu_raw_reset(mali_kernel_memory_mmu * mmu);

/**
 * Issues the enable paging command to the MMU and waits for HW to complete the request
 * @param mmu The MMU to enable paging for
 */
static void mali_mmu_enable_paging(mali_kernel_memory_mmu * mmu);

/**
 * Issues the enable stall command to the MMU and waits for HW to complete the request
 * @param mmu The MMU to enable paging for
 * @return MALI_TRUE if HW stall was successfully engaged, otherwise MALI_FALSE (req timed out)
 */
static mali_bool mali_mmu_enable_stall(mali_kernel_memory_mmu * mmu);

/**
 * Issues the disable stall command to the MMU and waits for HW to complete the request
 * @param mmu The MMU to enable paging for
 */
static void mali_mmu_disable_stall(mali_kernel_memory_mmu * mmu);

#if MALI_USE_UNIFIED_MEMORY_PROVIDER != 0
static void ump_memory_release(void * ctx, void * handle);
static mali_physical_memory_allocation_result ump_memory_commit(void* ctx, mali_allocation_engine * engine, mali_memory_allocation * descriptor, u32* offset, mali_physical_memory_allocation * alloc_info);
#endif /* MALI_USE_UNIFIED_MEMORY_PROVIDER != 0*/


static void external_memory_release(void * ctx, void * handle);
static mali_physical_memory_allocation_result external_memory_commit(void* ctx, mali_allocation_engine * engine, mali_memory_allocation * descriptor, u32* offset, mali_physical_memory_allocation * alloc_info);




/* nop functions */

/* mali address manager needs to allocate page tables on allocate, write to page table(s) on map, write to page table(s) and release page tables on release */
static _mali_osk_errcode_t  mali_address_manager_allocate(mali_memory_allocation * descriptor); /* validates the range, allocates memory for the page tables if needed */
static _mali_osk_errcode_t  mali_address_manager_map(mali_memory_allocation * descriptor, u32 offset, u32 *phys_addr, u32 size);
static void mali_address_manager_release(mali_memory_allocation * descriptor);

static void mali_mmu_activate_address_space(mali_kernel_memory_mmu * mmu, u32 page_directory);

_mali_osk_errcode_t mali_mmu_page_table_cache_create(void);
void mali_mmu_page_table_cache_destroy(void);

_mali_osk_errcode_t mali_mmu_get_table_page(u32 *table_page, mali_io_address *mapping);
void mali_mmu_release_table_page(u32 pa);

static _mali_osk_errcode_t mali_allocate_empty_page_directory(void);

static void mali_free_empty_page_directory(void);

static _mali_osk_errcode_t fill_page(mali_io_address mapping, u32 data);

static _mali_osk_errcode_t mali_allocate_fault_flush_pages(void);

static void mali_free_fault_flush_pages(void);

static void mali_mmu_probe_irq_trigger(mali_kernel_memory_mmu * mmu);
static _mali_osk_errcode_t mali_mmu_probe_irq_acknowledge(mali_kernel_memory_mmu * mmu);

/* MMU variables */

typedef struct mali_mmu_page_table_allocation
{
	_mali_osk_list_t list;
	u32 * usage_map;
	u32 usage_count;
	u32 num_pages;
	mali_page_table_block pages;
} mali_mmu_page_table_allocation;

typedef struct mali_mmu_page_table_allocations
{
	_mali_osk_lock_t *lock;
	_mali_osk_list_t partial;
	_mali_osk_list_t full;
	/* we never hold on to a empty allocation */
} mali_mmu_page_table_allocations;

/* Head of the list of MMUs */
static _MALI_OSK_LIST_HEAD(mmu_head);

/* the mmu page table cache */
static struct mali_mmu_page_table_allocations page_table_cache;

/* page fault queue flush helper pages
 * note that the mapping pointers are currently unused outside of the initialization functions */
static u32 mali_page_fault_flush_page_directory = MALI_INVALID_PAGE;
static mali_io_address mali_page_fault_flush_page_directory_mapping = NULL;
static u32 mali_page_fault_flush_page_table = MALI_INVALID_PAGE;
static mali_io_address mali_page_fault_flush_page_table_mapping = NULL;
static u32 mali_page_fault_flush_data_page = MALI_INVALID_PAGE;
static mali_io_address mali_page_fault_flush_data_page_mapping = NULL;

/* an empty page directory (no address valid) which is active on any MMU not currently marked as in use */
static u32 mali_empty_page_directory = MALI_INVALID_PAGE;

/*
   The fixed memory system's mali subsystem interface implementation.
   We currently handle module and session life-time management.
*/
struct mali_kernel_subsystem mali_subsystem_memory =
{
	mali_memory_core_initialize,        /* startup */
	mali_memory_core_terminate,         /* shutdown */
	mali_memory_core_load_complete,     /* load_complete */
	mali_memory_core_system_info_fill,  /* system_info_fill */
	mali_memory_core_session_begin,     /* session_begin */
	mali_memory_core_session_end,       /* session_end */
	NULL,                               /* broadcast_notification */
#if MALI_STATE_TRACKING
	NULL,                               /* dump_state */
#endif
};

static mali_kernel_mem_address_manager mali_address_manager =
{
	mali_address_manager_allocate, /* allocate */
	mali_address_manager_release,  /* release */
	mali_address_manager_map,      /* map_physical */
	NULL                           /* unmap_physical not present*/
};

static mali_kernel_mem_address_manager process_address_manager =
{
	_mali_osk_mem_mapregion_init,  /* allocate */
	_mali_osk_mem_mapregion_term,  /* release */
	_mali_osk_mem_mapregion_map,   /* map_physical */
	_mali_osk_mem_mapregion_unmap  /* unmap_physical */
};

static mali_allocation_engine memory_engine = NULL;
static mali_physical_memory_allocator * physical_memory_allocators = NULL;

static dedicated_memory_info * mem_region_registrations = NULL;

/* Initialized when this subsystem is initialized. This is determined by the
 * position in subsystems[], and so the value used to initialize this is
 * determined at compile time */
static mali_kernel_subsystem_identifier mali_subsystem_memory_id = (mali_kernel_subsystem_identifier)-1;

/* called during module init */
static _mali_osk_errcode_t mali_memory_core_initialize(mali_kernel_subsystem_identifier id)
{
	MALI_DEBUG_PRINT(2, ("MMU memory system initializing\n"));

	/* save our subsystem id for later for use in slot lookup during session activation */
	mali_subsystem_memory_id = id;

	_MALI_OSK_INIT_LIST_HEAD(&mmu_head);

	MALI_CHECK_NO_ERROR( mali_mmu_page_table_cache_create() );

	/* register our handlers */
	MALI_CHECK_NO_ERROR( _mali_kernel_core_register_resource_handler(MMU, mali_memory_core_resource_mmu) );

	MALI_CHECK_NO_ERROR( _mali_kernel_core_register_resource_handler(FPGA_FRAMEWORK, mali_memory_core_resource_fpga) );

	MALI_CHECK_NO_ERROR( _mali_kernel_core_register_resource_handler(MEMORY, mali_memory_core_resource_dedicated_memory) );

	MALI_CHECK_NO_ERROR( _mali_kernel_core_register_resource_handler(OS_MEMORY, mali_memory_core_resource_os_memory) );

	memory_engine = mali_allocation_engine_create(&mali_address_manager, &process_address_manager);
	MALI_CHECK_NON_NULL( memory_engine, _MALI_OSK_ERR_FAULT);

	MALI_SUCCESS;
}

/* called if/when our module is unloaded */
static void mali_memory_core_terminate(mali_kernel_subsystem_identifier id)
{
	mali_kernel_memory_mmu * mmu, *temp_mmu;

	MALI_DEBUG_PRINT(2, ("MMU memory system terminating\n"));

	/* loop over all MMU units and shut them down */
	_MALI_OSK_LIST_FOREACHENTRY(mmu, temp_mmu, &mmu_head, mali_kernel_memory_mmu, list)
	{
		/* reset to defaults */
		mali_mmu_raw_reset(mmu);

		/* unregister the irq */
		_mali_osk_irq_term(mmu->irq);

		/* remove from the list of MMU's on the system */
		_mali_osk_list_del(&mmu->list);

		/* release resources */
		_mali_osk_mem_unmapioregion(mmu->base, mmu->mapping_size, mmu->mapped_registers);
		_mali_osk_mem_unreqregion(mmu->base, mmu->mapping_size);
		_mali_osk_lock_term(mmu->lock);
		_mali_osk_free(mmu);
	}

	/* free global helper pages */
	mali_free_empty_page_directory();
	mali_free_fault_flush_pages();

	/* destroy the page table cache before shutting down backends in case we have a page table leak to report */
	mali_mmu_page_table_cache_destroy();

	while ( NULL != mem_region_registrations)
	{
		dedicated_memory_info * m;
		m = mem_region_registrations;
		mem_region_registrations = m->next;
		_mali_osk_mem_unreqregion(m->base, m->size);
		_mali_osk_free(m);
	}

	while ( NULL != physical_memory_allocators)
	{
		mali_physical_memory_allocator * m;
		m = physical_memory_allocators;
		physical_memory_allocators = m->next;
		m->destroy(m);
	}

	if (NULL != memory_engine)
	{
		mali_allocation_engine_destroy(memory_engine);
		memory_engine = NULL;
	}

}

static _mali_osk_errcode_t mali_memory_core_session_begin(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot, _mali_osk_notification_queue_t * queue)
{
	memory_session * session_data;
	_mali_osk_errcode_t err;
	int i;
	mali_io_address pd_mapped;

	/* validate input */
	if (NULL == slot)
	{
		MALI_DEBUG_PRINT(1, ("NULL slot given to memory session begin\n"));
		MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	}

	if (NULL != *slot)
	{
		MALI_DEBUG_PRINT(1, ("The slot given to memory session begin already contains data"));
		MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	}

	MALI_DEBUG_PRINT(2, ("MMU session begin\n"));

	/* create the session data object */
	session_data = _mali_osk_calloc(1, sizeof(memory_session));
	MALI_CHECK_NON_NULL( session_data, _MALI_OSK_ERR_NOMEM );

	/* create descriptor mapping table */
	session_data->descriptor_mapping = mali_descriptor_mapping_create(MALI_MEM_DESCRIPTORS_INIT, MALI_MEM_DESCRIPTORS_MAX);

	if (NULL == session_data->descriptor_mapping)
	{
		_mali_osk_free(session_data);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	err = mali_mmu_get_table_page(&session_data->page_directory, &pd_mapped);

	session_data->page_directory_mapped = pd_mapped;
	if (_MALI_OSK_ERR_OK != err)
	{
		mali_descriptor_mapping_destroy(session_data->descriptor_mapping);
		_mali_osk_free(session_data);
		MALI_ERROR(err);
	}
	MALI_DEBUG_ASSERT_POINTER( session_data->page_directory_mapped );

	MALI_DEBUG_PRINT(2, ("Page directory for session 0x%x placed at physical address 0x%08X\n", mali_session_data, session_data->page_directory));

	for (i = 0; i < MALI_MMU_PAGE_SIZE/4; i++)
	{
		/* mark each page table as not present */
		_mali_osk_mem_iowrite32_relaxed(session_data->page_directory_mapped, sizeof(u32) * i, 0);
	}
	_mali_osk_write_mem_barrier();

	/* page_table_mapped[] is already set to NULL by _mali_osk_calloc call */

	_MALI_OSK_INIT_LIST_HEAD(&session_data->active_mmus);
	session_data->lock = _mali_osk_lock_init( _MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_ONELOCK | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, 128);
	if (NULL == session_data->lock)
	{
		mali_mmu_release_table_page(session_data->page_directory);
		mali_descriptor_mapping_destroy(session_data->descriptor_mapping);
		_mali_osk_free(session_data);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* Init the session's memory allocation list */
	_MALI_OSK_INIT_LIST_HEAD( &session_data->memory_head );

	*slot = session_data; /* slot will point to our data object */
	MALI_DEBUG_PRINT(2, ("MMU session begin: success\n"));
	MALI_SUCCESS;
}

static void descriptor_table_cleanup_callback(int descriptor_id, void* map_target)
{
	mali_memory_allocation * descriptor;

	descriptor = (mali_memory_allocation*)map_target;

	MALI_DEBUG_PRINT(1, ("Cleanup of descriptor %d mapping to 0x%x in descriptor table\n", descriptor_id, map_target));
	MALI_DEBUG_ASSERT(descriptor);

	mali_allocation_engine_release_memory(memory_engine, descriptor);
	_mali_osk_free(descriptor);
}

static void mali_memory_core_session_end(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot)
{
	memory_session * session_data;
	int i;
	const int num_page_table_entries = sizeof(session_data->page_entries_mapped) / sizeof(session_data->page_entries_mapped[0]);

	MALI_DEBUG_PRINT(2, ("MMU session end\n"));

	/* validate input */
	if (NULL == slot)
	{
		MALI_DEBUG_PRINT(1, ("NULL slot given to memory session begin\n"));
		return;
	}

	session_data = (memory_session *)*slot;

	if (NULL == session_data)
	{
		MALI_DEBUG_PRINT(1, ("No session data found during session end\n"));
		return;
	}
	/* Lock the session so we can modify the memory list */
	_mali_osk_lock_wait( session_data->lock, _MALI_OSK_LOCKMODE_RW );
	/* Noninterruptable spinlock type, so must always have locked. Checking should've been done in OSK function. */

#ifndef MALI_UKK_HAS_IMPLICIT_MMAP_CLEANUP
#if _MALI_OSK_SPECIFIC_INDIRECT_MMAP
#error Indirect MMAP specified, but UKK does not have implicit MMAP cleanup. Current implementation does not handle this.
#else

	/* Free all memory engine allocations */
	if (0 == _mali_osk_list_empty(&session_data->memory_head))
	{
		mali_memory_allocation *descriptor;
		mali_memory_allocation *temp;
		_mali_uk_mem_munmap_s unmap_args;

		MALI_DEBUG_PRINT(1, ("Memory found on session usage list during session termination\n"));

		unmap_args.ctx = mali_session_data;

		/* use the 'safe' list iterator, since freeing removes the active block from the list we're iterating */
		_MALI_OSK_LIST_FOREACHENTRY(descriptor, temp, &session_data->memory_head, mali_memory_allocation, list)
		{
			MALI_DEBUG_PRINT(4, ("Freeing block with mali address 0x%x size %d mapped in user space at 0x%x\n",
						descriptor->mali_address, descriptor->size, descriptor->size, descriptor->mapping)
					);
			/* ASSERT that the descriptor's lock references the correct thing */
			MALI_DEBUG_ASSERT(  descriptor->lock == session_data->lock );
			/* Therefore, we have already locked the descriptor */

			unmap_args.size = descriptor->size;
			unmap_args.mapping = descriptor->mapping;
			unmap_args.cookie = (u32)descriptor;

			/*
			 * This removes the descriptor from the list, and frees the descriptor
			 *
			 * Does not handle the _MALI_OSK_SPECIFIC_INDIRECT_MMAP case, since
			 * the only OS we are aware of that requires indirect MMAP also has
			 * implicit mmap cleanup.
			 */
			_mali_ukk_mem_munmap_internal( &unmap_args );
		}
	}

	/* Assert that we really did free everything */
	MALI_DEBUG_ASSERT( _mali_osk_list_empty(&session_data->memory_head) );
#endif /* _MALI_OSK_SPECIFIC_INDIRECT_MMAP */
#endif /* MALI_UKK_HAS_IMPLICIT_MMAP_CLEANUP */

	if (NULL != session_data->descriptor_mapping)
	{
		mali_descriptor_mapping_call_for_each(session_data->descriptor_mapping, descriptor_table_cleanup_callback);
		mali_descriptor_mapping_destroy(session_data->descriptor_mapping);
		session_data->descriptor_mapping = NULL;
	}

	for (i = 0; i < num_page_table_entries; i++)
	{
		/* free PTE memory */
		if (session_data->page_directory_mapped && (_mali_osk_mem_ioread32(session_data->page_directory_mapped, sizeof(u32)*i) & MALI_MMU_FLAGS_PRESENT))
		{
			mali_mmu_release_table_page( _mali_osk_mem_ioread32(session_data->page_directory_mapped, i*sizeof(u32)) & ~MALI_MMU_FLAGS_MASK);
			_mali_osk_mem_iowrite32(session_data->page_directory_mapped, i * sizeof(u32), 0);
		}
	}

	if (MALI_INVALID_PAGE != session_data->page_directory)
	{
		mali_mmu_release_table_page(session_data->page_directory);
		session_data->page_directory = MALI_INVALID_PAGE;
	}

	_mali_osk_lock_signal( session_data->lock, _MALI_OSK_LOCKMODE_RW );

	/**
	 * @note Could the VMA close handler mean that we use the session data after it was freed?
	 * In which case, would need to refcount the session data, and free on VMA close
	 */

	/* Free the lock */
	_mali_osk_lock_term( session_data->lock );
	/* free the session data object */
	_mali_osk_free(session_data);

	/* clear our slot */
	*slot = NULL;

	return;
}

static _mali_osk_errcode_t mali_allocate_empty_page_directory(void)
{
	_mali_osk_errcode_t err;
	mali_io_address mapping;

	MALI_CHECK_NO_ERROR(mali_mmu_get_table_page(&mali_empty_page_directory, &mapping));

	MALI_DEBUG_ASSERT_POINTER( mapping );

	err = fill_page(mapping, 0);
	if (_MALI_OSK_ERR_OK != err)
	{
		mali_mmu_release_table_page(mali_empty_page_directory);
		mali_empty_page_directory = MALI_INVALID_PAGE;
	}
	return err;
}

static void mali_free_empty_page_directory(void)
{
	if (MALI_INVALID_PAGE != mali_empty_page_directory)
	{
		mali_mmu_release_table_page(mali_empty_page_directory);
		mali_empty_page_directory = MALI_INVALID_PAGE;
	}
}

static _mali_osk_errcode_t fill_page(mali_io_address mapping, u32 data)
{
	int i;
	MALI_DEBUG_ASSERT_POINTER( mapping );

	for(i = 0; i < MALI_MMU_PAGE_SIZE/4; i++)
	{
		_mali_osk_mem_iowrite32_relaxed( mapping, i * sizeof(u32), data);
	}
	_mali_osk_mem_barrier();
	MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_allocate_fault_flush_pages(void)
{
	_mali_osk_errcode_t err;

	err = mali_mmu_get_table_page(&mali_page_fault_flush_data_page, &mali_page_fault_flush_data_page_mapping);
	if (_MALI_OSK_ERR_OK == err)
	{
		err = mali_mmu_get_table_page(&mali_page_fault_flush_page_table, &mali_page_fault_flush_page_table_mapping);
		if (_MALI_OSK_ERR_OK == err)
		{
			err = mali_mmu_get_table_page(&mali_page_fault_flush_page_directory, &mali_page_fault_flush_page_directory_mapping);
			if (_MALI_OSK_ERR_OK == err)
			{
				fill_page(mali_page_fault_flush_data_page_mapping, 0);
				fill_page(mali_page_fault_flush_page_table_mapping, mali_page_fault_flush_data_page | MALI_MMU_FLAGS_WRITE_PERMISSION | MALI_MMU_FLAGS_READ_PERMISSION | MALI_MMU_FLAGS_PRESENT);
				fill_page(mali_page_fault_flush_page_directory_mapping, mali_page_fault_flush_page_table | MALI_MMU_FLAGS_PRESENT);
				MALI_SUCCESS;
			}
			mali_mmu_release_table_page(mali_page_fault_flush_page_table);
			mali_page_fault_flush_page_table = MALI_INVALID_PAGE;
			mali_page_fault_flush_page_table_mapping = NULL;
		}
		mali_mmu_release_table_page(mali_page_fault_flush_data_page);
		mali_page_fault_flush_data_page = MALI_INVALID_PAGE;
		mali_page_fault_flush_data_page_mapping = NULL;
	}
	MALI_ERROR(err);
}

static void mali_free_fault_flush_pages(void)
{
	if (MALI_INVALID_PAGE != mali_page_fault_flush_page_directory)
	{
		mali_mmu_release_table_page(mali_page_fault_flush_page_directory);
		mali_page_fault_flush_page_directory = MALI_INVALID_PAGE;
	}

	if (MALI_INVALID_PAGE != mali_page_fault_flush_page_table)
	{
		mali_mmu_release_table_page(mali_page_fault_flush_page_table);
		mali_page_fault_flush_page_table = MALI_INVALID_PAGE;
	}

	if (MALI_INVALID_PAGE != mali_page_fault_flush_data_page)
	{
		mali_mmu_release_table_page(mali_page_fault_flush_data_page);
		mali_page_fault_flush_data_page = MALI_INVALID_PAGE;
	}
}

static _mali_osk_errcode_t mali_memory_core_load_complete(mali_kernel_subsystem_identifier id)
{
	mali_kernel_memory_mmu * mmu, * temp_mmu;

	/* Report the allocators */
	mali_allocation_engine_report_allocators( physical_memory_allocators );

	/* allocate the helper pages */
	MALI_CHECK_NO_ERROR( mali_allocate_empty_page_directory() );
	if (_MALI_OSK_ERR_OK != mali_allocate_fault_flush_pages())
	{
		mali_free_empty_page_directory();
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* activate the empty page directory on all MMU's */
	_MALI_OSK_LIST_FOREACHENTRY(mmu, temp_mmu, &mmu_head, mali_kernel_memory_mmu, list)
	{
		mali_mmu_register_write(mmu, MALI_MMU_REGISTER_DTE_ADDR, mali_empty_page_directory);
		mali_mmu_enable_paging(mmu);
	}

	MALI_DEBUG_PRINT(4, ("MMUs activated\n"));
	/* the MMU system is now active */

	MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_memory_core_system_info_fill(_mali_system_info* info)
{
	_mali_mem_info * mem_info;

	/* Make sure we won't leak any memory. It could also be that it's an
	 * uninitialized variable, but the caller should have zeroed the
	 * variable. */
	MALI_DEBUG_ASSERT(NULL == info->mem_info);

	info->has_mmu = 1;

	mem_info = _mali_osk_calloc(1,sizeof(_mali_mem_info));
	MALI_CHECK_NON_NULL( mem_info, _MALI_OSK_ERR_NOMEM );

	mem_info->size = 2048UL * 1024UL * 1024UL;
	mem_info->maximum_order_supported = 30;
	mem_info->flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_READABLE | _MALI_PP_WRITEABLE |_MALI_GP_READABLE | _MALI_GP_WRITEABLE;
	mem_info->identifier = 0;

	info->mem_info = mem_info;

	/* all OK */
	MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_memory_core_resource_mmu(_mali_osk_resource_t * resource)
{
	mali_kernel_memory_mmu * mmu;

	MALI_DEBUG_PRINT(4, ("MMU '%s' @ (0x%08X - 0x%08X)\n",
				resource->description, resource->base, resource->base + MALI_MMU_REGISTERS_SIZE - 1
				));

	if (NULL != mali_memory_core_mmu_lookup(resource->mmu_id))
	{
		MALI_DEBUG_PRINT(1, ("Duplicate MMU ids found. The id %d is already in use\n", resource->mmu_id));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	if (_MALI_OSK_ERR_OK != _mali_osk_mem_reqregion(resource->base, MALI_MMU_REGISTERS_SIZE, resource->description))
	{
		/* specified addresses are already in used by another driver / the kernel */
		MALI_DEBUG_PRINT(
				1, ("Failed to request MMU '%s' register address space at (0x%08X - 0x%08X)\n",
					resource->description, resource->base, resource->base + MALI_MMU_REGISTERS_SIZE - 1
				   ));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	mmu = _mali_osk_calloc(1, sizeof(mali_kernel_memory_mmu));

	if (NULL == mmu)
	{
		MALI_DEBUG_PRINT(1, ("Failed to allocate memory for handling a MMU unit"));
		_mali_osk_mem_unreqregion(resource->base, MALI_MMU_REGISTERS_SIZE);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	/* basic setup */
	_MALI_OSK_INIT_LIST_HEAD(&mmu->list);

	mmu->id = resource->mmu_id;
	mmu->irq_nr = resource->irq;
	mmu->flags = resource->flags;
	mmu->base = resource->base;
	mmu->mapping_size = MALI_MMU_REGISTERS_SIZE;
	mmu->description = resource->description; /* no need to copy */
	_MALI_OSK_INIT_LIST_HEAD(&mmu->callbacks);
	_MALI_OSK_INIT_LIST_HEAD(&mmu->session_link);
	mmu->in_page_fault_handler = 0;

	mmu->lock = _mali_osk_lock_init( _MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_ONELOCK | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, 127-mmu->id);
	if (NULL == mmu->lock)
	{
		MALI_DEBUG_PRINT(1, ("Failed to create mmu lock\n"));
		_mali_osk_mem_unreqregion(mmu->base, mmu->mapping_size);
		_mali_osk_free(mmu);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* map the registers */
	mmu->mapped_registers = _mali_osk_mem_mapioregion( mmu->base, mmu->mapping_size, mmu->description );
	if (NULL == mmu->mapped_registers)
	{
		/* failed to map the registers */
		MALI_DEBUG_PRINT(1, ("Failed to map MMU registers at 0x%08X\n", mmu->base));
		_mali_osk_lock_term(mmu->lock);
		_mali_osk_mem_unreqregion(mmu->base, MALI_MMU_REGISTERS_SIZE);
		_mali_osk_free(mmu);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	MALI_DEBUG_PRINT(4, ("MMU '%s' @ (0x%08X - 0x%08X) mapped to 0x%08X\n",
				resource->description, resource->base, resource->base + MALI_MMU_REGISTERS_SIZE - 1,  mmu->mapped_registers
				));

	/* setup MMU interrupt mask */
	/* set all values to known defaults */
	mali_mmu_raw_reset(mmu);
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_MASK, MALI_MMU_INTERRUPT_PAGE_FAULT | MALI_MMU_INTERRUPT_READ_BUS_ERROR);
	/* setup MMU page directory pointer */
	/* The mali_page_directory pointer is guaranteed to be 4kb aligned because we've used get_zeroed_page to accquire it */
	/* convert the kernel virtual address into a physical address and set */

	/* add to our list of MMU's */
	_mali_osk_list_addtail(&mmu->list, &mmu_head);

	mmu->irq = _mali_osk_irq_init(
			mmu->irq_nr,
			mali_kernel_memory_mmu_interrupt_handler_upper_half,
			mali_kernel_memory_mmu_interrupt_handler_bottom_half,
			(_mali_osk_irq_trigger_t)mali_mmu_probe_irq_trigger,
			(_mali_osk_irq_ack_t)mali_mmu_probe_irq_acknowledge,
			mmu,
			"mali_mmu_irq_handlers"
			);
	if (NULL == mmu->irq)
	{
		_mali_osk_list_del(&mmu->list);
		_mali_osk_lock_term(mmu->lock);
		_mali_osk_mem_unmapioregion( mmu->base, mmu->mapping_size, mmu->mapped_registers );
		_mali_osk_mem_unreqregion(resource->base, MALI_MMU_REGISTERS_SIZE);
		_mali_osk_free(mmu);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* set to a known state */
	mali_mmu_raw_reset(mmu);
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_MASK, MALI_MMU_INTERRUPT_PAGE_FAULT | MALI_MMU_INTERRUPT_READ_BUS_ERROR);

	MALI_DEBUG_PRINT(2, ("MMU registered\n"));

	MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_memory_core_resource_fpga(_mali_osk_resource_t * resource)
{
	mali_io_address mapping;

	MALI_DEBUG_PRINT(5, ("FPGA framework '%s' @ (0x%08X - 0x%08X)\n",
				resource->description, resource->base, resource->base + sizeof(u32) * 2 - 1
				));

	mapping = _mali_osk_mem_mapioregion(resource->base + 0x1000, sizeof(u32) * 2, "fpga framework");
	if (mapping)
	{
		MALI_DEBUG_CODE(u32 data = )
			_mali_osk_mem_ioread32(mapping, 0);
		MALI_DEBUG_PRINT(2, ("FPGA framwork '%s' @ 0x%08X:\n", resource->description, resource->base));
		MALI_DEBUG_PRINT(2, ("\tBitfile date: %d%02d%02d_%02d%02d\n",
					(data >> 20),
					(data >> 16) & 0xF,
					(data >> 11) & 0x1F,
					(data >> 6)  & 0x1F,
					(data >> 0)  & 0x3F));
		MALI_DEBUG_CODE(data = )
			_mali_osk_mem_ioread32(mapping, sizeof(u32));
		MALI_DEBUG_PRINT(2, ("\tBitfile SCCS rev: %d\n", data));

		_mali_osk_mem_unmapioregion(resource->base + 0x1000, sizeof(u32) *2, mapping);
	}
	else MALI_DEBUG_PRINT(1, ("Failed to access FPGA framwork '%s' @ 0x%08X\n", resource->description, resource->base));

	MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_memory_core_resource_os_memory(_mali_osk_resource_t * resource)
{
	mali_physical_memory_allocator * allocator;
	mali_physical_memory_allocator ** next_allocator_list;

	u32 alloc_order = resource->alloc_order;

	allocator = mali_os_allocator_create(resource->size, resource->cpu_usage_adjust, resource->description);
	if (NULL == allocator)
	{
		MALI_DEBUG_PRINT(1, ("Failed to create OS memory allocator\n"));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	allocator->alloc_order = alloc_order;

	/* link in the allocator: insertion into ordered list
	 * resources of the same alloc_order will be Last-in-first */
	next_allocator_list = &physical_memory_allocators;

	while ( NULL != *next_allocator_list &&
			(*next_allocator_list)->alloc_order < alloc_order )
	{
		next_allocator_list = &((*next_allocator_list)->next);
	}

	allocator->next = (*next_allocator_list);
	(*next_allocator_list) = allocator;

	MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_memory_core_resource_dedicated_memory(_mali_osk_resource_t * resource)
{
	mali_physical_memory_allocator * allocator;
	mali_physical_memory_allocator ** next_allocator_list;
	dedicated_memory_info * cleanup_data;

	u32 alloc_order = resource->alloc_order;

	/* do the lowlevel linux operation first */

	/* Request ownership of the memory */
	if (_MALI_OSK_ERR_OK != _mali_osk_mem_reqregion(resource->base, resource->size, resource->description))
	{
		MALI_DEBUG_PRINT(1, ("Failed to request memory region %s (0x%08X - 0x%08X)\n", resource->description, resource->base, resource->base + resource->size - 1));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* create generic block allocator object to handle it */
	allocator = mali_block_allocator_create(resource->base, resource->cpu_usage_adjust, resource->size, resource->description );

	if (NULL == allocator)
	{
		MALI_DEBUG_PRINT(1, ("Memory bank registration failed\n"));
		_mali_osk_mem_unreqregion(resource->base, resource->size);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* save lowlevel cleanup info */
	allocator->alloc_order = alloc_order;

	cleanup_data = _mali_osk_malloc(sizeof(dedicated_memory_info));

	if (NULL == cleanup_data)
	{
		_mali_osk_mem_unreqregion(resource->base, resource->size);
		allocator->destroy(allocator);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	cleanup_data->base = resource->base;
	cleanup_data->size = resource->size;

	cleanup_data->next = mem_region_registrations;
	mem_region_registrations = cleanup_data;

	/* link in the allocator: insertion into ordered list
	 * resources of the same alloc_order will be Last-in-first */
	next_allocator_list = &physical_memory_allocators;

	while ( NULL != *next_allocator_list &&
			(*next_allocator_list)->alloc_order < alloc_order )
	{
		next_allocator_list = &((*next_allocator_list)->next);
	}

	allocator->next = (*next_allocator_list);
	(*next_allocator_list) = allocator;

	MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_kernel_memory_mmu_interrupt_handler_upper_half(void * data)
{
	mali_kernel_memory_mmu * mmu;
	u32 int_stat;
	mali_core_renderunit *core;

	if (mali_benchmark) MALI_SUCCESS;

	mmu = (mali_kernel_memory_mmu *)data;

	MALI_DEBUG_ASSERT_POINTER(mmu);
	
	/* Pointer to core holding this MMU */
	core = (mali_core_renderunit *)mmu->core;

	if(CORE_OFF == core->state)
	{
		MALI_SUCCESS;
	}


	/* check if it was our device which caused the interrupt (we could be sharing the IRQ line) */
	int_stat = mali_mmu_register_read(mmu, MALI_MMU_REGISTER_INT_STATUS);
	if (0 == int_stat)
	{
		MALI_ERROR(_MALI_OSK_ERR_FAULT); /* no bits set, we are sharing the IRQ line and someone else caused the interrupt */
	}

	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_MASK, 0);

	mali_mmu_register_read(mmu, MALI_MMU_REGISTER_STATUS);

	if (int_stat & MALI_MMU_INTERRUPT_PAGE_FAULT)
	{
		_mali_osk_irq_schedulework(mmu->irq);
	}
	if (int_stat & MALI_MMU_INTERRUPT_READ_BUS_ERROR)
	{
		/* clear interrupt flag */
		mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_CLEAR, MALI_MMU_INTERRUPT_READ_BUS_ERROR);
		/* reenable it */
		mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_MASK, mali_mmu_register_read(mmu, MALI_MMU_REGISTER_INT_MASK) | MALI_MMU_INTERRUPT_READ_BUS_ERROR);
	}

	MALI_SUCCESS;
}


static void mali_kernel_mmu_bus_reset(mali_kernel_memory_mmu * mmu)
{

#if defined(USING_MALI200)
	int i;
	const int replay_buffer_check_interval = 10; /* must be below 1000 */
	const int replay_buffer_max_number_of_checks = 100;
#endif

	_mali_osk_lock_wait(mmu->lock, _MALI_OSK_LOCKMODE_RW);
	/* add an extra reference while handling the page fault */
	mmu->usage_count++;
	_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);

	MALI_DEBUG_PRINT(4, ("Sending stop bus request to cores\n"));
	/* request to stop the bus, but don't wait for it to actually stop */
	_mali_kernel_core_broadcast_subsystem_message(MMU_KILL_STEP1_STOP_BUS_FOR_ALL_CORES, (u32)mmu);

#if defined(USING_MALI200)
	/* no new request will come from any of the connected cores from now
	 * we must now flush the playback buffer for any requests queued already
	 */

	_mali_osk_lock_wait(mmu->lock, _MALI_OSK_LOCKMODE_RW);

	MALI_DEBUG_PRINT(4, ("Switching to the special page fault flush page directory\n"));
	/* don't use the mali_mmu_activate_address_space function here as we can't stall the MMU */
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_DTE_ADDR, mali_page_fault_flush_page_directory);
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ZAP_CACHE);
	/* resume the MMU */
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_CLEAR, MALI_MMU_INTERRUPT_PAGE_FAULT);
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_PAGE_FAULT_DONE);
	/* the MMU will now play back all the requests, all going to our special page fault flush data page */

	/* just to be safe, check that the playback buffer is empty before continuing */
	if (!mali_benchmark) {
		for (i = 0; i < replay_buffer_max_number_of_checks; i++)
		{
			if (mali_mmu_register_read(mmu, MALI_MMU_REGISTER_STATUS) & MALI_MMU_STATUS_BIT_REPLAY_BUFFER_EMPTY) break;
			_mali_osk_time_ubusydelay(replay_buffer_check_interval);
		}

		MALI_DEBUG_PRINT_IF(1, i == replay_buffer_max_number_of_checks, ("MMU: %s: Failed to flush replay buffer on page fault\n", mmu->description));
		MALI_DEBUG_PRINT(1, ("Replay playback took %ld usec\n", i * replay_buffer_check_interval));
	}

	_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);

#endif
	/* notify all subsystems that the core should be reset once the bus is actually stopped */
	MALI_DEBUG_PRINT(4,("Sending job abort command to subsystems\n"));
	_mali_kernel_core_broadcast_subsystem_message(MMU_KILL_STEP2_RESET_ALL_CORES_AND_ABORT_THEIR_JOBS, (u32)mmu);

	_mali_osk_lock_wait(mmu->lock, _MALI_OSK_LOCKMODE_RW);

	/* reprogram the MMU */
	mali_mmu_raw_reset(mmu);
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_MASK, MALI_MMU_INTERRUPT_PAGE_FAULT | MALI_MMU_INTERRUPT_READ_BUS_ERROR);
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_DTE_ADDR, mali_empty_page_directory); /* no session is active, so just activate the empty page directory */
	mali_mmu_enable_paging(mmu);

	_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);

	/* release the extra address space reference, will schedule */
	mali_memory_core_mmu_release_address_space_reference(mmu);

	/* resume normal operation */
	_mali_kernel_core_broadcast_subsystem_message(MMU_KILL_STEP3_CONTINUE_JOB_HANDLING, (u32)mmu);
	MALI_DEBUG_PRINT(4, ("Page fault handling complete\n"));
}

static void mali_mmu_raw_reset(mali_kernel_memory_mmu * mmu)
{
	const int max_loop_count = 100;
	const int delay_in_usecs = 1;

	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_DTE_ADDR, 0xCAFEBABE);
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_SOFT_RESET);

	if (!mali_benchmark)
	{
		int i;
		for (i = 0; i < max_loop_count; ++i)
		{
			if (mali_mmu_register_read(mmu, MALI_MMU_REGISTER_DTE_ADDR) == 0)
			{
				break;
			}
			_mali_osk_time_ubusydelay(delay_in_usecs);
		}
		MALI_DEBUG_PRINT_IF(1, (max_loop_count == i), ("Reset request failed, MMU status is 0x%08X\n", mali_mmu_register_read(mmu, MALI_MMU_REGISTER_STATUS)));
	}
}

static void mali_mmu_enable_paging(mali_kernel_memory_mmu * mmu)
{
	const int max_loop_count = 100;
	const int delay_in_usecs = 1;

	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ENABLE_PAGING);

	if (!mali_benchmark)
	{
		int i;
		for (i = 0; i < max_loop_count; ++i)
		{
			if (mali_mmu_register_read(mmu, MALI_MMU_REGISTER_STATUS) & MALI_MMU_STATUS_BIT_PAGING_ENABLED)
			{
				break;
			}
			_mali_osk_time_ubusydelay(delay_in_usecs);
		}
		MALI_DEBUG_PRINT_IF(1, (max_loop_count == i), ("Enable paging request failed, MMU status is 0x%08X\n", mali_mmu_register_read(mmu, MALI_MMU_REGISTER_STATUS)));
	}
}

static mali_bool mali_mmu_enable_stall(mali_kernel_memory_mmu * mmu)
{
	const int max_loop_count = 100;
	const int delay_in_usecs = 999;
	int i;

	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ENABLE_STALL);

	if (!mali_benchmark)
	{
		for (i = 0; i < max_loop_count; ++i)
		{
			if (mali_mmu_register_read(mmu, MALI_MMU_REGISTER_STATUS) & MALI_MMU_STATUS_BIT_STALL_ACTIVE)
			{
				break;
			}
			_mali_osk_time_ubusydelay(delay_in_usecs);
		}
		MALI_DEBUG_PRINT_IF(1, (max_loop_count == i), ("Enable stall request failed, MMU status is 0x%08X\n", mali_mmu_register_read(mmu, MALI_MMU_REGISTER_STATUS)));
		if (max_loop_count == i)
		{
			return MALI_FALSE;
		}
	}
	
	return MALI_TRUE;
}

static void mali_mmu_disable_stall(mali_kernel_memory_mmu * mmu)
{
	const int max_loop_count = 100;
	const int delay_in_usecs = 1;
	int i;

	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_DISABLE_STALL);

	if (!mali_benchmark)
	{
		for (i = 0; i < max_loop_count; ++i)
		{
			if ((mali_mmu_register_read(mmu, MALI_MMU_REGISTER_STATUS) & MALI_MMU_STATUS_BIT_STALL_ACTIVE) == 0)
			{
				break;
			}
			_mali_osk_time_ubusydelay(delay_in_usecs);
		}
		MALI_DEBUG_PRINT_IF(1, (max_loop_count == i), ("Disable stall request failed, MMU status is 0x%08X\n", mali_mmu_register_read(mmu, MALI_MMU_REGISTER_STATUS)));
	}
}

void mali_kernel_mmu_reset(void * input_mmu)
{
	mali_kernel_memory_mmu * mmu;
	MALI_DEBUG_ASSERT_POINTER(input_mmu);
	mmu = (mali_kernel_memory_mmu *)input_mmu;

	MALI_DEBUG_PRINT(4, ("Mali MMU: mali_kernel_mmu_reset: %s\n", mmu->description));

	if ( 0 != mmu->in_page_fault_handler)
	{
		/* This is possible if the bus can never be stopped for some reason */
		MALI_PRINT_ERROR(("Stopping the Memory bus not possible. Mali reset could not be performed."));
		return;
	}
	_mali_osk_lock_wait(mmu->lock, _MALI_OSK_LOCKMODE_RW);
	mali_mmu_raw_reset(mmu);
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_MASK, MALI_MMU_INTERRUPT_PAGE_FAULT | MALI_MMU_INTERRUPT_READ_BUS_ERROR);
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_DTE_ADDR, mali_empty_page_directory); /* no session is active, so just activate the empty page directory */
	mali_mmu_enable_paging(mmu);
	_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);

}

void mali_kernel_mmu_force_bus_reset(void * input_mmu)
{
	mali_kernel_memory_mmu * mmu;
	MALI_DEBUG_ASSERT_POINTER(input_mmu);
	mmu = (mali_kernel_memory_mmu *)input_mmu;
	if ( 0 != mmu->in_page_fault_handler)
	{
		/* This is possible if the bus can never be stopped for some reason */
		MALI_PRINT_ERROR(("Stopping the Memory bus not possible. Mali reset could not be performed."));
		return;
	}
	MALI_DEBUG_PRINT(1, ("Mali MMU: Force_bus_reset.\n"));
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_MASK, 0);
	mali_kernel_mmu_bus_reset(mmu);
}


static void mali_kernel_memory_mmu_interrupt_handler_bottom_half(void * data)
{
	mali_kernel_memory_mmu *mmu;
	u32 raw, fault_address, status;
	mali_core_renderunit *core;

	MALI_DEBUG_PRINT(1, ("mali_kernel_memory_mmu_interrupt_handler_bottom_half\n"));
	if (NULL == data)
	{
		MALI_PRINT_ERROR(("MMU IRQ work queue: NULL argument"));
		return; /* Error */
	}
	mmu = (mali_kernel_memory_mmu*)data;


	MALI_DEBUG_PRINT(4, ("Locking subsystems\n"));
	/* lock all subsystems */
	_mali_kernel_core_broadcast_subsystem_message(MMU_KILL_STEP0_LOCK_SUBSYSTEM, (u32)mmu);

	/* Pointer to core holding this MMU */
	core = (mali_core_renderunit *)mmu->core;	
	
	if(CORE_OFF == core->state)
        {
                _mali_kernel_core_broadcast_subsystem_message(MMU_KILL_STEP4_UNLOCK_SUBSYSTEM, (u32)mmu);
                return;
        }

	raw = mali_mmu_register_read(mmu, MALI_MMU_REGISTER_INT_RAWSTAT);
	status = mali_mmu_register_read(mmu, MALI_MMU_REGISTER_STATUS);

	if ( (0==(raw & MALI_MMU_INTERRUPT_PAGE_FAULT)) &&  (0==(status & MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE)) )
	{
		MALI_DEBUG_PRINT(1, ("MMU: Page fault bottom half: No Irq found.\n"));
		MALI_DEBUG_PRINT(4, ("Unlocking subsystems"));
		_mali_kernel_core_broadcast_subsystem_message(MMU_KILL_STEP4_UNLOCK_SUBSYSTEM, (u32)mmu);
		return;
	}

	mmu->in_page_fault_handler = 1;

	fault_address = mali_mmu_register_read(mmu, MALI_MMU_REGISTER_PAGE_FAULT_ADDR);
	MALI_PRINT(("Page fault detected at 0x%x from bus id %d of type %s on %s\n",
				(void*)fault_address,
				(status >> 6) & 0x1F,
				(status & 32) ? "write" : "read",
				mmu->description)
			  );

	if (NULL == mmu->active_session)
	{
		MALI_PRINT(("Spurious memory access detected from MMU %s\n", mmu->description));
	}
	else
	{
		MALI_PRINT(("Active page directory at 0x%08X\n", mmu->active_session->page_directory));
		MALI_PRINT(("Info from page table for VA 0x%x:\n", (void*)fault_address));
		MALI_PRINT(("DTE entry: PTE at 0x%x marked as %s\n",
					(void*)(_mali_osk_mem_ioread32(mmu->active_session->page_directory_mapped,
					MALI_MMU_PDE_ENTRY(fault_address) * sizeof(u32)) & ~MALI_MMU_FLAGS_MASK),
					_mali_osk_mem_ioread32(mmu->active_session->page_directory_mapped,
					MALI_MMU_PDE_ENTRY(fault_address) * sizeof(u32)) & MALI_MMU_FLAGS_PRESENT ? "present" : "not present"
				   ));

		if (_mali_osk_mem_ioread32(mmu->active_session->page_directory_mapped, MALI_MMU_PDE_ENTRY(fault_address) * sizeof(u32)) & MALI_MMU_FLAGS_PRESENT)
		{
			mali_io_address pte;
			u32 data;
			pte = mmu->active_session->page_entries_mapped[MALI_MMU_PDE_ENTRY(fault_address)];
			data = _mali_osk_mem_ioread32(pte, MALI_MMU_PTE_ENTRY(fault_address) * sizeof(u32));
			MALI_PRINT(("PTE entry: Page at 0x%x, %s %s %s\n",
						(void*)(data & ~MALI_MMU_FLAGS_MASK),
						data & MALI_MMU_FLAGS_PRESENT ? "present" : "not present",
						data & MALI_MMU_FLAGS_READ_PERMISSION ? "readable" : "",
						data & MALI_MMU_FLAGS_WRITE_PERMISSION ? "writable" : ""
					   ));
		}
		else
		{
			MALI_PRINT(("PTE entry: Not present\n"));
		}
	}


	mali_kernel_mmu_bus_reset(mmu);

	mmu->in_page_fault_handler = 0;

	/* unlock all subsystems */
	MALI_DEBUG_PRINT(4, ("Unlocking subsystems"));
	_mali_kernel_core_broadcast_subsystem_message(MMU_KILL_STEP4_UNLOCK_SUBSYSTEM, (u32)mmu);

}


static u32 mali_mmu_register_read(mali_kernel_memory_mmu * unit, mali_mmu_register reg)
{
	u32 val;

	if (mali_benchmark) return 0;

	val = _mali_osk_mem_ioread32(unit->mapped_registers, (u32)reg * sizeof(u32));

	MALI_DEBUG_PRINT(6, ("mali_mmu_register_read addr:0x%04X val:0x%08x\n", (u32)reg * sizeof(u32),val));

	return val;
}

static void mali_mmu_register_write(mali_kernel_memory_mmu * unit, mali_mmu_register reg, u32 val)
{
	if (mali_benchmark) return;

	MALI_DEBUG_PRINT(6, ("mali_mmu_register_write  addr:0x%04X val:0x%08x\n", (u32)reg * sizeof(u32), val));

	_mali_osk_mem_iowrite32(unit->mapped_registers, (u32)reg * sizeof(u32), val);
}

#if MALI_USE_UNIFIED_MEMORY_PROVIDER != 0
static mali_physical_memory_allocation_result ump_memory_commit(void* ctx, mali_allocation_engine * engine, mali_memory_allocation * descriptor, u32* offset, mali_physical_memory_allocation * alloc_info)
{
	ump_dd_handle ump_mem;
	u32 nr_blocks;
	u32 i;
	ump_dd_physical_block * ump_blocks;
	ump_mem_allocation *ret_allocation;

    MALI_DEBUG_ASSERT_POINTER(ctx);
    MALI_DEBUG_ASSERT_POINTER(engine);
    MALI_DEBUG_ASSERT_POINTER(descriptor);
    MALI_DEBUG_ASSERT_POINTER(alloc_info);

	ret_allocation = _mali_osk_malloc( sizeof( ump_mem_allocation ) );
	if ( NULL==ret_allocation ) return MALI_MEM_ALLOC_INTERNAL_FAILURE;

	ump_mem = (ump_dd_handle)ctx;

	MALI_DEBUG_PRINT(4, ("In ump_memory_commit\n"));

	nr_blocks = ump_dd_phys_block_count_get(ump_mem);

	MALI_DEBUG_PRINT(4, ("Have %d blocks\n", nr_blocks));

	if (nr_blocks == 0)
	{
		MALI_DEBUG_PRINT(1, ("No block count\n"));
		_mali_osk_free( ret_allocation );
		return MALI_MEM_ALLOC_INTERNAL_FAILURE;
	}

	ump_blocks = _mali_osk_malloc(sizeof(*ump_blocks)*nr_blocks );
	if ( NULL==ump_blocks )
	{
		_mali_osk_free( ret_allocation );
		return MALI_MEM_ALLOC_INTERNAL_FAILURE;
	}

	if (UMP_DD_INVALID == ump_dd_phys_blocks_get(ump_mem, ump_blocks, nr_blocks))
	{
		_mali_osk_free(ump_blocks);
		_mali_osk_free( ret_allocation );
		return MALI_MEM_ALLOC_INTERNAL_FAILURE;
	}

	/* Store away the initial offset for unmapping purposes */
	ret_allocation->initial_offset = *offset;

	for(i=0; i<nr_blocks; ++i)
	{
		MALI_DEBUG_PRINT(4, ("Mapping in 0x%08x size %d\n", ump_blocks[i].addr , ump_blocks[i].size));
		if (_MALI_OSK_ERR_OK != mali_allocation_engine_map_physical(engine, descriptor, *offset, ump_blocks[i].addr , 0, ump_blocks[i].size ))
		{
			u32 size_allocated = *offset - ret_allocation->initial_offset;
			MALI_DEBUG_PRINT(1, ("Mapping of external memory failed\n"));

			/* unmap all previous blocks (if any) */
			mali_allocation_engine_unmap_physical(engine, descriptor, ret_allocation->initial_offset, size_allocated, (_mali_osk_mem_mapregion_flags_t)0 );

			_mali_osk_free(ump_blocks);
			_mali_osk_free(ret_allocation);
			return MALI_MEM_ALLOC_INTERNAL_FAILURE;
		}
		*offset += ump_blocks[i].size;
	}

	if (descriptor->flags & MALI_MEMORY_ALLOCATION_FLAG_MAP_GUARD_PAGE)
	{
		/* Map in an extra virtual guard page at the end of the VMA */
		MALI_DEBUG_PRINT(4, ("Mapping in extra guard page\n"));
		if (_MALI_OSK_ERR_OK != mali_allocation_engine_map_physical(engine, descriptor, *offset, ump_blocks[0].addr , 0, _MALI_OSK_MALI_PAGE_SIZE ))
		{
			u32 size_allocated = *offset - ret_allocation->initial_offset;
			MALI_DEBUG_PRINT(1, ("Mapping of external memory (guard page) failed\n"));

			/* unmap all previous blocks (if any) */
			mali_allocation_engine_unmap_physical(engine, descriptor, ret_allocation->initial_offset, size_allocated, (_mali_osk_mem_mapregion_flags_t)0 );

			_mali_osk_free(ump_blocks);
			_mali_osk_free(ret_allocation);
			return MALI_MEM_ALLOC_INTERNAL_FAILURE;
		}
		*offset += _MALI_OSK_MALI_PAGE_SIZE;
	}

	_mali_osk_free( ump_blocks );

	ret_allocation->engine = engine;
	ret_allocation->descriptor = descriptor;
	ret_allocation->ump_mem = ump_mem;
	ret_allocation->size_allocated = *offset - ret_allocation->initial_offset;

	alloc_info->ctx = NULL;
	alloc_info->handle = ret_allocation;
	alloc_info->next = NULL;
	alloc_info->release = ump_memory_release;

	return MALI_MEM_ALLOC_FINISHED;
}

static void ump_memory_release(void * ctx, void * handle)
{
	ump_dd_handle ump_mem;
	ump_mem_allocation *allocation;

	allocation = (ump_mem_allocation *)handle;

	MALI_DEBUG_ASSERT_POINTER( allocation );

	ump_mem = allocation->ump_mem;

	MALI_DEBUG_ASSERT(UMP_DD_HANDLE_INVALID!=ump_mem);

	/* At present, this is a no-op. But, it allows the mali_address_manager to
	 * do unmapping of a subrange in future. */
	mali_allocation_engine_unmap_physical( allocation->engine,
										   allocation->descriptor,
										   allocation->initial_offset,
										   allocation->size_allocated,
										   (_mali_osk_mem_mapregion_flags_t)0
										   );
	_mali_osk_free( allocation );


	ump_dd_reference_release(ump_mem) ;
	return;
}

_mali_osk_errcode_t _mali_ukk_attach_ump_mem( _mali_uk_attach_ump_mem_s *args )
{
	ump_dd_handle ump_mem;
	mali_physical_memory_allocator external_memory_allocator;
	memory_session * session_data;
	mali_memory_allocation * descriptor;
	int md;

  	MALI_DEBUG_ASSERT_POINTER(args);
  	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	session_data = (memory_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_memory_id);
	MALI_CHECK_NON_NULL(session_data, _MALI_OSK_ERR_INVALID_ARGS);

	/* check arguments */
	/* NULL might be a valid Mali address */
	if ( ! args->size) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	/* size must be a multiple of the system page size */
	if ( args->size % _MALI_OSK_MALI_PAGE_SIZE ) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	MALI_DEBUG_PRINT(3,
	                 ("Requested to map ump memory with secure id %d into virtual memory 0x%08X, size 0x%08X\n",
	                  args->secure_id, args->mali_address, args->size));

	ump_mem = ump_dd_handle_create_from_secure_id( (int)args->secure_id ) ;

	if ( UMP_DD_HANDLE_INVALID==ump_mem ) MALI_ERROR(_MALI_OSK_ERR_FAULT);

	descriptor = _mali_osk_calloc(1, sizeof(mali_memory_allocation));
	if (NULL == descriptor)
	{
		ump_dd_reference_release(ump_mem);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	descriptor->size = args->size;
	descriptor->mapping = NULL;
	descriptor->mali_address = args->mali_address;
	descriptor->mali_addr_mapping_info = (void*)session_data;
	descriptor->process_addr_mapping_info = NULL; /* do not map to process address space */
	descriptor->lock = session_data->lock;
	if (args->flags & _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE)
	{
		descriptor->flags = MALI_MEMORY_ALLOCATION_FLAG_MAP_GUARD_PAGE;
	}
	_mali_osk_list_init( &descriptor->list );

	if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_allocate_mapping(session_data->descriptor_mapping, descriptor, &md))
	{
		ump_dd_reference_release(ump_mem);
		_mali_osk_free(descriptor);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	external_memory_allocator.allocate = ump_memory_commit;
	external_memory_allocator.allocate_page_table_block = NULL;
	external_memory_allocator.ctx = ump_mem;
	external_memory_allocator.name = "UMP Memory";
	external_memory_allocator.next = NULL;

	_mali_osk_lock_wait(session_data->lock, _MALI_OSK_LOCKMODE_RW);

	if (_MALI_OSK_ERR_OK != mali_allocation_engine_allocate_memory(memory_engine, descriptor, &external_memory_allocator, NULL))
	{
		_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);
		mali_descriptor_mapping_free(session_data->descriptor_mapping, md);
		ump_dd_reference_release(ump_mem);
		_mali_osk_free(descriptor);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);

	args->cookie = md;

	MALI_DEBUG_PRINT(5,("Returning from UMP attach\n"));

	/* All OK */
	MALI_SUCCESS;
}


_mali_osk_errcode_t _mali_ukk_release_ump_mem( _mali_uk_release_ump_mem_s *args )
{
	mali_memory_allocation * descriptor;
    memory_session * session_data;

    MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

    session_data = (memory_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_memory_id);
    MALI_CHECK_NON_NULL(session_data, _MALI_OSK_ERR_INVALID_ARGS);

    if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_get(session_data->descriptor_mapping, args->cookie, (void**)&descriptor))
	{
		MALI_DEBUG_PRINT(1, ("Invalid memory descriptor %d used to release ump memory\n", args->cookie));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	mali_descriptor_mapping_free(session_data->descriptor_mapping, args->cookie);
	
	_mali_osk_lock_wait( session_data->lock, _MALI_OSK_LOCKMODE_RW );

	mali_allocation_engine_release_memory(memory_engine, descriptor);

	_mali_osk_lock_signal( session_data->lock, _MALI_OSK_LOCKMODE_RW );

	_mali_osk_free(descriptor);

	MALI_SUCCESS;

}
#endif /* MALI_USE_UNIFIED_MEMORY_PROVIDER != 0 */


static mali_physical_memory_allocation_result external_memory_commit(void* ctx, mali_allocation_engine * engine, mali_memory_allocation * descriptor, u32* offset, mali_physical_memory_allocation * alloc_info)
{
	u32 * data;
	external_mem_allocation * ret_allocation;

    MALI_DEBUG_ASSERT_POINTER(ctx);
    MALI_DEBUG_ASSERT_POINTER(engine);
    MALI_DEBUG_ASSERT_POINTER(descriptor);
    MALI_DEBUG_ASSERT_POINTER(alloc_info);

	ret_allocation = _mali_osk_malloc( sizeof(external_mem_allocation) );

	if ( NULL == ret_allocation )
	{
		return MALI_MEM_ALLOC_INTERNAL_FAILURE;
	}

	data = (u32*)ctx;

	ret_allocation->engine = engine;
	ret_allocation->descriptor = descriptor;
	ret_allocation->initial_offset = *offset;

	alloc_info->ctx = NULL;
	alloc_info->handle = ret_allocation;
	alloc_info->next = NULL;
	alloc_info->release = external_memory_release;

	MALI_DEBUG_PRINT(3, ("External map: mapping phys 0x%08X at mali virtual address 0x%08X staring at offset 0x%08X length 0x%08X\n", data[0], descriptor->mali_address, *offset, data[1]));

	if (_MALI_OSK_ERR_OK != mali_allocation_engine_map_physical(engine, descriptor, *offset, data[0], 0, data[1]))
	{
		MALI_DEBUG_PRINT(1, ("Mapping of external memory failed\n"));
		_mali_osk_free(ret_allocation);
		return MALI_MEM_ALLOC_INTERNAL_FAILURE;
	}
	*offset += data[1];

	if (descriptor->flags & MALI_MEMORY_ALLOCATION_FLAG_MAP_GUARD_PAGE)
	{
		/* Map in an extra virtual guard page at the end of the VMA */
		MALI_DEBUG_PRINT(4, ("Mapping in extra guard page\n"));
		if (_MALI_OSK_ERR_OK != mali_allocation_engine_map_physical(engine, descriptor, *offset, data[0], 0, _MALI_OSK_MALI_PAGE_SIZE))
		{
			u32 size_allocated = *offset - ret_allocation->initial_offset;
			MALI_DEBUG_PRINT(1, ("Mapping of external memory (guard page) failed\n"));

			/* unmap what we previously mapped */
			mali_allocation_engine_unmap_physical(engine, descriptor, ret_allocation->initial_offset, size_allocated, (_mali_osk_mem_mapregion_flags_t)0 );
			_mali_osk_free(ret_allocation);
			return MALI_MEM_ALLOC_INTERNAL_FAILURE;
		}
		*offset += _MALI_OSK_MALI_PAGE_SIZE;
	}

	ret_allocation->size = *offset - ret_allocation->initial_offset;

	return MALI_MEM_ALLOC_FINISHED;
}

static void external_memory_release(void * ctx, void * handle)
{
	external_mem_allocation * allocation;

	allocation = (external_mem_allocation *) handle;
	MALI_DEBUG_ASSERT_POINTER( allocation );

	/* At present, this is a no-op. But, it allows the mali_address_manager to
	 * do unmapping of a subrange in future. */

	mali_allocation_engine_unmap_physical( allocation->engine,
										   allocation->descriptor,
										   allocation->initial_offset,
										   allocation->size,
										   (_mali_osk_mem_mapregion_flags_t)0
										   );

	_mali_osk_free( allocation );

	return;
}

_mali_osk_errcode_t _mali_ukk_map_external_mem( _mali_uk_map_external_mem_s *args )
{
	mali_physical_memory_allocator external_memory_allocator;
    memory_session * session_data;
    u32 info[2];
	mali_memory_allocation * descriptor;
	int md;

    MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

    session_data = (memory_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_memory_id);
    MALI_CHECK_NON_NULL(session_data, _MALI_OSK_ERR_INVALID_ARGS);

    external_memory_allocator.allocate = external_memory_commit;
	external_memory_allocator.allocate_page_table_block = NULL;
	external_memory_allocator.ctx = &info[0];
	external_memory_allocator.name = "External Memory";
	external_memory_allocator.next = NULL;

	/* check arguments */
	/* NULL might be a valid Mali address */
	if ( ! args->size) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	/* size must be a multiple of the system page size */
	if ( args->size % _MALI_OSK_MALI_PAGE_SIZE ) MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);

	MALI_DEBUG_PRINT(3,
	        ("Requested to map physical memory 0x%x-0x%x into virtual memory 0x%x\n",
	        (void*)args->phys_addr,
	        (void*)(args->phys_addr + args->size -1),
	        (void*)args->mali_address)
	);

	/* Validate the mali physical range */
	MALI_CHECK_NO_ERROR( mali_kernel_core_validate_mali_phys_range( args->phys_addr, args->size ) );

	info[0] = args->phys_addr;
	info[1] = args->size;

	descriptor = _mali_osk_calloc(1, sizeof(mali_memory_allocation));
	if (NULL == descriptor) MALI_ERROR(_MALI_OSK_ERR_NOMEM);

	descriptor->size = args->size;
	descriptor->mapping = NULL;
	descriptor->mali_address = args->mali_address;
	descriptor->mali_addr_mapping_info = (void*)session_data;
	descriptor->process_addr_mapping_info = NULL; /* do not map to process address space */
	descriptor->lock = session_data->lock;
	if (args->flags & _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE)
	{
		descriptor->flags = MALI_MEMORY_ALLOCATION_FLAG_MAP_GUARD_PAGE;
	}
	_mali_osk_list_init( &descriptor->list );

	if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_allocate_mapping(session_data->descriptor_mapping, descriptor, &md))
	{
		_mali_osk_free(descriptor);
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	_mali_osk_lock_wait(session_data->lock, _MALI_OSK_LOCKMODE_RW);

	if (_MALI_OSK_ERR_OK != mali_allocation_engine_allocate_memory(memory_engine, descriptor, &external_memory_allocator, NULL))
	{
		_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);
		mali_descriptor_mapping_free(session_data->descriptor_mapping, md);
		_mali_osk_free(descriptor);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);

	args->cookie = md;

	MALI_DEBUG_PRINT(5,("Returning from range_map_external_memory\n"));

	/* All OK */
	MALI_SUCCESS;
}


_mali_osk_errcode_t _mali_ukk_unmap_external_mem( _mali_uk_unmap_external_mem_s *args )
{
	mali_memory_allocation * descriptor;
    memory_session * session_data;

    MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

    session_data = (memory_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_memory_id);
    MALI_CHECK_NON_NULL(session_data, _MALI_OSK_ERR_INVALID_ARGS);

    if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_get(session_data->descriptor_mapping, args->cookie, (void**)&descriptor))
	{
		MALI_DEBUG_PRINT(1, ("Invalid memory descriptor %d used to unmap external memory\n", args->cookie));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	mali_descriptor_mapping_free(session_data->descriptor_mapping, args->cookie);

	_mali_osk_lock_wait( session_data->lock, _MALI_OSK_LOCKMODE_RW );

	mali_allocation_engine_release_memory(memory_engine, descriptor);

	_mali_osk_lock_signal( session_data->lock, _MALI_OSK_LOCKMODE_RW );

	_mali_osk_free(descriptor);

	MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_init_mem( _mali_uk_init_mem_s *args )
{
    MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	args->memory_size = 2 * 1024 * 1024 * 1024UL; /* 2GB address space */
	args->mali_address_base = 1 * 1024 * 1024 * 1024UL; /* staring at 1GB, causing this layout: (0-1GB unused)(1GB-3G usage by Mali)(3G-4G unused) */
	MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_term_mem( _mali_uk_term_mem_s *args )
{
    MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
    MALI_SUCCESS;
}

_mali_osk_errcode_t mali_mmu_page_table_cache_create(void)
{
    page_table_cache.lock = _mali_osk_lock_init( _MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_ONELOCK | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, 110);
    MALI_CHECK_NON_NULL( page_table_cache.lock, _MALI_OSK_ERR_FAULT );
	_MALI_OSK_INIT_LIST_HEAD(&page_table_cache.partial);
	_MALI_OSK_INIT_LIST_HEAD(&page_table_cache.full);
    MALI_SUCCESS;
}

void mali_mmu_page_table_cache_destroy(void)
{
	mali_mmu_page_table_allocation * alloc, *temp;

	_MALI_OSK_LIST_FOREACHENTRY(alloc, temp, &page_table_cache.partial, mali_mmu_page_table_allocation, list)
	{
		MALI_DEBUG_PRINT_IF(1, 0 != alloc->usage_count, ("Destroying page table cache while pages are tagged as in use. %d allocations still marked as in use.\n", alloc->usage_count));
		_mali_osk_list_del(&alloc->list);
		alloc->pages.release(&alloc->pages);
		_mali_osk_free(alloc->usage_map);
		_mali_osk_free(alloc);
	}

	MALI_DEBUG_PRINT_IF(1, 0 == _mali_osk_list_empty(&page_table_cache.full), ("Page table cache full list contains one or more elements \n"));

	_MALI_OSK_LIST_FOREACHENTRY(alloc, temp, &page_table_cache.full, mali_mmu_page_table_allocation, list)
	{
		MALI_DEBUG_PRINT(1, ("Destroy alloc 0x%08X with usage count %d\n", (u32)alloc, alloc->usage_count));
		_mali_osk_list_del(&alloc->list);
		alloc->pages.release(&alloc->pages);
		_mali_osk_free(alloc->usage_map);
		_mali_osk_free(alloc);
	}

	_mali_osk_lock_term(page_table_cache.lock);
}

_mali_osk_errcode_t mali_mmu_get_table_page(u32 *table_page, mali_io_address *mapping)
{
	_mali_osk_lock_wait(page_table_cache.lock, _MALI_OSK_LOCKMODE_RW);

	if (0 == _mali_osk_list_empty(&page_table_cache.partial))
	{
		mali_mmu_page_table_allocation * alloc = _MALI_OSK_LIST_ENTRY(page_table_cache.partial.next, mali_mmu_page_table_allocation, list);
		int page_number = _mali_osk_find_first_zero_bit(alloc->usage_map, alloc->num_pages);
		MALI_DEBUG_PRINT(6, ("Partial page table allocation found, using page offset %d\n", page_number));
		_mali_osk_set_nonatomic_bit(page_number, alloc->usage_map);
		alloc->usage_count++;
		if (alloc->num_pages == alloc->usage_count)
		{
			/* full, move alloc to full list*/
			_mali_osk_list_move(&alloc->list, &page_table_cache.full);
		}
    	_mali_osk_lock_signal(page_table_cache.lock, _MALI_OSK_LOCKMODE_RW);

        *table_page = (MALI_MMU_PAGE_SIZE * page_number) + alloc->pages.phys_base;
		*mapping =  (mali_io_address)((MALI_MMU_PAGE_SIZE * page_number) + (u32)alloc->pages.mapping);
		MALI_DEBUG_PRINT(4, ("Page table allocated for VA=0x%08X, MaliPA=0x%08X\n", *mapping, *table_page ));
        MALI_SUCCESS;
	}
	else
	{
		mali_mmu_page_table_allocation * alloc;
		/* no free pages, allocate a new one */

		alloc = (mali_mmu_page_table_allocation *)_mali_osk_calloc(1, sizeof(mali_mmu_page_table_allocation));
		if (NULL == alloc)
		{
        	_mali_osk_lock_signal(page_table_cache.lock, _MALI_OSK_LOCKMODE_RW);
            *table_page = MALI_INVALID_PAGE;
			MALI_ERROR(_MALI_OSK_ERR_NOMEM);
		}

		_MALI_OSK_INIT_LIST_HEAD(&alloc->list);

		if (_MALI_OSK_ERR_OK != mali_allocation_engine_allocate_page_tables(memory_engine, &alloc->pages, physical_memory_allocators))
		{
			MALI_DEBUG_PRINT(1, ("No more memory for page tables\n"));
			_mali_osk_free(alloc);
        	_mali_osk_lock_signal(page_table_cache.lock, _MALI_OSK_LOCKMODE_RW);
            *table_page = MALI_INVALID_PAGE;
			*mapping = NULL;
			MALI_ERROR(_MALI_OSK_ERR_NOMEM);
		}

		/* create the usage map */
		alloc->num_pages = alloc->pages.size / MALI_MMU_PAGE_SIZE;
		alloc->usage_count = 1;
		MALI_DEBUG_PRINT(3, ("New page table cache expansion, %d pages in new cache allocation\n", alloc->num_pages));
		alloc->usage_map = _mali_osk_calloc(1, ((alloc->num_pages + BITS_PER_LONG - 1) & ~(BITS_PER_LONG-1) / BITS_PER_LONG) * sizeof(unsigned long));
		if (NULL == alloc->usage_map)
		{
			MALI_DEBUG_PRINT(1, ("Failed to allocate memory to describe MMU page table cache usage\n"));
			alloc->pages.release(&alloc->pages);
			_mali_osk_free(alloc);
        	_mali_osk_lock_signal(page_table_cache.lock, _MALI_OSK_LOCKMODE_RW);
            *table_page = MALI_INVALID_PAGE;
			*mapping = NULL;
            MALI_ERROR(_MALI_OSK_ERR_NOMEM);
		}

		/* clear memory allocation */
		fill_page(alloc->pages.mapping, 0);

		_mali_osk_set_nonatomic_bit(0, alloc->usage_map);

		if (alloc->num_pages > 1)
		{
			_mali_osk_list_add(&alloc->list, &page_table_cache.partial);
		}
		else
		{
			_mali_osk_list_add(&alloc->list, &page_table_cache.full);
		}

    	_mali_osk_lock_signal(page_table_cache.lock, _MALI_OSK_LOCKMODE_RW);
		*table_page = alloc->pages.phys_base; /* return the first page */
		*mapping = alloc->pages.mapping; /* Mapping for first page */
		MALI_DEBUG_PRINT(4, ("Page table allocated for VA=0x%08X, MaliPA=0x%08X\n", *mapping, *table_page ));
        MALI_SUCCESS;
	}
}

void mali_mmu_release_table_page(u32 pa)
{
	mali_mmu_page_table_allocation * alloc, * temp_alloc;

	MALI_DEBUG_PRINT_IF(1, pa & 4095, ("Bad page address 0x%x given to mali_mmu_release_table_page\n", (void*)pa));

	MALI_DEBUG_PRINT(4, ("Releasing table page 0x%08X to the cache\n", pa));

   	_mali_osk_lock_wait(page_table_cache.lock, _MALI_OSK_LOCKMODE_RW);

	/* find the entry this address belongs to */
	/* first check the partial list */
	_MALI_OSK_LIST_FOREACHENTRY(alloc, temp_alloc, &page_table_cache.partial, mali_mmu_page_table_allocation, list)
	{
		u32 start = alloc->pages.phys_base;
		u32 last = start + (alloc->num_pages - 1) * MALI_MMU_PAGE_SIZE;
		if (pa >= start && pa <= last)
		{
            MALI_DEBUG_ASSERT(0 != _mali_osk_test_bit((pa - start)/MALI_MMU_PAGE_SIZE, alloc->usage_map));
			_mali_osk_clear_nonatomic_bit((pa - start)/MALI_MMU_PAGE_SIZE, alloc->usage_map);
			alloc->usage_count--;

			_mali_osk_memset((void*)( ((u32)alloc->pages.mapping) + (pa - start) ), 0, MALI_MMU_PAGE_SIZE);

			if (0 == alloc->usage_count)
			{
				/* empty, release whole page alloc */
				_mali_osk_list_del(&alloc->list);
				alloc->pages.release(&alloc->pages);
				_mali_osk_free(alloc->usage_map);
				_mali_osk_free(alloc);
			}
           	_mali_osk_lock_signal(page_table_cache.lock, _MALI_OSK_LOCKMODE_RW);
        	MALI_DEBUG_PRINT(4, ("(partial list)Released table page 0x%08X to the cache\n", pa));
			return;
		}
	}

	/* the check the full list */
	_MALI_OSK_LIST_FOREACHENTRY(alloc, temp_alloc, &page_table_cache.full, mali_mmu_page_table_allocation, list)
	{
		u32 start = alloc->pages.phys_base;
		u32 last = start + (alloc->num_pages - 1) * MALI_MMU_PAGE_SIZE;
		if (pa >= start && pa <= last)
		{
			_mali_osk_clear_nonatomic_bit((pa - start)/MALI_MMU_PAGE_SIZE, alloc->usage_map);
			alloc->usage_count--;

			_mali_osk_memset((void*)( ((u32)alloc->pages.mapping) + (pa - start) ), 0, MALI_MMU_PAGE_SIZE);


			if (0 == alloc->usage_count)
			{
				/* empty, release whole page alloc */
				_mali_osk_list_del(&alloc->list);
				alloc->pages.release(&alloc->pages);
				_mali_osk_free(alloc->usage_map);
				_mali_osk_free(alloc);
			}
			else
			{
				/* transfer to partial list */
				_mali_osk_list_move(&alloc->list, &page_table_cache.partial);
			}
			
           	_mali_osk_lock_signal(page_table_cache.lock, _MALI_OSK_LOCKMODE_RW);
        	MALI_DEBUG_PRINT(4, ("(full list)Released table page 0x%08X to the cache\n", pa));
			return;
		}
	}

	MALI_DEBUG_PRINT(1, ("pa 0x%x not found in the page table cache\n", (void*)pa));

   	_mali_osk_lock_signal(page_table_cache.lock, _MALI_OSK_LOCKMODE_RW);
}

void* mali_memory_core_mmu_lookup(u32 id)
{
	mali_kernel_memory_mmu * mmu, * temp_mmu;

	/* find an MMU with a matching id */
	_MALI_OSK_LIST_FOREACHENTRY(mmu, temp_mmu, &mmu_head, mali_kernel_memory_mmu, list)
	{
		if (id == mmu->id) return mmu;
	}

	/* not found */
	return NULL;
}

void mali_memory_core_mmu_owner(void *core, void *mmu_ptr)
{
        mali_kernel_memory_mmu *mmu;

        MALI_DEBUG_ASSERT_POINTER(mmu_ptr);
        MALI_DEBUG_ASSERT_POINTER(core);

        mmu = (mali_kernel_memory_mmu *)mmu_ptr;
        mmu->core = core;
}

void mali_mmu_activate_address_space(mali_kernel_memory_mmu * mmu, u32 page_directory)
{
	mali_mmu_enable_stall(mmu); /* this might fail, but changing the DTE address and ZAP should work anyway... */
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_DTE_ADDR, page_directory);
	mali_mmu_register_write(mmu, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ZAP_CACHE);
	mali_mmu_disable_stall(mmu);
}

_mali_osk_errcode_t mali_memory_core_mmu_activate_page_table(void* mmu_ptr, struct mali_session_data * mali_session_data, void(*callback)(void*), void * callback_argument)
{
	memory_session * requested_memory_session;
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;
	mali_kernel_memory_mmu * mmu;

	MALI_DEBUG_ASSERT_POINTER(mmu_ptr);
	MALI_DEBUG_ASSERT_POINTER(mali_session_data);

	mmu = (mali_kernel_memory_mmu *)mmu_ptr;

	MALI_DEBUG_PRINT(4, ("Asked to activate page table for session 0x%x on MMU %s\n", mali_session_data, mmu->description));
	requested_memory_session = mali_kernel_session_manager_slot_get(mali_session_data, mali_subsystem_memory_id);
	MALI_DEBUG_PRINT(5, ("Session 0x%x looked up as using memory session 0x%x\n", mali_session_data, requested_memory_session));

	MALI_DEBUG_ASSERT_POINTER(requested_memory_session);

	MALI_DEBUG_PRINT(7, ("Taking locks\n"));

	_mali_osk_lock_wait(requested_memory_session->lock, _MALI_OSK_LOCKMODE_RW);
	_mali_osk_lock_wait(mmu->lock, _MALI_OSK_LOCKMODE_RW);
	if (0 == mmu->usage_count)
	{
		/* no session currently active, activate the requested session */
		MALI_DEBUG_ASSERT(NULL == mmu->active_session);
		mmu->active_session = requested_memory_session;
		mmu->usage_count = 1;
		MALI_DEBUG_PRINT(4, ("MMU idle, activating page directory 0x%08X on MMU %s\n", requested_memory_session->page_directory, mmu->description));
		mali_mmu_activate_address_space(mmu, requested_memory_session->page_directory);
		{
			/* Insert mmu into the right place in the active_mmus list so that
			 * it is still sorted. The list must be sorted by ID so we can get
			 * the mutexes in the right order in
			 * _mali_ukk_mem_munmap_internal().
			 */
			_mali_osk_list_t *entry;
			for (entry = requested_memory_session->active_mmus.next;
				 entry != &requested_memory_session->active_mmus;
				 entry = entry->next)
			{
				mali_kernel_memory_mmu *temp = _MALI_OSK_LIST_ENTRY(entry, mali_kernel_memory_mmu, session_link);
				if (mmu->id < temp->id)
					break;
			}
			/* If we broke out, then 'entry' points to the list node of the
			 * first mmu with a greater ID; otherwise, it points to
			 * active_mmus. We want to add *before* this node.
			 */
			_mali_osk_list_addtail(&mmu->session_link, entry);
		}
		err = _MALI_OSK_ERR_OK;
	}

	/* Allow two cores to run in parallel if they come from the same session */
	else if (
	           (mmu->in_page_fault_handler == 0) &&
	           (requested_memory_session == mmu->active_session ) &&
	           (0==(MALI_MMU_DISALLOW_PARALLELL_WORK_OF_MALI_CORES & mmu->flags))
	        )
	{
		/* nested activation detected, just update the reference count */
		MALI_DEBUG_PRINT(4, ("Nested activation detected, %d previous activations found\n", mmu->usage_count));
		mmu->usage_count++;
		err = _MALI_OSK_ERR_OK;
	}

	else if (NULL != callback)
	{
		/* can't activate right now, notify caller on idle via callback */
		mali_kernel_memory_mmu_idle_callback * callback_object, * temp_callback_object;
		int found = 0;

		MALI_DEBUG_PRINT(3, ("The MMU is busy and is using a different address space, callback given\n"));
		/* check for existing registration */
		_MALI_OSK_LIST_FOREACHENTRY(callback_object, temp_callback_object, &mmu->callbacks, mali_kernel_memory_mmu_idle_callback, link)
		{
			if (callback_object->callback == callback)
			{
				found = 1;
				break;
			}
		}

		if (found)
		{
			MALI_DEBUG_PRINT(5, ("Duplicate callback registration found, ignoring\n"));
			/* callback already registered */
			err = _MALI_OSK_ERR_BUSY;
		}
	 	else
		{
			MALI_DEBUG_PRINT(5,("New callback, registering\n"));
			/* register the new callback */
			callback_object = _mali_osk_malloc(sizeof(mali_kernel_memory_mmu_idle_callback));
			if (NULL != callback_object)
			{
				MALI_DEBUG_PRINT(7,("Callback struct setup\n"));
				callback_object->callback = callback;
				callback_object->callback_argument = callback_argument;
				_mali_osk_list_addtail(&callback_object->link, &mmu->callbacks);
				err = _MALI_OSK_ERR_BUSY;
			}
		}
	}

	_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);
	_mali_osk_lock_signal(requested_memory_session->lock, _MALI_OSK_LOCKMODE_RW);

    MALI_ERROR(err);
}

void mali_memory_core_mmu_release_address_space_reference(void* mmu_ptr)
{
	mali_kernel_memory_mmu_idle_callback * callback_object, * temp;
	mali_kernel_memory_mmu * mmu;
	memory_session * session;

	_MALI_OSK_LIST_HEAD(callbacks);

    MALI_DEBUG_ASSERT_POINTER(mmu_ptr);
	mmu = (mali_kernel_memory_mmu *)mmu_ptr;

	session = mmu->active_session;

	/* support that we handle spurious page faults */
	if (NULL != session)
	{
		_mali_osk_lock_wait(session->lock, _MALI_OSK_LOCKMODE_RW);
	}

  	_mali_osk_lock_wait(mmu->lock, _MALI_OSK_LOCKMODE_RW);
	MALI_DEBUG_PRINT(4, ("Deactivation of address space on MMU %s, %d references exists\n", mmu->description, mmu->usage_count));
    MALI_DEBUG_ASSERT(0 != mmu->usage_count);
	mmu->usage_count--;
	if (0 != mmu->usage_count)
	{
		MALI_DEBUG_PRINT(4, ("MMU still in use by this address space, %d references still exists\n", mmu->usage_count));
      	_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);
		/* support that we handle spurious page faults */
		if (NULL != session)
		{
			_mali_osk_lock_signal(session->lock, _MALI_OSK_LOCKMODE_RW);
		}
		return;
	}

	MALI_DEBUG_PRINT(4, ("Activating the empty page directory on %s\n", mmu->description));

	/* last reference gone, deactivate current address space */
	mali_mmu_activate_address_space(mmu, mali_empty_page_directory);

	/* unlink from session */
	_mali_osk_list_delinit(&mmu->session_link);
	/* remove the active session pointer */
	mmu->active_session = NULL;

	/* Notify all registered callbacks.
	 * We have to be clever here:
	 * We must call the callbacks with the spinlock unlocked and
	 * the callback list emptied to allow them to re-register.
	 * So we make a copy of the list, clears the list and then later call the callbacks on the local copy
	 */
	/* copy list */
	_MALI_OSK_INIT_LIST_HEAD(&callbacks);
	_mali_osk_list_splice(&mmu->callbacks, &callbacks);
	/* clear the original, allowing new registrations during the callback */
	_MALI_OSK_INIT_LIST_HEAD(&mmu->callbacks);

	/* end of mmu manipulation, so safe to unlock */
   	_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);

	/* then finally remove the (possible) session lock, supporting that no session was active (spurious page fault handling) */
	if (NULL != session)
	{
		_mali_osk_lock_signal(session->lock, _MALI_OSK_LOCKMODE_RW);
	}

	_MALI_OSK_LIST_FOREACHENTRY(callback_object, temp, &callbacks, mali_kernel_memory_mmu_idle_callback, link)
	{
        MALI_DEBUG_ASSERT_POINTER(callback_object->callback);
		(callback_object->callback)(callback_object->callback_argument);
		_mali_osk_list_del(&callback_object->link);
		_mali_osk_free(callback_object);
	}
}

void mali_memory_core_mmu_unregister_callback(void* mmu_ptr, void(*callback)(void*))
{
	mali_kernel_memory_mmu_idle_callback * callback_object, * temp_callback_object;
	mali_kernel_memory_mmu * mmu;
    MALI_DEBUG_ASSERT_POINTER(mmu_ptr);

    MALI_DEBUG_ASSERT_POINTER(callback);
    MALI_DEBUG_ASSERT_POINTER(mmu_ptr);

	mmu = (mali_kernel_memory_mmu *)mmu_ptr;

  	_mali_osk_lock_wait(mmu->lock, _MALI_OSK_LOCKMODE_RW);
	_MALI_OSK_LIST_FOREACHENTRY(callback_object, temp_callback_object, &mmu->callbacks, mali_kernel_memory_mmu_idle_callback, link)
	{
        MALI_DEBUG_ASSERT_POINTER(callback_object->callback);
		if (callback_object->callback == callback)
		{
			_mali_osk_list_del(&callback_object->link);
			_mali_osk_free(callback_object);
			break;
		}
	}
  	_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);
}

static _mali_osk_errcode_t mali_address_manager_allocate(mali_memory_allocation * descriptor)
{
	/* allocate page tables, if needed */
	int i;
	const int first_pde_idx = MALI_MMU_PDE_ENTRY(descriptor->mali_address);
	int last_pde_idx;
	memory_session * session_data;
#if defined USING_MALI400_L2_CACHE
	int has_active_mmus = 0;
	int page_dir_updated = 0;
#endif


	if (descriptor->flags & MALI_MEMORY_ALLOCATION_FLAG_MAP_GUARD_PAGE)
	{
		last_pde_idx = MALI_MMU_PDE_ENTRY(descriptor->mali_address + _MALI_OSK_MALI_PAGE_SIZE + descriptor->size - 1);
	}
	else
	{
		last_pde_idx = MALI_MMU_PDE_ENTRY(descriptor->mali_address + descriptor->size - 1);
	}

	session_data = (memory_session*)descriptor->mali_addr_mapping_info;
    MALI_DEBUG_ASSERT_POINTER(session_data);

	MALI_DEBUG_PRINT(4, ("allocating page tables for Mali virtual address space 0x%08X to 0x%08X\n", descriptor->mali_address, descriptor->mali_address + descriptor->size - 1));

#if defined USING_MALI400_L2_CACHE
	if (0 == _mali_osk_list_empty(&session_data->active_mmus))
	{
		/*
		 * We have active MMUs, so we are probably in the process of alocating more memory for a suspended GP job (PLBU heap)
		 * From Mali-400 MP r1p0, MMU page directory/tables are also cached by the Mali L2 cache, thus we need to invalidate the page directory
		 * from the L2 cache if we add new page directory entries (PDEs) to the page directory.
		 * We only need to do this when we have an active MMU, because we otherwise invalidate the entire Mali L2 cache before at job start
		 */
		has_active_mmus = 1;
	}
#endif

	for (i = first_pde_idx; i <= last_pde_idx; i++)
	{
		if ( 0 == (_mali_osk_mem_ioread32(session_data->page_directory_mapped, i * sizeof(u32)) & MALI_MMU_FLAGS_PRESENT) )
		{
			u32 pte_phys;
			mali_io_address pte_mapped;
            _mali_osk_errcode_t err;

			/* allocate a new page table */
            MALI_DEBUG_ASSERT(0 == session_data->page_entries_usage_count[i]);
            MALI_DEBUG_ASSERT(NULL == session_data->page_entries_mapped[i]);

			err = mali_mmu_get_table_page(&pte_phys, &pte_mapped);
            if (_MALI_OSK_ERR_OK == err)
			{
				session_data->page_entries_mapped[i] = pte_mapped;
				MALI_DEBUG_ASSERT_POINTER( session_data->page_entries_mapped[i] );

				_mali_osk_mem_iowrite32(session_data->page_directory_mapped, i * sizeof(u32), pte_phys | MALI_MMU_FLAGS_PRESENT); /* mark page table as present */

				/* update usage count */
				session_data->page_entries_usage_count[i]++;
#if defined USING_MALI400_L2_CACHE
				page_dir_updated = 1;
#endif
				continue; /* continue loop */
			}

			MALI_DEBUG_PRINT(1, ("Page table alloc failed\n"));
			break; /* abort loop, failed to allocate one or more page tables */
		}
		else
		{
			session_data->page_entries_usage_count[i]++;
		}
	}

	if (i <= last_pde_idx)
	{
		/* one or more pages could not be allocated, release reference count for the ones we added one for */
		/* adjust for the one which caused the for loop to be aborted */
		i--;

		while (i >= first_pde_idx)
		{
            MALI_DEBUG_ASSERT(0 != session_data->page_entries_usage_count[i]);
			session_data->page_entries_usage_count[i]--;
			if (0 == session_data->page_entries_usage_count[i])
			{
				/* last reference removed */
				mali_mmu_release_table_page(MALI_MMU_ENTRY_ADDRESS(_mali_osk_mem_ioread32(session_data->page_directory_mapped, i * sizeof(u32))));
				session_data->page_entries_mapped[i] = NULL;
				_mali_osk_mem_iowrite32(session_data->page_directory_mapped, i * sizeof(u32), 0); /* mark as not present in the page directory */
			}
			i--;
		}

		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

#if defined USING_MALI400_L2_CACHE
	if (1 == has_active_mmus && 1 == page_dir_updated)
	{
		/*
		 * We have updated the page directory and have an active MMU using it, so invalidate it in the Mali L2 cache.
		 */
		mali_kernel_l2_cache_invalidate_page(session_data->page_directory);
	}
#endif

	/* all OK */
	MALI_SUCCESS;
}

static void mali_address_manager_release(mali_memory_allocation * descriptor)
{
	int first_pde_idx;
	int last_pde_idx;
	memory_session * session_data;
	u32 mali_address;
	u32 mali_address_end;
	u32 left;
	int i;
#if defined USING_MALI400_L2_CACHE
	int has_active_mmus = 0;
	int page_dir_updated = 0;
#endif

    MALI_DEBUG_ASSERT_POINTER(descriptor);
	session_data = (memory_session*)descriptor->mali_addr_mapping_info;
    MALI_DEBUG_ASSERT_POINTER(session_data);
    MALI_DEBUG_ASSERT_POINTER(session_data->page_directory_mapped);

	mali_address = descriptor->mali_address;
	mali_address_end = descriptor->mali_address + descriptor->size;
	left = descriptor->size;

	first_pde_idx = MALI_MMU_PDE_ENTRY(mali_address);
	last_pde_idx = MALI_MMU_PDE_ENTRY(mali_address_end - 1);

	MALI_DEBUG_PRINT(3, ("Zapping Mali MMU table for address 0x%08X size 0x%08X\n", mali_address, left));
	MALI_DEBUG_PRINT(4, ("Zapping PDE %d through %d\n", first_pde_idx, last_pde_idx));

#if defined USING_MALI400_L2_CACHE
	if (0 == _mali_osk_list_empty(&session_data->active_mmus))
	{
		/*
		 * From Mali-400 MP r1p0, MMU page directory/tables are also cached by the Mali L2 cache, thus we need to invalidate the page tables
		 * from the L2 cache to ensure that the memory is unmapped.
		 * We only need to do this when we have an active MMU, because we otherwise invalidate the entire Mali L2 cache before at job start
		 */
		has_active_mmus = 1;
	}
#endif


	for (i = first_pde_idx; i <= last_pde_idx; i++)
	{
		int size_inside_pte = left < 0x400000 ? left : 0x400000;
		const int first_pte_idx = MALI_MMU_PTE_ENTRY(mali_address);
		int last_pte_idx = MALI_MMU_PTE_ENTRY(mali_address + size_inside_pte - 1);

		if (last_pte_idx < first_pte_idx)
		{
			/* The last_pte_idx is into the next PTE, crop it to fit into this */
			last_pte_idx = 1023; /* 1024 PTE entries, so 1023 is the last one */
			size_inside_pte = MALI_MMU_ADDRESS(i + 1, 0) - mali_address;
		}

		MALI_DEBUG_ASSERT_POINTER(session_data->page_entries_mapped[i]);
		MALI_DEBUG_ASSERT(0 != session_data->page_entries_usage_count[i]);
		MALI_DEBUG_PRINT(4, ("PDE %d: zapping entries %d through %d, address 0x%08X, size 0x%08X, left 0x%08X (page table at 0x%08X)\n",
		                     i, first_pte_idx, last_pte_idx, mali_address, size_inside_pte, left,
		                     MALI_MMU_ENTRY_ADDRESS(_mali_osk_mem_ioread32(session_data->page_directory_mapped, i * sizeof(u32)))));

		session_data->page_entries_usage_count[i]--;

		if (0 == session_data->page_entries_usage_count[i])
		{
			MALI_DEBUG_PRINT(4, ("Releasing page table as this is the last reference\n"));
			/* last reference removed, no need to zero out each PTE  */
			mali_mmu_release_table_page(MALI_MMU_ENTRY_ADDRESS(_mali_osk_mem_ioread32(session_data->page_directory_mapped, i * sizeof(u32))));
			session_data->page_entries_mapped[i] = NULL;
			_mali_osk_mem_iowrite32(session_data->page_directory_mapped, i * sizeof(u32), 0); /* mark as not present in the page directory */
#if defined USING_MALI400_L2_CACHE
			page_dir_updated = 1;
#endif
		}
		else
		{
			int j;

			for (j = first_pte_idx; j <= last_pte_idx; j++)
			{
				_mali_osk_mem_iowrite32(session_data->page_entries_mapped[i], j * sizeof(u32), 0);
			}

#if defined USING_MALI400_L2_CACHE
			if (1 == has_active_mmus)
			{
				/* Invalidate the page we've just modified */
				mali_kernel_l2_cache_invalidate_page( _mali_osk_mem_ioread32(session_data->page_directory_mapped, i*sizeof(u32)) & ~MALI_MMU_FLAGS_MASK);
			}
#endif
		}
		left -= size_inside_pte;
		mali_address += size_inside_pte;
	}

#if defined USING_MALI400_L2_CACHE
	if ((1 == page_dir_updated) && (1== has_active_mmus))
	{
		/* The page directory was also updated */
		mali_kernel_l2_cache_invalidate_page(session_data->page_directory);
	}
#endif
}

static _mali_osk_errcode_t mali_address_manager_map(mali_memory_allocation * descriptor, u32 offset, u32 *phys_addr, u32 size)
{
	memory_session * session_data;
	u32 mali_address;
	u32 mali_address_end;
	u32 current_phys_addr;
#if defined USING_MALI400_L2_CACHE
	int has_active_mmus = 0;
#endif

    MALI_DEBUG_ASSERT_POINTER(descriptor);

	MALI_DEBUG_ASSERT_POINTER( phys_addr );

	current_phys_addr = *phys_addr;

	session_data = (memory_session*)descriptor->mali_addr_mapping_info;
    MALI_DEBUG_ASSERT_POINTER(session_data);

	mali_address = descriptor->mali_address + offset;
	mali_address_end = descriptor->mali_address + offset + size;

#if defined USING_MALI400_L2_CACHE
	if (0 == _mali_osk_list_empty(&session_data->active_mmus))
	{
		/*
		 * We have active MMUs, so we are probably in the process of alocating more memory for a suspended GP job (PLBU heap)
		 * From Mali-400 MP r1p0, MMU page directory/tables are also cached by the Mali L2 cache, thus we need to invalidate the page tables
		 * from the L2 cache when we have allocated more heap memory.
		 * We only need to do this when we have an active MMU, because we otherwise invalidate the entire Mali L2 cache before at job start
		 */
		has_active_mmus = 1;
	}
#endif

	MALI_DEBUG_PRINT(6, ("Mali map: mapping 0x%08X to Mali address 0x%08X length 0x%08X\n", current_phys_addr, mali_address, size));

    MALI_DEBUG_ASSERT_POINTER(session_data->page_entries_mapped);

	for ( ; mali_address < mali_address_end; mali_address += MALI_MMU_PAGE_SIZE, current_phys_addr += MALI_MMU_PAGE_SIZE)
	{
        MALI_DEBUG_ASSERT_POINTER(session_data->page_entries_mapped[MALI_MMU_PDE_ENTRY(mali_address)]);
		_mali_osk_mem_iowrite32_relaxed(session_data->page_entries_mapped[MALI_MMU_PDE_ENTRY(mali_address)], MALI_MMU_PTE_ENTRY(mali_address) * sizeof(u32), current_phys_addr | MALI_MMU_FLAGS_WRITE_PERMISSION | MALI_MMU_FLAGS_READ_PERMISSION | MALI_MMU_FLAGS_PRESENT);
	}
	_mali_osk_write_mem_barrier();

#if defined USING_MALI400_L2_CACHE
	if (1 == has_active_mmus)
	{
		int i;
		const int first_pde_idx = MALI_MMU_PDE_ENTRY(mali_address);
		const int last_pde_idx = MALI_MMU_PDE_ENTRY(mali_address_end - 1);

		/*
		 * Invalidate the updated page table(s), incase they have been used for something
		 * else since last job start (invalidation of entire Mali L2 cache)
		 */
		for (i = first_pde_idx; i <= last_pde_idx; i++)
		{
			mali_kernel_l2_cache_invalidate_page( _mali_osk_mem_ioread32(session_data->page_directory_mapped, i*sizeof(u32)) & ~MALI_MMU_FLAGS_MASK);
		}
	}
#endif

	MALI_SUCCESS;
}

/* This handler registered to mali_mmap for MMU builds */
_mali_osk_errcode_t _mali_ukk_mem_mmap( _mali_uk_mem_mmap_s *args )
{
	struct mali_session_data * mali_session_data;
	mali_memory_allocation * descriptor;
	memory_session * session_data;

	/* validate input */
	if (NULL == args) { MALI_DEBUG_PRINT(3,("mali_ukk_mem_mmap: args was NULL\n")); MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS); }

	/* Unpack arguments */
	mali_session_data = (struct mali_session_data *)args->ctx;

	if (NULL == mali_session_data) { MALI_DEBUG_PRINT(3,("mali_ukk_mem_mmap: mali_session data was NULL\n")); MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS); }

	MALI_DEBUG_ASSERT( mali_subsystem_memory_id >= 0 );

	session_data = mali_kernel_session_manager_slot_get(mali_session_data, mali_subsystem_memory_id);
	/* validate input */
	if (NULL == session_data) { MALI_DEBUG_PRINT(3,("mali_ukk_mem_mmap: session data was NULL\n")); MALI_ERROR(_MALI_OSK_ERR_FAULT); }

	descriptor = (mali_memory_allocation*) _mali_osk_calloc( 1, sizeof(mali_memory_allocation) );
	if (NULL == descriptor) { MALI_DEBUG_PRINT(3,("mali_ukk_mem_mmap: descriptor was NULL\n")); MALI_ERROR(_MALI_OSK_ERR_NOMEM); }

	descriptor->size = args->size;
	descriptor->mali_address = args->phys_addr;
	descriptor->mali_addr_mapping_info = (void*)session_data;

	descriptor->process_addr_mapping_info = args->ukk_private; /* save to be used during physical manager callback */
	descriptor->flags = MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE;
    descriptor->lock = session_data->lock;
	_mali_osk_list_init( &descriptor->list );

	_mali_osk_lock_wait(session_data->lock, _MALI_OSK_LOCKMODE_RW);

	if (0 == mali_allocation_engine_allocate_memory(memory_engine, descriptor, physical_memory_allocators, &session_data->memory_head))
	{
		mali_kernel_memory_mmu * mmu, * temp_mmu;

		_MALI_OSK_LIST_FOREACHENTRY(mmu, temp_mmu, &session_data->active_mmus, mali_kernel_memory_mmu, session_link)
		{
			/* no need to lock the MMU as we own it already */
			MALI_DEBUG_PRINT(5, ("Zapping the cache of mmu %s as it's using the page table we have updated\n", mmu->description));

			_mali_osk_lock_wait(mmu->lock, _MALI_OSK_LOCKMODE_RW);

			mali_mmu_enable_stall(mmu); /* this might fail, but ZAP should work anyway... */
			mali_mmu_register_write(mmu, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ZAP_CACHE);
			mali_mmu_disable_stall(mmu);

			_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);
		}

       	_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);

        /* All ok, write out any information generated from this call */
        args->mapping = descriptor->mapping;
        args->cookie = (u32)descriptor;

        MALI_DEBUG_PRINT(7, ("MMAP OK\n"));
		/* All done */
		MALI_SUCCESS;
	}
	else
	{
       	_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);
		/* OOM, but not a fatal error */
		MALI_DEBUG_PRINT(4, ("Memory allocation failure, OOM\n"));
		_mali_osk_free(descriptor);
		/* Linux will free the CPU address allocation, userspace client the Mali address allocation */
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
}

static _mali_osk_errcode_t _mali_ukk_mem_munmap_internal( _mali_uk_mem_munmap_s *args )
{
	memory_session * session_data;
	mali_kernel_memory_mmu * mmu, * temp_mmu;
	mali_memory_allocation * descriptor;

	descriptor = (mali_memory_allocation *)args->cookie;
    MALI_DEBUG_ASSERT_POINTER(descriptor);

    /** @note args->context unused; we use the memory_session from the cookie */
    /* args->mapping and args->size are also discarded. They are only necessary
    for certain do_munmap implementations. However, they could be used to check the
    descriptor at this point. */

    session_data = (memory_session*)descriptor->mali_addr_mapping_info;
    MALI_DEBUG_ASSERT_POINTER(session_data);

	/* Stall the MMU(s) which is using the address space we're operating on.
	 * Note that active_mmus must be sorted in order of ID to avoid a mutex
	 * ordering violation.
	 */
	_MALI_OSK_LIST_FOREACHENTRY(mmu, temp_mmu, &session_data->active_mmus, mali_kernel_memory_mmu, session_link)
	{
		u32 status;
		status = mali_mmu_register_read(mmu, MALI_MMU_REGISTER_STATUS);
		if ( MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE == (status & MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE) ) {
			MALI_DEBUG_PRINT(2, ("Stopped stall attempt for mmu with id %d since it is in page fault mode.\n", mmu->id));
			continue;
		}
		_mali_osk_lock_wait(mmu->lock, _MALI_OSK_LOCKMODE_RW);

		/*
		 * If we're unable to stall, then make sure we tell our caller that,
		 * the caller should then release the session lock for a while,
		 * then this function again.
		 * This function will fail if we're in page fault mode, and to get
		 * out of page fault mode, the page fault handler must be able to
		 * take the session lock.
		 */
		if (!mali_mmu_enable_stall(mmu))
		{
			_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);
			return _MALI_OSK_ERR_BUSY;
		}

		_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);
	}

	_MALI_OSK_LIST_FOREACHENTRY(mmu, temp_mmu, &session_data->active_mmus, mali_kernel_memory_mmu, session_link)
	{
		_mali_osk_lock_wait(mmu->lock, _MALI_OSK_LOCKMODE_RW);
	}

	/* This function also removes the memory from the session's memory list */
	mali_allocation_engine_release_memory(memory_engine, descriptor);
	_mali_osk_free(descriptor);

	/* any L2 maintenance was done during mali_allocation_engine_release_memory */
	/* the session is locked, so the active mmu list should be the same */
	/* zap the TLB and resume operation */
	_MALI_OSK_LIST_FOREACHENTRY(mmu, temp_mmu, &session_data->active_mmus, mali_kernel_memory_mmu, session_link)
	{
		mali_mmu_register_write(mmu, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ZAP_CACHE);
		mali_mmu_disable_stall(mmu);

		_mali_osk_lock_signal(mmu->lock, _MALI_OSK_LOCKMODE_RW);
	}

	return _MALI_OSK_ERR_OK;
}

/* Handler for unmapping memory for MMU builds */
_mali_osk_errcode_t _mali_ukk_mem_munmap( _mali_uk_mem_munmap_s *args )
{
	mali_memory_allocation * descriptor;
	_mali_osk_lock_t *descriptor_lock;
	_mali_osk_errcode_t err;

	descriptor = (mali_memory_allocation *)args->cookie;
    MALI_DEBUG_ASSERT_POINTER(descriptor);

    /** @note args->context unused; we use the memory_session from the cookie */
    /* args->mapping and args->size are also discarded. They are only necessary
    for certain do_munmap implementations. However, they could be used to check the
    descriptor at this point. */

    MALI_DEBUG_ASSERT_POINTER((memory_session*)descriptor->mali_addr_mapping_info);
    
	descriptor_lock = descriptor->lock; /* should point to the session data lock... */

	err = _MALI_OSK_ERR_BUSY;
	while (err == _MALI_OSK_ERR_BUSY)
	{
		if (descriptor_lock)
		{
			_mali_osk_lock_wait( descriptor_lock, _MALI_OSK_LOCKMODE_RW );
		}

		err = _mali_ukk_mem_munmap_internal( args );

		if (descriptor_lock)
		{
			_mali_osk_lock_signal( descriptor_lock, _MALI_OSK_LOCKMODE_RW );
		}
		
		if (err == _MALI_OSK_ERR_BUSY)
		{
			/*
			 * Reason for this;
			 * We where unable to stall the MMU, probably because we are in page fault handling.
			 * Sleep for a while with the session lock released, then try again.
			 * Abnormal termination of programs with running Mali jobs is a normal reason for this.
			 */
			_mali_osk_time_ubusydelay(10);
		}
	}

	return err;
}

/* Is called when the rendercore wants the mmu to give an interrupt */
static void mali_mmu_probe_irq_trigger(mali_kernel_memory_mmu * mmu)
{
    MALI_DEBUG_PRINT(2, ("mali_mmu_probe_irq_trigger\n"));
    mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_RAWSTAT, MALI_MMU_INTERRUPT_PAGE_FAULT|MALI_MMU_INTERRUPT_READ_BUS_ERROR);
}

/* Is called when the irq probe wants the mmu to acknowledge an interrupt from the hw */
static _mali_osk_errcode_t mali_mmu_probe_irq_acknowledge(mali_kernel_memory_mmu * mmu)
{
	u32 int_stat;

    int_stat = mali_mmu_register_read(mmu, MALI_MMU_REGISTER_INT_STATUS);

    MALI_DEBUG_PRINT(2, ("mali_mmu_probe_irq_acknowledge: intstat 0x%x\n", int_stat));
    if (int_stat & MALI_MMU_INTERRUPT_PAGE_FAULT)
    {
	    MALI_DEBUG_PRINT(2, ("Probe: Page fault detect: PASSED\n"));
	    mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_CLEAR, MALI_MMU_INTERRUPT_PAGE_FAULT);
    }
    else MALI_DEBUG_PRINT(1, ("Probe: Page fault detect: FAILED\n"));

    if (int_stat & MALI_MMU_INTERRUPT_READ_BUS_ERROR)
    {
	    MALI_DEBUG_PRINT(2, ("Probe: Bus read error detect: PASSED\n"));
	    mali_mmu_register_write(mmu, MALI_MMU_REGISTER_INT_CLEAR, MALI_MMU_INTERRUPT_READ_BUS_ERROR);
    }
    else MALI_DEBUG_PRINT(1, ("Probe: Bus read error detect: FAILED\n"));

    if ( (int_stat & (MALI_MMU_INTERRUPT_PAGE_FAULT|MALI_MMU_INTERRUPT_READ_BUS_ERROR)) ==
                     (MALI_MMU_INTERRUPT_PAGE_FAULT|MALI_MMU_INTERRUPT_READ_BUS_ERROR))
    {
        MALI_SUCCESS;
    }

    MALI_ERROR(_MALI_OSK_ERR_FAULT);
}

struct dump_info
{
	u32 buffer_left;
	u32 register_writes_size;
	u32 page_table_dump_size;
	u32 *buffer;
};

static _mali_osk_errcode_t writereg(u32 where, u32 what, const char * comment, struct dump_info * info, int dump_to_serial)
{
	if (dump_to_serial) MALI_DEBUG_PRINT(1, ("writereg %08X %08X # %s\n", where, what, comment));

	if (NULL != info)
	{
		info->register_writes_size += sizeof(u32)*2; /* two 32-bit words */

		if (NULL != info->buffer)
		{
			/* check that we have enough space */
			if (info->buffer_left < sizeof(u32)*2) MALI_ERROR(_MALI_OSK_ERR_NOMEM);

			*info->buffer = where;
			info->buffer++;

			*info->buffer = what;
			info->buffer++;

			info->buffer_left -= sizeof(u32)*2;
		}
	}

	MALI_SUCCESS;
}

static _mali_osk_errcode_t dump_page(mali_io_address page, u32 phys_addr, struct dump_info * info, int dump_to_serial)
{
	if (dump_to_serial)
	{
		int i;
		for (i = 0; i < 256; i++)
		{
			MALI_DEBUG_PRINT(1, ("%08X: %08X %08X %08X %08X\n", phys_addr + 16*i, _mali_osk_mem_ioread32(page, (i*4 + 0) * sizeof(u32)),
			                                                                      _mali_osk_mem_ioread32(page, (i*4 + 1) * sizeof(u32)),
			                                                                      _mali_osk_mem_ioread32(page, (i*4 + 2) * sizeof(u32)),
			                                                                      _mali_osk_mem_ioread32(page, (i*4 + 3) * sizeof(u32))));

		}
	}

	if (NULL != info)
	{
		/* 4096 for the page and 4 bytes for the address */
		const u32 page_size_in_elements = MALI_MMU_PAGE_SIZE / 4;
		const u32 page_size_in_bytes = MALI_MMU_PAGE_SIZE;
		const u32 dump_size_in_bytes = MALI_MMU_PAGE_SIZE + 4;

		info->page_table_dump_size += dump_size_in_bytes;

		if (NULL != info->buffer)
		{
			if (info->buffer_left < dump_size_in_bytes) MALI_ERROR(_MALI_OSK_ERR_NOMEM);

			*info->buffer = phys_addr;
			info->buffer++;

			_mali_osk_memcpy(info->buffer, page, page_size_in_bytes);
			info->buffer += page_size_in_elements;

			info->buffer_left -= dump_size_in_bytes;
		}
	}

	MALI_SUCCESS;
}

static _mali_osk_errcode_t dump_mmu_page_table(memory_session * session_data, struct dump_info * info)
{
	MALI_DEBUG_ASSERT_POINTER(session_data);
	MALI_DEBUG_ASSERT_POINTER(info);

	if (NULL != session_data->page_directory_mapped)
	{
		int i;

		MALI_CHECK_NO_ERROR(
            dump_page(session_data->page_directory_mapped, session_data->page_directory, info, 0)
            );

		for (i = 0; i < 1024; i++)
		{
			if (NULL != session_data->page_entries_mapped[i])
			{
				MALI_CHECK_NO_ERROR(
                    dump_page(session_data->page_entries_mapped[i], _mali_osk_mem_ioread32(session_data->page_directory_mapped, i * sizeof(u32)) & ~MALI_MMU_FLAGS_MASK, info, 0)
                    );
			}
		}
	}

	MALI_SUCCESS;
}

static _mali_osk_errcode_t dump_mmu_registers(memory_session * session_data, struct dump_info * info)
{
	MALI_CHECK_NO_ERROR(writereg(0x00000000, session_data->page_directory, "set the page directory address", info, 0));
	MALI_CHECK_NO_ERROR(writereg(0x00000008, 4, "zap???", info, 0));
	MALI_CHECK_NO_ERROR(writereg(0x00000008, 0, "enable paging", info, 0));
    MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_query_mmu_page_table_dump_size( _mali_uk_query_mmu_page_table_dump_size_s *args )
{
	struct dump_info info = { 0, 0, 0, NULL };
	memory_session * session_data;

    MALI_DEBUG_ASSERT_POINTER(args);
  	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

    session_data = (memory_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_memory_id);

    MALI_CHECK_NO_ERROR(dump_mmu_registers(session_data, &info));
	MALI_CHECK_NO_ERROR(dump_mmu_page_table(session_data, &info));
	args->size = info.register_writes_size + info.page_table_dump_size;
    MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_dump_mmu_page_table( _mali_uk_dump_mmu_page_table_s * args )
{
	struct dump_info info = { 0, 0, 0, NULL };
	memory_session * session_data;

  	MALI_DEBUG_ASSERT_POINTER(args);
  	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
    MALI_CHECK_NON_NULL(args->buffer, _MALI_OSK_ERR_INVALID_ARGS);

    session_data = (memory_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_memory_id);

	info.buffer_left = args->size;
	info.buffer = args->buffer;

	args->register_writes = info.buffer;
	MALI_CHECK_NO_ERROR(dump_mmu_registers(session_data, &info));

	args->page_table_dump = info.buffer;
	MALI_CHECK_NO_ERROR(dump_mmu_page_table(session_data, &info));

	args->register_writes_size = info.register_writes_size;
	args->page_table_dump_size = info.page_table_dump_size;

    MALI_SUCCESS;
}

/**
 * Stub function to satisfy UDD interface exclusion requirement.
 * This is because the Base code compiles in \b both MMU and non-MMU calls,
 * so both sets must be declared (but the 'unused' set may be stub)
 */
_mali_osk_errcode_t _mali_ukk_get_big_block( _mali_uk_get_big_block_s *args )
{
	MALI_IGNORE( args );
	return _MALI_OSK_ERR_FAULT;
}

/**
 * Stub function to satisfy UDD interface exclusion requirement.
 * This is because the Base code compiles in \b both MMU and non-MMU calls,
 * so both sets must be declared (but the 'unused' set may be stub)
 */
_mali_osk_errcode_t _mali_ukk_free_big_block( _mali_uk_free_big_block_s *args )
{
	MALI_IGNORE( args );
	return _MALI_OSK_ERR_FAULT;
}

u32 _mali_ukk_report_memory_usage(void)
{
	return mali_allocation_engine_memory_usage(physical_memory_allocators);
}
