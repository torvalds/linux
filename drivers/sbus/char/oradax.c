/*
 * Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Oracle Data Analytics Accelerator (DAX)
 *
 * DAX is a coprocessor which resides on the SPARC M7 (DAX1) and M8
 * (DAX2) processor chips, and has direct access to the CPU's L3
 * caches as well as physical memory. It can perform several
 * operations on data streams with various input and output formats.
 * The driver provides a transport mechanism only and has limited
 * knowledge of the various opcodes and data formats. A user space
 * library provides high level services and translates these into low
 * level commands which are then passed into the driver and
 * subsequently the hypervisor and the coprocessor.  The library is
 * the recommended way for applications to use the coprocessor, and
 * the driver interface is not intended for general use.
 *
 * See Documentation/sparc/oradax/oracle-dax.txt for more details.
 */

#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/hypervisor.h>
#include <asm/mdesc.h>
#include <asm/oradax.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver for Oracle Data Analytics Accelerator");

#define	DAX_DBG_FLG_BASIC	0x01
#define	DAX_DBG_FLG_STAT	0x02
#define	DAX_DBG_FLG_INFO	0x04
#define	DAX_DBG_FLG_ALL		0xff

#define	dax_err(fmt, ...)      pr_err("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#define	dax_info(fmt, ...)     pr_info("%s: " fmt "\n", __func__, ##__VA_ARGS__)

#define	dax_dbg(fmt, ...)	do {					\
					if (dax_debug & DAX_DBG_FLG_BASIC)\
						dax_info(fmt, ##__VA_ARGS__); \
				} while (0)
#define	dax_stat_dbg(fmt, ...)	do {					\
					if (dax_debug & DAX_DBG_FLG_STAT) \
						dax_info(fmt, ##__VA_ARGS__); \
				} while (0)
#define	dax_info_dbg(fmt, ...)	do { \
					if (dax_debug & DAX_DBG_FLG_INFO) \
						dax_info(fmt, ##__VA_ARGS__); \
				} while (0)

#define	DAX1_MINOR		1
#define	DAX1_MAJOR		1
#define	DAX2_MINOR		0
#define	DAX2_MAJOR		2

#define	DAX1_STR    "ORCL,sun4v-dax"
#define	DAX2_STR    "ORCL,sun4v-dax2"

#define	DAX_CA_ELEMS		(DAX_MMAP_LEN / sizeof(struct dax_cca))

#define	DAX_CCB_USEC		100
#define	DAX_CCB_RETRIES		10000

/* stream types */
enum {
	OUT,
	PRI,
	SEC,
	TBL,
	NUM_STREAM_TYPES
};

/* completion status */
#define	CCA_STAT_NOT_COMPLETED	0
#define	CCA_STAT_COMPLETED	1
#define	CCA_STAT_FAILED		2
#define	CCA_STAT_KILLED		3
#define	CCA_STAT_NOT_RUN	4
#define	CCA_STAT_PIPE_OUT	5
#define	CCA_STAT_PIPE_SRC	6
#define	CCA_STAT_PIPE_DST	7

/* completion err */
#define	CCA_ERR_SUCCESS		0x0	/* no error */
#define	CCA_ERR_OVERFLOW	0x1	/* buffer overflow */
#define	CCA_ERR_DECODE		0x2	/* CCB decode error */
#define	CCA_ERR_PAGE_OVERFLOW	0x3	/* page overflow */
#define	CCA_ERR_KILLED		0x7	/* command was killed */
#define	CCA_ERR_TIMEOUT		0x8	/* Timeout */
#define	CCA_ERR_ADI		0x9	/* ADI error */
#define	CCA_ERR_DATA_FMT	0xA	/* data format error */
#define	CCA_ERR_OTHER_NO_RETRY	0xE	/* Other error, do not retry */
#define	CCA_ERR_OTHER_RETRY	0xF	/* Other error, retry */
#define	CCA_ERR_PARTIAL_SYMBOL	0x80	/* QP partial symbol warning */

/* CCB address types */
#define	DAX_ADDR_TYPE_NONE	0
#define	DAX_ADDR_TYPE_VA_ALT	1	/* secondary context */
#define	DAX_ADDR_TYPE_RA	2	/* real address */
#define	DAX_ADDR_TYPE_VA	3	/* virtual address */

/* dax_header_t opcode */
#define	DAX_OP_SYNC_NOP		0x0
#define	DAX_OP_EXTRACT		0x1
#define	DAX_OP_SCAN_VALUE	0x2
#define	DAX_OP_SCAN_RANGE	0x3
#define	DAX_OP_TRANSLATE	0x4
#define	DAX_OP_SELECT		0x5
#define	DAX_OP_INVERT		0x10	/* OR with translate, scan opcodes */

