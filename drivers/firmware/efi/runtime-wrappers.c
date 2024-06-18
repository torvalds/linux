// SPDX-License-Identifier: GPL-2.0-only
/*
 * runtime-wrappers.c - Runtime Services function call wrappers
 *
 * Implementation summary:
 * -----------------------
 * 1. When user/kernel thread requests to execute efi_runtime_service(),
 * enqueue work to efi_rts_wq.
 * 2. Caller thread waits for completion until the work is finished
 * because it's dependent on the return status and execution of
 * efi_runtime_service().
 * For instance, get_variable() and get_next_variable().
 *
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * Split off from arch/x86/platform/efi/efi.c
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999-2002 Hewlett-Packard Co.
 * Copyright (C) 2005-2008 Intel Co.
 * Copyright (C) 2013 SuSE Labs
 */

#define pr_fmt(fmt)	"efi: " fmt

#include <linux/bug.h>
#include <linux/efi.h>
#include <linux/irqflags.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/stringify.h>
#include <linux/workqueue.h>
#include <linux/completion.h>

#include <asm/efi.h>

/*
 * Wrap around the new efi_call_virt_generic() macros so that the
 * code doesn't get too cluttered:
 */
#define efi_call_virt(f, args...)   \
	arch_efi_call_virt(efi.runtime, f, args)

union efi_rts_args {
	struct {
		efi_time_t 	*time;
		efi_time_cap_t	*capabilities;
	} GET_TIME;

	struct {
		efi_time_t	*time;
	} SET_TIME;

	struct {
		efi_bool_t	*enabled;
		efi_bool_t	*pending;
		efi_time_t	*time;
	} GET_WAKEUP_TIME;

	struct {
		efi_bool_t	enable;
		efi_time_t	*time;
	} SET_WAKEUP_TIME;

	struct {
		efi_char16_t	*name;
		efi_guid_t	*vendor;
		u32		*attr;
		unsigned long	*data_size;
		void		*data;
	} GET_VARIABLE;

	struct {
		unsigned long	*name_size;
		efi_char16_t	*name;
		efi_guid_t 	*vendor;
	} GET_NEXT_VARIABLE;

	struct {
		efi_char16_t	*name;
		efi_guid_t	*vendor;
		u32		attr;
		unsigned long	data_size;
		void		*data;
	} SET_VARIABLE;

	struct {
		u32		attr;
		u64		*storage_space;
		u64		*remaining_space;
		u64		*max_variable_size;
	} QUERY_VARIABLE_INFO;

	struct {
		u32		*high_count;
	} GET_NEXT_HIGH_MONO_COUNT;

	struct {
		efi_capsule_header_t **capsules;
		unsigned long	count;
		unsigned long	sg_list;
	} UPDATE_CAPSULE;

	struct {
		efi_capsule_header_t **capsules;
		unsigned long	count;
		u64		*max_size;
		int		*reset_type;
	} QUERY_CAPSULE_CAPS;

	struct {
		efi_status_t	(__efiapi *acpi_prm_handler)(u64, void *);
		u64		param_buffer_addr;
		void		*context;
	} ACPI_PRM_HANDLER;
};

struct efi_runtime_work efi_rts_work;

/*
 * efi_queue_work:	Queue EFI runtime service call and wait for completion
 * @_rts:		EFI runtime service function identifier
 * @_args:		Arguments to pass to the EFI runtime service
 *
 * Accesses to efi_runtime_services() are serialized by a binary
 * semaphore (efi_runtime_lock) and caller waits until the work is
 * finished, hence _only_ one work is queued at a time and the caller
 * thread waits for completion.
 */
#define efi_queue_work(_rts, _args...)					\
	__efi_queue_work(EFI_ ## _rts,					\
			 &(union efi_rts_args){ ._rts = { _args }})

#ifndef arch_efi_save_flags
#define arch_efi_save_flags(state_flags)	local_save_flags(state_flags)
#define arch_efi_restore_flags(state_flags)	local_irq_restore(state_flags)
#endif

unsigned long efi_call_virt_save_flags(void)
{
	unsigned long flags;

	arch_efi_save_flags(flags);
	return flags;
}

