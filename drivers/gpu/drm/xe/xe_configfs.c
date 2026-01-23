// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/bitops.h>
#include <linux/ctype.h>
#include <linux/configfs.h>
#include <linux/cleanup.h>
#include <linux/find.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/string.h>

#include "instructions/xe_mi_commands.h"
#include "xe_configfs.h"
#include "xe_gt_types.h"
#include "xe_hw_engine_types.h"
#include "xe_module.h"
#include "xe_pci_types.h"
#include "xe_sriov_types.h"

/**
 * DOC: Xe Configfs
 *
 * Overview
 * ========
 *
 * Configfs is a filesystem-based manager of kernel objects. Xe KMD registers a
 * configfs subsystem called ``xe`` that creates a directory in the mounted
 * configfs directory. The user can create devices under this directory and
 * configure them as necessary. See Documentation/filesystems/configfs.rst for
 * more information about how configfs works.
 *
 * Create devices
 * ==============
 *
 * To create a device, the ``xe`` module should already be loaded, but some
 * attributes can only be set before binding the device. It can be accomplished
 * by blocking the driver autoprobe::
 *
 *	# echo 0 > /sys/bus/pci/drivers_autoprobe
 *	# modprobe xe
 *
 * In order to create a device, the user has to create a directory inside ``xe``::
 *
 *	# mkdir /sys/kernel/config/xe/0000:03:00.0/
 *
 * Every device created is populated by the driver with entries that can be
 * used to configure it::
 *
 *	/sys/kernel/config/xe/
 *	├── 0000:00:02.0
 *	│   └── ...
 *	├── 0000:00:02.1
 *	│   └── ...
 *	:
 *	└── 0000:03:00.0
 *	    ├── survivability_mode
 *	    ├── gt_types_allowed
 *	    ├── engines_allowed
 *	    └── enable_psmi
 *
 * After configuring the attributes as per next section, the device can be
 * probed with::
 *
 *	# echo 0000:03:00.0 > /sys/bus/pci/drivers/xe/bind
 *	# # or
 *	# echo 0000:03:00.0 > /sys/bus/pci/drivers_probe
 *
 * Configure Attributes
 * ====================
 *
 * Survivability mode:
 * -------------------
 *
 * Enable survivability mode on supported cards. This setting only takes
 * effect when probing the device. Example to enable it::
 *
 *	# echo 1 > /sys/kernel/config/xe/0000:03:00.0/survivability_mode
 *
 * This attribute can only be set before binding to the device.
 *
 * Allowed GT types:
 * -----------------
 *
 * Allow only specific types of GTs to be detected and initialized by the
 * driver.  Any combination of GT types can be enabled/disabled, although
 * some settings will cause the device to fail to probe.
 *
 * Writes support both comma- and newline-separated input format. Reads
 * will always return one GT type per line. "primary" and "media" are the
 * GT type names supported by this interface.
 *
 * This attribute can only be set before binding to the device.
 *
 * Examples:
 *
 * Allow both primary and media GTs to be initialized and used.  This matches
 * the driver's default behavior::
 *
 *	# echo 'primary,media' > /sys/kernel/config/xe/0000:03:00.0/gt_types_allowed
 *
 * Allow only the primary GT of each tile to be initialized and used,
 * effectively disabling the media GT if it exists on the platform::
 *
 *	# echo 'primary' > /sys/kernel/config/xe/0000:03:00.0/gt_types_allowed
 *
 * Allow only the media GT of each tile to be initialized and used,
 * effectively disabling the primary GT.  **This configuration will cause
 * device probe failure on all current platforms, but may be allowed on
 * igpu platforms in the future**::
 *
 *	# echo 'media' > /sys/kernel/config/xe/0000:03:00.0/gt_types_allowed
 *
 * Disable all GTs.  Only other GPU IP (such as display) is potentially usable.
 * **This configuration will cause device probe failure on all current
 * platforms, but may be allowed on igpu platforms in the future**::
 *
 *	# echo '' > /sys/kernel/config/xe/0000:03:00.0/gt_types_allowed
 *
 * Allowed engines:
 * ----------------
 *
 * Allow only a set of engine(s) to be available, disabling the other engines
 * even if they are available in hardware. This is applied after HW fuses are
 * considered on each tile. Examples:
 *
 * Allow only one render and one copy engines, nothing else::
 *
 *	# echo 'rcs0,bcs0' > /sys/kernel/config/xe/0000:03:00.0/engines_allowed
 *
 * Allow only compute engines and first copy engine::
 *
 *	# echo 'ccs*,bcs0' > /sys/kernel/config/xe/0000:03:00.0/engines_allowed
 *
 * Note that the engine names are the per-GT hardware names. On multi-tile
 * platforms, writing ``rcs0,bcs0`` to this file would allow the first render
 * and copy engines on each tile.
 *
 * The requested configuration may not be supported by the platform and driver
 * may fail to probe. For example: if at least one copy engine is expected to be
 * available for migrations, but it's disabled. This is intended for debugging
 * purposes only.
 *
 * This attribute can only be set before binding to the device.
 *
 * PSMI
 * ----
 *
 * Enable extra debugging capabilities to trace engine execution. Only useful
 * during early platform enabling and requires additional hardware connected.
 * Once it's enabled, additionals WAs are added and runtime configuration is
 * done via debugfs. Example to enable it::
 *
 *	# echo 1 > /sys/kernel/config/xe/0000:03:00.0/enable_psmi
 *
 * This attribute can only be set before binding to the device.
 *
 * Context restore BB
 * ------------------
 *
 * Allow to execute a batch buffer during any context switches. When the
 * GPU is restoring the context, it executes additional commands. It's useful
 * for testing additional workarounds and validating certain HW behaviors: it's
 * not intended for normal execution and will taint the kernel with TAINT_TEST
 * when used.
 *
 * The syntax allows to pass straight instructions to be executed by the engine
 * in a batch buffer or set specific registers.
 *
 * #. Generic instruction::
 *
 *	<engine-class> cmd <instr> [[dword0] [dword1] [...]]
 *
 * #. Simple register setting::
 *
 *	<engine-class> reg <address> <value>
 *
 * Commands are saved per engine class: all instances of that class will execute
 * those commands during context switch. The instruction, dword arguments,
 * addresses and values are in hex format like in the examples below.
 *
 * #. Execute a LRI command to write 0xDEADBEEF to register 0x4f10 after the
 *    normal context restore::
 *
 *	# echo 'rcs cmd 11000001 4F100 DEADBEEF' \
 *		> /sys/kernel/config/xe/0000:03:00.0/ctx_restore_post_bb
 *
 * #. Execute a LRI command to write 0xDEADBEEF to register 0x4f10 at the
 *    beginning of the context restore::
 *
 *	# echo 'rcs cmd 11000001 4F100 DEADBEEF' \
 *		> /sys/kernel/config/xe/0000:03:00.0/ctx_restore_mid_bb

 * #. Load certain values in a couple of registers (it can be used as a simpler
 *    alternative to the `cmd`) action::
 *
 *	# cat > /sys/kernel/config/xe/0000:03:00.0/ctx_restore_post_bb <<EOF
 *	rcs reg 4F100 DEADBEEF
 *	rcs reg 4F104 FFFFFFFF
 *	EOF
 *
 *    .. note::
 *
 *       When using multiple lines, make sure to use a command that is
 *       implemented with a single write syscall, like HEREDOC.
 *
 * Currently this is implemented only for post and mid context restore and
 * these attributes can only be set before binding to the device.
 *
 * Max SR-IOV Virtual Functions
 * ----------------------------
 *
 * This config allows to limit number of the Virtual Functions (VFs) that can
 * be managed by the Physical Function (PF) driver, where value 0 disables the
 * PF mode (no VFs).
 *
 * The default max_vfs config value is taken from the max_vfs modparam.
 *
 * How to enable PF with support with unlimited (up to HW limit) number of VFs::
 *
 *	# echo unlimited > /sys/kernel/config/xe/0000:00:02.0/sriov/max_vfs
 *	# echo 0000:00:02.0 > /sys/bus/pci/drivers/xe/bind
 *
 * How to enable PF with support up to 3 VFs::
 *
 *	# echo 3 > /sys/kernel/config/xe/0000:00:02.0/sriov/max_vfs
 *	# echo 0000:00:02.0 > /sys/bus/pci/drivers/xe/bind
 *
 * How to disable PF mode and always run as native::
 *
 *	# echo 0 > /sys/kernel/config/xe/0000:00:02.0/sriov/max_vfs
 *	# echo 0000:00:02.0 > /sys/bus/pci/drivers/xe/bind
 *
 * This setting only takes effect when probing the device.
 *
 * Remove devices
 * ==============
 *
 * The created device directories can be removed using ``rmdir``::
 *
 *	# rmdir /sys/kernel/config/xe/0000:03:00.0/
 */