struct dax_header {
	u32 ccb_version:4;	/* 31:28 CCB Version */
				/* 27:24 Sync Flags */
	u32 pipe:1;		/* Pipeline */
	u32 longccb:1;		/* Longccb. Set for scan with lu2, lu3, lu4. */
	u32 cond:1;		/* Conditional */
	u32 serial:1;		/* Serial */
	u32 opcode:8;		/* 23:16 Opcode */
				/* 15:0 Address Type. */
	u32 reserved:3;		/* 15:13 reserved */
	u32 table_addr_type:2;	/* 12:11 Huffman Table Address Type */
	u32 out_addr_type:3;	/* 10:8 Destination Address Type */
	u32 sec_addr_type:3;	/* 7:5 Secondary Source Address Type */
	u32 pri_addr_type:3;	/* 4:2 Primary Source Address Type */
	u32 cca_addr_type:2;	/* 1:0 Completion Address Type */
};

struct dax_control {
	u32 pri_fmt:4;		/* 31:28 Primary Input Format */
	u32 pri_elem_size:5;	/* 27:23 Primary Input Element Size(less1) */
	u32 pri_offset:3;	/* 22:20 Primary Input Starting Offset */
	u32 sec_encoding:1;	/* 19    Secondary Input Encoding */
				/*	 (must be 0 for Select) */
	u32 sec_offset:3;	/* 18:16 Secondary Input Starting Offset */
	u32 sec_elem_size:2;	/* 15:14 Secondary Input Element Size */
				/*	 (must be 0 for Select) */
	u32 out_fmt:2;		/* 13:12 Output Format */
	u32 out_elem_size:2;	/* 11:10 Output Element Size */
	u32 misc:10;		/* 9:0 Opcode specific info */
};

struct dax_data_access {
	u64 flow_ctrl:2;	/* 63:62 Flow Control Type */
	u64 pipe_target:2;	/* 61:60 Pipeline Target */
	u64 out_buf_size:20;	/* 59:40 Output Buffer Size */
				/*	 (cachelines less 1) */
	u64 unused1:8;		/* 39:32 Reserved, Set to 0 */
	u64 out_alloc:5;	/* 31:27 Output Allocation */
	u64 unused2:1;		/* 26	 Reserved */
	u64 pri_len_fmt:2;	/* 25:24 Input Length Format */
	u64 pri_len:24;		/* 23:0  Input Element/Byte/Bit Count */
				/*	 (less 1) */
};

struct dax_ccb {
	struct dax_header hdr;	/* CCB Header */
	struct dax_control ctrl;/* Control Word */
	void *ca;		/* Completion Address */
	void *pri;		/* Primary Input Address */
	struct dax_data_access dac; /* Data Access Control */
	void *sec;		/* Secondary Input Address */
	u64 dword5;		/* depends on opcode */
	void *out;		/* Output Address */
	void *tbl;		/* Table Address or bitmap */
};

struct dax_cca {
	u8	status;		/* user may mwait on this address */
	u8	err;		/* user visible error notification */
	u8	rsvd[2];	/* reserved */
	u32	n_remaining;	/* for QP partial symbol warning */
	u32	output_sz;	/* output in bytes */
	u32	rsvd2;		/* reserved */
	u64	run_cycles;	/* run time in OCND2 cycles */
	u64	run_stats;	/* nothing reported in version 1.0 */
	u32	n_processed;	/* number input elements */
	u32	rsvd3[5];	/* reserved */
	u64	retval;		/* command return value */
	u64	rsvd4[8];	/* reserved */
};

/* per thread CCB context */
struct dax_ctx {
	struct dax_ccb		*ccb_buf;
	u64			ccb_buf_ra;	/* cached RA of ccb_buf  */
	struct dax_cca		*ca_buf;
	u64			ca_buf_ra;	/* cached RA of ca_buf   */
	struct page		*pages[DAX_CA_ELEMS][NUM_STREAM_TYPES];
						/* array of locked pages */
	struct task_struct	*owner;		/* thread that owns ctx  */
	struct task_struct	*client;	/* requesting thread     */
	union ccb_result	result;
	u32			ccb_count;
	u32			fail_count;
};

/* driver public entry points */
static int dax_open(struct inode *inode, struct file *file);
static ssize_t dax_read(struct file *filp, char __user *buf,
			size_t count, loff_t *ppos);
static ssize_t dax_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *ppos);
static int dax_devmap(struct file *f, struct vm_area_struct *vma);
static int dax_close(struct inode *i, struct file *f);