void efi_call_virt_check_flags(unsigned long flags, const void *caller)
{
	unsigned long cur_flags, mismatch;

	cur_flags = efi_call_virt_save_flags();

	mismatch = flags ^ cur_flags;
	if (!WARN_ON_ONCE(mismatch & ARCH_EFI_IRQ_FLAGS_MASK))
		return;

	add_taint(TAINT_FIRMWARE_WORKAROUND, LOCKDEP_NOW_UNRELIABLE);
	pr_err_ratelimited(FW_BUG "IRQ flags corrupted (0x%08lx=>0x%08lx) by EFI call from %pS\n",
			   flags, cur_flags, caller ?: __builtin_return_address(0));
	arch_efi_restore_flags(flags);
}

/*
 * According to section 7.1 of the UEFI spec, Runtime Services are not fully
 * reentrant, and there are particular combinations of calls that need to be
 * serialized. (source: UEFI Specification v2.4A)
 *
 * Table 31. Rules for Reentry Into Runtime Services
 * +------------------------------------+-------------------------------+
 * | If previous call is busy in	| Forbidden to call		|
 * +------------------------------------+-------------------------------+
 * | Any				| SetVirtualAddressMap()	|
 * +------------------------------------+-------------------------------+
 * | ConvertPointer()			| ConvertPointer()		|
 * +------------------------------------+-------------------------------+
 * | SetVariable()			| ResetSystem()			|
 * | UpdateCapsule()			|				|
 * | SetTime()				|				|
 * | SetWakeupTime()			|				|
 * | GetNextHighMonotonicCount()	|				|
 * +------------------------------------+-------------------------------+
 * | GetVariable()			| GetVariable()			|
 * | GetNextVariableName()		| GetNextVariableName()		|
 * | SetVariable()			| SetVariable()			|
 * | QueryVariableInfo()		| QueryVariableInfo()		|
 * | UpdateCapsule()			| UpdateCapsule()		|
 * | QueryCapsuleCapabilities()		| QueryCapsuleCapabilities()	|
 * | GetNextHighMonotonicCount()	| GetNextHighMonotonicCount()	|
 * +------------------------------------+-------------------------------+
 * | GetTime()				| GetTime()			|
 * | SetTime()				| SetTime()			|
 * | GetWakeupTime()			| GetWakeupTime()		|
 * | SetWakeupTime()			| SetWakeupTime()		|
 * +------------------------------------+-------------------------------+
 *
 * Due to the fact that the EFI pstore may write to the variable store in
 * interrupt context, we need to use a lock for at least the groups that
 * contain SetVariable() and QueryVariableInfo(). That leaves little else, as
 * none of the remaining functions are actually ever called at runtime.
 * So let's just use a single lock to serialize all Runtime Services calls.
 */
static DEFINE_SEMAPHORE(efi_runtime_lock, 1);

/*
 * Expose the EFI runtime lock to the UV platform
 */
#ifdef CONFIG_X86_UV
extern struct semaphore __efi_uv_runtime_lock __alias(efi_runtime_lock);
#endif

/*
 * Calls the appropriate efi_runtime_service() with the appropriate
 * arguments.
 */
