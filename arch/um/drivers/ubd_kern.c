/*
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

/* 2001-09-28...2002-04-17
 * Partition stuff by James_McMechan@hotmail.com
 * old style ubd by setting UBD_SHIFT to 0
 * 2002-09-27...2002-10-18 massive tinkering for 2.5
 * partitions have changed in 2.5
 * 2003-01-29 more tinkering for 2.5.59-1
 * This should now address the sysfs problems and has
 * the symlink for devfs to allow for booting with
 * the common /dev/ubd/discX/... names rather than
 * only /dev/ubdN/discN this version also has lots of
 * clean ups preparing for ubd-many.
 * James McMechan
 */

#define MAJOR_NR UBD_MAJOR
#define UBD_SHIFT 4

#include "linux/kernel.h"
#include "linux/module.h"
#include "linux/blkdev.h"
#include "linux/hdreg.h"
#include "linux/init.h"
#include "linux/cdrom.h"
#include "linux/proc_fs.h"
#include "linux/ctype.h"
#include "linux/capability.h"
#include "linux/mm.h"
#include "linux/vmalloc.h"
#include "linux/blkpg.h"
#include "linux/genhd.h"
#include "linux/spinlock.h"
#include "linux/platform_device.h"
#include "linux/scatterlist.h"
#include "asm/segment.h"
#include "asm/uaccess.h"
#include "asm/irq.h"
#include "asm/types.h"
#include "asm/tlbflush.h"
#include "mem_user.h"
#include "kern_util.h"
#include "kern.h"
#include "mconsole_kern.h"
#include "init.h"
#include "irq_user.h"
#include "irq_kern.h"
#include "ubd_user.h"
#include "os.h"
#include "mem.h"
#include "mem_kern.h"
#include "cow.h"

enum ubd_req { UBD_READ, UBD_WRITE };

struct io_thread_req {
	struct request *req;
	enum ubd_req op;
	int fds[2];
	unsigned long offsets[2];
	unsigned long long offset;
	unsigned long length;
	char *buffer;
	int sectorsize;
	unsigned long sector_mask;
	unsigned long long cow_offset;
	unsigned long bitmap_words[2];
	int error;
};

extern int open_ubd_file(char *file, struct openflags *openflags, int shared,
			 char **backing_file_out, int *bitmap_offset_out,
			 unsigned long *bitmap_len_out, int *data_offset_out,
			 int *create_cow_out);
extern int create_cow_file(char *cow_file, char *backing_file,
			   struct openflags flags, int sectorsize,
			   int alignment, int *bitmap_offset_out,
			   unsigned long *bitmap_len_out,
			   int *data_offset_out);
extern int read_cow_bitmap(int fd, void *buf, int offset, int len);
extern void do_io(struct io_thread_req *req);

static inline int ubd_test_bit(__u64 bit, unsigned char *data)
{
	__u64 n;
	int bits, off;

	bits = sizeof(data[0]) * 8;
	n = bit / bits;
	off = bit % bits;
	return (data[n] & (1 << off)) != 0;
}

static inline void ubd_set_bit(__u64 bit, unsigned char *data)
{
	__u64 n;
	int bits, off;

	bits = sizeof(data[0]) * 8;
	n = bit / bits;
	off = bit % bits;
	data[n] |= (1 << off);
}
/*End stuff from ubd_user.h*/

#define DRIVER_NAME "uml-blkdev"

static DEFINE_MUTEX(ubd_lock);

static int ubd_open(struct inode * inode, struct file * filp);
static int ubd_release(struct inode * inode, struct file * file);
static int ubd_ioctl(struct inode * inode, struct file * file,
		     unsigned int cmd, unsigned long arg);
static int ubd_getgeo(struct block_device *bdev, struct hd_geometry *geo);

#define MAX_DEV (16)

static struct block_device_operations ubd_blops = {
        .owner		= THIS_MODULE,
        .open		= ubd_open,
        .release	= ubd_release,
        .ioctl		= ubd_ioctl,
	.getgeo		= ubd_getgeo,
};

/* Protected by ubd_lock */
static int fake_major = MAJOR_NR;
static struct gendisk *ubd_gendisk[MAX_DEV];
static struct gendisk *fake_gendisk[MAX_DEV];

#ifdef CONFIG_BLK_DEV_UBD_SYNC
#define OPEN_FLAGS ((struct openflags) { .r = 1, .w = 1, .s = 1, .c = 0, \
					 .cl = 1 })
#else
#define OPEN_FLAGS ((struct openflags) { .r = 1, .w = 1, .s = 0, .c = 0, \
					 .cl = 1 })
#endif
static struct openflags global_openflags = OPEN_FLAGS;

struct cow {
	/* backing file name */
	char *file;
	/* backing file fd */
	int fd;
	unsigned long *bitmap;
	unsigned long bitmap_len;
	int bitmap_offset;
	int data_offset;
};

#define MAX_SG 64

struct ubd {
	struct list_head restart;
	/* name (and fd, below) of the file opened for writing, either the
	 * backing or the cow file. */
	char *file;
	int count;
	int fd;
	__u64 size;
	struct openflags boot_openflags;
	struct openflags openflags;
	unsigned shared:1;
	unsigned no_cow:1;
	struct cow cow;
	struct platform_device pdev;
	struct request_queue *queue;
	spinlock_t lock;
	struct scatterlist sg[MAX_SG];
	struct request *request;
	int start_sg, end_sg;
};

#define DEFAULT_COW { \
	.file =			NULL, \
	.fd =			-1,	\
	.bitmap =		NULL, \
	.bitmap_offset =	0, \
	.data_offset =		0, \
}

