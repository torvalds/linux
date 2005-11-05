/* 
 *  Parallel SCSI (SPI) transport specific attributes exported to sysfs.
 *
 *  Copyright (c) 2003 Silicon Graphics, Inc.  All rights reserved.
 *  Copyright (c) 2004, 2005 James Bottomley <James.Bottomley@SteelEye.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/blkdev.h>
#include <asm/semaphore.h>
#include <scsi/scsi.h>
#include "scsi_priv.h"
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_spi.h>

#define SPI_NUM_ATTRS 14	/* increase this if you add attributes */
#define SPI_OTHER_ATTRS 1	/* Increase this if you add "always
				 * on" attributes */
#define SPI_HOST_ATTRS	1

#define SPI_MAX_ECHO_BUFFER_SIZE	4096

#define DV_LOOPS	3
#define DV_TIMEOUT	(10*HZ)
#define DV_RETRIES	3	/* should only need at most 
				 * two cc/ua clears */

/* Private data accessors (keep these out of the header file) */
#define spi_dv_pending(x) (((struct spi_transport_attrs *)&(x)->starget_data)->dv_pending)
#define spi_dv_sem(x) (((struct spi_transport_attrs *)&(x)->starget_data)->dv_sem)

struct spi_internal {
	struct scsi_transport_template t;
	struct spi_function_template *f;
	/* The actual attributes */
	struct class_device_attribute private_attrs[SPI_NUM_ATTRS];
	/* The array of null terminated pointers to attributes 
	 * needed by scsi_sysfs.c */
	struct class_device_attribute *attrs[SPI_NUM_ATTRS + SPI_OTHER_ATTRS + 1];
	struct class_device_attribute private_host_attrs[SPI_HOST_ATTRS];
	struct class_device_attribute *host_attrs[SPI_HOST_ATTRS + 1];
};

#define to_spi_internal(tmpl)	container_of(tmpl, struct spi_internal, t)

static const int ppr_to_ps[] = {
	/* The PPR values 0-6 are reserved, fill them in when
	 * the committee defines them */
	-1,			/* 0x00 */
	-1,			/* 0x01 */
	-1,			/* 0x02 */
	-1,			/* 0x03 */
	-1,			/* 0x04 */
	-1,			/* 0x05 */
	-1,			/* 0x06 */
	 3125,			/* 0x07 */
	 6250,			/* 0x08 */
	12500,			/* 0x09 */
	25000,			/* 0x0a */
	30300,			/* 0x0b */
	50000,			/* 0x0c */
};
/* The PPR values at which you calculate the period in ns by multiplying
 * by 4 */
#define SPI_STATIC_PPR	0x0c

static int sprint_frac(char *dest, int value, int denom)
{
	int frac = value % denom;
	int result = sprintf(dest, "%d", value / denom);

	if (frac == 0)
		return result;
	dest[result++] = '.';

	do {
		denom /= 10;
		sprintf(dest + result, "%d", frac / denom);
		result++;
		frac %= denom;
	} while (frac);

	dest[result++] = '\0';
	return result;
}

static int spi_execute(struct scsi_device *sdev, const void *cmd,
		       enum dma_data_direction dir,
		       void *buffer, unsigned bufflen,
		       struct scsi_sense_hdr *sshdr)
{
	int i, result;
	unsigned char sense[SCSI_SENSE_BUFFERSIZE];

	for(i = 0; i < DV_RETRIES; i++) {
		result = scsi_execute(sdev, cmd, dir, buffer, bufflen,
				      sense, DV_TIMEOUT, /* retries */ 1,
				      REQ_FAILFAST);
		if (result & DRIVER_SENSE) {
			struct scsi_sense_hdr sshdr_tmp;
			if (!sshdr)
				sshdr = &sshdr_tmp;

			if (scsi_normalize_sense(sense, sizeof(*sense),
						 sshdr)
			    && sshdr->sense_key == UNIT_ATTENTION)
				continue;
		}
		break;
	}
	return result;
}

static struct {
	enum spi_signal_type	value;
	char			*name;
} signal_types[] = {
	{ SPI_SIGNAL_UNKNOWN, "unknown" },
	{ SPI_SIGNAL_SE, "SE" },
	{ SPI_SIGNAL_LVD, "LVD" },
	{ SPI_SIGNAL_HVD, "HVD" },
};

static inline const char *spi_signal_to_string(enum spi_signal_type type)
{
	int i;

	for (i = 0; i < sizeof(signal_types)/sizeof(signal_types[0]); i++) {
		if (type == signal_types[i].value)
			return signal_types[i].name;
	}
	return NULL;
}
static inline enum spi_signal_type spi_signal_to_value(const char *name)
{
	int i, len;

	for (i = 0; i < sizeof(signal_types)/sizeof(signal_types[0]); i++) {
		len =  strlen(signal_types[i].name);
		if (strncmp(name, signal_types[i].name, len) == 0 &&
		    (name[len] == '\n' || name[len] == '\0'))
			return signal_types[i].value;
	}
	return SPI_SIGNAL_UNKNOWN;
}

