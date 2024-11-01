// SPDX-License-Identifier: GPL-2.0
/*
 * Author(s)......: Carsten Otte <cotte@de.ibm.com>
 * 		    Rob M van der Heij <rvdheij@nl.ibm.com>
 * 		    Steven Shultz <shultzss@us.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Copyright IBM Corp. 2002, 2004
 */

#define KMSG_COMPONENT "extmem"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <linux/refcount.h>
#include <linux/pgtable.h>
#include <asm/diag.h>
#include <asm/page.h>
#include <asm/ebcdic.h>
#include <asm/errno.h>
#include <asm/extmem.h>
#include <asm/cpcmd.h>
#include <asm/setup.h>

#define DCSS_PURGESEG   0x08
#define DCSS_LOADSHRX	0x20
#define DCSS_LOADNSRX	0x24
#define DCSS_FINDSEGX	0x2c
#define DCSS_SEGEXTX	0x38
#define DCSS_FINDSEGA   0x0c

struct qrange {
	unsigned long  start; /* last byte type */
	unsigned long  end;   /* last byte reserved */
};

struct qout64 {
	unsigned long segstart;
	unsigned long segend;
	int segcnt;
	int segrcnt;
	struct qrange range[6];
};

struct qin64 {
	char qopcode;
	char rsrv1[3];
	char qrcode;
	char rsrv2[3];
	char qname[8];
	unsigned int qoutptr;
	short int qoutlen;
};

struct dcss_segment {
	struct list_head list;
	char dcss_name[8];
	char res_name[16];
	unsigned long start_addr;
	unsigned long end;
	refcount_t ref_count;
	int do_nonshared;
	unsigned int vm_segtype;
	struct qrange range[6];
	int segcnt;
	struct resource *res;
};

static DEFINE_MUTEX(dcss_lock);
static LIST_HEAD(dcss_list);
static char *segtype_string[] = { "SW", "EW", "SR", "ER", "SN", "EN", "SC",
					"EW/EN-MIXED" };
static int loadshr_scode = DCSS_LOADSHRX;
static int loadnsr_scode = DCSS_LOADNSRX;
static int purgeseg_scode = DCSS_PURGESEG;
static int segext_scode = DCSS_SEGEXTX;

/*
 * Create the 8 bytes, ebcdic VM segment name from
 * an ascii name.
 */
static void
dcss_mkname(char *name, char *dcss_name)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (name[i] == '\0')
			break;
		dcss_name[i] = toupper(name[i]);
	}
	for (; i < 8; i++)
		dcss_name[i] = ' ';
	ASCEBC(dcss_name, 8);
}


/*
 * search all segments in dcss_list, and return the one
 * namend *name. If not found, return NULL.
 */
static struct dcss_segment *
segment_by_name (char *name)
{
	char dcss_name[9];
	struct list_head *l;
	struct dcss_segment *tmp, *retval = NULL;

	BUG_ON(!mutex_is_locked(&dcss_lock));
	dcss_mkname (name, dcss_name);
	list_for_each (l, &dcss_list) {
		tmp = list_entry (l, struct dcss_segment, list);
		if (memcmp(tmp->dcss_name, dcss_name, 8) == 0) {
			retval = tmp;
			break;
		}
	}
	return retval;
}


/*
 * Perform a function on a dcss segment.
 */
static inline int
dcss_diag(int *func, void *parameter,
           unsigned long *ret1, unsigned long *ret2)
{
	unsigned long rx, ry;
	int rc;

	rx = (unsigned long) parameter;
	ry = (unsigned long) *func;

	diag_stat_inc(DIAG_STAT_X064);
	asm volatile(
		"	diag	%0,%1,0x64\n"
		"	ipm	%2\n"
		"	srl	%2,28\n"
		: "+d" (rx), "+d" (ry), "=d" (rc) : : "cc");
	*ret1 = rx;
	*ret2 = ry;
	return rc;
}

static inline int
dcss_diag_translate_rc (int vm_rc) {
	if (vm_rc == 44)
		return -ENOENT;
	return -EIO;
}


/* do a diag to get info about a segment.
 * fills start_address, end and vm_segtype fields
 */
