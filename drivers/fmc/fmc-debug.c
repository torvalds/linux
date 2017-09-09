/*
 * Copyright (C) 2015 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <asm/byteorder.h>

#include <linux/fmc.h>
#include <linux/sdb.h>
#include <linux/fmc-sdb.h>

#define FMC_DBG_SDB_DUMP "dump_sdb"

static char *__strip_trailing_space(char *buf, char *str, int len)
{
	int i = len - 1;

	memcpy(buf, str, len);
	buf[len] = '\0';
	while (i >= 0 && buf[i] == ' ')
		buf[i--] = '\0';
	return buf;
}

#define __sdb_string(buf, field) ({			\
	BUILD_BUG_ON(sizeof(buf) < sizeof(field));	\
	__strip_trailing_space(buf, (void *)(field), sizeof(field));	\
		})

/**
 * We do not check seq_printf() errors because we want to see things in any case
 */
static void fmc_sdb_dump_recursive(struct fmc_device *fmc, struct seq_file *s,
				   const struct sdb_array *arr)
{
	unsigned long base = arr->baseaddr;
	int i, j, n = arr->len, level = arr->level;
	char tmp[64];

	for (i = 0; i < n; i++) {
		union  sdb_record *r;
		struct sdb_product *p;
		struct sdb_component *c;

		r = &arr->record[i];
		c = &r->dev.sdb_component;
		p = &c->product;

		for (j = 0; j < level; j++)
			seq_printf(s, "   ");
		switch (r->empty.record_type) {
		case sdb_type_interconnect:
			seq_printf(s, "%08llx:%08x %.19s\n",
				   __be64_to_cpu(p->vendor_id),
				   __be32_to_cpu(p->device_id),
				   p->name);
			break;
		case sdb_type_device:
			seq_printf(s, "%08llx:%08x %.19s (%08llx-%08llx)\n",
				   __be64_to_cpu(p->vendor_id),
				   __be32_to_cpu(p->device_id),
				   p->name,
				   __be64_to_cpu(c->addr_first) + base,
				   __be64_to_cpu(c->addr_last) + base);
			break;
		case sdb_type_bridge:
			seq_printf(s, "%08llx:%08x %.19s (bridge: %08llx)\n",
				   __be64_to_cpu(p->vendor_id),
				   __be32_to_cpu(p->device_id),
				   p->name,
				   __be64_to_cpu(c->addr_first) + base);
			if (IS_ERR(arr->subtree[i])) {
				seq_printf(s, "SDB: (bridge error %li)\n",
					 PTR_ERR(arr->subtree[i]));
				break;
			}
			fmc_sdb_dump_recursive(fmc, s, arr->subtree[i]);
			break;
		case sdb_type_integration:
			seq_printf(s, "integration\n");
			break;
		case sdb_type_repo_url:
			seq_printf(s, "Synthesis repository: %s\n",
					  __sdb_string(tmp, r->repo_url.repo_url));
			break;
		case sdb_type_synthesis:
			seq_printf(s, "Bitstream '%s' ",
					  __sdb_string(tmp, r->synthesis.syn_name));
			seq_printf(s, "synthesized %08x by %s ",
					  __be32_to_cpu(r->synthesis.date),
					  __sdb_string(tmp, r->synthesis.user_name));
			seq_printf(s, "(%s version %x), ",
					  __sdb_string(tmp, r->synthesis.tool_name),
					  __be32_to_cpu(r->synthesis.tool_version));
			seq_printf(s, "commit %pm\n",
					  r->synthesis.commit_id);
			break;
		case sdb_type_empty:
			seq_printf(s, "empty\n");
			break;
		default:
			seq_printf(s, "UNKNOWN TYPE 0x%02x\n",
				   r->empty.record_type);
			break;
		}
	}
}

static int fmc_sdb_dump(struct seq_file *s, void *offset)
{
	struct fmc_device *fmc = s->private;

	if (!fmc->sdb) {
		seq_printf(s, "no SDB information\n");
		return 0;
	}

	seq_printf(s, "FMC: %s (%s), slot %i, device %s\n", dev_name(fmc->hwdev),
	fmc->carrier_name, fmc->slot_id, dev_name(&fmc->dev));
	/* Dump SDB information */
	fmc_sdb_dump_recursive(fmc, s, fmc->sdb);

	return 0;
}


static int fmc_sdb_dump_open(struct inode *inode, struct file *file)
{
	struct fmc_device *fmc = inode->i_private;

	return single_open(file, fmc_sdb_dump, fmc);
}


const struct file_operations fmc_dbgfs_sdb_dump = {
	.owner = THIS_MODULE,
	.open  = fmc_sdb_dump_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

int fmc_debug_init(struct fmc_device *fmc)
{
	fmc->dbg_dir = debugfs_create_dir(dev_name(&fmc->dev), NULL);
	if (IS_ERR_OR_NULL(fmc->dbg_dir)) {
		pr_err("FMC: Cannot create debugfs\n");
		return PTR_ERR(fmc->dbg_dir);
	}

	fmc->dbg_sdb_dump = debugfs_create_file(FMC_DBG_SDB_DUMP, 0444,
						fmc->dbg_dir, fmc,
						&fmc_dbgfs_sdb_dump);
	if (IS_ERR_OR_NULL(fmc->dbg_sdb_dump))
		pr_err("FMC: Cannot create debugfs file %s\n",
		       FMC_DBG_SDB_DUMP);

	return 0;
}

void fmc_debug_exit(struct fmc_device *fmc)
{
	if (fmc->dbg_dir)
		debugfs_remove_recursive(fmc->dbg_dir);
}
