/*
 * Debug Store support
 *
 * This provides a low-level interface to the hardware's Debug Store
 * feature that is used for last branch recording (LBR) and
 * precise-event based sampling (PEBS).
 *
 * Different architectures use a different DS layout/pointer size.
 * The below functions therefore work on a void*.
 *
 *
 * Since there is no user for PEBS, yet, only LBR (or branch
 * trace store, BTS) is supported.
 *
 *
 * Copyright (C) 2007 Intel Corporation.
 * Markus Metzger <markus.t.metzger@intel.com>, Dec 2007
 */

#include <asm/ds.h>

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>


/*
 * Debug Store (DS) save area configuration (see Intel64 and IA32
 * Architectures Software Developer's Manual, section 18.5)
 *
 * The DS configuration consists of the following fields; different
 * architetures vary in the size of those fields.
 * - double-word aligned base linear address of the BTS buffer
 * - write pointer into the BTS buffer
 * - end linear address of the BTS buffer (one byte beyond the end of
 *   the buffer)
 * - interrupt pointer into BTS buffer
 *   (interrupt occurs when write pointer passes interrupt pointer)
 * - double-word aligned base linear address of the PEBS buffer
 * - write pointer into the PEBS buffer
 * - end linear address of the PEBS buffer (one byte beyond the end of
 *   the buffer)
 * - interrupt pointer into PEBS buffer
 *   (interrupt occurs when write pointer passes interrupt pointer)
 * - value to which counter is reset following counter overflow
 *
 * On later architectures, the last branch recording hardware uses
 * 64bit pointers even in 32bit mode.
 *
 *
 * Branch Trace Store (BTS) records store information about control
 * flow changes. They at least provide the following information:
 * - source linear address
 * - destination linear address
 *
 * Netburst supported a predicated bit that had been dropped in later
 * architectures. We do not suppor it.
 *
 *
 * In order to abstract from the actual DS and BTS layout, we describe
 * the access to the relevant fields.
 * Thanks to Andi Kleen for proposing this design.
 *
 * The implementation, however, is not as general as it might seem. In
 * order to stay somewhat simple and efficient, we assume an
 * underlying unsigned type (mostly a pointer type) and we expect the
 * field to be at least as big as that type.
 */

/*
 * A special from_ip address to indicate that the BTS record is an
 * info record that needs to be interpreted or skipped.
 */
#define BTS_ESCAPE_ADDRESS (-1)

/*
 * A field access descriptor
 */
struct access_desc {
	unsigned char offset;
	unsigned char size;
};

/*
 * The configuration for a particular DS/BTS hardware implementation.
 */
struct ds_configuration {
	/* the DS configuration */
	unsigned char  sizeof_ds;
	struct access_desc bts_buffer_base;
	struct access_desc bts_index;
	struct access_desc bts_absolute_maximum;
	struct access_desc bts_interrupt_threshold;
	/* the BTS configuration */
	unsigned char  sizeof_bts;
	struct access_desc from_ip;
	struct access_desc to_ip;
	/* BTS variants used to store additional information like
	   timestamps */
	struct access_desc info_type;
	struct access_desc info_data;
	unsigned long debugctl_mask;
};

/*
 * The global configuration used by the below accessor functions
 */
static struct ds_configuration ds_cfg;

/*
 * Accessor functions for some DS and BTS fields using the above
 * global ptrace_bts_cfg.
 */
static inline unsigned long get_bts_buffer_base(char *base)
{
	return *(unsigned long *)(base + ds_cfg.bts_buffer_base.offset);
}
static inline void set_bts_buffer_base(char *base, unsigned long value)
{
	(*(unsigned long *)(base + ds_cfg.bts_buffer_base.offset)) = value;
}
static inline unsigned long get_bts_index(char *base)
{
	return *(unsigned long *)(base + ds_cfg.bts_index.offset);
}
static inline void set_bts_index(char *base, unsigned long value)
{
	(*(unsigned long *)(base + ds_cfg.bts_index.offset)) = value;
}
static inline unsigned long get_bts_absolute_maximum(char *base)
{
	return *(unsigned long *)(base + ds_cfg.bts_absolute_maximum.offset);
}
static inline void set_bts_absolute_maximum(char *base, unsigned long value)
{
	(*(unsigned long *)(base + ds_cfg.bts_absolute_maximum.offset)) = value;
}
static inline unsigned long get_bts_interrupt_threshold(char *base)
{
	return *(unsigned long *)(base + ds_cfg.bts_interrupt_threshold.offset);
}
static inline void set_bts_interrupt_threshold(char *base, unsigned long value)
{
	(*(unsigned long *)(base + ds_cfg.bts_interrupt_threshold.offset)) = value;
}
static inline unsigned long get_from_ip(char *base)
{
	return *(unsigned long *)(base + ds_cfg.from_ip.offset);
}
static inline void set_from_ip(char *base, unsigned long value)
{
	(*(unsigned long *)(base + ds_cfg.from_ip.offset)) = value;
}
static inline unsigned long get_to_ip(char *base)
{
	return *(unsigned long *)(base + ds_cfg.to_ip.offset);
}
static inline void set_to_ip(char *base, unsigned long value)
{
	(*(unsigned long *)(base + ds_cfg.to_ip.offset)) = value;
}
static inline unsigned char get_info_type(char *base)
{
	return *(unsigned char *)(base + ds_cfg.info_type.offset);
}
static inline void set_info_type(char *base, unsigned char value)
{
	(*(unsigned char *)(base + ds_cfg.info_type.offset)) = value;
}
static inline unsigned long get_info_data(char *base)
{
	return *(unsigned long *)(base + ds_cfg.info_data.offset);
}
static inline void set_info_data(char *base, unsigned long value)
{
	(*(unsigned long *)(base + ds_cfg.info_data.offset)) = value;
}