static const struct file_operations dax_fops = {
	.owner	=	THIS_MODULE,
	.open	=	dax_open,
	.read	=	dax_read,
	.write	=	dax_write,
	.mmap	=	dax_devmap,
	.release =	dax_close,
};

static int dax_ccb_exec(struct dax_ctx *ctx, const char __user *buf,
			size_t count, loff_t *ppos);
static int dax_ccb_info(u64 ca, struct ccb_info_result *info);
static int dax_ccb_kill(u64 ca, u16 *kill_res);

static struct cdev c_dev;
static struct class *cl;
static dev_t first;

static int max_ccb_version;
static int dax_debug;
module_param(dax_debug, int, 0644);
MODULE_PARM_DESC(dax_debug, "Debug flags");

static int __init dax_attach(void)
{
	unsigned long dummy, hv_rv, major, minor, minor_requested, max_ccbs;
	struct mdesc_handle *hp = mdesc_grab();
	char *prop, *dax_name;
	bool found = false;
	int len, ret = 0;
	u64 pn;

	if (hp == NULL) {
		dax_err("Unable to grab mdesc");
		return -ENODEV;
	}

	mdesc_for_each_node_by_name(hp, pn, "virtual-device") {
		prop = (char *)mdesc_get_property(hp, pn, "name", &len);
		if (prop == NULL)
			continue;
		if (strncmp(prop, "dax", strlen("dax")))
			continue;
		dax_dbg("Found node 0x%llx = %s", pn, prop);

		prop = (char *)mdesc_get_property(hp, pn, "compatible", &len);
		if (prop == NULL)
			continue;
		dax_dbg("Found node 0x%llx = %s", pn, prop);
		found = true;
		break;
	}

	if (!found) {
		dax_err("No DAX device found");
		ret = -ENODEV;
		goto done;
	}

	if (strncmp(prop, DAX2_STR, strlen(DAX2_STR)) == 0) {
		dax_name = DAX_NAME "2";
		major = DAX2_MAJOR;
		minor_requested = DAX2_MINOR;
		max_ccb_version = 1;
		dax_dbg("MD indicates DAX2 coprocessor");
	} else if (strncmp(prop, DAX1_STR, strlen(DAX1_STR)) == 0) {
		dax_name = DAX_NAME "1";
		major = DAX1_MAJOR;
		minor_requested = DAX1_MINOR;
		max_ccb_version = 0;
		dax_dbg("MD indicates DAX1 coprocessor");
	} else {
		dax_err("Unknown dax type: %s", prop);
		ret = -ENODEV;
		goto done;
	}

	minor = minor_requested;
	dax_dbg("Registering DAX HV api with major %ld minor %ld", major,
		minor);
	if (sun4v_hvapi_register(HV_GRP_DAX, major, &minor)) {
		dax_err("hvapi_register failed");
		ret = -ENODEV;
		goto done;
	} else {
		dax_dbg("Max minor supported by HV = %ld (major %ld)", minor,
			major);
		minor = min(minor, minor_requested);
		dax_dbg("registered DAX major %ld minor %ld", major, minor);
	}

	/* submit a zero length ccb array to query coprocessor queue size */
	hv_rv = sun4v_ccb_submit(0, 0, HV_CCB_QUERY_CMD, 0, &max_ccbs, &dummy);
	if (hv_rv != 0) {
		dax_err("get_hwqueue_size failed with status=%ld and max_ccbs=%ld",
			hv_rv, max_ccbs);
		ret = -ENODEV;
		goto done;
	}

	if (max_ccbs != DAX_MAX_CCBS) {
		dax_err("HV reports unsupported max_ccbs=%ld", max_ccbs);
		ret = -ENODEV;
		goto done;
	}

	if (alloc_chrdev_region(&first, 0, 1, DAX_NAME) < 0) {
		dax_err("alloc_chrdev_region failed");
		ret = -ENXIO;
		goto done;
	}

	cl = class_create(THIS_MODULE, DAX_NAME);
	if (IS_ERR(cl)) {
		dax_err("class_create failed");
		ret = PTR_ERR(cl);
		goto class_error;
	}

	if (device_create(cl, NULL, first, NULL, dax_name) == NULL) {
		dax_err("device_create failed");
		ret = -ENXIO;
		goto device_error;
	}

	cdev_init(&c_dev, &dax_fops);
	if (cdev_add(&c_dev, first, 1) == -1) {
		dax_err("cdev_add failed");
		ret = -ENXIO;
		goto cdev_error;
	}

	pr_info("Attached DAX module\n");
	goto done;

cdev_error:
	device_destroy(cl, first);
device_error:
	class_destroy(cl);
class_error:
	unregister_chrdev_region(first, 1);
done:
	mdesc_release(hp);
	return ret;
}
module_init(dax_attach);

