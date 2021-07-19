// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  c 2001 PPC 64 Team, IBM Corp
 *
 * /dev/nvram driver for PPC64
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/nvram.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/kmsg_dump.h>
#include <linux/pagemap.h>
#include <linux/pstore.h>
#include <linux/zlib.h>
#include <linux/uaccess.h>
#include <asm/nvram.h>
#include <asm/rtas.h>
#include <asm/prom.h>
#include <asm/machdep.h>

#undef DEBUG_NVRAM

#define NVRAM_HEADER_LEN	sizeof(struct nvram_header)
#define NVRAM_BLOCK_LEN		NVRAM_HEADER_LEN

/* If change this size, then change the size of NVNAME_LEN */
struct nvram_header {
	unsigned char signature;
	unsigned char checksum;
	unsigned short length;
	/* Terminating null required only for names < 12 chars. */
	char name[12];
};

struct nvram_partition {
	struct list_head partition;
	struct nvram_header header;
	unsigned int index;
};

static LIST_HEAD(nvram_partitions);

#ifdef CONFIG_PPC_PSERIES
struct nvram_os_partition rtas_log_partition = {
	.name = "ibm,rtas-log",
	.req_size = 2079,
	.min_size = 1055,
	.index = -1,
	.os_partition = true
};
#endif

struct nvram_os_partition oops_log_partition = {
	.name = "lnx,oops-log",
	.req_size = 4000,
	.min_size = 2000,
	.index = -1,
	.os_partition = true
};

static const char *nvram_os_partitions[] = {
#ifdef CONFIG_PPC_PSERIES
	"ibm,rtas-log",
#endif
	"lnx,oops-log",
	NULL
};

static void oops_to_nvram(struct kmsg_dumper *dumper,
			  enum kmsg_dump_reason reason);

static struct kmsg_dumper nvram_kmsg_dumper = {
	.dump = oops_to_nvram
};

/*
 * For capturing and compressing an oops or panic report...

 * big_oops_buf[] holds the uncompressed text we're capturing.
 *
 * oops_buf[] holds the compressed text, preceded by a oops header.
 * oops header has u16 holding the version of oops header (to differentiate
 * between old and new format header) followed by u16 holding the length of
 * the compressed* text (*Or uncompressed, if compression fails.) and u64
 * holding the timestamp. oops_buf[] gets written to NVRAM.
 *
 * oops_log_info points to the header. oops_data points to the compressed text.
 *
 * +- oops_buf
 * |                                   +- oops_data
 * v                                   v
 * +-----------+-----------+-----------+------------------------+
 * | version   | length    | timestamp | text                   |
 * | (2 bytes) | (2 bytes) | (8 bytes) | (oops_data_sz bytes)   |
 * +-----------+-----------+-----------+------------------------+
 * ^
 * +- oops_log_info
 *
 * We preallocate these buffers during init to avoid kmalloc during oops/panic.
 */
static size_t big_oops_buf_sz;
static char *big_oops_buf, *oops_buf;
static char *oops_data;
static size_t oops_data_sz;

/* Compression parameters */
#define COMPR_LEVEL 6
#define WINDOW_BITS 12
#define MEM_LEVEL 4
static struct z_stream_s stream;

#ifdef CONFIG_PSTORE
#ifdef CONFIG_PPC_POWERNV
static struct nvram_os_partition skiboot_partition = {
	.name = "ibm,skiboot",
	.index = -1,
	.os_partition = false
};
#endif

#ifdef CONFIG_PPC_PSERIES
static struct nvram_os_partition of_config_partition = {
	.name = "of-config",
	.index = -1,
	.os_partition = false
};
#endif

static struct nvram_os_partition common_partition = {
	.name = "common",
	.index = -1,
	.os_partition = false
};

static enum pstore_type_id nvram_type_ids[] = {
	PSTORE_TYPE_DMESG,
	PSTORE_TYPE_PPC_COMMON,
	-1,
	-1,
	-1
};
static int read_type;
#endif