#define DEFAULT_UBD { \
	.file = 		NULL, \
	.count =		0, \
	.fd =			-1, \
	.size =			-1, \
	.boot_openflags =	OPEN_FLAGS, \
	.openflags =		OPEN_FLAGS, \
	.no_cow =               0, \
	.shared =		0, \
	.cow =			DEFAULT_COW, \
	.lock =			SPIN_LOCK_UNLOCKED,	\
	.request =		NULL, \
	.start_sg =		0, \
	.end_sg =		0, \
}

/* Protected by ubd_lock */
struct ubd ubd_devs[MAX_DEV] = { [ 0 ... MAX_DEV - 1 ] = DEFAULT_UBD };

/* Only changed by fake_ide_setup which is a setup */
static int fake_ide = 0;
static struct proc_dir_entry *proc_ide_root = NULL;
static struct proc_dir_entry *proc_ide = NULL;

static void make_proc_ide(void)
{
	proc_ide_root = proc_mkdir("ide", NULL);
	proc_ide = proc_mkdir("ide0", proc_ide_root);
}

static int proc_ide_read_media(char *page, char **start, off_t off, int count,
			       int *eof, void *data)
{
	int len;

	strcpy(page, "disk\n");
	len = strlen("disk\n");
	len -= off;
	if (len < count){
		*eof = 1;
		if (len <= 0) return 0;
	}
	else len = count;
	*start = page + off;
	return len;
}

static void make_ide_entries(const char *dev_name)
{
	struct proc_dir_entry *dir, *ent;
	char name[64];

	if(proc_ide_root == NULL) make_proc_ide();

	dir = proc_mkdir(dev_name, proc_ide);
	if(!dir) return;

	ent = create_proc_entry("media", S_IFREG|S_IRUGO, dir);
	if(!ent) return;
	ent->data = NULL;
	ent->read_proc = proc_ide_read_media;
	ent->write_proc = NULL;
	snprintf(name, sizeof(name), "ide0/%s", dev_name);
	proc_symlink(dev_name, proc_ide_root, name);
}

static int fake_ide_setup(char *str)
{
	fake_ide = 1;
	return 1;
}

__setup("fake_ide", fake_ide_setup);

__uml_help(fake_ide_setup,
"fake_ide\n"
"    Create ide0 entries that map onto ubd devices.\n\n"
);

static int parse_unit(char **ptr)
{
	char *str = *ptr, *end;
	int n = -1;

	if(isdigit(*str)) {
		n = simple_strtoul(str, &end, 0);
		if(end == str)
			return -1;
		*ptr = end;
	}
	else if (('a' <= *str) && (*str <= 'z')) {
		n = *str - 'a';
		str++;
		*ptr = str;
	}
	return n;
}

/* If *index_out == -1 at exit, the passed option was a general one;
 * otherwise, the str pointer is used (and owned) inside ubd_devs array, so it
 * should not be freed on exit.
 */
static int ubd_setup_common(char *str, int *index_out, char **error_out)
{
	struct ubd *ubd_dev;
	struct openflags flags = global_openflags;
	char *backing_file;
	int n, err = 0, i;

	if(index_out) *index_out = -1;
	n = *str;
	if(n == '='){
		char *end;
		int major;

		str++;
		if(!strcmp(str, "sync")){
			global_openflags = of_sync(global_openflags);
			goto out1;
		}

		err = -EINVAL;
		major = simple_strtoul(str, &end, 0);
		if((*end != '\0') || (end == str)){
			*error_out = "Didn't parse major number";
			goto out1;
		}

		mutex_lock(&ubd_lock);
		if(fake_major != MAJOR_NR){
			*error_out = "Can't assign a fake major twice";
			goto out1;
		}

		fake_major = major;

		printk(KERN_INFO "Setting extra ubd major number to %d\n",
		       major);
		err = 0;
	out1:
		mutex_unlock(&ubd_lock);
		return err;
	}

	n = parse_unit(&str);
	if(n < 0){
		*error_out = "Couldn't parse device number";
		return -EINVAL;
	}
	if(n >= MAX_DEV){
		*error_out = "Device number out of range";
		return 1;
	}

	err = -EBUSY;
	mutex_lock(&ubd_lock);

	ubd_dev = &ubd_devs[n];
	if(ubd_dev->file != NULL){
		*error_out = "Device is already configured";
		goto out;
	}

	if (index_out)
		*index_out = n;

	err = -EINVAL;
	for (i = 0; i < sizeof("rscd="); i++) {
		switch (*str) {
		case 'r':
			flags.w = 0;
			break;
		case 's':
			flags.s = 1;
			break;
		case 'd':
			ubd_dev->no_cow = 1;
			break;
		case 'c':
			ubd_dev->shared = 1;
			break;
		case '=':
			str++;
			goto break_loop;
		default:
			*error_out = "Expected '=' or flag letter "
				"(r, s, c, or d)";
			goto out;
		}
		str++;
	}

	if (*str == '=')
		*error_out = "Too many flags specified";
	else
		*error_out = "Missing '='";
	goto out;

break_loop:
	backing_file = strchr(str, ',');

	if (backing_file == NULL)
		backing_file = strchr(str, ':');

	if(backing_file != NULL){
		if(ubd_dev->no_cow){
			*error_out = "Can't specify both 'd' and a cow file";
			goto out;
		}
		else {
			*backing_file = '\0';
			backing_file++;
		}
	}
	err = 0;
	ubd_dev->file = str;
	ubd_dev->cow.file = backing_file;
	ubd_dev->boot_openflags = flags;
out:
	mutex_unlock(&ubd_lock);
	return err;
}