static void __exit dax_detach(void)
{
	pr_info("Cleaning up DAX module\n");
	cdev_del(&c_dev);
	device_destroy(cl, first);
	class_destroy(cl);
	unregister_chrdev_region(first, 1);
}
module_exit(dax_detach);

/* map completion area */
static int dax_devmap(struct file *f, struct vm_area_struct *vma)
{
	struct dax_ctx *ctx = (struct dax_ctx *)f->private_data;
	size_t len = vma->vm_end - vma->vm_start;

	dax_dbg("len=0x%lx, flags=0x%lx", len, vma->vm_flags);

	if (ctx->owner != current) {
		dax_dbg("devmap called from wrong thread");
		return -EINVAL;
	}

	if (len != DAX_MMAP_LEN) {
		dax_dbg("len(%lu) != DAX_MMAP_LEN(%d)", len, DAX_MMAP_LEN);
		return -EINVAL;
	}

	/* completion area is mapped read-only for user */
	if (vma->vm_flags & VM_WRITE)
		return -EPERM;
	vma->vm_flags &= ~VM_MAYWRITE;

	if (remap_pfn_range(vma, vma->vm_start, ctx->ca_buf_ra >> PAGE_SHIFT,
			    len, vma->vm_page_prot))
		return -EAGAIN;

	dax_dbg("mmapped completion area at uva 0x%lx", vma->vm_start);
	return 0;
}

/* Unlock user pages. Called during dequeue or device close */
static void dax_unlock_pages(struct dax_ctx *ctx, int ccb_index, int nelem)
{
	int i, j;

	for (i = ccb_index; i < ccb_index + nelem; i++) {
		for (j = 0; j < NUM_STREAM_TYPES; j++) {
			struct page *p = ctx->pages[i][j];

			if (p) {
				dax_dbg("freeing page %p", p);
				if (j == OUT)
					set_page_dirty(p);
				put_page(p);
				ctx->pages[i][j] = NULL;
			}
		}
	}
}

static int dax_lock_page(void *va, struct page **p)
{
	int ret;

	dax_dbg("uva %p", va);

	ret = get_user_pages_fast((unsigned long)va, 1, 1, p);
	if (ret == 1) {
		dax_dbg("locked page %p, for VA %p", *p, va);
		return 0;
	}

	dax_dbg("get_user_pages failed, va=%p, ret=%d", va, ret);
	return -1;
}

static int dax_lock_pages(struct dax_ctx *ctx, int idx,
			  int nelem, u64 *err_va)
{
	int i;

	for (i = 0; i < nelem; i++) {
		struct dax_ccb *ccbp = &ctx->ccb_buf[i];

		/*
		 * For each address in the CCB whose type is virtual,
		 * lock the page and change the type to virtual alternate
		 * context. On error, return the offending address in
		 * err_va.
		 */
		if (ccbp->hdr.out_addr_type == DAX_ADDR_TYPE_VA) {
			dax_dbg("output");
			if (dax_lock_page(ccbp->out,
					  &ctx->pages[i + idx][OUT]) != 0) {
				*err_va = (u64)ccbp->out;
				goto error;
			}
			ccbp->hdr.out_addr_type = DAX_ADDR_TYPE_VA_ALT;
		}

		if (ccbp->hdr.pri_addr_type == DAX_ADDR_TYPE_VA) {
			dax_dbg("input");
			if (dax_lock_page(ccbp->pri,
					  &ctx->pages[i + idx][PRI]) != 0) {
				*err_va = (u64)ccbp->pri;
				goto error;
			}
			ccbp->hdr.pri_addr_type = DAX_ADDR_TYPE_VA_ALT;
		}

		if (ccbp->hdr.sec_addr_type == DAX_ADDR_TYPE_VA) {
			dax_dbg("sec input");
			if (dax_lock_page(ccbp->sec,
					  &ctx->pages[i + idx][SEC]) != 0) {
				*err_va = (u64)ccbp->sec;
				goto error;
			}
			ccbp->hdr.sec_addr_type = DAX_ADDR_TYPE_VA_ALT;
		}

		if (ccbp->hdr.table_addr_type == DAX_ADDR_TYPE_VA) {
			dax_dbg("tbl");
			if (dax_lock_page(ccbp->tbl,
					  &ctx->pages[i + idx][TBL]) != 0) {
				*err_va = (u64)ccbp->tbl;
				goto error;
			}
			ccbp->hdr.table_addr_type = DAX_ADDR_TYPE_VA_ALT;
		}

		/* skip over 2nd 64 bytes of long CCB */
		if (ccbp->hdr.longccb)
			i++;
	}
	return DAX_SUBMIT_OK;

error:
	dax_unlock_pages(ctx, idx, nelem);
	return DAX_SUBMIT_ERR_NOACCESS;
}