/* Similar to struct xe_bb, but not tied to HW (yet) */
struct wa_bb {
	u32 *cs;
	u32 len; /* in dwords */
};

struct xe_config_group_device {
	struct config_group group;
	struct config_group sriov;

	struct xe_config_device {
		u64 gt_types_allowed;
		u64 engines_allowed;
		struct wa_bb ctx_restore_post_bb[XE_ENGINE_CLASS_MAX];
		struct wa_bb ctx_restore_mid_bb[XE_ENGINE_CLASS_MAX];
		bool survivability_mode;
		bool enable_psmi;
		struct {
			unsigned int max_vfs;
		} sriov;
	} config;

	/* protects attributes */
	struct mutex lock;
	/* matching descriptor */
	const struct xe_device_desc *desc;
	/* tentative SR-IOV mode */
	enum xe_sriov_mode mode;
};

static const struct xe_config_device device_defaults = {
	.gt_types_allowed = U64_MAX,
	.engines_allowed = U64_MAX,
	.survivability_mode = false,
	.enable_psmi = false,
	.sriov = {
		.max_vfs = UINT_MAX,
	},
};

static void set_device_defaults(struct xe_config_device *config)
{
	*config = device_defaults;
#ifdef CONFIG_PCI_IOV
	config->sriov.max_vfs = xe_modparam.max_vfs;
#endif
}

