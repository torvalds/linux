#include <linux/efi.h>
#include <linux/module.h>
#include <linux/pstore.h>
#include <linux/slab.h>
#include <linux/ucs2_string.h>

#define DUMP_NAME_LEN 52

static bool efivars_pstore_disable =
	IS_ENABLED(CONFIG_EFI_VARS_PSTORE_DEFAULT_DISABLE);

module_param_named(pstore_disable, efivars_pstore_disable, bool, 0644);

#define PSTORE_EFI_ATTRIBUTES \
	(EFI_VARIABLE_NON_VOLATILE | \
	 EFI_VARIABLE_BOOTSERVICE_ACCESS | \
	 EFI_VARIABLE_RUNTIME_ACCESS)

static int efi_pstore_open(struct pstore_info *psi)
{
	efivar_entry_iter_begin();
	psi->data = NULL;
	return 0;
}

static int efi_pstore_close(struct pstore_info *psi)
{
	efivar_entry_iter_end();
	psi->data = NULL;
	return 0;
}

struct pstore_read_data {
	u64 *id;
	enum pstore_type_id *type;
	int *count;
	struct timespec *timespec;
	bool *compressed;
	char **buf;
};

static int efi_pstore_read_func(struct efivar_entry *entry, void *data)
{
	efi_guid_t vendor = LINUX_EFI_CRASH_GUID;
	struct pstore_read_data *cb_data = data;
	char name[DUMP_NAME_LEN], data_type;
	int i;
	int cnt;
	unsigned int part;
	unsigned long time, size;

	if (efi_guidcmp(entry->var.VendorGuid, vendor))
		return 0;

	for (i = 0; i < DUMP_NAME_LEN; i++)
		name[i] = entry->var.VariableName[i];

	if (sscanf(name, "dump-type%u-%u-%d-%lu-%c",
		   cb_data->type, &part, &cnt, &time, &data_type) == 5) {
		*cb_data->id = part;
		*cb_data->count = cnt;
		cb_data->timespec->tv_sec = time;
		cb_data->timespec->tv_nsec = 0;
		if (data_type == 'C')
			*cb_data->compressed = true;
		else
			*cb_data->compressed = false;
	} else if (sscanf(name, "dump-type%u-%u-%d-%lu",
		   cb_data->type, &part, &cnt, &time) == 4) {
		*cb_data->id = part;
		*cb_data->count = cnt;
		cb_data->timespec->tv_sec = time;
		cb_data->timespec->tv_nsec = 0;
		*cb_data->compressed = false;
	} else if (sscanf(name, "dump-type%u-%u-%lu",
			  cb_data->type, &part, &time) == 3) {
		/*
		 * Check if an old format,
		 * which doesn't support holding
		 * multiple logs, remains.
		 */
		*cb_data->id = part;
		*cb_data->count = 0;
		cb_data->timespec->tv_sec = time;
		cb_data->timespec->tv_nsec = 0;
		*cb_data->compressed = false;
	} else
		return 0;

	entry->var.DataSize = 1024;
	__efivar_entry_get(entry, &entry->var.Attributes,
			   &entry->var.DataSize, entry->var.Data);
	size = entry->var.DataSize;

	*cb_data->buf = kmemdup(entry->var.Data, size, GFP_KERNEL);
	if (*cb_data->buf == NULL)
		return -ENOMEM;
	return size;
}

static ssize_t efi_pstore_read(u64 *id, enum pstore_type_id *type,
			       int *count, struct timespec *timespec,
			       char **buf, bool *compressed,
			       struct pstore_info *psi)
{
	struct pstore_read_data data;

	data.id = id;
	data.type = type;
	data.count = count;
	data.timespec = timespec;
	data.compressed = compressed;
	data.buf = buf;

	return __efivar_entry_iter(efi_pstore_read_func, &efivar_sysfs_list, &data,
				   (struct efivar_entry **)&psi->data);
}

static int efi_pstore_write(enum pstore_type_id type,
		enum kmsg_dump_reason reason, u64 *id,
		unsigned int part, int count, bool compressed, size_t size,
		struct pstore_info *psi)
{
	char name[DUMP_NAME_LEN];
	efi_char16_t efi_name[DUMP_NAME_LEN];
	efi_guid_t vendor = LINUX_EFI_CRASH_GUID;
	int i, ret = 0;