static int spi_host_setup(struct transport_container *tc, struct device *dev,
			  struct class_device *cdev)
{
	struct Scsi_Host *shost = dev_to_shost(dev);

	spi_signalling(shost) = SPI_SIGNAL_UNKNOWN;

	return 0;
}

static DECLARE_TRANSPORT_CLASS(spi_host_class,
			       "spi_host",
			       spi_host_setup,
			       NULL,
			       NULL);

static int spi_host_match(struct attribute_container *cont,
			  struct device *dev)
{
	struct Scsi_Host *shost;
	struct spi_internal *i;

	if (!scsi_is_host_device(dev))
		return 0;

	shost = dev_to_shost(dev);
	if (!shost->transportt  || shost->transportt->host_attrs.ac.class
	    != &spi_host_class.class)
		return 0;

	i = to_spi_internal(shost->transportt);
	
	return &i->t.host_attrs.ac == cont;
}

static int spi_device_configure(struct transport_container *tc,
				struct device *dev,
				struct class_device *cdev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct scsi_target *starget = sdev->sdev_target;

	/* Populate the target capability fields with the values
	 * gleaned from the device inquiry */

	spi_support_sync(starget) = scsi_device_sync(sdev);
	spi_support_wide(starget) = scsi_device_wide(sdev);
	spi_support_dt(starget) = scsi_device_dt(sdev);
	spi_support_dt_only(starget) = scsi_device_dt_only(sdev);
	spi_support_ius(starget) = scsi_device_ius(sdev);
	spi_support_qas(starget) = scsi_device_qas(sdev);

	return 0;
}

static int spi_setup_transport_attrs(struct transport_container *tc,
				     struct device *dev,
				     struct class_device *cdev)
{
	struct scsi_target *starget = to_scsi_target(dev);

	spi_period(starget) = -1;	/* illegal value */
	spi_min_period(starget) = 0;
	spi_offset(starget) = 0;	/* async */
	spi_max_offset(starget) = 255;
	spi_width(starget) = 0;	/* narrow */
	spi_max_width(starget) = 1;
	spi_iu(starget) = 0;	/* no IU */
	spi_dt(starget) = 0;	/* ST */
	spi_qas(starget) = 0;
	spi_wr_flow(starget) = 0;
	spi_rd_strm(starget) = 0;
	spi_rti(starget) = 0;
	spi_pcomp_en(starget) = 0;
	spi_hold_mcs(starget) = 0;
	spi_dv_pending(starget) = 0;
	spi_initial_dv(starget) = 0;
	init_MUTEX(&spi_dv_sem(starget));

	return 0;
}

#define spi_transport_show_simple(field, format_string)			\
									\
static ssize_t								\
show_spi_transport_##field(struct class_device *cdev, char *buf)	\
{									\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct spi_transport_attrs *tp;					\
									\
	tp = (struct spi_transport_attrs *)&starget->starget_data;	\
	return snprintf(buf, 20, format_string, tp->field);		\
}

#define spi_transport_store_simple(field, format_string)		\
									\
static ssize_t								\
store_spi_transport_##field(struct class_device *cdev, const char *buf, \
			    size_t count)				\
{									\
	int val;							\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct spi_transport_attrs *tp;					\
									\
	tp = (struct spi_transport_attrs *)&starget->starget_data;	\
	val = simple_strtoul(buf, NULL, 0);				\
	tp->field = val;						\
	return count;							\
}

#define spi_transport_show_function(field, format_string)		\
									\
static ssize_t								\
show_spi_transport_##field(struct class_device *cdev, char *buf)	\
{									\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct spi_transport_attrs *tp;					\
	struct spi_internal *i = to_spi_internal(shost->transportt);	\
	tp = (struct spi_transport_attrs *)&starget->starget_data;	\
	if (i->f->get_##field)						\
		i->f->get_##field(starget);				\
	return snprintf(buf, 20, format_string, tp->field);		\
}

#define spi_transport_store_function(field, format_string)		\
static ssize_t								\
store_spi_transport_##field(struct class_device *cdev, const char *buf, \
			    size_t count)				\
{									\
	int val;							\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct spi_internal *i = to_spi_internal(shost->transportt);	\
									\
	val = simple_strtoul(buf, NULL, 0);				\
	i->f->set_##field(starget, val);			\
	return count;							\
}