struct engine_info {
	const char *cls;
	u64 mask;
	enum xe_engine_class engine_class;
};

/* Some helpful macros to aid on the sizing of buffer allocation when parsing */
#define MAX_ENGINE_CLASS_CHARS 5
#define MAX_ENGINE_INSTANCE_CHARS 2

static const struct engine_info engine_info[] = {
	{ .cls = "rcs", .mask = XE_HW_ENGINE_RCS_MASK, .engine_class = XE_ENGINE_CLASS_RENDER },
	{ .cls = "bcs", .mask = XE_HW_ENGINE_BCS_MASK, .engine_class = XE_ENGINE_CLASS_COPY },
	{ .cls = "vcs", .mask = XE_HW_ENGINE_VCS_MASK, .engine_class = XE_ENGINE_CLASS_VIDEO_DECODE },
	{ .cls = "vecs", .mask = XE_HW_ENGINE_VECS_MASK, .engine_class = XE_ENGINE_CLASS_VIDEO_ENHANCE },
	{ .cls = "ccs", .mask = XE_HW_ENGINE_CCS_MASK, .engine_class = XE_ENGINE_CLASS_COMPUTE },
	{ .cls = "gsccs", .mask = XE_HW_ENGINE_GSCCS_MASK, .engine_class = XE_ENGINE_CLASS_OTHER },
};

static const struct {
	const char *name;
	enum xe_gt_type type;
} gt_types[] = {
	{ .name = "primary", .type = XE_GT_TYPE_MAIN },
	{ .name = "media", .type = XE_GT_TYPE_MEDIA },
};

static struct xe_config_group_device *to_xe_config_group_device(struct config_item *item)
{
	return container_of(to_config_group(item), struct xe_config_group_device, group);
}

static struct xe_config_device *to_xe_config_device(struct config_item *item)
{
	return &to_xe_config_group_device(item)->config;
}

static bool is_bound(struct xe_config_group_device *dev)
{
	unsigned int domain, bus, slot, function;
	struct pci_dev *pdev;
	const char *name;
	bool ret;

	lockdep_assert_held(&dev->lock);

	name = dev->group.cg_item.ci_name;
	if (sscanf(name, "%x:%x:%x.%x", &domain, &bus, &slot, &function) != 4)
		return false;

	pdev = pci_get_domain_bus_and_slot(domain, bus, PCI_DEVFN(slot, function));
	if (!pdev)
		return false;

	ret = pci_get_drvdata(pdev);
	if (ret)
		pci_dbg(pdev, "Already bound to driver\n");

	pci_dev_put(pdev);
	return ret;
}

static ssize_t survivability_mode_show(struct config_item *item, char *page)
{
	struct xe_config_device *dev = to_xe_config_device(item);

	return sprintf(page, "%d\n", dev->survivability_mode);
}

static ssize_t survivability_mode_store(struct config_item *item, const char *page, size_t len)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item);
	bool survivability_mode;
	int ret;

	ret = kstrtobool(page, &survivability_mode);
	if (ret)
		return ret;

	guard(mutex)(&dev->lock);
	if (is_bound(dev))
		return -EBUSY;

	dev->config.survivability_mode = survivability_mode;

	return len;
}

static ssize_t gt_types_allowed_show(struct config_item *item, char *page)
{
	struct xe_config_device *dev = to_xe_config_device(item);
	char *p = page;

	for (size_t i = 0; i < ARRAY_SIZE(gt_types); i++)
		if (dev->gt_types_allowed & BIT_ULL(gt_types[i].type))
			p += sprintf(p, "%s\n", gt_types[i].name);

	return p - page;
}

static ssize_t gt_types_allowed_store(struct config_item *item, const char *page,
				      size_t len)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item);
	char *buf __free(kfree) = kstrdup(page, GFP_KERNEL);
	char *p = buf;
	u64 typemask = 0;

	if (!buf)
		return -ENOMEM;

	while (p) {
		char *typename = strsep(&p, ",\n");
		bool matched = false;

		if (typename[0] == '\0')
			continue;

		for (size_t i = 0; i < ARRAY_SIZE(gt_types); i++) {
			if (strcmp(typename, gt_types[i].name) == 0) {
				typemask |= BIT(gt_types[i].type);
				matched = true;
				break;
			}
		}

		if (!matched)
			return -EINVAL;
	}

	guard(mutex)(&dev->lock);
	if (is_bound(dev))
		return -EBUSY;

	dev->config.gt_types_allowed = typemask;

	return len;
}