static void __nocfi efi_call_rts(struct work_struct *work)
{
	const union efi_rts_args *args = efi_rts_work.args;
	efi_status_t status = EFI_NOT_FOUND;
	unsigned long flags;

	arch_efi_call_virt_setup();
	flags = efi_call_virt_save_flags();

	switch (efi_rts_work.efi_rts_id) {
	case EFI_GET_TIME:
		status = efi_call_virt(get_time,
				       args->GET_TIME.time,
				       args->GET_TIME.capabilities);
		break;
	case EFI_SET_TIME:
		status = efi_call_virt(set_time,
				       args->SET_TIME.time);
		break;
	case EFI_GET_WAKEUP_TIME:
		status = efi_call_virt(get_wakeup_time,
				       args->GET_WAKEUP_TIME.enabled,
				       args->GET_WAKEUP_TIME.pending,
				       args->GET_WAKEUP_TIME.time);
		break;
	case EFI_SET_WAKEUP_TIME:
		status = efi_call_virt(set_wakeup_time,
				       args->SET_WAKEUP_TIME.enable,
				       args->SET_WAKEUP_TIME.time);
		break;
	case EFI_GET_VARIABLE:
		status = efi_call_virt(get_variable,
				       args->GET_VARIABLE.name,
				       args->GET_VARIABLE.vendor,
				       args->GET_VARIABLE.attr,
				       args->GET_VARIABLE.data_size,
				       args->GET_VARIABLE.data);
		break;
	case EFI_GET_NEXT_VARIABLE:
		status = efi_call_virt(get_next_variable,
				       args->GET_NEXT_VARIABLE.name_size,
				       args->GET_NEXT_VARIABLE.name,
				       args->GET_NEXT_VARIABLE.vendor);
		break;
	case EFI_SET_VARIABLE:
		status = efi_call_virt(set_variable,
				       args->SET_VARIABLE.name,
				       args->SET_VARIABLE.vendor,
				       args->SET_VARIABLE.attr,
				       args->SET_VARIABLE.data_size,
				       args->SET_VARIABLE.data);
		break;
	case EFI_QUERY_VARIABLE_INFO:
		status = efi_call_virt(query_variable_info,
				       args->QUERY_VARIABLE_INFO.attr,
				       args->QUERY_VARIABLE_INFO.storage_space,
				       args->QUERY_VARIABLE_INFO.remaining_space,
				       args->QUERY_VARIABLE_INFO.max_variable_size);
		break;
	case EFI_GET_NEXT_HIGH_MONO_COUNT:
		status = efi_call_virt(get_next_high_mono_count,
				       args->GET_NEXT_HIGH_MONO_COUNT.high_count);
		break;
	case EFI_UPDATE_CAPSULE:
		status = efi_call_virt(update_capsule,
				       args->UPDATE_CAPSULE.capsules,
				       args->UPDATE_CAPSULE.count,
				       args->UPDATE_CAPSULE.sg_list);
		break;
	case EFI_QUERY_CAPSULE_CAPS:
		status = efi_call_virt(query_capsule_caps,
				       args->QUERY_CAPSULE_CAPS.capsules,
				       args->QUERY_CAPSULE_CAPS.count,
				       args->QUERY_CAPSULE_CAPS.max_size,
				       args->QUERY_CAPSULE_CAPS.reset_type);
		break;
	case EFI_ACPI_PRM_HANDLER:
#ifdef CONFIG_ACPI_PRMT
		status = arch_efi_call_virt(args, ACPI_PRM_HANDLER.acpi_prm_handler,
					    args->ACPI_PRM_HANDLER.param_buffer_addr,
					    args->ACPI_PRM_HANDLER.context);
		break;
#endif
	default:
		/*
		 * Ideally, we should never reach here because a caller of this
		 * function should have put the right efi_runtime_service()
		 * function identifier into efi_rts_work->efi_rts_id
		 */
		pr_err("Requested executing invalid EFI Runtime Service.\n");
	}

	efi_call_virt_check_flags(flags, efi_rts_work.caller);
	arch_efi_call_virt_teardown();

	efi_rts_work.status = status;
	complete(&efi_rts_work.efi_rts_comp);
}

static efi_status_t __efi_queue_work(enum efi_rts_ids id,
				     union efi_rts_args *args)
{
	efi_rts_work.efi_rts_id = id;
	efi_rts_work.args = args;
	efi_rts_work.caller = __builtin_return_address(0);
	efi_rts_work.status = EFI_ABORTED;

	if (!efi_enabled(EFI_RUNTIME_SERVICES)) {
		pr_warn_once("EFI Runtime Services are disabled!\n");
		efi_rts_work.status = EFI_DEVICE_ERROR;
		goto exit;
	}

	init_completion(&efi_rts_work.efi_rts_comp);
	INIT_WORK(&efi_rts_work.work, efi_call_rts);

	/*
	 * queue_work() returns 0 if work was already on queue,
	 * _ideally_ this should never happen.
	 */
	if (queue_work(efi_rts_wq, &efi_rts_work.work))
		wait_for_completion(&efi_rts_work.efi_rts_comp);
	else
		pr_err("Failed to queue work to efi_rts_wq.\n");

	WARN_ON_ONCE(efi_rts_work.status == EFI_ABORTED);
exit:
	efi_rts_work.efi_rts_id = EFI_NONE;
	return efi_rts_work.status;
}

static efi_status_t virt_efi_get_time(efi_time_t *tm, efi_time_cap_t *tc)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(GET_TIME, tm, tc);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t virt_efi_set_time(efi_time_t *tm)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(SET_TIME, tm);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t virt_efi_get_wakeup_time(efi_bool_t *enabled,
					     efi_bool_t *pending,
					     efi_time_t *tm)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(GET_WAKEUP_TIME, enabled, pending, tm);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t virt_efi_set_wakeup_time(efi_bool_t enabled, efi_time_t *tm)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(SET_WAKEUP_TIME, enabled, tm);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t virt_efi_get_variable(efi_char16_t *name,
					  efi_guid_t *vendor,
					  u32 *attr,
					  unsigned long *data_size,
					  void *data)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(GET_VARIABLE, name, vendor, attr, data_size,
				data);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t virt_efi_get_next_variable(unsigned long *name_size,
					       efi_char16_t *name,
					       efi_guid_t *vendor)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(GET_NEXT_VARIABLE, name_size, name, vendor);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t virt_efi_set_variable(efi_char16_t *name,
					  efi_guid_t *vendor,
					  u32 attr,
					  unsigned long data_size,
					  void *data)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(SET_VARIABLE, name, vendor, attr, data_size,
				data);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t __nocfi