static void dax_ccb_wait(struct dax_ctx *ctx, int idx)
{
	int ret, nretries;
	u16 kill_res;

	dax_dbg("idx=%d", idx);

	for (nretries = 0; nretries < DAX_CCB_RETRIES; nretries++) {
		if (ctx->ca_buf[idx].status == CCA_STAT_NOT_COMPLETED)
			udelay(DAX_CCB_USEC);
		else
			return;
	}
	dax_dbg("ctx (%p): CCB[%d] timed out, wait usec=%d, retries=%d. Killing ccb",
		(void *)ctx, idx, DAX_CCB_USEC, DAX_CCB_RETRIES);

	ret = dax_ccb_kill(ctx->ca_buf_ra + idx * sizeof(struct dax_cca),
			   &kill_res);
	dax_dbg("Kill CCB[%d] %s", idx, ret ? "failed" : "succeeded");
}

static int dax_close(struct inode *ino, struct file *f)
{
	struct dax_ctx *ctx = (struct dax_ctx *)f->private_data;
	int i;

	f->private_data = NULL;

	for (i = 0; i < DAX_CA_ELEMS; i++) {
		if (ctx->ca_buf[i].status == CCA_STAT_NOT_COMPLETED) {
			dax_dbg("CCB[%d] not completed", i);
			dax_ccb_wait(ctx, i);
		}
		dax_unlock_pages(ctx, i, 1);
	}

	kfree(ctx->ccb_buf);
	kfree(ctx->ca_buf);
	dax_stat_dbg("CCBs: %d good, %d bad", ctx->ccb_count, ctx->fail_count);
	kfree(ctx);

	return 0;
}

static ssize_t dax_read(struct file *f, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct dax_ctx *ctx = f->private_data;

	if (ctx->client != current)
		return -EUSERS;

	ctx->client = NULL;

	if (count != sizeof(union ccb_result))
		return -EINVAL;
	if (copy_to_user(buf, &ctx->result, sizeof(union ccb_result)))
		return -EFAULT;
	return count;
}

static ssize_t dax_write(struct file *f, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct dax_ctx *ctx = f->private_data;
	struct dax_command hdr;
	unsigned long ca;
	int i, idx, ret;

	if (ctx->client != NULL)
		return -EINVAL;

	if (count == 0 || count > DAX_MAX_CCBS * sizeof(struct dax_ccb))
		return -EINVAL;

	if (count % sizeof(struct dax_ccb) == 0)
		return dax_ccb_exec(ctx, buf, count, ppos); /* CCB EXEC */

	if (count != sizeof(struct dax_command))
		return -EINVAL;

	/* immediate command */
	if (ctx->owner != current)
		return -EUSERS;

	if (copy_from_user(&hdr, buf, sizeof(hdr)))
		return -EFAULT;

	ca = ctx->ca_buf_ra + hdr.ca_offset;

	switch (hdr.command) {
	case CCB_KILL:
		if (hdr.ca_offset >= DAX_MMAP_LEN) {
			dax_dbg("invalid ca_offset (%d) >= ca_buflen (%d)",
				hdr.ca_offset, DAX_MMAP_LEN);
			return -EINVAL;
		}

		ret = dax_ccb_kill(ca, &ctx->result.kill.action);
		if (ret != 0) {
			dax_dbg("dax_ccb_kill failed (ret=%d)", ret);
			return ret;
		}

		dax_info_dbg("killed (ca_offset %d)", hdr.ca_offset);
		idx = hdr.ca_offset / sizeof(struct dax_cca);
		ctx->ca_buf[idx].status = CCA_STAT_KILLED;
		ctx->ca_buf[idx].err = CCA_ERR_KILLED;
		ctx->client = current;
		return count;

	case CCB_INFO:
		if (hdr.ca_offset >= DAX_MMAP_LEN) {
			dax_dbg("invalid ca_offset (%d) >= ca_buflen (%d)",
				hdr.ca_offset, DAX_MMAP_LEN);
			return -EINVAL;
		}

		ret = dax_ccb_info(ca, &ctx->result.info);
		if (ret != 0) {
			dax_dbg("dax_ccb_info failed (ret=%d)", ret);
			return ret;
		}

		dax_info_dbg("info succeeded on ca_offset %d", hdr.ca_offset);
		ctx->client = current;
		return count;

	case CCB_DEQUEUE:
		for (i = 0; i < DAX_CA_ELEMS; i++) {
			if (ctx->ca_buf[i].status !=
			    CCA_STAT_NOT_COMPLETED)
				dax_unlock_pages(ctx, i, 1);
		}
		return count;

	default:
		return -EINVAL;
	}
}

