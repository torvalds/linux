/*
 * runtime-wrappers.c - Runtime Services function call wrappers
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
 *
 * This file is released under the GPLv2.
 */

#define pr_fmt(fmt)	"efi: " fmt

#include <linux/bug.h>
#include <linux/efi.h>
#include <linux/irqflags.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/stringify.h>
#include <asm/efi.h>

/*
 * Wrap around the new efi_call_virt_generic() macros so that the
 * code doesn't get too cluttered:
 */
#define efi_call_virt(f, args...)   \
	efi_call_virt_pointer(efi.systab->runtime, f, args)
#define __efi_call_virt(f, args...) \
	__efi_call_virt_pointer(efi.systab->runtime, f, args)

void efi_call_virt_check_flags(unsigned long flags, const char *call)
{
	unsigned long cur_flags, mismatch;

	local_save_flags(cur_flags);

	mismatch = flags ^ cur_flags;
	if (!WARN_ON_ONCE(mismatch & ARCH_EFI_IRQ_FLAGS_MASK))
		return;

	add_taint(TAINT_FIRMWARE_WORKAROUND, LOCKDEP_NOW_UNRELIABLE);
	pr_err_ratelimited(FW_BUG "IRQ flags corrupted (0x%08lx=>0x%08lx) by EFI %s\n",
			   flags, cur_flags, call);
	local_irq_restore(flags);
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
static DEFINE_SEMAPHORE(efi_runtime_lock);

static efi_status_t virt_efi_get_time(efi_time_t *tm, efi_time_cap_t *tc)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_call_virt(get_time, tm, tc);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t virt_efi_set_time(efi_time_t *tm)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_call_virt(set_time, tm);
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
	status = efi_call_virt(get_wakeup_time, enabled, pending, tm);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t virt_efi_set_wakeup_time(efi_bool_t enabled, efi_time_t *tm)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_call_virt(set_wakeup_time, enabled, tm);
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
	status = efi_call_virt(get_variable, name, vendor, attr, data_size,
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
	status = efi_call_virt(get_next_variable, name_size, name, vendor);
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
	status = efi_call_virt(set_variable, name, vendor, attr, data_size,
			       data);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t
virt_efi_set_variable_nonblocking(efi_char16_t *name, efi_guid_t *vendor,
				  u32 attr, unsigned long data_size,
				  void *data)
{
	efi_status_t status;

	if (down_trylock(&efi_runtime_lock))
		return EFI_NOT_READY;

	status = efi_call_virt(set_variable, name, vendor, attr, data_size,
			       data);
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
	status = efi_call_virt(query_variable_info, attr, storage_space,
			       remaining_space, max_variable_size);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t
virt_efi_query_variable_info_nonblocking(u32 attr,
					 u64 *storage_space,
					 u64 *remaining_space,
					 u64 *max_variable_size)
{
	efi_status_t status;

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	if (down_trylock(&efi_runtime_lock))
		return EFI_NOT_READY;

	status = efi_call_virt(query_variable_info, attr, storage_space,
			       remaining_space, max_variable_size);
	up(&efi_runtime_lock);
	return status;
}

static efi_status_t virt_efi_get_next_high_mono_count(u32 *count)
{
	efi_status_t status;

	if (down_interruptible(&efi_runtime_lock))
		return EFI_ABORTED;
	status = efi_call_virt(get_next_high_mono_count, count);
	up(&efi_runtime_lock);
	return status;
}

static void virt_efi_reset_system(int reset_type,
				  efi_status_t status,
				  unsigned long data_size,
				  efi_char16_t *data)
{
	if (down_interruptible(&efi_runtime_lock)) {
		pr_warn("failed to invoke the reset_system() runtime service:\n"
			"could not get exclusive access to the firmware\n");
		return;
	}
	__efi_call_virt(reset_system, reset_type, status, data_size, data);
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
	status = efi_call_virt(update_capsule, capsules, count, sg_list);
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
	status = efi_call_virt(query_capsule_caps, capsules, count, max_size,
			       reset_type);
	up(&efi_runtime_lock);
	return status;
}

void efi_native_runtime_setup(void)
{
	efi.get_time = virt_efi_get_time;
	efi.set_time = virt_efi_set_time;
	efi.get_wakeup_time = virt_efi_get_wakeup_time;
	efi.set_wakeup_time = virt_efi_set_wakeup_time;
	efi.get_variable = virt_efi_get_variable;
	efi.get_next_variable = virt_efi_get_next_variable;
	efi.set_variable = virt_efi_set_variable;
	efi.set_variable_nonblocking = virt_efi_set_variable_nonblocking;
	efi.get_next_high_mono_count = virt_efi_get_next_high_mono_count;
	efi.reset_system = virt_efi_reset_system;
	efi.query_variable_info = virt_efi_query_variable_info;
	efi.query_variable_info_nonblocking = virt_efi_query_variable_info_nonblocking;
	efi.update_capsule = virt_efi_update_capsule;
	efi.query_capsule_caps = virt_efi_query_capsule_caps;
}
