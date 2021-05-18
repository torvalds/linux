// SPDX-License-Identifier: GPL-2.0-only
/*
 * SCSI Media Changer device driver for Linux 2.6
 *
 *     (c) 1996-2003 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#define VERSION "0.25"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/compat.h>
#include <linux/chio.h>			/* here are all the ioctls */
#include <linux/mutex.h>
#include <linux/idr.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dbg.h>

#define CH_DT_MAX       16
#define CH_TYPES        8
#define CH_MAX_DEVS     128

MODULE_DESCRIPTION("device driver for scsi media changer devices");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(SCSI_CHANGER_MAJOR);
MODULE_ALIAS_SCSI_DEVICE(TYPE_MEDIUM_CHANGER);

static int init = 1;
module_param(init, int, 0444);
MODULE_PARM_DESC(init, \
    "initialize element status on driver load (default: on)");

static int timeout_move = 300;
module_param(timeout_move, int, 0644);
MODULE_PARM_DESC(timeout_move,"timeout for move commands "
		 "(default: 300 seconds)");

static int timeout_init = 3600;
module_param(timeout_init, int, 0644);
MODULE_PARM_DESC(timeout_init,"timeout for INITIALIZE ELEMENT STATUS "
		 "(default: 3600 seconds)");

static int verbose = 1;
module_param(verbose, int, 0644);
MODULE_PARM_DESC(verbose,"be verbose (default: on)");

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug,"enable/disable debug messages, also prints more "
		 "detailed sense codes on scsi errors (default: off)");

static int dt_id[CH_DT_MAX] = { [ 0 ... (CH_DT_MAX-1) ] = -1 };
static int dt_lun[CH_DT_MAX];
module_param_array(dt_id,  int, NULL, 0444);
module_param_array(dt_lun, int, NULL, 0444);

/* tell the driver about vendor-specific slots */
static int vendor_firsts[CH_TYPES-4];
static int vendor_counts[CH_TYPES-4];
module_param_array(vendor_firsts, int, NULL, 0444);
module_param_array(vendor_counts, int, NULL, 0444);

static const char * vendor_labels[CH_TYPES-4] = {
	"v0", "v1", "v2", "v3"
};
// module_param_string_array(vendor_labels, NULL, 0444);