static int dax_open(struct inode *inode, struct file *f)
{
	struct dax_ctx *ctx = NULL;
	int i;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL)
		goto done;

	ctx->ccb_buf = kcalloc(DAX_MAX_CCBS, sizeof(struct dax_ccb),
			       GFP_KERNEL);
	if (ctx->ccb_buf == NULL)
		goto done;

	ctx->ccb_buf_ra = virt_to_phys(ctx->ccb_buf);
	dax_dbg("ctx->ccb_buf=0x%p, ccb_buf_ra=0x%llx",
		(void *)ctx->ccb_buf, ctx->ccb_buf_ra);

	/* allocate CCB completion area buffer */
	ctx->ca_buf = kzalloc(DAX_MMAP_LEN, GFP_KERNEL);
	if (ctx->ca_buf == NULL)
		goto alloc_error;
	for (i = 0; i < DAX_CA_ELEMS; i++)
		ctx->ca_buf[i].status = CCA_STAT_COMPLETED;

	ctx->ca_buf_ra = virt_to_phys(ctx->ca_buf);
	dax_dbg("ctx=0x%p, ctx->ca_buf=0x%p, ca_buf_ra=0x%llx",
		(void *)ctx, (void *)ctx->ca_buf, ctx->ca_buf_ra);

	ctx->owner = current;
	f->private_data = ctx;
	return 0;

alloc_error:
	kfree(ctx->ccb_buf);
done:
	kfree(ctx);
	return -ENOMEM;
}

static char *dax_hv_errno(unsigned long hv_ret, int *ret)
{
	switch (hv_ret) {
	case HV_EBADALIGN:
		*ret = -EFAULT;
		return "HV_EBADALIGN";
	case HV_ENORADDR:
		*ret = -EFAULT;
		return "HV_ENORADDR";
	case HV_EINVAL:
		*ret = -EINVAL;
		return "HV_EINVAL";
	case HV_EWOULDBLOCK:
		*ret = -EAGAIN;
		return "HV_EWOULDBLOCK";
	case HV_ENOACCESS:
		*ret = -EPERM;
		return "HV_ENOACCESS";
	default:
		break;
	}

	*ret = -EIO;
	return "UNKNOWN";
}

static int dax_ccb_kill(u64 ca, u16 *kill_res)
{
	unsigned long hv_ret;
	int count, ret = 0;
	char *err_str;

	for (count = 0; count < DAX_CCB_RETRIES; count++) {
		dax_dbg("attempting kill on ca_ra 0x%llx", ca);
		hv_ret = sun4v_ccb_kill(ca, kill_res);

		if (hv_ret == HV_EOK) {
			dax_info_dbg("HV_EOK (ca_ra 0x%llx): %d", ca,
				     *kill_res);
		} else {
			err_str = dax_hv_errno(hv_ret, &ret);
			dax_dbg("%s (ca_ra 0x%llx)", err_str, ca);
		}

		if (ret != -EAGAIN)
			return ret;
		dax_info_dbg("ccb_kill count = %d", count);
		udelay(DAX_CCB_USEC);
	}

	return -EAGAIN;
}

static int dax_ccb_info(u64 ca, struct ccb_info_result *info)
{
	unsigned long hv_ret;
	char *err_str;
	int ret = 0;

	dax_dbg("attempting info on ca_ra 0x%llx", ca);
	hv_ret = sun4v_ccb_info(ca, info);

	if (hv_ret == HV_EOK) {
		dax_info_dbg("HV_EOK (ca_ra 0x%llx): %d", ca, info->state);
		if (info->state == DAX_CCB_ENQUEUED) {
			dax_info_dbg("dax_unit %d, queue_num %d, queue_pos %d",
				     info->inst_num, info->q_num, info->q_pos);
		}
	} else {
		err_str = dax_hv_errno(hv_ret, &ret);
		dax_dbg("%s (ca_ra 0x%llx)", err_str, ca);
	}

	return ret;
}

static void dax_prt_ccbs(struct dax_ccb *ccb, int nelem)
{
	int i, j;
	u64 *ccbp;

	dax_dbg("ccb buffer:");
	for (i = 0; i < nelem; i++) {
		ccbp = (u64 *)&ccb[i];
		dax_dbg(" %sccb[%d]", ccb[i].hdr.longccb ? "long " : "",  i);
		for (j = 0; j < 8; j++)
			dax_dbg("\tccb[%d].dwords[%d]=0x%llx",
				i, j, *(ccbp + j));
	}
}

