int u300_gpio_config_get(struct gpio_chip *chip,
			 unsigned offset,
			 unsigned long *config);
int u300_gpio_config_set(struct gpio_chip *chip, unsigned offset,
			 enum pin_config_param param);
