// SPDX-License-Identifier: GPL-2.0
/*
 * efi_secret module
 *
 * Copyright (C) 2022 IBM Corporation
 * Author: Dov Murik <dovmurik@linux.ibm.com>
 */

/**
 * DOC: efi_secret: Allow reading EFI confidential computing (coco) secret area
 * via securityfs interface.
 *
 * When the module is loaded (and securityfs is mounted, typically under
 * /sys/kernel/security), a "secrets/coco" directory is created in securityfs.
 * In it, a file is created for each secret entry.  The name of each such file
 * is the GUID of the secret entry, and its content is the secret data.
 */

#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/security.h>
#include <linux/efi.h>
#include <linux/cacheflush.h>

#define EFI_SECRET_NUM_FILES 64

struct efi_secret {
	struct dentry *secrets_dir;
	struct dentry *fs_dir;
	struct dentry *fs_files[EFI_SECRET_NUM_FILES];
	void __iomem *secret_data;
	u64 secret_data_len;
};

/*
 * Structure of the EFI secret area
 *
 * Offset   Length
 * (bytes)  (bytes)  Usage
 * -------  -------  -----
 *       0       16  Secret table header GUID (must be 1e74f542-71dd-4d66-963e-ef4287ff173b)
 *      16        4  Length of bytes of the entire secret area
 *
 *      20       16  First secret entry's GUID
 *      36        4  First secret entry's length in bytes (= 16 + 4 + x)
 *      40        x  First secret entry's data
 *
 *    40+x       16  Second secret entry's GUID
 *    56+x        4  Second secret entry's length in bytes (= 16 + 4 + y)
 *    60+x        y  Second secret entry's data
 *
 * (... and so on for additional entries)
 *
 * The GUID of each secret entry designates the usage of the secret data.
 */

/**
 * struct secret_header - Header of entire secret area; this should be followed
 * by instances of struct secret_entry.
 * @guid:	Must be EFI_SECRET_TABLE_HEADER_GUID
 * @len:	Length in bytes of entire secret area, including header
 */
struct secret_header {
	efi_guid_t guid;
	u32 len;
} __attribute((packed));

/**
 * struct secret_entry - Holds one secret entry
 * @guid:	Secret-specific GUID (or NULL_GUID if this secret entry was deleted)
 * @len:	Length of secret entry, including its guid and len fields
 * @data:	The secret data (full of zeros if this secret entry was deleted)
 */
struct secret_entry {
	efi_guid_t guid;
	u32 len;
	u8 data[];
} __attribute((packed));

static size_t secret_entry_data_len(struct secret_entry *e)
{
	return e->len - sizeof(*e);
}

static struct efi_secret the_efi_secret;

static inline struct efi_secret *efi_secret_get(void)
{
	return &the_efi_secret;
}

