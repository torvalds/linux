// SPDX-License-Identifier: GPL-2.0+

#include <linux/efi.h>
#include <linux/module.h>
#include <linux/pstore.h>
#include <linux/slab.h>
#include <linux/ucs2_string.h>

MODULE_IMPORT_NS(EFIVAR);

#define DUMP_NAME_LEN 66

static unsigned int record_size = 1024;
module_param(record_size, uint, 0444);
MODULE_PARM_DESC(record_size, "size of each pstore UEFI var (in bytes, min/default=1024)");

#define PSTORE_EFI_ATTRIBUTES \
	(EFI_VARIABLE_NON_VOLATILE | \
	 EFI_VARIABLE_BOOTSERVICE_ACCESS | \
	 EFI_VARIABLE_RUNTIME_ACCESS)

static bool pstore_disable = IS_ENABLED(CONFIG_EFI_VARS_PSTORE_DEFAULT_DISABLE);

static int efivars_pstore_init(void);
static void efivars_pstore_exit(void);

static int efi_pstore_disable_set(const char *val, const struct kernel_param *kp)
{
	int err;
	bool old_pstore_disable = pstore_disable;

	err = param_set_bool(val, kp);
	if (err)
		return err;

	if (old_pstore_disable != pstore_disable) {
		if (pstore_disable)
			efivars_pstore_exit();
		else
			efivars_pstore_init();
	}

	return 0;
}

static const struct kernel_param_ops pstore_disable_ops = {
	.set	= efi_pstore_disable_set,
	.get	= param_get_bool,
};

module_param_cb(pstore_disable, &pstore_disable_ops, &pstore_disable, 0644);
__MODULE_PARM_TYPE(pstore_disable, "bool");

static int efi_pstore_open(struct pstore_info *psi)
{
	int err;

	err = efivar_lock();
	if (err)
		return err;

	psi->data = kzalloc(record_size, GFP_KERNEL);
	if (!psi->data)
		return -ENOMEM;

	return 0;
}

static int efi_pstore_close(struct pstore_info *psi)
{
	efivar_unlock();
	kfree(psi->data);
	return 0;
}

static inline u64 generic_id(u64 timestamp, unsigned int part, int count)
{
	return (timestamp * 100 + part) * 1000 + count;
}

static int efi_pstore_read_func(struct pstore_record *record,
				efi_char16_t *varname)
{
	unsigned long wlen, size = record_size;
	char name[DUMP_NAME_LEN], data_type;
	efi_status_t status;
	int cnt;
	unsigned int part;
	u64 time;

	ucs2_as_utf8(name, varname, DUMP_NAME_LEN);

	if (sscanf(name, "dump-type%u-%u-%d-%llu-%c",
		   &record->type, &part, &cnt, &time, &data_type) == 5) {
		record->id = generic_id(time, part, cnt);
		record->part = part;
		record->count = cnt;
		record->time.tv_sec = time;
		record->time.tv_nsec = 0;
		if (data_type == 'C')
			record->compressed = true;
		else
			record->compressed = false;
		record->ecc_notice_size = 0;
	} else if (sscanf(name, "dump-type%u-%u-%d-%llu",
		   &record->type, &part, &cnt, &time) == 4) {
		record->id = generic_id(time, part, cnt);
		record->part = part;
		record->count = cnt;
		record->time.tv_sec = time;
		record->time.tv_nsec = 0;
		record->compressed = false;
		record->ecc_notice_size = 0;
	} else if (sscanf(name, "dump-type%u-%u-%llu",
			  &record->type, &part, &time) == 3) {
		/*
		 * Check if an old format,
		 * which doesn't support holding
		 * multiple logs, remains.
		 */
		record->id = generic_id(time, part, 0);
		record->part = part;
		record->count = 0;
		record->time.tv_sec = time;
		record->time.tv_nsec = 0;
		record->compressed = false;
		record->ecc_notice_size = 0;
	} else
		return 0;

	record->buf = kmalloc(size, GFP_KERNEL);
	if (!record->buf)
		return -ENOMEM;

	status = efivar_get_variable(varname, &LINUX_EFI_CRASH_GUID, NULL,
				     &size, record->buf);
	if (status != EFI_SUCCESS) {
		kfree(record->buf);
		return -EIO;
	}

	/*
	 * Store the name of the variable in the pstore_record priv field, so
	 * we can reuse it later if we need to delete the EFI variable from the
	 * variable store.
	 */
	wlen = (ucs2_strnlen(varname, DUMP_NAME_LEN) + 1) * sizeof(efi_char16_t);
	record->priv = kmemdup(varname, wlen, GFP_KERNEL);
	if (!record->priv) {
		kfree(record->buf);
		return -ENOMEM;
	}

	return size;
}