static ssize_t engines_allowed_show(struct config_item *item, char *page)
{
	struct xe_config_device *dev = to_xe_config_device(item);
	char *p = page;

	for (size_t i = 0; i < ARRAY_SIZE(engine_info); i++) {
		u64 mask = engine_info[i].mask;

		if ((dev->engines_allowed & mask) == mask) {
			p += sprintf(p, "%s*\n", engine_info[i].cls);
		} else if (mask & dev->engines_allowed) {
			u16 bit0 = __ffs64(mask), bit;

			mask &= dev->engines_allowed;

			for_each_set_bit(bit, (const unsigned long *)&mask, 64)
				p += sprintf(p, "%s%u\n", engine_info[i].cls,
					     bit - bit0);
		}
	}

	return p - page;
}

/*
 * Lookup engine_info. If @mask is not NULL, reduce the mask according to the
 * instance in @pattern.
 *
 * Examples of inputs:
 * - lookup_engine_info("rcs0", &mask): return "rcs" entry from @engine_info and
 *   mask == BIT_ULL(XE_HW_ENGINE_RCS0)
 * - lookup_engine_info("rcs*", &mask): return "rcs" entry from @engine_info and
 *   mask == XE_HW_ENGINE_RCS_MASK
 * - lookup_engine_info("rcs", NULL): return "rcs" entry from @engine_info
 */
static const struct engine_info *lookup_engine_info(const char *pattern, u64 *mask)
{
	for (size_t i = 0; i < ARRAY_SIZE(engine_info); i++) {
		u8 instance;
		u16 bit;

		if (!str_has_prefix(pattern, engine_info[i].cls))
			continue;

		pattern += strlen(engine_info[i].cls);
		if (!mask)
			return *pattern ? NULL : &engine_info[i];

		if (!strcmp(pattern, "*")) {
			*mask = engine_info[i].mask;
			return &engine_info[i];
		}

		if (kstrtou8(pattern, 10, &instance))
			return NULL;

		bit = __ffs64(engine_info[i].mask) + instance;
		if (bit >= fls64(engine_info[i].mask))
			return NULL;

		*mask = BIT_ULL(bit);
		return &engine_info[i];
	}

	return NULL;
}

static int parse_engine(const char *s, const char *end_chars, u64 *mask,
			const struct engine_info **pinfo)
{
	char buf[MAX_ENGINE_CLASS_CHARS + MAX_ENGINE_INSTANCE_CHARS + 1];
	const struct engine_info *info;
	size_t len;

	len = strcspn(s, end_chars);
	if (len >= sizeof(buf))
		return -EINVAL;

	memcpy(buf, s, len);
	buf[len] = '\0';

	info = lookup_engine_info(buf, mask);
	if (!info)
		return -ENOENT;

	if (pinfo)
		*pinfo = info;

	return len;
}

static ssize_t engines_allowed_store(struct config_item *item, const char *page,
				     size_t len)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item);
	ssize_t patternlen, p;
	u64 mask, val = 0;

	for (p = 0; p < len; p += patternlen + 1) {
		patternlen = parse_engine(page + p, ",\n", &mask, NULL);
		if (patternlen < 0)
			return -EINVAL;

		val |= mask;
	}

	guard(mutex)(&dev->lock);
	if (is_bound(dev))
		return -EBUSY;

	dev->config.engines_allowed = val;

	return len;
}

static ssize_t enable_psmi_show(struct config_item *item, char *page)
{
	struct xe_config_device *dev = to_xe_config_device(item);

	return sprintf(page, "%d\n", dev->enable_psmi);
}

static ssize_t enable_psmi_store(struct config_item *item, const char *page, size_t len)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item);
	bool val;
	int ret;

	ret = kstrtobool(page, &val);
	if (ret)
		return ret;

	guard(mutex)(&dev->lock);
	if (is_bound(dev))
		return -EBUSY;

	dev->config.enable_psmi = val;

	return len;
}

static bool wa_bb_read_advance(bool dereference, char **p,
			       const char *append, size_t len,
			       size_t *max_size)
{
	if (dereference) {
		if (len >= *max_size)
			return false;
		*max_size -= len;
		if (append)
			memcpy(*p, append, len);
	}

	*p += len;

	return true;
}

static ssize_t wa_bb_show(struct xe_config_group_device *dev,
			  struct wa_bb wa_bb[static XE_ENGINE_CLASS_MAX],
			  char *data, size_t sz)
{
	char *p = data;

	guard(mutex)(&dev->lock);

	for (size_t i = 0; i < ARRAY_SIZE(engine_info); i++) {
		enum xe_engine_class ec = engine_info[i].engine_class;
		size_t len;

		if (!wa_bb[ec].len)
			continue;

		len = snprintf(p, sz, "%s:", engine_info[i].cls);
		if (!wa_bb_read_advance(data, &p, NULL, len, &sz))
			return -ENOBUFS;

		for (size_t j = 0; j < wa_bb[ec].len; j++) {
			len = snprintf(p, sz, " %08x", wa_bb[ec].cs[j]);
			if (!wa_bb_read_advance(data, &p, NULL, len, &sz))
				return -ENOBUFS;
		}

		if (!wa_bb_read_advance(data, &p, "\n", 1, &sz))
			return -ENOBUFS;
	}

	if (!wa_bb_read_advance(data, &p, "", 1, &sz))
		return -ENOBUFS;

	/* Reserve one more to match check for '\0' */
	if (!data)
		p++;

	return p - data;
}