#define spi_transport_store_max(field, format_string)			\
static ssize_t								\
store_spi_transport_##field(struct class_device *cdev, const char *buf, \
			    size_t count)				\
{									\
	int val;							\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct spi_internal *i = to_spi_internal(shost->transportt);	\
	struct spi_transport_attrs *tp					\
		= (struct spi_transport_attrs *)&starget->starget_data;	\
									\
	val = simple_strtoul(buf, NULL, 0);				\
	if (val > tp->max_##field)					\
		val = tp->max_##field;					\
	i->f->set_##field(starget, val);				\
	return count;							\
}

#define spi_transport_rd_attr(field, format_string)			\
	spi_transport_show_function(field, format_string)		\
	spi_transport_store_function(field, format_string)		\
static CLASS_DEVICE_ATTR(field, S_IRUGO | S_IWUSR,			\
			 show_spi_transport_##field,			\
			 store_spi_transport_##field);

#define spi_transport_simple_attr(field, format_string)			\
	spi_transport_show_simple(field, format_string)			\
	spi_transport_store_simple(field, format_string)		\
static CLASS_DEVICE_ATTR(field, S_IRUGO | S_IWUSR,			\
			 show_spi_transport_##field,			\
			 store_spi_transport_##field);

#define spi_transport_max_attr(field, format_string)			\
	spi_transport_show_function(field, format_string)		\
	spi_transport_store_max(field, format_string)			\
	spi_transport_simple_attr(max_##field, format_string)		\
static CLASS_DEVICE_ATTR(field, S_IRUGO | S_IWUSR,			\
			 show_spi_transport_##field,			\
			 store_spi_transport_##field);

/* The Parallel SCSI Tranport Attributes: */
spi_transport_max_attr(offset, "%d\n");
spi_transport_max_attr(width, "%d\n");
spi_transport_rd_attr(iu, "%d\n");
spi_transport_rd_attr(dt, "%d\n");
spi_transport_rd_attr(qas, "%d\n");
spi_transport_rd_attr(wr_flow, "%d\n");
spi_transport_rd_attr(rd_strm, "%d\n");
spi_transport_rd_attr(rti, "%d\n");
spi_transport_rd_attr(pcomp_en, "%d\n");
spi_transport_rd_attr(hold_mcs, "%d\n");

/* we only care about the first child device so we return 1 */
static int child_iter(struct device *dev, void *data)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	spi_dv_device(sdev);
	return 1;
}

static ssize_t
store_spi_revalidate(struct class_device *cdev, const char *buf, size_t count)
{
	struct scsi_target *starget = transport_class_to_starget(cdev);

	device_for_each_child(&starget->dev, NULL, child_iter);
	return count;
}
static CLASS_DEVICE_ATTR(revalidate, S_IWUSR, NULL, store_spi_revalidate);

/* Translate the period into ns according to the current spec
 * for SDTR/PPR messages */
static ssize_t
show_spi_transport_period_helper(struct class_device *cdev, char *buf,
				 int period)
{
	int len, picosec;

	if (period < 0 || period > 0xff) {
		picosec = -1;
	} else if (period <= SPI_STATIC_PPR) {
		picosec = ppr_to_ps[period];
	} else {
		picosec = period * 4000;
	}

	if (picosec == -1) {
		len = sprintf(buf, "reserved");
	} else {
		len = sprint_frac(buf, picosec, 1000);
	}

	buf[len++] = '\n';
	buf[len] = '\0';
	return len;
}

static ssize_t
store_spi_transport_period_helper(struct class_device *cdev, const char *buf,
				  size_t count, int *periodp)
{
	int j, picosec, period = -1;
	char *endp;

	picosec = simple_strtoul(buf, &endp, 10) * 1000;
	if (*endp == '.') {
		int mult = 100;
		do {
			endp++;
			if (!isdigit(*endp))
				break;
			picosec += (*endp - '0') * mult;
			mult /= 10;
		} while (mult > 0);
	}

	for (j = 0; j <= SPI_STATIC_PPR; j++) {
		if (ppr_to_ps[j] < picosec)
			continue;
		period = j;
		break;
	}

	if (period == -1)
		period = picosec / 4000;

	if (period > 0xff)
		period = 0xff;

	*periodp = period;

	return count;
}

static ssize_t
show_spi_transport_period(struct class_device *cdev, char *buf)
{
	struct scsi_target *starget = transport_class_to_starget(cdev);
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct spi_internal *i = to_spi_internal(shost->transportt);
	struct spi_transport_attrs *tp =
		(struct spi_transport_attrs *)&starget->starget_data;

	if (i->f->get_period)
		i->f->get_period(starget);

	return show_spi_transport_period_helper(cdev, buf, tp->period);
}

static ssize_t
store_spi_transport_period(struct class_device *cdev, const char *buf,
			    size_t count)
{
	struct scsi_target *starget = transport_class_to_starget(cdev);
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct spi_internal *i = to_spi_internal(shost->transportt);
	struct spi_transport_attrs *tp =
		(struct spi_transport_attrs *)&starget->starget_data;
	int period, retval;

	retval = store_spi_transport_period_helper(cdev, buf, count, &period);

	if (period < tp->min_period)
		period = tp->min_period;

	i->f->set_period(starget, period);

	return retval;
}

static CLASS_DEVICE_ATTR(period, S_IRUGO | S_IWUSR, 
			 show_spi_transport_period,
			 store_spi_transport_period);

static ssize_t
show_spi_transport_min_period(struct class_device *cdev, char *buf)
{
	struct scsi_target *starget = transport_class_to_starget(cdev);
	struct spi_transport_attrs *tp =
		(struct spi_transport_attrs *)&starget->starget_data;

	return show_spi_transport_period_helper(cdev, buf, tp->min_period);
}

static ssize_t
store_spi_transport_min_period(struct class_device *cdev, const char *buf,
			    size_t count)
{
	struct scsi_target *starget = transport_class_to_starget(cdev);
	struct spi_transport_attrs *tp =
		(struct spi_transport_attrs *)&starget->starget_data;

	return store_spi_transport_period_helper(cdev, buf, count,
						 &tp->min_period);
}


static CLASS_DEVICE_ATTR(min_period, S_IRUGO | S_IWUSR, 
			 show_spi_transport_min_period,
			 store_spi_transport_min_period);


static ssize_t show_spi_host_signalling(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	struct spi_internal *i = to_spi_internal(shost->transportt);

	if (i->f->get_signalling)
		i->f->get_signalling(shost);

	return sprintf(buf, "%s\n", spi_signal_to_string(spi_signalling(shost)));
}
static ssize_t store_spi_host_signalling(struct class_device *cdev,
					 const char *buf, size_t count)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	struct spi_internal *i = to_spi_internal(shost->transportt);
	enum spi_signal_type type = spi_signal_to_value(buf);

	if (type != SPI_SIGNAL_UNKNOWN)
		i->f->set_signalling(shost, type);

	return count;
}
static CLASS_DEVICE_ATTR(signalling, S_IRUGO | S_IWUSR,
			 show_spi_host_signalling,
			 store_spi_host_signalling);

#define DV_SET(x, y)			\
	if(i->f->set_##x)		\
		i->f->set_##x(sdev->sdev_target, y)

enum spi_compare_returns {
	SPI_COMPARE_SUCCESS,
	SPI_COMPARE_FAILURE,
	SPI_COMPARE_SKIP_TEST,
};


/* This is for read/write Domain Validation:  If the device supports
 * an echo buffer, we do read/write tests to it */
static enum spi_compare_returns
spi_dv_device_echo_buffer(struct scsi_device *sdev, u8 *buffer,
			  u8 *ptr, const int retries)
{
	int len = ptr - buffer;
	int j, k, r, result;
	unsigned int pattern = 0x0000ffff;
	struct scsi_sense_hdr sshdr;

	const char spi_write_buffer[] = {
		WRITE_BUFFER, 0x0a, 0, 0, 0, 0, 0, len >> 8, len & 0xff, 0
	};
	const char spi_read_buffer[] = {
		READ_BUFFER, 0x0a, 0, 0, 0, 0, 0, len >> 8, len & 0xff, 0
	};

	/* set up the pattern buffer.  Doesn't matter if we spill
	 * slightly beyond since that's where the read buffer is */
	for (j = 0; j < len; ) {

		/* fill the buffer with counting (test a) */
		for ( ; j < min(len, 32); j++)
			buffer[j] = j;
		k = j;
		/* fill the buffer with alternating words of 0x0 and
		 * 0xffff (test b) */
		for ( ; j < min(len, k + 32); j += 2) {
			u16 *word = (u16 *)&buffer[j];
			
			*word = (j & 0x02) ? 0x0000 : 0xffff;
		}
		k = j;
		/* fill with crosstalk (alternating 0x5555 0xaaa)
                 * (test c) */
		for ( ; j < min(len, k + 32); j += 2) {
			u16 *word = (u16 *)&buffer[j];

			*word = (j & 0x02) ? 0x5555 : 0xaaaa;
		}
		k = j;
		/* fill with shifting bits (test d) */
		for ( ; j < min(len, k + 32); j += 4) {
			u32 *word = (unsigned int *)&buffer[j];
			u32 roll = (pattern & 0x80000000) ? 1 : 0;
			
			*word = pattern;
			pattern = (pattern << 1) | roll;
		}
		/* don't bother with random data (test e) */
	}

	for (r = 0; r < retries; r++) {
		result = spi_execute(sdev, spi_write_buffer, DMA_TO_DEVICE,
				     buffer, len, &sshdr);
		if(result || !scsi_device_online(sdev)) {

			scsi_device_set_state(sdev, SDEV_QUIESCE);
			if (scsi_sense_valid(&sshdr)
			    && sshdr.sense_key == ILLEGAL_REQUEST
			    /* INVALID FIELD IN CDB */
			    && sshdr.asc == 0x24 && sshdr.ascq == 0x00)
				/* This would mean that the drive lied
				 * to us about supporting an echo
				 * buffer (unfortunately some Western
				 * Digital drives do precisely this)
				 */
				return SPI_COMPARE_SKIP_TEST;


			sdev_printk(KERN_ERR, sdev, "Write Buffer failure %x\n", result);
			return SPI_COMPARE_FAILURE;
		}

		memset(ptr, 0, len);
		spi_execute(sdev, spi_read_buffer, DMA_FROM_DEVICE,
			    ptr, len, NULL);
		scsi_device_set_state(sdev, SDEV_QUIESCE);

		if (memcmp(buffer, ptr, len) != 0)
			return SPI_COMPARE_FAILURE;
	}
	return SPI_COMPARE_SUCCESS;
}

/* This is for the simplest form of Domain Validation: a read test
 * on the inquiry data from the device */
static enum spi_compare_returns
spi_dv_device_compare_inquiry(struct scsi_device *sdev, u8 *buffer,
			      u8 *ptr, const int retries)
{
	int r, result;
	const int len = sdev->inquiry_len;
	const char spi_inquiry[] = {
		INQUIRY, 0, 0, 0, len, 0
	};

	for (r = 0; r < retries; r++) {
		memset(ptr, 0, len);

		result = spi_execute(sdev, spi_inquiry, DMA_FROM_DEVICE,
				     ptr, len, NULL);
		
		if(result || !scsi_device_online(sdev)) {
			scsi_device_set_state(sdev, SDEV_QUIESCE);
			return SPI_COMPARE_FAILURE;
		}

		/* If we don't have the inquiry data already, the
		 * first read gets it */
		if (ptr == buffer) {
			ptr += len;
			--r;
			continue;
		}

		if (memcmp(buffer, ptr, len) != 0)
			/* failure */
			return SPI_COMPARE_FAILURE;
	}
	return SPI_COMPARE_SUCCESS;
}

static enum spi_compare_returns
spi_dv_retrain(struct scsi_device *sdev, u8 *buffer, u8 *ptr,
	       enum spi_compare_returns 
	       (*compare_fn)(struct scsi_device *, u8 *, u8 *, int))
{
	struct spi_internal *i = to_spi_internal(sdev->host->transportt);
	struct scsi_target *starget = sdev->sdev_target;
	int period = 0, prevperiod = 0; 
	enum spi_compare_returns retval;


	for (;;) {
		int newperiod;
		retval = compare_fn(sdev, buffer, ptr, DV_LOOPS);

		if (retval == SPI_COMPARE_SUCCESS
		    || retval == SPI_COMPARE_SKIP_TEST)
			break;

		/* OK, retrain, fallback */
		if (i->f->get_iu)
			i->f->get_iu(starget);
		if (i->f->get_qas)
			i->f->get_qas(starget);
		if (i->f->get_period)
			i->f->get_period(sdev->sdev_target);

		/* Here's the fallback sequence; first try turning off
		 * IU, then QAS (if we can control them), then finally
		 * fall down the periods */
		if (i->f->set_iu && spi_iu(starget)) {
			starget_printk(KERN_ERR, starget, "Domain Validation Disabing Information Units\n");
			DV_SET(iu, 0);
		} else if (i->f->set_qas && spi_qas(starget)) {
			starget_printk(KERN_ERR, starget, "Domain Validation Disabing Quick Arbitration and Selection\n");
			DV_SET(qas, 0);
		} else {
			newperiod = spi_period(starget);
			period = newperiod > period ? newperiod : period;
			if (period < 0x0d)
				period++;
			else
				period += period >> 1;

			if (unlikely(period > 0xff || period == prevperiod)) {
				/* Total failure; set to async and return */
				starget_printk(KERN_ERR, starget, "Domain Validation Failure, dropping back to Asynchronous\n");
				DV_SET(offset, 0);
				return SPI_COMPARE_FAILURE;
			}
			starget_printk(KERN_ERR, starget, "Domain Validation detected failure, dropping back\n");
			DV_SET(period, period);
			prevperiod = period;
		}
	}
	return retval;
}

static int
spi_dv_device_get_echo_buffer(struct scsi_device *sdev, u8 *buffer)
{
	int l, result;

	/* first off do a test unit ready.  This can error out 
	 * because of reservations or some other reason.  If it
	 * fails, the device won't let us write to the echo buffer
	 * so just return failure */
	
	const char spi_test_unit_ready[] = {
		TEST_UNIT_READY, 0, 0, 0, 0, 0
	};

	const char spi_read_buffer_descriptor[] = {
		READ_BUFFER, 0x0b, 0, 0, 0, 0, 0, 0, 4, 0
	};

	
	/* We send a set of three TURs to clear any outstanding 
	 * unit attention conditions if they exist (Otherwise the
	 * buffer tests won't be happy).  If the TUR still fails
	 * (reservation conflict, device not ready, etc) just
	 * skip the write tests */
	for (l = 0; ; l++) {
		result = spi_execute(sdev, spi_test_unit_ready, DMA_NONE, 
				     NULL, 0, NULL);

		if(result) {
			if(l >= 3)
				return 0;
		} else {
			/* TUR succeeded */
			break;
		}
	}

	result = spi_execute(sdev, spi_read_buffer_descriptor, 
			     DMA_FROM_DEVICE, buffer, 4, NULL);

	if (result)
		/* Device has no echo buffer */
		return 0;

	return buffer[3] + ((buffer[2] & 0x1f) << 8);
}

static void
spi_dv_device_internal(struct scsi_device *sdev, u8 *buffer)
{
	struct spi_internal *i = to_spi_internal(sdev->host->transportt);
	struct scsi_target *starget = sdev->sdev_target;
	int len = sdev->inquiry_len;
	/* first set us up for narrow async */
	DV_SET(offset, 0);
	DV_SET(width, 0);
	
	if (spi_dv_device_compare_inquiry(sdev, buffer, buffer, DV_LOOPS)
	    != SPI_COMPARE_SUCCESS) {
		starget_printk(KERN_ERR, starget, "Domain Validation Initial Inquiry Failed\n");
		/* FIXME: should probably offline the device here? */
		return;
	}

	/* test width */
	if (i->f->set_width && spi_max_width(starget) &&
	    scsi_device_wide(sdev)) {
		i->f->set_width(starget, 1);

		if (spi_dv_device_compare_inquiry(sdev, buffer,
						   buffer + len,
						   DV_LOOPS)
		    != SPI_COMPARE_SUCCESS) {
			starget_printk(KERN_ERR, starget, "Wide Transfers Fail\n");
			i->f->set_width(starget, 0);
		}
	}

	if (!i->f->set_period)
		return;

	/* device can't handle synchronous */
	if (!scsi_device_sync(sdev) && !scsi_device_dt(sdev))
		return;

	/* see if the device has an echo buffer.  If it does we can
	 * do the SPI pattern write tests */

	len = 0;
	if (scsi_device_dt(sdev))
		len = spi_dv_device_get_echo_buffer(sdev, buffer);

 retry:

	/* now set up to the maximum */
	DV_SET(offset, spi_max_offset(starget));
	DV_SET(period, spi_min_period(starget));
	/* try QAS requests; this should be harmless to set if the
	 * target supports it */
	if (scsi_device_qas(sdev))
		DV_SET(qas, 1);
	/* Also try IU transfers */
	if (scsi_device_ius(sdev))
		DV_SET(iu, 1);
	if (spi_min_period(starget) < 9) {
		/* This u320 (or u640). Ignore the coupled parameters
		 * like DT and IU, but set the optional ones */
		DV_SET(rd_strm, 1);
		DV_SET(wr_flow, 1);
		DV_SET(rti, 1);
		if (spi_min_period(starget) == 8)
			DV_SET(pcomp_en, 1);
	}

	if (len == 0) {
		starget_printk(KERN_INFO, starget, "Domain Validation skipping write tests\n");
		spi_dv_retrain(sdev, buffer, buffer + len,
			       spi_dv_device_compare_inquiry);
		return;
	}

	if (len > SPI_MAX_ECHO_BUFFER_SIZE) {
		starget_printk(KERN_WARNING, starget, "Echo buffer size %d is too big, trimming to %d\n", len, SPI_MAX_ECHO_BUFFER_SIZE);
		len = SPI_MAX_ECHO_BUFFER_SIZE;
	}

	if (spi_dv_retrain(sdev, buffer, buffer + len,
			   spi_dv_device_echo_buffer)
	    == SPI_COMPARE_SKIP_TEST) {
		/* OK, the stupid drive can't do a write echo buffer
		 * test after all, fall back to the read tests */
		len = 0;
		goto retry;
	}
}


/**	spi_dv_device - Do Domain Validation on the device
 *	@sdev:		scsi device to validate
 *
 *	Performs the domain validation on the given device in the
 *	current execution thread.  Since DV operations may sleep,
 *	the current thread must have user context.  Also no SCSI
 *	related locks that would deadlock I/O issued by the DV may
 *	be held.
 */
void
spi_dv_device(struct scsi_device *sdev)
{
	struct scsi_target *starget = sdev->sdev_target;
	u8 *buffer;
	const int len = SPI_MAX_ECHO_BUFFER_SIZE*2;

	if (unlikely(scsi_device_get(sdev)))
		return;

	buffer = kmalloc(len, GFP_KERNEL);

	if (unlikely(!buffer))
		goto out_put;

	memset(buffer, 0, len);

	/* We need to verify that the actual device will quiesce; the
	 * later target quiesce is just a nice to have */
	if (unlikely(scsi_device_quiesce(sdev)))
		goto out_free;

	scsi_target_quiesce(starget);

	spi_dv_pending(starget) = 1;
	down(&spi_dv_sem(starget));

	starget_printk(KERN_INFO, starget, "Beginning Domain Validation\n");

	spi_dv_device_internal(sdev, buffer);

	starget_printk(KERN_INFO, starget, "Ending Domain Validation\n");

	up(&spi_dv_sem(starget));
	spi_dv_pending(starget) = 0;

	scsi_target_resume(starget);

	spi_initial_dv(starget) = 1;

 out_free:
	kfree(buffer);
 out_put:
	scsi_device_put(sdev);
}
EXPORT_SYMBOL(spi_dv_device);

struct work_queue_wrapper {
	struct work_struct	work;
	struct scsi_device	*sdev;
};

static void
spi_dv_device_work_wrapper(void *data)
{
	struct work_queue_wrapper *wqw = (struct work_queue_wrapper *)data;
	struct scsi_device *sdev = wqw->sdev;

	kfree(wqw);
	spi_dv_device(sdev);
	spi_dv_pending(sdev->sdev_target) = 0;
	scsi_device_put(sdev);
}


/**
 *	spi_schedule_dv_device - schedule domain validation to occur on the device
 *	@sdev:	The device to validate
 *
 *	Identical to spi_dv_device() above, except that the DV will be
 *	scheduled to occur in a workqueue later.  All memory allocations
 *	are atomic, so may be called from any context including those holding
 *	SCSI locks.
 */
void
spi_schedule_dv_device(struct scsi_device *sdev)
{
	struct work_queue_wrapper *wqw =
		kmalloc(sizeof(struct work_queue_wrapper), GFP_ATOMIC);

	if (unlikely(!wqw))
		return;

	if (unlikely(spi_dv_pending(sdev->sdev_target))) {
		kfree(wqw);
		return;
	}
	/* Set pending early (dv_device doesn't check it, only sets it) */
	spi_dv_pending(sdev->sdev_target) = 1;
	if (unlikely(scsi_device_get(sdev))) {
		kfree(wqw);
		spi_dv_pending(sdev->sdev_target) = 0;
		return;
	}

	INIT_WORK(&wqw->work, spi_dv_device_work_wrapper, wqw);
	wqw->sdev = sdev;

	schedule_work(&wqw->work);
}
EXPORT_SYMBOL(spi_schedule_dv_device);

/**
 * spi_display_xfer_agreement - Print the current target transfer agreement
 * @starget: The target for which to display the agreement
 *
 * Each SPI port is required to maintain a transfer agreement for each
 * other port on the bus.  This function prints a one-line summary of
 * the current agreement; more detailed information is available in sysfs.
 */
void spi_display_xfer_agreement(struct scsi_target *starget)
{
	struct spi_transport_attrs *tp;
	tp = (struct spi_transport_attrs *)&starget->starget_data;

	if (tp->offset > 0 && tp->period > 0) {
		unsigned int picosec, kb100;
		char *scsi = "FAST-?";
		char tmp[8];

		if (tp->period <= SPI_STATIC_PPR) {
			picosec = ppr_to_ps[tp->period];
			switch (tp->period) {
				case  7: scsi = "FAST-320"; break;
				case  8: scsi = "FAST-160"; break;
				case  9: scsi = "FAST-80"; break;
				case 10:
				case 11: scsi = "FAST-40"; break;
				case 12: scsi = "FAST-20"; break;
			}
		} else {
			picosec = tp->period * 4000;
			if (tp->period < 25)
				scsi = "FAST-20";
			else if (tp->period < 50)
				scsi = "FAST-10";
			else
				scsi = "FAST-5";
		}

		kb100 = (10000000 + picosec / 2) / picosec;
		if (tp->width)
			kb100 *= 2;
		sprint_frac(tmp, picosec, 1000);

		dev_info(&starget->dev,
			 "%s %sSCSI %d.%d MB/s %s%s%s%s%s%s%s%s (%s ns, offset %d)\n",
			 scsi, tp->width ? "WIDE " : "", kb100/10, kb100 % 10,
			 tp->dt ? "DT" : "ST",
			 tp->iu ? " IU" : "",
			 tp->qas  ? " QAS" : "",
			 tp->rd_strm ? " RDSTRM" : "",
			 tp->rti ? " RTI" : "",
			 tp->wr_flow ? " WRFLOW" : "",
			 tp->pcomp_en ? " PCOMP" : "",
			 tp->hold_mcs ? " HMCS" : "",
			 tmp, tp->offset);
	} else {
		dev_info(&starget->dev, "%sasynchronous.\n",
				tp->width ? "wide " : "");
	}
}
EXPORT_SYMBOL(spi_display_xfer_agreement);

#define SETUP_ATTRIBUTE(field)						\
	i->private_attrs[count] = class_device_attr_##field;		\
	if (!i->f->set_##field) {					\
		i->private_attrs[count].attr.mode = S_IRUGO;		\
		i->private_attrs[count].store = NULL;			\
	}								\
	i->attrs[count] = &i->private_attrs[count];			\
	if (i->f->show_##field)						\
		count++

#define SETUP_RELATED_ATTRIBUTE(field, rel_field)			\
	i->private_attrs[count] = class_device_attr_##field;		\
	if (!i->f->set_##rel_field) {					\
		i->private_attrs[count].attr.mode = S_IRUGO;		\
		i->private_attrs[count].store = NULL;			\
	}								\
	i->attrs[count] = &i->private_attrs[count];			\
	if (i->f->show_##rel_field)					\
		count++

#define SETUP_HOST_ATTRIBUTE(field)					\
	i->private_host_attrs[count] = class_device_attr_##field;	\
	if (!i->f->set_##field) {					\
		i->private_host_attrs[count].attr.mode = S_IRUGO;	\
		i->private_host_attrs[count].store = NULL;		\
	}								\
	i->host_attrs[count] = &i->private_host_attrs[count];		\
	count++

static int spi_device_match(struct attribute_container *cont,
			    struct device *dev)
{
	struct scsi_device *sdev;
	struct Scsi_Host *shost;
	struct spi_internal *i;

	if (!scsi_is_sdev_device(dev))
		return 0;

	sdev = to_scsi_device(dev);
	shost = sdev->host;
	if (!shost->transportt  || shost->transportt->host_attrs.ac.class
	    != &spi_host_class.class)
		return 0;
	/* Note: this class has no device attributes, so it has
	 * no per-HBA allocation and thus we don't need to distinguish
	 * the attribute containers for the device */
	i = to_spi_internal(shost->transportt);
	if (i->f->deny_binding && i->f->deny_binding(sdev->sdev_target))
		return 0;
	return 1;
}

static int spi_target_match(struct attribute_container *cont,
			    struct device *dev)
{
	struct Scsi_Host *shost;
	struct scsi_target *starget;
	struct spi_internal *i;

	if (!scsi_is_target_device(dev))
		return 0;

	shost = dev_to_shost(dev->parent);
	if (!shost->transportt  || shost->transportt->host_attrs.ac.class
	    != &spi_host_class.class)
		return 0;

	i = to_spi_internal(shost->transportt);
	starget = to_scsi_target(dev);

	if (i->f->deny_binding && i->f->deny_binding(starget))
		return 0;

	return &i->t.target_attrs.ac == cont;
}

static DECLARE_TRANSPORT_CLASS(spi_transport_class,
			       "spi_transport",
			       spi_setup_transport_attrs,
			       NULL,
			       NULL);

static DECLARE_ANON_TRANSPORT_CLASS(spi_device_class,
				    spi_device_match,
				    spi_device_configure);

struct scsi_transport_template *
spi_attach_transport(struct spi_function_template *ft)
{
	struct spi_internal *i = kmalloc(sizeof(struct spi_internal),
					 GFP_KERNEL);
	int count = 0;
	if (unlikely(!i))
		return NULL;

	memset(i, 0, sizeof(struct spi_internal));


	i->t.target_attrs.ac.class = &spi_transport_class.class;
	i->t.target_attrs.ac.attrs = &i->attrs[0];
	i->t.target_attrs.ac.match = spi_target_match;
	transport_container_register(&i->t.target_attrs);
	i->t.target_size = sizeof(struct spi_transport_attrs);
	i->t.host_attrs.ac.class = &spi_host_class.class;
	i->t.host_attrs.ac.attrs = &i->host_attrs[0];
	i->t.host_attrs.ac.match = spi_host_match;
	transport_container_register(&i->t.host_attrs);
	i->t.host_size = sizeof(struct spi_host_attrs);
	i->f = ft;

	SETUP_ATTRIBUTE(period);
	SETUP_RELATED_ATTRIBUTE(min_period, period);
	SETUP_ATTRIBUTE(offset);
	SETUP_RELATED_ATTRIBUTE(max_offset, offset);
	SETUP_ATTRIBUTE(width);
	SETUP_RELATED_ATTRIBUTE(max_width, width);
	SETUP_ATTRIBUTE(iu);
	SETUP_ATTRIBUTE(dt);
	SETUP_ATTRIBUTE(qas);
	SETUP_ATTRIBUTE(wr_flow);
	SETUP_ATTRIBUTE(rd_strm);
	SETUP_ATTRIBUTE(rti);
	SETUP_ATTRIBUTE(pcomp_en);
	SETUP_ATTRIBUTE(hold_mcs);

	/* if you add an attribute but forget to increase SPI_NUM_ATTRS
	 * this bug will trigger */
	BUG_ON(count > SPI_NUM_ATTRS);

	i->attrs[count++] = &class_device_attr_revalidate;

	i->attrs[count] = NULL;

	count = 0;
	SETUP_HOST_ATTRIBUTE(signalling);

	BUG_ON(count > SPI_HOST_ATTRS);

	i->host_attrs[count] = NULL;

	return &i->t;
}
EXPORT_SYMBOL(spi_attach_transport);

void spi_release_transport(struct scsi_transport_template *t)
{
	struct spi_internal *i = to_spi_internal(t);

	transport_container_unregister(&i->t.target_attrs);
	transport_container_unregister(&i->t.host_attrs);

	kfree(i);
}
EXPORT_SYMBOL(spi_release_transport);

static __init int spi_transport_init(void)
{
	int error = transport_class_register(&spi_transport_class);
	if (error)
		return error;
	error = anon_transport_class_register(&spi_device_class);
	return transport_class_register(&spi_host_class);
}

static void __exit spi_transport_exit(void)
{
	transport_class_unregister(&spi_transport_class);
	anon_transport_class_unregister(&spi_device_class);
	transport_class_unregister(&spi_host_class);
}

MODULE_AUTHOR("Martin Hicks");
MODULE_DESCRIPTION("SPI Transport Attributes");
MODULE_LICENSE("GPL");

module_init(spi_transport_init);
module_exit(spi_transport_exit);