static int ubd_setup(char *str)
{
	char *error;
	int err;

	err = ubd_setup_common(str, NULL, &error);
	if(err)
		printk(KERN_ERR "Failed to initialize device with \"%s\" : "
		       "%s\n", str, error);
	return 1;
}

__setup("ubd", ubd_setup);
__uml_help(ubd_setup,
"ubd<n><flags>=<filename>[(:|,)<filename2>]\n"
"    This is used to associate a device with a file in the underlying\n"
"    filesystem. When specifying two filenames, the first one is the\n"
"    COW name and the second is the backing file name. As separator you can\n"
"    use either a ':' or a ',': the first one allows writing things like;\n"
"	ubd0=~/Uml/root_cow:~/Uml/root_backing_file\n"
"    while with a ',' the shell would not expand the 2nd '~'.\n"
"    When using only one filename, UML will detect whether to treat it like\n"
"    a COW file or a backing file. To override this detection, add the 'd'\n"
"    flag:\n"
"	ubd0d=BackingFile\n"
"    Usually, there is a filesystem in the file, but \n"
"    that's not required. Swap devices containing swap files can be\n"
"    specified like this. Also, a file which doesn't contain a\n"
"    filesystem can have its contents read in the virtual \n"
"    machine by running 'dd' on the device. <n> must be in the range\n"
"    0 to 7. Appending an 'r' to the number will cause that device\n"
"    to be mounted read-only. For example ubd1r=./ext_fs. Appending\n"
"    an 's' will cause data to be written to disk on the host immediately.\n"
"    'c' will cause the device to be treated as being shared between multiple\n"
"    UMLs and file locking will be turned off - this is appropriate for a\n"
"    cluster filesystem and inappropriate at almost all other times.\n\n"
);

static int udb_setup(const char *str)
{
	printk("udb%s specified on command line is almost certainly a ubd -> "
	       "udb TYPO\n", str);
	return 1;
}

__setup("udb", udb_setup);
__uml_help(udb_setup,
"udb\n"
"    This option is here solely to catch ubd -> udb typos, which can be\n"
"    to impossible to catch visually unless you specifically look for\n"
"    them.  The only result of any option starting with 'udb' is an error\n"
"    in the boot output.\n\n"
);

static int fakehd_set = 0;
static int fakehd(char *str)
{
	printk(KERN_INFO "fakehd : Changing ubd name to \"hd\".\n");
	fakehd_set = 1;
	return 1;
}

__setup("fakehd", fakehd);
__uml_help(fakehd,
"fakehd\n"
"    Change the ubd device name to \"hd\".\n\n"
);

static void do_ubd_request(struct request_queue * q);

/* Only changed by ubd_init, which is an initcall. */
int thread_fd = -1;

static void ubd_end_request(struct request *req, int bytes, int error)
{
	blk_end_request(req, error, bytes);
}

/* Callable only from interrupt context - otherwise you need to do
 * spin_lock_irq()/spin_lock_irqsave() */
static inline void ubd_finish(struct request *req, int bytes)
{
	if(bytes < 0){
		ubd_end_request(req, 0, -EIO);
		return;
	}
	ubd_end_request(req, bytes, 0);
}

static LIST_HEAD(restart);

/* XXX - move this inside ubd_intr. */
/* Called without dev->lock held, and only in interrupt context. */
static void ubd_handler(void)
{
	struct io_thread_req *req;
	struct request *rq;
	struct ubd *ubd;
	struct list_head *list, *next_ele;
	unsigned long flags;
	int n;

	while(1){
		n = os_read_file(thread_fd, &req,
				 sizeof(struct io_thread_req *));
		if(n != sizeof(req)){
			if(n == -EAGAIN)
				break;
			printk(KERN_ERR "spurious interrupt in ubd_handler, "
			       "err = %d\n", -n);
			return;
		}

		rq = req->req;
		rq->nr_sectors -= req->length >> 9;
		if(rq->nr_sectors == 0)
			ubd_finish(rq, rq->hard_nr_sectors << 9);
		kfree(req);
	}
	reactivate_fd(thread_fd, UBD_IRQ);

	list_for_each_safe(list, next_ele, &restart){
		ubd = container_of(list, struct ubd, restart);
		list_del_init(&ubd->restart);
		spin_lock_irqsave(&ubd->lock, flags);
		do_ubd_request(ubd->queue);
		spin_unlock_irqrestore(&ubd->lock, flags);
	}
}

static irqreturn_t ubd_intr(int irq, void *dev)
{
	ubd_handler();
	return IRQ_HANDLED;
}

/* Only changed by ubd_init, which is an initcall. */
static int io_pid = -1;

void kill_io_thread(void)
{
	if(io_pid != -1)
		os_kill_process(io_pid, 1);
}

__uml_exitcall(kill_io_thread);

static inline int ubd_file_size(struct ubd *ubd_dev, __u64 *size_out)
{
	char *file;

	file = ubd_dev->cow.file ? ubd_dev->cow.file : ubd_dev->file;
	return os_file_size(file, size_out);
}

static void ubd_close_dev(struct ubd *ubd_dev)
{
	os_close_file(ubd_dev->fd);
	if(ubd_dev->cow.file == NULL)
		return;

	os_close_file(ubd_dev->cow.fd);
	vfree(ubd_dev->cow.bitmap);
	ubd_dev->cow.bitmap = NULL;
}

