// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifdef CONFIG_DEBUG_FS

#include <linux/debugfs.h>

#define CHAR_BRAKE_MODE				24
#define CHAR_PER_PATTERN_S			48

static int vmax_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->vmax_mv;

	return 0;
}

static int vmax_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	if (val > MAX_VMAX_MV)
		val = MAX_VMAX_MV;

	effect->vmax_mv = (u32) val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(vmax_debugfs_ops, vmax_dbgfs_read,
		vmax_dbgfs_write, "%llu\n");

static int auto_res_en_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = !effect->auto_res_disable;

	return 0;
}

static int auto_res_en_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	effect->auto_res_disable = !val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(auto_res_en_debugfs_ops,  auto_res_en_dbgfs_read,
		auto_res_en_dbgfs_write, "%llu\n");

static ssize_t pattern_s_dbgfs_read(struct file *fp, char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	u32 pos = 0, size = CHAR_PER_PATTERN_S * SAMPLES_PER_PATTERN;
	char *str;
	int i = 0, rc;

	if (!effect->pattern)
		return 0;

	str = kzalloc(size, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	for (i = 0; i < SAMPLES_PER_PATTERN; i++) {
		pos += scnprintf(str + pos, size - pos, "0x%03x  ",
				effect->pattern->samples[i].amplitude);
		pos += scnprintf(str + pos, size - pos, "%s(0x%02x)  ",
				period_str[effect->pattern->samples[i].period],
				effect->pattern->samples[i].period);
		pos += scnprintf(str + pos, size - pos, "F_LRA_X2(%1d)\n",
				 effect->pattern->samples[i].f_lra_x2);
	}

	rc = simple_read_from_buffer(buf, count, ppos, str, pos);
	kfree(str);

	return rc;
}

static ssize_t pattern_s_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct pattern_s patterns[SAMPLES_PER_PATTERN] = {{0, 0, 0},};
	char *str, *kbuf, *token;
	u32 val, tmp[3 * SAMPLES_PER_PATTERN] = {0};
	int rc, i = 0, j = 0;

	if (count > CHAR_PER_PATTERN_S * SAMPLES_PER_PATTERN)
		return -EINVAL;

	kbuf = kzalloc(CHAR_PER_PATTERN_S * SAMPLES_PER_PATTERN + 1,
						GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	str = kbuf;
	rc = copy_from_user(str, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	str[count] = '\0';
	*ppos += count;

	while ((token = strsep((char **)&str, " ")) != NULL) {
		rc = kstrtouint(token, 0, &val);
		if (rc < 0) {
			rc = -EINVAL;
			goto exit;
		}

		tmp[i++] = val;
	}

	if (i % 3)
		pr_warn("Tuple should be having 3 elements, discarding tuple %d\n",
				i / 3);

	for (j = 0; j < i / 3; j++) {
		if (tmp[3 * j] > 0x1ff || tmp[3 * j + 1] > T_LRA_X_8 ||
				tmp[3 * j + 2] > 1) {
			pr_err("allowed tuples: [amplitude(<= 0x1ff) period(<=6(T_LRA_X_8)) f_lra_x2(0,1)]\n");
			rc = -EINVAL;
			goto exit;
		}

		patterns[j].amplitude = (u16)tmp[3 * j];
		patterns[j].period = (enum s_period)tmp[3 * j + 1];
		patterns[j].f_lra_x2 = !!tmp[3 * j + 2];
	}

	memcpy(effect->pattern->samples, patterns,
			sizeof(effect->pattern->samples));

	/* recalculate the play length */
	effect->pattern->play_length_us =
		get_pattern_play_length_us(effect->pattern);
	if (effect->pattern->play_length_us == -EINVAL) {
		pr_err("get pattern play length failed\n");
		rc = -EINVAL;
		goto exit;
	}

	rc = count;
exit:
	kfree(kbuf);
	return rc;
}

static const struct file_operations pattern_s_dbgfs_ops = {
	.read = pattern_s_dbgfs_read,
	.write = pattern_s_dbgfs_write,
	.open = simple_open,
};

static int pattern_play_rate_us_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->pattern->play_rate_us;

	return 0;
}

static int pattern_play_rate_us_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	if (val > TLRA_MAX_US)
		val = TLRA_MAX_US;

	effect->pattern->play_rate_us = (u32)val;
	/* recalculate the play length */
	effect->pattern->play_length_us =
		get_pattern_play_length_us(effect->pattern);
	if (effect->pattern->play_length_us == -EINVAL) {
		pr_err("get pattern play length failed\n");
		return -EINVAL;
	}

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(pattern_play_rate_dbgfs_ops,
		pattern_play_rate_us_dbgfs_read,
		pattern_play_rate_us_dbgfs_write, "%llu\n");

static ssize_t fifo_s_dbgfs_read(struct file *fp, char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct fifo_cfg *fifo = effect->fifo;
	char *kbuf;
	int rc, i;
	u32 size, pos = 0;

	size = CHAR_PER_SAMPLE * fifo->num_s + 1;
	kbuf = kzalloc(size, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	for (i = 0; i < fifo->num_s; i++)
		pos += scnprintf(kbuf + pos, size - pos,
				"%d ", (s8)fifo->samples[i]);

	pos += scnprintf(kbuf + pos, size - pos, "%s", "\n");
	rc = simple_read_from_buffer(buf, count, ppos, kbuf, pos);
	kfree(kbuf);

	return rc;
}

static ssize_t fifo_s_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct fifo_cfg *fifo = effect->fifo;
	char *str, *kbuf, *token;
	int rc, i = 0;
	int val;
	u8 *samples;

	kbuf = kzalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	str = kbuf;
	rc = copy_from_user(str, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	str[count] = '\0';
	*ppos += count;

	samples = kcalloc(fifo->num_s, sizeof(*samples), GFP_KERNEL);
	if (!samples) {
		rc = -ENOMEM;
		goto exit;
	}

	while ((token = strsep(&str, " ")) != NULL) {
		rc = kstrtoint(token, 0, &val);
		if (rc < 0) {
			rc = -EINVAL;
			goto exit2;
		}

		if (val > 0xff)
			val = 0xff;

		samples[i++] = (u8)val;
		/* only support fifo pattern no longer than before */
		if (i >= fifo->num_s)
			break;
	}

	memcpy(fifo->samples, samples, fifo->num_s);
	fifo->play_length_us = get_fifo_play_length_us(fifo, effect->t_lra_us);
	if (fifo->play_length_us == -EINVAL) {
		pr_err("get fifo play length failed\n");
		rc = -EINVAL;
		goto exit2;
	}

	rc = count;
exit2:
	kfree(samples);
exit:
	kfree(kbuf);
	return rc;
}

static const struct file_operations fifo_s_dbgfs_ops = {
	.read = fifo_s_dbgfs_read,
	.write = fifo_s_dbgfs_write,
	.owner = THIS_MODULE,
	.open = simple_open,
};

static int fifo_period_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->fifo->period_per_s;

	return 0;
}

static int fifo_period_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;
	struct fifo_cfg *fifo = effect->fifo;

	if (val > F_48KHZ)
		return -EINVAL;

	fifo->period_per_s = (enum s_period)val;
	fifo->play_length_us = get_fifo_play_length_us(fifo, effect->t_lra_us);
	if (fifo->play_length_us == -EINVAL) {
		pr_err("get fifo play length failed\n");
		return -EINVAL;
	}

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fifo_period_dbgfs_ops,
		fifo_period_dbgfs_read,
		fifo_period_dbgfs_write, "%llu\n");