int ds_allocate(void **dsp, size_t bts_size_in_bytes)
{
	size_t bts_size_in_records;
	unsigned long bts;
	void *ds;

	if (!ds_cfg.sizeof_ds || !ds_cfg.sizeof_bts)
		return -EOPNOTSUPP;

	if (bts_size_in_bytes < 0)
		return -EINVAL;

	bts_size_in_records =
		bts_size_in_bytes / ds_cfg.sizeof_bts;
	bts_size_in_bytes =
		bts_size_in_records * ds_cfg.sizeof_bts;

	if (bts_size_in_bytes <= 0)
		return -EINVAL;

	bts = (unsigned long)kzalloc(bts_size_in_bytes, GFP_KERNEL);

	if (!bts)
		return -ENOMEM;

	ds = kzalloc(ds_cfg.sizeof_ds, GFP_KERNEL);

	if (!ds) {
		kfree((void *)bts);
		return -ENOMEM;
	}

	set_bts_buffer_base(ds, bts);
	set_bts_index(ds, bts);
	set_bts_absolute_maximum(ds, bts + bts_size_in_bytes);
	set_bts_interrupt_threshold(ds, bts + bts_size_in_bytes + 1);

	*dsp = ds;
	return 0;
}

int ds_free(void **dsp)
{
	if (*dsp) {
		kfree((void *)get_bts_buffer_base(*dsp));
		kfree(*dsp);
		*dsp = NULL;
	}
	return 0;
}

int ds_get_bts_size(void *ds)
{
	int size_in_bytes;

	if (!ds_cfg.sizeof_ds || !ds_cfg.sizeof_bts)
		return -EOPNOTSUPP;

	if (!ds)
		return 0;

	size_in_bytes =
		get_bts_absolute_maximum(ds) -
		get_bts_buffer_base(ds);
	return size_in_bytes;
}

int ds_get_bts_end(void *ds)
{
	int size_in_bytes = ds_get_bts_size(ds);

	if (size_in_bytes <= 0)
		return size_in_bytes;

	return size_in_bytes / ds_cfg.sizeof_bts;
}

int ds_get_bts_index(void *ds)
{
	int index_offset_in_bytes;

	if (!ds_cfg.sizeof_ds || !ds_cfg.sizeof_bts)
		return -EOPNOTSUPP;

	index_offset_in_bytes =
		get_bts_index(ds) -
		get_bts_buffer_base(ds);

	return index_offset_in_bytes / ds_cfg.sizeof_bts;
}

int ds_set_overflow(void *ds, int method)
{
	switch (method) {
	case DS_O_SIGNAL:
		return -EOPNOTSUPP;
	case DS_O_WRAP:
		return 0;
	default:
		return -EINVAL;
	}
}

int ds_get_overflow(void *ds)
{
	return DS_O_WRAP;
}

int ds_clear(void *ds)
{
	int bts_size = ds_get_bts_size(ds);
	unsigned long bts_base;

	if (bts_size <= 0)
		return bts_size;

	bts_base = get_bts_buffer_base(ds);
	memset((void *)bts_base, 0, bts_size);

	set_bts_index(ds, bts_base);
	return 0;
}