/* nvram_write_os_partition
 *
 * We need to buffer the error logs into nvram to ensure that we have
 * the failure information to decode.  If we have a severe error there
 * is no way to guarantee that the OS or the machine is in a state to
 * get back to user land and write the error to disk.  For example if
 * the SCSI device driver causes a Machine Check by writing to a bad
 * IO address, there is no way of guaranteeing that the device driver
 * is in any state that is would also be able to write the error data
 * captured to disk, thus we buffer it in NVRAM for analysis on the
 * next boot.
 *
 * In NVRAM the partition containing the error log buffer will looks like:
 * Header (in bytes):
 * +-----------+----------+--------+------------+------------------+
 * | signature | checksum | length | name       | data             |
 * |0          |1         |2      3|4         15|16        length-1|
 * +-----------+----------+--------+------------+------------------+
 *
 * The 'data' section would look like (in bytes):
 * +--------------+------------+-----------------------------------+
 * | event_logged | sequence # | error log                         |
 * |0            3|4          7|8                  error_log_size-1|
 * +--------------+------------+-----------------------------------+
 *
 * event_logged: 0 if event has not been logged to syslog, 1 if it has
 * sequence #: The unique sequence # for each event. (until it wraps)
 * error log: The error log from event_scan
 */
int nvram_write_os_partition(struct nvram_os_partition *part,
			     char *buff, int length,
			     unsigned int err_type,
			     unsigned int error_log_cnt)
{
	int rc;
	loff_t tmp_index;
	struct err_log_info info;

	if (part->index == -1)
		return -ESPIPE;

	if (length > part->size)
		length = part->size;

	info.error_type = cpu_to_be32(err_type);
	info.seq_num = cpu_to_be32(error_log_cnt);

	tmp_index = part->index;

	rc = ppc_md.nvram_write((char *)&info, sizeof(info), &tmp_index);
	if (rc <= 0) {
		pr_err("%s: Failed nvram_write (%d)\n", __func__, rc);
		return rc;
	}

	rc = ppc_md.nvram_write(buff, length, &tmp_index);
	if (rc <= 0) {
		pr_err("%s: Failed nvram_write (%d)\n", __func__, rc);
		return rc;
	}

	return 0;
}

/* nvram_read_partition
 *
 * Reads nvram partition for at most 'length'
 */
int nvram_read_partition(struct nvram_os_partition *part, char *buff,
			 int length, unsigned int *err_type,
			 unsigned int *error_log_cnt)
{
	int rc;
	loff_t tmp_index;
	struct err_log_info info;

	if (part->index == -1)
		return -1;

	if (length > part->size)
		length = part->size;

	tmp_index = part->index;

	if (part->os_partition) {
		rc = ppc_md.nvram_read((char *)&info, sizeof(info), &tmp_index);
		if (rc <= 0) {
			pr_err("%s: Failed nvram_read (%d)\n", __func__, rc);
			return rc;
		}
	}

	rc = ppc_md.nvram_read(buff, length, &tmp_index);
	if (rc <= 0) {
		pr_err("%s: Failed nvram_read (%d)\n", __func__, rc);
		return rc;
	}

	if (part->os_partition) {
		*error_log_cnt = be32_to_cpu(info.seq_num);
		*err_type = be32_to_cpu(info.error_type);
	}

	return 0;
}

/* nvram_init_os_partition
 *
 * This sets up a partition with an "OS" signature.
 *
 * The general strategy is the following:
 * 1.) If a partition with the indicated name already exists...
 *	- If it's large enough, use it.
 *	- Otherwise, recycle it and keep going.
 * 2.) Search for a free partition that is large enough.
 * 3.) If there's not a free partition large enough, recycle any obsolete
 * OS partitions and try again.
 * 4.) Will first try getting a chunk that will satisfy the requested size.
 * 5.) If a chunk of the requested size cannot be allocated, then try finding
 * a chunk that will satisfy the minum needed.
 *
 * Returns 0 on success, else -1.
 */
