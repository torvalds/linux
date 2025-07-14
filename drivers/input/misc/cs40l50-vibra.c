// SPDX-License-Identifier: GPL-2.0
/*
 * CS40L50 Advanced Haptic Driver with waveform memory,
 * integrated DSP, and closed-loop algorithms
 *
 * Copyright 2024 Cirrus Logic, Inc.
 *
 * Author: James Ogletree <james.ogletree@cirrus.com>
 */

#include <linux/bitfield.h>
#include <linux/input.h>
#include <linux/mfd/cs40l50.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

/* Wavetables */
#define CS40L50_RAM_INDEX_START		0x1000000
#define CS40L50_RAM_INDEX_END		0x100007F
#define CS40L50_RTH_INDEX_START		0x1400000
#define CS40L50_RTH_INDEX_END		0x1400001
#define CS40L50_ROM_INDEX_START		0x1800000
#define CS40L50_ROM_INDEX_END		0x180001A
#define CS40L50_TYPE_PCM		8
#define CS40L50_TYPE_PWLE		12
#define CS40L50_PCM_ID			0x0
#define CS40L50_OWT_CUSTOM_DATA_SIZE	2
#define CS40L50_CUSTOM_DATA_MASK	0xFFFFU

/* DSP */
#define CS40L50_GPIO_BASE		0x2804140
#define CS40L50_OWT_BASE		0x2805C34
#define CS40L50_OWT_SIZE		0x2805C38
#define CS40L50_OWT_NEXT		0x2805C3C
#define CS40L50_EFFECTS_MAX		1

/* GPIO */
#define CS40L50_GPIO_NUM_MASK		GENMASK(14, 12)
#define CS40L50_GPIO_EDGE_MASK		BIT(15)
#define CS40L50_GPIO_MAPPING_NONE	0
#define CS40L50_GPIO_DISABLE		0x1FF

enum cs40l50_bank_type {
	CS40L50_WVFRM_BANK_RAM,
	CS40L50_WVFRM_BANK_ROM,
	CS40L50_WVFRM_BANK_OWT,
	CS40L50_WVFRM_BANK_NUM,
};

/* Describes an area in DSP memory populated by effects */
struct cs40l50_bank {
	enum cs40l50_bank_type type;
	u32 base_index;
	u32 max_index;
};

struct cs40l50_effect {
	enum cs40l50_bank_type type;
	struct list_head list;
	u32 gpio_reg;
	u32 index;
	int id;
};

/* Describes haptic interface of loaded DSP firmware */
struct cs40l50_vibra_dsp {
	struct cs40l50_bank *banks;
	u32 gpio_base_reg;
	u32 owt_offset_reg;
	u32 owt_size_reg;
	u32 owt_base_reg;
	u32 push_owt_cmd;
	u32 delete_owt_cmd;
	u32 stop_cmd;
	int (*write)(struct device *dev, struct regmap *regmap, u32 val);
};

/* Describes configuration and state of haptic operations */
struct cs40l50_vibra {
	struct device *dev;
	struct regmap *regmap;
	struct input_dev *input;
	struct workqueue_struct *vib_wq;
	struct list_head effect_head;
	struct cs40l50_vibra_dsp dsp;
};

struct cs40l50_work {
	struct cs40l50_vibra *vib;
	struct ff_effect *effect;
	struct work_struct work;
	s16 *custom_data;
	int custom_len;
	int count;
	int error;
};

static struct cs40l50_bank cs40l50_banks[] = {
	{
		.type =		CS40L50_WVFRM_BANK_RAM,
		.base_index =	CS40L50_RAM_INDEX_START,
		.max_index =	CS40L50_RAM_INDEX_END,
	},
	{
		.type =		CS40L50_WVFRM_BANK_ROM,
		.base_index =	CS40L50_ROM_INDEX_START,
		.max_index =	CS40L50_ROM_INDEX_END,
	},
	{
		.type =		CS40L50_WVFRM_BANK_OWT,
		.base_index =	CS40L50_RTH_INDEX_START,
		.max_index =	CS40L50_RTH_INDEX_END,
	},
};