static int
query_segment_type (struct dcss_segment *seg)
{
	unsigned long dummy, vmrc;
	int diag_cc, rc, i;
	struct qout64 *qout;
	struct qin64 *qin;

	qin = kmalloc(sizeof(*qin), GFP_KERNEL | GFP_DMA);
	qout = kmalloc(sizeof(*qout), GFP_KERNEL | GFP_DMA);
	if ((qin == NULL) || (qout == NULL)) {
		rc = -ENOMEM;
		goto out_free;
	}

	/* initialize diag input parameters */
	qin->qopcode = DCSS_FINDSEGA;
	qin->qoutptr = (unsigned long) qout;
	qin->qoutlen = sizeof(struct qout64);
	memcpy (qin->qname, seg->dcss_name, 8);

	diag_cc = dcss_diag(&segext_scode, qin, &dummy, &vmrc);

	if (diag_cc < 0) {
		rc = diag_cc;
		goto out_free;
	}
	if (diag_cc > 1) {
		pr_warn("Querying a DCSS type failed with rc=%ld\n", vmrc);
		rc = dcss_diag_translate_rc (vmrc);
		goto out_free;
	}

	if (qout->segcnt > 6) {
		rc = -EOPNOTSUPP;
		goto out_free;
	}

	if (qout->segcnt == 1) {
		seg->vm_segtype = qout->range[0].start & 0xff;
	} else {
		/* multi-part segment. only one type supported here:
		    - all parts are contiguous
		    - all parts are either EW or EN type
		    - maximum 6 parts allowed */
		unsigned long start = qout->segstart >> PAGE_SHIFT;
		for (i=0; i<qout->segcnt; i++) {
			if (((qout->range[i].start & 0xff) != SEG_TYPE_EW) &&
			    ((qout->range[i].start & 0xff) != SEG_TYPE_EN)) {
				rc = -EOPNOTSUPP;
				goto out_free;
			}
			if (start != qout->range[i].start >> PAGE_SHIFT) {
				rc = -EOPNOTSUPP;
				goto out_free;
			}
			start = (qout->range[i].end >> PAGE_SHIFT) + 1;
		}
		seg->vm_segtype = SEG_TYPE_EWEN;
	}

	/* analyze diag output and update seg */
	seg->start_addr = qout->segstart;
	seg->end = qout->segend;

	memcpy (seg->range, qout->range, 6*sizeof(struct qrange));
	seg->segcnt = qout->segcnt;

	rc = 0;

 out_free:
	kfree(qin);
	kfree(qout);
	return rc;
}

/*
 * get info about a segment
 * possible return values:
 * -ENOSYS  : we are not running on VM
 * -EIO     : could not perform query diagnose
 * -ENOENT  : no such segment
 * -EOPNOTSUPP: multi-part segment cannot be used with linux
 * -ENOMEM  : out of memory
 * 0 .. 6   : type of segment as defined in include/asm-s390/extmem.h
 */
int
segment_type (char* name)
{
	int rc;
	struct dcss_segment seg;

	if (!MACHINE_IS_VM)
		return -ENOSYS;

	dcss_mkname(name, seg.dcss_name);
	rc = query_segment_type (&seg);
	if (rc < 0)
		return rc;
	return seg.vm_segtype;
}

/*
 * check if segment collides with other segments that are currently loaded
 * returns 1 if this is the case, 0 if no collision was found
 */
static int
segment_overlaps_others (struct dcss_segment *seg)
{
	struct list_head *l;
	struct dcss_segment *tmp;

	BUG_ON(!mutex_is_locked(&dcss_lock));
	list_for_each(l, &dcss_list) {
		tmp = list_entry(l, struct dcss_segment, list);
		if ((tmp->start_addr >> 20) > (seg->end >> 20))
			continue;
		if ((tmp->end >> 20) < (seg->start_addr >> 20))
			continue;
		if (seg == tmp)
			continue;
		return 1;
	}
	return 0;
}

/*
 * real segment loading function, called from segment_load
 * Must return either an error code < 0, or the segment type code >= 0
 */