static ssize_t ctx_restore_mid_bb_show(struct config_item *item, char *page)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item);

	return wa_bb_show(dev, dev->config.ctx_restore_mid_bb, page, SZ_4K);
}

static ssize_t ctx_restore_post_bb_show(struct config_item *item, char *page)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item);

	return wa_bb_show(dev, dev->config.ctx_restore_post_bb, page, SZ_4K);
}

static void wa_bb_append(struct wa_bb *wa_bb, u32 val)
{
	if (wa_bb->cs)
		wa_bb->cs[wa_bb->len] = val;

	wa_bb->len++;
}

static ssize_t parse_hex(const char *line, u32 *pval)
{
	char numstr[12];
	const char *p;
	ssize_t numlen;

	p = line + strspn(line, " \t");
	if (!*p || *p == '\n')
		return 0;

	numlen = strcspn(p, " \t\n");
	if (!numlen || numlen >= sizeof(numstr) - 1)
		return -EINVAL;

	memcpy(numstr, p, numlen);
	numstr[numlen] = '\0';
	p += numlen;

	if (kstrtou32(numstr, 16, pval))
		return -EINVAL;

	return p - line;
}

/*
 * Parse lines with the format
 *
 *	<engine-class> cmd <u32> <u32...>
 *	<engine-class> reg <u32_addr> <u32_val>
 *
 * and optionally save them in @wa_bb[i].cs is non-NULL.
 *
 * Return the number of dwords parsed.
 */
static ssize_t parse_wa_bb_lines(const char *lines,
				 struct wa_bb wa_bb[static XE_ENGINE_CLASS_MAX])
{
	ssize_t dwords = 0, ret;
	const char *p;

	for (p = lines; *p; p++) {
		const struct engine_info *info = NULL;
		u32 val, val2;

		/* Also allow empty lines */
		p += strspn(p, " \t\n");
		if (!*p)
			break;

		ret = parse_engine(p, " \t\n", NULL, &info);
		if (ret < 0)
			return ret;

		p += ret;
		p += strspn(p, " \t");

		if (str_has_prefix(p, "cmd")) {
			for (p += strlen("cmd"); *p;) {
				ret = parse_hex(p, &val);
				if (ret < 0)
					return -EINVAL;
				if (!ret)
					break;

				p += ret;
				dwords++;
				wa_bb_append(&wa_bb[info->engine_class], val);
			}
		} else if (str_has_prefix(p, "reg")) {
			p += strlen("reg");
			ret = parse_hex(p, &val);
			if (ret <= 0)
				return -EINVAL;

			p += ret;
			ret = parse_hex(p, &val2);
			if (ret <= 0)
				return -EINVAL;

			p += ret;
			dwords += 3;
			wa_bb_append(&wa_bb[info->engine_class],
				     MI_LOAD_REGISTER_IMM | MI_LRI_NUM_REGS(1));
			wa_bb_append(&wa_bb[info->engine_class], val);
			wa_bb_append(&wa_bb[info->engine_class], val2);
		} else {
			return -EINVAL;
		}
	}

	return dwords;
}

static ssize_t wa_bb_store(struct wa_bb wa_bb[static XE_ENGINE_CLASS_MAX],
			   struct xe_config_group_device *dev,
			   const char *page, size_t len)
{
	/* tmp_wa_bb must match wa_bb's size */
	struct wa_bb tmp_wa_bb[XE_ENGINE_CLASS_MAX] = { };
	ssize_t count, class;
	u32 *tmp;

	/* 1. Count dwords - wa_bb[i].cs is NULL for all classes */
	count = parse_wa_bb_lines(page, tmp_wa_bb);
	if (count < 0)
		return count;

	guard(mutex)(&dev->lock);

	if (is_bound(dev))
		return -EBUSY;

	/*
	 * 2. Allocate a u32 array and set the pointers to the right positions
	 * according to the length of each class' wa_bb
	 */
	tmp = krealloc(wa_bb[0].cs, count * sizeof(u32), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (!count) {
		memset(wa_bb, 0, sizeof(tmp_wa_bb));
		return len;
	}

	for (class = 0, count = 0; class < XE_ENGINE_CLASS_MAX; ++class) {
		tmp_wa_bb[class].cs = tmp + count;
		count += tmp_wa_bb[class].len;
		tmp_wa_bb[class].len = 0;
	}

	/* 3. Parse wa_bb lines again, this time saving the values */
	count = parse_wa_bb_lines(page, tmp_wa_bb);
	if (count < 0)
		return count;

	memcpy(wa_bb, tmp_wa_bb, sizeof(tmp_wa_bb));

	return len;
}

static ssize_t ctx_restore_mid_bb_store(struct config_item *item,
					const char *data, size_t sz)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item);

	return wa_bb_store(dev->config.ctx_restore_mid_bb, dev, data, sz);
}