static int ubd_open_dev(struct ubd *ubd_dev)
{
	struct openflags flags;
	char **back_ptr;
	int err, create_cow, *create_ptr;
	int fd;

	ubd_dev->openflags = ubd_dev->boot_openflags;
	create_cow = 0;
	create_ptr = (ubd_dev->cow.file != NULL) ? &create_cow : NULL;
	back_ptr = ubd_dev->no_cow ? NULL : &ubd_dev->cow.file;

	fd = open_ubd_file(ubd_dev->file, &ubd_dev->openflags, ubd_dev->shared,
				back_ptr, &ubd_dev->cow.bitmap_offset,
				&ubd_dev->cow.bitmap_len, &ubd_dev->cow.data_offset,
				create_ptr);

	if((fd == -ENOENT) && create_cow){
		fd = create_cow_file(ubd_dev->file, ubd_dev->cow.file,
					  ubd_dev->openflags, 1 << 9, PAGE_SIZE,
					  &ubd_dev->cow.bitmap_offset,
					  &ubd_dev->cow.bitmap_len,
					  &ubd_dev->cow.data_offset);
		if(fd >= 0){
			printk(KERN_INFO "Creating \"%s\" as COW file for "
			       "\"%s\"\n", ubd_dev->file, ubd_dev->cow.file);
		}
	}

	if(fd < 0){
		printk("Failed to open '%s', errno = %d\n", ubd_dev->file,
		       -fd);
		return fd;
	}
	ubd_dev->fd = fd;

	if(ubd_dev->cow.file != NULL){
		blk_queue_max_sectors(ubd_dev->queue, 8 * sizeof(long));

		err = -ENOMEM;
		ubd_dev->cow.bitmap = vmalloc(ubd_dev->cow.bitmap_len);
		if(ubd_dev->cow.bitmap == NULL){
			printk(KERN_ERR "Failed to vmalloc COW bitmap\n");
			goto error;
		}
		flush_tlb_kernel_vm();

		err = read_cow_bitmap(ubd_dev->fd, ubd_dev->cow.bitmap,
				      ubd_dev->cow.bitmap_offset,
				      ubd_dev->cow.bitmap_len);
		if(err < 0)
			goto error;

		flags = ubd_dev->openflags;
		flags.w = 0;
		err = open_ubd_file(ubd_dev->cow.file, &flags, ubd_dev->shared, NULL,
				    NULL, NULL, NULL, NULL);
		if(err < 0) goto error;
		ubd_dev->cow.fd = err;
	}
	return 0;
 error:
	os_close_file(ubd_dev->fd);
	return err;
}

static void ubd_device_release(struct device *dev)
{
	struct ubd *ubd_dev = dev->driver_data;

	blk_cleanup_queue(ubd_dev->queue);
	*ubd_dev = ((struct ubd) DEFAULT_UBD);
}

static int ubd_disk_register(int major, u64 size, int unit,
			     struct gendisk **disk_out)
{
	struct gendisk *disk;

	disk = alloc_disk(1 << UBD_SHIFT);
	if(disk == NULL)
		return -ENOMEM;

	disk->major = major;
	disk->first_minor = unit << UBD_SHIFT;
	disk->fops = &ubd_blops;
	set_capacity(disk, size / 512);
	if(major == MAJOR_NR)
		sprintf(disk->disk_name, "ubd%c", 'a' + unit);
	else
		sprintf(disk->disk_name, "ubd_fake%d", unit);

	/* sysfs register (not for ide fake devices) */
	if (major == MAJOR_NR) {
		ubd_devs[unit].pdev.id   = unit;
		ubd_devs[unit].pdev.name = DRIVER_NAME;
		ubd_devs[unit].pdev.dev.release = ubd_device_release;
		ubd_devs[unit].pdev.dev.driver_data = &ubd_devs[unit];
		platform_device_register(&ubd_devs[unit].pdev);
		disk->driverfs_dev = &ubd_devs[unit].pdev.dev;
	}

	disk->private_data = &ubd_devs[unit];
	disk->queue = ubd_devs[unit].queue;
	add_disk(disk);

	*disk_out = disk;
	return 0;
}

#define ROUND_BLOCK(n) ((n + ((1 << 9) - 1)) & (-1 << 9))

static int ubd_add(int n, char **error_out)
{
	struct ubd *ubd_dev = &ubd_devs[n];
	int err = 0;

	if(ubd_dev->file == NULL)
		goto out;

	err = ubd_file_size(ubd_dev, &ubd_dev->size);
	if(err < 0){
		*error_out = "Couldn't determine size of device's file";
		goto out;
	}

	ubd_dev->size = ROUND_BLOCK(ubd_dev->size);

	INIT_LIST_HEAD(&ubd_dev->restart);
	sg_init_table(ubd_dev->sg, MAX_SG);

	err = -ENOMEM;
	ubd_dev->queue = blk_init_queue(do_ubd_request, &ubd_dev->lock);
	if (ubd_dev->queue == NULL) {
		*error_out = "Failed to initialize device queue";
		goto out;
	}
	ubd_dev->queue->queuedata = ubd_dev;

	blk_queue_max_hw_segments(ubd_dev->queue, MAX_SG);
	err = ubd_disk_register(MAJOR_NR, ubd_dev->size, n, &ubd_gendisk[n]);
	if(err){
		*error_out = "Failed to register device";
		goto out_cleanup;
	}

	if(fake_major != MAJOR_NR)
		ubd_disk_register(fake_major, ubd_dev->size, n,
				  &fake_gendisk[n]);

	/* perhaps this should also be under the "if (fake_major)" above */
	/* using the fake_disk->disk_name and also the fakehd_set name */
	if (fake_ide)
		make_ide_entries(ubd_gendisk[n]->disk_name);

	err = 0;
out:
	return err;

out_cleanup:
	blk_cleanup_queue(ubd_dev->queue);
	goto out;
}