static struct cs40l50_vibra_dsp cs40l50_dsp = {
	.banks =		cs40l50_banks,
	.gpio_base_reg =	CS40L50_GPIO_BASE,
	.owt_base_reg =		CS40L50_OWT_BASE,
	.owt_offset_reg =	CS40L50_OWT_NEXT,
	.owt_size_reg =		CS40L50_OWT_SIZE,
	.push_owt_cmd =		CS40L50_OWT_PUSH,
	.delete_owt_cmd =	CS40L50_OWT_DELETE,
	.stop_cmd =		CS40L50_STOP_PLAYBACK,
	.write =		cs40l50_dsp_write,
};

static struct cs40l50_effect *cs40l50_find_effect(int id, struct list_head *effect_head)
{
	struct cs40l50_effect *effect;

	list_for_each_entry(effect, effect_head, list)
		if (effect->id == id)
			return effect;

	return NULL;
}

static int cs40l50_effect_bank_set(struct cs40l50_work *work_data,
				   struct cs40l50_effect *effect)
{
	s16 bank_type = work_data->custom_data[0] & CS40L50_CUSTOM_DATA_MASK;

	if (bank_type >= CS40L50_WVFRM_BANK_NUM) {
		dev_err(work_data->vib->dev, "Invalid bank (%d)\n", bank_type);
		return -EINVAL;
	}

	if (work_data->custom_len > CS40L50_OWT_CUSTOM_DATA_SIZE)
		effect->type = CS40L50_WVFRM_BANK_OWT;
	else
		effect->type = bank_type;

	return 0;
}

static int cs40l50_effect_index_set(struct cs40l50_work *work_data,
				    struct cs40l50_effect *effect)
{
	struct cs40l50_vibra *vib = work_data->vib;
	struct cs40l50_effect *owt_effect;
	u32 base_index, max_index;

	base_index = vib->dsp.banks[effect->type].base_index;
	max_index = vib->dsp.banks[effect->type].max_index;

	effect->index = base_index;

	switch (effect->type) {
	case CS40L50_WVFRM_BANK_OWT:
		list_for_each_entry(owt_effect, &vib->effect_head, list)
			if (owt_effect->type == CS40L50_WVFRM_BANK_OWT)
				effect->index++;
		break;
	case CS40L50_WVFRM_BANK_ROM:
	case CS40L50_WVFRM_BANK_RAM:
		effect->index += work_data->custom_data[1] & CS40L50_CUSTOM_DATA_MASK;
		break;
	default:
		dev_err(vib->dev, "Bank type %d not supported\n", effect->type);
		return -EINVAL;
	}

	if (effect->index > max_index || effect->index < base_index) {
		dev_err(vib->dev, "Index out of bounds: %u\n", effect->index);
		return -ENOSPC;
	}

	return 0;
}

static int cs40l50_effect_gpio_mapping_set(struct cs40l50_work *work_data,
					   struct cs40l50_effect *effect)
{
	u16 gpio_edge, gpio_num, button = work_data->effect->trigger.button;
	struct cs40l50_vibra *vib = work_data->vib;

	if (button) {
		gpio_num = FIELD_GET(CS40L50_GPIO_NUM_MASK, button);
		gpio_edge = FIELD_GET(CS40L50_GPIO_EDGE_MASK, button);
		effect->gpio_reg = vib->dsp.gpio_base_reg + (gpio_num * 8) - gpio_edge;

		return regmap_write(vib->regmap, effect->gpio_reg, button);
	}

	effect->gpio_reg = CS40L50_GPIO_MAPPING_NONE;

	return 0;
}

struct cs40l50_owt_header {
	u32 type;
	u32 data_words;
	u32 offset;
} __packed;