static ssize_t ctx_restore_post_bb_store(struct config_item *item,
					 const char *data, size_t sz)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item);

	return wa_bb_store(dev->config.ctx_restore_post_bb, dev, data, sz);
}

CONFIGFS_ATTR(, ctx_restore_mid_bb);
CONFIGFS_ATTR(, ctx_restore_post_bb);
CONFIGFS_ATTR(, enable_psmi);
CONFIGFS_ATTR(, engines_allowed);
CONFIGFS_ATTR(, gt_types_allowed);
CONFIGFS_ATTR(, survivability_mode);

static struct configfs_attribute *xe_config_device_attrs[] = {
	&attr_ctx_restore_mid_bb,
	&attr_ctx_restore_post_bb,
	&attr_enable_psmi,
	&attr_engines_allowed,
	&attr_gt_types_allowed,
	&attr_survivability_mode,
	NULL,
};

static void xe_config_device_release(struct config_item *item)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item);

	mutex_destroy(&dev->lock);

	kfree(dev->config.ctx_restore_post_bb[0].cs);
	kfree(dev);
}

static struct configfs_item_operations xe_config_device_ops = {
	.release	= xe_config_device_release,
};

static bool xe_config_device_is_visible(struct config_item *item,
					struct configfs_attribute *attr, int n)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item);

	if (attr == &attr_survivability_mode) {
		if (!dev->desc->is_dgfx || dev->desc->platform < XE_BATTLEMAGE)
			return false;
	}

	return true;
}

static struct configfs_group_operations xe_config_device_group_ops = {
	.is_visible	= xe_config_device_is_visible,
};

static const struct config_item_type xe_config_device_type = {
	.ct_item_ops	= &xe_config_device_ops,
	.ct_group_ops	= &xe_config_device_group_ops,
	.ct_attrs	= xe_config_device_attrs,
	.ct_owner	= THIS_MODULE,
};

static ssize_t sriov_max_vfs_show(struct config_item *item, char *page)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item->ci_parent);

	guard(mutex)(&dev->lock);

	if (dev->config.sriov.max_vfs == UINT_MAX)
		return sprintf(page, "%s\n", "unlimited");
	else
		return sprintf(page, "%u\n", dev->config.sriov.max_vfs);
}

static ssize_t sriov_max_vfs_store(struct config_item *item, const char *page, size_t len)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item->ci_parent);
	unsigned int max_vfs;
	int ret;

	guard(mutex)(&dev->lock);

	if (is_bound(dev))
		return -EBUSY;

	ret = kstrtouint(page, 0, &max_vfs);
	if (ret) {
		if (!sysfs_streq(page, "unlimited"))
			return ret;
		max_vfs = UINT_MAX;
	}

	dev->config.sriov.max_vfs = max_vfs;
	return len;
}

CONFIGFS_ATTR(sriov_, max_vfs);

static struct configfs_attribute *xe_config_sriov_attrs[] = {
	&sriov_attr_max_vfs,
	NULL,
};

static bool xe_config_sriov_is_visible(struct config_item *item,
				       struct configfs_attribute *attr, int n)
{
	struct xe_config_group_device *dev = to_xe_config_group_device(item->ci_parent);

	if (attr == &sriov_attr_max_vfs && dev->mode != XE_SRIOV_MODE_PF)
		return false;

	return true;
}

static struct configfs_group_operations xe_config_sriov_group_ops = {
	.is_visible	= xe_config_sriov_is_visible,
};

static const struct config_item_type xe_config_sriov_type = {
	.ct_owner	= THIS_MODULE,
	.ct_group_ops	= &xe_config_sriov_group_ops,
	.ct_attrs	= xe_config_sriov_attrs,
};

static const struct xe_device_desc *xe_match_desc(struct pci_dev *pdev)
{
	struct device_driver *driver = driver_find("xe", &pci_bus_type);
	struct pci_driver *drv = to_pci_driver(driver);
	const struct pci_device_id *ids = drv ? drv->id_table : NULL;
	const struct pci_device_id *found = pci_match_id(ids, pdev);

	return found ? (const void *)found->driver_data : NULL;
}

static struct pci_dev *get_physfn_instead(struct pci_dev *virtfn)
{
	struct pci_dev *physfn = pci_physfn(virtfn);

	pci_dev_get(physfn);
	pci_dev_put(virtfn);
	return physfn;
}