static int ubd_config(char *str, char **error_out)
{
	int n, ret;

	/* This string is possibly broken up and stored, so it's only
	 * freed if ubd_setup_common fails, or if only general options
	 * were set.
	 */
	str = kstrdup(str, GFP_KERNEL);
	if (str == NULL) {
		*error_out = "Failed to allocate memory";
		return -ENOMEM;
	}

	ret = ubd_setup_common(str, &n, error_out);
	if (ret)
		goto err_free;

	if (n == -1) {
		ret = 0;
		goto err_free;
	}

	mutex_lock(&ubd_lock);
	ret = ubd_add(n, error_out);
	if (ret)
		ubd_devs[n].file = NULL;
	mutex_unlock(&ubd_lock);

out:
	return ret;

err_free:
	kfree(str);
	goto out;
}

static int ubd_get_config(char *name, char *str, int size, char **error_out)
{
	struct ubd *ubd_dev;
	int n, len = 0;

	n = parse_unit(&name);
	if((n >= MAX_DEV) || (n < 0)){
		*error_out = "ubd_get_config : device number out of range";
		return -1;
	}

	ubd_dev = &ubd_devs[n];
	mutex_lock(&ubd_lock);

	if(ubd_dev->file == NULL){
		CONFIG_CHUNK(str, size, len, "", 1);
		goto out;
	}

	CONFIG_CHUNK(str, size, len, ubd_dev->file, 0);

	if(ubd_dev->cow.file != NULL){
		CONFIG_CHUNK(str, size, len, ",", 0);
		CONFIG_CHUNK(str, size, len, ubd_dev->cow.file, 1);
	}
	else CONFIG_CHUNK(str, size, len, "", 1);

 out:
	mutex_unlock(&ubd_lock);
	return len;
}

static int ubd_id(char **str, int *start_out, int *end_out)
{
	int n;

	n = parse_unit(str);
	*start_out = 0;
	*end_out = MAX_DEV - 1;
	return n;
}

static int ubd_remove(int n, char **error_out)
{
	struct gendisk *disk = ubd_gendisk[n];
	struct ubd *ubd_dev;
	int err = -ENODEV;

	mutex_lock(&ubd_lock);

	ubd_dev = &ubd_devs[n];

	if(ubd_dev->file == NULL)
		goto out;

	/* you cannot remove a open disk */
	err = -EBUSY;
	if(ubd_dev->count > 0)
		goto out;

	ubd_gendisk[n] = NULL;
	if(disk != NULL){
		del_gendisk(disk);
		put_disk(disk);
	}

	if(fake_gendisk[n] != NULL){
		del_gendisk(fake_gendisk[n]);
		put_disk(fake_gendisk[n]);
		fake_gendisk[n] = NULL;
	}

	err = 0;
	platform_device_unregister(&ubd_dev->pdev);
out:
	mutex_unlock(&ubd_lock);
	return err;
}

/* All these are called by mconsole in process context and without
 * ubd-specific locks.  The structure itself is const except for .list.
 */
static struct mc_device ubd_mc = {
	.list		= LIST_HEAD_INIT(ubd_mc.list),
	.name		= "ubd",
	.config		= ubd_config,
	.get_config	= ubd_get_config,
	.id		= ubd_id,
	.remove		= ubd_remove,
};

static int __init ubd_mc_init(void)
{
	mconsole_register_dev(&ubd_mc);
	return 0;
}

__initcall(ubd_mc_init);

static int __init ubd0_init(void)
{
	struct ubd *ubd_dev = &ubd_devs[0];

	mutex_lock(&ubd_lock);
	if(ubd_dev->file == NULL)
		ubd_dev->file = "root_fs";
	mutex_unlock(&ubd_lock);

	return 0;
}

__initcall(ubd0_init);

/* Used in ubd_init, which is an initcall */
static struct platform_driver ubd_driver = {
	.driver = {
		.name  = DRIVER_NAME,
	},
};

static int __init ubd_init(void)
{
	char *error;
	int i, err;

	if (register_blkdev(MAJOR_NR, "ubd"))
		return -1;

	if (fake_major != MAJOR_NR) {
		char name[sizeof("ubd_nnn\0")];

		snprintf(name, sizeof(name), "ubd_%d", fake_major);
		if (register_blkdev(fake_major, "ubd"))
			return -1;
	}
	platform_driver_register(&ubd_driver);
	mutex_lock(&ubd_lock);
	for (i = 0; i < MAX_DEV; i++){
		err = ubd_add(i, &error);
		if(err)
			printk(KERN_ERR "Failed to initialize ubd device %d :"
			       "%s\n", i, error);
	}
	mutex_unlock(&ubd_lock);
	return 0;
}

late_initcall(ubd_init);

static int __init ubd_driver_init(void){
	unsigned long stack;
	int err;

	/* Set by CONFIG_BLK_DEV_UBD_SYNC or ubd=sync.*/
	if(global_openflags.s){
		printk(KERN_INFO "ubd: Synchronous mode\n");
		/* Letting ubd=sync be like using ubd#s= instead of ubd#= is
		 * enough. So use anyway the io thread. */
	}
	stack = alloc_stack(0, 0);
	io_pid = start_io_thread(stack + PAGE_SIZE - sizeof(void *),
				 &thread_fd);
	if(io_pid < 0){
		printk(KERN_ERR
		       "ubd : Failed to start I/O thread (errno = %d) - "
		       "falling back to synchronous I/O\n", -io_pid);
		io_pid = -1;
		return 0;
	}
	err = um_request_irq(UBD_IRQ, thread_fd, IRQ_READ, ubd_intr,
			     IRQF_DISABLED, "ubd", ubd_devs);
	if(err != 0)
		printk(KERN_ERR "um_request_irq failed - errno = %d\n", -err);
	return 0;
}