int ds_read_bts(void *ds, int index, struct bts_struct *out)
{
	void *bts;

	if (!ds_cfg.sizeof_ds || !ds_cfg.sizeof_bts)
		return -EOPNOTSUPP;

	if (index < 0)
		return -EINVAL;

	if (index >= ds_get_bts_size(ds))
		return -EINVAL;

	bts = (void *)(get_bts_buffer_base(ds) + (index * ds_cfg.sizeof_bts));

	memset(out, 0, sizeof(*out));
	if (get_from_ip(bts) == BTS_ESCAPE_ADDRESS) {
		out->qualifier       = get_info_type(bts);
		out->variant.jiffies = get_info_data(bts);
	} else {
		out->qualifier = BTS_BRANCH;
		out->variant.lbr.from_ip = get_from_ip(bts);
		out->variant.lbr.to_ip   = get_to_ip(bts);
	}

	return sizeof(*out);;
}

int ds_write_bts(void *ds, const struct bts_struct *in)
{
	unsigned long bts;

	if (!ds_cfg.sizeof_ds || !ds_cfg.sizeof_bts)
		return -EOPNOTSUPP;

	if (ds_get_bts_size(ds) <= 0)
		return -ENXIO;

	bts = get_bts_index(ds);

	memset((void *)bts, 0, ds_cfg.sizeof_bts);
	switch (in->qualifier) {
	case BTS_INVALID:
		break;

	case BTS_BRANCH:
		set_from_ip((void *)bts, in->variant.lbr.from_ip);
		set_to_ip((void *)bts, in->variant.lbr.to_ip);
		break;

	case BTS_TASK_ARRIVES:
	case BTS_TASK_DEPARTS:
		set_from_ip((void *)bts, BTS_ESCAPE_ADDRESS);
		set_info_type((void *)bts, in->qualifier);
		set_info_data((void *)bts, in->variant.jiffies);
		break;

	default:
		return -EINVAL;
	}

	bts = bts + ds_cfg.sizeof_bts;
	if (bts >= get_bts_absolute_maximum(ds))
		bts = get_bts_buffer_base(ds);
	set_bts_index(ds, bts);

	return ds_cfg.sizeof_bts;
}

unsigned long ds_debugctl_mask(void)
{
	return ds_cfg.debugctl_mask;
}

#ifdef __i386__
static const struct ds_configuration ds_cfg_netburst = {
	.sizeof_ds = 9 * 4,
	.bts_buffer_base = { 0, 4 },
	.bts_index = { 4, 4 },
	.bts_absolute_maximum = { 8, 4 },
	.bts_interrupt_threshold = { 12, 4 },
	.sizeof_bts = 3 * 4,
	.from_ip = { 0, 4 },
	.to_ip = { 4, 4 },
	.info_type = { 4, 1 },
	.info_data = { 8, 4 },
	.debugctl_mask = (1<<2)|(1<<3)
};

static const struct ds_configuration ds_cfg_pentium_m = {
	.sizeof_ds = 9 * 4,
	.bts_buffer_base = { 0, 4 },
	.bts_index = { 4, 4 },
	.bts_absolute_maximum = { 8, 4 },
	.bts_interrupt_threshold = { 12, 4 },
	.sizeof_bts = 3 * 4,
	.from_ip = { 0, 4 },
	.to_ip = { 4, 4 },
	.info_type = { 4, 1 },
	.info_data = { 8, 4 },
	.debugctl_mask = (1<<6)|(1<<7)
};
#endif /* _i386_ */

static const struct ds_configuration ds_cfg_core2 = {
	.sizeof_ds = 9 * 8,
	.bts_buffer_base = { 0, 8 },
	.bts_index = { 8, 8 },
	.bts_absolute_maximum = { 16, 8 },
	.bts_interrupt_threshold = { 24, 8 },
	.sizeof_bts = 3 * 8,
	.from_ip = { 0, 8 },
	.to_ip = { 8, 8 },
	.info_type = { 8, 1 },
	.info_data = { 16, 8 },
	.debugctl_mask = (1<<6)|(1<<7)|(1<<9)
};

static inline void
ds_configure(const struct ds_configuration *cfg)
{
	ds_cfg = *cfg;
}

void __cpuinit ds_init_intel(struct cpuinfo_x86 *c)
{
	switch (c->x86) {
	case 0x6:
		switch (c->x86_model) {
#ifdef __i386__
		case 0xD:
		case 0xE: /* Pentium M */
			ds_configure(&ds_cfg_pentium_m);
			break;
#endif /* _i386_ */
		case 0xF: /* Core2 */
			ds_configure(&ds_cfg_core2);
			break;
		default:
			/* sorry, don't know about them */
			break;
		}
		break;
	case 0xF:
		switch (c->x86_model) {
#ifdef __i386__
		case 0x0:
		case 0x1:
		case 0x2: /* Netburst */
			ds_configure(&ds_cfg_netburst);
			break;
#endif /* _i386_ */
		default:
			/* sorry, don't know about them */
			break;
		}
		break;
	default:
		/* sorry, don't know about them */
		break;
	}
}