static int cs40l50_upload_owt(struct cs40l50_work *work_data)
{
	u8 *new_owt_effect_data __free(kfree) = NULL;
	struct cs40l50_vibra *vib = work_data->vib;
	size_t len = work_data->custom_len * 2;
	struct cs40l50_owt_header header;
	u32 offset, size;
	int error;

	error = regmap_read(vib->regmap, vib->dsp.owt_size_reg, &size);
	if (error)
		return error;

	if ((size * sizeof(u32)) < sizeof(header) + len) {
		dev_err(vib->dev, "No space in open wavetable for effect\n");
		return -ENOSPC;
	}

	header.type = work_data->custom_data[0] == CS40L50_PCM_ID ? CS40L50_TYPE_PCM :
								    CS40L50_TYPE_PWLE;
	header.offset = sizeof(header) / sizeof(u32);
	header.data_words = len / sizeof(u32);

	new_owt_effect_data = kmalloc(sizeof(header) + len, GFP_KERNEL);
	if (!new_owt_effect_data)
		return -ENOMEM;

	memcpy(new_owt_effect_data, &header, sizeof(header));
	memcpy(new_owt_effect_data + sizeof(header), work_data->custom_data, len);

	error = regmap_read(vib->regmap, vib->dsp.owt_offset_reg, &offset);
	if (error)
		return error;

	error = regmap_bulk_write(vib->regmap, vib->dsp.owt_base_reg +
				  (offset * sizeof(u32)), new_owt_effect_data,
				  sizeof(header) + len);
	if (error)
		return error;

	error = vib->dsp.write(vib->dev, vib->regmap, vib->dsp.push_owt_cmd);
	if (error)
		return error;

	return 0;
}

static void cs40l50_add_worker(struct work_struct *work)
{
	struct cs40l50_work *work_data = container_of(work, struct cs40l50_work, work);
	struct cs40l50_vibra *vib = work_data->vib;
	struct cs40l50_effect *effect;
	bool is_new = false;
	int error;

	error = pm_runtime_resume_and_get(vib->dev);
	if (error)
		goto err_exit;

	/* Update effect if already uploaded, otherwise create new effect */
	effect = cs40l50_find_effect(work_data->effect->id, &vib->effect_head);
	if (!effect) {
		effect = kzalloc(sizeof(*effect), GFP_KERNEL);
		if (!effect) {
			error = -ENOMEM;
			goto err_pm;
		}

		effect->id = work_data->effect->id;
		is_new = true;
	}

	error = cs40l50_effect_bank_set(work_data, effect);
	if (error)
		goto err_free;

	error = cs40l50_effect_index_set(work_data, effect);
	if (error)
		goto err_free;

	error = cs40l50_effect_gpio_mapping_set(work_data, effect);
	if (error)
		goto err_free;

	if (effect->type == CS40L50_WVFRM_BANK_OWT)
		error = cs40l50_upload_owt(work_data);
err_free:
	if (is_new) {
		if (error)
			kfree(effect);
		else
			list_add(&effect->list, &vib->effect_head);
	}
err_pm:
	pm_runtime_mark_last_busy(vib->dev);
	pm_runtime_put_autosuspend(vib->dev);
err_exit:
	work_data->error = error;
}

static int cs40l50_add(struct input_dev *dev, struct ff_effect *effect,
		       struct ff_effect *old)
{
	struct ff_periodic_effect *periodic = &effect->u.periodic;
	struct cs40l50_vibra *vib = input_get_drvdata(dev);
	struct cs40l50_work work_data;

	if (effect->type != FF_PERIODIC || periodic->waveform != FF_CUSTOM) {
		dev_err(vib->dev, "Type (%#X) or waveform (%#X) unsupported\n",
			effect->type, periodic->waveform);
		return -EINVAL;
	}