device_initcall(ubd_driver_init);

static int ubd_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ubd *ubd_dev = disk->private_data;
	int err = 0;

	if(ubd_dev->count == 0){
		err = ubd_open_dev(ubd_dev);
		if(err){
			printk(KERN_ERR "%s: Can't open \"%s\": errno = %d\n",
			       disk->disk_name, ubd_dev->file, -err);
			goto out;
		}
	}
	ubd_dev->count++;
	set_disk_ro(disk, !ubd_dev->openflags.w);

	/* This should no more be needed. And it didn't work anyway to exclude
	 * read-write remounting of filesystems.*/
	/*if((filp->f_mode & FMODE_WRITE) && !ubd_dev->openflags.w){
	        if(--ubd_dev->count == 0) ubd_close_dev(ubd_dev);
	        err = -EROFS;
	}*/
 out:
	return err;
}

static int ubd_release(struct inode * inode, struct file * file)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ubd *ubd_dev = disk->private_data;

	if(--ubd_dev->count == 0)
		ubd_close_dev(ubd_dev);
	return 0;
}

static void cowify_bitmap(__u64 io_offset, int length, unsigned long *cow_mask,
			  __u64 *cow_offset, unsigned long *bitmap,
			  __u64 bitmap_offset, unsigned long *bitmap_words,
			  __u64 bitmap_len)
{
	__u64 sector = io_offset >> 9;
	int i, update_bitmap = 0;

	for(i = 0; i < length >> 9; i++){
		if(cow_mask != NULL)
			ubd_set_bit(i, (unsigned char *) cow_mask);
		if(ubd_test_bit(sector + i, (unsigned char *) bitmap))
			continue;

		update_bitmap = 1;
		ubd_set_bit(sector + i, (unsigned char *) bitmap);
	}

	if(!update_bitmap)
		return;

	*cow_offset = sector / (sizeof(unsigned long) * 8);

	/* This takes care of the case where we're exactly at the end of the
	 * device, and *cow_offset + 1 is off the end.  So, just back it up
	 * by one word.  Thanks to Lynn Kerby for the fix and James McMechan
	 * for the original diagnosis.
	 */
	if(*cow_offset == ((bitmap_len + sizeof(unsigned long) - 1) /
			   sizeof(unsigned long) - 1))
		(*cow_offset)--;

	bitmap_words[0] = bitmap[*cow_offset];
	bitmap_words[1] = bitmap[*cow_offset + 1];

	*cow_offset *= sizeof(unsigned long);
	*cow_offset += bitmap_offset;
}

static void cowify_req(struct io_thread_req *req, unsigned long *bitmap,
		       __u64 bitmap_offset, __u64 bitmap_len)
{
	__u64 sector = req->offset >> 9;
	int i;

	if(req->length > (sizeof(req->sector_mask) * 8) << 9)
		panic("Operation too long");

	if(req->op == UBD_READ) {
		for(i = 0; i < req->length >> 9; i++){
			if(ubd_test_bit(sector + i, (unsigned char *) bitmap))
				ubd_set_bit(i, (unsigned char *)
					    &req->sector_mask);
		}
	}
	else cowify_bitmap(req->offset, req->length, &req->sector_mask,
			   &req->cow_offset, bitmap, bitmap_offset,
			   req->bitmap_words, bitmap_len);
}

/* Called with dev->lock held */
static void prepare_request(struct request *req, struct io_thread_req *io_req,
			    unsigned long long offset, int page_offset,
			    int len, struct page *page)
{
	struct gendisk *disk = req->rq_disk;
	struct ubd *ubd_dev = disk->private_data;

	io_req->req = req;
	io_req->fds[0] = (ubd_dev->cow.file != NULL) ? ubd_dev->cow.fd :
		ubd_dev->fd;
	io_req->fds[1] = ubd_dev->fd;
	io_req->cow_offset = -1;
	io_req->offset = offset;
	io_req->length = len;
	io_req->error = 0;
	io_req->sector_mask = 0;

	io_req->op = (rq_data_dir(req) == READ) ? UBD_READ : UBD_WRITE;
	io_req->offsets[0] = 0;
	io_req->offsets[1] = ubd_dev->cow.data_offset;
	io_req->buffer = page_address(page) + page_offset;
	io_req->sectorsize = 1 << 9;

	if(ubd_dev->cow.file != NULL)
		cowify_req(io_req, ubd_dev->cow.bitmap,
			   ubd_dev->cow.bitmap_offset, ubd_dev->cow.bitmap_len);

}