static ssize_t brake_s_dbgfs_read(struct file *fp, char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char *str;
	int rc, i;
	u32 size, pos = 0;

	size = CHAR_PER_SAMPLE * BRAKE_SAMPLE_COUNT + 1;
	str = kzalloc(size, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	for (i = 0; i < BRAKE_SAMPLE_COUNT; i++)
		pos += scnprintf(str + pos, size - pos, "0x%02x ",
				brake->samples[i]);

	pos += scnprintf(str + pos, size - pos, "%s", "\n");
	rc = simple_read_from_buffer(buf, count, ppos, str, pos);
	kfree(str);

	return rc;
}

static ssize_t brake_s_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char *str, *kbuf, *token;
	int rc, i = 0;
	u32 val;
	u8 samples[BRAKE_SAMPLE_COUNT] = {0};

	if (count > CHAR_PER_SAMPLE * BRAKE_SAMPLE_COUNT)
		return -EINVAL;

	kbuf = kzalloc(CHAR_PER_SAMPLE * BRAKE_SAMPLE_COUNT + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	str = kbuf;
	rc = copy_from_user(str, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	str[count] = '\0';
	*ppos += count;

	while ((token = strsep((char **)&str, " ")) != NULL) {
		rc = kstrtouint(token, 0, &val);
		if (rc < 0) {
			rc = -EINVAL;
			goto exit;
		}

		if (val > 0xff)
			val = 0xff;

		samples[i++] = (u8)val;
		if (i >= BRAKE_SAMPLE_COUNT)
			break;
	}

	memcpy(brake->samples, samples, BRAKE_SAMPLE_COUNT);
	verify_brake_samples(brake);
	brake->play_length_us =
		get_brake_play_length_us(brake, effect->t_lra_us);

	rc = count;
exit:
	kfree(kbuf);
	return rc;
}

static const struct file_operations brake_s_dbgfs_ops = {
	.read = brake_s_dbgfs_read,
	.write = brake_s_dbgfs_write,
	.open = simple_open,
};

static ssize_t brake_mode_dbgfs_read(struct file *fp,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char str[CHAR_BRAKE_MODE] = {0};
	u32 size;
	int rc;

	size = scnprintf(str, ARRAY_SIZE(str), "%s\n", brake_str[brake->mode]);
	rc = simple_read_from_buffer(buf, count, ppos, str, size);

	return rc;
}

static ssize_t brake_mode_dbgfs_write(struct file *fp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct haptics_effect *effect = fp->private_data;
	struct brake_cfg *brake = effect->brake;
	char *kbuf;
	int rc;

	kbuf = kzalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	rc = copy_from_user(kbuf, buf, count);
	if (rc > 0) {
		rc = -EFAULT;
		goto exit;
	}

	kbuf[count] = '\0';
	*ppos += count;
	rc = count;
	if (strcmp(kbuf, "open-loop") == 0) {
		brake->mode = OL_BRAKE;
	} else if (strcmp(kbuf, "close-loop") == 0) {
		brake->mode = CL_BRAKE;
	} else if (strcmp(kbuf, "predictive") == 0) {
		brake->mode = PREDICT_BRAKE;
	} else if (strcmp(kbuf, "auto") == 0) {
		brake->mode = AUTO_BRAKE;
	} else {
		pr_err("%s brake mode is not supported\n", kbuf);
		rc = -EINVAL;
	}

exit:
	kfree(kbuf);
	return rc;
}

static const struct file_operations brake_mode_dbgfs_ops = {
	.read = brake_mode_dbgfs_read,
	.write = brake_mode_dbgfs_write,
	.open = simple_open,
};

static int brake_en_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = !effect->brake->disabled;

	return 0;
}

static int brake_en_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	effect->brake->disabled = !val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(brake_en_dbgfs_ops,  brake_en_dbgfs_read,
		brake_en_dbgfs_write, "%llu\n");

static int brake_sine_gain_dbgfs_read(void *data, u64 *val)
{
	struct haptics_effect *effect = data;

	*val = effect->brake->sine_gain;

	return 0;
}

static int brake_sine_gain_dbgfs_write(void *data, u64 val)
{
	struct haptics_effect *effect = data;

	if (val > BRAKE_SINE_GAIN_X8)
		return -EINVAL;

	effect->brake->sine_gain = val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(brake_sine_gain_dbgfs_ops,
		brake_sine_gain_dbgfs_read,
		brake_sine_gain_dbgfs_write, "%llu\n");

static int preload_effect_idx_dbgfs_read(void *data, u64 *val)
{
	struct haptics_chip *chip = data;

	*val = chip->config.preload_effect;

	return 0;
}

static int preload_effect_idx_dbgfs_write(void *data, u64 val)
{
	struct haptics_chip *chip = data;
	struct haptics_effect *new, *old;
	int rc, i;

	for (i = 0; i < chip->effects_count; i++)
		if (chip->effects[i].id == val)
			break;

	if (i == chip->effects_count)
		return -EINVAL;

	new = &chip->effects[i];

	for (i = 0; i < chip->effects_count; i++)
		if (chip->effects[i].id == chip->config.preload_effect)
			break;

	old = &chip->effects[i];

	chip->config.preload_effect = (u32)val;

	new->pattern->preload = true;
	new->src = PATTERN2;
	rc = haptics_set_pattern(chip, new->pattern, new->src);
	if (rc < 0)
		return rc;

	old->src = PATTERN1;
	old->pattern->preload = false;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(preload_effect_idx_dbgfs_ops,
		preload_effect_idx_dbgfs_read,
		preload_effect_idx_dbgfs_write, "%llu\n");

static int haptics_add_effects_debugfs(struct haptics_effect *effect, struct dentry *dir)
{
	struct dentry *file, *pattern_dir, *fifo_dir, *brake_dir;

	file = debugfs_create_file_unsafe("vmax_mv", 0644, dir,
			effect, &vmax_debugfs_ops);
	if (IS_ERR(file))
		return PTR_ERR(file);

	file = debugfs_create_file_unsafe("lra_auto_res_en", 0644, dir,
			effect, &auto_res_en_debugfs_ops);
	if (IS_ERR(file))
		return PTR_ERR(file);

	/* effect can have either pattern or FIFO */
	if (effect->pattern) {
		pattern_dir = debugfs_create_dir("pattern", dir);
		if (IS_ERR(pattern_dir))
			return PTR_ERR(pattern_dir);

		file = debugfs_create_file("samples", 0644, pattern_dir,
				effect, &pattern_s_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("play_rate_us", 0644,
				pattern_dir, effect,
				&pattern_play_rate_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);
	} else if (effect->fifo) {
		fifo_dir = debugfs_create_dir("fifo", dir);
		if (IS_ERR(fifo_dir))
			return PTR_ERR(fifo_dir);

		file = debugfs_create_file("samples", 0644, fifo_dir,
				effect, &fifo_s_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("period", 0644, fifo_dir,
				effect, &fifo_period_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);
	}

	if (effect->brake) {
		brake_dir = debugfs_create_dir("brake", dir);
		if (IS_ERR(brake_dir))
			return PTR_ERR(brake_dir);

		file = debugfs_create_file("samples", 0644, brake_dir,
				effect, &brake_s_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file("mode", 0644, brake_dir,
				effect, &brake_mode_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("enable", 0644, brake_dir,
				effect, &brake_en_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file_unsafe("sine_gain", 0644, brake_dir,
				effect, &brake_sine_gain_dbgfs_ops);
		if (IS_ERR(file))
			return PTR_ERR(file);
	}

	return 0;
}

#define EFFECT_NAME_SIZE		15
static int haptics_add_debugfs(struct dentry *hap_dir, struct haptics_effect *effects,
			int count, char *effect_name)
{
	struct dentry *effect_dir;
	char str[EFFECT_NAME_SIZE] = {0};
	int rc = 0;
	int i = 0;

	for (; i < count; i++) {
		scnprintf(str, ARRAY_SIZE(str), "%s%d", effect_name, effects[i].id);
		effect_dir = debugfs_create_dir(str, hap_dir);
		if (IS_ERR(effect_dir)) {
			rc = PTR_ERR(effect_dir);
			pr_err("create %s debugfs directory failed, rc=%d\n", str, rc);
			return rc;
		}
		rc = haptics_add_effects_debugfs(&effects[i], effect_dir);
		if (rc < 0) {
			pr_err("create debugfs nodes for %s failed, rc=%d\n", str, rc);
			return rc;
		}
	}

	return rc;
}

void haptics_remove_debugfs(struct haptics_chip *chip)
{
	debugfs_remove_recursive(chip->debugfs_dir);
}

int haptics_create_debugfs(struct haptics_chip *chip)
{
	struct dentry *hap_dir, *file;
	int rc = 0;

	hap_dir = debugfs_create_dir("haptics", NULL);
	if (IS_ERR(hap_dir)) {
		rc = PTR_ERR(hap_dir);
		dev_err(chip->dev, "create haptics debugfs directory failed, rc=%d\n",
				rc);
		return rc;
	}

	rc = haptics_add_debugfs(hap_dir, chip->effects, chip->effects_count, "effect");
	if (rc < 0)
		goto exit;

	rc = haptics_add_debugfs(hap_dir, chip->primitives, chip->primitives_count, "primitive");
	if (rc < 0)
		goto exit;

	file = debugfs_create_file_unsafe("preload_effect_idx", 0644, hap_dir,
			chip, &preload_effect_idx_dbgfs_ops);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		dev_err(chip->dev, "create preload_effect_idx debugfs failed, rc=%d\n",
				rc);
		goto exit;
	}

	debugfs_create_u32("fifo_empty_thresh", 0600, hap_dir,
			&chip->config.fifo_empty_thresh);
	chip->debugfs_dir = hap_dir;
	return 0;

exit:
	haptics_remove_debugfs(chip);
	return rc;
}
#else
static inline void haptics_remove_debugfs(struct haptics_chip *chip)
{
}
static inline int haptics_create_debugfs(struct haptics_chip *chip)
{
	return 0;
}
#endif