int __init nvram_init_os_partition(struct nvram_os_partition *part)
{
	loff_t p;
	int size;

	/* Look for ours */
	p = nvram_find_partition(part->name, NVRAM_SIG_OS, &size);

	/* Found one but too small, remove it */
	if (p && size < part->min_size) {
		pr_info("nvram: Found too small %s partition,"
					" removing it...\n", part->name);
		nvram_remove_partition(part->name, NVRAM_SIG_OS, NULL);
		p = 0;
	}

	/* Create one if we didn't find */
	if (!p) {
		p = nvram_create_partition(part->name, NVRAM_SIG_OS,
					part->req_size, part->min_size);
		if (p == -ENOSPC) {
			pr_info("nvram: No room to create %s partition, "
				"deleting any obsolete OS partitions...\n",
				part->name);
			nvram_remove_partition(NULL, NVRAM_SIG_OS,
					nvram_os_partitions);
			p = nvram_create_partition(part->name, NVRAM_SIG_OS,
					part->req_size, part->min_size);
		}
	}

	if (p <= 0) {
		pr_err("nvram: Failed to find or create %s"
		       " partition, err %d\n", part->name, (int)p);
		return -1;
	}

	part->index = p;
	part->size = nvram_get_partition_size(p) - sizeof(struct err_log_info);

	return 0;
}

/* Derived from logfs_compress() */
static int nvram_compress(const void *in, void *out, size_t inlen,
							size_t outlen)
{
	int err, ret;

	ret = -EIO;
	err = zlib_deflateInit2(&stream, COMPR_LEVEL, Z_DEFLATED, WINDOW_BITS,
						MEM_LEVEL, Z_DEFAULT_STRATEGY);
	if (err != Z_OK)
		goto error;

	stream.next_in = in;
	stream.avail_in = inlen;
	stream.total_in = 0;
	stream.next_out = out;
	stream.avail_out = outlen;
	stream.total_out = 0;

	err = zlib_deflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END)
		goto error;

	err = zlib_deflateEnd(&stream);
	if (err != Z_OK)
		goto error;

	if (stream.total_out >= stream.total_in)
		goto error;

	ret = stream.total_out;
error:
	return ret;
}

/* Compress the text from big_oops_buf into oops_buf. */
static int zip_oops(size_t text_len)
{
	struct oops_log_info *oops_hdr = (struct oops_log_info *)oops_buf;
	int zipped_len = nvram_compress(big_oops_buf, oops_data, text_len,
								oops_data_sz);
	if (zipped_len < 0) {
		pr_err("nvram: compression failed; returned %d\n", zipped_len);
		pr_err("nvram: logging uncompressed oops/panic report\n");
		return -1;
	}
	oops_hdr->version = cpu_to_be16(OOPS_HDR_VERSION);
	oops_hdr->report_length = cpu_to_be16(zipped_len);
	oops_hdr->timestamp = cpu_to_be64(ktime_get_real_seconds());
	return 0;
}

#ifdef CONFIG_PSTORE
static int nvram_pstore_open(struct pstore_info *psi)
{
	/* Reset the iterator to start reading partitions again */
	read_type = -1;
	return 0;
}

/**
 * nvram_pstore_write - pstore write callback for nvram
 * @record:             pstore record to write, with @id to be set
 *
 * Called by pstore_dump() when an oops or panic report is logged in the
 * printk buffer.
 * Returns 0 on successful write.
 */
static int nvram_pstore_write(struct pstore_record *record)
{
	int rc;
	unsigned int err_type = ERR_TYPE_KERNEL_PANIC;
	struct oops_log_info *oops_hdr = (struct oops_log_info *) oops_buf;

	/* part 1 has the recent messages from printk buffer */
	if (record->part > 1 || (record->type != PSTORE_TYPE_DMESG))
		return -1;

	if (clobbering_unread_rtas_event())
		return -1;

	oops_hdr->version = cpu_to_be16(OOPS_HDR_VERSION);
	oops_hdr->report_length = cpu_to_be16(record->size);
	oops_hdr->timestamp = cpu_to_be64(ktime_get_real_seconds());

	if (record->compressed)
		err_type = ERR_TYPE_KERNEL_PANIC_GZ;

	rc = nvram_write_os_partition(&oops_log_partition, oops_buf,
		(int) (sizeof(*oops_hdr) + record->size), err_type,
		record->count);

	if (rc != 0)
		return rc;

	record->id = record->part;
	return 0;
}

/*
 * Reads the oops/panic report, rtas, of-config and common partition.
 * Returns the length of the data we read from each partition.
 * Returns 0 if we've been called before.
 */
