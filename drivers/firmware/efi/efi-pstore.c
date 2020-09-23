// SPDX-License-Identifier: GPL-2.0+

#include <linux/efi.h>
#include <linux/module.h>
#include <linux/pstore.h>
#include <linux/slab.h>
#include <linux/ucs2_string.h>

#define DUMP_NAME_LEN 66

#define EFIVARS_DATA_SIZE_MAX 1024

static bool efivars_pstore_disable =
	IS_ENABLED(CONFIG_EFI_VARS_PSTORE_DEFAULT_DISABLE);

module_param_named(pstore_disable, efivars_pstore_disable, bool, 0644);

#define PSTORE_EFI_ATTRIBUTES \
	(EFI_VARIABLE_NON_VOLATILE | \
	 EFI_VARIABLE_BOOTSERVICE_ACCESS | \
	 EFI_VARIABLE_RUNTIME_ACCESS)

static LIST_HEAD(efi_pstore_list);

static int efi_pstore_open(struct pstore_info *psi)
{
	psi->data = NULL;
	return 0;
}

static int efi_pstore_close(struct pstore_info *psi)
{
	psi->data = NULL;
	return 0;
}

static inline u64 generic_id(u64 timestamp, unsigned int part, int count)
{
	return (timestamp * 100 + part) * 1000 + count;
}

static int efi_pstore_read_func(struct efivar_entry *entry,
				struct pstore_record *record)
{
	efi_guid_t vendor = LINUX_EFI_CRASH_GUID;
	char name[DUMP_NAME_LEN], data_type;
	int i;
	int cnt;
	unsigned int part;
	unsigned long size;
	u64 time;

	if (efi_guidcmp(entry->var.VendorGuid, vendor))
		return 0;

	for (i = 0; i < DUMP_NAME_LEN; i++)
		name[i] = entry->var.VariableName[i];

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

	entry->var.DataSize = 1024;
	__efivar_entry_get(entry, &entry->var.Attributes,
			   &entry->var.DataSize, entry->var.Data);
	size = entry->var.DataSize;
	memcpy(record->buf, entry->var.Data,
	       (size_t)min_t(unsigned long, EFIVARS_DATA_SIZE_MAX, size));

	return size;
}

/**
 * efi_pstore_scan_sysfs_enter
 * @pos: scanning entry
 * @next: next entry
 * @head: list head
 */
static void efi_pstore_scan_sysfs_enter(struct efivar_entry *pos,
					struct efivar_entry *next,
					struct list_head *head)
{
	pos->scanning = true;
	if (&next->list != head)
		next->scanning = true;
}

/**
 * __efi_pstore_scan_sysfs_exit
 * @entry: deleting entry
 * @turn_off_scanning: Check if a scanning flag should be turned off
 */
static inline int __efi_pstore_scan_sysfs_exit(struct efivar_entry *entry,
						bool turn_off_scanning)
{
	if (entry->deleting) {
		list_del(&entry->list);
		efivar_entry_iter_end();
		kfree(entry);
		if (efivar_entry_iter_begin())
			return -EINTR;
	} else if (turn_off_scanning)
		entry->scanning = false;

	return 0;
}

/**
 * efi_pstore_scan_sysfs_exit
 * @pos: scanning entry
 * @next: next entry
 * @head: list head
 * @stop: a flag checking if scanning will stop
 */
static int efi_pstore_scan_sysfs_exit(struct efivar_entry *pos,
				       struct efivar_entry *next,
				       struct list_head *head, bool stop)
{
	int ret = __efi_pstore_scan_sysfs_exit(pos, true);

	if (ret)
		return ret;

	if (stop)
		ret = __efi_pstore_scan_sysfs_exit(next, &next->list != head);
	return ret;
}

/**
 * efi_pstore_sysfs_entry_iter
 *
 * @record: pstore record to pass to callback
 *
 * You MUST call efivar_entry_iter_begin() before this function, and
 * efivar_entry_iter_end() afterwards.
 *
 */
static int efi_pstore_sysfs_entry_iter(struct pstore_record *record)
{
	struct efivar_entry **pos = (struct efivar_entry **)&record->psi->data;
	struct efivar_entry *entry, *n;
	struct list_head *head = &efi_pstore_list;
	int size = 0;
	int ret;

	if (!*pos) {
		list_for_each_entry_safe(entry, n, head, list) {
			efi_pstore_scan_sysfs_enter(entry, n, head);

			size = efi_pstore_read_func(entry, record);
			ret = efi_pstore_scan_sysfs_exit(entry, n, head,
							 size < 0);
			if (ret)
				return ret;
			if (size)
				break;
		}
		*pos = n;
		return size;
	}

	list_for_each_entry_safe_from((*pos), n, head, list) {
		efi_pstore_scan_sysfs_enter((*pos), n, head);

		size = efi_pstore_read_func((*pos), record);
		ret = efi_pstore_scan_sysfs_exit((*pos), n, head, size < 0);
		if (ret)
			return ret;
		if (size)
			break;
	}
	*pos = n;
	return size;
}

/**
 * efi_pstore_read
 *
 * This function returns a size of NVRAM entry logged via efi_pstore_write().
 * The meaning and behavior of efi_pstore/pstore are as below.
 *
 * size > 0: Got data of an entry logged via efi_pstore_write() successfully,
 *           and pstore filesystem will continue reading subsequent entries.
 * size == 0: Entry was not logged via efi_pstore_write(),
 *            and efi_pstore driver will continue reading subsequent entries.
 * size < 0: Failed to get data of entry logging via efi_pstore_write(),
 *           and pstore will stop reading entry.
 */
