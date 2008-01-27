/*
 * File...........: arch/s390/mm/extmem.c
 * Author(s)......: Carsten Otte <cotte@de.ibm.com>
 * 		    Rob M van der Heij <rvdheij@nl.ibm.com>
 * 		    Steven Shultz <shultzss@us.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation 2002-2004
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bootmem.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/ebcdic.h>
#include <asm/errno.h>
#include <asm/extmem.h>
#include <asm/cpcmd.h>
#include <asm/setup.h>

#define DCSS_DEBUG	/* Debug messages on/off */

#define DCSS_NAME "extmem"
#ifdef DCSS_DEBUG
#define PRINT_DEBUG(x...)	printk(KERN_DEBUG DCSS_NAME " debug:" x)
#else
#define PRINT_DEBUG(x...)   do {} while (0)
#endif
#define PRINT_INFO(x...)	printk(KERN_INFO DCSS_NAME " info:" x)
#define PRINT_WARN(x...)	printk(KERN_WARNING DCSS_NAME " warning:" x)
#define PRINT_ERR(x...)		printk(KERN_ERR DCSS_NAME " error:" x)


#define DCSS_LOADSHR    0x00
#define DCSS_LOADNSR    0x04
#define DCSS_PURGESEG   0x08
#define DCSS_FINDSEG    0x0c
#define DCSS_LOADNOLY   0x10
#define DCSS_SEGEXT     0x18
#define DCSS_FINDSEGA   0x0c

struct qrange {
	unsigned int  start; // 3byte start address, 1 byte type
	unsigned int  end;   // 3byte end address, 1 byte reserved
};