static ssize_t nvram_pstore_read(struct pstore_record *record)
{
	struct oops_log_info *oops_hdr;
	unsigned int err_type, id_no, size = 0;
	struct nvram_os_partition *part = NULL;
	char *buff = NULL;
	int sig = 0;
	loff_t p;

	read_type++;

	switch (nvram_type_ids[read_type]) {
	case PSTORE_TYPE_DMESG:
		part = &oops_log_partition;
		record->type = PSTORE_TYPE_DMESG;
		break;
	case PSTORE_TYPE_PPC_COMMON:
		sig = NVRAM_SIG_SYS;
		part = &common_partition;
		record->type = PSTORE_TYPE_PPC_COMMON;
		record->id = PSTORE_TYPE_PPC_COMMON;
		record->time.tv_sec = 0;
		record->time.tv_nsec = 0;
		break;
#ifdef CONFIG_PPC_PSERIES
	case PSTORE_TYPE_PPC_RTAS:
		part = &rtas_log_partition;
		record->type = PSTORE_TYPE_PPC_RTAS;
		record->time.tv_sec = last_rtas_event;
		record->time.tv_nsec = 0;
		break;
	case PSTORE_TYPE_PPC_OF:
		sig = NVRAM_SIG_OF;
		part = &of_config_partition;
		record->type = PSTORE_TYPE_PPC_OF;
		record->id = PSTORE_TYPE_PPC_OF;
		record->time.tv_sec = 0;
		record->time.tv_nsec = 0;
		break;
#endif
#ifdef CONFIG_PPC_POWERNV
	case PSTORE_TYPE_PPC_OPAL:
		sig = NVRAM_SIG_FW;
		part = &skiboot_partition;
		record->type = PSTORE_TYPE_PPC_OPAL;
		record->id = PSTORE_TYPE_PPC_OPAL;
		record->time.tv_sec = 0;
		record->time.tv_nsec = 0;
		break;
#endif
	default:
		return 0;
	}

	if (!part->os_partition) {
		p = nvram_find_partition(part->name, sig, &size);
		if (p <= 0) {
			pr_err("nvram: Failed to find partition %s, "
				"err %d\n", part->name, (int)p);
			return 0;
		}
		part->index = p;
		part->size = size;
	}

	buff = kmalloc(part->size, GFP_KERNEL);

	if (!buff)
		return -ENOMEM;

	if (nvram_read_partition(part, buff, part->size, &err_type, &id_no)) {
		kfree(buff);
		return 0;
	}

	record->count = 0;

	if (part->os_partition)
		record->id = id_no;

	if (nvram_type_ids[read_type] == PSTORE_TYPE_DMESG) {
		size_t length, hdr_size;

		oops_hdr = (struct oops_log_info *)buff;
		if (be16_to_cpu(oops_hdr->version) < OOPS_HDR_VERSION) {
			/* Old format oops header had 2-byte record size */
			hdr_size = sizeof(u16);
			length = be16_to_cpu(oops_hdr->version);
			record->time.tv_sec = 0;
			record->time.tv_nsec = 0;
		} else {
			hdr_size = sizeof(*oops_hdr);
			length = be16_to_cpu(oops_hdr->report_length);
			record->time.tv_sec = be64_to_cpu(oops_hdr->timestamp);
			record->time.tv_nsec = 0;
		}
		record->buf = kmemdup(buff + hdr_size, length, GFP_KERNEL);
		kfree(buff);
		if (record->buf == NULL)
			return -ENOMEM;

		record->ecc_notice_size = 0;
		if (err_type == ERR_TYPE_KERNEL_PANIC_GZ)
			record->compressed = true;
		else
			record->compressed = false;
		return length;
	}

	record->buf = buff;
	return part->size;
}

static struct pstore_info nvram_pstore_info = {
	.owner = THIS_MODULE,
	.name = "nvram",
	.flags = PSTORE_FLAGS_DMESG,
	.open = nvram_pstore_open,
	.read = nvram_pstore_read,
	.write = nvram_pstore_write,
};