#define ch_printk(prefix, ch, fmt, a...) \
	sdev_prefix_printk(prefix, (ch)->device, (ch)->name, fmt, ##a)

#define DPRINTK(fmt, arg...)						\
do {									\
	if (debug)							\
		ch_printk(KERN_DEBUG, ch, fmt, ##arg);			\
} while (0)
#define VPRINTK(level, fmt, arg...)					\
do {									\
	if (verbose)							\
		ch_printk(level, ch, fmt, ##arg);			\
} while (0)

/* ------------------------------------------------------------------- */

#define MAX_RETRIES   1

static struct class * ch_sysfs_class;

typedef struct {
	struct kref         ref;
	struct list_head    list;
	int                 minor;
	char                name[8];
	struct scsi_device  *device;
	struct scsi_device  **dt;        /* ptrs to data transfer elements */
	u_int               firsts[CH_TYPES];
	u_int               counts[CH_TYPES];
	u_int               unit_attention;
	u_int		    voltags;
	struct mutex	    lock;
} scsi_changer;

static DEFINE_IDR(ch_index_idr);
static DEFINE_SPINLOCK(ch_index_lock);

static const struct {
	unsigned char  sense;
	unsigned char  asc;
	unsigned char  ascq;
	int	       errno;
} ch_err[] = {
/* Just filled in what looks right. Hav'nt checked any standard paper for
   these errno assignments, so they may be wrong... */
	{
		.sense  = ILLEGAL_REQUEST,
		.asc    = 0x21,
		.ascq   = 0x01,
		.errno  = EBADSLT, /* Invalid element address */
	},{
		.sense  = ILLEGAL_REQUEST,
		.asc    = 0x28,
		.ascq   = 0x01,
		.errno  = EBADE,   /* Import or export element accessed */
	},{
		.sense  = ILLEGAL_REQUEST,
		.asc    = 0x3B,
		.ascq   = 0x0D,
		.errno  = EXFULL,  /* Medium destination element full */
	},{
		.sense  = ILLEGAL_REQUEST,
		.asc    = 0x3B,
		.ascq   = 0x0E,
		.errno  = EBADE,   /* Medium source element empty */
	},{
		.sense  = ILLEGAL_REQUEST,
		.asc    = 0x20,
		.ascq   = 0x00,
		.errno  = EBADRQC, /* Invalid command operation code */
	},{
	        /* end of list */
	}
};

/* ------------------------------------------------------------------- */

static int ch_find_errno(struct scsi_sense_hdr *sshdr)
{
	int i,errno = 0;

	/* Check to see if additional sense information is available */
	if (scsi_sense_valid(sshdr) &&
	    sshdr->asc != 0) {
		for (i = 0; ch_err[i].errno != 0; i++) {
			if (ch_err[i].sense == sshdr->sense_key &&
			    ch_err[i].asc   == sshdr->asc &&
			    ch_err[i].ascq  == sshdr->ascq) {
				errno = -ch_err[i].errno;
				break;
			}
		}
	}
	if (errno == 0)
		errno = -EIO;
	return errno;
}

static int
ch_do_scsi(scsi_changer *ch, unsigned char *cmd, int cmd_len,
	   void *buffer, unsigned buflength,
	   enum dma_data_direction direction)
{
	int errno, retries = 0, timeout, result;
	struct scsi_sense_hdr sshdr;

	timeout = (cmd[0] == INITIALIZE_ELEMENT_STATUS)
		? timeout_init : timeout_move;

 retry:
	errno = 0;
	result = scsi_execute_req(ch->device, cmd, direction, buffer,
				  buflength, &sshdr, timeout * HZ,
				  MAX_RETRIES, NULL);

	if (driver_byte(result) == DRIVER_SENSE) {
		if (debug)
			scsi_print_sense_hdr(ch->device, ch->name, &sshdr);
		errno = ch_find_errno(&sshdr);

		switch(sshdr.sense_key) {
		case UNIT_ATTENTION:
			ch->unit_attention = 1;
			if (retries++ < 3)
				goto retry;
			break;
		}
	}
	return errno;
}

/* ------------------------------------------------------------------------ */

static int
ch_elem_to_typecode(scsi_changer *ch, u_int elem)
{
	int i;

	for (i = 0; i < CH_TYPES; i++) {
		if (elem >= ch->firsts[i]  &&
		    elem <  ch->firsts[i] +
	            ch->counts[i])
			return i+1;
	}
	return 0;
}

static int
ch_read_element_status(scsi_changer *ch, u_int elem, char *data)
{
	u_char  cmd[12];
	u_char  *buffer;
	int     result;

	buffer = kmalloc(512, GFP_KERNEL | GFP_DMA);
	if(!buffer)
		return -ENOMEM;

 retry:
	memset(cmd,0,sizeof(cmd));
	cmd[0] = READ_ELEMENT_STATUS;
	cmd[1] = ((ch->device->lun & 0x7) << 5) |
		(ch->voltags ? 0x10 : 0) |
		ch_elem_to_typecode(ch,elem);
	cmd[2] = (elem >> 8) & 0xff;
	cmd[3] = elem        & 0xff;
	cmd[5] = 1;
	cmd[9] = 255;
	if (0 == (result = ch_do_scsi(ch, cmd, 12,
				      buffer, 256, DMA_FROM_DEVICE))) {
		if (((buffer[16] << 8) | buffer[17]) != elem) {
			DPRINTK("asked for element 0x%02x, got 0x%02x\n",
				elem,(buffer[16] << 8) | buffer[17]);
			kfree(buffer);
			return -EIO;
		}
		memcpy(data,buffer+16,16);
	} else {
		if (ch->voltags) {
			ch->voltags = 0;
			VPRINTK(KERN_INFO, "device has no volume tag support\n");
			goto retry;
		}
		DPRINTK("READ ELEMENT STATUS for element 0x%x failed\n",elem);
	}
	kfree(buffer);
	return result;
}

static int
ch_init_elem(scsi_changer *ch)
{
	int err;
	u_char cmd[6];

	VPRINTK(KERN_INFO, "INITIALIZE ELEMENT STATUS, may take some time ...\n");
	memset(cmd,0,sizeof(cmd));
	cmd[0] = INITIALIZE_ELEMENT_STATUS;
	cmd[1] = (ch->device->lun & 0x7) << 5;
	err = ch_do_scsi(ch, cmd, 6, NULL, 0, DMA_NONE);
	VPRINTK(KERN_INFO, "... finished\n");
	return err;
}

static int
ch_readconfig(scsi_changer *ch)
{
	u_char  cmd[10], data[16];
	u_char  *buffer;
	int     result,id,lun,i;
	u_int   elem;

	buffer = kzalloc(512, GFP_KERNEL | GFP_DMA);
	if (!buffer)
		return -ENOMEM;

	memset(cmd,0,sizeof(cmd));
	cmd[0] = MODE_SENSE;
	cmd[1] = (ch->device->lun & 0x7) << 5;
	cmd[2] = 0x1d;
	cmd[4] = 255;
	result = ch_do_scsi(ch, cmd, 10, buffer, 255, DMA_FROM_DEVICE);
	if (0 != result) {
		cmd[1] |= (1<<3);
		result  = ch_do_scsi(ch, cmd, 10, buffer, 255, DMA_FROM_DEVICE);
	}
	if (0 == result) {
		ch->firsts[CHET_MT] =
			(buffer[buffer[3]+ 6] << 8) | buffer[buffer[3]+ 7];
		ch->counts[CHET_MT] =
			(buffer[buffer[3]+ 8] << 8) | buffer[buffer[3]+ 9];
		ch->firsts[CHET_ST] =
			(buffer[buffer[3]+10] << 8) | buffer[buffer[3]+11];
		ch->counts[CHET_ST] =
			(buffer[buffer[3]+12] << 8) | buffer[buffer[3]+13];
		ch->firsts[CHET_IE] =
			(buffer[buffer[3]+14] << 8) | buffer[buffer[3]+15];
		ch->counts[CHET_IE] =
			(buffer[buffer[3]+16] << 8) | buffer[buffer[3]+17];
		ch->firsts[CHET_DT] =
			(buffer[buffer[3]+18] << 8) | buffer[buffer[3]+19];
		ch->counts[CHET_DT] =
			(buffer[buffer[3]+20] << 8) | buffer[buffer[3]+21];
		VPRINTK(KERN_INFO, "type #1 (mt): 0x%x+%d [medium transport]\n",
			ch->firsts[CHET_MT],
			ch->counts[CHET_MT]);
		VPRINTK(KERN_INFO, "type #2 (st): 0x%x+%d [storage]\n",
			ch->firsts[CHET_ST],
			ch->counts[CHET_ST]);
		VPRINTK(KERN_INFO, "type #3 (ie): 0x%x+%d [import/export]\n",
			ch->firsts[CHET_IE],
			ch->counts[CHET_IE]);
		VPRINTK(KERN_INFO, "type #4 (dt): 0x%x+%d [data transfer]\n",
			ch->firsts[CHET_DT],
			ch->counts[CHET_DT]);
	} else {
		VPRINTK(KERN_INFO, "reading element address assignment page failed!\n");
	}

	/* vendor specific element types */
	for (i = 0; i < 4; i++) {
		if (0 == vendor_counts[i])
			continue;
		if (NULL == vendor_labels[i])
			continue;
		ch->firsts[CHET_V1+i] = vendor_firsts[i];
		ch->counts[CHET_V1+i] = vendor_counts[i];
		VPRINTK(KERN_INFO, "type #%d (v%d): 0x%x+%d [%s, vendor specific]\n",
			i+5,i+1,vendor_firsts[i],vendor_counts[i],
			vendor_labels[i]);
	}

	/* look up the devices of the data transfer elements */
	ch->dt = kcalloc(ch->counts[CHET_DT], sizeof(*ch->dt),
			 GFP_KERNEL);

	if (!ch->dt) {
		kfree(buffer);
		return -ENOMEM;
	}

	for (elem = 0; elem < ch->counts[CHET_DT]; elem++) {
		id  = -1;
		lun = 0;
		if (elem < CH_DT_MAX  &&  -1 != dt_id[elem]) {
			id  = dt_id[elem];
			lun = dt_lun[elem];
			VPRINTK(KERN_INFO, "dt 0x%x: [insmod option] ",
				elem+ch->firsts[CHET_DT]);
		} else if (0 != ch_read_element_status
			   (ch,elem+ch->firsts[CHET_DT],data)) {
			VPRINTK(KERN_INFO, "dt 0x%x: READ ELEMENT STATUS failed\n",
				elem+ch->firsts[CHET_DT]);
		} else {
			VPRINTK(KERN_INFO, "dt 0x%x: ",elem+ch->firsts[CHET_DT]);
			if (data[6] & 0x80) {
				VPRINTK(KERN_CONT, "not this SCSI bus\n");
				ch->dt[elem] = NULL;
			} else if (0 == (data[6] & 0x30)) {
				VPRINTK(KERN_CONT, "ID/LUN unknown\n");
				ch->dt[elem] = NULL;
			} else {
				id  = ch->device->id;
				lun = 0;
				if (data[6] & 0x20) id  = data[7];
				if (data[6] & 0x10) lun = data[6] & 7;
			}
		}
		if (-1 != id) {
			VPRINTK(KERN_CONT, "ID %i, LUN %i, ",id,lun);
			ch->dt[elem] =
				scsi_device_lookup(ch->device->host,
						   ch->device->channel,
						   id,lun);
			if (!ch->dt[elem]) {
				/* should not happen */
				VPRINTK(KERN_CONT, "Huh? device not found!\n");
			} else {
				VPRINTK(KERN_CONT, "name: %8.8s %16.16s %4.4s\n",
					ch->dt[elem]->vendor,
					ch->dt[elem]->model,
					ch->dt[elem]->rev);
			}
		}
	}
	ch->voltags = 1;
	kfree(buffer);

	return 0;
}

/* ------------------------------------------------------------------------ */

static int
ch_position(scsi_changer *ch, u_int trans, u_int elem, int rotate)
{
	u_char  cmd[10];

	DPRINTK("position: 0x%x\n",elem);
	if (0 == trans)
		trans = ch->firsts[CHET_MT];
	memset(cmd,0,sizeof(cmd));
	cmd[0]  = POSITION_TO_ELEMENT;
	cmd[1]  = (ch->device->lun & 0x7) << 5;
	cmd[2]  = (trans >> 8) & 0xff;
	cmd[3]  =  trans       & 0xff;
	cmd[4]  = (elem  >> 8) & 0xff;
	cmd[5]  =  elem        & 0xff;
	cmd[8]  = rotate ? 1 : 0;
	return ch_do_scsi(ch, cmd, 10, NULL, 0, DMA_NONE);
}

static int
ch_move(scsi_changer *ch, u_int trans, u_int src, u_int dest, int rotate)
{
	u_char  cmd[12];

	DPRINTK("move: 0x%x => 0x%x\n",src,dest);
	if (0 == trans)
		trans = ch->firsts[CHET_MT];
	memset(cmd,0,sizeof(cmd));
	cmd[0]  = MOVE_MEDIUM;
	cmd[1]  = (ch->device->lun & 0x7) << 5;
	cmd[2]  = (trans >> 8) & 0xff;
	cmd[3]  =  trans       & 0xff;
	cmd[4]  = (src   >> 8) & 0xff;
	cmd[5]  =  src         & 0xff;
	cmd[6]  = (dest  >> 8) & 0xff;
	cmd[7]  =  dest        & 0xff;
	cmd[10] = rotate ? 1 : 0;
	return ch_do_scsi(ch, cmd, 12, NULL,0, DMA_NONE);
}

static int
ch_exchange(scsi_changer *ch, u_int trans, u_int src,
	    u_int dest1, u_int dest2, int rotate1, int rotate2)
{
	u_char  cmd[12];

	DPRINTK("exchange: 0x%x => 0x%x => 0x%x\n",
		src,dest1,dest2);
	if (0 == trans)
		trans = ch->firsts[CHET_MT];
	memset(cmd,0,sizeof(cmd));
	cmd[0]  = EXCHANGE_MEDIUM;
	cmd[1]  = (ch->device->lun & 0x7) << 5;
	cmd[2]  = (trans >> 8) & 0xff;
	cmd[3]  =  trans       & 0xff;
	cmd[4]  = (src   >> 8) & 0xff;
	cmd[5]  =  src         & 0xff;
	cmd[6]  = (dest1 >> 8) & 0xff;
	cmd[7]  =  dest1       & 0xff;
	cmd[8]  = (dest2 >> 8) & 0xff;
	cmd[9]  =  dest2       & 0xff;
	cmd[10] = (rotate1 ? 1 : 0) | (rotate2 ? 2 : 0);

	return ch_do_scsi(ch, cmd, 12, NULL, 0, DMA_NONE);
}

static void
ch_check_voltag(char *tag)
{
	int i;

	for (i = 0; i < 32; i++) {
		/* restrict to ascii */
		if (tag[i] >= 0x7f || tag[i] < 0x20)
			tag[i] = ' ';
		/* don't allow search wildcards */
		if (tag[i] == '?' ||
		    tag[i] == '*')
			tag[i] = ' ';
	}
}

static int
ch_set_voltag(scsi_changer *ch, u_int elem,
	      int alternate, int clear, u_char *tag)
{
	u_char  cmd[12];
	u_char  *buffer;
	int result;

	buffer = kzalloc(512, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	DPRINTK("%s %s voltag: 0x%x => \"%s\"\n",
		clear     ? "clear"     : "set",
		alternate ? "alternate" : "primary",
		elem, tag);
	memset(cmd,0,sizeof(cmd));
	cmd[0]  = SEND_VOLUME_TAG;
	cmd[1] = ((ch->device->lun & 0x7) << 5) |
		ch_elem_to_typecode(ch,elem);
	cmd[2] = (elem >> 8) & 0xff;
	cmd[3] = elem        & 0xff;
	cmd[5] = clear
		? (alternate ? 0x0d : 0x0c)
		: (alternate ? 0x0b : 0x0a);

	cmd[9] = 255;

	memcpy(buffer,tag,32);
	ch_check_voltag(buffer);

	result = ch_do_scsi(ch, cmd, 12, buffer, 256, DMA_TO_DEVICE);
	kfree(buffer);
	return result;
}

static int ch_gstatus(scsi_changer *ch, int type, unsigned char __user *dest)
{
	int retval = 0;
	u_char data[16];
	unsigned int i;

	mutex_lock(&ch->lock);
	for (i = 0; i < ch->counts[type]; i++) {
		if (0 != ch_read_element_status
		    (ch, ch->firsts[type]+i,data)) {
			retval = -EIO;
			break;
		}
		put_user(data[2], dest+i);
		if (data[2] & CESTATUS_EXCEPT)
			VPRINTK(KERN_INFO, "element 0x%x: asc=0x%x, ascq=0x%x\n",
				ch->firsts[type]+i,
				(int)data[4],(int)data[5]);
		retval = ch_read_element_status
			(ch, ch->firsts[type]+i,data);
		if (0 != retval)
			break;
	}
	mutex_unlock(&ch->lock);
	return retval;
}

/* ------------------------------------------------------------------------ */

static void ch_destroy(struct kref *ref)
{
	scsi_changer *ch = container_of(ref, scsi_changer, ref);

	ch->device = NULL;
	kfree(ch->dt);
	kfree(ch);
}

static int
ch_release(struct inode *inode, struct file *file)
{
	scsi_changer *ch = file->private_data;

	scsi_device_put(ch->device);
	file->private_data = NULL;
	kref_put(&ch->ref, ch_destroy);
	return 0;
}

static int
ch_open(struct inode *inode, struct file *file)
{
	scsi_changer *ch;
	int minor = iminor(inode);

	spin_lock(&ch_index_lock);
	ch = idr_find(&ch_index_idr, minor);

	if (ch == NULL || !kref_get_unless_zero(&ch->ref)) {
		spin_unlock(&ch_index_lock);
		return -ENXIO;
	}
	spin_unlock(&ch_index_lock);
	if (scsi_device_get(ch->device)) {
		kref_put(&ch->ref, ch_destroy);
		return -ENXIO;
	}
	/* Synchronize with ch_probe() */
	mutex_lock(&ch->lock);
	file->private_data = ch;
	mutex_unlock(&ch->lock);
	return 0;
}

static int
ch_checkrange(scsi_changer *ch, unsigned int type, unsigned int unit)
{
	if (type >= CH_TYPES  ||  unit >= ch->counts[type])
		return -1;
	return 0;
}

static long ch_ioctl(struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	scsi_changer *ch = file->private_data;
	int retval;
	void __user *argp = (void __user *)arg;

	retval = scsi_ioctl_block_when_processing_errors(ch->device, cmd,
			file->f_flags & O_NDELAY);
	if (retval)
		return retval;

	switch (cmd) {
	case CHIOGPARAMS:
	{
		struct changer_params params;

		params.cp_curpicker = 0;
		params.cp_npickers  = ch->counts[CHET_MT];
		params.cp_nslots    = ch->counts[CHET_ST];
		params.cp_nportals  = ch->counts[CHET_IE];
		params.cp_ndrives   = ch->counts[CHET_DT];

		if (copy_to_user(argp, &params, sizeof(params)))
			return -EFAULT;
		return 0;
	}
	case CHIOGVPARAMS:
	{
		struct changer_vendor_params vparams;

		memset(&vparams,0,sizeof(vparams));
		if (ch->counts[CHET_V1]) {
			vparams.cvp_n1  = ch->counts[CHET_V1];
			strncpy(vparams.cvp_label1,vendor_labels[0],16);
		}
		if (ch->counts[CHET_V2]) {
			vparams.cvp_n2  = ch->counts[CHET_V2];
			strncpy(vparams.cvp_label2,vendor_labels[1],16);
		}
		if (ch->counts[CHET_V3]) {
			vparams.cvp_n3  = ch->counts[CHET_V3];
			strncpy(vparams.cvp_label3,vendor_labels[2],16);
		}
		if (ch->counts[CHET_V4]) {
			vparams.cvp_n4  = ch->counts[CHET_V4];
			strncpy(vparams.cvp_label4,vendor_labels[3],16);
		}
		if (copy_to_user(argp, &vparams, sizeof(vparams)))
			return -EFAULT;
		return 0;
	}

	case CHIOPOSITION:
	{
		struct changer_position pos;

		if (copy_from_user(&pos, argp, sizeof (pos)))
			return -EFAULT;

		if (0 != ch_checkrange(ch, pos.cp_type, pos.cp_unit)) {
			DPRINTK("CHIOPOSITION: invalid parameter\n");
			return -EBADSLT;
		}
		mutex_lock(&ch->lock);
		retval = ch_position(ch,0,
				     ch->firsts[pos.cp_type] + pos.cp_unit,
				     pos.cp_flags & CP_INVERT);
		mutex_unlock(&ch->lock);
		return retval;
	}

	case CHIOMOVE:
	{
		struct changer_move mv;

		if (copy_from_user(&mv, argp, sizeof (mv)))
			return -EFAULT;

		if (0 != ch_checkrange(ch, mv.cm_fromtype, mv.cm_fromunit) ||
		    0 != ch_checkrange(ch, mv.cm_totype,   mv.cm_tounit  )) {
			DPRINTK("CHIOMOVE: invalid parameter\n");
			return -EBADSLT;
		}

		mutex_lock(&ch->lock);
		retval = ch_move(ch,0,
				 ch->firsts[mv.cm_fromtype] + mv.cm_fromunit,
				 ch->firsts[mv.cm_totype]   + mv.cm_tounit,
				 mv.cm_flags & CM_INVERT);
		mutex_unlock(&ch->lock);
		return retval;
	}

	case CHIOEXCHANGE:
	{
		struct changer_exchange mv;

		if (copy_from_user(&mv, argp, sizeof (mv)))
			return -EFAULT;

		if (0 != ch_checkrange(ch, mv.ce_srctype,  mv.ce_srcunit ) ||
		    0 != ch_checkrange(ch, mv.ce_fdsttype, mv.ce_fdstunit) ||
		    0 != ch_checkrange(ch, mv.ce_sdsttype, mv.ce_sdstunit)) {
			DPRINTK("CHIOEXCHANGE: invalid parameter\n");
			return -EBADSLT;
		}

		mutex_lock(&ch->lock);
		retval = ch_exchange
			(ch,0,
			 ch->firsts[mv.ce_srctype]  + mv.ce_srcunit,
			 ch->firsts[mv.ce_fdsttype] + mv.ce_fdstunit,
			 ch->firsts[mv.ce_sdsttype] + mv.ce_sdstunit,
			 mv.ce_flags & CE_INVERT1, mv.ce_flags & CE_INVERT2);
		mutex_unlock(&ch->lock);
		return retval;
	}

	case CHIOGSTATUS:
	{
		struct changer_element_status ces;

		if (copy_from_user(&ces, argp, sizeof (ces)))
			return -EFAULT;
		if (ces.ces_type < 0 || ces.ces_type >= CH_TYPES)
			return -EINVAL;

		return ch_gstatus(ch, ces.ces_type, ces.ces_data);
	}

	case CHIOGELEM:
	{
		struct changer_get_element cge;
		u_char ch_cmd[12];
		u_char *buffer;
		unsigned int elem;
		int     result,i;

		if (copy_from_user(&cge, argp, sizeof (cge)))
			return -EFAULT;

		if (0 != ch_checkrange(ch, cge.cge_type, cge.cge_unit))
			return -EINVAL;
		elem = ch->firsts[cge.cge_type] + cge.cge_unit;

		buffer = kmalloc(512, GFP_KERNEL | GFP_DMA);
		if (!buffer)
			return -ENOMEM;
		mutex_lock(&ch->lock);

	voltag_retry:
		memset(ch_cmd, 0, sizeof(ch_cmd));
		ch_cmd[0] = READ_ELEMENT_STATUS;
		ch_cmd[1] = ((ch->device->lun & 0x7) << 5) |
			(ch->voltags ? 0x10 : 0) |
			ch_elem_to_typecode(ch,elem);
		ch_cmd[2] = (elem >> 8) & 0xff;
		ch_cmd[3] = elem        & 0xff;
		ch_cmd[5] = 1;
		ch_cmd[9] = 255;

		result = ch_do_scsi(ch, ch_cmd, 12,
				    buffer, 256, DMA_FROM_DEVICE);
		if (!result) {
			cge.cge_status = buffer[18];
			cge.cge_flags = 0;
			if (buffer[18] & CESTATUS_EXCEPT) {
				cge.cge_errno = EIO;
			}
			if (buffer[25] & 0x80) {
				cge.cge_flags |= CGE_SRC;
				if (buffer[25] & 0x40)
					cge.cge_flags |= CGE_INVERT;
				elem = (buffer[26]<<8) | buffer[27];
				for (i = 0; i < 4; i++) {
					if (elem >= ch->firsts[i] &&
					    elem <  ch->firsts[i] + ch->counts[i]) {
						cge.cge_srctype = i;
						cge.cge_srcunit = elem-ch->firsts[i];
					}
				}
			}
			if ((buffer[22] & 0x30) == 0x30) {
				cge.cge_flags |= CGE_IDLUN;
				cge.cge_id  = buffer[23];
				cge.cge_lun = buffer[22] & 7;
			}
			if (buffer[9] & 0x80) {
				cge.cge_flags |= CGE_PVOLTAG;
				memcpy(cge.cge_pvoltag,buffer+28,36);
			}
			if (buffer[9] & 0x40) {
				cge.cge_flags |= CGE_AVOLTAG;
				memcpy(cge.cge_avoltag,buffer+64,36);
			}
		} else if (ch->voltags) {
			ch->voltags = 0;
			VPRINTK(KERN_INFO, "device has no volume tag support\n");
			goto voltag_retry;
		}
		kfree(buffer);
		mutex_unlock(&ch->lock);

		if (copy_to_user(argp, &cge, sizeof (cge)))
			return -EFAULT;
		return result;
	}

	case CHIOINITELEM:
	{
		mutex_lock(&ch->lock);
		retval = ch_init_elem(ch);
		mutex_unlock(&ch->lock);
		return retval;
	}

	case CHIOSVOLTAG:
	{
		struct changer_set_voltag csv;
		int elem;

		if (copy_from_user(&csv, argp, sizeof(csv)))
			return -EFAULT;

		if (0 != ch_checkrange(ch, csv.csv_type, csv.csv_unit)) {
			DPRINTK("CHIOSVOLTAG: invalid parameter\n");
			return -EBADSLT;
		}
		elem = ch->firsts[csv.csv_type] + csv.csv_unit;
		mutex_lock(&ch->lock);
		retval = ch_set_voltag(ch, elem,
				       csv.csv_flags & CSV_AVOLTAG,
				       csv.csv_flags & CSV_CLEARTAG,
				       csv.csv_voltag);
		mutex_unlock(&ch->lock);
		return retval;
	}

	default:
		return scsi_ioctl(ch->device, cmd, argp);

	}
}

#ifdef CONFIG_COMPAT

struct changer_element_status32 {
	int		ces_type;
	compat_uptr_t	ces_data;
};
#define CHIOGSTATUS32  _IOW('c', 8,struct changer_element_status32)

static long ch_ioctl_compat(struct file * file,
			    unsigned int cmd, unsigned long arg)
{
	scsi_changer *ch = file->private_data;
	int retval = scsi_ioctl_block_when_processing_errors(ch->device, cmd,
							file->f_flags & O_NDELAY);
	if (retval)
		return retval;

	switch (cmd) {
	case CHIOGPARAMS:
	case CHIOGVPARAMS:
	case CHIOPOSITION:
	case CHIOMOVE:
	case CHIOEXCHANGE:
	case CHIOGELEM:
	case CHIOINITELEM:
	case CHIOSVOLTAG:
		/* compatible */
		return ch_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
	case CHIOGSTATUS32:
	{
		struct changer_element_status32 ces32;
		unsigned char __user *data;

		if (copy_from_user(&ces32, (void __user *)arg, sizeof (ces32)))
			return -EFAULT;
		if (ces32.ces_type < 0 || ces32.ces_type >= CH_TYPES)
			return -EINVAL;

		data = compat_ptr(ces32.ces_data);
		return ch_gstatus(ch, ces32.ces_type, data);
	}
	default:
		return scsi_compat_ioctl(ch->device, cmd, compat_ptr(arg));

	}
}
#endif

/* ------------------------------------------------------------------------ */

static int ch_probe(struct device *dev)
{
	struct scsi_device *sd = to_scsi_device(dev);
	struct device *class_dev;
	int ret;
	scsi_changer *ch;

	if (sd->type != TYPE_MEDIUM_CHANGER)
		return -ENODEV;

	ch = kzalloc(sizeof(*ch), GFP_KERNEL);
	if (NULL == ch)
		return -ENOMEM;

	idr_preload(GFP_KERNEL);
	spin_lock(&ch_index_lock);
	ret = idr_alloc(&ch_index_idr, ch, 0, CH_MAX_DEVS + 1, GFP_NOWAIT);
	spin_unlock(&ch_index_lock);
	idr_preload_end();

	if (ret < 0) {
		if (ret == -ENOSPC)
			ret = -ENODEV;
		goto free_ch;
	}

	ch->minor = ret;
	sprintf(ch->name,"ch%d",ch->minor);
	ret = scsi_device_get(sd);
	if (ret) {
		sdev_printk(KERN_WARNING, sd, "ch%d: failed to get device\n",
			    ch->minor);
		goto remove_idr;
	}

	mutex_init(&ch->lock);
	kref_init(&ch->ref);
	ch->device = sd;
	class_dev = device_create(ch_sysfs_class, dev,
				  MKDEV(SCSI_CHANGER_MAJOR, ch->minor), ch,
				  "s%s", ch->name);
	if (IS_ERR(class_dev)) {
		sdev_printk(KERN_WARNING, sd, "ch%d: device_create failed\n",
			    ch->minor);
		ret = PTR_ERR(class_dev);
		goto put_device;
	}

	mutex_lock(&ch->lock);
	ret = ch_readconfig(ch);
	if (ret) {
		mutex_unlock(&ch->lock);
		goto destroy_dev;
	}
	if (init)
		ch_init_elem(ch);

	mutex_unlock(&ch->lock);
	dev_set_drvdata(dev, ch);
	sdev_printk(KERN_INFO, sd, "Attached scsi changer %s\n", ch->name);

	return 0;
destroy_dev:
	device_destroy(ch_sysfs_class, MKDEV(SCSI_CHANGER_MAJOR, ch->minor));
put_device:
	scsi_device_put(sd);
remove_idr:
	idr_remove(&ch_index_idr, ch->minor);
free_ch:
	kfree(ch);
	return ret;
}

static int ch_remove(struct device *dev)
{
	scsi_changer *ch = dev_get_drvdata(dev);

	spin_lock(&ch_index_lock);
	idr_remove(&ch_index_idr, ch->minor);
	dev_set_drvdata(dev, NULL);
	spin_unlock(&ch_index_lock);

	device_destroy(ch_sysfs_class, MKDEV(SCSI_CHANGER_MAJOR,ch->minor));
	scsi_device_put(ch->device);
	kref_put(&ch->ref, ch_destroy);
	return 0;
}

static struct scsi_driver ch_template = {
	.gendrv     	= {
		.name	= "ch",
		.owner	= THIS_MODULE,
		.probe  = ch_probe,
		.remove = ch_remove,
	},
};

static const struct file_operations changer_fops = {
	.owner		= THIS_MODULE,
	.open		= ch_open,
	.release	= ch_release,
	.unlocked_ioctl	= ch_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ch_ioctl_compat,
#endif
	.llseek		= noop_llseek,
};

static int __init init_ch_module(void)
{
	int rc;

	printk(KERN_INFO "SCSI Media Changer driver v" VERSION " \n");
        ch_sysfs_class = class_create(THIS_MODULE, "scsi_changer");
        if (IS_ERR(ch_sysfs_class)) {
		rc = PTR_ERR(ch_sysfs_class);
		return rc;
        }
	rc = register_chrdev(SCSI_CHANGER_MAJOR,"ch",&changer_fops);
	if (rc < 0) {
		printk("Unable to get major %d for SCSI-Changer\n",
		       SCSI_CHANGER_MAJOR);
		goto fail1;
	}
	rc = scsi_register_driver(&ch_template.gendrv);
	if (rc < 0)
		goto fail2;
	return 0;

 fail2:
	unregister_chrdev(SCSI_CHANGER_MAJOR, "ch");
 fail1:
	class_destroy(ch_sysfs_class);
	return rc;
}

static void __exit exit_ch_module(void)
{
	scsi_unregister_driver(&ch_template.gendrv);
	unregister_chrdev(SCSI_CHANGER_MAJOR, "ch");
	class_destroy(ch_sysfs_class);
	idr_destroy(&ch_index_idr);
}

module_init(init_ch_module);
module_exit(exit_ch_module);