static struct config_group *xe_config_make_device_group(struct config_group *group,
							const char *name)
{
	unsigned int domain, bus, slot, function;
	struct xe_config_group_device *dev;
	const struct xe_device_desc *match;
	enum xe_sriov_mode mode;
	struct pci_dev *pdev;
	char canonical[16];
	int vfnumber = 0;
	int ret;

	ret = sscanf(name, "%x:%x:%x.%x", &domain, &bus, &slot, &function);
	if (ret != 4)
		return ERR_PTR(-EINVAL);

	ret = scnprintf(canonical, sizeof(canonical), "%04x:%02x:%02x.%d", domain, bus,
			PCI_SLOT(PCI_DEVFN(slot, function)),
			PCI_FUNC(PCI_DEVFN(slot, function)));
	if (ret != 12 || strcmp(name, canonical))
		return ERR_PTR(-EINVAL);

	pdev = pci_get_domain_bus_and_slot(domain, bus, PCI_DEVFN(slot, function));
	mode = pdev ? dev_is_pf(&pdev->dev) ?
		XE_SRIOV_MODE_PF : XE_SRIOV_MODE_NONE : XE_SRIOV_MODE_VF;

	if (!pdev && function)
		pdev = pci_get_domain_bus_and_slot(domain, bus, PCI_DEVFN(slot, 0));
	if (!pdev && slot)
		pdev = pci_get_domain_bus_and_slot(domain, bus, PCI_DEVFN(0, 0));
	if (!pdev)
		return ERR_PTR(-ENODEV);

	if (PCI_DEVFN(slot, function) != pdev->devfn) {
		pdev = get_physfn_instead(pdev);
		vfnumber = PCI_DEVFN(slot, function) - pdev->devfn;
		if (!dev_is_pf(&pdev->dev) || vfnumber > pci_sriov_get_totalvfs(pdev)) {
			pci_dev_put(pdev);
			return ERR_PTR(-ENODEV);
		}
	}

	match = xe_match_desc(pdev);
	if (match && vfnumber && !match->has_sriov) {
		pci_info(pdev, "xe driver does not support VFs on this device\n");
		match = NULL;
	} else if (!match) {
		pci_info(pdev, "xe driver does not support configuration of this device\n");
	}

	pci_dev_put(pdev);

	if (!match)
		return ERR_PTR(-ENOENT);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->desc = match;
	dev->mode = match->has_sriov ? mode : XE_SRIOV_MODE_NONE;

	set_device_defaults(&dev->config);

	config_group_init_type_name(&dev->group, name, &xe_config_device_type);
	if (dev->mode != XE_SRIOV_MODE_NONE) {
		config_group_init_type_name(&dev->sriov, "sriov", &xe_config_sriov_type);
		configfs_add_default_group(&dev->sriov, &dev->group);
	}

	mutex_init(&dev->lock);

	return &dev->group;
}

static struct configfs_group_operations xe_config_group_ops = {
	.make_group	= xe_config_make_device_group,
};

static const struct config_item_type xe_configfs_type = {
	.ct_group_ops	= &xe_config_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem xe_configfs = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "xe",
			.ci_type = &xe_configfs_type,
		},
	},
};

static struct xe_config_group_device *find_xe_config_group_device(struct pci_dev *pdev)
{
	struct config_item *item;

	mutex_lock(&xe_configfs.su_mutex);
	item = config_group_find_item(&xe_configfs.su_group, pci_name(pdev));
	mutex_unlock(&xe_configfs.su_mutex);

	if (!item)
		return NULL;

	return to_xe_config_group_device(item);
}

static void dump_custom_dev_config(struct pci_dev *pdev,
				   struct xe_config_group_device *dev)
{
#define PRI_CUSTOM_ATTR(fmt_, attr_) do { \
		if (dev->config.attr_ != device_defaults.attr_) \
			pci_info(pdev, "configfs: " __stringify(attr_) " = " fmt_ "\n", \
				 dev->config.attr_); \
	} while (0)

	PRI_CUSTOM_ATTR("%llx", gt_types_allowed);
	PRI_CUSTOM_ATTR("%llx", engines_allowed);
	PRI_CUSTOM_ATTR("%d", enable_psmi);
	PRI_CUSTOM_ATTR("%d", survivability_mode);

#undef PRI_CUSTOM_ATTR
}

/**
 * xe_configfs_check_device() - Test if device was configured by configfs
 * @pdev: the &pci_dev device to test
 *
 * Try to find the configfs group that belongs to the specified pci device
 * and print a diagnostic message if different than the default value.
 */
void xe_configfs_check_device(struct pci_dev *pdev)
{
	struct xe_config_group_device *dev = find_xe_config_group_device(pdev);

	if (!dev)
		return;

	/* memcmp here is safe as both are zero-initialized */
	if (memcmp(&dev->config, &device_defaults, sizeof(dev->config))) {
		pci_info(pdev, "Found custom settings in configfs\n");
		dump_custom_dev_config(pdev, dev);
	}

	config_group_put(&dev->group);
}

/**
 * xe_configfs_get_survivability_mode - get configfs survivability mode attribute
 * @pdev: pci device
 *
 * Return: survivability_mode attribute in configfs
 */
bool xe_configfs_get_survivability_mode(struct pci_dev *pdev)
{
	struct xe_config_group_device *dev = find_xe_config_group_device(pdev);
	bool mode;

	if (!dev)
		return device_defaults.survivability_mode;

	mode = dev->config.survivability_mode;
	config_group_put(&dev->group);

	return mode;
}

static u64 get_gt_types_allowed(struct pci_dev *pdev)
{
	struct xe_config_group_device *dev = find_xe_config_group_device(pdev);
	u64 mask;

	if (!dev)
		return device_defaults.gt_types_allowed;

	mask = dev->config.gt_types_allowed;
	config_group_put(&dev->group);

	return mask;
}