static int nvram_pstore_init(void)
{
	int rc = 0;

	if (machine_is(pseries)) {
		nvram_type_ids[2] = PSTORE_TYPE_PPC_RTAS;
		nvram_type_ids[3] = PSTORE_TYPE_PPC_OF;
	} else
		nvram_type_ids[2] = PSTORE_TYPE_PPC_OPAL;

	nvram_pstore_info.buf = oops_data;
	nvram_pstore_info.bufsize = oops_data_sz;

	rc = pstore_register(&nvram_pstore_info);
	if (rc && (rc != -EPERM))
		/* Print error only when pstore.backend == nvram */
		pr_err("nvram: pstore_register() failed, returned %d. "
				"Defaults to kmsg_dump\n", rc);

	return rc;
}
#else
static int nvram_pstore_init(void)
{
	return -1;
}
#endif

void __init nvram_init_oops_partition(int rtas_partition_exists)
{
	int rc;

	rc = nvram_init_os_partition(&oops_log_partition);
	if (rc != 0) {
#ifdef CONFIG_PPC_PSERIES
		if (!rtas_partition_exists) {
			pr_err("nvram: Failed to initialize oops partition!");
			return;
		}
		pr_notice("nvram: Using %s partition to log both"
			" RTAS errors and oops/panic reports\n",
			rtas_log_partition.name);
		memcpy(&oops_log_partition, &rtas_log_partition,
						sizeof(rtas_log_partition));
#else
		pr_err("nvram: Failed to initialize oops partition!");
		return;
#endif
	}
	oops_buf = kmalloc(oops_log_partition.size, GFP_KERNEL);
	if (!oops_buf) {
		pr_err("nvram: No memory for %s partition\n",
						oops_log_partition.name);
		return;
	}
	oops_data = oops_buf + sizeof(struct oops_log_info);
	oops_data_sz = oops_log_partition.size - sizeof(struct oops_log_info);

	rc = nvram_pstore_init();

	if (!rc)
		return;

	/*
	 * Figure compression (preceded by elimination of each line's <n>
	 * severity prefix) will reduce the oops/panic report to at most
	 * 45% of its original size.
	 */
	big_oops_buf_sz = (oops_data_sz * 100) / 45;
	big_oops_buf = kmalloc(big_oops_buf_sz, GFP_KERNEL);
	if (big_oops_buf) {
		stream.workspace =  kmalloc(zlib_deflate_workspacesize(
					WINDOW_BITS, MEM_LEVEL), GFP_KERNEL);
		if (!stream.workspace) {
			pr_err("nvram: No memory for compression workspace; "
				"skipping compression of %s partition data\n",
				oops_log_partition.name);
			kfree(big_oops_buf);
			big_oops_buf = NULL;
		}
	} else {
		pr_err("No memory for uncompressed %s data; "
			"skipping compression\n", oops_log_partition.name);
		stream.workspace = NULL;
	}

	rc = kmsg_dump_register(&nvram_kmsg_dumper);
	if (rc != 0) {
		pr_err("nvram: kmsg_dump_register() failed; returned %d\n", rc);
		kfree(oops_buf);
		kfree(big_oops_buf);
		kfree(stream.workspace);
	}
}

/*
 * This is our kmsg_dump callback, called after an oops or panic report
 * has been written to the printk buffer.  We want to capture as much
 * of the printk buffer as possible.  First, capture as much as we can
 * that we think will compress sufficiently to fit in the lnx,oops-log
 * partition.  If that's too much, go back and capture uncompressed text.
 */
static void oops_to_nvram(struct kmsg_dumper *dumper,
			  enum kmsg_dump_reason reason)
{
	struct oops_log_info *oops_hdr = (struct oops_log_info *)oops_buf;
	static unsigned int oops_count = 0;
	static struct kmsg_dump_iter iter;
	static bool panicking = false;
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;
	size_t text_len;
	unsigned int err_type = ERR_TYPE_KERNEL_PANIC_GZ;
	int rc = -1;

	switch (reason) {
	case KMSG_DUMP_SHUTDOWN:
		/* These are almost always orderly shutdowns. */
		return;
	case KMSG_DUMP_OOPS:
		break;
	case KMSG_DUMP_PANIC:
		panicking = true;
		break;
	case KMSG_DUMP_EMERG:
		if (panicking)
			/* Panic report already captured. */
			return;
		break;
	default:
		pr_err("%s: ignoring unrecognized KMSG_DUMP_* reason %d\n",
		       __func__, (int) reason);
		return;
	}