virt_efi_set_variable_nb(efi_char16_t *name, efi_guid_t *vendor, u32 attr,
			 unsigned long data_size, void *data)
{
	efi_status_t status;

	if (down_trylock(&efi_runtime_lock))
		return EFI_NOT_READY;

	status = efi_call_virt_pointer(efi.runtime, set_variable, name, vendor,
				       attr, data_size, data);
	up(&efi_runtime_lock);
	return status;
}


static efi_status_t virt_efi_query_variable_info(u32 attr,
						 u64 *storage_space,
						 u64 *remaining_space,
						 u64 *max_variable_size)
{
	efi_status_t status;

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(QUERY_VARIABLE_INFO, attr, storage_space,
				remaining_space, max_variable_size);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t __nocfi
virt_efi_query_variable_info_nb(u32 attr, u64 *storage_space,
				u64 *remaining_space, u64 *max_variable_size)
{
	efi_status_t status;

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	if (down_trylock(&efi_runtime_lock))
		return EFI_NOT_READY;

	status = efi_call_virt_pointer(efi.runtime, query_variable_info, attr,
				       storage_space, remaining_space,
				       max_variable_size);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t virt_efi_get_next_high_mono_count(u32 *count)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(GET_NEXT_HIGH_MONO_COUNT, count);
	up(&efi_runtime_lock);
	return status;
}

static void __nocfi
virt_efi_reset_system(int reset_type, efi_status_t status,
		      unsigned long data_size, efi_char16_t *data)
{
	if (down_trylock(&efi_runtime_lock)) {
		pr_warn("failed to invoke the reset_system() runtime service:\n"
			"could not get exclusive access to the firmware\n");
		return;
	}

	arch_efi_call_virt_setup();
	efi_rts_work.efi_rts_id = EFI_RESET_SYSTEM;
	arch_efi_call_virt(efi.runtime, reset_system, reset_type, status,
			   data_size, data);
	arch_efi_call_virt_teardown();

	up(&efi_runtime_lock);
}

static efi_status_t virt_efi_update_capsule(efi_capsule_header_t **capsules,
					    unsigned long count,
					    unsigned long sg_list)
{
	efi_status_t status;

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(UPDATE_CAPSULE, capsules, count, sg_list);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t virt_efi_query_capsule_caps(efi_capsule_header_t **capsules,
						unsigned long count,
						u64 *max_size,
						int *reset_type)
{
	efi_status_t status;

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(QUERY_CAPSULE_CAPS, capsules, count,
				max_size, reset_type);
	up(&efi_runtime_lock);
	return status;
}

void __init efi_native_runtime_setup(void)
{
	efi.get_time			    = virt_efi_get_time;
	efi.set_time			    = virt_efi_set_time;
	efi.get_wakeup_time		    = virt_efi_get_wakeup_time;
	efi.set_wakeup_time		    = virt_efi_set_wakeup_time;
	efi.get_variable		    = virt_efi_get_variable;
	efi.get_next_variable		    = virt_efi_get_next_variable;
	efi.set_variable		    = virt_efi_set_variable;
	efi.set_variable_nonblocking	    = virt_efi_set_variable_nb;
	efi.get_next_high_mono_count	    = virt_efi_get_next_high_mono_count;
	efi.reset_system 		    = virt_efi_reset_system;
	efi.query_variable_info		    = virt_efi_query_variable_info;
	efi.query_variable_info_nonblocking = virt_efi_query_variable_info_nb;
	efi.update_capsule		    = virt_efi_update_capsule;
	efi.query_capsule_caps		    = virt_efi_query_capsule_caps;
}

#ifdef CONFIG_ACPI_PRMT

efi_status_t
efi_call_acpi_prm_handler(efi_status_t (__efiapi *handler_addr)(u64, void *),
			  u64 param_buffer_addr, void *context)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_queue_work(ACPI_PRM_HANDLER, handler_addr,
				param_buffer_addr, context);
	up(&efi_runtime_lock);
	return status;
}

#endif