/* Called with dev->lock held */
static void do_ubd_request(struct request_queue *q)
{
	struct io_thread_req *io_req;
	struct request *req;
	int n, last_sectors;

	while(1){
		struct ubd *dev = q->queuedata;
		if(dev->end_sg == 0){
			struct request *req = elv_next_request(q);
			if(req == NULL)
				return;

			dev->request = req;
			blkdev_dequeue_request(req);
			dev->start_sg = 0;
			dev->end_sg = blk_rq_map_sg(q, req, dev->sg);
		}

		req = dev->request;
		last_sectors = 0;
		while(dev->start_sg < dev->end_sg){
			struct scatterlist *sg = &dev->sg[dev->start_sg];

			req->sector += last_sectors;
			io_req = kmalloc(sizeof(struct io_thread_req),
					 GFP_ATOMIC);
			if(io_req == NULL){
				if(list_empty(&dev->restart))
					list_add(&dev->restart, &restart);
				return;
			}
			prepare_request(req, io_req,
					(unsigned long long) req->sector << 9,
					sg->offset, sg->length, sg_page(sg));

			last_sectors = sg->length >> 9;
			n = os_write_file(thread_fd, &io_req,
					  sizeof(struct io_thread_req *));
			if(n != sizeof(struct io_thread_req *)){
				if(n != -EAGAIN)
					printk("write to io thread failed, "
					       "errno = %d\n", -n);
				else if(list_empty(&dev->restart))
					list_add(&dev->restart, &restart);
				kfree(io_req);
				return;
			}

			dev->start_sg++;
		}
		dev->end_sg = 0;
		dev->request = NULL;
	}
}

static int ubd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct ubd *ubd_dev = bdev->bd_disk->private_data;

	geo->heads = 128;
	geo->sectors = 32;
	geo->cylinders = ubd_dev->size / (128 * 32 * 512);
	return 0;
}

static int ubd_ioctl(struct inode * inode, struct file * file,
		     unsigned int cmd, unsigned long arg)
{
	struct ubd *ubd_dev = inode->i_bdev->bd_disk->private_data;
	struct hd_driveid ubd_id = {
		.cyls		= 0,
		.heads		= 128,
		.sectors	= 32,
	};

	switch (cmd) {
		struct cdrom_volctrl volume;
	case HDIO_GET_IDENTITY:
		ubd_id.cyls = ubd_dev->size / (128 * 32 * 512);
		if(copy_to_user((char __user *) arg, (char *) &ubd_id,
				 sizeof(ubd_id)))
			return -EFAULT;
		return 0;

	case CDROMVOLREAD:
		if(copy_from_user(&volume, (char __user *) arg, sizeof(volume)))
			return -EFAULT;
		volume.channel0 = 255;
		volume.channel1 = 255;
		volume.channel2 = 255;
		volume.channel3 = 255;
		if(copy_to_user((char __user *) arg, &volume, sizeof(volume)))
			return -EFAULT;
		return 0;
	}
	return -EINVAL;
}

static int path_requires_switch(char *from_cmdline, char *from_cow, char *cow)
{
	struct uml_stat buf1, buf2;
	int err;

	if(from_cmdline == NULL)
		return 0;
	if(!strcmp(from_cmdline, from_cow))
		return 0;

	err = os_stat_file(from_cmdline, &buf1);
	if(err < 0){
		printk("Couldn't stat '%s', err = %d\n", from_cmdline, -err);
		return 0;
	}
	err = os_stat_file(from_cow, &buf2);
	if(err < 0){
		printk("Couldn't stat '%s', err = %d\n", from_cow, -err);
		return 1;
	}
	if((buf1.ust_dev == buf2.ust_dev) && (buf1.ust_ino == buf2.ust_ino))
		return 0;

	printk("Backing file mismatch - \"%s\" requested,\n"
	       "\"%s\" specified in COW header of \"%s\"\n",
	       from_cmdline, from_cow, cow);
	return 1;
}

static int backing_file_mismatch(char *file, __u64 size, time_t mtime)
{
	unsigned long modtime;
	unsigned long long actual;
	int err;

	err = os_file_modtime(file, &modtime);
	if(err < 0){
		printk("Failed to get modification time of backing file "
		       "\"%s\", err = %d\n", file, -err);
		return err;
	}

	err = os_file_size(file, &actual);
	if(err < 0){
		printk("Failed to get size of backing file \"%s\", "
		       "err = %d\n", file, -err);
		return err;
	}

	if(actual != size){
		/*__u64 can be a long on AMD64 and with %lu GCC complains; so
		 * the typecast.*/
		printk("Size mismatch (%llu vs %llu) of COW header vs backing "
		       "file\n", (unsigned long long) size, actual);
		return -EINVAL;
	}
	if(modtime != mtime){
		printk("mtime mismatch (%ld vs %ld) of COW header vs backing "
		       "file\n", mtime, modtime);
		return -EINVAL;
	}
	return 0;
}

int read_cow_bitmap(int fd, void *buf, int offset, int len)
{
	int err;

	err = os_seek_file(fd, offset);
	if(err < 0)
		return err;

	err = os_read_file(fd, buf, len);
	if(err < 0)
		return err;

	return 0;
}