	if (clobbering_unread_rtas_event())
		return;

	if (!spin_trylock_irqsave(&lock, flags))
		return;

	if (big_oops_buf) {
		kmsg_dump_rewind(&iter);
		kmsg_dump_get_buffer(&iter, false,
				     big_oops_buf, big_oops_buf_sz, &text_len);
		rc = zip_oops(text_len);
	}
	if (rc != 0) {
		kmsg_dump_rewind(&iter);
		kmsg_dump_get_buffer(&iter, false,
				     oops_data, oops_data_sz, &text_len);
		err_type = ERR_TYPE_KERNEL_PANIC;
		oops_hdr->version = cpu_to_be16(OOPS_HDR_VERSION);
		oops_hdr->report_length = cpu_to_be16(text_len);
		oops_hdr->timestamp = cpu_to_be64(ktime_get_real_seconds());
	}

	(void) nvram_write_os_partition(&oops_log_partition, oops_buf,
		(int) (sizeof(*oops_hdr) + text_len), err_type,
		++oops_count);

	spin_unlock_irqrestore(&lock, flags);
}

#ifdef DEBUG_NVRAM
static void __init nvram_print_partitions(char * label)
{
	struct nvram_partition * tmp_part;
	
	printk(KERN_WARNING "--------%s---------\n", label);
	printk(KERN_WARNING "indx\t\tsig\tchks\tlen\tname\n");
	list_for_each_entry(tmp_part, &nvram_partitions, partition) {
		printk(KERN_WARNING "%4d    \t%02x\t%02x\t%d\t%12.12s\n",
		       tmp_part->index, tmp_part->header.signature,
		       tmp_part->header.checksum, tmp_part->header.length,
		       tmp_part->header.name);
	}
}
#endif


static int __init nvram_write_header(struct nvram_partition * part)
{
	loff_t tmp_index;
	int rc;
	struct nvram_header phead;

	memcpy(&phead, &part->header, NVRAM_HEADER_LEN);
	phead.length = cpu_to_be16(phead.length);

	tmp_index = part->index;
	rc = ppc_md.nvram_write((char *)&phead, NVRAM_HEADER_LEN, &tmp_index);

	return rc;
}


static unsigned char __init nvram_checksum(struct nvram_header *p)
{
	unsigned int c_sum, c_sum2;
	unsigned short *sp = (unsigned short *)p->name; /* assume 6 shorts */
	c_sum = p->signature + p->length + sp[0] + sp[1] + sp[2] + sp[3] + sp[4] + sp[5];

	/* The sum may have spilled into the 3rd byte.  Fold it back. */
	c_sum = ((c_sum & 0xffff) + (c_sum >> 16)) & 0xffff;
	/* The sum cannot exceed 2 bytes.  Fold it into a checksum */
	c_sum2 = (c_sum >> 8) + (c_sum << 8);
	c_sum = ((c_sum + c_sum2) >> 8) & 0xff;
	return c_sum;
}

/*
 * Per the criteria passed via nvram_remove_partition(), should this
 * partition be removed?  1=remove, 0=keep
 */
static int nvram_can_remove_partition(struct nvram_partition *part,
		const char *name, int sig, const char *exceptions[])
{
	if (part->header.signature != sig)
		return 0;
	if (name) {
		if (strncmp(name, part->header.name, 12))
			return 0;
	} else if (exceptions) {
		const char **except;
		for (except = exceptions; *except; except++) {
			if (!strncmp(*except, part->header.name, 12))
				return 0;
		}
	}
	return 1;
}

/**
 * nvram_remove_partition - Remove one or more partitions in nvram
 * @name: name of the partition to remove, or NULL for a
 *        signature only match
 * @sig: signature of the partition(s) to remove
 * @exceptions: When removing all partitions with a matching signature,
 *        leave these alone.
 */

