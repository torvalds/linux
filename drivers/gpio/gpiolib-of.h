/* SPDX-License-Identifier: GPL-2.0 */

#ifndef GPIOLIB_OF_H
#define GPIOLIB_OF_H

struct gpio_chip;
enum of_gpio_flags;

#ifdef CONFIG_OF_GPIO
struct gpio_desc *of_find_gpio(struct device *dev,
			       const char *con_id,
			       unsigned int idx,
			       unsigned long *lookupflags);
int of_gpiochip_add(struct gpio_chip *gc);
void of_gpiochip_remove(struct gpio_chip *gc);
int of_gpio_get_count(struct device *dev, const char *con_id);
bool of_gpio_need_valid_mask(const struct gpio_chip *gc);
#else
static inline struct gpio_desc *of_find_gpio(struct device *dev,
					     const char *con_id,
					     unsigned int idx,
					     unsigned long *lookupflags)
{
	return ERR_PTR(-ENOENT);
}
static inline int of_gpiochip_add(struct gpio_chip *gc) { return 0; }
static inline void of_gpiochip_remove(struct gpio_chip *gc) { }
static inline int of_gpio_get_count(struct device *dev, const char *con_id)
{
	return 0;
}
static inline bool of_gpio_need_valid_mask(const struct gpio_chip *gc)
{
	return false;
}
#endif /* CONFIG_OF_GPIO */

#endif /* GPIOLIB_OF_H */