static ssize_t efi_pstore_read(struct pstore_record *record)
{
	ssize_t size;

	record->buf = kzalloc(EFIVARS_DATA_SIZE_MAX, GFP_KERNEL);
	if (!record->buf)
		return -ENOMEM;

	if (efivar_entry_iter_begin()) {
		size = -EINTR;
		goto out;
	}
	size = efi_pstore_sysfs_entry_iter(record);
	efivar_entry_iter_end();

out:
	if (size <= 0) {
		kfree(record->buf);
		record->buf = NULL;
	}
	return size;
}

static int efi_pstore_write(struct pstore_record *record)
{
	char name[DUMP_NAME_LEN];
	efi_char16_t efi_name[DUMP_NAME_LEN];
	efi_guid_t vendor = LINUX_EFI_CRASH_GUID;
	int i, ret = 0;

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

	ret = efivar_entry_set_safe(efi_name, vendor, PSTORE_EFI_ATTRIBUTES,
			      preemptible(), record->size, record->psi->buf);

	if (record->reason == KMSG_DUMP_OOPS)
		efivar_run_worker();

	return ret;
};

/*
 * Clean up an entry with the same name
 */
static int efi_pstore_erase_func(struct efivar_entry *entry, void *data)
{
	efi_char16_t *efi_name = data;
	efi_guid_t vendor = LINUX_EFI_CRASH_GUID;
	unsigned long ucs2_len = ucs2_strlen(efi_name);

	if (efi_guidcmp(entry->var.VendorGuid, vendor))
		return 0;

	if (ucs2_strncmp(entry->var.VariableName, efi_name, (size_t)ucs2_len))
		return 0;

	if (entry->scanning) {
		/*
		 * Skip deletion because this entry will be deleted
		 * after scanning is completed.
		 */
		entry->deleting = true;
	} else
		list_del(&entry->list);

	/* found */
	__efivar_entry_delete(entry);

	return 1;
}

static int efi_pstore_erase_name(const char *name)
{
	struct efivar_entry *entry = NULL;
	efi_char16_t efi_name[DUMP_NAME_LEN];
	int found, i;

	for (i = 0; i < DUMP_NAME_LEN; i++) {
		efi_name[i] = name[i];
		if (name[i] == '\0')
			break;
	}

	if (efivar_entry_iter_begin())
		return -EINTR;

	found = __efivar_entry_iter(efi_pstore_erase_func, &efi_pstore_list,
				    efi_name, &entry);
	efivar_entry_iter_end();

	if (found && !entry->scanning)
		kfree(entry);

	return found ? 0 : -ENOENT;
}

static int efi_pstore_erase(struct pstore_record *record)
{
	char name[DUMP_NAME_LEN];
	int ret;

	snprintf(name, sizeof(name), "dump-type%u-%u-%d-%lld",
		 record->type, record->part, record->count,
		 (long long)record->time.tv_sec);
	ret = efi_pstore_erase_name(name);
	if (ret != -ENOENT)
		return ret;

	snprintf(name, sizeof(name), "dump-type%u-%u-%lld",
		record->type, record->part, (long long)record->time.tv_sec);
	ret = efi_pstore_erase_name(name);

	return ret;
}

static struct pstore_info efi_pstore_info = {
	.owner		= THIS_MODULE,
	.name		= "efi",
	.flags		= PSTORE_FLAGS_DMESG,
	.open		= efi_pstore_open,
	.close		= efi_pstore_close,
	.read		= efi_pstore_read,
	.write		= efi_pstore_write,
	.erase		= efi_pstore_erase,
};

static int efi_pstore_callback(efi_char16_t *name, efi_guid_t vendor,
			       unsigned long name_size, void *data)
{
	struct efivar_entry *entry;
	int ret;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	memcpy(entry->var.VariableName, name, name_size);
	entry->var.VendorGuid = vendor;

	ret = efivar_entry_add(entry, &efi_pstore_list);
	if (ret)
		kfree(entry);

	return ret;
}

static int efi_pstore_update_entry(efi_char16_t *name, efi_guid_t vendor,
				   unsigned long name_size, void *data)
{
	struct efivar_entry *entry = data;

	if (efivar_entry_find(name, vendor, &efi_pstore_list, false))
		return 0;

	memcpy(entry->var.VariableName, name, name_size);
	memcpy(&(entry->var.VendorGuid), &vendor, sizeof(efi_guid_t));

	return 1;
}

static void efi_pstore_update_entries(struct work_struct *work)
{
	struct efivar_entry *entry;
	int err;

	/* Add new sysfs entries */
	while (1) {
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry)
			return;

		err = efivar_init(efi_pstore_update_entry, entry,
				  false, &efi_pstore_list);
		if (!err)
			break;

		efivar_entry_add(entry, &efi_pstore_list);
	}

	kfree(entry);
}

static __init int efivars_pstore_init(void)
{
	int ret;

	if (!efivars_kobject() || !efivar_supports_writes())
		return 0;

	if (efivars_pstore_disable)
		return 0;

	ret = efivar_init(efi_pstore_callback, NULL, true, &efi_pstore_list);
	if (ret)
		return ret;

	efi_pstore_info.buf = kmalloc(4096, GFP_KERNEL);
	if (!efi_pstore_info.buf)
		return -ENOMEM;

	efi_pstore_info.bufsize = 1024;

	if (pstore_register(&efi_pstore_info)) {
		kfree(efi_pstore_info.buf);
		efi_pstore_info.buf = NULL;
		efi_pstore_info.bufsize = 0;
	}

	INIT_WORK(&efivar_work, efi_pstore_update_entries);

	return 0;
}

static __exit void efivars_pstore_exit(void)
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