int __init nvram_remove_partition(const char *name, int sig,
						const char *exceptions[])
{
	struct nvram_partition *part, *prev, *tmp;
	int rc;

	list_for_each_entry(part, &nvram_partitions, partition) {
		if (!nvram_can_remove_partition(part, name, sig, exceptions))
			continue;

		/* Make partition a free partition */
		part->header.signature = NVRAM_SIG_FREE;
		memset(part->header.name, 'w', 12);
		part->header.checksum = nvram_checksum(&part->header);
		rc = nvram_write_header(part);
		if (rc <= 0) {
			printk(KERN_ERR "nvram_remove_partition: nvram_write failed (%d)\n", rc);
			return rc;
		}
	}

	/* Merge contiguous ones */
	prev = NULL;
	list_for_each_entry_safe(part, tmp, &nvram_partitions, partition) {
		if (part->header.signature != NVRAM_SIG_FREE) {
			prev = NULL;
			continue;
		}
		if (prev) {
			prev->header.length += part->header.length;
			prev->header.checksum = nvram_checksum(&prev->header);
			rc = nvram_write_header(prev);
			if (rc <= 0) {
				printk(KERN_ERR "nvram_remove_partition: nvram_write failed (%d)\n", rc);
				return rc;
			}
			list_del(&part->partition);
			kfree(part);
		} else
			prev = part;
	}
	
	return 0;
}

/**
 * nvram_create_partition - Create a partition in nvram
 * @name: name of the partition to create
 * @sig: signature of the partition to create
 * @req_size: size of data to allocate in bytes
 * @min_size: minimum acceptable size (0 means req_size)
 *
 * Returns a negative error code or a positive nvram index
 * of the beginning of the data area of the newly created
 * partition. If you provided a min_size smaller than req_size
 * you need to query for the actual size yourself after the
 * call using nvram_partition_get_size().
 */
loff_t __init nvram_create_partition(const char *name, int sig,
				     int req_size, int min_size)
{
	struct nvram_partition *part;
	struct nvram_partition *new_part;
	struct nvram_partition *free_part = NULL;
	static char nv_init_vals[16];
	loff_t tmp_index;
	long size = 0;
	int rc;

	BUILD_BUG_ON(NVRAM_BLOCK_LEN != 16);

	/* Convert sizes from bytes to blocks */
	req_size = ALIGN(req_size, NVRAM_BLOCK_LEN) / NVRAM_BLOCK_LEN;
	min_size = ALIGN(min_size, NVRAM_BLOCK_LEN) / NVRAM_BLOCK_LEN;

	/* If no minimum size specified, make it the same as the
	 * requested size
	 */
	if (min_size == 0)
		min_size = req_size;
	if (min_size > req_size)
		return -EINVAL;

	/* Now add one block to each for the header */
	req_size += 1;
	min_size += 1;

	/* Find a free partition that will give us the maximum needed size 
	   If can't find one that will give us the minimum size needed */
	list_for_each_entry(part, &nvram_partitions, partition) {
		if (part->header.signature != NVRAM_SIG_FREE)
			continue;

		if (part->header.length >= req_size) {
			size = req_size;
			free_part = part;
			break;
		}
		if (part->header.length > size &&
		    part->header.length >= min_size) {
			size = part->header.length;
			free_part = part;
		}
	}
	if (!size)
		return -ENOSPC;
	
	/* Create our OS partition */
	new_part = kzalloc(sizeof(*new_part), GFP_KERNEL);
	if (!new_part) {
		pr_err("%s: kmalloc failed\n", __func__);
		return -ENOMEM;
	}

	new_part->index = free_part->index;
	new_part->header.signature = sig;
	new_part->header.length = size;
	memcpy(new_part->header.name, name, strnlen(name, sizeof(new_part->header.name)));
	new_part->header.checksum = nvram_checksum(&new_part->header);

	rc = nvram_write_header(new_part);
	if (rc <= 0) {
		pr_err("%s: nvram_write_header failed (%d)\n", __func__, rc);
		kfree(new_part);
		return rc;
	}
	list_add_tail(&new_part->partition, &free_part->partition);

	/* Adjust or remove the partition we stole the space from */
	if (free_part->header.length > size) {
		free_part->index += size * NVRAM_BLOCK_LEN;
		free_part->header.length -= size;
		free_part->header.checksum = nvram_checksum(&free_part->header);
		rc = nvram_write_header(free_part);
		if (rc <= 0) {
			pr_err("%s: nvram_write_header failed (%d)\n",
			       __func__, rc);
			return rc;
		}
	} else {
		list_del(&free_part->partition);
		kfree(free_part);
	} 

	/* Clear the new partition */
	for (tmp_index = new_part->index + NVRAM_HEADER_LEN;
	     tmp_index <  ((size - 1) * NVRAM_BLOCK_LEN);
	     tmp_index += NVRAM_BLOCK_LEN) {
		rc = ppc_md.nvram_write(nv_init_vals, NVRAM_BLOCK_LEN, &tmp_index);
		if (rc <= 0) {
			pr_err("%s: nvram_write failed (%d)\n",
			       __func__, rc);
			return rc;
		}
	}

	return new_part->index + NVRAM_HEADER_LEN;
}