static int efi_secret_bin_file_show(struct seq_file *file, void *data)
{
	struct secret_entry *e = file->private;

	if (e)
		seq_write(file, e->data, secret_entry_data_len(e));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(efi_secret_bin_file);

/*
 * Overwrite memory content with zeroes, and ensure that dirty cache lines are
 * actually written back to memory, to clear out the secret.
 */
static void wipe_memory(void *addr, size_t size)
{
	memzero_explicit(addr, size);
#ifdef CONFIG_X86
	clflush_cache_range(addr, size);
#endif
}

static int efi_secret_unlink(struct inode *dir, struct dentry *dentry)
{
	struct efi_secret *s = efi_secret_get();
	struct inode *inode = d_inode(dentry);
	struct secret_entry *e = (struct secret_entry *)inode->i_private;
	int i;

	if (e) {
		/* Zero out the secret data */
		wipe_memory(e->data, secret_entry_data_len(e));
		e->guid = NULL_GUID;
	}

	inode->i_private = NULL;

	for (i = 0; i < EFI_SECRET_NUM_FILES; i++)
		if (s->fs_files[i] == dentry)
			s->fs_files[i] = NULL;

	/*
	 * securityfs_remove tries to lock the directory's inode, but we reach
	 * the unlink callback when it's already locked
	 */
	inode_unlock(dir);
	securityfs_remove(dentry);
	inode_lock(dir);

	return 0;
}

static const struct inode_operations efi_secret_dir_inode_operations = {
	.lookup         = simple_lookup,
	.unlink         = efi_secret_unlink,
};

static int efi_secret_map_area(struct platform_device *dev)
{
	int ret;
	struct efi_secret *s = efi_secret_get();
	struct linux_efi_coco_secret_area *secret_area;

	if (efi.coco_secret == EFI_INVALID_TABLE_ADDR) {
		dev_err(&dev->dev, "Secret area address is not available\n");
		return -EINVAL;
	}

	secret_area = memremap(efi.coco_secret, sizeof(*secret_area), MEMREMAP_WB);
	if (secret_area == NULL) {
		dev_err(&dev->dev, "Could not map secret area EFI config entry\n");
		return -ENOMEM;
	}
	if (!secret_area->base_pa || secret_area->size < sizeof(struct secret_header)) {
		dev_err(&dev->dev,
			"Invalid secret area memory location (base_pa=0x%llx size=0x%llx)\n",
			secret_area->base_pa, secret_area->size);
		ret = -EINVAL;
		goto unmap;
	}

	s->secret_data = ioremap_encrypted(secret_area->base_pa, secret_area->size);
	if (s->secret_data == NULL) {
		dev_err(&dev->dev, "Could not map secret area\n");
		ret = -ENOMEM;
		goto unmap;
	}

	s->secret_data_len = secret_area->size;
	ret = 0;

unmap:
	memunmap(secret_area);
	return ret;
}

static void efi_secret_securityfs_teardown(struct platform_device *dev)
{
	struct efi_secret *s = efi_secret_get();
	int i;

	for (i = (EFI_SECRET_NUM_FILES - 1); i >= 0; i--) {
		securityfs_remove(s->fs_files[i]);
		s->fs_files[i] = NULL;
	}

	securityfs_remove(s->fs_dir);
	s->fs_dir = NULL;

	securityfs_remove(s->secrets_dir);
	s->secrets_dir = NULL;

	dev_dbg(&dev->dev, "Removed securityfs entries\n");
}

static int efi_secret_securityfs_setup(struct platform_device *dev)
{
	struct efi_secret *s = efi_secret_get();
	int ret = 0, i = 0, bytes_left;
	unsigned char *ptr;
	struct secret_header *h;
	struct secret_entry *e;
	struct dentry *dent;
	char guid_str[EFI_VARIABLE_GUID_LEN + 1];

	ptr = (void __force *)s->secret_data;
	h = (struct secret_header *)ptr;
	if (efi_guidcmp(h->guid, EFI_SECRET_TABLE_HEADER_GUID)) {
		/*
		 * This is not an error: it just means that EFI defines secret
		 * area but it was not populated by the Guest Owner.
		 */
		dev_dbg(&dev->dev, "EFI secret area does not start with correct GUID\n");
		return -ENODEV;
	}
	if (h->len < sizeof(*h)) {
		dev_err(&dev->dev, "EFI secret area reported length is too small\n");
		return -EINVAL;
	}
	if (h->len > s->secret_data_len) {
		dev_err(&dev->dev, "EFI secret area reported length is too big\n");
		return -EINVAL;
	}

	s->secrets_dir = NULL;
	s->fs_dir = NULL;
	memset(s->fs_files, 0, sizeof(s->fs_files));

	dent = securityfs_create_dir("secrets", NULL);
	if (IS_ERR(dent)) {
		dev_err(&dev->dev, "Error creating secrets securityfs directory entry err=%ld\n",
			PTR_ERR(dent));
		return PTR_ERR(dent);
	}
	s->secrets_dir = dent;

	dent = securityfs_create_dir("coco", s->secrets_dir);
	if (IS_ERR(dent)) {
		dev_err(&dev->dev, "Error creating coco securityfs directory entry err=%ld\n",
			PTR_ERR(dent));
		return PTR_ERR(dent);
	}
	d_inode(dent)->i_op = &efi_secret_dir_inode_operations;
	s->fs_dir = dent;

	bytes_left = h->len - sizeof(*h);
	ptr += sizeof(*h);
	while (bytes_left >= (int)sizeof(*e) && i < EFI_SECRET_NUM_FILES) {
		e = (struct secret_entry *)ptr;
		if (e->len < sizeof(*e) || e->len > (unsigned int)bytes_left) {
			dev_err(&dev->dev, "EFI secret area is corrupted\n");
			ret = -EINVAL;
			goto err_cleanup;
		}

		/* Skip deleted entries (which will have NULL_GUID) */
		if (efi_guidcmp(e->guid, NULL_GUID)) {
			efi_guid_to_str(&e->guid, guid_str);

			dent = securityfs_create_file(guid_str, 0440, s->fs_dir, (void *)e,
						      &efi_secret_bin_file_fops);
			if (IS_ERR(dent)) {
				dev_err(&dev->dev, "Error creating efi_secret securityfs entry\n");
				ret = PTR_ERR(dent);
				goto err_cleanup;
			}

			s->fs_files[i++] = dent;
		}
		ptr += e->len;
		bytes_left -= e->len;
	}

	dev_info(&dev->dev, "Created %d entries in securityfs secrets/coco\n", i);
	return 0;

err_cleanup:
	efi_secret_securityfs_teardown(dev);
	return ret;
}

static void efi_secret_unmap_area(void)
{
	struct efi_secret *s = efi_secret_get();

	if (s->secret_data) {
		iounmap(s->secret_data);
		s->secret_data = NULL;
		s->secret_data_len = 0;
	}
}

static int efi_secret_probe(struct platform_device *dev)
{
	int ret;

	ret = efi_secret_map_area(dev);
	if (ret)
		return ret;

	ret = efi_secret_securityfs_setup(dev);
	if (ret)
		goto err_unmap;

	return ret;

err_unmap:
	efi_secret_unmap_area();
	return ret;
}

static void efi_secret_remove(struct platform_device *dev)
{
	efi_secret_securityfs_teardown(dev);
	efi_secret_unmap_area();
}

static struct platform_driver efi_secret_driver = {
	.probe = efi_secret_probe,
	.remove = efi_secret_remove,
	.driver = {
		.name = "efi_secret",
	},
};

module_platform_driver(efi_secret_driver);

MODULE_DESCRIPTION("Confidential computing EFI secret area access");
MODULE_AUTHOR("IBM");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:efi_secret");
