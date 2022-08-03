// SPDX-License-Identifier: GPL-2.0+
/*
 * Originally from efivars.c
 *
 * Copyright (C) 2001,2003,2004 Dell <Matt_Domsch@dell.com>
 * Copyright (C) 2004 Intel Corporation <matthew.e.tolentino@intel.com>
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/efi.h>
#include <linux/ucs2_string.h>

/* Private pointer to registered efivars */
static struct efivars *__efivars;

static DEFINE_SEMAPHORE(efivars_lock);

efi_status_t check_var_size(u32 attributes, unsigned long size)
{
	const struct efivar_operations *fops;

	fops = __efivars->ops;

	if (!fops->query_variable_store)
		return EFI_UNSUPPORTED;

	return fops->query_variable_store(attributes, size, false);
}
EXPORT_SYMBOL_NS_GPL(check_var_size, EFIVAR);

efi_status_t check_var_size_nonblocking(u32 attributes, unsigned long size)
{
	const struct efivar_operations *fops;

	fops = __efivars->ops;

	if (!fops->query_variable_store)
		return EFI_UNSUPPORTED;

	return fops->query_variable_store(attributes, size, true);
}
EXPORT_SYMBOL_NS_GPL(check_var_size_nonblocking, EFIVAR);

/**
 * efivars_kobject - get the kobject for the registered efivars
 *
 * If efivars_register() has not been called we return NULL,
 * otherwise return the kobject used at registration time.
 */
struct kobject *efivars_kobject(void)
{
	if (!__efivars)
		return NULL;

	return __efivars->kobject;
}
EXPORT_SYMBOL_GPL(efivars_kobject);

/**
 * efivars_register - register an efivars
 * @efivars: efivars to register
 * @ops: efivars operations
 * @kobject: @efivars-specific kobject
 *
 * Only a single efivars can be registered at any time.
 */
int efivars_register(struct efivars *efivars,
		     const struct efivar_operations *ops,
		     struct kobject *kobject)
{
	if (down_interruptible(&efivars_lock))
		return -EINTR;

	efivars->ops = ops;
	efivars->kobject = kobject;

	__efivars = efivars;

	pr_info("Registered efivars operations\n");

	up(&efivars_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(efivars_register);

/**
 * efivars_unregister - unregister an efivars
 * @efivars: efivars to unregister
 *
 * The caller must have already removed every entry from the list,
 * failure to do so is an error.
 */
int efivars_unregister(struct efivars *efivars)
{
	int rv;

	if (down_interruptible(&efivars_lock))
		return -EINTR;

	if (!__efivars) {
		printk(KERN_ERR "efivars not registered\n");
		rv = -EINVAL;
		goto out;
	}

	if (__efivars != efivars) {
		rv = -EINVAL;
		goto out;
	}

	pr_info("Unregistered efivars operations\n");
	__efivars = NULL;

	rv = 0;
out:
	up(&efivars_lock);
	return rv;
}
EXPORT_SYMBOL_GPL(efivars_unregister);

int efivar_supports_writes(void)
{
	return __efivars && __efivars->ops->set_variable;
}
EXPORT_SYMBOL_GPL(efivar_supports_writes);

/*
 * efivar_lock() - obtain the efivar lock, wait for it if needed
 * @return 0 on success, error code on failure
 */
int efivar_lock(void)
{
	if (down_interruptible(&efivars_lock))
		return -EINTR;
	if (!__efivars->ops) {
		up(&efivars_lock);
		return -ENODEV;
	}
	return 0;
}
EXPORT_SYMBOL_NS_GPL(efivar_lock, EFIVAR);

/*
 * efivar_lock() - obtain the efivar lock if it is free
 * @return 0 on success, error code on failure
 */
int efivar_trylock(void)
{
	if (down_trylock(&efivars_lock))
		 return -EBUSY;
	if (!__efivars->ops) {
		up(&efivars_lock);
		return -ENODEV;
	}
	return 0;
}
EXPORT_SYMBOL_NS_GPL(efivar_trylock, EFIVAR);

/*
 * efivar_unlock() - release the efivar lock
 */
void efivar_unlock(void)
{
	up(&efivars_lock);
}
EXPORT_SYMBOL_NS_GPL(efivar_unlock, EFIVAR);

/*
 * efivar_get_variable() - retrieve a variable identified by name/vendor
 *
 * Must be called with efivars_lock held.
 */
efi_status_t efivar_get_variable(efi_char16_t *name, efi_guid_t *vendor,
				 u32 *attr, unsigned long *size, void *data)
{
	return __efivars->ops->get_variable(name, vendor, attr, size, data);
}
EXPORT_SYMBOL_NS_GPL(efivar_get_variable, EFIVAR);

/*
 * efivar_get_next_variable() - enumerate the next name/vendor pair
 *
 * Must be called with efivars_lock held.
 */
efi_status_t efivar_get_next_variable(unsigned long *name_size,
				      efi_char16_t *name, efi_guid_t *vendor)
{
	return __efivars->ops->get_next_variable(name_size, name, vendor);
}
EXPORT_SYMBOL_NS_GPL(efivar_get_next_variable, EFIVAR);

/*
 * efivar_set_variable_blocking() - local helper function for set_variable
 *
 * Must be called with efivars_lock held.
 */
static efi_status_t
efivar_set_variable_blocking(efi_char16_t *name, efi_guid_t *vendor,
			     u32 attr, unsigned long data_size, void *data)
{
	efi_status_t status;

	if (data_size > 0) {
		status = check_var_size(attr, data_size +
					      ucs2_strsize(name, 1024));
		if (status != EFI_SUCCESS)
			return status;
	}
	return __efivars->ops->set_variable(name, vendor, attr, data_size, data);
}

/*
 * efivar_set_variable_locked() - set a variable identified by name/vendor
 *
 * Must be called with efivars_lock held. If @nonblocking is set, it will use
 * non-blocking primitives so it is guaranteed not to sleep.
 */
efi_status_t efivar_set_variable_locked(efi_char16_t *name, efi_guid_t *vendor,
					u32 attr, unsigned long data_size,
					void *data, bool nonblocking)
{
	efi_set_variable_t *setvar;
	efi_status_t status;

	if (!nonblocking)
		return efivar_set_variable_blocking(name, vendor, attr,
						    data_size, data);

	/*
	 * If no _nonblocking variant exists, the ordinary one
	 * is assumed to be non-blocking.
	 */
	setvar = __efivars->ops->set_variable_nonblocking ?:
		 __efivars->ops->set_variable;

	if (data_size > 0) {
		status = check_var_size_nonblocking(attr, data_size +
							  ucs2_strsize(name, 1024));
		if (status != EFI_SUCCESS)
			return status;
	}
	return setvar(name, vendor, attr, data_size, data);
}
EXPORT_SYMBOL_NS_GPL(efivar_set_variable_locked, EFIVAR);

/*
 * efivar_set_variable() - set a variable identified by name/vendor
 *
 * Can be called without holding the efivars_lock. Will sleep on obtaining the
 * lock, or on obtaining other locks that are needed in order to complete the
 * call.
 */
efi_status_t efivar_set_variable(efi_char16_t *name, efi_guid_t *vendor,
				 u32 attr, unsigned long data_size, void *data)
{
	efi_status_t status;

	if (efivar_lock())
		return EFI_ABORTED;

	status = efivar_set_variable_blocking(name, vendor, attr, data_size, data);
	efivar_unlock();
	return status;
}
EXPORT_SYMBOL_NS_GPL(efivar_set_variable, EFIVAR);