/*
 * Validates user CCB content.  Also sets completion address and address types
 * for all addresses contained in CCB.
 */
static int dax_preprocess_usr_ccbs(struct dax_ctx *ctx, int idx, int nelem)
{
	int i;

	/*
	 * The user is not allowed to specify real address types in
	 * the CCB header.  This must be enforced by the kernel before
	 * submitting the CCBs to HV.  The only allowed values for all
	 * address fields are VA or IMM
	 */
	for (i = 0; i < nelem; i++) {
		struct dax_ccb *ccbp = &ctx->ccb_buf[i];
		unsigned long ca_offset;

		if (ccbp->hdr.ccb_version > max_ccb_version)
			return DAX_SUBMIT_ERR_CCB_INVAL;

		switch (ccbp->hdr.opcode) {
		case DAX_OP_SYNC_NOP:
		case DAX_OP_EXTRACT:
		case DAX_OP_SCAN_VALUE:
		case DAX_OP_SCAN_RANGE:
		case DAX_OP_TRANSLATE:
		case DAX_OP_SCAN_VALUE | DAX_OP_INVERT:
		case DAX_OP_SCAN_RANGE | DAX_OP_INVERT:
		case DAX_OP_TRANSLATE | DAX_OP_INVERT:
		case DAX_OP_SELECT:
			break;
		default:
			return DAX_SUBMIT_ERR_CCB_INVAL;
		}

		if (ccbp->hdr.out_addr_type != DAX_ADDR_TYPE_VA &&
		    ccbp->hdr.out_addr_type != DAX_ADDR_TYPE_NONE) {
			dax_dbg("invalid out_addr_type in user CCB[%d]", i);
			return DAX_SUBMIT_ERR_CCB_INVAL;
		}

		if (ccbp->hdr.pri_addr_type != DAX_ADDR_TYPE_VA &&
		    ccbp->hdr.pri_addr_type != DAX_ADDR_TYPE_NONE) {
			dax_dbg("invalid pri_addr_type in user CCB[%d]", i);
			return DAX_SUBMIT_ERR_CCB_INVAL;
		}

		if (ccbp->hdr.sec_addr_type != DAX_ADDR_TYPE_VA &&
		    ccbp->hdr.sec_addr_type != DAX_ADDR_TYPE_NONE) {
			dax_dbg("invalid sec_addr_type in user CCB[%d]", i);
			return DAX_SUBMIT_ERR_CCB_INVAL;
		}

		if (ccbp->hdr.table_addr_type != DAX_ADDR_TYPE_VA &&
		    ccbp->hdr.table_addr_type != DAX_ADDR_TYPE_NONE) {
			dax_dbg("invalid table_addr_type in user CCB[%d]", i);
			return DAX_SUBMIT_ERR_CCB_INVAL;
		}

		/* set completion (real) address and address type */
		ccbp->hdr.cca_addr_type = DAX_ADDR_TYPE_RA;
		ca_offset = (idx + i) * sizeof(struct dax_cca);
		ccbp->ca = (void *)ctx->ca_buf_ra + ca_offset;
		memset(&ctx->ca_buf[idx + i], 0, sizeof(struct dax_cca));

		dax_dbg("ccb[%d]=%p, ca_offset=0x%lx, compl RA=0x%llx",
			i, ccbp, ca_offset, ctx->ca_buf_ra + ca_offset);

		/* skip over 2nd 64 bytes of long CCB */
		if (ccbp->hdr.longccb)
			i++;
	}

	return DAX_SUBMIT_OK;
}