static ssize_t efi_pstore_read(struct pstore_record *record)
{
	efi_char16_t *varname = record->psi->data;
	efi_guid_t guid = LINUX_EFI_CRASH_GUID;
	unsigned long varname_size;
	efi_status_t status;

	for (;;) {
		varname_size = 1024;

		/*
		 * If this is the first read() call in the pstore enumeration,
		 * varname will be the empty string, and the GetNextVariable()
		 * runtime service call will return the first EFI variable in
		 * its own enumeration order, ignoring the guid argument.
		 *
		 * Subsequent calls to GetNextVariable() must pass the name and
		 * guid values returned by the previous call, which is why we
		 * store varname in record->psi->data. Given that we only
		 * enumerate variables with the efi-pstore GUID, there is no
		 * need to record the guid return value.
		 */
		status = efivar_get_next_variable(&varname_size, varname, &guid);
		if (status == EFI_NOT_FOUND)
			return 0;

		if (status != EFI_SUCCESS)
			return -EIO;

		/* skip variables that don't concern us */
		if (efi_guidcmp(guid, LINUX_EFI_CRASH_GUID))
			continue;

		return efi_pstore_read_func(record, varname);
	}
}

static int efi_pstore_write(struct pstore_record *record)
{
	char name[DUMP_NAME_LEN];
	efi_char16_t efi_name[DUMP_NAME_LEN];
	efi_status_t status;
	int i;

	record->id = generic_id(record->time.tv_sec, record->part,
				record->count);

	/* Since we copy the entire length of name, make sure it is wiped. */
	memset(name, 0, sizeof(name));

	snprintf(name, sizeof(name), "dump-type%u-%u-%d-%lld-%c",
		 record->type, record->part, record->count,
		 (long long)record->time.tv_sec,
		 record->compressed ? 'C' : 'D');

	for (i = 0; i < DUMP_NAME_LEN; i++)
		efi_name[i] = name[i];

	if (efivar_trylock())
		return -EBUSY;
	status = efivar_set_variable_locked(efi_name, &LINUX_EFI_CRASH_GUID,
					    PSTORE_EFI_ATTRIBUTES,
					    record->size, record->psi->buf,
					    true);
	efivar_unlock();
	return status == EFI_SUCCESS ? 0 : -EIO;
};

static int efi_pstore_erase(struct pstore_record *record)
{
	efi_status_t status;

	status = efivar_set_variable(record->priv, &LINUX_EFI_CRASH_GUID,
				     PSTORE_EFI_ATTRIBUTES, 0, NULL);

	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND)
		return -EIO;
	return 0;
}

static struct pstore_info efi_pstore_info = {
	.owner		= THIS_MODULE,
	.name		= KBUILD_MODNAME,
	.flags		= PSTORE_FLAGS_DMESG,
	.open		= efi_pstore_open,
	.close		= efi_pstore_close,
	.read		= efi_pstore_read,
	.write		= efi_pstore_write,
	.erase		= efi_pstore_erase,
};

static int efivars_pstore_init(void)
{
	if (!efivar_supports_writes())
		return 0;

	if (pstore_disable)
		return 0;

	/*
	 * Notice that 1024 is the minimum here to prevent issues with
	 * decompression algorithms that were spotted during tests;
	 * even in the case of not using compression, smaller values would
	 * just pollute more the pstore FS with many small collected files.
	 */
	if (record_size < 1024)
		record_size = 1024;

	efi_pstore_info.buf = kmalloc(record_size, GFP_KERNEL);
	if (!efi_pstore_info.buf)
		return -ENOMEM;

	efi_pstore_info.bufsize = record_size;

	if (pstore_register(&efi_pstore_info)) {
		kfree(efi_pstore_info.buf);
		efi_pstore_info.buf = NULL;
		efi_pstore_info.bufsize = 0;
	}

	return 0;
}

static void efivars_pstore_exit(void)
{
	if (!efi_pstore_info.bufsize)
		return;

	pstore_unregister(&efi_pstore_info);
	kfree(efi_pstore_info.buf);
	efi_pstore_info.buf = NULL;
	efi_pstore_info.bufsize = 0;
}

module_init(efivars_pstore_init);
module_exit(efivars_pstore_exit);

MODULE_DESCRIPTION("EFI variable backend for pstore");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:efivars");
