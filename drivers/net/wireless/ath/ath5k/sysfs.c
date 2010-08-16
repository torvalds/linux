#include <linux/device.h>
#include <linux/pci.h>

#include "base.h"
#include "ath5k.h"
#include "reg.h"

#define SIMPLE_SHOW_STORE(name, get, set)				\
static ssize_t ath5k_attr_show_##name(struct device *dev,		\
			struct device_attribute *attr,			\
			char *buf)					\
{									\
	struct ath5k_softc *sc = dev_get_drvdata(dev);			\
	return snprintf(buf, PAGE_SIZE, "%d\n", get); 			\
}									\
									\
static ssize_t ath5k_attr_store_##name(struct device *dev,		\
			struct device_attribute *attr,			\
			const char *buf, size_t count)			\
{									\
	struct ath5k_softc *sc = dev_get_drvdata(dev);			\
	int val;							\
									\
	val = (int)simple_strtoul(buf, NULL, 10);			\
	set(sc->ah, val);						\
	return count;							\
}									\
static DEVICE_ATTR(name, S_IRUGO | S_IWUSR,				\
		   ath5k_attr_show_##name, ath5k_attr_store_##name)

#define SIMPLE_SHOW(name, get)						\
static ssize_t ath5k_attr_show_##name(struct device *dev,		\
			struct device_attribute *attr,			\
			char *buf)					\
{									\
	struct ath5k_softc *sc = dev_get_drvdata(dev);			\
	return snprintf(buf, PAGE_SIZE, "%d\n", get); 			\
}									\
static DEVICE_ATTR(name, S_IRUGO, ath5k_attr_show_##name, NULL)

/*** ANI ***/

SIMPLE_SHOW_STORE(ani_mode, sc->ani_state.ani_mode, ath5k_ani_init);
SIMPLE_SHOW_STORE(noise_immunity_level, sc->ani_state.noise_imm_level,
			ath5k_ani_set_noise_immunity_level);
SIMPLE_SHOW_STORE(spur_level, sc->ani_state.spur_level,
			ath5k_ani_set_spur_immunity_level);
SIMPLE_SHOW_STORE(firstep_level, sc->ani_state.firstep_level,
			ath5k_ani_set_firstep_level);
SIMPLE_SHOW_STORE(ofdm_weak_signal_detection, sc->ani_state.ofdm_weak_sig,
			ath5k_ani_set_ofdm_weak_signal_detection);
SIMPLE_SHOW_STORE(cck_weak_signal_detection, sc->ani_state.cck_weak_sig,
			ath5k_ani_set_cck_weak_signal_detection);
SIMPLE_SHOW(spur_level_max, sc->ani_state.max_spur_level);

static ssize_t ath5k_attr_show_noise_immunity_level_max(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", ATH5K_ANI_MAX_NOISE_IMM_LVL);
}
static DEVICE_ATTR(noise_immunity_level_max, S_IRUGO,
		   ath5k_attr_show_noise_immunity_level_max, NULL);

static ssize_t ath5k_attr_show_firstep_level_max(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", ATH5K_ANI_MAX_FIRSTEP_LVL);
}
static DEVICE_ATTR(firstep_level_max, S_IRUGO,
		   ath5k_attr_show_firstep_level_max, NULL);

static struct attribute *ath5k_sysfs_entries_ani[] = {
	&dev_attr_ani_mode.attr,
	&dev_attr_noise_immunity_level.attr,
	&dev_attr_spur_level.attr,
	&dev_attr_firstep_level.attr,
	&dev_attr_ofdm_weak_signal_detection.attr,
	&dev_attr_cck_weak_signal_detection.attr,
	&dev_attr_noise_immunity_level_max.attr,
	&dev_attr_spur_level_max.attr,
	&dev_attr_firstep_level_max.attr,
	NULL
};

static struct attribute_group ath5k_attribute_group_ani = {
	.name = "ani",
	.attrs = ath5k_sysfs_entries_ani,
};


/*** register / unregister ***/

int
ath5k_sysfs_register(struct ath5k_softc *sc)
{
	struct device *dev = &sc->pdev->dev;
	int err;

	err = sysfs_create_group(&dev->kobj, &ath5k_attribute_group_ani);
	if (err) {
		ATH5K_ERR(sc, "failed to create sysfs group\n");
		return err;
	}

	return 0;
}

void
ath5k_sysfs_unregister(struct ath5k_softc *sc)
{
	struct device *dev = &sc->pdev->dev;

	sysfs_remove_group(&dev->kobj, &ath5k_attribute_group_ani);
}