static int dax_ccb_exec(struct dax_ctx *ctx, const char __user *buf,
			size_t count, loff_t *ppos)
{
	unsigned long accepted_len, hv_rv;
	int i, idx, nccbs, naccepted;

	ctx->client = current;
	idx = *ppos;
	nccbs = count / sizeof(struct dax_ccb);

	if (ctx->owner != current) {
		dax_dbg("wrong thread");
		ctx->result.exec.status = DAX_SUBMIT_ERR_THR_INIT;
		return 0;
	}
	dax_dbg("args: ccb_buf_len=%ld, idx=%d", count, idx);

	/* for given index and length, verify ca_buf range exists */
	if (idx < 0 || idx > (DAX_CA_ELEMS - nccbs)) {
		ctx->result.exec.status = DAX_SUBMIT_ERR_NO_CA_AVAIL;
		return 0;
	}

	/*
	 * Copy CCBs into kernel buffer to prevent modification by the
	 * user in between validation and submission.
	 */
	if (copy_from_user(ctx->ccb_buf, buf, count)) {
		dax_dbg("copyin of user CCB buffer failed");
		ctx->result.exec.status = DAX_SUBMIT_ERR_CCB_ARR_MMU_MISS;
		return 0;
	}

	/* check to see if ca_buf[idx] .. ca_buf[idx + nccbs] are available */
	for (i = idx; i < idx + nccbs; i++) {
		if (ctx->ca_buf[i].status == CCA_STAT_NOT_COMPLETED) {
			dax_dbg("CA range not available, dequeue needed");
			ctx->result.exec.status = DAX_SUBMIT_ERR_NO_CA_AVAIL;
			return 0;
		}
	}
	dax_unlock_pages(ctx, idx, nccbs);

	ctx->result.exec.status = dax_preprocess_usr_ccbs(ctx, idx, nccbs);
	if (ctx->result.exec.status != DAX_SUBMIT_OK)
		return 0;

	ctx->result.exec.status = dax_lock_pages(ctx, idx, nccbs,
						 &ctx->result.exec.status_data);
	if (ctx->result.exec.status != DAX_SUBMIT_OK)
		return 0;

	if (dax_debug & DAX_DBG_FLG_BASIC)
		dax_prt_ccbs(ctx->ccb_buf, nccbs);

	hv_rv = sun4v_ccb_submit(ctx->ccb_buf_ra, count,
				 HV_CCB_QUERY_CMD | HV_CCB_VA_SECONDARY, 0,
				 &accepted_len, &ctx->result.exec.status_data);

	switch (hv_rv) {
	case HV_EOK:
		/*
		 * Hcall succeeded with no errors but the accepted
		 * length may be less than the requested length.  The
		 * only way the driver can resubmit the remainder is
		 * to wait for completion of the submitted CCBs since
		 * there is no way to guarantee the ordering semantics
		 * required by the client applications.  Therefore we
		 * let the user library deal with resubmissions.
		 */
		ctx->result.exec.status = DAX_SUBMIT_OK;
		break;
	case HV_EWOULDBLOCK:
		/*
		 * This is a transient HV API error. The user library
		 * can retry.
		 */
		dax_dbg("hcall returned HV_EWOULDBLOCK");
		ctx->result.exec.status = DAX_SUBMIT_ERR_WOULDBLOCK;
		break;
	case HV_ENOMAP:
		/*
		 * HV was unable to translate a VA. The VA it could
		 * not translate is returned in the status_data param.
		 */
		dax_dbg("hcall returned HV_ENOMAP");
		ctx->result.exec.status = DAX_SUBMIT_ERR_NOMAP;
		break;
	case HV_EINVAL:
		/*
		 * This is the result of an invalid user CCB as HV is
		 * validating some of the user CCB fields.  Pass this
		 * error back to the user. There is no supporting info
		 * to isolate the invalid field.
		 */
		dax_dbg("hcall returned HV_EINVAL");
		ctx->result.exec.status = DAX_SUBMIT_ERR_CCB_INVAL;
		break;
	case HV_ENOACCESS:
		/*
		 * HV found a VA that did not have the appropriate
		 * permissions (such as the w bit). The VA in question
		 * is returned in status_data param.
		 */
		dax_dbg("hcall returned HV_ENOACCESS");
		ctx->result.exec.status = DAX_SUBMIT_ERR_NOACCESS;
		break;
	case HV_EUNAVAILABLE:
		/*
		 * The requested CCB operation could not be performed
		 * at this time. Return the specific unavailable code
		 * in the status_data field.
		 */
		dax_dbg("hcall returned HV_EUNAVAILABLE");
		ctx->result.exec.status = DAX_SUBMIT_ERR_UNAVAIL;
		break;
	default:
		ctx->result.exec.status = DAX_SUBMIT_ERR_INTERNAL;
		dax_dbg("unknown hcall return value (%ld)", hv_rv);
		break;
	}

	/* unlock pages associated with the unaccepted CCBs */
	naccepted = accepted_len / sizeof(struct dax_ccb);
	dax_unlock_pages(ctx, idx + naccepted, nccbs - naccepted);

	/* mark unaccepted CCBs as not completed */
	for (i = idx + naccepted; i < idx + nccbs; i++)
		ctx->ca_buf[i].status = CCA_STAT_COMPLETED;

	ctx->ccb_count += naccepted;
	ctx->fail_count += nccbs - naccepted;

	dax_dbg("hcall rv=%ld, accepted_len=%ld, status_data=0x%llx, ret status=%d",
		hv_rv, accepted_len, ctx->result.exec.status_data,
		ctx->result.exec.status);

	if (count == accepted_len)
		ctx->client = NULL; /* no read needed to complete protocol */
	return accepted_len;
}