struct qout64 {
	int segstart;
	int segend;
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
	char res_name[15];
	unsigned long start_addr;
	unsigned long end;
	atomic_t ref_count;
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
	};
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
dcss_diag (__u8 func, void *parameter,
           unsigned long *ret1, unsigned long *ret2)
{
	unsigned long rx, ry;
	int rc;

	rx = (unsigned long) parameter;
	ry = (unsigned long) func;
	asm volatile(
#ifdef CONFIG_64BIT
		"	sam31\n"
		"	diag	%0,%1,0x64\n"
		"	sam64\n"
#else
		"	diag	%0,%1,0x64\n"
#endif
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
	struct qin64  *qin = kmalloc (sizeof(struct qin64), GFP_DMA);
	struct qout64 *qout = kmalloc (sizeof(struct qout64), GFP_DMA);

	int diag_cc, rc, i;
	unsigned long dummy, vmrc;

	if ((qin == NULL) || (qout == NULL)) {
		rc = -ENOMEM;
		goto out_free;
	}

	/* initialize diag input parameters */
	qin->qopcode = DCSS_FINDSEGA;
	qin->qoutptr = (unsigned long) qout;
	qin->qoutlen = sizeof(struct qout64);
	memcpy (qin->qname, seg->dcss_name, 8);

	diag_cc = dcss_diag (DCSS_SEGEXT, qin, &dummy, &vmrc);

	if (diag_cc > 1) {
		PRINT_WARN ("segment_type: diag returned error %ld\n", vmrc);
		rc = dcss_diag_translate_rc (vmrc);
		goto out_free;
	}

	if (qout->segcnt > 6) {
		rc = -ENOTSUPP;
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
				rc = -ENOTSUPP;
				goto out_free;
			}
			if (start != qout->range[i].start >> PAGE_SHIFT) {
				rc = -ENOTSUPP;
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
 * -ENOTSUPP: multi-part segment cannot be used with linux
 * -ENOSPC  : segment cannot be used (overlaps with storage)
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
 * real segment loading function, called from segment_load
 */
static int
__segment_load (char *name, int do_nonshared, unsigned long *addr, unsigned long *end)
{
	struct dcss_segment *seg = kmalloc(sizeof(struct dcss_segment),
			GFP_DMA);
	int dcss_command, rc, diag_cc;

	if (seg == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	dcss_mkname (name, seg->dcss_name);
	rc = query_segment_type (seg);
	if (rc < 0)
		goto out_free;

	rc = add_shared_memory(seg->start_addr, seg->end - seg->start_addr + 1);

	switch (rc) {
	case 0:
		break;
	case -ENOSPC:
		PRINT_WARN("segment_load: not loading segment %s - overlaps "
			   "storage/segment\n", name);
		goto out_free;
	case -ERANGE:
		PRINT_WARN("segment_load: not loading segment %s - exceeds "
			   "kernel mapping range\n", name);
		goto out_free;
	default:
		PRINT_WARN("segment_load: not loading segment %s (rc: %d)\n",
			   name, rc);
		goto out_free;
	}

	seg->res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	if (seg->res == NULL) {
		rc = -ENOMEM;
		goto out_shared;
	}
	seg->res->flags = IORESOURCE_BUSY | IORESOURCE_MEM;
	seg->res->start = seg->start_addr;
	seg->res->end = seg->end;
	memcpy(&seg->res_name, seg->dcss_name, 8);
	EBCASC(seg->res_name, 8);
	seg->res_name[8] = '\0';
	strncat(seg->res_name, " (DCSS)", 7);
	seg->res->name = seg->res_name;
	rc = seg->vm_segtype;
	if (rc == SEG_TYPE_SC ||
	    ((rc == SEG_TYPE_SR || rc == SEG_TYPE_ER) && !do_nonshared))
		seg->res->flags |= IORESOURCE_READONLY;
	if (request_resource(&iomem_resource, seg->res)) {
		rc = -EBUSY;
		kfree(seg->res);
		goto out_shared;
	}

	if (do_nonshared)
		dcss_command = DCSS_LOADNSR;
	else
		dcss_command = DCSS_LOADNOLY;

	diag_cc = dcss_diag(dcss_command, seg->dcss_name,
			&seg->start_addr, &seg->end);
	if (diag_cc > 1) {
		PRINT_WARN ("segment_load: could not load segment %s - "
				"diag returned error (%ld)\n",name,seg->end);
		rc = dcss_diag_translate_rc (seg->end);
		dcss_diag(DCSS_PURGESEG, seg->dcss_name,
				&seg->start_addr, &seg->end);
		goto out_resource;
	}
	seg->do_nonshared = do_nonshared;
	atomic_set(&seg->ref_count, 1);
	list_add(&seg->list, &dcss_list);
	*addr = seg->start_addr;
	*end  = seg->end;
	if (do_nonshared)
		PRINT_INFO ("segment_load: loaded segment %s range %p .. %p "
				"type %s in non-shared mode\n", name,
				(void*)seg->start_addr, (void*)seg->end,
				segtype_string[seg->vm_segtype]);
	else {
		PRINT_INFO ("segment_load: loaded segment %s range %p .. %p "
				"type %s in shared mode\n", name,
				(void*)seg->start_addr, (void*)seg->end,
				segtype_string[seg->vm_segtype]);
	}
	goto out;
 out_resource:
	release_resource(seg->res);
	kfree(seg->res);
 out_shared:
	remove_shared_memory(seg->start_addr, seg->end - seg->start_addr + 1);
 out_free:
	kfree(seg);
 out:
	return rc;
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
 * -ENOTSUPP: multi-part segment cannot be used with linux
 * -ENOSPC  : segment cannot be used (overlaps with storage)
 * -EBUSY   : segment can temporarily not be used (overlaps with dcss)
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
			atomic_inc(&seg->ref_count);
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
	unsigned long dummy;
	int dcss_command, rc, diag_cc;

	mutex_lock(&dcss_lock);
	seg = segment_by_name (name);
	if (seg == NULL) {
		rc = -EINVAL;
		goto out_unlock;
	}
	if (do_nonshared == seg->do_nonshared) {
		PRINT_INFO ("segment_modify_shared: not reloading segment %s"
				" - already in requested mode\n",name);
		rc = 0;
		goto out_unlock;
	}
	if (atomic_read (&seg->ref_count) != 1) {
		PRINT_WARN ("segment_modify_shared: not reloading segment %s - "
				"segment is in use by other driver(s)\n",name);
		rc = -EAGAIN;
		goto out_unlock;
	}
	release_resource(seg->res);
	if (do_nonshared) {
		dcss_command = DCSS_LOADNSR;
		seg->res->flags &= ~IORESOURCE_READONLY;
	} else {
		dcss_command = DCSS_LOADNOLY;
		if (seg->vm_segtype == SEG_TYPE_SR ||
		    seg->vm_segtype == SEG_TYPE_ER)
			seg->res->flags |= IORESOURCE_READONLY;
	}
	if (request_resource(&iomem_resource, seg->res)) {
		PRINT_WARN("segment_modify_shared: could not reload segment %s"
			   " - overlapping resources\n", name);
		rc = -EBUSY;
		kfree(seg->res);
		goto out_del;
	}
	dcss_diag(DCSS_PURGESEG, seg->dcss_name, &dummy, &dummy);
	diag_cc = dcss_diag(dcss_command, seg->dcss_name,
			&seg->start_addr, &seg->end);
	if (diag_cc > 1) {
		PRINT_WARN ("segment_modify_shared: could not reload segment %s"
				" - diag returned error (%ld)\n",name,seg->end);
		rc = dcss_diag_translate_rc (seg->end);
		goto out_del;
	}
	seg->do_nonshared = do_nonshared;
	rc = 0;
	goto out_unlock;
 out_del:
	remove_shared_memory(seg->start_addr, seg->end - seg->start_addr + 1);
	list_del(&seg->list);
	dcss_diag(DCSS_PURGESEG, seg->dcss_name, &dummy, &dummy);
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
		PRINT_ERR ("could not find segment %s in segment_unload, "
				"please report to linux390@de.ibm.com\n",name);
		goto out_unlock;
	}
	if (atomic_dec_return(&seg->ref_count) != 0)
		goto out_unlock;
	release_resource(seg->res);
	kfree(seg->res);
	remove_shared_memory(seg->start_addr, seg->end - seg->start_addr + 1);
	list_del(&seg->list);
	dcss_diag(DCSS_PURGESEG, seg->dcss_name, &dummy, &dummy);
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
	int startpfn = 0;
	int endpfn = 0;
	char cmd1[160];
	char cmd2[80];
	int i, response;

	if (!MACHINE_IS_VM)
		return;

	mutex_lock(&dcss_lock);
	seg = segment_by_name (name);

	if (seg == NULL) {
		PRINT_ERR("could not find segment %s in segment_save, please "
			  "report to linux390@de.ibm.com\n", name);
		goto out;
	}

	startpfn = seg->start_addr >> PAGE_SHIFT;
	endpfn = (seg->end) >> PAGE_SHIFT;
	sprintf(cmd1, "DEFSEG %s", name);
	for (i=0; i<seg->segcnt; i++) {
		sprintf(cmd1+strlen(cmd1), " %X-%X %s",
			seg->range[i].start >> PAGE_SHIFT,
			seg->range[i].end >> PAGE_SHIFT,
			segtype_string[seg->range[i].start & 0xff]);
	}
	sprintf(cmd2, "SAVESEG %s", name);
	response = 0;
	cpcmd(cmd1, NULL, 0, &response);
	if (response) {
		PRINT_ERR("segment_save: DEFSEG failed with response code %i\n",
			  response);
		goto out;
	}
	cpcmd(cmd2, NULL, 0, &response);
	if (response) {
		PRINT_ERR("segment_save: SAVESEG failed with response code %i\n",
			  response);
		goto out;
	}
out:
	mutex_unlock(&dcss_lock);
}

EXPORT_SYMBOL(segment_load);
EXPORT_SYMBOL(segment_unload);
EXPORT_SYMBOL(segment_save);
EXPORT_SYMBOL(segment_type);
EXPORT_SYMBOL(segment_modify_shared);