	sprintf(name, "dump-type%u-%u-%d-%lu-%c", type, part, count,
		get_seconds(), compressed ? 'C' : 'D');

	for (i = 0; i < DUMP_NAME_LEN; i++)
		efi_name[i] = name[i];

	efivar_entry_set_safe(efi_name, vendor, PSTORE_EFI_ATTRIBUTES,
			      !pstore_cannot_block_path(reason),
			      size, psi->buf);

	if (reason == KMSG_DUMP_OOPS)
		efivar_run_worker();

	*id = part;
	return ret;
};

struct pstore_erase_data {
	u64 id;
	enum pstore_type_id type;
	int count;
	struct timespec time;
	efi_char16_t *name;
};

/*
 * Clean up an entry with the same name
 */
static int efi_pstore_erase_func(struct efivar_entry *entry, void *data)
{
	struct pstore_erase_data *ed = data;
	efi_guid_t vendor = LINUX_EFI_CRASH_GUID;
	efi_char16_t efi_name_old[DUMP_NAME_LEN];
	efi_char16_t *efi_name = ed->name;
	unsigned long ucs2_len = ucs2_strlen(ed->name);
	char name_old[DUMP_NAME_LEN];
	int i;

	if (efi_guidcmp(entry->var.VendorGuid, vendor))
		return 0;

	if (ucs2_strncmp(entry->var.VariableName,
			  efi_name, (size_t)ucs2_len)) {
		/*
		 * Check if an old format, which doesn't support
		 * holding multiple logs, remains.
		 */
		sprintf(name_old, "dump-type%u-%u-%lu", ed->type,
			(unsigned int)ed->id, ed->time.tv_sec);

		for (i = 0; i < DUMP_NAME_LEN; i++)
			efi_name_old[i] = name_old[i];

		if (ucs2_strncmp(entry->var.VariableName, efi_name_old,
				  ucs2_strlen(efi_name_old)))
			return 0;
	}

	/* found */
	__efivar_entry_delete(entry);
	list_del(&entry->list);

	return 1;
}

static int efi_pstore_erase(enum pstore_type_id type, u64 id, int count,
			    struct timespec time, struct pstore_info *psi)
{
	struct pstore_erase_data edata;
	struct efivar_entry *entry = NULL;
	char name[DUMP_NAME_LEN];
	efi_char16_t efi_name[DUMP_NAME_LEN];
	int found, i;

	sprintf(name, "dump-type%u-%u-%d-%lu", type, (unsigned int)id, count,
		time.tv_sec);

	for (i = 0; i < DUMP_NAME_LEN; i++)
		efi_name[i] = name[i];

	edata.id = id;
	edata.type = type;
	edata.count = count;
	edata.time = time;
	edata.name = efi_name;

	efivar_entry_iter_begin();
	found = __efivar_entry_iter(efi_pstore_erase_func, &efivar_sysfs_list, &edata, &entry);
	efivar_entry_iter_end();

	if (found)
		efivar_unregister(entry);

	return 0;
}

static struct pstore_info efi_pstore_info = {
	.owner		= THIS_MODULE,
	.name		= "efi",
	.open		= efi_pstore_open,
	.close		= efi_pstore_close,
	.read		= efi_pstore_read,
	.write		= efi_pstore_write,
	.erase		= efi_pstore_erase,
};

static __init int efivars_pstore_init(void)
{
	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return 0;

	if (!efivars_kobject())
		return 0;

	if (efivars_pstore_disable)
		return 0;

	efi_pstore_info.buf = kmalloc(4096, GFP_KERNEL);
	if (!efi_pstore_info.buf)
		return -ENOMEM;

	efi_pstore_info.bufsize = 1024;
	spin_lock_init(&efi_pstore_info.buf_lock);

	if (pstore_register(&efi_pstore_info)) {
		kfree(efi_pstore_info.buf);
		efi_pstore_info.buf = NULL;
		efi_pstore_info.bufsize = 0;
	}

	return 0;
}

static __exit void efivars_pstore_exit(void)
{
}

module_init(efivars_pstore_init);
module_exit(efivars_pstore_exit);

MODULE_DESCRIPTION("EFI variable backend for pstore");
MODULE_LICENSE("GPL");