static int
__segment_load (char *name, int do_nonshared, unsigned long *addr, unsigned long *end)
{
	unsigned long start_addr, end_addr, dummy;
	struct dcss_segment *seg;
	int rc, diag_cc, segtype;

	start_addr = end_addr = 0;
	segtype = -1;
	seg = kmalloc(sizeof(*seg), GFP_KERNEL | GFP_DMA);
	if (seg == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	dcss_mkname (name, seg->dcss_name);
	rc = query_segment_type (seg);
	if (rc < 0)
		goto out_free;

	if (segment_overlaps_others(seg)) {
		rc = -EBUSY;
		goto out_free;
	}

	seg->res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	if (seg->res == NULL) {
		rc = -ENOMEM;
		goto out_free;
	}
	seg->res->flags = IORESOURCE_BUSY | IORESOURCE_MEM;
	seg->res->start = seg->start_addr;
	seg->res->end = seg->end;
	memcpy(&seg->res_name, seg->dcss_name, 8);
	EBCASC(seg->res_name, 8);
	seg->res_name[8] = '\0';
	strlcat(seg->res_name, " (DCSS)", sizeof(seg->res_name));
	seg->res->name = seg->res_name;
	segtype = seg->vm_segtype;
	if (segtype == SEG_TYPE_SC ||
	    ((segtype == SEG_TYPE_SR || segtype == SEG_TYPE_ER) && !do_nonshared))
		seg->res->flags |= IORESOURCE_READONLY;

	/* Check for overlapping resources before adding the mapping. */
	if (request_resource(&iomem_resource, seg->res)) {
		rc = -EBUSY;
		goto out_free_resource;
	}

	rc = vmem_add_mapping(seg->start_addr, seg->end - seg->start_addr + 1);
	if (rc)
		goto out_resource;

	if (do_nonshared)
		diag_cc = dcss_diag(&loadnsr_scode, seg->dcss_name,
				&start_addr, &end_addr);
	else
		diag_cc = dcss_diag(&loadshr_scode, seg->dcss_name,
				&start_addr, &end_addr);
	if (diag_cc < 0) {
		dcss_diag(&purgeseg_scode, seg->dcss_name,
				&dummy, &dummy);
		rc = diag_cc;
		goto out_mapping;
	}
	if (diag_cc > 1) {
		pr_warn("Loading DCSS %s failed with rc=%ld\n", name, end_addr);
		rc = dcss_diag_translate_rc(end_addr);
		dcss_diag(&purgeseg_scode, seg->dcss_name,
				&dummy, &dummy);
		goto out_mapping;
	}
	seg->start_addr = start_addr;
	seg->end = end_addr;
	seg->do_nonshared = do_nonshared;
	refcount_set(&seg->ref_count, 1);
	list_add(&seg->list, &dcss_list);
	*addr = seg->start_addr;
	*end  = seg->end;
	if (do_nonshared)
		pr_info("DCSS %s of range %px to %px and type %s loaded as "
			"exclusive-writable\n", name, (void*) seg->start_addr,
			(void*) seg->end, segtype_string[seg->vm_segtype]);
	else {
		pr_info("DCSS %s of range %px to %px and type %s loaded in "
			"shared access mode\n", name, (void*) seg->start_addr,
			(void*) seg->end, segtype_string[seg->vm_segtype]);
	}
	goto out;
 out_mapping:
	vmem_remove_mapping(seg->start_addr, seg->end - seg->start_addr + 1);
 out_resource:
	release_resource(seg->res);
 out_free_resource:
	kfree(seg->res);
 out_free:
	kfree(seg);
 out:
	return rc < 0 ? rc : segtype;
}

/*
 * this function loads a DCSS segment
 * name         : name of the DCSS
 * do_nonshared : 0 indicates that the dcss should be shared with other linux images
 *                1 indicates that the dcss should be exclusive for this linux image
 * addr         : will be filled with start address of the segment
 * end          : will be filled with end address of the segment
 * return values:
 * -ENOSYS  : we are not running on VM
 * -EIO     : could not perform query or load diagnose
 * -ENOENT  : no such segment
 * -EOPNOTSUPP: multi-part segment cannot be used with linux
 * -EBUSY   : segment cannot be used (overlaps with dcss or storage)
 * -ERANGE  : segment cannot be used (exceeds kernel mapping range)
 * -EPERM   : segment is currently loaded with incompatible permissions
 * -ENOMEM  : out of memory
 * 0 .. 6   : type of segment as defined in include/asm-s390/extmem.h
 */
int
segment_load (char *name, int do_nonshared, unsigned long *addr,
		unsigned long *end)
{
	struct dcss_segment *seg;
	int rc;

	if (!MACHINE_IS_VM)
		return -ENOSYS;

	mutex_lock(&dcss_lock);
	seg = segment_by_name (name);
	if (seg == NULL)
		rc = __segment_load (name, do_nonshared, addr, end);
	else {
		if (do_nonshared == seg->do_nonshared) {
			refcount_inc(&seg->ref_count);
			*addr = seg->start_addr;
			*end  = seg->end;
			rc    = seg->vm_segtype;
		} else {
			*addr = *end = 0;
			rc    = -EPERM;
		}
	}
	mutex_unlock(&dcss_lock);
	return rc;
}

/*
 * this function modifies the shared state of a DCSS segment. note that
 * name         : name of the DCSS
 * do_nonshared : 0 indicates that the dcss should be shared with other linux images
 *                1 indicates that the dcss should be exclusive for this linux image
 * return values:
 * -EIO     : could not perform load diagnose (segment gone!)
 * -ENOENT  : no such segment (segment gone!)
 * -EAGAIN  : segment is in use by other exploiters, try later
 * -EINVAL  : no segment with the given name is currently loaded - name invalid
 * -EBUSY   : segment can temporarily not be used (overlaps with dcss)
 * 0	    : operation succeeded
 */
int
segment_modify_shared (char *name, int do_nonshared)
{
	struct dcss_segment *seg;
	unsigned long start_addr, end_addr, dummy;
	int rc, diag_cc;

	start_addr = end_addr = 0;
	mutex_lock(&dcss_lock);
	seg = segment_by_name (name);
	if (seg == NULL) {
		rc = -EINVAL;
		goto out_unlock;
	}
	if (do_nonshared == seg->do_nonshared) {
		pr_info("DCSS %s is already in the requested access "
			"mode\n", name);
		rc = 0;
		goto out_unlock;
	}
	if (refcount_read(&seg->ref_count) != 1) {
		pr_warn("DCSS %s is in use and cannot be reloaded\n", name);
		rc = -EAGAIN;
		goto out_unlock;
	}
	release_resource(seg->res);
	if (do_nonshared)
		seg->res->flags &= ~IORESOURCE_READONLY;
	else
		if (seg->vm_segtype == SEG_TYPE_SR ||
		    seg->vm_segtype == SEG_TYPE_ER)
			seg->res->flags |= IORESOURCE_READONLY;

	if (request_resource(&iomem_resource, seg->res)) {
		pr_warn("DCSS %s overlaps with used memory resources and cannot be reloaded\n",
			name);
		rc = -EBUSY;
		kfree(seg->res);
		goto out_del_mem;
	}

	dcss_diag(&purgeseg_scode, seg->dcss_name, &dummy, &dummy);
	if (do_nonshared)
		diag_cc = dcss_diag(&loadnsr_scode, seg->dcss_name,
				&start_addr, &end_addr);
	else
		diag_cc = dcss_diag(&loadshr_scode, seg->dcss_name,
				&start_addr, &end_addr);
	if (diag_cc < 0) {
		rc = diag_cc;
		goto out_del_res;
	}
	if (diag_cc > 1) {
		pr_warn("Reloading DCSS %s failed with rc=%ld\n",
			name, end_addr);
		rc = dcss_diag_translate_rc(end_addr);
		goto out_del_res;
	}
	seg->start_addr = start_addr;
	seg->end = end_addr;
	seg->do_nonshared = do_nonshared;
	rc = 0;
	goto out_unlock;
 out_del_res:
	release_resource(seg->res);
	kfree(seg->res);
 out_del_mem:
	vmem_remove_mapping(seg->start_addr, seg->end - seg->start_addr + 1);
	list_del(&seg->list);
	dcss_diag(&purgeseg_scode, seg->dcss_name, &dummy, &dummy);
	kfree(seg);
 out_unlock:
	mutex_unlock(&dcss_lock);
	return rc;
}

/*
 * Decrease the use count of a DCSS segment and remove
 * it from the address space if nobody is using it
 * any longer.
 */
void
segment_unload(char *name)
{
	unsigned long dummy;
	struct dcss_segment *seg;

	if (!MACHINE_IS_VM)
		return;

	mutex_lock(&dcss_lock);
	seg = segment_by_name (name);
	if (seg == NULL) {
		pr_err("Unloading unknown DCSS %s failed\n", name);
		goto out_unlock;
	}
	if (!refcount_dec_and_test(&seg->ref_count))
		goto out_unlock;
	release_resource(seg->res);
	kfree(seg->res);
	vmem_remove_mapping(seg->start_addr, seg->end - seg->start_addr + 1);
	list_del(&seg->list);
	dcss_diag(&purgeseg_scode, seg->dcss_name, &dummy, &dummy);
	kfree(seg);
out_unlock:
	mutex_unlock(&dcss_lock);
}

/*
 * save segment content permanently
 */
void
segment_save(char *name)
{
	struct dcss_segment *seg;
	char cmd1[160];
	char cmd2[80];
	int i, response;

	if (!MACHINE_IS_VM)
		return;

	mutex_lock(&dcss_lock);
	seg = segment_by_name (name);

	if (seg == NULL) {
		pr_err("Saving unknown DCSS %s failed\n", name);
		goto out;
	}

	sprintf(cmd1, "DEFSEG %s", name);
	for (i=0; i<seg->segcnt; i++) {
		sprintf(cmd1+strlen(cmd1), " %lX-%lX %s",
			seg->range[i].start >> PAGE_SHIFT,
			seg->range[i].end >> PAGE_SHIFT,
			segtype_string[seg->range[i].start & 0xff]);
	}
	sprintf(cmd2, "SAVESEG %s", name);
	response = 0;
	cpcmd(cmd1, NULL, 0, &response);
	if (response) {
		pr_err("Saving a DCSS failed with DEFSEG response code "
		       "%i\n", response);
		goto out;
	}
	cpcmd(cmd2, NULL, 0, &response);
	if (response) {
		pr_err("Saving a DCSS failed with SAVESEG response code "
		       "%i\n", response);
		goto out;
	}
out:
	mutex_unlock(&dcss_lock);
}

/*
 * print appropriate error message for segment_load()/segment_type()
 * return code
 */
void segment_warning(int rc, char *seg_name)
{
	switch (rc) {
	case -ENOENT:
		pr_err("DCSS %s cannot be loaded or queried\n", seg_name);
		break;
	case -ENOSYS:
		pr_err("DCSS %s cannot be loaded or queried without "
		       "z/VM\n", seg_name);
		break;
	case -EIO:
		pr_err("Loading or querying DCSS %s resulted in a "
		       "hardware error\n", seg_name);
		break;
	case -EOPNOTSUPP:
		pr_err("DCSS %s has multiple page ranges and cannot be "
		       "loaded or queried\n", seg_name);
		break;
	case -EBUSY:
		pr_err("%s needs used memory resources and cannot be "
		       "loaded or queried\n", seg_name);
		break;
	case -EPERM:
		pr_err("DCSS %s is already loaded in a different access "
		       "mode\n", seg_name);
		break;
	case -ENOMEM:
		pr_err("There is not enough memory to load or query "
		       "DCSS %s\n", seg_name);
		break;
	case -ERANGE:
		pr_err("DCSS %s exceeds the kernel mapping range (%lu) "
		       "and cannot be loaded\n", seg_name, VMEM_MAX_PHYS);
		break;
	default:
		break;
	}
}

EXPORT_SYMBOL(segment_load);
EXPORT_SYMBOL(segment_unload);
EXPORT_SYMBOL(segment_save);
EXPORT_SYMBOL(segment_type);
EXPORT_SYMBOL(segment_modify_shared);
EXPORT_SYMBOL(segment_warning);