	work_data.custom_data = memdup_array_user(effect->u.periodic.custom_data,
						  effect->u.periodic.custom_len,
						  sizeof(s16));
	if (IS_ERR(work_data.custom_data))
		return PTR_ERR(work_data.custom_data);

	work_data.custom_len = effect->u.periodic.custom_len;
	work_data.vib = vib;
	work_data.effect = effect;
	INIT_WORK_ONSTACK(&work_data.work, cs40l50_add_worker);

	/* Push to the workqueue to serialize with playbacks */
	queue_work(vib->vib_wq, &work_data.work);
	flush_work(&work_data.work);
	destroy_work_on_stack(&work_data.work);

	kfree(work_data.custom_data);

	return work_data.error;
}

static void cs40l50_start_worker(struct work_struct *work)
{
	struct cs40l50_work *work_data = container_of(work, struct cs40l50_work, work);
	struct cs40l50_vibra *vib = work_data->vib;
	struct cs40l50_effect *start_effect;

	if (pm_runtime_resume_and_get(vib->dev) < 0)
		goto err_free;

	start_effect = cs40l50_find_effect(work_data->effect->id, &vib->effect_head);
	if (start_effect) {
		while (--work_data->count >= 0) {
			vib->dsp.write(vib->dev, vib->regmap, start_effect->index);
			usleep_range(work_data->effect->replay.length,
				     work_data->effect->replay.length + 100);
		}
	} else {
		dev_err(vib->dev, "Effect to play not found\n");
	}

	pm_runtime_mark_last_busy(vib->dev);
	pm_runtime_put_autosuspend(vib->dev);
err_free:
	kfree(work_data);
}

static void cs40l50_stop_worker(struct work_struct *work)
{
	struct cs40l50_work *work_data = container_of(work, struct cs40l50_work, work);
	struct cs40l50_vibra *vib = work_data->vib;

	if (pm_runtime_resume_and_get(vib->dev) < 0)
		return;

	vib->dsp.write(vib->dev, vib->regmap, vib->dsp.stop_cmd);

	pm_runtime_mark_last_busy(vib->dev);
	pm_runtime_put_autosuspend(vib->dev);

	kfree(work_data);
}

static int cs40l50_playback(struct input_dev *dev, int effect_id, int val)
{
	struct cs40l50_vibra *vib = input_get_drvdata(dev);
	struct cs40l50_work *work_data;

	work_data = kzalloc(sizeof(*work_data), GFP_ATOMIC);
	if (!work_data)
		return -ENOMEM;

	work_data->vib = vib;

	if (val > 0) {
		work_data->effect = &dev->ff->effects[effect_id];
		work_data->count = val;
		INIT_WORK(&work_data->work, cs40l50_start_worker);
	} else {
		/* Stop the amplifier as device drives only one effect */
		INIT_WORK(&work_data->work, cs40l50_stop_worker);
	}

	queue_work(vib->vib_wq, &work_data->work);

	return 0;
}

static void cs40l50_erase_worker(struct work_struct *work)
{
	struct cs40l50_work *work_data = container_of(work, struct cs40l50_work, work);
	struct cs40l50_effect *erase_effect, *owt_effect;
	struct cs40l50_vibra *vib = work_data->vib;
	int error;

	error = pm_runtime_resume_and_get(vib->dev);
	if (error)
		goto err_exit;

	erase_effect = cs40l50_find_effect(work_data->effect->id, &vib->effect_head);
	if (!erase_effect) {
		dev_err(vib->dev, "Effect to erase not found\n");
		error = -EINVAL;
		goto err_pm;
	}

	if (erase_effect->gpio_reg != CS40L50_GPIO_MAPPING_NONE) {
		error = regmap_write(vib->regmap, erase_effect->gpio_reg,
				     CS40L50_GPIO_DISABLE);
		if (error)
			goto err_pm;
	}

	if (erase_effect->type == CS40L50_WVFRM_BANK_OWT) {
		error = vib->dsp.write(vib->dev, vib->regmap,
				       vib->dsp.delete_owt_cmd |
				       (erase_effect->index & 0xFF));
		if (error)
			goto err_pm;

		list_for_each_entry(owt_effect, &vib->effect_head, list)
			if (owt_effect->type == CS40L50_WVFRM_BANK_OWT &&
			    owt_effect->index > erase_effect->index)
				owt_effect->index--;
	}

	list_del(&erase_effect->list);
	kfree(erase_effect);
err_pm:
	pm_runtime_mark_last_busy(vib->dev);
	pm_runtime_put_autosuspend(vib->dev);
err_exit:
	work_data->error = error;
}