/**
 * xe_configfs_primary_gt_allowed - determine whether primary GTs are supported
 * @pdev: pci device
 *
 * Return: True if primary GTs are enabled, false if they have been disabled via
 *     configfs.
 */
bool xe_configfs_primary_gt_allowed(struct pci_dev *pdev)
{
	return get_gt_types_allowed(pdev) & BIT_ULL(XE_GT_TYPE_MAIN);
}

/**
 * xe_configfs_media_gt_allowed - determine whether media GTs are supported
 * @pdev: pci device
 *
 * Return: True if the media GTs are enabled, false if they have been disabled
 *     via configfs.
 */
bool xe_configfs_media_gt_allowed(struct pci_dev *pdev)
{
	return get_gt_types_allowed(pdev) & BIT_ULL(XE_GT_TYPE_MEDIA);
}

/**
 * xe_configfs_get_engines_allowed - get engine allowed mask from configfs
 * @pdev: pci device
 *
 * Return: engine mask with allowed engines set in configfs
 */
u64 xe_configfs_get_engines_allowed(struct pci_dev *pdev)
{
	struct xe_config_group_device *dev = find_xe_config_group_device(pdev);
	u64 engines_allowed;

	if (!dev)
		return device_defaults.engines_allowed;

	engines_allowed = dev->config.engines_allowed;
	config_group_put(&dev->group);

	return engines_allowed;
}

/**
 * xe_configfs_get_psmi_enabled - get configfs enable_psmi setting
 * @pdev: pci device
 *
 * Return: enable_psmi setting in configfs
 */
bool xe_configfs_get_psmi_enabled(struct pci_dev *pdev)
{
	struct xe_config_group_device *dev = find_xe_config_group_device(pdev);
	bool ret;

	if (!dev)
		return false;

	ret = dev->config.enable_psmi;
	config_group_put(&dev->group);

	return ret;
}

/**
 * xe_configfs_get_ctx_restore_mid_bb - get configfs ctx_restore_mid_bb setting
 * @pdev: pci device
 * @class: hw engine class
 * @cs: pointer to the bb to use - only valid during probe
 *
 * Return: Number of dwords used in the mid_ctx_restore setting in configfs
 */
u32 xe_configfs_get_ctx_restore_mid_bb(struct pci_dev *pdev,
				       enum xe_engine_class class,
				       const u32 **cs)
{
	struct xe_config_group_device *dev = find_xe_config_group_device(pdev);
	u32 len;

	if (!dev)
		return 0;

	if (cs)
		*cs = dev->config.ctx_restore_mid_bb[class].cs;

	len = dev->config.ctx_restore_mid_bb[class].len;
	config_group_put(&dev->group);

	return len;
}

/**
 * xe_configfs_get_ctx_restore_post_bb - get configfs ctx_restore_post_bb setting
 * @pdev: pci device
 * @class: hw engine class
 * @cs: pointer to the bb to use - only valid during probe
 *
 * Return: Number of dwords used in the post_ctx_restore setting in configfs
 */
u32 xe_configfs_get_ctx_restore_post_bb(struct pci_dev *pdev,
					enum xe_engine_class class,
					const u32 **cs)
{
	struct xe_config_group_device *dev = find_xe_config_group_device(pdev);
	u32 len;

	if (!dev)
		return 0;

	*cs = dev->config.ctx_restore_post_bb[class].cs;
	len = dev->config.ctx_restore_post_bb[class].len;
	config_group_put(&dev->group);

	return len;
}

#ifdef CONFIG_PCI_IOV
/**
 * xe_configfs_get_max_vfs() - Get number of VFs that could be managed
 * @pdev: the &pci_dev device
 *
 * Find the configfs group that belongs to the PCI device and return maximum
 * number of Virtual Functions (VFs) that could be managed by this device.
 * If configfs group is not present, use value of max_vfs module parameter.
 *
 * Return: maximum number of VFs that could be managed.
 */
unsigned int xe_configfs_get_max_vfs(struct pci_dev *pdev)
{
	struct xe_config_group_device *dev = find_xe_config_group_device(pdev);
	unsigned int max_vfs;

	if (!dev)
		return xe_modparam.max_vfs;

	scoped_guard(mutex, &dev->lock)
		max_vfs = dev->config.sriov.max_vfs;

	config_group_put(&dev->group);

	return max_vfs;
}
#endif

int __init xe_configfs_init(void)
{
	int ret;

	config_group_init(&xe_configfs.su_group);
	mutex_init(&xe_configfs.su_mutex);
	ret = configfs_register_subsystem(&xe_configfs);
	if (ret) {
		mutex_destroy(&xe_configfs.su_mutex);
		return ret;
	}

	return 0;
}

void xe_configfs_exit(void)
{
	configfs_unregister_subsystem(&xe_configfs);
	mutex_destroy(&xe_configfs.su_mutex);
}
