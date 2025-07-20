// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <linux/kstrtox.h>
#include <asm/loongarch.h>

struct dentry *arch_debugfs_dir;
EXPORT_SYMBOL(arch_debugfs_dir);

static int sfb_state, tso_state;

static void set_sfb_state(void *info)
{
	int val = *(int *)info << CSR_STFILL_SHIFT;

	csr_xchg32(val, CSR_STFILL, LOONGARCH_CSR_IMPCTL1);
}

static ssize_t sfb_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int s, state;
	char str[32];

	state = (csr_read32(LOONGARCH_CSR_IMPCTL1) & CSR_STFILL) >> CSR_STFILL_SHIFT;

	s = snprintf(str, sizeof(str), "Boot State: %x\nCurrent State: %x\n", sfb_state, state);

	if (*ppos >= s)
		return 0;

	s -= *ppos;
	s = min_t(u32, s, count);

	if (copy_to_user(buf, &str[*ppos], s))
		return -EFAULT;

	*ppos += s;

	return s;
}

static ssize_t sfb_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	int state;

	if (kstrtoint_from_user(buf, count, 10, &state))
		return -EFAULT;

	switch (state) {
	case 0: case 1:
		on_each_cpu(set_sfb_state, &state, 1);
		break;
	default:
		return -EINVAL;
	}

	return count;
}

static const struct file_operations sfb_fops = {
	.read = sfb_read,
	.write = sfb_write,
	.open = simple_open,
	.llseek = default_llseek
};

#define LDSTORDER_NLD_NST        0x0 /* 000 = No Load No Store */
#define LDSTORDER_ALD_NST        0x1 /* 001 = All Load No Store */
#define LDSTORDER_SLD_NST        0x3 /* 011 = Same Load No Store */
#define LDSTORDER_NLD_AST        0x4 /* 100 = No Load All Store */
#define LDSTORDER_ALD_AST        0x5 /* 101 = All Load All Store */
#define LDSTORDER_SLD_AST        0x7 /* 111 = Same Load All Store */

static char *tso_hints[] = {
	"No Load No Store",
	"All Load No Store",
	"Invalid Config",
	"Same Load No Store",
	"No Load All Store",
	"All Load All Store",
	"Invalid Config",
	"Same Load All Store"
};

static void set_tso_state(void *info)
{
	int val = *(int *)info << CSR_LDSTORDER_SHIFT;

	csr_xchg32(val, CSR_LDSTORDER_MASK, LOONGARCH_CSR_IMPCTL1);
}

static ssize_t tso_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int s, state;
	char str[240];

	state = (csr_read32(LOONGARCH_CSR_IMPCTL1) & CSR_LDSTORDER_MASK) >> CSR_LDSTORDER_SHIFT;

	s = snprintf(str, sizeof(str), "Boot State: %d (%s)\n"
			               "Current State: %d (%s)\n\n"
				       "Available States:\n"
				       "0 (%s)\t" "1 (%s)\t" "3 (%s)\n"
				       "4 (%s)\t" "5 (%s)\t" "7 (%s)\n",
				       tso_state, tso_hints[tso_state], state, tso_hints[state],
				       tso_hints[0], tso_hints[1], tso_hints[3], tso_hints[4], tso_hints[5], tso_hints[7]);

	if (*ppos >= s)
		return 0;

	s -= *ppos;
	s = min_t(u32, s, count);

	if (copy_to_user(buf, &str[*ppos], s))
		return -EFAULT;

	*ppos += s;

	return s;
}

static ssize_t tso_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	int state;

	if (kstrtoint_from_user(buf, count, 10, &state))
		return -EFAULT;

	switch (state) {
	case 0: case 1: case 3:
	case 4: case 5: case 7:
		on_each_cpu(set_tso_state, &state, 1);
		break;
	default:
		return -EINVAL;
	}

	return count;
}

static const struct file_operations tso_fops = {
	.read = tso_read,
	.write = tso_write,
	.open = simple_open,
	.llseek = default_llseek
};

static int __init arch_kdebugfs_init(void)
{
	unsigned int config = read_cpucfg(LOONGARCH_CPUCFG3);

	arch_debugfs_dir = debugfs_create_dir("loongarch", NULL);

	if (config & CPUCFG3_SFB) {
		debugfs_create_file("sfb_state", S_IRUGO | S_IWUSR,
			    arch_debugfs_dir, &sfb_state, &sfb_fops);
		sfb_state = (csr_read32(LOONGARCH_CSR_IMPCTL1) & CSR_STFILL) >> CSR_STFILL_SHIFT;
	}

	if (config & (CPUCFG3_ALDORDER_CAP | CPUCFG3_ASTORDER_CAP)) {
		debugfs_create_file("tso_state", S_IRUGO | S_IWUSR,
			    arch_debugfs_dir, &tso_state, &tso_fops);
		tso_state = (csr_read32(LOONGARCH_CSR_IMPCTL1) & CSR_LDSTORDER_MASK) >> CSR_LDSTORDER_SHIFT;
	}

	return 0;
}
postcore_initcall(arch_kdebugfs_init);