int open_ubd_file(char *file, struct openflags *openflags, int shared,
		  char **backing_file_out, int *bitmap_offset_out,
		  unsigned long *bitmap_len_out, int *data_offset_out,
		  int *create_cow_out)
{
	time_t mtime;
	unsigned long long size;
	__u32 version, align;
	char *backing_file;
	int fd, err, sectorsize, asked_switch, mode = 0644;

	fd = os_open_file(file, *openflags, mode);
	if (fd < 0) {
		if ((fd == -ENOENT) && (create_cow_out != NULL))
			*create_cow_out = 1;
		if (!openflags->w ||
		    ((fd != -EROFS) && (fd != -EACCES)))
			return fd;
		openflags->w = 0;
		fd = os_open_file(file, *openflags, mode);
		if (fd < 0)
			return fd;
	}

	if(shared)
		printk("Not locking \"%s\" on the host\n", file);
	else {
		err = os_lock_file(fd, openflags->w);
		if(err < 0){
			printk("Failed to lock '%s', err = %d\n", file, -err);
			goto out_close;
		}
	}

	/* Successful return case! */
	if(backing_file_out == NULL)
		return fd;

	err = read_cow_header(file_reader, &fd, &version, &backing_file, &mtime,
			      &size, &sectorsize, &align, bitmap_offset_out);
	if(err && (*backing_file_out != NULL)){
		printk("Failed to read COW header from COW file \"%s\", "
		       "errno = %d\n", file, -err);
		goto out_close;
	}
	if(err)
		return fd;

	asked_switch = path_requires_switch(*backing_file_out, backing_file, file);

	/* Allow switching only if no mismatch. */
	if (asked_switch && !backing_file_mismatch(*backing_file_out, size, mtime)) {
		printk("Switching backing file to '%s'\n", *backing_file_out);
		err = write_cow_header(file, fd, *backing_file_out,
				       sectorsize, align, &size);
		if (err) {
			printk("Switch failed, errno = %d\n", -err);
			goto out_close;
		}
	} else {
		*backing_file_out = backing_file;
		err = backing_file_mismatch(*backing_file_out, size, mtime);
		if (err)
			goto out_close;
	}

	cow_sizes(version, size, sectorsize, align, *bitmap_offset_out,
		  bitmap_len_out, data_offset_out);

	return fd;
 out_close:
	os_close_file(fd);
	return err;
}

int create_cow_file(char *cow_file, char *backing_file, struct openflags flags,
		    int sectorsize, int alignment, int *bitmap_offset_out,
		    unsigned long *bitmap_len_out, int *data_offset_out)
{
	int err, fd;

	flags.c = 1;
	fd = open_ubd_file(cow_file, &flags, 0, NULL, NULL, NULL, NULL, NULL);
	if(fd < 0){
		err = fd;
		printk("Open of COW file '%s' failed, errno = %d\n", cow_file,
		       -err);
		goto out;
	}

	err = init_cow_file(fd, cow_file, backing_file, sectorsize, alignment,
			    bitmap_offset_out, bitmap_len_out,
			    data_offset_out);
	if(!err)
		return fd;
	os_close_file(fd);
 out:
	return err;
}

static int update_bitmap(struct io_thread_req *req)
{
	int n;

	if(req->cow_offset == -1)
		return 0;

	n = os_seek_file(req->fds[1], req->cow_offset);
	if(n < 0){
		printk("do_io - bitmap lseek failed : err = %d\n", -n);
		return 1;
	}

	n = os_write_file(req->fds[1], &req->bitmap_words,
			  sizeof(req->bitmap_words));
	if(n != sizeof(req->bitmap_words)){
		printk("do_io - bitmap update failed, err = %d fd = %d\n", -n,
		       req->fds[1]);
		return 1;
	}

	return 0;
}

void do_io(struct io_thread_req *req)
{
	char *buf;
	unsigned long len;
	int n, nsectors, start, end, bit;
	int err;
	__u64 off;

	nsectors = req->length / req->sectorsize;
	start = 0;
	do {
		bit = ubd_test_bit(start, (unsigned char *) &req->sector_mask);
		end = start;
		while((end < nsectors) &&
		      (ubd_test_bit(end, (unsigned char *)
				    &req->sector_mask) == bit))
			end++;

		off = req->offset + req->offsets[bit] +
			start * req->sectorsize;
		len = (end - start) * req->sectorsize;
		buf = &req->buffer[start * req->sectorsize];

		err = os_seek_file(req->fds[bit], off);
		if(err < 0){
			printk("do_io - lseek failed : err = %d\n", -err);
			req->error = 1;
			return;
		}
		if(req->op == UBD_READ){
			n = 0;
			do {
				buf = &buf[n];
				len -= n;
				n = os_read_file(req->fds[bit], buf, len);
				if (n < 0) {
					printk("do_io - read failed, err = %d "
					       "fd = %d\n", -n, req->fds[bit]);
					req->error = 1;
					return;
				}
			} while((n < len) && (n != 0));
			if (n < len) memset(&buf[n], 0, len - n);
		} else {
			n = os_write_file(req->fds[bit], buf, len);
			if(n != len){
				printk("do_io - write failed err = %d "
				       "fd = %d\n", -n, req->fds[bit]);
				req->error = 1;
				return;
			}
		}

		start = end;
	} while(start < nsectors);

	req->error = update_bitmap(req);
}

/* Changed in start_io_thread, which is serialized by being called only
 * from ubd_init, which is an initcall.
 */
int kernel_fd = -1;

/* Only changed by the io thread. XXX: currently unused. */
static int io_count = 0;

int io_thread(void *arg)
{
	struct io_thread_req *req;
	int n;

	ignore_sigwinch_sig();
	while(1){
		n = os_read_file(kernel_fd, &req,
				 sizeof(struct io_thread_req *));
		if(n != sizeof(struct io_thread_req *)){
			if(n < 0)
				printk("io_thread - read failed, fd = %d, "
				       "err = %d\n", kernel_fd, -n);
			else {
				printk("io_thread - short read, fd = %d, "
				       "length = %d\n", kernel_fd, n);
			}
			continue;
		}
		io_count++;
		do_io(req);
		n = os_write_file(kernel_fd, &req,
				  sizeof(struct io_thread_req *));
		if(n != sizeof(struct io_thread_req *))
			printk("io_thread - write failed, fd = %d, err = %d\n",
			       kernel_fd, -n);
	}

	return 0;
}