/**
 * nvram_get_partition_size - Get the data size of an nvram partition
 * @data_index: This is the offset of the start of the data of
 *              the partition. The same value that is returned by
 *              nvram_create_partition().
 */
int nvram_get_partition_size(loff_t data_index)
{
	struct nvram_partition *part;
	
	list_for_each_entry(part, &nvram_partitions, partition) {
		if (part->index + NVRAM_HEADER_LEN == data_index)
			return (part->header.length - 1) * NVRAM_BLOCK_LEN;
	}
	return -1;
}


/**
 * nvram_find_partition - Find an nvram partition by signature and name
 * @name: Name of the partition or NULL for any name
 * @sig: Signature to test against
 * @out_size: if non-NULL, returns the size of the data part of the partition
 */
loff_t nvram_find_partition(const char *name, int sig, int *out_size)
{
	struct nvram_partition *p;

	list_for_each_entry(p, &nvram_partitions, partition) {
		if (p->header.signature == sig &&
		    (!name || !strncmp(p->header.name, name, 12))) {
			if (out_size)
				*out_size = (p->header.length - 1) *
					NVRAM_BLOCK_LEN;
			return p->index + NVRAM_HEADER_LEN;
		}
	}
	return 0;
}

int __init nvram_scan_partitions(void)
{
	loff_t cur_index = 0;
	struct nvram_header phead;
	struct nvram_partition * tmp_part;
	unsigned char c_sum;
	char * header;
	int total_size;
	int err;

	if (ppc_md.nvram_size == NULL || ppc_md.nvram_size() <= 0)
		return -ENODEV;
	total_size = ppc_md.nvram_size();
	
	header = kmalloc(NVRAM_HEADER_LEN, GFP_KERNEL);
	if (!header) {
		printk(KERN_ERR "nvram_scan_partitions: Failed kmalloc\n");
		return -ENOMEM;
	}

	while (cur_index < total_size) {

		err = ppc_md.nvram_read(header, NVRAM_HEADER_LEN, &cur_index);
		if (err != NVRAM_HEADER_LEN) {
			printk(KERN_ERR "nvram_scan_partitions: Error parsing "
			       "nvram partitions\n");
			goto out;
		}

		cur_index -= NVRAM_HEADER_LEN; /* nvram_read will advance us */

		memcpy(&phead, header, NVRAM_HEADER_LEN);

		phead.length = be16_to_cpu(phead.length);

		err = 0;
		c_sum = nvram_checksum(&phead);
		if (c_sum != phead.checksum) {
			printk(KERN_WARNING "WARNING: nvram partition checksum"
			       " was %02x, should be %02x!\n",
			       phead.checksum, c_sum);
			printk(KERN_WARNING "Terminating nvram partition scan\n");
			goto out;
		}
		if (!phead.length) {
			printk(KERN_WARNING "WARNING: nvram corruption "
			       "detected: 0-length partition\n");
			goto out;
		}
		tmp_part = kmalloc(sizeof(*tmp_part), GFP_KERNEL);
		err = -ENOMEM;
		if (!tmp_part) {
			printk(KERN_ERR "nvram_scan_partitions: kmalloc failed\n");
			goto out;
		}
		
		memcpy(&tmp_part->header, &phead, NVRAM_HEADER_LEN);
		tmp_part->index = cur_index;
		list_add_tail(&tmp_part->partition, &nvram_partitions);
		
		cur_index += phead.length * NVRAM_BLOCK_LEN;
	}
	err = 0;

#ifdef DEBUG_NVRAM
	nvram_print_partitions("NVRAM Partitions");
#endif

 out:
	kfree(header);
	return err;
}