static int cs40l50_erase(struct input_dev *dev, int effect_id)
{
	struct cs40l50_vibra *vib = input_get_drvdata(dev);
	struct cs40l50_work work_data;

	work_data.vib = vib;
	work_data.effect = &dev->ff->effects[effect_id];

	INIT_WORK_ONSTACK(&work_data.work, cs40l50_erase_worker);

	/* Push to workqueue to serialize with playbacks */
	queue_work(vib->vib_wq, &work_data.work);
	flush_work(&work_data.work);
	destroy_work_on_stack(&work_data.work);

	return work_data.error;
}

static void cs40l50_remove_wq(void *data)
{
	flush_workqueue(data);
	destroy_workqueue(data);
}

static int cs40l50_vibra_probe(struct platform_device *pdev)
{
	struct cs40l50 *cs40l50 = dev_get_drvdata(pdev->dev.parent);
	struct cs40l50_vibra *vib;
	int error;

	vib = devm_kzalloc(pdev->dev.parent, sizeof(*vib), GFP_KERNEL);
	if (!vib)
		return -ENOMEM;

	vib->dev = cs40l50->dev;
	vib->regmap = cs40l50->regmap;
	vib->dsp = cs40l50_dsp;

	vib->input = devm_input_allocate_device(vib->dev);
	if (!vib->input)
		return -ENOMEM;

	vib->input->id.product = cs40l50->devid;
	vib->input->id.version = cs40l50->revid;
	vib->input->name = "cs40l50_vibra";

	input_set_drvdata(vib->input, vib);
	input_set_capability(vib->input, EV_FF, FF_PERIODIC);
	input_set_capability(vib->input, EV_FF, FF_CUSTOM);

	error = input_ff_create(vib->input, CS40L50_EFFECTS_MAX);
	if (error) {
		dev_err(vib->dev, "Failed to create input device\n");
		return error;
	}

	vib->input->ff->upload = cs40l50_add;
	vib->input->ff->playback = cs40l50_playback;
	vib->input->ff->erase = cs40l50_erase;

	INIT_LIST_HEAD(&vib->effect_head);

	vib->vib_wq = alloc_ordered_workqueue("vib_wq", WQ_HIGHPRI);
	if (!vib->vib_wq)
		return -ENOMEM;

	error = devm_add_action_or_reset(vib->dev, cs40l50_remove_wq, vib->vib_wq);
	if (error)
		return error;

	error = input_register_device(vib->input);
	if (error)
		return error;

	return 0;
}

static const struct platform_device_id cs40l50_vibra_id_match[] = {
	{ "cs40l50-vibra", },
	{}
};
MODULE_DEVICE_TABLE(platform, cs40l50_vibra_id_match);

static struct platform_driver cs40l50_vibra_driver = {
	.probe		= cs40l50_vibra_probe,
	.id_table	= cs40l50_vibra_id_match,
	.driver		= {
		.name	= "cs40l50-vibra",
	},
};
module_platform_driver(cs40l50_vibra_driver);

MODULE_DESCRIPTION("CS40L50 Advanced Haptic Driver");
MODULE_AUTHOR("James Ogletree, Cirrus Logic Inc. <james.ogletree@cirrus.com>");
MODULE_LICENSE("GPL");
